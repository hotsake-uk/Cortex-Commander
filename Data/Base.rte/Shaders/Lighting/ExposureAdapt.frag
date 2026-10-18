// ExposureAdapt.frag
// Second step of auto exposure: moves the adapted log luminance towards this frame's average, like eyes adjusting.
// Eyes adjust to bright light faster than to darkness.
#version 330 core

out vec4 FragColor;

uniform sampler2D rteLuminance; // Log luminance with mipmaps, the last level is the average.
uniform float rteLuminanceLod;
uniform sampler2D rtePrevious; // 1x1, the adapted log luminance so far.
uniform float rteDeltaSeconds;
uniform float rteBrightenSpeed;
uniform float rteDarkenSpeed;
uniform bool rteReset;

void main() {
	float current = textureLod(rteLuminance, vec2(0.5), rteLuminanceLod).r;
	float previous = texture(rtePrevious, vec2(0.5)).r;
	if (rteReset || isnan(previous) || isinf(previous)) {
		FragColor = vec4(current, 0.0, 0.0, 1.0);
		return;
	}
	float speed = current > previous ? rteBrightenSpeed : rteDarkenSpeed;
	float adapted = previous + (current - previous) * (1.0 - exp(-rteDeltaSeconds * speed));
	FragColor = vec4(adapted, 0.0, 0.0, 1.0);
}
