// RCIrradiance.frag
// Averages each cascade 0 probe's 4 directions into the light arriving at that point. One texel per probe (quarter resolution); sampled with linear filtering.
#version 330 core

out vec4 FragColor;

uniform sampler2D rteCascade0;

void main() {
	ivec2 probe = ivec2(gl_FragCoord.xy);
	vec3 sum = vec3(0.0);
	for (int i = 0; i < 4; ++i) {
		sum += texelFetch(rteCascade0, probe * 2 + ivec2(i & 1, i >> 1), 0).rgb;
	}
	FragColor = vec4(sum * 0.25, 1.0);
}
