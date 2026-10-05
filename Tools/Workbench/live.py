"""The Workbench's dealings with games it starts: launch profiles, the live link to a running game, and graphics presets.

Launch profile: a saved answer to "start the game how?" - which program, where it starts, time and weather, a graphics preset, which mods.
  A profile that changes anything is started with its own copy of the settings (your Settings.ini plus the profile's changes), so nothing it does is written back to yours.
Live link: games started here listen on a port of this computer (CCCP_CONTROL_PORT); see Source/Managers/ControlLink.h for the commands.
Graphics preset: every setting of the game's "Lighting and Post-Processing" block except the world's own (time of day, weather), saved under a name."""
import json, os, re, socket, subprocess, threading, time

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
STATE_DIR = os.path.join(REPO, 'Userdata', 'Workbench')
PRESET_DIR = os.path.join(STATE_DIR, 'Presets')
PROFILE_FILE = os.path.join(STATE_DIR, 'profiles.json')
SETTINGS = os.path.join(REPO, 'Userdata', 'Settings.ini')
EXES = {'play': 'Cortex Command.exe', 'debug': 'Cortex Command.debug.release.exe'}
GRAPHICS_HEADING = 'Lighting and Post-Processing Settings'
# In the graphics block, but about the world or the rules, not the look. A preset leaves these alone.
NOT_LOOK = {'LightingSettingsVersion', 'GraphicsQuality', 'WeatherType', 'WeatherIntensity', 'Wind', 'TimeOfDay', 'DayLengthMinutes', 'NightAffectsAI'}
NO_WINDOW = getattr(subprocess, 'CREATE_NO_WINDOW', 0)

lock = threading.RLock()
instances = []       # Games started from here that are still running (or just ended).
instanceCounter = [0]
INSTANCE_FILE = os.path.join(STATE_DIR, 'instances.json')


class AdoptedGame:
	"""A game started by an earlier run of the Workbench and still going. Stands in for the process object the Workbench would otherwise hold."""

	def __init__(self, pid):
		self.pid = pid
		self.returncode = None

	def poll(self):
		if self.returncode is None and not processAlive(self.pid):
			self.returncode = 0
		return self.returncode

	def kill(self):
		subprocess.run(['taskkill', '/PID', str(self.pid), '/F'], capture_output=True, creationflags=NO_WINDOW)


def processAlive(pid):
	try:
		import ctypes
		handle = ctypes.windll.kernel32.OpenProcess(0x1000, False, int(pid))  # PROCESS_QUERY_LIMITED_INFORMATION
		if not handle:
			return False
		code = ctypes.c_ulong()
		ctypes.windll.kernel32.GetExitCodeProcess(handle, ctypes.byref(code))
		ctypes.windll.kernel32.CloseHandle(handle)
		return code.value == 259  # STILL_ACTIVE
	except (OSError, AttributeError):
		return False


def rememberInstances():
	"""Writes down the running games, so a restarted Workbench can pick them up again."""
	with lock:
		running = [{key: instance[key] for key in ('id', 'name', 'profile', 'exe', 'port', 'started', 'stage', 'preset')} | {'pid': instance['process'].pid} for instance in instances if instance['process'].poll() is None]
	try:
		os.makedirs(STATE_DIR, exist_ok=True)
		with open(INSTANCE_FILE, 'w', encoding='utf-8') as out:
			json.dump(running, out)
	except OSError:
		pass


def adoptInstances():
	try:
		remembered = json.load(open(INSTANCE_FILE, encoding='utf-8'))
	except (OSError, ValueError):
		return
	for item in remembered:
		pid = item.pop('pid', 0)
		if not processAlive(pid):
			continue
		try:
			if not ask(item['port'], 'ping', 1.0).startswith('ok'):
				continue
		except OSError:
			pass  # Alive but not answering: probably still loading. Keep it.
		item.update({'process': AdoptedGame(pid), 'state': None, 'answering': False})
		instances.append(item)
		instanceCounter[0] = max(instanceCounter[0], item['id'])


def repoPath(*parts):
	return os.path.join(REPO, *parts)


# ---------------------------------------------------------------- Settings files

def readText(path):
	with open(path, encoding='utf-8', errors='replace') as source:
		return source.read()


