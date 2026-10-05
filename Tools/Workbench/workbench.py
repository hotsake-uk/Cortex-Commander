"""The Workbench: a page in your browser for starting the game and running the project's chores - mods, tests, builds - with several copies of the game at once.

Start it:   python Tools/Workbench/workbench.py        (then open http://127.0.0.1:8765, or let Start-Workbench.ps1 do both)
Needs only Python 3.9 or later. It listens on this computer only.

How it works: the page asks this program for the state of things about once a second and sends it requests ("test this mod", "build").
Requests become jobs in a queue. Mod tests can run side by side, each copy of the game in its own sandbox: its own mods folder (CCCP_MODS_DIR)
and its own logs (CCCP_INSTANCE). Builds and picture comparisons run alone, because they replace the game's program file or need steady timing."""
import json, os, re, shutil, subprocess, sys, threading, time, traceback, urllib.parse, webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import live

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
HERE = os.path.dirname(os.path.abspath(__file__))
PORT = 8765
TEST_EXE = 'Cortex Command.debug.release.exe'
PLAY_EXE = 'Cortex Command.exe'
FOLDERS = {'active': 'Mods', 'parked': 'ModsParked'}
STATE_DIR = os.path.join(REPO, 'Userdata', 'Workbench')
RESULTS_FILE = os.path.join(STATE_DIR, 'results.json')
SLOTS = 3  # How many copies of the game may run tests at once.
NO_WINDOW = getattr(subprocess, 'CREATE_NO_WINDOW', 0)

lock = threading.RLock()
jobs = []          # Every job of this session, oldest first.
jobCounter = [0]
results = {}       # Mod folder name -> {'verdict', 'time', 'problems', 'seconds'}
sizeCache = {}     # Folder path -> (mtime, megabytes)
infoCache = {}     # Folder path -> (mtime, info)


def repoPath(*parts):
	return os.path.join(REPO, *parts)


def loadResults():
	global results
	try:
		results = json.load(open(RESULTS_FILE, encoding='utf-8'))
	except (OSError, ValueError):
		results = {}


def saveResults():
	os.makedirs(STATE_DIR, exist_ok=True)
	with open(RESULTS_FILE, 'w', encoding='utf-8') as out:
		json.dump(results, out, indent=1)


# ---------------------------------------------------------------- What's on disk

def readModInfo(path):
	"""The few facts at the top of a mod's Index.ini."""
	info = {'title': '', 'author': '', 'description': '', 'madeFor': ''}
	keys = {'ModuleName': 'title', 'Author': 'author', 'Description': 'description', 'SupportedGameVersion': 'madeFor'}
	try:
		with open(os.path.join(path, 'Index.ini'), encoding='utf-8', errors='replace') as index:
			for number, line in enumerate(index):
				if number > 60:
					break
				match = re.match(r'^\t(\w+)\s*=\s*(.*)$', line)
				if match and match.group(1) in keys and not info[keys[match.group(1)]]:
					info[keys[match.group(1)]] = match.group(2).split('//')[0].strip()
	except OSError:
		pass
	return info


def folderMegabytes(path):
	total = 0
	for root, _, files in os.walk(path):
		for name in files:
			try:
				total += os.path.getsize(os.path.join(root, name))
			except OSError:
				pass
	return round(total / 1048576, 1)


def listMods():
	mods = []
	for where, folder in FOLDERS.items():
		base = repoPath(folder)
		if not os.path.isdir(base):
			continue
		for name in sorted(os.listdir(base), key=str.lower):
			path = os.path.join(base, name)
			if not name.lower().endswith('.rte') or not os.path.isdir(path) or name == 'RenderTest.rte':
				continue
			try:
				mtime = os.path.getmtime(path)
			except OSError:
				continue
			if infoCache.get(path, (None,))[0] != mtime:
				infoCache[path] = (mtime, readModInfo(path))
			if sizeCache.get(path, (None,))[0] != mtime:
				sizeCache[path] = (mtime, folderMegabytes(path))
			mod = {'name': name, 'where': where, 'megabytes': sizeCache[path][1]}
			mod.update(infoCache[path][1])
			mod['result'] = results.get(name)
			mod['busy'] = any(job['status'] in ('queued', 'running') and name in job.get('mods', []) for job in jobs)
			mods.append(mod)
	return mods


