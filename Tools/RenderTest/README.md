# Render Test

These tools are for checking rendering changes by eye, against fixed scenarios, and against another build such as a `development` checkout.

## Setup

1. Build the game with `Build.ps1` (Debug Release by default), then run it once so `Userdata\Settings.ini` exists.
2. Run `Setup.ps1`. It installs `RenderTest.rte` into `Mods\` and writes scenario settings files to `Userdata\RenderTest\`. Each scenario is your `Settings.ini` with a few keys changed so the game starts straight into a known scene.

## Capturing

```powershell
.\Capture.ps1 -Scenario BunkerNight -Burst 6 -BurstIntervalMs 250
.\ContactSheet.ps1 -Name BunkerNight -Count 6 -Cols 3
.\Capture.ps1 -Scenario Caves -CameraPOI 2
```

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

- **Scenes:** six calm, fixed scenes (`GoldenNoon`, `GoldenNight`, `GoldenCaves`, `GoldenClassic`, `GoldenLightingOnly`, `GoldenInterior`). They have no explosions and use the built-in lighting defaults, so your own settings can't change them.
- **How it compares:** captures are cropped to the 960x540 game view and compared against the baselines. Small camera offsets are allowed by aligning first, within ±12 px.
- **Tolerances:** a scene fails when the mean channel difference goes over 6, or more than 4% of pixels differ clearly. Normal run-to-run noise is 0–4. A 20% exposure change scores about 12.
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
