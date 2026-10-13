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
uniform sampler2D rteDistortion; // Screen space displacement in pixels (shockwaves).
uniform sampler2D rteEmissive; // Glows in gamma space; hot areas shimmer.
uniform bool rteDistortionEnabled;
uniform float rteHeatHaze; // Shimmer in pixels at full heat.
uniform bool rteHazeFromHeat; // The haze reads the glow buffer's heat (its alpha), so only hot things shimmer; else its brightness, so anything bright does.
uniform float rteTime; // Seconds of sim time.
uniform int rteDebugView; // 5 shows the distortion.
uniform float rteTemperature;
uniform float rteTint;
uniform float rteContrast;
uniform vec3 rteShadowTint;
uniform vec3 rteHighlightTint;
uniform float rteFilmGrain;
uniform float rteChromaticAberration;
uniform sampler2D rteAdaptedLuminance; // 1x1, the adapted average log luminance of the scene.
uniform float rteAutoExposure; // Strength, 0 off.
uniform float rteAutoExposureLow; // Average luminance range where exposure stays put.
uniform float rteAutoExposureHigh;
uniform sampler2D rteOutlineRows; // Unit outlines: per pixel, the distance along its row to the nearest unit pixel and that unit's slot (see UnitOutlineRow.frag).
uniform float rteOutlineWidth; // In pixels; 0 for no outlines.
uniform int rteOutlineRadius; // How far the search reaches, in whole pixels, at most 12.
uniform float rteOutlineOpacity;
uniform bool rteOutlineTeamColor; // Each outline in its side's colour, else all in rteOutlineColor.
uniform vec3 rteOutlineColor; // In display (gamma) space.
uniform vec3 rteOutlineSideColors[5]; // By slot - 1: no team, then teams 1 to 4. In display space.
uniform float rteHighlightWidth; // A highlighted unit's glow (slot 6), in pixels; 0 for none.
uniform float rteNeonGlow; // A screen style's neon glow, 0 to 1: the bloom turns hot pink from warm lights and cyan from cool ones.
uniform int rteLook; // LightingSettings::ScreenStyles: whose colours the neon glow and the terminal readout take, 1 Synthwave's, 2 Hackerman's phosphor green.
uniform float rteTerminalOverlay; // Faint scrolling hex down the screen's edges and a blinking cursor, 0 to 1.
uniform int rtePalette; // LightingSettings::RetroPalettes: 0 none, 1 synthwave, 2 vaporwave, 3 Miami, 4 Game Boy, 5 CGA, 6 sepia, 7 thermal, 8 night vision, 9 terminal green.
uniform float rtePaletteStrength; // How far the picture is remapped onto the palette, 0 to 1.
uniform float rtePosterize; // Fewer colour levels, 0 (as is) to 1 (four a channel).
uniform float rteDither; // Ordered dithering across those levels (or a palette's bands), 0 to 1.

vec3 Shoulder(vec3 color) {
	if (rteShoulderStart >= 0.999) {
		return color;
	}
	vec3 over = max(color - rteShoulderStart, vec3(0.0));
	float range = 1.0 - rteShoulderStart;
	vec3 compressed = rteShoulderStart + range * (1.0 - exp(-over / range));
	return mix(color, compressed, step(rteShoulderStart, color));
}

// A colour ramp through up to five stops, evenly spaced over t 0 to 1.
vec3 Ramp5(float t, vec3 a, vec3 b, vec3 c, vec3 d, vec3 e) {
	t = clamp(t, 0.0, 1.0) * 4.0;
	if (t < 1.0) return mix(a, b, t);
	if (t < 2.0) return mix(b, c, t - 1.0);
	if (t < 3.0) return mix(c, d, t - 2.0);
	return mix(d, e, t - 3.0);
}

