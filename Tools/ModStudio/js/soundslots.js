// Cortex Mod Studio: the Sounds section of the designer's properties. Every sound a part can make is a slot (a gun's Fire, Empty, Reload
// start..., a unit's Pain and Death, a craft's hatch, anything's Gib), with what fills it now and where that comes from:
//   ▶            plays one of its files, with its volume and pitch
//   Pick…        fills the slot with a sound preset from any mod (FireSound = SoundContainer / CopyOf = ...)
//   the row      opens the slot: its files (add, take out), how loud, the pitch and how far it carries, silence it, or go back to what
//                the part inherits
// Which slots a class has comes from the engine's own list of what each class reads (data/ini-surface.json), so a class shows the sounds
// the game will actually play. A change is the part's own: an inherited sound is copied into the part first (as CopyOf the preset it
// used, or its lines), so other units and guns that share it are left as they are. The sound presets themselves are changed in the
// Sounds editor (Open in the Sounds editor).
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

	// The slots, in the order they're shown: [key, label, what makes it play].
	const SLOTS = [
		['FireSound', 'Fire', 'Each shot'],
		['FireEchoSound', 'Fire echo', 'The tail of a shot, heard from far away'],
		['PreFireSound', 'Pre-fire', 'Before the first shot, as the trigger is pulled (a spin-up)'],
		['ActiveSound', 'Firing loop', 'Plays for as long as it fires'],
		['DeactivationSound', 'Stop firing', 'When the trigger is let go'],
		['EmptySound', 'Empty', 'The trigger pulled with no ammo'],
		['ReloadStartSound', 'Reload start', 'As the magazine comes out'],
		['ReloadEndSound', 'Reload end', 'As the new magazine goes in'],
		['BladeSwingSound', 'Swing', 'A melee swing'],
		['BladeHitSound', 'Hit', 'A swing that hits'],
		['BladeClashSound', 'Clash', 'Blade against blade'],
		['BladeHumSound', 'Hum', 'Plays while the blade is out'],
		['BladeIgniteSound', 'Ignite', 'As the blade comes out'],
		['StickSound', 'Stick', 'When it sticks into something'],
		['ActivationSound', 'Activate', 'When it is set off (a grenade\'s pin)'],
		['DetonationSound', 'Detonate', 'When it goes off'],
		['PainSound', 'Pain', 'When it is hurt'],
		['DeathSound', 'Death', 'When it dies'],
		['BodyHitSound', 'Body hit', 'When it hits the ground or a wall hard'],
		['AlarmSound', 'Alarm', 'When it spots an enemy'],
		['DeviceSwitchSound', 'Switch item', 'When it changes what it holds'],
		['StrideSound', 'Footstep', 'Each step'],
		['HatchOpenSound', 'Hatch open', 'As the doors open'],
		['HatchCloseSound', 'Hatch close', 'As the doors close'],
		['CrashSound', 'Crash', 'When it hits the ground too hard'],
		['EngineSound', 'Engine', 'Plays while the engine runs'],
		['EmissionSound', 'Emitting', 'Plays while it emits'],
		['BurstSound', 'Burst', 'At the start of a burst'],
		['EndSound', 'End', 'When it stops emitting'],
		['DoorMoveStartSound', 'Door starts', 'As the door starts to move'],
		['DoorMoveSound', 'Door moving', 'Plays while the door moves'],
		['DoorDirectionChangeSound', 'Door turns back', 'When the door changes direction'],
		['DoorMoveEndSound', 'Door stops', 'When the door stops'],
		['MoveStartSound', 'Move start', 'As it starts to move'],
		['MoveSound', 'Moving', 'Plays while it moves'],
		['MoveEndSound', 'Move end', 'When it stops'],
		['GibSound', 'Gib', 'When it breaks apart'],
	];
	const SLOT_KEYS = new Set(SLOTS.map((s) => s[0]));
	// The settings shown on an open slot (the rest are in the Sounds editor).
	const QUICK_SETTINGS = [
		['Volume', 'Volume', '1 is as recorded', '1'],
		['Pitch', 'Pitch', '1 is as recorded, 2 an octave up', '1'],
		['PitchVariation', 'Pitch varies', 'A random change each time it plays', '0'],
		['AttenuationStartDistance', 'Fades from (px)', 'How far away it starts getting quieter', ''],
	];
	// Settings a flattened sound keeps (see flatten): every one the Sounds editor shows.
	const KEPT_SETTINGS = ['Volume', 'Pitch', 'PitchVariation', 'AttenuationStartDistance', 'Priority', 'LoopSetting', 'SoundOverlapMode', 'BusRouting', 'Immobile', 'AffectedByGlobalPitch', 'CustomPanValue', 'PanningStrengthMultiplier'];
	const SOUND_EXT = /\.(wav|ogg|flac|mp3)$/i;

	// ---- which slots a class has ----

	let surface = null;
	fetch('data/ini-surface.json', { cache: 'no-cache' })
		.then((r) => (r.ok ? r.json() : null))
		.then((json) => {
			surface = json && json.classes;
			if (surface && app() && app().rig) {
				MS.panels.refresh();
			}
		})
		.catch(() => {});

	const slotsCache = new Map();
	function slotKeysOf(cls) {
		if (!surface) {
			return null;
		}
		if (!slotsCache.has(cls)) {
			const keys = new Set();
			const seen = new Set();
			for (let c = cls; c && surface[c] && !seen.has(c); c = surface[c].parent) {
				seen.add(c);
				for (const p of surface[c].properties) {
					if (SLOT_KEYS.has(p)) {
						keys.add(p);
					}
				}
			}
			slotsCache.set(cls, keys);
		}
		return slotsCache.get(cls);
	}

	// The slots to show for a part: the ones its class reads (not the ones only Commander reads, when the mod is for Community Edition),
	// and any the part sets anyway.
	function slotsFor(ent) {
		const known = slotKeysOf(ent.cls);
		return SLOTS.filter(([key]) => (ent.has(key) || (known ? known.has(key) : false)) && !(MS.target && MS.target.commanderOnly(ent.cls, key)));
	}

	// ---- what fills a slot ----

	// The slot as it stands: the sound entity (or null), its files, the preset it is or copies, and whether the part sets it itself.
	function slotState(part, key) {
		const lib = app().lib;
		const entry = part.ent.last(key);
		if (!entry) {
			return { key, entry: null, sound: null, files: [], preset: null, own: false };
		}
		const sound = lib.resolve(entry.node);
		const copy = entry.node.child('CopyOf');
		const preset = copy ? lib.find(copy.value, 'SoundContainer', entry.node.doc ? entry.node.doc.module : null) : null;
		// set in the part's own lines (as opposed to what it copies), and whether that is over a sound it inherits
		const own = entry.node.parent === part.ent.node && part.ent.node.isWithin(part.anchor);
		return { key, entry, sound, files: MS.editors.soundFiles(sound), preset, own, overInherited: own && part.ent.all(key).length > 1 };
	}

	// The slot's own node in the part's block, made if asked: what the part inherited (CopyOf the preset it used, or a copy of its lines),
	// or an empty sound. Call inside app.edit.
	function ownSlot(part, key, make) {
		const block = app().ownBlock(part);
		if (!block) {
			return null;
		}
		const mine = block.children.filter((c) => c.key === key).pop();
		if (mine || !make) {
			return mine || null;
		}
		const lib = app().lib;
		const entry = lib.resolve(block).last(key);
		const node = entry ? lib.inheritedCopy(entry.node, block.doc.module) : new MS.IniNode(key, 'SoundContainer');
		node.key = key;
		return block.add(node);
	}

	// Replaces the slot's own node (if any) with the given one. Call inside app.edit.
	function replaceSlot(part, key, node) {
		const block = app().ownBlock(part);
		if (!block) {
			return null;
		}
		const old = block.children.filter((c) => c.key === key);
		const at = old.length ? block.children.indexOf(old[old.length - 1]) : -1;
		for (const o of old) {
			o.remove();
		}
		return block.add(node, at >= 0 ? Math.min(at, block.children.length) : undefined);
	}

	// The slot written out in full in the part's own block: its settings and its files, with no CopyOf, so any one file can be taken out.
	function flatten(part, key, without) {
		const s = slotState(part, key);
		const node = new MS.IniNode(key, 'SoundContainer');
		if (s.sound) {
			for (const k of KEPT_SETTINGS) {
				if (s.sound.has(k)) {
					node.add(k, s.sound.str(k, ''));
				}
			}
			for (const f of s.files) {
				if (f.path !== without) {
					node.add('AddSound', 'ContentFile').add('FilePath', f.path);
				}
			}
		}
		return replaceSlot(part, key, node);
	}

	const label = (key) => (SLOTS.find((s) => s[0] === key) || [key, key])[1].toLowerCase();

	function pickSound(part, key) {
		const lib = app().lib;
		const module = part.anchor.doc ? part.anchor.doc.module : '';
		const word = label(key).split(' ')[0];
		const all = lib.presets.filter((p) => p.topLevel && p.cls === 'SoundContainer');
		// this mod's sounds first, then ones whose name fits the slot ("Fire" for the fire sound), then the rest by name
		const rank = (p) => (p.module === module ? 0 : 2) + (p.name.toLowerCase().includes(word) ? 0 : 1);
		all.sort((x, y) => rank(x) - rank(y) || x.name.localeCompare(y.name));
		const search = el('input', { type: 'search', placeholder: 'Search ' + all.length + ' sounds…', className: 'ed-input' });
		const list = el('div', { className: 'snd-pick' });
		let close = null;
		const render = () => {
			const q = search.value.trim().toLowerCase();
			const shown = all.filter((p) => !q || p.name.toLowerCase().includes(q) || p.module.toLowerCase().includes(q)).slice(0, 300);
			list.replaceChildren(...shown.map((p) => {
				const sound = lib.resolve(p.node);
				const files = MS.editors.soundFiles(sound);
				return el('div', { className: 'snd-pick-row' },
					el('button', { className: 'ico-btn', textContent: '▶', title: 'Play it', disabled: !files.length, onclick: () => playOne(files, sound) }),
					el('button', { className: 'snd-pick-name', title: 'Use ' + p.name, onclick: () => {
						app().edit('Use ' + p.name + ' as ' + part.name + '\'s ' + label(key) + ' sound', () => {
							const node = new MS.IniNode(key, 'SoundContainer');
							node.add('CopyOf', lib.reference(p.name, p.module, module));
							replaceSlot(part, key, node);
						});
						if (close) {
							close();
						}
					} }, el('span', { textContent: p.name }), el('small', { textContent: p.module + ' · ' + files.length + (files.length === 1 ? ' file' : ' files') })));
			}));
		};
		let t = null;
		search.oninput = () => {
			clearTimeout(t);
			t = setTimeout(render, 120);
		};
		render();
		MS.dialogs.open('The ' + label(key) + ' sound for ' + part.name, el('div', {}, search, list), [{ text: 'Cancel' }], true);
		close = () => MS.dialogs.close();
		setTimeout(() => search.focus(), 0);
	}

	function playOne(files, sound) {
		if (files.length) {
			MS.editors.play(files[Math.floor(Math.random() * files.length)].path, sound);
		}
	}

	function openInSoundsEditor(preset) {
		MS.editors.open('sounds', preset.module);
		const t = MS.ide.tabs.find((x) => x.kind === 'form' && x.editor && x.editor.id === 'sounds' && x.editor.module === preset.module);
		if (t) {
			t.state = Object.assign(t.state || {}, { selected: preset.doc.path + ':' + preset.name, filter: '' });
			MS.ide.refreshForm();
		}
	}

	// ---- the section ----

	const openSlots = new Set(); // the slots shown open, by key, while Studio runs

	function render(insp, part, ent, locked, section) {
		const slots = slotsFor(ent);
		if (!slots.length) {
			return;
		}
		const filled = slots.filter(([key]) => ent.has(key)).length;
		const rows = section(insp, 'Sounds');
		rows.classList.add('snd-rows');
		const head = rows.previousElementSibling;
		if (head) {
			head.querySelector('span').textContent = 'Sounds (' + filled + ' of ' + slots.length + ')';
		}
		for (const [key, text, help] of slots) {
			rows.append(slotRow(part, key, text, help, locked));
		}
	}

	function slotRow(part, key, text, help, locked) {
		const s = slotState(part, key);
		const open = openSlots.has(key);
		const what = !s.entry ? 'None' : s.preset ? s.preset.name : s.files.length ? s.files.length + (s.files.length === 1 ? ' file' : ' files') : 'Silent';
		const where = !s.entry ? '' : s.own ? (s.preset && s.entry.node.child('CopyOf') && s.entry.node.children.length > 1 ? 'set here, changed' : 'set here') : 'from ' + (s.entry.node.topLevel.child('PresetName') ? s.entry.node.topLevel.child('PresetName').value : s.entry.node.doc.path);
		const row = el('div', { className: 'snd-slot' + (open ? ' open' : '') + (s.entry ? '' : ' empty') });
		const name = el('button', { className: 'snd-name', title: help + '. Click to ' + (open ? 'close it' : 'see its files and how it plays') + ' (' + key + ')', onclick: () => {
			open ? openSlots.delete(key) : openSlots.add(key);
			MS.panels.refresh();
		} }, el('span', { className: 'snd-caret', textContent: open ? '▾' : '▸' }), el('span', { textContent: text }));
		const play = el('button', { className: 'ico-btn', textContent: '▶', title: s.files.length ? 'Play it' : 'Nothing to play', disabled: !s.files.length, onclick: () => playOne(s.files, s.sound) });
		const value = el('span', { className: 'snd-what' + (s.own ? ' own' : ''), title: s.entry ? (s.entry.node.doc ? s.entry.node.doc.path + ':' + s.entry.node.line : '') : 'The game plays nothing here' }, what, el('small', { textContent: where }));
		const pick = el('button', { className: 'small-btn', textContent: s.entry ? 'Change…' : 'Pick…', title: 'Pick a sound for this from any mod', disabled: !!locked, onclick: () => pickSound(part, key) });
		row.append(el('div', { className: 'snd-line' }, name, play, value, pick));
		if (open) {
			row.append(slotDetail(part, s, locked));
		}
		return row;
	}

	function slotDetail(part, s, locked) {
		const key = s.key;
		const box = el('div', { className: 'snd-detail' });
		const edit = (what, fn) => app().edit(what + ' (' + part.name + '\'s ' + label(key) + ' sound)', fn);
		// its files
		for (const f of s.files) {
			const ownFile = s.own && f.own; // a file the part's own lines add (not one from the sound it copies)
			box.append(el('div', { className: 'snd-file' },
				el('button', { className: 'ico-btn', textContent: '▶', title: 'Play this one', onclick: () => MS.editors.play(f.path, s.sound) }),
				el('span', { className: 'snd-file-name', textContent: f.path.split('/').pop(), title: f.path }),
				locked ? null : el('button', { className: 'ico-btn', textContent: '×', title: 'Take it out (just for ' + part.name + ')', onclick: () => edit('Take ' + f.path.split('/').pop() + ' out', () => {
					if (ownFile) {
						f.node.remove();
					} else {
						flatten(part, key, f.path);
					}
				}) })));
		}
		if (!locked) {
			const module = part.anchor.doc ? part.anchor.doc.module : '';
			const input = el('input', { type: 'text', className: 'text', placeholder: 'or a file in the mod', title: 'A sound file already in a mod, e.g. ' + module + '/Sounds/' + label(key).replace(/ /g, '') + '.flac' });
			const listId = 'sndFiles' + key;
			const list = el('datalist', { id: listId });
			input.setAttribute('list', listId);
			input.onfocus = () => {
				if (list.childElementCount) {
					return;
				}
				app().fs.allFiles().then((paths) => {
					const mine = (paths || []).filter((x) => SOUND_EXT.test(x));
					mine.sort((x, y) => (x.toLowerCase().startsWith(module.toLowerCase() + '/') ? 0 : 1) - (y.toLowerCase().startsWith(module.toLowerCase() + '/') ? 0 : 1));
					list.append(...mine.slice(0, 3000).map((x) => el('option', { value: x })));
				}).catch(() => {});
			};
			const add = async () => {
				const path = input.value.trim().replace(/\\/g, '/').replace(/^(Data|Mods)\//i, '');
				if (!SOUND_EXT.test(path)) {
					app().toast('Pick a .wav, .ogg, .flac or .mp3 file.', 'bad');
					return;
				}
				if (!(await app().fs.exists(path))) {
					app().toast(path + ' isn\'t there. Put the file in the mod\'s folder first.', 'bad');
					return;
				}
				edit('Add ' + path.split('/').pop(), () => {
					const node = ownSlot(part, key, true);
					if (node) {
						node.add('AddSound', 'ContentFile').add('FilePath', path);
					}
				});
			};
			input.onkeydown = (e) => {
				if (e.key === 'Enter') {
					add();
				}
			};
			const addFiles = async (picked) => {
				const folder = (app().actor ? app().actor.name : part.name) + ' ' + label(key);
				const paths = await MS.editors.importSounds(module, folder, picked);
				if (paths.length) {
					edit('Add ' + (paths.length === 1 ? paths[0].split('/').pop() : paths.length + ' sound files'), () => {
						const node = ownSlot(part, key, true);
						if (node) {
							for (const path of paths) {
								node.add('AddSound', 'ContentFile').add('FilePath', path);
							}
						}
					});
				}
			};
			box.append(el('div', { className: 'snd-add' }, el('button', { className: 'small-btn add', textContent: '+ Sound files…', title: 'Pick .wav, .ogg, .flac or .mp3 files from your computer (or drop them here): they are copied into ' + module + '/Sounds', onclick: () => addFiles() }), input, list, el('button', { className: 'small-btn', textContent: 'Add', title: 'Add a file already in the mod', onclick: add })));
			MS.editors.soundDrop(box, (dropped) => addFiles(dropped));
		}
		if (s.entry) {
			// how it plays
			const settings = el('div', { className: 'snd-settings' });
			for (const [k, text, help, def] of QUICK_SETTINGS) {
				const ownNode = s.own ? s.entry.node.child(k) : null;
				const input = el('input', { type: 'number', step: 'any', className: 'text num', disabled: !!locked, value: ownNode ? ownNode.value : '', placeholder: s.sound.str(k, def) });
				input.onchange = () => edit('Set ' + text.toLowerCase(), () => {
					const v = input.value.trim();
					const node = ownSlot(part, key, !!v);
					if (!node) {
						return;
					}
					if (v) {
						node.set(k, v);
					} else if (node.child(k)) {
						node.child(k).remove();
					}
				});
				settings.append(el('label', { title: help }, el('span', { textContent: text }), input));
			}
			box.append(settings);
		}
		const buttons = el('div', { className: 'snd-buttons' });
		if (s.preset) {
			buttons.append(el('button', { className: 'small-btn', textContent: 'Open in the Sounds editor', title: s.preset.name + ' · ' + s.preset.module + ': its files and every setting (a change there changes everything that uses it)', onclick: () => openInSoundsEditor(s.preset) }));
		}
		if (s.entry) {
			buttons.append(el('button', { className: 'small-btn', textContent: 'Its lines', title: 'Open where it is written', onclick: () => MS.ide.openFile(s.entry.node.doc.path, s.entry.node.line) }));
		}
		if (!locked && s.entry && (s.files.length || s.entry.node.child('CopyOf'))) {
			buttons.append(el('button', { className: 'small-btn', textContent: 'Silence', title: 'Play nothing here', onclick: () => edit('Silence it', () => replaceSlot(part, key, new MS.IniNode(key, 'SoundContainer'))) }));
		}
		if (!locked && s.own && s.overInherited) {
			buttons.append(el('button', { className: 'small-btn', textContent: 'Use the inherited sound', title: 'Take this part\'s own sound away, back to what it copies', onclick: () => edit('Back to the inherited sound', () => {
				const node = ownSlot(part, key, false);
				if (node) {
					node.remove();
				}
			}) }));
		} else if (!locked && s.own) {
			buttons.append(el('button', { className: 'small-btn danger', textContent: 'Remove', title: 'No sound here', onclick: () => edit('Remove it', () => {
				const node = ownSlot(part, key, false);
				if (node) {
					node.remove();
				}
			}) }));
		}
		if (buttons.childElementCount) {
			box.append(buttons);
		}
		return box;
	}

	MS.soundSlots = { section: render, slotsFor };
})();
