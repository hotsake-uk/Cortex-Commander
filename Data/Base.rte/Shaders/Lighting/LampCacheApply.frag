// LampCacheApply.frag
// Adds the steady scenery lamps from the world lamp cache (LightingSettings::LampCache) to the dynamic light buffer, shaded like PointLight.frag shades a
// light: edges facing the lamps catch more, and shiny surfaces throw back highlights. The direction is the brightness-weighted average of the lamps'.
#version 330 core

in vec2 textureUV;

out vec4 FragColor;

uniform sampler2D rteLampCache; // World, RGB = light arriving from the steady lamps.
uniform sampler2D rteLampDirection; // World, RG = the screen direction towards them (xy of a unit vector) times their brightness.
uniform sampler2D rteNormals; // Player screen normals: RG = normal xy * 0.5 + 0.5, B = 1 - shininess, A > 0.25 where something was drawn.
uniform sampler2D rteSurface; // Player screen surface values, R = how metallic, B = 1 where a solid object was drawn.
uniform vec2 rteScreenOrigin; // World position of the screen's top left pixel.
uniform vec2 rteScreenSize;
uniform vec2 rteCacheWorldSize; // World size covered by the cache.
uniform float rteEdgeLighting;
uniform float rteSpecular;

void main() {
	vec2 cacheUV = (rteScreenOrigin + gl_FragCoord.xy) / rteCacheWorldSize;
	vec3 light = texture(rteLampCache, cacheUV).rgb;
	float brightness = dot(light, vec3(0.2126, 0.7152, 0.0722));
	if (brightness <= 0.0002) {
		discard;
	}
	vec2 lean = texture(rteLampDirection, cacheUV).rg / brightness;
	float leanLength = length(lean);
	if (leanLength > 0.995) {
		lean *= 0.995 / leanLength;
	}
	vec3 toLight = vec3(lean, sqrt(max(1.0 - dot(lean, lean), 0.0)));

	float shading = 1.0;
	float highlight = 0.0;
	vec4 normalSample = texture(rteNormals, gl_FragCoord.xy / rteScreenSize);
	if (normalSample.a > 0.25) {
		vec2 normalXY = normalSample.xy * 2.0 - 1.0;
		vec3 normal = vec3(normalXY, sqrt(max(1.0 - dot(normalXY, normalXY), 0.0)));
		shading = mix(1.0, clamp(dot(normal, toLight) / max(toLight.z, 0.05), 0.0, 2.5), rteEdgeLighting);
		// Units and other solid objects: edges facing the light don't catch more than a flat surface would, so a light right by one (its own headlamp)
		// doesn't wash its sprite out. Edges facing away still darken.
		bool solidObject = texture(rteSurface, gl_FragCoord.xy / rteScreenSize).b > 0.5;
		if (solidObject) {
			shading = min(shading, 1.0);
		}
		float shine = 1.0 - normalSample.b;
		if (shine > 0.02 && rteSpecular > 0.0 && !solidObject) {
			float metalness = texture(rteSurface, gl_FragCoord.xy / rteScreenSize).r;
			vec3 halfway = normalize(toLight + vec3(0.0, 0.0, 1.0));
			highlight = pow(max(dot(normal, halfway), 0.0), mix(18.0, 64.0, shine)) * shine * rteSpecular * mix(1.6, 3.2, metalness);
		}
	}
	FragColor = vec4(light * shading, highlight * brightness);
}
