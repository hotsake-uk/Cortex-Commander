// Emissive.frag
// Draws glow sprites (ScreenEffects) additively into the HDR scene as emitted light, so they aren't darkened by scene lighting and feed the bloom.
#version 330 core

in vec2 textureUV;
in vec4 vertexColor;
out vec4 FragColor;

uniform sampler2D rteTexture;
uniform float rteEmissiveIntensity;

void main() {
	vec3 glow = texture(rteTexture, textureUV).rgb;
	// Glow art is authored for screen blending in gamma space; convert so it adds the same amount of brightness in linear space.
	FragColor = vec4(pow(glow, vec3(2.2)) * vertexColor.rgb * rteEmissiveIntensity, 1.0);
}
