-- A pouch of caltrops: once thrown (or dropped from a falling man's belt) it bursts open where it lands, scattering its caltrops
-- (Arsenal Caltrop, Caltrop.lua) on the ground about it. They keep the thrower's side, so they only bite the other.

local count = 8;

local function burst(self)
	if self.burst then
		return;
	end
	self.burst = true;
	for i = 1, count do
		local caltrop = CreateMOSParticle("Arsenal Caltrop", "Medieval.rte");
		if caltrop then
			caltrop.Pos = self.Pos + Vector(RangeRand(-3, 3), -3);
			caltrop.Vel = Vector(self.Vel.X * 0.3 + RangeRand(-5, 5), RangeRand(-6, -2));
			caltrop.Team = self.Team;
			MovableMan:AddParticle(caltrop);
		end
	end
	AudioMan:PlaySound("Medieval.rte/Sounds/Arsenal/CaltropScatter1.flac", self.Pos);
	self.ToDelete = true;
end

function Create(self)
	self.held = false;
	self.freeTimer = Timer();
end

function Update(self)
	if self:IsAttached() then
		self.held = true;
		self.freeTimer:Reset();
		return;
	end
	-- However it came to rest, it spills in the end.
	if self.held and self.freeTimer:IsPastSimMS(3000) then
		burst(self);
	end
end

function OnCollideWithTerrain(self, terrainID)
	if self.held and not self:IsAttached() and self.freeTimer:IsPastSimMS(30) then
		burst(self);
	end
end

function OnCollideWithMO(self, hitMO, hitMORootParent)
	if self.held and not self:IsAttached() and self.freeTimer:IsPastSimMS(30) then
		burst(self);
	end
end
