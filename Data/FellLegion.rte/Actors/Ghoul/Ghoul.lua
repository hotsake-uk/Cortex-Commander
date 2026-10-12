-- A Ghoul: its wounds knit themselves closed. While it lives it keeps the "Regenerating" status effect on itself (Medieval.rte's
-- SpellEffects.lua: health back a little at a time), put back every few seconds, and twitches green when a wound closes. A ghoul raised
-- by Raise Ghoul (RaiseGhoul.lua) carries NumberValue FellSummonerUID and crumbles after its time; a bought one lives as long as it can.

local Effects = dofile("Medieval.rte/Devices/Spells/SpellEffects.lua");

function Create(self)
	self.regenTimer = Timer();
	self.lastHealth = self.Health;
end

function Update(self)
	if self.Status >= Actor.DYING then
		return;
	end
	if self.regenTimer:IsPastSimMS(4000) then
		self.regenTimer:Reset();
		if not self:HasStatusEffect("Regenerating") then
			Effects.Apply(self, "Regenerating", 6);
		end
	end
	if self.Health > self.lastHealth + 0.5 and math.random() < 0.3 then
		EmitVisualParticles("Mist", self.Pos + Vector(math.random(-5, 5), math.random(-8, 4)), Vector(0, -0.5), 0.4, 1, 0x60C060);
	end
	self.lastHealth = self.Health;
end
