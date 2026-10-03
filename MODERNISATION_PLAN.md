# Cortex Command: Visual Modernisation Plan

*Drafted 2026-10-03 against `development` @ `20dfb3ea5`.*

---

## 1. The goal

Keep what makes Cortex Command look like Cortex Command:
- crisp pixel art
- per-pixel destructible terrain
- chunky gibs
- the hand-drawn factions

Then light it like a modern game.

The target look is **"a pixel-art diorama under real light"**:
- dark caves that are actually dark
- muzzle flashes that light up the tunnel walls
- explosions that bloom, glow, and push heat haze outward
- sunlight that falls off as you dig down
- smoke that catches light
- soft fog of war

Reference points:
- *Noita* (lit falling-sand world)
- *Dead Cells* / *Blasphemous* (lit pixel art, normal-mapped sprites)
- *Teardown* (destruction + light)
- radiance-cascade 2D GI demos

Two principles:
1. **The pixels stay.** World albedo is still rendered at internal resolution and upscaled crisply. Lighting, bloom and post-processing are what get the "HD" treatment.
2. **Every existing asset gets better for free.** About 5,300 legacy sprites and 576 INI files cannot be redrawn. Everything new must work automatically on old content, with optional authored upgrades (normal maps, emissive maps, light definitions) layered on top.

---

## 2. Where the engine actually is today

The renderer is **halfway through a migration**, not a legacy Allegro relic:

| Layer | State |
|---|---|
| Window / context | SDL3 + OpenGL 3.3 core (GLAD) |
| GPU drawing | Vendored and modified **raylib rlgl** (`Source/Renderer/raylib`): one immediate-mode batch, depth-sorted via injected `rlZDepth` |
| Colour model | **8-bit palette end to end.** Sprites and layers are uploaded as `GL_RED`; `Background.frag` looks up a 256×1 palette texture |
| Scene layers | GPU-drawn from CPU bitmaps. **The whole visible region of every layer is re-uploaded every frame, with no dirty tracking** (`SceneLayer.cpp:427-469`) |
| Movable objects (MOs) | **CPU-rasterised with Allegro** (`pivot_scaled_sprite`) into a scene-sized 8-bit bitmap **during the sim tick** (`MovableMan.cpp:1689`), then uploaded |
| HUD / GUI / menus | Allegro on the CPU into 8-bit and 32-bit screen bitmaps, uploaded **in full** every frame |
| Lighting | None. Only "glows": screen-blended RGB PNG sprites in one post pass (`PostProcessMan.cpp:326-358`) |
| Post-processing | That one glow pass. No bloom, HDR, tone mapping, grading, or distortion |
| Timing | Fixed 60 Hz sim, **no interpolation**, single render thread |

What else exists:
- **Unmerged upstream work:**
  - `origin/gpu-renderer` (HeliumAnt, 134 commits, stalled since 2026-03-28): a proper C++ renderer with RenderMan, RenderBatch, GLState, Camera and DrawCall, plus GPU HUD and split-screen.
  - `origin/interpolated-render-redux` (Causeless, 2026-05-23): render interpolation decoupled from the sim tick.
  - Two `dynamic-fow` branches: GPU fog of war with visibility triangles.
- **Bugs found in passing:**
  - `GLResourceMan::UpdateDynamicBitmap` computes PBO offsets as sizes, not a running sum (`GLResourceMan.cpp:133-134`). Uploads of three or more regions corrupt each other.
  - The same function's row copy ignores `bytesPerPixel` in its x offset (`:130`). That is wrong for 32bpp.
  - Dot glows scan `BackBuffer8` (`PostProcessMan.cpp:389`), which no longer contains the scene, so they only fire on HUD pixels.
  - The `Background` shader preset is cloned every frame (`FrameMan.cpp:815`), and the palette texture is rebuilt every frame.
- **Project health:**
  - No stable release since v6.2.2 (Feb 2024).
  - Two core developers, working in bursts; nothing merged since 2026-05-28.
  - Not dead, but not going to block us either.

### Hard constraints any new renderer must respect

