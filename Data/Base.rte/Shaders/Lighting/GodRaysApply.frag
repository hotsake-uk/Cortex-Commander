// GodRaysApply.frag
// Adds the god rays to the HDR scene over the air inside caves and bunkers: the terrain background layer, not solid foreground terrain, objects or the open sky.
#version 330 core

in vec2 textureUV;
out vec4 FragColor;

uniform sampler2D rteGodRays;
uniform sampler2D rteSceneDepth;
uniform float rteForegroundDepth; // Depth beyond which pixels are behind the foreground terrain and objects.
uniform float rteBackgroundDepth; // Depth beyond which pixels are distant background layers.
uniform vec2 rteScreenSize;

void main() {
	vec2 uv = gl_FragCoord.xy / rteScreenSize;
	float depth = texture(rteSceneDepth, uv).r;
	float terrainBackground = step(rteForegroundDepth, depth) * (1.0 - step(rteBackgroundDepth, depth));
	FragColor = vec4(texture(rteGodRays, uv).rgb * terrainBackground, 1.0);
}
