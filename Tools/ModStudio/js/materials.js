// Cortex Mod Studio: the Materials data editor (Data view). A material is what a terrain pixel is made of: its number (Index, the number a building
// piece's material picture uses for it), its colour when it is knocked loose, how it bounces and holds, and for liquids and powders how it moves.
//   Index map   the 256 material numbers: which are taken, by what, and which are free for a new material
//   Material    its settings in groups: grip and strength, weight, behaviour (burns, flows, piles), look (textures)
// A new material is a copy of one you pick, on the first free number, written in the mod's own Materials file. Two materials on the same number
// make the game mix them up, so clashes are marked in the index map.
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
	const hex = (rgb) => '#' + rgb.map((v) => Math.max(0, Math.min(255, Math.round(v))).toString(16).padStart(2, '0')).join('');

	// [key, label, help, kind]. kind: number, bool, or a list of choices.
	const GROUPS = [
		['Strength and grip', [
			['Bounce', 'Bounce', '0 loses all its energy in a hit, 1 keeps it all', 'num'],
			['Friction', 'Friction', '0 glides, 1 snags on everything', 'num'],
			['StructuralIntegrity', 'Strength', 'The impulse that knocks a pixel loose (kg·m/s); -1 never breaks', 'num'],
			['DensityKGPerVolumeL', 'Density', 'Kilograms a litre weighs: water 1, metals around 8', 'num'],
			['Priority', 'Priority', 'Which material shows where two overlap: higher wins', 'num'],
			['Stickiness', 'Stickiness', 'How well things stick to it', 'num'],
			['Metalness', 'Metalness', 'How metallic it shines', 'num'],
			['Gloss', 'Gloss', 'How glossy it looks', 'num'],
		]],
		['Hurting actors', [
			['GibImpulseLimitPerVolumeL', 'Tears limbs at', 'Force a litre of an actor can take against it before limbs tear', 'num'],
			['GibWoundLimitPerVolumeL', 'Wounds a litre takes', 'Wounds per litre before something breaks', 'num'],
			['TouchDamage', 'Touch damage', 'Harm to what touches it', 'num'],
		]],
		['When it is knocked loose', [
			['SettleMaterial', 'Settles as', 'The material number it turns into when it lands', 'num'],
			['TransformsInto', 'Turns into', 'The material number it becomes when knocked loose (0: itself)', 'num'],
			['UseOwnColor', 'Keeps its colour', 'Loose pixels use this material\'s colour', 'bool'],
			['IsScrap', 'Is scrap', 'Counts as scrap for collecting', 'bool'],
		]],
		['Liquids and powders', [
			['Flows', 'Flows', 'It runs like a liquid', 'bool'],
			['Powder', 'Powder', 'It piles up like sand', 'bool'],
			['Piling', 'Piling', 'How it piles', 'num'],
			['Viscosity', 'Viscosity', 'How thick it runs', 'num'],
			['FlowSpeed', 'Flow speed', 'How fast it spreads', 'num'],
			['FallSpeed', 'Fall speed', 'How fast it drops', 'num'],
			['Gravity', 'Gravity', 'How hard it is pulled down', 'num'],
			['SlideChance', 'Slide chance', 'How likely a pile is to slide', 'num'],
			['MoveEvery', 'Moves every', 'Frames between steps', 'num'],
			['LiquidWeight', 'Liquid weight', 'How heavy it is to swim through', 'num'],
			['SightDepth', 'Sight depth', 'How far into it a unit can see', 'num'],
			['ShotDepth', 'Shot depth', 'How far into it a shot goes', 'num'],
			['Breathable', 'Breathable', 'A unit can breathe in it', 'bool'],
			['Burns', 'Burns', 'It catches fire', 'bool'],
		]],
	];

	const materialsOf = (module) => app().lib.presets.filter((p) => p.topLevel && p.cls === 'Material' && p.module === module);
	const indexOf = (p) => app().lib.resolve(p.node).num('Index', -1);
	const colorOf = (p) => {
		const color = app().lib.resolve(p.node).sub('Color');
		return color ? [color.num('R', 0), color.num('G', 0), color.num('B', 0)] : [60, 62, 74];
	};

	// Material number -> the materials of one module (and of Base.rte, whose numbers a mod's pictures may use too) that have it. The game gives
	// each module's materials their own place when it loads, so numbers only clash inside a module, or with the base game's.
	function indexUsers(module) {
		const map = new Map();
		for (const p of app().lib.presets.filter((x) => x.topLevel && x.cls === 'Material' && (x.module === module || x.module === 'Base.rte'))) {
			const i = indexOf(p);
			if (i >= 0) {
				map.set(i, [...(map.get(i) || []), p]);
			}
		}
		return map;
	}

	const firstFree = (users) => {
		for (let i = 255; i > 0; i--) {
			if (!users.has(i)) {
				return i;
			}
		}
		return -1;
	};

	// ---- the editor ----

	function render(module) {
		return (host, tab) => {
			const a = app();
			if (!a.lib || !a.lib.presets.length) {
				host.replaceChildren(el('p', { className: 'ed ed-empty', textContent: 'Studio is still reading the game…' }));
				return;
			}
			const s = tab.state || (tab.state = { filter: '', selected: null });
			const all = materialsOf(module).sort((x, y) => indexOf(x) - indexOf(y));
			const q = s.filter.trim().toLowerCase();
			const list = all.filter((p) => !q || p.name.toLowerCase().includes(q) || String(indexOf(p)) === q);
			const keyOf = (p) => p.doc.path + ':' + p.node.line + ':' + p.name;
			const selected = all.find((p) => keyOf(p) === s.selected) || list[0] || null;
			const filter = el('input', { type: 'search', placeholder: 'Filter ' + all.length + ' materials…', value: s.filter, className: 'ed-input' });
			filter.dataset.fk = 'filter';
			filter.oninput = () => {
				s.filter = filter.value;
				clearTimeout(s.timer);
				s.timer = setTimeout(() => MS.ide.refreshForm(), 200);
			};
			const ro = !all[0] || locked(all[0].doc);
			const users = indexUsers(module);
			const items = list.map((p) => {
				const i = indexOf(p);
				const clash = (users.get(i) || []).length > 1;
				return el('button', { className: 'ed-item mat-item' + (p === selected ? ' sel' : ''), title: clash ? 'Another material in this mod (or the base game) has this number' : '', onclick: () => { s.selected = keyOf(p); MS.ide.refreshForm(); } },
					el('span', { className: 'mat-chip', style: 'background:' + hex(colorOf(p)) }), el('span', { className: 'mat-num', textContent: String(i) }), p.name, clash ? el('span', { className: 'mat-clash', textContent: '⚠' }) : null);
			});
			host.replaceChildren(el('div', { className: 'ed ed-split' },
				el('div', { className: 'ed-head' }, el('h2', { textContent: 'Materials · ' + module }), el('span', { className: 'spacer' }),
					el('button', { className: 'tb', textContent: '+ New material…', disabled: ro && !!all.length, title: selected ? 'A copy of ' + selected.name + ' on a free number' : 'A new material', onclick: () => newMaterial(module, selected, s) })),
				all[0] && ro ? el('div', { className: 'ed-banner', textContent: module + ' is read only: its materials can be looked at. Make a material of your own with + New material (in your mod).' }) : null,
				el('div', { className: 'ed-panes' },
					el('div', { className: 'ed-list' }, filter, ...(items.length ? items : [el('p', { className: 'ed-empty', textContent: 'No materials' + (q ? ' match.' : ' in this mod yet.') })])),
					el('div', { className: 'ed-detail' }, indexMap(users, s, all, keyOf), selected ? detail(selected, users) : null))));
		};
	}

	// The 256 numbers as a grid: each in its material's colour, free ones dark; a number with two materials is ringed in red.
	function indexMap(users, s, mine, keyOf) {
		const canvas = el('canvas', { className: 'mat-map', width: 256, height: 256, title: 'Material numbers 0-255: dark ones are free' });
		const ctx = canvas.getContext('2d');
		for (let i = 0; i < 256; i++) {
			const x = (i % 16) * 16;
			const y = Math.floor(i / 16) * 16;
			const found = users.get(i);
			ctx.fillStyle = found ? hex(colorOf(found[0])) : '#161a22';
			ctx.fillRect(x, y, 15, 15);
			if (found && found.length > 1) {
				ctx.strokeStyle = '#ef6a5b';
				ctx.lineWidth = 2;
				ctx.strokeRect(x + 1, y + 1, 13, 13);
			}
			if (found && found.some((p) => mine.includes(p) && keyOf(p) === s.selected)) {
				ctx.strokeStyle = '#fff';
				ctx.lineWidth = 2;
				ctx.strokeRect(x + 0.5, y + 0.5, 14, 14);
			}
		}
		canvas.onpointermove = (e) => {
			const r = canvas.getBoundingClientRect();
			const i = Math.floor(((e.clientY - r.top) / r.height) * 16) * 16 + Math.floor(((e.clientX - r.left) / r.width) * 16);
			const found = users.get(i);
			canvas.title = i + ': ' + (found ? found.map((p) => p.name + ' (' + p.module + ')').join(', ') : 'free');
		};
		canvas.onclick = (e) => {
			const r = canvas.getBoundingClientRect();
			const i = Math.floor(((e.clientY - r.top) / r.height) * 16) * 16 + Math.floor(((e.clientX - r.left) / r.width) * 16);
			const found = (users.get(i) || []).find((p) => mine.includes(p));
			if (found) {
				s.selected = keyOf(found);
				MS.ide.refreshForm();
			}
		};
		return el('div', {}, el('div', { className: 'ed-label', textContent: 'Material numbers (free ones are dark)' }), canvas);
	}

	function detail(p, users) {
		const a = app();
		const e = a.lib.resolve(p.node);
		const ro = locked(p.doc);
		const set = (label, key, value) => change('Change ' + p.name + '\'s ' + label.toLowerCase(), () => {
			if (value === null) {
				const old = p.node.child(key);
				if (old) {
					old.remove();
				}
			} else {
				p.node.set(key, String(value));
			}
		});
		const index = indexOf(p);
		const clash = (users.get(index) || []).filter((x) => x !== p);
		const color = colorOf(p);
		const picker = el('input', { type: 'color', value: hex(color), disabled: ro, title: 'The colour its pixels have when knocked loose, and in the material picture' });
		picker.onchange = () => change('Recolour ' + p.name, () => {
			const node = p.node.child('Color') || p.node.add('Color', 'Color');
			node.setValue('Color');
			const rgb = [1, 3, 5].map((i) => parseInt(picker.value.slice(i, i + 2), 16));
			['R', 'G', 'B'].forEach((k, i) => node.set(k, String(rgb[i])));
		});
		const indexInput = el('input', { type: 'number', min: 0, max: 255, className: 'ed-input num', value: index, disabled: ro });
		indexInput.onchange = () => set('number', 'Index', Math.max(0, Math.min(255, Math.round(Number(indexInput.value) || 0))));
		const groups = GROUPS.map(([title, fields]) => {
			const rows = fields.map(([key, label, help, kind]) => {
				const own = p.node.child(key);
				const now = e.str(key, '');
				if (kind === 'bool') {
					const box = el('input', { type: 'checkbox', checked: e.bool(key, false), disabled: ro });
					box.onchange = () => set(label, key, box.checked ? 1 : 0);
					return el('label', { className: 'ed-check wide', title: help }, box, label);
				}
				const input = el('input', { type: 'number', step: 'any', className: 'ed-input num', disabled: ro, value: own ? own.value : '', placeholder: now });
				input.onchange = () => set(label, key, input.value.trim() === '' ? null : input.value.trim());
				input.dataset.fk = 'mat:' + key;
				return el('label', { className: 'ed-setting', title: help }, el('span', { textContent: label }), input, el('small', { textContent: help }));
			});
			return [el('div', { className: 'ed-label', textContent: title }), el('div', { className: 'ed-settings' }, ...rows)];
		});
		const look = ['FGTextureFile', 'BGTextureFile'].map((key) => {
			const file = e.file(key);
			const canvas = el('canvas', { width: 64, height: 64, className: 'ed-thumb mat-tex' });
			const frame = file ? a.sprite(file, 1, 0) : null;
			if (frame) {
				const ctx = canvas.getContext('2d');
				ctx.imageSmoothingEnabled = false;
				ctx.drawImage(frame.canvas, 0, 0, 64, 64);
			}
			return el('div', { className: 'mat-look' }, canvas, el('span', {}, el('b', { textContent: key === 'FGTextureFile' ? 'Foreground texture' : 'Background texture' }), el('br'), el('small', { textContent: file || '(none)' })));
		});
		return el('div', {},
			el('div', { className: 'ed-card-head' }, el('h3', { textContent: p.name }), el('span', { className: 'spacer' }),
				el('button', { className: 'ico-btn', textContent: 'Its lines', onclick: () => MS.ide.openFile(p.doc.path, p.node.line) })),
			clash.length ? el('div', { className: 'ed-banner', textContent: 'Number ' + index + ' is also used by ' + clash.map((x) => x.name + ' (' + x.module + ')').join(', ') + '. The game mixes them up: give one a free number.' }) : null,
			el('div', { className: 'ed-row' }, el('span', { className: 'ed-label', textContent: 'Number' }), indexInput, el('span', { className: 'ed-label', textContent: 'Colour' }), picker),
			...groups.flat(), el('div', { className: 'ed-label', textContent: 'Look' }), ...look);
	}

	function newMaterial(module, base, s) {
		const a = app();
		const users = indexUsers(module);
		const free = firstFree(users);
		if (free < 0) {
			a.toast('All 255 material numbers are taken.', 'bad');
			return;
		}
		const name = el('input', { type: 'text', placeholder: 'My Material' });
		const info = base ? 'It starts as a copy of ' + base.name + ' on number ' + free + ', the highest free one.' : 'It goes on number ' + free + ', the highest free one.';
		MS.dialogs.open('New material', MS.dialogs.parts.field('Name', name, info), [
			{ text: 'Cancel' },
			{ text: 'Make it', primary: true, action: async () => {
				const n = name.value.trim();
				if (!n) {
					return false;
				}
				if (a.lib.presets.some((p) => p.cls === 'Material' && p.name.toLowerCase() === n.toLowerCase())) {
					a.toast('There is already a material called ' + n + '.', 'bad');
					return false;
				}
				const doc = await MS.editors.docFor(module, 'Material', 'Materials.ini');
				change('New material ' + n, () => {
					const block = doc.addTopLevel('AddMaterial', 'Material');
					if (base) {
						block.add('CopyOf', a.lib.reference(base.name, base.module, module));
					}
					block.add('PresetName', n);
					block.add('Index', String(free));
					if (!base) {
						block.add('Bounce', '0.5');
						block.add('Friction', '0.5');
						block.add('StructuralIntegrity', '50');
						block.add('DensityKGPerVolumeL', '2');
						block.add('Priority', '10');
						block.add('Color', 'Color').setValue('Color');
						const color = block.child('Color');
						color.add('R', '150');
						color.add('G', '150');
						color.add('B', '150');
					}
				});
				const made = a.lib.presets.find((p) => p.doc === doc && p.name === n);
				if (made) {
					s.selected = made.doc.path + ':' + made.node.line + ':' + made.name;
				}
				MS.ide.refreshForm();
				return true;
			} },
		]);
	}

	if (MS.editors && MS.editors.register) {
		MS.editors.register(['materials', 'Materials', 'What terrain is made of: numbers, colours, grip, flow', '▦', render, (module) => materialsOf(module).length]);
	}
	MS.materials = { render };
})();
