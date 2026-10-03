/******************************************************************************
    MeinLive Studio - Einblendungen und Szenen-Vorlagen
    Copyright (C) 2026 MeinLive

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "MeinLiveScenes.hpp"
#include "MeinLiveAccount.hpp"
#include "MeinLiveHttp.hpp"

#include <OBSApp.hpp>
#include <utility/platform.hpp>
#include <widgets/OBSBasic.hpp>

#include <obs-frontend-api.h>

#include <QButtonGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QStringList>
#include <QVBoxLayout>

#include <util/config-file.h>

#include <algorithm>
#include <iterator>
#include <vector>

namespace MeinLive {

namespace {

constexpr const char *Section = "MeinLive";

struct Overlay {
	std::string type;  /* chat, gifts, goal, poll */
	std::string name;  /* Anzeigename der Quelle */
	std::string url;   /* geheimer Link */
	int width = 0;     /* empfohlene Größe */
	int height = 0;
};

enum class Format { Portrait, Landscape };

std::vector<Overlay> ParseOverlays(const nlohmann::json &json)
{
	std::vector<Overlay> overlays;
	if (!json.contains("overlays") || !json["overlays"].is_array()) {
		return overlays;
	}
	for (const auto &entry : json["overlays"]) {
		if (!entry.is_object()) {
			continue;
		}
		Overlay overlay;
		overlay.type = entry.value("type", std::string());
		overlay.name = entry.value("name", overlay.type);
		overlay.url = entry.value("url", std::string());
		overlay.width = entry.value("width", 500);
		overlay.height = entry.value("height", 500);
		if (!overlay.type.empty() && !overlay.url.empty()) {
			overlays.push_back(overlay);
		}
	}
	return overlays;
}

std::string SourceName(const Overlay &overlay)
{
	return "MeinLive - " + overlay.name;
}

/* Lage einer Einblendung im Bild, abhängig vom Format (wie auf meinlive.de empfohlen):
 * Chat unten links, Geschenke mittig, Ziel oben, Umfrage rechts bzw. oben */
void PlaceOverlay(obs_sceneitem_t *item, const Overlay &overlay, uint32_t cx, uint32_t cy)
{
	const bool portrait = cy > cx;
	const float unit = portrait ? cx / 720.0f : cy / 1080.0f;
	const float margin = 24.0f * unit;

	float scale = unit;
	if (portrait) {
		/* Im Hochformat etwas kleiner, damit das Gesicht frei bleibt */
		scale = unit * (overlay.type == "chat" ? 0.8f : overlay.type == "gifts" ? 0.85f : 0.9f);
	}
	const float w = overlay.width * scale;
	const float h = overlay.height * scale;

	vec2 pos;
	if (overlay.type == "chat") {
		vec2_set(&pos, margin, cy - h - (portrait ? 220.0f * unit : margin));
	} else if (overlay.type == "gifts") {
		vec2_set(&pos, (cx - w) / 2.0f, (cy - h) / 2.0f);
	} else if (overlay.type == "goal") {
		vec2_set(&pos, (cx - w) / 2.0f, portrait ? 110.0f * unit : margin);
	} else if (overlay.type == "poll") {
		if (portrait) {
			vec2_set(&pos, (cx - w) / 2.0f, 260.0f * unit);
		} else {
			vec2_set(&pos, cx - w - margin, (cy - h) / 2.0f);
		}
	} else {
		vec2_set(&pos, margin, margin);
	}

	vec2 scaleVec;
	vec2_set(&scaleVec, scale, scale);
	obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_NONE);
	obs_sceneitem_set_scale(item, &scaleVec);
	obs_sceneitem_set_pos(item, &pos);
}

/* Browser-Quelle anlegen oder (falls schon vorhanden) mit neuem Link aktualisieren */
OBSSource CreateOrUpdateOverlaySource(const Overlay &overlay)
{
	std::string name = SourceName(overlay);
	OBSSourceAutoRelease existing = obs_get_source_by_name(name.c_str());

	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "url", overlay.url.c_str());
	obs_data_set_int(settings, "width", overlay.width);
	obs_data_set_int(settings, "height", overlay.height);
	/* Geschenk-Töne mit in den Stream */
	obs_data_set_bool(settings, "reroute_audio", overlay.type == "gifts");

	if (existing) {
		obs_source_update(existing, settings);
		return OBSSource(existing.Get());
	}

	const char *id = obs_get_latest_input_type_id("browser_source");
	if (!id) {
		return OBSSource();
	}
	OBSSourceAutoRelease source = obs_source_create(id, name.c_str(), settings, nullptr);
	return OBSSource(source.Get());
}

