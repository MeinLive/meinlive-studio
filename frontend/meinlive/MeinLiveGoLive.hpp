/******************************************************************************
    MeinLive Studio - Live gehen (Stream-Key, Titel, Kategorie, Beenden)
    Copyright (C) 2026 MeinLive

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;

namespace MeinLive {

/* RTMP-Eingang von meinlive.de (SRS), wie in services.json */
constexpr const char *IngestUrl = "rtmp://stream.meinlive.de:1935/live";
constexpr const char *ServiceName = "MeinLive";

/* true, wenn als Streaming-Dienst MeinLive eingestellt ist */
bool IsMeinLiveServiceSelected();

/* Streaming-Dienst auf MeinLive umstellen (nach dem Anmelden) */
void SelectMeinLiveService();

/* Vor dem Start einer Übertragung (OBSBasic::StartStreaming):
 * angemeldet + MeinLive gewählt -> Titel/Kategorie abfragen und frischen Stream-Key holen.
 * false = Start abbrechen. */
bool PrepareStreamStart(QWidget *parent);

/* true, wenn MeinLive Studio den Stream-Key selbst besorgt (dann keine Warnung "kein Stream-Key") */
bool ProvidesStreamKey();

/* Frontend-Ereignisse (Stream gestoppt -> Sendung auf MeinLive sofort beenden) */
void RegisterStreamEvents();

class GoLiveDialog : public QDialog {
	Q_OBJECT

public:
	explicit GoLiveDialog(QWidget *parent = nullptr);

	QString Title() const;
	int CategoryId() const;
	bool RecordVod() const;
	bool AskAgain() const;

private:
	void LoadCategories();

	QLineEdit *title = nullptr;
	QComboBox *category = nullptr;
	QCheckBox *record = nullptr;
	QCheckBox *askAgain = nullptr;
};

} // namespace MeinLive
