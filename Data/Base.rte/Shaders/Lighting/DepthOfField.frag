// DepthOfField.frag
// Depth of field and tilt-shift (LightingSettings::DepthOfField, TiltShift): each pixel is blurred over a disc whose radius grows with how far its depth
// is from the focus, and with how far it is from the sharp band across the screen. Samples only count where their own blur reaches back to this pixel,
// so sharp things don't smear over the blur behind them and blurred things still spill softly over sharp edges a little.
#version 330 core

in vec2 textureUV;

out vec4 FragColor;

uniform sampler2D rteScene; // A copy of the lit scene.
uniform sampler2D rteSceneDepth;
uniform vec2 rteScreenSize;
uniform float rteFocusDepth; // The depth in focus.
uniform float rteDepthRange; // How far from it, in depth, the blur is widest.
uniform float rteDepthBlur; // The widest blur from depth, in pixels; 0 for none.
uniform float rteTiltLine; // Where the sharp band is, 0 the top of the screen to 1 the bottom.
uniform float rteTiltBlur; // The widest blur from the band, in pixels; 0 for none.

const int c_Taps = 24;

float BlurRadius(vec2 pixel) {
	float radius = 0.0;
	if (rteDepthBlur > 0.0) {
		float depth = texture(rteSceneDepth, pixel / rteScreenSize).r;
		radius = clamp(abs(depth - rteFocusDepth) / rteDepthRange, 0.0, 1.0) * rteDepthBlur;
	}
	if (rteTiltBlur > 0.0) {
		float away = abs(pixel.y / rteScreenSize.y - rteTiltLine);
		radius = max(radius, smoothstep(0.08, 0.45, away) * rteTiltBlur);
	}
	return radius;
}

void main() {
	vec2 pixel = gl_FragCoord.xy;
	vec4 centre = texture(rteScene, pixel / rteScreenSize);
	float radius = BlurRadius(pixel);
	if (radius < 0.5) {
		FragColor = centre;
		return;
	}
	vec3 sum = centre.rgb;
	float weight = 1.0;
	// A golden angle spiral fills the disc evenly with a few taps.
	for (int i = 0; i < c_Taps; ++i) {
		float along = sqrt((float(i) + 0.5) / float(c_Taps));
		float angle = float(i) * 2.39996;
		vec2 offset = vec2(cos(angle), sin(angle)) * along * radius;
		vec2 samplePixel = pixel + offset;
		float sampleRadius = BlurRadius(samplePixel);
		float reaches = clamp(sampleRadius - length(offset) + 1.0, 0.0, 1.0);
		sum += texture(rteScene, samplePixel / rteScreenSize).rgb * reaches;
		weight += reaches;
	}
	FragColor = vec4(sum / weight, centre.a);
}
