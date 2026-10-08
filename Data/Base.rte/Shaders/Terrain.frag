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
uniform sampler2D rteEmissivePalette; // 256x1, R = how much each palette color glows, G = whether it's a vegetation color, B = its liquid look * 16 (0 not a liquid).

// The liquid looks (RenderMan::LiquidLook), by look number: 1 water, 2 lava, 3 acid, 4 oil, 5 mud, 6 slime, 7 mercury, the rest free for new liquids.
const int c_MaxLiquidLooks = 16;
uniform vec4 rteLiquidShallow[c_MaxLiquidLooks]; // RGB colour near the surface (molten: the cool colour; bubbling: the bubbles' colour), A opacity there.
uniform vec4 rteLiquidDeep[c_MaxLiquidLooks]; // RGB colour in the depths (molten: the hot colour), A opacity there.
uniform vec4 rteLiquidSurface[c_MaxLiquidLooks]; // x shine, y metalness, z how much the ripples tilt it, w how strongly light plays through it in lines.
uniform vec4 rteLiquidStyle[c_MaxLiquidLooks]; // x 0 clear, 1 molten, 2 bubbling; y how much it froths when thin; z its own glow; w 1 if it reflects (the composite's water reflection).
uniform vec4 rteLiquidLine[c_MaxLiquidLooks]; // RGB the colour of its surface line where it meets open air, A how strongly.

uniform bool rteLivingWorld;
uniform float rteTime; // Seconds.
uniform float rteWind; // Pixels per second, negative blows left.
uniform float rteSnowCover; // 0..1, how deep snow lies on exposed ground.
uniform float rteWetness; // 0..1, how wet exposed ground is.
uniform float rteWaterFoamStray; // How much of that froth a stray pixel or two of water gets, against a stream of them: 0 none (they stay bare pixels), 1 as much.
uniform float rteWaterFoamBright; // How bright the froth is drawn.
uniform float rteWaterFoamBubbles; // How much the froth bubbles (flickers lighter and darker): 0 smooth like still water, 1 lively.
uniform float rteWaterFoamGlow; // How much light of its own the froth carries, so it shows in the dark.
uniform float rteWaterFoam; // How much thin, broken water (a stream off a ledge, spray, the lip of a pour) is drawn as froth. 0 for none.
uniform float rteWaterRipples; // How much the surface ripples tilt water's normal, for the reflection and the glints on it. 0 for flat (as before).
uniform sampler2D rteFlowField; // The moving liquid (FluidSim), in the light grid's cells: R sideways speed (0.5 none), G speed, B how lately it moved (1 just now, 0 settled), A whether any moves there.
uniform float rteFlowSurface; // How much liquid's surface follows how it moves: 0 the slow waves alone (as before), 1 fully.
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
		if (textureLod(rteWorldGrid, p / rteGridWorldSize, 0.0).a > 0.55) {
			return false;
		}
		p += back * (i < 20 ? 1.0 : (i < 40 ? 2.0 : 5.0));
	}
	return true;
}

// The slope of the slow surface waves at a place, for tilting liquid's normal.
vec2 RippleSlope(vec2 pos) {
	float phaseX = pos.x * 0.35 + rteTime * 2.3;
	float phaseY = pos.y * 0.21 - rteTime * 1.7;
	return vec2(0.35 * cos(phaseX) * sin(phaseY) + 0.055 * cos(pos.x * 0.11 - rteTime * 0.9), 0.21 * sin(phaseX) * cos(phaseY));
}

// The liquid look of a palette colour, 0 for none.
int LiquidLook(float colorIndex) {
	return clamp(int(texture(rteEmissivePalette, vec2(colorIndex, 0.0)).b * 255.0 / 16.0 + 0.5), 0, c_MaxLiquidLooks - 1);
}

// 1 where the terrain pixel at a place is a liquid that froths (water), 0 otherwise.
float WaterAt(vec2 uv) {
	int look = LiquidLook(texture(rteTexture, uv).r);
	return (look > 0 && rteLiquidStyle[look].x < 0.5 && rteLiquidStyle[look].y > 0.0) ? 1.0 : 0.0;
}

