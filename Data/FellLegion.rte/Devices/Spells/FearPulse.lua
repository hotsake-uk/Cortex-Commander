-- Fear (made by FellCast.lua at the caster): a ring of dark light bursts out to six paces, and every enemy it passes over is Afraid
-- (FellEffects.lua) for four seconds: shoved away from the caster, kept running from them while it lasts, and too shaken to hit anything
-- properly. (The engine has no way to turn a unit's own AI to flight, so the fear is the shove and the effect.) NumberValues: CasterUID.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");
local Fell = dofile("FellLegion.rte/Devices/Spells/FellEffects.lua");

local RADIUS = 150;
local BURST = 450;
local LIFE = 4500;
local SHOVE = 7;
local PUSH = 0.35;

function Create(self)
	self.start = FX.Now();
	self.frame = 0;
	self.centre = Vector(self.Pos.X, self.Pos.Y);
	self.afraid = {};
	self.caught = {};
	local sound = CreateSoundContainer("Fell Fear Pulse", "FellLegion.rte");
	if sound then
		sound:Play(self.centre);
	end
	PostProcessMan:AddShockwave(self.centre, 4000);
	FX.Spawn("MOPixel", "Fell Spell Flash Shadow", "FellLegion.rte", self.centre, Vector());
	CameraMan:AddScreenShake(4, self.centre);
end

local function frighten(self, actor, offset)
	if self.caught[actor.UniqueID] then
		return;
	end
	self.caught[actor.UniqueID] = true;
	if not Fell.Apply(actor, "Afraid") then
		return;
	end
	local away = offset.Magnitude > 0.5 and offset.Normalized or Vector(1, 0);
	actor.Vel = actor.Vel + Vector(away.X * SHOVE, -3);
	actor:FlashWhite(80);
	FX.Say(actor, "Afraid");
	self.afraid[#self.afraid + 1] = actor.UniqueID;
	EmitVisualParticles("Mist", actor.Pos, Vector(0, -2), 0.8, 6, 0x301848);
	EmitVisualParticles("Sparks", actor.Pos + Vector(0, -10), Vector(0, -3), 0.6, 6, 0x9050C0);
end

function Update(self)
	local t = FX.Now() - self.start;
	self.frame = self.frame + 1;
	if t >= LIFE then
		self.ToDelete = true;
		return;
	end
	if t < BURST then
		-- The ring, running out from the caster.
		local r = RADIUS * (t / BURST);
		for _, near in ipairs(FX.ActorsNear(self.centre, r, self.Team)) do
			if FX.Alive(near.actor) then
				frighten(self, near.actor, near.offset);
			end
		end
		for i = 0, 15 do
			local angle = i * math.pi / 8 + t * 0.004;
			local dir = Vector(math.cos(angle), math.sin(angle));
			EmitVisualParticles("Mist", self.centre + dir * r, dir * 3, 0.4, 1, 0x301848);
		end
		if self.frame % 2 == 0 then
			local angle = math.random() * math.pi * 2;
			local dir = Vector(math.cos(angle), math.sin(angle));
			EmitVisualParticles("Sparks", self.centre + dir * r, dir * 4, 0.6, 1, 0x9050C0);
		end
		PostProcessMan:AddLight(self.centre, r + 40, 90, 40, 120, 1.4 * (1 - t / BURST));
		PostProcessMan:AddShimmer(self.centre, r, 0.6);
	end
	-- The frightened keep running: a steady push away from where the fear came, while the effect holds.
	for _, uid in ipairs(self.afraid) do
		local actor = FX.ActorByUID(uid);
		if actor and FX.Alive(actor) and actor:HasStatusEffect("Afraid") then
			local offset = SceneMan:ShortestDistance(self.centre, actor.Pos, true);
			if offset.Magnitude > 0.5 and offset.Magnitude < 400 then
				local away = offset.Normalized;
				actor.Vel = actor.Vel + Vector(away.X * PUSH, 0);
				if actor.Vel.X * away.X < 0 then
					actor.Vel = Vector(actor.Vel.X * 0.9, actor.Vel.Y);
				end
			end
			if self.frame % 5 == 0 then
				EmitVisualParticles("Mist", actor.Pos + Vector(0, -14), Vector(0, -1), 0.4, 1, 0x301848);
			end
		end
	end
end
