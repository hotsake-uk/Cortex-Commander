-- The Cave Troll's boulders (Bestiary I): while it hunts something (CreatureBehaviors' CreatureHuntID) that is too far off for its club
-- but within its throw, and nothing stands between, every few seconds it lobs a Troll Boulder (Boulder.lua) at it: a real rock, thrown
-- from its hand on an arc worked out to land on the prey (the lower of the two arcs that reach it; the high one when only that does). It
-- throws the way its prey is and never at its own side (the boulder ignores its team's hits). The troll's number values:
--   TrollThrowMin       how near its prey may be and still be thrown at, in pixels (default 110)
--   TrollThrowRange     how far it throws, in pixels (default 420)
--   TrollThrowCooldown  how long between throws, in ms (default 4000)
--   TrollThrowSpeed     how fast the boulder leaves its hand, in m/s (default 22)

local function Number(self, key, default)
	return self:NumberValueExists(key) and self:GetNumberValue(key) or default;
end

function Create(self)
	self.throwTimer = Timer();
	self.throwTimer.ElapsedSimTimeMS = 2000;
end

-- The velocity (m/s) to throw at a point dx, dy px away (dy down) at speed (m/s), or nil when it is out of reach.
local function lob(dx, dy, speed)
	local ppm = GetPPM();
	local g = SceneMan.GlobalAcc.Y * ppm;
	local v = speed * ppm;
	local x = math.abs(dx);
	local h = -dy;
	if x < 1 or g <= 0 then
		return nil;
	end
	local disc = v * v * v * v - g * (g * x * x + 2 * h * v * v);
	local tan;
	if disc < 0 then
		-- Out of reach at that speed: the best arc there is, 45 degrees, and the rock falls short.
		tan = 1;
	else
		tan = (v * v - math.sqrt(disc)) / (g * x);
		if tan < -0.3 then
			-- (Far below: not thrown down steeply, the rock goes level and falls.)
			tan = -0.3;
		end
	end
	local angle = math.atan(tan);
	local vx = v * math.cos(angle) * (dx < 0 and -1 or 1);
	local vy = -v * math.sin(angle);
	return Vector(vx / ppm, vy / ppm);
end

function Update(self)
	if not self.throwTimer:IsPastSimMS(Number(self, "TrollThrowCooldown", 4000)) or self.Status ~= Actor.STABLE or self:IsPlayerControlled()
		or not self:NumberValueExists("CreatureHuntID") then
		return;
	end
	local Target = MovableMan:FindObjectByUniqueID(self:GetNumberValue("CreatureHuntID"));
	if not Target or not MovableMan:ValidMO(Target) or (IsActor(Target) and ToActor(Target):IsDead()) then
		return;
	end
	local To = SceneMan:ShortestDistance(self.Pos, Target.Pos, false);
	local gap = To.Magnitude;
	if gap < Number(self, "TrollThrowMin", 110) or gap > Number(self, "TrollThrowRange", 420) or To.Y < -240 or To.Y > 260 then
		return;
	end
	if SceneMan:CastStrengthSumRay(self.EyePos, Target.Pos, 4, rte.grassID) > 60 then
		return;
	end
	local dir = To.X < 0 and -1 or 1;
	local hand = self.Pos + Vector(dir * 16, -self.Radius * 0.5 - 8);
	local vel = lob(To.X - dir * 16, To.Y + self.Radius * 0.5 + 8, Number(self, "TrollThrowSpeed", 22));
	if not vel then
		return;
	end
	local rock = CreateMOSRotating("Troll Boulder", "Base.rte");
	if not rock then
		return;
	end
	rock.Pos = hand;
	rock.Vel = vel + self.Vel * 0.5;
	rock.Team = self.Team;
	rock.AngularVel = -dir * 6;
	rock:SetNumberValue("ThrowerUID", self.UniqueID);
	MovableMan:AddParticle(rock);
	self.HFlipped = dir < 0;
	self.throwTimer:Reset();
	-- The heave: the troll leans into it and the ground dust flies.
	self:AddImpulseForce(Vector(-dir * 2, 0) * self.Mass, Vector());
	EmitVisualParticles("Dust", hand, Vector(dir * 2, -1), 0.5, 4, 0x6A6660);
end
