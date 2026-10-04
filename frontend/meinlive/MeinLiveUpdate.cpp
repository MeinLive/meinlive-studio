/******************************************************************************
    MeinLive Studio - Updates (wie die Android-App)
    Copyright (C) 2026 MeinLive

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "MeinLiveUpdate.hpp"
#include "MeinLiveHttp.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QPixmap>
#include <QProgressDialog>
#include <QPushButton>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <util/config-file.h>

#include <atomic>
#include <memory>
#include <utility>

#ifdef _WIN32
#include <Windows.h>
#include <shellapi.h>
#endif

namespace MeinLive {

namespace {

constexpr const char *Section = "MeinLive";
constexpr const char *LatestUrl = "https://meinlive.de/download/studio-latest.json";
constexpr const char *DownloadPage = "https://meinlive.de/studio";
constexpr qint64 SnoozeSeconds = 3LL * 24 * 60 * 60;

struct Release {
	bool valid = false;
	int code = 0;
	QString name;
	QString installerUrl;
	QString sha256;
	QStringList notes;
};

QString Absolute(const std::string &path)
{
	QString url = QString::fromStdString(path);
	return url.startsWith("http") ? url : QString(BaseUrl) + url;
}

Release ParseRelease(const HttpResponse &response)
{
	Release release;
	if (!response.ok()) {
		return release;
	}
	nlohmann::json json = response.json();
	if (!json.is_object() || !json.contains("version_code") || !json["version_code"].is_number_integer()) {
		return release;
	}

	release.code = json["version_code"].get<int>();
	release.name = QString::fromStdString(json.value("version_name", std::string()));
	release.installerUrl = Absolute(json.value("installer_url", std::string()));
	release.sha256 = QString::fromStdString(json.value("sha256", std::string())).toLower();

	/* Neuerungen in der Sprache der Oberfläche (deutsch, sonst englisch) */
	bool german = QString(App()->GetLocale()).startsWith("de");
	const char *key = german ? "notes" : "notes_en";
	if (!json.contains(key) || !json[key].is_array() || json[key].empty()) {
		key = "notes";
	}
	if (json.contains(key) && json[key].is_array()) {
		for (const auto &note : json[key]) {
			if (note.is_string() && release.notes.size() < 8) {
				release.notes << QString::fromStdString(note.get<std::string>());
			}
		}
	}

	release.valid = !release.installerUrl.isEmpty();
	return release;
}

bool IsSnoozed(const Release &release)
{
	config_t *config = App()->GetAppConfig();
	int code = (int)config_get_int(config, Section, "SnoozedCode");
	qint64 at = config_get_int(config, Section, "SnoozedAt");
	return code == release.code && QDateTime::currentSecsSinceEpoch() - at < SnoozeSeconds;
}

void Snooze(const Release &release)
{
	config_t *config = App()->GetAppConfig();
	config_set_int(config, Section, "SnoozedCode", release.code);
	config_set_int(config, Section, "SnoozedAt", QDateTime::currentSecsSinceEpoch());
	config_save_safe(config, "tmp", nullptr);
}

bool LaunchInstaller(const QString &path)
{
#ifdef _WIN32
	/* ShellExecute statt QProcess: der Installer braucht Administratorrechte (UAC) */
	std::wstring file = QDir::toNativeSeparators(path).toStdWString();
	HINSTANCE result = ShellExecuteW(nullptr, L"open", file.c_str(), L"/SILENT /SP- /NOCANCEL /UPDATE", nullptr,
					 SW_SHOWNORMAL);
	return (INT_PTR)result > 32;
#else
	return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
#endif
}