def graphicsFromSettings(text):
	"""The look settings in a settings file, in the file's order: {key: value}."""
	values = {}
	inBlock = False
	for line in text.splitlines():
		if line.startswith('//'):
			if GRAPHICS_HEADING in line:
				inBlock = True
			elif inBlock and line.startswith('////') and values:
				break
			continue
		match = re.match(r'^\s*(\w+)\s*=\s*(.*?)\s*$', line)
		if inBlock and match and match.group(1) not in NOT_LOOK:
			values[match.group(1)] = match.group(2)
	return values


def settingsWith(text, overrides):
	"""A settings file's text with some keys changed; keys it doesn't have are added at the end. EnableGlobalScript lines are replaced when the overrides give any."""
	overrides = dict(overrides)
	scripts = overrides.pop('EnableGlobalScript', None)
	done = set()
	out = []
	for line in text.splitlines():
		match = re.match(r'^(\s*)(\w+)\s*=', line)
		if match:
			key = match.group(2)
			if scripts is not None and key == 'EnableGlobalScript':
				continue
			if key in overrides:
				out.append('%s%s = %s' % (match.group(1), key, overrides[key]))
				done.add(key)
				continue
		out.append(line)
	for key, value in overrides.items():
		if key not in done:
			out.append('\t%s = %s' % (key, value))
	for script in scripts or []:
		out.append('\tEnableGlobalScript = %s' % script)
	return '\n'.join(out) + '\n'


# ---------------------------------------------------------------- What the settings are (names, ranges, groups), read from the game's own source

def graphicsMeta():
	"""For each look setting: the label, group and range the in-game Graphics Lab gives it. Read from the source, so it never falls out of step."""
	meta = {}
	try:
		settingsSource = readText(repoPath('Source', 'Managers', 'SettingsMan.cpp'))
		labSource = readText(repoPath('Source', 'Managers', 'DebugMan.cpp'))
	except OSError:
		return meta
	memberOf = {}
	for match in re.finditer(r'MatchProperty\("(\w+)",\s*\{[^}]*?GetLightingSettings\(\)\.(\w+)', settingsSource, re.S):
		memberOf.setdefault(match.group(1), match.group(2))
	lab = labSource[labSource.find('ImGui::Begin("Graphics Lab"'):]
	lab = lab[:lab.find('ImGui::End()')] if 'ImGui::End()' in lab else lab
	byMember = {}
	group = 'Other'
	for line in lab.splitlines():
		heading = re.search(r'SeparatorText\("([^"]+)"\)', line)
		if heading:
			group = heading.group(1)
		control = re.search(r'ImGui::(SliderFloat|SliderInt|Checkbox|ColorEdit3)\("([^"#]+)[^"]*",\s*&settings\.(\w+)(?:\.x)?(?:,\s*(-?[\d.]+)F?,\s*(-?[\d.]+)F?)?', line)
		if control and control.group(3) not in byMember:
			kind = {'SliderFloat': 'number', 'SliderInt': 'whole', 'Checkbox': 'switch', 'ColorEdit3': 'colour'}[control.group(1)]
			entry = {'label': control.group(2), 'group': group, 'kind': kind}
			if control.group(4) is not None:
				entry['min'] = float(control.group(4))
				entry['max'] = float(control.group(5))
			byMember[control.group(3)] = entry
	for key, member in memberOf.items():
		if key in NOT_LOOK:
			continue
		meta[key] = byMember.get(member, {'label': key, 'group': 'Other', 'kind': 'text'})
	return meta


# ---------------------------------------------------------------- Presets

def presetPath(name):
	if not re.fullmatch(r"[\w .,()'+-]{1,60}", name or ''):
		raise ValueError('A preset name can have letters, numbers, spaces and . , ( ) \' + - and be up to 60 long.')
	return os.path.join(PRESET_DIR, name + '.ini')


def readPreset(name):
	values = {}
	for line in readText(presetPath(name)).splitlines():
		match = re.match(r'^\s*(\w+)\s*=\s*(.*?)\s*$', line)
		if match and not line.lstrip().startswith('//'):
			values[match.group(1)] = match.group(2)
	return values


def writePreset(name, values, note=''):
	os.makedirs(PRESET_DIR, exist_ok=True)
	lines = ['// Cortex Commander graphics preset: %s' % name]
	if note:
		lines.append('// ' + note)
	lines += ['%s = %s' % (key, value) for key, value in values.items() if key not in NOT_LOOK]
	with open(presetPath(name), 'w', encoding='utf-8', newline='\n') as out:
		out.write('\n'.join(lines) + '\n')


