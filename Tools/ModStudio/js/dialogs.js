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
		const last = a.readLocal('ms.lastMod');
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
		select.value = a.readLocal('ms.lastMod') && mods.includes(a.readLocal('ms.lastMod')) ? a.readLocal('ms.lastMod') : mods[0] || '__new';
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

	function copyUnit() {
		const a = app();
		if (!a.actor) {
			return;
		}
		const mod = modPicker();
		const name = el('input', { type: 'text', value: a.actor.name + ' Custom' });
		const body = el('div', {},
			para('Studio makes a new unit in your mod that starts as a copy of <b></b> (<code>CopyOf</code>). Everything you change is written into the copy; the original and the game\'s own files stay as they are.'),
			field('Your mod', mod.node, 'Mods live in the game\'s Mods folder. A new mod is made for you.'),
			field('New unit\'s name', name, 'This is the name in the buy menu. It has to be different from every other unit\'s name.'));
		body.querySelector('b').textContent = a.actor.name;
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
					if (a.lib.find(unitName, a.actor.cls, modName) && a.lib.find(unitName, a.actor.cls, modName).name.toLowerCase() === unitName.toLowerCase()) {
						a.toast('There is already a unit called ' + unitName + '.', 'bad');
						return false;
					}
					await makeCopy(modName, unitName);
				},
			},
		]);
	}

	async function makeCopy(modName, unitName) {
		const a = app();
		const lib = a.lib;
		const path = modName + '/Actors/' + safeName(unitName) + '.ini';
		let doc = lib.docFor(path);
		if (!doc) {
			if (await a.fs.exists(path)) {
				a.toast(path + ' already exists but isn\'t part of the mod. Pick another name.', 'bad');
				return;
			}
			const hadModule = !!lib.moduleOf(path);
			doc = await lib.newDoc(path);
			a.hookDoc(doc);
			if (!hadModule) {
				a.hookDoc(lib.docFor(modName + '/Index.ini'));
				a.toast('Made the new mod ' + modName + '. The game loads new mods when it starts, so restart it once before previewing.', 'good');
			}
		}
		const original = a.actor;
		const block = doc.addTopLevel('AddActor', original.cls);
		block.add('CopyOf', lib.reference(original.name, original.path.split('/')[0], modName));
		block.add('PresetName', unitName);
		lib.reindex();
		a.writeLocal('ms.lastMod', modName);
		const preset = lib.presets.find((p) => p.doc === doc && p.name === unitName);
		a.undo = [];
		a.redo = [];
		a.openActor(preset);
		MS.panels.showLibrary();
		a.updateFileStatus();
		a.toast('Made ' + unitName + ' in ' + modName + '. Save to write it to disk.', 'good');
	}

	// ---- pickers ----

	function pickerGrid(presets, onPick, thumb) {
		const grid = el('div', { className: 'pick-grid' });
		const search = el('input', { type: 'search', placeholder: 'Search…' });
		const render = () => {
			grid.innerHTML = '';
			const q = search.value.trim().toLowerCase();
			const list = presets.filter((p) => !q || p.name.toLowerCase().includes(q) || p.module.toLowerCase().includes(q)).slice(0, 240);
			for (const p of list) {
				const canvas = el('canvas', { width: 64, height: 64 });
				const tile = el('div', { className: 'pick', title: p.name + ' (' + p.module + ')' }, canvas, el('div', { textContent: p.name }), el('small', { textContent: p.module }));
				tile.onclick = () => {
					close();
					onPick(p);
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
		return el('div', {}, field('Search', search), grid);
	}

	function attachablePresets() {
		const seen = new Set();
		return app().lib.presets.filter((p) => {
			if (!MS.panels.TAB_CLASSES.attachments.has(p.cls)) {
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
			open('Add an attachment to ' + part.name, pickerGrid(attachablePresets(), (p) => attach(p, part), (c, p) => MS.panels.drawSpriteThumb(c, p)), [{ text: 'Cancel' }], true);
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
		const items = a.lib.presets.filter((p) => MS.panels.TAB_CLASSES.items.has(p.cls) && p.topLevel);
		open('Put an item in the unit\'s hand', pickerGrid(items, (p) => a.setDevice(p), (c, p) => MS.panels.drawSpriteThumb(c, p)), [{ text: 'Cancel' }], true);
	}

	// ---- sprite import ----

	function importSprite(part) {
		const a = app();
		const reason = a.lockReason(part);
		if (reason) {
			a.toast(reason, 'bad');
			return;
		}
		const input = el('input', { type: 'file', accept: 'image/png,image/bmp,image/gif,image/webp,image/jpeg', multiple: true });
		const framesInput = el('input', { type: 'number', min: 1, max: 64, value: 1 });
		const centre = el('input', { type: 'checkbox', checked: true });
		const actorName = safeName(a.actor.name);
		const roleName = { body: 'Body', head: 'Head', jetpack: 'Jetpack', fgarm: 'ArmFG', bgarm: 'ArmBG', fgleg: 'LegFG', bgleg: 'LegBG', foot: 'Foot', magazine: 'Magazine', device: 'Item', attachable: 'Attachment' }[part.role] || 'Part';
		const module = part.anchor.doc.module;
		const folder = el('input', { type: 'text', value: module + '/Sprites/' + actorName });
		const base = el('input', { type: 'text', value: roleName });
		const preview = el('div', { className: 'import-preview' });
		const info = el('p', { className: 'small' });
		let sources = []; // ImageData per file
		let converted = [];

		const rebuild = () => {
			converted = [];
			preview.innerHTML = '';
			if (!sources.length) {
				info.textContent = '';
				return;
			}
			let frames = [];
			if (sources.length === 1) {
				const img = sources[0];
				const n = Math.max(1, Math.min(64, parseInt(framesInput.value, 10) || 1));
				const w = Math.floor(img.width / n);
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
			info.textContent = converted.length + ' frame' + (converted.length > 1 ? 's' : '') + ', ' + converted[0].width + '×' + converted[0].height + ' pixels. ' + (changed ? changed + ' pixels moved to the nearest game colour (shown above as the game will draw them).' : 'Every colour is already in the game palette.');
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

		const limbNote = part.role === 'fgarm' || part.role === 'bgarm' || part.role === 'fgleg' || part.role === 'bgleg' ? 'Arms and legs choose their frame by how far they stretch: the first frame is the most bent, the last the straightest. Feet use frames 0–3 for flat, toes forward, heel back and far back.' : null;
		const body = el('div', {},
			para('Pick a picture (or several, one per frame, or one sprite sheet with the frames side by side). Studio turns it into the game\'s palette and saves the frames as numbered files the game can load.'),
			field('Picture files', input),
			field('Frames in a sprite sheet', framesInput, 'For one picture: how many equal frames it is cut into, left to right.'),
			preview, info,
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
					if (!dir.split('/')[0].toLowerCase().endsWith('.rte') || a.fs.isOfficial(dir)) {
						a.toast('Save the sprite inside your own mod\'s folder (it starts with YourMod.rte/).', 'bad');
						return false;
					}
					const name = safeName(base.value);
					const filePath = dir + '/' + name + '.png';
					const paths = MS.Images.framePaths(filePath, converted.length);
					try {
						for (let i = 0; i < converted.length; i++) {
							const c = converted[i];
							await a.fs.writeFile(paths[i], await a.images.encodePng(c.width, c.height, c.indices));
						}
					} catch (e) {
						a.toast('Couldn\'t save the sprite: ' + e.message, 'bad');
						return false;
					}
					a.forgetSprites(paths);
					MS.panels.forgetThumbs();
					const w = converted[0].width;
					const h = converted[0].height;
					a.edit('replace the sprite of ' + part.name, () => {
						const node = a.ownBlock(part);
						if (!node) {
							return;
						}
						let sf = node.children.find((c) => c.key === 'SpriteFile');
						if (!sf) {
							sf = node.add('SpriteFile', 'ContentFile', node.insertIndexFor('SpriteFile'));
						}
						sf.setValue('ContentFile');
						for (const old of sf.children.filter((c) => c.key === 'Path' || c.key === 'CopyOf')) {
							old.remove();
						}
						sf.set('FilePath', filePath);
						node.set('FrameCount', String(converted.length));
						if (centre.checked) {
							node.setVector('SpriteOffset', -Math.floor(w / 2), -Math.floor(h / 2));
						}
					});
					a.toast('Saved ' + paths.length + ' frame' + (paths.length > 1 ? 's' : '') + ' to ' + dir + '. Save to keep the unit change.', 'good');
				},
			},
		]);
	}

	// ---- help ----

	function help() {
		const keys = [
			['Drag a part', 'move it on its parent (ParentOffset)'],
			['Shift + drag', 'move the picture around the part\'s pivot (SpriteOffset)'],
			['Drag the gold dot', 'move the pivot, keeping the picture still (JointOffset)'],
			['Drag a hand', 'where it rests (IdleOffset) or holds the item (StanceOffset, SupportOffset)'],
			['Drag the red dot', 'where shots come from (MuzzleOffset)'],
			['Arrows / Shift+arrows', 'nudge the part / its picture by a pixel'],
			['Alt while dragging', 'half-pixel steps'],
			['Wheel, double-click', 'zoom, fit the unit in the view'],
			['1–5', 'stand, walk, run, crouch, jump'],
			['Space, F', 'play or pause, face the other way'],
			['I', 'fade everything but the selected part'],
			['Delete', 'remove the selected attachment'],
			['Ctrl+Z, Ctrl+Y, Ctrl+S', 'undo, redo, save'],
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
			para('<b>What you see</b> is placed the way the game places it, but without physics: in the game the body settles on its legs and walking feet push along the ground. <b>Preview in game</b> shows the real thing.'),
			grid);
		open('Using Mod Studio', body, [{ text: 'Close' }], true);
	}

	function previewHelp() {
		const body = el('div', {},
			para('Studio saved your unit and asked the game to show it. To see it:'),
			para('1. Start the game with <b>Actor Editor.bat</b> or <b>Test In Sandbox.bat</b> in Tools\\ModStudio (the Actor Editor is also in the main menu under <b>Editors</b>).'),
			para('2. It shows the unit Studio sent, holding the item picked under <b>Held item</b>, and swaps in the new version every time you press <b>Preview in game</b> again. Walk it, jump, shoot and get shot to see how it really behaves. In the Actor Editor, Ctrl + mouse wheel zooms.'),
			para('The game reads mods when it starts: after making a <b>new</b> mod, restart the game once. New sprites and changed values show straight away.'));
		open('Preview in game', body, [{ text: 'Got it', primary: true }]);
	}

	MS.dialogs = { open, close, copyUnit, attach, pickItem, importSprite, help, previewHelp };
})();
