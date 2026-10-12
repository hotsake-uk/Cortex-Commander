#!/usr/bin/env python3
"""Writes the Medieval status effects' object shaders (Data/Medieval.rte/Shaders/*.frag) from Frozen.frag, the hand-written one: each is
Blit8.frag's sprite shader with one block of its own, between the "// Frost:" comment and the normal, swapped for the effect's look.
All fade by rteStrength (the player's mod shader strength) and use rteTime and rteObjectSeed for their movement.

Run from the repository root: python3 Tools/Medieval/shaders.py Data
"""
import os
import sys

DATA = sys.argv[1] if len(sys.argv) > 1 else "Data"
OUT = os.path.join(DATA, "Medieval.rte/Shaders")

with open(os.path.join(OUT, "Frozen.frag")) as f:
	FROZEN = f.read()

START = "\t// Frost:"
END = "\tvec3 normal = normalize(EdgeNormal("
head, rest = FROZEN.split(START, 1)
_, tail = rest.split(END, 1)
tail = END + tail

# The header comment lines of Frozen.frag, which each shader replaces with its own.
headLines = head.split("\n")
firstCode = next(i for i, line in enumerate(headLines) if line.startswith("#version"))
HEAD_CODE = "\n".join(headLines[firstCode:])

SHADERS = {
	"Blessed": (
		"A unit under a Blessing: lit gold from above, a soft halo round its edge, and motes of light drifting up through it.",
		"""	// Blessing: gold light from above, a halo at the edge and motes rising.
	{
		float lum = dot(FragColor.rgb, vec3(0.3, 0.59, 0.11));
		float fromAbove = 1.0 - Coverage(textureUV - uvDy * 2.0);
		float edge = 1.0 - Coverage(textureUV - uvDy) * Coverage(textureUV + uvDy) * Coverage(textureUV - uvDx) * Coverage(textureUV + uvDx);
		float pulse = 0.85 + 0.15 * sin(rteTime * 3.0 + rteObjectSeed * 20.0);
		vec2 texel = floor(textureUV * vec2(textureSize(rteTexture, 0)));
		float mote = step(0.97, Hash(texel + vec2(0.0, floor(rteTime * 6.0 + rteObjectSeed * 50.0))));
		vec3 gold = vec3(1.0, 0.86, 0.45);
		FragColor.rgb = mix(FragColor.rgb, gold * (0.6 + 0.8 * lum), (0.25 + 0.35 * fromAbove) * pulse * rteStrength);
		FragColor.rgb += gold * (edge * 0.5 + mote * 0.8) * pulse * rteStrength;
		emissive = max(emissive, (0.15 + 0.5 * edge + 0.6 * mote) * pulse * rteStrength);
	}
"""),
	"Hasted": (
		"A unit under Haste: tinted electric cyan with streaks of speed racing across it and a flickering double edge.",
		"""	// Haste: cyan tint, racing streaks and a flickering edge.
	{
		float lum = dot(FragColor.rgb, vec3(0.3, 0.59, 0.11));
		float streak = smoothstep(0.75, 1.0, sin(gl_FragCoord.x * 0.9 + gl_FragCoord.y * 0.3 - rteTime * 40.0 + rteObjectSeed * 30.0));
		float edge = 1.0 - Coverage(textureUV - uvDx * 2.0) * Coverage(textureUV + uvDx * 2.0);
		float flicker = step(0.5, fract(rteTime * 18.0 + rteObjectSeed));
		vec3 cyan = vec3(0.45, 0.95, 1.0);
		FragColor.rgb = mix(FragColor.rgb, cyan * (0.5 + 0.9 * lum), 0.3 * rteStrength);
		FragColor.rgb += cyan * (streak * 0.6 + edge * flicker * 0.5) * rteStrength;
		emissive = max(emissive, (0.1 + streak * 0.6 + edge * flicker * 0.4) * rteStrength);
	}
"""),
	"Stoneskin": (
		"A unit under Stoneskin: its colours turned to grey granite, grained and flecked, dull and heavy.",
		"""	// Stoneskin: grey granite, grained and flecked.
	{
		float lum = dot(FragColor.rgb, vec3(0.3, 0.59, 0.11));
		vec2 texel = floor(textureUV * vec2(textureSize(rteTexture, 0)));
		float grain = Hash(texel + vec2(rteObjectSeed * 90.0, 7.0));
		float fleck = step(0.93, Hash(texel * 1.7 + vec2(3.0, rteObjectSeed * 40.0)));
		vec3 stone = vec3(0.42, 0.42, 0.40) * (0.55 + 0.8 * lum) * (0.85 + 0.3 * grain) + vec3(0.15) * fleck;
		FragColor.rgb = mix(FragColor.rgb, stone, 0.85 * rteStrength);
		shine = mix(shine, 0.05, rteStrength);
	}
"""),
	"Warded": (
		"A unit under a Fire Ward: a thin rim of orange flame licking round its edge, embers winking on the body, the colours a little warmer.",
		"""	// Fire Ward: an orange rim of flame, winking embers, a warm tint.
	{
		float lum = dot(FragColor.rgb, vec3(0.3, 0.59, 0.11));
		float edge = 1.0 - Coverage(textureUV - uvDy) * Coverage(textureUV + uvDy) * Coverage(textureUV - uvDx) * Coverage(textureUV + uvDx);
		float lick = 0.6 + 0.4 * sin(gl_FragCoord.y * 0.7 + rteTime * 12.0 + rteObjectSeed * 25.0) * sin(gl_FragCoord.x * 0.5 - rteTime * 9.0);
		vec2 texel = floor(textureUV * vec2(textureSize(rteTexture, 0)));
		float ember = step(0.975, Hash(texel + vec2(floor(rteTime * 8.0), rteObjectSeed * 60.0)));
		vec3 flame = vec3(1.0, 0.55, 0.15);
		FragColor.rgb = mix(FragColor.rgb, vec3(1.0, 0.8, 0.6) * (0.5 + 0.9 * lum), 0.2 * rteStrength);
		FragColor.rgb += flame * (edge * lick * 0.9 + ember * 0.9) * rteStrength;
		emissive = max(emissive, (edge * lick * 0.7 + ember * 0.9) * rteStrength);
	}
"""),
	"Illusion": (
		"A Mirror Image: a see-through, wavering copy in pale violet, shot through with scan lines, its edge glowing.",
		"""	// Illusion: see-through (dithered away), violet, wavering, with scan lines and a glowing edge.
	{
		vec2 texel = floor(textureUV * vec2(textureSize(rteTexture, 0)));
		float dither = Hash(texel + vec2(floor(rteTime * 10.0), rteObjectSeed * 70.0));
		float waver = 0.5 + 0.5 * sin(gl_FragCoord.y * 0.6 + rteTime * 7.0 + rteObjectSeed * 10.0);
		if (dither < (0.25 + 0.2 * waver) * rteStrength) {
			discard;
		}
		float lum = dot(FragColor.rgb, vec3(0.3, 0.59, 0.11));
		float scan = step(0.5, fract(gl_FragCoord.y * 0.5 + rteTime * 3.0));
		float edge = 1.0 - Coverage(textureUV - uvDy) * Coverage(textureUV + uvDy) * Coverage(textureUV - uvDx) * Coverage(textureUV + uvDx);
		vec3 violet = vec3(0.75, 0.6, 1.0) * (0.4 + 1.0 * lum) * (0.8 + 0.2 * scan);
		FragColor.rgb = mix(FragColor.rgb, violet, 0.7 * rteStrength) + vec3(0.8, 0.7, 1.0) * edge * 0.6 * rteStrength;
		emissive = max(emissive, (0.2 + 0.5 * edge) * rteStrength);
	}
"""),
	"Petrified": (
		"A unit turned to stone: drained to grey rock, cracked, with a faint dust of lighter stone where the art is lightest.",
		"""	// Petrified: grey rock, cracked.
	{
		float lum = dot(FragColor.rgb, vec3(0.3, 0.59, 0.11));
		vec2 texel = floor(textureUV * vec2(textureSize(rteTexture, 0)));
		float grain = Hash(texel + vec2(rteObjectSeed * 50.0, 11.0));
		float crack = step(0.985, Hash(floor(texel / 2.0) + vec2(rteObjectSeed * 30.0, 5.0)));
		vec3 rock = vec3(0.36, 0.35, 0.33) * (0.5 + 0.9 * lum) * (0.8 + 0.4 * grain);
		rock = mix(rock, vec3(0.08), crack);
		FragColor.rgb = mix(FragColor.rgb, rock, 0.92 * rteStrength);
		shine = mix(shine, 0.0, rteStrength);
	}
"""),
}


def write_shaders(shaders, out, rte="Medieval.rte", prefix="Medieval", tool="Tools/Medieval/shaders.py", source="Frozen.frag"):
	"""Writes each shader (name: (what, block)) to out as rte's "<prefix> <name>" preset's fragment shader."""
	os.makedirs(out, exist_ok=True)
	for name, (what, block) in shaders.items():
		header = (f"// {name}.frag ({rte}, the \"{prefix} {name}\" Shader preset, written by {tool} from {source})\n"
			f"// {what} Fades by rteStrength.\n"
			"// Everything else (the three outputs, palette, snow, heat, authored maps) is Blit8.frag's.\n")
		with open(os.path.join(out, name + ".frag"), "w") as f:
			f.write(header + HEAD_CODE + block + tail)


def main():
	write_shaders(SHADERS, OUT)
	print("shaders done")


if __name__ == "__main__":
	main()
