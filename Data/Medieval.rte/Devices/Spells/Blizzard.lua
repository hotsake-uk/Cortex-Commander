-- Blizzard's storm (made by SpellCast.lua over what the aim strikes): for six seconds a cloud 70 pixels over the point pours snow (real
-- flakes that settle as snow), a cold wind drives it sideways, and every third of a second every enemy within 110 pixels of the point is
-- chilled (FX.Chill, slowed and frosted) and stung by the cold. The storm howls, fogs the air, and the screen goes to the "Medieval
-- Blizzard" post shader (snow streaking across it, frost at the edges) while any blizzard rages. Like the Time Warp field it runs in the
-- master Lua state so overlapping blizzards share the count of who holds the screen's pass, which is put back when the last ends.

local FX = dofile("Medieval.rte/Devices/Spells/SpellFX.lua");

local LIFE = 6000;
local RADIUS = 110;
local CLOUD_HEIGHT = 70;
local SHADER = "Medieval Blizzard";

MedievalBlizzard = MedievalBlizzard or {count = 0, saved = ""};

local function finish(self)
	if self.finished then
		return;
	end
	self.finished = true;
	if self.holdsScreen then
		self.holdsScreen = false;
		MedievalBlizzard.count = math.max(MedievalBlizzard.count - 1, 0);
		if MedievalBlizzard.count == 0 and PostProcessMan.PostShader == SHADER then
			PostProcessMan:SetPostShader(MedievalBlizzard.saved or "");
		end
	end
	if self.sound then
		pcall(function() self.sound:Stop(); end);
	end
	self.ToDelete = true;
end

function Create(self)
	self.start = FX.Now();
	self.frame = 0;
	self.ground = Vector(self.Pos.X, self.Pos.Y);
	self.cloud = self.ground + Vector(0, -CLOUD_HEIGHT);
	self.wind = (math.random() < 0.5 and -1 or 1) * FX.Rand(1.5, 3);
	self.nextChill = self.start;
	if MedievalBlizzard.count <= 0 or PostProcessMan.PostShader ~= SHADER then
		local current = PostProcessMan.PostShader;
		MedievalBlizzard.saved = current ~= SHADER and current or "";
		MedievalBlizzard.count = 0;
	end
	MedievalBlizzard.count = MedievalBlizzard.count + 1;
	self.holdsScreen = true;
	PostProcessMan:SetPostShader(SHADER);
	self.sound = FX.PlaySound("Medieval Spell Blizzard", self.cloud);
	FX.Flash("Ice", self.cloud);
	FX.Ring("Mist", self.cloud, 20, 6, 0xD8E8FF, 10);
end

function Update(self)
	if self.finished then
		return;
	end
	local now = FX.Now();
	local t = now - self.start;
	self.frame = self.frame + 1;
	if t >= LIFE then
		finish(self);
		return;
	end
	if PostProcessMan.PostShader ~= SHADER and PostProcessMan.PostShader == "" then
		PostProcessMan:SetPostShader(SHADER);
	end
	-- The storm builds over the first second and dies over the last.
	local strength = math.min(t / 1000, 1) * math.min((LIFE - t) / 1000, 1);
	-- Snow: real flakes out of the cloud, driven by the wind.
	for _ = 1, math.floor(2 + 3 * strength) do
		local at = self.cloud + Vector(FX.Rand(-RADIUS, RADIUS), FX.Rand(-14, 6));
		FX.Spawn("MOPixel", "Medieval Spell Snow Flake", FX.Module, at, Vector(self.wind + FX.Rand(-1, 1), FX.Rand(2, 5)));
	end
	-- The cloud and the whirling air.
	if self.frame % 3 == 0 then
		EmitVisualParticles("Mist", self.cloud + Vector(FX.Rand(-RADIUS, RADIUS), FX.Rand(-16, 10)), Vector(self.wind * 0.5, 0.2), 0.7, 2, 0xC8D8F0);
	end
	if self.frame % 6 == 0 then
		PostProcessMan:AddFog(self.cloud + Vector(FX.Rand(-RADIUS * 0.8, RADIUS * 0.8), 0), 40, 0.35 * strength);
		EmitVisualParticles("Sparks", self.ground + Vector(FX.Rand(-RADIUS, RADIUS), FX.Rand(-CLOUD_HEIGHT, 0)), Vector(self.wind * 2, 1), 0.5, 2, 0xEAF6FF);
	end
	PostProcessMan:AddLight(self.cloud, RADIUS * 2, 140, 180, 255, 0.5 * strength);
	-- The cold: everyone of the other side under the cloud, a third of a second at a time.
	if now >= self.nextChill then
		self.nextChill = now + 330;
		for _, near in ipairs(FX.ActorsNear(self.ground + Vector(0, -CLOUD_HEIGHT / 2), RADIUS, self.Team)) do
			if FX.Alive(near.actor) then
				FX.Chill(near.actor, 1500, self.Team);
				FX.Hurt(near.actor, 2 * strength, 0);
				near.actor.Vel = near.actor.Vel + Vector(self.wind * 0.3, 0);
			end
		end
		-- Fires under the storm go out.
		if math.random() < 0.5 then
			SceneMan:PourLiquid(self.ground + Vector(FX.Rand(-RADIUS, RADIUS), -4), 3, "Water");
		end
	end
end

function Destroy(self)
	pcall(finish, self);
end
