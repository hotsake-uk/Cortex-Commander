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
		rig: null,
		actor: null, // {path, name, cls}: found again by these after undo, since undo re-reads the file
		actorNode: null,
		device: null, // {path, name} of the held item preset shown in the front hand
		selected: null,
		hidden: new Set(), // ids of parts hidden in the view (the view only: the unit's files are untouched)
		collapsed: new Set(), // ids of parts whose children are folded away in the parts list
		isolate: false,
		poseState: { flip: false, aim: 0, pose: 'stand', time: 0, sharp: false, frames: {}, animateSprites: true },
		playing: true,
		speed: 1,
		undo: [],
		redo: [],
		txn: null,
		frames: new Map(),
		groundY: null,
		savedSinceLastPreview: new Set(),
	};
	MS.app = app;

	// ---- start-up ----

	app.init = function () {
		app.view = new MS.Viewport($('view'), app);
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
			app.lib.onDocDirty = () => updateFileStatus();
			await app.lib.load(report);
			await MS.history.loadEditable();
			$('btnHistory').hidden = !MS.history.available();
			report('Getting ready…', 1);
			hookDocs();
		} catch (e) {
			progress.hidden = true;
			app.toast(e.message || String(e), 'bad');
			return;
		}
		$('welcome').hidden = true;
		$('statusMode').textContent = fs.writable ? 'Saving into ' + fs.name : 'Read-only: saves download as a zip';
		const units = app.lib.presets.filter((p) => p.cls === 'AHuman' && p.topLevel).length;
		const summary = 'Read ' + app.lib.presets.length + ' presets (' + units + ' units) from ' + app.lib.docs.size + ' files in ' + app.lib.modules.length + ' modules.';
		app.status(summary);
		if (app.lib.warnings.length) {
			console.warn(app.lib.warnings.join('\n'));
			app.toast(app.lib.warnings.length + ' files couldn\'t be read, for example: ' + app.lib.warnings[0], 'bad');
		}
		MS.panels.showLibrary();
		updateScopeButton();
		MS.ide.gameLoaded();
		const last = readLocal('ms.lastActor');
		const preset = last && app.lib.presets.find((p) => p.doc.path === last.path && p.name === last.name);
		if (preset) {
			app.openActor(preset);
		} else {
			app.status(summary + ' Pick a unit on the left to start.');
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

	app.scope = (() => {
		const saved = readLocal('ms.scope');
		return Array.isArray(saved) ? new Set(saved) : null; // lower-cased module names; null is all of them
	})();

	app.inScope = function (pathOrModule) {
		return !app.scope || app.scope.has(String(pathOrModule).split('/')[0].toLowerCase());
	};

	function updateScopeButton() {
		const total = app.fs ? app.fs.modules().length : 0;
		const b = $('btnScope');
		b.textContent = (!app.scope ? 'All modules' : app.scope.size === 1 ? [...app.scope][0].replace(/\.rte$/, '') : app.scope.size + (total ? ' of ' + total : '') + ' modules') + ' ▾';
		b.classList.toggle('on', !!app.scope);
	}

	function setScope(names) {
		const all = app.fs ? app.fs.modules().map((m) => m.name.toLowerCase()) : [];
		app.scope = !names || names.size === 0 || all.every((n) => names.has(n)) ? null : names;
		writeLocal('ms.scope', app.scope ? [...app.scope] : null);
		updateScopeButton();
		if (app.lib) {
			MS.panels.showLibrary();
		}
		MS.ide.scopeChanged();
	}

	function scopeMenu() {
		const old = $('scopeMenu');
		if (old) {
			old.remove();
			return;
		}
		if (!app.fs) {
			return;
		}
		const mods = app.fs.modules();
		const menu = document.createElement('div');
		menu.className = 'ctx-menu scope-menu';
		menu.id = 'scopeMenu';
		const chosen = new Set(app.scope || mods.map((m) => m.name.toLowerCase()));
		const boxes = [];
		const quick = document.createElement('div');
		quick.className = 'scope-quick';
		const quickBtn = (text, pick) => {
			const b = document.createElement('button');
			b.textContent = text;
			b.onclick = () => {
				chosen.clear();
				for (const m of mods.filter(pick)) {
					chosen.add(m.name.toLowerCase());
				}
				for (const box of boxes) {
					box.checked = chosen.has(box.value);
				}
				setScope(new Set(chosen));
			};
			quick.append(b);
		};
		quickBtn('All', () => true);
		quickBtn('My mods', (m) => m.home === 'Mods');
		quickBtn('Base only', (m) => /^base\.rte$/i.test(m.name));
		menu.append(quick);
		for (const [title, home] of [['Your mods', 'Mods'], ['Game', 'Data']]) {
			const list = mods.filter((m) => m.home === home).sort((a, b) => a.name.localeCompare(b.name));
			if (!list.length) {
				continue;
			}
			const head = document.createElement('div');
			head.className = 'scope-head';
			head.textContent = title;
			menu.append(head);
			for (const m of list) {
				const label = document.createElement('label');
				const box = document.createElement('input');
				box.type = 'checkbox';
				box.value = m.name.toLowerCase();
				box.checked = chosen.has(box.value);
				box.onchange = () => {
					box.checked ? chosen.add(box.value) : chosen.delete(box.value);
					setScope(new Set(chosen));
				};
				boxes.push(box);
				const info = app.lib && app.lib.moduleOf(m.name + '/Index.ini');
				label.append(box, ' ' + m.name + (info && info.title && info.title !== m.name ? '  ·  ' + info.title : ''));
				menu.append(label);
			}
		}
		document.body.append(menu);
		const r = $('btnScope').getBoundingClientRect();
		menu.style.left = Math.min(r.left, window.innerWidth - menu.offsetWidth - 8) + 'px';
		menu.style.top = r.bottom + 4 + 'px';
		const close = (e) => {
			if (!menu.contains(e.target) && e.target !== $('btnScope')) {
				menu.remove();
				document.removeEventListener('pointerdown', close, true);
			}
		};
		setTimeout(() => document.addEventListener('pointerdown', close, true), 0);
	}
	app.updateScopeButton = updateScopeButton;

	app.openActor = function (preset) {
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

	// Rebuilds the rig from the current files, keeping the selection.
	app.rebuild = function (fresh) {
		const selectedId = app.selected ? app.selected.id : null;
		app.lib.invalidate();
		app.actorNode = app.findActorNode();
		if (!app.actorNode) {
			app.rig = null;
			app.selected = null;
			MS.panels.refresh();
			app.requestDraw();
			return;
		}
		app.rig = new MS.Rig(app.lib, app.actorNode);
		const deviceNode = findDeviceNode();
		app.rig.setDevice(deviceNode);
		app.selected = (selectedId && app.rig.parts.find((p) => p.id === selectedId)) || (fresh ? app.rig.body : app.selected && app.rig.body);
		if (fresh || app.groundY === null) {
			app.groundY = standingGround();
		}
		updateTitle();
		if (fresh) {
			MS.panels.refresh();
		}
		app.requestDraw();
	};

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
		if (kind === 'mod') {
			const module = app.actor.path.split('/')[0];
			const edit = document.createElement('button');
			edit.className = 'badge-btn';
			edit.textContent = 'Edit this mod';
			edit.title = 'Studio keeps ' + module + ' as it is now, so History can put any file back, then lets you change it';
			edit.onclick = () => MS.history.editMod(module);
			t.append(edit);
		}
		$('btnCopy').disabled = false;
		$('btnCopy').classList.toggle('attention', locked);
		$('btnPreview').disabled = locked;
		$('btnLive').disabled = locked || !app.fs || !app.fs.writable;
	}

	// A part is hidden when it, or a part it hangs from, has its eye off.
	app.isHidden = function (part) {
		for (let p = part; p; p = p.parent) {
			if (app.hidden.has(p.id)) {
				return true;
			}
		}
		return false;
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

	app.onZoom = function (z) {
		$('zoomVal').textContent = (Math.round(z * 10) / 10) + '×';
	};

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
		if (app.playing && app.rig) {
			app.poseState.time += dt * app.speed;
			drawQueued = true;
		}
		if (drawQueued) {
			drawQueued = false;
			app.view.render();
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
		app.undo.push({ label: txn.label, docs });
		if (app.undo.length > 200) {
			app.undo.shift();
		}
		app.redo = [];
		updateUndo();
		updateFileStatus();
		app.liveChanged();
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
				if (part.role === 'foot' || !parentPose) {
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
				app.toast(reason, 'bad');
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
		if (written && !quiet) {
			const undo = MS.history.available() ? { label: 'Undo', run: () => MS.history.undoSave(saved) } : null;
			app.toast('Saved ' + written + ' file' + (written > 1 ? 's' : '') + '.', 'good', undo);
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

	app.preview = async function (quiet) {
		if (!app.actor) {
			return;
		}
		await app.save(quiet);
		const files = new Set(app.savedSinceLastPreview);
		files.add(app.actor.path);
		// An item designed on its own goes to the game in the hands of the last unit opened (or a soldier of the game's).
		const itemAlone = app.rig && app.rig.isDevice;
		let holder = app.actor;
		if (itemAlone) {
			const last = readLocal('ms.lastHolder');
			const found = (last && app.lib.presets.find((p) => p.cls === 'AHuman' && p.doc.path === last.path && p.name === last.name)) || app.lib.find('Soldier Light', 'AHuman', null) || app.lib.presets.find((p) => p.cls === 'AHuman' && p.topLevel);
			if (!found) {
				app.toast('Studio needs a unit to hold ' + app.actor.name + ' in the game, and found none.', 'bad');
				return;
			}
			holder = { path: found.doc.path, name: found.name, cls: 'AHuman' };
		}
		const device = app.device && !app.fs.isOfficial(app.device.path) ? app.device.path : null;
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
		const heldRef = itemAlone ? app.actor : app.device;
		const held = heldRef && app.lib.presets.find((p) => p.doc.path === heldRef.path && p.name === heldRef.name);
		if (held) {
			lines.push('DeviceClass = ' + held.cls, 'DevicePreset = ' + held.name, 'DeviceModule = ' + held.module);
		}
		try {
			await app.fs.writeDisk(PREVIEW_FILE, lines.join('\r\n') + '\r\n');
		} catch (e) {
			app.toast('Couldn\'t write the preview request: ' + e.message, 'bad');
			return;
		}
		app.savedSinceLastPreview.clear();
		if (quiet) {
			app.status('Live: sent ' + app.actor.name + ' to the game at ' + new Date().toLocaleTimeString() + '.');
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
		if (!app.live || !app.actor || !app.fs || !app.fs.writable) {
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
	function setLive(on) {
		app.live = on;
		const btn = $('btnLive');
		btn.classList.toggle('live-on', on);
		btn.textContent = on ? '● Live on' : '● Live';
		writeLocal('ms.live', on);
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
		$('btnSave').onclick = () => (MS.ide.mode === 'deep' ? MS.ide.saveAll() : app.save());
		$('btnDiscard').onclick = () => app.discard();
		$('btnPreview').onclick = () => app.preview();
		$('btnLive').onclick = () => setLive(!app.live);
		$('btnView').onclick = () => setViewShown($('app').classList.contains('no-view'));
		if (readLocal('ms.viewHidden')) {
			setViewShown(false);
		}
		$('btnLib').onclick = () => setListShown($('app').classList.contains('no-lib'));
		if (readLocal('ms.listHidden')) {
			setListShown(false);
		}
		if (readLocal('ms.live')) {
			setLive(true);
		}
		$('btnCopy').onclick = () => MS.dialogs.copyUnit();
		$('btnHelp').onclick = () => MS.dialogs.help();
		$('btnNew').onclick = () => MS.wizard.open();
		$('btnHistory').onclick = () => MS.history.open(app.actor && app.actor.path);
		$('btnScope').onclick = (e) => {
			e.stopPropagation();
			scopeMenu();
		};
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
				btn.classList.toggle('sel', app.view.options[opt]);
				app.requestDraw();
			};
		}
		setPlaying(true);
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
					app.save();
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
			} else if (e.key.toLowerCase() === 'i' && !ctrl) {
				app.isolate = !app.isolate;
				app.requestDraw();
			} else if (e.key === 'Escape') {
				app.select(app.rig ? app.rig.body : null);
			}
		});
	}

	// ---- small things ----

	app.status = function (text) {
		$('statusText').textContent = text;
	};

	// action: {label, run}, a button on the message (like Undo after a save), which then stays up a little longer.
	app.toast = function (text, kind, action) {
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
