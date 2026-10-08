// WetnessUpdate.frag
// Steps the wetness map (LightingSettings::WetnessMap) on, in the light grid's cells: R = how wet the ground there is, 0 to 1, and past 1 the water
// standing in its dips, up to 2. Rain wets everything quickly and then fills puddles slowly; once it stops the ground dries, hard rock and concrete
// three times slower than earth. Where the weather actually reaches is up to the terrain shader.
#version 330 core

in vec2 textureUV;

out vec4 FragColor;

uniform sampler2D rtePrevious; // Last step's map.
uniform sampler2D rteOccupancy; // The light grid's terrain map: G = how metallic its material, B = how glossy.
uniform float rteRain; // How hard it's raining now, 0 to 1.
uniform float rteSeconds; // Game seconds since the last step.
uniform float rteDrySeconds; // How long wet earth takes to dry, puddles included.

void main() {
	float wet = texture(rtePrevious, textureUV).r;
	vec4 grid = texture(rteOccupancy, textureUV);
	float hard = clamp(grid.b * 1.5 + grid.g, 0.0, 1.0);
	if (rteRain > 0.02) {
		float soaked = min(1.0, rteRain * 1.3);
		if (wet < soaked) {
			wet = min(wet + rteSeconds / 8.0, soaked);
		} else {
			// Standing water gathers over a minute or so of heavy rain, less in a drizzle.
			wet = max(wet, min(wet + rteSeconds * rteRain / 45.0, 1.0 + rteRain));
		}
	} else {
		wet = max(wet - rteSeconds / (rteDrySeconds * mix(1.0, 3.0, hard)), 0.0);
	}
	FragColor = vec4(wet, 0.0, 0.0, 1.0);
}
