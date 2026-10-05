"""Brings downloaded mods up to date with this version of the game: text replacements in their INI and Lua files.
Run after Get-Mods.ps1: python Tools/Mods/Fix-Mods.py. It looks in Mods and ModsParked. Safe to run again; a rule that has nothing left to change does nothing.

The engine already forgives the common things (an old version number, a setting it doesn't know, a missing sound, old jetpacks, renamed script functions).
What's here is what it can't guess: a preset that was renamed, or a script that assumes something that is no longer always true."""
import glob, os, re

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
FOLDERS = [os.path.join(ROOT, name) for name in ('Mods', 'ModsParked')]

# Plain replacements: (module folder or * for every mod, file pattern inside it, text to find, text to put instead, why)
RULES = [
	('Empire.rte', '**/*.ini', 'CopyOf = Imperatus Robot Walk Path', 'CopyOf = Imperatus Combat Robot Walk Path', "The Imperatus robot's walk path was renamed in the game."),
	('HalfLifeHECU.rte', '**/*.lua', '"Ronin Kar98 Bolt Pull Sound", "Ronin.rte"', '"Bolt Back Ronin Kar98", "Ronin.rte"', "The stock rifle's bolt sound was renamed in the game."),
]

# Pattern replacements (regular expressions): (module folder or *, file pattern, pattern, replacement, why)
PATTERN_RULES = [
	# Scripts that go through every object ID and use what they get straight away:
	#     thing = MovableMan:GetMOFromID(i)
	#     if thing.PresetName == ... then
	# An ID can belong to something that is already gone, and the lookup gives nil for those. The check is made to ask first.
	('*', '**/*.lua', r'(\b(\w+) = (?:To\w+\()?MovableMan:GetMOFromID\([^()\n]*(?:\([^()\n]*\))?[^()\n]*\)\)?[ \t]*;?[ \t]*(?:--[^\n]*)?\r?\n[ \t]*if )(?!\2 and )(?!\2 then)(?!\2 ~= nil)(?!not \2)(\2\.)', r'\1\2 and \3',
	 'An object ID can belong to something that is already gone; the lookup gives nil for those.'),
]


def main():
	changed = 0
	for rules, isPattern in ((RULES, False), (PATTERN_RULES, True)):
		for module, pattern, old, new, why in rules:
			paths = []
			for folder in FOLDERS:
				paths += glob.glob(os.path.join(folder, '*.rte' if module == '*' else module, pattern), recursive=True)
			for path in paths:
				raw = open(path, 'rb').read()
				try:
					text = raw.decode('utf-8')
					encoding = 'utf-8'
				except UnicodeDecodeError:
					text = raw.decode('latin-1')
					encoding = 'latin-1'
				fixed = re.sub(old, new, text) if isPattern else text.replace(old, new)
				if fixed != text:
					open(path, 'wb').write(fixed.encode(encoding))
					changed += 1
					print('fixed %s: %s' % (os.path.relpath(path, ROOT), why))
	print('%d file(s) changed' % changed)


if __name__ == '__main__':
	main()
