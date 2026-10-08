// SunShadowMap.frag
// The sun's shadow map (LightingSettings::SunShadowMap). In a 2D scene lit from one direction it is a single strip: for every ray coming down from the sun, how far down
// it gets before it meets solid ground. Each texel is one ray, named by where it crosses the top of the scene (a coordinate sheared along the sun's direction), and holds
// the scene y of the first solid point on it. A point is in sunlight when it lies above that. The rays are marched through the light grid's terrain coverage a cell at a time,
// and the crossing is found to a fraction of a pixel by halving the last step, so the shadows' edges are as sharp as the ground's.
#version 330 core

out vec4 FragColor;

uniform sampler2D rteOccupancy; // The light grid, R = terrain coverage of each cell, 0 air .. 1 solid, linearly filtered.
uniform vec2 rteGridWorldSize; // Scene pixels the grid covers.
uniform vec2 rteSceneSize; // Scene size in pixels.
uniform float rteCellSize; // Pixels per grid cell.
uniform float rteSlope; // How far a ray moves in x for each pixel it comes down (the sun's direction).
uniform float rteStripStart; // Where the first texel's ray crosses the top of the scene, in scene x.
uniform float rteStripTexel; // Scene pixels between neighbouring rays.
uniform bool rteWrapX;

// How solid the scene is at a point. Off the sides of a scene that doesn't wrap the sun shines unhindered.
float Solid(float x, float y) {
	if (rteWrapX) {
		x = mod(x, rteSceneSize.x);
	} else if (x < 0.0 || x > rteSceneSize.x) {
		return 0.0;
	}
	return texture(rteOccupancy, vec2(x, y) / rteGridWorldSize).r;
}

void main() {
	float top = rteStripStart + gl_FragCoord.x * rteStripTexel;
	// A cell's length along the ray.
	float step = rteCellSize / sqrt(1.0 + rteSlope * rteSlope);
	float previous = 0.0;
	float first = 1.0e6; // Nothing solid all the way down.
	for (int i = 0; i < 16384; ++i) {
		float y = min(float(i) * step, rteSceneSize.y);
		if (Solid(top - rteSlope * y, y) >= 0.5) {
			// Halve the last step a few times to find the surface to a fraction of a pixel.
			float above = previous;
			float below = y;
			for (int j = 0; j < 6; ++j) {
				float middle = (above + below) * 0.5;
				if (Solid(top - rteSlope * middle, middle) >= 0.5) {
					below = middle;
				} else {
					above = middle;
				}
			}
			first = i == 0 ? 0.0 : below;
			break;
		}
		if (y >= rteSceneSize.y) {
			break;
		}
		previous = y;
	}
	FragColor = vec4(first, 0.0, 0.0, 1.0);
}
