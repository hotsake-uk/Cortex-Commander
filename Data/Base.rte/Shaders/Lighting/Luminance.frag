// Luminance.frag
// First step of auto exposure: writes the log luminance of the HDR scene into a small power of two target, whose mipmaps then average it.
// The centre of the screen counts more, since that's where the action (and the player's eyes) usually are.
#version 330 core

in vec2 textureUV;
out vec4 FragColor;

uniform sampler2D rteScene;
uniform vec2 rteSourceTexelSize;

void main() {
	vec2 uv = textureUV;
	// Four bilinear taps cover a decent part of the source footprint of each texel.
	vec3 color = texture(rteScene, uv + rteSourceTexelSize * vec2(-1.0, -1.0)).rgb;
	color += texture(rteScene, uv + rteSourceTexelSize * vec2(1.0, -1.0)).rgb;
	color += texture(rteScene, uv + rteSourceTexelSize * vec2(-1.0, 1.0)).rgb;
	color += texture(rteScene, uv + rteSourceTexelSize * vec2(1.0, 1.0)).rgb;
	float luminance = dot(color * 0.25, vec3(0.2126, 0.7152, 0.0722));
	if (isnan(luminance) || isinf(luminance)) {
		luminance = 0.0;
	}
	// Centre weighting through the average: edges are pulled towards the centre's value by blending the log luminance with a fixed mid grey.
	float edge = smoothstep(0.25, 0.75, length((uv - 0.5) * vec2(1.6, 1.0)));
	float logLuminance = log(max(luminance, 0.0001));
	FragColor = vec4(mix(logLuminance, log(0.18), edge * 0.6), 0.0, 0.0, 1.0);
}
