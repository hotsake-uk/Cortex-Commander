-- Bolas: thrown, they whirl through the air, and when they pass close by an enemy's legs (within a dozen pixels) they wrap round them. The
-- bolas themselves are gone then; an invisible Arsenal Bolas Tangle (BolasTangle.lua) holds the unit's legs bound for a few seconds and
-- drops the bolas when it lets go. Only a throw arms them: dropped or lying about, they're just cords and weights to pick up.

local wrapReach = 12;

function Create(self)
	self.holderID = nil;
	self.armed = false;
	self.flightTimer = Timer();
end

-- Where a unit's legs are: its shins and feet, or about where they'd be for anything else that walks.
local function legPoints(actor)
	local points = {};
	if IsAHuman(actor) then
		local human = ToAHuman(actor);
		for _, part in ipairs({ human.FGLeg, human.BGLeg, human.FGFoot, human.BGFoot }) do
			if part then
				points[#points + 1] = part.Pos;
			end
		end
	end
	if #points == 0 then
		points[1] = actor.Pos + Vector(0, 8);
	end
	return points;
end

function Update(self)
	if self:IsAttached() then
		local root = self:GetRootParent();
		self.holderID = (root and IsActor(root)) and root.UniqueID or nil;
		self.armed = false;
		return;
	end
	if self.holderID then
		self.holderID = nil;
		if self.Vel:MagnitudeIsGreaterThan(3.5) then
			self.armed = true;
			self.flightTimer:Reset();
			self.AngularVel = (self.Vel.X >= 0 and -1 or 1) * 24;
			AudioMan:PlaySound("Medieval.rte/Sounds/Arsenal/BolasWhirr1.flac", self.Pos);
		end
	end
	if not self.armed then
		return;
	end
	if self.flightTimer:IsPastSimMS(3000) or self.Vel:MagnitudeIsLessThan(3) then
		self.armed = false;
		return;
	end
	-- Keep them whirling through the air.
	if math.abs(self.AngularVel) < 14 then
		self.AngularVel = (self.AngularVel >= 0 and 1 or -1) * 18;
	end

	for actor in MovableMan.Actors do
		if actor.Team ~= self.Team and actor.Status < Actor.DYING and (IsAHuman(actor) or IsACrab(actor)) then
			for _, point in ipairs(legPoints(actor)) do
				if SceneMan:ShortestDistance(self.Pos, point, SceneMan.SceneWrapsX):MagnitudeIsLessThan(wrapReach) then
					local tangle = CreateMOSParticle("Arsenal Bolas Tangle", "Medieval.rte");
					if tangle then
						tangle.Pos = Vector(actor.Pos.X, actor.Pos.Y);
						tangle.Team = self.Team;
						tangle:SetNumberValue("TargetID", actor.UniqueID);
						MovableMan:AddParticle(tangle);
					end
					AudioMan:PlaySound("Medieval.rte/Sounds/Arsenal/BolasWrap1.flac", self.Pos);
					actor:Say("Entangled");
					self.armed = false;
					self.ToDelete = true;
					return;
				end
			end
		end
	end
end
