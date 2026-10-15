"""Checks a mod against this version of the game without starting it, and fixes what can be fixed by rule.

	python Tools/Mods/Check-Mod.py Mods/SomeMod.rte          one mod (a .rte folder)
	python Tools/Mods/Check-Mod.py path/to/SomeMod.zip       one downloaded zip, checked where it is (fixing needs it unpacked)
	python Tools/Mods/Check-Mod.py --all                     every mod in Mods/ and ModsParked/, one line each
	python Tools/Mods/Check-Mod.py --all --fix               fix by rule: shows the changes, asks, keeps a .bak of each file it changes

Other options: --mods DIR (where other mods live, for dependencies; default Mods/ and ModsParked/, or CCCP_MODS_DIR),
--yes (don't ask before fixing), --quiet (summary only), --no-report (don't write ModChecks/<mod>.txt),
--all-scripts (check scripts nothing in the mod uses too: for a mod other mods borrow scripts from).

What the game supports is read from this checkout's source (engine_surface.py), so the check matches the branch you're on.
Each finding is an ERROR (it will break: the mod or a script won't load, or a script stops with an error), a WARNING (part of it won't work:
a setting the game skips, a missing picture or sound, a call that is likely wrong) or a NOTE. "fixable" ones are changed by --fix.
Exit code: 1 if any ERROR is left, else 0.

It reads code and doesn't run it, so names built while the game runs (a preset name glued together from strings, a function looked up by
name) aren't checked, and a method called on an object can only be checked against every class at once, not the object's own.
"""
import argparse, atexit, json, os, re, shutil, sys, tempfile, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import engine_surface, mod_index  # noqa: E402

ROOT = engine_surface.ROOT
ORDER = {'ERROR': 0, 'WARNING': 1, 'NOTE': 2}
FILE_KEYS = {'FilePath', 'Path', 'ScriptPath', 'IconFile', 'SkinFile', 'BannerFile', 'LogoFile'}
PATH_RE = re.compile(r"^[\w .,'&()+!#-]+\.rte[/\\][^\"*?<>|]+\.(?:png|bmp|wav|ogg|flac|mp3|lua|ini)$", re.I)
AUDIO = ('.wav', '.ogg', '.flac')
IMAGE = ('.png', '.bmp')


class Finding:
	def __init__(self, severity, path, line, message, fix=None):
		self.severity, self.path, self.line, self.message, self.fix = severity, path, line, message, fix


class History:
	"""mod_history.json, looked up by old name."""

	def __init__(self, surface):
		with open(os.path.join(HERE, 'mod_history.json'), encoding='utf-8') as f:
			changes = json.load(f)['changes']
		self.byKind = {}
		managers = surface['lua']['globals']['managers']
		for c in changes:
			old, cls = c['old'] or '', c['class']
			if '.' in old and ' ' not in old:
				cls, old = old.split('.', 1)
			cls = managers.get(cls, cls)
			self.byKind.setdefault(c['kind'], {}).setdefault(old, []).append(dict(c, old=old, cls=cls))

	def find(self, kind, old, cls=None):
		entries = self.byKind.get(kind, {}).get(old, [])
		if cls:
			same = [e for e in entries if e['cls'] in (cls, None)]
			entries = same or entries
		return entries[0] if entries else None


def a(word):
	return ('an ' if word[:1] in 'AEIOU' else 'a ') + word


def describe(entry):
	text = entry['note'].rstrip('.')
	where = '' if entry['version'] == 'Cortex Commander' else ' (community edition %s)' % entry['version'].lstrip('v') if entry['version'] != 'Unreleased' else ' (community edition, after 6.2)'
	return text + where + '.'


