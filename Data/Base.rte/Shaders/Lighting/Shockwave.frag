// Shockwave.frag
// An expanding refraction ring, written as a screen space displacement (in pixels) that the final pass samples the scene through.
// Uses PointLight.vert: localPos is -1..1 across the quad (the ring's maximum radius), lightColor.r is the amplitude in pixels, lightColor.g the ring's progress 0..1.
#version 330 core

in vec2 localPos;
in vec4 lightColor;
in vec2 lightCenter;
in float lightRadius;
in vec2 screenPos;

out vec4 FragColor;

void main() {
	float distance = length(localPos);
	float progress = lightColor.g;
	if (distance >= 1.0 || distance < 1e-4) {
		discard;
	}
	// A thin front that thickens and fades as it expands.
	float width = mix(0.06, 0.18, progress);
	float profile = exp(-pow((distance - progress) / width, 2.0));
	float amplitude = lightColor.r * (1.0 - progress) * (1.0 - progress);
	vec2 displacement = (localPos / distance) * amplitude * profile;
	FragColor = vec4(displacement, 0.0, 1.0);
}