void AddOverlaysToScene(obs_scene_t *scene, const std::vector<Overlay> &overlays)
{
	obs_video_info ovi;
	if (!obs_get_video_info(&ovi)) {
		return;
	}

	for (const Overlay &overlay : overlays) {
		OBSSource source = CreateOrUpdateOverlaySource(overlay);
		if (!source) {
			continue;
		}
		if (obs_scene_find_source(scene, obs_source_get_name(source))) {
			continue; /* schon in dieser Szene */
		}
		obs_sceneitem_t *item = obs_scene_add(scene, source);
		if (item) {
			PlaceOverlay(item, overlay, ovi.base_width, ovi.base_height);
		}
	}
}

/* Studio-Daten (Einblendungen usw.) holen, Ergebnis im UI-Thread */
template<typename Done> void FetchStudioInfo(QWidget *parent, Done done)
{
	Account *account = Account::Get();
	std::string token = account->Token();
	RunAsync(
		parent, [token]() { return ApiRequest("GET", "/api/v1/me/studio", nullptr, token); },
		[parent, done](const HttpResponse &response) {
			if (response.ok()) {
				done(response.json());
				return;
			}
			if (Account::Get()->HandleApiError(response)) {
				return;
			}
			QMessageBox::warning(parent, QTStr("MeinLive.Overlays.Title"),
					     response.status == 0 ? QTStr("MeinLive.Error.Connection")
								  : QTStr("MeinLive.Overlays.Failed").arg(response.status));
		});
}

bool EnsureLoggedIn(QWidget *parent)
{
	if (Account::Get()->IsLoggedIn()) {
		return true;
	}
	LoginDialog login(parent);
	return login.exec() == QDialog::Accepted;
}

/* ---- Vorlage ------------------------------------------------------------ */

OBSSource CreateInput(const char *unversionedId, const char *name, obs_data_t *settings)
{
	const char *id = obs_get_latest_input_type_id(unversionedId);
	if (!id) {
		return OBSSource();
	}
	OBSSourceAutoRelease source = obs_source_create(id, name, settings, nullptr);
	return OBSSource(source.Get());
}

/* Quelle bildfüllend (Kamera) oder eingepasst (Logo) */
void FitToCanvas(obs_sceneitem_t *item, uint32_t cx, uint32_t cy, obs_bounds_type type)
{
	vec2 bounds, pos;
	vec2_set(&bounds, (float)cx, (float)cy);
	vec2_set(&pos, 0.0f, 0.0f);
	obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
	obs_sceneitem_set_bounds_type(item, type);
	obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
	obs_sceneitem_set_bounds(item, &bounds);
	obs_sceneitem_set_pos(item, &pos);
}

void AddBackground(obs_scene_t *scene, uint32_t cx, uint32_t cy)
{
	OBSDataAutoRelease color = obs_data_create();
	obs_data_set_int(color, "color", 0xFF0E0C0C); /* #0c0c0e (ABGR) wie meinlive.de */
	obs_data_set_int(color, "width", cx);
	obs_data_set_int(color, "height", cy);
	OBSSourceAutoRelease existing = obs_get_source_by_name("MeinLive - Hintergrund");
	OBSSource background = existing ? OBSSource(existing.Get())
					: CreateInput("color_source", "MeinLive - Hintergrund", color);
	if (background) {
		obs_scene_add(scene, background);
	}
}

void AddLogo(obs_scene_t *scene, uint32_t cx, uint32_t cy, float relativeY)
{
	std::string path;
	if (!GetDataFilePath("meinlive/meinlive-wordmark.png", path)) {
		return;
	}
	OBSSourceAutoRelease existing = obs_get_source_by_name("MeinLive - Logo");
	OBSSource logo;
	if (existing) {
		logo = OBSSource(existing.Get());
	} else {
		OBSDataAutoRelease settings = obs_data_create();
		obs_data_set_string(settings, "file", path.c_str());
		logo = CreateInput("image_source", "MeinLive - Logo", settings);
	}
	if (!logo) {
		return;
	}
	obs_sceneitem_t *item = obs_scene_add(scene, logo);
	const float width = std::min(cx * 0.7f, 900.0f);
	const float height = width / 3.0f;
	vec2 bounds, pos;
	vec2_set(&bounds, width, height);
	vec2_set(&pos, (cx - width) / 2.0f, cy * relativeY - height / 2.0f);
	obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
	obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
	obs_sceneitem_set_bounds(item, &bounds);
	obs_sceneitem_set_pos(item, &pos);
}