// How much of the neighbourhood of a pixel is water, 0 to 1: twelve places within two pixels of it.
float WaterAround(vec2 uv, vec2 texel) {
	float sum = 0.0;
	sum += WaterAt(uv + vec2(texel.x, 0.0)) + WaterAt(uv - vec2(texel.x, 0.0)) + WaterAt(uv + vec2(0.0, texel.y)) + WaterAt(uv - vec2(0.0, texel.y));
	sum += WaterAt(uv + texel) + WaterAt(uv - texel) + WaterAt(uv + vec2(texel.x, -texel.y)) + WaterAt(uv + vec2(-texel.x, texel.y));
	sum += WaterAt(uv + vec2(2.0 * texel.x, 0.0)) + WaterAt(uv - vec2(2.0 * texel.x, 0.0)) + WaterAt(uv + vec2(0.0, 2.0 * texel.y)) + WaterAt(uv - vec2(0.0, 2.0 * texel.y));
	return sum / 12.0;
}

// The thin bright lines of light that wander and cross through water. The same in still water and in the froth of a pour, so the two look like one thing.
float WaterCaustic(vec2 world) {
	float bandA = sin(world.x * 0.13 + rteTime * 0.9 + 2.0 * sin(world.y * 0.11 + rteTime * 0.6));
	float bandB = sin(world.x * 0.07 - rteTime * 0.7 + 1.5 * sin(world.y * 0.17 - rteTime * 0.5));
	return pow(max(0.0, 1.0 - abs(bandA + bandB) * 0.9), 6.0);
}

// Water's own colour near the surface, with its slow ripple of light.
vec3 WaterShallow(vec2 world) {
	float ripple = 0.5 + 0.5 * sin(world.x * 0.09 + world.y * 0.05 + rteTime * 1.3 + 1.7 * sin(world.y * 0.07 - rteTime * 0.8));
	return vec3(0.27, 0.6, 0.8) * (0.93 + 0.12 * ripple);
}

