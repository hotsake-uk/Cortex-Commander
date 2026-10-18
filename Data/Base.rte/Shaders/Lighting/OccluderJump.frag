// OccluderJump.frag
// One pass of jump flooding over the solid object seeds from OccluderSeed.frag: each pixel looks at eight others a step away and keeps whichever seed is nearest to itself.
// Run with the step halving each pass (32, 16, ... 1). Afterwards every pixel within about 60 pixels of a solid object holds the position of its nearest pixel.
#version 330 core

out vec4 FragColor;

uniform sampler2D rteSeeds; // RG = position of the nearest seed found so far.
uniform int rteStep;

void main() {
	ivec2 pixel = ivec2(gl_FragCoord.xy);
	ivec2 size = textureSize(rteSeeds, 0);
	vec2 best = texelFetch(rteSeeds, pixel, 0).xy;
	float bestDistance = distance(best, gl_FragCoord.xy);
	for (int dy = -1; dy <= 1; ++dy) {
		for (int dx = -1; dx <= 1; ++dx) {
			if (dx == 0 && dy == 0) {
				continue;
			}
			ivec2 other = pixel + ivec2(dx, dy) * rteStep;
			if (any(lessThan(other, ivec2(0))) || any(greaterThanEqual(other, size))) {
				continue;
			}
			vec2 seed = texelFetch(rteSeeds, other, 0).xy;
			float seedDistance = distance(seed, gl_FragCoord.xy);
			if (seedDistance < bestDistance) {
				best = seed;
				bestDistance = seedDistance;
			}
		}
	}
	FragColor = vec4(best, 0.0, 1.0);
}
