# Cortex Commander: what has been added

This document lists every system and feature this fork adds to the Cortex Command Community Project, and that works today. It is written in plain technical English. Where an idea is abstract, a short note in *italics* explains it.

Last updated: 5 October 2026.

**How to read it**

- Each section is one area of the game. Each bullet is one feature.
- "Setting" means a line in `Settings.ini` that turns the feature on or off, or changes its strength. Most settings also have a control in the game.
- Things that are built but have not been looked at in real play are listed at the end, under "Built but not yet checked".
- For the exact setting names, INI properties and Lua functions, see `Documentation/LightingAndEffects.md`.

**A few words used throughout**

| Word | Meaning |
|---|---|
| Renderer | The part of the program that draws the picture. |
| GPU | The graphics card. Drawing on the GPU is much faster than drawing on the main processor. |
| Shader | A small program that runs on the GPU for every pixel. Most visual effects here are shaders. |
| Simulation ("sim") | The part of the program that decides what happens: physics, damage, AI. It is separate from drawing. |
| Deterministic | The same inputs always give the same result. The simulation must stay this way so saved games and replays behave. |
| Sprite | A small picture: a soldier's arm, a gun, a crate. |
| Terrain | The destructible ground and buildings. It is stored as a picture, one pixel at a time. |
| INI file | A text file that describes game content: units, weapons, bunker pieces. Mod makers write these. |
| Lua | The scripting language mods use for behaviour. |
| Preset | One named thing defined in an INI file, such as "Soldier Light". |
| Mod (module) | A folder ending in `.rte` that adds content to the game. |

---

## 1. The foundation: a GPU renderer that matches the old game

*The original game drew everything on the main processor, one pixel at a time. The Community Project had started a new renderer that draws on the graphics card, but it was unfinished. Nothing else in this document would be possible without finishing it.*

- **The unfinished GPU renderer was merged and completed.** The title screen, menus, HUD, scripted drawing, tracer trails, split screen and fog of war all draw correctly again.
- **Smooth motion between simulation steps.** The simulation runs at a fixed rate. The renderer now draws in-between positions, so movement looks smooth at any frame rate.
- **Classic mode.** With Lighting, Bloom and Extra Effects turned off in the Video settings, the game looks like the original. A regression test checks this.
- **Soft fog of war.** The fog that hides unexplored areas is back, with soft edges.
- **Sharp upscaling.** The game is drawn at its small internal size and enlarged to the window. The enlargement keeps pixels crisp without shimmering.
- **Per-object blend modes.** Any object can be drawn see-through, or as glowing light added on top of the scene (for energy, plasma, fire).

## 2. Lighting

*In the original game every pixel is drawn at full brightness. Here, the picture is drawn first and then "lit": each pixel is made brighter or darker depending on what light reaches it.*

- **Sky light.** Daylight comes from the open sky and fades as it travels into caves and tunnels. Dig a hole and light comes in.
- **Interior light.** A base level of light where no sky light reaches, so bunkers and caves are not black. One slider controls it (see section 15).
- **Light from every glow.** Muzzle flashes, explosions, fire, thrusters and any other glow effect cast real light in their own colour. Old content gets this with no changes.
- **Lights on objects.** Any object can carry a light: all-round, or a beam like a torch. The light moves and turns with the object.
- **HDR.** *Brightness is calculated with a much wider range than a screen can show, then compressed for display. This is what lets an explosion look blinding without washing the picture out.*
- **Edge lighting.** Sprites and terrain automatically get a slight bevel at their outlines, so edges facing a light catch it.
- **Glowing colours.** Some colours in the game's palette always shine: tracers, gold, hot metal, fresh blast craters.
- **Bounce light.** Lit surfaces pass some of their colour to nearby surfaces. A stronger version (called radiance cascades) is part of the Ultra quality preset.
- **Auto exposure.** The picture adapts when the whole screen is very bright or very dark, like an eye or a camera does.
- **Light in smoke.** Smoke near a fire, a lamp or a muzzle flash glows with that light.

## 3. Shadows

