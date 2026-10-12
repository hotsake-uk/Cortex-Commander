-- Soul Harvest (made by FellCast.lua on the caster, who is Harvesting, FellEffects.lua): for ten seconds this keeps with the caster and
-- watches every enemy within eight paces. When one dies, their soul, a green wisp, flies from where they fell to the caster over half a
-- second and gives him thirty energy and twenty health when it arrives. NumberValues: CasterUID.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local LIFE = 10000;
local RADIUS = 150;
local ENERGY = 30;
local HEALTH = 20;
local TRAVEL = 500;

function Create(self)
	self.casterUID = self:GetNumberValue("CasterUID");
	self.start = FX.Now();
	self.frame = 0;
	self.watched = {}; -- uid -> {pos, alive}
	self.wisps = {};
end

local function harvest(self, caster, from)
	self.wisps[#self.wisps + 1] = {from = Vector(from.X, from.Y), start = FX.Now()};
	EmitVisualParticles("Sparks", from, Vector(0, -4), 1, 12, 0x5AFF6E);
	EmitVisualParticles("Mist", from, Vector(0, -2), 1, 6, 0x206030);
	PostProcessMan:AddLight(from, 80, 90, 255, 110, 1.6);
	FX.Spawn("MOPixel", "Fell Spell Flash Hellfire", "FellLegion.rte", from, Vector());
	local sound = CreateSoundContainer("Fell Soul Wisp", "FellLegion.rte");
	if sound then
		sound:Play(from);
	end
end

function Update(self)
	local caster = FX.ActorByUID(self.casterUID);
	local now = FX.Now();
	local t = now - self.start;
	self.frame = self.frame + 1;
	if not caster or not FX.Alive(caster) or (t >= LIFE and #self.wisps == 0) then
		self.ToDelete = true;
		return;
	end
	self.Pos = caster.Pos;
	self.Vel = Vector();
	if t < LIFE then
		-- Who is near and alive now; whoever was and is no longer has died, and is harvested.
		local seen = {};
		for _, near in ipairs(FX.ActorsNear(caster.Pos, RADIUS, caster.Team)) do
			local a = near.actor;
			if a.Status < Actor.DYING then
				seen[a.UniqueID] = true;
				self.watched[a.UniqueID] = {pos = Vector(a.Pos.X, a.Pos.Y)};
			end
		end
		for uid, entry in pairs(self.watched) do
			if not seen[uid] then
				local a = FX.ActorByUID(uid);
				-- Gone, dying or dead on the spot: a harvest. Only walked out of reach (still alive): forgotten.
				if not a or a.Status >= Actor.DYING or a.Health <= 0 then
					if not a or SceneMan:ShortestDistance(caster.Pos, a.Pos, true):MagnitudeIsLessThan(RADIUS + 30) then
						harvest(self, caster, a and a.Pos or entry.pos);
					end
				end
				self.watched[uid] = nil;
			end
		end
		-- The harvest's look on the caster: green motes rising, a faint ring at the reach.
		if self.frame % 3 == 0 then
			EmitVisualParticles("Embers", caster.Pos + Vector(FX.Rand(-10, 10), FX.Rand(-20, 4)), Vector(0, -1.5), 0.4, 1, 0x5AFF6E);
		end
		if self.frame % 6 == 0 then
			local angle = math.random() * math.pi * 2;
			local dir = Vector(math.cos(angle), math.sin(angle));
			EmitVisualParticles("Sparks", caster.Pos + dir * RADIUS, Vector(-dir.Y, dir.X) * 1.5, 0.1, 1, 0x3AC050);
		end
	end
	-- The wisps on their way.
	local to = caster.Pos + Vector(0, -6);
	for i = #self.wisps, 1, -1 do
		local w = self.wisps[i];
		local f = math.min((now - w.start) / TRAVEL, 1);
		local offset = SceneMan:ShortestDistance(w.from, to, true);
		local at = w.from + offset * f + Vector(0, -30 * math.sin(f * math.pi));
		PostProcessMan:AddEnergyBeam(w.from, at, 2, 90, 255, 110, 1.0 * (1 - f) + 0.3, 30);
		PostProcessMan:AddLight(at, 50, 90, 255, 110, 1.2);
		EmitVisualParticles("Mist", at, Vector(0, -1), 0.6, 1, 0x3AC050);
		if f >= 1 then
			caster.Energy = math.min(caster.Energy + ENERGY, caster.MaxEnergy);
			caster.Health = math.min(caster.Health + HEALTH, caster.MaxHealth);
			FX.Ring("Sparks", to, 12, 4, 0x5AFF6E, 6);
			PostProcessMan:AddLight(to, 100, 90, 255, 110, 2);
			caster:Say("Harvested");
			table.remove(self.wisps, i);
		end
	end
end
