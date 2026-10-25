// Blit8.frag
// The default sprite/layer shader. Indexed textures are resolved through the palette.
// Also writes a screen space normal (location 1) derived from the texture's coverage edges, which gives every sprite and the terrain a small automatic bevel for the lighting to catch,
// and the surface values of what's drawn (location 2): R how metallic, G how glossy, B 1 for solid objects that cast shadows.
#version 330 core

in vec2 textureUV;
in vec4 vertexColor;
in vec4 vertexSurface;
layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 NormalOut;
layout(location = 2) out vec4 SurfaceOut;

uniform sampler2D rteTexture;
uniform sampler2D rtePalette;
uniform bool rteIndexed;
uniform vec4 rteColor;
uniform bool rteReplaceColor;
uniform sampler2D rteEmissivePalette; // 256x1, R = how much each palette color glows.
uniform float rteRelief; // How much the sprite's own shading counts as relief for the lighting, 0 for none.
// A sprite's authored maps (MOSprite's NormalMapFile and EmissiveMapFile), true colour, one image per frame, the size of the frame.
uniform bool rteHasNormalMap; // RGB = normal * 0.5 + 0.5, x to the right and y up the image, as most tools write them. Alpha 0 leaves a pixel to the automatic normal.
uniform sampler2D rteNormalMap;
uniform bool rteHasEmissiveMap; // How much each pixel glows, in its own colour: the brightest of RGB, times alpha.
uniform sampler2D rteEmissiveMap;
uniform vec4 rteMapUVRect; // Where the frame sits in rteTexture (xy corner, zw size, in UV): the sprite atlas puts it anywhere, the maps are the frame alone.
uniform float rteMapStrength; // How much the authored maps count over the automatic ones, 0 to 1.
// The sprite's volume map (SpriteVolume): the sprite rounded off from its outline, made by the game. One image per frame, the size of the frame.
uniform bool rteHasVolumeMap; // RGB = normal * 0.5 + 0.5 in the frame's own axes, y down the image. A 0 outside the sprite.
uniform sampler2D rteVolumeMap;
uniform float rteVolumeStrength; // How steep the rounding is, 0 (flat) to 2.
// The parts of a sprite drawn in its side's colour (MOSprite's TeamColorIndex and TeamColorMapFile).
uniform vec4 rteTeamColor; // RGB the colour, A 1 when anything is recoloured, 0 when not.
uniform vec4 rteTeamColorIndices[2]; // Up to 8 palette indices recoloured wherever they're drawn, -1 for none.
uniform bool rteHasTeamColorMap; // How much each pixel is recoloured: the brightest of RGB, times alpha. True colour, one image per frame, the size of the frame.
uniform sampler2D rteTeamColorMap;
// Blood on the sprite (MOSprite's blood stains): spots in the frame's own pixels, so they turn, flip and animate with it, and only colour what's drawn.
uniform int rteStainCount; // 0 for none.
uniform vec4 rteStains[8]; // xy the centre in the frame's pixels, z the radius in pixels, w how much, 0 to 1.
uniform vec4 rteStainColors[8]; // rgb the colour, a how wet it still is, 0 to 1.

vec4 textureAA(sampler2D tex, vec2 uv) {
	vec2 texsize = vec2(textureSize(tex, 0));
	vec2 uv_texspace = uv * texsize;
	vec2 seam = floor(uv_texspace + .5);
	uv_texspace = (uv_texspace - seam) / fwidth(uv_texspace) + seam;
	uv_texspace = clamp(uv_texspace, seam - .5, seam + .5);
	return texture(tex, uv_texspace / texsize);
}

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
	// Coverage gradient in texels, at two widths for a slightly softer bevel.
	vec2 gradient = vec2(Coverage(textureUV + dx) - Coverage(textureUV - dx), Coverage(textureUV + dy) - Coverage(textureUV - dy));
	gradient += 0.5 * vec2(Coverage(textureUV + 2.0 * dx) - Coverage(textureUV - 2.0 * dx), Coverage(textureUV + 2.0 * dy) - Coverage(textureUV - 2.0 * dy));
	float edge = clamp(length(gradient), 0.0, 1.0);
	if (edge <= 0.0) {
		return vec3(0.0, 0.0, 1.0);
	}
	// Bring the texture space gradient into screen space using how UVs change across screen pixels. This handles rotation, flipping and scaling without needing the transform.
	mat2 uvPerPixel = mat2(uvDx / texel, uvDy / texel);
	vec2 screenGradient = transpose(uvPerPixel) * gradient;
	if (dot(screenGradient, screenGradient) < 1e-8) {
		return vec3(0.0, 0.0, 1.0);
	}
	// Coverage increases into the sprite, so the outward normal points against the gradient.
	vec2 outward = -normalize(screenGradient);
	return normalize(vec3(outward * edge, 1.0 - 0.6 * edge));
}

