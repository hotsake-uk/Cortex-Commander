// Cortex Mod Studio: the app. Holds the open game folder, the unit being designed, edits with undo, saving and the in-game preview.
'use strict';
window.MS = window.MS || {};

(function () {
	const $ = (id) => document.getElementById(id);
	const { rotate, flipX, sub, add } = MS.geom;
	const DEVICE_CLASSES = new Set(['HDFirearm', 'HeldDevice', 'TDExplosive', 'ThrownDevice']);
	const PREVIEW_FILE = 'ModStudio/Preview.ini';

	const app = {
		fs: null,
		lib: null,
		images: null,

		undo: [],
		redo: [],
		txn: null,
		frames: new Map(),
		savedSinceLastPreview: new Set(),
	};
	MS.app = app;

	// ---- designers ----
	// What one designer shows and how: the thing open and its rig, what is picked, hidden and folded, the pose. Each design tab has its own
	// (ide.js); app.actor, app.rig, app.selected and the rest are the designer on show, so switching tabs points at another designer rather
	// than copying one designer's state in and out. Two can be on screen at once, side by side (the views, below).
	const DESIGNER_KEYS = ['rig', 'actor', 'actorNode', 'device', 'selected', 'selectedPiece', 'hidden', 'soloed', 'collapsed', 'isolate', 'poseState', 'groundY'];
	app.newDesigner = () => ({
		rig: null,
		actor: null, // {path, name, cls}: found again by these after undo, since undo re-reads the file
		actorNode: null,
		device: null, // {path, name} of the held item preset shown in the front hand
		selected: null,
		selectedPiece: null,
		hidden: new Set(), // ids of parts hidden in the view (the view only: the unit's files are untouched)
		soloed: new Set(), // ids of parts soloed: while any is, only they (and what hangs from them) are shown
		collapsed: new Set(), // ids of parts whose children are folded away in the parts list
		isolate: false,
		poseState: { flip: false, aim: 0, pose: 'stand', time: 0, sharp: false, frames: {}, animateSprites: true },
		groundY: null,
		viewState: null, // the zoom and pan it was last seen with
	});
	app.designer = app.newDesigner();
	for (const key of DESIGNER_KEYS) {
		Object.defineProperty(app, key, { get: () => app.designer[key], set: (v) => { app.designer[key] = v; }, enumerable: true, configurable: true });
	}
	// playing or paused, and how fast: each design tab's own, like its pose
	Object.defineProperty(app, 'playing', { get: () => !app.designer.poseState.paused, set: (v) => { app.designer.poseState.paused = !v; }, enumerable: true, configurable: true });
	Object.defineProperty(app, 'speed', { get: () => app.designer.poseState.speed || 1, set: (v) => { app.designer.poseState.speed = v; }, enumerable: true, configurable: true });

	// Runs fn with another designer (and its view) as the one on show: to draw it, or bring it up to date, while it is beside the one you edit.
	app.withDesigner = function (designer, fn, view) {
		const before = app.designer;
		const beforeView = app.view;
		app.designer = designer;
		if (view) {
			app.view = view;
		}
		try {
			return fn();
		} finally {
			app.designer = before;
			app.view = beforeView;
		}
	};

	// ---- start-up ----

	app.init = function () {
		MS.popout.begin();
		app.view = new MS.Viewport($('view'), app);
		setUpPanes();
		MS.panels.init(app);
		MS.ide.init(app);
		bindToolbar();
		bindKeys();
		window.addEventListener('beforeunload', (e) => {
			if (!MS.leaving && (app.dirtyDocs().length || MS.ide.dirtyCount())) {
				e.preventDefault();
				e.returnValue = '';
			}
		});
		const note = $('openNote');
		const token = MS.GameFS.serverToken;
		if (token) {
			// Started by Mod Studio.bat or the launcher: the game folder is already chosen.
			$('btnOpen').hidden = true;
			const fs = new MS.GameFS();
			loadGame(() => fs.useServer(token), fs);
		} else {
			note.textContent = 'Opened straight from disk, Studio can read the game folder but not save into it: your changes download as a zip to unpack over it. To save straight into the game, start Mod Studio.bat in Tools\\ModStudio, or use the launcher\'s Mod Studio button.';
		}
		$('btnOpen').onclick = () => openFolder();
		$('folderInput').onchange = async (e) => {
			if (e.target.files.length) {
				const fs = new MS.GameFS();
				await loadGame(() => fs.useFileList(e.target.files), fs);
			}
		};
		rememberedFolder().then((handle) => {
			if (handle) {
				const btn = $('btnReopen');
				btn.hidden = false;
				btn.textContent = 'Reopen ' + handle.name;
				btn.onclick = async () => {
					const fs = new MS.GameFS();
					await loadGame(() => fs.useHandle(handle), fs);
				};
			}
		});
		loop();
	};

	async function openFolder() {
		if (!MS.GameFS.canWrite) {
			$('folderInput').click();
			return;
		}
		const fs = new MS.GameFS();
		try {
			await fs.pickFolder();
		} catch (e) {
			if (e.name !== 'AbortError') {
				app.toast(e.message, 'bad');
			}
			return;
		}
		await loadGame(null, fs);
		rememberFolder(fs.root);
	}

	async function loadGame(open, fs) {
		const progress = $('loadProgress');
		progress.hidden = false;
		const report = (text, p) => {
			progress.querySelector('span').textContent = text;
			progress.querySelector('.bar').style.setProperty('--p', Math.round(p * 100) + '%');
		};
		try {
			report('Opening the folder…', 0);
			if (open) {
				await open();
			}
			app.fs = fs;
			app.images = new MS.Images(fs);
			await app.images.loadPalette();
			app.lib = new MS.Library(fs);
			app.lib.onDocDirty = () => {
				updateFileStatus();
				scheduleAutosave();
			};
			await app.lib.load(report);
			$('btnHistory').hidden = !MS.history.available();
			report('Getting ready…', 1);
			hookDocs();
		} catch (e) {
			progress.hidden = true;
			app.toast(e.message || String(e), 'bad');
			return;
		}
		$('welcome').hidden = true;
		$('statusMode').textContent = fs.writable ? 'Saves go straight to disk' : 'Read-only: saves download as a zip';
		$('statusMode').title = fs.writable ? 'Changes are written to your mod\'s files as soon as you save. The game folder: ' + fs.name : '';
		const units = app.lib.presets.filter((p) => p.cls === 'AHuman' && p.topLevel).length;
		const summary = 'Read ' + app.lib.presets.length + ' presets (' + units + ' units) from ' + app.lib.docs.size + ' files in ' + app.lib.modules.length + ' modules.';
		app.status(summary, 5000);
		if (app.lib.warnings.length) {
			console.warn(app.lib.warnings.join('\n'));
		}
		MS.friendly.afterLoad();
		MS.panels.showLibrary();
		updateScopeButton();
		MS.ide.gameLoaded();
		MS.friendly.update();
		if (MS.popout.request) {
			await MS.popout.run();
			return;
		}
		app.chooseProject(true);
		const last = readLocal('ms.lastActor');
		const preset = last && app.lib.presets.find((p) => p.doc.path === last.path && p.name === last.name);
		if (preset) {
			app.openActor(preset);
		} else {
			app.status(summary + ' Pick a unit on the left to start.', 5000);
		}
	}

	function hookDocs() {
		for (const doc of app.lib.docs.values()) {
			hookDoc(doc);
		}
	}

	function hookDoc(doc) {
		doc.onWillChange = (d) => {
			if (app.txn && !app.txn.before.has(d)) {
				app.txn.before.set(d, d.serialize());
			}
		};
	}
	app.hookDoc = hookDoc;

	// ---- the unit being designed ----

	// ---- which modules the lists show ----
	// A view filter only: every module is still read, so CopyOf and inheritance across modules keep working.

	// ---- the project: the one mod you are working on ----
	// The lists, files and search show just the project's mod. The Library switch (top of the left bar) shows everything instead, to look
	// through and clone from; things you add go into the project. A view filter only: every module is still read, so CopyOf and inheritance
	// across modules keep working.

	app.project = readLocal('ms.project') || null; // a mod's folder name, e.g. MyMod.rte
	app.libraryOn = false;
	app.scope = null; // lower-cased module names the lists show; null is all of them

	function applyScope() {
		app.scope = app.libraryOn || !app.project ? null : new Set([app.project.toLowerCase()]);
	}
	applyScope();

	app.inScope = function (pathOrModule) {
		if (app.hiddenModule(pathOrModule)) {
			return false;
		}
		return !app.scope || app.scope.has(String(pathOrModule).split('/')[0].toLowerCase());
	};

	// A module switched off in Studio's game options (the medieval factions, or a mod): left out of the game Studio starts, and hidden in the
	// library, the pickers and the faction lists (never the mod you're working on). Its presets are still read, for those that copy them.
	const MEDIEVAL = new Set(['medieval.rte', 'felllegion.rte']);
	app.hiddenModule = function (pathOrModule) {
		const module = String(pathOrModule || '').split('/')[0].toLowerCase();
		if (!module || (app.project && module === app.project.toLowerCase()) || !MS.settings) {
			return false;
		}
		if (MEDIEVAL.has(module) && MS.settings.get('game.disableMedieval', false)) {
			return true;
		}
		return (MS.settings.get('game.disabledMods', []) || []).some((m) => String(m).toLowerCase() === module);
	};

	function updateScopeButton() {
		const b = $('btnScope');
		b.textContent = (app.project ? 'Project: ' + app.project.replace(/\.rte$/i, '') : 'Choose a project') + ' ▾';
		b.classList.toggle('on', !!app.project);
		for (const seg of document.querySelectorAll('.scope-seg')) {
			for (const btn of seg.querySelectorAll('button')) {
				btn.classList.toggle('sel', (btn.dataset.lib === 'all') === app.libraryOn);
			}
		}
	}

	function refreshScope() {
		applyScope();
		updateScopeButton();
		if (app.lib) {
			MS.panels.showLibrary();
		}
		MS.ide.scopeChanged();
	}

	app.setLibrary = function (on) {
		app.libraryOn = !!on;
		refreshScope();
	};

	app.setProject = function (name) {
		app.project = name;
		app.libraryOn = false;
		writeLocal('ms.project', name);
		writeLocal('ms.lastMod', name); // new things default to it
		refreshScope();
		MS.friendly.repairProject(name);
		if (app.actor && app.actor.path.split('/')[0].toLowerCase() !== name.toLowerCase() && !app.fs.isOfficial(app.actor.path)) {
			app.status('Now working on ' + name + '.');
		}
	};

	// Your mods with what is in each, for the project picker.
	function projectChoices() {
		const mods = app.fs ? app.fs.modules().filter((m) => m.home === 'Mods').sort((x, y) => x.name.localeCompare(y.name)) : [];
		return mods.map((m) => {
			const own = app.lib.presets.filter((p) => p.module === m.name && p.topLevel);
			const units = own.filter((p) => p.cls === 'AHuman' || p.cls === 'ACrab').length;
			return { name: m.name, text: units + ' unit' + (units === 1 ? '' : 's') + ', ' + (own.length - units) + ' other thing' + (own.length - units === 1 ? '' : 's') };
		});
	}

	async function makeProject(name) {
		const n = MS.dialogs.parts.safeName(name || '').replace(/\.?rte$/i, '');
		if (!n) {
			app.toast('Give the mod a name.', 'bad');
			return null;
		}
		const modName = n + '.rte';
		if (app.lib.modules.some((m) => m.name.toLowerCase() === modName.toLowerCase()) || (await app.fs.exists(modName + '/Index.ini'))) {
			app.toast('There is already a mod called ' + modName + '.', 'bad');
			return null;
		}
		app.lib.newModule(modName);
		const index = app.lib.docFor(modName + '/Index.ini');
		app.hookDoc(index);
		await app.save(true, [index]);
		app.lib.reindex();
		return modName;
	}

	// "Which mod are you working on?": your mods, or a new one. At start (startup) it can also be skipped to just look through the library.
	app.chooseProject = function (startup) {
		if (!app.lib || !app.fs) {
			return;
		}
		const el = MS.dialogs.parts.el;
		const choices = projectChoices();
		let picked = choices.find((c) => app.project && c.name.toLowerCase() === app.project.toLowerCase()) ? app.project : choices[0] ? choices[0].name : null;
		const list = el('div', { className: 'proj-list' });
		const draw = () => {
			list.replaceChildren(...choices.map((c) => el('button', { className: 'proj-row' + (c.name === picked ? ' sel' : ''), onclick: () => { picked = c.name; draw(); }, ondblclick: () => go(c.name) },
				el('b', { textContent: c.name.replace(/\.rte$/i, '') }), el('small', { textContent: c.text }))));
		};
		const go = async (name) => {
			MS.dialogs.close();
			app.setProject(name);
		};
		const newName = el('input', { type: 'text', placeholder: 'A new mod: its name', className: 'ed-input' });
		const make = el('button', { className: 'tb', textContent: 'Start it', onclick: async () => {
			const made = await makeProject(newName.value);
			if (made) {
				MS.dialogs.close();
				app.setProject(made);
			}
		} });
		draw();
		const body = el('div', {},
			el('p', { textContent: 'Everything on the left, saving and backups are for this one mod. You can still look through the whole game and every mod in the Library, and clone units or add parts from there; they are copied into your project.' }),
			choices.length ? list : el('p', { className: 'small', textContent: 'You have no mods yet. Start one below.' }),
			el('div', { className: 'proj-new' }, newName, make));
		const buttons = [];
		if (startup) {
			buttons.push({ text: 'Just browse the Library', action: () => { app.setLibrary(true); } });
		} else {
			buttons.push({ text: 'Cancel' });
		}
		if (choices.length) {
			buttons.push({ text: 'Work on this mod', primary: true, action: () => { if (picked) { app.setProject(picked); } } });
		}
		MS.dialogs.open('Which mod are you working on?', body, buttons);
	};

	// The open unit (or item) isn't yours: the game's, or another mod's. With a project, the first change to it copies it into the project
	// (named after it, with Custom) and carries on with the copy, so there is nothing to ask. needsCopy says whether that would happen;
	// copyThen makes the copy, opens it, finds the same part again, and calls then(thatPart).
	app.needsCopy = function () {
		return !!(app.project && app.actor && app.actorNode && app.lib.isProtected(app.actorNode.doc) && !app.unlocked);
	};

	app.copyThen = async function (part, then) {
		const original = app.actor;
		const role = part ? part.role : null;
		const nth = part ? app.rig.parts.filter((p) => p.role === role).indexOf(part) : 0;
		const taken = new Set(app.lib.presets.filter((p) => p.module.toLowerCase() === app.project.toLowerCase()).map((p) => p.name.toLowerCase()));
		let name = original.name + ' Custom';
		for (let n = 2; taken.has(name.toLowerCase()); n++) {
			name = original.name + ' Custom ' + n;
		}
		await MS.dialogs.makeCopy(app.project, name, original);
		if (!app.actor || app.actor.name !== name) {
			return; // the copy didn't happen (it said why)
		}
		const same = part ? app.rig.parts.filter((p) => p.role === role)[Math.max(0, nth)] || app.rig.body : app.rig.body;
		app.select(same);
		then(same);
	};

	function projectMenu() {
		const old = $('scopeMenu');
		if (old) {
			old.remove();
			return;
		}
		if (!app.lib) {
			return;
		}
		const menu = document.createElement('div');
		menu.className = 'ctx-menu scope-menu';
		menu.id = 'scopeMenu';
		const close = () => {
			menu.remove();
			document.removeEventListener('pointerdown', outside, true);
		};
		const outside = (e) => {
			if (!menu.contains(e.target) && e.target !== $('btnScope')) {
				close();
			}
		};
		const head = document.createElement('div');
		head.className = 'scope-head';
		head.textContent = 'Work on';
		menu.append(head);
		for (const c of projectChoices()) {
			const b = document.createElement('button');
			b.textContent = (app.project && c.name.toLowerCase() === app.project.toLowerCase() ? '● ' : '   ') + c.name.replace(/\.rte$/i, '');
			b.onclick = () => {
				close();
				app.setProject(c.name);
			};
			menu.append(b);
		}
		const more = document.createElement('button');
		more.textContent = 'Another mod, or a new one…';
		more.onclick = () => {
			close();
			app.chooseProject(false);
		};
		menu.append(more);
		document.body.append(menu);
		const r = $('btnScope').getBoundingClientRect();
		menu.style.left = Math.min(r.left, window.innerWidth - menu.offsetWidth - 8) + 'px';
		menu.style.top = r.bottom + 4 + 'px';
		setTimeout(() => document.addEventListener('pointerdown', outside, true), 0);
	}
	app.updateScopeButton = updateScopeButton;

	// opts.newTab: open it in a designer tab of its own (Ctrl/Cmd+click); otherwise units and craft share one tab, items another, the rest a third.
	// What goes to the game (Preview in game, Live, Spawn): the thing in the design tab linked to the game (the game-link mark on a design tab),
	// or, with none linked, the thing on show. { actor, device }.
	app.gameThing = function () {
		const t = MS.ide && MS.ide.gameTab;
		if (t && MS.ide.tabs.includes(t) && t !== MS.ide.currentDesigner && t.state && t.state.actor) {
			return { actor: t.state.actor, device: t.state.device || null };
		}
		return { actor: app.actor, device: app.device };
	};

	app.openActor = function (preset, opts) {
		const tab = MS.ide.designerFor(preset.cls, !!(opts && opts.newTab), preset);
		MS.ide.currentDesigner = tab;
		app.selectedPiece = null;
		app.actor = { path: preset.doc.path, name: preset.name, cls: preset.cls };
		app.selected = null;
		app.hidden.clear();
		app.collapsed.clear();
		app.poseState.frames = {};
		app.poseState.time = 0;
		writeLocal('ms.lastActor', { path: preset.doc.path, name: preset.name });
		// A unit shows the first gun it carries; anything else (an item, a craft) is designed on its own.
		app.device = preset.cls === 'AHuman' ? defaultDevice(preset.node) : null;
		if (preset.cls === 'AHuman') {
			writeLocal('ms.lastHolder', { path: preset.doc.path, name: preset.name });
		}
		app.rebuild(true);
		app.select(app.rig ? app.rig.body : null);
		app.view.fit();
		MS.panels.markLibrary();
		app.liveChanged();
		MS.ide.renderTabs();
	};

	// How a thing is put into a running game: units and craft are added as actors, held items and the like as items, gibs and particles loose.
	function spawnKind(cls) {
		if (!cls) {
			return null;
		}
		if (['AHuman', 'ACrab', 'ACDropShip', 'ACRocket'].includes(cls)) {
			return 'actor';
		}
		if (DEVICE_CLASSES.has(cls)) {
			return 'item';
		}
		return ['MOSRotating', 'MOSParticle'].includes(cls) ? 'particle' : null;
	}

	// Spawn (or Give) the open thing in the game running from Studio: it appears beside the unit you control (or in view), on its team. The game must
	// have loaded it already (a brand new mod needs one restart); Live and saving then show later changes.
	app.spawnInGame = async function (give) {
		const actor = app.gameThing().actor;
		const kind = spawnKind(actor && actor.cls);
		if (!kind) {
			return;
		}
		const module = actor.path.split('/')[0];
		const q = (s) => JSON.stringify(String(s));
		const add = kind === 'actor' ? 'o.Team = ctl and ctl.Team or 0\n\tMovableMan:AddActor(o)' : kind === 'item' ? (give ? 'if ctl then ctl:AddInventoryItem(o) else MovableMan:AddItem(o) end' : 'MovableMan:AddItem(o)') : 'MovableMan:AddParticle(o)';
		const lua = [
			'local act = ActivityMan:GetActivity()',
			'local ctl = act and act:GetControlledActor(0)',
			'local made, o = pcall(Create' + actor.cls + ', ' + q(actor.name) + ', ' + q(module) + ')',
			'if not made or not o then',
			'\tConsoleMan:PrintString("Studio: the game has not loaded ' + actor.name.replace(/["\\]/g, '') + ' (' + module + '). Check the console in Studio: the mod may have failed to load, or it is new since the game started.")',
			'\treturn',
			'end',
			'local ok, where = pcall(function() return CameraMan:GetScrollTarget(0) end)',
			'o.Pos = ctl and (ctl.Pos + Vector(ctl.HFlipped and -40 or 40, -20)) or (ok and where or Vector(0, 0))',
			add,
			'ConsoleMan:PrintString("Studio: put ' + actor.name.replace(/["\\]/g, '') + ' in the game.")',
		].join('\n');
		// What has been saved goes to disk first, so the game is not a step behind.
		try {
			await app.save(true, app.dirtyDocs().filter((d) => !app.lib.isProtected(d)));
		} catch (e) {
			// the spawn still goes
		}
		MS.ide.runLua(lua);
		app.status((give ? 'Gave ' : 'Spawned ') + actor.name + ' to the game (sent). It only works in a game started from Studio (Test in game, Sandbox).');
	};

	// Material number -> [r, g, b], from the Materials the game loads (a material map picture's pixels are these numbers).
	let materialCache = null;
	let materialGeneration = -1;
	MS.materialColors = function () {
		if (materialCache && materialGeneration === app.lib.generation) {
			return materialCache;
		}
		const map = new Map();
		for (const p of app.lib.presets) {
			if (p.cls !== 'Material' || !p.topLevel) {
				continue;
			}
			const ent = app.lib.resolve(p.node);
			const index = ent.num('Index', -1);
			const color = ent.sub('Color');
			if (index >= 0 && color) {
				map.set(index, [color.num('R', 0), color.num('G', 0), color.num('B', 0)]);
			}
		}
		materialCache = map;
		materialGeneration = app.lib.generation;
		return map;
	};

	// ---- bunker assemblies: the pieces they place ----

	app.selectedPiece = null;

	app.selectPiece = function (index) {
		app.selectedPiece = index === undefined ? null : index;
		MS.panels.refresh();
		app.requestDraw();
	};

	// The piece's own line in the assembly (taking over a line the assembly inherits from the one it copies, as for any list), or null.
	function ownPiece(piece) {
		const part = app.rig.body;
		const block = app.ownBlock(part);
		if (!block) {
			return null;
		}
		return piece.node.parent === block ? piece.node : app.lib.adopt(block, piece.node);
	}

	function pieceOf(index) {
		return app.rig.body.pieces.find((p) => p.index === index) || null;
	}

	// Changes to a locked assembly go to a copy in the project first, like anything else, then carry on with the same piece.
	function withEditable(then) {
		if (app.needsCopy()) {
			app.copyThen(app.rig.body, () => then());
			return false;
		}
		const reason = app.lockReason(app.rig.body);
		if (reason) {
			app.toast(reason, 'bad');
			return false;
		}
		return true;
	}

	app.movePiece = function (index, x, y) {
		if (!withEditable(() => app.movePiece(index, x, y))) {
			return;
		}
		const piece = pieceOf(index);
		if (!piece) {
			return;
		}
		app.edit('move ' + piece.name, () => {
			const node = ownPiece(piece);
			if (node) {
				node.setVector('Position', Math.round(x), Math.round(y));
			}
		});
	};

	app.removePiece = function (index) {
		if (!withEditable(() => app.removePiece(index))) {
			return;
		}
		const piece = pieceOf(index);
		if (!piece) {
			return;
		}
		app.edit('take ' + piece.name + ' out', () => {
			const node = ownPiece(piece);
			if (node) {
				node.remove();
			}
		});
		app.selectedPiece = null;
		MS.panels.refresh();
	};

	// ---- a building piece's lights ----

	// The light's own line in the piece (taking over one the piece inherits from what it copies, as for any list).
	function ownLight(part, light) {
		const block = app.ownBlock(part);
		if (!block) {
			return null;
		}
		return light.node.parent === block ? light.node : app.lib.adopt(block, light.node);
	}

	// A change to one light: key is Radius, Intensity, Offset (value {x, y}) or Color (value [r, g, b]).
	app.setLight = function (part, index, key, value) {
		if (!withEditable(() => app.setLight(app.rig.body, index, key, value))) {
			return;
		}
		const light = app.rig.body.lights && app.rig.body.lights[index];
		if (!light) {
			return;
		}
		app.edit('change a light', () => {
			const node = ownLight(app.rig.body, light);
			if (!node) {
				return;
			}
			if (key === 'Offset') {
				node.setVector('Offset', Math.round(value.x * 2) / 2, Math.round(value.y * 2) / 2);
			} else if (key === 'Color') {
				const color = node.child('Color') || node.add('Color', 'Color');
				color.setValue('Color');
				['R', 'G', 'B'].forEach((k, i) => color.set(k, String(Math.round(value[i]))));
			} else {
				node.set(key, String(value));
			}
		});
	};

	app.addLight = function () {
		if (!withEditable(() => app.addLight())) {
			return;
		}
		const part = app.rig.body;
		const frame = part.layers && part.layers.map((l) => (l.file ? app.sprite(l.file, 1, 0) : null)).find((f) => f);
		app.edit('add a light', () => {
			const block = app.ownBlock(part);
			if (!block) {
				return;
			}
			const light = block.add('AddLight', 'TerrainLight');
			light.setVector('Offset', frame ? Math.round(frame.width / 2) : 24, frame ? Math.round(frame.height / 2) : 24);
			const color = light.add('Color', 'Color');
			color.add('R', '255');
			color.add('G', '230');
			color.add('B', '180');
			light.add('Radius', '80');
			light.add('Intensity', '1');
		});
	};

	app.removeLight = function (index) {
		if (!withEditable(() => app.removeLight(index))) {
			return;
		}
		const light = app.rig.body.lights && app.rig.body.lights[index];
		if (!light) {
			return;
		}
		app.edit('take a light out', () => {
			const node = ownLight(app.rig.body, light);
			if (node) {
				node.remove();
			}
		});
	};

	// Sets (or with null takes off) one of a placed piece's own lines: HFlipped, Team...
	app.setPieceProp = function (index, key, value) {
		if (!withEditable(() => app.setPieceProp(index, key, value))) {
			return;
		}
		const piece = pieceOf(index);
		if (!piece) {
			return;
		}
		app.edit('change ' + piece.name, () => {
			const node = ownPiece(piece);
			if (!node) {
				return;
			}
			if (value === null) {
				const old = node.child(key);
				if (old) {
					old.remove();
				}
			} else {
				node.set(key, String(value));
			}
		});
	};

	// Adds a terrain object (a preset) to the assembly, in the middle of what is there, and picks it.
	app.addPiece = function (preset) {
		if (!withEditable(() => app.addPiece(preset))) {
			return;
		}
		const boxes = app.view.pieceBoxes(app.view.drawList.find((i) => i.part.pieces) || { part: app.rig.body, pose: { pos: { x: 0, y: 0 } } });
		const mid = boxes.length ? { x: Math.round((Math.min(...boxes.map((b) => b.piece.pos.x)) + Math.max(...boxes.map((b) => b.piece.pos.x))) / 24) * 12, y: Math.round((Math.min(...boxes.map((b) => b.piece.pos.y)) + Math.max(...boxes.map((b) => b.piece.pos.y))) / 24) * 12 } : { x: 0, y: 0 };
		let added = null;
		app.edit('add ' + preset.name, () => {
			const block = app.ownBlock(app.rig.body);
			if (!block) {
				return;
			}
			added = block.add('PlaceObject', preset.cls);
			added.add('CopyOf', app.lib.reference(preset.name, preset.module, block.doc.module));
			added.setVector('Position', mid.x, mid.y);
		});
		const pieces = app.rig.body.pieces;
		if (pieces.length) {
			app.selectPiece(pieces.length - 1);
		}
	};

	app.findActorNode = function () {
		if (!app.actor) {
			return null;
		}
		const doc = app.lib.docFor(app.actor.path);
		if (!doc) {
			return null;
		}
		const preset = app.lib.presets.find((p) => p.doc === doc && p.name === app.actor.name && p.cls === app.actor.cls);
		return preset ? preset.node : null;
	};

	function findDeviceNode() {
		if (!app.device) {
			return null;
		}
		const preset = app.lib.presets.find((p) => p.doc.path === app.device.path && p.name === app.device.name);
		return preset ? preset.node : null;
	}

	// The first gun or tool the unit carries, as a preset.
	function defaultDevice(actorNode) {
		const ent = app.lib.resolve(actorNode);
		for (const entry of ent.all('AddInventory')) {
			if (!DEVICE_CLASSES.has(entry.node.value)) {
				continue;
			}
			const copyOf = entry.node.child('CopyOf');
			const preset = copyOf ? app.lib.find(copyOf.value, entry.node.value, entry.node.doc.module) : null;
			if (preset && preset.cls !== 'TDExplosive') {
				return { path: preset.doc.path, name: preset.name };
			}
		}
		return null;
	}

	// After Make my own copy of a held item: every AddInventory of the unit being designed that carries the original now carries the
	// copy (an inherited one is taken into the unit's own block first, so other units are untouched), and the copy is held.
	app.useOwnCopy = function (original, copy) {
		const actorNode = app.actorNode;
		const module = actorNode.doc.path.split('/')[0];
		let replaced = 0;
		app.edit('use my own copy of ' + original.name, () => {
			const entries = app.lib.resolve(actorNode).all('AddInventory');
			entries.forEach((entry, index) => {
				const copyOf = entry.node.child('CopyOf');
				const carried = copyOf && app.lib.find(copyOf.value, entry.node.value, entry.node.doc.module);
				if (!carried || carried.node !== original.node) {
					return;
				}
				const own = app.lib.ownBlock(actorNode, [{ key: 'AddInventory', index }]);
				if (own.node) {
					own.node.set('CopyOf', app.lib.reference(copy.name, copy.doc.path.split('/')[0], module));
					replaced++;
				}
			});
		});
		app.setDevice(copy);
		app.toast(replaced ? app.actor.name + ' now carries ' + copy.name + ' instead of ' + original.name + '. Save to keep it.' : app.actor.name + ' holds ' + copy.name + ' here. It doesn\'t carry ' + original.name + ' in its inventory, so nothing in the unit changed.', 'good');
	};

	app.setDevice = function (preset) {
		app.device = preset ? { path: preset.doc.path, name: preset.name } : null;
		app.rebuild();
		MS.panels.refresh();
		app.liveChanged();
	};

	// Rebuilds the rig from the current files, keeping the selection; and the other designer on screen, side by side, so a change made in one
	// (a gun's grip) shows in the other (the unit holding the gun) as it is made.
	app.rebuild = function (fresh) {
		const built = rebuildRig(fresh);
		if (!built) {
			MS.panels.refresh();
			app.requestDraw();
			rebuildOthers();
			return;
		}
		updateTitle();
		if (fresh) {
			MS.panels.refresh();
		}
		app.requestDraw();
		rebuildOthers();
	};

	// The designer on show's rig from the current files (false when its thing is gone).
	function rebuildRig(fresh) {
		const selectedId = app.selected ? app.selected.id : null;
		app.lib.invalidate();
		app.actorNode = app.actor ? app.findActorNode() : null;
		if (!app.actorNode) {
			app.rig = null;
			app.selected = null;
			return false;
		}
		app.rig = new MS.Rig(app.lib, app.actorNode);
		const deviceNode = findDeviceNode();
		app.rig.setDevice(deviceNode);
		app.selected = (selectedId && app.rig.parts.find((p) => p.id === selectedId)) || (fresh ? app.rig.body : app.selected && app.rig.body);
		if (fresh || app.groundY === null) {
			app.groundY = standingGround();
		}
		return true;
	}

	function rebuildOthers() {
		if (!app.split) {
			return;
		}
		for (const pane of app.panes) {
			const d = pane.tab && pane.tab.designer;
			if (d && d !== app.designer && d.actor) {
				app.withDesigner(d, () => rebuildRig(false), pane.view);
			}
		}
		drawPaneLabels();
	}

	// ---- the views: the designer on show, or two designers side by side ----
	// Each view (a canvas with its own zoom and pan) shows one design tab. With two, the one you click becomes the one you edit; both are drawn
	// every frame, and both rebuilt when the files change.
	app.panes = [];
	app.split = false;
	function setUpPanes() {
		const wrap = $('viewWrap');
		const second = document.createElement('canvas');
		second.id = 'view2';
		second.hidden = true;
		wrap.insertBefore(second, $('view').nextSibling);
		const label = (i) => {
			const b = document.createElement('button');
			b.className = 'pane-label ' + (i ? 'right' : 'left');
			b.hidden = true;
			b.onclick = () => {
				const pane = app.panes[i];
				if (pane.tab && pane.tab !== MS.ide.currentDesigner) {
					MS.ide.activate(pane.tab);
				}
			};
			wrap.append(b);
			return b;
		};
		app.panes = [{ view: app.view, tab: null, label: label(0) }, { view: new MS.Viewport(second, app), tab: null, label: label(1) }];
		app.panes.forEach((pane) => {
			// a view not being edited takes a click as "edit this one", and ignores the mouse passing over it
			pane.view.isActive = () => !app.split || pane.view === app.view;
			pane.view.activate = () => pane.tab && MS.ide.activate(pane.tab);
		});
		const button = document.createElement('button');
		button.id = 'btnSplit';
		button.className = 'tb';
		button.textContent = '◫ Side by side';
		button.title = 'Show two design tabs side by side: changes to one (a gun\'s grip) show in the other (the unit holding it) as you make them';
		button.onclick = () => {
			// the other design tab into the right group of tabs, or the right group's design tabs back
			const ide = MS.ide;
			const right = ide.tabs.filter((t) => t.kind === 'designer' && t.group === 1);
			if (right.length) {
				right.forEach((t) => ide.moveToGroup(t, 0, true));
				ide.renderTabs();
				return;
			}
			const current = ide.currentDesigner;
			const other = app.recentDesigners.find((t) => t !== current && ide.tabs.includes(t) && t.designer && t.designer.actor)
				|| ide.tabs.find((t) => t.kind === 'designer' && t !== current && t.designer && t.designer.actor);
			if (!current || !other) {
				app.toast('Open a second thing first (each opens in its own design tab), then show them side by side.', 'bad');
				return;
			}
			ide.moveToGroup(other, 1);
			ide.activate(current);
		};
		$('viewTools').append(button);
	}

	// The design tab now on show (ide.js activates it): its designer becomes app's, in the view it is in (or the view being edited), with its
	// own zoom and pan, and its rig rebuilt from the files as they are now.
	app.recentDesigners = [];
	app.focusDesigner = function (tab) {
		const ide = MS.ide;
		let pane = app.split ? app.panes.find((p) => p.tab === tab) : null;
		if (!pane) {
			pane = app.split ? app.panes[tab.group === 1 ? 1 : 0] : app.panes[0];
			if (pane.tab && pane.tab !== tab && pane.tab.designer) {
				pane.tab.designer.viewState = { zoom: pane.view.zoom, pan: Object.assign({}, pane.view.pan) };
			}
			pane.tab = tab;
			if (tab.designer.viewState) {
				pane.view.zoom = tab.designer.viewState.zoom;
				pane.view.pan = Object.assign({}, tab.designer.viewState.pan);
			}
		}
		ide.currentDesigner = tab;
		app.designer = tab.designer;
		app.view = pane.view;
		app.recentDesigners = [tab, ...app.recentDesigners.filter((t) => t !== tab && ide.tabs.includes(t))];
		if (app.actor) {
			app.rebuild(false);
		}
		MS.panels.refresh();
		MS.panels.markLibrary();
		drawPaneLabels();
		app.syncTools();
		app.requestDraw();
	};

	// Two views on or off. On: the design tab on show and the one used before it (which has something open).
	app.setSplit = function (on) {
		const ide = MS.ide;
		if (on) {
			const current = ide.currentDesigner;
			const other = app.recentDesigners.find((t) => t !== current && ide.tabs.includes(t) && t.designer && t.designer.actor)
				|| ide.tabs.find((t) => t.kind === 'designer' && t !== current && t.designer && t.designer.actor);
			if (!current || !other) {
				app.toast('Open a second thing first (each opens in its own design tab), then show them side by side.', 'bad');
				return;
			}
			app.split = true;
			app.panes[0].tab = current;
			app.panes[1].tab = other;
			app.view = app.panes[0].view;
			const second = app.panes[1].view;
			if (other.designer.viewState) {
				second.zoom = other.designer.viewState.zoom;
				second.pan = Object.assign({}, other.designer.viewState.pan);
			}
			app.withDesigner(other.designer, () => rebuildRig(false), second);
			// fitted once the second view is on screen and has its size (and only if it has no view of its own yet)
			if (!other.designer.viewState) {
				requestAnimationFrame(() => app.withDesigner(other.designer, () => second.fit(), second));
			}
		} else {
			const shown = app.panes.find((p) => p.view === app.view);
			if (shown && shown !== app.panes[0]) {
				app.panes[0].view.zoom = shown.view.zoom;
				app.panes[0].view.pan = Object.assign({}, shown.view.pan);
			}
			app.split = false;
			app.panes[0].tab = ide.currentDesigner;
			app.panes[1].tab = null;
			app.view = app.panes[0].view;
		}
		$('viewWrap').classList.toggle('split', app.split);
		$('view2').hidden = !app.split;
		$('btnSplit').classList.toggle('sel', app.split);
		drawPaneLabels();
		ide.renderTabs();
		app.requestDraw();
	};

	// The right group of tabs' designer (or null: none): drawn in its own view, the second one, which moves into the right group, beside the
	// main group's designer in the first.
	app.secondPane = function (tab) {
		const pane = app.panes[1];
		if (!pane) {
			return;
		}
		const view = pane.view;
		const canvas = $('view2');
		const keep = (t) => {
			if (t && t.designer) {
				t.designer.viewState = { zoom: view.zoom, pan: Object.assign({}, view.pan) };
			}
		};
		if (tab) {
			if (canvas.parentNode !== $('g2Canvas')) {
				$('g2Canvas').append(canvas);
			}
			canvas.hidden = false;
			$('viewWrap').classList.remove('split');
			if (pane.tab !== tab) {
				keep(pane.tab);
				pane.tab = tab;
				if (tab.designer.viewState) {
					view.zoom = tab.designer.viewState.zoom;
					view.pan = Object.assign({}, tab.designer.viewState.pan);
				}
				app.split = true;
				if (tab.designer.actor) {
					app.withDesigner(tab.designer, () => rebuildRig(false), view);
				}
				if (!tab.designer.viewState) {
					requestAnimationFrame(() => app.withDesigner(tab.designer, () => view.fit(), view));
				}
			}
			app.split = true;
		} else if (pane.tab) {
			keep(pane.tab);
			pane.tab = null;
			app.split = false;
			if (app.view === view) {
				const main = app.panes[0].tab;
				app.view = app.panes[0].view;
				if (main && main.designer) {
					MS.ide.currentDesigner = main;
					app.designer = main.designer;
				}
			}
		}
		$('btnSplit').classList.toggle('sel', !!pane.tab);
		drawPaneLabels();
		app.syncTools();
		app.requestDraw();
	};

	// A design tab closed: out of the views (two views become one if it was in one).
	app.designerClosed = function (tab) {
		app.recentDesigners = app.recentDesigners.filter((t) => t !== tab);
		if (app.panes[1] && app.panes[1].tab === tab) {
			app.secondPane(null); // the right group's designer: its view goes
		} else if (app.panes[0] && app.panes[0].tab === tab) {
			app.panes[0].tab = null;
		}
	};

	function drawPaneLabels() {
		app.panes.forEach((pane) => {
			const d = pane.tab && pane.tab.designer;
			pane.label.hidden = true; // each group of tabs has its own bar, which says what it shows
			if (d) {
				pane.label.textContent = (d.actor ? d.actor.name : 'Designer') + (pane.view === app.view ? '' : '  (click to edit)');
				pane.label.classList.toggle('on', pane.view === app.view);
			}
		});
	}

	// Where the feet rest when standing: the lowest foot pixel.
	function standingGround() {
		const rig = app.rig;
		if (rig.cls !== 'AHuman' && rig.cls !== 'ACrab') {
			return null; // items and craft don't stand on anything
		}
		const poses = rig.pose(Object.assign({}, app.poseState, { pose: 'stand', time: 0 }));
		let ground = null;
		for (const role of ['foot']) {
			for (const part of rig.parts.filter((p) => p.role === role)) {
				const pose = poses.get(part);
				const frame = app.sprite(part.sprite.file, part.sprite.frameCount, pose.frame);
				if (!pose || !frame) {
					continue;
				}
				const item = { part, kind: 'sprite', pose };
				const rect = app.view.itemRect(item);
				for (const p of rect || []) {
					ground = ground === null ? p.y : Math.max(ground, p.y);
				}
			}
		}
		return ground !== null ? Math.round(ground) : poses.ground !== undefined ? Math.round(poses.ground) + 3 : null;
	}

	app.updateTitle = () => updateTitle();
	function updateTitle() {
		const t = $('docTitle');
		if (!app.actor) {
			t.textContent = '';
			return;
		}
		const kind = app.lib.lockKind(app.lib.docFor(app.actor.path));
		const locked = !!kind;
		const unlockedOther = app.unlocked && app.lib.ownerKind(app.lib.docFor(app.actor.path));
		t.innerHTML = '';
		const b = document.createElement('b');
		b.textContent = app.actor.name;
		t.append(b, ' · ' + app.actor.path);
		const badge = document.createElement('span');
		badge.className = 'badge ' + (locked ? 'lock' : 'own');
		badge.textContent = kind === 'game' ? 'built-in: make a copy to change it' : kind === 'mod' ? 'someone else\'s mod' : unlockedOther ? 'unlocked: changed in place' : 'your mod';
		if (unlockedOther) {
			badge.className = 'badge unlocked';
		}
		t.append(badge);
		$('btnCopy').disabled = false;
		$('btnCopy').classList.toggle('attention', locked);
		$('btnPreview').disabled = locked;
		$('layerBox').hidden = !(app.rig && (app.rig.body.ent.cls === 'TerrainObject' || app.rig.body.ent.cls === 'BunkerAssembly'));
		const spawn = spawnKind(app.actor && app.actor.cls);
		$('btnSpawn').disabled = !spawn;
		$('btnGive').hidden = spawn !== 'item';
		$('btnGive').disabled = spawn !== 'item';
		$('btnLive').disabled = locked || !app.fs || !app.fs.writable;
		// Dimmed, not hidden: say what would make them work.
		$('btnPreview').title = locked ? 'Make your own copy of this unit first, then you can see it in the game.' : 'Save, then show this unit in the game\'s Actor Editor';
		$('btnLive').title = locked ? 'Make your own copy of this unit first, then every change can show in the game as you make it.' : 'Live: send every change to the game as you make it (a drag is sent when you let go)';
		// another design tab is linked to the game: the game buttons are about its thing
		const game = app.gameThing().actor;
		if (game && game !== app.actor) {
			const gameLocked = !!app.lib.lockKind(app.lib.docFor(game.path));
			const gameSpawn = spawnKind(game.cls);
			$('btnPreview').disabled = gameLocked;
			$('btnLive').disabled = gameLocked || !app.fs || !app.fs.writable;
			$('btnSpawn').disabled = !gameSpawn;
			$('btnGive').hidden = gameSpawn !== 'item';
			$('btnGive').disabled = gameSpawn !== 'item';
			$('btnPreview').title = 'Save, then show ' + game.name + ' (the tab linked to the game) in the game';
			$('btnLive').title = 'Live: send every change to ' + game.name + ' (the tab linked to the game) as you make it';
			$('btnSpawn').title = 'Put ' + game.name + ' (the tab linked to the game) in the running game';
		}
	}

	// A part is hidden when it, or a part it hangs from, has its eye off; or, while any part is soloed (as on a mixing desk), when neither it
	// nor a part it hangs from is soloed.
	app.isHidden = function (part) {
		let soloed = false;
		for (let p = part; p; p = p.parent) {
			if (app.hidden.has(p.id)) {
				return true;
			}
			soloed = soloed || app.soloed.has(p.id);
		}
		return app.soloed.size > 0 && !soloed;
	};

	app.select = function (part) {
		app.selected = part;
		MS.panels.refresh();
		app.requestDraw();
	};

	app.onHover = function (part) {
		MS.panels.hoverTree(part);
		const hint = $('viewHint');
		const turns = part && !MS.Rig.POSED_ROLES.has(part.role) && part.ent.bool('InheritsRotAngle', true);
		hint.textContent = part ? part.name + ' — drag to move · Shift+drag to move the picture' + (turns ? ' · Ctrl+drag to rotate' : '') : 'Drag a part to move it · wheel to zoom · double-click to fit';
	};

	app.onZoom = function () {
		app.syncTools();
	};

	// ---- the designer's toolbar, one for each design tab on screen ----
	// The main toolbar (#viewTools) is the left group's designer's; with the tabs split, the right group's designer has its own (#g2Tools, a
	// copy). Each shows its own designer and view: the pose, facing, aim, held item, playing, speed, the view's options and zoom. A control in
	// the copy runs the main one's handler with its designer on show and its elements standing in for the main ones (the same ids), so one set
	// of handlers serves both; after anything, both are shown again from their designers.
	const TOOL_IDS = ['poseButtons', 'btnPlay', 'speed', 'btnFlip', 'aim', 'aimVal', 'sharp', 'deviceSelect', 'layerBox', 'terrainLayer', 'viewOptions', 'zoomVal'];
	function rightTab() {
		return app.split && app.panes[1] && app.panes[1].tab && app.panes[1].tab.designer ? app.panes[1].tab : null;
	}
	function withRightTools(fn) {
		const swapped = TOOL_IDS.filter((id) => $(id) && $('g2-' + id));
		for (const id of swapped) {
			$(id).id = id + '__main';
			$('g2-' + id).id = id;
		}
		try {
			return fn();
		} finally {
			for (const id of swapped) {
				$(id).id = 'g2-' + id;
				$(id + '__main').id = id;
			}
		}
	}
	function showTools() {
		const ps = app.poseState;
		for (const b of document.querySelectorAll('#poseButtons button')) {
			b.classList.toggle('sel', b.dataset.pose === ps.pose);
		}
		$('btnPlay').textContent = ps.paused ? '▶' : '❚❚';
		$('btnPlay').classList.toggle('sel', !!ps.paused);
		$('speed').value = String(ps.speed || 1);
		$('btnFlip').classList.toggle('sel', !!ps.flip);
		const deg = Math.round(((ps.aim || 0) * 180) / Math.PI);
		$('aim').value = String(deg);
		$('aimVal').textContent = deg + '°';
		$('sharp').checked = !!ps.sharp;
		for (const b of document.querySelectorAll('#viewOptions button')) {
			b.classList.toggle('sel', !!app.view.options[b.dataset.opt]);
		}
		$('zoomVal').textContent = (Math.round(app.view.zoom * 10) / 10) + '×';
		$('layerBox').hidden = !(app.rig && (app.rig.body.ent.cls === 'TerrainObject' || app.rig.body.ent.cls === 'BunkerAssembly'));
		if (app.view.terrainShow) {
			$('terrainLayer').value = app.view.terrainShow;
		}
		if (MS.panels && MS.panels.refreshDeviceSelect && app.lib) {
			MS.panels.refreshDeviceSelect();
		}
	}
	app.syncTools = function () {
		const left = app.panes[0] && app.panes[0].tab;
		const right = rightTab();
		if (!right || !$('g2Tools').children.length) {
			showTools();
			return;
		}
		const as = (tab, pane) => (tab && tab.designer && tab.designer !== app.designer ? app.withDesigner(tab.designer, showTools, pane.view) : showTools());
		as(left, app.panes[0]);
		withRightTools(() => as(right, app.panes[1]));
	};
	// The right group's toolbar: a copy of the main one, its controls running the main ones' handlers for the right group's designer.
	function setUpRightTools() {
		const box = $('g2Tools');
		const copy = $('viewTools').cloneNode(true);
		for (const e of copy.querySelectorAll('[id]')) {
			e.id = 'g2-' + e.id;
		}
		for (const e of copy.querySelectorAll('#g2-btnSplit, #g2-btnGuide')) {
			e.remove(); // one Side by side, and one guide, for the window
		}
		box.replaceChildren(...copy.childNodes);
		const mainOf = (e) => {
			if (e.id && e.id.startsWith('g2-')) {
				return $(e.id.slice(3));
			}
			const holder = e.parentElement && e.parentElement.id && e.parentElement.id.startsWith('g2-') ? $(e.parentElement.id.slice(3)) : null;
			return holder ? [...holder.children][[...e.parentElement.children].indexOf(e)] : null;
		};
		const run = (e, kind) => {
			const target = e.target.closest('button, input, select');
			const main = target && mainOf(target);
			const tab = rightTab();
			if (!main || !tab) {
				return;
			}
			if (MS.ide.currentDesigner !== tab) {
				MS.ide.activate(tab); // its designer comes up first
			}
			if (kind === 'click' && main.tagName === 'BUTTON') {
				withRightTools(() => main.onclick && main.onclick(e));
			} else if (kind === 'input' || kind === 'change') {
				main.value = target.value;
				main.checked = target.checked;
				const handler = kind === 'input' ? main.oninput : main.onchange;
				withRightTools(() => handler && handler.call(main, { target: main }));
			}
			app.syncTools();
		};
		box.addEventListener('click', (e) => run(e, 'click'));
		box.addEventListener('input', (e) => run(e, 'input'));
		box.addEventListener('change', (e) => run(e, 'change'));
	}

	// ---- sprites ----

	// A frame if it's loaded; otherwise starts loading it and redraws when it arrives.
	app.sprite = function (file, frameCount, frame) {
		if (!file) {
			return null;
		}
		const paths = MS.Images.framePaths(file, frameCount);
		const path = paths[Math.min(frame || 0, paths.length - 1)];
		const key = path.toLowerCase();
		if (app.frames.has(key)) {
			return app.frames.get(key);
		}
		app.frames.set(key, null);
		app.images.get(path).then((loaded) => {
			app.frames.set(key, loaded);
			if (app.groundY === null && app.rig) {
				app.groundY = standingGround();
			}
			app.requestDraw();
			MS.panels.spriteLoaded(path);
		});
		return null;
	};

	app.forgetSprites = function (paths) {
		if (MS.scenes && MS.scenes.forgetAsked) {
			MS.scenes.forgetAsked(paths);
		}
		for (const path of paths) {
			app.frames.delete(path.toLowerCase());
			app.images.forget(path);
		}
	};

	// ---- drawing loop ----

	let drawQueued = false;
	app.requestDraw = function () {
		drawQueued = true;
	};

	let lastTime = performance.now();
	function loop() {
		const now = performance.now();
		const dt = Math.min((now - lastTime) / 1000, 0.1);
		lastTime = now;
		for (const d of app.split ? app.panes.map((p) => p.tab && p.tab.designer).filter(Boolean) : [app.designer]) {
			if (d.rig && !d.poseState.paused) {
				d.poseState.time += dt * (d.poseState.speed || 1);
				drawQueued = true;
			}
		}
		if (drawQueued) {
			drawQueued = false;
			if (app.split) {
				for (const pane of app.panes) {
					const d = pane.tab && pane.tab.designer;
					if (d && d !== app.designer) {
						app.withDesigner(d, () => pane.view.render(), pane.view);
					} else if (d) {
						pane.view.render();
					}
				}
			} else {
				app.view.render();
			}
		}
		requestAnimationFrame(loop);
	}

	// ---- edits and undo ----

	app.begin = function (label) {
		if (!app.txn) {
			app.txn = { label, before: new Map() };
		}
	};

	app.commit = function () {
		const txn = app.txn;
		app.txn = null;
		if (!txn || !txn.before.size) {
			return;
		}
		const docs = [...txn.before].map(([doc, before]) => ({ doc, before, after: doc.serialize() })).filter((d) => d.before !== d.after);
		if (!docs.length) {
			return;
		}
		app.undo.push({ label: txn.label, docs, at: Date.now() });
		shareDocs(docs.map((d) => [d.doc.path, d.after]));
		if (app.undo.length > 200) {
			app.undo.shift();
		}
		app.redo = [];
		updateUndo();
		updateFileStatus();
		app.liveChanged();
	};

	// ---- changes shared between Studio windows ----
	// Each change made here goes to the other windows as it is made (the files' new text), and theirs come here: a designer in another window
	// (a unit holding the gun you are changing) shows it straight away, before anything is saved.
	function shareDocs(list) {
		if (list.length && MS.popout && MS.popout.announce) {
			MS.popout.announce('docs', { docs: list });
		}
	}
	app.applySharedDocs = function (list) {
		let changed = false;
		for (const [path, text] of list || []) {
			const doc = app.lib && app.lib.docFor(path);
			if (doc && doc.serialize() !== text) {
				doc.reset(text);
				changed = true;
			}
		}
		if (!changed) {
			return;
		}
		app.lib.reindex();
		app.rebuild();
		MS.panels.refresh();
		updateFileStatus();
	};

	// Runs fn as one undoable change.
	app.edit = function (label, fn) {
		const nested = !!app.txn;
		app.begin(label);
		let result;
		try {
			result = fn();
		} finally {
			if (!nested) {
				app.lib.reindex();
				app.commit();
				app.rebuild();
				MS.panels.refresh();
			}
		}
		return result;
	};

	function applyHistory(entry, which) {
		for (const d of entry.docs) {
			d.doc.reset(d[which]);
		}
		shareDocs(entry.docs.map((d) => [d.doc.path, d[which]]));
		app.lib.reindex();
		app.rebuild();
		MS.panels.refresh();
		updateUndo();
		updateFileStatus();
	}

	app.doUndo = function () {
		const entry = app.undo.pop();
		if (entry) {
			applyHistory(entry, 'before');
			app.redo.push(entry);
			app.status('Undid: ' + entry.label);
			updateUndo();
			app.liveChanged();
		}
	};

	app.doRedo = function () {
		const entry = app.redo.pop();
		if (entry) {
			applyHistory(entry, 'after');
			app.undo.push(entry);
			app.status('Redid: ' + entry.label);
			updateUndo();
			app.liveChanged();
		}
	};

	function updateUndo() {
		$('btnUndo').disabled = !app.undo.length;
		$('btnRedo').disabled = !app.redo.length;
		$('btnUndo').title = app.undo.length ? 'Undo ' + app.undo[app.undo.length - 1].label + ' (Ctrl+Z)' : 'Undo (Ctrl+Z)';
	}

	// Why a part can't be changed, or null.
	app.lockReason = function (part) {
		return app.lib.canEdit(part.anchor, part.path);
	};

	// The block a part's own values go in (copying an inherited block in first), or null after telling the user why not.
	app.ownBlock = function (part) {
		const result = app.lib.ownBlock(part.anchor, part.path);
		if (result.readOnly) {
			app.toast(result.readOnly, 'bad');
			return null;
		}
		return result.node;
	};

	app.setVector = function (part, key, x, y, label) {
		return app.edit(label || 'change ' + key, () => {
			const node = app.ownBlock(part);
			if (node) {
				node.setVector(key, x, y);
			}
		});
	};

	app.setValue = function (part, key, value, label) {
		return app.edit(label || 'change ' + key, () => {
			const node = app.ownBlock(part);
			if (node) {
				node.set(key, value);
			}
		});
	};

	// ---- dragging in the view ----

	// What a drag changes: a list of {part, key, start, convert(delta) -> offset change}.
	function dragPlan(d) {
		const part = d.part;
		const poses = d.poses;
		const pose = poses.get(part);
		const parentPose = part.parent ? poses.get(part.parent) : null;
		const bodyPose = poses.get(app.rig.body);
		// The held item, or the object itself when it's an item designed on its own.
		const device = app.rig.device || (app.rig.isDevice ? app.rig.body : null);
		const devicePose = device ? poses.get(device) : null;
		const local = (p) => (delta) => flipX(rotate(delta, -p.rot), p.flip);
		const aimLocal = (delta) => flipX(rotate(delta, -app.poseState.aim * (app.poseState.flip ? -1 : 1)), app.poseState.flip);
		const negate = (fn) => (delta) => {
			const v = fn(delta);
			return { x: -v.x, y: -v.y };
		};
		const stanceKey = app.poseState.sharp && device && device.ent.has('SharpStanceOffset') ? 'SharpStanceOffset' : 'StanceOffset';
		const vec = (p, key, fallback) => p.ent.vec(key, fallback || { x: 0, y: 0 });
		const spriteStart = () => {
			const s = part.sprite;
			if (s.offset && (s.offset.x || s.offset.y)) {
				return s.offset;
			}
			const frame = app.sprite(s.file, s.frameCount, pose.frame);
			return frame ? { x: -frame.width / 2, y: -frame.height / 2 } : { x: 0, y: 0 };
		};
		switch (d.kind) {
			case 'move':
				if (part.role === 'body') {
					return null;
				}
				if (part.role === 'device') {
					return [{ part, key: stanceKey, start: vec(part, stanceKey), convert: aimLocal }];
				}
				if (part.role === 'foot') {
					return [{ part, key: 'JointOffset', start: vec(part, 'JointOffset'), convert: negate(local(pose)) }];
				}
				// A muzzle flash is where the gun's muzzle is: moving it moves the gun's MuzzleOffset (its own ParentOffset is overwritten by the game).
				if (part.role === 'flash' && part.parent && parentPose) {
					return [{ part: part.parent, key: 'MuzzleOffset', start: vec(part.parent, 'MuzzleOffset'), convert: local(parentPose) }];
				}
				return parentPose ? [{ part, key: 'ParentOffset', start: vec(part, 'ParentOffset'), convert: local(parentPose) }] : null;
			case 'sprite':
				return [{ part, key: 'SpriteOffset', start: spriteStart(), convert: local(pose) }];
			case 'rotate': {
				// Attachments only, turning about their pivot. The angle is kept in degrees, mirrored like the game does when facing left.
				if (MS.Rig.POSED_ROLES.has(part.role) || !parentPose || !part.ent.bool('InheritsRotAngle', true)) {
					return null;
				}
				const startDegrees = (MS.Rig.rotationOffset(part.ent) * 180) / Math.PI;
				const from = sub(d.start, pose.joint);
				const startAngle = Math.atan2(from.y, from.x);
				const sign = parentPose.flip ? -1 : 1;
				return [{ part, key: 'InheritedRotAngleDegOffset', start: startDegrees, angle: (delta) => {
					const to = add(from, delta);
					// Screen y points down, so a positive screen angle is clockwise; the game's positive angles are anticlockwise.
					let turn = -(Math.atan2(to.y, to.x) - startAngle);
					turn = Math.atan2(Math.sin(turn), Math.cos(turn));
					return (turn * 180) / Math.PI * sign;
				} }];
			}
			case 'joint':
				if (part.role === 'device') {
					return [{ part, key: 'JointOffset', start: vec(part, 'JointOffset'), convert: local(pose) }, { part, key: stanceKey, start: vec(part, stanceKey), convert: aimLocal }];
				}
				if (part.role === 'foot' || part.role === 'flash' || !parentPose) {
					return [{ part, key: 'JointOffset', start: vec(part, 'JointOffset'), convert: local(pose) }];
				}
				return [{ part, key: 'JointOffset', start: vec(part, 'JointOffset'), convert: local(pose) }, { part, key: 'ParentOffset', start: vec(part, 'ParentOffset'), convert: local(parentPose) }];
			case 'idle': {
				const key = part.ent.has('HandIdleOffset') && !part.ent.has('IdleOffset') ? 'HandIdleOffset' : 'IdleOffset';
				return [{ part, key, start: vec(part, key), convert: local(bodyPose) }];
			}
			case 'stance':
				return device ? [{ part: device, key: stanceKey, start: vec(device, stanceKey), convert: aimLocal }] : null;
			case 'support':
				return device && devicePose ? [{ part: device, key: 'SupportOffset', start: vec(device, 'SupportOffset'), convert: local(devicePose) }] : null;
			case 'muzzle':
				return device && devicePose ? [{ part: device, key: 'MuzzleOffset', start: vec(device, 'MuzzleOffset'), convert: local(devicePose) }] : null;
			case 'eject':
				return device && devicePose ? [{ part: device, key: 'EjectionOffset', start: vec(device, 'EjectionOffset'), convert: local(devicePose) }] : null;
		}
		return null;
	}

	app.beginDrag = function (d) {
		const plan = dragPlan(d);
		if (!plan) {
			return false;
		}
		for (const step of plan) {
			const reason = app.lockReason(step.part);
			if (reason) {
				// Say why, and offer the fix on the spot: a copy in your mod.
				const action = app.lib.isProtected(app.actorNode.doc) ? { label: 'Make my own copy', run: () => MS.dialogs.copyUnit() } : null;
				app.toast(reason, 'bad', action);
				return false;
			}
		}
		d.plan = plan.map((s) => ({ partId: s.part.id, key: s.key, start: s.start, convert: s.convert, angle: s.angle }));
		app.begin((d.kind === 'rotate' ? 'rotate ' : 'move ') + d.part.name);
		return true;
	};

	app.dragTo = function (d, delta, snap) {
		for (const step of d.plan) {
			const part = app.rig.parts.find((p) => p.id === step.partId);
			if (!part) {
				continue;
			}
			const round = (v) => Math.round(v / snap) * snap;
			const node = app.lib.ownBlock(part.anchor, part.path).node;
			if (node && step.angle) {
				// Whole degrees (half degrees with Alt).
				const degrees = round(step.start + step.angle(delta));
				node.set(step.key, MS.formatNumber(((degrees % 360) + 540) % 360 - 180));
			} else if (node) {
				const change = step.convert(delta);
				node.setVector(step.key, round(step.start.x + change.x), round(step.start.y + change.y));
			}
		}
		app.lib.reindex();
		app.rebuild();
		MS.panels.refreshValues();
	};

	app.endDrag = function () {
		app.commit();
		MS.panels.refresh();
	};

	// Arrow keys: nudge the selected part by a pixel.
	function nudge(dx, dy, sprite) {
		const part = app.selected;
		if (!part || !app.view.poses) {
			return;
		}
		const d = { kind: sprite ? 'sprite' : 'move', part, poses: app.view.poses };
		if (!app.beginDrag(d)) {
			return;
		}
		// The nudge is in screen pixels: turn it into the part's own space through the same conversions a drag uses.
		app.dragTo(d, { x: dx, y: dy }, 1);
		app.endDrag(d);
	}

	// ---- saving and preview ----

	app.dirtyDocs = function () {
		return app.lib ? [...app.lib.docs.values()].filter((d) => d.dirty) : [];
	};

	function updateFileStatus() {
		const n = app.dirtyDocs().length + MS.ide.dirtyCount();
		const el = $('statusFiles');
		el.textContent = n ? n + ' unsaved file' + (n > 1 ? 's' : '') : 'All saved';
		el.className = n ? 'dirty' : '';
		$('btnSave').disabled = !n && !(app.fs && !app.fs.writable && app.fs.pendingWrites.size);
		$('btnDiscard').disabled = !n;
		$('btnDiscard').title = MS.ide.dirtyCount() ? 'Throw away unsaved changes, scripts included, and go back to what\'s saved' : 'Throw away unsaved changes and go back to what\'s saved';
	}
	app.updateFileStatus = updateFileStatus;

	// only: write just these documents (a new mod or unit is saved on its own, so unfinished edits elsewhere stay unsaved).
	// Changes to your own mods' files are saved by themselves a moment after the last one (not while a drag is in progress), so what the
	// designer shows is what is on disk. The game's own files and other people's mods are never written (they can't be changed here).
	let autosaveTimer = 0;
	function scheduleAutosave() {
		clearTimeout(autosaveTimer);
		autosaveTimer = setTimeout(autosave, 1500);
	}

	async function autosave() {
		if (!app.lib || !app.fs || !app.fs.writable) {
			return;
		}
		if (app.txn || (app.view && app.view.drag)) {
			scheduleAutosave();
			return;
		}
		const docs = app.dirtyDocs().filter((d) => !app.lib.isProtected(d));
		if (docs.length) {
			await app.save(true, docs);
		}
	}

	app.save = async function (quiet, only) {
		const docs = app.dirtyDocs().filter((d) => !only || only.includes(d));
		let written = 0;
		const saved = []; // For Undo on the Saved message: what each file now holds, and whether the save made it.
		for (const doc of docs) {
			if (app.lib.isProtected(doc)) {
				app.toast('Not saving ' + doc.path + ': ' + app.lib.lockMessage(doc), 'bad');
				continue;
			}
			const isNew = !!doc.isNew;
			const text = doc.serialize();
			try {
				await app.fs.writeFile(doc.path, text);
			} catch (e) {
				app.toast('Couldn\'t save ' + doc.path + ': ' + e.message, 'bad');
				continue;
			}
			doc.savedText = text;
			doc.dirty = false;
			doc.isNew = false;
			app.savedSinceLastPreview.add(doc.path);
			saved.push({ path: doc.path, text, isNew });
			written++;
		}
		updateFileStatus();
		if (!app.fs.writable) {
			const zip = app.fs.pendingZip();
			if (zip) {
				download(zip, 'ModStudio-changes.zip');
				app.toast('Downloaded your changes as ModStudio-changes.zip: unpack it into the game folder.', 'good');
			}
			return written;
		}
		if (written && MS.friendly) {
			MS.friendly.afterSave(saved.map((x) => x.path));
		}
		if (written && !quiet) {
			app.toast('Saved ' + written + ' file' + (written > 1 ? 's' : '') + '.', 'good');
		}
		return written;
	};

	// quiet: sent by Live mode, so no messages beyond the status line.
	// Throws away every unsaved change, after asking. Pictures Studio already wrote into the mod stay on disk, unused.
	app.discard = function () {
		const docs = app.dirtyDocs();
		const scripts = MS.ide.dirtyScripts();
		if (!docs.length && !scripts.length) {
			return;
		}
		const list = document.createElement('div');
		const intro = document.createElement('p');
		const count = docs.length + scripts.length;
		intro.textContent = 'Throw away the unsaved changes in ' + (count === 1 ? 'this file' : 'these ' + count + ' files') + '? They go back to what\'s saved on disk. This can\'t be undone.';
		const files = document.createElement('p');
		files.className = 'small';
		files.textContent = docs.map((d) => d.path + (d.isNew ? ' (new, not saved yet)' : '')).concat(scripts).join(', ');
		list.append(intro, files);
		MS.dialogs.open('Discard changes', list, [
			{ text: 'Keep them' },
			{ text: 'Discard', primary: true, action: () => {
				for (const doc of docs) {
					app.lib.discard(doc);
				}
				MS.ide.discardScripts();
				app.undo = [];
				app.redo = [];
				app.lib.reindex();
				if (app.actor && !app.lib.docFor(app.actor.path)) {
					app.actor = null;
					app.selected = null;
				}
				app.rebuild();
				MS.panels.refresh();
				MS.panels.markLibrary && MS.panels.markLibrary();
				updateUndo();
				updateFileStatus();
				app.status('Discarded the unsaved changes.');
				app.liveChanged();
			} },
		]);
	};

	// The request that makes the game show the open unit (Preview.ini): who, where it is, the files saved since, the held item. null when there is
	// nothing to show.
	function previewLines() {
		// Only units and items can be shown in the game this way (not building pieces, assemblies, gibs...).
		const { actor, device: heldDevice } = app.gameThing();
		const kind = actor && spawnKind(actor.cls);
		if (kind !== 'actor' && kind !== 'item') {
			return null;
		}
		const files = new Set(app.savedSinceLastPreview);
		files.add(actor.path);
		// An item designed on its own goes to the game in the hands of the last unit opened (or a soldier of the game's).
		const itemAlone = DEVICE_CLASSES.has(actor.cls);
		let holder = actor;
		if (itemAlone) {
			const last = readLocal('ms.lastHolder');
			const found = (last && app.lib.presets.find((p) => p.cls === 'AHuman' && p.doc.path === last.path && p.name === last.name)) || app.lib.find('Soldier Light', 'AHuman', null) || app.lib.presets.find((p) => p.cls === 'AHuman' && p.topLevel);
			if (!found) {
				app.toast('Studio needs a unit to hold ' + actor.name + ' in the game, and found none.', 'bad');
				return null;
			}
			holder = { path: found.doc.path, name: found.name, cls: 'AHuman' };
		}
		const device = heldDevice && !app.fs.isOfficial(heldDevice.path) ? heldDevice.path : null;
		if (device) {
			files.add(device);
		}
		const lines = [
			'// Written by Cortex Mod Studio. The game\'s Actor Editor watches this file and shows the unit named here.',
			'Stamp = ' + Date.now(),
			'Class = ' + holder.cls,
			'Preset = ' + holder.name,
			'Module = ' + holder.path.split('/')[0],
		];
		for (const file of files) {
			if (!/\/index\.ini$/i.test(file) && !app.fs.isOfficial(file)) {
				lines.push('ReloadFile = ' + file);
			}
		}
		// The held item picked in Studio goes in the unit's hands in game.
		const heldRef = itemAlone ? actor : heldDevice;
		const held = heldRef && app.lib.presets.find((p) => p.doc.path === heldRef.path && p.name === heldRef.name);
		if (held) {
			lines.push('DeviceClass = ' + held.cls, 'DevicePreset = ' + held.name, 'DeviceModule = ' + held.module);
		}
		return lines;
	}

	// The same request as text, for starting a game: Test in game, Sandbox and the Actor Editor then open with the unit you have open (the game
	// has no Preview.ini of its own yet, so it showed a default soldier).
	app.previewText = function () {
		const lines = previewLines();
		return lines ? lines.join('\r\n') + '\r\n' : null;
	};

	app.preview = async function (quiet) {
		const actor = app.gameThing().actor;
		if (!actor) {
			return;
		}
		await app.save(quiet);
		const lines = previewLines();
		if (!lines) {
			return;
		}
		try {
			await app.fs.writeDisk(PREVIEW_FILE, lines.join('\r\n') + '\r\n');
		} catch (e) {
			app.toast('Couldn\'t write the preview request: ' + e.message, 'bad');
			return;
		}
		app.savedSinceLastPreview.clear();
		if (quiet) {
			app.status('Live: sent ' + actor.name + ' to the game at ' + new Date().toLocaleTimeString() + '.');
		} else if (!readLocal('ms.previewExplained')) {
			MS.dialogs.previewHelp();
			writeLocal('ms.previewExplained', true);
		} else {
			app.toast('Sent to the game. In the Actor Editor it updates within a second.', 'good');
		}
	};

	// ---- Live mode: every change goes to the game as it's made ----

	// A change made while a drag or scrub is still under way (an open transaction) waits until it's let go; changes close together go as one.
	app.live = false;
	let liveTimer = 0;
	let liveBusy = false;
	let liveAgain = false;
	app.liveChanged = function () {
		if (!app.live || !app.gameThing().actor || !app.fs || !app.fs.writable) {
			return;
		}
		clearTimeout(liveTimer);
		liveTimer = setTimeout(sendLive, 250);
	};
	async function sendLive() {
		if (app.txn) {
			liveTimer = setTimeout(sendLive, 250);
			return;
		}
		if (liveBusy) {
			liveAgain = true;
			return;
		}
		liveBusy = true;
		try {
			await app.preview(true);
		} finally {
			liveBusy = false;
			if (liveAgain) {
				liveAgain = false;
				app.liveChanged();
			}
		}
	}
	app.setLive = (on, quiet) => setLive(on, quiet);
	function setLive(on, quiet) {
		app.live = on;
		const btn = $('btnLive');
		btn.classList.toggle('live-on', on);
		btn.textContent = on ? '● Live on' : '● Live';
		writeLocal('ms.live', on);
		if (on && MS.popout && MS.popout.announce) {
			MS.popout.announce('game-link');
		}
		if (quiet) {
			return;
		}
		if (on) {
			app.status('Live: every change is sent to the game when you make it. Start the game with Actor Editor.bat or Test In Sandbox.bat.');
			app.liveChanged();
		} else {
			app.status('Live off: press Preview in game to send changes.');
		}
	}

	// The unit view can be hidden, to edit numbers with the game showing the unit beside Studio (Live mode).
	function setViewShown(shown) {
		$('app').classList.toggle('no-view', !shown);
		$('btnView').textContent = shown ? '◧ Hide view' : '◧ Show view';
		writeLocal('ms.viewHidden', !shown);
		if (shown) {
			app.requestDraw();
		}
	}

	function setListShown(shown) {
		$('app').classList.toggle('no-lib', !shown);
		$('btnLib').textContent = shown ? '◨ Hide list' : '◨ Show list';
		writeLocal('ms.listHidden', !shown);
		app.requestDraw();
	}

	function download(blob, name) {
		const a = document.createElement('a');
		a.href = URL.createObjectURL(blob);
		a.download = name;
		document.body.append(a);
		a.click();
		setTimeout(() => {
			URL.revokeObjectURL(a.href);
			a.remove();
		}, 1000);
	}
	app.download = download;

	// ---- toolbar and keys ----

	function bindToolbar() {
		$('btnUndo').onclick = () => app.doUndo();
		$('btnRedo').onclick = () => app.doRedo();
		$('btnSave').onclick = () => MS.ide.saveMod();
		$('btnDiscard').onclick = () => app.discard();
		$('btnPreview').onclick = () => app.preview();
		$('terrainLayer').onchange = () => {
			app.view.terrainShow = $('terrainLayer').value;
			app.requestDraw();
		};
		$('btnSpawn').onclick = () => app.spawnInGame(false);
		$('btnGive').onclick = () => app.spawnInGame(true);
		$('btnLive').onclick = () => setLive(!app.live);
		$('btnView').onclick = () => setViewShown($('app').classList.contains('no-view'));
		if (readLocal('ms.viewHidden')) {
			setViewShown(false);
		}
		$('btnLib').onclick = () => setListShown($('app').classList.contains('no-lib'));
		if (readLocal('ms.listHidden')) {
			setListShown(false);
		}
		// Live is on unless you turned it off: a game started from Studio then follows every change (the unit, its sprites), with nothing to press.
		if (readLocal('ms.live') !== false) {
			setLive(true, true);
		}
		$('btnCopy').onclick = () => MS.dialogs.copyUnit();
		$('btnHelp').onclick = () => MS.dialogs.help();
		$('btnNew').onclick = () => MS.wizard.open();
		$('btnHistory').onclick = () => MS.history.open(app.actor && app.actor.path);
		$('btnScope').onclick = (e) => {
			e.stopPropagation();
			projectMenu();
		};
		for (const btn of document.querySelectorAll('.scope-seg button')) {
			btn.onclick = () => app.setLibrary(btn.dataset.lib === 'all');
		}
		updateScopeButton();
		for (const btn of document.querySelectorAll('#poseButtons button')) {
			btn.onclick = () => setPose(btn.dataset.pose);
		}
		$('btnPlay').onclick = () => setPlaying(!app.playing);
		$('speed').oninput = (e) => {
			app.speed = parseFloat(e.target.value);
		};
		$('btnFlip').onclick = () => {
			app.poseState.flip = !app.poseState.flip;
			$('btnFlip').classList.toggle('sel', app.poseState.flip);
			app.requestDraw();
		};
		$('aim').oninput = (e) => {
			app.poseState.aim = (parseFloat(e.target.value) * Math.PI) / 180;
			$('aimVal').textContent = e.target.value + '°';
			app.requestDraw();
		};
		$('sharp').onchange = (e) => {
			app.poseState.sharp = e.target.checked;
			app.requestDraw();
		};
		$('deviceSelect').onchange = (e) => {
			const v = e.target.value;
			if (v === '__pick') {
				MS.dialogs.pickItem();
				MS.panels.refreshDeviceSelect();
				return;
			}
			const [path, name] = v ? JSON.parse(v) : [null, null];
			app.setDevice(path ? app.lib.presets.find((p) => p.doc.path === path && p.name === name) : null);
		};
		for (const btn of document.querySelectorAll('#viewOptions button')) {
			btn.onclick = () => {
				const opt = btn.dataset.opt;
				app.view.options[opt] = !app.view.options[opt];
				const shown = $('viewOptions').querySelector('[data-opt="' + opt + '"]'); // (the right group's toolbar's, when it is the one clicked)
				shown.classList.toggle('sel', app.view.options[opt]);
				if (opt === 'outlines') {
					MS.panels.refresh();
				}
				app.requestDraw();
			};
		}
		setPlaying(true);
		setUpRightTools();
		$('viewTools').addEventListener('click', () => requestAnimationFrame(app.syncTools));
	}

	function setPose(pose) {
		app.poseState.pose = pose;
		for (const b of document.querySelectorAll('#poseButtons button')) {
			b.classList.toggle('sel', b.dataset.pose === pose);
		}
		app.requestDraw();
	}

	function setPlaying(on) {
		app.playing = on;
		$('btnPlay').textContent = on ? '❚❚' : '▶';
		$('btnPlay').classList.toggle('sel', !on);
		app.requestDraw();
	}

	function bindKeys() {
		window.addEventListener('keydown', (e) => {
			const typing = /^(INPUT|SELECT|TEXTAREA)$/.test(e.target.tagName) && e.target.type !== 'range' && e.target.type !== 'checkbox';
			const ctrl = e.ctrlKey || e.metaKey;
			if (ctrl && e.key.toLowerCase() === 's') {
				e.preventDefault();
				if (app.lib) {
					MS.ide.saveMod();
				}
				return;
			}
			// (In Deep mode with a file open, keys belong to the editor and its tabs, not the designer.)
			if (typing || !$('modal').hidden || $('app').classList.contains('code-active')) {
				if (e.key === 'Escape' && !$('modal').hidden) {
					MS.dialogs.close();
				}
				return;
			}
			if (ctrl && e.key.toLowerCase() === 'z') {
				e.preventDefault();
				e.shiftKey ? app.doRedo() : app.doUndo();
			} else if (ctrl && e.key.toLowerCase() === 'y') {
				e.preventDefault();
				app.doRedo();
			} else if (e.key === ' ') {
				e.preventDefault();
				setPlaying(!app.playing);
			} else if (e.key.toLowerCase() === 'f' && !ctrl) {
				$('btnFlip').click();
			} else if (e.key >= '1' && e.key <= '5') {
				setPose(['stand', 'walk', 'run', 'crouch', 'jump'][Number(e.key) - 1]);
			} else if (e.key.startsWith('Arrow') && app.selected) {
				e.preventDefault();
				const d = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] }[e.key];
				nudge(d[0], d[1], e.shiftKey);
			} else if (e.key === 'Delete' && app.selected) {
				MS.panels.removeSelected();
			} else if (e.key.toLowerCase() === 'l' && !ctrl) {
				$('btnLib').click();
			} else if (e.key.toLowerCase() === 'v' && !ctrl) {
				$('btnView').click();
			} else if (e.key.toLowerCase() === 'k' && !ctrl && e.shiftKey && app.view.options.skeleton) {
				MS.skeletonPictures();
			} else if (e.key.toLowerCase() === 'k' && !ctrl) {
				document.querySelector('#viewOptions [data-opt=skeleton]').click();
			} else if (e.key.toLowerCase() === 'i' && !ctrl) {
				app.isolate = !app.isolate;
				app.requestDraw();
			} else if (e.key === 'Escape') {
				app.select(app.rig ? app.rig.body : null);
			}
		});
	}

	// ---- small things ----

	// A line in the status bar. fadeMs (optional): it fades away after that long (the "read N presets" line at start), unless something replaces it first.
	let statusFade = 0;
	app.status = function (text, fadeMs) {
		const el = $('statusText');
		clearTimeout(statusFade);
		el.style.transition = 'none';
		el.style.opacity = '1';
		el.textContent = text;
		if (fadeMs) {
			statusFade = setTimeout(() => {
				el.style.transition = 'opacity 1.2s';
				el.style.opacity = '0';
				statusFade = setTimeout(() => {
					el.textContent = '';
					el.style.transition = 'none';
					el.style.opacity = '1';
				}, 1300);
			}, fadeMs);
		}
	};

	// action: {label, run}, a button on the message (like Undo after a save), which then stays up a little longer.
	app.toast = function (text, kind, action) {
		// Plain wording for what the browser and the file system say, and the same message once, not three times in a row.
		text = MS.friendly ? MS.friendly.say(text) : text;
		const now = Date.now();
		if (app.lastToast && app.lastToast.text === text && now - app.lastToast.at < 5000) {
			return;
		}
		app.lastToast = { text, at: now };
		const el = document.createElement('div');
		el.className = 'toast ' + (kind || '');
		el.textContent = text;
		if (action) {
			const b = document.createElement('button');
			b.className = 'toast-btn';
			b.textContent = action.label;
			b.onclick = () => {
				el.remove();
				action.run();
			};
			el.append(b);
		}
		$('toasts').append(el);
		setTimeout(() => el.remove(), action ? 9000 : kind === 'bad' ? 7000 : 4000);
	};

	// After Edit this mod: everything that shows what can be changed looks again.
	// The game's own files are read only, except while Studio is unlocked (the Game screen).
	app.lockedGameFile = (path) => app.fs.isOfficial(path) && !app.unlocked;

	app.refreshLocks = function () {
		updateTitle();
		MS.ide.refreshReadOnly();
		MS.panels.refresh();
		app.requestDraw();
	};

	function readLocal(key) {
		try {
			return JSON.parse(localStorage.getItem(key));
		} catch (e) {
			return null;
		}
	}

	function writeLocal(key, value) {
		try {
			localStorage.setItem(key, JSON.stringify(value));
		} catch (e) {
			// Private windows can refuse storage; Studio works without it.
		}
	}
	app.readLocal = readLocal;
	app.writeLocal = writeLocal;

	// The game folder is remembered between visits (Chrome and Edge keep folder handles in IndexedDB).
	function db() {
		return new Promise((resolve, reject) => {
			const req = indexedDB.open('cortex-mod-studio', 1);
			req.onupgradeneeded = () => req.result.createObjectStore('kv');
			req.onsuccess = () => resolve(req.result);
			req.onerror = () => reject(req.error);
		});
	}

	async function rememberFolder(handle) {
		try {
			const d = await db();
			d.transaction('kv', 'readwrite').objectStore('kv').put(handle, 'gameFolder');
		} catch (e) {
			// Not remembered; the folder is asked for again next time.
		}
	}

	async function rememberedFolder() {
		if (!MS.GameFS.canWrite) {
			return null;
		}
		try {
			const d = await db();
			return await new Promise((resolve) => {
				const req = d.transaction('kv').objectStore('kv').get('gameFolder');
				req.onsuccess = () => resolve(req.result || null);
				req.onerror = () => resolve(null);
			});
		} catch (e) {
			return null;
		}
	}

	app.sub = sub;
	window.addEventListener('DOMContentLoaded', () => app.init());
})();
