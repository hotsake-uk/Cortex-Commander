// RetroScene.frag
// A screen style's own sky and the edge lines (LightingSettings::RetroLayers), on the lit HDR scene before bloom, so the sun, the grid and neon lines glow.
// Synthwave's sky: behind the terrain, an Outrun sunset (orange at the horizon through pink and purple to navy, darker and starrier at night), the big
// striped sun sitting on the horizon, and a magenta grid racing towards it across the ground below. Distant background layers that scroll (hills,
// mountains) turn to dark violet silhouettes with a glowing rim; the furthest ones (the sky art) are replaced outright.
// Hackerman's sky: columns of green code falling over a faint grid, a bright glyph at the head of each, fading up its tail; distant hills become green
// wireframes. Comic keeps the original sky and hills, flattened into bold bands with a dot gradient up the sky and an ink line along the hill tops,
// and its ink lines are bold at silhouettes and light inside them. The Game Boy's sky is flat bands of its shades with a checkered dither between them,
// a few stars at night, and the distant hills flattened to two shades with a dark line along their tops. ASCII mode (and any style) can draw the distant
// background or everything as ASCII art: text characters picked by brightness, with line characters along outlines, in the look's colours, the scene's own,
// a phosphor's or as ink on paper.
#version 330 core

in vec2 textureUV;
out vec4 FragColor;

uniform sampler2D rteScene;
uniform sampler2D rteSceneDepth;
uniform vec2 rteScreenSize;
uniform vec2 rteScreenOrigin;
uniform float rteTime;
uniform int rteLook; // LightingSettings::ScreenStyles: 1 Synthwave's sky and neon, 2 Hackerman's code rain and phosphor green, 3 Comic's printed sky and ink, 4 Game Boy's banded sky.
uniform float rteExposure; // The tonemap's exposure, so the Game Boy sky lands on the shades it means to.
uniform float rteSky; // How fully the sky is replaced, 0 to 1.
uniform float rteGridSpeed; // How fast the grid scrolls, 0 to 3.
uniform float rteDaylight; // The sky light's brightness now, 0 (night) to 1 (day).
uniform float rteEdges; // Edge lines, 0 to 1.
uniform bool rteInkEdges; // Black ink lines; else glowing neon.
uniform bool rteHasDepth;
uniform float rteBackgroundDepth; // Depth beyond which pixels are distant background layers.
uniform float rteBackgroundNearDepth; // The nearest and furthest background layers' depth.
uniform float rteBackgroundFarDepth;
uniform float rteASCII; // The whole picture as ASCII art, 0 to 1.
uniform float rteASCIIBackground; // The distant background (not the replaced sky) as ASCII art, 0 to 1.
uniform float rteAsciiScale; // How many game pixels a character's pixel is, 1 to 4.
uniform float rteAsciiEdges; // How readily outlines get line characters, 0 to 1.
uniform float rteAsciiContrast; // How far the shading is spread between sparse and dense characters, 0.5 to 2.
uniform float rteAsciiExposure; // How bright the picture is read as, 0.25 to 3.
uniform int rteAsciiColours; // LightingSettings::AsciiColours: 0 the look's, 1 the scene's, 2 green, 3 amber, 4 white phosphor, 5 ink on paper.
uniform float rteAsciiFill; // A tint behind each character, 0 to 1.
uniform float rteAsciiGlow; // Bright characters drawn brighter, so they bloom, 0 to 1.
uniform float rteAsciiChurn; // Bright parts flicker through changing characters, 0 to 1.
uniform bool rteAsciiWorldLocked; // The characters' grid moves with the battlefield; else fixed to the screen.

const float c_SkyLevel = 0.5; // The sky's colours are made for display; this brings them to the scene's linear level before exposure.

