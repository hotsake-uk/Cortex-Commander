// Terrain.frag
// Blit8.frag for the terrain color layers, plus scorch marks left by explosions and the glow of freshly blasted, cooling terrain.
// Keep the shared parts in sync with Blit8.frag.
#version 330 core

in vec2 textureUV;
in vec4 vertexColor;
in vec2 worldPos;
layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 NormalOut;

uniform sampler2D rteTexture;
uniform sampler2D rtePalette;
uniform bool rteIndexed;
uniform vec4 rteColor;
uniform bool rteReplaceColor;
uniform sampler2D rteEmissivePalette; // 256x1, R = how much each palette color glows.

uniform sampler2D rteScorch; // World space, R = soot darkness.
uniform vec2 rteScorchWorldSize; // World size covered by the scorch map.
uniform bool rteScorchEnabled;

const int c_MaxHotSpots = 16;
uniform int rteHotSpotCount;
uniform vec4 rteHotSpots[c_MaxHotSpots]; // xy = world position, z = radius, w = heat 0..1 (cools over time).

float Coverage(vec2 uv) {
	if (rteIndexed) {
		return texture(rteTexture, uv).r > 0.0 ? 1.0 : 0.0;
	}
	return texture(rteTexture, uv).a > 0.5 ? 1.0 : 0.0;
}

vec3 EdgeNormal(vec2 uvDx, vec2 uvDy) {
	vec2 texel = 1.0 / vec2(textureSize(rteTexture, 0));
	vec2 dx = vec2(texel.x, 0.0);
	vec2 dy = vec2(0.0, texel.y);
	vec2 gradient = vec2(Coverage(textureUV + dx) - Coverage(textureUV - dx), Coverage(textureUV + dy) - Coverage(textureUV - dy));
	gradient += 0.5 * vec2(Coverage(textureUV + 2.0 * dx) - Coverage(textureUV - 2.0 * dx), Coverage(textureUV + 2.0 * dy) - Coverage(textureUV - 2.0 * dy));
	float edge = clamp(length(gradient), 0.0, 1.0);
	if (edge <= 0.0) {
		return vec3(0.0, 0.0, 1.0);
	}
	mat2 uvPerPixel = mat2(uvDx / texel, uvDy / texel);
	vec2 screenGradient = transpose(uvPerPixel) * gradient;
	if (dot(screenGradient, screenGradient) < 1e-8) {
		return vec3(0.0, 0.0, 1.0);
	}
	vec2 outward = -normalize(screenGradient);
	return normalize(vec3(outward * edge, 1.0 - 0.6 * edge));
}

void main() {
	vec2 uvDx = dFdx(textureUV);
	vec2 uvDy = dFdy(textureUV);
	float emissive = 0.0;
	if (rteIndexed) {
		float colorIndex = texture(rteTexture, textureUV).r;
		FragColor = texture(rtePalette, vec2(colorIndex, 0.0F)) * vertexColor;
		emissive = texture(rteEmissivePalette, vec2(colorIndex, 0.0F)).r;
	} else {
		FragColor = texture(rteTexture, textureUV) * vertexColor;
	}
	if (FragColor.a == 0.0) {
		discard;
	} else if (rteReplaceColor) {
		FragColor.rgba = rteColor;
	}
	vec3 normal = EdgeNormal(uvDx, uvDy);

	if (rteScorchEnabled) {
		// Soot: darken towards a warm black, keeping a little of the original color so the texture still reads.
		float scorch = texture(rteScorch, worldPos / rteScorchWorldSize).r;
		FragColor.rgb = mix(FragColor.rgb, FragColor.rgb * vec3(0.22, 0.19, 0.17), scorch);

		// Freshly blasted terrain glows and cools down, brightest at exposed edges.
		float heat = 0.0;
		for (int i = 0; i < rteHotSpotCount; ++i) {
			vec2 toSpot = worldPos - rteHotSpots[i].xy;
			float falloff = 1.0 - smoothstep(rteHotSpots[i].z * 0.35, rteHotSpots[i].z, length(toSpot));
			heat = max(heat, falloff * rteHotSpots[i].w);
		}
		if (heat > 0.0) {
			// Only exposed surfaces glow (the crater rim), as embers: per pixel speckle so it never reads as a flat disc.
			float exposure = smoothstep(0.02, 0.35, 1.0 - normal.z);
			float speckle = fract(sin(dot(floor(worldPos), vec2(12.9898, 78.233))) * 43758.5453);
			heat *= exposure * (0.35 + 0.65 * speckle);
			vec3 glowColor = mix(vec3(0.9, 0.18, 0.02), vec3(1.0, 0.75, 0.3), heat * heat);
			FragColor.rgb = mix(FragColor.rgb, glowColor, clamp(heat * 1.2, 0.0, 0.95));
			emissive = max(emissive, heat);
		}
	}
	NormalOut = vec4(normal * 0.5 + 0.5, 0.5 + 0.5 * emissive);
}
