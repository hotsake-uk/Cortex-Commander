// Cortex Mod Studio: New…, for making things without writing INI. Four steps in one dialog: what to make (a soldier, a crab, a craft,
// a gun, a grenade, or an empty mod), which existing one to start from, its name and mod, then a few settings in plain words. The new
// preset is a copy (CopyOf) of the one you started from, made by the same code as Make my own copy (dialogs.js), with only the settings
// you changed written into it; then it opens in the designer.
'use strict';
window.MS = window.MS || {};

(function () {
	const app = () => MS.app;
	const P = () => MS.dialogs.parts;

	const UNIT_STATS = [
		{ key: 'Health', label: 'Health', help: 'How much damage it takes before it dies. Most soldiers have 100.', min: 1, max: 10000 },
		{ key: 'Mass', label: 'Weight (kg)', help: 'Heavier units are slower to push around and jump less high.', min: 1, max: 100000 },
		{ key: 'GoldValue', label: 'Cost (gold)', help: 'What it costs in the buy menu.', min: 0, max: 100000 },
		{ key: 'Description', label: 'Description', help: 'Shown in the buy menu.', text: true },
	];
	const KINDS = [
		{ id: 'soldier', title: 'Soldier', sub: 'Arms and legs, carries guns', classes: ['AHuman'], rig: true, stats: UNIT_STATS },
		{ id: 'crab', title: 'Crab, drone or walker', sub: 'A unit on legs or tracks', classes: ['ACrab'], rig: true, stats: UNIT_STATS },
		{
			id: 'craft',
			title: 'Dropship or rocket',
			sub: 'Brings units and items to the battle',
			classes: ['ACDropShip', 'ACRocket'],
			rig: true,
			stats: [
				UNIT_STATS[0],
				UNIT_STATS[1],
				{ key: 'MaxPassengers', label: 'Passengers', help: 'How many units it can carry.', min: 0, max: 50 },
				UNIT_STATS[2],
				UNIT_STATS[3],
			],
		},
		{
			id: 'gun',
			title: 'Gun',
			sub: 'A weapon to hold and fire',
			classes: ['HDFirearm'],
			stats: [
				{ key: 'RateOfFire', label: 'Rounds a minute', help: 'How fast it fires.', min: 1, max: 6000 },
				{ key: 'ReloadTime', label: 'Reload time (ms)', help: '1000 is one second.', min: 0, max: 60000 },
				{ key: 'FullAuto', label: 'Fully automatic', help: 'Keeps firing while you hold the trigger.', bool: true },
				{ key: 'Mass', label: 'Weight (kg)', help: 'Heavy guns slow the unit down.', min: 0, max: 1000 },
				UNIT_STATS[2],
				UNIT_STATS[3],
			],
		},
		{
			id: 'thrown',
			title: 'Grenade or thrown item',
			sub: 'Something to throw',
			classes: ['TDExplosive', 'ThrownDevice'],
			stats: [{ key: 'Mass', label: 'Weight (kg)', help: 'Heavier things are thrown less far.', min: 0, max: 1000 }, UNIT_STATS[2], UNIT_STATS[3]],
		},
		{ id: 'mod', title: 'Empty mod', sub: 'A new mod folder, to fill later' },
	];

	// A preset's Description: one line, or the lines of a MultiLineText.
	function descriptionOf(entity) {
		const entry = entity.last('Description');
		if (!entry) {
			return '';
		}
		const n = entry.node;
		return n.children.length ? n.children.filter((c) => c.key === 'AddLine').map((c) => c.value).join(' ') : n.value;
	}

	function currentValue(entity, stat) {
		if (stat.text) {
			return descriptionOf(entity);
		}
		if (stat.bool) {
			return entity.bool(stat.key, false);
		}
		return entity.str(stat.key, '');
	}

	function open(kindId) {
		const { el } = P();
		const state = { kind: KINDS.find((k) => k.id === kindId) || null, base: null, values: {} };
		const body = el('div', { className: 'wiz' });
		MS.dialogs.open('New…', body, [{ text: 'Cancel' }], true);
		const steps = ['What to make', 'Start from', 'Name it', 'Settings'];

		const header = (n) => el('div', { className: 'wiz-steps' }, ...steps.map((t, i) => el('span', { className: i === n ? 'sel' : i < n ? 'done' : '', textContent: i + 1 + '. ' + t })));
		const nav = (back, next, nextText) =>
			el('div', { className: 'wiz-nav' },
				back ? el('button', { className: 'tb', textContent: '← Back', onclick: back }) : null,
				el('span', { className: 'spacer' }),
				next ? el('button', { className: 'tb primary', textContent: nextText || 'Next →', onclick: next }) : null);

		function stepKind() {
			const cards = KINDS.map((k) =>
				el('button', {
					className: 'wiz-kind',
					onclick: () => {
						state.kind = k;
						state.base = null;
						if (k.id === 'mod') {
							stepName();
						} else {
							stepBase();
						}
					},
				}, el('b', { textContent: k.title }), el('span', { textContent: k.sub })));
			body.replaceChildren(header(0), el('p', { className: 'wiz-lead', textContent: 'What do you want to make? It starts as a copy of something in the game, so it works straight away; then change what you like.' }), el('div', { className: 'wiz-kinds' }, ...cards));
		}

		function stepBase() {
			const { pickerGrid } = P();
			const a = app();
			const presets = a.lib.presets
				.filter((p) => p.topLevel && state.kind.classes.includes(p.cls))
				.sort((x, y) => a.fs.isOfficial(y.module) - a.fs.isOfficial(x.module) || x.name.localeCompare(y.name));
			const thumb = state.kind.rig ? (c, p) => MS.panels.drawRigThumb(c, p) : (c, p) => MS.panels.drawSpriteThumb(c, p);
			let picked = null;
			const chosen = el('div', { className: 'wiz-chosen', textContent: 'Pick one to start from.' });
			const grid = pickerGrid(presets, (p, tile) => {
				picked = p;
				for (const t of grid.querySelectorAll('.pick.sel')) {
					t.classList.remove('sel');
				}
				tile.classList.add('sel');
				chosen.textContent = 'Starting from ' + p.name + ' (' + p.module + ').';
			}, thumb, true);
			body.replaceChildren(header(1), el('p', { className: 'wiz-lead', textContent: 'Which ' + state.kind.title.toLowerCase() + ' should it start as? It gets everything that one has: its look, parts, AI and sounds.' }), grid, chosen,
				nav(stepKind, () => {
					if (!picked) {
						app().toast('Pick one to start from.', 'bad');
						return;
					}
					state.base = picked;
					stepName();
				}));
		}

		function stepName() {
			const { field, modPicker } = P();
			const mod = modPicker();
			const isMod = state.kind.id === 'mod';
			const name = el('input', { type: 'text', value: isMod ? '' : state.name || (state.base ? 'My ' + state.base.name : ''), placeholder: isMod ? 'MyMod' : 'Its name' });
			const fields = isMod
				? [field('Mod name', name, 'A folder called <name>.rte in the game\'s Mods folder.')]
				: [field('Name', name, 'Shown in the buy menu. It has to be different from every other one.'), field('Your mod', mod.node, 'Where it\'s saved. A new mod is made for you.')];
			body.replaceChildren(header(2), ...fields,
				nav(isMod ? stepKind : stepBase, async () => {
					const a = app();
					const n = name.value.trim();
					if (isMod) {
						await makeMod(n);
						return;
					}
					const modName = mod.value();
					if (!n || !modName) {
						a.toast('Give it a name, and the mod one too.', 'bad');
						return;
					}
					const taken = a.lib.find(n, state.base.cls, modName);
					if (taken && taken.name.toLowerCase() === n.toLowerCase()) {
						a.toast('There is already a ' + state.base.cls + ' called ' + n + '.', 'bad');
						return;
					}
					state.name = n;
					state.mod = modName;
					stepStats();
				}, isMod ? 'Make the mod' : 'Next →'));
			setTimeout(() => name.focus(), 0);
		}

		function stepStats() {
			const { field } = P();
			const entity = app().lib.resolve(state.base.node);
			const inputs = [];
			const rows = [];
			for (const stat of state.kind.stats) {
				const now = currentValue(entity, stat);
				let input;
				if (stat.bool) {
					input = el('input', { type: 'checkbox', checked: !!now });
				} else if (stat.text) {
					input = el('textarea', { rows: 3, value: now });
				} else {
					input = el('input', { type: 'number', value: now, min: stat.min, max: stat.max, step: 'any' });
				}
				inputs.push({ stat, input, was: now });
				const row = field(stat.label, input, stat.help + (stat.bool || stat.text ? '' : ' ' + state.base.name + ' has ' + (now === '' ? 'none set' : now) + '.'));
				if (stat.bool) {
					row.classList.add('field-check');
				}
				rows.push(row);
			}
			body.replaceChildren(header(3), el('p', { className: 'wiz-lead', textContent: 'Change what you like; leave the rest as ' + state.base.name + ' has it. Everything else can be changed later in the designer.' }), ...rows,
				nav(stepName, () => make(inputs), 'Make ' + state.name));
		}

		async function make(inputs) {
			const a = app();
			const changed = [];
			for (const { stat, input, was } of inputs) {
				const value = stat.bool ? input.checked : stat.text ? input.value.trim() : input.value.trim();
				if (stat.bool ? value !== !!was : String(value) !== String(was) && value !== '') {
					if (!stat.bool && !stat.text && (isNaN(Number(value)) || (stat.min !== undefined && Number(value) < stat.min) || (stat.max !== undefined && Number(value) > stat.max))) {
						a.toast(stat.label + ' should be a number from ' + stat.min + ' to ' + stat.max + '.', 'bad');
						return;
					}
					changed.push({ stat, value });
				}
			}
			const base = { name: state.base.name, cls: state.base.cls, path: state.base.doc.path };
			await P().makeCopy(state.mod, state.name, base, async (preset) => {
				if (!preset) {
					return;
				}
				if (changed.length) {
					a.edit('Set ' + state.name + '\'s settings', () => {
						for (const { stat, value } of changed) {
							const v = stat.bool ? (value ? '1' : '0') : String(value);
							preset.node.set(stat.key, v);
							if (stat.key === 'Health') {
								preset.node.set('MaxHealth', v); // Health only ever raises the maximum
							}
						}
					});
					await a.save(true, [preset.doc]);
				}
				a.undo = [];
				a.redo = [];
				MS.dialogs.close();
				a.openActor(a.lib.presets.find((p) => p.doc === preset.doc && p.name === state.name) || preset);
				MS.panels.showLibrary();
				a.updateFileStatus();
			});
		}

		async function makeMod(name) {
			const a = app();
			const n = P().safeName(name).replace(/\.?rte$/i, '');
			if (!name || !n) {
				a.toast('Give the mod a name.', 'bad');
				return;
			}
			const modName = n + '.rte';
			if (a.lib.modules.some((m) => m.name.toLowerCase() === modName.toLowerCase()) || (await a.fs.exists(modName + '/Index.ini'))) {
				a.toast('There is already a mod called ' + modName + '.', 'bad');
				return;
			}
			a.lib.newModule(modName);
			const index = a.lib.docFor(modName + '/Index.ini');
			a.hookDoc(index);
			await a.save(true, [index]);
			a.writeLocal('ms.lastMod', modName);
			a.lib.reindex();
			MS.dialogs.close();
			a.updateFileStatus();
			a.toast('Made ' + modName + '. New… can now put things in it.', 'good');
		}

		if (state.kind) {
			state.kind.id === 'mod' ? stepName() : stepBase();
		} else {
			stepKind();
		}
	}

	MS.wizard = { open, KINDS };
})();
