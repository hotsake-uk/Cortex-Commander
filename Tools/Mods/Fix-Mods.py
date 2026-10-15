"""Brings downloaded mods up to date with this version of the game: run after Get-Mods.ps1 with python Tools/Mods/Fix-Mods.py.
It looks in Mods and ModsParked, and is safe to run again; a fix that has nothing left to change does nothing.

The fixes themselves live in Check-Mod.py and mod_history.json (renamed presets, functions and settings, missing nil checks, letter case
in paths), so there is one list to add to. This runs that checker over every mod with --fix --yes; run Check-Mod.py yourself to see
what's still wrong afterwards, or to look before anything changes."""
import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))

if __name__ == '__main__':
	sys.exit(subprocess.call([sys.executable, os.path.join(HERE, 'Check-Mod.py'), '--all', '--fix', '--yes', '--quiet'] + sys.argv[1:]))
