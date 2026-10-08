// DecalFade.frag
// Fades the world scorch and stain maps: draws rteAmount over the whole map, subtracted from what it holds (see SceneLighting::FadeDecals).
#version 330 core

in vec2 textureUV;

out vec4 FragColor;

uniform vec4 rteAmount;

void main() {
	FragColor = rteAmount;
}
