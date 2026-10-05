# Workbench plan: the companion app

**6 October 2026: cut back.** The Workbench is now five pages: Launch, Mods, Tests, Builds, Jobs. The Live and Graphics pages described below were removed: tuning and presets live in the game's own settings panel (F6), and a launch profile can name one of those presets. The Launch page lists the games it started and can close them. The rest of this file is the earlier plan, kept for reference.

The Workbench today is a browser page that manages mods, runs tests and builds the game. This plan turns it into the place you start the game from, control it while it runs, and shape how it looks.

Status (5 October 2026): **steps 1 to 4 are built.** Launch profiles, the live link, the Live page, graphics presets and the live editor work. Gallery, Performance and polish (steps 5 and 6) are not built.

Decisions taken: the game listens for commands only when started from the Workbench or when `ControlLinkPort` is set in Settings.ini; a preset holds every Graphics Lab setting and nothing else (time of day and weather are left out); it stays a browser page.

Checked so far: launching a profile, reading a running game's state, changing settings live, applying and saving presets, console lines and screenshots, all through the Workbench's requests; the pages load and the editor lists all 56 settings. Not yet tried by hand: the profile editor's form, the sliders on the Live and Graphics pages, "Make my default", mod lists in profiles.

## What you will be able to do

1. **Launch the game any way you like.** One click for "straight into the Sandbox on this map at night", "debug build with the Graphics Lab open", "the classic look", "vanilla, no mods". Save your own combinations.
2. **Control a running game from the page.** Change time of day, weather and every lighting setting while you watch. Type console commands. Take screenshots.
3. **Keep graphics presets.** Save the current look under a name, switch between presets with one click while the game runs, compare two, set one as your default.
4. **See everything in one place.** What's running, what the console says, how fast it runs, your screenshots, your mods, your builds.

## The piece that makes it possible: a link to the running game

*Right now the Workbench can start the game and read its log files, but cannot talk to it. Everything "live" needs a link.*

- The game listens for commands from this computer only, and only when asked to (started from the Workbench, or a setting). A normal launch outside the Workbench is unchanged.
- The Workbench sends short text commands and gets text back.
- Commands at the start:
  - **Run a console line.** Anything the in-game console can do.
  - **Read and change a setting by name.** Uses the same names as `Settings.ini`, through the code that already reads that file, so every existing setting works without writing new code for each.
  - **Report state.** Scene, game mode, frame rate, unit and particle counts, what is open.
  - **Take a screenshot** to a named file.
- The same link is what lets several test copies be steered at once later.

Risk to check first: whether a single setting can be applied mid-game through the existing reader without side effects. If some can't (resolution, anything that needs a restart), the page will mark those "applies on next launch".

## Pages

| Page | What it's for | State |
|---|---|---|
| **Launch** | Launch profiles: pick one, press Play. | built |
| **Live** | The games running now: state, console, quick controls. | built |
| **Graphics** | Presets and live sliders. | built |
| **Mods** | State, test, park, activate. | exists |
| **Tests** | Regression scenes, scenario captures. | exists |
| **Builds** | Build the test and game programs. | exists |
| **Gallery** | Screenshots and captures, newest first. | new |
| **Performance** | Where frame time goes, from the performance log. | new |
| **Jobs** | Everything the Workbench has been asked to do. | exists |

### Launch

A launch profile is a saved answer to "start the game how?":

- **Which program:** the game build or the debug build.
- **Where it starts:** main menu, or straight into a game mode and map (Sandbox, Test Activity, any scenario).
- **World:** time of day, weather, wind.
- **Graphics preset** to start with.
- **Mods:** your normal set, none, or a chosen list (uses the sandboxed mods folder, so your real `Mods\` is untouched).
- **Extras:** open the Graphics Lab or World Debug on start, pause the AI, a global script to run, window size.

Built-in profiles to start with: Play (normal), Sandbox, Sandbox at night, Debug with tools open, Classic look, Vanilla (no mods), Stress battle. You can copy, edit, rename and delete them. Several can run at once.

How it works: the game already reads an alternative settings file and a set of switches at start (the test harness uses them). A profile is those, saved under a name.

### Live

One card per running game:

- What it is (profile, build), the scene and mode, frames per second, unit and particle counts.
- **Quick controls:** time of day, weather, game speed, pause AI, zoom.
- **Console:** the game's console output as it happens, and a box to type commands.
- **Buttons:** screenshot, open Graphics Lab, open World Debug, save game, close.
- A crash is shown with its reason and call stack (the crash-dump reader already exists).

### Graphics

- **Presets list.** Built in: Classic, Natural, Gritty, Vivid, Noir, the quality levels, and "Lamps only" (dark interiors lit by their lamps). Plus your own.
- **Apply** a preset to a running game with one click. Pick which game if several are running.
- **Save current as...** reads the running game's settings and stores them under a name.
- **Edit:** every graphics setting as a slider or switch, grouped (light, shadows, materials, sky, effects, colour). Moving a slider changes the running game at once.
- **Compare:** flip between two presets with one key, or capture both as pictures side by side.
- **Manage:** rename, duplicate, delete, export to a file, import one, mark as the default the game starts with.
- A preset stores only graphics settings. Gameplay switches (fire, liquids, collapse) are a separate small "rules" set, so a look never changes how the game plays.

### Gallery and Performance

- **Gallery:** every screenshot and test capture with the profile and preset it was taken with. Click to enlarge, open the folder, delete.
- **Performance:** start a timed run from a profile, then see average and worst frame time split by stage (simulation, drawing, lighting), and compare two runs. Uses the performance log that already exists.

## Order of work

| Step | What gets built | You can then |
|---|---|---|
| 1 | Launch page and profiles. No engine changes. | Start the game straight into any mode, map, time and mod set. |
| 2 | The link to the running game, and the Live page. | Watch and steer a running game; use the console from the page. |
| 3 | Graphics presets: save, apply live, manage. | Switch looks while playing. |
| 4 | Graphics editor: live sliders, compare. | Tune a look from the page and save it. |
| 5 | Gallery and Performance. | Review pictures and measure changes. |
| 6 | Polish: layout, keyboard shortcuts, search, a tidy first-run. | Use it daily without friction. |

Each step is usable on its own and is committed separately. Steps 1 to 3 are the core; I'd expect them to be most of the value.

## What stays out

- **Not a mod downloader or store.** It manages mods you have.
- **Not a replacement for the in-game Graphics Lab.** That stays; the Workbench and the Lab change the same settings.
- **No internet access, no accounts.** It talks only to this computer.
- **Windows first.** Nothing in the design prevents other systems, but it will only be tested here.

## Known loose ends to clear first

- The last regression run stopped after five of seven scenes without saying why. That needs finding before the Tests page can be trusted.
- The Tests page pictures and the Mods page Park and Activate buttons have not been tried by hand.

## Decisions for you

1. **When may the game listen for commands?** I recommend: only when started from the Workbench, or when a setting turns it on. The alternative is always on.
2. **What does a graphics preset cover?** I recommend: graphics only, with gameplay switches kept separately. The alternative is one preset for both.
3. **Stay a browser page, or wrap it as a desktop app with its own window and icon?** I recommend staying a browser page for now; a wrapper can be added in step 6 without changing anything underneath.