1. **Terrain material and FG/BG colour stay as CPU 8-bit bitmaps.** These are the source of truth for physics. The sim reads and writes them per pixel (about 160 call sites), in scenes up to 20000×8000. The GPU can only *mirror* them.
2. **Sprite frames stay available as CPU 8-bit data.** Hit tests, AtomGroup generation, silhouettes and terrain erase all use them.
3. **Palette indices carry meaning:**
   - Index 0 is transparent.
   - Material IDs are palette indices.
   - Debris takes the colour index of the terrain it came from.
   - Lua primitives take index colours.
   - `LoadPalette` / `FadeInPalette` must keep working.
4. **Deterministic fixed-step sim.** Rendering must never feed back into the sim.
5. **Wrapping scenes** (X/Y), **up to 4-way split-screen**, and a resolution multiplier with letterboxing.
6. **The modding contract:**
   - INI visual keys
   - the 12 `DrawBlendMode`s
   - `ScreenEffect` / `Effect*` keys
   - INI `AddShader` presets
   - the Lua primitive, `FrameMan` and `PostProcessMan` APIs
7. **All GL work stays on the main thread.** Sim tasks must be fenced before layers are read.

---

## 3. Strategy decisions (decide these first)

### 3.1 Build on `gpu-renderer`, don't start from scratch

HeliumAnt's branch already does the hardest plumbing:
- a real batch / draw-call abstraction
- GL state tracking
- a camera with wrap and clip
- the GPU HUD

It touches about 137 files in `Source/` (+3.4k / -0.7k lines) and is only 20 commits behind `development`.

**Recommendation:**
1. Rebase it onto `development`.
2. Get it building and visually matching current output.
3. Merge it into our working branch as Phase 0.
4. Then layer `interpolated-render-redux` on top.

This saves months and keeps us upstreamable.

### 3.2 Fork vs upstream

Work on our own long-lived branch (or fork), structured as upstreamable PRs:
- clang-format clean (CI uses clang-format-17)
- a CHANGELOG entry per change

Reach out on Discord to HeliumAnt and Causeless early. They own the branches we're building on, and alignment avoids a hard fork. If upstream stays quiet, we ship our own builds. Note the licensing caveat in §10.

### 3.3 Graphics API: stay on GL 3.3 now, abstract for SDL_GPU later

- OpenGL is deprecated on macOS.
- SDL3 (already vendored, 3.2.10) ships **SDL_GPU**, which targets Vulkan, Metal and D3D12.
- Porting now would stall everything.

**Recommendation:** everything new goes through the `gpu-renderer` abstraction (RenderMan / DrawCall / GLState), with **no raw `gl*` calls in feature code**. A later SDL_GPU backend then becomes a contained project (Phase 8).

### 3.4 The rendering model we're moving to

A small, explicit **render graph** of passes per view, replacing "everything into one batch, sorted by depth":

```
 Sim tick ──► Snapshot draw list (prev+curr transforms) ──┐
                                                          ▼
 ┌─────────────── per player view, internal res ────────────────────────┐
 │ 1. Albedo pass     palette-resolved colour: BG layers, terrain BG,    │
 │                    MOs (GPU sprite batch), terrain FG                 │
 │ 2. Normal/Material terrain edge-normals, sprite normals (auto/authored)│
 │    + Emissive      emissive sprites, glows, hot pixels, lasers        │
 │ 3. Occluder/SDF    from terrain material mirror (dirty-rect updated)  │
 │ 4. Lighting        sky light + point lights + emissive GI  (HDR 16F)  │
 │ 5. Composite       albedo × light + emissive, fog of war, atmosphere  │
 │ 6. FX              distortion (heat/shockwave), lit smoke/particles   │
 └───────────────────────────────────────────────────────────────────────┘
 7. Post (HDR)   bloom → exposure → tonemap → LUT grade → grain/vignette/CA
 8. Upscale      crisp pixel upscale to window (+ optional CRT)
 9. UI           native-resolution UI on top (not upscaled)
```

---

## 4. Phased roadmap

