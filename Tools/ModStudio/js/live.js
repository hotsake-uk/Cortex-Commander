// Cortex Mod Studio: live values in Lua scripts, like Quokka's live comments. A comment marks what to show:
//
//   local speed = self.Vel.Magnitude --?        the statement on that line (here the value of speed)
//   foo(a.b --[[?]], c)                         the expression just before the comment (here a.b)
//   self:Update() --?.                          how long the line took (in a loop: average, min, max and total)
//   local t = MakeTable() --?+                  the value with tables shown wider
//   local actor = ToActor(mo) --? $.Health      any Lua, with $ the marked value
//   --? self.Pos                                alone on a line: that Lua's value at that point (Quokka's bare expressions, which
//                                               Lua itself wouldn't take)
//
// Or click left of a line number (a logpoint, as in VS Code): the line is logged as if it ended with --?, with nothing written in the
// script, so the game's own read-only scripts can be logged too. A logpoint on an unchanged script takes effect at once.
//
// Saving a script with markers writes a copy of it with value probes in it to ModStudio/Live/<script path>, every line where it was,
// and asks the game to reload the script; a game started from Studio (built from 8.5.252 on) loads the copy in its place, so errors
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
		return { block: raw.startsWith('--['), kind, expr: rest.includes('$') ? rest.replace(/\$/g, '__v') : '', text: rest };
	}

	const luaString = (s) => JSON.stringify(s);

	// Instruments a script: {code, probes} with the probes it has, {probes: []} when it has no markers, or {error}. logLines are the
	// lines with a logpoint (clicked in the editor's gutter), each shown as if it ended with "--?".
	L.instrument = function (path, text, logLines) {
		logLines = logLines || [];
		if (!logLines.length && !/--(\[=*\[)?\?/.test(text)) {
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
		const marked = (ast.comments || []).map((comment) => ({ comment, marker: readMarker(comment) })).filter((m) => m.marker);
		// A logpoint is a "--?" just before any comment at the end of its line (or at the line's end), unless the line has a marker.
		const lineStarts = [0];
		for (let i = 0; i < text.length; i++) {
			if (text[i] === '\n') {
				lineStarts.push(i + 1);
			}
		}
		for (const line of new Set(logLines)) {
			if (line < 1 || line > lineStarts.length || marked.some((m) => !m.marker.block && m.comment.loc.start.line === line)) {
				continue;
			}
			const trailing = (ast.comments || []).find((c) => c.loc.start.line === line);
			let at = trailing ? trailing.range[0] : (lineStarts[line] !== undefined ? lineStarts[line] - 1 : text.length);
			while (at > lineStarts[line - 1] && /[\r\n]/.test(text[at - 1])) {
				at--;
			}
			marked.push({ comment: { range: [at, at], loc: { start: { line, column: -1 } } }, marker: { block: false, kind: '', expr: '' }, logpoint: true });
		}
		for (const { comment, marker, logpoint } of marked) {
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
				} else if (marker.text && !marker.text.includes('$') && /^[ \t]*$/.test(text.slice(text.lastIndexOf('\n', comment.range[0] - 1) + 1, comment.range[0]))
					&& !expressions.some((e) => e.range[0] < comment.range[0] && e.range[1] > comment.range[0])) {
					// (Not inside a table or a call's arguments spread over lines, where a probe would be one more value in it.)
					// "--? speed" alone on a line, like a bare expression in Quokka (which Lua wouldn't allow): logs that Lua at that point.
					edits.push({ at: comment.range[0], end: comment.range[0], text: `${fn}(${k}, (${marker.text})); ` });
					done = true;
				}
			}
			if (done) {
				probes.push({ key, line, kind: marker.kind, logpoint: !!logpoint });
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
		// The copy has to load: a probe somewhere Lua can't take one (a "--? x" line inside a table, say) turns live values off for the
		// script, with the reason, rather than breaking it in the game.
		try {
			window.luaparse.parse(code, { luaVersion: '5.2' });
		} catch (e) {
			return { error: 'a marker is somewhere a value can\'t be logged (' + e.message + ')' };
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
		// live values need Commander's engine
		if (MS.target.classic()) {
			return;
		}
		const lower = path.toLowerCase();
		const result = L.instrument(path, text, logLinesOf(path));
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
		if (MS.target.classic()) {
			return;
		}
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
			// The game rewrites the file as values come in: a read that lands while it is being written (empty, or cut short) changes nothing
			// on screen, and values are updated in place rather than wiped and drawn again (which made them flash).
			if (text.trim() && text !== lastText) {
				lastText = text;
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
					hover = 'Shows here when this line runs in a game started from Studio (Test in game, Sandbox or the Actor Editor), after saving. The game has to be built from 8.5.252 or later.';
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

	// ---- logpoints: click left of a line number to log that line, with no comment in the file ----

	const logpoints = new Map(); // tab -> decoration ids (Monaco moves them with the text as it's edited)
	let hintIds = [];

	function logLinesOf(path) {
		const tab = MS.ide.tabs.find((t) => t.model && t.path && t.path.toLowerCase() === path.toLowerCase());
		const ids = tab ? logpoints.get(tab) || [] : [];
		return ids.map((id) => tab.model.getDecorationRange(id)).filter(Boolean).map((r) => r.startLineNumber);
	}

	function logpointDecoration(line) {
		return {
			range: new monaco.Range(line, 1, line, 1),
			options: {
				glyphMarginClassName: 'live-logpoint',
				glyphMarginHoverMessage: { value: 'Logs this line\'s value while the game runs (click to remove)' },
				stickiness: monaco.editor.TrackedRangeStickiness.NeverGrowsWhenTypingAtEdges,
			},
		};
	}

	L.toggleLogpoint = async function (tab, line) {
		const ids = logpoints.get(tab) || [];
		const at = ids.findIndex((id) => {
			const r = tab.model.getDecorationRange(id);
			return r && r.startLineNumber === line;
		});
		if (at >= 0) {
			tab.model.deltaDecorations([ids[at]], []);
			ids.splice(at, 1);
		} else {
			ids.push(...tab.model.deltaDecorations([], [logpointDecoration(line)]));
		}
		logpoints.set(tab, ids);
		if (tab.model.getValue() !== tab.savedText) {
			app().toast('Save the script to start logging that line in the game.', '');
			return;
		}
		// Nothing unsaved: the live copy is written again from what's on disk, and the game reloads the script.
		await L.afterSave(tab.path, tab.savedText);
		MS.ide.requestScripts({ scripts: [tab.path] });
	};

	L.attach = function (editor) {
		const glyph = monaco.editor.MouseTargetType.GUTTER_GLYPH_MARGIN;
		const luaTab = () => {
			const model = editor.getModel();
			const tab = model && model.ccTab;
			return tab && tab.lang === 'lua' ? tab : null; // The game's own scripts too: the live copy never touches the file.
		};
		editor.onMouseDown((e) => {
			const tab = luaTab();
			if (tab && e.target.type === glyph && e.target.position) {
				L.toggleLogpoint(tab, e.target.position.lineNumber);
			}
		});
		// A faint dot where a click would add a logpoint, as VS Code shows for breakpoints.
		editor.onMouseMove((e) => {
			const tab = luaTab();
			const line = tab && e.target.type === glyph && e.target.position ? e.target.position.lineNumber : 0;
			const model = editor.getModel();
			if (model) {
				hintIds = model.deltaDecorations(hintIds, line ? [{ range: new monaco.Range(line, 1, line, 1), options: { glyphMarginClassName: 'live-logpoint-hint' } }] : []);
			}
		});
		editor.onMouseLeave(() => {
			const model = editor.getModel();
			if (model) {
				hintIds = model.deltaDecorations(hintIds, []);
			}
		});
		editor.onDidChangeModel(() => {
			hintIds = [];
		});
	};
})();
