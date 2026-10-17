// Tests for mission logic: the graph (js/flowmodel.js) and the Lua made from it (js/flowlua.js). The Lua is run, in a Lua interpreter (fengari), against a
// small stand-in for the game: units with teams and positions, scene areas, gold, a clock, messages, objective markers and battle events. Each test builds a
// mission, runs it frame by frame while moving units about, and checks what the mission did. Run with: npm test
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const { lua, lauxlib, lualib, to_luastring, to_jsstring } = require('fengari');

globalThis.window = globalThis;
window.MS = {};
for (const file of ['flowmodel.js', 'flowlua.js']) {
	vm.runInThisContext(fs.readFileSync(new URL('../js/' + file, import.meta.url), 'utf8'), { filename: file });
}
const F = window.MS.flow;

// ---- the stand-in for the game ----

const GAME = String.raw`
Log = {}
function log(s) Log[#Log + 1] = s end
Clock = 0
Timer = function()
	local t = { start = Clock }
	return setmetatable(t, { __index = function(self, k) if k == "ElapsedSimTimeS" then return Clock - self.start end end })
end
Actors = {}
Items = {}
local function iter(list) local i = 0 return function() i = i + 1 return list[i] end end
local ActorMeta = { __index = function(a, k)
	if k == "IsInGroup" then return function(self, g) return self.groups[g] == true end
	elseif k == "AddToGroup" then return function(self, g) self.groups[g] = true end
	elseif k == "ClearAIWaypoints" then return function(self) self.waypoints = {} end
	elseif k == "AddAISceneWaypoint" then return function(self, p) self.waypoints = self.waypoints or {}; self.waypoints[#self.waypoints + 1] = p end
	elseif k == "AddInventoryItem" then return function(self, item) self.cargo[#self.cargo + 1] = item end
	elseif k == "OpenDoor" then return function(self) log("open door " .. self.UniqueID) end
	elseif k == "CloseDoor" then return function(self) log("close door " .. self.UniqueID) end
	elseif k == "GibThis" then return function(self) self.Health = 0; log("gib " .. self.UniqueID) end
	elseif k == "Inventory" then return iter(rawget(a, "inv"))
	elseif k == "AboveHUDPos" then return rawget(a, "Pos") end
end }
NextID = 0
function MakeActor(team, x, y, extra)
	NextID = NextID + 1
	local a = { Team = team, Pos = { X = x, Y = y }, Health = 100, UniqueID = NextID, groups = {}, inv = {}, PresetName = "Unit" }
	if extra then for k, v in pairs(extra) do a[k] = v end end
	setmetatable(a, ActorMeta)
	Actors[#Actors + 1] = a
	return a
end
MovableMan = setmetatable({
	AddActor = function(self, a) setmetatable(a, ActorMeta); Actors[#Actors + 1] = a; log("spawn " .. a.PresetName .. " team " .. a.Team .. (#(a.cargo or {}) > 0 and (" carrying " .. #a.cargo) or "")) end,
	GetMOsInBox = function(self, box, ignoreTeam, getsHit)
		local found = {}
		for _, d in ipairs(Doors) do
			if d.Pos.X >= box.Corner.X and d.Pos.X < box.Corner.X + box.Width and d.Pos.Y >= box.Corner.Y and d.Pos.Y < box.Corner.Y + box.Height then found[#found + 1] = d end
		end
		return iter(found)
	end,
	ChangeActorTeam = function(self, a, team) a.Team = team end,
	AddItem = function(self, it) Items[#Items + 1] = it; log("item " .. it.PresetName) end,
}, { __index = function(t, k) if k == "Actors" then return iter(Actors) elseif k == "Items" then return iter(Items) end end })
function made(name) NextID = NextID + 1; return setmetatable({ PresetName = name, Team = -1, Pos = { X = 0, Y = 0 }, Health = 100, UniqueID = NextID, groups = {}, inv = {}, cargo = {} }, ActorMeta) end
CreateAHuman = function(name, module) return made(name) end
CreateACrab = function(name, module) return made(name) end
CreateHDFirearm = function(name, module) return { PresetName = name, Pos = { X = 0, Y = 0 } } end
Areas = {}
-- an area may have more boxes (Areas[n].more = { {x,y,w,h}, ... })
SceneMan = { Scene = {
	HasArea = function(self, n) return Areas[n] ~= nil end,
	GetArea = function(self, n)
		local b = Areas[n]
		local boxes = { { Corner = { X = b.x, Y = b.y }, Width = b.w, Height = b.h, Center = { X = b.x + b.w / 2, Y = b.y + b.h / 2 } } }
		for _, m in ipairs(b.more or {}) do boxes[#boxes + 1] = { Corner = { X = m.x, Y = m.y }, Width = m.w, Height = m.h, Center = { X = m.x + m.w / 2, Y = m.y + m.h / 2 } } end
		return setmetatable({
			IsInside = function(self, p) for _, bx in ipairs(boxes) do if p.X >= bx.Corner.X and p.X < bx.Corner.X + bx.Width and p.Y >= bx.Corner.Y and p.Y < bx.Corner.Y + bx.Height then return true end end return false end,
			Center = { X = b.x + b.w / 2, Y = b.y + b.h / 2 },
			RandomPoint = { X = b.x + 1, Y = b.y + 1 },
		}, { __index = function(t, k) if k == "Boxes" then return iter(boxes) end end })
	end,
},
	RevealUnseenBox = function(self, x, y, w, h, team) log("reveal " .. x .. "," .. y .. " for " .. team) end,
	RestoreUnseenBox = function(self, x, y, w, h, team) log("hide " .. x .. "," .. y .. " for " .. team) end,
	MakeAllUnseen = function(self, v, team) log("hide all for " .. team) end,
}
function Vector(x, y) return { X = x or 0, Y = y or 0 } end
Actor = { AIMODE_SENTRY = 1, AIMODE_PATROL = 2, AIMODE_GOTO = 3, AIMODE_BRAINHUNT = 4, AIMODE_GOLDDIG = 5, AIMODE_RETURN = 6, AIMODE_STAY = 7, AIMODE_DELIVER = 9, INACTIVE = 5, STABLE = 0 }
Activity = { MINDIFFICULTY = 0, CAKEDIFFICULTY = 20, EASYDIFFICULTY = 40, MEDIUMDIFFICULTY = 60, HARDDIFFICULTY = 80, NUTSDIFFICULTY = 95, MAXDIFFICULTY = 100 }
PresetMan = {
	GetModuleID = function(self, name) return 7 end,
	GetLoadout = function(self, name, tech, spawnDelivery) local a = made("Loadout " .. name); return a end,
}
RandomAHuman = function(group, tech) return made("Random " .. group) end
RandomACDropShip = function(group, tech) local c = made("Dropship"); c.cargo = {}; return c end
RandomACRocket = function(group, tech) local c = made("Rocket"); c.cargo = {}; return c end
MusicMan = { PlayDynamicSong = function(self, name) log("music " .. name) end }
Doors = {}
function IsADoor(mo) return mo.isDoor == true end
function ToADoor(mo) return mo end
FrameMan = { ClearScreenText = function(self, s) end, SetScreenText = function(self, text, screen, blink, ms, centred) log("msg " .. text) end }
ConsoleMan = { PrintString = function(self, s) log("console " .. s) end }
ActivityMan = { EndActivity = function(self) log("end winner " .. tostring(Act.WinnerTeam)) end }
GameActivity = { ARROWDOWN = 0 }
BattleQueue = {}
Marks = 0
Test = {}
Act = setmetatable({ Funds = {} }, { __index = Test })
function Test:GetTeamFunds(t) return self.Funds[t] or 0 end
function Test:ChangeTeamFunds(amount, t) self.Funds[t] = (self.Funds[t] or 0) + amount; log("gold " .. amount .. " to " .. t) end
function Test:PlayerActive(p) return p == 0 end
function Test:PlayerHuman(p) return p == 0 end
function Test:ScreenOfPlayer(p) return p end
function Test:AddObjectivePoint(text, pos, team, dir) Marks = Marks + 1; LastMark = text end
function Test:SetTeamFunds(amount, t) self.Funds[t] = amount end
function Test:GetTeamTech(t) return "Base.rte" end
function Test:GetScenarioSettingOr(key, fallback) if Settings[key] ~= nil then return Settings[key] end return fallback end
function Test:SetLZArea(team, area) log("lz for " .. team) end
function Test:SaveString(key, value) Saved[key] = value end
function Test:LoadString(key) return Saved[key] or "" end
Settings = {}
Saved = {}
function Step(seconds) Clock = Clock + (seconds or 0.1); Act:RunStudioFlow() end
function Logged() local s = table.concat(Log, "\n"); Log = {}; return s end
`;

