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
uniform float rteTime; // Seconds of sim time.
uniform int rteDebugView; // 5 shows the distortion.
uniform float rteTemperature;
uniform float rteTint;
uniform float rteContrast;
uniform vec3 rteShadowTint;
uniform vec3 rteHighlightTint;
uniform float rteFilmGrain;
uniform float rteChromaticAberration;

vec3 Shoulder(vec3 color) {
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
		float heat = dot(texture(rteEmissive, uv + vec2(0.0, 5.0) / rteScreenSize).rgb, vec3(0.3333));
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
	FragColor = vec4(clamp(outputColor, 0.0, 1.0), 1.0);
}
