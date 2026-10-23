// GhostMask.frag
// The LCD ghosting's mask (LightingSettings::LCDGhostSelected): how much of each pixel is something that smears. The glow and particle quads it
// picks are screen blended in, each as much as it covers the pixel; the sprites it picks are added from their marks in the surface buffer
// (Afterglow.frag mode 4).
#version 330 core

in vec2 textureUV;
in vec4 vertexColor;
in float vertexLayer; // 1 for a particle in the effects layer, behind units and the ground in front (LightingSettings::Behind).
out vec4 FragColor;

uniform sampler2D rteTexture;
uniform bool rteUseAlpha; // Multiply by the texture's alpha (soft shapes).
uniform bool rteGlow; // A glow (how bright it is is how much it covers), else a lit particle (its opacity).
uniform sampler2D rteSceneDepth;
uniform bool rteEffectsLayer;
uniform float rteEffectsFrontDepth;
uniform vec2 rteScreenSize;

void main() {
	if (rteEffectsLayer && vertexLayer > 0.5 && texture(rteSceneDepth, gl_FragCoord.xy / rteScreenSize).r < rteEffectsFrontDepth) {
		discard;
	}
	vec4 texel = texture(rteTexture, textureUV);
	float shape = rteUseAlpha ? texel.a : 1.0;
	float cover = rteGlow ? max(max(texel.r, texel.g), texel.b) * shape * max(max(vertexColor.r, vertexColor.g), vertexColor.b) : shape * vertexColor.a;
	FragColor = vec4(vec3(clamp(cover, 0.0, 1.0)), 1.0);
}
