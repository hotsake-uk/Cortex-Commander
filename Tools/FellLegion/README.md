# FellLegion.rte generators

The Fell Legion's art, sounds and actor definitions are made by these scripts on top of `Tools/Medieval`'s (the Legion is the kingdom's dark mirror, so most of its drawings are the Medieval grids in black iron, crimson, purple and sickly green).

- `sprites.py <Data folder>`: the units' torsos and heads (Medieval's grids swapped to the Legion's colours, plus the Dread Lord's horned helm, the Executioner's hood and the Lich's skull), their limbs (Ronin's, recoloured), the weapons (black steel versions of the Medieval ones, and the Legion's own Executioner's axe, barbed flail and bone javelin), the pots, the module icon, and the spells' books, orbs, rune circle and glows.
- `actors.py <Data folder>`: writes `Actors.ini` with `Tools/Medieval/actors.py`'s writer (`MODULE = "FellLegion"`): twelve units, every one in the Unholy group.
- `sounds.py <Data folder>`: synthesises `Sounds/` as FLAC with `Tools/Medieval/spellsounds.py`'s helpers (needs numpy and ffmpeg): the Doom's toll and burst, the chains, the shadow bolt, the life drain, the bone throw and the hellfire's fall.

Run from the repository root: `python3 Tools/FellLegion/sprites.py Data`, and the same for the others. `Index.ini`, `Devices.ini`, `Devices/Arsenal/Arsenal.ini`, `Devices/Spells/Spells.ini`, `Loadouts.ini`, `Sounds.ini`, `Speech/FellLegion.ini` and the Lua scripts are written by hand.
