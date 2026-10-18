// Cortex Mod Studio: what the INI text editor knows. Keys a block can have, values a key takes, preset names for CopyOf, file paths,
// and checks, all learned from the INI files Studio has read (every key any block of that class uses, every value any file gives a key).
'use strict';
window.MS = window.MS || {};

(function () {
	// Keys whose value names a file.
	const FILE_KEY = /^(FilePath|ScriptPath|IncludeFile|SpriteFile|IconFile|BitmapFile|PresetIconFile|ScenePreview|PreviewImage|Icon)$/;
	const FILE_VALUE = /\.(png|bmp|wav|ogg|flac|mp3|lua|ini)$/i;

	class IniIntel {
		constructor(app) {
			this.app = app;
			this.index = null;
			this.fileChecks = new Map(); // lowercased path -> true/false (exists), filled in as files are looked up
		}

		// What every file says: keys by the class of their block, values by key, and class names by key. Rebuilt when presets change.
		invalidate() {
			this.index = null;
		}

		build() {
			if (this.index) {
				return this.index;
			}
			const keysByClass = new Map(); // class -> Map(key -> count)
			const valuesByKey = new Map(); // key -> Map(value -> count)
			const topKeys = new Map();
			const classes = new Set();
			const bump = (map, key, sub) => {
				if (!map.has(key)) {
					map.set(key, new Map());
				}
				const inner = map.get(key);
				inner.set(sub, (inner.get(sub) || 0) + 1);
			};
			for (const doc of this.app.lib.docs.values()) {
				for (const node of doc.walk()) {
					if (!node.parent || node.parent === doc.root) {
						topKeys.set(node.key, (topKeys.get(node.key) || 0) + 1);
					} else if (node.parent.value) {
						bump(keysByClass, node.parent.value, node.key);
					}
					if (node.children.length && node.value) {
						classes.add(node.value);
					}
					if (node.value && node.value.length < 80) {
						bump(valuesByKey, node.key, node.value);
					}
				}
			}
			this.index = { keysByClass, valuesByKey, topKeys, classes };
			return this.index;
		}

		// The block a line is in: {cls, key, depth} from the nearest line above that's less indented. lines: the text's lines.
		static blockOf(lines, lineIndex, indent) {
			for (let i = lineIndex - 1; i >= 0; i--) {
				const line = lines[i];
				const content = line.trim();
				if (!content || content.startsWith('//') || content.startsWith('/*') || content.startsWith('*')) {
					continue;
				}
				const lineIndent = indentOf(line);
				if (lineIndent < indent) {
					const m = /^\s*([^=\s/]+)\s*(?:=\s*([^\t/]*))?/.exec(line);
					return m ? { key: m[1], cls: (m[2] || '').trim(), indent: lineIndent, line: i } : null;
				}
			}
			return null;
		}

		// Completion at a position: [{label, kind, detail, doc, insert, sort}]. Values that are file paths ask the game folder (async).
		async complete(path, text, lineNumber, column) {
			const lines = text.split(/\r?\n/);
			const line = lines[lineNumber - 1] || '';
			const before = line.slice(0, column - 1);
			const index = this.build();
			const indent = indentOf(before);
			const valueMatch = /^\s*([^=\s/]+)\s*=\s*(.*)$/.exec(before);
			if (!valueMatch) {
				if (before.includes('//')) {
					return [];
				}
				// A key: those used in blocks of this class, most used first.
				const block = indent > 0 ? IniIntel.blockOf(lines, lineNumber - 1, indent) : null;
				const counts = block ? index.keysByClass.get(block.cls) : index.topKeys;
				const siblings = new Set();
				if (block) {
					for (let i = block.line + 1; i < lines.length; i++) {
						const l = lines[i];
						if (l.trim() && indentOf(l) <= block.indent) {
							break;
						}
						if (indentOf(l) === indent) {
							siblings.add((/^\s*([^=\s/]+)/.exec(l) || [])[1]);
						}
					}
				}
				const items = [];
				for (const [key, count] of counts || []) {
					// a Commander key in someone's mod isn't one to offer for a game that ignores it
					if (block && MS.target.commanderOnly(block.cls, key)) {
						continue;
					}
					const listy = MS.LIST_KEYS.has(key) || /^Add/.test(key);
					items.push({
						label: key, kind: listy ? 'module' : 'property', detail: (block ? block.cls + ' · ' : '') + 'used ' + count + ' time' + (count > 1 ? 's' : '') + (siblings.has(key) && !listy ? ' · already set here' : ''),
						insert: key + ' = ', sort: String(1e6 - count).padStart(7, '0'), retrigger: true,
					});
				}
				return items;
			}
			const key = valueMatch[1];
			const typed = valueMatch[2];
			if (key === 'CopyOf') {
				const block = IniIntel.blockOf(lines, lineNumber - 1, indent);
				const cls = block && block.cls;
				return this.app.lib.presets.filter((p) => !cls || p.cls === cls).map((p) => ({ label: p.name, kind: 'value', detail: p.cls + ' · ' + p.module, doc: p.doc.path + ':' + p.node.line, sort: p.module === (path || '').split('/')[0] ? '0' : this.app.fs.isOfficial(p.module) ? '1' : '2' })).filter(uniqueLabels());
			}
			if (FILE_KEY.test(key) || /\//.test(typed) || FILE_VALUE.test(typed)) {
				return this.completePath(typed, key);
			}
			const values = index.valuesByKey.get(key);
			if (!values) {
				return [];
			}
			const isClass = [...values.keys()].every((v) => index.classes.has(v));
			return [...values].sort((a, b) => b[1] - a[1]).slice(0, 60).map(([value, count], i) => ({ label: value, kind: isClass ? 'class' : 'value', detail: 'used ' + count + ' time' + (count > 1 ? 's' : ''), sort: String(i).padStart(4, '0') }));
		}

		// Files and folders in the folder typed so far ("Base.rte/Actors/" lists that folder; nothing typed lists the modules).
		async completePath(typed, key) {
			const fs = this.app.fs;
			const slash = typed.lastIndexOf('/');
			if (slash < 0) {
				return this.app.lib.modules.map((m) => ({ label: m.name + '/', kind: 'folder', detail: m.title || '', insert: m.name + '/', retrigger: true }));
			}
			const folder = typed.slice(0, slash);
			let entries;
			try {
				entries = await fs.listDir(fs.toDiskPath(folder));
			} catch (e) {
				return [];
			}
			const wantExt = key === 'ScriptPath' ? /\.lua$/i : key === 'IncludeFile' ? /\.ini$/i : /^(FilePath|SpriteFile|IconFile|BitmapFile)$/.test(key) ? /\.(png|bmp|wav|ogg|flac|mp3)$/i : null;
			return entries
				.filter((e) => e.kind === 'directory' || !wantExt || wantExt.test(e.name))
				.map((e) => ({ label: e.name + (e.kind === 'directory' ? '/' : ''), kind: e.kind === 'directory' ? 'folder' : 'file', insert: e.name + (e.kind === 'directory' ? '/' : ''), retrigger: e.kind === 'directory', sort: e.kind === 'directory' ? '0' : '1', replaceTyped: typed.slice(slash + 1) }));
		}

		// Markdown for the word under the mouse: a CopyOf preset's whereabouts, a file, or a key's common values.
		hover(path, text, lineNumber, column) {
			const lines = text.split(/\r?\n/);
			const line = lines[lineNumber - 1] || '';
			const m = /^(\s*)([^=\s/]+)\s*(=\s*)?([^\t/]*)/.exec(line);
			if (!m) {
				return null;
			}
			const keyEnd = m[1].length + m[2].length;
			const key = m[2];
			const value = (m[4] || '').trim();
			if (column <= keyEnd + 1) {
				const values = this.build().valuesByKey.get(key);
				if (!values) {
					return '**' + key + '**';
				}
				const top = [...values].sort((a, b) => b[1] - a[1]).slice(0, 6).map(([v, c]) => '`' + v + '` (' + c + ')');
				return '**' + key + '**\n\nCommon values: ' + top.join(', ');
			}
			if (key === 'CopyOf' || (this.build().classes.has(value) === false && this.app.lib.byName.has(value.toLowerCase()))) {
				const preset = this.app.lib.find(value, null, (path || '').split('/')[0]);
				if (preset) {
					return '**' + preset.name + '** (' + preset.cls + ')\n\n' + preset.doc.path + ':' + preset.node.line + '\n\nCtrl+click or F12 to open it.';
				}
				return key === 'CopyOf' ? '"' + value + '" isn\'t a preset Studio can find.' : null;
			}
			if (FILE_VALUE.test(value)) {
				return value + '\n\nCtrl+click or F12 to open it.';
			}
			return null;
		}

		// Where the value under the cursor points: {path, line} for a CopyOf preset or a file.
		definition(path, text, lineNumber) {
			const line = text.split(/\r?\n/)[lineNumber - 1] || '';
			const m = /^\s*([^=\s/]+)\s*=\s*([^\t/]*)/.exec(line);
			if (!m) {
				return null;
			}
			const value = m[2].trim();
			if (FILE_VALUE.test(value)) {
				return { path: value.replace(/\\/g, '/'), line: 1 };
			}
			const preset = this.app.lib.find(value, null, (path || '').split('/')[0]);
			return preset ? { path: preset.doc.path, line: preset.node.line || 1 } : null;
		}

		// Problems: what the reader found (bad indents, lines without "="), CopyOf presets that don't exist, and missing files.
		// Files are looked up in the background; onMore is called when that changes the answer.
		check(path, text, doc, onMore) {
			const problems = [];
			const parsed = doc || new MS.IniDoc(path, text);
			for (const p of parsed.problems) {
				problems.push({ line: p.line, col: 1, endLine: p.line, endCol: 200, severity: /stops reading/.test(p.text) ? 'error' : 'warning', message: p.text });
			}
			const lib = this.app.lib;
			const moduleName = (path || '').split('/')[0];
			const lookups = [];
			for (const node of parsed.walk()) {
				if (!node.line) {
					continue;
				}
				// Classic mode: what Community Edition's engine doesn't have
				if (node.parent && node.parent !== parsed.root && MS.target.commanderOnly(node.parent.value, node.key)) {
					problems.push({ line: node.line, col: 1, endLine: node.line, endCol: 200, severity: 'warning', message: node.key + ' is a Cortex Commander setting. ' + MS.target.get().name + ' doesn\'t read it, so it does nothing there.' });
				}
				if (node.children.length && MS.target.commanderOnlyClass(node.value)) {
					problems.push({ line: node.line, col: 1, endLine: node.line, endCol: 200, severity: 'error', message: 'Only Cortex Commander has ' + node.value + '. ' + MS.target.get().name + ' can\'t make one, so the mod won\'t load there.' });
				}
				if (node.key === 'CopyOf' && node.value && !lib.find(node.value, node.parent && node.parent.value, moduleName)) {
					problems.push({ line: node.line, col: 1, endLine: node.line, endCol: 200, severity: 'error', message: 'CopyOf "' + node.value + '" isn\'t a preset Studio can find. The game stops loading here.' });
				}
				if ((FILE_KEY.test(node.key) || FILE_VALUE.test(node.value)) && FILE_VALUE.test(node.value)) {
					const file = node.value.replace(/\\/g, '/');
					const key = file.toLowerCase();
					if (this.fileChecks.has(key)) {
						if (!this.fileChecks.get(key)) {
							problems.push({ line: node.line, col: 1, endLine: node.line, endCol: 200, severity: 'warning', message: file + ' isn\'t in the game folder.' });
						}
					} else {
						lookups.push(file);
					}
				}
			}
			if (lookups.length && onMore) {
				Promise.all([...new Set(lookups)].map(async (file) => {
					let exists;
					// A sprite's first frame is "Name000.png" for "Name.png" with several frames.
					try {
						exists = (await this.app.fs.exists(file)) || (/\.(png|bmp)$/i.test(file) && (await this.app.fs.exists(file.replace(/(\.\w+)$/, '000$1'))));
					} catch (e) {
						exists = true;
					}
					this.fileChecks.set(file.toLowerCase(), exists);
				})).then(() => onMore());
			}
			return problems;
		}
	}

	function indentOf(line) {
		let indent = 0;
		let spaces = 0;
		for (const c of line) {
			if (c === '\t') {
				indent++;
				spaces = 0;
			} else if (c === ' ') {
				if (++spaces === 4) {
					indent++;
					spaces = 0;
				}
			} else {
				break;
			}
		}
		return indent;
	}

	function uniqueLabels() {
		const seen = new Set();
		return (item) => (seen.has(item.label) ? false : seen.add(item.label));
	}

	// Monaco's colouring for the game's INI files: keys, values, numbers, comments, file paths and class names.
	IniIntel.monarch = {
		defaultToken: '',
		tokenizer: {
			root: [
				[/\/\*/, 'comment', '@comment'],
				[/\/\/.*$/, 'comment'],
				[/^(\s*)([A-Za-z_][\w]*)(\s*)(=)/, ['white', 'key', 'white', 'delimiter']],
				[/^(\s*)([A-Za-z_][\w]*)\s*$/, ['white', 'key']],
				[/[\w.\- ]+\.rte\/[^\t/\n]*?\.(png|bmp|wav|ogg|flac|mp3|lua|ini)\b/i, 'string.path'],
				[/-?\d+(\.\d+)?\b/, 'number'],
				[/\b(A[A-Z]\w+|HD\w+|TD\w+|MO\w+|Attachable|Arm|Leg|Turret|Magazine|HeldDevice|ThrownDevice|SoundContainer|ContentFile|Vector|Matrix|Material|Emission|Gib|Round|AtomGroup|Atom|LimbPath|PieSlice|PieMenu|Deployment|TerrainObject|Scene|Box|Color)\b/, 'type'],
				[/[^\s/]+/, 'string'],
			],
			comment: [
				[/[^/*]+/, 'comment'],
				[/\*\//, 'comment', '@pop'],
				[/[/*]/, 'comment'],
			],
		},
	};

	IniIntel.indentOf = indentOf;
	MS.IniIntel = IniIntel;
})();
