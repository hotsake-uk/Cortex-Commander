// SmokeScatter.frag
// Light scattered by smoke: wherever there's smoke, the light passing through it (fire, muzzle flashes, lamps, bounced light) lights the smoke up,
// so glows bleed into the smoke around them. Added onto the lit HDR scene.
#version 330 core

out vec4 FragColor;

uniform sampler2D rteDensity; // Half resolution smoke density, linear filtered.
uniform sampler2D rteDynamicLight; // Screen space dynamic light.
uniform sampler2D rteGI; // Radiance cascades light, quarter resolution.
uniform float rteGIStrength;
uniform vec2 rteScreenSize;
uniform float rteStrength;
uniform vec3 rteSmokeColor; // Linear tint of scattered light.

void main() {
	vec2 uv = gl_FragCoord.xy / rteScreenSize;
	float density = texture(rteDensity, uv).r;
	if (density <= 0.002) {
		discard;
	}
	// Smoke this thick is opaque to the eye; scattering saturates rather than growing without bound.
	float coverage = 1.0 - exp(-density * 1.5);
	vec3 light = texture(rteDynamicLight, uv).rgb;
	if (rteGIStrength > 0.0) {
		light += texture(rteGI, uv).rgb * rteGIStrength;
	}
	FragColor = vec4(min(light, vec3(4.0)) * rteSmokeColor * coverage * rteStrength, 1.0);
}