const BATTLE = String.raw`
function SandboxBattleEvent() if #BattleQueue == 0 then return "" end return table.remove(BattleQueue, 1) end
function SandboxBattleMode(mode, size, ship) log("battle " .. mode .. " " .. size) end
function SandboxBattleLayout(mode, seed, zones) log("layout " .. mode .. " " .. seed) end
`;

// A Lua state with the stand-in game and a mission's block loaded; run(code) runs Lua and returns what it returns, as a string.
function game(graph, opts) {
	const L = lauxlib.luaL_newstate();
	lualib.luaL_openlibs(L);
	const run = (code) => {
		if (lauxlib.luaL_loadstring(L, to_luastring(code)) !== lua.LUA_OK || lua.lua_pcall(L, 0, 1, 0) !== lua.LUA_OK) {
			const message = to_jsstring(lua.lua_tostring(L, -1));
			lua.lua_pop(L, 1);
			throw new Error('Lua: ' + message);
		}
		const out = lua.lua_isnil(L, -1) ? '' : to_jsstring(lua.lua_tostring(L, -1) || to_luastring(''));
		lua.lua_pop(L, 1);
		return out;
	};
	run(GAME + (opts && opts.noBattle ? '' : BATTLE));
	const block = F.compile(graph, 'Test');
	run(block);
	return {
		run,
		block,
		step: (seconds, times) => {
			for (let i = 0; i < (times || 1); i++) {
				run('Step(' + (seconds === undefined ? 0.1 : seconds) + ')');
			}
			return run('return Logged()');
		},
	};
}

// A graph made by a small description: [kind, params, {pin: index}] for each node, in order (ids n1, n2...).
function graph(nodes) {
	const g = F.create();
	const made = nodes.map(([kind, p]) => F.addNode(g, kind, 0, 0, p));
	nodes.forEach(([, , out, conds], i) => {
		for (const [pin, j] of Object.entries(out || {})) {
			const why = F.connect(g, made[i].id, pin, made[j].id);
			assert.equal(why, '', 'wire ' + made[i].kind + '.' + pin + ' -> ' + made[j].kind + ': ' + why);
		}
		for (const c of conds || []) {
			made[i].lists.conds.push(Object.assign(F.condDefaults(c.kind), c));
		}
	});
	return g;
}

const lines = (log) => log.split('\n').filter(Boolean);

// ---- events ----