- **Terrain blocks light.** A light behind a wall does not light the other side. Shadow edges are soft.
- **Units and objects cast shadows.** Soldiers, crates, doors, craft and wreckage block light from lamps, flashes and explosions. Shadows are sharp near the feet and softer further away.
- **The sun has a direction.** Daylight comes from one side and turns with the time of day. The far side of a hill and the ground under an overhang are dimmer and cooler. At night the moon does the same, more faintly.
- **Sunbeams indoors.** Where the sun shines through an opening into a cave or bunker, it lights what it falls on and the beam is visible in the air.
- **Contact shading.** Back walls darken slightly right next to objects and solid ground, so things look attached to the scene.
- **Opt out.** An object can be marked as not casting a shadow (energy shields, holograms).

## 4. Materials and surface states

*"Material" here means how a surface reacts to light: dull like earth, glossy like plastic, or mirror-like like steel.*

- **Metal and gloss.** Every object and every kind of terrain has two values: how metallic and how glossy. Metal reflects its surroundings and takes coloured highlights. Glossy things take small bright highlights.
- **Sensible defaults.** Robots, drones, turrets and craft in the stock game are marked as metal. Dummies are glossy plastic. Guns count as steel. Terrain looks like what it is made of.
- **Rounded shading.** Metallic and glossy parts shade like tubes, not flat cut-outs.
- **Relief.** The shading an artist painted on a sprite (plates, rivets, folds) is treated as real bumps that catch light.
- **Shine.** Lights throw highlights on metal, concrete, wet ground and water.
- **Surface states.** Units show what has happened to them. These are looks only; they do not change gameplay.
  - **Wet:** darker and glossy after wading or standing in rain. Dries in about twenty seconds.
  - **Sooty:** blackened by nearby explosions and by burning. Wears off, and washes off in water.
  - **Snow:** settles on the head and shoulders of a unit standing still in snowfall.
  - **Hot:** guns glow after sustained fire, metal glows where it is hit, burning units glow.

## 5. Lights in the scenery

- **Bunker pieces carry lamps.** A piece of scenery can define lamps in its INI file. When the piece is placed, its lamps become part of the scene and are stored in saved games.
- **Stock pieces are lit.** 67 stock bunker pieces now have real lamps where their artwork already showed one, 90 lamps in all.
- **24 placeable fixtures.** A new build group, "Bunker Lights": ceiling lamps, wall lamps, floor lamps, tiny indicator lights, strip lights, floodlights (beams), a pulsing red warning beacon and a flickering lamp. They are painted on the back wall, so they never block movement.
- **Lamps can be destroyed.** A lamp goes out for good when a shot passes through it, when an explosion goes off nearby, or when the ceiling or wall it hangs on is destroyed.
- **Glowing equipment.** Brain cases, teleporters and consoles give off a little light.
- **Light controls.** How colorful all light is, a tint on all of it, and brightness, reach and tint for scenery lamps. Headlamps have brightness, reach, beam width, color, team color and a daytime switch, and a unit can have its own. Tracers shine in their own color.

## 6. Sky, time of day and weather

- **Time of day.** Any hour from 0 to 24. Dawn, dusk and night tint the sky and the light. Time can stand still or pass as a day and night cycle.
- **Sun and moon.** The sun crosses the sky by day and turns warm near the horizon. At night there are stars and a moon.
- **Cloud shadows.** Clouds drift with the wind and their shadows cross the ground.
- **Weather.** Clear, rain, snow, ash fall or dust storm, each with a strength and a wind speed. Rain and snow do not fall under overhangs or in caves.
- **Lightning** in heavy rain.
- **Haze.** Distant background layers fade into the air, and are slightly blurred.
- **Living world.** Plants sway in the wind and bend away from blasts. Snow builds up on open ground and melts. Rain darkens surfaces, which then dry.
- **Scenes can set their own atmosphere.** A map can say what time and weather it has. The scenario setup screen also has Time and Weather buttons.

## 7. Effects

