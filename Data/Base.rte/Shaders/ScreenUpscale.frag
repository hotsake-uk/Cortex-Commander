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
// Retro filters over the whole frame, HUD included (LightingSettings::RetroLayers); all 0 outside a game.
uniform float rteTime; // Real seconds, so tape and glitches keep moving through pauses.
uniform float rtePixelate; // Block size in source pixels, 1 for none.
uniform float rteVHS; // Video tape, 0 to 1.
uniform float rteTrackingBand; // Tape's tracking band rolling up the screen, tearing and snowy, 0 to 1.
uniform float rteGlitch; // How often and how hard glitch bursts come, 0 to 1.
uniform float rteGlitchKick; // A burst set off now (a big blast), 0 to 1.
uniform float rteCurvature; // CRT glass bend, 0 to 1.
uniform int rteGlitchKind; // 1 blocks torn sideways with the colours split (Synthwave), 2 blocks scrambled into green glyphs (Hackerman).
uniform float rteFlicker; // A monitor's faint flicker and a brighter refresh band rolling down, 0 to 1.
uniform float rtePanelFrame; // A comic panel's frame round the screen: a paper gutter and a thick black line, 0 to 1.
uniform float rteLCD; // A Game Boy screen: the whole frame in four shades, 0 to 1.
uniform vec3 rteLCDShades[4]; // Its shades in display colours, the one the darkest things take first (LightingSettings::LCDShades).
uniform float rteLCDGrid; // The LCD's dot matrix: thin light gaps between its pixels, 0 to 1.
uniform float rteLCDBlock; // The battlefield's pixel size in source pixels (RetroLayers::ChunkyPixels), so the dot matrix lines up with its pixels.
uniform float rteLCDFlash; // A big blast's flash, showing the shades inverted, 0 to 1 now.
uniform float rteBezel; // The handheld round the screen: grey plastic and the dark glass surround with its stripes and power light, 0 to 1.

