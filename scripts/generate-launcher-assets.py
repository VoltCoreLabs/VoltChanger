"""Render the launcher's original SVG artwork; requires resvg-py and Pillow."""
from pathlib import Path
import resvg_py
from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "resources/launcher"
for name in ("hero", "background"):
    data = resvg_py.svg_to_bytes(svg_path=str(ASSETS / f"{name}.svg"))
    (ASSETS / f"{name}.png").write_bytes(data)

source = Image.open(ASSETS / "volt-icon-source.png").convert("RGBA")
icon = ImageOps.pad(source, (256, 256), method=Image.Resampling.LANCZOS, color=(0, 0, 0, 0))
icon.save(ASSETS / "volt-icon.png")
icon.save(ASSETS / "VoltChanger.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