// How high a point of the sprite stands, going by how bright the art is there: pixel artists paint what sticks out lighter. Below zero outside the sprite.
float Height(vec2 uv) {
	if (rteIndexed) {
		float colorIndex = texture(rteTexture, uv).r;
		return colorIndex > 0.0 ? dot(texture(rtePalette, vec2(colorIndex, 0.0)).rgb, vec3(0.299, 0.587, 0.114)) : -1.0;
	}
	vec4 color = texture(rteTexture, uv);
	return color.a > 0.5 ? dot(color.rgb, vec3(0.299, 0.587, 0.114)) : -1.0;
}

// The tilt the art's own shading implies inside the sprite, in screen space: plates, rivets and folds painted lighter and darker lean the surface, so lights and reflections pick them out.
// The outline is left to EdgeNormal: neighbours outside the sprite count as level with this pixel.
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
	// Downhill is away from the lighter side.
	return -(transpose(uvPerPixel) * gradient) * 2.2;
}

// A steady random number for a pixel, 0 to 1.
float PixelHash(vec2 cell) {
	return fract(sin(dot(cell, vec2(12.9898, 78.233))) * 43758.5453);
}

// The frame's pixel in its authored maps.
ivec2 MapTexel(sampler2D map) {
	ivec2 size = textureSize(map, 0);
	vec2 frameUV = (textureUV - rteMapUVRect.xy) / max(rteMapUVRect.zw, vec2(1e-6));
	return clamp(ivec2(floor(frameUV * vec2(size))), ivec2(0), size - 1);
}

