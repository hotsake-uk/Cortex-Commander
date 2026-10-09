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
uniform float rteMistBright; // How bright spray off water is drawn.
uniform float rteMistGlow; // The least light spray off water is drawn with, so it shows in the dark.

void main() {
	float alpha = texture(rteTexture, textureUV).a * vertexColor.a;
	if (alpha <= 0.003) {
		discard;
	}
	vec2 worldPos = rteScreenOrigin + gl_FragCoord.xy;
	float sky = smoothstep(0.0, 1.0, texture(rteSkyLight, worldPos / rteGridWorldSize).r);
	// The lights soften towards 2 as they pile up, the same as on surfaces (LightComposite.frag's rteMaxDynamicLight): taken raw, puffs next to a fire
	// or a few lamps went near white while the ground under them didn't.
	vec3 lamps = 2.0 * (1.0 - exp(-texture(rteDynamicLight, gl_FragCoord.xy / rteScreenSize).rgb / 2.0));
	vec3 light = mix(rteAmbient, rteSkyColor, sky) + lamps;
	vec3 albedo = vertexColor.rgb;
	if (albedo.b > 1.0) {
		// Spray off water (its colour is sent with 1 added as the sign): pale stuff that catches whatever light there is, so it's never drawn darker than this.
		albedo -= vec3(1.0);
		light = max(light, vec3(0.84, 1.0, 1.2) * rteMistGlow) * rteMistBright;
	}
	FragColor = vec4(pow(albedo, vec3(2.2)) * light, alpha);
}
