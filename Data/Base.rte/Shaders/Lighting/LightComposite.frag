// LightComposite.frag
// Lights the scene: albedo (the unlit scene as drawn) times the light reaching each pixel, written to an HDR target in linear space.
// At full daylight the light is exactly the sky color, so with a white sky the original art is reproduced unchanged.
#version 330 core

in vec2 textureUV;
out vec4 FragColor;

uniform sampler2D rteAlbedo; // The unlit player screen.
uniform sampler2D rteDynamicLight; // Screen space, RGB = linear light from dynamic lights.
uniform sampler2D rteEmissive; // Screen space, RGB = glows in gamma space, screen blended.
uniform float rteEmissiveIntensity;
uniform float rteMaxDynamicLight; // Dynamic light softly saturates towards this, so piles of overlapping lights don't blow out.
uniform sampler2D rteSkyLight; // World grid, R = sky light 0..1, linearly filtered.
uniform sampler2D rteSceneDepth; // The player screen's depth buffer.
uniform float rteBackgroundDepth; // Depth beyond which pixels belong to the distant background layers (or nothing was drawn).
uniform vec3 rteBackgroundLight; // Linear light on the distant background layers.
uniform vec2 rteScreenSize;
uniform vec2 rteScreenOrigin; // World position of the screen's top left pixel.
uniform vec2 rteGridWorldSize; // World size covered by the sky light grid.
uniform vec3 rteAmbient; // Linear light where no sky light reaches.
uniform vec3 rteSkyColor; // Linear light under open sky.
uniform int rteDebugView; // 0 final, 1 lighting on grey, 2 sky light only, 3 dynamic light only, 4 normals.
uniform sampler2D rteNormals; // Player screen normals, RGB = normal * 0.5 + 0.5, A = 1 where something was drawn.
uniform float rteEdgeLighting;

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
		// Sky light comes from above: upward facing edges catch more of it, undersides less.
		vec4 normalSample = texture(rteNormals, screenUV);
		if (normalSample.a > 0.5) {
			vec3 normal = normalSample.xyz * 2.0 - 1.0;
			sky *= mix(1.0, clamp(1.0 - normal.y * 0.9, 0.35, 1.6), rteEdgeLighting);
		}
		vec3 dynamicLight = texture(rteDynamicLight, screenUV).rgb;
		dynamicLight = rteMaxDynamicLight * (1.0 - exp(-dynamicLight / rteMaxDynamicLight));
		light = mix(rteAmbient, rteSkyColor, sky) + dynamicLight;
	}

	vec3 albedoLinear = pow(albedo.rgb, vec3(2.2));
	if (rteDebugView == 1) {
		albedoLinear = vec3(0.5);
	} else if (rteDebugView == 2) {
		vec2 worldPos = rteScreenOrigin + gl_FragCoord.xy;
		FragColor = vec4(vec3(texture(rteSkyLight, worldPos / rteGridWorldSize).r), 1.0);
		return;
	} else if (rteDebugView == 3) {
		FragColor = vec4(texture(rteDynamicLight, screenUV).rgb, 1.0);
		return;
	} else if (rteDebugView == 4) {
		FragColor = vec4(pow(texture(rteNormals, screenUV).rgb, vec3(2.2)), 1.0);
		return;
	}
	vec3 emissive = pow(texture(rteEmissive, screenUV).rgb, vec3(2.2)) * rteEmissiveIntensity;
	FragColor = vec4(albedoLinear * light + emissive, 1.0);
}
