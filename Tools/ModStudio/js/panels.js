// Cortex Mod Studio: the side panels. The preset library on the left; the parts tree and the selected part's properties on the right.
'use strict';
window.MS = window.MS || {};

(function () {
	const $ = (id) => document.getElementById(id);
	let app = null;
	let tab = 'units';
	const ROLE_ICONS = { body: '▣', head: '●', jetpack: '⬒', fgarm: '╲', bgarm: '╲', fgleg: '┃', bgleg: '┃', foot: '▂', device: '⌐', magazine: '▮', attachable: '◆' };
	const TAB_CLASSES = {
		units: new Set(['AHuman']),
		attachments: new Set(['Attachable', 'AEmitter', 'AEJetpack', 'PEmitter']),
		items: new Set(['HDFirearm', 'HeldDevice', 'TDExplosive', 'ThrownDevice']),
	};

	// ---- drawing small pictures ----

	// Draws with the view's own drawing code, so thumbnails match the view.
	function painter() {
		const proto = MS.Viewport.prototype;
		return { app, frameFor: proto.frameFor, spriteOffset: proto.spriteOffset, itemRect: proto.itemRect, drawItem: proto.drawItem };
	}

	const thumbRigs = new Map();
	const pendingThumbs = new Set();

	function drawRigThumb(canvas, preset) {
		let rig = thumbRigs.get(preset.node);
		if (!rig) {
			try {
				rig = new MS.Rig(app.lib, preset.node);
			} catch (e) {
				return true;
			}
			thumbRigs.set(preset.node, rig);
		}
		const poses = rig.pose({ flip: false, aim: 0, pose: 'stand', time: 0, frames: {} });
		const list = MS.drawOrder(rig, poses);
		return drawItems(canvas, list);
	}

	function drawSpriteThumb(canvas, preset) {
		const ent = app.lib.resolve(preset.node);
		const file = ent.file('SpriteFile');
		const frame = app.sprite(file, Math.max(1, Math.round(ent.num('FrameCount', 1))), 0);
		const ctx = canvas.getContext('2d');
		ctx.clearRect(0, 0, canvas.width, canvas.height);
		if (!frame) {
			return !file || app.images.missing.has((MS.Images.framePaths(file, ent.num('FrameCount', 1))[0] || '').toLowerCase());
		}
		const z = Math.max(1, Math.floor(Math.min(canvas.width / frame.width, canvas.height / frame.height)));
		ctx.imageSmoothingEnabled = false;
		const w = frame.width * Math.min(z, 4);
		const h = frame.height * Math.min(z, 4);
		ctx.drawImage(frame.canvas, Math.floor((canvas.width - w) / 2), Math.floor((canvas.height - h) / 2), w, h);
		return true;
	}

	// Draws a list of posed items fitted into the canvas. Returns false while sprites are still loading.
	function drawItems(canvas, list) {
		const p = painter();
		let box = null;
		let complete = true;
		for (const item of list) {
			const r = p.itemRect(item);
			if (!r) {
				complete = false;
				continue;
			}
			for (const c of r) {
				box = box || { x0: c.x, y0: c.y, x1: c.x, y1: c.y };
				box.x0 = Math.min(box.x0, c.x);
				box.y0 = Math.min(box.y0, c.y);
				box.x1 = Math.max(box.x1, c.x);
				box.y1 = Math.max(box.y1, c.y);
			}
		}
		const ctx = canvas.getContext('2d');
		ctx.setTransform(1, 0, 0, 1, 0, 0);
		ctx.clearRect(0, 0, canvas.width, canvas.height);
		if (!box) {
			return complete;
		}
		const w = box.x1 - box.x0;
		const h = box.y1 - box.y0;
		const z = Math.max(1, Math.min(4, Math.floor(Math.min((canvas.width - 2) / w, (canvas.height - 2) / h))));
		ctx.imageSmoothingEnabled = false;
		ctx.translate(Math.floor(canvas.width / 2 - (box.x0 + w / 2) * z), Math.floor(canvas.height / 2 - (box.y0 + h / 2) * z));
		ctx.scale(z, z);
		for (const item of list) {
			p.drawItem(ctx, item);
		}
		return complete;
	}

	function queueThumb(canvas, draw) {
		const done = draw();
		if (!done) {
			pendingThumbs.add({ canvas, draw });
		}
	}

	let thumbTimer = null;
	function retryThumbs() {
		if (thumbTimer) {
			return;
		}
		thumbTimer = setTimeout(() => {
			thumbTimer = null;
			for (const t of [...pendingThumbs]) {
				if (!t.canvas.isConnected || t.draw()) {
					pendingThumbs.delete(t);
				}
			}
		}, 120);
	}

	// ---- library ----

	function libraryPresets() {
		const wanted = TAB_CLASSES[tab];
		const query = $('libSearch').value.trim().toLowerCase();
		const mineOnly = $('libMineOnly').checked;
		const seen = new Set();
		const out = [];
		for (const p of app.lib.presets) {
			if (!wanted.has(p.cls) || (tab !== 'attachments' && !p.topLevel)) {
				continue;
			}
			if (mineOnly && app.fs.isOfficial(p.module)) {
				continue;
			}
			if (query && !(p.name.toLowerCase().includes(query) || p.module.toLowerCase().includes(query))) {
				continue;
			}
			// Attachments: only ones with a picture, and each name once.
			if (tab === 'attachments') {
				const key = p.module + '/' + p.name;
				if (seen.has(key) || !app.lib.resolve(p.node).file('SpriteFile')) {
					continue;
				}
				seen.add(key);
			}
			out.push(p);
		}
		return out;
	}

	function showLibrary() {
		const list = $('libList');
		list.innerHTML = '';
		const presets = libraryPresets();
		$('libCount').textContent = presets.length + ' shown';
		if (!presets.length) {
			const empty = document.createElement('div');
			empty.className = 'lib-empty';
			empty.textContent = 'Nothing matches.';
			list.append(empty);
			return;
		}
		const observer = new IntersectionObserver((entries) => {
			for (const e of entries) {
				if (e.isIntersecting && !e.target.dataset.drawn) {
					e.target.dataset.drawn = '1';
					const preset = e.target._preset;
					queueThumb(e.target, () => (tab === 'units' ? drawRigThumb(e.target, preset) : drawSpriteThumb(e.target, preset)));
				}
			}
		}, { root: list });
		let module = null;
		const limit = 600;
		for (const p of presets.slice(0, limit)) {
			if (p.module !== module) {
				module = p.module;
				const head = document.createElement('div');
				head.className = 'group-head';
				const info = app.lib.moduleOf(p.doc.path);
				head.textContent = (info ? info.title : p.module) + (app.fs.isOfficial(p.module) ? '' : ' · your mod');
				list.append(head);
			}
			const row = document.createElement('div');
			row.className = 'lib-row';
			row._preset = p;
			const canvas = document.createElement('canvas');
			canvas.width = 40;
			canvas.height = 40;
			canvas._preset = p;
			observer.observe(canvas);
			const txt = document.createElement('div');
			txt.className = 'txt';
			txt.innerHTML = '<div class="name"></div><div class="meta"></div>';
			txt.firstChild.textContent = p.name;
			txt.lastChild.textContent = p.cls + ' · ' + p.doc.path.split('/').slice(1).join('/');
			row.append(canvas, txt);
			if (tab === 'units') {
				row.onclick = () => app.openActor(p);
				row.title = 'Open ' + p.name;
			} else if (tab === 'items') {
				row.onclick = () => {
					app.setDevice(p);
					app.toast(p.name + ' is now in the front hand.');
				};
				row.title = 'Put ' + p.name + ' in the unit\'s hand';
			} else {
				const add = document.createElement('button');
				add.className = 'small-btn add';
				add.textContent = '+ Add';
				add.title = 'Add to the selected part';
				add.onclick = (e) => {
					e.stopPropagation();
					MS.dialogs.attach(p, app.selected);
				};
				row.append(add);
				row.onclick = () => MS.dialogs.attach(p, app.selected);
				row.title = 'Add ' + p.name + ' to the selected part';
			}
			list.append(row);
		}
		if (presets.length > limit) {
			const more = document.createElement('div');
			more.className = 'lib-empty';
			more.textContent = (presets.length - limit) + ' more: search to narrow the list.';
			list.append(more);
		}
		markLibrary();
	}

	function markLibrary() {
		for (const row of document.querySelectorAll('.lib-row')) {
			const p = row._preset;
			const selected = tab === 'units' ? app.actor && p.doc.path === app.actor.path && p.name === app.actor.name : tab === 'items' ? app.device && p.doc.path === app.device.path && p.name === app.device.name : false;
			row.classList.toggle('sel', !!selected);
		}
	}

	// ---- parts tree ----

	function renderTree() {
		const tree = $('tree');
		tree.innerHTML = '';
		if (!app.rig) {
			return;
		}
		const addRow = (part, depth) => {
			const row = document.createElement('div');
			row.className = 'tree-row' + (part === app.selected ? ' sel' : '');
			row.style.paddingLeft = 6 + depth * 14 + 'px';
			row._part = part;
			const icon = document.createElement('span');
			icon.className = 'role';
			icon.textContent = ROLE_ICONS[part.role] || '·';
			const label = document.createElement('span');
			label.className = 'label';
			const preset = part.ent.presetName || (part.ent.node.child('CopyOf') ? part.ent.node.child('CopyOf').value : '');
			label.textContent = { body: 'Body', head: 'Head', jetpack: 'Jetpack', fgarm: 'Front arm', bgarm: 'Back arm', fgleg: 'Front leg', bgleg: 'Back leg', foot: 'Foot', magazine: 'Magazine', device: 'Held item', attachable: 'Attachment' }[part.role] + ' ';
			if (preset) {
				const small = document.createElement('small');
				small.textContent = preset;
				label.append(small);
			}
			row.append(icon, label);
			if (app.lockReason(part)) {
				const lock = document.createElement('span');
				lock.className = 'role';
				lock.textContent = '🔒';
				lock.title = app.lockReason(part);
				row.append(lock);
			}
			row.onclick = () => app.select(part);
			row.onmouseenter = () => {
				app.view.hover = part;
				app.requestDraw();
			};
			row.onmouseleave = () => {
				app.view.hover = null;
				app.requestDraw();
			};
			tree.append(row);
			for (const child of part.children) {
				addRow(child, depth + 1);
			}
		};
		addRow(app.rig.body, 0);
		if (app.rig.device) {
			addRow(app.rig.device, 0);
		}
		const sel = tree.querySelector('.sel');
		if (sel) {
			sel.scrollIntoView({ block: 'nearest' });
		}
	}

	function hoverTree(part) {
		for (const row of document.querySelectorAll('.tree-row')) {
			row.classList.toggle('hover', row._part === part);
		}
	}

	// ---- inspector ----

	const closedSections = new Set(['All properties']);
	let fieldUpdaters = [];

	function section(parent, title, extra) {
		const s = document.createElement('div');
		s.className = 'section' + (closedSections.has(title) ? ' closed' : '');
		const h = document.createElement('h3');
		const span = document.createElement('span');
		span.textContent = title;
		h.append(span);
		if (extra) {
			h.append(extra);
		}
		h.onclick = (e) => {
			if (e.target.closest('button')) {
				return;
			}
			s.classList.toggle('closed');
			s.classList.contains('closed') ? closedSections.add(title) : closedSections.delete(title);
		};
		const rows = document.createElement('div');
		rows.className = 'rows';
		s.append(h, rows);
		parent.append(s);
		return rows;
	}

	function describeSource(part, key) {
		const node = part.ent.sourceOf(key);
		if (!node) {
			return { text: 'not set: the game uses its default', cls: '' };
		}
		if (node.isWithin(part.anchor)) {
			return { text: 'set here · ' + node.doc.path.split('/').pop() + ':' + (node.line || 'new'), cls: 'own' };
		}
		const top = node.topLevel;
		const name = top.child('PresetName') ? top.child('PresetName').value : node.doc.path;
		return { text: 'from ' + name + ' · ' + node.doc.module + (node.line ? ':' + node.line : ''), cls: '' };
	}

	// A scrubbable number: drag its letter sideways to change it.
	function numberBox(letter, value, onCommit, onScrub, disabled) {
		const box = document.createElement('div');
		box.className = 'num';
		const b = document.createElement('b');
		b.textContent = letter;
		const input = document.createElement('input');
		input.type = 'text';
		input.inputMode = 'decimal';
		input.value = MS.formatNumber(value);
		input.disabled = disabled;
		input.onchange = () => {
			const v = parseFloat(input.value);
			if (Number.isFinite(v)) {
				onCommit(v);
			}
		};
		input.onkeydown = (e) => {
			if (e.key === 'ArrowUp' || e.key === 'ArrowDown') {
				e.preventDefault();
				const step = (e.shiftKey ? 10 : 1) * (e.key === 'ArrowUp' ? 1 : -1);
				const v = (parseFloat(input.value) || 0) + step;
				input.value = MS.formatNumber(v);
				onCommit(v);
			} else if (e.key === 'Enter') {
				input.blur();
			}
		};
		if (onScrub && !disabled) {
			b.onpointerdown = (e) => {
				b.setPointerCapture(e.pointerId);
				const startX = e.clientX;
				const start = parseFloat(input.value) || 0;
				let began = false;
				b.onpointermove = (m) => {
					const v = Math.round(start + (m.clientX - startX) / 4);
					if (!began) {
						began = true;
						onScrub('begin');
					}
					input.value = MS.formatNumber(v);
					onScrub('move', v);
				};
				b.onpointerup = () => {
					b.onpointermove = null;
					b.onpointerup = null;
					if (began) {
						onScrub('end');
					}
				};
			};
		}
		box.append(b, input);
		return { box, input };
	}

	function vectorRow(rows, part, key, label, locked, fallbackKey) {
		const useKey = fallbackKey && !part.ent.has(key) && part.ent.has(fallbackKey) ? fallbackKey : key;
		const row = document.createElement('div');
		row.className = 'row';
		const l = document.createElement('label');
		l.textContent = label;
		l.title = useKey;
		const val = document.createElement('div');
		val.className = 'val';
		const v = () => part.ent.vec(useKey);
		const current = v();
		const scrub = (axis) => (phase, value) => {
			if (phase === 'begin') {
				app.begin('change ' + useKey);
			} else if (phase === 'move') {
				const node = app.ownBlock(part);
				if (!node) {
					return;
				}
				const now = v();
				node.setVector(useKey, axis === 'x' ? value : now.x, axis === 'y' ? value : now.y);
				app.lib.reindex();
				app.rebuild();
				part = app.selected && app.selected.id === part.id ? app.selected : part;
			} else {
				app.commit();
				refresh();
			}
		};
		const x = numberBox('X', current.x, (n) => app.setVector(part, useKey, n, v().y), scrub('x'), !!locked);
		const y = numberBox('Y', current.y, (n) => app.setVector(part, useKey, v().x, n), scrub('y'), !!locked);
		const src = describeSource(part, useKey);
		if (!part.ent.has(useKey)) {
			x.box.classList.add('defaulted');
			y.box.classList.add('defaulted');
		} else if (src.cls !== 'own') {
			x.box.classList.add('inherited');
			y.box.classList.add('inherited');
		}
		val.append(x.box, y.box);
		const note = document.createElement('div');
		note.className = 'src ' + src.cls;
		note.textContent = src.text;
		row.append(l, val, note);
		rows.append(row);
		fieldUpdaters.push(() => {
			const now = app.selected && app.selected.id === part.id ? app.selected.ent.vec(useKey) : null;
			if (now && document.activeElement !== x.input && document.activeElement !== y.input) {
				x.input.value = MS.formatNumber(now.x);
				y.input.value = MS.formatNumber(now.y);
			}
		});
	}

	function numberRow(rows, part, key, label, fallback, locked) {
		const row = document.createElement('div');
		row.className = 'row';
		const l = document.createElement('label');
		l.textContent = label;
		l.title = key;
		const val = document.createElement('div');
		val.className = 'val';
		const n = numberBox('#', part.ent.num(key, fallback), (value) => app.setValue(part, key, MS.formatNumber(value)), null, !!locked);
		if (!part.ent.has(key)) {
			n.box.classList.add('defaulted');
		} else if (describeSource(part, key).cls !== 'own') {
			n.box.classList.add('inherited');
		}
		val.append(n.box);
		const note = document.createElement('div');
		const src = describeSource(part, key);
		note.className = 'src ' + src.cls;
		note.textContent = src.text;
		row.append(l, val, note);
		rows.append(row);
	}

	function boolRow(rows, part, key, label, fallback, locked) {
		const row = document.createElement('div');
		row.className = 'row';
		const l = document.createElement('label');
		l.textContent = label;
		l.title = key;
		const val = document.createElement('div');
		val.className = 'val';
		const box = document.createElement('input');
		box.type = 'checkbox';
		box.checked = part.ent.bool(key, fallback);
		box.disabled = !!locked;
		box.onchange = () => app.setValue(part, key, box.checked ? '1' : '0');
		val.append(box);
		const note = document.createElement('div');
		const src = describeSource(part, key);
		note.className = 'src ' + src.cls;
		note.textContent = src.text;
		row.append(l, val, note);
		rows.append(row);
	}

	function textRow(rows, label, text, title) {
		const row = document.createElement('div');
		row.className = 'row';
		const l = document.createElement('label');
		l.textContent = label;
		const val = document.createElement('div');
		val.className = 'val';
		const span = document.createElement('span');
		span.style.cssText = 'overflow:hidden;text-overflow:ellipsis;white-space:nowrap;color:var(--text)';
		span.textContent = text;
		span.title = title || text;
		val.append(span);
		row.append(l, val);
		rows.append(row);
	}

	function framesStrip(rows, part) {
		const s = part.sprite;
		if (!s.file) {
			return;
		}
		const strip = document.createElement('div');
		strip.className = 'frames';
		const forced = app.poseState.frames[part.id];
		const count = Math.min(s.frameCount, 40);
		for (let i = 0; i < count; i++) {
			const c = document.createElement('canvas');
			c.width = 40;
			c.height = 40;
			c.title = 'Frame ' + i + (forced === i ? ' (shown)' : ': click to show this frame');
			if (forced === i) {
				c.classList.add('sel');
			}
			const draw = () => {
				const frame = app.sprite(s.file, s.frameCount, i);
				if (!frame) {
					return false;
				}
				const ctx = c.getContext('2d');
				ctx.imageSmoothingEnabled = false;
				ctx.clearRect(0, 0, 40, 40);
				const z = Math.max(1, Math.min(4, Math.floor(38 / Math.max(frame.width, frame.height))));
				ctx.drawImage(frame.canvas, Math.floor((40 - frame.width * z) / 2), Math.floor((40 - frame.height * z) / 2), frame.width * z, frame.height * z);
				return true;
			};
			queueThumb(c, draw);
			c.onclick = () => {
				if (app.poseState.frames[part.id] === i) {
					delete app.poseState.frames[part.id];
				} else {
					app.poseState.frames[part.id] = i;
				}
				refresh();
				app.requestDraw();
			};
			strip.append(c);
		}
		rows.append(strip);
		if (forced !== undefined) {
			const note = document.createElement('div');
			note.className = 'src';
			note.textContent = 'Showing frame ' + forced + ' (view only). Click it again to go back to the game\'s choice.';
			rows.append(note);
		}
	}

	function renderInspector() {
		const insp = $('inspector');
		insp.innerHTML = '';
		fieldUpdaters = [];
		const part = app.selected;
		if (!app.rig) {
			insp.innerHTML = '<div class="insp-empty">Pick a unit in the library to start designing.</div>';
			$('inspHead').textContent = 'Properties';
			return;
		}
		if (!part) {
			insp.innerHTML = '<div class="insp-empty">Click a part in the view or the list above.</div>';
			return;
		}
		$('inspHead').textContent = part.name;
		const locked = app.lockReason(part);
		const actorLocked = app.lib.isProtected(app.actorNode.doc);
		if (locked) {
			const n = document.createElement('div');
			n.className = 'notice';
			n.textContent = locked;
			if (actorLocked && part.anchor === app.actorNode) {
				const b = document.createElement('button');
				b.className = 'small-btn primary';
				b.textContent = 'Make my own copy';
				b.onclick = () => MS.dialogs.copyUnit();
				n.append(document.createElement('br'), b);
			}
			insp.append(n);
		} else if (part.role !== 'body' && part.anchor === app.actorNode && !part.ent.node.isWithin(app.actorNode)) {
			const n = document.createElement('div');
			n.className = 'notice info';
			n.textContent = 'This part is shared with other units. Changing it here copies it into this unit first, so the others stay as they are.';
			insp.append(n);
		}

		const where = section(insp, 'Preset');
		const ent = part.ent;
		textRow(where, 'Class', ent.cls);
		const presetName = ent.presetName || (ent.node.child('CopyOf') ? 'copy of ' + ent.node.child('CopyOf').value : '(written in place)');
		textRow(where, 'Preset', presetName);
		textRow(where, 'Written in', ent.node.doc.path + (ent.node.line ? ':' + ent.node.line : ''));
		const missing = app.lib.missingCopyOf(ent.node);
		if (missing) {
			const n = document.createElement('div');
			n.className = 'notice bad';
			n.textContent = 'CopyOf "' + missing + '" isn\'t a preset Studio can find. The game will stop loading on this.';
			where.append(n);
		}

		const sprite = section(insp, 'Sprite', (() => {
			const b = document.createElement('button');
			b.className = 'small-btn';
			b.textContent = 'Replace…';
			b.title = 'Use your own picture or sprite sheet for this part';
			b.disabled = !!locked;
			b.onclick = () => MS.dialogs.importSprite(part);
			return b;
		})());
		const s = part.sprite;
		textRow(sprite, 'File', s.file || '(none)');
		if (s.file && app.images.missing.has(MS.Images.framePaths(s.file, s.frameCount)[0].toLowerCase())) {
			const n = document.createElement('div');
			n.className = 'notice bad';
			n.textContent = 'This picture isn\'t in the game folder.';
			sprite.append(n);
		}
		framesStrip(sprite, part);
		numberRow(sprite, part, 'FrameCount', 'Frames', 1, locked);
		vectorRow(sprite, part, 'SpriteOffset', 'Picture offset', locked);

		const place = section(insp, 'Placement');
		if (part.role !== 'body' && part.role !== 'foot' && part.role !== 'device') {
			vectorRow(place, part, 'ParentOffset', 'On parent at', locked);
		}
		if (part.role !== 'body') {
			vectorRow(place, part, 'JointOffset', 'Pivot', locked);
		}
		if (!['body', 'fgarm', 'bgarm', 'bgleg', 'device'].includes(part.role)) {
			boolRow(place, part, 'DrawAfterParent', 'In front of parent', true, locked);
		}
		if (part.role === 'attachable' || part.role === 'jetpack') {
			boolRow(place, part, 'InheritsRotAngle', 'Turns with parent', true, locked);
		}

		if (part.role === 'fgarm' || part.role === 'bgarm') {
			const arm = section(insp, 'Arm');
			numberRow(arm, part, 'MaxLength', 'Reach (MaxLength)', 0, locked);
			vectorRow(arm, part, 'IdleOffset', 'Hand rests at', locked, 'HandIdleOffset');
			numberRow(arm, part, 'MoveSpeed', 'Hand speed', 0, locked);
			textRow(arm, 'Hand sprite', part.ent.file('Hand') || part.ent.file('HandSprite') || '(none)');
		}
		if (part.role === 'fgleg' || part.role === 'bgleg') {
			const leg = section(insp, 'Leg');
			vectorRow(leg, part, 'ContractedOffset', 'Bent (Contracted)', locked);
			vectorRow(leg, part, 'ExtendedOffset', 'Straight (Extended)', locked);
			vectorRow(leg, part, 'IdleOffset', 'Foot rests at', locked);
			numberRow(leg, part, 'MoveSpeed', 'Foot speed', 0, locked);
		}
		if (part.role === 'device') {
			const dev = section(insp, 'Holding');
			vectorRow(dev, part, 'StanceOffset', 'Held at (Stance)', locked);
			vectorRow(dev, part, 'SharpStanceOffset', 'Aiming (Sharp)', locked);
			vectorRow(dev, part, 'SupportOffset', 'Back hand at', locked);
			if (ent.cls === 'HDFirearm') {
				vectorRow(dev, part, 'MuzzleOffset', 'Muzzle', locked);
				vectorRow(dev, part, 'EjectionOffset', 'Shell ejection', locked);
			}
			boolRow(dev, part, 'OneHanded', 'One-handed', false, locked);
			boolRow(dev, part, 'Supportable', 'Back hand helps', true, locked);
		}
		if (part.role === 'body') {
			const stats = section(insp, 'Unit');
			numberRow(stats, part, 'Mass', 'Mass (kg)', 0, locked);
			numberRow(stats, part, 'Health', 'Health', 100, locked);
			numberRow(stats, part, 'GoldValue', 'Cost', 0, locked);
			numberRow(stats, part, 'ImpulseDamageThreshold', 'Impact damage at', 0, locked);
			const move = section(insp, 'Posture');
			numberRow(move, part, 'LookToAimRatio', 'Head follows aim', 0.7, locked);
			numberRow(move, part, 'ArmSwingRate', 'Arm swing', 1, locked);
			numberRow(move, part, 'WalkRotAngleTarget', 'Walk lean', 0, locked);
			numberRow(move, part, 'RunRotAngleTarget', 'Run lean', 0, locked);
			numberRow(move, part, 'CrouchRotAngleTarget', 'Crouch lean', 0.668, locked);
		}

		// Attachments on this part.
		const addBtn = document.createElement('button');
		addBtn.className = 'small-btn';
		addBtn.textContent = '+ Add…';
		addBtn.title = 'Add a helmet, pack or other attachment to this part';
		addBtn.disabled = !!locked;
		addBtn.onclick = () => MS.dialogs.attach(null, part);
		const att = section(insp, 'Attachments', addBtn);
		const listed = part.children.filter((c) => c.role === 'attachable');
		if (!listed.length) {
			const none = document.createElement('div');
			none.className = 'src';
			none.style.gridColumn = 'auto';
			none.textContent = 'None. Use Add, or drag one from the Attachments tab.';
			att.append(none);
		}
		for (const child of listed) {
			const row = document.createElement('div');
			row.className = 'btn-row';
			const b = document.createElement('button');
			b.className = 'small-btn';
			b.style.flex = '1';
			b.style.textAlign = 'left';
			b.textContent = child.name;
			b.onclick = () => app.select(child);
			row.append(b);
			if (canRemove(child)) {
				const r = document.createElement('button');
				r.className = 'small-btn danger';
				r.textContent = 'Remove';
				r.onclick = () => removePart(child);
				row.append(r);
			}
			att.append(row);
		}

		// Everything the part holds, and where each value comes from.
		const all = section(insp, 'All properties');
		const table = document.createElement('table');
		table.className = 'props-table';
		const lastOf = new Map();
		ent.entries.forEach((e, i) => lastOf.set(e.key, i));
		ent.entries.forEach((e, i) => {
			if (lastOf.get(e.key) !== i && !MS.LIST_KEYS.has(e.key)) {
				return;
			}
			const tr = document.createElement('tr');
			if (e.node.isWithin(part.anchor)) {
				tr.className = 'own';
			}
			const value = e.node.value === 'Vector' ? (() => {
				const v = MS.readVector(e.node);
				return v.x + ', ' + v.y;
			})() : e.node.children.length && e.node.child('CopyOf') ? e.node.value + ' (' + e.node.child('CopyOf').value + ')' : e.node.child('FilePath') ? e.node.child('FilePath').value : e.node.value;
			const top = e.node.topLevel;
			const from = top.child('PresetName') ? top.child('PresetName').value : e.node.doc.path;
			tr.innerHTML = '<td></td><td></td><td class="from"></td>';
			tr.children[0].textContent = e.key;
			tr.children[1].textContent = value;
			tr.children[2].textContent = from;
			tr.children[2].title = e.node.doc.path + ':' + e.node.line;
			table.append(tr);
		});
		all.append(table);
	}

	function canRemove(part) {
		if (part.role !== 'attachable') {
			return false;
		}
		const node = part.ent.node;
		return !app.lib.isProtected(node.doc) && (node.isWithin(part.anchor) || !app.fs.isOfficial(node.doc.path));
	}

	function removePart(part) {
		const node = part.ent.node;
		if (!node.isWithin(part.anchor) && !confirm('This attachment is written in ' + node.doc.path + ', which other units may share. Remove it there?')) {
			return;
		}
		app.edit('remove ' + part.name, () => node.remove());
		app.select(app.rig ? app.rig.body : null);
	}

	function removeSelected() {
		const part = app.selected;
		if (part && canRemove(part)) {
			removePart(part);
		}
	}

	// ---- held item list ----

	function refreshDeviceSelect() {
		const sel = $('deviceSelect');
		sel.innerHTML = '';
		const opt = (value, text) => {
			const o = document.createElement('option');
			o.value = value;
			o.textContent = text;
			sel.append(o);
			return o;
		};
		opt('', 'None');
		const seen = new Set();
		const add = (preset) => {
			const key = JSON.stringify([preset.doc.path, preset.name]);
			if (!seen.has(key)) {
				seen.add(key);
				opt(key, preset.name);
			}
		};
		if (app.actorNode) {
			for (const entry of app.lib.resolve(app.actorNode).all('AddInventory')) {
				const copyOf = entry.node.child('CopyOf');
				const preset = copyOf && app.lib.find(copyOf.value, entry.node.value, entry.node.doc.module);
				if (preset && TAB_CLASSES.items.has(preset.cls)) {
					add(preset);
				}
			}
		}
		if (app.device) {
			const p = app.lib.presets.find((x) => x.doc.path === app.device.path && x.name === app.device.name);
			if (p) {
				add(p);
			}
		}
		opt('__pick', 'Choose another…');
		sel.value = app.device ? JSON.stringify([app.device.path, app.device.name]) : '';
	}

	// ---- public ----

	function refresh() {
		renderTree();
		renderInspector();
		refreshDeviceSelect();
		markLibrary();
		app.updateFileStatus();
	}

	let valuesQueued = false;
	function refreshValues() {
		if (valuesQueued) {
			return;
		}
		valuesQueued = true;
		requestAnimationFrame(() => {
			valuesQueued = false;
			for (const f of fieldUpdaters) {
				f();
			}
		});
	}

	function init(theApp) {
		app = theApp;
		for (const b of document.querySelectorAll('#libTabs button')) {
			b.onclick = () => {
				tab = b.dataset.tab;
				for (const x of document.querySelectorAll('#libTabs button')) {
					x.classList.toggle('sel', x === b);
				}
				if (app.lib) {
					showLibrary();
				}
			};
		}
		let searchTimer = null;
		$('libSearch').oninput = () => {
			clearTimeout(searchTimer);
			searchTimer = setTimeout(() => app.lib && showLibrary(), 150);
		};
		$('libMineOnly').onchange = () => app.lib && showLibrary();
		renderInspector();
	}

	MS.panels = {
		init,
		showLibrary,
		markLibrary,
		refresh,
		refreshValues,
		hoverTree,
		refreshDeviceSelect,
		removeSelected,
		spriteLoaded: retryThumbs,
		drawItems,
		drawSpriteThumb,
		queueThumb,
		forgetThumbs: () => thumbRigs.clear(),
		TAB_CLASSES,
	};
})();
