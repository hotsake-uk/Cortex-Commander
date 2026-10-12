-- A hunting net: thrown, it spreads in the air, and when it passes close by an enemy it drops over them. The net is gone then; an invisible
-- Arsenal Net Tangle (NetTangle.lua) holds the unit snared for a few seconds and leaves the net on the ground when it lets go. Only a throw
-- arms it: dropped or lying about, it's a net to pick up.

local wrapReach = 16;

function Create(self)
	self.holderID = nil;
	self.armed = false;
	self.flightTimer = Timer();
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
			self.AngularVel = (self.Vel.X >= 0 and -1 or 1) * 6;
		end
	end
	if not self.armed then
		return;
	end
	if self.flightTimer:IsPastSimMS(3000) or self.Vel:MagnitudeIsLessThan(2.5) then
		self.armed = false;
		return;
	end
	for actor in MovableMan.Actors do
		if actor.Team ~= self.Team and actor.Status < Actor.DYING and (IsAHuman(actor) or IsACrab(actor)) then
			if SceneMan:ShortestDistance(self.Pos, actor.Pos, SceneMan.SceneWrapsX):MagnitudeIsLessThan(wrapReach + actor.Radius * 0.5) then
				local tangle = CreateMOSParticle("Arsenal Net Tangle", "Medieval.rte");
				if tangle then
					tangle.Pos = Vector(actor.Pos.X, actor.Pos.Y);
					tangle.Team = self.Team;
					tangle:SetNumberValue("TargetID", actor.UniqueID);
					MovableMan:AddParticle(tangle);
				end
				actor:Ensnare(3.2);
				AudioMan:PlaySound("Medieval.rte/Sounds/Arsenal/NetWrap1.flac", self.Pos);
				actor:Say("Entangled");
				self.armed = false;
				self.ToDelete = true;
				return;
			end
		end
	end
end
