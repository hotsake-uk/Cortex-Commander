// Emissive.frag
// Glow sprites (ScreenEffects) are screen blended into an emissive buffer in gamma space, exactly like the original glows, so many overlapping glows saturate gracefully instead of adding up to a white blob.
// The composite then adds the buffer to the HDR scene as emitted light.
#version 330 core

in vec2 textureUV;
in vec4 vertexColor;
out vec4 FragColor;

uniform sampler2D rteTexture;

void main() {
	FragColor = vec4(texture(rteTexture, textureUV).rgb * vertexColor.rgb, 1.0);
}
