// Precipitation.frag
// Rain streaks and snow flakes, lit by the sky. Hidden where the ground shelters them from the way they are falling (worked out per drop in the vertex shader).
#version 330 core

in vec2 quadPos;
in vec2 worldPos;
in float dropAlpha;
flat in float reaches;

out vec4 FragColor;

uniform int rteType;
uniform vec3 rteSkyLight; // Linear sky light, time of day included.
uniform float rteIntensity;
uniform sampler2D rteDynamicLight; // Screen space dynamic light: drops glint where lights catch them.
uniform vec2 rteScreenSize;
uniform float rteOwnLight; // The least light rain, snow, ash and dust are drawn with, so they can be seen on a dark night.

void main() {
	if (reaches < 0.5) {
		discard;
	}
	float alpha = dropAlpha * rteIntensity;
	vec3 color;
	if (rteType == 2 || rteType == 3) {
		// Soft round flake: white snow, or grey ash.
		alpha *= 1.0 - smoothstep(0.35, 0.5, length(quadPos - 0.5));
		color = rteType == 2 ? vec3(0.9, 0.93, 1.0) : vec3(0.5, 0.48, 0.47);
	} else if (rteType == 4) {
		// A streak of blown dust.
		alpha *= quadPos.y * 1.3;
		color = vec3(0.8, 0.66, 0.46);
	} else {
		// Streak that fades towards its tail.
		alpha *= quadPos.y * 1.3;
		color = vec3(0.7, 0.78, 0.9);
	}
	vec3 dynamicLight = texture(rteDynamicLight, gl_FragCoord.xy / rteScreenSize).rgb;
	vec3 light = max(rteSkyLight, vec3(rteOwnLight)) + dynamicLight * 1.5;
	// Lit drops read better: let bright light push the alpha up a little.
	alpha *= clamp(0.6 + dot(light, vec3(0.333)) * 0.8, 0.6, 1.6);
	FragColor = vec4(color * light, clamp(alpha, 0.0, 1.0));
}
