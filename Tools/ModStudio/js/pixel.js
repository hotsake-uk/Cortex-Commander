// Cortex Mod Studio: the pixel editor. A basic one for the game's pictures: pencil, eraser, fill, colour picker, line and rectangle on the game's own
// palette (so what you draw is exactly what the game will show), several frames with a playing preview, onion skin, mirror drawing, flip, resize,
// and undo. It opens from anywhere a sprite is shown (Edit sprite, with Swap sprite beside it): a part's picture, a picture in a unit's files, a
// picture open in Script mode, or the guide. A picture in your own mod is saved in place; anything else is saved as a copy in your mod, and the
// part it came from is pointed at the copy.
'use strict';
window.MS = window.MS || {};

(function () {
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

	const UNDO_LIMIT = 80;
	const CELL = 14; // a palette cell, in pixels
	const ICONS = {
		pencil: '<path d="M2 14l1-4 8-8 3 3-8 8z"/><path d="M9.5 3.5l3 3"/>',
		eraser: '<path d="M3 10l5-6 5 4-4 5H5z"/><path d="M7 14h7"/>',
		fill: '<path d="M3 9l5-5 5 5-5 5z"/><path d="M13 11c1 1.4 1 2.6 0 3.4"/>',
		pick: '<path d="M10 2l4 4-2 2-4-4z"/><path d="M8 6l-5 5v3h3l5-5"/>',
		line: '<path d="M3 13L13 3"/>',
		rect: '<path d="M3 4h10v8H3z"/>',
	};
	const TOOLS = [
		['pencil', 'Pencil (B)', 'b'],
		['eraser', 'Eraser (E): paints see-through', 'e'],
		['fill', 'Fill (G)', 'g'],
		['pick', 'Pick a colour (I); Alt+click does it with any tool', 'i'],
		['line', 'Line (L)', 'l'],
		['rect', 'Rectangle (R): hold Shift for a solid one', 'r'],
	];

	let E = null; // the editor that is open

	const icon = (name) => el('span', { className: 'ico-svg', innerHTML: '<svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">' + ICONS[name] + '</svg>' });
	const blank = (w, h) => ({ w, h, indices: new Uint8Array(w * h) });
	const copyFrame = (f) => ({ w: f.w, h: f.h, indices: Uint8Array.from(f.indices) });
	const frame = () => E.frames[E.cur];
	const safe = (s) => s.replace(/[^A-Za-z0-9 _-]+/g, '').trim().replace(/\s+/g, '') || 'Picture';

	// ---- a sprite as editable palette numbers ----

	async function loadFrames(file, frameCount) {
		const a = app();
		const toFrame = (loaded) => {
			if (!loaded) {
				return null;
			}
			let indices = loaded.indices ? Uint8Array.from(loaded.indices) : null;
			if (!indices) {
				// A true-colour picture: moved to the nearest game colours.
				indices = a.images.toPalette(loaded.canvas.getContext('2d').getImageData(0, 0, loaded.width, loaded.height)).indices;
			}
			return { w: loaded.width, h: loaded.height, indices };
		};
		let frames = [];
		for (const path of MS.Images.framePaths(file, frameCount)) {
			const f = toFrame(await a.images.get(path));
			if (!f) {
				frames = null;
				break;
			}
			frames.push(f);
		}
		if (frames || frameCount > 1) {
			return frames;
		}
		// One name that is really a series (Name000.png, Name001.png, ...).
		frames = [];
		for (const path of MS.Images.framePaths(file, 64)) {
			const f = toFrame(await a.images.get(path));
			if (!f) {
				break;
			}
			frames.push(f);
		}
		return frames.length ? frames : null;
	}

	// ---- undo ----

	const snapshot = () => ({ cur: E.cur, frames: E.frames.map(copyFrame) });

	function restore(s) {
		E.frames = s.frames.map(copyFrame);
		E.cur = Math.min(s.cur, E.frames.length - 1);
		renderAll();
	}

	function begin() {
		E.pending = snapshot();
	}

	function commit() {
		if (!E.pending) {
			return;
		}
		E.undo.push(E.pending);
		if (E.undo.length > UNDO_LIMIT) {
			E.undo.shift();
		}
		E.redo = [];
		E.pending = null;
		E.dirty = true;
		E.warned = false;
		renderFrames();
		updateButtons();
	}

	function undo() {
		if (!E.undo.length) {
			return;
		}
		E.redo.push(snapshot());
		restore(E.undo.pop());
		E.dirty = true;
	}

	function redo() {
		if (!E.redo.length) {
			return;
		}
		E.undo.push(snapshot());
		restore(E.redo.pop());
		E.dirty = true;
	}

	// ---- drawing ----

	function linePoints(x0, y0, x1, y1) {
		const points = [];
		const dx = Math.abs(x1 - x0);
		const dy = -Math.abs(y1 - y0);
		const sx = x0 < x1 ? 1 : -1;
		const sy = y0 < y1 ? 1 : -1;
		let err = dx + dy;
		for (let guard = 0; guard < 4096; guard++) {
			points.push([x0, y0]);
			if (x0 === x1 && y0 === y1) {
				break;
			}
			const e2 = 2 * err;
			if (e2 >= dy) {
				err += dy;
				x0 += sx;
			}
			if (e2 <= dx) {
				err += dx;
				y0 += sy;
			}
		}
		return points;
	}

	function rectPoints(x0, y0, x1, y1, solid) {
		const points = [];
		const [lx, hx] = x0 < x1 ? [x0, x1] : [x1, x0];
		const [ly, hy] = y0 < y1 ? [y0, y1] : [y1, y0];
		for (let y = ly; y <= hy; y++) {
			for (let x = lx; x <= hx; x++) {
				if (solid || x === lx || x === hx || y === ly || y === hy) {
					points.push([x, y]);
				}
			}
		}
		return points;
	}

	// Paints points with a palette number, and their mirror image when mirror drawing is on.
	function plot(points, index) {
		const f = frame();
		for (const [x, y] of points) {
			if (x >= 0 && y >= 0 && x < f.w && y < f.h) {
				f.indices[y * f.w + x] = index;
			}
			if (E.mirror) {
				const mx = f.w - 1 - x;
				if (mx >= 0 && y >= 0 && mx < f.w && y < f.h) {
					f.indices[y * f.w + mx] = index;
				}
			}
		}
	}

	function floodFill(x, y, index) {
		const f = frame();
		if (x < 0 || y < 0 || x >= f.w || y >= f.h) {
			return;
		}
		const target = f.indices[y * f.w + x];
		if (target === index) {
			return;
		}
		const stack = [[x, y]];
		while (stack.length) {
			const [px, py] = stack.pop();
			if (px < 0 || py < 0 || px >= f.w || py >= f.h || f.indices[py * f.w + px] !== target) {
				continue;
			}
			f.indices[py * f.w + px] = index;
			stack.push([px + 1, py], [px - 1, py], [px, py + 1], [px, py - 1]);
		}
	}

	function setColor(index) {
		E.color = index;
		if (index > 0) {
			E.recent = [index, ...E.recent.filter((c) => c !== index)].slice(0, 12);
		}
		renderPalette();
	}

	const palette = (i) => {
		const p = app().images.palette;
		return [p[i * 3], p[i * 3 + 1], p[i * 3 + 2]];
	};

	// ---- the picture ----

	function checker(ctx, w, h) {
		const size = 8;
		for (let y = 0; y < h; y += size) {
			for (let x = 0; x < w; x += size) {
				ctx.fillStyle = ((x + y) / size) % 2 ? '#262b36' : '#1d212a';
				ctx.fillRect(x, y, Math.min(size, w - x), Math.min(size, h - y));
			}
		}
	}

	function render() {
		const a = app();
		const f = frame();
		const z = E.zoom;
		const cv = E.cv;
		const W = f.w * z;
		const H = f.h * z;
		if (cv.width !== W || cv.height !== H) {
			cv.width = W;
			cv.height = H;
		}
		const ctx = cv.getContext('2d');
		ctx.imageSmoothingEnabled = false;
		checker(ctx, W, H);
		if (E.onion && E.cur > 0) {
			const before = E.frames[E.cur - 1];
			if (before.w === f.w && before.h === f.h) {
				ctx.globalAlpha = 0.35;
				ctx.drawImage(a.images.fromIndices(before.w, before.h, before.indices).canvas, 0, 0, W, H);
				ctx.globalAlpha = 1;
			}
		}
		ctx.drawImage(a.images.fromIndices(f.w, f.h, f.indices).canvas, 0, 0, W, H);
		if (E.preview) {
			const [r, g, b] = palette(E.preview.index);
			ctx.fillStyle = E.preview.index ? 'rgb(' + r + ',' + g + ',' + b + ')' : 'rgba(239,106,91,.55)';
			for (const [x, y] of E.preview.points) {
				ctx.fillRect(x * z, y * z, z, z);
				if (E.mirror) {
					ctx.fillRect((f.w - 1 - x) * z, y * z, z, z);
				}
			}
		}
		if (E.grid && z >= 6) {
			ctx.strokeStyle = 'rgba(255,255,255,.1)';
			ctx.lineWidth = 1;
			ctx.beginPath();
			for (let x = 0; x <= f.w; x++) {
				ctx.moveTo(x * z + 0.5, 0);
				ctx.lineTo(x * z + 0.5, H);
			}
			for (let y = 0; y <= f.h; y++) {
				ctx.moveTo(0, y * z + 0.5);
				ctx.lineTo(W, y * z + 0.5);
			}
			ctx.stroke();
		}
		if (E.mirror) {
			ctx.strokeStyle = 'rgba(242,182,61,.6)';
			ctx.setLineDash([4, 4]);
			ctx.beginPath();
			ctx.moveTo(W / 2, 0);
			ctx.lineTo(W / 2, H);
			ctx.stroke();
			ctx.setLineDash([]);
		}
		if (E.hover && E.hover.inside) {
			ctx.strokeStyle = '#f2b63d';
			ctx.lineWidth = 1;
			ctx.strokeRect(E.hover.x * z + 0.5, E.hover.y * z + 0.5, z - 1, z - 1);
		}
	}

	function renderPalette() {
		const cv = E.pal;
		const ctx = cv.getContext('2d');
		ctx.clearRect(0, 0, cv.width, cv.height);
		for (let i = 0; i < 256; i++) {
			const x = (i % 16) * CELL;
			const y = Math.floor(i / 16) * CELL;
			if (i === 0) {
				ctx.fillStyle = '#262b36';
				ctx.fillRect(x, y, CELL, CELL);
				ctx.strokeStyle = '#ef6a5b';
				ctx.beginPath();
				ctx.moveTo(x + 2, y + CELL - 2);
				ctx.lineTo(x + CELL - 2, y + 2);
				ctx.stroke();
			} else {
				const [r, g, b] = palette(i);
				ctx.fillStyle = 'rgb(' + r + ',' + g + ',' + b + ')';
				ctx.fillRect(x, y, CELL, CELL);
			}
		}
		const x = (E.color % 16) * CELL;
		const y = Math.floor(E.color / 16) * CELL;
		ctx.strokeStyle = '#fff';
		ctx.lineWidth = 2;
		ctx.strokeRect(x + 1, y + 1, CELL - 2, CELL - 2);
		ctx.strokeStyle = '#000';
		ctx.lineWidth = 1;
		ctx.strokeRect(x + 0.5, y + 0.5, CELL - 1, CELL - 1);
		const [r, g, b] = E.color ? palette(E.color) : [0, 0, 0];
		E.swatch.style.background = E.color ? 'rgb(' + r + ',' + g + ',' + b + ')' : '';
		E.swatch.classList.toggle('clear', !E.color);
		E.swatchText.textContent = E.color ? 'Colour ' + E.color + ' · ' + r + ', ' + g + ', ' + b : 'See-through';
		E.recentBox.replaceChildren(...E.recent.map((c) => {
			const [rr, gg, bb] = palette(c);
			return el('button', { className: 'px-recent', title: 'Colour ' + c, style: 'background:rgb(' + rr + ',' + gg + ',' + bb + ')', onclick: () => setColor(c) });
		}));
	}

	function thumb(f, size) {
		const c = el('canvas', { width: size, height: size });
		const ctx = c.getContext('2d');
		ctx.imageSmoothingEnabled = false;
		checker(ctx, size, size);
		const z = Math.max(1, Math.floor(size / Math.max(f.w, f.h)));
		const source = app().images.fromIndices(f.w, f.h, f.indices).canvas;
		ctx.drawImage(source, Math.floor((size - f.w * z) / 2), Math.floor((size - f.h * z) / 2), f.w * z, f.h * z);
		return c;
	}

	function renderFrames() {
		E.strip.replaceChildren(...E.frames.map((f, i) => {
			const b = el('button', { className: 'px-frame' + (i === E.cur ? ' sel' : ''), title: 'Frame ' + (i + 1) + ' of ' + E.frames.length }, thumb(f, 44), el('small', { textContent: String(i + 1) }));
			b.onclick = () => {
				E.cur = i;
				renderAll();
			};
			return b;
		}));
		E.frameInfo.textContent = 'Frame ' + (E.cur + 1) + ' of ' + E.frames.length + ' · ' + frame().w + '×' + frame().h;
	}

	function renderPreview() {
		const pv = E.previewCv;
		const f = E.frames[E.previewAt % E.frames.length];
		const z = Math.max(1, Math.min(6, Math.floor(96 / Math.max(f.w, f.h))));
		pv.width = f.w * z;
		pv.height = f.h * z;
		const ctx = pv.getContext('2d');
		ctx.imageSmoothingEnabled = false;
		checker(ctx, pv.width, pv.height);
		ctx.drawImage(app().images.fromIndices(f.w, f.h, f.indices).canvas, 0, 0, pv.width, pv.height);
	}

	function updateButtons() {
		E.undoBtn.disabled = !E.undo.length;
		E.redoBtn.disabled = !E.redo.length;
		E.delBtn.disabled = E.frames.length < 2;
		E.leftBtn.disabled = E.cur === 0;
		E.rightBtn.disabled = E.cur === E.frames.length - 1;
		E.sizeW.value = frame().w;
		E.sizeH.value = frame().h;
		for (const [id, button] of Object.entries(E.toolButtons)) {
			button.classList.toggle('sel', id === E.tool);
		}
		E.gridBtn.classList.toggle('on', E.grid);
		E.onionBtn.classList.toggle('on', E.onion);
		E.mirrorBtn.classList.toggle('on', E.mirror);
		E.zoomText.textContent = E.zoom + '×';
	}

	function renderAll() {
		render();
		renderFrames();
		renderPreview();
		updateButtons();
	}

	function fit() {
		const f = frame();
		const box = E.wrap.getBoundingClientRect();
		E.zoom = Math.max(1, Math.min(40, Math.floor(Math.min((box.width - 24) / f.w, (box.height - 24) / f.h)) || 8));
	}

	// ---- the mouse ----

	function pixelAt(e) {
		const r = E.cv.getBoundingClientRect();
		const f = frame();
		const x = Math.floor((e.clientX - r.left) / E.zoom);
		const y = Math.floor((e.clientY - r.top) / E.zoom);
		return { x, y, inside: x >= 0 && y >= 0 && x < f.w && y < f.h };
	}

	function describe(p) {
		if (!p.inside) {
			E.status.textContent = '';
			return;
		}
		const index = frame().indices[p.y * frame().w + p.x];
		E.status.textContent = p.x + ', ' + p.y + ' · ' + (index ? 'colour ' + index : 'see-through');
	}

	function pickAt(p) {
		if (p.inside) {
			setColor(frame().indices[p.y * frame().w + p.x]);
		}
	}

	function onDown(e) {
		if (e.button === 1) {
			return;
		}
		e.preventDefault();
		try {
			E.cv.setPointerCapture(e.pointerId);
		} catch (err) {
			// not a real pointer: nothing to capture
		}
		const p = pixelAt(e);
		if (e.altKey || E.tool === 'pick') {
			pickAt(p);
			return;
		}
		const index = E.tool === 'eraser' || e.button === 2 ? 0 : E.color;
		begin();
		if (E.tool === 'fill') {
			floodFill(p.x, p.y, index);
			commit();
			renderAll();
			return;
		}
		if (E.tool === 'pencil' || E.tool === 'eraser') {
			E.drag = { last: p, index };
			plot([[p.x, p.y]], index);
		} else {
			E.drag = { start: p, index };
			E.preview = { points: [[p.x, p.y]], index };
		}
		render();
	}

	function onMove(e) {
		const p = pixelAt(e);
		E.hover = p;
		describe(p);
		if (E.drag) {
			if (E.drag.last) {
				plot(linePoints(E.drag.last.x, E.drag.last.y, p.x, p.y), E.drag.index);
				E.drag.last = p;
			} else if (E.tool === 'line') {
				E.preview = { points: linePoints(E.drag.start.x, E.drag.start.y, p.x, p.y), index: E.drag.index };
			} else {
				E.preview = { points: rectPoints(E.drag.start.x, E.drag.start.y, p.x, p.y, e.shiftKey), index: E.drag.index };
			}
		}
		render();
	}

	function onUp() {
		if (!E.drag) {
			return;
		}
		if (E.preview) {
			plot(E.preview.points, E.drag.index);
			E.preview = null;
		}
		E.drag = null;
		commit();
		renderAll();
	}

	// ---- frames and the whole picture ----

	function edit(change) {
		begin();
		change();
		commit();
		renderAll();
	}

	function addFrame(duplicate) {
		edit(() => {
			const f = frame();
			E.frames.splice(E.cur + 1, 0, duplicate ? copyFrame(f) : blank(f.w, f.h));
			E.cur++;
		});
	}

	function deleteFrame() {
		if (E.frames.length < 2) {
			return;
		}
		edit(() => {
			E.frames.splice(E.cur, 1);
			E.cur = Math.min(E.cur, E.frames.length - 1);
		});
	}

	function moveFrame(by) {
		const to = E.cur + by;
		if (to < 0 || to >= E.frames.length) {
			return;
		}
		edit(() => {
			const [f] = E.frames.splice(E.cur, 1);
			E.frames.splice(to, 0, f);
			E.cur = to;
		});
	}

	function flip(horizontal) {
		edit(() => {
			const f = frame();
			const out = new Uint8Array(f.indices.length);
			for (let y = 0; y < f.h; y++) {
				for (let x = 0; x < f.w; x++) {
					out[(horizontal ? y : f.h - 1 - y) * f.w + (horizontal ? f.w - 1 - x : x)] = f.indices[y * f.w + x];
				}
			}
			f.indices = out;
		});
	}

	function clearFrame() {
		edit(() => {
			frame().indices.fill(0);
		});
	}

	// Every frame is cropped, or given see-through room, on the right and the bottom.
	function resize(width, height) {
		width = Math.max(1, Math.min(256, Math.floor(width) || 1));
		height = Math.max(1, Math.min(256, Math.floor(height) || 1));
		if (width === frame().w && height === frame().h) {
			return;
		}
		edit(() => {
			E.frames = E.frames.map((f) => {
				const out = blank(width, height);
				for (let y = 0; y < Math.min(height, f.h); y++) {
					for (let x = 0; x < Math.min(width, f.w); x++) {
						out.indices[y * width + x] = f.indices[y * f.w + x];
					}
				}
				return out;
			});
		});
		fit();
		renderAll();
	}

	// ---- saving ----

	function canOverwrite() {
		const a = app();
		if (!E.file) {
			return false;
		}
		const module = E.file.split('/')[0];
		return !!a.unlocked || (!a.fs.isOfficial(module) && a.lib.lockKind({ path: E.file }) === '');
	}

	const outFrames = () => E.frames.map((f) => ({ width: f.w, height: f.h, indices: f.indices }));

	function finished(message) {
		const a = app();
		a.requestDraw();
		MS.panels.refresh();
		a.toast(message, 'good');
		const done = E.onSaved;
		E.saved = true;
		MS.dialogs.close();
		E = null;
		if (done) {
			done();
		}
	}

	// The unit's INI change from pointing a part at the picture is written straight away too.
	async function keepUnit() {
		const a = app();
		const doc = a.actor ? a.lib.docFor(a.actor.path) : null;
		if (doc && doc.dirty) {
			await a.save(true, [doc]);
		}
	}

	async function saveInPlace() {
		const a = app();
		try {
			const countChanged = E.frames.length !== E.frameCount;
			await MS.dialogs.saveSprite(countChanged ? E.part : null, E.file, outFrames(), false);
			await keepUnit();
		} catch (e) {
			a.toast('Couldn\'t save the picture: ' + e.message, 'bad');
			return;
		}
		finished('Saved ' + E.file.split('/').pop() + '.');
	}

	async function saveCopy(mod, name) {
		const a = app();
		const folder = mod + '/Sprites/' + safe(a.actor ? a.actor.name : 'Pictures');
		const path = folder + '/' + safe(name) + '.png';
		if (MS.Images.framePaths(path, E.frames.length).some((p) => a.fs.isOfficial(p))) {
			a.toast('Save it inside your own mod\'s folder.', 'bad');
			return;
		}
		if (path !== E.file && (await a.fs.exists(path))) {
			a.toast('There is already a picture called ' + safe(name) + ' in that mod. Pick another name.', 'bad');
			return;
		}
		const linkable = E.part && !a.lockReason(E.part);
		try {
			await MS.dialogs.saveSprite(linkable ? E.part : null, path, outFrames(), false);
			await keepUnit();
		} catch (e) {
			a.toast('Couldn\'t save the picture: ' + e.message, 'bad');
			return;
		}
		finished('Saved ' + path + (E.part && !linkable ? '. Make your own copy of the unit, then use Swap sprite to put it in.' : '.'));
	}

	function copyForm() {
		const { modPicker, field } = MS.dialogs.parts;
		const mod = modPicker();
		const name = el('input', { type: 'text', value: E.file ? safe(E.file.split('/').pop().replace(/\.[^.]+$/, '').replace(/\d+$/, '')) + 'Edited' : 'MyPicture' });
		const go = el('button', { className: 'tb primary', textContent: 'Save the copy', onclick: () => {
			const modName = mod.value();
			if (!modName || !name.value.trim()) {
				app().toast('Pick a mod and give the picture a name.', 'bad');
				return;
			}
			saveCopy(modName, name.value);
		} });
		E.copyBox.replaceChildren(
			field('Put the copy in', mod.node, 'Saved in its Sprites folder' + (E.part ? ', and ' + E.part.name + ' is switched to it' : '') + '.'),
			field('Picture name', name),
			el('div', { className: 'px-copy-nav' }, el('button', { className: 'tb', textContent: 'Cancel', onclick: () => { E.copyBox.hidden = true; } }), go));
		E.copyBox.hidden = false;
		setTimeout(() => name.select(), 0);
	}

	// ---- the window ----

	function build() {
		const toolButtons = {};
		const toolColumn = el('div', { className: 'px-tools' }, ...TOOLS.map(([id, title]) => {
			const b = el('button', { className: 'px-tool', title, onclick: () => { E.tool = id; updateButtons(); } }, icon(id));
			toolButtons[id] = b;
			return b;
		}));
		E.toolButtons = toolButtons;
		const toggle = (text, title, key) => el('button', { className: 'ico-btn', textContent: text, title, onclick: () => { E[key] = !E[key]; updateButtons(); render(); } });
		E.gridBtn = toggle('Grid', 'Show the pixel grid (when zoomed in)', 'grid');
		E.onionBtn = toggle('Onion', 'Show the frame before, faintly, to line frames up', 'onion');
		E.mirrorBtn = toggle('Mirror', 'Draw on both sides of the middle at once (M)', 'mirror');
		E.undoBtn = el('button', { className: 'ico-btn', textContent: '↶ Undo', title: 'Undo (Ctrl+Z)', onclick: undo });
		E.redoBtn = el('button', { className: 'ico-btn', textContent: '↷ Redo', title: 'Redo (Ctrl+Y)', onclick: redo });
		E.zoomText = el('span', { className: 'px-zoom' });
		const zoomBy = (d) => () => { E.zoom = Math.max(1, Math.min(40, E.zoom + d * (E.zoom >= 8 ? 2 : 1))); renderAll(); };
		const top = el('div', { className: 'px-top' }, E.undoBtn, E.redoBtn, el('span', { className: 'px-sep' }), E.gridBtn, E.onionBtn, E.mirrorBtn, el('span', { className: 'spacer' }),
			el('button', { className: 'ico-btn', textContent: '−', title: 'Zoom out (-)', onclick: zoomBy(-1) }), E.zoomText, el('button', { className: 'ico-btn', textContent: '+', title: 'Zoom in (+)', onclick: zoomBy(1) }),
			el('button', { className: 'ico-btn', textContent: 'Fit', title: 'Fit the picture in the window', onclick: () => { fit(); renderAll(); } }));
		E.cv = el('canvas', { className: 'px-canvas' });
		E.cv.onpointerdown = onDown;
		E.cv.onpointermove = onMove;
		E.cv.onpointerup = onUp;
		E.cv.onpointerleave = () => { E.hover = null; E.status.textContent = ''; render(); };
		E.cv.oncontextmenu = (e) => e.preventDefault();
		E.wrap = el('div', { className: 'px-wrap' }, E.cv);
		E.wrap.addEventListener('wheel', (e) => {
			e.preventDefault();
			E.zoom = Math.max(1, Math.min(40, E.zoom + (e.deltaY < 0 ? 1 : -1) * (E.zoom >= 8 ? 2 : 1)));
			renderAll();
		}, { passive: false });
		E.status = el('span', { className: 'px-status' });
		const center = el('div', { className: 'px-center' }, top, E.wrap, el('div', { className: 'px-bottom' }, E.status));

		// the side: colours, frames, the whole picture
		E.pal = el('canvas', { className: 'px-pal', width: CELL * 16, height: CELL * 16, title: 'The game\'s colours: click one' });
		E.pal.onpointerdown = (e) => {
			const r = E.pal.getBoundingClientRect();
			const x = Math.floor((e.clientX - r.left) / CELL);
			const y = Math.floor((e.clientY - r.top) / CELL);
			if (x >= 0 && x < 16 && y >= 0 && y < 16) {
				setColor(y * 16 + x);
			}
		};
		E.swatch = el('span', { className: 'px-swatch' });
		E.swatchText = el('span', { className: 'px-swatch-text' });
		E.recentBox = el('div', { className: 'px-recents' });
		E.strip = el('div', { className: 'px-strip' });
		E.frameInfo = el('div', { className: 'px-frameinfo' });
		const small = (text, title, run) => el('button', { className: 'ico-btn', textContent: text, title, onclick: run });
		E.leftBtn = small('◀', 'Move the frame earlier', () => moveFrame(-1));
		E.rightBtn = small('▶', 'Move the frame later', () => moveFrame(1));
		E.delBtn = small('Delete', 'Delete this frame', deleteFrame);
		E.previewCv = el('canvas', { className: 'px-preview' });
		E.sizeW = el('input', { type: 'number', min: 1, max: 256, className: 'px-size' });
		E.sizeH = el('input', { type: 'number', min: 1, max: 256, className: 'px-size' });
		const side = el('div', { className: 'px-side' },
			el('div', { className: 'px-h' }, 'Colour'),
			el('div', { className: 'px-colour' }, E.swatch, E.swatchText),
			E.pal, E.recentBox,
			el('div', { className: 'px-h' }, 'Frames'),
			E.strip, E.frameInfo,
			el('div', { className: 'px-row' }, small('+ Blank', 'A new empty frame after this one', () => addFrame(false)), small('Copy', 'A copy of this frame after it', () => addFrame(true)), E.leftBtn, E.rightBtn, E.delBtn),
			el('div', { className: 'px-h' }, 'Playing'),
			E.previewCv,
			el('div', { className: 'px-h' }, 'This frame'),
			el('div', { className: 'px-row' }, small('Flip ⇄', 'Flip left to right', () => flip(true)), small('Flip ⇅', 'Flip top to bottom', () => flip(false)), small('Clear', 'Make it all see-through', clearFrame)),
			el('div', { className: 'px-h' }, 'Size (all frames)'),
			el('div', { className: 'px-row' }, E.sizeW, '×', E.sizeH, small('Apply', 'Crop or add see-through room on the right and bottom', () => resize(Number(E.sizeW.value), Number(E.sizeH.value)))));

		// saving
		E.copyBox = el('div', { className: 'px-copy', hidden: true });
		const own = canOverwrite();
		const saveRow = el('div', { className: 'px-save' },
			el('span', { className: 'px-where', textContent: E.file ? (own ? 'Saves over ' : 'This is ' + (app().fs.isOfficial(E.file.split('/')[0]) ? 'one of the game\'s pictures, so it saves as a copy in your mod: ' : 'in someone else\'s mod, so it saves as a copy in yours: ')) + E.file.split('/').pop() : 'A new picture: it saves in your mod.' }),
			el('span', { className: 'spacer' }),
			own ? el('button', { className: 'tb primary', textContent: 'Save', onclick: saveInPlace }) : null,
			el('button', { className: 'tb' + (own ? '' : ' primary'), textContent: own ? 'Save a copy…' : 'Save a copy in my mod…', onclick: copyForm }));
		return el('div', { className: 'px' }, el('div', { className: 'px-main' }, toolColumn, center, side), E.copyBox, saveRow);
	}

	function onKey(e) {
		if (!E || !E.cv.isConnected) {
			document.removeEventListener('keydown', onKey, true);
			return;
		}
		const tag = (document.activeElement && document.activeElement.tagName) || '';
		if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT') {
			return;
		}
		const key = e.key.toLowerCase();
		if ((e.ctrlKey || e.metaKey) && key === 'z') {
			e.shiftKey ? redo() : undo();
		} else if ((e.ctrlKey || e.metaKey) && key === 'y') {
			redo();
		} else if (e.ctrlKey || e.metaKey || e.altKey) {
			return;
		} else if (TOOLS.some((t) => t[2] === key)) {
			E.tool = TOOLS.find((t) => t[2] === key)[0];
			updateButtons();
		} else if (key === 'm') {
			E.mirror = !E.mirror;
			updateButtons();
			render();
		} else if (key === '+' || key === '=') {
			E.zoom = Math.min(40, E.zoom + (E.zoom >= 8 ? 2 : 1));
			renderAll();
		} else if (key === '-') {
			E.zoom = Math.max(1, E.zoom - (E.zoom > 8 ? 2 : 1));
			renderAll();
		} else if (key === ',' || key === '.') {
			E.cur = Math.max(0, Math.min(E.frames.length - 1, E.cur + (key === '.' ? 1 : -1)));
			renderAll();
		} else {
			return;
		}
		e.preventDefault();
		e.stopPropagation();
	}

	// target: {part} (a part's picture), or {file, frameCount} (a picture file), or {} (a new picture). onSaved runs after it is saved.
	async function open(target) {
		const a = app();
		target = target || {};
		const part = target.part || null;
		const file = part ? part.sprite.file : target.file || null;
		const frameCount = part ? part.sprite.frameCount : target.frameCount || 1;
		let frames = null;
		if (file) {
			frames = await loadFrames(file, frameCount);
			if (!frames) {
				a.toast('Studio can\'t read ' + file + '.', 'bad');
				return;
			}
		} else {
			frames = [blank(16, 16)];
		}
		const counts = new Map();
		for (const i of frames[0].indices) {
			if (i) {
				counts.set(i, (counts.get(i) || 0) + 1);
			}
		}
		const common = [...counts.entries()].sort((x, y) => y[1] - x[1])[0];
		E = { part, file, frameCount: file ? frames.length : 0, frames, cur: 0, tool: 'pencil', color: common ? common[0] : 1, recent: [], mirror: false, grid: true, onion: false, zoom: 8, undo: [], redo: [], pending: null, dirty: false, warned: false, drag: null, preview: null, hover: null, previewAt: 0, saved: false, onSaved: target.onSaved || null };
		const body = build();
		const name = file ? file.split('/').pop() : 'a new picture';
		MS.dialogs.open('Edit sprite · ' + name, body, [{
			text: 'Close',
			action: () => {
				if (E && E.dirty && !E.saved && !E.warned) {
					E.warned = true;
					app().toast('You have changes that aren\'t saved. Press Close again to throw them away.', 'bad');
					return false;
				}
				E = null;
			},
		}], true);
		$modal().classList.add('px-card');
		document.addEventListener('keydown', onKey, true);
		setTimeout(() => {
			if (!E) {
				return;
			}
			fit();
			renderPalette();
			renderAll();
		}, 30);
		const timer = setInterval(() => {
			if (!E || !E.cv.isConnected) {
				clearInterval(timer);
				return;
			}
			if (E.frames.length > 1) {
				E.previewAt++;
				renderPreview();
			}
		}, 160);
	}

	const $modal = () => document.getElementById('modalCard');

	// ---- the small menu: Edit sprite / Swap sprite ----

	let menuEl = null;

	function closeMenu() {
		if (menuEl) {
			menuEl.remove();
			menuEl = null;
		}
		document.removeEventListener('pointerdown', outside, true);
	}

	function outside(e) {
		if (menuEl && !menuEl.contains(e.target)) {
			closeMenu();
		}
	}

	// Opens the Edit sprite / Swap sprite menu beside an element. target is what open takes; Swap is there when it is a part's picture.
	function menu(anchor, target) {
		closeMenu();
		const item = (text, title, run) => el('button', { className: 'px-menu-item', textContent: text, title, onclick: () => { closeMenu(); run(); } });
		menuEl = el('div', { className: 'px-menu' },
			item('Edit sprite…', 'Draw on this picture in the pixel editor', () => open(target)),
			target.part ? item('Swap sprite…', 'Put a different picture in this part', () => MS.dialogs.importSprite(target.part)) : null);
		document.body.append(menuEl);
		const r = anchor.getBoundingClientRect();
		menuEl.style.left = Math.max(4, Math.min(window.innerWidth - menuEl.offsetWidth - 4, r.left)) + 'px';
		menuEl.style.top = Math.min(window.innerHeight - menuEl.offsetHeight - 4, r.bottom + 4) + 'px';
		setTimeout(() => document.addEventListener('pointerdown', outside, true), 0);
	}

	MS.pixel = { open, menu, close: closeMenu };
})();
