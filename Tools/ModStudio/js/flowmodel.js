// Cortex Mod Studio: the flow model. A mission's logic is a graph of nodes joined by execution wires, like a blueprint: an event node starts a chain
// (when a unit enters an area, when a stage begins...), and the nodes after it run in order, branching and gating as the wires say. This file is only the
// data: what kinds of node there are and what each asks for, the graph and the operations on it (add, connect, remove, move), checking it for mistakes,
// and bringing the older rules-and-stages data into it. It touches no page and no Lua, so it runs the same in a test as in Studio. flowlua.js turns a
// graph into the script; flowview.js draws and edits it.
//
// A graph:   { v: 3, seq: <last id number>, nodes: [ node ] }
// A node:    { id: "n7", kind: "enter", p: { area: "LZ", team: -1 }, x: 120, y: 40, out: { then: "n8" } }
//            p holds the node's settings (the kind says which), out says which node each exit wire goes to (a pin with no wire is missing or null).
// Pins:      an event node has one exit, then. An action or a do-once node has an entrance and one exit, then. A branch has an entrance and two exits,
//            true and false. A stage has the exits started, done and timeout (what it runs when it begins, when its goal is met and when time runs out) and
//            next, which points at the stage to go to when it is done. Execution wires never loop back (checked as they are made); stage wires may.
// Nothing here ever throws on bad data: a graph is always something that can be shown, with problems() saying what is wrong with it.
'use strict';
window.MS = window.MS || {};