def listPresets():
	try:
		names = sorted((name[:-4] for name in os.listdir(PRESET_DIR) if name.endswith('.ini')), key=str.lower)
	except OSError:
		names = []
	presets = []
	for name in names:
		try:
			first = readText(presetPath(name)).splitlines()[:2]
			note = first[1][3:] if len(first) > 1 and first[1].startswith('// ') else ''
			presets.append({'name': name, 'note': note, 'saved': int(os.path.getmtime(presetPath(name)))})
		except (OSError, ValueError):
			pass
	return presets


def scaled(value, factor):
	"""A number, or three numbers of a colour, multiplied."""
	return ' '.join('%g' % (float(part) * factor) for part in value.split())


def seedPresets():
	"""On first use: a few presets to start from, each your current settings with a handful of changes."""
	if os.path.isdir(PRESET_DIR) and os.listdir(PRESET_DIR):
		return
	try:
		current = graphicsFromSettings(readText(SETTINGS))
	except OSError:
		return
	if not current:
		return

	def variant(changes, scales=None):
		values = dict(current)
		values.update({key: value for key, value in changes.items() if key in values})
		for key, factor in (scales or {}).items():
			if key in values:
				values[key] = scaled(values[key], factor)
		return values

	neutral = {'PostSaturation': '1.05', 'GradeContrast': '1', 'GradeTemperature': '0', 'GradeTint': '0', 'PostVignette': '0.15', 'FilmGrain': '0', 'BloomIntensity': '0.5', 'GradeShadowTint': '1 1 1', 'GradeHighlightTint': '1 1 1'}
	writePreset('My settings', current, 'Your settings as they were when the Workbench first ran.')
	writePreset('Soft glow', variant({}, {'BloomIntensity': 0.4, 'LightingGlowIntensity': 0.6, 'LightingEmissiveIntensity': 0.75}), 'Blooms and glows turned well down.')
	writePreset('Lamps only', variant({'LightingAmbient': '0.11 0.11 0.125', 'LightingForegroundAmbient': '0.09 0.09 0.1'}), 'Dark interiors, lit by their lamps.')
	writePreset('Classic', variant({'LightingEnabled': '0', 'BloomEnabled': '0', 'DistortionEnabled': '0', 'ScorchMarks': '0', 'Embers': '0', 'EffectsParticles': '0', 'Stains': '0', 'LivingWorld': '0', 'SmokeScattering': '0'}), 'The original look: lighting, bloom and extra effects off.')
	writePreset('Look - Natural', variant(neutral), 'Neutral colours.')
	writePreset('Look - Gritty', variant(dict(neutral, PostSaturation='0.78', GradeContrast='1.16', GradeTemperature='-0.08', PostVignette='0.32', FilmGrain='0.22', GradeShadowTint='0.9 0.97 1.08', GradeHighlightTint='1.05 1 0.94')), 'Drained and hard, with cold shadows and a little grain.')
	writePreset('Look - Vivid', variant(dict(neutral, PostSaturation='1.32', GradeContrast='1.07', GradeTemperature='0.06', PostVignette='0.1', BloomIntensity='0.75')), 'Rich colour and glowing lights.')
	writePreset('Look - Noir', variant(dict(neutral, PostSaturation='0', GradeContrast='1.28', PostVignette='0.42', FilmGrain='0.3')), 'Black and white, deep contrast, heavy vignette and grain.')


# ---------------------------------------------------------------- The live link

def ask(port, line, timeout=3.0):
	"""Sends one command to a game and returns its one-line answer ('ok ...' or 'err ...'). Raises OSError if the game isn't answering."""
	with socket.create_connection(('127.0.0.1', port), timeout=timeout) as link:
		link.sendall((line.replace('\n', '\\n') + '\n').encode('utf-8'))
		data = b''
		while not data.endswith(b'\n'):
			chunk = link.recv(65536)
			if not chunk:
				break
			data += chunk
	return data.decode('utf-8', errors='replace').strip()


def findInstance(instanceId):
	with lock:
		for instance in instances:
			if instance['id'] == instanceId:
				return instance
	return None


