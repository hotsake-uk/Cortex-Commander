// GodRays.frag
// Light shafts: the air inside caves and bunkers that the sun (or the moon) reaches shows as beams, streaked like dust in the light.
// Where the sun reaches comes from the light grid's sun visibility, so a beam ends where a roof or a wall cuts it off, and it turns with the time of day.
// Runs at half resolution.
#version 330 core

in vec2 textureUV;
out vec4 FragColor;

uniform sampler2D rteSkyLight; // World grid, R = sky light 0..1, G = how much of the sun (or moon) is visible.
uniform vec2 rteScreenOrigin; // World position of the screen's top left pixel.
uniform vec2 rteScreenSize;
uniform vec2 rteGridWorldSize;
uniform vec2 rteSunDirection; // Unit vector towards the sun (or the moon at night), in screen pixels (y down).
uniform vec3 rteSunColor; // Linear ray color and strength.
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

void main() {
	vec2 uv = gl_FragCoord.xy / rteTargetSize;
	vec2 worldPos = rteScreenOrigin + uv * rteScreenSize;
	vec2 light = texture(rteSkyLight, worldPos / rteGridWorldSize).rg;
	// A beam only shows against shade: under open sky everything is lit and there's nothing to see, so it fades in with how enclosed the place is.
	// The fade starts just short of open sky and runs deep into the shade, so a beam coming in past a cliff edge or a cave mouth builds up gradually. Before, it ran from 0.8 to 0.25,
	// which sky light falling off a few percent per cell crossed within a few cells of the opening, so beams appeared abruptly partway down a cliff.
	float enclosed = 1.0 - smoothstep(0.1, 0.97, light.r);
	// Squared so beams have defined edges where terrain cuts them off.
	float shaft = light.g * light.g * enclosed;
	// Streaks: vary brightness across the beams (perpendicular to the sun direction), drifting slowly, like dust in the light.
	float across = dot(worldPos, vec2(-rteSunDirection.y, rteSunDirection.x));
	float streaks = 0.55 + 0.45 * (0.6 * Noise(across * 0.07 + rteTime * 0.15) + 0.4 * Noise(across * 0.19 - rteTime * 0.1));
	FragColor = vec4(rteSunColor * shaft * streaks, 1.0);
}
