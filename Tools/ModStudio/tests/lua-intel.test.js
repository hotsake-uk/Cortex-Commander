// Tests for what the Lua editor knows (js/lua-intel.js): go to definition, completion, hover and signature help, on small scripts
// that use each other the way mods do (require, dofile, an activity's class split over methods). Run with: npm test
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

globalThis.window = globalThis;
window.MS = {};
vm.runInThisContext(fs.readFileSync(new URL('../vendor/luaparse/luaparse.js', import.meta.url), 'utf8'), { filename: 'luaparse.js' });
vm.runInThisContext(fs.readFileSync(new URL('../js/lua-intel.js', import.meta.url), 'utf8'), { filename: 'lua-intel.js' });
const LuaIntel = window.MS.LuaIntel;

// ---- a few mods' worth of scripts ----

const MISSION = `require("Lib")
dofile("Base.rte/Scripts/Util.lua")

-- Sends the next wave of attackers.
---@param size number How many come.
function MyMission:SpawnWave(size)
	self.wave = self.wave + 1
	local h = CreateAHuman("Ronin Soldier", "Ronin.rte")
	h:GetController()
	LibHelper(self.wave)
	MovableMan:AddActor(h)
	AudioMan:PlaySound("Base.rte/Sounds/Boom.wav")
end

function MyMission:StartActivity(isNewGame)
	self.timer = Timer()
	self.wave = 1
	self.LZ = require("Activities/LZ")
	self:SpawnWave(3)
end

local count = 0
local function bump(n)
	local step = n or 1
	count = count + step
	return count
end

function MyMission:UpdateActivity()
	if self.timer:IsPastSimMS(100) then
		self:SpawnWave(bump(2))
	end
	local spot = self.LZ.Find(self.wave)
	local clamped = Util.Clamp(spot)
	for i = 1, clamped do
		local h = i
	end
end
`;
const LIB = `function LibHelper(wave)
	return wave * 2
end
`;
const EXTRA = `function MyMission:Extra()
	self.bonus = 3
end
`;
const UTIL = `Util = {}
function Util.Clamp(v, low, high)
	return v
end
`;
const LZ = `local M = {}
function M.Find(wave)
	return wave
end
return M
`;
const PATH = 'MyMod.rte/Mission.lua';

const PRESETS = [
	{ name: 'Ronin Soldier', cls: 'AHuman', module: 'Ronin.rte', doc: { path: 'Ronin.rte/Actors.ini' }, node: { line: 12, children: [] }, topLevel: true },
	{ name: 'Boom', cls: 'SoundContainer', module: 'Base.rte', doc: { path: 'Base.rte/Sounds.ini' }, node: { line: 3, children: [] }, topLevel: true },
];
const lib = {
	presets: PRESETS,
	modules: [{ name: 'Ronin.rte' }, { name: 'Base.rte' }, { name: 'MyMod.rte' }],
	docs: new Map(),
	find(name, cls) {
		const plain = name.includes('/') ? name.slice(name.indexOf('/') + 1) : name;
		return PRESETS.find((p) => p.name === plain && (!cls || p.cls === cls)) || null;
	},
};

const intel = new LuaIntel({ lib });
intel.api = JSON.parse(fs.readFileSync(new URL('../data/lua-api.json', import.meta.url), 'utf8'));
intel.setApiText(fs.readFileSync(new URL('../data/lua-api.d.lua', import.meta.url), 'utf8'));
const scripts = [
	[PATH, MISSION],
	['MyMod.rte/Lib.lua', LIB],
	['MyMod.rte/MissionExtra.lua', EXTRA],
	['Base.rte/Scripts/Util.lua', UTIL],
	['MyMod.rte/Activities/LZ.lua', LZ],
];
intel.setScripts(new Map(scripts.map(([path, text]) => [path, { path: 'Mods/' + path, text }])));