def liveInstances():
	with lock:
		return [instance for instance in instances if instance['process'].poll() is None]


def command(instanceId, line, timeout=3.0):
	instance = findInstance(instanceId)
	if not instance or instance['process'].poll() is not None:
		return 'err that game is not running'
	try:
		return ask(instance['port'], line, timeout)
	except OSError:
		return 'err the game is not answering (still loading?)'


def applyValues(instanceId, values):
	lines = ['%s = %s' % (key, value) for key, value in values.items() if key not in NOT_LOOK]
	return command(instanceId, 'set ' + '\n'.join(lines)) if lines else 'ok 0'


def currentGraphics(instanceId):
	"""The look settings a running game has right now."""
	instance = findInstance(instanceId)
	if not instance:
		raise ValueError('That game is not running.')
	relative = 'Instances/%s/Dump.ini' % instance['name']
	os.makedirs(repoPath('Instances', instance['name']), exist_ok=True)
	reply = command(instanceId, 'dump ' + relative, 6.0)
	if not reply.startswith('ok'):
		raise ValueError(reply[4:] or 'The game did not answer.')
	return graphicsFromSettings(readText(repoPath(*relative.split('/'))))


def watcher():
	"""Keeps each running game's state fresh, so the page never waits on a game."""
	while True:
		time.sleep(1.0)
		for instance in liveInstances():
			try:
				reply = ask(instance['port'], 'state', 1.5)
				if reply.startswith('ok '):
					instance['state'] = json.loads(reply[3:])
					instance['answering'] = True
			except (OSError, ValueError):
				instance['answering'] = False
		with lock:
			for instance in instances:
				if instance['process'].poll() is not None and not instance.get('ended'):
					instance['ended'] = time.time()
					cleanUp(instance)
					rememberInstances()
			# Forget games that ended more than five minutes ago.
			instances[:] = [instance for instance in instances if not instance.get('ended') or time.time() - instance['ended'] < 300]


# ---------------------------------------------------------------- Profiles and launching

DEFAULT_PROFILES = [
	{'name': 'Play', 'note': 'The game as it is, with your settings and mods.', 'exe': 'play', 'start': 'menu'},
	{'name': 'Sandbox', 'note': 'Straight into the Sandbox god mode.', 'exe': 'play', 'start': 'activity', 'activityType': 'GAScripted', 'activity': 'Sandbox', 'scene': 'Ketanot Hills'},
	{'name': 'Sandbox at night', 'note': 'The Sandbox in the tutorial bunker after dark.', 'exe': 'play', 'start': 'activity', 'activityType': 'GAScripted', 'activity': 'Sandbox', 'scene': 'Tutorial Bunker', 'timeOfDay': 23, 'weather': 0},
	{'name': 'Debug with tools', 'note': 'The debug build in the Sandbox with the Graphics Lab and World Debug open.', 'exe': 'debug', 'start': 'activity', 'activityType': 'GAScripted', 'activity': 'Sandbox', 'scene': 'Ketanot Hills', 'graphicsLab': True, 'worldDebug': True},
	{'name': 'Classic look', 'note': 'The original look, for comparison.', 'exe': 'play', 'start': 'menu', 'preset': 'Classic'},
	{'name': 'Vanilla', 'note': 'No mods at all.', 'exe': 'play', 'start': 'menu', 'mods': 'none'},
]
PROFILE_KEYS = {'name', 'note', 'exe', 'start', 'activityType', 'activity', 'scene', 'timeOfDay', 'weather', 'weatherIntensity', 'wind', 'preset', 'mods', 'modList', 'graphicsLab', 'worldDebug', 'width', 'height', 'fullscreen'}


def loadProfiles():
	try:
		profiles = json.load(open(PROFILE_FILE, encoding='utf-8'))
		if isinstance(profiles, list) and profiles:
			return profiles
	except (OSError, ValueError):
		pass
	return [dict(profile) for profile in DEFAULT_PROFILES]


def saveProfiles(profiles):
	os.makedirs(STATE_DIR, exist_ok=True)
	with open(PROFILE_FILE, 'w', encoding='utf-8') as out:
		json.dump(profiles, out, indent=1)


