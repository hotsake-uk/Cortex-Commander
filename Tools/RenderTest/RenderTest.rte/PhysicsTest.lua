function PhysicsTestScript:StartScript()
	self.timer = Timer();
	self.stage = 0;
	self.pours = 0;
end

function PhysicsTestScript:UpdateScript()
	-- Drops boulders and a lump of concrete on the hill near the camera, pours loose sand beside them, and oil then water into one spot so they layer.
	local origin = CameraMan:GetOffset(0);
	local w = FrameMan.PlayerScreenWidth;
	local h = FrameMan.PlayerScreenHeight;
	if self.stage == 0 and self.timer:IsPastSimMS(4000) then
		self.stage = 1;
		SceneMan:SpawnTerrainChunk(origin + Vector(w * 0.22, h * 0.12), 24, "Stone");
		SceneMan:SpawnTerrainChunk(origin + Vector(w * 0.40, h * 0.10), 14, "Concrete");
		SceneMan:SpawnTerrainChunk(origin + Vector(w * 0.55, h * 0.05), 18, "Earth");
	elseif self.stage == 1 and self.timer:IsPastSimMS(7000) then
		self.stage = 2;
		SceneMan:SpawnTerrainChunk(origin + Vector(w * 0.30, h * 0.05), 12, "Sand");
		SceneMan:SpawnTerrainChunk(origin + Vector(w * 0.48, h * 0.05), 30, "Stone");
	end
	if self.pours < 60 and self.timer:IsPastSimMS(4000 + self.pours * 100) then
		self.pours = self.pours + 1;
		SceneMan:PourLiquid(origin + Vector(w * 0.80, h * 0.2), 3, "Sand");
		SceneMan:PourLiquid(origin + Vector(w * 0.66, h * 0.2), 3, self.pours < 25 and "Oil" or "Water");
	end
	if self.timer:IsPastSimMS(1000 * (self.logged or 5)) then
		self.logged = (self.logged or 5) + 1;
		print("PhysicsTest: " .. SceneMan:GetFallingTerrainChunkCount() .. " pieces moving");
	end
end
