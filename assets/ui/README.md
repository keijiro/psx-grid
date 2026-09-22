# UI asset sources

`masks.py` is the editable source of the PSX Grid Bitmap face and icon masks.
`scripts/generate-assets.py` packs it into a 256 x 64, 4-bit indexed atlas and
emits glyph advances with the pixel data. CMake regenerates its private header
when either source changes. The build requires Python 3's standard library.

The bitmap face was drawn for this prototype, guided by Jura's monoline forms.
It is named **PSX Grid Bitmap**, not Jura, and distributed under the accompanying
SIL Open Font License (`OFL.txt`). Jura copyright: 2019 The Jura Project Authors
(https://github.com/ossobuffo/jura). Bitmap modifications: 2026 Keijiro Takahashi.
`Jura-Regular.ttf` is the unmodified comparison candidate copied from
`Assets/UI/Fonts/` of the local Jacquard checkout at commit `5f02d3d`.
The shipping atlas uses the bitmap face, not rasterized Jura.

Icons are integer-grid redrawings of Jacquard's `Assets/Jacquard/UI/TileIcons.cs`;
the palette and shells follow `Style.cs` and `TileElement.cs` at the same commit.
`JACQUARD-LICENSE.txt` retains Jacquard's MIT attribution. The reference image
`docs/captures/jacquard-tiles.png` is copied from its `Docs/Figures/14-tiles.png`;
it is an existing illustration, not a newly captured running application.

For the optional visual study, install Pillow in a virtual environment and run
`scripts/appearance-study.py` with that environment's Python. It saves native
320 x 240 comparisons in `docs/captures` and nearest-neighbor enlargements in
`build`. Pillow is not needed for game builds or host tests.
