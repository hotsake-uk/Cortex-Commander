// WeatherEverywhere.frag
// Weather everywhere (LightingSettings::WeatherEverywhere): lays the weather's drops, drawn on their own and premultiplied, over the lit scene everywhere but the
// terrain and its back walls. So they show over the sky, the scenery behind, units and open air under roofs and in cave mouths, and not over solid ground.
#version 330 core

out vec4 FragColor;

uniform sampler2D rteDrops; // The drops on their own: RGB premultiplied, A how much they cover.
uniform sampler2D rteSceneDepth; // The player screen's depth buffer.
uniform sampler2D rteSurface; // Player screen surface values, B = 1 where a solid object (not terrain) was drawn.
uniform sampler2D rteOccupancy; // The world's grid of solid ground: A = how full each cell is.
uniform vec2 rteScreenSize;
uniform vec2 rteScreenOrigin;
uniform vec2 rteGridWorldSize;
uniform float rteForegroundDepth; // Depth below which pixels are foreground terrain and objects.
uniform float rteBackgroundDepth; // Depth beyond which pixels are the distant background layers (or nothing was drawn).

void main() {
	vec2 screenUV = gl_FragCoord.xy / rteScreenSize;
	vec4 drops = texture(rteDrops, screenUV);
	if (drops.a <= 0.0) {
		discard;
	}
	float depth = texture(rteSceneDepth, screenUV).r;
	// The terrain's back walls sit between the foreground and the distant layers.
	bool backWall = depth >= rteForegroundDepth && depth <= rteBackgroundDepth;
	// The foreground terrain shares its depth with units and objects; solid objects mark themselves in the surface buffer, and the ground grid tells the terrain
	// from loose particles out in the open.
	vec2 worldPos = rteScreenOrigin + gl_FragCoord.xy;
	bool ground = depth < rteForegroundDepth && texture(rteSurface, screenUV).b < 0.5 && texture(rteOccupancy, worldPos / rteGridWorldSize).a > 0.05;
	if (backWall || ground) {
		discard;
	}
	FragColor = drops;
}
