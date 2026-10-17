// Cortex Mod Studio: the Scenarios data editor (Data view). A scenario (an Activity in the game's files) is a way to play: which scene, who the teams
// are and what they start with, the rules a player can switch, the settings its script reads, the areas the scene must have, and the Lua script that
// runs it. This puts all of that in one form, and starts a new one as a copy of an existing scenario with its script copied along.
//   Story      description, the scene it plays on, where it sits in the campaign
//   Teams      team names and funds, who is on which team, whether the AI plays
//   Rules      the defaults a player starts with (fog of war, deploying units...) and which of them they may change
//   Settings   sliders the player gets on the scenario screen, read by the script (AddScenarioSetting)
//   Areas      areas the scene must have for it to work (AddRequiredArea)
//   Script     the Lua file and its callbacks, with the missing ones added in a click, and a button to start it in a running game
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
	const activitiesOf = (module) => app().lib.presets.filter((p) => p.topLevel && p.module === module && p.node.key === 'AddActivity').sort((x, y) => x.name.localeCompare(y.name));
	const TEAMS = [1, 2, 3, 4];

	// The scenario's script callbacks the game calls, with what each is for and a starting body.
	const CALLBACKS = [
		['StartActivity', 'isNewGame', 'Runs once when it begins (isNewGame is false when loading a save): set up teams, spawn units, show messages.'],
		['UpdateActivity', '', 'Runs every frame: check win and lose, run waves, update the screen.'],
		['EndActivity', '', 'Runs when it ends.'],
		['PauseActivity', 'pause', 'Runs when the game is paused or resumed.'],
		['OnSave', '', 'Runs before the game is saved: put anything to remember into the activity.'],
	];

	// ---- the scenario screen settings: "Key | Label | Unit | Min | Max | Step | Default" ----

	function parseSetting(text) {
		const f = text.split('|').map((x) => x.trim());
		return f.length >= 7 ? { key: f[0], label: f[1], unit: f[2], min: f[3], max: f[4], step: f[5], def: f[6] } : null;
	}

	const settingText = (s) => [s.key, s.label, s.unit, s.min, s.max, s.step, s.def].join(' | ');

	// ---- script text ----

	function scriptText(path) {
		if (!path) {
			return null;
		}
		const want = path.toLowerCase();
		for (const s of MS.ide.lua.scripts.values()) {
			if (s.path.toLowerCase() === want) {
				return s.text;
			}
		}
		return null;
	}

	// Writes a script: through its open tab if it has one, then to disk.
	async function writeScript(path, text) {
		const a = app();
		const tab = MS.ide.tabs.find((t) => t.path === path && t.model);
		if (tab) {
			tab.model.pushEditOperations([], [{ range: tab.model.getFullModelRange(), text }], () => null);
		}
		await a.fs.writeFile(path, text);
		MS.ide.lua.updateScript(path, text);
	}

	async function addCallback(path, className, [name, args]) {
		const current = scriptText(path) || (await app().fs.readText(path).catch(() => ''));
		const text = current.replace(/\s*$/, '') + '\n\nfunction ' + className + ':' + name + '(' + args + ')\n\t\nend\n';
		await writeScript(path, text);
		MS.ide.refreshForm();
	}

	// What the mission logic needs from this editor: the script's text and a way to change it, to jump to code or open it beside, and a unit picker.
	const missionHost = {
		scriptText,
		saveText: async (path, text) => {
			await writeScript(path, text);
			MS.ide.refreshForm();
		},
		openLine: (path, line) => MS.ide.openFile(path, line),
		openBeside: (path, line) => {
			MS.ide.openFile(path, line);
			const tab = MS.ide.tabs.find((t) => t.path === path);
			if (tab && MS.popout && MS.popout.pop) {
				MS.popout.pop(tab);
			}
		},
		pickItem: (onPick) => {
			const items = app().lib.presets.filter((x) => x.topLevel && ['HDFirearm', 'HeldDevice', 'TDExplosive', 'ThrownDevice'].includes(x.cls)).sort((x, y) => x.name.localeCompare(y.name));
			MS.dialogs.open('Pick an item', MS.dialogs.parts.pickerGrid(items, (chosen) => onPick(chosen), (c, x) => MS.panels.drawSpriteThumb(c, x), false, []), [{ text: 'Cancel' }], true);
		},
		pickUnit: (onPick) => {
			const units = app().lib.presets.filter((x) => x.topLevel && (x.cls === 'AHuman' || x.cls === 'ACrab')).sort((x, y) => x.name.localeCompare(y.name));
			MS.dialogs.open('Pick a unit', MS.dialogs.parts.pickerGrid(units, (chosen) => onPick(chosen), (c, x) => MS.panels.drawRigThumb(c, x), false, []), [{ text: 'Cancel' }], true);
		},
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
			const all = activitiesOf(module);
			const q = s.filter.trim().toLowerCase();
			const list = all.filter((p) => !q || p.name.toLowerCase().includes(q));
			const keyOf = (p) => p.doc.path + ':' + p.name;
			const selected = all.find((p) => keyOf(p) === s.selected) || list[0] || null;
			const filter = el('input', { type: 'search', placeholder: 'Filter ' + all.length + ' scenarios…', value: s.filter, className: 'ed-input' });
			filter.dataset.fk = 'filter';
			filter.oninput = () => {
				s.filter = filter.value;
				clearTimeout(s.timer);
				s.timer = setTimeout(() => MS.ide.refreshForm(), 200);
			};
			const items = list.map((p) => el('button', { className: 'ed-item' + (p === selected ? ' sel' : ''), textContent: p.name, onclick: () => { s.selected = keyOf(p); MS.ide.refreshForm(); } }));
			const readOnly = module && a.lib.modules.find((m) => m.name === module) && a.fs.isOfficial(module);
			host.replaceChildren(el('div', { className: 'ed ed-split' },
				el('div', { className: 'ed-head' }, el('h2', { textContent: 'Scenarios · ' + module }), el('span', { className: 'spacer' }),
					el('button', { className: 'tb primary', textContent: '+ New scenario…', disabled: !!readOnly, title: readOnly ? 'Pick one of your mods in the list above' : 'Start one from nothing, or as a copy of one that works', onclick: () => newScenario(module, s) })),
				all[0] && locked(all[0].doc) ? el('div', { className: 'ed-banner', textContent: module + ' is read only: its scenarios can be looked at. Make a copy in your mod with + New scenario (as a copy of one of these).' }) : null,
				el('div', { className: 'ed-panes' },
					el('div', { className: 'ed-list' }, filter, ...(items.length ? items : [el('p', { className: 'ed-empty', textContent: 'No scenarios' + (q ? ' match.' : ' in this mod yet.') })])),
					el('div', { className: 'ed-detail' }, selected ? detail(selected, module) : null))));
		};
	}

	function detail(p, module) {
		const a = app();
		const e = a.lib.resolve(p.node);
		const ro = locked(p.doc);
		const set = (label, key, value) => change('Change ' + label.toLowerCase() + ' in ' + p.name, () => {
			if (value === null || value === '') {
				const old = p.node.child(key);
				if (old) {
					old.remove();
				}
			} else {
				p.node.set(key, String(value));
			}
		});
		const text = (key, label, help, kind) => {
			const own = p.node.child(key);
			const input = el('input', { type: kind || 'text', step: 'any', className: 'ed-input' + (kind === 'number' ? ' num' : ''), value: own ? own.value : '', placeholder: e.str(key, ''), disabled: ro, title: help || '' });
			input.onchange = () => set(label, key, input.value.trim());
			input.dataset.fk = 'sc:' + key;
			return el('div', { className: 'ed-row', title: help || '' }, el('span', { className: 'ed-label', textContent: label }), input, help ? el('small', { textContent: help }) : null);
		};
		const flag = (key, label, help, fallback) => {
			const box = el('input', { type: 'checkbox', checked: e.bool(key, fallback), disabled: ro });
			box.onchange = () => set(label, key, box.checked ? '1' : '0');
			return el('label', { className: 'ed-check wide', title: help }, box, label);
		};

		// story
		const scenes = a.lib.presets.filter((x) => x.topLevel && x.cls === 'Scene').sort((x, y) => x.name.localeCompare(y.name));
		const sceneName = e.str('SceneName', '');
		const scene = el('select', { className: 'ed-input', disabled: ro }, el('option', { value: '', textContent: '(none)' }),
			...scenes.map((x) => el('option', { value: x.name, textContent: x.name + (x.module === 'Base.rte' ? '' : ' (' + x.module.replace(/[.]rte$/i, '') + ')'), selected: x.name === sceneName })));
		scene.onchange = () => set('the scene', 'SceneName', scene.value);
		const desc = el('textarea', { className: 'ed-input', rows: 3, value: e.str('Description', ''), disabled: ro });
		desc.onchange = () => set('the description', 'Description', desc.value.replace(/\r?\n/g, ' ').trim());

		// teams
		const teamRows = TEAMS.map((n) => el('div', { className: 'ed-row' }, el('span', { className: 'ed-label', textContent: 'Team ' + n }),
			(() => {
				const i = el('input', { type: 'text', className: 'ed-input', value: p.node.child('Team' + n + 'Name') ? p.node.child('Team' + n + 'Name').value : '', placeholder: e.str('Team' + n + 'Name', 'Team ' + n), disabled: ro, title: 'Its name' });
				i.onchange = () => set('team ' + n + '\'s name', 'Team' + n + 'Name', i.value.trim());
				return i;
			})(),
			(() => {
				const i = el('input', { type: 'number', className: 'ed-input num', value: p.node.child('Team' + n + 'Funds') ? p.node.child('Team' + n + 'Funds').value : '', placeholder: e.str('Team' + n + 'Funds', ''), disabled: ro, title: 'Gold it starts with' });
				i.onchange = () => set('team ' + n + '\'s funds', 'Team' + n + 'Funds', i.value.trim());
				return i;
			})(),
			el('small', { textContent: 'name, starting gold' })));
		const playerRows = TEAMS.map((n) => {
			const select = el('select', { className: 'ed-input', disabled: ro, title: 'Which team this player is on' }, el('option', { value: '', textContent: 'usual' }),
				...TEAMS.map((t) => el('option', { value: String(t - 1), textContent: 'Team ' + t, selected: p.node.child('TeamOfPlayer' + n) && p.node.child('TeamOfPlayer' + n).value === String(t - 1) })));
			select.onchange = () => set('player ' + n + '\'s team', 'TeamOfPlayer' + n, select.value);
			return el('div', { className: 'ed-row' }, el('span', { className: 'ed-label', textContent: 'Player ' + n }), select);
		});
		const cpu = el('select', { className: 'ed-input', disabled: ro, title: 'The team the computer plays' }, el('option', { value: '', textContent: 'none unless the script says' }),
			...TEAMS.map((t) => el('option', { value: String(t - 1), textContent: 'Team ' + t, selected: p.node.child('CPUTeam') && p.node.child('CPUTeam').value === String(t - 1) })));
		cpu.onchange = () => set('the computer team', 'CPUTeam', cpu.value);

		// scenario settings
		const settings = e.all('AddScenarioSetting').map((entry) => ({ node: entry.node, own: entry.node.isWithin(p.node), s: parseSetting(entry.node.value) })).filter((x) => x.s);
		const settingRows = settings.map((x) => {
			const editable = x.own && !ro;
			const field = (key, width, title) => {
				const input = el('input', { type: 'text', className: 'ed-input' + (width ? ' num' : ''), value: x.s[key], disabled: !editable, title });
				input.onchange = () => change('Change setting ' + x.s.key, () => x.node.setValue(settingText(Object.assign({}, x.s, { [key]: input.value.trim() }))));
				return input;
			};
			return el('div', { className: 'ed-row scn-setting' + (x.own ? '' : ' inherited') }, field('key', true, 'Its name in the script (no spaces): self:GetScenarioSetting("' + x.s.key + '")'), field('label', false, 'What the player sees'), field('unit', true, 'The unit shown beside the number'),
				field('min', true, 'Smallest'), field('max', true, 'Largest'), field('step', true, 'Step'), field('def', true, 'Starting value (or Auto)'),
				el('button', { className: 'ico-btn', textContent: '×', title: 'Take it out', disabled: !editable, onclick: () => change('Take setting ' + x.s.key + ' out', () => x.node.remove()) }));
		});
		const addSetting = el('button', { className: 'tb', textContent: '+ Add a setting', disabled: ro, onclick: () => change('Add a setting to ' + p.name, () => p.node.add('AddScenarioSetting', 'Difficulty | Enemy strength | Multiplier | 0.25 | 3 | 0.25 | 1')) });

		// required areas
		const areas = e.all('AddRequiredArea').map((entry) => ({ node: entry.node, own: entry.node.isWithin(p.node), name: entry.node.value }));
		const sceneEntity = scenes.find((x) => x.name === sceneName);
		const sceneAreas = sceneEntity ? a.lib.resolve(sceneEntity.node).all('AddArea').map((x) => x.node.childValue('Name', '')) : [];
		const areaChoice = el('select', { className: 'ed-input', disabled: ro }, ...[...new Set(sceneAreas)].map((n) => el('option', { value: n, textContent: n })));
		const areaRows = areas.map((r) => el('div', { className: 'ed-row' + (r.own ? '' : ' inherited') }, el('span', { textContent: r.name }), sceneAreas.length && !sceneAreas.includes(r.name) ? el('small', { className: 'scn-warn', textContent: 'the scene has no area with this name' }) : null,
			el('button', { className: 'ico-btn', textContent: '×', disabled: !r.own || ro, onclick: () => change('Take area ' + r.name + ' out', () => r.node.remove()) })));

		// script
		const scriptPath = e.str('ScriptPath', '');
		const className = e.str('LuaClassName', '');
		const code = scriptText(scriptPath);
		const have = new Set(code ? [...code.matchAll(/function\s+[A-Za-z0-9_]+[:.]([A-Za-z0-9_]+)\s*\(/g)].map((m) => m[1]) : []);
		const callbackRows = CALLBACKS.map(([name, args, help]) => el('div', { className: 'ed-row', title: help },
			el('span', { className: 'ed-label', textContent: name }), have.has(name) ? el('b', { textContent: 'in the script' }) : el('button', { className: 'tb', textContent: 'Add it', disabled: ro || !code || !className, onclick: () => addCallback(scriptPath, className, [name, args]) }), el('small', { textContent: help })));
		const play = el('button', { className: 'tb primary', textContent: '▶ Play in the game', title: 'Starts it in a game running from Studio (Test in game or Sandbox)', onclick: () => {
			MS.ide.runLua('ActivityMan:StartActivity(' + JSON.stringify(p.cls) + ', ' + JSON.stringify(p.name) + ')');
			a.status('Told the game to start ' + p.name + '. It only works in a game started from Studio.');
		} });

		return el('div', {},
			el('div', { className: 'ed-card-head' }, el('h3', { textContent: p.name }), el('span', { className: 'spacer' }), play,
				scriptPath ? el('button', { className: 'tb', textContent: 'Open the script', onclick: () => MS.ide.openFile(scriptPath, 1) }) : null,
				el('button', { className: 'ico-btn', textContent: 'Its lines', onclick: () => MS.ide.openFile(p.doc.path, p.node.line) })),
			el('div', { className: 'ed-label', textContent: 'Story' }),
			el('div', { className: 'ed-row' }, el('span', { className: 'ed-label', textContent: 'Plays on' }), scene),
			el('div', { className: 'ed-row' }, el('span', { className: 'ed-label', textContent: 'Description' }), desc),
			text('InCampaignStage', 'Campaign stage', 'The stage of the campaign it appears in (1 is the start)', 'number'),
			el('div', { className: 'ed-label', textContent: 'Teams and players' }),
			text('MinTeamsRequired', 'Teams needed', 'The fewest teams it can be played with', 'number'),
			text('MaxPlayerSupport', 'Most players', 'How many human players it allows', 'number'),
			...teamRows, ...playerRows, el('div', { className: 'ed-row' }, el('span', { className: 'ed-label', textContent: 'Computer plays' }), cpu),
			el('div', { className: 'ed-label', textContent: 'Rules a player starts with' }),
			el('div', { className: 'ed-settings' },
				flag('DefaultFogOfWar', 'Fog of war on', 'Unexplored ground is hidden', false),
				flag('DefaultRequireClearPathToOrbit', 'Units need a clear sky above them to be sent in', 'Craft need open air to land', true),
				flag('DefaultDeployUnits', 'Deploy units placed in the scene', 'Pre-placed units go into the scene at the start', false),
				flag('BuyMenuEnabled', 'The buy menu works', 'Players can buy units', true),
				flag('FogOfWarSwitchEnabled', 'Players may change fog of war', '', true),
				flag('DeployUnitsSwitchEnabled', 'Players may change unit deploying', '', false),
				flag('RequireClearPathToOrbitSwitchEnabled', 'Players may change the clear sky rule', '', true),
				flag('GoldSwitchEnabled', 'Players may change starting gold', '', true)),
			el('div', { className: 'ed-label', textContent: 'Settings the player gets (' + settings.length + ')' }),
			el('p', { className: 'ed-note', textContent: 'key, label, unit, smallest, largest, step, starting value. The script reads one with self:GetScenarioSetting("key").' }),
			...settingRows, el('div', { className: 'ed-row' }, addSetting),
			el('div', { className: 'ed-label', textContent: 'Areas the scene must have (' + areas.length + ')' }), ...areaRows,
			el('div', { className: 'ed-row' }, areaChoice, el('button', { className: 'tb', textContent: '+ Require it', disabled: ro || !sceneAreas.length, onclick: () => areaChoice.value && change('Require the area ' + areaChoice.value, () => p.node.add('AddRequiredArea', areaChoice.value)) }), el('small', { textContent: sceneAreas.length ? 'areas of ' + sceneName : 'pick a scene to list its areas' })),
			el('div', { className: 'ed-label', textContent: 'Mission logic: when this happens, and if these hold, do these things' }), MS.missions.section(p, e, ro, sceneAreas, missionHost),
			el('div', { className: 'ed-label', textContent: 'Script' }),
			text('ScriptPath', 'Script file', 'The Lua file that runs it'), text('LuaClassName', 'Script class', 'The name its functions start with (Class:StartActivity)'),
			code === null && scriptPath ? el('p', { className: 'ed-note', textContent: 'Studio has not read ' + scriptPath + ' (it is not in a mod it knows, or is not there).' }) : null,
			...(scriptPath && className ? callbackRows : []));
	}

	// ---- a new scenario ----

	// From nothing, as the New file dialog does (a scripted activity on a scene), or as a copy of one that works: its preset, with its script copied
	// into this mod under the new class name, so the copy runs as the original does and can then be changed.
	function newScenario(module, s) {
		const a = app();
		const scenes = a.lib.presets.filter((p) => p.topLevel && p.cls === 'Scene').sort((x, y) => x.name.localeCompare(y.name));
		const scripted = a.lib.presets.filter((p) => p.topLevel && p.node.key === 'AddActivity' && p.cls === 'GAScripted').sort((x, y) => x.name.localeCompare(y.name));
		const name = el('input', { type: 'text', className: 'ed-input', placeholder: 'My Scenario' });
		const from = el('select', { className: 'ed-input' }, el('option', { value: '', textContent: 'Nothing: an empty scenario with a script to fill in' }),
			...scripted.map((p, i) => el('option', { value: String(i), textContent: 'A copy of ' + p.name + (p.module === 'Base.rte' ? '' : ' (' + p.module.replace(/[.]rte$/i, '') + ')') })));
		const scene = el('select', { className: 'ed-input' }, ...scenes.map((p) => el('option', { value: p.name, textContent: p.name })));
		const { field } = MS.dialogs.parts;
		MS.dialogs.open('New scenario', el('div', {}, field('Name', name, 'What it is called in the game.'), field('Start from', from, 'A copy brings its script, which already works.'), field('Scene', scene, 'The scene it plays on (a copy keeps its own unless you change it).')), [
			{ text: 'Cancel' },
			{ text: 'Make it', primary: true, action: async () => {
				const n = name.value.trim();
				if (!n) {
					return false;
				}
				if (a.lib.presets.some((p) => p.node.key === 'AddActivity' && p.name.toLowerCase() === n.toLowerCase())) {
					a.toast('There is already a scenario called ' + n + '.', 'bad');
					return false;
				}
				const base = from.value === '' ? null : scripted[Number(from.value)];
				const stem = n.replace(/[^A-Za-z0-9 _-]/g, '').trim().replace(/ +/g, '_') || 'MyScenario';
				const path = module + '/Activities/' + stem + '.ini';
				if (!base) {
					const made = await MS.ide.makePresetFile(path, 'activity', scene.value);
					if (made) {
						MS.ide.refreshForm();
					}
					return made;
				}
				if (await a.fs.exists(path)) {
					a.toast(path + ' is already there.', 'bad');
					return false;
				}
				const baseEnt = a.lib.resolve(base.node);
				const oldClass = baseEnt.str('LuaClassName', '');
				const oldScript = baseEnt.str('ScriptPath', '');
				const newClass = n.replace(/[^A-Za-z0-9_]/g, '') || 'MyScenario';
				const newScript = module + '/Activities/' + newClass + '.lua';
				let code = null;
				if (oldScript) {
					code = scriptText(oldScript);
					if (code === null) {
						try {
							code = await a.fs.readText(oldScript);
						} catch (err) {
							code = null;
						}
					}
				}
				if (code !== null && oldClass) {
					code = code.split(oldClass + ':').join(newClass + ':').split(oldClass + '.').join(newClass + '.');
					if (!(await a.fs.exists(newScript))) {
						await writeScript(newScript, code);
					}
				}
				const doc = await a.lib.newDoc(path);
				a.hookDoc(doc);
				a.hookDoc(a.lib.docFor(module + '/Index.ini'));
				change('New scenario ' + n, () => {
					const block = doc.addTopLevel('AddActivity', base.cls);
					block.add('CopyOf', a.lib.reference(base.name, base.module, module));
					block.add('PresetName', n);
					if (scene.value) {
						block.add('SceneName', scene.value);
					}
					if (code !== null && oldClass) {
						block.add('ScriptPath', newScript);
						block.add('LuaClassName', newClass);
					}
				});
				const made = a.lib.presets.find((p) => p.doc === doc && p.name === n);
				if (made) {
					s.selected = made.doc.path + ':' + made.name;
				}
				await a.save(true, [doc, a.lib.docFor(module + '/Index.ini')].filter(Boolean));
				MS.ide.refreshForm();
				a.toast('Made ' + n + ' in ' + module + (code !== null ? ', with its own copy of the script' : '') + '.', 'good');
				return true;
			} },
		]);
	}

	if (MS.editors && MS.editors.register) {
		MS.editors.register(['scenarios', 'Scenarios', 'Ways to play: scene, teams, rules, settings and script', '⚑', render, (module) => activitiesOf(module).length]);
	}
	MS.scenarios = { render };
})();
