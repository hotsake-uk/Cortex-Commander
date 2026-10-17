// Cortex Mod Studio: the Inspector panel (beside Console, Problems, Watch and Profiler in Script mode), for looking inside a game started
// from Studio while it runs. While the panel is open Studio writes ModStudio/Inspect.ini (which actor to show, whether to pause, a step
// count) about once a second; the game (ModStudioPreview::UpdateFrame) answers with ModStudio/World.ini four times a second: every actor,
// and the selected one's values, inventory and parts. The selected actor is ringed in the game. Changes (health, team, position, kill,
// take control) are Lua run in the game through the console's Run bridge, so they show in the console too.
'use strict';
window.MS = window.MS || {};

(function () {
	const $ = (id) => document.getElementById(id);
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

	const REQUEST = 'ModStudio/Inspect.ini';
	const WORLD = 'ModStudio/World.ini';
	const stored = (key, fallback) => {
		try {
			const v = localStorage.getItem(key);
			return v === null ? fallback : v === '1';
		} catch (e) {
			return fallback;
		}
	};
	const store = (key, on) => {
		try {
			localStorage.setItem(key, on ? '1' : '0');
		} catch (e) {
			// for this visit only
		}
	};
	const I = {
		timer: 0, selected: 0, camera: 0, cameraSince: 0, paused: false, steps: 0, lastRequest: 0, requestText: '', world: null, modified: 0, filter: '', team: 'all',
		follow: stored('ms.inspectorFollow', true), // keep the unit you control selected, across switching bodies
		seen: new Map(), // id -> the actor as last seen, to tell what arrived and what left
		arrived: new Map(), // id -> when it arrived, to mark it new for a few seconds
		events: [], // {time, text, kind}, newest first
		lastLive: 0,
		hist: new Map(), // id -> { runs: [{t, b}] what the unit's AI did, newest last; totals: Map behaviour -> ms; target }
		damage: [], // {t, team, amount}: health lost, for the battle summary
		lastTrack: 0,
		view: 'actors', // actors | combat | charts
		hitSeq: 0, // the newest hit already logged
		hits: [], // {time, shooterId, shooterName, victimName, team, weapon, dmg}, newest first
		bySource: new Map(), // weapon -> {hits, damage}
		byShooter: new Map(), // shooter name -> {hits, damage, id, team}
		series: [], // {t, teams: Map team -> {health, units, dmg}} once a second
		lastSample: 0,
		sampleDamage: new Map(), // team -> damage since the last sample
	};
	const AUTO_KEY = 'ms.inspectorAuto'; // open the Inspector when Test in game or Sandbox starts from Studio
	const PENDING_KEY = 'ms.inspectorPending'; // set by a launch, taken by the page that loads after it

	function parse(text) {
		const w = { actors: [], hits: [], mind: new Map(), state: [], values: [], inventory: [], parts: [], scripts: [], selected: 0, controlled: 0, following: 0, camera: null, paused: false };
		for (const line of text.split(/\r?\n/)) {
			const at = line.indexOf(' = ');
			if (at < 0 || line.startsWith('//')) {
				continue;
			}
			const key = line.slice(0, at);
			const v = line.slice(at + 3);
			const f = v.split('\t');
			if (key === 'Actor') {
				w.actors.push({ id: Number(f[0]), preset: f[1], cls: f[2], team: Number(f[3]), x: Number(f[4]), y: Number(f[5]), health: Number(f[6]), max: Number(f[7]), status: f[8] });
			} else if (key === 'Hit') {
				w.hits.push({ seq: Number(f[0]), victim: Number(f[1]), shooter: Number(f[2]), team: Number(f[3]), weapon: f[4] || '', projectile: f[5] || '' });
			} else if (key === 'Mind') {
				w.mind.set(Number(f[0]), { behavior: f[1], target: Number(f[2]), seen: f[3] === '1', why: f[4] || '', leader: Number(f[5]) || 0 });
			} else if (key === 'State') {
				w.state.push([f[0], f[1]]);
			} else if (key === 'Value') {
				w.values.push([f[0], f.slice(1).join('\t')]);
			} else if (key === 'Script') {
				w.scripts.push(v);
			} else if (key === 'Inventory') {
				w.inventory.push(v);
			} else if (key === 'Part') {
				w.parts.push({ preset: f[0], cls: f[1], wounds: Number(f[2]), limit: Number(f[3]) });
			} else if (key === 'Selected') {
				w.selected = Number(v);
			} else if (key === 'Controlled') {
				w.controlled = Number(v);
			} else if (key === 'Following') {
				w.following = Number(v);
			} else if (key === 'Camera') {
				w.camera = { x: Number(f[0]), y: Number(f[1]) };
			} else if (key === 'Paused') {
				w.paused = v === '1';
			} else if (key === 'PausedByStudio') {
				w.pausedByStudio = v === '1';
			}
		}
		return w;
	}

	// The request, written when it changes and every second as a heartbeat (the game stops inspecting, and lets go of a pause, when it
	// hasn't been written for a few seconds).
	async function sendRequest(force) {
		const text = '// Written by Cortex Mod Studio\'s Inspector while it is open.\nSelected = ' + I.selected + '\nFollow = ' + I.camera + '\nPause = ' + (I.paused ? 1 : 0) + '\nStep = ' + I.steps + '\n';
		if (!force && text === I.requestText && Date.now() - I.lastRequest < 1000) {
			return;
		}
		I.requestText = text;
		I.lastRequest = Date.now();
		try {
			await app().fs.writeDisk(REQUEST, text);
		} catch (e) {
			// next time
		}
	}

	async function poll() {
		clearTimeout(I.timer);
		if (MS.ide.panel !== 'inspector' || MS.ide.mode !== 'deep' || !app().fs || !app().fs.server) {
			return;
		}
		await sendRequest(false);
		try {
			const response = await app().fs.api('GET', 'file', WORLD);
			if (response) {
				const modified = Number(response.headers.get('X-Last-Modified')) || 0;
				if (modified !== I.modified) {
					I.modified = modified;
					I.world = parse(await response.text());
					trackDamage(I.world);
					noteChanges(I.world);
					trackMinds(I.world);
					sampleSeries(I.world);
					render();
				}
			}
		} catch (e) {
			// no game yet
		}
		// The panel only changes by itself when the game goes live or quiet.
		if (isLive() !== I.shownLive) {
			render();
		}
		I.timer = setTimeout(poll, 300);
	}

	// What arrived and what left since the last look, and following the unit you control.
	function noteChanges(w) {
		const now = Date.now();
		const clock = new Date().toLocaleTimeString(undefined, { hour: '2-digit', minute: '2-digit', second: '2-digit' });
		const event = (text, kind) => {
			I.events.unshift({ time: clock, text, kind });
			I.events.length = Math.min(I.events.length, 60);
		};
		if (!I.lastLive || now - I.lastLive > 5000) {
			// A game (re)started, or the panel opened on one: what's there now is the start, not arrivals.
			I.seen.clear();
			I.arrived.clear();
			event('Watching the game: ' + w.actors.length + ' actor' + (w.actors.length === 1 ? '' : 's') + ' in it', 'info');
			for (const a of w.actors) {
				I.seen.set(a.id, a);
			}
		} else {
			const ids = new Set(w.actors.map((a) => a.id));
			for (const a of w.actors) {
				if (!I.seen.has(a.id)) {
					I.arrived.set(a.id, now);
					event('Arrived: ' + a.preset + ' (' + teamName(a.team) + ')' + (a.id === w.controlled ? ', you' : ''), 'in');
				}
				I.seen.set(a.id, a);
			}
			for (const [id, a] of I.seen) {
				if (!ids.has(id)) {
					I.seen.delete(id);
					I.arrived.delete(id);
					const died = a.health <= 0 || a.status === 'Dying' || a.status === 'Dead';
					event((died ? 'Died: ' : 'Left: ') + a.preset + ' (' + teamName(a.team) + ')', died ? 'died' : 'out');
				}
			}
		}
		I.lastLive = now;
		if (I.follow && w.controlled && w.controlled !== I.selected) {
			const you = w.actors.find((a) => a.id === w.controlled);
			if (you && I.selected) {
				event('You took control of ' + you.preset, 'info');
			}
			I.selected = w.controlled;
			sendRequest(true);
		}
	}

	const teamName = (t) => (t >= 0 ? 'team ' + (t + 1) : 'no team');

	// ---- combat and minds ----
	const HIST_MS = 120000;
	const DAMAGE_MS = 30000;
	const clockNow = () => new Date().toLocaleTimeString(undefined, { hour: '2-digit', minute: '2-digit', second: '2-digit' });
	function logEvent(text, kind) {
		I.events.unshift({ time: clockNow(), text, kind });
		I.events.length = Math.min(I.events.length, 60);
	}

	// Health lost since the last look (before noteChanges updates what was seen), for the battle summary, the combat log and the charts.
	// Each unit's loss is shared among the hits on it since the last look; a loss with no hit (a fall, an explosion's blast) is "other".
	function trackDamage(w) {
		const now = Date.now();
		const fresh = I.lastLive && now - I.lastLive <= 5000;
		const newHits = w.hits.filter((h) => h.seq > I.hitSeq);
		if (w.hits.length) {
			I.hitSeq = Math.max(I.hitSeq, ...w.hits.map((h) => h.seq));
		}
		if (!fresh) {
			I.damage = [];
			return;
		}
		const nameOf = (id) => {
			const a = w.actors.find((x) => x.id === id) || (I.seen.get(id));
			return a ? a.preset : '';
		};
		const note = (victim, shooter, shooterTeam, weapon, amount) => {
			const key = weapon || 'other (falls, blasts, fire)';
			const src = I.bySource.get(key) || { hits: 0, damage: 0 };
			src.hits++;
			src.damage += amount;
			I.bySource.set(key, src);
			const who = shooter ? nameOf(shooter) + ' #' + shooter : (shooterTeam >= 0 ? teamName(shooterTeam) : 'unknown');
			const sh = I.byShooter.get(who) || { hits: 0, damage: 0, id: shooter, team: shooterTeam };
			sh.hits++;
			sh.damage += amount;
			I.byShooter.set(who, sh);
			I.hits.unshift({ time: clockNow(), shooter, shooterName: who, victimName: victim.preset + ' #' + victim.id, team: shooterTeam, weapon: key, dmg: amount });
			I.hits.length = Math.min(I.hits.length, 80);
		};
		for (const a of w.actors) {
			const before = I.seen.get(a.id);
			if (!before) {
				continue;
			}
			const lost = before.health - a.health;
			const mine = newHits.filter((h) => h.victim === a.id);
			if (lost >= 1 && a.health > 0) {
				I.damage.push({ t: now, team: a.team, amount: lost });
				I.sampleDamage.set(a.team, (I.sampleDamage.get(a.team) || 0) + lost);
				if (mine.length) {
					for (const h of mine) {
						note(a, h.shooter, h.team, h.weapon || cleanShot(h.projectile), lost / mine.length);
					}
				} else {
					note(a, 0, -1, '', lost);
				}
				if (lost >= 5 || (a.id === I.selected && lost >= 2)) {
					const by = mine.length ? ' by ' + (mine[0].weapon || cleanShot(mine[0].projectile)) : '';
					logEvent('Hurt: ' + a.preset + ' (' + teamName(a.team) + ') lost ' + Math.round(lost) + by + ', ' + Math.round(a.health) + ' left', 'hurt');
				}
			}
		}
		I.damage = I.damage.filter((d) => now - d.t < DAMAGE_MS);
	}

	// A shot's preset reads "Particle SMG" or "Particle SMG Tracer": the gun is what is left.
	const cleanShot = (name) => name.replace(/^Particle /, '').replace(/ Tracer$/, '') || name;

	// Once a second: each team's health, units and damage, for the charts.
	function sampleSeries(w) {
		const now = Date.now();
		if (now - I.lastSample < 1000) {
			return;
		}
		const dt = I.lastSample ? (now - I.lastSample) / 1000 : 1;
		I.lastSample = now;
		const teams = new Map();
		for (const a of w.actors) {
			const t = teams.get(a.team) || { health: 0, max: 0, units: 0, dmg: 0 };
			t.health += Math.max(0, a.health);
			t.max += a.max;
			t.units++;
			teams.set(a.team, t);
		}
		for (const [team, amount] of I.sampleDamage) {
			if (teams.has(team)) {
				teams.get(team).dmg = amount / dt;
			}
		}
		I.sampleDamage.clear();
		I.series.push({ t: now, teams });
		if (I.series.length > 600) {
			I.series.shift();
		}
	}

	// What each AI unit is doing, as a history: when its behaviour changed, how long it has spent in each, and who it has been after.
	function trackMinds(w) {
		const now = Date.now();
		const dt = I.lastTrack && now - I.lastTrack < 1500 ? now - I.lastTrack : 0;
		I.lastTrack = now;
		const name = (id) => {
			const a = w.actors.find((x) => x.id === id);
			return a ? a.preset + ' #' + id : '#' + id;
		};
		for (const [id, m] of w.mind) {
			let h = I.hist.get(id);
			if (!h) {
				h = { runs: [], totals: new Map(), target: 0 };
				I.hist.set(id, h);
			}
			const last = h.runs[h.runs.length - 1];
			if (!last || last.b !== m.behavior) {
				h.runs.push({ t: now, b: m.behavior });
				if (id === I.selected && last) {
					logEvent(name(id) + ': ' + last.b + ' → ' + m.behavior, 'ai');
				}
			}
			h.totals.set(m.behavior, (h.totals.get(m.behavior) || 0) + dt);
			if (m.target !== h.target) {
				if (id === I.selected) {
					logEvent(m.target ? name(id) + ' ' + (m.seen ? 'sees' : 'is after') + ' ' + name(m.target) : name(id) + ' lost its target', 'ai');
				}
				h.target = m.target;
			}
			while (h.runs.length > 1 && now - h.runs[1].t > HIST_MS) {
				h.runs.shift();
			}
		}
		const ids = new Set(w.actors.map((a) => a.id));
		for (const id of [...I.hist.keys()]) {
			if (!ids.has(id)) {
				I.hist.delete(id);
			}
		}
	}

	// One colour a behaviour, the same each time.
	function behaviorColor(name) {
		let hash = 0;
		for (let i = 0; i < name.length; i++) {
			hash = (hash * 31 + name.charCodeAt(i)) % 360;
		}
		return 'hsl(' + hash + ', 55%, 52%)';
	}

	// The battle at a glance: each team's units, health, health lost in the last 30 s, and what they are all doing.
	function updateOverview(w) {
		const now = Date.now();
		const teams = new Map();
		for (const a of w.actors) {
			let t = teams.get(a.team);
			if (!t) {
				t = { units: 0, health: 0, max: 0, lost: 0, modes: new Map() };
				teams.set(a.team, t);
			}
			t.units++;
			t.health += Math.max(0, a.health);
			t.max += a.max;
			const m = w.mind.get(a.id);
			if (m) {
				t.modes.set(m.behavior, (t.modes.get(m.behavior) || 0) + 1);
			}
		}
		for (const d of I.damage) {
			if (teams.has(d.team) && now - d.t < DAMAGE_MS) {
				teams.get(d.team).lost += d.amount;
			}
		}
		const rows = [...teams].sort((x, y) => x[0] - y[0]).map(([team, t]) => ({ team, ...t }));
		const key = JSON.stringify(rows.map((r) => [r.team, r.units, Math.round(r.health), Math.round(r.lost), [...r.modes]]));
		if (key === V.overviewKey) {
			return;
		}
		V.overviewKey = key;
		V.overview.replaceChildren(...rows.map((r) => el('div', { className: 'insp-team' },
			el('div', { className: 'insp-team-head' }, el('b', { textContent: teamName(r.team) }), el('span', { textContent: r.units + ' unit' + (r.units === 1 ? '' : 's') }),
				el('span', { textContent: r.max ? Math.round((100 * r.health) / r.max) + '% health' : '' }), el('span', { className: r.lost > 0 ? 'insp-lost' : '', title: 'Health lost in the last 30 seconds', textContent: r.lost > 0 ? '−' + Math.round(r.lost) + ' in 30 s' : 'no damage' })),
			r.modes.size ? el('div', { className: 'insp-modes' }, ...[...r.modes].sort((x, y) => y[1] - x[1]).map(([b, n]) => el('span', { className: 'insp-mode', style: 'border-color:' + behaviorColor(b), title: n + ' unit' + (n === 1 ? '' : 's') + ' doing ' + b, textContent: n + ' ' + b }))) : null)));
		V.overview.hidden = rows.length === 0;
	}

	const TEAM_COLORS = ['#4fa3ff', '#ff6b5f', '#5fd07a', '#e5c24a', '#b283ff'];
	const teamColor = (t) => (t >= 0 ? TEAM_COLORS[t % TEAM_COLORS.length] : '#888');

	// Who hit whom: damage by weapon, by shooter, and the latest hits.
	function updateCombat(w) {
		const key = JSON.stringify([I.hits.length && I.hits[0], I.bySource.size, I.byShooter.size, [...I.bySource.values()].map((x) => Math.round(x.damage))]);
		if (key === V.combatKey) {
			return;
		}
		V.combatKey = key;
		const bar = (value, max) => el('span', { className: 'insp-bar' }, el('i', { style: 'width:' + Math.round((100 * value) / (max || 1)) + '%' }));
		const table = (title, map, withLink) => {
			const rows = [...map].sort((x, y) => y[1].damage - x[1].damage).slice(0, 12);
			const max = rows.length ? rows[0][1].damage : 1;
			return el('div', {}, el('div', { className: 'prof-list-head', textContent: title }),
				rows.length ? el('table', { className: 'watch-table' }, el('tr', {}, el('th', { textContent: 'Who' }), el('th', { textContent: 'Hits' }), el('th', { textContent: 'Damage' }), el('th')),
					...rows.map(([name, r]) => el('tr', {}, el('td', {}, withLink && r.id ? el('button', { className: 'insp-link', textContent: name, onclick: () => { I.view = 'actors'; select(r.id); } }) : name), el('td', { textContent: r.hits }), el('td', { textContent: Math.round(r.damage) }), el('td', {}, bar(r.damage, max)))))
					: el('p', { className: 'pempty', textContent: 'Nothing yet. Damage shows here as units get hurt.' }));
		};
		V.combat.replaceChildren(
			el('div', { className: 'insp-pane-head' }, el('b', { textContent: 'Combat' }), el('span', { className: 'spacer' }), el('button', { className: 'ico-btn', textContent: 'Clear', onclick: () => { I.bySource.clear(); I.byShooter.clear(); I.hits = []; V.combatKey = ''; render(); } })),
			table('Damage by weapon', I.bySource, false),
			table('Damage by shooter', I.byShooter, true),
			el('div', {}, el('div', { className: 'prof-list-head', textContent: 'Latest hits' }),
				...I.hits.slice(0, 30).map((h) => el('div', { className: 'insp-event hurt' }, el('small', { textContent: h.time }), h.shooterName + ' → ' + h.victimName + ' · ' + h.weapon + ' · ' + Math.round(h.dmg)))));
	}

	// Health, damage and units over time, one line a team.
	function drawChart(cv, title, pick, unit, fixedMax) {
		const width = cv.clientWidth || 400;
		if (cv.width !== width) {
			cv.width = width;
		}
		const h = cv.height;
		const ctx = cv.getContext('2d');
		ctx.clearRect(0, 0, width, h);
		const series = I.series;
		const teams = new Set();
		series.forEach((p) => p.teams.forEach((_, t) => teams.add(t)));
		let max = fixedMax || 1;
		if (!fixedMax) {
			series.forEach((p) => p.teams.forEach((t) => { max = Math.max(max, pick(t)); }));
		}
		ctx.strokeStyle = 'rgba(255,255,255,.08)';
		ctx.fillStyle = '#8b93a1';
		ctx.font = '10px sans-serif';
		for (let i = 0; i <= 2; i++) {
			const y = 4 + ((h - 18) * i) / 2;
			ctx.beginPath();
			ctx.moveTo(26, y);
			ctx.lineTo(width, y);
			ctx.stroke();
			ctx.fillText(String(Math.round(max * (1 - i / 2))), 0, y + 3);
		}
		if (series.length < 2) {
			return;
		}
		const t0 = series[0].t;
		const span = Math.max(10000, series[series.length - 1].t - t0);
		for (const team of teams) {
			ctx.strokeStyle = teamColor(team);
			ctx.lineWidth = 1.5;
			ctx.beginPath();
			let started = false;
			for (const p of series) {
				const t = p.teams.get(team);
				const x = 26 + ((p.t - t0) / span) * (width - 30);
				const y = 4 + (h - 18) * (1 - (t ? pick(t) : 0) / max);
				if (started) {
					ctx.lineTo(x, y);
				} else {
					ctx.moveTo(x, y);
					started = true;
				}
			}
			ctx.stroke();
		}
		ctx.fillStyle = '#8b93a1';
		ctx.fillText(Math.round(span / 1000) + ' s of this watch  ·  ' + unit, 26, h - 3);
	}

	function updateCharts() {
		if (!V.chartList) {
			V.chartList = [
				['Team health', (t) => (t.max ? (100 * t.health) / t.max : 0), '% of full', 100],
				['Damage per second', (t) => t.dmg, 'health lost a second', 0],
				['Units', (t) => t.units, 'units', 0],
			].map(([title, pick, unit, fixed]) => ({ title, pick, unit, fixed, cv: el('canvas', { className: 'insp-chart', height: 110 }) }));
			V.charts.replaceChildren(el('div', { className: 'insp-pane-head' }, el('b', { textContent: 'Charts' }), el('span', { className: 'insp-legend' }), el('span', { className: 'spacer' }), el('button', { className: 'ico-btn', textContent: 'Clear', onclick: () => { I.series = []; I.sampleDamage.clear(); updateCharts(); } })),
				...V.chartList.map((c) => el('div', {}, el('div', { className: 'prof-list-head', textContent: c.title }), c.cv)));
		}
		const teams = new Set();
		I.series.forEach((p) => p.teams.forEach((_, t) => teams.add(t)));
		V.charts.querySelector('.insp-legend').replaceChildren(...[...teams].sort().map((t) => el('span', { className: 'insp-mode', style: 'border-color:' + teamColor(t), textContent: teamName(t) })));
		V.chartList.forEach((c) => drawChart(c.cv, c.title, c.pick, c.unit, c.fixed));
	}

	// The selected unit's mind: what it is doing, after whom, why it took cover, its orders and route; and a strip of what it has done lately.
	function fillMind(w) {
		const d = V.d;
		const m = w.mind.get(I.selected);
		const state = new Map(w.state);
		const st = (k) => state.get(k);
		const self = w.actors.find((a) => a.id === I.selected);
		const who = (id) => {
			const a = w.actors.find((x) => x.id === id);
			return a ? a.preset + ' #' + id + ' (' + teamName(a.team) + (self ? ', ' + Math.round(Math.hypot(a.x - self.x, a.y - self.y)) + ' px away' : '') + ')' : '#' + id + ' (gone)';
		};
		const link = (cell, id) => {
			cell.replaceChildren(el('button', { className: 'insp-link', textContent: who(id), title: 'Look at it', onclick: () => select(id) }));
		};
		const set = (row, text, id) => {
			row.tr.hidden = !text;
			if (!text) {
				return;
			}
			if (id) {
				if (row.id !== id || row.text !== text) {
					link(row.cell, id);
				}
			} else {
				setText(row.cell, text);
			}
			row.id = id || 0;
			row.text = text;
		};
		d.mindBox.hidden = !m && !w.state.length;
		const R = d.mind;
		set(R.behavior, m ? m.behavior + (st('AI_ClimbStage') ? ' (climb: ' + st('AI_ClimbStage') + ')' : '') : '');
		set(R.target, m && m.target ? (m.seen ? 'In sight: ' : 'Last seen: ') + who(m.target) : m ? 'no target' : '', m && m.target ? m.target : 0);
		set(R.squad, m && m.leader ? 'Follows ' + who(m.leader) : '', m && m.leader ? m.leader : 0);
		set(R.cover, m && m.why ? 'Took cover: ' + m.why + (st('AI_CoverMs') ? ' (' + (Number(st('AI_CoverMs')) / 1000).toFixed(1) + ' s)' : '') : st('AI_HoldRange') ? 'Keeping ' + st('AI_HoldRange') + ' px off its target' : '');
		const order = [st('orderKind') && st('orderKind') !== 'none' ? st('orderKind') : '', st('orderFailed') ? 'failed: ' + st('orderFailed') : ''].filter(Boolean).join(', ');
		set(R.order, [st('aiMode'), order].filter(Boolean).join(' · ') + (st('weaponRule') ? ' · weapons: ' + st('weaponRule') : '') + (st('temperament') ? ' · ' + st('temperament') : ''));
		set(R.route, st('routePoints') !== undefined ? (Number(st('routePoints')) ? st('routePoints') + ' points' + (st('step') && st('step') !== 'none' ? ', now ' + st('step') + (st('nextStep') && st('nextStep') !== 'none' ? ' then ' + st('nextStep') : '') : '') + (st('routeCost') ? ', cost ' + st('routeCost') : '') : 'no route') + (st('routeAsked') === 'true' ? ' (asking for a new one)' : '') : '');
		set(R.hurt, st('sinceHurtMS') ? 'Last hurt ' + (Number(st('sinceHurtMS')) / 1000).toFixed(1) + ' s ago, by ' + teamName(Number(st('lastAttackerTeam'))) : '');
		// the strip: the last two minutes, one colour a behaviour
		const h = I.hist.get(I.selected);
		const cv = d.strip;
		const width = cv.clientWidth || 300;
		if (cv.width !== width) {
			cv.width = width;
		}
		const ctx = cv.getContext('2d');
		ctx.clearRect(0, 0, cv.width, cv.height);
		d.legend.replaceChildren();
		if (h && h.runs.length) {
			const now = Date.now();
			const t0 = now - HIST_MS;
			h.runs.forEach((run, i) => {
				const start = Math.max(run.t, t0);
				const end = i + 1 < h.runs.length ? h.runs[i + 1].t : now;
				ctx.fillStyle = behaviorColor(run.b);
				ctx.fillRect(((start - t0) / HIST_MS) * cv.width, 0, Math.max(1, ((end - start) / HIST_MS) * cv.width), cv.height);
			});
			const total = [...h.totals.values()].reduce((a, b) => a + b, 0) || 1;
			d.legend.replaceChildren(...[...h.totals].sort((x, y) => y[1] - x[1]).map(([b, ms]) => el('span', { className: 'insp-mode', style: 'border-color:' + behaviorColor(b), title: 'Time since the Inspector started watching it', textContent: b + ' ' + Math.round((100 * ms) / total) + '%' })));
		}
		d.stripBox.hidden = !(h && h.runs.length);
	}

	// The game's camera follows a unit until told to stop: a double-click on it in the list. The game keeps its camera on it every frame.
	function followWithCamera(id) {
		select(id);
		I.camera = id;
		I.cameraSince = Date.now();
		sendRequest(true);
		render();
	}

	function stopCamera() {
		I.camera = 0;
		sendRequest(true);
		render();
	}

	// The chip in the head says who the camera is on; if the game says it is not following (the unit left), it clears.
	function updateCamera(w) {
		if (I.camera && w && Date.now() - I.cameraSince > 1500 && w.following !== I.camera) {
			I.camera = 0;
		}
		V.camera.hidden = !I.camera;
		if (I.camera) {
			const a = w && w.actors.find((x) => x.id === I.camera);
			setText(V.camera, '◎ Camera on ' + (a ? a.preset : '#' + I.camera) + ' ✕');
		}
	}

	function select(id) {
		// Picking someone else stops following you; picking yourself starts it again.
		const controlled = I.world ? I.world.controlled : 0;
		setFollow(!!controlled && id === controlled);
		I.selected = id;
		sendRequest(true);
		render();
	}

	// Lua run in the game on the selected actor (a = it), through the console's Run bridge.
	function onActor(code, what) {
		const id = I.selected;
		if (!id) {
			return;
		}
		MS.ide.runLua('local a = MovableMan:FindObjectByUniqueID(' + id + ')\nif a and IsActor(a) then\n\ta = ToActor(a)\n\t' + code + '\nelse\n\tConsoleMan:PrintString("ERROR: That actor isn\'t in the game any more.")\nend');
		app().status(what + ' (sent to the game).');
	}

	const fixed = (n, d) => (Number.isFinite(n) ? n.toFixed(d === undefined ? 0 : d) : '–');

	// ---- drawing ----
	//
	// The panel is built once and then updated in place: rows, values and inputs are kept and only their text changes. It used to be rebuilt
	// from nothing on every update (several a second), which made it flicker, threw away the scroll position, lost a click that landed
	// while it happened, closed an open Team menu, and showed "left the game" for a moment after picking someone while the game caught up.

	const V = { root: null, head: null, state: null, hint: null, follow: null, pause: null, step: null, filter: null, cols: null, list: null, rows: new Map(), feed: null, feedKey: '', note: null, detail: null, d: null, detailId: 0 };
	I.shownLive = null;
	const isLive = () => !!(I.modified && Date.now() - I.modified < 3000);
	const setText = (node, text) => {
		if (node.textContent !== text) {
			node.textContent = text;
		}
	};

	function build(body) {
		V.root = body;
		V.rows.clear();
		V.feedKey = '';
		V.detailId = 0;
		V.d = null;
		V.state = el('span', { className: 'prof-state off' });
		V.hint = el('span', { className: 'prof-hint', textContent: 'Start one with ▶ Play ▾ › Test in game or Sandbox, then pick an actor here; it\'s ringed in the game.' });
		V.follow = el('button', { className: 'ico-btn', textContent: '★ Follow me', title: 'Keep the unit you control selected, even when you switch to another', onclick: () => {
			setFollow(!I.follow);
			if (I.follow && I.world && I.world.controlled) {
				select(I.world.controlled);
			}
			render();
		} });
		V.pause = el('button', { className: 'ico-btn', title: 'Hold the game still to look (it keeps drawing)', onclick: () => {
			I.paused = !I.paused;
			sendRequest(true);
			render();
		} });
		V.step = el('button', { className: 'ico-btn', textContent: 'Step ▸', title: 'One simulation update (1/60 s), while paused', onclick: () => {
			I.steps++;
			sendRequest(true);
		} });
		const auto = el('label', { className: 'insp-auto', title: 'Open the Inspector when Test in game or Sandbox starts from Studio' }, el('input', { type: 'checkbox', checked: stored(AUTO_KEY, true), onchange: (e) => store(AUTO_KEY, e.target.checked) }), 'Open on Test/Sandbox');
		V.filter = el('input', { type: 'search', placeholder: 'Filter…', value: I.filter, className: 'ed-input insp-filter' });
		V.filter.oninput = () => {
			I.filter = V.filter.value;
			render();
		};
		V.camera = el('button', { className: 'ico-btn on', hidden: true, title: 'The game\'s camera is following this unit. Click to let go.', onclick: () => stopCamera() });
		V.tabs = ['actors', 'combat', 'charts'].map((id) => el('button', { className: 'ico-btn', textContent: { actors: 'Actors', combat: 'Combat', charts: 'Charts' }[id], title: { actors: 'Pick an actor and see its values and mind', combat: 'Who hit whom, with what: damage by weapon and by shooter', charts: 'Health, damage and units over time, by team' }[id], onclick: () => { I.view = id; render(); } }));
		V.head = el('div', { className: 'prof-head' }, V.state, el('span', { className: 'insp-tabs' }, ...V.tabs), V.hint, el('span', { className: 'spacer' }), V.camera, auto, V.follow, V.pause, V.step, V.filter);
		V.overview = el('div', { className: 'insp-overview', hidden: true });
		V.overviewKey = '';
		V.list = el('div', { className: 'insp-list' });
		V.feed = el('div', { className: 'insp-feed' });
		V.note = el('p', { className: 'pempty' });
		V.detail = el('div', { className: 'insp-detail' }, V.note);
		V.combat = el('div', { className: 'insp-pane', hidden: true });
		V.charts = el('div', { className: 'insp-pane', hidden: true });
		V.combatKey = '';
		V.cols = el('div', { className: 'insp-cols', hidden: true }, el('div', { className: 'insp-left' }, V.overview, V.list, el('div', { className: 'prof-list-head', textContent: 'What happened' }), V.feed), V.detail, V.combat, V.charts);
		body.replaceChildren(V.head, V.cols);
	}

	function render() {
		const body = $('inspectorBody');
		if (!body) {
			return;
		}
		if (V.root !== body) {
			build(body);
		}
		const w = I.world;
		const live = isLive();
		I.shownLive = live;
		V.state.className = live ? 'prof-state on' : 'prof-state off';
		setText(V.state, live ? (w && w.paused ? (w.pausedByStudio ? '❚❚ Paused' : '❚❚ Paused by the game (its menu or the sandbox tools)') : '● Live') + ' · ' + (w ? w.actors.length : 0) + ' actors' : '○ No game running from Studio');
		V.hint.hidden = live;
		V.follow.classList.toggle('on', !!I.follow);
		updateCamera(w);
		setText(V.pause, I.paused ? '▶ Resume' : '❚❚ Pause');
		V.step.disabled = !I.paused && !(w && w.paused);
		const showing = !!w && live;
		V.cols.hidden = !showing;
		if (!showing) {
			return;
		}
		V.tabs.forEach((b, i) => b.classList.toggle('on', ['actors', 'combat', 'charts'][i] === I.view));
		V.detail.hidden = I.view !== 'actors';
		V.combat.hidden = I.view !== 'combat';
		V.charts.hidden = I.view !== 'charts';
		updateOverview(w);
		updateList(w);
		updateFeed();
		if (I.view === 'combat') {
			updateCombat(w);
		} else if (I.view === 'charts') {
			updateCharts();
		} else {
			updateDetail(w);
		}
	}

	const freshActor = (id) => I.arrived.has(id) && Date.now() - I.arrived.get(id) < 8000;

	function makeRow(id) {
		const team = el('span', { className: 'insp-team' });
		const nameText = document.createTextNode('');
		const you = el('em', { className: 'insp-you', textContent: 'you', hidden: true });
		const isNew = el('em', { className: 'insp-new', textContent: 'new', hidden: true });
		const bar = el('i');
		const hp = el('small');
		const row = el('button', { className: 'insp-actor', onclick: () => select(id), ondblclick: () => followWithCamera(id), title: 'Double-click: the game\'s camera follows it' }, team, el('span', { className: 'insp-name' }, nameText, you, isNew), el('span', { className: 'insp-health' }, bar), hp);
		row._r = { team, nameText, you, isNew, bar, hp };
		return row;
	}

	function fillRow(row, a, w) {
		const r = row._r;
		const ratio = a.max > 0 ? Math.max(0, Math.min(1, a.health / a.max)) : 0;
		const fresh = freshActor(a.id);
		const className = 'insp-actor' + (a.id === I.selected ? ' sel' : '') + (fresh ? ' new' : '') + (a.id === I.camera ? ' cam' : '');
		if (row.className !== className) {
			row.className = className;
		}
		const title = a.cls + ' #' + a.id + ' at (' + fixed(a.x) + ', ' + fixed(a.y) + ')\nDouble-click: the game\'s camera follows it';
		if (row.title !== title) {
			row.title = title;
		}
		const teamClass = 'insp-team t' + a.team;
		if (r.team.className !== teamClass) {
			r.team.className = teamClass;
		}
		setText(r.team, a.team >= 0 ? 'T' + (a.team + 1) : '–');
		if (r.nameText.data !== a.preset) {
			r.nameText.data = a.preset;
		}
		r.you.hidden = a.id !== w.controlled;
		r.isNew.hidden = !fresh;
		const width = ratio * 100 + '%';
		if (r.bar.style.width !== width) {
			r.bar.style.width = width;
		}
		setText(r.hp, fixed(a.health));
	}

	// The actor rows, matched by id: the ones already there are updated, new ones added, and the order kept.
	function updateList(w) {
		const q = I.filter.trim().toLowerCase();
		const actors = w.actors.filter((a) => !q || a.preset.toLowerCase().includes(q) || a.cls.toLowerCase().includes(q) || String(a.id) === q);
		const keep = new Set();
		let previous = null;
		for (const a of actors) {
			let row = V.rows.get(a.id);
			if (!row) {
				row = makeRow(a.id);
				V.rows.set(a.id, row);
			}
			keep.add(a.id);
			fillRow(row, a, w);
			const expected = previous ? previous.nextSibling : V.list.firstChild;
			if (row !== expected) {
				V.list.insertBefore(row, expected);
			}
			previous = row;
		}
		for (const [id, row] of V.rows) {
			if (!keep.has(id)) {
				row.remove();
				V.rows.delete(id);
			}
		}
	}

	function updateFeed() {
		const first = I.events[0];
		const key = I.events.length + '|' + (first ? first.time + first.text : '');
		if (key === V.feedKey) {
			return;
		}
		V.feedKey = key;
		const top = V.feed.scrollTop;
		V.feed.replaceChildren(...I.events.slice(0, 40).map((e) => el('div', { className: 'insp-event ' + e.kind }, el('small', { textContent: e.time }), e.text)));
		V.feed.scrollTop = top;
	}

	function showNote(text) {
		V.detail.classList.remove('stale');
		setText(V.note, text);
		V.note.hidden = false;
		if (V.d) {
			V.d.box.hidden = true;
		}
	}

	function updateDetail(w) {
		const a = w.actors.find((x) => x.id === I.selected);
		if (!I.selected) {
			showNote('Pick an actor to see its values; it\'s ringed in the game.');
			return;
		}
		if (!a) {
			showNote('That actor has left the game. Pick another.');
			return;
		}
		V.note.hidden = true;
		if (V.detailId !== I.selected) {
			buildDetail(a);
		}
		V.d.box.hidden = false;
		const current = w.selected === I.selected;
		// Right after picking someone, the game still says it is looking at the one before: keep what is shown, dimmed, until it catches up.
		V.detail.classList.toggle('stale', !current);
		setText(V.d.name, a.preset || '#' + I.selected);
		setText(V.d.cls, ' ' + (a.cls || '') + ' #' + I.selected);
		V.d.health.placeholder = fixed(a.health);
		if (document.activeElement !== V.d.team) {
			V.d.team.value = String(a.team);
		}
		if (current) {
			fillMind(w);
			fillValues(w);
			fillSide(w);
		}
	}

	// The values, matched by name: a value's cell is updated, not its row replaced.
	function fillValues(w) {
		const d = V.d;
		const keep = new Set();
		let previous = null;
		w.values.forEach(([k, v], i) => {
			const key = k + '#' + i;
			let entry = d.values.get(key);
			if (!entry) {
				const value = el('td');
				entry = { tr: el('tr', {}, el('td', { textContent: k }), value), value };
				d.values.set(key, entry);
			}
			keep.add(key);
			setText(entry.value, v);
			const expected = previous ? previous.nextSibling : d.table.firstChild;
			if (entry.tr !== expected) {
				d.table.insertBefore(entry.tr, expected);
			}
			previous = entry.tr;
		});
		for (const [key, entry] of d.values) {
			if (!keep.has(key)) {
				entry.tr.remove();
				d.values.delete(key);
			}
		}
	}

	// Scripts, inventory and parts change rarely: they are redrawn only when they do.
	function fillSide(w) {
		const d = V.d;
		const key = JSON.stringify([w.scripts, w.inventory, w.parts]);
		if (key === d.sideKey) {
			return;
		}
		d.sideKey = key;
		const parts = w.parts.length
			? el('table', { className: 'watch-table insp-parts' }, el('tr', {}, el('th', { textContent: 'Part' }), el('th', { textContent: 'Class' }), el('th', { textContent: 'Wounds' })),
				...w.parts.map((p) => el('tr', { className: p.limit > 0 && p.wounds >= p.limit * 0.75 ? 'insp-hurt' : '' }, el('td', { textContent: p.preset }), el('td', { textContent: p.cls }), el('td', { textContent: p.wounds + (p.limit > 0 ? ' of ' + p.limit : '') }))))
			: null;
		d.side.replaceChildren(
			w.scripts.length ? el('div', { className: 'prof-list-head', textContent: 'Scripts it runs' }) : null,
			...w.scripts.map((path) => el('button', { className: 'insp-script', textContent: path.split('/').pop(), title: path + ' (click to open)', onclick: () => MS.ide.openFile(path) })),
			w.inventory.length ? el('div', { className: 'prof-list-head', textContent: 'Inventory' }) : null, ...w.inventory.map((i) => el('div', { className: 'insp-inv', textContent: i })),
			parts ? el('div', { className: 'prof-list-head', textContent: 'Parts' }) : null, parts);
	}

	// The detail for one actor: its title, the buttons and inputs (kept while you use them), and the places the values go.
	function buildDetail(a) {
		V.detailId = I.selected;
		if (V.d) {
			V.d.box.remove();
		}
		const preset = app().lib && app().lib.presets.find((p) => p.topLevel && p.name === a.preset && p.cls === a.cls);
		const health = el('input', { type: 'number', className: 'ed-input num', title: 'New health' });
		const team = el('select', { className: 'ed-input', title: 'Team' }, ...[-1, 0, 1, 2, 3].map((t) => el('option', { value: t, textContent: t < 0 ? 'No team' : 'Team ' + (t + 1) })));
		const actions = el('div', { className: 'insp-actions' },
			el('label', {}, 'Health ', health, el('button', { className: 'ico-btn', textContent: 'Set', onclick: () => health.value !== '' && onActor('a.Health = ' + Number(health.value), 'Set health to ' + health.value) })),
			el('button', { className: 'ico-btn', textContent: 'Heal', onclick: () => onActor('a.Health = a.MaxHealth\n\ta:RemoveWounds(a:GetWoundCount())', 'Healed') }),
			el('label', {}, 'Team ', team, el('button', { className: 'ico-btn', textContent: 'Set', onclick: () => onActor('a.Team = ' + Number(team.value), 'Moved to ' + team.selectedOptions[0].textContent) })),
			el('button', { className: 'ico-btn', textContent: 'Stop it', title: 'No speed or spin', onclick: () => onActor('a.Vel = Vector(0, 0)\n\ta.AngularVel = 0', 'Stopped') }),
			el('button', { className: 'ico-btn', textContent: 'Lift it', title: 'Up 60 pixels, to free it', onclick: () => onActor('a.Pos = a.Pos + Vector(0, -60)\n\ta.Vel = Vector(0, 0)', 'Lifted') }),
			el('button', { className: 'ico-btn', textContent: 'Take control', onclick: () => onActor('ActivityMan:GetActivity():SwitchToActor(a, 0, a.Team)', 'Took control') }),
			el('button', { className: 'ico-btn', textContent: 'Kill', onclick: () => onActor('a.Health = 0', 'Killed') }),
			el('button', { className: 'ico-btn', textContent: 'Gib', onclick: () => onActor('a:GibThis()', 'Gibbed') }),
			el('button', { className: 'ico-btn', textContent: 'Remove', onclick: () => onActor('a.ToDelete = true', 'Removed') }),
			preset ? el('button', { className: 'ico-btn', textContent: 'Open in the designer', onclick: () => { app().openActor(preset); MS.ide.activate(MS.ide.tabs.find((t) => t.kind === 'designer')); } }) : null,
			preset ? el('button', { className: 'ico-btn', textContent: 'Its lines', onclick: () => MS.ide.openFile(preset.doc.path, preset.node.line) }) : null);
		const name = el('b');
		const cls = el('small');
		const table = el('table', { className: 'watch-table' });
		const side = el('div');
		const mindRow = (label, title) => {
			const cell = el('td');
			return { tr: el('tr', { hidden: true }, el('th', { textContent: label, title }), cell), cell, id: 0, text: '' };
		};
		const mind = { behavior: mindRow('Doing', 'What its AI is doing now'), target: mindRow('Target', 'Who it is after; click to look at them'), squad: mindRow('Squad', 'The leader it follows'), cover: mindRow('Cover', 'Why it took cover, or the range it keeps'), order: mindRow('Orders', 'Its AI mode, order and rules'), route: mindRow('Route', 'The path it is following'), hurt: mindRow('Hurt', 'When it was last hit') };
		const strip = el('canvas', { className: 'insp-strip', height: 14, title: 'The last two minutes of what its AI did, one colour a behaviour' });
		const legend = el('div', { className: 'insp-modes' });
		const stripBox = el('div', {}, strip, legend);
		const mindBox = el('div', { className: 'insp-mind' }, el('div', { className: 'prof-list-head', textContent: 'Its mind' }), el('table', { className: 'watch-table insp-mindtable' }, ...Object.values(mind).map((r) => r.tr)), stripBox);
		const box = el('div', {}, el('div', { className: 'insp-title' }, name, cls), actions, mindBox, el('div', { className: 'insp-tables' }, table, side));
		V.detail.append(box);
		V.d = { box, name, cls, health, team, table, side, mind, mindBox, strip, legend, stripBox, values: new Map(), sideKey: null };
	}

	function setFollow(on) {
		I.follow = on;
		store('ms.inspectorFollow', on);
	}

	// Opens the panel (Script mode, bottom panel) for a game that has just started.
	function open() {
		MS.ide.setMode('deep');
		MS.ide.showPanel('inspector');
		setFollow(true);
	}

	MS.inspector = {
		show() {
			render();
			poll();
		},
		// A launch of Test in game or Sandbox (launcher.js): open the Inspector now, or on the page that loads when Studio moves to the
		// version that started.
		launched(reloading) {
			if (!stored(AUTO_KEY, true)) {
				return;
			}
			if (reloading) {
				store(PENDING_KEY, true);
			} else {
				open();
			}
		},
	};

	// After a launch that moved Studio to another folder, this page opens the Inspector once Studio has read the game.
	if (stored(PENDING_KEY, false)) {
		store(PENDING_KEY, false);
		const started = Date.now();
		const wait = setInterval(() => {
			if (MS.ide && MS.ide.showPanel && MS.app && MS.app.lib) {
				clearInterval(wait);
				open();
			} else if (Date.now() - started > 30000) {
				clearInterval(wait);
			}
		}, 300);
	}
})();
