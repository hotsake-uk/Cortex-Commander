-- Hellfire Rain's sigil (made by FellCast.lua on the ground under the aim): a green rune circle burns there, and for three seconds drops
-- of hellfire fall out of the sky onto it, a little scattered, each bursting where it lands (HellfireDrop.lua).

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");
local MODULE = "FellLegion.rte";

local DROPS = 7;
local RAIN_MS = 3000;
local FROM_ABOVE = 340;

function Create(self)
	self.start = FX.Now();
	self.dropped = 0;
	FX.Spawn("MOSParticle", "Fell Spell Rune Circle Hell", MODULE, self.Pos + Vector(0, -2), Vector());
	FX.Ring("Embers", self.Pos, 16, 3, 0x6EFF6E, 10);
	local sound = CreateSoundContainer("Medieval Spell Rune", "Medieval.rte");
	if sound then
		sound:Play(self.Pos);
	end
end

function Update(self)
	local t = FX.Now() - self.start;
	local pulse = 0.6 + 0.4 * math.sin(t * 0.012);
	PostProcessMan:AddLight(self.Pos + Vector(0, -6), 90, 90, 255, 90, pulse);
	if math.random() < 0.3 then
		EmitVisualParticles("Embers", self.Pos + Vector(FX.Rand(-26, 26), -2), Vector(0, -2), 0.3, 1, 0x6EFF6E);
	end
	-- The drops, spread over the rain's three seconds: each from high above, scattered a little either side.
	local due = math.min(DROPS, math.floor(t / (RAIN_MS / DROPS)) + 1);
	while self.dropped < due do
		self.dropped = self.dropped + 1;
		local x = FX.Rand(-50, 50);
		local from = Vector(self.Pos.X + x + FX.Rand(-20, 20), self.Pos.Y - FROM_ABOVE);
		local drop = FX.Spawn("MOSParticle", "Fell Spell Hellfire Drop", MODULE, from, Vector(-x / 25, 9), self.Team);
		if drop then
			drop:SetNumberValue("CasterUID", self:GetNumberValue("CasterUID"));
		end
		if self.dropped == 1 or self.dropped == 4 then
			local sound = CreateSoundContainer("Fell Hellfire Fall", MODULE);
			if sound then
				sound:Play(from + Vector(0, 100));
			end
		end
	end
	if t > RAIN_MS + 800 then
		self.ToDelete = true;
	end
end
