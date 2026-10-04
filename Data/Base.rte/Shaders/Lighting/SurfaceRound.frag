// SurfaceRound.frag
// Gives metallic and glossy objects their roundness. A sprite's automatic normals only tilt in a thin rim at its outline; metal needs to turn smoothly from its top edge to its bottom edge
// to read as a tube or a plate catching the sky above and the ground below. This copies the player screen's normals and, for such objects, leans them outwards over a few pixels from each sprite's edge.
// A sprite's own pixels are told from its neighbours by depth: every draw has its own. What lies in front (an arm over a body) counts as the surface carrying on, so the body gets no false edge under it.
#version 330 core

out vec4 FragColor;

uniform sampler2D rteNormals; // Player screen normals: RG = normal xy * 0.5 + 0.5, B = 1 - shininess, A = 0 nothing drawn, else 0.5..1 with the emissive strength.
uniform sampler2D rteSurface; // Player screen surface values: R how metallic, G how glossy, B 1 for solid objects.
uniform sampler2D rteSceneDepth; // The player screen's depth buffer.
uniform float rteRounding; // Strength, 0 for none.

// Whether the sprite at the middle pixel carries on at another pixel: the same draw, or something in front of it.
float Covered(ivec2 pixel, ivec2 size, float depth) {
	if (any(lessThan(pixel, ivec2(0))) || any(greaterThanEqual(pixel, size))) {
		return 1.0;
	}
	return texelFetch(rteSceneDepth, pixel, 0).r <= depth + 2.0e-8 ? 1.0 : 0.0;
}

void main() {
	ivec2 pixel = ivec2(gl_FragCoord.xy);
	vec4 normalSample = texelFetch(rteNormals, pixel, 0);
	FragColor = normalSample;
	if (normalSample.a <= 0.25 || rteRounding <= 0.0) {
		return;
	}
	vec4 surface = texelFetch(rteSurface, pixel, 0);
	float amount = max(surface.r, surface.g * 0.7) * rteRounding;
	// Objects only: terrain plating stays flat.
	if (surface.b < 0.5 || amount < 0.05) {
		return;
	}
	ivec2 size = textureSize(rteNormals, 0);
	float depth = texelFetch(rteSceneDepth, pixel, 0).r;
	// How the sprite's coverage changes around this pixel, near and a little further out: zero in the middle of a wide part, strongest at an edge.
	vec2 inward = vec2(0.0);
	inward += vec2(Covered(pixel + ivec2(2, 0), size, depth) - Covered(pixel - ivec2(2, 0), size, depth), Covered(pixel + ivec2(0, 2), size, depth) - Covered(pixel - ivec2(0, 2), size, depth));
	inward += 0.6 * vec2(Covered(pixel + ivec2(4, 0), size, depth) - Covered(pixel - ivec2(4, 0), size, depth), Covered(pixel + ivec2(0, 4), size, depth) - Covered(pixel - ivec2(0, 4), size, depth));
	if (inward == vec2(0.0)) {
		return;
	}
	vec2 tilt = normalSample.xy * 2.0 - 1.0;
	vec3 normal = vec3(tilt, sqrt(max(1.0 - dot(tilt, tilt), 0.0)));
	normal = normalize(normal + vec3(-inward / 1.6 * 0.85 * min(amount, 1.2), 0.0));
	FragColor = vec4(normal.xy * 0.5 + 0.5, normalSample.b, normalSample.a);
}
