// Cortex Studio: the Studio settings menu (⚙ in the top bar). Settings are kept in this browser's storage, one key each; a part of Studio
// adds its own with section() and reads them with get(); on() hears a setting change. The first: how the left menu shows the kinds of thing
// (a grid of them all, one dropdown, or some always shown and the rest in a dropdown). Then the other programs files open in, the theme, and
// the keyboard shortcuts.
'use strict';
window.MS = window.MS || {};

(function () {
	const PREFIX = 'ms.setting.';
	const listeners = new Map(); // setting -> [fn]
	const SECTIONS = [];

	function get(name, fallback) {
		try {
			const raw = localStorage.getItem(PREFIX + name);
			return raw === null ? fallback : JSON.parse(raw);
		} catch (e) {
			return fallback;
		}
	}
	function set(name, value) {
		try {
			localStorage.setItem(PREFIX + name, JSON.stringify(value));
		} catch (e) {
			// kept for this run only
		}
		for (const fn of listeners.get(name) || []) {
			fn(value);
		}
	}
	function on(name, fn) {
		listeners.set(name, [...(listeners.get(name) || []), fn]);
	}

	// A section of the menu: { id, title, render(body, redraw) }, in the order added.
	function section(spec) {
		const at = SECTIONS.findIndex((s) => s.id === spec.id);
		if (at >= 0) {
			SECTIONS[at] = spec;
		} else {
			SECTIONS.push(spec);
		}
	}

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
	const radio = (name, value, current, label, note, onPick) => {
		const input = el('input', { type: 'radio', name, value, checked: value === current, onchange: () => onPick(value) });
		return el('label', { className: 'set-choice' }, input, el('span', {}, el('b', { textContent: label }), note ? el('small', { textContent: note }) : null));
	};

	// ---- the left menu's kinds of thing ----

	const BROWSE_DEFAULT = { mode: 'grid', pinned: ['units', 'items'] };
	const browseLayout = () => Object.assign({}, BROWSE_DEFAULT, get('browseLayout', BROWSE_DEFAULT));
	section({
		id: 'browse', title: 'Left menu: kinds of thing',
		render(body, redraw) {
			const layout = browseLayout();
			const pick = (mode) => {
				set('browseLayout', Object.assign(browseLayout(), { mode }));
				redraw();
			};
			body.append(
				radio('setBrowse', 'grid', layout.mode, 'All as buttons', 'Units, craft, items… each a button, as a grid', pick),
				radio('setBrowse', 'menu', layout.mode, 'One dropdown', 'A single list to pick from: the most room for the list below', pick),
				radio('setBrowse', 'mix', layout.mode, 'Some as buttons, the rest in a dropdown', 'Tick the ones that always show', pick));
			if (layout.mode === 'mix') {
				const cats = MS.panels && MS.panels.categories ? MS.panels.categories() : [];
				body.append(el('div', { className: 'set-pins' }, ...cats.map((c) => {
					const box = el('input', { type: 'checkbox', checked: layout.pinned.includes(c.id), onchange: () => {
						const now = browseLayout();
						const pinned = box.checked ? [...new Set([...now.pinned, c.id])] : now.pinned.filter((x) => x !== c.id);
						set('browseLayout', Object.assign(now, { pinned: cats.map((x) => x.id).filter((id) => pinned.includes(id)) }));
					} });
					return el('label', { className: 'set-pin' }, box, el('span', { textContent: c.icon + ' ' + c.title }));
				})));
			}
		},
	});

	section({
		id: 'programs', title: 'Other programs',
		render(body) {
			const choices = ['image', 'script', 'ini', 'sound'].map((k) => MS.external && MS.external.programFor(k)).filter(Boolean);
			body.append(el('p', { className: 'set-note', textContent: choices.length ? 'Some files open in: ' + [...new Set(choices.map((c) => c.name))].join(', ') + '.' : 'Every file opens in Studio\'s own editors.' }),
				el('button', { className: 'tb', textContent: 'Open files in other programs… (Ctrl+,)', onclick: () => {
					close();
					MS.external.dialog();
				} }));
		},
	});

	// The game Studio starts: the medieval factions, and mods, switched off. They stay off in that game (written into its settings at each
	// launch) and are hidden across Studio: the library, the pickers, the faction lists.
	section({
		id: 'game', title: 'Game options',
		render(body, redraw) {
			const changed = () => {
				if (MS.panels && MS.app && MS.app.lib) {
					MS.panels.showLibrary();
				}
				redraw();
			};
			const medieval = el('input', { type: 'checkbox', checked: !!get('game.disableMedieval', false) });
			medieval.onchange = () => {
				set('game.disableMedieval', medieval.checked);
				changed();
			};
			body.append(el('label', { className: 'set-check', title: 'Kingdom (Medieval.rte) and Fell Legion (FellLegion.rte): no longer factions and not for sale in the game, and hidden in Studio. Their presets stay loaded, since other parts of the game use them.' },
				medieval, 'Switch off the medieval factions (Kingdom and Fell Legion)'));
			const mods = MS.app && MS.app.lib ? MS.app.lib.modules.filter((m) => !m.official).map((m) => m.name).sort((a, b) => a.localeCompare(b)) : [];
			const off = new Set((get('game.disabledMods', []) || []).map((m) => String(m).toLowerCase()));
			body.append(el('p', { className: 'set-note', textContent: mods.length ? 'Mods switched off (not loaded by the game Studio starts, and hidden here; the mod you\'re working on always shows):' : 'No mods loaded.' }));
			const list = el('div', { className: 'set-mods' });
			for (const name of mods) {
				const box = el('input', { type: 'checkbox', checked: off.has(name.toLowerCase()) });
				box.onchange = () => {
					const now = (get('game.disabledMods', []) || []).filter((m) => String(m).toLowerCase() !== name.toLowerCase());
					if (box.checked) {
						now.push(name);
					}
					set('game.disabledMods', now);
					changed();
				};
				list.append(el('label', { className: 'set-check' }, box, name.replace(/\.rte$/i, '')));
			}
			body.append(list);
		},
	});

	section({
		id: 'look', title: 'Look',
		render(body) {
			body.append(el('button', { className: 'tb', textContent: '◐ Theme: colours, fonts and size…', onclick: () => {
				close();
				const theme = document.getElementById('btnTheme');
				if (theme) {
					theme.click();
				}
			} }));
		},
	});

	section({
		id: 'keys', title: 'Keyboard',
		render(body) {
			body.append(el('button', { className: 'tb', textContent: 'Keyboard shortcuts… (F1)', onclick: () => {
				close();
				MS.shortcuts.list();
			} }));
		},
	});

	// ---- the menu ----

	let panel = null;
	let button = null;
	function draw() {
		panel.replaceChildren(el('div', { className: 'set-head' }, el('b', { textContent: 'Studio settings' })), ...SECTIONS.map((s) => {
			const body = el('div', { className: 'set-body' });
			s.render(body, draw);
			return el('section', { className: 'set-section' }, el('h4', { textContent: s.title }), body);
		}));
	}
	function close() {
		if (panel) {
			panel.hidden = true;
		}
	}
	function open() {
		if (!panel) {
			panel = el('div', { className: 'ctx-menu settings-panel', hidden: true });
			document.body.append(panel);
			document.addEventListener('pointerdown', (e) => {
				if (!panel.hidden && !panel.contains(e.target) && e.target !== button) {
					close();
				}
			}, true);
			document.addEventListener('keydown', (e) => {
				if (e.key === 'Escape') {
					close();
				}
			});
		}
		if (!panel.hidden) {
			close();
			return;
		}
		draw();
		panel.hidden = false;
		const r = button.getBoundingClientRect();
		panel.style.top = r.bottom + 4 + 'px';
		panel.style.left = Math.max(8, Math.min(r.right - panel.offsetWidth, innerWidth - panel.offsetWidth - 8)) + 'px';
	}

	function mountButton() {
		const help = document.getElementById('btnHelp');
		if (!help || document.getElementById('btnSettings')) {
			return;
		}
		button = el('button', { className: 'tb icon', id: 'btnSettings', title: 'Studio settings', textContent: '⚙', onclick: () => open() });
		help.parentNode.insertBefore(button, help);
	}
	if (document.readyState === 'loading') {
		document.addEventListener('DOMContentLoaded', mountButton);
	} else {
		setTimeout(mountButton, 0);
	}

	MS.settings = { get, set, on, section, open, close, browseLayout };
})();