Sizes are rough for one dedicated developer: **S** is about 1–2 weeks, **M** is about 3–6 weeks, **L** is about 2–3 months. Each phase ends playable and shippable.

### Phase 0: Foundation and safety nets (M)

Nothing visual yet. It makes everything else safe.

- [ ] **Rebase and merge `gpu-renderer`** onto `development`, and fix whatever regressed.
- [ ] **Merge `interpolated-render-redux`.** Smooth motion above 60 Hz is the single cheapest "feels modern" upgrade.
- [ ] **Golden-image regression harness:**
  - deterministic seed
  - a scripted scene list, including wrap, split-screen and big scenes
  - screenshot after N ticks
  - perceptual diff against baselines
  - run in CI on the Windows build
- [ ] **Profiling:**
  - Tracy is already vendored (`tracy_*` meson options), so add GPU zones per pass
  - a RenderDoc capture workflow, documented
  - a frame-time HUD in the ImGui overlay (ImGui 1.91.9 is already in-tree)
- [ ] **Fix the bugs listed in §2:**
  - `UpdateDynamicBitmap` offsets and the bpp row offset
  - the shader clone every frame
  - the palette rebuilt every frame
  - dot glows reading the wrong buffer
- [ ] **ImGui "Graphics Lab" panel:** live-tweak every new parameter (lights, bloom, grading) and save to INI. This makes art direction iteration take minutes instead of rebuilds.

### Phase 1: Finish the GPU migration (L)

This is the prerequisite for lighting: everything must be on the GPU, drawn in the render phase.

- [ ] **MOs off the CPU:**
  - At `DrawnSimUpdate`, snapshot a draw list (sprite, frame, transform, flip, palette effects, previous transform for interpolation).
  - Render it as a **GPU sprite batch**.
  - Delete Allegro `pivot_scaled_sprite` from the visual path. Collision already rotates mathematically, so nothing breaks.
  - The existing `g_DrawTrans` GPU path is a template for this.
- [ ] **Sprite atlas:**
  - Pack all 8-bit sprite frames into R8 atlas pages at load time, via `GLResourceMan::GetStaticTextureFromBitmap`.
  - That turns about one draw call per sprite into a handful per frame.
- [ ] **MOPixels and trails as GPU points/quads**, not `putpixel` into a scene-sized bitmap. Retire the scene-sized MO colour bitmap; keep it only for world dumps and `Atom` trail reads, behind a flag.
- [ ] **Dirty-rect terrain mirroring:**
  - Terrain writes (`RegisterDrawing`, dislodge, crater, settle) mark tiles dirty.
  - Only dirty `BigTexture` tiles upload, through the existing PBO path.
  - This replaces the full visible-region re-upload, and is the biggest single performance win.
- [ ] **UI to the GPU:**
  - Implement the GUI library's `GUIScreen` / `GUIBitmap` interface over the renderer instead of Allegro (`Source/GUI/Wrappers`).
  - The pie menu, HUD and fonts become batched quads.
  - This kills both full-screen texture uploads per frame.
- [ ] **Explicit passes.** Introduce the pass structure from §3.4 with only an albedo pass, so output matches today pixel for pixel. Verify with the golden harness.

**Exit criterion:** pixel-identical (or approved-diff) output. CPU frame time is down substantially, and no Allegro drawing is left in the per-frame path.

### Phase 2: Lighting (L). The headline feature.

**2a. HDR light buffer and sky light (M)**
- Light accumulation in `RGBA16F`, composited as `albedo × light + emissive`.
- **Sky light:**
  - A per-scene sun/sky colour and direction.
  - Occluded by terrain, using a downward visibility pass over the material mirror. Use a coarse "skylight depth" texture updated from dirty tiles.
  - Surface is lit and caves fall to ambient. Digging a hole lets light in.
- **Ambient:** per scene, with an optional **day/night cycle**. New `Scene` INI keys `SkyColor`, `AmbientColor`, `SunAngle`, `TimeOfDay`, `DayLength`. This directly answers upstream issue #195 (night battles).