// The 4x4 Bayer matrix, -0.5 to 0.5, for ordered dithering.
float Bayer4(ivec2 pixel) {
	int index = (pixel.x & 3) + (pixel.y & 3) * 4;
	const float matrix[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
	return matrix[index] / 16.0 - 0.46875;
}

// Hex glyphs 0 to F, 5 wide by 7 tall, a row to an int with the leftmost pixel in bit 4 (as RetroScene.frag's).
const int c_HexGlyphs[112] = int[112](14, 17, 19, 21, 25, 17, 14, 4, 12, 4, 4, 4, 4, 14, 14, 17, 1, 2, 4, 8, 31, 31, 2, 4, 2, 1, 17, 14, 2, 6, 10, 18, 31, 2, 2, 31, 16, 30, 1, 1, 17, 14, 6, 8, 16, 30, 17, 17, 14, 31, 1, 2, 4, 8, 8, 8, 14, 17, 17, 14, 17, 17, 14, 14, 17, 17, 15, 1, 2, 12, 14, 17, 17, 31, 17, 17, 17, 30, 17, 17, 30, 17, 17, 30, 14, 17, 16, 16, 16, 17, 14, 28, 18, 17, 17, 17, 18, 28, 31, 16, 16, 30, 16, 16, 31, 31, 16, 16, 30, 16, 16, 16);

float GlyphHash(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// The terminal readout at screen pixel px (y down): how much of it is lit here, 0 to 1. Columns of hex scroll up the left and right edges, each line
// its own length (the right side's ranged right), with a blinking block cursor near the bottom left.
float TerminalReadout(vec2 px) {
	vec2 cellSize = vec2(6.0, 9.0);
	vec2 cell = floor(px / cellSize);
	ivec2 inCell = ivec2(px - cell * cellSize);
	float columns = floor(rteScreenSize.x / cellSize.x);
	float rows = floor(rteScreenSize.y / cellSize.y);
	if (cell.x == 1.0 && cell.y == rows - 2.0) {
		return step(0.5, fract(rteTime * 1.6)) * step(float(inCell.y), 6.0) * step(float(inCell.x), 4.0);
	}
	bool left = cell.x >= 1.0 && cell.x < 15.0;
	bool right = cell.x >= columns - 15.0 && cell.x < columns - 1.0;
	if (!(left || right) || cell.y < 1.0 || cell.y > rows - 4.0) {
		return 0.0;
	}
	float side = left ? 0.0 : 1.0;
	float line = cell.y + floor(rteTime * 2.5);
	float place = left ? cell.x - 1.0 : (columns - 2.0) - cell.x;
	float lineLength = floor(mix(3.0, 14.0, GlyphHash(vec2(line, side))));
	if (place >= lineLength || GlyphHash(vec2(line, side + 7.0)) < 0.2) {
		return 0.0;
	}
	int glyph = int(GlyphHash(vec2(line * 16.0 + place, side + 3.0)) * 16.0);
	if (inCell.x > 4 || inCell.y > 6) {
		return 0.0;
	}
	return float((c_HexGlyphs[clamp(glyph, 0, 15) * 7 + inCell.y] >> (4 - inCell.x)) & 1);
}

// The picture (display space) remapped onto a retro palette by its brightness.
vec3 RetroPalette(vec3 color, int palette, float dither) {
	float luminance = clamp(dot(color, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.0);
	if (palette == 1) {
		// Synthwave: indigo blacks, violet shadows, hot magenta mids, cyan highlights, white-hot peaks. A little of the scene's own hue rides along.
		vec3 ramp = Ramp5(luminance, vec3(0.04, 0.01, 0.12), vec3(0.24, 0.04, 0.42), vec3(0.92, 0.16, 0.62), vec3(0.22, 0.86, 1.0), vec3(1.0, 0.96, 1.0));
		return mix(ramp, ramp * (0.6 + 0.8 * color / max(luminance, 0.05)), 0.25);
	} else if (palette == 2) {
		return Ramp5(luminance, vec3(0.12, 0.06, 0.24), vec3(0.46, 0.32, 0.78), vec3(0.98, 0.56, 0.82), vec3(0.62, 0.94, 0.92), vec3(1.0, 0.98, 0.96));
	} else if (palette == 3) {
		return Ramp5(luminance, vec3(0.02, 0.06, 0.1), vec3(0.0, 0.42, 0.48), vec3(0.06, 0.8, 0.78), vec3(1.0, 0.46, 0.44), vec3(1.0, 0.94, 0.84));
	} else if (palette == 4) {
		// Game Boy: four greens, banded, with the dither between bands.
		float level = clamp(floor(luminance * 3.999 + dither), 0.0, 3.0);
		vec3 shades[4] = vec3[4](vec3(0.06, 0.22, 0.06), vec3(0.19, 0.38, 0.19), vec3(0.55, 0.67, 0.06), vec3(0.61, 0.74, 0.06));
		return shades[int(level)];
	} else if (palette == 5) {
		// CGA palette 1, high intensity: black, cyan, magenta, white. Bluish things go cyan and reddish ones magenta in the middle band.
		float level = clamp(floor(luminance * 2.999 + dither), 0.0, 2.0);
		if (level < 0.5) return vec3(0.0);
		if (level > 1.5) return vec3(1.0);
		return color.b + color.g > color.r * 1.6 ? vec3(0.33, 1.0, 1.0) : vec3(1.0, 0.33, 1.0);
	} else if (palette == 6) {
		return Ramp5(luminance, vec3(0.08, 0.05, 0.03), vec3(0.3, 0.2, 0.12), vec3(0.6, 0.45, 0.3), vec3(0.86, 0.74, 0.56), vec3(1.0, 0.96, 0.86));
	} else if (palette == 7) {
		return Ramp5(luminance, vec3(0.0, 0.0, 0.08), vec3(0.2, 0.0, 0.6), vec3(0.85, 0.05, 0.3), vec3(1.0, 0.6, 0.0), vec3(1.0, 1.0, 0.8));
	} else if (palette == 9) {
		// A green phosphor terminal.
		return Ramp5(luminance, vec3(0.0, 0.02, 0.0), vec3(0.0, 0.2, 0.05), vec3(0.08, 0.62, 0.2), vec3(0.3, 1.0, 0.42), vec3(0.85, 1.0, 0.88));
	} else if (palette == 8) {
		float boosted = pow(luminance, 0.6);
		return vec3(0.12, 1.0, 0.25) * boosted + vec3(0.0, 0.03, 0.0);
	}
	return color;
}

void main() {
	vec2 uv = gl_FragCoord.xy / rteScreenSize;
	vec2 offsetPixels = vec2(0.0);
	if (rteDistortionEnabled) {
		// Shockwaves push the scene outwards.
		offsetPixels -= texture(rteDistortion, uv).xy;
		// Heat haze: sample the glows a little below, so the shimmer rises above hot things.
		vec4 glow = texture(rteEmissive, uv + vec2(0.0, 5.0) / rteScreenSize);
		float heat = rteHazeFromHeat ? glow.a : dot(glow.rgb, vec3(0.3333));
		if (heat > 0.01) {
			vec2 p = gl_FragCoord.xy * vec2(0.21, 0.13) + vec2(0.0, rteTime * 4.0);
			vec2 wobble = vec2(sin(p.y * 1.7 + sin(p.x * 0.9) * 2.0), cos(p.x * 1.3 + sin(p.y * 1.1 + rteTime) * 2.0));
			offsetPixels += wobble * min(heat, 1.0) * rteHeatHaze;
		}
	}
	if (rteDebugView == 5) {
		FragColor = vec4(abs(offsetPixels) * 0.25, 0.0, 1.0);
		return;
	}
	vec2 sampleUV = uv + offsetPixels / rteScreenSize;
	vec3 color;
	if (rteChromaticAberration > 0.0) {
		// Fringing grows towards the edges of the screen.
		vec2 fringe = (uv - 0.5) * 2.0 * rteChromaticAberration / rteScreenSize;
		color = vec3(texture(rteScene, sampleUV + fringe).r, texture(rteScene, sampleUV).g, texture(rteScene, sampleUV - fringe).b);
	} else {
		color = texture(rteScene, sampleUV).rgb;
	}
	vec3 bloom = texture(rteBloom, sampleUV).rgb;
	if (rteNeonGlow > 0.0) {
		// Neon: warm glows (fire, blasts, the sun) bloom hot pink, cool ones (lamps, lasers, sky) cyan, and all of it a little wider and brighter.
		float bloomLuminance = dot(bloom, vec3(0.2126, 0.7152, 0.0722));
		float warmth = smoothstep(-0.15, 0.25, (bloom.r - bloom.b) / max(bloomLuminance, 0.001));
		vec3 neon = (rteLook == 2 ? vec3(0.25, 1.0, 0.4) : mix(vec3(0.2, 0.85, 1.0), vec3(1.0, 0.22, 0.72), warmth)) * bloomLuminance * 1.4;
		bloom = mix(bloom, neon, rteNeonGlow * 0.75);
	}
	color += bloom * rteBloomIntensity;
	color *= rteExposure;
	if (rteAutoExposure > 0.0) {
		// Only scenes far outside the usual brightness range adapt, so the pixel art keeps its look the rest of the time.
		float average = exp(texture(rteAdaptedLuminance, vec2(0.5)).r);
		float target = clamp(average, rteAutoExposureLow, rteAutoExposureHigh);
		color *= clamp(pow(target / max(average, 0.0001), rteAutoExposure), 0.5, 2.0);
	}

	float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
	color = max(mix(vec3(luminance), color, rteSaturation), vec3(0.0));

	color = Shoulder(color);

	// Grading, in linear space: white balance, split toning, contrast.
	color *= vec3(1.0 + 0.12 * rteTemperature, 1.0 - 0.08 * rteTint, 1.0 - 0.12 * rteTemperature) * vec3(1.0, 1.0 + 0.04 * rteTint, 1.0);
	float gradeLuminance = clamp(dot(color, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.0);
	color *= mix(rteShadowTint, rteHighlightTint, smoothstep(0.05, 0.6, gradeLuminance));
	color = max(pow(max(color, vec3(0.0)) / 0.18, vec3(rteContrast)) * 0.18, vec3(0.0));

	vec2 centered = uv - 0.5;
	color *= 1.0 - rteVignette * smoothstep(0.35, 0.85, length(centered * vec2(rteScreenSize.x / rteScreenSize.y, 1.0)));

	vec3 outputColor = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));
	if (rtePaletteStrength > 0.0 || rtePosterize > 0.0) {
		float ditherOffset = Bayer4(ivec2(gl_FragCoord.xy)) * rteDither;
		if (rtePaletteStrength > 0.0) {
			// Night vision brings its own noise and a scope's dark rim.
			vec3 mapped = RetroPalette(outputColor, rtePalette, ditherOffset);
			if (rtePalette == 8) {
				float noise = fract(sin(dot(gl_FragCoord.xy + fract(rteTime * 7.31) * 100.0, vec2(12.9898, 78.233))) * 43758.5453);
				mapped *= 0.82 + 0.3 * noise;
				mapped *= 1.0 - smoothstep(0.42, 0.75, length((uv - 0.5) * vec2(rteScreenSize.x / rteScreenSize.y, 1.0)));
			}
			outputColor = mix(outputColor, mapped, rtePaletteStrength);
		}
		if (rtePosterize > 0.0) {
			float levels = mix(32.0, 4.0, rtePosterize);
			outputColor = clamp(floor(outputColor * (levels - 1.0) + 0.5 + ditherOffset) / (levels - 1.0), 0.0, 1.0);
		}
	}
	if (rteTerminalOverlay > 0.0) {
		float lit = TerminalReadout(vec2(gl_FragCoord.x, rteScreenSize.y - gl_FragCoord.y));
		vec3 ink = rteLook == 2 ? vec3(0.35, 1.0, 0.45) : vec3(0.4, 0.9, 1.0);
		outputColor = mix(outputColor, ink, lit * rteTerminalOverlay * 0.55);
	}
	if (rteFilmGrain > 0.0) {
		// Grain in gamma space, strongest in the mid tones, changing every frame.
		float noise = fract(sin(dot(gl_FragCoord.xy + fract(rteTime * 13.17) * 100.0, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
		float midtones = 1.0 - abs(dot(outputColor, vec3(0.333)) * 2.0 - 1.0);
		outputColor += noise * rteFilmGrain * 0.12 * midtones;
	}
	if ((rteOutlineWidth > 0.0 || rteHighlightWidth > 0.0) && rteDebugView == 0) {
		// Unit outlines: finish UnitOutlineRow.frag's search down this column for the nearest unit pixel. Not on the unit itself (its distance
		// is 0) and only where the stroke may go (B). Full strength out to the width, then fading over one pixel, which softens the corners.
		ivec2 pixel = ivec2(gl_FragCoord.xy);
		vec4 here = texelFetch(rteOutlineRows, pixel, 0);
		if (here.b > 0.5 && here.r > 0.5 / 255.0) {
			int rows = textureSize(rteOutlineRows, 0).y;
			float nearest = 1.0e6;
			int slot = 0;
			for (int dy = -12; dy <= 12; ++dy) {
				int y = pixel.y + dy;
				if (abs(dy) > rteOutlineRadius || y < 0 || y >= rows) {
					continue;
				}
				vec4 row = texelFetch(rteOutlineRows, ivec2(pixel.x, y), 0);
				int rowSlot = int(row.g * 255.0 + 0.5);
				if (rowSlot == 0) {
					continue;
				}
				float dx = row.r * 255.0;
				float squared = dx * dx + float(dy * dy);
				if (squared < nearest) {
					nearest = squared;
					slot = rowSlot;
				}
			}
			if (slot == 6) {
				// A highlighted unit (a flag carrier, a VIP): a bright stroke hard against it, glowing out to the width and pulsing. A soft pink,
				// a colour no team has (red, green, blue and yellow).
				float distance = sqrt(nearest);
				float stroke = 1.0 - smoothstep(1.5, 2.5, distance);
				float glow = 1.0 - smoothstep(0.0, rteHighlightWidth, distance);
				float pulse = 0.7 + 0.3 * sin(rteTime * 6.0);
				vec3 hot = vec3(0.55, 0.22, 0.38);
				outputColor = mix(outputColor, vec3(0.9, 0.55, 0.72), stroke);
				outputColor = clamp(outputColor + hot * glow * glow * pulse * (1.0 - stroke), 0.0, 1.0);
			} else if (slot >= 7) {
				// A unit highlighted in its side's colour (a VIP): a stroke in the side's colour hard against it, a second one in the pink round
				// that, then a glow in the side's colour out to the width. Laid on part see-through so the unit and what's round it still show,
				// and pulsing harder than the pink highlight so it stands out from plain outlines.
				float distance = sqrt(nearest);
				float pulse = 0.5 + 0.5 * sin(rteTime * 5.0);
				vec3 side = mix(rteOutlineSideColors[clamp(slot - 7, 0, 4)], vec3(1.0), 0.25);
				float strength = mix(0.45, 0.8, pulse);
				float inner = 1.0 - smoothstep(1.5, 2.5, distance);
				float outer = (1.0 - smoothstep(3.0, 4.0, distance)) * (1.0 - inner);
				float glow = 1.0 - smoothstep(0.0, rteHighlightWidth, distance);
				outputColor = mix(outputColor, side, inner * strength);
				outputColor = mix(outputColor, vec3(0.9, 0.55, 0.72), outer * strength);
				outputColor = clamp(outputColor + side * 0.6 * glow * glow * mix(0.3, 1.0, pulse) * (1.0 - inner - outer), 0.0, 1.0);
			} else if (slot > 0 && rteOutlineWidth > 0.0) {
				float coverage = 1.0 - smoothstep(rteOutlineWidth, rteOutlineWidth + 1.0, sqrt(nearest));
				vec3 stroke = rteOutlineTeamColor ? rteOutlineSideColors[clamp(slot - 1, 0, 4)] : rteOutlineColor;
				outputColor = mix(outputColor, stroke, coverage * rteOutlineOpacity);
			}
		}
	}
	FragColor = vec4(clamp(outputColor, 0.0, 1.0), 1.0);
}
