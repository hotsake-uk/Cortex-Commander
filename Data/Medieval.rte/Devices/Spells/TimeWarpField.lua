-- Time Warp's field (made by SpellCast.lua at the caster): for five seconds every enemy unit and thrown thing within 250 pixels of where
-- it was cast moves in slow motion (its movement strongly damped every update), a golden clock face turns on the ground of time round it,
-- a ticking warble plays and the screen goes to the "Medieval Time Warp" post shader (sepia, rippling, darkened at the edges).
-- It runs on its own, not on the caster, so it ends and puts the screen back even if the caster dies. It runs in the master Lua state
-- (ForceIntoMasterLuaState) so that overlapping warps share one count: the screen's own post pass is saved when the first starts and put
-- back when the last ends.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local LIFE = 5000;
local RADIUS = 250;
local DAMP = 0.8;
local SHADER = "Medieval Time Warp";
local GOLD, PALE_GOLD, DARK_GOLD = 77, 118, 84; -- Palette indices for the clock's lines.

MedievalTimeWarp = MedievalTimeWarp or {count = 0, saved = ""};

local function finish(self)
	if self.finished then
		return;
	end
	self.finished = true;
	if self.holdsScreen then
		self.holdsScreen = false;
		MedievalTimeWarp.count = math.max(MedievalTimeWarp.count - 1, 0);
		if MedievalTimeWarp.count == 0 and PostProcessMan.PostShader == SHADER then
			PostProcessMan:SetPostShader(MedievalTimeWarp.saved or "");
		end
	end
	if self.sound then
		pcall(function() self.sound:Stop(); end);
	end
	self.ToDelete = true;
end

function Create(self)
	self.start = FX.Now();
	self.frame = 0;
	self.centre = Vector(self.Pos.X, self.Pos.Y);
	if MedievalTimeWarp.count <= 0 or PostProcessMan.PostShader ~= SHADER then
		local current = PostProcessMan.PostShader;
		MedievalTimeWarp.saved = current ~= SHADER and current or "";
		MedievalTimeWarp.count = 0;
	end
	MedievalTimeWarp.count = MedievalTimeWarp.count + 1;
	self.holdsScreen = true;
	PostProcessMan:SetPostShader(SHADER);
	self.sound = FX.PlaySound("Medieval Spell Time Warp", self.centre);
	PostProcessMan:AddShockwave(self.centre, 6000);
	FX.Flash("Gold", self.centre);
end

-- The clock: the ring at the edge of the field, twelve ticks, and two hands turning backward.
local function drawClock(self, t)
	PrimitiveMan:DrawCirclePrimitive(self.centre, RADIUS, GOLD);
	PrimitiveMan:DrawCirclePrimitive(self.centre, RADIUS - 6, DARK_GOLD);
	for i = 0, 11 do
		local angle = i * math.pi / 6 - t * 0.0002;
		local dir = Vector(math.cos(angle), math.sin(angle));
		PrimitiveMan:DrawLinePrimitive(self.centre + dir * (RADIUS - 16), self.centre + dir * (RADIUS - 6), i % 3 == 0 and PALE_GOLD or GOLD);
	end
	local minute = -t * 0.003 - math.pi / 2;
	local hour = -t * 0.0006 - math.pi / 2;
	PrimitiveMan:DrawLinePrimitive(self.centre, self.centre + Vector(math.cos(minute), math.sin(minute)) * 60, PALE_GOLD);
	PrimitiveMan:DrawLinePrimitive(self.centre, self.centre + Vector(math.cos(hour), math.sin(hour)) * 38, GOLD);
end

function Update(self)
	if self.finished then
		return;
	end
	local t = FX.Now() - self.start;
	self.frame = self.frame + 1;
	if t >= LIFE then
		finish(self);
		return;
	end
	if PostProcessMan.PostShader ~= SHADER and PostProcessMan.PostShader == "" then
		-- (Something else cleared it; put it back while the warp lasts.)
		PostProcessMan:SetPostShader(SHADER);
	end
	-- Slow time: every enemy thing in the field crawls.
	local sayNow = self.frame % 24 == 0;
	for mo in MovableMan:GetMOsInRadius(self.centre, RADIUS, self.Team) do
		if mo.ID == mo.RootID and mo.Team ~= self.Team and mo.PinStrength <= 0 then
			mo.Vel = mo.Vel * DAMP;
			mo.AngularVel = mo.AngularVel * DAMP;
			if sayNow and IsActor(mo) then
				ToActor(mo):Say("Slowed");
			end
		end
	end
	drawClock(self, t);
	PostProcessMan:AddLight(self.centre, 220, 255, 210, 120, 0.5 + 0.1 * math.sin(t * 0.006));
	-- Golden motes drifting round the rim.
	for _ = 1, 2 do
		local angle = math.random() * math.pi * 2;
		local dir = Vector(math.cos(angle), math.sin(angle));
		EmitVisualParticles("Sparks", self.centre + dir * (RADIUS - FX.Rand(0, 20)), Vector(-dir.Y, dir.X) * -2, 0.1, 1, 0xFFD070);
	end
	if self.frame % 3 == 0 then
		EmitVisualParticles("Embers", self.centre + Vector(FX.Rand(-RADIUS, RADIUS) * 0.7, FX.Rand(-RADIUS, RADIUS) * 0.5), Vector(0, -0.3), 0.5, 1, 0xFFE0A0);
	end
end

function Destroy(self)
	pcall(finish, self);
end
