// Cortex Studio: the transform tool. Next to Edit sprite, Resize puts a box round the part's picture on the grid, in the exact scale of the unit (one
// grid square is one game pixel): drag any corner or edge square to resize it, drag inside to move it, with Keep proportions on (or Shift held) so a corner
// scales evenly. A panel takes exact numbers too. Apply saves the resized picture as a copy in your project, points the part at it with its picture
// offset set to where the box is, and opens it to edit. The box follows the part through anything else you do while it is open.
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

	let R = null; // the transform in progress

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

	// How tall the unit stands in the view (its other parts' pictures, as drawn now), for "this picture is N% of the unit".
	function unitHeight() {
		const v = app().view;
		let min = Infinity;
		let max = -Infinity;
		for (const item of v.drawList || []) {
			if (R && item.part.id === R.partId) {
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
		const key = R.W + 'x' + R.H;
		if (!R.cache.has(key)) {
			R.cache.set(key, R.frames.map((f) => {
				const s = scaled(f, R.W, R.H);
				return { canvas: app().images.fromIndices(s.w, s.h, s.indices).canvas, width: s.w, height: s.h, indices: s.indices };
			}));
		}
		return R.cache.get(key);
	}

	// What the viewport asks while it draws and handles the pointer. The part is found again by its id each time, so the box survives the unit being
	// rebuilt (a move, an edit) while it is open.
	function makePreview() {
		return {
			get partId() {
				return R.partId;
			},
			frameAt: (i) => previewFrames()[Math.min(previewFrames().length - 1, i || 0)],
			offset: () => ({ x: R.tlx, y: R.tly }),
			// A drag on a handle: sx, sy are -1, 0 or 1 (which side), local the pointer in the box's own axes, measured from the box's top-left at the start.
			onHandle(start, sx, sy, local, shift) {
				const keep = R.lock !== shift;
				let W = start.W;
				let H = start.H;
				let left = 0;
				let top = 0;
				if (sx > 0) {
					W = local.x;
				} else if (sx < 0) {
					W = start.W - local.x;
				}
				if (sy > 0) {
					H = local.y;
				} else if (sy < 0) {
					H = start.H - local.y;
				}
				if (keep) {
					// Evenly: by whichever way you dragged further (a corner), or by the one way an edge goes.
					const fx = W / start.W;
					const fy = H / start.H;
					const f = sx && sy ? (Math.abs(fx - 1) > Math.abs(fy - 1) ? fx : fy) : sx ? fx : fy;
					W = start.W * f;
					H = start.H * f;
				}
				W = Math.max(1, Math.round(W));
				H = Math.max(1, Math.round(H));
				// The opposite side stays put; a side with no handle on it (an edge handle's other axis) stays centred.
				left = sx > 0 ? 0 : sx < 0 ? start.W - W : (start.W - W) / 2;
				top = sy > 0 ? 0 : sy < 0 ? start.H - H : (start.H - H) / 2;
				R.W = W;
				R.H = H;
				R.tlx = Math.round(start.tlx + left);
				R.tly = Math.round(start.tly + top);
				refresh();
			},
			onMove(start, local) {
				R.tlx = Math.round(start.tlx + local.x);
				R.tly = Math.round(start.tly + local.y);
				refresh();
			},
			start: () => ({ W: R.W, H: R.H, tlx: R.tlx, tly: R.tly }),
		};
	}

	function refresh() {
		const a = app();
		R.fields.w.value = R.W;
		R.fields.h.value = R.H;
		R.fields.x.value = R.tlx;
		R.fields.y.value = R.tly;
		R.fields.pct.value = Math.round((R.W / R.w0) * 100);
		R.keep.checked = R.lock;
		const unit = unitHeight();
		R.info.textContent = 'Now ' + R.w0 + '×' + R.h0 + ' px, new ' + R.W + '×' + R.H + ' px (' + Math.round((R.W / R.w0) * 100) + '% across, ' + Math.round((R.H / R.h0) * 100) + '% down).';
		R.unitInfo.textContent = unit ? 'The rest of the unit stands about ' + Math.round(unit) + ' px tall: this picture is ' + Math.round((R.H / unit) * 100) + '% of that (it was ' + Math.round((R.h0 / unit) * 100) + '%).' : '';
		R.apply.disabled = R.W === R.w0 && R.H === R.h0 && R.tlx === R.tl0x && R.tly === R.tl0y;
		a.view.resizePreview = R.preview;
		a.requestDraw();
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

	const find = (partId) => app().rig && app().rig.parts.find((p) => p.id === partId);

	async function open(part, key) {
		const a = app();
		if (a.needsCopy()) {
			a.copyThen(part, (copied) => open(copied, key));
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
		const w0 = frames[0].w;
		const h0 = frames[0].h;
		// Where the picture's top-left sits on the part now (an unset offset is centred).
		const tl0x = s.offset && (s.offset.x || s.offset.y) ? s.offset.x : -Math.floor(w0 / 2);
		const tl0y = s.offset && (s.offset.x || s.offset.y) ? s.offset.y : -Math.floor(h0 / 2);
		R = { partId: part.id, role: part.role, frames, w0, h0, W: w0, H: h0, tl0x, tl0y, tlx: tl0x, tly: tl0y, lock: true, cache: new Map(), fields: {}, gridWas: a.view.options.grid };
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
		// Typed sizes keep the picture's middle where it is.
		const resize = (W, H) => {
			const cx = R.tlx + R.W / 2;
			const cy = R.tly + R.H / 2;
			R.W = Math.max(1, Math.min(512, Math.round(W) || 1));
			R.H = Math.max(1, Math.min(512, Math.round(H) || 1));
			R.tlx = Math.round(cx - R.W / 2);
			R.tly = Math.round(cy - R.H / 2);
			refresh();
		};
		R.fields.w = num(1, 512, (v) => (R.lock ? resize(v, (R.H * v) / R.W) : resize(v, R.H)));
		R.fields.h = num(1, 512, (v) => (R.lock ? resize((R.W * v) / R.H, v) : resize(R.W, v)));
		R.fields.pct = num(5, 800, (v) => resize((R.w0 * v) / 100, (R.h0 * v) / 100));
		R.fields.x = num(-512, 512, (v) => {
			R.tlx = Math.round(v);
			refresh();
		});
		R.fields.y = num(-512, 512, (v) => {
			R.tly = Math.round(v);
			refresh();
		});
		R.keep = el('input', { type: 'checkbox', checked: true, onchange: (e) => { R.lock = e.target.checked; } });
		const after = el('input', { type: 'checkbox', checked: true });
		R.info = el('div', { className: 'rs-info' });
		R.unitInfo = el('div', { className: 'rs-info dim' });
		R.apply = el('button', { className: 'tb primary', textContent: 'Resize', onclick: () => apply(after.checked) });
		R.panel = el('div', { className: 'rs-panel' },
			el('div', { className: 'rs-title' }, el('b', { textContent: 'Transform ' + part.name }), el('span', { className: 'spacer' }), el('button', { className: 'ico-btn', textContent: '×', title: 'Cancel', onclick: close })),
			el('div', { className: 'rs-row' }, 'Size', R.fields.w, '×', R.fields.h, 'px', el('label', { className: 'ed-check', title: 'Corners and edges scale the picture evenly (hold Shift to switch it for one drag)' }, R.keep, 'Keep proportions')),
			el('div', { className: 'rs-row' }, 'Scale', R.fields.pct, '%'),
			el('div', { className: 'rs-row' }, 'Position', 'X', R.fields.x, 'Y', R.fields.y, el('small', { className: 'rs-hint', textContent: 'top-left, from the part\'s pivot' })),
			R.info, R.unitInfo,
			el('div', { className: 'rs-hint', textContent: 'Drag a corner or edge square to resize, drag inside to move. One grid square is one game pixel, in the same scale as the whole unit.' }),
			el('div', { className: 'rs-row' }, el('label', { className: 'ed-check' }, after, 'Then edit it'), el('span', { className: 'spacer' }), el('button', { className: 'tb', textContent: 'Cancel', onclick: close }), R.apply));
		document.getElementById('viewWrap').append(R.panel);
		R.preview = makePreview();
		a.select(part);
		refresh();
	}

	// Saves the resized frames as a copy in the project (or in place when the picture is already yours), points the part at it with the offset where the
	// box is, and opens the editor.
	async function apply(edit) {
		const a = app();
		const r = R;
		if (!r) {
			return;
		}
		const part = find(r.partId);
		if (!part) {
			a.toast('That part is no longer there.', 'bad');
			close();
			return;
		}
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
		const { tlx, tly, W, H, tl0x, tl0y, w0, h0, role, partId } = r;
		const nth = a.rig.parts.filter((p) => p.role === role).indexOf(part);
		close();
		try {
			await MS.dialogs.saveSprite(part, path, frames, false);
		} catch (e) {
			a.toast('Couldn\'t save the resized picture: ' + e.message, 'bad');
			return;
		}
		// The picture offset is where the box is. Left unset when that is just the centred default for its size.
		const resized = a.rig.parts.filter((p) => p.role === role)[Math.max(0, nth)] || find(partId);
		const centred = tlx === -Math.floor(W / 2) && tly === -Math.floor(H / 2);
		if (resized && (W !== w0 || H !== h0 || tlx !== tl0x || tly !== tl0y)) {
			if (centred) {
				a.edit('resize ' + resized.name, () => {
					const node = a.ownBlock(resized);
					const old = node && node.child('SpriteOffset');
					if (old) {
						old.remove();
					}
				});
			} else {
				a.setVector(resized, 'SpriteOffset', tlx, tly, 'resize ' + resized.name);
			}
		}
		a.toast('Resized to ' + W + '×' + H + ' and saved ' + path.split('/').pop() + '.', 'good');
		if (edit) {
			const again = a.rig.parts.filter((p) => p.role === role)[Math.max(0, nth)] || resized;
			MS.pixel.open({ part: again });
		}
	}

	MS.resize = { open, close, active: () => !!R };
})();