def findMod(name):
	"""Where a mod is, or None. Only names that really are mod folders are accepted, so nothing else can be moved or deleted through the page."""
	if not re.fullmatch(r'[^\\/:*?"<>|]+\.rte', name, re.IGNORECASE) or name == 'RenderTest.rte':
		return None
	for where, folder in FOLDERS.items():
		if os.path.isdir(repoPath(folder, name)):
			return where
	return None


def groupOf(name, allNames):
	"""Mods that only work together are tested together: X.rte with X_Something.rte."""
	stem = name[:-4]
	root = stem.split('_')[0]
	if (root + '.rte') in allNames:
		return sorted(other for other in allNames if other[:-4] == root or other[:-4].startswith(root + '_'))
	return [name]


gameRunning = {'checked': 0.0, 'value': False}


def playerGameRunning():
	if time.time() - gameRunning['checked'] > 3:
		gameRunning['checked'] = time.time()
		try:
			listing = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq ' + PLAY_EXE, '/FO', 'CSV', '/NH'], capture_output=True, text=True, timeout=10, creationflags=NO_WINDOW).stdout
			gameRunning['value'] = PLAY_EXE.lower() in listing.lower()
		except (OSError, subprocess.SubprocessError):
			pass
	return gameRunning['value']


def git(*arguments):
	try:
		return subprocess.run(['git'] + list(arguments), cwd=REPO, capture_output=True, text=True, timeout=15, creationflags=NO_WINDOW).stdout.strip()
	except (OSError, subprocess.SubprocessError):
		return ''


gitInfo = {'checked': 0.0, 'value': {}}


def repoInfo():
	if time.time() - gitInfo['checked'] > 10:
		gitInfo['checked'] = time.time()
		status = git('status', '--short')
		gitInfo['value'] = {'branch': git('rev-parse', '--abbrev-ref', 'HEAD'), 'head': git('log', '-1', '--format=%h %s'), 'changed': len([line for line in status.splitlines() if line.strip()])}
	return gitInfo['value']


def exeInfo(name):
	try:
		return {'name': name, 'built': time.strftime('%Y-%m-%d %H:%M', time.localtime(os.path.getmtime(repoPath(name))))}
	except OSError:
		return {'name': name, 'built': None}


def scenarios():
	"""The test scenarios, read from the script that writes them, so a new one shows up without anything being run first."""
	try:
		with open(repoPath('Tools', 'RenderTest', 'Setup.ps1'), encoding='utf-8', errors='replace') as source:
			return sorted(set(re.findall(r'^Write-Scenario "(\w+)"', source.read(), re.M)))
	except OSError:
		return []


def pictures():
	"""The regression scenes: baseline, latest capture and difference picture, where they exist."""
	golden = repoPath('Tools', 'RenderTest', 'Golden')
	output = repoPath('Tools', 'RenderTest', 'Output')
	scenes = []
	try:
		names = sorted(name[:-4] for name in os.listdir(golden) if name.endswith('.png'))
	except OSError:
		names = []
	for name in names:
		scene = {'name': name, 'golden': 'Tools/RenderTest/Golden/%s.png' % name}
		for key, fileName in (('latest', 'golden_%s.png' % name), ('diff', 'golden_%s_diff.png' % name)):
			path = os.path.join(output, fileName)
			if os.path.exists(path):
				scene[key] = 'Tools/RenderTest/Output/' + fileName
				scene[key + 'Time'] = int(os.path.getmtime(path))
		scenes.append(scene)
	return scenes


# ---------------------------------------------------------------- Jobs

