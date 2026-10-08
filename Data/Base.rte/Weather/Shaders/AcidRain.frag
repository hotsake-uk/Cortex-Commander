// AcidRain.frag
// Acid Rain's drops (Base.rte/Weather/Weather.ini, DropShader): a hot bright core down the middle of each streak, a fat glowing bead at its head,
// green going sickly yellow towards the head, and a fizzing flicker. Uses Precipitation.vert, as any weather's DropShader does.
#version 330 core

in vec2 quadPos; // 0..1 across and along the drop (1 at its head).
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

float Hash(float n) {
	return fract(sin(n * 12.9898) * 43758.5453);
}

void main() {
	if (reaches < 0.5) {
		discard;
	}
	float across = abs(quadPos.x - 0.5) * 2.0;
	// The streak fades to its tail; its core is thin and hot.
	float body = quadPos.y * 1.2 * (1.0 - across * 0.6);
	float core = (1.0 - smoothstep(0.0, 0.45, across)) * quadPos.y;
	// A bead at the head.
	float bead = 1.0 - smoothstep(0.0, 0.35, length(vec2(quadPos.x - 0.5, (quadPos.y - 0.9) * 2.5)));
	float shape = clamp(max(body, bead), 0.0, 1.0);

	vec3 color = mix(rteDropColor, rteDropColor2, dropSeeds.y);
	color = mix(color, vec3(0.85, 1.0, 0.25), quadPos.y * 0.35 * rteStrength);
	// Fizzing: each drop flickers on its own, quickly.
	float fizz = 0.7 + 0.3 * Hash(floor(rteTime * 20.0 + dropSeeds.x * 50.0) + dropSeeds.z * 17.0);
	float pulse = rtePulse.x > 0.0 ? 1.0 - rtePulse.y * (0.5 + 0.5 * sin((rteTime * rtePulse.x + dropSeeds.x) * 6.2832)) : 1.0;
	float glow = rteGlow * mix(1.0, fizz, rteStrength) * pulse;

	vec3 light = max(rteSkyLight, vec3(rteOwnLight)) + texture(rteDynamicLight, gl_FragCoord.xy / rteScreenSize).rgb * 1.5;
	vec3 lit = color * light * 0.6 + color * glow * (0.6 + core * 1.4 + bead * 0.8);
	float alpha = dropAlpha * rteIntensity * shape * clamp(0.7 + glow * 0.4, 0.7, 1.4);
	FragColor = vec4(lit, clamp(alpha, 0.0, 1.0));
}
