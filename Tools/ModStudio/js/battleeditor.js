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
	const SANDBOX_CLASS = 'SandboxActivity'; // a custom battle is played in the Sandbox (Battle Command plays the modes only)

	// ---- the modes, as SandboxBattleModes.cpp has them (c_Modes, by BattleMode) ----
	// point: each team's own point (Mode.Points.N) and what it is; zones: the mode's own zones (Mode.Zones), what each is, how many it needs and whether
	// their order counts; goals: each team's goal zone (Mode.Goals.N); spots: positions placed (Mode.FlagSpots), what each is and how many at most;
	// role: the one side the mode singles out; rules: [key, label, min, max, default, note] numbers, or [key, label, 'bool', default, note].
	const TIME_LIMIT = ['TimeLimit', 'Time limit (seconds, 0: none)', 0, 3600, 300];
	const MODES = [
		{ name: 'Custom battle', icon: '⚙', custom: true, blurb: 'The Battle Director: each active team keeps sending in units, by ship, drop crate or in its spawn zones, as its card says, until you stop them. No scoring: nobody wins. Played in the Sandbox.',
			rules: [] },
		{ name: 'Capture the flag', icon: '⚑', blurb: 'Each team has a flag and spawn zones. Bring the enemy\'s flag home to your own to score.',
			point: { label: 'flag', note: 'Where its flag stands. Without one, the game picks a place in its base.' },
			rules: [['ScoreToWin', 'Captures to win (0: play on)', 0, 50, 3], ['ReturnSeconds', 'A dropped flag goes home after (seconds)', 1, 300, 30], ['GuardPercent', 'Units guarding their flag (%)', 0, 100, 30],
				['EscortPercent', 'Team-mates going all the way with a carrier (%)', 0, 100, 50], ['MoveStuckPoint', 'A flag nobody can reach moves', 'bool', true]] },
		{ name: 'King of the hill', icon: '⛰', blurb: 'Every unit fights for the hill. A team alone on it scores; first to the time set wins.',
			look: 'ground', zones: { label: 'Hill', min: 1, ordered: true, note: 'With more than one, they come into play in this order.' },
			rules: [['HoldToWin', 'Seconds held to win', 5, 3600, 120], ['HillMoveSeconds', 'The hill moves on after (seconds, 0: stays)', 0, 3600, 0], ['HillsToWin', 'Hills to take to win (0: seconds held win)', 0, 20, 0],
				['MajorityScores', 'A shared hill scores for the team with most there', 'bool', false],
				['HillMovesWhenTaken', 'The hill moves when taken', 'bool', false, 'Playing for hills: when a team takes it. Playing for seconds held: when a team holds it alone for the seconds below.'],
				['HillTakeSeconds', 'Seconds held alone to take it (seconds held, moving when taken)', 1, 300, 20],
				['NextHill', 'Where a hill moving when taken goes', 'choice', 2, [[0, 'The next hill, in order'], [1, 'Another hill, at random'], [2, 'A new spot, picked as the battle goes']],
					'A new spot needs only the first hill drawn: fresh ground every team can get to, away from the last hill and the bases. With one hill drawn, the others pick a new spot too.']] },
		{ name: 'Assault', icon: '➤', blurb: 'One team attacks a line of objectives in order; the rest defend them. Each one taken buys the attackers time.',
			look: 'glow', zones: { label: 'Objective', min: 1, ordered: true, note: 'Taken in this order: drag them in the list to change it.' },
			role: { key: 'Attacker', label: 'Attackers' },
			rules: [['CaptureSeconds', 'Seconds to take an objective', 1, 300, 15], TIME_LIMIT, ['BonusSeconds', 'Seconds added for each one taken', 0, 600, 60],
				['BlockLastRespawns', 'No defender respawns at the last objective under attack', 'bool', false],
				['AttackerSpawn', 'Attackers come in, once objectives are taken', 'choice', 1, [[0, 'At their own spawn zones'], [1, 'At the last objective taken'], [2, 'One objective behind']],
					'Own spawn zones: where they started, all game. Last taken: the objective they took last is their only spawn zone. One behind: the first taken, then always the one before the last taken.'],
				['DefenderSpawn', 'Defenders come in', 'choice', 1, [[0, 'At their own spawn zones'], [1, 'Two objectives ahead']],
					'Own spawn zones: where they started, all game. Two ahead: the objective two after the one being attacked is their only spawn zone (attacking 1, they come in at 3); once there is none that far on, their own spawn zones.']] },
		{ name: 'Last team standing', icon: '☠', blurb: 'Every team has so many units in all and hunts the nearest enemy. The last team in wins.',
			rules: [['Tickets', 'Units each team gets in all', 1, 1000, 60]] },
		{ name: 'VIP hunt', icon: '♛', blurb: 'One unit of each team is its VIP. Bring down the enemy VIPs to score.',
			point: { label: 'VIP spot', note: 'Where its VIP keeps to. Without one, its widest spawn zone.' },
			rules: [['KillsToWin', 'VIPs to bring down to win', 1, 100, 5], ['VipRespawnSeconds', 'A new VIP after (seconds)', 1, 300, 20], ['GuardPercent', 'Bodyguards (%)', 0, 100, 30]] },
		{ name: 'One flag', icon: '⚐', blurb: 'One neutral flag, and every team after it. Bring it into your own goal zone to score.',
			spots: { label: 'Flag position', max: 8, note: 'The flag comes in at each in turn, the next after every score.' },
			zones: { label: 'Flag spawn zone', min: 0, note: 'With "Flag comes in a zone" ticked: somewhere in one of these, another each time.' },
			goals: { label: 'goal zone' }, look: 'outline',
			rules: [['ScoreToWin', 'Scores to win (0: play on)', 0, 50, 3], ['ReturnSeconds', 'A dropped flag goes back after (seconds)', 1, 300, 30], ['EscortPercent', 'Team-mates going all the way with a carrier (%)', 0, 100, 50],
				['FlagByZones', 'Flag comes in a zone (not at the positions)', 'bool', false]] },
		{ name: 'Capture the ship', icon: '✈', blurb: 'One ship, and every team after it. Fly it down into your landing zone to score.',
			spots: { label: 'Ship position', max: 8, note: 'Each ship comes in at the next of these.' },
			goals: { label: 'landing zone' }, look: 'outline',
			rules: [['ScoreToWin', 'Landings to win (0: play on)', 0, 50, 3], ['ReturnSeconds', 'A ship shot down is replaced after (seconds)', 1, 300, 30], ['EscortPercent', 'Team-mates following the ship (%)', 0, 100, 50]] },
		{ name: 'Wave defence', icon: '⛨', blurb: 'One team holds a defence zone against waves of attackers from the rest, each bigger than the last.',
			look: 'glow', zones: { label: 'Defence zone', min: 1, note: 'The first is the one held.' },
			role: { key: 'Defender', label: 'Defenders' },
			rules: [['Waves', 'Waves to beat (0: endless)', 0, 200, 10], ['FirstWave', 'Attackers in the first wave', 1, 200, 6], ['WaveGrowth', 'More each wave', 0, 100, 3],
				['WaveBreak', 'Seconds between waves', 0, 600, 20], ['CaptureSeconds', 'Seconds attackers hold the zone to win', 1, 300, 15]] },
		{ name: 'Escort', icon: '⛉', blurb: 'One team takes its VIP to a destination; every other team hunts it.',
			look: 'outline', zones: { label: 'Destination', min: 1, note: 'The first is where the VIP goes.' },
			role: { key: 'EscortSide', label: 'Escort' },
			rules: [TIME_LIMIT] },
		{ name: 'Extraction', icon: '⇪', blurb: 'One team gets its units out through an extraction zone; the rest hold it against them.',
			look: 'glow', zones: { label: 'Extraction zone', min: 1, note: 'The first is the way out.' },
			role: { key: 'ExtractSide', label: 'Getting out' },
			rules: [['Tickets', 'Units it has in all', 1, 1000, 60], ['ExtractToWin', 'Units out to win', 1, 500, 10], ['ExtractSeconds', 'Seconds in the zone to be lifted out', 0, 60, 3], TIME_LIMIT] },
		{ name: 'The point', icon: '◎', blurb: 'One point with rings round it, each worth its own points a second to the team alone in it.',
			spots: { label: 'The point', max: 1, note: 'The bullseye. Its rings are drawn round it.' },
			rules: [['PointsToWin', 'Points to win (0: play on)', 0, 100000, 300], ['PointRings', 'Rings round the bullseye', 0, 4, 2], ['MajorityScores', 'A shared band scores for the team with most there', 'bool', false]] },
		{ name: 'Territory Control', icon: '▬', blurb: 'The whole map\'s ground is the zone. Each unit claims the ground under it, and every team scores its share of the line each second.',
			look: 'ground', zones: { label: 'Territory zone', min: 0, note: 'None: the ground all the way across the map counts. Drawn: only the ground in them (straight below, where a zone has no floor).' },
			rules: [['TerritoryToWin', 'Points to win (0: play on)', 0, 100000, 1000], ['TerritoryRate', 'Points a second for holding all of it', 1, 100, 10],
				['TerritoryKeep', 'Ground counts', 'choice', 0, [[0, 'While a unit stands on it'], [1, 'Taken till an enemy takes it']],
					'Taken: a unit walking over ground takes it for its team, and it stays theirs till an enemy unit walks over it.'],
				['TerritoryHoldSeconds', 'Taken ground stays taken for (seconds, 0: unlimited)', 0, 300, 0],
				['TerritoryScale', 'Claim width (% of the unit\'s width)', 25, 300, 100], ['TerritoryPadding', 'Claim padding (px each side)', 0, 60, 6],
				['TerritoryAirCounts', 'Flying units count', 'bool', false], ['TerritoryUnderCounts', 'Units underground count', 'bool', false],
				['TerritorySectors', 'Sectors (objectives along the line)', 1, 16, 8]] },
	];
	// Every mode's own, as the Battle tab's "Respawns and routes" has them.
	const SHARED_RULES = [['RespawnSeconds', 'Seconds before a fallen unit is replaced (0: at once)', 0, 60, 5], ['MaxRespawns', 'Units each team gets back in all (0: no limit)', 0, 500, 0],
		['CommanderCallsIn', 'Commander calls in reinforcements', 'bool', false, 'A team you command doesn\'t respawn by itself: each fallen unit goes into its reserves once its respawn time is up, and they come in only when you call them (the Reserves tile, Alt+R). Teams the AI commands respawn as before.'],
		['StuckSeconds', 'Respawn a unit stuck for (seconds, 0: never)', 0, 120, 20], ['RouteVariety', 'Units taking routes of their own (%)', 0, 100, 0],
		['MovingSpawns', 'You can move your spawn zones during the battle', 'bool', false, 'Teams you command: drag their spawn zones about behind the spawn line while it is on.']];
	// Where units come in (ByShip, Anywhere).
	const ARRIVALS = [['Appear in their spawn zones', false, false], ['Appear anywhere', false, true], ['Ships over their base', true, false], ['Ships anywhere', true, true]];
	const PICKUP_RULES = [['PickupEvery', 'Seconds between pickups', 5, 600, 45], ['PickupMost', 'Most on the map at once', 1, 20, 3], ['PickupLasts', 'A pickup lies for (seconds, 0: till taken)', 0, 600, 90]];
	const PICKUP_KINDS = ['Energy shield', 'Stim', 'Overcharge', 'Cloak', 'Medkit', 'Juggernaut', 'Spell scroll']; // bits of PickupKinds, by c_PickupKinds
	const ARMY_CARDS = [['Rifleman', 8], ['Sniper', 2], ['Heavy', 2], ['Digger', 2], ['Crab', 0]]; // c_ReinforcementCards, with Army's defaults
	const CRAFTS = ['Dropship', 'Rocket', 'Drop crate'];
	const STYLES = ['Attack nearest enemy', 'Hunt brains', 'Defend a place', 'Patrol', 'Sentry']; // c_BattleStyleNames, by BattleStyle
	// A spawn order's kinds (SpawnOrderKind) and the points each takes: none, one, a route, or one it can do without.
	const ORDER_KINDS = [['Hold where they land', 'none'], ['Move', 'one'], ['Attack-move', 'one'], ['Defend at', 'one'], ['Patrol', 'route'], ['Jetpack here', 'one'], ['Battle objective', 'optional']];
	// How the battle shows (the Battle tab's show settings, the battle's own when set): -1 the player's own setting.
	const SHOWS = [['ShowObjectives', 'Battle objectives lit up'], ['ShowSpawnZones', 'Spawn zones'], ['ShowModeZones', 'The mode\'s own zones (hills, objectives, goals)'], ['ShowInfo', 'Battle info under the score']];
	const LOOKS = ['Each mode\'s own', 'Glowing ring', 'Glowing outline', 'Glowing ground line', 'Glowing terrain'];
	const LOOK_OF = [null, 'outline', 'outline', 'ground', 'glow']; // ObjectiveLook + 1 -> how a zone is drawn (a ring on a zone is an outline)
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

	// A spawn order: "set kind facing risk% backAndForth radius chase roam; x,y x,y", as SandboxBattlePresets.cpp writes it.
	function parseOrder(t) {
		const [head, pts] = String(t || '').split(';');
		const n = String(head || '').trim().split(/\s+/).map(Number);
		const at = (i, def) => (n[i] === undefined || isNaN(n[i]) ? def : n[i]);
		return { set: at(0, 0) !== 0, kind: Math.max(0, Math.min(ORDER_KINDS.length - 1, at(1, 0))), facing: Math.max(-1, Math.min(1, at(2, 0))), risk: at(3, 0), back: at(4, 0) !== 0,
			radius: at(5, 150), chase: at(6, 300), roam: at(7, 0), points: parsePoints(pts) };
	}
	const fmtOrder = (o) => [o.set ? 1 : 0, o.kind, o.facing, Math.round(o.risk), o.back ? 1 : 0, o.radius, o.chase, o.roam].join(' ') + '; ' + fmtPoints(o.points);

	// What a key holds: a list of zones, one zone (a goal), a list of positions, one point (a team's own, with its HasPoint), or a spawn order's points
	// (a team's, Mode.SpawnOrders.N, or one of its spawn zones', Mode.ZoneSpawnOrders.N.i).
	// A custom battle's: each team card's spawn zones (TeamN.SpawnZones, kept the same as Mode.SpawnZones.N, as the game keeps them), its defence
	// point (TeamN.DefendPos, with HasDefendPos) and its drop line (TeamN.Line: LineA and LineB, with HasLine).
	function typeOf(key) {
		if (/^Team\d\.SpawnZones$/.test(key)) {
			return 'zones';
		}
		if (/^Team\d\.DefendPos$/.test(key)) {
			return 'point';
		}
		if (/^Team\d\.Line$/.test(key)) {
			return 'line';
		}
		if (/^Mode\.(SpawnOrders\.\d|ZoneSpawnOrders\.\d\.\d+)$/.test(key)) {
			return 'order';
		}
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
			return Math.max(0, Math.min(MODES.length - 1, this.num('Mode.Mode', 1)));
		}

		// Whether a team takes part: in a mode, Plays; in a custom battle, its card's Active.
		playsKey(side) {
			return this.mode === 0 ? 'Team' + side + '.Active' : 'Mode.Plays.' + side;
		}

		plays(side) {
			return this.bool(this.playsKey(side), this.mode !== 0 && side < 2);
		}

		// A point's "placed" flag: a team's own point's HasPoint, or a card's HasDefendPos.
		static hasKey(key) {
			return /^Team\d\./.test(key) ? key.split('.')[0] + '.HasDefendPos' : 'Mode.HasPoint.' + key.split('.')[2];
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
				const p = parsePoint(v);
				return this.bool(Setup.hasKey(key), false) && p ? [[p]] : [];
			}
			if (t === 'line') {
				const team = key.split('.')[0];
				const a = parsePoint(this.raw(team + '.LineA'));
				const b = parsePoint(this.raw(team + '.LineB'));
				return this.bool(team + '.HasLine', false) && a && b ? [[a, b]] : [];
			}
			if (t === 'order') {
				return this.order(key).points.map((p) => [p]);
			}
			return [];
		}

		setShapes(key, shapes) {
			const t = typeOf(key);
			if (t === 'zones') {
				this.set(key, fmtZones(shapes));
				// (a team's spawn zones are its card's too, whichever is drawn)
				const zones = /^Mode\.SpawnZones\.(\d)$/.exec(key) || /^Team(\d)\.SpawnZones$/.exec(key);
				if (zones) {
					this.set(key.startsWith('Mode') ? 'Team' + zones[1] + '.SpawnZones' : 'Mode.SpawnZones.' + zones[1], fmtZones(shapes));
				}
			} else if (t === 'line') {
				const team = key.split('.')[0];
				this.set(team + '.HasLine', shapes.length > 0);
				if (shapes.length) {
					this.set(team + '.LineA', fmtPoint(shapes[0][0]));
					this.set(team + '.LineB', fmtPoint(shapes[0][1]));
				}
			} else if (t === 'poly') {
				this.set(key, shapes.length ? fmtPoints(shapes[0]) : '');
			} else if (t === 'points') {
				this.set(key, fmtPoints(shapes.map((s) => s[0])));
			} else if (t === 'point') {
				this.set(Setup.hasKey(key), shapes.length > 0);
				this.set(key, shapes.length ? fmtPoint(shapes[0][0]) : '0,0');
			} else if (t === 'order') {
				this.setOrder(key, Object.assign(this.order(key), { points: shapes.map((x) => x[0]) }));
			}
		}

		// A spawn order, by its key (as typeOf): a zone's, from its team's list of them.
		order(key) {
			const m = /^Mode\.ZoneSpawnOrders\.(\d)\.(\d+)$/.exec(key);
			if (m) {
				return parseOrder(String(this.raw('Mode.ZoneSpawnOrders.' + m[1]) || '').split('|')[Number(m[2])]);
			}
			const o = parseOrder(this.raw(key));
			o.set = true; // (a team's own is always set)
			return o;
		}

		setOrder(key, o) {
			const m = /^Mode\.ZoneSpawnOrders\.(\d)\.(\d+)$/.exec(key);
			if (!m) {
				this.set(key, fmtOrder(Object.assign({}, o, { set: true })));
				return;
			}
			const raw = this.raw('Mode.ZoneSpawnOrders.' + m[1]);
			const list = raw ? raw.split('|').map((x) => x.trim()) : [];
			while (list.length <= Number(m[2])) {
				list.push(fmtOrder(parseOrder('')));
			}
			list[Number(m[2])] = fmtOrder(o);
			this.set('Mode.ZoneSpawnOrders.' + m[1], list.join(' | '));
		}

		// A team's spawn zone taken out: its own order goes with it, so the rest keep theirs.
		dropZoneOrder(side, i) {
			const raw = this.raw('Mode.ZoneSpawnOrders.' + side);
			if (raw) {
				this.set('Mode.ZoneSpawnOrders.' + side, raw.split('|').map((x) => x.trim()).filter((_, j) => j !== i).join(' | '));
			}
		}

		// The battle's own show setting, or -1: the player's.
		show(key) {
			return Math.max(-1, Math.min(key === 'ObjectiveLook' ? 4 : 1, this.num('Mode.' + key, -1)));
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

	// A Battle: a copy of Battle Command (running its script) under a name of its own, or of the Sandbox for a custom battle.
	function isBattle(scenario) {
		if (!scenario || scenario.node.key !== 'AddActivity' || scenario.name === 'Battle Command' || scenario.name === 'Sandbox') {
			return false;
		}
		return [BATTLE_CLASS, SANDBOX_CLASS].includes(app().lib.resolve(scenario.node).str('LuaClassName', ''));
	}

	// What a battle is built on: the Sandbox for a custom battle, Battle Command for the modes.
	const baseFor = (mode) => app().lib.presets.find((p) => p.topLevel && p.node.key === 'AddActivity' && p.name === (mode === 0 ? 'Sandbox' : 'Battle Command')) || null;

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
		if (spec.custom) {
			// a custom battle: each active team's spawn zones, defence point and drop line, as its card has them
			for (const side of playing) {
				const team = 'Team' + side + '.';
				out.push({ key: team + 'SpawnZones', label: SIDE_NAMES[side] + ' spawn zone', side, required: false, many: true, max: 8, note: 'Where ' + SIDE_NAMES[side] + '\'s units appear, a lot every so often. None: they come only by ship or drop crate.' });
				if (setup.num(team + 'Style', 0) === 2) {
					out.push({ key: team + 'DefendPos', label: SIDE_NAMES[side] + ' defence point', side, required: false, many: false, note: 'The place it defends. Not set: it attacks till it is.' });
				}
				if (setup.bool(team + 'DropOnLine', false)) {
					out.push({ key: team + 'Line', label: SIDE_NAMES[side] + ' drop line', side, required: false, many: false, note: 'Its ships come in over this line. Not drawn: anywhere till it is. Drag along the map to draw it.' });
				}
			}
			for (const n of out) {
				n.count = setup.shapes(n.key).length;
				n.done = n.count > 0;
				n.type = typeOf(n.key);
				n.colour = SIDE_COLOURS[n.side];
			}
			return out;
		}
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
		// the points of the spawn orders of the teams the player commands (the team's own, and each spawn zone's with one)
		for (const side of playing.filter((x) => setup.bool('Mode.PlayerCommands.' + x, false))) {
			const keys = [['Mode.SpawnOrders.' + side, SIDE_NAMES[side] + ' spawn order']];
			setup.shapes('Mode.SpawnZones.' + side).forEach((_, i) => keys.push(['Mode.ZoneSpawnOrders.' + side + '.' + i, SIDE_NAMES[side] + ' spawn ' + (i + 1) + '\'s order']));
			for (const [key, label] of keys) {
				const o = setup.order(key);
				const takes = ORDER_KINDS[o.kind][1];
				if (o.set && takes !== 'none') {
					out.push({ key, label: label + ': ' + ORDER_KINDS[o.kind][0].toLowerCase(), side, required: false, many: takes === 'route', max: takes === 'route' ? 16 : 1, order: true,
						note: takes === 'route' ? 'The patrol\'s route, a point at a time.' : takes === 'optional' ? 'Near which objective. Without one: the nearest to each unit.' : 'Where its new units are sent.' });
				}
			}
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

	const pointy = (type) => type === 'point' || type === 'points' || type === 'order';

	// Why the game won't start it yet (as ModeReady says), or '' when it will.
	function problem(setup) {
		const spec = MODES[setup.mode];
		const playing = [...Array(SIDES).keys()].filter((side) => setup.plays(side));
		if (spec.custom) {
			return playing.length ? '' : 'Tick Active on at least one team.';
		}
		if (playing.length < 2) {
			return 'Tick at least 2 teams.';
		}
		if (spec.role && !setup.plays(setup.role(spec))) {
			return SIDE_NAMES[setup.role(spec)] + ' has to play: they are the ' + spec.role.label.toLowerCase() + '.';
		}
		const missing = needs(setup).find((n) => n.required && !n.done);
		if (missing) {
			const what = missing.label.toLowerCase();
			return (pointy(missing.type) ? 'Place ' : 'Draw ') + (missing.side >= 0 ? missing.label : (/^[aeiou]/.test(what) ? 'an ' : 'a ') + what) + ' on the map.';
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
				label = n.type === 'line' ? SIDE_NAMES[n.side] + ' drop line' : label;
				if (n.many && (n.ordered || n.count > 1)) {
					label += ' ' + (i + 1);
				}
				out.push({ key: n.key, i, type: n.type, pts, colour: n.colour, label, side: n.side, ring: n.key === 'Mode.FlagSpots' && setup.mode === 11 && i === 0, faint: faintOf(setup, n.key), look: lookOf(setup, n.key) });
			});
			// a patrol's route, joined up
			if (n.order && n.many && n.count > 1) {
				(out.routes || (out.routes = [])).push({ pts: setup.shapes(n.key).map((x) => x[0]), colour: n.colour, loop: !setup.order(n.key).back });
			}
		}
		if (spec && setup.mode === 11) {
			out.rings = rings(setup);
		}
		// Territory control with no zones drawn: the whole map's ground is the line.
		out.wholeLine = !!spec && setup.mode === 12 && setup.shapes('Mode.Zones').length === 0 && showing(setup, 'ShowObjectives');
		return out;
	}

	// Shown as the battle shows them in the game: the battle's own setting, else the game's (each on by default).
	const showing = (setup, key) => setup.show(key) !== 0;

	// A zone hidden in the game (spawn zones or the mode's own zones shown off): drawn faint here, to still be placed and moved.
	function faintOf(setup, key) {
		if (/^Mode\.SpawnZones\.|^Team\d\.SpawnZones$/.test(key)) {
			return !showing(setup, 'ShowSpawnZones');
		}
		return /^Mode\.(Zones|Goals\.\d)$/.test(key) && !showing(setup, 'ShowModeZones');
	}

	// How a zone objective is lit up in the game (DrawObjective): the mode's own look, or the battle's (or nothing with objectives not shown).
	// A flag spawn zone (one flag) isn't an objective.
	function lookOf(setup, key) {
		const spec = MODES[setup.mode];
		const isObjective = (key === 'Mode.Zones' && spec.zones && setup.mode !== 6) || (/^Mode\.Goals\.\d$/.test(key) && spec.goals);
		if (!isObjective || !showing(setup, 'ShowObjectives')) {
			return null;
		}
		const look = setup.show('ObjectiveLook');
		return look > 0 ? LOOK_OF[look] : spec.look || 'outline';
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

	// The ground in a zone, as the game finds it to light it up (ZoneTerrain): its solid cells (the edge ones, next to air, marked) and the crest,
	// the top of the ground across it, in runs. Kept by the zone's corners.
	const zoneTerrains = new Map();
	function zoneTerrain(g, pts) {
		if (!g || pts.length < 3) {
			return null;
		}
		const key = g.w + 'x' + g.h + ':' + fmtPoints(pts);
		if (zoneTerrains.has(key)) {
			return zoneTerrains.get(key);
		}
		const xs = pts.map((p) => p.x);
		const ys = pts.map((p) => p.y);
		const left = Math.max(0, Math.floor(Math.min(...xs)));
		const right = Math.min(g.w - 1, Math.ceil(Math.max(...xs)));
		const top = Math.max(0, Math.floor(Math.min(...ys)));
		const bottom = Math.min(g.h - 1, Math.ceil(Math.max(...ys)));
		const cell = Math.max(4, Math.ceil(Math.sqrt(((right - left) * (bottom - top)) / 40000)));
		const solidCell = (x, y) => solid(g, x + cell / 2, y + cell / 2);
		const inZone = (x, y) => insideZone(pts, { x: x + cell / 2, y: y + cell / 2 });
		const out = { cell, cells: [], crest: [] };
		let run = null;
		for (let x = left; x <= right; x += cell) {
			let crest = null;
			for (let y = top; y <= bottom; y += cell) {
				if (!inZone(x, y) || !solidCell(x, y)) {
					continue;
				}
				const edge = !solidCell(x - cell, y) || !solidCell(x + cell, y) || !solidCell(x, y - cell) || !solidCell(x, y + cell);
				out.cells.push([x, y, edge]);
				if (!crest && !solidCell(x, y - cell)) {
					crest = { x: x + cell / 2, y };
				}
			}
			if (crest && run && Math.abs(crest.y - run[run.length - 1].y) <= cell * 4) {
				run.push(crest);
			} else if (crest) {
				run = [crest];
				out.crest.push(run);
			} else {
				run = null;
			}
		}
		zoneTerrains.set(key, out);
		if (zoneTerrains.size > 200) {
			zoneTerrains.delete(zoneTerrains.keys().next().value);
		}
		return out;
	}

	// A colour halfway to white (the lit edge of glowing ground).
	const mixWhite = (hex) => '#' + [1, 3, 5].map((i) => Math.round(128 + parseInt(hex.slice(i, i + 2), 16) / 2).toString(16).padStart(2, '0')).join('');

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
			const line = list.slice().reverse().find((t) => t.pts.length === 2 && segDist(p, t.pts[0], t.pts[1]) < 8 / this.zoom);
			if (line) {
				return line;
			}
			return list.filter((t) => t.pts.length >= 3 && insideZone(t.pts, p)).sort((a, b) => area(a.pts) - area(b.pts))[0] || null;
		}

		cornerAt(t, sx, sy) {
			if (!t || t.pts.length < 2) {
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

		// A zone objective as the game lights it up (DrawObjective): a glowing line round it, along the top of the ground in it, or its ground glowing.
		drawLook(ctx, t) {
			const colour = t.colour === NEUTRAL ? '#ffffff' : t.colour;
			const glowLine = (pts, closed) => {
				if (pts.length < 2) {
					return;
				}
				ctx.beginPath();
				pts.forEach((c, i) => {
					const s = this.toScreen(c.x, c.y);
					if (i) {
						ctx.lineTo(s.x, s.y);
					} else {
						ctx.moveTo(s.x, s.y);
					}
				});
				if (closed) {
					ctx.closePath();
				}
				ctx.lineJoin = 'round';
				ctx.lineCap = 'round';
				for (const [width, alpha] of [[7, '46'], [2.5, 'e6']]) {
					ctx.strokeStyle = colour + alpha;
					ctx.lineWidth = width;
					ctx.stroke();
				}
				ctx.strokeStyle = 'rgba(255,255,255,.47)';
				ctx.lineWidth = 1;
				ctx.stroke();
			};
			const terrain = t.look === 'outline' ? null : zoneTerrain(this.hooks.groundData(), t.pts);
			if (t.look === 'outline' || !terrain) {
				glowLine(t.pts, true);
			} else if (t.look === 'ground') {
				for (const run of terrain.crest) {
					glowLine(run, false);
				}
			} else {
				const cell = terrain.cell;
				for (const [x, y, edge] of terrain.cells) {
					if (edge) {
						const a = this.toScreen(x - cell * 1.5, y - cell * 1.5);
						ctx.fillStyle = colour + '16';
						ctx.fillRect(a.x, a.y, cell * 4 * this.zoom, cell * 4 * this.zoom);
					}
				}
				for (const [x, y, edge] of terrain.cells) {
					const a = this.toScreen(x, y);
					ctx.fillStyle = edge ? mixWhite(colour) + 'c8' : colour + '3c';
					ctx.fillRect(a.x, a.y, Math.max(cell * this.zoom, 1), Math.max(cell * this.zoom, 1));
				}
			}
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
			// zones under points: team spawns, goals (dashed), the mode's own; one the game hides, faint; then each objective lit up as in the game
			for (const t of list.filter((x) => x.pts.length >= 3)) {
				const picked = sel === t;
				this.path(ctx, t.pts);
				ctx.fillStyle = t.colour + (picked ? '55' : t.faint ? '0a' : t.type === 'poly' ? '1c' : '33');
				ctx.fill();
				ctx.strokeStyle = picked ? '#fff' : t.colour + (t.faint ? '66' : '');
				ctx.lineWidth = picked ? 2.5 : 1.5;
				ctx.setLineDash(t.type === 'poly' || t.faint ? [7, 4] : []);
				ctx.stroke();
				ctx.setLineDash([]);
				if (t.look) {
					this.drawLook(ctx, t);
				}
				const top = t.pts.reduce((a, b) => (b.y < a.y || (b.y === a.y && b.x < a.x) ? b : a));
				const s = this.toScreen(top.x, top.y);
				this.tag(ctx, s.x, s.y - 4, t.label, t.colour);
			}
			// territory control's line along the ground all the way across
			if (list.wholeLine) {
				this.drawLook(ctx, { colour: NEUTRAL, look: 'ground', pts: [{ x: 0, y: 0 }, { x: w.w - 1, y: 0 }, { x: w.w - 1, y: w.h - 1 }, { x: 0, y: w.h - 1 }] });
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
			// a patrol's route
			for (const route of list.routes || []) {
				ctx.beginPath();
				route.pts.concat(route.loop ? [route.pts[0]] : []).forEach((p, i) => {
					const s = this.toScreen(p.x, p.y - 8);
					if (i) {
						ctx.lineTo(s.x, s.y);
					} else {
						ctx.moveTo(s.x, s.y);
					}
				});
				ctx.strokeStyle = route.colour;
				ctx.lineWidth = 1.5;
				ctx.setLineDash([6, 4]);
				ctx.stroke();
				ctx.setLineDash([]);
			}
			// drop lines: a dashed line its ships come in over, with an arrow down at each end
			for (const t of list.filter((x) => x.pts.length === 2)) {
				const a = this.toScreen(t.pts[0].x, t.pts[0].y);
				const b = this.toScreen(t.pts[1].x, t.pts[1].y);
				ctx.beginPath();
				ctx.moveTo(a.x, a.y);
				ctx.lineTo(b.x, b.y);
				ctx.strokeStyle = sel === t ? '#fff' : t.colour;
				ctx.lineWidth = sel === t ? 3 : 2;
				ctx.setLineDash([10, 5]);
				ctx.stroke();
				ctx.setLineDash([]);
				for (const e of [a, b]) {
					ctx.beginPath();
					ctx.moveTo(e.x - 5, e.y + 4);
					ctx.lineTo(e.x, e.y + 11);
					ctx.lineTo(e.x + 5, e.y + 4);
					ctx.stroke();
				}
				this.tag(ctx, Math.min(a.x, b.x), Math.min(a.y, b.y) - 6, t.label, t.colour);
			}
			for (const t of list.filter((x) => x.pts.length === 1)) {
				this.drawFlag(ctx, t.pts[0], t.colour, t.label, sel === t);
			}
			if (sel && sel.pts.length >= 2) {
				ctx.fillStyle = '#fff';
				for (const cn of sel.pts) {
					const s = this.toScreen(cn.x, cn.y);
					ctx.fillRect(s.x - 4, s.y - 4, 8, 8);
				}
			}
			// what's being placed
			const placing = this.hooks.placing();
			const d = this.drag;
			if (d && d.kind === 'box' && d.now && placing && placing.type === 'line') {
				const a = this.toScreen(d.start.x, d.start.y);
				const b = this.toScreen(d.now.x, d.now.y);
				ctx.beginPath();
				ctx.moveTo(a.x, a.y);
				ctx.lineTo(b.x, b.y);
				ctx.strokeStyle = '#fff';
				ctx.lineWidth = 2;
				ctx.stroke();
			} else if (d && d.kind === 'box' && d.now) {
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
				const text = 'Placing: ' + placing.label + (pointy(placing.type) ? ' (click on the map)' : placing.type === 'line' ? ' (drag along the map)' : this.state.shape === 'poly' ? ' (click its corners; double-click or Enter closes it)' : ' (drag a box)') + '. Esc stops.';
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
					if (pointy(placing.type)) {
						const p = this.hooks.ground(world);
						this.hooks.place(placing.key, [{ x: Math.round(p.x), y: Math.round(p.y) }]);
						return;
					}
					if (this.state.shape === 'poly' && placing.type !== 'line') {
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
					if (placing && placing.type === 'line' && d.now && Math.hypot(d.now.x - d.start.x, d.now.y - d.start.y) * this.zoom > 6) {
						this.hooks.place(placing.key, [d.start, d.now].map((p) => ({ x: Math.round(p.x), y: Math.round(p.y) })));
					} else if (placing && placing.type !== 'line' && d.now && Math.abs(d.now.x - d.start.x) * this.zoom > 4 && Math.abs(d.now.y - d.start.y) * this.zoom > 4) {
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
			const remove = (k, i) => edit('Take out ' + ((listNow().find((x) => x.key === k) || {}).label || 'it'), (st) => {
				st.setShapes(k, st.shapes(k).filter((_, j) => j !== i));
				const zone = /^Mode\.SpawnZones\.(\d)$/.exec(k);
				if (zone) {
					st.dropZoneOrder(Number(zone[1]), i);
				}
			});
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
				const items = listNow().map((n) => [(pointy(n.type) ? '' : n.type === 'line' ? '― ' : '▭ ') + n.label + ' here', () => {
					const pts = pointy(n.type) ? [ground(world)].map((p) => ({ x: Math.round(p.x), y: Math.round(p.y) })) : n.type === 'line' ? [{ x: Math.round(world.x - 200), y: Math.round(world.y) }, { x: Math.round(world.x + 200), y: Math.round(world.y) }]
						: box(/SpawnZones/.test(n.key) ? 200 : 160, 120);
					place(n.key, pts);
				}]);
				menuAt(items, cx, cy);
			};
			const hooks = {
				things: () => (docs.get(key) && docs.get(key).setup ? things(docs.get(key).setup) : []),
				terrain, place, move, remove, menu, ground,
				groundData: () => groundOf(terrainFile),
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

			// ---- the panel: mode, teams, what to place, rules, and the rest of the Battle tab's settings ----
			const panel = el('div', { className: 'me-right be-panel' });
			// (A key with no "Mode." or "TeamN." in front is the mode's.)
			const full = (k) => (/^(Mode|Team\d)\./.test(k) ? k : 'Mode.' + k);
			const number = (k, label, min, max, def, note) => {
				const input = el('input', { type: 'number', className: 'ed-input be-num', min, max, value: setup.num(full(k), def), disabled: ro });
				input.onchange = () => {
					const v = Math.max(min, Math.min(max, Math.round(Number(input.value) || 0)));
					edit(label, (st) => st.set(full(k), v));
				};
				return el('label', { className: 'be-rule', title: note || '' }, el('span', { textContent: label }), input);
			};
			const flag = (k, label, def, note) => {
				const input = el('input', { type: 'checkbox', checked: setup.bool(full(k), def), disabled: ro });
				input.onchange = () => edit(label, (st) => st.set(full(k), input.checked));
				return el('label', { className: 'be-rule be-flag', title: note || '' }, input, el('span', { textContent: label }));
			};
			const choice = (k, label, def, options, note) => {
				const now = setup.num(full(k), def);
				const pick = el('select', { className: 'ed-input', disabled: ro }, ...options.map(([v, t]) => el('option', { value: String(v), textContent: t, selected: now === v })));
				pick.onchange = () => edit(label + ': ' + pick.selectedOptions[0].textContent, (st) => st.set(full(k), Number(pick.value)));
				return el('label', { className: 'be-rule', title: note || '' }, el('span', { textContent: label }), pick);
			};
			const rule = (r) => (r[2] === 'bool' ? flag(r[0], r[1], r[3], r[4]) : r[2] === 'choice' ? choice(r[0], r[1], r[3], r[4], r[5]) : number(r[0], r[1], r[2], r[3], r[4], r[5]));
			const section = (title, ...body) => el('section', { className: 'be-sec' }, el('h3', { textContent: title }), ...body);
			// A section that folds away, remembered on the tab.
			const fold = (id, title, ...body) => {
				const box = el('details', { className: 'be-sec be-fold', open: !!(s.open && s.open[id]) }, el('summary', {}, el('h3', { textContent: title })), ...body);
				box.ontoggle = () => {
					s.open = Object.assign(s.open || {}, { [id]: box.open });
				};
				return box;
			};
			const note = (text) => el('p', { className: 'ed-note', textContent: text });
			const commands = (st, side) => st.bool('Mode.PlayerCommands.' + side, false);
			// A spawn order: what it is, which way to face, how urgent, and the rest its kind takes; its points are placed on the map from the checklist.
			const orderEditor = (st, k, label) => {
				const o = st.order(k);
				const takes = ORDER_KINDS[o.kind][1];
				const set = (what, fn) => edit(label + ': ' + what, (x) => {
					const now = x.order(k);
					fn(now);
					x.setOrder(k, now);
				});
				const kind = el('select', { className: 'ed-input', disabled: ro }, ...ORDER_KINDS.map(([t], i) => el('option', { value: String(i), textContent: t, selected: o.kind === i })));
				kind.onchange = () => set(ORDER_KINDS[Number(kind.value)][0].toLowerCase(), (now) => {
					now.kind = Number(kind.value);
					const t = ORDER_KINDS[now.kind][1];
					now.points = t === 'none' ? [] : t === 'route' ? now.points : now.points.slice(0, 1);
				});
				const rows = [el('label', { className: 'be-rule' }, el('span', { textContent: 'New units' }), kind)];
				if (takes !== 'none') {
					const facing = el('select', { className: 'ed-input', disabled: ro }, ...[[0, 'Either way'], [-1, 'Left'], [1, 'Right']].map(([v, t]) => el('option', { value: String(v), textContent: t, selected: o.facing === v })));
					facing.onchange = () => set('facing', (now) => {
						now.facing = Number(facing.value);
					});
					const risk = el('input', { type: 'number', className: 'ed-input be-num', min: -100, max: 100, value: o.risk, disabled: ro });
					risk.onchange = () => set('risk', (now) => {
						now.risk = Math.max(-100, Math.min(100, Math.round(Number(risk.value) || 0)));
					});
					rows.push(el('label', { className: 'be-rule' }, el('span', { textContent: 'Facing there' }), facing),
						el('label', { className: 'be-rule', title: 'How urgent, as an order placed by clicks: -100 cautious, 0 normal, 100 the shortest way whatever it takes.' }, el('span', { textContent: 'Risk (-100 to 100)' }), risk));
				}
				if (takes === 'route') {
					const back = el('input', { type: 'checkbox', checked: o.back, disabled: ro });
					back.onchange = () => set('back and forth', (now) => {
						now.back = back.checked;
					});
					rows.push(el('label', { className: 'be-rule be-flag' }, back, el('span', { textContent: 'Back and forth (not a loop)' })));
				}
				if (o.kind === 3) {
					for (const [field, text, max] of [['radius', 'Zone reach (px)', 2000], ['chase', 'Chase out to (px)', 3000], ['roam', 'Roaming (%)', 100]]) {
						const input = el('input', { type: 'number', className: 'ed-input be-num', min: 0, max, value: o[field], disabled: ro });
						input.onchange = () => set(text.toLowerCase(), (now) => {
							now[field] = Math.max(0, Math.min(max, Math.round(Number(input.value) || 0)));
						});
						rows.push(el('label', { className: 'be-rule' }, el('span', { textContent: text }), input));
					}
				}
				if (takes !== 'none') {
					const n = listNow().find((x) => x.key === k);
					rows.push(el('div', { className: 'be-rule' }, el('span', { textContent: o.points.length ? (takes === 'route' ? o.points.length + ' points on its route' : 'Placed') : takes === 'optional' ? 'No point: the nearest objective' : 'Not placed yet' }),
						ro || !n ? null : el('button', { className: 'tb', textContent: takes === 'route' ? '+ Add points' : o.points.length ? 'Move' : 'Place', onclick: () => startPlacing(n) })));
				}
				return rows;
			};
			// A custom battle is played in the Sandbox, a mode in Battle Command: the scenario is a copy of the one that plays it.
			const rebase = (mode) => {
				const base = baseFor(mode);
				if (!base || ro || resolvedClass() === base.node.str('LuaClassName', '')) {
					return;
				}
				a.edit('Play ' + scenario.name + ' in ' + base.name, () => scenario.node.set('CopyOf', a.lib.reference(base.name, base.module, scenario.module)));
			};
			const resolvedClass = () => a.lib.resolve(scenario.node).str('LuaClassName', '');
			// A custom battle's team cards, as the Battle Director's (BattleCard): who comes, how they play, and how they come in.
			const customTeams = (st) => [...Array(SIDES).keys()].map((side) => {
				const team = 'Team' + side + '.';
				const on = st.plays(side);
				const active = el('input', { type: 'checkbox', checked: on, disabled: ro });
				active.onchange = () => edit((active.checked ? 'Make ' : 'Take out ') + SIDE_NAMES[side] + (active.checked ? ' active' : ''), (x) => x.set(team + 'Active', active.checked));
				const now = String(st.raw(team + 'Factions') || '').split(';').filter(Boolean);
				const facText = now.length ? now.map((m) => m.replace(/[.]rte$/i, '')).join(', ') : 'Any faction';
				const row = el('div', { className: 'be-team be-team-custom' + (on ? '' : ' off') }, el('label', { className: 'be-flag' }, active, el('span', { className: 'me-swatch', style: 'background:' + SIDE_COLOURS[side] }), el('b', { textContent: SIDE_NAMES[side] })),
					el('span', { className: 'be-dim', textContent: on ? STYLES[st.num(team + 'Style', 0)] || STYLES[0] : 'Not taking part' }), el('span', { className: 'be-dim be-fac', textContent: facText, title: facText }));
				if (!on) {
					return [row];
				}
				const any = el('input', { type: 'checkbox', checked: !now.length, disabled: ro });
				any.onchange = () => any.checked && edit(SIDE_NAMES[side] + ': any faction', (x) => x.set(team + 'Factions', ''));
				const facList = el('div', { className: 'be-facs' }, el('label', { className: 'be-flag' }, any, el('span', { textContent: 'Any faction' })), ...factions().map((m) => {
					const tick = el('input', { type: 'checkbox', checked: now.includes(m), disabled: ro });
					tick.onchange = () => edit(SIDE_NAMES[side] + (tick.checked ? ': add ' : ': take out ') + m, (x) => {
						const list = String(x.raw(team + 'Factions') || '').split(';').filter(Boolean).filter((f) => f !== m);
						x.set(team + 'Factions', (tick.checked ? list.concat([m]) : list).join(';'));
					});
					return el('label', { className: 'be-flag' }, tick, el('span', { textContent: m.replace(/[.]rte$/i, '') }));
				}));
				const style = st.num(team + 'Style', 0);
				const endless = st.bool(team + 'EndlessMoney', false);
				const onLine = st.bool(team + 'DropOnLine', false);
				const anywhereToo = st.bool(team + 'AppearAnywhere', false);
				const details = el('details', { className: 'be-teamdet', open: !s.open || s.open['card' + side] !== false }, el('summary', { textContent: 'Its card: units, play style, waves, ships, spawn zones' }),
					el('div', { className: 'be-sub' },
						el('div', { className: 'be-label', textContent: 'Factions (tick as many as you like)' }), facList,
						flag(team + 'FavouritesOnly', 'Favourites only', false, 'Only units marked as favourites, of those factions (any, when none are).'),
						flag(team + 'Crabs', 'Spawn crabs', false, 'Crabs among its units: crabs, and the tanks and walkers built on them. Off: infantry and drones only.'),
						flag(team + 'JetpackOnly', 'Jetpacks only', false, 'Only units with a jetpack.'),
						choice(team + 'Style', 'Play style', 0, STYLES.map((t, i) => [i, t]), 'What its units do once in: attack the nearest enemy, hunt the brains, defend a place, patrol, or stand sentry.'),
						...(style === 2 ? [note('Place its defence point on the map (Place on the map, below). Not set: it attacks till it is.'),
							number(team + 'DefendRadius', 'Defend radius (px)', 30, 600, 150), number(team + 'ChaseDistance', 'Chase distance (px)', 0, 1500, 300),
							number(team + 'RoamPercent', 'Roaming (% of them)', 0, 100, 0)] : []),
						el('div', { className: 'be-label', textContent: 'Waves' }),
						flag(team + 'EndlessMoney', 'No money limit', false),
						endless ? null : number(team + 'Budget', 'Budget (oz)', 500, 100000, 5000, 'What it may spend in all.'),
						number(team + 'WaveSize', 'Units per ship', 1, 10, 5),
						number(team + 'UnitLimit', 'Unit limit (0: none)', 0, 200, 0, 'Most units it has in at once, those still in its ships counted.'),
						el('div', { className: 'be-label', textContent: 'Ships' }),
						choice(team + 'Craft', 'Craft', 2, CRAFTS.map((c, i) => [i, c]), 'A dropship, a rocket, or drop crates that fall from the sky, land and let their units out.'),
						choice(team + 'DropOnLine', 'Ships come in', 0, [[0, 'Anywhere'], [1, 'Over a drop line']], 'Over a drop line: draw it on the map (Place on the map, below).'),
						number(team + 'ShipsPerBurst', 'Ships at a time (0: no ships)', 0, 6, 1),
						number(team + 'EverySeconds', 'Every (seconds)', 5, 300, 30),
						flag(team + 'Invincible', 'Ships can\'t be hurt', false, 'Its ships take no harm, and are taken away once they\'ve unloaded and left.'),
						el('div', { className: 'be-label', textContent: 'Where units come in' }),
						flag(team + 'AppearAnywhere', 'Units appear anywhere on the map', false, 'Its units also appear anywhere on the map there is ground to stand on, a lot at a time, rather than only in its spawn zones.'),
						!onLine || anywhereToo ? number(team + 'KeepClear', 'Keep clear of enemies by (px)', 0, 2000, 300, 'Ships coming in anywhere, and units appearing anywhere: how far they keep from enemy units.') : null,
						number(team + 'ZoneUnits', 'Units per zone', 1, 10, 3, 'Units that appear at each spawn zone (or anywhere) each time.'),
						number(team + 'ZoneEverySeconds', 'Every (seconds)', 5, 300, 30)));
				details.ontoggle = () => {
					s.open = Object.assign(s.open || {}, { ['card' + side]: details.open });
				};
				return [row, details];
			}).flat();
			// A custom battle's panel: the mode, the team cards, what to place, how it shows, and with the game.
			const drawCustom = (st, modes, placeRows, why) => {
				const showPick = (k, label) => {
					const pick = el('select', { className: 'ed-input', disabled: ro }, ...[[-1, 'As the player has it'], [1, 'Shown'], [0, 'Hidden']].map(([v, t]) => el('option', { value: String(v), textContent: t, selected: st.show(k) === v })));
					pick.onchange = () => edit(label + ': ' + pick.selectedOptions[0].textContent.toLowerCase(), (x) => x.set('Mode.' + k, Number(pick.value)));
					return el('label', { className: 'be-rule' }, el('span', { textContent: label }), pick);
				};
				panel.replaceChildren(...[
					ro ? el('div', { className: 'ed-banner', textContent: scenario.module + ' is read only here. Make a battle of your own with + New battle to change it.' }) : null,
					section('Mode', modes, note(MODES[0].blurb)),
					section('Teams', note('Tick Active on each team that takes part. Each keeps sending in units as its card says, from the start of the battle till it is stopped.'), ...customTeams(st)),
					section('Place on the map', why ? el('div', { className: 'be-why', textContent: why }) : el('div', { className: 'be-ready', textContent: 'Ready to play.' }), ...placeRows,
						note('Spawn zones, a defence point (Play style: Defend a place) and a drop line (Ships come in: Over a drop line) for each active team. Drag along the map to draw a drop line.')),
					fold('looks', 'How it shows', showPick('ShowSpawnZones', 'Spawn zones'), showPick('ShowInfo', 'Battle info under the score')),
					section('With the game', el('div', { className: 'be-io' },
						el('button', { className: 'tb', textContent: 'Copy from the game\'s Battle tab…', disabled: ro || !scene, title: 'Brings in a setup saved on this map from the Battle tab', onclick: () => importFromGame(key) }),
						el('button', { className: 'tb', textContent: 'Save to the game\'s Battle tab', disabled: !scene, title: 'Saves this battle with the map\'s own (Userdata/BattlePresets), to change it there', onclick: () => exportToGame(key) })),
					note('A custom battle plays in the Sandbox: ▶ Play starts it there, and the game lists it with ' + (scene ? scene.name : 'its map') + '\'s battles in the map browser and the Battle tab.')),
				].filter(Boolean));
			};
			const drawPanel = () => {
				const st = docs.get(key).setup;
				const sp = MODES[st.mode];
				const items = needs(st);
				const why = problem(st);
				const sides = [...Array(SIDES).keys()];
				// the mode
				const modes = el('div', { className: 'be-modes' }, ...MODES.map((m, i) => (m ? el('button', { className: 'be-mode' + (i === st.mode ? ' sel' : ''), title: m.blurb, disabled: ro, onclick: () => {
					s.map.place = null;
					rebase(i);
					edit('Make it ' + m.name, (x) => x.set('Mode.Mode', i));
				} }, el('span', { className: 'be-mode-icon', textContent: m.icon }), el('span', { textContent: m.name })) : null)));
				// the teams
				const who = el('select', { className: 'ed-input', disabled: ro, title: 'Who the player is in this battle: a team they command from above (Battle Command), or none, to watch. More than one (hot-seat): tick "You command this team" under each.' },
					el('option', { value: '-1', textContent: 'Nobody: watch' }), ...sides.map((side) => el('option', { value: String(side), textContent: 'Command ' + SIDE_NAMES[side], selected: commands(st, side) })));
				const commanded = sides.filter((side) => commands(st, side));
				if (!commanded.length) {
					who.value = '-1';
				} else if (commanded.length > 1) {
					who.prepend(el('option', { value: 'many', textContent: commanded.map((x) => SIDE_NAMES[x]).join(' and '), selected: true }));
				}
				who.onchange = () => edit('The player ' + (who.value === '-1' ? 'watches' : 'commands ' + SIDE_NAMES[Number(who.value)]), (x) => {
					for (let side = 0; side < SIDES; side++) {
						x.set('Mode.PlayerCommands.' + side, String(side) === who.value);
					}
				});
				const mods = factions();
				const teamRow = (side) => {
					const on = st.plays(side);
					const off = ro || !on;
					const plays = el('input', { type: 'checkbox', checked: on, disabled: ro });
					plays.onchange = () => edit((plays.checked ? 'Add ' : 'Take out ') + SIDE_NAMES[side], (x) => x.set('Mode.Plays.' + side, plays.checked));
					const size = el('input', { type: 'number', className: 'ed-input be-num', min: 2, max: 100, value: st.num('Mode.TeamSizes.' + side, 16), disabled: off, title: 'Most of its units alive at once' });
					size.onchange = () => edit(SIDE_NAMES[side] + '\'s size', (x) => x.set('Mode.TeamSizes.' + side, Math.max(2, Math.min(100, Math.round(Number(size.value) || 16)))));
					const yours = commands(st, side);
					const ai = yours ? el('span', { className: 'be-dim', textContent: 'You command it' }) : el('select', { className: 'ed-input', disabled: off, title: 'Whether an AI commander leads it, splitting its units between the objectives as a player would' },
						el('option', { value: '1', textContent: 'AI commander', selected: st.bool('Mode.Commander.' + side, true) }), el('option', { value: '0', textContent: 'No commander', selected: !st.bool('Mode.Commander.' + side, true) }));
					ai.onchange = () => edit(SIDE_NAMES[side] + '\'s commander', (x) => x.set('Mode.Commander.' + side, ai.value === '1'));
					const now = String(st.raw('Team' + side + '.Factions') || '').split(';').filter(Boolean);
					const facText = now.length ? now.map((m) => m.replace(/[.]rte$/i, '')).join(', ') : 'Any faction';
					const row = el('div', { className: 'be-team' + (on ? '' : ' off') }, el('label', { className: 'be-flag' }, plays, el('span', { className: 'me-swatch', style: 'background:' + SIDE_COLOURS[side] }), el('b', { textContent: SIDE_NAMES[side] })), size,
						el('span', { className: 'be-dim be-fac', textContent: facText, title: facText }), ai);
					if (!on) {
						return [row];
					}
					// its units, how they go about it, and (yours) what each new one is ordered to do
					const any = el('input', { type: 'checkbox', checked: !now.length, disabled: ro });
					any.onchange = () => any.checked && edit(SIDE_NAMES[side] + ': any faction', (x) => x.set('Team' + side + '.Factions', ''));
					const facList = el('div', { className: 'be-facs' }, el('label', { className: 'be-flag' }, any, el('span', { textContent: 'Any faction' })), ...mods.map((m) => {
						const tick = el('input', { type: 'checkbox', checked: now.includes(m), disabled: ro });
						tick.onchange = () => edit(SIDE_NAMES[side] + (tick.checked ? ': add ' : ': take out ') + m, (x) => {
							const list = String(x.raw('Team' + side + '.Factions') || '').split(';').filter(Boolean).filter((f) => f !== m);
							x.set('Team' + side + '.Factions', (tick.checked ? list.concat([m]) : list).join(';'));
						});
						return el('label', { className: 'be-flag' }, tick, el('span', { textContent: m.replace(/[.]rte$/i, '') }));
					}));
					const team = 'Team' + side + '.';
					const risk = (k, label, def) => {
						const input = el('input', { type: 'number', className: 'ed-input be-num', min: -100, max: 100, value: st.num('Mode.' + k + '.' + side, def), disabled: ro });
						input.onchange = () => edit(SIDE_NAMES[side] + ': ' + label.toLowerCase(), (x) => {
							const v = Math.max(-100, Math.min(100, Math.round(Number(input.value) || 0)));
							x.set('Mode.' + k + '.' + side, v);
							// (the most never under the least, as the game keeps them)
							if (k === 'RiskMin' && x.num('Mode.RiskMax.' + side, 0) < v) {
								x.set('Mode.RiskMax.' + side, v);
							} else if (k === 'RiskMax' && x.num('Mode.RiskMin.' + side, 0) > v) {
								x.set('Mode.RiskMin.' + side, v);
							}
						});
						return el('label', { className: 'be-rule', title: 'Recklessness with the mode\'s orders: -100 cautious (walking round jumps, waiting for a good shot), 0 normal, 100 a suicide mission. Each unit gets a risk between the least and the most, the strongest toward the most.' }, el('span', { textContent: label }), input);
					};
					const zoneOrders = yours ? st.shapes('Mode.SpawnZones.' + side).map((_, i) => {
						const k = 'Mode.ZoneSpawnOrders.' + side + '.' + i;
						const own = st.order(k).set;
						const pick = el('select', { className: 'ed-input', disabled: ro }, el('option', { value: '0', textContent: 'The team\'s order', selected: !own }), el('option', { value: '1', textContent: 'Its own order', selected: own }));
						pick.onchange = () => edit(SIDE_NAMES[side] + ' spawn ' + (i + 1) + (pick.value === '1' ? ': its own order' : ': the team\'s order'), (x) => x.setOrder(k, Object.assign(x.order(k), { set: pick.value === '1' })));
						return [el('label', { className: 'be-rule' }, el('span', { textContent: 'Units from spawn zone ' + (i + 1) }), pick), ...(own ? [el('div', { className: 'be-sub' }, ...orderEditor(st, k, SIDE_NAMES[side] + ' spawn ' + (i + 1) + '\'s order'))] : [])];
					}).flat() : [];
					const details = el('details', { className: 'be-teamdet', open: !!(s.open && s.open['team' + side]) }, el('summary', { textContent: 'Units, risk' + (yours ? ', spawn orders' : '') }),
						el('div', { className: 'be-sub' },
							el('div', { className: 'be-label', textContent: 'Factions (tick as many as you like)' }), facList,
							flag(team + 'FavouritesOnly', 'Favourites only', false, 'Only units marked as favourites, of those factions (any, when none are).'),
							flag(team + 'Crabs', 'Spawn crabs', false, 'Crabs among its units: crabs, and the tanks and walkers built on them. Off: infantry and drones only.'),
							flag(team + 'JetpackOnly', 'Jetpacks only', false, 'Only units with a jetpack.'),
							risk('RiskMin', 'Least risk', 0), risk('RiskMax', 'Most risk', 0),
							number('RushPercent.' + side, 'Rush the objective (% of its units)', 0, 100, 30, 'Its units that make a beeline for the objective, shooting as they go, not taking cover or stopping to fight. Guards never rush.'),
							flag('PlayerCommands.' + side, 'You command this team', false, 'Its units come in with no job from the mode, for the player to command (Battle Command). Tick two teams for hot-seat.'),
							...(yours ? [el('div', { className: 'be-label', textContent: 'Spawn order: what each new unit does as it comes in' }), ...orderEditor(st, 'Mode.SpawnOrders.' + side, SIDE_NAMES[side] + '\'s spawn order'), ...zoneOrders] : [])));
					details.ontoggle = () => {
						s.open = Object.assign(s.open || {}, { ['team' + side]: details.open });
					};
					return [row, details];
				};
				const role = sp.role ? (() => {
					const pick = el('select', { className: 'ed-input', disabled: ro }, ...sides.map((side) => el('option', { value: String(side), textContent: SIDE_NAMES[side], selected: st.role(sp.role) === side })));
					pick.onchange = () => edit(sp.role.label + ': ' + SIDE_NAMES[Number(pick.value)], (x) => x.set('Mode.' + sp.role.key, Number(pick.value)));
					return el('label', { className: 'ed-check' }, sp.role.label, pick);
				})() : null;
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
					if ((n.many && shapes.length > 1) || (n.ordered && shapes.length)) {
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
				if (sp.custom) {
					drawCustom(st, modes, placeRows, why);
					return;
				}
				// mirror: a team's spawn zones and point copied to another, flipped across the map
				const mirrorFrom = el('select', { className: 'ed-input' }, ...sides.filter((x) => st.plays(x)).map((x) => el('option', { value: String(x), textContent: SIDE_NAMES[x] })));
				const mirrorTo = el('select', { className: 'ed-input' }, ...sides.filter((x) => st.plays(x)).map((x, i) => el('option', { value: String(x), textContent: SIDE_NAMES[x], selected: i === 1 })));
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
				// where units come in
				const byShip = st.bool('Mode.ByShip', false);
				const anywhere = st.bool('Mode.Anywhere', false);
				const arrive = el('select', { className: 'ed-input', disabled: ro }, ...ARRIVALS.map(([t, ship, wide], i) => el('option', { value: String(i), textContent: t, selected: byShip === ship && anywhere === wide })));
				arrive.onchange = () => edit('Units: ' + ARRIVALS[Number(arrive.value)][0].toLowerCase(), (x) => {
					x.set('Mode.ByShip', ARRIVALS[Number(arrive.value)][1]);
					x.set('Mode.Anywhere', ARRIVALS[Number(arrive.value)][2]);
				});
				// AI commanders, as the Battle tab's CommanderPanel shows them for this mode
				const anyAI = sides.some((side) => st.plays(side) && !commands(st, side) && st.bool('Mode.Commander.' + side, true));
				const aiRules = [choice('CommanderLevel', 'Difficulty', 1, LEVELS, 'Easy looks at the battle every 3 s and sends few units; normal every second; hard twice a second, sending the most.')];
				if (![2, 3, 7, 11].includes(st.mode)) {
					aiRules.push(flag('CommanderSquads', 'Squads', true, 'On normal and hard: its attackers (not its guards) go in squads that follow a leader.'));
				}
				if ([2, 3].includes(st.mode)) {
					aiRules.push(number('CommanderReserve', 'Held on the next objective (%)', 0, 60, 30, 'The share of its units put on the next objective (or sent ahead to the next hill), ready for when the one in play goes.'));
				}
				if (st.mode === 3) {
					aiRules.push(number('CommanderFallBack', 'Defenders fall back when taken (%)', 10, 100, 60, 'Once the attackers have got this far taking the objective in play, every defender falls back to hold the next one.'));
				}
				// Battle Command: for the team the player commands
				const fieldBrain = st.bool('Mode.FieldBrain', false);
				const hotSeat = st.bool('Mode.HotSeat', false);
				const pickArmy = st.bool('Mode.PickArmy', false);
				const armyRows = pickArmy ? [number('ArmyPoints', 'Army points', 500, 20000, 3000, 'What the whole army may cost. Over it, the dearest units are left out.'),
					...ARMY_CARDS.map(([name, def], i) => number('Army.' + i, name + 's', 0, 99, def))] : [];
				// pickups
				const pickups = st.bool('Mode.Pickups', false);
				const places = st.num('Mode.PickupPlaces', 1);
				const placeBox = (bit, label) => {
					const input = el('input', { type: 'checkbox', checked: !!(places & bit), disabled: ro });
					input.onchange = () => edit('Pickups ' + label.toLowerCase(), (x) => x.set('Mode.PickupPlaces', (input.checked ? places | bit : places & ~bit) || 1));
					return el('label', { className: 'be-rule be-flag' }, input, el('span', { textContent: label }));
				};
				const kinds = st.num('Mode.PickupKinds', 0x7f);
				const kindBox = (bit, label) => {
					const input = el('input', { type: 'checkbox', checked: !!(kinds & (1 << bit)), disabled: ro });
					input.onchange = () => edit('Pickups: ' + (input.checked ? 'with ' : 'no ') + label.toLowerCase(), (x) => x.set('Mode.PickupKinds', input.checked ? kinds | (1 << bit) : kinds & ~(1 << bit)));
					return el('label', { className: 'be-flag' }, input, el('span', { textContent: label }));
				};
				// how it shows: the battle's own, or the player's
				const showPick = (k, label) => {
					const pick = el('select', { className: 'ed-input', disabled: ro }, ...[[-1, 'As the player has it'], [1, 'Shown'], [0, 'Hidden']].map(([v, t]) => el('option', { value: String(v), textContent: t, selected: st.show(k) === v })));
					pick.onchange = () => edit(label + ': ' + pick.selectedOptions[0].textContent.toLowerCase(), (x) => x.set('Mode.' + k, Number(pick.value)));
					return el('label', { className: 'be-rule' }, el('span', { textContent: label }), pick);
				};
				const lookPick = el('select', { className: 'ed-input', disabled: ro || st.show('ShowObjectives') === 0 }, el('option', { value: '-1', textContent: 'As the player has it', selected: st.show('ObjectiveLook') === -1 }),
					...LOOKS.map((t, i) => el('option', { value: String(i), textContent: t, selected: st.show('ObjectiveLook') === i })));
				lookPick.onchange = () => edit('Objective look: ' + lookPick.selectedOptions[0].textContent.toLowerCase(), (x) => x.set('Mode.ObjectiveLook', Number(lookPick.value)));
				const ownLook = sp.look ? { ground: 'a glowing line along the top of the ground', glow: 'the ground and buildings in it glowing', outline: 'a glowing line round it' }[sp.look] : null;
				panel.replaceChildren(...[
					ro ? el('div', { className: 'ed-banner', textContent: scenario.module + ' is read only here. Make a battle of your own with + New battle to change it.' }) : null,
					section('Mode', modes, note(sp.blurb)),
					section('Teams', el('div', { className: 'be-teamhead' }, el('label', { className: 'ed-check' }, 'The player', who), role), ...sides.map(teamRow).flat()),
					section('Place on the map', why ? el('div', { className: 'be-why', textContent: why }) : el('div', { className: 'be-ready', textContent: 'Ready to play.' }), ...placeRows,
						el('div', { className: 'be-mirror' }, el('span', { textContent: 'Mirror' }), mirrorFrom, el('span', { textContent: 'onto' }), mirrorTo, mirror),
						note('Place, then click (points) or drag a box (zones) on the map; right-click the map to place something there. Drag to move, drag a corner to reshape, Alt-click an edge to add a corner, Shift-click a corner to take it out, Delete takes out what is picked.')),
					section('Rules', ...sp.rules.map(rule), ...ringRules),
					section('Where units come in', el('label', { className: 'be-rule' }, el('span', { textContent: 'Units' }), arrive),
						anywhere ? number('KeepClear', 'Coming in anywhere: keep clear of enemies by (px)', 0, 2000, 300) : null,
						...sides.filter((side) => st.plays(side)).map((side) => choice('Team' + side + '.Craft', SIDE_NAMES[side] + ' comes in by', 2, CRAFTS.map((c, i) => [i, c]), 'With "Ships over their base" or "Ships anywhere": a dropship, a rocket, or drop crates that fall from the sky, land and let their units out.')),
						byShip ? null : note('Each team\'s craft is used with "Ships over their base" or "Ships anywhere", and for reinforcements called in.')),
					fold('respawn', 'Respawns and routes', ...SHARED_RULES.map(rule)),
					anyAI ? fold('ai', 'AI commanders', ...aiRules) : null,
					fold('command', 'Battle Command', commanded.length ? null : note('For a team the player commands (The player, above).'),
						flag('FogOfWar', 'Fog of war', false, 'You see only the enemies your team\'s units can see.'),
						flag('FieldBrain', 'Your brain on the field', false, 'Your brain is at your base: if it dies you lose, and orders only reach units within its range.'),
						fieldBrain ? number('FieldBrainRange', 'Command range (px)', 300, 3000, 900) : null,
						flag('HotSeat', 'Hot-seat: two players take turns', false, 'Two players on one screen, each commanding one of the teams ticked "You command this team", in turn.'),
						hotSeat ? number('TurnSeconds', 'Seconds a turn', 5, 180, 30) : null,
						hotSeat && commanded.length < 2 ? note('Tick "You command this team" under a second team (its Units, in Teams).') : null,
						flag('PickArmy', 'Pick your army', false, 'The team you command fields the army picked here: its units come from that roster till it is used up.'), ...armyRows),
					fold('pickups', 'Pickups', flag('Pickups', 'Pickups come in on the map', false, 'Powerups (an energy shield, a stim, a spell scroll...) now and then, for the first unit to reach each.'),
						...(pickups ? [...PICKUP_RULES.map(rule), el('div', { className: 'be-label', textContent: 'Which kinds' }), el('div', { className: 'be-facs' }, ...PICKUP_KINDS.map((k, i) => kindBox(i, k))),
							el('div', { className: 'be-label', textContent: 'Where they come in' }), placeBox(1, 'Anywhere'), placeBox(2, 'At pickup spots'), placeBox(4, 'In pickup zones')] : [])),
					fold('looks', 'How it shows', ...SHOWS.map(([k, label]) => showPick(k, label)), el('label', { className: 'be-rule' }, el('span', { textContent: 'Objective look' }), lookPick),
						note('Saved with the battle, so it shows its way whoever plays it; "As the player has it" leaves it to their own Battle tab settings. ' + (ownLook ? 'This mode\'s own look: ' + ownLook + '. ' : '') + 'The map here shows each look as the game will.')),
					section('With the game', el('div', { className: 'be-io' },
						el('button', { className: 'tb', textContent: 'Copy from the game\'s Battle tab…', disabled: ro || !scene, title: 'Brings in a setup saved on this map from the Battle tab (in the Sandbox or Battle Command)', onclick: () => importFromGame(key) }),
						el('button', { className: 'tb', textContent: 'Save to the game\'s Battle tab', disabled: !scene, title: 'Saves this battle with the map\'s own (Userdata/BattlePresets), to change it there', onclick: () => exportToGame(key) })),
					note('The game lists this battle with ' + (scene ? scene.name : 'its map') + '\'s battles too: in the map browser (Sandbox and Battle Command) and the Sandbox\'s Battle tab, to play there.')),
				].filter(Boolean));
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
			// (a custom battle plays in the Sandbox, a mode in Battle Command)
			const base = baseFor(Number(sec.lines.get('Mode.Mode')) === 0 ? 0 : 1);
			if (base && !locked(scenario) && app().lib.resolve(scenario.node).str('LuaClassName', '') !== base.node.str('LuaClassName', '')) {
				app().edit('Play ' + scenario.name + ' in ' + base.name, () => scenario.node.set('CopyOf', app().lib.reference(base.name, base.module, scenario.module)));
			}
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
				const on = baseFor(Number(mode.value)) || base; // (a custom battle: a copy of the Sandbox)
				const doc = a.lib.docFor(path) || (await a.lib.newDoc(path));
				a.hookDoc(doc);
				a.hookDoc(a.lib.docFor(module + '/Index.ini'));
				a.edit('New battle ' + n, () => {
					const block = doc.addTopLevel('AddActivity', on.cls);
					block.add('CopyOf', a.lib.reference(on.name, on.module, module));
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

	MS.battleEditor = { open, openByKey, isBattle, newBattle, MODES, parse, serialise, Setup, needs, problem, things, parseOrder, fmtOrder };
})();
