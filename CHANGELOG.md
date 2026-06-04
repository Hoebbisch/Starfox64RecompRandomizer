# Changelog

All notable changes to this project are documented here.

## [0.2.0] - 2026-06-04 (WIP)

### Added
- **Random Planets (WIP / in testing)** — seed-deterministic route.
  - Route is forced at the single level-load point (`Map_PlayLevel`), so map
    navigation no longer overrides the randomized planet.
  - Fixed route structure (7 stops): start + 4 random planets → Area 6 / Bolse
    (Venom gateway, seed-chosen) → Venom. Area 6 & Bolse are excluded from the
    middle pool because they route directly to Venom.
  - OOB safety: patched `Map_GetPathId` (returned 24 → `sPaths[24]` overflow).
- **Random Items** — seeded type→type swap at item spawn (`Item_Load`). Path
  markers and special objects are left untouched.
- **Seed lock** — the seed is frozen at run start (new game); mid-run changes
  are ignored, so a run stays reproducible (speedrun-friendly).
- **Config: Start Planet** — `Random` (like the original) or `Corneria`. Does
  not shift the seeded route of the middle slots.

### Known issues / to verify
- Random Planets sequence determinism needs verification (same seed twice,
  different seeds). One run showed a possibly-orderly sequence — to confirm.
- Map zoom animation may visually fly to a different planet than the one played
  (cosmetic; the played level and score screen are correct).
- Full playthrough to Venom/Andross not yet verified end-to-end.

## [0.1.0] - 2026-06-04

### Added
- Initial release: build pipeline (clang/MIPS + RecompModTool), repo, MIT
  license, credits.
- **Random Music** — seeded Fisher-Yates shuffle of the 15 main level themes.
- **QoL: Skip Intros & Briefings** — level intros and General Pepper briefings
  are always skippable (START) / faster (A), even on a first playthrough.
