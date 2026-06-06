# Credits & Attribution

This randomizer stands on the shoulders of several projects and people.
Huge thanks to everyone below.

## Core foundations
- **[sonicdcer](https://github.com/sonicdcer)** — creator of **Star Fox 64: Recompiled**, and the foundation this entire mod is built on:
  - [Star Fox 64: Recompiled](https://github.com/sonicdcer/Starfox64Recomp) — the native PC port (N64: Recompiled based) that this mod runs on, created by sonicdcer.
  - [sf64](https://github.com/sonicdcer/sf64) — the Star Fox 64 decompilation (led by sonicdcer, with petrie911, KiritoDv, inspectredc, Ryan-Myers & other contributors), whose named functions/types make modding possible (included as a submodule).
  - [SF64RecompModTemplate](https://github.com/sonicdcer/SF64RecompModTemplate) — the mod template this project was bootstrapped from.
  - [Starfox64RecompSyms](https://github.com/sonicdcer/Starfox64RecompSyms) — reference symbol files (included as a submodule).

## Randomizer concept & feature blueprint
- **punk7890** — for the original [Star Fox 64 Randomizer](https://github.com/punk7890/Star-Fox-64-Randomizer)
  (ASM ROM hack). We did not port its code, but its feature set served as the
  design blueprint (random planets, items, music, special modes, etc.).

## Tooling
- **N64Recomp / Mr-Wiseguy & contributors** — [N64: Recompiled](https://github.com/N64Recomp/N64Recomp)
  and the `RecompModTool` used to build mods, plus the modding framework.
- **RT64** — the rendering backend used by the port.

## Differences from the original ASM randomizer
- **Seed-deterministic by design.** The original derived randomness from frame
  timers, so runs were not reproducible. This mod uses a real seeded PRNG
  (xorshift32 from an FNV-1a–hashed seed string), so the same seed always
  produces the same run — built for fair seed racing / speedrunning.

If you feel you should be credited here and aren't, please open an issue. 🦊