- **Bloom.** Very bright things glow softly around their edges.
- **Heat haze and shockwaves.** Air shimmers above hot things. Explosions send out a visible ring that bends the picture as it passes.
- **Explosions.** Fireballs swell, rise and turn to dark smoke that hangs for a few seconds. A ring of dust races along the ground.
- **Sparks, dust and debris.** Explosions and fast hits on terrain throw them. Hard materials spark; soft ones puff dust. These are visual only.
- **Scorch marks.** Explosions blacken the terrain, and crater rims glow while they cool.
- **Embers** rise from fire.
- **Stains.** Blood and oil mark the terrain.
- **Tracer light.** Fast shots with a trail light what they fly past.
- **Colour grading.** Controls for exposure, saturation, contrast, colour temperature, tints, vignette (darker corners), film grain, lens fringing and optional TV scanlines.
- **Looks.** Four ready-made styles: Natural, Gritty, Vivid, Noir.

## 8. New simulation systems

*These change what happens in the game, not just how it looks. Each one runs in fixed steps with its own random numbers, so the simulation stays deterministic. Each has its own on/off setting.*

- **Spreading fire.** Grass and plants burn away. Wood burns slowly to ash. Oil burns fast. Fire needs air, climbs upward, spreads downwind, is weakened by rain and snow, and is put out by water. Burning ground gives light, smoke and flames that hurt.
- **Flowing liquids.** Water, lava, acid and oil in the terrain fall, run downhill, pool and find a common level, even through a tunnel.
  - Liquid has momentum: it pours in an arc, speeds up as it runs and sloshes back off walls.
  - Liquids layer by weight: oil floats on water, water on acid, and everything on lava.
  - Explosions, units and boulders falling in throw splashes. The drops fly, land and join the liquid again, so none is lost.
  - In snowy weather still water freezes over. Lava melts ice and snow.
  - Water puts out fire.
  - Lava glows, sets things alight, and turns to stone where it meets water.
  - Acid eats soft ground and is used up doing it.
  - Oil burns where it pools.
- **Loose sand and snow.** Sand, snow, rubble and ash slide and pile at a slope when something disturbs them, and sink in liquid.
- **Collapsing terrain.** After a big explosion, pieces of ground it cut loose fall as solid bodies with weight. Something that was already floating stays up when chipped; cut in two, the smaller part falls. A piece left hanging by a thin neck snaps off. They tip, roll and slide to rest, crack into smaller pieces if they land hard, hit units in their way and can be shot apart as they fall. Pieces of buildings fall too once nothing holds them up.
- **Burning units.** Flames, napalm, burning ground and lava set units of flesh alight; machines never burn, and lasers, explosions and jetpacks don't set anyone on fire. A burning unit takes damage, panics, runs, and spreads fire to what it touches. Water puts it out.
- **Steam.** Water on fire, and lava meeting water, throws up steam.
- **Swimming and drowning.** Units move through liquid instead of destroying it. Wading slows them. Light units float and heavy ones sink. Living soldiers run out of air after 12 seconds under water; robots do not breathe.
- **Smoke and gas block sight.** Units and the AI cannot see through thick smoke or steam, so smoke screens work.
- **Weather that matters.** Rain and snow weaken fire. Snow slows walking by up to 15%. Wind pushes fire. Dust storms cut how far units see.
- **Night matters.** After dark, soldiers wear headlamps that light where they aim. The AI sees about half as far at night without one.
- **Saved games** store burning fire and moving liquid, and continue them when loaded.

## 9. New equipment

| Item | What it does |
|---|---|
| Smoke Grenade | A thick screen for about ten seconds. |
| Toxic Gas Grenade | A cloud that hurts anyone inside and blocks sight. |
| Flare | Burns red for about 45 seconds, lighting the area. |
| Napalm Flamer | Sprays burning fuel that pools and keeps burning. |
| Water Cannon | Knocks units back and puts fires out. |
| Acid Sprayer | Lobs acid that stings and eats soft ground. |
| Fuel Barrel | Leaks oil when shot, and explodes in burning fuel. |

## 10. Sandbox mode

*A game mode with no goal. You are not a soldier; you look down on the battlefield and can place, command and destroy anything.*

