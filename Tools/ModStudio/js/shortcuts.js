// Cortex Studio: keyboard shortcuts that work everywhere, and the list of them all (F1 or Ctrl+/). Undo and redo go to what the tab on show
// edits: the code editor on a script, a tab's own history where it has one (the Mission Editor's graph), Studio's changes to the mod's files
// on the data editors and the designer, the sprite editor's on a picture. Typing in a field or in the code editor keeps its own undo.
'use strict';
window.MS = window.MS || {};

(function () {
	const $ = (id) => document.getElementById(id);
	const ide = () => MS.ide;
	const app = () => MS.app;

	// Where keys belong to what has the focus: a text field, a list, the code editor.
	const typing = (t) => !!t && ((/^(INPUT|SELECT|TEXTAREA)$/.test(t.tagName) && !['range', 'checkbox', 'radio', 'button'].includes(t.type)) || t.isContentEditable || !!(t.closest && t.closest('.monaco-editor')));

	// Undo (or redo) for the tab on show. True when something took it.
	function undo(redo) {
		const t = ide().active;
		if (!t) {
			return false;
		}
		if (t.kind === 'code') {
			if (ide().editor) {
				ide().editor.trigger('keyboard', redo ? 'redo' : 'undo', null);
			}
			return true;
		}
		if (t.sprite) {
			return false; // the sprite editor has its own (pixel.js)
		}
		if (t.kind === 'form' && (redo ? t.redo : t.undo) && (redo ? t.redo() : t.undo())) {
			return true;
		}
		redo ? app().doRedo() : app().doUndo();
		return true;
	}

	// The tabs the bar shows, in order.
	const shownTabs = () => ide().tabs.filter((t) => (ide().tabShown ? ide().tabShown(t) : true));
	function stepTab(by) {
		const tabs = shownTabs();
		if (tabs.length < 2) {
			return;
		}
		const at = tabs.indexOf(ide().active);
		ide().activate(tabs[(at + by + tabs.length) % tabs.length]);
	}

	// Tabs closed lately, to open again (Ctrl+Shift+T): what the tab was, packed as for another window.
	const closed = [];
	async function remember(tab) {
		if (!MS.popout || !MS.popout.pack) {
			return;
		}
		const packed = await MS.popout.pack(tab);
		if (packed && packed.req && !packed.save && !packed.error) {
			closed.push(packed.req);
			if (closed.length > 20) {
				closed.shift();
			}
		}
	}
	async function reopen() {
		const req = closed.pop();
		if (req) {
			await MS.popout.restore(req);
		} else {
			app().status('No closed tab to open again.');
		}
	}

	const click = (id) => {
		const b = $(id);
		if (b && !b.disabled && !b.hidden) {
			b.click();
			return true;
		}
		return false;
	};
	const setMode = (mode) => ide().setMode(mode);

	// [keys shown, what it does, test(e, k), run(e)]. While typing (a field, the code editor) only the ones in FIELD_OK work: the rest are
	// the field's or the code editor's own keys there (Ctrl+/ comments a line, F1 is the code editor's commands, Ctrl+Shift+L picks every match). The ones handled elsewhere (the designer's, the code editor's)
	// are listed with no test, so the list shows every key.
	const ctrl = (e) => e.ctrlKey || e.metaKey;
	const FIELD_OK = new Set(['Ctrl+W', 'Ctrl+Tab, Ctrl+Page Down', 'Ctrl+Shift+Tab, Ctrl+Page Up', 'Ctrl+1 … Ctrl+9', 'Ctrl+Shift+T', 'Ctrl+E', 'Ctrl+B', 'Ctrl+Shift+E', 'Ctrl+J, Ctrl+`', 'F5', 'Ctrl+,']);
	const SHORTCUTS = [
		['Everywhere'],
		['Ctrl+Z', 'Undo (what the tab on show edits)', (e, k) => ctrl(e) && k === 'z' && !e.shiftKey, () => undo(false)],
		['Ctrl+Y, Ctrl+Shift+Z', 'Redo', (e, k) => ctrl(e) && (k === 'y' || (k === 'z' && e.shiftKey)), () => undo(true)],
		['Ctrl+S', 'Save changed files'],
		['Ctrl+Tab, Ctrl+Page Down', 'Next tab', (e) => ctrl(e) && ((e.key === 'Tab' && !e.shiftKey) || e.key === 'PageDown'), () => stepTab(1)],
		['Ctrl+Shift+Tab, Ctrl+Page Up', 'Previous tab', (e) => ctrl(e) && ((e.key === 'Tab' && e.shiftKey) || e.key === 'PageUp'), () => stepTab(-1)],
		['Ctrl+1 … Ctrl+9', 'Go to that tab (9: the last)', (e) => ctrl(e) && !e.shiftKey && !e.altKey && /^[1-9]$/.test(e.key), (e) => {
			const tabs = shownTabs();
			const tab = e.key === '9' ? tabs[tabs.length - 1] : tabs[Number(e.key) - 1];
			if (tab) {
				ide().activate(tab);
			}
		}],
		['Ctrl+W', 'Close the tab', (e, k) => ctrl(e) && !e.shiftKey && k === 'w', () => ide().active && ide().closeTab(ide().active)],
		['Ctrl+Shift+T', 'Open the last closed tab again', (e, k) => ctrl(e) && e.shiftKey && k === 't', () => reopen()],
		['Ctrl+E', 'Switch between Design and Script', (e, k) => ctrl(e) && !e.shiftKey && k === 'e', () => setMode(ide().mode === 'deep' ? 'artist' : 'deep')],
		['Ctrl+B', 'Hide or show the left menu', (e, k) => ctrl(e) && !e.shiftKey && k === 'b', () => click('btnLib')],
		['Ctrl+Shift+L', 'The browser (units, missions, scenes…)', (e, k) => ctrl(e) && e.shiftKey && k === 'l', () => {
			setMode('deep');
			ide().showView('presets');
			$('libSearch') && $('libSearch').focus();
		}],
		['Ctrl+Shift+E', 'Files', (e, k) => ctrl(e) && e.shiftKey && k === 'e', () => {
			setMode('deep');
			ide().showView('files');
		}],
		['Ctrl+Shift+F', 'Search every file'],
		['Ctrl+P', 'Open a file by name'],
		['Ctrl+J, Ctrl+`', 'Hide or show the console panel', (e) => ctrl(e) && e.key === '`', () => ide().togglePanel()],
		['Ctrl+M', 'Missions and battles', (e, k) => ctrl(e) && !e.shiftKey && k === 'm', () => click('btnMissions')],
		['Ctrl+N', 'Make something new', (e, k) => ctrl(e) && !e.shiftKey && k === 'n', () => click('btnNew')],
		['F5', 'Play: start the game', (e) => e.key === 'F5' && !ctrl(e), () => click('btnRun')],
		['Ctrl+Shift+S', 'Spawn the open (or game-linked) thing in the running game', (e, k) => ctrl(e) && e.shiftKey && k === 's', () => click('btnSpawn')],
		['Ctrl+Shift+P', 'Preview in game', (e, k) => ctrl(e) && e.shiftKey && k === 'p', () => click('btnPreview')],
		['Ctrl+Shift+V', 'Live on or off', (e, k) => ctrl(e) && e.shiftKey && k === 'v', () => click('btnLive')],
		['Ctrl+,', 'Settings (other programs for pictures, scripts…)', (e) => ctrl(e) && e.key === ',', () => MS.external && MS.external.dialog()],
		['F1, Ctrl+/', 'This list', (e) => e.key === 'F1' || (ctrl(e) && e.key === '/'), () => list()],
		['The designer'],
		['Space', 'Play or pause the animation'],
		['1 … 5', 'Stand, walk, run, crouch, jump'],
		['F', 'Face left or right'],
		['Arrows (Shift: 10)', 'Move the picked part a pixel'],
		['Delete', 'Remove the picked part'],
		['I', 'Show only the picked part'],
		['K (Shift+K)', 'Skeleton (its pictures)'],
		['L', 'Hide or show the list'],
		['V', 'Hide or show the unit view'],
		['Esc', 'Pick the body'],
		['The Mission Editor graph'],
		['Delete', 'Remove the picked nodes'],
		['Ctrl+C, Ctrl+V, Ctrl+D', 'Copy, paste, duplicate nodes'],
		['F', 'Show the whole graph'],
		['Right-click', 'Add a node'],
		['Scripts'],
		['Ctrl+Click, F12', 'Go to where it is defined'],
		['Ctrl+Space', 'Suggestions'],
		['Ctrl+Shift+O', 'Go to a function or block'],
		['Ctrl+F, Ctrl+H', 'Find, replace in the file'],
	];

	function list() {
		const body = document.createElement('div');
		body.className = 'keys-list';
		for (const [keys, what] of SHORTCUTS) {
			if (what === undefined) {
				const h = document.createElement('h3');
				h.textContent = keys;
				body.append(h);
				continue;
			}
			const row = document.createElement('div');
			row.className = 'keys-row';
			const k = document.createElement('span');
			k.className = 'keys-k';
			for (const part of keys.split(', ')) {
				const kbd = document.createElement('kbd');
				kbd.textContent = part;
				k.append(kbd);
			}
			const w = document.createElement('span');
			w.textContent = what;
			row.append(k, w);
			body.append(row);
		}
		MS.dialogs.open('Keyboard shortcuts', body, [{ text: 'Close', primary: true }], true);
	}

	// First in line (before the code editor's and the designer's own handlers): the shortcuts above, unless the keys belong to a field or
	// the code editor (where undo, tabs and the rest are its own).
	window.addEventListener('keydown', (e) => {
		if (!app() || !ide() || !ide().tabs) {
			return;
		}
		const modal = $('modal') && !$('modal').hidden;
		const k = (e.key || '').toLowerCase();
		for (const [keys, , test, run] of SHORTCUTS) {
			if (!test || !test(e, k)) {
				continue;
			}
			// in a field or the code editor most keys are theirs (undo too); a dialog keeps its keys
			if ((typing(e.target) && !FIELD_OK.has(keys)) || modal) {
				return;
			}
			if (run(e) === false) {
				return; // nobody here took it (the sprite editor's undo): its own handler does
			}
			e.preventDefault();
			e.stopImmediatePropagation();
			return;
		}
	}, true);

	MS.shortcuts = { undo, remember, reopen, list, typing };
})();
