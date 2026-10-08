// RainSplash.frag
// Raindrops hitting things: on any surface the rain can reach (ground, water, roofs, units), little splashes of two droplets jumping up and apart, and a short ripple.
// Drawn in the air just above the surface. Visual only: nothing here touches the simulation.
#version 330 core

out vec4 FragColor;

uniform sampler2D rteSceneDepth; // The player screen's depth buffer.
uniform float rteForegroundDepth; // Depth beyond which pixels are behind the foreground terrain and objects.
uniform sampler2D rteOccupancy; // The world's grid of solid ground: A = how full each cell is (R is how much it stops light, which water barely does).
uniform vec2 rteGridWorldSize;
uniform float rteCellSize; // World pixels per grid cell.
uniform sampler2D rteDynamicLight; // Screen space dynamic light.
uniform vec2 rteScreenSize;
uniform vec2 rteScreenOrigin; // World position of the screen's top left pixel.
uniform float rteTime;
uniform vec2 rteFall; // Which way the rain is falling, a unit vector (y down).
uniform bool rteShelterOn; // Shelter from the weather's shelter map (SunShadowMap.frag made for the weather) rather than marching the grid.
uniform sampler2D rteShelterMap; // 1 row: for each line the weather falls down, the scene y of the first solid point on it.
uniform float rteShelterSlope; // How far a line moves in x per pixel down, as the map was made.
uniform float rteShelterStart; // Where the map's first line crosses the top of the scene.
uniform float rteShelterTexel; // Scene pixels between its lines.
uniform float rteShelterSoftness; // How far splashes' lines are spread sideways to soften the edge of a shelter, in pixels either way.
uniform float rteAmount; // 0..1, how many of the places a splash can be have one at a time.
uniform vec3 rteSkyLight;
uniform float rteOwnLight;
uniform vec3 rteSplashColor; // Linear (Weather::SplashColor).
uniform float rteSplashGlow; // Light a splash gives off itself (Weather::SplashGlow).
uniform bool rteSheltered; // The weather is kept out from under roofs (Weather::Shelter). Off: it splashes everywhere it lands.

float Hash(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

bool Foreground(vec2 fragment) {
	return texture(rteSceneDepth, fragment / rteScreenSize).r < rteForegroundDepth;
}

// The scene y of the first solid point on the line the weather falls down through a point, from the shelter map; offset moves the line sideways.
float ShelterFirst(vec2 at, float offset) {
	float line = (at.x + rteShelterSlope * at.y + offset - rteShelterStart) / rteShelterTexel;
	return textureLod(rteShelterMap, vec2(line / float(textureSize(rteShelterMap, 0).x), 0.5), 0.0).r;
}

// Whether rain reaches a point: back along the way it falls until out of the top of the world, or blocked by ground.
float Reaches(vec2 at, vec2 direction) {
	vec2 back = -direction * rteCellSize;
	vec2 p = at + back * 1.2;
	for (int i = 0; i < 176; ++i) {
		if (p.y < 0.0) {
			return 1.0;
		}
		if (textureLod(rteOccupancy, p / rteGridWorldSize, 0.0).a > 0.55) {
			return 0.0;
		}
		p += back * (i < 64 ? 1.0 : (i < 128 ? 2.0 : 4.0));
	}
	return 1.0;
}

void main() {
	vec2 fragment = gl_FragCoord.xy;
	if (Foreground(fragment)) {
		discard;
	}
	// How far below this pixel the surface is, if it is within a splash's height.
	float above = 0.0;
	for (int k = 1; k <= 5; ++k) {
		if (Foreground(fragment + vec2(0.0, float(k)))) {
			above = float(k);
			break;
		}
	}
	if (above == 0.0) {
		discard;
	}
	vec2 world = rteScreenOrigin + fragment;
	float band = floor((world.y + above) / 24.0);
	float alpha = 0.0;
	// Along the surface there is a place a splash can be every six pixels. Each has its own rhythm, and on each beat may or may not have one.
	for (int n = -1; n <= 1; ++n) {
		float place = floor(world.x / 6.0) + float(n);
		float beat = rteTime * (2.2 + 1.5 * Hash(vec2(place, band))) + 10.0 * Hash(vec2(place + 0.5, band));
		float which = floor(beat);
		float life = fract(beat) / 0.42;
		if (life > 1.0 || Hash(vec2(place + which * 0.37, band + which)) > rteAmount) {
			continue;
		}
		float centre = (place + 0.2 + 0.6 * Hash(vec2(place - which, band))) * 6.0;
		float across = world.x - centre;
		// Two droplets jump up and apart, and fall back; on the surface itself a ripple widens and fades.
		float height = 1.0 + 3.6 * sin(3.14159 * life);
		float apart = 1.0 + 4.5 * life;
		float droplet = step(abs(abs(across) - apart), 0.8) * step(abs(above - height), 0.8) * (1.0 - 0.4 * life);
		// A second, lower pair close in, and at the first instant a little spike straight up where the drop struck.
		float inner = step(abs(abs(across) - apart * 0.45), 0.7) * step(abs(above - (1.0 + 0.5 * (height - 1.0))), 0.7) * (0.8 - 0.5 * life);
		float spike = step(abs(across), 0.7) * step(above, 1.0 + 2.5 * (1.0 - life * 2.5)) * step(life, 0.4);
		float ripple = (above == 1.0 ? 1.0 : 0.0) * step(abs(across), apart + 0.5) * (1.0 - life) * 0.8;
		alpha = max(alpha, max(max(droplet, inner), max(spike, ripple)));
	}
	if (alpha <= 0.0) {
		discard;
	}
	if (!rteSheltered) {
		// Weather that gets everywhere splashes everywhere.
	} else if (rteShelterOn) {
		// Each place a splash can be has its own line, nudged a little so the edge of a shelter is soft. Ground within a cell or so above doesn't count, as with the march.
		float nudge = (Hash(vec2(floor(world.x / 6.0), band + 0.5)) - 0.5) * 2.0 * rteShelterSoftness;
		if (world.y - 1.2 * rteCellSize * rteFall.y > ShelterFirst(world, nudge)) {
			discard;
		}
	} else if (Reaches(world, rteFall) < 0.5) {
		discard;
	}
	vec3 light = max(rteSkyLight, vec3(rteOwnLight)) + texture(rteDynamicLight, fragment / rteScreenSize).rgb * 1.5;
	// A touch brighter than the rain itself: a splash catches the light.
	FragColor = vec4(rteSplashColor * (light * 1.25 + 0.08 + rteSplashGlow), clamp(alpha, 0.0, 1.0));
}