void AddText(obs_scene_t *scene, const char *name, const QString &text, uint32_t cx, uint32_t cy, float relativeY)
{
	OBSDataAutoRelease font = obs_data_create();
	obs_data_set_string(font, "face", "Segoe UI");
	obs_data_set_int(font, "size", (int)(cy > cx ? cx / 9 : cy / 10));
	obs_data_set_int(font, "flags", 1); /* fett */

	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "text", text.toUtf8().constData());
	obs_data_set_obj(settings, "font", font);
	obs_data_set_int(settings, "color", 0xFFF6F4F4); /* #f4f4f6 */
	obs_data_set_string(settings, "align", "center");

	OBSSource source = CreateInput("text_gdiplus", name, settings);
	if (!source) {
		source = CreateInput("text_ft2_source", name, settings);
	}
	if (!source) {
		return;
	}
	obs_sceneitem_t *item = obs_scene_add(scene, source);
	vec2 pos;
	vec2_set(&pos, cx / 2.0f, cy * relativeY);
	obs_sceneitem_set_alignment(item, OBS_ALIGN_CENTER);
	obs_sceneitem_set_pos(item, &pos);
}

QString UniqueCollectionName(const QString &base)
{
	char **names = obs_frontend_get_scene_collections();
	QStringList existing;
	for (char **name = names; name && *name; name++) {
		existing << QString::fromUtf8(*name);
	}
	bfree(names);

	QString candidate = base;
	for (int i = 2; existing.contains(candidate); i++) {
		candidate = QString("%1 (%2)").arg(base).arg(i);
	}
	return candidate;
}

void ApplyVideoFormat(Format format)
{
	config_t *profile = obs_frontend_get_profile_config();
	if (format == Format::Portrait) {
		/* wie die MeinLive-App: 720 x 1280 */
		config_set_uint(profile, "Video", "BaseCX", 720);
		config_set_uint(profile, "Video", "BaseCY", 1280);
		config_set_uint(profile, "Video", "OutputCX", 720);
		config_set_uint(profile, "Video", "OutputCY", 1280);
	} else {
		config_set_uint(profile, "Video", "BaseCX", 1920);
		config_set_uint(profile, "Video", "BaseCY", 1080);
		config_set_uint(profile, "Video", "OutputCX", 1280);
		config_set_uint(profile, "Video", "OutputCY", 720);
	}
	const char *mode = config_get_string(profile, "Output", "Mode");
	if (!mode || strcmp(mode, "Advanced") != 0) {
		config_set_uint(profile, "SimpleOutput", "VBitrate", 3000);
	}
	config_save_safe(profile, "tmp", nullptr);
	obs_frontend_reset_video();
}

void BuildTemplate(QWidget *parent, Format format, const std::vector<Overlay> &overlays)
{
	QString collection = UniqueCollectionName(QTStr(format == Format::Portrait ? "MeinLive.Template.PortraitName"
										 : "MeinLive.Template.LandscapeName"));
	if (!obs_frontend_add_scene_collection(collection.toUtf8().constData())) {
		QMessageBox::warning(parent, QTStr("MeinLive.Template.Title"), QTStr("MeinLive.Template.Failed"));
		return;
	}

	ApplyVideoFormat(format);

	obs_video_info ovi;
	obs_get_video_info(&ovi);
	const uint32_t cx = ovi.base_width;
	const uint32_t cy = ovi.base_height;

	/* 1. "Live": Kamera bildfüllend + Einblendungen (nutzt die leere Standardszene) */
	OBSSourceAutoRelease firstScene = obs_frontend_get_current_scene();
	obs_scene_t *live = obs_scene_from_source(firstScene);
	if (firstScene) {
		obs_source_set_name(firstScene, QTStr("MeinLive.Template.SceneLive").toUtf8().constData());
	}

	OBSDataAutoRelease cameraSettings = obs_data_create();
	OBSSource camera = CreateInput("dshow_input", QTStr("MeinLive.Template.Camera").toUtf8().constData(),
				       cameraSettings);
	if (!camera) {
		camera = CreateInput("av_capture_input", QTStr("MeinLive.Template.Camera").toUtf8().constData(),
				     cameraSettings);
	}
	if (live) {
		AddBackground(live, cx, cy);
		if (camera) {
			obs_sceneitem_t *item = obs_scene_add(live, camera);
			FitToCanvas(item, cx, cy, OBS_BOUNDS_SCALE_OUTER);
		}
		AddOverlaysToScene(live, overlays);
	}

	/* 2. "Gleich geht's los" und 3. "Pause": Logo + Text + Chat */
	auto addInfoScene = [&](const char *sceneKey, const char *textKey) {
		QString sceneName = QTStr(sceneKey);
		OBSSceneAutoRelease scene = obs_scene_create(sceneName.toUtf8().constData());
		AddBackground(scene, cx, cy);
		AddLogo(scene, cx, cy, 0.30f);
		AddText(scene, (std::string("MeinLive - ") + sceneName.toStdString()).c_str(), QTStr(textKey), cx,
			cy, 0.55f);
		std::vector<Overlay> chatOnly;
		std::copy_if(overlays.begin(), overlays.end(), std::back_inserter(chatOnly),
			     [](const Overlay &o) { return o.type == "chat" || o.type == "goal"; });
		AddOverlaysToScene(scene, chatOnly);
	};
	addInfoScene("MeinLive.Template.SceneStarting", "MeinLive.Template.TextStarting");
	addInfoScene("MeinLive.Template.ScenePause", "MeinLive.Template.TextPause");

	if (firstScene) {
		obs_frontend_set_current_scene(firstScene);
	}

	config_set_bool(App()->GetAppConfig(), Section, "TemplateOffered", true);
	config_save_safe(App()->GetAppConfig(), "tmp", nullptr);

	blog(LOG_INFO, "[MeinLive] Szenen-Vorlage '%s' angelegt", collection.toUtf8().constData());

	/* Kamera auswählen lassen */
	if (camera) {
		obs_frontend_open_source_properties(camera);
	}
}

} // namespace

