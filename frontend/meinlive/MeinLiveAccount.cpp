/******************************************************************************
    MeinLive Studio - MeinLive-Konto (Anmeldung, Token)
    Copyright (C) 2026 MeinLive

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "MeinLiveAccount.hpp"
#include "MeinLiveUpdate.hpp"

#include <OBSApp.hpp>

#include <QByteArray>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHostInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <util/config-file.h>

#ifdef _WIN32
#include <Windows.h>
#include <dpapi.h>
#endif

#include "moc_MeinLiveAccount.cpp"

namespace MeinLive {

namespace {

constexpr const char *Section = "MeinLive";

#ifdef _WIN32
QByteArray Protect(const QByteArray &plain)
{
	DATA_BLOB in{(DWORD)plain.size(), (BYTE *)plain.data()};
	DATA_BLOB out{};
	if (!CryptProtectData(&in, L"MeinLive Studio", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
		return QByteArray();
	}
	QByteArray result((const char *)out.pbData, (int)out.cbData);
	LocalFree(out.pbData);
	return result;
}

QByteArray Unprotect(const QByteArray &cipher)
{
	DATA_BLOB in{(DWORD)cipher.size(), (BYTE *)cipher.data()};
	DATA_BLOB out{};
	if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
		return QByteArray();
	}
	QByteArray result((const char *)out.pbData, (int)out.cbData);
	LocalFree(out.pbData);
	return result;
}
#else
QByteArray Protect(const QByteArray &plain)
{
	return plain;
}

QByteArray Unprotect(const QByteArray &cipher)
{
	return cipher;
}
#endif

QString JsonString(const nlohmann::json &object, const char *key)
{
	if (object.is_object() && object.contains(key) && object[key].is_string()) {
		return QString::fromStdString(object[key].get<std::string>());
	}
	return QString();
}

} // namespace

Account *Account::Get()
{
	static Account *instance = new Account(qApp);
	return instance;
}

void Account::Load()
{
	config_t *config = App()->GetAppConfig();
	const char *stored = config_get_string(config, Section, "Token");
	token.clear();
	if (stored && *stored) {
		QByteArray plain = Unprotect(QByteArray::fromBase64(QByteArray(stored)));
		token = plain.toStdString();
	}
	const char *name = config_get_string(config, Section, "Username");
	const char *display = config_get_string(config, Section, "DisplayName");
	username = QString::fromUtf8(name ? name : "");
	displayName = QString::fromUtf8(display ? display : "");
	if (token.empty()) {
		username.clear();
		displayName.clear();
	}
	emit Changed();
}

void Account::Save()
{
	config_t *config = App()->GetAppConfig();
	if (token.empty()) {
		config_remove_value(config, Section, "Token");
		config_remove_value(config, Section, "Username");
		config_remove_value(config, Section, "DisplayName");
	} else {
		QByteArray cipher = Protect(QByteArray::fromStdString(token)).toBase64();
		config_set_string(config, Section, "Token", cipher.constData());
		config_set_string(config, Section, "Username", username.toUtf8().constData());
		config_set_string(config, Section, "DisplayName", displayName.toUtf8().constData());
	}
	config_save_safe(config, "tmp", nullptr);
}

void Account::SetLogin(const std::string &newToken, const nlohmann::json &user)
{
	token = newToken;
	username = JsonString(user, "username");
	displayName = JsonString(user, "display_name");
	Save();
	blog(LOG_INFO, "[MeinLive] Angemeldet als %s", username.toUtf8().constData());
	emit Changed();
}

void Account::Logout(bool revokeOnServer)
{
	if (token.empty()) {
		return;
	}
	if (revokeOnServer) {
		std::string oldToken = token;
		RunAsync(
			this, [oldToken]() { return ApiRequest("POST", "/api/v1/auth/logout", nullptr, oldToken); },
			[](const HttpResponse &) {});
	}
	token.clear();
	username.clear();
	displayName.clear();
	Save();
	blog(LOG_INFO, "[MeinLive] Abgemeldet");
	emit Changed();
}

bool Account::HandleApiError(const HttpResponse &response)
{
	if (response.status == 426) {
		ShowUpdateRequired();
		return true;
	}
	if (response.status == 401 && IsLoggedIn()) {
		/* Token abgelaufen oder auf der Webseite widerrufen */
		Logout(false);
		return false;
	}
	return false;
}

/* ------------------------------------------------------------------------ */