test('the start runs its chain once, on the first frame', () => {
	const m = game(graph([['start', {}, { then: 1 }], ['message', { text: 'Hello' }, { then: 2 }], ['gold', { team: 0, amount: 100 }]]));
	assert.deepEqual(lines(m.step()), ['msg Hello', 'gold 100 to 0']);
	assert.equal(m.step(0.1, 5), '');
});

test('entering an area: once per unit that comes in, and only the team asked for', () => {
	const m = game(graph([['enter', { area: 'Gate', team: 0 }, { then: 1 }], ['message', { text: 'In' }]]));
	m.run('Areas["Gate"] = { x = 100, y = 0, w = 50, h = 50 }');
	m.run('A = MakeActor(0, 0, 10); E = MakeActor(1, 0, 10)');
	assert.equal(m.step(), '');
	m.run('E.Pos.X = 110');
	assert.equal(m.step(), '', 'the other team does not count');
	m.run('A.Pos.X = 120');
	assert.deepEqual(lines(m.step()), ['msg In']);
	assert.equal(m.step(0.1, 3), '', 'staying inside does not fire again');
	m.run('A.Pos.X = 0');
	m.step();
	m.run('A.Pos.X = 120');
	assert.deepEqual(lines(m.step()), ['msg In'], 'coming back in fires again');
});

test('leaving and emptying an area', () => {
	const m = game(graph([['leave', { area: 'Room', team: -1 }, { then: 1 }], ['message', { text: 'Left' }], ['empty', { area: 'Room', team: -1 }, { then: 3 }], ['message', { text: 'Empty' }]]));
	m.run('Areas["Room"] = { x = 0, y = 0, w = 100, h = 100 }; A = MakeActor(0, 10, 10); B = MakeActor(1, 20, 20)');
	assert.equal(m.step(), '');
	m.run('A.Pos.X = 500');
	assert.deepEqual(lines(m.step()), ['msg Left']);
	m.run('B.Pos.X = 500');
	assert.deepEqual(lines(m.step()).sort(), ['msg Empty', 'msg Left']);
});

test('a team gone fires once (it latches), or every moment if told to run again', () => {
	const once = game(graph([['teamGone', { team: 1 }, { then: 1 }], ['message', { text: 'Gone' }]]));
	once.run('E = MakeActor(1, 0, 0)');
	assert.equal(once.step(), '', 'not before the team was ever seen');
	once.run('E.Health = 0');
	assert.deepEqual(lines(once.step(0.1, 4)), ['msg Gone']);
	const again = game(graph([['teamGone', { team: 1, again: true }, { then: 1 }], ['message', { text: 'Gone' }]]));
	again.run('E = MakeActor(1, 0, 0)');
	again.step();
	again.run('E.Health = 0');
	assert.equal(lines(again.step(0.1, 3)).length, 3);
});

test('a brain destroyed: only units in the Brains group count', () => {
	const m = game(graph([['brainGone', { team: 1 }, { then: 1 }], ['win', { team: 0 }]]));
	m.run('B = MakeActor(1, 0, 0); B.groups["Brains"] = true; S = MakeActor(1, 5, 5)');
	assert.equal(m.step(), '');
	m.run('B.Health = 0');
	assert.deepEqual(lines(m.step()), ['end winner 0']);
});

test('timers: once after the seconds, or again and again', () => {
	const once = game(graph([['timer', { seconds: 1 }, { then: 1 }], ['message', { text: 'T' }]]));
	assert.equal(once.step(0.5), '');
	assert.deepEqual(lines(once.step(0.6, 5)), ['msg T']);
	const repeat = game(graph([['timer', { seconds: 1, repeat: true }, { then: 1 }], ['message', { text: 'R' }]]));
	// the mission's clock starts on its first frame, so 17 frames of a quarter second is 4 s of it
	assert.equal(lines(repeat.step(0.25, 17)).length, 4);
});

test('gold reaching an amount fires as it gets there, not while it stays there', () => {
	const m = game(graph([['funds', { team: 0, op: '>=', amount: 100 }, { then: 1 }], ['message', { text: 'Rich' }]]));
	m.run('Act.Funds[0] = 50');
	assert.equal(m.step(), '');
	m.run('Act.Funds[0] = 150');
	assert.deepEqual(lines(m.step(0.1, 3)), ['msg Rich']);
	m.run('Act.Funds[0] = 10');
	m.step();
	m.run('Act.Funds[0] = 200');
	assert.deepEqual(lines(m.step()), ['msg Rich']);
});

// ---- flow ----

test('a branch takes the true or the false side', () => {
	const g = graph([['always', {}, { then: 1 }], ['branch', {}, { true: 2, false: 3 }, [{ kind: 'funds', team: 0, op: '>=', amount: 100 }]], ['message', { text: 'Yes' }], ['message', { text: 'No' }]]);
	const m = game(g);
	assert.deepEqual(lines(m.step()), ['msg No']);
	m.run('Act.Funds[0] = 100');
	assert.deepEqual(lines(m.step()), ['msg Yes']);
});

test('every condition of a branch has to hold', () => {
	const g = graph([['always', {}, { then: 1 }], ['branch', {}, { true: 2 }, [{ kind: 'flag', name: 'a', value: true }, { kind: 'inArea', team: 0, area: 'Z', op: '>=', count: 2 }]], ['message', { text: 'Both' }]]);
	const m = game(g);
	m.run('Areas["Z"] = { x = 0, y = 0, w = 10, h = 10 }; MakeActor(0, 1, 1); MakeActor(0, 2, 2)');
	assert.equal(m.step(), '');
	m.run('Act.StudioState.flags["a"] = true');
	assert.deepEqual(lines(m.step()), ['msg Both']);
});

