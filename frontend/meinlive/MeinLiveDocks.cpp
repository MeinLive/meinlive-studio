/******************************************************************************
    MeinLive Studio - Docks (Chat, Live-Steuerung)
    Copyright (C) 2026 MeinLive

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "MeinLiveDocks.hpp"
#include "MeinLiveAccount.hpp"
#include "MeinLiveHttp.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#ifdef BROWSER_AVAILABLE
#include <browser-panel.hpp>
#include <docks/BrowserDock.hpp>

extern QCef *cef;
extern QCefCookieManager *panel_cookies;
#endif

#include <util/config-file.h>

#include <QDockWidget>

#include <algorithm>

namespace MeinLive {

#ifdef BROWSER_AVAILABLE

namespace {

struct DockInfo {
	const char *objectName;
	const char *titleKey;
	const char *target; /* Ziel der Web-Übergabe, siehe AppHandoffController::TARGETS */
	const char *path;   /* Rückfall ohne Übergabe */
	int width;
	int height;
	bool floating; /* beim ersten Anzeigen als eigenes Fenster */
};

const DockInfo docks[] = {
	{"meinliveChatDock", "MeinLive.Dock.Chat", "studio-chat", "/studio/chat", 380, 640, true},
	{"meinliveLiveDock", "MeinLive.Dock.Live", "studio-live", "/studio/live", 340, 420, false},
};

void CreateDock(OBSBasic *main, const DockInfo &info, const std::string &url, bool firstTime, bool raise)
{
	if (!cef || main->IsDockObjectNameUsed(info.objectName)) {
		return;
	}

	BrowserDock *dock = new BrowserDock(QTStr(info.titleKey));
	dock->setObjectName(info.objectName);
	dock->resize(info.width, info.height);
	dock->setMinimumSize(220, 200);
	dock->setWindowTitle(QTStr(info.titleKey));
	dock->setAllowedAreas(Qt::AllDockWidgetAreas);

	QCefWidget *browser = cef->create_widget(dock, url, panel_cookies);
	dock->SetWidget(browser);
	main->AddDockWidget(dock, Qt::RightDockWidgetArea);

	/* Gemerkte Anordnung vom letzten Mal: OBS stellt sie beim Start wieder her,
	 * BEVOR diese Docks existieren - Qt kann sie nachträglich anwenden.
	 * (Fehler in 1.0.0: dort blieben die Docks ab dem zweiten Start unsichtbar.) */
	if (!firstTime && main->restoreDockWidget(dock)) {
		if (raise) {
			dock->setVisible(true);
			dock->raise();
		}
		return;
	}

	/* Erstes Mal (oder nichts gemerkt): Chat als eigenes Fenster rechts neben dem
	 * Hauptfenster (z. B. für einen zweiten Bildschirm), Live-Steuerung angedockt */
	if (info.floating) {
		dock->setFloating(true);
		QRect frame = main->frameGeometry();
		dock->resize(info.width, std::max(info.height, frame.height() - 120));
		dock->move(frame.right() - info.width - 40, frame.top() + 80);
	}
	dock->setVisible(true);
}

} // namespace

void ShowDocks(OBSBasic *main, bool raise)
{
	if (!cef || !Account::Get()->IsLoggedIn()) {
		return;
	}

	OBSBasic::InitBrowserPanelSafeBlock();

	config_t *config = App()->GetAppConfig();
	/* Beim allerersten Mal sichtbar, danach merkt sich OBS die Anordnung selbst */
	/* "DocksLayout2": neuer Schlüssel ab 1.0.2, damit die in 1.0.0 versehentlich
	 * unsichtbar gespeicherten Docks einmalig wieder erscheinen */
	bool firstTime = !config_get_bool(config, "MeinLive", "DocksLayout2");
	config_set_bool(config, "MeinLive", "DocksLayout2", true);
	config_save_safe(config, "tmp", nullptr);

	std::string token = Account::Get()->Token();

	for (const DockInfo &info : docks) {
		if (main->IsDockObjectNameUsed(info.objectName)) {
			if (raise) {
				QDockWidget *dock = main->findChild<QDockWidget *>(info.objectName);
				if (dock) {
					dock->setVisible(true);
					dock->raise();
				}
			}
			continue;
		}

		std::string target = info.target;
		std::string fallback = std::string(BaseUrl) + info.path;
		RunAsync(
			main,
			[token, target]() {
				return ApiRequest("POST", "/api/v1/me/web-handoff", nlohmann::json{{"target", target}},
						  token);
			},
			[main, info, fallback, firstTime, raise](const HttpResponse &response) {
				nlohmann::json json = response.json();
				std::string url = fallback;
				if (response.ok() && json.contains("url") && json["url"].is_string()) {
					url = json["url"].get<std::string>();
				} else if (Account::Get()->HandleApiError(response)) {
					return;
				}
				CreateDock(main, info, url, firstTime, raise);
			});
	}
}

void ToggleChatWindow(OBSBasic *main)
{
	QDockWidget *dock = main->findChild<QDockWidget *>(docks[0].objectName);
	if (!dock) {
		ShowDocks(main, true);
		return;
	}
	dock->setFloating(!dock->isFloating());
	dock->setVisible(true);
	dock->raise();
}

void RemoveDocks(OBSBasic *main)
{
	for (const DockInfo &info : docks) {
		if (main->IsDockObjectNameUsed(info.objectName)) {
			main->RemoveDockWidget(info.objectName);
		}
	}
	if (panel_cookies) {
		panel_cookies->DeleteCookies("meinlive.de", std::string());
	}
	config_set_bool(App()->GetAppConfig(), "MeinLive", "DocksLayout2", false);
}

#else

void ShowDocks(OBSBasic *, bool) {}
void ToggleChatWindow(OBSBasic *) {}
void RemoveDocks(OBSBasic *) {}

#endif

} // namespace MeinLive
