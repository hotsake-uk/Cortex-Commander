// Cortex Mod Studio: Deep mode, the modding IDE. Artist mode is the unit designer on its own; Deep mode puts it in a workspace with the
// mod's files, a code editor (Monaco, the editor VS Code is built on, shipped in vendor/) for Lua scripts and INI files with completion and
// checks from lua-intel.js and ini-intel.js, the game's console, watched values, search and packaging. The designer and the INI editor
// edit the same files, so a change in one shows in the other.
//
// The editor is only loaded when Deep mode is first opened, so Artist mode starts as quickly as before.
'use strict';
window.MS = window.MS || {};

(function () {
	const $ = (id) => document.getElementById(id);
	const el = (tag, props, ...children) => {
		const e = document.createElement(tag);
		Object.assign(e, props || {});
		for (const c of children) {
			if (c !== null && c !== undefined && c !== false) {
				e.append(c);
			}
		}
		return e;
	};
	const baseName = (p) => p.split('/').pop();
	const extOf = (p) => (/\.([^./]+)$/.exec(p) || [])[1] ? /\.([^./]+)$/.exec(p)[1].toLowerCase() : '';
	const CONSOLE_FILE = 'ModStudio/Console.log';
	const WATCH_FILE = 'ModStudio/Watch.ini';
	const SCRIPTS_FILE = 'ModStudio/Scripts.ini';
	const RUN_FILE = 'ModStudio/Run.lua';
	const TEXT_EXT = new Set(['lua', 'ini', 'txt', 'cfg', 'md', 'json', 'xml', 'csv']);
	const IMAGE_EXT = new Set(['png', 'bmp', 'jpg', 'jpeg', 'gif']);

	let app = null;

	const ide = {
		mode: 'artist',
		monaco: null,
		editor: null,
		lua: null,
		ini: null,
		tabs: [], // {id, kind: "designer" | "code" | "image", path, title, model, doc, savedText, readOnly, viewState}
		active: null,
		scriptsLoaded: false,
	};
	MS.ide = ide;

	// ---- start-up and the mode switch ----

	ide.init = function (theApp) {
		app = theApp;
		ide.lua = new MS.LuaIntel(app);
		ide.ini = new MS.IniIntel(app);
		MS.nav = new MS.Nav(app);
		pollBridge();
		for (const b of document.querySelectorAll('#modeSwitch button')) {
			b.onclick = () => ide.setMode(b.dataset.mode);
		}
		for (const b of document.querySelectorAll('#activity button[data-view]')) {
			b.onclick = () => ide.showView(b.dataset.view === ide.view && !$('app').classList.contains('no-lib') ? null : b.dataset.view);
		}
		for (const b of document.querySelectorAll('#bottomTabs button[data-panel]')) {
			b.onclick = () => ide.showPanel(b.dataset.panel);
		}
		$('btnPanelToggle').onclick = () => ide.togglePanel();
		$('btnConsoleClear').onclick = () => clearConsole();
		$('btnReloadScripts').onclick = () => ide.requestScripts({ reload: true }, true);
		// The status bar keeps these when the panel is hidden.
		$('btnStatusReload').onclick = () => ide.requestScripts({ reload: true }, true);
		$('btnStatusPanel').onclick = () => ide.showPanel(ide.panel || 'console');
		const newIn = (kind) => {
			const mod = app.actor && !app.fs.isOfficial(app.actor.path) ? app.actor.path.split('/')[0] : (app.lib && (app.lib.modules.find((m) => !m.official) || {}).name);
			if (!mod) {
				app.toast('Make a mod first: Make my own copy of a unit starts one.', 'bad');
				return;
			}
			newFileDialog(kind === 'lua' ? mod + '/Scripts' : mod, kind);
		};
		$('btnNewScript').onclick = () => newIn('lua');
		$('btnNewIni').onclick = () => newIn('ini');
		$('btnRefreshFiles').onclick = () => refreshExplorer();
		fileKind = app.readLocal('ms.fileKind') || '';
		for (const b of document.querySelectorAll('#fileKinds button')) {
			b.classList.toggle('sel', b.dataset.kind === fileKind);
			b.onclick = () => {
				fileKind = b.dataset.kind;
				app.writeLocal('ms.fileKind', fileKind);
				for (const x of document.querySelectorAll('#fileKinds button')) {
					x.classList.toggle('sel', x === b);
				}
				renderExplorer();
			};
		}
		let filterTimer = 0;
		$('fileFilter').oninput = () => {
			clearTimeout(filterTimer);
			filterTimer = setTimeout(renderExplorer, 200);
		};
		bindRunBox();
		bindPanelResize();
		bindSearch();
		bindPackage();
		window.addEventListener('keydown', onKey, true);

		// Edits made by the designer (or undo) show in open INI files.
		const rebuild = app.rebuild;
		app.rebuild = function (...args) {
			const result = rebuild.apply(this, args);
			usersFor = null;
			ide.syncModels();
			if (ide.active && ide.mode === 'deep') {
				renderUsers(ide.active);
			}
			if (ide.ini) {
				ide.ini.invalidate();
			}
			if (ide.mode === 'deep') {
				renderTabs();
			}
			return result;
		};
		const openActor = app.openActor;
		app.openActor = function (...args) {
			const result = openActor.apply(this, args);
			if (ide.mode === 'deep') {
				ide.activate(designerTab());
			}
			return result;
		};

		const saved = app.readLocal('ms.mode');
		ide.setMode(saved === 'deep' ? 'deep' : 'artist', true);
	};

	// Called once the game folder is read.
	ide.gameLoaded = function () {
		if (ide.mode === 'deep') {
			startDeep();
		}
		renderExplorer();
	};

	ide.setMode = function (mode, quiet) {
		ide.mode = mode;
		$('app').classList.toggle('deep', mode === 'deep');
		for (const b of document.querySelectorAll('#modeSwitch button')) {
			b.classList.toggle('sel', b.dataset.mode === mode);
		}
		app.writeLocal('ms.mode', mode);
		if (mode === 'deep') {
			if (!ide.view) {
				ide.showView(app.readLocal('ms.deepView') || 'files');
			}
			if (app.lib) {
				startDeep();
			}
			renderTabs();
			if (!ide.active) {
				ide.activate(designerTab());
			} else {
				ide.activate(ide.active);
			}
			if (!quiet) {
				app.status('Script mode: the mod\'s files, scripts with autocomplete, the game\'s console. Design mode goes back to just the designer.');
			}
		} else {
			$('app').classList.remove('code-active');
			if (!quiet) {
				app.status('Design mode: design units visually. Script mode has scripts and the mod\'s files.');
			}
		}
		MS.panels.refresh && app.lib && MS.panels.refresh();
		app.requestDraw();
		if (ide.editor) {
			setTimeout(() => ide.editor.layout(), 0);
		}
	};

	let deepStarted = false;
	async function startDeep() {
		if (deepStarted) {
			return;
		}
		deepStarted = true;
		renderExplorer();
		startConsole();
		try {
			await Promise.all([loadMonaco(), ide.lua.load()]);
		} catch (e) {
			app.toast('The code editor couldn\'t load: ' + (e.message || e), 'bad');
			return;
		}
		loadScripts();
		if (ide.pendingOpen) {
			const { path, line } = ide.pendingOpen;
			ide.pendingOpen = null;
			ide.openFile(path, line);
		}
	}

	async function loadScripts() {
		try {
			const bundle = await app.fs.luaBundle();
			ide.lua.setScripts(bundle);
			ide.scriptsLoaded = true;
			for (const tab of ide.tabs) {
				if (tab.kind === 'code' && tab.lang === 'lua') {
					checkSoon(tab);
				}
			}
		} catch (e) {
			console.warn('Couldn\'t read the scripts for autocomplete: ' + e.message);
		}
	}

	// Reads every Lua script once, for anything that looks inside scripts (the graph, Used by) before Script mode has been opened.
	let scriptsPromise = null;
	ide.ensureScripts = function () {
		if (ide.scriptsLoaded) {
			return Promise.resolve();
		}
		if (!scriptsPromise) {
			scriptsPromise = loadScripts();
		}
		return scriptsPromise;
	};

	// ---- the editor (Monaco) ----

	let monacoLoading = null;
	function loadMonaco() {
		if (monacoLoading) {
			return monacoLoading;
		}
		monacoLoading = new Promise((resolve, reject) => {
			window.MonacoEnvironment = {
				getWorkerUrl: () => 'data:text/javascript;charset=utf-8,' + encodeURIComponent('self.MonacoEnvironment={baseUrl:"' + location.origin + '/vendor/monaco/"};importScripts("' + location.origin + '/vendor/monaco/vs/base/worker/workerMain.js");'),
			};
			const css = el('link', { rel: 'stylesheet', href: 'vendor/monaco/vs/editor/editor.main.css' });
			css.setAttribute('data-name', 'vs/editor/editor.main');
			document.head.append(css);
			const loader = el('script', { src: 'vendor/monaco/vs/loader.js' });
			loader.onerror = () => reject(new Error('vendor/monaco/vs/loader.js is missing'));
			loader.onload = () => {
				window.require.config({ paths: { vs: 'vendor/monaco/vs' } });
				window.require(['vs/editor/editor.main'], () => {
					ide.monaco = window.monaco;
					setUpMonaco();
					resolve();
				}, reject);
			};
			document.head.append(loader);
		});
		return monacoLoading;
	}

	ide.loadMonaco = loadMonaco;

	const KIND = () => {
		const k = ide.monaco.languages.CompletionItemKind;
		return { method: k.Method, function: k.Function, property: k.Property, field: k.Field, constant: k.Constant, class: k.Class, module: k.Module, variable: k.Variable, keyword: k.Keyword, snippet: k.Snippet, value: k.Value, file: k.File, folder: k.Folder };
	};

	function setUpMonaco() {
		const monaco = ide.monaco;
		monaco.editor.defineTheme('cortex', {
			base: 'vs-dark',
			inherit: true,
			rules: [
				{ token: 'key', foreground: '7fd1ff' },
				{ token: 'type', foreground: 'f2b63d' },
				{ token: 'string.path', foreground: '9fd88a' },
				{ token: 'comment', foreground: '6a7486', fontStyle: 'italic' },
			],
			colors: {
				'editor.background': '#101216',
				'editor.lineHighlightBackground': '#171b22',
				'editorLineNumber.foreground': '#4a5262',
				'editorLineNumber.activeForeground': '#8d96a8',
				'editorCursor.foreground': '#f2b63d',
				'editor.selectionBackground': '#2c3a52',
				'editorWidget.background': '#1b1f27',
				'editorSuggestWidget.background': '#1b1f27',
				'editorSuggestWidget.selectedBackground': '#2a3142',
				'editorHoverWidget.background': '#1b1f27',
			},
		});
		monaco.languages.register({ id: 'ccini', extensions: ['.ini'] });
		monaco.languages.setMonarchTokensProvider('ccini', MS.IniIntel.monarch);
		monaco.languages.setLanguageConfiguration('ccini', { comments: { lineComment: '//', blockComment: ['/*', '*/'] } });

		const toRange = (model, position, length) => new monaco.Range(position.lineNumber, position.column - length, position.lineNumber, position.column);
		const pathOf = (model) => (model.ccTab ? model.ccTab.path : decodeURIComponent(model.uri.path.replace(/^\//, '')));

		monaco.languages.registerCompletionItemProvider('lua', {
			triggerCharacters: ['.', ':', '"', '\'', '/'],
			provideCompletionItems(model, position) {
				const text = model.getValue();
				const offset = model.getOffsetAt(position);
				const items = ide.lua.complete(pathOf(model), text, offset);
				const lineBefore = model.getLineContent(position.lineNumber).slice(0, position.column - 1);
				const str = /(["'])([^"']*)$/.exec(lineBefore);
				const inString = str && (lineBefore.slice(0, str.index).split(str[1]).length % 2 === 1);
				const word = model.getWordUntilPosition(position);
				const range = inString ? toRange(model, position, str[2].length) : new monaco.Range(position.lineNumber, word.startColumn, position.lineNumber, word.endColumn);
				return { suggestions: items.map((i) => toSuggestion(i, range)) };
			},
		});
		monaco.languages.registerSignatureHelpProvider('lua', {
			signatureHelpTriggerCharacters: ['(', ','],
			signatureHelpRetriggerCharacters: [','],
			provideSignatureHelp(model, position) {
				const help = ide.lua.signatureAt(pathOf(model), model.getValue(), model.getOffsetAt(position));
				if (!help) {
					return null;
				}
				return {
					value: {
						signatures: help.signatures.map((s) => ({ label: s.label, documentation: { value: s.doc }, parameters: s.params.map((p) => ({ label: p.label, documentation: p.doc })) })),
						activeSignature: help.active,
						activeParameter: help.activeParam,
					},
					dispose() {},
				};
			},
		});
		monaco.languages.registerHoverProvider('lua', {
			provideHover(model, position) {
				const word = model.getWordAtPosition(position);
				if (!word) {
					return null;
				}
				const start = model.getOffsetAt({ lineNumber: position.lineNumber, column: word.startColumn });
				const end = model.getOffsetAt({ lineNumber: position.lineNumber, column: word.endColumn });
				const md = ide.lua.hover(pathOf(model), model.getValue(), start, end);
				return md ? { range: new monaco.Range(position.lineNumber, word.startColumn, position.lineNumber, word.endColumn), contents: [{ value: md }] } : null;
			},
		});
		monaco.languages.registerDefinitionProvider('lua', {
			async provideDefinition(model, position) {
				const word = model.getWordAtPosition(position);
				const start = model.getOffsetAt({ lineNumber: position.lineNumber, column: word ? word.startColumn : position.column });
				const end = model.getOffsetAt({ lineNumber: position.lineNumber, column: word ? word.endColumn : position.column });
				const target = ide.lua.definition(pathOf(model), model.getValue(), start, end);
				return target ? await toLocation(model, target) : null;
			},
		});

		monaco.languages.registerCompletionItemProvider('ccini', {
			triggerCharacters: ['=', ' ', '/'],
			async provideCompletionItems(model, position) {
				const lineBefore = model.getLineContent(position.lineNumber).slice(0, position.column - 1);
				if (/=\s*$/.test(lineBefore) === false && / $/.test(lineBefore) && !/=/.test(lineBefore)) {
					return { suggestions: [] };
				}
				const items = await ide.ini.complete(pathOf(model), model.getValue(), position.lineNumber, position.column);
				const value = /=\s*(.*)$/.exec(lineBefore);
				const word = model.getWordUntilPosition(position);
				return {
					suggestions: items.map((i) => {
						const length = i.replaceTyped !== undefined ? i.replaceTyped.length : value ? value[1].length : position.column - word.startColumn;
						return toSuggestion(i, toRange(model, position, length));
					}),
				};
			},
		});
		monaco.languages.registerHoverProvider('ccini', {
			provideHover(model, position) {
				const md = ide.ini.hover(pathOf(model), model.getValue(), position.lineNumber, position.column);
				return md ? { contents: [{ value: md }] } : null;
			},
		});
		monaco.languages.registerDefinitionProvider('ccini', {
			async provideDefinition(model, position) {
				const target = ide.ini.definition(pathOf(model), model.getValue(), position.lineNumber);
				return target ? await toLocation(model, target) : null;
			},
		});

		// The outline: Ctrl+Shift+O lists a file's blocks or functions, and sticky scroll names the block you are in by it.
		for (const language of ['ccini', 'lua']) {
			monaco.languages.registerDocumentSymbolProvider(language, {
				displayName: language === 'lua' ? 'Lua functions' : 'INI blocks',
				provideDocumentSymbols: (model) => MS.nav.symbols(pathOf(model), model),
			});
		}

		// Find all references (Shift+F12): what copies a preset, which scripts name it, which blocks use a file, where a Lua name is used.
		for (const language of ['ccini', 'lua']) {
			monaco.languages.registerReferenceProvider(language, {
				async provideReferences(model, position, context) {
					const refs = MS.nav.referencesAt(pathOf(model), model, position, context.includeDeclaration, 300);
					const out = [];
					for (const r of refs) {
						const target = await ide.ensureModel(r.path);
						if (target) {
							out.push({ uri: target.uri, range: new monaco.Range(r.line || 1, r.col || 1, r.line || 1, r.endCol || 200) });
						}
					}
					return out;
				},
			});
		}

		// Script paths in INI and Lua files are links: Ctrl+click opens the script.
		const scriptLinks = {
			provideLinks(model) {
				const links = [];
				const re = /[\w .'-]+\.rte[\\/][^\s"'=\t]*?\.lua\b/gi;
				for (let n = 1; n <= model.getLineCount(); n++) {
					const line = model.getLineContent(n);
					for (const m of line.matchAll(re)) {
						const start = m.index + (m[0].length - m[0].trimStart().length);
						const path = m[0].trim().replace(/\\/g, '/');
						links.push({ range: new monaco.Range(n, start + 1, n, m.index + m[0].length + 1), url: 'ccstudio://open/' + encodeURIComponent(path), tooltip: 'Open ' + path });
					}
				}
				return { links };
			},
		};
		monaco.languages.registerLinkProvider('ccini', scriptLinks);
		monaco.languages.registerLinkProvider('lua', scriptLinks);
		if (monaco.editor.registerLinkOpener) {
			monaco.editor.registerLinkOpener({
				open(uri) {
					if (uri.scheme !== 'ccstudio') {
						return false;
					}
					ide.openFile(uri.path.replace(/^\//, ''));
					return true;
				},
			});
		}

		// Going to a definition in another file opens it in a tab.
		if (monaco.editor.registerEditorOpener) {
			monaco.editor.registerEditorOpener({
				openCodeEditor(source, resource, selectionOrPosition) {
					const tab = ide.tabs.find((t) => t.model && t.model.uri.toString() === resource.toString());
					const line = selectionOrPosition ? selectionOrPosition.startLineNumber || selectionOrPosition.lineNumber : 1;
					if (tab) {
						ide.activate(tab);
						reveal(line);
					} else {
						ide.openFile(decodeURIComponent(resource.path.replace(/^\//, '')), line);
					}
					return true;
				},
			});
		}

		monaco.editor.onDidChangeMarkers(() => renderProblems());

		ide.editor = monaco.editor.create($('codeHost'), {
			theme: 'cortex',
			automaticLayout: true,
			fontSize: 13,
			fontFamily: 'ui-monospace, Consolas, "Cascadia Mono", monospace',
			minimap: { enabled: true, scale: 1, renderCharacters: false },
			tabSize: 4,
			insertSpaces: false,
			detectIndentation: true,
			renderWhitespace: 'selection',
			scrollBeyondLastLine: false,
			smoothScrolling: true,
			fixedOverflowWidgets: true,
			suggest: { showStatusBar: true, preview: true, showInlineDetails: false },
			suggestFontSize: 13,
			suggestLineHeight: 22,
			quickSuggestions: { other: true, comments: false, strings: true },
			glyphMargin: true, // Live values' logpoints (live.js): click left of a line number to log that line.
			folding: true,
			foldingStrategy: 'indentation', // INI blocks nest by their tabs, as do Lua's
			showFoldingControls: 'always',
			foldingHighlight: true,
			stickyScroll: { enabled: true, maxLineCount: 5 }, // the blocks the cursor is inside stay pinned at the top
			model: null,
		});
		MS.live.attach(ide.editor);
		MS.nav.attach(ide.editor);
		// Inheritance and Resolved for the preset the cursor is in.
		for (const b of document.querySelectorAll('#codeTools [data-rel]')) {
			b.onclick = () => {
				const tab = ide.active;
				const pos = ide.editor.getPosition();
				const preset = tab && tab.path && pos ? MS.nav.presetAtLine(tab.path, pos.lineNumber) : null;
				if (!preset) {
					app.toast('Put the cursor inside a preset first.', 'bad');
					return;
				}
				MS.relations.open(preset, b.dataset.rel);
			};
		}
		// Fold buttons: levels follow the file's indentation (in an INI file level 1 is each preset, level 2 the blocks inside it).
		const foldActions = { 1: 'editor.foldLevel1', 2: 'editor.foldLevel2', except: 'editor.foldAllExcept', unfold: 'editor.unfoldAll' };
		for (const b of document.querySelectorAll('#codeTools [data-fold]')) {
			b.onclick = () => {
				ide.editor.focus();
				ide.editor.trigger('codeTools', foldActions[b.dataset.fold], null);
			};
		}
		// The description beside the suggestion list shows from the start (Ctrl+Space still toggles it), and the list is wider and taller.
		try {
			const suggest = ide.editor.getContribution('editor.contrib.suggestController');
			const widget = suggest && suggest.widget && suggest.widget.value;
			if (widget && widget._setDetailsVisible) {
				widget._setDetailsVisible(true);
			}
			// About 14 rows and a wide list (dragging its corner still resizes it).
			if (widget && widget._persistedSize && !widget._persistedSize.restore()) {
				widget._persistedSize.store({ width: 520, height: 14 * 22 });
			}
		} catch (e) {
			// Monaco's insides moved: the description stays a Ctrl+Space away.
		}
		ide.editor.addAction({
			id: 'cortex.save', label: 'Save', keybindings: [monaco.KeyMod.CtrlCmd | monaco.KeyCode.KeyS],
			run: () => ide.saveAll(),
		});
		ide.editor.addAction({
			id: 'cortex.quickOpen', label: 'Go to file…', keybindings: [monaco.KeyMod.CtrlCmd | monaco.KeyCode.KeyP],
			run: () => quickOpen(),
		});
		ide.editor.addAction({
			id: 'cortex.runSelection', label: 'Run selection in the game', contextMenuGroupId: 'navigation', keybindings: [monaco.KeyMod.CtrlCmd | monaco.KeyCode.Enter],
			run: (ed) => {
				const selection = ed.getModel().getValueInRange(ed.getSelection()) || ed.getModel().getLineContent(ed.getPosition().lineNumber);
				runLua(selection.trim());
			},
		});
		ide.editor.onDidChangeCursorPosition((e) => {
			$('statusCursor').textContent = 'Ln ' + e.position.lineNumber + ', Col ' + e.position.column;
		});
	}

	function toSuggestion(item, range) {
		const monaco = ide.monaco;
		return {
			label: item.label,
			kind: KIND()[item.kind] || KIND().value,
			detail: item.detail || '',
			documentation: item.doc ? { value: item.doc } : undefined,
			insertText: item.insert || item.label,
			insertTextRules: item.snippet ? monaco.languages.CompletionItemInsertTextRule.InsertAsSnippet : undefined,
			sortText: (item.sort || '5') + item.label,
			range,
			command: item.retrigger ? { id: 'editor.action.triggerSuggest', title: '' } : item.snippet && /\(\$0\)$/.test(item.insert || '') ? { id: 'editor.action.triggerParameterHints', title: '' } : undefined,
		};
	}

	// A Monaco location for a definition target: in this model ({offset}) or another file's (made into a model so Monaco can go there).
	async function toLocation(model, target) {
		const monaco = ide.monaco;
		if (target.offset !== undefined) {
			const pos = model.getPositionAt(target.offset);
			return { uri: model.uri, range: new monaco.Range(pos.lineNumber, pos.column, pos.lineNumber, pos.column) };
		}
		const other = await ide.ensureModel(target.path);
		if (!other) {
			return null;
		}
		return { uri: other.uri, range: new monaco.Range(target.line || 1, 1, target.line || 1, 1) };
	}

	// ---- tabs ----

	let nextTabId = 1;

	function designerTab() {
		let tab = ide.tabs.find((t) => t.kind === 'designer');
		if (!tab) {
			tab = { id: nextTabId++, kind: 'designer' };
			ide.tabs.unshift(tab);
		}
		return tab;
	}

	function tabTitle(tab) {
		if (tab.kind === 'designer') {
			return app.actor ? app.actor.name : 'Designer';
		}
		return baseName(tab.path);
	}

	function tabDirty(tab) {
		if (tab.kind !== 'code') {
			return false;
		}
		if (tab.doc) {
			return tab.doc.dirty;
		}
		return !!tab.model && tab.model.getValue() !== tab.savedText;
	}

	ide.dirtyCount = function () {
		return ide.tabs.filter((t) => t.kind === 'code' && !t.doc && tabDirty(t)).length;
	};

	// Paths of open scripts (and other plain files) with unsaved changes; INI files Studio has read are documents, counted by the app.
	ide.dirtyScripts = function () {
		return ide.tabs.filter((t) => t.kind === 'code' && !t.doc && tabDirty(t)).map((t) => t.path);
	};

	// Discard: open scripts go back to what's saved.
	ide.discardScripts = function () {
		for (const tab of ide.tabs) {
			if (tab.kind === 'code' && !tab.doc && tabDirty(tab)) {
				tab.model.pushEditOperations([], [{ range: tab.model.getFullModelRange(), text: tab.savedText }], () => null);
			}
		}
		renderTabs();
	};

	function renderTabs() {
		const bar = $('tabbar');
		bar.innerHTML = '';
		for (const tab of ide.tabs) {
			const dirty = tabDirty(tab);
			const icon = el('span', { className: 'tab-icon ' + (tab.kind === 'designer' ? 'designer' : extOf(tab.path)), textContent: tab.kind === 'designer' ? '◆' : extOf(tab.path) });
			const item = el('div', { className: 'tab' + (tab === ide.active ? ' sel' : '') + (dirty ? ' dirty' : '') + (tab.readOnly ? ' ro' : ''), title: tab.path || 'The unit designer' }, icon, el('span', { className: 'tab-name', textContent: tabTitle(tab) }));
			if (tab.kind !== 'designer') {
				const close = el('button', { className: 'tab-close', textContent: dirty ? '●' : '×', title: 'Close (Ctrl+W)' });
				close.onclick = (e) => {
					e.stopPropagation();
					ide.closeTab(tab);
				};
				item.append(close);
			}
			item.onclick = () => ide.activate(tab);
			item.onauxclick = (e) => {
				if (e.button === 1 && tab.kind !== 'designer') {
					ide.closeTab(tab);
				}
			};
			bar.append(item);
		}
	}

	// After unlocking or locking (the Game screen): open code tabs of the game's own files follow.
	ide.refreshReadOnly = function () {
		for (const tab of ide.tabs) {
			if (tab.kind === 'code') {
				tab.readOnly = app.lockedGameFile(tab.path) || !app.fs.writable;
			}
		}
		renderTabs();
		if (ide.active && ide.active.kind === 'code') {
			$('codeBanner').hidden = !ide.active.readOnly;
			if (ide.editor) {
				ide.editor.updateOptions({ readOnly: !!ide.active.readOnly });
			}
		}
	};

	ide.activate = function (tab) {
		if (ide.active && ide.active.kind === 'code' && ide.editor && ide.editor.getModel() === ide.active.model) {
			ide.active.viewState = ide.editor.saveViewState();
		}
		ide.active = tab;
		const code = tab.kind !== 'designer';
		$('app').classList.toggle('code-active', code && ide.mode === 'deep');
		$('imageHost').hidden = tab.kind !== 'image';
		$('codeHost').hidden = tab.kind !== 'code';
		$('codeBanner').hidden = !(tab.kind === 'code' && tab.readOnly);
		$('codeTools').hidden = tab.kind !== 'code';
		renderUsers(tab);
		if (tab.kind === 'code' && ide.editor) {
			ide.editor.setModel(tab.model);
			ide.editor.updateOptions({ readOnly: !!tab.readOnly });
			if (tab.viewState) {
				ide.editor.restoreViewState(tab.viewState);
			}
			setTimeout(() => {
				ide.editor.layout();
				ide.editor.focus();
			}, 0);
			$('statusLang').textContent = tab.lang === 'lua' ? 'Lua' : tab.lang === 'ccini' ? 'INI' : 'Text';
		} else {
			$('statusLang').textContent = '';
			$('statusCursor').textContent = '';
		}
		if (tab.kind === 'image') {
			showImage(tab);
		}
		if (!code) {
			app.requestDraw();
		}
		markExplorer();
		renderTabs();
	};

	ide.closeTab = function (tab, force) {
		if (!force && tab.kind === 'code' && !tab.doc && tabDirty(tab)) {
			const body = el('div', {}, el('p', { textContent: baseName(tab.path) + ' has unsaved changes.' }));
			MS.dialogs.open('Close ' + baseName(tab.path), body, [
				{ text: 'Cancel' },
				{ text: 'Close without saving', action: () => ide.closeTab(tab, true) },
				{ text: 'Save and close', primary: true, action: async () => {
					await saveTab(tab);
					ide.closeTab(tab, true);
				} },
			]);
			return;
		}
		const index = ide.tabs.indexOf(tab);
		ide.tabs.splice(index, 1);
		if (tab.model) {
			tab.model.ccTab = null;
			tab.model.dispose();
			tab.model = null;
		}
		if (ide.active === tab) {
			ide.activate(ide.tabs[Math.max(0, index - 1)] || designerTab());
		} else {
			renderTabs();
		}
		app.updateFileStatus();
	};

	// A Monaco model for a file, made (from the file, or the INI document Studio already has) if there isn't one.
	ide.ensureModel = async function (path) {
		await loadMonaco();
		const existing = ide.tabs.find((t) => t.path && t.path.toLowerCase() === path.toLowerCase() && t.model);
		if (existing) {
			return existing.model;
		}
		const tab = await makeCodeTab(path);
		if (!tab) {
			return null;
		}
		ide.tabs.push(tab);
		renderTabs();
		return tab.model;
	};

	async function makeCodeTab(path) {
		const monaco = ide.monaco;
		const ext = extOf(path);
		const doc = ext === 'ini' ? app.lib.docFor(path) : null;
		let text;
		if (doc) {
			text = doc.serialize();
		} else {
			try {
				text = await app.fs.readText(path);
			} catch (e) {
				app.toast('Couldn\'t open ' + path + ': ' + e.message, 'bad');
				return null;
			}
		}
		const lang = ext === 'lua' ? 'lua' : ext === 'ini' ? 'ccini' : 'plaintext';
		const uri = monaco.Uri.from({ scheme: 'cortex', path: '/' + path });
		const old = monaco.editor.getModel(uri);
		if (old) {
			old.dispose();
		}
		const model = monaco.editor.createModel(text, lang, uri);
		const tab = { id: nextTabId++, kind: 'code', path, lang, model, doc, savedText: text, docText: doc ? text : null, readOnly: app.lockedGameFile(path) || !app.fs.writable };
		model.ccTab = tab;
		model.onDidChangeContent(() => onModelChange(tab));
		checkSoon(tab);
		if (lang === 'lua') {
			// Live values (live.js): once the tab is in the list, its "--?" markers show their values or that they're waiting.
			setTimeout(() => MS.live.noteOpen(path, text), 0);
		}
		return tab;
	}

	// Opens a file in a tab (or goes to its tab), at a line.
	ide.openFile = async function (path, line) {
		path = path.replace(/\\/g, '/').replace(/^(Data|Mods)\//i, '');
		if (ide.mode !== 'deep') {
			ide.setMode('deep');
		}
		const ext = extOf(path);
		let tab = ide.tabs.find((t) => t.path && t.path.toLowerCase() === path.toLowerCase());
		if (!tab && IMAGE_EXT.has(ext)) {
			tab = { id: nextTabId++, kind: 'image', path };
			ide.tabs.push(tab);
		}
		if (!tab) {
			if (!ide.monaco) {
				ide.pendingOpen = { path, line };
				return;
			}
			if (!TEXT_EXT.has(ext)) {
				app.toast('Studio can\'t open .' + ext + ' files.', 'bad');
				return;
			}
			tab = await makeCodeTab(path);
			if (!tab) {
				return;
			}
			ide.tabs.push(tab);
		}
		ide.activate(tab);
		if (line && tab.kind === 'code') {
			setTimeout(() => reveal(line), 0);
		}
	};

	function reveal(line) {
		if (!ide.editor) {
			return;
		}
		ide.editor.revealLineInCenter(line);
		ide.editor.setPosition({ lineNumber: line, column: 1 });
		ide.editor.focus();
	}

	// A picture in a tab: zoom with the wheel (at the cursor), the buttons or + − 0 1, drag to move it, and the pixel under the cursor.
	const IMAGE_ZOOMS = [0.25, 0.5, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64];

	function showImage(tab) {
		const host = $('imageHost');
		host.innerHTML = '';
		const canvas = el('canvas', { className: 'image-view' });
		const holder = el('div', { className: 'image-holder' }, canvas);
		const stage = el('div', { className: 'image-stage', tabIndex: 0 }, holder);
		const zoomText = el('span', { className: 'image-zoom' });
		const where = el('span', { className: 'image-where' });
		const info = el('span', { className: 'image-info', textContent: tab.path });
		const btn = (text, title, action) => el('button', { className: 'ico-btn', textContent: text, title, onclick: action });
		const tools = el('div', { className: 'image-tools' },
			btn('−', 'Zoom out (−, or the wheel)', () => step(-1)),
			zoomText,
			btn('+', 'Zoom in (+, or the wheel)', () => step(1)),
			btn('Fit', 'Fit in the window (0)', () => fit()),
			btn('1:1', 'Actual size (1)', () => setZoom(1)),
			el('span', { className: 'ct-sep' }),
			info,
			el('span', { className: 'spacer' }),
			where);
		host.append(tools, stage);
		let img = null;

		// Zooms keeping the picture's point under (x, y) in the stage where it is; without a point, about the middle.
		function setZoom(zoom, x, y) {
			if (!img) {
				return;
			}
			zoom = Math.max(IMAGE_ZOOMS[0], Math.min(IMAGE_ZOOMS[IMAGE_ZOOMS.length - 1], zoom));
			const old = tab.zoom || zoom;
			const ax = x === undefined ? stage.clientWidth / 2 : x;
			const ay = y === undefined ? stage.clientHeight / 2 : y;
			const r = canvas.getBoundingClientRect();
			const s = stage.getBoundingClientRect();
			const px = (stage.scrollLeft + ax - (r.left - s.left + stage.scrollLeft)) / old;
			const py = (stage.scrollTop + ay - (r.top - s.top + stage.scrollTop)) / old;
			tab.zoom = zoom;
			canvas.style.width = img.width * zoom + 'px';
			canvas.style.height = img.height * zoom + 'px';
			const r2 = canvas.getBoundingClientRect();
			stage.scrollLeft += r2.left - s.left + px * zoom - ax;
			stage.scrollTop += r2.top - s.top + py * zoom - ay;
			zoomText.textContent = (zoom < 1 ? Math.round(zoom * 100) + '%' : zoom + '×');
		}

		function step(dir, x, y) {
			const z = tab.zoom || 1;
			const next = dir > 0 ? IMAGE_ZOOMS.find((v) => v > z + 1e-6) : [...IMAGE_ZOOMS].reverse().find((v) => v < z - 1e-6);
			if (next) {
				setZoom(next, x, y);
			}
		}

		function fit() {
			if (!img) {
				return;
			}
			const room = Math.min((stage.clientWidth - 40) / img.width, (stage.clientHeight - 40) / img.height);
			// Whole steps where it fits (pixels stay square), else the biggest smaller step.
			const zoom = room >= 1 ? Math.min(64, Math.floor(room)) : [...IMAGE_ZOOMS].reverse().find((v) => v <= room) || IMAGE_ZOOMS[0];
			setZoom(zoom);
		}

		stage.addEventListener('wheel', (e) => {
			e.preventDefault();
			const s = stage.getBoundingClientRect();
			step(e.deltaY < 0 ? 1 : -1, e.clientX - s.left, e.clientY - s.top);
		}, { passive: false });
		stage.addEventListener('pointerdown', (e) => {
			if (e.button !== 0 && e.button !== 1) {
				return;
			}
			stage.focus();
			const start = { x: e.clientX, y: e.clientY, left: stage.scrollLeft, top: stage.scrollTop };
			stage.setPointerCapture(e.pointerId);
			stage.classList.add('dragging');
			const move = (ev) => {
				stage.scrollLeft = start.left - (ev.clientX - start.x);
				stage.scrollTop = start.top - (ev.clientY - start.y);
			};
			const up = () => {
				stage.classList.remove('dragging');
				stage.removeEventListener('pointermove', move);
				stage.removeEventListener('pointerup', up);
			};
			stage.addEventListener('pointermove', move);
			stage.addEventListener('pointerup', up);
		});
		canvas.addEventListener('pointermove', (e) => {
			const r = canvas.getBoundingClientRect();
			const x = Math.floor((e.clientX - r.left) / (tab.zoom || 1));
			const y = Math.floor((e.clientY - r.top) / (tab.zoom || 1));
			where.textContent = img && x >= 0 && y >= 0 && x < img.width && y < img.height ? 'x ' + x + ', y ' + y : '';
		});
		canvas.addEventListener('pointerleave', () => (where.textContent = ''));
		stage.addEventListener('keydown', (e) => {
			const keys = { '+': () => step(1), '=': () => step(1), '-': () => step(-1), '_': () => step(-1), '0': fit, '1': () => setZoom(1) };
			if (keys[e.key] && !e.ctrlKey && !e.metaKey && !e.altKey) {
				e.preventDefault();
				keys[e.key]();
			}
		});

		app.images.get(tab.path).then((picture) => {
			if (!picture) {
				info.textContent = tab.path + ' (couldn\'t read it)';
				return;
			}
			img = picture;
			canvas.width = img.width;
			canvas.height = img.height;
			const ctx = canvas.getContext('2d');
			ctx.imageSmoothingEnabled = false;
			ctx.drawImage(img.canvas || img, 0, 0);
			info.textContent = tab.path + ' · ' + img.width + '×' + img.height;
			if (tab.zoom) {
				const z = tab.zoom;
				tab.zoom = 0;
				setZoom(z);
			} else {
				fit();
			}
		});
	}

	// ---- editing ----

	const changeTimers = new Map();
	function onModelChange(tab) {
		if (tab.syncing) {
			return;
		}
		renderTabsSoon();
		app.updateFileStatus();
		clearTimeout(changeTimers.get(tab.id));
		changeTimers.set(tab.id, setTimeout(() => {
			if (tab.doc && !tab.readOnly) {
				applyIniText(tab);
			}
			check(tab);
		}, tab.doc ? 350 : 400));
	}

	let tabsQueued = false;
	function renderTabsSoon() {
		if (!tabsQueued) {
			tabsQueued = true;
			requestAnimationFrame(() => {
				tabsQueued = false;
				renderTabs();
			});
		}
	}

	// The INI text was typed in: the document (and so the designer) takes it, as one undoable change.
	function applyIniText(tab) {
		if (!tab.model) {
			return;
		}
		const text = tab.model.getValue();
		if (text === tab.docText) {
			return;
		}
		app.edit('edit ' + baseName(tab.path), () => {
			if (tab.doc.onWillChange) {
				tab.doc.onWillChange(tab.doc);
			}
			tab.doc.reset(text);
			tab.docText = tab.doc.serialize();
		});
		ide.ini.invalidate();
	}

	// The designer (or undo) changed a document: open INI tabs show it, keeping their own undo history.
	ide.syncModels = function () {
		for (const tab of ide.tabs) {
			if (!tab.doc || !tab.model) {
				continue;
			}
			// After Discard a new document may be gone.
			const current = app.lib.docFor(tab.path);
			if (current !== tab.doc) {
				if (!current) {
					continue;
				}
				tab.doc = current;
			}
			const text = tab.doc.serialize();
			if (text === tab.docText) {
				continue;
			}
			tab.docText = text;
			if (text !== tab.model.getValue()) {
				tab.syncing = true;
				tab.model.pushEditOperations([], [{ range: tab.model.getFullModelRange(), text }], () => null);
				tab.syncing = false;
				checkSoon(tab);
			}
		}
	};

	function checkSoon(tab) {
		clearTimeout(changeTimers.get('c' + tab.id));
		changeTimers.set('c' + tab.id, setTimeout(() => check(tab), 200));
	}

	// Problems in a tab's text, as squiggles and in the Problems panel.
	function check(tab) {
		if (!tab.model || !ide.monaco) {
			return;
		}
		const text = tab.model.getValue();
		let problems = [];
		if (tab.lang === 'lua') {
			problems = ide.lua.check(tab.path, text);
		} else if (tab.lang === 'ccini') {
			const doc = tab.doc && tab.docText === text ? tab.doc : null;
			problems = ide.ini.check(tab.path, text, doc, () => check(tab));
		}
		const severity = ide.monaco.MarkerSeverity;
		ide.monaco.editor.setModelMarkers(tab.model, 'studio', problems.map((p) => ({
			startLineNumber: p.line, startColumn: p.col, endLineNumber: p.endLine || p.line, endColumn: p.endCol || p.col + 1,
			severity: p.severity === 'error' ? severity.Error : p.severity === 'warning' ? severity.Warning : severity.Info,
			message: p.message,
		})));
		applyGameMarkers(tab);
	}

	// ---- saving ----

	async function saveTab(tab) {
		if (tab.kind !== 'code' || tab.doc || tab.readOnly || !tabDirty(tab)) {
			return false;
		}
		const text = tab.model.getValue();
		try {
			await app.fs.writeFile(tab.path, text);
		} catch (e) {
			app.toast('Couldn\'t save ' + tab.path + ': ' + e.message, 'bad');
			return false;
		}
		tab.savedText = text;
		if (tab.lang === 'lua') {
			ide.lua.updateScript(tab.path, text);
		}
		return true;
	}

	// Saves the open scripts and every changed INI file; saved scripts are reloaded in a game started from Studio.
	ide.saveAll = async function () {
		if (!app.lib) {
			return;
		}
		const tabs = ide.tabs.filter((t) => t.kind === 'code' && !t.doc && tabDirty(t));
		const scripts = [];
		for (const tab of tabs) {
			if (await saveTab(tab) && tab.lang === 'lua') {
				scripts.push(tab.path);
				await MS.live.afterSave(tab.path, tab.savedText);
			}
		}
		const inis = await app.save(true);
		renderTabs();
		app.updateFileStatus();
		const total = tabs.length + inis;
		if (total) {
			app.toast('Saved ' + total + ' file' + (total > 1 ? 's' : '') + '.', 'good');
		}
		if (scripts.length) {
			// Only the saved scripts reload, so other scripted objects keep their state. The game reloads everything when no object uses one.
			ide.requestScripts({ scripts });
		}
	};

	// ---- requests to the game (ModStudio/Scripts.ini) ----

	// Requests written close together go as one file write: the game looks at the file a few times a second, and a second write before it
	// looked would replace the first.
	let pendingRequest = null;
	let requestTimer = 0;
	let lastRequestAt = 0;
	ide.requestScripts = function (request, loud) {
		if (!app.fs || !app.fs.writable) {
			return;
		}
		const merged = Object.assign(pendingRequest || {}, request);
		if (request.scripts && pendingRequest && pendingRequest.scripts) {
			merged.scripts = [...new Set([...pendingRequest.scripts, ...request.scripts])];
		}
		if (request.files && pendingRequest && pendingRequest.files) {
			merged.files = [...new Set([...pendingRequest.files, ...request.files])];
		}
		pendingRequest = merged;
		clearTimeout(requestTimer);
		const wait = Math.max(0, 350 - (Date.now() - lastRequestAt));
		requestTimer = setTimeout(async () => {
			const req = pendingRequest;
			pendingRequest = null;
			lastRequestAt = Date.now();
			const lines = ['// Written by Cortex Mod Studio for a game started with -modstudio (Test In Game, Test In Sandbox or Actor Editor in Tools\\ModStudio).', 'Stamp = ' + Date.now()];
			if (req.reload) {
				lines.push('ReloadScripts = 1');
			} else if (req.scripts) {
				for (const path of req.scripts) {
					lines.push('ReloadScript = ' + path);
				}
			}
			for (const path of req.files || []) {
				lines.push('ReloadFile = ' + path);
			}
			if (req.run) {
				lines.push('Run = ' + RUN_FILE);
			}
			try {
				await app.fs.writeDisk(SCRIPTS_FILE, lines.join('\r\n') + '\r\n');
				if (req.reload || req.scripts) {
					clearGameProblems();
					if (loud) {
						app.toast('Asked the game to reload its scripts.', 'good');
					}
					consoleLine(req.reload ? '— reload all scripts sent —' : '— reload sent: ' + req.scripts.map(baseName).join(', ') + ' —', 'studio');
				}
			} catch (e) {
				app.toast('Couldn\'t send that to the game: ' + e.message, 'bad');
			}
		}, wait);
	};

	// ---- the file bridge: files saved in another editor ----

	// Start-ModStudio.ps1 watches Data and Mods. When a file is saved by anything but Studio it asks the game to reload it itself, and this
	// keeps the page up to date: open scripts and INI files read the new text, and pictures are drawn again. Unsaved edits in Studio win.
	let bridgeSeq = -1;
	async function pollBridge() {
		if (!app.fs || !app.fs.server || !app.lib) {
			setTimeout(pollBridge, 2000);
			return;
		}
		let text;
		try {
			const response = await fetch('/api/changes?since=' + bridgeSeq, { headers: { 'X-ModStudio-Token': app.fs.server }, cache: 'no-store' });
			if (response.status === 404) {
				return; // Started with -NoBridge, or a Studio window from before the bridge.
			}
			if (!response.ok) {
				throw new Error(response.statusText);
			}
			text = await response.text();
		} catch (e) {
			setTimeout(pollBridge, 3000);
			return;
		}
		const lines = text.split('\n');
		const seq = Number(lines.shift()) || 0;
		const changes = bridgeSeq < 0 ? [] : lines.filter(Boolean).map((line) => {
			const [path, kind] = line.split('\t');
			return { path, kind };
		});
		bridgeSeq = seq;
		if (changes.length) {
			try {
				await takeOutsideChanges(changes);
			} catch (e) {
				console.warn('Couldn\'t read a file saved outside Studio: ' + e.message);
			}
		}
		setTimeout(pollBridge, 1000);
	}

	// Also used by History's Restore (why says where the change came from, for the console line).
	ide.reloadFromDisk = (changes, why) => takeOutsideChanges(changes, why);

	async function takeOutsideChanges(changes, why) {
		const names = [];
		const kept = [];
		const sprites = [];
		let inisChanged = false;
		for (const { path: disk, kind } of changes) {
			const path = disk.replace(/^(Data|Mods)\//i, '');
			const ext = extOf(path);
			const deleted = kind === 'deleted';
			names.push(baseName(path) + (deleted ? ' (deleted)' : ''));
			if (ext === 'png' || ext === 'bmp') {
				sprites.push(path);
				continue;
			}
			if (ext === 'lua') {
				const tab = ide.tabs.find((t) => t.kind === 'code' && !t.doc && t.path.toLowerCase() === path.toLowerCase());
				if (deleted) {
					ide.lua.scripts.delete(path.toLowerCase());
					continue;
				}
				const fresh = await (await app.fs.readFile(disk)).text();
				if (tab && tabDirty(tab)) {
					kept.push(baseName(path));
					tab.savedText = fresh; // Saving from Studio now would replace the outside change, as it should: it's what the tab shows.
					continue;
				}
				ide.lua.updateScript(tab ? tab.path : path, fresh);
				if (tab && tab.model && tab.model.getValue() !== fresh) {
					tab.savedText = fresh;
					tab.model.pushEditOperations([], [{ range: tab.model.getFullModelRange(), text: fresh }], () => null);
					tab.savedText = fresh;
					checkSoon(tab);
				} else if (tab) {
					tab.savedText = fresh;
				}
				continue;
			}
			if (ext === 'ini' && !deleted) {
				const doc = app.lib.docFor(path);
				app.fs.texts.delete(disk.toLowerCase());
				if (!doc) {
					continue; // A file no loaded mod includes yet: Reload in the File menu picks it up.
				}
				if (doc.dirty) {
					kept.push(baseName(path));
					continue;
				}
				doc.reset(await app.fs.readText(disk));
				doc.savedText = doc.serialize();
				doc.dirty = false;
				inisChanged = true;
			}
		}
		if (sprites.length) {
			// The game's frames are "Name000.png", "Name001.png"...; the base name without the number is what INI files write.
			app.forgetSprites(sprites.concat(sprites.map((p) => p.replace(/\d{3}(\.\w+)$/, '$1'))));
			MS.panels.forgetThumbs();
		}
		if (inisChanged) {
			ide.ini.invalidate();
			app.lib.reindex();
			app.rebuild();
			MS.panels.refresh();
			ide.syncModels();
			app.updateFileStatus();
		}
		if (sprites.length) {
			app.requestDraw();
		}
		renderTabs();
		consoleLine('— ' + (why || 'saved outside Studio') + ': ' + names.join(', ') + ' —', 'studio');
		if (kept.length) {
			app.toast(kept.join(', ') + (kept.length > 1 ? ' were' : ' was') + ' changed on disk, but has unsaved edits in Studio, so Studio kept yours. Saving replaces the outside change.', 'bad');
		}
	}

	// Runs Lua in the game (the console box, or Ctrl+Enter on a selection). An expression's value is printed.
	async function runLua(code) {
		if (!code) {
			return;
		}
		consoleLine('> ' + code, 'input');
		let isExpression = false;
		try {
			window.luaparse.parse('return ' + code, { luaVersion: '5.2' });
			isExpression = !/^\s*(local\s|function\s|for\s|while\s|if\s|repeat\b|return\b)/.test(code);
		} catch (e) {
			isExpression = false;
		}
		const wrapped = isExpression
			? 'local __ok, __a, __b = pcall(function() return ' + code + '\nend)\nif __ok then ConsoleMan:PrintString("STUDIO-RESULT: " .. tostring(__a) .. (__b ~= nil and (", " .. tostring(__b)) or "")) else ConsoleMan:PrintString("ERROR: " .. tostring(__a)) end\n'
			: 'local __ok, __e = pcall(function()\n' + code + '\nend)\nif not __ok then ConsoleMan:PrintString("ERROR: " .. tostring(__e)) end\n';
		try {
			await app.fs.writeDisk(RUN_FILE, wrapped);
		} catch (e) {
			app.toast('Couldn\'t send that to the game: ' + e.message, 'bad');
			return;
		}
		ide.requestScripts({ run: true });
	}

	function bindRunBox() {
		const input = $('runInput');
		const history = app.readLocal('ms.runHistory') || [];
		let at = history.length;
		input.addEventListener('keydown', (e) => {
			if (e.key === 'Enter') {
				const code = input.value.trim();
				if (code) {
					history.push(code);
					if (history.length > 50) {
						history.shift();
					}
					app.writeLocal('ms.runHistory', history);
					at = history.length;
					runLua(code);
					input.value = '';
				}
			} else if (e.key === 'ArrowUp' && at > 0) {
				input.value = history[--at];
				e.preventDefault();
			} else if (e.key === 'ArrowDown') {
				at = Math.min(history.length, at + 1);
				input.value = history[at] || '';
				e.preventDefault();
			}
		});
	}

	// ---- the game's console (ModStudio/Console.log) ----

	let consoleOffset = 0;
	let consoleTimer = 0;
	let consoleCount = 0;
	let unseenErrors = 0;
	const gameProblems = new Map(); // lowercased module path -> [{line, message}]

	function startConsole() {
		if (!app.fs || !app.fs.server) {
			consoleLine('The game\'s console shows here when Studio is started from Mod Studio.bat.', 'studio');
			return;
		}
		consoleLine('The game\'s console shows here for games started with Test In Game, Test In Sandbox or Actor Editor (in Tools\\ModStudio). Lua typed below runs in the game.', 'studio');
		pollConsole();
	}

	async function pollConsole() {
		clearTimeout(consoleTimer);
		try {
			const { text, size, from } = await app.fs.tail(CONSOLE_FILE, consoleOffset);
			if (size < consoleOffset || from < consoleOffset) {
				consoleOffset = 0;
				clearGameProblems();
				consoleLine('— the game started again —', 'studio');
				consoleTimer = setTimeout(pollConsole, 50);
				return;
			}
			if (text) {
				// Only whole lines; the rest waits for the next look.
				const lastBreak = text.lastIndexOf('\n');
				if (lastBreak >= 0) {
					const whole = text.slice(0, lastBreak + 1);
					const first = consoleOffset === 0;
					consoleOffset += new TextEncoder().encode(whole).length;
					// A long log from earlier: only its end.
					const lines = whole.split('\n');
					for (const line of first && lines.length > 400 ? lines.slice(-400) : lines) {
						if (line.trim()) {
							gameLine(line.replace(/\r$/, ''));
						}
					}
				}
			}
		} catch (e) {
			// Studio's window was closed, or the file isn't there yet.
		}
		consoleTimer = setTimeout(pollConsole, ide.mode === 'deep' ? 500 : 2000);
	}

	function gameLine(line) {
		let kind = 'out';
		if (/^ERROR\b|error:/i.test(line)) {
			kind = 'error';
		} else if (/^WARNING\b/i.test(line)) {
			kind = 'warn';
		} else if (/^SYSTEM\b/.test(line)) {
			kind = 'system';
		} else if (/^STUDIO-RESULT: /.test(line)) {
			kind = 'result';
			line = '= ' + line.slice(15);
		} else if (/^PRINT: /.test(line)) {
			kind = 'print';
			line = line.slice(7);
		}
		const row = consoleLine(line, kind);
		// "Base.rte/Foo.lua:12: attempt to …": a link to the line, and a squiggle on it.
		const m = /([\w\-. ]+\.rte\/[^:\n"]+?\.lua):(\d+):?\s*(.*)$/.exec(line);
		if (m && row) {
			row.classList.add('link');
			row.title = 'Open ' + m[1] + ' at line ' + m[2];
			row.onclick = () => ide.openFile(m[1], Number(m[2]));
			if (kind === 'error') {
				const key = m[1].toLowerCase();
				if (!gameProblems.has(key)) {
					gameProblems.set(key, []);
				}
				const list = gameProblems.get(key);
				if (!list.some((p) => p.line === Number(m[2]) && p.message === m[3])) {
					list.push({ line: Number(m[2]), message: m[3] || line, path: m[1] });
					const tab = ide.tabs.find((t) => t.path && t.path.toLowerCase() === key);
					if (tab) {
						applyGameMarkers(tab);
					}
					renderProblems();
				}
			}
		}
		if (kind === 'error' && (ide.panel !== 'console' || $('app').classList.contains('no-panel'))) {
			unseenErrors++;
			$('consoleBadge').textContent = unseenErrors;
			$('consoleBadge').hidden = false;
			$('statusConsoleBadge').textContent = unseenErrors;
			$('statusConsoleBadge').hidden = false;
		}
	}

	function consoleLine(text, kind) {
		const body = $('consoleBody');
		const atBottom = body.scrollTop + body.clientHeight >= body.scrollHeight - 30;
		const row = el('div', { className: 'cline ' + kind, textContent: text });
		body.append(row);
		if (++consoleCount > 3000) {
			body.firstChild.remove();
			consoleCount--;
		}
		if (atBottom) {
			body.scrollTop = body.scrollHeight;
		}
		return row;
	}

	function clearConsole() {
		$('consoleBody').innerHTML = '';
		consoleCount = 0;
	}

	function clearGameProblems() {
		gameProblems.clear();
		for (const tab of ide.tabs) {
			if (tab.model) {
				applyGameMarkers(tab);
			}
		}
		renderProblems();
	}

	function applyGameMarkers(tab) {
		if (!ide.monaco || !tab.model) {
			return;
		}
		const list = gameProblems.get((tab.path || '').toLowerCase()) || [];
		ide.monaco.editor.setModelMarkers(tab.model, 'game', list.map((p) => ({
			startLineNumber: p.line, startColumn: 1, endLineNumber: p.line, endColumn: tab.model.getLineMaxColumn(Math.min(p.line, tab.model.getLineCount())),
			severity: ide.monaco.MarkerSeverity.Error, message: 'In the game: ' + p.message,
		})));
	}

	// ---- the Problems and Watch panels ----

	let problemsQueued = false;
	function renderProblems() {
		if (problemsQueued) {
			return;
		}
		problemsQueued = true;
		requestAnimationFrame(() => {
			problemsQueued = false;
			const body = $('problemsBody');
			body.innerHTML = '';
			let count = 0;
			const shown = new Set();
			const groups = new Map();
			if (ide.monaco) {
				for (const marker of ide.monaco.editor.getModelMarkers({})) {
					const tab = ide.tabs.find((t) => t.model && t.model.uri.toString() === marker.resource.toString());
					if (!tab) {
						continue;
					}
					if (!groups.has(tab.path)) {
						groups.set(tab.path, []);
					}
					groups.get(tab.path).push({ line: marker.startLineNumber, message: marker.message, severity: marker.severity >= 8 ? 'error' : marker.severity >= 4 ? 'warn' : 'info' });
					shown.add(tab.path.toLowerCase());
				}
			}
			for (const [key, list] of gameProblems) {
				if (!shown.has(key)) {
					groups.set(list[0].path, list.map((p) => ({ line: p.line, message: 'In the game: ' + p.message, severity: 'error' })));
				}
			}
			for (const [path, list] of groups) {
				body.append(el('div', { className: 'pgroup', textContent: path }));
				for (const p of list.sort((a, b) => a.line - b.line)) {
					const row = el('div', { className: 'prow ' + p.severity }, el('span', { className: 'pline', textContent: p.line }), el('span', { textContent: p.message }));
					row.onclick = () => ide.openFile(path, p.line);
					body.append(row);
					count++;
				}
			}
			if (!count) {
				body.append(el('div', { className: 'pempty', textContent: 'No problems in the open files.' }));
			}
			$('problemsBadge').textContent = count;
			$('problemsBadge').hidden = !count;
		});
	}

	let watchTimer = 0;
	async function pollWatch() {
		clearTimeout(watchTimer);
		if (ide.panel !== 'watch' || ide.mode !== 'deep' || !app.fs || !app.fs.server) {
			return;
		}
		const body = $('watchBody');
		try {
			const response = await app.fs.api('GET', 'file', WATCH_FILE);
			const text = response ? await response.text() : '';
			const rows = text.split(/\r?\n/).filter((l) => l.includes(' = '));
			body.innerHTML = '';
			if (!rows.length) {
				body.append(el('div', { className: 'pempty', textContent: 'Nothing watched yet. In a script, StudioWatch("speed", self.Vel.Magnitude) shows a value here while the game runs.' }));
			}
			const table = el('table', { className: 'watch-table' });
			for (const row of rows) {
				const at = row.indexOf(' = ');
				table.append(el('tr', {}, el('td', { textContent: row.slice(0, at) }), el('td', { textContent: row.slice(at + 3) })));
			}
			if (rows.length) {
				body.append(table);
			}
		} catch (e) {
			// try again
		}
		watchTimer = setTimeout(pollWatch, 400);
	}

	ide.showPanel = function (panel, keepHidden) {
		ide.panel = panel;
		for (const b of document.querySelectorAll('#bottomTabs button[data-panel]')) {
			b.classList.toggle('sel', b.dataset.panel === panel);
		}
		for (const id of ['console', 'problems', 'watch']) {
			$(id + 'Pane').hidden = id !== panel;
		}
		if (!keepHidden && $('app').classList.contains('no-panel')) {
			$('app').classList.remove('no-panel');
			app.writeLocal('ms.panelHidden', false);
			app.requestDraw();
		}
		if (panel === 'console') {
			unseenErrors = 0;
			$('consoleBadge').hidden = true;
			$('statusConsoleBadge').hidden = true;
			const body = $('consoleBody');
			body.scrollTop = body.scrollHeight;
		}
		if (panel === 'problems') {
			renderProblems();
		}
		if (panel === 'watch') {
			pollWatch();
		}
		app.writeLocal('ms.panel', panel);
	};

	ide.togglePanel = function () {
		const hidden = $('app').classList.toggle('no-panel');
		app.writeLocal('ms.panelHidden', hidden);
		app.requestDraw();
	};

	function bindPanelResize() {
		const handle = $('panelResize');
		let startY = 0;
		let startH = 0;
		const saved = app.readLocal('ms.panelHeight');
		if (saved) {
			$('app').style.setProperty('--bottom-h', saved + 'px');
		}
		if (app.readLocal('ms.panelHidden')) {
			$('app').classList.add('no-panel');
		}
		ide.panel = app.readLocal('ms.panel') || 'console';
		handle.addEventListener('pointerdown', (e) => {
			startY = e.clientY;
			startH = $('bottom').getBoundingClientRect().height;
			handle.setPointerCapture(e.pointerId);
			const move = (ev) => {
				const h = Math.max(90, Math.min(window.innerHeight - 200, startH + startY - ev.clientY));
				$('app').style.setProperty('--bottom-h', h + 'px');
				app.writeLocal('ms.panelHeight', h);
				app.requestDraw();
			};
			const up = () => {
				handle.removeEventListener('pointermove', move);
				handle.removeEventListener('pointerup', up);
			};
			handle.addEventListener('pointermove', move);
			handle.addEventListener('pointerup', up);
		});
		setTimeout(() => ide.showPanel(ide.panel, true), 0);
	}

	// ---- the side bar: files, presets, search, package ----

	ide.showView = function (view) {
		if (!view) {
			$('btnLib').click(); // hides the side bar (L)
			return;
		}
		if ($('app').classList.contains('no-lib')) {
			$('btnLib').click();
		}
		ide.view = view;
		for (const b of document.querySelectorAll('#activity button[data-view]')) {
			b.classList.toggle('sel', b.dataset.view === view);
		}
		$('app').dataset.view = view;
		for (const id of ['files', 'outline', 'search', 'package']) {
			$('view-' + id).hidden = id !== view;
		}
		app.requestDraw();
		app.writeLocal('ms.deepView', view);
		if (view === 'search') {
			setTimeout(() => $('searchInput').focus(), 0);
		}
		if (view === 'outline') {
			MS.nav.renderOutline();
		}
		if (view === 'package') {
			renderPackage();
		}
	};

	// -- files --

	const expanded = new Set();
	// The file type and name filter: with either set, the tree becomes the matching files under their folders.
	const FILE_KINDS = { lua: /\.lua$/i, ini: /\.ini$/i, image: /\.(png|bmp|jpe?g|gif)$/i, sound: /\.(wav|ogg|flac|mp3)$/i };
	let fileKind = '';
	const moduleTrees = new Map();
	let allFilesTried = false;
	let filterRun = 0;

	// Lua and INI files come from what Studio has already read (instant); pictures and sounds need the folder listings, which are fetched a
	// module at a time and shown as they come, your mods first.
	async function renderFiltered(box, mods, game, kind, text) {
		const run = ++filterRun;
		const matches = (path) => (!kind || FILE_KINDS[kind].test(path)) && (!text || path.toLowerCase().includes(text));
		const known = new Set();
		for (const doc of app.lib.docs.values()) {
			known.add(doc.path);
		}
		for (const sc of ide.lua.scripts.values()) {
			known.add(sc.path);
		}
		const needListing = kind !== 'lua' && kind !== 'ini';
		const LIMIT = 2000;
		const draw = (pending) => {
			const out = [];
			let shown = 0;
			for (const [title, list, official] of [['Your mods', mods, false], [app.unlocked ? 'Game (unlocked)' : 'Game (read only)', game, true]]) {
				const rows = [];
				let count = 0;
				for (const m of list) {
					const prefix = m.name.toLowerCase() + '/';
					const paths = new Set([...known].filter((p) => p.toLowerCase().startsWith(prefix)));
					for (const rel of moduleTrees.get(m.name) || []) {
						paths.add(m.name + '/' + rel);
					}
					let folder = null;
					for (const path of [...paths].filter(matches).sort((a, b) => a.localeCompare(b, undefined, { numeric: true }))) {
						if (shown >= LIMIT) {
							break;
						}
						const dir = path.slice(0, path.lastIndexOf('/'));
						if (dir !== folder) {
							folder = dir;
							rows.push(el('div', { className: 'x-dir', textContent: dir, title: dir }));
						}
						rows.push(fileRow(path, path.slice(path.lastIndexOf('/') + 1), 1, official));
						shown++;
						count++;
					}
				}
				out.push(groupHead(title, count), ...(rows.length ? rows : [el('div', { className: 'x-empty', textContent: pending ? 'Looking…' : 'Nothing matches.' })]));
			}
			if (pending) {
				out.push(el('div', { className: 'x-empty', textContent: 'Reading ' + pending + '…' }));
			} else if (shown >= LIMIT) {
				out.push(el('div', { className: 'x-empty', textContent: 'Showing the first ' + LIMIT + ': type more of the name to narrow it.' }));
			}
			box.innerHTML = '';
			box.append(...out);
			markExplorer();
		};
		let missing = needListing ? [...mods, ...game].filter((m) => !moduleTrees.has(m.name)) : [];
		if (missing.length && !allFilesTried) {
			// One listing of everything (a newer Studio server), split by module; older servers are asked a module at a time below.
			allFilesTried = true;
			draw('the game folder');
			let all = null;
			try {
				all = await app.fs.allFiles();
			} catch (e) {
				all = null;
			}
			if (all) {
				const byModule = new Map(app.fs.modules().map((m) => [m.name.toLowerCase(), []]));
				for (const path of all) {
					const cut = path.indexOf('/');
					const list = cut > 0 && byModule.get(path.slice(0, cut).toLowerCase());
					if (list) {
						list.push(path.slice(cut + 1));
					}
				}
				for (const m of app.fs.modules()) {
					moduleTrees.set(m.name, byModule.get(m.name.toLowerCase()));
				}
			}
			if (run !== filterRun) {
				return;
			}
			missing = missing.filter((m) => !moduleTrees.has(m.name));
		}
		draw(missing.length ? missing[0].name : null);
		for (let i = 0; i < missing.length; i++) {
			const m = missing[i];
			let files;
			try {
				files = (await app.fs.tree(app.fs.toDiskPath(m.name))).map((f) => f.path.replace(/\\/g, '/'));
			} catch (e) {
				files = [];
			}
			moduleTrees.set(m.name, files);
			if (run !== filterRun) {
				return; // the filter changed: the listing is kept for next time
			}
			draw(missing[i + 1] ? missing[i + 1].name : null);
		}
	}

	function renderExplorer() {
		const box = $('explorer');
		if (!box || !app || !app.fs || !app.lib) {
			return;
		}
		const text = ($('fileFilter').value || '').trim().toLowerCase();
		if (fileKind || text) {
			renderFiltered(box, app.fs.modules().filter((m) => m.home === 'Mods' && app.inScope(m.name)).sort((a, b) => a.name.localeCompare(b.name)), app.fs.modules().filter((m) => m.home === 'Data' && app.inScope(m.name)), fileKind, text);
			return;
		}
		filterRun++;
		const saved = app.readLocal('ms.expanded');
		if (saved && !expanded.size) {
			for (const p of saved) {
				expanded.add(p);
			}
		}
		box.innerHTML = '';
		const mods = app.fs.modules().filter((m) => m.home === 'Mods' && app.inScope(m.name)).sort((a, b) => a.name.localeCompare(b.name));
		const game = app.fs.modules().filter((m) => m.home === 'Data' && app.inScope(m.name));
		box.append(groupHead('Your mods', mods.length));
		if (!mods.length) {
			box.append(el('div', { className: 'x-empty', textContent: 'No mods yet. Make my own copy of a unit, or New mod, starts one.' }));
		}
		for (const m of mods) {
			box.append(folderRow(m.name, m.name, 0, false));
		}
		box.append(groupHead(app.unlocked ? 'Game (unlocked)' : 'Game (read only)', game.length));
		for (const m of game) {
			box.append(folderRow(m.name, m.name, 0, true));
		}
	}

	function groupHead(text, count) {
		return el('div', { className: 'x-group', textContent: text + ' · ' + count });
	}

	function folderRow(path, name, depth, official) {
		const wrap = el('div', { className: 'x-folder' });
		const open = expanded.has(path.toLowerCase());
		const row = el('div', { className: 'x-row folder' + (open ? ' open' : ''), title: path }, el('span', { className: 'x-twist', textContent: open ? '▾' : '▸' }), el('span', { className: 'x-name', textContent: name }));
		row.style.paddingLeft = 6 + depth * 12 + 'px';
		row.dataset.path = path;
		const children = el('div', { className: 'x-children' });
		row.onclick = () => {
			const key = path.toLowerCase();
			if (expanded.has(key)) {
				expanded.delete(key);
				children.innerHTML = '';
				row.classList.remove('open');
				row.firstChild.textContent = '▸';
			} else {
				expanded.add(key);
				row.classList.add('open');
				row.firstChild.textContent = '▾';
				fillFolder(children, path, depth + 1, official);
			}
			app.writeLocal('ms.expanded', [...expanded]);
		};
		row.oncontextmenu = (e) => {
			e.preventDefault();
			folderMenu(e, path, official);
		};
		wrap.append(row, children);
		if (open) {
			fillFolder(children, path, depth + 1, official);
		}
		return wrap;
	}

	async function fillFolder(box, path, depth, official) {
		let entries;
		try {
			entries = await app.fs.listDir(app.fs.toDiskPath(path));
		} catch (e) {
			entries = [];
		}
		box.innerHTML = '';
		entries.sort((a, b) => (a.kind === b.kind ? a.name.localeCompare(b.name, undefined, { numeric: true }) : a.kind === 'directory' ? -1 : 1));
		for (const entry of entries) {
			const child = path + '/' + entry.name;
			if (entry.kind === 'directory') {
				box.append(folderRow(child, entry.name, depth, official));
			} else {
				box.append(fileRow(child, entry.name, depth, official));
			}
		}
		markExplorer();
	}

	function fileRow(path, name, depth, official) {
		const ext = extOf(name);
		const row = el('div', { className: 'x-row file', title: path }, el('span', { className: 'x-ext ' + ext, textContent: ext || '·' }), el('span', { className: 'x-name', textContent: name }));
		row.style.paddingLeft = 6 + depth * 12 + 14 + 'px';
		row.dataset.path = path;
		row.onclick = () => ide.openFile(path);
		row.oncontextmenu = (e) => {
			e.preventDefault();
			fileMenu(e, path, official);
		};
		return row;
	}

	function markExplorer() {
		const path = ide.active && ide.active.path ? ide.active.path.toLowerCase() : null;
		for (const row of document.querySelectorAll('#explorer .x-row.file')) {
			row.classList.toggle('sel', !!path && row.dataset.path.toLowerCase() === path);
		}
	}

	function contextMenu(e, items) {
		closeMenu();
		const menu = el('div', { className: 'ctx-menu', id: 'ctxMenu' });
		for (const item of items) {
			if (!item) {
				menu.append(el('div', { className: 'ctx-sep' }));
				continue;
			}
			const b = el('button', { textContent: item.text, disabled: !!item.disabled });
			b.onclick = () => {
				closeMenu();
				item.action();
			};
			menu.append(b);
		}
		document.body.append(menu);
		const r = menu.getBoundingClientRect();
		menu.style.left = Math.min(e.clientX, window.innerWidth - r.width - 8) + 'px';
		menu.style.top = Math.min(e.clientY, window.innerHeight - r.height - 8) + 'px';
		setTimeout(() => document.addEventListener('pointerdown', onMenuOutside, true), 0);
	}

	function onMenuOutside(e) {
		if (!e.target.closest('#ctxMenu')) {
			closeMenu();
		}
	}

	function closeMenu() {
		const menu = $('ctxMenu');
		if (menu) {
			menu.remove();
		}
		document.removeEventListener('pointerdown', onMenuOutside, true);
	}

	function folderMenu(e, path, official) {
		contextMenu(e, [
			{ text: 'New Lua script…', disabled: official, action: () => newFileDialog(path, 'lua') },
			{ text: 'New INI file…', disabled: official, action: () => newFileDialog(path, 'ini') },
			null,
			{ text: 'Copy path', action: () => navigator.clipboard.writeText(path) },
			{ text: 'Refresh', action: () => refreshExplorer() },
		]);
	}

	function fileMenu(e, path, official) {
		contextMenu(e, [
			{ text: 'Open', action: () => ide.openFile(path) },
			{ text: 'Copy path', action: () => navigator.clipboard.writeText(path) },
			{ text: 'Find where it\'s used', action: () => showUses(path) },
			null,
			{ text: 'Rename…', disabled: official, action: () => renameDialog(path) },
			{ text: 'Delete…', disabled: official, action: () => deleteDialog(path) },
		]);
	}

	// The top bar's module choice changed.
	ide.scopeChanged = function () {
		renderExplorer();
		if (ide.view === 'search' && $('searchInput').value) {
			runSearch();
		}
	};

	function refreshExplorer() {
		app.fs.forgetListings();
		moduleTrees.clear();
		allFilesTried = false;
		renderExplorer();
	}

	const TEMPLATES = {
		entity: (name, preset) => [
			'-- ' + name + (preset ? ': the script of ' + preset + '.' : '.'),
			'-- The game runs these functions. Type "function " for the others it calls, and "self." or "self:" to see what the object has.',
			'',
			'function Create(self)',
			'\t',
			'end',
			'',
			'function Update(self)',
			'\t',
			'end',
			'',
			'function Destroy(self)',
			'\t',
			'end',
			'',
		].join('\n'),
		activity: (name) => {
			const cls = name.replace(/\W/g, '') || 'MyActivity';
			return [
				'-- ' + name + ': an activity. A GAScripted preset with ScriptPath set to this file and LuaClassName = ' + cls + ' runs it.',
				'',
				'function ' + cls + ':StartActivity(isNewGame)',
				'\t',
				'end',
				'',
				'function ' + cls + ':UpdateActivity()',
				'\t',
				'end',
				'',
			].join('\n');
		},
		blank: () => '',
		ini: () => '// ' + 'Presets go here. Include this file from the mod\'s Index.ini (IncludeFile = ...).\n',
	};

	// INI files that start with a preset in them.
	const INI_KINDS = {
		blank: { label: 'Empty' },
		unit: { label: 'Unit (a copy of one of the game\'s)', file: 'MyUnit', block: 'AddActor', base: ['AHuman', 'ACrab'], baseLabel: 'Starts as a copy of', suggest: 'Soldier Light' },
		gun: { label: 'Gun (a copy of one of the game\'s)', file: 'MyGun', block: 'AddDevice', base: ['HDFirearm'], baseLabel: 'Starts as a copy of', suggest: 'Assault Rifle' },
		activity: { label: 'Scripted activity (with its Lua script)', file: 'MyActivity', base: ['Scene'], baseLabel: 'Plays on the scene' },
		scene: { label: 'Scene (a copy of one of the game\'s)', file: 'MyScene', block: 'AddScene', base: ['Scene'], baseLabel: 'Starts as a copy of' },
	};

	// Makes an INI file with one preset in it (included from the mod's Index.ini), and for an activity its script too.
	async function makePresetFile(path, kindName, baseName_) {
		const k = INI_KINDS[kindName];
		const lib = app.lib;
		const modName = path.split('/')[0];
		const name = baseName(path).replace(/\.ini$/i, '').replace(/[_-]+/g, ' ');
		const basePreset = baseName_ ? lib.presets.find((pr) => pr.topLevel && k.base.includes(pr.cls) && pr.name === baseName_) : null;
		if (k.block && !basePreset) {
			app.toast('Pick a preset to copy: "' + baseName_ + '" isn\'t one.', 'bad');
			return false;
		}
		if (lib.find(name, null, modName) && lib.find(name, null, modName).name.toLowerCase() === name.toLowerCase()) {
			app.toast('There is already a preset called ' + name + '. Pick another file name.', 'bad');
			return false;
		}
		const doc = await lib.newDoc(path);
		app.hookDoc(doc);
		app.hookDoc(lib.docFor(modName + '/Index.ini'));
		if (k.block) {
			const block = doc.addTopLevel(k.block, basePreset.cls);
			block.add('CopyOf', lib.reference(basePreset.name, basePreset.module, modName));
			block.add('PresetName', name);
		} else {
			// A scripted activity: the preset and its script.
			const cls = name.replace(/\W/g, '') || 'MyActivity';
			const script = path.replace(/[^/]*$/, '') + cls + '.lua';
			const block = doc.addTopLevel('AddActivity', 'GAScripted');
			block.add('PresetName', name);
			block.add('Description', 'Made with Cortex Mod Studio.');
			if (basePreset) {
				block.add('SceneName', basePreset.name);
			}
			block.add('ScriptPath', script);
			block.add('LuaClassName', cls);
			block.add('MinTeamsRequired', '1');
			block.add('Team1Name', 'Players');
			if (!(await app.fs.exists(script))) {
				const text = TEMPLATES.activity(cls);
				try {
					await app.fs.writeFile(script, text);
					ide.lua.updateScript(script, text);
				} catch (e) {
					app.toast('Couldn\'t make ' + script + ': ' + e.message, 'bad');
				}
			}
		}
		lib.reindex();
		app.writeLocal('ms.lastMod', modName);
		const made = lib.presets.find((pr) => pr.doc === doc && pr.name === name);
		if (made && made.cls === 'AHuman') {
			app.openActor(made);
		}
		await app.save(true, [doc, lib.docFor(modName + '/Index.ini')].filter(Boolean));
		app.updateFileStatus();
		refreshExplorer();
		await ide.openFile(path, 1);
		app.toast('Made and saved ' + name + ' in ' + modName + '. The game loads new presets when it starts.', 'good');
		return true;
	}

	function newFileDialog(folder, kind, preset, onMade) {
		const name = el('input', { type: 'text', value: kind === 'lua' ? (preset ? preset.replace(/[^\w -]/g, '').replace(/\s+/g, '') : 'NewScript') + '.lua' : 'NewFile.ini' });
		const template = el('select', {});
		if (kind === 'lua') {
			template.append(el('option', { value: 'entity', textContent: 'Object script (Create, Update, Destroy)' }), el('option', { value: 'activity', textContent: 'Activity script (StartActivity, UpdateActivity)' }), el('option', { value: 'blank', textContent: 'Empty' }));
		} else {
			for (const [value, text] of Object.entries(INI_KINDS).map(([k, v]) => [k, v.label])) {
				template.append(el('option', { value, textContent: text }));
			}
		}
		const where = el('input', { type: 'text', value: folder });
		// INI: the preset the new one copies (or, for an activity, the scene it plays on).
		const base = el('input', { type: 'text', placeholder: 'Type to pick from the game\'s presets' });
		const baseList = el('datalist', { id: 'newBaseList' });
		base.setAttribute('list', 'newBaseList');
		const baseLabel = el('label', {});
		const baseField = el('div', { className: 'field' }, baseLabel, base, baseList);
		const syncBase = () => {
			const k = kind === 'ini' && INI_KINDS[template.value];
			baseField.hidden = !k || !k.base;
			if (!k || !k.base) {
				return;
			}
			baseLabel.textContent = k.baseLabel;
			baseList.innerHTML = '';
			const seen = new Set();
			for (const pr of app.lib.presets) {
				if (pr.topLevel && k.base.includes(pr.cls) && !seen.has(pr.name)) {
					seen.add(pr.name);
					baseList.append(el('option', { value: pr.name }));
				}
			}
			if (!base.value || !seen.has(base.value)) {
				base.value = k.suggest && seen.has(k.suggest) ? k.suggest : [...seen][0] || '';
			}
			if (name.value === 'NewFile.ini' || Object.values(INI_KINDS).some((o) => o.file && name.value === o.file + '.ini')) {
				name.value = k.file + '.ini';
			}
		};
		template.onchange = syncBase;
		syncBase();
		const body = el('div', {},
			el('div', { className: 'field' }, el('label', { textContent: 'Name' }), name),
			el('div', { className: 'field' }, el('label', { textContent: 'In folder' }), where),
			el('div', { className: 'field' }, el('label', { textContent: 'Start from' }), template),
			baseField);
		MS.dialogs.open(kind === 'lua' ? 'New Lua script' : 'New INI file', body, [
			{ text: 'Cancel' },
			{ text: 'Make it', primary: true, action: async () => {
				let file = name.value.trim();
				if (!file) {
					return false;
				}
				if (!new RegExp('\\.' + kind + '$', 'i').test(file)) {
					file += '.' + kind;
				}
				const path = where.value.replace(/\\/g, '/').replace(/\/+$/, '') + '/' + file;
				if (app.lockedGameFile(path)) {
					app.toast('That folder is one of the game\'s own. Pick a folder in your mod.', 'bad');
					return false;
				}
				if (await app.fs.exists(path)) {
					app.toast(path + ' is already there.', 'bad');
					return false;
				}
				if (kind === 'ini' && template.value !== 'blank') {
					return makePresetFile(path, template.value, base.value.trim());
				}
				const text = kind === 'lua' ? TEMPLATES[template.value](file.replace(/\.lua$/i, ''), preset) : TEMPLATES.ini();
				try {
					await app.fs.writeFile(path, text);
				} catch (e) {
					app.toast('Couldn\'t make ' + path + ': ' + e.message, 'bad');
					return false;
				}
				if (kind === 'lua') {
					ide.lua.updateScript(path, text);
				}
				refreshExplorer();
				if (onMade) {
					onMade(path);
				}
				await ide.openFile(path, kind === 'lua' && template.value !== 'blank' ? 5 : 1);
				if (ide.editor && kind === 'lua' && template.value !== 'blank') {
					ide.editor.setPosition({ lineNumber: template.value === 'activity' ? 4 : 5, column: 2 });
				}
				return true;
			} },
		]);
	}
	ide.newScript = newFileDialog;

	function renameDialog(path) {
		const name = el('input', { type: 'text', value: baseName(path) });
		MS.dialogs.open('Rename ' + baseName(path), el('div', {}, el('div', { className: 'field' }, el('label', { textContent: 'New name' }), name), el('p', { className: 'small', textContent: 'INI files and scripts that name it keep the old name: use Find where it\'s used to change them.' })), [
			{ text: 'Cancel' },
			{ text: 'Rename', primary: true, action: async () => {
				const target = path.slice(0, path.lastIndexOf('/') + 1) + name.value.trim();
				if (!name.value.trim() || target === path) {
					return false;
				}
				try {
					const bytes = await app.fs.readBytes(path);
					await app.fs.writeFile(target, bytes);
					await app.fs.deleteFile(path);
				} catch (e) {
					app.toast('Couldn\'t rename it: ' + e.message, 'bad');
					return false;
				}
				const tab = ide.tabs.find((t) => t.path === path);
				if (tab) {
					ide.closeTab(tab, true);
				}
				refreshExplorer();
				return true;
			} },
		]);
	}

	function deleteDialog(path) {
		MS.dialogs.open('Delete ' + baseName(path), el('p', { textContent: 'Delete ' + path + '? This can\'t be undone.' }), [
			{ text: 'Keep it' },
			{ text: 'Delete', primary: true, action: async () => {
				try {
					await app.fs.deleteFile(path);
				} catch (e) {
					app.toast('Couldn\'t delete it: ' + e.message, 'bad');
					return false;
				}
				const tab = ide.tabs.find((t) => t.path === path);
				if (tab) {
					ide.closeTab(tab, true);
				}
				refreshExplorer();
				return true;
			} },
		]);
	}

	// -- search --

	function bindSearch() {
		let timer = 0;
		const go = () => {
			clearTimeout(timer);
			timer = setTimeout(runSearch, 250);
		};
		$('searchInput').addEventListener('input', go);
		$('searchCase').addEventListener('change', go);
		$('searchRegex').addEventListener('change', go);
		$('searchMine').addEventListener('change', go);
	}

	// Every INI file and script, as Studio has them now (unsaved changes included).
	function searchableFiles() {
		const files = new Map();
		for (const doc of app.lib.docs.values()) {
			files.set(doc.path.toLowerCase(), { path: doc.path, text: doc.serialize() });
		}
		for (const s of ide.lua.scripts.values()) {
			files.set(s.path.toLowerCase(), { path: s.path, text: s.text });
		}
		for (const tab of ide.tabs) {
			if (tab.model) {
				files.set(tab.path.toLowerCase(), { path: tab.path, text: tab.model.getValue() });
			}
		}
		return [...files.values()];
	}

	function runSearch() {
		const box = $('searchResults');
		box.innerHTML = '';
		const query = $('searchInput').value;
		if (!query || !app.lib) {
			return;
		}
		let re;
		try {
			re = new RegExp($('searchRegex').checked ? query : query.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), $('searchCase').checked ? 'g' : 'gi');
		} catch (e) {
			box.append(el('div', { className: 'x-empty', textContent: 'That pattern doesn\'t work: ' + e.message }));
			return;
		}
		const mine = $('searchMine').checked;
		let hits = 0;
		let fileCount = 0;
		for (const file of searchableFiles().filter((f) => app.inScope(f.path)).sort((a, b) => a.path.localeCompare(b.path))) {
			if (mine && app.fs.isOfficial(file.path)) {
				continue;
			}
			const lines = file.text.split(/\r?\n/);
			const found = [];
			lines.forEach((line, i) => {
				re.lastIndex = 0;
				if (re.test(line)) {
					found.push({ line: i + 1, text: line.trim() });
				}
			});
			if (!found.length) {
				continue;
			}
			fileCount++;
			box.append(el('div', { className: 'pgroup', textContent: file.path + ' · ' + found.length }));
			for (const f of found.slice(0, 50)) {
				const row = el('div', { className: 'srow' }, el('span', { className: 'pline', textContent: f.line }), el('span', { textContent: f.text.slice(0, 160) }));
				row.onclick = () => ide.openFile(file.path, f.line);
				box.append(row);
			}
			hits += found.length;
			if (hits > 3000) {
				box.append(el('div', { className: 'x-empty', textContent: 'Stopped at 3000 matches.' }));
				break;
			}
		}
		$('searchCount').textContent = hits ? hits + ' in ' + fileCount + ' file' + (fileCount > 1 ? 's' : '') : 'No matches';
	}

	// The presets that name a file (a script, sprite or sound), anywhere in their blocks, with where.
	let usersFor = null;
	let usersIndex = null;
	const fileKey = (p) => p.replace(/\\/g, '/').toLowerCase().replace(/^(data|mods)\//, '').replace(/(\D)\d{3}(\.(png|bmp))$/, '$1$2');
	ide.fileUsers = function (path) {
		const lib = app.lib;
		if (!lib) {
			return [];
		}
		if (usersFor !== lib.presets) {
			usersFor = lib.presets;
			usersIndex = new Map();
			const byNode = new Map(lib.presets.map((pr) => [pr.node, pr]));
			for (const doc of lib.docs.values()) {
				for (const node of doc.walk()) {
					if (!node.line || !/\.(lua|png|bmp|wav|ogg|flac|mp3)$/i.test(node.value)) {
						continue;
					}
					let owner = node.parent;
					while (owner && !byNode.has(owner)) {
						owner = owner.parent;
					}
					if (!owner) {
						continue;
					}
					const key = fileKey(node.value);
					if (!usersIndex.has(key)) {
						usersIndex.set(key, []);
					}
					const list = usersIndex.get(key);
					if (!list.some((u) => u.preset === byNode.get(owner))) {
						list.push({ preset: byNode.get(owner), key: node.parent && node.parent !== owner && node.key === 'FilePath' ? node.parent.key : node.key, line: node.line });
					}
				}
			}
		}
		return usersIndex.get(fileKey(path)) || [];
	};

	function openUser(u) {
		ide.openFile(u.preset.doc.path, u.line);
	}

	// Search view: the presets that use a file, then every line that names it.
	function showUses(path) {
		ide.showView('search');
		$('searchInput').value = baseName(path).replace(/(\D)\d{3}(\.(png|bmp))$/i, '$1$2');
		runSearch();
		const users = ide.fileUsers(path);
		const box = $('searchResults');
		const head = [el('div', { className: 'pgroup', textContent: users.length ? 'Presets that use ' + baseName(path) + ' · ' + users.length : 'No preset uses ' + baseName(path) })];
		for (const u of users) {
			const row = el('div', { className: 'srow' }, el('span', { className: 'pline', textContent: u.preset.cls }), el('span', { textContent: u.preset.name + '  (' + u.key + ', ' + baseName(u.preset.doc.path) + ':' + u.line + ')' }));
			row.title = u.preset.doc.path;
			row.onclick = () => openUser(u);
			head.push(row);
		}
		box.prepend(...head);
	}
	ide.showUses = showUses;

	// The strip over a script or picture saying which presets use it.
	function renderUsers(tab) {
		const bar = $('codeUsers');
		bar.innerHTML = '';
		const show = tab && tab.path && (tab.kind === 'image' || /\.lua$/i.test(tab.path));
		const users = show ? ide.fileUsers(tab.path) : [];
		bar.hidden = !show;
		if (!show) {
			return;
		}
		if (!users.length) {
			bar.append(el('span', { textContent: /\.lua$/i.test(tab.path) ? 'No preset uses this script yet. Add it to a part with + New script, or as a ScriptPath line.' : 'No preset uses this picture.' }));
			return;
		}
		bar.append(el('span', { textContent: 'Used by' }));
		for (const u of users.slice(0, 8)) {
			const b = el('button', { className: 'user-chip', textContent: u.preset.name, title: u.preset.cls + ' in ' + u.preset.doc.path + ':' + u.line });
			b.onclick = () => openUser(u);
			bar.append(b);
		}
		if (users.length > 8) {
			const more = el('button', { className: 'user-chip more', textContent: 'and ' + (users.length - 8) + ' more' });
			more.onclick = () => showUses(tab.path);
			bar.append(more);
		}
	}

	// -- quick open (Ctrl+P) --

	function quickOpen() {
		const input = el('input', { type: 'text', placeholder: 'Type part of a file name…' });
		const list = el('div', { className: 'qo-list' });
		const files = searchableFiles().map((f) => f.path).filter((p) => app.inScope(p));
		let shown = [];
		let index = 0;
		const render = () => {
			const q = input.value.toLowerCase();
			shown = files.filter((p) => !q || p.toLowerCase().includes(q)).sort((a, b) => (app.fs.isOfficial(a) - app.fs.isOfficial(b)) || a.length - b.length).slice(0, 60);
			index = Math.min(index, Math.max(0, shown.length - 1));
			list.innerHTML = '';
			shown.forEach((p, i) => {
				const row = el('div', { className: 'qo-row' + (i === index ? ' sel' : '') }, el('span', { className: 'x-ext ' + extOf(p), textContent: extOf(p) }), el('span', { textContent: baseName(p) }), el('small', { textContent: p }));
				row.onclick = () => pick(p);
				list.append(row);
			});
		};
		const pick = (p) => {
			MS.dialogs.close();
			ide.openFile(p);
		};
		input.addEventListener('input', () => {
			index = 0;
			render();
		});
		input.addEventListener('keydown', (e) => {
			if (e.key === 'ArrowDown') {
				index = Math.min(shown.length - 1, index + 1);
				render();
				e.preventDefault();
			} else if (e.key === 'ArrowUp') {
				index = Math.max(0, index - 1);
				render();
				e.preventDefault();
			} else if (e.key === 'Enter' && shown[index]) {
				pick(shown[index]);
			}
		});
		MS.dialogs.open('Go to file', el('div', { className: 'qo' }, input, list), [{ text: 'Cancel' }], true);
		render();
	}
	ide.quickOpen = quickOpen;

	// -- package --

	function bindPackage() {
		$('btnCheckMod').onclick = () => checkMod();
		$('btnExportMod').onclick = () => exportMod();
	}

	function renderPackage() {
		const sel = $('packageMod');
		const current = sel.value || (app.actor && !app.fs.isOfficial(app.actor.path) ? app.actor.path.split('/')[0] : '');
		sel.innerHTML = '';
		for (const m of app.lib ? app.lib.modules.filter((x) => !x.official) : []) {
			sel.append(el('option', { value: m.name, textContent: m.title && m.title !== m.name ? m.name + ' (' + m.title + ')' : m.name }));
		}
		if (current) {
			sel.value = current;
		}
		$('btnCheckMod').disabled = !sel.options.length;
		$('btnExportMod').disabled = !sel.options.length || !app.fs.server;
	}

	// Looks the mod over: files it names that aren't there, CopyOf presets that don't exist, scripts with errors, unsaved changes.
	async function checkMod() {
		const moduleName = $('packageMod').value;
		const box = $('packageResults');
		box.innerHTML = '';
		const module = app.lib.moduleOf(moduleName + '/Index.ini');
		if (!module) {
			return;
		}
		box.append(el('div', { className: 'x-empty', textContent: 'Checking ' + moduleName + '…' }));
		const problems = [];
		for (const doc of module.docs) {
			if (doc.dirty) {
				problems.push({ path: doc.path, line: 1, severity: 'warn', message: 'Not saved yet.' });
			}
			const found = ide.ini.check(doc.path, null, doc, null).filter((p) => !/isn't in the game folder\.$/.test(p.message));
			for (const p of found) {
				problems.push({ path: doc.path, line: p.line, severity: p.severity === 'error' ? 'error' : 'warn', message: p.message });
			}
			for (const node of doc.walk()) {
				if (node.line && /\.(png|bmp|wav|ogg|flac|mp3|lua|ini)$/i.test(node.value)) {
					const file = node.value.replace(/\\/g, '/');
					let exists = await app.fs.exists(file);
					if (!exists && /\.(png|bmp)$/i.test(file)) {
						exists = await app.fs.exists(file.replace(/(\.\w+)$/, '000$1'));
					}
					if (!exists) {
						problems.push({ path: doc.path, line: node.line, severity: 'error', message: file + ' isn\'t in the game folder.' });
					} else if (file.split('/')[0].toLowerCase() !== moduleName.toLowerCase() && !app.fs.isOfficial(file)) {
						problems.push({ path: doc.path, line: node.line, severity: 'warn', message: file + ' is in another mod, which players will need too.' });
					}
				}
			}
		}
		for (const s of ide.lua.scripts.values()) {
			if (s.path.toLowerCase().startsWith(moduleName.toLowerCase() + '/')) {
				for (const p of ide.lua.check(s.path, s.text).filter((x) => x.severity === 'error')) {
					problems.push({ path: s.path, line: p.line, severity: 'error', message: p.message });
				}
			}
		}
		box.innerHTML = '';
		if (!problems.length) {
			box.append(el('div', { className: 'pempty good', textContent: moduleName + ' looks ready to share.' }));
			return;
		}
		for (const p of problems) {
			const row = el('div', { className: 'prow ' + p.severity }, el('span', { textContent: baseName(p.path) + ':' + p.line }), el('span', { textContent: p.message }));
			row.title = p.path;
			row.onclick = () => ide.openFile(p.path, p.line);
			box.append(row);
		}
	}

	// Saves, then zips the mod's folder for sharing: unzipped into another player's Mods folder, it works as it does here.
	async function exportMod() {
		const moduleName = $('packageMod').value;
		await ide.saveAll();
		const info = app.fs.modules().find((m) => m.name.toLowerCase() === moduleName.toLowerCase());
		const disk = (info ? info.home : 'Mods') + '/' + moduleName;
		let list;
		try {
			list = await app.fs.tree(disk);
		} catch (e) {
			app.toast('Couldn\'t list ' + moduleName + ': ' + e.message, 'bad');
			return;
		}
		const entries = [];
		for (const f of list) {
			entries.push({ path: moduleName + '/' + f.path, bytes: await app.fs.readBytes(moduleName + '/' + f.path) });
		}
		const zip = MS.makeZip(entries);
		app.download(zip, moduleName + '.zip');
		app.toast('Exported ' + moduleName + '.zip (' + entries.length + ' files). Players unzip it into their Mods folder.', 'good');
	}

	// ---- scripts in the inspector (shared with the designer) ----

	// The Scripts section of the inspector, in Deep mode: the part's scripts, to open, and a button to write a new one.
	ide.scriptsSection = function (insp, part, locked, section) {
		if (ide.mode !== 'deep') {
			return;
		}
		const add = el('button', { className: 'small-btn', textContent: '+ New script', title: 'Write a Lua script for this part', disabled: !!locked });
		add.onclick = () => {
			const module = (app.actor && app.actor.path.split('/')[0]) || '';
			const name = part.ent.presetName || part.name;
			newFileDialog(module + '/Scripts', 'lua', name, (path) => {
				app.edit('add script ' + baseName(path), () => {
					const node = app.ownBlock(part);
					if (node) {
						node.add('ScriptPath', path);
					}
				});
			});
		};
		const rows = section(insp, 'Scripts', add);
		const scripts = part.ent.all('ScriptPath');
		if (!scripts.length) {
			rows.append(el('div', { className: 'src', style: 'grid-column:auto', textContent: 'No scripts. A script makes the part do things: New script writes one and adds it.' }));
		}
		for (const entry of scripts) {
			const path = entry.node.value.replace(/\\/g, '/');
			const b = el('button', { className: 'small-btn', style: 'flex:1;text-align:left', textContent: path, title: 'Open in the editor' });
			b.onclick = () => ide.openFile(path);
			rows.append(el('div', { className: 'btn-row' }, b));
		}
	};

	// ---- keys ----

	function onKey(e) {
		if (ide.mode !== 'deep') {
			return;
		}
		const ctrl = e.ctrlKey || e.metaKey;
		const k = e.key.toLowerCase();
		if (ctrl && k === 's') {
			e.preventDefault();
			e.stopPropagation();
			ide.saveAll();
		} else if (ctrl && k === 'p') {
			e.preventDefault();
			e.stopPropagation();
			quickOpen();
		} else if (ctrl && k === 'j') {
			e.preventDefault();
			e.stopPropagation();
			ide.togglePanel();
		} else if (ctrl && e.shiftKey && k === 'f') {
			e.preventDefault();
			e.stopPropagation();
			ide.showView('search');
		} else if (ctrl && k === 'w' && ide.active && ide.active.kind !== 'designer') {
			e.preventDefault();
			e.stopPropagation();
			ide.closeTab(ide.active);
		} else if (ide.active && ide.active.kind !== 'designer' && !e.target.closest('#modal') && /^(z|y)$/.test(k) && ctrl && !e.target.closest('.monaco-editor')) {
			// Undo on a code tab, from outside the editor, goes to the editor.
			e.preventDefault();
			e.stopPropagation();
			if (ide.editor) {
				ide.editor.trigger('keyboard', k === 'z' && !e.shiftKey ? 'undo' : 'redo', null);
			}
		}
	}
})();
