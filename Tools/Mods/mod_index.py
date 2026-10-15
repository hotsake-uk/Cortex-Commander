"""Reads a module (a .rte folder) the way the game does: its INI files from Index.ini through every IncludeFile, and its Lua scripts.

Used by engine_surface.py (to index what Data/ defines) and Check-Mod.py (to check a mod). Nothing here changes any file.
"""
import os, re

LUA_KEYWORDS = set('and break do else elseif end false for function goto if in local nil not or repeat return then true until while'.split())


def read_text(path):
	with open(path, 'rb') as f:
		raw = f.read()
	try:
		return raw.decode('utf-8'), 'utf-8'
	except UnicodeDecodeError:
		return raw.decode('latin-1'), 'latin-1'


class Modules:
	"""Finds module folders by name ("Base.rte") in the places the game looks: the mod being checked first, then Data/, then Mods/."""

	def __init__(self, *roots, extra=None):
		self.byName = {}
		for name, path in (extra or {}).items():
			self.byName.setdefault(name, path)
		for root in roots:
			if root and os.path.isdir(root):
				for name in sorted(os.listdir(root)):
					path = os.path.join(root, name)
					if name.lower().endswith('.rte') and os.path.isdir(path):
						self.byName.setdefault(name, path)
		self.lowerNames = {name.lower(): name for name in self.byName}
		self._listings = {}

	def module_of(self, gamePath):
		gamePath = re.sub(r'^(?:Data|Mods)/(?=[^/]+\.rte)', '', gamePath.replace('\\', '/').lstrip('/'), flags=re.I)
		first = gamePath.split('/', 1)[0]
		if first in self.byName:
			return first
		return self.lowerNames.get(first.lower())

	def _entries(self, folder):
		if folder not in self._listings:
			try:
				self._listings[folder] = os.listdir(folder)
			except OSError:
				self._listings[folder] = None
		return self._listings[folder]

	def resolve(self, gamePath):
		"""A game path ("Base.rte/Actors/X.png") on disk: (exact path or None, path with only the letter case differing or None)."""
		gamePath = gamePath.replace('\\', '/').strip().lstrip('/')
		# The game also takes paths that start with the folder the module is in.
		gamePath = re.sub(r'^(?:Data|Mods)/(?=[^/]+\.rte/)', '', gamePath, flags=re.I)
		parts = [p for p in gamePath.split('/') if p and p != '.']
		if not parts:
			return None, None
		module = self.module_of(parts[0])
		if not module:
			return None, None
		exact = module == parts[0]
		current = self.byName[module]
		for part in parts[1:]:
			entries = self._entries(current)
			if entries is None:
				return None, None
			if part in entries:
				current = os.path.join(current, part)
				continue
			match = next((e for e in entries if e.lower() == part.lower()), None)
			if match is None:
				return None, None
			exact = False
			current = os.path.join(current, match)
		return (current, None) if exact else (None, current)

	def game_path(self, diskPath):
		"""The game path for a file on disk under one of the modules, or None."""
		diskPath = os.path.normpath(diskPath)
		for name, folder in self.byName.items():
			folder = os.path.normpath(folder)
			if diskPath == folder or diskPath.startswith(folder + os.sep):
				return (name + diskPath[len(folder):]).replace(os.sep, '/')
		return None


class IniLine:
	__slots__ = ('file', 'line', 'indent', 'key', 'value', 'spaceIndent')

	def __init__(self, file, line, indent, key, value, spaceIndent=False):
		self.file, self.line, self.indent, self.key, self.value, self.spaceIndent = file, line, indent, key, value, spaceIndent


def ini_lines(path):
	"""The property lines of one INI file: (line number, tab count, name, value, whether spaces were used to indent). Comments dropped."""
	text, _ = read_text(path)
	text = text.replace('\r\n', '\n').replace('\r', '\n')
	out = []
	depth = 0  # nested /* */ comments
	for number, raw in enumerate(text.split('\n'), 1):
		line, i, clean = raw, 0, []
		while i < len(line):
			if depth:
				if line.startswith('*/', i):
					depth -= 1
					i += 2
				elif line.startswith('/*', i):
					depth += 1
					i += 2
				else:
					i += 1
				continue
			if line.startswith('//', i):
				break
			if line.startswith('/*', i):
				depth += 1
				i += 2
				continue
			clean.append(line[i])
			i += 1
		clean = ''.join(clean)
		if not clean.strip():
			continue
		lead = re.match(r'[ \t]*', clean).group(0)
		indent = lead.count('\t')
		body = clean[len(lead):]
		if '=' in body:
			key, value = body.split('=', 1)
			# The game stops reading a value at a tab.
			value = value.split('\t', 1)[0]
			out.append((number, indent, key.strip(), value.strip(), ' ' in lead))
		else:
			out.append((number, indent, None, body.split('\t', 1)[0].strip(), ' ' in lead))
	return out


