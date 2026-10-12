-- The Plague Pot's miasma (an invisible gib where it broke): for seven seconds a thick purple-green fog hangs there, and every unit
-- standing in it is poisoned (Actor:Envenom: a little damage a second for a few seconds, slowed), the poison kept up while they stay.
-- The thrower's own side is poisoned too: it's a cloud, not a spell.

local RADIUS = 48;

function Create(self)
	self.Vel = Vector();
	self.poisonTimer = Timer();
	PostProcessMan:AddFog(self.Pos, RADIUS, 0.8);
end

function Update(self)
	self.Vel = Vector();
	local fade = math.max(1 - self.Age / self.Lifetime, 0);
	-- The fog, kept up: purple mist, rolling.
	if math.random() < 0.6 * fade + 0.1 then
		EmitVisualParticles("Mist", self.Pos + Vector(math.random(-RADIUS, RADIUS) * 0.7, math.random(-20, 6)), Vector(math.random(-1, 1), -0.6), 0.8, 2, 0x6A3A8C);
	end
	PostProcessMan:AddLight(self.Pos + Vector(0, -8), 30 + 60 * fade, 150, 70, 200, 0.5 * fade);
	-- Whoever stands in it.
	if self.poisonTimer:IsPastSimMS(500) then
		self.poisonTimer:Reset();
		for mo in MovableMan:GetMOsInRadius(self.Pos, RADIUS * (0.6 + 0.4 * fade), -1) do
			local root = mo:GetRootParent();
			if root and IsActor(root) then
				local actor = ToActor(root);
				if actor.Status < Actor.DYING then
					actor:Envenom(3, 4, 0.7);
				end
			end
		end
	end
end
