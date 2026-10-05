// Precipitation.vert
// Procedural rain/snow: every 6 vertices make one drop's quad, positioned from a hash of its index. No vertex buffers.
// Drops live in a world space field that repeats every screen-sized cell, so they stay put relative to the world as the camera moves.
#version 330 core

out vec2 quadPos; // 0..1 across and along the drop.
out vec2 worldPos;
out float dropAlpha;
flat out float reaches; // 1 if this drop can get to where it is, 0 if something is in the way upwind.

uniform vec2 rteScreenSize;
uniform vec2 rteScreenOrigin; // World position of the screen's top left pixel.
uniform float rteTime;
uniform int rteType; // 1 rain, 2 snow, 3 ash fall, 4 dust storm.
uniform float rteWind; // Horizontal speed, pixels per second.
uniform sampler2D rteOccupancy; // The world's grid of solid ground: R = how solid each cell is.
uniform vec2 rteGridWorldSize;
uniform float rteCellSize; // World pixels per grid cell.

float Hash(float n) {
	return fract(sin(n * 12.9898) * 43758.5453);
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
		if (textureLod(rteOccupancy, p / rteGridWorldSize, 0.0).r > 0.55) {
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

	// Snow and ash are flakes that drift down; rain and dust are streaks.
	bool snow = rteType == 2 || rteType == 3;
	bool dust = rteType == 4;
	float fallSpeed = rteType == 3 ? mix(14.0, 34.0, seedC) : (snow ? mix(30.0, 60.0, seedC) : mix(520.0, 760.0, seedC));
	vec2 velocity = vec2(rteWind * (snow ? 0.6 : 1.0), fallSpeed);
	if (dust) {
		// A dust storm blows nearly level, at least at a stiff breeze whatever the wind setting.
		float gale = (rteWind < 0.0 ? -1.0 : 1.0) * max(abs(rteWind) * 2.5, 260.0);
		velocity = vec2(gale * mix(0.7, 1.3, seedC), mix(10.0, 60.0, seedA));
	}

	// A repeating field slightly larger than the screen.
	vec2 fieldSize = rteScreenSize + vec2(64.0);
	vec2 fieldPos = vec2(seedA, seedB) * fieldSize + velocity * rteTime;
	if (snow) {
		fieldPos.x += sin(rteTime * mix(0.6, 1.4, seedC) + seedA * 30.0) * 12.0;
	}
	vec2 relative = mod(fieldPos - rteScreenOrigin, fieldSize) - vec2(32.0);
	vec2 head = rteScreenOrigin + relative;

	vec2 direction = normalize(velocity);
	vec2 side = vec2(-direction.y, direction.x);
	float length = rteType == 3 ? 3.0 : snow ? 2.0 : (dust ? mix(5.0, 11.0, seedC) : mix(7.0, 13.0, seedC));
	float width = rteType == 3 ? 3.0 : snow ? 2.0 : (dust ? 1.5 : 1.0);
	vec2 position = head - direction * length * (1.0 - cornerPos.y) + side * width * (cornerPos.x - 0.5);

	// Shelter is worked out for the drop as a whole, along the line it is falling down: rain driven by wind gets in under an overhang on the windward side
	// and leaves a dry strip beyond a wall on the lee side. Each drop's line is nudged a little so the edge of the shelter is soft, not ruled.
	reaches = textureLod(rteOccupancy, head / rteGridWorldSize, 0.0).r > 0.9 ? 0.0 : Reaches(head, normalize(velocity + vec2((seedB - 0.5) * 60.0, 0.0)), seedA);
	worldPos = position;
	quadPos = cornerPos;
	dropAlpha = rteType == 3 ? mix(0.75, 1.0, seedA) : snow ? mix(0.55, 0.9, seedA) : (dust ? mix(0.15, 0.4, seedA) : mix(0.25, 0.5, seedA));
	vec2 screenPos = position - rteScreenOrigin;
	gl_Position = vec4((screenPos / rteScreenSize) * 2.0 - 1.0, 0.0, 1.0);
}
