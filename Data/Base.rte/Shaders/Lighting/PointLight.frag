// PointLight.frag
// Smooth radial falloff with soft shadows marched through the terrain occupancy grid. Additively blended into the dynamic light buffer.
#version 330 core

in vec2 localPos;
in vec4 lightColor;
in vec2 lightCenter;
in float lightRadius;
in vec2 screenPos;
in vec3 lightCone;

out vec4 FragColor;

uniform sampler2D rteOccupancy; // World grid, R = terrain coverage 0..1, linearly filtered.
uniform vec2 rteScreenOrigin; // World position of the screen's top left pixel.
uniform vec2 rteGridWorldSize; // World size covered by the occupancy grid.
uniform float rteShadowStrength; // How much each solid sample blocks, 0..1.
uniform sampler2D rteNormals; // Player screen normals: RG = normal xy * 0.5 + 0.5, B = 1 - shininess, A > 0.25 where something was drawn.
uniform float rteSpecular; // Strength of highlights on shiny surfaces (metal, concrete, wet ground, water).
uniform vec2 rteScreenSize;
uniform float rteEdgeLighting;
uniform bool rteBeamMode; // Drawing the visible beam of cone lights over the lit scene, instead of light falling on surfaces.

const int c_ShadowSteps = 12;

void main() {
	if (rteBeamMode && lightCone.z < -1.5) {
		discard;
	}
	float distanceSq = dot(localPos, localPos);
	if (distanceSq >= 1.0) {
		discard;
	}
	// Smooth falloff that reaches exactly zero at the radius, so lights never show a hard edge.
	float falloff = (1.0 - distanceSq);
	falloff *= falloff;
	if (lightCone.z > -1.5) {
		// Flashlight: a soft edged cone, with a little spill right around the lamp.
		vec2 toPixel = gl_FragCoord.xy - lightCenter;
		float along = dot(normalize(toPixel + vec2(0.0001)), lightCone.xy);
		float cone = smoothstep(lightCone.z, mix(lightCone.z, 1.0, 0.35), along);
		float spill = 0.12 * (1.0 - smoothstep(0.0, 0.12, sqrt(distanceSq)));
		falloff *= max(cone, spill);
		if (falloff <= 0.0005) {
			discard;
		}
	}

	// Soft shadow: march from this pixel to the light through the occupancy grid. Skip the ends so lit terrain surfaces and lights embedded in terrain still work.
	vec2 fromWorld = rteScreenOrigin + gl_FragCoord.xy;
	vec2 toWorld = rteScreenOrigin + lightCenter;
	float transmittance = 1.0;
	for (int i = 1; i < c_ShadowSteps; ++i) {
		float t = (float(i) / float(c_ShadowSteps)) * 0.9 + 0.05;
		vec2 sampleWorld = mix(fromWorld, toWorld, t);
		// Lights sitting on or in the ground (flares, lamps on walls) shouldn't be shadowed by the terrain right around them.
		if (distance(sampleWorld, toWorld) < 8.0) {
			continue;
		}
		float occupancy = texture(rteOccupancy, sampleWorld / rteGridWorldSize).r;
		transmittance *= 1.0 - occupancy * rteShadowStrength;
	}

	if (rteBeamMode) {
		// A faint haze along the beam, brightest near the lamp.
		FragColor = vec4(lightColor.rgb * vec3(1.0, 0.92, 0.78) * falloff * transmittance * 0.035, 1.0);
		return;
	}

	// Edges facing the light catch more of it, edges facing away get less. Normalized so flat surfaces are lit exactly as without normals.
	float shading = 1.0;
	vec4 normalSample = texture(rteNormals, gl_FragCoord.xy / rteScreenSize);
	if (normalSample.a > 0.25) {
		vec2 normalXY = normalSample.xy * 2.0 - 1.0;
		vec3 normal = vec3(normalXY, sqrt(max(1.0 - dot(normalXY, normalXY), 0.0)));
		vec3 toLight = normalize(vec3(lightCenter - gl_FragCoord.xy, lightRadius * 0.25));
		shading = mix(1.0, clamp(dot(normal, toLight) / max(toLight.z, 0.05), 0.0, 2.5), rteEdgeLighting);
		// Shiny surfaces throw the light back at the viewer where it strikes them squarely: a hot spot near the light, and glints on edges turned towards it.
		float shine = 1.0 - normalSample.b;
		if (shine > 0.02 && rteSpecular > 0.0) {
			vec3 halfway = normalize(toLight + vec3(0.0, 0.0, 1.0));
			shading += pow(max(dot(normal, halfway), 0.0), 28.0) * shine * rteSpecular * 2.0;
		}
	}

	FragColor = vec4(lightColor.rgb * falloff * transmittance * shading, 1.0);
}
