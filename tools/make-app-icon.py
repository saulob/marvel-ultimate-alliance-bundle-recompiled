#!/usr/bin/env python3
"""Renders packaging/icons/app-icon.svg into packaging/icons/<variant>/app-icon-<size>.png and app-icon.ico.

Developer tool: the generated files are committed, so building the port needs no SVG converter.
Needs Microsoft Edge or Chrome for the rasterization; the rest is the Python standard library.
The source's two fills are recolored per variant; the game variants must match PaletteOf in src/game_theme.h.

  python tools/make-app-icon.py
  python tools/make-app-icon.py --browser /path/to/chrome
"""
import argparse
import base64
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
ICONS = ROOT / "packaging" / "icons"
SOURCE_DARK, SOURCE_LIGHT = "rgb(25,144,167)", "rgb(144,224,239)"
LAUNCHER_PRIMARY = 0xBFC5CC
LIGHT_TINT = 0.55
VARIANTS = {"launcher": None, "mua1": "kMua1", "mua1-gold": "kMua1Gold", "mua2": "kMua2"}
PNG_SIZES = (16, 24, 32, 48, 64, 128, 256, 512)
ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)
BROWSERS = (
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    "microsoft-edge", "google-chrome", "chromium", "chromium-browser",
)


def find_browser(requested):
    for candidate in (requested,) if requested else BROWSERS:
        found = shutil.which(candidate)
        if found:
            return found
    sys.exit("No Edge or Chrome found: pass --browser")


def game_primaries():
    source = (ROOT / "src" / "game_theme.h").read_text(encoding="utf-8")
    primaries = {case.group(1): int(case.group(2), 16)
                 for case in re.finditer(r"case GameId::(\w+):\s*return \{0x([0-9A-Fa-f]{6})", source)}
    fallback = re.search(r"\}\s*return \{0x([0-9A-Fa-f]{6})", source)
    if not fallback:
        sys.exit("PaletteOf not found in src/game_theme.h")
    primaries["kMua1"] = int(fallback.group(1), 16)
    return primaries


def variant_colors():
    primaries = game_primaries()
    return {name: LAUNCHER_PRIMARY if game is None else primaries[game] for name, game in VARIANTS.items()}


def tint(color):
    channels = ((color >> 16) & 255, (color >> 8) & 255, color & 255)
    return tuple(round(c + (255 - c) * LIGHT_TINT) for c in channels)


def recolor(svg, color):
    text = svg.decode("utf-8")
    if SOURCE_DARK not in text or SOURCE_LIGHT not in text:
        sys.exit("app-icon.svg no longer carries the two source fills")
    dark = f"#{color:06X}"
    light = "#{:02X}{:02X}{:02X}".format(*tint(color))
    return text.replace(SOURCE_DARK, dark).replace(SOURCE_LIGHT, light).encode("utf-8")


def render(browser, svg, size, work):
    page = work / f"{size}.html"
    uri = base64.b64encode(svg).decode()
    page.write_text(
        '<!doctype html><body style="margin:0;background:transparent">'
        f'<img src="data:image/svg+xml;base64,{uri}" width="{size}" height="{size}" style="display:block">',
        encoding="utf-8")
    out = work / f"{size}.png"
    subprocess.run(
        [browser, "--headless", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=1",
         "--default-background-color=00000000", f"--user-data-dir={work / 'profile'}",
         f"--window-size={size},{size}", f"--screenshot={out}", page.as_uri()],
        check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120)
    png = out.read_bytes()
    width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", png[16:29])
    if (width, height, depth, color, interlace) != (size, size, 8, 6, 0):
        sys.exit(f"{size}px render is not an 8-bit RGBA {size}x{size} PNG")
    return png


def rgba_pixels(png):
    width, height = struct.unpack(">II", png[16:24])
    data, pos = b"", 8
    while pos < len(png):
        length, kind = struct.unpack(">I4s", png[pos:pos + 8])
        if kind == b"IDAT":
            data += png[pos + 8:pos + 8 + length]
        pos += 12 + length
    raw, stride = zlib.decompress(data), width * 4
    rows, prev = [], bytearray(stride)
    for y in range(height):
        kind, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            left = line[x - 4] if x >= 4 else 0
            up = prev[x]
            corner = prev[x - 4] if x >= 4 else 0
            if kind == 1:
                line[x] = (line[x] + left) & 255
            elif kind == 2:
                line[x] = (line[x] + up) & 255
            elif kind == 3:
                line[x] = (line[x] + (left + up) // 2) & 255
            elif kind == 4:
                p = left + up - corner
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - corner)
                line[x] = (line[x] + (left if pa <= pb and pa <= pc else up if pb <= pc else corner)) & 255
        rows.append(line)
        prev = line
    return rows


def dib_frame(png):
    rows = rgba_pixels(png)
    size = len(rows)
    pixels, mask = b"", b""
    for line in reversed(rows):
        pixels += b"".join(bytes((line[i + 2], line[i + 1], line[i], line[i + 3])) for i in range(0, size * 4, 4))
        bits = "".join("1" if line[i + 3] == 0 else "0" for i in range(0, size * 4, 4)).ljust((size + 31) // 32 * 32, "0")
        mask += int(bits, 2).to_bytes(len(bits) // 8, "big")
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, len(pixels) + len(mask), 0, 0, 0, 0)
    return header + pixels + mask


def build_ico(pngs):
    frames = [(size, pngs[size] if size == 256 else dib_frame(pngs[size])) for size in ICO_SIZES]
    out = struct.pack("<HHH", 0, 1, len(frames))
    offset = 6 + 16 * len(frames)
    for size, data in frames:
        out += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    return out + b"".join(data for _, data in frames)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--browser", help="Edge or Chrome executable")
    browser = find_browser(parser.parse_args().browser)
    source = (ICONS / "app-icon.svg").read_bytes()
    for name, color in variant_colors().items():
        out_dir = ICONS / name
        out_dir.mkdir(exist_ok=True)
        svg = recolor(source, color)
        with tempfile.TemporaryDirectory() as work:
            pngs = {size: render(browser, svg, size, pathlib.Path(work)) for size in PNG_SIZES}
        for size, png in pngs.items():
            (out_dir / f"app-icon-{size}.png").write_bytes(png)
        (out_dir / "app-icon.ico").write_bytes(build_ico(pngs))
        print(f"{name}: #{color:06X}, {len(pngs)} PNGs and app-icon.ico ({', '.join(map(str, ICO_SIZES))}) in {out_dir}")


if __name__ == "__main__":
    main()
