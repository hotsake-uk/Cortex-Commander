// RCCascade.frag
// One cascade of 2D radiance cascades. Cascade i has a probe every 2^(i+1) pixels, each with 4^(i+1) directions stored in its 2^(i+1) square block of texels,
// and traces the interval of distances [L(4^i - 1)/3, L(4^(i+1) - 1)/3]. Each ray's radiance is merged with the next cascade's rays continuing in the same
// directions, interpolated between its four nearest probes. Merging from the top cascade down gives every pixel light from the whole screen with soft occlusion.
#version 330 core

out vec4 FragColor;

uniform sampler2D rteScene; // RCScene output: RGB light, A opaque.
uniform sampler2D rteUpper; // The next cascade, already merged.
uniform int rteCascade;
uniform int rteCascadeCount;
uniform vec2 rteTargetSize; // Size of the cascade textures and the scene, in pixels.
uniform float rteBaseInterval;

const float c_TwoPi = 6.28318530718;

vec4 UpperRay(ivec2 probe, int direction, int upperBlock, ivec2 upperGrid) {
	probe = clamp(probe, ivec2(0), upperGrid - 1);
	ivec2 texel = probe * upperBlock + ivec2(direction % upperBlock, direction / upperBlock);
	return texelFetch(rteUpper, texel, 0);
}

void main() {
	ivec2 texel = ivec2(gl_FragCoord.xy);
	int block = 1 << (rteCascade + 1);
	int directionCount = block * block;
	ivec2 probe = texel / block;
	ivec2 local = texel - probe * block;
	int direction = local.y * block + local.x;
	vec2 probeCenter = (vec2(probe) + 0.5) * float(block);

	float angle = (float(direction) + 0.5) * c_TwoPi / float(directionCount);
	vec2 rayDirection = vec2(cos(angle), sin(angle));
	float scaleStart = (pow(4.0, float(rteCascade)) - 1.0) / 3.0;
	float scaleEnd = (pow(4.0, float(rteCascade + 1)) - 1.0) / 3.0;
	float start = rteBaseInterval * scaleStart;
	float end = rteBaseInterval * scaleEnd;

	// March the interval. Rays only need to find the first surface; terrain is thick, so coarse steps on long intervals are fine.
	float length = end - start;
	int steps = int(clamp(ceil(length), 2.0, 16.0));
	float stepLength = length / float(steps);
	vec3 radiance = vec3(0.0);
	float transmittance = 1.0;
	for (int i = 0; i < steps; ++i) {
		vec2 position = probeCenter + rayDirection * (start + (float(i) + 0.5) * stepLength);
		if (any(lessThan(position, vec2(0.0))) || any(greaterThanEqual(position, rteTargetSize))) {
			// Off screen: nothing more to find this way.
			transmittance = 0.0;
			break;
		}
		vec4 scene = texelFetch(rteScene, ivec2(position), 0);
		if (scene.a > 0.5) {
			radiance += scene.rgb * transmittance;
			transmittance = 0.0;
			break;
		}
		// Glowing air (fire, lamps) adds light along the ray, in proportion to how much of it the ray crosses.
		radiance += scene.rgb * transmittance * min(stepLength, 3.0) * 0.35;
	}

	if (transmittance > 0.0 && rteCascade < rteCascadeCount - 1) {
		int upperBlock = block * 2;
		ivec2 upperGrid = ivec2(rteTargetSize) / upperBlock;
		vec2 upperPosition = probeCenter / float(upperBlock) - 0.5;
		ivec2 base = ivec2(floor(upperPosition));
		vec2 weight = upperPosition - vec2(base);
		vec3 merged = vec3(0.0);
		for (int corner = 0; corner < 4; ++corner) {
			ivec2 offset = ivec2(corner & 1, corner >> 1);
			vec3 sum = vec3(0.0);
			for (int child = 0; child < 4; ++child) {
				sum += UpperRay(base + offset, direction * 4 + child, upperBlock, upperGrid).rgb;
			}
			float cornerWeight = (offset.x == 1 ? weight.x : 1.0 - weight.x) * (offset.y == 1 ? weight.y : 1.0 - weight.y);
			merged += sum * 0.25 * cornerWeight;
		}
		radiance += merged * transmittance;
	}
	FragColor = vec4(radiance, 1.0);
}
