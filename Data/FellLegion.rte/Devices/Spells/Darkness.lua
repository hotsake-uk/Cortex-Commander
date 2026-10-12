-- Darkness (made by FellCast.lua at the caster): night falls on the whole field for eight seconds. The screen goes to the "Fell Darkness"
-- post shader (Shaders/Darkness.frag: the scene drained to blue-black, only the brightest lights left burning), and every enemy unit sees
-- only half as far (Actor.Perceptiveness halved; what it was is kept on the unit in FellDarkPerceptiveness and put back when the last
-- darkness lifts). It runs on its own so the night ends even if the caster dies, and in the master Lua state (ForceIntoMasterLuaState) so
-- overlapping darknesses share one count: the screen's own post pass is saved when the first falls and put back when the last lifts.
-- NumberValues: CasterUID.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local LIFE = 8000;
local SHADER = "Fell Darkness";
local SIGHT = 0.5;
local KEY = "FellDarkPerceptiveness";

FellDarkness = FellDarkness or {count = 0, saved = ""};

-- Every enemy of the team not yet in the dark: their sight halved, what it was remembered.
local function darken(team)
	for actor in MovableMan.Actors do
		if actor.Team ~= team and not actor:NumberValueExists(KEY) then
			actor:SetNumberValue(KEY, actor.Perceptiveness);
			actor.Perceptiveness = actor.Perceptiveness * SIGHT;
		end
	end
end

-- Everyone's sight back, when the last darkness lifts.
local function lighten()
	for actor in MovableMan.Actors do
		if actor:NumberValueExists(KEY) then
			actor.Perceptiveness = actor:GetNumberValue(KEY);
			actor:RemoveNumberValue(KEY);
		end
	end
end

local function finish(self)
	if self.finished then
		return;
	end
	self.finished = true;
	if self.holdsScreen then
		self.holdsScreen = false;
		FellDarkness.count = math.max(FellDarkness.count - 1, 0);
		if FellDarkness.count == 0 then
			if PostProcessMan.PostShader == SHADER then
				PostProcessMan:SetPostShader(FellDarkness.saved or "");
			end
			lighten();
		end
	end
	self.ToDelete = true;
end

function Create(self)
	self.start = FX.Now();
	self.frame = 0;
	self.centre = Vector(self.Pos.X, self.Pos.Y);
	if FellDarkness.count <= 0 or PostProcessMan.PostShader ~= SHADER then
		local current = PostProcessMan.PostShader;
		FellDarkness.saved = current ~= SHADER and current or "";
		FellDarkness.count = 0;
	end
	FellDarkness.count = FellDarkness.count + 1;
	self.holdsScreen = true;
	PostProcessMan:SetPostShader(SHADER);
	darken(self.Team);
	PostProcessMan:AddShockwave(self.centre, 8000);
	-- The night pouring out of the caster: black mist and a few violet motes.
	EmitVisualParticles("Mist", self.centre, Vector(0, -2), 1, 20, 0x080410);
	EmitVisualParticles("Smoke", self.centre + Vector(0, -10), Vector(0, -3), 1, 8, 0x100820);
	FX.Ring("Sparks", self.centre, 20, 7, 0x4030A0, 10);
	CameraMan:AddScreenShake(2, self.centre);
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
	if PostProcessMan.PostShader == "" then
		-- (Something else cleared it; night again while the darkness lasts.)
		PostProcessMan:SetPostShader(SHADER);
	end
	-- Enemies that have since come onto the field go into the dark too.
	if self.frame % 30 == 0 then
		darken(self.Team);
	end
	-- Dark motes drifting over the field near where it fell.
	if self.frame % 2 == 0 then
		EmitVisualParticles("Mist", self.centre + Vector(FX.Rand(-200, 200), FX.Rand(-120, 40)), Vector(0, -0.4), 0.4, 1, 0x100820);
	end
	if self.frame % 5 == 0 then
		EmitVisualParticles("Sparks", self.centre + Vector(FX.Rand(-160, 160), FX.Rand(-100, 20)), Vector(0, -0.5), 0.2, 1, 0x4030A0);
	end
end

function Destroy(self)
	pcall(finish, self);
end
