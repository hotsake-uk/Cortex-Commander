// Cortex Mod Studio: finding your way around. A shared index of how presets relate (which preset a CopyOf points at, which presets copy
// this one, which blocks name a file) and the outline of an open file: the blocks of an INI file or the functions of a Lua script.
// The editor's Ctrl+Shift+O, its sticky scroll and the Outline panel all read the outline from here.
'use strict';
window.MS = window.MS || {};

(function () {
	const $ = (id) => document.getElementById(id);

	// Monaco's SymbolKind numbers.
	const KIND = { class: 4, method: 5, function: 11, struct: 22, object: 18, namespace: 2 };
	// Blocks that only hold a value (a position, a picture, a material): not worth a line in the outline.
	const VALUE_BLOCKS = new Set(['Vector', 'ContentFile', 'Material', 'AtomGroup', 'Atom', 'Color', 'Matrix']);
	const FILE_VALUE = /\.(png|bmp|wav|ogg|flac|mp3|lua|ini)$/i;
	const FILE_KEY = /^(FilePath|ScriptPath|IncludeFile|SpriteFile|IconFile|BitmapFile|PresetIconFile|ScenePreview|PreviewImage|Icon)$/;

	function lastLine(node) {
		let last = node.line;
		for (const child of node.children) {
			last = Math.max(last, lastLine(child));
		}
		return last;
	}

	class Nav {
		constructor(app) {
			this.app = app;
			this.generation = -1;
			this.baseOf = new Map(); // preset -> the preset its CopyOf names
			this.derivedOf = new Map(); // preset -> [presets that CopyOf it]
			this.fileUsers = new Map(); // lowercased path -> [nodes naming it]
			this.usesOf = new Map(); // a preset's block -> [every CopyOf node that names it, in presets or inside other blocks]
			this.presetOfNode = new Map(); // a preset's block -> the preset
			this.buildMs = 0;
			this.cache = new Map(); // model uri -> {version, symbols}
		}

		// Reads every preset once and keeps the answer until the library changes.
		ensure() {
			const lib = this.app.lib;
			if (!lib) {
				return false;
			}
			if (this.generation === lib.generation) {
				return true;
			}
			const t0 = performance.now();
			this.baseOf.clear();
			this.derivedOf.clear();
			this.fileUsers.clear();
			this.usesOf.clear();
			this.presetOfNode.clear();
			for (const preset of lib.presets) {
				if (!this.presetOfNode.has(preset.node)) {
					this.presetOfNode.set(preset.node, preset);
				}
				const copy = preset.node.child('CopyOf');
				if (!copy) {
					continue;
				}
				const base = lib.find(copy.value, preset.cls, preset.doc.module);
				if (base && base !== preset) {
					this.baseOf.set(preset, base);
					if (!this.derivedOf.has(base)) {
						this.derivedOf.set(base, []);
					}
					this.derivedOf.get(base).push(preset);
				}
			}
			for (const doc of lib.docs.values()) {
				for (const node of doc.walk()) {
					if (node.key === 'CopyOf' && node.value) {
						const target = lib.find(node.value, node.parent && node.parent.value, doc.module);
						if (target) {
							if (!this.usesOf.has(target.node)) {
								this.usesOf.set(target.node, []);
							}
							this.usesOf.get(target.node).push(node);
						}
					}
					if (node.value && FILE_KEY.test(node.key)) {
						const key = node.value.replace(/\\/g, '/').toLowerCase();
						if (!this.fileUsers.has(key)) {
							this.fileUsers.set(key, []);
						}
						this.fileUsers.get(key).push(node);
					}
				}
			}
			this.generation = lib.generation;
			this.buildMs = Math.round(performance.now() - t0);
			return true;
		}

		base(preset) {
			return this.ensure() ? this.baseOf.get(preset) || null : null;
		}

		derived(preset) {
			return this.ensure() ? this.derivedOf.get(preset) || [] : [];
		}

		// The presets above this one: what it copies, what that copies, and so on.
		ancestors(preset) {
			const list = [];
			const seen = new Set([preset]);
			for (let p = this.base(preset); p && !seen.has(p); p = this.base(p)) {
				seen.add(p);
				list.push(p);
			}
			return list;
		}

		presetOf(node) {
			return this.ensure() ? this.presetOfNode.get(node) || null : null;
		}

		uses(preset) {
			return this.ensure() ? this.usesOf.get(preset.node) || [] : [];
		}

		// A CopyOf somewhere, described for a list: the preset it sits in (and the part, when it is a block inside one) and where.
		describeUse(node) {
			let owner = node.parent;
			while (owner && !owner.child('PresetName')) {
				owner = owner.parent;
			}
			const inside = node.parent && owner && node.parent !== owner ? node.parent.key : '';
			const name = owner ? owner.child('PresetName').value : node.doc.path.split('/').pop();
			return { label: name + (inside ? ' › ' + inside : ''), detail: node.doc.path.split('/').pop() + ':' + node.line, path: node.doc.path, line: node.line };
		}

		// Scripts that name a preset in quotes, like CreateActor("Green Dummy"): [{path, line, col, endCol}].
		luaMentions(name, limit) {
			const scripts = MS.ide && MS.ide.lua ? MS.ide.lua.scripts : null;
			const found = [];
			if (!scripts || !name) {
				return found;
			}
			for (const { path, text } of scripts.values()) {
				for (const quote of ['"', "'"]) {
					const needle = quote + name + quote;
					for (let at = text.indexOf(needle); at >= 0; at = text.indexOf(needle, at + needle.length)) {
						const before = text.slice(0, at);
						const line = before.split('\n').length;
						const col = at - before.lastIndexOf('\n');
						found.push({ path, line, col, endCol: col + needle.length });
						if (found.length >= limit) {
							return found;
						}
					}
				}
			}
			return found;
		}

		// Scripts that use a name as a word (a function, a table, a field).
		luaWord(word, limit) {
			const scripts = MS.ide && MS.ide.lua ? MS.ide.lua.scripts : null;
			const found = [];
			if (!scripts || !word) {
				return found;
			}
			const re = new RegExp('(?<![\\w])' + word.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + '(?![\\w])', 'g');
			for (const { path, text } of scripts.values()) {
				if (!text.includes(word)) {
					continue;
				}
				const lines = text.split(/\r?\n/);
				lines.forEach((line, i) => {
					re.lastIndex = 0;
					let m;
					while (found.length < limit && (m = re.exec(line))) {
						found.push({ path, line: i + 1, col: m.index + 1, endCol: m.index + 1 + word.length });
					}
				});
				if (found.length >= limit) {
					break;
				}
			}
			return found;
		}

		// Everything that refers to what the cursor is on: [{path, line, col, endCol}].
		referencesAt(path, model, position, includeDeclaration, limit) {
			const lib = this.app.lib;
			if (!lib || !this.ensure()) {
				return [];
			}
			limit = limit || 300;
			const module = (path || '').split('/')[0];
			const lineText = model.getLineContent(position.lineNumber);
			const presetRefs = (preset) => {
				const out = [];
				if (includeDeclaration && preset.node.line) {
					out.push({ path: preset.doc.path, line: preset.node.line, col: 1, endCol: 200 });
				}
				for (const node of this.uses(preset)) {
					out.push({ path: node.doc.path, line: node.line, col: 1, endCol: 200 });
				}
				return out.concat(this.luaMentions(preset.name, Math.max(0, limit - out.length)));
			};
			if (/\.lua$/i.test(path)) {
				const strings = /(["'])((?:\\.|(?!\1).)*)\1/g;
				let m;
				while ((m = strings.exec(lineText))) {
					if (position.column > m.index && position.column <= m.index + m[0].length) {
						const preset = lib.find(m[2], null, module);
						if (preset) {
							return presetRefs(preset).slice(0, limit);
						}
					}
				}
				const word = model.getWordAtPosition(position);
				return word ? this.luaWord(word.word, limit) : [];
			}
			const m = /^(\s*)([^=\s/]+)\s*=\s*([^\t/]*)/.exec(lineText);
			if (!m) {
				return [];
			}
			const value = m[3].trim();
			if (FILE_VALUE.test(value)) {
				const file = value.replace(/\\/g, '/');
				const users = this.fileUsers.get(file.toLowerCase()) || [];
				const out = users.map((n) => ({ path: n.doc.path, line: n.line, col: 1, endCol: 200 }));
				return out.concat(/\.lua$/i.test(file) ? [] : this.luaMentions(file, limit)).slice(0, limit);
			}
			const preset = lib.find(value, null, module);
			return preset ? presetRefs(preset).slice(0, limit) : [];
		}

		// The preset whose block holds a line of a file (the innermost named block), or null.
		presetAtLine(path, line) {
			const doc = this.app.lib && this.app.lib.docs.get((path || '').toLowerCase());
			if (!doc || !this.ensure()) {
				return null;
			}
			let best = null;
			for (const node of doc.walk()) {
				if (node.line && node.line <= line && lastLine(node) >= line && this.presetOfNode.has(node)) {
					best = node;
				}
			}
			return best ? this.presetOfNode.get(best) : null;
		}

		// The block the game ends up with for a preset, every CopyOf followed: [{text, node, own, label}] per line. A key set again
		// further down replaces the earlier value; list keys keep every entry.
		resolved(preset) {
			const lib = this.app.lib;
			const lines = [];
			const nameOf = (node) => {
				for (let n = node.parent; n; n = n.parent) {
					const name = n.child && n.child('PresetName');
					if (name && name.value) {
						return name.value;
					}
				}
				return node.doc ? node.doc.path.split('/').pop() : '';
			};
			const where = (node) => (node.doc ? nameOf(node) + ' · ' + node.doc.path.split('/').pop() + ':' + node.line : '');
			const emit = (entity, depth) => {
				const last = new Map();
				entity.entries.forEach((e, i) => last.set(e.key, i));
				entity.entries.forEach((entry, i) => {
					const isList = MS.LIST_KEYS.has(entry.key);
					if (!isList && last.get(entry.key) !== i) {
						return; // overridden further down
					}
					const node = entry.node;
					lines.push({ text: '\t'.repeat(depth) + entry.key + (node.value !== '' ? ' = ' + node.value : ' ='), node, own: node.isWithin(preset.node), label: where(node) });
					if (node.children.length) {
						emit(lib.resolve(node), depth + 1);
					}
				});
			};
			lines.push({ text: preset.node.key + ' = ' + preset.node.value, node: preset.node, own: true, label: '' });
			emit(lib.resolve(preset.node), 1);
			return { lines };
		}

		// ---- the relationship graph ----

		// The presets and scripts around a preset, and how they connect: {nodes, edges, truncated, moreCopies}.
		//   copy:  a preset -> the preset it copies (CopyOf)
		//   part:  a preset -> a preset it carries or is built from (an attachment, a held item, a limb), labelled by what it is
		//   runs:  a preset -> a script it runs (ScriptPath)
		//   names: a script -> a preset it names in quotes (CreateActor("...") and the like)
		// depth is how many steps out from the preset to follow.
		graph(preset, options) {
			const o = Object.assign({ depth: 1, copies: true, parts: true, scripts: true, mentions: true }, options);
			const lib = this.app.lib;
			const nodes = new Map();
			const edges = [];
			const edgeSeen = new Set();
			const MAX_NODES = 160;
			const MAX_COPIES = 12;
			const PART_NAMES = { AddAttachable: 'attachment', AddInventory: 'carries', AddDevice: 'holds', FGArm: 'front arm', BGArm: 'back arm', FGLeg: 'front leg', BGLeg: 'back leg', Head: 'head', Jetpack: 'jetpack', Turret: 'turret', Leg: 'leg', AddEmitter: 'emitter', AddAEmitter: 'emitter' };
			const UNITS = new Set(['AHuman', 'ACrab', 'ACDropShip', 'ACRocket', 'Actor', 'ACraft', 'ADoor']);
			const ITEMS = new Set(['HDFirearm', 'HeldDevice', 'TDExplosive', 'ThrownDevice', 'Magazine', 'Round']);
			const ATTACH = new Set(['Attachable', 'AEmitter', 'AEJetpack', 'PEmitter', 'Leg', 'Arm', 'Turret']);
			const groupOf = (cls) => (UNITS.has(cls) ? 'unit' : ITEMS.has(cls) ? 'item' : ATTACH.has(cls) ? 'attachment' : 'other');
			const canon = (p) => this.presetOfNode.get(p.node) || p;
			const truncated = { value: false };
			let moreCopies = 0;

			const presetId = (p) => 'p:' + p.doc.path + ':' + p.node.line;
			const addPreset = (p) => {
				p = canon(p);
				const id = presetId(p);
				if (!nodes.has(id)) {
					if (nodes.size >= MAX_NODES) {
						truncated.value = true;
						return null;
					}
					nodes.set(id, { id, kind: 'preset', group: groupOf(p.cls), label: p.name, sub: p.cls + ' · ' + p.module, path: p.doc.path, line: p.node.line, preset: p, focus: p === canon(preset) });
				}
				return id;
			};
			const addScript = (path) => {
				const clean = path.replace(/\\/g, '/');
				const id = 's:' + clean.toLowerCase();
				if (!nodes.has(id)) {
					if (nodes.size >= MAX_NODES) {
						truncated.value = true;
						return null;
					}
					nodes.set(id, { id, kind: 'script', group: 'script', label: clean.split('/').pop(), sub: clean.split('/')[0], path: clean, line: 1 });
				}
				return id;
			};
			const addEdge = (from, to, kind, label) => {
				if (!from || !to || from === to) {
					return;
				}
				const key = from + '>' + to + '>' + kind + '>' + (label || '');
				if (!edgeSeen.has(key)) {
					edgeSeen.add(key);
					edges.push({ id: 'e' + edges.length, source: from, target: to, kind, label: label || '' });
				}
			};

			const first = canon(preset);
			const rootId = addPreset(first);
			const queue = [{ p: first, level: 0 }];
			const expanded = new Set([rootId]);
			const enqueue = (p, level) => {
				const id = presetId(canon(p));
				if (level <= o.depth && !expanded.has(id)) {
					expanded.add(id);
					queue.push({ p: canon(p), level });
				}
			};

			while (queue.length) {
				const { p, level } = queue.shift();
				const from = addPreset(p);
				if (level >= o.depth) {
					continue; // shown as a node, not followed further
				}
				if (o.copies) {
					const base = this.base(p);
					if (base) {
						addEdge(from, addPreset(base), 'copy', 'copies');
						enqueue(base, level + 1);
					}
					if (level === 0) {
						const derived = this.derived(p);
						for (const d of derived.slice(0, MAX_COPIES)) {
							addEdge(addPreset(d), from, 'copy', 'copies');
							enqueue(d, level + 1);
						}
						moreCopies = Math.max(0, derived.length - MAX_COPIES);
					}
				}
				if (o.parts || o.scripts) {
					for (const entry of lib.resolve(p.node).entries) {
						const n = entry.node;
						if (o.parts && n.children.length) {
							const copy = n.child('CopyOf');
							const target = copy ? lib.find(copy.value, n.value, p.doc.module) : null;
							// Only things you would build with: not wounds, gibs, sounds or effects, which are blocks with a CopyOf too.
							if (target && target.node !== p.node && groupOf(target.cls) !== 'other' && !/Wound|Gib|Sound|Effect|Emission/i.test(entry.key)) {
								addEdge(from, addPreset(target), 'part', PART_NAMES[entry.key] || entry.key);
								enqueue(target, level + 1);
							}
						}
						if (o.scripts && entry.key === 'ScriptPath' && n.value) {
							addEdge(from, addScript(n.value), 'runs', 'runs');
						}
					}
				}
				if (o.mentions) {
					const seenScripts = new Set();
					for (const m of this.luaMentions(p.name, 200)) {
						if (seenScripts.size >= 12) {
							break;
						}
						if (!seenScripts.has(m.path)) {
							seenScripts.add(m.path);
							addEdge(addScript(m.path), from, 'names', 'names');
						}
					}
				}
			}
			return { nodes: [...nodes.values()], edges, truncated: truncated.value, moreCopies };
		}

		// ---- outline of an open file ----

		symbols(path, model) {
			const key = model.uri.toString();
			const version = model.getVersionId();
			const hit = this.cache.get(key);
			if (hit && hit.version === version) {
				return hit.symbols;
			}
			const text = model.getValue();
			const symbols = /\.lua$/i.test(path) ? this.luaSymbols(text, model) : this.iniSymbols(path, text, model);
			this.cache.set(key, { version, symbols });
			return symbols;
		}

		range(model, from, to) {
			const last = model.getLineCount();
			const a = Math.max(1, Math.min(from, last));
			const b = Math.max(a, Math.min(to, last));
			return { startLineNumber: a, startColumn: 1, endLineNumber: b, endColumn: model.getLineMaxColumn(b) };
		}

		// Presets and the blocks inside them, named the way the designer names them.
		iniSymbols(path, text, model) {
			let doc;
			try {
				doc = new MS.IniDoc(path, text);
			} catch (e) {
				return [];
			}
			const build = (node, top) => {
				if (!node.children.length || !node.line || (!top && VALUE_BLOCKS.has(node.value))) {
					return null;
				}
				const name = node.child('PresetName');
				const copy = node.child('CopyOf');
				const label = name && name.value ? name.value : node.key + (node.value ? ' = ' + node.value : '');
				const detail = name && name.value ? node.value + (copy ? ' · copy of ' + copy.value : '') : copy ? 'copy of ' + copy.value : '';
				const children = node.children.map((c) => build(c, false)).filter(Boolean);
				const range = this.range(model, node.line, lastLine(node));
				return {
					name: label,
					detail,
					kind: top ? KIND.class : name || copy ? KIND.object : KIND.struct,
					tags: [],
					range,
					selectionRange: this.range(model, node.line, node.line),
					children,
				};
			};
			return doc.root.children.map((n) => build(n, true)).filter(Boolean);
		}

		luaSymbols(text, model) {
			if (!window.luaparse) {
				return [];
			}
			let ast;
			try {
				ast = window.luaparse.parse(text, { luaVersion: '5.2', locations: true, comments: false });
			} catch (e) {
				return [];
			}
			const nameOf = (expr) => {
				if (!expr) {
					return '';
				}
				if (expr.type === 'Identifier') {
					return expr.name;
				}
				if (expr.type === 'MemberExpression') {
					return nameOf(expr.base) + expr.indexer + nameOf(expr.identifier);
				}
				return '';
			};
			const found = [];
			const visit = (node, owner) => {
				if (!node || typeof node !== 'object') {
					return;
				}
				if (Array.isArray(node)) {
					node.forEach((n) => visit(n, owner));
					return;
				}
				let name = '';
				if (node.type === 'FunctionDeclaration') {
					name = nameOf(node.identifier) || owner || '';
				}
				if ((node.type === 'AssignmentStatement' || node.type === 'LocalStatement') && node.init && node.init.length) {
					const targets = node.type === 'AssignmentStatement' ? node.variables : node.variables;
					node.init.forEach((init, i) => {
						if (init && init.type === 'FunctionDeclaration' && targets[i]) {
							init._name = nameOf(targets[i]);
						}
					});
				}
				if (node.type === 'FunctionDeclaration') {
					name = name || node._name || '';
					found.push({
						name: name || 'function',
						detail: '(' + (node.parameters || []).map((p) => (p.type === 'VarargLiteral' ? '...' : p.name)).join(', ') + ')',
						kind: name.includes(':') ? KIND.method : KIND.function,
						tags: [],
						range: this.range(model, node.loc.start.line, node.loc.end.line),
						selectionRange: this.range(model, node.loc.start.line, node.loc.start.line),
						children: [],
						start: node.loc.start.line,
						end: node.loc.end.line,
					});
				}
				for (const key of Object.keys(node)) {
					if (key !== 'loc' && key !== 'range' && key !== '_name' && node[key] && typeof node[key] === 'object') {
						visit(node[key], name);
					}
				}
			};
			visit(ast);
			// Functions inside functions nest under the one that contains them.
			found.sort((a, b) => a.start - b.start || b.end - a.end);
			const roots = [];
			const stack = [];
			for (const f of found) {
				while (stack.length && stack[stack.length - 1].end < f.start) {
					stack.pop();
				}
				(stack.length ? stack[stack.length - 1].children : roots).push(f);
				stack.push(f);
			}
			return roots;
		}

		// ---- the Outline panel ----

		attach(editor) {
			this.editor = editor;
			let timer = 0;
			const later = () => {
				clearTimeout(timer);
				timer = setTimeout(() => this.renderOutline(), 250);
			};
			editor.onDidChangeModel(later);
			editor.onDidChangeModelContent(later);
			editor.onDidChangeCursorPosition(() => this.markCurrent());
		}

		renderOutline() {
			const box = $('outlineTree');
			if (!box || $('view-outline').hidden) {
				return;
			}
			const ide = MS.ide;
			const tab = ide.active;
			box.innerHTML = '';
			const model = this.editor && this.editor.getModel();
			if (!tab || tab.kind !== 'code' || !model) {
				box.append(Object.assign(document.createElement('div'), { className: 'side-note', textContent: 'Open an INI or Lua file to see its blocks and functions here.' }));
				$('outlineCount').textContent = '';
				return;
			}
			const symbols = this.symbols(tab.path, model);
			let count = 0;
			const add = (list, depth, parent) => {
				for (const s of list) {
					count++;
					const row = document.createElement('div');
					row.className = 'ol-row';
					row.style.paddingLeft = 8 + depth * 14 + 'px';
					row._symbol = s;
					const glyph = document.createElement('span');
					glyph.className = 'ol-glyph';
					glyph.textContent = s.kind === KIND.class ? '◆' : s.kind === KIND.function || s.kind === KIND.method ? 'ƒ' : '▫';
					const name = document.createElement('span');
					name.className = 'ol-name';
					name.textContent = s.name;
					const detail = document.createElement('small');
					detail.textContent = s.detail || '';
					row.append(glyph, name, detail);
					row.title = s.name + (s.detail ? ' ' + s.detail : '') + ' · line ' + s.range.startLineNumber;
					row.onclick = () => {
						this.editor.setPosition({ lineNumber: s.range.startLineNumber, column: 1 });
						this.editor.revealLineInCenter(s.range.startLineNumber);
						this.editor.focus();
					};
					parent.append(row);
					add(s.children, depth + 1, parent);
				}
			};
			add(symbols, 0, box);
			$('outlineCount').textContent = count ? count + '' : '';
			if (!count) {
				box.append(Object.assign(document.createElement('div'), { className: 'side-note', textContent: 'Nothing to list in this file.' }));
			}
			this.markCurrent();
		}

		// The innermost block or function the cursor is in.
		markCurrent() {
			const box = $('outlineTree');
			if (!box || $('view-outline').hidden || !this.editor) {
				return;
			}
			const line = this.editor.getPosition().lineNumber;
			let best = null;
			for (const row of box.querySelectorAll('.ol-row')) {
				const r = row._symbol.range;
				row.classList.remove('cur');
				if (line >= r.startLineNumber && line <= r.endLineNumber) {
					best = row;
				}
			}
			if (best) {
				best.classList.add('cur');
				best.scrollIntoView({ block: 'nearest' });
			}
		}
	}

	MS.Nav = Nav;
	MS.KIND = KIND;
})();
