-- A net dropped over a unit (an invisible particle that follows it; NumberValue TargetID is its UniqueID): while it lasts the net is drawn
-- over the unit as a mesh of cord, and the engine's snare (Actor:Ensnare, set by Net.lua) keeps them from going anywhere. Then the net
-- falls to the ground beside them, to be folded up again.

local cordLight = 66;
local cordDark = 22;

local function free(self, actor)
	if actor then
		local net = CreateThrownDevice("Net", "Medieval.rte");
		if net then
			net.Pos = actor.Pos + Vector(RangeRand(-6, 6), 6);
			net.Vel = Vector(RangeRand(-1, 1), -2);
			MovableMan:AddItem(net);
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
	if actor.Status >= Actor.DYING or self.Age >= self.Lifetime - 50 or not actor.IsEnsnared then
		free(self, actor);
		return;
	end
	self.Pos = Vector(actor.Pos.X, actor.Pos.Y);
	self.Vel = Vector();
	-- The mesh, draped over them: a diamond lattice from the head down past the knees, sagging a little with their movement.
	local r = math.max(actor.Radius, 10);
	local top = actor.Pos + Vector(0, -r - 2);
	local sway = math.sin(self.Age * 0.008) * 1.5;
	for k = -3, 3 do
		local x = k * (r / 3);
		PrimitiveMan:DrawLinePrimitive(top + Vector(x + sway, 0), actor.Pos + Vector(x * 1.3 - sway, r + 4), k % 2 == 0 and cordLight or cordDark);
	end
	for k = 0, 4 do
		local y = -r - 2 + k * (r * 2 + 6) / 4;
		local w = r * (0.9 + 0.1 * k);
		PrimitiveMan:DrawLinePrimitive(actor.Pos + Vector(-w + sway, y), actor.Pos + Vector(w + sway, y + 1), k % 2 == 0 and cordDark or cordLight);
	end
end
