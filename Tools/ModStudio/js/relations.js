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
			resolved: el('button', { textContent: 'Resolved', title: 'The lines the game ends up with, and where each comes from' }),
		};
		const panes = { inheritance: el('div', { className: 'rel-pane' }), resolved: el('div', { className: 'rel-pane' }) };
		state = { title, tabs, panes };
		for (const [id, button] of Object.entries(tabs)) {
			button.onclick = () => render(state.preset, id);
		}
		const body = el('div', { className: 'rel' }, title, el('div', { className: 'rel-tabs' }, tabs.inheritance, tabs.resolved), panes.inheritance, panes.resolved);
		MS.dialogs.open('Relationships', body, [{ text: 'Close', action: () => { disposeEditor(); state = null; } }], true);
		$('modalCard').classList.add('hist-card');
		render(preset, tab || 'inheritance');
	}

	MS.relations = { open };
})();
