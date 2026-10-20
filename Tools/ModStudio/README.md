# Cortex Mod Studio

A visual editor for Cortex Command units, and a modding IDE around it. Move a unit's parts, swap sprites, add helmets and gear,
and watch it stand, walk, run, crouch and aim. Studio writes the INI for you and can show the unit in the game while you work.
Script mode adds the mod's files, Lua scripts with autocomplete from the game's own API, the game's console and search.

It is a web page, built with Vite into `dist/`, that the Cortex Studio app (`Tools/CortexStudio`) serves and builds in for you.
(The code editor, Monaco, and the Lua parser, luaparse, are shipped in `vendor/` with their licences.)

## Opening it

Double-click **Mod Studio.bat** in `Tools\ModStudio` (or press **Mod Studio** in the launcher). It opens Studio in your browser
with the game folder already loaded; keep its small window open while you work, and close it to stop. Any browser works.

The .bat runs `Start-ModStudio.ps1` with Windows' built-in PowerShell: a tiny server on your own computer (localhost only) that
lets the page read and save the game's files. It's needed because browsers refuse to let a web page open `.ini` files in a
folder. If the .bat isn't inside a game folder, it asks which game folder to use; `-GameDir <folder>` picks one directly. It
serves the built page in `dist/`: with Node.js installed, run `npm ci` and `npm run build` in this folder first (the Cortex
Studio app does this itself).

## What you can do

- **Pick a unit** from the Units list on the left. Official units are locked; click **Make my own copy** to make a new
  unit in your mod (`CopyOf` the original), then change anything.
- **Drag parts** in the view to move them. Hold **Shift** to move the sprite instead of the part, and
  **Ctrl** to rotate an attachment (also under Placement > Rotation). Dots show pivots: drag
  a joint dot to change where a part turns, and the hand dots to change idle, aiming and support hand positions.
- **Swap sprites**: select a part and click **Replace** in Sprite. Studio converts any PNG to the game palette, slices
  sprite sheets into frames and writes the files into your mod.
- **Add helmets and gear** from the Attachments tab, and try items in the hand from the Items tab or the Held item list.
- **Design guns, craft and crabs** too: the Craft tab opens dropships and rockets, the Units tab also has crabs, and **Design** on
  an item opens the item itself. A gun turns with the aim about its grip, with dots for the grip, muzzle, shell ejection and
  back hand; Preview puts it in the hands of the last unit you opened. Engines, thrusters, hatch doors, legs and turrets move
  and turn like attachments.
- **Animate**: the pose buttons, Play, the aim slider, Sharp aim and Flip show how the unit moves in game, using the
  unit's own walk paths, limb lengths and arm reach.
- **Skeleton** (K, or the button above the view) shows how the unit is built: each part's bone from its parent to the joint where
  it attaches and turns, each part's origin, where hands and ankles reach, and its name. The pictures fade so the bones read. The
  box beside it says what each mark means and gives the selected part's ParentOffset, JointOffset and SpriteOffset in plain
  words; **How a unit is put together…** explains the idea and lists the whole unit as a tree. **Pictures** there (or Shift+K) fades,
  hides or shows the sprites under the bones; with them hidden, click a joint or origin to pick a part.
- **Every setting**: All settings lists everything the selected part has (from its own block or inherited), editable, with a filter
  and **Add a setting** offering every key that presets of its class use. **Discard unsaved changes** (in **Save ▾**) throws
  away unsaved changes.
- **+ New…** in the top bar makes a soldier, crab, dropship or rocket, gun, grenade, or an empty mod: pick one in the game to start
  from, name it and its mod, and change a few settings in plain words (health, weight, cost, passengers, rate of fire...). It's
  a copy (`CopyOf`) of the original with only your changes written in, saved and opened in the designer.
- **Data editors** (▦ in Script mode's activity bar), per module, as tabs: **Buy menu** (cost, buyable and groups of every unit,
  craft and item), **Loadouts** (the craft each comes in and what it brings, picked from thumbnails; make your mod a faction;
  copy a faction's loadouts into your mod) and **Sounds** (a sound's files to play, add and remove, and how it plays: volume,
  pitch, range, overlap...). Edits undo and save like any other; the game's own modules are read only unless unlocked.
