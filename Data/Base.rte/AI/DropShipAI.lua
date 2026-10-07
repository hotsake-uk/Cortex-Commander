require("AI/NativeDropShipAI");

function Create(self)
	self.AI = NativeDropShipAI:Create(self);
	-- The engine's autopilot (ACDropShip::UpdateAutopilot, a port of NativeDropShipAI) where the build has it; else the script. (Asked
	-- without the error a missing member is, for an older exe; CCCP_LUA_CRAFT=1 keeps the script.)
	local ok, member = pcall(function() return self.UpdateAutopilot; end);
	self.EngineAutopilot = ok and member ~= nil and not (os and os.getenv and os.getenv("CCCP_LUA_CRAFT") == "1");
end

function ThreadedUpdateAI(self)
	if self.EngineAutopilot then
		self:UpdateAutopilot();
	else
		self.AI:Update(self);
	end
end