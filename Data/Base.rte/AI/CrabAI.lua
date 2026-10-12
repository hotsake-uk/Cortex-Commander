require("AI/NativeCrabAI");

function Create(self)
	self.AI = NativeCrabAI:Create(self);
end

function ThreadedUpdateAI(self)
	self.AI:Update(self);
end

-- Runs after the threaded AI, one actor at a time: a spit made there goes into the scene here (CreatureBehaviors.SpitAt).
function UpdateAI(self)
	if self.AI and self.AI.PendingSpit then
		MovableMan:AddParticle(self.AI.PendingSpit);
		self.AI.PendingSpit = nil;
	end
end

function Destroy(self)
	self.AI:Destroy(self);
end
