-- Meteor Storm's rune circle: glows red on the ground for a moment while five meteors come crashing down out of the sky around it, one
-- after another over two seconds, all from the same side of the sky (Meteor.lua). Then it fades.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local METEOR_TIMES = {450, 800, 1150, 1500, 1850};
local LIFE = 2700;

local function callMeteor(self)
	local target = self.Pos + Vector(FX.Rand(-85, 85), 0);
	local ground, found = FX.GroundBelow(target + Vector(0, -60), 160);
	if found then
		target = ground;
	end
	local sky = target + Vector(self.side * FX.Rand(140, 230), -FX.Rand(420, 520));
	sky.Y = math.max(sky.Y, 6);
	local vel = SceneMan:ShortestDistance(sky, target, true).Normalized * FX.Rand(34, 40);
	FX.Spawn("MOSParticle", "Medieval Spell Meteor", FX.Module, sky, vel, self.Team, {CasterUID = self.casterUID});
end

function Create(self)
	self.casterUID = self:GetNumberValue("CasterUID");
	self.start = FX.Now();
	self.next = 1;
	self.side = math.random() < 0.5 and -1 or 1;
	self.frame = 0;
	FX.Spawn("MOSParticle", "Medieval Spell Rune Circle Red", FX.Module, self.Pos + Vector(0, -3), Vector());
	FX.PlaySound("Medieval Spell Rune", self.Pos);
	PostProcessMan:AddShockwave(self.Pos, 3000);
end

function Update(self)
	local t = FX.Now() - self.start;
	self.frame = self.frame + 1;
	local fade = t > LIFE - 500 and math.max((LIFE - t) / 500, 0) or math.min(t / 200, 1);
	PostProcessMan:AddLight(self.Pos + Vector(0, -8), 120, 255, 60, 30, (0.8 + 0.3 * math.sin(t * 0.012)) * fade);
	if self.frame % 2 == 0 then
		EmitVisualParticles("Embers", self.Pos + Vector(FX.Rand(-20, 20), -2), Vector(0, -3), 0.3, 1, 0xFF4020);
	end
	while self.next <= #METEOR_TIMES and t >= METEOR_TIMES[self.next] do
		callMeteor(self);
		self.next = self.next + 1;
	end
	if t >= LIFE then
		self.ToDelete = true;
	end
end
