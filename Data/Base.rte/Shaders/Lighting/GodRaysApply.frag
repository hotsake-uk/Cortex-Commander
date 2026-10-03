// GodRaysApply.frag
// Adds the god rays to the HDR scene over the air inside caves and bunkers: the terrain background layer, not solid foreground terrain, objects or the open sky.
// Dust motes drift through the shafts and glint where the light is strongest.
#version 330 core

in vec2 textureUV;
out vec4 FragColor;

uniform sampler2D rteGodRays;
uniform sampler2D rteSceneDepth;
uniform float rteForegroundDepth; // Depth beyond which pixels are behind the foreground terrain and objects.
uniform float rteBackgroundDepth; // Depth beyond which pixels are distant background layers.
uniform vec2 rteScreenSize;
uniform vec2 rteScreenOrigin; // World position of the screen's top left pixel.
uniform float rteTime;
uniform float rteDustMotes;

float Hash(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float Motes(vec2 worldPos) {
	// Sparse points on a drifting grid: each 9px cell may hold one mote, slowly wandering and twinkling.
	vec2 drift = vec2(sin(rteTime * 0.21) * 6.0 + rteTime * 2.0, rteTime * 3.0);
	vec2 p = worldPos + drift;
	vec2 cell = floor(p / 9.0);
	float present = step(0.86, Hash(cell));
	vec2 motePos = (cell + vec2(Hash(cell + 3.1), Hash(cell + 7.7))) * 9.0;
	motePos += vec2(sin(rteTime * 0.7 + Hash(cell) * 30.0), cos(rteTime * 0.5 + Hash(cell + 1.3) * 30.0)) * 2.0;
	float twinkle = 0.5 + 0.5 * sin(rteTime * 2.0 + Hash(cell + 5.0) * 40.0);
	return present * twinkle * step(length(p - motePos), 0.75);
}

void main() {
	vec2 uv = gl_FragCoord.xy / rteScreenSize;
	float depth = texture(rteSceneDepth, uv).r;
	float terrainBackground = step(rteForegroundDepth, depth) * (1.0 - step(rteBackgroundDepth, depth));
	vec3 rays = texture(rteGodRays, uv).rgb;
	vec3 color = rays;
	if (rteDustMotes > 0.0) {
		color += rays * Motes(rteScreenOrigin + gl_FragCoord.xy) * 6.0 * rteDustMotes;
	}
	FragColor = vec4(color * terrainBackground, 1.0);
}
