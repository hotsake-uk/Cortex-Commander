-- Summon Wolf's mist (made by SpellCast.lua on the ground in front of the caster): grey-green mist gathers, a howl, and two Wolves
-- (Actors/Wolf/Wolf.ini) step out of it one after the other on the caster's side, glowing with the "Medieval Spectral" shader. A caster
-- has at most four summoned at once (counted by the NumberValue MedievalSummonerUID they carry, which Raise Dead's skeletons share, so a
-- caster's whole menagerie counts together); past that the mist gathers and nothing comes. Each wolf fades back into mist thirty seconds
-- after it came; this controller stays (unseen) until then to see to it.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local OPEN_TIME = 2200;
local RISE_TIMES = {700, 1300};
local LIFESPAN = 30000;
local CAP = 4;
local SPECTRAL = "Medieval Spectral";

local function countSummoned(casterUID, team)
	local count = 0;
	for actor in MovableMan.Actors do
		if actor:NumberValueExists("MedievalSummonerUID") and actor.Status < Actor.DYING then
			if (casterUID ~= 0 and actor:GetNumberValue("MedievalSummonerUID") == casterUID) or (casterUID == 0 and actor.Team == team) then
				count = count + 1;
			end
		end
	end
	return count;
end

local function summon(self)
	local wolf = CreateACrab("Wolf", "Medieval.rte");
	if not wolf then
		return;
	end
	local side = #self.summoned == 0 and -1 or 1;
	local x = self.Pos.X + side * FX.Rand(8, 18);
	local ground, found = FX.GroundBelow(Vector(x, self.Pos.Y - 30), 60);
	wolf.Pos = Vector(x, (found and ground.Y or self.Pos.Y) - 14);
	wolf.Vel = Vector(self.dir * 2, -2);
	wolf.Team = self.Team;
	wolf.HFlipped = self.dir < 0;
	local caster = FX.ActorByUID(self.casterUID);
	if caster and caster:IsPlayerControlled() then
		wolf.AIMode = Actor.AIMODE_SENTRY;
	else
		wolf.AIMode = Actor.AIMODE_BRAINHUNT;
	end
	wolf.Shader = SPECTRAL;
	wolf:SetNumberValue("MedievalSummonerUID", self.casterUID);
	MovableMan:AddActor(wolf);
	self.summoned[#self.summoned + 1] = {uid = wolf.UniqueID, born = FX.Now(), done = false};
	EmitVisualParticles("Mist", wolf.Pos, Vector(0, -0.8), 1, 14, 0xB8C8C0);
	EmitVisualParticles("Sparks", wolf.Pos, Vector(0, -3), 0.7, 10, 0x9CE0B0);
	PostProcessMan:AddFog(wolf.Pos, 30, 0.4);
	FX.Flash("Green", wolf.Pos);
end

local function fade(wolf)
	EmitVisualParticles("Mist", wolf.Pos, Vector(0, -0.8), 1, 16, 0xB8C8C0);
	EmitVisualParticles("Sparks", wolf.Pos, Vector(0, -3), 1, 12, 0x9CE0B0);
	PostProcessMan:AddFog(wolf.Pos, 30, 0.4);
	wolf.ToDelete = true;
end

function Create(self)
	self.casterUID = self:GetNumberValue("CasterUID");
	self.dir = self:GetNumberValue("Dir");
	if self.dir == 0 then
		self.dir = 1;
	end
	self.start = FX.Now();
	self.frame = 0;
	self.summoned = {};
	self.toSummon = math.max(0, math.min(#RISE_TIMES, CAP - countSummoned(self.casterUID, self.Team)));
	self.next = 1;
	PostProcessMan:AddFog(self.Pos + Vector(0, -8), 40, 0.5);
end

function Update(self)
	local now = FX.Now();
	local t = now - self.start;
	self.frame = self.frame + 1;
	if t < OPEN_TIME then
		local strength = t > OPEN_TIME - 500 and (OPEN_TIME - t) / 500 or math.min(t / 300, 1);
		PostProcessMan:AddLight(self.Pos + Vector(0, -10), 90, 120, 200, 150, 0.6 * strength);
		if self.frame % 3 == 0 then
			EmitVisualParticles("Mist", self.Pos + Vector(FX.Rand(-24, 24), -FX.Rand(0, 10)), Vector(self.dir * 0.3, -0.5), 0.6, 1, 0xB8C8C0);
		end
		if self.frame % 8 == 0 then
			PostProcessMan:AddFog(self.Pos + Vector(FX.Rand(-20, 20), -6), 36, 0.3 * strength);
		end
		while self.next <= self.toSummon and t >= RISE_TIMES[self.next] do
			summon(self);
			self.next = self.next + 1;
		end
	end
	local waiting = false;
	for _, entry in ipairs(self.summoned) do
		if not entry.done then
			local wolf = FX.ActorByUID(entry.uid);
			if not wolf then
				entry.done = true;
			elseif now - entry.born >= LIFESPAN then
				entry.done = true;
				fade(wolf);
			else
				waiting = true;
			end
		end
	end
	if t >= OPEN_TIME and not waiting and self.next > self.toSummon then
		self.ToDelete = true;
	end
end