test('do once lets a chain through the first time only', () => {
	const m = game(graph([['always', {}, { then: 1 }], ['doOnce', {}, { then: 2 }], ['message', { text: 'Once' }]]));
	assert.deepEqual(lines(m.step(0.1, 10)), ['msg Once']);
});

test('flags set by one chain are seen by another', () => {
	const g = graph([['start', {}, { then: 1 }], ['flag', { name: 'ready', value: true }], ['timer', { seconds: 1 }, { then: 3 }], ['branch', {}, { true: 4 }, [{ kind: 'flag', name: 'ready', value: true }]], ['message', { text: 'Go' }]]);
	const m = game(g);
	assert.deepEqual(lines(m.step(1.5, 2)), ['msg Go']);
});

test('spawning puts units of the team in the area', () => {
	const m = game(graph([['start', {}, { then: 1 }], ['spawn', { preset: 'AHuman|Soldier Light|Base.rte', team: 1, area: 'Drop', count: 3 }]]));
	m.run('Areas["Drop"] = { x = 200, y = 0, w = 10, h = 10 }');
	assert.equal(lines(m.step()).filter((l) => l === 'spawn Soldier Light team 1').length, 3);
	assert.equal(m.run('local n = 0 for a in MovableMan.Actors do if a.Team == 1 and a.Pos.X >= 200 then n = n + 1 end end return tostring(n)'), '3');
});

test('killing a team gibs each of its units', () => {
	const m = game(graph([['start', {}, { then: 1 }], ['killAll', { team: 1 }]]));
	m.run('MakeActor(1, 0, 0); MakeActor(1, 0, 0); MakeActor(0, 0, 0)');
	assert.equal(lines(m.step()).filter((l) => l.startsWith('gib')).length, 2);
});

// ---- stages ----

test('stages run in order: start, the goal, done, then the next', () => {
	const g = graph([
		['stageReach', { name: 'Reach', area: 'A', team: 0, text: 'Go there' }, { started: 1, done: 2, next: 3 }],
		['message', { text: 'Start 1' }],
		['message', { text: 'Done 1' }],
		['stageHold', { name: 'Hold', area: 'B', team: 0, seconds: 2 }, { started: 4, done: 5 }],
		['message', { text: 'Start 2' }],
		['win', { team: 0 }],
	]);
	const m = game(g);
	m.run('Areas["A"] = { x = 0, y = 0, w = 10, h = 10 }; Areas["B"] = { x = 100, y = 0, w = 10, h = 10 }; U = MakeActor(0, 50, 5)');
	assert.deepEqual(lines(m.step()), ['msg Start 1']);
	assert.ok(Number(m.run('return tostring(Marks)')) > 0, 'the objective is marked');
	m.run('U.Pos.X = 5');
	assert.deepEqual(lines(m.step()), ['msg Done 1']);
	assert.deepEqual(lines(m.step()), ['msg Start 2']);
	m.run('U.Pos.X = 105');
	assert.equal(m.step(1, 1), '', 'held for a second: not yet');
	m.run('E = MakeActor(1, 106, 5)');
	assert.equal(m.step(1.5, 2), '', 'an enemy in the area stops the hold');
	m.run('E.Health = 0');
	assert.deepEqual(lines(m.step(1, 4)), ['end winner 0']);
});

test('a stage that runs out of time runs its time-out chain, then goes on', () => {
	const g = graph([
		['stageSurvive', { seconds: 100, failAfter: 2 }, { timeout: 1, next: 2 }],
		['message', { text: 'Too slow' }],
		['stageSurvive', { seconds: 0.5 }, { started: 3 }],
		['message', { text: 'Second stage' }],
	]);
	const m = game(g);
	assert.equal(m.step(1), '', 'the stage starts (its clock with it)');
	assert.equal(m.step(1.5), '');
	assert.deepEqual(lines(m.step(1)), ['msg Too slow']);
	assert.deepEqual(lines(m.step()), ['msg Second stage']);
});

test('a jump goes to another stage instead of the next one', () => {
	const g = graph([
		['stageSurvive', { seconds: 1 }, { done: 1, next: 2 }],
		['jump', { target: '' }],
		['stageSurvive', { seconds: 1 }, { started: 4 }],
		['stageSurvive', { seconds: 1 }, { started: 5 }],
		['message', { text: 'Skipped to' }],
		['message', { text: 'Third' }],
	]);
	g.nodes[1].p.target = g.nodes[3].id; // jump to the third stage
	const m = game(g);
	m.step();
	assert.deepEqual(lines(m.step(1.5, 2)), ['msg Third']);
});

test('fetch: the item appears, and the stage is done when a unit carries it into the area', () => {
	const g = graph([['stageFetch', { item: 'HDFirearm|Case|Base.rte', from: 'Vault', area: 'Base' }, { done: 1 }], ['message', { text: 'Delivered' }]]);
	const m = game(g);
	m.run('Areas["Vault"] = { x = 0, y = 0, w = 10, h = 10 }; Areas["Base"] = { x = 100, y = 0, w = 10, h = 10 }; U = MakeActor(0, 50, 5)');
	assert.deepEqual(lines(m.step()), ['item Case']);
	m.run('U.inv = { Items[1] }; Items = {}; U.Pos.X = 105');
	assert.deepEqual(lines(m.step()), ['msg Delivered']);
});

