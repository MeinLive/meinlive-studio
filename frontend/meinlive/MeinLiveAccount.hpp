/******************************************************************************
    MeinLive Studio - MeinLive-Konto (Anmeldung, Token)
    Copyright (C) 2026 MeinLive

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

#include "MeinLiveHttp.hpp"

#include <QDialog>
#include <QObject>
#include <QString>

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;

namespace MeinLive {

/* Angemeldetes MeinLive-Konto. Das API-Token liegt verschlüsselt (Windows DPAPI,
 * nur für diesen Windows-Benutzer lesbar) in global.ini, Abschnitt [MeinLive]. */
class Account : public QObject {
	Q_OBJECT

public:
	static Account *Get();

	bool IsLoggedIn() const { return !token.empty(); }
	const std::string &Token() const { return token; }
	QString Username() const { return username; }
	QString DisplayName() const { return displayName.isEmpty() ? username : displayName; }

	void Load();
	void SetLogin(const std::string &newToken, const nlohmann::json &user);
	void Logout(bool revokeOnServer = true);

	/* Gemeinsame Behandlung von API-Fehlern: 401 -> abmelden, 426 -> Pflicht-Update.
	 * Liefert true, wenn der Fehler damit erledigt ist (keine weitere Meldung nötig). */
	bool HandleApiError(const HttpResponse &response);

signals:
	void Changed();

private:
	explicit Account(QObject *parent = nullptr) : QObject(parent) {}
	void Save();

	std::string token;
	QString username;
	QString displayName;
};

/* Anmeldefenster: Benutzername/E-Mail + Passwort, danach ggf. 2FA-Code */
class LoginDialog : public QDialog {
	Q_OBJECT

public:
	explicit LoginDialog(QWidget *parent = nullptr);

private slots:
	void SubmitLogin();
	void SubmitCode();

private:
	void SetBusy(bool busy);
	void ShowError(const QString &message);
	void Finish(const HttpResponse &response);

	QStackedWidget *pages = nullptr;
	QLineEdit *identifier = nullptr;
	QLineEdit *password = nullptr;
	QLineEdit *code = nullptr;
	QLabel *error = nullptr;
	QPushButton *loginButton = nullptr;
	QPushButton *codeButton = nullptr;
};

} // namespace MeinLive
