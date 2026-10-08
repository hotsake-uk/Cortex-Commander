// Emissive.frag
// Glow sprites (ScreenEffects) are screen blended into an emissive buffer in gamma space, exactly like the original glows, so many overlapping glows saturate gracefully instead of adding up to a white blob.
// The composite then adds the buffer to the HDR scene as emitted light.
#version 330 core

in vec2 textureUV;
in vec4 vertexColor;
out vec4 FragColor;

uniform sampler2D rteTexture;
uniform bool rteUseAlpha; // Multiply by the texture's alpha (soft shapes), for density splats.
uniform bool rteHeatAlpha; // Write how hot the glow is (the vertex alpha) into alpha, for the heat haze; else alpha is 1.

void main() {
	vec4 texel = texture(rteTexture, textureUV);
	float shape = rteUseAlpha ? texel.a : 1.0;
	FragColor = vec4(texel.rgb * shape * vertexColor.rgb, rteHeatAlpha ? vertexColor.a * shape * max(max(texel.r, texel.g), texel.b) : 1.0);
}
