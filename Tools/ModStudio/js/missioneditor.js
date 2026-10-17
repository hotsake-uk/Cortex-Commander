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
			const areas = this.shown();
			const names = areas.map((x) => x.name);
			const lit = new Set(this.state.lit || []);
			ctx.font = '12px system-ui, sans-serif';
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
					this.drag = { kind: 'pan', start: at, panX: this.state.panX || 0, panY: this.state.panY || 0 };
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

	function render(key) {
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
			const hooks = {
				areas, terrain: () => terrain(), usage, freshName, menu,
				change: (name, boxes) => change(name, boxes),
				picked: (name) => {
					if (MS.missionLogic && MS.missionLogic.areaPicked) {
						MS.missionLogic.areaPicked(name);
					}
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
			const head = el('div', { className: 'me-head' },
				el('h2', { textContent: scenario.name }),
				el('label', { className: 'ed-check' }, 'Scene', sceneSelect),
				el('span', { className: 'spacer' }),
				el('button', { className: 'tb', textContent: 'Fit the map', onclick: () => view.fit() }),
				scriptPath ? el('button', { className: 'tb', textContent: 'Open the script', onclick: () => MS.ide.openFile(scriptPath, 1) }) : null,
				el('button', { className: 'tb primary', textContent: '▶ Play in the game', title: 'Starts it in a game running from Studio', onclick: () => {
					MS.ide.runLua('ActivityMan:StartActivity(' + JSON.stringify(scenario.cls) + ', ' + JSON.stringify(scenario.name) + ')');
					a.status('Told the game to start ' + scenario.name + '. It only works in a game started from Studio.');
				} }));
			// the areas list: every area of the scene, what uses it, and (for the one picked) each use, to jump to
			const list = el('div', { className: 'me-arealist' });
			const drawList = () => {
				const all = areas();
				const names = all.map((r) => r.name);
				const picked = s.map.area;
				list.replaceChildren(el('div', { className: 'me-listhead', textContent: 'Areas (' + all.length + ')' }));
				if (!all.length) {
					list.append(el('p', { className: 'ed-note', textContent: 'None yet: drag on the map to draw one.' }));
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
							uses.append(el('button', { className: 'me-use', title: 'Show this node in the graph', onclick: () => MS.missionLogic.focusNode(scenarioByKey(key), n.id) }, el('span', { className: 'me-use-kind', textContent: '⚑' }), n.title));
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
			};
			drawList();
			const sidePicked = hooks.picked;
			hooks.picked = (name) => {
				sidePicked(name);
				drawList();
			};
			Object.assign(view.hooks, { picked: hooks.picked });
			const mapPane = el('div', { className: 'me-left' }, side, el('div', { className: 'me-mapwrap' }, list, view.root));
			const logic = el('div', { className: 'me-right' });
			if (MS.missionLogic && MS.missionLogic.render) {
				MS.missionLogic.render(logic, scenario, { areas: () => areas().map((r) => r.name), changed: () => {
					view.draw();
					drawSide();
					drawList();
				}, highlight: (names) => {
					s.map.lit = names;
					view.draw();
				}, pickArea: (name) => {
					s.map.area = name;
					s.map.box = 0;
					view.draw();
					drawSide();
				} });
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

	function open(scenario) {
		const key = keyOf(scenario);
		MS.ide.openForm('mission:' + key, 'Mission · ' + scenario.name, '⚑', render(key), { id: 'mission', module: key });
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

	MS.missionEditor = { open, choose, areasOf, sceneOf, writeArea, editableScene };

	const button = document.getElementById('btnMissions');
	if (button) {
		button.onclick = choose;
	}
})();
