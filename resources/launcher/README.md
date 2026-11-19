# VoltChanger launcher artwork

The glossy blue ribbon app icon was supplied by the project owner and is kept
unchanged in `volt-icon-source.png`. The PNG and ICO build variants preserve its
proportions and transparency. Hero/background SVGs are the editable original artwork.
PNG variants are embedded by the Rust asset source.
`VoltChanger.ico` includes 16, 24, 32, 48, 64, 128 and 256 px variants and
is embedded in the EXE by `build.rs` as resource 1, used by GPUI on Windows.

Normal builds use the committed assets and need no Python tooling.
To regenerate after editing the SVGs, install `resvg-py` and `Pillow` in your
development environment and run `python scripts/generate-launcher-assets.py`.

The artwork is distributed under the project's GPL-3.0-only license.
