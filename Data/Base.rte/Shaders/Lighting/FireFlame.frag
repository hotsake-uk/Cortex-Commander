// FireFlame.frag
// The flames of burning ground, drawn into the glow buffer like the other glows (screen blended, gamma space): one quad per stretch of the fire
// front, filled with tongues of scrolling noise that narrow and break up towards the top, a darker core at the base, and a colour ramp from deep red
// at the edges to yellow-white where it's hottest. The noise runs in scene coordinates, so flames stay put as the camera moves. The wind bends
// the tongues over downwind, more towards their tips.
#version 330 core

in vec2 textureUV; // x across the quad 0..1, y from its top 0 to its bottom 1.
in vec4 vertexColor; // RGB how bright, A how hot (0..1).
out vec4 FragColor;

uniform vec2 rteScreenOrigin; // Scene position of the glow buffer's top left pixel.
uniform float rteTime; // Seconds.
uniform float rteWind; // Pixels a second the air carries things with (Air & wind), negative blows left; 0 with it off.
uniform bool rteHeatAlpha; // Write the heat into alpha (the heat haze reads it); else alpha is 1, as the other glows write.

float Hash(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float Noise(vec2 p) {
	vec2 cell = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(Hash(cell), Hash(cell + vec2(1.0, 0.0)), f.x), mix(Hash(cell + vec2(0.0, 1.0)), Hash(cell + vec2(1.0, 1.0)), f.x), f.y);
}

void main() {
	vec2 world = rteScreenOrigin + gl_FragCoord.xy;
	float up = 1.0 - textureUV.y; // 0 at the base, 1 at the top.
	// Bent downwind: the flame's middle moves over the higher it is, as far as the quad leaves room for the tip.
	float lean = clamp(rteWind / 150.0, -1.5, 1.5);
	float bend = clamp(lean * up * up * 0.3, -0.35, 0.35);
	float across = abs(textureUV.x - 0.5 - bend) * 2.0; // 0 in the middle, 1 at the sides.
	// Two octaves rising at different speeds, with a sideways sway, so tongues split and lick rather than slide up as one; the wind carries the tongues along.
	float sway = sin(world.y * 0.05 + rteTime * 1.7) * 3.0 - rteTime * rteWind * 0.12;
	float n = 0.65 * Noise(vec2((world.x + sway) * 0.11, (world.y + rteTime * 42.0) * 0.08)) + 0.35 * Noise(vec2(world.x * 0.27, (world.y + rteTime * 70.0) * 0.19));
	// The flame: full at the base, eaten away by the noise towards the top, narrowing to the tips.
	float flame = clamp(n * 1.25 + 0.45 - up * 1.35 - across * across * (0.5 + 0.8 * up), 0.0, 1.0);
	flame *= smoothstep(0.0, 0.08, up) * (1.0 - smoothstep(0.85, 1.0, across));
	if (flame <= 0.003) {
		discard;
	}
	float heat = vertexColor.a;
	// Hottest in the body of the flame: deep red at its edges and tips, orange, then yellow-white.
	float t = clamp(flame * (0.7 + 0.5 * heat), 0.0, 1.0);
	vec3 color = mix(vec3(0.55, 0.06, 0.01), vec3(1.0, 0.45, 0.06), smoothstep(0.0, 0.45, t));
	color = mix(color, vec3(1.0, 0.92, 0.6), smoothstep(0.55, 0.95, t));
	// A darker core where the flame stands on the ground, as fuel too rich to burn bright.
	float core = (1.0 - smoothstep(0.0, 0.35, up)) * (1.0 - smoothstep(0.0, 0.45, across)) * smoothstep(0.5, 0.9, flame);
	color *= 1.0 - 0.4 * core;
	float strength = smoothstep(0.0, 0.35, flame);
	FragColor = vec4(min(color * strength * vertexColor.rgb, vec3(1.0)), rteHeatAlpha ? strength * heat : 1.0);
}
