#version 330 core
// Where the posterize and the dither go when they're limited to some things (LightingSettings::RetroLayers::PosterizeSky and the rest): 1 where they
// do, 0 where they don't, in between under see-through smoke and mist. The sky, the back walls and everything else come from the depth buffer, smoke
// sprites from their mark in the surface buffer (LightingSettings::c_PosterizeSmokeMark), and mist and dust hanging in the air from the fog volume,
// as LightComposite.frag draws it. The soft smoke and spray puffs are blended in after, in their soft shape (GhostMask.frag). Its own pass so
// Tonemap.frag needn't sample the depth buffer of the screen it draws into.

in vec2 textureUV;
out vec4 FragColor;

uniform sampler2D rteSceneDepth;
uniform sampler2D rteSurface;
uniform bool rteHasSurface;
uniform float rteForegroundDepth; // Depth below which pixels are foreground terrain and objects.
uniform float rteBackgroundDepth; // Depth beyond which pixels are the distant backdrop layers, or the sky where nothing was drawn.
uniform bool rteSky;
uniform bool rteSmoke;
uniform bool rteBackWalls;
uniform bool rteRest;
uniform sampler2D rteFog; // World grid, R = how thick mist or dust hangs in the air there (FogUpdate.frag).
uniform float rteFogStrength; // As LightComposite.frag's, 0 for no fog.
uniform float rteFogOpacity;
uniform vec2 rteScreenOrigin; // World position of the screen's first pixel, as LightComposite.frag's.
uniform vec2 rteGridWorldSize;

void main() {
	float depth = texture(rteSceneDepth, textureUV).r;
	float mark = rteHasSurface ? floor(texelFetch(rteSurface, ivec2(gl_FragCoord.xy), 0).b * 255.0 + 0.5) : 0.0;
	bool on;
	if (mark == 88.0 || mark == 92.0) {
		on = rteSmoke;
	} else if (depth > rteBackgroundDepth) {
		on = rteSky;
	} else if (depth >= rteForegroundDepth) {
		on = rteBackWalls;
	} else {
		on = rteRest;
	}
	float cover = on ? 1.0 : 0.0;
	if (rteFogStrength > 0.0) {
		vec2 fogWorld = rteScreenOrigin + gl_FragCoord.xy;
		float fog = texture(rteFog, fogWorld / rteGridWorldSize).r;
		if (fog > 0.002) {
			float amount = (1.0 - exp(-fog * rteFogStrength * 2.0)) * rteFogOpacity;
			cover = rteSmoke ? cover + (1.0 - cover) * amount : cover * (1.0 - amount);
		}
	}
	FragColor = vec4(cover, 0.0, 0.0, 1.0);
}
