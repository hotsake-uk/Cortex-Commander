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
			for (const preset of lib.presets) {
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
