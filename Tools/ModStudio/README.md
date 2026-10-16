# Cortex Mod Studio

A visual editor for Cortex Command units. Move a unit's parts, swap sprites, add helmets and gear, and watch it stand,
walk, run, crouch and aim. Studio writes the INI for you and can show the unit in the game's Actor Editor while you work.

It is a web page plus a small PowerShell script, with nothing to install: no internet, no packages, no runtime.

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
- **Undo and redo** everything (Ctrl+Z, Ctrl+Y). Press **?** for all the keys.

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
- **Test In Sandbox.bat** opens the sandbox (`-modstudio -sandbox [map]`) with the unit as your own sandbox character, the tools put away. Add a map name to use
  another map, for example `"Test In Sandbox.bat" "Ketanot Hills"`.

**◧ Hide view** (V) hides Studio's own picture of the unit, so the parts and properties get the whole window while the game
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

The engine side of the preview is `Source/Managers/ModStudioPreview.cpp` (reading the request, reloading files, making the unit,
making it your sandbox character), used by `Source/Activities/ActorEditor.cpp`, plus `PresetMan::ReloadPresetFile`.
