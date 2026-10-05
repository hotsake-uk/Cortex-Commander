// LitParticle.frag
// Translucent particles (dust, smoke) drawn over the lit HDR scene, lit by the same sky light, ambient and dynamic lights as the scene behind them.
// Drawing them after the composite keeps them out of the depth buffer, so the background behind them keeps its own lighting.
#version 330 core

in vec2 textureUV;
in vec4 vertexColor; // RGB albedo in gamma space, A opacity.
out vec4 FragColor;

uniform sampler2D rteTexture; // Particle shape, alpha.
uniform sampler2D rteSkyLight; // World grid, R = sky light 0..1.
uniform sampler2D rteDynamicLight; // Screen space dynamic light.
uniform vec2 rteGridWorldSize;
uniform vec2 rteScreenOrigin;
uniform vec2 rteScreenSize;
uniform vec3 rteAmbient;
uniform vec3 rteSkyColor;

void main() {
	float alpha = texture(rteTexture, textureUV).a * vertexColor.a;
	if (alpha <= 0.003) {
		discard;
	}
	vec2 worldPos = rteScreenOrigin + gl_FragCoord.xy;
	float sky = smoothstep(0.0, 1.0, texture(rteSkyLight, worldPos / rteGridWorldSize).r);
	vec3 light = mix(rteAmbient, rteSkyColor, sky) + texture(rteDynamicLight, gl_FragCoord.xy / rteScreenSize).rgb;
	vec3 albedo = vertexColor.rgb;
	if (albedo.b > 1.0) {
		// Spray off water (its colour is sent with 1 added as the sign): pale stuff that catches whatever light there is, so it's never drawn darker than this.
		albedo -= vec3(1.0);
		light = max(light, vec3(0.42, 0.5, 0.6));
	}
	FragColor = vec4(pow(albedo, vec3(2.2)) * light, alpha);
}
