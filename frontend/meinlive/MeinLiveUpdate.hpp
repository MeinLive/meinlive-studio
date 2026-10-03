/******************************************************************************
    MeinLive Studio - Updates (wie die Android-App)
    Copyright (C) 2026 MeinLive

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

namespace MeinLive {

/* Fragt meinlive.de/download/studio-latest.json ab (geschrieben von
 * meinlive-tools/publish-studio.ps1) und bietet eine neuere Version an.
 * "Später" blendet genau diese Version für drei Tage aus - wie in der App.
 * manual = über "Hilfe -> Nach Updates suchen": dann auch "kein Update" melden. */
void CheckForUpdates(bool manual);

/* Pflicht-Update: Die API hat mit HTTP 426 geantwortet (zu alte Version,
 * siehe App\Support\AppVersionGate). Fenster ohne "Später". */
void ShowUpdateRequired();

} // namespace MeinLive
