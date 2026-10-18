// Writes Mod Studio's Lua API (data/lua-api.json, used for autocomplete, hover help and checks) from the game's own Lua bindings,
// so what Studio offers is what the engine exposes. Run it after changing Source/Lua/LuaBindings*.cpp:
//
//   node Tools/ModStudio/tools/make-lua-api.js [--root <game checkout>] [--out <folder>]
//
// --root reads another checkout's Source (Community Edition's, for Studio's Classic mode: tools/make-targets.js does that) and --out
// writes there instead of data/.
//
// It reads the luabind registrations (classes, .def, .property, .def_readwrite, .enum_, constructors, luabind::def), then looks up each
// bound C++ function in the headers for its arguments, return type and /// comment. It is a careful reader of this codebase's style,
// not a C++ parser: a signature it can't work out is left without types rather than guessed. Also writes data/lua-api.d.lua, LuaLS
// annotations for the same API, for VS Code users.
import fs from 'node:fs';
import path from 'node:path';

const option = (name) => {
	const at = process.argv.indexOf(name);
	return at > 0 ? process.argv[at + 1] : null;
};
const repo = path.resolve(option('--root') || path.join(import.meta.dirname, '..', '..', '..'));
const source = path.join(repo, 'Source');
const outDir = path.resolve(option('--out') || path.join(import.meta.dirname, '..', 'data'));

// ---- reading C++ ----

// Every header by its file name, for finding where a class is declared.
const headers = new Map();
(function walk(dir) {
	for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
		const full = path.join(dir, entry.name);
		if (entry.isDirectory()) {
			if (!/^(external|Resources)$/i.test(entry.name)) {
				walk(full);
			}
		} else if (/\.h$/.test(entry.name)) {
			headers.set(entry.name, full);
		}
	}
})(source);

