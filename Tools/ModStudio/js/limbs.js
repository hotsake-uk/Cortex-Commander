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
			speed: sub.num('TravelSpeed', sub.num('NormalTravelSpeed', 1)),
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

	function along(loop, distance) {
		let d = distance % loop.length;
		for (let i = 0; i < loop.lengths.length; i++) {
			if (d <= loop.lengths[i] || i === loop.lengths.length - 1) {
				const t = loop.lengths[i] ? Math.min(d / loop.lengths[i], 1) : 0;
				const a = loop.points[i];
				const b = loop.points[i + 1];
				return { x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t };
			}
			d -= loop.lengths[i];
		}
		return loop.points[0];
	}

	// ---- the dialog ----

	function open() {
		const a = app();
		if (!a.rig || !['AHuman', 'ACrab'].includes(a.rig.cls)) {
			a.toast('Open a unit (a soldier or a crab) first: limb paths are what its feet follow.', 'bad');
			return;
		}
		const state = { key: 'WalkLimbPath', model: null, time: 0, playing: true, drag: null, scale: 7, raf: 0 };
		const body = () => a.rig.body;
		const reload = () => {
			state.model = readModel(body(), state.key);
		};
		reload();

		const canvas = el('canvas', { className: 'limb-canvas', width: 560, height: 360, title: 'Drag a point. The white dot is the foot, walking the path. The yellow dot is where it starts.' });
		const side = el('div', { className: 'limb-side' });
		const list = el('div', { className: 'limb-kinds' });
		const play = el('button', { className: 'tb', textContent: '❚❚ Pause', onclick: () => { state.playing = !state.playing; play.textContent = state.playing ? '❚❚ Pause' : '▶ Play'; } });
		const wrap = el('div', { className: 'limb-dialog' }, list, el('div', { className: 'limb-main' }, canvas, el('div', { className: 'limb-tools' }, play, el('span', { className: 'ed-note', textContent: 'Positions are in pixels from the leg\'s hip. Down is positive.' }))), side);

		// The leg's hip, to draw the body's side of things (where the ground is).
		const hip = () => {
			const leg = a.rig.byRole.fgleg || a.rig.byRole.bgleg;
			return leg ? leg.ent.vec('ParentOffset', { x: 0, y: 0 }) : { x: 0, y: 0 };
		};
		const view = () => ({ ox: canvas.width / 2, oy: 70, s: state.scale });
		const toScreen = (p) => ({ x: view().ox + p.x * state.scale, y: view().oy + p.y * state.scale });
		const toLocal = (x, y) => ({ x: (x - view().ox) / state.scale, y: (y - view().oy) / state.scale });

		const draw = () => {
			const ctx = canvas.getContext('2d');
			ctx.fillStyle = '#0d1117';
			ctx.fillRect(0, 0, canvas.width, canvas.height);
			// grid every 5 px, and the hip
			ctx.strokeStyle = 'rgba(255,255,255,.05)';
			ctx.lineWidth = 1;
			for (let x = -40; x <= 40; x += 5) {
				const s = toScreen({ x, y: 0 });
				ctx.beginPath();
				ctx.moveTo(s.x, 0);
				ctx.lineTo(s.x, canvas.height);
				ctx.stroke();
			}
			for (let y = -10; y <= 45; y += 5) {
				const s = toScreen({ x: 0, y });
				ctx.beginPath();
				ctx.moveTo(0, s.y);
				ctx.lineTo(canvas.width, s.y);
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
			ctx.lineWidth = 2;
			ctx.strokeStyle = '#f2b63d';
			ctx.beginPath();
			loop.points.forEach((p, i) => {
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
			// the points
			loop.points.slice(0, -1).forEach((p, i) => {
				const s = toScreen(p);
				ctx.fillStyle = i === 0 ? '#f2b63d' : i <= m.startSegCount ? '#9bc53d' : '#fff';
				ctx.strokeStyle = 'rgba(0,0,0,.7)';
				ctx.beginPath();
				ctx.arc(s.x, s.y, i === 0 ? 6 : 5, 0, Math.PI * 2);
				ctx.stroke();
				ctx.fill();
				ctx.fillStyle = 'rgba(255,255,255,.6)';
				ctx.fillText(String(i), s.x + 8, s.y - 6);
			});
			// the foot
			const foot = along(loop, state.time * m.speed * PPM);
			const f = toScreen(foot);
			ctx.fillStyle = '#fff';
			ctx.beginPath();
			ctx.arc(f.x, f.y, 4, 0, Math.PI * 2);
			ctx.fill();
		};

		const fields = () => {
			const m = state.model;
			side.replaceChildren();
			if (!m) {
				side.append(el('p', { className: 'ed-note', textContent: 'There is no ' + state.key + ' on this unit.' }),
					el('button', { className: 'tb primary', textContent: 'Add one', onclick: () => {
						writeModel(body(), { key: state.key, start: { x: 0, y: 12 }, segments: [{ x: 0, y: 0 }], startSegCount: 0, speed: 1, sub: null });
						reload();
						refresh();
					} }));
				return;
			}
			const num = (value, onChange, step) => {
				const input = el('input', { type: 'number', step: step || 0.5, className: 'ed-input num', value });
				input.onchange = () => {
					onChange(Number(input.value) || 0);
					commit();
				};
				return input;
			};
			side.append(
				m.shared ? el('p', { className: 'ed-note', textContent: 'It copies a shared path. Changing it writes a full copy into this unit; the shared one is left alone.' }) : null,
				el('div', { className: 'ed-label', textContent: 'Start point' }),
				el('div', { className: 'ed-row' }, 'x', num(m.start.x, (v) => { m.start.x = v; }), 'y', num(m.start.y, (v) => { m.start.y = v; })),
				el('div', { className: 'ed-label', textContent: 'Speed and standing' }),
				el('div', { className: 'ed-row' }, el('label', { className: 'ed-check', title: 'How fast the foot travels the loop (metres a second)' }, 'Speed', num(m.speed, (v) => { m.speed = Math.max(0, v); }, 0.1)),
					el('label', { className: 'ed-check', title: 'How many of the first segments the foot has already done when standing still' }, 'Standing segments', num(m.startSegCount, (v) => { m.startSegCount = Math.max(0, Math.min(m.segments.length, Math.round(v))); }, 1))),
				el('div', { className: 'ed-label', textContent: 'Steps (' + m.segments.length + ')' }),
				...m.segments.map((s, i) => el('div', { className: 'ed-row' }, el('span', { className: 'limb-i', textContent: String(i + 1) }), 'x', num(s.x, (v) => { s.x = v; }), 'y', num(s.y, (v) => { s.y = v; }),
					el('button', { className: 'ico-btn', textContent: '×', title: 'Take this step out', onclick: () => { m.segments.splice(i, 1); m.startSegCount = Math.min(m.startSegCount, m.segments.length); commit(); } }))),
				el('div', { className: 'ed-row' }, el('button', { className: 'tb', textContent: '+ Add a step', onclick: () => { const lastSeg = m.segments[m.segments.length - 1] || { x: 4, y: 0 }; m.segments.push({ x: lastSeg.x, y: 0 }); commit(); } })));
		};

		const commit = () => {
			writeModel(body(), state.model);
			reload();
			refresh();
		};

		const kinds = () => {
			// the walk paths the target game reads (6.2.2 has no running path, only Commander walks crouched)
			list.replaceChildren(...KINDS.filter(([key]) => !MS.target.commanderOnly(body().ent.cls, key)).map(([key, label, help]) => {
				const present = !!body().ent.sub(key);
				return el('button', { className: 'limb-kind' + (state.key === key ? ' sel' : '') + (present ? '' : ' none'), title: help, onclick: () => { state.key = key; state.time = 0; reload(); refresh(); } },
					el('span', { textContent: label }), el('small', { textContent: present ? readModel(body(), key).segments.length + ' steps' : 'not set' }));
			}));
		};

		const refresh = () => {
			kinds();
			fields();
			draw();
		};

		// dragging a point
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
				return;
			}
			canvas.setPointerCapture(e.pointerId);
			state.drag = { at, scale, rect: r, points: loop.points.map((p) => ({ x: p.x, y: p.y })) };
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
			if (state.drag) {
				state.drag = null;
				commit();
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
		state.raf = requestAnimationFrame(tick);
		refresh();
		MS.dialogs.open('Limb paths · ' + (a.actor ? a.actor.name : 'unit'), wrap, [{ text: 'Done', primary: true }], true);
	}

	MS.limbs = { open, readModel, loopOf };
})();