def read_module_ini(moduleDir, modules, problems=None):
	"""Every INI line the game reads for this module, in order, following IncludeFile from Index.ini (and every INI file in the module when it
	says ScanFolderContents). Indents are absolute: an included file's top level is the level of its IncludeFile line.
	problems, when given, gets (kind, file, line, detail) for includes that can't be followed."""
	seen = set()
	lines = []

	def read(path, base, chain):
		real = os.path.normcase(os.path.abspath(path))
		if real in chain:
			if problems is not None:
				problems.append(('include-loop', path, 0, ''))
			return
		seen.add(real)
		for number, indent, key, value, spaces in ini_lines(path):
			lines.append(IniLine(path, number, base + indent, key, value, spaces))
			if key == 'IncludeFile':
				exact, wrongCase = modules.resolve(value)
				target = exact or wrongCase
				if problems is not None and not exact:
					problems.append(('include-missing' if not target else 'include-case', path, number, value))
				if target and os.path.isfile(target):
					read(target, base + indent, chain | {real})

	index = os.path.join(moduleDir, 'Index.ini')
	if os.path.isfile(index):
		read(index, 0, frozenset())
		if any(l.key == 'ScanFolderContents' and l.value.strip() in ('1', 'true', 'True') for l in lines):
			for folder, _, names in os.walk(moduleDir):
				for name in sorted(names):
					path = os.path.join(folder, name)
					if name.lower().endswith('.ini') and os.path.normcase(os.path.abspath(path)) not in seen:
						read(path, 1, frozenset())
	return lines


def walk_ini(lines, surface):
	"""Pairs each INI line with the class of the object it belongs to and the class of the object it starts (if its value names a class).
	Yields (line, ownerClass, valueClass, depth). ownerClass is None where the type isn't known (a property whose value isn't a class name)."""
	classes = surface['ini']['classes']
	stack = []  # (indent, class)
	for l in lines:
		if l.key is None:
			# A bare word: the DataModule header of Index.ini, or a value on its own line.
			if l.indent == 0 and l.value == 'DataModule':
				stack = [(-1, None), (0, 'DataModule')]
			continue
		while stack and stack[-1][0] >= l.indent:
			stack.pop()
		owner = stack[-1][1] if stack else 'DataModule'
		if not stack and l.indent > 0:
			owner = 'DataModule'
		valueClass = l.value if l.value in classes else None
		yield l, owner, valueClass, l.indent
		stack.append((l.indent, valueClass))


def index_presets(lines, surface, module):
	"""Presets the lines define: list of (class, name, module, file, line)."""
	out = []
	for l, owner, valueClass, depth in walk_ini(lines, surface):
		if l.key in ('PresetName', 'InstanceName') and owner:
			name = l.value.split('/', 1)[1] if '/' in l.value and '.rte' in l.value.split('/', 1)[0] else l.value
			out.append((owner, name, module, l.file, l.line))
	return out


# ---------------------------------------------------------------- Lua

_tokenRe = re.compile(r'''
	(?P<ws>[ \t\r\f\v]+) |
	(?P<nl>\n) |
	(?P<longcomment>--\[(?P<lceq>=*)\[) |
	(?P<comment>--[^\n]*) |
	(?P<longstring>\[(?P<lseq>=*)\[) |
	(?P<string>"(?:\\.|\\\n|[^"\\\n])*"|'(?:\\.|\\\n|[^'\\\n])*') |
	(?P<number>0[xX][0-9a-fA-F]*(?:\.[0-9a-fA-F]*)?(?:[pP][+-]?\d+)?|(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?) |
	(?P<name>[A-Za-z_][A-Za-z0-9_]*) |
	(?P<op>\.\.\.|\.\.|==|~=|<=|>=|::|<<|>>|//|[-+*/%^\#&~|<>=(){}\[\];:,.])
''', re.X)