test('stage conditions: a branch can ask which stage it is', () => {
	const g = graph([
		['stageSurvive', { seconds: 1 }, { next: 1 }],
		['stageSurvive', { seconds: 100 }],
		['always', {}, { then: 3 }],
		['branch', {}, { true: 4 }, []],
		['doOnce', {}, { then: 5 }],
		['message', { text: 'On stage 2' }],
	]);
	g.nodes[3].lists.conds.push({ kind: 'stage', target: g.nodes[1].id, is: true });
	const m = game(g);
	assert.equal(m.step(0.5), '');
	assert.deepEqual(lines(m.step(0.6, 3)), ['msg On stage 2']);
});

test('the script\'s own stages: a branch can ask which one it is on, and a jump moves it', () => {
	const g = graph([
		['always', {}, { then: 1 }],
		['branch', {}, { true: 2 }, []],
		['doOnce', {}, { then: 3 }],
		['message', { text: 'Bunker taken' }, { then: 4 }],
		['jump', { target: 'code:attackBrain' }],
	]);
	g.nodes[1].lists.conds.push({ kind: 'stage', target: 'code:frontBunkerCaptured', is: true });
	assert.deepEqual(F.problems(g, { codeStages: ['frontBunkerCaptured', 'attackBrain'] }), []);
	assert.match(F.problems(g, { codeStages: ['frontBunkerCaptured'] }).map((p) => p.message).join(), /no stage called attackBrain/);
	const m = game(g);
	m.run('Act.stages = { frontBunkerCaptured = 4, attackBrain = 11 }; Act.currentStage = 1');
	assert.equal(m.step(), '', 'not on that stage yet');
	m.run('Act.currentStage = 4');
	assert.deepEqual(lines(m.step()), ['msg Bunker taken']);
	assert.equal(m.run('return tostring(Act.currentStage)'), '11', 'jumped to the script\'s stage');
});

// ---- battles ----

test('battle events start rules and finish stages', () => {
	const g = graph([
		['stageBattle', { team: 0 }, { started: 1, done: 2 }],
		['battle', { mode: 2, size: 10, layout: true, seed: 4 }],
		['win', { team: 0 }],
		['battleScore', { team: 0 }, { then: 4 }],
		['message', { text: 'Scored' }],
	]);
	const m = game(g);
	assert.deepEqual(lines(m.step()), ['layout 2 4', 'battle 2 10']);
	m.run('table.insert(BattleQueue, "started 2"); table.insert(BattleQueue, "score 1 1")');
	assert.equal(m.step(), '', 'the other team scoring is not ours');
	m.run('table.insert(BattleQueue, "score 0 1")');
	assert.deepEqual(lines(m.step()), ['msg Scored']);
	m.run('table.insert(BattleQueue, "ended 0")');
	assert.deepEqual(lines(m.step()), ['end winner 0']);
});

test('on a game without battle modes the battle step says so and nothing breaks', () => {
	const m = game(graph([['start', {}, { then: 1 }], ['battle', { mode: 1 }]]), { noBattle: true });
	assert.match(m.step(), /console Studio: this game build has no battle modes/);
	assert.equal(m.step(0.1, 3), '');
});

// ---- the model ----

test('wires: no loops, nothing into an event, next only to a stage', () => {
	const g = F.create();
	const e = F.addNode(g, 'start', 0, 0);
	const a = F.addNode(g, 'message', 0, 0);
	const b = F.addNode(g, 'message', 0, 0);
	const s = F.addNode(g, 'stageSurvive', 0, 0);
	assert.equal(F.connect(g, e.id, 'then', a.id), '');
	assert.equal(F.connect(g, a.id, 'then', b.id), '');
	assert.match(F.connect(g, b.id, 'then', a.id), /loop/);
	assert.match(F.connect(g, a.id, 'then', e.id), /starts a chain/);
	assert.match(F.connect(g, s.id, 'next', a.id), /has to be a stage/);
	assert.match(F.connect(g, a.id, 'nope', b.id), /does not exist/);
});

test('removing a step joins up the chain when asked', () => {
	const g = graph([['start', {}, { then: 1 }], ['message', { text: 'a' }, { then: 2 }], ['message', { text: 'b' }]]);
	F.removeNode(g, g.nodes[1].id, true);
	assert.equal(g.nodes[0].out.then, g.nodes[1].id);
	F.removeNode(g, g.nodes[1].id, false);
	assert.equal(g.nodes[0].out.then, undefined);
});

test('problems: missing areas, things that never run, an event with nothing after it', () => {
	const g = graph([['enter', { area: 'Nowhere' }, {}], ['message', {}]]);
	const found = F.problems(g, { areas: ['Gate'] });
	assert.ok(found.some((p) => p.id === g.nodes[0].id && /no area called "Nowhere"/.test(p.message)));
	assert.ok(found.some((p) => p.id === g.nodes[0].id && /does nothing/.test(p.message)));
	assert.ok(found.some((p) => p.id === g.nodes[1].id && /never runs/.test(p.message)));
});

// ---- the older data, and the script ----

