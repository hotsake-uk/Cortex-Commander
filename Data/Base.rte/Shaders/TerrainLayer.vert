// TerrainLayer.vert
// Blit8.vert plus the world position, so terrain layers can sample world space effect maps (scorch marks, cooling hot spots).
#version 330 core

in vec3 rteVertexPosition;
in vec2 rteVertexTexUV;
in vec4 rteVertexColor;

out vec2 textureUV;
out vec4 vertexColor;
out vec2 worldPos;

uniform mat4 rteView;
uniform mat4 rteProjection;
uniform mat4 rteTransform;

void main() {
	vec4 world = rteTransform * vec4(rteVertexPosition, 1.0);
	gl_Position = rteProjection * rteView * world;
	textureUV = rteVertexTexUV;
	vertexColor = rteVertexColor;
	worldPos = world.xy;
}
