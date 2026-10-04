/******************************************************************************
    MeinLive Studio - Einstieg aller MeinLive-Funktionen
    Copyright (C) 2026 MeinLive

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "MeinLive.hpp"
#include "MeinLiveAccount.hpp"
#include "MeinLiveDocks.hpp"
#include "MeinLiveScenes.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QMenu>
#include <QMenuBar>
#include <QTimer>
#include <QUrl>

#include <util/config-file.h>

#include <memory>

namespace MeinLive {

namespace {

QPointer<QMenu> menu;

void OpenUrl(const QString &path)
{
	QDesktopServices::openUrl(QUrl(QString(BaseUrl) + path));
}

void RebuildMenu(OBSBasic *main)
{
	if (!menu) {
		return;
	}
	menu->clear();
	Account *account = Account::Get();

	if (!account->IsLoggedIn()) {
		QAction *login = menu->addAction(QTStr("MeinLive.Menu.Login"));
		QObject::connect(login, &QAction::triggered, main, [main]() {
			LoginDialog dialog(main);
			dialog.exec();
		});
	} else {
		QAction *who = menu->addAction(QTStr("MeinLive.Menu.LoggedInAs").arg(account->DisplayName()));
		who->setEnabled(false);
		menu->addSeparator();

		QAction *overlays = menu->addAction(QTStr("MeinLive.Menu.AddOverlays"));
		QObject::connect(overlays, &QAction::triggered, main, [main]() { AddOverlaysToCurrentScene(main); });

		QAction *templates = menu->addAction(QTStr("MeinLive.Menu.SceneTemplate"));
		QObject::connect(templates, &QAction::triggered, main, [main]() { SetupSceneTemplate(main); });

		QAction *docks = menu->addAction(QTStr("MeinLive.Menu.ShowDocks"));
		QObject::connect(docks, &QAction::triggered, main, [main]() { ShowDocks(main, true); });

		QAction *chatWindow = menu->addAction(QTStr("MeinLive.Menu.ChatWindow"));
		QObject::connect(chatWindow, &QAction::triggered, main, [main]() { ToggleChatWindow(main); });

		menu->addSeparator();

		QAction *ask = menu->addAction(QTStr("MeinLive.Menu.AskBeforeLive"));
		ask->setCheckable(true);
		config_set_default_bool(App()->GetAppConfig(), "MeinLive", "AskBeforeLive", true);
		ask->setChecked(config_get_bool(App()->GetAppConfig(), "MeinLive", "AskBeforeLive"));
		QObject::connect(ask, &QAction::toggled, main, [](bool checked) {
			config_set_bool(App()->GetAppConfig(), "MeinLive", "AskBeforeLive", checked);
			config_save_safe(App()->GetAppConfig(), "tmp", nullptr);
		});

		QAction *channel = menu->addAction(QTStr("MeinLive.Menu.OpenChannel"));
		QString username = account->Username();
		QObject::connect(channel, &QAction::triggered, main,
				 [username]() { OpenUrl("/watch/" + QString::fromLatin1(QUrl::toPercentEncoding(username))); });

		QAction *goLive = menu->addAction(QTStr("MeinLive.Menu.OpenGoLive"));
		QObject::connect(goLive, &QAction::triggered, main, []() { OpenUrl("/go-live"); });

		menu->addSeparator();
		QAction *logout = menu->addAction(QTStr("MeinLive.Menu.Logout"));
		QObject::connect(logout, &QAction::triggered, main, []() { Account::Get()->Logout(); });
	}

	menu->addSeparator();
	QAction *update = menu->addAction(QTStr("MeinLive.Menu.CheckUpdates"));
	QObject::connect(update, &QAction::triggered, main, []() { CheckForUpdates(true); });
	QAction *help = menu->addAction(QTStr("MeinLive.Menu.Help"));
	QObject::connect(help, &QAction::triggered, main, []() { OpenUrl("/hilfe"); });
}

/* Anmeldung erst anbieten, wenn kein anderes Fenster (z. B. der Einrichtungs-
 * assistent beim ersten Start) offen ist */
void OfferLoginWhenIdle(OBSBasic *main)
{
	QTimer::singleShot(800, main, [main]() {
		if (QApplication::activeModalWidget() || !main->isVisible()) {
			OfferLoginWhenIdle(main);
			return;
		}
		if (!Account::Get()->IsLoggedIn()) {
			LoginDialog dialog(main);
			dialog.exec();
		}
	});
}

void CreateMenu(OBSBasic *main)
{
	QMenuBar *bar = main->menuBar();
	menu = new QMenu(QTStr("MeinLive.Menu.Title"), bar);
	menu->setObjectName("meinliveMenu");

	/* vor "Hilfe" einsortieren (letzter Eintrag der Menüleiste) */
	QList<QAction *> actions = bar->actions();
	if (!actions.isEmpty()) {
		bar->insertMenu(actions.last(), menu);
	} else {
		bar->addMenu(menu);
	}
	RebuildMenu(main);
}

} // namespace

void Initialize(OBSBasic *main)
{
	RepairTemplateAudioOnce();

	Account *account = Account::Get();
	account->Load();

	CreateMenu(main);
	RegisterStreamEvents();

	auto wasLoggedIn = std::make_shared<bool>(account->IsLoggedIn());
	QObject::connect(account, &Account::Changed, main, [main, wasLoggedIn]() {
		RebuildMenu(main);
		bool loggedIn = Account::Get()->IsLoggedIn();
		if (loggedIn && !*wasLoggedIn) {
			/* Frisch angemeldet: MeinLive als Dienst, Docks, Vorlage anbieten */
			SelectMeinLiveService();
			ShowDocks(main, false);
			OfferSceneTemplateOnce(main);
		} else if (!loggedIn && *wasLoggedIn) {
			RemoveDocks(main);
		}
		*wasLoggedIn = loggedIn;
	});

	if (account->IsLoggedIn()) {
		ShowDocks(main, false);
	} else {
		/* Erster Start: Anmeldung anbieten (einmalig) */
		config_t *config = App()->GetAppConfig();
		if (!config_get_bool(config, "MeinLive", "LoginOffered")) {
			config_set_bool(config, "MeinLive", "LoginOffered", true);
			config_save_safe(config, "tmp", nullptr);
			OfferLoginWhenIdle(main);
		}
	}
}

} // namespace MeinLive
