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

void main() {
	// Derivatives must be taken in uniform control flow, before any discard.
	vec2 uvDx = dFdx(textureUV);
	vec2 uvDy = dFdy(textureUV);
	vec2 relief = rteRelief > 0.0 ? ReliefTilt(uvDx, uvDy) * rteRelief : vec2(0.0);
	float emissive = 0.0;
	float shine = 0.0;
	if (rteIndexed) {
		float colorIndex = texture(rteTexture, vec2(textureUV.x, textureUV.y)).r;
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
	// RG: normal x and y (z is worked out from them). B: 1 - shininess. Alpha: 0 means nothing drawn, 0.5..1 is drawn with emissive strength 0..1.
	// What has happened to the object, packed into the surface's last value: heat in the high half, snow in the low.
	float packedStates = floor(vertexSurface.a * 255.0 + 0.5);
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
	vec3 normal = normalize(EdgeNormal(uvDx, uvDy) + vec3(relief, 0.0));
	// An object that says what it's made of is as glossy as that; the palette's guess (greys are metal or concrete) only counts in full for things that don't say, like particles.
	bool hasSurface = vertexSurface.r + vertexSurface.g > 0.0;
	if (hasSurface) {
		shine = max(shine * 0.5, vertexSurface.g);
	}
	NormalOut = vec4(normal.xy * 0.5 + 0.5, 1.0 - shine, 0.5 + 0.5 * emissive);
	SurfaceOut = vec4(vertexSurface.rgb, 1.0);
}
