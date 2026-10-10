# Handover

How this fork is worked on day to day, for anyone (person or coding agent) joining. Building is in the [README](README.md#building-and-running).

## Branches

- **`dev-8.4`**: all new work (since the rope feature, 2026-10-10). Branched from `dev-8.3` at `f062493c`.
- **`dev-8.3`**: only work started before ropes. It is merged forward into `dev-8.4`, so fixes there reach both.
- **`dev-8.2`**: frozen. Never merge into it.
- "3.2" in the maintainer's notes means `dev-8.3`.

## Workflow

- One feature branch and one PR per feature, into its target branch. PR titles say what a player sees ("Trees: ...", "Sandbox: ...").
- The PR is merged by whoever opened it once merge conflicts are resolved; CI is not waited on. Compile-check on Linux first (README, *Linux compile check*). The maintainer tests in game on Windows and reports back.
- Resolve conflicts by merging the base into the feature branch. No force-pushes to shared branches.
- Every new gameplay or visual feature gets a setting to turn it off (in the F6 panel, in its category), and must work on existing content and mods.

## Testing

- In-game and headless tests start from a local copy of the maintainer's settings preset, with explicit overrides for what the test needs (turn on the feature's debug view, toggles, resolution). Never edit the shared preset.
- Small "gym" mods (an `.rte` with a Lua script that sets up a scene) are the usual way to test a feature headlessly.
- Drawing-heavy features can't be judged in software GL; ask for an in-game check.

## Major systems and where they live

| System | Code |
|---|---|
| Sandbox mode, Battle Director, battle modes | `Source/Managers/Sandbox*.cpp` |
| Liquids, fire, collapse, smoke, burning and swimming units | `Source/Managers/FluidSim`, `TerrainFire`, `TerrainCollapse`, `SmokeGrid`, `ActorFire`, `ActorWater` |
| Lighting and post-processing | `Source/Renderer/` (`LightingSettings.h` holds defaults) |
| Settings panel (F6) and presets | `Source/Managers/SettingsPanel.cpp`, `SettingsMan` |
| Units, movement, AI, pathfinding | `Source/Entities/` (AHuman, Actor, PathFinder) |
| Medieval faction | `Data/Medieval.rte` |
| Sprite and texture generators (trees, metals, candles, vehicles) | `Tools/Make*.py` |
| Crash stacks from an AbortLog | `Tools/CrashStack.py` |

Reference docs: `Documentation/LightingAndEffects.md` (settings, INI properties, Lua API), `MODERNISATION_PLAN.md`, `FEATURE_PROPOSAL.md`, `AI_PLAN.md`, `PHYSICS_PLAN.md`, `CHANGELOG.md`.

## State at 2026-10-10

- **Recently merged (#121 to #155):** effect layers and split Sparks/Dust/Debris settings, unit speech tones and mod tones, the Wooden Cart, the medieval faction, paintable metals and materials, trees as their own module, candles, bleed chance, spawn-panel stats, settings saved instantly, 8x zoom, and ropes (dev-8.4 only).
- **In progress:** tree damage from units and bullets, and bullet hit chance (dev-8.3); non-combatant units phase 1 and medieval buildings (dev-8.4).
- **Planned:** magic system, falling-terrain breakage, non-combatants later phases.
- **Open:** a crash report from a recent build that hasn't been reproduced yet.
