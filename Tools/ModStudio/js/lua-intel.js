// Cortex Mod Studio: what the Lua editor knows about the game. Completion, hover help, signature help, go to definition and checks for
// scripts, from data/lua-api.json (written from the engine's own bindings by tools/make-lua-api.js), the presets Studio has read, and the
// globals and functions every loaded script defines.
//
// Types are worked out the way a modder reads a script, not by running it: "self" is the class of the presets that use the script,
// "local x = CreateAHuman(...)" is an AHuman, "for actor in MovableMan.Actors do" makes actor an Actor, and ---@type and ---@param
// comments (as VS Code's Lua extension reads them) say what anything else is.
'use strict';
window.MS = window.MS || {};

(function () {
	// ---- what the game calls in scripts ----

	const ENTITY_CALLBACKS = {
		Create: { params: [], doc: 'Runs once when the object is made (and again when its scripts are reloaded).' },
		Update: { params: [], doc: 'Runs every frame while the object is in the game.' },
		Destroy: { params: [], doc: 'Runs when the object is removed from the game.' },
		ThreadedUpdate: { params: [], doc: 'Runs every frame on a worker thread, alongside other objects. Only read other objects here; change them in SyncedUpdate.' },
		SyncedUpdate: { params: [], doc: 'Runs after ThreadedUpdate, one object at a time, for changes ThreadedUpdate asked for with self:RequestSyncedUpdate().' },
		OnScriptEnable: { params: [], doc: 'Runs when this script is turned on with EnableScript.' },
		OnScriptDisable: { params: [], doc: 'Runs when this script is turned off with DisableScript.' },
		OnCollideWithTerrain: { params: [{ name: 'terrainID', type: 'number', doc: 'The material the object hit.' }], doc: 'Runs when the object hits the ground (needs ProvidesPieMenuContext or a collision flag set, as for OnCollideWithMO).' },
		OnCollideWithMO: { params: [{ name: 'collidedMO', type: 'MovableObject', doc: 'The object this one hit.' }, { name: 'collidedRootMO', type: 'MovableObject', doc: 'The object that one is attached to, or itself.' }], doc: 'Runs when the object hits another object.' },
		WhilePieMenuOpen: { params: [{ name: 'pieMenu', type: 'PieMenu', doc: 'The open pie menu.' }], doc: 'Runs every frame while this object\'s pie menu is open.' },
		OnSave: { params: [], doc: 'Runs when the game is saved, to store values with self:SetStringValue and the like.' },
		OnMessage: { params: [{ name: 'message', type: 'string' }, { name: 'context', type: 'any' }], doc: 'Runs when something sends this object a message with SendMessage.' },
		OnGlobalMessage: { params: [{ name: 'message', type: 'string' }, { name: 'context', type: 'any' }], doc: 'Runs when a message is sent to every object with MovableMan:SendGlobalMessage.' },
		UpdateAI: { params: [], doc: 'Actors: runs every frame the AI controls this actor, in place of its own AI.' },
		ThreadedUpdateAI: { params: [], doc: 'Actors: UpdateAI on a worker thread.' },
		OnControllerInputModeChange: { params: [{ name: 'previousControllerMode', type: 'number' }, { name: 'previousControllingPlayer', type: 'number' }], doc: 'Actors: runs when control passes between a player and the AI.' },
		OnStride: { params: [], doc: 'AHuman, ACrab and AMech: runs on every footstep.' },
		OnAttach: { params: [{ name: 'newParent', type: 'MOSRotating', doc: 'What it was attached to.' }], doc: 'Attachables: runs when attached to something.' },
		OnDetach: { params: [{ name: 'exParent', type: 'MOSRotating', doc: 'What it came off.' }], doc: 'Attachables: runs when it comes off what it was attached to.' },
		OnFire: { params: [], doc: 'Guns (HDFirearm): runs each time it fires.' },
		OnReload: { params: [{ name: 'hadMagazineBeforeReloading', type: 'boolean' }], doc: 'Guns (HDFirearm): runs when a reload finishes.' },
	};
	const ACTIVITY_CALLBACKS = {
		StartActivity: { params: [{ name: 'isNewGame', type: 'boolean', doc: 'False when a saved game is being loaded.' }], doc: 'Runs when the activity starts.' },
		UpdateActivity: { params: [], doc: 'Runs every frame of the activity.' },
		PauseActivity: { params: [{ name: 'pause', type: 'boolean' }], doc: 'Runs when the game is paused or unpaused.' },
		EndActivity: { params: [], doc: 'Runs when the activity ends.' },
		OnSave: { params: [], doc: 'Runs when the game is saved, to store values with self:SaveNumber and SaveString.' },
		CraftEnteredOrbit: { params: [{ name: 'orbitedCraft', type: 'ACraft' }], doc: 'Runs when a craft flies off the top of the map.' },
		OnMessage: { params: [{ name: 'message', type: 'string' }, { name: 'context', type: 'any' }], doc: 'Runs when the activity is sent a message.' },
		OnGlobalMessage: { params: [{ name: 'message', type: 'string' }, { name: 'context', type: 'any' }], doc: 'Runs when a message is sent to everything.' },
		IsCompatibleScene: { params: [{ name: 'scene', type: 'Scene' }], returns: 'boolean', doc: 'Return whether the activity can be played on this scene.' },
	};

	// ---- the parts of Lua the game keeps ----

	const fn = (params, returns, doc) => ({ kind: 'function', params: params.map((p) => (typeof p === 'string' ? { name: p.replace('?', ''), type: 'any', optional: p.endsWith('?') } : p)), returns: returns || 'any', doc: doc || '' });
	const LIBS = {
		math: {
			abs: fn(['x'], 'number'), ceil: fn(['x'], 'number'), floor: fn(['x'], 'number'), sqrt: fn(['x'], 'number'), sin: fn(['x'], 'number', 'Radians.'),
			cos: fn(['x'], 'number', 'Radians.'), tan: fn(['x'], 'number'), asin: fn(['x'], 'number'), acos: fn(['x'], 'number'), atan: fn(['y', 'x?'], 'number'),
			atan2: fn(['y', 'x'], 'number'), exp: fn(['x'], 'number'), log: fn(['x', 'base?'], 'number'), max: fn(['x', '...'], 'number'), min: fn(['x', '...'], 'number'),
			fmod: fn(['x', 'y'], 'number'), modf: fn(['x'], 'number'), pow: fn(['x', 'y'], 'number'), rad: fn(['degrees'], 'number'), deg: fn(['radians'], 'number'),
			random: fn(['m?', 'n?'], 'number', 'The game\'s own random numbers: random() is 0 to 1, random(n) is 1 to n, random(m, n) is m to n.'),
			huge: { kind: 'const', type: 'number' }, pi: { kind: 'const', type: 'number' },
		},
		string: {
			format: fn(['format', '...'], 'string'), sub: fn(['s', 'i', 'j?'], 'string'), len: fn(['s'], 'number'), lower: fn(['s'], 'string'), upper: fn(['s'], 'string'),
			find: fn(['s', 'pattern', 'init?', 'plain?'], 'number'), match: fn(['s', 'pattern', 'init?'], 'string'), gmatch: fn(['s', 'pattern'], 'any'),
			gsub: fn(['s', 'pattern', 'replacement', 'n?'], 'string'), rep: fn(['s', 'n'], 'string'), reverse: fn(['s'], 'string'), byte: fn(['s', 'i?', 'j?'], 'number'), char: fn(['...'], 'string'),
		},
		table: {
			insert: fn(['t', 'value'], 'nil', 'Adds a value at the end, or table.insert(t, position, value) at a position.'), remove: fn(['t', 'position?'], 'any'),
			concat: fn(['t', 'separator?', 'i?', 'j?'], 'string'), sort: fn(['t', 'compare?'], 'nil'), unpack: fn(['t', 'i?', 'j?'], 'any'),
		},
		os: { clock: fn([], 'number'), time: fn(['t?'], 'number'), date: fn(['format?', 'time?'], 'string') },
		coroutine: { create: fn(['f'], 'any'), resume: fn(['co', '...'], 'boolean'), yield: fn(['...'], 'any'), status: fn(['co'], 'string'), wrap: fn(['f'], 'any'), running: fn([], 'any') },
	};
	const BUILTINS = {
		print: fn(['value'], 'nil', 'Writes to the game\'s console (and Studio\'s).'),
		tostring: fn(['value'], 'string'), tonumber: fn(['value', 'base?'], 'number'), type: fn(['value'], 'string'),
		pairs: fn(['t'], 'any'), ipairs: fn(['t'], 'any'), next: fn(['t', 'key?'], 'any'), select: fn(['n', '...'], 'any'), unpack: fn(['t', 'i?', 'j?'], 'any'),
		pcall: fn(['f', '...'], 'boolean'), xpcall: fn(['f', 'handler'], 'boolean'), error: fn(['message', 'level?'], 'nil'), assert: fn(['value', 'message?'], 'any'),
		setmetatable: fn(['t', 'metatable'], 'table'), getmetatable: fn(['t'], 'table'), rawget: fn(['t', 'key'], 'any'), rawset: fn(['t', 'key', 'value'], 'table'), rawequal: fn(['a', 'b'], 'boolean'),
		require: fn(['module'], 'any'), dofile: fn(['path'], 'any', 'Runs a Lua file, by its path from the game\'s Data or Mods folder ("Base.rte/Scripts/Utility.lua").'), loadfile: fn(['path'], 'any'),
		loadstring: fn(['code'], 'any'),
		StudioWatch: fn([{ name: 'name', type: 'string' }, { name: 'value', type: 'any' }], 'nil', 'Shows a value in Mod Studio\'s Watch panel, updated a few times a second (in games started from Studio). For example StudioWatch("speed", self.Vel.Magnitude).'),
		SelectRand: fn([{ name: 'lower', type: 'number' }, { name: 'upper', type: 'number' }], 'number', 'A random whole number from lower to upper.'),
		RangeRand: fn([{ name: 'lower', type: 'number' }, { name: 'upper', type: 'number' }], 'number', 'A random number from lower to upper.'),
		PosRand: fn([], 'number', 'A random number from 0 to 1.'), NormalRand: fn([], 'number', 'A random number from -1 to 1.'),
		cls: fn([], 'nil', 'Clears the console.'),
	};
	const KEYWORDS = ['and', 'break', 'do', 'else', 'elseif', 'end', 'false', 'for', 'function', 'goto', 'if', 'in', 'local', 'nil', 'not', 'or', 'repeat', 'return', 'then', 'true', 'until', 'while'];

	const elementOf = (type) => (type && type.endsWith('[]') ? type.slice(0, -2) : 'any');

	// ---- what each name in a script refers to ----

	// Reads a script's scopes from luaparse's tree. Every name and field gets a key saying what it is: "L120" (the local declared at offset
	// 120), "G:DecisionDay" (a global), "G:DecisionDay.SpawnCraft" (a field of one), "S.Timer" (a field of an entity script's self), "R.x" (a
	// field of the table the file returns). self inside "function DecisionDay:M()" is G:DecisionDay, so self.x and DecisionDay.x are the
	// same thing. Returns null when the text doesn't parse.
	//   ids: [{start, end, name, key, decl, field, sep, baseStart, isDecl}] in text order
	//   defs: Map(key -> [{at, end, kind: "function" | "table" | "value" | "local" | "param" | "for", params, method, require, alias, value}])
	//   decls: [{name, at, end, key, scopeStart, scopeEnd, kind}]; funcs: [{start, end, selfKey}]; requires: [{name, how}]; returnKey
	// options.chunkSelf: the key self has outside any function (a snippet of a method's body); options.paramSelf: what a "self" parameter of a
	// plain function is (default "S").
	function analyze(text, options) {
		const parser = (typeof window !== 'undefined' && window.luaparse) || null;
		if (!parser) {
			return null;
		}
		let ast;
		try {
			ast = parser.parse(text, { luaVersion: '5.2', ranges: true, locations: false, scope: false, comments: false });
		} catch (e) {
			return null;
		}
		options = options || {};
		const ids = [];
		const defs = new Map();
		const decls = [];
		const funcs = [];
		const requires = [];
		let returnKey = null;
		const scopeOf = (parent, node) => ({ vars: new Map(), parent, start: node.range[0], end: node.range[1] });
		const lookup = (scope, name) => {
			for (let s = scope; s; s = s.parent) {
				const d = s.vars.get(name);
				if (d) {
					return d;
				}
			}
			return null;
		};
		const addDef = (key, def) => {
			if (!defs.has(key)) {
				defs.set(key, []);
			}
			def.inFirst = inFirst > 0;
			defs.get(key).push(def);
		};
		let inFirst = 0; // inside the callback the game runs first (StartActivity, Create), whose values are where fields start
		const declare = (scope, id, kind, key, init) => {
			const d = { name: id.name, at: id.range[0], end: id.range[1], key: key || 'L' + id.range[0], scopeStart: scope.start, scopeEnd: scope.end, kind };
			scope.vars.set(id.name, d);
			decls.push(d);
			ids.push({ start: d.at, end: d.end, name: d.name, key: d.key, decl: d, isDecl: true });
			addDef(d.key, Object.assign(describe(init), { at: d.at, end: d.end, kind: init ? describe(init).kind : kind }));
			return d;
		};
		const implicitSelf = (scope, at, key) => {
			const d = { name: 'self', at, end: at, key, scopeStart: scope.start, scopeEnd: scope.end, kind: 'self', implicit: true };
			scope.vars.set('self', d);
			decls.push(d);
		};
		const stringOf = (node) => (node && node.type === 'StringLiteral' ? node.raw.slice(1, -1) : null);
		const keyOf = (node, scope) => {
			if (!node) {
				return null;
			}
			if (node.type === 'Identifier') {
				const d = lookup(scope, node.name);
				return d ? d.key : 'G:' + node.name;
			}
			if (node.type === 'MemberExpression' && node.indexer === '.') {
				const base = keyOf(node.base, scope);
				return base && base + '.' + node.identifier.name;
			}
			if (node.type === 'IndexExpression' && stringOf(node.index) && /^[A-Za-z_]\w*$/.test(stringOf(node.index))) {
				const base = keyOf(node.base, scope);
				return base && base + '.' + stringOf(node.index);
			}
			return null;
		};
		let scopeForDescribe = null;
		function describe(init) {
			if (!init) {
				return { kind: 'value' };
			}
			if (init.type === 'FunctionDeclaration') {
				return { kind: 'function', params: init.parameters.map((p) => (p.type === 'VarargLiteral' ? '...' : p.name)) };
			}
			if (init.type === 'TableConstructorExpression') {
				return { kind: 'table' };
			}
			const call = requireOf(init);
			if (call) {
				return { kind: 'value', require: call.name, how: call.how };
			}
			const value = text.slice(init.range[0], Math.min(init.range[1], init.range[0] + 80));
			if ((init.type === 'Identifier' || init.type === 'MemberExpression') && scopeForDescribe) {
				return { kind: 'value', alias: keyOf(init, scopeForDescribe), value };
			}
			return { kind: 'value', value };
		}
		function requireOf(node) {
			if (!node || (node.type !== 'CallExpression' && node.type !== 'StringCallExpression') || node.base.type !== 'Identifier' || !/^(require|dofile|loadfile)$/.test(node.base.name)) {
				return null;
			}
			const name = stringOf(node.type === 'StringCallExpression' ? node.argument : node.arguments[0]);
			return name ? { name, how: node.base.name } : null;
		}

		const expr = (node, scope, owner) => {
			if (!node || typeof node !== 'object') {
				return;
			}
			switch (node.type) {
				case 'Identifier': {
					const d = lookup(scope, node.name);
					ids.push({ start: node.range[0], end: node.range[1], name: node.name, key: d ? d.key : 'G:' + node.name, decl: d });
					return;
				}
				case 'MemberExpression': {
					expr(node.base, scope);
					const base = keyOf(node.base, scope);
					ids.push({ start: node.identifier.range[0], end: node.identifier.range[1], name: node.identifier.name, key: base && base + '.' + node.identifier.name, field: true, sep: node.indexer, baseStart: node.base.range[0], baseEnd: node.base.range[1] });
					return;
				}
				case 'IndexExpression':
					expr(node.base, scope);
					expr(node.index, scope);
					return;
				case 'CallExpression':
				case 'StringCallExpression':
				case 'TableCallExpression': {
					const r = requireOf(node);
					if (r) {
						requires.push(r);
					}
					expr(node.base, scope);
					for (const a of node.arguments ? [].concat(node.arguments) : [node.argument]) {
						expr(a, scope);
					}
					return;
				}
				case 'FunctionDeclaration':
					fn(node, scope, null);
					return;
				case 'TableConstructorExpression':
					for (const f of node.fields) {
						if (f.type === 'TableKeyString') {
							if (owner) {
								ids.push({ start: f.key.range[0], end: f.key.range[1], name: f.key.name, key: owner + '.' + f.key.name, field: true, sep: '.', isDecl: true });
								scopeForDescribe = scope;
								addDef(owner + '.' + f.key.name, Object.assign(describe(f.value), { at: f.key.range[0], end: f.key.range[1] }));
							}
							expr(f.value, scope, owner && owner + '.' + f.key.name);
						} else {
							if (f.key) {
								expr(f.key, scope);
							}
							expr(f.value, scope);
						}
					}
					return;
				case 'BinaryExpression':
				case 'LogicalExpression':
					expr(node.left, scope);
					expr(node.right, scope);
					return;
				case 'UnaryExpression':
					expr(node.argument, scope);
					return;
				default:
			}
		};

		// A function: its own scope, its parameters, and self (a method's own, or a "self" parameter).
		const fn = (node, scope, selfKey) => {
			const inner = scopeOf(scope, node);
			if (node.identifier && node.identifier.type === 'MemberExpression' && node.identifier.indexer === ':') {
				implicitSelf(inner, node.range[0], selfKey);
			}
			const params = [];
			for (const p of node.parameters) {
				if (p.type === 'Identifier') {
					params.push(p.name);
					declare(inner, p, 'param', p.name === 'self' && params.length === 1 ? selfKey || options.paramSelf || 'S' : null);
				}
			}
			funcs.push({ start: node.range[0], end: node.range[1], selfKey: lookup(inner, 'self') ? lookup(inner, 'self').key : null });
			const id = node.identifier;
			const first = !!id && /^(StartActivity|Create)$/.test(id.type === 'Identifier' ? id.name : id.identifier ? id.identifier.name : '');
			inFirst += first ? 1 : 0;
			block(node.body, inner);
			inFirst -= first ? 1 : 0;
		};

		const statement = (node, scope) => {
			switch (node.type) {
				case 'LocalStatement':
					node.init.forEach((init, i) => expr(init, scope, node.variables[i] && 'L' + node.variables[i].range[0]));
					scopeForDescribe = scope;
					node.variables.forEach((v, i) => declare(scope, v, 'local', null, node.init[i]));
					return;
				case 'AssignmentStatement':
					node.variables.forEach((v, i) => {
						const key = keyOf(v, scope);
						expr(v, scope);
						const nameNode = v.type === 'MemberExpression' ? v.identifier : v.type === 'Identifier' ? v : null;
						if (key && nameNode) {
							const id = ids[ids.length - 1];
							if (id && id.start === nameNode.range[0] && !id.decl) {
								id.isDecl = !defs.has(key);
							}
							scopeForDescribe = scope;
							addDef(key, Object.assign(describe(node.init[i]), { at: nameNode.range[0], end: nameNode.range[1] }));
						}
						expr(node.init[i], scope, key);
					});
					for (let i = node.variables.length; i < node.init.length; i++) {
						expr(node.init[i], scope);
					}
					return;
				case 'CallStatement':
					expr(node.expression, scope);
					return;
				case 'FunctionDeclaration': {
					const id = node.identifier;
					const params = node.parameters.map((p) => (p.type === 'VarargLiteral' ? '...' : p.name));
					if (!id) {
						fn(node, scope, null);
					} else if (node.isLocal) {
						declare(scope, id, 'local', null, node);
						fn(node, scope, null);
					} else if (id.type === 'Identifier') {
						const key = keyOf(id, scope);
						expr(id, scope);
						ids[ids.length - 1].isDecl = true;
						addDef(key, { kind: 'function', params, at: id.range[0], end: id.range[1] });
						fn(node, scope, null);
					} else {
						const key = keyOf(Object.assign({}, id, { indexer: '.' }), scope);
						expr(id, scope);
						ids[ids.length - 1].isDecl = true;
						const method = id.indexer === ':';
						if (key) {
							addDef(key, { kind: 'function', params, method, at: id.identifier.range[0], end: id.identifier.range[1] });
						}
						fn(node, scope, method || params[0] === 'self' ? keyOf(id.base, scope) : null);
					}
					return;
				}
				case 'IfStatement':
					for (const clause of node.clauses) {
						if (clause.condition) {
							expr(clause.condition, scope);
						}
						block(clause.body, scopeOf(scope, clause));
					}
					return;
				case 'WhileStatement':
					expr(node.condition, scope);
					block(node.body, scopeOf(scope, node));
					return;
				case 'RepeatStatement': {
					const inner = scopeOf(scope, node);
					block(node.body, inner);
					expr(node.condition, inner);
					return;
				}
				case 'DoStatement':
					block(node.body, scopeOf(scope, node));
					return;
				case 'ForNumericStatement': {
					expr(node.start, scope);
					expr(node.end, scope);
					expr(node.step, scope);
					const inner = scopeOf(scope, node);
					declare(inner, node.variable, 'for');
					block(node.body, inner);
					return;
				}
				case 'ForGenericStatement': {
					node.iterators.forEach((it) => expr(it, scope));
					const inner = scopeOf(scope, node);
					node.variables.forEach((v) => declare(inner, v, 'for'));
					block(node.body, inner);
					return;
				}
				case 'ReturnStatement':
					node.arguments.forEach((a, i) => expr(a, scope, i === 0 && scope.parent === null ? 'R' : null));
					if (scope.parent === null && node.arguments[0]) {
						returnKey = node.arguments[0].type === 'TableConstructorExpression' ? 'R' : keyOf(node.arguments[0], scope);
					}
					return;
				default:
			}
		};
		const block = (body, scope) => {
			for (const s of body) {
				statement(s, scope);
			}
		};
		const root = { vars: new Map(), parent: null, start: 0, end: text.length };
		if (options.chunkSelf) {
			implicitSelf(root, 0, options.chunkSelf);
		}
		block(ast.body, root);
		ids.sort((a, b) => a.start - b.start);
		return { ids, defs, decls, funcs, requires, returnKey };
	}

	// The analysis of a script being typed in: as it is if it parses, else with the line being typed blanked out (and missing "end"s added),
	// which keeps every offset where it was.
	function analyzeTyping(text, offset, options) {
		const plain = analyze(text, options);
		if (plain || offset === undefined) {
			if (plain) {
				plain.plain = true;
			}
			return plain;
		}
		const lineStart = text.lastIndexOf('\n', offset - 1) + 1;
		let lineEnd = text.indexOf('\n', offset);
		lineEnd = lineEnd < 0 ? text.length : lineEnd;
		const blanked = text.slice(0, lineStart) + ' '.repeat(lineEnd - lineStart) + text.slice(lineEnd);
		for (const tail of ['', '\nend', '\nend end', '\nend end end']) {
			const a = analyze(blanked + tail, options);
			if (a) {
				return a;
			}
		}
		return null;
	}

	// The innermost of a script's names at an offset (the one the cursor is on).
	function idAt(analysis, start, end) {
		let lo = 0;
		let hi = analysis.ids.length - 1;
		while (lo <= hi) {
			const mid = (lo + hi) >> 1;
			const id = analysis.ids[mid];
			if (id.end <= start) {
				lo = mid + 1;
			} else if (id.start > start) {
				hi = mid - 1;
			} else {
				return id.start <= start && id.end >= (end === undefined ? start : end) ? id : null;
			}
		}
		return null;
	}

	// The key a name has at an offset: the local in scope there, self of the function it's in, or a global.
	function keyAt(analysis, name, offset) {
		let best = null;
		for (const d of analysis.decls) {
			if (d.name === name && d.at <= offset && d.scopeStart <= offset && offset <= d.scopeEnd && (!best || d.at >= best.at)) {
				best = d;
			}
		}
		return best ? best.key : 'G:' + name;
	}

	class LuaIntel {
		constructor(app) {
			this.app = app;
			this.api = null;
			this.scripts = new Map(); // lowercased module path -> {path, text}: every loaded .lua
			this.symbols = new Map(); // global name ("rte.NoMOID", "MyMod_DoThing") -> [{path, line, params, kind}]
			this.symbolNames = []; // top-level names (before any "."), for completion
		}

		// The API of the game the mods are for (MS.target): this game's in data/, a Community Edition release's in data/targets/<id>/.
		async load() {
			const data = MS.target.get().data;
			const response = await fetch(data + 'lua-api.json', { cache: 'no-cache' });
			this.api = await response.json();
			this.apiData = data;
			// The same API as Lua (LuaLS annotations), where go to definition takes a game name. Not waited for: completion doesn't need it.
			this.apiTextLoading = fetch(data + 'lua-api.d.lua', { cache: 'no-cache' }).then((r) => (r.ok ? r.text() : '')).then((text) => this.setApiText(text)).catch(() => {});
		}

		// The game's API as a Lua file (data/lua-api.d.lua), indexed by what each line declares: "Actor", "Actor.GetController", "MovableMan".
		setApiText(text) {
			this.apiText = text || '';
			this.apiLines = new Map();
			let cls = null;
			this.apiText.split(/\r?\n/).forEach((line, i) => {
				const add = (key) => {
					if (!this.apiLines.has(key)) {
						this.apiLines.set(key, i + 1);
					}
				};
				let m = /^---@class\s+(\w+)/.exec(line);
				if (m) {
					cls = m[1];
					add(cls);
					return;
				}
				m = /^---@field\s+(\w+)/.exec(line);
				if (m && cls) {
					add(cls + '.' + m[1]);
					return;
				}
				m = /^function\s+(\w+)[.:](\w+)\s*\(/.exec(line);
				if (m) {
					add(m[1] + '.' + m[2]);
					return;
				}
				m = /^function\s+(\w+)\s*\(/.exec(line) || /^(\w+)\s*=/.exec(line);
				if (m) {
					add(m[1]);
				}
			});
		}

		// Where the game's API declares a name ("Actor", "Actor.GetController", "CreateAHuman", "MovableMan"): {api: true, path, line}, or null.
		apiDefinition(name) {
			const line = this.apiLines && this.apiLines.get(name);
			return line ? { api: true, path: LuaIntel.API_PATH, line } : null;
		}

		// ---- every loaded script, for globals other scripts define ----

		setScripts(bundle) {
			this.scripts.clear();
			if (bundle) {
				for (const { path, text } of bundle.values()) {
					const modulePath = path.replace(/^(Data|Mods)\//i, '');
					this.scripts.set(modulePath.toLowerCase(), { path: modulePath, text });
				}
			}
			this.reindexSymbols();
		}

		updateScript(path, text) {
			this.scripts.set(path.toLowerCase(), { path, text });
			this.reindexSymbols();
		}

		reindexSymbols() {
			this.symbols.clear();
			this._analyses = new Map();
			const tops = new Set();
			for (const { path, text } of this.scripts.values()) {
				const lines = text.split(/\r?\n/);
				let owner = null; // the class whose method the line is in ("DecisionDay" in "function DecisionDay:StartActivity()")
				const ownFields = new Set();
				lines.forEach((line, i) => {
					let m = /^function\s+([A-Za-z_][\w.:]*)\s*\(([^)]*)\)/.exec(line);
					if (m) {
						const params = m[2].split(',').map((p) => p.trim()).filter(Boolean);
						owner = m[1].includes(':') ? m[1].slice(0, m[1].indexOf(':')) : m[1].includes('.') && params[0] === 'self' ? m[1].slice(0, m[1].lastIndexOf('.')) : null;
						this.addSymbol(m[1].replace(':', '.'), { path, line: i + 1, params, kind: 'function', method: m[1].includes(':') }, tops);
						return;
					}
					if (/^(end|local\s+function|function)\b/.test(line)) {
						owner = /^end\b/.test(line) ? null : owner;
					}
					// self.Name = ... inside a class's method: a field of the class, for self. in its other methods (here or in other files).
					m = owner && /^\s+self\.([A-Za-z_]\w*)\s*=(?!=)\s*(.*)$/.exec(line);
					if (m && !ownFields.has(owner + '.' + m[1])) {
						ownFields.add(owner + '.' + m[1]);
						const isFunction = /^function\s*\(([^)]*)\)/.exec(m[2]);
						this.addSymbol(owner + '.' + m[1], { path, line: i + 1, params: isFunction ? isFunction[1].split(',').map((p) => p.trim()).filter(Boolean) : null, kind: isFunction ? 'function' : 'field', value: m[2].slice(0, 80), viaSelf: true }, tops);
						return;
					}
					m = /^([A-Za-z_][\w.]*)\s*=(?!=)\s*(.*)$/.exec(line);
					if (m && !KEYWORDS.includes(m[1])) {
						const isFunction = /^function\s*\(([^)]*)\)/.exec(m[2]);
						this.addSymbol(m[1], { path, line: i + 1, params: isFunction ? isFunction[1].split(',').map((p) => p.trim()).filter(Boolean) : null, kind: isFunction ? 'function' : /^\{/.test(m[2]) ? 'table' : 'value', value: m[2].slice(0, 80) }, tops);
					}
				});
			}
			this.symbolNames = [...tops];
		}

		addSymbol(name, info, tops) {
			if (ENTITY_CALLBACKS[name]) {
				return; // every entity script defines these
			}
			if (!this.symbols.has(name)) {
				this.symbols.set(name, []);
			}
			this.symbols.get(name).push(info);
			tops.add(name.split('.')[0]);
		}

		// ---- the game's classes ----

		cls(name) {
			return (this.api && this.api.classes[name]) || null;
		}

		// A member of a class or its bases: {member, owner} or null.
		member(clsName, name) {
			for (let c = clsName, guard = 0; c && guard < 30; guard++) {
				const cls = this.cls(c);
				if (!cls) {
					return null;
				}
				if (cls.members[name]) {
					return { member: cls.members[name], owner: c };
				}
				c = cls.base;
			}
			return null;
		}

		// Every member of a class and its bases: Map(name -> {member, owner}), the class's own first.
		members(clsName) {
			const out = new Map();
			for (let c = clsName, guard = 0; c && guard < 30; guard++) {
				const cls = this.cls(c);
				if (!cls) {
					break;
				}
				for (const [name, member] of Object.entries(cls.members)) {
					if (!out.has(name)) {
						out.set(name, { member, owner: c });
					}
				}
				c = cls.base;
			}
			return out;
		}

		isA(clsName, ancestor) {
			for (let c = clsName, guard = 0; c && guard < 30; guard++) {
				if (c === ancestor) {
					return true;
				}
				const cls = this.cls(c);
				c = cls && cls.base;
			}
			return false;
		}

		// ---- what a script is ----

		// {kind: "entity" | "activity", selfType, presets}: entity scripts are the ones presets list in ScriptPath; activity scripts define
		// StartActivity or UpdateActivity as methods.
		scriptInfo(path, text) {
			const lowerPath = (path || '').toLowerCase();
			const presets = this.presetsUsing(lowerPath);
			const activityByPreset = presets.some((p) => /Activity|GAScripted/.test(p.cls));
			const activityByText = /^\s*function\s+\w+\s*:\s*(StartActivity|UpdateActivity)\b/m.test(text || '');
			if (activityByPreset || activityByText) {
				const className = (/^\s*function\s+(\w+)\s*:\s*(?:StartActivity|UpdateActivity)\b/m.exec(text || '') || [])[1];
				return { kind: 'activity', selfType: 'GameActivity', className, presets };
			}
			// The class every preset using it is (the most specific one they all share).
			let selfType = null;
			for (const preset of presets) {
				if (!this.cls(preset.cls)) {
					continue;
				}
				if (!selfType || this.isA(selfType, preset.cls)) {
					selfType = preset.cls;
				} else if (!this.isA(preset.cls, selfType)) {
					let c = selfType;
					while (c && !this.isA(preset.cls, c)) {
						c = this.cls(c) && this.cls(c).base;
					}
					selfType = c || 'MovableObject';
				}
			}
			if (!presets.length && !/^function\s+(Create|Update|Destroy|ThreadedUpdate|OnCollideWithMO|OnCollideWithTerrain|UpdateAI|OnFire|OnAttach)\s*\(\s*self\b/m.test(text || '')) {
				return { kind: 'module', selfType: 'any', presets }; // a library of functions other scripts load
			}
			return { kind: 'entity', selfType: selfType || 'MovableObject', presets };
		}

		// The presets that list a script in ScriptPath. Worked out once each time the presets are re-read.
		presetsUsing(lowerPath) {
			const lib = this.app.lib;
			if (!lib) {
				return [];
			}
			if (this._usersFor !== lib.presets) {
				this._usersFor = lib.presets;
				this._users = new Map();
				for (const preset of lib.presets) {
					for (const c of preset.node.children) {
						if (c.key === 'ScriptPath' && c.value) {
							const key = c.value.replace(/\\/g, '/').toLowerCase();
							if (!this._users.has(key)) {
								this._users.set(key, []);
							}
							this._users.get(key).push(preset);
						}
					}
				}
			}
			return this._users.get(lowerPath) || [];
		}

		// ---- reading expressions ----

		// The expression ending at "end" in text ("self.Head:GetParent().Pos"), found by walking back over names, dots and brackets.
		static exprBefore(text, end) {
			let i = end;
			let depth = 0;
			while (i > 0) {
				const c = text[i - 1];
				if (c === ')' || c === ']') {
					depth++;
				} else if (c === '(' || c === '[') {
					if (depth === 0) {
						break;
					}
					depth--;
				} else if (depth === 0 && !/[\w.:]/.test(c)) {
					if (/\s/.test(c)) {
						// Allow "foo :Bar" spacing only directly before . or :
						let j = i - 1;
						while (j > 0 && /\s/.test(text[j - 1])) {
							j--;
						}
						if (/[.:]/.test(text[i]) && /[\w)\]]/.test(text[j - 1] || '')) {
							i = j;
							continue;
						}
					}
					break;
				} else if (depth > 0 && c === '\n') {
					// keep going inside brackets
				}
				i--;
			}
			return text.slice(i, end).trim();
		}

		// "self.Head:GetParent().Pos" -> [{name: "self"}, {sep: ".", name: "Head"}, {sep: ":", name: "GetParent", call: true}, {sep: ".", name: "Pos"}]
		static parseChain(expr) {
			const segs = [];
			let i = 0;
			const skipBracket = (open) => {
				const close = open === '(' ? ')' : ']';
				let depth = 0;
				const start = i;
				for (; i < expr.length; i++) {
					if (expr[i] === open) {
						depth++;
					} else if (expr[i] === close && --depth === 0) {
						i++;
						return expr.slice(start + 1, i - 1);
					}
				}
				return expr.slice(start + 1);
			};
			while (i < expr.length) {
				const c = expr[i];
				if (c === '.' || c === ':') {
					i++;
					const m = /^\s*([A-Za-z_]\w*)?/.exec(expr.slice(i));
					segs.push({ sep: c, name: m[1] || '' });
					i += m[0].length;
				} else if (c === '(') {
					const args = skipBracket('(');
					if (segs.length) {
						segs[segs.length - 1].call = true;
						segs[segs.length - 1].args = args;
					}
				} else if (c === '[') {
					skipBracket('[');
					segs.push({ sep: '[', name: '' });
				} else if (/[A-Za-z_]/.test(c) && !segs.length) {
					const m = /^[A-Za-z_]\w*/.exec(expr.slice(i));
					segs.push({ name: m[0] });
					i += m[0].length;
				} else if (/\s/.test(c)) {
					i++;
				} else {
					return null;
				}
			}
			return segs;
		}

		// The type of an expression at "offset" in a script: a class name, "number", "string", "boolean", "X[]", "class:X" (a class itself,
		// like Vector or Actor), "lib:math", "global:rte" (a table a script defines), or "any".
		typeOfExpr(expr, ctx, depth) {
			const segs = LuaIntel.parseChain(expr);
			if (!segs || !segs.length) {
				if (/^["']/.test(expr)) {
					return 'string';
				}
				if (/^-?\d/.test(expr)) {
					return 'number';
				}
				return 'any';
			}
			let type = this.typeOfName(segs[0], ctx, depth || 0);
			let globalPath = segs[0].name;
			for (let k = 1; k < segs.length; k++) {
				const seg = segs[k];
				if (seg.sep === '[') {
					type = elementOf(type);
					globalPath = null;
					continue;
				}
				globalPath = globalPath ? globalPath + '.' + seg.name : null;
				type = this.typeOfMember(type, seg, ctx, globalPath);
			}
			return type;
		}

		typeOfName(seg, ctx, depth) {
			const name = seg.name;
			if (seg.call) {
				// A call: a game function (CreateAHuman), a class (Vector(...)), or a built-in.
				const f = this.api.functions[name];
				if (f) {
					return f.overloads[0].returns;
				}
				if (this.cls(name)) {
					return name;
				}
				if (BUILTINS[name]) {
					return BUILTINS[name].returns;
				}
				const local = ctx && this.localType(name, ctx, depth);
				return local && local.startsWith('fn:') ? local.slice(3) : 'any';
			}
			if (name === 'self' && ctx) {
				return this.selfAt(ctx);
			}
			if (ctx) {
				const local = this.localType(name, ctx, depth);
				if (local) {
					return local;
				}
			}
			if (this.api.globals[name]) {
				return this.api.globals[name];
			}
			if (this.cls(name)) {
				return 'class:' + name;
			}
			if (LIBS[name]) {
				return 'lib:' + name;
			}
			if (this.symbols.has(name) || this.symbolNames.includes(name)) {
				return 'global:' + name;
			}
			return 'any';
		}

		typeOfMember(type, seg, ctx, globalPath) {
			if (type.startsWith('class:')) {
				const found = this.member(type.slice(6), seg.name);
				return found ? found.member.type || (found.member.overloads && found.member.overloads[0].returns) || 'any' : 'any';
			}
			if (type.startsWith('lib:')) {
				const entry = LIBS[type.slice(4)][seg.name];
				return entry ? (seg.call ? entry.returns : entry.kind === 'const' ? entry.type : 'any') : 'any';
			}
			if (type.startsWith('global:')) {
				return globalPath && (this.symbols.has(globalPath) || this.symbolNames.includes(globalPath)) ? 'global:' + globalPath : 'any';
			}
			const found = this.member(type, seg.name);
			if (!found) {
				return 'any';
			}
			const m = found.member;
			if (m.kind === 'method') {
				return seg.call ? this.pickOverload(m, seg.args).returns : 'any';
			}
			return m.type || 'any';
		}

		pickOverload(member, args) {
			const count = args === undefined ? -1 : args.trim() ? splitArgs(args).length : 0;
			return member.overloads.find((o) => o.params && o.params.length === count) || member.overloads[0];
		}

		// The type of a local variable or parameter, from the text before ctx.offset: the nearest "local name = ...", "name = ...",
		// "for name in ... do", a ---@type or ---@param comment, or a callback's parameter. (Nearest in the text, not by scope: close enough
		// for scripts, and quick.)
		localType(name, ctx, depth) {
			if (depth > 6) {
				return null;
			}
			for (const c of this.declarations(name, ctx.text)) {
				if (c.at < ctx.offset) {
					const type = c.handler(c.m, ctx, depth);
					if (type !== undefined) {
						return type;
					}
				}
			}
			return null;
		}

		// Every place in the text that could say what "name" is, nearest the end first. Kept for the text it was worked out for, since checks
		// and completion ask about the same names over and over.
		declarations(name, text) {
			if (this._declText !== text) {
				this._declText = text;
				this._decls = new Map();
			}
			if (this._decls.has(name)) {
				return this._decls.get(name);
			}
			const esc = name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
			const candidates = [];
			const push = (re, handler) => {
				for (const m of text.matchAll(re)) {
					candidates.push({ at: m.index, m, handler });
				}
			};
			const annotationAbove = (at) => {
				const above = text.slice(Math.max(0, at - 600), at).split('\n');
				above.pop();
				for (let i = above.length - 1; i >= 0 && /^\s*---/.test(above[i]); i--) {
					const m = new RegExp('---@param\\s+' + esc + '\\??\\s+([\\w.\\[\\]]+)').exec(above[i]);
					if (m) {
						return m[1];
					}
				}
				return null;
			};
			push(new RegExp('---@type\\s+([\\w.\\[\\]]+)[^\\n]*\\n\\s*(?:local\\s+)?' + esc + '\\b', 'g'), (m) => m[1]);
			push(new RegExp('(?:^|[\\s;])(?:local\\s+)?' + esc + '\\s*=(?!=)\\s*([^\\n;]+)', 'g'), (m, ctx, depth) => {
				const rhs = m[1].replace(/\s+(or|and)\s+.*$/, '').replace(/--.*$/, '').trim();
				if (/^function\b/.test(rhs)) {
					return 'any';
				}
				if (/^\{/.test(rhs)) {
					return 'table';
				}
				if (/^["']/.test(rhs)) {
					return 'string';
				}
				if (/^-?\d/.test(rhs)) {
					return 'number';
				}
				if (/^(true|false)\b/.test(rhs) || /^not\s/.test(rhs)) {
					return 'boolean';
				}
				return this.typeOfExpr(rhs, Object.assign({}, ctx, { offset: m.index }), depth + 1);
			});
			push(new RegExp('for\\s+' + esc + '\\s+in\\s+(.+?)\\s+do\\b', 'g'), (m, ctx, depth) => elementOf(this.typeOfExpr(m[1], Object.assign({}, ctx, { offset: m.index }), depth + 1)));
			push(new RegExp('for\\s+\\w+\\s*,\\s*' + esc + '\\s+in\\s+i?pairs\\s*\\((.+?)\\)\\s+do\\b', 'g'), (m, ctx, depth) => elementOf(this.typeOfExpr(m[1], Object.assign({}, ctx, { offset: m.index }), depth + 1)));
			push(new RegExp('for\\s+' + esc + '\\s*=', 'g'), () => 'number');
			// A function's parameter: what the game passes to a callback, or what a ---@param comment above it says.
			push(new RegExp('function\\s*(?:[\\w.]+\\s*([:.])\\s*)?(\\w*)\\s*\\(([^)]*)\\)', 'g'), (m, ctx) => {
				const params = m[3].split(',').map((p) => p.trim());
				const index = params.indexOf(name);
				if (index < 0) {
					return undefined;
				}
				const annotated = annotationAbove(m.index);
				if (annotated) {
					return annotated;
				}
				const isMethod = m[1] === ':';
				const callback = (isMethod ? ACTIVITY_CALLBACKS : ENTITY_CALLBACKS)[m[2]];
				if (!callback || (isMethod && ctx.kind !== 'activity')) {
					return 'any';
				}
				if (!isMethod && index === 0) {
					return ctx.selfType;
				}
				const param = callback.params[isMethod ? index : index - 1];
				return param ? param.type : 'any';
			});
			candidates.sort((x, y) => y.at - x.at);
			this._decls.set(name, candidates);
			return candidates;
		}

		// What "self" is at ctx.offset: the script's object inside the game's callbacks (and helpers that take self first), the activity inside
		// its own methods, and unknown inside other tables' methods ("function PID:Update()" is a Lua class of the script's own).
		selfAt(ctx) {
			if (!ctx.selfType || ctx.selfType === 'any') {
				return 'any';
			}
			if (ctx.analysis) {
				// The self in scope there: the script's own (in its callbacks and the activity's methods), or another table's.
				return keyAt(ctx.analysis, 'self', ctx.offset) === ctx.selfKey ? ctx.selfType : 'any';
			}
			if (this._headText !== ctx.text) {
				this._headText = ctx.text;
				this._heads = [...ctx.text.matchAll(/^(?:local\s+)?function\s+([\w.]*?)(?:([.:])(\w+))?\s*\(([^)]*)\)/gm)].map((m) => ({ at: m.index, table: m[2] ? m[1] : null, sep: m[2] || null, name: m[2] ? m[3] : m[1], selfFirst: /^\s*self\b/.test(m[4]) }));
			}
			let head = null;
			for (const h of this._heads) {
				if (h.at < ctx.offset) {
					head = h;
				} else {
					break;
				}
			}
			if (!head || (ctx.snippet && !/^\s*(local\s+)?function\b/m.test(ctx.text.slice(0, ctx.offset)))) {
				return ctx.snippet ? ctx.selfType : 'any'; // a snippet is the inside of one of the script's functions
			}
			if (ctx.kind === 'activity') {
				return head.sep === ':' && (!ctx.className || head.table === ctx.className) ? ctx.selfType : head.selfFirst ? ctx.selfType : 'any';
			}
			return head.selfFirst ? ctx.selfType : 'any';
		}

		// ---- completion ----

		// Completion items at an offset of a script: [{label, kind, detail, doc, insert, snippet, sort}]
		complete(path, text, offset, opts) {
			const { info, ctx } = this.context(path, text, offset, opts);
			const lineStart = text.lastIndexOf('\n', offset - 1) + 1;
			const line = text.slice(lineStart, offset);

			const str = stringContext(line);
			if (str) {
				return this.completeInString(str, text.slice(0, lineStart) + line, info);
			}
			if (/--/.test(line.replace(/(["'])(?:\\.|(?!\1).)*\1/g, ''))) {
				return [];
			}

			// After "function " at the start of a line: the callbacks the game calls.
			let m = /^\s*function\s+(\w*)$/.exec(line);
			if (m && info.kind === 'entity') {
				return Object.entries(ENTITY_CALLBACKS).map(([name, cb]) => ({
					label: name, kind: 'snippet', detail: 'called by the game', doc: cb.doc,
					insert: name + '(' + ['self', ...cb.params.map((p) => p.name)].join(', ') + ')\n\t$0\nend', snippet: true, sort: '0',
				}));
			}
			m = /^\s*function\s+(\w+)\s*:\s*(\w*)$/.exec(line);
			if (m && info.kind === 'activity') {
				return Object.entries(ACTIVITY_CALLBACKS).map(([name, cb]) => ({
					label: name, kind: 'snippet', detail: 'called by the game', doc: cb.doc,
					insert: name + '(' + cb.params.map((p) => p.name).join(', ') + ')\n\t$0\nend', snippet: true, sort: '0',
				}));
			}

			// After a "." or ":": the members of what's before it.
			m = /([.:])\s*(\w*)$/.exec(line);
			if (m && !/\.\.\s*\w*$/.test(line)) {
				const sepAt = offset - m[0].length;
				const expr = LuaIntel.exprBefore(text, sepAt);
				if (expr) {
					return this.completeMembers(this.typeOfExpr(expr, ctx), m[1], ctx, expr);
				}
			}

			// A name: locals, the game's globals and functions, classes, Lua's own, and what other scripts define.
			return this.completeNames(ctx, info);
		}

		// The members of what's before a "." or ":": the game's (by its type) and what scripts define on it (a class's methods, and the
		// fields its methods set on self, here and in other scripts; a table's functions; what a required file returns).
		completeMembers(type, sep, ctx, expr) {
			const items = this.completeTypeMembers(type, sep, ctx, expr);
			const key = ctx && this.keyOfExpr(ctx, expr, ctx.offset - 1);
			if (!key) {
				return items;
			}
			const have = new Map(items.map((i) => [i.label, i]));
			for (const [name, m] of this.membersOfKey(ctx, key)) {
				const isFunction = m.kind === 'function';
				if ((sep === ':' && !isFunction) || name === '') {
					continue;
				}
				const params = isFunction ? (m.params || []).slice(sep === ':' && !m.method && (m.params || [])[0] === 'self' ? 1 : 0) : null;
				const item = {
					label: name,
					kind: isFunction ? (sep === ':' ? 'method' : 'function') : m.kind === 'table' ? 'module' : 'field',
					detail: isFunction ? name + '(' + params.join(', ') + ')' : m.value ? '= ' + m.value : m.kind,
					doc: 'From ' + m.where,
					insert: isFunction ? name + '(' + (params.length ? '$0' : '') + ')' : name,
					snippet: isFunction,
					sort: '0',
				};
				const old = have.get(name);
				if (!old) {
					items.push(item);
					have.set(name, item);
				} else if (old.detail === 'set in this script') {
					Object.assign(old, item);
				}
			}
			return items;
		}

		completeTypeMembers(type, sep, ctx, expr) {
			const items = [];
			if (type.startsWith('lib:')) {
				for (const [name, entry] of Object.entries(LIBS[type.slice(4)])) {
					items.push(entry.kind === 'const' ? { label: name, kind: 'constant', detail: entry.type } : functionItem(name, entry, sep === ':'));
				}
				return items;
			}
			if (type.startsWith('class:')) {
				const clsName = type.slice(6);
				for (const [name, { member, owner }] of this.members(clsName)) {
					if (member.kind === 'const') {
						items.push({ label: name, kind: 'constant', detail: clsName + '.' + name + ' (number)', doc: member.doc, sort: '1' });
					}
				}
				return items;
			}
			if (type.startsWith('global:')) {
				const prefix = type.slice(7) + '.';
				const seen = new Set();
				for (const [name, list] of this.symbols) {
					if (name.startsWith(prefix)) {
						const rest = name.slice(prefix.length).split('.')[0];
						if (!seen.has(rest)) {
							seen.add(rest);
							const s = list[0];
							items.push({ label: rest, kind: s.kind === 'function' ? (s.method ? 'method' : 'function') : 'field', detail: s.kind === 'function' ? '(' + (s.params || []).join(', ') + ')' : s.value || '', doc: 'From ' + s.path + ':' + s.line, insert: s.kind === 'function' ? rest + '(' + (s.params && s.params.length ? '$0' : '') + ')' : rest, snippet: s.kind === 'function' });
						}
					}
				}
				return items;
			}
			if (!this.cls(type)) {
				// Unknown type: the fields this script sets on the same name.
				return this.fieldsSetOn(expr, ctx.text).map((name) => ({ label: name, kind: 'field', detail: 'set in this script' }));
			}
			for (const [name, { member, owner }] of this.members(type)) {
				const inherited = owner !== type ? 'From ' + owner + '. ' : '';
				if (member.kind === 'method') {
					if (sep === '.' && !/^[A-Z]/.test(name)) {
						continue;
					}
					const o = member.overloads[0];
					items.push({
						label: name, kind: 'method', detail: signature(name, o, member.overloads.length), doc: inherited + (member.doc || ''),
						insert: name + '(' + (o.params && o.params.length ? '$0' : '') + ')', snippet: true, sort: sep === ':' ? '0' : '2',
					});
				} else if (member.kind === 'const') {
					if (sep === '.') {
						items.push({ label: name, kind: 'constant', detail: 'number', doc: member.doc, sort: '3' });
					}
				} else if (sep === '.') {
					items.push({ label: name, kind: member.kind === 'field' ? 'field' : 'property', detail: (member.type || 'any') + (member.readonly ? ' (read only)' : ''), doc: inherited + (member.doc || ''), sort: '0' });
				}
			}
			if (sep === '.' && ctx && expr === 'self') {
				for (const name of this.fieldsSetOn('self', ctx.text)) {
					if (!items.some((i) => i.label === name)) {
						items.push({ label: name, kind: 'field', detail: 'set in this script', sort: '0' });
					}
				}
			}
			return items;
		}

		// Names written as "expr.name = ..." anywhere in the text.
		fieldsSetOn(expr, text) {
			const esc = expr.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
			const out = new Set();
			for (const m of text.matchAll(new RegExp('(?:^|[^\\w.])' + esc + '\\.(\\w+)\\s*=(?!=)', 'gm'))) {
				out.add(m[1]);
			}
			return [...out];
		}

		completeNames(ctx, info) {
			const items = [];
			const seen = new Set();
			const add = (item) => {
				if (!seen.has(item.label)) {
					seen.add(item.label);
					items.push(item);
				}
			};
			if (ctx.analysis) {
				// What's in scope here, and (in a snippet) the script's own top-level locals.
				const visible = (a, offset, rootOnly) => {
					for (const d of a.decls) {
						if (!d.implicit && d.name !== 'self' && (rootOnly ? d.scopeStart === 0 && d.scopeEnd === a.text.length : d.at < offset && d.scopeStart <= offset && offset <= d.scopeEnd)) {
							const def = (a.defs.get(d.key) || [])[0];
							add({ label: d.name, kind: def && def.kind === 'function' ? 'function' : 'variable', detail: def && def.kind === 'function' ? 'local ' + d.name + '(' + (def.params || []).join(', ') + ')' : d.kind === 'param' ? 'parameter' : 'local', sort: '0' });
						}
					}
				};
				visible(ctx.analysis, ctx.offset, false);
				if (ctx.host) {
					visible(ctx.host, 0, true);
				}
				for (const [key, list] of ctx.analysis.defs) {
					if (key.startsWith('G:') && !key.includes('.') && list[0].kind === 'function') {
						add({ label: key.slice(2), kind: 'function', detail: key.slice(2) + '(' + (list[0].params || []).join(', ') + ')', doc: 'Defined in this script', sort: '1' });
					}
				}
			} else {
				for (const name of localNames(ctx.text, ctx.offset)) {
					add({ label: name, kind: 'variable', detail: 'local', sort: '0' });
				}
			}
			if (info.kind === 'entity' || info.kind === 'activity') {
				add({ label: 'self', kind: 'variable', detail: info.selfType, doc: info.kind === 'entity' ? 'The object running this script.' : 'The activity.', sort: '0' });
			}
			for (const [name, type] of Object.entries(this.api.globals)) {
				add({ label: name, kind: 'module', detail: type, doc: (this.cls(type) && this.cls(type).doc) || '', sort: '1' });
			}
			for (const [name, f] of Object.entries(this.api.functions)) {
				if (name.startsWith('_')) {
					continue;
				}
				const o = f.overloads[0];
				add({ label: name, kind: 'function', detail: signature(name, o, f.overloads.length), doc: f.doc || '', insert: name + '(' + (o.params && o.params.length ? '$0' : '') + ')', snippet: true, sort: '2' });
			}
			for (const [name, cls] of Object.entries(this.api.classes)) {
				const ctor = cls.ctors && cls.ctors.length;
				add({ label: name, kind: 'class', detail: ctor ? name + '(...)' : 'class', doc: cls.doc || '', sort: '3' });
			}
			for (const [name, entry] of Object.entries(BUILTINS)) {
				add(functionItem(name, entry, false, '2'));
			}
			for (const name of Object.keys(LIBS)) {
				add({ label: name, kind: 'module', detail: 'Lua library', sort: '3' });
			}
			for (const name of this.symbolNames) {
				const s = (this.symbols.get(name) || [])[0];
				add({ label: name, kind: s && s.kind === 'function' ? 'function' : 'module', detail: s ? 'from ' + s.path : 'defined by a script', doc: s ? s.path + ':' + s.line : '', sort: '4' });
			}
			for (const kw of KEYWORDS) {
				add({ label: kw, kind: 'keyword', sort: '5' });
			}
			return items;
		}

		// Inside a string: preset names for CreateX("..."), classes and presets for PresetMan:GetPreset, modules, group names, script paths.
		completeInString(str, before, info) {
			const lib = this.app.lib;
			const call = callBefore(before.slice(0, before.length - str.typed.length - 1));
			if (!call || !lib) {
				return [];
			}
			const presetItems = (cls) => {
				const seen = new Set();
				const out = [];
				for (const p of lib.presets) {
					if ((!cls || p.cls === cls || this.isA(p.cls, cls)) && p.topLevel && !seen.has(p.name)) {
						seen.add(p.name);
						out.push({ label: p.name, kind: 'value', detail: p.cls + ' · ' + p.module, doc: p.doc.path + ':' + p.node.line });
					}
				}
				return out;
			};
			const moduleItems = () => lib.modules.map((m) => ({ label: m.name, kind: 'module', detail: m.title || '' }));
			const groupItems = () => {
				const groups = new Set();
				for (const doc of lib.docs.values()) {
					for (const node of doc.walk()) {
						if (node.key === 'AddToGroup' && node.value) {
							groups.add(node.value);
						}
					}
				}
				return [...groups].sort().map((g) => ({ label: g, kind: 'value', detail: 'group' }));
			};
			const m = /^(Create|Random)(\w+)$/.exec(call.name);
			if (m && this.cls(m[2])) {
				if (call.arg === 0) {
					return m[1] === 'Create' ? presetItems(m[2]) : groupItems();
				}
				return moduleItems();
			}
			if (/GetPreset|GetLoadout|ReloadEntityPreset/.test(call.name)) {
				if (call.arg === 0) {
					return Object.keys(this.api.classes).filter((c) => this.isA(c, 'Entity')).map((c) => ({ label: c, kind: 'class' }));
				}
				if (call.arg === 1) {
					return presetItems(call.firstString || null);
				}
				return moduleItems();
			}
			if (/Group/.test(call.name)) {
				return groupItems();
			}
			if (/^(dofile|loadfile|AddScript|LoadScript|ReloadScripts|EnableScript|DisableScript|HasScript)$/.test(call.name)) {
				return [...this.scripts.values()].map((s) => ({ label: s.path, kind: 'file' }));
			}
			if (/ModuleID|ModuleName|Module$/.test(call.name)) {
				return moduleItems();
			}
			return [];
		}

		// ---- signature help ----

		// {signatures: [{label, doc, params: [{label, doc}]}], active, activeParam} for the call the cursor is in, or null.
		signatureAt(path, text, offset, opts) {
			const { ctx } = this.context(path, text, offset, opts);
			// The innermost unclosed "(" before the cursor.
			let depth = 0;
			let commas = 0;
			let i = offset;
			for (; i > 0 && offset - i < 4000; i--) {
				const c = text[i - 1];
				if (c === ')' || c === ']' || c === '}') {
					depth++;
				} else if (c === '(' || c === '[' || c === '{') {
					if (depth === 0) {
						if (c !== '(') {
							return null;
						}
						break;
					}
					depth--;
				} else if (c === ',' && depth === 0) {
					commas++;
				}
			}
			if (i <= 0 || text[i - 1] !== '(') {
				return null;
			}
			const callee = LuaIntel.exprBefore(text, i - 1);
			const overloads = this.overloadsOf(callee, ctx);
			if (!overloads || !overloads.list.length) {
				return null;
			}
			const signatures = overloads.list.map((o) => {
				const params = o.params || [{ name: '...', type: 'any' }];
				return {
					label: overloads.name + '(' + params.map((p) => p.name + (p.optional ? '?' : '') + ': ' + (p.type || 'any')).join(', ') + ')' + (o.returns && o.returns !== 'nil' ? ' → ' + o.returns : ''),
					doc: (overloads.doc || '') + (o.returnsDoc ? '\n\nReturns ' + o.returnsDoc : ''),
					params: params.map((p) => ({ label: p.name + (p.optional ? '?' : '') + ': ' + (p.type || 'any'), doc: p.doc || '' })),
				};
			});
			const active = Math.max(0, overloads.list.findIndex((o) => (o.params || []).length > commas));
			return { signatures, active, activeParam: commas };
		}

		// Something a script defines, by the expression that names it ("self:SpawnCraft", "helper", "self.Timer"): {name, kind, params,
		// callParams (the parameters as this call passes them: no self for obj:Method()), value, doc, where}, or null.
		userThing(ctx, expr) {
			const key = this.keyOfExpr(ctx, expr, ctx.offset);
			const found = key && this.locate(ctx, key);
			if (!found) {
				return null;
			}
			const segs = LuaIntel.parseChain(expr);
			const last = segs[segs.length - 1];
			const d = this.describeFound(ctx, found, segs.map((x, i) => (i ? x.sep : '') + x.name).join(''));
			if (key.startsWith('G:') && d.kind === 'function') {
				// By the name it's defined with: "MyMission:SpawnWave", not "self:SpawnWave".
				d.name = key.slice(2).replace(/\.(\w+)$/, (m, n) => (d.kind === 'function' && (d.method || last.sep === ':') ? ':' : '.') + n);
			}
			if (d.kind === 'function') {
				let params = d.params || [];
				if (last.sep === ':' && !d.method && params[0] === 'self') {
					params = params.slice(1);
				} else if (last.sep === '.' && d.method) {
					params = ['self', ...params];
				}
				d.callParams = params;
			}
			return d;
		}

		// Markdown for something a script defines.
		userHover(d) {
			const head = d.kind === 'function' ? (d.local ? 'local ' : '') + 'function ' + d.name + '(' + (d.callParams || d.params || []).join(', ') + ')' : d.name + (d.value ? ' = ' + d.value : d.kind === 'table' ? ' = {…}' : '');
			return '```lua\n' + head + '\n```\n' + (d.doc ? d.doc + '\n\n' : '') + '*' + (d.kind === 'function' ? 'Defined' : 'First set') + ' at ' + d.where + '*';
		}

		// The overloads a callee expression can mean: {name, doc, list}.
		overloadsOf(callee, ctx) {
			const segs = LuaIntel.parseChain(callee);
			if (!segs || !segs.length) {
				return null;
			}
			const last = segs[segs.length - 1];
			const user = () => {
				const d = this.userThing(ctx, callee);
				return d && d.kind === 'function' ? { name: d.name, doc: (d.doc ? d.doc + '\n\n' : '') + 'Defined at ' + d.where, list: [{ params: d.callParams.map((p) => ({ name: p, type: 'any' })), returns: null }] } : null;
			};
			if (segs.length === 1) {
				const name = last.name;
				const local = ctx.analysis && keyAt(ctx.analysis, name, ctx.offset);
				if (local && !local.startsWith('G:')) {
					return user();
				}
				if (this.api.functions[name]) {
					return { name, doc: this.api.functions[name].doc, list: this.api.functions[name].overloads };
				}
				const cls = this.cls(name);
				if (cls && cls.ctors && cls.ctors.length) {
					return { name, doc: cls.doc, list: cls.ctors.map((params) => ({ params, returns: name })) };
				}
				if (BUILTINS[name]) {
					return { name, doc: BUILTINS[name].doc, list: [BUILTINS[name]] };
				}
				const mine = user();
				if (mine) {
					return mine;
				}
				const sym = (this.symbols.get(name) || [])[0];
				if (sym && sym.params) {
					return { name, doc: 'From ' + sym.path + ':' + sym.line, list: [{ params: sym.params.map((p) => ({ name: p, type: 'any' })), returns: 'any' }] };
				}
				return null;
			}
			const baseExpr = callee.slice(0, callee.length - last.name.length).replace(/\s*[.:]\s*$/, '');
			const baseType = this.typeOfExpr(baseExpr, ctx);
			if (baseType.startsWith('lib:')) {
				const entry = LIBS[baseType.slice(4)][last.name];
				return entry && entry.kind === 'function' ? { name: baseType.slice(4) + '.' + last.name, doc: entry.doc, list: [entry] } : null;
			}
			if (baseType.startsWith('global:')) {
				const mine = user();
				if (mine) {
					return mine;
				}
				const sym = (this.symbols.get(baseType.slice(7) + '.' + last.name) || [])[0];
				if (sym && sym.params) {
					const params = sym.method || last.sep !== ':' ? sym.params : sym.params.slice(1);
					return { name: last.name, doc: 'From ' + sym.path + ':' + sym.line, list: [{ params: params.map((p) => ({ name: p, type: 'any' })), returns: 'any' }] };
				}
				return null;
			}
			const found = this.member(baseType, last.name);
			if (found && found.member.kind === 'method') {
				return { name: baseType + (last.sep || ':') + last.name, doc: found.member.doc, list: found.member.overloads };
			}
			return user();
		}

		// ---- hover ----

		// Markdown describing the name ending at "end" (the word under the mouse), or null.
		hover(path, text, start, end, opts) {
			const str = this.stringAt(text, start, path);
			if (str) {
				return this.hoverString(str);
			}
			const { info, ctx } = this.context(path, text, start, opts);
			const word = text.slice(start, end);
			const expr = LuaIntel.exprBefore(text, end);
			const segs = LuaIntel.parseChain(expr);
			if (!segs || !segs.length) {
				return null;
			}
			if (segs.length === 1) {
				if (word === 'self') {
					return '```lua\nself: ' + info.selfType + '\n```\n' + (info.kind === 'entity' ? 'The object running this script' + (info.presets.length ? ', used by ' + info.presets.slice(0, 4).map((p) => p.name).join(', ') + (info.presets.length > 4 ? ' and ' + (info.presets.length - 4) + ' more' : '') : '') + '.' : 'The activity.');
				}
				const localKey = ctx.analysis && keyAt(ctx.analysis, word, start);
				const mine = localKey && !localKey.startsWith('G:') ? this.userThing(ctx, word) : null;
				const local = this.localType(word, ctx, 0);
				if (mine && (mine.kind === 'function' || !local || local === 'any' || local === 'table')) {
					return this.userHover(Object.assign(mine, mine.kind === 'function' ? { local: true } : { name: 'local ' + mine.name }));
				}
				if (local) {
					return '```lua\nlocal ' + word + ': ' + prettyType(local) + '\n```';
				}
				if (this.api.globals[word]) {
					const cls = this.cls(this.api.globals[word]);
					return '```lua\n' + word + ': ' + this.api.globals[word] + '\n```\n' + ((cls && cls.doc) || '');
				}
				if (this.api.functions[word]) {
					const f = this.api.functions[word];
					return '```lua\n' + f.overloads.map((o) => signature(word, o)).join('\n') + '\n```\n' + (f.doc || '');
				}
				if (this.cls(word)) {
					const cls = this.cls(word);
					return '```lua\nclass ' + word + (cls.base ? ' : ' + cls.base : '') + '\n```\n' + (cls.doc || '');
				}
				if (BUILTINS[word]) {
					return '```lua\n' + signature(word, BUILTINS[word]) + '\n```\n' + (BUILTINS[word].doc || '');
				}
				const callback = (info.kind === 'activity' ? ACTIVITY_CALLBACKS : ENTITY_CALLBACKS)[word];
				if (callback) {
					return '**' + word + '** is called by the game. ' + callback.doc;
				}
				const global = this.userThing(ctx, word);
				if (global) {
					return this.userHover(global);
				}
				const sym = this.symbols.get(word);
				if (sym) {
					return '```lua\n' + word + (sym[0].params ? '(' + sym[0].params.join(', ') + ')' : '') + '\n```\nDefined in ' + sym.map((s) => s.path + ':' + s.line).slice(0, 3).join(', ');
				}
				return null;
			}
			const last = segs[segs.length - 1];
			const baseExpr = expr.slice(0, expr.length - last.name.length).replace(/\s*[.:]\s*$/, '');
			const baseType = this.typeOfExpr(baseExpr, ctx);
			const mine = () => {
				const d = this.userThing(ctx, expr);
				if (!d) {
					return null;
				}
				const callback = ctx.kind === 'activity' && last.sep === ':' && ACTIVITY_CALLBACKS[last.name];
				return this.userHover(d) + (callback ? '\n\nThe game calls it: ' + callback.doc : '');
			};
			if (baseType.startsWith('global:')) {
				const user = mine();
				if (user) {
					return user;
				}
				const sym = this.symbols.get(baseType.slice(7) + '.' + last.name);
				return sym ? '```lua\n' + baseType.slice(7) + '.' + last.name + (sym[0].params ? '(' + sym[0].params.join(', ') + ')' : sym[0].value ? ' = ' + sym[0].value : '') + '\n```\nDefined in ' + sym[0].path + ':' + sym[0].line : null;
			}
			if (baseType.startsWith('lib:')) {
				const entry = LIBS[baseType.slice(4)][last.name];
				return entry ? '```lua\n' + baseType.slice(4) + '.' + (entry.kind === 'function' ? signature(last.name, entry) : last.name + ': ' + entry.type) + '\n```\n' + (entry.doc || '') : null;
			}
			const clsName = baseType.startsWith('class:') ? baseType.slice(6) : baseType;
			const found = this.member(clsName, last.name);
			if (!found) {
				const user = mine();
				if (user) {
					return user;
				}
				return this.cls(clsName) ? '`' + clsName + '` has no `' + last.name + '` in the game\'s API.' + (last.sep === '.' ? ' (A script can still set its own values on an object.)' : '') : null;
			}
			const m = found.member;
			const owner = found.owner !== clsName ? '\n\n*From ' + found.owner + '*' : '';
			if (m.kind === 'method') {
				return '```lua\n' + m.overloads.map((o) => found.owner + ':' + signature(last.name, o)).join('\n') + '\n```\n' + (m.doc || '') + paramDocs(m.overloads[0]) + owner;
			}
			if (m.kind === 'const') {
				return '```lua\n' + found.owner + '.' + last.name + ': number\n```\n' + (m.doc || '') + owner;
			}
			return '```lua\n' + found.owner + '.' + last.name + ': ' + prettyType(m.type) + (m.readonly ? '  -- read only' : '') + '\n```\n' + (m.doc || '') + owner;
		}

		// ---- what a name refers to ----

		// How a script's text is read: what the script is, and its scopes. A snippet (the body of one of a script's functions, edited on its
		// own, as a flow node's code is) is read as part of the script its path names: self is that script's activity (or object), and
		// what the script defines is known. opts.snippet says whether text is a snippet; by default it is when the path is a loaded script,
		// the text isn't that script's, and it declares no functions of its own at the start of a line.
		context(path, text, offset, opts) {
			const stored = this.scripts.get((path || '').toLowerCase());
			const snippet = opts && opts.snippet !== undefined ? !!opts.snippet : !!(stored && stored.text !== text && !/^function\b/m.test(text));
			const info = this.scriptInfo(path, snippet && stored ? stored.text : text);
			const selfKey = info.kind === 'activity' && info.className ? 'G:' + info.className : 'S';
			const options = { chunkSelf: snippet ? selfKey : null, paramSelf: selfKey };
			const analysis = this.typingAnalysis(text, offset, options);
			if (analysis) {
				analysis.path = path;
			}
			const host = snippet && stored ? this.analysisOf(stored.path) : null;
			const ctx = { text, offset, path, selfType: info.selfType, kind: info.kind, className: info.className, snippet, analysis, host, selfKey };
			return { info, ctx };
		}

		// analyzeTyping, kept for the last few texts (completion, hover and checks ask about the same text over and over).
		typingAnalysis(text, offset, options) {
			const cache = this._typing || (this._typing = []);
			const optKey = (options.chunkSelf || '') + '|' + (options.paramSelf || '');
			const line = text.lastIndexOf('\n', (offset || 0) - 1);
			const hit = cache.find((c) => c.text === text && c.optKey === optKey && ((c.analysis && c.analysis.plain) || c.line === line));
			if (hit) {
				return hit.analysis;
			}
			const analysis = analyzeTyping(text, offset, options);
			if (analysis) {
				analysis.text = text;
				analysis.path = analysis.path || null;
			}
			cache.unshift({ text, optKey, analysis, line });
			cache.length = Math.min(cache.length, 4);
			return analysis;
		}

		// A loaded script's analysis (by its path), kept until the scripts change.
		analysisOf(path) {
			const s = path && this.scripts.get(path.toLowerCase());
			if (!s) {
				return null;
			}
			const cache = this._analyses || (this._analyses = new Map());
			const key = s.path.toLowerCase();
			const hit = cache.get(key);
			if (hit && hit.text === s.text) {
				return hit.analysis;
			}
			const info = this.scriptInfo(s.path, s.text);
			const selfKey = info.kind === 'activity' && info.className ? 'G:' + info.className : 'S';
			const analysis = analyze(s.text, { paramSelf: selfKey });
			if (analysis) {
				analysis.text = s.text;
				analysis.path = s.path;
			}
			cache.set(key, { text: s.text, analysis });
			return analysis;
		}

		// The script a require("Name") or dofile("Mod.rte/File.lua") in a script loads, by its path, or null. require looks in the
		// script's own module first, then Base.rte, as the game does (package.path gets each module's folder as its scripts load).
		resolveRequire(fromPath, name, how) {
			const clean = name.replace(/\\/g, '/').replace(/^(Data|Mods)\//i, '');
			const has = (p) => this.scripts.get(p.toLowerCase());
			if (how !== 'require' || /\.lua$/i.test(clean)) {
				return has(clean) ? has(clean).path : null;
			}
			const rel = clean.replace(/\./g, '/') + '.lua';
			const module = (fromPath || '').split('/')[0];
			for (const candidate of [module + '/' + rel, 'Base.rte/' + rel, 'Base.rte/LuaIntegration/' + rel]) {
				if (has(candidate)) {
					return has(candidate).path;
				}
			}
			const tail = '/' + rel.toLowerCase();
			for (const [lower, s] of this.scripts) {
				if (lower.endsWith(tail) && lower.split('/').length === rel.split('/').length + 1) {
					return s.path;
				}
			}
			return null;
		}

		// Where a key is defined in one script's analysis, following "x = require(...)" into the file it loads and "x = y" to y:
		// {analysis, def} or null.
		findKey(analysis, key, depth) {
			depth = depth || 0;
			if (!analysis || !key || depth > 5) {
				return null;
			}
			const list = analysis.defs.get(key);
			if (list && list.length) {
				// A function's definition, else the first value set in the callback the game runs first (StartActivity, Create), else the first.
				return { analysis, def: list.find((d) => d.kind === 'function') || list.find((d) => d.inFirst) || list[0] };
			}
			const parts = key.split('.');
			for (let i = parts.length - 1; i >= 1; i--) {
				const prefix = parts.slice(0, i).join('.');
				const rest = parts.slice(i).join('.');
				for (const d of analysis.defs.get(prefix) || []) {
					let found = null;
					if (d.require) {
						const target = this.resolveRequire(analysis.path, d.require, d.how);
						const loaded = target && this.analysisOf(target);
						found = loaded && loaded.returnKey ? this.findKey(loaded, loaded.returnKey + '.' + rest, depth + 1) : null;
					} else if (d.alias && d.alias !== prefix && !d.alias.startsWith(prefix + '.')) {
						found = this.findKey(analysis, d.alias + '.' + rest, depth + 1);
					}
					if (found) {
						return found;
					}
				}
			}
			return null;
		}

		// The scripts a script loads with require and dofile (and what those load), nearest first.
		requiredAnalyses(analysis, limit) {
			const out = [];
			const seen = new Set([analysis && analysis.path && analysis.path.toLowerCase()]);
			const queue = analysis ? [analysis] : [];
			while (queue.length && out.length < (limit || 12)) {
				const a = queue.shift();
				for (const r of a.requires) {
					const target = this.resolveRequire(a.path || analysis.path, r.name, r.how);
					if (target && !seen.has(target.toLowerCase())) {
						seen.add(target.toLowerCase());
						const loaded = this.analysisOf(target);
						if (loaded) {
							out.push(loaded);
							queue.push(loaded);
						}
					}
				}
			}
			return out;
		}

		// Where a key is defined, anywhere: this text, the script a snippet belongs to, the scripts this one loads, then any loaded script
		// (globals only). {analysis, def} for a place found by reading a script, {path, line, symbol} from the index of every script, or null.
		locate(ctx, key) {
			if (!key) {
				return null;
			}
			const own = this.findKey(ctx.analysis, key) || this.findKey(ctx.host, key);
			if (own) {
				return own;
			}
			if (!key.startsWith('G:')) {
				return null;
			}
			for (const a of this.requiredAnalyses(ctx.host || ctx.analysis)) {
				const found = this.findKey(a, key);
				if (found) {
					return found;
				}
			}
			const list = this.symbols.get(key.slice(2));
			if (!list) {
				return null;
			}
			const module = (ctx.path || '').split('/')[0].toLowerCase();
			const symbol = list.find((s) => s.path.toLowerCase() === (ctx.path || '').toLowerCase()) || list.find((s) => s.path.split('/')[0].toLowerCase() === module) || list[0];
			return { path: symbol.path, line: symbol.line, symbol, name: key.slice(2) };
		}

		// A found place as a definition target: {offset} in the text being edited, else {path, line, column}.
		placeOf(ctx, found) {
			if (!found) {
				return null;
			}
			if (found.symbol) {
				const text = (this.scripts.get(found.path.toLowerCase()) || {}).text || '';
				const line = text.slice(lineOffset(text, found.line)).split('\n')[0];
				const name = /[\w]+$/.exec(found.name || '');
				const column = name ? new RegExp('(^|[^\\w])' + name[0] + '\\b').exec(line) : null;
				return column ? { path: found.path, line: found.line, column: column.index + column[1].length + 1 } : { path: found.path, line: found.line };
			}
			if (found.analysis === ctx.analysis) {
				return { offset: found.def.at };
			}
			const before = found.analysis.text.slice(0, found.def.at);
			const line = before.split('\n').length;
			return { path: found.analysis.path, line, column: found.def.at - before.lastIndexOf('\n') };
		}

		// What a found place says about itself: {name, kind, params, method, value, doc, where}.
		describeFound(ctx, found, name) {
			if (found.symbol) {
				const s = found.symbol;
				const text = (this.scripts.get(s.path.toLowerCase()) || {}).text || '';
				return { name, kind: s.kind, params: s.params, method: s.method, value: s.value, doc: commentAbove(text, lineOffset(text, s.line)), where: s.path + ':' + s.line };
			}
			const def = found.def;
			const text = found.analysis.text;
			const line = text.slice(0, def.at).split('\n').length;
			const where = found.analysis === ctx.analysis ? 'line ' + line : found.analysis.path + ':' + line;
			return { name, kind: def.kind, params: def.params, method: def.method, value: def.value, doc: commentAbove(text, text.lastIndexOf('\n', def.at) + 1), where };
		}

		// The key an expression of plain names has at an offset ("self.Timer" -> "G:DecisionDay.Timer"), or null.
		keyOfExpr(ctx, expr, offset) {
			const segs = LuaIntel.parseChain(expr);
			if (!segs || !segs.length || segs.some((s) => s.call || s.sep === '[' || !s.name)) {
				return null;
			}
			let root;
			if (ctx.analysis) {
				root = keyAt(ctx.analysis, segs[0].name, offset);
			} else {
				root = segs[0].name === 'self' ? (this.selfAt(ctx) !== 'any' ? ctx.selfKey : null) : 'G:' + segs[0].name;
			}
			return root && [root, ...segs.slice(1).map((s) => s.name)].join('.');
		}

		// What's defined on a key, one level down ("G:DecisionDay" -> its methods and the fields its methods set on self): Map(name -> {kind,
		// params, method, where}).
		membersOfKey(ctx, key) {
			const out = new Map();
			if (!key) {
				return out;
			}
			const fromAnalysis = (a, k, depth) => {
				if (!a || depth > 4) {
					return;
				}
				const prefix = k + '.';
				for (const [defKey, list] of a.defs) {
					if (defKey.startsWith(prefix) && !defKey.includes('.', prefix.length)) {
						const name = defKey.slice(prefix.length);
						const def = list.find((d) => d.kind === 'function') || list[0];
						if (!out.has(name) || (def.kind === 'function' && out.get(name).kind !== 'function')) {
							out.set(name, { kind: def.kind, params: def.params, method: def.method, value: def.value, where: a === ctx.analysis ? 'this script' : a.path });
						}
					}
				}
				// What the key itself is: the table a required file returns, or another name.
				for (const d of a.defs.get(k) || []) {
					if (d.require) {
						const target = this.resolveRequire(a.path || ctx.path, d.require, d.how);
						const loaded = target && this.analysisOf(target);
						if (loaded && loaded.returnKey) {
							fromAnalysis(loaded, loaded.returnKey, depth + 1);
						}
					} else if (d.alias && d.alias !== k) {
						fromAnalysis(a, d.alias, depth + 1);
					}
				}
			};
			fromAnalysis(ctx.analysis, key, 0);
			fromAnalysis(ctx.host, key, 0);
			if (key.startsWith('G:')) {
				for (const a of this.requiredAnalyses(ctx.host || ctx.analysis)) {
					fromAnalysis(a, key, 0);
				}
				const prefix = key.slice(2) + '.';
				for (const [name, list] of this.symbols) {
					if (name.startsWith(prefix) && !name.includes('.', prefix.length) && !out.has(name.slice(prefix.length))) {
						const s = list[0];
						out.set(name.slice(prefix.length), { kind: s.kind, params: s.params, method: s.method, value: s.value, where: s.path });
					}
				}
			}
			return out;
		}

		// A string the cursor is in, and what it names: {value, start, end, target} where target is {path, line} (a script or INI file),
		// {asset: {kind: "preset", preset}}, {asset: {kind: "image" | "sound", path}}, or null.
		stringAt(text, start, fromPath) {
			const lineStart = text.lastIndexOf('\n', start - 1) + 1;
			const str = stringContext(text.slice(lineStart, start + 1));
			if (!str) {
				return null;
			}
			const open = start + 1 - str.typed.length - 1; // the opening quote
			const close = text.indexOf(str.quote, open + 1);
			const value = text.slice(open + 1, close < 0 ? start : close).replace(/\\\\/g, '\\');
			const out = { value, start: open + 1, end: close < 0 ? start : close, target: null };
			const path = value.trim().replace(/\\/g, '/').replace(/^(Data|Mods)\//i, '');
			const call = callBefore(text.slice(0, open));
			if (call && /^(require|dofile|loadfile)$/.test(call.name) && call.arg === 0) {
				const script = this.resolveRequire(fromPath, path, call.name);
				if (script) {
					out.target = { path: script, line: 1 };
					return out;
				}
			}
			if (/\.(lua|ini|txt|cfg)$/i.test(path)) {
				out.target = { path, line: 1 };
				return out;
			}
			if (/\.(png|bmp|jpe?g|gif)$/i.test(path)) {
				out.target = { asset: { kind: 'image', path } };
				return out;
			}
			if (/\.(wav|ogg|flac|mp3)$/i.test(path)) {
				out.target = { asset: { kind: 'sound', path } };
				return out;
			}
			const lib = this.app.lib;
			if (!lib || !value.trim()) {
				return out;
			}
			if (/\.rte$/i.test(path) && lib.modules && lib.modules.some((m) => m.name.toLowerCase() === path.toLowerCase())) {
				out.target = { path: path + '/Index.ini', line: 1 };
				return out;
			}
			// A preset: of the class the call makes (CreateAHuman("Name", "Mod.rte"), PresetMan:GetPreset("AHuman", "Name", "Mod.rte")).
			let cls = null;
			if (call) {
				const m = /^(?:Create|Random)(\w+)$/.exec(call.name);
				cls = m && this.cls(m[1]) ? m[1] : /GetPreset|GetLoadout/.test(call.name) && call.arg === 1 ? call.firstString : null;
			}
			const after = /^\s*,\s*["']([^"'\n]+\.rte)["']/i.exec(text.slice(out.end + 1));
			const preset = lib.find(after ? after[1] + '/' + value : value, cls, null) || lib.find(value, null, null);
			if (preset) {
				out.target = { asset: { kind: 'preset', preset } };
			}
			return out;
		}

		// Markdown for what a string names (from stringAt), with links to open it.
		hoverString(str) {
			const t = str.target;
			if (!t) {
				return null;
			}
			const link = (label, kind, path, query) => '[' + label + '](ccstudio://' + kind + '/' + encodeURIComponent(path) + (query ? '?' + query : '') + ')';
			if (t.asset && t.asset.kind === 'preset') {
				const p = t.asset.preset;
				const how = LuaIntel.openWith(p);
				const links = [link(how === 'designer' ? 'Open in the designer' : how === 'sounds' ? 'Open in Sounds' : 'Open its lines', 'preset', p.doc.path, 'name=' + encodeURIComponent(p.name) + '&line=' + (p.node.line || 1))];
				if (how !== 'lines') {
					links.push(link('Open its lines', 'open', p.doc.path, 'line=' + (p.node.line || 1)));
				}
				return '**' + p.name + '** · ' + p.cls + ' in ' + p.module + '\n\n' + links.join(' · ') + '\n\n*Ctrl+click to ' + (how === 'designer' ? 'open it in the designer' : how === 'sounds' ? 'open it in Sounds' : 'go to its lines') + '.*';
			}
			if (t.asset && t.asset.kind === 'sound') {
				return 'Sound file `' + t.asset.path + '`\n\n' + link('Play it', 'play', t.asset.path) + '\n\n*Ctrl+click to play it.*';
			}
			if (t.asset && t.asset.kind === 'image') {
				return 'Picture `' + t.asset.path + '`\n\n' + link('Open it', 'open', t.asset.path) + ' · ' + link('Edit it', 'paint', t.asset.path);
			}
			return '`' + t.path + '`\n\n' + link('Open it', 'open', t.path);
		}

		// ---- go to definition ----

		// Where the name at [start, end) is defined: {offset} (in this text), {path, line, column} (a script or INI file), {api: true, path,
		// line} (the game's API, data/lua-api.d.lua), {asset} (a preset, picture or sound a string names: see stringAt), or null.
		definition(path, text, start, end, opts) {
			const str = this.stringAt(text, start, path);
			if (str) {
				return str.target;
			}
			const { ctx } = this.context(path, text, start, opts);
			const id = ctx.analysis && idAt(ctx.analysis, start, end);
			if (id) {
				const found = this.definitionOf(ctx, id);
				if (found !== undefined) {
					return found;
				}
			}
			return this.definitionByText(path, text, start, end);
		}

		// The definition of a name the analysis found: a target, null (known to be nowhere), or undefined (look for it the old way).
		definitionOf(ctx, id) {
			if (id.decl) {
				return id.decl.implicit ? null : { offset: id.decl.at };
			}
			if (id.isDecl) {
				return { offset: id.start };
			}
			if (id.field) {
				const api = this.apiMemberAt(ctx, id);
				if (api) {
					return this.apiDefinition(api.owner + '.' + id.name) || this.placeOf(ctx, this.locate(ctx, id.key));
				}
				return this.placeOf(ctx, this.locate(ctx, id.key)) || undefined;
			}
			const found = this.locate(ctx, id.key);
			if (found) {
				return this.placeOf(ctx, found);
			}
			if (this.api.functions[id.name] || this.api.globals[id.name] || this.cls(id.name)) {
				return this.apiDefinition(id.name) || null;
			}
			return undefined;
		}

		// The game's member a field is ({member, owner}), when what it's on is a class of the game's.
		apiMemberAt(ctx, id) {
			if (!id.field || id.baseStart === undefined) {
				return null;
			}
			const baseType = this.typeOfExpr(ctx.text.slice(id.baseStart, id.baseEnd), Object.assign({}, ctx, { offset: id.baseStart }));
			const clsName = baseType.startsWith('class:') ? baseType.slice(6) : baseType;
			return this.cls(clsName) ? this.member(clsName, id.name) : null;
		}

		// Go to definition without an analysis (a script that doesn't parse): the nearest declaration before it, or a global another script
		// defines.
		definitionByText(path, text, start, end) {
			const word = text.slice(start, end);
			const expr = LuaIntel.exprBefore(text, end);
			const segs = LuaIntel.parseChain(expr) || [];
			if (segs.length === 1) {
				// A local: where it's declared in this file (the nearest declaration before it).
				const before = text.slice(0, start);
				const esc = word.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
				const re = new RegExp('\\blocal\\s+function\\s+' + esc + '\\b|\\blocal\\s+[\\w\\s,]*?\\b' + esc + '\\b|\\bfunction\\b[^(\\n]*\\([^)]*?\\b' + esc + '\\b|\\bfor\\s+[\\w\\s,]*?\\b' + esc + '\\b', 'g');
				let found = -1;
				for (const m of before.matchAll(re)) {
					found = m.index + m[0].length - word.length;
				}
				if (found >= 0) {
					return { offset: found };
				}
			}
			const name = segs.map((s) => s.name).join('.');
			const sym = this.symbols.get(name) || this.symbols.get(word);
			if (sym) {
				const own = sym.find((s) => s.path.toLowerCase() === (path || '').toLowerCase());
				return own || sym[0];
			}
			return null;
		}

		// ---- checks ----

		// Problems in a script: [{line, col, endLine, endCol, severity: "error" | "warning" | "info", message}]
		check(path, text) {
			const problems = [];
			if (!window.luaparse) {
				return problems;
			}
			let ast;
			try {
				ast = window.luaparse.parse(text, { luaVersion: '5.2', locations: true, ranges: true, scope: true, comments: false });
			} catch (e) {
				if (e.line) {
					problems.push({ line: e.line, col: e.column + 1, endLine: e.line, endCol: e.column + 30, severity: 'error', message: e.message.replace(/^\[\d+:\d+\]\s*/, '') });
				}
				return problems;
			}
			const info = this.scriptInfo(path, text);
			const callbacks = info.kind === 'activity' ? ACTIVITY_CALLBACKS : ENTITY_CALLBACKS;
			const setFields = new Set();
			for (const m of text.matchAll(/\.(\w+)\s*=(?!=)|^\s*function\s+[\w.]+[.:](\w+)\s*\(/gm)) {
				setFields.add(m[1] || m[2]);
			}
			// Without a preset using the script, what self is was only guessed: don't nag about it.
			const selfKnown = info.kind === 'activity' || info.presets.length > 0;
			const unsure = (baseText) => !selfKnown && /^self\b/.test(baseText);
			const loc = (node) => ({ line: node.loc.start.line, col: node.loc.start.column + 1, endLine: node.loc.end.line, endCol: node.loc.end.column + 1 });
			const report = (node, severity, message) => problems.push(Object.assign(loc(node), { severity, message }));
			const lib = this.app.lib;

			walk(ast, (node, parent) => {
				// Callbacks the game will never call because of a typo.
				if (node.type === 'FunctionDeclaration' && node.identifier && !node.isLocal) {
					const id = node.identifier;
					const name = id.type === 'Identifier' ? id.name : id.type === 'MemberExpression' && id.indexer === ':' ? id.identifier.name : null;
					const isCallbackSpot = id.type === 'Identifier' ? info.kind === 'entity' : info.kind === 'activity';
					if (name && isCallbackSpot && !callbacks[name]) {
						const close = Object.keys(callbacks).find((c) => c.toLowerCase() === name.toLowerCase() || (Math.abs(c.length - name.length) <= 2 && distance(c, name) <= 2 && name.length > 4));
						if (close) {
							report(id.type === 'Identifier' ? id : id.identifier, 'warning', 'The game calls ' + close + ', not ' + name + ', so this never runs.');
						}
					}
				}
				// obj:Method() where obj's class has no such method.
				if (node.type === 'CallExpression' && node.base.type === 'MemberExpression') {
					const member = node.base;
					const baseText = text.slice(member.base.range[0], member.base.range[1]);
					const type = this.typeOfExpr(baseText, { text, offset: member.base.range[0], selfType: info.selfType, kind: info.kind, className: info.className });
					const clsName = type.startsWith('class:') || unsure(baseText) ? null : type;
					const ownMethod = info.className && /^self\b/.test(baseText) && this.symbols.has(info.className + '.' + member.identifier.name);
					if (clsName && this.cls(clsName) && !this.member(clsName, member.identifier.name) && !setFields.has(member.identifier.name) && !ownMethod) {
						const near = closest(member.identifier.name, [...this.members(clsName).keys()]);
						report(member.identifier, 'warning', clsName + ' has no function ' + member.identifier.name + '.' + (near ? ' Did you mean ' + near + '?' : ''));
					}
				}
				// obj.Name read where obj's class has no such property, and nothing in this script sets it.
				if (node.type === 'MemberExpression' && node.indexer === '.' && !(parent && parent.type === 'CallExpression' && parent.base === node) && !(parent && parent.type === 'AssignmentStatement' && parent.variables.includes(node))) {
					const baseText = text.slice(node.base.range[0], node.base.range[1]);
					const type = this.typeOfExpr(baseText, { text, offset: node.base.range[0], selfType: info.selfType, kind: info.kind, className: info.className });
					if (this.cls(type) && !unsure(baseText) && !this.member(type, node.identifier.name) && !setFields.has(node.identifier.name)) {
						const near = closest(node.identifier.name, [...this.members(type).keys()]);
						report(node.identifier, 'info', type + ' has no ' + node.identifier.name + ' in the game\'s API, and this script doesn\'t set it.' + (near ? ' Did you mean ' + near + '?' : ''));
					}
				}
				// CreateAHuman("Name") with a preset Studio doesn't know.
				if ((node.type === 'CallExpression' || node.type === 'StringCallExpression') && node.base.type === 'Identifier' && lib) {
					const m = /^Create(\w+)$/.exec(node.base.name);
					const arg = node.type === 'StringCallExpression' ? node.argument : node.arguments[0];
					const second = node.type === 'CallExpression' ? node.arguments[1] : null;
					if (m && this.cls(m[1]) && arg && arg.type === 'StringLiteral') {
						const name = text.slice(arg.range[0] + 1, arg.range[1] - 1);
						const moduleName = second && second.type === 'StringLiteral' ? text.slice(second.range[0] + 1, second.range[1] - 1) : null;
						const found = lib.find(moduleName ? moduleName + '/' + name : name, null, null);
						if (!found && !/[.]{2}|%/.test(name)) {
							report(arg, 'warning', 'No preset called "' + name + '" in the mods Studio has read. Check the spelling, or the mod it\'s in.');
						}
					}
				}
				// Calls to global names nothing defines.
				if (node.type === 'CallExpression' && node.base.type === 'Identifier' && !node.base.isLocal) {
					const name = node.base.name;
					if (!this.api.functions[name] && !this.cls(name) && !BUILTINS[name] && !LIBS[name] && !this.api.globals[name] && !this.symbols.has(name) && !definedInText(text, name)) {
						report(node.base, 'info', name + ' isn\'t defined in this script, the game, or another loaded script.');
					}
				}
			});
			return problems;
		}
	}

	// ---- helpers ----

	function splitArgs(text) {
		const out = [];
		let depth = 0;
		let start = 0;
		let quote = null;
		for (let i = 0; i < text.length; i++) {
			const c = text[i];
			if (quote) {
				if (c === '\\') {
					i++;
				} else if (c === quote) {
					quote = null;
				}
			} else if (c === '"' || c === '\'') {
				quote = c;
			} else if ('([{'.includes(c)) {
				depth++;
			} else if (')]}'.includes(c)) {
				depth--;
			} else if (c === ',' && depth === 0) {
				out.push(text.slice(start, i));
				start = i + 1;
			}
		}
		out.push(text.slice(start));
		return out;
	}

	// If the line ends inside a string: {quote, typed} (what's typed in it so far).
	function stringContext(line) {
		let quote = null;
		let start = 0;
		for (let i = 0; i < line.length; i++) {
			const c = line[i];
			if (quote) {
				if (c === '\\') {
					i++;
				} else if (c === quote) {
					quote = null;
				}
			} else if (c === '-' && line[i + 1] === '-') {
				return null;
			} else if (c === '"' || c === '\'') {
				quote = c;
				start = i + 1;
			}
		}
		return quote ? { quote, typed: line.slice(start) } : null;
	}

	// The call a string argument is in: {name, arg (index), firstString}. "before" ends just before the string's opening quote.
	function callBefore(before) {
		let depth = 0;
		let commas = 0;
		for (let i = before.length; i > 0 && before.length - i < 2000; i--) {
			const c = before[i - 1];
			if (c === ')') {
				depth++;
			} else if (c === '(') {
				if (depth === 0) {
					const name = (/([A-Za-z_]\w*)\s*$/.exec(before.slice(0, i - 1)) || [])[1];
					const first = /^\s*["']([^"']*)["']/.exec(before.slice(i));
					return name ? { name, arg: commas, firstString: first ? first[1] : null } : null;
				}
				depth--;
			} else if (c === ',' && depth === 0) {
				commas++;
			} else if (c === '\n' && depth === 0 && commas === 0 && !/\S/.test(before.slice(i))) {
				// still looking
			}
		}
		return null;
	}

	// Names declared before "offset": locals, function parameters, loop variables.
	function localNames(text, offset) {
		const before = text.slice(0, offset);
		const names = new Set();
		for (const m of before.matchAll(/\blocal\s+function\s+([A-Za-z_]\w*)|\blocal\s+([A-Za-z_][\w\s,]*?)(?:=|\n|$)|\bfor\s+([\w\s,]+?)\s+in\b|\bfor\s+([A-Za-z_]\w*)\s*=|\bfunction\b[^(\n]*\(([^)]*)\)/g)) {
			const list = m[1] || m[2] || m[3] || m[4] || m[5] || '';
			for (const n of list.split(',')) {
				const name = n.trim();
				if (/^[A-Za-z_]\w*$/.test(name) && name !== 'self' && !KEYWORDS.includes(name)) {
					names.add(name);
				}
			}
		}
		return [...names];
	}

	// The comment lines just above a line (its offset), as Markdown: what a function's author wrote about it.
	function commentAbove(text, lineStart) {
		const lines = text.slice(Math.max(0, lineStart - 3000), lineStart).split('\n');
		lines.pop();
		const out = [];
		for (let i = lines.length - 1; i >= 0 && out.length < 20; i--) {
			const line = lines[i].trim();
			if (!line.startsWith('--') || /^--\[\[|^--\]\]/.test(line)) {
				break;
			}
			const param = /^---@param\s+(\S+)\s+(\S+)\s*(.*)$/.exec(line);
			const ret = /^---@return\s+(\S+)\s*(.*)$/.exec(line);
			if (param) {
				out.unshift('- `' + param[1] + '` (' + param[2] + ')' + (param[3] ? ': ' + param[3] : ''));
			} else if (ret) {
				out.unshift('Returns ' + ret[1] + (ret[2] ? ': ' + ret[2] : ''));
			} else if (!/^---@/.test(line)) {
				out.unshift(line.replace(/^-+\s?/, ''));
			}
		}
		return out.join('\n').trim();
	}

	// The offset a line (1-based) starts at.
	function lineOffset(text, line) {
		let at = 0;
		for (let n = 1; n < line && at >= 0; n++) {
			at = text.indexOf('\n', at) + 1;
			if (at === 0) {
				return text.length;
			}
		}
		return at;
	}

	function definedInText(text, name) {
		const esc = name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
		return new RegExp('(?:^|[^.\\w])(?:function\\s+' + esc + '\\b|' + esc + '\\s*=(?!=))', 'm').test(text);
	}

	function signature(name, o, count) {
		const params = o.params ? o.params.map((p) => p.name + (p.optional ? '?' : '') + ': ' + (p.type || 'any')).join(', ') : '...';
		return name + '(' + params + ')' + (o.returns && o.returns !== 'nil' ? ' → ' + prettyType(o.returns) : '') + (count > 1 ? '  (+' + (count - 1) + ' more)' : '');
	}

	function paramDocs(o) {
		const lines = (o.params || []).filter((p) => p.doc).map((p) => '- `' + p.name + '`: ' + p.doc);
		return (lines.length ? '\n\n' + lines.join('\n') : '') + (o.returnsDoc && o.returnsDoc !== 'None.' ? '\n\nReturns ' + o.returnsDoc : '');
	}

	function prettyType(type) {
		if (!type) {
			return 'any';
		}
		if (type.startsWith('class:')) {
			return 'class ' + type.slice(6);
		}
		if (type.startsWith('lib:') || type.startsWith('global:')) {
			return 'table';
		}
		if (type.startsWith('fn:')) {
			return 'function';
		}
		return type;
	}

	function functionItem(name, entry, method, sort) {
		return { label: name, kind: method ? 'method' : 'function', detail: signature(name, entry), doc: entry.doc || '', insert: name + '(' + (entry.params.length ? '$0' : '') + ')', snippet: true, sort: sort || '1' };
	}

	function walk(node, visit, parent) {
		if (!node || typeof node !== 'object') {
			return;
		}
		if (node.type) {
			visit(node, parent);
		}
		for (const key of Object.keys(node)) {
			if (key === 'loc' || key === 'range' || key === 'scope') {
				continue;
			}
			const value = node[key];
			if (Array.isArray(value)) {
				for (const child of value) {
					walk(child, visit, node.type ? node : parent);
				}
			} else if (value && typeof value === 'object' && value.type) {
				walk(value, visit, node.type ? node : parent);
			}
		}
	}

	function distance(a, b) {
		a = a.toLowerCase();
		b = b.toLowerCase();
		const row = Array.from({ length: b.length + 1 }, (_, i) => i);
		for (let i = 1; i <= a.length; i++) {
			let prev = row[0];
			row[0] = i;
			for (let j = 1; j <= b.length; j++) {
				const old = row[j];
				row[j] = Math.min(row[j] + 1, row[j - 1] + 1, prev + (a[i - 1] === b[j - 1] ? 0 : 1));
				prev = old;
			}
		}
		return row[b.length];
	}

	function closest(name, names) {
		let best = null;
		let bestDistance = Math.max(2, Math.floor(name.length / 3)) + 1;
		for (const n of names) {
			const d = distance(name, n);
			if (d < bestDistance) {
				best = n;
				bestDistance = d;
			}
		}
		return best;
	}

	LuaIntel.ENTITY_CALLBACKS = ENTITY_CALLBACKS;
	LuaIntel.ACTIVITY_CALLBACKS = ACTIVITY_CALLBACKS;
	LuaIntel.splitArgs = splitArgs;
	LuaIntel.analyze = analyze;
	// Where a preset opens: "designer" (units, craft, items and the rest with a picture), "sounds" (sound presets) or "lines" (its INI).
	LuaIntel.openWith = (preset) => {
		const tabs = window.MS && MS.panels && MS.panels.TAB_CLASSES;
		if (tabs && Object.values(tabs).some((set) => set.has(preset.cls))) {
			return 'designer';
		}
		return preset.cls === 'SoundContainer' ? 'sounds' : 'lines';
	};
	LuaIntel.API_PATH = 'Game API.lua';
	MS.LuaIntel = LuaIntel;
})();
