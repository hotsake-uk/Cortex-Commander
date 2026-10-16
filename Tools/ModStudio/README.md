# Cortex Mod Studio

A visual editor for Cortex Command units. Move a unit's parts, swap sprites, add helmets and gear, and watch it stand,
walk, run, crouch and aim. Studio writes the INI for you and can show the unit in the game's Actor Editor while you work.

It is a single web page with no install and no external dependencies: no server, no internet, no packages.

## Opening it

1. Double-click `ModStudio.html`. Use **Chrome** or **Edge** (any recent version).
2. Click **Open the game folder** and pick the folder that holds `Data` (and `Mods`, if you have one).
3. Allow editing when the browser asks. Next time, **Reopen** brings you straight back.

Firefox and Safari can't save into a folder from a web page. Studio still opens there in read-only mode: you can look at
and change units, and **Save** downloads a zip of the changed files to unzip into the game folder.

## What you can do

- **Pick a unit** from the Units list on the left. Official units are locked; click **Make my own copy** to make a new
  unit in your mod (`CopyOf` the original), then change anything.
- **Drag parts** in the view to move them. Hold **Shift** to move the sprite instead of the part. Dots show pivots: drag
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

**Preview in game** saves, then writes `ModStudio/Preview.ini` in the game folder naming the unit and the files that
changed. Start the game's Actor Editor (`Launch Actor Editor.bat`, or launch with `-editor ActorEditor`) and leave it
running: it checks that file about twice a second, reloads the files and sprites, and shows the unit.

The game only finds new mod folders when it starts, so after making a brand-new mod, restart the game once.

## Code layout

All scripts are plain classic scripts (no modules), so the page works from `file://`.

| File | What it does |
| --- | --- |
| `js/ini.js` | Lossless INI reader and writer that keeps comments and untouched lines exactly. |
| `js/gamefs.js` | Game folder access (File System Access API, or read-only with zip download). |
| `js/library.js` | Loads every module, indexes presets, resolves `CopyOf`, decides where an edit can safely go. |
| `js/images.js` | Indexed PNG and BMP decoding with the game palette, palette conversion, PNG writing. |
| `js/rig.js` | The unit's skeleton and pose maths, matching the engine's arm, leg, limb path and attachable rules. |
| `js/viewport.js` | The drawing canvas: zoom, pan, overlays, handles and pixel-accurate picking. |
| `js/panels.js` | Library list, parts tree and property inspector. |
| `js/dialogs.js` | Make a copy, add attachments, pick items, import sprites, help. |
| `js/app.js` | App state, editing with undo, dragging, saving and the preview request. |

The engine side of the preview lives in `Source/Activities/ActorEditor.cpp` (`CheckModStudioPreview`) and
`PresetMan::ReloadPresetFile`.
