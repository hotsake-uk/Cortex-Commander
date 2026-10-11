-- Heavy and blunt weapons (Warhammer, Mace, Halberd, Greatsword, Throwing Hammer): each blow that lands (in a swing or thrown) strikes
-- sparks and a flash, and may knock the man it hits off his feet. A great weapon's wielder sometimes roars as he winds up a swing.
-- NumberValues: KnockDown (chance, 0 to 1, a hit knocks the unit down), KnockPower (how hard it shoves, m/s), BigSwing (1: the wielder may
-- shout as a swing starts).

local function value(self, name, default)
	return self:NumberValueExists(name) and self:GetNumberValue(name) or default;
end

function Create(self)
	self.knockChance = value(self, "KnockDown", 0);
	self.knockPower = value(self, "KnockPower", 5);
	self.bigSwing = value(self, "BigSwing", 0) > 0;
	self.lastSinceHit = self.TimeSinceBladeHit;
	self.wasSwinging = false;
end

local function wielderOf(self)
	local root = self:GetRootParent();
	if root and root.UniqueID ~= self.UniqueID and IsActor(root) then
		return ToActor(root);
	end
	return nil;
end

local function struck(self, wielder)
	local team = wielder and wielder.Team or self.Team;
	local tip = self.BladeEndPos;
	local head = (self.BladeStartPos + tip) * 0.5;

	for i = 1, 5 do
		local spark = CreateMOPixel("Spark Yellow 1", "Base.rte");
		if spark then
			spark.Pos = tip;
			spark.Vel = Vector(RangeRand(-6, 6), RangeRand(-7, 1));
			MovableMan:AddParticle(spark);
		end
	end
	PostProcessMan:AddLight(tip, 26, 255, 210, 150, 1.4);

	if self.knockChance <= 0 or math.random() >= self.knockChance then
		return;
	end
	-- The unit struck: the closest enemy with a part by the head of the weapon.
	local victim, best;
	for _, centre in ipairs({ tip, head }) do
		for mo in MovableMan:GetMOsInRadius(centre, 14, team) do
			local root = mo:GetRootParent();
			if root and IsActor(root) and root.Team ~= team then
				local dist = SceneMan:ShortestDistance(centre, mo.Pos, SceneMan.SceneWrapsX).Magnitude;
				if not best or dist < best then
					victim, best = ToActor(root), dist;
				end
			end
		end
	end
	if not victim or victim.Status >= Actor.DYING then
		return;
	end

	local away = wielder and SceneMan:ShortestDistance(wielder.Pos, victim.Pos, SceneMan.SceneWrapsX) or Vector(self.Vel.X, self.Vel.Y);
	if away:MagnitudeIsLessThan(0.1) then
		away = Vector(1, 0);
	end
	away = away.Normalized;
	victim.Vel = victim.Vel + away * self.knockPower + Vector(0, -self.knockPower * 0.4);
	victim.AngularVel = victim.AngularVel + (away.X >= 0 and -1 or 1) * self.knockPower * 0.6;
	if victim.Status == Actor.STABLE then
		victim.Status = Actor.UNSTABLE;
	end
	victim:Say("KnockedDown");
	CameraMan:AddScreenShake(3, tip);
end

function Update(self)
	local wielder = wielderOf(self);
	if self.bigSwing and wielder then
		local swinging = self.IsSwinging;
		if swinging and not self.wasSwinging and math.random() < 0.12 then
			wielder:Say("BigSwing");
		end
		self.wasSwinging = swinging;
	end
	-- The blade's hit timer starts over on every blow that lands on a unit.
	local sinceHit = self.TimeSinceBladeHit;
	if sinceHit < self.lastSinceHit then
		struck(self, wielder);
	end
	self.lastSinceHit = sinceHit;
end
