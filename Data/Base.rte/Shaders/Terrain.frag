// Terrain.frag
// Blit8.frag for the terrain color layers, plus scorch marks left by explosions and the glow of freshly blasted, cooling terrain.
// Keep the shared parts in sync with Blit8.frag.
#version 330 core

in vec2 textureUV;
in vec4 vertexColor;
in vec2 worldPos;
layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 NormalOut;
layout(location = 2) out vec4 SurfaceOut; // R how metallic, G how glossy (both from the terrain's material), B 1 for solid objects that cast shadows (never terrain: the light grid handles its shadows).

uniform sampler2D rteTexture;
uniform sampler2D rtePalette;
uniform bool rteIndexed;
uniform vec4 rteColor;
uniform bool rteReplaceColor;
uniform sampler2D rteEmissivePalette; // 256x1, R = how much each palette color glows, G = whether it's a vegetation color.

uniform bool rteLivingWorld;
uniform float rteTime; // Seconds.
uniform float rteWind; // Pixels per second, negative blows left.
uniform float rteSnowCover; // 0..1, how deep snow lies on exposed ground.
uniform float rteWetness; // 0..1, how wet exposed ground is.
uniform vec2 rteWeatherFall; // Which way rain or snow is falling, a unit vector (y down): wind slants it.
uniform sampler2D rteSkyline; // 1 row, R = grid row of the first solid cell in each column, normalized by grid height.
uniform vec2 rteGridWorldSize;
uniform sampler2D rteWorldGrid; // The light grid's terrain map: R = how solid each cell is, G = how metallic its material, B = how glossy.
uniform float rteRelief; // How much the terrain's own texture counts as relief for the lighting, 0 for none.
const int c_MaxBlasts = 8;
uniform int rteBlastCount;
uniform vec4 rteBlasts[c_MaxBlasts]; // xy = world position, z = wavefront radius, w = strength.

uniform sampler2D rteScorch; // World space, R = soot darkness.
uniform vec2 rteScorchWorldSize; // World size covered by the scorch map.
uniform bool rteScorchEnabled;
uniform sampler2D rteStains; // World space liquid stains, same cells as the scorch map: RGB color, A coverage.
uniform bool rteStainsEnabled;

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

// How high a point of the terrain's texture stands, going by how bright it is. Below zero where there's nothing.
float Height(vec2 uv) {
	if (rteIndexed) {
		float colorIndex = texture(rteTexture, uv).r;
		return colorIndex > 0.0 ? dot(texture(rtePalette, vec2(colorIndex, 0.0)).rgb, vec3(0.299, 0.587, 0.114)) : -1.0;
	}
	vec4 color = texture(rteTexture, uv);
	return color.a > 0.5 ? dot(color.rgb, vec3(0.299, 0.587, 0.114)) : -1.0;
}

// The tilt the texture's own shading implies (see Blit8.frag): seams, plates and stones lean the surface a little, so lights pick them out.
vec2 ReliefTilt(vec2 uvDx, vec2 uvDy) {
	vec2 texel = 1.0 / vec2(textureSize(rteTexture, 0));
	float here = Height(textureUV);
	if (here < 0.0) {
		return vec2(0.0);
	}
	float left = Height(textureUV - vec2(texel.x, 0.0));
	float right = Height(textureUV + vec2(texel.x, 0.0));
	float up = Height(textureUV - vec2(0.0, texel.y));
	float down = Height(textureUV + vec2(0.0, texel.y));
	vec2 gradient = vec2((right < 0.0 ? here : right) - (left < 0.0 ? here : left), (down < 0.0 ? here : down) - (up < 0.0 ? here : up));
	mat2 uvPerPixel = mat2(uvDx / texel, uvDy / texel);
	return -(transpose(uvPerPixel) * gradient) * 1.6;
}

bool IsVegetation(float colorIndex) {
	return colorIndex > 0.0 && texture(rteEmissivePalette, vec2(colorIndex, 0.0)).g > 0.5;
}

// How far vegetation at this point leans, in pixels per pixel of height: wind with gusts rolling across, plus blast waves pushing outwards as they pass.
float Lean(vec2 world) {
	float wind = clamp(rteWind / 150.0, -1.5, 1.5);
	float gust = 0.6 * sin(rteTime * 1.3 + world.x * 0.045) + 0.4 * sin(rteTime * 2.9 + world.x * 0.13 + world.y * 0.05);
	float lean = wind * 0.45 + gust * (0.15 + 0.25 * abs(wind));
	for (int i = 0; i < rteBlastCount; ++i) {
		vec2 toPoint = world - rteBlasts[i].xy;
		float distance = length(toPoint);
		float front = exp(-pow((distance - rteBlasts[i].z) / 14.0, 2.0));
		lean += sign(toPoint.x) * front * rteBlasts[i].w * 0.35;
	}
	return lean;
}

