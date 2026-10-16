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
		timer: 0, selected: 0, paused: false, steps: 0, lastRequest: 0, requestText: '', world: null, modified: 0, filter: '', team: 'all',
		follow: stored('ms.inspectorFollow', true), // keep the unit you control selected, across switching bodies
		seen: new Map(), // id -> the actor as last seen, to tell what arrived and what left
		arrived: new Map(), // id -> when it arrived, to mark it new for a few seconds
		events: [], // {time, text, kind}, newest first
		lastLive: 0,
	};
	const AUTO_KEY = 'ms.inspectorAuto'; // open the Inspector when Test in game or Sandbox starts from Studio
	const PENDING_KEY = 'ms.inspectorPending'; // set by a launch, taken by the page that loads after it

	function parse(text) {
		const w = { actors: [], values: [], inventory: [], parts: [], scripts: [], selected: 0, controlled: 0, paused: false };
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
		const text = '// Written by Cortex Mod Studio\'s Inspector while it is open.\nSelected = ' + I.selected + '\nPause = ' + (I.paused ? 1 : 0) + '\nStep = ' + I.steps + '\n';
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
					noteChanges(I.world);
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
		V.head = el('div', { className: 'prof-head' }, V.state, V.hint, el('span', { className: 'spacer' }), auto, V.follow, V.pause, V.step, V.filter);
		V.list = el('div', { className: 'insp-list' });
		V.feed = el('div', { className: 'insp-feed' });
		V.note = el('p', { className: 'pempty' });
		V.detail = el('div', { className: 'insp-detail' }, V.note);
		V.cols = el('div', { className: 'insp-cols', hidden: true }, el('div', { className: 'insp-left' }, V.list, el('div', { className: 'prof-list-head', textContent: 'What happened' }), V.feed), V.detail);
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
		setText(V.pause, I.paused ? '▶ Resume' : '❚❚ Pause');
		V.step.disabled = !I.paused && !(w && w.paused);
		const showing = !!w && live;
		V.cols.hidden = !showing;
		if (!showing) {
			return;
		}
		updateList(w);
		updateFeed();
		updateDetail(w);
	}

	const freshActor = (id) => I.arrived.has(id) && Date.now() - I.arrived.get(id) < 8000;

	function makeRow(id) {
		const team = el('span', { className: 'insp-team' });
		const nameText = document.createTextNode('');
		const you = el('em', { className: 'insp-you', textContent: 'you', hidden: true });
		const isNew = el('em', { className: 'insp-new', textContent: 'new', hidden: true });
		const bar = el('i');
		const hp = el('small');
		const row = el('button', { className: 'insp-actor', onclick: () => select(id) }, team, el('span', { className: 'insp-name' }, nameText, you, isNew), el('span', { className: 'insp-health' }, bar), hp);
		row._r = { team, nameText, you, isNew, bar, hp };
		return row;
	}

	function fillRow(row, a, w) {
		const r = row._r;
		const ratio = a.max > 0 ? Math.max(0, Math.min(1, a.health / a.max)) : 0;
		const fresh = freshActor(a.id);
		const className = 'insp-actor' + (a.id === I.selected ? ' sel' : '') + (fresh ? ' new' : '');
		if (row.className !== className) {
			row.className = className;
		}
		const title = a.cls + ' #' + a.id + ' at (' + fixed(a.x) + ', ' + fixed(a.y) + ')';
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
		const box = el('div', {}, el('div', { className: 'insp-title' }, name, cls), actions, el('div', { className: 'insp-tables' }, table, side));
		V.detail.append(box);
		V.d = { box, name, cls, health, team, table, side, values: new Map(), sideKey: null };
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
