// Blizzard.frag (Medieval.rte, the "Medieval Blizzard" Shader preset)
// The screen while a Blizzard rages: the scene chilled to blue-white, snow streaking across it in driven sheets, frost creeping in from the
// edges of the view, and everything a little blurred by the whirling air. A post pass (Fullscreen.vert, on the lit HDR scene); all of it
// fades by rteStrength. Set and cleared by Devices/Spells/Blizzard.lua.
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
	// The world position of this pixel, so the snow is fixed to the world and not to the camera.
	vec2 world = rteScreenOrigin + vec2(textureUV.x, 1.0 - textureUV.y) * rteScreenSize;
	// Driven snow: three layers of streaks racing down and across, each at its own speed and size.
	float snow = 0.0;
	for (int layer = 0; layer < 3; layer++) {
		float scale = 1.0 + float(layer) * 0.7;
		vec2 drift = vec2(rteTime * (220.0 + 60.0 * float(layer)), rteTime * (90.0 + 40.0 * float(layer)));
		vec2 cell = floor((world + drift) / vec2(14.0 * scale, 5.0 * scale));
		float h = Hash(cell + float(layer) * 17.0);
		vec2 inCell = fract((world + drift) / vec2(14.0 * scale, 5.0 * scale));
		float streak = step(0.72 + 0.1 * float(layer), h) * smoothstep(0.5, 0.0, abs(inCell.y - 0.5)) * smoothstep(0.0, 0.2, inCell.x) * smoothstep(1.0, 0.7, inCell.x);
		snow += streak * (0.5 - 0.1 * float(layer));
	}
	// The air whirls: the scene is sampled a little off, swaying.
	vec2 sway = vec2(sin(world.y * 0.02 + rteTime * 3.0), cos(world.x * 0.015 + rteTime * 2.3)) * 0.0015 * strength;
	vec4 scene = texture(rteScene, clamp(textureUV + sway, vec2(0.001), vec2(0.999)));

	float luminance = dot(scene.rgb, vec3(0.299, 0.587, 0.114));
	vec3 cold = mix(scene.rgb, vec3(luminance) * vec3(0.8, 0.9, 1.1), 0.5);
	vec3 color = mix(scene.rgb, cold, strength);
	color += vec3(0.04, 0.06, 0.1) * strength;
	// Frost creeping in from the edges.
	vec2 fromCentre = (textureUV - 0.5) * aspect;
	float edge = smoothstep(0.35, 0.95, length(fromCentre));
	float frostGrain = Hash(floor(world / 3.0)) * 0.3 + 0.7;
	color = mix(color, vec3(0.85, 0.92, 1.0) * frostGrain, edge * 0.55 * strength);
	// The snow itself, bright and a little blue.
	color += vec3(0.9, 0.95, 1.0) * snow * strength;
	FragColor = vec4(color, scene.a);
}