test('older rules and stages become a graph that behaves the same', () => {
	const old = {
		rules: [
			// a timer with a condition, fired once: it waited until the condition held
			{ name: 'Late gold', once: true, when: { kind: 'timer', seconds: 1 }, ifs: [{ kind: 'funds', team: 0, op: '>=', amount: 100 }], then: [{ kind: 'message', text: 'Paid' }] },
			{ name: 'Every entry', once: false, when: { kind: 'enter', area: 'G', team: -1 }, ifs: [], then: [{ kind: 'message', text: 'Hi' }] },
		],
		stages: [
			{ name: 'One', goal: { kind: 'survive', seconds: 1 }, text: '', marker: false, viewer: 0, failAfter: 0, onStart: [], onDone: [{ kind: 'message', text: 'One done' }], onFail: [] },
			{ name: 'Two', goal: { kind: 'survive', seconds: 1 }, text: '', marker: false, viewer: 0, failAfter: 0, onStart: [{ kind: 'message', text: 'Two begins' }], onDone: [{ kind: 'stage', n: 1 }], onFail: [] },
		],
	};
	const g = F.migrate(old);
	assert.equal(F.problems(g, { areas: ['G'] }).filter((p) => p.level === 'error').length, 0);
	const m = game(g);
	m.run('Areas["G"] = { x = 0, y = 0, w = 10, h = 10 }');
	assert.equal(m.step(0.5), '', 'stage one starts');
	assert.deepEqual(lines(m.step(1.1)), ['msg One done'], 'past 1 s: stage one done, and the gold timer waits for its condition');
	assert.deepEqual(lines(m.step()), ['msg Two begins']);
	m.run('Act.Funds[0] = 150');
	assert.deepEqual(lines(m.step(0.1, 3)), ['msg Paid']);
	assert.ok(lines(m.step(1, 3)).includes('msg One done'), 'stage two jumps back to stage one, which runs again');
});

test('writing the script: the block goes before UpdateActivity, which runs it; written twice it is the same; your functions are added and kept', () => {
	const script = 'function Test:StartActivity(isNewGame)\nend\n\nfunction Test:UpdateActivity()\n\tself:Mine()\nend\n\nfunction Test:Mine()\nend\n';
	const g = graph([['start', {}, { then: 1 }], ['call', { fn: 'Opening' }]]);
	const once = F.apply(script, 'Test', g);
	const twice = F.apply(once, 'Test', g);
	assert.equal(twice, once);
	assert.ok(once.indexOf(F.MARK_BEGIN) < once.indexOf('function Test:UpdateActivity()'));
	assert.match(once, /function Test:UpdateActivity\(\)\n\tself:RunStudioFlow\(\)/);
	assert.match(once, /function Test:Opening\(actor\)/);
	assert.match(once, /function Test:Mine\(\)/);
	assert.deepEqual(F.parse(once), JSON.parse(JSON.stringify(g)));
});

test('a script from the first versions (rules block, trigger block) is converted and their calls removed', () => {
	const old = 'function Test:UpdateActivity()\n\tself:RunStudioRules()\n\tself:RunStudioTriggers()\nend\n-- >>> Studio rules: x\n-- studio-rules-data: {"v":2,"rules":[{"name":"R","once":false,"when":{"kind":"start"},"ifs":[],"then":[{"kind":"message","text":"Old"}]}],"stages":[]}\nfunction Test:RunStudioRules() end\n-- <<< Studio rules\n';
	const g = F.parse(old);
	assert.ok(g && g.nodes.some((n) => n.kind === 'message' && n.p.text === 'Old'));
	const text = F.apply(old, 'Test', g);
	assert.doesNotMatch(text, /RunStudioRules|RunStudioTriggers|studio-rules-data/);
	const m = game(g);
	assert.deepEqual(lines(m.step()), ['msg Old']);
});

test('every kind of node compiles to Lua that loads', () => {
	const g = F.create();
	for (const name of F.ORDER) {
		const n = F.addNode(g, name, 0, 0);
		if (n.p.area !== undefined) {
			n.p.area = 'A';
		}
		if (n.p.from !== undefined) {
			n.p.from = 'A';
		}
	}
	const m = game(g);
	assert.ok(m.block.length > 1000);
});

// ---- variables and values ----

const V = (n, extra) => Object.assign({ k: 'num', n }, extra || {});

test('variables: set, add and multiply them, test them, and fire when one reaches a value', () => {
	const g = graph([
		['start', {}, { then: 1 }],
		['setVar', { name: 'count', op: 'set', value: V(2) }, { then: 2 }],
		['setVar', { name: 'count', op: 'mul', value: V(3) }],
		['timer', { seconds: 1, repeat: true }, { then: 4 }],
		['setVar', { name: 'count', op: 'add', value: V(1) }],
		['varReaches', { name: 'count', op: '>=', value: V(8) }, { then: 6 }],
		['message', { text: 'Eight' }],
	]);
	const m = game(g);
	m.step();
	assert.equal(m.run('return tostring(Act.StudioState.vars["count"])'), '6');
	assert.equal(m.step(1, 1), '');
	assert.deepEqual(lines(m.step(1, 3)), ['msg Eight'], 'fires once, as it gets there');
});

test('values: a scenario setting, and times the difficulty', () => {
	const g = graph([['start', {}, { then: 1 }], ['gold', { team: 0, amount: V(0, { k: 'setting', name: 'Bonus', n: 100 }) }, { then: 2 }], ['gold', { team: 0, amount: V(100, { x: true }) }]]);
	const m = game(g);
	m.run('Settings["Bonus"] = 250; Act.Difficulty = 90');
	// (the test's Lua writes 175 as 175.0; the game's writes 175)
	assert.deepEqual(lines(m.step()).map((l) => l.replace('.0 ', ' ')), ['gold 250 to 0', 'gold 175 to 0'], '100 times 1.75 at nuts difficulty');
});

// ---- capture points ----

function captureGame(extra) {
	const g = graph([
		['capture', Object.assign({ name: 'Ops', area: 'Ops', region: 'Bunker', owner: 1, attackers: 0, seconds: V(2), decays: true, active: true, marker: true, text: 'Capture' }, extra || {}), { taken: 1, retaken: 2 }],
		['message', { text: 'Taken' }],
		['message', { text: 'Lost' }],
		['captureChanged', { target: '', team: -1 }, { then: 4 }],
		['setVar', { name: 'changes', op: 'add', value: V(1) }],
	]);
	g.nodes[3].p.target = g.nodes[0].id;
	const m = game(g);
	m.run('Areas["Ops"] = { x = 0, y = 0, w = 10, h = 10 }; Areas["Bunker"] = { x = 0, y = 0, w = 100, h = 100 }');
	return m;
}

