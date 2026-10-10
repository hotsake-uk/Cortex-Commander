// Emissive.vert
// Textured quads positioned in player screen pixels (Y down, which is also the render target's row order).
#version 330 core

in vec3 rteVertexPosition;
in vec2 rteVertexTexUV;
in vec4 rteVertexColor;

out vec2 textureUV;
out vec4 vertexColor;
out float vertexLayer; // The quad's Z: 1 for a particle in the effects layer (Emissive.frag, LitParticle.frag).

uniform vec2 rteScreenSize;

void main() {
	gl_Position = vec4((rteVertexPosition.xy / rteScreenSize) * 2.0 - 1.0, 0.0, 1.0);
	textureUV = rteVertexTexUV;
	vertexColor = rteVertexColor;
	vertexLayer = rteVertexPosition.z;
}
