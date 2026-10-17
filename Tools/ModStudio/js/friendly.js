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
	const checks = new Map(); // module -> what the check after the last save found: [{path, line, severity, message}]

	function problems() {
		const lib = app().lib;
		return lib && lib.problems ? lib.problems.filter((p) => !hidden.has(p.module + '|' + p.path)) : [];
	}

	function checkProblems() {
		const all = [];
		for (const [module, list] of checks) {
			for (const p of list) {
				if (!hidden.has(module + '|' + p.path + ':' + p.line + ':' + p.message)) {
					all.push(Object.assign({ module }, p));
				}
			}
		}
		return all;
	}

	// What the mod check says, in everyday words.
	function sayCheck(message) {
		let m = /^"(.+)" is read after the file that copies it/.exec(message);
		if (m) {
			return '"' + m[1] + '" is defined further down than the thing that copies it. The game reads files in order, so it would stop loading the mod here.';
		}
		m = /^CopyOf "(.+)" isn't a preset Studio can find/.exec(message);
		if (m) {
			return 'It copies "' + m[1] + '", but nothing has that name. The game would stop loading the mod here.';
		}
		m = /^(.+\.(png|bmp)) isn't in the game folder\.$/i.exec(message);
		if (m) {
			return 'The picture ' + m[1].split('/').pop() + ' is missing. Put it back, or choose another picture.';
		}
		m = /^(.+\.(wav|ogg|flac|mp3)) isn't in the game folder\.$/i.exec(message);
		if (m) {
			return 'The sound ' + m[1].split('/').pop() + ' is missing.';
		}
		m = /^(.+\.lua) isn't in the game folder\.$/i.exec(message);
		if (m) {
			return 'The script ' + m[1].split('/').pop() + ' is missing.';
		}
		m = /^(.+) isn't in the game folder\.$/.exec(message);
		if (m) {
			return 'The file ' + m[1].split('/').pop() + ' is missing.';
		}
		m = /^(.+) is in another mod, which players will need too\.$/.exec(message);
		if (m) {
			return 'It uses ' + m[1].split('/').pop() + ' from ' + m[1].split('/')[0] + '. Players will need that mod as well.';
		}
		return message;
	}

	function updateBadge() {
		const b = $('statusProblems');
		if (!b) {
			return;
		}
		const n = problems().length + checkProblems().filter((p) => p.severity === 'error').length;
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

	// One missing file named in a mod's list is enough to stop the game loading the whole mod ("123.rte was NOT loaded"), after which none of its units
	// are there. For the project's mod (yours) the lines that name files that aren't there are taken out as soon as it is chosen and before a game starts.
	async function repairProject(module) {
		const a = app();
		if (!a.lib || !module) {
			return 0;
		}
		const found = (a.lib.problems || []).filter((p) => p.missing && p.module.toLowerCase() === module.toLowerCase());
		const names = [];
		for (const p of found) {
			const doc = a.lib.docFor(p.module + '/Index.ini');
			if (doc && a.lib.isProtected(doc)) {
				continue;
			}
			const quiet = a.toast;
			a.toast = () => {}; // one summary below instead of one message each
			try {
				if (await removeFromList(p)) {
					names.push(p.path.split('/').pop());
				}
			} finally {
				a.toast = quiet;
			}
		}
		if (names.length) {
			a.toast('Took ' + names.length + ' file' + (names.length > 1 ? 's' : '') + ' that no longer exist (' + names.slice(0, 3).join(', ') + (names.length > 3 ? '…' : '') + ') out of ' + module + ' list. With them listed the game would not load ' + module + ' at all.', 'good');
		}
		return names.length;
	}

	function showProblems() {
		const a = app();
		const list = el('div', { className: 'prob-list' });
		const draw = () => {
			list.replaceChildren();
			const items = problems();
			const found = checkProblems();
			updateBadge();
			if (!items.length && !found.length) {
				list.append(el('p', { className: 'prob-none', textContent: 'Nothing is wrong. Your mods loaded and passed the check.' }));
				return;
			}
			if (items.length) {
				list.append(el('div', { className: 'prob-group', textContent: 'Files that couldn\'t be read' }));
			}
			for (const p of items) {
				const actions = el('div', { className: 'prob-actions' });
				if (p.missing) {
					actions.append(el('button', { className: 'tb primary', textContent: 'Take it out of the list', title: 'Removes the line that names the missing file, so the mod loads without it', onclick: async () => { if (await removeFromList(p)) { draw(); } } }));
					if (MS.history && MS.history.available()) {
						actions.append(el('button', { className: 'tb', textContent: 'Look in Backups', title: 'If you took a backup of this mod, you can put it back', onclick: () => { MS.dialogs.close(); MS.history.open(p.path); } }));
					}
				}
				actions.append(el('button', { className: 'tb', textContent: 'Ignore', onclick: () => { hidden.add(p.module + '|' + p.path); draw(); } }));
				list.append(el('div', { className: 'prob-row' }, el('div', { className: 'prob-text' }, el('b', { textContent: describe(p) }), el('small', { textContent: p.missing ? 'The game would stop loading this mod at that point. Either put the file back, or take it out of the list.' : p.message })), actions));
			}
			if (found.length) {
				list.append(el('div', { className: 'prob-group', textContent: 'Worth fixing before you share it' }));
				for (const p of found) {
					const key = p.module + '|' + p.path + ':' + p.line + ':' + p.message;
					list.append(el('div', { className: 'prob-row ' + p.severity },
						el('div', { className: 'prob-text' }, el('b', { textContent: sayCheck(p.message) }), el('small', { textContent: p.module + ' · ' + p.path.split('/').slice(1).join('/') + (p.line ? ', line ' + p.line : '') })),
						el('div', { className: 'prob-actions' },
							p.fix ? el('button', { className: 'tb primary', textContent: p.fix.label, title: 'Moves one file\'s line in the mod\'s list so it is read first', onclick: async () => {
								if (await p.fix.run()) {
									checks.set(p.module, (checks.get(p.module) || []).filter((q) => q !== p));
									draw();
									app().toast('Done: the mod reads them in the right order now.', 'good');
								}
							} }) : null,
							el('button', { className: p.fix ? 'tb' : 'tb primary', textContent: 'Show me', title: 'Open the file at that line', onclick: () => { MS.dialogs.close(); MS.ide.openFile(p.path, p.line); } }),
							el('button', { className: 'tb', textContent: 'Ignore', onclick: () => { hidden.add(key); draw(); } }))));
				}
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

	// ---- the check after saving ----

	let checkTimer = 0;
	const pending = new Set();

	// After a save, look over the mods it touched. Only the ones that are yours; it says something only when it finds something new.
	function afterSave(paths) {
		const a = app();
		for (const p of paths) {
			const module = p.split('/')[0];
			if (module && /\.rte$/i.test(module) && !a.fs.isOfficial(module)) {
				pending.add(module);
			}
		}
		if (!pending.size) {
			return;
		}
		clearTimeout(checkTimer);
		checkTimer = setTimeout(async () => {
			const modules = [...pending];
			pending.clear();
			for (const module of modules) {
				let found = [];
				try {
					found = (await MS.ide.checkModule(module)).filter((p) => !/^Not saved yet/.test(p.message));
				} catch (e) {
					continue; // a check that can't run is not worth a message
				}
				const before = (checks.get(module) || []).filter((p) => p.severity === 'error').length;
				checks.set(module, found);
				const errors = found.filter((p) => p.severity === 'error').length;
				if (errors > before) {
					a.toast(module + ' has ' + (errors === 1 ? 'one thing' : errors + ' things') + ' to fix before it will work.', 'bad', { label: 'See what', run: showProblems });
				}
			}
			updateBadge();
		}, 1500);
	}

	// ---- what each setting does ----

	let help = null; // key -> plain description, read once

	async function loadHelp() {
		if (help === null) {
			try {
				help = await (await fetch('data/property-help.json')).json();
			} catch (e) {
				help = {};
			}
		}
		return help;
	}

	// Gives every labelled setting in the properties a plain-words tooltip (and a dotted underline, so it shows there is one).
	async function decorate() {
		const box = $('inspector');
		if (!box) {
			return;
		}
		const words = await loadHelp();
		for (const label of box.querySelectorAll('.row > label')) {
			if (label.dataset.helped) {
				continue;
			}
			const link = label.classList.contains('code-link');
			const key = link ? label.textContent.trim() : (label.title || '').trim();
			if (words[key]) {
				label.dataset.helped = '1';
				label.classList.add('has-help');
				label.title = words[key] + (link ? '\n\nClick to open it in the code editor.' : '\n\n(' + key + ' in the file)');
			}
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
					MS.app.setLibrary(false);
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

	MS.friendly = { say, showProblems, afterLoad, afterSave, update, updateBadge, decorate, repairProject };
	const badge = $('statusProblems');
	if (badge) {
		badge.onclick = showProblems;
	}
	// The properties are redrawn on every selection and edit: add the tooltips each time, once the redraw settles.
	const inspector = $('inspector');
	if (inspector) {
		let queued = 0;
		new MutationObserver(() => {
			cancelAnimationFrame(queued);
			queued = requestAnimationFrame(decorate);
		}).observe(inspector, { childList: true, subtree: true });
	}
})();
