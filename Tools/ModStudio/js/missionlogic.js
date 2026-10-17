// Cortex Mod Studio: a mission's logic in the Mission Editor. Keeps each scenario's graph (read from its script, flowlua.js), with its own undo and redo,
// shows it in a flow view (flowview.js) beside the map, and writes it back into the script a moment after each change. Links the two sides: right-click
// an area on the map to make it an objective or a trigger; picking a node lights up the areas it uses, and picking an area lights up the nodes that use it.
// The script is still the only place the mission is kept; editing the script by hand (outside Studio's block, or the block itself) is picked up here.
'use strict';
window.MS = window.MS || {};

(function () {
	const app = () => MS.app;
	const F = () => MS.flow;
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
	const keyOf = (p) => p.doc.path + '::' + p.name;
	const docs = new Map(); // scenario key -> { g, undo, redo, text, path, cls, view state }
	const reading = new Set(); // scripts asked for once, for a window that had not read them yet

	// ---- the script ----

	function scriptOf(scenario) {
		const ent = app().lib.resolve(scenario.node);
		return { path: ent.str('ScriptPath', ''), cls: ent.str('LuaClassName', ''), scripted: scenario.cls === 'GAScripted' };
	}

	function scriptText(path) {
		const want = String(path || '').toLowerCase();
		for (const s of MS.ide.lua.scripts.values()) {
			if (s.path.toLowerCase() === want) {
				return s.text;
			}
		}
		return null;
	}

	async function writeScript(path, text) {
		const tab = MS.ide.tabs.find((t) => t.path === path && t.model);
		if (tab) {
			tab.model.pushEditOperations([], [{ range: tab.model.getFullModelRange(), text }], () => null);
			tab.savedText = text;
		}
		await app().fs.writeFile(path, text);
		MS.ide.lua.updateScript(path, text);
	}

	// The scenario's graph, read again when the script changed outside the editor (by hand, or another window).
	function docFor(scenario) {
		const key = keyOf(scenario);
		const { path, cls } = scriptOf(scenario);
		let doc = docs.get(key);
		const text = scriptText(path);
		if (!doc) {
			doc = { undo: [], redo: [], text: null, path, cls, state: {}, saving: 0 };
			docs.set(key, doc);
		}
		doc.path = path;
		doc.cls = cls;
		if (text !== doc.text || !doc.g) {
			doc.g = (text && F().parse(text)) || F().create();
			doc.text = text;
			doc.undo = [];
			doc.redo = [];
		}
		return doc;
	}

	function save(doc) {
		clearTimeout(doc.saving);
		doc.saving = setTimeout(async () => {
			const current = scriptText(doc.path);
			if (current === null || !doc.cls) {
				return;
			}
			const text = F().apply(current, doc.cls, doc.g, doc.path);
			doc.text = text;
			try {
				await writeScript(doc.path, text);
				if (doc.onSaved) {
					doc.onSaved();
				}
			} catch (e) {
				app().toast('Couldn\'t save the mission into ' + doc.path + ': ' + e.message, 'bad');
			}
		}, 350);
	}

	// ---- changing the graph (undo, redo) ----

	// Undo or redo for a mission tab (Ctrl+Z): its graph's history when the graph changed last, else false (Studio's own then undoes the
	// changes to the mod's files, such as areas drawn on the map).
	function historyStep(scenario, forward) {
		const doc = docs.get(keyOf(scenario));
		if (!doc || !(forward ? doc.redo : doc.undo).length) {
			return false;
		}
		const appStack = forward ? app().redo : app().undo;
		const appLast = appStack.length ? appStack[appStack.length - 1].at || 0 : 0;
		if (appLast > (doc.lastAt || 0)) {
			return false;
		}
		forward ? redo(doc) : undo(doc);
		return true;
	}

	function change(doc, label, fn) {
		const before = JSON.stringify(doc.g);
		fn(doc.g);
		if (JSON.stringify(doc.g) === before) {
			return;
		}
		doc.undo.push(before);
		if (doc.undo.length > 200) {
			doc.undo.shift();
		}
		doc.redo = [];
		doc.lastLabel = label;
		after(doc);
	}

	function after(doc) {
		doc.lastAt = Date.now(); // when the graph last changed (Ctrl+Z picks the newer history: the graph's or the files')
		if (doc.hint && doc.g.nodes.length) {
			doc.hint.remove(); // the how-to goes once there is something to show
			doc.hint = null;
		}
		if (doc.view) {
			doc.view.only = focusIds(doc); // a stage's nodes as they are now, with any just made
			doc.view.render();
		}
		if (doc.refreshBar) {
			doc.refreshBar();
		}
		if (doc.refreshStages) {
			doc.refreshStages();
		}
		if (doc.ctx && doc.ctx.changed) {
			doc.ctx.changed();
		}
		save(doc);
	}

	function undo(doc) {
		if (!doc.undo.length) {
			return;
		}
		doc.redo.push(JSON.stringify(doc.g));
		doc.g = JSON.parse(doc.undo.pop());
		after(doc);
	}

	function redo(doc) {
		if (!doc.redo.length) {
			return;
		}
		doc.undo.push(JSON.stringify(doc.g));
		doc.g = JSON.parse(doc.redo.pop());
		after(doc);
	}

	// ---- the areas a node uses ----

	function areasOfNode(n) {
		const names = [];
		const spec = F().KINDS[n.kind];
		for (const [key, type] of spec ? spec.params : []) {
			if (type === 'area' && n.p) {
				names.push(...F().idsOf(n.p[key]));
			}
		}
		for (const c of (n.lists && n.lists.conds) || []) {
			if (c.area) {
				names.push(c.area);
			}
		}
		return names;
	}

	// What an area is for, from the nodes that use it (for the map's filter): spawning units, landing (craft and landing zones), capture points,
	// objectives (stages), triggers (events and conditions about an area), orders (where units are sent), doors and map objects, and the view
	// (fog, painting, the camera).
	const AREA_ROLES = [['spawn', 'Spawning'], ['landing', 'Landing (craft and LZs)'], ['capture', 'Capture points'], ['objective', 'Objectives (stages)'], ['trigger', 'Triggers (events and conditions)'], ['orders', 'Where units are sent'], ['objects', 'Doors and map objects'], ['view', 'Fog and painting']];
	function rolesOfNode(n) {
		const roles = []; // [area name, role]
		const p = n.p || {};
		const spec = F().KINDS[n.kind];
		const cat = spec ? spec.category : '';
		for (const [key, type] of spec ? spec.params : []) {
			const list = F().idsOf(p[key]);
			if (type !== 'area' || !list.length || !F().relevant(spec, p, key)) {
				continue;
			}
			let role = 'trigger';
			if (key === 'goTo') {
				role = 'orders';
			} else if (n.kind === 'spawn') {
				role = p.arrive === 'dropship' || p.arrive === 'rocket' ? 'landing' : 'spawn';
			} else if (n.kind === 'playerStart') {
				role = 'spawn';
			} else if (n.kind === 'lz') {
				role = 'landing';
			} else if (n.kind === 'capture') {
				role = 'capture';
			} else if (cat === 'stage') {
				role = 'objective';
			} else if (['doors', 'placeObject', 'remove', 'placeItem', 'changeTeam'].includes(n.kind)) {
				role = 'objects';
			} else if (['fog', 'overlay', 'camera'].includes(n.kind)) {
				role = 'view';
			} else if (n.kind === 'order' || n.kind === 'control') {
				role = 'orders';
			}
			list.forEach((name) => roles.push([name, role]));
		}
		for (const c of (n.lists && n.lists.conds) || []) {
			if (c.area) {
				roles.push([c.area, 'trigger']);
			}
		}
		return roles;
	}
	// Each area's roles: name -> Set of role ids.
	function areaRoles(scenario) {
		const doc = docs.get(keyOf(scenario));
		const out = new Map();
		const add = (name, role) => {
			if (!out.has(name)) {
				out.set(name, new Set());
			}
			out.get(name).add(role);
		};
		for (const n of doc ? doc.g.nodes : []) {
			rolesOfNode(n).forEach(([name, role]) => add(name, role));
			// a stage about a capture point makes that point's areas objectives too
			const spec = F().KINDS[n.kind];
			if (spec && spec.category === 'stage') {
				spec.params.forEach(([key, type]) => {
					const point = type === 'capture' && n.p && F().find(doc.g, n.p[key]);
					(point ? rolesOfNode(point) : []).forEach(([name]) => add(name, 'objective'));
				});
			}
		}
		return out;
	}

	// ---- a script for a scenario that has none ----

	async function giveScript(scenario) {
		const a = app();
		if (a.lib.isProtected(scenario.doc)) {
			a.toast('This scenario is not yours to change. Copy it into your mod first.', 'bad');
			return;
		}
		const cls = F().className(scenario.name, 'MyMission');
		const path = scenario.module + '/Activities/' + cls + '.lua';
		if (!(await a.fs.exists(path))) {
			const text = ['-- ' + scenario.name + ': a scripted activity. The Mission Editor keeps its logic in the marked block it adds; your own code goes outside it.', '',
				'function ' + cls + ':StartActivity(isNewGame)', '\t', 'end', '', 'function ' + cls + ':UpdateActivity()', '\tself:RunStudioFlow()', 'end', ''].join('\n');
			await writeScript(path, text);
		}
		a.edit('Give ' + scenario.name + ' a script', () => {
			scenario.node.set('ScriptPath', path);
			scenario.node.set('LuaClassName', cls);
		});
	}

	// A script class Lua cannot read ("1234": a name has to start with a letter) gets a name it can, in the script and in the scenario. Only where the
	// script uses it as the class (Class:Method, Class.field), not in file names or text.
	async function fixClassName(scenario) {
		const a = app();
		const { path, cls } = scriptOf(scenario);
		const fixed = F().className(cls, 'MyMission');
		const text = scriptText(path) || (await a.fs.readText(path));
		const asClass = new RegExp('(?<![A-Za-z0-9_/.])' + cls.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + '(?=[:.][A-Za-z_])', 'g');
		await writeScript(path, text.replace(asClass, fixed));
		a.edit('Rename ' + scenario.name + '\'s script class to ' + fixed, () => scenario.node.set('LuaClassName', fixed));
		a.toast('The script class is now ' + fixed + ', in the script and the scenario.', 'good');
	}

	// ---- the panel ----

	// host: the element to fill; ctx from the Mission Editor: areas(), highlight(names), pickArea(name), changed().
	function render(host, scenario, ctx) {
		const a = app();
		const locked = a.lib.isProtected(scenario.doc);
		const { path, cls, scripted } = scriptOf(scenario);
		host.classList.add('ml');
		if (!scripted) {
			host.replaceChildren(el('div', { className: 'ed-banner', textContent: scenario.name + ' is a ' + scenario.cls + ', which runs the game\'s own code rather than a script, so it has no mission logic to build. Make a scenario of your own (Scenarios, + New scenario) to build one.' }));
			return;
		}
		if (!path || !cls) {
			host.replaceChildren(el('div', { className: 'ml-empty' },
				el('p', { textContent: 'This scenario has no script yet, and the mission is kept in its script.' }),
				el('button', { className: 'tb primary', textContent: 'Give it a script', disabled: locked, onclick: () => giveScript(scenario) })));
			return;
		}
		if (!F().isClassName(cls)) {
			host.replaceChildren(el('div', { className: 'ml-empty' },
				el('p', { textContent: 'The script class "' + cls + '" is not a name Lua can use (it has to start with a letter, and have only letters, digits and _), so the script cannot run.' }),
				el('button', { className: 'tb primary', textContent: 'Rename it to ' + F().className(cls), disabled: locked, onclick: () => fixClassName(scenario).catch((e) => a.toast('Couldn\'t rename it: ' + e.message, 'bad')) })));
			return;
		}
		if (scriptText(path) === null) {
			// A window just opened (a pop-out) has not read the scripts yet: read them, or failing that this one file, and draw again.
			if (!reading.has(path)) {
				reading.add(path);
				host.replaceChildren(el('p', { className: 'ed-note', textContent: 'Reading ' + path + '…' }));
				MS.ide.ensureScripts()
					.then(async () => {
						if (scriptText(path) === null) {
							MS.ide.lua.updateScript(path, await app().fs.readText(path));
						}
					})
					.catch(() => {})
					.finally(() => MS.ide.refreshForm());
				return;
			}
			host.replaceChildren(el('div', { className: 'ed-banner', textContent: 'Studio could not read ' + path + ': it is not in a mod Studio knows, or not there.' }));
			return;
		}
		const doc = docFor(scenario);
		doc.ctx = ctx;
		const problems = () => F().problems(doc.g, { areas: ctx.areas(), codeStages: scriptStages(path, ctx).map((c) => c.name) });
		const pickers = MS.scenarios && MS.scenarios.missionHost;
		const view = new MS.FlowView({
			state: doc.state,
			graph: () => doc.g,
			readOnly: locked,
			areas: ctx.areas,
			problems,
			change: (label, fn) => change(doc, label, fn),
			snapshot: () => JSON.stringify(doc.g),
			commit: (label, before) => {
				if (before !== JSON.stringify(doc.g)) {
					doc.undo.push(before);
					doc.redo = [];
					after(doc);
				}
			},
			undo: () => undo(doc),
			redo: () => redo(doc),
			pickUnit: (cb) => pickers.pickUnit(cb),
			pickItem: (cb) => pickers.pickItem(cb),
			highlight: (names) => ctx.highlight(names),
			suggests: (kind, key) => suggests(doc, path, kind, key),
			codeStages: () => scriptStages(path, ctx).map((c) => ({ name: c.name, label: words(c.name) })),
			editCode: (code, set, ro) => codeDialog(path, cls, code, set, ro),
			selected: (ids) => {
				const names = new Set();
				for (const id of ids) {
					const n = F().find(doc.g, id);
					if (n) {
						areasOfNode(n).forEach((x) => names.add(x));
					}
				}
				ctx.highlight([...names]);
			},
			openFn: (name) => {
				const code = scriptText(path) || '';
				const fn = F().ident(name);
				const line = Math.max(1, code.split('\n').findIndex((l) => l.includes(cls + ':' + fn + '(')) + 1);
				MS.ide.openFile(path, line);
			},
		});
		doc.view = view;
		const bar = el('div', { className: 'ml-bar' });
		doc.refreshBar = () => {
			const found = problems();
			const errors = found.filter((p) => p.level === 'error').length;
			const warns = found.length - errors;
			bar.replaceChildren(
				el('button', { className: 'tb', textContent: '↶', title: 'Undo (Ctrl+Z in the graph)', disabled: !doc.undo.length, onclick: () => undo(doc) }),
				el('button', { className: 'tb', textContent: '↷', title: 'Redo (Ctrl+Y)', disabled: !doc.redo.length, onclick: () => redo(doc) }),
				el('button', { className: 'tb', textContent: '+ Node', disabled: locked, title: 'Or right-click empty space in the graph', onclick: (e) => {
					const r = view.root.getBoundingClientRect();
					const at = view.toWorld(r.left + r.width / 2, r.top + r.height / 3);
					view.palette(e.clientX, e.clientY + 20, null, (k) => view.addAt(k, at, null));
				} }),
				el('button', { className: 'tb', textContent: 'Ready-made mission…', disabled: locked, onclick: () => templateDialog(doc, ctx) }),
				el('button', { className: 'tb', textContent: 'Fit', title: 'Show the whole graph (F)', onclick: () => view.fit() }),
				el('button', { className: 'tb', textContent: 'Tidy up', disabled: locked, title: 'Lay the whole graph out: each beginning (stages, capture points, events) in its own band, what it leads to running to the right, nothing on top of anything (Ctrl+Z puts it back)', onclick: () => {
					// each node's real height, as drawn (the layout's estimate when it isn't drawn)
					const measure = (n) => {
						const e = view.nodeEls.get(n.id);
						return e ? e.offsetHeight : 0;
					};
					measure.width = (n) => {
						const e = view.nodeEls.get(n.id);
						return e ? e.offsetWidth : 0;
					};
					change(doc, 'tidy up the graph', (g) => F().layout(g, true, measure));
					requestAnimationFrame(() => view.fit());
				} }),
				el('span', { className: 'spacer' }),
				found.length ? el('button', { className: 'tb ml-problems' + (errors ? ' bad' : ''), textContent: (errors ? errors + ' to fix' : '') + (errors && warns ? ', ' : '') + (warns ? warns + ' to check' : ''), title: found.map((p) => '• ' + p.message).join('\n'), onclick: () => {
					const first = found.find((p) => p.level === 'error') || found[0];
					view.pick([first.id]);
					const n = F().find(doc.g, first.id);
					if (n) {
						const r = view.root.getBoundingClientRect();
						view.state.view.x = r.width / 2 - n.x * view.state.view.z - 140;
						view.state.view.y = r.height / 3 - n.y * view.state.view.z;
						view.place();
					}
				} }) : el('span', { className: 'ml-ok', textContent: doc.g.nodes.length ? '✓ no problems' : '' }));
		};
		doc.refreshBar();
		const help = doc.g.nodes.length ? null : el('div', { className: 'ml-hint' },
			el('b', { textContent: 'Build the mission here.' }), ' Right-click empty space to add a node, or right-click an area on the map to make it an objective or a trigger. ',
			'Drag from a node\'s exit (the dots on its right) to wire what happens next. Or start from a ready-made mission.');
		const stages = stagesPanel(doc, scenario, ctx, path, locked);
		host.replaceChildren(...[locked ? el('div', { className: 'ed-banner', textContent: 'This scenario belongs to ' + scenario.module + ': its mission can be looked at, not changed.' }) : null, bar,
			el('div', { className: 'ml-body' }, stages, el('div', { className: 'ml-main' }, ...[help, view.root].filter(Boolean)))].filter(Boolean));
		doc.hint = help;
		view.only = focusIds(doc);
		view.render();
		doc.refreshStages();
		if (!doc.state.fitted) {
			doc.state.fitted = true;
			requestAnimationFrame(() => view.fit());
		}
	}

	// ---- your own code ----

	// A "Run my own code" node's Lua, written in the code editor. The editor shows it as the body of one of the script's own methods (the first
	// two lines and the last are hidden), so completions know self is the mission, and st and actor are there, as they are when it runs.
	function codeDialog(path, cls, code, set, readOnly) {
		const a = app();
		const head = ['function ' + cls + ':StudioCode(actor)', '\tlocal st = self.StudioState'];
		const body = String(code || '').split('\n').map((l) => '\t' + l);
		const host = el('div', { className: 'ml-codehost' }, el('p', { className: 'ed-note', textContent: 'Opening the code editor…' }));
		const status = el('div', { className: 'ml-codestatus' });
		let editor = null;
		let model = null;
		const dispose = () => {
			if (editor) {
				editor.dispose();
			}
			if (model) {
				model.dispose();
			}
			editor = null;
			model = null;
		};
		// the body back out of the editor: the hidden lines dropped (also when a backspace has joined the first line on to one of them)
		const taken = () => {
			const lines = model.getValue().split('\n');
			if (/StudioCode\(/.test(lines[0] || '')) {
				lines.shift();
			}
			const st = /^\s*local st = self\.StudioState(.*)$/.exec(lines[0] || '');
			if (st) {
				lines[0] = st[1];
				if (!lines[0].trim()) {
					lines.shift();
				}
			}
			if (lines.length && lines[lines.length - 1].trim() === 'end') {
				lines.pop();
			}
			return lines.map((l) => l.replace(/^\t/, '')).join('\n').replace(/\s+$/, '');
		};
		MS.dialogs.open('Your own code', el('div', { className: 'ml-codebox' },
			el('p', { className: 'ed-note', textContent: 'Lua, run where the node is. self is the mission, st its state (st.vars.name, st.flags.name) and actor the unit that set this chain off (nil when there is none). Ctrl+Space suggests what can go next. If it goes wrong as it runs, the error is written to the console and the mission carries on.' }),
			host, status), [
			{ text: readOnly ? 'Close' : 'Cancel', action: () => {
				dispose();
				return true;
			} },
			...(readOnly ? [] : [{ text: 'Use this code', primary: true, action: () => {
				if (model) {
					// Lua that does not read stops the whole script loading, not just this node
					const broken = MS.ide.lua && MS.ide.lua.check ? MS.ide.lua.check(path, model.getValue()).find((p) => p.severity === 'error' && p.line > 2) : null;
					if (broken) {
						a.toast('Fix the code first: ' + broken.message + '. Code Lua cannot read stops the whole mission script.', 'bad');
						return false;
					}
					set(taken());
				}
				dispose();
				return true;
			} }]),
		], true);
		MS.ide.loadMonaco().then(() => {
			const monaco = MS.ide.monaco;
			if (!host.isConnected) {
				return; // the dialog was closed while the editor loaded
			}
			const uri = monaco.Uri.from({ scheme: 'studio-code', path: '/' + path });
			const old = monaco.editor.getModel(uri);
			if (old) {
				old.dispose();
			}
			model = monaco.editor.createModel([...head, ...(body.length ? body : ['\t']), 'end'].join('\n'), 'lua', uri);
			host.replaceChildren();
			editor = monaco.editor.create(host, {
				theme: 'cortex', automaticLayout: true, fontSize: 13, fontFamily: 'ui-monospace, Consolas, "Cascadia Mono", monospace',
				minimap: { enabled: false }, tabSize: 4, insertSpaces: false, scrollBeyondLastLine: false, fixedOverflowWidgets: true,
				suggest: { showStatusBar: true, preview: true }, quickSuggestions: { other: true, comments: false, strings: true }, readOnly, model,
			});
			const hide = () => {
				const last = model.getLineCount();
				if (editor && editor.setHiddenAreas) {
					editor.setHiddenAreas([new monaco.Range(1, 1, 2, 1), new monaco.Range(last, 1, last, 1)]);
				}
			};
			hide();
			// problems as you type, from the same checks as the script editor
			let timer = 0;
			const check = () => {
				if (!model || !MS.ide.lua || !MS.ide.lua.check) {
					return;
				}
				const found = MS.ide.lua.check(path, model.getValue()).filter((p) => p.line > 2 && p.line < model.getLineCount());
				const severity = monaco.MarkerSeverity;
				monaco.editor.setModelMarkers(model, 'studio', found.map((p) => ({
					startLineNumber: p.line, startColumn: p.col, endLineNumber: p.endLine || p.line, endColumn: p.endCol || p.col + 1,
					severity: p.severity === 'error' ? severity.Error : p.severity === 'warning' ? severity.Warning : severity.Info, message: p.message,
				})));
				const errors = found.filter((p) => p.severity === 'error');
				status.textContent = errors.length ? '✗ ' + errors[0].message + ' (line ' + (errors[0].line - 2) + ')' : '✓ no problems found';
				status.classList.toggle('bad', !!errors.length);
			};
			model.onDidChangeContent(() => {
				hide();
				clearTimeout(timer);
				timer = setTimeout(check, 300);
			});
			check();
			editor.setPosition({ lineNumber: 3, column: model.getLineMaxColumn(3) });
			editor.focus();
		}).catch((e) => {
			host.replaceChildren(el('p', { className: 'ed-banner', textContent: 'The code editor could not open: ' + e.message }));
			a.toast('The code editor could not open.', 'bad');
		});
	}

	// ---- what a text setting offers to pick ----

	// The game's presets by class, the groups units are put in, and the mods: read once per load of the library.
	let libMemo = null;
	const ACTOR_CLASSES = ['AHuman', 'ACrab', 'Actor', 'Turret', 'AMech', 'ACRocket', 'ACDropShip', 'ACShip', 'AVehicle', 'ADoor'];
	function libLists() {
		const lib = app().lib;
		if (!lib || !lib.presets) {
			return null;
		}
		if (libMemo && libMemo.lib === lib && libMemo.gen === lib.generation) {
			return libMemo;
		}
		const all = Array.isArray(lib.presets) ? lib.presets : Object.values(lib.presets);
		const byCls = new Map();
		const groups = new Set();
		const modules = new Set();
		for (const p of all) {
			if (!byCls.has(p.cls)) {
				byCls.set(p.cls, []);
			}
			byCls.get(p.cls).push(p);
			modules.add(p.module);
			if (ACTOR_CLASSES.includes(p.cls) && p.node && p.node.children) {
				p.node.children.forEach((c) => c.key === 'AddToGroup' && c.value && groups.add(String(c.value).trim()));
			}
		}
		libMemo = { lib, gen: lib.generation, byCls, groups: [...groups].sort(), modules: [...modules].filter((m) => /\.rte$/i.test(m || '')).sort(), lists: new Map() };
		return libMemo;
	}

	// Presets of these classes as things to pick: the value is the name, or name|module where the setting takes that.
	function presetItems(classes, withModule) {
		const L = libLists();
		if (!L) {
			return [];
		}
		const memoKey = classes.join() + withModule;
		if (!L.lists.has(memoKey)) {
			const seen = new Set();
			const out = [];
			for (const cls of classes) {
				for (const p of L.byCls.get(cls) || []) {
					const v = withModule ? p.name + '|' + p.module : p.name;
					if (p.name && !seen.has(v)) {
						seen.add(v);
						out.push({ v, t: p.name, sub: p.module + (classes.length > 1 ? ' · ' + p.cls : '') });
					}
				}
			}
			L.lists.set(memoKey, out.sort((a, b) => a.t.localeCompare(b.t)));
		}
		return L.lists.get(memoKey);
	}

	// Names this mission already uses: the groups it puts units in, its variables, flags and pie buttons.
	function missionNames(g) {
		const out = { groups: new Set(), vars: new Set(), flags: new Set(), pies: new Set() };
		const add = (set, v) => v && String(v).trim() && set.add(String(v).trim());
		const vars = (x) => {
			if (x && typeof x === 'object') {
				if (x.k === 'var') {
					add(out.vars, x.name);
				}
				Object.values(x).forEach(vars);
			}
		};
		for (const n of g.nodes) {
			const p = n.p || {};
			add(out.groups, p.tag);
			if (p.which === 'group' || n.kind === 'order' || n.kind === 'groupGone' || (n.kind === 'pieAdd' && p.to === 'group')) {
				add(out.groups, p.group);
			}
			if (n.kind === 'setVar' || n.kind === 'varReaches') {
				add(out.vars, p.name);
			}
			if (n.kind === 'flag' || n.kind === 'flagChanges') {
				add(out.flags, p.name);
			}
			if (/^pie/.test(n.kind)) {
				add(out.pies, p.label);
			}
			vars(p);
			for (const c of (n.lists && n.lists.conds) || []) {
				vars(c);
				if (c.kind === 'var') {
					add(out.vars, c.name);
				}
				if (c.kind === 'flag') {
					add(out.flags, c.name);
				}
				if (c.kind === 'group' || c.which === 'group') {
					add(out.groups, c.group);
				}
			}
		}
		return out;
	}

	// The values the script keeps on itself (self.something), outside what Studio writes.
	function scriptFields(path) {
		const text = String(scriptText(path) || '').replace(/-- >>> Studio flow[\s\S]*?-- <<< Studio flow/, '');
		const out = new Set();
		for (const m of text.matchAll(/self\.([A-Za-z_]\w*)\s*(?![\w(:])/g)) {
			out.add(m[1]);
		}
		return [...out].sort();
	}

	// What a text setting offers, as a function giving the items (see FlowView.combo), or null for plain text.
	function suggests(doc, path, kind, key) {
		const k = String(kind).replace(/^cond:/, '');
		const names = (set, sec) => [...set].sort().map((v) => ({ v, t: v, sec }));
		const groups = () => (libLists() ? libLists().groups : []).map((v) => ({ v, t: v, sec: 'Groups the game\'s units are in' }));
		const made = () => names(missionNames(doc.g).groups, 'Groups this mission puts units in');
		if (key === 'group') {
			// a tech's group to pick a random unit from, or a group of units on the map
			return k === 'spawn' || k === 'playerStart' ? groups : () => made().concat(groups());
		}
		if (key === 'tag') {
			return () => made().concat(groups());
		}
		if (key === 'named') {
			return () => presetItems(ACTOR_CLASSES, false);
		}
		if (key === 'loadout') {
			return () => presetItems(['Loadout'], false);
		}
		if (key === 'progressScreen' || key === 'idleScreen') {
			return () => presetItems(['MOSParticle'], true);
		}
		if (key === 'terminalOn' || key === 'terminalOff') {
			// a pair of presets told apart by Left / Right can be picked as one: {side} is filled in from where the point is
			return () => {
				const all = presetItems(['TerrainObject'], true);
				const sided = new Map();
				all.filter((i) => / (Left|Right)( |$|\|)/.test(i.v)).forEach((i) => {
					const v = i.v.replace(/ (Left|Right)( |$|\|)/, ' {side}$2');
					sided.set(v, { v, t: i.t.replace(/ (Left|Right)( |$)/, ' {side}$2'), sub: i.sub, sec: 'Left or right, by where it stands' });
				});
				return [...sided.values()].concat(all.map((i) => Object.assign({}, i, { sec: 'One preset' })));
			};
		}
		if (key === 'object') {
			return () => presetItems(['TerrainObject'], true);
		}
		if (key === 'song') {
			return () => presetItems(['DynamicSong'], false);
		}
		if (key === 'tech') {
			return () => (libLists() ? libLists().modules : []).map((v) => ({ v, t: v }));
		}
		if (key === 'name' && ['varReaches', 'setVar', 'var', 'value'].includes(k)) {
			return () => names(missionNames(doc.g).vars, 'Variables this mission uses');
		}
		if (key === 'name' && ['flag', 'flagChanges'].includes(k)) {
			return () => names(missionNames(doc.g).flags, 'Flags this mission uses');
		}
		if (key === 'label' && /^pie/.test(k)) {
			return () => names(missionNames(doc.g).pies, 'Buttons this mission makes');
		}
		if (key === 'field') {
			return () => scriptFields(path).map((v) => ({ v, t: 'self.' + v, sec: 'Values the script keeps' }));
		}
		return null;
	}

	// ---- stages ----

	// The nodes that belong to a stage: it, what runs from its exits (up to another stage), the capture points it or they name (the one it is
	// about, those it turns on) and what those lead to, and the nodes that name it (a jump to it, a check of it).
	function stageNodes(g, stage) {
		const ids = new Set([stage.id]);
		const points = (n) => {
			const spec = F().KINDS[n.kind];
			if (spec && spec.category !== 'event') {
				spec.params.forEach(([key, type]) => (type === 'capture' || type === 'captures') && n.p && F().idsOf(n.p[key]).forEach(walk));
			}
		};
		const walk = (id) => {
			const n = F().find(g, id);
			if (!n || ids.has(id) || F().isStage(n.kind)) {
				return;
			}
			ids.add(id);
			points(n);
			Object.values(n.out || {}).forEach(walk);
		};
		points(stage);
		Object.entries(stage.out || {}).forEach(([pin, to]) => pin !== 'next' && walk(to));
		// and the events about those capture points (one coming under attack, changing hands)
		const own = new Set([...ids].filter((id) => (F().find(g, id) || {}).kind === 'capture'));
		for (const n of g.nodes) {
			const spec = F().KINDS[n.kind];
			const named = (spec ? spec.params : []).some(([key, type]) => n.p && ((type === 'stage' && n.p[key] === stage.id) || (type === 'capture' && spec.category === 'event' && own.has(n.p[key])))) ||
				((n.lists && n.lists.conds) || []).some((c) => c.stage === stage.id);
			if (named) {
				ids.add(n.id);
				Object.values(n.out || {}).forEach(walk);
			}
		}
		return [...ids];
	}

	// The nodes to show while a stage is picked: its own, and any made since it was picked (so a new node doesn't vanish as it is added). null: all.
	function focusIds(doc) {
		const sel = doc.stageSel;
		const st = sel && sel.id && F().find(doc.g, sel.id);
		if (!st) {
			return null;
		}
		const ids = new Set(stageNodes(doc.g, st));
		doc.g.nodes.forEach((n) => !(sel.seen || []).includes(n.id) && ids.add(n.id));
		return ids;
	}

	// What the map shows while a stage is picked: { name, areas } (the areas its nodes use, or a script stage's), or null for the whole mission.
	function stageFocus(scenario) {
		const doc = docs.get(keyOf(scenario));
		const sel = doc && doc.stageSel;
		if (!sel) {
			return null;
		}
		if (sel.code) {
			const c = doc.ctx && doc.path ? scriptStages(doc.path, doc.ctx).find((x) => x.name === sel.code) : null;
			return c ? { name: words(c.name), areas: new Set(c.areas) } : null;
		}
		const st = F().find(doc.g, sel.id);
		if (!st) {
			return null;
		}
		const areas = new Set();
		[...focusIds(doc)].forEach((id) => areasOfNode(F().find(doc.g, id)).forEach((a) => areas.add(a)));
		const i = stageOrder(doc.g).indexOf(st);
		return { name: (st.p && st.p.name) || 'Stage ' + (i + 1), areas };
	}

	// Back to the whole mission (the map's "Show all").
	function clearStage(scenario) {
		const doc = docs.get(keyOf(scenario));
		if (doc && doc.stageSel) {
			doc.stageSel = null;
			if (doc.focus) {
				doc.focus(null);
			}
		}
	}

	// The graph's stages in the order they run: the first, then each one's next, then any not chained in.
	function stageOrder(g) {
		const order = [];
		const seen = new Set();
		for (let s = F().firstStage(g); s && !seen.has(s.id); s = F().find(g, (s.out || {}).next)) {
			seen.add(s.id);
			order.push(s);
		}
		return order.concat(F().stages(g).filter((s) => !seen.has(s.id)));
	}

	// Stages a script keeps in its own code, the way the game's missions do: self.stages = { name = 1, ... }, and self.currentStage compared with
	// them. For each: the lines that use it, and the scene areas named in the code that runs for it. The Studio block is left out.
	function codeStages(text, areaNames) {
		const lines = text.split('\n');
		const studio = new Set();
		let inBlock = false;
		lines.forEach((l, i) => {
			if (/^\s*-- >>> Studio /.test(l)) {
				inBlock = true;
			}
			if (inBlock) {
				studio.add(i);
			}
			if (/^\s*-- <<< Studio /.test(l)) {
				inBlock = false;
			}
		});
		const own = lines.map((l, i) => (studio.has(i) ? '' : l)).join('\n');
		const table = /self\.stages\s*=\s*\{([\s\S]*?)\}/.exec(own);
		if (!table) {
			return [];
		}
		const names = [...table[1].matchAll(/([A-Za-z_]\w*)\s*=\s*(\d+)/g)].sort((a, b) => Number(a[2]) - Number(b[2])).map((m) => m[1]);
		const known = new Set(areaNames);
		const indent = (l) => /^\s*/.exec(l)[0].length;
		return names.map((name) => {
			const uses = [];
			const areas = new Set();
			const re = new RegExp('self\\.stages\\.' + name + '(?!\\w)');
			lines.forEach((l, i) => {
				if (studio.has(i) || !re.test(l) || /self\.stages\s*=/.test(l)) {
					return;
				}
				uses.push({ line: i + 1, text: l.trim(), what: useOf(l, name) });
				// "if/elseif self.currentStage == self.stages.name ... then": the code up to the next elseif/else/end at its depth runs for this stage
				const quoted = [l];
				if (/==\s*self\.stages\./.test(l) && /\bthen\s*$/.test(l)) {
					const depth = indent(l);
					for (let j = i + 1; j < lines.length && j < i + 400; j++) {
						if (indent(lines[j]) <= depth && /^\s*(elseif|else|end)\b/.test(lines[j])) {
							break;
						}
						quoted.push(lines[j]);
					}
				}
				for (const q of quoted) {
					for (const m of q.matchAll(/"([^"\n]+)"|'([^'\n]+)'/g)) {
						const s = m[1] || m[2];
						if (known.has(s)) {
							areas.add(s);
						}
					}
				}
			});
			return { name, uses, areas: [...areas] };
		});
	}

	// What a line does with a stage, in words: moves to it, runs code on it, or checks for before or after it.
	function useOf(line, name) {
		const op = new RegExp('currentStage\\s*(==|~=|<=|>=|<|>|=)\\s*self\\.stages\\.' + name + '(?!\\w)').exec(line);
		switch (op && op[1]) {
			case '=': return 'moves to this stage';
			case '==': return /\bthen\s*$/.test(line) ? 'code that runs on this stage' : 'checks it is on this stage';
			case '~=': return 'checks it is not on this stage';
			case '<': return 'checks it is before this stage';
			case '<=': return 'checks it is up to this stage';
			case '>': return 'checks it is past this stage';
			case '>=': return 'checks it is at or past this stage';
			default: return 'uses this stage';
		}
	}

	// "attackFrontBunker" -> "Attack front bunker"
	const words = (name) => {
		const s = name.replace(/_/g, ' ').replace(/([a-z0-9])([A-Z])/g, '$1 $2').toLowerCase();
		return s.charAt(0).toUpperCase() + s.slice(1);
	};

	// The script's own stages, read once per version of its text.
	let codeMemo = { text: null, areas: '', list: [] };
	function scriptStages(path, ctx) {
		const text = scriptText(path) || '';
		const areaNames = ctx.areas();
		const areaKey = areaNames.join('\n');
		if (codeMemo.text !== text || codeMemo.areas !== areaKey) {
			codeMemo = { text, areas: areaKey, list: codeStages(text, areaNames) };
		}
		return codeMemo.list;
	}

	// The list beside the graph: the mission's stages in order, then the ones the script keeps in its own code. Picking one lights its nodes in
	// the graph and its areas on the map, and opens it to show what it is made of.
	function stagesPanel(doc, scenario, ctx, path, locked) {
		const box = el('div', { className: 'ml-side' });
		const chip = (a) => el('button', { className: 'ml-chip', textContent: a, title: 'Show it on the map', onclick: (e) => {
			e.stopPropagation();
			ctx.highlight([a]);
		} });
		const addStage = (e) => {
			doc.view.palette(e.clientX, e.clientY + 10, (k) => k.category === 'stage', (kind) => change(doc, 'add a stage', (g) => {
				const count = F().stages(g).length;
				// after the end of the chain that runs from the start
				let last = F().firstStage(g);
				for (const seen = new Set(); last && last.out && last.out.next && !seen.has(last.id); last = F().find(g, last.out.next)) {
					seen.add(last.id);
				}
				const at = last ? { x: last.x + 360, y: last.y } : freeSpot(g, 0);
				const n = F().addNode(g, kind, at.x, at.y, { name: 'Stage ' + (count + 1) });
				if (last) {
					last.out.next = n.id;
				}
				doc.stageSel = { id: n.id, seen: g.nodes.map((x) => x.id) };
			}));
		};
		// pick a stage (sel) or the whole mission (null): the graph shows only its nodes, the map only its areas
		doc.focus = (sel) => {
			const view = doc.view;
			doc.stageSel = sel ? Object.assign({ seen: doc.g.nodes.map((n) => n.id) }, sel) : null;
			view.pick([]);
			view.only = focusIds(doc);
			view.render();
			requestAnimationFrame(() => view.fit());
			if (ctx.stageChanged) {
				ctx.stageChanged();
			}
			draw();
		};
		const draw = () => {
			const g = doc.g;
			const view = doc.view;
			if (doc.state.stagesHidden) {
				box.classList.add('closed');
				box.replaceChildren(el('button', { className: 'ml-side-open', title: 'Show the stages', onclick: () => {
					doc.state.stagesHidden = false;
					draw();
				} }, '›', el('span', { textContent: 'Stages' })));
				return;
			}
			box.classList.remove('closed');
			const sel = doc.stageSel || null;
			const ordered = stageOrder(g);
			const coded = scriptStages(path, ctx);
			const rows = [];
			ordered.forEach((s, i) => {
				const spec = F().KINDS[s.kind];
				const on = !!sel && sel.id === s.id;
				const ids = stageNodes(g, s);
				const areas = new Set();
				ids.forEach((id) => areasOfNode(F().find(g, id)).forEach((a) => areas.add(a)));
				rows.push(el('div', { className: 'ml-st' + (on ? ' on' : ''), title: on ? 'Showing only this stage: click again (or Whole mission) to show everything' : 'Show only this stage\'s nodes, and its areas on the map', onclick: () => doc.focus(on ? null : { id: s.id }) },
				el('span', { className: 'ml-st-n', textContent: String(i + 1) }),
				el('div', { className: 'ml-st-text' },
					el('div', { className: 'ml-st-name', textContent: (s.p && s.p.name) || 'Stage ' + (i + 1) }),
					el('div', { className: 'ml-st-sub', textContent: spec ? spec.title.replace(/^Goal: /, '') : s.kind }),
					on ? el('div', { className: 'ml-st-more' },
						el('div', { textContent: ids.length + ' node' + (ids.length === 1 ? '' : 's') + ' in this stage' + (areas.size ? ', using:' : ', no areas') }),
						areas.size ? el('div', { className: 'ml-chips' }, ...[...areas].map(chip)) : null) : null)));
			});
			const codeRows = coded.map((c, i) => {
				const on = !!sel && sel.code === c.name;
				return el('div', { className: 'ml-st code' + (on ? ' on' : ''), title: 'Show only the areas its code names on the map', onclick: () => doc.focus(on ? null : { code: c.name }) },
				el('span', { className: 'ml-st-n', textContent: String(i + 1) }),
				el('div', { className: 'ml-st-text' },
					el('div', { className: 'ml-st-name', textContent: words(c.name), title: c.name }),
					el('div', { className: 'ml-st-sub', textContent: (c.areas.length ? c.areas.length + ' area' + (c.areas.length === 1 ? '' : 's') + ' · ' : '') + c.uses.length + ' line' + (c.uses.length === 1 ? '' : 's') + ' of code' }),
					on ? el('div', { className: 'ml-st-more' },
						c.areas.length ? el('div', { className: 'ml-chips' }, ...c.areas.map(chip)) : el('div', { textContent: 'Its code names no scene areas.' }),
						el('div', { className: 'ml-st-lines' }, ...c.uses.map((u) => el('button', { className: 'ml-line', title: u.text + '\n\nOpen the script here', onclick: (e) => {
							e.stopPropagation();
							MS.ide.openFile(path, u.line);
						} }, el('span', { className: 'ml-ln', textContent: u.line }), u.what)))) : null));
			});
			box.replaceChildren(
				el('div', { className: 'ml-side-head' },
					el('b', { textContent: 'Stages' }),
					el('span', { className: 'spacer' }),
					el('button', { className: 'tb', textContent: '+ Add', disabled: locked, title: 'Add a stage after the last one', onclick: addStage }),
					el('button', { className: 'ico-btn', textContent: '‹', title: 'Hide the stages', onclick: () => {
						doc.state.stagesHidden = true;
						draw();
					} })),
				el('div', { className: 'ml-side-list' },
					rows.length || coded.length ? el('div', { className: 'ml-st all' + (sel ? '' : ' on'), title: 'Show every node and every area', onclick: () => sel && doc.focus(null) },
						el('span', { className: 'ml-st-n', textContent: '∗' }),
						el('div', { className: 'ml-st-text' }, el('div', { className: 'ml-st-name', textContent: 'Whole mission' }),
							el('div', { className: 'ml-st-sub', textContent: g.nodes.length + ' node' + (g.nodes.length === 1 ? '' : 's') }))) : null,
					rows.length ? el('div', { className: 'ml-side-sec', textContent: 'Mission' }) : null,
					...rows,
					!rows.length ? el('p', { className: 'ml-side-empty', textContent: coded.length
						? 'None built here yet. Add one to follow on from the script\'s stages, or use the script\'s stages below in conditions and jumps.'
						: 'None yet. A stage is one step of the mission: a goal to meet, and what happens when it starts and when it is done. They run one after another.' }) : null,
					coded.length ? el('div', { className: 'ml-side-sec', textContent: 'In the script', title: 'Stages the script runs in its own code (self.stages). Nodes can check them and jump to them; change what they do in the script.' }) : null,
					...codeRows));
		};
		doc.refreshStages = draw;
		return box;
	}

	// ---- ready-made missions ----

	function templateDialog(doc, ctx) {
		const T = MS.missionTemplates || [];
		const values = {};
		const holder = el('div', {});
		const pickers = MS.scenarios.missionHost;
		const maker = Object.create(MS.FlowView.prototype);
		maker.hooks = { readOnly: false, areas: ctx.areas, pickUnit: (cb) => pickers.pickUnit(cb), pickItem: (cb) => pickers.pickItem(cb) };
		Object.defineProperty(maker, 'g', { get: () => doc.g });
		const pick = el('select', { className: 'ed-input' }, ...T.map((t) => el('option', { value: t.id, textContent: t.name })));
		const draw = () => {
			const t = T.find((x) => x.id === pick.value);
			holder.replaceChildren(el('p', { className: 'ed-note', textContent: t.blurb }), ...t.params.map(([key, type, label, def]) => {
				if (!(key in values)) {
					values[key] = type === 'area' ? ctx.areas()[0] || '' : def;
				}
				return el('div', { className: 'ed-row' }, maker.field(type, label, values[key], (v) => {
					values[key] = v;
					draw();
				}));
			}));
		};
		pick.onchange = () => {
			Object.keys(values).forEach((k) => delete values[k]);
			draw();
		};
		draw();
		MS.dialogs.open('Start from a ready-made mission', el('div', {}, MS.dialogs.parts.field('Mission', pick, 'Its nodes are added to the graph, below what is there; change them like any others.'), holder), [
			{ text: 'Cancel' },
			{ text: 'Add to the mission', primary: true, action: () => {
				const t = T.find((x) => x.id === pick.value);
				if (t.params.some(([key, type]) => (type === 'area' || type === 'item') && !values[key])) {
					app().toast('Pick something for every choice first (the scene needs areas: draw some on the map).', 'bad');
					return false;
				}
				const built = F().migrate(t.build(values));
				change(doc, 'add ' + t.name, (g) => F().merge(g, built));
				if (doc.view) {
					requestAnimationFrame(() => doc.view.fit());
				}
				return true;
			} },
		]);
	}

	// ---- the map's side ----

	const current = () => {
		const tab = MS.ide.active;
		if (!tab || !tab.editor || tab.editor.id !== 'mission') {
			return null;
		}
		const key = tab.editor.module;
		const scenario = app().lib.presets.find((p) => p.topLevel && p.node.key === 'AddActivity' && keyOf(p) === key);
		return scenario ? docs.get(key) && { scenario, doc: docs.get(key) } : null;
	};

	// Where to put a new node: below everything, on the left.
	function freeSpot(g, x) {
		const bottom = g.nodes.reduce((m, n) => Math.max(m, (n.y || 0) + 200), 0);
		return { x: x || 0, y: bottom };
	}

	// What right-clicking an area offers: make it an objective (a new stage, after the last one) or a trigger (an event with a step after it).
	function areaActions(scenario, area) {
		const doc = docs.get(keyOf(scenario));
		if (!doc || !doc.cls || app().lib.isProtected(scenario.doc)) {
			return [];
		}
		const objective = (kind, title, extra) => () => change(doc, title, (g) => {
			let last = F().firstStage(g);
			const seen = new Set();
			while (last && last.out && last.out.next && !seen.has(last.id)) {
				seen.add(last.id);
				last = F().find(g, last.out.next);
			}
			const at = last ? { x: last.x + 360, y: last.y } : { x: 0, y: 0 };
			if (!last && g.nodes.length) {
				// stages go along the top: move the rest down to make room
				for (const n of g.nodes) {
					n.y += 260;
				}
			}
			const node = F().addNode(g, kind, at.x, at.y, Object.assign({ name: title + ' (' + area + ')', area, text: title }, extra || {}));
			const done = F().addNode(g, 'message', at.x + 20, at.y + 300, { text: 'Objective complete.' });
			node.out.done = done.id;
			if (last) {
				last.out.next = node.id;
			}
			doc.view && doc.view.pick([node.id]);
		});
		const trigger = (kind, title) => () => change(doc, title, (g) => {
			const at = freeSpot(g, 0);
			const event = F().addNode(g, kind, at.x, at.y, { area, team: -1 });
			const step = F().addNode(g, 'message', at.x + 360, at.y, { text: title });
			event.out.then = step.id;
			doc.view && doc.view.pick([event.id]);
		});
		return [
			['Objective: reach here', objective('stageReach', 'Reach ' + area)],
			['Objective: hold here', objective('stageHold', 'Hold ' + area, { seconds: 60 })],
			['When a unit enters here…', trigger('enter', 'A unit entered ' + area)],
			['When a unit leaves here…', trigger('leave', 'A unit left ' + area)],
			['When it is emptied…', trigger('empty', area + ' is clear')],
			['-', null],
		];
	}

	// An area picked on the map: the nodes that use it light up.
	function areaPicked(name) {
		const c = current();
		if (!c || !c.doc.view) {
			return;
		}
		for (const [id, e] of c.doc.view.nodeEls) {
			const n = F().find(c.doc.g, id);
			e.classList.toggle('lit', !!n && areasOfNode(n).includes(name));
		}
	}

	// An area renamed on the map: every node that used it uses the new name.
	async function renameArea(scenario, from, to) {
		const doc = docs.get(keyOf(scenario)) || docFor(scenario);
		change(doc, 'rename ' + from, (g) => {
			for (const n of g.nodes) {
				const spec = F().KINDS[n.kind];
				for (const [key, type] of spec ? spec.params : []) {
					if (type === 'area' && Array.isArray(n.p[key])) {
						n.p[key] = n.p[key].map((x) => (x === from ? to : x));
					} else if (type === 'area' && n.p[key] === from) {
						n.p[key] = to;
					}
				}
				for (const c of (n.lists && n.lists.conds) || []) {
					if (c.area === from) {
						c.area = to;
					}
				}
			}
		});
	}

	// How many nodes use an area (for the map's labels).
	function usage(scenario, name) {
		const doc = docs.get(keyOf(scenario));
		return doc ? doc.g.nodes.filter((n) => areasOfNode(n).includes(name)).length : 0;
	}

	// The nodes that use an area: [{ id, title }], for the areas list.
	function areaUses(scenario, name) {
		const doc = docs.get(keyOf(scenario));
		if (!doc) {
			return [];
		}
		return doc.g.nodes.filter((n) => areasOfNode(n).includes(name)).map((n) => {
			const spec = F().KINDS[n.kind];
			const title = spec ? spec.title : n.kind;
			return { id: n.id, title: (spec && spec.category === 'stage' && n.p.name ? n.p.name + ': ' : '') + title + (n.note ? ' (' + n.note + ')' : '') };
		});
	}

	// Picks a node and brings it into the middle of the graph.
	function focusNode(scenario, id) {
		const doc = docs.get(keyOf(scenario));
		if (!doc || !doc.view) {
			return;
		}
		const n = F().find(doc.g, id);
		if (!n) {
			return;
		}
		const view = doc.view;
		view.pick([id]);
		const r = view.root.getBoundingClientRect();
		view.state.view.x = r.width / 2 - (n.x + 140) * view.state.view.z;
		view.state.view.y = r.height / 3 - n.y * view.state.view.z;
		view.place();
	}

	MS.missionLogic = { AREA_ROLES, areaRoles, stageFocus, clearStage, historyStep, render, areaActions, areaPicked, renameArea, usage, docFor, areaUses, focusNode };
})();