def addJob(kind, title, exclusive, run, **extra):
	with lock:
		jobCounter[0] += 1
		job = {'id': jobCounter[0], 'kind': kind, 'title': title, 'exclusive': exclusive, 'status': 'queued', 'queued': time.time(), 'started': None, 'ended': None, 'log': [], 'summary': '', 'run': run}
		job.update(extra)
		jobs.append(job)
		return job


def say(job, text):
	with lock:
		for line in str(text).splitlines() or ['']:
			job['log'].append(line)
		if len(job['log']) > 4000:
			del job['log'][:len(job['log']) - 4000]


def dispatcher():
	"""Starts queued jobs: any number of ordinary ones up to the slots, an exclusive one only on its own, and nothing new while an exclusive one waits its turn."""
	while True:
		time.sleep(0.3)
		with lock:
			running = [job for job in jobs if job['status'] == 'running']
			queued = [job for job in jobs if job['status'] == 'queued']
			if not queued:
				continue
			nextJob = queued[0]
			if any(job['exclusive'] for job in running):
				continue
			if nextJob['exclusive']:
				if running:
					continue
			elif len(running) >= SLOTS:
				continue
			used = {job.get('slot') for job in running}
			nextJob['slot'] = next(slot for slot in range(1, SLOTS + 2) if slot not in used)
			nextJob['status'] = 'running'
			nextJob['started'] = time.time()
		threading.Thread(target=runJob, args=(nextJob,), daemon=True).start()


def runJob(job):
	try:
		ok = job['run'](job)
		status = 'done' if ok is not False else 'failed'
	except Exception:
		say(job, traceback.format_exc())
		status = 'failed'
	with lock:
		job['status'] = status
		job['ended'] = time.time()


