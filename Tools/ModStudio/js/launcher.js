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
		$('btnRun').oncontextmenu = (e) => {
			e.preventDefault();
			toggleRunMenu();
		};
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
			// Unlocked applies to the version it unlocked: Studio editing anything else keeps the usual locks.
			const unlocked = !!(state.config.unlocked && state.editingSlot);
			const button = $('btnGame');
			button.textContent = state.config.unlocked ? 'Game · 🔓 unlocked' : 'Game';
			button.classList.toggle('unlocked', !!state.config.unlocked);
			if (app() && app().unlocked !== unlocked) {
				app().unlocked = unlocked;
				if (app().lib) {
					app().refreshLocks();
				}
			}
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

	// Run configurations: saved ways to start the game (launcher.rs RunConfig), in the Play menu as "cfg:<name>".
	const runConfigs = () => (L.state && L.state.config.runConfigs) || [];
	const findRun = (id) => (id && id.startsWith('cfg:') ? runConfigs().find((c) => 'cfg:' + c.name === id) : null);
	function describeRun(c) {
		const what = (LAUNCHES.find((l) => l[0] === c.mode) || LAUNCHES[0])[1].replace('▶ ', '');
		return [what, (c.mode === 'test' || c.mode === 'sandbox') && c.map ? c.map : null, c.module ? 'only ' + c.module : null, c.timeScale ? c.timeScale + '× speed' : null, c.resolution || null].filter(Boolean).join(' · ');
	}

	function lastLaunch() {
		let mode = 'play';
		try {
			mode = localStorage.getItem('cortexStudio.lastLaunch') || 'play';
		} catch (e) {}
		const run = findRun(mode);
		if (run) {
			return [mode, '▶ ' + run.name, describeRun(run)];
		}
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
		if (runConfigs().length) {
			menu.append(el('div', { className: 'run-sep' }), el('div', { className: 'run-label', textContent: 'Saved' }));
			for (const c of runConfigs()) {
				const b = item(c.name, describeRun(c), () => L.launch('cfg:' + c.name), !s.builtExe);
				if ('cfg:' + c.name === last) {
					b.classList.add('current');
				}
				menu.append(b);
			}
		}
		menu.append(item('Run configurations…', 'Save ways to start it: a map, one mod only, faster, another window size', () => L.open('runs')));
		menu.append(el('div', { className: 'run-sep' }));
		if (s.config.workDir) {
			menu.append(item('⚒ Build my copy', 'Build ' + s.config.workDir + ' as it is now', () => L.open('play').then(() => startJob('update', {}))));
		} else if (s.repoOk && !advanced) {
			menu.append(item('⟳ Update & build', 'Fetch the newest ' + s.config.branch + ' and build it if it changed', () => L.open('play').then(() => startJob('update', {}))));
		}
		if (s.repoOk && advanced && !s.config.workDir) {
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
			['runs', 'Run configurations'],
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
		// The setup keeps its narrow column; the other screens use the window's width.
		body.classList.toggle('wide', L.tab !== 'setup');
		L.rendered = (L.rendered || 0) + 1;
		const view = { play: renderPlay, versions: renderVersions, settings: renderSettings, runs: renderRuns, setup: renderSetup }[L.tab] || renderPlay;
		view(body);
	}

	function builtLine() {
		const s = L.state;
		if (!s.builtExe) {
			// Built, but as another build type: the Launch buttons wait for this one, so say which and how to get it.
			const other = (s.built || '').split(' ')[1];
			return el('span', { className: 'gm-bad', textContent: other && other !== s.config.configuration ? 'No ' + s.config.configuration + ' build yet (this version is built as ' + other + '): press Update & build, or set Build type back to ' + other : 'Nothing built yet' });
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
		const own = !!s.config.workDir;
		const simple = s.config.mode !== 'advanced' || own;
		if (!s.repoOk && !own) {
			body.append(card('No game repo yet', el('p', { textContent: 'Cortex Studio builds the game from a git checkout of Cortex Command. Pick yours in Setup.' }), row(button('Open Setup', () => openTab('setup'), { primary: true }))));
			return;
		}
		const details = el('dl', { className: 'gm-facts' });
		const version = card(
			own ? 'Your own copy' : simple ? 'Your game' : 'The version in Studio\'s slot',
			own
				? el('p', {}, 'Built and played in ', el('code', { textContent: s.config.workDir }), ', with whatever is checked out there. ', builtLine())
				: el('p', {}, simple ? 'Keeps the newest ' : 'Built from the Versions tab. ', simple ? el('b', { textContent: s.config.remote + '/' + s.config.branch }) : null, simple ? ' built and ready. ' : '', builtLine()),
			details,
			row(
				own
					? button('⚒ Build my copy', () => startJob('update', {}), { primary: !s.builtExe, title: 'Build ' + s.config.workDir + ' as it is now' })
				: simple
					? button('⟳ Update & build', () => startJob('update', {}), { primary: !s.builtExe, disabled: !!s.config.unlocked, title: s.config.unlocked ? 'Unlocked: Reset, or Lock again, first' : 'Fetch the newest ' + s.config.branch + ' and build it if it changed' })
					: button('Pick a version…', () => openTab('versions')),
				el('label', { className: 'gm-inline' }, 'Build type ', configurationSelect()),
			),
			el('h4', { textContent: 'What Studio is editing' }),
			el('p', {}, el('code', { textContent: s.gameDir }), ' ', el('span', { className: 'gm-dim', textContent: s.editingSlot ? '(the built version)' : '(not the built version)' })),
			row(
				button('Edit the built version', () => editIn('slot'), { disabled: s.editingSlot || !s.builtExe }),
				button('Edit the repo checkout', () => editIn('repo')),
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
			el('p', { className: 'gm-dim', textContent: 'Launching applies your launch settings and points Studio at the version that starts, so saves, Preview in game and the console talk to that game.' }),
		);
		const using = card('Launch settings in use', el('p', { className: 'gm-dim', textContent: 'Reading…' }));
		body.append(el('div', { className: 'gm-grid' }, version, launches), unlockCard(), using);
		withOverview((o) => {
			fact(details, 'Commit', o.build.subject ? el('span', {}, el('code', { textContent: short(o.build.sha) }), ' ', o.build.subject) : null);
			fact(details, 'Version', o.build.version);
			fact(details, 'Committed', o.build.date ? when(o.build.date) + (o.build.author ? ' by ' + o.build.author : '') : null);
			fact(details, 'Built', o.build.exeModified ? when(o.build.exeModified) : null);
			fact(details, 'Game', el('code', { textContent: o.build.exe }));
			using.replaceChildren(el('h3', {}, 'Launch settings in use ', el('button', { className: 'ico-btn', textContent: 'Change…', onclick: () => openTab('settings') })), summaryStrip(o));
		});
	}

	// ---- Unlocked: change anything in place, Reset puts it back (launcher.rs) ----

	function unlockCard() {
		const s = L.state;
		const c = s.config;
		if (!c.unlocked) {
			const own = !!c.workDir;
			return card(
				'Unlock everything',
				el('p', { textContent: 'For messing about: the designer and the code editor change anything in the built version where it is, the game\'s own files in Data (Base.rte and the rest, Lua and INI) and every mod, with no copies and no Edit this mod.' }),
				el('p', { className: 'gm-dim' }, 'Studio first copies your mods folder ', c.modsDir ? el('code', { textContent: c.modsDir }) : '(none set)', ' into a dated backup and notes the commit the version is at, so Reset puts both back exactly.'),
				row(button('🔓 Unlock everything…', unlockAll, { disabled: own || !s.builtExe, title: own ? 'Your own copy is yours to manage with git' : !s.builtExe ? 'Build a version first' : '' })),
			);
		}
		const facts = el('dl', { className: 'gm-facts' });
		fact(facts, 'Since', c.unlockTime ? when(c.unlockTime) : null);
		fact(facts, 'Version', el('span', {}, el('code', { textContent: s.slot }), ' at ', el('code', { textContent: short(c.unlockCommit) })));
		fact(facts, 'Mods backup', c.unlockBackup ? el('code', { textContent: c.unlockBackup }) : 'None (no mods folder was set)');
		fact(facts, 'Of', c.unlockModsDir ? el('code', { textContent: c.unlockModsDir }) : null);
		return el('section', { className: 'gm-card gm-unlocked' },
			el('h3', { textContent: '🔓 Unlocked: everything can be changed in place' }),
			s.editingSlot ? null : el('p', { className: 'gm-bad', textContent: 'Studio is editing ' + s.gameDir + ', not the unlocked version, so the usual locks apply there. Press Edit the built version to change things in place.' }),
			facts,
			el('p', { className: 'gm-dim', textContent: 'Update & build waits until you Reset or Lock again, since it moves the version to another commit.' }),
			row(
				button('Reset…', resetAll, { primary: true, title: 'Put the version back at its commit and your mods folder back from the backup' }),
				button('Lock again, keep changes', lockAgain, { title: 'Back to the usual locks; your changes and the backup stay' }),
			),
		);
	}

	async function unlockAll() {
		if (!(await readyToLeave())) {
			return;
		}
		const o = await guarded(() => call('overview'));
		const mods = o && o.mods.exists ? o.mods.list : [];
		const bytes = mods.reduce((n, m) => n + m.bytes, 0);
		const go = await new Promise((resolve) => MS.dialogs.open('Unlock everything?', el('div', {},
			el('p', {}, 'Studio copies ', L.state.config.modsDir ? el('span', {}, el('code', { textContent: L.state.config.modsDir }), ' (' + mods.length + ' mods, ' + size(bytes) + ')') : 'nothing (no mods folder is set)', ' into a backup in CortexStudio/Backups, named with today\'s date and time, then unlocks the version in ', el('code', { textContent: L.state.slot }), '.'),
			el('p', { textContent: 'Saves then go straight into the game\'s own files and your mods (a version\'s mods are links to your mods folder, so they change there too). Reset puts it all back.' }),
		), [{ text: 'Cancel', action: () => resolve(false) }, { text: 'Back up and unlock', primary: true, action: () => resolve(true) }]));
		if (!go) {
			return;
		}
		app().toast('Backing up your mods…', '');
		const answer = await guarded(() => call('unlock', {}));
		if (!answer) {
			return;
		}
		app().toast(answer.backup ? 'Unlocked. Your mods (' + answer.files + ' files, ' + size(answer.bytes) + ') are backed up in ' + answer.backup + '.' : 'Unlocked.', 'good');
		if (answer.switched) {
			follow(answer);
			return;
		}
		await L.refresh();
		render();
	}

	async function lockAgain() {
		const answer = await guarded(() => call('lock', {}));
		if (!answer) {
			return;
		}
		app().toast('Locked again. Your changes stay' + (answer.backup ? '; the backup is still in ' + answer.backup : '') + '.', 'good');
		await L.refresh();
		render();
	}

	function changeList(title, rows) {
		const what = { changed: 'changed, goes back:', new: 'new, removed:', deleted: 'deleted, put back:' };
		const shown = rows.slice(0, 300);
		return el('div', {},
			el('h4', { textContent: title }),
			rows.length
				? el('div', { className: 'gm-changes' }, shown.map((r) => el('div', {}, el('span', { className: 'gm-dim', textContent: (what[r.what] || r.what) + '  ' }), r.path)), rows.length > shown.length ? el('div', { className: 'gm-dim', textContent: '…and ' + (rows.length - shown.length) + ' more' }) : null)
				: el('p', { className: 'gm-dim', textContent: 'Nothing changed.' }),
		);
	}

	async function resetAll() {
		const plan = await guarded(() => call('reset-plan'));
		if (!plan) {
			return;
		}
		const unsaved = app().dirtyDocs().length + MS.ide.dirtyCount();
		const total = plan.version.length + plan.mods.length + (plan.moved ? 1 : 0);
		const body = el('div', {},
			el('p', { textContent: total ? 'Everything below is thrown away for good. Studio\'s History doesn\'t keep it.' : 'Nothing has changed since you unlocked. Reset anyway?' }),
			plan.moved ? el('p', { className: 'gm-bad' }, 'The version has moved to ', el('code', { textContent: short(plan.head) }), ': it goes back to ', el('code', { textContent: short(plan.commit) }), '.') : null,
			changeList('The version, ' + L.state.slot + ', back to ' + short(plan.commit) + ' (' + plan.version.length + ')', plan.version),
			plan.backup ? changeList('Your mods folder, ' + plan.modsDir + ', back to the backup (' + plan.mods.length + ')', plan.mods) : null,
			plan.world ? el('p', { className: 'gm-dim', textContent: 'The world skin over Data/Base.rte goes too; the next launch puts it back.' }) : null,
			unsaved ? el('p', { className: 'gm-bad', textContent: 'Your ' + unsaved + ' unsaved file' + (unsaved > 1 ? 's go' : ' goes') + ' as well.' }) : null,
			plan.backup ? el('p', { className: 'gm-dim' }, 'The backup stays in ', el('code', { textContent: plan.backup }), ', and Studio stays unlocked.') : null,
		);
		const go = await new Promise((resolve) => MS.dialogs.open('Reset everything?', body, [{ text: 'Cancel', action: () => resolve(false) }, { text: total ? 'Throw ' + total + ' change' + (total > 1 ? 's' : '') + ' away and reset' : 'Reset', primary: true, action: () => resolve(true) }], true));
		if (!go) {
			return;
		}
		const answer = await guarded(() => call('reset', {}));
		if (!answer) {
			return;
		}
		showLog('Reset', answer.log);
		app().toast('Reset: the version and your mods are as they were. Reloading…', 'good');
		// Studio reads everything again, and what was unsaved goes with the old files.
		MS.leaving = true;
		setTimeout(() => location.reload(), 900);
	}

	// ---- the overview: details of the build and the launch settings, read on demand ----

	async function withOverview(draw) {
		const shown = L.rendered;
		const o = await guarded(() => call('overview'));
		if (o && shown === L.rendered) {
			draw(o);
		}
	}

	function fact(list, label, value) {
		if (value === null || value === undefined || value === '') {
			return;
		}
		list.append(el('dt', { textContent: label }), el('dd', {}, value));
	}

	function when(value) {
		// Seconds since 1970, or git's "2026-10-16 14:11:05 +0100".
		const d = typeof value === 'number' ? new Date(value * 1000) : new Date(String(value).replace(/^(\S+) (\S+) ([+-]\d\d)(\d\d)$/, '$1T$2$3:$4'));
		if (isNaN(d)) {
			return String(value);
		}
		const mins = Math.round((Date.now() - d) / 60000);
		const ago = mins < 1 ? 'just now' : mins < 60 ? mins + ' min ago' : mins < 48 * 60 ? Math.round(mins / 60) + ' h ago' : Math.round(mins / 1440) + ' days ago';
		return d.toLocaleString(undefined, { dateStyle: 'medium', timeStyle: 'short' }) + ' (' + ago + ')';
	}

	function size(bytes) {
		return bytes >= 1 << 30 ? (bytes / (1 << 30)).toFixed(1) + ' GB' : bytes >= 1 << 20 ? (bytes / (1 << 20)).toFixed(1) + ' MB' : Math.max(1, Math.round(bytes / 1024)) + ' KB';
	}

	function summaryStrip(o) {
		const s = L.state;
		const mods = o.mods.list;
		const count = (state) => mods.filter((m) => m.state === state).length;
		const tile = (label, main, sub, bad) => el('div', { className: 'gm-tile' }, el('span', { className: 'gm-dim', textContent: label }), el('b', { className: bad ? 'gm-bad' : '', textContent: main }), el('span', { className: 'gm-dim', textContent: sub || '' }));
		return el('div', { className: 'gm-tiles' },
			tile('Settings.ini', !s.config.settingsIni ? 'Not set' : o.settings.exists ? o.settings.values.length + ' settings' : 'Missing', o.settings.modified ? 'changed ' + when(o.settings.modified) : '', s.config.settingsIni && !o.settings.exists),
			tile('Presets', !s.config.presetsDir ? 'Not shared' : o.presets.exists ? o.presets.count + ' shared' : 'Missing', s.config.presetsDir ? 'same in every version' : 'each version keeps its own', s.config.presetsDir && !o.presets.exists),
			tile('Mods', !s.config.modsDir ? 'None' : o.mods.exists ? mods.length + ' mods' : 'Missing', mods.length ? count('linked') + ' linked, ' + count('new') + ' new, ' + count('own') + ' version\'s own' : '', s.config.modsDir && !o.mods.exists),
			tile('World', s.config.world || 'Stock', s.config.world ? 'over Data/Base.rte' : 'the game\'s own'),
		);
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
			const WORDS = { Final: 'Normal (recommended)', release: 'Normal (recommended)', 'Debug Release': 'Debug, full speed', 'Debug Minimal': 'Debug, some checks', 'Debug Full': 'Debug, every check (slow)', debug: 'Debug (for the game\'s programmers)' };
			select.append(el('option', { value: c, textContent: WORDS[c] || c, selected: c === s.config.configuration }));
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

	// A scenario started straight away on its own map (the Mission Editor's Play in the game, when no game from Studio is running).
	L.launchScenario = async function (name) {
		await L.refresh();
		if (!L.state.editingSlot && !(await readyToLeave())) {
			return;
		}
		if (app().project) {
			await MS.friendly.repairProject(app().project);
		}
		const answer = await guarded(() => call('launch', { mode: 'scenario', map: name }));
		if (!answer) {
			return;
		}
		showLog('Launch', answer.log);
		app().toast(answer.log[answer.log.length - 1] || 'Launched ' + name + '.', 'good');
		follow(answer);
	};

	L.launch = async function (mode) {
		// The top bar's Play button starts the way the game was last started, from its menu or the Game screen.
		try {
			localStorage.setItem('cortexStudio.lastLaunch', mode);
		} catch (e) {}
		await L.refresh();
		if (!L.state.editingSlot && !(await readyToLeave())) {
			return;
		}
		const run = findRun(mode);
		const body = run
			? { mode: run.mode || 'play', map: run.map || L.state.config.map || '', module: run.module || '', timeScale: run.timeScale || '', resolution: run.resolution || '' }
			: { mode, map: L.state.config.map || '' };
		// Files that no longer exist would stop the game loading the whole project mod: they come out of its list first.
		if (app().project) {
			await MS.friendly.repairProject(app().project);
		}
		// A test, sandbox or Actor Editor game starts with the unit that is open in the designer.
		if (['test', 'sandbox', 'editor'].includes(body.mode) && app().actor) {
			try {
				await app().save(true);
			} catch (e) {
				// the launch still goes
			}
			const text = app().previewText();
			if (text) {
				body.preview = text;
			}
		}
		const answer = await guarded(() => call('launch', body));
		if (!answer) {
			return;
		}
		showLog('Launch', answer.log);
		app().toast(answer.log[answer.log.length - 1] || 'Launched.', 'good');
		if (body.mode === 'test' || body.mode === 'sandbox') {
			MS.inspector.launched(!!answer.switched);
		}
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

	// ---- Run configurations ----

	function renderRuns(body) {
		const list = runConfigs().map((c) => Object.assign({}, c));
		const save = async () => {
			await saveConfig({ runConfigs: list });
			updateRunButton();
		};
		const mods = app() && app().lib ? app().lib.modules.filter((m) => !m.official).map((m) => m.name) : [];
		const rows = list.map((c, i) => {
			const input = (key, placeholder, width) => {
				const box = el('input', { type: 'text', value: c[key] || '', placeholder, className: 'gm-input', style: width ? 'width:' + width : '' });
				box.onchange = async () => {
					const v = box.value.trim();
					if (key === 'name' && (!v || list.some((o, j) => j !== i && o.name === v))) {
						app().toast(v ? 'There is already one called ' + v + '.' : 'It needs a name.', 'bad');
						box.value = c.name;
						return;
					}
					c[key] = v;
					await save();
					render();
				};
				return box;
			};
			const what = el('select', { className: 'gm-input' }, ...LAUNCHES.map(([id, label]) => el('option', { value: id, textContent: label.replace('▶ ', ''), selected: id === (c.mode || 'play') })));
			what.onchange = async () => {
				c.mode = what.value;
				await save();
				render();
			};
			const only = el('select', { className: 'gm-input', title: 'Load the game\'s own modules and only this mod (-module), to try it on its own' },
				el('option', { value: '', textContent: 'All my mods' }), ...mods.map((m) => el('option', { value: m, textContent: 'Only ' + m, selected: m === c.module })));
			only.onchange = async () => {
				c.module = only.value;
				await save();
			};
			const usesMap = c.mode === 'test' || c.mode === 'sandbox';
			return el('section', { className: 'gm-card run-card' },
				el('div', { className: 'gm-row' }, input('name', 'Name', '220px'), what, el('span', { className: 'spacer' }),
					button('▶ Run', () => L.launch('cfg:' + c.name), { primary: true, disabled: !L.state.builtExe }),
					button('Delete', async () => {
						list.splice(i, 1);
						await save();
						render();
					})),
				el('div', { className: 'run-grid' },
					el('label', {}, el('span', { textContent: 'Map' }), usesMap ? input('map', 'Maginot (the default)') : el('span', { className: 'gm-dim', textContent: 'Test and Sandbox take a map' })),
					el('label', {}, el('span', { textContent: 'Mods' }), only),
					el('label', {}, el('span', { textContent: 'Speed' }), input('timeScale', '1 (normal)'), el('small', { className: 'gm-dim', textContent: '2 runs twice as fast, for soak tests' })),
					el('label', {}, el('span', { textContent: 'Window size' }), input('resolution', 'Your settings (e.g. 1280x720)'))));
		});
		const add = button('+ New run configuration', async () => {
			let n = 1;
			while (list.some((c) => c.name === 'Run ' + n)) {
				n++;
			}
			list.push({ name: 'Run ' + n, mode: 'test', map: '', module: '', timeScale: '', resolution: '' });
			await save();
			render();
		}, { primary: !list.length });
		body.append(card('Run configurations',
			el('p', { className: 'gm-dim', textContent: 'Saved ways to start the game, in the ▶ Play menu. Each starts the built version with your launch settings, and the top bar\'s Play button repeats whichever you ran last.' }),
			row(add)), ...rows);
	}

	// ---- Launch settings ----

	function pathField(label, key, kind, note) {
		const s = L.state;
		const input = el('input', { type: 'text', value: s.config[key] || '', className: 'gm-input gm-wide', placeholder: 'Not set' });
		// Each change re-reads the overview, so the Settings.ini and mods lists follow the new paths.
		input.onchange = async () => {
			await saveConfig({ [key]: input.value.trim() });
			render();
		};
		const browse = button('Browse…', async () => {
			const answer = await guarded(() => call('pick', undefined, { kind, title: label }));
			if (answer && answer.path) {
				input.value = answer.path;
				await saveConfig({ [key]: answer.path });
				render();
			}
		});
		const clear = button('Clear', async () => {
			input.value = '';
			await saveConfig({ [key]: '' });
			render();
		});
		return el('div', { className: 'gm-field gm-field-row' }, el('label', { textContent: label }), el('div', {}, row(input, browse, clear), note ? el('div', { className: 'gm-dim', textContent: note }) : null));
	}

	function renderSettings(body) {
		const s = L.state;
		const world = el('select', { className: 'gm-input' }, el('option', { value: '', textContent: 'Stock (the game\'s own)' }));
		for (const w of s.worlds) {
			world.append(el('option', { value: w, textContent: w, selected: w === s.config.world }));
		}
		world.onchange = () => saveConfig({ world: world.value });
		const worldNote = el('div', { className: 'gm-dim', textContent: s.worlds.length ? 'Copied over the version\'s Data/Base.rte; Stock puts the game\'s own back.' : 'No worlds found. Put planet skin folders in CortexLauncher/Worlds (shared with the Windows launcher) or CortexStudio/Worlds.' });
		// The settings preset every game launched from Studio loads at start (a file in the Presets folder; the game's own "load at start").
		const preset = el('select', { className: 'gm-input' }, el('option', { value: '', textContent: 'None: Settings.ini as it is' }));
		preset.onchange = () => saveConfig({ defaultPreset: preset.value });
		const presetNote = el('div', { className: 'gm-dim', textContent: 'Loaded by the game over Settings.ini each time Studio starts it (Play, Test, Sandbox, Actor Editor).' });
		const settingsCard = card('Your Settings.ini', el('p', { className: 'gm-dim', textContent: 'Reading…' }));
		const modsCard = card('Your mods', el('p', { className: 'gm-dim', textContent: 'Reading…' }));
		body.append(
			el('div', { className: 'gm-grid' },
				card(
					'Used every time you launch',
					pathField('Settings.ini', 'settingsIni', 'file', 'Copied into the version before each launch, so every version plays with your settings.'),
					pathField('Presets folder', 'presetsDir', 'folder', 'Linked as the version\'s Userdata/Presets, so loadouts and presets are the same in every version.'),
					pathField('Mods folder', 'modsDir', 'folder', 'Each .rte in it is linked into the version\'s Mods before launching (changed files only).'),
					el('div', { className: 'gm-field gm-field-row' }, el('label', { textContent: 'Settings preset' }), el('div', {}, preset, presetNote)),
					el('div', { className: 'gm-field gm-field-row' }, el('label', { textContent: 'World' }), el('div', {}, world, worldNote)),
				),
				settingsCard,
			),
			modsCard,
		);
		withOverview((o) => {
			const names = (o.presets && o.presets.names) || [];
			const current = s.config.defaultPreset || '';
			for (const n of names.includes(current) || !current ? names : [current, ...names]) {
				preset.append(el('option', { value: n, textContent: n + (names.includes(n) ? '' : ' (not found)'), selected: n === current }));
			}
			preset.value = current;
			drawSettingsIni(settingsCard, o);
			drawMods(modsCard, o);
			worldNote.textContent += ' Worlds live in ' + o.worldsDir + '.';
		});
	}

	// A filter box over a table, for the long lists.
	function filtered(rows, placeholder, draw) {
		const box = el('input', { type: 'search', className: 'gm-input gm-filter', placeholder });
		const holder = el('div', { className: 'gm-table-wrap' });
		const redraw = () => {
			const q = box.value.trim().toLowerCase();
			holder.replaceChildren(draw(q ? rows.filter((r) => JSON.stringify(r).toLowerCase().includes(q)) : rows));
		};
		box.oninput = redraw;
		redraw();
		return [box, holder];
	}

	function drawSettingsIni(target, o) {
		const s = L.state;
		if (!s.config.settingsIni || !o.settings.exists) {
			target.replaceChildren(el('h3', { textContent: 'Your Settings.ini' }), el('p', { className: 'gm-dim', textContent: s.config.settingsIni ? 'That file doesn\'t exist.' : 'Not set: each version keeps its own Settings.ini.' }));
			return;
		}
		const [box, table] = filtered(o.settings.values, 'Filter ' + o.settings.values.length + ' settings…', (rows) =>
			el('table', { className: 'gm-table' }, rows.map(([k, v]) => el('tr', {}, el('td', { textContent: k }), el('td', { textContent: v }))))
		);
		target.replaceChildren(
			el('h3', { textContent: 'Your Settings.ini' }),
			el('div', { className: 'gm-row gm-tight' }, box, el('span', { className: 'gm-dim', textContent: 'Changed ' + when(o.settings.modified) })),
			table,
			el('p', { className: 'gm-dim', textContent: 'Read only here: change settings in the game, or edit the file. The game writes its changes back to this file.' }),
		);
	}

	function drawMods(target, o) {
		const s = L.state;
		const list = o.mods.list;
		if (!s.config.modsDir || !o.mods.exists) {
			target.replaceChildren(el('h3', { textContent: 'Your mods' }), el('p', { className: 'gm-dim', textContent: s.config.modsDir ? 'The mods folder doesn\'t exist.' : 'No mods folder set: the version only has the mods in its own Mods folder.' }));
			return;
		}
		const states = {
			linked: ['Linked', 'gm-good', 'In the built version; changed files are linked again on each launch'],
			new: ['On next launch', '', 'Not in the built version yet: linked when you next launch'],
			own: ['Version\'s own', 'gm-bad', 'The built version has its own copy, so yours is skipped'],
		};
		const total = list.reduce((n, m) => n + m.bytes, 0);
		const [box, table] = filtered(list, 'Filter ' + list.length + ' mods…', (rows) =>
			el('table', { className: 'gm-table' },
				el('tr', {}, ['Mod', 'Files', 'Size', 'Last changed', 'In the built version'].map((h) => el('th', { textContent: h }))),
				rows.map((m) => {
					const [label, cls, tip] = states[m.state] || [m.state, '', ''];
					return el('tr', {}, el('td', { textContent: m.name }), el('td', { className: 'gm-num', textContent: m.files }), el('td', { className: 'gm-num', textContent: size(m.bytes) }), el('td', { textContent: m.modified ? when(m.modified) : '' }), el('td', { className: cls, textContent: label, title: tip }));
				}),
			)
		);
		target.replaceChildren(
			el('h3', { textContent: 'Your mods (' + list.length + ', ' + size(total) + ')' }),
			el('div', { className: 'gm-row gm-tight' }, box, el('code', { textContent: s.config.modsDir })),
			table,
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
					pathField('My own copy (optional)', 'workDir', 'folder', 'A checkout of the game you are working on, such as a git worktree. When set, Build and Play use it where it is, with whatever you have checked out, and Studio never fetches into it, moves it or deletes it. Empty uses the branch build above.'),
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
