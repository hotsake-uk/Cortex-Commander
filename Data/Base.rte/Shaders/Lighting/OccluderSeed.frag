// OccluderSeed.frag
// Starts the map of distances to solid objects (units, devices, doors, wreckage), which unit shadows and contact shading are worked out from.
// Every pixel of a solid object becomes a seed holding its own position; OccluderJump.frag then spreads the seeds so each pixel knows its nearest one.
// Specks of a pixel or two are left out, so stray pixels never throw shadows.
#version 330 core

out vec4 FragColor;

uniform sampler2D rteSurface; // Player screen surface values, B = 1 where a solid object was drawn.
uniform sampler2D rteSceneDepth; // The player screen's depth buffer.
uniform float rteForegroundDepth; // Depth below which pixels are foreground terrain and objects. Background layers don't write surface values, so they're told apart by depth.

bool Solid(ivec2 pixel, ivec2 size) {
	if (any(lessThan(pixel, ivec2(0))) || any(greaterThanEqual(pixel, size))) {
		return false;
	}
	return texelFetch(rteSurface, pixel, 0).b > 0.5 && texelFetch(rteSceneDepth, pixel, 0).r < rteForegroundDepth;
}

void main() {
	ivec2 pixel = ivec2(gl_FragCoord.xy);
	ivec2 size = textureSize(rteSurface, 0);
	// Far away means no seed.
	vec2 seed = vec2(-20000.0);
	if (Solid(pixel, size)) {
		int neighbours = 0;
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				if ((dx != 0 || dy != 0) && Solid(pixel + ivec2(dx, dy), size)) {
					++neighbours;
				}
			}
		}
		if (neighbours >= 3) {
			seed = gl_FragCoord.xy;
		}
	}
	FragColor = vec4(seed, 0.0, 1.0);
}
