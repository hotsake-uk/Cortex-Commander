#!/usr/bin/env python3
"""Writes the Fell Legion status effects' object shaders (Data/FellLegion.rte/Shaders/*.frag) with Tools/Medieval/shaders.py's writer,
from Medieval.rte's Frozen.frag: each is Blit8.frag's sprite shader with one block of its own for the effect's look. All fade by
rteStrength (the player's mod shader strength). Darkness.frag, the fullscreen pass, is written by hand.

Run from the repository root: python3 Tools/FellLegion/shaders.py Data
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "Medieval"))
sys.argv = [sys.argv[0], sys.argv[1] if len(sys.argv) > 1 else "Data"]

import shaders as M

OUT = os.path.join(sys.argv[1], "FellLegion.rte/Shaders")

SHADERS = {
	"Hexed": (
		"A unit under a Hex: its colours drained grey-violet, violet veins crawling over it, and a sickly pulse at its edge.",
		"""	// Hex: drained to grey-violet, crawling veins, a sickly pulsing edge.
	{
		float lum = dot(FragColor.rgb, vec3(0.3, 0.59, 0.11));
		vec2 texel = floor(textureUV * vec2(textureSize(rteTexture, 0)));
		float vein = smoothstep(0.6, 1.0, sin(texel.x * 1.3 + texel.y * 2.1 + rteTime * 2.5 + rteObjectSeed * 40.0) * sin(texel.y * 0.9 - rteTime * 1.7));
		float edge = 1.0 - Coverage(textureUV - uvDy) * Coverage(textureUV + uvDy) * Coverage(textureUV - uvDx) * Coverage(textureUV + uvDx);
		float pulse = 0.6 + 0.4 * sin(rteTime * 5.0 + rteObjectSeed * 20.0);
		vec3 drained = vec3(0.45, 0.4, 0.55) * (0.4 + 1.0 * lum);
		vec3 violet = vec3(0.7, 0.3, 1.0);
		FragColor.rgb = mix(FragColor.rgb, drained, 0.6 * rteStrength);
		FragColor.rgb = mix(FragColor.rgb, violet, (vein * 0.7 + edge * pulse * 0.5) * rteStrength);
		emissive = max(emissive, (vein * 0.5 + edge * pulse * 0.4) * rteStrength);
	}
"""),
	"Cursed": (
		"A unit under a Curse of Weakness: darkened, a purple shadow seeping up from below, drips of shadow running down it.",
		"""	// Curse: darkened, purple shadow from below, drips of shadow running down.
	{
		float lum = dot(FragColor.rgb, vec3(0.3, 0.59, 0.11));
		vec2 texel = floor(textureUV * vec2(textureSize(rteTexture, 0)));
		float fromBelow = 1.0 - Coverage(textureUV + uvDy * 2.0);
		float drip = step(0.96, Hash(vec2(texel.x, floor(texel.y - rteTime * 9.0 + rteObjectSeed * 50.0)) * 0.5)) * step(0.5, Hash(vec2(texel.x, rteObjectSeed)));
		float breathe = 0.8 + 0.2 * sin(rteTime * 2.0 + rteObjectSeed * 10.0);
		vec3 shadow = vec3(0.25, 0.08, 0.35) * (0.3 + 0.9 * lum);
		FragColor.rgb = mix(FragColor.rgb, shadow, (0.45 + 0.35 * fromBelow) * breathe * rteStrength);
		FragColor.rgb = mix(FragColor.rgb, vec3(0.45, 0.1, 0.6), drip * 0.8 * rteStrength);
		emissive = max(emissive, drip * 0.4 * rteStrength);
		shine = mix(shine, 0.1, 0.5 * rteStrength);
	}
"""),
	"Frenzied": (
		"A unit under Unholy Frenzy: a red rim pulsing hard at its edge, the colours flushed with blood, and a heat shimmer racing over it.",
		"""	// Frenzy: a hard red pulse at the edge, blood-flushed colours, a racing shimmer.
	{
		float lum = dot(FragColor.rgb, vec3(0.3, 0.59, 0.11));
		float edge = 1.0 - Coverage(textureUV - uvDy) * Coverage(textureUV + uvDy) * Coverage(textureUV - uvDx) * Coverage(textureUV + uvDx);
		float beat = pow(0.5 + 0.5 * sin(rteTime * 9.0 + rteObjectSeed * 20.0), 3.0);
		float ripple = 0.5 + 0.5 * sin(gl_FragCoord.y * 1.1 - rteTime * 30.0 + rteObjectSeed * 15.0);
		vec3 blood = vec3(1.0, 0.15, 0.1);
		FragColor.rgb = mix(FragColor.rgb, vec3(1.0, 0.5, 0.4) * (0.4 + 1.0 * lum), 0.25 * rteStrength);
		FragColor.rgb += blood * (edge * (0.4 + 0.8 * beat) + ripple * 0.12) * rteStrength;
		emissive = max(emissive, (edge * (0.3 + 0.7 * beat) + ripple * 0.1) * rteStrength);
	}
"""),
	"Burning": (
		"A unit burning with hellfire: orange-green flames licking up it, brightest at the top and the edges, embers winking across it.",
		"""	// Hellfire burning: orange-green flames licking up, embers winking.
	{
		float lum = dot(FragColor.rgb, vec3(0.3, 0.59, 0.11));
		vec2 texel = floor(textureUV * vec2(textureSize(rteTexture, 0)));
		float edge = 1.0 - Coverage(textureUV - uvDy) * Coverage(textureUV + uvDy) * Coverage(textureUV - uvDx) * Coverage(textureUV + uvDx);
		float fromAbove = 1.0 - Coverage(textureUV - uvDy * 2.0);
		float lick = 0.5 + 0.5 * sin(texel.x * 1.7 + rteTime * 14.0 + rteObjectSeed * 25.0) * sin(texel.y * 0.8 - rteTime * 11.0);
		float ember = step(0.97, Hash(texel + vec2(floor(rteTime * 10.0), rteObjectSeed * 60.0)));
		vec3 flame = mix(vec3(1.0, 0.5, 0.1), vec3(0.5, 1.0, 0.3), lick);
		FragColor.rgb = mix(FragColor.rgb, vec3(1.0, 0.75, 0.4) * (0.5 + 0.9 * lum), 0.3 * rteStrength);
		FragColor.rgb += flame * ((edge + fromAbove) * lick * 0.8 + ember) * rteStrength;
		emissive = max(emissive, ((edge + fromAbove) * lick * 0.6 + ember * 0.9) * rteStrength);
	}
"""),
}


def main():
	M.write_shaders(SHADERS, OUT, rte="FellLegion.rte", prefix="Fell", tool="Tools/FellLegion/shaders.py", source="Medieval.rte's Frozen.frag")
	print("fell legion shaders done")


if __name__ == "__main__":
	main()
