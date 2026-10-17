// Cortex Mod Studio: the Scenes data editor (Data view). A scene is a map the game plays on: its terrain, the backdrop layers behind it (sky,
// mountains, clouds, scrolling at their own speeds), and named areas (landing zones and the like). It shows the whole map with its areas, and a
// camera view that scrolls it the way the game does, so a backdrop's speed can be judged by moving the camera, not by guessing a number.
//   Info      description, where it sits on the planet, whether campaigns can use it, gravity
//   Backdrops the layers in front-to-back order: scroll speed, wrapping, order, adding from the game's backdrops
//   Areas     the named boxes (LZ Team 1, and so on): name, corner and size
// Changes are ordinary edits (app.edit), so Undo and Save work as everywhere else.
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
	const locked = (doc) => app().lib.isProtected(doc);
	const change = (label, fn) => app().edit(label, fn);
	const AREA_COLORS = ['#f2b63d', '#5bc0eb', '#9bc53d', '#e55934', '#c77dff', '#fa7921'];
	const asked = new Set(); // pictures the editor has already waited for, so one that can't be read doesn't loop
	const CAMERA = { w: 960, h: 540 }; // what the game shows at 1x, in world pixels

	// A copy of one block's lines under another node (to put a layer somewhere else in the list).
	function graft(dst, src) {
		for (const child of src.children) {
			graft(dst.add(child.key, child.value), child);
		}
	}

	// Moves one of a scene's layers a place earlier (-1) or later (+1) among the layers.
	function moveLayer(node, delta) {
		const parent = node.parent;
		const layers = parent.children.filter((c) => c.key === 'AddBackgroundLayer');
		const at = layers.indexOf(node);
		const other = layers[at + delta];
		if (!other) {
			return;
		}
		const copy = node.clone();
		const key = node.key;
		const value = node.value;
		node.remove();
		const index = parent.children.indexOf(other) + (delta > 0 ? 1 : 0);
		graft(parent.add(key, value, index), copy);
	}

	// The terrain's picture. Its preset is usually another block (a "Terrain" the scene copies), which may be declared as any kind of layer.
	function terrainPicture(p, ent) {
		const a = app();
		const entry = ent.last('Terrain');
		if (!entry) {
			return null;
		}
		const resolved = a.lib.resolve(entry.node);
		let file = resolved.file('BitmapFile') || resolved.file('FGColorFile');
		const copy = entry.node.child('CopyOf');
		if (!file && copy) {
			const parts = copy.value.trim().split('/');
			const target = a.lib.find(parts[parts.length - 1], null, parts.length > 1 ? parts[0] : p.module);
			if (target) {
				const r = a.lib.resolve(target.node);
				file = r.file('BitmapFile') || r.file('FGColorFile');
			}
		}
		return file;
	}

	// The scene's layers, areas and terrain picture, as resolved, with which of them the scene itself holds (the rest come from a scene it copies).
	function read(p) {
		const a = app();
		const ent = a.lib.resolve(p.node);
		const layers = ent.all('AddBackgroundLayer').map((entry) => {
			const r = a.lib.resolve(entry.node);
			const ref = entry.node.child('CopyOf') ? entry.node.child('CopyOf').value : '';
			return {
				node: entry.node,
				own: entry.node.isWithin(p.node),
				name: r.str('PresetName', ref.split('/').pop() || 'Layer'),
				file: r.file('BitmapFile'),
				scroll: r.vec('ScrollRatio', { x: 1, y: 1 }),
				wrapX: r.bool('WrapX', true),
				wrapY: r.bool('WrapY', false),
				offset: r.vec('Offset', { x: 0, y: 0 }),
			};
		});
		const areas = ent.all('AddArea').map((entry) => {
			const box = entry.node.child('AddBox');
			const corner = box ? box.vector('Corner') || { x: 0, y: 0 } : { x: 0, y: 0 };
			return {
				node: entry.node,
				own: entry.node.isWithin(p.node),
				name: entry.node.childValue('Name', 'Area'),
				box,
				x: corner.x,
				y: corner.y,
				w: box ? Number(box.childValue('Width', '0')) || 0 : 0,
				h: box ? Number(box.childValue('Height', '0')) || 0 : 0,
				boxes: entry.node.children.filter((c) => c.key === 'AddBox').length,
			};
		});
		return { ent, layers, areas, terrainFile: terrainPicture(p, ent) };
	}

	// ---- the two pictures ----

	// The whole map: terrain, areas and the camera's frame. Click or drag to move the camera.
	function drawOverview(canvas, info, view, onMove) {
		const ctx = canvas.getContext('2d');
		const terrain = info.terrainFile ? app().sprite(info.terrainFile, 1, 0) : null;
		const worldW = view.worldW;
		const worldH = view.worldH;
		const scale = canvas.width / worldW;
		canvas.height = Math.max(40, Math.round(worldH * scale));
		ctx.fillStyle = '#0d1117';
		ctx.fillRect(0, 0, canvas.width, canvas.height);
		if (terrain) {
			ctx.imageSmoothingEnabled = false;
			ctx.drawImage(terrain.canvas, 0, 0, canvas.width, terrain.height * scale);
		}
		ctx.font = '11px sans-serif';
		info.areas.forEach((area, i) => {
			const color = AREA_COLORS[i % AREA_COLORS.length];
			for (const box of area.node.children.filter((c) => c.key === 'AddBox')) {
				const corner = box.vector('Corner') || { x: 0, y: 0 };
				const w = Number(box.childValue('Width', '0')) || 0;
				const h = Number(box.childValue('Height', '0')) || 0;
				ctx.fillStyle = color + '33';
				ctx.fillRect(corner.x * scale, corner.y * scale, w * scale, h * scale);
				ctx.strokeStyle = color;
				ctx.strokeRect(corner.x * scale + 0.5, corner.y * scale + 0.5, Math.max(1, w * scale - 1), Math.max(1, h * scale - 1));
				ctx.fillStyle = color;
				ctx.fillText(area.name, corner.x * scale + 3, corner.y * scale + 12);
			}
		});
		ctx.strokeStyle = '#fff';
		ctx.lineWidth = 1.5;
		ctx.strokeRect(view.x * scale, view.y * scale, CAMERA.w * scale, CAMERA.h * scale);
		canvas.onpointerdown = (e) => {
			canvas.setPointerCapture(e.pointerId);
			const move = (ev) => {
				const r = canvas.getBoundingClientRect();
				onMove(((ev.clientX - r.left) / r.width) * worldW - CAMERA.w / 2, ((ev.clientY - r.top) / r.height) * worldH - CAMERA.h / 2);
			};
			move(e);
			canvas.onpointermove = move;
			canvas.onpointerup = () => {
				canvas.onpointermove = null;
			};
		};
	}

	// What the camera sees at one place: the backdrops, far to near, each scrolled by its own ratio, then the terrain on top. Approximate: the game
	// aligns some backdrops to the bottom and treats big Y ratios as "stays put".
	function drawCamera(canvas, info, view) {
		const ctx = canvas.getContext('2d');
		const scale = canvas.width / CAMERA.w;
		canvas.height = Math.round(CAMERA.h * scale);
		ctx.fillStyle = '#1a2233';
		ctx.fillRect(0, 0, canvas.width, canvas.height);
		ctx.imageSmoothingEnabled = false;
		for (const layer of [...info.layers].reverse()) {
			const frame = layer.file ? app().sprite(layer.file, 1, 0) : null;
			if (!frame) {
				continue;
			}
			const w = frame.width * scale;
			const h = frame.height * scale;
			const sx = layer.scroll.x;
			const sy = Math.abs(layer.scroll.y) > 10 ? 0 : layer.scroll.y;
			let x = -view.x * sx * scale + layer.offset.x * scale;
			const pinned = Math.abs(layer.scroll.y) > 10 || h >= canvas.height;
			const y = pinned ? canvas.height - h : -view.y * sy * scale + layer.offset.y * scale;
			if (layer.wrapX && w > 0) {
				x = ((x % w) + w) % w - w;
			}
			for (let dx = x; dx < canvas.width; dx += layer.wrapX ? w : canvas.width + 1) {
				ctx.drawImage(frame.canvas, dx, y, w, h);
			}
		}
		const terrain = info.terrainFile ? app().sprite(info.terrainFile, 1, 0) : null;
		if (terrain) {
			ctx.drawImage(terrain.canvas, view.x, view.y, CAMERA.w, CAMERA.h, 0, 0, canvas.width, canvas.height);
		}
	}

	// ---- the editor ----

	function renderScenes(module) {
		return (host, tab) => {
			const a = app();
			if (!a.lib || !a.lib.presets.length) {
				host.replaceChildren(el('p', { className: 'ed ed-empty', textContent: 'Studio is still reading the game…' }));
				return;
			}
			const s = tab.state || (tab.state = { filter: '', selected: null, view: { x: 0, y: 0 } });
			const all = a.lib.presets.filter((p) => p.topLevel && p.module === module && p.cls === 'Scene').sort((x, y) => x.name.localeCompare(y.name));
			const q = s.filter.trim().toLowerCase();
			const list = all.filter((p) => !q || p.name.toLowerCase().includes(q));
			const keyOf = (p) => p.doc.path + ':' + p.node.line + ':' + p.name;
			const selected = all.find((p) => keyOf(p) === s.selected) || list[0] || null;
			const filter = el('input', { type: 'search', placeholder: 'Filter ' + all.length + ' scenes…', value: s.filter, className: 'ed-input' });
			filter.dataset.fk = 'filter';
			filter.oninput = () => {
				s.filter = filter.value;
				clearTimeout(s.timer);
				s.timer = setTimeout(() => MS.ide.refreshForm(), 200);
			};
			const items = list.slice(0, 500).map((p) => el('button', { className: 'ed-item' + (p === selected ? ' sel' : ''), textContent: p.name, onclick: () => { s.selected = keyOf(p); s.view = { x: 0, y: 0 }; MS.ide.refreshForm(); } }));
			host.replaceChildren(el('div', { className: 'ed ed-split' },
				el('div', { className: 'ed-head' }, el('h2', { textContent: 'Scenes · ' + module })),
				all[0] && locked(all[0].doc) ? el('div', { className: 'ed-banner', textContent: module + ' is read only: its scenes can be looked at. Copy one into your mod (New… → Scene) to change it.' }) : null,
				el('div', { className: 'ed-panes' },
					el('div', { className: 'ed-list' }, filter, ...(items.length ? items : [el('p', { className: 'ed-empty', textContent: 'No scenes' + (q ? ' match.' : ' in this mod. Make one with New… → Scene.') })])),
					el('div', { className: 'ed-detail' }, selected ? sceneDetail(selected, module, s) : null))));
		};
	}

	function numberField(value, placeholder, disabled, onChange) {
		const input = el('input', { type: 'number', step: 'any', className: 'ed-input num', value, placeholder, disabled });
		input.onchange = () => onChange(input.value.trim());
		return input;
	}

	function sceneDetail(p, module, s) {
		const a = app();
		const info = read(p);
		const ro = locked(p.doc);
		const terrain = info.terrainFile ? a.sprite(info.terrainFile, 1, 0) : null;
		const worldW = terrain ? terrain.width : Math.max(2000, ...info.areas.map((r) => r.x + r.w));
		const worldH = terrain ? terrain.height : Math.max(600, ...info.areas.map((r) => r.y + r.h));
		const view = { x: Math.max(0, Math.min(worldW - CAMERA.w, s.view.x)), y: Math.max(0, Math.min(worldH - CAMERA.h, s.view.y)), worldW, worldH };
		const overview = el('canvas', { className: 'scene-canvas', width: 640, height: 120, title: 'The whole map: click to look somewhere' });
		const camera = el('canvas', { className: 'scene-canvas', width: 640, height: 360, title: 'What the camera sees there, with the backdrops scrolling as set' });
		const redraw = () => {
			drawOverview(overview, info, view, (x, y) => {
				s.view = { x, y };
				view.x = Math.max(0, Math.min(worldW - CAMERA.w, x));
				view.y = Math.max(0, Math.min(worldH - CAMERA.h, y));
				redraw();
			});
			drawCamera(camera, info, view);
		};
		redraw();
		// Pictures arrive a little after the editor opens (the map's size comes with the terrain's): draw the editor again once they have, once.
		const waiting = [info.terrainFile, ...info.layers.map((l) => l.file)].filter((f) => f && !a.sprite(f, 1, 0) && !asked.has(f.toLowerCase()));
		if (waiting.length) {
			waiting.forEach((f) => asked.add(f.toLowerCase()));
			Promise.all(waiting.map((f) => a.images.get(f))).then(() => setTimeout(() => MS.ide.refreshForm(), 50));
		}
		const own = (label, fn) => change(label + ' in ' + p.name, fn);

		// info
		const desc = el('textarea', { className: 'ed-input', rows: 3, value: info.ent.str('Description', ''), disabled: ro });
		desc.onchange = () => own('Change the description', () => p.node.set('Description', desc.value.replace(/\r?\n/g, ' ')));
		const loc = info.ent.vec('LocationOnPlanet', { x: 0, y: 0 });
		const playable = el('input', { type: 'checkbox', checked: info.ent.bool('MetagamePlayable', true), disabled: ro });
		playable.onchange = () => own('Change whether campaigns can use it', () => p.node.set('MetagamePlayable', playable.checked ? '1' : '0'));
		const gravity = info.ent.vec('GlobalAcceleration', { x: 0, y: 20 });

		// backdrops
		const layerRows = info.layers.map((layer, i) => {
			const thumb = el('canvas', { width: 56, height: 34, className: 'ed-thumb scene-thumb' });
			const frame = layer.file ? a.sprite(layer.file, 1, 0) : null;
			if (frame) {
				const t = thumb.getContext('2d');
				t.imageSmoothingEnabled = false;
				t.drawImage(frame.canvas, 0, 0, thumb.width, thumb.height);
			}
			const editable = layer.own && !ro;
			const scrollX = numberField(layer.scroll.x, '1', !editable, (v) => own('Set ' + layer.name + '\'s scroll speed', () => layer.node.setVector('ScrollRatio', Number(v) || 0, layer.scroll.y)));
			const scrollY = numberField(layer.scroll.y, '1', !editable, (v) => own('Set ' + layer.name + '\'s scroll speed', () => layer.node.setVector('ScrollRatio', layer.scroll.x, Number(v) || 0)));
			scrollX.title = 'Sideways: 1 moves with the terrain, 0 stays still, 0.25 drifts at a quarter';
			scrollY.title = 'Up and down: the same, for the camera moving up and down';
			const wrap = el('input', { type: 'checkbox', checked: layer.wrapX, disabled: !editable });
			wrap.onchange = () => own('Set whether ' + layer.name + ' repeats', () => layer.node.set('WrapX', wrap.checked ? '1' : '0'));
			const button = (text, title, fn, off) => el('button', { className: 'ico-btn', textContent: text, title, disabled: off || !editable, onclick: fn });
			return el('div', { className: 'scene-row' + (layer.own ? '' : ' inherited') },
				thumb,
				el('span', { className: 'scene-name', textContent: layer.name, title: (layer.file || '') + (layer.own ? '' : ' (from the scene this one copies)') }),
				el('label', { className: 'ed-check', title: 'Sideways and up/down scroll speed' }, 'Scroll', scrollX, scrollY),
				el('label', { className: 'ed-check', title: 'Repeat sideways to fill the width' }, wrap, 'Repeats'),
				button('▲', 'Nearer the front', () => own('Move ' + layer.name + ' forward', () => moveLayer(layer.node, -1)), i === 0),
				button('▼', 'Further back', () => own('Move ' + layer.name + ' back', () => moveLayer(layer.node, 1)), i === info.layers.length - 1),
				button('×', 'Take it out of the scene', () => own('Take ' + layer.name + ' out', () => layer.node.remove())));
		});
		const addLayer = el('button', { className: 'tb', textContent: '+ Add a backdrop…', disabled: ro, onclick: () => {
			const all = a.lib.presets.filter((r) => r.topLevel && r.cls === 'SLBackground' && !!r.node.child('BitmapFile'));
			const modules = [...new Set(all.map((r) => r.module))].sort((x, y) => x.localeCompare(y));
			const draw = (c, r) => {
				const file = a.lib.resolve(r.node).file('BitmapFile');
				const frame = file ? a.sprite(file, 1, 0) : null;
				if (!frame) {
					return !file;
				}
				const ctx = c.getContext('2d');
				ctx.clearRect(0, 0, c.width, c.height);
				ctx.drawImage(frame.canvas, 0, 0, c.width, c.height);
				return true;
			};
			MS.dialogs.open('Add a backdrop to ' + p.name, MS.dialogs.parts.pickerGrid(all, (r) => {
				own('Add ' + r.name, () => {
					const layer = p.node.add('AddBackgroundLayer', 'SLBackground');
					layer.add('CopyOf', a.lib.reference(r.name, r.module, p.module));
				});
			}, draw, false, [{ label: 'Where it comes from', options: [['', 'Everything'], ...modules.map((m) => [m, m.replace(/\.rte$/i, '')])], test: (r, v) => r.module === v }]), [{ text: 'Cancel' }], true);
		} });

		// areas
		const areaRows = info.areas.map((area, i) => {
			const editable = area.own && !ro && area.box;
			const color = AREA_COLORS[i % AREA_COLORS.length];
			const name = el('input', { type: 'text', className: 'ed-input', value: area.name, disabled: !area.own || ro });
			name.onchange = () => own('Rename ' + area.name, () => area.node.set('Name', name.value.trim() || area.name));
			const set = (key, label) => numberField(area[key], '0', !editable, (v) => own('Change ' + area.name + '\'s ' + label, () => {
				if (key === 'x' || key === 'y') {
					area.box.setVector('Corner', key === 'x' ? Number(v) || 0 : area.x, key === 'y' ? Number(v) || 0 : area.y);
				} else {
					area.box.set(key === 'w' ? 'Width' : 'Height', String(Math.max(0, Number(v) || 0)));
				}
			}));
			return el('div', { className: 'scene-row' + (area.own ? '' : ' inherited') },
				el('span', { className: 'scene-swatch', style: 'background:' + color }),
				name,
				el('label', { className: 'ed-check' }, 'x', set('x', 'corner')), el('label', { className: 'ed-check' }, 'y', set('y', 'corner')),
				el('label', { className: 'ed-check' }, 'wide', set('w', 'width')), el('label', { className: 'ed-check' }, 'high', set('h', 'height')),
				area.boxes > 1 ? el('small', { textContent: '+' + (area.boxes - 1) + ' more boxes' }) : null,
				el('button', { className: 'ico-btn', textContent: '×', title: 'Take the area out', disabled: !area.own || ro, onclick: () => own('Take ' + area.name + ' out', () => area.node.remove()) }));
		});
		const addArea = el('button', { className: 'tb', textContent: '+ Add an area', disabled: ro, onclick: () => own('Add an area', () => {
			const area = p.node.add('AddArea', 'Area');
			const box = area.add('AddBox', 'Box');
			box.setVector('Corner', Math.round(view.x), Math.round(view.y));
			box.add('Width', '480');
			box.add('Height', '120');
			area.add('Name', 'New Area');
		}) });

		const row = (label, ...fields) => el('div', { className: 'ed-row' }, el('span', { className: 'ed-label', textContent: label }), ...fields);
		return el('div', {},
			el('div', { className: 'ed-card-head' }, el('h3', { textContent: p.name }), el('span', { className: 'spacer' }),
				el('button', { className: 'ico-btn', textContent: 'Its lines', onclick: () => MS.ide.openFile(p.doc.path, p.node.line) })),
			el('div', { className: 'ed-label', textContent: 'The map (terrain shown as its material picture)' }), overview,
			el('div', { className: 'ed-label', textContent: 'Through the camera (position ' + Math.round(view.x) + ', ' + Math.round(view.y) + ')' }), camera,
			el('div', { className: 'ed-label', textContent: 'About it' }),
			row('Description', desc),
			row('On the planet', 'x', numberField(loc.x, '0', ro, (v) => own('Move it on the planet', () => p.node.setVector('LocationOnPlanet', Number(v) || 0, loc.y))), 'y', numberField(loc.y, '0', ro, (v) => own('Move it on the planet', () => p.node.setVector('LocationOnPlanet', loc.x, Number(v) || 0)))),
			row('Gravity (down)', numberField(gravity.y, '20', ro, (v) => own('Change gravity', () => p.node.setVector('GlobalAcceleration', gravity.x, Number(v) || 0))), el('small', { textContent: '20 is the usual, 0 floats' })),
			row('', el('label', { className: 'ed-check' }, playable, 'Campaigns can use this scene')),
			el('div', { className: 'ed-label', textContent: 'Backdrops, nearest first (' + info.layers.length + ')' }), ...layerRows, el('div', { className: 'ed-row' }, addLayer),
			el('div', { className: 'ed-label', textContent: 'Areas (' + info.areas.length + ')' }), ...areaRows, el('div', { className: 'ed-row' }, addArea));
	}

	// Joins the Data view's list.
	if (MS.editors && MS.editors.register) {
		MS.editors.register(['scenes', 'Scenes', 'Maps: terrain, backdrop layers and areas', '▤', renderScenes, (module) => app().lib.presets.filter((p) => p.topLevel && p.module === module && p.cls === 'Scene').length]);
	}
	MS.scenes = { renderScenes };
})();
