// Cortex Mod Studio: mission logic. A scenario's rules, built from blocks and turned into Lua: WHEN something happens, IF some conditions hold, THEN do
// some actions. No code is needed for the common things (a message, gold, spawning a squad, winning); for anything else an action calls a
// function of your own, which Studio adds to the script for you to fill in, and the editor jumps to it (or opens it beside).
//   When   the scenario starts, after some seconds (or every so many), a unit enters / leaves / empties an area, a team has no units left,
//          a team's gold reaches an amount, or every moment
//   If     gold, time, units in an area, units in all, a flag you set earlier
//   Then   show a message, give or take gold, spawn units in an area, set a flag, kill a team's units, end with a winner or a draw, call your function
// The rules are kept in the script inside a marked block, as data on one comment line, with the Lua generated from it. Change the rules here, not in
// the block (it is rewritten each time); your own functions outside it are never touched. Older "Studio triggers" blocks are turned into rules.
//
// Stages chain objectives into a mission: each stage has a goal (reach an area, hold it, destroy a team or its brain, bring an item to an area, survive),
// an objective marker for the players, what happens when it starts, when it is done (the next stage then begins), and optionally a time limit with
// what happens when it runs out. "Reach the bunker, then hold the hill for two minutes, then bring the case back, then win" is four stages. Rules run
// alongside the stages and can look at which stage it is.
//
// Battles: the game's own battle modes (capture the flag, king of the hill, assault, last team standing, VIP hunt, one flag, capture the ship, wave defence,
// escort, extraction, the point) can be started by an action, and what happens in them (a score, an objective taken, the end) can start a rule or finish a
// stage. The game runs the battle, with its objectives lit up and its AI; the mission only says when it begins and what follows. This needs a game built
// with the battle events (SandboxBattleEvent); on an older build the actions say so in the console and do nothing.
'use strict';
window.MS = window.MS || {};