float Hash(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// Signed distance from a box of the given half size, its corners rounded by radius, centred on the origin.
float RoundedBox(vec2 p, vec2 halfSize, float radius) {
	vec2 q = abs(p) - halfSize + radius;
	return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

// The Game Boy screen's shade (0 to 3) for a colour: the shade it already is (the battlefield arrives banded and dithered onto them), or else by its
// brightness (the HUD, and anything drawn over the battlefield after).
int LCDShade(vec3 color) {
	for (int shade = 0; shade < 4; ++shade) {
		vec3 difference = abs(color - rteLCDShades[shade]);
		if (difference.r + difference.g + difference.b < 0.04) {
			return shade;
		}
	}
	return int(clamp(floor(dot(color, vec3(0.2126, 0.7152, 0.0722)) * 4.0), 0.0, 3.0));
}

// The frame at uv, "sharp bilinear". scale is fwidth of the texel position, taken once where the derivatives are smooth.
vec3 SampleSharp(vec2 uv, vec2 textureSizePixels, vec2 scale) {
	vec2 texel = uv * textureSizePixels;
	vec2 seam = floor(texel + 0.5);
	// Less sharpness widens the blend across each seam, all the way to plain bilinear.
	texel = (texel - seam) / mix(vec2(1.0), scale, rteSharpness) + seam;
	texel = clamp(texel, seam - 0.5, seam + 0.5);
	return texture(rteTexture, texel / textureSizePixels).rgb;
}

void main() {
	vec2 textureSizePixels = vec2(textureSize(rteTexture, 0));
	// fwidth is how many source pixels one screen pixel covers, so this works at any scale without knowing the window size.
	vec2 scale = fwidth(textureUV * textureSizePixels);
	vec2 uv = textureUV;
	float outside = 0.0;
	float aspect = textureSizePixels.x / max(textureSizePixels.y, 1.0);
	// The handheld is laid over the edges of the picture rather than shrinking it, so the cursor and the menus drawn over the frame still line up.
	// Measured in the frame's height from its middle, y down.
	vec2 bezelPoint = (textureUV - 0.5) * vec2(aspect, 1.0);
	float pictureScale = mix(1.0, 0.9, clamp(rteBezel, 0.0, 1.0));
	if (rteCurvature > 0.0) {
		// The tube's glass: the picture bulges, so the edges pull in, and what falls off the glass is black.
		vec2 centered = uv * 2.0 - 1.0;
		vec2 bend = centered * (1.0 + dot(centered, centered) * vec2(0.035, 0.05) * rteCurvature * 2.0);
		uv = bend * 0.5 + 0.5;
		vec2 overEdge = max(abs(bend) - 1.0, 0.0);
		outside = step(0.0001, overEdge.x + overEdge.y);
	}
	// Where on the glass this is, before tape and glitches move the picture about, for the LCD's dot matrix.
	vec2 glassUV = uv;
	float glitchBurst = 0.0;
	if (rteGlitch > 0.0 || rteGlitchKick > 0.0) {
		// A short burst now and then (more often and longer with more glitch), or when a blast kicks one off.
		float slot = floor(rteTime * 3.0);
		float chance = Hash(vec2(slot, 1.7));
		float randomBurst = rteGlitch > 0.0 && chance > 1.0 - 0.12 * rteGlitch ? rteGlitch : 0.0;
		glitchBurst = max(randomBurst * step(fract(rteTime * 3.0), 0.45 + 0.4 * rteGlitch), rteGlitchKick);
		if (glitchBurst > 0.0) {
			// Bands of the picture torn sideways by blocky amounts, changing many times a second.
			float tick = floor(rteTime * 24.0);
			float row = floor(uv.y * mix(10.0, 28.0, Hash(vec2(tick, 3.1))));
			float tear = Hash(vec2(row, tick));
			if (rteGlitchKind != 2 && tear > 1.0 - 0.35 * glitchBurst) {
				uv.x += (Hash(vec2(row, tick + 9.0)) - 0.5) * 0.12 * glitchBurst;
			}
		}
	}
	if (rteVHS > 0.0) {
		// Tape: the picture wobbles a little side to side and the bottom rows (head switching) slew sideways.
		uv.x += sin(uv.y * 40.0 + rteTime * 2.3) * 0.0006 * rteVHS + (Hash(vec2(floor(rteTime * 30.0), floor(uv.y * 240.0))) - 0.5) * 0.0012 * rteVHS;
		float headSwitch = 1.0 - smoothstep(0.0, 0.03, 1.0 - uv.y);
		uv.x += headSwitch * 0.015 * rteVHS * (0.6 + 0.4 * sin(rteTime * 9.0));
	}
	float inBand = 0.0;
	if (rteTrackingBand > 0.0) {
		// The tracking band rolls slowly up the screen, tearing what's inside it.
		inBand = 1.0 - smoothstep(0.0, 0.035, abs(uv.y - (1.0 - fract(rteTime * 0.06))));
		uv.x += inBand * (Hash(vec2(floor(uv.y * 300.0), floor(rteTime * 20.0))) - 0.5) * 0.02 * rteTrackingBand;
	}
	if (rtePixelate > 1.0) {
		uv = (floor(uv * textureSizePixels / rtePixelate) + 0.5) * rtePixelate / textureSizePixels;
	}
	vec3 color = SampleSharp(uv, textureSizePixels, scale);
	if (glitchBurst > 0.0 && rteGlitchKind == 2) {
		// Decoding: blocks of the picture scrambled into green glyphs for a moment, changing many times a second.
		float tick = floor(rteTime * 20.0);
		vec2 sourcePixel = uv * textureSizePixels;
		vec2 block = floor(sourcePixel / vec2(30.0, 18.0));
		if (Hash(block + tick * 1.37) > 1.0 - 0.3 * glitchBurst) {
			vec2 cell = floor(sourcePixel / vec2(6.0, 9.0));
			ivec2 inCell = ivec2(sourcePixel - cell * vec2(6.0, 9.0));
			float lit = inCell.x < 5 && inCell.y < 7 ? step(0.5, Hash(cell * 7.0 + vec2(inCell) * 0.37 + tick)) : 0.0;
			color = vec3(0.25, 1.0, 0.4) * lit * mix(0.5, 1.0, Hash(cell + tick));
		}
	}
	if (rteVHS > 0.0 || (glitchBurst > 0.0 && rteGlitchKind != 2)) {
		// Colour bleeding sideways (tape smears its colour more than its brightness), and the channels split in a glitch.
		vec2 shift = vec2((2.5 * rteVHS + (rteGlitchKind != 2 ? 6.0 * glitchBurst : 0.0)) / textureSizePixels.x, 0.0);
		vec3 left = SampleSharp(uv - shift, textureSizePixels, scale);
		vec3 right = SampleSharp(uv + shift, textureSizePixels, scale);
		color = vec3(mix(color.r, left.r, 0.7), color.g, mix(color.b, right.b, 0.7));
	}
	if (rteLCD > 0.0) {
		// The Game Boy screen: everything in its four shades, the HUD too. A big blast's flash shows them inverted for a blink or two.
		int shade = LCDShade(color);
		vec3 lcd = mix(rteLCDShades[shade], rteLCDShades[3 - shade], clamp(rteLCDFlash, 0.0, 1.0));
		color = mix(color, lcd, rteLCD);
	}
	if (rteLCDGrid > 0.0) {
		// The dot matrix: a screen pixel wide gap of the paler end of the shades along the top and left of each of the battlefield's pixels, only where
		// they're big enough on screen to show one.
		vec2 blockOnScreen = rteLCDBlock / max(scale, vec2(0.0001));
		vec2 intoBlock = fract(glassUV * textureSizePixels / rteLCDBlock) * blockOnScreen;
		float gap = max(step(intoBlock.x, 1.0), step(intoBlock.y, 1.0)) * smoothstep(2.5, 4.0, min(blockOnScreen.x, blockOnScreen.y));
		vec3 paler = rteLCD > 0.0 ? (dot(rteLCDShades[3], vec3(1.0)) > dot(rteLCDShades[0], vec3(1.0)) ? rteLCDShades[3] : rteLCDShades[0]) : vec3(1.0);
		color = mix(color, mix(color, paler, 0.45), gap * rteLCDGrid);
	}
	if (rteVHS > 0.0) {
		float luminance = dot(color, vec3(0.299, 0.587, 0.114));
		color = mix(color, vec3(luminance), 0.12 * rteVHS);
		vec2 cell = floor(gl_FragCoord.xy * vec2(0.5, 1.0)) + floor(rteTime * 30.0) * 13.0;
		float noise = Hash(cell);
		// The odd white fleck of dropout anywhere.
		color += step(0.9992, Hash(vec2(floor(gl_FragCoord.y), floor(rteTime * 24.0)) + floor(gl_FragCoord.x / 40.0))) * 0.6 * rteVHS;
		color += (noise - 0.5) * 0.05 * rteVHS;
	}
	if (inBand > 0.0) {
		// Snow in the tracking band.
		float snow = Hash(floor(gl_FragCoord.xy * vec2(0.5, 1.0)) + floor(rteTime * 30.0) * 13.0);
		color = mix(color, vec3(snow), inBand * 0.35 * rteTrackingBand);
	}
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
	if (rteFlicker > 0.0) {
		// A faint flicker, and the brighter band of a refresh rolling down the screen.
		color *= 1.0 - 0.07 * rteFlicker * Hash(vec2(floor(rteTime * 30.0), 5.3));
		float bandY = fract(rteTime * 0.22);
		float band = exp(-pow((textureUV.y - bandY) * 9.0, 2.0));
		color *= 1.0 + 0.12 * rteFlicker * band;
	}
	if (rteCurvature > 0.0) {
		// Rounded, darkened corners of the glass.
		vec2 centered = textureUV * 2.0 - 1.0;
		float corner = length(max(abs(centered) - (1.0 - 0.22 * rteCurvature), 0.0)) / (0.22 * rteCurvature + 0.0001);
		color *= 1.0 - smoothstep(0.6, 1.0, corner);
		color *= 1.0 - 0.35 * rteCurvature * smoothstep(0.5, 1.5, length(centered));
		color *= 1.0 - outside;
	}
	if (rtePanelFrame > 0.0) {
		// In source pixels from the nearest edge: paper out to 4, then a black line out to 7.
		vec2 px = textureUV * textureSizePixels;
		float fromEdge = min(min(px.x, textureSizePixels.x - px.x), min(px.y, textureSizePixels.y - px.y));
		vec3 frame = fromEdge < 4.0 ? vec3(0.97, 0.95, 0.88) : vec3(0.02);
		color = mix(color, frame, (1.0 - step(7.0, fromEdge)) * rtePanelFrame);
	}
	if (rteBezel > 0.0) {
		// The handheld round the picture. The glass surround: dark blue-grey with its lower right corner rounded right off, two thin stripes above the
		// picture and the red power light to its left. Outside it, light grey plastic. The picture sits a touch recessed, shadowed along its top and left.
		vec2 pictureHalf = vec2(aspect, 1.0) * 0.5 * pictureScale;
		vec2 margin = vec2(0.6, 0.4) * (1.0 - pictureScale);
		vec2 glassHalf = pictureHalf + margin;
		vec2 glassCentre = vec2(0.0, 0.0);
		float corner = bezelPoint.x > 0.0 && bezelPoint.y > 0.0 ? 0.1 : 0.02;
		float inGlass = 1.0 - step(0.0, RoundedBox(bezelPoint - glassCentre, glassHalf, corner));
		vec2 overPicture = abs(bezelPoint) - pictureHalf;
		float inPicture = 1.0 - step(0.0, max(overPicture.x, overPicture.y));
		if (inPicture > 0.5) {
			vec2 fromTopLeft = bezelPoint + pictureHalf;
			float recess = (1.0 - smoothstep(0.0, 0.012, fromTopLeft.y)) * 0.35 + (1.0 - smoothstep(0.0, 0.012, fromTopLeft.x)) * 0.25;
			color *= 1.0 - recess * rteBezel;
		} else if (inGlass > 0.5) {
			vec3 glass = vec3(0.29, 0.3, 0.36) * (0.92 + 0.08 * (1.0 - textureUV.y));
			// The stripes: plum over navy, across the top margin from the glass's left edge to two thirds of the way over.
			float stripeTop = -pictureHalf.y - margin.y * 0.62;
			float alongStripe = step(-glassHalf.x + margin.x * 0.35, bezelPoint.x) * step(bezelPoint.x, -glassHalf.x + (glassHalf.x * 2.0) * 0.66);
			float stripeWidth = margin.y * 0.09;
			float plum = step(stripeTop, bezelPoint.y) * step(bezelPoint.y, stripeTop + stripeWidth) * alongStripe;
			float navy = step(stripeTop + stripeWidth * 2.0, bezelPoint.y) * step(bezelPoint.y, stripeTop + stripeWidth * 3.0) * alongStripe;
			glass = mix(glass, vec3(0.45, 0.12, 0.32), plum);
			glass = mix(glass, vec3(0.16, 0.17, 0.42), navy);
			// The power light, lit, with a little glow on the glass round it.
			vec2 lightCentre = vec2(-pictureHalf.x - margin.x * 0.5, -pictureHalf.y * 0.35);
			float lightDistance = length(bezelPoint - lightCentre) / max(margin.x, 0.0001);
			glass = mix(glass, vec3(1.0, 0.18, 0.12), 1.0 - smoothstep(0.1, 0.13, lightDistance));
			glass += vec3(0.5, 0.05, 0.03) * (1.0 - smoothstep(0.1, 0.35, lightDistance)) * 0.5;
			color = mix(color, glass, rteBezel);
		} else {
			// Plastic, a little shaded towards the bottom and darker right against the glass.
			float nearGlass = 1.0 - smoothstep(0.0, 0.015, RoundedBox(bezelPoint - glassCentre, glassHalf, corner));
			vec3 plastic = vec3(0.78, 0.77, 0.74) * (1.0 - 0.08 * textureUV.y) * (1.0 - 0.18 * nearGlass);
			color = mix(color, plastic, rteBezel);
		}
	}
	FragColor = vec4(color, 1.0);
}
