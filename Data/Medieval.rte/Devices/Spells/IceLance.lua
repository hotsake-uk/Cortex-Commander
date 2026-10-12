-- Ice Lance's lance: a long spear of ice. The engine's hit does the damage (DamageOnCollision and its sharpness); a unit not of the
-- caster's side that it strikes is chilled for four seconds (FX.Chill, FrostChill.lua), and where it strikes a wall or the ground a
-- crust of ice (the "Medieval Spell Ice Crust" prop) is laid and settles into the terrain as real ice. A trail of frost follows it.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local function shatter(pos, count)
	EmitVisualParticles("Sparks", pos, Vector(0, -3), 1, count, 0xDDF4FF);
	EmitVisualParticles("Mist", pos, Vector(0, -0.5), 0.8, math.floor(count / 3), 0xE8F8FF);
	FX.Flash("Ice", pos);
	FX.PlaySound("Medieval Spell Ice Shatter", pos);
end

function Create(self)
	self.frame = 0;
end

function Update(self)
	self.frame = self.frame + 1;
	if self.frame % 2 == 0 then
		EmitVisualParticles("Mist", self.Pos, Vector(0, -0.3), 0.5, 1, 0xD8F0FF);
	end
	PostProcessMan:AddLight(self.Pos, 40, 160, 210, 255, 0.6);
end

function OnCollideWithMO(self, mo, rootMO)
	if self.struck then
		return;
	end
	self.struck = true;
	if rootMO and IsActor(rootMO) and rootMO.Team ~= self.Team then
		local actor = ToActor(rootMO);
		FX.Chill(actor, 4000, self.Team);
		actor:FlashWhite(120);
	end
	shatter(self.Pos, 14);
end

function OnCollideWithTerrain(self, terrainID)
	if self.crusted then
		return;
	end
	self.crusted = true;
	-- The crust: laid a little back from the wall along the way the lance came, so it sits on the surface, and settled at once.
	local back = self.Vel.Magnitude > 0.1 and (self.Vel / self.Vel.Magnitude) * 3 or Vector(0, 2);
	local crust = FX.Spawn("MOSRotating", "Medieval Spell Ice Crust", FX.Module, self.Pos - back, Vector());
	if crust then
		crust.RotAngle = 0;
		crust.ToSettle = true;
	end
	shatter(self.Pos, 20);
	self.ToDelete = true;
end
