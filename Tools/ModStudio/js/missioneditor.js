// Cortex Mod Studio: the Mission Editor. One tab for a scenario: its scene's map on the left, where the areas that triggers and objectives use are drawn,
// moved, resized, named and deleted, and the mission's logic on the right. Opened from Missions in the top bar (or a scenario's own page).
//   The map    the scene's terrain, zoomed (wheel) and moved (right or middle drag, or Space and drag). Drag on empty ground to draw an area;
//              Shift and drag adds another box to the area picked. Drag a box to move it, its squares to resize it, Delete takes the box out.
//              Right-click an area to rename or delete it, or to make it an objective or a trigger.
//   Areas      are the scene's own (AddArea in its file), the same the game uses. A scene of the game's own, or of someone else's mod, is copied into your
//              mod the first time you change it (a scene that copies the original, with your areas over it), and the scenario is pointed at the copy.
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
	const COLORS = ['#f2b63d', '#5bc0eb', '#9bc53d', '#e55934', '#c77dff', '#fa7921', '#2ec4b6', '#ff6b9a'];
	const colorOf = (name, all) => COLORS[Math.max(0, all.indexOf(name)) % COLORS.length];

	// ---- the scenario, its scene and the scene's areas ----

	const keyOf = (p) => p.doc.path + '::' + p.name;
	const scenarioByKey = (key) => app().lib.presets.find((p) => p.topLevel && p.node.key === 'AddActivity' && keyOf(p) === key) || null;

	function sceneOf(scenario) {
		const a = app();
		const name = a.lib.resolve(scenario.node).str('SceneName', '');
		if (!name) {
			return null;
		}
		const matches = a.lib.presets.filter((p) => p.topLevel && p.cls === 'Scene' && p.name === name);
		// a copy in the scenario's own mod wins over the original
		return matches.find((p) => p.module === scenario.module) || matches[matches.length - 1] || null;
	}

	// The scene's areas as the game ends up with them: by name, the last one written wins (an AddArea of a name the scene copies replaces it).
	function areasOf(scene) {
		if (!scene) {
			return [];
		}
		const a = app();
		const ent = a.lib.resolve(scene.node);
		const byName = new Map();
		for (const entry of ent.all('AddArea')) {
			const name = entry.node.childValue('Name', '');
			const boxes = entry.node.children.filter((c) => c.key === 'AddBox').map((b) => {
				const corner = b.vector('Corner') || { x: 0, y: 0 };
				return { x: corner.x, y: corner.y, w: Number(b.childValue('Width', '0')) || 0, h: Number(b.childValue('Height', '0')) || 0 };
			});
			byName.set(name, { name, boxes, node: entry.node, own: entry.node.isWithin(scene.node) });
		}
		return [...byName.values()].filter((r) => r.boxes.length);
	}

	// ---- changing areas ----

	const locked = (p) => !p || app().lib.isProtected(p.doc);

	// The scene to change: this one if it is yours; otherwise, once you say yes, a copy of it in the scenario's mod that the scenario then plays on.
	async function editableScene(scenario) {
		const a = app();
		const scene = sceneOf(scenario);
		if (!scene) {
			a.toast('Pick the scene the scenario plays on first.', 'bad');
			return null;
		}
		if (!locked(scene)) {
			return scene;
		}
		if (locked(scenario)) {
			a.toast('This scenario is not yours to change. Copy it into your mod first (Scenarios, + New scenario, as a copy of this one).', 'bad');
			return null;
		}
		const go = await new Promise((resolve) => {
			MS.dialogs.open('Copy ' + scene.name + ' into ' + scenario.module + '?', el('p', { textContent: scene.name + ' belongs to ' + scene.module + ', so it cannot be changed. Studio can make a copy of it in ' + scenario.module + ' (the same map, with your areas over it) and have ' + scenario.name + ' play on the copy.' }), [
				{ text: 'Cancel', action: () => resolve(false) },
				{ text: 'Make the copy', primary: true, action: () => resolve(true) },
			]);
		});
		if (!go) {
			return null;
		}
		const name = scene.name + ' (' + scenario.name + ')';
		const path = scenario.module + '/Scenes/' + (name.replace(/[^A-Za-z0-9 _-]/g, '').trim().replace(/ +/g, '_') || 'MissionScene') + '.ini';
		const doc = a.lib.docFor(path) || (await a.lib.newDoc(path));
		a.hookDoc(doc);
		a.hookDoc(a.lib.docFor(scenario.module + '/Index.ini'));
		a.edit('Copy ' + scene.name + ' for ' + scenario.name, () => {
			const block = doc.addTopLevel('AddScene', 'Scene');
			block.add('CopyOf', a.lib.reference(scene.name, scene.module, scenario.module));
			block.add('PresetName', name);
			scenario.node.set('SceneName', name);
		});
		return a.lib.presets.find((p) => p.topLevel && p.cls === 'Scene' && p.name === name && p.module === scenario.module) || null;
	}

	// Writes an area's boxes (an empty list takes it out). An area the scene copies from the original is replaced by one of the same name in the scene.
	function writeArea(scene, name, boxes) {
		const a = app();
		const own = scene.node.children.filter((c) => c.key === 'AddArea' && c.childValue('Name', '') === name);
		const inherited = a.lib.resolve(scene.node).all('AddArea').some((e) => !e.node.isWithin(scene.node) && e.node.childValue('Name', '') === name);
		a.edit((boxes.length ? 'Change' : 'Take out') + ' the area ' + name, () => {
			for (const node of own.slice(0, -1)) {
				node.remove(); // older duplicates of the same name: the last one is what counts
			}
			let node = own[own.length - 1];
			if (!boxes.length && !inherited) {
				if (node) {
					node.remove();
				}
				return;
			}
			if (!node) {
				node = scene.node.add('AddArea', 'Area');
			}
			for (const c of node.children.filter((x) => x.key === 'AddBox' || x.key === 'Name')) {
				c.remove();
			}
			for (const b of boxes) {
				const box = node.add('AddBox', 'Box');
				box.setVector('Corner', Math.round(b.x), Math.round(b.y));
				box.add('Width', String(Math.max(1, Math.round(b.w))));
				box.add('Height', String(Math.max(1, Math.round(b.h))));
			}
			node.add('Name', name);
		});
	}

	// ---- the map ----

	// The map's layers, in the areas list's order: [id (an area, or a mark's kind), name], and each one's colour in the list.
	const MAP_LAYERS = [['areas', 'Areas'], ['line', 'Camera lines'], ['object', 'Objects placed'], ['screen', 'Screens'], ['terminal', 'Terminals']];
	const LAYER_COLOURS = { line: '#ff6b9a', object: '#c8a35a', screen: '#5bc0eb', terminal: '#9bc53d' };

	// A preset's picture for the map ("Name|Module"): a terrain object's foreground (or background), placed as the game places it (its
	// BitmapOffset, or centred when it is over 24 pixels), or a particle's first frame (its SpriteOffset, or centred). Read once; onLoad is
	// called when it comes in. Returns { img, offset } or null while it is being read (or if there is none).
	const pictures = new Map();
	function pictureOf(ref, kind, onLoad) {
		const k = kind + '|' + ref;
		if (pictures.has(k)) {
			const known = pictures.get(k);
			if (!known.img && known.waiting) {
				known.waiting.add(onLoad);
			}
			return known.img ? known : null;
		}
		const entry = { img: null, offset: { x: 0, y: 0 }, waiting: new Set([onLoad]) };
		pictures.set(k, entry);
		const a = app();
		const [name, module] = String(ref).split('|');
		const preset = a.lib && a.lib.find(name, kind === 'sprite' ? null : 'TerrainObject', module || null);
		if (!preset) {
			entry.waiting = null;
			return null;
		}
		const r = a.lib.resolve(preset.node);
		const file = kind === 'sprite' ? r.file('SpriteFile') : r.file('FGColorFile') || r.file('BGColorFile');
		if (!file) {
			entry.waiting = null;
			return null;
		}
		const path = kind === 'sprite' ? MS.Images.framePaths(file, Math.max(1, Math.round(r.num('FrameCount', 1))))[0] : file;
		a.images.get(path).then((img) => {
			if (img) {
				const given = r.last(kind === 'sprite' ? 'SpriteOffset' : 'BitmapOffset');
				entry.offset = given ? r.vec(kind === 'sprite' ? 'SpriteOffset' : 'BitmapOffset') : kind === 'sprite'
					? { x: -img.width / 2, y: -img.height / 2 }
					: { x: img.width > 24 ? -Math.floor(img.width / 2) : 0, y: img.height > 24 ? -Math.floor(img.height / 2) : 0 };
				entry.img = img;
			}
			const waiting = entry.waiting;
			entry.waiting = null;
			(waiting || []).forEach((fn) => fn && fn());
		}, () => {
			entry.waiting = null;
		});
		return null;
	}

	class MapView {
		constructor(state, hooks) {
			this.state = state; // kept on the tab: zoom, pan, picked area and box, tool
			this.hooks = hooks; // { areas(), scene(), terrain(), change(name, boxes), rename(name), remove(name), menu(area, x, y), usage(name) }
			this.canvas = el('canvas', { className: 'me-canvas', tabIndex: 0 });
			this.root = el('div', { className: 'me-map' }, this.canvas);
			this.drag = null;
			this.space = false;
			this.bind();
			// the first time it has a size, the whole map is shown; after that a resize keeps the view where it is
			new ResizeObserver(() => (this.fitted ? this.draw() : this.fit())).observe(this.root);
		}

		get zoom() {
			return this.state.zoom || 0.25;
		}

		toScreen(x, y) {
			return { x: (x - (this.state.panX || 0)) * this.zoom, y: (y - (this.state.panY || 0)) * this.zoom };
		}

		toWorld(sx, sy) {
			return { x: sx / this.zoom + (this.state.panX || 0), y: sy / this.zoom + (this.state.panY || 0) };
		}

		world() {
			const frame = this.hooks.terrain();
			return frame ? { w: frame.width, h: frame.height } : { w: 4000, h: 1500 };
		}

		fit() {
			const r = this.root.getBoundingClientRect();
			const w = this.world();
			if (!r.width || !r.height) {
				return;
			}
			this.fitted = true;
			this.state.zoom = Math.min(r.width / w.w, r.height / w.h) * 0.95;
			this.state.panX = -(r.width / this.state.zoom - w.w) / 2;
			this.state.panY = -(r.height / this.state.zoom - w.h) / 2;
			this.draw();
		}

		// The areas as drawn now: the stored ones, with a drag under way shown where it is.
		shown() {
			const areas = this.hooks.areas().map((r) => ({ name: r.name, boxes: r.boxes.map((b) => Object.assign({}, b)) }));
			const d = this.drag;
			if (d && d.preview) {
				const area = areas.find((r) => r.name === d.preview.name);
				if (area) {
					area.boxes = d.preview.boxes;
				} else {
					areas.push({ name: d.preview.name, boxes: d.preview.boxes, fresh: true });
				}
			}
			return areas;
		}

		pickedBox() {
			const s = this.state;
			const area = this.shown().find((r) => r.name === s.area);
			return area && area.boxes[s.box || 0] ? { area, box: area.boxes[s.box || 0] } : null;
		}

		handles(box) {
			const a = this.toScreen(box.x, box.y);
			const b = this.toScreen(box.x + box.w, box.y + box.h);
			const mx = (a.x + b.x) / 2;
			const my = (a.y + b.y) / 2;
			return [['nw', a.x, a.y], ['n', mx, a.y], ['ne', b.x, a.y], ['e', b.x, my], ['se', b.x, b.y], ['s', mx, b.y], ['sw', a.x, b.y], ['w', a.x, my]];
		}

		hit(sx, sy) {
			const p = this.toWorld(sx, sy);
			const areas = this.shown();
			for (let i = areas.length - 1; i >= 0; i--) {
				const boxes = areas[i].boxes;
				for (let j = boxes.length - 1; j >= 0; j--) {
					const b = boxes[j];
					if (p.x >= b.x && p.x <= b.x + b.w && p.y >= b.y && p.y <= b.y + b.h) {
						return { area: areas[i], index: j };
					}
				}
			}
			return null;
		}

		draw() {
			const c = this.canvas;
			const r = this.root.getBoundingClientRect();
			const dpr = window.devicePixelRatio || 1;
			if (c.width !== Math.round(r.width * dpr) || c.height !== Math.round(r.height * dpr)) {
				c.width = Math.round(r.width * dpr);
				c.height = Math.round(r.height * dpr);
				c.style.width = r.width + 'px';
				c.style.height = r.height + 'px';
			}
			const ctx = c.getContext('2d');
			ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
			ctx.fillStyle = '#0b0e14';
			ctx.fillRect(0, 0, r.width, r.height);
			const frame = this.hooks.terrain();
			const w = this.world();
			const o = this.toScreen(0, 0);
			ctx.fillStyle = '#141a24';
			ctx.fillRect(o.x, o.y, w.w * this.zoom, w.h * this.zoom);
			if (frame) {
				ctx.imageSmoothingEnabled = this.zoom < 1;
				ctx.drawImage(frame.canvas, o.x, o.y, w.w * this.zoom, w.h * this.zoom);
			}
			// what the mission puts on the map (objects placed, screens, terminals), as they will look, under the areas
			ctx.font = '12px system-ui, sans-serif';
			for (const m of this.marks().filter((x) => x.img)) {
				const a = this.toScreen(m.x, m.y);
				ctx.globalAlpha = m.picked ? 1 : 0.85;
				ctx.imageSmoothingEnabled = this.zoom < 1;
				ctx.drawImage(m.img.canvas, a.x, a.y, m.img.width * this.zoom, m.img.height * this.zoom);
				ctx.globalAlpha = 1;
				if (m.picked) {
					ctx.strokeStyle = '#fff';
					ctx.lineWidth = 2;
					ctx.strokeRect(a.x, a.y, m.img.width * this.zoom, m.img.height * this.zoom);
				}
			}
			const areas = this.shown();
			const names = areas.map((x) => x.name);
			const lit = new Set(this.state.lit || []);
			for (const area of areas) {
				const color = colorOf(area.name, names);
				const picked = area.name === this.state.area;
				area.boxes.forEach((b, i) => {
					const a = this.toScreen(b.x, b.y);
					const bw = b.w * this.zoom;
					const bh = b.h * this.zoom;
					ctx.fillStyle = color + (picked || lit.has(area.name) ? '44' : '26');
					ctx.fillRect(a.x, a.y, bw, bh);
					ctx.strokeStyle = color;
					ctx.lineWidth = picked && i === (this.state.box || 0) ? 2.5 : lit.has(area.name) ? 2 : 1.2;
					ctx.setLineDash(picked ? [] : [6, 3]);
					ctx.strokeRect(a.x + 0.5, a.y + 0.5, bw, bh);
					ctx.setLineDash([]);
					const uses = i === 0 ? this.hooks.usage(area.name) : 0;
					const label = area.name + (uses ? '  ⚑' + uses : '');
					const tw = i === 0 ? ctx.measureText(label).width : 0;
					// a name is written where it fits in its box, or for the area picked or lit: with many areas, the map stays readable
					if (i === 0 && (picked || lit.has(area.name) || tw + 10 <= bw)) {
						ctx.fillStyle = 'rgba(0,0,0,.65)';
						ctx.fillRect(a.x, a.y - 18, tw + 10, 17);
						ctx.fillStyle = color;
						ctx.fillText(label, a.x + 5, a.y - 5);
					}
				});
			}
			// the mission's camera lines, over everything: a dashed line down the map, with what moves it at the top of the view
			for (const m of this.marks().filter((x) => x.kind === 'line')) {
				const x = this.drag && this.drag.kind === 'line' && this.drag.id === m.id ? this.drag.x : m.x;
				const sx = Math.round(this.toScreen(x, 0).x) + 0.5;
				const top = Math.max(o.y, 0);
				const bottom = Math.min(o.y + w.h * this.zoom, r.height);
				ctx.strokeStyle = m.picked ? '#fff' : '#ff6b9a';
				ctx.lineWidth = m.picked ? 2.5 : 1.5;
				ctx.setLineDash([8, 5]);
				ctx.beginPath();
				ctx.moveTo(sx, top);
				ctx.lineTo(sx, bottom);
				ctx.stroke();
				ctx.setLineDash([]);
				const text = m.label + ' (x ' + Math.round(x) + ')';
				const tw = ctx.measureText(text).width;
				ctx.fillStyle = 'rgba(0,0,0,.7)';
				ctx.fillRect(sx + 3, top + 4, tw + 10, 17);
				ctx.fillStyle = '#ff6b9a';
				ctx.fillText(text, sx + 8, top + 17);
			}
			const p = this.pickedBox();
			if (p) {
				ctx.fillStyle = '#fff';
				for (const [, hx, hy] of this.handles(p.box)) {
					ctx.fillRect(hx - 4, hy - 4, 8, 8);
				}
			}
			if (this.drag && this.drag.kind === 'draw' && this.drag.preview && this.drag.adding) {
				ctx.fillStyle = '#fff';
				ctx.fillText('Adding a box to ' + this.drag.preview.name, 10, r.height - 10);
			}
		}

		// the mission's things to draw ([] when the map's "Lines and objects" is off)
		marks() {
			return this.state.marks === false || !this.hooks.marks ? [] : this.hooks.marks();
		}

		// a camera line near a point on the screen, or an object's picture under it
		markAt(sx, sy) {
			const marks = this.marks();
			const line = marks.find((m) => m.kind === 'line' && Math.abs(this.toScreen(m.x, 0).x - sx) < 6);
			if (line) {
				return line;
			}
			const p = this.toWorld(sx, sy);
			return marks.slice().reverse().find((m) => m.img && p.x >= m.x && p.x <= m.x + m.img.width && p.y >= m.y && p.y <= m.y + m.img.height) || null;
		}

		bind() {
			const c = this.canvas;
			c.addEventListener('wheel', (e) => {
				e.preventDefault();
				const before = this.toWorld(e.offsetX, e.offsetY);
				this.state.zoom = Math.max(0.02, Math.min(8, this.zoom * (e.deltaY < 0 ? 1.15 : 1 / 1.15)));
				this.state.panX = before.x - e.offsetX / this.zoom;
				this.state.panY = before.y - e.offsetY / this.zoom;
				this.draw();
			}, { passive: false });
			c.addEventListener('contextmenu', (e) => e.preventDefault());
			c.addEventListener('keydown', (e) => {
				if (e.key === ' ') {
					this.space = true;
					e.preventDefault();
				} else if ((e.key === 'Delete' || e.key === 'Backspace') && this.state.area) {
					const area = this.hooks.areas().find((r) => r.name === this.state.area);
					if (area) {
						const boxes = area.boxes.filter((_, i) => i !== (this.state.box || 0));
						this.hooks.change(area.name, boxes);
						this.state.box = 0;
						if (!boxes.length) {
							this.state.area = null;
						}
					}
					e.preventDefault();
				} else if (e.key === 'f' || e.key === 'F') {
					this.fit();
				}
			});
			c.addEventListener('keyup', (e) => {
				if (e.key === ' ') {
					this.space = false;
				}
			});
			c.addEventListener('pointerdown', (e) => {
				c.focus();
				c.setPointerCapture(e.pointerId);
				const at = { x: e.offsetX, y: e.offsetY };
				if (e.button === 1 || e.button === 2 && !this.hit(at.x, at.y) || (e.button === 0 && this.space)) {
					this.drag = { kind: 'pan', start: at, panX: this.state.panX || 0, panY: this.state.panY || 0, button: e.button, client: { x: e.clientX, y: e.clientY } };
					return;
				}
				// a camera line: dragged sideways (or picked); an object, screen or terminal: its node picked
				const mark = e.button === 0 ? this.markAt(at.x, at.y) : null;
				if (mark && mark.kind === 'line') {
					this.drag = { kind: 'line', id: mark.id, x: mark.x, from: mark.x, startWorld: this.toWorld(at.x, at.y) };
					return;
				}
				if (mark && !this.hit(at.x, at.y)) {
					this.hooks.pickMark && this.hooks.pickMark(mark);
					return;
				}
				if (e.button === 2) {
					const h = this.hit(at.x, at.y);
					this.state.area = h.area.name;
					this.state.box = h.index;
					this.draw();
					this.hooks.picked(h.area.name);
					this.hooks.menu(h.area, e.clientX, e.clientY);
					return;
				}
				const p = this.pickedBox();
				if (p) {
					const handle = this.handles(p.box).find(([, hx, hy]) => Math.abs(hx - at.x) < 7 && Math.abs(hy - at.y) < 7);
					if (handle) {
						this.drag = { kind: 'resize', handle: handle[0], name: p.area.name, index: this.state.box || 0, from: Object.assign({}, p.box), boxes: p.area.boxes.map((b) => Object.assign({}, b)), startWorld: this.toWorld(at.x, at.y) };
						return;
					}
				}
				const h = this.hit(at.x, at.y);
				if (h && !e.shiftKey) {
					this.state.area = h.area.name;
					this.state.box = h.index;
					this.hooks.picked(h.area.name);
					this.drag = { kind: 'move', name: h.area.name, index: h.index, boxes: h.area.boxes.map((b) => Object.assign({}, b)), startWorld: this.toWorld(at.x, at.y) };
					this.draw();
					return;
				}
				// drawing: a new area, or with Shift another box for the one picked
				const start = this.toWorld(at.x, at.y);
				const adding = e.shiftKey && this.state.area && this.hooks.areas().some((r) => r.name === this.state.area);
				const existing = adding ? this.hooks.areas().find((r) => r.name === this.state.area).boxes : [];
				const name = adding ? this.state.area : this.hooks.freshName();
				this.drag = { kind: 'draw', start, adding, name, existing };
			});
			c.addEventListener('pointermove', (e) => {
				const d = this.drag;
				if (!d) {
					const mark = this.markAt(e.offsetX, e.offsetY);
					if (mark) {
						c.style.cursor = mark.kind === 'line' ? 'ew-resize' : 'pointer';
						c.title = mark.label + (mark.kind === 'line' ? ': drag it sideways, or click for its node' : ': click for its node');
						return;
					}
					c.title = '';
					const p = this.pickedBox();
					const over = p && this.handles(p.box).find(([, hx, hy]) => Math.abs(hx - e.offsetX) < 7 && Math.abs(hy - e.offsetY) < 7);
					c.style.cursor = over ? ({ n: 'ns-resize', s: 'ns-resize', e: 'ew-resize', w: 'ew-resize', ne: 'nesw-resize', sw: 'nesw-resize', nw: 'nwse-resize', se: 'nwse-resize' })[over[0]] : this.hit(e.offsetX, e.offsetY) ? 'move' : 'crosshair';
					return;
				}
				const now = this.toWorld(e.offsetX, e.offsetY);
				const snap = (v) => Math.round(v);
				if (d.kind === 'pan') {
					this.state.panX = d.panX - (e.offsetX - d.start.x) / this.zoom;
					this.state.panY = d.panY - (e.offsetY - d.start.y) / this.zoom;
					d.moved = d.moved || Math.abs(e.offsetX - d.start.x) + Math.abs(e.offsetY - d.start.y) > 3;
				} else if (d.kind === 'line') {
					d.x = Math.max(0, Math.round(d.from + now.x - d.startWorld.x));
					d.moved = d.moved || d.x !== d.from;
				} else if (d.kind === 'move') {
					const dx = snap(now.x - d.startWorld.x);
					const dy = snap(now.y - d.startWorld.y);
					const boxes = d.boxes.map((b, i) => (i === d.index ? Object.assign({}, b, { x: b.x + dx, y: b.y + dy }) : b));
					d.preview = { name: d.name, boxes };
					d.moved = Math.abs(dx) + Math.abs(dy) > 0;
				} else if (d.kind === 'resize') {
					const b = Object.assign({}, d.from);
					const dx = snap(now.x - d.startWorld.x);
					const dy = snap(now.y - d.startWorld.y);
					if (d.handle.includes('w')) {
						b.x = Math.min(d.from.x + dx, d.from.x + d.from.w - 2);
						b.w = d.from.w - (b.x - d.from.x);
					}
					if (d.handle.includes('e')) {
						b.w = Math.max(2, d.from.w + dx);
					}
					if (d.handle.includes('n')) {
						b.y = Math.min(d.from.y + dy, d.from.y + d.from.h - 2);
						b.h = d.from.h - (b.y - d.from.y);
					}
					if (d.handle.includes('s')) {
						b.h = Math.max(2, d.from.h + dy);
					}
					d.preview = { name: d.name, boxes: d.boxes.map((x, i) => (i === d.index ? b : x)) };
					d.moved = true;
				} else if (d.kind === 'draw') {
					const box = { x: snap(Math.min(d.start.x, now.x)), y: snap(Math.min(d.start.y, now.y)), w: snap(Math.abs(now.x - d.start.x)), h: snap(Math.abs(now.y - d.start.y)) };
					d.preview = { name: d.name, boxes: d.existing.concat([box]) };
					d.moved = box.w * this.zoom > 4 && box.h * this.zoom > 4;
				}
				this.draw();
			});
			c.addEventListener('pointerup', () => {
				const d = this.drag;
				this.drag = null;
				// a right-click on the map itself (not dragged): what can be made there
				if (d && d.kind === 'pan' && d.button === 2 && !d.moved && this.hooks.emptyMenu) {
					this.hooks.emptyMenu(this.toWorld(d.start.x, d.start.y), d.client.x, d.client.y);
					return;
				}
				if (d && d.kind === 'line') {
					if (d.moved) {
						this.hooks.moveMark && this.hooks.moveMark(d.id, d.x);
					} else {
						this.hooks.pickMark && this.hooks.pickMark({ id: d.id, kind: 'line' });
					}
					this.draw();
					return;
				}
				if (!d || d.kind === 'pan' || !d.moved || !d.preview) {
					this.draw();
					return;
				}
				if (d.kind === 'draw') {
					this.state.area = d.name;
					this.state.box = d.preview.boxes.length - 1;
					this.hooks.picked(d.name);
				}
				this.hooks.change(d.name, d.preview.boxes);
				this.draw();
			});
		}
	}

	// ---- where scripts name an area ----

	// Lines in the mod's scripts that name an area (in quotes), outside the block Studio writes: [{ path, line, text }]. The mission's own nodes are
	// listed separately, so the generated code is not counted twice.
	function codeUses(scenario, name) {
		const found = [];
		const module = scenario.module.toLowerCase() + '/';
		const quoted = new RegExp('["\']' + name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + '["\']');
		for (const sc of MS.ide.lua.scripts.values()) {
			if (!sc.path.toLowerCase().startsWith(module) || !sc.text.includes(name)) {
				continue;
			}
			let inside = false;
			sc.text.split('\n').forEach((text, i) => {
				if (text.startsWith(MS.flow.MARK_BEGIN) || text.startsWith('-- >>> Studio rules') || text.startsWith('-- >>> Studio triggers')) {
					inside = true;
				}
				if (!inside && quoted.test(text)) {
					found.push({ path: sc.path, line: i + 1, text: text.trim() });
				}
				if (text.startsWith(MS.flow.MARK_END) || text.startsWith('-- <<< Studio rules') || text.startsWith('-- <<< Studio triggers')) {
					inside = false;
				}
			});
		}
		return found;
	}

	// ---- the editor tab ----

	// The maps open for each scenario, to redraw when its mission changes: a tab can show the map, the graph or both, and the map and the graph
	// can be in different tabs or windows. key -> Map(tab -> { root, draw, stage, lit, pick }).
	const maps = new Map();
	function eachMap(key, fn) {
		for (const m of (maps.get(key) || new Map()).values()) {
			if (m.root.isConnected) {
				fn(m);
			}
		}
	}
	const redrawMaps = (key) => eachMap(key, (m) => m.draw());
	// what the graph picks lights up on the map, and what the map picks lights up in the graph, whichever windows they are in
	MS.news = MS.news || {};
	MS.news['mission-lit'] = (m) => eachMap(m.key, (x) => x.lit(m.names || []));
	MS.news['mission-area'] = (m) => MS.missionLogic && MS.missionLogic.areaPicked(m.name);
	MS.news['mission-node'] = (m) => {
		const scenario = scenarioByKey(m.key);
		if (scenario && MS.missionLogic) {
			MS.missionLogic.focusNode(scenario, m.id);
		}
	};
	const tell = (type, body) => MS.popout && MS.popout.announce && MS.popout.announce(type, body);

	// part: 'both' (the map and the graph side by side), 'map' or 'logic' (the graph), each its own tab
	function render(key, part) {
		return (host, tab) => {
			const a = app();
			if (!a.lib || !a.lib.presets.length) {
				host.replaceChildren(el('p', { className: 'ed ed-empty', textContent: 'Studio is still reading the game…' }));
				return;
			}
			const scenario = scenarioByKey(key);
			if (!scenario) {
				host.replaceChildren(el('p', { className: 'ed ed-empty', textContent: 'That scenario is not there any more.' }));
				return;
			}
			const s = tab.state || (tab.state = { map: {}, split: 0.58 });
			if (part === 'map' && MS.missionLogic && MS.missionLogic.docFor) {
				// the mission read here too, for what the map shows of it (what uses each area, the stage picked); a window just opened reads the
				// scripts first, then draws again
				if (MS.missionLogic.docFor(scenario).text === null && !s.readingScripts) {
					s.readingScripts = true;
					MS.ide.ensureScripts().then(() => MS.ide.refreshForm()).catch(() => {});
				}
			}
			// Ctrl+Z on this tab: the mission graph's history when it changed last, else Studio's (areas drawn on the map)
			tab.undo = () => !!(MS.missionLogic && MS.missionLogic.historyStep(scenarioByKey(key), false));
			tab.redo = () => !!(MS.missionLogic && MS.missionLogic.historyStep(scenarioByKey(key), true));
			const scene = sceneOf(scenario);
			const terrainFile = scene && MS.scenes.terrainPicture ? MS.scenes.terrainPicture(scene, a.lib.resolve(scene.node)) : null;
			// the scene with its buildings and objects once they are read (the bare terrain until then)
			let built = scene ? MS.scenes.picture(scene, (picture) => {
				built = picture;
				if (s.view) {
					s.view.draw();
				}
			}) : null;
			const terrain = () => built || (terrainFile ? a.sprite(terrainFile, 1, 0) : null);
			if (terrainFile && !terrain() && !s.asked) {
				s.asked = true;
				a.images.get(terrainFile).then(() => setTimeout(() => {
					if (s.view) {
						s.view.fit();
					}
				}, 30));
			}
			const areas = () => areasOf(sceneOf(scenarioByKey(key)));
			const scriptPath = a.lib.resolve(scenario.node).str('ScriptPath', '');
			const usage = (name) => (MS.missionLogic ? MS.missionLogic.usage(scenarioByKey(key), name) : 0);
			const change = async (name, boxes) => {
				const target = await editableScene(scenarioByKey(key));
				if (target) {
					writeArea(target, name, boxes);
				} else {
					MS.ide.refreshForm();
				}
			};
			const freshName = () => {
				const taken = new Set(areas().map((r) => r.name));
				let n = 1;
				while (taken.has('Area ' + n)) {
					n++;
				}
				return 'Area ' + n;
			};
			const renameArea = (name) => {
				const input = el('input', { type: 'text', className: 'ed-input', value: name });
				MS.dialogs.open('Rename ' + name, MS.dialogs.parts.field('Name', input, 'Triggers and objectives use areas by name; Studio renames it in the mission too.'), [
					{ text: 'Cancel' },
					{ text: 'Rename', primary: true, action: async () => {
						const to = input.value.trim();
						if (!to || to === name) {
							return true;
						}
						if (areas().some((r) => r.name === to)) {
							a.toast('There is already an area called ' + to + '.', 'bad');
							return false;
						}
						const target = await editableScene(scenarioByKey(key));
						if (!target) {
							return false;
						}
						const area = areas().find((r) => r.name === name);
						writeArea(target, to, area ? area.boxes : []);
						writeArea(target, name, []);
						s.map.area = to;
						if (MS.missionLogic && MS.missionLogic.renameArea) {
							await MS.missionLogic.renameArea(scenarioByKey(key), name, to);
						}
						return true;
					} },
				]);
			};
			const removeArea = async (name) => {
				const target = await editableScene(scenarioByKey(key));
				if (target) {
					writeArea(target, name, []);
					s.map.area = null;
				}
			};
			const menu = (area, x, y) => {
				const items = [
					['Rename…', () => renameArea(area.name)],
					['Delete the area', () => removeArea(area.name)],
				];
				if (MS.missionLogic && MS.missionLogic.areaActions) {
					items.unshift(...MS.missionLogic.areaActions(scenarioByKey(key), area.name));
				}
				const box = el('div', { className: 'ed-menu' }, ...items.map(([label, run]) => (label === '-' ? el('hr') : el('button', { className: 'ed-menu-item', onclick: () => {
					box.remove();
					run();
				} }, el('b', { textContent: label })))));
				box.style.left = x + 'px';
				box.style.top = y + 'px';
				document.body.append(box);
				const close = (ev) => {
					if (!box.contains(ev.target)) {
						box.remove();
						document.removeEventListener('pointerdown', close, true);
					}
				};
				setTimeout(() => document.addEventListener('pointerdown', close, true), 0);
			};
			// the filter (the areas list's Show): all, unused (a clear map to draw on), used, in code, or what the nodes use them for
			// and while a stage is picked beside the graph, only that stage's areas
			// the map's layers, each shown or hidden, or soloed as on a mixing desk (while any layer is soloed, only the soloed ones show)
			const layerState = (id) => {
				s.map.layers = s.map.layers || {};
				s.map.layers[id] = s.map.layers[id] || {};
				return s.map.layers[id];
			};
			const layerOn = (id) => {
				const all = s.map.layers || {};
				return Object.values(all).some((x) => x.solo) ? !!layerState(id).solo : !layerState(id).hidden;
			};
			const shown = () => {
				if (!layerOn('areas')) {
					return [];
				}
				const focus = MS.missionLogic ? MS.missionLogic.stageFocus(scenarioByKey(key)) : null;
				return byFilter().filter((r) => !focus || focus.areas.has(r.name));
			};
			const byFilter = () => {
				const f = s.map.filter || 'all';
				if (f === 'all') {
					return areas();
				}
				const roles = MS.missionLogic ? MS.missionLogic.areaRoles(scenarioByKey(key)) : new Map();
				return areas().filter((r) => {
					const used = roles.has(r.name);
					const code = codeUses(scenarioByKey(key), r.name).length > 0;
					if (f === 'unused') {
						return !used && !code;
					}
					if (f === 'used') {
						return used || code;
					}
					if (f === 'code') {
						return code;
					}
					return used && roles.get(r.name).has(f);
				});
			};
			// the mission's things on the map, where the game will put them: objects at their area (snapped to the game's 12 pixel grid, centred as the
			// game centres a terrain object), screens in the middle of each box of their area, a capture point's terminal on its middle (the Left or
			// Right one, as the game picks), and camera lines
			// one redraw for every picture that comes in about now (the same function each time, so a picture waits on it once)
			const redrawSoon = s.redrawSoon || (s.redrawSoon = () => {
				if (!s.redrawQueued) {
					s.redrawQueued = true;
					setTimeout(() => {
						s.redrawQueued = false;
						if (s.view) {
							s.view.draw();
						}
						if (s.drawList) {
							s.drawList(); // the layers' counts and lists, now their pictures are in
						}
					}, 40);
				}
			});
			const marks = () => {
				const scenarioNow = scenarioByKey(key);
				const list = MS.missionLogic && MS.missionLogic.mapMarks ? MS.missionLogic.mapMarks(scenarioNow) : [];
				const all = areas();
				const areaOf = (name) => all.find((x) => x.name === name);
				const snap = (v) => Math.floor(v / 12 + 0.5) * 12;
				const picked = s.map.markId;
				const out = [];
				for (const m of list) {
					const here = Object.assign({}, m, { picked: m.id === picked });
					if (m.kind === 'line') {
						out.push(here);
						continue;
					}
					const zone = areaOf(m.area);
					if (!zone || !zone.boxes.length) {
						continue;
					}
					const centre = (b) => ({ x: b.x + b.w / 2, y: b.y + b.h / 2 });
					let ref = m.ref;
					if (m.kind === 'terminal') {
						const around = areaOf(m.region) || zone;
						const mid = (z) => {
							const xs = z.boxes.map((b) => [b.x, b.x + b.w]).flat();
							return (Math.min(...xs) + Math.max(...xs)) / 2;
						};
						ref = ref.replace('{side}', mid(zone) >= mid(around) ? 'Left' : 'Right');
					}
					const pic = pictureOf(ref, m.kind === 'screen' ? 'sprite' : 'terrain', redrawSoon);
					if (!pic || !pic.img) {
						continue;
					}
					const spots = m.kind === 'screen' ? zone.boxes.map(centre)
						: m.kind === 'terminal' ? [centre(zone.boxes[0])].map((c) => ({ x: snap(c.x), y: snap(c.y) }))
						: (m.at === 'every' ? zone.boxes : zone.boxes.slice(0, 1)).map((b) => (m.at === 'centre' ? centre(b) : { x: b.x, y: b.y })).map((c) => ({ x: snap(c.x), y: snap(c.y) }));
					for (const at of spots) {
						out.push(Object.assign({}, here, { img: pic.img, x: at.x + pic.offset.x, y: at.y + pic.offset.y }));
					}
				}
				return out;
			};
			const pickMark = (m) => {
				s.map.markId = m.id;
				MS.missionLogic.focusNode(scenarioByKey(key), m.id);
				tell('mission-node', { key, id: m.id });
				view.draw();
			};
			const moveMark = (id, x) => MS.missionLogic.setFromMap(scenarioByKey(key), id, 'x', x, 'move the camera line');
			// a right-click on the map: a camera line there, or an object placed there (in a small area of its own)
			const emptyMenu = (at, cx, cy) => {
				const scenarioNow = scenarioByKey(key);
				const x = Math.round(at.x);
				const items = [
					['Camera line here (x ' + x + ')', () => MS.missionLogic.addFromMap(scenarioNow, 'cameraLimit', { x }, 'add a camera line')],
					['Place an object here…', async () => {
						const target = await editableScene(scenarioNow);
						if (!target) {
							return;
						}
						const taken = new Set(areas().map((r) => r.name));
						let k = 1;
						while (taken.has('Object ' + k)) {
							k++;
						}
						const name = 'Object ' + k;
						writeArea(target, name, [{ x: Math.round(at.x), y: Math.round(at.y), w: 24, h: 24 }]);
						MS.missionLogic.addFromMap(scenarioNow, 'placeObject', { object: '', area: name, at: 'corner' }, 'place an object');
						a.toast('Pick the object in the new node, and wire it in where it should appear.', 'ok');
					}],
				];
				const box = el('div', { className: 'ed-menu' }, ...items.map(([label, run]) => el('button', { className: 'ed-menu-item', onclick: () => {
					box.remove();
					run();
				} }, el('b', { textContent: label }))));
				box.style.left = cx + 'px';
				box.style.top = cy + 'px';
				document.body.append(box);
				const close = (ev) => {
					if (!box.contains(ev.target)) {
						box.remove();
						document.removeEventListener('pointerdown', close, true);
					}
				};
				setTimeout(() => document.addEventListener('pointerdown', close, true), 0);
			};
			const hooks = {
				marks: () => marks().filter((m) => layerOn(m.kind)), pickMark, moveMark, emptyMenu,
				areas: shown, terrain: () => terrain(), usage, freshName, menu,
				change: (name, boxes) => change(name, boxes),
				picked: (name) => {
					if (MS.missionLogic && MS.missionLogic.areaPicked) {
						MS.missionLogic.areaPicked(name);
					}
					tell('mission-area', { key, name });
					drawSide();
				},
			};
			if (!s.view) {
				s.view = new MapView(s.map, hooks);
				setTimeout(() => s.view.fit(), 50);
			} else {
				Object.assign(s.view.hooks, hooks);
			}
			const view = s.view;

			// the strip above the map: the scene, the area picked, and how to draw
			const scenes = a.lib.presets.filter((p) => p.topLevel && p.cls === 'Scene').sort((x, y) => x.name.localeCompare(y.name));
			const sceneSelect = el('select', { className: 'ed-input', disabled: locked(scenario), title: 'The scene this scenario plays on' }, el('option', { value: '', textContent: '(no scene)' }),
				...scenes.map((p) => el('option', { value: p.name, textContent: p.name + (p.module === 'Base.rte' ? '' : ' (' + p.module.replace(/[.]rte$/i, '') + ')'), selected: scene && p === scene })));
			sceneSelect.onchange = () => a.edit('Play ' + scenario.name + ' on ' + sceneSelect.value, () => scenario.node.set('SceneName', sceneSelect.value));
			const side = el('div', { className: 'me-areabar' });
			const drawSide = () => {
				const picked = areas().find((r) => r.name === s.map.area);
				side.replaceChildren(picked
					? el('span', {}, el('b', { textContent: picked.name }), ' · ' + picked.boxes.length + (picked.boxes.length === 1 ? ' box' : ' boxes') + ' · used ' + usage(picked.name) + ' times ',
						el('button', { className: 'tb', textContent: 'Rename', onclick: () => renameArea(picked.name) }),
						el('button', { className: 'tb', textContent: 'Delete', onclick: () => removeArea(picked.name) }))
					: el('span', { className: 'ed-note', textContent: 'Drag on the map to draw an area. Shift and drag adds a box to the area picked. Right-click an area for more. Wheel zooms, right-drag moves, F fits.' }));
			};
			drawSide();
			// the map or the graph in a window of its own (the other stays here, in its own tab), or the two back in one tab
			const apart = (popped) => {
				const scenarioNow = scenarioByKey(key);
				MS.ide.closeTab(tab, true);
				open(scenarioNow, popped === 'map' ? 'logic' : 'map');
				const other = open(scenarioNow, popped);
				if (other && MS.popout && MS.popout.pop) {
					MS.popout.pop(other);
				}
			};
			const together = () => {
				const scenarioNow = scenarioByKey(key);
				for (const t of MS.ide.tabs.filter((x) => x.kind === 'form' && x.editor && x.editor.module === key && /^mission(Map|Graph)$/.test(x.editor.id))) {
					MS.ide.closeTab(t, true);
				}
				open(scenarioNow, 'both');
			};
			const partButtons = part === 'both'
				? [el('button', { className: 'tb', textContent: '⧉ Graph in a window', title: 'The graph in a window of its own, the map staying here (they keep in step)', onclick: () => apart('logic') }),
					el('button', { className: 'tb', textContent: '⧉ Map in a window', title: 'The map in a window of its own, the graph staying here (they keep in step)', onclick: () => apart('map') })]
				: [el('button', { className: 'tb', textContent: part === 'map' ? 'Map and graph together' : 'Graph and map together', title: 'Back in one tab, side by side (a map or graph in another window can be closed there)', onclick: together })];
			const head = el('div', { className: 'me-head' },
				el('h2', { textContent: scenario.name + (part === 'map' ? ': map' : part === 'logic' ? ': graph' : '') }),
				part !== 'logic' ? el('label', { className: 'ed-check' }, 'Scene', sceneSelect) : null,
				el('span', { className: 'spacer' }),
				...partButtons,
				part !== 'logic' ? el('button', { className: 'tb', textContent: 'Fit the map', onclick: () => view.fit() }) : null,
				scriptPath ? el('button', { className: 'tb', textContent: 'Open the script', onclick: () => MS.ide.openFile(scriptPath, 1) }) : null,
				el('button', { className: 'tb primary', textContent: '▶ Play in the game', title: 'Starts it on its map: in the game running from Studio, or in a game started now', onclick: async () => {
					if (!scene) {
						a.toast('Pick the map it\'s played on first (Scene, above).', 'bad');
						return;
					}
					if (MS.inspector && MS.inspector.live && (await MS.inspector.live())) {
						MS.ide.runLua('ActivityMan:StartActivity(' + JSON.stringify(scenario.cls) + ', ' + JSON.stringify(scenario.name) + ')');
						a.status('Told the game to start ' + scenario.name + ' on ' + scene.name + '.');
					} else if (MS.launcher && MS.launcher.launchScenario) {
						MS.launcher.launchScenario(scenario.name); // no game from Studio running: one is started, straight into it
					}
				} }));
			// the areas list: every area of the scene, what uses it, and (for the one picked) each use, to jump to
			const list = el('div', { className: 'me-arealist' });
			const drawList = () => {
				const every = areas();
				const all = shown();
				const names = every.map((r) => r.name);
				const picked = s.map.area;
				// Show: which areas the list and the map show
				const roles = MS.missionLogic ? MS.missionLogic.AREA_ROLES : [];
				const filter = el('select', { className: 'ed-input me-filter', title: 'Which areas the list and the map show' },
					...[['all', 'All areas'], ['unused', 'Unused (a clear map to draw on)'], ['used', 'Used by the mission or the script'], ['code', 'Used in the script\'s own code'], ...roles].map(([v, t]) => el('option', { value: v, textContent: t, selected: v === (s.map.filter || 'all') })));
				filter.onchange = () => {
					s.map.filter = filter.value;
					view.draw();
					drawList();
				};
				const focus = MS.missionLogic ? MS.missionLogic.stageFocus(scenarioByKey(key)) : null;
				// the layers: an eye to show or hide each, S to solo it, and how many things are on it
				const raw = marks();
				const itemsOf = (id) => {
					const seen = new Set();
					return raw.filter((m) => m.kind === id && !seen.has(m.id + '|' + m.label) && seen.add(m.id + '|' + m.label));
				};
				const refresh = () => {
					view.draw();
					drawList();
				};
				const layerRow = ([id, label]) => {
					const st = layerState(id);
					return el('div', { className: 'me-layer' + (layerOn(id) ? '' : ' off') },
						el('button', { className: 'me-eye' + (st.hidden ? ' hidden' : ''), textContent: st.hidden ? '○' : '●', title: st.hidden ? 'Show this layer' : 'Hide this layer', onclick: () => {
							st.hidden = !st.hidden;
							refresh();
						} }),
						el('button', { className: 'me-solo' + (st.solo ? ' on' : ''), textContent: 'S', title: 'Solo: while any layer is soloed, only the soloed ones show', onclick: () => {
							st.solo = !st.solo;
							refresh();
						} }),
						el('span', { className: 'me-layername', textContent: label }),
						el('small', { textContent: String(id === 'areas' ? every.length : itemsOf(id).length) }));
				};
				list.replaceChildren(el('div', { className: 'me-listhead' }, el('span', { textContent: 'Layers' })), ...MAP_LAYERS.map(layerRow));
				if (layerOn('areas')) {
					list.append(el('div', { className: 'me-listhead' }, el('span', { textContent: 'Areas (' + (all.length === every.length ? every.length : all.length + ' of ' + every.length) + ')' }), filter,
						focus ? el('div', { className: 'me-stagefocus' }, el('span', { textContent: 'Only ' + focus.name, title: 'A stage is picked beside the graph: only its areas are shown' }),
							el('button', { className: 'tb', textContent: 'Show all', onclick: () => MS.missionLogic.clearStage(scenarioByKey(key)) })) : null));
					if (!all.length) {
						list.append(el('p', { className: 'ed-note', textContent: every.length ? 'None of the areas is of this kind.' : 'None yet: drag on the map to draw one.' }));
					}
				}
				for (const r of all) {
					const nodes = MS.missionLogic ? MS.missionLogic.areaUses(scenarioByKey(key), r.name) : [];
					const code = codeUses(scenarioByKey(key), r.name);
					const row = el('button', { className: 'me-area' + (r.name === picked ? ' sel' : ''), title: 'Show it on the map, and what uses it', onclick: () => {
						s.map.area = r.name;
						s.map.box = 0;
						view.draw();
						if (MS.missionLogic) {
							MS.missionLogic.areaPicked(r.name);
						}
						tell('mission-area', { key, name: r.name });
						drawSide();
						drawList();
					} },
					el('span', { className: 'me-swatch', style: 'background:' + colorOf(r.name, names) }),
					el('span', { className: 'me-areaname', textContent: r.name }),
					el('small', { textContent: (nodes.length ? nodes.length + ' ⚑' : '') + (nodes.length && code.length ? ' · ' : '') + (code.length ? code.length + ' in code' : '') || 'unused' }));
					list.append(row);
					if (r.name === picked) {
						const uses = el('div', { className: 'me-uses' });
						for (const n of nodes) {
							uses.append(el('button', { className: 'me-use', title: 'Show this node in the graph', onclick: () => {
								MS.missionLogic.focusNode(scenarioByKey(key), n.id);
								tell('mission-node', { key, id: n.id });
							} }, el('span', { className: 'me-use-kind', textContent: '⚑' }), n.title));
						}
						for (const c of code) {
							uses.append(el('button', { className: 'me-use', title: c.path + ', line ' + c.line + ': open it there', onclick: () => MS.ide.openFile(c.path, c.line) }, el('span', { className: 'me-use-kind code', textContent: c.path.split('/').pop() + ':' + c.line }), el('code', { textContent: c.text.slice(0, 90) })));
						}
						if (!nodes.length && !code.length) {
							uses.append(el('p', { className: 'ed-note', textContent: 'Nothing uses it yet. Right-click it on the map to make it an objective or a trigger.' }));
						}
						list.append(uses);
					}
				}
				// the other layers' things: click one to see it on the map and its node in the graph
				for (const [id, label] of MAP_LAYERS.slice(1)) {
					const items = layerOn(id) ? itemsOf(id) : [];
					if (!items.length) {
						continue;
					}
					list.append(el('div', { className: 'me-listhead' }, el('span', { textContent: label + ' (' + items.length + ')' })));
					for (const m of items) {
						list.append(el('button', { className: 'me-area' + (s.map.markId === m.id ? ' sel' : ''), title: m.label + ': show it on the map, and its node', onclick: () => {
							const r = view.root.getBoundingClientRect();
							const cx = m.kind === 'line' ? m.x : m.x + (m.img ? m.img.width / 2 : 0);
							const cy = m.kind === 'line' ? (s.map.panY || 0) + r.height / 2 / view.zoom : m.y + (m.img ? m.img.height / 2 : 0);
							s.map.panX = cx - r.width / 2 / view.zoom;
							s.map.panY = cy - r.height / 2 / view.zoom;
							pickMark(m);
							drawList();
						} }, el('span', { className: 'me-swatch', style: 'background:' + LAYER_COLOURS[id] }), el('span', { className: 'me-areaname', textContent: m.label })));
					}
				}
			};
			drawList();
			s.drawList = drawList;
			const sidePicked = hooks.picked;
			hooks.picked = (name) => {
				sidePicked(name);
				drawList();
			};
			Object.assign(view.hooks, { picked: hooks.picked });
			const mapPane = el('div', { className: 'me-left' }, side, el('div', { className: 'me-mapwrap' }, list, MS.missionLogic.sideGrip(list, s.map, 'listWidth', () => view.draw()), view.root));
			// this tab's map, for the graph (here, in another tab or in another window) to redraw
			if (part !== 'logic') {
				if (!maps.has(key)) {
					maps.set(key, new Map());
				}
				maps.get(key).set(tab, {
					root: view.root,
					draw: () => {
						view.draw();
						drawSide();
						drawList();
					},
					stage: () => {
						view.draw();
						drawList();
					},
					lit: (names) => {
						s.map.lit = names;
						view.draw();
					},
					pick: (name) => {
						s.map.area = name;
						s.map.box = 0;
						view.draw();
						drawSide();
					},
				});
			} else {
				maps.get(key) && maps.get(key).delete(tab);
			}
			const logic = el('div', { className: 'me-right' });
			if (part !== 'map' && MS.missionLogic && MS.missionLogic.render) {
				MS.missionLogic.render(logic, scenario, { areas: () => areas().map((r) => r.name), changed: () => eachMap(key, (m) => m.draw()), stageChanged: () => eachMap(key, (m) => m.stage()), highlight: (names) => {
					eachMap(key, (m) => m.lit(names));
					tell('mission-lit', { key, names });
				}, pickArea: (name) => eachMap(key, (m) => m.pick(name)) });
			}
			if (part !== 'both') {
				host.replaceChildren(el('div', { className: 'me me-' + part }, head, part === 'map' ? mapPane : logic));
				requestAnimationFrame(() => view.draw());
				return;
			}
			const splitter = el('div', { className: 'me-splitter', title: 'Drag to share the room between the map and the logic' });
			const body = el('div', { className: 'me-body' }, mapPane, splitter, logic);
			body.style.gridTemplateColumns = 'minmax(240px, ' + s.split + 'fr) 6px minmax(260px, ' + (1 - s.split) + 'fr)';
			splitter.onpointerdown = (e) => {
				splitter.setPointerCapture(e.pointerId);
				const r = body.getBoundingClientRect();
				splitter.onpointermove = (ev) => {
					s.split = Math.min(0.8, Math.max(0.2, (ev.clientX - r.left) / r.width));
					body.style.gridTemplateColumns = 'minmax(240px, ' + s.split + 'fr) 6px minmax(260px, ' + (1 - s.split) + 'fr)';
					view.draw();
				};
				splitter.onpointerup = () => {
					splitter.onpointermove = null;
				};
			};
			host.replaceChildren(el('div', { className: 'me' }, head, body));
			requestAnimationFrame(() => view.draw());
		};
	}

	// part: 'both' (default), 'map' or 'logic', each its own tab (so each can go in a window of its own)
	function open(scenario, part) {
		const key = keyOf(scenario);
		const p = part === 'map' || part === 'logic' ? part : 'both';
		const id = p === 'map' ? 'missionMap' : p === 'logic' ? 'missionGraph' : 'mission';
		const title = (p === 'map' ? 'Map · ' : p === 'logic' ? 'Graph · ' : 'Mission · ') + scenario.name;
		return MS.ide.openForm(id + ':' + key, title, p === 'map' ? '▦' : '⚑', render(key, p), { id, module: key });
	}

	// The Missions button: your scenarios to pick from, or a new one.
	function choose() {
		const a = app();
		if (!a.lib) {
			return;
		}
		const project = a.project;
		const mine = a.lib.presets.filter((p) => p.topLevel && p.node.key === 'AddActivity' && (project ? p.module === project : !a.fs.isOfficial(p.module))).sort((x, y) => x.name.localeCompare(y.name));
		const list = el('div', { className: 'me-pick' }, ...(mine.length ? mine.map((p) => el('button', { className: 'ed-menu-item', onclick: () => {
			MS.dialogs.close();
			open(p);
		} }, el('b', { textContent: p.name }), el('small', { textContent: p.module + ' · ' + (a.lib.resolve(p.node).str('SceneName', '') || 'no scene yet') })))
			: [el('p', { className: 'ed-note', textContent: project ? project + ' has no scenarios yet.' : 'Your mods have no scenarios yet.' })]));
		MS.dialogs.open('Missions', el('div', {}, el('p', { className: 'ed-note', textContent: 'Pick a mission to edit: its map, where you draw the areas, and its logic.' }), list), [
			{ text: 'Close' },
			{ text: '+ New mission…', primary: true, action: () => {
				const module = project || (a.lib.modules.find((m) => !m.official) || {}).name;
				if (!module) {
					a.toast('Choose the mod you are working on first (Project, at the top).', 'bad');
					return false;
				}
				MS.editors.open('scenarios', module);
				setTimeout(() => {
					const button = [...document.querySelectorAll('button')].find((b) => /New scenario/.test(b.textContent));
					if (button) {
						button.click();
					}
				}, 100);
				return true;
			} },
		], true);
	}

	MS.missionEditor = { open, redrawMaps, choose, areasOf, sceneOf, writeArea, editableScene };

	const button = document.getElementById('btnMissions');
	if (button) {
		button.onclick = choose;
	}
})();
