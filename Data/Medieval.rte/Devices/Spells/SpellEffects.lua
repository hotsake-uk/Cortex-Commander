-- The Medieval support spells' status effects (SE-1: Actor:AddStatusEffect, StatusEffect in Lua). Each entry builds the effect a spell puts
-- on a unit, so the spell scripts, the AI (which asks a unit whether it HasStatusEffect of a name before buffing it again) and later factions
-- all agree on the names. A script loads it with
--   local Effects = dofile("Medieval.rte/Devices/Spells/SpellEffects.lua");
-- and calls Effects.Apply(actor, "Blessed") (the effect's own time) or Effects.Apply(actor, "Blessed", seconds).
-- The engine draws the effect's shader on the unit while it lasts and puts the unit's own back after, lights it in the effect's colour, lists
-- it on the unit's HUD with its seconds left, and applies the numbers: a speed multiplier, damage taken and dealt, health and energy a second,
-- fire-proofing, being held, being silenced. Casting the same effect again on a unit only extends it.

local Effects = {};

-- name = {seconds, builder(effect)}
local DEFS = {
	-- A Blessing: hits harder and shrugs some off, lit gold.
	Blessed = {15, function(e)
		e.Label = "Blessed";
		e.DamageDealtMultiplier = 1.3;
		e.DamageTakenMultiplier = 0.85;
		e.Shader = "Medieval Blessed";
		e:SetLightColor(255, 220, 120);
		e.LightRadius = 70;
		e.LightIntensity = 0.5;
	end},
	-- Haste: moves at half again its pace (and swings and reloads no faster; that's the unit's own).
	Hasted = {10, function(e)
		e.Label = "Hasted";
		e.SpeedMultiplier = 1.6;
		e.Shader = "Medieval Hasted";
		e:SetLightColor(120, 240, 255);
		e.LightRadius = 50;
		e.LightIntensity = 0.4;
	end},
	-- Stoneskin: half the damage, a fifth slower, grey as granite.
	Stoneskin = {12, function(e)
		e.Label = "Stoneskin";
		e.DamageTakenMultiplier = 0.5;
		e.SpeedMultiplier = 0.8;
		e.Shader = "Medieval Stoneskin";
	end},
	-- A Fire Ward: fire can't catch on it, and burning does it no harm.
	["Fire Ward"] = {15, function(e)
		e.Label = "Fire warded";
		e.FireProof = true;
		e.Shader = "Medieval Warded";
		e:SetLightColor(255, 150, 60);
		e.LightRadius = 60;
		e.LightIntensity = 0.5;
	end},
	-- Regeneration: health back a little at a time, a wound closing now and then.
	Regenerating = {12, function(e)
		e.Label = "Regenerating";
		e.HealthPerSecond = 4;
		e:SetLightColor(120, 255, 140);
		e.LightRadius = 40;
		e.LightIntensity = 0.4;
	end},
	-- An Enchanted Weapon: everything it hits with, blade, bolt or spell, does half again the damage.
	["Enchanted Weapon"] = {12, function(e)
		e.Label = "Enchanted";
		e.DamageDealtMultiplier = 1.5;
		e:SetLightColor(200, 140, 255);
		e.LightRadius = 50;
		e.LightIntensity = 0.5;
	end},
	-- A Mirror Image: looks like the caster, hurts nothing, goes out at a touch (MirrorImage.lua).
	Illusion = {0, function(e)
		e.Label = "Illusion";
		e.DamageDealtMultiplier = 0;
		e.Shader = "Medieval Illusion";
		e:SetLightColor(190, 150, 255);
		e.LightRadius = 40;
		e.LightIntensity = 0.5;
	end},
	-- Held by a Telekinetic Grab: can't move, jump, crouch or fire, and is dragged (TelekineticGrab.lua).
	Grabbed = {1.5, function(e)
		e.Label = "Held";
		e.Harmful = true;
		e.Held = true;
		e:SetLightColor(180, 120, 255);
		e.LightRadius = 40;
		e.LightIntensity = 0.5;
	end},
	-- Turned to stone (for the creatures to come): held fast, half the damage, grey rock.
	Petrified = {6, function(e)
		e.Label = "Petrified";
		e.Harmful = true;
		e.Held = true;
		e.Silenced = true;
		e.DamageTakenMultiplier = 0.5;
		e.Shader = "Medieval Petrified";
	end},
	-- Silenced: no spell can be paid for.
	Silenced = {6, function(e)
		e.Label = "Silenced";
		e.Harmful = true;
		e.Silenced = true;
	end},
};

-- The effect by name, built fresh (a StatusEffect is a value: the unit keeps a copy).
function Effects.Make(name)
	local def = DEFS[name];
	if not def then
		return nil;
	end
	local e = StatusEffect(name);
	def[2](e);
	return e;
end

-- How long the effect lasts by default, in seconds (0: until removed).
function Effects.Seconds(name)
	local def = DEFS[name];
	return def and def[1] or 0;
end

-- Puts the effect on a unit for its own time, or for seconds. Returns whether it was put on.
function Effects.Apply(actor, name, seconds)
	if not actor or not MovableMan:ValidMO(actor) or actor.ToDelete then
		return false;
	end
	local e = Effects.Make(name);
	if not e then
		return false;
	end
	actor:AddStatusEffect(e, seconds or Effects.Seconds(name));
	return true;
end

-- The names, for anything that lists them.
function Effects.Names()
	local names = {};
	for name in pairs(DEFS) do
		names[#names + 1] = name;
	end
	table.sort(names);
	return names;
end

return Effects;
