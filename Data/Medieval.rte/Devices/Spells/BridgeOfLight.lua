-- Bridge of Light's keeper (made by SpellCast.lua, which lays the "Medieval Spell Light Bridge" prop along the aim): the plank glows and
-- pulses, motes of light drift up off it, and after ten seconds it flickers and is gone. It is pinned where it was laid; the keeper holds it
-- there (and its angle) in case anything shoves it, and takes it away with itself.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local LIFE = 10000;
local FLICKER = 1500;

function Create(self)
	self.start = FX.Now();
	self.bridgeUID = self:GetNumberValue("BridgeUID");
	self.angle = self:GetNumberValue("Angle");
	self.at = Vector(self.Pos.X, self.Pos.Y);
	self.frame = 0;
	local bridge = MovableMan:FindObjectByUniqueID(self.bridgeUID);
	if bridge then
		bridge.Shader = "Medieval Illusion";
	end
end

function Update(self)
	local t = FX.Now() - self.start;
	self.frame = self.frame + 1;
	local bridge = MovableMan:FindObjectByUniqueID(self.bridgeUID);
	if not bridge or bridge.ToDelete then
		self.ToDelete = true;
		return;
	end
	if t >= LIFE then
		EmitVisualParticles("Sparks", self.at, Vector(0, -2), 1, 16, 0xFFF4C8);
		bridge.ToDelete = true;
		self.ToDelete = true;
		return;
	end
	bridge.Pos = self.at;
	bridge.Vel = Vector();
	bridge.RotAngle = self.angle;
	bridge.AngularVel = 0;
	local left = LIFE - t;
	local pulse = 0.8 + 0.2 * math.sin(t * 0.008);
	if left < FLICKER then
		pulse = pulse * (math.random() < left / FLICKER and 1 or 0.2);
	end
	PostProcessMan:AddLight(self.at, 130, 255, 240, 190, 0.9 * pulse);
	if self.frame % 4 == 0 then
		local along = Vector(math.cos(self.angle), -math.sin(self.angle)) * FX.Rand(-48, 48);
		EmitVisualParticles("Sparks", self.at + along, Vector(0, -1.5), 0.4, 1, 0xFFF8E0);
	end
end

function Destroy(self)
	local bridge = MovableMan:FindObjectByUniqueID(self.bridgeUID or 0);
	if bridge then
		bridge.ToDelete = true;
	end
end