class Checker:
	def __init__(self, surface, modsDirs):
		self.surface = surface
		self.history = History(surface)
		self.modsDirs = [d for d in modsDirs if d and os.path.isdir(d)]
		lua = surface['lua']
		self.managers = lua['globals']['managers']
		self.luaClasses = lua['classes']
		self.engineFunctions = set(lua['globals']['functions'])
		self.engineMembers = set()
		for c in self.luaClasses.values():
			self.engineMembers.update(c['members'])
		self.knownGlobals = set(lua['builtins']) | self.engineFunctions | set(self.managers) | set(self.luaClasses) | set(lua['globals']['scripted'])
		self.knownGlobals |= set(surface['data']['lua']['globals'])
		self.knownMembers = set(surface['data']['lua']['members']) | set(lua['valueMethods'])
		self.iniClasses = surface['ini']['classes']
		self.officialModules = set(surface['data']['modules'])
		# Presets the game ships: name -> [(class, module)]
		self.presets = {}
		for module, info in surface['data']['modules'].items():
			for cls, name in info['presets']:
				self.presets.setdefault(name, []).append((cls, module))
		self._otherMods = None

	def other_mods(self):
		"""Other mods present (for dependencies): module name -> {'path', 'presets', 'globals'}; read when first needed."""
		if self._otherMods is None:
			self._otherMods = {}
			for folder in self.modsDirs:
				modules = mod_index.Modules(folder, os.path.join(ROOT, 'Data'))
				for name, path in modules.byName.items():
					if name in self.officialModules or name in self._otherMods:
						continue
					lines = mod_index.read_module_ini(path, modules)
					info = {'path': path, 'presets': [(c, p) for c, p, _, _, _ in mod_index.index_presets(lines, self.surface, name)], 'globals': set(), 'members': set()}
					for script in mod_index.lua_files(path):
						try:
							lua = mod_index.LuaFile(script)
						except Exception:
							continue
						info['globals'].update(lua.topLevelDefs)
						info['members'].update(lua.memberDefs)
					self._otherMods[name] = info
		return self._otherMods

	def inherits(self, cls, ancestor):
		seen = set()
		while cls and cls not in seen:
			if cls == ancestor:
				return True
			seen.add(cls)
			cls = self.iniClasses.get(cls, {}).get('parent')
		return False

	# ------------------------------------------------------------ one mod

	def check(self, modDir, moduleName=None):
		modDir = os.path.abspath(modDir)
		moduleName = moduleName or os.path.basename(modDir.rstrip('/\\'))
		self.findings = []
		self.needs = {}  # module -> (path, line) where first needed
		self.moduleName = moduleName
		self.modDir = modDir
		self.modules = mod_index.Modules(os.path.join(ROOT, 'Data'), *self.modsDirs, extra={moduleName: modDir})
		if not os.path.isfile(os.path.join(modDir, 'Index.ini')):
			self.add('ERROR', modDir, 0, 'No Index.ini: this isn\'t a module the game can load (the .rte folder may be one level further in).')
			return self.findings
		problems = []
		lines = mod_index.read_module_ini(modDir, self.modules, problems)
		self.own = {}  # this mod's presets: name -> [class]
		for cls, name, _, _, _ in mod_index.index_presets(lines, self.surface, moduleName):
			self.own.setdefault(name, []).append(cls)
		self.check_includes(problems)
		self.check_index(lines)
		self.check_ini(lines)
		self.check_lua(lines)
		self.check_dependencies()
		self.findings.sort(key=lambda f: (ORDER[f.severity], f.path, f.line))
		return self.findings

	def add(self, severity, path, line, message, fix=None):
		self.findings.append(Finding(severity, path, line, message, fix))

	def need(self, module, path, line):
		if module and module != self.moduleName and module not in self.officialModules:
			self.needs.setdefault(module, (path, line))

	def check_includes(self, problems):
		for kind, path, line, detail in problems:
			if kind == 'include-missing':
				module = detail.replace('\\', '/').split('/', 1)[0]
				if self.modules.module_of(module) or not module.lower().endswith('.rte'):
					self.add('ERROR', path, line, 'IncludeFile %s doesn\'t exist, so the game can\'t load this mod.' % detail)
				else:
					self.need(module, path, line)
			elif kind == 'include-case':
				exact, wrong = self.modules.resolve(detail)
				right = self.modules.game_path(wrong)
				self.add('ERROR', path, line, 'IncludeFile %s only exists as %s; the game matches letter case, so it can\'t load this mod.' % (detail, right), fix=('line', path, line, detail, right))
			elif kind == 'include-loop':
				self.add('ERROR', path, line, 'This file includes itself (through IncludeFile), which the game can\'t read.')

	def check_index(self, lines):
		index = os.path.join(self.modDir, 'Index.ini')
		version = next((l for l in lines if l.file == index and l.key == 'SupportedGameVersion'), None)
		ours = self.surface['version']
		if not version:
			self.add('NOTE', index, 1, 'No SupportedGameVersion; the game loads it anyway (this is %s).' % ours)
		else:
			m = re.match(r'^(\d+)\.(\d+)\.(\d+)$', version.value)
			if not m:
				self.add('NOTE', index, version.line, 'SupportedGameVersion "%s" is from before version numbers were settled (made for a game before 6.0): loaded anyway, expect some of it not to work.' % version.value)
			else:
				theirs = tuple(int(x) for x in m.groups())
				if theirs[0] < 6 or theirs > tuple(int(x) for x in ours.split('.')):
					self.add('NOTE', index, version.line, 'Made for %s (this is %s); the game loads it anyway.' % (version.value, ours))
		for l in lines:
			if l.file == index and l.key == 'Require':
				if not self.modules.module_of(l.value):
					self.add('ERROR', l.file, l.line, 'Requires %s, which isn\'t installed (not in Data/ or your mods); the game won\'t load this mod without it.' % l.value)

	# ------------------------------------------------------------ INI

	def check_ini(self, lines):
		prevIndent = {}
		walked = list(mod_index.walk_ini(lines, self.surface))
		for i, (l, owner, valueClass, depth) in enumerate(walked):
			hasChildren = i + 1 < len(walked) and walked[i + 1][0].indent > l.indent
			if l.spaceIndent:
				self.add('ERROR', l.file, l.line, 'Indented with spaces; the game only accepts tabs here and stops reading the mod.')
				continue
			last = prevIndent.get(l.file)
			if last is not None and l.indent > last + 1:
				self.add('ERROR', l.file, l.line, 'Indented two or more levels deeper than the line before; the game stops reading the mod.')
			prevIndent[l.file] = l.indent
			if l.key == 'IncludeFile':
				continue
			# The property itself.
			if owner == 'DataModule':
				known = engine_surface.ini_properties(self.surface, 'DataModule')
				if l.key not in known and not valueClass:
					entry = self.history.find('ini', l.value)
					if entry and entry['new'] in self.iniClasses and entry['mechanical']:
						self.add('ERROR', l.file, l.line, '%s = %s: %s' % (l.key, l.value, describe(entry)), fix=('value', l.file, l.line, l.value, entry['new']))
					else:
						self.add('ERROR', l.file, l.line, '%s = %s: "%s" isn\'t a kind of object this game knows, so it can\'t load this mod.' % (l.key, l.value, l.value))
			elif owner:
				known = engine_surface.ini_properties(self.surface, owner)
				if known is not None and l.key not in known:
					entry = self.history.find('ini', l.key, owner)
					if entry and entry['new'] and entry['mechanical'] and entry['new'] in known:
						self.add('WARNING', l.file, l.line, '%s isn\'t %s setting any more, so the game skips it: %s' % (l.key, a(owner), describe(entry)), fix=('key', l.file, l.line, l.key, entry['new']))
					elif entry:
						self.add('WARNING', l.file, l.line, '%s isn\'t %s setting any more, so the game skips it: %s' % (l.key, a(owner), describe(entry)))
					else:
						self.add('WARNING', l.file, l.line, '%s isn\'t something %s has, so the game skips this line.' % (l.key, a(owner)))
			# An object of a class that's gone.
			if hasChildren and not valueClass and re.fullmatch(r'[A-Z][A-Za-z]+', l.value) and owner and owner != 'DataModule':
				entry = self.history.find('ini', l.value)
				if entry and entry['new'] in self.iniClasses:
					self.add('ERROR', l.file, l.line, '%s = %s: %s' % (l.key, l.value, describe(entry)), fix=('value', l.file, l.line, l.value, entry['new']) if entry['mechanical'] else None)
			# References.
			if l.key == 'CopyOf':
				self.check_preset(l.value, owner, l.file, l.line, 'ini')
			if l.key in FILE_KEYS or PATH_RE.match(l.value):
				if l.value and l.value != 'None' and ('/' in l.value or '\\' in l.value):
					self.check_path(l.value, l.file, l.line)

	def check_preset(self, ref, cls, path, line, where, functionName=None):
		if not ref or ref == 'None':
			return
		module, name = None, ref
		if '/' in ref and ref.split('/', 1)[0].lower().endswith('.rte'):
			module, name = ref.split('/', 1)
		candidates = [(c, self.moduleName) for c in self.own.get(name, [])] + self.presets.get(name, [])
		for other, info in self.other_mods().items():
			candidates += [(c, other) for c, p in info['presets'] if p == name]
		if module and not self.modules.module_of(module):
			# The game searches every module when the one named isn't loaded; it's only a missing dependency if nothing has the preset.
			if not candidates:
				self.need(module, path, line)
				return
		elif module:
			# A preset not in the module named is looked for in the game's own modules.
			candidates = [(c, m) for c, m in candidates if m == module or m in self.officialModules] or candidates
		if candidates:
			fits = [(c, m) for c, m in candidates if not cls or self.inherits(c, cls) or (cls == 'AEJetpack' and c == 'AEmitter')]
			if fits:
				if not any(m == self.moduleName or m in self.officialModules for _, m in fits):
					self.need(fits[0][1], path, line)
				return
			self.add('ERROR', path, line, '"%s" is %s, not %s, so %s fails.' % (name, a(candidates[0][0]), a(cls), 'CopyOf' if where == 'ini' else functionName))
			return
		entry = self.history.find('preset', name)
		if entry:
			fixable = entry['mechanical'] and entry['new'] and entry['new'] in self.presets
			self.add('ERROR', path, line, 'No preset "%s" any more: %s' % (name, describe(entry)), fix=('text', path, line, name, entry['new']) if fixable else None)
			return
		if where == 'ini':
			self.add('ERROR', path, line, 'CopyOf "%s": there\'s no preset by that name in the game or this mod, so the game can\'t load this mod.' % ref)
		else:
			self.add('WARNING', path, line, '%s("%s"): there\'s no preset by that name in the game or this mod, so the script gets nothing back (and stops with an error if it uses that without checking).' % (functionName, ref))

	def check_path(self, value, path, line):
		gamePath = value.replace('\\', '/')
		module = gamePath.split('/', 1)[0]
		if not self.modules.module_of(module):
			if module.lower().endswith('.rte'):
				self.need(module, path, line)
			return
		exact, wrong = self.modules.resolve(gamePath)
		if exact:
			return
		base, ext = os.path.splitext(gamePath)
		alternatives = []
		if ext.lower() in IMAGE:
			alternatives = [base + '000' + ext, base + ('.bmp' if ext.lower() == '.png' else '.png'), base + '000' + ('.bmp' if ext.lower() == '.png' else '.png')]
		elif ext.lower() in AUDIO:
			alternatives = [base + e for e in AUDIO] + [base + '1' + ext]
			if '/Sounds/Fire' in gamePath:
				alternatives.append(gamePath.replace('/Sounds/Fire', '/Sounds/Shot'))
		for alt in alternatives:
			if self.modules.resolve(alt)[0]:
				return
		if wrong:
			right = self.modules.game_path(wrong)
			self.add('ERROR' if ext.lower() in ('.lua', '.ini') else 'WARNING', path, line, '%s only exists as %s; the game matches letter case, so it doesn\'t find it.' % (value, right), fix=('text', path, line, value, right))
			return
		for alt in alternatives:
			wrongAlt = self.modules.resolve(alt)[1]
			if wrongAlt:
				right = self.modules.game_path(wrongAlt)
				self.add('WARNING', path, line, '%s only exists as %s; the game matches letter case, so it doesn\'t find it.' % (value, right))
				return
		what = 'that sound is silent' if ext.lower() in AUDIO else 'it shows as blank' if ext.lower() in IMAGE else 'it isn\'t loaded'
		if module in self.officialModules:
			self.add('WARNING', path, line, '%s doesn\'t exist in the game\'s own %s (moved or removed), so %s.' % (value, module, what))
		else:
			self.add('WARNING', path, line, '%s doesn\'t exist, so %s.' % (value, what))

	# ------------------------------------------------------------ Lua

	def check_lua(self, lines):
		parsed = {}
		for script in mod_index.lua_files(self.modDir):
			try:
				parsed[os.path.normcase(os.path.abspath(script))] = mod_index.LuaFile(script)
			except Exception as error:  # a file the tokenizer can't read is reported, not fatal
				self.add('WARNING', script, 0, 'Couldn\'t read this script (%s).' % error)
		files = list(parsed.values()) if getattr(self, 'allScripts', False) else self.used_scripts(parsed, lines)
		unused = len(parsed) - len(files)
		if unused:
			self.add('NOTE', self.modDir, 0, '%d script(s) aren\'t used by anything the game loads from this mod, so they weren\'t checked.' % unused)
		ownGlobals = set()
		ownMembers = set()
		for f in files:
			ownGlobals.update(f.globalDefs)
			ownMembers.update(f.memberDefs)
		otherGlobals = {}
		otherMembers = set()
		for name, info in self.other_mods().items():
			for g in info['globals']:
				otherGlobals.setdefault(g, name)
			otherMembers |= info['members']
		allMembers = self.engineMembers | self.knownMembers | ownMembers | otherMembers
		for f in files:
			reported = set()
			for name, line, how in f.freeUses:
				if how == 'read' or name in self.knownGlobals or name in ownGlobals or (name, line) in reported:
					continue
				reported.add((name, line))
				if name in otherGlobals:
					self.need(otherGlobals[name], f.path, line)
					continue
				entry = self.history.find('lua', name)
				if entry:
					new = entry['new']
					fixable = entry['mechanical'] and new and (new in self.knownGlobals)
					self.add('ERROR', f.path, line, '%s no longer exists: %s' % (name, describe(entry)), fix=('word', f.path, line, name, new) if fixable else None)
				else:
					self.add('ERROR', f.path, line, '%s is used here but nothing defines it (not the game, this mod, or another installed mod), so the script stops with an error here.' % name)
			for base, sep, name, line, isCall in f.memberUses:
				if base in self.managers or (base in self.luaClasses and base not in ownGlobals):
					cls = self.managers.get(base, base)
					members = engine_surface.lua_members(self.surface, cls)
					if name in members or name in ownMembers:
						continue
					entry = self.history.find('lua', name, cls)
					label = base + sep + name
					if entry:
						fix = self.member_fix(entry, base, sep, name, f.path, line)
						self.add('ERROR', f.path, line, '%s no longer exists: %s' % (label, describe(entry)), fix=fix)
					else:
						self.add('ERROR', f.path, line, '%s doesn\'t exist in this game (%s has no %s), so the script stops with an error here.' % (label, base, name))
					continue
				if base and base in ownGlobals:
					continue
				if name in allMembers:
					continue
				entry = self.history.find('lua', name)
				if entry and entry['new'] != name:
					new = entry['new'] or ''
					fixable = entry['mechanical'] and new and ':' not in new and '.' not in new and new in self.engineMembers
					self.add('ERROR' if isCall else 'WARNING', f.path, line, '%s%s no longer exists: %s' % (sep, name, describe(entry)), fix=('member', f.path, line, name, new) if fixable else None)
				elif isCall and sep == ':':
					self.add('WARNING', f.path, line, ':%s() isn\'t a function of any game object and no script defines it, so this call likely fails.' % name)
			for function, args, line in f.stringCalls:
				m = re.match(r'(Create)(\w+)$', function)
				if m and function in self.engineFunctions:
					cls = m.group(2)
					if len(args) >= 2:
						self.check_preset(args[1] + '/' + args[0] if args[1] != self.moduleName else args[0], cls, f.path, line, 'lua', function)
					else:
						self.check_preset(args[0], cls, f.path, line, 'lua', function)
			for value, line in f.strings:
				if PATH_RE.match(value.strip()):
					self.check_path(value.strip(), f.path, line)
			self.check_patterns(f.path)

	def used_scripts(self, parsed, lines):
		"""The scripts the game can run: named in a loaded INI line, or named (by path, dofile/loadfile or require) by a script that is."""
		byPosix = {key: f for key, f in parsed.items()}
		used, queue = {}, []

		def refer(value):
			value = value.strip()
			if value.lower().endswith('.lua'):
				exact, wrong = self.modules.resolve(value)
				key = os.path.normcase(os.path.abspath(exact or wrong)) if (exact or wrong) else None
				if key in byPosix and key not in used:
					used[key] = byPosix[key]
					queue.append(byPosix[key])
			elif re.fullmatch(r'[\w.\-/]+', value) and not value.lower().endswith(('.png', '.bmp', '.wav', '.ogg', '.flac', '.ini')):
				# require("AI/NativeHumanAI") and require("Module.Sub") find files through the search path.
				tail = '/' + value.replace('.', '/').strip('/') + '.lua'
				for key, f in byPosix.items():
					if key not in used and f.path.replace('\\', '/').endswith(tail):
						used[key] = f
						queue.append(f)

		for l in lines:
			if l.value and '.lua' in l.value.lower():
				refer(l.value)
		while queue:
			f = queue.pop()
			for value, _ in f.strings:
				refer(value)
		return list(used.values())

	def member_fix(self, entry, base, sep, name, path, line):
		new = entry['new']
		if not entry['mechanical'] or not new:
			return None
		m = re.match(r'^(\w+)([:.])(\w+)$', new)
		if m:
			newBase, newSep, newName = m.groups()
			if newBase in self.managers and newName in engine_surface.lua_members(self.surface, self.managers[newBase]):
				return ('text', path, line, base + sep + name, newBase + sep + newName)
			if newBase in self.luaClasses and newName in engine_surface.lua_members(self.surface, newBase):
				return ('text', path, line, base + sep + name, newBase + '.' + newName)
			return None
		if new in engine_surface.lua_members(self.surface, self.managers.get(base, base)):
			return ('text', path, line, base + sep + name, base + sep + new)
		if new in self.engineFunctions:  # moved from a manager to a global function: FrameMan:GetPPM() -> GetPPM()
			return ('text', path, line, base + sep + name, new)
		return None

	def check_patterns(self, path):
		text, _ = mod_index.read_text(path)
		for rule in PATTERN_RULES:
			for m in rule['find'].finditer(text):
				line = text.count('\n', 0, m.start()) + 1
				self.add('WARNING', path, line, rule['why'], fix=('pattern', path, line, rule['name'], None))
		for number, row in enumerate(text.split('\n'), 1):
			m = re.search(r'([\w.:]+(?:\([^()]*\))?(?:\[[^\]]*\])?)\.Magazine[.:]', row)
			if not m or 'Magazine ~= nil' in row or 'Magazine and' in row or re.search(r'\bif\s+' + re.escape(m.group(1)) + r'\.Magazine\s+then', row):
				continue
			above = '\n'.join(text.split('\n')[max(0, number - 4):number - 1])
			if m.group(1) + '.Magazine' in above and re.search(r'(if|and)\s+' + re.escape(m.group(1)) + r'\.Magazine\b', above):
				continue
			fix = None
			if re.match(r'\s*(if|elseif)\s+' + re.escape(m.group(0)), row):
				fix = ('text', path, number, m.group(0), m.group(1) + '.Magazine and ' + m.group(0))
			self.add('WARNING', path, number, '%s.Magazine is used without checking it\'s there; it is nothing while the gun reloads or after it\'s dropped empty, and the script stops with an error.' % m.group(1), fix=fix)

	def check_dependencies(self):
		others = self.other_mods()
		for module, (path, line) in sorted(self.needs.items()):
			if module in others or self.modules.module_of(module):
				self.add('NOTE', path, line, 'Uses %s (installed), so it needs that mod to work.' % module)
			else:
				self.add('ERROR', path, line, 'Needs %s, which isn\'t installed (this is the first place that uses it).' % module)