def cleanProfile(raw):
	profile = {key: raw[key] for key in PROFILE_KEYS if key in raw and raw[key] not in ('', None)}
	name = str(profile.get('name', '')).strip()
	if not re.fullmatch(r"[\w .,()'+-]{1,60}", name):
		raise ValueError('Give the profile a name (letters, numbers, spaces; up to 60).')
	profile['name'] = name
	profile['exe'] = 'debug' if profile.get('exe') == 'debug' else 'play'
	profile['start'] = 'activity' if profile.get('start') == 'activity' else 'menu'
	return profile


def changesNothing(profile):
	"""A profile that can use your own settings file as it is."""
	return profile['start'] == 'menu' and not any(profile.get(key) not in (None, '', False) for key in ('timeOfDay', 'weather', 'weatherIntensity', 'wind', 'preset', 'graphicsLab', 'worldDebug', 'width', 'height', 'fullscreen'))


def launch(profile, activeMods):
	"""Starts the game as a profile says. Returns the new instance, or raises ValueError with what's wrong."""
	exe = repoPath(EXES[profile['exe']])
	if not os.path.exists(exe):
		raise ValueError('That build of the game does not exist yet. Build it on the Builds page.')
	with lock:
		instanceCounter[0] += 1
		number = instanceCounter[0]
		used = {instance['port'] for instance in instances if instance['process'].poll() is None}
	port = next(candidate for candidate in range(8801, 8900) if candidate not in used)
	name = 'game%d' % number
	instanceDir = repoPath('Instances', name)
	os.makedirs(instanceDir, exist_ok=True)
	for stale in ('Console.txt', 'AbortLog.txt', 'LogLoadingWarning.txt'):
		try:
			os.remove(os.path.join(instanceDir, stale))
		except OSError:
			pass
	environment = dict(os.environ, CCCP_CONTROL_PORT=str(port), CCCP_INSTANCE=name, CCCP_CONSOLE_LOG='Instances/%s/Console.txt' % name)
	for key in ('CCCP_MODS_DIR', 'CCCP_SETTINGSPATH', 'CCCP_UNATTENDED', 'CCCP_HIDE_PANELS', 'CCCP_NO_GAMEPAD'):
		environment.pop(key, None)

	if not changesNothing(profile):
		overrides = {}
		if profile['start'] == 'activity':
			overrides.update({'LaunchIntoActivity': 1, 'SkipIntro': 1, 'DefaultActivityType': profile.get('activityType') or 'GAScripted', 'DefaultActivityName': profile.get('activity') or 'Sandbox', 'DefaultSceneName': profile.get('scene') or 'Ketanot Hills'})
		for key, setting in (('timeOfDay', 'TimeOfDay'), ('weather', 'WeatherType'), ('weatherIntensity', 'WeatherIntensity'), ('wind', 'Wind'), ('width', 'ResolutionX'), ('height', 'ResolutionY')):
			if profile.get(key) not in (None, ''):
				overrides[setting] = profile[key]
		if profile.get('width') or profile.get('height'):
			overrides['ResolutionMultiplier'] = 1
		if profile.get('fullscreen') is not None and profile.get('fullscreen') != '':
			overrides['Fullscreen'] = 1 if profile['fullscreen'] else 0
		if profile.get('graphicsLab'):
			overrides['ShowGraphicsLab'] = 1
		if profile.get('worldDebug'):
			overrides['ShowWorldDebug'] = 1
		if profile.get('preset'):
			try:
				overrides.update({key: value for key, value in readPreset(profile['preset']).items() if key not in NOT_LOOK})
			except (OSError, ValueError):
				raise ValueError('The profile uses the graphics preset "%s", which no longer exists.' % profile['preset'])
		try:
			base = readText(SETTINGS)
		except OSError:
			raise ValueError('Run the game once first, so it writes its Settings.ini.')
		launchDir = os.path.join(STATE_DIR, 'Launch')
		os.makedirs(launchDir, exist_ok=True)
		with open(os.path.join(launchDir, name + '.ini'), 'w', encoding='ascii', errors='replace', newline='\r\n') as out:
			out.write(settingsWith(base, overrides))
		environment['CCCP_SETTINGSPATH'] = 'Userdata/Workbench/Launch/%s.ini' % name

	stage = None
	if profile.get('mods') in ('none', 'list'):
		# A folder of links to the chosen mods, so the game sees just those and your Mods folder is left alone.
		stage = 'ModsLaunch_' + name
		os.makedirs(repoPath(stage), exist_ok=True)
		for mod in (profile.get('modList') or []) if profile['mods'] == 'list' else []:
			if mod in activeMods:
				subprocess.run(['cmd', '/c', 'mklink', '/J', repoPath(stage, mod), repoPath('Mods', mod)], capture_output=True, creationflags=NO_WINDOW)
		environment['CCCP_MODS_DIR'] = stage

	process = subprocess.Popen([exe], cwd=REPO, env=environment)
	instance = {'id': number, 'name': name, 'profile': profile['name'], 'exe': profile['exe'], 'port': port, 'process': process, 'started': time.time(), 'state': None, 'answering': False, 'stage': stage, 'preset': profile.get('preset') or ''}
	with lock:
		instances.append(instance)
	rememberInstances()
	return instance


