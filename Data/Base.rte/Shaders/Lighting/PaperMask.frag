#version 330 core
// Where a screen style's paper grain may go when it's on the backdrop only (LightingSettings::RetroLayers::PaperBackgroundOnly): 1 behind the
// terrain (background walls, distant layers, the sky), 0 on terrain, objects and effects. Its own pass so Tonemap.frag needn't sample the depth
// buffer of the screen it draws into.

in vec2 textureUV;
out vec4 FragColor;

uniform sampler2D rteSceneDepth;
uniform float rteForegroundDepth; // Depth below which pixels are foreground terrain and objects.

void main() {
	FragColor = vec4(step(rteForegroundDepth, texture(rteSceneDepth, textureUV).r), 0.0, 0.0, 1.0);
}