def lua_tokens(text):
	"""(kind, value, line) for each token; kind is name, string (value without quotes), number, op or keyword. Comments dropped."""
	out, i, line, n = [], 0, 1, len(text)
	while i < n:
		m = _tokenRe.match(text, i)
		if not m:
			i += 1
			continue
		kind = m.lastgroup
		if kind in ('lceq', 'lseq'):
			kind = 'longcomment' if m.group('longcomment') else 'longstring'
		if kind == 'nl':
			line += 1
		elif kind in ('longcomment', 'longstring'):
			eq = m.group('lceq') if kind == 'longcomment' else m.group('lseq')
			close = text.find(']' + eq + ']', m.end())
			end = n if close < 0 else close + len(eq) + 2
			if kind == 'longstring':
				out.append(('string', text[m.end():close if close >= 0 else n], line))
			line += text.count('\n', m.start(), end)
			i = end
			continue
		elif kind == 'string':
			raw = m.group(0)
			out.append(('string', raw[1:-1], line))
			line += raw.count('\n')
		elif kind == 'name':
			out.append(('keyword' if m.group(0) in LUA_KEYWORDS else 'name', m.group(0), line))
		elif kind in ('number', 'op'):
			out.append((kind, m.group(0), line))
		i = m.end()
	return out


class LuaFile:
	"""What one script defines and uses, with Lua's scoping applied: a name declared local (or a parameter) isn't a global where it's visible."""

	def __init__(self, path, text=None):
		self.path = path
		if text is None:
			text, _ = read_text(path)
		self.globalDefs = {}     # name -> first line it's assigned as a global
		self.topLevelDefs = set()  # globals made when the file runs (not only once some function of it is called)
		self.memberDefs = set()  # names defined as fields: function a.b:c(), x.y = ..., { key = ... }
		self.freeUses = []       # (name, line, how) for globals read: how is call, index or read
		self.memberUses = []     # (base or None, separator, name, line, isCall): base is the global name before it, when it is one
		self.stringCalls = []    # (function name, [string args], line) for calls whose arguments start with string literals
		self.strings = []        # (string, line) every literal
		self._analyse(lua_tokens(text))

	def _analyse(self, toks):
		scopes = [{'locals': set(), 'awaitDo': False}]
		tables = []  # brace stack: True for a table constructor
		n = len(toks)

		def visible(name):
			return any(name in s['locals'] for s in scopes)

		def tok(j):
			return toks[j] if 0 <= j < n else (None, None, 0)

		i = 0
		while i < n:
			kind, value, line = toks[i]
			if kind == 'string':
				self.strings.append((value, line))
				# _G["name"] = ... makes a global.
				if tok(i - 2)[1] == '_G' and tok(i - 1)[1] == '[' and tok(i + 1)[1] == ']' and tok(i + 2)[1] == '=' and re.fullmatch(r'[A-Za-z_]\w*', value):
					self._define(value, line, scopes)
			if kind == 'keyword':
				if value == 'local':
					k2, v2, _ = tok(i + 1)
					if v2 == 'function':
						k3, v3, l3 = tok(i + 2)
						if k3 == 'name':
							scopes[-1]['locals'].add(v3)
						i = self._function(toks, i + 1, scopes, named=False)
						continue
					j = i + 1
					while tok(j)[0] == 'name':
						scopes[-1]['locals'].add(tok(j)[1])
						j += 1
						if tok(j)[1] == '<':  # <const> / <close>
							j += 3
						if tok(j)[1] != ',':
							break
						j += 1
					i = j
					continue
				if value == 'function':
					i = self._function(toks, i, scopes, named=True)
					continue
				if value in ('while', 'for'):
					scope = {'locals': set(), 'awaitDo': True}
					scopes.append(scope)
					if value == 'for':
						j = i + 1
						while tok(j)[0] == 'name':
							scope['locals'].add(tok(j)[1])
							j += 1
							if tok(j)[1] != ',':
								break
							j += 1
						i = j
						continue
				elif value == 'do':
					if scopes[-1]['awaitDo']:
						scopes[-1]['awaitDo'] = False
					else:
						scopes.append({'locals': set(), 'awaitDo': False})
				elif value in ('if', 'repeat'):
					scopes.append({'locals': set(), 'awaitDo': False})
				elif value in ('elseif', 'else'):
					scopes[-1]['locals'] = set()
				elif value in ('end', 'until'):
					if len(scopes) > 1:
						scopes.pop()
				elif value == 'goto':
					i += 2
					continue
				i += 1
				continue
			if kind == 'op':
				if value == '{':
					tables.append(True)
				elif value == '}':
					if tables:
						tables.pop()
				elif value == '::':  # ::label::
					i += 3
					continue
				i += 1
				continue
			if kind == 'name':
				prevKind, prev, _ = tok(i - 1)
				nextKind, nxt, _ = tok(i + 1)
				if prev in ('.', ':'):
					baseKind, base, _ = tok(i - 2)
					beforeBase = tok(i - 3)[1]
					isFreeBase = baseKind == 'name' and beforeBase not in ('.', ':') and not visible(base)
					isCall = nxt == '(' or nextKind == 'string' or nxt == '{'
					self.memberUses.append((base if isFreeBase else None, prev, value, line, isCall))
					if nxt == '=' and prev == '.':
						self.memberDefs.add(value)
						if base == '_G':
							self._define(value, line, scopes)
				elif tables and nxt == '=' and prev in ('{', ',', ';'):
					self.memberDefs.add(value)
				elif not visible(value):
					if nxt == '=':
						self._define(value, line, scopes)
					elif nxt == ',' and self._is_assignment_list(toks, i):
						self._define(value, line, scopes)
					else:
						how = 'call' if (nxt == '(' or nextKind == 'string' or nxt == '{') else 'index' if nxt in ('.', ':', '[') else 'read'
						self.freeUses.append((value, line, how))
				if (nxt == '(' or nextKind == 'string') and prev != '.':
					args, j = [], i + 2 if nxt == '(' else i + 1
					# Only whole strings: "Name " .. n is built while the game runs.
					if nextKind == 'string':
						args.append(tok(i + 1)[1])
					else:
						while tok(j)[0] == 'string' and tok(j + 1)[1] in (',', ')'):
							args.append(tok(j)[1])
							if tok(j + 1)[1] != ',':
								break
							j += 2
					if args:
						self.stringCalls.append((value, args, line))
			i += 1

	def _define(self, name, line, scopes):
		self.globalDefs.setdefault(name, line)
		if not any(s.get('function') for s in scopes):
			self.topLevelDefs.add(name)

	def _is_assignment_list(self, toks, i):
		j = i
		while j + 1 < len(toks) and toks[j + 1][1] == ',' and j + 2 < len(toks) and toks[j + 2][0] == 'name':
			j += 2
		return j + 1 < len(toks) and toks[j + 1][1] == '='

	def _function(self, toks, i, scopes, named):
		"""At a 'function' keyword: records its name (a global, or a member of a table), opens its scope with its parameters. Returns the next index."""
		n = len(toks)
		j = i + 1
		isMethod = False
		if named and j < n and toks[j][0] == 'name':
			parts = [toks[j][1]]
			j += 1
			while j + 1 < n and toks[j][1] in ('.', ':') and toks[j + 1][0] == 'name':
				isMethod = toks[j][1] == ':'
				parts.append(toks[j + 1][1])
				j += 2
			if not any(parts[0] in s['locals'] for s in scopes):
				# function Name() defines a global; function Name.x() / Name:x() fills a table the engine or the script made.
				self._define(parts[0], toks[i][2], scopes)
			self.memberDefs.update(parts[1:])
		elif not named and j < n and toks[j][0] == 'name':
			j += 1
		scope = {'locals': set(['self'] if isMethod else []), 'awaitDo': False, 'function': True}
		if j < n and toks[j][1] == '(':
			j += 1
			while j < n and toks[j][1] != ')':
				if toks[j][0] == 'name':
					scope['locals'].add(toks[j][1])
				j += 1
			j += 1
		scopes.append(scope)
		return j


def lua_files(moduleDir):
	for folder, dirs, names in os.walk(moduleDir):
		dirs.sort()
		for name in sorted(names):
			if name.lower().endswith('.lua'):
				yield os.path.join(folder, name)


def index_folder(folder, surface):
	"""What the modules in a folder (Data/ or Mods/) define, for other modules to rely on: presets, Lua globals and Lua members."""
	modules = Modules(folder)
	result = {'modules': {}, 'lua': {'globals': {}, 'members': set()}}
	for name, path in modules.byName.items():
		lines = read_module_ini(path, modules)
		result['modules'][name] = {'presets': [(c, p) for c, p, _, _, _ in index_presets(lines, surface, name)]}
		for script in lua_files(path):
			try:
				lua = LuaFile(script)
			except Exception:
				continue
			# Only what a script makes when it runs: a global first set inside a function only exists once that function has been called
			# (Base.rte's BrainVsBrain sets a global Craft that way), so another mod can't count on it.
			for g in lua.topLevelDefs:
				result['lua']['globals'].setdefault(g, name)
			result['lua']['members'].update(lua.memberDefs)
	result['lua']['members'] = sorted(result['lua']['members'])
	return result
