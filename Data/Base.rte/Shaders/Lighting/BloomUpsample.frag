// BloomUpsample.frag
// 3x3 tent filter upsample of the next smaller bloom mip, additively blended onto the current one.
#version 330 core

in vec2 textureUV;
out vec4 FragColor;

uniform sampler2D rteSource;
uniform vec2 rteSourceTexelSize;
uniform vec2 rteTargetTexelSize;
uniform float rteRadius;

void main() {
	vec2 uv = gl_FragCoord.xy * rteTargetTexelSize;
	vec2 t = rteSourceTexelSize * rteRadius;
	vec3 color = texture(rteSource, uv + vec2(-t.x, -t.y)).rgb;
	color += texture(rteSource, uv + vec2(0.0, -t.y)).rgb * 2.0;
	color += texture(rteSource, uv + vec2(t.x, -t.y)).rgb;
	color += texture(rteSource, uv + vec2(-t.x, 0.0)).rgb * 2.0;
	color += texture(rteSource, uv).rgb * 4.0;
	color += texture(rteSource, uv + vec2(t.x, 0.0)).rgb * 2.0;
	color += texture(rteSource, uv + vec2(-t.x, t.y)).rgb;
	color += texture(rteSource, uv + vec2(0.0, t.y)).rgb * 2.0;
	color += texture(rteSource, uv + vec2(t.x, t.y)).rgb;
	FragColor = vec4(color / 16.0, 1.0);
}
