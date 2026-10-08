// SmokeScatter.frag
// Light scattered by smoke: wherever there's smoke, the light passing through it (fire, muzzle flashes, lamps, bounced light) lights the smoke up,
// so glows bleed into the smoke around them. Added onto the lit HDR scene.
// With shading (LightingSettings::SmokeShading) the smoke also takes its own colour, shadows itself (it is lit on the side towards the brightest light
// and the sun, and dark on the far side, which then lets less of the scene behind show through), and the sun paints its top.
#version 330 core

out vec4 FragColor;

uniform sampler2D rteDensity; // Half resolution smoke: R the density; with shading RGB its colour times its density and A the density. Linear filtered.
uniform sampler2D rteDynamicLight; // Screen space dynamic light.
uniform sampler2D rteGI; // Radiance cascades light, quarter resolution.
uniform float rteGIStrength;
uniform vec2 rteScreenSize;
uniform float rteStrength;
uniform vec3 rteSmokeColor; // Linear tint of scattered light.
uniform bool rteShading;
uniform float rteShadingStrength; // 0 to 1.
uniform sampler2D rteSkyLight; // World grid, G = how much of the sun is visible.
uniform vec2 rteScreenOrigin; // World position of the screen's top left pixel.
uniform vec2 rteGridWorldSize;
uniform vec2 rteSunDirection; // Towards the sun, in screen pixels (y down, as the screen is drawn).
uniform vec3 rteSunLight; // The sun's light on the scene, linear, 0 when it's down or there are no sun shadows.

float Luminance(vec3 color) {
	return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
	vec2 uv = gl_FragCoord.xy / rteScreenSize;
	vec4 smoke = texture(rteDensity, uv);
	float density = rteShading ? smoke.a : smoke.r;
	if (density <= 0.002) {
		discard;
	}
	// Smoke this thick is opaque to the eye; scattering saturates rather than growing without bound.
	float coverage = 1.0 - exp(-density * 1.5);
	vec3 light = texture(rteDynamicLight, uv).rgb;
	if (rteGIStrength > 0.0) {
		light += texture(rteGI, uv).rgb * rteGIStrength;
	}
	if (!rteShading) {
		FragColor = vec4(min(light, vec3(4.0)) * rteSmokeColor * coverage * rteStrength, 1.0);
		return;
	}
	vec2 densityTexel = 1.0 / vec2(textureSize(rteDensity, 0));
	// The smoke's own colour (what its sprites look like), brightened a little since scattering lights it through.
	vec3 tint = mix(rteSmokeColor, min(smoke.rgb / density * 1.25, vec3(1.0)), rteShadingStrength);
	// Towards the brightest light around (uphill in the light buffer): smoke between here and it shades this side.
	vec2 step = 4.0 / rteScreenSize;
	vec2 uphill = vec2(Luminance(texture(rteDynamicLight, uv + vec2(step.x, 0.0)).rgb) - Luminance(texture(rteDynamicLight, uv - vec2(step.x, 0.0)).rgb),
	                   Luminance(texture(rteDynamicLight, uv + vec2(0.0, step.y)).rgb) - Luminance(texture(rteDynamicLight, uv - vec2(0.0, step.y)).rgb));
	float lightPassing = 1.0;
	if (dot(uphill, uphill) > 1e-8) {
		lightPassing = exp(-texture(rteDensity, uv + normalize(uphill) * densityTexel * 5.0).a * 1.4);
	}
	// The sun, where it reaches: it lights the side of the smoke towards it, and the far side is in the smoke's own shadow.
	vec2 worldPos = rteScreenOrigin + gl_FragCoord.xy;
	float sunVisible = texture(rteSkyLight, worldPos / rteGridWorldSize).g;
	float sunPassing = exp(-texture(rteDensity, uv + normalize(rteSunDirection) * densityTexel * 6.0).a * 1.4);
	vec3 sun = rteSunLight * sunVisible * sunPassing * 0.6;
	vec3 scattered = (min(light, vec3(4.0)) * mix(1.0, lightPassing, rteShadingStrength) + sun * rteShadingStrength) * tint * coverage * rteStrength;
	float shade = coverage * (1.0 - sunPassing) * sunVisible * min(Luminance(rteSunLight), 1.0) * 0.45 * rteShadingStrength;
	FragColor = vec4(scattered, 1.0 - shade);
}
