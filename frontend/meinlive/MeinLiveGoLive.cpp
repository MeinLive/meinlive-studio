/******************************************************************************
    MeinLive Studio - Live gehen (Stream-Key, Titel, Kategorie, Beenden)
    Copyright (C) 2026 MeinLive

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "MeinLiveGoLive.hpp"
#include "MeinLiveAccount.hpp"
#include "MeinLiveHttp.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <obs-frontend-api.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QVBoxLayout>

#include <util/config-file.h>

#include <cstring>

#include "moc_MeinLiveGoLive.cpp"

namespace MeinLive {

namespace {

constexpr const char *Section = "MeinLive";

/* Vom Benutzer ausgelöstes Stoppen (nicht Verbindungsabbruch) */
bool userStopping = false;

config_t *Config()
{
	return App()->GetAppConfig();
}

} // namespace

bool IsMeinLiveServiceSelected()
{
	obs_service_t *service = obs_frontend_get_streaming_service();
	if (!service) {
		return false;
	}
	OBSDataAutoRelease settings = obs_service_get_settings(service);
	const char *name = obs_data_get_string(settings, "service");
	const char *server = obs_data_get_string(settings, "server");
	return (name && strcmp(name, ServiceName) == 0) || (server && strstr(server, "meinlive.de") != nullptr);
}

void SelectMeinLiveService()
{
	obs_service_t *current = obs_frontend_get_streaming_service();
	if (current && strcmp(obs_service_get_id(current), "rtmp_common") == 0 && IsMeinLiveServiceSelected()) {
		return;
	}

	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "service", ServiceName);
	obs_data_set_string(settings, "server", IngestUrl);
	obs_data_set_string(settings, "key", "");

	OBSServiceAutoRelease service = obs_service_create("rtmp_common", "default_service", settings, nullptr);
	if (!service) {
		return;
	}
	obs_frontend_set_streaming_service(service);
	obs_frontend_save_streaming_service();
	blog(LOG_INFO, "[MeinLive] Streaming-Dienst auf MeinLive umgestellt");
}

bool ProvidesStreamKey()
{
	return Account::Get()->IsLoggedIn() && IsMeinLiveServiceSelected();
}

/* ------------------------------------------------------------------------ */

GoLiveDialog::GoLiveDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(QTStr("MeinLive.GoLive.Title"));
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
	setMinimumWidth(460);

	QVBoxLayout *layout = new QVBoxLayout(this);

	QLabel *intro = new QLabel(QTStr("MeinLive.GoLive.Intro").arg(Account::Get()->DisplayName().toHtmlEscaped()),
				   this);
	intro->setWordWrap(true);
	layout->addWidget(intro);

	QFormLayout *form = new QFormLayout();
	title = new QLineEdit(this);
	title->setMaxLength(120);
	const char *lastTitle = config_get_string(Config(), Section, "LastTitle");
	title->setText(lastTitle && *lastTitle ? QString::fromUtf8(lastTitle) : QTStr("MeinLive.GoLive.DefaultTitle"));
	form->addRow(QTStr("MeinLive.GoLive.StreamTitle"), title);

	category = new QComboBox(this);
	category->addItem(QTStr("MeinLive.GoLive.NoCategory"), 0);
	form->addRow(QTStr("MeinLive.GoLive.Category"), category);
	layout->addLayout(form);

	record = new QCheckBox(QTStr("MeinLive.GoLive.RecordVod"), this);
	config_set_default_bool(Config(), Section, "RecordVod", true);
	record->setChecked(config_get_bool(Config(), Section, "RecordVod"));
	layout->addWidget(record);

	askAgain = new QCheckBox(QTStr("MeinLive.GoLive.AskAgain"), this);
	askAgain->setChecked(true);
	layout->addWidget(askAgain);

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	QPushButton *ok = buttons->button(QDialogButtonBox::Ok);
	ok->setText(QTStr("MeinLive.GoLive.Start"));
	ok->setObjectName("meinliveGoLiveButton");
	buttons->button(QDialogButtonBox::Cancel)->setText(QTStr("Cancel"));
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

	LoadCategories();
	title->setFocus();
	title->selectAll();
}

void GoLiveDialog::LoadCategories()
{
	int lastCategory = (int)config_get_int(Config(), Section, "LastCategory");
	std::string token = Account::Get()->Token();

	RunAsync(
		this, [token]() { return ApiRequest("GET", "/api/v1/categories", nullptr, token); },
		[this, lastCategory](const HttpResponse &response) {
			nlohmann::json json = response.json();
			if (!response.ok() || !json.contains("categories") || !json["categories"].is_array()) {
				return;
			}
			for (const auto &entry : json["categories"]) {
				if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_number_integer()) {
					continue;
				}
				int id = entry["id"].get<int>();
				category->addItem(QString::fromStdString(entry.value("name", std::string())), id);
				if (id == lastCategory) {
					category->setCurrentIndex(category->count() - 1);
				}
			}
		});
}

QString GoLiveDialog::Title() const
{
	QString text = title->text().trimmed();
	return text.isEmpty() ? QTStr("MeinLive.GoLive.DefaultTitle") : text;
}

int GoLiveDialog::CategoryId() const
{
	return category->currentData().toInt();
}

bool GoLiveDialog::RecordVod() const
{
	return record->isChecked();
}

bool GoLiveDialog::AskAgain() const
{
	return askAgain->isChecked();
}

/* ------------------------------------------------------------------------ */