// Froth flickers: each pixel of it a different brightness, changing many times a second, like bubbles forming and bursting.
float FrothFlicker(vec2 world) {
	return fract(sin(dot(floor(world) + floor(rteTime * 9.0) * vec2(3.1, 7.7), vec2(12.9898, 78.233))) * 43758.5453);
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
		if (rteIndexed && rteWaterFoam > 0.0) {
			// Air around thin, broken water is drawn as part of it, so a few pixels of water read as a body of flowing, frothing water and not as pixels:
			// close in, the blue of the water itself; further out and up and down its fall, white froth. Not the air over a pool: that has water right across beneath it.
			float around = WaterAround(textureUV, texel);
			if (around < 0.45) {
				// Weighted by distance, so a lone pixel of water gets a round blob about it and not the shape of the places looked at.
				float beside = WaterAt(textureUV + vec2(texel.x, 0.0)) + WaterAt(textureUV - vec2(texel.x, 0.0)) + WaterAt(textureUV + vec2(0.0, texel.y)) + WaterAt(textureUV - vec2(0.0, texel.y));
				float corners = WaterAt(textureUV + texel) + WaterAt(textureUV - texel) + WaterAt(textureUV + vec2(texel.x, -texel.y)) + WaterAt(textureUV + vec2(-texel.x, texel.y));
				float twoOff = WaterAt(textureUV + vec2(2.0 * texel.x, 0.0)) + WaterAt(textureUV - vec2(2.0 * texel.x, 0.0)) + WaterAt(textureUV + vec2(0.0, 2.0 * texel.y)) + WaterAt(textureUV - vec2(0.0, 2.0 * texel.y));
				// Water a little way above and below: what joins the separate drops of a falling stream into one, as a tail that thins out.
				// Only above a drop (water a few pixels below this air), so each falling drop draws a short tail behind it like a streak, and not a cross.
				float column = 0.5 * WaterAt(textureUV + vec2(0.0, 3.0 * texel.y)) + 0.3 * WaterAt(textureUV + vec2(0.0, 4.0 * texel.y)) + 0.15 * WaterAt(textureUV + vec2(0.0, 5.0 * texel.y));
				float poolBelow = max(WaterAt(textureUV + vec2(-2.0 * texel.x, texel.y)) * WaterAt(textureUV + vec2(2.0 * texel.x, texel.y)),
				                      WaterAt(textureUV + vec2(-2.0 * texel.x, 2.0 * texel.y)) * WaterAt(textureUV + vec2(2.0 * texel.x, 2.0 * texel.y)) * WaterAt(textureUV + vec2(0.0, 2.0 * texel.y)));
				// Soft at the edges: full only where water is right beside, falling away over the two pixels beyond.
				float close = clamp(beside * 0.75 + corners * 0.7 + twoOff * 0.22, 0.0, 1.0);
				float presence = max(close, clamp(column, 0.0, 1.0) * 0.7) * (1.0 - smoothstep(0.3, 0.45, around)) * (1.0 - poolBelow);
				// A stray pixel or two thrown clear of the rest gets less of it than a stream does, or every flying drop becomes a blob.
				float stray = 1.0 - smoothstep(1.0, 3.5, beside + corners + twoOff + column * 2.0);
				presence *= mix(1.0, rteWaterFoamStray, stray);
				if (presence > 0.02) {
					// Drawn as water is: its blue with the same lines of light running through it, paling towards the edge the way water's surface line does.
					// A touch of bubbling keeps it alive, far short of the white sparkle it had, which looked like a different thing from the water it came off.
					float blobs = fract(sin(dot(floor(worldPos / 2.0) + floor(rteTime * 4.0) * vec2(5.3, 1.9), vec2(41.3, 289.1))) * 7593.1);
					float strength = min(rteWaterFoam, 1.5);
					vec3 color = WaterShallow(worldPos) + vec3(0.22, 0.36, 0.4) * WaterCaustic(worldPos) * 0.45;
					float edge = 1.0 - close;
					color = mix(color, vec3(0.82, 0.94, 1.0), clamp(edge * 0.55 + (blobs - 0.5) * 0.25 * rteWaterFoamBubbles, 0.0, 0.85)) * rteWaterFoamBright;
					float alpha = clamp(presence * (0.62 + 0.12 * blobs * rteWaterFoamBubbles) * strength, 0.0, 0.85);
					FragColor = vec4(color, alpha);
					// It carries a little light of its own, so falling water shows in the dark as it does by day.
					NormalOut = vec4(0.5, 0.5, 0.6, 0.5 + 0.5 * clamp(rteWaterFoamGlow * strength, 0.0, 1.0));
					SurfaceOut = vec4(0.0, 0.0, 0.0, 1.0);
					return;
				}
			}
		}
		discard;
	} else if (rteReplaceColor) {
		FragColor.rgba = rteColor;
	}
	vec3 normal = EdgeNormal(uvDx, uvDy);
	float shine = 0.0;
	float metalness = 0.0;
	float gloss = 0.0;
	float glowsThrough = 0.0; // Set for water: light inside it shows as a glow in the water itself (see the surface buffer's B channel).

	// Liquids, flagged in the emissive palette's B channel with their look.
	if (rteIndexed) {
		float colorIndex = texture(rteTexture, textureUV).r;
		shine = texture(rteEmissivePalette, vec2(colorIndex, 0.0)).a;
		int look = LiquidLook(colorIndex);
		// Solid terrain looks like what it's made of: steel plating is metal, concrete has a dull sheen, earth has none. The palette's guess (greys shine) is kept for
		// background walls, which have no material, at a lower strength, and for liquids.
		vec4 grid = texture(rteWorldGrid, worldPos / rteGridWorldSize);
		if (grid.r > 0.3 && look == 0) {
			metalness = grid.g;
			gloss = grid.b;
			shine = gloss;
		} else if (look == 0) {
			shine *= 0.6;
		}
		if (look > 0) {
			vec4 lookShallow = rteLiquidShallow[look];
			vec4 lookDeep = rteLiquidDeep[look];
			vec4 lookSurface = rteLiquidSurface[look];
			vec4 lookStyle = rteLiquidStyle[look];
			bool surface = LiquidLook(texture(rteTexture, textureUV - vec2(0.0, texel.y)).r) == 0;
			float wave = sin(worldPos.x * 0.35 + rteTime * 2.3) * sin(worldPos.y * 0.21 - rteTime * 1.7) + 0.5 * sin(worldPos.x * 0.11 - rteTime * 0.9);
			if (lookStyle.x < 0.5) {
				// Clear liquids (water, oil, mud, mercury): deepening in colour with depth, with slow ripples of light and a line where they meet the air.
				float depth = 7.0;
				for (int k = 1; k <= 6; ++k) {
					if (LiquidLook(texture(rteTexture, textureUV - vec2(0.0, texel.y * float(k))).r) == 0) {
						depth = float(k);
						break;
					}
				}
				float deep = smoothstep(1.0, 7.0, depth);
				// How the liquid here moves (FluidSim's moving pixels, a light grid cell at a time). Still water goes glassy, moving water ripples harder with
				// its waves carried downstream, and fast, fresh churn froths.
				vec4 flow = vec4(128.0 / 255.0, 0.0, 0.0, 0.0);
				float flowAmount = 0.0;
				if (rteFlowSurface > 0.0 && lookSurface.z > 0.0) {
					flow = texture(rteFlowField, worldPos / rteGridWorldSize);
					flowAmount = min(rteFlowSurface, 1.0);
				}
				float moving = flow.a;
				float sideways = (flow.r * 255.0 - 128.0) / 127.0;
				float churn = smoothstep(0.3, 0.8, flow.g) * flow.b * moving * flowAmount;
				float ripple = 0.5 + 0.5 * sin(worldPos.x * 0.09 + worldPos.y * 0.05 + rteTime * 1.3 + 1.7 * sin(worldPos.y * 0.07 - rteTime * 0.8));
				vec3 water = mix(lookShallow.rgb, lookDeep.rgb, deep) * (0.93 + 0.12 * ripple);
				// Light playing through it: thin bright lines that wander and cross, stronger in the depths.
				float caustic = WaterCaustic(worldPos);
				water += vec3(0.22, 0.36, 0.4) * caustic * (0.3 + 0.5 * deep) * lookSurface.w;
				// Glints: here and there near the surface a pixel flashes white for an instant, each at its own pace.
				vec2 glintCell = floor(worldPos / 2.0);
				float glintSeed = fract(sin(dot(glintCell, vec2(12.9898, 78.233))) * 43758.5453);
				float glint = pow(max(0.0, sin(rteTime * (2.0 + glintSeed * 4.0) + glintSeed * 60.0)), 24.0) * step(0.88, glintSeed);
				if (depth <= 3.0 && lookSurface.x >= 0.5) {
					water += vec3(0.9, 0.97, 1.0) * glint * (depth <= 1.0 ? 1.0 : 0.5);
				}
				// Glossy liquids: lamps, fires and the sun glance off them. Mercury is metal too.
				shine = max(shine, lookSurface.x);
				metalness = lookSurface.y;
				if (rteWaterRipples > 0.0 && lookSurface.z > 0.0) {
					// The ripples tilt the surface: the slope of the same slow waves the light plays on, so the glints of lamps and the sun and
					// the composite's reflection wobble with them. Stronger near the top, calmer in the depths.
					vec2 slope = RippleSlope(worldPos);
					vec2 lean = vec2(0.0);
					float calm = 1.0;
					if (flowAmount > 0.0) {
						if (moving > 0.01) {
							// Carried downstream: two copies of the waves drift with the flow, each faded out while it jumps back, so they never stretch.
							float phase = fract(rteTime * 0.5);
							vec2 drift = vec2(sideways * 14.0 * moving * flowAmount, 0.0);
							slope = mix(RippleSlope(worldPos - drift * phase), RippleSlope(worldPos - drift * fract(phase + 0.5)), abs(phase * 2.0 - 1.0));
						}
						// Still water goes glassy; moving water ripples harder, the faster the more.
						calm = mix(1.0, mix(0.3, 1.0 + flow.g, moving), flowAmount);
						// The surface leans off where the water moves fastest: rings round where a pour lands, a ridge along a stream.
						vec2 cellStep = rteGridWorldSize / vec2(textureSize(rteFlowField, 0));
						lean = vec2(texture(rteFlowField, (worldPos + vec2(cellStep.x, 0.0)) / rteGridWorldSize).g - texture(rteFlowField, (worldPos - vec2(cellStep.x, 0.0)) / rteGridWorldSize).g,
						            texture(rteFlowField, (worldPos + vec2(0.0, cellStep.y)) / rteGridWorldSize).g - texture(rteFlowField, (worldPos - vec2(0.0, cellStep.y)) / rteGridWorldSize).g) * 0.6 * flowAmount;
					}
					normal = normalize(normal + vec3(slope * 0.45 * rteWaterRipples * lookSurface.z * calm * mix(1.0, 0.5, deep) + lean * rteWaterRipples * lookSurface.z, 0.0));
				}
				glowsThrough = 0.25 * lookStyle.w;
				FragColor = vec4(water, mix(lookShallow.a, lookDeep.a, deep));
				// Fast, fresh churn (where a pour lands, a rapid) froths through the body of the water, not only where it's thin.
				if (churn > 0.0 && rteWaterFoam > 0.0 && lookStyle.y > 0.0) {
					float churned = clamp(churn * min(rteWaterFoam, 1.5) * lookStyle.y, 0.0, 1.0);
					float flicker = FrothFlicker(worldPos);
					FragColor.rgb = mix(FragColor.rgb, vec3(0.82, 0.94, 1.0) * rteWaterFoamBright, clamp(churned * (0.3 + 0.3 * (flicker - 0.5) * rteWaterFoamBubbles), 0.0, 1.0));
					emissive = max(emissive, rteWaterFoamGlow * churned * 0.5);
					shine = mix(shine, 0.4, churned);
				}
				// Thin, broken water is froth: white and bubbling instead of clear. (Checked only where there's air close by, which the middle of a pool never has.)
				if (rteWaterFoam > 0.0 && lookStyle.y > 0.0 && WaterAt(textureUV + vec2(2.0 * texel.x, 0.0)) * WaterAt(textureUV - vec2(2.0 * texel.x, 0.0)) * WaterAt(textureUV + vec2(0.0, 3.0 * texel.y)) * WaterAt(textureUV - vec2(0.0, 3.0 * texel.y)) < 0.5) {
					float waterNear = WaterAround(textureUV, texel);
					float thin = (1.0 - smoothstep(0.35, 0.75, waterNear)) * min(rteWaterFoam, 1.5) * lookStyle.y;
					// A stray pixel thrown clear of the rest is frothed less than a stream, by the same setting as the froth around it.
					thin *= mix(rteWaterFoamStray, 1.0, smoothstep(0.0, 0.2, waterNear));
					if (thin > 0.0) {
						// Thin water keeps water's colour and lines of light; it is only paler, towards the tone of water's surface line, with a little bubbling.
						float flicker = FrothFlicker(worldPos);
						FragColor.rgb = mix(FragColor.rgb, vec3(0.82, 0.94, 1.0) * rteWaterFoamBright, clamp(thin * (0.38 + 0.3 * (flicker - 0.5) * rteWaterFoamBubbles), 0.0, 1.0));
						FragColor.a = mix(FragColor.a, 0.96, clamp(thin, 0.0, 1.0));
						// Froth carries a little light of its own, so it shows in the dark.
						emissive = max(emissive, rteWaterFoamGlow * clamp(thin, 0.0, 1.0));
						// And it isn't glossy like still water (which is drawn darker for what it reflects).
						shine = mix(shine, 0.4, clamp(thin, 0.0, 1.0));
					}
				}
				// Open to the air above (not under a ceiling of rock): the surface catches the light and laps a little.
				if (surface && Coverage(textureUV - vec2(0.0, texel.y)) < 0.5) {
					FragColor = vec4(mix(water, rteLiquidLine[look].rgb, (0.6 + 0.2 * wave) * rteLiquidLine[look].a), mix(FragColor.a, 0.92, rteLiquidLine[look].a));
				}
			} else if (lookStyle.x < 1.5) {
				// Molten (lava): slow bright currents, crusting darker at the surface.
				float flow = 0.5 + 0.5 * sin(worldPos.x * 0.18 + worldPos.y * 0.07 + rteTime * 1.1 + wave);
				FragColor.rgb = mix(lookShallow.rgb, lookDeep.rgb, flow);
				if (surface) {
					FragColor.rgb *= 0.55;
				}
				emissive = max(emissive, lookStyle.z * (0.6 + 0.4 * flow));
			} else {
				// Bubbling (acid, slime): a sickly glow with drifting bubbles.
				float bubble = step(0.985, fract(sin(dot(floor(worldPos + vec2(0.0, rteTime * 8.0)), vec2(41.3, 289.1))) * 7593.1));
				FragColor.rgb = mix(FragColor.rgb, lookShallow.rgb, bubble * 0.8 + (surface ? 0.35 : 0.0));
				emissive = max(emissive, lookStyle.z * (0.25 + bubble * 0.5));
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
	// B: 0 for ground, a quarter for water (which light glows through; solid objects that cast shadows write 1 here, never terrain).
	SurfaceOut = vec4(metalness, gloss, glowsThrough, 1.0);
}
