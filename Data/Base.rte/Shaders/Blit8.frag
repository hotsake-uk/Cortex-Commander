// Blit8.frag
// The default sprite/layer shader. Indexed textures are resolved through the palette.
// Also writes a screen space normal (location 1) derived from the texture's coverage edges, which gives every sprite and the terrain a small automatic bevel for the lighting to catch.
#version 330 core

in vec2 textureUV;
in vec4 vertexColor;
layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 NormalOut;

uniform sampler2D rteTexture;
uniform sampler2D rtePalette;
uniform bool rteIndexed;
uniform vec4 rteColor;
uniform bool rteReplaceColor;

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

void main() {
	// Derivatives must be taken in uniform control flow, before any discard.
	vec2 uvDx = dFdx(textureUV);
	vec2 uvDy = dFdy(textureUV);
	if (rteIndexed) {
		float colorIndex = texture(rteTexture, vec2(textureUV.x, textureUV.y)).r;
		FragColor = texture(rtePalette, vec2(colorIndex, 0.0F)) * vertexColor;
	} else {
		FragColor = textureAA(rteTexture, textureUV) * vertexColor;
	}
	if (FragColor.a == 0.0) {
		discard;
	} else if (rteReplaceColor) {
		FragColor.rgba = rteColor;
	}
	NormalOut = vec4(EdgeNormal(uvDx, uvDy) * 0.5 + 0.5, 1.0);
}