// Glyphs, 5 wide by 7 tall, a row to an int with the leftmost pixel in bit 4. ASCII art: 16 by how much of the cell they fill, " .-:+=o%a&*$8B#@", then
// the line characters "|-/\". Hex: 0 to F.
const int c_AsciiGlyphs[140] = int[140](0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 12, 12, 0, 0, 0, 31, 0, 0, 0, 0, 12, 12, 0, 12, 12, 0, 0, 4, 4, 31, 4, 4, 0, 0, 0, 31, 0, 31, 0, 0, 0, 0, 14, 17, 17, 17, 14, 24, 25, 2, 4, 8, 19, 3, 0, 0, 14, 1, 15, 17, 15, 12, 18, 20, 8, 21, 18, 13, 0, 21, 14, 31, 14, 21, 0, 4, 15, 20, 14, 5, 30, 4, 14, 17, 17, 14, 17, 17, 14, 30, 17, 17, 30, 17, 17, 30, 10, 10, 31, 10, 31, 10, 10, 14, 17, 23, 21, 23, 16, 15, 4, 4, 4, 4, 4, 4, 4, 0, 0, 0, 31, 0, 0, 0, 1, 1, 2, 4, 8, 16, 16, 16, 16, 8, 4, 2, 1, 1);
const int c_HexGlyphs[112] = int[112](14, 17, 19, 21, 25, 17, 14, 4, 12, 4, 4, 4, 4, 14, 14, 17, 1, 2, 4, 8, 31, 31, 2, 4, 2, 1, 17, 14, 2, 6, 10, 18, 31, 2, 2, 31, 16, 30, 1, 1, 17, 14, 6, 8, 16, 30, 17, 17, 14, 31, 1, 2, 4, 8, 8, 8, 14, 17, 17, 14, 17, 17, 14, 14, 17, 17, 15, 1, 2, 12, 14, 17, 17, 31, 17, 17, 17, 30, 17, 17, 30, 17, 17, 30, 14, 17, 16, 16, 16, 17, 14, 28, 18, 17, 17, 17, 18, 28, 31, 16, 16, 30, 16, 16, 31, 31, 16, 16, 30, 16, 16, 16);
const vec2 c_GlyphCell = vec2(6.0, 9.0); // A glyph and the gap after it, in the game's pixels.

