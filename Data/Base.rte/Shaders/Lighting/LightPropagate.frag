// LightPropagate.frag
// One iteration of sky light propagation through the world light grid.
// Each cell takes the brightest of its neighbours, attenuated by its own medium (air carries light far, terrain only a few cells), and cells with open sky above are seeded at full brightness.
// Run a few iterations per frame on a persistent grid: light flows into newly dug holes over a few frames, and drains away again when they're filled.
#version 330 core

in vec2 textureUV;
out vec4 FragColor;

uniform sampler2D rteLight; // Previous iteration, R = light.
uniform sampler2D rteOccupancy; // R = terrain coverage of the cell, 0 air .. 1 solid.
uniform sampler2D rteSkyline; // 1 row, R = grid row of the first solid cell in each column, normalized by grid height.
uniform vec2 rteGridSize;
uniform bool rteWrapX;
uniform bool rteWrapY;
uniform float rteAirFalloff; // Per cell multiplier in air, e.g. 0.94.
uniform float rteSolidFalloff; // Per cell multiplier in terrain, e.g. 0.55.

float SampleLight(ivec2 cell) {
	if (rteWrapX) {
		cell.x = (cell.x + int(rteGridSize.x)) % int(rteGridSize.x);
	} else if (cell.x < 0 || cell.x >= int(rteGridSize.x)) {
		return 0.0;
	}
	if (rteWrapY) {
		cell.y = (cell.y + int(rteGridSize.y)) % int(rteGridSize.y);
	} else if (cell.y < 0) {
		// Light comes in from above the top of the scene.
		return 1.0;
	} else if (cell.y >= int(rteGridSize.y)) {
		return 0.0;
	}
	return texelFetch(rteLight, cell, 0).r;
}

void main() {
	ivec2 cell = ivec2(gl_FragCoord.xy);
	float occupancy = texelFetch(rteOccupancy, cell, 0).r;

	float skyline = texelFetch(rteSkyline, ivec2(cell.x, 0), 0).r * rteGridSize.y;
	float seed = (!rteWrapY && float(cell.y) < skyline) ? 1.0 : 0.0;

	float neighbours = max(max(SampleLight(cell + ivec2(1, 0)), SampleLight(cell + ivec2(-1, 0))), max(SampleLight(cell + ivec2(0, 1)), SampleLight(cell + ivec2(0, -1))));
	// Diagonals carry slightly less so light spreads roughly circularly instead of in a diamond.
	float diagonals = max(max(SampleLight(cell + ivec2(1, 1)), SampleLight(cell + ivec2(-1, 1))), max(SampleLight(cell + ivec2(1, -1)), SampleLight(cell + ivec2(-1, -1))));
	float incoming = max(neighbours, diagonals * 0.96);

	float falloff = mix(rteAirFalloff, rteSolidFalloff, occupancy);
	FragColor = vec4(max(seed * (1.0 - occupancy), incoming * falloff), 0.0, 0.0, 1.0);
}
