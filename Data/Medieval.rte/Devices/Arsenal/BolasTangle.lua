-- Bolas wrapped round a unit's legs (an invisible particle that follows it; NumberValue TargetID is its UniqueID): for its lifetime the unit
-- can't keep its feet, can't run or leap, and the cords are drawn wound round its shins with the weights dangling. Then it kicks free and
-- the bolas fall to the ground, to be picked up again.

local ropeLight = 66;
local ropeDark = 22;
local weightColor = 169;

local function shins(actor)
	if IsAHuman(actor) then
		local human = ToAHuman(actor);
		local a = human.FGFoot and human.FGFoot.Pos or (human.FGLeg and human.FGLeg.Pos);
		local b = human.BGFoot and human.BGFoot.Pos or (human.BGLeg and human.BGLeg.Pos);
		if human.FGLeg and a then
			a = (a + human.FGLeg.Pos) * 0.5;
		end
		if human.BGLeg and b then
			b = (b + human.BGLeg.Pos) * 0.5;
		end
		if a and b then
			return a, b;
		end
		if a or b then
			return a or b, a or b;
		end
	end
	local p = actor.Pos + Vector(0, 8);
	return p, p;
end

local function free(self, actor)
	if actor then
		local bolas = CreateThrownDevice("Bolas", "Medieval.rte");
		if bolas then
			bolas.Pos = actor.Pos + Vector(0, 6);
			bolas.Vel = Vector(RangeRand(-2, 2), -3);
			MovableMan:AddItem(bolas);
		end
	end
	self.ToDelete = true;
end

function Create(self)
	self.targetID = self:NumberValueExists("TargetID") and self:GetNumberValue("TargetID") or 0;
	self.Vel = Vector();
end

function Update(self)
	local found = MovableMan:FindObjectByUniqueID(self.targetID);
	if not found or not MovableMan:ValidMO(found) or not IsActor(found) then
		self.ToDelete = true;
		return;
	end
	local actor = ToActor(found);
	if actor.Status >= Actor.DYING then
		free(self, actor);
		return;
	end
	if self.Age >= self.Lifetime - 50 then
		free(self, actor);
		return;
	end
	self.Pos = Vector(actor.Pos.X, actor.Pos.Y);
	self.Vel = Vector();

	-- Bound: down, and going nowhere fast.
	if actor.Status == Actor.STABLE then
		actor.Status = Actor.UNSTABLE;
	end
	local vel = actor.Vel;
	actor.Vel = Vector(vel.X * 0.85, math.max(vel.Y, -1.5));

	-- The cords wound round both shins, and the weights hanging off them.
	local a, b = shins(actor);
	local mid = (a + b) * 0.5;
	local half = math.max(math.abs(SceneMan:ShortestDistance(a, b, SceneMan.SceneWrapsX).X) * 0.5 + 2, 3);
	for k = 0, 2 do
		local y = k * 2.5 - 3;
		PrimitiveMan:DrawLinePrimitive(mid + Vector(-half, y), mid + Vector(half, y + 1.5), k == 1 and ropeDark or ropeLight);
		PrimitiveMan:DrawLinePrimitive(mid + Vector(half, y + 1.5), mid + Vector(-half, y + 2.5), ropeDark);
	end
	local swing = math.sin(self.Age * 0.012) * 1.5;
	local weights = { Vector(-half - 1 + swing, 5), Vector(half + 1 - swing, 6), Vector(swing * 0.5, 7) };
	for i, offset in ipairs(weights) do
		local anchor = mid + Vector((i == 1 and -half) or (i == 2 and half) or 0, 1);
		PrimitiveMan:DrawLinePrimitive(anchor, mid + offset, ropeLight);
		PrimitiveMan:DrawCircleFillPrimitive(mid + offset, 1, weightColor);
	end
end
