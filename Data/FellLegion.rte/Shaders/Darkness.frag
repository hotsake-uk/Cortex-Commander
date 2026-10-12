// Darkness.frag (FellLegion.rte, the "Fell Darkness" Shader preset)
// The screen while a Darkness holds: the scene falls to night, drained of colour and sunk into blue-black, with only the brightest
// lights left burning (what was bright keeps its colour and glow), a slow dark breathing, and shadows creeping in from the edges.
// A post pass (Fullscreen.vert, on the lit HDR scene); all of it fades by rteStrength. Set and cleared by Devices/Spells/Darkness.lua.
#version 330 core

in vec2 textureUV;
out vec4 FragColor;

uniform sampler2D rteScene;
uniform vec2 rteScreenSize;
uniform vec2 rteScreenOrigin;
uniform float rteTime;
uniform float rteStrength;

float Hash(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
	float strength = clamp(rteStrength, 0.0, 1.0);
	vec2 aspect = vec2(rteScreenSize.x / max(rteScreenSize.y, 1.0), 1.0);
	vec2 fromCentre = (textureUV - 0.5) * aspect;
	float distance = length(fromCentre);
	vec4 scene = texture(rteScene, textureUV);
	float luminance = dot(scene.rgb, vec3(0.299, 0.587, 0.114));
	// Night: colour drained and sunk into blue-black; the brightest things keep burning.
	vec3 night = mix(vec3(luminance), scene.rgb, 0.35) * vec3(0.35, 0.4, 0.6) * 0.3;
	float bright = smoothstep(0.55, 1.2, luminance);
	vec3 color = mix(scene.rgb, night, (1.0 - bright) * strength);
	// Shadows creeping in from the edges, breathing.
	float breathe = 0.5 + 0.5 * sin(rteTime * 1.3);
	float worldX = rteScreenOrigin.x + textureUV.x * rteScreenSize.x;
	float worldY = rteScreenOrigin.y + (1.0 - textureUV.y) * rteScreenSize.y;
	float creep = 0.08 * sin(worldX * 0.02 + rteTime * 0.9) * sin(worldY * 0.017 - rteTime * 0.7);
	float vignette = smoothstep(0.35 + 0.1 * breathe + creep, 0.95, distance);
	color *= mix(1.0, 1.0 - 0.8 * vignette, strength);
	// A faint drifting grain in the dark.
	float grain = Hash(floor(vec2(worldX, worldY) * 0.5) + floor(rteTime * 8.0)) * 0.03;
	color += vec3(grain * 0.5, grain * 0.4, grain) * (1.0 - bright) * strength;
	FragColor = vec4(color, scene.a);
}
