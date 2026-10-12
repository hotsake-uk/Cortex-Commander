-- Wall of Stone's wall (made by SpellCast.lua on the ground a pace in front of the caster): a "Medieval Spell Stone Wall" prop, six pixels
-- thick and forty tall, grinds up out of the ground over half a second (anyone stood where it rises is shoved aside first), then settles
-- into the terrain as real stone, so arrows stop in it and it can be dug and blasted like any rock. Dust and rubble as it rises.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local RISE_MS = 450;
local HEIGHT = 40;

function Create(self)
	self.start = FX.Now();
	self.ground = Vector(self.Pos.X, self.Pos.Y);
	-- Nobody inside the wall: units standing on the spot are moved a step to whichever side they are already on.
	for _, near in ipairs(FX.ActorsNear(self.ground + Vector(0, -HEIGHT / 2), HEIGHT, -1)) do
		local a = near.actor;
		if FX.Alive(a) and math.abs(near.offset.X) < 10 and near.offset.Y > -HEIGHT - 12 and near.offset.Y < 12 then
			local side = near.offset.X >= 0 and 1 or -1;
			a.Pos = a.Pos + Vector(side * (12 - math.abs(near.offset.X)), 0);
			a.Vel = a.Vel + Vector(side * 3, -1);
		end
	end
	self.wall = FX.Spawn("MOSRotating", "Medieval Spell Stone Wall", FX.Module, self.ground + Vector(0, HEIGHT + 1), Vector());
	self.wallUID = self.wall and self.wall.UniqueID or 0;
	self.settled = false;
	EmitVisualParticles("Dust", self.ground, Vector(0, -4), 1, 14, 0x9C948A);
	for pixel in SceneMan:DislodgePixelCircle(self.ground + Vector(0, 1), 3) do
		pixel.Vel = Vector(FX.Rand(-4, 4), FX.Rand(-8, -3));
	end
	CameraMan:AddScreenShake(2, self.ground);
end

function Update(self)
	local t = FX.Now() - self.start;
	local wall = MovableMan:FindObjectByUniqueID(self.wallUID);
	if not wall or wall.ToDelete then
		self.ToDelete = true;
		return;
	end
	if self.settled then
		-- (A frame on, to be sure the settling took.)
		self.ToDelete = true;
		return;
	end
	local rise = math.min(t / RISE_MS, 1);
	wall.Pos = self.ground + Vector(0, HEIGHT + 1 - HEIGHT * rise);
	wall.Vel = Vector();
	wall.RotAngle = 0;
	if math.random() < 0.6 then
		EmitVisualParticles("Dust", self.ground + Vector(FX.Rand(-5, 5), -FX.Rand(0, 4)), Vector(FX.Rand(-1, 1), -1.5), 0.6, 1, 0xA09890);
	end
	if rise >= 1 then
		wall.ToSettle = true;
		self.settled = true;
		EmitVisualParticles("Dust", self.ground + Vector(0, -HEIGHT), Vector(0, -1), 0.8, 6, 0xA09890);
	end
end
