/******************************************************************************
    MeinLive Studio - Docks (Chat, Live-Steuerung)
    Copyright (C) 2026 MeinLive

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

class OBSBasic;

namespace MeinLive {

/* Chat und Live-Steuerung als Browser-Docks (Seiten /studio/chat und /studio/live
 * auf meinlive.de). Angemeldet wird über die Web-Übergabe der App
 * (POST /api/v1/me/web-handoff), die Sitzung bleibt im Browser-Speicher der Docks.
 * raise = vorhandene Docks wieder einblenden und nach vorne holen. */
void ShowDocks(OBSBasic *main, bool raise);

/* Chat zwischen eigenem Fenster und angedocktem Platz umschalten */
void ToggleChatWindow(OBSBasic *main);

/* Beim Abmelden: Docks entfernen und Sitzung löschen */
void RemoveDocks(OBSBasic *main);

} // namespace MeinLive
