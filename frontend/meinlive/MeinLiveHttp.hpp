/******************************************************************************
    MeinLive Studio - Verbindung zu meinlive.de
    Copyright (C) 2026 MeinLive

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

#include <QCoreApplication>
#include <QObject>
#include <QPointer>
#include <QThread>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <string>

namespace MeinLive {

/* Basisadresse der Webseite (Ziel aller API-Aufrufe) */
constexpr const char *BaseUrl = "https://meinlive.de";

struct HttpResponse {
	long status = 0;   /* HTTP-Status, 0 = keine Verbindung */
	std::string body;  /* Antworttext */
	std::string error; /* curl-Fehlertext */

	bool ok() const { return status >= 200 && status < 300; }
	/* Antwort als JSON, bei Fehlern ein leeres Objekt */
	nlohmann::json json() const;
	/* "message" bzw. "error" aus einer JSON-Fehlerantwort */
	std::string errorMessage() const;
};

/* Synchroner Aufruf der MeinLive-API (nie im UI-Thread aufrufen, siehe RunAsync).
 * path beginnt mit "/" (z. B. "/api/v1/me"). Schickt immer X-App-Platform: studio
 * und X-App-Version (Pflicht-Update per HTTP 426, wie in der Android-App). */
HttpResponse ApiRequest(const std::string &method, const std::string &path, const nlohmann::json &body = nullptr,
			const std::string &token = std::string());

/* Beliebige Adresse abrufen (z. B. studio-latest.json) */
HttpResponse HttpGet(const std::string &url);

/* Datei herunterladen. progress(now, total) -> false bricht ab. */
bool HttpDownload(const std::string &url, const std::string &filePath,
		  const std::function<bool(int64_t, int64_t)> &progress, std::string &error);

/* Arbeit in einem Hintergrund-Thread, Ergebnis im UI-Thread an done() -
 * aber nur, solange context noch existiert. */
template<typename Work, typename Done> void RunAsync(QObject *context, Work work, Done done)
{
	QPointer<QObject> guard(context);
	QThread *thread = QThread::create([guard, work, done]() {
		auto result = work();
		QMetaObject::invokeMethod(
			qApp,
			[guard, done, result]() {
				if (guard) {
					done(result);
				}
			},
			Qt::QueuedConnection);
	});
	QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
	thread->start();
}

} // namespace MeinLive
