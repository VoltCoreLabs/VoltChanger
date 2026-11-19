# Data fixtures

These files are test inputs; they are not shipped as live recommendations.

- `dotacoach-tidehunter.html`, `dotacoach-abaddon.html`,
  `dotacoach-lina.html`: minimized factual captures from the corresponding
  `https://dotacoach.gg/en/heroes/{hero}/builds` pages, retrieved 2026-11-18.
  Preserve actual item/skill/talent identifiers and the Next Flight envelope;
  website application code and guide prose are excluded.
- `d2pt-sven-pos1.json`: trimmed factual schema sample from the public
  Darktex/d2pt-guides capture. Tests only; Dota2ProTracker is not queried by
  the recommended-builds worker.
