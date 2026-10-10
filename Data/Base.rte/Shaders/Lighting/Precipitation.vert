// Precipitation.vert
// Procedural weather drops: every 6 vertices make one drop's quad, positioned from a hash of its index. No vertex buffers.
// Drops live in a world space field that repeats every screen-sized cell, so they stay put relative to the world as the camera moves.
// How they move is the weather's (Weather.h, a Weather preset such as Base.rte/Weather/Weather.ini's Rain): a mod's DropShader uses this vertex shader with its own fragment shader.
#version 330 core

out vec2 quadPos; // 0..1 across and along the drop.
out vec2 worldPos;
out float dropAlpha;
flat out float reaches; // 1 if this drop can get to where it is, 0 if something is in the way upwind.
flat out vec3 dropSeeds; // Three random numbers 0..1 of this drop's own, for the fragment shader's colour, pulse and twinkle.

uniform vec2 rteScreenSize;
uniform vec2 rteScreenOrigin; // World position of the screen's top left pixel.
uniform float rteTime;
uniform int rteType; // The weather's slot (0 clear, 1 rain, 2 snow, 3 ash fall, 4 dust storm, 5 on others), for a mod's shader to tell them apart.
uniform float rteWind; // Horizontal speed, pixels per second: the weather's steady wind.
uniform float rteWindDrift; // How far the natural wind's gusts and shifts have carried things beyond the steady wind, pixels.
uniform float rteWindNow; // The wind as it blows now, gusts and all, pixels per second.
uniform vec2 rteFallSpeed; // Down, pixels per second, min and max: each drop's is between them. Negative rises.
uniform float rteWindFactor; // How much of the wind the drops take.
uniform vec3 rteSway; // Side to side drift: pixels, and how fast, radians per second, min and max.
uniform bool rteBlown; // Blown nearly level (a dust storm).
uniform vec4 rteBlownWind; // Blown: the wind's scale, the least speed, and each drop's speed against that, min and max. Down at rteFallSpeed.
uniform vec2 rteSwirl; // Circling around the path: pixels, radians per second.
uniform vec2 rteJitter; // Sudden sideways jumps: pixels, jumps per second.
uniform vec2 rteLength; // Along the way it moves, pixels, min and max.
uniform float rteWidth; // Across, pixels.
uniform vec2 rteAlpha; // How solid, min and max.
uniform bool rteSheltered; // Kept out from under roofs and overhangs. Off: everywhere but inside the ground.
uniform sampler2D rteOccupancy; // The world's grid of solid ground: A = how full each cell is (R is how much it stops light, which water barely does).
uniform vec2 rteGridWorldSize;
uniform float rteCellSize; // World pixels per grid cell.
uniform bool rteShelterOn; // Shelter from the weather's shelter map (SunShadowMap.frag made for the weather) rather than marching the grid.
uniform sampler2D rteShelterMap; // 1 row: for each line the weather falls down, the scene y of the first solid point on it.
uniform float rteShelterSlope; // How far a line moves in x per pixel down, as the map was made.
uniform float rteShelterStart; // Where the map's first line crosses the top of the scene.
uniform float rteShelterTexel; // Scene pixels between its lines.
uniform float rteShelterSoftness; // How far drops' lines are spread sideways to soften the edge of a shelter, in pixels either way.

float Hash(float n) {
	return fract(sin(n * 12.9898) * 43758.5453);
}

// The scene y of the first solid point on the line the weather falls down through a point, from the shelter map; offset moves the line sideways.
float ShelterFirst(vec2 at, float offset) {
	float line = (at.x + rteShelterSlope * at.y + offset - rteShelterStart) / rteShelterTexel;
	return textureLod(rteShelterMap, vec2(line / float(textureSize(rteShelterMap, 0).x), 0.5), 0.0).r;
}

