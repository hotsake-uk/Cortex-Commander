// Cortex Mod Studio: turns a flow graph (flowmodel.js) into the Lua a scenario runs, and puts that Lua into the scenario's script. It lives in a marked
// block of the script, with the graph itself as JSON on one comment line, so the script is the only place the mission is stored: the editor reads the
// graph back from it, and the Lua next to it is generated afresh from the graph each time. Code you write outside the block is never touched.
// Like flowmodel.js this has no page in it, so a test can run the Lua it makes (with a stand-in for the game) and check what happens.
//
// The block holds, in order: small helper functions (areas, counting units, the game's battle events), one function for each event node that runs
// its chain, five functions for each stage (start, done, time out, marker, check), the table of stages, and RunStudioFlow, which the script's
// UpdateActivity calls every frame: it reads battle events, moves the current stage along, and looks for each event.
'use strict';
window.MS = window.MS || {};

(function () {
	const F = MS.flow;
	const MARK_BEGIN = '-- >>> Studio flow';
	const MARK_END = '-- <<< Studio flow';
	const DATA = '-- studio-flow-data: ';
	// What older versions of Studio wrote: read for the graph (or the rules) they hold, and replaced when the script is written again.
	const LEGACY = [['-- >>> Studio rules', '-- <<< Studio rules', '-- studio-rules-data: '], ['-- >>> Studio triggers', '-- <<< Studio triggers', null]];

	// ---- small Lua writers ----

	const luaStr = (s) => '"' + String(s === undefined || s === null ? '' : s).replace(/\\/g, '\\\\').replace(/"/g, '\\"').replace(/[\r\n]+/g, ' ') + '"';
	const luaNum = (n) => String(Number.isFinite(Number(n)) ? Number(n) : 0);
	const ident = (s) => (String(s || '').replace(/[^A-Za-z0-9_]/g, '') || 'MyFunction').replace(/^[0-9]/, 'F$&');
	// A name a script's class can have: letters, digits and _, not starting with a digit ("1234" becomes "Mission1234").
	const className = (s, fallback) => {
		const c = String(s || '').replace(/[^A-Za-z0-9_]/g, '') || fallback || 'MyMission';
		return /^[0-9]/.test(c) ? 'Mission' + c : c;
	};
	const isClassName = (s) => /^[A-Za-z_][A-Za-z0-9_]*$/.test(String(s || ''));
	const opOf = (o) => (F.OPS.some(([v]) => v === o) ? o : '>=');
	const area = (name) => 'studioArea(' + luaStr(name) + ')';
	const key = (id) => luaStr(id);
	const indent = (lines, n) => lines.map((l) => '\t'.repeat(n || 1) + l);
	const itemCreate = (cls) => 'Create' + (['HeldDevice', 'TDExplosive', 'ThrownDevice'].includes(cls) ? cls : 'HDFirearm');

	// ---- conditions (the lines inside a branch) ----

	function conditionLua(g, c) {
		switch (c.kind) {
			case 'funds': return 'self:GetTeamFunds(' + luaNum(c.team) + ') ' + opOf(c.op) + ' ' + luaNum(c.amount);
			case 'time': return 'st.clock.ElapsedSimTimeS ' + opOf(c.op) + ' ' + luaNum(c.seconds);
			case 'units': return 'studioCount(' + luaNum(c.team) + ', nil) ' + opOf(c.op) + ' ' + luaNum(c.count);
			case 'inArea': return 'studioCount(' + luaNum(c.team) + ', ' + area(c.area) + ') ' + opOf(c.op) + ' ' + luaNum(c.count);
			case 'flag': return '(st.flags[' + luaStr(c.name) + '] == true) == ' + (c.value ? 'true' : 'false');
			case 'stage': return '(st.stage == ' + luaStr(c.target) + ') == ' + (c.is === false ? 'false' : 'true');
			case 'battleScore': return '((st.battle and st.battle.scores[' + luaNum(c.team) + ']) or 0) ' + opOf(c.op) + ' ' + luaNum(c.count);
			default: return 'true';
		}
	}

	// ---- actions: the lines a step runs ----

	function actionLua(g, n) {
		const a = n.p || {};
		switch (n.kind) {
			case 'message': {
				const who = a.who === 'all' || a.who === undefined ? 'true' : 'player == ' + luaNum(a.who);
				return ['for player = 0, 3 do', '\tif self:PlayerActive(player) and self:PlayerHuman(player) and (' + who + ') then', '\t\tlocal screen = self:ScreenOfPlayer(player)', '\t\tFrameMan:ClearScreenText(screen)', '\t\tFrameMan:SetScreenText(' + luaStr(a.text) + ', screen, 0, ' + luaNum(Number(a.seconds) * 1000) + ', false)', '\tend', 'end'];
			}
			case 'gold': return ['self:ChangeTeamFunds(' + luaNum(a.amount) + ', ' + luaNum(a.team) + ')'];
			case 'spawn': {
				const parts = String(a.preset || '').split('|');
				const create = 'Create' + (parts[0] === 'ACrab' ? 'ACrab' : 'AHuman');
				return ['for i = 1, ' + luaNum(a.count) + ' do', '\tlocal where = ' + area(a.area), '\tlocal unit = ' + create + '(' + luaStr(parts[1] || '') + ', ' + luaStr(parts[2] || 'Base.rte') + ')', '\tif unit and where then', '\t\tunit.Team = ' + luaNum(a.team), '\t\tunit.Pos = where.RandomPoint', '\t\tMovableMan:AddActor(unit)', '\tend', 'end'];
			}
			case 'killAll': return ['local doomed = {}', 'for actor in MovableMan.Actors do', '\tif actor.Team == ' + luaNum(a.team) + ' then table.insert(doomed, actor) end', 'end', 'for _, actor in ipairs(doomed) do actor:GibThis() end'];
			case 'flag': return ['st.flags[' + luaStr(a.name) + '] = ' + (a.value ? 'true' : 'false')];
			case 'jump': return a.target && F.find(g, a.target) ? ['st.stage = ' + luaStr(a.target), 'st.stageStarted = false'] : ['st.stage = nil', 'st.stageStarted = false'];
			case 'win': return ['self.WinnerTeam = ' + luaNum(a.team), 'ActivityMan:EndActivity()'];
			case 'end': return ['ActivityMan:EndActivity()'];
			case 'call': return ['self:' + ident(a.fn) + '(actor)'];
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
			default: return ['-- (unknown step ' + n.kind + ')'];
		}
	}

	// ---- chains: a node and everything its then wire leads on to ----

	function chainLua(g, id, depth) {
		const L = [];
		let at = id;
		let guard = 0;
		while (at && guard++ < 300 && (depth || 0) < 40) {
			const n = F.find(g, at);
			const spec = n && F.KINDS[n.kind];
			if (!spec || spec.entrance === false) {
				break;
			}
			if (n.kind === 'branch') {
				const conds = (n.lists && n.lists.conds) || [];
				const test = conds.length ? conds.map((c) => '(' + conditionLua(g, c) + ')').join(' and ') : 'true';
				const yes = chainLua(g, (n.out || {}).true, (depth || 0) + 1);
				const no = conds.length ? chainLua(g, (n.out || {}).false, (depth || 0) + 1) : [];
				L.push('if ' + test + ' then', ...indent(yes.length ? yes : ['-- (nothing)']));
				if (no.length) {
					L.push('else', ...indent(no));
				}
				L.push('end');
				break;
			}
			if (n.kind === 'doOnce') {
				L.push('if not st.once[' + key(n.id) + '] then', '\tst.once[' + key(n.id) + '] = true', ...indent(chainLua(g, (n.out || {}).then, (depth || 0) + 1)), 'end');
				break;
			}
			L.push(...actionLua(g, n));
			at = (n.out || {}).then;
		}
		return L;
	}

	// ---- events: the lines that decide whether one has happened (setting fire) ----

	function eventLua(n) {
		const a = n.p || {};
		const k = key(n.id);
		const team = luaNum(a.team);
		// An event that stays true (a team gone, a timer that has passed) fires once, unless it is set to keep running.
		const latch = (lines) => (a.again ? lines : ['if not st.done[' + k + '] then', ...indent(lines), '\tif fire then st.done[' + k + '] = true end', 'end']);
		switch (n.kind) {
			case 'start': return ['if not st.started then fire = true end'];
			case 'timer': return a.repeat
				? ['local due = st.next[' + k + '] or ' + luaNum(a.seconds), 'if st.clock.ElapsedSimTimeS >= due then fire = true; st.next[' + k + '] = due + ' + luaNum(Math.max(0.1, Number(a.seconds))) + ' end']
				: latch(['if st.clock.ElapsedSimTimeS >= ' + luaNum(a.seconds) + ' then fire = true end']);
			case 'enter': case 'leave': case 'empty': {
				const lines = ['local now, count = studioInside(' + luaStr(a.area) + ', ' + team + ')', 'local before = st.inside[' + k + '] or {}'];
				if (n.kind === 'enter') {
					lines.push('for id in pairs(now) do if not before[id] then fire = true; st.who[' + k + '] = now[id] end end');
				} else if (n.kind === 'leave') {
					lines.push('for id in pairs(before) do if not now[id] then fire = true end end');
				} else {
					lines.push('if count == 0 and next(before) ~= nil then fire = true end');
				}
				lines.push('st.inside[' + k + '] = now');
				return lines;
			}
			case 'teamGone': return latch(['local alive = studioCount(' + team + ', nil)', 'if alive > 0 then st.seen[' + k + '] = true end', 'if st.seen[' + k + '] and alive == 0 then fire = true end']);
			case 'brainGone': return latch(['local alive = studioBrains(' + team + ')', 'if alive > 0 then st.seen[' + k + '] = true end', 'if st.seen[' + k + '] and alive == 0 then fire = true end']);
			case 'funds': return ['local reached = self:GetTeamFunds(' + team + ') ' + opOf(a.op) + ' ' + luaNum(a.amount), 'if reached and not st.prev[' + k + '] then fire = true end', 'st.prev[' + k + '] = reached'];
			case 'battleStarted': return ['if studioBattleEvent(st, "started", -1) then fire = true end'];
			case 'battleScore': return ['if studioBattleEvent(st, "score", ' + team + ') then fire = true end'];
			case 'battleObjective': return ['if studioBattleEvent(st, "objective", ' + team + ') then fire = true end'];
			case 'battleEnded': return ['if studioBattleEvent(st, "ended", ' + team + ') then fire = true end'];
			default: return ['fire = true']; // always
		}
	}

	// ---- stages ----

	function stageLua(g, cls, n) {
		const a = n.p || {};
		const id = n.id;
		const L = [];
		const wrap = (pin) => {
			const body = chainLua(g, (n.out || {})[pin], 0);
			return body.length ? indent(body) : ['\t-- (nothing)'];
		};
		L.push('-- Stage ' + String(a.name || id).replace(/[\r\n]+/g, ' '));
		L.push('function ' + cls + ':StudioStage_' + id + '_Start()', '\tlocal st = self.StudioState');
		if (n.kind === 'stageFetch' && a.item && a.from) {
			const parts = String(a.item).split('|');
			L.push('\tlocal where = ' + area(a.from), '\tlocal item = ' + itemCreate(parts[0]) + '(' + luaStr(parts[1] || '') + ', ' + luaStr(parts[2] || 'Base.rte') + ')', '\tif item and where then', '\t\titem.Pos = where.RandomPoint', '\t\tMovableMan:AddItem(item)', '\tend');
		}
		L.push(...wrap('started'), 'end');
		L.push('function ' + cls + ':StudioStage_' + id + '_Done()', '\tlocal st = self.StudioState', ...wrap('done'), 'end');
		L.push('function ' + cls + ':StudioStage_' + id + '_Fail()', '\tlocal st = self.StudioState', ...wrap('timeout'), 'end');
		const text = luaStr(a.text || a.name || '');
		const viewer = luaNum(a.viewer);
		L.push('function ' + cls + ':StudioStage_' + id + '_Mark()');
		if (a.marker) {
			if (['stageReach', 'stageHold', 'stageFetch'].includes(n.kind)) {
				L.push('\tlocal where = ' + area(a.area), '\tif where then self:AddObjectivePoint(' + text + ', where.Center, ' + viewer + ', GameActivity.ARROWDOWN) end');
			} else if (n.kind === 'stageDestroy' || n.kind === 'stageBrain') {
				L.push('\tfor actor in MovableMan.Actors do', '\t\tif actor.Team == ' + luaNum(a.team) + ' and actor.Health > 0' + (n.kind === 'stageBrain' ? ' and actor:IsInGroup("Brains")' : '') + ' then self:AddObjectivePoint(' + text + ', actor.AboveHUDPos, ' + viewer + ', GameActivity.ARROWDOWN) end', '\tend');
			} else {
				L.push('\t-- (the game lights up a battle\'s own objectives; nothing to mark for this goal)');
			}
		} else {
			L.push('\t-- (no marker)');
		}
		L.push('end');
		L.push('function ' + cls + ':StudioStage_' + id + '_Check()', '\tlocal st = self.StudioState', '\tlocal elapsed = st.stageClock.ElapsedSimTimeS');
		switch (n.kind) {
			case 'stageReach': L.push('\treturn studioCount(' + luaNum(a.team) + ', ' + area(a.area) + ') > 0'); break;
			case 'stageHold':
				L.push('\tlocal region = ' + area(a.area), '\tlocal held = region ~= nil and studioCount(' + luaNum(a.team) + ', region) > 0 and studioEnemies(' + luaNum(a.team) + ', region) == 0', '\tif not held then st.holdFrom = nil elseif not st.holdFrom then st.holdFrom = elapsed end', '\treturn st.holdFrom ~= nil and elapsed - st.holdFrom >= ' + luaNum(a.seconds));
				break;
			case 'stageDestroy': L.push('\treturn elapsed > 1 and studioCount(' + luaNum(a.team) + ', nil) == 0'); break;
			case 'stageBrain': L.push('\treturn elapsed > 1 and studioBrains(' + luaNum(a.team) + ') == 0'); break;
			case 'stageFetch': L.push('\treturn studioItemIn(' + luaStr(a.area) + ', ' + luaStr(String(a.item || '').split('|')[1] || '') + ')'); break;
			case 'stageBattle': L.push('\treturn st.battle ~= nil and st.battle.ended and (' + luaNum(a.team) + ' < 0 or st.battle.winner == ' + luaNum(a.team) + ')'); break;
			case 'stageBattleScore': L.push('\treturn ((st.battle and st.battle.scores[' + luaNum(a.team) + ']) or 0) >= ' + luaNum(a.count)); break;
			default: L.push('\treturn elapsed >= ' + luaNum(a.seconds));
		}
		L.push('end', '');
		return L;
	}

	// ---- the whole block ----

	function helpers() {
		return [
			'local function studioArea(name)', '\tif name ~= "" and SceneMan.Scene:HasArea(name) then return SceneMan.Scene:GetArea(name) end', '\treturn nil', 'end', '',
			'local function studioCount(team, region)', '\tlocal n = 0', '\tfor actor in MovableMan.Actors do', '\t\tif actor.Health > 0 and (team < 0 or actor.Team == team) and (region == nil or region:IsInside(actor.Pos)) then n = n + 1 end', '\tend', '\treturn n', 'end', '',
			'local function studioInside(name, team)', '\tlocal region = studioArea(name)', '\tlocal now, count = {}, 0', '\tif region then', '\t\tfor actor in MovableMan.Actors do', '\t\t\tif actor.Health > 0 and (team < 0 or actor.Team == team) and region:IsInside(actor.Pos) then now[actor.UniqueID] = actor; count = count + 1 end', '\t\tend', '\tend', '\treturn now, count', 'end', '',
			'-- What the game\'s battle mode reports (an older game build has none): kept in st.battle, with this frame\'s events in st.battleNow.',
			'local function studioPump(st)', '\tst.battleNow = {}', '\tif not SandboxBattleEvent then return end', '\tlocal line = SandboxBattleEvent()', '\twhile line ~= "" do',
			'\t\tlocal parts = {}', '\t\tfor token in line:gmatch("%S+") do parts[#parts + 1] = token end', '\t\tlocal name, a, b = parts[1], tonumber(parts[2]) or -1, tonumber(parts[3]) or 0',
			'\t\tst.battle = st.battle or { started = false, ended = false, winner = -1, scores = {} }',
			'\t\tif name == "started" then st.battle = { started = true, ended = false, winner = -1, scores = {} }', '\t\telseif name == "score" or name == "objective" then st.battle.scores[a] = b', '\t\telseif name == "ended" then st.battle.ended = true; st.battle.winner = a end',
			'\t\ttable.insert(st.battleNow, { name = name, team = a })', '\t\tline = SandboxBattleEvent()', '\tend', 'end', '',
			'local function studioBattleEvent(st, name, team)', '\tfor _, e in ipairs(st.battleNow or {}) do', '\t\tif e.name == name and (team < 0 or e.team == team) then return true end', '\tend', '\treturn false', 'end', '',
			'local function studioEnemies(team, region)', '\tlocal n = 0', '\tfor actor in MovableMan.Actors do', '\t\tif actor.Health > 0 and actor.Team >= 0 and actor.Team ~= team and region:IsInside(actor.Pos) then n = n + 1 end', '\tend', '\treturn n', 'end', '',
			'local function studioBrains(team)', '\tlocal n = 0', '\tfor actor in MovableMan.Actors do', '\t\tif actor.Health > 0 and actor.Team == team and actor:IsInGroup("Brains") then n = n + 1 end', '\tend', '\treturn n', 'end', '',
			'local function studioItemIn(areaName, itemName)', '\tlocal region = studioArea(areaName)', '\tif not region then return false end', '\tfor item in MovableMan.Items do', '\t\tif item.PresetName == itemName and region:IsInside(item.Pos) then return true end', '\tend', '\tfor actor in MovableMan.Actors do', '\t\tif actor.Health > 0 and region:IsInside(actor.Pos) then', '\t\t\tlocal held = actor.EquippedItem', '\t\t\tif held and held.PresetName == itemName then return true end', '\t\t\tfor item in actor.Inventory do', '\t\t\t\tif item.PresetName == itemName then return true end', '\t\t\tend', '\t\tend', '\tend', '\treturn false', 'end', '',
		];
	}

	function compile(g, cls) {
		const L = [];
		const events = F.entries(g).filter((n) => (n.out || {}).then);
		const stageNodes = F.stages(g);
		L.push(MARK_BEGIN + ': managed by Cortex Studio (the Scenarios editor, Mission logic). Change the mission there; this block is rewritten. Your own functions go outside it.');
		L.push(DATA + JSON.stringify(g));
		L.push(...helpers());
		for (const n of stageNodes) {
			L.push(...stageLua(g, cls, n));
		}
		const first = F.firstStage(g);
		L.push(cls + '.StudioFirstStage = ' + (first ? luaStr(first.id) : 'nil'));
		L.push(cls + '.StudioStages = {', ...stageNodes.map((n) => '\t[' + luaStr(n.id) + '] = { next = ' + ((n.out || {}).next && F.find(g, n.out.next) ? luaStr(n.out.next) : 'nil') + ', limit = ' + luaNum((n.p || {}).failAfter) + ' },'), '}', '');
		for (const n of events) {
			const label = String(n.note || (F.KINDS[n.kind] || {}).title || n.kind).replace(/[\r\n]+/g, ' ');
			L.push('-- ' + label, 'function ' + cls + ':StudioFlow_' + n.id + '(actor)', '\tlocal st = self.StudioState', ...indent(chainLua(g, n.out.then, 0)), 'end', '');
		}
		L.push('function ' + cls + ':RunStudioFlow()');
		L.push('\tself.StudioState = self.StudioState or { clock = Timer(), flags = {}, inside = {}, who = {}, seen = {}, prev = {}, next = {}, done = {}, once = {}, started = false }');
		L.push('\tlocal st = self.StudioState', '\tstudioPump(st)');
		if (stageNodes.length) {
			L.push('\tif ' + cls + '.StudioFirstStage and not st.stagesBegun then st.stage = ' + cls + '.StudioFirstStage; st.stagesBegun = true end');
			L.push('\tif st.stage then',
				'\t\tlocal id = st.stage',
				'\t\tlocal info = ' + cls + '.StudioStages[id]',
				'\t\tif not info then',
				'\t\t\tst.stage = nil',
				'\t\telse',
				'\t\t\tif not st.stageStarted then',
				'\t\t\t\tst.stageStarted = true', '\t\t\t\tst.stageClock = Timer()', '\t\t\t\tst.holdFrom = nil',
				'\t\t\t\tself["StudioStage_" .. id .. "_Start"](self)',
				'\t\t\tend',
				'\t\t\tif st.stage == id then',
				'\t\t\t\tself["StudioStage_" .. id .. "_Mark"](self)',
				'\t\t\t\tif self["StudioStage_" .. id .. "_Check"](self) then',
				'\t\t\t\t\tself["StudioStage_" .. id .. "_Done"](self)',
				'\t\t\t\t\tif st.stage == id then st.stage = info.next; st.stageStarted = false end',
				'\t\t\t\telseif info.limit > 0 and st.stageClock.ElapsedSimTimeS >= info.limit then',
				'\t\t\t\t\tself["StudioStage_" .. id .. "_Fail"](self)',
				'\t\t\t\t\tif st.stage == id then st.stage = info.next; st.stageStarted = false end',
				'\t\t\t\tend',
				'\t\t\tend',
				'\t\tend',
				'\tend');
		}
		for (const n of events) {
			L.push('\tdo -- ' + n.id, '\t\tlocal fire = false', ...indent(eventLua(n), 2), '\t\tif fire then self:StudioFlow_' + n.id + '(st.who[' + key(n.id) + ']) end', '\tend');
		}
		L.push('\tst.started = true', 'end', MARK_END);
		return L.join('\n');
	}

	// ---- reading and writing the script ----

	// The graph in a script, or null when there is none (an older block's rules or stages are turned into a graph).
	function parse(code) {
		if (!code) {
			return null;
		}
		const tryLine = (marker) => {
			const at = code.indexOf(marker);
			if (at < 0) {
				return undefined;
			}
			const end = code.indexOf('\n', at);
			try {
				return JSON.parse(code.slice(at + marker.length, end < 0 ? undefined : end).replace(/\r$/, ''));
			} catch (e) {
				return null;
			}
		};
		const now = tryLine(DATA);
		if (now) {
			return F.migrate(now);
		}
		for (const [, , marker] of LEGACY) {
			const old = marker ? tryLine(marker) : undefined;
			if (old) {
				return F.migrate(old);
			}
		}
		return null;
	}

	// The script with the flow block replaced (or added before UpdateActivity), UpdateActivity running it, and a function written for each of your own
	// functions the graph calls that the script does not have yet.
	function apply(code, cls, g) {
		let text = String(code || '').replace(/\r\n/g, '\n');
		for (const [begin, end] of [[MARK_BEGIN, MARK_END], ...LEGACY.map(([b, e]) => [b, e])]) {
			const from = text.indexOf(begin);
			const to = text.indexOf(end);
			if (from >= 0 && to > from) {
				text = text.slice(0, from).replace(/\s*$/, '') + '\n\n' + text.slice(to + end.length).replace(/^\n+/, '');
			}
		}
		const block = compile(g, cls);
		const update = new RegExp('function\\s+' + cls + ':UpdateActivity\\s*\\(\\s*\\)');
		if (update.test(text)) {
			text = text.replace(update, (m) => block + '\n\n' + m);
		} else {
			text = text.replace(/\s*$/, '') + '\n\n' + block + '\n\nfunction ' + cls + ':UpdateActivity()\n\tself:RunStudioFlow()\nend\n';
		}
		// UpdateActivity runs the flow, and no longer the first versions' functions
		text = text.replace(/\n[ \t]*self:RunStudioRules\(\)/g, '').replace(/\n[ \t]*self:RunStudioTriggers\(\)/g, '');
		const body = new RegExp(cls + ':UpdateActivity\\s*\\(\\s*\\)[^]*?:RunStudioFlow\\(\\)');
		if (!body.test(text)) {
			text = text.replace(update, (m) => m + '\n\tself:RunStudioFlow()');
		}
		for (const n of g.nodes) {
			if (n.kind === 'call') {
				const name = ident((n.p || {}).fn);
				if (!new RegExp('function\\s+' + cls + ':' + name + '\\s*\\(').test(text)) {
					text = text.replace(/\s*$/, '') + '\n\n-- Called by a "Run my own function" step in the mission. actor is the unit that came in, when the chain began with a unit entering an area.\nfunction ' + cls + ':' + name + '(actor)\n\t\nend\n';
				}
			}
		}
		return text;
	}

	MS.flow = Object.assign(MS.flow || {}, { compile, parse, apply, luaStr, ident, className, isClassName, MARK_BEGIN, MARK_END });
})();