void DownloadAndInstall(const Release &release)
{
	OBSBasic *main = OBSBasic::Get();

	if (main->Active()) {
		QMessageBox::information(main, QTStr("MeinLive.Update.Title"), QTStr("MeinLive.Update.StopOutputsFirst"));
		return;
	}

	QString target = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
				 .filePath(QString("meinlive-studio-%1-setup.exe").arg(release.name));

	auto *progress = new QProgressDialog(QTStr("MeinLive.Update.Downloading").arg(release.name), QTStr("Cancel"),
					     0, 100, main);
	progress->setWindowTitle(QTStr("MeinLive.Update.Title"));
	progress->setWindowModality(Qt::WindowModal);
	progress->setMinimumDuration(0);
	progress->setAttribute(Qt::WA_DeleteOnClose);
	progress->setValue(0);

	auto cancelled = std::make_shared<std::atomic<bool>>(false);
	QObject::connect(progress, &QProgressDialog::canceled, progress, [cancelled]() { *cancelled = true; });

	QPointer<QProgressDialog> guard(progress);
	std::string url = release.installerUrl.toStdString();
	std::string file = target.toStdString();

	RunAsync(
		progress,
		[url, file, cancelled, guard]() {
			std::string error;
			bool ok = HttpDownload(
				url, file,
				[cancelled, guard](int64_t now, int64_t total) {
					if (total > 0) {
						int percent = (int)(now * 100 / total);
						QMetaObject::invokeMethod(
							qApp,
							[guard, percent]() {
								if (guard) {
									guard->setValue(percent);
								}
							},
							Qt::QueuedConnection);
					}
					return !*cancelled;
				},
				error);
			return std::make_pair(ok, error);
		},
		[progress, release, target, cancelled](const std::pair<bool, std::string> &result) {
			progress->close();
			OBSBasic *main = OBSBasic::Get();

			if (*cancelled) {
				QFile::remove(target);
				return;
			}
			if (!result.first) {
				blog(LOG_WARNING, "[MeinLive] Update-Download fehlgeschlagen: %s", result.second.c_str());
				QMessageBox::warning(main, QTStr("MeinLive.Update.Title"),
						     QTStr("MeinLive.Update.DownloadFailed").arg(DownloadPage));
				return;
			}

			/* Prüfsumme wie bei der APK (sha256 aus studio-latest.json) */
			if (!release.sha256.isEmpty()) {
				QFile installer(target);
				QString actual;
				if (installer.open(QIODevice::ReadOnly)) {
					QCryptographicHash hash(QCryptographicHash::Sha256);
					hash.addData(&installer);
					actual = QString::fromLatin1(hash.result().toHex());
				}
				installer.close();
				if (actual != release.sha256) {
					blog(LOG_WARNING, "[MeinLive] Update verworfen: Prüfsumme stimmt nicht");
					QFile::remove(target);
					QMessageBox::warning(main, QTStr("MeinLive.Update.Title"),
							     QTStr("MeinLive.Update.DownloadFailed").arg(DownloadPage));
					return;
				}
			}

			if (!LaunchInstaller(target)) {
				QMessageBox::warning(main, QTStr("MeinLive.Update.Title"),
						     QTStr("MeinLive.Update.DownloadFailed").arg(DownloadPage));
				return;
			}

			/* Der Installer wartet, bis dieses Programm beendet ist, installiert und startet
			 * MeinLive Studio danach von selbst neu. Normal beenden (speichert Szenen usw.);
			 * hängt das Beenden, nach 15 s hart beenden, damit das Update nicht ausbleibt. */
			blog(LOG_INFO, "[MeinLive] Update auf %s wird installiert", release.name.toUtf8().constData());
			QTimer::singleShot(0, main, [main]() { main->close(); });
			QTimer::singleShot(15000, qApp, []() {
				blog(LOG_WARNING, "[MeinLive] Beenden für das Update dauert zu lange - erzwinge Ende");
				QCoreApplication::exit(0);
			});
		});
}

void ShowReleaseDialog(const Release &release, bool required)
{
	OBSBasic *main = OBSBasic::Get();

	QString text = required ? QTStr("MeinLive.Update.RequiredText") : QTStr("MeinLive.Update.Text");
	if (!release.notes.isEmpty()) {
		text += "<ul>";
		for (const QString &note : release.notes) {
			text += "<li>" + note.toHtmlEscaped() + "</li>";
		}
		text += "</ul>";
	}

	QMessageBox box(main);
	box.setWindowTitle(QTStr("MeinLive.Update.Title"));
	box.setIconPixmap(QPixmap(":/meinlive/images/meinlive-studio-256.png")
				  .scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation));
	box.setTextFormat(Qt::RichText);
	box.setText("<h3>" + QTStr("MeinLive.Update.Available").arg(release.name.toHtmlEscaped()) + "</h3>" + text);
	QPushButton *install = box.addButton(QTStr("MeinLive.Update.Install"), QMessageBox::AcceptRole);
	QPushButton *later = box.addButton(required ? QTStr("Close") : QTStr("MeinLive.Update.Later"),
					   QMessageBox::RejectRole);
	box.setDefaultButton(install);
	box.exec();

	if (box.clickedButton() == install) {
		DownloadAndInstall(release);
	} else if (box.clickedButton() == later && !required) {
		Snooze(release);
	}
}

bool checking = false;

} // namespace

void CheckForUpdates(bool manual)
{
	if (checking) {
		return;
	}
	checking = true;

	RunAsync(
		OBSBasic::Get(), []() { return HttpGet(LatestUrl); },
		[manual](const HttpResponse &response) {
			checking = false;
			Release release = ParseRelease(response);
			OBSBasic *main = OBSBasic::Get();

			if (!release.valid) {
				/* Fehler (offline usw.) beim automatischen Prüfen bewusst verschlucken */
				if (manual) {
					QMessageBox::warning(main, QTStr("MeinLive.Update.Title"),
							     QTStr("MeinLive.Update.CheckFailed").arg(DownloadPage));
				}
				return;
			}

			if (release.code <= MEINLIVE_STUDIO_VERSION_CODE) {
				if (manual) {
					QMessageBox::information(main, QTStr("MeinLive.Update.Title"),
								 QTStr("MeinLive.Update.UpToDate").arg(MEINLIVE_STUDIO_VERSION));
				}
				return;
			}

			if (!manual && IsSnoozed(release)) {
				return;
			}

			ShowReleaseDialog(release, false);
		});
}

void ShowUpdateRequired()
{
	static bool shown = false;
	if (shown) {
		return;
	}
	shown = true;

	RunAsync(
		OBSBasic::Get(), []() { return HttpGet(LatestUrl); },
		[](const HttpResponse &response) {
			Release release = ParseRelease(response);
			if (release.valid) {
				ShowReleaseDialog(release, true);
			} else {
				QDesktopServices::openUrl(QUrl(DownloadPage));
			}
			shown = false;
		});
}

} // namespace MeinLive