- **Spawn** units from any faction, brains and items, for four sides. Choose squad size, weapons and orders.
- **Build** bunkers during play with the game's own build menu. Money never runs out.
- **Orders:** hold, attack the nearest enemy, hunt brains, patrol, or go to a rally point.
- **Command:** drag a box around units, then click where they should go or what they should attack.
- **Take control** of any unit and play it yourself.
- **Drop squads** arrive by dropship or rocket.
- **Auto battle:** give each side a faction and a budget. The AI buys and sends waves until one side is left.
- **Paint** fire, water, lava, acid, oil, smoke, gas, earth, sand, ice, grass, wood and concrete. Pour loose sand and snow. Drop boulders and lumps of concrete. Dig. Call down grenades, bombs, napalm and lightning.
- **Strikes from the sky.** Click where it should land: a rocket, a barrage of rockets, a stick of bombs, artillery, napalm, a beam that bores straight down, or a rain of boulders. Three sizes of crater blast as well.
- **Things to knock down.** One click builds a concrete beam, pillar, room or four-storey tower, a wooden bridge, a floating island, or a tank full of water.
- **Speed of time.** A slider from a twentieth of normal speed to three times, to watch a collapse slowly or hurry a battle along.
- **Pause AI:** every AI unit stands still while you set things up.
- **World controls:** weather, wind, time of day, slow motion.
- **Follow** a unit with the camera, or let the camera follow the fighting.
- The same tools open in any other game mode with **F7**.

## 11. Camera, HUD and feel

- **Camera zoom.** Ctrl + mouse wheel zooms out to see about six times as much of the map, or in to 2x. Lighting and HUD work at any zoom.
- **Modern HUD** (optional): a minimap, health and ammo bars, an air bar under water, a feed of units lost, damage numbers, and an arc showing where a hit came from.
- **Smooth HUD text.** Text is drawn with a crisp modern font at the window's full resolution.
- **Interface scaling.** Debug windows and the modern HUD scale with the window size.
- **Hit-stop.** A big explosion in view freezes the game for a few hundredths of a second, which makes it feel heavier. Only timing changes, never the simulation.
- **Recoil kick.** The view kicks back when your unit fires.
- **Smooth screen shake.**
- **Frame cap.** Limits frames per second to save power.
- **Photo mode (F8).** Freezes time, hides the HUD, gives a free camera and look sliders, and saves screenshots at up to four times the internal size.

## 12. Settings and tools

- **Video settings.** Switches for Lighting, Bloom and Extra Effects, and quality presets from Potato to Ultra.
- **Docked tool windows.** The Sandbox and the settings panel sit in panels at the sides of the window, as tabs where a side has more than one, and the game's picture is fitted between them instead of being covered. A switch lets them float again.
- **Settings panel (F6).** Every setting that can be tuned while the game runs, in one panel: eleven categories, a search across all of them, and named presets that hold everything (files in `Userdata/Presets`). It replaces the World Debug, Graphics Lab and Debug Options windows.
- **Tab.** In a game, puts every tool window away or brings them back. In the Sandbox game mode that is the switch between the tools (with the world paused) and your own character.
- **Sandbox character.** A body and kit of your choice with optional abilities: no harm, endless jetpack and ammunition, number keys for the kit, flying through anything, ignored by enemy AI. It can be switched off to only look around.
- **Colony buildings (first step).** A barracks trains units and keeps a number alive; an extractor earns supply. Hidden for now: the Sandbox's Colony tab is switched off in the code (`c_ShowColonyTab` in `Sandbox.cpp`).
- **Ambient lighting slider.** One control for how bright interiors are without lamps. Turn it down and bunkers are lit by their lamps, and go dark where the lamps are shot out. It also lowers the minimum light on units, so units in a dark room are dark too.
- **Settings versioning.** When the default look changes a lot, old saved values are ignored so players get the new look.

## 13. Additions for mod makers

