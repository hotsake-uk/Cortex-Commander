// UnseenSoft.frag
// Fog of war with soft edges. The unseen layer is a low resolution indexed bitmap (0 = seen, anything else = unseen), upscaled over the scene.
// Coverage is bilinearly filtered by hand (indices can't be filtered by the hardware), eased, and lightly dithered at the edge so it stays pixel-art friendly.
#version 330 core

in vec2 textureUV;
in vec4 vertexColor;
layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 NormalOut;
layout(location = 2) out vec4 SurfaceOut;

uniform sampler2D rteTexture;

float Coverage(ivec2 texel, ivec2 size) {
	texel = clamp(texel, ivec2(0), size - 1);
	return texelFetch(rteTexture, texel, 0).r > 0.0 ? 1.0 : 0.0;
}

void main() {
	ivec2 size = textureSize(rteTexture, 0);
	vec2 position = textureUV * vec2(size) - 0.5;
	ivec2 base = ivec2(floor(position));
	vec2 fraction = fract(position);
	float coverage = mix(mix(Coverage(base, size), Coverage(base + ivec2(1, 0), size), fraction.x), mix(Coverage(base + ivec2(0, 1), size), Coverage(base + ivec2(1, 1), size), fraction.x), fraction.y);
	coverage = smoothstep(0.0, 1.0, coverage);

	// Dither in 2x2 pixel blocks, only near the edge.
	float noise = fract(sin(dot(floor(gl_FragCoord.xy / 2.0), vec2(12.9898, 78.233))) * 43758.5453);
	float edge = 1.0 - abs(coverage * 2.0 - 1.0);
	coverage = clamp(coverage + (noise - 0.5) * 0.35 * edge, 0.0, 1.0);
	if (coverage <= 0.01) {
		discard;
	}
	FragColor = vec4(0.0, 0.0, 0.0, coverage * vertexColor.a);
	NormalOut = vec4(0.5, 0.5, 1.0, 0.5);
	// Nothing under the fog casts a shadow anyone can see.
	SurfaceOut = vec4(0.0);
}
