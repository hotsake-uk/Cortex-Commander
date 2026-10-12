-- Holy Smite's pillar (made by SpellCast.lua where the aim strikes): a gold rune circle glows on the ground half a second, then a pillar
-- of white-gold light comes down out of the sky on it: everything within 50 pixels is hurt and thrown (the caster's side spared), the
-- undead twice as hard (a Skeleton, anything in "Actors - Undead", anything carrying the NumberValue MedievalUndead), dust blown out,
-- a shockwave, the field lit like noon for a moment. The pillar fades over the next half second.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local CHARGE = 500;
local LIFE = 1400;
local RADIUS = 50;
local DAMAGE = 45;

local function isUndead(actor)
	if actor:IsInGroup("Actors - Undead") or actor:NumberValueExists("MedievalUndead") then
		return true;
	end
	return string.find(actor.PresetName, "Skeleton") ~= nil or string.find(actor.PresetName, "Ghoul") ~= nil;
end

function Create(self)
	self.start = FX.Now();
	self.struck = false;
	self.sky = Vector(self.Pos.X, self.Pos.Y - 600);
	FX.Spawn("MOSParticle", "Medieval Spell Rune Circle Gold", FX.Module, self.Pos + Vector(0, -3), Vector());
	FX.PlaySound("Medieval Spell Holy Smite", self.Pos);
end

local function smite(self)
	self.struck = true;
	for _, hit in ipairs(FX.HurtRadius(self.Pos, RADIUS, DAMAGE, self.Team, 7, 250)) do
		if FX.Alive(hit.actor) and isUndead(hit.actor) then
			local share = 1 - 0.75 * math.min(hit.distance / RADIUS, 1);
			FX.Hurt(hit.actor, DAMAGE * share, 300);
			EmitVisualParticles("Sparks", hit.actor.Pos, Vector(0, -6), 1, 20, 0xFFF4C8);
		end
	end
	EmitVisualParticles("Dust", self.Pos, Vector(0, -3), 1, 24, 0xE8DCC0);
	EmitVisualParticles("Sparks", self.Pos, Vector(0, -10), 1, 40, 0xFFF0B0);
	EmitVisualParticles("Sparks", self.Pos, Vector(0, -4), 1, 20, 0xFFFFFF);
	FX.Ring("Sparks", self.Pos, 24, 14, 0xFFE68C, 8);
	FX.Flash("Gold", self.Pos + Vector(0, -20));
	FX.Flash("White", self.Pos);
	PostProcessMan:AddShockwave(self.Pos, 9000);
	SceneMan:AddAirBlast(self.Pos, 2500);
	CameraMan:AddScreenShake(6, self.Pos);
	-- Fires in the light go out (holy, not hot).
	for _, near in ipairs(FX.ActorsNear(self.Pos, RADIUS, -1)) do
		SceneMan:Extinguish(near.actor);
	end
end

function Update(self)
	local t = FX.Now() - self.start;
	if t >= LIFE then
		self.ToDelete = true;
		return;
	end
	if t < CHARGE then
		-- The circle gathering light: motes rising, a thin thread down from the sky brightening.
		local charge = t / CHARGE;
		EmitVisualParticles("Sparks", self.Pos + Vector(FX.Rand(-18, 18), -2), Vector(0, -3), 0.4, 1, 0xFFE68C);
		PostProcessMan:AddEnergyBeam(self.sky, self.Pos, 1 + 2 * charge, 255, 240, 190, 0.4 + 0.6 * charge, 30);
		PostProcessMan:AddLight(self.Pos, 80 + 80 * charge, 255, 230, 140, 0.6 + charge);
		return;
	end
	if not self.struck then
		smite(self);
	end
	-- The pillar, fading.
	local fade = 1 - (t - CHARGE) / (LIFE - CHARGE);
	PostProcessMan:AddEnergyBeam(self.sky, self.Pos, 14 * fade + 2, 255, 245, 210, 2.2 * fade, 120);
	PostProcessMan:AddEnergyBeam(self.sky, self.Pos, 4 * fade + 1, 255, 255, 255, 2.5 * fade, 60);
	PostProcessMan:AddLight(self.Pos + Vector(0, -30), 300 * fade + 60, 255, 240, 190, 2.5 * fade);
	PostProcessMan:AddShimmer(self.Pos + Vector(0, -20), 30 * fade, 0.5);
	if math.random() < 0.6 then
		EmitVisualParticles("Sparks", self.Pos + Vector(FX.Rand(-10, 10), -FX.Rand(0, 120)), Vector(0, -2), 0.5, 1, 0xFFF8E0);
	end
end