**2b. Dynamic point/spot lights with soft shadows (M)**
- **A `Light` component on any `MovableObject`**, in INI and Lua:
  - `LightColor`, `LightRadius`, `LightIntensity`
  - `LightFlicker`, `LightOffset`, `LightConeAngle`
  - `CastsShadows`
- **Shadows from a terrain SDF:**
  - Run a jump-flood pass over the occluder texture, recomputed only in dirty regions.
  - Sphere-trace per light, which gives soft penumbrae for free.
  - Lights are drawn as screen-space quads, clipped to radius.
- **Backwards-compat bridge.** Every existing `ScreenEffect` glow automatically becomes:
  - an **emissive sprite**, the glow PNG drawn into the emissive buffer, and
  - a **light** with colour sampled from the PNG's average and radius from its size.

  So muzzle flashes, explosions, jet thrusters and laser dots across all 10 modules start lighting the world with **zero content changes**. `EffectAlwaysShows` and fog-of-war culling keep their meaning.
- **Palette emissive flags.** Mark palette indices as emissive (for example the existing glow indices 117, 98, 120, lava, and hot-metal colours) so 8-bit art glows without new assets.

**2c. 2D global illumination with radiance cascades (L, stretch)**
- **Radiance cascades** compute 2D GI from *any number* of emitters at a fixed cost per pixel. That suits CC's chaos: hundreds of tracers, fire particles and explosions at once.
- Emissive buffer + occluder SDF go in; bounce-lit, shadowed light comes out.
- This replaces 2b's per-light loop at high settings. 2b stays as the "Medium" preset and for low-end GPUs.

**2d. Lit sprites (M)**
- **Auto-normals for all legacy sprites.** At atlas build time, derive a normal map from the 8-bit sprite: edge-distance bevel plus luminance height. Bake it into a parallel atlas page. Every soldier, dropship and gib now catches rim light from explosions.
- **Terrain edge normals**, derived from the material bitmap (distance to air), so terrain surfaces and cave walls catch light at their edges. This is huge for the look: terrain stops being a flat cutout.
- **Optional authored maps:** INI `NormalMapFile`, `EmissiveFile` next to `SpriteFile`, using the same frame-numbering convention. They override auto maps when present.

### Phase 3: Post-processing stack (M)

All in HDR, all tweakable in the Graphics Lab, all per-scene overridable:

- [ ] **Bloom:** dual-filter (Kawase) down/up chain with threshold and knee. It replaces the old "screen-blend glow" look while keeping its spirit.
- [ ] **Exposure:** auto-adaptation via a luminance histogram, so walking into a dark cave lets eyes adjust and explosions punch.
- [ ] **Tone mapping:** AgX or ACES, selectable.
- [ ] **Colour grading:** a 3D LUT per scene and per faction/activity, crossfadable (for example "nuke flash", "low health"). This replaces `FadeInPalette` / `FadeOutPalette` visually, though the palette API keeps working.
- [ ] **Distortion buffer:**
  - Heat haze above fire, jets and hot barrels.
  - **Shockwave rings** from explosions, driven by gib/explosion events.
  - Refraction of the composite.
- [ ] **Screen-space god rays** from the sky through cave openings and smoke (radial blur of the sky-light mask).
- [ ] **Subtle extras:**
  - chromatic aberration and lens dirt on big blasts
  - film grain
  - vignette
  - all off by default in "Clean" presets
- [ ] **Upscaler:**
  - A crisp pixel-art upscale (sharp-bilinear / fwidth AA; `textureAA` already exists in the shaders) at arbitrary window sizes.
  - Optional CRT/scanline mode as a fun extra.

### Phase 4: Particles and FX (M–L)

- [ ] **GPU visual-particle system** for effects with no gameplay consequence:
  - smoke, embers, sparks, dust, shell-casing glints, debris dust
  - instanced, alpha or additive, lit by the light buffer, soft-faded at terrain
  - new INI `VisualEmitter` / `AddVisualEmission`, mirroring `AEmitter` syntax
  - sim MOPixels and MOSParticles are untouched, since gameplay particles stay CPU-authoritative
