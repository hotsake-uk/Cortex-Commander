-- Petrify's watch (made by SpellCast.lua on the unit turned to stone): follows the statue while the "Petrified" status effect holds
-- (SpellEffects.lua: held fast, silenced, half the damage, grey as granite), puffing stone dust when it is struck, and when the stone wears
-- off has the unit say so (a statue can't speak) with a last shower of dust and grit.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

function Create(self)
	self.targetUID = self:GetNumberValue("TargetUID");
	self.frame = 0;
	local target = FX.ActorByUID(self.targetUID);
	self.lastHealth = target and target.Health or 0;
	if not target then
		self.ToDelete = true;
	end
end

function Update(self)
	local target = FX.ActorByUID(self.targetUID);
	if not target then
		self.ToDelete = true;
		return;
	end
	self.frame = self.frame + 1;
	self.Pos = target.Pos;
	if not target:HasStatusEffect("Petrified") then
		EmitVisualParticles("Dust", target.Pos, Vector(0, -1), 1, 16, 0xA09890);
		EmitVisualParticles("Debris", target.Pos, Vector(0, -4), 0.6, 8, 0x6E6660);
		FX.PlaySound("Medieval Spell Stone Grind", target.Pos);
		if FX.Alive(target) then
			target:Say("Petrified");
		end
		self.ToDelete = true;
		return;
	end
	if target.Health < self.lastHealth - 0.5 then
		EmitVisualParticles("Dust", target.Pos + Vector(FX.Rand(-6, 6), FX.Rand(-10, 6)), Vector(0, -1), 0.6, 4, 0xA09890);
	end
	self.lastHealth = target.Health;
	if self.frame % 20 == 0 then
		EmitVisualParticles("Dust", target.Pos + Vector(FX.Rand(-6, 6), FX.Rand(-12, 8)), Vector(0, 0.3), 0.4, 1, 0xA09890);
	end
end
