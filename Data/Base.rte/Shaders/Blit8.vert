#version 330 core

in vec3 rteVertexPosition;
in vec2 rteVertexTexUV;
in vec4 rteVertexColor;
in vec4 rteVertexSurface;

out vec2 textureUV;
out vec4 vertexColor;
out vec4 vertexSurface;
uniform mat4 rteView;
uniform mat4 rteProjection;
uniform mat4 rteTransform;

void main() {
	gl_Position = rteProjection * rteView * rteTransform * vec4(rteVertexPosition, 1.0);
	textureUV = rteVertexTexUV;
	vertexColor = rteVertexColor;
	vertexSurface = rteVertexSurface;
}
