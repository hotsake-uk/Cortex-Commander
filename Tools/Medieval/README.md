# Medieval.rte generators

The Medieval faction's art, sounds and actor definitions are made by these scripts, so a change to a unit is a change here and a re-run, not a hand edit of 60 PNGs.

- `sprites.py <Data folder>`: the torsos, heads, weapons, shields, arrows and module icon are ASCII grids in the script; the arms, legs, feet and hands are Ronin's, recoloured onto each unit's colours so their frames and joints still fit. Everything is saved in the game palette.
- `sounds.py <Data folder>`: synthesises the swings, clashes, cuts, bow and crossbow shots, bow draw and arrow hits as FLAC (needs numpy and ffmpeg).
- `buildings.py <Data folder>`: draws the buildings (huts, cottages, a longhouse, a stone house, a watchtower, tents, a market stall, a palisade, thatched roofs) and writes `Scenes/Buildings/Buildings.ini`. Each is a terrain object in the build menu's Bunker Modules (and its own Medieval Buildings group), made of Wood, Stone and the module's own Thatch, Canvas and Daub (`Materials.ini`).
- `actors.py <Data folder>`: writes `Actors.ini` (each unit's head, limbs and body, and the leap they have instead of a jetpack).

Run from the repository root: `python3 Tools/Medieval/sprites.py Data`, and the same for the others. `Devices.ini`, `Sounds.ini`, `Loadouts.ini` and `Materials.ini` are written by hand.