const fileCache = new Map();
function read(file) {
	if (!fileCache.has(file)) {
		fileCache.set(file, fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n'));
	}
	return fileCache.get(file);
}

// The text from "open" (an index of "(" "[" "{" or "<") to its closing bracket, without the brackets.
function balanced(text, open) {
	const pairs = { '(': ')', '[': ']', '{': '}', '<': '>' };
	const stack = [pairs[text[open]]];
	let i = open + 1;
	let inString = null;
	for (; i < text.length && stack.length; i++) {
		const c = text[i];
		if (inString) {
			if (c === '\\') {
				i++;
			} else if (c === inString) {
				inString = null;
			}
			continue;
		}
		if (c === '"' || c === '\'') {
			inString = c;
		} else if (c === '/' && text[i + 1] === '/') {
			i = text.indexOf('\n', i);
			if (i < 0) {
				i = text.length;
			}
		} else if ('([{'.includes(c)) {
			stack.push(pairs[c]);
		} else if (c === stack[stack.length - 1]) {
			stack.pop();
		}
	}
	return { inner: text.slice(open + 1, i - 1), end: i };
}

// Splits at top-level commas, counting (), [], {} and <> (but not -> or << >>).
function splitTop(text) {
	const parts = [];
	let depth = 0;
	let start = 0;
	let inString = null;
	for (let i = 0; i < text.length; i++) {
		const c = text[i];
		if (inString) {
			if (c === '\\') {
				i++;
			} else if (c === inString) {
				inString = null;
			}
			continue;
		}
		if (c === '"' || c === '\'') {
			inString = c;
		} else if ('([{'.includes(c)) {
			depth++;
		} else if (')]}'.includes(c)) {
			depth--;
		} else if (c === '<' && text[i + 1] !== '<' && text[i - 1] !== '<' && /[\w:>\s]/.test(text[i - 1] || '')) {
			depth++;
		} else if (c === '>' && text[i - 1] !== '-' && text[i + 1] !== '>' && text[i - 1] !== '>') {
			depth--;
		} else if (c === ',' && depth === 0) {
			parts.push(text.slice(start, i).trim());
			start = i + 1;
		}
	}
	const last = text.slice(start).trim();
	if (last) {
		parts.push(last);
	}
	return parts;
}

// Drops // comments (outside strings), which can hold commas and brackets.
function stripLineComments(text) {
	return text.split('\n').map((line) => {
		let inString = false;
		for (let i = 0; i < line.length - 1; i++) {
			if (line[i] === '"' && line[i - 1] !== '\\') {
				inString = !inString;
			} else if (!inString && line[i] === '/' && line[i + 1] === '/') {
				return line.slice(0, i);
			}
		}
		return line;
	}).join('\n');
}

// ---- C++ types to Lua types ----

const boundClasses = new Set();
const cppToLua = new Map(); // C++ class -> the name Lua knows it by (ActivityMan -> ActivityManager)
const NUMBER = /^(?:unsigned\s+)?(?:int|long|long long|short|float|double|size_t|uint8_t|uint16_t|uint32_t|uint64_t|int8_t|int16_t|int32_t|int64_t|char|unsigned|long double|std::size_t|MOID|MID|ID)$/;
function luaType(cpp) {
	if (!cpp) {
		return 'any';
	}
	let t = cpp.replace(/\b(const|volatile|static|virtual|inline|constexpr|explicit|typename|struct|class|enum|friend)\b/g, ' ')
		.replace(/\bRTE::/g, '').replace(/[&*]/g, ' ').replace(/\s+/g, ' ').trim();
	if (t === 'void') {
		return 'nil';
	}
	if (t === 'bool') {
		return 'boolean';
	}
	if (NUMBER.test(t)) {
		return 'number';
	}
	if (/^std::(string|string_view)$|^char$|^string$/.test(t) || /^const char/.test(cpp.trim())) {
		return 'string';
	}
	if (/^luabind::(object|adl::object)$/.test(t)) {
		return 'any';
	}
	const list = /^std::(?:list|vector|deque|set|unordered_set)<(.+)>$/.exec(t);
	if (list) {
		return luaType(list[1]) + '[]';
	}
	if (/^std::(?:map|unordered_map)</.test(t)) {
		return 'table';
	}
	if (/^std::(?:unique_ptr|shared_ptr)<(.+)>$/.test(t)) {
		return luaType(/<(.+)>/.exec(t)[1]);
	}
	// Scene::Area -> Area; Actor::Status -> number (an enum)
	const last = t.split('::').pop();
	if (cppToLua.has(last)) {
		return cppToLua.get(last);
	}
	if (boundClasses.has(last)) {
		return last;
	}
	if (/^long\b|\bint$/.test(t) || isEnum(last)) {
		return 'number';
	}
	return last;
}

// "const Vector &pos = Vector()" -> {name: "pos", type: "Vector", optional: true}
function parseParam(text, index) {
	text = text.replace(/\/\*.*?\*\//g, '').trim();
	let optional = false;
	const eq = (() => {
		let depth = 0;
		for (let i = 0; i < text.length; i++) {
			const c = text[i];
			if ('(<[{'.includes(c)) {
				depth++;
			} else if (')>]}'.includes(c)) {
				depth--;
			} else if (c === '=' && depth === 0) {
				return i;
			}
		}
		return -1;
	})();
	if (eq >= 0) {
		optional = true;
		text = text.slice(0, eq).trim();
	}
	const m = /^(.*?[\s&*>])([A-Za-z_]\w*)(\s*\[\s*\d*\s*\])?$/.exec(text);
	let type = text;
	let name = 'arg' + (index + 1);
	if (m && !/^(const|unsigned|int|float|bool|double|long|short|char)$/.test(m[2]) && m[1].replace(/\b(const|unsigned)\b/g, '').trim()) {
		type = m[1];
		name = m[2];
	}
	return { name, type: luaType(type), optional };
}

// ---- finding a C++ function's declaration ----

const classBases = new Map(); // C++ class -> base class, from the headers
// The header declaring a class: its own (Actor.h), or the one that declares it inside another (Scene::Area in Scene.h).
const declaredIn = new Map();
function headerFor(cls) {
	if (headers.has(cls + '.h')) {
		return headers.get(cls + '.h');
	}
	if (!declaredIn.has(cls)) {
		const re = new RegExp('\\b(?:class|struct)\\s+' + cls + '\\s*(?:final\\s*)?[:{]');
		declaredIn.set(cls, [...headers.values()].find((file) => re.test(read(file))) || null);
	}
	return declaredIn.get(cls);
}

// Whether some header declares an enum by this name (its values are numbers in Lua).
const enumNames = new Map();
function isEnum(name) {
	if (!enumNames.has(name)) {
		const re = new RegExp('\\benum\\s+(?:class\\s+)?' + name + '\\b');
		enumNames.set(name, [...headers.values()].some((file) => re.test(read(file))));
	}
	return enumNames.get(name);
}

function cppBase(cls) {
	if (classBases.has(cls)) {
		return classBases.get(cls);
	}
	let base = null;
	const file = headerFor(cls);
	if (file) {
		const m = new RegExp('\\b(?:class|struct)\\s+' + cls + '\\s*(?:final\\s*)?:\\s*public\\s+([\\w:]+)').exec(read(file));
		base = m ? m[1].split('::').pop() : null;
	}
	classBases.set(cls, base);
	return base;
}

// The /// comment just above line "at" of "text", as plain text.
function docAbove(text, at) {
	const before = text.slice(0, at).split('\n');
	before.pop();
	const lines = [];
	while (before.length) {
		const line = before[before.length - 1].trim();
		if (line.startsWith('///')) {
			lines.unshift(line.replace(/^\/\/\/\s?/, ''));
			before.pop();
		} else if (line.startsWith('//') && !lines.length) {
			lines.unshift(line.replace(/^\/\/\s?/, ''));
			before.pop();
		} else {
			break;
		}
	}
	return tidyDoc(lines);
}

function tidyDoc(lines) {
	const out = { text: [], params: {}, returns: '' };
	for (const line of lines) {
		const param = /^@param\s+(\w+)\s*(.*)$/.exec(line);
		const ret = /^@return\s*(.*)$/.exec(line);
		if (param) {
			out.params[param[1]] = param[2];
		} else if (ret) {
			out.returns = ret[1];
		} else if (!/^@|^(Description|Arguments|Return value):/.test(line) && line) {
			out.text.push(line);
		}
	}
	return out;
}

// Declarations of "name" in a header: [{returns, params, doc, isConst}]
function findDeclarations(file, name, inClass) {
	const text = read(file);
	const results = [];
	const re = new RegExp('(^|[\\s*&:~])' + name + '\\s*\\(', 'g');
	let m;
	while ((m = re.exec(text))) {
		const nameAt = m.index + m[1].length;
		// The line it's on, for the return type and to skip comments and calls.
		const lineStart = text.lastIndexOf('\n', nameAt) + 1;
		const head = text.slice(lineStart, nameAt);
		if (/\/\/|^\s*\*|return\b|[=(,!]\s*$|->\s*$|\.\s*$|[({;]/.test(head) || /\b(if|while|for|switch)\b/.test(head)) {
			continue;
		}
		if (/::\s*$/.test(head) && !new RegExp('\\b' + inClass + '::\\s*$').test(head)) {
			continue;
		}
		const open = text.indexOf('(', nameAt);
		const { inner, end } = balanced(text, open);
		const after = text.slice(end, end + 40);
		if (!/^\s*(const\s*)?(override\s*|final\s*|noexcept\s*)*(\{|;|=|->)/.test(after)) {
			continue;
		}
		const returns = head.replace(/\b(virtual|static|inline|constexpr|explicit|friend|\[\[nodiscard\]\])\b/g, '').replace(/\w+::\s*$/, '').trim();
		if (!returns || /^(public|private|protected):?$/.test(returns)) {
			continue; // a constructor or a call
		}
		const params = inner.trim() && inner.trim() !== 'void' ? splitTop(inner).map(parseParam) : [];
		results.push({ returns, params, doc: docAbove(text, lineStart + 1), isConst: /^\s*const/.test(after) });
	}
	return results;
}

// A member function of a C++ class, looked up in its header and then its bases' headers.
function lookUpMember(cls, name, arity) {
	for (let c = cls, guard = 0; c && guard < 20; c = cppBase(c), guard++) {
		const file = headerFor(c);
		if (!file) {
			continue;
		}
		const found = findDeclarations(file, name, c);
		if (found.length) {
			const exact = arity !== undefined ? found.find((f) => f.params.length === arity) : null;
			return exact || found[0];
		}
	}
	return null;
}

const adapterHeader = path.join(source, 'Lua', 'LuaAdapterDefinitions.h');
const adapterText = read(adapterHeader);

// A function an adapter struct declares ("LuaAdaptersActor::AddInventoryItem").
function lookUpAdapter(struct, name) {
	const at = adapterText.search(new RegExp('struct\\s+' + struct + '\\b'));
	if (at < 0) {
		return null;
	}
	const open = adapterText.indexOf('{', at);
	const { inner } = balanced(adapterText, open);
	const re = new RegExp('static\\s+([^;(]*?)\\b' + name + '\\s*\\(', 'g');
	const m = re.exec(inner);
	if (!m) {
		return null;
	}
	const paren = inner.indexOf('(', m.index + m[0].length - 1);
	const { inner: args } = balanced(inner, paren);
	return { returns: m[1].trim(), params: args.trim() ? splitTop(args).map(parseParam) : [], doc: docAbove(inner, inner.lastIndexOf('\n', m.index) + 1) };
}

// What a .def's function expression says about the function: {returns, params, doc} or null.
// selfFirst: an adapter or free function used as a method takes the object as its first argument, which Lua passes with ":".
function describeFunction(expr, ownerCls, selfFirst) {
	expr = expr.trim();
	// (Ret(Class::*)(Args) const) & Class::Name  or  (Ret (*)(Args)) & Name
	const cast = /^\((.*)\(\s*(?:[\w:]+::)?\*\s*\)\s*\((.*)\)\s*(?:const)?\s*\)\s*&\s*([\w:]+)$/s.exec(expr);
	if (cast) {
		const params = cast[2].trim() ? splitTop(cast[2]).map(parseParam) : [];
		const qualified = cast[3].split('::');
		const fn = qualified.pop();
		const owner = qualified.pop();
		const isMember = /\(\s*[\w:]+::\*\s*\)/.test(expr);
		let found = owner && !/^LuaAdapters/.test(owner) ? lookUpMember(owner, fn, params.length) : owner ? lookUpAdapter(owner, fn) : null;
		// Use the header's names for the arguments when the counts agree; the types come from the cast.
		if (found && found.params.length === params.length) {
			params.forEach((p, i) => {
				p.name = found.params[i].name;
				p.optional = found.params[i].optional;
			});
		}
		const result = { returns: cast[1].trim(), params: !isMember && selfFirst ? params.slice(1) : params, doc: found ? found.doc : null };
		return result;
	}
	const plain = /^&\s*([\w:]+)$/.exec(expr);
	if (plain) {
		const qualified = plain[1].split('::');
		const fn = qualified.pop();
		const owner = qualified.join('::');
		if (/^LuaAdapters/.test(owner)) {
			const found = lookUpAdapter(owner, fn);
			if (found && selfFirst) {
				found.params = found.params.slice(1);
			}
			return found;
		}
		if (owner) {
			return lookUpMember(owner.split('::').pop(), fn);
		}
		// A free function: look in every header that declares it.
		for (const file of headers.values()) {
			if (read(file).includes(fn + '(')) {
				const found = findDeclarations(file, fn, '');
				if (found.length) {
					return found[0];
				}
			}
		}
	}
	return null;
}

// ---- reading the bindings ----

const api = { classes: {}, globals: {}, functions: {}, generated: 'from Source/Lua by Tools/ModStudio/tools/make-lua-api.js' };

function classEntry(name) {
	if (!api.classes[name]) {
		api.classes[name] = { base: null, cpp: name, members: {}, ctors: [] };
	}
	return api.classes[name];
}

function addMember(cls, name, member) {
	const existing = cls.members[name];
	if (existing && existing.kind === 'method' && member.kind === 'method') {
		existing.overloads.push(...member.overloads);
		if (!existing.doc && member.doc) {
			existing.doc = member.doc;
		}
		return;
	}
	cls.members[name] = member;
}

function docText(doc) {
	return doc && doc.text.length ? doc.text.join(' ') : '';
}

function makeOverload(desc) {
	if (!desc) {
		return { params: null, returns: 'any' };
	}
	const docParams = (desc.doc && desc.doc.params) || {};
	return {
		params: desc.params.map((p) => Object.assign({}, p, docParams[p.name] ? { doc: docParams[p.name] } : {})),
		returns: luaType(desc.returns),
		returnsDoc: desc.doc && desc.doc.returns ? desc.doc.returns : undefined,
	};
}

const bindingFiles = fs.readdirSync(path.join(source, 'Lua')).filter((f) => /^LuaBindings\w*\.cpp$/.test(f)).map((f) => path.join(source, 'Lua', f));

// First pass: every bound class name, so types can be recognised.
const blocks = [];
for (const file of bindingFiles) {
	const text = stripLineComments(read(file));
	const re = /LuaBindingRegisterFunctionDefinitionForType\(\s*(\w+)\s*,\s*(\w+)\s*\)\s*\{/g;
	let m;
	while ((m = re.exec(text))) {
		const { inner } = balanced(text, m.index + m[0].length - 1);
		const head = /ConcreteTypeLuaClassDefinition\(\s*([\w:]+)\s*,\s*([\w:]+)\s*\)|AbstractTypeLuaClassDefinition\(\s*([\w:]+)\s*,\s*([\w:]+)\s*\)|luabind::class_<\s*([\w:]+)\s*(?:,\s*([\w:]+)\s*)?(?:,[^>]*)?>\s*\(\s*"(\w+)"\s*\)/.exec(inner);
		if (!head) {
			continue;
		}
		let cpp, base, name, concrete = false;
		if (head[1]) {
			[cpp, base, name, concrete] = [head[1], head[2], head[1], true];
		} else if (head[3]) {
			[cpp, base, name] = [head[3], head[4], head[3]];
		} else {
			[cpp, base, name] = [head[5], head[6] || null, head[7]];
		}
		blocks.push({ file, inner: inner.slice(head.index + head[0].length), cpp: cpp.split('::').pop(), cppFull: cpp, base: base ? base.split('::').pop() : null, name, concrete });
		boundClasses.add(name);
		cppToLua.set(cpp.split('::').pop(), name);
	}
}
// LuaMan.cpp binds LuaManager in place.
const luaMan = stripLineComments(read(path.join(source, 'Managers', 'LuaMan.cpp')));
{
	const at = luaMan.indexOf('luabind::class_<LuaStateWrapper>("LuaManager")');
	if (at >= 0) {
		const head = 'luabind::class_<LuaStateWrapper>("LuaManager")';
		blocks.push({ file: 'LuaMan.cpp', inner: luaMan.slice(at + head.length, luaMan.indexOf('luabind::def(', at)), cpp: 'LuaStateWrapper', cppFull: 'LuaStateWrapper', base: null, name: 'LuaManager', concrete: false });
		boundClasses.add('LuaManager');
		cppToLua.set('LuaStateWrapper', 'LuaManager');
	}
}

// Second pass: members.
for (const block of blocks) {
	const cls = classEntry(block.name);
	cls.base = block.base;
	cls.cpp = block.cppFull;
	cls.concrete = block.concrete;
	if (block.concrete) {
		addMember(cls, 'Clone', { kind: 'method', overloads: [{ params: [], returns: block.name }], doc: 'Makes a copy of this ' + block.name + ', which you own until you add it to the game.' });
	}
	if (block.concrete || /AbstractTypeLuaClassDefinition/.test(block.inner) || true) {
		cls.members.ClassName = cls.members.ClassName || { kind: 'property', type: 'string', readonly: true, doc: 'The C++ class name, like "' + block.name + '".' };
	}
	const classDoc = (() => {
		const file = headerFor(block.cpp);
		if (!file) {
			return '';
		}
		const text = read(file);
		const at = text.search(new RegExp('\\n\\s*(?:class|struct)\\s+' + block.cpp + '\\b'));
		return at >= 0 ? docText(docAbove(text, at + 2)) : '';
	})();
	if (classDoc) {
		cls.doc = classDoc;
	}
	const body = block.inner;
	const re = /\.(def_readonly|def_readwrite|def|property|enum_)\s*\(/g;
	let m;
	while ((m = re.exec(body))) {
		const lineStart = body.lastIndexOf('\n', m.index) + 1;
		if (/\/\//.test(body.slice(lineStart, m.index))) {
			continue; // commented out
		}
		const open = m.index + m[0].length - 1;
		const { inner, end } = balanced(body, open);
		const args = splitTop(inner);
		const kind = m[1];
		const nameMatch = /^"(\w+)"$/.exec(args[0] || '');
		if (kind === 'def' && !nameMatch) {
			const ctor = /^luabind::constructor<(.*)>\s*\(\s*\)$/s.exec(args[0] || '');
			if (ctor) {
				cls.ctors.push((ctor[1].trim() ? splitTop(ctor[1]) : []).map((t, i) => ({ name: 'arg' + (i + 1), type: luaType(t), optional: false })));
			}
			continue;
		}
		if (kind === 'enum_') {
			const bracket = body.indexOf('[', end);
			if (bracket >= 0 && /^\s*$/.test(body.slice(end, bracket))) {
				const { inner: values } = balanced(body, bracket);
				const enumName = nameMatch ? nameMatch[1] : '';
				for (const v of values.matchAll(/luabind::value\(\s*"(\w+)"\s*,\s*([^)]+)\)/g)) {
					cls.members[v[1]] = { kind: 'const', type: 'number', doc: enumName ? 'One of ' + block.name + '.' + enumName + '.' : '' };
				}
			}
			continue;
		}
		if (!nameMatch) {
			continue;
		}
		const name = nameMatch[1];
		if (kind === 'def') {
			const desc = describeFunction(args[1] || '', block.cpp, true);
			addMember(cls, name, { kind: 'method', overloads: [makeOverload(desc)], doc: docText(desc && desc.doc) });
		} else if (kind === 'property') {
			const getter = describeFunction(args[1] || '', block.cpp, true);
			const iter = args.some((a) => /return_stl_iterator/.test(a));
			let type = getter ? luaType(getter.returns) : 'any';
			if (iter && !type.endsWith('[]')) {
				type += '[]';
			}
			const setter = args[2] && !/^luabind::/.test(args[2]);
			cls.members[name] = { kind: 'property', type, readonly: !setter, iterator: iter || undefined, doc: docText(getter && getter.doc) };
		} else {
			const field = /&\s*([\w:]+)::(\w+)/.exec(args[1] || '');
			let type = 'any';
			let doc = '';
			if (field) {
				const owner = field[1].split('::').pop();
				for (let c = owner, guard = 0; c && guard < 20; c = cppBase(c), guard++) {
					const file = headerFor(c);
					const text = file ? read(file) : '';
					const decl = new RegExp('\\n([ \\t]*)([\\w:<>,\\s*&]+?)\\s+' + field[2] + '\\s*(?:=[^;]*|\\{[^}]*\\})?;').exec(text);
					if (decl) {
						type = luaType(decl[2]);
						const comment = new RegExp(field[2] + '[^\\n]*//!<\\s*(.*)').exec(text);
						doc = comment ? comment[1] : '';
						break;
					}
				}
			}
			cls.members[name] = { kind: 'field', type, readonly: kind === 'def_readonly', doc };
		}
	}
}

// ---- globals and free functions (LuaMan.cpp) ----

for (const m of luaMan.matchAll(/luabind::globals\(m_State\)\["(\w+)"\]\s*=\s*&?(\w+)/g)) {
	const type = m[2] === 'this' ? 'LuaManager' : cppToLua.get(m[2].replace(/^g_/, '')) || m[2].replace(/^g_/, '');
	api.globals[m[1]] = boundClasses.has(type) ? type : 'any';
}
const moduleStart = luaMan.indexOf('luabind::module(m_State)[');
if (moduleStart >= 0) {
	const { inner } = balanced(luaMan, luaMan.indexOf('[', moduleStart));
	for (const part of splitTop(inner)) {
		const def = /^luabind::def\(\s*"(\w+)"\s*,\s*(.*)\)$/s.exec(part);
		if (def) {
			const args = splitTop(def[2]);
			const desc = describeFunction(args[0], '', false);
			const fn = api.functions[def[1]] || (api.functions[def[1]] = { overloads: [], doc: '' });
			fn.overloads.push(makeOverload(desc));
			fn.doc = fn.doc || docText(desc && desc.doc);
			continue;
		}
		const typed = /^RegisterLuaBindingsOf(Concrete|Abstract)?Type\(\s*\w+\s*,\s*(\w+)\s*\)$/.exec(part);
		if (typed && typed[1]) {
			const type = typed[2];
			const add = (name, params, returns, doc) => {
				const fn = api.functions[name] || (api.functions[name] = { overloads: [], doc });
				fn.overloads.push({ params, returns });
			};
			const entity = { name: 'entity', type: 'Entity', optional: false };
			add('To' + type, [entity], type, 'Treats an object as a ' + type + ', for its ' + type + ' functions and properties. Use Is' + type + ' first when it might be something else.');
			add('Is' + type, [entity], 'boolean', 'Whether an object is a ' + type + ' (or a kind of one).');
			if (typed[1] === 'Concrete') {
				const preset = { name: 'presetName', type: 'string', optional: false };
				const module = { name: 'moduleName', type: 'string', optional: false };
				const group = { name: 'groupName', type: 'string', optional: false };
				add('Create' + type, [preset, module], type, 'Makes a new ' + type + ' from a preset, by its PresetName (optionally in a module like "Base.rte"). Add it to the game with MovableMan:AddParticle, AddActor or AddItem.');
				add('Create' + type, [preset], type);
				add('Random' + type, [group, { name: 'moduleID', type: 'number', optional: false }], type, 'Makes a random ' + type + ' from a group.');
				add('Random' + type, [group, module], type);
				add('Random' + type, [group], type);
			}
		}
	}
}

// The manager and class tables Lua sees by name (Timer(), Vector(), and the enum holders) are the classes themselves.

// ---- script callbacks ----

const callbacks = { entity: {}, activity: {} };
for (const file of headers.values()) {
	const text = read(file);
	for (const m of text.matchAll(/\b(Add)?ScriptFunctionNames\(\s*(?:(\w+)\s*,\s*)?((?:"\w+"\s*,?\s*)+)\)/g)) {
		const cls = path.basename(file, '.h');
		if (m[1] && !m[2]) {
			continue;
		}
		const names = [...m[3].matchAll(/"(\w+)"/g)].map((x) => x[1]);
		const group = /GAScripted|Activity/.test(cls) ? callbacks.activity : callbacks.entity;
		for (const name of names) {
			if (!group[name]) {
				group[name] = { classes: [] };
			}
			group[name].classes.push(cls);
		}
	}
}
api.callbacks = callbacks;

// ---- write ----

fs.mkdirSync(outDir, { recursive: true });
fs.writeFileSync(path.join(outDir, 'lua-api.json'), JSON.stringify(api));

// LuaLS annotations, for VS Code with the Lua extension: point "Lua.workspace.library" at this file.
const lines = ['---@meta', '-- Cortex Command\'s Lua API, written by Tools/ModStudio/tools/make-lua-api.js from the game\'s bindings.', ''];
const lsType = (t) => (t === 'nil' ? 'nil' : t || 'any');
for (const [name, cls] of Object.entries(api.classes)) {
	if (cls.doc) {
		lines.push('--- ' + cls.doc);
	}
	lines.push('---@class ' + name + (cls.base ? ' : ' + cls.base : ''));
	for (const [key, member] of Object.entries(cls.members)) {
		if (member.kind !== 'method') {
			lines.push('---@field ' + key + ' ' + lsType(member.type) + (member.doc ? ' ' + member.doc.replace(/\n/g, ' ') : ''));
		}
	}
	lines.push(name + ' = {}', '');
	for (const [key, member] of Object.entries(cls.members)) {
		if (member.kind !== 'method') {
			continue;
		}
		member.overloads.forEach((o, i) => {
			if (member.doc && i === 0) {
				lines.push('--- ' + member.doc);
			}
			const params = o.params || [{ name: '...', type: 'any' }];
			for (const p of params) {
				if (p.name !== '...') {
					lines.push('---@param ' + p.name + (p.optional ? '?' : '') + ' ' + lsType(p.type) + (p.doc ? ' ' + p.doc : ''));
				}
			}
			if (o.returns && o.returns !== 'nil') {
				lines.push('---@return ' + lsType(o.returns));
			}
			lines.push('function ' + name + ':' + key + '(' + params.map((p) => p.name).join(', ') + ') end', '');
		});
	}
}
for (const [name, type] of Object.entries(api.globals)) {
	lines.push('---@type ' + type, name + ' = nil', '');
}
for (const [name, fn] of Object.entries(api.functions)) {
	fn.overloads.forEach((o, i) => {
		if (fn.doc && i === 0) {
			lines.push('--- ' + fn.doc);
		}
		const params = o.params || [{ name: '...', type: 'any' }];
		for (const p of params) {
			if (p.name !== '...') {
				lines.push('---@param ' + p.name + (p.optional ? '?' : '') + ' ' + lsType(p.type));
			}
		}
		if (o.returns && o.returns !== 'nil') {
			lines.push('---@return ' + lsType(o.returns));
		}
		lines.push('function ' + name + '(' + params.map((p) => p.name).join(', ') + ') end', '');
	});
}
fs.writeFileSync(path.join(outDir, 'lua-api.d.lua'), lines.join('\n'));

const classCount = Object.keys(api.classes).length;
const memberCount = Object.values(api.classes).reduce((n, c) => n + Object.keys(c.members).length, 0);
const typed = Object.values(api.classes).reduce((n, c) => n + Object.values(c.members).filter((m) => (m.kind === 'method' ? m.overloads.every((o) => o.params) : m.type !== 'any')).length, 0);
console.log('Wrote ' + classCount + ' classes, ' + memberCount + ' members (' + typed + ' with types), ' + Object.keys(api.functions).length + ' functions, ' + Object.keys(api.globals).length + ' globals.');
