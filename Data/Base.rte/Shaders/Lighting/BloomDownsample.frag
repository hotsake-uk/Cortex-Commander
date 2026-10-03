// BloomDownsample.frag
// 13 tap downsample (Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare"), with a soft threshold on the first pass.
#version 330 core

in vec2 textureUV;
out vec4 FragColor;

uniform sampler2D rteSource;
uniform vec2 rteSourceTexelSize;
uniform bool rteFirstPass;
uniform float rteThreshold;
uniform float rteKnee;

vec3 Prefilter(vec3 color) {
	float brightness = max(color.r, max(color.g, color.b));
	float soft = clamp(brightness - rteThreshold + rteKnee, 0.0, 2.0 * rteKnee);
	soft = (soft * soft) / (4.0 * rteKnee + 1e-4);
	float contribution = max(soft, brightness - rteThreshold) / max(brightness, 1e-4);
	return color * contribution;
}

void main() {
	vec2 uv = gl_FragCoord.xy * 2.0 * rteSourceTexelSize;
	vec2 t = rteSourceTexelSize;
	vec3 a = texture(rteSource, uv + t * vec2(-2.0, -2.0)).rgb;
	vec3 b = texture(rteSource, uv + t * vec2(0.0, -2.0)).rgb;
	vec3 c = texture(rteSource, uv + t * vec2(2.0, -2.0)).rgb;
	vec3 d = texture(rteSource, uv + t * vec2(-2.0, 0.0)).rgb;
	vec3 e = texture(rteSource, uv).rgb;
	vec3 f = texture(rteSource, uv + t * vec2(2.0, 0.0)).rgb;
	vec3 g = texture(rteSource, uv + t * vec2(-2.0, 2.0)).rgb;
	vec3 h = texture(rteSource, uv + t * vec2(0.0, 2.0)).rgb;
	vec3 i = texture(rteSource, uv + t * vec2(2.0, 2.0)).rgb;
	vec3 j = texture(rteSource, uv + t * vec2(-1.0, -1.0)).rgb;
	vec3 k = texture(rteSource, uv + t * vec2(1.0, -1.0)).rgb;
	vec3 l = texture(rteSource, uv + t * vec2(-1.0, 1.0)).rgb;
	vec3 m = texture(rteSource, uv + t * vec2(1.0, 1.0)).rgb;

	vec3 color = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
	if (rteFirstPass) {
		color = Prefilter(color);
	}
	FragColor = vec4(max(color, vec3(0.0)), 1.0);
}
