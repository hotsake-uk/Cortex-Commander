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

vec3 Shoulder(vec3 color) {
	if (rteShoulderStart >= 0.999) {
		return color;
	}
	vec3 over = max(color - rteShoulderStart, vec3(0.0));
	float range = 1.0 - rteShoulderStart;
	vec3 compressed = rteShoulderStart + range * (1.0 - exp(-over / range));
	return mix(color, compressed, step(rteShoulderStart, color));
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
	color += texture(rteBloom, sampleUV).rgb * rteBloomIntensity;
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
			} else if (slot > 0 && rteOutlineWidth > 0.0) {
				float coverage = 1.0 - smoothstep(rteOutlineWidth, rteOutlineWidth + 1.0, sqrt(nearest));
				vec3 stroke = rteOutlineTeamColor ? rteOutlineSideColors[clamp(slot - 1, 0, 4)] : rteOutlineColor;
				outputColor = mix(outputColor, stroke, coverage * rteOutlineOpacity);
			}
		}
	}
	FragColor = vec4(clamp(outputColor, 0.0, 1.0), 1.0);
}
