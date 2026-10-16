// Cortex Mod Studio: being kind to people who don't read INI. Plain wording for the errors the browser and the file system produce,
// a window that says what is wrong with a mod in everyday words and fixes the common ones in a click, and the welcome card shown before
// a unit is open, so the first screen says what to do.
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

	// ---- everyday wording ----

	const PATTERNS = [
		[/^(Failed to fetch|Load failed|NetworkError.*)$/i, () => 'Lost the connection to Studio. If its window was closed, open Studio again.'],
		[/File not found: (?:Mods\/|Data\/)?(.+)$/i, (m) => 'A file is missing: ' + m[1] + '.'],
		[/^(?:Error: )?QuotaExceededError.*$/i, () => 'There is no room left to save that.'],
	];

	// Turns a raw message into one a modder can act on; anything it doesn't know passes through unchanged.
	function say(text) {
		const t = String(text === undefined || text === null ? '' : text).trim();
		for (const [re, out] of PATTERNS) {
			const m = re.exec(t);
			if (m) {
				return out(m);
			}
		}
		return t;
	}

	// ---- what is wrong with your mods ----

	let hidden = new Set(); // problems skipped for this session

	function problems() {
		const lib = app().lib;
		return lib && lib.problems ? lib.problems.filter((p) => !hidden.has(p.module + '|' + p.path)) : [];
	}

	function updateBadge() {
		const b = $('statusProblems');
		if (!b) {
			return;
		}
		const n = problems().length;
		b.hidden = !n;
		b.textContent = '⚠ ' + n + (n === 1 ? ' problem' : ' problems') + ' in your mods';
	}

	function describe(p) {
		const file = p.path.split('/').slice(1).join('/');
		return p.missing ? p.module + ' lists ' + file + ', but that file isn\'t there.' : p.module + ': Studio couldn\'t read ' + file + '.';
	}

	// Takes the line that names a missing file out of the mod's Index.ini (or whichever file includes it).
	async function removeFromList(p) {
		const a = app();
		const module = a.lib.modules.find((m) => m.name === p.module);
		const want = p.path.replace(/\\/g, '/').toLowerCase();
		const changed = [];
		for (const doc of module ? module.docs : []) {
			for (const node of [...doc.walk()]) {
				if (node.key === 'IncludeFile' && node.value.replace(/\\/g, '/').toLowerCase() === want) {
					if (a.lib.isProtected(doc)) {
						a.toast(p.module + ' isn\'t one of your mods, so Studio won\'t change it. Choose Edit this mod first.', 'bad');
						return false;
					}
					node.remove();
					changed.push(doc);
				}
			}
		}
		if (!changed.length) {
			a.toast('Couldn\'t find where ' + p.module + ' lists that file.', 'bad');
			return false;
		}
		for (const doc of changed) {
			doc.markDirty();
		}
		await a.save(true, changed);
		a.lib.problems = a.lib.problems.filter((q) => q !== p);
		a.lib.reindex();
		a.toast('Took it out of ' + p.module + '\'s list. The mod loads without it now.', 'good');
		return true;
	}

	function showProblems() {
		const a = app();
		const list = el('div', { className: 'prob-list' });
		const draw = () => {
			list.replaceChildren();
			const items = problems();
			updateBadge();
			if (!items.length) {
				list.append(el('p', { className: 'prob-none', textContent: 'Nothing is wrong. All your mods loaded.' }));
				return;
			}
			for (const p of items) {
				const actions = el('div', { className: 'prob-actions' });
				if (p.missing) {
					actions.append(el('button', { className: 'tb primary', textContent: 'Take it out of the list', title: 'Removes the line that names the missing file, so the mod loads without it', onclick: async () => { if (await removeFromList(p)) { draw(); } } }));
					if (MS.history && MS.history.available()) {
						actions.append(el('button', { className: 'tb', textContent: 'Look in History', title: 'If Studio kept a copy of this file, you can put it back', onclick: () => { MS.dialogs.close(); MS.history.open(p.path); } }));
					}
				}
				actions.append(el('button', { className: 'tb', textContent: 'Ignore', onclick: () => { hidden.add(p.module + '|' + p.path); draw(); } }));
				list.append(el('div', { className: 'prob-row' }, el('div', { className: 'prob-text' }, el('b', { textContent: describe(p) }), el('small', { textContent: p.missing ? 'The game would stop loading this mod at that point. Either put the file back, or take it out of the list.' : p.message })), actions));
			}
		};
		draw();
		MS.dialogs.open('Problems in your mods', el('div', {}, el('p', { className: 'prob-lead', textContent: 'Studio found things in your mods that need a look.' }), list), [{ text: 'Close' }], true);
	}

	// Called once the mods are read.
	function afterLoad() {
		const n = problems().length;
		updateBadge();
		if (n) {
			app().toast(n === 1 ? 'One of your mods has a problem.' : n + ' problems in your mods.', 'bad', { label: 'See what\'s wrong', run: showProblems });
		}
	}

	// ---- the welcome card ----

	let card = null;

	function build() {
		const a = app();
		const choice = (icon, title, text, run) => el('button', { className: 'start-choice', onclick: run }, el('span', { className: 'start-icon', textContent: icon }), el('b', { textContent: title }), el('span', { textContent: text }));
		const mine = a.lib ? a.lib.modules.filter((m) => !m.official).length : 0;
		return el('div', { className: 'start-card' },
			el('div', { className: 'start-mark', textContent: '◆' }),
			el('h1', { textContent: 'Make something for Cortex Command' }),
			el('p', { textContent: 'Units, guns, grenades, craft, whole mods. Pick a way to begin; nothing you do here can break the game\'s own files.' }),
			el('div', { className: 'start-choices' },
				choice('✦', 'Make something new', 'A soldier, crab, dropship, gun or grenade, step by step, with your own pictures', () => MS.wizard.open()),
				choice('✎', 'Change a unit from the game', 'Pick one on the left. Studio makes your own copy, so the original stays safe', () => {
					const search = $('libSearch');
					search.focus();
					const left = $('left');
					left.classList.add('attention');
					setTimeout(() => left.classList.remove('attention'), 1600);
				}),
				choice('▤', 'Open one of my mods', mine ? 'You have ' + mine + ' mod' + (mine === 1 ? '' : 's') + ' on this computer' : 'Your mods will show here once you have some', () => {
					const box = $('libMineOnly');
					if (box && !box.checked) {
						box.checked = true;
						box.dispatchEvent(new Event('change', { bubbles: true }));
					}
					$('left').classList.add('attention');
					setTimeout(() => $('left').classList.remove('attention'), 1600);
				}),
				choice('?', 'How it works', 'A short guide to the controls and the words Studio uses', () => MS.dialogs.help())));
	}

	// Shown in the empty unit view until something is open.
	function update() {
		const a = app();
		const wrap = $('viewWrap');
		if (!wrap || !a) {
			return;
		}
		const show = !!a.lib && !a.rig && $('welcome').hidden;
		if (show && !card) {
			card = build();
			wrap.append(card);
		} else if (!show && card) {
			card.remove();
			card = null;
		}
	}

	MS.friendly = { say, showProblems, afterLoad, update, updateBadge };
	const badge = $('statusProblems');
	if (badge) {
		badge.onclick = showProblems;
	}
})();