void AddOverlaysToCurrentScene(QWidget *parent)
{
	if (!EnsureLoggedIn(parent)) {
		return;
	}
	FetchStudioInfo(parent, [parent](const nlohmann::json &json) {
		std::vector<Overlay> overlays = ParseOverlays(json);
		OBSSourceAutoRelease current = obs_frontend_get_current_scene();
		obs_scene_t *scene = obs_scene_from_source(current);
		if (!scene || overlays.empty()) {
			QMessageBox::warning(parent, QTStr("MeinLive.Overlays.Title"),
					     QTStr("MeinLive.Overlays.Failed").arg(0));
			return;
		}
		AddOverlaysToScene(scene, overlays);
		QMessageBox::information(parent, QTStr("MeinLive.Overlays.Title"), QTStr("MeinLive.Overlays.Added"));
	});
}

void SetupSceneTemplate(QWidget *parent)
{
	if (OBSBasic::Get()->Active()) {
		QMessageBox::information(parent, QTStr("MeinLive.Template.Title"),
					 QTStr("MeinLive.Update.StopOutputsFirst"));
		return;
	}
	if (!EnsureLoggedIn(parent)) {
		return;
	}

	QDialog dialog(parent);
	dialog.setWindowTitle(QTStr("MeinLive.Template.Title"));
	dialog.setWindowFlags(dialog.windowFlags() & ~Qt::WindowContextHelpButtonHint);
	QVBoxLayout *layout = new QVBoxLayout(&dialog);
	QLabel *intro = new QLabel(QTStr("MeinLive.Template.Intro"), &dialog);
	intro->setWordWrap(true);
	layout->addWidget(intro);
	QRadioButton *portrait = new QRadioButton(QTStr("MeinLive.Template.Portrait"), &dialog);
	QRadioButton *landscape = new QRadioButton(QTStr("MeinLive.Template.Landscape"), &dialog);
	portrait->setChecked(true);
	layout->addWidget(portrait);
	layout->addWidget(landscape);
	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	buttons->button(QDialogButtonBox::Ok)->setText(QTStr("MeinLive.Template.Create"));
	buttons->button(QDialogButtonBox::Cancel)->setText(QTStr("Cancel"));
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);

	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	Format format = portrait->isChecked() ? Format::Portrait : Format::Landscape;

	FetchStudioInfo(parent, [parent, format](const nlohmann::json &json) {
		BuildTemplate(parent, format, ParseOverlays(json));
	});
}

void OfferSceneTemplateOnce(QWidget *parent)
{
	config_t *config = App()->GetAppConfig();
	if (config_get_bool(config, Section, "TemplateOffered")) {
		return;
	}
	config_set_bool(config, Section, "TemplateOffered", true);
	config_save_safe(config, "tmp", nullptr);

	QMessageBox::StandardButton answer = QMessageBox::question(
		parent, QTStr("MeinLive.Template.Title"), QTStr("MeinLive.Template.Offer"),
		QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
	if (answer == QMessageBox::Yes) {
		SetupSceneTemplate(parent);
	}
}

} // namespace MeinLive
