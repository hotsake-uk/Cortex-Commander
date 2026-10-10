# Render Test

These tools are for checking rendering changes by eye, against fixed scenarios, and against another build such as a `development` checkout.

## Setup

0. Windows blocks PowerShell scripts by default. In the PowerShell window you'll use, run `Set-ExecutionPolicy -Scope Process Bypass` first; it lasts for that window only.
1. Build the game with `Build.ps1` (Debug Release by default).
2. Run `Setup.ps1`. It installs `RenderTest.rte` into `Mods\` and writes scenario settings files to `Userdata\RenderTest\`. Each scenario is the `releasezone2` preset (`Data\Presets\releasezone2.ini`, which the built-in defaults match) with the keys it depends on changed, so the game starts straight into a known scene. `-Base Userdata\Settings.ini` starts from your own settings instead. `Gym.ps1` takes the same `-Base`.

A scenario or gym sets every key it relies on, debug views included (the wind scenarios turn on the weather overlay with `WorldSimOverlay = 5`, say), because the base only sets the look. Later keys in a settings file win over earlier ones, so on Linux a test's settings can be made with `cat Data/Presets/releasezone2.ini overrides.ini > test.ini`, or with `Tools/RenderTest/MakeTestSettings.sh`.

## Capturing

```powershell
.\Capture.ps1 -Scenario BunkerNight -Burst 6 -BurstIntervalMs 250
.\ContactSheet.ps1 -Name BunkerNight -Count 6 -Cols 3
.\Capture.ps1 -Scenario Caves -CameraPOI 2
```

- **Close any debug-build game you have open first.** If `Cortex Command.debug.release.exe` is already running from this folder, `Capture.ps1` photographs that one instead of starting the scenario, and closes it afterwards.
- `Capture.ps1` launches the game with the scenario's settings, using the `CCCP_SETTINGSPATH` environment variable. It waits for loading to finish, captures the window to `Output\`, and closes the game. If an assert or error dialog appears, it captures that instead.
- Glows and explosions are brief, so use `-Burst` to catch them.
- `-CameraPOI 1..6` pins the camera tour to one spot, so shots from different builds can be compared.
- To compare against another checkout, pass `-Repo` pointing at its game folder. That folder needs the mod and scenario files too, so run `Setup.ps1 -Repo <path>` there.
- Then compose the results with `BeforeAfter.ps1 -Before a.png -After b.png -Out compare.png` or `Gallery.ps1`.

## Golden image regression check

```powershell
.\Golden.ps1            # compare against Golden\*.png; exit code 1 on any failure
.\Golden.ps1 -Update    # accept the current look as the new baselines (after an intended visual change)
.\Golden.ps1 -Only Noon,Caves
```

- **Scenes:** seven calm, fixed scenes (`GoldenNoon`, `GoldenNight`, `GoldenCaves`, `GoldenClassic`, `GoldenLightingOnly`, `GoldenInterior`, `GoldenShadows`). They have no explosions and use the built-in lighting defaults, so your own settings can't change them.
- **Mouse:** the first six start in the build phase, where the view follows the mouse. If the pointer is over the game window as it opens, the view can land far enough off to fail a scene; run that scene again (`-Only`) before believing it.
- **How it compares:** captures are cropped to the 960x540 game view and compared against the baselines. Small camera offsets are allowed by aligning first, within ±32 px.
- **Tolerances:** a scene fails when the mean channel difference goes over 5, or more than 4% of pixels differ clearly, comparing the game area left of the editor panel (x < 600). Normal run-to-run noise is 0–3. A 20% exposure change scores about 12.
- **Diffs:** each scene writes a diff image to `Output\golden_<scene>_diff.png`. Differences show in red.
- **Not in CI:** hosted runners have no GPU, so this runs locally. Run it before committing renderer changes. It takes about 3 minutes.

## Scenarios

| Scenario | What it shows |
|---|---|
| `Bunker` | Zekarra Mining Outpost by day, with frag and napalm detonations next to the bunker. |
| `BunkerNight`, `BunkerDusk` | The same scene at night or dusk: fire lighting, edge lighting, atmosphere. |
| `BunkerRain` | Night rain glinting in firelight. |
| `BunkerFog` | Fog of war with soft edges, revealed by see-rays. |
| `Caves` | A camera tour of Dvorak Caves with detonations: sky light falloff, scorch marks, god rays. |
| `CavesNightLights` | Actor lamps and a pulsing Lua light (`PostProcessMan:AddLight`). |
| `Primitives` | Every Lua primitive type, drawn near the top left. |
| `Classic` | Lighting, bloom and distortion turned off. |
| `SplitScreen` | Two players, each screen lit independently. |
| `Menu` | The main menu. |

## The mod's global scripts

These are enabled per scenario in the scenario files:

- **Render Test FX:** detonates frag grenades and napalm near player 1's camera.
- **Render Test Camera Tour:** cycles through points of interest. The `CCCP_CAMERA_POI` environment variable pins it to one.
- **Render Test Lights:** gives every actor a lamp, and adds a Lua light.
- **Render Test Fog:** fog of war for team 1, revealed around the screen centre.
- **Render Test Primitives:** a gallery of every primitive type.
- **Render Test Atmosphere:** sets a snowy dawn from Lua.
- **Render Test Perf:** shows the performance overlay.

## Soak tests

- `SoakAuto.ps1` launches the `SoakAuto` scenario, which plays itself (an auto battle with dropships, a flood, napalm and fuel barrels, camera zoom, dusk rain), and samples memory and responsiveness. It sends no input, so it is safe to run while the machine is in use.
- `Soak.ps1` drives the game with real key presses and mouse clicks. Only run it when nobody is using the machine: its input goes to whichever window has focus.
- `LightsBroken` destroys lamps three ways (digging out the ceiling, a shot, a grenade) and leaves one whole: only that one should shine.
- `LightsFixtures`, `LightsFixturesOnly` and `LightsDarkInterior` show the tutorial bunker by night with its own lamps and one of each placeable fixture.
- Test runs set `CCCP_NO_GAMEPAD`, so a controller in use for something else cannot steer them, and `CCCP_HIDE_PANELS`, so the sandbox window (which opens wherever the player last left it) stays out of the shots. Scenarios always run in a 960x540 window.

## Performance log

```powershell
$env:CCCP_PERF_LOG = "perf.log"   # written in the game folder
.\Capture.ps1 -Scenario Blast -ExtraWait 50
```

- With `CCCP_PERF_LOG` set, the game writes where its time went every five seconds. For each part of the sim update and of drawing it gives the share of the time, the average, and the worst single call. Each block also gives the worst frame and how many frames took longer than 16.7 ms and 33 ms. Averages hide hitches; the worst figures show them.
- Set `CCCP_PERF_LOG_GPU` as well to make the drawing stages wait for the GPU, so their times include its work. That slows the game down, so take frame rates from a run without it.
- `Blast` (night) and `BlastDay` are the scenarios for it: a blast on the bunker and the ground every 300 ms, then four big bombs at once every 1.5 seconds, then dropships blown up above the bunker.
- New timed scopes are one line each: `PerformanceMan::LogScope` for a block, `PerformanceMan::LogStages` for the stages of a long function, `PerformanceMan::AddLogCount` for a reading such as a light count.
