# Launcher header art

`header-art.svg` is the source of truth: one composition, drawn in the Marvel Ultimate Alliance red palette.

`header-art-mua1.png`, `header-art-mua1-gold.png` and `header-art-mua2.png` are the runtime assets, 3840x432 with a
transparent background. Do not edit them by hand. `tools/make-launcher-header-art.py` renders each one from the SVG,
recoloured from `PaletteOf` in `src/game_theme.h`, and records that palette in the PNG; regenerate after every SVG or
palette change (needs Edge or Chrome):

```powershell
python tools/make-launcher-header-art.py
```

The launcher draws the PNG of the selected game over its header gradient, anchored to the header's top-right corner at
the full header height. The SVG's empty left side keeps the title readable.
