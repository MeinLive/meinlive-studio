"""Ersetzt "OBS" in den sichtbaren Texten der Oberfläche durch "MeinLive Studio".

Nach jedem Übernehmen einer neuen OBS-Version erneut ausführen:
    python meinlive/branding/rebrand_locale.py
Das Skript ist idempotent (mehrfaches Ausführen ändert nichts weiter).
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOCALE_DIR = ROOT / "frontend/data/locale"

# Texte, in denen "OBS" weiterhin OBS meint (Import aus OBS, OBS-Projekt, Plugins, fremde Dienste)
KEEP_KEYS = (
    "Importer.",
    "About.",
    "CrashHandling.Labels.PrivacyNotice",
    "TwitchAuth.",
    "YouTube.",
    "Restream",
    "PluginsFailedToLoad",
    "PluginManager.",
    "MeinLive.",  # eigene Texte nennen OBS bewusst (Hinweis auf die Grundlage)
)

NAME = "MeinLive Studio"

# Reihenfolge wichtig: erst Zusammensetzungen, dann das einzelne Wort
RULES = [
    # "OBS Studio-Fenster" -> "MeinLive-Studio-Fenster"
    (re.compile(r"\bOBS Studio-(?=\w)"), "MeinLive-Studio-"),
    (re.compile(r"\bOBS Studio\b"), NAME),
    # deutsche Komposita: "OBS-Fenster" -> "MeinLive-Studio-Fenster" (nicht bei Plugins/Projekt)
    (re.compile(r"\bOBS-(?!Plugin|Projekt\b|Project|WebSocket|Websocket)"), "MeinLive-Studio-"),
    (re.compile(r"\bOBS(?![-_.]?(?:Plugin|plugin|Project|Projekt|WebSocket|Websocket|-Plugin|-Projekt|Recording))\b(?!-)"), NAME),
]

LINE = re.compile(r'^([A-Za-z0-9_.]+)="(.*)"\s*$')


def rebrand_value(value: str) -> str:
    for pattern, repl in RULES:
        value = pattern.sub(repl, value)
    return value.replace("MeinLive Studio Studio", NAME)


def process(path: Path) -> int:
    lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
    changed = 0
    for i, line in enumerate(lines):
        m = LINE.match(line.rstrip("\r\n"))
        if not m or m.group(1).startswith(KEEP_KEYS):
            continue
        new_value = rebrand_value(m.group(2))
        if new_value != m.group(2):
            ending = line[len(line.rstrip("\r\n")):]
            lines[i] = f'{m.group(1)}="{new_value}"{ending}'
            changed += 1
    if changed:
        path.write_text("".join(lines), encoding="utf-8", newline="")
    return changed


def merge_meinlive_strings() -> None:
    """Eigene Texte (meinlive/locale/*.ini) ans Ende der OBS-Sprachdateien hängen.
    Fehlende Sprachen nutzen automatisch en-US."""
    for own in sorted((ROOT / "meinlive/locale").glob("*.ini")):
        target = LOCALE_DIR / own.name
        lines = target.read_text(encoding="utf-8").splitlines(keepends=True)
        ending = "\r\n" if lines and lines[0].endswith("\r\n") else "\n"
        kept = [line for line in lines if not line.startswith("MeinLive.")]
        if kept and not kept[-1].endswith(("\n", "\r")):
            kept[-1] += ending
        added = [line.rstrip("\r\n") + ending for line in own.read_text(encoding="utf-8").splitlines() if line.strip()]
        target.write_text("".join(kept + added), encoding="utf-8", newline="")
        print(f"{len(added)} MeinLive-Texte in {own.name}")


def main() -> None:
    total = sum(process(p) for p in sorted(LOCALE_DIR.glob("*.ini")))
    print(f"{total} Texte angepasst.")
    merge_meinlive_strings()


if __name__ == "__main__":
    main()
