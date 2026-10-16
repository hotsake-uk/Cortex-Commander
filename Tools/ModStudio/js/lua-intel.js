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

	class LuaIntel {
		constructor(app) {
			this.app = app;
			this.api = null;
			this.scripts = new Map(); // lowercased module path -> {path, text}: every loaded .lua
			this.symbols = new Map(); // global name ("rte.NoMOID", "MyMod_DoThing") -> [{path, line, params, kind}]
			this.symbolNames = []; // top-level names (before any "."), for completion
		}

		async load() {
			const response = await fetch('data/lua-api.json', { cache: 'no-cache' });
			this.api = await response.json();
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
			const tops = new Set();
			for (const { path, text } of this.scripts.values()) {
				const lines = text.split(/\r?\n/);
				lines.forEach((line, i) => {
					let m = /^function\s+([A-Za-z_][\w.:]*)\s*\(([^)]*)\)/.exec(line);
					if (m) {
						this.addSymbol(m[1].replace(':', '.'), { path, line: i + 1, params: m[2].split(',').map((p) => p.trim()).filter(Boolean), kind: 'function', method: m[1].includes(':') }, tops);
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
			if (!head) {
				return 'any';
			}
			if (ctx.kind === 'activity') {
				return head.sep === ':' && (!ctx.className || head.table === ctx.className) ? ctx.selfType : head.selfFirst ? ctx.selfType : 'any';
			}
			return head.selfFirst ? ctx.selfType : 'any';
		}

		// ---- completion ----

		// Completion items at an offset of a script: [{label, kind, detail, doc, insert, snippet, sort}]
		complete(path, text, offset) {
			const info = this.scriptInfo(path, text);
			const ctx = { text, offset, selfType: info.selfType, kind: info.kind, className: info.className };
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

		completeMembers(type, sep, ctx, expr) {
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
			for (const name of localNames(ctx.text, ctx.offset)) {
				add({ label: name, kind: 'variable', detail: 'local', sort: '0' });
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
		signatureAt(path, text, offset) {
			const info = this.scriptInfo(path, text);
			const ctx = { text, offset, selfType: info.selfType, kind: info.kind, className: info.className };
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

		// The overloads a callee expression can mean: {name, doc, list}.
		overloadsOf(callee, ctx) {
			const segs = LuaIntel.parseChain(callee);
			if (!segs || !segs.length) {
				return null;
			}
			const last = segs[segs.length - 1];
			if (segs.length === 1) {
				const name = last.name;
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
			return null;
		}

		// ---- hover ----

		// Markdown describing the name ending at "end" (the word under the mouse), or null.
		hover(path, text, start, end) {
			const info = this.scriptInfo(path, text);
			const ctx = { text, offset: start, selfType: info.selfType, kind: info.kind, className: info.className };
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
				const local = this.localType(word, ctx, 0);
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
				const sym = this.symbols.get(word);
				if (sym) {
					return '```lua\n' + word + (sym[0].params ? '(' + sym[0].params.join(', ') + ')' : '') + '\n```\nDefined in ' + sym.map((s) => s.path + ':' + s.line).slice(0, 3).join(', ');
				}
				return null;
			}
			const last = segs[segs.length - 1];
			const baseExpr = expr.slice(0, expr.length - last.name.length).replace(/\s*[.:]\s*$/, '');
			const baseType = this.typeOfExpr(baseExpr, ctx);
			if (baseType.startsWith('global:')) {
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

		// ---- go to definition ----

		// Where the name at [start, end) is defined: {path, line} (a script or INI file), {offset} (in this file), or null.
		definition(path, text, start, end) {
			const word = text.slice(start, end);
			const lineStart = text.lastIndexOf('\n', start - 1) + 1;
			const str = stringContext(text.slice(lineStart, start + 1));
			if (str) {
				// A preset name or a file path in a string.
				const full = /^[^"'\n]*/.exec(text.slice(start))[0];
				const value = (str.typed + full).replace(/^["']/, '');
				if (/\.(lua|ini|png|bmp|wav|ogg|flac)$/i.test(value)) {
					return { path: value.replace(/\\/g, '/'), line: 1 };
				}
				const preset = this.app.lib && this.app.lib.find(value, null, null);
				return preset ? { path: preset.doc.path, line: preset.node.line || 1 } : null;
			}
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
					if (clsName && this.cls(clsName) && !this.member(clsName, member.identifier.name) && !setFields.has(member.identifier.name)) {
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
	MS.LuaIntel = LuaIntel;
})();
