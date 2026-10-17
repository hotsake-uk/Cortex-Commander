// Cortex Mod Studio: the side panels. The preset library on the left; the parts tree and the selected part's properties on the right.
'use strict';
window.MS = window.MS || {};

(function () {
	const $ = (id) => document.getElementById(id);
	let app = null;
	let tab = 'units';
	const ROLE_ICONS = { body: '▣', head: '●', jetpack: '⬒', fgarm: '╲', bgarm: '╲', fgleg: '┃', bgleg: '┃', foot: '▂', device: '⌐', magazine: '▮', attachable: '◆', flash: '✶', leg: '┃', turret: '◘', mounted: '⌐', engine: '▼', thruster: '▿', door: '◧' };
	// Small line icons (drawn here so they look the same everywhere and need no font).
	const ICON_PATHS = {
		eye: '<path d="M1 8s2.5-4.5 7-4.5S15 8 15 8s-2.5 4.5-7 4.5S1 8 1 8z"/><circle cx="8" cy="8" r="2"/>',
		'eye-off': '<path d="M1 8s2.5-4.5 7-4.5S15 8 15 8s-2.5 4.5-7 4.5S1 8 1 8z" opacity=".45"/><path d="M2.5 13.5l11-11"/>',
		down: '<path d="M4 6l4 4 4-4"/>',
		right: '<path d="M6 4l4 4-4 4"/>',
		code: '<path d="M5 4L1 8l4 4M11 4l4 4-4 4"/>',
		pencil: '<path d="M2 14l1-4 8-8 3 3-8 8z"/><path d="M9.5 3.5l3 3"/>',
	};

	function icon(name) {
		const span = document.createElement('span');
		span.className = 'ico-svg';
		span.innerHTML = '<svg viewBox="0 0 16 16" width="14" height="14" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">' + ICON_PATHS[name] + '</svg>';
		return span;
	}

	// Text that opens its place in the code editor when clicked.
	function codeLink(text, path, line, title) {
		const a = document.createElement('span');
		a.className = 'code-link';
		a.textContent = text;
		a.title = title || 'Open ' + path + (line ? ' at line ' + line : '') + ' in the code editor';
		a.onclick = (e) => {
			e.stopPropagation();
			MS.ide.openFile(path, line);
		};
		return a;
	}

	const FILE_LINK = /\.(lua|ini|png|bmp|jpe?g|wav|ogg|flac|mp3)$/i;

	// Fills a "set here / from ..." note: the place it names is a link to those lines.
	function fillSource(note, src) {
		note.className = 'src ' + src.cls;
		note.textContent = '';
		if (src.node && src.node.doc) {
			note.append(codeLink(src.text, src.node.doc.path, src.node.line));
		} else {
			note.textContent = src.text;
		}
	}

	const TAB_CLASSES = {
		units: new Set(['AHuman', 'ACrab']),
		craft: new Set(['ACDropShip', 'ACRocket']),
		attachments: new Set(['Attachable', 'AEmitter', 'AEJetpack', 'PEmitter']),
		items: new Set(['HDFirearm', 'HeldDevice', 'TDExplosive', 'ThrownDevice']),
		// Everything else with a picture: open it, edit its settings and its sprite like any other.
		more: new Set(['MOSRotating', 'MOSParticle', 'Magazine', 'Turret', 'ADoor', 'Arm', 'Leg', 'TerrainObject', 'BunkerAssembly']),
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
		const file = preset.cls === 'TerrainObject' ? ent.file('FGColorFile') || ent.file('BGColorFile') : ent.file('SpriteFile');
		const frame = app.sprite(file, Math.max(1, Math.round(ent.num('FrameCount', 1))), 0);
		const ctx = canvas.getContext('2d');
		ctx.clearRect(0, 0, canvas.width, canvas.height);
		if (file && /(^|\/)null\.png$/i.test(file)) {
			drawInvisible(canvas);
			return true;
		}
		if (!frame) {
			return !file || app.images.missing.has((MS.Images.framePaths(file, ent.num('FrameCount', 1))[0] || '').toLowerCase());
		}
		if (!hasPixels(frame.canvas)) {
			drawInvisible(canvas);
			return true;
		}
		const z = Math.max(1, Math.floor(Math.min(canvas.width / frame.width, canvas.height / frame.height)));
		ctx.imageSmoothingEnabled = false;
		const w = frame.width * Math.min(z, 4);
		const h = frame.height * Math.min(z, 4);
		ctx.drawImage(frame.canvas, Math.floor((canvas.width - w) / 2), Math.floor((canvas.height - h) / 2), w, h);
		return true;
	}

	// Items with nothing to draw (Base.rte/Null.png, or a sprite with no visible pixels), like creatures' claws and jaws, which are
	// invisible in the game on purpose: a dashed box with a crossed-out circle, so the empty thumbnail doesn't look like a failed load.
	function drawInvisible(canvas) {
		const ctx = canvas.getContext('2d');
		const w = canvas.width;
		const h = canvas.height;
		const s = Math.min(w, h);
		ctx.save();
		ctx.strokeStyle = 'rgba(200, 205, 215, 0.45)';
		ctx.lineWidth = Math.max(1, s / 24);
		ctx.setLineDash([s / 10, s / 14]);
		ctx.strokeRect(s * 0.12 + (w - s) / 2, s * 0.12 + (h - s) / 2, s * 0.76, s * 0.76);
		ctx.setLineDash([]);
		ctx.beginPath();
		ctx.arc(w / 2, h / 2, s * 0.2, 0, Math.PI * 2);
		ctx.moveTo(w / 2 - s * 0.14, h / 2 + s * 0.14);
		ctx.lineTo(w / 2 + s * 0.14, h / 2 - s * 0.14);
		ctx.stroke();
		ctx.restore();
		canvas.title = 'Invisible in the game: this item has no sprite (Base.rte/Null.png), on purpose';
	}

	function hasPixels(source) {
		try {
			const data = source.getContext('2d').getImageData(0, 0, source.width, source.height).data;
			for (let i = 3; i < data.length; i += 4) {
				if (data[i]) {
					return true;
				}
			}
			return false;
		} catch (e) {
			return true;
		}
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

	// What each class is called in the Type list.
	const CLASS_LABELS = {
		AHuman: 'Humans', ACrab: 'Crabs', ACDropShip: 'Dropships', ACRocket: 'Rockets',
		Attachable: 'Attachments', AEmitter: 'Emitters', AEJetpack: 'Jetpacks', PEmitter: 'Particle emitters',
		HDFirearm: 'Guns', HeldDevice: 'Tools and shields', TDExplosive: 'Grenades and explosives', ThrownDevice: 'Thrown',
	};

	// A preset's buy menu groups, inherited ones too (cached until the presets are read again).
	let groupCache = new Map();
	let groupCacheFor = null;
	function groupsOf(p) {
		if (groupCacheFor !== app.lib.presets) {
			groupCacheFor = app.lib.presets;
			groupCache = new Map();
		}
		let groups = groupCache.get(p.node);
		if (!groups) {
			groups = app.lib.resolve(p.node).all('AddToGroup').map((e) => e.node.value.trim()).filter(Boolean);
			groupCache.set(p.node, groups);
		}
		return groups;
	}

	// Fills the Type, Group and Mod lists from what's in the tab, keeping each choice while it still exists.
	function fillLibSelects(candidates) {
		const fill = (id, all, values, label) => {
			const select = $(id);
			const keep = select.value;
			select.innerHTML = '';
			select.append(new Option(all, ''));
			for (const v of values) {
				select.append(new Option(label ? label(v) : v, v));
			}
			select.value = values.includes(keep) ? keep : '';
		};
		const classes = [...new Set(candidates.map((p) => p.cls))].sort((a, b) => (CLASS_LABELS[a] || a).localeCompare(CLASS_LABELS[b] || b));
		fill('libType', 'Every type', classes, (c) => CLASS_LABELS[c] || c);
		$('libType').hidden = classes.length < 2;
		const type = $('libType').value;
		const groups = new Set();
		for (const p of candidates) {
			if (!type || p.cls === type) {
				for (const g of groupsOf(p)) {
					groups.add(g);
				}
			}
		}
		fill('libGroup', 'Every group', [...groups].sort());
		$('libGroup').hidden = !groups.size;
		const modules = [...new Set(candidates.map((p) => p.module))];
		fill('libModule', 'Every mod', modules, (m) => {
			const info = app.lib.moduleOf(m + '/Index.ini');
			return (info ? info.title : m) + (app.fs.isOfficial(m) ? '' : ' (yours)');
		});
	}

	function libraryPresets() {
		const wanted = TAB_CLASSES[tab];
		const query = $('libSearch').value.trim().toLowerCase();
		const mineOnly = $('libMineOnly').checked;
		const candidates = app.lib.presets.filter((p) => wanted.has(p.cls) && (tab === 'attachments' || p.topLevel) && app.inScope(p.module));
		fillLibSelects(candidates);
		const type = $('libType').value;
		const group = $('libGroup').value;
		const moduleName = $('libModule').value;
		const seen = new Set();
		const out = [];
		for (const p of candidates) {
			if (mineOnly && app.fs.isOfficial(p.module)) {
				continue;
			}
			if ((type && p.cls !== type) || (moduleName && p.module !== moduleName) || (group && !groupsOf(p).includes(group))) {
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
					queueThumb(e.target, () => (tab === 'units' || tab === 'craft' ? drawRigThumb(e.target, preset) : drawSpriteThumb(e.target, preset)));
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
			const twist = document.createElement('button');
			twist.className = 'lib-twist';
			twist.textContent = '▸';
			twist.title = 'Its files: the INI that makes it, its scripts, pictures, sounds and parts';
			twist.onclick = (e) => {
				e.stopPropagation();
				toggleFiles(row);
			};
			row.append(twist, canvas, txt);
			if (tab === 'units' || tab === 'craft') {
				row.onclick = (e) => app.openActor(p, { newTab: e.ctrlKey || e.metaKey });
				row.title = 'Open ' + p.name;
			} else if (tab === 'items') {
				// A click opens the item in the designer (its grip, muzzle, magazine, settings and sprite); with a unit open, Hold puts it in the unit's hand.
				const hold = document.createElement('button');
				hold.className = 'small-btn add';
				hold.textContent = 'Hold';
				hold.title = 'Put ' + p.name + ' in the open unit\'s front hand';
				hold.onclick = (e) => {
					e.stopPropagation();
					// The unit may be in another designer tab: Hold goes to it.
					const unitTab = MS.ide.designerOfFamily('unit');
					if (unitTab && unitTab !== MS.ide.active) {
						MS.ide.activate(unitTab);
					}
					if (app.rig && app.rig.cls === 'AHuman') {
						app.setDevice(p);
						app.toast(p.name + ' is now in the front hand.');
					} else {
						app.toast('Open a unit first, then Hold puts this in its hand.', 'bad');
					}
				};
				row.append(hold);
				row.onclick = (e) => app.openActor(p, { newTab: e.ctrlKey || e.metaKey });
				row.title = 'Open ' + p.name + ' in the designer';
			} else if (tab === 'more') {
				row.onclick = (e) => app.openActor(p, { newTab: e.ctrlKey || e.metaKey });
				row.title = 'Open ' + p.name + ' in the designer';
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
			if (app.libraryOn && app.project && p.module.toLowerCase() !== app.project.toLowerCase()) {
				// In the Library: clone it into the project (a copy in your mod you can change), then open it.
				const clone = document.createElement('button');
				clone.className = 'small-btn add';
				clone.textContent = 'Clone';
				clone.title = 'Copy ' + p.name + ' into ' + app.project + ' and open the copy';
				clone.onclick = (e) => {
					e.stopPropagation();
					MS.dialogs.copyUnit({ name: p.name, cls: p.cls, path: p.doc.path });
				};
				row.append(clone);
			}
			list.append(row);
		}
		if (presets.length > limit) {
			const more = document.createElement('div');
			more.className = 'lib-empty';
			more.textContent = (presets.length - limit) + ' more: search to narrow the list.';
			list.append(more);
		}
		if (openFiles) {
			const row = [...list.querySelectorAll('.lib-row')].find((r) => presetKey(r._preset) === openFiles);
			if (row) {
				toggleFiles(row, true);
			}
		}
		markLibrary();
	}

	// ---- an item's files, under its row (Design and Script mode) ----

	let openFiles = null; // the preset whose files are showing; one at a time
	const presetKey = (p) => p.doc.path + ':' + p.node.line;

	function toggleFiles(row, keepOpen) {
		const list = $('libList');
		const shown = list.querySelector('.lib-files');
		const shownRow = shown && shown.previousElementSibling;
		if (shown) {
			shown.remove();
			shownRow.classList.remove('open');
		}
		if (shownRow === row && !keepOpen) {
			openFiles = null;
			return;
		}
		openFiles = presetKey(row._preset);
		row.classList.add('open');
		row.after(filesBox(row._preset));
	}

	const FILE_GROUPS = [
		['ini', 'INI'],
		['lua', 'Scripts it runs'],
		['image', 'Pictures'],
		['sound', 'Sounds'],
		['parts', 'Parts'],
		['related', 'Related'],
		['named', 'Scripts that name it'],
	];

	function filesBox(preset) {
		const box = document.createElement('div');
		box.className = 'lib-files';
		const files = MS.nav ? MS.nav.files(preset) : null;
		if (!files) {
			box.innerHTML = '<div class="lf-empty">Studio is still reading the game.</div>';
			return box;
		}
		files.related = MS.nav.related(preset, files);
		for (const [kind, title] of FILE_GROUPS) {
			const items = files[kind];
			if (!items.length) {
				continue;
			}
			const head = document.createElement('div');
			head.className = 'lf-head';
			head.textContent = title;
			box.append(head);
			const SHOWN = 12;
			const names = new Map();
			for (const item of items) {
				const name = item.path ? item.path.split('/').pop().toLowerCase() : '';
				names.set(name, (names.get(name) || 0) + 1);
			}
			// Two files with one name (Pistol.png in two folders) show their folder too.
			const rows = items.map((item) => fileRow(kind, item, item.path && names.get(item.path.split('/').pop().toLowerCase()) > 1));
			box.append(...rows.slice(0, SHOWN));
			if (rows.length > SHOWN) {
				const more = document.createElement('button');
				more.className = 'lf-more';
				more.textContent = (rows.length - SHOWN) + ' more';
				more.onclick = (e) => {
					e.stopPropagation();
					more.replaceWith(...rows.slice(SHOWN));
				};
				box.append(more);
			}
		}
		return box;
	}

	function fileRow(kind, item, withFolder) {
		const row = document.createElement('div');
		row.className = 'lf-row';
		const icon = document.createElement('span');
		const name = document.createElement('span');
		name.className = 'lf-name';
		const note = document.createElement('span');
		note.className = 'lf-note';
		if (kind === 'related') {
			const p = item.preset;
			const designable = Object.values(TAB_CLASSES).some((set) => set.has(p.cls));
			icon.className = 'x-ext part';
			icon.textContent = '≈';
			name.textContent = p.name;
			note.textContent = item.why.join(', ');
			row.title = p.cls + ' · ' + p.doc.path + ':' + p.node.line + '\n' + item.why.join('\n') + '\n' + (designable ? 'Click to open it in the designer' : 'Click to open its lines');
			row.onclick = () => {
				if (designable) {
					app.openActor(p);
					MS.ide.activate(MS.ide.tabs.find((t) => t.kind === 'designer') || MS.ide.active);
				} else {
					MS.ide.openFile(p.doc.path, p.node.line);
				}
			};
		} else if (kind === 'parts') {
			icon.className = 'x-ext part';
			icon.textContent = '◆';
			name.textContent = item.preset.name;
			note.textContent = item.note;
			row.title = item.preset.cls + ' in ' + item.preset.doc.path + ':' + item.preset.node.line + ' (click to open its lines)';
			row.onclick = () => MS.ide.openFile(item.preset.doc.path, item.preset.node.line);
		} else {
			const ext = item.path.split('.').pop().toLowerCase();
			icon.className = 'x-ext ' + ext;
			icon.textContent = ext;
			name.textContent = item.path.split('/').slice(withFolder ? -2 : -1).join('/') + (item.line > 1 ? ':' + item.line : '');
			note.textContent = item.note;
			row.title = item.path + (item.line > 1 ? ':' + item.line : '') + (item.where ? '\nNamed in ' + item.where : '');
			row.onclick = () => openListedFile(kind, item);
			if (kind === 'image') {
				const edit = document.createElement('button');
				edit.className = 'lf-edit';
				edit.textContent = '✎';
				edit.title = 'Edit this picture';
				edit.onclick = (e) => {
					e.stopPropagation();
					MS.pixel.open({ file: item.path });
				};
				row.append(icon, name, note, edit);
				return row;
			}
		}
		row.append(icon, name, note);
		return row;
	}

	async function openListedFile(kind, item) {
		if (kind === 'sound') {
			try {
				const audio = new Audio(URL.createObjectURL(await app.fs.readFile(item.path)));
				await audio.play();
				app.status('Playing ' + item.path + '.');
			} catch (e) {
				app.toast('Couldn\'t play ' + item.path + ': ' + e.message, 'bad');
			}
			return;
		}
		let path = item.path;
		if (kind === 'image' && !(await app.fs.exists(path))) {
			// Frames are X000.png, X001.png...: open the first.
			path = MS.Images.framePaths(path, Math.max(2, item.frames || 2))[0];
		}
		MS.ide.openFile(path, item.line);
	}

	function markLibrary() {
		for (const row of document.querySelectorAll('.lib-row')) {
			const p = row._preset;
			const isActor = app.actor && p.doc.path === app.actor.path && p.name === app.actor.name;
			const selected = tab === 'units' || tab === 'craft' || tab === 'more' ? isActor : tab === 'items' ? isActor || (app.device && p.doc.path === app.device.path && p.name === app.device.name) : false;
			row.classList.toggle('sel', !!selected);
		}
	}

	// ---- parts tree ----

	function allParts() {
		return app.rig ? app.rig.parts : [];
	}

	function descendsFrom(part, ancestor) {
		for (let p = part; p; p = p.parent) {
			if (p === ancestor) {
				return true;
			}
		}
		return false;
	}

	// The eye: shows or hides a part (and what hangs from it) in the view only. Alt+click shows just this part, and its parents.
	function toggleEye(part, solo) {
		if (solo) {
			const others = allParts().filter((p) => !descendsFrom(p, part) && !descendsFrom(part, p));
			const alone = others.length && others.every((p) => app.hidden.has(p.id));
			app.hidden.clear();
			if (!alone) {
				for (const p of others) {
					app.hidden.add(p.id);
				}
			}
		} else if (app.hidden.has(part.id)) {
			app.hidden.delete(part.id);
		} else {
			app.hidden.add(part.id);
		}
		renderTree();
		app.requestDraw();
	}

	// Right-click a layer (in the Parts list or in the view): what you can do with it.
	let partMenuEl = null;
	function closePartMenu() {
		if (partMenuEl) {
			partMenuEl.remove();
			partMenuEl = null;
		}
		document.removeEventListener('pointerdown', outsidePartMenu, true);
	}
	function outsidePartMenu(e) {
		if (partMenuEl && !partMenuEl.contains(e.target)) {
			closePartMenu();
		}
	}
	function showPartMenu(part, x, y) {
		closePartMenu();
		if (!part || !app.rig) {
			return;
		}
		app.select(part);
		// A unit that isn't yours gets copied into the project on the first change, so its layers are not greyed out.
		const reason = app.needsCopy() ? null : app.lockReason(part);
		const item = (text, title, run, disabled) => {
			const b = document.createElement('button');
			b.textContent = text;
			b.title = title || '';
			b.disabled = !!disabled;
			b.onclick = () => {
				closePartMenu();
				run();
			};
			return b;
		};
		const menu = document.createElement('div');
		menu.className = 'ctx-menu';
		menu.append(
			item('Add attachment…', reason || 'Search everything and add it to ' + part.name + ' (a copy goes into your project)', () => MS.dialogs.attach(null, part), !!reason),
			item('Edit sprite…', part.sprite && part.sprite.file ? 'Draw on ' + part.name + '\'s picture' : 'This part has no picture', () => MS.pixel.open({ part }), !(part.sprite && part.sprite.file)),
			item('Resize sprite…', reason || 'Make the picture bigger or smaller on the grid, to the scale of the unit', () => MS.resize.open(part), !(part.sprite && part.sprite.file) || !!reason),
			item('Swap sprite…', reason || 'Put a different picture in ' + part.name, () => MS.dialogs.importSprite(part), !!reason),
			item(app.hidden.has(part.id) ? 'Show layer' : 'Hide layer', 'Show or hide it in the view only', () => toggleEye(part, false)),
			item('Its lines', 'Open its INI in the code editor', () => MS.ide.openFile(part.ent.node.doc.path, part.ent.node.line)));
		document.body.append(menu);
		partMenuEl = menu;
		menu.style.left = Math.max(4, Math.min(x, window.innerWidth - menu.offsetWidth - 8)) + 'px';
		menu.style.top = Math.max(4, Math.min(y, window.innerHeight - menu.offsetHeight - 8)) + 'px';
		setTimeout(() => document.addEventListener('pointerdown', outsidePartMenu, true), 0);
	}

	function updateTreeTools() {
		const n = app.hidden.size;
		const show = $('btnShowParts');
		if (show) {
			show.hidden = !n;
			show.textContent = 'Show all (' + n + ' hidden)';
		}
		const fold = $('btnFoldParts');
		if (fold) {
			fold.textContent = app.collapsed.size ? 'Unfold all' : 'Fold all';
		}
	}

	function renderTree() {
		const tree = $('tree');
		tree.innerHTML = '';
		updateTreeTools();
		if (!app.rig) {
			return;
		}
		// A part picked in the view opens up the parts above it.
		for (let p = app.selected && app.selected.parent; p; p = p.parent) {
			app.collapsed.delete(p.id);
		}
		const addRow = (part, depth) => {
			const hiddenSelf = app.hidden.has(part.id);
			const hiddenAbove = !hiddenSelf && app.isHidden(part);
			const row = document.createElement('div');
			row.className = 'tree-row' + (part === app.selected ? ' sel' : '') + (hiddenSelf ? ' off' : hiddenAbove ? ' dim' : '');
			row._part = part;
			for (let i = 0; i < depth; i++) {
				const guide = document.createElement('span');
				guide.className = 'indent';
				row.append(guide);
			}
			const folded = app.collapsed.has(part.id);
			const twisty = document.createElement('button');
			twisty.className = 'twisty';
			if (part.children.length) {
				twisty.append(icon(folded ? 'right' : 'down'));
				twisty.title = (folded ? 'Show the ' : 'Fold the ') + part.children.length + ' part' + (part.children.length > 1 ? 's' : '') + ' on ' + part.name;
				twisty.onclick = (e) => {
					e.stopPropagation();
					folded ? app.collapsed.delete(part.id) : app.collapsed.add(part.id);
					renderTree();
				};
			} else {
				twisty.disabled = true;
			}
			row.append(twisty);
			const roleIcon = document.createElement('span');
			roleIcon.className = 'role';
			roleIcon.textContent = ROLE_ICONS[part.role] || '·';
			const label = document.createElement('span');
			label.className = 'label';
			const preset = part.ent.presetName || (part.ent.node.child('CopyOf') ? part.ent.node.child('CopyOf').value : '');
			label.textContent = (MS.Rig.ROLE_NAMES[part.role] || part.role) + ' ';
			if (preset) {
				const small = document.createElement('small');
				small.textContent = preset;
				label.append(small);
			}
			if (folded) {
				const count = document.createElement('small');
				count.className = 'folded-count';
				count.textContent = ' +' + part.children.length;
				label.append(count);
			}
			if (app.view && app.view.options.outlines) {
				const swatch = document.createElement('span');
				swatch.className = 'swatch';
				swatch.style.background = MS.partColor(part);
				swatch.title = 'This part\'s outline colour in the view';
				row.append(swatch);
			}
			row.append(roleIcon, label);
			if (app.lockReason(part)) {
				const lock = document.createElement('span');
				lock.className = 'role';
				lock.textContent = '🔒';
				lock.title = app.lockReason(part);
				row.append(lock);
			}
			const code = document.createElement('button');
			code.className = 'row-btn';
			code.append(icon('code'));
			code.title = 'Open ' + part.ent.node.doc.path + (part.ent.node.line ? ' at line ' + part.ent.node.line : '') + ' in the code editor';
			code.onclick = (e) => {
				e.stopPropagation();
				MS.ide.openFile(part.ent.node.doc.path, part.ent.node.line);
			};
			const eye = document.createElement('button');
			eye.className = 'row-btn eye' + (hiddenSelf || hiddenAbove ? ' off' : '');
			eye.append(icon(hiddenSelf || hiddenAbove ? 'eye-off' : 'eye'));
			eye.title = hiddenAbove ? 'Hidden because a part above it is hidden' : (hiddenSelf ? 'Show' : 'Hide') + ' ' + part.name + ' in the view (the unit itself is not changed). Alt+click: show only this part.';
			eye.onclick = (e) => {
				e.stopPropagation();
				toggleEye(part, e.altKey);
			};
			const picture = document.createElement('button');
			picture.className = 'row-btn';
			picture.append(icon('pencil'));
			picture.title = 'Edit this part\'s sprite, or swap it for another';
			picture.disabled = !part.sprite.file;
			picture.hidden = !part.sprite.file;
			picture.onclick = (e) => {
				e.stopPropagation();
				MS.pixel.menu(picture, { part });
			};
			row.append(picture, code, eye);
			row.onclick = () => app.select(part);
			row.oncontextmenu = (e) => {
				e.preventDefault();
				showPartMenu(part, e.clientX, e.clientY);
			};
			row.ondblclick = (e) => {
				// Quick clicks on the eye, the fold arrow or the code button are not a double-click on the row.
				if (!e.target.closest('button')) {
					MS.ide.openFile(part.ent.node.doc.path, part.ent.node.line);
				}
			};
			row.onmouseenter = () => {
				app.view.hover = part;
				app.requestDraw();
			};
			row.onmouseleave = () => {
				app.view.hover = null;
				app.requestDraw();
			};
			tree.append(row);
			if (!folded) {
				for (const child of part.children) {
					addRow(child, depth + 1);
				}
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

	function foldAll(fold) {
		app.collapsed.clear();
		if (fold) {
			for (const p of allParts()) {
				if (p.children.length && p !== app.rig.body) {
					app.collapsed.add(p.id);
				}
			}
		}
		renderTree();
	}

	function hoverTree(part) {
		for (const row of document.querySelectorAll('.tree-row')) {
			row.classList.toggle('hover', row._part === part);
		}
	}

	// ---- inspector ----

	const closedSections = new Set();
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
		// Properties sections have a fixed place: the same section is always in the same column and slot, whatever part is picked.
		const column = parent.classList.contains('props-cols') ? parent.querySelector('.props-col[data-side="' + (/^(Sprite|Attachments|All settings)/.test(title) ? 'right' : 'left') + '"]') : null;
		if (column) {
			// One order for every section, so a narrow panel (one column) reads in the same sequence: Sprite is always second.
			const rank = ['Preset', 'Sprite', 'Used by', 'Placement', 'Unit', 'Aiming', 'Posture', 'Attachments', 'Scripts'].indexOf(title);
			s.style.order = String(rank < 0 ? (title.startsWith('All settings') ? 99 : 6) : rank);
			column.append(s);
		} else {
			parent.append(s);
		}
		return rows;
	}

	function describeSource(part, key) {
		const node = part.ent.sourceOf(key);
		if (!node) {
			return { text: 'not set: the game uses its default', cls: '' };
		}
		if (node.isWithin(part.anchor)) {
			return { text: 'set here · ' + node.doc.path.split('/').pop() + ':' + (node.line || 'new'), cls: 'own', node };
		}
		const top = node.topLevel;
		const name = top.child('PresetName') ? top.child('PresetName').value : node.doc.path;
		return { text: 'from ' + name + ' · ' + node.doc.module + (node.line ? ':' + node.line : ''), cls: '', node };
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
		fillSource(note, src);
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
		fillSource(note, src);
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
		fillSource(note, src);
		row.append(l, val, note);
		rows.append(row);
	}

	// link: {path, line} makes the text open that place in the code editor.
	function textRow(rows, label, text, title, link) {
		const row = document.createElement('div');
		row.className = 'row';
		const l = document.createElement('label');
		l.textContent = label;
		const val = document.createElement('div');
		val.className = 'val';
		const span = link ? codeLink(text, link.path, link.line, title) : document.createElement('span');
		span.style.cssText = 'overflow:hidden;text-overflow:ellipsis;white-space:nowrap;' + (link ? '' : 'color:var(--text)');
		span.textContent = text;
		span.title = title || (link ? span.title : text);
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
		strip.oncontextmenu = (e) => {
			e.preventDefault();
			MS.pixel.menu(strip, { part });
		};
		rows.append(strip);
		if (forced !== undefined) {
			const note = document.createElement('div');
			note.className = 'src';
			note.textContent = 'Showing frame ' + forced + ' (view only). Click it again to go back to the game\'s choice.';
			rows.append(note);
		}
	}

	// The Used by section: every place that copies this preset, each a link to those lines.
	function usedBySection(insp, preset) {
		const uses = MS.nav.uses(preset);
		const count = document.createElement('span');
		count.className = 'side-count';
		count.textContent = uses.length ? String(uses.length) : 'none';
		const rows = section(insp, 'Used by', count);
		if (!uses.length) {
			const none = document.createElement('div');
			none.className = 'src';
			none.textContent = 'Nothing in the mods or the game copies ' + preset.name + '.';
			rows.append(none);
		}
		const list = document.createElement('div');
		rows.append(list);
		const addUse = (label, detail, path, line) => {
			const row = document.createElement('div');
			row.className = 'row use-row';
			const link = codeLink(label, path, line);
			const small = document.createElement('small');
			small.textContent = detail;
			row.append(link, small);
			list.append(row);
		};
		const shown = 12;
		uses.slice(0, shown).forEach((node) => {
			const d = MS.nav.describeUse(node);
			addUse(d.label, d.detail, d.path, d.line);
		});
		if (uses.length > shown) {
			const more = document.createElement('button');
			more.className = 'small-btn';
			more.textContent = 'Show all ' + uses.length;
			more.onclick = () => {
				uses.slice(shown).forEach((node) => {
					const d = MS.nav.describeUse(node);
					addUse(d.label, d.detail, d.path, d.line);
				});
				more.remove();
			};
			rows.append(more);
		}
		const scripts = document.createElement('button');
		scripts.className = 'small-btn';
		scripts.textContent = 'Scripts that name it';
		scripts.title = 'Look through the Lua scripts for "' + preset.name + '" in quotes, like CreateActor("' + preset.name + '")';
		scripts.onclick = async () => {
			scripts.textContent = 'Reading scripts…';
			await MS.ide.ensureScripts();
			const found = MS.nav.luaMentions(preset.name, 60);
			scripts.remove();
			if (!found.length) {
				const none = document.createElement('div');
				none.className = 'src';
				none.textContent = 'No script names ' + preset.name + ' in quotes.';
				rows.append(none);
			}
			found.forEach((f) => addUse(f.path.split('/').pop() + ':' + f.line, f.path.split('/')[0], f.path, f.line));
		};
		rows.append(scripts);
	}

	function renderInspector() {
		// Sections go in an inner box, which wide screens lay out in columns (the scrolling box itself can't be: its columns would run off sideways).
		const box = $('inspector');
		box.innerHTML = '';
		const insp = document.createElement('div');
		insp.className = 'props-cols';
		box.append(insp);
		for (const side of ['left', 'right']) {
			const column = document.createElement('div');
			column.className = 'props-col';
			column.dataset.side = side;
			insp.append(column);
		}
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
			const heldPreset = !actorLocked && part.anchor !== app.actorNode ? app.lib.presets.find((p) => p.node === part.anchor) : null;
			if (heldPreset) {
				// A held item (or a part of one) from the game or someone else's mod: copy it into your mod, and the unit uses the copy.
				const b = document.createElement('button');
				b.className = 'small-btn primary';
				b.textContent = 'Make my own copy of ' + heldPreset.name;
				b.title = 'Makes ' + heldPreset.name + ' in your mod (CopyOf the original), and ' + app.actor.name + ' carries your copy instead';
				b.onclick = () => MS.dialogs.copyUnit({ name: heldPreset.name, cls: heldPreset.cls, path: heldPreset.doc.path }, (copy) => app.useOwnCopy(heldPreset, copy));
				n.append(document.createElement('br'), b);
			}
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
		textRow(where, 'Written in', ent.node.doc.path + (ent.node.line ? ':' + ent.node.line : ''), null, { path: ent.node.doc.path, line: ent.node.line });
		const copyOf = ent.node.child('CopyOf');
		const base = copyOf ? app.lib.find(copyOf.value, ent.cls, ent.node.doc.module) : null;
		if (base) {
			textRow(where, 'Based on', base.name + ' · ' + base.module + (base.node.line ? ':' + base.node.line : ''), 'Open ' + base.name + ' in the code editor', { path: base.doc.path, line: base.node.line });
		}
		const ownPreset = MS.nav ? MS.nav.presetOf(ent.node) : null;
		if (ownPreset) {
			const relRow = document.createElement('div');
			relRow.className = 'row';
			const relLabel = document.createElement('label');
			relLabel.textContent = 'Relations';
			const relVal = document.createElement('div');
			relVal.className = 'val';
			for (const [tab, text, title] of [['inheritance', 'Inheritance…', 'What it copies and what copies it'], ['graph', 'Graph…', 'A picture of the presets and scripts around it'], ['resolved', 'Resolved…', 'The lines the game ends up with, and where each comes from']]) {
				const b = document.createElement('button');
				b.className = 'small-btn';
				b.textContent = text;
				b.title = title;
				b.onclick = () => MS.relations.open(ownPreset, tab);
				relVal.append(b);
			}
			relRow.append(relLabel, relVal);
			where.append(relRow);
			closedSections.add('Used by'); // starts folded; its count is in the heading
			usedBySection(insp, ownPreset);
		}
		const missing = app.lib.missingCopyOf(ent.node);
		if (missing) {
			const n = document.createElement('div');
			n.className = 'notice bad';
			n.textContent = 'CopyOf "' + missing + '" isn\'t a preset Studio can find. The game will stop loading on this.';
			where.append(n);
		}

		if (part.pieces) {
			// A bunker assembly: the pieces it places. Click a row (or the piece in the view) to pick it; drag it in the view or type where it goes.
			const pieces = part.pieces;
			const box = section(insp, 'Pieces (' + pieces.length + ')', (() => {
				const add = document.createElement('button');
				add.className = 'small-btn';
				add.textContent = '+ Add piece…';
				add.title = 'Search the building pieces (terrain objects) and add one';
				add.onclick = () => {
					const all = app.lib.presets.filter((p) => p.topLevel && p.cls === 'TerrainObject');
					const modules = [...new Set(all.map((p) => p.module))].sort((x, y) => x.localeCompare(y));
					MS.dialogs.open('Add a piece to ' + part.name, MS.dialogs.parts.pickerGrid(all, (p) => app.addPiece(p), (c, p) => drawSpriteThumb(c, p), false, [
						{ label: 'Where it comes from', options: [['', 'Everything'], ...modules.map((m) => [m, m.replace(/\.rte$/i, '')])], test: (p, v) => p.module === v },
					]), [{ text: 'Cancel' }], true);
				};
				return add;
			})());
			for (const piece of pieces) {
				const row = document.createElement('div');
				row.className = 'piece-row' + (piece.index === app.selectedPiece ? ' sel' : '');
				const kind = document.createElement('small');
				kind.textContent = piece.cls === 'TerrainObject' ? '' : piece.cls;
				const name = document.createElement('span');
				name.className = 'piece-name';
				name.textContent = piece.name;
				name.title = piece.ref + (piece.team !== -2 ? ' · team ' + piece.team : '') + (piece.flipped ? ' · flipped' : '');
				const coord = (value, which) => {
					const input = document.createElement('input');
					input.type = 'number';
					input.step = 12;
					input.value = value;
					input.className = 'ed-input num';
					input.onclick = (e) => e.stopPropagation();
					input.onchange = () => app.movePiece(piece.index, which === 'x' ? Number(input.value) : piece.pos.x, which === 'y' ? Number(input.value) : piece.pos.y);
					return input;
				};
				const del = document.createElement('button');
				del.className = 'ico-btn';
				del.textContent = '×';
				del.title = 'Take it out of the assembly';
				del.onclick = (e) => {
					e.stopPropagation();
					app.removePiece(piece.index);
				};
				row.append(kind, name, coord(piece.pos.x, 'x'), coord(piece.pos.y, 'y'), del);
				row.onclick = () => app.selectPiece(piece.index);
				box.append(row);
			}
		}
		if (part.layers) {
			// A building piece: three pictures. Foreground and background can be drawn on or swapped; the material map can be swapped.
			const layers = section(insp, 'Pictures');
			for (const layer of part.layers) {
				const row = document.createElement('div');
				row.className = 'layer-row';
				const name = document.createElement('b');
				name.textContent = layer.label;
				const file = document.createElement('span');
				file.className = 'src';
				file.textContent = layer.file ? layer.file.split('/').pop() : '(none)';
				file.title = layer.file || '';
				const edit = document.createElement('button');
				edit.className = 'small-btn';
				edit.textContent = 'Edit…';
				edit.disabled = !layer.file || layer.key === 'MaterialFile';
				edit.title = layer.key === 'MaterialFile' ? 'The material map\'s pixels are material numbers: swap it for another picture for now' : 'Draw on this picture';
				edit.onclick = () => MS.pixel.open({ part, key: layer.key });
				const swap = document.createElement('button');
				swap.className = 'small-btn';
				swap.textContent = 'Swap…';
				swap.disabled = !!locked;
				swap.onclick = () => MS.dialogs.importSprite(part, { key: layer.key });
				row.append(name, file, edit, ' ', swap);
				layers.append(row);
			}
		}
		// (A building piece has no single sprite: the rows below go nowhere.)
		const sprite = part.layers || part.pieces ? document.createElement('div') : section(insp, 'Sprite', (() => {
			const wrap = document.createElement('span');
			const edit = document.createElement('button');
			edit.className = 'small-btn';
			edit.textContent = 'Edit…';
			edit.title = 'Draw on this picture in the pixel editor';
			edit.disabled = !part.sprite.file;
			edit.onclick = () => MS.pixel.open({ part });
			const b = document.createElement('button');
			b.className = 'small-btn';
			b.textContent = 'Swap…';
			b.title = 'Use a different picture or sprite sheet for this part';
			b.disabled = !!locked;
			b.onclick = () => MS.dialogs.importSprite(part);
			const resize = document.createElement('button');
			resize.className = 'small-btn';
			resize.textContent = 'Resize…';
			resize.title = 'Make this picture bigger or smaller on the grid, in the scale of the whole unit';
			resize.disabled = !part.sprite.file;
			resize.onclick = () => MS.resize.open(part);
			wrap.append(edit, ' ', resize, ' ', b);
			return wrap;
		})());
		const s = part.sprite;
		const spriteFirst = s.file ? MS.Images.framePaths(s.file, s.frameCount)[0] : null;
		textRow(sprite, 'File', s.file || '(none)', null, spriteFirst ? { path: spriteFirst, line: 0 } : null);
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
		if (part.role === 'flash' && part.parent) {
			// The game ignores a flash's own ParentOffset and puts it at the gun's muzzle, so this is the gun's Muzzle setting.
			vectorRow(place, part.parent, 'MuzzleOffset', 'At the muzzle', locked);
			const hint = document.createElement('div');
			hint.className = 'src';
			hint.style.gridColumn = 'auto';
			hint.textContent = 'The game puts the flash on the gun\'s muzzle (the gun\'s Muzzle setting), whatever ParentOffset the flash has.';
			place.append(hint);
		} else if (part.role !== 'body' && part.role !== 'foot' && part.role !== 'device') {
			vectorRow(place, part, 'ParentOffset', 'On parent at', locked);
		}
		if (part.role !== 'body') {
			vectorRow(place, part, 'JointOffset', 'Pivot', locked);
		}
		if (!['body', 'fgarm', 'bgarm', 'bgleg', 'device'].includes(part.role)) {
			boolRow(place, part, 'DrawAfterParent', 'In front of parent', true, locked);
		}
		if (!MS.Rig.POSED_ROLES.has(part.role)) {
			boolRow(place, part, 'InheritsRotAngle', 'Turns with parent', true, locked);
			if (part.ent.bool('InheritsRotAngle', true)) {
				const degrees = Math.round((MS.Rig.rotationOffset(part.ent) * 180) / Math.PI * 100) / 100;
				numberRow(place, part, 'InheritedRotAngleDegOffset', 'Rotation (°)', degrees, locked);
			}
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
		if (part.role === 'body' && app.rig.isDevice) {
			const item = section(insp, 'Item');
			numberRow(item, part, 'Mass', 'Mass (kg)', 0, locked);
			numberRow(item, part, 'GoldValue', 'Cost', 0, locked);
			const dev = section(insp, 'Holding');
			vectorRow(dev, part, 'JointOffset', 'Grip (Joint)', locked);
			vectorRow(dev, part, 'StanceOffset', 'Held at (Stance)', locked);
			vectorRow(dev, part, 'SharpStanceOffset', 'Aiming (Sharp)', locked);
			vectorRow(dev, part, 'SupportOffset', 'Back hand at', locked);
			boolRow(dev, part, 'OneHanded', 'One-handed', false, locked);
			boolRow(dev, part, 'Supportable', 'Back hand helps', true, locked);
			if (ent.cls === 'HDFirearm') {
				const gun = section(insp, 'Firing');
				vectorRow(gun, part, 'MuzzleOffset', 'Muzzle', locked);
				vectorRow(gun, part, 'EjectionOffset', 'Shell ejection', locked);
				numberRow(gun, part, 'RateOfFire', 'Rounds a minute', 0, locked);
				boolRow(gun, part, 'FullAuto', 'Full auto', false, locked);
				numberRow(gun, part, 'ReloadTime', 'Reload (ms)', 0, locked);
				numberRow(gun, part, 'ShakeRange', 'Spread (hip)', 0, locked);
				numberRow(gun, part, 'SharpShakeRange', 'Spread (aiming)', 0, locked);
				numberRow(gun, part, 'RecoilTransmission', 'Recoil passed on', 1, locked);
			}
		} else if (part.role === 'body' && /^AC(DropShip|Rocket)$/.test(ent.cls)) {
			const craft = section(insp, 'Craft');
			numberRow(craft, part, 'Mass', 'Mass (kg)', 0, locked);
			numberRow(craft, part, 'Health', 'Health', 100, locked);
			numberRow(craft, part, 'GoldValue', 'Cost', 0, locked);
			numberRow(craft, part, 'MaxPassengers', 'Passengers', -1, locked);
			numberRow(craft, part, 'HatchDelay', 'Hatch delay (ms)', 0, locked);
			if (ent.cls === 'ACDropShip') {
				numberRow(craft, part, 'MaxEngineAngle', 'Engine tilt (°)', 20, locked);
				numberRow(craft, part, 'LateralControlSpeed', 'Sideways speed', 6, locked);
				numberRow(craft, part, 'HoverHeightModifier', 'Hover height', 0, locked);
			} else {
				numberRow(craft, part, 'MaxGimbalAngle', 'Engine gimbal (°)', 0, locked);
			}
		} else if (part.role === 'body') {
			const stats = section(insp, 'Unit');
			numberRow(stats, part, 'Mass', 'Mass (kg)', 0, locked);
			numberRow(stats, part, 'Health', 'Health', 100, locked);
			numberRow(stats, part, 'GoldValue', 'Cost', 0, locked);
			numberRow(stats, part, 'ImpulseDamageThreshold', 'Impact damage at', 0, locked);
		}
		if (part.role === 'body' && ent.cls === 'ACrab') {
			const aim = section(insp, 'Aiming');
			numberRow(aim, part, 'AimRangeUpperLimit', 'Aim up to', 0, locked);
			numberRow(aim, part, 'AimRangeLowerLimit', 'Aim down to', 0, locked);
		}
		if (part.role === 'body' && ent.cls === 'AHuman') {
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

		MS.ide.scriptsSection(insp, part, locked, section);
		allSettings(insp, part, ent, locked);
	}

	// Every setting the part has, editable: plain values as text, vectors as X and Y. Changing one writes it into this unit's own block
	// (an inherited value is copied in first, so nothing else changes); × takes the unit's own value away again, back to what it inherits.
	// Sub-objects and list entries (AddAttachable and the like) show where they come from but are changed elsewhere. "Add a setting" offers
	// every key that presets of the same class use.
	const keysOfClass = new Map();
	function knownKeys(cls) {
		if (!keysOfClass.has(cls)) {
			const keys = new Set();
			for (const preset of app.lib.presets) {
				if (preset.cls === cls) {
					for (const child of preset.node.children) {
						if (child.key && child.key !== 'CopyOf' && child.key !== 'PresetName') {
							keys.add(child.key);
						}
					}
				}
			}
			keysOfClass.set(cls, [...keys].sort());
		}
		return keysOfClass.get(cls);
	}

	function allSettings(insp, part, ent, locked) {
		const filter = document.createElement('input');
		filter.type = 'search';
		filter.className = 'text settings-filter';
		filter.placeholder = 'Filter settings…';
		const all = section(insp, 'All settings (' + ent.cls + ')');
		all.append(filter);
		const list = document.createElement('div');
		all.append(list);

		const lastOf = new Map();
		ent.entries.forEach((e, i) => lastOf.set(e.key, i));
		const shown = new Set();
		ent.entries.forEach((e, i) => {
			// (List entries, and the name and base, which the Preset section and Make my own copy look after, aren't edited here.)
			const isList = MS.LIST_KEYS.has(e.key) || e.key === 'PresetName' || e.key === 'CopyOf';
			if (lastOf.get(e.key) !== i && !isList) {
				return;
			}
			shown.add(e.key);
			const row = document.createElement('div');
			row.className = 'row setting';
			row.dataset.key = e.key.toLowerCase();
			const l = document.createElement('label');
			l.textContent = e.key;
			l.className = 'code-link';
			l.title = e.key + ': open ' + e.node.doc.path + (e.node.line ? ' at line ' + e.node.line : '') + ' in the code editor';
			l.onclick = () => MS.ide.openFile(e.node.doc.path, e.node.line);
			const val = document.createElement('div');
			val.className = 'val';
			const own = e.node.isWithin(part.anchor);
			const node = e.node;
			if (node.value === 'Vector' && !isList) {
				const v = MS.readVector(node);
				const x = numberBox('X', v.x, (n) => app.setVector(part, e.key, n, MS.readVector(node).y), null, !!locked);
				const y = numberBox('Y', v.y, (n) => app.setVector(part, e.key, MS.readVector(node).x, n), null, !!locked);
				val.append(x.box, y.box);
			} else if (!node.children.length && !isList) {
				const input = document.createElement('input');
				input.className = 'text';
				input.value = node.value;
				input.disabled = !!locked;
				input.onchange = () => app.setValue(part, e.key, input.value.trim());
				input.onkeydown = (k) => {
					if (k.key === 'Enter') {
						input.blur();
					}
				};
				val.append(input);
				if (FILE_LINK.test(node.value.trim())) {
					const open = document.createElement('button');
					open.className = 'small-btn';
					open.textContent = 'Open';
					open.title = 'Open this file in the editor';
					open.onclick = () => MS.ide.openFile(input.value.trim().replace(/\\/g, '/'));
					val.append(open);
				}
			} else {
				const text = document.createElement('span');
				text.className = 'setting-ro';
				text.textContent = node.child('FilePath') ? node.child('FilePath').value : node.child('CopyOf') ? node.value + ' (' + node.child('CopyOf').value + ')' : node.child('PresetName') ? node.value + ' (' + node.child('PresetName').value + ')' : node.value;
				text.title = isList ? 'A list entry: change it in its own part or section.' : 'A whole object: change it in its own section, or in the file.';
				text.classList.add('file-link');
				if (FILE_LINK.test(text.textContent.trim())) {
					// A file: clicking it opens it.
					const file = text.textContent.trim().replace(/\\/g, '/');
					text.title = 'Open ' + file;
					text.onclick = () => MS.ide.openFile(file);
				} else if (node.doc) {
					// An object or list entry: clicking it opens the lines it is written on.
					text.title += ' Click to open it in the code editor.';
					text.onclick = () => MS.ide.openFile(node.doc.path, node.line);
				}
				val.append(text);
			}
			if (own && !isList && !locked && ent.entries.some((other) => other.key === e.key && other.node !== node)) {
				// Set here over an inherited value: × goes back to that.
				const back = document.createElement('button');
				back.className = 'small-btn';
				back.textContent = '×';
				back.title = 'Use the inherited value again';
				back.onclick = () => app.edit('reset ' + e.key, () => node.remove());
				val.append(back);
			}
			const note = document.createElement('div');
			const src = describeSource(part, e.key);
			fillSource(note, own ? src : { cls: '', node, text: 'from ' + (node.topLevel.child('PresetName') ? node.topLevel.child('PresetName').value : node.doc.path) + ' · ' + node.doc.module + (node.line ? ':' + node.line : '') });
			row.append(l, val, note);
			list.append(row);
		});

		// Adding one the part doesn't set (the game's default applies until then).
		if (!locked) {
			const add = document.createElement('div');
			add.className = 'row setting-add';
			const keyInput = document.createElement('input');
			keyInput.className = 'text';
			keyInput.placeholder = 'Add a setting…';
			const listId = 'keys-' + ent.cls;
			keyInput.setAttribute('list', listId);
			const datalist = document.createElement('datalist');
			datalist.id = listId;
			for (const key of knownKeys(ent.cls)) {
				if (!shown.has(key)) {
					const o = document.createElement('option');
					o.value = key;
					datalist.append(o);
				}
			}
			const val = document.createElement('div');
			val.className = 'val';
			const valueInput = document.createElement('input');
			valueInput.className = 'text';
			valueInput.placeholder = 'value';
			const btn = document.createElement('button');
			btn.className = 'small-btn';
			btn.textContent = 'Add';
			btn.onclick = () => {
				const key = keyInput.value.trim();
				if (!/^[A-Za-z][\w]*$/.test(key)) {
					app.toast('A setting name is letters and numbers, like Mass or GibImpulseLimit.', 'bad');
					return;
				}
				app.setValue(part, key, valueInput.value.trim(), 'add ' + key);
			};
			val.append(valueInput, btn);
			add.append(keyInput, val, datalist);
			all.append(add);
		}

		filter.oninput = () => {
			const q = filter.value.trim().toLowerCase();
			for (const row of list.children) {
				row.hidden = !!q && !row.dataset.key.includes(q);
			}
		};
	}

	function canRemove(part) {
		return part.role === 'attachable' && !app.lib.canEdit(part.anchor, part.path);
	}

	// Removes the attachment from this unit only: an inherited one is first made the unit's own (Library.adopt).
	function removePart(part) {
		app.edit('remove ' + part.name, () => {
			const node = app.ownBlock(part);
			if (node) {
				node.remove();
			}
		});
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
		if (MS.friendly) {
			MS.friendly.update();
		}
		renderTree();
		renderInspector();
		refreshDeviceSelect();
		markLibrary();
		app.updateFileStatus();
		if (MS.ide && MS.ide.refreshForm) {
			MS.ide.refreshForm(); // an open data editor shows the change (an edit, an undo)
		}
		if (MS.editors && !$('view-data').hidden) {
			MS.editors.renderSide(); // the Data view's counts
		}
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
		$('btnShowParts').onclick = () => {
			app.hidden.clear();
			renderTree();
			app.requestDraw();
		};
		$('btnFoldParts').onclick = () => {
			foldAll(!app.collapsed.size);
		};
		for (const b of document.querySelectorAll('#libTabs button')) {
			b.onclick = () => {
				tab = b.dataset.tab;
				$('libType').value = '';
				$('libGroup').value = '';
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
		for (const id of ['libType', 'libGroup', 'libModule']) {
			$(id).onchange = () => app.lib && showLibrary();
		}
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
		drawRigThumb,
		queueThumb,
		forgetThumbs: () => thumbRigs.clear(),
		filesOpen: () => openFiles !== null,
		TAB_CLASSES,
		showPartMenu,
	};
})();
