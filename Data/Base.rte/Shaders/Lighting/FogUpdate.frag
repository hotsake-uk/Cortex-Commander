// FogUpdate.frag
// One step of the fog volume (LightingSettings::FogVolume): mist and dust hanging in the air, a value per light grid cell, kept from frame to frame.
// Each step it drifts with the wind, spreads a little, thins out over time, never fills solid ground, and is topped up from its sources: dawn mist low in
// open valleys, and puffs put in the air (steam, collapse dust, scripts) through PostProcessMan::RegisterFog.
#version 330 core

in vec2 textureUV;
out vec4 FragColor;

uniform sampler2D rtePrevious; // Last step's fog, R = thickness 0 to 1.
uniform sampler2D rteOccupancy; // The light grid's terrain map, A = how much of the cell is filled.
uniform sampler2D rteSkyLight; // R = sky light, G = how much of the sun can be seen.
uniform vec2 rteGridSize; // In cells.
uniform vec2 rteDrift; // How far the air moved this step, in cells.
uniform float rteKeep; // How much of the fog is left after this step, before sources.
uniform float rteMist; // How much dawn mist gathers this step where it can.
uniform int rtePuffCount;
const int c_MaxPuffs = 16;
uniform vec4 rtePuffs[c_MaxPuffs]; // xy = centre in cells, z = radius in cells, w = how much this step.

float Filled(vec2 cell) {
	return texture(rteOccupancy, cell / rteGridSize).a;
}

void main() {
	vec2 cell = gl_FragCoord.xy;
	if (Filled(cell) > 0.5) {
		FragColor = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}
	// Carried by the wind from upwind (the textures wrap or clamp as the scene does), and spread a little into the cells around.
	vec2 from = cell - rteDrift;
	float fog = texture(rtePrevious, from / rteGridSize).r * 0.8;
	fog += (texture(rtePrevious, (from + vec2(1.0, 0.0)) / rteGridSize).r + texture(rtePrevious, (from - vec2(1.0, 0.0)) / rteGridSize).r +
	        texture(rtePrevious, (from + vec2(0.0, 1.0)) / rteGridSize).r + texture(rtePrevious, (from - vec2(0.0, 1.0)) / rteGridSize).r) * 0.05;
	fog *= rteKeep;
	// Dawn mist lies in open air under the sky, close above the ground (row y grows downwards), thickest right at it.
	if (rteMist > 0.0) {
		float sky = texture(rteSkyLight, cell / rteGridSize).r;
		float ground = 0.0;
		for (int below = 1; below <= 4; ++below) {
			if (Filled(cell + vec2(0.0, float(below))) > 0.5) {
				ground = 1.0 - float(below - 1) * 0.22;
				break;
			}
		}
		fog += rteMist * smoothstep(0.4, 0.9, sky) * ground * (1.0 - fog);
	}
	for (int i = 0; i < c_MaxPuffs; ++i) {
		if (i >= rtePuffCount) {
			break;
		}
		float reach = distance(cell, rtePuffs[i].xy) / max(rtePuffs[i].z, 0.5);
		if (reach < 1.0) {
			fog += rtePuffs[i].w * (1.0 - reach * reach) * (1.0 - fog);
		}
	}
	FragColor = vec4(clamp(fog, 0.0, 1.0), 0.0, 0.0, 1.0);
}
