// FogOfWar.frag
// Fog of war over the lit scene: what the lighting adds after the scene is drawn (lamp and headlamp light, glows, beams, lit dust, weather,
// god rays) is darkened under the unseen layer as the scene under it was, so nothing shows through. The coverage is worked out as
// UnseenSoft.frag does it, from the part of the unseen layer around the screen (SceneLighting::UploadFogOfWar).
#version 330 core

out vec4 FragColor;

uniform sampler2D rteFog; // R = 1 where unseen, one texel per unseen layer pixel, from rteFogTexelOrigin.
uniform vec2 rteFogTexelOrigin; // The unseen layer pixel the texture starts at.
uniform vec2 rteFogScale; // Scene pixels per unseen layer pixel.
uniform vec2 rteScreenOrigin; // World position of the screen's top left pixel.

float Coverage(ivec2 texel, ivec2 size) {
	texel = clamp(texel, ivec2(0), size - 1);
	return texelFetch(rteFog, texel, 0).r > 0.5 ? 1.0 : 0.0;
}

void main() {
	ivec2 size = textureSize(rteFog, 0);
	vec2 worldPos = rteScreenOrigin + gl_FragCoord.xy;
	vec2 position = worldPos / rteFogScale - 0.5 - rteFogTexelOrigin;
	ivec2 base = ivec2(floor(position));
	vec2 fraction = fract(position);
	float coverage = mix(mix(Coverage(base, size), Coverage(base + ivec2(1, 0), size), fraction.x), mix(Coverage(base + ivec2(0, 1), size), Coverage(base + ivec2(1, 1), size), fraction.x), fraction.y);
	coverage = smoothstep(0.0, 1.0, coverage);
	if (coverage <= 0.01) {
		discard;
	}
	FragColor = vec4(0.0, 0.0, 0.0, coverage);
}
