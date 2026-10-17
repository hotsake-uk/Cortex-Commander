// Cortex Studio: Resize sprite. Next to Edit sprite: the part's picture is shown on the grid, in the exact scale of the unit (one grid square is one
// game pixel), with a handle to drag its corner and boxes for the exact size, so it can be made bigger or smaller until it fits the rest of the unit.
// Apply saves the resized picture as a copy in your project, points the part at it (its picture offset scaled to match), and opens it to edit.
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

	let R = null; // the resize in progress: { part, frames, w0, h0, W, H, offset0, lock, panel, ... }

	// Nearest-pixel scaling of palette pictures (see-through stays see-through): each new pixel takes the old one under its middle.
	function scaled(frame, W, H) {
		const out = new Uint8Array(W * H);
		for (let y = 0; y < H; y++) {
			const sy = Math.min(frame.h - 1, Math.floor(((y + 0.5) * frame.h) / H));
			for (let x = 0; x < W; x++) {
				const sx = Math.min(frame.w - 1, Math.floor(((x + 0.5) * frame.w) / W));
				out[y * W + x] = frame.indices[sy * frame.w + sx];
			}
		}
		return { w: W, h: H, indices: out };
	}

	// How tall the unit stands in the view (its parts' pictures, as drawn now), for "this part is N% of the unit".
	function unitHeight() {
		const v = app().view;
		let min = Infinity;
		let max = -Infinity;
		for (const item of v.drawList || []) {
			if (R && item.part === R.part) {
				continue;
			}
			const corners = v.itemRect(item);
			if (corners) {
				for (const p of corners) {
					min = Math.min(min, p.y);
					max = Math.max(max, p.y);
				}
			}
		}
		return max > min ? max - min : 0;
	}

	function previewFrames() {
		if (!R.cache.has(R.W + 'x' + R.H)) {
			R.cache.set(R.W + 'x' + R.H, R.frames.map((f) => {
				const s = scaled(f, R.W, R.H);
				return { canvas: app().images.fromIndices(s.w, s.h, s.indices).canvas, width: s.w, height: s.h, indices: s.indices };
			}));
		}
		return R.cache.get(R.W + 'x' + R.H);
	}

	// The preview the viewport draws in place of the part's own picture.
	function makePreview() {
		return {
			part: R.part,
			frameAt: (i) => previewFrames()[Math.min(previewFrames().length - 1, i || 0)],
			offset: () => (R.offset0 ? { x: R.offset0.x * (R.W / R.w0), y: R.offset0.y * (R.H / R.h0) } : null),
			onDrag: (factor) => setFactor(factor),
			baseW: () => R.w0,
			baseH: () => R.h0,
		};
	}

	function refresh() {
		const a = app();
		R.fields.w.value = R.W;
		R.fields.h.value = R.H;
		const pct = Math.round((R.W / R.w0) * 100);
		R.fields.pct.value = pct;
		const unit = unitHeight();
		R.info.textContent = 'Now ' + R.w0 + '×' + R.h0 + ' px, new ' + R.W + '×' + R.H + ' px (' + pct + '%).';
		R.unitInfo.textContent = unit ? 'The rest of the unit stands about ' + Math.round(unit) + ' px tall: this picture is ' + Math.round((R.H / unit) * 100) + '% of that (it was ' + Math.round((R.h0 / unit) * 100) + '%).' : '';
		R.apply.disabled = R.W === R.w0 && R.H === R.h0;
		a.view.resizePreview = R.preview;
		a.requestDraw();
	}

	function setSize(W, H) {
		R.W = Math.max(1, Math.min(512, Math.round(W) || 1));
		R.H = Math.max(1, Math.min(512, Math.round(H) || 1));
		refresh();
	}

	function setFactor(f) {
		setSize(R.w0 * f, R.h0 * f);
	}

	function close() {
		if (!R) {
			return;
		}
		const a = app();
		a.view.resizePreview = null;
		a.view.options.grid = R.gridWas;
		const gridBtn = document.querySelector('#viewOptions [data-opt=grid]');
		if (gridBtn) {
			gridBtn.classList.toggle('sel', R.gridWas);
		}
		R.panel.remove();
		R = null;
		a.requestDraw();
	}

	async function open(part) {
		const a = app();
		if (a.needsCopy()) {
			a.copyThen(part, (copied) => open(copied));
			return;
		}
		const reason = a.lockReason(part);
		if (reason) {
			a.toast(reason, 'bad');
			return;
		}
		if (!part.sprite.file) {
			a.toast(part.name + ' has no picture to resize.', 'bad');
			return;
		}
		close();
		const frames = await MS.pixel.loadFrames(part.sprite.file, part.sprite.frameCount);
		if (!frames || !frames.length) {
			a.toast('Studio can\'t read ' + part.sprite.file + '.', 'bad');
			return;
		}
		const s = part.sprite;
		R = { part, frames, w0: frames[0].w, h0: frames[0].h, W: frames[0].w, H: frames[0].h, offset0: s.offset && (s.offset.x || s.offset.y) ? { x: s.offset.x, y: s.offset.y } : null, lock: true, cache: new Map(), fields: {}, gridWas: a.view.options.grid };
		a.view.options.grid = true;
		const gridBtn = document.querySelector('#viewOptions [data-opt=grid]');
		if (gridBtn) {
			gridBtn.classList.add('sel');
		}
		const num = (min, max, onInput) => {
			const input = el('input', { type: 'number', min, max, className: 'ed-input num' });
			input.oninput = () => {
				if (input.value !== '') {
					onInput(Number(input.value));
				}
			};
			return input;
		};
		R.fields.w = num(1, 512, (v) => (R.lock ? setSize(v, (R.h0 * v) / R.w0) : setSize(v, R.H)));
		R.fields.h = num(1, 512, (v) => (R.lock ? setSize((R.w0 * v) / R.h0, v) : setSize(R.W, v)));
		R.fields.pct = num(5, 800, (v) => setFactor(v / 100));
		const slider = el('input', { type: 'range', min: 10, max: 400, step: 1, value: 100, title: 'Scale' });
		slider.oninput = () => setFactor(Number(slider.value) / 100);
		const lock = el('label', { className: 'ed-check', title: 'Keep the picture\'s shape' }, el('input', { type: 'checkbox', checked: true, onchange: (e) => { R.lock = e.target.checked; } }), 'Keep shape');
		const after = el('input', { type: 'checkbox', checked: true });
		R.info = el('div', { className: 'rs-info' });
		R.unitInfo = el('div', { className: 'rs-info dim' });
		R.apply = el('button', { className: 'tb primary', textContent: 'Resize', onclick: () => apply(after.checked) });
		R.panel = el('div', { className: 'rs-panel' },
			el('div', { className: 'rs-title' }, el('b', { textContent: 'Resize ' + part.name }), el('span', { className: 'spacer' }), el('button', { className: 'ico-btn', textContent: '×', title: 'Cancel', onclick: close })),
			el('div', { className: 'rs-row' }, 'Size', R.fields.w, '×', R.fields.h, 'px', lock),
			el('div', { className: 'rs-row' }, 'Scale', R.fields.pct, '%', slider),
			R.info, R.unitInfo,
			el('div', { className: 'rs-hint', textContent: 'Drag the square on the picture\'s corner, or type a size. One grid square is one game pixel, in the same scale as the whole unit.' }),
			el('div', { className: 'rs-row' }, el('label', { className: 'ed-check' }, after, 'Then edit it'), el('span', { className: 'spacer' }), el('button', { className: 'tb', textContent: 'Cancel', onclick: close }), R.apply));
		document.getElementById('viewWrap').append(R.panel);
		R.preview = makePreview();
		a.select(part);
		refresh();
	}

	// Saves the resized frames as a copy in the project (or in place when the picture is already yours), points the part at it, and opens the editor.
	async function apply(edit) {
		const a = app();
		const r = R;
		if (!r) {
			return;
		}
		const part = r.part;
		const frames = previewFrames().map((f) => ({ width: f.width, height: f.height, indices: f.indices }));
		const file = part.sprite.file;
		const own = !a.fs.isOfficial(file.split('/')[0]) && a.lib.lockKind({ path: file }) === '' && (!a.project || file.split('/')[0].toLowerCase() === a.project.toLowerCase());
		let path = file;
		if (!own) {
			const mod = a.project || (a.actor && a.actor.path.split('/')[0]);
			const folder = mod + '/Sprites/' + MS.dialogs.parts.safeName(a.actor ? a.actor.name : 'Pictures');
			const stem = MS.dialogs.parts.safeName(file.split('/').pop().replace(/\.[^.]+$/, '').replace(/\d+$/, '')) || 'Picture';
			path = folder + '/' + stem + '.png';
			for (let n = 2; await a.fs.exists(path); n++) {
				path = folder + '/' + stem + n + '.png';
			}
		}
		const oldOffset = r.offset0;
		const factorX = r.W / r.w0;
		const factorY = r.H / r.h0;
		close();
		try {
			await MS.dialogs.saveSprite(part, path, frames, false);
		} catch (e) {
			a.toast('Couldn\'t save the resized picture: ' + e.message, 'bad');
			return;
		}
		// A picture offset that was set keeps the same place on the picture; an unset one stays centred.
		const role = part.role;
		const nth = a.rig.parts.filter((p) => p.role === role).indexOf(part);
		const resized = a.rig.parts.filter((p) => p.role === role)[Math.max(0, nth)] || part;
		if (oldOffset) {
			a.setVector(resized, 'SpriteOffset', Math.round(oldOffset.x * factorX), Math.round(oldOffset.y * factorY), 'resize ' + resized.name);
		}
		a.toast('Resized to ' + frames[0].width + '×' + frames[0].height + ' and saved ' + path.split('/').pop() + '.', 'good');
		if (edit) {
			const again = a.rig.parts.filter((p) => p.role === role)[Math.max(0, nth)] || resized;
			MS.pixel.open({ part: again });
		}
	}

	MS.resize = { open, close, active: () => !!R };
})();
