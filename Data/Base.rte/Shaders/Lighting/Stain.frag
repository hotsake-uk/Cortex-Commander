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

float Hash(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
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
