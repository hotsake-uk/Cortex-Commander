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
uniform bool rteSunMapOn; // Where the sun reaches from the sun's shadow map (SunShadowMap.frag) rather than the sky light grid.
uniform sampler2D rteSunMap; // 1 row: for each ray from the sun, the scene y of the first solid point on it.
uniform float rteSunMapSlope;
uniform float rteSunMapStart;
uniform float rteSunMapTexel;
uniform sampler2D rteSceneDepth; // The player screen's depth buffer.
uniform bool rteEffectsLayer; // Smoke is in the effects layer (LightingSettings::LayerSmoke): it neither lights nor shades what's in front of it.
uniform float rteEffectsFrontDepth; // Depth below which a pixel is in front of the effects layer: a unit or the ground in front.

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
	if (rteEffectsLayer && texture(rteSceneDepth, uv).r < rteEffectsFrontDepth) {
		discard;
	}
	// Smoke this thick is opaque to the eye; scattering saturates rather than growing without bound.
	float coverage = 1.0 - exp(-density * 1.5);
	// Softened towards 2 as lights pile up, as on surfaces (LightComposite.frag's rteMaxDynamicLight), rather than taken raw up to 4.
	vec3 light = 2.0 * (1.0 - exp(-texture(rteDynamicLight, uv).rgb / 2.0));
	if (rteGIStrength > 0.0) {
		light += min(texture(rteGI, uv).rgb * rteGIStrength, vec3(2.0));
	}
	if (!rteShading) {
		FragColor = vec4(light * rteSmokeColor * coverage * rteStrength, 1.0);
		return;
	}
	vec2 densityTexel = 1.0 / vec2(textureSize(rteDensity, 0));
	// The smoke's own colour (what its sprites look like), brightened a little since scattering lights it through.
	// The colour is averaged from the sprites' sRGB palette colours: its hue is made linear to match the light, which keeps coloured smoke from washing out,
	// but at the brightness it had, since grey smoke made linear would scatter only about half the light it did.
	vec3 spriteColor = smoke.rgb / density;
	vec3 linearColor = pow(spriteColor, vec3(2.2));
	linearColor *= Luminance(spriteColor) / max(Luminance(linearColor), 1e-4);
	vec3 tint = mix(rteSmokeColor, min(linearColor * 1.25, vec3(1.0)), rteShadingStrength);
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
	float sunVisible;
	if (rteSunMapOn) {
		// In the sun above the first solid point on its ray, fading over a few pixels below it, as soft as smoke is.
		float ray = (worldPos.x + rteSunMapSlope * worldPos.y - rteSunMapStart) / rteSunMapTexel;
		float first = texture(rteSunMap, vec2(ray / float(textureSize(rteSunMap, 0).x), 0.5)).r;
		sunVisible = 1.0 - smoothstep(0.0, 6.0, worldPos.y - first);
	} else {
		sunVisible = texture(rteSkyLight, worldPos / rteGridWorldSize).g;
	}
	float sunPassing = exp(-texture(rteDensity, uv + normalize(rteSunDirection) * densityTexel * 6.0).a * 1.4);
	vec3 sun = rteSunLight * sunVisible * sunPassing * 0.6;
	vec3 scattered = (light * mix(1.0, lightPassing, rteShadingStrength) + sun * rteShadingStrength) * tint * coverage * rteStrength;
	float shade = coverage * (1.0 - sunPassing) * sunVisible * min(Luminance(rteSunLight), 1.0) * 0.45 * rteShadingStrength;
	FragColor = vec4(scattered, 1.0 - shade);
}
