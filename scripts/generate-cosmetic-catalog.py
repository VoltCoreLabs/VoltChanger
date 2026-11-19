"""Build an equipable-cosmetics allowlist from the installed items_game schema.

The runtime validates every listed ID against its actual loaded schema as well.
No tools, consumables, crates or bundle containers are included.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
SOURCE = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/items_game.txt"
OUTPUT = ROOT / "resources/assistant/cosmetic_catalog.txt"


def tokens(text):
    for match in re.finditer(r'"((?:\\.|[^"\\])*)"|([{}])|//[^\n]*|([^\s{}"]+)', text):
        quoted, brace, plain = match.groups()
        if quoted is not None:
            yield quoted.replace(r'\"', '"').replace(r'\\', '\\')
        elif brace:
            yield brace
        elif plain:
            yield plain


def block(stream):
    out = {}
    for key in stream:
        if key == "}":
            return out
        value = next(stream)
        out[key] = block(stream) if value == "{" else value
    raise ValueError("Unclosed KeyValues block")


stream = iter(tokens(SOURCE.read_text(encoding="utf-8-sig")))
assert next(stream) == "items_game" and next(stream) == "{"
schema = block(stream)
prefabs = schema["prefabs"]
cache = {}


def resolve(name, seen=frozenset()):
    if name in cache:
        return cache[name]
    if name in seen or name not in prefabs:
        return {}
    entry = prefabs[name]
    out = {}
    for parent in entry.get("prefab", "").split():
        out.update(resolve(parent, seen | {name}))
    out.update(entry)
    cache[name] = out
    return out


allowed = []
for key, item in schema["items"].items():
    if not key.isdigit() or not 0 < int(key) <= 65535 or not isinstance(item, dict):
        continue
    merged = {}
    for parent in item.get("prefab", "").split():
        merged.update(resolve(parent))
    merged.update(item)
    if merged.get("item_class") != "dota_item_wearable" or merged.get("baseitem") == "1":
        continue
    global_item = merged.get("player_loadout") == "1"
    heroes = merged.get("used_by_heroes")
    if not global_item and (not heroes or merged.get("item_slot", "none") == "none"):
        continue
    if merged.get("tool") or merged.get("bundle"):
        continue
    allowed.append((int(key), "G" if global_item else "H"))

OUTPUT.parent.mkdir(parents=True, exist_ok=True)
OUTPUT.write_text("# OVR equipable cosmetics v1\n" + "".join(f"{key} {kind}\n" for key, kind in sorted(allowed)), encoding="utf-8")
print(f"Equipable cosmetics: {len(allowed)}; global: {sum(kind == 'G' for _, kind in allowed)}")
