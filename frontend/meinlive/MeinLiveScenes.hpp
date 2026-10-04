/******************************************************************************
    MeinLive Studio - Einblendungen und Szenen-Vorlagen
    Copyright (C) 2026 MeinLive

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

class QWidget;

namespace MeinLive {

/* Einblendungen (Chat, Geschenke, Geschenk-Ziel, Umfrage) als Browser-Quellen in
 * die aktuelle Szene legen. Die geheimen Links kommen von /api/v1/me/studio.
 * Vorhandene MeinLive-Einblendungen werden nur aktualisiert, nicht verdoppelt. */
void AddOverlaysToCurrentScene(QWidget *parent);

/* Neue Szenensammlung im Hoch- oder Querformat mit Kamera und Einblendungen */
void SetupSceneTemplate(QWidget *parent);

/* Nach dem ersten Anmelden einmalig die Vorlage anbieten */
void OfferSceneTemplateOnce(QWidget *parent);

/* Vorlagen-Sammlungen ohne Tonquellen (bis 1.0.5) einmalig um Desktop-Audio + Mikrofon ergänzen */
void RepairTemplateAudioOnce();

} // namespace MeinLive
