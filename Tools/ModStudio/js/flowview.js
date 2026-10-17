// Cortex Mod Studio: the flow view, where a mission's graph (flowmodel.js) is drawn and edited as nodes and wires, like a blueprint.
//   Nodes      a coloured title (blue events, amber stages, purple flow, green actions) and their settings under it. Drag the title to move a node.
//   Wires      come out of a node's exits on its right (then, true and false, started, done, time out, next) and into a node's entrance on its left.
//              Drag from an exit to a node to wire it; drop on empty space to pick a new node there, wired up. Click a wire and press Delete to cut it.
//   The rest   right-click empty space to add a node; Delete takes the picked node out (the chain either side joined up); Ctrl+Z and Ctrl+Y undo
//              and redo; the wheel zooms, dragging empty space moves the view; F fits everything in.
// It edits through the hooks it is given (change, snapshot), so the owner decides where the graph lives and how it is saved.
'use strict';
window.MS = window.MS || {};

(function () {
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
	const SVG = 'http://www.w3.org/2000/svg';
	const PIN_LABELS = { then: 'then', true: 'true', false: 'false', started: 'when it starts', done: 'when done', timeout: 'if time runs out', next: 'next stage', taken: 'when taken', retaken: 'when taken back' };
	const PIN_COLORS = { then: '#d8dee9', true: '#9bc53d', false: '#e55934', started: '#5bc0eb', done: '#9bc53d', timeout: '#e55934', next: '#f2b63d', taken: '#9bc53d', retaken: '#e55934' };
	const VALUE_KINDS = [['num', 'a number'], ['var', 'a variable'], ['setting', 'a scenario setting'], ['difficulty', 'the difficulty (0.5 to 2)'], ['time', 'seconds since it began']];
	const TEAM_NAMES = ['Team 1', 'Team 2', 'Team 3', 'Team 4'];

	// How many characters wide a text box is drawn, from what it holds, so the node grows to show it.
	const sizeFor = (v) => Math.max(12, Math.min(48, String(v === undefined || v === null ? '' : v).length + 2));

	class FlowView {
		// hooks: graph() the graph shown; change(label, fn) edits it (fn changes the graph in place); snapshot() / commit(label, before) for edits made live
		// (a drag); undo(); redo(); readOnly; areas() names on the map; pickUnit(cb); pickItem(cb); problems() for the badges; selected(ids) told when the
		// pick changes; openFn(name) jumps to one of your functions; state, an object kept between redraws (the view's place and zoom).
		constructor(hooks) {
			this.hooks = hooks;
			this.state = hooks.state;
			this.state.view = this.state.view || { x: 40, y: 40, z: 0.8 };
			this.picked = new Set(this.state.picked || []);
			this.pickedWire = null;
			this.world = el('div', { className: 'fv-world' });
			this.svg = document.createElementNS(SVG, 'svg');
			this.svg.classList.add('fv-wires');
			this.world.append(this.svg);
			this.root = el('div', { className: 'fv', tabIndex: 0 }, this.world);
			this.nodeEls = new Map();
			this.bind();
		}

		get g() {
			return this.hooks.graph();
		}

		place() {
			const v = this.state.view;
			this.world.style.transform = 'translate(' + v.x + 'px, ' + v.y + 'px) scale(' + v.z + ')';
		}

		toWorld(clientX, clientY) {
			const r = this.root.getBoundingClientRect();
			const v = this.state.view;
			return { x: (clientX - r.left - v.x) / v.z, y: (clientY - r.top - v.y) / v.z };
		}

		fit() {
			const g = this.g;
			const nodes = this.only ? g.nodes.filter((n) => this.only.has(n.id)) : g.nodes;
			if (!nodes.length) {
				return;
			}
			const r = this.root.getBoundingClientRect();
			let minX = Infinity;
			let minY = Infinity;
			let maxX = -Infinity;
			let maxY = -Infinity;
			for (const n of nodes) {
				const e = this.nodeEls.get(n.id);
				minX = Math.min(minX, n.x);
				minY = Math.min(minY, n.y);
				maxX = Math.max(maxX, n.x + (e ? e.offsetWidth : 280));
				maxY = Math.max(maxY, n.y + (e ? e.offsetHeight : 120));
			}
			const z = Math.max(0.2, Math.min(1.2, Math.min((r.width - 40) / (maxX - minX || 1), (r.height - 40) / (maxY - minY || 1))));
			this.state.view = { z, x: 20 - minX * z + (r.width - 40 - (maxX - minX) * z) / 2, y: 20 - minY * z };
			this.place();
		}

		// ---- drawing ----

		render() {
			const g = this.g;
			const problems = this.hooks.problems ? this.hooks.problems() : [];
			for (const e of this.nodeEls.values()) {
				e.remove();
			}
			this.nodeEls.clear();
			// only: when set, the ids of the nodes to draw (a stage picked in the list beside the graph); the rest, and their wires, are left out
			for (const n of this.only ? g.nodes.filter((x) => this.only.has(x.id)) : g.nodes) {
				const e = this.nodeEl(n, problems.filter((p) => p.id === n.id));
				this.world.append(e);
				this.nodeEls.set(n.id, e);
			}
			this.place();
			requestAnimationFrame(() => this.wires());
		}

		nodeEl(n, problems) {
			const spec = F().KINDS[n.kind];
			const category = spec ? spec.category : 'action';
			const e = el('div', { className: 'fv-node fv-' + category + (this.picked.has(n.id) ? ' picked' : '') + (problems.some((p) => p.level === 'error') ? ' has-error' : problems.length ? ' has-warn' : '') });
			e.dataset.id = n.id;
			e.style.left = n.x + 'px';
			e.style.top = n.y + 'px';
			const title = el('div', { className: 'fv-title' },
				spec && spec.entrance ? el('span', { className: 'fv-in', title: 'Wire into this' }) : null,
				el('span', { className: 'fv-name', textContent: (category === 'stage' && n.p && n.p.name ? n.p.name + ' · ' : '') + (spec ? spec.title : 'Unknown: ' + n.kind) }),
				problems.length ? el('span', { className: 'fv-badge', textContent: problems.length, title: problems.map((p) => p.message).join('\n') }) : null);
			e.append(title);
			if (n.note) {
				e.append(el('div', { className: 'fv-note', textContent: n.note }));
			}
			const body = el('div', { className: 'fv-body' });
			if (spec) {
				for (const [key, type, label, , options] of spec.params) {
					if (!F().relevant(spec, n.p, key)) {
						continue; // a setting these settings make no use of (a group's name when a unit is picked)
					}
					body.append(this.field(type, label, (n.p || {})[key], (value) => this.edit('change ' + (label || key), (g) => {
						F().find(g, n.id).p[key] = value;
					}), options, { kind: n.kind, key }));
				}
				if (n.kind === 'branch') {
					body.append(this.conditions(n));
				}
				if (spec.lists && spec.lists.includes('lines')) {
					body.append(this.rows(n, 'lines', '+ Line'));
				}
			}
			e.append(body);
			if (spec) {
				const exits = el('div', { className: 'fv-exits' });
				for (const pin of spec.exits) {
					const out = el('div', { className: 'fv-exit' }, el('span', { textContent: PIN_LABELS[pin] || pin }), el('span', { className: 'fv-pin', title: 'Drag to wire ' + (PIN_LABELS[pin] || pin) }));
					out.dataset.pin = pin;
					out.querySelector('.fv-pin').style.borderColor = PIN_COLORS[pin] || '#ccc';
					if ((n.out || {})[pin]) {
						out.querySelector('.fv-pin').style.background = PIN_COLORS[pin] || '#ccc';
					}
					exits.append(out);
				}
				e.append(exits);
			}
			if (n.kind === 'call') {
				body.append(el('button', { className: 'tb', textContent: 'Edit the code', onclick: () => this.hooks.openFn && this.hooks.openFn((n.p || {}).fn) }));
			}
			return e;
		}

		// A list of rows, each with the same settings (a dialogue's lines: what is said, and for how long).
		rows(n, listName, addLabel) {
			const F_ = F();
			const params = F_.LIST_ROWS[listName] || [];
			const box = el('div', { className: 'fv-conds' });
			((n.lists && n.lists[listName]) || []).forEach((row, i) => {
				box.append(el('div', { className: 'fv-cond' }, el('span', { className: 'fv-and', textContent: String(i + 1) }),
					...params.map(([key, type, label, , options]) => this.field(type, label, row[key], (value) => this.edit('change a line', (g) => {
						F_.find(g, n.id).lists[listName][i][key] = value;
					}), options, { kind: n.kind + ':' + listName, key })),
					el('button', { className: 'ico-btn', textContent: '×', title: 'Take it out', disabled: this.hooks.readOnly, onclick: () => this.edit('take a line out', (g) => {
						F_.find(g, n.id).lists[listName].splice(i, 1);
					}) })));
			});
			box.append(el('button', { className: 'tb fv-add-cond', textContent: addLabel, disabled: this.hooks.readOnly, onclick: () => this.edit('add a line', (g) => {
				const node = F_.find(g, n.id);
				node.lists = node.lists || {};
				node.lists[listName] = node.lists[listName] || [];
				node.lists[listName].push(Object.assign({}, ...params.map(([key, , , def]) => ({ [key]: def }))));
			}) }));
			return box;
		}

		// The settings of a branch: its conditions, each a kind and its settings, all of which must hold.
		conditions(n) {
			const F_ = F();
			const box = el('div', { className: 'fv-conds' });
			const conds = (n.lists && n.lists.conds) || [];
			conds.forEach((c, i) => {
				const spec = F_.CONDS[c.kind];
				const kind = el('select', { className: 'ed-input', disabled: this.hooks.readOnly }, ...Object.entries(F_.CONDS).map(([k, v]) => el('option', { value: k, textContent: v.title, selected: k === c.kind })));
				kind.onchange = () => this.edit('change a condition', (g) => {
					F_.find(g, n.id).lists.conds[i] = F_.condDefaults(kind.value);
				});
				const row = el('div', { className: 'fv-cond' }, el('span', { className: 'fv-and', textContent: i ? 'and' : 'if' }), kind,
					...(spec ? spec.params.filter(([key]) => !spec.relevant || spec.relevant(c, key)).map(([key, type, label, , options]) => this.field(type, label, c[key], (value) => this.edit('change a condition', (g) => {
						F_.find(g, n.id).lists.conds[i][key] = value;
					}), options, { kind: 'cond:' + c.kind, key })) : []),
					el('button', { className: 'ico-btn', textContent: '×', title: 'Take this condition out', disabled: this.hooks.readOnly, onclick: () => this.edit('take a condition out', (g) => {
						F_.find(g, n.id).lists.conds.splice(i, 1);
					}) }));
				box.append(row);
			});
			box.append(el('button', { className: 'tb fv-add-cond', textContent: '+ Condition', disabled: this.hooks.readOnly, onclick: () => this.edit('add a condition', (g) => {
				F_.addCondition(F_.find(g, n.id), 'funds');
			}) }));
			return box;
		}

		// One setting, as a small input that edits the graph when it changes. where: { kind, key }, which setting it is (for what it offers).
		field(type, label, value, set, options, where) {
			const ro = this.hooks.readOnly;
			const F_ = F();
			let input;
			const select = (options, current, parse) => {
				const s = el('select', { className: 'ed-input', disabled: ro }, ...options.map(([v, t]) => el('option', { value: String(v), textContent: t, selected: String(v) === String(current) })));
				s.onchange = () => set(parse ? parse(s.value) : s.value);
				return s;
			};
			if (type === 'team') {
				input = select([[-1, 'any team'], ...TEAM_NAMES.map((t, i) => [i, t])], value, Number);
			} else if (type === 'area' && where && F_.stacks(where.kind, where.key)) {
				// one area or several: the step runs in each
				const names = this.hooks.areas ? this.hooks.areas() : [];
				const picked = F_.idsOf(value);
				const rest = names.filter((x) => !picked.includes(x));
				const add = el('select', { className: 'ed-input fv-area', disabled: ro || !rest.length },
					el('option', { value: '', textContent: picked.length ? '+ another area' : '(pick an area)' }), ...rest.map((x) => el('option', { value: x, textContent: x })));
				add.onchange = () => set(picked.length ? picked.concat(add.value) : add.value);
				add.onfocus = () => this.hooks.highlight && this.hooks.highlight(picked);
				const one = (x) => el('span', { className: 'fv-chip' + (names.includes(x) ? '' : ' gone'), title: names.includes(x) ? x : 'The scene has no area called ' + x }, x,
					ro ? null : el('button', { className: 'ico-btn', textContent: '×', title: 'Take it out', onclick: (e) => {
						e.preventDefault();
						const left = picked.filter((y) => y !== x);
						set(left.length > 1 ? left : left[0] || '');
					} }));
				input = el('div', { className: 'fv-chips' }, ...picked.map(one), add);
			} else if (type === 'area') {
				const names = this.hooks.areas ? this.hooks.areas() : [];
				const options = [...new Set([...names, ...(value ? [value] : [])])];
				input = select([['', '(pick an area)'], ...options.map((n) => [n, n])], value || '');
				input.classList.add('fv-area');
				input.onfocus = () => this.hooks.highlight && this.hooks.highlight(value ? [value] : []);
			} else if (type === 'op') {
				input = select(F_.OPS, value || '>=');
			} else if (type === 'player') {
				input = select([['all', 'all players'], [0, 'player 1'], [1, 'player 2'], [2, 'player 3'], [3, 'player 4']], value === undefined ? 'all' : value, (v) => (v === 'all' ? 'all' : Number(v)));
			} else if (type === 'mode') {
				input = select(F_.BATTLE_MODES, value, Number);
			} else if (type === 'stage') {
				// the graph's stages, and the ones the script runs in its own code
				const stages = F_.stages(this.g);
				const coded = this.hooks.codeStages ? this.hooks.codeStages() : [];
				const option = (v, t) => el('option', { value: v, textContent: t, selected: v === (value || '') });
				input = el('select', { className: 'ed-input', disabled: ro },
					option('', stages.length || coded.length ? '(pick a stage)' : '(no stages yet: add one)'),
					stages.length ? el('optgroup', { label: 'Mission stages' }, ...stages.map((s) => option(s.id, (s.p && s.p.name) || s.id))) : null,
					coded.length ? el('optgroup', { label: 'In the script' }, ...coded.map((c) => option('code:' + c.name, c.label))) : null);
				input.onchange = () => set(input.value);
			} else if (type === 'captures') {
				// one or more: a chip for each, and a list to add another (or all the rest)
				const points = this.g.nodes.filter((x) => x.kind === 'capture');
				const ids = F_.idsOf(value);
				const nameOf = (id) => {
					const x = F_.find(this.g, id);
					return x ? (x.p && x.p.name) || id : '(not there any more)';
				};
				const rest = points.filter((x) => !ids.includes(x.id));
				const add = el('select', { className: 'ed-input', disabled: ro || !rest.length },
					el('option', { value: '', textContent: !points.length ? '(add a capture point first)' : !rest.length ? '(all picked)' : ids.length ? '+ add another' : '(pick capture points)' }),
					rest.length > 1 ? el('option', { value: '*', textContent: '+ all of them' }) : null,
					...rest.map((x) => el('option', { value: x.id, textContent: nameOf(x.id) })));
				add.onchange = () => set(add.value === '*' ? ids.concat(rest.map((x) => x.id)) : ids.concat(add.value));
				input = el('div', { className: 'fv-chips' }, ...ids.map((id) => el('span', { className: 'fv-chip' + (F_.find(this.g, id) ? '' : ' gone') }, nameOf(id),
					ro ? null : el('button', { className: 'ico-btn', textContent: '×', title: 'Take it out', onclick: (e) => {
						e.preventDefault();
						set(ids.filter((x) => x !== id));
					} }))), add);
			} else if (type === 'capture') {
				const points = this.g.nodes.filter((x) => x.kind === 'capture');
				input = select([['', points.length ? '(pick a capture point)' : '(add a capture point first)'], ...points.map((x) => [x.id, (x.p && x.p.name) || x.id])], value || '');
			} else if (type === 'choice') {
				input = select(options || [], value === undefined ? (options && options[0] ? options[0][0] : '') : value);
			} else if (type === 'value') {
				input = this.valueInput(value, set);
			} else if (type === 'lua') {
				// your own code: its first lines, and a button to write it in the code editor (with the script's completions)
				const code = String(value || '');
				const lines = code.split('\n').filter((l) => l.trim());
				input = el('div', { className: 'fv-code' },
					el('pre', { className: 'fv-code-peek', textContent: lines.length ? lines.slice(0, 4).join('\n') + (lines.length > 4 ? '\n…' : '') : '-- no code yet' }),
					el('button', { className: 'tb', textContent: ro ? 'Read the code…' : 'Write the code…', onclick: () => (this.hooks.editCode ? this.hooks.editCode(code, set, ro) : null) }));
			} else if (type === 'bool') {
				input = el('input', { type: 'checkbox', checked: !!value, disabled: ro });
				input.onchange = () => set(input.checked);
			} else if (type === 'number') {
				input = el('input', { type: 'number', step: 'any', className: 'ed-input num', value: value === undefined ? '' : value, disabled: ro });
				input.onchange = () => set(Number(input.value) || 0);
			} else if (type === 'preset' || type === 'item') {
				const name = String(value || '').split('|')[1] || '(pick one)';
				input = el('button', { className: 'tb', textContent: name, disabled: ro, onclick: () => (type === 'preset' ? this.hooks.pickUnit : this.hooks.pickItem)((chosen) => set(chosen.cls + '|' + chosen.name + '|' + chosen.module)) });
			} else {
				// a name of something: type it, or search what there is of that kind (the game's presets, names used in this mission, the script's values)
				const items = where && this.hooks.suggests ? this.hooks.suggests(where.kind, where.key) : null;
				if (items) {
					input = this.combo(value, items, set, ro);
				} else {
					input = el('input', { type: 'text', className: 'ed-input' + (type === 'fn' ? ' mono' : ''), value: value === undefined ? '' : value, size: sizeFor(value), disabled: ro });
					input.onchange = () => set(input.value);
				}
			}
			input.addEventListener('pointerdown', (e) => e.stopPropagation());
			input.addEventListener('keydown', (e) => e.stopPropagation());
			return el('label', { className: 'fv-field' + (type === 'bool' ? ' fv-bool' : '') }, label ? el('span', { textContent: label }) : null, input);
		}

		// A text box that offers what there is to pick as you type: items() gives [{ v: the value, t: what it is called, sub: a note, sec: its heading }].
		// Anything typed is kept too (a group or a variable can be a new name). Up and down move through the list, Enter picks, Escape closes it.
		combo(value, items, set, ro) {
			const start = value === undefined || value === null ? '' : String(value);
			const input = el('input', { type: 'text', className: 'ed-input fv-combo', value: start, size: sizeFor(start), disabled: ro, placeholder: 'type to search', spellcheck: false });
			let pop = null;
			let rows = [];
			let shown = [];
			let at = -1;
			let done = false;
			const close = () => {
				if (pop) {
					pop.remove();
					pop = null;
				}
			};
			const commit = (text) => {
				if (done) {
					return;
				}
				close();
				if (text !== start) {
					done = true;
					set(text);
				}
			};
			const mark = () => rows.forEach((r, i) => {
				r.classList.toggle('on', i === at);
				if (i === at) {
					r.scrollIntoView({ block: 'nearest' });
				}
			});
			const open = () => {
				close();
				const list = items() || [];
				const q = input.value.trim().toLowerCase();
				// a value picked already shows the whole list (to look through); typing narrows it
				const words = list.some((it) => it.v === input.value) ? [] : q.split(/\s+/).filter(Boolean);
				shown = list.filter((it) => {
					const hay = (it.t + ' ' + (it.sub || '') + ' ' + it.v).toLowerCase();
					return words.every((w) => hay.includes(w));
				}).slice(0, 400);
				if (!shown.length) {
					return;
				}
				pop = el('div', { className: 'fv-suggest' });
				rows = [];
				let sec = null;
				shown.forEach((it) => {
					if (it.sec && it.sec !== sec) {
						sec = it.sec;
						pop.append(el('div', { className: 'fv-suggest-sec', textContent: sec }));
					}
					const row = el('div', { className: 'fv-suggest-item' + (it.v === input.value ? ' cur' : ''), title: it.v }, el('span', { textContent: it.t }), it.sub ? el('small', { textContent: it.sub }) : null);
					row.addEventListener('pointerdown', (e) => {
						e.preventDefault();
						input.value = it.v;
						commit(it.v);
					});
					rows.push(row);
					pop.append(row);
				});
				const r = input.getBoundingClientRect();
				Object.assign(pop.style, { left: Math.min(r.left, innerWidth - 300) + 'px', top: r.bottom + 2 + 'px', minWidth: Math.max(240, r.width) + 'px' });
				document.body.append(pop);
				const pr = pop.getBoundingClientRect();
				if (pr.bottom > innerHeight - 8) {
					pop.style.top = Math.max(8, r.top - 2 - pr.height) + 'px';
				}
			};
			input.addEventListener('focus', () => {
				at = -1;
				open();
			});
			input.addEventListener('input', () => {
				at = -1;
				input.size = sizeFor(input.value);
				open();
			});
			input.addEventListener('blur', () => commit(input.value));
			input.addEventListener('keydown', (e) => {
				if (e.key === 'Escape') {
					close();
				} else if (e.key === 'Enter') {
					e.preventDefault();
					if (pop && at >= 0) {
						input.value = shown[at].v;
					}
					commit(input.value);
				} else if (pop && (e.key === 'ArrowDown' || e.key === 'ArrowUp')) {
					e.preventDefault();
					at = (at + (e.key === 'ArrowDown' ? 1 : -1) + shown.length) % shown.length;
					mark();
				}
			});
			return input;
		}

		// A value: what it is (a number, a variable, a scenario setting, the difficulty, the time), the number or name, and whether it is multiplied by
		// the difficulty too.
		valueInput(value, set) {
			const ro = this.hooks.readOnly;
			const v = value && typeof value === 'object' ? value : { k: 'num', n: Number(value) || 0 };
			const box = el('span', { className: 'fv-value' });
			const kind = el('select', { className: 'ed-input', disabled: ro }, ...VALUE_KINDS.map(([k, t]) => el('option', { value: k, textContent: t, selected: k === (v.k || 'num') })));
			kind.onchange = () => set(Object.assign({}, v, { k: kind.value }));
			box.append(kind);
			if ((v.k || 'num') === 'num') {
				const n = el('input', { type: 'number', step: 'any', className: 'ed-input num', value: v.n === undefined ? '' : v.n, disabled: ro });
				n.onchange = () => set(Object.assign({}, v, { n: Number(n.value) || 0 }));
				box.append(n);
			} else if (v.k === 'var' || v.k === 'setting') {
				const items = v.k === 'var' && this.hooks.suggests ? this.hooks.suggests('value', 'name') : null;
				const name = items ? this.combo(v.name || '', items, (text) => set(Object.assign({}, v, { name: text.trim() })), ro)
					: el('input', { type: 'text', className: 'ed-input', value: v.name || '', placeholder: v.k === 'var' ? 'variable' : 'setting name', disabled: ro });
				if (!items) {
					name.onchange = () => set(Object.assign({}, v, { name: name.value.trim() }));
				}
				box.append(name);
			}
			const times = el('input', { type: 'checkbox', checked: !!v.x, disabled: ro, title: 'Multiply it by the difficulty (0.5 at the easiest, 2 at the hardest)' });
			times.onchange = () => set(Object.assign({}, v, { x: times.checked }));
			box.append(el('label', { className: 'fv-times', title: times.title }, times, '× difficulty'));
			for (const c of box.querySelectorAll('input, select')) {
				c.addEventListener('pointerdown', (e) => e.stopPropagation());
				c.addEventListener('keydown', (e) => e.stopPropagation());
			}
			return box;
		}

		// The wires: a curve from each exit to the node it goes to.
		wires(temp) {
			const g = this.g;
			while (this.svg.firstChild) {
				this.svg.firstChild.remove();
			}
			let maxX = 2000;
			let maxY = 2000;
			const curve = (x1, y1, x2, y2, color, wireKey, thick) => {
				const dx = Math.max(40, Math.abs(x2 - x1) / 2);
				const path = document.createElementNS(SVG, 'path');
				path.setAttribute('d', 'M' + x1 + ' ' + y1 + ' C' + (x1 + dx) + ' ' + y1 + ', ' + (x2 - dx) + ' ' + y2 + ', ' + x2 + ' ' + y2);
				path.setAttribute('stroke', color);
				path.setAttribute('stroke-width', thick ? 4 : 2.2);
				path.setAttribute('fill', 'none');
				if (wireKey) {
					path.classList.add('fv-wire');
					path.dataset.wire = wireKey;
					if (this.pickedWire === wireKey) {
						path.classList.add('picked');
					}
				}
				this.svg.append(path);
				maxX = Math.max(maxX, x1, x2);
				maxY = Math.max(maxY, y1, y2);
			};
			for (const n of g.nodes) {
				const from = this.nodeEls.get(n.id);
				if (!from) {
					continue;
				}
				for (const [pin, to] of Object.entries(n.out || {})) {
					const target = to && this.nodeEls.get(to);
					const exit = from.querySelector('.fv-exit[data-pin="' + pin + '"] .fv-pin');
					if (!target || !exit) {
						continue;
					}
					const a = this.pinPoint(exit, n);
					const tn = F().find(g, to);
					curve(a.x, a.y, tn.x, tn.y + 16, PIN_COLORS[pin] || '#ccc', n.id + ':' + pin, pin === 'next');
				}
			}
			if (temp) {
				curve(temp.x1, temp.y1, temp.x2, temp.y2, temp.color, null, false);
			}
			this.svg.setAttribute('width', maxX + 400);
			this.svg.setAttribute('height', maxY + 400);
		}

		// Where an exit's dot is, in the graph's own coordinates.
		pinPoint(pinEl, n) {
			const nodeEl = this.nodeEls.get(n.id);
			let x = pinEl.offsetLeft + pinEl.offsetWidth / 2;
			let y = pinEl.offsetTop + pinEl.offsetHeight / 2;
			for (let p = pinEl.offsetParent; p && p !== nodeEl; p = p.offsetParent) {
				x += p.offsetLeft;
				y += p.offsetTop;
			}
			return { x: n.x + x, y: n.y + y };
		}

		// ---- editing ----

		edit(label, fn) {
			if (this.hooks.readOnly) {
				return;
			}
			this.hooks.change(label, fn);
		}

		pick(ids) {
			this.picked = new Set(ids);
			this.state.picked = [...this.picked];
			for (const [id, e] of this.nodeEls) {
				e.classList.toggle('picked', this.picked.has(id));
			}
			if (this.hooks.selected) {
				this.hooks.selected([...this.picked]);
			}
		}

		// The menu of nodes to add: by category, with a search box. only limits the kinds (those a wire can go to).
		palette(clientX, clientY, only, onPick) {
			const F_ = F();
			const close = () => {
				menu.remove();
				document.removeEventListener('pointerdown', away, true);
			};
			const search = el('input', { type: 'search', className: 'ed-input', placeholder: 'Search nodes…' });
			const list = el('div', { className: 'fv-palette-list' });
			const draw = () => {
				const q = search.value.trim().toLowerCase();
				list.replaceChildren();
				for (const [cat, title] of F_.CATEGORIES) {
					const kinds = F_.ORDER.filter((k) => F_.KINDS[k].category === cat && (!only || only(F_.KINDS[k])) && (!q || F_.KINDS[k].title.toLowerCase().includes(q)));
					if (!kinds.length) {
						continue;
					}
					list.append(el('div', { className: 'fv-palette-head fv-' + cat, textContent: title }));
					for (const k of kinds) {
						list.append(el('button', { className: 'fv-palette-item', textContent: F_.KINDS[k].title, onclick: () => {
							close();
							onPick(k);
						} }));
					}
				}
			};
			search.oninput = draw;
			search.onkeydown = (e) => {
				e.stopPropagation();
				if (e.key === 'Enter') {
					const first = list.querySelector('.fv-palette-item');
					if (first) {
						first.click();
					}
				} else if (e.key === 'Escape') {
					close();
				}
			};
			const menu = el('div', { className: 'fv-palette' }, search, list);
			menu.style.left = Math.min(clientX, window.innerWidth - 300) + 'px';
			menu.style.top = Math.min(clientY, window.innerHeight - 380) + 'px';
			document.body.append(menu);
			draw();
			search.focus();
			const away = (e) => {
				if (!menu.contains(e.target)) {
					close();
				}
			};
			setTimeout(() => document.addEventListener('pointerdown', away, true), 0);
		}

		addAt(kindName, at, wireFrom) {
			this.edit('add a node', (g) => {
				const n = F().addNode(g, kindName, at.x, at.y);
				if (n && n.kind === 'branch') {
					F().addCondition(n, 'funds');
				}
				if (n && wireFrom) {
					const why = F().connect(g, wireFrom.id, wireFrom.pin, n.id);
					if (why && MS.app) {
						MS.app.toast(why, 'bad');
					}
				}
				if (n) {
					this.picked = new Set([n.id]);
					this.state.picked = [n.id];
				}
			});
		}

		bind() {
			const root = this.root;
			root.addEventListener('wheel', (e) => {
				e.preventDefault();
				const r = root.getBoundingClientRect();
				const v = this.state.view;
				const before = this.toWorld(e.clientX, e.clientY);
				v.z = Math.max(0.2, Math.min(2, v.z * (e.deltaY < 0 ? 1.1 : 1 / 1.1)));
				v.x = e.clientX - r.left - before.x * v.z;
				v.y = e.clientY - r.top - before.y * v.z;
				this.place();
			}, { passive: false });
			root.addEventListener('contextmenu', (e) => {
				e.preventDefault();
				if (this.hooks.readOnly || e.target.closest('.fv-node')) {
					return;
				}
				const at = this.toWorld(e.clientX, e.clientY);
				this.palette(e.clientX, e.clientY, null, (k) => this.addAt(k, at, null));
			});
			root.addEventListener('pointerdown', (e) => {
				root.focus();
				const pinEl = e.target.closest('.fv-pin');
				const nodeEl = e.target.closest('.fv-node');
				const wireEl = e.target.closest && e.target.closest('.fv-wire');
				if (wireEl) {
					this.pickedWire = wireEl.dataset.wire;
					this.wires();
					return;
				}
				if (pinEl && nodeEl && !this.hooks.readOnly) {
					// a wire being drawn from an exit
					e.preventDefault();
					const n = F().find(this.g, nodeEl.dataset.id);
					const pin = pinEl.closest('.fv-exit').dataset.pin;
					const start = this.pinPoint(pinEl, n);
					this.drag = { kind: 'wire', from: n.id, pin, start };
					root.setPointerCapture(e.pointerId);
					return;
				}
				if (nodeEl && e.target.closest('.fv-title') && e.button === 0) {
					const id = nodeEl.dataset.id;
					if (!this.picked.has(id)) {
						this.pick(e.shiftKey ? [...this.picked, id] : [id]);
					}
					const start = this.toWorld(e.clientX, e.clientY);
					const moving = [...this.picked].map((pid) => F().find(this.g, pid)).filter(Boolean).map((n) => ({ n, x: n.x, y: n.y }));
					this.drag = { kind: 'move', start, moving, before: this.hooks.snapshot() };
					root.setPointerCapture(e.pointerId);
					return;
				}
				if (nodeEl) {
					this.pick([nodeEl.dataset.id]);
					return;
				}
				this.pickedWire = null;
				this.pick([]);
				this.wires();
				const v = this.state.view;
				this.drag = { kind: 'pan', sx: e.clientX, sy: e.clientY, vx: v.x, vy: v.y };
				root.setPointerCapture(e.pointerId);
			});
			root.addEventListener('pointermove', (e) => {
				const d = this.drag;
				if (!d) {
					return;
				}
				if (d.kind === 'pan') {
					this.state.view.x = d.vx + e.clientX - d.sx;
					this.state.view.y = d.vy + e.clientY - d.sy;
					this.place();
				} else if (d.kind === 'move') {
					const now = this.toWorld(e.clientX, e.clientY);
					const dx = Math.round((now.x - d.start.x) / 8) * 8;
					const dy = Math.round((now.y - d.start.y) / 8) * 8;
					for (const m of d.moving) {
						m.n.x = m.x + dx;
						m.n.y = m.y + dy;
						const ne = this.nodeEls.get(m.n.id);
						if (ne) {
							ne.style.left = m.n.x + 'px';
							ne.style.top = m.n.y + 'px';
						}
					}
					d.moved = dx !== 0 || dy !== 0;
					this.wires();
				} else if (d.kind === 'wire') {
					const now = this.toWorld(e.clientX, e.clientY);
					this.wires({ x1: d.start.x, y1: d.start.y, x2: now.x, y2: now.y, color: PIN_COLORS[d.pin] || '#ccc' });
				}
			});
			root.addEventListener('pointerup', (e) => {
				const d = this.drag;
				this.drag = null;
				if (!d) {
					return;
				}
				if (d.kind === 'move' && d.moved) {
					this.hooks.commit('move', d.before);
				} else if (d.kind === 'wire') {
					const under = document.elementFromPoint(e.clientX, e.clientY);
					const target = under && under.closest('.fv-node');
					if (target && target.dataset.id !== d.from) {
						this.edit('wire up', (g) => {
							const why = F().connect(g, d.from, d.pin, target.dataset.id);
							if (why && MS.app) {
								MS.app.toast(why, 'bad');
							}
						});
					} else if (!target) {
						// dropped on empty space: a new node there, wired from this exit
						const at = this.toWorld(e.clientX, e.clientY);
						const only = d.pin === 'next' ? (k) => k.category === 'stage' : (k) => k.entrance;
						this.wires();
						this.palette(e.clientX, e.clientY, only, (k) => this.addAt(k, { x: at.x, y: at.y - 16 }, { id: d.from, pin: d.pin }));
					} else {
						this.wires();
					}
				}
			});
			root.addEventListener('keydown', (e) => {
				if (e.target !== root) {
					return;
				}
				const ctrl = e.ctrlKey || e.metaKey;
				if ((e.key === 'Delete' || e.key === 'Backspace') && !this.hooks.readOnly) {
					if (this.pickedWire) {
						const [id, pin] = this.pickedWire.split(':');
						this.pickedWire = null;
						this.edit('cut a wire', (g) => F().disconnect(g, id, pin));
					} else if (this.picked.size) {
						const ids = [...this.picked];
						this.pick([]);
						this.edit('take ' + (ids.length > 1 ? ids.length + ' nodes' : 'a node') + ' out', (g) => ids.forEach((id) => F().removeNode(g, id, true)));
					}
					e.preventDefault();
				} else if (ctrl && (e.key === 'z' || e.key === 'Z') && !e.shiftKey) {
					this.hooks.undo();
					e.preventDefault();
				} else if (ctrl && (e.key === 'y' || e.key === 'Y' || ((e.key === 'z' || e.key === 'Z') && e.shiftKey))) {
					this.hooks.redo();
					e.preventDefault();
				} else if (ctrl && (e.key === 'd' || e.key === 'D') && this.picked.size && !this.hooks.readOnly) {
					const ids = [...this.picked];
					this.edit('copy nodes', (g) => {
						const made = [];
						for (const id of ids) {
							const n = F().find(g, id);
							const copy = F().addNode(g, n.kind, n.x + 40, n.y + 40, JSON.parse(JSON.stringify(n.p || {})));
							if (copy && n.lists) {
								copy.lists = JSON.parse(JSON.stringify(n.lists));
							}
							made.push(copy.id);
						}
						this.picked = new Set(made);
						this.state.picked = made;
					});
					e.preventDefault();
				} else if (e.key === 'f' || e.key === 'F') {
					this.fit();
				}
			});
		}
	}

	MS.FlowView = FlowView;
})();
