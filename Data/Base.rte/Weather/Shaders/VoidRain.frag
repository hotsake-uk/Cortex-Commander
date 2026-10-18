// VoidRain.frag
// Void Rain's drops (Base.rte/Weather/Weather.ini, DropShader): streaks of something that falls up, dark at the core with glowing violet edges, and bands of
// light running along them. Uses Precipitation.vert, as any weather's DropShader does.
#version 330 core

in vec2 quadPos; // 0..1 across and along the drop (1 at its head, which leads the way it moves).
in vec2 worldPos;
in float dropAlpha;
flat in float reaches;
flat in vec3 dropSeeds;

out vec4 FragColor;

uniform vec3 rteSkyLight;
uniform float rteIntensity;
uniform sampler2D rteDynamicLight;
uniform vec2 rteScreenSize;
uniform float rteOwnLight;
uniform float rteTime;
uniform vec3 rteDropColor;
uniform vec3 rteDropColor2;
uniform float rteGlow;
uniform vec2 rtePulse;
uniform float rteStrength; // Mod shader strength, 0..1.

void main() {
	if (reaches < 0.5) {
		discard;
	}
	float across = abs(quadPos.x - 0.5) * 2.0;
	float shape = quadPos.y * 1.3;
	// Bright at the edges, dark in the middle.
	float rim = mix(1.0, smoothstep(0.1, 0.9, across) * 1.4 + 0.15, rteStrength);
	// Bands of light running along the streak towards its head.
	float bands = 0.6 + 0.4 * sin((quadPos.y * 3.0 - rteTime * 4.0 + dropSeeds.z * 6.2832) * 6.2832);

	vec3 color = mix(rteDropColor, rteDropColor2, dropSeeds.y);
	float pulse = rtePulse.x > 0.0 ? 1.0 - rtePulse.y * (0.5 + 0.5 * sin((rteTime * rtePulse.x + dropSeeds.x) * 6.2832)) : 1.0;
	float glow = rteGlow * pulse * rim * mix(1.0, bands, rteStrength);

	vec3 light = max(rteSkyLight, vec3(rteOwnLight)) + texture(rteDynamicLight, gl_FragCoord.xy / rteScreenSize).rgb * 1.5;
	vec3 lit = color * light * 0.4 + color * glow;
	float alpha = dropAlpha * rteIntensity * shape * clamp(0.6 + rim * 0.5, 0.6, 1.3);
	FragColor = vec4(lit, clamp(alpha, 0.0, 1.0));
}
