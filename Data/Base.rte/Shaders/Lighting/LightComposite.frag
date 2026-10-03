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
uniform float rteBackgroundNearDepth; // Depth of the nearest background layers.
uniform float rteBackgroundFarDepth; // Depth of the furthest background layers.
uniform vec3 rteAtmosphereColor; // Linear haze color, time of day included.
uniform float rteAtmosphereHaze; // How much the furthest layers fade into the haze.
uniform vec2 rteScreenSize;
uniform vec2 rteScreenOrigin; // World position of the screen's top left pixel.
uniform vec2 rteGridWorldSize; // World size covered by the sky light grid.
uniform vec3 rteAmbient; // Linear light where no sky light reaches.
uniform vec3 rteSkyColor; // Linear light under open sky.
uniform int rteDebugView; // 0 final, 1 lighting on grey, 2 sky light only, 3 dynamic light only, 4 normals.
uniform sampler2D rteNormals; // Player screen normals, RGB = normal * 0.5 + 0.5, A = 1 where something was drawn.
uniform float rteEdgeLighting;
uniform float rteForegroundDepth; // Depth below which pixels are foreground terrain and objects.
uniform vec3 rteForegroundAmbient; // Minimum light on the foreground, so the playfield stays readable deep underground.
uniform sampler2D rteIndirect; // Last frame's lit scene, heavily blurred: the light bouncing off nearby surfaces.
uniform float rteIndirectStrength;
uniform vec2 rteIndirectOffset; // How far the screen moved since the indirect light was made, in pixels.
uniform sampler2D rteGI; // Light from radiance cascades, quarter resolution.
uniform float rteGIStrength;
uniform float rteNightSky; // 0 by day, 1 at full night: stars and the moon show on the furthest sky layers.
uniform vec2 rteMoonPosition; // Screen pixels, as gl_FragCoord (y 0 is the top of the player screen).
uniform float rteTime; // Seconds, for twinkling.

float StarHash(vec2 p) {
	p = fract(p * vec2(123.34, 456.21));
	p += dot(p, p + 45.32);
	return fract(p.x * p.y);
}

// Pixel stars with a slow twinkle, and a pixel-art moon with a soft halo. Linear, added on top of the lit sky.
vec3 NightSky(vec2 fragCoord) {
	// The sky barely moves with the camera, like the furthest parallax layers.
	vec2 skyPixel = floor(fragCoord + rteScreenOrigin * vec2(0.03, -0.03));
	float hash = StarHash(skyPixel);
	vec3 sky = vec3(0.0);
	if (hash > 0.9965) {
		float brightness = (hash - 0.9965) / 0.0035;
		float twinkle = 0.65 + 0.35 * sin(rteTime * (0.8 + brightness * 2.5) + hash * 300.0);
		vec3 tint = mix(vec3(0.75, 0.82, 1.0), vec3(1.0, 0.9, 0.75), fract(hash * 97.0));
		sky += tint * (0.15 + 0.85 * brightness * brightness) * twinkle * 0.9;
	}
	vec2 toMoon = fragCoord - rteMoonPosition;
	float moonDistance = length(toMoon);
	if (moonDistance < 6.5) {
		// A crescent-ish shading so it reads as a moon, not a lamp.
		float shade = 0.75 + 0.25 * clamp(dot(normalize(toMoon + vec2(0.001)), vec2(-0.7, 0.7)), -1.0, 1.0);
		sky += vec3(0.95, 0.95, 1.0) * 1.3 * shade;
	} else {
		sky += vec3(0.55, 0.62, 0.85) * 0.22 * exp(-(moonDistance - 6.5) / 14.0);
	}
	return sky;
}