(function () {
	// ---- the kinds of setting ----
	// A setting is [key, type, label, default]. Types: team, area, number, text, op, bool, player (all or 1-4), fn (a function name), preset (a unit),
	// item, mode (a battle mode), stage (another stage node's id, picked by name).

	const BATTLE_MODES = [[1, 'Capture the flag'], [2, 'King of the hill'], [3, 'Assault'], [4, 'Last team standing'], [5, 'VIP hunt'], [6, 'One flag'], [7, 'Capture the ship'], [8, 'Wave defence'], [9, 'Escort'], [10, 'Extraction'], [11, 'The point']];
	const OPS = [['>=', 'is at least'], ['<=', 'is at most'], ['==', 'is exactly'], ['>', 'is more than'], ['<', 'is less than']];

	// ---- the registry ----

	const KINDS = {};
	const ORDER = []; // the order kinds are listed in, by category

	function kind(name, category, title, params, pins, extra) {
		KINDS[name] = Object.assign({ name, category, title, params, entrance: pins.entrance !== false, exits: pins.exits }, extra || {});
		ORDER.push(name);
	}

	const EVENT = { entrance: false, exits: ['then'] };
	const STEP = { entrance: true, exits: ['then'] };

	// events: how a chain begins
	kind('start', 'event', 'The mission starts', [], EVENT);
	kind('timer', 'event', 'After some seconds', [['seconds', 'number', 'seconds', 30], ['repeat', 'bool', 'and then again every time', false], ['again', 'bool', 'run again every moment it still holds', false]], EVENT);
	kind('enter', 'event', 'A unit enters an area', [['area', 'area', 'area', ''], ['team', 'team', 'whose', -1]], EVENT);
	kind('leave', 'event', 'A unit leaves an area', [['area', 'area', 'area', ''], ['team', 'team', 'whose', -1]], EVENT);
	kind('empty', 'event', 'An area is emptied', [['area', 'area', 'area', ''], ['team', 'team', 'of', -1]], EVENT);
	kind('teamGone', 'event', 'A team has no units left', [['team', 'team', 'team', 0], ['again', 'bool', 'run again every moment it still holds', false]], EVENT);
	kind('brainGone', 'event', 'A team\'s brain is destroyed', [['team', 'team', 'team', 0], ['again', 'bool', 'run again every moment it still holds', false]], EVENT);
	kind('funds', 'event', 'A team\'s gold reaches', [['team', 'team', 'team', 0], ['op', 'op', '', '>='], ['amount', 'number', 'gold', 1000]], EVENT);
	kind('always', 'event', 'Every moment', [], EVENT);
	kind('battleStarted', 'event', 'A battle begins', [], EVENT);
	kind('battleScore', 'event', 'A team scores in the battle', [['team', 'team', 'team', -1]], EVENT);
	kind('battleObjective', 'event', 'An objective is taken in the battle', [['team', 'team', 'team', -1]], EVENT);
	kind('battleEnded', 'event', 'The battle ends', [['team', 'team', 'won by', -1]], EVENT);

	// stages: goals in a sequence. Every stage also has a name, an objective text and marker, whose screen shows it, and a time limit.
	const STAGE_COMMON = [['name', 'text', 'name', 'Stage'], ['text', 'text', 'objective text', ''], ['marker', 'bool', 'show a marker', true], ['viewer', 'team', 'marker seen by', 0], ['failAfter', 'number', 'time limit (seconds, 0: none)', 0]];
	const STAGE_PINS = { entrance: false, exits: ['started', 'done', 'timeout', 'next'] };
	const stage = (name, title, goalParams) => kind(name, 'stage', title, goalParams.concat(STAGE_COMMON), STAGE_PINS);
	stage('stageReach', 'Goal: a unit reaches an area', [['area', 'area', 'area', ''], ['team', 'team', 'whose', 0]]);
	stage('stageHold', 'Goal: hold an area for some seconds', [['area', 'area', 'area', ''], ['team', 'team', 'team', 0], ['seconds', 'number', 'seconds', 60]]);
	stage('stageDestroy', 'Goal: a team has no units left', [['team', 'team', 'team', 1]]);
	stage('stageBrain', 'Goal: a team\'s brain is destroyed', [['team', 'team', 'team', 1]]);
	stage('stageFetch', 'Goal: an item is brought to an area', [['item', 'item', 'item', ''], ['from', 'area', 'put it in', ''], ['area', 'area', 'bring it to', '']]);
	stage('stageSurvive', 'Goal: time passes', [['seconds', 'number', 'seconds', 120]]);
	stage('stageBattle', 'Goal: the battle is won', [['team', 'team', 'by', -1]]);
	stage('stageBattleScore', 'Goal: a team scores in the battle', [['team', 'team', 'team', 0], ['count', 'number', 'points', 1]]);

	// flow control
	kind('branch', 'flow', 'Branch: if these hold', [], { entrance: true, exits: ['true', 'false'] }, { lists: ['conds'] });
	kind('doOnce', 'flow', 'Do this only once', [], STEP);

	// actions
	kind('message', 'action', 'Show a message', [['text', 'text', 'text', 'Well done.'], ['who', 'player', 'to', 'all'], ['seconds', 'number', 'for seconds', 5]], STEP);
	kind('gold', 'action', 'Give gold', [['team', 'team', 'to team', 0], ['amount', 'number', 'gold (negative takes it)', 500]], STEP);
	kind('spawn', 'action', 'Spawn units', [['preset', 'preset', 'unit', ''], ['team', 'team', 'for team', 0], ['area', 'area', 'in area', ''], ['count', 'number', 'how many', 1]], STEP);
	kind('killAll', 'action', 'Destroy a team\'s units', [['team', 'team', 'team', 0]], STEP);
	kind('flag', 'action', 'Set a flag', [['name', 'text', 'name', 'flag'], ['value', 'bool', 'to set', true]], STEP);
	kind('jump', 'action', 'Jump to a stage', [['target', 'stage', 'stage', '']], STEP);
	kind('win', 'action', 'End it: a team wins', [['team', 'team', 'team', 0]], STEP);
	kind('end', 'action', 'End it with no winner', [], STEP);
	kind('call', 'action', 'Run my own function', [['fn', 'fn', 'function', 'MyFunction']], STEP);
	kind('battle', 'action', 'Start a battle', [['mode', 'mode', 'mode', 2], ['size', 'number', 'units a side', 12], ['byShip', 'bool', 'they arrive by ship', false], ['layout', 'bool', 'lay the map out for me', true], ['seed', 'number', 'layout seed', 1]], STEP);
	kind('battleStop', 'action', 'Stop the battle', [], STEP);

	// conditions: not nodes, but the lines inside a branch
	const CONDS = {
		funds: { title: 'a team\'s gold', params: [['team', 'team', 'team', 0], ['op', 'op', '', '>='], ['amount', 'number', 'gold', 1000]] },
		time: { title: 'the time since it started', params: [['op', 'op', '', '>='], ['seconds', 'number', 'seconds', 60]] },
		units: { title: 'units of a team', params: [['team', 'team', 'team', 0], ['op', 'op', '', '>='], ['count', 'number', 'count', 1]] },
		inArea: { title: 'units of a team in an area', params: [['team', 'team', 'team', 0], ['area', 'area', 'area', ''], ['op', 'op', '', '>='], ['count', 'number', 'count', 1]] },
		flag: { title: 'a flag', params: [['name', 'text', 'name', 'flag'], ['value', 'bool', 'is set', true]] },
		stage: { title: 'the stage it is on', params: [['target', 'stage', 'stage', ''], ['is', 'bool', 'is the current one', true]] },
		battleScore: { title: 'a team\'s score in the battle', params: [['team', 'team', 'team', 0], ['op', 'op', '', '>='], ['count', 'number', 'points', 1]] },
	};

	const CATEGORIES = [['event', 'Events', 'How a chain begins'], ['stage', 'Stages', 'Goals, one after another'], ['flow', 'Flow', 'Decide and gate'], ['action', 'Actions', 'What happens']];

	const isStage = (k) => !!KINDS[k] && KINDS[k].category === 'stage';
	const defaults = (params) => Object.assign({}, ...params.map(([key, , , def]) => ({ [key]: def })));
	const condDefaults = (name) => Object.assign({ kind: name }, defaults(CONDS[name].params));

	// ---- the graph ----

	function create() {
		return { v: 3, seq: 0, nodes: [] };
	}

	const find = (g, id) => g.nodes.find((n) => n.id === id) || null;

	// A new node of a kind, at a place. Unknown kinds are refused (null).
	function addNode(g, kindName, x, y, p) {
		const spec = KINDS[kindName];
		if (!spec) {
			return null;
		}
		g.seq = (Number(g.seq) || 0) + 1;
		const node = { id: 'n' + g.seq, kind: kindName, p: Object.assign(defaults(spec.params), p || {}), x: Number.isFinite(x) ? Math.round(x) : null, y: Number.isFinite(y) ? Math.round(y) : null, out: {} };
		if (spec.lists) {
			node.lists = {};
			for (const l of spec.lists) {
				node.lists[l] = [];
			}
		}
		g.nodes.push(node);
		return node;
	}

	// The nodes that wire into this one: [{from, pin}].
	function incoming(g, id) {
		const found = [];
		for (const n of g.nodes) {
			for (const [pin, to] of Object.entries(n.out || {})) {
				if (to === id) {
					found.push({ from: n.id, pin });
				}
			}
		}
		return found;
	}

	// Whether following execution wires (not stage next wires) from one node can reach another.
	function reaches(g, fromId, toId, seen) {
		seen = seen || new Set();
		if (fromId === toId) {
			return true;
		}
		if (seen.has(fromId)) {
			return false;
		}
		seen.add(fromId);
		const n = find(g, fromId);
		if (!n) {
			return false;
		}
		return Object.entries(n.out || {}).some(([pin, to]) => to && pin !== 'next' && reaches(g, to, toId, seen));
	}

	// Why a wire cannot be made, or '' when it can.
	function whyNot(g, fromId, pin, toId) {
		const from = find(g, fromId);
		const to = find(g, toId);
		if (!from || !to) {
			return 'That node is not there.';
		}
		const spec = KINDS[from.kind];
		if (!spec || !spec.exits.includes(pin)) {
			return 'That exit does not exist on this node.';
		}
		if (fromId === toId) {
			return 'A node cannot be wired to itself.';
		}
		if (pin === 'next') {
			return isStage(to.kind) ? '' : 'The next stage has to be a stage.';
		}
		const target = KINDS[to.kind];
		if (!target || !target.entrance) {
			return 'Nothing can be wired into ' + (target ? target.title.toLowerCase() : 'that') + ': it starts a chain.';
		}
		if (reaches(g, toId, fromId)) {
			return 'That would make a loop: a chain cannot run back into itself.';
		}
		return '';
	}

	function connect(g, fromId, pin, toId) {
		const why = whyNot(g, fromId, pin, toId);
		if (why) {
			return why;
		}
		find(g, fromId).out[pin] = toId;
		return '';
	}

	function disconnect(g, fromId, pin) {
		const from = find(g, fromId);
		if (from && from.out) {
			delete from.out[pin];
		}
	}

	// Takes a node out and every wire that went to it. With bridge, a step in the middle of a chain is taken out and its neighbours joined up.
	function removeNode(g, id, bridge) {
		const node = find(g, id);
		if (!node) {
			return;
		}
		const after = (node.out || {}).then || null;
		g.nodes = g.nodes.filter((n) => n.id !== id);
		for (const n of g.nodes) {
			for (const [pin, to] of Object.entries(n.out || {})) {
				if (to === id) {
					if (bridge && after && pin !== 'next' && !whyNot(g, n.id, pin, after)) {
						n.out[pin] = after;
					} else {
						delete n.out[pin];
					}
				}
			}
		}
	}

	const clone = (g) => JSON.parse(JSON.stringify(g));

	// A condition line for a branch.
	function addCondition(node, name) {
		if (!CONDS[name]) {
			return null;
		}
		const c = condDefaults(name);
		node.lists.conds.push(c);
		return c;
	}

	// ---- looking at a graph ----

	// The chain of nodes from a wire onward, in the order they run (a branch stops it: its two sides are separate chains).
	function chainFrom(g, id) {
		const chain = [];
		const seen = new Set();
		let at = id;
		while (at && !seen.has(at)) {
			const n = find(g, at);
			if (!n) {
				break;
			}
			seen.add(at);
			chain.push(n);
			at = KINDS[n.kind] && KINDS[n.kind].exits.length === 1 && KINDS[n.kind].exits[0] === 'then' ? (n.out || {}).then : null;
		}
		return chain;
	}

	const entries = (g) => g.nodes.filter((n) => KINDS[n.kind] && !KINDS[n.kind].entrance && KINDS[n.kind].category === 'event');
	const stages = (g) => g.nodes.filter((n) => isStage(n.kind));

	// The stage that starts the mission: the first stage (in the order they were made) that no other stage goes on to.
	function firstStage(g) {
		const targets = new Set();
		for (const s of stages(g)) {
			if (s.out && s.out.next) {
				targets.add(s.out.next);
			}
		}
		return stages(g).find((s) => !targets.has(s.id)) || null;
	}

	// ---- mistakes ----
	// ctx: { areas: [names the scene has, or null if unknown], hasScript: bool }. Each problem: { id, level: 'error' | 'warn', message }.

	function problems(g, ctx) {
		const found = [];
		const areas = ctx && Array.isArray(ctx.areas) ? ctx.areas : null;
		const add = (id, level, message) => found.push({ id, level, message });
		for (const n of g.nodes) {
			const spec = KINDS[n.kind];
			if (!spec) {
				add(n.id, 'error', 'Studio does not know a "' + n.kind + '" node, so it is left out of the script.');
				continue;
			}
			for (const [key, type, label] of spec.params) {
				const v = (n.p || {})[key];
				if (type === 'area') {
					if (!v) {
						add(n.id, 'error', 'Pick the ' + label + '.');
					} else if (areas && !areas.includes(v)) {
						add(n.id, 'warn', 'The scene has no area called "' + v + '".');
					}
				} else if (type === 'item' || type === 'preset') {
					if (!v) {
						add(n.id, 'error', 'Pick the ' + label + '.');
					}
				} else if (type === 'fn' && !/^[A-Za-z_][A-Za-z0-9_ ]*$/.test(String(v || ''))) {
					add(n.id, 'error', 'Give the function a name made of letters and numbers.');
				} else if (type === 'stage' && (!v || !find(g, v) || !isStage(find(g, v).kind))) {
					add(n.id, 'error', 'Pick which stage to jump to.');
				}
			}
			if (n.kind === 'branch') {
				const conds = (n.lists && n.lists.conds) || [];
				if (!conds.length) {
					add(n.id, 'warn', 'A branch with no conditions always takes the true side.');
				}
				for (const c of conds) {
					for (const [key, type, label] of (CONDS[c.kind] ? CONDS[c.kind].params : [])) {
						const v = c[key];
						if (type === 'area' && (!v || (areas && !areas.includes(v)))) {
							add(n.id, v ? 'warn' : 'error', v ? 'The scene has no area called "' + v + '".' : 'A condition needs an area.');
						}
						if (type === 'stage' && (!v || !find(g, v))) {
							add(n.id, 'error', 'A condition needs a stage.');
						}
					}
				}
			}
			for (const pin of spec.exits) {
				const to = (n.out || {})[pin];
				if (to && !find(g, to)) {
					add(n.id, 'error', 'The ' + pin + ' wire goes to a node that is not there.');
				}
			}
			if (spec.entrance && !incoming(g, n.id).length) {
				add(n.id, 'warn', 'Nothing leads to this, so it never runs.');
			}
			if (spec.category === 'event' && !(n.out || {}).then) {
				add(n.id, 'warn', 'This event does nothing: wire something after it.');
			}
		}
		const starts = stages(g).filter((s) => !incoming(g, s.id).some((i) => i.pin === 'next'));
		if (starts.length > 1) {
			for (const s of starts.slice(1)) {
				add(s.id, 'warn', 'Only the first stage runs from the start. Wire another stage\'s next exit to this one, or jump to it.');
			}
		}
		// A loop of execution wires should never exist (connect refuses them), but data may come from a hand-edited script.
		for (const n of g.nodes) {
			for (const [pin, to] of Object.entries(n.out || {})) {
				if (to && pin !== 'next' && find(g, to) && reaches(g, to, n.id)) {
					add(n.id, 'error', 'This is part of a loop of wires, which cannot run.');
					break;
				}
			}
		}
		return found;
	}

	// ---- laying out ----

	const COL = 330;
	const ROW = 190;

	// Places every node by following the wires: events on the left in rows, what they lead to going right, a branch's false side below its true side,
	// stages in a row along the top with what they run hanging below. Moves only nodes that have no place yet unless all is true.
	function layout(g, all) {
		const placed = new Set();
		const needs = (n) => all || !Number.isFinite(n.x) || !Number.isFinite(n.y);
		let top = 0;
		// stages: a column each, in order of the next wires
		const order = [];
		const seen = new Set();
		const first = firstStage(g);
		for (let s = first; s && !seen.has(s.id); s = find(g, (s.out || {}).next)) {
			seen.add(s.id);
			order.push(s);
		}
		for (const s of stages(g)) {
			if (!seen.has(s.id)) {
				order.push(s);
			}
		}
		let stageBottom = 0;
		order.forEach((s, i) => {
			const x = i * COL;
			if (needs(s)) {
				s.x = x;
				s.y = 0;
			}
			placed.add(s.id);
			let y = ROW * 1.25;
			for (const pin of ['started', 'done', 'timeout']) {
				const chain = chainFrom(g, (s.out || {})[pin]);
				for (const n of chain) {
					if (!placed.has(n.id) && needs(n)) {
						n.x = x;
						n.y = y;
					}
					placed.add(n.id);
					y += ROW * 0.8;
				}
			}
			stageBottom = Math.max(stageBottom, y);
		});
		top = order.length ? stageBottom + ROW * 0.4 : 0;
		// events: a row each
		let row = top;
		const place = (id, x, y) => {
			const n = find(g, id);
			if (!n || placed.has(n.id)) {
				return y;
			}
			placed.add(n.id);
			if (needs(n)) {
				n.x = x;
				n.y = y;
			}
			if (n.kind === 'branch') {
				let end = place((n.out || {}).true, x + COL, y);
				const falseTop = (n.out || {}).false ? Math.max(end, y + ROW * 0.9) : y;
				end = Math.max(end, place((n.out || {}).false, x + COL, falseTop));
				return Math.max(end, y + ROW * 0.9);
			}
			return Math.max(place((n.out || {}).then, x + COL, y), y + ROW * 0.9);
		};
		for (const e of entries(g)) {
			row = Math.max(row, place(e.id, 0, row)) + ROW * 0.15;
		}
		// whatever is left (nothing wires to it): in a row at the bottom
		let x = 0;
		for (const n of g.nodes) {
			if (!placed.has(n.id)) {
				if (needs(n)) {
					n.x = x;
					n.y = row;
				}
				x += COL;
			}
		}
		return g;
	}

	// ---- from the older rules-and-stages data ----
	// v1 was a list of rules; v2 was { rules, stages }. A rule was WHEN event, IF conditions, THEN actions, and once. A stage had a goal, and actions for
	// starting, finishing and running out of time. Both become a graph that does the same: a rule an event, a branch for its conditions, a do-once where
	// it fired once, then its actions; a stage a stage node whose next wire goes to the stage after.

	// Events that fire once by themselves (they latch, unless told to keep running): the start, a team or brain gone, a timer that does not repeat.
	const latched = (e) => e.kind === 'start' || e.kind === 'teamGone' || e.kind === 'brainGone' || (e.kind === 'timer' && !(e.p || {}).repeat);

	function migrate(data) {
		if (data && data.v === 3 && Array.isArray(data.nodes)) {
			return data;
		}
		const rules = Array.isArray(data) ? data : data && Array.isArray(data.rules) ? data.rules : [];
		const stageList = data && Array.isArray(data.stages) ? data.stages : [];
		const g = create();

		const chain = (items, fromNode, pin) => {
			let prev = fromNode;
			let prevPin = pin;
			for (const a of items || []) {
				const actionKind = a.kind === 'stage' ? 'jump' : a.kind;
				if (!KINDS[actionKind]) {
					continue;
				}
				const p = Object.assign({}, a);
				delete p.kind;
				if (a.kind === 'stage') {
					// an old jump named a stage by its number
					const target = stageNodes[Number(a.n) - 1];
					p.target = target ? target.id : '';
					delete p.n;
				}
				const node = addNode(g, actionKind, 0, 0, p);
				prev.out[prevPin] = node.id;
				prev = node;
				prevPin = 'then';
			}
			return prev;
		};

		const stageNodes = [];
		const goalKinds = { reach: 'stageReach', hold: 'stageHold', destroy: 'stageDestroy', brain: 'stageBrain', fetch: 'stageFetch', survive: 'stageSurvive', battle: 'stageBattle', battleScore: 'stageBattleScore' };
		for (const st of stageList) {
			const goal = st.goal || {};
			const p = Object.assign({}, goal, { name: st.name, text: st.text || '', marker: !!st.marker, viewer: Number(st.viewer) || 0, failAfter: Number(st.failAfter) || 0 });
			delete p.kind;
			stageNodes.push(addNode(g, goalKinds[goal.kind] || 'stageSurvive', 0, 0, p));
		}
		stageList.forEach((st, i) => {
			const node = stageNodes[i];
			chain(st.onStart, node, 'started');
			chain(st.onDone, node, 'done');
			chain(st.onFail, node, 'timeout');
			if (stageNodes[i + 1]) {
				node.out.next = stageNodes[i + 1].id;
			}
		});

		for (const r of rules) {
			const w = r.when || { kind: 'start' };
			if (!KINDS[w.kind]) {
				continue;
			}
			const p = Object.assign({}, w);
			delete p.kind;
			const event = addNode(g, w.kind, null, null, p);
			let tail = event;
			let pin = 'then';
			const conds = (r.ifs || []).map((c) => {
				const q = Object.assign({}, c);
				if (c.kind === 'stage') {
					// an old stage test compared the stage number: "is stage n" becomes "is that stage", the others have no direct form and stay as is
					const target = stageNodes[Number(c.n) - 1];
					return { kind: 'stage', target: target ? target.id : '', is: (c.op || '==') !== '!=' };
				}
				return q;
			});
			if (conds.length) {
				const branch = addNode(g, 'branch', 0, 0);
				branch.lists.conds = conds;
				event.out.then = branch.id;
				tail = branch;
				pin = 'true';
			}
			// An old rule whose event stayed true kept being checked every moment until its conditions held and it had run: that is what "run again" and a
			// do-once after the branch say. Without conditions the event itself fires only once.
			if (conds.length && latched(event) && w.kind !== 'start') {
				event.p.again = true;
			}
			const once = r.once || latched(event);
			if (once && !(latched(event) && !conds.length)) {
				const gate = addNode(g, 'doOnce', 0, 0);
				tail.out[pin] = gate.id;
				tail = gate;
				pin = 'then';
			}
			chain(r.then, tail, pin);
			event.note = r.name; // kept on the node so the rule's name is not lost
		}
		layout(g, true);
		return g;
	}

	MS.flow = Object.assign(MS.flow || {}, {
		KINDS, ORDER, CONDS, CATEGORIES, BATTLE_MODES, OPS,
		create, addNode, find, incoming, whyNot, connect, disconnect, removeNode, clone, addCondition, condDefaults,
		chainFrom, entries, stages, firstStage, isStage, problems, layout, migrate, reaches,
	});
})();
