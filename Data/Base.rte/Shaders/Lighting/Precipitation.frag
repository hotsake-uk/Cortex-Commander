// Precipitation.frag
// Weather drops (Weather.h), lit by the sky and lights, and glowing if the weather glows. Hidden where the ground shelters them from the way they are falling
// (worked out per drop in the vertex shader). Rain is grey-blue streaks, snow and ash soft flakes, dust tan streaks; other weather is what its preset says.
#version 330 core

in vec2 quadPos;
in vec2 worldPos;
in float dropAlpha;
flat in float reaches;
flat in vec3 dropSeeds;

out vec4 FragColor;

uniform int rteType;
uniform vec3 rteSkyLight; // Linear sky light, time of day included.
uniform float rteIntensity;
uniform sampler2D rteDynamicLight; // Screen space dynamic light: drops glint where lights catch them.
uniform vec2 rteScreenSize;
uniform float rteOwnLight; // The least light rain, snow, ash and dust are drawn with, so they can be seen on a dark night.
uniform float rteTime;
uniform int rteShape; // 0 streak, 1 flake, 2 spark, 3 orb.
uniform vec3 rteDropColor; // Linear; each drop's colour is between this and rteDropColor2.
uniform vec3 rteDropColor2;
uniform float rteGlow; // Light the drop gives off itself, not lit by anything.
uniform vec2 rtePulse; // Brightening and dimming: cycles per second, how far it dims 0..1.
uniform float rteTwinkle; // 0..1, sparkling on and off at random.

float Hash(float n) {
	return fract(sin(n * 12.9898) * 43758.5453);
}

void main() {
	if (reaches < 0.5) {
		discard;
	}
	float shape;
	if (rteShape == 1) {
		// Soft round flake.
		shape = 1.0 - smoothstep(0.35, 0.5, length(quadPos - 0.5));
	} else if (rteShape == 2) {
		// A spark: bright at its head, a short faint tail.
		shape = quadPos.y * quadPos.y * 1.6;
	} else if (rteShape == 3) {
		// A soft glowing ball.
		vec2 offset = quadPos - 0.5;
		shape = exp(-dot(offset, offset) * 14.0);
	} else {
		// Streak that fades towards its tail.
		shape = quadPos.y * 1.3;
	}
	float alpha = dropAlpha * rteIntensity * shape;
	vec3 color = mix(rteDropColor, rteDropColor2, dropSeeds.y);
	float shine = 1.0;
	if (rtePulse.x > 0.0) {
		shine *= 1.0 - rtePulse.y * (0.5 + 0.5 * sin((rteTime * rtePulse.x + dropSeeds.x) * 6.2832));
	}
	if (rteTwinkle > 0.0) {
		float on = step(0.7, Hash(floor(rteTime * 9.0 + dropSeeds.z * 13.0) + dropSeeds.x * 91.0));
		shine *= mix(1.0, 0.25 + 1.5 * on, rteTwinkle);
	}
	vec3 dynamicLight = texture(rteDynamicLight, gl_FragCoord.xy / rteScreenSize).rgb;
	vec3 light = max(rteSkyLight, vec3(rteOwnLight)) + dynamicLight * 1.5;
	// Lit drops read better: let bright light push the alpha up a little.
	alpha *= clamp(0.6 + dot(light, vec3(0.333)) * 0.8, 0.6, 1.6);
	vec3 lit = color * light;
	if (rteGlow > 0.0) {
		// Glowing drops show in the dark, and the brightest feed the bloom.
		lit += color * rteGlow * shine;
		alpha = max(alpha, min(rteGlow * shine, 1.0) * dropAlpha * rteIntensity * shape * 0.8);
	}
	FragColor = vec4(lit, clamp(alpha * (rteGlow > 0.0 ? 1.0 : shine), 0.0, 1.0));
}