float Hash(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// How much of the sky art this depth is: 1 for the furthest layers (they scroll least: the sky) and where nothing was drawn, 0 for nearer background layers.
float SkyArt(float depth) {
	float farness = (depth - rteBackgroundNearDepth) / max(rteBackgroundFarDepth - rteBackgroundNearDepth, 0.0001);
	return depth >= 0.9999 ? 1.0 : smoothstep(0.72, 0.88, farness);
}

vec3 Ramp4(float t, vec3 a, vec3 b, vec3 c, vec3 d) {
	t = clamp(t, 0.0, 1.0) * 3.0;
	if (t < 1.0) return mix(a, b, t);
	if (t < 2.0) return mix(b, c, t - 1.0);
	return mix(c, d, t - 2.0);
}

// The Outrun sky at screen position p (0 to 1, y down), in display colours.
vec3 SynthwaveSky(vec2 p, float aspect) {
	float day = smoothstep(0.05, 0.6, rteDaylight);
	float horizon = 0.62;
	vec3 color;
	if (p.y < horizon) {
		float up = (horizon - p.y) / horizon;
		vec3 dusk = Ramp4(up, vec3(1.0, 0.5, 0.18), vec3(0.98, 0.2, 0.52), vec3(0.42, 0.07, 0.55), vec3(0.06, 0.02, 0.2));
		vec3 night = Ramp4(up, vec3(0.5, 0.08, 0.46), vec3(0.24, 0.04, 0.38), vec3(0.09, 0.02, 0.22), vec3(0.01, 0.0, 0.06));
		color = mix(night, dusk, day);
		// Stars, fixed to the far sky, twinkling, thicker higher up and at night.
		vec2 cell = floor(p * vec2(aspect, 1.0) * 180.0);
		float star = step(0.993 - 0.004 * (1.0 - day), Hash(cell));
		float twinkle = 0.5 + 0.5 * sin(rteTime * (2.0 + 3.0 * Hash(cell + 7.0)) + Hash(cell + 3.0) * 6.28);
		color += vec3(1.0, 0.9, 1.0) * star * twinkle * smoothstep(0.15, 0.7, up) * mix(1.0, 0.35, day);
		// The sun: a disc sitting on the horizon, yellow at the top to hot pink at the bottom, its lower half cut by bands that widen as they go down and
		// slide slowly downwards. Dimmer at night, like a low red moon.
		vec2 sunCenter = vec2(0.5, horizon - 0.13);
		float sunRadius = 0.2;
		vec2 offset = (p - sunCenter) * vec2(aspect, 1.0);
		float distance = length(offset);
		float down = (p.y - sunCenter.y) / sunRadius;
		float bands = 1.0;
		if (down > -0.1) {
			float band = fract((p.y - sunCenter.y) * 34.0 - rteTime * 0.35);
			float gap = clamp((down + 0.1) * 0.55, 0.0, 0.6);
			bands = step(gap, band);
		}
		float disc = (1.0 - smoothstep(sunRadius - 0.004, sunRadius, distance)) * bands;
		vec3 sunColor = mix(vec3(1.0, 0.92, 0.35), vec3(1.0, 0.18, 0.62), smoothstep(-0.9, 0.9, down)) * mix(0.55, 1.0, day);
		float halo = exp(-max(distance - sunRadius, 0.0) * 9.0) * 0.45;
		color = mix(color, sunColor * 2.6, disc) + vec3(1.0, 0.3, 0.6) * halo * mix(0.4, 1.0, day);
	} else {
		// The ground: dark violet glowing towards the horizon, crossed by the grid.
		float below = (p.y - horizon) / (1.0 - horizon);
		color = mix(vec3(0.55, 0.08, 0.5), vec3(0.04, 0.0, 0.08), smoothstep(0.0, 0.5, below)) * mix(0.6, 1.0, day);
		float perspective = 1.0 / max(p.y - horizon, 0.002);
		// Pans with the camera, as ground does, and races towards the horizon.
		vec2 grid = vec2((p.x - 0.5) * aspect * perspective * 0.9 + rteScreenOrigin.x * 0.004, perspective * 0.12 + rteTime * rteGridSpeed * 1.2);
		vec2 width = fwidth(grid) * 1.2;
		vec2 line = 1.0 - smoothstep(vec2(0.0), width, min(fract(grid), 1.0 - fract(grid)));
		vec2 glow = 1.0 - smoothstep(vec2(0.0), width * 5.0, min(fract(grid), 1.0 - fract(grid)));
		float fade = smoothstep(0.0, 0.08, below);
		float lines = max(line.x, line.y) + 0.35 * max(glow.x, glow.y);
		color += vec3(1.0, 0.16, 0.82) * lines * fade * 2.4;
		// The glow along the horizon line itself.
		color += vec3(1.0, 0.35, 0.7) * exp(-below * 40.0) * 0.8;
	}
	return color * c_SkyLevel;
}

// Whether the glyph's pixel at inCell (from its top left, y down) is lit.
float AsciiGlyph(int glyph, ivec2 inCell) {
	if (inCell.x < 0 || inCell.y < 0 || inCell.x > 4 || inCell.y > 6) return 0.0;
	return float((c_AsciiGlyphs[clamp(glyph, 0, 19) * 7 + inCell.y] >> (4 - inCell.x)) & 1);
}
float HexGlyph(int glyph, ivec2 inCell) {
	if (inCell.x < 0 || inCell.y < 0 || inCell.x > 4 || inCell.y > 6) return 0.0;
	return float((c_HexGlyphs[clamp(glyph, 0, 15) * 7 + inCell.y] >> (4 - inCell.x)) & 1);
}

// Hackerman's sky at screen pixel px (y down): code raining down columns over a faint grid, in display colours.
vec3 HackermanSky(vec2 px, vec2 size) {
	float day = smoothstep(0.05, 0.6, rteDaylight);
	vec3 color = mix(vec3(0.0, 0.006, 0.002), vec3(0.0, 0.05, 0.015), px.y / max(size.y, 1.0));
	vec2 gridLine = step(fract(px / 24.0), vec2(1.0 / 24.0));
	color += vec3(0.0, 0.06, 0.02) * max(gridLine.x, gridLine.y) * mix(0.6, 1.0, day);
	vec2 cell = floor(px / c_GlyphCell);
	ivec2 inCell = ivec2(px - cell * c_GlyphCell);
	float rows = size.y / c_GlyphCell.y;
	float brightness = 0.0;
	float head = 0.0;
	// Two streams to a column, each with its own speed, length and start.
	for (int stream = 0; stream < 2; ++stream) {
		vec2 seed = vec2(cell.x, float(stream) * 37.0);
		float speed = mix(5.0, 16.0, Hash(seed)) * rteGridSpeed;
		float tailLength = mix(6.0, 22.0, Hash(seed + 1.3));
		float span = rows + tailLength + mix(0.0, 30.0, Hash(seed + 2.7));
		float headRow = mod(rteTime * speed + Hash(seed + 4.1) * span, span);
		float behind = headRow - cell.y;
		if (behind >= 0.0 && behind < tailLength) {
			head = max(head, behind < 1.0 ? 1.0 : 0.0);
			brightness = max(brightness, behind < 1.0 ? 1.0 : pow(1.0 - behind / tailLength, 1.4) * 0.75);
		}
	}
	if (brightness > 0.0) {
		// Each glyph changes now and then, some faster than others.
		float rate = mix(0.5, 6.0, Hash(cell + 9.0));
		int glyph = int(Hash(cell + floor(rteTime * rate)) * 16.0);
		float lit = HexGlyph(glyph, inCell);
		vec3 ink = mix(vec3(0.08, 0.85, 0.28), vec3(0.8, 1.0, 0.85) * 2.2, head);
		color += ink * lit * brightness;
	}
	return color * c_SkyLevel;
}

// The scene as ASCII art at screen pixel px (y down). Each cell reads the picture at nine points across it: their average picks a character by how much
// of the cell it fills, and where they show an outline (in brightness, or where the battlefield meets the distant background) a line character along
// it takes its place. Bright cells can churn through changing characters. Coloured by rteAsciiColours.
vec3 AsciiArt(vec2 px, vec2 size) {
	float scale = max(rteAsciiScale, 1.0);
	vec2 cellSize = c_GlyphCell * scale;
	vec2 shift = rteAsciiWorldLocked ? rteScreenOrigin : vec2(0.0);
	vec2 at = px + shift;
	vec2 cell = floor(at / cellSize);
	vec2 corner = cell * cellSize - shift;
	// The glyph is 7 rows in a 9 row cell: centred, a row of gap above and below.
	ivec2 inCell = ivec2(floor((at - cell * cellSize) / scale)) - ivec2(0, 1);

	vec3 sum = vec3(0.0);
	float lum[9];
	float near[9];
	for (int y = 0; y < 3; ++y) {
		for (int x = 0; x < 3; ++x) {
			vec2 point = corner + cellSize * (vec2(x, y) + 0.5) / 3.0;
			vec2 pointUV = clamp(vec2(point.x, size.y - point.y) / size, vec2(0.0), vec2(1.0));
			vec3 sampled = texture(rteScene, pointUV).rgb;
			sum += sampled;
			lum[y * 3 + x] = log(1.0 + 4.0 * dot(sampled, vec3(0.2126, 0.7152, 0.0722)));
			near[y * 3 + x] = rteHasDepth ? step(texture(rteSceneDepth, pointUV).r, rteBackgroundDepth) : 1.0;
		}
	}
	vec3 source = sum / 9.0;
	float level = 1.0 - exp(-dot(source, vec3(0.2126, 0.7152, 0.0722)) * 3.0 * rteAsciiExposure);
	level = clamp((level - 0.5) * rteAsciiContrast + 0.5, 0.0, 1.0);
	bool paper = rteAsciiColours == 5 || (rteAsciiColours == 0 && rteLook == 3);

	// Ink on paper: the darker, the denser.
	int glyph = int(clamp(paper ? 1.0 - level : level, 0.0, 0.999) * 16.0);
	bool line = false;
	if (rteAsciiEdges > 0.0) {
		// Across the cell, y down: the brightness's slope, and the battlefield's edge against the background counted as a strong one.
		float gx = (lum[2] + 2.0 * lum[5] + lum[8]) - (lum[0] + 2.0 * lum[3] + lum[6]);
		float gy = (lum[6] + 2.0 * lum[7] + lum[8]) - (lum[0] + 2.0 * lum[1] + lum[2]);
		float nx = (near[2] + 2.0 * near[5] + near[8]) - (near[0] + 2.0 * near[3] + near[6]);
		float ny = (near[6] + 2.0 * near[7] + near[8]) - (near[0] + 2.0 * near[1] + near[2]);
		vec2 slope = vec2(gx, gy) + vec2(nx, ny) * 1.5;
		float steepness = length(slope);
		if (steepness > mix(3.0, 0.45, rteAsciiEdges)) {
			// The outline runs across the slope: a slope left to right is an upright line, top to bottom a level one, else a diagonal.
			vec2 across = abs(slope / steepness);
			line = true;
			glyph = across.x > 0.92 ? 16 : (across.y > 0.92 ? 17 : (slope.x * slope.y > 0.0 ? 18 : 19));
		}
	}
	float lit;
	float churn = rteAsciiChurn * smoothstep(0.45, 0.9, level);
	float rate = mix(2.0, 12.0, Hash(cell + 5.0));
	if (!line && churn > 0.0 && Hash(cell + floor(rteTime * rate * 0.25) * 1.7) < churn) {
		lit = HexGlyph(int(Hash(cell + floor(rteTime * rate) + 3.0) * 16.0), inCell);
	} else {
		lit = AsciiGlyph(glyph, inCell);
	}

	if (paper) {
		vec3 sheet = vec3(0.86, 0.82, 0.72) * mix(1.0, 0.55 + 0.45 * level, rteAsciiFill);
		return mix(sheet, vec3(0.05, 0.045, 0.07), lit) * c_SkyLevel * 1.4;
	}
	vec3 ink;
	if (rteAsciiColours == 1) {
		// The scene's own hue at full strength, richened a little so it doesn't wash out to white.
		float peak = max(max(source.r, source.g), source.b);
		ink = peak > 0.0001 ? pow(source / peak, vec3(1.25)) : vec3(1.0);
	} else if (rteAsciiColours == 2 || (rteAsciiColours == 0 && rteLook == 2)) {
		ink = vec3(0.15, 1.0, 0.35);
	} else if (rteAsciiColours == 3) {
		ink = vec3(1.0, 0.62, 0.12);
	} else if (rteAsciiColours == 4) {
		ink = vec3(0.85, 0.9, 1.0);
	} else {
		ink = mix(vec3(1.0, 0.25, 0.75), vec3(0.25, 0.9, 1.0), px.y / max(size.y, 1.0));
	}
	// Outlines show even in the dark.
	float shade = 0.15 + 0.85 * (line ? max(level, 0.45) : level);
	vec3 character = ink * shade * (1.0 + rteAsciiGlow * 2.5 * level * level);
	vec3 fill = ink * shade * rteAsciiFill * 0.35;
	return mix(fill, character, lit) * c_SkyLevel * 1.4;
}

void main() {
	vec2 uv = textureUV;
	vec4 scene = texture(rteScene, uv);
	vec3 color = scene.rgb;
	float aspect = rteScreenSize.x / max(rteScreenSize.y, 1.0);
	vec2 screen = vec2(uv.x, 1.0 - uv.y);
	float depth = rteHasDepth ? texture(rteSceneDepth, uv).r : 0.0;
	bool background = rteHasDepth && depth > rteBackgroundDepth;

	if (rteSky > 0.0 && background && rteLook == 4) {
		// Game Boy: the levels here are display brightness, which the Game Boy palette then bands into its four shades (Tonemap.frag): about 0.1,
		// 0.29, 0.42 and 0.8 sit in the middle of each, and 0.494 on the line between the two lightest, where the dither checkers them evenly.
		float skyArt = SkyArt(depth);
		vec2 px = screen * rteScreenSize;
		float day = smoothstep(0.15, 0.6, rteDaylight);
		// The sky: a darker band along the top, a checkered band, then the lightest shade down to the hills; two shades darker at night, with stars.
		float lower = step(0.3, screen.y);
		float checker = mod(floor(px.x) + floor(px.y), 2.0);
		float band = screen.y > 0.22 && screen.y < 0.3 ? checker : lower;
		float sky = mix(mix(0.1, 0.29, band), mix(0.42, 0.8, band), day);
		float star = step(0.9985, Hash(floor(px))) * (1.0 - day) * (1.0 - lower);
		sky = mix(sky, 0.8, star);
		// The hills: the brighter of their art the lighter of the two middle shades (the two darkest at night), and the darkest along their tops.
		float level = dot(scene.rgb, vec3(0.2126, 0.7152, 0.0722)) * rteExposure;
		float lit = step(0.18, level / (level + 0.15));
		float hill = mix(mix(0.1, 0.29, lit), mix(0.29, 0.42, lit), day);
		vec2 pixel = 1.0 / rteScreenSize;
		float above = SkyArt(texture(rteSceneDepth, uv + vec2(0.0, 1.5 * pixel.y)).r);
		hill = mix(hill, 0.04, smoothstep(0.3, 0.9, above) * (1.0 - skyArt));
		float shown = mix(hill, sky, step(0.5, skyArt));
		color = mix(color, vec3(pow(shown, 2.2) / max(rteExposure, 0.01)), rteSky);
	} else if (rteSky > 0.0 && background && rteLook == 3) {
		// Comic: the original art, its light flattened into three hue-true bands, Ben-Day dots thickening up the sky, and a black ink line along the
		// top of every hill where the sky shows above it.
		float skyArt = SkyArt(depth);
		vec2 px = screen * rteScreenSize;
		float level = max(max(scene.r, scene.g), scene.b);
		vec3 banded = scene.rgb;
		if (level > 0.0005) {
			float band = clamp(floor(level / (level + 0.5) * 3.0 + 0.5) / 3.0, 0.08, 0.85);
			banded = scene.rgb * (0.5 * band / (1.0 - band) / level);
		}
		vec2 grid = mat2(0.7071, -0.7071, 0.7071, 0.7071) * px / 6.0;
		float dotRadius = mix(0.04, 0.36, 1.0 - smoothstep(0.0, 0.7, screen.y)) * skyArt;
		float printedDot = 1.0 - smoothstep(dotRadius - 0.06, dotRadius + 0.06, length(fract(grid) - 0.5));
		banded *= 1.0 - 0.22 * printedDot;
		vec2 pixel = 1.0 / rteScreenSize;
		float above = max(SkyArt(texture(rteSceneDepth, uv + vec2(0.0, 1.5 * pixel.y)).r), SkyArt(texture(rteSceneDepth, uv + vec2(0.0, 3.0 * pixel.y)).r));
		float rim = smoothstep(0.3, 0.9, above) * (1.0 - skyArt);
		color = mix(color, mix(banded, vec3(0.005), rim), rteSky);
	} else if (rteSky > 0.0 && background) {
		float skyArt = SkyArt(depth);
		vec2 px = screen * rteScreenSize;
		vec3 sky = rteLook == 2 ? HackermanSky(px, rteScreenSize) : SynthwaveSky(screen, aspect);
		// Nearer background layers: dark silhouettes keeping a trace of their shading, with a rim of light where the sky shows just above. Synthwave's
		// are violet with a pink rim; Hackerman's green wireframes, filled with faint scan lines.
		float shade = clamp(dot(scene.rgb, vec3(0.2126, 0.7152, 0.0722)) * 2.0, 0.0, 1.0);
		vec2 pixel = 1.0 / rteScreenSize;
		float above = SkyArt(texture(rteSceneDepth, uv + vec2(0.0, 2.0 * pixel.y)).r);
		float rim = smoothstep(0.3, 0.9, above) * (1.0 - skyArt);
		vec3 silhouette;
		if (rteLook == 2) {
			float scan = step(fract(px.y / 3.0), 0.34);
			silhouette = vec3(0.0, 0.02, 0.006) + vec3(0.02, 0.22, 0.07) * scan * (0.3 + 0.7 * shade) + vec3(0.2, 1.0, 0.4) * rim * 1.4;
		} else {
			silhouette = vec3(0.07, 0.015, 0.14) * (0.4 + 0.8 * shade) + vec3(1.0, 0.25, 0.75) * rim * 1.6;
		}
		vec3 replaced = mix(silhouette, sky, skyArt);
		color = mix(color, replaced, rteSky);
	}

	if ((rteASCII > 0.0 || rteASCIIBackground > 0.0)) {
		// ASCII art: everywhere, or the distant background only (not a sky that was replaced, which has its own look).
		float amount = rteASCII;
		if (background) {
			amount = max(amount, rteASCIIBackground * (1.0 - rteSky * SkyArt(depth)));
		}
		if (amount > 0.0) {
			color = mix(color, AsciiArt(screen * rteScreenSize, rteScreenSize), amount);
		}
	}

	if (rteEdges > 0.0) {
		// Edges in the scene's brightness (on its log, so dark and bright areas count alike), and where the battlefield meets the distant background.
		vec2 pixel = 1.0 / rteScreenSize;
		// Comic ink reaches further at the silhouettes, so they're bold.
		vec2 nearStep = pixel * (rteLook == 3 ? 2.0 : 1.0);
		float lum[9];
		float near[9];
		int i = 0;
		for (int y = -1; y <= 1; ++y) {
			for (int x = -1; x <= 1; ++x) {
				vec2 at = uv + vec2(x, y) * pixel;
				lum[i] = log(1.0 + 4.0 * dot(texture(rteScene, at).rgb, vec3(0.2126, 0.7152, 0.0722)));
				near[i] = rteHasDepth ? step(texture(rteSceneDepth, uv + vec2(x, y) * nearStep).r, rteBackgroundDepth) : 1.0;
				++i;
			}
		}
		float gx = (lum[2] + 2.0 * lum[5] + lum[8]) - (lum[0] + 2.0 * lum[3] + lum[6]);
		float gy = (lum[6] + 2.0 * lum[7] + lum[8]) - (lum[0] + 2.0 * lum[1] + lum[2]);
		// Comic keeps the lines inside sprites light, so the art doesn't get noisy.
		float brightnessEdge = smoothstep(0.5, 1.1, length(vec2(gx, gy))) * (rteLook == 3 ? 0.35 : 0.7);
		float dx = (near[2] + 2.0 * near[5] + near[8]) - (near[0] + 2.0 * near[3] + near[6]);
		float dy = (near[6] + 2.0 * near[7] + near[8]) - (near[0] + 2.0 * near[1] + near[2]);
		// Only the battlefield side of the boundary, so the line hugs the terrain and not the sky.
		float silhouetteEdge = near[4] * smoothstep(0.5, 2.0, length(vec2(dx, dy)));
		float edge = clamp(max(brightnessEdge * near[4], silhouetteEdge), 0.0, 1.0) * rteEdges;
		if (rteInkEdges) {
			color *= 1.0 - 0.92 * edge;
		} else {
			// Synthwave: pink near the top of the screen to cyan near the bottom. Hackerman: phosphor green. Bright enough to bloom.
			vec3 neon = rteLook == 2 ? vec3(0.25, 1.0, 0.4) : mix(vec3(1.0, 0.2, 0.75), vec3(0.2, 0.9, 1.0), smoothstep(0.15, 0.85, screen.y));
			color = mix(color, neon * 1.8, edge * 0.85);
		}
	}
	FragColor = vec4(color, scene.a);
}
