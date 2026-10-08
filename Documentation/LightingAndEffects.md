# Lighting and Visual Effects

This branch adds scene lighting and post-processing on top of the GPU renderer. Everything works automatically with existing content. These are the knobs for players, modders and activity scripters.

## How it works, briefly

Each player screen is drawn as before. Before the HUD goes on top, it is then:

1. **Lit.** Light comes from four sources:
   - **Sky light.** It spreads through a low-resolution copy of the terrain, so caves get darker the deeper they go, and digging lets light in.
   - **Ambient light.** It applies where no sky light reaches.
   - **Glow lights.** Every glow effect (`ScreenEffect`) casts light in its own colour, with soft shadows from terrain.
   - **Lights you add.** These come from INI or Lua (see below).

   All of it is computed in HDR. At full daylight the original art is reproduced unchanged.
2. **Edge shading.** Sprites and terrain get automatic edge normals from their outlines, so edges facing a light catch it.
3. **Emissive pixels.** Glows, the palette's glow yellows (gold sparkle, tracers) and freshly blasted terrain shine regardless of the surrounding light.
4. **Post-processing**, in this order:
   - Heat haze and explosion shockwaves
   - God rays into caves
   - Rain or snow
   - Bloom
   - Highlight compression, colour grading and vignette

## Settings (Settings.ini)

All colours are linear `R G B` (1 = neutral). Most of these can be tuned live in the **settings panel** (**F6**, in a game or in the menus; `DebugMan:ShowGraphicsLab()` from Lua opens it too). Where this document says "Graphics Lab" or "World Debug" it means that panel: the two were merged into it, sorted into categories with a search box. *Keep for next time* writes the settings to Settings.ini, and any setup can be saved as a named preset (a file in `Userdata\Presets`). The in-game *Video* settings have toggles for Lighting, Bloom and Extra Effects (heat haze, shockwaves, scorch marks, embers). Turning all three off gives the classic look.

The panel's categories are: Time & weather, Sky & daylight, Interiors & shadows, Lamps & lights, Surfaces, Water, Fire, smoke & blast, Falling ground, Camera & image, Game & HUD, and Debug (debug views, performance stats, the layout of the tool windows). **Tab** in a game puts every tool window away or brings them back. While any tool window is open the mouse is released from aiming so the window can be used, and clicks on a window don't reach the game.

**Effects particles** (`EffectsParticles`, a multiplier where 0 turns them off) add sparks, dust and debris chips from explosions and from fast hits on terrain. Hard materials (integrity 100 or more) throw sparks; soft ones throw dust. They are purely visual: the simulation spawns them, but they move in simulation time with their own random numbers and only read the terrain. Sparks glow (emissive), chips are lit in the scene, and dust is lit over the scene. They're part of Extra Effects and the quality presets.

**Smoke scattering** (`SmokeScattering`, where 0 turns it off): smoke particles, meaning Air-material MOSParticles with negative gravity, are splatted into a half-resolution density buffer. The light passing through them, from dynamic lights and radiance cascades, is added where the smoke is, so fire, muzzle flashes and lamps glow through smoke. The smoke sprites themselves are unchanged.

**Living world** (`LivingWorld`):
- Vegetation in the terrain (green-dominant palette colours) sways with the wind and gusts, more towards the tips, and is pushed outwards as blast waves pass.
- Snow builds up a few pixels deep on ground under open sky while it snows, over about a minute, and melts afterwards.
- Rain darkens exposed surfaces and they dry slowly.

All of it happens in the terrain shader; the terrain bitmaps are never changed.

**Radiance cascades GI** (`RadianceCascades = 1`, which the Ultra preset turns on) is 2D global illumination at half resolution. Glows such as fire, explosions and lamps light their surroundings with soft occlusion from terrain and objects. Lit surfaces pass on part of their light (`GIBounce`), building up to several bounces over a few frames. It replaces the screen-space indirect light, costs about 0.3 ms, and `GIStrength` scales it. The Graphics Lab has a "GI only" view.

At night, stars and a moon appear on the sky layers. These are the background layers with little or no parallax, and both fade out toward the horizon. Heavy rain (intensity above 0.5) brings occasional lightning that briefly lights the sky. Auto exposure (`AutoExposure`, `AutoExposureLow`, `AutoExposureHigh`) only kicks in when the average scene brightness leaves the normal range, such as a screen-filling flash or near-total darkness.

**Shadows** (three settings, each a strength from 0 to 1 where 0 turns it off; sliders in the Graphics Lab; the quality presets set them):
- **Shadows of units and objects** (`UnitShadows`, 0.85, Medium and up): soldiers, devices, doors, crates, craft and wreckage block light. A muzzle flash, flare, lamp or explosion throws their shadows across the walls and the ground, sharp at the feet and softer further away. Particles, smoke and flashes cast none, and a light isn't blocked by whatever is carrying it.
- **Sun and moon shadows** (`SunShadows`, 0.55, Low and up): daylight has a direction, which turns with the time of day; at night it is the moon's, fainter.
  - Where the sun can't be seen (the far side of a hill, under an overhang, in the shadow of a unit), ground, walls and units are dimmer and cooler. In full sun under open sky nothing changes.
  - Where the sun gets into a cave or a bunker through an opening, it lights the walls it falls on, and the beam shows in the air (the `GodRays` setting).
  - Shadows fade out around sunrise and sunset, when the sun and moon swap, and under rain, snow, ash and dust.
- **The sun and clouds:** by day the sun shows in the sky (`SunDisc`, 1), following the same arc the moon does at night and turning warm towards the horizon; weather hides it. Clouds drift across it with the wind and their shadows cross the ground, units and walls under open sky (`CloudShadows`, 0.5; needs sun shadows on).
- **Contact shading** (`ContactShading`, 0.4, Medium and up): background walls darken slightly right next to units and objects and next to solid ground, so things look anchored to the scene.
- An object can opt out with `CastsShadow = 0` in its INI (any `MOSRotating`: energy shields, holograms), or `object.CastsShadow = false` from Lua. Anything drawn see-through or with an additive or screen blend mode never casts.
- The Graphics Lab's views include "Solid objects and distance to them" and "Where the sun is visible", for checking what casts and what is lit.