// Whether rain or snow reaches a point: follows the line it falls down back upwind through the world's grid of solid ground, until it is out of the top of the
// world (it reaches) or meets ground (the point is sheltered). The same test the falling drops use, with fewer and longer steps since it runs for every pixel of exposed ground.
bool WeatherReaches(vec2 world) {
	vec2 back = -rteWeatherFall * (rteGridWorldSize.x / float(textureSize(rteWorldGrid, 0).x));
	vec2 p = world + back * 1.5;
	for (int i = 0; i < 56; ++i) {
		if (p.y < 0.0) {
			return true;
		}
		if (textureLod(rteWorldGrid, p / rteGridWorldSize, 0.0).r > 0.55) {
			return false;
		}
		p += back * (i < 20 ? 1.0 : (i < 40 ? 2.0 : 5.0));
	}
	return true;
}

void main() {
	vec2 uvDx = dFdx(textureUV);
	vec2 uvDy = dFdy(textureUV);
	float emissive = 0.0;
	vec2 texel = 1.0 / vec2(textureSize(rteTexture, 0));
	if (rteIndexed) {
		float colorIndex = texture(rteTexture, textureUV).r;
		if (rteLivingWorld) {
			// Vegetation sways: count how far up a stalk this pixel is, lean the stalk that much, and draw whatever vegetation lands here.
			float height = 0.0;
			for (int k = 1; k <= 6; ++k) {
				if (!IsVegetation(texture(rteTexture, textureUV + vec2(0.0, texel.y * float(k))).r)) {
					break;
				}
				height += 1.0;
			}
			float offset = clamp(floor(Lean(worldPos) * height * 0.35 + 0.5), -3.0, 3.0);
			if (offset != 0.0) {
				float sourceIndex = texture(rteTexture, textureUV - vec2(offset * texel.x, 0.0)).r;
				if (IsVegetation(sourceIndex)) {
					colorIndex = sourceIndex;
				} else if (IsVegetation(colorIndex)) {
					// The blade moved away from here and nothing replaced it.
					discard;
				}
			}
		}
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
	float shine = 0.0;
	float metalness = 0.0;
	float gloss = 0.0;

	// Liquids (water, lava, acid), flagged in the emissive palette's B channel.
	if (rteIndexed) {
		float colorIndex = texture(rteTexture, textureUV).r;
		shine = texture(rteEmissivePalette, vec2(colorIndex, 0.0)).a;
		float liquid = texture(rteEmissivePalette, vec2(colorIndex, 0.0)).b;
		// Solid terrain looks like what it's made of: steel plating is metal, concrete has a dull sheen, earth has none. The palette's guess (greys shine) is kept for
		// background walls, which have no material, at a lower strength, and for liquids.
		vec4 grid = texture(rteWorldGrid, worldPos / rteGridWorldSize);
		if (grid.r > 0.3 && liquid < 0.1) {
			metalness = grid.g;
			gloss = grid.b;
			shine = gloss;
		} else if (liquid < 0.1) {
			shine *= 0.6;
		}
		if (liquid > 0.1) {
			bool surface = texture(rteEmissivePalette, vec2(texture(rteTexture, textureUV - vec2(0.0, texel.y)).r, 0.0)).b < 0.1;
			float wave = sin(worldPos.x * 0.35 + rteTime * 2.3) * sin(worldPos.y * 0.21 - rteTime * 1.7) + 0.5 * sin(worldPos.x * 0.11 - rteTime * 0.9);
			if (liquid < 0.45) {
				// Water: translucent, deepening in colour with depth, with slow ripples of light and a bright line where it meets the air.
				float depth = 7.0;
				for (int k = 1; k <= 6; ++k) {
					if (texture(rteEmissivePalette, vec2(texture(rteTexture, textureUV - vec2(0.0, texel.y * float(k))).r, 0.0)).b < 0.1) {
						depth = float(k);
						break;
					}
				}
				float deep = smoothstep(1.0, 7.0, depth);
				float ripple = 0.5 + 0.5 * sin(worldPos.x * 0.09 + worldPos.y * 0.05 + rteTime * 1.3 + 1.7 * sin(worldPos.y * 0.07 - rteTime * 0.8));
				vec3 water = mix(vec3(0.27, 0.6, 0.8), vec3(0.06, 0.3, 0.52), deep) * (0.93 + 0.12 * ripple);
				// Light playing through it: thin bright lines that wander and cross, stronger in the depths.
				float bandA = sin(worldPos.x * 0.13 + rteTime * 0.9 + 2.0 * sin(worldPos.y * 0.11 + rteTime * 0.6));
				float bandB = sin(worldPos.x * 0.07 - rteTime * 0.7 + 1.5 * sin(worldPos.y * 0.17 - rteTime * 0.5));
				float caustic = pow(max(0.0, 1.0 - abs(bandA + bandB) * 0.9), 6.0);
				water += vec3(0.22, 0.36, 0.4) * caustic * (0.3 + 0.5 * deep);
				// Glints: here and there near the surface a pixel flashes white for an instant, each at its own pace.
				vec2 glintCell = floor(worldPos / 2.0);
				float glintSeed = fract(sin(dot(glintCell, vec2(12.9898, 78.233))) * 43758.5453);
				float glint = pow(max(0.0, sin(rteTime * (2.0 + glintSeed * 4.0) + glintSeed * 60.0)), 24.0) * step(0.88, glintSeed);
				if (depth <= 3.0) {
					water += vec3(0.9, 0.97, 1.0) * glint * (depth <= 1.0 ? 1.0 : 0.5);
				}
				// Water is glossy: lamps, fires and the sun glance off it.
				shine = max(shine, 0.9);
				FragColor = vec4(water, mix(0.6, 0.8, deep));
				// Open to the air above (not under a ceiling of rock): the surface catches the light and laps a little.
				if (surface && Coverage(textureUV - vec2(0.0, texel.y)) < 0.5) {
					FragColor = vec4(mix(water, vec3(0.82, 0.94, 1.0), 0.6 + 0.2 * wave), 0.92);
				}
			} else if (liquid < 0.8) {
				// Lava: slow bright currents, crusting darker at the surface.
				float flow = 0.5 + 0.5 * sin(worldPos.x * 0.18 + worldPos.y * 0.07 + rteTime * 1.1 + wave);
				FragColor.rgb = mix(vec3(0.75, 0.15, 0.02), vec3(1.0, 0.75, 0.25), flow);
				if (surface) {
					FragColor.rgb *= 0.55;
				}
				emissive = max(emissive, 0.6 + 0.4 * flow);
			} else {
				// Acid: a sickly glow with drifting bubbles.
				float bubble = step(0.985, fract(sin(dot(floor(worldPos + vec2(0.0, rteTime * 8.0)), vec2(41.3, 289.1))) * 7593.1));
				FragColor.rgb = mix(FragColor.rgb, vec3(0.85, 1.0, 0.45), bubble * 0.8 + (surface ? 0.35 : 0.0));
				emissive = max(emissive, 0.25 + bubble * 0.5);
			}
		}
	}

	if (rteLivingWorld && (rteSnowCover > 0.01 || rteWetness > 0.01)) {
		// How deep below the surface this pixel is: snow lies a few pixels deep on top, rain wets the top layer.
		float depth = 99.0;
		for (int k = 1; k <= 5; ++k) {
			if (Coverage(textureUV - vec2(0.0, texel.y * float(k))) < 0.5) {
				depth = float(k);
				break;
			}
		}
		// Only ground the weather can get to: tested from the air just above the surface, along the way the weather is falling.
		if (depth < 99.0 && !WeatherReaches(worldPos - vec2(0.0, depth + 1.0))) {
			depth = 99.0;
		}
		float snowDepth = rteSnowCover * 5.0;
		if (depth <= snowDepth) {
			float brightness = dot(FragColor.rgb, vec3(0.299, 0.587, 0.114));
			vec3 snow = vec3(0.86, 0.9, 0.98) * (0.85 + 0.25 * brightness);
			FragColor.rgb = mix(FragColor.rgb, snow, depth <= snowDepth - 1.0 ? 0.95 : 0.6);
			shine = 0.0;
		} else if (depth <= 3.0 && rteWetness > 0.01) {
			FragColor.rgb *= mix(vec3(1.0), vec3(0.68, 0.7, 0.78), rteWetness);
			// Wet ground glistens under lights.
			shine = max(shine, rteWetness * 0.85);
		}
	}

	if (rteStainsEnabled) {
		// Liquid stains tint the terrain but keep its texture: the stain's color at the terrain's brightness.
		vec4 stain = texture(rteStains, worldPos / rteScorchWorldSize);
		float brightness = dot(FragColor.rgb, vec3(0.299, 0.587, 0.114));
		vec3 stained = stain.rgb * (0.55 + 0.9 * brightness);
		FragColor.rgb = mix(FragColor.rgb, stained, clamp(stain.a, 0.0, 0.85));
	}

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
	// RG: normal x and y. B: 1 - shininess. Alpha: drawn, with emissive strength.
	if (rteRelief > 0.0) {
		// Rough ground would glitter if every speck of its texture tilted it, so the smoother and shinier the material, the more its texture counts.
		normal = normalize(normal + vec3(ReliefTilt(uvDx, uvDy) * rteRelief * mix(0.35, 1.0, max(shine, metalness)), 0.0));
	}
	NormalOut = vec4(normal.xy * 0.5 + 0.5, 1.0 - shine, 0.5 + 0.5 * emissive);
	SurfaceOut = vec4(metalness, gloss, 0.0, 1.0);
}