# Fixes that are a pattern over a whole script, not a renamed name. (Moved here from Fix-Mods.py.)
PATTERN_RULES = [
	# Scripts that go through every object ID and use what they get straight away:
	#     thing = MovableMan:GetMOFromID(i)
	#     if thing.PresetName == ... then
	# An ID can belong to something that is already gone, and the lookup gives nil for those. The check is made to ask first.
	{
		'name': 'GetMOFromID nil check',
		'find': re.compile(r'(\b(\w+) = (?:To\w+\()?MovableMan:GetMOFromID\([^()\n]*(?:\([^()\n]*\))?[^()\n]*\)\)?[ \t]*;?[ \t]*(?:--[^\n]*)?\r?\n[ \t]*if )(?!\2 and )(?!\2 then)(?!\2 ~= nil)(?!not \2)(\2\.)'),
		'replace': r'\1\2 and \3',
		'why': 'The object looked up by ID is used without checking it\'s there; an ID can belong to something already gone, and the script stops with an error.',
	},
]


# ---------------------------------------------------------------- fixing

def apply_fixes(findings, ask):
	byFile = {}
	for f in findings:
		if f.fix:
			byFile.setdefault(f.fix[1], []).append(f)
	if not byFile:
		return 0
	print('\nFixes to make:')
	for path, items in sorted(byFile.items()):
		for f in items:
			kind, _, line, old, new = f.fix
			print('  %s:%d  %s' % (path, line, ('%s -> %s' % (old, new)) if kind != 'pattern' else 'add the nil check (%s)' % old))
	if ask:
		try:
			answer = input('Make these %d change(s)? A .bak copy of each file is kept. [y/N] ' % sum(len(v) for v in byFile.values()))
		except EOFError:
			answer = ''
		if answer.strip().lower() not in ('y', 'yes'):
			print('Nothing changed.')
			return 0
	changed = 0
	for path, items in sorted(byFile.items()):
		text, encoding = mod_index.read_text(path)
		original = text
		newline = '\r\n' if '\r\n' in text else '\n'
		rows = text.split(newline)
		for f in items:
			kind, _, line, old, new = f.fix
			if kind == 'pattern':
				continue
			i = line - 1
			if not 0 <= i < len(rows):
				continue
			row = rows[i]
			if kind == 'key':
				rows[i] = re.sub(r'^(\s*)' + re.escape(old) + r'(\s*=)', lambda m: m.group(1) + new + m.group(2), row, count=1)
			elif kind == 'value':
				rows[i] = re.sub(r'(=\s*)' + re.escape(old) + r'\b', lambda m: m.group(1) + new, row, count=1)
			elif kind == 'member':
				rows[i] = re.sub(r'(?<=[.:])' + re.escape(old) + r'\b', new, row, count=1)
			elif kind == 'word':
				rows[i] = re.sub(r'(?<![\w.:])' + re.escape(old) + r'\b', new, row, count=1)
			else:  # text, line
				rows[i] = row.replace(old, new, 1)
		text = newline.join(rows)
		for f in items:
			if f.fix[0] == 'pattern':
				rule = next(r for r in PATTERN_RULES if r['name'] == f.fix[3])
				text = rule['find'].sub(rule['replace'], text)
		if text != original:
			if not os.path.exists(path + '.bak'):
				shutil.copy2(path, path + '.bak')
			with open(path, 'wb') as out:
				out.write(text.encode(encoding))
			changed += 1
	print('%d file(s) changed.' % changed)
	return changed


