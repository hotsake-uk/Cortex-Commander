// TimeWarp.frag (Medieval.rte, the "Medieval Time Warp" Shader preset)
// The screen while a Time Warp holds: the scene drained to old sepia, rippling slowly outward from the middle as if seen through
// warped glass, darkened at the edges, and breathing a little brighter and dimmer. A post pass (Fullscreen.vert, on the lit HDR scene);
// all of it fades by rteStrength. Set and cleared by Devices/Spells/TimeWarpField.lua.
#version 330 core

in vec2 textureUV;
out vec4 FragColor;

uniform sampler2D rteScene;
uniform vec2 rteScreenSize;
uniform vec2 rteScreenOrigin;
uniform float rteTime;
uniform float rteStrength;

void main() {
	float strength = clamp(rteStrength, 0.0, 1.0);
	vec2 aspect = vec2(rteScreenSize.x / max(rteScreenSize.y, 1.0), 1.0);
	vec2 fromCentre = (textureUV - 0.5) * aspect;
	float distance = length(fromCentre);
	// Slow rings spreading from the middle, and a gentle sway fixed to the world so it doesn't slide with the camera.
	float ripple = sin(distance * 30.0 - rteTime * 2.0) * 0.0022;
	float worldY = rteScreenOrigin.y + (1.0 - textureUV.y) * rteScreenSize.y;
	float sway = sin(worldY * 0.035 + rteTime * 1.1) * 0.0012;
	vec2 direction = distance > 0.0001 ? fromCentre / distance : vec2(0.0);
	vec2 uv = textureUV + (direction * ripple / aspect + vec2(sway, 0.0)) * strength;
	vec4 scene = texture(rteScene, clamp(uv, vec2(0.001), vec2(0.999)));

	float luminance = dot(scene.rgb, vec3(0.299, 0.587, 0.114));
	vec3 sepia = luminance * vec3(1.08, 0.9, 0.66);
	vec3 color = mix(scene.rgb, sepia, 0.78 * strength);
	// A golden haze in the shadows, like old varnish.
	color += vec3(0.035, 0.025, 0.008) * strength;
	float vignette = smoothstep(0.95, 0.3, distance);
	color *= mix(1.0, 0.4 + 0.6 * vignette, strength);
	color *= 1.0 + 0.05 * sin(rteTime * 2.6) * strength;
	FragColor = vec4(color, scene.a);
}
