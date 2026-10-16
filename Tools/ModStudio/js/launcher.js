// Cortex Mod Studio: the Game screen, only in the Cortex Studio app. It does the Windows launcher's jobs: the setup (Simple or
// Advanced, what to install, where the repo is), the versions of the game, building one, launching it to play or to test mods, and the
// launch settings (Settings.ini, presets, mods folder, world). The app's server does the work (src-tauri/src/launcher.rs); this asks it
// through "launcher/..." requests.
'use strict';
window.MS = window.MS || {};

(function () {
	const $ = (id) => document.getElementById(id);
	const app = () => MS.app;
	const el = (tag, props, ...children) => {
		const e = document.createElement(tag);
		Object.assign(e, props || {});
		for (const c of children.flat()) {
			if (c !== null && c !== undefined && c !== false) {
				e.append(c);
			}
		}
		return e;
	};
	const short = (sha) => (sha || '').slice(0, 10);

	const L = {
		state: null, // the answer to launcher/config
		tab: 'play',
		refs: null,
		ref: '',
		commits: [],
		versions: {},
		picked: '',
		jobNext: 0,
		jobPolling: false,
		afterJob: null,
		setupStep: 0,
	};
	MS.launcher = L;

	async function call(action, body, params) {
		const qs = params ? '?' + new URLSearchParams(params).toString() : '';
		const response = await fetch('/api/launcher/' + action + qs, {
			method: body === undefined ? 'GET' : action === 'config' ? 'PUT' : 'POST',
			body: body === undefined ? undefined : JSON.stringify(body),
			headers: { 'X-ModStudio-Token': MS.GameFS.serverToken, 'Content-Type': 'application/json' },
			cache: 'no-store',
		});
		const answer = await response.json().catch(() => ({ error: 'Cortex Studio sent an answer it couldn\'t read.' }));
		if (!response.ok) {
			throw new Error(answer.error || 'Cortex Studio said no (' + response.status + ').');
		}
		return answer;
	}

	async function guarded(work) {
		try {
			return await work();
		} catch (e) {
			app().toast(e.message, 'bad');
			return undefined;
		}
	}

	// ---- start-up: only in the app ----

	L.init = async function () {
		const token = MS.GameFS.serverToken;
		if (!token) {
			return;
		}
		let info;
		try {
			info = await (await fetch('/api/info', { headers: { 'X-ModStudio-Token': token }, cache: 'no-store' })).json();
		} catch (e) {
			return;
		}
		if (info.app !== 'Cortex Studio') {
			return;
		}
		const button = $('btnGame');
		button.hidden = false;
		button.onclick = () => L.open();
		$('runSplit').hidden = false;
		$('btnRun').onclick = () => runLast();
		$('btnRunMenu').onclick = () => toggleRunMenu();
		$('gameClose').onclick = () => L.close();
		document.addEventListener('keydown', (e) => {
			if (e.key === 'Escape' && !$('game').hidden && $('modal').hidden) {
				L.close();
			}
		});
		await L.refresh();
		if (L.state && !L.state.config.mode) {
			L.open('setup');
		}
		// A build still running when the page reloaded (after a launch moved Studio) keeps showing its output.
		const job = await guarded(() => call('job', undefined, { from: 0 }));
		if (job && job.state === 'running') {
			L.jobNext = 0;
			pollJob();
		}
	};

	L.refresh = async function () {
		const state = await guarded(() => call('config'));
		if (state) {
			L.state = state;
			updateRunButton();
		}
		return state;
	};

	// ---- the Play button and its menu in Studio's top bar ----

	const LAUNCHES = [
		['play', '▶ Play', 'The game as players see it'],
		['test', 'Test in game', 'A test map with Studio\'s unit: Preview in game and the console work with it'],
		['sandbox', 'Sandbox', 'A sandbox map to try your mod in'],
		['editor', 'Actor Editor', 'The game\'s Actor Editor, where Preview in game shows your unit'],
	];

	function lastLaunch() {
		let mode = 'play';
		try {
			mode = localStorage.getItem('cortexStudio.lastLaunch') || 'play';
		} catch (e) {}
		return LAUNCHES.find((l) => l[0] === mode) || LAUNCHES[0];
	}

	function updateRunButton() {
		const [, label, what] = lastLaunch();
		const run = $('btnRun');
		run.textContent = label.startsWith('▶') ? label : '▶ ' + label;
		run.title = L.state && L.state.builtExe ? what + ' (the ▾ has the other ways to start it)' : 'Nothing is built yet: opens the Game screen to build the game';
	}

	function runLast() {
		if (!L.state || !L.state.builtExe) {
			L.open('play');
			return;
		}
		L.launch(lastLaunch()[0]);
	}

	function toggleRunMenu() {
		const open = $('runMenu');
		if (open) {
			open.remove();
			return;
		}
		const s = L.state;
		if (!s) {
			return;
		}
		const menu = el('div', { className: 'ctx-menu run-menu', id: 'runMenu' });
		const close = () => {
			menu.remove();
			document.removeEventListener('pointerdown', outside, true);
		};
		const outside = (e) => {
			if (!menu.contains(e.target) && !$('runSplit').contains(e.target)) {
				close();
			}
		};
		const item = (main, sub, action, disabled) => {
			const b = el('button', {}, el('b', { textContent: main }), sub ? el('span', { textContent: sub }) : null);
			b.disabled = !!disabled;
			b.onclick = () => {
				close();
				action();
			};
			return b;
		};
		const advanced = s.config.mode === 'advanced';
		menu.append(el('div', { className: 'run-status' }, s.repoOk ? builtLine() : el('span', { className: 'gm-bad', textContent: 'No game repo yet' }), advanced || !s.repoOk ? null : el('span', { className: 'gm-dim', textContent: ' · ' + s.config.remote + '/' + s.config.branch })));
		const last = lastLaunch()[0];
		for (const [mode, label, what] of LAUNCHES) {
			const b = item(label.replace('▶ ', ''), what, () => L.launch(mode), !s.builtExe);
			if (mode === last) {
				b.classList.add('current');
			}
			menu.append(b);
		}
		menu.append(el('div', { className: 'run-sep' }));
		if (s.repoOk && !advanced) {
			menu.append(item('⟳ Update & build', 'Fetch the newest ' + s.config.branch + ' and build it if it changed', () => L.open('play').then(() => startJob('update', {}))));
		}
		if (s.repoOk && advanced) {
			menu.append(item('Versions…', 'Pick and build a branch, tag or commit', () => L.open('versions')));
		}
		menu.append(item('Launch settings…', 'Settings.ini, presets, mods folder and world', () => L.open('settings')));
		document.body.append(menu);
		const r = $('runSplit').getBoundingClientRect();
		menu.style.left = Math.min(r.left, window.innerWidth - menu.offsetWidth - 8) + 'px';
		menu.style.top = r.bottom + 4 + 'px';
		setTimeout(() => document.addEventListener('pointerdown', outside, true), 0);
	}

	L.open = async function (tab) {
		$('game').hidden = false;
		await L.refresh();
		if (!L.state) {
			return;
		}
		L.tab = tab || (!L.state.config.mode ? 'setup' : L.tab === 'setup' ? 'play' : L.tab);
		render();
	};

	L.close = function () {
		if (L.state && !L.state.config.mode) {
			app().toast('You can finish the setup any time from the Game button.', '');
		}
		$('game').hidden = true;
	};

	// ---- the screen ----

	function render() {
		const tabs = $('gameTabs');
		tabs.innerHTML = '';
		const advanced = L.state.config.mode === 'advanced';
		const list = [
			['play', 'Play'],
			advanced ? ['versions', 'Versions'] : null,
			['settings', 'Launch settings'],
			['setup', 'Setup'],
		].filter(Boolean);
		for (const [id, label] of list) {
			const b = el('button', { textContent: label, className: L.tab === id ? 'sel' : '' });
			b.onclick = () => {
				L.tab = id;
				render();
			};
			tabs.append(b);
		}
		const body = $('gameBody');
		body.innerHTML = '';
		const view = { play: renderPlay, versions: renderVersions, settings: renderSettings, setup: renderSetup }[L.tab] || renderPlay;
		view(body);
	}

	function builtLine() {
		const s = L.state;
		if (!s.builtExe) {
			return el('span', { className: 'gm-bad', textContent: 'Nothing built yet' });
		}
		const [sha, configuration] = (s.built || '').split(' ');
		return el('span', {}, el('span', { className: 'gm-good', textContent: '● Built ' }), el('code', { textContent: short(sha) || '?' }), ' (' + (configuration || s.config.configuration) + ')');
	}

	function card(title, ...children) {
		return el('section', { className: 'gm-card' }, title ? el('h3', { textContent: title }) : null, ...children);
	}

	function row(...children) {
		return el('div', { className: 'gm-row' }, ...children);
	}

	function button(text, action, opts) {
		const b = el('button', { className: 'tb' + (opts && opts.primary ? ' primary' : ''), textContent: text, title: (opts && opts.title) || '' });
		b.disabled = !!(opts && opts.disabled);
		b.onclick = async () => {
			b.disabled = true;
			try {
				await action();
			} finally {
				b.disabled = !!(opts && opts.disabled);
			}
		};
		return b;
	}

	// ---- Play ----

	function renderPlay(body) {
		const s = L.state;
		const simple = s.config.mode !== 'advanced';
		if (!s.repoOk) {
			body.append(card('No game repo yet', el('p', { textContent: 'Cortex Studio builds the game from a git checkout of Cortex Command. Pick yours in Setup.' }), row(button('Open Setup', () => openTab('setup'), { primary: true }))));
			return;
		}
		const version = card(
			simple ? 'Your game' : 'The version in Studio\'s slot',
			el('p', {}, simple ? 'Keeps the newest ' : 'Built from the Versions tab. ', simple ? el('b', { textContent: s.config.remote + '/' + s.config.branch }) : null, simple ? ' built and ready. ' : '', builtLine()),
			el('p', { className: 'gm-dim', textContent: s.slot }),
			row(
				simple
					? button('⟳ Update & build', () => startJob('update', {}), { primary: !s.builtExe, title: 'Fetch the newest ' + s.config.branch + ' and build it if it changed' })
					: button('Pick a version…', () => openTab('versions')),
				el('label', { className: 'gm-inline' }, 'Build type ', configurationSelect()),
			),
		);
		const map = el('input', { type: 'text', value: s.config.map || '', placeholder: 'Maginot (the default)', className: 'gm-input' });
		map.onchange = () => saveConfig({ map: map.value.trim() });
		const launches = card(
			'Launch',
			el('div', { className: 'gm-launch' },
				launchButton('▶ Play', 'play', 'The game as players see it'),
				launchButton('Test in game', 'test', 'A test map with Studio\'s unit: Preview in game and the console work with it'),
				launchButton('Sandbox', 'sandbox', 'A sandbox map to try your mod in'),
				launchButton('Actor Editor', 'editor', 'The game\'s Actor Editor, where Preview in game shows your unit'),
			),
			row(el('label', { className: 'gm-inline' }, 'Map for Test and Sandbox ', map)),
			el('p', { className: 'gm-dim', textContent: 'Launching uses your launch settings (Settings.ini, presets, mods, world) and points Studio at the version that starts, so saves, Preview in game and the console talk to that game.' }),
		);
		const editing = card(
			'What Studio is editing',
			el('p', {}, el('code', { textContent: s.gameDir })),
			row(
				button('Edit the built version', () => editIn('slot'), { disabled: s.editingSlot || !s.builtExe }),
				button('Edit the repo checkout', () => editIn('repo')),
			),
		);
		body.append(version, launches, editing);
	}

	function launchButton(text, mode, title) {
		const b = el('button', { className: 'big-launch' + (mode === 'play' ? ' primary' : ''), title });
		b.append(el('b', { textContent: text }), el('span', { textContent: title }));
		b.disabled = !L.state.builtExe;
		b.onclick = () => L.launch(mode);
		return b;
	}

	function configurationSelect() {
		const s = L.state;
		const select = el('select', { className: 'gm-input' });
		for (const c of s.configurations) {
			select.append(el('option', { value: c, textContent: c, selected: c === s.config.configuration }));
		}
		select.onchange = async () => {
			await saveConfig({ configuration: select.value });
			render();
		};
		return select;
	}

	function openTab(tab) {
		L.tab = tab;
		render();
	}

	async function saveConfig(changes) {
		const answer = await guarded(() => call('config', changes));
		if (answer) {
			await L.refresh();
		}
		return answer;
	}

	// Saves unsaved work first when the launch will move Studio to another folder (the page reloads there).
	async function readyToLeave() {
		const dirty = app().dirtyDocs().length + MS.ide.dirtyCount();
		if (!dirty) {
			return true;
		}
		return new Promise((resolve) => {
			MS.dialogs.open(
				'Save first?',
				el('p', { textContent: 'You have ' + dirty + ' unsaved file' + (dirty > 1 ? 's' : '') + '. Studio moves to the version you launch, so save them now to keep them.' }),
				[
					{ text: 'Cancel', action: () => resolve(false) },
					{
						text: 'Save and continue',
						primary: true,
						action: async () => {
							await app().save(true);
							if (MS.ide.dirtyCount()) {
								await MS.ide.saveAll();
							}
							const left = app().dirtyDocs().length + MS.ide.dirtyCount();
							resolve(left === 0);
						},
					},
				],
			);
		});
	}

	function follow(answer) {
		if (answer && answer.switched) {
			app().toast('Studio now edits ' + L.state.slot + '. Reloading…', 'good');
			MS.leaving = true;
			setTimeout(() => location.reload(), 900);
		}
	}

	L.launch = async function (mode) {
		// The top bar's Play button starts the way the game was last started, from its menu or the Game screen.
		try {
			localStorage.setItem('cortexStudio.lastLaunch', mode);
		} catch (e) {}
		await L.refresh();
		if (!L.state.editingSlot && !(await readyToLeave())) {
			return;
		}
		const answer = await guarded(() => call('launch', { mode, map: L.state.config.map || '' }));
		if (!answer) {
			return;
		}
		showLog('Launch', answer.log);
		app().toast(answer.log[answer.log.length - 1] || 'Launched.', 'good');
		follow(answer);
	};

	async function editIn(which) {
		if (!(await readyToLeave())) {
			return;
		}
		const answer = await guarded(() => call('edit', { which }));
		if (answer) {
			MS.leaving = true;
			location.reload();
		}
	}

	// ---- Versions (Advanced) ----

	async function renderVersions(body) {
		const s = L.state;
		if (!s.repoOk) {
			renderPlay(body);
			return;
		}
		const refSelect = el('select', { className: 'gm-input gm-wide' });
		const table = el('div', { className: 'gm-commits' }, el('p', { className: 'gm-dim', textContent: 'Reading the history…' }));
		const buildBtn = button('Build', () => buildPicked(false), { primary: true });
		const buildLaunch = button('Build & play', () => buildPicked(true));
		const head = card(
			null,
			row(
				el('label', { className: 'gm-inline' }, 'Branch, tag or version ', refSelect),
				button('⟳ Fetch', () => startJob('fetch', {}, async () => {
					L.refs = null;
					render();
				}), { title: 'Get the newest branches, tags and commits from ' + s.config.remote }),
				el('span', { className: 'spacer' }),
				el('label', { className: 'gm-inline' }, 'Build type ', configurationSelect()),
				buildBtn,
				buildLaunch,
				button('Delete build', deleteBuild, { title: 'Remove ' + s.slot + ' (the repo stays)' }),
			),
			el('p', { className: 'gm-dim' }, 'Built now: ', builtLine(), '. Pick a commit, then Build. Builds go in ', el('code', { textContent: s.slot }), ', so the next one is quick.'),
		);
		body.append(head, card(null, table));
		if (!L.refs) {
			const answer = await guarded(() => call('refs'));
			if (!answer) {
				return;
			}
			L.refs = answer.refs;
			if (!L.ref) {
				L.ref = s.config.lastRef && answer.refs.some((r) => r.ref === s.config.lastRef) ? s.config.lastRef : answer.default;
			}
		}
		for (const r of L.refs) {
			refSelect.append(el('option', { value: r.ref, textContent: r.name, selected: r.ref === L.ref }));
		}
		if (!L.refs.some((r) => r.ref === L.ref)) {
			refSelect.prepend(el('option', { value: L.ref, textContent: L.ref, selected: true }));
		}
		refSelect.onchange = () => {
			L.ref = refSelect.value;
			L.picked = '';
			saveConfig({ lastRef: L.ref });
			loadCommits(table);
		};
		loadCommits(table);
	}

	async function loadCommits(table) {
		table.innerHTML = '';
		table.append(el('p', { className: 'gm-dim', textContent: 'Reading the history…' }));
		const answer = await guarded(() => call('commits', undefined, { ref: L.ref }));
		if (!answer) {
			return;
		}
		L.commits = answer.commits;
		if (!L.picked && L.commits.length) {
			L.picked = L.commits[0].sha;
		}
		drawCommits(table);
		const missing = L.commits.map((c) => c.sha).filter((sha) => !(sha in L.versions));
		if (missing.length) {
			const versions = await guarded(() => call('versions', { shas: missing }));
			if (versions) {
				Object.assign(L.versions, versions);
				drawCommits(table);
			}
		}
	}

	function drawCommits(table) {
		table.innerHTML = '';
		const t = el('table', {}, el('tr', {}, el('th', { textContent: '' }), el('th', { textContent: 'Date' }), el('th', { textContent: 'Version' }), el('th', { textContent: 'Author' }), el('th', { textContent: 'Change' })));
		for (const c of L.commits) {
			const tr = el(
				'tr',
				{ className: c.sha === L.picked ? 'sel' : '', title: c.sha },
				el('td', { textContent: c.built ? '●' : '', className: 'gm-good', title: c.built ? 'Built' : '' }),
				el('td', { textContent: c.date }),
				el('td', { textContent: L.versions[c.sha] || '…' }),
				el('td', { textContent: c.author }),
				el('td', { textContent: c.subject }),
			);
			tr.onclick = () => {
				L.picked = c.sha;
				drawCommits(table);
			};
			tr.ondblclick = () => buildPicked(false);
			t.append(tr);
		}
		table.append(t);
	}

	async function buildPicked(thenPlay) {
		if (!L.picked) {
			app().toast('Pick a commit first.', 'bad');
			return;
		}
		await startJob('build', { sha: L.picked }, async (state) => {
			if (state === 'ok' && thenPlay) {
				await L.launch('play');
			}
		});
	}

	async function deleteBuild() {
		const answer = await guarded(() => call('delete-build', {}));
		if (answer) {
			app().toast(answer.ok ? 'Deleted the build.' : 'Some files couldn\'t be deleted (is the game still running?).', answer.ok ? 'good' : 'bad');
			await L.refresh();
			render();
		}
	}

	// ---- jobs (fetch, build, update) and their log ----

	async function startJob(action, body, after) {
		const answer = await guarded(() => call(action, body));
		if (!answer) {
			return;
		}
		L.afterJob = after || null;
		L.jobNext = 0;
		$('gameLog').innerHTML = '';
		pollJob();
	}

	async function pollJob() {
		if (L.jobPolling) {
			return;
		}
		L.jobPolling = true;
		const log = $('gameLog');
		$('gameLogWrap').hidden = false;
		$('gameCancel').hidden = false;
		$('gameCancel').onclick = () => call('cancel', {});
		let job;
		try {
			for (;;) {
				job = await call('job', undefined, { from: L.jobNext });
				$('gameLogTitle').textContent = job.title + (job.state === 'running' ? '…' : job.state === 'ok' ? ': done' : ': ' + job.state);
				const atEnd = log.scrollTop + log.clientHeight >= log.scrollHeight - 4;
				for (const line of job.lines) {
					log.append(el('div', { textContent: line, className: /\berror\b|failed|stopped with/i.test(line) ? 'gm-bad' : '' }));
				}
				if (atEnd) {
					log.scrollTop = log.scrollHeight;
				}
				L.jobNext = job.next;
				if (job.state !== 'running') {
					break;
				}
				await new Promise((r) => setTimeout(r, 500));
			}
		} catch (e) {
			app().toast(e.message, 'bad');
		}
		L.jobPolling = false;
		$('gameCancel').hidden = true;
		await L.refresh();
		if (!$('game').hidden && L.state) {
			render();
		}
		if (job) {
			app().toast(job.title + (job.state === 'ok' ? ': done.' : ': ' + job.state + '. The log has the details.'), job.state === 'ok' ? 'good' : 'bad');
		}
		const after = L.afterJob;
		L.afterJob = null;
		if (after && job) {
			await after(job.state);
		}
	}

	function showLog(title, lines) {
		$('gameLogWrap').hidden = false;
		$('gameLogTitle').textContent = title;
		const log = $('gameLog');
		log.innerHTML = '';
		for (const line of lines) {
			log.append(el('div', { textContent: line }));
		}
	}

	// ---- Launch settings ----

	function pathField(label, key, kind, note) {
		const s = L.state;
		const input = el('input', { type: 'text', value: s.config[key] || '', className: 'gm-input gm-wide', placeholder: 'Not set' });
		input.onchange = () => saveConfig({ [key]: input.value.trim() });
		const browse = button('Browse…', async () => {
			const answer = await guarded(() => call('pick', undefined, { kind, title: label }));
			if (answer && answer.path) {
				input.value = answer.path;
				await saveConfig({ [key]: answer.path });
			}
		});
		const clear = button('Clear', async () => {
			input.value = '';
			await saveConfig({ [key]: '' });
		});
		return el('div', { className: 'gm-field' }, el('label', { textContent: label }), row(input, browse, clear), note ? el('div', { className: 'gm-dim', textContent: note }) : null);
	}

	function renderSettings(body) {
		const s = L.state;
		const world = el('select', { className: 'gm-input' }, el('option', { value: '', textContent: 'Stock (the game\'s own)' }));
		for (const w of s.worlds) {
			world.append(el('option', { value: w, textContent: w, selected: w === s.config.world }));
		}
		world.onchange = () => saveConfig({ world: world.value });
		body.append(
			card(
				'Used every time you launch',
				pathField('Your Settings.ini', 'settingsIni', 'file', 'Copied into the version before each launch, so every version plays with your settings.'),
				pathField('Shared presets folder', 'presetsDir', 'folder', 'Linked as the version\'s Userdata/Presets, so loadouts and presets are the same in every version.'),
				pathField('Your mods folder', 'modsDir', 'folder', 'Each .rte in it is linked into the version\'s Mods before launching (changed files only).'),
				el('div', { className: 'gm-field' }, el('label', { textContent: 'World (planet skin)' }), world, el('div', { className: 'gm-dim', textContent: s.worlds.length ? 'Copied over the version\'s Data/Base.rte; Stock puts the game\'s own back.' : 'No worlds found. Put planet skin folders in CortexLauncher/Worlds (shared with the Windows launcher) or CortexStudio/Worlds.' })),
			),
		);
	}

	// ---- Setup ----

	function renderSetup(body) {
		const s = L.state;
		const steps = ['How you want it', 'What to install', 'The game\'s code'];
		const step = L.setupStep;
		body.append(el('div', { className: 'gm-steps' }, steps.map((t, i) => el('span', { className: i === step ? 'sel' : i < step ? 'done' : '', textContent: i + 1 + '. ' + t }))));
		const next = (n) => {
			L.setupStep = n;
			render();
		};
		if (step === 0) {
			const pick = async (mode) => {
				await saveConfig({ mode });
				next(1);
			};
			const choice = (mode, title, text) => {
				const b = el('button', { className: 'gm-choice' + (s.config.mode === mode ? ' sel' : '') }, el('b', { textContent: title }), el('span', { textContent: text }));
				b.onclick = () => pick(mode);
				return b;
			};
			body.append(
				card(
					'Simple or Advanced?',
					el('div', { className: 'gm-choices' },
						choice('simple', 'Simple', 'Studio keeps one branch of the game up to date: Update & build gets the newest version and builds it. Best for making mods.'),
						choice('advanced', 'Advanced', 'Pick any branch, tag, version or commit and build it, as in the Windows launcher. Best for testing mods on other game versions.'),
					),
					el('p', { className: 'gm-dim', textContent: 'You can change this any time here.' }),
				),
			);
		} else if (step === 1) {
			const list = el('div', {}, el('p', { className: 'gm-dim', textContent: 'Checking this computer…' }));
			body.append(card('What building the game needs', list), row(button('Back', () => next(0)), el('span', { className: 'spacer' }), button('Check again', () => drawDeps(list)), button('Next', () => next(2), { primary: true })));
			drawDeps(list);
		} else {
			const remote = el('input', { type: 'text', value: s.config.remote, className: 'gm-input' });
			remote.onchange = () => saveConfig({ remote: remote.value.trim() });
			const branch = el('input', { type: 'text', value: s.config.branch, className: 'gm-input' });
			branch.onchange = () => saveConfig({ branch: branch.value.trim() });
			const status = s.repoOk ? el('p', { className: 'gm-good', textContent: '✓ That folder is a git checkout.' }) : el('p', { className: 'gm-bad', textContent: s.config.repo ? '✗ That folder isn\'t a git checkout.' : 'Not set yet.' });
			body.append(
				card(
					'Where the game\'s code is',
					el('p', { textContent: 'A git clone of Cortex Command (git clone https://github.com/hotsake-uk/Cortex-Commander). Studio only fetches from it; versions are built in a separate folder next to it, so your own work in the clone is never touched.' }),
					pathField('Repo folder', 'repo', 'folder'),
					status,
					pathField('Versions folder', 'versionsDir', 'folder', 'Where built versions go. Empty means CortexVersions next to the repo. Studio uses CortexVersions/' + s.config.slot + ', so the Windows launcher\'s Current is left alone.'),
					row(el('label', { className: 'gm-inline' }, 'Remote ', remote), s.config.mode !== 'advanced' ? el('label', { className: 'gm-inline' }, 'Branch to keep up to date ', branch) : null),
				),
				row(button('Back', () => next(1)), el('span', { className: 'spacer' }), button('Done', async () => {
					await L.refresh();
					if (!L.state.repoOk) {
						app().toast('Pick a folder with a git checkout of the game first.', 'bad');
						return;
					}
					L.setupStep = 0;
					openTab('play');
				}, { primary: true })),
			);
		}
	}

	async function drawDeps(list) {
		list.innerHTML = '';
		list.append(el('p', { className: 'gm-dim', textContent: 'Checking this computer…' }));
		const deps = await guarded(() => call('deps'));
		if (!deps) {
			return;
		}
		list.innerHTML = '';
		const missing = deps.rows.filter((r) => !r.ok).length;
		list.append(el('p', { textContent: missing ? 'On ' + deps.platform + ', ' + missing + ' thing' + (missing > 1 ? 's are' : ' is') + ' missing. Run the command next to each in a terminal, then Check again.' : 'Everything is installed on this ' + deps.platform + ' computer.' }));
		const t = el('table', { className: 'gm-deps' });
		for (const r of deps.rows) {
			const fix = r.ok ? el('td', { className: 'gm-dim', textContent: r.found }) : el('td', {}, el('code', { textContent: r.fix }), ' ', button('Copy', async () => {
				await navigator.clipboard.writeText(r.fix).catch(() => {});
				app().toast('Copied.', 'good');
			}));
			t.append(el('tr', {}, el('td', { className: r.ok ? 'gm-good' : 'gm-bad', textContent: r.ok ? '✓' : '✗' }), el('td', {}, el('b', { textContent: r.name }), el('div', { className: 'gm-dim', textContent: r.neededFor })), fix));
		}
		list.append(t);
	}

	window.addEventListener('DOMContentLoaded', () => L.init());
})();