# ---------------------------------------------------------------- output

def report(name, modDir, findings, quiet, write):
	counts = {s: sum(1 for f in findings if f.severity == s) for s in ORDER}
	fixable = sum(1 for f in findings if f.fix)
	summary = '%-40s %3d error(s), %3d warning(s), %3d note(s)%s' % (name, counts['ERROR'], counts['WARNING'], counts['NOTE'], ', %d fixable by --fix' % fixable if fixable else '')
	lines = [summary, '']
	for f in findings:
		where = os.path.relpath(f.path, os.path.dirname(modDir)) if f.path.startswith(os.path.dirname(modDir)) else f.path
		lines.append('%-7s %s%s  %s%s' % (f.severity, where.replace('\\', '/'), ':%d' % f.line if f.line else '', f.message, '  [fixable]' if f.fix else ''))
	text = '\n'.join(lines) + '\n'
	print(summary if quiet else text)
	if write:
		folder = os.path.join(ROOT, 'ModChecks')
		os.makedirs(folder, exist_ok=True)
		with open(os.path.join(folder, name + '.txt'), 'w', encoding='utf-8') as out:
			out.write(text)
	return counts['ERROR']


def unpack(zipPath):
	"""Unpacks a zip into a temporary folder (refusing entries that would land outside it) and returns the .rte folders found."""
	target = tempfile.mkdtemp(prefix='modcheck-')
	with zipfile.ZipFile(zipPath) as z:
		for member in z.infolist():
			dest = os.path.realpath(os.path.join(target, member.filename))
			if not dest.startswith(os.path.realpath(target) + os.sep):
				raise ValueError('the zip has a file that would land outside its folder: ' + member.filename)
		z.extractall(target)
	found = []
	for folder, dirs, _ in os.walk(target):
		for d in list(dirs):
			if d.lower().endswith('.rte'):
				found.append(os.path.join(folder, d))
				dirs.remove(d)
	return target, found


