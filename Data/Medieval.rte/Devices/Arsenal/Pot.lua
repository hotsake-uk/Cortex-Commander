-- A clay pot (Firepot, Stinkpot): once it has left the hand it breaks on whatever it hits hard enough, a wall, the ground or a man,
-- and its gibs are what was inside. Shot while held, it breaks there too (GibWoundLimit). Dropped gently, it doesn't break; a lit
-- firepot's fuse (TriggerDelay) bursts it a while later anyway.
-- StringValue ThrowSpeech: a speech trigger the thrower says as he lets it fly. NumberValue BreakSpeed: how hard it must hit.

function Create(self)
	self.breakSpeed = self:NumberValueExists("BreakSpeed") and self:GetNumberValue("BreakSpeed") or 6;
	self.throwSpeech = self:StringValueExists("ThrowSpeech") and self:GetStringValue("ThrowSpeech") or nil;
	self.lastSpeed = 0;
	self.freeTimer = Timer();
	self.holderID = nil;
end

function Update(self)
	if self:IsAttached() then
		local root = self:GetRootParent();
		self.holderID = (root and IsActor(root)) and root.UniqueID or nil;
		self.freeTimer:Reset();
		self.lastSpeed = 0;
		return;
	end
	if self.holderID then
		-- Just left the hand: thrown, if it's going at a fair clip, rather than dropped.
		local holder = MovableMan:FindObjectByUniqueID(self.holderID);
		if self.throwSpeech and holder and IsActor(holder) and self.Vel:MagnitudeIsGreaterThan(3.5) then
			ToActor(holder):Say(self.throwSpeech);
		end
		self.holderID = nil;
	end
	self.lastSpeed = self.Vel.Magnitude;
end

local function shatter(self)
	if not self.broken then
		self.broken = true;
		self:GibThis();
	end
end

function OnCollideWithTerrain(self, terrainID)
	if not self:IsAttached() and self.freeTimer:IsPastSimMS(30) and self.lastSpeed >= self.breakSpeed then
		shatter(self);
	end
end

function OnCollideWithMO(self, hitMO, hitMORootParent)
	if not self:IsAttached() and self.freeTimer:IsPastSimMS(30) and self.lastSpeed >= self.breakSpeed * 0.6 then
		shatter(self);
	end
end