// The offset of the nth match of "find" in text, plus "into" characters.
function at(text, find, into = 0, nth = 0) {
	let i = -1;
	for (let n = 0; n <= nth; n++) {
		i = text.indexOf(find, i + 1);
		assert.ok(i >= 0, 'no "' + find + '" (' + n + ') in the script');
	}
	return i + into;
}
// Go to definition on the word at an offset.
function def(text, offset, path = PATH, opts) {
	let start = offset;
	let end = offset;
	while (start > 0 && /\w/.test(text[start - 1])) {
		start--;
	}
	while (end < text.length && /\w/.test(text[end])) {
		end++;
	}
	return intel.definition(path, text, start, end, opts);
}
const labels = (items) => items.map((i) => i.label);
const lineOf = (text, find) => text.slice(0, text.indexOf(find)).split('\n').length;

// ---- go to definition ----

test('locals and upvalues go to their declaration', () => {
	assert.deepEqual(def(MISSION, at(MISSION, 'count + step', 1)), { offset: at(MISSION, 'local count', 6) });
	assert.deepEqual(def(MISSION, at(MISSION, 'step\n', 1)), { offset: at(MISSION, 'local step', 6) });
	assert.deepEqual(def(MISSION, at(MISSION, 'bump(2)', 1)), { offset: at(MISSION, 'function bump', 9) });
	assert.deepEqual(def(MISSION, at(MISSION, 'or 1', 0) - 2), { offset: at(MISSION, 'bump(n', 5) }); // the parameter n
	assert.deepEqual(def(MISSION, at(MISSION, 'clamped do')), { offset: at(MISSION, 'local clamped', 6) });
});

test('a local in an inner scope is not the one outside it', () => {
	// "h" in the for loop is its own local, not SpawnWave's
	assert.deepEqual(def(MISSION, at(MISSION, 'local h = i', 6)), { offset: at(MISSION, 'local h = i', 6) });
	assert.deepEqual(def(MISSION, at(MISSION, 'h:GetController', 0)), { offset: at(MISSION, 'local h = Create', 6) });
});

test('self:Method and Class:Method go to the method', () => {
	const target = { offset: at(MISSION, 'MyMission:SpawnWave', 10) };
	assert.deepEqual(def(MISSION, at(MISSION, 'self:SpawnWave(3)', 6)), target);
	assert.deepEqual(def(MISSION, at(MISSION, 'self:SpawnWave(bump', 6)), target);
});

test('a field set on self goes to its first assignment', () => {
	const first = { offset: at(MISSION, 'self.wave = 1', 5) };
	assert.deepEqual(def(MISSION, at(MISSION, 'self.wave)', 5)), first);
	assert.deepEqual(def(MISSION, at(MISSION, 'self.timer:Is', 5)), { offset: at(MISSION, 'self.timer =', 5) });
});

test('globals other scripts define, including required and dofile\'d ones', () => {
	assert.deepEqual(def(MISSION, at(MISSION, 'LibHelper(self', 1)), { path: 'MyMod.rte/Lib.lua', line: 1, column: 10 });
	assert.deepEqual(def(MISSION, at(MISSION, 'Util.Clamp', 6)), { path: 'Base.rte/Scripts/Util.lua', line: 2, column: 15 });
	// a field holding what require returned: the module's own table
	assert.deepEqual(def(MISSION, at(MISSION, 'LZ.Find', 4)), { path: 'MyMod.rte/Activities/LZ.lua', line: 2, column: 12 });
	// a method another file adds to the same class
	const extra = MISSION + '\nfunction MyMission:Later()\n\tself:Extra()\nend\n';
	assert.deepEqual(def(extra, at(extra, ':Extra()', 2)), { path: 'MyMod.rte/MissionExtra.lua', line: 1, column: 20 });
});

