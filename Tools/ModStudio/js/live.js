// Cortex Mod Studio: live values in Lua scripts, like Quokka's live comments. A comment marks what to show:
//
//   local speed = self.Vel.Magnitude --?        the statement on that line (here the value of speed)
//   foo(a.b --[[?]], c)                         the expression just before the comment (here a.b)
//   self:Update() --?.                          how long the line took (in a loop: average, min, max and total)
//   local t = MakeTable() --?+                  the value with tables shown wider
//   local actor = ToActor(mo) --? $.Health      any Lua, with $ the marked value
//
// Saving a script with markers writes a copy of it with value probes in it to ModStudio/Live/<script path>, every line where it was,
// and asks the game to reload the script; a game started from Studio (built from 8.5.255 on) loads the copy in its place, so errors
// still point at the script's own lines. The probes' values come back through ModStudio/Live.ini and show at the end of each marked
// line, with how many times it ran; hovering shows the last few values. Saving a script with no markers writes an empty copy, which
// the game ignores. The script itself is never changed.
'use strict';
window.MS = window.MS || {};

(function () {
	const app = () => MS.app;
	const LIVE_DIR = 'ModStudio/Live/';
	const VALUES_FILE = 'ModStudio/Live.ini';

	const L = {
		probes: new Map(), // lower-case script path -> [{key, line, kind}]
		values: new Map(), // key -> {count, recent: [text]}
		decorations: new Map(), // tab -> {ids, keys}
	};
	MS.live = L;

	// ---- the instrumenter ----

	const EXPRESSIONS = new Set([
		'Identifier', 'MemberExpression', 'IndexExpression', 'CallExpression', 'TableCallExpression', 'StringCallExpression', 'BinaryExpression',
		'LogicalExpression', 'UnaryExpression', 'StringLiteral', 'NumericLiteral', 'BooleanLiteral', 'NilLiteral', 'VarargLiteral',
		'TableConstructorExpression',
	]);

	// Every node of the tree with its parent and the field it sits in, so lvalues and method bases can be told apart.
	function walk(node, visit, parent, field) {
		if (!node || typeof node !== 'object') {
			return;
		}
		if (Array.isArray(node)) {
			node.forEach((n) => walk(n, visit, parent, field));
			return;
		}
		if (!node.type) {
			return;
		}
		visit(node, parent, field);
		for (const key of Object.keys(node)) {
			if (key !== 'loc' && key !== 'range' && key !== 'type' && key !== 'inParens') {
				walk(node[key], visit, node, key);
			}
		}
	}

	// What a marker asks for: kind '' (a value), 'time' or 'wide', and the Lua after it, with $ standing for the value.
	function readMarker(comment) {
		const raw = comment.raw;
		let inner;
		if (/^--\[(=*)\[/.test(raw)) {
			inner = comment.value;
		} else {
			inner = raw.slice(2);
		}
		if (!inner.startsWith('?')) {
			return null;
		}
		let rest = inner.slice(1);
		let kind = '';
		if (rest.startsWith('.')) {
			kind = 'time';
			rest = rest.slice(1);
		} else if (rest.startsWith('+')) {
			kind = 'wide';
			rest = rest.slice(1);
		}
		rest = rest.trim();
		return { block: raw.startsWith('--['), kind, expr: rest.includes('$') ? rest.replace(/\$/g, '__v') : '' };
	}

	const luaString = (s) => JSON.stringify(s);

	// Instruments a script: {code, probes} with the probes it has, {probes: []} when it has no markers, or {error}.
	L.instrument = function (path, text) {
		if (!/--(\[=*\[)?\?/.test(text)) {
			return { probes: [] };
		}
		let ast;
		try {
			ast = window.luaparse.parse(text, { luaVersion: '5.2', locations: true, ranges: true, comments: true });
		} catch (e) {
			return { error: e.message };
		}
		const statements = [];
		const expressions = [];
		walk(ast.body, (node, parent, field) => {
			if (EXPRESSIONS.has(node.type)) {
				const lvalue = parent && ((field === 'variables' && (parent.type === 'AssignmentStatement' || parent.type === 'LocalStatement' || parent.type === 'ForGenericStatement'))
					|| (field === 'variable' && parent.type === 'ForNumericStatement') || field === 'parameters' || (field === 'identifier' && parent.type === 'FunctionDeclaration')
					|| (field === 'base' && node.type === 'MemberExpression' && node.indexer === ':') || (parent.type === 'MemberExpression' && field === 'identifier')
					|| (parent.type === 'TableKeyString' && field === 'key') || parent.type === 'LabelStatement' || parent.type === 'GotoStatement');
				const methodBase = node.type === 'MemberExpression' && node.indexer === ':';
				if (!lvalue && !methodBase) {
					expressions.push(node);
				}
			} else if (/Statement$|Clause$/.test(node.type)) {
				statements.push(node);
			}
		});

		const edits = []; // {at, end, text}: replace text[at, end) with text, all on the line they are on
		const probes = [];
		const src = (n) => text.slice(n.range[0], n.range[1]);
		for (const comment of ast.comments || []) {
			const marker = readMarker(comment);
			if (!marker) {
				continue;
			}
			const line = comment.loc.start.line;
			const key = path + '|' + line + ':' + (comment.loc.start.column + 1);
			const k = luaString(key);
			const fn = marker.kind === 'wide' ? '__qx' : '__q';
			// The probe for values text that evaluates to them: as they are, or through the marker's Lua with $ as the first one.
			const show = (values) => (marker.expr ? `(function(...) local __v = ...; ${fn}(${k}, (${marker.expr})); return ... end)(${values})` : `${fn}(${k}, ${values})`);
			let done = false;
			if (marker.block) {
				// The expression that ends just before the comment; the outermost one, as Quokka does.
				let end = comment.range[0];
				while (end > 0 && /\s/.test(text[end - 1])) {
					end--;
				}
				const hits = expressions.filter((e) => e.range[1] === end);
				const target = hits.sort((a, b) => a.range[0] - b.range[0])[0];
				if (target) {
					const wrapped = marker.kind === 'time' ? `__qt1(${k}, select(2, __qt0(${k}), ${src(target)}))` : show(src(target));
					edits.push({ at: target.range[0], end: target.range[1], text: wrapped });
					done = true;
				}
			} else {
				const before = statements.filter((s) => s.loc.end.line === line && s.range[1] <= comment.range[0] && !/Clause$/.test(s.type));
				const target = before.sort((a, b) => b.range[1] - a.range[1])[0]
					|| statements.filter((s) => s.loc.start.line === line && /^(If|While|ForNumeric|ForGeneric)Statement$/.test(s.type)).sort((a, b) => b.range[0] - a.range[0])[0];
				if (target) {
					done = lineProbe(target, marker, k, show, edits, text, line);
				}
			}
			if (done) {
				probes.push({ key, line, kind: marker.kind });
			}
		}
		if (!probes.length) {
			return { probes: [] };
		}
		// Apply from the end, so earlier positions stay right. Edits never overlap in practice; one that would is dropped.
		edits.sort((a, b) => b.at - a.at);
		let code = text;
		let limit = Infinity;
		for (const e of edits) {
			if (e.end > limit) {
				continue;
			}
			code = code.slice(0, e.at) + e.text + code.slice(e.end);
			limit = e.at;
		}
		return { code, probes };
	};

	// A "--?" at the end of a line: what the statement on it shows.
	function lineProbe(s, marker, k, show, edits, text, line) {
		const src = (n) => text.slice(n.range[0], n.range[1]);
		if (marker.kind === 'time') {
			if (s.type === 'ReturnStatement') {
				if (!s.arguments.length) {
					edits.push({ at: s.range[0], end: s.range[0], text: `__qt0(${k}); __qt1(${k}); ` });
					return true;
				}
				const first = s.arguments[0].range[0];
				const last = s.arguments[s.arguments.length - 1].range[1];
				edits.push({ at: first, end: last, text: `__qt1(${k}, ${text.slice(first, last)})` }, { at: s.range[0], end: s.range[0], text: `__qt0(${k}); ` });
				return true;
			}
			edits.push({ at: s.range[1], end: s.range[1], text: `; __qt1(${k})` }, { at: s.range[0], end: s.range[0], text: `__qt0(${k}); ` });
			return true;
		}
		switch (s.type) {
			case 'LocalStatement':
			case 'AssignmentStatement':
				edits.push({ at: s.range[1], end: s.range[1], text: '; ' + show(s.variables.map(src).join(', ')) });
				return true;
			case 'CallStatement':
				edits.push({ at: s.expression.range[0], end: s.expression.range[1], text: show(src(s.expression)) });
				return true;
			case 'ReturnStatement': {
				if (!s.arguments.length) {
					edits.push({ at: s.range[0], end: s.range[0], text: show('') + '; ' });
					return true;
				}
				const first = s.arguments[0].range[0];
				const last = s.arguments[s.arguments.length - 1].range[1];
				edits.push({ at: first, end: last, text: show(text.slice(first, last)) });
				return true;
			}
			case 'IfStatement': {
				const clause = s.clauses.find((c) => c.condition && c.condition.loc.start.line === line);
				if (clause) {
					edits.push({ at: clause.condition.range[0], end: clause.condition.range[1], text: show(src(clause.condition)) });
					return true;
				}
				return false;
			}
			case 'WhileStatement':
			case 'RepeatStatement':
				edits.push({ at: s.condition.range[0], end: s.condition.range[1], text: show(src(s.condition)) });
				return true;
			case 'ForNumericStatement':
			case 'ForGenericStatement': {
				// Each time round: the loop's variables, at the start of the body (just after "do").
				const header = s.type === 'ForNumericStatement' ? (s.step || s.end) : s.iterators[s.iterators.length - 1];
				const doAt = text.indexOf('do', header.range[1]);
				if (doAt < 0) {
					return false;
				}
				const vars = s.type === 'ForNumericStatement' ? src(s.variable) : s.variables.map(src).join(', ');
				edits.push({ at: doAt + 2, end: doAt + 2, text: ' ' + show(vars) + ';' });
				return true;
			}
			default:
				return false;
		}
	}

	// ---- after a script is saved ----

	// Writes the script's live copy (or an empty one when it has no markers, if it had a copy), before the game is asked to reload it.
	L.afterSave = async function (path, text) {
		const lower = path.toLowerCase();
		const result = L.instrument(path, text);
		if (result.error) {
			app().toast('Live values are off for ' + path.split('/').pop() + ' until it parses: ' + result.error, 'bad');
			result.probes = [];
		}
		const had = L.probes.has(lower);
		try {
			if (result.probes.length) {
				await app().fs.writeDisk(LIVE_DIR + path, result.code);
				L.probes.set(lower, result.probes);
			} else if (had || (await app().fs.api('GET', 'file', LIVE_DIR + path).catch(() => null))) {
				await app().fs.writeDisk(LIVE_DIR + path, '');
				L.probes.delete(lower);
			}
		} catch (e) {
			app().toast('Couldn\'t write the live copy of ' + path + ': ' + e.message, 'bad');
		}
		for (const key of [...L.values.keys()]) {
			if (key.toLowerCase().startsWith(lower + '|')) {
				L.values.delete(key);
			}
		}
		L.redraw();
		poll();
	};

	// Markers in a script that's open but not saved since Studio started: shown as waiting, so you can see they were read.
	L.noteOpen = function (path, text) {
		const lower = path.toLowerCase();
		if (!L.probes.has(lower)) {
			const result = L.instrument(path, text);
			if (result.probes && result.probes.length) {
				L.probes.set(lower, result.probes);
				poll();
			}
		}
		L.redraw();
	};

	// ---- showing the values ----

	let timer = 0;
	let lastText = null;
	async function poll() {
		clearTimeout(timer);
		if (!L.probes.size || !app().fs || !app().fs.server) {
			return;
		}
		try {
			const response = await app().fs.api('GET', 'file', VALUES_FILE);
			const text = response ? await response.text() : '';
			if (text !== lastText) {
				lastText = text;
				L.values.clear();
				for (const line of text.split('\n')) {
					const at = line.indexOf(' = ');
					if (at < 0) {
						continue;
					}
					const [count, ...recent] = line.slice(at + 3).replace(/\r$/, '').split('\t');
					L.values.set(line.slice(0, at), { count: Number(count) || 0, recent });
				}
				L.redraw();
			}
		} catch (e) {
			// The game isn't running, or no probe has run yet.
		}
		timer = setTimeout(poll, 400);
	}

	// Draws each open script's values at the end of its marked lines. A line keeps its value while you edit above it (Monaco moves the
	// decoration with the text), and a marker whose probe hasn't run yet says so.
	L.redraw = function () {
		for (const tab of MS.ide.tabs) {
			if (tab.kind !== 'code' || !tab.model || tab.lang !== 'lua') {
				continue;
			}
			const probes = L.probes.get(tab.path.toLowerCase()) || [];
			const old = L.decorations.get(tab) || { ids: [], keys: [] };
			const where = new Map();
			old.keys.forEach((key, i) => {
				const range = tab.model.getDecorationRange(old.ids[i]);
				if (range) {
					where.set(key, range.startLineNumber);
				}
			});
			const decorations = [];
			const keys = [];
			for (const probe of probes) {
				const line = where.get(probe.key) || probe.line;
				if (line > tab.model.getLineCount()) {
					continue;
				}
				const value = L.values.get(probe.key);
				const end = tab.model.getLineMaxColumn(line);
				let content;
				let hover;
				if (value) {
					const last = value.recent[value.recent.length - 1] || '';
					content = '  ' + (last.length > 120 ? last.slice(0, 117) + '...' : last) + (value.count > 1 ? '  ×' + value.count : '');
					hover = '**Ran ' + value.count + ' time' + (value.count === 1 ? '' : 's') + '.** Last values, newest last:\n\n' + value.recent.map((v) => '- `' + v.replace(/`/g, "'") + '`').join('\n');
				} else {
					content = '  … not run yet';
					hover = 'Shows here when this line runs in a game started from Studio (Test in game, Sandbox or the Actor Editor), after saving. The game has to be built from 8.5.255 or later.';
				}
				decorations.push({
					range: new monaco.Range(line, end, line, end),
					options: {
						after: { content, inlineClassName: value ? 'live-value' : 'live-waiting' },
						hoverMessage: { value: hover },
						showIfCollapsed: true, // The range is empty (the end of the line), which Monaco otherwise doesn't draw.
						stickiness: monaco.editor.TrackedRangeStickiness.NeverGrowsWhenTypingAtEdges,
					},
				});
				keys.push(probe.key);
			}
			const ids = tab.model.deltaDecorations(old.ids, decorations);
			L.decorations.set(tab, { ids, keys });
		}
	};
})();
