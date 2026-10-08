// LightComposite.frag
// Lights the scene: albedo (the unlit scene as drawn) times the light reaching each pixel, written to an HDR target in linear space.
// At full daylight the light is exactly the sky color, so with a white sky the original art is reproduced unchanged.
#version 330 core

in vec2 textureUV;
out vec4 FragColor;

uniform sampler2D rteAlbedo; // The unlit player screen.
uniform sampler2D rteDynamicLight; // Screen space, RGB = linear light from dynamic lights.
uniform sampler2D rteEmissive; // Screen space, RGB = glows in gamma space, screen blended.
uniform float rteEmissiveIntensity;
uniform float rteMaxDynamicLight; // Dynamic light softly saturates towards this, so piles of overlapping lights don't blow out.
uniform sampler2D rteSkyLight; // World grid, R = sky light 0..1, G = how much of the sun (or moon) is visible, linearly filtered.
uniform sampler2D rteOccupancy; // World grid, R = terrain coverage 0..1, linearly filtered.
uniform sampler2D rteOccluders; // Player screen: RG = position of the nearest pixel of a solid object.
uniform sampler2D rteSurface; // Player screen surface values, B = 1 where a solid object was drawn.
uniform vec2 rteSunDirection; // Unit vector towards the sun (or the moon at night), in screen pixels (y down).
uniform float rteSunShadows; // How much shade darkens the sky light, 0 (no directional daylight) to 1.
uniform vec3 rteShadeTint; // What sky light is multiplied by in full shade at full strength: darker and cooler.
uniform float rteUnitShadows; // How dark the shadows of solid objects are, 0 (off) to 1.
uniform float rteContactShading; // How much background walls darken right next to solid objects and terrain, 0 (off) to 1.
uniform float rteMetals; // How strongly metallic surfaces mirror their surroundings and glint in the sun, 0 for none.
uniform float rteBackgroundBlur; // How much the far background layers are softened, 0 for none.
uniform vec2 rteSunPosition; // Where the sun is in the sky, in screen pixels as gl_FragCoord (y 0 is the top of the player screen).
uniform vec3 rteSunDisc; // The sun's color and brightness, linear. Black when it's down or hidden by weather.
uniform float rteCloudShadows; // How much drifting clouds shade the ground, 0 for none.
uniform float rteCloudDrift; // How far the clouds have drifted, in scene pixels.
uniform float rteSpecular; // Strength of highlights on shiny surfaces.
uniform sampler2D rteSceneDepth; // The player screen's depth buffer.
uniform float rteBackgroundDepth; // Depth beyond which pixels belong to the distant background layers (or nothing was drawn).
uniform vec3 rteBackgroundLight; // Linear light on the distant background layers.
uniform float rteBackgroundNearDepth; // Depth of the nearest background layers.
uniform float rteBackgroundFarDepth; // Depth of the furthest background layers.
uniform vec3 rteAtmosphereColor; // Linear haze color, time of day included.
uniform float rteAtmosphereHaze; // How much the furthest layers fade into the haze.
uniform vec2 rteScreenSize;
uniform vec2 rteScreenOrigin; // World position of the screen's top left pixel.
uniform vec2 rteGridWorldSize; // World size covered by the sky light grid.
uniform vec3 rteAmbient; // Linear light where no sky light reaches.
uniform vec3 rteSkyColor; // Linear light under open sky.
uniform int rteDebugView; // 0 final, 1 lighting on grey, 2 sky light only, 3 dynamic light only, 4 normals, 6 GI only, 7 solid objects and the distance to them, 8 where the sun is visible.
uniform sampler2D rteNormals; // Player screen normals, RGB = normal * 0.5 + 0.5, A = 1 where something was drawn.
uniform float rteEdgeLighting;
uniform float rteForegroundDepth; // Depth below which pixels are foreground terrain and objects.
uniform vec3 rteForegroundAmbient; // Minimum light on the foreground, so the playfield stays readable deep underground.
uniform sampler2D rteIndirect; // Last frame's lit scene, heavily blurred: the light bouncing off nearby surfaces.
uniform float rteIndirectStrength;
uniform vec2 rteIndirectOffset; // How far the screen moved since the indirect light was made, in pixels.
uniform sampler2D rteGI; // Light from radiance cascades, quarter resolution.
uniform float rteGIStrength;
uniform float rteNightSky; // 0 by day, 1 at full night: stars and the moon show on the furthest sky layers.
uniform float rteWaterGlow; // How much the light of lamps, fires and blasts shows as a glow in water it passes through, in the light's own colour.
uniform float rteSkyRecolor; // 0..1, how far the sky layers' own painted colours are replaced by the sky of the hour (0 around midday, when the art is right as it is).
uniform vec3 rteSkyDaylight; // The colour of daylight at this hour (white at noon), apart from how bright the player has set the light on the scene.
uniform float rteSkyOwnLight; // 0..1, how far the sky is lit by that rather than by the scene's sky light setting.
uniform vec3 rteSkyZenith; // The sky's colour overhead at this hour, linear.
uniform vec3 rteSkyHorizon; // And at the horizon.
uniform vec3 rteSkyCloud; // What the light of the hour makes of white cloud.
uniform vec2 rteMoonPosition; // Screen pixels, as gl_FragCoord (y 0 is the top of the player screen).
uniform float rteTime; // Seconds, for twinkling.
uniform float rteWaterReflection; // How strongly water mirrors the scene above its surface, 0 for none.
uniform sampler2D rteFog; // World grid, R = how thick mist or dust hangs in the air there (FogUpdate.frag).
uniform float rteFogStrength; // How thick the fog volume is drawn, 0 for none.
uniform float rteWaterRefraction; // How much water's ripples bend what's seen through it and how much it darkens with depth, 0 for none.
uniform bool rteWaterMirrorSurface; // The reflection is wobbled by the tilt of the surface above each pixel (the terrain pass's normal there, which follows the flow), the whole column together. Off: by the pixel's own tilt, as before.

