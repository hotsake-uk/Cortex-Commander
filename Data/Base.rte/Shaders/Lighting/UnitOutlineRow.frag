#version 330 core
// Unit outlines (LightingSettings::UnitOutline), first half: for each pixel, how far along its row the nearest pixel of a unit is, out to rteRadius,
// and that unit's side. Tonemap.frag finishes the search down the columns; the two together give the true distance to the nearest unit pixel
// for the cost of a line each way rather than a whole square.
// Units are marked in the surface buffer's solid flag (MOSRotating::GetRenderSurface): 255 - 8 * slot for shadow casters, 8 * slot for the
// rest, slot 1 for no team and 2 to 5 for teams 1 to 4. Other values (255 solid, 0, 64 water, 128 shining) are not units.

out vec4 FragColor; // R distance along the row in 255ths (255 none in reach), G the slot in 255ths (0 none), B 1 where an outline may be drawn here.

uniform sampler2D rteSurface;
uniform sampler2D rteSceneDepth;
uniform float rteForegroundDepth; // Depth between the foreground (terrain, objects) and the terrain background.
uniform int rteRadius; // In pixels, at most 12.

int SlotAt(int x, int y, int width) {
	if (x < 0 || x >= width) {
		return 0;
	}
	int value = int(texelFetch(rteSurface, ivec2(x, y), 0).b * 255.0 + 0.5);
	if (value >= 211 && value <= 251) {
		return (255 - value + 4) / 8;
	}
	if (value >= 4 && value <= 44) {
		return (value + 4) / 8;
	}
	return 0;
}

void main() {
	ivec2 pixel = ivec2(gl_FragCoord.xy);
	int width = textureSize(rteSurface, 0).x;
	int nearest = 255;
	int slot = 0;
	for (int distance = 0; distance <= 12; ++distance) {
		if (distance > rteRadius) {
			break;
		}
		int found = SlotAt(pixel.x - distance, pixel.y, width);
		if (found == 0) {
			found = SlotAt(pixel.x + distance, pixel.y, width);
		}
		if (found > 0) {
			nearest = distance;
			slot = found;
			break;
		}
	}
	// The stroke goes over the sky, the terrain background and other objects, never over foreground terrain.
	bool open = texelFetch(rteSceneDepth, pixel, 0).r >= rteForegroundDepth || texelFetch(rteSurface, pixel, 0).b > 0.5;
	FragColor = vec4(float(nearest) / 255.0, float(slot) / 255.0, open ? 1.0 : 0.0, 1.0);
}
