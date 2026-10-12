-- A fire arrow (with Arrow.lua, which does the flight, the thud and the sticking): it burns as it flies, and where it strikes, ground, wall,
-- unit or shield, it sets alight what is there. Its flame lives on in the copy left stuck in a unit, which keeps burning them for a while.

function Create(self)
	self.lit = true;
	self.ignited = false;
	self.frame = 0;
end

local function catch(self, radius)
	if self.ignited then
		return;
	end
	self.ignited = true;
	SceneMan:IgniteArea(self.Pos, radius);
	PostProcessMan:AddLight(self.Pos, 60, 255, 150, 50, 1.5);
	EmitVisualParticles("Embers", self.Pos, Vector(0, -2), 0.6, 8, 0xFF9030);
end

function Update(self)
	self.frame = self.frame + 1;
	if self:NumberValueExists("StuckToID") then
		-- Burning in someone: it sets them alight again now and then until it burns out.
		if self.frame % 90 == 0 and self.Age < 6000 then
			self.ignited = false;
			catch(self, 6);
		end
		return;
	end
	if self.frame % 3 == 0 then
		EmitVisualParticles("Embers", self.Pos, Vector(0, -1), 0.4, 1, 0xFF9030);
	end
end

function OnCollideWithTerrain(self, terrainID)
	catch(self, 10);
end

function OnCollideWithMO(self, hitMO, hitMORootParent)
	catch(self, 10);
end
