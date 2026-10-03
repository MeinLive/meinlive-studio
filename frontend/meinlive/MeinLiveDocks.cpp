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

void CreateDock(OBSBasic *main, const DockInfo &info, const std::string &url, bool show)
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

	/* Chat beim ersten Mal als eigenes Fenster rechts neben dem Hauptfenster
	 * (z. B. für einen zweiten Bildschirm), danach gilt die gemerkte Anordnung */
	if (show && info.floating) {
		dock->setFloating(true);
		QRect frame = main->frameGeometry();
		dock->resize(info.width, std::max(info.height, frame.height() - 120));
		dock->move(frame.right() - info.width - 40, frame.top() + 80);
	}
	dock->setVisible(show);
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
	bool firstTime = !config_get_bool(config, "MeinLive", "DocksCreated");
	config_set_bool(config, "MeinLive", "DocksCreated", true);
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
		bool show = firstTime || raise;
		RunAsync(
			main,
			[token, target]() {
				return ApiRequest("POST", "/api/v1/me/web-handoff", nlohmann::json{{"target", target}},
						  token);
			},
			[main, info, fallback, show](const HttpResponse &response) {
				nlohmann::json json = response.json();
				std::string url = fallback;
				if (response.ok() && json.contains("url") && json["url"].is_string()) {
					url = json["url"].get<std::string>();
				} else if (Account::Get()->HandleApiError(response)) {
					return;
				}
				CreateDock(main, info, url, show);
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
	config_set_bool(App()->GetAppConfig(), "MeinLive", "DocksCreated", false);
}

#else

void ShowDocks(OBSBasic *, bool) {}
void ToggleChatWindow(OBSBasic *) {}
void RemoveDocks(OBSBasic *) {}

#endif

} // namespace MeinLive
