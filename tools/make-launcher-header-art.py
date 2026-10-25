#!/usr/bin/env python3
"""Renders assets/launcher/header-art.svg into assets/launcher/header-art-<game>.png, one per game.

Developer tool: the generated files are committed, so building the port needs no SVG converter.
Needs Microsoft Edge or Chrome for the rasterization; the rest is the Python standard library.
The source carries the Marvel Ultimate Alliance palette; each game's colors derive from PaletteOf in src/game_theme.h.

  python tools/make-launcher-header-art.py
  python tools/make-launcher-header-art.py --browser /path/to/chrome
"""
import argparse
import importlib.util
import pathlib
import re
import struct
import subprocess
import sys
import tempfile
import time
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
ART = ROOT / "assets" / "launcher"
WIDTH, HEIGHT = 3840, 432
VARIANTS = {"mua1": "kMua1", "mua1-gold": "kMua1Gold", "mua2": "kMua2"}
SOURCE_PRIMARY = 0xD73A3A
DEEP_SHADE, LIGHT_TINT, HOT_TINT = 0.28, 0.55, 0.85
PALETTE_KEYWORD = b"mua-palette"


def load_icon_tool():
    spec = importlib.util.spec_from_file_location("make_app_icon", ROOT / "tools" / "make-app-icon.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def channels(color):
    return (color >> 16) & 255, (color >> 8) & 255, color & 255


def pack(r, g, b):
    return r << 16 | g << 8 | b


def palette(primary):
    deep = pack(*(round(c * DEEP_SHADE) for c in channels(primary)))
    light = pack(*(round(c + (255 - c) * LIGHT_TINT) for c in channels(primary)))
    hot = pack(*(round(c + (255 - c) * HOT_TINT) for c in channels(primary)))
    return deep, primary, light, hot


def recolor(svg, primary):
    text = svg.decode("utf-8")
    source, target = palette(SOURCE_PRIMARY), palette(primary)
    for old in source:
        if text.count(f"#{old:06X}") == 0:
            sys.exit(f"header-art.svg no longer carries source color #{old:06X}")
    return re.sub("|".join(f"#{c:06X}" for c in source),
                  lambda match: f"#{target[source.index(int(match.group()[1:], 16))]:06X}", text).encode("utf-8")


def render(icons, browser, svg, work):
    page = work / "header.html"
    uri = icons.base64.b64encode(svg).decode()
    page.write_text(
        '<!doctype html><body style="margin:0;background:transparent;overflow:hidden">'
        f'<img src="data:image/svg+xml;base64,{uri}" width="{WIDTH}" height="{HEIGHT}" style="display:block">',
        encoding="utf-8")
    out = work / "header.png"
    subprocess.run(
        [browser, "--headless", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=1",
         "--default-background-color=00000000", f"--user-data-dir={work / 'profile'}",
         f"--window-size={WIDTH},{HEIGHT}", f"--screenshot={out}", page.as_uri()],
        check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=180)
    deadline = time.monotonic() + 60
    while not icons.finished_png(out):
        if time.monotonic() > deadline:
            sys.exit("header render wrote no PNG")
        time.sleep(0.1)
    png = out.read_bytes()
    width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", png[16:29])
    if (width, height, depth, color, interlace) != (WIDTH, HEIGHT, 8, 6, 0):
        sys.exit(f"header render is {width}x{height}, not an 8-bit RGBA {WIDTH}x{HEIGHT} PNG")
    return png


def check_transparent_left(icons, png):
    rows = icons.rgba_pixels(png)
    for line in rows:
        for x in range(0, WIDTH // 4, 4):
            if line[x * 4 + 3] != 0:
                sys.exit("header art is not transparent across its left quarter")
    if not any(line[i + 3] for line in rows for i in range(0, WIDTH * 4, 4)):
        sys.exit("header art rendered fully transparent")


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def with_palette(png, primary):
    text = chunk(b"tEXt", PALETTE_KEYWORD + b"\0" + f"{primary:06X}".encode())
    return png[:33] + text + png[33:]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--browser", help="Edge or Chrome executable")
    icons = load_icon_tool()
    browser = icons.find_browser(parser.parse_args().browser)
    primaries = icons.game_primaries()
    source = (ART / "header-art.svg").read_bytes()
    for name, game in VARIANTS.items():
        primary = primaries[game]
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as work:
            png = render(icons, browser, recolor(source, primary), pathlib.Path(work))
        check_transparent_left(icons, png)
        out = ART / f"header-art-{name}.png"
        out.write_bytes(with_palette(png, primary))
        print(f"{name}: #{primary:06X}, {WIDTH}x{HEIGHT} in {out}")


if __name__ == "__main__":
    main()
