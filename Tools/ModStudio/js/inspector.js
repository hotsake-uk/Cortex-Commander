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
	const I = { timer: 0, selected: 0, paused: false, steps: 0, lastRequest: 0, requestText: '', world: null, modified: 0, filter: '', team: 'all' };

	function parse(text) {
		const w = { actors: [], values: [], inventory: [], parts: [], selected: 0, controlled: 0, paused: false };
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
					if (!typing()) {
						render();
					}
				}
			}
		} catch (e) {
			// no game yet
		}
		if ((!I.modified || Date.now() - I.modified > 3000) && !typing()) {
			render();
		}
		I.timer = setTimeout(poll, 300);
	}

	// A number or team being changed in the panel: the next refresh waits, so it isn't thrown away.
	function typing() {
		const a = document.activeElement;
		return !!a && $('inspectorBody').contains(a) && (a.tagName === 'INPUT' || a.tagName === 'SELECT') && !a.classList.contains('insp-filter');
	}

	function select(id) {
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

	function render() {
		const body = $('inspectorBody');
		if (!body) {
			return;
		}
		const w = I.world;
		const live = I.modified && Date.now() - I.modified < 3000;
		const pause = el('button', { className: 'ico-btn', textContent: I.paused ? '▶ Resume' : '❚❚ Pause', title: 'Hold the game still to look (it keeps drawing)', onclick: () => { I.paused = !I.paused; sendRequest(true); render(); } });
		const step = el('button', { className: 'ico-btn', textContent: 'Step ▸', title: 'One simulation update (1/60 s), while paused', disabled: !I.paused, onclick: () => { I.steps++; sendRequest(true); } });
		const mine = el('button', { className: 'ico-btn', textContent: '★ The one I control', disabled: !w || !w.controlled, onclick: () => select(w.controlled) });
		const filter = el('input', { type: 'search', placeholder: 'Filter…', value: I.filter, className: 'ed-input insp-filter' });
		filter.oninput = () => {
			I.filter = filter.value;
			render();
			const again = $('inspectorBody').querySelector('.insp-filter');
			again.focus();
			again.setSelectionRange(again.value.length, again.value.length);
		};
		const head = el('div', { className: 'prof-head' },
			el('span', { className: live ? 'prof-state on' : 'prof-state off', textContent: live ? (w && w.paused ? (w.pausedByStudio ? '❚❚ Paused' : '❚❚ Paused by the game (its menu or the sandbox tools)') : '● Live') + ' · ' + (w ? w.actors.length : 0) + ' actors' : '○ No game running from Studio' }),
			live ? null : el('span', { className: 'prof-hint', textContent: 'Start one with ▶ Play ▾ › Test in game or Sandbox, then pick an actor here; it\'s ringed in the game.' }),
			el('span', { className: 'spacer' }), mine, pause, step, filter);
		if (!w || !live) {
			body.replaceChildren(head);
			return;
		}
		const q = I.filter.trim().toLowerCase();
		const actors = w.actors.filter((a) => !q || a.preset.toLowerCase().includes(q) || a.cls.toLowerCase().includes(q) || String(a.id) === q);
		const list = el('div', { className: 'insp-list' }, ...actors.map((a) => {
			const ratio = a.max > 0 ? Math.max(0, Math.min(1, a.health / a.max)) : 0;
			return el('button', { className: 'insp-actor' + (a.id === I.selected ? ' sel' : ''), onclick: () => select(a.id), title: a.cls + ' #' + a.id + ' at (' + fixed(a.x) + ', ' + fixed(a.y) + ')' },
				el('span', { className: 'insp-team t' + a.team, textContent: a.team >= 0 ? 'T' + (a.team + 1) : '–' }),
				el('span', { className: 'insp-name', textContent: (a.id === w.controlled ? '★ ' : '') + a.preset }),
				el('span', { className: 'insp-health' }, el('i', { style: 'width:' + ratio * 100 + '%' })),
				el('small', { textContent: fixed(a.health) }));
		}));
		const detail = el('div', { className: 'insp-detail' });
		if (I.selected && w.selected === I.selected) {
			detail.append(...detailOf(w));
		} else if (I.selected) {
			detail.append(el('p', { className: 'pempty', textContent: 'That actor has left the game. Pick another.' }));
		} else {
			detail.append(el('p', { className: 'pempty', textContent: 'Pick an actor to see its values; it\'s ringed in the game.' }));
		}
		body.replaceChildren(head, el('div', { className: 'insp-cols' }, list, detail));
	}

	function detailOf(w) {
		const a = w.actors.find((x) => x.id === I.selected) || {};
		const rows = w.values.map(([k, v]) => el('tr', {}, el('td', { textContent: k }), el('td', { textContent: v })));
		const preset = app().lib && app().lib.presets.find((p) => p.topLevel && p.name === a.preset && p.cls === a.cls);
		const health = el('input', { type: 'number', className: 'ed-input num', placeholder: fixed(a.health), title: 'New health' });
		const team = el('select', { className: 'ed-input', title: 'Team' }, ...[-1, 0, 1, 2, 3].map((t) => el('option', { value: t, textContent: t < 0 ? 'No team' : 'Team ' + (t + 1), selected: t === a.team })));
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
		const parts = w.parts.length
			? el('table', { className: 'watch-table insp-parts' }, el('tr', {}, el('th', { textContent: 'Part' }), el('th', { textContent: 'Class' }), el('th', { textContent: 'Wounds' })),
				...w.parts.map((p) => el('tr', { className: p.limit > 0 && p.wounds >= p.limit * 0.75 ? 'insp-hurt' : '' }, el('td', { textContent: p.preset }), el('td', { textContent: p.cls }), el('td', { textContent: p.wounds + (p.limit > 0 ? ' of ' + p.limit : '') }))))
			: null;
		return [
			el('div', { className: 'insp-title' }, el('b', { textContent: a.preset || '#' + I.selected }), el('small', { textContent: ' ' + (a.cls || '') + ' #' + I.selected })),
			actions,
			el('div', { className: 'insp-tables' },
				el('table', { className: 'watch-table' }, ...rows),
				el('div', {}, w.inventory.length ? el('div', { className: 'prof-list-head', textContent: 'Inventory' }) : null, ...w.inventory.map((i) => el('div', { className: 'insp-inv', textContent: i })),
					parts ? el('div', { className: 'prof-list-head', textContent: 'Parts' }) : null, parts)),
		];
	}

	MS.inspector = {
		show() {
			render();
			poll();
		},
	};
})();