**Materials** (what things look like they're made of; sliders in the Graphics Lab):
- **Metal and gloss.** Every object and every terrain material has two looks: how metallic it is and how glossy.
  - Metal mirrors its surroundings: brighter where it leans up towards the sky, darker where it leans down, with highlights in its own colour. Glossy things take tight, bright highlights from lights and glint in the sun.
  - Metallic and glossy objects are also rounded off, so a limb or a barrel shades like a tube, not a flat cut-out.
  - `LightingMetals` (1) scales the mirroring, rounding and sun glints; 0 turns them off. `LightingSpecular` scales highlights.
- **Where the looks come from.** An object uses its own `Metalness` and `Gloss` if its INI sets them, else those of the unit or object it's part of, else those of its physical material. Held devices (guns, tools, shields) count as steel unless they say otherwise.
  - Robots, drones, turrets and craft in the stock modules are marked in their INI. Dummies are glossy plastic.
  - A `Material` can set `Metalness` and `Gloss` too. Without them it gets values that suit its name (the metals, gold, glass, ice, concrete and so on).
  - Solid terrain looks like what it's made of: steel plating is metal, concrete has a dull sheen, earth has none.
- **Relief** (`LightingRelief`, 0.6): the lighting reads a sprite's or the terrain's own shading as relief, so painted plates, rivets and folds catch the light. 0 leaves only the outline bevel.

```ini
AddActor = AHuman
	PresetName = My Chrome Robot
	Metalness = 1      // 0 to 1. Its parts take this unless they set their own.
	Gloss = 0.9        // 0 to 1.
```

From Lua: `object.Metalness = 0.8`, `object.Gloss = 0.5` (below 0 means "not set").

**Surface states** (`SurfaceStates`, on; a tick box in the Graphics Lab): units show what has happened to them. Looks only, nothing in the simulation reads them.
- **Wet:** darker and glossy. At once when wading; building up in rain under open sky; dry again in about twenty seconds.
- **Sooty:** blackened and dull. From explosions nearby (more the closer) and from burning. Wears off over a couple of minutes, sooner in rain, and washes off in water.
- **Snow:** white on the head, shoulders and whatever is held up there. Settles on a unit standing still in snowfall under open sky, over about twenty seconds of heavy snow; moving shakes it off.
- **Heat:** glowing from dull red to orange, and it shows in the dark. A gun warms with every shot, so sustained fire makes it glow; metal glows for a moment where it's hit; burning units glow. Cools in a few seconds.
- A unit's parts share its wetness, soot and snow. Heat is each part's own.

From Lua, each 0 to 1 on any object: `actor.Wetness`, `actor.Soot`, `actor.SnowCover`, `gun.Heat`. Units' wetness, soot and snow are set by the game every update, so a script that wants to hold one has to keep setting it.

**Explosions, energy and looks:**
- **Explosions** throw balls of fire that swell, roll upwards and burn out into dark smoke, which hangs and drifts for a few seconds; on the ground, a ring of dust races out either side. Visual only, part of `EffectsParticles`.
- **Tracers** (`TracerLights`, on): fast shots with a trail throw a little light in their trail's colour on what they pass. At most 40 a frame.
- **Shimmer:** any object can bend the scene around it in a wobbling ring, for energy shields, cloaks and hot things: `Shimmer = 1` in INI, `object.Shimmer = 1` from Lua, or `PostProcessMan:AddShimmer(pos, radius, strength)` each update. Needs Distortion on.
- **Depth** (`BackgroundBlur`, 0.6): the far background layers are softened a little, more the further away they are.
- **Looks:** ready-made grades in the Graphics Lab and Photo Mode (F8): Natural, Gritty, Vivid, Noir. They set saturation, contrast, tints, vignette, grain and bloom, which can then be adjusted as before. Lua: `PostProcessMan:ApplyLook(0..3)`.

**Spreading fire** (`TerrainFire` in the gameplay settings, also in F6):
- **What lights it:** explosions and fire, flame and napalm particles light flammable terrain.
- **How it burns:** grass and vegetation flare up and burn away; wood, cloth and rubber burn slowly from the surface in, leaving ash; oil burns fast.
- **How it spreads:** fire needs air, so only exposed pixels catch, and it climbs upward more readily than sideways.
- **Weather:** rain (and less so snow) makes it spread less and burn out sooner. Wind makes it spread downwind, and in dry weather carries embers a few pixels downwind to start new fires.
- **Water** splashing on it puts it out.
- **What it gives off:** burning areas give light, smoke, embers and flames that hurt whoever stands in them.
- **Determinism:** it is part of the simulation and stays deterministic. It ticks in fixed sim steps with its own seeded random numbers, and ignitions are sorted before they're applied.
- **Saves:** burning terrain is saved in saved games and keeps burning when loaded.

**Collapsing terrain** (`TerrainCollapse`, a gameplay setting, also in F6):
- **When it checks:** half a second and again a second and a half after a big explosion, it looks around the crater for pieces of terrain no longer touching anything.
- **What falls:** what a blast or digging cuts loose. The system notes how things were before the crater is dug and compares afterwards.
  - A piece cut from the world falls.
  - A mass that was already hanging in the air (a floating island, a bunker with nothing under it) stays when it is chipped. Cut in two, the bigger part stays and the smaller falls.
  - A piece left joined to the rest by a neck of no more than 3 pixels snaps off. One that hung by such a neck before the blast is left.
  - A piece of more than 30,000 pixels counts as the world and stays.
- **Tuning** (F6, "What falls, and how"; saved as `CollapseFloatingStays`, `CollapseNeckWidth`, `CollapseMaxPiece`, `CollapseMinFitting`, `CollapseBreakStrength`, `CollapseRestSeconds`): whether floating masses stay, the neck width that snaps (0 for off), the biggest piece that can fall, the smallest loose bit of a building that falls, how hard a landing cracks a piece, and how long a piece lies still before it is ground again.
- **Scraps in the way** (`CollapseCrushPixels`, 24; in the same tuning list): a falling piece goes through loose bits of ground of up to that many pixels (and never more than a quarter of its own size) and flattens them, so a slab of bunker isn't held up by two leftover pixels of wall. 0 turns it off.
- **Explosions** throw loose pieces near them: ones still moving, and ones that came to rest within the last minute, which are lifted out of the ground again. Heavier pieces are thrown less.
- **Landing:** a puff of dust and a few chips in the piece's own colour. No fire or light.
- **How it falls:** as a rigid body. Its mass and how hard it is to spin come from its materials. It is tested against the ground along its outline, moving no more than a pixel at a time, and where it touches it gets a push with a little bounce and friction (the method rigid body engines such as Box2D use). So a piece tips off a ledge, rolls down a slope and slides to rest. After each push it is lifted clear of the surface (position correction), which is what lets it roll on a corner instead of jamming.
- **Coming to rest:** a piece becomes ordinary ground again only after lying still for two and a half seconds. Until then it can still tip, roll or be knocked on.
- **Breaking:** a piece that lands faster than its material can take cracks along jagged lines into smaller pieces, which fall on their own; the tiniest bits fly off as loose particles. Sand and soil break easily, stone needs a long fall, concrete a longer one, and metal doesn't break.
- **Buildings** (`CollapseBuildings`, on; also in F6): pieces of concrete and metal fall too once nothing holds them. Off, anything with building material in it stays up, and holds up the ground joined to it, so only natural ground falls.
- **Damage while falling:** when pixels are shot, dug or blasted off a piece, what it collides with, its weight and its balance are worked out again from what's left, and if it has come apart each part falls on its own.
- **Building pieces:** A loose bit that is mostly building material and smaller than 150 pixels stays (lamps, signs and consoles are drawn hanging in mid-air), and so does anything joined to a door or the edge of the world.
- **While it falls** a piece is part of the terrain, so units collide with it and can stand on it. It hits and pushes units in its way, loses the pixels that are shot or blasted off it, sinks slowly in liquid, and lifts liquid out of its way instead of deleting it.
- **Not kept:** a saved game stores a falling piece as ground where it was.
- **Cost:** about 0.1 ms an update in a bombing run on a bunker, 4 ms at worst (Debug Release build).
- **Determinism:** it is part of the simulation and deterministic, with sorted checks and its own seeded random numbers.
- **Lua:** `SceneMan:SpawnTerrainChunk(Vector, radius, "Stone")` makes a boulder of any material; `SceneMan:CheckTerrainCollapse(Vector, radius)` runs the check; `SceneMan:GetFallingTerrainChunkCount()`.

**Flowing liquids** (`FlowingLiquids`, a gameplay setting, also in F6):
- **What flows:** Water (material 160), Lava (165), Acid (167) and Oil in the terrain fall, run sideways and pool.
- **Drops join in:** a liquid particle that settles into the terrain flows too, if it's drawn in its liquid's own colour (so blood, which uses the Water material in red, stays put). A burning particle that settles as something flammable, such as napalm fuel, sets it alight.
- **Water** puts out fire. It is drawn translucent, deepening in colour with depth, with slow ripples of light and a bright line where it meets the air.
- **Lava** flows slowly, glows, sets flammable terrain alight, hurts whatever touches its surface, and sets to stone in a puff of steam where it meets water.
- **Acid** slowly eats soft terrain and is used up doing it.
- **Oil** flows more slowly and burns where it pools. It isn't drawn shimmering, because its colour is shared with many sprites.
- **Saves:** moving liquid is saved in saved games.
- **How it moves:** each moving pixel has a speed. Falling, it gets faster and drifts the way it was going, so liquid pours in an arc. Speed left over from a fall or a slide carries it along the level, slowing; at a wall it turns round with half its speed, so a wave sloshes back.
- **Coming level:** a pixel at the surface that can't get lower looks along its own row, up to 720 pixels for water, for a place to drop into, and goes there. The look passes through liquid as well as air, so a body of liquid presses outwards: a heap on a floor is flat within a second or two. A pixel that slips off an edge slides down the face of the liquid in the same step. A pixel with nowhere lower to go stays put, so pools come fully to rest.
- **Under pressure:** liquid with more of it pressing down from above goes out through an opening along its row within 48 pixels (a hole in a tank's wall, the mouth of a pipe), and liquid running under a ceiling or along a pipe looks ahead the same way as liquid at an open surface. A falling stream frays a little at its edges.
- **Pouring water** (Graphics Lab, "Pouring water"; all visual only):
  - `WaterFoam` (1): thin, broken water (a stream off a ledge, the lip of a pour) is drawn as froth, and the air around it is drawn as part of it, blue close in and white further out, so a few pixels read as a body of water. Each falling drop has a short tail.
  - `WaterFoamStray` (0.25): how much of that a pixel or two thrown clear of the rest gets. 0 leaves stray drops as single pixels.
  - `WaterFoamBrightness` (1) and `WaterFoamGlow` (0.45): how bright froth is, and how much light of its own it carries so it shows at night.
  - `WaterMist` (0.4): soft puffs of spray thrown off water that is falling fast or landing. `WaterMistBrightness` (1) and `WaterMistGlow` (0.4) are its brightness and its least light at night.
  - No two pixels of a pour fall quite alike (one sometimes takes a step fewer; sideways steps are left to chance), so a pour off a ledge is a shifting cascade and not a fixed pattern of dots.
- **Water's look:** thin bright lines of light wander and cross through it, stronger in the depths; pixels near the surface flash white now and then; and it is glossy, so lamps, fires and the sun glance off it.
- **Layers:** heavier liquid sinks through lighter. From the top: oil, water, acid, lava.
- **Splashes:** liquid that lands fast throws drops. An explosion in or beside liquid throws it into the air, and so does a unit or a boulder falling in. Every pixel thrown is a drop that flies and joins the liquid again where it lands; a drop that comes down inside liquid rises to its surface, so none is lost.
- **Ice** (`WaterFreezes`, off; also in F6): when on, still water slowly freezes over from the top in snowy weather. Lava melts ice and snow to water, and a falling piece of terrain breaks through ice into the water under it.
- **Loose powders** (`LoosePowders`, a gameplay setting, also in F6): Sand, Snow, Earth Rubble and Ashes fall and slide down slopes when disturbed (by a blast, digging, a landing boulder or flowing liquid beside them), and pile at a slope instead of levelling. Snow stands steeper than sand. Powder sinks in liquid. Ground that nothing disturbs stays as the map made it.
- **Finding its level:** liquid that has nowhere left to run looks through the body it belongs to for a lower free spot and moves there. Connected pools come to one level, including through a tunnel or under a wall.
- **Nothing gets left hanging:** liquid wakes when ground next to it is dug, shot or blasted away, and a sweep of the whole map every few seconds catches anything missed. Any amount can be poured; beyond 80,000 moving pixels the rest wait their turn.
- **Cost:** liquid at rest costs nothing. Measured: about 0.7 ms per update with 6,000 pixels moving (Debug Release build), and 6 to 8 ms, with spikes to 11 ms, with the most that can move at once, 80,000 (Final build). F6 shows the count and time.
- **Determinism:** it is part of the simulation and deterministic.
- **Lua:** `SceneMan:PourLiquid(Vector, radius, "Water"|"Lava"|"Acid"|"Oil"|"Sand"|"Snow"|"Earth Rubble"|"Ashes")`, `SceneMan:GetFlowingLiquidPixelCount()`, `SceneMan:GetBurningPixelCount()`.
- **Liquid weapons:** the **Napalm Flamer** sprays burning fuel that pools and burns, the **Water Cannon** knocks people back and puts fires out, and the **Acid Sprayer** lobs globs that sting and eat soft ground.

**Swimming and drowning** (`SwimmingAndDrowning`, a gameplay setting, also in F6):
- Bodies (units, limbs, weapons, wreckage) move through liquid instead of hitting and destroying it, so pools and floods stay put when units walk in. Single particles, such as drops and bullets, still strike the surface.
- **Wading** slows walking, more the deeper the unit is.
- **Buoyancy:** light units float (a lightly armed soldier, about 145 kg all in, just floats; robots like the Dummy bob high), and heavy armour and big guns sink.
- **Air:** flesh and blood units hold their breath for 12 seconds with their heads under, then lose health until they surface. Robots and drones don't breathe. The Modern HUD shows an air bar for the unit you control.
- **Acid** hurts anything standing in it. Lava sets units alight (see Burning units).
- Deterministic, with no random numbers.

**Smoke and gas** (`SmokeBlocksSight`, a gameplay setting, also in F6):
- Thick smoke blocks sight: units can't spot enemies through it, and the AI loses track of targets hidden by it. Thin wisps don't.
- It's worked out each sim update from the smoke particles on a coarse grid, so it is deterministic. Lua: `SceneMan:SmokeBlocksSight(from, to)`.
- **Smoke Grenade:** a thick screen lasting about ten seconds. The AI doesn't throw it.
- **Toxic Gas Grenade:** a green cloud that hurts anyone inside it for about ten seconds, and blocks sight too.

**Burning units** (`BurningUnits`, a gameplay setting, also in F6):
- **What burns:** units made of flesh. Robots, droids, drones, turrets, craft and brains in jars never catch fire. A unit counts as a machine if its body's material isn't a flesh one, or if its INI gives it `Metalness` of 0.2 or more.
- **What sets them alight:** burning fuel and flame-thrower flames (things named Napalm, Incendiary, Flamer, "Flame ... Hurt", "Burn Particle", "Ground Flame"), burning ground and lava. Not explosions' fire puffs, jetpack flames, smoke, muzzle flashes, bullets or lasers.
- **How easily:** a lick of flame doesn't always take. About a second in a flame-thrower's stream or standing in burning ground does it.
- Flames, napalm, burning ground and lava set units alight. A burning unit takes damage, panics and runs, and sets grass and anyone it bumps into alight.
- It burns out after a few seconds. Water puts it out sooner (a pool, the Water Cannon or heavy rain).
- Water putting out fire, and lava meeting water, throws up **steam** that rises and blocks sight like smoke. Only real water drops douse fire: blood is made of water too, but doesn't.
- **Fuel Barrel:** leaks oil that flows where it's shot, and explodes in burning fuel when fire reaches it or it's shot to pieces. It's in the build menu and the item list. Lua: `SceneMan:IsBurningNear(pos, radius)`, `SceneMan:GetBurningUnitCount()`.

**Weather** also changes gameplay: rain and snow damp fire (see above), snow slows walking by up to 15%, and wind drives fire downwind. The weather stays fixed through a game. It comes from the scene, the scenario setup's **Time** and **Weather** buttons, or the player's settings.

**Light colors, lamps, headlamps and tracers** (all in the Graphics Lab):
- **All lights:** `LightSaturation` (1) is how colorful the light of lamps, glows, flashes and fire is: 0 makes it all white, 2 deepens the colors. `LightTint` (`1 1 1`) multiplies every one of them by a color.
- **Scenery lamps:** `LampBrightness` (1), `LampReach` (1) and `LampTint` (`1 1 1`) change the lamps of bunker pieces and placed fixtures together.
- **Headlamps:** `HeadlampBrightness` (1.4), `HeadlampReach` (210 px), `HeadlampWidth` (26 degrees, the beam's half-angle), `HeadlampColor` (linear, `1 0.875 0.687`), `HeadlampGlow` (0.35, the small glow around the lamp), `HeadlampTeamTint` (0 to 1: how much each side's lamps take its team color) and `HeadlampsByDay` (on in daylight too).
  - A unit can have its own: `HeadlampBrightness = 0` in its INI means it has no lamp, `2` a strong one; `HeadlampColor = Color` gives it a color of its own. Lua: `actor.HeadlampBrightness`, `actor:SetHeadlampColor(r, g, b)`, `actor:ClearHeadlampColor()`.
- **Dead of night:** `DeepNightDarkness` (0.5; "Darker in the dead of night" in the Graphics Lab). From nightfall the light on the scene (sky light, interior light and the least light on units and ground) falls away to its least at eleven, stays there until two and comes back by first light. 0.5 halves it, 0.9 leaves a tenth, 0 leaves night as bright as nightfall. Lamps, fires and headlamps are not dimmed, so they matter more.
- **Sky of the hour:** `SkyFollowsTime` (1; "Sky takes the hour's colours" in the Graphics Lab). The sky art is painted as a blue day, and darkening it only ever gave a dark blue day. Away from midday its colours are now replaced: a gradient from overhead to the horizon where the art is blue sky, and the hour's light on cloud where it is pale. Night is near black-blue with stars, dawn and dusk run from deep blue to orange and pink, and bad weather greys it at any hour. Around midday the art is shown as painted. Distant scenery and the sky are lit by the hour's own daylight, not by the `LightingSkyColor` setting, which now only sets how much light falls on the scene in front. 0 gives the old behaviour.
- **Aiming dots:** `AimDotsLight` (off; "Aiming dots light the scene" in F6). The dots that show where a weapon points always glow; on, they also cast light on what is around them.
- **Tracers:** `TracerGlow` (0.8) makes shots with a trail, and the trail, shine in their own color and bloom. `TracerLightBrightness` (0.55) and `TracerLightReach` (28 px) set the light they throw on what they pass.
- **Weather shelter:** rain, snow, ash and dust are hidden where the ground shelters them from the way they are falling, drop by drop: each drop's line is followed back upwind through the world's grid of solid ground. Wind-driven rain gets in under an overhang on the windward side and leaves a dry strip in the lee of a wall; with no wind it stops at the roof's edge.
- **Rain splashes:** `RainSplashes` (1; 0 to 2, 0 for none; "Rain splashes" in the Graphics Lab). Little splashes where rain lands on anything it can reach: ground, water, roofs and units. Visual only.
- **Weather:** `WeatherLight` (0.35) is the least light rain, snow, ash and dust are drawn with, so weather shows on a dark night and not only where a lamp catches it.

**Tool windows** (`DockPanels`, on; `PanelWidth`, 380): the Sandbox, the settings panel, Photo Mode and the other tool windows are panels at the sides of the window, and the game's picture is fitted into the space between them, so nothing covers it. The Sandbox docks left; the rest dock right, as tabs of one panel. The settings panel's Debug category has the switch to let them float again, their size and the panel width.

**Night gameplay**:
- **Headlamps** (`Headlamps`): after dark, soldiers wear headlamps that throw a cone of light where they aim, with a faint visible beam.
- **AI sight** (`NightAffectsAI`): AI sees only about half as far at night without a headlamp, or 80% with one. This changes gameplay.
- **Flare:** a buyable grenade that burns red for about 45 seconds.
- **Cone lights:** available to code via `PostProcessMan::RegisterConeLight`.
- **Lights near terrain:** a light no longer shadows itself on the terrain right around it, so a lamp lying in grass still lights up its surroundings.

**Sandbox** (game mode and F7 tools):
- **The game mode:** pick **Sandbox** on the main menu (or in the scenario menu, to choose the map). **Tab** switches between the tools, with the world paused, and your own character (the "You" tab sets it up, or switches it off). You play as a god with a free camera (right-drag or WASD), every unit is run by the AI, including your own side's, and the game never ends.
- **Spawning:** units from every faction (with squad size, loadout and orders), brains and items. Each spawn list has a search box. The default loadout uses each faction's guns and plain grenades.
- **Building:** "Build bunkers with the build menu" opens the game's own build menu in the middle of play. It places straight into the world, and the money never runs out. Choose Done in its pie menu, or press F7, to go back. There are four sides, in the game's own team colours: Red, Green, Blue and Yellow.
- **Brains are optional:** place one for any side to give the others something to hunt and that side something to defend.
- **Orders:**
  - Hold position, attack nearest enemy, hunt brains, patrol, go to the side's rally point, or do nothing.
  - Given when units spawn, or to a whole side from the Orders tab. "Everyone attack!" starts a free-for-all.
  - Units told to attack pick a new target when theirs dies.
- **Pause AI** (top of the sandbox window, also in F6): every AI-run unit stands still and holds fire, so you can set up armies, bunkers and traps, then untick it to let them loose. Physics, fire and liquids carry on. Craft keep flying, units you control still move, and a banner shows while it's paused. A new game starts with the AI running. Lua: `SandboxPauseAI(true/false)`.
- **Drop squad:** a squad arrives by dropship or rocket over the point you click, then the craft flies off.
- **Command and Follow:** with Command, drag a box to select units, then click the ground to send them there or an enemy to attack it. Follow locks the camera onto a unit; "Follow the action" (World tab) keeps the camera on the closest fighting. Moving the camera yourself stops following.
- **Auto battle (Orders tab):** give sides a faction and a budget. Each buys waves of its faction's units and drops them in to attack until one side is left, and the winner is announced. Lua: `SandboxAutoBattleSide(side, faction, budget)`, `SandboxStartAutoBattle()`.
- **Take control:** click a unit to play it yourself. F7 (or the unit dying) puts you back in the god view.
- **Paint and Boom:** fire, water, lava, acid, oil, smoke and toxic gas; dig, or add earth, sand, grass, wood or concrete; grenade blasts, big bombs, napalm and lightning strikes.
- **World tab:** weather, wind, time of day, slow motion, a free camera toggle, and putting out all fire.
- **In other games:** F7 opens the same tools as a debug panel.
- **Lua:**
  - `SandboxDo(tool, position, side, order, count, presetName)` uses any tool from a script. For example, `SandboxDo("Units", pos, 1, 1, 4, "Soldier Light")` spawns four Red soldiers told to attack.
  - `SandboxCountUnits(side)` counts a side's units, and `SandboxBuildMode(true/false)` opens or closes the build menu.

**Modern HUD** (`ModernHUD`, in Video settings and F6) is drawn crisply at window resolution on top of the classic HUD. It is off by default. It adds:
- A minimap of the terrain, your view and every unit, coloured by team.
- Health and ammo bars for the unit you control.
- A feed of units lost.

**Feel:**
- **Hit-stop** (`HitStopStrength`, 1 by default, 0 for off, also in F6): a big blast in view holds the game for 25 to 70 milliseconds and smears the lens for an instant. It happens at most every 0.6 seconds, so chains of explosions don't stutter. Only the timing changes, never what the simulation does.
- **Recoil kick:** the view kicks back with the gun when the unit you control fires, and springs back. It follows the Screen Shake Strength setting.
- **Frame cap** (`FrameCap`, 0 for none, also in F6): limits frames per second (30 to 1000), to save power and heat when VSync is off.
- **CRT scanlines** (`PostScanlines`, 0 to 1, in the Graphics Lab): optional scanlines on the final image.
- **Modern HUD extras:** numbers float up from units as they lose health (slow damage like fire is gathered up), and a red arc shows which way a hit on your unit came from.

**Camera zoom:**
- **Ctrl + mouse wheel** zooms the view out (down to 0.4x, but never so far that the view is bigger than the map) or in (up to 2x). In the Sandbox god view the wheel alone zooms. There are sliders in F6 and the sandbox's World tab.
- The whole view (scene, lighting and HUD) is drawn at the size of the area it shows and scaled to the screen, so every effect works at any zoom. Far backgrounds don't zoom: the sky and backdrop stay put.
- HUD text stays readable when zoomed out (with Smooth HUD Text on); HUD icons shrink with the view.
- The view eases back to 1x while the buy menu or the build phase is open, because those menus are laid out for the normal screen. Menus and editors don't zoom. A new game starts at 1x.
- Split screens zoom together. Zooming far out costs GPU time in proportion to the area shown.
- Lua: `FrameMan.CameraZoom` (read and write). `FrameMan.PlayerScreenWidth` and `Height` give the size of the area in view, so they change with the zoom.

Press **F8** for **Photo Mode**:
- Freezes time and hides the HUD and screen text.
- Free camera: drag with the right mouse button, or use the arrow keys.
- Look sliders: time of day, weather, exposure, grading, bloom, haze, god rays, film grain, chromatic aberration.
- Screenshots go to the ScreenShots folder, as shown in the window or at 2x, 3x or 4x the internal resolution.
- Look changes are undone when you close it, unless "Keep look changes" is ticked.
- F6 and F8 work even while time is frozen.

`LightingSettingsVersion` records which defaults a settings file was written with. When the lighting defaults change a lot (version 2 made interiors and caves much brighter), saved values from older files are ignored so players get the new look.

| Key | Default | What it does |
|---|---|---|
| `LightingEnabled` | 1 | Scene lighting on or off. |
| `LightingAmbient` | 0.13 0.13 0.16 | Light where no sky light reaches (cave backgrounds). |
| `LightingSkyColor` | 1 0.98 0.95 | Light under open sky at noon. |
| `LightingForegroundAmbient` | 0.4 0.39 0.42 | Minimum light on diggable terrain and objects, so the playfield stays readable underground. Dims at night. |
| `LightingAirFalloff` / `LightingSolidFalloff` | 0.96 / 0.6 | How far sky light reaches through air and into terrain. |
| `TimeOfDay` | 12 | Hours (0–24). Noon is the classic look; dawn, dusk and night tint the sky, haze and light. |
| `DayLengthMinutes` | 0 | Length of a full day/night cycle in minutes. 0 keeps the time fixed. |
| `AtmosphereHaze` / `AtmosphereColor` | 0.18 / 0.62 0.74 0.95 | Distant background layers fade into the atmosphere, based on their parallax. |
| `GodRays` | 0.7 | Light shafts in the air of caves and bunkers, where the sun (or moon) gets in through an opening. |
| `WeatherType` | 0 | 0 clear, 1 rain, 2 snow, 3 ash fall (grey flakes and a grey haze), 4 dust storm (dust blown level, a tan haze, and units see up to half as far). Precipitation doesn't fall under overhangs or in caves. |
| `WeatherIntensity` / `Wind` | 0.6 / 60 | How heavy the rain or snow is, and its horizontal speed in px/s. |
| `LightingGlowIntensity` / `LightingGlowRadiusScale` | 2.5 / 8 | Brightness and reach of the light that glow effects cast. |
| `LightingShadowStrength` | 0.85 | How much terrain blocks dynamic lights. |
| `UnitShadows` | 0.85 | How dark the shadows of units and objects are, from lights and from the sun. |
| `SunShadows` | 0.55 | Directional daylight: how much dimmer and cooler things are where the sun (or moon) can't be seen. |
| `ContactShading` | 0.4 | How much background walls darken next to objects and solid ground. |
| `SunDisc` | 1 | Brightness of the sun drawn in the sky by day. |
| `CloudShadows` | 0.5 | How much drifting clouds shade the ground under open sky. |
| `LightingEmissiveIntensity` | 1.4 | Brightness of glow sprites. |
| `LightingEdgeLighting` | 1 | Strength of the automatic edge normals. |
| `LightingSpecular` | 1 | Highlights that lights throw on glossy surfaces: metal, glass, rain-wet ground, water and acid. 0 turns them off. |
| `LightingMetals` | 1 | How strongly metal mirrors its surroundings, is rounded off, and glints in the sun. |
| `LightingRelief` | 0.6 | How much sprites' and terrain's own shading counts as relief. |
| `LightingIndirect` | 0.35 | One bounce of light: lit surfaces bleed their colour onto their surroundings, mostly in shadowed areas and caves. |
| `Embers` | 1 | Embers rising from fire and other warm glows. |
| `DistortionEnabled` / `HeatHaze` / `ShockwaveStrength` | 1 / 1.5 / 1 | Heat haze above hot things, and refraction rings from explosions. |
| `ScorchMarks` | 1 | Explosions leave soot on terrain, and the crater rim glows while it cools. |
| `BloomEnabled` / `BloomThreshold` / `BloomIntensity` | 1 / 0.9 / 0.5 | Bloom. |
| `PostExposure` / `PostSaturation` / `PostVignette` | 1 / 1.05 / 0.15 | Final image. |
| `GradeTemperature` / `GradeTint` / `GradeContrast` | 0 / 0 / 1 | White balance and contrast. |
| `GradeShadowTint` / `GradeHighlightTint` | 1 1 1 / 1 1 1 | Split toning. |
| `FilmGrain` / `ChromaticAberration` | 0 / 0 | Film grain (0–1) and lens fringing (pixels). |
| `ModShaders` / `ModShaderStrength` | 1 / 1 | Mods' own object shaders and post passes (see "Mod shaders" below). Off draws everything with the game's shaders. The strength (0–1) is handed to mods' shaders as `rteStrength`. |

## Scene atmosphere (INI)

A Scene can set its own time of day and weather. These override the player's settings while the Scene is loaded, and every Scene load restores the player's own atmosphere first.

```ini
AddScene = Scene
	PresetName = My Frozen Valley
	TimeOfDay = 17.5        // Hours. Dusk.
	DayLengthMinutes = 0    // 0 freezes the time.
	WeatherType = 2         // 0 clear, 1 rain, 2 snow, 3 ash fall, 4 dust storm
	WeatherIntensity = 0.7
	Wind = -90
	...
```

**From the scene editor:** set the time and weather you want in the settings panel (F6, Time & weather), press "Use what is set now" under "This scene's own time and weather", then save the scene. The keys above are written into it.

## Lights in the scenery (INI)

Bunker pieces and other `TerrainObject`s can bring lamps with them. When the piece is placed the lamps become part of the scene: they shine from then on, cast shadows like any light, and are saved with the game.

```ini
AddTerrainObject = TerrainObject
	PresetName = My Lit Corridor
	...
	AddLight = TerrainLight
		Offset = Vector        // From the top left corner of the piece's pictures, in pixels. Put it in the open, not inside something solid.
			X = 47.5
			Y = 25
		Color = Color
			R = 150
			G = 200
			B = 255
		Radius = 125           // Pixels, where the light reaches zero.
		Intensity = 1.4
		Flicker = 0            // 0..1 random flicker.
		Pulse = 0              // Times a second it swells and fades (warning lights). 0 is steady.
		ConeAngle = 0          // Above 0 it's a beam with this half-angle in degrees.
		ConeDirection = 90     // Which way the beam points, in degrees clockwise from pointing right. 90 is straight down.
```

- A piece can have any number of `AddLight` blocks. A copy (`CopyOf`) keeps its original's lights; `ClearLights = 1` drops them.
- **Lamps can be destroyed**, and go out for good in a few sparks. Three things do it:
  - A shot passing through the lamp (any fast, sharp projectile within about four pixels of it).
  - An explosion nearby: the bigger the blast, the further it reaches, from 20 to 75 pixels.
  - Losing what it hangs on. A lamp hangs on the nearest solid thing within five pixels of it (the pixel it's on, else above, beside, below), and goes out when that is dug, shot or blown away.
- **One lamp to a spot.** A lamp placed within ten pixels of another replaces it, and building a piece over a lamp removes the lamp. So a background piece painted with a lamp's glow can bring the lamp without doubling the one of the module it's put in.
- A `Scene`'s terrain can have lamps of its own: `AddLight = TerrainLight` inside the `Terrain = SLTerrain` block, with `Position` in scene coordinates.
- From Lua: `SceneMan:AddTerrainLight(pos, r, g, b, radius, intensity)` puts one up, `SceneMan:RemoveTerrainLights(pos, radius)` takes down those near a point.

**Stock pieces.** The lamps painted on the stock bunker pieces are real: the blue ceiling lamps of the concrete modules and tutorial pieces, the white and yellow strip and panel lights, lit windows and screens, the brain vault and rocket silo, vending machines, and the Browncoats' sodium and amber lamps. Brain cases, teleporters and consoles glow a little too.

**Placeable fixtures** (group `Bunker Lights`, in the build menus and the sandbox's Structure tool): Ceiling Lamp, Wall Lamp and Tiny Light in White, Warm, Blue, Red and Green; Floor Lamp and Strip Light in White and Warm; Floodlight Down, Left and Right (beams); Warning Beacon (pulses red); Flickering Lamp. They're drawn on the back wall, so they never block anything; the lamp's picture stays when it's smashed, only its light goes. `Tools/MakeBunkerLights.py` draws them and writes their INI.

How much the lamps matter depends on how bright interiors are without them: `LightingAmbient` (0.6 by default) and `LightingForegroundAmbient` (0.5) light every interior evenly. Turn them down (the F6 window's interior light) and bunkers are lit by their lamps, and go dark where the lamps are shot out.

## Lights on objects (INI)

Any `MovableObject` (actors, devices, particles, gibs…) can cast a light. The light moves, rotates and flips with the object, and terrain casts soft shadows from it.

```ini
AddDevice = HDFirearm
	PresetName = Flashlight Rifle
	...
	LightColor = Color
		R = 255
		G = 235
		B = 200
	LightRadius = 180      // Pixels, where the light reaches zero. 0 = no light.
	LightIntensity = 1.2   // 0 = no light.
	LightFlicker = 0.1     // 0..1 random flicker, nice for fire and torches.
	LightConeAngle = 25    // Above 0, the light is a beam (a flashlight) with this half-angle in degrees. 0 shines all round.
	LightConeDirection = 0 // Which way the beam points, in degrees clockwise from the way the object faces.
	LightOffset = Vector   // Offset from the object's position, rotated and flipped with it.
		X = 10
		Y = -2
```

Any `MovableObject` can give off purely visual particles as it goes (they never affect the simulation, and thousands cost next to nothing):

```ini
	VisualEmission = Sparks        // Sparks, Dust, Debris or Embers
	VisualEmissionRate = 40        // Per second.
	VisualEmissionSpread = 0.6     // 0 (straight along the object's velocity) to 1 (every direction).
```

From Lua: `object:SetVisualEmission("Embers", 20, 0.5)`, or a one-off burst anywhere with `EmitVisualParticles(kind, position, velocity, spread, count, 0xRRGGBB)` (0 for the kind's own colour).

Any `MovableObject` can also be drawn translucent or glowing:

```ini
	RenderBlendMode = Additive   // Normal (default), Additive (energy, plasma, fire), or Screen (soft light)
	RenderOpacity = 0.6          // 0..1
```

Glow effects (`ScreenEffect`) already cast light automatically, in the average colour of their glow image. You don't need to add lights for muzzle flashes, explosions, thrusters or fire.

## Lua

```lua
-- Lights on any MovableObject:
actor.LightRadius = 140
actor.LightIntensity = 0.9
actor:SetLightColor(255, 230, 190)   -- 0-255
actor.LightOffset = Vector(0, -12)
actor.LightFlicker = 0.0
actor.LightConeAngle = 25      -- a beam; 0 for all round
actor.LightConeDirection = 0
shield.RenderBlendMode = 1   -- 0 normal, 1 additive, 2 screen
shield.RenderOpacity = 0.5
print(actor.LightRed, actor.LightGreen, actor.LightBlue)

-- A light for this frame only. Call every update to keep it on:
PostProcessMan:AddLight(position, radius, r, g, b, intensity)

-- Atmosphere for scripted scenes:
PostProcessMan.TimeOfDay = 6.8          -- dawn
PostProcessMan.DayLengthMinutes = 20    -- 0 to freeze the time
PostProcessMan.WeatherType = 1          -- 0 clear, 1 rain, 2 snow, 3 ash fall, 4 dust storm
PostProcessMan.WeatherIntensity = 0.9
PostProcessMan.Wind = -120
PostProcessMan.LightingEnabled = true
PostProcessMan:SetSkyColor(255, 250, 240)     -- 0-255, gamma space
PostProcessMan:SetAmbientColor(90, 90, 110)
PostProcessMan:SetColorGrade(-0.4, 0.0, 1.1)  -- temperature, tint, contrast
PostProcessMan:SetSplitToning(0.85, 0.95, 1.15, 1.1, 1.0, 0.9)  -- shadow rgb, highlight rgb
```

Atmosphere changed from Lua (time, weather, sky and ambient colours, grade) lasts until the next Scene loads. It is never saved over the player's own settings.

## Shaders

The lighting shaders live in `Data/Base.rte/Shaders/Lighting/`. The sprite shader `Base.rte/Shaders/Blit8.frag` and the terrain shader `Base.rte/Shaders/Terrain.frag` write two more outputs. Custom shaders used for scene drawing should write them too.
- **Location 1, normals:** the screen-space normal's x and y in RG, 1 minus the surface's shininess in B, and in alpha 0 for "nothing drawn" or 0.5–1 for "drawn, with emissive strength 0–1".
- **Location 2, surface:** how metallic in R, how glossy in G, and in B 1 for solid objects that cast shadows (0 for terrain, particles and effects). Sprites get these per vertex (`rteVertexSurface`), set by the object being drawn.

## Mod shaders

A mod can draw its objects with its own shader, and give a scene or an activity its own pass over the whole screen. The player can turn both off ("Mod shaders" in the settings panel's Camera and image page). A shader that fails to compile or link is reported in the console and left unused; the mod still loads.

Declare the shader in the mod's own INI, before anything that uses it:

```ini
AddShader = Shader
	PresetName = My Hologram
	VertexShader = MyMod.rte/Shaders/Hologram.vert
	FragmentShader = MyMod.rte/Shaders/Hologram.frag
```

### On objects

```ini
AddActor = AHuman
	PresetName = Ghost Trooper
	Shader = My Hologram    // None, or leave it out, for the game's own
	...
```

From Lua: `actor.Shader = "My Hologram"` (an empty string goes back to the game's shader). A part with no shader of its own (an arm, a held gun) is drawn with its unit's or object's, unless it is drawn added or screened on top (`RenderBlendMode` 1 or 2).

An object shader takes everything `Blit8.vert` and `Blit8.frag` take (start from copies of them), and must write the same three outputs as the sprite shader (location 0 colour, 1 normals, 2 surface, as above), or the lighting reads garbage where the object is. On top of those it is given:

| Uniform | What it is |
|---|---|
| `float rteTime` | Seconds of effect time. It holds while the game is paused. |
| `float rteObjectSeed` | 0 to 1, the same for the unit every frame and different between units. |
| `float rteHealth` | The unit's health, 0 to 1. 1 for anything that isn't a unit. |
| `float rteStrength` | The player's "Mod shader strength", 0 to 1. Fade the effect by it. |
| `float rteRelief` | How much sprites' own shading counts as relief, as `Blit8.frag` uses it. |

The palette (`rtePalette`, unit 0), the drawn texture (`rteTexture`, unit 1) and the glow palette (`rteEmissivePalette`, unit 2) are bound as for the game's sprites, and `rteIndexed` says whether the texture holds palette indices.

### On the screen

```ini
AddScene = Scene
	PresetName = My Sandstorm Valley
	PostShader = My Sandstorm Filter
	...

AddActivity = GAScripted
	PresetName = Scanner Mission
	PostShader = My Scanner Overlay
	...
```

From Lua: `PostProcessMan:SetPostShader("My Scanner Overlay")`, or `""` to go back to the activity's or scene's own. A script's choice lasts until the next activity starts; otherwise the activity's wins over the scene's.

The pass runs on the lit scene in HDR, after the lighting and before exposure, bloom and tonemapping. Use `Base.rte/Shaders/Lighting/Fullscreen.vert` as its vertex shader (it hands over `textureUV`, 0 to 1 from the bottom left). It writes one output, the new scene colour, and is given:

| Uniform | What it is |
|---|---|
| `sampler2D rteScene` | The lit scene to redraw (unit 0). Keep its alpha. |
| `sampler2D rteSceneDepth` | The scene's depth (unit 1). |
| `float rteForegroundDepth` | Depths below this are the terrain and what is in front of it; above it, the backgrounds and sky. |
| `vec2 rteScreenSize` | The player screen's size in pixels. |
| `vec2 rteScreenOrigin` | The scene position of its top left corner, for effects fixed to the world. |
| `float rteTime` / `float rteStrength` | As for object shaders. |

```glsl
// A minimal scanner overlay.
#version 330 core
in vec2 textureUV;
out vec4 FragColor;
uniform sampler2D rteScene;
uniform vec2 rteScreenSize;
uniform float rteTime;
uniform float rteStrength;

void main() {
	vec4 scene = texture(rteScene, textureUV);
	float line = 0.5 + 0.5 * sin(textureUV.y * rteScreenSize.y * 0.5 - rteTime * 6.0);
	vec3 tinted = vec3(dot(scene.rgb, vec3(0.3, 0.59, 0.11))) * vec3(0.4, 1.0, 0.6) * (0.85 + 0.15 * line);
	FragColor = vec4(mix(scene.rgb, tinted, rteStrength), scene.a);
}
```
