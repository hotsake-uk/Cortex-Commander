-- A thread of stolen life (the Shadow Bolt's, the Cursed Greatsword's cousin): from where a unit was struck, a green-violet beam runs back
-- to the caster over a third of a second and heals them a little when it arrives. NumberValues: CasterUID, Heal.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local TRAVEL = 350;

function Create(self)
	self.start = FX.Now();
	self.from = Vector(self.Pos.X, self.Pos.Y);
	self.heal = self:NumberValueExists("Heal") and self:GetNumberValue("Heal") or 8;
	self.done = false;
end

function Update(self)
	local caster = FX.ActorByUID(self:GetNumberValue("CasterUID"));
	if not caster or not FX.Alive(caster) then
		self.ToDelete = true;
		return;
	end
	local t = math.min((FX.Now() - self.start) / TRAVEL, 1);
	local to = caster.Pos + Vector(0, -4);
	local offset = SceneMan:ShortestDistance(self.from, to, true);
	local at = self.from + offset * t;
	PostProcessMan:AddEnergyBeam(self.from, at, 2, 150, 80, 230, 1.2 * (1 - t) + 0.3, 30);
	PostProcessMan:AddLight(at, 40, 150, 80, 230, 1.0);
	EmitVisualParticles("Mist", at, Vector(0, -1), 0.6, 1, 0x7A40B0);
	if t >= 1 and not self.done then
		self.done = true;
		caster.Health = math.min(caster.Health + self.heal, caster.MaxHealth);
		EmitVisualParticles("Sparks", to, Vector(0, -3), 1, 8, 0x90FF90);
		local sound = CreateSoundContainer("Fell Life Drain", "FellLegion.rte");
		if sound then
			sound:Play(to);
		end
		self.ToDelete = true;
	end
end
