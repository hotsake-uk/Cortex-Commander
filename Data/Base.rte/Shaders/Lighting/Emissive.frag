// Emissive.frag
// Glow sprites (ScreenEffects) are screen blended into an emissive buffer in gamma space, exactly like the original glows, so many overlapping glows saturate gracefully instead of adding up to a white blob.
// The composite then adds the buffer to the HDR scene as emitted light.
#version 330 core

in vec2 textureUV;
in vec4 vertexColor;
in float vertexLayer; // 1 for a glowing particle in the effects layer (sparks, embers, explosion fire), behind units and the ground in front.
out vec4 FragColor;

uniform sampler2D rteTexture;
uniform bool rteUseAlpha; // Multiply by the texture's alpha (soft shapes), for density splats.
uniform bool rteHeatAlpha; // Write how hot the glow is (the vertex alpha) into alpha, for the heat haze; else alpha is 1.
uniform sampler2D rteSceneDepth; // The player screen's depth buffer.
uniform bool rteEffectsLayer; // Some glows are drawn in the effects layer (LightingSettings::Behind).
uniform float rteEffectsFrontDepth; // Depth below which a pixel is in front of the effects layer: a unit or the ground in front.
uniform vec2 rteScreenSize;

void main() {
	if (rteEffectsLayer && vertexLayer > 0.5 && texture(rteSceneDepth, gl_FragCoord.xy / rteScreenSize).r < rteEffectsFrontDepth) {
		discard;
	}
	vec4 texel = texture(rteTexture, textureUV);
	float shape = rteUseAlpha ? texel.a : 1.0;
	FragColor = vec4(texel.rgb * shape * vertexColor.rgb, rteHeatAlpha ? vertexColor.a * shape * max(max(texel.r, texel.g), texel.b) : 1.0);
}
