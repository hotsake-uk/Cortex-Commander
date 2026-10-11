-- One caltrop on the ground: it always lands with a point up. Four times a second (each caltrop at its own moment) it looks for an enemy's
-- foot on it; one that treads on it is hurt a little, hops, sometimes stumbles, and says so. The same man is bitten again only after a
-- moment, so he can't be killed standing on one. It lies there for its LifeTime (30 s).

local reach = 5.5;

function Create(self)
	self.checkTimer = Timer();
	self.checkTimer.ElapsedSimTimeMS = math.random(0, 240);
	self.bitten = {};
end

local function feet(actor)
	local points = {};
	if IsAHuman(actor) then
		local human = ToAHuman(actor);
		if human.FGFoot then
			points[#points + 1] = human.FGFoot.Pos;
		end
		if human.BGFoot then
			points[#points + 1] = human.BGFoot.Pos;
		end
	end
	return points;
end

local function bite(self, actor)
	local last = self.bitten[actor.UniqueID];
	if last and self.Age - last < 1500 then
		return;
	end
	self.bitten[actor.UniqueID] = self.Age;
	actor.Health = actor.Health - 6;
	actor.Vel = actor.Vel + Vector(RangeRand(-1, 1), -3.5);
	if actor.Status == Actor.STABLE and math.random() < 0.35 then
		actor.Status = Actor.UNSTABLE;
	end
	actor:FlashWhite(60);
	actor:Say("Caltrops");
	AudioMan:PlaySound("Medieval.rte/Sounds/Arsenal/CaltropStep1.flac", self.Pos);
	for i = 1, 3 do
		local drop = CreateMOPixel("Drop Blood", "Base.rte");
		if drop then
			drop.Pos = self.Pos + Vector(0, -2);
			drop.Vel = Vector(RangeRand(-1.5, 1.5), RangeRand(-3, -1));
			MovableMan:AddParticle(drop);
		end
	end
end

function Update(self)
	self.RotAngle = 0;
	self.AngularVel = 0;
	if self.Age < 500 or not self.checkTimer:IsPastSimMS(250) then
		return;
	end
	self.checkTimer:Reset();
	if self.Vel:MagnitudeIsGreaterThan(3) then
		return;
	end
	for actor in MovableMan.Actors do
		if actor.Team ~= self.Team and actor.Status < Actor.DYING then
			for _, foot in ipairs(feet(actor)) do
				if SceneMan:ShortestDistance(self.Pos, foot, SceneMan.SceneWrapsX):MagnitudeIsLessThan(reach) then
					bite(self, actor);
					break;
				end
			end
		end
	end
end
