"""Render the launcher's original SVG artwork; requires resvg-py and Pillow."""
from pathlib import Path
from io import BytesIO
import resvg_py
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "resources/launcher"
for name in ("volt-icon", "hero", "background"):
    data = resvg_py.svg_to_bytes(svg_path=str(ASSETS / f"{name}.svg"))
    (ASSETS / f"{name}.png").write_bytes(data)

icon = Image.open(BytesIO(resvg_py.svg_to_bytes(svg_path=str(ASSETS / "volt-icon.svg"), width=256, height=256)))
icon.save(ASSETS / "VoltChanger.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
