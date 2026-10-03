#version 330 core
// Scales a GUI layer whose transparent pixels are pure black up to the window, over what's already there (alpha blended).
// Same even "sharp bilinear" pixels as ScreenUpscale.frag, but the blend across pixel boundaries treats black as transparent,
// so edges against transparency fade out instead of darkening.

in vec2 textureUV;

out vec4 FragColor;

uniform sampler2D rteTexture;

void main() {
	vec2 textureSizePixels = vec2(textureSize(rteTexture, 0));
	vec2 texel = textureUV * textureSizePixels;
	vec2 seam = floor(texel + 0.5);
	texel = (texel - seam) / fwidth(texel) + seam;
	texel = clamp(texel, seam - 0.5, seam + 0.5);

	// Manual bilinear filtering between the four nearest pixels, with coverage from the color key.
	vec2 position = texel - 0.5;
	ivec2 base = ivec2(floor(position));
	vec2 weight = position - vec2(base);
	ivec2 maxCoord = ivec2(textureSizePixels) - 1;
	vec3 colors[4];
	float coverage[4];
	for (int i = 0; i < 4; ++i) {
		ivec2 coord = clamp(base + ivec2(i & 1, i >> 1), ivec2(0), maxCoord);
		colors[i] = texelFetch(rteTexture, coord, 0).rgb;
		coverage[i] = colors[i] == vec3(0.0) ? 0.0 : 1.0;
	}
	// Black pixels contribute nothing, so this is the premultiplied color.
	vec3 color = mix(mix(colors[0], colors[1], weight.x), mix(colors[2], colors[3], weight.x), weight.y);
	float alpha = mix(mix(coverage[0], coverage[1], weight.x), mix(coverage[2], coverage[3], weight.x), weight.y);
	if (alpha <= 0.0) {
		discard;
	}
	FragColor = vec4(color / alpha, alpha);
}
