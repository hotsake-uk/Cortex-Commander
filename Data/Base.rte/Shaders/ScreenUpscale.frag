#version 330 core
// Scales the finished frame up to the window. "Sharp bilinear": every source pixel becomes an evenly sized block, and only the one screen pixel
// on the boundary between blocks is blended. At non-integer scales (e.g. 2.5x) plain nearest filtering makes pixel columns alternately 2 and 3
// screen pixels wide, which makes pixel art and text look uneven; this keeps it crisp and regular. Needs the texture to be linearly filtered.

in vec2 textureUV;

out vec4 FragColor;

uniform sampler2D rteTexture;
uniform float rteScanlines; // 0..1: CRT style scanlines, darkening the gap between rows of source pixels.

void main() {
	vec2 textureSizePixels = vec2(textureSize(rteTexture, 0));
	vec2 texel = textureUV * textureSizePixels;
	vec2 seam = floor(texel + 0.5);
	// fwidth is how many source pixels one screen pixel covers, so this works at any scale without knowing the window size.
	texel = (texel - seam) / fwidth(texel) + seam;
	texel = clamp(texel, seam - 0.5, seam + 0.5);
	vec3 color = texture(rteTexture, texel / textureSizePixels).rgb;
	if (rteScanlines > 0.0) {
		// Brightest along the middle of each source row, darkest between rows, with the lost brightness given back.
		float row = fract(textureUV.y * textureSizePixels.y);
		float beam = sin(row * 3.14159265);
		color *= (1.0 - rteScanlines * 0.5 * (1.0 - beam)) * (1.0 + rteScanlines * 0.18);
	}
	FragColor = vec4(color, 1.0);
}
