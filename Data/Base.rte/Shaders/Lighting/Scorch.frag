// Scorch.frag
// Stamps a ragged soot mark into the world scorch map. Uses PointLight.vert with the scorch map as the "screen": lightColor.r is the darkness.
#version 330 core

in vec2 localPos;
in vec4 lightColor;
in vec2 lightCenter;
in float lightRadius;
in vec2 screenPos;

out vec4 FragColor;

uniform sampler2D rteDecalGround; // R: 1 where a cell of the map holds ground (see SceneLighting::RefreshDecalGround).
uniform vec2 rteDecalGroundSize; // In cells.

// Whether there's ground in this cell or beside it, which is all the terrain shows of the map. Marks are only kept there, so none is left hanging in the air
// to show on whatever lands there later.
bool NearGround() {
	for (int y = -1; y <= 1; ++y) {
		for (int x = -1; x <= 1; ++x) {
			if (texture(rteDecalGround, (gl_FragCoord.xy + vec2(x, y)) / rteDecalGroundSize).r > 0.5) {
				return true;
			}
		}
	}
	return false;
}

float Hash(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float Noise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(Hash(i), Hash(i + vec2(1.0, 0.0)), u.x), mix(Hash(i + vec2(0.0, 1.0)), Hash(i + vec2(1.0, 1.0)), u.x), u.y);
}

void main() {
	if (!NearGround()) {
		discard;
	}
	float distance = length(localPos);
	// Ragged edge: wobble the radius with noise around the mark.
	float angle = atan(localPos.y, localPos.x);
	vec2 seed = lightCenter * 0.137;
	float raggedRadius = 0.7 + 0.3 * Noise(vec2(angle * 2.5, 0.0) + seed);
	float mask = 1.0 - smoothstep(raggedRadius * 0.5, raggedRadius, distance);
	// Speckle so it reads as soot rather than a flat disc.
	float speckle = 0.7 + 0.3 * Noise(gl_FragCoord.xy * 0.9 + seed);
	float darkness = lightColor.r * mask * speckle;
	if (darkness <= 0.002) {
		discard;
	}
	FragColor = vec4(darkness, 0.0, 0.0, 1.0);
}
