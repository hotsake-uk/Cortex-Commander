-- Chains of the Damned (made by FellCast.lua on the ground in front of the caster): chains burst out of the ground across some eight paces
-- ahead and bind every enemy standing there (Actor:Ensnare) for three seconds, each held by two chains of grey light from the ground to
-- their body, dragged down a little, until the chains sink back. NumberValues: Dir (1 right, -1 left), CasterUID.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");
local MODULE = "FellLegion.rte";

local REACH = 170;
local HEIGHT = 70;
local HOLD_MS = 3000;

function Create(self)
	self.start = FX.Now();
	self.dir = self:GetNumberValue("Dir") >= 0 and 1 or -1;
	self.chained = {};
	local team = self.Team;
	for actor in MovableMan.Actors do
		if actor.Team ~= team and actor.Status < Actor.DYING and not actor.ToDelete then
			local offset = SceneMan:ShortestDistance(self.Pos, actor.Pos, true);
			local ahead = offset.X * self.dir;
			if ahead > -20 and ahead < REACH and math.abs(offset.Y) < HEIGHT then
				actor:Ensnare(HOLD_MS / 1000);
				actor.Vel = Vector(actor.Vel.X * 0.3, actor.Vel.Y + 4);
				FX.Say(actor, "Chained");
				local ground = FX.GroundBelow(actor.Pos, 90);
				self.chained[#self.chained + 1] = {uid = actor.UniqueID, left = ground + Vector(-10, 0), right = ground + Vector(10, 0), sway = FX.Rand(0, 6)};
				EmitVisualParticles("Dust", ground, Vector(0, -5), 0.9, 14, 0x5A5A66);
				EmitVisualParticles("Sparks", ground, Vector(0, -4), 1, 8, 0x9A9AAA);
				FX.Spawn("MOPixel", "Spark Yellow 1", "Base.rte", ground, Vector(FX.Rand(-4, 4), FX.Rand(-8, -3)));
			end
		end
	end
	-- The ground itself cracks along the reach.
	for i = 0, 5 do
		local x = self.Pos.X + self.dir * (i * REACH / 5 + 10);
		local ground = FX.GroundBelow(Vector(x, self.Pos.Y - 20), 60);
		EmitVisualParticles("Dust", ground, Vector(0, -3), 0.8, 4, 0x5A5A66);
	end
	CameraMan:AddScreenShake(3, self.Pos);
end

function Update(self)
	local t = FX.Now() - self.start;
	local fade = t < HOLD_MS and 1 or math.max(1 - (t - HOLD_MS) / 500, 0);
	if fade <= 0 then
		self.ToDelete = true;
		return;
	end
	for _, link in ipairs(self.chained) do
		local actor = FX.ActorByUID(link.uid);
		if actor and FX.Alive(actor) then
			local body = actor.Pos + Vector(0, -2);
			local sag = Vector(0, 4 + 2 * math.sin(t * 0.01 + link.sway));
			for _, anchor in ipairs({link.left, link.right}) do
				local mid = (anchor + body) * 0.5 + sag;
				PostProcessMan:AddEnergyBeam(anchor, mid, 2, 110, 110, 130, 0.9 * fade, 16);
				PostProcessMan:AddEnergyBeam(mid, body, 2, 110, 110, 130, 0.9 * fade, 16);
			end
			PostProcessMan:AddLight(body, 40, 150, 120, 200, 0.5 * fade);
			if math.random() < 0.2 then
				EmitVisualParticles("Dust", link.left + Vector(10, 0), Vector(0, -1), 0.6, 1, 0x5A5A66);
			end
		end
	end
end
