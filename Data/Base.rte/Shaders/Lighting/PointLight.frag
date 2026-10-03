// PointLight.frag
// Smooth radial falloff with soft shadows marched through the terrain occupancy grid. Additively blended into the dynamic light buffer.
#version 330 core

in vec2 localPos;
in vec4 lightColor;
in vec2 lightCenter;
in float lightRadius;
in vec2 screenPos;

out vec4 FragColor;

uniform sampler2D rteOccupancy; // World grid, R = terrain coverage 0..1, linearly filtered.
uniform vec2 rteScreenOrigin; // World position of the screen's top left pixel.
uniform vec2 rteGridWorldSize; // World size covered by the occupancy grid.
uniform float rteShadowStrength; // How much each solid sample blocks, 0..1.
uniform sampler2D rteNormals; // Player screen normals, RGB = normal * 0.5 + 0.5, A = 1 where something was drawn.
uniform vec2 rteScreenSize;
uniform float rteEdgeLighting;

const int c_ShadowSteps = 12;

void main() {
	float distanceSq = dot(localPos, localPos);
	if (distanceSq >= 1.0) {
		discard;
	}
	// Smooth falloff that reaches exactly zero at the radius, so lights never show a hard edge.
	float falloff = (1.0 - distanceSq);
	falloff *= falloff;

	// Soft shadow: march from this pixel to the light through the occupancy grid. Skip the ends so lit terrain surfaces and lights embedded in terrain still work.
	vec2 fromWorld = rteScreenOrigin + gl_FragCoord.xy;
	vec2 toWorld = rteScreenOrigin + lightCenter;
	float transmittance = 1.0;
	for (int i = 1; i < c_ShadowSteps; ++i) {
		float t = (float(i) / float(c_ShadowSteps)) * 0.9 + 0.05;
		vec2 sampleWorld = mix(fromWorld, toWorld, t);
		float occupancy = texture(rteOccupancy, sampleWorld / rteGridWorldSize).r;
		transmittance *= 1.0 - occupancy * rteShadowStrength;
	}

	// Edges facing the light catch more of it, edges facing away get less. Normalized so flat surfaces are lit exactly as without normals.
	float shading = 1.0;
	vec4 normalSample = texture(rteNormals, gl_FragCoord.xy / rteScreenSize);
	if (normalSample.a > 0.5) {
		vec3 normal = normalize(normalSample.xyz * 2.0 - 1.0);
		vec3 toLight = normalize(vec3(lightCenter - gl_FragCoord.xy, lightRadius * 0.25));
		shading = mix(1.0, clamp(dot(normal, toLight) / max(toLight.z, 0.05), 0.0, 2.5), rteEdgeLighting);
	}

	FragColor = vec4(lightColor.rgb * falloff * transmittance * shading, 1.0);
}
