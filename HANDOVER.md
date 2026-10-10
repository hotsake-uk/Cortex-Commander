# Handover

How this fork is worked on day to day, for anyone (person or coding agent) joining. Building is in the [README](README.md#building-and-running).

## Branches

- **`maindev`** is the main branch. Every change goes on its own feature branch off `maindev` (one per request) and back in by pull request.
- `dev-8.2`, `dev-8.3`, `dev-8.4` and `modernisation` are history. Nothing new targets them.

## Workflow

- One feature branch and one PR per feature, into `maindev`. PR titles say what a player sees ("Trees: ...", "Sandbox: ...").
- The PR is merged by whoever opened it as soon as it builds (Linux compile check in the README) and merge conflicts are resolved. CI is not waited on.
- Resolve conflicts by merging `maindev` into the feature branch. No force-pushes to shared branches.
- The maintainer tests in game on Windows and reports back. Headless or in-game test runs only when the maintainer asks for them.
- Every new gameplay or visual feature gets a setting to turn it off (in the F6 panel, in its category) and must work on existing content and mods.

## Testing (when asked)

- Start from a local copy of the maintainer's settings preset, with explicit overrides for what the test needs (the feature's debug view, toggles, resolution). Never edit the shared preset.
- Small "gym" mods (an `.rte` with a Lua script that sets up a scene) are the usual way to test a feature headlessly.
- Drawing-heavy features can't be judged in software GL; ask for an in-game check.

## Major systems and where they live

| System | Code |
|---|---|
| Sandbox mode, Battle Director, battle modes, paint tools, ropes | `Source/Managers/Sandbox*.cpp` |
| Liquids, fire, collapse, smoke, burning and swimming units | `Source/Managers/FluidSim`, `TerrainFire`, `TerrainCollapse`, `SmokeGrid`, `ActorFire`, `ActorWater` |
| Lighting and post-processing | `Source/Renderer/` (`LightingSettings.h` holds defaults) |
| Settings panel (F6) and presets | `Source/Managers/SettingsPanel.cpp`, `SettingsMan` |
| Units, movement, AI, pathfinding | `Source/Entities/` (AHuman, Actor, PathFinder) |
| Medieval faction and buildings | `Data/Medieval.rte` |
| Sprite and texture generators (trees, metals, candles, vehicles) | `Tools/Make*.py` |
| Crash stacks from an AbortLog | `Tools/CrashStack.py` |

Reference docs: `Documentation/LightingAndEffects.md` (settings, INI properties, Lua API), `MODERNISATION_PLAN.md`, `FEATURE_PROPOSAL.md`, `AI_PLAN.md`, `PHYSICS_PLAN.md`, `CHANGELOG.md`.

## State at 2026-10-10

- **In `maindev`:** everything up to PR #163: effect layers, split Sparks/Dust/Debris, unit speech tones, the Wooden Cart, the medieval faction and buildings, paintable metals and materials, trees (and their damage and falling), candles, bleed chance, spawn-panel stats, instant settings saving, 8x zoom, ropes, non-combatants phase 1 (animals and civilians), and the Pause/Break hotkey.
- **Planned:** magic system (design waiting on the maintainer), falling-terrain breakage, later non-combatant phases.
- **Open:** a crash report from a recent build that hasn't been reproduced yet.
