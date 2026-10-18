// Cortex Mod Studio: dialogs. Making your own copy of a unit, adding attachments, picking a held item, importing sprites, help.
'use strict';
window.MS = window.MS || {};

(function () {
	const $ = (id) => document.getElementById(id);
	const app = () => MS.app;
	const el = (tag, props, ...children) => {
		const e = document.createElement(tag);
		Object.assign(e, props || {});
		for (const c of children) {
			if (c !== null && c !== undefined) {
				e.append(c);
			}
		}
		return e;
	};
	const safeName = (s) => s.replace(/[^A-Za-z0-9 _-]+/g, '').trim().replace(/\s+/g, '') || 'Unit';

	function open(title, body, buttons, wide) {
		const card = $('modalCard');
		card.className = 'modal-card' + (wide ? ' wide' : '');
		card.innerHTML = '';
		const foot = el('div', { className: 'foot' });
		for (const b of buttons || [{ text: 'Close' }]) {
			const btn = el('button', { className: 'tb' + (b.primary ? ' primary' : ''), textContent: b.text });
			btn.onclick = async () => {
				if (b.action) {
					btn.disabled = true;
					const keep = await b.action();
					btn.disabled = false;
					if (keep === false) {
						return;
					}
				}
				close();
			};
			foot.append(btn);
		}
		card.append(el('h2', { textContent: title }), el('div', { className: 'body' }, body), foot);
		$('modal').hidden = false;
		$('modal').onclick = (e) => {
			if (e.target === $('modal')) {
				close();
			}
		};
		const first = card.querySelector('input:not([type=checkbox]),select');
		if (first) {
			setTimeout(() => first.focus(), 30);
		}
	}

	function close() {
		$('modal').hidden = true;
		$('modalCard').innerHTML = '';
	}

	function field(label, input, note) {
		return el('div', { className: 'field' }, el('label', { textContent: label }), input, note ? el('div', { className: 'note', textContent: note }) : null);
	}

	function para(html) {
		const p = el('p');
		p.innerHTML = html;
		return p;
	}

	// The mods (not the game's own modules) a new file can go in, newest choice first.
	function modChoices() {
		const a = app();
		const mods = a.lib.modules.filter((m) => !m.official).map((m) => m.name);
		const last = a.project || a.readLocal('ms.lastMod');
		if (last && !mods.includes(last)) {
			mods.unshift(last);
		}
		return mods;
	}

	function modPicker() {
		const a = app();
		const select = el('select');
		const mods = modChoices();
		for (const m of mods) {
			select.append(el('option', { value: m, textContent: m }));
		}
		select.append(el('option', { value: '__new', textContent: 'A new mod…' }));
		const name = el('input', { type: 'text', placeholder: 'MyMod', value: mods.length ? '' : 'MyMod' });
		const wrap = el('div', {}, select, name);
		name.style.marginTop = '6px';
		const sync = () => {
			name.style.display = select.value === '__new' ? '' : 'none';
		};
		const preferred = a.project || a.readLocal('ms.lastMod');
		select.value = preferred && mods.includes(preferred) ? preferred : mods[0] || '__new';
		select.onchange = sync;
		sync();
		return {
			node: wrap,
			value() {
				if (select.value !== '__new') {
					return select.value;
				}
				const n = safeName(name.value);
				return n ? (n.toLowerCase().endsWith('rte') ? n.replace(/\.?rte$/i, '') : n) + '.rte' : null;
			},
		};
	}

	// ---- make my own copy ----

	// Make my own copy: of the unit being designed, or of another preset (source: {name, cls, path}), like a held item or the
	// preset an attachment is in. onMade(preset) runs with the new copy instead of opening it in the designer.
	function copyUnit(source, onMade) {
		const a = app();
		source = source || a.actor;
		if (!source) {
			return;
		}
		const mod = modPicker();
		const thing = MS.Rig.DEVICE_CLASSES.has(source.cls) ? 'item' : 'unit';
		if (onMade && a.actor) {
			// A held item's copy goes in the unit's own mod unless you pick another.
			const select = mod.node.querySelector('select');
			const unitsMod = a.actor.path.split('/')[0];
			if ([...select.options].some((o) => o.value === unitsMod)) {
				select.value = unitsMod;
				select.onchange();
			}
		}
		const name = el('input', { type: 'text', value: source.name + ' Custom' });
		const body = el('div', {},
			para('Studio makes a new ' + thing + ' in your mod that starts as a copy of <b></b> (<code>CopyOf</code>). Everything you change is written into the copy; the original and the game\'s own files stay as they are.' + (onMade ? ' The unit you\'re designing then carries your copy instead.' : '')),
			field('Your mod', mod.node, 'Mods live in the game\'s Mods folder. A new mod is made for you.'),
			field('New ' + thing + '\'s name', name, 'This is the name in the buy menu. It has to be different from every other ' + thing + '\'s name.'));
		body.querySelector('b').textContent = source.name;
		open('Make my own copy', body, [
			{ text: 'Cancel' },
			{
				text: 'Make the copy',
				primary: true,
				action: async () => {
					const modName = mod.value();
					const unitName = name.value.trim();
					if (!modName || !unitName) {
						a.toast('Give the mod and the unit a name.', 'bad');
						return false;
					}
					const taken = a.lib.find(unitName, source.cls, modName);
					if (taken && taken.name.toLowerCase() === unitName.toLowerCase()) {
						a.toast('There is already a ' + source.cls + ' called ' + unitName + '.', 'bad');
						return false;
					}
					await makeCopy(modName, unitName, source, onMade);
				},
			},
		]);
	}

	async function makeCopy(modName, unitName, original, onMade) {
		const a = app();
		const lib = a.lib;
		const isItem = MS.Rig.DEVICE_CLASSES.has(original.cls);
		const path = modName + (isItem ? '/Devices/' : '/Actors/') + safeName(unitName) + '.ini';
		let doc = lib.docFor(path);
		if (!doc) {
			if (await a.fs.exists(path)) {
				a.toast(path + ' already exists but isn\'t part of the mod. Pick another name.', 'bad');
				return;
			}
			const hadModule = !!lib.moduleOf(path);
			// A held item's copy is read before the unit that carries it.
			doc = await lib.newDoc(path, onMade && a.actor ? a.actor.path : null);
			a.hookDoc(doc);
			if (!hadModule) {
				a.hookDoc(lib.docFor(modName + '/Index.ini'));
				a.toast('Made the new mod ' + modName + '. The game loads new mods when it starts, so restart it once before previewing.', 'good');
			}
		}
		const block = doc.addTopLevel(isItem ? 'AddDevice' : 'AddActor', original.cls);
		block.add('CopyOf', lib.reference(original.name, original.path.split('/')[0], modName));
		block.add('PresetName', unitName);
		lib.reindex();
		a.writeLocal('ms.lastMod', modName);
		const preset = lib.presets.find((p) => p.doc === doc && p.name === unitName);
		// A new mod and each new copy are written as soon as they're made: the copy's file and the mod's Index.ini that includes it.
		await a.save(true, [doc, lib.docFor(modName + '/Index.ini')].filter(Boolean));
		a.updateFileStatus();
		if (onMade) {
			onMade(preset);
			a.toast('Made and saved ' + unitName + ' in ' + modName + '.', 'good');
			return;
		}
		a.undo = [];
		a.redo = [];
		a.openActor(preset);
		MS.panels.showLibrary();
		a.updateFileStatus();
		a.toast('Made and saved ' + unitName + ' in ' + modName + '.', 'good');
	}

	// ---- pickers ----

	// keepOpen: the dialog stays (a step of a bigger dialog), else picking closes it.
	// filters (optional): [{ label, options: [[value, text]...], test(p, value) }], shown as drop-downs beside the search.
	function pickerGrid(presets, onPick, thumb, keepOpen, filters) {
		const grid = el('div', { className: 'pick-grid' });
		const search = el('input', { type: 'search', placeholder: 'Search…' });
		const selects = (filters || []).map((f) => {
			const select = el('select', { className: 'ed-input', title: f.label }, ...f.options.map(([value, text]) => el('option', { value, textContent: text })));
			if (f.initial !== undefined) {
				select.value = f.initial;
			}
			select.onchange = () => render();
			return select;
		});
		const render = () => {
			grid.innerHTML = '';
			const q = search.value.trim().toLowerCase();
			const list = presets.filter((p) => (!q || p.name.toLowerCase().includes(q) || p.module.toLowerCase().includes(q) || p.cls.toLowerCase().includes(q)) && selects.every((sel, i) => !sel.value || filters[i].test(p, sel.value))).slice(0, 240);
			for (const p of list) {
				const canvas = el('canvas', { width: 64, height: 64 });
				const tile = el('div', { className: 'pick', title: p.name + ' (' + p.module + ')' }, canvas, el('div', { textContent: p.name }), el('small', { textContent: p.module }));
				tile.onclick = () => {
					if (!keepOpen) {
						close();
					}
					onPick(p, tile);
				};
				grid.append(tile);
				MS.panels.queueThumb(canvas, () => thumb(canvas, p));
			}
		};
		let t = null;
		search.oninput = () => {
			clearTimeout(t);
			t = setTimeout(render, 120);
		};
		render();
		return el('div', {}, el('div', { className: 'pick-filters' }, search, ...selects), grid);
	}

	function attachablePresets() {
		const seen = new Set();
		return app().lib.presets.filter((p) => {
			if (!MS.panels.TAB_CLASSES.attachments.has(p.cls) || app().hiddenModule(p.module)) {
				return false;
			}
			const key = p.module + '/' + p.name;
			if (seen.has(key) || !app().lib.resolve(p.node).file('SpriteFile')) {
				return false;
			}
			seen.add(key);
			return true;
		});
	}

	function attach(preset, part) {
		const a = app();
		if (!a.rig) {
			return;
		}
		part = part || a.selected || a.rig.body;
		if (!preset) {
			const all = attachablePresets();
			const modules = [...new Set(all.map((p) => p.module))].sort((x, y) => x.localeCompare(y));
			const kinds = [...new Set(all.map((p) => p.cls))].sort();
			const filters = [
				{ label: 'Where it comes from', initial: a.project || '', options: [['', 'Everything'], ...modules.map((m) => [m, m.replace(/\.rte$/i, '')])], test: (p, v) => p.module === v },
				{ label: 'What kind', options: [['', 'Any kind'], ...kinds.map((k) => [k, k])], test: (p, v) => p.cls === v },
			];
			open('Add an attachment to ' + part.name, pickerGrid(all, (p) => attach(p, part), (c, p) => MS.panels.drawSpriteThumb(c, p), false, filters), [{ text: 'Cancel' }], true);
			return;
		}
		if (a.needsCopy()) {
			a.copyThen(part, (copied) => attach(preset, copied));
			return;
		}
		const reason = a.lockReason(part);
		if (reason) {
			a.toast(reason, 'bad');
			return;
		}
		const parentId = part.id;
		a.edit('add ' + preset.name, () => {
			const node = a.ownBlock(part);
			if (!node) {
				return;
			}
			const added = node.add('AddAttachable', preset.cls);
			added.add('CopyOf', a.lib.reference(preset.name, preset.module, node.doc.module));
			added.setVector('ParentOffset', 0, 0);
		});
		const parent = a.rig && a.rig.parts.find((p) => p.id === parentId);
		const kids = parent ? parent.children.filter((c) => c.role === 'attachable') : [];
		if (kids.length) {
			a.select(kids[kids.length - 1]);
		}
		a.toast('Added ' + preset.name + '. Drag it into place.', 'good');
	}

	function pickItem() {
		const a = app();
		const items = a.lib.presets.filter((p) => MS.panels.TAB_CLASSES.items.has(p.cls) && p.topLevel && !a.hiddenModule(p.module));
		open('Put an item in the unit\'s hand', pickerGrid(items, (p) => a.setDevice(p), (c, p) => MS.panels.drawSpriteThumb(c, p)), [{ text: 'Cancel' }], true);
	}

	// ---- sprite import ----

	// Writes a sprite's frames ({width, height, indices} each) as numbered PNGs beside filePath, then, when a part is given, points that part at
	// them: its SpriteFile, FrameCount and, with centre, SpriteOffset. Returns the files written. Used by Replace the sprite and the pixel editor.
	async function saveSprite(part, filePath, frames, centre, key) {
		key = key || 'SpriteFile';
		const a = app();
		const paths = MS.Images.framePaths(filePath, frames.length);
		for (let i = 0; i < frames.length; i++) {
			const c = frames[i];
			await a.fs.writeFile(paths[i], await a.images.encodePng(c.width, c.height, c.indices));
		}
		a.forgetSprites(paths);
		MS.panels.forgetThumbs();
		MS.popout.saved(paths);
		for (const p of paths) {
			a.savedSinceLastPreview.add(p); // the game reads these again with the next Preview or Live update
		}
		if (part) {
			const w = frames[0].width;
			const h = frames[0].height;
			a.edit('change the sprite of ' + part.name, () => {
				const node = a.ownBlock(part);
				if (!node) {
					return;
				}
				let sf = node.children.find((c) => c.key === key);
				if (!sf) {
					sf = node.add(key, 'ContentFile', node.insertIndexFor(key));
				}
				sf.setValue('ContentFile');
				for (const old of sf.children.filter((c) => c.key === 'Path' || c.key === 'CopyOf')) {
					old.remove();
				}
				sf.set('FilePath', filePath);
				if (key === 'SpriteFile') {
					node.set('FrameCount', String(frames.length));
				}
				if (centre) {
					node.setVector('SpriteOffset', -Math.floor(w / 2), -Math.floor(h / 2));
				}
			});
		}
		return paths;
	}

	// An ImageData scaled by a factor. Pixel art (few colours, often an enlarged picture with big square "pixels") is sampled at the middle of
	// each new pixel, so it stays crisp and a stray pixel in a corner is dropped; photos and smooth art are smoothed when they shrink.
	function scaleImage(img, factor) {
		const w = Math.max(1, Math.round(img.width * factor));
		const h = Math.max(1, Math.round(img.height * factor));
		const src = new Uint32Array(img.data.buffer, img.data.byteOffset, img.width * img.height);
		const colours = new Set();
		for (let i = 0; i < src.length && colours.size <= 64; i++) {
			colours.add(src[i]);
		}
		if (colours.size <= 64) {
			const out = new ImageData(w, h);
			const dst = new Uint32Array(out.data.buffer);
			for (let y = 0; y < h; y++) {
				const sy = Math.min(img.height - 1, Math.floor((y + 0.5) / factor));
				for (let x = 0; x < w; x++) {
					dst[y * w + x] = src[sy * img.width + Math.min(img.width - 1, Math.floor((x + 0.5) / factor))];
				}
			}
			return out;
		}
		const from = document.createElement('canvas');
		from.width = img.width;
		from.height = img.height;
		from.getContext('2d').putImageData(img, 0, 0);
		const to = document.createElement('canvas');
		to.width = w;
		to.height = h;
		const ctx = to.getContext('2d');
		ctx.imageSmoothingEnabled = factor < 1;
		ctx.imageSmoothingQuality = 'high';
		ctx.drawImage(from, 0, 0, w, h);
		return ctx.getImageData(0, 0, w, h);
	}

	// options.files: pictures to start with (dropped on the guide); options.onDone: called once the sprite is in the unit.
	function importSprite(part, options) {
		const a = app();
		if (a.needsCopy()) {
			a.copyThen(part, (copied) => importSprite(copied, options));
			return;
		}
		const reason = a.lockReason(part);
		if (reason) {
			a.toast(reason, 'bad');
			return;
		}
		const input = el('input', { type: 'file', accept: 'image/png,image/bmp,image/gif,image/webp,image/jpeg', multiple: true });
		const framesInput = el('input', { type: 'number', min: 1, max: 64, value: 1 });
		const centre = el('input', { type: 'checkbox', checked: true });
		// Scaling: a picture much bigger or smaller than the part's own would break the unit's proportions, so by default it is fitted to the
		// size of the picture it replaces (keeping its shape); or kept as it is; or scaled by a percentage.
		const scaleMode = el('select', {}, el('option', { value: 'auto', textContent: 'Fit to the current picture' }), el('option', { value: 'keep', textContent: 'Keep its own size' }), el('option', { value: 'custom', textContent: 'Scale by a percentage' }));
		const scalePct = el('input', { type: 'number', min: 1, max: 800, value: 100, hidden: true, style: 'width:70px', title: 'Percent of its own size' });
		const actorName = safeName(a.actor.name);
		const roleName = { body: 'Body', head: 'Head', jetpack: 'Jetpack', fgarm: 'ArmFG', bgarm: 'ArmBG', fgleg: 'LegFG', bgleg: 'LegBG', foot: 'Foot', magazine: 'Magazine', device: 'Item', attachable: 'Attachment' }[part.role] || 'Part';
		const module = part.anchor.doc.module;
		const folder = el('input', { type: 'text', value: module + '/Sprites/' + actorName });
		const base = el('input', { type: 'text', value: roleName });
		const preview = el('div', { className: 'import-preview' });
		const info = el('p', { className: 'small' });
		const notes = el('div', { className: 'import-notes' });
		const compare = el('div', { className: 'import-compare' });
		let sources = []; // ImageData per file
		let scaledBy = 1; // how much the pictures were scaled, for the note
		let converted = [];

		// What the part has now, to compare the new picture with.
		const layerKey = options && options.key;
		const current = layerKey ? { file: part.ent.file(layerKey), frameCount: 1 } : part.sprite;
		const currentFirst = current.file ? a.sprite(current.file, current.frameCount, 0) : null;

		const addNote = (level, text) => notes.append(el('div', { className: 'import-note ' + level, textContent: text }));

		// The picture's likely problems, in everyday words.
		const analyse = (frames, leftover, changedPixels) => {
			notes.replaceChildren();
			const first = converted[0];
			let clear = 0;
			let partial = 0;
			let total = 0;
			for (const f of frames) {
				const d = f.data;
				for (let i = 0; i < f.width * f.height; i++) {
					const alpha = d[i * 4 + 3];
					total++;
					if (alpha < 128 || (d[i * 4] === 255 && d[i * 4 + 1] === 0 && d[i * 4 + 2] === 255)) {
						clear++;
					} else if (alpha < 255) {
						partial++;
					}
				}
			}
			let fine = true;
			const say = (level, text) => {
				fine = false;
				addNote(level, text);
			};
			if (clear === total) {
				say('bad', 'Every pixel is see-through, so nothing would show. Check the picture has something drawn on it.');
			} else if (clear === 0) {
				say('warn', 'There is no see-through background, so it will draw as a solid box. Make the background transparent (or pink, 255 0 255).');
			}
			if (partial > total * 0.02) {
				say('info', partial + ' pixels are partly see-through. The game only has solid or see-through, so Studio makes them solid.');
			}
			if (leftover) {
				say('warn', 'The sheet is ' + sources[0].width + ' pixels wide, which doesn\'t divide evenly into ' + framesInput.value + ' frames. ' + leftover + ' pixel' + (leftover === 1 ? '' : 's') + ' on the right would be dropped.');
			}
			if (currentFirst) {
				const big = first.width > currentFirst.width * 2.2 || first.height > currentFirst.height * 2.2;
				const small = first.width < currentFirst.width * 0.45 || first.height < currentFirst.height * 0.45;
				if (big || small) {
					say('warn', 'It is much ' + (big ? 'bigger' : 'smaller') + ' than the original (' + first.width + '×' + first.height + ' against ' + currentFirst.width + '×' + currentFirst.height + '), so this part would look ' + (big ? 'huge' : 'tiny') + ' next to others.');
				}
			}
			if ((part.role === 'fgarm' || part.role === 'bgarm' || part.role === 'fgleg' || part.role === 'bgleg') && converted.length < 2) {
				say('info', 'Arms and legs bend by frame, so with one frame they stay stiff. A sheet of several frames, most bent first, looks right.');
			}
			if (part.role === 'foot' && converted.length < 4) {
				say('info', 'Feet use up to four frames: flat, toes forward, heel back and far back. With fewer, the foot can\'t follow the ground.');
			}
			const solid = total - clear;
			if (solid && changedPixels / solid > 0.4) {
				say('info', 'Many colours were moved to the nearest game colour, so it may look a little different. The preview above is how the game will draw it.');
			}
			if (fine) {
				addNote('good', 'Looks good: a see-through background and a sensible size.');
			}
		};

		// The old picture beside the new one, and the new frames playing, so a walk or an arm bend can be judged before it goes in.
		let animTimer = 0;
		const showCompare = () => {
			clearInterval(animTimer);
			compare.replaceChildren();
			if (!converted.length) {
				return;
			}
			const zoomFor = (w, h) => Math.max(1, Math.min(6, Math.floor(96 / Math.max(w, h))));
			const frameCanvas = (p) => {
				const c = a.images.fromIndices(p.width, p.height, p.indices).canvas;
				const z = zoomFor(p.width, p.height);
				c.style.width = p.width * z + 'px';
				c.style.height = p.height * z + 'px';
				return c;
			};
			const col = (title, node, caption) => el('div', { className: 'import-col' }, el('b', { textContent: title }), node, el('small', { textContent: caption }));
			let oldNode = el('span', { className: 'import-none', textContent: 'no picture yet' });
			if (currentFirst) {
				const c = document.createElement('canvas');
				c.width = currentFirst.width;
				c.height = currentFirst.height;
				c.getContext('2d').drawImage(currentFirst.canvas, 0, 0);
				const z = zoomFor(c.width, c.height);
				c.style.width = c.width * z + 'px';
				c.style.height = c.height * z + 'px';
				oldNode = c;
			}
			const shown = el('div', { className: 'import-anim' });
			let i = 0;
			const draw = () => {
				shown.replaceChildren(frameCanvas(converted[i % converted.length]));
			};
			draw();
			if (converted.length > 1) {
				animTimer = setInterval(() => {
					if (!shown.isConnected) {
						clearInterval(animTimer);
						return;
					}
					i++;
					draw();
				}, 180);
			}
			compare.append(
				col('Now', oldNode, currentFirst ? currentFirst.width + '×' + currentFirst.height + ', ' + current.frameCount + ' frame' + (current.frameCount > 1 ? 's' : '') : ''),
				col('Yours' + (converted.length > 1 ? ' (playing)' : ''), shown, converted[0].width + '×' + converted[0].height + ', ' + converted.length + ' frame' + (converted.length > 1 ? 's' : '')));
		};

		const rebuild = () => {
			converted = [];
			preview.innerHTML = '';
			if (!sources.length) {
				info.textContent = '';
				notes.replaceChildren();
				compare.replaceChildren();
				return;
			}
			let frames = [];
			let leftover = 0;
			if (sources.length === 1) {
				const img = sources[0];
				const n = Math.max(1, Math.min(64, parseInt(framesInput.value, 10) || 1));
				const w = Math.floor(img.width / n);
				leftover = img.width - w * n;
				for (let i = 0; i < n; i++) {
					const c = document.createElement('canvas');
					c.width = w;
					c.height = img.height;
					const ctx = c.getContext('2d');
					ctx.putImageData(img, -i * w, 0);
					frames.push(ctx.getImageData(0, 0, w, img.height));
				}
			} else {
				frames = sources;
			}
			// The factor: to fit the current picture (the shape kept), 1, or the percentage. Frames are scaled together so a sheet stays in step.
			let factor = 1;
			const first = frames[0];
			if (scaleMode.value === 'auto' && currentFirst && first.width > 0 && first.height > 0) {
				factor = Math.min(currentFirst.width / first.width, currentFirst.height / first.height);
				// close enough is left alone, and a smaller picture is not blown up past twice
				if (factor > 0.85 && factor < 1.25) {
					factor = 1;
				}
				factor = Math.min(factor, 2);
			} else if (scaleMode.value === 'custom') {
				factor = Math.max(0.01, Math.min(8, (Number(scalePct.value) || 100) / 100));
			}
			scaledBy = factor;
			if (factor !== 1) {
				frames = frames.map((f) => scaleImage(f, factor));
			}
			let changed = 0;
			for (const f of frames) {
				const p = a.images.toPalette(f);
				changed += p.changed;
				converted.push(p);
				const shown = a.images.fromIndices(p.width, p.height, p.indices).canvas;
				const z = Math.max(1, Math.min(6, Math.floor(96 / Math.max(p.width, p.height))));
				shown.style.width = p.width * z + 'px';
				shown.style.height = p.height * z + 'px';
				preview.append(shown);
			}
			info.textContent = (scaledBy !== 1 ? 'Scaled to ' + Math.round(scaledBy * 100) + '% of its own size. ' : '') + converted.length + ' frame' + (converted.length > 1 ? 's' : '') + ', ' + converted[0].width + '×' + converted[0].height + ' pixels. ' + (changed ? changed + ' pixels moved to the nearest game colour (shown above as the game will draw them).' : 'Every colour is already in the game palette.');
			analyse(frames, leftover, changed);
			showCompare();
		};

		input.onchange = async () => {
			const files = [...input.files].sort((x, y) => x.name.localeCompare(y.name, undefined, { numeric: true }));
			sources = [];
			for (const file of files) {
				const bitmap = await createImageBitmap(file);
				const c = document.createElement('canvas');
				c.width = bitmap.width;
				c.height = bitmap.height;
				const ctx = c.getContext('2d');
				ctx.drawImage(bitmap, 0, 0);
				sources.push(ctx.getImageData(0, 0, c.width, c.height));
			}
			if (sources.length === 1 && sources[0].width > sources[0].height && sources[0].width % sources[0].height === 0) {
				framesInput.value = sources[0].width / sources[0].height;
			} else if (sources.length === 1) {
				framesInput.value = 1;
			}
			const stem = files[0] ? files[0].name.replace(/\.[^.]+$/, '').replace(/\d+$/, '') : '';
			if (stem && safeName(stem)) {
				base.value = safeName(stem);
			}
			rebuild();
		};
		framesInput.oninput = rebuild;
		scaleMode.onchange = () => {
			scalePct.hidden = scaleMode.value !== 'custom';
			rebuild();
		};
		scalePct.oninput = rebuild;

		const limbNote = part.role === 'fgarm' || part.role === 'bgarm' || part.role === 'fgleg' || part.role === 'bgleg' ? 'Arms and legs choose their frame by how far they stretch: the first frame is the most bent, the last the straightest. Feet use frames 0–3 for flat, toes forward, heel back and far back.' : null;
		const body = el('div', {},
			para('Pick a picture (or several, one per frame, or one sprite sheet with the frames side by side). Studio turns it into the game\'s palette and saves the frames as numbered files the game can load.'),
			field('Picture files', input),
			field('Frames in a sprite sheet', framesInput, 'For one picture: how many equal frames it is cut into, left to right.'),
			field('Size', el('span', { className: 'px-row' }, scaleMode, scalePct, el('span', { textContent: scalePct.hidden ? '' : '%' })), 'Fit keeps the picture\'s shape and shrinks it to the size of the one it replaces, so a huge picture doesn\'t break the unit.'),
			preview, info, compare, notes,
			limbNote ? el('div', { className: 'notice info', textContent: limbNote }) : null,
			field('Save into folder', folder),
			field('File name', base, 'Frames are saved as ' + 'Name000.png, Name001.png… (one frame: Name.png).'),
			el('label', { className: 'mini', style: 'margin:8px 0' }, centre, document.createTextNode(' Centre the picture on the part\'s pivot')));

		open('Replace the sprite of ' + part.name, body, [
			{ text: 'Cancel' },
			{
				text: 'Use this sprite',
				primary: true,
				action: async () => {
					if (!converted.length) {
						a.toast('Pick a picture first.', 'bad');
						return false;
					}
					const dir = folder.value.replace(/\\/g, '/').replace(/\/+$/, '');
					if (!dir.split('/')[0].toLowerCase().endsWith('.rte') || a.lockedGameFile(dir)) {
						a.toast('Save the sprite inside your own mod\'s folder (it starts with YourMod.rte/).', 'bad');
						return false;
					}
					const name = safeName(base.value);
					const filePath = dir + '/' + name + '.png';
					let paths;
					try {
						paths = await saveSprite(part, filePath, converted, centre.checked && !layerKey, layerKey);
					} catch (e) {
						a.toast('Couldn\'t save the sprite: ' + e.message, 'bad');
						return false;
					}
					a.toast('Saved ' + paths.length + ' frame' + (paths.length > 1 ? 's' : '') + ' to ' + dir + '. Save to keep the unit change.', 'good');
					if (options && options.onDone) {
						options.onDone(part);
					}
				},
			},
		]);
		if (options && options.files && options.files.length) {
			// Pictures that came with the request fill the picker as if they had been chosen in it.
			const transfer = new DataTransfer();
			for (const f of options.files) {
				transfer.items.add(f);
			}
			input.files = transfer.files;
			input.onchange();
		}
	}

	// ---- help ----

	const GLOSSARY = [
		['Preset', 'is one named thing in the game: a unit, a gun, a helmet, a sound. Everything you make is a preset.'],
		['Mod', 'is a folder (its name ends in .rte) that holds your presets and pictures. The game loads every mod when it starts.'],
		['Copy of', 'means a preset starts as a copy of another and only changes what you tell it. That is how Make my own copy works, and why the original stays safe.'],
		['Part', 'is a piece of a unit or gun that is fixed to another piece: the head on the body, an arm, a magazine.'],
		['Pivot', 'is the spot a part turns around, where it joins the part it is fixed to.'],
		['Sprite and frame', 'A sprite is a picture; a frame is one picture in a row of them, like the stages of an arm bending or a walk.'],
		['Skeleton', 'shows the bones of a unit: how its parts are joined and where.'],
		['Palette', 'is the fixed set of colours the game draws with. Studio moves your pictures to the nearest colours for you.'],
		['Group', 'is a shelf in the buy menu, like Weapons or Actors.'],
		['Script', 'is a Lua file that makes something do extra things while the game runs.'],
	];

	function help() {
		const keys = [
			['Drag a part', 'move it on its parent (ParentOffset)'],
			['Shift + drag', 'move the picture around the part\'s pivot (SpriteOffset)'],
			['Ctrl + drag', 'rotate an attachment about its pivot (InheritedRotAngleDegOffset); Alt for half degrees'],
			['Drag the gold dot', 'move the pivot, keeping the picture still (JointOffset)'],
			['Drag a hand', 'where it rests (IdleOffset) or holds the item (StanceOffset, SupportOffset)'],
			['Drag the red dot', 'where shots come from (MuzzleOffset)'],
			['Arrows / Shift+arrows', 'nudge the part / its picture by a pixel'],
			['Alt while dragging', 'half-pixel steps'],
			['Wheel, double-click', 'zoom, fit the unit in the view'],
			['1–5', 'stand, walk, run, crouch, jump'],
			['Space, F', 'play or pause, face the other way'],
			['I', 'fade everything but the selected part'],
			['V', 'hide or show the unit view, to edit numbers with the game beside Studio'],
			['L', 'hide or show the list of units, attachments and items'],
			['Delete', 'remove the selected attachment'],
			['Ctrl+Z, Ctrl+Y, Ctrl+S', 'undo, redo, save'],
			['Ctrl+P', 'Script mode: go to any file'],
			['Ctrl+Shift+F', 'Script mode: search every INI file and script'],
			['Ctrl+J', 'Script mode: hide or show the console panel'],
			['Ctrl+Enter', 'Script mode: run the selected Lua in the game'],
			['F12, Ctrl+click', 'Script mode: go to where something is defined'],
			['Ctrl+W', 'Script mode: close the file'],
		];
		const grid = el('div', { className: 'help-keys' });
		for (const [k, v] of keys) {
			const kbd = el('div');
			kbd.innerHTML = k.split(/(, | \/ | \+ |–)/).map((s) => (/^(, | \/ | \+ |–)$/.test(s) ? s : '<kbd>' + s.replace(/</g, '&lt;') + '</kbd>')).join('');
			grid.append(kbd, el('div', { textContent: v }));
		}
		const body = el('div', {},
			para('<b>Start</b> by picking a unit in the library. The game\'s own units can\'t be changed in place: press <b>Make my own copy</b> and Studio makes a unit in your mod that copies it, and writes your changes into that.'),
			para('<b>Shared parts.</b> Units often share a head, arms or helmets with others. Changing or removing a shared part here copies it into your unit first, so the others are untouched. A lock in the parts list means the part can\'t be changed from this unit, and says why.'),
			para('<b>Grey values</b> come from the preset this unit copies; <i>faint italic</i> ones aren\'t set and the game uses its default. Green notes mean the value is written in your unit.'),
			para('<b>Design and Script.</b> Design mode is this designer. Script mode (the switch at the top left) is the whole mod: its files, Lua scripts with autocomplete from the game\'s own API, the game\'s console, search and packaging. Saving a script reloads it in a game started from Tools\\ModStudio.'),
			para('<b>What you see</b> is placed the way the game places it, but without physics: in the game the body settles on its legs and walking feet push along the ground. <b>Preview in game</b> shows the real thing.'),
			el('h4', { textContent: 'Words Studio uses' }),
			...GLOSSARY.map(([word, meaning]) => para('<b>' + word + '</b> ' + meaning)),
			el('h4', { textContent: 'Keys and the mouse' }),
			grid);
		open('Using Studio', body, [{ text: 'Close' }], true);
	}

	function previewHelp() {
		const body = el('div', {},
			para('Studio saved your unit and asked the game to show it. To see it:'),
			para('1. Start the game with <b>Actor Editor.bat</b>, <b>Test In Game.bat</b> (the Maginot map, no sandbox) or <b>Test In Sandbox.bat</b> in Tools\\ModStudio (the Actor Editor is also in the main menu under <b>Editors</b>).'),
			para('2. It shows the unit Studio sent, holding the item picked under <b>Held item</b>, and swaps in the new version every time you press <b>Preview in game</b> again. Walk it, jump, shoot and get shot to see how it really behaves. In the Actor Editor, Alt lets go of the mouse so you can use Studio beside it, a click on the game takes it back, and Ctrl + mouse wheel zooms. In the sandbox the unit is your own character.'),
			para('Turn on <b>Live</b> next to Preview in game and every change is sent by itself, a drag once you let go.'),
			para('The game reads mods when it starts: after making a <b>new</b> mod, restart the game once. New sprites and changed values show straight away.'));
		open('Preview in game', body, [{ text: 'Got it', primary: true }]);
	}

	MS.dialogs = { makeCopy, open, close, copyUnit, attach, pickItem, importSprite, saveSprite, help, previewHelp };
	// For the New… wizard (wizard.js) and the data editors (editors.js).
	MS.dialogs.parts = { el, field, para, safeName, modChoices, modPicker, makeCopy, pickerGrid };
})();