// Whether a pixel of the player screen is water: the terrain pass flags it with a quarter in the surface buffer's B.
bool WaterAt(vec2 position) {
	if (position.y < 0.0 || position.x < 0.0 || position.x >= rteScreenSize.x || position.y >= rteScreenSize.y) {
		return false;
	}
	return abs(texelFetch(rteSurface, ivec2(position), 0).b - 0.25) < 0.08;
}

// How many pixels up from a water pixel the first pixel that isn't water is (y 0 is the top of the player screen), looked for in steps of four
// and then pixel by pixel, up to 64. -1 when the water goes on further than that, where there's nothing near enough to mirror.
float WaterSurfaceDistance(vec2 position) {
	float last = 0.0;
	for (int i = 1; i <= 16; ++i) {
		float reach = float(i) * 4.0;
		if (!WaterAt(position - vec2(0.0, reach))) {
			for (float k = last + 1.0; k < reach; k += 1.0) {
				if (!WaterAt(position - vec2(0.0, k))) {
					return k;
				}
			}
			return reach;
		}
		last = reach;
	}
	return -1.0;
}

// Distance in pixels from a point of the player screen to the nearest solid object. The map only reaches about 60 pixels, so it's capped.
float OccluderDistance(vec2 position) {
	return min(distance(texture(rteOccluders, position / rteScreenSize).xy, position), 48.0);
}

// How much of the sun gets past solid objects on its way to a pixel, 0 to 1. See ObjectShadow in PointLight.frag; this one looks along the sun's direction, and objects only shade what's within about a hundred pixels of them.
float ObjectSunShadow(vec2 from, bool fromSolid) {
	float t = 1.5;
	if (fromSolid) {
		int steps = 0;
		for (; steps < 9; ++steps) {
			if (OccluderDistance(from + rteSunDirection * t) > 1.0) {
				break;
			}
			t += 2.0;
		}
		if (steps == 9) {
			return 1.0;
		}
		t += 1.0;
	}
	float start = t;
	float visibility = 1.0;
	for (int i = 0; i < 16 && t < 110.0; ++i) {
		vec2 position = from + rteSunDirection * t;
		if (position.x < 0.0 || position.y < 0.0 || position.x >= rteScreenSize.x || position.y >= rteScreenSize.y) {
			break;
		}
		float clearance = OccluderDistance(position);
		if (clearance < 0.8) {
			// Fade the shadow out towards the end of its reach instead of cutting it off.
			return smoothstep(70.0, 110.0, t);
		}
		// The sun is a small disc: the soft edge of a shadow widens steadily with the distance from what casts it.
		visibility = min(visibility, clearance / (0.09 * (t - start) + 1.0));
		t += max(clearance * 0.95, 1.0);
	}
	return clamp(visibility, 0.0, 1.0);
}