- [ ] **Lit volumetric smoke.** Smoke particles write density into a low-res buffer that lights scatter through. Muzzle flashes light up their own smoke.
- [ ] **In-world translucency** for existing sprites. Add an INI `DrawMode = Additive | Alpha | Opaque` per MO, drawn in the FX pass. (Today translucency exists only in editor previews.)
- [ ] **Terrain decals:**
  - blood splatter, scorch marks and bullet pocks in a GPU decal layer over terrain colour
  - it never touches the material bitmap, so physics is unaffected
  - decals are removed when the underlying terrain pixel is destroyed (sample the material mirror)
- [ ] **Hot pixels.** Freshly cratered terrain edges and molten debris glow and cool over a few seconds, through an emissive-decay texture.

### Phase 5: World and atmosphere (M)

- [ ] **Procedural sky:** a gradient, sun/moon, stars at night, and clouds as a GPU background layer. It works on any scene, with existing background PNGs as overlays.
- [ ] **Depth fog / aerial perspective** for parallax background layers. Z-order already exists (BG z=100, terrain BG z=50), so tint and fade by depth and time of day.
- [ ] **Weather:**
  - rain, snow, ash and dust storms as GPU particles
  - they collide with terrain via the material mirror, splash, and get occluded by overhangs
  - per-scene INI and Lua control
- [ ] **Ambient life:** dust motes in light shafts, drips in caves, distant lightning that briefly drives the sky light.
- [ ] **Soft fog of war:**
  - Port Causeless' `dynamic-two-stage-fow` approach.
  - Render the unseen layer as a filtered, blurred mask with a dithered edge, instead of hard blocks.

### Phase 6: UI and feel (M)

- [ ] **Native-resolution UI.** The game world stays pixel-art, while the UI renders at window resolution with a scale setting.
- [ ] **Font upgrade.** Keep the bitmap-font option for authenticity. Add a crisp TTF/SDF font path for menus and small text at high DPI.
- [ ] **HUD refresh:**
  - animated health and ammo
  - damage-direction indicators
  - lit, blooming pie menu
  - smoother transitions
