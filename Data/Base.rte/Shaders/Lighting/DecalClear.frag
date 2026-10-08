// DecalClear.frag
// Wipes the world scorch and stain maps where there's no ground left (see SceneLighting::RefreshDecalGround): drawn over the cells where the ground changed,
// it clears each cell with no ground in it or beside it and leaves the rest alone, so a mark goes with the ground it was on.
#version 330 core

in vec2 textureUV;

out vec4 FragColor;

uniform sampler2D rteDecalGround; // R: 1 where a cell of the maps holds ground.
uniform vec2 rteDecalGroundSize; // In cells.

void main() {
	// The cell and the eight around it: the terrain is drawn with the maps filtered, so a cell beside ground still shows on its edge.
	for (int y = -1; y <= 1; ++y) {
		for (int x = -1; x <= 1; ++x) {
			if (texture(rteDecalGround, (gl_FragCoord.xy + vec2(x, y)) / rteDecalGroundSize).r > 0.5) {
				discard;
			}
		}
	}
	FragColor = vec4(0.0);
}
