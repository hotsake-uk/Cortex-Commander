# Cortex Mod Studio

A visual editor for Cortex Command units, and a modding IDE around it. Move a unit's parts, swap sprites, add helmets and gear,
and watch it stand, walk, run, crouch and aim. Studio writes the INI for you and can show the unit in the game while you work.
Deep mode adds the mod's files, Lua scripts with autocomplete from the game's own API, the game's console and search.

It is a web page plus a small PowerShell script, with nothing to install: no internet, no packages, no runtime. (The code
editor, Monaco, and the Lua parser, luaparse, are shipped in `vendor/` with their licences.)

## Opening it

Double-click **Mod Studio.bat** in `Tools\ModStudio` (or press **Mod Studio** in the launcher). It opens Studio in your browser
with the game folder already loaded; keep its small window open while you work, and close it to stop. Any browser works.

The .bat runs `Start-ModStudio.ps1` with Windows' built-in PowerShell: a tiny server on your own computer (localhost only) that
lets the page read and save the game's files. It's needed because browsers refuse to let a web page open `.ini` files in a
folder. If the .bat isn't inside a game folder, it asks which game folder to use; `-GameDir <folder>` picks one directly.

Opening `ModStudio.html` straight from disk also works, read-only: you can look at and change units, and **Save** downloads a
zip of the changed files to unpack into the game folder.

## What you can do

- **Pick a unit** from the Units list on the left. Official units are locked; click **Make my own copy** to make a new
  unit in your mod (`CopyOf` the original), then change anything.
- **Drag parts** in the view to move them. Hold **Shift** to move the sprite instead of the part, and
  **Ctrl** to rotate an attachment (also under Placement > Rotation). Dots show pivots: drag
  a joint dot to change where a part turns, and the hand dots to change idle, aiming and support hand positions.
- **Swap sprites**: select a part and click **Replace** in Sprite. Studio converts any PNG to the game palette, slices
  sprite sheets into frames and writes the files into your mod.
- **Add helmets and gear** from the Attachments tab, and try items in the hand from the Items tab or the Held item list.
- **Animate**: the pose buttons, Play, the aim slider, Sharp aim and Flip show how the unit moves in game, using the
  unit's own walk paths, limb lengths and arm reach.
- **Every setting**: All settings lists everything the selected part has (from its own block or inherited), editable, with a filter
  and **Add a setting** offering every key that presets of its class use. **Discard** throws away unsaved changes.
- **Undo and redo** everything (Ctrl+Z, Ctrl+Y). Press **?** for all the keys.

## Artist and Deep modes

The switch at the top left picks how much Studio shows.

- **Artist** is the unit designer on its own: pick a unit, move its parts, swap sprites. Nothing about scripts or files.
- **Deep** is the whole mod. The bar on the far left switches the side bar between **Files** (your mods, and the game's own
  files read only), **Units** (the designer's list), **Search** (every INI file and script, Ctrl+Shift+F) and **Package**.
  The designer is the first tab; files open in tabs beside it. The panel at the bottom (Ctrl+J) has the game's **Console**,
  **Problems** and **Watch**.

In Deep mode:

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
- **Saving a script** asks the running game to reload every script, and Lua typed in the console box runs in the game (an
  expression's value is printed). Ctrl+Enter runs the selection. Errors in the game's console link to the line, and show
  underlined in the file. `StudioWatch("name", value)` in a script shows a value in the Watch panel, live.
  This works in games started from `Tools\ModStudio` (they pass `-modstudio`).
- **Package**: Check looks for missing files, presets that don't exist, scripts with errors and unsaved changes; Export saves
  and zips the mod for sharing. Ctrl+P goes to any file.

## How files are written

- Official files in `Data` are never changed.
- Your mod's files are edited in place: only the lines you change are rewritten; comments, order and spacing stay.
- Changing a part a unit inherits copies that part into the unit's own block first, so other units that share it don't change.
- A new unit is saved as `<YourMod>.rte/Actors/<Name>.ini` and added to the mod's `Index.ini` (made for you if the mod is new).

## Preview in game

**Preview in game** saves, then writes `ModStudio/Preview.ini` in the game folder naming the unit, the files that changed and
the held item. Two ways to see it, both started from `Tools\ModStudio` and left running:

- **Actor Editor.bat** opens the game's Actor Editor (`-editor ActorEditor`), zoomed in on the unit. Press **Alt** to let go of the mouse and use
  Studio beside it; click the game to control the unit again. Ctrl + mouse wheel zooms.
- **Test In Game.bat** opens the Maginot map (`-modstudio -test [map]`) as a plain game, no sandbox and none of the scenario's
  attackers, with you in the unit. Add a map name to use another map.
- **Test In Sandbox.bat** opens the sandbox (`-modstudio -sandbox [map]`) with the unit as your own sandbox character, the tools put away. Add a map name to use
  another map, for example `"Test In Sandbox.bat" "Ketanot Hills"`.

**◨ Hide list** (L) hides the list on the left, and **◧ Hide view** (V) hides Studio's own picture of the unit, so the parts and properties get the whole window while the game
shows the unit beside it. Turn on **Live** (next to Preview in game) to send every change as you make it: a drag or a number scrub is sent when you let
go, an undo, a new held item or another unit straight away.

Either one checks the file about twice a second, reloads the changed files and sprites, and swaps in the new unit, holding the
item picked under **Held item**. The game only finds new mod folders when it starts, so after making a brand-new mod, restart
the game once.

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
| `js/ide.js` | Deep mode: tabs, the code editor, files, search, packaging, the game's console and watched values. |
| `js/lua-intel.js` | Lua completion, hover, signature help, go to definition and checks. |
| `js/ini-intel.js` | INI completion, hover, go to definition and checks, learned from every file Studio read. |
| `data/lua-api.json` | The game's Lua API, written from `Source/Lua/LuaBindings*.cpp` by `tools/make-lua-api.js` (run it with Node after changing the bindings). `data/lua-api.d.lua` is the same API as LuaLS annotations, for VS Code: add it to `Lua.workspace.library`. |

The engine side of the preview is `Source/Managers/ModStudioPreview.cpp` (reading the request, reloading files, making the unit,
making it your sandbox character), used by `Source/Activities/ActorEditor.cpp`, plus `PresetMan::ReloadPresetFile`. In games
started with `-modstudio` it also writes the console to `ModStudio/Console.log`, carries out `ModStudio/Scripts.ini` (reload
scripts, run `ModStudio/Run.lua`) and writes `StudioWatch` values to `ModStudio/Watch.ini`.
