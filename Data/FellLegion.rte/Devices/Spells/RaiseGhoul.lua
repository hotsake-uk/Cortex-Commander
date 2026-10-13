-- Raise Ghoul's grave (made by FellCast.lua where the aim strikes the ground): a hellish rune circle glows on the ground, grave dirt
-- heaves, and a Ghoul (Actors.ini) claws up out of the earth on the caster's side. A caster has at most three ghouls at once (counted by
-- the NumberValue FellSummonerUID the ghouls carry); past that the grave opens and nothing rises. Each ghoul rots back into the ground
-- forty seconds after it rose; this controller stays (unseen) until then to see to it. A ghoul raised for a unit the player controls
-- stands guard where it rose (sentry); one raised by the AI goes hunting. Built as Medieval.rte's RaiseDeadPortal.lua is.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");
local MODULE = "FellLegion.rte";

local OPEN_TIME = 2200;
local RISE_TIME = 900;
local LIFESPAN = 40000;
local CAP = 3;

local function countRaised(casterUID, team)
	local count = 0;
	for actor in MovableMan.Actors do
		if actor:NumberValueExists("FellSummonerUID") and actor.Status < Actor.DYING then
			if (casterUID ~= 0 and actor:GetNumberValue("FellSummonerUID") == casterUID) or (casterUID == 0 and actor.Team == team) then
				count = count + 1;
			end
		end
	end
	return count;
end

local function raise(self)
	local ghoul = CreateAHuman("Ghoul", MODULE);
	if not ghoul then
		return;
	end
	local x = self.Pos.X + FX.Rand(-8, 8);
	local ground, found = FX.GroundBelow(Vector(x, self.Pos.Y - 30), 60);
	local y = (found and ground.Y or self.Pos.Y) - 18;
	ghoul.Pos = Vector(x, y);
	ghoul.Vel = Vector(0, -4);
	ghoul.Team = self.Team;
	ghoul.HFlipped = math.random() < 0.5;
	local caster = FX.ActorByUID(self.casterUID);
	if caster and caster:IsPlayerControlled() then
		ghoul.AIMode = Actor.AIMODE_SENTRY;
	else
		ghoul.AIMode = Actor.AIMODE_BRAINHUNT;
	end
	ghoul:SetNumberValue("FellSummonerUID", self.casterUID);
	-- Its time as its own lifetime too, a little past this grave's rotting of it: a saved game keeps that (not this grave), so a ghoul
	-- loaded still rots when its time is up.
	ghoul.Lifetime = LIFESPAN + 1000;
	ghoul.GibAtEndOfLifetime = true;
	MovableMan:AddActor(ghoul);
	self.raised = {uid = ghoul.UniqueID, born = FX.Now(), greeted = false, warned = false};
	-- Clawing out of the grave.
	local at = Vector(x, found and ground.Y or self.Pos.Y);
	EmitVisualParticles("Debris", at, Vector(0, -9), 0.6, 18, 0x3A2A20);
	EmitVisualParticles("Dust", at, Vector(0, -3), 0.8, 14, 0x5A4A38);
	EmitVisualParticles("Sparks", at, Vector(0, -6), 0.7, 12, 0x6EFF6E);
	PostProcessMan:AddFog(at + Vector(0, -8), 30, 0.5);
	FX.Flash("Green", at + Vector(0, -10));
	local sound = CreateSoundContainer("Fell Ghoul Rise", MODULE);
	if sound then
		sound:Play(at);
	end
	CameraMan:AddScreenShake(3, at);
end

local function rot(ghoul)
	EmitVisualParticles("Dust", ghoul.Pos, Vector(0, -1), 1, 16, 0x5A4A38);
	EmitVisualParticles("Mist", ghoul.Pos, Vector(0, -1), 1, 10, 0x3A7040);
	EmitVisualParticles("Sparks", ghoul.Pos, Vector(0, -3), 1, 12, 0x6EFF6E);
	ghoul:GibThis();
end

function Create(self)
	self.casterUID = self:GetNumberValue("CasterUID");
	self.start = FX.Now();
	self.frame = 0;
	self.toRaise = countRaised(self.casterUID, self.Team) < CAP;
	self.risen = false;
	self.circle = FX.Spawn("MOSParticle", "Fell Spell Rune Circle Hell", MODULE, self.Pos + Vector(0, -3), Vector());
	FX.PlaySound("Medieval Spell Summon", self.Pos);
	PostProcessMan:AddShockwave(self.Pos, 2500);
end

function Update(self)
	local now = FX.Now();
	local t = now - self.start;
	self.frame = self.frame + 1;
	if t < OPEN_TIME then
		local fade = t > OPEN_TIME - 500 and (OPEN_TIME - t) / 500 or math.min(t / 300, 1);
		PostProcessMan:AddLight(self.Pos + Vector(0, -10), 100, 110, 255, 110, (0.8 + 0.3 * math.sin(t * 0.01)) * fade);
		PostProcessMan:AddShimmer(self.Pos + Vector(0, -8), 24 * fade, 0.5);
		if self.frame % 8 == 0 then
			PostProcessMan:AddFog(self.Pos + Vector(FX.Rand(-14, 14), -6), 30, 0.3 * fade);
		end
		-- The grave dirt heaving before the rise.
		if not self.risen and self.toRaise and self.frame % 3 == 0 then
			EmitVisualParticles("Debris", self.Pos + Vector(FX.Rand(-10, 10), 0), Vector(0, -3), 0.3, 1, 0x3A2A20);
		end
		EmitVisualParticles("Embers", self.Pos + Vector(FX.Rand(-18, 18), -2), Vector(0, -2.5), 0.3, 1, 0x6EFF6E);
		if not self.risen and self.toRaise and t >= RISE_TIME then
			self.risen = true;
			raise(self);
		end
	end
	-- The raised: a word on rising, a last word before the end, and rotting back into the ground at forty seconds.
	local waiting = false;
	local entry = self.raised;
	if entry and not entry.done then
		local ghoul = FX.ActorByUID(entry.uid);
		if not ghoul then
			entry.done = true;
		else
			local age = now - entry.born;
			if not entry.greeted and age > 350 then
				entry.greeted = true;
				ghoul:Say("GhoulRise");
			end
			if not entry.warned and age > LIFESPAN - 1500 then
				entry.warned = true;
				ghoul:Say("Crumble");
			end
			if age >= LIFESPAN then
				entry.done = true;
				rot(ghoul);
			else
				waiting = true;
			end
		end
	end
	if t >= OPEN_TIME and not waiting then
		self.ToDelete = true;
	end
end
