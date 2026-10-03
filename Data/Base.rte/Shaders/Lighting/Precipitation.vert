// Precipitation.vert
// Procedural rain/snow: every 6 vertices make one drop's quad, positioned from a hash of its index. No vertex buffers.
// Drops live in a world space field that repeats every screen-sized cell, so they stay put relative to the world as the camera moves.
#version 330 core

out vec2 quadPos; // 0..1 across and along the drop.
out vec2 worldPos;
out float dropAlpha;

uniform vec2 rteScreenSize;
uniform vec2 rteScreenOrigin; // World position of the screen's top left pixel.
uniform float rteTime;
uniform int rteType; // 1 rain, 2 snow.
uniform float rteWind; // Horizontal speed, pixels per second.

float Hash(float n) {
	return fract(sin(n * 12.9898) * 43758.5453);
}

void main() {
	int drop = gl_VertexID / 6;
	int corner = gl_VertexID % 6;
	vec2 cornerPos = vec2(corner == 1 || corner == 2 || corner == 4 ? 1.0 : 0.0, corner == 2 || corner == 4 || corner == 5 ? 1.0 : 0.0);

	float seedA = Hash(float(drop) + 0.37);
	float seedB = Hash(float(drop) * 1.731 + 4.1);
	float seedC = Hash(float(drop) * 0.913 + 9.7);

	bool snow = rteType == 2;
	float fallSpeed = snow ? mix(30.0, 60.0, seedC) : mix(520.0, 760.0, seedC);
	vec2 velocity = vec2(rteWind * (snow ? 0.6 : 1.0), fallSpeed);

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
	float length = snow ? 2.0 : mix(7.0, 13.0, seedC);
	float width = snow ? 2.0 : 1.0;
	vec2 position = head - direction * length * (1.0 - cornerPos.y) + side * width * (cornerPos.x - 0.5);

	worldPos = position;
	quadPos = cornerPos;
	dropAlpha = snow ? mix(0.55, 0.9, seedA) : mix(0.25, 0.5, seedA);
	vec2 screenPos = position - rteScreenOrigin;
	gl_Position = vec4((screenPos / rteScreenSize) * 2.0 - 1.0, 0.0, 1.0);
}