void main() {
	// Derivatives must be taken in uniform control flow, before any discard.
	vec2 uvDx = dFdx(textureUV);
	vec2 uvDy = dFdy(textureUV);
	vec2 relief = rteRelief > 0.0 ? ReliefTilt(uvDx, uvDy) * rteRelief : vec2(0.0);
	float emissive = 0.0;
	float shine = 0.0;
	float colorIndex = 0.0;
	if (rteIndexed) {
		colorIndex = texture(rteTexture, vec2(textureUV.x, textureUV.y)).r;
		FragColor = texture(rtePalette, vec2(colorIndex, 0.0F)) * vertexColor;
		vec4 surface = texture(rteEmissivePalette, vec2(colorIndex, 0.0F));
		emissive = surface.r;
		shine = surface.a;
	} else {
		FragColor = textureAA(rteTexture, textureUV) * vertexColor;
	}
	if (FragColor.a == 0.0) {
		discard;
	} else if (rteReplaceColor) {
		FragColor.rgba = rteColor;
	}
	if (rteTeamColor.a > 0.0 && !rteReplaceColor) {
		float recolor = 0.0;
		if (rteIndexed) {
			float index = floor(colorIndex * 255.0 + 0.5);
			for (int i = 0; i < 2; ++i) {
				vec4 marked = rteTeamColorIndices[i];
				if (any(equal(vec4(index), marked))) {
					recolor = 1.0;
				}
			}
		}
		if (rteHasTeamColorMap) {
			vec4 mask = texelFetch(rteTeamColorMap, MapTexel(rteTeamColorMap), 0);
			recolor = max(recolor, max(mask.r, max(mask.g, mask.b)) * mask.a);
		}
		if (recolor > 0.0) {
			// The art's own shading is kept: lighter pixels take a lighter shade of the colour, so folds and highlights still read.
			float brightness = dot(FragColor.rgb, vec3(0.299, 0.587, 0.114));
			vec3 recolored = clamp(rteTeamColor.rgb * (0.25 + 1.5 * brightness), 0.0, 1.0);
			FragColor.rgb = mix(FragColor.rgb, recolored, recolor);
		}
	}
	float stainShine = 0.0;
	if (rteStainCount > 0 && !rteReplaceColor) {
		// Whole art pixels, with ragged edges, so a stain reads as pixel art rather than a soft blur.
		vec2 cell = floor((textureUV - rteMapUVRect.xy) * vec2(textureSize(rteTexture, 0)));
		float covered = 0.0;
		vec4 stainColor = vec4(0.0);
		for (int i = 0; i < 8; ++i) {
			if (i >= rteStainCount) {
				break;
			}
			vec4 spot = rteStains[i];
			float distance = length(cell + 0.5 - spot.xy) / max(spot.z, 0.5);
			float ragged = PixelHash(cell + floor(spot.xy) * 7.0) - 0.5;
			float cover = clamp((1.0 - distance) * 2.5 + ragged * 1.4, 0.0, 1.0) * spot.w;
			if (cover > covered) {
				covered = cover;
				stainColor = rteStainColors[i];
			}
		}
		if (covered > 0.0) {
			// Fresh blood is its own bright colour; dried, it goes dark and brown. The art's shading shows through either way.
			vec3 dried = mix(stainColor.rgb, vec3(dot(stainColor.rgb, vec3(0.299, 0.587, 0.114))), 0.3) * vec3(0.5, 0.42, 0.38);
			vec3 blood = mix(dried, stainColor.rgb, stainColor.a);
			float brightness = dot(FragColor.rgb, vec3(0.299, 0.587, 0.114));
			FragColor.rgb = mix(FragColor.rgb, clamp(blood * (0.55 + 0.9 * brightness), 0.0, 1.0), clamp(covered, 0.0, 0.92));
			stainShine = stainColor.a * covered * 0.6;
		}
	}
	// RG: normal x and y (z is worked out from them). B: 1 - shininess. Alpha: 0 means nothing drawn, 0.5..1 is drawn with emissive strength 0..1.
	// What has happened to the object, packed into the surface's last value: heat in the high half, snow in the low.
	// A shining thing with no surface of its own (a tracer and its trail) is marked by a half value where solid objects say whether they cast shadows; its last value is then how strongly it shines.
	bool shining = abs(vertexSurface.b - 0.5) < 0.1;
	// Marked for the LCD ghosting (LightingSettings::c_GhostSurfaceMark, or c_GhostShiningMark for a shining thing): written out as the plain mark,
	// which every reader of the solid flag takes as nothing solid, no water and no unit.
	float surfaceMark = floor(vertexSurface.b * 255.0 + 0.5);
	bool ghosted = surfaceMark == 96.0 || surfaceMark == 112.0;
	if (shining) {
		emissive = max(emissive, vertexSurface.a);
	}
	float packedStates = shining ? 0.0 : floor(vertexSurface.a * 255.0 + 0.5);
	float heat = floor(packedStates / 16.0) / 15.0;
	float snow = mod(packedStates, 16.0) / 15.0;
	if (snow > 0.0) {
		// Snow lies where nothing of the sprite is above: its top pixel, and thinner on the one below. "Above" is up the screen, whichever way the sprite is turned.
		float lying = 1.0 - Coverage(textureUV - uvDy);
		lying = max(lying, 0.3 * (1.0 - Coverage(textureUV - uvDy * 2.0)));
		FragColor.rgb = mix(FragColor.rgb, vec3(0.9, 0.93, 1.0), clamp(lying * snow * 1.4, 0.0, 0.95));
		shine *= 1.0 - lying * snow;
	}
	if (heat > 0.0) {
		// Hot metal glows from dull red to orange.
		vec3 glow = mix(vec3(0.75, 0.12, 0.03), vec3(1.0, 0.6, 0.18), heat);
		float brightness = dot(FragColor.rgb, vec3(0.3, 0.55, 0.15));
		FragColor.rgb = mix(FragColor.rgb, glow * (0.6 + 1.1 * brightness), heat * 0.75);
		emissive = max(emissive, heat * 0.85);
	}
	vec3 base = EdgeNormal(uvDx, uvDy);
	float reliefScale = 1.0;
	if (rteHasVolumeMap) {
		// The rounded surface, brought onto the screen the same way the automatic tilt is, which handles turning, flipping and scaling.
		// The outline's own bevel is kept a little, so a part only a pixel or two wide, which the rounding can't tilt, still catches the light at its edge.
		vec4 volume = texelFetch(rteVolumeMap, MapTexel(rteVolumeMap), 0);
		if (volume.a > 0.5) {
			vec2 tilt = volume.rg * 2.0 - 1.0;
			vec2 texel = 1.0 / vec2(textureSize(rteTexture, 0));
			vec2 screenTilt = transpose(mat2(uvDx / texel, uvDy / texel)) * tilt;
			screenTilt = dot(screenTilt, screenTilt) > 1e-8 ? normalize(screenTilt) * length(tilt) : vec2(0.0);
			vec3 rounded = normalize(vec3(screenTilt * rteVolumeStrength, max(volume.b * 2.0 - 1.0, 0.05)));
			base = normalize(rounded + vec3(base.xy * 0.4, 0.0));
			// The painted relief tilts every pixel against its neighbours, several times more steeply than the rounding, so at full strength it would bury the shape.
			// Toned down the more the sprite is rounded: the detail still catches the light, on top of the shape.
			reliefScale = mix(1.0, 0.3, clamp(rteVolumeStrength, 0.0, 1.0));
		}
	}
	vec3 normal = normalize(base + vec3(relief * reliefScale, 0.0));
	if (rteHasNormalMap) {
		// The artist's normal, brought from the image's axes onto the screen the same way the automatic tilt is (which handles turning, flipping and scaling),
		// and as steep as the relief setting makes the art's own shading.
		vec4 authored = texelFetch(rteNormalMap, MapTexel(rteNormalMap), 0);
		vec2 tilt = vec2(authored.r, 1.0 - authored.g) * 2.0 - 1.0; // Image y up becomes texture y down.
		vec2 texel = 1.0 / vec2(textureSize(rteTexture, 0));
		vec2 screenTilt = transpose(mat2(uvDx / texel, uvDy / texel)) * tilt;
		screenTilt = dot(screenTilt, screenTilt) > 1e-8 ? normalize(screenTilt) * length(tilt) : vec2(0.0);
		vec3 drawn = normalize(vec3(screenTilt * clamp(rteRelief / 0.6, 0.0, 2.0), max(authored.b * 2.0 - 1.0, 0.05)));
		normal = normalize(mix(normal, drawn, rteMapStrength * authored.a));
	}
	if (rteHasEmissiveMap) {
		vec4 glow = texelFetch(rteEmissiveMap, MapTexel(rteEmissiveMap), 0);
		emissive = max(emissive, max(glow.r, max(glow.g, glow.b)) * glow.a * rteMapStrength);
	}
	// An object that says what it's made of is as glossy as that; the palette's guess (greys are metal or concrete) only counts in full for things that don't say, like particles.
	bool hasSurface = vertexSurface.r + vertexSurface.g > 0.0;
	if (hasSurface) {
		shine = max(shine * 0.5, vertexSurface.g);
	}
	// Wet blood glistens, whatever it's on.
	shine = max(shine, stainShine);
	NormalOut = vec4(normal.xy * 0.5 + 0.5, 1.0 - shine, 0.5 + 0.5 * emissive);
	SurfaceOut = vec4(vertexSurface.rg, ghosted ? 96.0 / 255.0 : shining ? 0.0 : vertexSurface.b, 1.0);
}
