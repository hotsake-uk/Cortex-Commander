-- Chain Lightning's bolt (made by SpellCast.lua where the aim strikes): from the hand into the enemy struck, then leaping on to the nearest
-- enemy within 90 pixels not yet struck, and on again, five in all, each hit a little weaker than the last (28, 24, 20, 16, 12). The arcs
-- are drawn as jagged energy beams that flicker for a third of a second, each strike flashing blue-white; the caster's own side is spared.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local LIFE = 380;
local JUMP = 90;
local DAMAGE = {28, 24, 20, 16, 12};

local function nextLink(from, team, struck)
	local best, bestDist;
	for _, near in ipairs(FX.ActorsNear(from, JUMP, team)) do
		local a = near.actor;
		if FX.Alive(a) and not struck[a.UniqueID] and (not bestDist or near.distance < bestDist) then
			best, bestDist = a, near.distance;
		end
	end
	return best;
end

local function strike(self, actor, damage)
	FX.Hurt(actor, damage, 200);
	actor:FlashWhite(200);
	EmitVisualParticles("Sparks", actor.Pos, Vector(0, -4), 1, 16, 0xCFE4FF);
	EmitVisualParticles("Sparks", actor.Pos, Vector(0, -2), 1, 6, 0xFFFFFF);
	FX.Flash("White", actor.Pos);
	FX.Spawn("MOPixel", "Spark Blue 1", "Base.rte", actor.Pos, Vector(FX.Rand(-6, 6), FX.Rand(-8, -2)));
end

function Create(self)
	self.start = FX.Now();
	self.team = self.Team;
	self.hand = Vector(self:GetNumberValue("HandX"), self:GetNumberValue("HandY"));
	self.points = {Vector(self.hand.X, self.hand.Y)};
	self.jags = {};
	local struck = {};
	local first = FX.ActorByUID(self:GetNumberValue("StruckUID"));
	if not first or not FX.Alive(first) then
		-- Nothing under the aim: the bolt cracks into the ground and stops there.
		self.points[2] = Vector(self.Pos.X, self.Pos.Y);
		EmitVisualParticles("Sparks", self.Pos, Vector(0, -4), 1, 10, 0xCFE4FF);
		SceneMan:IgniteArea(self.Pos, 6);
	else
		local link = first;
		for i = 1, #DAMAGE do
			struck[link.UniqueID] = true;
			self.points[#self.points + 1] = Vector(link.Pos.X, link.Pos.Y);
			strike(self, link, DAMAGE[i]);
			link = nextLink(link.Pos, self.team, struck);
			if not link then
				break;
			end
		end
	end
	for i = 1, #self.points - 1 do
		local jag = {};
		for k = 1, 5 do
			jag[k] = Vector(FX.Rand(-9, 9), FX.Rand(-9, 9));
		end
		self.jags[i] = jag;
	end
	FX.PlaySound("Medieval Spell Chain Zap", self.points[2] or self.Pos);
	CameraMan:AddScreenShake(2, self.Pos);
end

local function drawArc(from, to, jag, brightness)
	local segments = 6;
	local last = from;
	for i = 1, segments do
		local s = i / segments;
		local at = from + (to - from) * s;
		if i < segments then
			at = at + jag[i];
		end
		PostProcessMan:AddEnergyBeam(last, at, 1.5, 200, 225, 255, brightness, 40);
		last = at;
	end
end

function Update(self)
	local t = FX.Now() - self.start;
	if t >= LIFE then
		self.ToDelete = true;
		return;
	end
	local on = t < 90 or (t > 140 and t < 200) or (t > 250 and t < 300);
	if on then
		local bright = t < 90 and 1.6 or 1.0;
		for i = 1, #self.points - 1 do
			drawArc(self.points[i], self.points[i + 1], self.jags[i], bright);
			PostProcessMan:AddLight(self.points[i + 1], 90, 190, 215, 255, bright);
		end
		-- The jags shift each flicker.
		if math.random() < 0.3 then
			for _, jag in ipairs(self.jags) do
				for k = 1, #jag do
					jag[k] = Vector(FX.Rand(-9, 9), FX.Rand(-9, 9));
				end
			end
		end
	end
end
