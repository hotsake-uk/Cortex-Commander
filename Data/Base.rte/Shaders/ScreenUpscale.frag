#version 330 core
// Scales the finished frame up to the window. "Sharp bilinear": every source pixel becomes an evenly sized block, and only the one screen pixel
// on the boundary between blocks is blended. At non-integer scales (e.g. 2.5x) plain nearest filtering makes pixel columns alternately 2 and 3
// screen pixels wide, which makes pixel art and text look uneven; this keeps it crisp and regular. Needs the texture to be linearly filtered.

in vec2 textureUV;

out vec4 FragColor;

uniform sampler2D rteTexture;
uniform float rteScanlines; // 0..1: how strong the CRT effect is.
uniform int rteCRTStyle; // 0 scanlines, darkening the gap between rows of source pixels; 1 aperture grille; 2 shadow mask; 3 scanlines that bright pixels bloom across.
uniform float rteSharpness; // 1: even, sharp blocks; down to 0, plain bilinear smoothing.

void main() {
	vec2 textureSizePixels = vec2(textureSize(rteTexture, 0));
	vec2 texel = textureUV * textureSizePixels;
	vec2 seam = floor(texel + 0.5);
	// fwidth is how many source pixels one screen pixel covers, so this works at any scale without knowing the window size.
	// Less sharpness widens the blend across each seam, all the way to plain bilinear.
	texel = (texel - seam) / mix(vec2(1.0), fwidth(texel), rteSharpness) + seam;
	texel = clamp(texel, seam - 0.5, seam + 0.5);
	vec3 color = texture(rteTexture, texel / textureSizePixels).rgb;
	if (rteScanlines > 0.0) {
		float row = fract(textureUV.y * textureSizePixels.y);
		float beam = sin(row * 3.14159265);
		if (rteCRTStyle == 1) {
			// Aperture grille: each screen pixel column a red, green or blue phosphor stripe, with the lost brightness given back.
			int stripe = int(gl_FragCoord.x) % 3;
			vec3 phosphor = vec3(stripe == 0 ? 1.0 : 0.0, stripe == 1 ? 1.0 : 0.0, stripe == 2 ? 1.0 : 0.0);
			color *= mix(vec3(1.0), phosphor * 1.6 + 0.35, rteScanlines * 0.7);
		} else if (rteCRTStyle == 2) {
			// Shadow mask: triads of red, green and blue dots, every other row shifted half a triad, with dark gaps between rows of dots.
			ivec2 pixel = ivec2(gl_FragCoord.xy);
			int triad = (pixel.x + (pixel.y / 2 % 2) * 2) % 3;
			vec3 phosphor = vec3(triad == 0 ? 1.0 : 0.0, triad == 1 ? 1.0 : 0.0, triad == 2 ? 1.0 : 0.0);
			float gap = pixel.y % 2 == 0 ? 1.0 : 0.7;
			color *= mix(vec3(1.0), (phosphor * 1.7 + 0.3) * gap * 1.12, rteScanlines * 0.7);
		} else if (rteCRTStyle == 3) {
			// Scanlines that bright pixels bloom across: a bright row's beam is wider, so it fills the gap, and glows a little.
			float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
			float width = mix(0.35, 1.0, clamp(brightness * 1.4, 0.0, 1.0));
			color *= (1.0 - rteScanlines * 0.6 * (1.0 - pow(beam, 1.0 / width))) * (1.0 + rteScanlines * 0.15);
			color += color * color * rteScanlines * 0.2;
		} else {
			// Brightest along the middle of each source row, darkest between rows, with the lost brightness given back.
			color *= (1.0 - rteScanlines * 0.5 * (1.0 - beam)) * (1.0 + rteScanlines * 0.18);
		}
	}
	FragColor = vec4(color, 1.0);
}
