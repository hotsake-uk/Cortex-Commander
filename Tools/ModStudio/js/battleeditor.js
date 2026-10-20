// Cortex Mod Studio: the Battle Editor. A scenario of the Battle kind is one of the game's battle modes (capture the flag, king of the hill, assault...)
// laid out on a map by hand: one tab with the scene's map on the left, where each team's spawn zones, flags, hills, objectives, goals and the rest are
// placed and moved, and the battle on the right: the mode, the teams, a checklist of what the mode needs placed, and its rules.
//   The scenario  an ordinary copy of Battle Command (Base.rte) under its own name and scene: the game plays it as Battle Command, setting up and
//                 starting the battle of the same name when it starts.
//   The battle    kept in <mod>/Battles/<name>.txt, in the form the game's own Battle tab saves its setups in (Userdata/BattlePresets/<map>.txt), with
//                 a "Scene = " line. The game lists a mod's battles with each map's own: in the Sandbox's Battle tab and the map browser (Sandbox
//                 and Battle Command), to load and play there too. Fields Studio doesn't show are kept as they are.
//   The map       the scene, as in the Mission Editor (wheel zooms, right or middle drag moves, F fits). Place: pick what from the checklist (or
//                 right-click the map), then click for a point, or drag a box for a zone (with Polygon picked, click its corners and double-click or
//                 Enter to close it). Drag a point or zone to move it, a corner to reshape it; Alt-click an edge adds a corner, Shift-click one takes it
//                 out; Delete takes out what's picked.
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

	const SIDES = 4;
	const SIDE_NAMES = ['Red', 'Green', 'Blue', 'Yellow'];
	const SIDE_COLOURS = ['#e5484d', '#46a758', '#3e8ed0', '#f5c518'];
	const NEUTRAL = '#e8e8e8';
	const BATTLE_CLASS = 'BattleCommandActivity';

	// ---- the modes, as SandboxBattleModes.cpp has them (c_Modes, by BattleMode) ----
	// point: each team's own point (Mode.Points.N) and what it is; zones: the mode's own zones (Mode.Zones), what each is, how many it needs and whether
	// their order counts; goals: each team's goal zone (Mode.Goals.N); spots: positions placed (Mode.FlagSpots), what each is and how many at most;
	// role: the one side the mode singles out; rules: [key, label, min, max, default, note] numbers, or [key, label, 'bool', default, note].
	const TIME_LIMIT = ['TimeLimit', 'Time limit (seconds, 0: none)', 0, 3600, 300];
	const MODES = [
		null,
		{ name: 'Capture the flag', icon: '⚑', blurb: 'Each team has a flag and spawn zones. Bring the enemy\'s flag home to your own to score.',
			point: { label: 'flag', note: 'Where its flag stands. Without one, the game picks a place in its base.' },
			rules: [['ScoreToWin', 'Captures to win (0: play on)', 0, 50, 3], ['ReturnSeconds', 'A dropped flag goes home after (seconds)', 1, 300, 30], ['GuardPercent', 'Units guarding their flag (%)', 0, 100, 30],
				['EscortPercent', 'Team-mates going all the way with a carrier (%)', 0, 100, 50], ['MoveStuckPoint', 'A flag nobody can reach moves', 'bool', true]] },
		{ name: 'King of the hill', icon: '⛰', blurb: 'Every unit fights for the hill. A team alone on it scores; first to the time set wins.',
			zones: { label: 'Hill', min: 1, ordered: true, note: 'With more than one, they come into play in this order.' },
			rules: [['HoldToWin', 'Seconds held to win', 5, 3600, 120], ['HillMoveSeconds', 'The hill moves on after (seconds, 0: stays)', 0, 3600, 0], ['HillsToWin', 'Hills to take to win (0: seconds held win)', 0, 20, 0],
				['MajorityScores', 'A shared hill scores for the team with most there', 'bool', false]] },
		{ name: 'Assault', icon: '➤', blurb: 'One team attacks a line of objectives in order; the rest defend them. Each one taken buys the attackers time.',
			zones: { label: 'Objective', min: 1, ordered: true, note: 'Taken in this order: drag them in the list to change it.' },
			role: { key: 'Attacker', label: 'Attackers' },
			rules: [['CaptureSeconds', 'Seconds to take an objective', 1, 300, 15], TIME_LIMIT, ['BonusSeconds', 'Seconds added for each one taken', 0, 600, 60],
				['BlockLastRespawns', 'No defender respawns at the last objective under attack', 'bool', false], ['CommanderFallBack', 'Defenders fall back at (% taken)', 0, 100, 60]] },
		{ name: 'Last team standing', icon: '☠', blurb: 'Every team has so many units in all and hunts the nearest enemy. The last team in wins.',
			rules: [['Tickets', 'Units each team gets in all', 1, 1000, 60]] },
		{ name: 'VIP hunt', icon: '♛', blurb: 'One unit of each team is its VIP. Bring down the enemy VIPs to score.',
			point: { label: 'VIP spot', note: 'Where its VIP keeps to. Without one, its widest spawn zone.' },
			rules: [['KillsToWin', 'VIPs to bring down to win', 1, 100, 5], ['VipRespawnSeconds', 'A new VIP after (seconds)', 1, 300, 20], ['GuardPercent', 'Bodyguards (%)', 0, 100, 30]] },
		{ name: 'One flag', icon: '⚐', blurb: 'One neutral flag, and every team after it. Bring it into your own goal zone to score.',
			spots: { label: 'Flag position', max: 8, note: 'The flag comes in at each in turn, the next after every score.' },
			zones: { label: 'Flag spawn zone', min: 0, note: 'With "Flag comes in a zone" ticked: somewhere in one of these, another each time.' },
			goals: { label: 'goal zone' },
			rules: [['ScoreToWin', 'Scores to win (0: play on)', 0, 50, 3], ['ReturnSeconds', 'A dropped flag goes back after (seconds)', 1, 300, 30], ['EscortPercent', 'Team-mates going all the way with a carrier (%)', 0, 100, 50],
				['FlagByZones', 'Flag comes in a zone (not at the positions)', 'bool', false]] },
		{ name: 'Capture the ship', icon: '✈', blurb: 'One ship, and every team after it. Fly it down into your landing zone to score.',
			spots: { label: 'Ship position', max: 8, note: 'Each ship comes in at the next of these.' },
			goals: { label: 'landing zone' },
			rules: [['ScoreToWin', 'Landings to win (0: play on)', 0, 50, 3], ['ReturnSeconds', 'A ship shot down is replaced after (seconds)', 1, 300, 30], ['EscortPercent', 'Team-mates following the ship (%)', 0, 100, 50]] },
		{ name: 'Wave defence', icon: '⛨', blurb: 'One team holds a defence zone against waves of attackers from the rest, each bigger than the last.',
			zones: { label: 'Defence zone', min: 1, note: 'The first is the one held.' },
			role: { key: 'Defender', label: 'Defenders' },
			rules: [['Waves', 'Waves to beat (0: endless)', 0, 200, 10], ['FirstWave', 'Attackers in the first wave', 1, 200, 6], ['WaveGrowth', 'More each wave', 0, 100, 3],
				['WaveBreak', 'Seconds between waves', 0, 600, 20], ['CaptureSeconds', 'Seconds attackers hold the zone to win', 1, 300, 15]] },
		{ name: 'Escort', icon: '⛉', blurb: 'One team takes its VIP to a destination; every other team hunts it.',
			zones: { label: 'Destination', min: 1, note: 'The first is where the VIP goes.' },
			role: { key: 'EscortSide', label: 'Escort' },
			rules: [TIME_LIMIT] },
		{ name: 'Extraction', icon: '⇪', blurb: 'One team gets its units out through an extraction zone; the rest hold it against them.',
			zones: { label: 'Extraction zone', min: 1, note: 'The first is the way out.' },
			role: { key: 'ExtractSide', label: 'Getting out' },
			rules: [['Tickets', 'Units it has in all', 1, 1000, 60], ['ExtractToWin', 'Units out to win', 1, 500, 10], ['ExtractSeconds', 'Seconds in the zone to be lifted out', 0, 60, 3], TIME_LIMIT] },
		{ name: 'The point', icon: '◎', blurb: 'One point with rings round it, each worth its own points a second to the team alone in it.',
			spots: { label: 'The point', max: 1, note: 'The bullseye. Its rings are drawn round it.' },
			rules: [['PointsToWin', 'Points to win (0: play on)', 0, 100000, 300], ['PointRings', 'Rings round the bullseye', 0, 4, 2], ['MajorityScores', 'A shared band scores for the team with most there', 'bool', false]] },
	];
	// Every mode's own: what each team's units do between the mode's rules.
	const SHARED_RULES = [['RespawnSeconds', 'Seconds before a fallen unit is replaced', 0, 300, 5], ['MaxRespawns', 'Units each team gets back in all (0: no limit)', 0, 10000, 0],
		['StuckSeconds', 'Respawn a unit stuck for (seconds, 0: never)', 0, 600, 20], ['RouteVariety', 'Units taking routes of their own (%)', 0, 100, 0],
		['ByShip', 'Units come in by ship', 'bool', false], ['Anywhere', 'Units come in anywhere on the map', 'bool', false], ['KeepClear', 'Coming in anywhere: keep clear of enemies by (pixels)', 0, 2000, 300],
		['MovingSpawns', 'You can move your spawn zones during the battle', 'bool', false], ['FogOfWar', 'Fog of war (Battle Command)', 'bool', false],
		['CommanderSquads', 'AI commanders send squads', 'bool', true], ['CommanderReserve', 'AI commanders hold back (%)', 0, 100, 30]];
	const PICKUP_RULES = [['PickupEvery', 'Seconds between pickups', 5, 600, 45], ['PickupMost', 'Most on the map at once', 1, 20, 3], ['PickupLasts', 'A pickup lies for (seconds, 0: till taken)', 0, 600, 90]];
	const RING_DEFAULTS = { radius: [60, 130, 200, 270, 340], points: [5, 3, 1, 1, 1] };
	const LEVELS = [[0, 'Easy'], [1, 'Normal'], [2, 'Hard']];

	// ---- the file: "[Name]" sections of "Key = value" lines, as SandboxBattlePresets.cpp reads and writes them ----

	function parse(text) {
		const out = { heading: [], sections: [] };
		let at = null;
		for (const raw of String(text || '').split(/\r?\n/)) {
			const line = raw.trim();
			if (line.length >= 2 && line[0] === '[' && line[line.length - 1] === ']') {
				at = { name: line.slice(1, -1), lines: new Map() };
				out.sections.push(at);
			} else if (!at) {
				if (line) {
					out.heading.push(raw);
				}
			} else {
				const eq = line.indexOf('=');
				if (eq > 0) {
					at.lines.set(line.slice(0, eq).trim(), line.slice(eq + 1).trim());
				}
			}
		}
		return out;
	}

	function serialise(file) {
		const parts = [(file.heading.length ? file.heading : ['// A battle made in Cortex Studio\'s Battle Editor. The game lists it with its map\'s battles.']).join('\n')];
		for (const sec of file.sections) {
			parts.push('', '[' + sec.name + ']');
			for (const [k, v] of sec.lines) {
				parts.push(k + ' = ' + v);
			}
		}
		return parts.join('\n') + '\n';
	}

	const parsePoint = (t) => {
		const m = /^(-?[\d.]+),(-?[\d.]+)$/.exec(String(t || '').trim());
		return m ? { x: Number(m[1]), y: Number(m[2]) } : null;
	};
	const parsePoints = (t) => String(t || '').split(/\s+/).map(parsePoint).filter(Boolean);
	const parseZones = (t) => String(t || '').split('|').map(parsePoints).filter((z) => z.length >= 3);
	const fmtPoint = (p) => Math.round(p.x) + ',' + Math.round(p.y);
	const fmtPoints = (ps) => ps.map(fmtPoint).join(' ');
	const fmtZones = (zs) => zs.filter((z) => z.length >= 3).map(fmtPoints).join(' | ');

	// What a key holds: a list of zones, one zone (a goal), a list of positions, or one point (a team's own, with its HasPoint).
	function typeOf(key) {
		if (/^Mode\.(SpawnZones\.\d|Zones|PickupZones)$/.test(key)) {
			return 'zones';
		}
		if (/^Mode\.Goals\.\d$/.test(key)) {
			return 'poly';
		}
		if (/^Mode\.(FlagSpots|PickupSpots)$/.test(key)) {
			return 'points';
		}
		return /^Mode\.Points\.\d$/.test(key) ? 'point' : null;
	}

	// The setup of one battle: its section's lines, read and changed by key, with the struct's defaults (BattleModeSettings) for keys not written.
	class Setup {
		constructor(sec) {
			this.sec = sec;
		}

		raw(key) {
			return this.sec.lines.get(key);
		}

		num(key, def) {
			const v = this.raw(key);
			return v === undefined || v === '' || isNaN(Number(v)) ? def : Number(v);
		}

		bool(key, def) {
			const v = this.raw(key);
			return v === undefined ? def : v !== '0';
		}

		set(key, value) {
			this.sec.lines.set(key, typeof value === 'boolean' ? (value ? '1' : '0') : String(value));
		}

		get mode() {
			return Math.max(1, Math.min(MODES.length - 1, this.num('Mode.Mode', 1)));
		}

		plays(side) {
			return this.bool('Mode.Plays.' + side, side < 2);
		}

		shapes(key) {
			const t = typeOf(key);
			const v = this.raw(key);
			if (t === 'zones') {
				return parseZones(v);
			}
			if (t === 'poly') {
				const pts = parsePoints(v);
				return pts.length >= 3 ? [pts] : [];
			}
			if (t === 'points') {
				return parsePoints(v).map((p) => [p]);
			}
			if (t === 'point') {
				const side = key.split('.')[2];
				const p = parsePoint(v);
				return this.bool('Mode.HasPoint.' + side, false) && p ? [[p]] : [];
			}
			return [];
		}

		setShapes(key, shapes) {
			const t = typeOf(key);
			if (t === 'zones') {
				this.set(key, fmtZones(shapes));
			} else if (t === 'poly') {
				this.set(key, shapes.length ? fmtPoints(shapes[0]) : '');
			} else if (t === 'points') {
				this.set(key, fmtPoints(shapes.map((s) => s[0])));
			} else if (t === 'point') {
				const side = key.split('.')[2];
				this.set('Mode.HasPoint.' + side, shapes.length > 0);
				this.set(key, shapes.length ? fmtPoint(shapes[0][0]) : '0,0');
			}
		}

		// The side a mode singles out (Attacker...), kept to one that plays as the game does (AttackerOf and the like clamp it).
		role(spec) {
			return Math.max(0, Math.min(SIDES - 1, this.num('Mode.' + spec.key, 0)));
		}
	}

	// A new battle's lines: the mode, Red and Green playing, you commanding Red, and Scene for the map.
	function freshSection(name, scene, mode) {
		const lines = new Map([['Scene', scene || ''], ['WithMap', '0'], ['Linked', '0'], ['Mode.Mode', String(mode || 1)]]);
		for (let side = 0; side < SIDES; side++) {
			lines.set('Mode.Plays.' + side, side < 2 ? '1' : '0');
			lines.set('Mode.PlayerCommands.' + side, side === 0 ? '1' : '0');
		}
		return { name, lines };
	}

	// ---- the scenario and its battle file ----

	const keyOf = (p) => p.doc.path + '::' + p.name;
	const scenarioByKey = (key) => app().lib.presets.find((p) => p.topLevel && p.node.key === 'AddActivity' && keyOf(p) === key) || null;
	const stemOf = (name) => name.replace(/[^A-Za-z0-9 _-]/g, '').trim().replace(/ +/g, '_') || 'Battle';
	const battlePath = (scenario) => scenario.module + '/Battles/' + stemOf(scenario.name) + '.txt';
	const locked = (scenario) => !scenario || app().lib.isProtected(scenario.doc);

	// A Battle: a copy of Battle Command (running its script) under a name of its own.
	function isBattle(scenario) {
		if (!scenario || scenario.node.key !== 'AddActivity' || scenario.name === 'Battle Command') {
			return false;
		}
		return app().lib.resolve(scenario.node).str('LuaClassName', '') === BATTLE_CLASS;
	}

	const sceneOf = (scenario) => (MS.missionEditor ? MS.missionEditor.sceneOf(scenario) : null);

	// key -> { file, setup, undo, redo, loading, saveTimer, path }
	const docs = new Map();

	function docFor(scenario, onRead) {
		const key = keyOf(scenario);
		let doc = docs.get(key);
		if (doc) {
			return doc;
		}
		doc = { file: null, setup: null, undo: [], redo: [], loading: true, path: battlePath(scenario) };
		docs.set(key, doc);
		const a = app();
		(async () => {
			let text = null;
			try {
				if (await a.fs.exists(doc.path)) {
					text = await a.fs.readText(doc.path);
				}
			} catch (err) {
				text = null;
			}
			doc.file = parse(text);
			let sec = doc.file.sections.find((x) => x.name === scenario.name) || doc.file.sections[0];
			if (!sec) {
				const scene = sceneOf(scenario);
				sec = freshSection(scenario.name, scene ? scene.name : '', 1);
				doc.file.sections.push(sec);
			}
			sec.name = scenario.name; // the game finds it by the scenario's name
			doc.setup = new Setup(sec);
			doc.loading = false;
			if (onRead) {
				onRead();
			}
		})();
		return doc;
	}

	// Writes the battle a moment after the last change (one write for a drag's worth of changes).
	function saveSoon(scenario, doc) {
		clearTimeout(doc.saveTimer);
		doc.saveTimer = setTimeout(async () => {
			const scene = sceneOf(scenario);
			doc.setup.set('Scene', scene ? scene.name : '');
			try {
				await app().fs.writeFile(doc.path, serialise(doc.file));
				app().status('Saved ' + doc.path + '.');
			} catch (err) {
				app().toast('Could not save ' + doc.path + ': ' + err.message, 'bad');
			}
		}, 250);
	}

	// One change to the battle: undoable (the tab's Ctrl+Z), saved, and drawn again.
	function change(key, label, fn) {
		const scenario = scenarioByKey(key);
		const doc = scenario && docs.get(key);
		if (!doc || !doc.setup) {
			return;
		}
		if (locked(scenario)) {
			app().toast(scenario.module + ' is not yours to change. Make a battle of your own with + New battle.', 'bad');
			return;
		}
		const before = new Map(doc.setup.sec.lines);
		fn(doc.setup);
		doc.undo.push({ label, lines: before });
		if (doc.undo.length > 200) {
			doc.undo.shift();
		}
		doc.redo = [];
		saveSoon(scenario, doc);
		redraw(key);
	}

	function historyStep(key, redo) {
		const scenario = scenarioByKey(key);
		const doc = scenario && docs.get(key);
		const from = doc && (redo ? doc.redo : doc.undo);
		if (!from || !from.length) {
			return false;
		}
		const step = from.pop();
		(redo ? doc.undo : doc.redo).push({ label: step.label, lines: new Map(doc.setup.sec.lines) });
		doc.setup.sec.lines = step.lines;
		saveSoon(scenario, doc);
		app().status((redo ? 'Redid: ' : 'Undid: ') + step.label);
		redraw(key);
		return true;
	}

	// The tabs showing each battle, to draw again after a change.
	const views = new Map(); // key -> Set(redraw)
	function redraw(key) {
		for (const fn of views.get(key) || []) {
			fn();
		}
	}

	// ---- what the mode puts on the map, and what it still needs ----

	// The checklist: each thing the mode has placed, its key, what it is, whose (side or -1), whether it's needed and whether it's done.
	function needs(setup) {
		const spec = MODES[setup.mode];
		const out = [];
		const playing = [...Array(SIDES).keys()].filter((side) => setup.plays(side));
		for (const side of playing) {
			out.push({ key: 'Mode.SpawnZones.' + side, label: SIDE_NAMES[side] + ' spawn zone', side, required: true, many: true, note: 'Where ' + SIDE_NAMES[side] + '\'s units come in. Draw as many as you like.' });
			if (spec.point) {
				out.push({ key: 'Mode.Points.' + side, label: SIDE_NAMES[side] + ' ' + spec.point.label, side, required: false, many: false, note: spec.point.note });
			}
			if (spec.goals) {
				out.push({ key: 'Mode.Goals.' + side, label: SIDE_NAMES[side] + ' ' + spec.goals.label, side, required: true, many: false, note: 'Where ' + SIDE_NAMES[side] + ' brings it to score.' });
			}
		}
		const byZones = setup.mode === 6 && setup.bool('Mode.FlagByZones', false);
		if (spec.spots && !byZones) {
			out.push({ key: 'Mode.FlagSpots', label: spec.spots.label, side: -1, required: true, many: spec.spots.max > 1, max: spec.spots.max, note: spec.spots.note });
		}
		if (spec.zones && (setup.mode !== 6 || byZones)) {
			out.push({ key: 'Mode.Zones', label: spec.zones.label, side: -1, required: (setup.mode === 6 ? 1 : spec.zones.min) > 0, many: true, ordered: spec.zones.ordered, note: spec.zones.note });
		}
		if (setup.bool('Mode.Pickups', false)) {
			const places = setup.num('Mode.PickupPlaces', 1);
			if (places & 2) {
				out.push({ key: 'Mode.PickupSpots', label: 'Pickup spot', side: -1, required: false, many: true, max: 8, note: 'Places pickups come in at.' });
			}
			if (places & 4) {
				out.push({ key: 'Mode.PickupZones', label: 'Pickup zone', side: -1, required: false, many: true, max: 8, note: 'Zones pickups come in somewhere in.' });
			}
		}
		for (const n of out) {
			n.count = setup.shapes(n.key).length;
			n.done = n.count > 0;
			n.type = typeOf(n.key);
			n.colour = n.side >= 0 ? SIDE_COLOURS[n.side] : n.key.startsWith('Mode.Pickup') ? '#c77dff' : NEUTRAL;
		}
		return out;
	}

	// Why the game won't start it yet (as ModeReady says), or '' when it will.
	function problem(setup) {
		const spec = MODES[setup.mode];
		const playing = [...Array(SIDES).keys()].filter((side) => setup.plays(side));
		if (playing.length < 2) {
			return 'Tick at least 2 teams.';
		}
		if (spec.role && !setup.plays(setup.role(spec))) {
			return SIDE_NAMES[setup.role(spec)] + ' has to play: they are the ' + spec.role.label.toLowerCase() + '.';
		}
		const missing = needs(setup).find((n) => n.required && !n.done);
		if (missing) {
			return (missing.type === 'point' || missing.type === 'points' ? 'Place ' : 'Draw ') + (missing.side >= 0 ? missing.label : 'a ' + missing.label.toLowerCase()) + ' on the map.';
		}
		return '';
	}

	// What's on the map: one thing for each shape of each key the checklist lists.
	function things(setup) {
		const out = [];
		const spec = MODES[setup.mode];
		for (const n of needs(setup)) {
			setup.shapes(n.key).forEach((pts, i) => {
				let label = n.side >= 0 ? (n.type === 'zones' ? SIDE_NAMES[n.side] + ' spawn' : n.label) : n.label;
				if (n.many && (n.ordered || n.count > 1)) {
					label += ' ' + (i + 1);
				}
				out.push({ key: n.key, i, type: n.type, pts, colour: n.colour, label, side: n.side, ring: n.key === 'Mode.FlagSpots' && setup.mode === 11 && i === 0 });
			});
		}
		if (spec && setup.mode === 11) {
			out.rings = rings(setup);
		}
		return out;
	}

	function rings(setup) {
		const count = Math.max(0, Math.min(4, setup.num('Mode.PointRings', 2)));
		return [...Array(count + 1).keys()].map((i) => setup.num('Mode.RingRadius.' + i, RING_DEFAULTS.radius[i]));
	}

	// ---- the ground, for points placed on it ----

	const grounds = new Map(); // terrain file -> { w, h, data } (the material map's pixels), or null while read
	function groundOf(file) {
		if (!file) {
			return null;
		}
		if (grounds.has(file)) {
			return grounds.get(file);
		}
		const frame = app().sprite(file, 1, 0);
		if (!frame || !frame.canvas) {
			return null;
		}
		try {
			const ctx = frame.canvas.getContext('2d', { willReadFrequently: true });
			const g = { w: frame.width, h: frame.height, data: ctx.getImageData(0, 0, frame.width, frame.height).data };
			grounds.set(file, g);
			return g;
		} catch (err) {
			grounds.set(file, null);
			return null;
		}
	}

	// Air in the material map: see-through, black (the game's air) or the key colour.
	function solid(g, x, y) {
		if (x < 0 || x >= g.w || y < 0) {
			return false;
		}
		if (y >= g.h) {
			return true;
		}
		const i = (Math.floor(y) * g.w + Math.floor(x)) * 4;
		const r = g.data[i];
		const gr = g.data[i + 1];
		const b = g.data[i + 2];
		return g.data[i + 3] >= 128 && !(r === 0 && gr === 0 && b === 0) && !(r === 255 && gr === 0 && b === 255);
	}

	// The point on the ground under (or, in the ground, over) a place: where a flag stands.
	function onGround(g, p) {
		if (!g) {
			return p;
		}
		let y = Math.round(p.y);
		if (solid(g, p.x, y)) {
			while (y > 0 && solid(g, p.x, y - 1)) {
				y--;
			}
			return { x: p.x, y: y - 1 };
		}
		while (y < g.h && !solid(g, p.x, y + 1)) {
			y++;
		}
		return { x: p.x, y };
	}

	// ---- the map ----

	const insideZone = (pts, p) => {
		let inside = false;
		for (let i = 0, j = pts.length - 1; i < pts.length; j = i++) {
			const a = pts[i];
			const b = pts[j];
			if ((a.y > p.y) !== (b.y > p.y) && p.x < ((b.x - a.x) * (p.y - a.y)) / (b.y - a.y) + a.x) {
				inside = !inside;
			}
		}
		return inside;
	};
	const boxZone = (a, b) => [{ x: Math.min(a.x, b.x), y: Math.min(a.y, b.y) }, { x: Math.max(a.x, b.x), y: Math.min(a.y, b.y) }, { x: Math.max(a.x, b.x), y: Math.max(a.y, b.y) }, { x: Math.min(a.x, b.x), y: Math.max(a.y, b.y) }];
	const segDist = (p, a, b) => {
		const dx = b.x - a.x;
		const dy = b.y - a.y;
		const t = Math.max(0, Math.min(1, ((p.x - a.x) * dx + (p.y - a.y) * dy) / (dx * dx + dy * dy || 1)));
		return Math.hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
	};

	class BattleMap {
		// state: kept on the tab (zoom, pan, picked thing, placing); hooks: { things(), terrain(), place(key, pts), move(key, i, pts), remove(key, i),
		// placing(), donePlacing(), picked(), menu(thing|null, world, clientX, clientY), ground(p) }
		constructor(state, hooks) {
			this.state = state;
			this.hooks = hooks;
			this.canvas = el('canvas', { className: 'me-canvas', tabIndex: 0 });
			this.root = el('div', { className: 'me-map' }, this.canvas);
			this.drag = null;
			this.draft = null; // a polygon being drawn: its corners so far
			this.mouse = null;
			this.space = false;
			this.bind();
			new ResizeObserver(() => (this.fitted ? this.draw() : this.fit())).observe(this.root);
		}

		get zoom() {
			return this.state.zoom || 0.25;
		}

		toScreen(x, y) {
			return { x: (x - (this.state.panX || 0)) * this.zoom, y: (y - (this.state.panY || 0)) * this.zoom };
		}

		toWorld(sx, sy) {
			return { x: sx / this.zoom + (this.state.panX || 0), y: sy / this.zoom + (this.state.panY || 0) };
		}

		world() {
			const frame = this.hooks.terrain();
			return frame ? { w: frame.width, h: frame.height } : { w: 4000, h: 1500 };
		}

		fit() {
			const r = this.root.getBoundingClientRect();
			const w = this.world();
			if (!r.width || !r.height) {
				return;
			}
			this.fitted = true;
			this.state.zoom = Math.min(r.width / w.w, r.height / w.h) * 0.95;
			this.state.panX = -(r.width / this.state.zoom - w.w) / 2;
			this.state.panY = -(r.height / this.state.zoom - w.h) / 2;
			this.draw();
		}

		// What's shown now: the battle's things, with a drag under way where it is.
		shown() {
			const list = this.hooks.things();
			const d = this.drag;
			if (d && d.preview && (d.kind === 'move' || d.kind === 'corner')) {
				const t = list.find((x) => x.key === d.key && x.i === d.i);
				if (t) {
					t.pts = d.preview;
				}
			}
			return list;
		}

		picked() {
			const sel = this.state.sel;
			return sel ? this.shown().find((t) => t.key === sel.key && t.i === sel.i) || null : null;
		}

		// The thing under a point on the screen: a point's marker first, then the zone it is in (the smallest, so one inside another can be picked).
		hit(sx, sy) {
			const p = this.toWorld(sx, sy);
			const list = this.shown();
			const point = list.slice().reverse().find((t) => t.pts.length === 1 && Math.abs(this.toScreen(t.pts[0].x, 0).x - sx) < 10 && sy <= this.toScreen(0, t.pts[0].y).y + 4 && sy >= this.toScreen(0, t.pts[0].y).y - 30);
			if (point) {
				return point;
			}
			const area = (pts) => Math.abs(pts.reduce((s, a, i) => s + a.x * pts[(i + 1) % pts.length].y - pts[(i + 1) % pts.length].x * a.y, 0));
			return list.filter((t) => t.pts.length >= 3 && insideZone(t.pts, p)).sort((a, b) => area(a.pts) - area(b.pts))[0] || null;
		}

		cornerAt(t, sx, sy) {
			if (!t || t.pts.length < 3) {
				return -1;
			}
			return t.pts.findIndex((c) => {
				const s = this.toScreen(c.x, c.y);
				return Math.abs(s.x - sx) < 7 && Math.abs(s.y - sy) < 7;
			});
		}

		edgeAt(t, sx, sy) {
			if (!t || t.pts.length < 3) {
				return -1;
			}
			const p = this.toWorld(sx, sy);
			let best = -1;
			let bestDist = 8 / this.zoom;
			t.pts.forEach((a, i) => {
				const dist = segDist(p, a, t.pts[(i + 1) % t.pts.length]);
				if (dist < bestDist) {
					best = i;
					bestDist = dist;
				}
			});
			return best;
		}

		drawFlag(ctx, at, colour, label, picked) {
			const s = this.toScreen(at.x, at.y);
			ctx.strokeStyle = picked ? '#fff' : '#111';
			ctx.lineWidth = 3;
			ctx.beginPath();
			ctx.moveTo(s.x, s.y);
			ctx.lineTo(s.x, s.y - 26);
			ctx.stroke();
			ctx.strokeStyle = colour;
			ctx.lineWidth = 1.5;
			ctx.stroke();
			ctx.fillStyle = colour;
			ctx.beginPath();
			ctx.moveTo(s.x, s.y - 26);
			ctx.lineTo(s.x + 15, s.y - 21);
			ctx.lineTo(s.x, s.y - 16);
			ctx.closePath();
			ctx.fill();
			ctx.strokeStyle = picked ? '#fff' : 'rgba(0,0,0,.7)';
			ctx.lineWidth = picked ? 2 : 1;
			ctx.stroke();
			ctx.beginPath();
			ctx.arc(s.x, s.y, 3, 0, Math.PI * 2);
			ctx.fill();
			this.tag(ctx, s.x + 4, s.y - 30, label, colour);
		}

		tag(ctx, x, y, text, colour) {
			const tw = ctx.measureText(text).width;
			ctx.fillStyle = 'rgba(0,0,0,.7)';
			ctx.fillRect(x, y - 13, tw + 10, 17);
			ctx.fillStyle = colour;
			ctx.fillText(text, x + 5, y);
		}

		path(ctx, pts) {
			ctx.beginPath();
			pts.forEach((c, i) => {
				const s = this.toScreen(c.x, c.y);
				if (i) {
					ctx.lineTo(s.x, s.y);
				} else {
					ctx.moveTo(s.x, s.y);
				}
			});
			ctx.closePath();
		}

		draw() {
			const c = this.canvas;
			const r = this.root.getBoundingClientRect();
			const dpr = window.devicePixelRatio || 1;
			if (c.width !== Math.round(r.width * dpr) || c.height !== Math.round(r.height * dpr)) {
				c.width = Math.round(r.width * dpr);
				c.height = Math.round(r.height * dpr);
				c.style.width = r.width + 'px';
				c.style.height = r.height + 'px';
			}
			const ctx = c.getContext('2d');
			ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
			ctx.fillStyle = '#0b0e14';
			ctx.fillRect(0, 0, r.width, r.height);
			const frame = this.hooks.terrain();
			const w = this.world();
			const o = this.toScreen(0, 0);
			ctx.fillStyle = '#141a24';
			ctx.fillRect(o.x, o.y, w.w * this.zoom, w.h * this.zoom);
			if (frame) {
				ctx.imageSmoothingEnabled = this.zoom < 1;
				ctx.drawImage(frame.canvas, o.x, o.y, w.w * this.zoom, w.h * this.zoom);
			}
			ctx.font = '12px system-ui, sans-serif';
			const list = this.shown();
			const sel = this.picked();
			// zones under points: team spawns, goals (dashed), the mode's own
			for (const t of list.filter((x) => x.pts.length >= 3)) {
				const picked = sel === t;
				this.path(ctx, t.pts);
				ctx.fillStyle = t.colour + (picked ? '55' : t.type === 'poly' ? '1c' : '33');
				ctx.fill();
				ctx.strokeStyle = picked ? '#fff' : t.colour;
				ctx.lineWidth = picked ? 2.5 : 1.5;
				ctx.setLineDash(t.type === 'poly' ? [7, 4] : []);
				ctx.stroke();
				ctx.setLineDash([]);
				const top = t.pts.reduce((a, b) => (b.y < a.y || (b.y === a.y && b.x < a.x) ? b : a));
				const s = this.toScreen(top.x, top.y);
				this.tag(ctx, s.x, s.y - 4, t.label, t.colour);
			}
			// the point's rings
			if (list.rings) {
				const bull = list.find((t) => t.ring);
				if (bull) {
					const s = this.toScreen(bull.pts[0].x, bull.pts[0].y);
					list.rings.forEach((radius, i) => {
						ctx.beginPath();
						ctx.arc(s.x, s.y, radius * this.zoom, 0, Math.PI * 2);
						ctx.strokeStyle = i ? 'rgba(255,255,255,.55)' : '#f2b63d';
						ctx.lineWidth = i ? 1.2 : 2;
						ctx.setLineDash(i ? [5, 4] : []);
						ctx.stroke();
						ctx.setLineDash([]);
					});
				}
			}
			for (const t of list.filter((x) => x.pts.length === 1)) {
				this.drawFlag(ctx, t.pts[0], t.colour, t.label, sel === t);
			}
			if (sel && sel.pts.length >= 3) {
				ctx.fillStyle = '#fff';
				for (const cn of sel.pts) {
					const s = this.toScreen(cn.x, cn.y);
					ctx.fillRect(s.x - 4, s.y - 4, 8, 8);
				}
			}
			// what's being placed
			const placing = this.hooks.placing();
			const d = this.drag;
			if (d && d.kind === 'box' && d.now) {
				const pts = boxZone(d.start, d.now);
				this.path(ctx, pts);
				ctx.fillStyle = (placing ? placing.colour : '#fff') + '44';
				ctx.fill();
				ctx.strokeStyle = '#fff';
				ctx.lineWidth = 1.5;
				ctx.stroke();
			}
			if (this.draft && this.draft.length) {
				const pts = this.mouse ? this.draft.concat([this.mouse]) : this.draft;
				ctx.beginPath();
				pts.forEach((cn, i) => {
					const s = this.toScreen(cn.x, cn.y);
					if (i) {
						ctx.lineTo(s.x, s.y);
					} else {
						ctx.moveTo(s.x, s.y);
					}
				});
				ctx.strokeStyle = '#fff';
				ctx.lineWidth = 1.5;
				ctx.setLineDash([4, 3]);
				ctx.stroke();
				ctx.setLineDash([]);
				ctx.fillStyle = '#fff';
				for (const cn of this.draft) {
					const s = this.toScreen(cn.x, cn.y);
					ctx.fillRect(s.x - 3, s.y - 3, 6, 6);
				}
			}
			if (placing) {
				const text = 'Placing: ' + placing.label + (placing.type === 'point' || placing.type === 'points' ? ' (click on the map)' : this.state.shape === 'poly' ? ' (click its corners; double-click or Enter closes it)' : ' (drag a box)') + '. Esc stops.';
				ctx.fillStyle = 'rgba(0,0,0,.75)';
				ctx.fillRect(8, r.height - 28, ctx.measureText(text).width + 16, 20);
				ctx.fillStyle = placing.colour;
				ctx.fillText(text, 16, r.height - 14);
			}
		}

		finishDraft() {
			const placing = this.hooks.placing();
			if (placing && this.draft && this.draft.length >= 3) {
				this.hooks.place(placing.key, this.draft.map((p) => ({ x: Math.round(p.x), y: Math.round(p.y) })));
			}
			this.draft = null;
			this.draw();
		}

		bind() {
			const c = this.canvas;
			c.addEventListener('wheel', (e) => {
				e.preventDefault();
				const before = this.toWorld(e.offsetX, e.offsetY);
				this.state.zoom = Math.max(0.02, Math.min(8, this.zoom * (e.deltaY < 0 ? 1.15 : 1 / 1.15)));
				this.state.panX = before.x - e.offsetX / this.zoom;
				this.state.panY = before.y - e.offsetY / this.zoom;
				this.draw();
			}, { passive: false });
			c.addEventListener('contextmenu', (e) => e.preventDefault());
			c.addEventListener('keydown', (e) => {
				if (e.key === ' ') {
					this.space = true;
					e.preventDefault();
				} else if (e.key === 'Escape') {
					this.draft = null;
					this.hooks.donePlacing();
					this.draw();
				} else if (e.key === 'Enter' && this.draft) {
					this.finishDraft();
				} else if ((e.key === 'Delete' || e.key === 'Backspace') && this.state.sel) {
					this.hooks.remove(this.state.sel.key, this.state.sel.i);
					this.state.sel = null;
					e.preventDefault();
				} else if (e.key === 'f' || e.key === 'F') {
					this.fit();
				}
			});
			c.addEventListener('keyup', (e) => {
				if (e.key === ' ') {
					this.space = false;
				}
			});
			c.addEventListener('dblclick', () => this.draft && this.finishDraft());
			c.addEventListener('pointerdown', (e) => {
				c.focus();
				c.setPointerCapture(e.pointerId);
				const at = { x: e.offsetX, y: e.offsetY };
				const world = this.toWorld(at.x, at.y);
				if (e.button === 1 || e.button === 2 || (e.button === 0 && this.space)) {
					this.drag = { kind: 'pan', start: at, panX: this.state.panX || 0, panY: this.state.panY || 0, button: e.button, client: { x: e.clientX, y: e.clientY } };
					return;
				}
				const placing = this.hooks.placing();
				if (placing) {
					if (placing.type === 'point' || placing.type === 'points') {
						const p = this.hooks.ground(world);
						this.hooks.place(placing.key, [{ x: Math.round(p.x), y: Math.round(p.y) }]);
						return;
					}
					if (this.state.shape === 'poly') {
						this.draft = (this.draft || []).concat([world]);
						this.draw();
						return;
					}
					this.drag = { kind: 'box', start: world };
					return;
				}
				// the picked zone's corners first: dragged, taken out (Shift), or a new one on an edge (Alt)
				const sel = this.picked();
				const corner = this.cornerAt(sel, at.x, at.y);
				if (corner >= 0 && e.shiftKey) {
					if (sel.pts.length > 3) {
						this.hooks.move(sel.key, sel.i, sel.pts.filter((_, i) => i !== corner), 'take out a corner');
					}
					return;
				}
				if (corner >= 0) {
					this.drag = { kind: 'corner', key: sel.key, i: sel.i, corner, from: sel.pts.map((p) => Object.assign({}, p)) };
					return;
				}
				const edge = e.altKey ? this.edgeAt(sel, at.x, at.y) : -1;
				if (edge >= 0) {
					const pts = sel.pts.slice();
					pts.splice(edge + 1, 0, { x: Math.round(world.x), y: Math.round(world.y) });
					this.hooks.move(sel.key, sel.i, pts, 'add a corner');
					return;
				}
				const t = this.hit(at.x, at.y);
				if (t) {
					this.state.sel = { key: t.key, i: t.i };
					this.hooks.picked();
					this.drag = { kind: 'move', key: t.key, i: t.i, from: t.pts.map((p) => Object.assign({}, p)), start: world, point: t.pts.length === 1 };
					this.draw();
					return;
				}
				this.state.sel = null;
				this.hooks.picked();
				this.drag = { kind: 'pan', start: at, panX: this.state.panX || 0, panY: this.state.panY || 0, button: 0 };
				this.draw();
			});
			c.addEventListener('pointermove', (e) => {
				const d = this.drag;
				const now = this.toWorld(e.offsetX, e.offsetY);
				if (this.draft) {
					this.mouse = now;
					this.draw();
				}
				if (!d) {
					const sel = this.picked();
					c.style.cursor = this.hooks.placing() ? 'crosshair' : this.cornerAt(sel, e.offsetX, e.offsetY) >= 0 ? 'grab' : this.hit(e.offsetX, e.offsetY) ? 'move' : 'default';
					return;
				}
				if (d.kind === 'pan') {
					this.state.panX = d.panX - (e.offsetX - d.start.x) / this.zoom;
					this.state.panY = d.panY - (e.offsetY - d.start.y) / this.zoom;
					d.moved = d.moved || Math.abs(e.offsetX - d.start.x) + Math.abs(e.offsetY - d.start.y) > 3;
				} else if (d.kind === 'box') {
					d.now = now;
				} else if (d.kind === 'move') {
					const dx = Math.round(now.x - d.start.x);
					const dy = Math.round(now.y - d.start.y);
					d.moved = d.moved || Math.abs(dx) + Math.abs(dy) > 0;
					d.preview = d.from.map((p) => ({ x: p.x + dx, y: p.y + dy }));
					if (d.point && !e.altKey) {
						d.preview = [this.hooks.ground(d.preview[0])].map((p) => ({ x: Math.round(p.x), y: Math.round(p.y) }));
					}
				} else if (d.kind === 'corner') {
					d.moved = true;
					d.preview = d.from.map((p, i) => (i === d.corner ? { x: Math.round(now.x), y: Math.round(now.y) } : p));
				}
				this.draw();
			});
			c.addEventListener('pointerup', () => {
				const d = this.drag;
				this.drag = null;
				if (!d) {
					return;
				}
				if (d.kind === 'pan') {
					if (d.button === 2 && !d.moved) {
						const t = this.hit(d.start.x, d.start.y);
						if (t) {
							this.state.sel = { key: t.key, i: t.i };
							this.hooks.picked();
						}
						this.hooks.menu(t, this.toWorld(d.start.x, d.start.y), d.client.x, d.client.y);
					}
					this.draw();
					return;
				}
				if (d.kind === 'box') {
					const placing = this.hooks.placing();
					if (placing && d.now && Math.abs(d.now.x - d.start.x) * this.zoom > 4 && Math.abs(d.now.y - d.start.y) * this.zoom > 4) {
						this.hooks.place(placing.key, boxZone(d.start, d.now).map((p) => ({ x: Math.round(p.x), y: Math.round(p.y) })));
					}
					this.draw();
					return;
				}
				if ((d.kind === 'move' || d.kind === 'corner') && d.moved && d.preview) {
					this.hooks.move(d.key, d.i, d.preview, d.kind === 'move' ? 'move it' : 'move a corner');
				}
				this.draw();
			});
		}
	}

	// ---- the editor tab ----

	const menuAt = (items, x, y) => {
		const box = el('div', { className: 'ed-menu' }, ...items.map(([label, run]) => (label === '-' ? el('hr') : el('button', { className: 'ed-menu-item', onclick: () => {
			box.remove();
			run();
		} }, el('b', { textContent: label })))));
		box.style.left = x + 'px';
		box.style.top = y + 'px';
		document.body.append(box);
		const close = (ev) => {
			if (!box.contains(ev.target)) {
				box.remove();
				document.removeEventListener('pointerdown', close, true);
			}
		};
		setTimeout(() => document.addEventListener('pointerdown', close, true), 0);
	};

	// The modules with units in them, for a team's factions.
	function factions() {
		const seen = new Set();
		for (const p of app().lib.presets) {
			if (p.topLevel && ['AHuman', 'ACrab'].includes(p.cls)) {
				seen.add(p.module);
			}
		}
		return [...seen].sort();
	}

	function render(key) {
		return (host, tab) => {
			const a = app();
			if (!a.lib || !a.lib.presets.length) {
				host.replaceChildren(el('p', { className: 'ed ed-empty', textContent: 'Studio is still reading the game…' }));
				return;
			}
			const scenario = scenarioByKey(key);
			if (!scenario) {
				host.replaceChildren(el('p', { className: 'ed ed-empty', textContent: 'That battle is not there any more.' }));
				return;
			}
			const doc = docFor(scenario, () => MS.ide.refreshForm());
			if (doc.loading) {
				host.replaceChildren(el('p', { className: 'ed ed-empty', textContent: 'Reading ' + doc.path + '…' }));
				return;
			}
			const setup = doc.setup;
			const s = tab.state || (tab.state = { map: { shape: 'box', snap: true }, split: 0.62 });
			tab.undo = () => historyStep(key, false);
			tab.redo = () => historyStep(key, true);
			const ro = locked(scenario);
			const edit = (label, fn) => change(key, label, fn);
			const scene = sceneOf(scenario);
			const terrainFile = scene && MS.scenes.terrainPicture ? MS.scenes.terrainPicture(scene, a.lib.resolve(scene.node)) : null;
			let built = scene ? MS.scenes.picture(scene, (picture) => {
				built = picture;
				if (s.view) {
					s.view.draw();
				}
			}, s.map.terrain || 'textured') : null;
			const terrain = () => built || (terrainFile ? a.sprite(terrainFile, 1, 0) : null);
			if (terrainFile && !terrain() && !s.asked) {
				s.asked = true;
				a.images.get(terrainFile).then(() => setTimeout(() => s.view && s.view.fit(), 30));
			}
			// what the mode asks for now (it changes with each edit: read afresh each time)
			const listNow = () => needs(docs.get(key).setup);
			const placingOf = () => (s.map.place ? listNow().find((n) => n.key === s.map.place) || null : null);
			const startPlacing = (n) => {
				s.map.place = n.key;
				s.map.sel = null;
				if (s.view) {
					s.view.canvas.focus();
					s.view.draft = null;
				}
				drawAll();
			};
			const place = (k, pts) => {
				const n = listNow().find((x) => x.key === k);
				if (!n) {
					return;
				}
				let at = 0;
				// one of a kind, or the last of those allowed: done placing it; many: carry on until Esc
				if (!n.many || (n.max && n.count + 1 >= n.max)) {
					s.map.place = null;
				}
				edit('Place ' + n.label, (st) => {
					let shapes = n.many ? st.shapes(k) : [];
					shapes.push(pts);
					shapes = n.max ? shapes.slice(-n.max) : shapes;
					st.setShapes(k, shapes);
					at = shapes.length - 1;
				});
				s.map.sel = { key: k, i: at };
				if (s.view) {
					s.view.draw();
				}
			};
			const move = (k, i, pts, what) => edit(what.charAt(0).toUpperCase() + what.slice(1), (st) => {
				const shapes = st.shapes(k);
				if (shapes[i]) {
					shapes[i] = pts;
					st.setShapes(k, shapes);
				}
			});
			const remove = (k, i) => edit('Take out ' + ((listNow().find((x) => x.key === k) || {}).label || 'it'), (st) => st.setShapes(k, st.shapes(k).filter((_, j) => j !== i)));
			const reorder = (k, i, by) => edit('Reorder', (st) => {
				const shapes = st.shapes(k);
				const j = i + by;
				if (j >= 0 && j < shapes.length) {
					[shapes[i], shapes[j]] = [shapes[j], shapes[i]];
					st.setShapes(k, shapes);
					s.map.sel = { key: k, i: j };
				}
			});
			const ground = (p) => (s.map.snap ? onGround(groundOf(terrainFile), p) : p);
			const menu = (t, world, cx, cy) => {
				if (ro) {
					return;
				}
				if (t) {
					const n = listNow().find((x) => x.key === t.key);
					const items = [['Take out ' + t.label, () => remove(t.key, t.i)]];
					if (n && n.ordered) {
						items.push(['Earlier in the order', () => reorder(t.key, t.i, -1)], ['Later in the order', () => reorder(t.key, t.i, 1)]);
					}
					menuAt(items, cx, cy);
					return;
				}
				// what can go here: a point at the spot, a zone as a box round it
				const box = (w, h) => boxZone({ x: world.x - w / 2, y: world.y - h / 2 }, { x: world.x + w / 2, y: world.y + h / 2 }).map((p) => ({ x: Math.round(p.x), y: Math.round(p.y) }));
				const items = listNow().map((n) => [(n.type === 'point' || n.type === 'points' ? '' : '▭ ') + n.label + ' here', () => {
					const pts = n.type === 'point' || n.type === 'points' ? [ground(world)].map((p) => ({ x: Math.round(p.x), y: Math.round(p.y) })) : box(n.key.startsWith('Mode.SpawnZones') ? 200 : 160, 120);
					place(n.key, pts);
				}]);
				menuAt(items, cx, cy);
			};
			const hooks = {
				things: () => (docs.get(key) && docs.get(key).setup ? things(docs.get(key).setup) : []),
				terrain, place, move, remove, menu, ground,
				placing: () => (ro ? null : placingOf()),
				donePlacing: () => {
					s.map.place = null;
					drawAll();
				},
				picked: () => drawPanel(),
			};
			if (!s.view) {
				s.view = new BattleMap(s.map, hooks);
				setTimeout(() => s.view.fit(), 50);
			} else {
				s.view.hooks = hooks;
			}
			const view = s.view;

			// ---- the strip above: the scene, how the map looks, and play ----
			const scenes = a.lib.presets.filter((p) => p.topLevel && p.cls === 'Scene').sort((x, y) => x.name.localeCompare(y.name));
			const sceneSelect = el('select', { className: 'ed-input', disabled: ro, title: 'The map this battle is played on' }, el('option', { value: '', textContent: '(no scene)' }),
				...scenes.map((p) => el('option', { value: p.name, textContent: p.name + (p.module === 'Base.rte' ? '' : ' (' + p.module.replace(/[.]rte$/i, '') + ')'), selected: scene && p === scene })));
			sceneSelect.onchange = () => {
				a.edit('Play ' + scenario.name + ' on ' + sceneSelect.value, () => scenario.node.set('SceneName', sceneSelect.value));
				edit('Move to ' + sceneSelect.value, (st) => st.set('Scene', sceneSelect.value));
				s.asked = false;
				setTimeout(() => s.view && s.view.fit(), 60);
			};
			const terrainPick = el('select', { className: 'ed-input' }, el('option', { value: 'textured', textContent: 'Textured', selected: (s.map.terrain || 'textured') === 'textured' }), el('option', { value: 'materials', textContent: 'Materials', selected: s.map.terrain === 'materials' }));
			terrainPick.onchange = () => {
				s.map.terrain = terrainPick.value;
				MS.ide.refreshForm();
			};
			const shapePick = el('select', { className: 'ed-input', title: 'How a zone is drawn' }, el('option', { value: 'box', textContent: 'Box', selected: s.map.shape !== 'poly' }), el('option', { value: 'poly', textContent: 'Polygon', selected: s.map.shape === 'poly' }));
			shapePick.onchange = () => {
				s.map.shape = shapePick.value;
				view.draft = null;
				view.draw();
			};
			const snap = el('input', { type: 'checkbox', checked: s.map.snap !== false });
			snap.onchange = () => {
				s.map.snap = snap.checked;
			};
			const playButton = el('button', { className: 'tb primary', textContent: '▶ Play in the game', title: 'Starts it in the game running from Studio (Battle Command on its map, the battle set up and started), or in a game started now', onclick: async () => {
				if (!scene) {
					a.toast('Pick the map it\'s played on first (Scene, above).', 'bad');
					return;
				}
				clearTimeout(doc.saveTimer);
				setup.set('Scene', scene.name);
				await a.fs.writeFile(doc.path, serialise(doc.file));
				if (scenario.doc.dirty) {
					await a.save(true, [scenario.doc]);
				}
				const live = async () => !!(MS.inspector && MS.inspector.live && (await MS.inspector.live()));
				const wasLive = await live();
				MS.ide.runLua('ActivityMan:StartActivity(' + JSON.stringify(scenario.cls) + ', ' + JSON.stringify(scenario.name) + ')');
				if (wasLive) {
					a.status('Told the game to start ' + scenario.name + ' on ' + scene.name + '.');
					return;
				}
				a.status('Looking for an open game to start ' + scenario.name + ' in…');
				for (let i = 0; i < 6; i++) {
					await new Promise((r) => setTimeout(r, 500));
					if (await live()) {
						a.status('The open game is starting ' + scenario.name + ' on ' + scene.name + '.');
						return;
					}
				}
				if (MS.launcher && MS.launcher.launchScenario) {
					MS.launcher.launchScenario(scenario.name);
				}
			} });
			const head = el('div', { className: 'me-head' },
				el('h2', { textContent: scenario.name }), el('span', { className: 'be-kind', textContent: 'Battle' }),
				el('label', { className: 'ed-check' }, 'Scene', sceneSelect),
				el('label', { className: 'ed-check', title: 'Textured: the ground as the game draws it. Materials: what each pixel is made of.' }, 'Terrain', terrainPick),
				el('label', { className: 'ed-check' }, 'Zones', shapePick),
				el('label', { className: 'ed-check', title: 'Points placed (flags, positions) go onto the ground under where you click. Alt while dragging one: not.' }, snap, 'Onto the ground'),
				el('span', { className: 'spacer' }),
				el('button', { className: 'tb', textContent: 'Fit the map', onclick: () => view.fit() }),
				el('button', { className: 'tb', textContent: 'Open the file', title: doc.path, onclick: () => MS.ide.openFile(doc.path, 1) }),
				playButton);

			// ---- the panel: mode, teams, what to place, rules ----
			const panel = el('div', { className: 'me-right be-panel' });
			const number = (k, label, min, max, def, note) => {
				const input = el('input', { type: 'number', className: 'ed-input be-num', min, max, value: setup.num('Mode.' + k, def), disabled: ro });
				input.onchange = () => {
					const v = Math.max(min, Math.min(max, Math.round(Number(input.value) || 0)));
					edit(label, (st) => st.set('Mode.' + k, v));
				};
				return el('label', { className: 'be-rule', title: note || '' }, el('span', { textContent: label }), input);
			};
			const flag = (k, label, def, note) => {
				const input = el('input', { type: 'checkbox', checked: setup.bool('Mode.' + k, def), disabled: ro });
				input.onchange = () => edit(label, (st) => st.set('Mode.' + k, input.checked));
				return el('label', { className: 'be-rule be-flag', title: note || '' }, input, el('span', { textContent: label }));
			};
			const rule = (r) => (r[2] === 'bool' ? flag(r[0], r[1], r[3], r[4]) : number(r[0], r[1], r[2], r[3], r[4], r[5]));
			const section = (title, ...body) => el('section', { className: 'be-sec' }, el('h3', { textContent: title }), ...body);
			const drawPanel = () => {
				const st = docs.get(key).setup;
				const sp = MODES[st.mode];
				const items = needs(st);
				const why = problem(st);
				// the mode
				const modes = el('div', { className: 'be-modes' }, ...MODES.map((m, i) => (m ? el('button', { className: 'be-mode' + (i === st.mode ? ' sel' : ''), title: m.blurb, disabled: ro, onclick: () => {
					s.map.place = null;
					edit('Make it ' + m.name, (x) => x.set('Mode.Mode', i));
				} }, el('span', { className: 'be-mode-icon', textContent: m.icon }), el('span', { textContent: m.name })) : null)));
				// the teams
				const who = el('select', { className: 'ed-input', disabled: ro, title: 'Who the player is in this battle: a team they command from above (Battle Command), or none, to watch' },
					el('option', { value: '-1', textContent: 'Nobody: watch' }), ...[...Array(SIDES).keys()].map((side) => el('option', { value: String(side), textContent: 'Command ' + SIDE_NAMES[side], selected: st.bool('Mode.PlayerCommands.' + side, false) })));
				if (![...Array(SIDES).keys()].some((side) => st.bool('Mode.PlayerCommands.' + side, false))) {
					who.value = '-1';
				}
				who.onchange = () => edit('The player ' + (who.value === '-1' ? 'watches' : 'commands ' + SIDE_NAMES[Number(who.value)]), (x) => {
					for (let side = 0; side < SIDES; side++) {
						x.set('Mode.PlayerCommands.' + side, String(side) === who.value);
					}
				});
				const mods = factions();
				const teamRow = (side) => {
					const plays = el('input', { type: 'checkbox', checked: st.plays(side), disabled: ro });
					plays.onchange = () => edit((plays.checked ? 'Add ' : 'Take out ') + SIDE_NAMES[side], (x) => x.set('Mode.Plays.' + side, plays.checked));
					const size = el('input', { type: 'number', className: 'ed-input be-num', min: 1, max: 100, value: st.num('Mode.TeamSizes.' + side, 16), disabled: ro || !st.plays(side), title: 'Most units it has in at once' });
					size.onchange = () => edit(SIDE_NAMES[side] + '\'s size', (x) => x.set('Mode.TeamSizes.' + side, Math.max(1, Math.min(100, Math.round(Number(size.value) || 16)))));
					const now = String(st.raw('Team' + side + '.Factions') || '').split(';').filter(Boolean);
					const fac = el('select', { className: 'ed-input be-fac', disabled: ro || !st.plays(side), title: 'The faction its units come from' },
						el('option', { value: '', textContent: 'Any faction' }),
						now.length > 1 ? el('option', { value: now.join(';'), textContent: now.length + ' factions', selected: true }) : null,
						...mods.map((m) => el('option', { value: m, textContent: m.replace(/[.]rte$/i, ''), selected: now.length === 1 && now[0] === m })));
					fac.onchange = () => edit(SIDE_NAMES[side] + '\'s faction', (x) => x.set('Team' + side + '.Factions', fac.value));
					const ai = el('select', { className: 'ed-input', disabled: ro || !st.plays(side), title: 'Whether an AI commander leads it (not a team the player commands)' },
						el('option', { value: '1', textContent: 'AI commander', selected: st.bool('Mode.Commander.' + side, true) }), el('option', { value: '0', textContent: 'No commander', selected: !st.bool('Mode.Commander.' + side, true) }));
					ai.onchange = () => edit(SIDE_NAMES[side] + '\'s commander', (x) => x.set('Mode.Commander.' + side, ai.value === '1'));
					return el('div', { className: 'be-team' + (st.plays(side) ? '' : ' off') }, el('label', { className: 'be-flag' }, plays, el('span', { className: 'me-swatch', style: 'background:' + SIDE_COLOURS[side] }), el('b', { textContent: SIDE_NAMES[side] })), size, fac, ai);
				};
				const role = sp.role ? (() => {
					const pick = el('select', { className: 'ed-input', disabled: ro }, ...[...Array(SIDES).keys()].map((side) => el('option', { value: String(side), textContent: SIDE_NAMES[side], selected: st.role(sp.role) === side })));
					pick.onchange = () => edit(sp.role.label + ': ' + SIDE_NAMES[Number(pick.value)], (x) => x.set('Mode.' + sp.role.key, Number(pick.value)));
					return el('label', { className: 'ed-check' }, sp.role.label, pick);
				})() : null;
				const level = el('select', { className: 'ed-input', disabled: ro, title: 'How hard the AI commanders play' }, ...LEVELS.map(([v, t]) => el('option', { value: String(v), textContent: t, selected: st.num('Mode.CommanderLevel', 1) === v })));
				level.onchange = () => edit('AI commanders: ' + level.selectedOptions[0].textContent, (x) => x.set('Mode.CommanderLevel', Number(level.value)));
				// what to place
				const sel = s.map.sel;
				const placeRows = items.map((n) => {
					const placing = s.map.place === n.key;
					const shapes = st.shapes(n.key);
					const row = el('div', { className: 'be-need' + (placing ? ' placing' : '') },
						el('span', { className: 'be-tick ' + (n.done ? 'done' : n.required ? 'missing' : 'optional'), textContent: n.done ? '✓' : n.required ? '!' : '○', title: n.done ? 'Placed' : n.required ? 'Needed before it can start' : 'Optional' }),
						el('span', { className: 'me-swatch', style: 'background:' + n.colour }),
						el('span', { className: 'be-need-name', textContent: n.label + (n.many && n.count ? ' (' + n.count + (n.max ? ' of ' + n.max : '') + ')' : ''), title: n.note || '' }),
						ro ? null : el('button', { className: 'tb' + (placing ? ' primary' : ''), textContent: placing ? 'Placing… (Esc)' : n.many && n.done ? '+ Add' : n.done ? 'Replace' : 'Place', onclick: () => (placing ? hooks.donePlacing() : startPlacing(n)) }));
					const out = [row];
					if (n.many && shapes.length > 1 || n.ordered && shapes.length) {
						out.push(el('div', { className: 'be-shapes' }, ...shapes.map((_, i) => el('div', { className: 'be-shape' + (sel && sel.key === n.key && sel.i === i ? ' sel' : '') },
							el('button', { className: 'me-area', textContent: n.label + ' ' + (i + 1), onclick: () => {
								s.map.sel = { key: n.key, i };
								drawAll();
							} }),
							n.ordered && !ro ? el('button', { className: 'tb', textContent: '↑', title: 'Earlier in the order', disabled: i === 0, onclick: () => reorder(n.key, i, -1) }) : null,
							n.ordered && !ro ? el('button', { className: 'tb', textContent: '↓', title: 'Later in the order', disabled: i === shapes.length - 1, onclick: () => reorder(n.key, i, 1) }) : null,
							ro ? null : el('button', { className: 'tb', textContent: '✕', title: 'Take it out', onclick: () => remove(n.key, i) })))));
					}
					return out;
				}).flat();
				// mirror: a team's spawn zones and point copied to another, flipped across the map
				const mirrorFrom = el('select', { className: 'ed-input' }, ...[...Array(SIDES).keys()].filter((x) => st.plays(x)).map((x) => el('option', { value: String(x), textContent: SIDE_NAMES[x] })));
				const mirrorTo = el('select', { className: 'ed-input' }, ...[...Array(SIDES).keys()].filter((x) => st.plays(x)).map((x, i) => el('option', { value: String(x), textContent: SIDE_NAMES[x], selected: i === 1 })));
				const mirror = el('button', { className: 'tb', textContent: 'Mirror', disabled: ro, title: 'Puts a copy of the first team\'s spawn zones (and its flag or goal) in place of the second\'s, flipped left to right across the map', onclick: () => {
					const from = Number(mirrorFrom.value);
					const to = Number(mirrorTo.value);
					if (from === to) {
						return;
					}
					const width = view.world().w;
					const flip = (pts) => pts.map((p) => ({ x: width - p.x, y: p.y }));
					edit('Mirror ' + SIDE_NAMES[from] + ' onto ' + SIDE_NAMES[to], (x) => {
						for (const k of ['SpawnZones', 'Points', 'Goals']) {
							x.setShapes('Mode.' + k + '.' + to, x.shapes('Mode.' + k + '.' + from).map(flip));
						}
					});
				} });
				// the rules
				const ringRules = st.mode === 11 ? [...Array(Math.max(0, Math.min(4, st.num('Mode.PointRings', 2))) + 1).keys()].map((i) => el('div', { className: 'be-ringrow' },
					el('span', { textContent: i ? 'Ring ' + i : 'Bullseye' }),
					number('RingRadius.' + i, 'reach (px)', 10, 2000, RING_DEFAULTS.radius[i]), number('RingPoints.' + i, 'points a second', 0, 100, RING_DEFAULTS.points[i]))) : [];
				const pickups = st.bool('Mode.Pickups', false);
				const places = st.num('Mode.PickupPlaces', 1);
				const placeBox = (bit, label) => {
					const input = el('input', { type: 'checkbox', checked: !!(places & bit), disabled: ro });
					input.onchange = () => edit('Pickups ' + label.toLowerCase(), (x) => x.set('Mode.PickupPlaces', (input.checked ? places | bit : places & ~bit) || 1));
					return el('label', { className: 'be-rule be-flag' }, input, el('span', { textContent: label }));
				};
				const more = el('details', { className: 'be-more', open: !!s.moreOpen }, el('summary', { textContent: 'More: respawns, arrivals, AI, pickups' }),
					...SHARED_RULES.map(rule), el('label', { className: 'be-rule' }, el('span', { textContent: 'AI commanders play' }), level),
					flag('Pickups', 'Pickups come in on the map', false),
					...(pickups ? [...PICKUP_RULES.map(rule), placeBox(1, 'Anywhere'), placeBox(2, 'At pickup spots'), placeBox(4, 'In pickup zones')] : []));
				more.ontoggle = () => {
					s.moreOpen = more.open;
				};
				panel.replaceChildren(
					ro ? el('div', { className: 'ed-banner', textContent: scenario.module + ' is read only here. Make a battle of your own with + New battle to change it.' }) : null,
					section('Mode', modes, el('p', { className: 'ed-note', textContent: sp.blurb })),
					section('Teams', el('div', { className: 'be-teamhead' }, el('label', { className: 'ed-check' }, 'The player', who), role), ...[...Array(SIDES).keys()].map(teamRow)),
					section('Place on the map', why ? el('div', { className: 'be-why', textContent: why }) : el('div', { className: 'be-ready', textContent: 'Ready to play.' }), ...placeRows,
						el('div', { className: 'be-mirror' }, el('span', { textContent: 'Mirror' }), mirrorFrom, el('span', { textContent: 'onto' }), mirrorTo, mirror),
						el('p', { className: 'ed-note', textContent: 'Place, then click (points) or drag a box (zones) on the map; right-click the map to place something there. Drag to move, drag a corner to reshape, Alt-click an edge to add a corner, Shift-click a corner to take it out, Delete takes out what is picked.' })),
					section('Rules', ...sp.rules.map(rule), ...ringRules, more),
					section('With the game', el('div', { className: 'be-io' },
						el('button', { className: 'tb', textContent: 'Copy from the game\'s Battle tab…', disabled: ro || !scene, title: 'Brings in a setup saved on this map from the Battle tab (in the Sandbox or Battle Command)', onclick: () => importFromGame(key) }),
						el('button', { className: 'tb', textContent: 'Save to the game\'s Battle tab', disabled: !scene, title: 'Saves this battle with the map\'s own (Userdata/BattlePresets), to change it there', onclick: () => exportToGame(key) })),
					el('p', { className: 'ed-note', textContent: 'The game lists this battle with ' + (scene ? scene.name : 'its map') + '\'s battles too: in the map browser (Sandbox and Battle Command) and the Sandbox\'s Battle tab, to play there.' })));
			};
			const drawAll = () => {
				if (!host.isConnected) {
					return;
				}
				view.draw();
				drawPanel();
			};
			drawPanel();
			// this tab's way to draw again after a change (here or in another tab)
			if (!views.has(key)) {
				views.set(key, new Set());
			}
			const set = views.get(key);
			for (const fn of [...set]) {
				if (fn.tab === tab) {
					set.delete(fn);
				}
			}
			const again = () => (host.isConnected ? drawAll() : set.delete(again));
			again.tab = tab;
			set.add(again);

			const mapPane = el('div', { className: 'me-left' }, el('div', { className: 'me-mapwrap' }, view.root));
			const splitter = el('div', { className: 'me-splitter', title: 'Drag to share the room between the map and the battle' });
			const body = el('div', { className: 'me-body' }, mapPane, splitter, panel);
			const columns = () => {
				body.style.gridTemplateColumns = 'minmax(240px, ' + s.split + 'fr) 6px minmax(300px, ' + (1 - s.split) + 'fr)';
			};
			columns();
			splitter.onpointerdown = (e) => {
				splitter.setPointerCapture(e.pointerId);
				const r = body.getBoundingClientRect();
				splitter.onpointermove = (ev) => {
					s.split = Math.min(0.8, Math.max(0.2, (ev.clientX - r.left) / r.width));
					columns();
					view.draw();
				};
				splitter.onpointerup = () => {
					splitter.onpointermove = null;
				};
			};
			host.replaceChildren(el('div', { className: 'me be' }, head, body));
			requestAnimationFrame(() => view.draw());
		};
	}

	// ---- with the game's own Battle tab: Userdata/BattlePresets/<map>.txt ----

	const gameFileOf = (sceneName) => 'Userdata/BattlePresets/' + sceneName.replace(/[<>:"/\\|?*\x00-\x1f]/g, '_') + '.txt';

	async function readGameFile(sceneName) {
		try {
			return parse(await app().fs.readDiskText(gameFileOf(sceneName)));
		} catch (err) {
			return parse('');
		}
	}

	async function importFromGame(key) {
		const scenario = scenarioByKey(key);
		const scene = scenario && sceneOf(scenario);
		if (!scene) {
			return;
		}
		const file = await readGameFile(scene.name);
		if (!file.sections.length) {
			app().toast('No setups are saved on ' + scene.name + ' in the game yet (Battle tab, Saved setups for this map).', 'bad');
			return;
		}
		const list = el('div', { className: 'me-pick' }, ...file.sections.map((sec) => el('button', { className: 'ed-menu-item', onclick: () => {
			MS.dialogs.close();
			change(key, 'Copy ' + sec.name + ' from the game', (st) => {
				const keep = new Map([['Scene', scene.name], ['WithMap', '0'], ['Linked', '0']]);
				for (const [k, v] of sec.lines) {
					if (!keep.has(k)) {
						keep.set(k, v);
					}
				}
				st.sec.lines = keep;
			});
			app().toast('Copied ' + sec.name + ' into ' + scenario.name + '.', 'good');
		} }, el('b', { textContent: sec.name }), el('small', { textContent: (MODES[Number(sec.lines.get('Mode.Mode')) || 0] || { name: 'Custom' }).name }))));
		MS.dialogs.open('Copy a setup from the game', el('div', {}, el('p', { className: 'ed-note', textContent: 'Setups saved on ' + scene.name + ' from the game\'s Battle tab. The one picked takes the place of this battle (Undo puts it back).' }), list), [{ text: 'Cancel' }], true);
	}

	async function exportToGame(key) {
		const scenario = scenarioByKey(key);
		const doc = scenario && docs.get(key);
		const scene = scenario && sceneOf(scenario);
		if (!doc || !scene) {
			return;
		}
		const file = await readGameFile(scene.name);
		const lines = new Map(doc.setup.sec.lines);
		lines.delete('Scene');
		lines.set('WithMap', '0');
		lines.set('Linked', '0');
		const write = async () => {
			const at = file.sections.findIndex((x) => x.name === scenario.name);
			if (at >= 0) {
				lines.set('WithMap', file.sections[at].lines.get('WithMap') || '0');
				file.sections[at] = { name: scenario.name, lines };
			} else {
				file.sections.push({ name: scenario.name, lines });
			}
			if (!file.heading.length) {
				file.heading = ['// Battle Director setups for ' + scene.name + ', saved from the Battle tab.'];
			}
			await app().fs.writeDisk(gameFileOf(scene.name), serialise(file));
			app().toast('Saved as "' + scenario.name + '" with ' + scene.name + '\'s setups in the game.', 'good');
		};
		if (file.sections.some((x) => x.name === scenario.name)) {
			MS.dialogs.open('Save over ' + scenario.name + '?', el('p', { textContent: scene.name + ' already has a setup called ' + scenario.name + ' in the game. Save over it?' }), [{ text: 'Cancel' }, { text: 'Save over it', primary: true, action: () => write().then(() => true) }]);
			return;
		}
		await write();
	}

	// ---- opening one, and making one ----

	function open(scenario) {
		const key = keyOf(scenario);
		return MS.ide.openForm('battle:' + key, 'Battle · ' + scenario.name, '⚔', render(key), { id: 'battle', module: key });
	}

	function openByKey(key) {
		const scenario = scenarioByKey(key);
		return scenario ? open(scenario) : null;
	}

	// + New battle: a copy of Battle Command in the mod, under its name, on its scene, and its battle file with the mode picked.
	function newBattle(module) {
		const a = app();
		module = module || a.project || (a.lib.modules.find((m) => !m.official) || {}).name;
		if (!module) {
			a.toast('Choose the mod you are working on first (Project, at the top).', 'bad');
			return;
		}
		const base = a.lib.presets.find((p) => p.topLevel && p.node.key === 'AddActivity' && p.name === 'Battle Command');
		if (!base) {
			a.toast('This game has no Battle Command to build a battle on.', 'bad');
			return;
		}
		const scenes = a.lib.presets.filter((p) => p.topLevel && p.cls === 'Scene').sort((x, y) => x.name.localeCompare(y.name));
		const name = el('input', { type: 'text', className: 'ed-input', placeholder: 'Hilltop Flags' });
		const scene = el('select', { className: 'ed-input' }, ...scenes.map((p) => el('option', { value: p.name, textContent: p.name + (p.module === 'Base.rte' ? '' : ' (' + p.module.replace(/[.]rte$/i, '') + ')') })));
		const mode = el('select', { className: 'ed-input' }, ...MODES.map((m, i) => (m ? el('option', { value: String(i), textContent: m.icon + ' ' + m.name }) : null)));
		const { field } = MS.dialogs.parts;
		MS.dialogs.open('New battle in ' + module, el('div', {}, field('Name', name, 'What it is called in the game\'s scenario list and its map\'s battles.'), field('Map', scene, 'The scene it is played on.'),
			field('Mode', mode, 'You can change it later.')), [
			{ text: 'Cancel' },
			{ text: 'Make it', primary: true, action: async () => {
				const n = name.value.trim().replace(/[[\]\r\n]/g, '');
				if (!n) {
					return false;
				}
				if (a.lib.presets.some((p) => p.node.key === 'AddActivity' && p.name.toLowerCase() === n.toLowerCase())) {
					a.toast('There is already a scenario called ' + n + '.', 'bad');
					return false;
				}
				const path = module + '/Activities/' + stemOf(n) + '.ini';
				if (await a.fs.exists(path)) {
					a.toast(path + ' is already there.', 'bad');
					return false;
				}
				const m = MODES[Number(mode.value)];
				const doc = a.lib.docFor(path) || (await a.lib.newDoc(path));
				a.hookDoc(doc);
				a.hookDoc(a.lib.docFor(module + '/Index.ini'));
				a.edit('New battle ' + n, () => {
					const block = doc.addTopLevel('AddActivity', base.cls);
					block.add('CopyOf', a.lib.reference(base.name, base.module, module));
					block.add('PresetName', n);
					block.add('Description', m.name + ' on ' + scene.value + '. ' + m.blurb);
					if (scene.value) {
						block.add('SceneName', scene.value);
					}
				});
				await a.save(true, [doc, a.lib.docFor(module + '/Index.ini')].filter(Boolean));
				const made = a.lib.presets.find((p) => p.doc === doc && p.name === n);
				if (!made) {
					return true;
				}
				const file = { heading: [], sections: [freshSection(n, scene.value, Number(mode.value))] };
				await a.fs.writeFile(battlePath(made), serialise(file));
				docs.delete(keyOf(made));
				open(made);
				a.toast('Made the battle ' + n + ' in ' + module + '. Place what the checklist asks for, then ▶ Play.', 'good');
				return true;
			} },
		]);
	}

	MS.battleEditor = { open, openByKey, isBattle, newBattle, MODES, parse, serialise };
})();