- **Scenarios** (⚑ in the top bar, Ctrl+M) are missions or battles. A **mission** opens in the Mission Editor: its map, where its areas
  are drawn, beside its logic graph. A **battle** (+ New battle…) is one of the game's battle modes (capture the flag, king of the
  hill, assault, one flag, the point...) laid out on a map by hand, in the **Battle Editor**: the same map, and beside it the mode,
  the teams (who plays, their size and faction, which one the player commands), a checklist of what the mode needs placed (each
  team's spawn zones, flags, goal zones, the hills or objectives in order, flag or ship positions, pickup spots), its rules, and a
  red line saying what still stops it starting. Place one from the checklist or by right-clicking the map: a click for a point (it
  lands on the ground under it), a dragged box for a zone, or its corners with Zones set to Polygon. Drag to move, drag a corner to
  reshape, Alt-click an edge for a new corner, Shift-click a corner to take it out; Mirror copies a team's side across the map.
  A battle is a copy of Battle Command under its own name (`Activities/<name>.ini`) and its setup, in the form the game's Battle tab
  saves setups in, in `Battles/<name>.txt` in the mod. The game plays it as Battle Command with that setup started, and lists it
  with its map's battles in the map browser and the Sandbox's Battle tab, to play there too. The Battle Editor can copy a setup
  saved on the map in the game's Battle tab, or save the battle there. A mission's Start a battle step can start a battle saved
  for its map by name (`SandboxBattleSetup` in Lua).
- **Panels resize**: drag the edge of the list, the parts and properties panel, the line between the parts and the properties,
  or the console's top edge. Studio remembers the sizes; double-click an edge for the usual size.
- **Modules** in the top bar picks which modules (.rte) the lists, files and search show (All, My mods, Base only, or any set).
  Everything is still read, so copies and inheritance across modules keep working.
- **Undo and redo** everything (Ctrl+Z, Ctrl+Y). Press **?** for all the keys.

## Classic mode: mods for Community Edition

Studio also makes mods for a release build of Cortex Command Community Edition (6.2.2, or the nightly), so a modder doesn't need to
build the game. Point Studio at the folder you unzipped the release into (the one with `Data/Base.rte`), or let the Cortex Studio
app download it (the Game screen's **Community Edition** tab, or ⚙ **Get Community Edition…**). Studio sees it isn't
Cortex Commander and works in **Classic** mode, with a badge beside the project name saying which game; click the badge (or ⚙,
**Game the mods are for**) to pick another.

- Autocomplete, hover help and checks follow that release's engine: its Lua API and the INI properties each class reads come with
  Studio (`data/targets/<id>/`, since a release has no engine source). A key only Cortex Commander reads is underlined ("does
  nothing there") and isn't offered; a class only Commander has (AMech, ACShip...) is an error.
- The designer, sprites, gibs, guns, effects, walk paths, buy menu, loadouts, sounds, scenes, scenarios, the script editor and
  packaging all work. What only Commander's engine does is hidden: Preview in game, Live, Spawn, the console, Watch, Profiler,
  Inspector, reload on save, live values, Missions, building lights, scene weather, scenario settings, and the materials, walk paths
  and settings Community Edition doesn't have.
- New mods are written with the `SupportedGameVersion` that release loads (6.0.0 for 6.2.2, 7.0.0 for the nightly).
- In the Cortex Studio app, **▶ Play** starts the release in the game folder, and its ▾ has the Actor, Gib and Scene editors. The Game
  screen has only its Community Edition tab: building versions is Commander's only.

`node tools/make-targets.js --ce <clone of cortex-command-community/Cortex-Command-Community-Project>` writes `data/targets/` from each
release's tag in the clone (fetch its tags first; `nightly` moves), and `data/ini-surface.json` for Commander.

## Design and Script modes

The switch at the top left picks how much Studio shows.

- **Design** is the unit designer on its own: pick a unit, move its parts, swap sprites. Nothing about scripts or files.
- **Script** is the whole mod. The bar on the far left switches the side bar between **Files** (your mods, and the game's own
  files read only), **Units** (the designer's list), **Search** (every INI file and script, Ctrl+Shift+F) and **Package**.
  The designer is the first tab; files open in tabs beside it. The panel at the bottom (Ctrl+J) has the game's **Console**,
  **Problems** and **Watch**.

In Script mode:

- **Each item's files**: in the Units, Craft, Attachments and Items list, the ▸ beside an item shows the files it uses, one item
  at a time: the INI lines that make it (and what it copies), the scripts it runs, its pictures and sounds, its parts, and the
  scripts that name it, and **Related**: units (or items) that copy it or copy the same thing, run the same scripts, use the
  same parts or pictures, rarest in common first. Click one to open it (an INI at the right line, a picture in a tab, a sound
  plays, a related unit in the designer). Pictures zoom with the wheel, + − 0 1 or the buttons above them, and drag to move.
- **Profiler** and **Inspector** (bottom panel, with a game started from Studio's Play menu): the Profiler shows frames a second,
  update and draw time, what's in the world, where a frame's time goes and the costliest scripts. The Inspector lists every actor
  in the game; pick one to see its live values, parts and inventory (it's ringed in the game), pause and step the game, and
  change it: health, team, stop, lift, take control, kill, gib, remove, or open it in the designer. Starting Test in game or
  Sandbox from Studio opens it by itself (untick Open on Test/Sandbox to stop that); ★ Follow me keeps the unit you control
  selected as you switch bodies; units that arrive are marked new, and What happened lists arrivals, deaths and departures.
- **Lua scripts** open in a code editor that knows the game's API: type `self.` or `self:` for what the object has, any
  manager (`MovableMan:`), `function ` for the functions the game calls (Create, Update, OnCollideWithMO…), and
  `CreateAHuman("` for preset names. Hover for help, Ctrl+click or F12 to go to a definition (a function in another script,
  a preset's INI). Mistakes are underlined as you type: syntax errors, functions a class doesn't have, callbacks the game will
  never call (`update` for `Update`), presets that don't exist. "self" is the class of the presets that use the script;
  `---@type` and `---@param` comments (as VS Code's Lua extension reads them) say what other values are.
- **INI files** open as text with completion for the keys a block of that class takes, the values a key is given elsewhere,
  presets for `CopyOf` and file paths, and checks for missing files and presets. The designer and the text are the same file:
  a change in one shows in the other, and undo works across both.
- **The Scripts section** of a part's properties lists its scripts; **New script** writes one from a template and adds it.
  Script paths anywhere in the properties open the script when clicked, and in INI and Lua files with Ctrl+click. Over a
  script or picture, **Used by** lists the presets that use it; **Find where it's used** in the file menu lists them too.
- **New INI file** can start as a unit, gun or scene copied from one of the game's, or a scripted activity with its script.
  The file is added to the mod's Index.ini.
- **Saving a script** reloads just that script in the running game: the presets and objects that use it run its new code (their
  Create runs again) and everything else keeps its state. A script no object uses (a library other scripts load) reloads
  everything, as does the Reload scripts button. Lua typed in the console box runs in the game (an
  expression's value is printed). Ctrl+Enter runs the selection. Errors in the game's console link to the line, and show
  underlined in the file. `StudioWatch("name", value)` in a script shows a value in the Watch panel, live.
  This works in games started from `Tools\ModStudio` (they pass `-modstudio`).
- **Live values**, as in Quokka: end a line with `--?` and, once saved, the value it makes shows at the end of the line while the
  game runs, with how many times it ran (hover for the last few). `--?.` shows how long the line takes, `--?+` shows tables wider,
  `--? $.Health` shows any Lua with `$` as the line's value, and `--[[?]]` right after an expression shows just that expression.
  `--? self.Pos` alone on a line shows that Lua's value at that point. Or click left of a line number for a logpoint: the line is
  logged with nothing written in the script (the game's own scripts too), and it starts at once if the script is saved.
  Studio runs a copy of the script with the probes in it (`ModStudio/Live/`), never changing the script; errors still point at the
  script's own lines. Needs a game built from 8.5.252 or later.
- **Package**: Check looks for missing files, presets that don't exist, scripts with errors and unsaved changes; Export saves
  and zips the mod for sharing. Ctrl+P goes to any file.

## How files are written

- Official files in `Data` are never changed (unless you unlock everything in the app's Game screen).
- Your mod's files are edited in place: only the lines you change are rewritten; comments, order and spacing stay.
- Changing a part a unit inherits copies that part into the unit's own block first, so other units that share it don't change.
- A new unit is saved as `<YourMod>.rte/Actors/<Name>.ini` and added to the mod's `Index.ini` (made for you if the mod is new).
- The designer changes only your mods: ones Studio made, and ones you chose **Edit this mod** on. Other people's mods are locked
  like the game's own until then (**Make my own copy** works on them too).
- In the Cortex Studio app, **Unlock everything** on the Game screen lifts all of that for the built version: the game's own files
  and every mod are changed in place, and Reset there puts them back.
- Every version is kept. The first time Studio changes a mod it keeps the mod as it was, then every save or delete is kept too, so
  **History** (in **Save ▾**) puts any file back, and **Undo** on the Saved message undoes that save. The Cortex Studio app keeps
  each mod's history with git (built in, nothing to install) in `CortexStudio/History` in your config folder (`%APPDATA%` on
  Windows); in a browser, Studio keeps copies there instead (the original and the newest 5 of each file).

## Preview in game

**Preview in game** saves, then writes `ModStudio/Preview.ini` in the game folder naming the unit, the files that changed and
the held item. Two ways to see it, both started from `Tools\ModStudio` and left running:

- **Actor Editor.bat** opens the game's Actor Editor (`-editor ActorEditor`), zoomed in on the unit. Press **Alt** to let go of the mouse and use
  Studio beside it; click the game to control the unit again. Ctrl + mouse wheel zooms.
- **Test In Game.bat** opens the Maginot map (`-modstudio -test [map]`) as a plain game, no sandbox and none of the scenario's
  attackers, with you in the unit. Add a map name to use another map.
- **Test In Sandbox.bat** opens the sandbox (`-modstudio -sandbox [map]`) with the unit as your own sandbox character, the tools put away. Add a map name to use
  another map, for example `"Test In Sandbox.bat" "Ketanot Hills"`.

**◨ Hide list** (L) and **◧ Hide view** (V), at the bottom left, hide the list on the left and Studio's own picture of the unit, so the parts and properties get the whole window while the game
shows the unit beside it. Turn on **Live** (next to Preview in game) to send every change as you make it: a drag or a number scrub is sent when you let
go, an undo, a new held item or another unit straight away.

Either one checks the file about twice a second, reloads the changed files and sprites, and swaps in the new unit, holding the
item picked under **Held item**. The game only finds new mod folders when it starts, so after making a brand-new mod, restart
the game once.

## Editing in your own editor (the file bridge)

While Studio's window is open it watches `Data` and `Mods`. Save a `.lua` or `.ini` in any editor (VS Code, Notepad++, anything)
and a game started from Studio reloads it, the same as saving inside Studio:

- A script reloads on its own, so other objects keep their state (everything reloads when no object uses it).
- An INI file is read again and the last unit from Studio comes back with the change, when the file is in its mod or its held item's.
- An open Studio page takes the new text too: open tabs, the designer, and pictures you repainted. A file with unsaved edits in Studio
  keeps yours, and Studio says so.

**Mod Studio Bridge.bat** runs it without opening the page: leave the game and the bridge window open and work entirely in your own
editor. The window lists each file it sends to the game, and prints Studio's address in case you want the page as well. Saves made by
Studio itself aren't sent twice. Start Studio with `-NoBridge` to turn the watching off.

## Code layout

All scripts are plain classic scripts (no modules), so the page works from `file://`.

| File | What it does |
| --- | --- |
| `js/ini.js` | Lossless INI reader and writer that keeps comments and untouched lines exactly. |
| `Start-ModStudio.ps1` | The local server: serves the page and lists, reads and saves files in the game folder. |
| `js/gamefs.js` | Game folder access (through the local server, or read-only with zip download). |
| `js/library.js` | Loads every module, indexes presets, resolves `CopyOf`, decides where an edit can safely go. |
| `js/images.js` | Indexed PNG and BMP decoding with the game palette, palette conversion, PNG writing. |
| `js/rig.js` | The unit's skeleton and pose maths, matching the engine's arm, leg, limb path and attachable rules. |
| `js/viewport.js` | The drawing canvas: zoom, pan, overlays, handles and pixel-accurate picking. |
| `js/panels.js` | Library list, parts tree and property inspector. |
| `js/dialogs.js` | Make a copy, add attachments, pick items, import sprites, help. |
| `js/app.js` | App state, editing with undo, dragging, saving and the preview request. |
| `js/layout.js` | Resizable panels (drag a panel's edge, double-click for the usual size) and the Save ▾ menu. |
| `js/main.js` | The scripts in the order they load: Vite bundles from here (`npm run build`, see `vite.config.js`). |
| `js/ide.js` | Script mode: tabs, the code editor, files, search, packaging, the game's console and watched values. |
| `js/lua-intel.js` | Lua completion, hover, signature help, go to definition and checks. |
| `js/ini-intel.js` | INI completion, hover, go to definition and checks, learned from every file Studio read. |
| `data/lua-api.json` | The game's Lua API, written from `Source/Lua/LuaBindings*.cpp` by `tools/make-lua-api.js` (run it with Node after changing the bindings). `data/lua-api.d.lua` is the same API as LuaLS annotations, for VS Code: add it to `Lua.workspace.library`. |
| `js/target.js` | The game the mods are for: Commander, or a Community Edition release (Classic mode), found from the game folder. |
| `data/targets/<id>/` | A Community Edition release's Lua API and INI properties, written by `tools/make-targets.js`; `data/ini-surface.json` is Commander's INI properties, for comparing. |

The engine side of the preview is `Source/Managers/ModStudioPreview.cpp` (reading the request, reloading files, making the unit,
making it your sandbox character), used by `Source/Activities/ActorEditor.cpp`, plus `PresetMan::ReloadPresetFile`. In games
started with `-modstudio` it also writes the console to `ModStudio/Console.log`, carries out `ModStudio/Scripts.ini` (reload
scripts, run `ModStudio/Run.lua`) and writes `StudioWatch` values to `ModStudio/Watch.ini`.
