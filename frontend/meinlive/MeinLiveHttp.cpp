/******************************************************************************
    MeinLive Studio - Verbindung zu meinlive.de
    Copyright (C) 2026 MeinLive

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "MeinLiveHttp.hpp"

#include <OBSApp.hpp>

#include <QString>

#include <curl/curl.h>

#include <cstdio>
#include <mutex>

namespace MeinLive {

namespace {

/* Gemeinsamer Cookie-Speicher aller Aufrufe: Der 2FA-Schritt der API
 * (/api/v1/auth/2fa) erkennt die angefangene Anmeldung an der PHP-Sitzung. */
std::mutex shareMutex;

void LockShare(CURL *, curl_lock_data, curl_lock_access, void *)
{
	shareMutex.lock();
}

void UnlockShare(CURL *, curl_lock_data, void *)
{
	shareMutex.unlock();
}

CURLSH *CookieShare()
{
	static CURLSH *share = [] {
		CURLSH *s = curl_share_init();
		curl_share_setopt(s, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
		curl_share_setopt(s, CURLSHOPT_LOCKFUNC, LockShare);
		curl_share_setopt(s, CURLSHOPT_UNLOCKFUNC, UnlockShare);
		return s;
	}();
	return share;
}

size_t WriteString(char *data, size_t size, size_t count, void *user)
{
	static_cast<std::string *>(user)->append(data, size * count);
	return size * count;
}

size_t WriteFile(char *data, size_t size, size_t count, void *user)
{
	return fwrite(data, size, count, static_cast<FILE *>(user)) * size;
}

std::string UserAgent()
{
	return std::string("MeinLive-Studio/") + MEINLIVE_STUDIO_VERSION;
}

void ApplyCommon(CURL *curl, const std::string &url, char *errorBuffer)
{
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_USERAGENT, UserAgent().c_str());
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
}

} // namespace

nlohmann::json HttpResponse::json() const
{
	nlohmann::json parsed = nlohmann::json::parse(body, nullptr, false);
	return parsed.is_discarded() ? nlohmann::json::object() : parsed;
}

std::string HttpResponse::errorMessage() const
{
	nlohmann::json j = json();
	if (j.is_object()) {
		if (j.contains("message") && j["message"].is_string()) {
			return j["message"].get<std::string>();
		}
		if (j.contains("error") && j["error"].is_string()) {
			return j["error"].get<std::string>();
		}
	}
	return error;
}

HttpResponse ApiRequest(const std::string &method, const std::string &path, const nlohmann::json &body,
			const std::string &token)
{
	HttpResponse response;
	CURL *curl = curl_easy_init();
	if (!curl) {
		response.error = "curl_easy_init failed";
		return response;
	}

	char errorBuffer[CURL_ERROR_SIZE] = {};
	std::string url = std::string(BaseUrl) + path;
	ApplyCommon(curl, url, errorBuffer);
	curl_easy_setopt(curl, CURLOPT_SHARE, CookieShare());
	curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

	struct curl_slist *headers = nullptr;
	headers = curl_slist_append(headers, "Accept: application/json");
	headers = curl_slist_append(headers, "Content-Type: application/json");
	headers = curl_slist_append(headers, "X-App-Platform: studio");
	std::string versionHeader = "X-App-Version: " + std::to_string(MEINLIVE_STUDIO_VERSION_CODE);
	headers = curl_slist_append(headers, versionHeader.c_str());
	std::string languageHeader = std::string("Accept-Language: ") + App()->GetLocale();
	headers = curl_slist_append(headers, languageHeader.c_str());
	std::string authHeader;
	if (!token.empty()) {
		authHeader = "Authorization: Bearer " + token;
		headers = curl_slist_append(headers, authHeader.c_str());
	}
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	std::string payload;
	if (method != "GET") {
		payload = body.is_null() ? std::string("{}") : body.dump();
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)payload.size());
	}

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteString);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);

	CURLcode code = curl_easy_perform(curl);
	if (code == CURLE_OK) {
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
	} else {
		response.error = errorBuffer[0] ? errorBuffer : curl_easy_strerror(code);
	}

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return response;
}

HttpResponse HttpGet(const std::string &url)
{
	HttpResponse response;
	CURL *curl = curl_easy_init();
	if (!curl) {
		response.error = "curl_easy_init failed";
		return response;
	}

	char errorBuffer[CURL_ERROR_SIZE] = {};
	ApplyCommon(curl, url, errorBuffer);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteString);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);

	CURLcode code = curl_easy_perform(curl);
	if (code == CURLE_OK) {
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
	} else {
		response.error = errorBuffer[0] ? errorBuffer : curl_easy_strerror(code);
	}

	curl_easy_cleanup(curl);
	return response;
}

namespace {
struct ProgressContext {
	const std::function<bool(int64_t, int64_t)> *callback;
};

int ProgressCallback(void *user, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t)
{
	auto *context = static_cast<ProgressContext *>(user);
	if (context->callback && *context->callback && !(*context->callback)((int64_t)now, (int64_t)total)) {
		return 1; /* abbrechen */
	}
	return 0;
}
} // namespace

bool HttpDownload(const std::string &url, const std::string &filePath,
		  const std::function<bool(int64_t, int64_t)> &progress, std::string &error)
{
#ifdef _WIN32
	FILE *file = _wfopen(QString::fromStdString(filePath).toStdWString().c_str(), L"wb");
#else
	FILE *file = fopen(filePath.c_str(), "wb");
#endif
	if (!file) {
		error = "Datei kann nicht angelegt werden: " + filePath;
		return false;
	}

	CURL *curl = curl_easy_init();
	if (!curl) {
		fclose(file);
		error = "curl_easy_init failed";
		return false;
	}

	char errorBuffer[CURL_ERROR_SIZE] = {};
	ProgressContext context{&progress};
	ApplyCommon(curl, url, errorBuffer);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFile);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &context);

	CURLcode code = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	fclose(file);

	if (code != CURLE_OK) {
		error = errorBuffer[0] ? errorBuffer : curl_easy_strerror(code);
		return false;
	}
	return true;
}

} // namespace MeinLive
