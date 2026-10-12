-- A snare trap: thrown or dropped, it lies where it lands and arms itself a second later (it stops being an item then, so no one picks it up
-- by walking over it: it is lifted only with the pick-up key). Twice a second it looks for a foot within its jaw; the first enemy to tread
-- there has the jaw close on their leg: hurt, held fast for a few seconds (Actor:Ensnare) and bleeding, and the trap is sprung and gone.
-- It keeps the side of whoever set it, so it never closes on a friend's foot.

local reach = 7;

function Create(self)
	self.armed = false;
	self.held = false;
	self.restTimer = Timer();
	self.checkTimer = Timer();
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
	if #points == 0 then
		points[1] = actor.Pos + Vector(0, actor.Radius * 0.8);
	end
	return points;
end

local function snap(self, actor)
	actor.Health = actor.Health - 18;
	actor:Ensnare(4);
	actor.Vel = Vector(actor.Vel.X * 0.2, actor.Vel.Y);
	if actor.Status == Actor.STABLE then
		actor.Status = Actor.UNSTABLE;
	end
	actor:FlashWhite(80);
	actor:Say("Entangled");
	AudioMan:PlaySound("Medieval.rte/Sounds/Arsenal/TrapSnap1.flac", self.Pos);
	for i = 1, 6 do
		local drop = CreateMOPixel("Drop Blood", "Base.rte");
		if drop then
			drop.Pos = self.Pos + Vector(0, -3);
			drop.Vel = Vector(RangeRand(-2, 2), RangeRand(-4, -1));
			MovableMan:AddParticle(drop);
		end
	end
	for i = 1, 4 do
		local spark = CreateMOPixel("Spark Yellow 1", "Base.rte");
		if spark then
			spark.Pos = self.Pos;
			spark.Vel = Vector(RangeRand(-4, 4), RangeRand(-5, -1));
			MovableMan:AddParticle(spark);
		end
	end
	self.ToDelete = true;
end

function Update(self)
	if self:IsAttached() then
		self.held = true;
		self.armed = false;
		self.restTimer:Reset();
		return;
	end
	if not self.armed then
		-- Lying still a moment after it was put down: set.
		if self.Vel:MagnitudeIsGreaterThan(1) then
			self.restTimer:Reset();
		elseif self.restTimer:IsPastSimMS(1000) then
			self.armed = true;
			self.PinStrength = 1000;
			self.RotAngle = 0;
			self.AngularVel = 0;
		end
		return;
	end
	self.Vel = Vector();
	if not self.checkTimer:IsPastSimMS(120) then
		return;
	end
	self.checkTimer:Reset();
	for actor in MovableMan.Actors do
		if actor.Team ~= self.Team and actor.Status < Actor.DYING and SceneMan:ShortestDistance(self.Pos, actor.Pos, SceneMan.SceneWrapsX):MagnitudeIsLessThan(40) then
			for _, foot in ipairs(feet(actor)) do
				if SceneMan:ShortestDistance(self.Pos, foot, SceneMan.SceneWrapsX):MagnitudeIsLessThan(reach) then
					snap(self, actor);
					return;
				end
			end
		end
	end
end