def main():
	parser = argparse.ArgumentParser(description='Checks mods against this version of the game, and fixes what can be fixed by rule.')
	parser.add_argument('mods', nargs='*', help='.rte folders or .zip files')
	parser.add_argument('--all', action='store_true', help='every mod in the mods folders')
	parser.add_argument('--mods', dest='modsDirs', action='append', help='a folder of other installed mods (repeatable)')
	parser.add_argument('--fix', action='store_true', help='make the fixable changes')
	parser.add_argument('--yes', action='store_true', help="don't ask before fixing")
	parser.add_argument('--quiet', action='store_true', help='one summary line per mod')
	parser.add_argument('--no-report', action='store_true', help="don't write ModChecks/<mod>.txt")
	parser.add_argument('--all-scripts', action='store_true', help='check every script, not just the ones something loaded uses (for a mod other mods use scripts from)')
	args = parser.parse_args()
	modsDirs = args.modsDirs or ([os.environ['CCCP_MODS_DIR']] if os.environ.get('CCCP_MODS_DIR') else [os.path.join(ROOT, 'Mods'), os.path.join(ROOT, 'ModsParked')])
	targets = []
	for value in args.mods:
		if value.lower().endswith('.zip'):
			if args.fix:
				print('%s: unpack the zip into your Mods folder to fix it; checking only.' % value)
			folder, found = unpack(value)
			atexit.register(shutil.rmtree, folder, True)
			if not found:
				print('%s: no .rte folder inside.' % value)
			targets += [(f, False) for f in found]
		else:
			targets.append((os.path.abspath(value.rstrip('/\\')), True))
	if args.all:
		for folder in modsDirs:
			if os.path.isdir(folder):
				targets += [(os.path.join(folder, d), True) for d in sorted(os.listdir(folder)) if d.lower().endswith('.rte') and os.path.isdir(os.path.join(folder, d))]
	if not targets:
		if args.all:
			print('No mods found in %s.' % ' or '.join(modsDirs))
			return 0
		parser.print_help()
		return 2
	checker = Checker(engine_surface.build(), modsDirs)
	checker.allScripts = args.all_scripts
	errors = 0
	quiet = args.quiet or (args.all and not args.mods)
	allFindings = []
	for modDir, fixable in targets:
		findings = checker.check(modDir)
		errors += report(os.path.basename(modDir), modDir, findings, quiet, not args.no_report)
		if fixable:
			allFindings += findings
	if args.fix:
		apply_fixes(allFindings, ask=not args.yes)
	if quiet and not args.no_report:
		print('Details for each mod are in ModChecks/.')
	return 1 if errors else 0


if __name__ == '__main__':
	sys.exit(main())
