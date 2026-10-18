// Cortex Mod Studio: the Limb paths editor. A walking unit's feet follow paths (StandLimbPath, WalkLimbPath, RunLimbPath, CrouchLimbPath...):
// a start point and a list of segments, each a step on from the one before. The numbers are hard to picture, so this draws the path and
// lets the points be dragged, with a foot walking along it at the path's speed. Positions are measured from the leg's hip (its joint).
//   Pick a path on the left, drag its points, or type the numbers; Play walks a foot round it.
// A path that copies a shared one (CopyOf = Human Walk Path) is written out in full in this unit before it is changed, so the shared one,
// and the other units using it, stay as they were.
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
	const PPM = 20;
	const KINDS = [
		['StandLimbPath', 'Standing', 'Where the front foot rests'],
		['StandLimbPathBG', 'Standing (back foot)', 'Where the back foot rests, when it differs'],
		['WalkLimbPath', 'Walking', 'The loop the feet follow when walking'],
		['RunLimbPath', 'Running', 'The loop when running'],
		['CrouchLimbPath', 'Crouching', 'Where the front foot rests crouched'],
		['CrouchLimbPathBG', 'Crouching (back foot)', 'The back foot, crouched'],
		['CrouchWalkLimbPath', 'Crouch walking', 'The loop when walking crouched'],
		['CrawlLimbPath', 'Crawling', 'How the limbs move when crawling'],
		['ClimbLimbPath', 'Climbing', 'How the limbs move on a ladder'],
		['JumpLimbPath', 'Jumping', 'Where the feet hang in the air'],
	];
	const SKIP = new Set(['StartOffset', 'StartSegCount', 'AddSegment', 'CopyOf', 'PresetName', 'TravelSpeed', 'NormalTravelSpeed']);

	// The path as numbers, from the unit as it is now (null if it has none).
	function readModel(part, key) {
		const sub = part.ent.sub(key);
		if (!sub) {
			return null;
		}
		const start = sub.vec('StartOffset', { x: 0, y: 0 });
		return {
			key,
			start: { x: start.x, y: start.y },
			segments: sub.all('AddSegment').map((e) => MS.readVector(e.node)).map((v) => ({ x: v.x, y: v.y })),
			startSegCount: Math.round(sub.num('StartSegCount', 0)),
			speed: MS.target.get().legacyLimbs ? sub.num('NormalTravelSpeed', 1) : sub.num('TravelSpeed', sub.num('NormalTravelSpeed', 1)),
			shared: !!(part.ent.sourceOf(key) && part.ent.sourceOf(key).child('CopyOf')),
			sub,
		};
	}

	// Writes the whole path into the unit's own block, replacing what was there (and flattening a copy of a shared path).
	function writeModel(part, model) {
		const a = app();
		a.edit('change ' + part.name + '\'s ' + model.key.replace(/LimbPath/, ' path').toLowerCase(), () => {
			const own = a.ownBlock(part);
			if (!own) {
				return;
			}
			const old = own.child(model.key);
			let keep = [];
			if (model.sub) {
				keep = model.sub.entries.filter((e) => !SKIP.has(e.key)).map((e) => e.node.clone());
			}
			if (old) {
				old.remove();
			}
			const node = own.add(model.key, 'LimbPath');
			node.setVector('StartOffset', model.start.x, model.start.y);
			node.add('StartSegCount', String(model.startSegCount));
			for (const s of model.segments) {
				const seg = node.add('AddSegment', 'Vector');
				seg.add('X', MS.formatNumber(s.x));
				seg.add('Y', MS.formatNumber(s.y));
			}
			// 6.2.2's paths take NormalTravelSpeed (with Slow and Fast); later games take TravelSpeed
			node.add(MS.target.commanderOnly('LimbPath', 'TravelSpeed') ? 'NormalTravelSpeed' : 'TravelSpeed', MS.formatNumber(model.speed));
			for (const k of keep) {
				const n = node.add(k.key, k.value);
				const copy = (src, dst) => {
					for (const c of src.children) {
						copy(c, dst.add(c.key, c.value));
					}
				};
				copy(k, n);
			}
		});
	}

	// The feet's loop: along every segment, then back to the start.
	function loopOf(model) {
		const points = [{ x: model.start.x, y: model.start.y }];
		for (const s of model.segments) {
			const p = points[points.length - 1];
			points.push({ x: p.x + s.x, y: p.y + s.y });
		}
		points.push({ x: model.start.x, y: model.start.y });
		const lengths = [];
		let total = 0;
		for (let i = 1; i < points.length; i++) {
			const l = Math.hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y);
			lengths.push(l);
			total += l;
		}
		return { points, lengths, length: Math.max(total, 1) };
	}

	// Where the foot is, distance along the loop, and which leg of it it is on (leg i runs from point i to point i + 1).
	function along(loop, distance) {
		let d = distance % loop.length;
		for (let i = 0; i < loop.lengths.length; i++) {
			if (d <= loop.lengths[i] || i === loop.lengths.length - 1) {
				const t = loop.lengths[i] ? Math.min(d / loop.lengths[i], 1) : 0;
				const a = loop.points[i];
				const b = loop.points[i + 1];
				return { x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t, leg: i };
			}
			d -= loop.lengths[i];
		}
		return { x: loop.points[0].x, y: loop.points[0].y, leg: 0 };
	}

	// ---- the dialog ----
	// The paths down the left, the picture in the middle with the path's own settings beside it, and its steps underneath, one column each
	// (Step, X, Y). The step the foot is on lights up as it walks; a picked step (its column, or its point in the picture) is outlined in
	// both. A step comes out with its × or, picked, with Delete.

	function open() {
		const a = app();
		if (!a.rig || !['AHuman', 'ACrab'].includes(a.rig.cls)) {
			a.toast('Open a unit (a soldier or a crab) first: limb paths are what its feet follow.', 'bad');
			return;
		}
		const state = { key: 'WalkLimbPath', model: null, time: 0, playing: true, drag: null, view: null, sel: -1, on: -1, raf: 0 };
		const body = () => a.rig.body;
		const reload = () => {
			state.model = readModel(body(), state.key);
		};
		reload();

		const canvas = el('canvas', { className: 'limb-canvas', width: 620, height: 340, title: 'Drag a point, or click one to pick its step. The white dot is the foot, walking the path; the yellow dot is where it starts.' });
		const settings = el('div', { className: 'limb-settings' });
		const steps = el('div', { className: 'limb-steps' });
		const list = el('div', { className: 'limb-kinds' });
		const play = el('button', { className: 'tb', textContent: '❚❚ Pause', onclick: () => { state.playing = !state.playing; play.textContent = state.playing ? '❚❚ Pause' : '▶ Play'; } });
		const wrap = el('div', { className: 'limb-dialog' },
			list,
			el('div', { className: 'limb-main' }, canvas, el('div', { className: 'limb-tools' }, play, el('span', { className: 'ed-note', textContent: 'Positions are in pixels from the leg\'s hip. Down is positive.' }))),
			settings,
			steps);

		// Fits the picture to the path and the hip, with a margin. Kept while a point is dragged, so the picture doesn't move under the mouse.
		const fit = () => {
			const m = state.model;
			const pts = m ? loopOf(m).points.concat([{ x: 0, y: 0 }]) : [{ x: -10, y: -5 }, { x: 10, y: 30 }];
			const xs = pts.map((p) => p.x);
			const ys = pts.map((p) => p.y);
			const minX = Math.min(...xs) - 4;
			const maxX = Math.max(...xs) + 4;
			const minY = Math.min(...ys) - 4;
			const maxY = Math.max(...ys) + 4;
			const s = Math.max(2, Math.min(12, (canvas.width - 60) / Math.max(1, maxX - minX), (canvas.height - 50) / Math.max(1, maxY - minY)));
			state.view = { s, ox: canvas.width / 2 - ((minX + maxX) / 2) * s, oy: canvas.height / 2 - ((minY + maxY) / 2) * s };
		};
		const toScreen = (p) => ({ x: state.view.ox + p.x * state.view.s, y: state.view.oy + p.y * state.view.s });
		const toLocal = (x, y) => ({ x: (x - state.view.ox) / state.view.s, y: (y - state.view.oy) / state.view.s });

		const draw = () => {
			const ctx = canvas.getContext('2d');
			ctx.fillStyle = '#0d1117';
			ctx.fillRect(0, 0, canvas.width, canvas.height);
			// grid every 5 px, across what's in view, and the hip
			const tl = toLocal(0, 0);
			const br = toLocal(canvas.width, canvas.height);
			ctx.strokeStyle = 'rgba(255,255,255,.05)';
			ctx.lineWidth = 1;
			for (let x = Math.ceil(tl.x / 5) * 5; x <= br.x; x += 5) {
				const s = toScreen({ x, y: 0 });
				ctx.beginPath();
				ctx.moveTo(Math.round(s.x) + 0.5, 0);
				ctx.lineTo(Math.round(s.x) + 0.5, canvas.height);
				ctx.stroke();
			}
			for (let y = Math.ceil(tl.y / 5) * 5; y <= br.y; y += 5) {
				const s = toScreen({ x: 0, y });
				ctx.beginPath();
				ctx.moveTo(0, Math.round(s.y) + 0.5);
				ctx.lineTo(canvas.width, Math.round(s.y) + 0.5);
				ctx.stroke();
			}
			const h = toScreen({ x: 0, y: 0 });
			ctx.strokeStyle = '#5bc0eb';
			ctx.beginPath();
			ctx.moveTo(h.x - 8, h.y);
			ctx.lineTo(h.x + 8, h.y);
			ctx.moveTo(h.x, h.y - 8);
			ctx.lineTo(h.x, h.y + 8);
			ctx.stroke();
			ctx.fillStyle = '#5bc0eb';
			ctx.font = '11px sans-serif';
			ctx.fillText('hip', h.x + 10, h.y - 4);
			const m = state.model;
			if (!m) {
				ctx.fillStyle = 'rgba(255,255,255,.5)';
				ctx.fillText('This unit has no ' + state.key + '. Add one on the right.', 20, 40);
				return;
			}
			const loop = loopOf(m);
			const foot = along(loop, state.time * m.speed * PPM);
			// the step the foot is on lights up in its column (none on the way back to the start)
			const on = foot.leg < m.segments.length ? foot.leg : -1;
			if (on !== state.on) {
				state.on = on;
				steps.querySelectorAll('.limb-step[data-i]').forEach((c) => c.classList.toggle('on', Number(c.dataset.i) === on));
			}
			ctx.lineWidth = 2;
			ctx.strokeStyle = '#f2b63d';
			ctx.beginPath();
			loop.points.slice(0, -1).forEach((p, i) => {
				const s = toScreen(p);
				i ? ctx.lineTo(s.x, s.y) : ctx.moveTo(s.x, s.y);
			});
			ctx.stroke();
			// the return leg of the loop is the dashed line back to the start
			ctx.setLineDash([4, 4]);
			ctx.strokeStyle = 'rgba(242,182,61,.45)';
			ctx.beginPath();
			const last = toScreen(loop.points[loop.points.length - 2]);
			const first = toScreen(loop.points[0]);
			ctx.moveTo(last.x, last.y);
			ctx.lineTo(first.x, first.y);
			ctx.stroke();
			ctx.setLineDash([]);
			// the picked step, and the one the foot is on: its line, drawn over
			const legLine = (i, color, width) => {
				const p = toScreen(loop.points[i]);
				const q = toScreen(loop.points[i + 1]);
				ctx.strokeStyle = color;
				ctx.lineWidth = width;
				ctx.beginPath();
				ctx.moveTo(p.x, p.y);
				ctx.lineTo(q.x, q.y);
				ctx.stroke();
			};
			if (on >= 0) {
				legLine(on, 'rgba(79,195,122,.9)', 3);
			}
			if (state.sel >= 0 && state.sel < m.segments.length) {
				legLine(state.sel, '#7fd1ff', 4);
			}
			// the points: point i is where step i ends
			loop.points.slice(0, -1).forEach((p, i) => {
				const s = toScreen(p);
				ctx.fillStyle = i === 0 ? '#f2b63d' : i <= m.startSegCount ? '#9bc53d' : '#fff';
				ctx.strokeStyle = 'rgba(0,0,0,.7)';
				ctx.lineWidth = 2;
				ctx.beginPath();
				ctx.arc(s.x, s.y, i === 0 ? 6 : 5, 0, Math.PI * 2);
				ctx.stroke();
				ctx.fill();
				if (i > 0 && i - 1 === state.sel) {
					ctx.strokeStyle = '#7fd1ff';
					ctx.beginPath();
					ctx.arc(s.x, s.y, 9, 0, Math.PI * 2);
					ctx.stroke();
				}
				ctx.fillStyle = 'rgba(255,255,255,.6)';
				ctx.fillText(String(i), s.x + 8, s.y - 6);
			});
			const f = toScreen(foot);
			ctx.fillStyle = '#fff';
			ctx.beginPath();
			ctx.arc(f.x, f.y, 4, 0, Math.PI * 2);
			ctx.fill();
		};

		// Picks a step (-1: none), outlining its column and its line.
		const pick = (i, scroll) => {
			state.sel = i;
			steps.querySelectorAll('.limb-step[data-i]').forEach((c) => {
				const yes = Number(c.dataset.i) === i;
				c.classList.toggle('sel', yes);
				if (yes && scroll) {
					c.scrollIntoView({ block: 'nearest', inline: 'nearest' });
				}
			});
			draw();
		};

		// A number box. Its change goes into the path as it is then (set gets the current model), written straight away; the boxes aren't
		// rebuilt for it, so Tab goes on to the next one.
		const num = (value, set, opts) => {
			const input = el('input', { type: 'number', step: (opts && opts.step) || 0.5, className: 'ed-input num' + (opts && opts.cls ? ' ' + opts.cls : ''), value });
			input.onchange = () => {
				if (!state.model) {
					return;
				}
				set(state.model, Number(input.value) || 0);
				commit(false);
			};
			if (opts && opts.onfocus) {
				input.onfocus = opts.onfocus;
			}
			return input;
		};
		const field = (label, input, title) => el('label', { className: 'limb-field', title: title || '' }, el('span', { textContent: label }), input);

		const fields = () => {
			const m = state.model;
			settings.replaceChildren();
			steps.replaceChildren();
			if (!m) {
				settings.append(el('p', { className: 'ed-note', textContent: 'There is no ' + state.key + ' on this unit.' }),
					el('button', { className: 'tb primary', textContent: 'Add one', onclick: () => {
						writeModel(body(), { key: state.key, start: { x: 0, y: 12 }, segments: [{ x: 0, y: 0 }], startSegCount: 0, speed: 1, sub: null });
						reload();
						refresh(true);
					} }));
				return;
			}
			settings.append(
				el('div', { className: 'ed-label', textContent: 'Start point' }),
				el('div', { className: 'limb-pair' },
					field('X', num(m.start.x, (mm, v) => { mm.start.x = v; })),
					field('Y', num(m.start.y, (mm, v) => { mm.start.y = v; }))),
				el('div', { className: 'ed-label', textContent: 'Speed and standing' }),
				el('div', { className: 'limb-pair' },
					field('Speed', num(m.speed, (mm, v) => { mm.speed = Math.max(0, v); }, { step: 0.1 }), 'How fast the foot travels the loop (metres a second)'),
					field('Standing', num(m.startSegCount, (mm, v) => { mm.startSegCount = Math.max(0, Math.min(mm.segments.length, Math.round(v))); }, { step: 1 }), 'How many of the first steps the foot has already done when standing still (their points are green)')),
				...(m.shared ? [el('p', { className: 'ed-note limb-shared', textContent: 'This path copies a shared one. Changing it writes a full copy into this unit; the shared one is left alone.' })] : []));
			// the steps: a column each, after one naming the rows
			const del = (i) => el('button', { className: 'limb-del', textContent: '×', title: 'Take this step out (or pick it and press Delete)', onclick: (e) => {
				e.stopPropagation();
				removeStep(i);
			} });
			const add = el('button', { className: 'limb-add', textContent: '+', title: 'Add a step at the end', onclick: () => {
				const lastSeg = m.segments[m.segments.length - 1] || { x: 4, y: 0 };
				m.segments.push({ x: lastSeg.x, y: 0 });
				state.sel = m.segments.length - 1;
				commit(true);
				pick(state.sel);
			} });
			steps.append(el('div', { className: 'limb-step head' }, add, el('span', { textContent: 'X' }), el('span', { textContent: 'Y' })), el('div', { className: 'limb-cols' },
				...m.segments.map((s, i) => {
					const col = el('div', { className: 'limb-step' + (i === state.sel ? ' sel' : '') + (i === state.on ? ' on' : '') + (i < m.startSegCount ? ' standing' : ''), title: i < m.startSegCount ? 'Done already when standing still' : '' },
						el('div', { className: 'limb-step-n' }, el('span', { textContent: String(i + 1) }), del(i)),
						num(s.x, (mm, v) => { mm.segments[i].x = v; }, { onfocus: () => pick(i) }),
						num(s.y, (mm, v) => { mm.segments[i].y = v; }, { onfocus: () => pick(i) }));
					col.dataset.i = String(i);
					col.onclick = (e) => {
						pick(i);
						if (!(e.target instanceof HTMLInputElement)) {
							wrap.focus({ preventScroll: true });
						}
					};
					return col;
				})));
		};

		// Takes step i out; the points after it move back by it.
		const removeStep = (i) => {
			const m = state.model;
			if (!m || i < 0 || i >= m.segments.length) {
				return;
			}
			m.segments.splice(i, 1);
			m.startSegCount = Math.min(m.startSegCount, m.segments.length);
			state.sel = -1;
			commit(true);
		};
		// Delete (or Backspace) takes out the picked step, when not typing in a box
		wrap.tabIndex = -1;
		wrap.addEventListener('keydown', (e) => {
			if ((e.key === 'Delete' || e.key === 'Backspace') && state.sel >= 0 && !(e.target instanceof HTMLInputElement)) {
				e.preventDefault();
				removeStep(state.sel);
			}
		});

		// Writes the path into the unit. rebuild: the steps changed in number, so the boxes are made again.
		const commit = (rebuild) => {
			writeModel(body(), state.model);
			reload();
			kinds();
			if (rebuild) {
				fields();
			}
			if (!state.drag) {
				fit();
			}
			draw();
		};

		const kinds = () => {
			// the walk paths the target game reads (6.2.2 has no running path, only Commander walks crouched)
			list.replaceChildren(...KINDS.filter(([key]) => !MS.target.commanderOnly(body().ent.cls, key)).map(([key, label, help]) => {
				const present = !!body().ent.sub(key);
				return el('button', { className: 'limb-kind' + (state.key === key ? ' sel' : '') + (present ? '' : ' none'), title: help, onclick: () => { state.key = key; state.time = 0; state.sel = -1; state.on = -1; reload(); refresh(); } },
					el('span', { textContent: label }), el('small', { textContent: present ? readModel(body(), key).segments.length + ' steps' : 'not set' }));
			}));
		};

		const refresh = () => {
			kinds();
			fields();
			fit();
			draw();
		};

		// dragging a point, or clicking one to pick its step
		canvas.onpointerdown = (e) => {
			const m = state.model;
			if (!m) {
				return;
			}
			const r = canvas.getBoundingClientRect();
			const scale = canvas.width / r.width;
			const px = (e.clientX - r.left) * scale;
			const py = (e.clientY - r.top) * scale;
			const loop = loopOf(m);
			const at = loop.points.slice(0, -1).findIndex((p) => Math.hypot(toScreen(p).x - px, toScreen(p).y - py) < 10);
			if (at < 0) {
				pick(-1);
				return;
			}
			pick(at - 1, true);
			wrap.focus({ preventScroll: true });
			canvas.setPointerCapture(e.pointerId);
			state.drag = { at, scale, rect: r, points: loop.points.map((p) => ({ x: p.x, y: p.y })), moved: false };
		};
		canvas.onpointermove = (e) => {
			const d = state.drag;
			const m = state.model;
			if (!d || !m) {
				return;
			}
			const px = (e.clientX - d.rect.left) * d.scale;
			const py = (e.clientY - d.rect.top) * d.scale;
			const p = toLocal(px, py);
			const snap = (v) => Math.round(v * 2) / 2;
			const target = { x: snap(p.x), y: snap(p.y) };
			d.moved = true;
			if (d.at === 0) {
				// moving the start moves the whole loop: the steps stay as they are
				m.start = target;
			} else {
				// a point is where the steps before it end: change the step that arrives there, and the one that leaves it so the rest stays put
				const pts = d.points;
				m.segments[d.at - 1] = { x: snap(target.x - pts[d.at - 1].x), y: snap(target.y - pts[d.at - 1].y) };
				if (d.at < m.segments.length) {
					m.segments[d.at] = { x: snap(pts[d.at + 1].x - target.x), y: snap(pts[d.at + 1].y - target.y) };
				}
			}
			draw();
		};
		canvas.onpointerup = () => {
			const d = state.drag;
			if (d) {
				state.drag = null;
				if (d.moved) {
					commit(true);
				}
			}
		};

		const tick = (now) => {
			if (!canvas.isConnected) {
				return;
			}
			if (state.playing && !state.drag) {
				state.time = now / 1000;
				draw();
			}
			state.raf = requestAnimationFrame(tick);
		};
		refresh();
		MS.dialogs.open('Limb paths · ' + (a.actor ? a.actor.name : 'unit'), wrap, [{ text: 'Done', primary: true }], true);
		const card = wrap.closest('.modal-card');
		if (card) {
			card.classList.add('limb-card');
		}
		// the picture's pixels follow its size on screen, so it stays sharp however big the dialog is
		const sizeCanvas = () => {
			const w = Math.round(canvas.clientWidth);
			const h = Math.round(canvas.clientHeight);
			if (w > 0 && h > 0 && (w !== canvas.width || h !== canvas.height) && !state.drag) {
				canvas.width = w;
				canvas.height = h;
				fit();
				draw();
			}
		};
		new ResizeObserver(sizeCanvas).observe(canvas);
		sizeCanvas();
		state.raf = requestAnimationFrame(tick);
	}

	MS.limbs = { open, readModel, loopOf };
})();