float CloudHash(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float CloudNoise(vec2 p) {
	vec2 cell = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(CloudHash(cell), CloudHash(cell + vec2(1.0, 0.0)), f.x), mix(CloudHash(cell + vec2(0.0, 1.0)), CloudHash(cell + vec2(1.0, 1.0)), f.x), f.y);
}

// How much of the sun a drifting cloud hides above a point of the scene, 0 to 1. Clouds are wide soft patches a few hundred pixels across; the shadow falls along the sun's direction, so it leans with the time of day.
float CloudShade(vec2 worldPos) {
	// Where the sun's ray to this point crosses the height of the clouds: only the x matters, a column of ground shares a cloud.
	float along = worldPos.x + rteSunDirection.x / max(-rteSunDirection.y, 0.2) * worldPos.y - rteCloudDrift;
	float cloud = 0.65 * CloudNoise(vec2(along / 420.0, 3.7)) + 0.35 * CloudNoise(vec2(along / 150.0, 9.1));
	return smoothstep(0.45, 0.65, cloud);
}

// The sun: a bright disc with a wide soft glow. Linear, added on top of the sky.
vec3 SunDisc(vec2 fragCoord) {
	float sunDistance = length(fragCoord - rteSunPosition);
	float disc = 1.0 - smoothstep(7.0, 8.5, sunDistance);
	float glow = exp(-sunDistance / 16.0) * 0.55 + exp(-sunDistance / 70.0) * 0.18;
	return rteSunDisc * (disc * 2.2 + glow);
}

float StarHash(vec2 p) {
	p = fract(p * vec2(123.34, 456.21));
	p += dot(p, p + 45.32);
	return fract(p.x * p.y);
}

// Pixel stars with a slow twinkle, and a pixel-art moon with a soft halo. Linear, added on top of the lit sky.
vec3 NightSky(vec2 fragCoord) {
	// The sky barely moves with the camera, like the furthest parallax layers.
	vec2 skyPixel = floor(fragCoord + rteScreenOrigin * vec2(0.03, -0.03));
	float hash = StarHash(skyPixel);
	vec3 sky = vec3(0.0);
	if (hash > 0.9965) {
		float brightness = (hash - 0.9965) / 0.0035;
		float twinkle = 0.65 + 0.35 * sin(rteTime * (0.8 + brightness * 2.5) + hash * 300.0);
		vec3 tint = mix(vec3(0.75, 0.82, 1.0), vec3(1.0, 0.9, 0.75), fract(hash * 97.0));
		sky += tint * (0.15 + 0.85 * brightness * brightness) * twinkle * 0.9;
	}
	vec2 toMoon = fragCoord - rteMoonPosition;
	float moonDistance = length(toMoon);
	if (moonDistance < 6.5) {
		// A crescent-ish shading so it reads as a moon, not a lamp.
		float shade = 0.75 + 0.25 * clamp(dot(normalize(toMoon + vec2(0.001)), vec2(-0.7, 0.7)), -1.0, 1.0);
		sky += vec3(0.95, 0.95, 1.0) * 1.3 * shade;
	} else {
		sky += vec3(0.55, 0.62, 0.85) * 0.22 * exp(-(moonDistance - 6.5) / 14.0);
	}
	return sky;
}