test('a capture point is taken by standing in it, over its seconds, and taken back the same way', () => {
	const m = captureGame();
	m.run('A = MakeActor(0, 500, 5)');
	m.step();
	m.run('A.Pos.X = 5');
	assert.equal(m.step(0.5, 3), '', 'not yet');
	assert.ok(/Capture \d+%/.test(m.run('return LastMark')), 'the marker shows the progress');
	assert.deepEqual(lines(m.step(0.5, 2)), ['msg Taken']);
	assert.equal(m.run('return tostring(Act.StudioState.cap["n1"].owner)'), '0');
	m.run('A.Health = 0; D = MakeActor(1, 5, 5)');
	assert.deepEqual(lines(m.step(0.5, 6)), ['msg Lost'], 'the defenders take it back');
	assert.equal(m.run('return tostring(Act.StudioState.vars["changes"])'), '2', 'the change of hands event fired both times');
});

test('a defender anywhere in its area stops a capture starting; progress drains when the attackers leave', () => {
	const m = captureGame();
	m.run('A = MakeActor(0, 5, 5); D = MakeActor(1, 50, 50)');
	assert.equal(m.step(0.5, 8), '', 'a defender in the bunker: no capture');
	m.run('D.Health = 0');
	m.step(0.5, 2);
	m.run('A.Pos.X = 500');
	m.step(0.5, 4);
	assert.equal(m.run('return tostring(Act.StudioState.cap["n1"].progress)'), '0', 'drained away');
});

test('a capture point that is switched off does nothing until it is switched on; a stage can wait for one', () => {
	const g = graph([
		['capture', { name: 'Ops', area: 'Ops', region: '', owner: 1, attackers: 0, seconds: V(1), decays: false, active: false, marker: false, text: '' }, {}],
		['stageCapture', { target: '', team: 0 }, { done: 2 }],
		['message', { text: 'Stage done' }],
		['timer', { seconds: 2 }, { then: 4 }],
		['captureSet', { target: '', do: 'on' }],
	]);
	g.nodes[1].p.target = g.nodes[0].id;
	g.nodes[4].p.target = g.nodes[0].id;
	const m = game(g);
	m.run('Areas["Ops"] = { x = 0, y = 0, w = 10, h = 10 }; A = MakeActor(0, 5, 5)');
	assert.equal(m.step(0.5, 4), '', 'switched off');
	assert.deepEqual(lines(m.step(0.5, 4)), ['msg Stage done'], 'switched on at 2 s, taken a second later');
});

// ---- spawning and orders ----

test('spawning a random unit of a group in each box of an area, tagged and sent somewhere', () => {
	const g = graph([['start', {}, { then: 1 }], ['spawn', { source: 'group', group: 'Actors - Heavy', team: 1, area: 'Posts', perBox: true, ai: 'goto', goTo: 'Gate', tag: 'Guards', arrive: 'appear', facing: 'left' }]]);
	const m = game(g);
	m.run('Areas["Posts"] = { x = 0, y = 0, w = 10, h = 10, more = { { x = 50, y = 0, w = 10, h = 10 }, { x = 90, y = 0, w = 10, h = 10 } } }; Areas["Gate"] = { x = 500, y = 0, w = 10, h = 10 }');
	assert.equal(lines(m.step()).filter((l) => l === 'spawn Random Actors - Heavy team 1').length, 3);
	assert.equal(m.run('local n = 0 for a in MovableMan.Actors do if a.groups["Guards"] and a.AIMode == Actor.AIMODE_GOTO and a.waypoints and a.waypoints[1].X == 505 and a.HFlipped then n = n + 1 end end return tostring(n)'), '3');
});

test('spawning by dropship puts the units in one craft', () => {
	const m = game(graph([['start', {}, { then: 1 }], ['spawn', { source: 'loadout', loadout: 'Infantry Heavy', team: 1, area: 'LZ', count: V(4), arrive: 'dropship', ai: 'brainhunt' }]]));
	m.run('Areas["LZ"] = { x = 0, y = 0, w = 10, h = 10 }');
	assert.deepEqual(lines(m.step()), ['spawn Dropship team 1 carrying 4']);
});

test('orders: a group, a whole team, or a team in an area', () => {
	const g = graph([['start', {}, { then: 1 }], ['order', { who: 'area', team: 1, area: 'Room', ai: 'goto', goTo: 'Gate' }, { then: 2 }], ['order', { who: 'group', group: 'Hunters', ai: 'brainhunt' }]]);
	const m = game(g);
	m.run('Areas["Room"] = { x = 0, y = 0, w = 10, h = 10 }; Areas["Gate"] = { x = 500, y = 0, w = 10, h = 10 }; In = MakeActor(1, 5, 5); Out = MakeActor(1, 50, 50); H = MakeActor(0, 50, 50); H.groups["Hunters"] = true');
	m.step();
	assert.equal(m.run('return tostring(In.AIMode) .. " " .. tostring(Out.AIMode) .. " " .. tostring(H.AIMode)'), '3 nil 4');
});

// ---- the world ----