LoginDialog::LoginDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(QTStr("MeinLive.Login.Title"));
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
	setMinimumWidth(440);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setSpacing(12);

	QLabel *logo = new QLabel(this);
	logo->setPixmap(QPixmap(":/meinlive/images/meinlive-wordmark.png")
				.scaledToWidth(260, Qt::SmoothTransformation));
	logo->setAlignment(Qt::AlignCenter);
	layout->addWidget(logo);

	pages = new QStackedWidget(this);
	layout->addWidget(pages);

	/* Seite 1: Zugangsdaten */
	QWidget *loginPage = new QWidget(pages);
	QVBoxLayout *loginLayout = new QVBoxLayout(loginPage);
	loginLayout->setContentsMargins(0, 0, 0, 0);
	QLabel *intro = new QLabel(QTStr("MeinLive.Login.Intro"), loginPage);
	intro->setWordWrap(true);
	loginLayout->addWidget(intro);

	QFormLayout *form = new QFormLayout();
	identifier = new QLineEdit(loginPage);
	password = new QLineEdit(loginPage);
	password->setEchoMode(QLineEdit::Password);
	form->addRow(QTStr("MeinLive.Login.Identifier"), identifier);
	form->addRow(QTStr("MeinLive.Login.Password"), password);
	loginLayout->addLayout(form);

	QLabel *links = new QLabel(QString("<a href='%1/register'>%2</a> &nbsp;·&nbsp; <a href='%1/passwort-vergessen'>%3</a>")
					   .arg(QString(BaseUrl), QTStr("MeinLive.Login.Register"),
						QTStr("MeinLive.Login.Forgot")),
				   loginPage);
	links->setOpenExternalLinks(true);
	loginLayout->addWidget(links);

	loginButton = new QPushButton(QTStr("MeinLive.Login.Submit"), loginPage);
	loginButton->setDefault(true);
	loginButton->setObjectName("meinliveLoginButton");
	loginLayout->addWidget(loginButton);
	pages->addWidget(loginPage);

	/* Seite 2: Zwei-Faktor-Code */
	QWidget *codePage = new QWidget(pages);
	QVBoxLayout *codeLayout = new QVBoxLayout(codePage);
	codeLayout->setContentsMargins(0, 0, 0, 0);
	QLabel *codeIntro = new QLabel(QTStr("MeinLive.Login.CodeIntro"), codePage);
	codeIntro->setWordWrap(true);
	codeLayout->addWidget(codeIntro);
	code = new QLineEdit(codePage);
	code->setPlaceholderText("123456");
	code->setMaxLength(32);
	codeLayout->addWidget(code);
	codeButton = new QPushButton(QTStr("MeinLive.Login.Confirm"), codePage);
	codeButton->setObjectName("meinliveLoginButton");
	codeLayout->addWidget(codeButton);
	pages->addWidget(codePage);

	error = new QLabel(this);
	error->setWordWrap(true);
	error->setProperty("class", "text-danger");
	error->hide();
	layout->addWidget(error);

	connect(loginButton, &QPushButton::clicked, this, &LoginDialog::SubmitLogin);
	connect(password, &QLineEdit::returnPressed, this, &LoginDialog::SubmitLogin);
	connect(codeButton, &QPushButton::clicked, this, &LoginDialog::SubmitCode);
	connect(code, &QLineEdit::returnPressed, this, &LoginDialog::SubmitCode);

	identifier->setFocus();
}

void LoginDialog::SetBusy(bool busy)
{
	loginButton->setEnabled(!busy);
	codeButton->setEnabled(!busy);
	identifier->setEnabled(!busy);
	password->setEnabled(!busy);
	code->setEnabled(!busy);
	setCursor(busy ? Qt::WaitCursor : Qt::ArrowCursor);
	if (busy) {
		error->hide();
	}
}

void LoginDialog::ShowError(const QString &message)
{
	error->setText(message);
	error->show();
}

void LoginDialog::SubmitLogin()
{
	if (identifier->text().trimmed().isEmpty() || password->text().isEmpty()) {
		ShowError(QTStr("MeinLive.Login.Missing"));
		return;
	}

	nlohmann::json body = {
		{"identifier", identifier->text().trimmed().toStdString()},
		{"password", password->text().toStdString()},
		{"device_label", ("MeinLive Studio (" + QHostInfo::localHostName() + ")").toStdString()},
	};

	SetBusy(true);
	RunAsync(
		this, [body]() { return ApiRequest("POST", "/api/v1/auth/login", body); },
		[this](const HttpResponse &response) {
			SetBusy(false);
			nlohmann::json json = response.json();
			if (response.ok() && json.value("requires_2fa", false)) {
				pages->setCurrentIndex(1);
				code->setFocus();
				return;
			}
			Finish(response);
		});
}

void LoginDialog::SubmitCode()
{
	if (code->text().trimmed().isEmpty()) {
		ShowError(QTStr("MeinLive.Login.Missing"));
		return;
	}

	nlohmann::json body = {
		{"code", code->text().trimmed().toStdString()},
		{"device_label", ("MeinLive Studio (" + QHostInfo::localHostName() + ")").toStdString()},
	};

	SetBusy(true);
	RunAsync(
		this, [body]() { return ApiRequest("POST", "/api/v1/auth/2fa", body); },
		[this](const HttpResponse &response) {
			SetBusy(false);
			Finish(response);
		});
}

void LoginDialog::Finish(const HttpResponse &response)
{
	nlohmann::json json = response.json();

	if (response.ok() && json.contains("token") && json["token"].is_string()) {
		Account::Get()->SetLogin(json["token"].get<std::string>(),
					 json.contains("user") ? json["user"] : nlohmann::json::object());
		accept();
		return;
	}

	if (Account::Get()->HandleApiError(response)) {
		reject();
		return;
	}

	if (response.status == 0) {
		ShowError(QTStr("MeinLive.Error.Connection"));
	} else if (response.status == 429) {
		ShowError(QTStr("MeinLive.Error.RateLimited"));
	} else {
		std::string message = response.errorMessage();
		ShowError(message.empty() || message == "invalid_credentials" || message == "invalid_code"
				  ? QTStr("MeinLive.Login.Failed")
				  : QString::fromStdString(message));
	}
}

} // namespace MeinLive