test('the game\'s API goes to its entry in the API file', () => {
	const api = intel.apiText;
	const g = def(MISSION, at(MISSION, 'GetController', 2));
	assert.equal(g.api, true);
	assert.match(api.split('\n')[g.line - 1], /function Actor:GetController\(/);
	const mm = def(MISSION, at(MISSION, 'MovableMan:', 2));
	assert.match(api.split('\n')[mm.line - 1], /^MovableMan = /);
	const add = def(MISSION, at(MISSION, 'AddActor', 2));
	assert.match(api.split('\n')[add.line - 1], /AddActor/);
	const create = def(MISSION, at(MISSION, 'CreateAHuman', 2));
	assert.match(api.split('\n')[create.line - 1], /function CreateAHuman\(/);
	// a method of the activity the game defines
	const t = MISSION + '\nfunction MyMission:Other()\n\tlocal d = self.Difficulty\nend\n';
	assert.match(api.split('\n')[def(t, at(t, 'Difficulty', 2)).line - 1], /---@field Difficulty/);
});

test('names in strings: presets, sounds, pictures and scripts', () => {
	assert.equal(def(MISSION, at(MISSION, 'Ronin Soldier', 2)).asset.preset.name, 'Ronin Soldier');
	assert.deepEqual(def(MISSION, at(MISSION, 'Boom.wav', 1)), { asset: { kind: 'sound', path: 'Base.rte/Sounds/Boom.wav' } });
	assert.deepEqual(def(MISSION, at(MISSION, 'Scripts/Util', 2)), { path: 'Base.rte/Scripts/Util.lua', line: 1 });
	assert.deepEqual(def(MISSION, at(MISSION, '"Lib"', 2)), { path: 'MyMod.rte/Lib.lua', line: 1 });
	assert.deepEqual(def(MISSION, at(MISSION, 'Activities/LZ', 2)), { path: 'MyMod.rte/Activities/LZ.lua', line: 1 });
	const pic = 'local s = CreateMOSRotating("Thing")\nlocal p = "MyMod.rte/Images/Thing.png"\n';
	assert.deepEqual(def(pic, at(pic, 'Thing.png'), PATH), { asset: { kind: 'image', path: 'MyMod.rte/Images/Thing.png' } });
	assert.equal(intel.definition(PATH, 'local s = CreateSoundContainer("Boom", "Base.rte")', 33, 35).asset.preset.cls, 'SoundContainer');
	assert.match(intel.hover(PATH, MISSION, at(MISSION, 'Ronin Soldier', 2), at(MISSION, 'Ronin Soldier', 7)), /Ronin Soldier.*AHuman/);
});

test('a script that doesn\'t parse still finds locals and globals', () => {
	const broken = MISSION + '\nfunction MyMission:Broken()\n\tlocal x = (\n\tLibHelper(x\n';
	assert.equal(def(broken, at(broken, 'LibHelper(x', 1)).path, 'MyMod.rte/Lib.lua');
});

// ---- completion ----

test('self: in a class method lists its methods, from this script and others', () => {
	const offset = at(MISSION, 'self:SpawnWave(bump', 5);
	const text = MISSION.slice(0, offset) + '\n' + MISSION.slice(at(MISSION, '(bump'));
	const items = labels(intel.complete(PATH, text, offset));
	for (const name of ['SpawnWave', 'StartActivity', 'UpdateActivity', 'Extra']) {
		assert.ok(items.includes(name), name + ' in ' + items.slice(0, 20).join(', '));
	}
	assert.ok(items.includes('GetTeamFunds'), 'and the game\'s activity methods');
	assert.ok(!items.includes('wave'), 'no fields after a colon');
});

test('self. in a class method lists fields set in any of its methods', () => {
	const offset = at(MISSION, 'self.wave)', 5);
	const items = labels(intel.complete(PATH, MISSION, offset));
	for (const name of ['wave', 'timer', 'LZ', 'bonus', 'Difficulty']) {
		assert.ok(items.includes(name), name);
	}
});

test('fields of what require returned, and of a table a local holds', () => {
	const text = MISSION.replace('local spot = self.LZ.Find(self.wave)', 'local spot = self.LZ.');
	const offset = at(text, 'self.LZ.', 8);
	assert.ok(labels(intel.complete(PATH, text, offset)).includes('Find'));
	const local = 'local T = {}\nfunction T.Go(a, b) end\nT.size = 1\nlocal x = T.';
	assert.deepEqual(labels(intel.complete(PATH, local, local.length)).filter((l) => l === 'Go' || l === 'size').sort(), ['Go', 'size']);
});

test('names in scope: locals here, not ones in other functions', () => {
	const offset = at(MISSION, 'local clamped');
	const items = labels(intel.complete(PATH, MISSION, offset));
	assert.ok(items.includes('spot') && items.includes('count') && items.includes('bump'));
	assert.ok(!items.includes('step'), 'step is bump\'s own');
	assert.ok(!items.includes('isNewGame'), 'isNewGame is StartActivity\'s');
});

// ---- a snippet: just a function's body, edited somewhere else ----

test('a snippet of a script reads self as the script\'s activity', () => {
	const body = 'local n = 2\nself:';
	const items = labels(intel.complete(PATH, body, body.length));
	assert.ok(items.includes('SpawnWave') && items.includes('GetTeamFunds'));
	const dot = 'if self.';
	const fields = labels(intel.complete(PATH, dot, dot.length));
	assert.ok(fields.includes('wave') && fields.includes('Difficulty'));
	// and goes to the script's own definitions
	const call = 'self:SpawnWave(1)';
	assert.deepEqual(def(call, 6), { path: PATH, line: lineOf(MISSION, 'function MyMission:SpawnWave'), column: 20 });
	assert.match(intel.hover(PATH, call, 5, 14), /MyMission:SpawnWave\(size\)/);
	assert.equal(intel.signatureAt(PATH, 'self:SpawnWave(', 15).signatures[0].label, 'MyMission:SpawnWave(size: any)');
	// the script's top-level locals are there too
	assert.ok(labels(intel.complete(PATH, 'local x = ', 10)).includes('bump'));
});

// ---- hover and signature help ----

test('hover shows a function\'s signature and the comment above it', () => {
	const md = intel.hover(PATH, MISSION, at(MISSION, 'self:SpawnWave(3)', 5), at(MISSION, 'self:SpawnWave(3)', 14));
	assert.match(md, /function MyMission:SpawnWave\(size\)/);
	assert.match(md, /Sends the next wave/);
	assert.match(md, /`size` \(number\): How many come/);
	const local = intel.hover(PATH, MISSION, at(MISSION, 'bump(2)'), at(MISSION, 'bump(2)', 4));
	assert.match(local, /function bump\(n\)/);
	const api = intel.hover(PATH, MISSION, at(MISSION, 'GetController'), at(MISSION, 'GetController', 13));
	assert.match(api, /Actor:GetController\(\)/);
	const lib = intel.hover(PATH, MISSION, at(MISSION, 'LibHelper(self'), at(MISSION, 'LibHelper(self', 9));
	assert.match(lib, /function LibHelper\(wave\)/);
	assert.match(lib, /MyMod.rte\/Lib.lua:1/);
});

test('signature help for a script\'s own functions and methods', () => {
	const text = MISSION.replace('self:SpawnWave(3)', 'self:SpawnWave(');
	const help = intel.signatureAt(PATH, text, at(text, 'self:SpawnWave(', 15));
	assert.equal(help.signatures[0].label, 'MyMission:SpawnWave(size: any)');
	const u = MISSION.replace('Util.Clamp(spot)', 'Util.Clamp(spot, ');
	assert.equal(intel.signatureAt(PATH, u, at(u, 'Clamp(spot, ', 12)).activeParam, 1);
	assert.match(intel.signatureAt(PATH, u, at(u, 'Clamp(spot, ', 12)).signatures[0].label, /Util.Clamp\(v: any, low: any, high: any\)/);
});

// ---- every script the game ships parses ----

test('the analysis reads the game\'s own scripts', { skip: !fs.existsSync('C:/Cortex/CortexVersions/Studio/Data/Missions.rte/Activities/DecisionDay.lua') }, () => {
	const text = fs.readFileSync('C:/Cortex/CortexVersions/Studio/Data/Missions.rte/Activities/DecisionDay.lua', 'utf8');
	const big = new LuaIntel({ lib: { presets: [], modules: [], docs: new Map(), find: () => null } });
	big.api = intel.api;
	big.setApiText(intel.apiText);
	big.setScripts(new Map([['Data/Missions.rte/Activities/DecisionDay.lua', { path: 'Data/Missions.rte/Activities/DecisionDay.lua', text }]]));
	const path = 'Missions.rte/Activities/DecisionDay.lua';
	const call = text.indexOf('self:SetupFogOfWar()');
	const found = big.definition(path, text, call + 5, call + 18);
	assert.equal(text.slice(found.offset, found.offset + 13), 'SetupFogOfWar');
	const items = labels(big.complete(path, text, call + 5));
	assert.ok(items.includes('SpawnCraft') && items.includes('UpdateCamera'));
});
