// Cortex Studio: tabs in other windows. Any tab can go in its own window (the ⧉ button, or dragged out of the window), and be dragged from one
// window's tab bar into another's. A window opened for a tab reads what to show from its address (?popout=...), loads Studio as usual, and shows
// just its tabs, with none of the rest of the screen; outside the app (a browser, the headless bridge) the same address opens in a browser tab.
//
// What a tab is, and how to rebuild it somewhere else, is said once per kind of tab in KINDS (register() adds one): pack(tab) gives a small
// request (what goes in the address) and, for what does not fit there (unsaved text, an editor's view), a hand-off that the new window collects
// from this one over the windows' channel. Every data editor (editors.js) and the Mission Editor open through MS.editors, so a new editor can
// go in its own window with nothing more written for it.
//
// The windows talk over one BroadcastChannel: who has a picture open, pictures saved, hand-offs, and a tab taken from one window into another.
'use strict';
window.MS = window.MS || {};

(function () {
	const params = new URLSearchParams(location.search);
	let request = null;
	try {
		request = params.get('popout') ? JSON.parse(params.get('popout')) : null;
	} catch (e) {
		request = null;
	}
	// this window's name: the app's own for a pop-out ("popout-3"), or a made-up one (the main window, a browser tab)
	const winId = params.get('win') || 'w-' + Math.random().toString(36).slice(2, 10);

	let channel = null;
	const waiting = new Map(); // question id -> resolve
	const handoffs = new Map(); // hand-off id -> what the new window collects
	let asked = 0;
	const newId = () => winId + ':' + Date.now() + ':' + ++asked;

	// Asks the other windows something and waits a moment for the answer (null when nobody answers).
	function ask(message, ms) {
		if (!channel) {
			return Promise.resolve(null);
		}
		const id = newId();
		return new Promise((resolve) => {
			waiting.set(id, resolve);
			channel.postMessage(Object.assign({}, message, { id, from: winId }));
			setTimeout(() => {
				if (waiting.has(id)) {
					waiting.delete(id);
					resolve(null);
				}
			}, ms || 300);
		});
	}
	const answer = (m, body) => channel && channel.postMessage(Object.assign({ type: 'answer', id: m.id, to: m.from }, body));

	try {
		channel = new BroadcastChannel('cortex-studio');
		channel.onmessage = async (e) => {
			const m = e.data || {};
			if (m.type === 'answer') {
				if (m.to === winId && waiting.has(m.id)) {
					const resolve = waiting.get(m.id);
					waiting.delete(m.id);
					resolve(m);
				}
			} else if (m.type === 'has?' && MS.ide && MS.ide.findSpriteTab && MS.ide.findSpriteTab(m.key)) {
				// another window asks who has a picture open: the one that does answers, and comes to the front
				answer(m, { has: true });
				focusWindow();
			} else if (m.type === 'sprites' && Array.isArray(m.paths) && MS.app && MS.app.forgetSprites) {
				MS.app.forgetSprites(m.paths);
				MS.panels && MS.panels.forgetThumbs && MS.panels.forgetThumbs();
				MS.app.requestDraw();
			} else if (m.type === 'handoff?' && handoffs.has(m.key)) {
				answer(m, { payload: handoffs.get(m.key) });
				handoffs.delete(m.key);
			} else if (m.type === 'take' && m.win === winId) {
				// another window wants one of this window's tabs (dropped on its tab bar): give it what it needs, and let the tab go
				const tab = MS.ide.tabs.find((t) => t.id === m.tab);
				const packed = tab ? await pack(tab) : { error: 'That tab has gone.' };
				if (packed.error || packed.save) {
					answer(m, { error: packed.error || packed.save.message });
					return;
				}
				answer(m, { req: packed.req, payload: packed.payload || null });
				letGo(tab);
			} else if (m.type === 'docs' && m.from !== winId && MS.app && MS.app.applySharedDocs) {
				MS.app.applySharedDocs(m.docs); // another window's changes, as it makes them
			} else if (m.type === 'game-link' && m.from !== winId && MS.ide && MS.ide.gameTakenElsewhere) {
				MS.ide.gameTakenElsewhere();
			} else if (m.type === 'dragging') {
				lastDrag = { win: m.from, tab: m.tab, at: Date.now() };
			} else if (m.from !== winId && MS.news && typeof MS.news[m.type] === 'function') {
				MS.news[m.type](m); // a kind of news another part of Studio listens for (MS.news[type] = fn, set when it loads)
			}
		};
	} catch (e) {
		channel = null;
	}
	let lastDrag = null; // the tab another window is dragging (for when the drop itself carries nothing)

	function focusWindow() {
		try {
			window.focus();
		} catch (err) {
			// the window stays where it is
		}
	}

	// ---- the kinds of tab ----
	// Each: match(tab), pack(tab) -> { req, payload } (or { error } when it cannot go, or { save: { message, run } } when it can once saved),
	// and restore(req, payload) to open it in this window. req.k names the kind.

	const KINDS = [];
	function register(k, spec) {
		KINDS.push(Object.assign({ k }, spec));
	}
	const kindOf = (tab) => KINDS.find((x) => x.match(tab)) || null;

	// An editor's view (what is picked, filtered, scrolled) as plain data: anything that is not (a map on the page, a timer) is left out.
	function plain(value, depth) {
		if (value === null || ['string', 'number', 'boolean'].includes(typeof value)) {
			return value;
		}
		if ((depth || 0) > 6 || typeof value !== 'object') {
			return undefined;
		}
		if (Array.isArray(value)) {
			return value.map((v) => plain(v, (depth || 0) + 1)).filter((v) => v !== undefined);
		}
		if (Object.getPrototypeOf(value) !== Object.prototype) {
			return undefined;
		}
		const out = {};
		for (const [k, v] of Object.entries(value)) {
			const p = plain(v, (depth || 0) + 1);
			if (p !== undefined) {
				out[k] = p;
			}
		}
		return out;
	}

	const saveDocsFirst = (docs, what) => ({ save: { message: what + ' has changes that are not saved yet. Save them, and it moves.', run: () => MS.app.save(true, docs) } });

	register('sprite', {
		match: (tab) => !!tab.sprite,
		pack: (tab) => {
			if (!tab.px || !tab.px.file) {
				return { error: 'A new picture has no file yet. Save it first, then it can go in another window.' };
			}
			if (tab.px.dirty) {
				return { save: { message: 'The picture has changes that are not saved yet. Save it, and it moves.', run: () => MS.pixel.saveTab(tab.px) } };
			}
			return { req: { k: 'sprite', file: tab.px.file, frames: tab.px.frames.length } };
		},
		restore: (req) => MS.pixel.open({ file: req.file, frameCount: req.frames || 1, inStudio: true }),
	});

	register('file', {
		// a script or another plain file: unsaved text goes with it
		match: (tab) => tab.kind === 'code' && !tab.doc,
		pack: (tab) => {
			const dirty = MS.ide.tabIsDirty(tab);
			const view = MS.ide.editor && MS.ide.active === tab ? MS.ide.editor.getPosition() : null;
			return { req: { k: 'file', path: tab.path, line: view ? view.lineNumber : undefined }, payload: dirty ? { text: tab.model.getValue(), saved: tab.savedText } : null };
		},
		restore: async (req, payload) => {
			MS.ide.setMode('deep', true);
			await MS.ide.loadMonaco(); // a window just opened has no code editor yet (opening would only be put off)
			await MS.ide.openFile(req.path, req.line, undefined, { inStudio: true });
			const tab = MS.ide.tabs.find((t) => t.kind === 'code' && t.path && t.path.toLowerCase() === req.path.toLowerCase().replace(/\\/g, '/').replace(/^(data|mods)\//, ''));
			if (payload && tab && tab.model && typeof payload.text === 'string') {
				tab.model.pushEditOperations([], [{ range: tab.model.getFullModelRange(), text: payload.text }], () => null);
				MS.ide.renderTabs();
			}
		},
	});

	register('ini', {
		// an INI file Studio has read: its document is shared with the library, so it moves once saved
		match: (tab) => tab.kind === 'code' && !!tab.doc,
		pack: (tab) => (tab.doc.dirty ? saveDocsFirst([tab.doc], tab.path.split('/').pop()) : { req: { k: 'file', path: tab.path } }),
		restore: null, // opened as a file
	});

	register('image', {
		match: (tab) => tab.kind === 'image',
		pack: (tab) => ({ req: { k: 'image', path: tab.path } }),
		restore: (req) => MS.ide.openFile(req.path, undefined, undefined, { inStudio: true }),
	});

	register('editor', {
		// every data editor and the Mission Editor: which one, for which mod or scenario, and its view
		match: (tab) => tab.kind === 'form' && !tab.sprite && !!tab.editor,
		pack: (tab) => ({ req: { k: 'editor', id: tab.editor.id, module: tab.editor.module }, payload: tab.state ? { state: plain(tab.state) } : null }),
		restore: (req, payload) => {
			MS.editors.open(req.id, req.module);
			const tab = MS.ide.tabs.find((t) => t.kind === 'form' && t.editor && t.editor.id === req.id && t.editor.module === req.module) || MS.ide.active;
			if (payload && payload.state && tab && tab.kind === 'form') {
				tab.state = Object.assign(tab.state || {}, payload.state);
				MS.ide.refreshForm();
			}
		},
	});

	register('design', {
		match: (tab) => tab.kind === 'designer',
		pack: (tab) => {
			const current = tab === MS.ide.currentDesigner ? MS.app.actor : tab.state && tab.state.actor;
			if (!current) {
				return { error: 'Open something in the designer first.' };
			}
			const dirty = MS.app.dirtyDocs();
			if (dirty.length) {
				return saveDocsFirst(dirty, current.name);
			}
			return { req: { k: 'design', path: current.path, name: current.name, cls: current.cls } };
		},
		restore: (req) => {
			const a = MS.app;
			const preset = a.lib.presets.find((p) => p.topLevel && p.doc.path === req.path && p.name === req.name && p.cls === req.cls);
			if (!preset) {
				a.toast('Studio cannot find ' + req.name + ' in ' + req.path + ' to show it here.', 'bad');
				return;
			}
			if (!request) {
				MS.ide.setMode('design', true); // a pop-out takes the designer's layout as the tab comes up
			}
			a.openActor(preset, { newTab: !request });
		},
	});

	const restorerFor = (req) => {
		const k = req.k === 'file' ? 'file' : req.k;
		return (KINDS.find((x) => x.k === k && x.restore) || {}).restore || null;
	};

	// Whether a tab can go to another window at all (the ⧉ button shows when it can).
	function canMove(tab) {
		if (tab.kind === 'designer' && MS.ide.tabs.filter((t) => t.kind === 'designer').length < 2 && !request) {
			return !!kindOf(tab); // the only designer is copied, not moved
		}
		return !!kindOf(tab);
	}

	async function pack(tab) {
		const kind = kindOf(tab);
		if (!kind) {
			return { error: 'This tab can\'t go to another window.' };
		}
		try {
			return (await kind.pack(tab)) || { error: 'This tab can\'t go to another window.' };
		} catch (e) {
			return { error: 'This tab can\'t go to another window: ' + (e.message || e) };
		}
	}

	// A tab whose work is now in another window closes here; the only designer stays (the window keeps its place).
	function letGo(tab) {
		if (tab.kind !== 'designer' || MS.ide.tabs.filter((t) => t.kind === 'designer').length > 1 || request) {
			MS.ide.closeTab(tab, true);
		}
	}

	// When a tab has unsaved work that must be saved before it can move: asks, saves, and goes on.
	function saveThen(save, then) {
		MS.dialogs.open('Save first?', MS.dialogs.parts ? MS.dialogs.parts.field('', document.createTextNode(save.message)) : document.createTextNode(save.message), [
			{ text: 'Cancel' },
			{ text: 'Save and move it', primary: true, action: async () => {
				await save.run();
				then();
				return true;
			} },
		]);
	}

	// ---- opening a window for a tab ----

	async function openWindow(req) {
		const a = MS.app;
		const text = JSON.stringify(req);
		let native = false;
		try {
			const response = await a.fs.api('POST', 'popout', '', JSON.stringify({ what: text }));
			native = !!(response && (await response.json()).native);
		} catch (e) {
			native = false;
		}
		if (!native) {
			const url = new URL(location.href);
			url.searchParams.set('popout', text);
			url.searchParams.delete('win');
			window.open(url.toString(), '_blank');
		}
	}

	// Moves a tab into its own window.
	async function pop(tab) {
		const packed = await pack(tab);
		if (packed.save) {
			saveThen(packed.save, () => pop(tab));
			return;
		}
		if (packed.error) {
			MS.app.toast(packed.error, 'bad');
			return;
		}
		const req = Object.assign({}, packed.req);
		if (packed.payload) {
			req.h = newId();
			handoffs.set(req.h, packed.payload);
			setTimeout(() => handoffs.delete(req.h), 60000);
		}
		await openWindow(req);
		letGo(tab);
	}

	// Opens what a request asks for in this window: a pop-out's own tab as it starts, or a tab dropped in from another window.
	async function restore(req, payload) {
		const a = MS.app;
		const run = restorerFor(req);
		if (!run) {
			a.toast('This window doesn\'t know how to show that tab.', 'bad');
			return;
		}
		try {
			await run(req, payload || null);
		} catch (e) {
			a.toast('Couldn\'t open that here: ' + (e.message || e), 'bad');
		}
		focusWindow();
	}

	// Before the page loads: a pop-out shows only its tabs.
	function begin() {
		if (request) {
			document.getElementById('app').classList.add('popout');
			document.getElementById('app').classList.toggle('popout-design', request.k === 'design');
			document.title = 'Cortex Studio';
		}
	}

	// After the game is loaded: a pop-out opens what it was asked for (collecting what did not fit in its address from the window that sent it).
	async function run() {
		let payload = null;
		if (request.h) {
			const got = await ask({ type: 'handoff?', key: request.h }, 3000);
			payload = got ? got.payload : null;
		}
		await restore(request, payload);
		const tab = MS.ide.active;
		if (tab && tab.title) {
			document.title = tab.title + ' · Cortex Studio';
		} else if (tab && tab.path) {
			document.title = tab.path.split('/').pop() + ' · Cortex Studio';
		}
	}

	// ---- dragging tabs: along the bar, into another window, or out of every window into a new one ----

	const MIME = 'application/x-cortex-tab';
	let dragging = null; // this window's tab being dragged

	function dragStart(e, tab) {
		dragging = { tab, droppedHere: false };
		e.dataTransfer.effectAllowed = 'move';
		const data = JSON.stringify({ win: winId, tab: tab.id });
		e.dataTransfer.setData(MIME, data);
		e.dataTransfer.setData('text/plain', 'cortex-tab:' + data);
		if (channel) {
			channel.postMessage({ type: 'dragging', from: winId, tab: tab.id });
		}
	}

	// The drag ended: dropped outside every window, the tab goes into a new one of its own.
	function dragEnd(e, tab) {
		const d = dragging;
		dragging = null;
		if (!d || d.droppedHere || e.dataTransfer.dropEffect !== 'none' || !canMove(tab)) {
			return;
		}
		const outside = e.screenX < window.screenX || e.screenX > window.screenX + window.outerWidth || e.screenY < window.screenY || e.screenY > window.screenY + window.outerHeight;
		if (outside && MS.ide.tabs.includes(tab)) {
			pop(tab);
		}
	}

	// What is being dragged over a tab bar: { win, tab } for a Studio tab, or null.
	function dragged(e) {
		const types = Array.from(e.dataTransfer.types || []);
		if (types.includes(MIME) || types.includes('text/plain')) {
			try {
				const raw = e.dataTransfer.getData(MIME) || (e.dataTransfer.getData('text/plain') || '').replace(/^cortex-tab:/, '');
				if (raw) {
					return JSON.parse(raw);
				}
			} catch (err) {
				// fall through
			}
			if (dragging) {
				return { win: winId, tab: dragging.tab.id };
			}
			if (lastDrag && Date.now() - lastDrag.at < 30000) {
				return { win: lastDrag.win, tab: lastDrag.tab };
			}
		}
		return null;
	}
	const isTabDrag = (e) => {
		const types = Array.from(e.dataTransfer.types || []);
		return !!dragging || types.includes(MIME) || (types.includes('text/plain') && !!lastDrag && Date.now() - lastDrag.at < 30000);
	};

	// A tab dropped on this window's tab bar, before the tab "before" (or at the end): moved along the bar, or taken from the window it was in.
	async function drop(e, before) {
		const what = dragged(e);
		if (!what) {
			return false;
		}
		e.preventDefault();
		if (what.win === winId) {
			if (dragging) {
				dragging.droppedHere = true;
			}
			const tab = MS.ide.tabs.find((t) => t.id === what.tab);
			if (tab && tab !== before) {
				const list = MS.ide.tabs;
				list.splice(list.indexOf(tab), 1);
				list.splice(before ? list.indexOf(before) : list.length, 0, tab);
				MS.ide.renderTabs();
			}
			return true;
		}
		lastDrag = null;
		const got = await ask({ type: 'take', win: what.win, tab: what.tab }, 4000);
		if (!got) {
			MS.app.toast('The window that tab was in did not answer.', 'bad');
		} else if (got.error) {
			MS.app.toast(got.error, 'bad');
		} else {
			await restore(got.req, got.payload);
		}
		return true;
	}

	// ---- the rest ----

	// Whether another window has this sprite tab open (it answers within a moment, or nobody has).
	async function elsewhere(key) {
		const got = await ask({ type: 'has?', key }, 200);
		return !!(got && got.has);
	}

	function saved(paths) {
		if (channel && paths && paths.length) {
			channel.postMessage({ type: 'sprites', paths });
		}
	}

	// A pop-out with nothing left in it closes: Studio's app closes its own window (named in the address); a browser tab closes itself.
	async function closeWindow() {
		const win = params.get('win');
		try {
			if (win && MS.app && MS.app.fs) {
				const response = await MS.app.fs.api('POST', 'popout-close', '', JSON.stringify({ win }));
				if (response && (await response.json()).closed) {
					return;
				}
			}
		} catch (e) {
			// fall back to the browser's own
		}
		window.close();
	}

	// Tells the other windows something (no answer wanted): 'game-link' when this window takes the game.
	function announce(type, extra) {
		if (channel) {
			channel.postMessage(Object.assign({}, extra || {}, { type, from: winId }));
		}
	}

	MS.popout = { announce, request, winId, begin, run, pop, restore, register, canMove, pack, saved, elsewhere, closeWindow, dragStart, dragEnd, drop, isTabDrag };
})();