- **In INI files:** lights on objects (colour, reach, brightness, flicker, beam), lamps on scenery pieces, metal and gloss values, blend mode and opacity, shadow casting on or off, shimmer, visual particle emission, and per-scene time and weather.
- **In Lua:** time of day, weather and light colours; adding lights and shimmer; pouring liquid; asking whether smoke blocks a line of sight; reading fire and liquid counts; camera zoom; applying a look; surface states; scenery lamps; and all the sandbox tools.
- **Old content improves by itself.** Glows become lights, sprites get edge lighting, smoke scatters light, without any change to the mod.

## 14. Loading mods made for older versions

*This part is new and is built and tested, but not yet committed to the repository.*

- **Version mismatch is a note, not an error.** A mod made for version 6 or older now loads. Before, every such mod stopped with an error.
- **A broken mod is skipped, not fatal.** If a mod has a real error, the game leaves that mod out, carries on, and reports the reason in the console, in `LogLoadingWarning.txt` and in the Mod Manager.
- **Small problems are forgiven.** A setting the game no longer has, or a missing sound file, is noted and passed over.
- **Old formats are understood.** Old-style jetpacks, old jetpack settings on units, and renamed stock gun sounds are handled.
- **Tools.** `Tools/Mods/Get-Mods.ps1` downloads a list of mods from mod.io. `Fix-Mods.py` applies known text fixes. `Test-Mods.ps1` loads every mod and reports failures, and can put each mod's units and weapons through a short scripted fight to find script errors.
- **Result so far.** 30 mods (34 modules) were downloaded and all 34 load. Eight run cleanly and are in `Mods\`. The other 26 are parked in `ModsParked\` until their script errors are fixed or they are tested in play.

## 15. Performance work

- **Draw call pooling and batching.** *The game sends the graphics card far fewer, larger instructions.* Frame rate on the stress scene rose from 169 to 250.
- **Sprite atlas.** Small sprites share large texture pages. About 18% more frames per second.
- **Terrain uploads only what changed.**
- **Collapse check.** The check after each explosion took 20 to 38 milliseconds; it now takes about 1.
- **Big battles.** A leftover step that drew every object a second time was removed, and particles are drawn in batches. A 500-unit battle went from 88.8 to 78.3 milliseconds per frame.
- **Liquid step.** About 2.5 times faster, with a fixed upper limit on its cost. Liquid at rest costs nothing.
- **Performance log.** A setting writes where each frame's time goes to a file, with worst cases.

## 16. Testing tools

- **Scenario captures.** Scripts start the game in a known scene with known settings and save a picture.
- **Golden images.** Seven fixed scenes are compared against stored pictures to catch anything that changes the look by accident.
- **Scripted tests** for explosions, shadows, materials, surface states, lamps and breaking lamps.
- **Soak tests** run the game for a long time to find crashes and memory growth.

## 17. Fixes to the base game

- Loading saved games works again.
- The pie menu ring and the inventory carousel were invisible on the GPU renderer.
- Scenario markers on the planet map were missing.
- Black frames appeared when the window was uncovered.
- Copied presets with their own sprites drew the wrong frames.
- Settings listed after one particular line in `Settings.ini` were silently ignored.
- A crash in the modern HUD when the controlled unit was deleted.

---

## Built but not yet checked

These are built and compile, but nobody has looked at them in real play or in a capture.

- **Shimmer, tracer light and background blur:** not confirmed by eye.
- **Surface states:** soot from a real blast, a gun heating from real firing, and a struck plate glowing.
- **Scenery lamps:** saving and loading a game with lamps; the Browncoat pieces in a real scene; whether the build menus list the new "Bunker Lights" group.
- **The Graphics Lab ambient slider** and the linked unit light floor: not tried in the running game.
- **The Mod Manager's "FAILED TO LOAD" entry:** not opened to look at.
- **Linux and macOS builds:** the new files are in the build scripts, but only Windows has been built and run.

## Not built

- A newer graphics backend (Vulkan, Metal, DirectX 12).
- From the visuals plan: items 4, 5, 18, 19 and 20.
- Agreed ideas not started: electricity through water, freezing, mud, storms that roll in mid-battle, burn scars that regrow, Wildfire and Night Siege modes, stealth in shadow, footprints, instant replay, and sandbox extras (saved setups, unit inspector, kill statistics, spawner zones).
