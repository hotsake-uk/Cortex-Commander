-- Curse of Weakness (made by FellCast.lua where the aim strikes): a dark violet sigil opens on the spot and every enemy within four paces
-- is Cursed (FellEffects.lua): half again the damage taken and a fifth slower for ten seconds. The sigil smoulders a second and a half.
-- NumberValues: CasterUID.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");
local Fell = dofile("FellLegion.rte/Devices/Spells/FellEffects.lua");

local RADIUS = 90;
local LIFE = 1500;
local VIOLET, DARK_VIOLET = 227, 208; -- Palette indices (a dusty mauve and a deep blue) for the sigil's lines.

function Create(self)
	self.start = FX.Now();
	self.frame = 0;
	self.centre = Vector(self.Pos.X, self.Pos.Y);
	local cursed = 0;
	for _, near in ipairs(FX.ActorsNear(self.centre, RADIUS, self.Team)) do
		if FX.Alive(near.actor) and Fell.Apply(near.actor, "Cursed") then
			cursed = cursed + 1;
			FX.Say(near.actor, "Cursed");
			PostProcessMan:AddEnergyBeam(self.centre, near.actor.Pos, 2, 120, 30, 160, 1.2, 40);
			FX.Ring("Sparks", near.actor.Pos, 12, 4, 0x8020C0, 8);
		end
	end
	self.cursed = cursed;
	local sound = CreateSoundContainer("Fell Curse Whisper", "FellLegion.rte");
	if sound then
		sound:Play(self.centre);
	end
	PostProcessMan:AddShockwave(self.centre, 2500);
	FX.Spawn("MOPixel", "Fell Spell Flash Shadow", "FellLegion.rte", self.centre, Vector());
	EmitVisualParticles("Mist", self.centre, Vector(0, -2), 1, 14, 0x40186A);
	EmitVisualParticles("Sparks", self.centre, Vector(0, -4), 1, 16, 0x8020C0);
end

function Update(self)
	local t = FX.Now() - self.start;
	self.frame = self.frame + 1;
	if t >= LIFE then
		self.ToDelete = true;
		return;
	end
	local fade = 1 - t / LIFE;
	-- The sigil: a circle and a five-pointed star inside it, turning slowly.
	PrimitiveMan:DrawCirclePrimitive(self.centre, RADIUS * 0.4, VIOLET);
	PrimitiveMan:DrawCirclePrimitive(self.centre, RADIUS * 0.4 - 3, DARK_VIOLET);
	local spin = t * 0.001;
	for i = 0, 4 do
		local a = spin + i * math.pi * 4 / 5;
		local b = spin + (i + 1) * math.pi * 4 / 5;
		local r = RADIUS * 0.36;
		PrimitiveMan:DrawLinePrimitive(self.centre + Vector(math.cos(a), math.sin(a)) * r, self.centre + Vector(math.cos(b), math.sin(b)) * r, VIOLET);
	end
	PostProcessMan:AddLight(self.centre, 120, 120, 30, 160, (0.6 + 0.3 * math.sin(t * 0.02)) * fade);
	if self.frame % 2 == 0 then
		local angle = math.random() * math.pi * 2;
		local dir = Vector(math.cos(angle), math.sin(angle));
		EmitVisualParticles("Embers", self.centre + dir * FX.Rand(0, RADIUS * 0.4), Vector(0, -2), 0.3, 1, 0xA040FF);
	end
	if self.frame % 4 == 0 then
		EmitVisualParticles("Mist", self.centre + Vector(FX.Rand(-20, 20), -2), Vector(0, -0.5), 0.5, 1, 0x40186A);
	end
end
