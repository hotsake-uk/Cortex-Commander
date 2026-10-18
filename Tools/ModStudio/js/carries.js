// Cortex Mod Studio: what a unit carries and the loadouts it is in, as sections of the designer's properties.
//   Carries      a unit's or craft's inventory (AddInventory): what it starts with (a unit holds the first gun it carries), or what a
//                craft brings. Add from thumbnails, take out, move up and down, and show an item in the unit's hand in the view. Items it
//                inherits are listed too; the game can't take an inherited item out, so only the unit's own are changed.
//   In loadouts  the loadouts that bring this unit, craft or item (the AI and the buy menu order loadouts), each opening the Loadouts
//                editor, and Add to a loadout… to put it in one of your mod's loadouts, or a new one.
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
	const UNITS = new Set(['AHuman', 'ACrab']);
	const CRAFT = new Set(['ACDropShip', 'ACRocket']);
	const ITEMS = new Set(['HDFirearm', 'HeldDevice', 'TDExplosive', 'ThrownDevice']);
	const drawThumb = (canvas, p) => (UNITS.has(p.cls) || CRAFT.has(p.cls) ? MS.panels.drawRigThumb(canvas, p) : MS.panels.drawSpriteThumb(canvas, p));
	const thumb = (p) => {
		const c = el('canvas', { width: 28, height: 28, className: 'carry-thumb' });
		if (p) {
			MS.panels.queueThumb(c, () => drawThumb(c, p));
		}
		return c;
	};
	const nameOf = (value) => value.split('/').pop();

	// ---- carries ----

	function carriesSection(insp, part, ent, locked, section) {
		if (part.role !== 'body' || !(UNITS.has(ent.cls) || CRAFT.has(ent.cls))) {
			return;
		}
		const lib = app().lib;
		const craft = CRAFT.has(ent.cls);
		const module = part.anchor.doc ? part.anchor.doc.module : '';
		const entries = ent.entries.filter((e) => e.key === 'AddInventory' || e.key === 'AddInventoryDevice');
		const own = entries.filter((e) => e.node.parent === ent.node && ent.node.isWithin(part.anchor));
		const add = el('button', { className: 'small-btn', textContent: '+ Add…', title: craft ? 'Something for it to bring' : 'Something for it to carry', disabled: !!locked, onclick: () => pickToCarry(part, craft, module) });
		const rows = section(insp, 'Carries (' + entries.length + ')', add);
		if (!entries.length) {
			rows.append(el('div', { className: 'src', style: 'grid-column:auto', textContent: craft ? 'Nothing: it arrives empty. Add a unit or an item for it to bring.' : 'Nothing. Add a gun, a tool or a grenade; a unit holds the first gun it carries.' }));
		}
		entries.forEach((e) => {
			const ref = e.node.childValue('CopyOf', e.node.childValue('PresetName', ''));
			const p = ref ? lib.find(ref, e.node.value, e.node.doc ? e.node.doc.module : module) : null;
			const mine = own.indexOf(e);
			const from = mine >= 0 ? 'its own' : 'from ' + (e.node.topLevel.child('PresetName') ? e.node.topLevel.child('PresetName').value : e.node.doc.path);
			const buttons = [];
			if (!craft && p && ITEMS.has(p.cls) && ent.cls === 'AHuman') {
				const shown = app().device && app().device.path === p.doc.path && app().device.name === p.name;
				buttons.push(el('button', { className: 'ico-btn' + (shown ? ' on' : ''), textContent: '✋', title: shown ? 'In its hand in the view' : 'Show it in the unit\'s hand in the view', onclick: () => app().setDevice(shown ? null : p) }));
			}
			if (mine >= 0 && !locked) {
				// Swaps it with the unit's own item before or after it.
				const move = (dir) => app().edit('Move ' + (p ? p.name : 'item') + (dir < 0 ? ' up' : ' down'), () => {
					const other = own[mine + dir].node;
					const parent = e.node.parent;
					const at = parent.children.indexOf(other);
					e.node.remove();
					parent.add(e.node, undefined, at);
				});
				buttons.push(
					el('button', { className: 'ico-btn', textContent: '↑', title: 'Earlier', disabled: mine === 0, onclick: () => move(-1) }),
					el('button', { className: 'ico-btn', textContent: '↓', title: 'Later', disabled: mine === own.length - 1, onclick: () => move(1) }),
					el('button', { className: 'ico-btn', textContent: '×', title: 'Take it out', onclick: () => app().edit('Take ' + (p ? p.name : 'item') + ' out of ' + part.name + '\'s inventory', () => e.node.remove()) }));
			}
			const name = el('button', { className: 'carry-name', title: (p ? p.cls + ' · ' + p.module : ref + ' (not found)') + '. Click to open where it is written.', onclick: () => MS.ide.openFile(e.node.doc.path, e.node.line) },
				el('span', { textContent: p ? p.name : nameOf(ref || '?') + ' (not found)' }), el('small', { className: mine >= 0 ? 'own' : '', textContent: from }));
			rows.append(el('div', { className: 'carry-row' + (mine >= 0 ? '' : ' inherited'), title: mine >= 0 ? '' : 'It comes from what this copies: the game can\'t take an inherited item out' }, thumb(p), name, ...buttons));
		});
	}

	function pickToCarry(part, craft, module) {
		const lib = app().lib;
		const classes = craft ? new Set([...UNITS, ...ITEMS]) : ITEMS;
		const presets = lib.presets.filter((p) => p.topLevel && classes.has(p.cls)).sort((a, b) => a.name.localeCompare(b.name));
		const filters = [{ label: 'Mod', options: [['', 'Every mod'], ...[...new Set(presets.map((p) => p.module))].sort().map((m) => [m, m])], test: (p, v) => p.module === v }];
		MS.dialogs.open((craft ? 'Something for ' : 'Something to carry for ') + part.name, MS.dialogs.parts.pickerGrid(presets, (p) => app().edit('Give ' + part.name + ' ' + p.name, () => {
			const block = app().ownBlock(part);
			if (block) {
				block.add('AddInventory', p.cls).add('CopyOf', lib.reference(p.name, p.module, module));
			}
		}), drawThumb, false, filters), [{ text: 'Cancel' }], true);
	}

	// ---- in loadouts ----

	// The loadouts that bring a preset: as cargo, or as the craft they come in.
	function loadoutsWith(preset) {
		const lib = app().lib;
		const out = [];
		for (const l of lib.presets) {
			if (!l.topLevel || l.cls !== 'Loadout') {
				continue;
			}
			const named = (n) => n && lib.find(n.childValue('PresetName', n.childValue('CopyOf', '')), n.value, l.module) === preset;
			const cargo = l.node.children.filter((c) => c.key === 'AddCargoItem' && named(c)).length;
			const craft = named(l.node.child('DeliveryCraft'));
			if (cargo || craft) {
				out.push({ loadout: l, cargo, craft });
			}
		}
		return out.sort((a, b) => a.loadout.module.localeCompare(b.loadout.module) || a.loadout.name.localeCompare(b.loadout.name));
	}

	function loadoutsSection(insp, part, ent, section) {
		if (part.role !== 'body' || !(UNITS.has(ent.cls) || CRAFT.has(ent.cls) || ITEMS.has(ent.cls))) {
			return;
		}
		const preset = MS.nav ? MS.nav.presetOf(ent.node) : null;
		if (!preset) {
			return;
		}
		const found = loadoutsWith(preset);
		const add = el('button', { className: 'small-btn', textContent: '+ Add to a loadout…', title: 'Put ' + preset.name + ' in one of your loadouts, or a new one', onclick: () => addToLoadout(preset) });
		const rows = section(insp, 'In loadouts (' + found.length + ')', add);
		if (!found.length) {
			rows.append(el('div', { className: 'src', style: 'grid-column:auto', textContent: 'No loadout brings it. The AI and the buy menu\'s Loadouts order loadouts, so a unit in none of them is only bought by hand.' }));
		}
		for (const { loadout, cargo, craft } of found) {
			const what = craft ? 'comes in it' : cargo > 1 ? cargo + ' of them' : '';
			rows.append(el('div', { className: 'carry-row' },
				el('button', { className: 'carry-name', title: 'Open ' + loadout.module + '\'s loadouts', onclick: () => openLoadouts(loadout) }, el('span', { textContent: loadout.name }), el('small', { textContent: loadout.module + (what ? ' · ' + what : '') }))));
		}
	}

	function openLoadouts(loadout) {
		MS.editors.open('loadouts', loadout.module);
	}

	function addToLoadout(preset) {
		const lib = app().lib;
		const own = lib.presets.filter((l) => l.topLevel && l.cls === 'Loadout' && !lib.isProtected(l.doc)).sort((a, b) => a.module.localeCompare(b.module) || a.name.localeCompare(b.name));
		const home = app().actor ? app().actor.path.split('/')[0] : preset.module;
		const craft = CRAFT.has(preset.cls);
		const put = (l) => {
			MS.dialogs.close();
			app().edit((craft ? 'Deliver ' : 'Add ') + l.name + (craft ? ' in ' : ' to ') + preset.name, () => {
				const ref = lib.reference(preset.name, preset.module, l.module);
				if (craft) {
					const c = l.node.child('DeliveryCraft') || l.node.add('DeliveryCraft', preset.cls);
					c.setValue(preset.cls);
					c.set('PresetName', ref);
				} else {
					l.node.add('AddCargoItem', preset.cls).add('PresetName', ref);
				}
			});
			app().status((craft ? l.name + ' now comes in ' + preset.name : 'Added ' + preset.name + ' to ' + l.name) + '. Save to keep it.');
		};
		const fresh = async () => {
			MS.dialogs.close();
			const index = lib.docFor(home + '/Index.ini');
			if (!index || lib.isProtected(index)) {
				app().toast(home + ' is read only: make your own copy first, then add it to a loadout.', 'bad');
				return;
			}
			const doc = await MS.editors.docFor(home, 'Loadout', 'Loadouts.ini');
			const taken = new Set(lib.presets.filter((p) => p.cls === 'Loadout' && p.module === home).map((p) => p.name));
			let name = preset.name + (craft ? ' delivery' : ' squad');
			for (let n = 2; taken.has(name); n++) {
				name = preset.name + (craft ? ' delivery ' : ' squad ') + n;
			}
			app().edit('New loadout ' + name, () => {
				const l = doc.addTopLevel('AddLoadout', 'Loadout');
				l.add('PresetName', name);
				const ref = lib.reference(preset.name, preset.module, home);
				if (craft) {
					l.add('DeliveryCraft', preset.cls).add('PresetName', ref);
				} else {
					l.add('AddCargoItem', preset.cls).add('PresetName', ref);
				}
			});
			app().status('Made the loadout ' + name + ' in ' + doc.path + '. Open the Loadouts editor to pick its craft and the rest, then Save.');
		};
		const list = el('div', { className: 'snd-pick' }, ...own.map((l) => el('div', { className: 'snd-pick-row' },
			el('button', { className: 'snd-pick-name', onclick: () => put(l) }, el('span', { textContent: l.name }), el('small', { textContent: l.module + ' · ' + l.node.children.filter((c) => c.key === 'AddCargoItem').length + ' in it' })))));
		MS.dialogs.open('Add ' + preset.name + ' to a loadout', el('div', {},
			el('p', { className: 'note', textContent: own.length ? (craft ? 'The loadout will come in ' + preset.name + '.' : 'Pick one of your loadouts, or start a new one in ' + home + '.') : 'None of your mods has a loadout yet.' }),
			own.length ? list : null), [{ text: 'Cancel' }, { text: 'New loadout in ' + home, primary: !own.length, action: () => { fresh(); return false; } }], true);
	}

	MS.carries = { carriesSection, loadoutsSection, loadoutsWith };
})();
