// Afterglow.frag
// A screen style's afterglow (LightingSettings::RetroLayers::Afterglow), like an old monochrome monitor's phosphor: bright moving things leave a fading
// trail. Mode 0 writes the history: the bright parts of the scene now, or what was there before, faded, whichever is brighter, sampled where the world
// was so the trails stay put as the camera moves. Mode 1 lays the history over the scene (blended with GL_MAX) in the look's colour.
#version 330 core

in vec2 textureUV;
out vec4 FragColor;

uniform sampler2D rteScene;
uniform sampler2D rteHistory;
uniform int rteMode;
uniform vec2 rteScreenSize;
uniform vec2 rteShift; // Where this pixel's world point was in the last history, as an offset in texture coordinates.
uniform bool rteFresh; // No history yet.
uniform float rteKeep; // How much of the history is kept this frame.
uniform int rteLook; // 1 Synthwave (hot pink), 2 Hackerman (phosphor green).
uniform float rteStrength;

void main() {
	if (rteMode == 0) {
		vec3 scene = texture(rteScene, textureUV).rgb;
		float bright = max(dot(scene, vec3(0.2126, 0.7152, 0.0722)) - 0.35, 0.0);
		float kept = 0.0;
		vec2 before = textureUV + rteShift;
		if (!rteFresh && all(greaterThanEqual(before, vec2(0.0))) && all(lessThanEqual(before, vec2(1.0)))) {
			kept = texture(rteHistory, before).r * rteKeep;
		}
		FragColor = vec4(vec3(max(bright, kept)), 1.0);
	} else {
		float trail = texture(rteHistory, textureUV).r;
		vec3 tint = rteLook == 2 ? vec3(0.2, 1.0, 0.35) : vec3(1.0, 0.3, 0.8);
		FragColor = vec4(tint * trail * rteStrength * 1.2, 0.0);
	}
}
