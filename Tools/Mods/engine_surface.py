"""What this version of the game offers mods, read straight from the source tree, so it always matches the branch it's run on.

	import engine_surface
	surface = engine_surface.build()          # or build('/path/to/checkout')
	surface['lua']['classes']['AHuman']       # {'parent': 'Actor', 'members': [...], 'cpp': 'AHuman'}
	surface['ini']['classes']['AHuman']       # {'parent': 'Actor', 'properties': [...]}

Everything returned is plain lists, dicts and strings, so json.dump(surface) works (python engine_surface.py writes it to stdout).
Check-Mod.py is built on this; the Mod SDK plan uses the same model for docs and Lua type stubs.

What's in it:
	version        the game version (Source/System/GameVersion.h)
	lua.classes    every class bound to Lua, by its Lua name: parent class, members (functions, properties, enum values), C++ type
	lua.globals    global names: manager instances (AudioMan, ...) with their class, free functions (CreateAHuman, ToActor, Lerp, ...),
	               and the helpers the engine defines in Lua itself (print, SelectRand, ...)
	ini.classes    every class an INI file can name, by its INI name: parent class and the properties it reads (MatchProperty),
	               not counting the parent's
	data.modules   the modules in Data/: name, and the presets each defines (class and name)
	data.lua       global names and member names defined by the Lua scripts in Data/ (what a mod can rely on being there)
"""
import json, os, re, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))

LUA_BUILTINS = sorted('''
	_G _VERSION assert collectgarbage dofile error getfenv getmetatable ipairs load loadfile loadstring module next pairs pcall print rawequal
	rawget rawset require select setfenv setmetatable tonumber tostring type unpack xpcall bit coroutine debug io jit math os package string
	table ffi arg self gcinfo newproxy tracy
'''.split())

# Methods of Lua's own values (strings, files), which scripts call with ':' like engine functions.
LUA_VALUE_METHODS = sorted('''
	byte char dump find format gmatch gsub len lower match rep reverse sub upper close flush lines read seek setvbuf write
'''.split())


def _read(path):
	with open(path, 'rb') as f:
		return f.read().decode('utf-8', 'replace')


def _strip_cpp_comments(text):
	# Keep string literals intact (they hold the names); drop // and /* */ comments.
	return re.sub(r'"(?:\\.|[^"\\\n])*"|//[^\n]*|/\*.*?\*/', lambda m: m.group(0) if m.group(0).startswith('"') else (' ' if m.group(0).startswith('//') else '\n' * m.group(0).count('\n')), text, flags=re.S)


def _lua_bindings(root):
	"""Lua classes and globals, from Source/Lua/LuaBindings*.cpp and Source/Managers/LuaMan.cpp."""
	files = [os.path.join(root, 'Source', 'Lua', name) for name in sorted(os.listdir(os.path.join(root, 'Source', 'Lua'))) if name.startswith('LuaBindings') and name.endswith('.cpp')]
	files.append(os.path.join(root, 'Source', 'Managers', 'LuaMan.cpp'))
	classStart = re.compile(r'(?:Concrete|Abstract)TypeLuaClassDefinition\(\s*([\w:]+)\s*,\s*([\w:]+)\s*\)|class_<\s*([\w:]+)\s*(?:,\s*([\w:]+)\s*)?>\s*\(\s*"(\w+)"\s*\)')
	member = re.compile(r'\.(?:def|property|def_readwrite|def_readonly)\(\s*"(\w+)"|luabind::value\(\s*"(\w+)"|\.enum_\(\s*"(\w+)"')
	freeFunction = re.compile(r'luabind::def\(\s*"(\w+)"')
	classes = {}
	cppToLua = {}
	for path in files:
		text = _strip_cpp_comments(_read(path))
		starts = list(classStart.finditer(text))
		for i, m in enumerate(starts):
			if m.group(1):
				cpp, parentCpp, luaName, concrete = m.group(1), m.group(2), m.group(1), m.group(0).startswith('Concrete')
			else:
				cpp, parentCpp, luaName, concrete = m.group(3), m.group(4), m.group(5), False
			end = starts[i + 1].start() if i + 1 < len(starts) else len(text)
			# A class's chain of members ends at the first ';' or at a free function (both only happen after the chain in these files).
			body = text[m.end():end]
			stop = len(body)
			for marker in (re.search(r';', body), freeFunction.search(body)):
				if marker:
					stop = min(stop, marker.start())
			body = body[:stop]
			members = set()
			for mm in member.finditer(body):
				members.add(mm.group(1) or mm.group(2) or mm.group(3))
			if m.group(1):
				members.update(('ClassName',) + (('Clone',) if concrete else ()))
			entry = classes.setdefault(luaName, {'cpp': cpp, 'parentCpp': parentCpp, 'members': set()})
			entry['members'].update(members)
			cppToLua[cpp] = luaName
	for entry in classes.values():
		entry['parent'] = cppToLua.get(entry.pop('parentCpp')) if entry.get('parentCpp') else None
		entry.pop('parentCpp', None)
		entry['members'] = sorted(entry['members'])

	luaMan = _strip_cpp_comments(_read(os.path.join(root, 'Source', 'Managers', 'LuaMan.cpp')))
	functions = set(freeFunction.findall(luaMan))
	# Create/Random/To/Is functions made for each registered class (RegisterLuaBindingsOf*Type).
	for kind, cpp in re.findall(r'RegisterLuaBindingsOf(Concrete|Abstract)Type\(\s*\w+\s*,\s*(\w+)\s*\)', luaMan):
		functions.update(('To' + cpp, 'Is' + cpp))
		if kind == 'Concrete':
			functions.update(('Create' + cpp, 'Random' + cpp))
	managers = {}
	for name, target in re.findall(r'luabind::globals\(m_State\)\["(\w+)"\]\s*=\s*(&?\w+)', luaMan):
		cpp = 'LuaStateWrapper' if target == 'this' else target.lstrip('&').replace('g_', '', 1)
		managers[name] = cppToLua.get(cpp, cpp)
	# Helpers the engine defines in Lua when it starts (cls, print, SelectRand, ...).
	scripted = set()
	for chunk in re.findall(r'luaL_dostring\(m_State,(.*?)\);\s*\n', luaMan, flags=re.S):
		code = ''.join(re.findall(r'"((?:\\.|[^"\\])*)"', chunk))
		scripted.update(re.findall(r'(?:^|\\n|;)\s*(?:do\s+local\s+\w+\s*=\s*\w+;\s*)?(\w+)\s*=', code))
	scripted -= {'local', 'filePath', 'package', 'ExpressionResult'}
	return {'classes': dict(sorted(classes.items())), 'globals': {'managers': dict(sorted(managers.items())), 'functions': sorted(functions), 'scripted': sorted(scripted)}}


