// Fullscreen.vert
// Shared vertex shader for fullscreen lighting and post-processing passes. Positions are in NDC, UVs go 0..1 bottom to top.
#version 330 core

in vec3 rteVertexPosition;
in vec2 rteVertexTexUV;

out vec2 textureUV;

void main() {
	gl_Position = vec4(rteVertexPosition.xy, 0.0, 1.0);
	textureUV = rteVertexTexUV;
}
