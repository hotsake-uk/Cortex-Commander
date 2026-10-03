// Tonemap.frag
// Combines the HDR scene with bloom, applies exposure, a gentle shoulder that leaves normal brightness untouched (so the pixel art keeps its palette), vignette, and converts back to gamma space.
#version 330 core

in vec2 textureUV;
out vec4 FragColor;

uniform sampler2D rteScene;
uniform sampler2D rteBloom;
uniform vec2 rteScreenSize;
uniform float rteBloomIntensity;
uniform float rteExposure;
uniform float rteShoulderStart; // Linear values below this pass through unchanged.
uniform float rteVignette;
uniform float rteSaturation;

vec3 Shoulder(vec3 color) {
	vec3 over = max(color - rteShoulderStart, vec3(0.0));
	float range = 1.0 - rteShoulderStart;
	vec3 compressed = rteShoulderStart + range * (1.0 - exp(-over / range));
	return mix(color, compressed, step(rteShoulderStart, color));
}

void main() {
	vec2 uv = gl_FragCoord.xy / rteScreenSize;
	vec3 color = texture(rteScene, uv).rgb;
	color += texture(rteBloom, uv).rgb * rteBloomIntensity;
	color *= rteExposure;

	float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
	color = max(mix(vec3(luminance), color, rteSaturation), vec3(0.0));

	color = Shoulder(color);

	vec2 centered = uv - 0.5;
	color *= 1.0 - rteVignette * smoothstep(0.35, 0.85, length(centered * vec2(rteScreenSize.x / rteScreenSize.y, 1.0)));

	FragColor = vec4(pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);
}
