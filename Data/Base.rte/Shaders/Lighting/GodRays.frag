// GodRays.frag
// Crepuscular rays: open sky is smeared towards the sun, so light streams down through gaps in the terrain.
// Runs at half resolution. "Open sky" comes from the terrain skyline in world space, so moving objects and background art don't cast shafts.
#version 330 core

in vec2 textureUV;
out vec4 FragColor;

uniform sampler2D rteSkyline; // 1 row, R = grid row of the first solid cell in each column, normalized by grid height.
uniform vec2 rteScreenOrigin; // World position of the screen's top left pixel.
uniform vec2 rteScreenSize;
uniform vec2 rteGridWorldSize;
uniform vec2 rteSunPosition; // Screen UV of the sun, usually above the screen.
uniform vec3 rteSunColor; // Linear ray color and strength.
uniform float rteDecay; // Per sample falloff along the ray.
uniform vec2 rteTargetSize;
uniform float rteTime;

float Hash(float n) {
	return fract(sin(n) * 43758.5453);
}

float Noise(float x) {
	float i = floor(x);
	float f = fract(x);
	return mix(Hash(i), Hash(i + 1.0), f * f * (3.0 - 2.0 * f));
}

const int c_Samples = 48;

float OpenSky(vec2 screenUV) {
	vec2 worldPos = rteScreenOrigin + screenUV * rteScreenSize;
	vec2 gridUV = worldPos / rteGridWorldSize;
	float skyline = texture(rteSkyline, vec2(fract(gridUV.x), 0.5)).r;
	return gridUV.y < skyline ? 1.0 : 0.0;
}

void main() {
	vec2 uv = gl_FragCoord.xy / rteTargetSize;
	vec2 toSun = rteSunPosition - uv;
	// March towards the sun, but not too far: keep shafts local to the openings they come through.
	vec2 stepUV = toSun * (0.55 / float(c_Samples));
	vec2 sampleUV = uv;
	float weight = 1.0;
	float accumulated = 0.0;
	float total = 0.0;
	for (int i = 0; i < c_Samples; ++i) {
		sampleUV += stepUV;
		accumulated += weight * OpenSky(sampleUV);
		total += weight;
		weight *= rteDecay;
	}
	// The fraction of the path towards the sun that's open. Squared so beams have defined edges where terrain starts blocking them.
	float openness = accumulated / total;
	float shaft = openness * openness;
	// Streaks: vary brightness across the beams (perpendicular to the sun direction), drifting slowly, like dust in the light.
	vec2 worldPos = rteScreenOrigin + uv * rteScreenSize;
	vec2 sunDirection = normalize(toSun * rteScreenSize);
	float across = dot(worldPos, vec2(-sunDirection.y, sunDirection.x));
	float streaks = 0.55 + 0.45 * (0.6 * Noise(across * 0.07 + rteTime * 0.15) + 0.4 * Noise(across * 0.19 - rteTime * 0.1));
	FragColor = vec4(rteSunColor * shaft * streaks, 1.0);
}
