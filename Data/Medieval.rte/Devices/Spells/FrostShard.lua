-- A shard of Frost Shards: a sharp sliver of ice. The engine's hit does the damage (DamageOnCollision); a unit not of the caster's side
-- that it strikes is chilled for three seconds (FX.Chill, FrostChill.lua). It shatters into glittering ice where it hits.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local function shatter(pos)
	EmitVisualParticles("Sparks", pos, Vector(0, -3), 1, 6, 0xDDF4FF);
	EmitVisualParticles("Mist", pos, Vector(0, -0.5), 0.8, 2, 0xE8F8FF);
end

function OnCollideWithMO(self, mo, rootMO)
	if self.struck then
		return;
	end
	self.struck = true;
	if rootMO and IsActor(rootMO) and rootMO.Team ~= self.Team then
		FX.Chill(ToActor(rootMO), 3000, self.Team);
	end
	shatter(self.Pos);
	if math.random() < 0.3 then
		FX.PlaySound("Medieval Spell Ice Shatter", self.Pos);
	end
end

function OnCollideWithTerrain(self, terrainID)
	if not self.shattered then
		self.shattered = true;
		shatter(self.Pos);
		self.ToDelete = true;
	end
end