def runCommand(job, command, timeout=3600):
	"""Runs a program, copying what it prints into the job's log as it comes."""
	say(job, '> ' + ' '.join(command))
	process = subprocess.Popen(command, cwd=REPO, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace', creationflags=NO_WINDOW)
	job['process'] = process
	started = time.time()
	for line in process.stdout:
		say(job, line.rstrip('\n'))
		if time.time() - started > timeout:
			process.kill()
			say(job, 'Stopped: it ran longer than %d seconds.' % timeout)
			break
	process.wait()
	return process.returncode


def powershell(job, script, *arguments, timeout=3600):
	return runCommand(job, ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', repoPath(*script.split('/'))] + list(arguments), timeout)


# ---------------------------------------------------------------- Testing mods

SWEEP_SETTINGS = 'Userdata/RenderTest/ModSweep.ini'
PROBLEM = re.compile(r'ERROR|MODSWEEP PROBLEM|attempt to|nil value|No such|not found|ASSERT')


def testMods(job):
	"""Loads the job's mods in a sandboxed copy of the game, puts their units and weapons through a short scripted fight, and records what went wrong."""
	names = job['mods']
	slot = 'wb%d' % job['slot']
	stage = repoPath('ModsTest_' + slot)
	instance = repoPath('Instances', slot)
	if not os.path.exists(repoPath(*SWEEP_SETTINGS.split('/'))):
		say(job, 'Writing the test scenarios (first run).')
		powershell(job, 'Tools/RenderTest/Setup.ps1')
	shutil.rmtree(stage, ignore_errors=True)
	os.makedirs(stage)
	shutil.rmtree(instance, ignore_errors=True)
	os.makedirs(instance)
	shutil.copytree(repoPath('Tools', 'RenderTest', 'RenderTest.rte'), os.path.join(stage, 'RenderTest.rte'))
	for name in names:
		where = findMod(name)
		if not where:
			say(job, 'No such mod: ' + name)
			return False
		shutil.copytree(repoPath(FOLDERS[where], name), os.path.join(stage, name))
	console = os.path.join(instance, 'Console.txt')
	environment = dict(os.environ, CCCP_MODS_DIR='ModsTest_' + slot, CCCP_INSTANCE=slot, CCCP_UNATTENDED='1', CCCP_SETTINGSPATH=SWEEP_SETTINGS, CCCP_NO_GAMEPAD='1', CCCP_HIDE_PANELS='1', CCCP_CONSOLE_LOG=os.path.join('Instances', slot, 'Console.txt'))
	say(job, 'Starting the game with %s (sandbox %s).' % (', '.join(names), slot))
	started = time.time()
	process = subprocess.Popen([repoPath(TEST_EXE)], cwd=REPO, env=environment)
	job['process'] = process
	limit = 120 + 30 * len(names)
	finished = exited = False
	while time.time() - started < limit:
		time.sleep(0.5)
		if job.get('cancel'):
			break
		try:
			with open(console, encoding='utf-8', errors='replace') as log:
				if 'MODSWEEP DONE' in log.read():
					finished = True
					break
		except OSError:
			pass
		if process.poll() is not None:
			exited = True
			break
	if process.poll() is None:
		process.kill()
		process.wait()
	seconds = round(time.time() - started)

	problems = []
	if job.get('cancel'):
		problems.append('The test was stopped.')
	elif exited:
		problems.append('The game stopped by itself (a crash, or an abort) with exit code %s.' % process.returncode)
	elif not finished:
		problems.append('The test did not finish within %d seconds (a hang, or far too slow).' % limit)
	try:
		for line in open(os.path.join(instance, 'LogLoadingWarning.txt'), encoding='utf-8', errors='replace'):
			if line.startswith('MOD NOT LOADED'):
				problems.append(line.strip())
	except OSError:
		pass
	try:
		abort = open(os.path.join(instance, 'AbortLog.txt'), encoding='utf-8', errors='replace').read()
		match = re.search(r'because:\s*(.+?)(?:\n\s*\n|\Z)', abort, re.S)
		problems.append('ABORT: ' + (match.group(1).strip() if match else abort[-300:]).replace('\n', ' ')[:400])
	except OSError:
		pass
	counts = {}
	section = None
	lastMaking = ''
	try:
		for line in open(console, encoding='utf-8', errors='replace'):
			line = line.rstrip('\n')
			if 'MODSWEEP BEGIN ' in line:
				section = line.split('MODSWEEP BEGIN ', 1)[1]
			elif 'MODSWEEP making ' in line:
				lastMaking = line.split('MODSWEEP making ', 1)[1]
			elif section and 'MODSWEEP' not in line and PROBLEM.search(line):
				key = line[:300]
				counts[key] = counts.get(key, 0) + 1
	except OSError:
		pass
	if exited and lastMaking:
		problems.append('The last thing put into the world before it stopped: ' + lastMaking)
	for line, count in sorted(counts.items(), key=lambda item: -item[1])[:40]:
		problems.append(('x%d  ' % count if count > 1 else '') + line)
	verdict = 'clean' if not problems else 'problems'
	say(job, '%s in %d seconds.' % ('CLEAN' if verdict == 'clean' else 'PROBLEMS', seconds))
	for problem in problems:
		say(job, '  ' + problem)
	with lock:
		if not job.get('cancel'):
			for name in names:
				results[name] = {'verdict': verdict, 'time': int(time.time()), 'problems': problems, 'seconds': seconds}
			saveResults()
			# A mod that works goes where the game loads it; one that doesn't is kept out of the way.
			if job.get('sort'):
				for name in names:
					where = findMod(name)
					target = 'active' if verdict == 'clean' else 'parked'
					if where and where != target:
						os.makedirs(repoPath(FOLDERS[target]), exist_ok=True)
						shutil.move(repoPath(FOLDERS[where], name), repoPath(FOLDERS[target], name))
						say(job, 'Moved %s to %s.' % (name, FOLDERS[target]))
		job['summary'] = verdict
	shutil.rmtree(stage, ignore_errors=True)
	return verdict == 'clean'


def queueModTests(names, sort):
	allNames = [mod['name'] for mod in listMods()]
	seen = set()
	made = []
	for name in names:
		if name in seen or name not in allNames:
			continue
		group = groupOf(name, allNames)
		seen.update(group)
		with lock:
			if any(job['status'] in ('queued', 'running') and set(group) & set(job.get('mods', [])) for job in jobs):
				continue
		made.append(addJob('modtest', 'Test ' + ', '.join(group), False, testMods, mods=group, sort=sort)['id'])
	return made


# ---------------------------------------------------------------- Other jobs

def buildJob(config):
	def run(job):
		if config == 'Final' and playerGameRunning():
			say(job, 'The game is running, so its program file cannot be replaced. Close the game and try again.')
			return False
		arguments = ['-Config', 'Final'] if config == 'Final' else []
		powershell(job, 'Tools/RenderTest/Build.ps1', *arguments)
		text = '\n'.join(job['log'])
		match = re.search(r'(\d+) unique errors', text)
		errors = int(match.group(1)) if match else -1
		job['summary'] = 'built' if errors == 0 else ('%d errors' % errors if errors > 0 else 'unclear')
		return errors == 0
	return run


def goldenJob(update):
	def run(job):
		code = powershell(job, 'Tools/RenderTest/Golden.ps1', *(['-Update'] if update else []))
		lines = [line for line in job['log'] if line.startswith(('PASS', 'FAIL', 'UPDATED'))]
		failed = sum(1 for line in lines if line.startswith('FAIL'))
		job['summary'] = ('%d baselines re-recorded' % len(lines)) if update else ('%d of %d match' % (len(lines) - failed, len(lines)))
		return code == 0
	return run


def captureJob(scenario, wait):
	def run(job):
		# The scenarios' settings files are written afresh first: they are made from your own settings, and a new scenario has none yet.
		powershell(job, 'Tools/RenderTest/Setup.ps1')
		code = powershell(job, 'Tools/RenderTest/Capture.ps1', '-Scenario', scenario, '-ExtraWait', str(wait))
		job['picture'] = 'Tools/RenderTest/Output/%s.png' % scenario
		job['summary'] = 'captured' if code == 0 else 'failed'
		return code == 0
	return run


def scriptJob(command):
	def run(job):
		return runCommand(job, command) == 0
	return run


# ---------------------------------------------------------------- The page's requests

def state():
	with lock:
		running = [job for job in jobs if job['status'] == 'running']
		jobList = []
		for job in jobs[-60:]:
			item = {key: job.get(key) for key in ('id', 'kind', 'title', 'status', 'summary', 'slot', 'picture', 'mods')}
			end = job['ended'] or time.time()
			item['seconds'] = round(end - job['started']) if job['started'] else 0
			item['tail'] = job['log'][-1][:200] if job['log'] else ''
			jobList.append(item)
		return {
			'mods': listMods(),
			'jobs': jobList,
			'running': len(running),
			'queued': sum(1 for job in jobs if job['status'] == 'queued'),
			'slots': SLOTS,
			'gameRunning': playerGameRunning(),
			'repo': repoInfo(),
			'exes': [exeInfo(PLAY_EXE), exeInfo(TEST_EXE)],
			'scenarios': scenarios(),
			'pictures': pictures(),
			'profiles': live.loadProfiles(),
			'instances': [live.instanceSummary(instance) for instance in list(live.instances)],
			'presets': live.listPresets(),
			'catalogueReady': live.catalogue['read'],
		}


def act(request):
	"""Carries out one request from the page. Returns what to tell it."""
	action = request.get('action')
	if action == 'testMods':
		return {'jobs': queueModTests(request.get('mods', []), bool(request.get('sort')))}
	if action == 'testWhere':
		names = [mod['name'] for mod in listMods() if mod['where'] == request.get('where')]
		return {'jobs': queueModTests(names, True)}
	if action == 'moveMod':
		name, target = request.get('mod', ''), request.get('to')
		where = findMod(name)
		if not where or target not in FOLDERS:
			return {'error': 'No such mod or place.'}
		with lock:
			if any(job['status'] in ('queued', 'running') and name in job.get('mods', []) for job in jobs):
				return {'error': 'That mod is being tested; wait for the test.'}
		if where != target:
			if playerGameRunning() and where == 'active':
				return {'error': 'The game is running and may be using that mod. Close the game first.'}
			os.makedirs(repoPath(FOLDERS[target]), exist_ok=True)
			shutil.move(repoPath(FOLDERS[where], name), repoPath(FOLDERS[target], name))
		return {'ok': True}
	if action == 'build':
		config = 'Final' if request.get('config') == 'Final' else 'Debug Release'
		return {'job': addJob('build', 'Build ' + config, True, buildJob(config))['id']}
	if action == 'golden':
		update = bool(request.get('update'))
		return {'job': addJob('golden', 'Re-record the regression baselines' if update else 'Regression scenes', True, goldenJob(update))['id']}
	if action == 'capture':
		scenario = request.get('scenario', '')
		if scenario not in scenarios():
			return {'error': 'No such scenario.'}
		wait = max(2, min(int(request.get('wait', 8)), 120))
		return {'job': addJob('capture', 'Capture ' + scenario, True, captureJob(scenario, wait))['id']}
	if action == 'fixMods':
		return {'job': addJob('script', 'Apply known fixes to mods', True, scriptJob([sys.executable, repoPath('Tools', 'Mods', 'Fix-Mods.py')]))['id']}
	if action == 'getMods':
		return {'job': addJob('script', 'Download the mod list', True, scriptJob(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', repoPath('Tools', 'Mods', 'Get-Mods.ps1')]))['id']}
	if action == 'play':
		request = {'action': 'launch', 'profile': live.loadProfiles()[0]['name']}
		action = 'launch'
	companion = companionAct(action, request)
	if companion is not None:
		gameRunning['checked'] = 0
		return companion
	if action == 'openFolder':
		folder = {'mods': 'Mods', 'parked': 'ModsParked', 'output': 'Tools/RenderTest/Output'}.get(request.get('which'))
		if folder and os.path.isdir(repoPath(*folder.split('/'))):
			os.startfile(repoPath(*folder.split('/')))
		return {'ok': True}
	if action == 'stopJob':
		with lock:
			for job in jobs:
				if job['id'] == request.get('job'):
					if job['status'] == 'queued':
						job['status'] = 'failed'
						job['summary'] = 'cancelled'
						job['ended'] = time.time()
					elif job['status'] == 'running':
						job['cancel'] = True
						process = job.get('process')
						if process and process.poll() is None:
							process.kill()
		return {'ok': True}
	if action == 'setSlots':
		global SLOTS
		SLOTS = max(1, min(int(request.get('slots', 3)), 6))
		return {'ok': True}
	return {'error': 'Unknown request.'}


def companionAct(action, request):
	"""Requests about launch profiles and the games started from them. Returns None if the request isn't one of these."""
	try:
		if action == 'launch':
			profile = next((item for item in live.loadProfiles() if item['name'] == request.get('profile')), None)
			if not profile:
				return {'error': 'No such profile.'}
			active = [mod['name'] for mod in listMods() if mod['where'] == 'active']
			return {'instance': live.launch(live.cleanProfile(profile), active)['id']}
		if action == 'saveProfile':
			profile = live.cleanProfile(request.get('profile') or {})
			profiles = live.loadProfiles()
			original = request.get('original') or profile['name']
			if profile['name'] != original and any(item['name'] == profile['name'] for item in profiles):
				return {'error': 'There is already a profile with that name.'}
			index = next((number for number, item in enumerate(profiles) if item['name'] == original), None)
			if index is None:
				profiles.append(profile)
			else:
				profiles[index] = profile
			live.saveProfiles(profiles)
			return {'ok': True}
		if action == 'deleteProfile':
			profiles = [item for item in live.loadProfiles() if item['name'] != request.get('profile')]
			if not profiles:
				return {'error': 'Keep at least one profile.'}
			live.saveProfiles(profiles)
			return {'ok': True}
		if action == 'resetProfiles':
			live.saveProfiles([dict(profile) for profile in live.DEFAULT_PROFILES])
			return {'ok': True}
		if action == 'gameCommand':
			instance = int(request.get('instance', 0))
			kind = request.get('kind')
			if kind == 'quit':
				reply = live.command(instance, 'quit')
			elif kind == 'kill':
				found = live.findInstance(instance)
				if found and found['process'].poll() is None:
					found['process'].kill()
				reply = 'ok'
			else:
				return {'error': 'Unknown command.'}
			return {'ok': True, 'reply': reply} if reply.startswith('ok') else {'error': reply[4:] or 'The game did not answer.'}
	except (ValueError, OSError) as problem:
		return {'error': str(problem)}
	return None


PICTURE_ROOTS = [os.path.join(REPO, 'Tools', 'RenderTest', 'Output'), os.path.join(REPO, 'Tools', 'RenderTest', 'Golden'), os.path.join(REPO, 'Documentation', 'Images'), os.path.join(REPO, 'ScreenShots')]


class Handler(BaseHTTPRequestHandler):
	def log_message(self, *arguments):
		pass

	def send(self, code, body, kind='application/json'):
		data = body if isinstance(body, bytes) else body.encode('utf-8')
		self.send_response(code)
		self.send_header('Content-Type', kind)
		self.send_header('Content-Length', str(len(data)))
		self.send_header('Cache-Control', 'no-store')
		self.end_headers()
		self.wfile.write(data)

	def fromThisComputer(self):
		# Only pages served by this program may ask it to do things: other web pages open in the browser are turned away.
		host = self.headers.get('Host', '')
		origin = self.headers.get('Origin')
		return host in ('127.0.0.1:%d' % PORT, 'localhost:%d' % PORT) and (origin is None or origin in ('http://127.0.0.1:%d' % PORT, 'http://localhost:%d' % PORT))

	def do_GET(self):
		if not self.fromThisComputer():
			return self.send(403, '{}')
		url = urllib.parse.urlparse(self.path)
		query = urllib.parse.parse_qs(url.query)
		if url.path == '/':
			return self.send(200, open(os.path.join(HERE, 'index.html'), 'rb').read(), 'text/html; charset=utf-8')
		if url.path == '/companion.js':
			return self.send(200, open(os.path.join(HERE, 'companion.js'), 'rb').read(), 'text/javascript; charset=utf-8')
		if url.path == '/api/state':
			return self.send(200, json.dumps(state()))
		if url.path == '/api/log':
			with lock:
				for job in jobs:
					if str(job['id']) == query.get('job', [''])[0]:
						return self.send(200, json.dumps({'log': job['log'][-1500:], 'status': job['status'], 'title': job['title']}))
			return self.send(404, '{}')
		if url.path == '/api/meta':
			return self.send(200, json.dumps({'activities': live.catalogue['activities'], 'scenes': live.catalogue['scenes']}))
		if url.path == '/picture':
			path = os.path.normpath(os.path.join(REPO, query.get('path', [''])[0]))
			if any(path.lower().startswith(root.lower() + os.sep) for root in PICTURE_ROOTS) and path.lower().endswith('.png') and os.path.isfile(path):
				return self.send(200, open(path, 'rb').read(), 'image/png')
			return self.send(404, b'', 'image/png')
		self.send(404, '{}')

	def do_POST(self):
		if not self.fromThisComputer() or self.headers.get('Content-Type', '').split(';')[0] != 'application/json':
			return self.send(403, '{}')
		try:
			request = json.loads(self.rfile.read(int(self.headers.get('Content-Length', '0'))) or b'{}')
			self.send(200, json.dumps(act(request)))
		except Exception as problem:
			self.send(200, json.dumps({'error': str(problem)}))


def main():
	loadResults()
	live.start()
	threading.Thread(target=dispatcher, daemon=True).start()
	server = ThreadingHTTPServer(('127.0.0.1', PORT), Handler)
	address = 'http://127.0.0.1:%d' % PORT
	print('Workbench running at %s  (Ctrl+C to stop)' % address)
	if '--no-browser' not in sys.argv:
		webbrowser.open(address)
	try:
		server.serve_forever()
	except KeyboardInterrupt:
		pass


if __name__ == '__main__':
	main()
