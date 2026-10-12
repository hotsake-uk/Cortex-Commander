-- The Fell Legion's status effects (Medieval.rte's SpellEffects.lua pattern, Actor:AddStatusEffect): what the dark spells put on a unit.
-- A script loads it with local Fell = dofile("FellLegion.rte/Devices/Spells/FellEffects.lua") and calls Fell.Apply(actor, "Hexed") or
-- Fell.Apply(actor, "Hexed", seconds). The engine draws the effect's shader (Shaders/Shaders.ini) on the unit while it lasts, lights it in
-- the effect's colour, lists it on the unit's HUD, and applies the numbers. Harmful effects are what Dispel takes off (the Kingdom's AI
-- knows "Hexed", "Cursed" and "Doomed" as curses to dispel).

local Fell = {};

-- name = {seconds, builder(effect)}
local DEFS = {
	-- A Hex: the arm weakened, every blow and spell doing little more than half.
	Hexed = {8, function(e)
		e.Label = "Hexed";
		e.Harmful = true;
		e.DamageDealtMultiplier = 0.6;
		e.Shader = "Fell Hexed";
		e:SetLightColor(170, 80, 255);
		e.LightRadius = 40;
		e.LightIntensity = 0.4;
	end},
	-- A Curse of Weakness: half again the damage taken, a fifth slower.
	Cursed = {10, function(e)
		e.Label = "Cursed";
		e.Harmful = true;
		e.DamageTakenMultiplier = 1.5;
		e.SpeedMultiplier = 0.8;
		e.Shader = "Fell Cursed";
		e:SetLightColor(120, 30, 160);
		e.LightRadius = 40;
		e.LightIntensity = 0.4;
	end},
	-- Unholy Frenzy: faster and harder-hitting, and the worse for every blow taken.
	Frenzied = {10, function(e)
		e.Label = "Frenzied";
		e.SpeedMultiplier = 1.4;
		e.DamageDealtMultiplier = 1.5;
		e.DamageTakenMultiplier = 1.3;
		e.Shader = "Fell Frenzied";
		e:SetLightColor(255, 60, 40);
		e.LightRadius = 50;
		e.LightIntensity = 0.5;
	end},
	-- Burning with hellfire: health away a little at a time while the green flames cling.
	Burning = {4, function(e)
		e.Label = "Burning";
		e.Harmful = true;
		e.HealthPerSecond = -3;
		e.Shader = "Fell Burning";
		e:SetLightColor(120, 255, 90);
		e.LightRadius = 60;
		e.LightIntensity = 0.7;
	end},
	-- Afraid: running from a Fear, too shaken to hit anything properly, and quick about it.
	Afraid = {4, function(e)
		e.Label = "Afraid";
		e.Harmful = true;
		e.DamageDealtMultiplier = 0.7;
		e.SpeedMultiplier = 1.25;
		e:SetLightColor(90, 40, 120);
		e.LightRadius = 30;
		e.LightIntensity = 0.4;
	end},
	-- Harvesting: a Lich under a Soul Harvest, the souls of those who die near him feeding him (SoulHarvest.lua).
	Harvesting = {10, function(e)
		e.Label = "Soul Harvest";
		e:SetLightColor(90, 255, 110);
		e.LightRadius = 60;
		e.LightIntensity = 0.5;
	end},
	-- Shrouded: wrapped in a Shroud (the cloak powerup does the hiding; this marks the caster so the AI doesn't cast it again meanwhile).
	Shrouded = {6, function(e)
		e.Label = "Shrouded";
		e:SetLightColor(60, 40, 90);
		e.LightRadius = 20;
		e.LightIntensity = 0.2;
	end},
	-- Nightfall: the caster of a Darkness while it lasts (Darkness.lua does the dark; this marks the caster for the AI as Shrouded does).
	Nightfall = {8, function(e)
		e.Label = "Nightfall";
		e:SetLightColor(40, 40, 120);
		e.LightRadius = 50;
		e.LightIntensity = 0.5;
	end},
};

function Fell.Make(name)
	local def = DEFS[name];
	if not def then
		return nil;
	end
	local e = StatusEffect(name);
	def[2](e);
	return e;
end

function Fell.Seconds(name)
	local def = DEFS[name];
	return def and def[1] or 0;
end

-- Puts the effect on a unit for its own time, or for seconds. Returns whether it was put on.
function Fell.Apply(actor, name, seconds)
	if not actor or not MovableMan:ValidMO(actor) or actor.ToDelete then
		return false;
	end
	local e = Fell.Make(name);
	if not e then
		return false;
	end
	actor:AddStatusEffect(e, seconds or Fell.Seconds(name));
	return true;
end

return Fell;
