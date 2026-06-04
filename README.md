# Star Fox 64 Randomizer (Recompiled)

A **free & open-source randomizer mod** for
[Star Fox 64: Recompiled](https://github.com/sonicdcer/Starfox64Recomp).

> **Seed-deterministic by design** — the same seed always produces the same
> run, so it's built for **fair seed racing and speedrunning**. 🏁

This is a work in progress. It is a **mod only**: it contains no Nintendo
assets or ROM data — you bring your own legally obtained copy of the game
(US v1.1 / Rev A), which the recompilation loads.

## Features

| Feature | Status | Notes |
|---|---|---|
| 🎵 Random Music | ✅ | Shuffles the 15 main level themes deterministically per seed. Boss/menu music untouched. |
| ⏩ Skip Intros & Briefings (QoL) | ✅ | Level intros and General Pepper briefings are always skippable with **START** / faster with **A** — even on a first playthrough. |
| 🪐 Random Planets | 🚧 planned | The core feature — a seeded, beatable planet sequence. |
| 💥 One-Hit KO, Random Items, … | 🚧 planned | More to come. |

## Configuration

Open the mod's config in the in-game **Mods** menu:

- **Seed** — any text (e.g. `RUN-123`). Same seed = same run. Share it to race.
- **Random Music** — on/off.
- **Skip Intros & Briefings** — on/off.

## Installing (players)

1. Grab the latest `.nrm` from the Releases page.
2. Drag it onto the game window, or use **Install Mods** in the mod menu.
3. Enable **Star Fox 64 Randomizer**, set your seed, play.

## Building (developers)

Requirements:
- **clang + ld.lld**, LLVM **18.1.8** (LLVM 19.x does **not** target MIPS correctly).
- **Python 3** (for `build.py`) **or** GNU `make`.
- **RecompModTool** from the [N64Recomp releases](https://github.com/N64Recomp/N64Recomp/releases).

Clone with submodules:

```sh
git clone --recurse-submodules <repo-url>
cd SF64Randomizer
```

Build (Python helper — no `make` needed):

```sh
# point it at LLVM if clang isn't on PATH:  set LLVM_BIN=...\llvm\bin
python build.py . --tool   # compiles + runs RecompModTool -> build/sf64_randomizer.nrm
```

Or with make + RecompModTool manually:

```sh
make
RecompModTool mod.toml build
```

The resulting `build/sf64_randomizer.nrm` can be dragged into the game.

## License & credits

Code is licensed **MIT** (see [LICENSE](LICENSE)). It contains no game assets.
Built on sonicdcer's recomp/decomp/template and inspired by punk7890's original
ASM randomizer — full attribution in [CREDITS.md](CREDITS.md).