// Whether weather coming down a line reaches a point: follows the line back the way the weather came, through the world's grid of solid ground, until it is
// out of the top of the world (it reaches) or meets ground (it is sheltered). Fine steps near the point, coarser further off.
float Reaches(vec2 at, vec2 direction, float jitter) {
	vec2 back = -direction * rteCellSize;
	vec2 p = at + back * (0.8 + jitter);
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
	int drop = gl_VertexID / 6;
	int corner = gl_VertexID % 6;
	vec2 cornerPos = vec2(corner == 1 || corner == 2 || corner == 4 ? 1.0 : 0.0, corner == 2 || corner == 4 || corner == 5 ? 1.0 : 0.0);

	float seedA = Hash(float(drop) + 0.37);
	float seedB = Hash(float(drop) * 1.731 + 4.1);
	float seedC = Hash(float(drop) * 0.913 + 9.7);

	float fallSpeed = mix(rteFallSpeed.x, rteFallSpeed.y, seedC);
	vec2 velocity = vec2(rteWind * rteWindFactor, fallSpeed);
	if (rteBlown) {
		// Blown nearly level, at least at a stiff breeze whatever the wind setting.
		float gale = (rteWind < 0.0 ? -1.0 : 1.0) * max(abs(rteWind) * rteBlownWind.x, rteBlownWind.y);
		velocity = vec2(gale * mix(rteBlownWind.z, rteBlownWind.w, seedC), mix(rteFallSpeed.x, rteFallSpeed.y, seedA));
	}

	// A repeating field slightly larger than the screen.
	vec2 fieldSize = rteScreenSize + vec2(64.0);
	vec2 fieldPos = vec2(seedA, seedB) * fieldSize + velocity * rteTime;
	if (!rteBlown) {
		// Gusts carry the drops along further and lean them over more, without making them jump.
		fieldPos.x += rteWindDrift * rteWindFactor;
		velocity.x += (rteWindNow - rteWind) * rteWindFactor;
	}
	if (rteSway.x != 0.0) {
		fieldPos.x += sin(rteTime * mix(rteSway.y, rteSway.z, seedC) + seedA * 30.0) * rteSway.x;
	}
	if (rteSwirl.x != 0.0) {
		float turn = rteTime * rteSwirl.y * mix(0.7, 1.3, seedC) + seedB * 6.2832;
		fieldPos += vec2(cos(turn), sin(turn)) * rteSwirl.x;
	}
	if (rteJitter.x != 0.0) {
		// Now and then it jumps to a new place sideways, and stays there until the next jump.
		float beat = floor(rteTime * rteJitter.y + seedC * 7.0);
		fieldPos.x += (Hash(beat * 1.37 + float(drop) * 0.173) - 0.5) * 2.0 * rteJitter.x;
	}
	vec2 relative = mod(fieldPos - rteScreenOrigin, fieldSize) - vec2(32.0);
	vec2 head = rteScreenOrigin + relative;

	vec2 direction = normalize(velocity);
	vec2 side = vec2(-direction.y, direction.x);
	float length = mix(rteLength.x, rteLength.y, seedC);
	float width = rteWidth;
	vec2 position = head - direction * length * (1.0 - cornerPos.y) + side * width * (cornerPos.x - 0.5);

	// Shelter is worked out for the drop as a whole, along the line it is falling down: rain driven by wind gets in under an overhang on the windward side
	// and leaves a dry strip beyond a wall on the lee side. Each drop's line is nudged a little so the edge of the shelter is soft, not ruled.
	if (textureLod(rteOccupancy, head / rteGridWorldSize, 0.0).a > 0.9) {
		reaches = 0.0;
	} else if (!rteSheltered || velocity.y <= 0.0) {
		// Weather that goes everywhere, and drops that rise, which nothing above shelters.
		reaches = 1.0;
	} else if (rteShelterOn) {
		// The drop reaches where it is if the first solid point on its line is below it. Ground within a cell or so above it doesn't count, as with the march, which starts
		// that far back so that the ground a drop is about to land on, blurred over its grid cell, doesn't shelter it.
		float lead = (0.8 + seedA) * rteCellSize * direction.y;
		float nudge = (seedB - 0.5) * 2.0 * rteShelterSoftness;
		float first = ShelterFirst(head, nudge);
		if (head.y - lead > first) {
			// Sheltered along the map's line, which is made for the weather's average fall. This drop falls at its own speed, so at the height of what sheltered
			// it its own line has drifted sideways from the map's: look again from there, so slow snow and ash in a wind get in under an edge as far as they really do.
			float ownSlope = -velocity.x / velocity.y;
			first = ShelterFirst(head, nudge + (ownSlope - rteShelterSlope) * (head.y - first));
		}
		reaches = head.y - lead <= first ? 1.0 : 0.0;
	} else {
		reaches = Reaches(head, normalize(velocity + vec2((seedB - 0.5) * 60.0, 0.0)), seedA);
	}
	worldPos = position;
	quadPos = cornerPos;
	dropAlpha = mix(rteAlpha.x, rteAlpha.y, seedA);
	dropSeeds = vec3(seedA, seedB, seedC);
	vec2 screenPos = position - rteScreenOrigin;
	gl_Position = vec4((screenPos / rteScreenSize) * 2.0 - 1.0, 0.0, 1.0);
}
