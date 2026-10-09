// PointLight.frag
// Smooth radial falloff with soft shadows marched through the terrain occupancy grid, and shadows from solid objects (units, devices, doors, wreckage) traced through the map of distances to them.
// Additively blended into the dynamic light buffer, or, in cache mode, into the world lamp cache (LightingSettings::LampCache): there it writes the light
// arriving at each texel and, to location 1, which way it comes from weighted by its brightness, for LampCacheApply.frag to shade with.
#version 330 core

in vec2 localPos;
in vec4 lightColor;
in vec2 lightCenter;
in float lightRadius;
in vec2 screenPos;
in vec3 lightCone;

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 DirectionOut;

uniform sampler2D rteOccupancy; // World grid, R = terrain coverage 0..1, linearly filtered.
uniform vec2 rteScreenOrigin; // World position of the screen's top left pixel.
uniform vec2 rteGridWorldSize; // World size covered by the occupancy grid.
uniform float rteShadowStrength; // How much each solid sample blocks, 0..1.
uniform sampler2D rteNormals; // Player screen normals: RG = normal xy * 0.5 + 0.5, B = 1 - shininess, A > 0.25 where something was drawn.
uniform float rteSpecular; // Strength of highlights on shiny surfaces (metal, concrete, wet ground, water).
uniform bool rteUnitShine; // Highlights and brighter edges on units and other solid objects too (LightingSettings::UnitShineLights).
uniform vec2 rteScreenSize;
uniform float rteEdgeLighting;
uniform bool rteBeamMode; // Drawing the visible beam of cone lights over the lit scene, instead of light falling on surfaces.
uniform sampler2D rteOccluders; // Player screen: RG = position of the nearest pixel of a solid object.
uniform sampler2D rteSurface; // Player screen surface values, B = 1 where a solid object was drawn.
uniform float rteUnitShadows; // How dark the shadows of solid objects are, 0 (off) to 1.
uniform bool rteShadowFieldOn; // Trace terrain shadows through the distance field (LightingSettings::LightShadowField), instead of the fixed march.
uniform sampler2D rteShadowField; // World grid, R = distance to the nearest wall cell, as a fraction of rteShadowFieldReach, linearly filtered.
uniform float rteShadowFieldReach; // How far the field reaches, in pixels.
uniform bool rteSoftWallLight; // Feather the lit edge on walls by tracing from three points across the light.
uniform float rteShadowSoftness; // How soft terrain shadows' edges are, 0 sharp to 2.
uniform bool rteCacheMode; // Drawing into the world lamp cache: positions are in its texels, each rteCacheCell pixels across, from the world's corner.
uniform float rteCacheCell;

const int c_ShadowSteps = 12;

// Distance in pixels from a point of the player screen to the nearest solid object. The map only reaches about 60 pixels, so it's capped.
float OccluderDistance(vec2 position) {
	return min(distance(texture(rteOccluders, position / rteScreenSize).xy, position), 48.0);
}

