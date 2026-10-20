-- A battle's drop crate (the Sandbox's "Drop crate" craft): falls straight down onto where it was sent, upright, crushing any enemy it comes
-- down on; once landed it lets its units out, and once empty it is gone, with nothing left lying about. Its units come to no harm in the fall.

local fallSpeed = 30; -- Most it falls at, so it neither breaks nor buries itself in the ground.
local landedSpeed = 2; -- Slower than this, and turning no faster, it has landed.
local crushRadius = 18;
local goneAfterMS = 1500; -- Once empty, how long it stands before it goes.

function Create(self)
	self.RotAngle = 0;
	self.AngularVel = 0;
	self.Vel = Vector(0, math.min(10 + 10 * 1500 / SceneMan.SceneHeight, fallSpeed));
	self.landed = false;
	self.emptyTimer = Timer();
end

local function crush(self)
	for mo in MovableMan:GetMOsInRadius(self.Pos + Vector(0, 8), crushRadius, self.Team) do
		local root = mo:GetRootParent();
		if root and root.UniqueID ~= self.UniqueID and IsActor(root) and not IsACraft(root) and root.Team ~= self.Team then
			ToActor(root).Health = 0;
		end
	end
end

local function dust(self)
	for i = 1, 8 do
		local puff = CreateMOSParticle("Small Smoke Ball 1", "Base.rte");
		if puff then
			local side = i % 2 == 0 and 1 or -1;
			puff.Pos = self.Pos + Vector(side * RangeRand(4, 14), 14);
			puff.Vel = Vector(side * RangeRand(2, 6), RangeRand(-2, 0));
			MovableMan:AddParticle(puff);
		end
	end
end

function Update(self)
	-- Upright all its life, so its units get out on either side and it never tips over.
	self.RotAngle = 0;
	self.AngularVel = 0;
	if not self.landed then
		-- Straight down, at no more than fallSpeed.
		self.Vel = Vector(0, math.min(math.max(self.Vel.Y, 0), fallSpeed));
		if self.Vel.Y > landedSpeed then
			crush(self);
		elseif self.Age > 500 then
			self.landed = true;
			dust(self);
			self:OpenHatch();
		end
		return;
	end
	if self.HatchState == ACraft.CLOSED and not self:IsInventoryEmpty() then
		self:OpenHatch();
	end
	if not self:IsInventoryEmpty() then
		self.emptyTimer:Reset();
	elseif self.emptyTimer:IsPastSimMS(goneAfterMS) then
		self.ToDelete = true;
	end
end

function Destroy(self)
	-- (Taken away, not killed: no loss to its team.)
	ActivityMan:GetActivity():ReportDeath(self.Team, -1);
end
