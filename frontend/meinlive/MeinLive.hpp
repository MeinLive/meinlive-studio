/******************************************************************************
    MeinLive Studio - Einstieg aller MeinLive-Funktionen
    Copyright (C) 2026 MeinLive

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

/* Bewusst schmale Schnittstelle: Der OBS-Code ruft nur diese Funktionen auf,
 * damit spätere OBS-Versionen mit wenig Aufwand übernommen werden können.
 * Eingriffe in OBS-Dateien sind mit "MeinLive Studio" kommentiert. */

#include "MeinLiveGoLive.hpp"
#include "MeinLiveUpdate.hpp"

class OBSBasic;

namespace MeinLive {

/* Nach dem Laden des Hauptfensters (OBSBasic::OnFirstLoad) */
void Initialize(OBSBasic *main);

} // namespace MeinLive
