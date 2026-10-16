// Cortex Mod Studio: the app. Holds the open game folder, the unit being designed, edits with undo, saving and the in-game preview.
'use strict';
window.MS = window.MS || {};

(function () {
	const $ = (id) => document.getElementById(id);
	const { rotate, flipX, sub } = MS.geom;
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
		hidden: new Set(),
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
		bindToolbar();
		bindKeys();
		window.addEventListener('beforeunload', (e) => {
			if (app.dirtyDocs().length) {
				e.preventDefault();
				e.returnValue = '';
			}
		});
		const note = $('openNote');
		if (MS.GameFS.canWrite) {
			note.textContent = 'Pick the folder that has the game\'s program in it (with Data and Mods inside). Studio saves your work straight into it.';
		} else {
			note.textContent = 'This browser can read the game folder but not save into it: your changes are downloaded as a zip to unpack over the game folder. Chrome or Edge can save in place.';
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
			hookDocs();
		} catch (e) {
			progress.hidden = true;
			app.toast(e.message || String(e), 'bad');
			return;
		}
		$('welcome').hidden = true;
		$('statusMode').textContent = fs.writable ? 'Saving into ' + fs.name : 'Read-only: saves download as a zip';
		app.status('Read ' + app.lib.presets.length + ' presets from ' + app.lib.modules.length + ' modules.');
		if (app.lib.warnings.length) {
			console.warn(app.lib.warnings.join('\n'));
		}
		MS.panels.showLibrary();
		const last = readLocal('ms.lastActor');
		const preset = last && app.lib.presets.find((p) => p.doc.path === last.path && p.name === last.name);
		if (preset) {
			app.openActor(preset);
		} else {
			app.status('Pick a unit on the left to start.');
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

	app.openActor = function (preset) {
		app.actor = { path: preset.doc.path, name: preset.name, cls: preset.cls };
		app.selected = null;
		app.hidden.clear();
		app.poseState.frames = {};
		app.poseState.time = 0;
		writeLocal('ms.lastActor', { path: preset.doc.path, name: preset.name });
		app.device = defaultDevice(preset.node);
		app.rebuild(true);
		app.select(app.rig ? app.rig.body : null);
		app.view.fit();
		MS.panels.markLibrary();
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

	app.setDevice = function (preset) {
		app.device = preset ? { path: preset.doc.path, name: preset.name } : null;
		app.rebuild();
		MS.panels.refresh();
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
		const locked = app.lib.isProtected(app.lib.docFor(app.actor.path));
		t.innerHTML = '';
		const b = document.createElement('b');
		b.textContent = app.actor.name;
		t.append(b, ' · ' + app.actor.path);
		const badge = document.createElement('span');
		badge.className = 'badge ' + (locked ? 'lock' : 'own');
		badge.textContent = locked ? 'built-in: make a copy to change it' : 'your mod';
		t.append(badge);
		$('btnCopy').disabled = false;
		$('btnCopy').classList.toggle('attention', locked);
		$('btnPreview').disabled = locked;
	}

	app.select = function (part) {
		app.selected = part;
		MS.panels.refresh();
		app.requestDraw();
	};

	app.onHover = function (part) {
		MS.panels.hoverTree(part);
		const hint = $('viewHint');
		hint.textContent = part ? part.name + ' — drag to move · Shift+drag to move the picture' : 'Drag a part to move it · wheel to zoom · double-click to fit';
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
		}
	};

	app.doRedo = function () {
		const entry = app.redo.pop();
		if (entry) {
			applyHistory(entry, 'after');
			app.undo.push(entry);
			app.status('Redid: ' + entry.label);
			updateUndo();
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
		const device = app.rig.device;
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
		d.plan = plan.map((s) => ({ partId: s.part.id, key: s.key, start: s.start, convert: s.convert }));
		app.begin('move ' + d.part.name);
		return true;
	};

	app.dragTo = function (d, delta, snap) {
		for (const step of d.plan) {
			const part = app.rig.parts.find((p) => p.id === step.partId);
			if (!part) {
				continue;
			}
			const change = step.convert(delta);
			const round = (v) => Math.round(v / snap) * snap;
			const node = app.lib.ownBlock(part.anchor, part.path).node;
			if (node) {
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
		const n = app.dirtyDocs().length;
		const el = $('statusFiles');
		el.textContent = n ? n + ' unsaved file' + (n > 1 ? 's' : '') : 'All saved';
		el.className = n ? 'dirty' : '';
		$('btnSave').disabled = !n && !(app.fs && !app.fs.writable && app.fs.pendingWrites.size);
	}
	app.updateFileStatus = updateFileStatus;

	app.save = async function () {
		const docs = app.dirtyDocs();
		let written = 0;
		for (const doc of docs) {
			if (app.lib.isProtected(doc)) {
				app.toast('Not saving ' + doc.path + ': it is one of the game\'s own files.', 'bad');
				continue;
			}
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
		if (written) {
			app.toast('Saved ' + written + ' file' + (written > 1 ? 's' : '') + '.', 'good');
		}
		return written;
	};

	app.preview = async function () {
		if (!app.actor) {
			return;
		}
		await app.save();
		const files = new Set(app.savedSinceLastPreview);
		files.add(app.actor.path);
		const device = app.device && !app.fs.isOfficial(app.device.path) ? app.device.path : null;
		if (device) {
			files.add(device);
		}
		const lines = [
			'// Written by Cortex Mod Studio. The game\'s Actor Editor watches this file and shows the unit named here.',
			'Stamp = ' + Date.now(),
			'Class = ' + app.actor.cls,
			'Preset = ' + app.actor.name,
			'Module = ' + app.actor.path.split('/')[0],
		];
		for (const file of files) {
			if (!/\/index\.ini$/i.test(file) && !app.fs.isOfficial(file)) {
				lines.push('ReloadFile = ' + file);
			}
		}
		try {
			await app.fs.writeDisk(PREVIEW_FILE, lines.join('\r\n') + '\r\n');
		} catch (e) {
			app.toast('Couldn\'t write the preview request: ' + e.message, 'bad');
			return;
		}
		app.savedSinceLastPreview.clear();
		if (!readLocal('ms.previewExplained')) {
			MS.dialogs.previewHelp();
			writeLocal('ms.previewExplained', true);
		} else {
			app.toast('Sent to the game. In the Actor Editor it updates within a second.', 'good');
		}
	};

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
		$('btnSave').onclick = () => app.save();
		$('btnPreview').onclick = () => app.preview();
		$('btnCopy').onclick = () => MS.dialogs.copyUnit();
		$('btnHelp').onclick = () => MS.dialogs.help();
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
			if (typing || !$('modal').hidden) {
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

	app.toast = function (text, kind) {
		const el = document.createElement('div');
		el.className = 'toast ' + (kind || '');
		el.textContent = text;
		$('toasts').append(el);
		setTimeout(() => el.remove(), kind === 'bad' ? 7000 : 4000);
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
