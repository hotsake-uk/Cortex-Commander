// RCScene.frag
// Radiance cascades input, at half resolution: RGB = light a ray picks up when it hits this pixel, A = whether the pixel stops rays.
// Glows are light sources. Foreground terrain and objects block light, and re-emit some of the light that reached them last frame (one bounce per frame, so it builds up to several).
#version 330 core

out vec4 FragColor;

uniform sampler2D rteEmissive; // Full resolution glows, gamma space.
uniform sampler2D rteSceneDepth; // Full resolution depth.
uniform sampler2D rtePreviousLit; // Half resolution lit scene of the previous frame, linear HDR.
uniform vec2 rtePreviousOffset; // How far the screen moved since then, in full resolution pixels.
uniform vec2 rteTargetSize;
uniform vec2 rteScreenSize;
uniform float rteForegroundDepth;
uniform float rteEmissiveIntensity;
uniform float rteBounce;
uniform bool rteHavePrevious;

void main() {
	vec2 uv = gl_FragCoord.xy / rteTargetSize;
	vec3 emission = pow(texture(rteEmissive, uv).rgb, vec3(2.2)) * rteEmissiveIntensity;
	float depth = texture(rteSceneDepth, uv).r;
	float opaque = depth < rteForegroundDepth ? 1.0 : 0.0;
	if (opaque > 0.0 && rteHavePrevious) {
		vec2 previousUV = (gl_FragCoord.xy * (rteScreenSize / rteTargetSize) + rtePreviousOffset) / rteScreenSize;
		if (all(greaterThanEqual(previousUV, vec2(0.0))) && all(lessThanEqual(previousUV, vec2(1.0)))) {
			emission += min(texture(rtePreviousLit, previousUV).rgb, vec3(4.0)) * rteBounce;
		}
	}
	// Glows (fire, explosions, lamps) don't block rays: rays passing through them pick up their light (see RCCascade.frag), so small burning particles don't cast shadows.
	FragColor = vec4(emission, opaque);
}
