// PointLight.vert
// One quad per dynamic light, positioned in player screen pixels (Y down, which is also the render target's row order).
#version 330 core

in vec3 rteVertexPosition; // Screen pixel position of this corner.
in vec2 rteVertexTexUV; // Position within the light's quad, -1..1.
in vec4 rteVertexColor; // Linear RGB light color pre-multiplied by intensity.
in vec3 rteNormal; // Light center in screen pixels (xy) and radius (z).
in vec3 rteLightCone; // Cone lights: direction (xy, screen space) and cosine of the half angle (z, below -1 for an all-round light).

out vec2 localPos;
out vec4 lightColor;
out vec2 lightCenter;
out float lightRadius;
out vec2 screenPos;
out vec3 lightCone;

uniform vec2 rteScreenSize;

void main() {
	vec2 ndc = (rteVertexPosition.xy / rteScreenSize) * 2.0 - 1.0;
	gl_Position = vec4(ndc, 0.0, 1.0);
	localPos = rteVertexTexUV;
	lightColor = rteVertexColor;
	lightCenter = rteNormal.xy;
	lightRadius = rteNormal.z;
	screenPos = rteVertexPosition.xy;
	lightCone = rteLightCone;
}
