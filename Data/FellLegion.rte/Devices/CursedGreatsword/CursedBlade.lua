-- The Cursed Greatsword: each blow that lands (in a swing or thrown) gives its wielder back a little health, drawn up the blade as a thread
-- of green light from the point to his chest, the runes flaring. NumberValue LifeSteal: the health given back a hit (6).
-- (HeavyWeapon.lua, the Greatsword's, runs beside this one: the sparks, the knock-down and the roar.)

function Create(self)
	self.lifeSteal = self:NumberValueExists("LifeSteal") and self:GetNumberValue("LifeSteal") or 6;
	self.lastSinceHit = self.TimeSinceBladeHit;
	self.drainTimer = Timer();
	self.drainFrom = nil;
end

local function wielderOf(self)
	local root = self:GetRootParent();
	if root and root.UniqueID ~= self.UniqueID and IsActor(root) then
		return ToActor(root);
	end
	return nil;
end

function Update(self)
	local since = self.TimeSinceBladeHit;
	local wielder = wielderOf(self);
	if since < self.lastSinceHit and wielder then
		-- A fresh hit: the drain, if the blade struck a unit rather than a wall (something not of the wielder's side within reach of the point).
		local tip = self.BladeEndPos;
		local struck = false;
		for mo in MovableMan:GetMOsInRadius(tip, 16, wielder.Team) do
			local root = mo:GetRootParent();
			if root and IsActor(root) and root.Team ~= wielder.Team then
				struck = true;
				break;
			end
		end
		if struck then
			wielder.Health = math.min(wielder.Health + self.lifeSteal, wielder.MaxHealth);
			self.drainTimer:Reset();
			self.drainFrom = Vector(tip.X, tip.Y);
			EmitVisualParticles("Sparks", tip, Vector(0, -3), 1, 10, 0x80FF80);
		end
	end
	self.lastSinceHit = since;
	if self.drainFrom and not self.drainTimer:IsPastSimMS(320) and wielder then
		local t = self.drainTimer.ElapsedSimTimeMS / 320;
		local to = wielder.Pos + Vector(0, -4);
		local at = self.drainFrom + (to - self.drainFrom) * t;
		PostProcessMan:AddEnergyBeam(self.drainFrom, at, 2, 90, 240, 110, 1.2 * (1 - t) + 0.3, 30);
		PostProcessMan:AddLight(at, 40, 90, 240, 110, 1.0);
		EmitVisualParticles("Mist", at, Vector(0, -1), 0.6, 1, 0x60C070);
	elseif self.drainFrom then
		self.drainFrom = nil;
	end
	if wielder and self:IsAttached() then
		-- The runes glow along the blade.
		if math.random() < 0.15 then
			EmitVisualParticles("Embers", (self.BladeStartPos + self.BladeEndPos) * 0.5 + Vector(math.random(-6, 6), 0), Vector(0, -0.5), 0.3, 1, 0x60FF70);
		end
	end
end
