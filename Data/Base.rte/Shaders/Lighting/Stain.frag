// Stain.frag
// Stamps a splat of blood, oil or water into the world stain map. Uses PointLight.vert with the stain map as the "screen":
// lightColor.rgb is the stain color, lightColor.a its opacity.
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

void main() {
	if (!NearGround()) {
		discard;
	}
	float distance = length(localPos);
	// An irregular splat: a soft core plus a few droplets around it.
	vec2 seed = lightCenter * 0.173;
	float wobble = 0.75 + 0.25 * Hash(floor(gl_FragCoord.xy) + seed);
	float mask = 1.0 - smoothstep(0.45 * wobble, 0.9 * wobble, distance);
	if (mask <= 0.01) {
		discard;
	}
	FragColor = vec4(lightColor.rgb, lightColor.a * mask);
}
