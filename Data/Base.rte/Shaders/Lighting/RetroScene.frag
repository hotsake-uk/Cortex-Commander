// RetroScene.frag
// A screen style's own sky and the edge lines (LightingSettings::RetroLayers), on the lit HDR scene before bloom, so the sun, the grid and neon lines glow.
// Synthwave's sky: behind the terrain, an Outrun sunset (orange at the horizon through pink and purple to navy, darker and starrier at night), the big
// striped sun sitting on the horizon, and a magenta grid racing towards it across the ground below. Distant background layers that scroll (hills,
// mountains) turn to dark violet silhouettes with a glowing rim; the furthest ones (the sky art) are replaced outright.
#version 330 core

in vec2 textureUV;
out vec4 FragColor;

uniform sampler2D rteScene;
uniform sampler2D rteSceneDepth;
uniform vec2 rteScreenSize;
uniform vec2 rteScreenOrigin;
uniform float rteTime;
uniform int rteStyle; // LightingSettings::ScreenStyles. The sky is Synthwave's for now, whatever the style.
uniform float rteSky; // How fully the sky is replaced, 0 to 1.
uniform float rteGridSpeed; // How fast the grid scrolls, 0 to 3.
uniform float rteDaylight; // The sky light's brightness now, 0 (night) to 1 (day).
uniform float rteEdges; // Edge lines, 0 to 1.
uniform bool rteInkEdges; // Black ink lines; else glowing neon.
uniform bool rteHasDepth;
uniform float rteBackgroundDepth; // Depth beyond which pixels are distant background layers.
uniform float rteBackgroundNearDepth; // The nearest and furthest background layers' depth.
uniform float rteBackgroundFarDepth;

const float c_SkyLevel = 0.5; // The sky's colours are made for display; this brings them to the scene's linear level before exposure.

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

void main() {
	vec2 uv = textureUV;
	vec4 scene = texture(rteScene, uv);
	vec3 color = scene.rgb;
	float aspect = rteScreenSize.x / max(rteScreenSize.y, 1.0);
	vec2 screen = vec2(uv.x, 1.0 - uv.y);
	float depth = rteHasDepth ? texture(rteSceneDepth, uv).r : 0.0;
	bool background = rteHasDepth && depth > rteBackgroundDepth;

	if (rteSky > 0.0 && background) {
		float skyArt = SkyArt(depth);
		vec3 sky = SynthwaveSky(screen, aspect);
		// Nearer background layers: dark violet silhouettes keeping a trace of their shading, with a rim of light where the sky shows just above.
		float shade = dot(scene.rgb, vec3(0.2126, 0.7152, 0.0722));
		vec3 silhouette = vec3(0.07, 0.015, 0.14) * (0.4 + 0.8 * clamp(shade * 2.0, 0.0, 1.0));
		vec2 pixel = 1.0 / rteScreenSize;
		float above = SkyArt(texture(rteSceneDepth, uv + vec2(0.0, 2.0 * pixel.y)).r);
		silhouette += vec3(1.0, 0.25, 0.75) * smoothstep(0.3, 0.9, above) * (1.0 - skyArt) * 1.6;
		vec3 replaced = mix(silhouette, sky, skyArt);
		color = mix(color, replaced, rteSky);
	}

	if (rteEdges > 0.0) {
		// Edges in the scene's brightness (on its log, so dark and bright areas count alike), and where the battlefield meets the distant background.
		vec2 pixel = 1.0 / rteScreenSize;
		float lum[9];
		float near[9];
		int i = 0;
		for (int y = -1; y <= 1; ++y) {
			for (int x = -1; x <= 1; ++x) {
				vec2 at = uv + vec2(x, y) * pixel;
				lum[i] = log(1.0 + 4.0 * dot(texture(rteScene, at).rgb, vec3(0.2126, 0.7152, 0.0722)));
				near[i] = rteHasDepth ? step(texture(rteSceneDepth, at).r, rteBackgroundDepth) : 1.0;
				++i;
			}
		}
		float gx = (lum[2] + 2.0 * lum[5] + lum[8]) - (lum[0] + 2.0 * lum[3] + lum[6]);
		float gy = (lum[6] + 2.0 * lum[7] + lum[8]) - (lum[0] + 2.0 * lum[1] + lum[2]);
		float brightnessEdge = smoothstep(0.5, 1.1, length(vec2(gx, gy))) * 0.7;
		float dx = (near[2] + 2.0 * near[5] + near[8]) - (near[0] + 2.0 * near[3] + near[6]);
		float dy = (near[6] + 2.0 * near[7] + near[8]) - (near[0] + 2.0 * near[1] + near[2]);
		// Only the battlefield side of the boundary, so the line hugs the terrain and not the sky.
		float silhouetteEdge = near[4] * smoothstep(0.5, 2.0, length(vec2(dx, dy)));
		float edge = clamp(max(brightnessEdge * near[4], silhouetteEdge), 0.0, 1.0) * rteEdges;
		if (rteInkEdges) {
			color *= 1.0 - 0.92 * edge;
		} else {
			// Pink near the top of the screen to cyan near the bottom, bright enough to bloom.
			vec3 neon = mix(vec3(1.0, 0.2, 0.75), vec3(0.2, 0.9, 1.0), smoothstep(0.15, 0.85, screen.y));
			color = mix(color, neon * 1.8, edge * 0.85);
		}
	}
	FragColor = vec4(color, scene.a);
}