bool PrepareStreamStart(QWidget *parent)
{
	Account *account = Account::Get();

	if (!IsMeinLiveServiceSelected()) {
		return true; /* Stream geht zu einem anderen Dienst - nichts zu tun */
	}

	if (!account->IsLoggedIn()) {
		/* MeinLive gewählt, aber kein Stream-Key eingetragen -> Anmeldung anbieten */
		OBSDataAutoRelease settings = obs_service_get_settings(obs_frontend_get_streaming_service());
		const char *key = obs_data_get_string(settings, "key");
		if (key && *key) {
			return true; /* klassisch mit Stream-Key von der Webseite */
		}
		if (!parent || !parent->isVisible()) {
			return true;
		}
		LoginDialog login(parent);
		if (login.exec() != QDialog::Accepted) {
			return false;
		}
	}

	config_set_default_bool(Config(), Section, "AskBeforeLive", true);
	bool ask = config_get_bool(Config(), Section, "AskBeforeLive");

	QString title;
	int categoryId = (int)config_get_int(Config(), Section, "LastCategory");
	bool recordVod = config_get_bool(Config(), Section, "RecordVod");
	const char *lastTitle = config_get_string(Config(), Section, "LastTitle");
	title = lastTitle && *lastTitle ? QString::fromUtf8(lastTitle) : QTStr("MeinLive.GoLive.DefaultTitle");

	if (ask && parent && parent->isVisible()) {
		GoLiveDialog dialog(parent);
		if (dialog.exec() != QDialog::Accepted) {
			return false;
		}
		title = dialog.Title();
		categoryId = dialog.CategoryId();
		recordVod = dialog.RecordVod();
		config_set_string(Config(), Section, "LastTitle", title.toUtf8().constData());
		config_set_int(Config(), Section, "LastCategory", categoryId);
		config_set_bool(Config(), Section, "RecordVod", recordVod);
		config_set_bool(Config(), Section, "AskBeforeLive", dialog.AskAgain());
		config_save_safe(Config(), "tmp", nullptr);
	}

	nlohmann::json body = {{"title", title.toStdString()}, {"record_vod", recordVod}};
	if (categoryId > 0) {
		body["category_id"] = categoryId;
	}

	/* Stream-Key holen; das UI wartet mit einem Hinweis (dauert meist < 1 s) */
	QProgressDialog busy(QTStr("MeinLive.GoLive.Preparing"), QString(), 0, 0, parent);
	busy.setWindowTitle(QTStr("MeinLive.GoLive.Title"));
	busy.setWindowModality(Qt::WindowModal);
	busy.setMinimumDuration(400);
	busy.setCancelButton(nullptr);

	HttpResponse response;
	QEventLoop loop;
	std::string token = account->Token();
	RunAsync(
		&loop, [token, body]() { return ApiRequest("POST", "/api/v1/me/go-live", body, token); },
		[&response, &loop](const HttpResponse &result) {
			response = result;
			loop.quit();
		});
	loop.exec();
	busy.close();

	nlohmann::json json = response.json();
	if (response.ok() && json.contains("stream_key") && json["stream_key"].is_string()) {
		obs_service_t *service = obs_frontend_get_streaming_service();
		OBSDataAutoRelease settings = obs_service_get_settings(service);
		obs_data_set_string(settings, "service", ServiceName);
		if (json.contains("rtmp_url") && json["rtmp_url"].is_string()) {
			obs_data_set_string(settings, "server", json["rtmp_url"].get<std::string>().c_str());
		}
		obs_data_set_string(settings, "key", json["stream_key"].get<std::string>().c_str());
		obs_service_update(service, settings);
		obs_frontend_save_streaming_service();
		blog(LOG_INFO, "[MeinLive] Stream-Key für Sendung %d erhalten", json.value("stream_id", 0));
		return true;
	}

	if (account->HandleApiError(response)) {
		return false;
	}

	QString message;
	if (response.status == 0) {
		message = QTStr("MeinLive.Error.Connection");
	} else if (response.status == 401) {
		message = QTStr("MeinLive.Error.LoggedOut");
	} else if (response.status == 403) {
		std::string text = response.errorMessage();
		message = text.empty() || text == "age_restricted" ? QTStr("MeinLive.GoLive.AgeRestricted")
								   : QString::fromStdString(text);
	} else {
		message = QTStr("MeinLive.GoLive.Failed").arg(response.status);
	}
	QMessageBox::warning(parent, QTStr("MeinLive.GoLive.Title"), message);
	return false;
}

/* ------------------------------------------------------------------------ */

namespace {

void EndStreamOnServer()
{
	Account *account = Account::Get();
	if (!account->IsLoggedIn()) {
		return;
	}
	std::string token = account->Token();
	RunAsync(
		account, [token]() { return ApiRequest("POST", "/api/v1/me/go-live/end", nullptr, token); },
		[](const HttpResponse &response) {
			if (response.ok()) {
				blog(LOG_INFO, "[MeinLive] Sendung auf meinlive.de beendet");
			} else {
				Account::Get()->HandleApiError(response);
			}
		});
}

void OnFrontendEvent(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_STREAMING_STARTING:
		userStopping = false;
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPING:
		/* Nur beim bewussten Stoppen: ohne Abmeldung warten Zuschauer sonst
		 * 3 Minuten auf "gleich wieder da" (Funkloch-Überbrückung der Webseite) */
		userStopping = true;
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		if (userStopping && IsMeinLiveServiceSelected()) {
			EndStreamOnServer();
		}
		userStopping = false;
		break;
	default:
		break;
	}
}

} // namespace

void RegisterStreamEvents()
{
	obs_frontend_add_event_callback(OnFrontendEvent, nullptr);
}

} // namespace MeinLive
