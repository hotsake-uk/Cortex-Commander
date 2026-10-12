-- The Giant Beetle's carapace (Bestiary II): a "Carapace" status effect (SE-1) kept on it for good, so it takes only a share of the
-- harm. A hard enough blow (a fall of health at once) flips it on its back: its carapace is off, it is held where it lies, belly up and
-- "Flipped" (taking half again as much) for a moment, then it rights itself and the carapace is back. The beetle's number values:
--   CarapaceTaken         the share of harm it takes with its shell (default 0.6)
--   CarapaceFlipDamage    how much health it must lose in one update to be flipped (default 20)
--   CarapaceFlipSeconds   how long it lies flipped (default 2.5)
--   CarapaceFlipCooldown  the least time between flips, in ms (default 6000)

local function Number(self, key, default)
	return self:NumberValueExists(key) and self:GetNumberValue(key) or default;
end

local function shell(self)
	local e = StatusEffect("Carapace");
	e.Label = "Carapace";
	e.DamageTakenMultiplier = Number(self, "CarapaceTaken", 0.6);
	self:AddStatusEffect(e, 36000);
end

function Create(self)
	self.lastHealth = self.Health;
	self.flipTimer = Timer();
	self.flipTimer.ElapsedSimTimeMS = 100000;
	self.flippedTimer = Timer();
	self.flipped = false;
	shell(self);
end

function Update(self)
	if self.Status >= Actor.DYING then
		return;
	end
	if self.flipped then
		if self.flippedTimer:IsPastSimMS(Number(self, "CarapaceFlipSeconds", 2.5) * 1000) then
			-- Righted: the shell back on.
			self.flipped = false;
			shell(self);
			self:AddImpulseForce(Vector(0, -4) * self.Mass, Vector());
		end
	else
		if not self:HasStatusEffect("Carapace") then
			shell(self);
		end
		local lost = self.lastHealth - self.Health;
		if lost >= Number(self, "CarapaceFlipDamage", 20) and self.flipTimer:IsPastSimMS(Number(self, "CarapaceFlipCooldown", 6000)) then
			-- Over it goes: the shell off, held on its back, and the harm goes in.
			self:RemoveStatusEffect("Carapace");
			local e = StatusEffect("Flipped");
			e.Label = "Flipped";
			e.Harmful = true;
			e.Held = true;
			e.DamageTakenMultiplier = 1.5;
			self:AddStatusEffect(e, Number(self, "CarapaceFlipSeconds", 2.5));
			self.Status = Actor.UNSTABLE;
			self.AngularVel = (self.HFlipped and -1 or 1) * 10;
			self:AddImpulseForce(Vector(0, -7) * self.Mass, Vector());
			self.flipped = true;
			self.flipTimer:Reset();
			self.flippedTimer:Reset();
			EmitVisualParticles("Dust", self.Pos + Vector(0, self.Radius * 0.5), Vector(0, -2), 0.8, 8, 0x6A6660);
		end
	end
	self.lastHealth = self.Health;
end
