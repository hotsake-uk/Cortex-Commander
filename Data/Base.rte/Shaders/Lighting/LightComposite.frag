// LightComposite.frag
// Lights the scene: albedo (the unlit scene as drawn) times the light reaching each pixel, written to an HDR target in linear space.
// At full daylight the light is exactly the sky color, so with a white sky the original art is reproduced unchanged.
#version 330 core

in vec2 textureUV;
out vec4 FragColor;

uniform sampler2D rteAlbedo; // The unlit player screen.
uniform sampler2D rteDynamicLight; // Screen space, RGB = linear light from dynamic lights.
uniform sampler2D rteSkyLight; // World grid, R = sky light 0..1, linearly filtered.
uniform sampler2D rteSceneDepth; // The player screen's depth buffer.
uniform float rteBackgroundDepth; // Depth beyond which pixels belong to the distant background layers (or nothing was drawn).
uniform vec3 rteBackgroundLight; // Linear light on the distant background layers.
uniform vec2 rteScreenSize;
uniform vec2 rteScreenOrigin; // World position of the screen's top left pixel.
uniform vec2 rteGridWorldSize; // World size covered by the sky light grid.
uniform vec3 rteAmbient; // Linear light where no sky light reaches.
uniform vec3 rteSkyColor; // Linear light under open sky.

void main() {
	vec2 screenUV = gl_FragCoord.xy / rteScreenSize;
	vec4 albedo = texture(rteAlbedo, screenUV);

	vec3 light;
	if (texture(rteSceneDepth, screenUV).r > rteBackgroundDepth) {
		// Background layers are far behind the action: they aren't shadowed by terrain or lit by explosions in front of them.
		light = rteBackgroundLight;
	} else {
		vec2 worldPos = rteScreenOrigin + gl_FragCoord.xy;
		float sky = texture(rteSkyLight, worldPos / rteGridWorldSize).r;
		// Shape the falloff a little so cave mouths stay bright and deep caves get properly dark.
		sky = smoothstep(0.0, 1.0, sky);
		light = mix(rteAmbient, rteSkyColor, sky) + texture(rteDynamicLight, screenUV).rgb;
	}

	vec3 albedoLinear = pow(albedo.rgb, vec3(2.2));
	FragColor = vec4(albedoLinear * light, 1.0);
}