(function () {
	const app = () => MS.app;
	const el = (tag, props, ...children) => {
		const e = document.createElement(tag);
		Object.assign(e, props || {});
		for (const c of children.flat()) {
			if (c !== null && c !== undefined && c !== false) {
				e.append(c);
			}
		}
		return e;
	};

	const MARK_BEGIN = '-- >>> Studio rules';
	const MARK_END = '-- <<< Studio rules';
	const OLD_BEGIN = '-- >>> Studio triggers';
	const OLD_END = '-- <<< Studio triggers';
	const DATA = '-- studio-rules-data: ';

	// ---- the blocks ----
	// Each kind: label (a sentence with the parameters after it), and the parameters: [key, type, label, default]. Types: team, area, number, text, op,
	// bool, player, fn, preset.

	const BATTLE_MODES = [[1, 'Capture the flag'], [2, 'King of the hill'], [3, 'Assault'], [4, 'Last team standing'], [5, 'VIP hunt'], [6, 'One flag'], [7, 'Capture the ship'], [8, 'Wave defence'], [9, 'Escort'], [10, 'Extraction'], [11, 'The point']];

	const OPS = [['>=', 'is at least'], ['<=', 'is at most'], ['==', 'is exactly'], ['>', 'is more than'], ['<', 'is less than']];

	const WHEN = {
		start: { label: 'the scenario starts', params: [] },
		timer: { label: 'after some seconds', params: [['seconds', 'number', 'seconds', 30], ['repeat', 'bool', 'and then again every time', false]] },
		enter: { label: 'a unit enters an area', params: [['area', 'area', 'area', ''], ['team', 'team', 'whose', -1]] },
		leave: { label: 'a unit leaves an area', params: [['area', 'area', 'area', ''], ['team', 'team', 'whose', -1]] },
		empty: { label: 'an area is emptied', params: [['area', 'area', 'area', ''], ['team', 'team', 'of', -1]] },
		teamGone: { label: 'a team has no units left', params: [['team', 'team', 'team', 0]] },
		brainGone: { label: 'a team\'s brain is destroyed', params: [['team', 'team', 'team', 0]] },
		battleStarted: { label: 'a battle begins', params: [] },
		battleScore: { label: 'a team scores in the battle (a capture, goal or hill)', params: [['team', 'team', 'team', -1]] },
		battleObjective: { label: 'an objective is taken in the battle (assault)', params: [['team', 'team', 'team', -1]] },
		battleEnded: { label: 'the battle ends', params: [['team', 'team', 'won by', -1]] },
		funds: { label: 'a team\'s gold reaches', params: [['team', 'team', 'team', 0], ['op', 'op', '', '>='], ['amount', 'number', 'gold', 1000]] },
		always: { label: 'every moment (use conditions)', params: [] },
	};

	const IF = {
		funds: { label: 'a team\'s gold', params: [['team', 'team', 'team', 0], ['op', 'op', '', '>='], ['amount', 'number', 'gold', 1000]] },
		time: { label: 'the time since it started', params: [['op', 'op', '', '>='], ['seconds', 'number', 'seconds', 60]] },
		units: { label: 'units of a team', params: [['team', 'team', 'team', 0], ['op', 'op', '', '>='], ['count', 'number', 'count', 1]] },
		inArea: { label: 'units of a team in an area', params: [['team', 'team', 'team', 0], ['area', 'area', 'area', ''], ['op', 'op', '', '>='], ['count', 'number', 'count', 1]] },
		flag: { label: 'a flag', params: [['name', 'text', 'name', 'flag'], ['value', 'bool', 'is set', true]] },
		stage: { label: 'the stage it is on', params: [['op', 'op', '', '=='], ['n', 'number', 'stage number', 1]] },
		battleScore: { label: 'a team\'s score in the battle', params: [['team', 'team', 'team', 0], ['op', 'op', '', '>='], ['count', 'number', 'points', 1]] },
	};

	// What finishes a stage.
	const GOAL = {
		reach: { label: 'a unit reaches an area', params: [['area', 'area', 'area', ''], ['team', 'team', 'whose', 0]] },
		hold: { label: 'a team holds an area, with no enemy in it, for some seconds', params: [['area', 'area', 'area', ''], ['team', 'team', 'team', 0], ['seconds', 'number', 'seconds', 60]] },
		destroy: { label: 'a team has no units left', params: [['team', 'team', 'team', 1]] },
		brain: { label: 'a team\'s brain is destroyed', params: [['team', 'team', 'team', 1]] },
		fetch: { label: 'an item is brought to an area', params: [['item', 'item', 'item', ''], ['from', 'area', 'put it in', ''], ['area', 'area', 'bring it to', '']] },
		survive: { label: 'time passes', params: [['seconds', 'number', 'seconds', 120]] },
		battle: { label: 'the battle is won', params: [['team', 'team', 'by', -1]] },
		battleScore: { label: 'a team scores in the battle', params: [['team', 'team', 'team', 0], ['count', 'number', 'points', 1]] },
	};

	const THEN = {
		message: { label: 'show a message', params: [['text', 'text', 'text', 'Well done.'], ['who', 'player', 'to', 'all'], ['seconds', 'number', 'for seconds', 5]] },
		gold: { label: 'give gold', params: [['team', 'team', 'to team', 0], ['amount', 'number', 'gold (negative takes it)', 500]] },
		spawn: { label: 'spawn units', params: [['preset', 'preset', 'unit', ''], ['team', 'team', 'for team', 0], ['area', 'area', 'in area', ''], ['count', 'number', 'how many', 1]] },
		killAll: { label: 'destroy a team\'s units', params: [['team', 'team', 'team', 0]] },
		flag: { label: 'set a flag', params: [['name', 'text', 'name', 'flag'], ['value', 'bool', 'to set', true]] },
		win: { label: 'end it: a team wins', params: [['team', 'team', 'team', 0]] },
		end: { label: 'end it with no winner', params: [] },
		call: { label: 'run my own function', params: [['fn', 'fn', 'function', 'MyFunction']] },
		stage: { label: 'jump to a stage', params: [['n', 'number', 'stage number', 1]] },
		battle: { label: 'start a battle', params: [['mode', 'mode', 'mode', 2], ['size', 'number', 'units a side', 12], ['byShip', 'bool', 'they arrive by ship', false], ['layout', 'bool', 'lay the map out for me', true], ['seed', 'number', 'layout seed', 1]] },
		battleStop: { label: 'stop the battle', params: [] },
	};

	let defaultArea = ''; // the scene's first area, for new blocks that need one
	const blank = (table, kind) => Object.assign({ kind }, ...table[kind].params.map(([key, type, , def]) => ({ [key]: type === 'area' ? defaultArea : def })));

	// ---- generating the Lua ----

	const luaStr = (s) => '"' + String(s).replace(/\\/g, '\\\\').replace(/"/g, '\\"').replace(/[\r\n]+/g, ' ') + '"';
	const luaNum = (n) => String(Number.isFinite(Number(n)) ? Number(n) : 0);
	const ident = (s) => (String(s).replace(/[^A-Za-z0-9_]/g, '') || 'MyFunction').replace(/^[0-9]/, 'F$&');
	const opOf = (o) => (OPS.some(([v]) => v === o) ? o : '>=');
	const area = (name) => 'studioArea(' + luaStr(name) + ')';

	function conditionLua(c) {
		switch (c.kind) {
			case 'funds': return 'self:GetTeamFunds(' + luaNum(c.team) + ') ' + opOf(c.op) + ' ' + luaNum(c.amount);
			case 'time': return 'st.clock.ElapsedSimTimeS ' + opOf(c.op) + ' ' + luaNum(c.seconds);
			case 'units': return 'studioCount(' + luaNum(c.team) + ', nil) ' + opOf(c.op) + ' ' + luaNum(c.count);
			case 'inArea': return 'studioCount(' + luaNum(c.team) + ', ' + area(c.area) + ') ' + opOf(c.op) + ' ' + luaNum(c.count);
			case 'flag': return '(st.flags[' + luaStr(c.name) + '] == true) == ' + (c.value ? 'true' : 'false');
			case 'stage': return '(st.stage or 1) ' + opOf(c.op) + ' ' + luaNum(c.n);
			case 'battleScore': return '((st.battle and st.battle.scores[' + luaNum(c.team) + ']) or 0) ' + opOf(c.op) + ' ' + luaNum(c.count);
			default: return 'true';
		}
	}

	function actionLua(a) {
		switch (a.kind) {
			case 'message': {
				const who = a.who === 'all' ? 'true' : 'player == ' + luaNum(a.who);
				return ['for player = 0, 3 do', '\tif self:PlayerActive(player) and self:PlayerHuman(player) and (' + who + ') then', '\t\tlocal screen = self:ScreenOfPlayer(player)', '\t\tFrameMan:ClearScreenText(screen)', '\t\tFrameMan:SetScreenText(' + luaStr(a.text) + ', screen, 0, ' + luaNum(Number(a.seconds) * 1000) + ', false)', '\tend', 'end'];
			}
			case 'gold': return ['self:ChangeTeamFunds(' + luaNum(a.amount) + ', ' + luaNum(a.team) + ')'];
			case 'spawn': {
				const parts = String(a.preset).split('|');
				const create = 'Create' + (parts[0] === 'ACrab' ? 'ACrab' : 'AHuman');
				return ['for i = 1, ' + luaNum(a.count) + ' do', '\tlocal where = ' + area(a.area), '\tlocal unit = ' + create + '(' + luaStr(parts[1] || '') + ', ' + luaStr(parts[2] || 'Base.rte') + ')', '\tif unit and where then', '\t\tunit.Team = ' + luaNum(a.team), '\t\tunit.Pos = where.RandomPoint', '\t\tMovableMan:AddActor(unit)', '\tend', 'end'];
			}
			case 'killAll': return ['local doomed = {}', 'for actor in MovableMan.Actors do', '\tif actor.Team == ' + luaNum(a.team) + ' then table.insert(doomed, actor) end', 'end', 'for _, actor in ipairs(doomed) do actor:GibThis() end'];
			case 'flag': return ['st.flags[' + luaStr(a.name) + '] = ' + (a.value ? 'true' : 'false')];
			case 'win': return ['self.WinnerTeam = ' + luaNum(a.team), 'ActivityMan:EndActivity()'];
			case 'end': return ['ActivityMan:EndActivity()'];
			case 'call': return ['self:' + ident(a.fn) + '()'];
			case 'stage': return ['st.stage = ' + luaNum(a.n), 'st.stageStarted = false'];
			case 'battle': return [
				'st.battle = { started = false, ended = false, winner = -1, scores = {} }',
				'if SandboxBattleMode then',
				...(a.layout ? ['\tSandboxBattleLayout(' + luaNum(a.mode) + ', ' + luaNum(a.seed) + ', 1)'] : []),
				'\tSandboxBattleMode(' + luaNum(a.mode) + ', ' + luaNum(a.size) + ', ' + (a.byShip ? 'true' : 'false') + ')',
				'else',
				'\tConsoleMan:PrintString("Studio: this game build has no battle modes to start (SandboxBattleMode).")',
				'end',
			];
			case 'battleStop': return ['if SandboxBattleMode then SandboxBattleMode(0, 2, false) end'];
			default: return [];
		}
	}

	// The lines that decide whether a rule's event has happened (setting fire).
	function eventLua(i, w) {
		const team = luaNum(w.team);
		switch (w.kind) {
			case 'start': return ['if not st.started then fire = true end'];
			case 'timer': return w.repeat
				? ['local due = st.next[' + i + '] or ' + luaNum(w.seconds), 'if st.clock.ElapsedSimTimeS >= due then fire = true; st.next[' + i + '] = due + ' + luaNum(Math.max(0.1, Number(w.seconds))) + ' end']
				: ['if st.clock.ElapsedSimTimeS >= ' + luaNum(w.seconds) + ' then fire = true end'];
			case 'enter': case 'leave': case 'empty': {
				const lines = ['local now, count = studioInside(' + luaStr(w.area) + ', ' + team + ')', 'local before = st.inside[' + i + '] or {}'];
				if (w.kind === 'enter') {
					lines.push('for id in pairs(now) do if not before[id] then fire = true; st.who[' + i + '] = now[id] end end');
				} else if (w.kind === 'leave') {
					lines.push('for id in pairs(before) do if not now[id] then fire = true end end');
				} else {
					lines.push('if count == 0 and next(before) ~= nil then fire = true end');
				}
				lines.push('st.inside[' + i + '] = now');
				return lines;
			}
			case 'teamGone': return ['local alive = studioCount(' + team + ', nil)', 'if alive > 0 then st.seen[' + i + '] = true end', 'if st.seen[' + i + '] and alive == 0 then fire = true end'];
			case 'battleStarted': return ['if studioBattleEvent(st, "started", -1) then fire = true end'];
			case 'battleScore': return ['if studioBattleEvent(st, "score", ' + team + ') then fire = true end'];
			case 'battleObjective': return ['if studioBattleEvent(st, "objective", ' + team + ') then fire = true end'];
			case 'battleEnded': return ['if studioBattleEvent(st, "ended", ' + team + ') then fire = true end'];
			case 'brainGone': return ['local alive = studioBrains(' + team + ')', 'if alive > 0 then st.seen[' + i + '] = true end', 'if st.seen[' + i + '] and alive == 0 then fire = true end'];
			case 'funds': return ['local reached = self:GetTeamFunds(' + team + ') ' + opOf(w.op) + ' ' + luaNum(w.amount), 'if reached and not st.prev[' + i + '] then fire = true end', 'st.prev[' + i + '] = reached'];
			default: return ['fire = true'];
		}
	}

	// A rule fires once if it says so, and always for events that stay true (a team gone, a timer that does not repeat, the start).
	const once = (r) => r.once || ['start', 'teamGone', 'brainGone'].includes(r.when.kind) || (r.when.kind === 'timer' && !r.when.repeat);

	const itemCreate = (cls) => 'Create' + (['HeldDevice', 'TDExplosive', 'ThrownDevice'].includes(cls) ? cls : 'HDFirearm');

	// The lua for one stage: what it does when it begins, how it is known to be done, its marker, and what happens when it is done or time runs out.
	function stageLua(cls, stage, n) {
		const L = [];
		const g = stage.goal;
		const body = (actions) => (actions.length ? actions.flatMap((a) => actionLua(a).map((line) => '\t' + line)) : ['\t-- (nothing)']);
		L.push('-- Stage ' + n + ': ' + stage.name.replace(/[\r\n]+/g, ' '));
		L.push('function ' + cls + ':StudioStage' + n + 'Start()', '\tlocal st = self.StudioState');
		if (g.kind === 'fetch' && g.item && g.from) {
			const parts = String(g.item).split('|');
			L.push('\tlocal where = ' + area(g.from), '\tlocal item = ' + itemCreate(parts[0]) + '(' + luaStr(parts[1] || '') + ', ' + luaStr(parts[2] || 'Base.rte') + ')', '\tif item and where then', '\t\titem.Pos = where.RandomPoint', '\t\tMovableMan:AddItem(item)', '\tend');
		}
		L.push(...body(stage.onStart), 'end');
		L.push('function ' + cls + ':StudioStage' + n + 'Done()', '\tlocal st = self.StudioState', ...body(stage.onDone), 'end');
		L.push('function ' + cls + ':StudioStage' + n + 'Fail()', '\tlocal st = self.StudioState', ...body(stage.onFail), 'end');
		// each frame: the objective marker
		const text = luaStr(stage.text || stage.name);
		const viewer = luaNum(stage.viewer);
		L.push('function ' + cls + ':StudioStage' + n + 'Mark()');
		if (stage.marker) {
			if (['reach', 'hold'].includes(g.kind)) {
				L.push('\tlocal where = ' + area(g.area), '\tif where then self:AddObjectivePoint(' + text + ', where.Center, ' + viewer + ', GameActivity.ARROWDOWN) end');
			} else if (g.kind === 'fetch') {
				L.push('\tlocal where = ' + area(g.area), '\tif where then self:AddObjectivePoint(' + text + ', where.Center, ' + viewer + ', GameActivity.ARROWDOWN) end');
			} else if (g.kind === 'battle' || g.kind === 'battleScore') {
				L.push('\t-- (the game lights up the battle\'s own objectives)');
			} else if (g.kind === 'destroy' || g.kind === 'brain') {
				L.push('\tfor actor in MovableMan.Actors do', '\t\tif actor.Team == ' + luaNum(g.team) + ' and actor.Health > 0' + (g.kind === 'brain' ? ' and actor:IsInGroup("Brains")' : '') + ' then self:AddObjectivePoint(' + text + ', actor.AboveHUDPos, ' + viewer + ', GameActivity.ARROWDOWN) end', '\tend');
			} else {
				L.push('\t-- (no marker for this goal)');
			}
		} else {
			L.push('\t-- (no marker)');
		}
		L.push('end');
		// is it done
		L.push('function ' + cls + ':StudioStage' + n + 'Check()', '\tlocal st = self.StudioState', '\tlocal elapsed = st.stageClock.ElapsedSimTimeS');
		switch (g.kind) {
			case 'reach': L.push('\treturn studioCount(' + luaNum(g.team) + ', ' + area(g.area) + ') > 0'); break;
			case 'hold':
				L.push('\tlocal region = ' + area(g.area), '\tlocal held = region ~= nil and studioCount(' + luaNum(g.team) + ', region) > 0 and studioEnemies(' + luaNum(g.team) + ', region) == 0', '\tif not held then st.holdFrom = nil elseif not st.holdFrom then st.holdFrom = elapsed end', '\treturn st.holdFrom ~= nil and elapsed - st.holdFrom >= ' + luaNum(g.seconds));
				break;
			case 'destroy': L.push('\treturn elapsed > 1 and studioCount(' + luaNum(g.team) + ', nil) == 0'); break;
			case 'battle': L.push('\treturn st.battle ~= nil and st.battle.ended and (' + luaNum(g.team) + ' < 0 or st.battle.winner == ' + luaNum(g.team) + ')'); break;
			case 'battleScore': L.push('\treturn ((st.battle and st.battle.scores[' + luaNum(g.team) + ']) or 0) >= ' + luaNum(g.count)); break;
			case 'brain': L.push('\treturn elapsed > 1 and studioBrains(' + luaNum(g.team) + ') == 0'); break;
			case 'fetch': L.push('\treturn studioItemIn(' + luaStr(g.area) + ', ' + luaStr(String(g.item).split('|')[1] || '') + ')'); break;
			default: L.push('\treturn elapsed >= ' + luaNum(g.seconds));
		}
		L.push('end', '');
		return L;
	}

	function generate(cls, rules, stages) {
		stages = stages || [];
		const L = [];
		L.push(MARK_BEGIN + ': managed by Cortex Studio (the Scenarios editor, Mission logic). Change the rules there; this block is rewritten. Your own functions go outside it.');
		L.push(DATA + JSON.stringify({ v: 2, rules, stages }));
		L.push('local function studioArea(name)', '\tif name ~= "" and SceneMan.Scene:HasArea(name) then return SceneMan.Scene:GetArea(name) end', '\treturn nil', 'end', '');
		L.push('local function studioCount(team, region)', '\tlocal n = 0', '\tfor actor in MovableMan.Actors do', '\t\tif actor.Health > 0 and (team < 0 or actor.Team == team) and (region == nil or region:IsInside(actor.Pos)) then n = n + 1 end', '\tend', '\treturn n', 'end', '');
		L.push('local function studioInside(name, team)', '\tlocal region = studioArea(name)', '\tlocal now, count = {}, 0', '\tif region then', '\t\tfor actor in MovableMan.Actors do', '\t\t\tif actor.Health > 0 and (team < 0 or actor.Team == team) and region:IsInside(actor.Pos) then now[actor.UniqueID] = actor; count = count + 1 end', '\t\tend', '\tend', '\treturn now, count', 'end', '');
		L.push('-- What the game\'s battle mode reports (an older game build has none): kept in st.battle, with this frame\'s events in st.battleNow.',
			'local function studioPump(st)', '\tst.battleNow = {}', '\tif not SandboxBattleEvent then return end', '\tlocal line = SandboxBattleEvent()', '\twhile line ~= "" do',
			'\t\tlocal parts = {}', '\t\tfor token in line:gmatch("%S+") do parts[#parts + 1] = token end', '\t\tlocal name, a, b = parts[1], tonumber(parts[2]) or -1, tonumber(parts[3]) or 0',
			'\t\tst.battle = st.battle or { started = false, ended = false, winner = -1, scores = {} }',
			'\t\tif name == "started" then st.battle = { started = true, ended = false, winner = -1, scores = {} }', '\t\telseif name == "score" or name == "objective" then st.battle.scores[a] = b', '\t\telseif name == "ended" then st.battle.ended = true; st.battle.winner = a end',
			'\t\ttable.insert(st.battleNow, { name = name, team = a })', '\t\tline = SandboxBattleEvent()', '\tend', 'end', '');
		L.push('local function studioBattleEvent(st, name, team)', '\tfor _, e in ipairs(st.battleNow or {}) do', '\t\tif e.name == name and (team < 0 or e.team == team) then return true end', '\tend', '\treturn false', 'end', '');
		L.push('local function studioEnemies(team, region)', '\tlocal n = 0', '\tfor actor in MovableMan.Actors do', '\t\tif actor.Health > 0 and actor.Team >= 0 and actor.Team ~= team and region:IsInside(actor.Pos) then n = n + 1 end', '\tend', '\treturn n', 'end', '');
		L.push('local function studioBrains(team)', '\tlocal n = 0', '\tfor actor in MovableMan.Actors do', '\t\tif actor.Health > 0 and actor.Team == team and actor:IsInGroup("Brains") then n = n + 1 end', '\tend', '\treturn n', 'end', '');
		L.push('local function studioItemIn(areaName, itemName)', '\tlocal region = studioArea(areaName)', '\tif not region then return false end', '\tfor item in MovableMan.Items do', '\t\tif item.PresetName == itemName and region:IsInside(item.Pos) then return true end', '\tend', '\tfor actor in MovableMan.Actors do', '\t\tif actor.Health > 0 and region:IsInside(actor.Pos) then', '\t\t\tlocal held = actor.EquippedItem', '\t\t\tif held and held.PresetName == itemName then return true end', '\t\t\tfor item in actor.Inventory do', '\t\t\t\tif item.PresetName == itemName then return true end', '\t\t\tend', '\t\tend', '\tend', '\treturn false', 'end', '');
		stages.forEach((st, i) => L.push(...stageLua(cls, st, i + 1)));
		if (stages.length) {
			L.push(cls + '.StudioStageLimit = { ' + stages.map((st) => luaNum(st.failAfter)).join(', ') + ' }', '');
		}
		rules.forEach((r, i) => {
			L.push('-- Rule ' + (i + 1) + ': ' + r.name.replace(/[\r\n]+/g, ' '));
			L.push('function ' + cls + ':StudioRule' + (i + 1) + '(actor)', '\tlocal st = self.StudioState');
			if (!r.then.length) {
				L.push('\t-- (no actions)');
			}
			for (const a of r.then) {
				L.push(...actionLua(a).map((line) => '\t' + line));
			}
			L.push('end', '');
		});
		L.push('function ' + cls + ':RunStudioRules()');
		L.push('\tself.StudioState = self.StudioState or { clock = Timer(), flags = {}, inside = {}, who = {}, seen = {}, prev = {}, next = {}, done = {}, started = false }');
		L.push('\tlocal st = self.StudioState');
		L.push('\tstudioPump(st)');
		if (stages.length) {
			L.push('\tst.stage = st.stage or 1');
			L.push('\tif st.stage <= ' + stages.length + ' then');
			L.push('\t\tlocal n = st.stage');
			L.push('\t\tif not st.stageStarted then', '\t\t\tst.stageStarted = true', '\t\t\tst.stageClock = Timer()', '\t\t\tst.holdFrom = nil', '\t\t\tself["StudioStage" .. n .. "Start"](self)', '\t\tend');
			L.push('\t\tself["StudioStage" .. n .. "Mark"](self)');
			L.push('\t\tif self["StudioStage" .. n .. "Check"](self) then', '\t\t\tself["StudioStage" .. n .. "Done"](self)', '\t\t\tif st.stage == n then st.stage = n + 1; st.stageStarted = false end');
			L.push('\t\telseif ' + cls + '.StudioStageLimit[n] > 0 and st.stageClock.ElapsedSimTimeS >= ' + cls + '.StudioStageLimit[n] then', '\t\t\tself["StudioStage" .. n .. "Fail"](self)', '\t\t\tif st.stage == n then st.stage = n + 1; st.stageStarted = false end', '\t\tend', '\tend');
		}
		rules.forEach((r, i) => {
			L.push('\tdo -- Rule ' + (i + 1));
			L.push('\t\tlocal fire = false');
			L.push(...eventLua(i + 1, r.when).map((line) => '\t\t' + line));
			const conds = r.ifs.map(conditionLua);
			L.push('\t\tif fire and not st.done[' + (i + 1) + ']' + conds.map((c) => ' and (' + c + ')').join('') + ' then');
			L.push('\t\t\tself:StudioRule' + (i + 1) + '(st.who[' + (i + 1) + '])');
			if (once(r)) {
				L.push('\t\t\tst.done[' + (i + 1) + '] = true');
			}
			L.push('\t\tend', '\tend');
		});
		L.push('\tst.started = true', 'end', MARK_END);
		return L.join('\n');
	}

	// ---- reading the script ----

	function parseRules(code) {
		if (!code) {
			return null;
		}
		const at = code.indexOf(DATA);
		if (at >= 0) {
			const end = code.indexOf('\n', at);
			try {
				const data = JSON.parse(code.slice(at + DATA.length, end < 0 ? undefined : end));
				if (Array.isArray(data)) {
					return { rules: data, stages: [] };
				}
				return data && Array.isArray(data.rules) ? { rules: data.rules, stages: Array.isArray(data.stages) ? data.stages : [] } : null;
			} catch (e) {
				return null;
			}
		}
		return null;
	}

	// Triggers from the first version of this feature, as rules that call the function they ran.
	function oldTriggers(code) {
		const begin = code.indexOf(OLD_BEGIN);
		const end = code.indexOf(OLD_END);
		if (begin < 0 || end < begin) {
			return [];
		}
		const re = /\{ name = "([^"]*)", area = "([^"]*)", team = (-?\d+), event = "(\w+)", once = (true|false), handler = "(\w+)" \}/g;
		return [...code.slice(begin, end).matchAll(re)].map((m) => ({ name: m[1], once: m[5] === 'true', when: { kind: m[4], area: m[2], team: Number(m[3]) }, ifs: [], then: [{ kind: 'call', fn: m[6] }] }));
	}

	// The script with its rules block replaced (or added before UpdateActivity), UpdateActivity running the rules, and a stub for each function called.
	function withRules(code, cls, rules, stages) {
		let text = code.replace(/\r\n/g, '\n');
		for (const [b, e] of [[MARK_BEGIN, MARK_END], [OLD_BEGIN, OLD_END]]) {
			const begin = text.indexOf(b);
			const end = text.indexOf(e);
			if (begin >= 0 && end > begin) {
				text = text.slice(0, begin).replace(/\s*$/, '') + '\n\n' + text.slice(end + e.length).replace(/^\n+/, '');
			}
		}
		const block = generate(cls, rules, stages || []);
		const update = new RegExp('function\\s+' + cls + ':UpdateActivity\\s*\\(\\s*\\)');
		if (update.test(text)) {
			text = text.replace(update, (m) => block + '\n\n' + m);
		} else {
			text = text.replace(/\s*$/, '') + '\n\n' + block + '\n\nfunction ' + cls + ':UpdateActivity()\n\tself:RunStudioRules()\nend\n';
		}
		const body = new RegExp(cls + ':UpdateActivity\\s*\\(\\s*\\)[^]*?:RunStudioRules\\(\\)');
		if (!body.test(text)) {
			text = text.replace(update, (m) => m + '\n\tself:RunStudioRules()');
		}
		// the old trigger call is no longer needed
		text = text.replace(/\n\tself:RunStudioTriggers\(\)/g, '');
		for (const r of rules) {
			for (const a of r.then) {
				if (a.kind === 'call') {
					const name = ident(a.fn);
					if (!new RegExp('function\\s+' + cls + ':' + name + '\\s*\\(').test(text)) {
						text = text.replace(/\s*$/, '') + '\n\n-- Called by the rule "' + r.name.replace(/[\r\n]+/g, ' ') + '".\nfunction ' + cls + ':' + name + '()\n\t\nend\n';
					}
				}
			}
		}
		return text;
	}

	// ---- ready-made missions ----
	// Each template asks a few questions and builds stages (and sometimes rules) that can then be changed like any others.

	const msg = (text, who) => ({ kind: 'message', text, who: who === undefined ? 'all' : who, seconds: 5 });
	const win = (team) => ({ kind: 'win', team });
	const stage = (name, goal, p, extra) => Object.assign({ name, goal, text: '', marker: true, viewer: Number(p.team) || 0, failAfter: 0, onStart: [], onDone: [], onFail: [] }, extra || {});
	const battleStage = (name, mode, p) => stage(name, { kind: 'battle', team: Number(p.team) }, p, {
		marker: false,
		onStart: [msg(name + '!'), { kind: 'battle', mode, size: Number(p.size) || 12, byShip: false, layout: true, seed: Number(p.seed) || 1 }],
		onDone: [msg('Victory!'), win(Number(p.team))],
		failAfter: Math.max(0, Number(p.minutes) || 0) * 60,
		onFail: [msg('Out of time.'), { kind: 'end' }],
	});
	const battleParams = [['team', 'team', 'you are', 0], ['size', 'number', 'units a side', 12], ['minutes', 'number', 'time limit in minutes (0: none)', 0], ['seed', 'number', 'layout seed (a different number lays the map out differently)', 1]];

	const TEMPLATES = [
		{ id: 'hold', name: 'Hold the hill', blurb: 'Reach an area and keep it, free of enemies, for a while.', params: [['area', 'area', 'the hill', ''], ['team', 'team', 'you are', 0], ['seconds', 'number', 'hold it for (seconds)', 120]],
			build: (p) => ({ rules: [], stages: [
				stage('Reach the hill', { kind: 'reach', area: p.area, team: Number(p.team) }, p, { text: 'Take the hill', onDone: [msg('Now hold it!')] }),
				stage('Hold the hill', { kind: 'hold', area: p.area, team: Number(p.team), seconds: Number(p.seconds) }, p, { text: 'Hold it!', onDone: [msg('Hill held!'), win(Number(p.team))] })] }) },
		{ id: 'brain', name: 'Destroy the enemy brain', blurb: 'Hunt down and destroy the other team\'s brain, before time runs out.', params: [['team', 'team', 'you are', 0], ['enemy', 'team', 'the enemy', 1], ['minutes', 'number', 'time limit in minutes (0: none)', 15]],
			build: (p) => ({ rules: [], stages: [
				stage('Destroy the brain', { kind: 'brain', team: Number(p.enemy) }, p, { text: 'Terminate!', failAfter: Math.max(0, Number(p.minutes) || 0) * 60, onStart: [msg('Find and destroy their brain.')], onDone: [msg('The brain is gone.'), win(Number(p.team))], onFail: [msg('Time is up.'), win(Number(p.enemy))] })] }) },
		{ id: 'retrieve', name: 'Retrieve the item', blurb: 'An item appears in one area. Take it, and bring it to another.', params: [['item', 'item', 'the item', ''], ['from', 'area', 'it starts in', ''], ['area', 'area', 'bring it to', ''], ['team', 'team', 'you are', 0], ['minutes', 'number', 'time limit in minutes (0: none)', 0]],
			build: (p) => ({ rules: [], stages: [
				stage('Retrieve the item', { kind: 'fetch', item: p.item, from: p.from, area: p.area }, p, { text: 'Bring it here', failAfter: Math.max(0, Number(p.minutes) || 0) * 60, onStart: [msg('Find it and bring it back.')], onDone: [msg('Delivered.'), win(Number(p.team))], onFail: [msg('Too late.'), { kind: 'end' }] })] }) },
		{ id: 'survive', name: 'Survive', blurb: 'Stay alive for a set time.', params: [['team', 'team', 'you are', 0], ['seconds', 'number', 'seconds', 300]],
			build: (p) => ({ rules: [], stages: [stage('Survive', { kind: 'survive', seconds: Number(p.seconds) }, p, { marker: false, onStart: [msg('Survive!')], onDone: [msg('You made it.'), win(Number(p.team))] })] }) },
		{ id: 'assault', name: 'Take the position, clear it, hold it', blurb: 'Reach one area, destroy the enemy team, then hold another area for a while.', params: [['area', 'area', 'first, reach', ''], ['hill', 'area', 'finally, hold', ''], ['team', 'team', 'you are', 0], ['enemy', 'team', 'the enemy', 1], ['seconds', 'number', 'hold it for (seconds)', 90]],
			build: (p) => ({ rules: [], stages: [
				stage('Reach the position', { kind: 'reach', area: p.area, team: Number(p.team) }, p, { text: 'Move up', onDone: [msg('Clear them out.')] }),
				stage('Clear the enemy', { kind: 'destroy', team: Number(p.enemy) }, p, { text: 'Eliminate', onDone: [msg('All clear. Now take and hold the final position.')] }),
				stage('Hold the position', { kind: 'hold', area: p.hill, team: Number(p.team), seconds: Number(p.seconds) }, p, { text: 'Hold!', onDone: [msg('Mission complete.'), win(Number(p.team))] })] }) },
		{ id: 'ctf', name: 'Capture the flag match', blurb: 'The game\'s capture the flag, with a victory at the end. Needs a game with battle modes.', params: battleParams, build: (p) => ({ rules: [], stages: [battleStage('Capture the flag', 1, p)] }) },
		{ id: 'koth', name: 'King of the hill match', blurb: 'The game\'s king of the hill. Needs a game with battle modes.', params: battleParams, build: (p) => ({ rules: [], stages: [battleStage('King of the hill', 2, p)] }) },
		{ id: 'assaultmode', name: 'Assault match', blurb: 'The game\'s assault: take each objective in turn against the clock. Needs a game with battle modes.', params: battleParams, build: (p) => ({ rules: [], stages: [battleStage('Assault', 3, p)] }) },
		{ id: 'campaign', name: 'Two battles in a row', blurb: 'Win capture the flag, then king of the hill, then the mission is won.', params: battleParams,
			build: (p) => ({ rules: [], stages: [
				Object.assign(battleStage('Capture the flag', 1, p), { onDone: [msg('Flag war won. Now hold the hill.')] }),
				battleStage('King of the hill', 2, p)] }) },
	];

	// ---- the form ----

	// Where the function a rule calls is in the script, to jump to it.
	function lineOf(code, cls, fn) {
		return Math.max(1, (code || '').split('\n').findIndex((l) => l.includes(cls + ':' + ident(fn) + '(')) + 1);
	}

	// The section for the scenarios editor: p the activity preset, e its resolved entity, helpers from it.
	function section(p, e, ro, sceneAreas, host) {
		const box = el('div', {});
		const path = e.str('ScriptPath', '');
		const cls = e.str('LuaClassName', '');
		const code = host.scriptText(path);
		if (!path || !cls) {
			box.append(el('p', { className: 'ed-note', textContent: 'Mission logic needs a script: give the scenario a script file and class (below) first.' }));
			return box;
		}
		const parsed = parseRules(code);
		let rules = parsed ? parsed.rules : null;
		let stages = parsed ? parsed.stages : [];
		let migrated = false;
		if (rules === null) {
			rules = code ? oldTriggers(code) : [];
			migrated = rules.length > 0;
		}
		const areaList = [...new Set(sceneAreas)];
		defaultArea = areaList[0] || '';
		const save = (nextRules, nextStages) => host.saveText(path, withRules(host.scriptText(path) || '', cls, nextRules, nextStages)).catch((err) => app().toast('Couldn\'t change the script: ' + err.message, 'bad'));
		const edit = (mutate) => {
			const next = JSON.parse(JSON.stringify(rules));
			mutate(next);
			return save(next, stages);
		};
		const editStages = (mutate) => {
			const next = JSON.parse(JSON.stringify(stages));
			mutate(next);
			return save(rules, next);
		};
		const team = (value, on, anyOk) => el('select', { className: 'ed-input', disabled: ro }, ...(anyOk ? [el('option', { value: '-1', textContent: 'any team', selected: Number(value) < 0 })] : []), ...[0, 1, 2, 3].map((n) => el('option', { value: String(n), textContent: 'Team ' + (n + 1), selected: Number(value) === n })));
		const field = (type, label, value, set) => {
			let input;
			if (type === 'team') {
				input = team(value, set, true);
				input.onchange = () => set(Number(input.value));
			} else if (type === 'area') {
				input = el('select', { className: 'ed-input', disabled: ro }, ...[...new Set([...areaList, ...(value ? [value] : [])])].map((n) => el('option', { value: n, textContent: n, selected: n === value })));
				input.onchange = () => set(input.value);
			} else if (type === 'op') {
				input = el('select', { className: 'ed-input', disabled: ro }, ...OPS.map(([v, t]) => el('option', { value: v, textContent: t, selected: v === value })));
				input.onchange = () => set(input.value);
			} else if (type === 'bool') {
				input = el('input', { type: 'checkbox', checked: !!value, disabled: ro });
				input.onchange = () => set(input.checked);
			} else if (type === 'player') {
				input = el('select', { className: 'ed-input', disabled: ro }, el('option', { value: 'all', textContent: 'all players', selected: value === 'all' }), ...[0, 1, 2, 3].map((n) => el('option', { value: String(n), textContent: 'player ' + (n + 1), selected: String(value) === String(n) })));
				input.onchange = () => set(input.value === 'all' ? 'all' : Number(input.value));
			} else if (type === 'number') {
				input = el('input', { type: 'number', step: 'any', className: 'ed-input num', value, disabled: ro });
				input.onchange = () => set(Number(input.value) || 0);
			} else if (type === 'mode') {
				input = el('select', { className: 'ed-input', disabled: ro }, ...BATTLE_MODES.map(([v, t]) => el('option', { value: String(v), textContent: t, selected: Number(value) === v })));
				input.onchange = () => set(Number(input.value));
			} else if (type === 'item') {
				const name = String(value).split('|')[1] || '(pick one)';
				input = el('button', { className: 'tb', textContent: name, disabled: ro, onclick: () => host.pickItem((chosen) => set(chosen.cls + '|' + chosen.name + '|' + chosen.module)) });
			} else if (type === 'preset') {
				const name = String(value).split('|')[1] || '(pick one)';
				input = el('button', { className: 'tb', textContent: name, disabled: ro, onclick: () => host.pickUnit((chosen) => set(chosen.cls + '|' + chosen.name + '|' + chosen.module)) });
			} else {
				input = el('input', { type: 'text', className: 'ed-input' + (type === 'fn' ? ' mono' : ''), value, disabled: ro });
				input.onchange = () => set(input.value);
			}
			return el('label', { className: 'ed-check mission-field' }, label ? el('span', { textContent: label }) : null, input);
		};
		const block = (table, kinds, item, setter, remove) => {
			const spec = table[item.kind];
			const kind = el('select', { className: 'ed-input', disabled: ro }, ...Object.entries(table).map(([k, v]) => el('option', { value: k, textContent: v.label, selected: k === item.kind })));
			kind.onchange = () => setter(() => blank(table, kind.value));
			return el('div', { className: 'mission-block' }, kind, ...spec.params.map(([key, type, label]) => field(type, label, item[key], (value) => setter((old) => Object.assign({}, old, { [key]: value })))),
				item.kind === 'call' ? el('button', { className: 'tb', textContent: 'Edit code', title: 'Jump to this function in the script', onclick: () => host.openLine(path, lineOf(host.scriptText(path), cls, item.fn)) }) : null,
				item.kind === 'call' ? el('button', { className: 'ico-btn', textContent: '⧉', title: 'Edit the code beside this, in its own window', onclick: () => host.openBeside(path, lineOf(host.scriptText(path), cls, item.fn)) }) : null,
				remove ? el('button', { className: 'ico-btn', textContent: '×', title: 'Take it out', disabled: ro, onclick: remove }) : null);
		};
		const templateDialog = () => {
			const values = {};
			const holder = el('div', {});
			const pick = el('select', { className: 'ed-input' }, ...TEMPLATES.map((t) => el('option', { value: t.id, textContent: t.name })));
			const draw = () => {
				const t = TEMPLATES.find((x) => x.id === pick.value);
				holder.replaceChildren(el('p', { className: 'ed-note', textContent: t.blurb }), ...t.params.map(([key, type, label, def]) => {
					if (!(key in values)) {
						values[key] = type === 'area' ? areaList[0] || '' : def;
					}
					return el('div', { className: 'ed-row' }, field(type, label, values[key], (v) => {
						values[key] = v;
						draw();
					}));
				}));
			};
			pick.onchange = () => {
				Object.keys(values).forEach((k) => delete values[k]);
				draw();
			};
			draw();
			MS.dialogs.open('Start from a ready-made mission', el('div', {}, MS.dialogs.parts.field('Mission', pick, 'Its stages are added below any you have; change them like any others.'), holder), [
				{ text: 'Cancel' },
				{ text: 'Add to the mission', primary: true, action: async () => {
					const t = TEMPLATES.find((x) => x.id === pick.value);
					if (t.params.some(([key, type]) => (type === 'area' || type === 'item') && !values[key])) {
						app().toast('Pick ' + (areaList.length ? 'an item' : 'an area') + ' for every choice first (the scene needs areas: add some in the Scenes editor).', 'bad');
						return false;
					}
					const built = t.build(values);
					await save(rules.concat(built.rules), stages.concat(built.stages));
					return true;
				} },
			]);
		};
		const stageBox = el('div', { className: 'mission-stages' });
		stages.forEach((st, i) => {
			const name = el('input', { type: 'text', className: 'ed-input', value: st.name, disabled: ro, title: 'What this stage is called' });
			name.onchange = () => editStages((next) => { next[i].name = name.value.trim() || st.name; });
			const text = el('input', { type: 'text', className: 'ed-input', value: st.text || '', placeholder: 'Objective text, shown on the marker', disabled: ro });
			text.onchange = () => editStages((next) => { next[i].text = text.value; });
			const marker = el('input', { type: 'checkbox', checked: !!st.marker, disabled: ro });
			marker.onchange = () => editStages((next) => { next[i].marker = marker.checked; });
			const viewer = el('select', { className: 'ed-input', disabled: ro, title: 'Whose screen shows the marker' }, ...[0, 1, 2, 3].map((n) => el('option', { value: String(n), textContent: 'Team ' + (n + 1), selected: Number(st.viewer) === n })));
			viewer.onchange = () => editStages((next) => { next[i].viewer = Number(viewer.value); });
			const limit = el('input', { type: 'number', className: 'ed-input num', value: st.failAfter || 0, disabled: ro, title: 'Seconds before it fails (0 for no limit)' });
			limit.onchange = () => editStages((next) => { next[i].failAfter = Math.max(0, Number(limit.value) || 0); });
			const move = (d) => el('button', { className: 'ico-btn', textContent: d < 0 ? '▲' : '▼', disabled: ro || i + d < 0 || i + d >= stages.length, title: d < 0 ? 'Earlier' : 'Later', onclick: () => editStages((next) => { next.splice(i + d, 0, next.splice(i, 1)[0]); }) });
			const list = (key, label, table, fallbackKind) => [
				el('div', { className: 'mission-line' }, el('b', { className: 'mission-word then', textContent: label })),
				...st[key].map((a, k) => el('div', { className: 'mission-line' }, el('b', { className: 'mission-word then', textContent: '' }), block(table, null, a, (fn) => editStages((next) => { next[i][key][k] = fn(next[i][key][k]); }), () => editStages((next) => { next[i][key].splice(k, 1); })))),
				el('div', { className: 'mission-add' }, el('button', { className: 'tb', textContent: '+ Action', disabled: ro, onclick: () => editStages((next) => { next[i][key].push(blank(THEN, fallbackKind)); }) })),
			];
			stageBox.append(el('div', { className: 'mission-rule mission-stage' },
				el('div', { className: 'mission-head' }, el('b', { className: 'mission-num', textContent: String(i + 1) }), name, move(-1), move(1), el('button', { className: 'ico-btn', textContent: '×', title: 'Take the stage out', disabled: ro, onclick: () => editStages((next) => { next.splice(i, 1); }) })),
				el('div', { className: 'mission-line' }, el('b', { className: 'mission-word when', textContent: 'GOAL' }), block(GOAL, null, st.goal, (fn) => editStages((next) => { next[i].goal = fn(next[i].goal); }))),
				el('div', { className: 'mission-line' }, el('b', { className: 'mission-word if', textContent: 'SHOW' }), el('div', { className: 'mission-block' }, el('label', { className: 'ed-check' }, marker, 'a marker'), text, viewer, el('label', { className: 'ed-check', title: 'Seconds before it fails' }, 'time limit', limit))),
				...list('onStart', 'WHEN IT STARTS', THEN, 'message'), ...list('onDone', 'WHEN DONE', THEN, 'message'), ...(st.failAfter > 0 || st.onFail.length ? list('onFail', 'IF TIME RUNS OUT', THEN, 'end') : [])));
		});
		box.append(el('div', { className: 'ed-label', textContent: 'Stages: one goal after another (' + stages.length + ')' }), stageBox,
			el('div', { className: 'ed-row' }, el('button', { className: 'tb primary', textContent: '+ Add a stage', disabled: ro || code === null, onclick: () => editStages((next) => {
				let n = next.length + 1;
				while (next.some((x) => x.name === 'Stage ' + n)) {
					n++;
				}
				next.push({ name: 'Stage ' + n, goal: blank(GOAL, 'reach'), text: '', marker: true, viewer: 0, failAfter: 0, onStart: [], onDone: [{ kind: 'message', text: 'Objective complete.', who: 'all', seconds: 4 }], onFail: [] });
			}) }), el('button', { className: 'tb', textContent: 'Start from a ready-made mission…', disabled: ro || code === null, onclick: templateDialog }), stages.length ? el('small', { textContent: 'When a stage is done the next begins. Make the last one end the mission with a win.' }) : null),
			el('div', { className: 'ed-label', textContent: 'Rules that run alongside (' + rules.length + ')' }));
		if (migrated) {
			box.append(el('div', { className: 'ed-banner', textContent: 'These come from the older "Studio triggers" block. Change anything and they are converted to rules, each running the function it ran before.' }));
		}
		rules.forEach((r, i) => {
			const name = el('input', { type: 'text', className: 'ed-input', value: r.name, disabled: ro, title: 'What this rule is for' });
			name.onchange = () => edit((next) => { next[i].name = name.value.trim() || r.name; });
			const onceBox = el('input', { type: 'checkbox', checked: once(r), disabled: ro || ['start', 'teamGone'].includes(r.when.kind) || (r.when.kind === 'timer' && !r.when.repeat) });
			onceBox.onchange = () => edit((next) => { next[i].once = onceBox.checked; });
			const up = (d) => el('button', { className: 'ico-btn', textContent: d < 0 ? '▲' : '▼', disabled: ro || i + d < 0 || i + d >= rules.length, title: d < 0 ? 'Earlier' : 'Later', onclick: () => edit((next) => { next.splice(i + d, 0, next.splice(i, 1)[0]); }) });
			box.append(el('div', { className: 'mission-rule' },
				el('div', { className: 'mission-head' }, name, el('label', { className: 'ed-check', title: 'Only the first time it happens' }, onceBox, 'once'), up(-1), up(1), el('button', { className: 'ico-btn', textContent: '×', title: 'Take the rule out', disabled: ro, onclick: () => edit((next) => { next.splice(i, 1); }) })),
				el('div', { className: 'mission-line' }, el('b', { className: 'mission-word when', textContent: 'WHEN' }), block(WHEN, null, r.when, (fn) => edit((next) => { next[i].when = fn(next[i].when); }))),
				...r.ifs.map((c, k) => el('div', { className: 'mission-line' }, el('b', { className: 'mission-word if', textContent: k ? 'AND' : 'IF' }), block(IF, null, c, (fn) => edit((next) => { next[i].ifs[k] = fn(next[i].ifs[k]); }), () => edit((next) => { next[i].ifs.splice(k, 1); })))),
				...r.then.map((a, k) => el('div', { className: 'mission-line' }, el('b', { className: 'mission-word then', textContent: k ? 'AND' : 'THEN' }), block(THEN, null, a, (fn) => edit((next) => { next[i].then[k] = fn(next[i].then[k]); }), () => edit((next) => { next[i].then.splice(k, 1); })))),
				el('div', { className: 'mission-add' },
					el('button', { className: 'tb', textContent: '+ Condition', disabled: ro, onclick: () => edit((next) => { next[i].ifs.push(blank(IF, 'funds')); }) }),
					el('button', { className: 'tb', textContent: '+ Action', disabled: ro, onclick: () => edit((next) => { next[i].then.push(blank(THEN, 'message')); }) }))));
		});
		if (!rules.length) {
			box.append(el('p', { className: 'ed-empty', textContent: 'No rules yet. A rule says: when this happens, and if these hold, do these things.' }));
		}
		box.append(el('div', { className: 'ed-row' }, el('button', { className: 'tb primary', textContent: '+ Add a rule', disabled: ro || code === null, onclick: () => edit((next) => {
			let n = next.length + 1;
			while (next.some((r) => r.name === 'Rule ' + n)) {
				n++;
			}
			next.push({ name: 'Rule ' + n, once: true, when: blank(WHEN, 'start'), ifs: [], then: [blank(THEN, 'message')] });
		}) })));
		return box;
	}

	MS.missions = { section, generate, parseRules, withRules, oldTriggers };
})();