def _ini_classes(root):
	"""Every class with an INI reader: its parent (where unmatched properties go) and the properties it reads itself."""
	classes = {}
	classNames = {}
	bases = {}
	for folder, _, names in os.walk(os.path.join(root, 'Source')):
		for name in names:
			if not name.endswith(('.cpp', '.h')):
				continue
			text = _strip_cpp_comments(_read(os.path.join(folder, name)))
			for cls, value in re.findall(r'(?:const\s+)?std::string\s+(\w+)::c_ClassName\s*=\s*"(\w+)"', text):
				classNames[cls] = value
			for cls, base in re.findall(r'\bclass\s+(\w+)\s*(?:final\s*)?:\s*public\s+(\w+)', text):
				bases.setdefault(cls, base)
			for m in re.finditer(r'int\s+([\w:]+)(?:<[^<>]*>)?::ReadProperty\s*\(\s*const\s+std::string_view\s*&\s*\w+\s*,\s*Reader\s*&\s*\w+\s*\)\s*\{', text):
				cls = m.group(1).split('::')[-1]
				# The function body: up to the matching closing brace.
				depth, i = 1, m.end()
				while depth and i < len(text):
					depth += {'{': 1, '}': -1}.get(text[i], 0)
					i += 1
				body = text[m.end():i]
				start = re.search(r'StartPropertyList\((.*?)\);\s*\n', body, flags=re.S)
				parent = None
				if start:
					p = re.search(r"(?:return\s+)?([\w:]+)::ReadProperty", start.group(1))
					parent = p.group(1).split('::')[-1] if p else None
				props = set(re.findall(r'Match(?:Property|Forwards)\(\s*"(\w+)"', body))
				entry = classes.setdefault(cls, {'parent': parent, 'properties': set(), 'macro': bool(start)})
				entry['properties'].update(props)
				if parent:
					entry['parent'] = parent
	# Classes that read nothing of their own (StaticSceneLayer, a template's named forms) read what their base class reads.
	for cls, base in bases.items():
		if cls not in classes and base in bases.keys() | classes.keys():
			classes[cls] = {'parent': base, 'properties': set(), 'macro': True}
	def reaches(cls, seen=()):
		entry = classes.get(cls)
		return bool(entry) and cls not in seen and (bool(entry['properties']) or reaches(entry['parent'], seen + (cls,)))
	classes = {cls: entry for cls, entry in classes.items() if reaches(cls)}
	out = {}
	for cls, entry in classes.items():
		if not entry['macro'] and not entry['properties']:
			continue
		out[classNames.get(cls, cls)] = {'cpp': cls, 'parent': classNames.get(entry['parent'], entry['parent']) if entry['parent'] else None, 'properties': sorted(entry['properties'])}
	# DataModule's unmatched properties are presets (AddActor = AHuman and so on), not its parent's.
	if 'DataModule' in out:
		out['DataModule']['parent'] = None
		out['DataModule']['presetsAtTop'] = True
	return dict(sorted(out.items()))


def _version(root):
	m = re.search(r'c_VersionString\s*=\s*"([^"]+)"', _read(os.path.join(root, 'Source', 'System', 'GameVersion.h')))
	return m.group(1) if m else '0.0.0'


def build(root=ROOT, withData=True):
	"""The whole model. withData=False skips reading Data/ (the slow part)."""
	surface = {'version': _version(root), 'lua': _lua_bindings(root), 'ini': {'classes': _ini_classes(root)}}
	surface['lua']['builtins'] = LUA_BUILTINS
	surface['lua']['valueMethods'] = LUA_VALUE_METHODS
	if withData:
		import mod_index  # beside this file
		surface['data'] = mod_index.index_folder(os.path.join(root, 'Data'), surface)
	return surface


def lua_members(surface, luaClass):
	"""Members of a Lua class including everything inherited."""
	members, seen = set(), set()
	classes = surface['lua']['classes']
	while luaClass and luaClass in classes and luaClass not in seen:
		seen.add(luaClass)
		members.update(classes[luaClass]['members'])
		luaClass = classes[luaClass]['parent']
	return members


def ini_properties(surface, iniClass):
	"""Properties an INI block of this class may set, including every parent's, or None if the class isn't known."""
	classes = surface['ini']['classes']
	if iniClass not in classes:
		return None
	props, seen = set(), set()
	while iniClass and iniClass in classes and iniClass not in seen:
		seen.add(iniClass)
		props.update(classes[iniClass]['properties'])
		iniClass = classes[iniClass]['parent']
	return props


if __name__ == '__main__':
	sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
	json.dump(build(sys.argv[1] if len(sys.argv) > 1 else ROOT), sys.stdout, indent='\t')
