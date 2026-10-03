"""Erzeugt alle Programm-Symbole von MeinLive Studio aus dem MeinLive-Logo.

Vorlage: logo-source.png (= meinlive-tools/grafiken/normales_logo.png, 2170x725).
Daraus werden freigestellt:
  - Emblem (Krone + Play-Kreis) für große Symbole (Programm, Installer, Über-Dialog)
  - nur der Play-Kreis für kleine Symbole (16-32 px, Infobereich), dort wäre die Krone Matsch

Aufruf (im Projektordner):  python meinlive/branding/make_icons.py
Benötigt: pip install pillow
"""

from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageEnhance, ImageOps

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SOURCE = HERE / "logo-source.png"

# Play-Kreis im Logo (gemessen: links 185, rechts 680, unten 705)
CX, CY, R = 433, 459, 249


def _mask(size, draw_fn, ss=2):
    w, h = size
    m = Image.new("L", (w * ss, h * ss), 0)
    draw_fn(ImageDraw.Draw(m), ss)
    return m.resize(size, Image.LANCZOS)


def _square(img: Image.Image, pad: int) -> Image.Image:
    e = img.crop(img.getbbox())
    n = max(e.size) + 2 * pad
    sq = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    sq.alpha_composite(e, ((n - e.width) // 2, (n - e.height) // 2))
    return sq


def load_parts():
    src = Image.open(SOURCE).convert("RGBA")
    w, h = src.size

    # Unten rechts überdeckt der "LIVE"-Balken den Kreisrand: dort das gespiegelte linke Kreisstück einsetzen
    mirrored = Image.new("RGBA", src.size, (0, 0, 0, 0))
    mirrored.alpha_composite(ImageOps.mirror(src.crop((0, 0, 2 * CX, h))), (0, 0))
    patch = _mask(src.size, lambda d, s: d.rounded_rectangle((575 * s, 560 * s, 1000 * s, 760 * s), radius=40 * s, fill=255))
    fixed = Image.composite(mirrored, src, patch)

    def circle(d, s, r=R):
        d.ellipse(((CX - r) * s, (CY - r) * s, (CX + r) * s, (CY + r) * s), fill=255)

    def crown(d, s):
        d.polygon([(140 * s, 0), (760 * s, 0), (760 * s, 290 * s), (660 * s, 330 * s), (200 * s, 330 * s), (140 * s, 280 * s)], fill=255)

    emblem = fixed.copy()
    emblem.putalpha(ImageChops.multiply(fixed.getchannel("A"), _mask(src.size, lambda d, s: (circle(d, s), crown(d, s)))))

    # Nur Kreis: Krone wegschneiden (Kreis ist oben von der Krone verdeckt -> unteren Teil spiegeln wäre falsch,
    # daher den Kreis aus dem Emblem nehmen und oben mit der vollen Kreisform abschließen)
    disc = fixed.copy()
    disc.putalpha(ImageChops.multiply(fixed.getchannel("A"), _mask(src.size, lambda d, s: circle(d, s, R - 9))))
    disc = _flip_fill_top(disc)

    return _square(emblem, 24), _square(disc, 8)


def _flip_fill_top(disc: Image.Image) -> Image.Image:
    """Oberen, von der Krone verdeckten Kreisteil durch den vertikal gespiegelten unteren Teil ersetzen."""
    w, h = disc.size
    flipped = Image.new("RGBA", disc.size, (0, 0, 0, 0))
    lower = disc.crop((0, CY, w, min(h, 2 * CY)))
    flipped.alpha_composite(ImageOps.flip(lower), (0, CY - lower.height))
    # weicher Übergang über 120 px, damit keine Kante sichtbar ist
    top = Image.new("L", disc.size, 0)
    for y in range(h):
        v = 255 if y < CY - 120 else (0 if y >= CY else int(255 * (CY - y) / 120))
        top.paste(v, (0, y, w, y + 1))
    out = Image.composite(flipped, disc, top)
    # Mitte (Play-Symbol) stammt aus dem Original, weicher Übergang durch Kreismaske
    inner = _mask(disc.size, lambda d, s: d.ellipse(((CX - 175) * s, (CY - 175) * s, (CX + 175) * s, (CY + 175) * s), fill=255))
    return Image.composite(disc, out, inner)


def scaled(img: Image.Image, n: int) -> Image.Image:
    return img.resize((n, n), Image.LANCZOS)


def paused(img: Image.Image) -> Image.Image:
    """Aufnahme pausiert: Symbol entsättigt (wie bisher bei OBS)."""
    alpha = img.getchannel("A")
    grey = ImageEnhance.Color(img).enhance(0.15)
    grey = ImageEnhance.Brightness(grey).enhance(0.85)
    grey.putalpha(alpha)
    return grey


def svg_wrap(png_path: str, size: int = 256) -> str:
    """Qt lädt die *_macos.svg nur unter macOS; als Platzhalter ein SVG, das das PNG einbettet."""
    import base64

    data = base64.b64encode((ROOT / png_path).read_bytes()).decode()
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
        f'viewBox="0 0 {size} {size}"><image width="{size}" height="{size}" '
        f'xlink:href="data:image/png;base64,{data}"/></svg>\n'
    )


def main() -> None:
    emblem, disc = load_parts()

    # Windows-Programmsymbol: klein = Kreis, groß = Emblem mit Krone
    frames = {n: scaled(disc if n <= 32 else emblem, n) for n in (16, 20, 24, 32, 40, 48, 64, 256)}
    for target in ("frontend/cmake/windows/obs-studio.ico", "cmake/bundle/windows/obs-studio.ico"):
        big = frames[256]
        big.save(ROOT / target, sizes=[(n, n) for n in frames], append_images=[frames[n] for n in frames if n != 256], bitmap_format="png")

    images = ROOT / "frontend/forms/images"
    scaled(emblem, 256).save(images / "obs.png")  # Fenster-Symbol, Über-Dialog
    scaled(disc, 256).save(images / "obs_macos.png")
    scaled(paused(disc), 256).save(images / "obs_paused.png")
    scaled(paused(disc), 256).save(images / "obs_paused_macos.png")
    (images / "obs_macos.svg").write_text(svg_wrap("frontend/forms/images/obs_macos.png"), encoding="utf-8")
    (images / "obs_paused_macos.svg").write_text(svg_wrap("frontend/forms/images/obs_paused_macos.png"), encoding="utf-8")

    linux = ROOT / "frontend/cmake/linux/icons"
    for n in (128, 256, 512):
        scaled(emblem, n).save(linux / f"obs-logo-{n}.png")
    (linux / "obs-logo-scalable.svg").write_text(svg_wrap("frontend/cmake/linux/icons/obs-logo-512.png", 512), encoding="utf-8")

    # Für Installer, Webseite und README
    scaled(emblem, 1024).save(HERE / "meinlive-studio-1024.png")
    scaled(emblem, 256).save(HERE / "meinlive-studio-256.png")
    scaled(disc, 256).save(HERE / "meinlive-studio-disc-256.png")
    wordmark = Image.open(SOURCE).convert("RGBA")
    wordmark = wordmark.crop(wordmark.getbbox())
    wordmark.thumbnail((900, 300), Image.LANCZOS)
    wordmark.save(HERE / "meinlive-wordmark.png")
    print("Symbole erzeugt.")


if __name__ == "__main__":
    main()


def installer_images() -> None:
    """Bilder für den Inno-Setup-Installer (je 100 % und 250 % Skalierung).
    Platzhalter, bis eigene Grafiken vorliegen: meinlive/installer/wizard-large-*.png ersetzen."""
    from PIL import ImageDraw as _Draw

    emblem, _ = load_parts()
    out = ROOT / "meinlive/installer"
    custom = out / "wizard-large-custom.png"
    for w, h in ((164, 314), (410, 785)):
        if custom.exists():
            img = Image.open(custom).convert("RGB")
            ratio = max(w / img.width, h / img.height)
            img = img.resize((round(img.width * ratio), round(img.height * ratio)), Image.LANCZOS)
            left, top = (img.width - w) // 2, (img.height - h) // 2
            img = img.crop((left, top, left + w, top + h))
        else:
            img = Image.new("RGB", (w, h), (12, 12, 14))
            d = _Draw.Draw(img)
            # Verlauf unten wie --gradient-brand (#ff2d55 -> #ff6a3d)
            for y in range(h):
                t = y / h
                glow = max(0.0, (t - 0.55) / 0.45)
                r = int(12 + (255 - 12) * glow * 0.55)
                g = int(12 + (60 - 12) * glow * 0.55)
                b = int(14 + (70 - 14) * glow * 0.55)
                d.line((0, y, w, y), fill=(r, g, b))
            size = int(w * 0.8)
            mark = scaled(emblem, size)
            img.paste(mark, ((w - size) // 2, int(h * 0.18)), mark)
        img.save(out / f"wizard-large-{w}.bmp")
    for n in (55, 138):
        img = Image.new("RGB", (n, int(n * 58 / 55)), (255, 255, 255))
        mark = scaled(emblem, n)
        img.paste(mark, (0, (img.height - n) // 2), mark)
        img.save(out / f"wizard-small-{n}.bmp")
    print("Installer-Bilder erzeugt.")


if __name__ == "__main__":
    installer_images()
