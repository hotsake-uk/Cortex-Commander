// Cortex Mod Studio: how presets relate. One window with the preset's inheritance (what it copies and what copies it) and its resolved
// block (the lines the game ends up with after every CopyOf, each marked with the file and preset it came from).
'use strict';
window.MS = window.MS || {};

(function () {
	const $ = (id) => document.getElementById(id);
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

	const app = () => MS.app;
	const MAX_CHILDREN = 150;

	let editor = null;
	let state = null; // {preset, tab, panes, tabs}

	function disposeEditor() {
		if (editor) {
			editor.dispose();
			editor = null;
		}
		if (cy) {
			cy.destroy();
			cy = null;
		}
	}

	function openCode(path, line) {
		MS.dialogs.close();
		MS.ide.openFile(path, line);
	}

	// ---- inheritance ----

	function presetLabel(p) {
		return el('span', { className: 'rel-name' }, el('b', { textContent: p.name }), el('small', { textContent: ' ' + p.cls + ' · ' + p.module }));
	}

	function inheritanceTab(host, preset) {
		const nav = MS.nav;
		host.replaceChildren();
		const tree = el('div', { className: 'rel-tree' });
		host.append(el('p', { className: 'rel-note', textContent: 'What ' + preset.name + ' copies, above it, and what copies it, below. Click a preset to look at it; the arrow opens its lines.' }), tree);

		const row = (p, depth, kind) => {
			const r = el('div', { className: 'rel-row' + (kind === 'focus' ? ' focus' : '') });
			r.style.paddingLeft = 8 + depth * 18 + 'px';
			const open = el('button', { className: 'rel-open', title: 'Open ' + p.doc.path + (p.node.line ? ' at line ' + p.node.line : ''), textContent: '</>' });
			open.onclick = (e) => {
				e.stopPropagation();
				openCode(p.doc.path, p.node.line);
			};
			r.append(el('span', { className: 'rel-kind', textContent: kind === 'up' ? '▲' : kind === 'focus' ? '◆' : '▼' }), presetLabel(p), el('span', { className: 'spacer' }), el('small', { className: 'rel-where', textContent: p.doc.path.split('/').pop() + ':' + p.node.line }), open);
			if (kind !== 'focus') {
				r.onclick = () => render(p);
			}
			return r;
		};

		const chain = nav.ancestors(preset).reverse();
		chain.forEach((p, i) => tree.append(row(p, i, 'up')));
		tree.append(row(preset, chain.length, 'focus'));

		// What copies a preset, each with a fold that opens what copies that, and so on.
		const addKids = (container, parent, depth, seen) => {
			const all = nav.derived(parent);
			for (const p of all.slice(0, MAX_CHILDREN)) {
				if (seen.has(p)) {
					continue;
				}
				const r = row(p, depth, 'down');
				const sub = nav.derived(p);
				const holder = el('div', {});
				if (sub.length) {
					const fold = el('button', { className: 'rel-fold', textContent: '+' + sub.length, title: 'Show or hide the ' + sub.length + ' presets that copy ' + p.name });
					let open = false;
					fold.onclick = (e) => {
						e.stopPropagation();
						open = !open;
						fold.textContent = open ? '−' : '+' + sub.length;
						holder.replaceChildren();
						if (open) {
							addKids(holder, p, depth + 1, new Set([...seen, p]));
						}
					};
					r.insertBefore(fold, r.children[1]);
				}
				container.append(r, holder);
			}
			if (all.length > MAX_CHILDREN) {
				container.append(el('div', { className: 'rel-row more', textContent: '… and ' + (all.length - MAX_CHILDREN) + ' more' }));
			}
		};
		const kids = nav.derived(preset);
		if (!kids.length) {
			tree.append(el('p', { className: 'rel-note', textContent: 'Nothing copies ' + preset.name + (chain.length ? '.' : ', and it copies nothing.') }));
		} else {
			addKids(tree, preset, chain.length + 1, new Set([preset]));
		}
	}

	// ---- resolved ----

	async function resolvedTab(host, preset) {
		disposeEditor();
		host.replaceChildren(el('p', { className: 'rel-note', textContent: 'Loading the editor…' }));
		try {
			await MS.ide.loadMonaco();
		} catch (e) {
			host.replaceChildren(el('p', { className: 'rel-note', textContent: 'Couldn\'t load the editor: ' + e.message }));
			return;
		}
		if (!state || state.preset !== preset) {
			return; // another preset was picked while the editor loaded
		}
		const monaco = MS.ide.monaco;
		const resolved = MS.nav.resolved(preset);
		const note = el('p', { className: 'rel-note' });
		note.append('The lines the game ends up with for ' + preset.name + ', every CopyOf followed. A ', el('span', { className: 'rel-key own', textContent: 'bright bar' }), ' is written in this preset; a ', el('span', { className: 'rel-key', textContent: 'grey bar' }), ' is inherited, with where it comes from after the arrow. Double-click a line to open where it is written.');
		const box = el('div', { className: 'rel-editor' });
		host.replaceChildren(note, box);
		editor = monaco.editor.create(box, {
			value: resolved.lines.map((l) => l.text).join('\n'),
			language: 'ccini',
			theme: 'cortex',
			readOnly: true,
			automaticLayout: true,
			minimap: { enabled: false },
			fontSize: 12.5,
			lineNumbers: 'on',
			glyphMargin: false,
			folding: true,
			foldingStrategy: 'indentation',
			scrollBeyondLastLine: false,
			stickyScroll: { enabled: false },
			renderLineHighlight: 'line',
			domReadOnly: true,
		});
		// The "from where" note at the end of an inherited line is a style rule per distinct origin (this editor has no per-line text).
		const classes = new Map();
		const rules = [];
		const classFor = (label) => {
			if (!classes.has(label)) {
				classes.set(label, 'rel-from-' + classes.size);
					const backslash = String.fromCharCode(92);
					const text = label.split(backslash).join(backslash + backslash).split('"').join(backslash + '"');
					rules.push('.' + classes.get(label) + '::after { content: "   ←  ' + text + '"; color: #6a7486; font-style: italic; }');
			}
			return classes.get(label);
		};
		const decorations = resolved.lines.map((l, i) => ({
			range: new monaco.Range(i + 1, 1, i + 1, 1),
			options: {
				isWholeLine: true,
				linesDecorationsClassName: l.own ? 'rel-gutter-own' : l.label ? 'rel-gutter-inherited' : '',
				afterContentClassName: l.label && !l.own ? classFor(l.label) : undefined,
			},
		}));
		const style = el('style', { textContent: rules.join(' ') });
		box.append(style);
		editor.createDecorationsCollection(decorations);
		editor.onMouseDown((e) => {
			if (e.event.detail !== 2 || !e.target.position) {
				return;
			}
			const line = resolved.lines[e.target.position.lineNumber - 1];
			if (line && line.node && line.node.doc) {
				openCode(line.node.doc.path, line.node.line);
			}
		});
	}

	// ---- graph ----

	const NODE_COLOURS = { unit: '#f2b63d', item: '#7fd1ff', attachment: '#9fd88a', other: '#8d96a8', script: '#c792ea' };
	const EDGE_COLOURS = { copy: '#7a8499', part: '#f2b63d', runs: '#c792ea', names: '#ef7fb5' };
	let cy = null;
	let graphLibs = null;

	function loadScript(src) {
		return new Promise((resolve, reject) => {
			const script = document.createElement('script');
			script.src = src;
			script.onload = resolve;
			script.onerror = () => reject(new Error(src + ' is missing'));
			document.head.append(script);
		});
	}

	// Cytoscape draws the graph and its dagre layout puts it in rows; both are read on first use.
	function loadGraphLibs() {
		if (!graphLibs) {
			graphLibs = (async () => {
				// The editor's loader defines a global "define", which makes these libraries register as its modules instead of as
				// globals. They are read with it out of the way.
				const amd = window.define;
				window.define = undefined;
				try {
					await loadScript('vendor/cytoscape/cytoscape.min.js');
					await loadScript('vendor/cytoscape/dagre.min.js');
					await loadScript('vendor/cytoscape/cytoscape-dagre.js');
				} finally {
					window.define = amd;
				}
				try {
					window.cytoscape.use(window.cytoscapeDagre);
				} catch (e) {
					// the layout registered itself when it loaded
				}
			})();
			graphLibs.catch(() => {
				graphLibs = null;
			});
		}
		return graphLibs;
	}

	const GRAPH_STYLE = () => [
		{ selector: 'node', style: { shape: 'round-rectangle', width: 'label', height: 'label', padding: '9px', label: 'data(text)', 'text-wrap': 'wrap', 'text-max-width': '190px', 'text-valign': 'center', 'text-halign': 'center', 'font-size': '11px', 'line-height': 1.25, color: '#e6e9ef', 'background-color': '#171b22', 'border-width': 1.5, 'border-color': NODE_COLOURS.other } },
		{ selector: 'node[group="unit"]', style: { 'border-color': NODE_COLOURS.unit } },
		{ selector: 'node[group="item"]', style: { 'border-color': NODE_COLOURS.item } },
		{ selector: 'node[group="attachment"]', style: { 'border-color': NODE_COLOURS.attachment } },
		{ selector: 'node[group="script"]', style: { 'border-color': NODE_COLOURS.script, shape: 'cut-rectangle', 'background-color': '#1c1726' } },
		{ selector: 'node[?focus]', style: { 'border-width': 3.5, 'background-color': '#26211a' } },
		{ selector: 'node:selected', style: { 'overlay-color': '#f2b63d', 'overlay-opacity': 0.18, 'overlay-padding': 6 } },
		{ selector: 'edge', style: { 'curve-style': 'bezier', width: 1.6, 'line-color': EDGE_COLOURS.copy, 'target-arrow-color': EDGE_COLOURS.copy, 'target-arrow-shape': 'triangle', label: 'data(label)', 'font-size': '9px', color: '#9aa3b5', 'text-background-color': '#0d0f13', 'text-background-opacity': 1, 'text-background-padding': '2px', 'text-rotation': 'autorotate' } },
		{ selector: 'edge[kind="part"]', style: { 'line-color': EDGE_COLOURS.part, 'target-arrow-color': EDGE_COLOURS.part, 'line-style': 'dashed' } },
		{ selector: 'edge[kind="runs"]', style: { 'line-color': EDGE_COLOURS.runs, 'target-arrow-color': EDGE_COLOURS.runs, 'line-style': 'dotted' } },
		{ selector: 'edge[kind="names"]', style: { 'line-color': EDGE_COLOURS.names, 'target-arrow-color': EDGE_COLOURS.names, 'line-style': 'dotted' } },
	];

	async function graphTab(host, preset) {
		host.replaceChildren(el('p', { className: 'rel-note', textContent: 'Loading the graph…' }));
		try {
			await loadGraphLibs();
		} catch (e) {
			host.replaceChildren(el('p', { className: 'rel-note', textContent: 'Couldn\'t load the graph library: ' + e.message }));
			return;
		}
		if (!state || state.preset !== preset || state.tab !== 'graph') {
			return; // something else was picked while the library loaded
		}
		const options = state.graphOptions || (state.graphOptions = { depth: 1, copies: true, parts: true, scripts: true, mentions: true });
		const canvas = el('div', { className: 'rel-graph' });
		const info = el('div', { className: 'rel-ginfo', textContent: 'Click a box to see what it is. Double-click a preset to look at it, or a script to open it.' });
		const count = el('span', { className: 'rel-gcount' });

		const checks = [
			['copies', 'Copies', EDGE_COLOURS.copy, 'Presets it copies, and presets that copy it'],
			['parts', 'Parts', EDGE_COLOURS.part, 'Attachments, limbs and items it carries or is built from'],
			['scripts', 'Scripts it runs', EDGE_COLOURS.runs, 'Scripts named by ScriptPath'],
			['mentions', 'Scripts that name it', EDGE_COLOURS.names, 'Scripts that have its name in quotes, like CreateActor("..."), including from other mods'],
		].map(([key, text, colour, title]) => {
			const box = el('input', { type: 'checkbox', checked: options[key] });
			box.onchange = () => {
				options[key] = box.checked;
				draw();
			};
			return el('label', { className: 'rel-check', title }, box, el('i', { style: 'background:' + colour }), text);
		});
		const depth = el('select', { title: 'How many steps out to follow' }, ...[1, 2, 3].map((n) => el('option', { value: String(n), textContent: n === 1 ? '1 step' : n + ' steps', selected: n === options.depth })));
		depth.onchange = () => {
			options.depth = Number(depth.value);
			draw();
		};
		const fit = el('button', { className: 'ico-btn', textContent: 'Fit', title: 'Fit everything on screen' });
		fit.onclick = () => cy && cy.fit(undefined, 24);
		const legend = el('span', { className: 'rel-legend' }, ...Object.entries({ unit: 'unit', item: 'item', attachment: 'attachment', script: 'script' }).map(([key, text]) => el('span', {}, el('i', { className: 'sq', style: 'border-color:' + NODE_COLOURS[key] }), text)));
		host.replaceChildren(el('div', { className: 'rel-gtools' }, ...checks, depth, fit, el('span', { className: 'spacer' }), legend, count), canvas, info);

		let byId = new Map();
		const focusOn = (node) => {
			if (node.preset) {
				render(node.preset, 'graph');
			} else {
				openCode(node.path, node.line);
			}
		};
		const select = (node) => {
			const buttons = [];
			if (node.preset) {
				buttons.push(el('button', { className: 'small-btn primary', textContent: 'Look at this one', onclick: () => focusOn(node) }));
				buttons.push(el('button', { className: 'small-btn', textContent: 'Inheritance', onclick: () => render(node.preset, 'inheritance') }));
			}
			buttons.push(el('button', { className: 'small-btn', textContent: node.preset ? 'Open its lines' : 'Open the script', onclick: () => openCode(node.path, node.line) }));
			info.replaceChildren(el('b', { textContent: node.label }), el('span', { textContent: ' ' + node.sub + (node.preset ? ' · ' + node.path.split('/').pop() + ':' + node.line : '') }), el('span', { className: 'spacer' }), ...buttons);
		};

		const draw = () => {
			if (cy) {
				cy.destroy();
				cy = null;
			}
			const model = MS.nav.graph(preset, options);
			byId = new Map(model.nodes.map((n) => [n.id, n]));
			const elements = model.nodes.map((n) => ({ data: { id: n.id, text: n.label + '\n' + n.sub, group: n.group, focus: !!n.focus } })).concat(model.edges.map((e) => ({ data: { id: e.id, source: e.source, target: e.target, kind: e.kind, label: e.label } })));
			cy = window.cytoscape({
				container: canvas,
				elements,
				style: GRAPH_STYLE(),
				layout: { name: 'dagre', rankDir: 'LR', nodeSep: 16, rankSep: 80, edgeSep: 10, animate: false, fit: true, padding: 24 },
				wheelSensitivity: 0.25,
				minZoom: 0.12,
				maxZoom: 2.5,
			});
			cy.on('tap', 'node', (e) => select(byId.get(e.target.id())));
			cy.on('dbltap', 'node', (e) => focusOn(byId.get(e.target.id())));
			const notes = [model.nodes.length + ' boxes'];
			if (model.moreCopies) {
				notes.push(model.moreCopies + ' more copies not drawn');
			}
			if (model.truncated) {
				notes.push('stopped at ' + model.nodes.length + ': use fewer steps or switch some off');
			}
			count.textContent = notes.join(' · ');
		};
		if (options.mentions && !MS.ide.scriptsLoaded) {
			count.textContent = 'Reading scripts…';
			await MS.ide.ensureScripts();
			if (!state || state.preset !== preset || state.tab !== 'graph') {
				return;
			}
		}
		draw();
	}

	// ---- the window ----

	function render(preset, tab) {
		state = state || {};
		state.preset = preset;
		state.tab = tab || state.tab || 'inheritance';
		const { title, panes, tabs } = state;
		title.textContent = preset.name + ' · ' + preset.cls + ' · ' + preset.module;
		for (const [id, button] of Object.entries(tabs)) {
			button.classList.toggle('sel', id === state.tab);
			panes[id].hidden = id !== state.tab;
		}
		disposeEditor();
		if (state.tab === 'inheritance') {
			inheritanceTab(panes.inheritance, preset);
		} else if (state.tab === 'graph') {
			graphTab(panes.graph, preset);
		} else {
			resolvedTab(panes.resolved, preset);
		}
	}

	// preset: a preset from the library. tab: "inheritance" or "resolved".
	function open(preset, tab) {
		if (!preset || !MS.nav) {
			return;
		}
		disposeEditor();
		const title = el('div', { className: 'rel-title' });
		const tabs = {
			inheritance: el('button', { textContent: 'Inheritance', title: 'What it copies and what copies it' }),
			graph: el('button', { textContent: 'Graph', title: 'A picture of the presets and scripts around it' }),
			resolved: el('button', { textContent: 'Resolved', title: 'The lines the game ends up with, and where each comes from' }),
		};
		const panes = { inheritance: el('div', { className: 'rel-pane' }), graph: el('div', { className: 'rel-pane' }), resolved: el('div', { className: 'rel-pane' }) };
		state = { title, tabs, panes };
		for (const [id, button] of Object.entries(tabs)) {
			button.onclick = () => render(state.preset, id);
		}
		const body = el('div', { className: 'rel' }, title, el('div', { className: 'rel-tabs' }, tabs.inheritance, tabs.graph, tabs.resolved), panes.inheritance, panes.graph, panes.resolved);
		MS.dialogs.open('Relationships', body, [{ text: 'Close', action: () => { disposeEditor(); state = null; } }], true);
		$('modalCard').classList.add('hist-card');
		render(preset, tab || 'inheritance');
	}

	MS.relations = { open };
})();
