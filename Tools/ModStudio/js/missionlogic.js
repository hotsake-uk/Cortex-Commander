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
			const text = F().apply(current, doc.cls, doc.g);
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
		if (doc.hint && doc.g.nodes.length) {
			doc.hint.remove(); // the how-to goes once there is something to show
			doc.hint = null;
		}
		if (doc.view) {
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
			if (type === 'area' && n.p && n.p[key]) {
				names.push(n.p[key]);
			}
		}
		for (const c of (n.lists && n.lists.conds) || []) {
			if (c.area) {
				names.push(c.area);
			}
		}
		return names;
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
		const problems = () => F().problems(doc.g, { areas: ctx.areas() });
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
		host.replaceChildren(...[locked ? el('div', { className: 'ed-banner', textContent: 'This scenario belongs to ' + scenario.module + ': its mission can be looked at, not changed.' }) : null, bar, stages, help, view.root].filter(Boolean));
		doc.hint = help;
		view.render();
		doc.refreshStages();
		if (!doc.state.fitted) {
			doc.state.fitted = true;
			requestAnimationFrame(() => view.fit());
		}
	}

	// ---- stages ----

	// The nodes that belong to a stage: it, what runs from its exits (up to another stage), and the nodes that name it (a jump to it, a check of it).
	function stageNodes(g, stage) {
		const ids = new Set([stage.id]);
		const walk = (id) => {
			const n = F().find(g, id);
			if (!n || ids.has(id) || F().isStage(n.kind)) {
				return;
			}
			ids.add(id);
			Object.values(n.out || {}).forEach(walk);
		};
		Object.entries(stage.out || {}).forEach(([pin, to]) => pin !== 'next' && walk(to));
		for (const n of g.nodes) {
			const spec = F().KINDS[n.kind];
			const named = (spec ? spec.params : []).some(([key, type]) => type === 'stage' && n.p && n.p[key] === stage.id) ||
				((n.lists && n.lists.conds) || []).some((c) => c.stage === stage.id);
			if (named) {
				ids.add(n.id);
				Object.values(n.out || {}).forEach(walk);
			}
		}
		return [...ids];
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
				uses.push({ line: i + 1, text: l.trim() });
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

	// "attackFrontBunker" -> "Attack front bunker"
	const words = (name) => {
		const s = name.replace(/_/g, ' ').replace(/([a-z0-9])([A-Z])/g, '$1 $2').toLowerCase();
		return s.charAt(0).toUpperCase() + s.slice(1);
	};

	// The strip above the graph: the stages in order, from the graph and from the script's own code. Picking one lights its nodes and its areas.
	function stagesPanel(doc, scenario, ctx, path, locked) {
		const box = el('div', { className: 'ml-stages' });
		const draw = () => {
			const g = doc.g;
			const view = doc.view;
			const ordered = stageOrder(g);
			const coded = codeStages(scriptText(path) || '', ctx.areas());
			const sel = doc.stageSel || null;
			const chips = [];
			ordered.forEach((s, i) => {
				const spec = F().KINDS[s.kind];
				chips.push(el('button', { className: 'ml-stage' + (sel && sel.id === s.id ? ' on' : ''), title: spec ? spec.title : s.kind, onclick: () => {
					doc.stageSel = { id: s.id };
					const ids = stageNodes(g, s);
					view.pick(ids);
					for (const [id, e] of view.nodeEls) {
						e.classList.toggle('lit', ids.includes(id));
					}
					const r = view.root.getBoundingClientRect();
					view.state.view.x = r.width / 2 - (s.x + 140) * view.state.view.z;
					view.state.view.y = r.height / 3 - s.y * view.state.view.z;
					view.place();
					draw();
				} }, el('b', { textContent: String(i + 1) }), (s.p && s.p.name) || (spec ? spec.title : s.kind)));
			});
			chips.push(el('button', { className: 'tb ml-add-stage', textContent: '+ Stage', disabled: locked, title: 'Add a stage after the last one', onclick: (e) => {
				view.palette(e.clientX, e.clientY + 20, (k) => k.category === 'stage', (kind) => change(doc, 'add a stage', (gg) => {
					const all = stageOrder(gg);
					// the end of the chain that runs from the start
					let last = F().firstStage(gg);
					for (const seen = new Set(); last && last.out && last.out.next && !seen.has(last.id); last = F().find(gg, last.out.next)) {
						seen.add(last.id);
					}
					const at = last ? { x: last.x + 360, y: last.y } : freeSpot(gg, 0);
					const n = F().addNode(gg, kind, at.x, at.y, { name: 'Stage ' + (all.length + 1) });
					if (last) {
						last.out.next = n.id;
					}
					doc.stageSel = { id: n.id };
				}));
			} }));
			const rows = [el('div', { className: 'ml-stage-row' }, el('span', { className: 'ml-stage-head', textContent: 'Stages' }), ...chips)];
			if (coded.length) {
				rows.push(el('div', { className: 'ml-stage-row' }, el('span', { className: 'ml-stage-head', textContent: 'In code', title: 'Stages the script runs in its own code (self.stages). Change them in the script.' }),
					...coded.map((c, i) => el('button', { className: 'ml-stage code' + (sel && sel.code === c.name ? ' on' : ''), title: c.name + (c.areas.length ? '\nAreas: ' + c.areas.join(', ') : ''), onclick: () => {
						doc.stageSel = { code: c.name };
						ctx.highlight(c.areas);
						if (view) {
							view.pick([]);
						}
						draw();
					} }, el('b', { textContent: String(i + 1) }), words(c.name)))));
			}
			// what the picked stage is made of
			let detail = null;
			if (sel && sel.code) {
				const c = coded.find((x) => x.name === sel.code);
				if (c) {
					detail = el('div', { className: 'ml-stage-detail' },
						el('div', {}, el('b', { textContent: words(c.name) }), ' · ', c.areas.length ? 'areas: ' : 'no scene areas named in its code',
							...c.areas.map((a) => el('button', { className: 'ml-chip', textContent: a, title: 'Show it on the map', onclick: () => ctx.highlight([a]) }))),
						el('div', { className: 'ml-stage-lines' }, ...c.uses.map((u) => el('button', { className: 'ml-line', title: 'Open the script here', onclick: () => MS.ide.openFile(path, u.line) },
							el('span', { className: 'ml-ln', textContent: u.line }), u.text))));
				}
			} else if (sel && sel.id) {
				const s = F().find(g, sel.id);
				if (s) {
					const ids = stageNodes(g, s);
					const areas = new Set();
					ids.forEach((id) => areasOfNode(F().find(g, id)).forEach((a) => areas.add(a)));
					detail = el('div', { className: 'ml-stage-detail' },
						el('div', {}, el('b', { textContent: (s.p && s.p.name) || s.kind }), ' · ', ids.length + ' node' + (ids.length === 1 ? '' : 's'), ' · ', areas.size ? 'areas: ' : 'no areas',
							...[...areas].map((a) => el('button', { className: 'ml-chip', textContent: a, title: 'Show it on the map', onclick: () => ctx.highlight([a]) }))));
				}
			}
			box.replaceChildren(...rows, ...(detail ? [detail] : []));
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
					if (type === 'area' && n.p[key] === from) {
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

	MS.missionLogic = { render, areaActions, areaPicked, renameArea, usage, docFor, areaUses, focusNode };
})();
