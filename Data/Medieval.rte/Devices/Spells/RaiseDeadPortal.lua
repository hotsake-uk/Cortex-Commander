-- Raise Dead's portal: a green rune circle glows on the ground, necrotic fog gathers, motes rise, and two Skeletons (Medieval.rte's
-- "Skeleton") claw up out of the ground one after the other, on the caster's side and glowing with the "Medieval Spectral" shader. A caster
-- has at most four raised at once (counted by the NumberValue MedievalSummonerUID the skeletons carry); past that the portal opens and
-- raises none. Each skeleton crumbles to bones thirty seconds after it rose; this controller stays (unseen) until then to see to it.
-- A skeleton raised for a unit the player controls stands guard where it rose (sentry); one raised by the AI goes hunting.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local OPEN_TIME = 2600;
local RISE_TIMES = {800, 1450};
local LIFESPAN = 30000;
local CAP = 4;
local SPECTRAL = "Medieval Spectral";

local function countRaised(casterUID, team)
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

local function raise(self)
	local skeleton = CreateAHuman("Skeleton", "Medieval.rte");
	if not skeleton then
		return;
	end
	local x = self.Pos.X + FX.Rand(-14, 14);
	local ground, found = FX.GroundBelow(Vector(x, self.Pos.Y - 30), 60);
	local y = (found and ground.Y or self.Pos.Y) - 18;
	skeleton.Pos = Vector(x, y);
	skeleton.Vel = Vector(0, -3);
	skeleton.Team = self.Team;
	skeleton.HFlipped = math.random() < 0.5;
	local caster = FX.ActorByUID(self.casterUID);
	if caster and caster:IsPlayerControlled() then
		skeleton.AIMode = Actor.AIMODE_SENTRY;
	else
		skeleton.AIMode = Actor.AIMODE_BRAINHUNT;
	end
	skeleton.Shader = SPECTRAL;
	skeleton:SetNumberValue("MedievalSummonerUID", self.casterUID);
	MovableMan:AddActor(skeleton);
	self.raised[#self.raised + 1] = {uid = skeleton.UniqueID, born = FX.Now(), greeted = false, warned = false};
	-- Clawing out of the ground.
	local at = Vector(x, found and ground.Y or self.Pos.Y);
	EmitVisualParticles("Debris", at, Vector(0, -9), 0.6, 16, 0x5A4630);
	EmitVisualParticles("Dust", at, Vector(0, -3), 0.8, 12, 0x7A6A50);
	EmitVisualParticles("Sparks", at, Vector(0, -6), 0.7, 14, 0x60FF70);
	PostProcessMan:AddFog(at + Vector(0, -8), 30, 0.4);
	FX.Flash("Green", at + Vector(0, -10));
end

local function crumble(skeleton)
	EmitVisualParticles("Dust", skeleton.Pos, Vector(0, -1), 1, 20, 0xC8C0A8);
	EmitVisualParticles("Sparks", skeleton.Pos, Vector(0, -3), 1, 16, 0x60FF70);
	EmitVisualParticles("Mist", skeleton.Pos, Vector(0, -1), 1, 8, 0x60FF70);
	skeleton:GibThis();
end

function Create(self)
	self.casterUID = self:GetNumberValue("CasterUID");
	self.start = FX.Now();
	self.frame = 0;
	self.raised = {};
	self.toRaise = math.max(0, math.min(#RISE_TIMES, CAP - countRaised(self.casterUID, self.Team)));
	self.next = 1;
	self.circle = FX.Spawn("MOSParticle", "Medieval Spell Rune Circle Green", FX.Module, self.Pos + Vector(0, -3), Vector());
	FX.PlaySound("Medieval Spell Summon", self.Pos);
	PostProcessMan:AddShockwave(self.Pos, 3000);
end

function Update(self)
	local now = FX.Now();
	local t = now - self.start;
	self.frame = self.frame + 1;
	if t < OPEN_TIME then
		local fade = t > OPEN_TIME - 500 and (OPEN_TIME - t) / 500 or math.min(t / 300, 1);
		PostProcessMan:AddLight(self.Pos + Vector(0, -10), 110, 80, 255, 110, (0.8 + 0.3 * math.sin(t * 0.01)) * fade);
		PostProcessMan:AddShimmer(self.Pos + Vector(0, -8), 26 * fade, 0.5);
		if self.frame % 8 == 0 then
			PostProcessMan:AddFog(self.Pos + Vector(FX.Rand(-16, 16), -6), 34, 0.3 * fade);
		end
		EmitVisualParticles("Embers", self.Pos + Vector(FX.Rand(-20, 20), -2), Vector(0, -2.5), 0.3, 1, 0x60FF70);
		if self.frame % 3 == 0 then
			EmitVisualParticles("Mist", self.Pos + Vector(FX.Rand(-18, 18), -4), Vector(0, -0.6), 0.5, 1, 0x40C050);
		end
		while self.next <= self.toRaise and t >= RISE_TIMES[self.next] do
			raise(self);
			self.next = self.next + 1;
		end
	end
	-- The raised: a word on rising, a last word before the end, and crumbling to dust at thirty seconds.
	local waiting = false;
	for _, entry in ipairs(self.raised) do
		if not entry.done then
			local skeleton = FX.ActorByUID(entry.uid);
			if not skeleton then
				entry.done = true;
			else
				local age = now - entry.born;
				if not entry.greeted and age > 350 then
					entry.greeted = true;
					skeleton:Say("SkeletonRise");
				end
				if not entry.warned and age > LIFESPAN - 1500 then
					entry.warned = true;
					skeleton:Say("Crumble");
				end
				if age >= LIFESPAN then
					entry.done = true;
					crumble(skeleton);
				else
					waiting = true;
				end
			end
		end
	end
	if t >= OPEN_TIME and not waiting and self.next > self.toRaise then
		self.ToDelete = true;
	end
end
