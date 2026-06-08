# Changelog

All notable changes to this project are documented here.

## [0.6.1] - 2026-06-08

### Fixed
- Ship Skin: cleaner coexistence with other mods. The ship draw no longer forces
  RT64's extended-RDRAM mode off afterwards (it is global state another mod may
  rely on). Level rendering and the player skin are unaffected.

## [0.6.0] - 2026-06-06

### Added
- **Ship Skin** — fly a different ship instead of the Arwing: `Wolfen` /
  `Wolfen (Venom)` / `Katt` / `Bill`. Purely cosmetic (hitbox/controls
  unchanged). Works in **every** level: the ship model is DMA'd from the ROM
  into a mod buffer once per level entry and bound to its native RSP segment
  only for the player-ship draw, then restored (so the level's own ships render
  normally).

### Fixed
- Ship Skin crash in levels that don't natively load the chosen ship: mod
  memory (`recomp_alloc`) lives in RT64's extended RDRAM, so the usual
  `K0_TO_PHYS` segment base pointed out of bounds. The ship draw is now bracketed
  with `gEXSetRDRAMExtended` and bound to the extended base, which RT64 resolves
  correctly for the model and all its vertices/textures.

### Known issues
- Cosmetic only: in-level cutscenes draw all ships through the same routine, so
  teammates also show the chosen skin there; in gameplay they stay Arwings.

## [0.5.0] - 2026-06-05

### Added
- **Marathon — full run + Venom finale.** Play all 12 normal planets
  back-to-back without the map, then a chosen gateway into the Venom endgame
  (→ Venom → Andross → ending). Two config dropdowns:
  - **Marathon:** `Disabled` / `Bolse` / `Area 6` / `Expert + Bolse` /
    `Expert + Area 6` (Bolse = longer via Venom 1 + 2; Area 6 = shorter/harder;
    Expert variants force expert difficulty).
  - **Marathon: Level order:** `Shuffled (Seed)` or `Fixed`.
- The gateway (Bolse/Area 6) is the final marathon level; its completion
  self-chains into Venom via the engine's own `GSTATE_PLAY` transitions, which
  the marathon hook leaves untouched (it only rewrites the post-level
  `GSTATE_MAP` transition).

### Verified
- Bolse gateway with Fixed level order: full end-to-end run confirmed.

### To verify
- Shuffled (Seed) order (determinism + variability); Area 6 gateway; Expert
  variants; the ending score screen (`gMissionNumber` stays 0 in marathon).

## [0.4.0] - 2026-06-05

### Added
- **Marathon mode (early WIP)** — continuous level-to-level transitions without
  the map in between. Phase 1: a 4-level test chain.

### Fixed
- Hard crash on the seamless level→level transition: skipping `Play_Setup`
  left a stale on-rails streaming cursor (`gSavedObjectLoadIndex`), making the
  next level's `Player_Setup` out-of-bounds index the object table. Now routed
  through the engine's own `GSTATE_PLAY` branch (like the Venom transitions),
  with the matching audio/object teardown before `Memory_FreeAll`.
- Lost audio after a transition: `Audio_FadeOutAll` mutes every sequence player
  and the skipped map normally restores them — volumes are now restored on the
  first track of the next level.

## [0.3.0] - 2026-06-05

### Added
- **One-Hit KO (Hardmode)** — Fox dies on any hit that gets through; an active
  shield item still absorbs its one hit.
- **Expert Mode** — runs the whole run in expert difficulty (double damage, more
  enemies, expert ending), locked at run start like the seed; no in-game unlock
  needed.

### Verified
- Random Planets determinism: same seed → same planet order; different seed →
  different order (the 0.2.0 to-verify item).

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