test('doors: opened, locked and given to a team', () => {
	const g = graph([['start', {}, { then: 1 }], ['doors', { area: 'Front', do: 'open', team: 0 }, { then: 2 }], ['doors', { area: 'Front', do: 'lock', team: -1 }]]);
	const m = game(g);
	m.run('Areas["Front"] = { x = 0, y = 0, w = 100, h = 100 }; D = MakeActor(1, 5, 5); D.isDoor = true; Doors = { D }; Far = MakeActor(1, 500, 5); Far.isDoor = true; table.insert(Doors, Far)');
	assert.deepEqual(lines(m.step()), ['open door 1']);
	assert.equal(m.run('return tostring(D.Team) .. " " .. tostring(D.Status) .. " " .. tostring(Far.Team)'), '0 5 1');
});

test('fog of war, landing zones, the buy menu and gold', () => {
	const g = graph([['start', {}, { then: 1 }], ['fog', { do: 'hideAll', team: 0 }, { then: 2 }], ['fog', { do: 'reveal', area: 'Base', team: 0 }, { then: 3 }], ['lz', { team: 0, area: 'Base' }, { then: 4 }], ['buyMenu', { on: false }, { then: 5 }], ['setFunds', { team: 0, amount: V(300) }]]);
	const m = game(g);
	m.run('Areas["Base"] = { x = 10, y = 20, w = 10, h = 10 }');
	assert.deepEqual(lines(m.step()), ['hide all for 0', 'reveal 10,20 for 0', 'lz for 0']);
	assert.equal(m.run('return tostring(Act.BuyMenuEnabled) .. " " .. tostring(Act.Funds[0])'), 'false 300');
});

test('income comes in a little every moment', () => {
	const m = game(graph([['start', {}, { then: 1 }], ['income', { team: 0, amount: V(6) }]]));
	m.run('Act.Funds[0] = 0');
	m.step(0.5, 21);
	assert.equal(m.run('return tostring(Act.Funds[0])'), '60');
});

test('dialogue: lines one after another, each when the one before has had its time', () => {
	const g = graph([['start', {}, { then: 1 }], ['dialogue', { who: 'all' }]]);
	g.nodes[1].lists.lines = [{ text: 'First', seconds: 2 }, { text: 'Second', seconds: 3 }, { text: 'Third', seconds: 1 }];
	const m = game(g);
	assert.deepEqual(lines(m.step()), ['msg First']);
	assert.equal(m.step(1, 1), '');
	assert.deepEqual(lines(m.step(1, 1)), ['msg Second']);
	assert.equal(m.step(1, 2), '');
	assert.deepEqual(lines(m.step(1, 1)), ['msg Third']);
});

test('a win that waits: the message shows, and the game ends after the seconds', () => {
	const m = game(graph([['start', {}, { then: 1 }], ['win', { team: 0, after: 3, text: 'Victory' }]]));
	assert.deepEqual(lines(m.step()), ['msg Victory']);
	assert.equal(m.step(1, 2), '');
	assert.deepEqual(lines(m.step(1, 2)), ['end winner 0']);
});

// ---- saved games ----

test('a saved game keeps the mission: the stage, variables, flags, capture points and what has already run', () => {
	const g = graph([
		['start', {}, { then: 1 }],
		['message', { text: 'Opening' }, { then: 2 }],
		['setVar', { name: 'score', op: 'set', value: V(5) }],
		['stageSurvive', { seconds: 1 }, { done: 4, next: 5 }],
		['message', { text: 'One done' }],
		['stageSurvive', { seconds: 100 }, { started: 6 }],
		['flag', { name: 'second', value: true }],
		['capture', { name: 'P', area: 'P', region: '', owner: 1, attackers: 0, seconds: V(10), decays: false, active: true, marker: false, text: '' }, {}],
	]);
	const m = game(g);
	m.run('Areas["P"] = { x = 0, y = 0, w = 10, h = 10 }; A = MakeActor(0, 5, 5)');
	m.step(0.6, 4);
	m.run('Act:StudioSave()');
	const saved = m.run('return Saved["StudioFlow"]');
	assert.ok(saved.length > 20 && !/[\r\n]/.test(saved), 'saved as one line');
	// a new game is loaded: a fresh activity reads the save
	m.run('Old = Act; Act = setmetatable({ Funds = {} }, { __index = Test }); Act:StudioLoad()');
	assert.equal(m.run('local st = Act.StudioState return st.stage == Old.StudioState.stage and tostring(st.vars["score"]) .. " " .. tostring(st.flags["second"]) or "wrong stage"'), '5 true');
	assert.ok(Number(m.run('return tostring(Act.StudioState.cap["n8"].progress)')) > 0, 'the capture point carries on from where it was');
	assert.equal(m.step(0.1, 3), '', 'nothing runs again: no opening message, stage one not done twice');
});

test('the script saves and loads the mission: OnSave and StartActivity are hooked once, and created when missing', () => {
	const g = graph([['start', {}, { then: 1 }], ['message', { text: 'Hi' }]]);
	const withBoth = 'function Test:StartActivity(newGame)\n\tself.x = 1\nend\n\nfunction Test:OnSave()\n\tself:SaveNumber("x", 1)\nend\n\nfunction Test:UpdateActivity()\nend\n';
	const once = F.apply(withBoth, 'Test', g);
	assert.match(once, /function Test:StartActivity\(newGame\)\n\tif not newGame then self:StudioLoad\(\) end/);
	assert.match(once, /function Test:OnSave\(\)\n\tself:StudioSave\(\)/);
	assert.equal(F.apply(once, 'Test', g), once, 'written twice it is the same');
	const bare = F.apply('function Test:UpdateActivity()\nend\n', 'Test', g);
	assert.match(bare, /function Test:OnSave\(\)\n\tself:StudioSave\(\)/);
	assert.match(bare, /function Test:StartActivity\(isNewGame\)\n\tif not isNewGame then self:StudioLoad\(\) end/);
});