void main() {
	vec2 screenUV = gl_FragCoord.xy / rteScreenSize;
	vec4 albedo = texture(rteAlbedo, screenUV);

	vec3 light;
	float nightSkyAmount = 0.0;
	float sceneDepth = texture(rteSceneDepth, screenUV).r;
	float haze = 0.0;
	if (sceneDepth > rteBackgroundDepth) {
		// Background layers are far behind the action: they aren't shadowed by terrain or lit by explosions in front of them, but fade into the atmosphere with distance.
		light = rteBackgroundLight;
		float distance = clamp((sceneDepth - rteBackgroundNearDepth) / (rteBackgroundFarDepth - rteBackgroundNearDepth), 0.0, 1.0);
		// Nothing drawn at all (cleared depth) is open sky.
		// The furthest layers are usually the sky itself, which shouldn't be washed out; haze peaks on the distant scenery in between.
		haze = sceneDepth >= 0.9999 ? rteAtmosphereHaze * 0.4 : rteAtmosphereHaze * smoothstep(0.0, 0.6, distance) * (1.0 - 0.6 * smoothstep(0.85, 1.0, distance));
		// Only layers that barely scroll (the sky itself) get stars, so they never show on mountains or nearer scenery.
		// They also fade towards the horizon, where distant mountains usually are.
		nightSkyAmount = rteNightSky * smoothstep(0.88, 0.95, distance) * (1.0 - smoothstep(0.25, 0.48, screenUV.y)); // Player screens are drawn top down: UV y 0 is the top.
	} else {
		vec2 worldPos = rteScreenOrigin + gl_FragCoord.xy;
		float sky = texture(rteSkyLight, worldPos / rteGridWorldSize).r;
		// Shape the falloff a little so cave mouths stay bright and deep caves get properly dark.
		sky = smoothstep(0.0, 1.0, sky);
		// Sky light comes from above: upward facing edges catch more of it, undersides less.
		vec4 normalSample = texture(rteNormals, screenUV);
		if (normalSample.a > 0.25) {
			vec3 normal = normalSample.xyz * 2.0 - 1.0;
			sky *= mix(1.0, clamp(1.0 - normal.y * 0.9, 0.35, 1.6), rteEdgeLighting);
		}
		vec3 dynamicLight = texture(rteDynamicLight, screenUV).rgb;
		dynamicLight = rteMaxDynamicLight * (1.0 - exp(-dynamicLight / rteMaxDynamicLight));
		light = mix(rteAmbient, rteSkyColor, sky) + dynamicLight;
		if (rteIndirectStrength > 0.0) {
			// One bounce: what the surroundings reflect. Reprojected for camera movement; the history is so blurry that's all it needs.
			vec2 historyUV = (gl_FragCoord.xy + rteIndirectOffset) / rteScreenSize;
			vec3 bounce = texture(rteIndirect, clamp(historyUV, vec2(0.0), vec2(1.0))).rgb;
			// Never let a bad value in the history feed back into every following frame.
			bounce = any(isnan(bounce)) || any(isinf(bounce)) ? vec3(0.0) : bounce;
			// Fully sky lit areas already look as authored, so the bounce mostly fills shadowed areas and caves.
			light += min(bounce, vec3(4.0)) * rteIndirectStrength * (1.0 - 0.85 * sky);
		}
		if (rteGIStrength > 0.0) {
			vec3 gi = texture(rteGI, screenUV).rgb;
			light += min(gi, vec3(6.0)) * rteGIStrength;
		}
		if (sceneDepth < rteForegroundDepth) {
			light = max(light, rteForegroundAmbient);
		}
		// Emissive palette colors (gold glints, glowing bits) shine regardless of the light around them.
		float emissive = normalSample.a > 0.25 ? max(normalSample.a - 0.5, 0.0) * 2.0 : 0.0;
		light = max(light, vec3(emissive * 1.6));
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
	} else if (rteDebugView == 6) {
		FragColor = vec4(texture(rteGI, screenUV).rgb * max(rteGIStrength, 1.0), 1.0);
		return;
	} else if (rteDebugView == 4) {
		FragColor = vec4(pow(texture(rteNormals, screenUV).rgb, vec3(2.2)), 1.0);
		return;
	}
	vec3 emissive = pow(texture(rteEmissive, screenUV).rgb, vec3(2.2)) * rteEmissiveIntensity;
	vec3 litColor = mix(albedoLinear * light, rteAtmosphereColor, haze);
	if (nightSkyAmount > 0.0) {
		// Bright parts of the sky art (clouds, glowing horizons) hide the stars.
		float skyBrightness = dot(litColor, vec3(0.2126, 0.7152, 0.0722));
		litColor += NightSky(gl_FragCoord.xy) * nightSkyAmount * (1.0 - smoothstep(0.03, 0.12, skyBrightness));
	}
	vec3 result = litColor + emissive;
	FragColor = vec4(any(isnan(result)) || any(isinf(result)) ? vec3(0.0) : result, 1.0);
}