// How much of the light gets past solid objects on its way to a pixel, 0 to 1. Steps along the line by the distance to the nearest object each time, so empty space is crossed quickly and thin things are still hit.
float ObjectShadow(vec2 from, vec2 to, bool fromSolid) {
	vec2 delta = to - from;
	float range = length(delta);
	// A light sits on or in whatever carries it (a headlamp, a muzzle, an engine): don't let the carrier's own skin block it.
	// Soft: the trim eases from 22 to 5 pixels as the light moves off the carrier, instead of jumping, so a lit band doesn't appear on objects (falling terrain) passing the light.
	float carried = OccluderDistance(to);
	float end = range - (rteSoftWallLight ? mix(22.0, 5.0, smoothstep(1.0, 8.0, carried)) : (carried < 1.5 ? 22.0 : 5.0));
	if (end <= 2.0) {
		return 1.0;
	}
	vec2 direction = delta / range;
	// Bigger lights are bigger sources, with softer shadows.
	float lightSize = clamp(lightRadius * 0.04, 4.0, 12.0);
	float t = 1.5;
	if (fromSolid) {
		// A pixel of an object: step out of the object first, so it never shadows itself. Bodies too thick to step out of stay lit.
		int steps = 0;
		for (; steps < 9; ++steps) {
			if (OccluderDistance(from + direction * t) > 1.0) {
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
	for (int i = 0; i < 20 && t < end; ++i) {
		vec2 position = from + direction * t;
		if (position.x < 0.0 || position.y < 0.0 || position.x >= rteScreenSize.x || position.y >= rteScreenSize.y) {
			break;
		}
		float clearance = OccluderDistance(position);
		if (clearance < 0.8) {
			return 0.0;
		}
		// The light isn't a point: it has a size, so a pixel sees it as a small disc, and an object that only just clears the line still hides part of it.
		// The width of that soft edge grows from nothing at the object to the light's own size far behind it, so shadows are sharp at the feet and softer further away.
		visibility = min(visibility, clearance * range / (lightSize * (t + 1.0)));
		t += max(clearance * 0.95, 1.0);
	}
	return clamp(visibility, 0.0, 1.0);
}

// How much light gets past the terrain from one world position to another, 0 to 1, the old way: eleven evenly spaced samples of the light grid.
// Skips the ends so lit terrain surfaces and lights embedded in terrain still work.
float TerrainShadowMarch(vec2 fromWorld, vec2 toWorld) {
	float transmittance = 1.0;
	for (int i = 1; i < c_ShadowSteps; ++i) {
		float t = (float(i) / float(c_ShadowSteps)) * 0.9 + 0.05;
		vec2 sampleWorld = mix(fromWorld, toWorld, t);
		// Lights sitting on or in the ground (flares, lamps on walls) shouldn't be shadowed by the terrain right around them.
		if (distance(sampleWorld, toWorld) < 8.0) {
			continue;
		}
		float occupancy = texture(rteOccupancy, sampleWorld / rteGridWorldSize).r;
		transmittance *= 1.0 - occupancy * rteShadowStrength;
	}
	return transmittance;
}

// The same, traced through the terrain distance field: each step goes as far as the nearest wall allows, so open air is crossed in a few steps and walls,
// even a cell thin, are never stepped over. Walls block outright, with a soft edge that widens away from them; lighter cells (water, glass, a wall's
// fringe) dim the light by how much of the way they fill.
float TerrainShadowTraced(vec2 fromWorld, vec2 toWorld) {
	vec2 delta = toWorld - fromWorld;
	float range = length(delta);
	if (range < 2.0) {
		return 1.0;
	}
	vec2 direction = delta / range;
	float cell = rteGridWorldSize.x / float(textureSize(rteShadowField, 0).x);
	float lightSize = clamp(lightRadius * 0.04, 4.0, 12.0) * rteShadowSoftness;
	float t = 0.05 * range;
	// Lights sitting on or in the ground (flares, lamps on walls) aren't shadowed by the terrain right around them.
	float end = range - max(0.05 * range, 8.0);
	// A lit terrain surface sits in or beside its own wall: step out of it first, so it doesn't shadow itself. Two cells at most; past that the pixel
	// is facing away through rock and the trace goes on through it.
	float leave = t + 2.0 * cell;
	while (t < leave && t < end && texture(rteShadowField, (fromWorld + direction * t) / rteGridWorldSize).r * rteShadowFieldReach < cell) {
		t += 0.5 * cell;
	}
	// Likewise a lamp set into a wall shines out of it rather than being buried: the trace stops short of the wall around the light, two cells at most.
	float stop = end - 2.0 * cell;
	while (end > stop && end > t && texture(rteShadowField, (fromWorld + direction * end) / rteGridWorldSize).r * rteShadowFieldReach < cell) {
		end -= 0.5 * cell;
	}
	float start = t;
	float transmittance = 1.0;
	float visibility = 1.0;
	for (int i = 0; i < 28 && t < end; ++i) {
		vec2 position = (fromWorld + direction * t) / rteGridWorldSize;
		// Distances are stored for cell centres; half a cell off finds the wall's edge.
		float clearance = texture(rteShadowField, position).r * rteShadowFieldReach - 0.5 * cell;
		float advance = clamp(clearance, 0.5 * cell, end - t + 0.5 * cell);
		transmittance *= pow(max(1.0 - texture(rteOccupancy, position).r * rteShadowStrength, 0.0), advance / (1.5 * cell));
		// The light has a size, so a wall that only just clears the line still hides part of it, more the further the pixel is behind the wall.
		visibility = min(visibility, lightSize > 0.0 ? max(clearance, 0.0) * range / (lightSize * (t - start + 1.0)) : (clearance > 0.0 ? 1.0 : 0.0));
		if (transmittance < 0.01) {
			break;
		}
		t += advance;
	}
	return transmittance * mix(1.0, clamp(visibility, 0.0, 1.0), rteShadowStrength);
}

// The trace, feathered when asked: a light that touches a wall gets a hard lit/shadowed edge there, since each pixel's trace is trimmed to the wall by whole steps.
// Tracing to three points across the light (and averaging) turns that edge into a short fade.
float TerrainShadowSoft(vec2 fromWorld, vec2 toWorld) {
	if (!rteSoftWallLight) {
		return TerrainShadowTraced(fromWorld, toWorld);
	}
	vec2 delta = toWorld - fromWorld;
	float range = length(delta);
	if (range < 2.0) {
		return 1.0;
	}
	vec2 across = vec2(-delta.y, delta.x) / range * clamp(lightRadius * 0.04, 4.0, 12.0) * 0.6;
	return 0.5 * TerrainShadowTraced(fromWorld, toWorld) + 0.25 * (TerrainShadowTraced(fromWorld, toWorld + across) + TerrainShadowTraced(fromWorld, toWorld - across));
}

void main() {
	if (rteBeamMode && lightCone.z < -1.5) {
		discard;
	}
	float distanceSq = dot(localPos, localPos);
	if (distanceSq >= 1.0) {
		discard;
	}
	// Smooth falloff that reaches exactly zero at the radius, so lights never show a hard edge.
	float falloff = (1.0 - distanceSq);
	falloff *= falloff;
	if (lightCone.z > -1.5) {
		// Flashlight: a soft edged cone, with a little spill right around the lamp.
		vec2 toPixel = gl_FragCoord.xy - lightCenter;
		float along = dot(normalize(toPixel + vec2(0.0001)), lightCone.xy);
		float cone = smoothstep(lightCone.z, mix(lightCone.z, 1.0, 0.35), along);
		float spill = 0.12 * (1.0 - smoothstep(0.0, 0.12, sqrt(distanceSq)));
		falloff *= max(cone, spill);
		if (falloff <= 0.0005) {
			discard;
		}
	}

	// Soft shadow from the terrain between this pixel and the light.
	float cell = rteCacheMode ? rteCacheCell : 1.0;
	vec2 fromWorld = rteScreenOrigin + gl_FragCoord.xy * cell;
	vec2 toWorld = rteScreenOrigin + lightCenter * cell;
	float transmittance = rteShadowFieldOn ? TerrainShadowSoft(fromWorld, toWorld) : TerrainShadowMarch(fromWorld, toWorld);

	if (rteCacheMode) {
		vec3 arriving = lightColor.rgb * falloff * transmittance;
		vec3 toLight = normalize(vec3((lightCenter - gl_FragCoord.xy) * cell, lightRadius * 0.25));
		FragColor = vec4(arriving, 0.0);
		DirectionOut = vec4(toLight.xy * dot(arriving, vec3(0.2126, 0.7152, 0.0722)), 0.0, 0.0);
		return;
	}

	if (rteUnitShadows > 0.0) {
		bool fromSolid = !rteBeamMode && texture(rteSurface, gl_FragCoord.xy / rteScreenSize).b > 0.5;
		transmittance *= mix(1.0, ObjectShadow(gl_FragCoord.xy, lightCenter, fromSolid), rteUnitShadows);
	}

	if (rteBeamMode) {
		// A faint haze along the beam, brightest near the lamp.
		FragColor = vec4(lightColor.rgb * vec3(1.0, 0.92, 0.78) * falloff * transmittance * 0.035, 1.0);
		return;
	}

	// Edges facing the light catch more of it, edges facing away get less. Normalized so flat surfaces are lit exactly as without normals.
	float shading = 1.0;
	float highlight = 0.0;
	vec4 normalSample = texture(rteNormals, gl_FragCoord.xy / rteScreenSize);
	if (normalSample.a > 0.25) {
		vec2 normalXY = normalSample.xy * 2.0 - 1.0;
		vec3 normal = vec3(normalXY, sqrt(max(1.0 - dot(normalXY, normalXY), 0.0)));
		vec3 toLight = normalize(vec3(lightCenter - gl_FragCoord.xy, lightRadius * 0.25));
		shading = mix(1.0, clamp(dot(normal, toLight) / max(toLight.z, 0.05), 0.0, 2.5), rteEdgeLighting);
		// Units and other solid objects, unless asked: edges facing the light don't catch more than a flat surface would and there are no highlights,
		// so a light right by one (its own headlamp) doesn't wash its sprite out. Edges facing away still darken.
		bool keepArt = !rteUnitShine && texture(rteSurface, gl_FragCoord.xy / rteScreenSize).b > 0.5;
		if (keepArt) {
			shading = min(shading, 1.0);
		}
		// Shiny surfaces throw the light back at the viewer where it strikes them squarely: a hot spot near the light, and glints on edges and relief turned towards it.
		// The glossier the surface the tighter the highlight, and metal throws back more of the light.
		float shine = 1.0 - normalSample.b;
		if (shine > 0.02 && rteSpecular > 0.0 && !keepArt) {
			float metalness = texture(rteSurface, gl_FragCoord.xy / rteScreenSize).r;
			vec3 halfway = normalize(toLight + vec3(0.0, 0.0, 1.0));
			highlight = pow(max(dot(normal, halfway), 0.0), mix(18.0, 64.0, shine)) * shine * rteSpecular * mix(1.6, 3.2, metalness);
		}
	}

	// Alpha gathers the highlights' brightness (the blend adds it up like the color): the composite adds them on top of the lit surface instead of multiplying them by its color.
	float brightness = dot(lightColor.rgb, vec3(0.2126, 0.7152, 0.0722));
	FragColor = vec4(lightColor.rgb * falloff * transmittance * shading, highlight * brightness * falloff * transmittance);
}
