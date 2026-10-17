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
		adjust: '<circle cx="8" cy="8" r="2.5"/><path d="M8 1.5v2M8 12.5v2M1.5 8h2M12.5 8h2M3.4 3.4l1.4 1.4M11.2 11.2l1.4 1.4M3.4 12.6l1.4-1.4M11.2 4.8l1.4-1.4"/>',
	};
	const TOOLS = [
		['pencil', 'Pencil (B)', 'b'],
		['eraser', 'Eraser (E): paints see-through', 'e'],
		['fill', 'Fill (G)', 'g'],
		['pick', 'Pick a colour (I); Alt+click does it with any tool', 'i'],
		['line', 'Line (L)', 'l'],
		['rect', 'Rectangle (R): hold Shift for a solid one', 'r'],
		['adjust', 'Brighten the glow (H): drag over lit pixels, right-click or Shift to dim them (Lights mode)', 'h'],
	];

	let E = null; // the editor that is open

	const icon = (name) => el('span', { className: 'ico-svg', innerHTML: '<svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">' + ICONS[name] + '</svg>' });
	const blank = (w, h) => ({ w, h, indices: new Uint8Array(w * h) });
	const copyFrame = (f) => ({ w: f.w, h: f.h, indices: Uint8Array.from(f.indices) });
	const frame = () => E.frames[E.cur];
	const safe = (s) => s.replace(/[^A-Za-z0-9 _-]+/g, '').trim().replace(/\s+/g, '') || 'Picture';

	// ---- a sprite as editable palette numbers ----

	const MAX_SIDE = 1024;

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

	const snapshot = () => ({ cur: E.cur, frames: E.frames.map(copyFrame), glow: E.glow ? { w: E.glow.w, h: E.glow.h, rgb: Uint8ClampedArray.from(E.glow.rgb) } : null });

	function restore(s) {
		E.glow = s.glow ? { w: s.glow.w, h: s.glow.h, rgb: Uint8ClampedArray.from(s.glow.rgb) } : null;
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
		if (E.mode === 'light') {
			plotGlow(points, index === 0);
			return;
		}
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
		if (E.mode === 'light') {
			floodGlow(x, y, index === 0);
			return;
		}
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

	// ---- lights: a glow layer the game draws as emitted light ----
	// One layer for the whole picture (the game takes one glow picture per part, whichever frame is showing). Each pixel holds the light colour it gives
	// off; a brighter colour is a stronger glow, black is none. It is saved as its own picture beside the sprite and named in the part's ScreenEffect.

	const lit = (g, i) => g.rgb[i * 3] + g.rgb[i * 3 + 1] + g.rgb[i * 3 + 2] > 0;

	function ensureGlow() {
		const f = E.frames[0];
		if (!E.glow || E.glow.w !== f.w || E.glow.h !== f.h) {
			E.glow = { w: f.w, h: f.h, rgb: new Uint8ClampedArray(f.w * f.h * 3) };
		}
		return E.glow;
	}

	const litCount = () => {
		const g = E.glow;
		let n = 0;
		for (let i = 0; g && i < g.w * g.h; i++) {
			if (lit(g, i)) {
				n++;
			}
		}
		return n;
	};

	// The game colour nearest to a light colour (never the see-through number, so a faint glow still shows).
	function nearestIndex(r, g, b) {
		const p = app().images.palette;
		let best = 1;
		let bestDist = Infinity;
		for (let i = 1; i < 256; i++) {
			const d = (p[i * 3] - r) ** 2 + (p[i * 3 + 1] - g) ** 2 + (p[i * 3 + 2] - b) ** 2;
			if (d < bestDist) {
				bestDist = d;
				best = i;
			}
		}
		return best;
	}

	const glowColour = () => E.glowColor.map((c) => Math.round(c * E.glowStrength));

	function setGlowAt(x, y, rgb) {
		const g = ensureGlow();
		if (x < 0 || y < 0 || x >= g.w || y >= g.h) {
			return;
		}
		const i = (y * g.w + x) * 3;
		g.rgb[i] = rgb ? rgb[0] : 0;
		g.rgb[i + 1] = rgb ? rgb[1] : 0;
		g.rgb[i + 2] = rgb ? rgb[2] : 0;
	}

	function plotGlow(points, erase) {
		const g = ensureGlow();
		const rgb = erase ? null : glowColour();
		for (const [x, y] of points) {
			setGlowAt(x, y, rgb);
			if (E.mirror) {
				setGlowAt(g.w - 1 - x, y, rgb);
			}
		}
	}

	function floodGlow(x, y, erase) {
		const g = ensureGlow();
		if (x < 0 || y < 0 || x >= g.w || y >= g.h) {
			return;
		}
		const rgb = erase ? [0, 0, 0] : glowColour();
		const at = (px, py) => (py * g.w + px) * 3;
		const same = (a, b) => g.rgb[a] === g.rgb[b] && g.rgb[a + 1] === g.rgb[b + 1] && g.rgb[a + 2] === g.rgb[b + 2];
		const start = at(x, y);
		const target = [g.rgb[start], g.rgb[start + 1], g.rgb[start + 2]];
		if (target[0] === rgb[0] && target[1] === rgb[1] && target[2] === rgb[2]) {
			return;
		}
		const stack = [[x, y]];
		while (stack.length) {
			const [px, py] = stack.pop();
			if (px < 0 || py < 0 || px >= g.w || py >= g.h) {
				continue;
			}
			const i = at(px, py);
			if (g.rgb[i] !== target[0] || g.rgb[i + 1] !== target[1] || g.rgb[i + 2] !== target[2]) {
				continue;
			}
			g.rgb[i] = rgb[0];
			g.rgb[i + 1] = rgb[1];
			g.rgb[i + 2] = rgb[2];
			stack.push([px + 1, py], [px - 1, py], [px, py + 1], [px, py - 1]);
		}
	}

	// Brighter or dimmer glow on the pixels under the brush (only ones that already glow).
	function adjustGlow(points, direction) {
		const g = ensureGlow();
		const factor = direction > 0 ? 1.15 : 0.87;
		for (const [x, y] of points) {
			for (const px of E.mirror ? [x, g.w - 1 - x] : [x]) {
				if (px < 0 || y < 0 || px >= g.w || y >= g.h) {
					continue;
				}
				const i = (y * g.w + px) * 3;
				if (lit(g, i / 3)) {
					for (let k = 0; k < 3; k++) {
						g.rgb[i + k] = Math.max(direction > 0 ? g.rgb[i + k] + 1 : 0, Math.round(g.rgb[i + k] * factor));
					}
				}
			}
		}
	}

	// Every pixel of the picture painted this colour gets the glow, at the strength set: lights a group in one go (every red lamp, say).
	function glowWhereColour(index) {
		const f = E.frames[0];
		const g = ensureGlow();
		const rgb = index ? glowColour() : null;
		for (let i = 0; i < f.indices.length; i++) {
			if (f.indices[i] === index && index) {
				g.rgb[i * 3] = rgb[0];
				g.rgb[i * 3 + 1] = rgb[1];
				g.rgb[i * 3 + 2] = rgb[2];
			}
		}
	}

	function scaleGlow(factor) {
		const g = ensureGlow();
		for (let i = 0; i < g.rgb.length; i++) {
			g.rgb[i] = Math.min(255, Math.round(g.rgb[i] * factor));
		}
	}

	// Where the picture's top-left sits on the part's origin (an unset offset is centred), for placing the glow picture's centre on the part.
	function spriteOrigin(part, f) {
		const o = part && part.sprite.offset && (part.sprite.offset.x || part.sprite.offset.y) ? part.sprite.offset : null;
		return o ? { x: Math.round(o.x), y: Math.round(o.y) } : { x: -Math.floor(f.w / 2), y: -Math.floor(f.h / 2) };
	}

	// A part that already has a ScreenEffect picture: its pixels come back onto the sprite's grid.
	async function loadGlow(part) {
		const a = app();
		const file = part && part.ent.file('ScreenEffect');
		if (!file) {
			return null;
		}
		const loaded = await a.images.get(file).catch(() => null);
		if (!loaded || !loaded.indices) {
			return null;
		}
		const f0 = E.frames[0];
		const so = spriteOrigin(part, f0);
		const g = { w: f0.w, h: f0.h, rgb: new Uint8ClampedArray(f0.w * f0.h * 3) };
		for (let y = 0; y < f0.h; y++) {
			for (let x = 0; x < f0.w; x++) {
				const gx = Math.floor(loaded.width / 2) + so.x + x;
				const gy = Math.floor(loaded.height / 2) + so.y + y;
				if (gx >= 0 && gy >= 0 && gx < loaded.width && gy < loaded.height) {
					const idx = loaded.indices[gy * loaded.width + gx];
					if (idx) {
						const c = palette(idx);
						g.rgb.set(c, (y * f0.w + x) * 3);
					}
				}
			}
		}
		E.glowOverall = Math.max(10, Math.min(255, Math.round(part.ent.num('EffectStartStrength', 0.5) * 255)));
		E.hadGlow = true;
		return g;
	}

	// The glow picture, centred on the part's origin as the game draws screen effects, and the part's ScreenEffect lines to match.
	async function saveGlow() {
		const a = app();
		const part = E.part;
		if (!part || (!litCount() && !E.hadGlow)) {
			return;
		}
		const g = ensureGlow();
		const f0 = E.frames[0];
		const dir = E.file.slice(0, E.file.lastIndexOf('/'));
		const stem = safe(E.file.split('/').pop().replace(/\.[^.]+$/, '').replace(/\d+$/, ''));
		const path = dir + '/' + stem + 'Glow.png';
		if (!litCount()) {
			a.edit('remove the glow of ' + part.name, () => {
				const node = a.ownBlock(part);
				if (node) {
					for (const key of ['ScreenEffect', 'EffectStartStrength', 'EffectStopStrength', 'EffectAlwaysShows']) {
						const c = node.child(key);
						if (c) {
							c.remove();
						}
					}
				}
			});
			E.hadGlow = false;
			return;
		}
		const so = spriteOrigin(part, f0);
		const halfW = Math.max(1, Math.ceil(Math.max(Math.abs(so.x), Math.abs(so.x + f0.w))));
		const halfH = Math.max(1, Math.ceil(Math.max(Math.abs(so.y), Math.abs(so.y + f0.h))));
		const GW = halfW * 2;
		const GH = halfH * 2;
		const out = new Uint8Array(GW * GH);
		for (let y = 0; y < f0.h; y++) {
			for (let x = 0; x < f0.w; x++) {
				const i = y * f0.w + x;
				if (lit(g, i)) {
					out[(halfH + so.y + y) * GW + (halfW + so.x + x)] = nearestIndex(g.rgb[i * 3], g.rgb[i * 3 + 1], g.rgb[i * 3 + 2]);
				}
			}
		}
		await a.fs.writeFile(path, await a.images.encodePng(GW, GH, out));
		a.forgetSprites([path]);
		a.edit('glow of ' + part.name, () => {
			const node = a.ownBlock(part);
			if (!node) {
				return;
			}
			let se = node.child('ScreenEffect');
			if (!se) {
				se = node.add('ScreenEffect', 'ContentFile');
			}
			se.setValue('ContentFile');
			for (const old of se.children.filter((c) => c.key === 'Path' || c.key === 'CopyOf')) {
				old.remove();
			}
			se.set('FilePath', path);
			node.set('EffectAlwaysShows', '1');
			node.set('EffectStartStrength', String(+(E.glowOverall / 255).toFixed(3)));
			node.set('EffectStopStrength', String(+(E.glowOverall / 255).toFixed(3)));
		});
		E.hadGlow = true;
	}

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
		if (E.dark) {
			// In the dark: what the lights do shows best with nothing else lit.
			ctx.fillStyle = 'rgba(4, 5, 8, .92)';
			ctx.fillRect(0, 0, W, H);
		}
		if (E.onion && E.cur > 0) {
			const before = E.frames[E.cur - 1];
			if (before.w === f.w && before.h === f.h) {
				ctx.globalAlpha = 0.35;
				ctx.drawImage(a.images.fromIndices(before.w, before.h, before.indices).canvas, 0, 0, W, H);
				ctx.globalAlpha = 1;
			}
		}
		if (E.dark) {
			ctx.globalAlpha = 0.4;
		}
		ctx.drawImage(a.images.fromIndices(f.w, f.h, f.indices).canvas, 0, 0, W, H);
		ctx.globalAlpha = 1;
		drawGlow(ctx, W, H, z);
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
			ctx.strokeStyle = MS.theme.accentA(0.6);
			ctx.setLineDash([4, 4]);
			ctx.beginPath();
			ctx.moveTo(W / 2, 0);
			ctx.lineTo(W / 2, H);
			ctx.stroke();
			ctx.setLineDash([]);
		}
		if (E.hover && E.hover.inside) {
			ctx.strokeStyle = MS.theme.accent();
			ctx.lineWidth = 1;
			ctx.strokeRect(E.hover.x * z + 0.5, E.hover.y * z + 0.5, z - 1, z - 1);
		}
	}

	// The glow over the picture: each lit pixel in its colour, and a soft bloom round it, added to what is there, as the game's lighting would.
	function drawGlow(ctx, W, H, z) {
		const g = E.glow;
		if (!g || !(E.glowShow || E.mode === 'light') || !litCount()) {
			return;
		}
		const canvas = document.createElement('canvas');
		canvas.width = g.w;
		canvas.height = g.h;
		const gctx = canvas.getContext('2d');
		const data = gctx.createImageData(g.w, g.h);
		for (let i = 0; i < g.w * g.h; i++) {
			if (lit(g, i)) {
				data.data[i * 4] = g.rgb[i * 3];
				data.data[i * 4 + 1] = g.rgb[i * 3 + 1];
				data.data[i * 4 + 2] = g.rgb[i * 3 + 2];
				data.data[i * 4 + 3] = 255;
			}
		}
		gctx.putImageData(data, 0, 0);
		ctx.save();
		ctx.globalCompositeOperation = 'lighter';
		ctx.imageSmoothingEnabled = false;
		ctx.globalAlpha = E.mode === 'light' ? 0.55 : 0.4;
		ctx.drawImage(canvas, 0, 0, W, H);
		ctx.globalAlpha = 1;
		for (const blur of [z * 0.9, z * 2.2, z * 4]) {
			ctx.filter = 'blur(' + Math.round(blur) + 'px)';
			ctx.globalAlpha = 0.55;
			ctx.drawImage(canvas, 0, 0, W, H);
		}
		ctx.restore();
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
			// Drag a frame to a new place in the sequence.
			b.draggable = true;
			b.ondragstart = (e) => {
				e.dataTransfer.setData('text/plain', String(i));
				e.dataTransfer.effectAllowed = 'move';
				b.classList.add('dragging');
			};
			b.ondragend = () => b.classList.remove('dragging');
			b.ondragover = (e) => {
				e.preventDefault();
				e.dataTransfer.dropEffect = 'move';
				b.classList.add('drop');
			};
			b.ondragleave = () => b.classList.remove('drop');
			b.ondrop = (e) => {
				e.preventDefault();
				b.classList.remove('drop');
				const from = Number(e.dataTransfer.getData('text/plain'));
				if (Number.isInteger(from) && from !== i && from >= 0 && from < E.frames.length) {
					moveFrameTo(from, i);
				}
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
		E.lightBtn.classList.toggle('on', E.mode === 'light');
		E.glowBtn.classList.toggle('on', E.glowShow);
		E.toolButtons.adjust.hidden = E.mode !== 'light';
		E.lightBox.hidden = E.mode !== 'light';
		if (E.mode !== 'light' && E.tool === 'adjust') {
			E.tool = 'pencil';
		}
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
		if (!p.inside) {
			return;
		}
		if (E.mode === 'light') {
			// The light takes the colour of the picture's pixel (or, over see-through, of the glow already there).
			const index = frame().indices[p.y * frame().w + p.x];
			const g = ensureGlow();
			const i = p.y * g.w + p.x;
			const c = index ? palette(index) : lit(g, i) ? [g.rgb[i * 3], g.rgb[i * 3 + 1], g.rgb[i * 3 + 2]] : null;
			if (c) {
				const top = Math.max(1, c[0], c[1], c[2]);
				// Kept as a full-strength colour; the strength slider is the amount.
				E.glowColor = c.map((v) => Math.round((v * 255) / top));
				E.glowStrength = Math.max(0.1, Math.min(1, top / 255));
				syncLights();
			}
			return;
		}
		setColor(frame().indices[p.y * frame().w + p.x]);
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
		if (E.tool === 'adjust') {
			if (E.mode !== 'light') {
				return;
			}
			begin();
			E.drag = { last: p, adjust: e.button === 2 || e.shiftKey ? -1 : 1 };
			adjustGlow([[p.x, p.y]], E.drag.adjust);
			render();
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
			if (E.drag.adjust) {
				adjustGlow(linePoints(E.drag.last.x, E.drag.last.y, p.x, p.y), E.drag.adjust);
				E.drag.last = p;
			} else if (E.drag.last) {
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
		E.dirty = true;
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

	// Takes a frame out and puts it back at another place; the frame you were on stays selected.
	function moveFrameTo(from, to) {
		edit(() => {
			const current = E.frames[E.cur];
			const [f] = E.frames.splice(from, 1);
			E.frames.splice(to, 0, f);
			E.cur = E.frames.indexOf(current);
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

	// Every frame is cropped, or given see-through room, so the picture stays where the anchor says: E.anchor is [0..2, 0..2] (left/middle/right, top/middle/bottom).
	function resize(width, height) {
		width = Math.max(1, Math.min(MAX_SIDE, Math.floor(width) || 1));
		height = Math.max(1, Math.min(MAX_SIDE, Math.floor(height) || 1));
		if (width === frame().w && height === frame().h) {
			return;
		}
		edit(() => {
			E.frames = E.frames.map((f) => {
				const out = blank(width, height);
				// where the old picture's top left goes in the new one (negative when cropping from the left or top)
				const dx = Math.round(((width - f.w) * E.anchor[0]) / 2);
				const dy = Math.round(((height - f.h) * E.anchor[1]) / 2);
				for (let y = 0; y < f.h; y++) {
					for (let x = 0; x < f.w; x++) {
						const nx = x + dx;
						const ny = y + dy;
						if (nx >= 0 && ny >= 0 && nx < width && ny < height) {
							out.indices[ny * width + nx] = f.indices[y * f.w + x];
						}
					}
				}
				return out;
			});
		});
		fit();
		renderAll();
	}

	// Shrinks every frame to the smallest box that holds the art in any of them (the see-through border goes).
	function trim() {
		let x0 = Infinity;
		let y0 = Infinity;
		let x1 = -1;
		let y1 = -1;
		for (const f of E.frames) {
			for (let y = 0; y < f.h; y++) {
				for (let x = 0; x < f.w; x++) {
					if (f.indices[y * f.w + x] !== 0) {
						x0 = Math.min(x0, x);
						y0 = Math.min(y0, y);
						x1 = Math.max(x1, x);
						y1 = Math.max(y1, y);
					}
				}
			}
		}
		if (x1 < 0 || (x0 === 0 && y0 === 0 && x1 === frame().w - 1 && y1 === frame().h - 1)) {
			return;
		}
		const width = x1 - x0 + 1;
		const height = y1 - y0 + 1;
		edit(() => {
			E.frames = E.frames.map((f) => {
				const out = blank(width, height);
				for (let y = 0; y < height; y++) {
					for (let x = 0; x < width; x++) {
						out.indices[y * width + x] = f.indices[(y + y0) * f.w + x + x0];
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

	function finished(message, action) {
		const a = app();
		a.requestDraw();
		MS.panels.refresh();
		a.toast(message, 'good', action);
		const done = E.onSaved;
		E.saved = true;
		E.dirty = false;
		E.frameCount = E.frames.length;
		if (E.tab) {
			E.tab.title = 'Sprite · ' + (E.file ? E.file.split('/').pop() : 'a new picture');
			if (MS.ide) {
				MS.ide.renderTabs && MS.ide.renderTabs();
			}
		}
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

	// The glow is part of the unit (its ScreenEffect lines), so it is saved when the part can be changed; otherwise it waits for a copy of the unit.
	async function saveGlowIfEditable() {
		const a = app();
		if (!E.part || (!litCount() && !E.hadGlow)) {
			return;
		}
		if (a.lockReason(E.part)) {
			a.toast('The glow is part of the unit, which isn\'t yours yet: clone it into your project, then save again to keep the glow.', 'bad');
			return;
		}
		await saveGlow();
	}

	async function saveInPlace() {
		const a = app();
		try {
			const countChanged = E.frames.length !== E.frameCount;
			await MS.dialogs.saveSprite(countChanged || E.layerKey ? E.part : null, E.file, outFrames(), false, E.layerKey);
			await saveGlowIfEditable();
			await keepUnit();
		} catch (e) {
			a.toast('Couldn\'t save the picture: ' + e.message, 'bad');
			return;
		}
		finished('Saved ' + E.file.split('/').pop() + '.');
	}

	async function saveCopy(mod, name, unique) {
		const a = app();
		const folder = mod + '/Sprites/' + safe(a.actor ? a.actor.name : 'Pictures');
		let path = folder + '/' + safe(name) + '.png';
		// An automatic copy takes a free name (Torso, then Torso2...) rather than overwrite a picture that is already there.
		for (let n = 2; unique && path !== E.file && (await a.fs.exists(path)); n++) {
			path = folder + '/' + safe(name) + n + '.png';
		}
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
			await MS.dialogs.saveSprite(linkable ? E.part : null, path, outFrames(), false, E.layerKey);
			E.file = path;
			if (E.tab) {
				E.tab.key = spriteKey(path);
			}
			await saveGlowIfEditable();
			await keepUnit();
		} catch (e) {
			a.toast('Couldn\'t save the picture: ' + e.message, 'bad');
			return;
		}
		const clone = E.part && !linkable && a.actor && a.project ? { label: 'Clone ' + a.actor.name + ' into ' + a.project.replace(/\.rte$/i, ''), run: () => MS.dialogs.copyUnit() } : null;
		finished('Saved ' + path + (E.part && !linkable ? '. The unit is not yours yet: clone it into your project, then Swap sprite puts this in.' : '.'), clone);
	}

	// A picture that isn't in your mod saves as a copy in the project, named after the original (nothing to fill in).
	async function saveAuto() {
		const a = app();
		if (!a.project) {
			copyForm();
			return;
		}
		const stem = E.file ? safe(E.file.split('/').pop().replace(/\.[^.]+$/, '').replace(/\d+$/, '')) : 'Picture';
		await saveCopy(a.project, stem || 'Picture', true);
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

	// The Lights controls: the light's colour and strength, the strength the game gives it, and ways to change a whole group at once.
	function buildLights() {
		const hex = (c) => '#' + c.map((v) => v.toString(16).padStart(2, '0')).join('');
		const color = el('input', { type: 'color', value: hex(E.glowColor), title: 'The light\'s colour (Pick, with Alt, takes it from the picture)' });
		const strength = el('input', { type: 'range', min: 10, max: 100, value: Math.round(E.glowStrength * 100), title: 'How strongly the pixels you paint glow' });
		const strengthText = el('span', { className: 'px-note' });
		const overall = el('input', { type: 'range', min: 10, max: 255, value: E.glowOverall, title: 'How strongly the game draws the whole glow (EffectStartStrength)' });
		const overallText = el('span', { className: 'px-note' });
		E.lightUi = { color, strength, strengthText, overall, overallText };
		color.oninput = () => {
			E.glowColor = [parseInt(color.value.slice(1, 3), 16), parseInt(color.value.slice(3, 5), 16), parseInt(color.value.slice(5, 7), 16)];
			syncLights();
		};
		strength.oninput = () => {
			E.glowStrength = Number(strength.value) / 100;
			syncLights();
		};
		overall.oninput = () => {
			E.glowOverall = Number(overall.value);
			E.dirty = true;
			syncLights();
		};
		const act = (text, title, run) => el('button', { className: 'ico-btn', textContent: text, title, onclick: () => { begin(); run(); commit(); renderAll(); } });
		const dark = el('label', { className: 'ed-check' }, el('input', { type: 'checkbox', onchange: (e) => { E.dark = e.target.checked; render(); } }), 'See it in the dark');
		const box = el('div', { hidden: true },
			el('div', { className: 'px-h' }, 'Lights'),
			el('div', { className: 'px-row' }, 'Colour', color, 'Strength', strength, strengthText),
			el('div', { className: 'px-row' }, 'In the game', overall, overallText),
			el('div', { className: 'px-row' },
				act('Light this colour', 'Every pixel of the picture painted the colour picked in the palette gets the glow, at the strength set', () => glowWhereColour(E.color)),
				act('Brighter', 'Make every light stronger', () => scaleGlow(1.2)),
				act('Dimmer', 'Make every light weaker', () => scaleGlow(0.8)),
				act('Clear lights', 'Take all the glow off', () => { E.glow = null; })),
			dark,
			el('p', { className: 'px-note', textContent: 'Paint with any tool: it lights pixels instead of colouring them. Brighten (H) strengthens lit pixels, right-click dims. The game uses one glow picture for the whole part, whichever frame shows.' }));
		syncLights();
		return box;
	}

	function syncLights() {
		const u = E.lightUi;
		if (!u) {
			return;
		}
		u.color.value = '#' + E.glowColor.map((v) => v.toString(16).padStart(2, '0')).join('');
		u.strength.value = Math.round(E.glowStrength * 100);
		u.strengthText.textContent = Math.round(E.glowStrength * 100) + '%';
		u.overall.value = E.glowOverall;
		u.overallText.textContent = E.glowOverall + ' of 255';
	}

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
		E.lightBtn = el('button', { className: 'ico-btn', textContent: '💡 Lights', title: 'Paint where the picture gives off light: the game draws it as real glow. Every tool then paints the light instead of the colours.', onclick: () => { E.mode = E.mode === 'light' ? 'colour' : 'light'; updateButtons(); render(); } });
		E.glowBtn = toggle('Glow', 'Show the glow on the picture', 'glowShow');
		E.lightBox = buildLights();
		E.undoBtn = el('button', { className: 'ico-btn', textContent: '↶ Undo', title: 'Undo (Ctrl+Z)', onclick: undo });
		E.redoBtn = el('button', { className: 'ico-btn', textContent: '↷ Redo', title: 'Redo (Ctrl+Y)', onclick: redo });
		E.zoomText = el('span', { className: 'px-zoom' });
		const zoomBy = (d) => () => { E.zoom = Math.max(1, Math.min(40, E.zoom + d * (E.zoom >= 8 ? 2 : 1))); renderAll(); };
		const top = el('div', { className: 'px-top' }, E.undoBtn, E.redoBtn, el('span', { className: 'px-sep' }), E.gridBtn, E.onionBtn, E.mirrorBtn, el('span', { className: 'px-sep' }), E.lightBtn, E.glowBtn, el('span', { className: 'spacer' }),
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
		E.sizeW = el('input', { type: 'number', min: 1, max: MAX_SIDE, className: 'px-size' });
		E.anchor = [0, 0];
		E.anchorBtns = [];
		const anchorGrid = el('div', { className: 'px-anchor', title: 'Where the picture stays when the canvas grows or shrinks' });
		for (let ay = 0; ay < 3; ay++) {
			for (let ax = 0; ax < 3; ax++) {
				const b = el('button', { className: 'px-anchor-cell', onclick: () => { E.anchor = [ax, ay]; E.anchorBtns.forEach((c) => c.b.classList.toggle('on', c.ax === ax && c.ay === ay)); } });
				b.classList.toggle('on', ax === 0 && ay === 0);
				E.anchorBtns.push({ b, ax, ay });
				anchorGrid.append(b);
			}
		}
		E.sizeH = el('input', { type: 'number', min: 1, max: MAX_SIDE, className: 'px-size' });
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
			E.lightBox,
			el('div', { className: 'px-h' }, 'Canvas size (all frames)'),
			el('div', { className: 'px-row' }, E.sizeW, '×', E.sizeH, small('Apply', 'Make the canvas this size: bigger adds see-through room, smaller crops. The picture stays where the square points.', () => resize(Number(E.sizeW.value), Number(E.sizeH.value)))),
			el('div', { className: 'px-row' }, anchorGrid, el('span', { className: 'px-note', textContent: 'Picture stays here' }), small('Trim', 'Cut away the see-through border, so the canvas is just big enough for the art', trim)));

		// saving
		E.copyBox = el('div', { className: 'px-copy', hidden: true });
		const own = canOverwrite();
		const saveRow = el('div', { className: 'px-save' },
			el('span', { className: 'px-where', textContent: E.file ? (own ? 'Saves over ' + E.file.split('/').pop() : app().project ? 'Saves as a copy in ' + app().project + ' (this is ' + E.file.split('/').pop() + ' from ' + E.file.split('/')[0] + ')' : 'This is in ' + E.file.split('/')[0] + ', so it saves as a copy in your mod: ' + E.file.split('/').pop()) : 'A new picture: it saves in your mod.' }),
			el('span', { className: 'spacer' }),
			own ? el('button', { className: 'tb primary', textContent: 'Save', onclick: saveInPlace }) : app().project ? el('button', { className: 'tb primary', textContent: 'Save', title: 'Saves a copy in ' + app().project + ', named after this picture', onclick: saveAuto }) : null,
			el('button', { className: 'tb' + (own || app().project ? '' : ' primary'), textContent: own ? 'Save a copy…' : app().project ? 'Save as…' : 'Save a copy in my mod…', onclick: copyForm }));
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
		if (target.part && a.needsCopy()) {
			a.copyThen(target.part, (copied) => open(Object.assign({}, target, { part: copied })));
			return;
		}
		const part = target.part || null;
		const file = part ? (target.key ? part.ent.file(target.key) : part.sprite.file) : target.file || null;
		const key = file ? spriteKey(file) : 'sprite:new' + ++newCount;
		const existing = MS.ide.findSpriteTab(key);
		if (existing) {
			MS.ide.activate(existing);
			return;
		}
		// One picture, one editor: a second request while the first is still loading (a double click) or in another window comes back to it.
		if (opening.has(key)) {
			return;
		}
		if (file && (await MS.popout.elsewhere(key))) {
			a.toast(file.split('/').pop() + ' is already open in another Studio window.', 'good');
			return;
		}
		opening.add(key);
		try {
			await openNew(target, part, file, key);
		} finally {
			opening.delete(key);
		}
	}

	async function openNew(target, part, file, key) {
		const a = app();
		const frameCount = part ? (target.key ? 1 : part.sprite.frameCount) : target.frameCount || 1;
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
		E = { layerKey: target.key || null, part, file, frameCount: file ? frames.length : 0, frames, cur: 0, tool: 'pencil', color: common ? common[0] : 1, recent: [], mirror: false, grid: true, onion: false, zoom: 8, undo: [], redo: [], pending: null, dirty: false, warned: false, drag: null, preview: null, hover: null, previewAt: 0, saved: false, onSaved: target.onSaved || null, mode: 'colour', glow: null, glowColor: [255, 200, 120], glowStrength: 1, glowOverall: 128, glowShow: true, dark: false, hadGlow: false };
		E.glow = await loadGlow(part);
		const body = build();
		E.root = body;
		const name = file ? file.split('/').pop() : 'a new picture';
		const px = E;
		const tab = MS.ide.openSpriteTab(key, 'Sprite · ' + name, px, (host, t) => mount(host, t));
		px.tab = tab;
	}

	// Puts a sprite tab's editor in the page (the tab's own state becomes the current one).
	function mount(host, tab) {
		const px = tab.px;
		E = px;
		if (host.firstElementChild !== px.root) {
			host.replaceChildren(px.root);
		}
		setTimeout(() => {
			if (E !== px || !px.cv.isConnected) {
				return;
			}
			if (!px.fitted) {
				px.fitted = true;
				fit();
			}
			renderPalette();
			renderAll();
		}, 30);
	}

	// Ctrl+S on a picture being edited: written in place when it is in a mod of yours; a game picture needs Save a copy, which asks for a name.
	async function saveTab(px) {
		const before = E;
		E = px;
		try {
			if (!canOverwrite()) {
				if (!app().project) {
					return false;
				}
				await saveAuto();
				return !px.dirty;
			}
			await saveInPlace();
			return !px.dirty;
		} finally {
			E = before === px ? px : before;
		}
	}

	function forget(px) {
		if (E === px) {
			E = null;
		}
	}

	let newCount = 0;
	const opening = new Set();
	const spriteKey = (file) => 'sprite:' + file.replace(/\\/g, '/').toLowerCase();

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

	// One key handler and one preview timer for every sprite tab: they act on the editor that is showing.
	document.addEventListener('keydown', (e) => {
		if (E && E.cv && E.cv.isConnected && E.cv.offsetParent) {
			onKey(e);
		}
	}, true);
	setInterval(() => {
		if (E && E.cv && E.cv.isConnected && E.cv.offsetParent && E.frames.length > 1) {
			E.previewAt++;
			renderPreview();
		}
	}, 160);

	MS.pixel = { open, menu, close: closeMenu, forget, saveTab, loadFrames };
})();
