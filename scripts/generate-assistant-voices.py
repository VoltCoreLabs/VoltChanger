"""Prepare local preview metadata from the installed game's chat wheel definitions."""
from pathlib import Path
import json
import re

root = Path(__file__).resolve().parent.parent
source = root / 'build/plus-resources/chat/scripts/chat_wheel_heroes.txt'
tokens = [m.group() for m in re.finditer(r'//[^\n]*|"(?:\\.|[^"\\])*"|[{}]', source.read_text(encoding='utf-8-sig')) if not m.group().startswith('//')]
index = 0
def read_object(end=False):
    global index
    result = {}
    while index < len(tokens):
        key = tokens[index]; index += 1
        if key == '}':
            if not end: raise ValueError('Unexpected object terminator')
            return result
        key = key[1:-1]
        value = tokens[index]; index += 1
        result[key] = read_object(True) if value == '{' else value[1:-1]
    if end: raise ValueError('Unterminated object')
    return result
data = read_object()['chat_wheel']['hero_messages']
output = {}
for hero, messages in data.items():
    values = []
    for key, item in messages.items():
        if not isinstance(item, dict) or not item.get('sound'): continue
        values.append({'key': key, 'label': item.get('label', ''), 'sound': item['sound'], 'persona': int(item.get('persona', '0')), 'tier': item.get('unlock_hero_badge_tier', '')})
    output[hero] = values
destination = root / 'resources/assistant'
destination.mkdir(parents=True, exist_ok=True)
(destination / 'voice_lines.json').write_text(json.dumps(output, ensure_ascii=False, separators=(',', ':')), encoding='utf-8')
print('Local voice preview metadata:', len(output), 'heroes,', sum(map(len, output.values())), 'lines')
