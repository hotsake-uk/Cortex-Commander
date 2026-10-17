// Cortex Mod Studio: mission logic. A scenario's rules, built from blocks and turned into Lua: WHEN something happens, IF some conditions hold, THEN do
// some actions. No code is needed for the common things (a message, gold, spawning a squad, winning); for anything else an action calls a
// function of your own, which Studio adds to the script for you to fill in, and the editor jumps to it (or opens it beside).
//   When   the scenario starts, after some seconds (or every so many), a unit enters / leaves / empties an area, a team has no units left,
//          a team's gold reaches an amount, or every moment
//   If     gold, time, units in an area, units in all, a flag you set earlier
//   Then   show a message, give or take gold, spawn units in an area, set a flag, kill a team's units, end with a winner or a draw, call your function
// The rules are kept in the script inside a marked block, as data on one comment line, with the Lua generated from it. Change the rules here, not in
// the block (it is rewritten each time); your own functions outside it are never touched. Older "Studio triggers" blocks are turned into rules.
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

	const OPS = [['>=', 'is at least'], ['<=', 'is at most'], ['==', 'is exactly'], ['>', 'is more than'], ['<', 'is less than']];

	const WHEN = {
		start: { label: 'the scenario starts', params: [] },
		timer: { label: 'after some seconds', params: [['seconds', 'number', 'seconds', 30], ['repeat', 'bool', 'and then again every time', false]] },
		enter: { label: 'a unit enters an area', params: [['area', 'area', 'area', ''], ['team', 'team', 'whose', -1]] },
		leave: { label: 'a unit leaves an area', params: [['area', 'area', 'area', ''], ['team', 'team', 'whose', -1]] },
		empty: { label: 'an area is emptied', params: [['area', 'area', 'area', ''], ['team', 'team', 'of', -1]] },
		teamGone: { label: 'a team has no units left', params: [['team', 'team', 'team', 0]] },
		funds: { label: 'a team\'s gold reaches', params: [['team', 'team', 'team', 0], ['op', 'op', '', '>='], ['amount', 'number', 'gold', 1000]] },
		always: { label: 'every moment (use conditions)', params: [] },
	};

	const IF = {
		funds: { label: 'a team\'s gold', params: [['team', 'team', 'team', 0], ['op', 'op', '', '>='], ['amount', 'number', 'gold', 1000]] },
		time: { label: 'the time since it started', params: [['op', 'op', '', '>='], ['seconds', 'number', 'seconds', 60]] },
		units: { label: 'units of a team', params: [['team', 'team', 'team', 0], ['op', 'op', '', '>='], ['count', 'number', 'count', 1]] },
		inArea: { label: 'units of a team in an area', params: [['team', 'team', 'team', 0], ['area', 'area', 'area', ''], ['op', 'op', '', '>='], ['count', 'number', 'count', 1]] },
		flag: { label: 'a flag', params: [['name', 'text', 'name', 'flag'], ['value', 'bool', 'is set', true]] },
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
			case 'funds': return ['local reached = self:GetTeamFunds(' + team + ') ' + opOf(w.op) + ' ' + luaNum(w.amount), 'if reached and not st.prev[' + i + '] then fire = true end', 'st.prev[' + i + '] = reached'];
			default: return ['fire = true'];
		}
	}

	// A rule fires once if it says so, and always for events that stay true (a team gone, a timer that does not repeat, the start).
	const once = (r) => r.once || ['start', 'teamGone'].includes(r.when.kind) || (r.when.kind === 'timer' && !r.when.repeat);

	function generate(cls, rules) {
		const L = [];
		L.push(MARK_BEGIN + ': managed by Cortex Studio (the Scenarios editor, Mission logic). Change the rules there; this block is rewritten. Your own functions go outside it.');
		L.push(DATA + JSON.stringify(rules));
		L.push('local function studioArea(name)', '\tif name ~= "" and SceneMan.Scene:HasArea(name) then return SceneMan.Scene:GetArea(name) end', '\treturn nil', 'end', '');
		L.push('local function studioCount(team, region)', '\tlocal n = 0', '\tfor actor in MovableMan.Actors do', '\t\tif actor.Health > 0 and (team < 0 or actor.Team == team) and (region == nil or region:IsInside(actor.Pos)) then n = n + 1 end', '\tend', '\treturn n', 'end', '');
		L.push('local function studioInside(name, team)', '\tlocal region = studioArea(name)', '\tlocal now, count = {}, 0', '\tif region then', '\t\tfor actor in MovableMan.Actors do', '\t\t\tif actor.Health > 0 and (team < 0 or actor.Team == team) and region:IsInside(actor.Pos) then now[actor.UniqueID] = actor; count = count + 1 end', '\t\tend', '\tend', '\treturn now, count', 'end', '');
		rules.forEach((r, i) => {
			L.push('-- Rule ' + (i + 1) + ': ' + r.name.replace(/[\r\n]+/g, ' '));
			L.push('function ' + cls + ':StudioRule' + (i + 1) + '(actor)');
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
				return Array.isArray(data) ? data : null;
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
	function withRules(code, cls, rules) {
		let text = code.replace(/\r\n/g, '\n');
		for (const [b, e] of [[MARK_BEGIN, MARK_END], [OLD_BEGIN, OLD_END]]) {
			const begin = text.indexOf(b);
			const end = text.indexOf(e);
			if (begin >= 0 && end > begin) {
				text = text.slice(0, begin).replace(/\s*$/, '') + '\n\n' + text.slice(end + e.length).replace(/^\n+/, '');
			}
		}
		const block = generate(cls, rules);
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
		let rules = parseRules(code);
		let migrated = false;
		if (rules === null) {
			rules = code ? oldTriggers(code) : [];
			migrated = rules.length > 0;
		}
		const areaList = [...new Set(sceneAreas)];
		defaultArea = areaList[0] || '';
		const save = (next) => host.saveText(path, withRules(host.scriptText(path) || '', cls, next)).catch((err) => app().toast('Couldn\'t change the script: ' + err.message, 'bad'));
		const edit = (mutate) => {
			const next = JSON.parse(JSON.stringify(rules));
			mutate(next);
			return save(next);
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