void main() {
	vec2 screenUV = gl_FragCoord.xy / rteScreenSize;
	vec4 albedo = texture(rteAlbedo, screenUV);

	vec3 light;
	// Light thrown straight back by shiny surfaces: added on top of the lit surface, white on most things and tinted by the surface on metal.
	vec3 highlights = vec3(0.0);
	float metalness = 0.0;
	float nightSkyAmount = 0.0;
	vec3 waterGlow = vec3(0.0); // Light scattered in water at this pixel.
	vec3 waterReflection = vec3(0.0); // What water mirrors at this pixel, linear and unlit, and how much (see rteWaterReflection).
	float waterReflectionAmount = 0.0;
	bool waterReflectionBackground = false; // The mirrored pixel is distant scenery, lit as the background is.
	float skyLayer = 0.0; // How much this pixel is the sky itself: the furthest layers, or nothing drawn at all.
	float sceneDepth = texture(rteSceneDepth, screenUV).r;
	float haze = 0.0;
	if (sceneDepth > rteBackgroundDepth) {
		// Background layers are far behind the action: they aren't shadowed by terrain or lit by explosions in front of them, but fade into the atmosphere with distance.
		// Distant scenery is lit by the hour's own daylight. (The sky light setting is for how much of it falls on the scene in front; turned down for
		// moodier ground, it used to turn the midday sky and mountains navy as well.)
		// Away from midday that light is the sky's own colour at the hour, so mountains are dark shapes against a night sky and take the red of a dusk.
		light = mix(rteBackgroundLight, mix(rteSkyDaylight, (rteSkyHorizon + rteSkyZenith) * 0.32, rteSkyRecolor), rteSkyOwnLight);
		float distance = clamp((sceneDepth - rteBackgroundNearDepth) / (rteBackgroundFarDepth - rteBackgroundNearDepth), 0.0, 1.0);
		// Nothing drawn at all (cleared depth) is open sky.
		// The furthest layers are usually the sky itself, which shouldn't be washed out; haze peaks on the distant scenery in between.
		haze = sceneDepth >= 0.9999 ? rteAtmosphereHaze * 0.4 : rteAtmosphereHaze * smoothstep(0.0, 0.6, distance) * (1.0 - 0.6 * smoothstep(0.85, 1.0, distance));
		if (rteBackgroundBlur > 0.0 && distance > 0.15) {
			// The further a layer, the softer it is drawn, like a lens focused on the fight. Only other background pixels are mixed in, so nothing in front bleeds into it.
			vec2 reach = rteBackgroundBlur * distance * 1.6 / rteScreenSize;
			vec4 mixed = albedo;
			float count = 1.0;
			for (int i = 0; i < 4; ++i) {
				vec2 offset = vec2(i < 2 ? 1.0 : -1.0, (i % 2 == 0) ? 1.0 : -1.0) * reach;
				if (texture(rteSceneDepth, screenUV + offset).r > rteBackgroundDepth) {
					mixed += texture(rteAlbedo, screenUV + offset);
					count += 1.0;
				}
			}
			albedo = mixed / count;
		}
		// Only layers that barely scroll (the sky itself) get stars, so they never show on mountains or nearer scenery.
		// They also fade towards the horizon, where distant mountains usually are.
		skyLayer = smoothstep(0.88, 0.95, distance);
		nightSkyAmount = rteNightSky * smoothstep(0.88, 0.95, distance) * (1.0 - smoothstep(0.25, 0.48, screenUV.y)); // Player screens are drawn top down: UV y 0 is the top.
	} else {
		vec2 worldPos = rteScreenOrigin + gl_FragCoord.xy;
		float sky = texture(rteSkyLight, worldPos / rteGridWorldSize).r;
		// How far sky light reaches here before shaping: it fades within a few pixels of entering solid ground, which marks the band just under the terrain's surface.
		float skyReach = sky;
		// Shape the falloff a little so cave mouths stay bright and deep caves get properly dark.
		sky = smoothstep(0.0, 1.0, sky);
		// Sky light comes from above: upward facing edges catch more of it, undersides less.
		vec4 normalSample = texture(rteNormals, screenUV);
		if (normalSample.a > 0.25) {
			float normalY = normalSample.y * 2.0 - 1.0;
			sky *= mix(1.0, clamp(1.0 - normalY * 0.9, 0.35, 1.6), rteEdgeLighting);
		}
		if ((rteWaterReflection > 0.0 || rteWaterRefraction > 0.0) && normalSample.a > 0.25 && WaterAt(gl_FragCoord.xy)) {
			// Water: what's behind it bent by the ripples and darker the deeper it is, and the scene above the surface mirrored in it.
			vec2 tilt = normalSample.xy * 2.0 - 1.0;
			float surfaceDistance = WaterSurfaceDistance(gl_FragCoord.xy);
			float depth = surfaceDistance < 0.0 ? 64.0 : surfaceDistance;
			if (rteWaterRefraction > 0.0) {
				// Only bent towards more water, so the edge of a pool never pulls in the rock beside it.
				vec2 bent = gl_FragCoord.xy + tilt * rteWaterRefraction * (3.0 + depth * 0.08);
				if (WaterAt(bent)) {
					albedo.rgb = texture(rteAlbedo, bent / rteScreenSize).rgb;
				}
				albedo.rgb *= 1.0 - 0.3 * min(rteWaterRefraction, 1.0) * smoothstep(4.0, 64.0, depth);
			}
			// Only a real surface mirrors: open to air (or a unit) above it. Water filling a tunnel up to its rock roof has no surface to mirror in,
			// only the refracted wall behind it.
			vec2 aboveSurface = gl_FragCoord.xy - vec2(0.0, surfaceDistance);
			bool openAbove = surfaceDistance > 0.0 && (texture(rteSceneDepth, aboveSurface / rteScreenSize).r >= rteForegroundDepth || texture(rteSurface, aboveSurface / rteScreenSize).b > 0.5);
			if (rteWaterReflection > 0.0 && openAbove) {
				// What a mirror shows is bent by the surface itself, so the tilt is read where the surface is: the topmost water pixel of this column. The whole
				// column then moves together, an image rippling as the surface does (glassy where the water is still, rippling where it flows).
				vec2 mirrorTilt = tilt;
				if (rteWaterMirrorSurface) {
					vec4 surfaceNormal = texture(rteNormals, (gl_FragCoord.xy - vec2(0.0, surfaceDistance - 1.0)) / rteScreenSize);
					if (surfaceNormal.a > 0.25) {
						mirrorTilt = surfaceNormal.xy * 2.0 - 1.0;
					}
				}
				// Mirrored about the surface line (half a pixel above the topmost water pixel), shifted sideways by the ripples, more the deeper.
				vec2 mirrored = vec2(gl_FragCoord.x + mirrorTilt.x * (2.0 + depth * 0.25), gl_FragCoord.y - 2.0 * surfaceDistance + 1.0);
				// Faded out where the mirror point leaves the screen, and onto other water (nothing new to show).
				float fade = smoothstep(0.0, 16.0, mirrored.y) * smoothstep(0.0, 16.0, mirrored.x) * smoothstep(0.0, 16.0, rteScreenSize.x - mirrored.x);
				if (fade > 0.0 && !WaterAt(mirrored)) {
					vec2 mirroredUV = mirrored / rteScreenSize;
					waterReflection = pow(texture(rteAlbedo, mirroredUV).rgb, vec3(2.2));
					// Lit as the scene around the water is, or as distant scenery where the mirror shows the background.
					waterReflectionBackground = texture(rteSceneDepth, mirroredUV).r > rteBackgroundDepth;
					// Fresnel, in two dimensions: strongest just under the surface and where the ripples tip the water towards the view of the sky.
					float fresnel = mix(0.75, 0.2, smoothstep(0.0, 40.0, depth)) + 0.5 * clamp(length(mirrorTilt), 0.0, 0.5);
					waterReflectionAmount = clamp(rteWaterReflection * fresnel * fade, 0.0, 0.85);
				}
			}
		}
		vec4 dynamicSample = texture(rteDynamicLight, screenUV);
		vec3 dynamicLight = rteMaxDynamicLight * (1.0 - exp(-dynamicSample.rgb / rteMaxDynamicLight));
		// Water scatters the light passing through it: a lamp under water sits in a glow of its own colour that spreads through the pool, over and above
		// lighting the water as it would any surface (which only ever gives the light's colour times the water's blue).
		if (rteWaterGlow > 0.0 && abs(texture(rteSurface, screenUV).b - 0.25) < 0.08) {
			waterGlow = dynamicLight * rteWaterGlow;
		}
		// Highlights from the lights, in the lights' own color.
		highlights = dynamicSample.rgb / max(max(dynamicSample.r, max(dynamicSample.g, dynamicSample.b)), 0.001) * min(dynamicSample.a, 6.0);
		// Daylight has a direction. Where the sun (or moon) can't be seen, the sky light is dimmer and cooler; under open sky in full sun it is exactly as without shadows.
		bool solidObject = normalSample.a > 0.25 && texture(rteSurface, screenUV).b > 0.5;
		bool terrainPixel = sceneDepth < rteForegroundDepth && !solidObject;
		vec3 skyColor = rteSkyColor;
		float daylight = sky;
		float terrainShade = 0.0;
		if (rteSunShadows > 0.0) {
			float sunVisible = texture(rteSkyLight, worldPos / rteGridWorldSize).g;
			float cloudShade = rteCloudShadows > 0.0 ? rteCloudShadows * CloudShade(worldPos) : 0.0;
			if (!terrainPixel) {
				// Clouds drift across the sun: wide soft shadows cross the scene with the wind.
				sunVisible *= 1.0 - cloudShade;
			}
			if (rteUnitShadows > 0.0) {
				sunVisible *= mix(1.0, ObjectSunShadow(gl_FragCoord.xy, solidObject), rteUnitShadows);
			}
			float shade = (1.0 - sunVisible) * rteSunShadows;
			if (terrainPixel) {
				// Solid ground is kept readable by a floor of light, which would hide the shade. So ground is shaded after that floor, in the band under its surface:
				// the side of a hill turned away from the sun, the ground under an overhang, the patch a unit's shadow falls on.
				terrainShade = shade * smoothstep(0.0, 0.25, skyReach);
				if (cloudShade > 0.0) {
					// A cloud's shadow has to read as a patch crossing the hillside, not a line along its top, so it reaches much deeper into sunlit ground than other shade does:
					// as far as sky light is found a little way above the pixel.
					vec2 gridUV = worldPos / rteGridWorldSize;
					float above = max(texture(rteSkyLight, gridUV - vec2(0.0, 30.0 / rteGridWorldSize.y)).r, texture(rteSkyLight, gridUV - vec2(0.0, 70.0 / rteGridWorldSize.y)).r * 0.7);
					float underSky = max(smoothstep(0.0, 0.25, skyReach), smoothstep(0.0, 0.3, above));
					terrainShade = max(terrainShade, cloudShade * rteSunShadows * 1.3 * underSky * sunVisible);
				}
			} else {
				skyColor *= mix(vec3(1.0), rteShadeTint, shade);
				// Wherever the sun does reach, it lights at nearly full strength however little sky light gets there: sunlight falling through a hatch or a doorway makes a bright patch on the walls inside.
				daylight = max(sky, sunVisible * min(rteSunShadows * 1.6, 1.0));
			}
		}
		light = mix(rteAmbient, skyColor, daylight) + dynamicLight;
		if (rteIndirectStrength > 0.0) {
			// One bounce: what the surroundings reflect. Reprojected for camera movement; the history is so blurry that's all it needs.
			vec2 historyUV = (gl_FragCoord.xy + rteIndirectOffset) / rteScreenSize;
			vec3 bounce = texture(rteIndirect, clamp(historyUV, vec2(0.0), vec2(1.0))).rgb;
			// Never let a bad value in the history feed back into every following frame.
			bounce = any(isnan(bounce)) || any(isinf(bounce)) ? vec3(0.0) : bounce;
			// Fully sky lit areas already look as authored, so the bounce mostly fills shadowed areas and caves.
			light += min(bounce, vec3(4.0)) * rteIndirectStrength * (1.0 - 0.85 * sky);
		}
		if (rteGIStrength > 0.0) {
			vec3 gi = texture(rteGI, screenUV).rgb;
			light += min(gi, vec3(6.0)) * rteGIStrength;
		}
		vec4 surfaceSample = normalSample.a > 0.25 ? texture(rteSurface, screenUV) : vec4(0.0);
		metalness = surfaceSample.r;
		if (rteMetals > 0.0 && normalSample.a > 0.25) {
			vec2 tiltXY = normalSample.xy * 2.0 - 1.0;
			vec3 normal = vec3(tiltXY, sqrt(max(1.0 - dot(tiltXY, tiltXY), 0.0)));
			if (metalness > 0.02) {
				// Metal mirrors what's around it instead of scattering light: the sky where it leans up, the dark ground where it leans down. Facing the viewer it shows the horizon, which leaves it as it was.
				float lean = -tiltXY.y;
				float mirrored = lean >= 0.0 ? mix(1.0, 1.75, lean) : mix(1.0, 0.4, -lean);
				light *= mix(1.0, mirrored, metalness * min(rteMetals, 1.5));
			}
			// The sun (or moon) glints on glossy surfaces turned halfway between it and the viewer, where daylight reaches.
			float gloss = max(surfaceSample.g, 1.0 - normalSample.b);
			if (gloss > 0.1) {
				vec3 halfway = normalize(vec3(rteSunDirection * 0.8, 0.6) + vec3(0.0, 0.0, 1.0));
				highlights += rteSkyColor * pow(max(dot(normal, halfway), 0.0), mix(24.0, 90.0, gloss)) * gloss * daylight * rteMetals * rteSpecular * mix(0.5, 1.6, metalness);
			}
		}
		if (rteContactShading > 0.0 && sceneDepth >= rteForegroundDepth) {
			// Background walls darken right next to solid objects and next to terrain, so things look anchored to the scene instead of pasted on.
			float nearObject = 1.0 - smoothstep(0.5, 7.0, OccluderDistance(gl_FragCoord.xy));
			vec2 gridUV = worldPos / rteGridWorldSize;
			vec2 reach = vec2(5.0) / rteGridWorldSize;
			float terrain = max(max(texture(rteOccupancy, gridUV + vec2(reach.x, 0.0)).r, texture(rteOccupancy, gridUV - vec2(reach.x, 0.0)).r), max(texture(rteOccupancy, gridUV + vec2(0.0, reach.y)).r, texture(rteOccupancy, gridUV - vec2(0.0, reach.y)).r));
			float nearTerrain = smoothstep(0.05, 0.7, terrain);
			light *= 1.0 - rteContactShading * max(nearObject, 0.8 * nearTerrain);
		}
		if (sceneDepth < rteForegroundDepth) {
			light = max(light, rteForegroundAmbient);
		}
		if (terrainShade > 0.0) {
			// Shade dims daylight, not the light of lamps and fire falling on the same ground.
			vec3 shadeFactor = mix(vec3(1.0), rteShadeTint, terrainShade);
			light = max(light - dynamicLight, vec3(0.0)) * shadeFactor + min(dynamicLight, light);
		}
		// Emissive palette colors (gold glints, glowing bits) shine regardless of the light around them.
		float emissive = normalSample.a > 0.25 ? max(normalSample.a - 0.5, 0.0) * 2.0 : 0.0;
		light = max(light, vec3(emissive * 1.6));
	}

	vec3 albedoLinear = pow(albedo.rgb, vec3(2.2));
	if (rteDebugView == 1) {
		albedoLinear = vec3(0.5);
	} else if (rteDebugView == 2) {
		vec2 worldPos = rteScreenOrigin + gl_FragCoord.xy;
		FragColor = vec4(vec3(texture(rteSkyLight, worldPos / rteGridWorldSize).r), 1.0);
		return;
	} else if (rteDebugView == 3) {
		FragColor = vec4(texture(rteDynamicLight, screenUV).rgb, 1.0);
		return;
	} else if (rteDebugView == 6) {
		FragColor = vec4(texture(rteGI, screenUV).rgb * max(rteGIStrength, 1.0), 1.0);
		return;
	} else if (rteDebugView == 7) {
		// Solid objects in white on the map of distances to them (black next to an object, lighter further away).
		bool solid = texture(rteNormals, screenUV).a > 0.25 && texture(rteSurface, screenUV).b > 0.5;
		// Background walls (which contact shading darkens) are tinted blue, the background layers beyond them red.
		vec3 layerTint = sceneDepth > rteBackgroundDepth ? vec3(1.0, 0.5, 0.5) : (sceneDepth >= rteForegroundDepth ? vec3(0.5, 0.7, 1.0) : vec3(1.0));
		FragColor = vec4(solid ? vec3(1.0, 0.9, 0.3) : layerTint * (0.08 + pow(OccluderDistance(gl_FragCoord.xy) / 48.0, 2.2) * 0.6), 1.0);
		return;
	} else if (rteDebugView == 8) {
		// Where the sun (or moon) can be seen from.
		vec2 worldPos = rteScreenOrigin + gl_FragCoord.xy;
		FragColor = vec4(vec3(pow(texture(rteSkyLight, worldPos / rteGridWorldSize).g, 2.2)), 1.0);
		return;
	} else if (rteDebugView == 4) {
		FragColor = vec4(pow(texture(rteNormals, screenUV).rgb, vec3(2.2)), 1.0);
		return;
	}
	vec3 emissive = pow(texture(rteEmissive, screenUV).rgb, vec3(2.2)) * rteEmissiveIntensity;
	vec3 litColor = mix(albedoLinear * light, rteAtmosphereColor, haze) + waterGlow;
	if (waterReflectionAmount > 0.0 && rteDebugView == 0) {
		vec3 reflectedLight = waterReflectionBackground ? mix(rteBackgroundLight, mix(rteSkyDaylight, (rteSkyHorizon + rteSkyZenith) * 0.32, rteSkyRecolor), rteSkyOwnLight) : light;
		litColor = mix(litColor, waterReflection * reflectedLight + waterGlow, waterReflectionAmount);
	}
	if (rteSkyOwnLight > 0.0 && skyLayer > 0.0) {
		// The sky itself is as bright as the hour makes it. The sky light setting is for how much of it falls on the scene; turned down for moodier ground, it
		// used to turn the midday sky navy as well.
		litColor = mix(litColor, mix(albedoLinear * rteSkyDaylight, rteAtmosphereColor, haze), skyLayer * rteSkyOwnLight * (1.0 - rteSkyRecolor));
	}
	if (rteSkyRecolor > 0.0 && skyLayer > 0.0) {
		// The sky art is painted as a blue day. Darkening it only ever gives a dark blue day, so away from midday its colours are replaced by the sky of the hour:
		// a gradient from overhead to the horizon where the art is blue sky, and the hour's light on cloud where it is pale. The art's own light and shade is kept faintly.
		float brightness = dot(albedoLinear, vec3(0.2126, 0.7152, 0.0722));
		float strongest = max(max(albedoLinear.r, albedoLinear.g), albedoLinear.b);
		float weakest = min(min(albedoLinear.r, albedoLinear.g), albedoLinear.b);
		float saturation = strongest > 0.001 ? (strongest - weakest) / strongest : 0.0;
		float cloud = (1.0 - smoothstep(0.12, 0.38, saturation)) * smoothstep(0.2, 0.55, brightness);
		float down = clamp(screenUV.y / 0.8, 0.0, 1.0);
		vec3 gradient = mix(rteSkyZenith, rteSkyHorizon, pow(down, 1.6));
		gradient *= mix(1.0, clamp(brightness / 0.3, 0.7, 1.4), 0.25);
		vec3 sky = mix(gradient, rteSkyCloud * (0.35 + brightness), cloud);
		litColor = mix(litColor, sky, rteSkyRecolor * skyLayer);
	}
	if (rteSunDisc != vec3(0.0) && sceneDepth > rteBackgroundDepth) {
		// The sun shows on the sky itself: the furthest layers and where nothing is drawn, never on mountains or nearer scenery.
		float distance = clamp((sceneDepth - rteBackgroundNearDepth) / (rteBackgroundFarDepth - rteBackgroundNearDepth), 0.0, 1.0);
		litColor += SunDisc(gl_FragCoord.xy) * smoothstep(0.88, 0.95, distance);
	}
	if (nightSkyAmount > 0.0) {
		// Bright parts of the sky art (clouds, glowing horizons) hide the stars.
		float skyBrightness = dot(litColor, vec3(0.2126, 0.7152, 0.0722));
		litColor += NightSky(gl_FragCoord.xy) * nightSkyAmount * (1.0 - smoothstep(0.03, 0.12, skyBrightness));
	}
	// Highlights: white on most things, taking the surface's own color on metal (which is why gold glints gold and steel glints white).
	litColor += highlights * mix(vec3(1.0), albedoLinear * 2.5 + 0.15, metalness) * (1.0 - haze);
	if (rteFogStrength > 0.0 && rteDebugView == 0) {
		// Mist and dust in the air in front of whatever is here, lit as the air here is: by the sky where it reaches, the ambient where it doesn't, and the
		// lamps and fires around. Glows still shine through it.
		vec2 fogWorld = rteScreenOrigin + gl_FragCoord.xy;
		float fog = texture(rteFog, fogWorld / rteGridWorldSize).r;
		if (fog > 0.002) {
			float amount = (1.0 - exp(-fog * rteFogStrength * 2.0)) * 0.85;
			float fogSky = smoothstep(0.0, 1.0, texture(rteSkyLight, fogWorld / rteGridWorldSize).r);
			vec3 fogLamps = rteMaxDynamicLight * (1.0 - exp(-texture(rteDynamicLight, screenUV).rgb / rteMaxDynamicLight));
			litColor = mix(litColor, vec3(0.82, 0.85, 0.9) * (mix(rteAmbient, rteSkyColor, fogSky) + fogLamps), amount);
		}
	}
	vec3 result = litColor + emissive;
	FragColor = vec4(any(isnan(result)) || any(isinf(result)) ? vec3(0.0) : result, 1.0);
}
