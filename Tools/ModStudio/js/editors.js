// Cortex Mod Studio: data editors, for the game's data that isn't a unit to design. Each opens as a tab in Script mode (ide.openForm)
// from the Data view in the activity bar, for one module at a time:
//   Buy menu   every unit, craft and item: cost, whether it's in the buy menu, and its groups (AddToGroup)
//   Loadouts   the module's loadouts (AddLoadout): the craft they come in and what they bring, picked from thumbnails
//   Sounds     the module's sound presets (SoundContainer): their files, to play, add and remove, and how they play
// Changes are ordinary edits (app.edit), so Undo and Save work as everywhere else. The game's own modules and other people's mods are
// read only, as in the designer (library.js isProtected), unless Studio is unlocked.
'use strict';
window.MS = window.MS || {};

(function () {
	const $ = (id) => document.getElementById(id);
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

	const UNITS = new Set(['AHuman', 'ACrab']);
	const CRAFT = new Set(['ACDropShip', 'ACRocket']);
	const ITEMS = new Set(['HDFirearm', 'HeldDevice', 'TDExplosive', 'ThrownDevice']);
	const STANDARD_GROUPS = [
		'Actors', 'Actors - Light', 'Actors - Heavy', 'Actors - Creatures', 'Actors - Turrets', 'Brains', 'Craft',
		'Weapons', 'Weapons - Primary', 'Weapons - Secondary', 'Weapons - Light', 'Weapons - Heavy', 'Weapons - Sniper', 'Weapons - Explosive', 'Weapons - CQB', 'Weapons - Melee',
		'Bombs', 'Bombs - Grenades', 'Tools', 'Powerups', 'Non-combatants',
	];

	const presetsOf = (module, test) => app().lib.presets.filter((p) => p.topLevel && p.module === module && test(p));
	const locked = (doc) => app().lib.isProtected(doc);
	const thumbFor = (canvas, p) => MS.panels.queueThumb(canvas, () => (UNITS.has(p.cls) || CRAFT.has(p.cls) ? MS.panels.drawRigThumb(canvas, p) : MS.panels.drawSpriteThumb(canvas, p)));
	const thumb = (p, size) => {
		const c = el('canvas', { width: size || 36, height: size || 36, className: 'ed-thumb' });
		if (p) {
			thumbFor(c, p);
		}
		return c;
	};
	const change = (label, fn) => app().edit(label, fn);
	// An editor opened while Studio is still reading the game waits for it (the next refresh draws it).
	const whenRead = (render) => (host, tab) => (app().lib && app().lib.presets.length ? render(host, tab) : host.replaceChildren(el('p', { className: 'ed ed-empty', textContent: 'Studio is still reading the game…' })));

	function lockBanner(doc, module, extra) {
		if (!doc || !locked(doc)) {
			return null;
		}
		return el('div', { className: 'ed-banner' }, app().lib.lockKind(doc) === 'game'
			? module + ' is one of the game\'s own modules, so this is read only. ' + (extra || 'Use New… to make your own things') + ', or Unlock everything on the Game screen to change it in place.'
			: module + ' is someone else\'s mod: read only until you choose Edit this mod on one of its units.');
	}

	// Picks a preset from thumbnails: classes is a Set of the kinds to offer.
	function pick(title, classes, onPick) {
		const presets = app().lib.presets.filter((p) => p.topLevel && classes.has(p.cls)).sort((a, b) => a.name.localeCompare(b.name));
		const draw = (c, p) => (UNITS.has(p.cls) || CRAFT.has(p.cls) ? MS.panels.drawRigThumb(c, p) : MS.panels.drawSpriteThumb(c, p));
		MS.dialogs.open(title, MS.dialogs.parts.pickerGrid(presets, onPick, draw), [{ text: 'Cancel' }], true);
	}

	// The doc a new block of this kind goes in for an own mod: one that has some already, else <mod>/<file>.
	async function docFor(module, cls, file) {
		const lib = app().lib;
		const existing = presetsOf(module, (p) => p.cls === cls)[0];
		if (existing) {
			return existing.doc;
		}
		const path = module + '/' + file;
		let doc = lib.docFor(path);
		if (!doc) {
			doc = await lib.newDoc(path);
			app().hookDoc(doc);
			app().hookDoc(lib.docFor(module + '/Index.ini'));
		}
		return doc;
	}

	const resolvePreset = (node, cls, module) => (node ? app().lib.find(node.value, cls, module) : null);
	const reference = (p, module) => app().lib.reference(p.name, p.module, module);

	// ---- buy menu ----

	function renderBuyMenu(module) {
		return whenRead((host, tab) => {
			const a = app();
			const s = tab.state || (tab.state = { filter: '', kind: 'all', buyableOnly: false });
			const kinds = { all: (p) => UNITS.has(p.cls) || CRAFT.has(p.cls) || ITEMS.has(p.cls), units: (p) => UNITS.has(p.cls), craft: (p) => CRAFT.has(p.cls), items: (p) => ITEMS.has(p.cls) };
			const all = presetsOf(module, kinds[s.kind]);
			const filter = el('input', { type: 'search', placeholder: 'Filter ' + all.length + '…', value: s.filter, className: 'ed-input' });
			filter.dataset.fk = 'filter';
			filter.oninput = () => {
				s.filter = filter.value;
				clearTimeout(s.timer);
				s.timer = setTimeout(() => MS.ide.refreshForm(), 200);
			};
			const chips = el('div', { className: 'chips' }, ...Object.keys(kinds).map((k) => el('button', { className: s.kind === k ? 'sel' : '', textContent: k[0].toUpperCase() + k.slice(1), onclick: () => { s.kind = k; MS.ide.refreshForm(); } })));
			const buyableBox = el('input', { type: 'checkbox', checked: s.buyableOnly, onchange: () => { s.buyableOnly = buyableBox.checked; MS.ide.refreshForm(); } });
			const q = s.filter.trim().toLowerCase();
			const rows = all
				.map((p) => ({ p, e: a.lib.resolve(p.node) }))
				.filter(({ p, e }) => (!q || p.name.toLowerCase().includes(q) || e.all('AddToGroup').some((g) => g.node.value.toLowerCase().includes(q))) && (!s.buyableOnly || e.bool('Buyable', true)));
			const groups = new Map();
			for (const r of rows) {
				const first = (r.e.all('AddToGroup')[0] || { node: { value: 'No group' } }).node.value;
				if (!groups.has(first)) {
					groups.set(first, []);
				}
				groups.get(first).push(r);
			}
			const banner = lockBanner(all[0] && all[0].doc, module);
			const colHeads = () => el('div', { className: 'ed-cols buy' }, el('span'), el('span', { textContent: 'Name' }), el('span', { textContent: 'Cost' }), el('span', { textContent: 'In the buy menu' }), el('span', { textContent: 'Groups' }));
			const body = [];
			for (const [group, list] of [...groups.entries()].sort((x, y) => x[0].localeCompare(y[0]))) {
				body.push(el('section', { className: 'ed-buy-group' }, el('div', { className: 'ed-group' }, group + ' · ' + list.length), colHeads(),
					...list.sort((x, y) => x.p.name.localeCompare(y.p.name)).map(({ p, e }) => buyRow(p, e, module))));
			}
			host.replaceChildren(el('div', { className: 'ed' },
				el('div', { className: 'ed-head' }, el('h2', { textContent: 'Buy menu · ' + module }), el('span', { className: 'spacer' }), chips, el('label', { className: 'ed-check' }, buyableBox, 'Only buyable'), filter),
				banner,
				body.length ? el('div', { className: 'ed-buy-groups' }, ...body) : el('p', { className: 'ed-empty', textContent: 'Nothing here' + (q ? ' matches.' : '.') })));
		});
	}

	function buyRow(p, e, module) {
		const a = app();
		const ro = locked(p.doc);
		const key = p.doc.path + ':' + p.node.line;
		const cost = el('input', { type: 'number', min: 0, step: 'any', value: e.str('GoldValue', e.str('GoldCost', '')), className: 'ed-input num', disabled: ro, title: 'Gold it costs (GoldValue)' });
		cost.dataset.fk = 'cost:' + key;
		cost.onchange = () => change('Set ' + p.name + '\'s cost', () => p.node.set('GoldValue', cost.value.trim() || '0'));
		const buyable = el('input', { type: 'checkbox', checked: e.bool('Buyable', true), disabled: ro, title: 'Shown in the buy menu (Buyable)' });
		buyable.onchange = () => change((buyable.checked ? 'Show ' : 'Hide ') + p.name + ' in the buy menu', () => p.node.set('Buyable', buyable.checked ? '1' : '0'));
		const chips = el('div', { className: 'ed-chips' });
		for (const g of e.all('AddToGroup')) {
			const own = g.node.isWithin(p.node);
			const chip = el('span', { className: 'ed-chip' + (own ? '' : ' inherited'), title: own ? 'Click × to take it out of this group' : 'From what it copies (' + (g.node.doc ? g.node.doc.path : '') + ')' }, g.node.value);
			if (own && !ro) {
				chip.append(el('button', { textContent: '×', onclick: () => change('Take ' + p.name + ' out of ' + g.node.value, () => g.node.remove()) }));
			}
			chips.append(chip);
		}
		if (!ro) {
			const have = new Set(e.all('AddToGroup').map((g) => g.node.value));
			const add = el('select', { className: 'ed-add', title: 'Put it in another group' }, el('option', { value: '', textContent: '+ group' }), ...STANDARD_GROUPS.filter((g) => !have.has(g)).map((g) => el('option', { value: g, textContent: g })));
			add.onchange = () => add.value && change('Put ' + p.name + ' in ' + add.value, () => p.node.add('AddToGroup', add.value));
			chips.append(add);
		}
		const name = el('button', { className: 'ed-link', textContent: p.name, title: p.cls + ' · ' + p.doc.path + ':' + p.node.line + ' (open in the designer)', onclick: () => openInDesigner(p) });
		return el('div', { className: 'ed-cols buy row' }, thumb(p), el('div', { className: 'ed-name' }, name, el('small', { textContent: p.cls })), cost, buyable, chips);
	}

	function openInDesigner(p) {
		app().openActor(p);
		MS.ide.activate(MS.ide.tabs.find((t) => t.kind === 'designer'));
	}

	// ---- loadouts ----

	function renderLoadouts(module) {
		return whenRead((host) => {
			const a = app();
			const loadouts = presetsOf(module, (p) => p.cls === 'Loadout');
			const index = a.lib.docFor(module + '/Index.ini');
			const ro = index ? locked(index) : true;
			const dm = index && (index.root.children.find((n) => n.key === 'DataModule') || null);
			const faction = el('label', { className: 'ed-check', title: 'A faction can be picked as a team in skirmishes and the campaign, with these loadouts (IsFaction in Index.ini)' },
				el('input', { type: 'checkbox', checked: !!dm && /^(1|true)$/i.test(dm.childValue('IsFaction', '0')), disabled: ro || !dm, onchange: (ev) => change((ev.target.checked ? 'Make ' : 'Stop ') + module + ' a faction', () => dm.set('IsFaction', ev.target.checked ? '1' : '0')) }),
				'This module is a faction');
			const newBtn = el('button', { className: 'tb', textContent: '+ New loadout', disabled: ro, onclick: () => newLoadout(module) });
			const copyBtn = loadouts.length && ro ? el('button', { className: 'tb', textContent: 'Copy them into my mod…', onclick: () => copyLoadouts(module, loadouts) }) : null;
			host.replaceChildren(el('div', { className: 'ed' },
				el('div', { className: 'ed-head' }, el('h2', { textContent: 'Loadouts · ' + module }), el('span', { className: 'spacer' }), faction, copyBtn, newBtn),
				lockBanner(index, module, 'Press Copy them into my mod… above to get copies of these you can change'),
				el('p', { className: 'ed-note', textContent: 'A loadout is what a team orders in one go: the craft it arrives in and what it brings. The AI and the buy menu\'s Loadouts use them; one called Default is the team\'s usual order.' }),
				loadouts.length ? el('div', { className: 'ed-cards' }, ...loadouts.map((l) => loadoutCard(l, module))) : el('p', { className: 'ed-empty', textContent: module + ' has no loadouts yet.' })));
		});
	}

	function slot(p, missing, kindText, buttons) {
		return el('div', { className: 'ed-slot' + (p ? '' : ' missing') }, thumb(p, 40),
			el('div', { className: 'ed-name' }, el('span', { textContent: p ? p.name : missing || '(none)' }), el('small', { textContent: p ? p.cls + ' · ' + p.module : kindText })),
			...buttons);
	}

	function loadoutCard(l, module) {
		const ro = locked(l.doc);
		const node = l.node;
		const craftNode = node.child('DeliveryCraft');
		const craft = craftNode ? resolvePreset(craftNode.child('PresetName'), craftNode.value, module) : null;
		const name = el('input', { type: 'text', value: l.name, className: 'ed-input', disabled: ro });
		name.dataset.fk = 'name:' + l.doc.path + ':' + node.line;
		name.onchange = () => name.value.trim() && change('Rename loadout ' + l.name, () => node.set('PresetName', name.value.trim()));
		const craftSlot = slot(craft, craftNode ? craftNode.childValue('PresetName', '') + ' (not found)' : 'No craft', 'Delivery craft', ro ? [] : [
			el('button', { className: 'ico-btn', textContent: 'Change…', onclick: () => pick('The craft it comes in', CRAFT, (p) => change('Set ' + l.name + '\'s craft', () => {
				const c = node.child('DeliveryCraft') || node.add('DeliveryCraft', p.cls);
				c.setValue(p.cls);
				c.set('PresetName', reference(p, module));
			})) }),
		]);
		const cargo = node.children.filter((c) => c.key === 'AddCargoItem');
		const cargoRows = cargo.map((c, i) => {
			const p = resolvePreset(c.child('PresetName'), c.value, module);
			// Swaps it with the cargo before or after it: taken out, then put in where that one was (after it, going down).
			const move = (dir) => change('Move ' + (p ? p.name : 'cargo'), () => {
				const parent = c.parent;
				const at = parent.children.indexOf(cargo[i + dir]);
				c.remove();
				parent.add(c, undefined, at);
			});
			return slot(p, c.childValue('PresetName', '?') + ' (not found)', c.value, ro ? [] : [
				el('button', { className: 'ico-btn', textContent: '↑', title: 'Earlier', disabled: i === 0, onclick: () => move(-1) }),
				el('button', { className: 'ico-btn', textContent: '↓', title: 'Later', disabled: i === cargo.length - 1, onclick: () => move(1) }),
				el('button', { className: 'ico-btn', textContent: '×', title: 'Take it out', onclick: () => change('Take ' + (p ? p.name : 'cargo') + ' out of ' + l.name, () => c.remove()) }),
			]);
		});
		const add = ro ? null : el('button', { className: 'ico-btn ed-addrow', textContent: '+ Add a unit or item…', onclick: () => pick('Add to ' + l.name, new Set([...UNITS, ...ITEMS]), (p) => change('Add ' + p.name + ' to ' + l.name, () => node.add('AddCargoItem', p.cls).add('PresetName', reference(p, module)))) });
		return el('section', { className: 'ed-card' },
			el('div', { className: 'ed-card-head' }, name, el('span', { className: 'spacer' }), el('button', { className: 'ico-btn', textContent: 'Its lines', title: l.doc.path + ':' + node.line, onclick: () => MS.ide.openFile(l.doc.path, node.line) }),
				ro ? null : el('button', { className: 'ico-btn', textContent: 'Delete', onclick: () => change('Delete loadout ' + l.name, () => node.remove()) })),
			el('div', { className: 'ed-label', textContent: 'Comes in' }), craftSlot,
			el('div', { className: 'ed-label', textContent: 'Brings (' + cargo.length + ')' }), ...cargoRows, add);
	}

	async function newLoadout(module) {
		const a = app();
		const doc = await docFor(module, 'Loadout', 'Loadouts.ini');
		const taken = new Set(presetsOf(module, (p) => p.cls === 'Loadout').map((p) => p.name));
		let name = taken.has('Default') ? 'New loadout' : 'Default';
		for (let n = 2; taken.has(name); n++) {
			name = 'New loadout ' + n;
		}
		change('New loadout ' + name, () => doc.addTopLevel('AddLoadout', 'Loadout').add('PresetName', name));
		a.status('Made the loadout ' + name + ' in ' + doc.path + '. Pick its craft and what it brings, then Save.');
	}

	function copyLoadouts(module, loadouts) {
		const { field, modPicker, para } = MS.dialogs.parts;
		const mod = modPicker();
		MS.dialogs.open('Copy ' + module + '\'s loadouts', el('div', {}, para('Studio copies all ' + loadouts.length + ' loadouts into <b>Loadouts.ini</b> in your mod, to change as you like.'), field('Your mod', mod.node)), [
			{ text: 'Cancel' },
			{
				text: 'Copy them',
				primary: true,
				action: async () => {
					const a = app();
					const target = mod.value();
					if (!target) {
						return false;
					}
					const doc = await docFor(target, 'Loadout', 'Loadouts.ini');
					change('Copy ' + module + '\'s loadouts into ' + target, () => {
						for (const l of loadouts) {
							const copy = l.node.clone();
							// Names that meant something in the original module still point at the same presets from the new one.
							for (const c of copy.children.filter((n) => n.key === 'DeliveryCraft' || n.key === 'AddCargoItem')) {
								const ref = c.child('PresetName');
								const p = ref ? a.lib.find(ref.value, c.value, module) : null;
								if (p) {
									ref.setValue(reference(p, target));
								}
							}
							copy.lead = doc.root.children.length ? ['', ''] : [];
							doc.root.add(copy);
						}
					});
					MS.ide.openForm('loadouts:' + target, 'Loadouts · ' + target, '▦', renderLoadouts(target));
				},
			},
		]);
	}

	// ---- sounds ----

	const SOUND_EXT = /\.(wav|ogg|flac|mp3)$/i;
	const SOUND_SETTINGS = [
		{ key: 'Volume', label: 'Volume', help: '1 is as recorded', def: '1' },
		{ key: 'Pitch', label: 'Pitch', help: '1 is as recorded, 2 an octave up', def: '1' },
		{ key: 'PitchVariation', label: 'Pitch variation', help: 'Random change each time it plays', def: '0' },
		{ key: 'AttenuationStartDistance', label: 'Fades from (px)', help: 'How far away it starts getting quieter', def: '' },
		{ key: 'Priority', label: 'Priority', help: '0 is most important, 256 least', def: '128' },
		{ key: 'LoopSetting', label: 'Loops', help: '0 plays once, -1 loops for ever', def: '0' },
		{ key: 'SoundOverlapMode', label: 'When played again', choices: ['Overlap', 'Restart', 'Ignore Play'], def: 'Overlap' },
		{ key: 'BusRouting', label: 'Plays on', choices: ['SFX', 'UI', 'Music'], def: 'SFX' },
		{ key: 'Immobile', label: 'Heard the same everywhere (not placed in the world)', bool: true },
		{ key: 'AffectedByGlobalPitch', label: 'Slows down with the game', bool: true, defOn: true },
	];

	// A sound file node: "AddSound = path.flac", or "AddSound = ContentFile" with "FilePath = ...".
	const soundPath = (n) => (SOUND_EXT.test(n.value) ? n.value : n.childValue('FilePath', n.childValue('Path', ''))).trim().replace(/\\/g, '/');

	function soundFiles(entity) {
		const out = [];
		const walk = (n, own) => {
			if (n.key === 'AddSound') {
				const path = soundPath(n);
				if (path) {
					out.push({ node: n, path, own });
				}
			}
			for (const c of n.children) {
				walk(c, own);
			}
		};
		for (const e of entity.entries) {
			if (e.key === 'AddSound' || e.key === 'AddSoundSet') {
				walk(e.node, e.node.isWithin(entity.node));
			}
		}
		return out;
	}

	async function play(path, entity) {
		try {
			const audio = new Audio(URL.createObjectURL(await app().fs.readFile(path)));
			if (entity) {
				audio.volume = Math.max(0, Math.min(1, Number(entity.str('Volume', '1')) || 1));
				audio.preservesPitch = false;
				const pitch = Number(entity.str('Pitch', '1')) || 1;
				const vary = Number(entity.str('PitchVariation', '0')) || 0;
				audio.playbackRate = Math.max(0.25, Math.min(4, pitch * (1 + (Math.random() * 2 - 1) * vary)));
			}
			await audio.play();
		} catch (e) {
			app().toast('Couldn\'t play ' + path + ': ' + e.message, 'bad');
		}
	}

	function renderSounds(module) {
		return whenRead((host, tab) => {
			const a = app();
			const s = tab.state || (tab.state = { filter: '', selected: null });
			const all = presetsOf(module, (p) => p.cls === 'SoundContainer').sort((x, y) => x.name.localeCompare(y.name));
			const q = s.filter.trim().toLowerCase();
			const list = all.filter((p) => !q || p.name.toLowerCase().includes(q));
			const keyOf = (p) => p.doc.path + ':' + p.node.line + ':' + p.name;
			const selected = all.find((p) => keyOf(p) === s.selected) || list[0] || null;
			const filter = el('input', { type: 'search', placeholder: 'Filter ' + all.length + ' sounds…', value: s.filter, className: 'ed-input' });
			filter.dataset.fk = 'filter';
			filter.oninput = () => {
				s.filter = filter.value;
				clearTimeout(s.timer);
				s.timer = setTimeout(() => MS.ide.refreshForm(), 200);
			};
			const ro = locked(a.lib.docFor(module + '/Index.ini') || (all[0] && all[0].doc));
			const items = list.slice(0, 500).map((p) => el('button', { className: 'ed-item' + (p === selected ? ' sel' : ''), textContent: p.name, onclick: () => { s.selected = keyOf(p); MS.ide.refreshForm(); } }));
			const newBtn = el('button', { className: 'tb', textContent: '+ New sound', disabled: ro, onclick: () => newSound(module, tab) });
			host.replaceChildren(el('div', { className: 'ed ed-split' },
				el('div', { className: 'ed-head' }, el('h2', { textContent: 'Sounds · ' + module }), el('span', { className: 'spacer' }), newBtn),
				lockBanner(all[0] && all[0].doc, module),
				el('div', { className: 'ed-panes' },
					el('div', { className: 'ed-list' }, filter, ...(items.length ? items : [el('p', { className: 'ed-empty', textContent: 'No sounds' + (q ? ' match.' : ' yet.') })])),
					el('div', { className: 'ed-detail' }, selected ? soundDetail(selected, module) : null))));
		});
	}

	function soundDetail(p, module) {
		const a = app();
		const e = a.lib.resolve(p.node);
		const ro = locked(p.doc);
		const files = soundFiles(e);
		const fileRows = files.map((f) => el('div', { className: 'ed-file' + (f.own ? '' : ' inherited') },
			el('button', { className: 'ico-btn', textContent: '▶', title: 'Play it', onclick: () => play(f.path, e) }),
			el('span', { className: 'ed-filename', textContent: f.path.split('/').pop(), title: f.path + (f.own ? '' : ' (from what it copies)') }),
			el('small', { textContent: f.path.split('/').slice(0, -1).join('/') }),
			f.own && !ro ? el('button', { className: 'ico-btn', textContent: '×', title: 'Take it out', onclick: () => change('Take ' + f.path.split('/').pop() + ' out of ' + p.name, () => f.node.remove()) }) : null));
		const addInput = el('input', { type: 'text', placeholder: module + '/Sounds/MySound.flac', className: 'ed-input', disabled: ro });
		addInput.dataset.fk = 'addsound';
		const list = el('datalist', { id: 'edSoundFiles' });
		addInput.setAttribute('list', 'edSoundFiles');
		a.fs.allFiles().then((paths) => {
			for (const f of (paths || []).filter((x) => SOUND_EXT.test(x) && x.toLowerCase().startsWith(module.toLowerCase() + '/')).slice(0, 2000)) {
				list.append(el('option', { value: f }));
			}
		}).catch(() => {});
		const addBtn = el('button', { className: 'tb', textContent: 'Add file', disabled: ro, onclick: async () => {
			const path = addInput.value.trim().replace(/\\/g, '/').replace(/^(Data|Mods)\//i, '');
			if (!SOUND_EXT.test(path)) {
				a.toast('Pick a .wav, .ogg, .flac or .mp3 file.', 'bad');
				return;
			}
			if (!(await a.fs.exists(path))) {
				a.toast(path + ' isn\'t there. Put the file in the mod\'s folder first.', 'bad');
				return;
			}
			change('Add ' + path.split('/').pop() + ' to ' + p.name, () => p.node.add('AddSound', 'ContentFile').add('FilePath', path));
		} });
		const settings = SOUND_SETTINGS.map((st) => {
			const own = p.node.child(st.key);
			const now = e.str(st.key, '');
			let input;
			if (st.bool) {
				input = el('input', { type: 'checkbox', checked: e.bool(st.key, !!st.defOn), disabled: ro });
				input.onchange = () => change('Set ' + p.name + '\'s ' + st.label, () => p.node.set(st.key, input.checked ? '1' : '0'));
				return el('label', { className: 'ed-check wide' }, input, st.label);
			}
			if (st.choices) {
				input = el('select', { className: 'ed-input', disabled: ro }, ...st.choices.map((c) => el('option', { value: c, textContent: c, selected: (now || st.def).toLowerCase() === c.toLowerCase() })));
				input.onchange = () => change('Set ' + p.name + '\'s ' + st.label, () => p.node.set(st.key, input.value));
			} else {
				input = el('input', { type: 'number', step: 'any', className: 'ed-input num', disabled: ro, value: own ? own.value : '', placeholder: now || st.def || '' });
				input.onchange = () => change('Set ' + p.name + '\'s ' + st.label, () => {
					const v = input.value.trim();
					if (v) {
						p.node.set(st.key, v);
					} else if (p.node.child(st.key)) {
						p.node.child(st.key).remove(); // back to what it copies, or the game's default
					}
				});
			}
			input.dataset.fk = 'set:' + st.key;
			return el('label', { className: 'ed-setting', title: st.help }, el('span', { textContent: st.label }), input, el('small', { textContent: st.help }));
		});
		return el('div', {},
			el('div', { className: 'ed-card-head' }, el('h3', { textContent: p.name }), el('span', { className: 'spacer' }),
				el('button', { className: 'tb', textContent: '▶ Play', title: 'Play one of its files, as the game would', disabled: !files.length, onclick: () => files.length && play(files[Math.floor(Math.random() * files.length)].path, e) }),
				el('button', { className: 'ico-btn', textContent: 'Its lines', onclick: () => MS.ide.openFile(p.doc.path, p.node.line) })),
			el('div', { className: 'ed-label', textContent: 'Files (' + files.length + '): the game picks one each time' }), ...fileRows,
			ro ? null : el('div', { className: 'ed-row' }, addInput, list, addBtn),
			el('div', { className: 'ed-label', textContent: 'How it plays' }), el('div', { className: 'ed-settings' }, ...settings));
	}

	function newSound(module, tab) {
		const { field } = MS.dialogs.parts;
		const name = el('input', { type: 'text', placeholder: 'My Sound' });
		MS.dialogs.open('New sound', field('Name', name, 'Units, guns and scripts use it by this name.'), [
			{ text: 'Cancel' },
			{
				text: 'Make it',
				primary: true,
				action: async () => {
					const n = name.value.trim();
					if (!n) {
						return false;
					}
					const doc = await docFor(module, 'SoundContainer', 'Sounds.ini');
					change('New sound ' + n, () => doc.addTopLevel('AddSoundContainer', 'SoundContainer').add('PresetName', n));
					const made = app().lib.presets.find((p) => p.doc === doc && p.name === n);
					if (made) {
						tab.state = Object.assign(tab.state || {}, { selected: made.doc.path + ':' + made.node.line + ':' + made.name, filter: '' });
					}
					MS.ide.refreshForm();
				},
			},
		]);
	}

	// ---- the Data view in the activity bar ----

	const EDITORS = [
		['buy', 'Buy menu', 'Cost, buy menu and groups of every unit, craft and item', '$', renderBuyMenu],
		['loadouts', 'Loadouts', 'What a team orders: the craft and what it brings', '⇣', renderLoadouts],
		['sounds', 'Sounds', 'Sound presets: their files and how they play', '♪', renderSounds],
	];

	function renderSide() {
		const a = app();
		if (!a.lib) {
			$('dataEditors').replaceChildren(el('p', { className: 'side-note', textContent: 'Studio is still reading the game…' }));
			return;
		}
		const select = $('dataModule');
		const mods = a.lib.modules.filter((m) => !m.official);
		const game = a.lib.modules.filter((m) => m.official);
		const current = select.value || a.readLocal('ms.dataModule') || (mods[0] || game[0] || {}).name;
		select.replaceChildren(
			el('optgroup', { label: 'Your mods' }, ...mods.map((m) => el('option', { value: m.name, textContent: m.name }))),
			el('optgroup', { label: 'The game' }, ...game.map((m) => el('option', { value: m.name, textContent: m.name }))));
		select.value = current;
		select.onchange = () => {
			a.writeLocal('ms.dataModule', select.value);
			renderSide();
		};
		const module = select.value;
		$('dataEditors').replaceChildren(...EDITORS.map(([id, title, sub, icon, render]) => {
			const count = id === 'loadouts' ? presetsOf(module, (p) => p.cls === 'Loadout').length : id === 'sounds' ? presetsOf(module, (p) => p.cls === 'SoundContainer').length : presetsOf(module, (p) => UNITS.has(p.cls) || CRAFT.has(p.cls) || ITEMS.has(p.cls)).length;
			return el('button', { className: 'data-editor', onclick: () => MS.ide.openForm(id + ':' + module, title + ' · ' + module.replace(/\.rte$/i, ''), icon, render(module)) },
				el('b', {}, icon + '  ' + title, el('span', { className: 'badge-n', textContent: count })), el('span', { textContent: sub }));
		}));
	}

	MS.editors = { renderSide, renderBuyMenu, renderLoadouts, renderSounds };
})();
