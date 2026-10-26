// Afterglow.frag
// A screen style's afterglow (LightingSettings::RetroLayers::Afterglow), like an old monochrome monitor's phosphor: bright moving things leave a fading
// trail. Mode 0 writes the history: the bright parts of the scene now, or what was there before, faded, whichever is brighter, sampled where the world
// was so the trails stay put as the camera moves. Mode 1 lays the history over the scene (blended with GL_MAX) in the look's colour.
// The Game Boy LCD's ghosting (RetroLayers::LCDGhosting) uses it too: mode 2 writes the picture its slow pixels show, the last one drifting towards
// the scene now, in screen space; mode 3 copies that over the scene. With only the effects picked (LightingSettings::LCDGhostSelected), mode 4 adds
// the sprites marked for it in the surface buffer to its mask (GhostMask.frag), mode 5 writes the history of just those, premultiplied, faded and
// sampled where the world was, and mode 3 lays that over the scene (blended as premultiplied alpha).
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
uniform int rteLook; // 1 Synthwave (hot pink), 2 Hackerman (phosphor green), 3 Comic and 4 Game Boy take Synthwave's.
uniform float rteStrength;
uniform sampler2D rteSurface; // The player screen's surface buffer, mode 4.
uniform sampler2D rteMask; // The LCD ghosting's mask, mode 5.

void main() {
	if (rteMode == 2) {
		vec4 scene = texture(rteScene, textureUV);
		vec3 shown = rteFresh ? scene.rgb : mix(scene.rgb, texture(rteHistory, textureUV).rgb, rteKeep);
		FragColor = vec4(shown, scene.a);
	} else if (rteMode == 3) {
		FragColor = texture(rteHistory, textureUV);
	} else if (rteMode == 4) {
		// LightingSettings::c_GhostSurfaceMark, or c_GhostPosterizeSmokeMark on a smoke sprite while the posterize is limited too.
		float mark = floor(texelFetch(rteSurface, ivec2(gl_FragCoord.xy), 0).b * 255.0 + 0.5);
		FragColor = vec4(vec3(mark == 96.0 || mark == 92.0 ? 1.0 : 0.0), 1.0);
	} else if (rteMode == 5) {
		float cover = texture(rteMask, textureUV).r;
		vec4 kept = vec4(0.0);
		vec2 before = textureUV + rteShift;
		if (!rteFresh && all(greaterThanEqual(before, vec2(0.0))) && all(lessThanEqual(before, vec2(1.0)))) {
			kept = texture(rteHistory, before) * rteKeep;
		}
		// What smears now, over what it left before.
		FragColor = vec4(texture(rteScene, textureUV).rgb * cover, cover) + kept * (1.0 - cover);
	} else if (rteMode == 0) {
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