def cleanUp(instance):
	"""Takes down the folder of mod links a finished game used. The links are removed one by one; what they point at is never touched."""
	stage = instance.get('stage')
	if not stage:
		return
	folder = repoPath(stage)
	try:
		for entry in os.listdir(folder):
			os.rmdir(os.path.join(folder, entry))  # Removes a link (or an empty folder), never a mod's contents.
		os.rmdir(folder)
	except OSError:
		pass


def loadingLine(instance):
	"""What a game that is still starting up is reading right now: the mod named by the last 'loading:' line of its loading log."""
	try:
		with open(repoPath('Instances', instance['name'], 'LogLoading.txt'), 'rb') as log:
			log.seek(0, os.SEEK_END)
			size = log.tell()
			log.seek(max(size - 200000, 0))
			text = log.read().decode('latin-1')
	except OSError:
		return ''
	modules = re.findall(r'^(\S[^\r\n]*?\.rte) . loading:', text, re.M)
	return modules[-1] if modules else ''


def consoleTail(instance, lines=200):
	try:
		with open(repoPath('Instances', instance['name'], 'Console.txt'), encoding='utf-8', errors='replace') as log:
			return log.read().splitlines()[-lines:]
	except OSError:
		return []


def instanceSummary(instance):
	running = instance['process'].poll() is None
	item = {key: instance[key] for key in ('id', 'name', 'profile', 'exe', 'port', 'answering', 'preset')}
	item.update({'running': running, 'seconds': round((instance.get('ended') or time.time()) - instance['started']), 'state': instance['state'] if running else None})
	if running and not instance['answering']:
		item['loading'] = loadingLine(instance)
	if not running:
		item['exitCode'] = instance['process'].returncode
		abort = repoPath('Instances', instance['name'], 'AbortLog.txt')
		if os.path.exists(abort):
			text = readText(abort)
			match = re.search(r'because:\s*(.+?)(?:\n\s*\n|\Z)', text, re.S)
			item['abort'] = (match.group(1).strip() if match else text[-400:])[:600]
	return item


# ---------------------------------------------------------------- What can be launched into

catalogue = {'activities': [], 'scenes': [], 'read': False}


def readCatalogue():
	"""The game modes and maps the game's data defines, for the profile editor's lists."""
	activities, scenes = set(), set()
	for root in (repoPath('Data'), repoPath('Mods')):
		for folder, _, files in os.walk(root):
			for fileName in files:
				if not fileName.lower().endswith('.ini'):
					continue
				try:
					pending = None
					for line in open(os.path.join(folder, fileName), encoding='utf-8', errors='replace'):
						if line.startswith('AddActivity = '):
							pending = ('activity', line.split('=', 1)[1].split('//')[0].strip())
						elif line.startswith('AddScene = '):
							pending = ('scene', '')
						elif pending and line.startswith('\tPresetName = '):
							name = line.split('=', 1)[1].split('//')[0].strip()
							if pending[0] == 'activity':
								activities.add((name, pending[1]))
							else:
								scenes.add(name)
							pending = None
						elif line and not line[0].isspace() and not line.startswith('//'):
							pending = None
				except OSError:
					pass
	catalogue['activities'] = [{'name': name, 'type': kind} for name, kind in sorted(activities)]
	catalogue['scenes'] = sorted(scenes)
	catalogue['read'] = True


def start():
	os.makedirs(STATE_DIR, exist_ok=True)
	seedPresets()
	adoptInstances()
	threading.Thread(target=watcher, daemon=True).start()
	threading.Thread(target=readCatalogue, daemon=True).start()