- [ ] **Graphics settings menu:**
  - presets (Potato / Low / Medium / High / Ultra / "Classic", which is today's look)
  - per-feature toggles
  - internal resolution
  - frame cap
  - vsync, including the Windows windowed-vsync fix
- [ ] **Juice:** hit-stop on big impacts, camera kick and recoil (render-only, never sim), and smoother screen shake.

### Phase 7: Modding API and content upgrades (ongoing)

- **Lua:**
  - `Light` objects
  - `PostProcessMan` parameter control (bloom, exposure, LUT crossfade, distortion pulses)
  - `RegisterPostEffect` by **path** (the current `BITMAP*` signature is unusable from Lua)
  - per-scene atmosphere
- **INI:** document every new key; all of them default to "auto" so old mods just work.
- **Shader hooks:** extend the existing `Shader` entity registry so mods can attach a post shader per scene or activity, sandboxed to the uniform contract.
- **Optional truecolour sprite path:**
  - An RGBA sprite plus a generated 8-bit *index mask* for collision and hit tests.
  - Ship it only for new art, given the licensing caveat in §10.

### Phase 8: Platform future (L, later)

- An SDL_GPU backend behind the renderer abstraction: Vulkan, Metal, D3D12, and macOS without deprecated GL.
- Re-evaluate GLES / Steam Deck targets. Upstream PR #40 (GLES) has groundwork.

---

## 5. Suggested order and first milestones

| Milestone | Contents | Why first |
|---|---|---|
| **M1** (Phase 0) | `gpu-renderer` + interpolation merged, golden harness, Tracy GPU zones, bug fixes | De-risks everything; instant smoothness win |
| **M2** (Phase 1 core) | GPU sprite batch + atlas, dirty-rect terrain | Unblocks lighting; big performance win |
| **M3** ("first light" vertical slice) | HDR buffer, sky light + ambient, glow→light bridge, bloom + tonemap | **The moment it looks like a new game.** A demo-able clip for Discord or a trailer |
| **M4** | SDF soft shadows, terrain edge normals, auto sprite normals, day/night | Depth and drama |
| **M5** | Post stack (LUTs, distortion, god rays), GPU particles, lit smoke | Polish and spectacle |
| **M6** | UI to GPU, native-res UI, settings menu, presets | Ship-ready |
| **M7+** | Radiance cascades GI, weather, decals, mod API docs, SDL_GPU | Next level |

**Recommended immediate next step:** check out `origin/gpu-renderer`, rebase it onto `development`, build it, and assess how far it is from parity. That single afternoon tells us whether M1 takes one week or four.

---

## 6. Performance budget (target: 1080p window, 960×540 internal, mid-range GPU)

| Pass | Budget |
|---|---|
| Albedo + normals + emissive | ≤ 1.5 ms |
| SDF / occluder update (dirty only) | ≤ 0.5 ms |
| Lighting (2b) / GI (2c) | ≤ 1.5 ms / ≤ 3 ms |
| Post stack | ≤ 1.5 ms |
| UI | ≤ 0.3 ms |
| **Total GPU** | **≤ 5–7 ms**, leaving headroom for 144 Hz with interpolation |

On the CPU side, Phase 1 should *reduce* render cost versus today, by removing CPU rasterisation and full re-uploads, and give that headroom back to the sim.

## 7. Testing strategy

- **Golden images per milestone**, with deliberate visual changes approved by updating baselines.
- A **"Classic" preset that must keep matching** today's output. This protects players who want the original look, and catches accidental regressions in the albedo path.
- **Stress scenes:** YskelyRefinery (20000×8000), 4-way split, wrap-X and wrap-Y scenes, nuke spam (hundreds of lights), heavy Lua mods.
- **Determinism check:** the same replay produces identical sim state with all graphics features on or off.

## 8. Risks

| Risk | Mitigation |
|---|---|
| `gpu-renderer` rebase is harder than expected | Assess first (§5). Worst case, cherry-pick its abstractions and redo the integration |
| Palette semantics break subtly (debris colours, Lua index colours, fades) | Keep the palette as the albedo source of truth; golden tests; Classic preset |
| Huge scenes blow VRAM with extra buffers (normals, SDF, emissive) | Per-view, screen-sized lighting buffers; world-sized data only for the occluder mirror, kept at R8 and tiled |
| Lighting makes gameplay unreadable (too dark) | Minimum ambient floor; team-colour rim light on actors; a "visibility" accessibility slider |
| Scope creep | Each phase ships on its own; M3 is the motivational checkpoint |
| macOS GL deprecation | Abstraction discipline now; SDL_GPU in Phase 8 |

## 9. Things deliberately out of scope (for now)

- Changing physics resolution or the material model.
- Fluid simulation (water or lava as simulated fluids).
- Redrawing all original art.
- Reviving multiplayer. It was removed upstream in #179, and its frame-streaming design doesn't fit a GPU renderer.

## 10. Licensing note

- **Code** is AGPLv3, which is fine to fork and ship, with source.
- **The `Data/` assets have no licence file.** They're the original Data Realms content.
- FMOD is proprietary.

Before distributing a fork with modified art or a rebrand, ask the maintainers about the asset terms. Engine and shader work, and *new* art we create, carry no such ambiguity.

---

## Appendix: key code locations

| Area | Location |
|---|---|
| Frame orchestration | `Source/Managers/FrameMan.cpp:810-947` (`FrameMan::Draw`) |
| Main loop / timing | `Source/Main.cpp:293-402`, `Source/Managers/TimerMan.cpp:63-106` |
| Scene layer upload | `Source/Entities/SceneLayer.cpp:427-559`, `Source/Renderer/BigTexture.*` |
| MO CPU drawing | `Source/Managers/MovableMan.cpp:1282,1689`, `Source/Entities/MOSRotating.cpp:1649-1761` |
| Texture cache | `Source/Managers/GLResourceMan.cpp` |
| Glow / post pass | `Source/Managers/PostProcessMan.cpp:326-436` |
| Final composite | `Source/Managers/WindowMan.cpp:757-816` |
| Shaders | `Data/Base.rte/Shaders/*.frag/.vert`, `Shaders.ini` |
| Modified rlgl | `Source/Renderer/raylib/rlgl.{c,h}` (`rlZDepth`, advanced blends) |
| Terrain | `Source/Entities/SLTerrain.cpp` (3 layers, `TexturizeTerrain`) |
| Effect INI keys | `Source/Entities/MovableObject.cpp:343-368` |
| Lua bindings | `Source/Lua/LuaBindingsManagers.cpp`, `LuaBindingsEntities.cpp` |
| Upstream branches | `origin/gpu-renderer`, `origin/interpolated-render-redux`, `origin/dynamic-two-stage-fow` |

---

## Progress log

Work happens on the local `modernisation` branch. Each entry corresponds to one or more commits.

### M1: Foundation (in progress)

- **Merged `origin/gpu-renderer`** (HeliumAnt's WIP rewrite):
  - Added the new renderer sources to `RTEA.vcxproj` (the branch only updated meson).
  - Made `no_sanitize_address` portable to MSVC.
  - Resolved the conflict with #277 (post-effect rotation).
- **Brought the GPU renderer to parity with `development`.** The branch was mid-refactor, so a lot was missing:
  - HUD: bridged to the CPU HUD, drawn once per screen.
  - Lua/graphical primitives: every shape implemented on the new batch.
  - Title screen: nebula, planet, moon, station, logo and slides.
  - Clipped menu text: a depth-test bug in `UploadFrame`.
  - Sprite particles (`MOSParticle`) drew nothing.
  - `MOSprite` offset and flip were wrong; `Scale` was ignored.
  - CPU-path previews (placement, deployment, pie menu, inventory carousel, scenario markers) called no-op wrappers.
  - Leftover debug pixels and lines drawn every frame.
  - Alpha blending was never enabled, and translucent draws punched holes in render targets.
- **Render interpolation:**
  - Render-only previous-state snapshots, interpolated by the sim accumulator. Wrap-aware, snaps on teleport, pixel-rounded.
  - Follows Causeless' approach, without the input changes from `interpolated-render-redux` that broke weapon switching.
- **Upstream bugs fixed along the way:**
  - `AudioMan::m_MuteAudioOnFocusLoss` was uninitialised. The garbage written to `Settings.ini` made the reader drop every later setting.
  - `EndlessMetaGameMode` was read under the wrong key.
- **Dev tooling:** `Mods/RenderTest.rte` (gitignored) holds a primitive gallery and an FX stress script, plus scripted screenshot capture for side-by-side comparison against a `development` baseline worktree.

**Still open for parity:**
- Atom trails on the GPU.
- Background fill colours for non-wrapping layers.
- Dot glows. These are folded into the lighting work (palette emissive flags) instead.
- A split-screen check.

### M1 parity, continued

- **Tracers:** atom trails drawn on the GPU (they were invisible).
- **Previews:** placement, deployment and bunker previews, the pie menu background and the inventory carousel work again.

### M3 "First light": done

- **`SceneLighting`** (`Source/Renderer/SceneLighting.*`, shaders in `Data/Base.rte/Shaders/Lighting/`):
  - A world light grid, refreshed round-robin from the terrain material layer.
  - Sky light propagated on the GPU.
  - Glow lights with soft terrain shadows.
  - HDR composite, with distant background layers detected via depth.
  - Glows screen-blended as emissive light. This also fixes glows being missing on the GPU renderer.
  - Bloom, a highlight shoulder and a vignette.
- **Time of day and day/night cycle:** `TimeOfDay`, `DayLengthMinutes`.
- **Modder lights:**
  - INI keys on `MovableObject`: `LightColor`, `LightRadius`, `LightIntensity`, `LightFlicker`, `LightOffset`.
  - The same properties exposed to Lua, plus `PostProcessMan:AddLight(pos, radius, r, g, b, intensity)`.
- **Graphics Lab** (ImGui):
  - Live tuning for every parameter, with stats and debug views (lighting, sky, dynamic, normals).
  - Settings persist in `Settings.ini`.
- **Upstream bug fixed:** scene effects registered by activities and global scripts were cleared before they could be drawn.

### M4 (in progress)

- **Automatic edge normals** written by the default sprite shader into a second attachment, used for:
  - N·L shading of dynamic lights.
  - Sky light from above.
- **Performance:** the full lighting pipeline costs about 0.1 ms; frame time is dominated by CPU draw submission (one draw call per particle). Sprite batching is the next performance item.


### Status update: M4 and M5 largely done, M6 and M7 started

- **Lighting quality:**
  - Edge normals.
  - Emissive palette colours: gold sparkle and tracers glint.
  - A foreground readability floor.
  - Blending fixed so it never touches the normal attachment.
- **Post stack:**
  - Heat haze and explosion shockwaves.
  - God rays from the sky into caves.
  - Atmospheric haze on parallax layers.
  - Colour grading (temperature, tint, contrast, split toning), film grain and chromatic aberration.
- **World:**
  - Procedural rain and snow: GPU-generated, wind-blown, kept out from under overhangs, lit by fire.
  - A day/night cycle.
  - Per-scene atmosphere INI keys. Rayvord Tundra and Paeterra Ice Caves now snow.
  - Scorch marks with cooling, glowing crater rims (a GPU decal map; the physics bitmaps are never touched).
- **Fog of war:** it was never drawn on the GPU renderer, because cameras had no team. It now draws with soft, dithered edges.
- **Split-screen:** verified, with each screen lit independently.
- **Settings and tools:**
  - Video settings toggles for Lighting, Bloom and Heat/Shockwaves; turning all three off gives the classic look.
  - The Graphics Lab exposes everything.
- **Modding:**
  - `Documentation/LightingAndEffects.md`.
  - Lua API for lights, atmosphere and grading.
  - INI lights on any `MovableObject`.
- **Performance:**
  - Draw-call merging, and sprite transforms baked on the CPU.
  - Removed per-frame `std::cout` spam from `BigTexture` uploads.
  - The lighting pipeline costs about 0.7 ms per screen.

**Still open:**
- A GPU HUD and fonts at native resolution.
- A sprite atlas.
- Dirty-rect terrain uploads.
- A general visual particle system.
- Radiance-cascade GI.
- An SDL_GPU backend.
- A CI-run golden-image harness. The scripted screenshot comparisons currently live outside the repo.


### Status update: performance, polish, tooling

- **Performance:**
  - Draw calls are pooled and quads built in place. Final build on the night bunker stress scene: 169 → 250 FPS, draw time 4.0 → 2.5 ms.
  - Verified in the shipping Final configuration.
- **New effects:**
  - One-bounce screen-space indirect light.
  - Embers rising from fire.
  - Dust motes in god rays.
  - Per-object `RenderBlendMode` (Normal, Additive, Screen) and `RenderOpacity`, cascading to attachables.
- **Classic parity:**
  - With Lighting, Bloom and Extra Effects off, the image matches `development` side by side.
  - The tonemap is neutral when lighting is off.
  - Fixed: a highlight shoulder of 1.0 produced NaNs and a black screen.
- **Tooling:** `Tools/RenderTest`, an in-repo scenario capture harness:
  - Scenario settings, a dev mod, burst captures, baseline comparison, contact sheets.
  - README included.
- **Formatting:** lines changed on this branch are clang-format clean (formatted with clang-format-diff, so upstream code is left alone).

### Remaining roadmap (not started)

- **GPU HUD and native-resolution UI and fonts (M6):** replace the CPU HUD bridge and the Allegro GUI backend.
- **Sprite atlas and instanced sprite batching:** the next large performance step.
- **Dirty-rect terrain uploads:** visible layers still re-upload every frame. Cheap enough now (2.5 ms total draw), but wasteful.
- **Full radiance-cascade GI:** the screen-space bounce covers most of the visual win.
- **Graphics quality presets:** Potato to Ultra.
- **An SDL_GPU backend (M8)** behind the renderer abstraction.
- **CI golden-image runs** using `Tools/RenderTest`.
