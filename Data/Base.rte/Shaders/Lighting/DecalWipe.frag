// DecalWipe.frag
// Wipes the world scorch and stain maps where the ground came or went (see SceneLighting::RefreshDecalGround): drawn over the cells it worked out
// again, it clears each cell marked in rteDecalWipe and leaves the rest alone, so a mark goes with the surface it was on.
#version 330 core

in vec2 textureUV;

out vec4 FragColor;

uniform sampler2D rteDecalWipe; // R: 1 for the cells to wipe.
uniform vec2 rteDecalWipeSize; // In cells.

void main() {
	if (texture(rteDecalWipe, gl_FragCoord.xy / rteDecalWipeSize).r < 0.5) {
		discard;
	}
	FragColor = vec4(0.0);
}
