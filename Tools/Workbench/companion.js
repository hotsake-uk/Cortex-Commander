// The Launch, Live and Graphics pages. Uses data, ask, text, toast, duration and refresh from the page's main script.
let meta = null, editing = null, liveCards = new Map(), consoleFor = null;
let preset = { name: null, values: {}, saved: {}, liveTo: 0 };
const WEATHER = ['Clear', 'Rain', 'Snow', 'Ash fall', 'Dust storm'];

async function loadMeta() {
	const fresh = await fetch('/api/meta').then((r) => r.json()).catch(() => null);
	if (fresh) meta = fresh;
}
function option(value, label, selected) { return `<option value="${text(value)}" ${selected ? 'selected' : ''}>${text(label ?? value)}</option>`; }
function runningGames() { return data.instances.filter((game) => game.running); }

// ---------------------------------------------------------------- Launch

function profileChips(profile) {
	const chips = [profile.exe === 'debug' ? 'Debug build' : 'Game build'];
	chips.push(profile.start === 'activity' ? `${profile.activity || 'Sandbox'} on ${profile.scene || '?'}` : 'Main menu');
	if (profile.timeOfDay !== undefined) chips.push(`${profile.timeOfDay}:00`.replace(/\.5:00$/, ':30'));
	if (profile.weather !== undefined) chips.push(WEATHER[profile.weather] || 'Weather');
	if (profile.preset) chips.push('Look: ' + profile.preset);
	if (profile.mods === 'none') chips.push('No mods');
	if (profile.mods === 'list') chips.push((profile.modList || []).length + ' mods');
	if (profile.graphicsLab) chips.push('Graphics Lab');
	if (profile.worldDebug) chips.push('World Debug');
	return chips.map((chip) => `<span class="chip">${text(chip)}</span>`).join(' ');
}

function drawLaunch() {
	if (editing) return;
	put($('#profiles')).innerHTML = data.profiles.map((profile) => `<div class="card">
		<h2>${text(profile.name)}</h2><p>${text(profile.note || '')}</p>
		<div style="display: flex; gap: 6px; flex-wrap: wrap; margin-bottom: 12px">${profileChips(profile)}</div>
		<button class="act primary" data-launch="${text(profile.name)}">Launch</button>
		<button class="act" data-edit="${text(profile.name)}">Edit</button>
		<button class="act" data-copy="${text(profile.name)}">Copy</button>
		<button class="act" data-delprofile="${text(profile.name)}">Delete</button></div>`).join('');
}

function field(label, control, hint) { return `<label class="field"><span>${label}</span>${control}${hint ? `<small>${hint}</small>` : ''}</label>`; }

function openEditor(profile, original) {
	editing = { original };
	const activities = meta ? meta.activities : [], scenes = meta ? meta.scenes : [];
	const activityNames = activities.map((item) => item.name);
	if (profile.activity && !activityNames.includes(profile.activity)) activityNames.push(profile.activity);
	const sceneNames = [...scenes];
	if (profile.scene && !sceneNames.includes(profile.scene)) sceneNames.push(profile.scene);
	const mods = data.mods.filter((mod) => mod.where === 'active');
	$('#editor').innerHTML = `<div class="dialog"><h2>${original ? 'Edit profile' : 'New profile'}</h2>
		<div class="form">
		${field('Name', `<input type="text" id="pName" value="${text(profile.name || '')}">`)}
		${field('Description', `<input type="text" id="pNote" value="${text(profile.note || '')}">`)}
		${field('Program', `<select id="pExe">${option('play', 'Game build (the one you play)', profile.exe !== 'debug')}${option('debug', 'Debug build (slower, more checks)', profile.exe === 'debug')}</select>`)}
		${field('Starts at', `<select id="pStart">${option('menu', 'The main menu', profile.start !== 'activity')}${option('activity', 'Straight into a game', profile.start === 'activity')}</select>`)}
		${field('Game mode', `<select id="pActivity">${activityNames.map((name) => option(name, name, name === (profile.activity || 'Sandbox'))).join('')}</select>`, 'Used when starting straight into a game.')}
		${field('Map', `<select id="pScene">${sceneNames.map((name) => option(name, name, name === (profile.scene || 'Ketanot Hills'))).join('')}</select>`)}
		${field('Time of day', `<input type="number" id="pTime" min="0" max="24" step="0.5" value="${profile.timeOfDay ?? ''}" placeholder="yours">`, 'Hours, 0 to 24. Empty keeps your own setting.')}
		${field('Weather', `<select id="pWeather">${option('', 'Your own setting', profile.weather === undefined)}${WEATHER.map((name, index) => option(index, name, profile.weather === index)).join('')}</select>`)}
		${field('Graphics preset', `<select id="pPreset">${option('', 'Your own settings', !profile.preset)}${data.presets.map((item) => option(item.name, item.name, item.name === profile.preset)).join('')}</select>`)}
		${field('Mods', `<select id="pMods">${option('', 'Your active mods', !profile.mods)}${option('none', 'None', profile.mods === 'none')}${option('list', 'Only the ones ticked below', profile.mods === 'list')}</select>`)}
		${field('Window size', `<span><input type="number" id="pWidth" min="320" max="7680" value="${profile.width ?? ''}" placeholder="yours" style="width: 90px"> x <input type="number" id="pHeight" min="240" max="4320" value="${profile.height ?? ''}" placeholder="yours" style="width: 90px"></span>`)}
		<label class="field"><span>Open on start</span><span><label><input type="checkbox" id="pLab" ${profile.graphicsLab ? 'checked' : ''}> Graphics Lab</label> &nbsp; <label><input type="checkbox" id="pWorld" ${profile.worldDebug ? 'checked' : ''}> World Debug</label></span></label>
		</div>
		<div id="pModList" class="modlist">${mods.map((mod) => `<label><input type="checkbox" value="${text(mod.name)}" ${(profile.modList || []).includes(mod.name) ? 'checked' : ''}> ${text(mod.title || mod.name)}</label>`).join('')}</div>
		<div class="bar" style="margin: 16px 0 0"><span class="grow"></span><button class="act" id="pCancel">Cancel</button><button class="act primary" id="pSave">Save profile</button></div></div>`;
	$('#editor').style.display = 'flex';
	const showMods = () => { $('#pModList').style.display = $('#pMods').value === 'list' ? 'grid' : 'none'; };
	$('#pMods').addEventListener('change', showMods); showMods();
	$('#pCancel').onclick = closeEditor;
	$('#pSave').onclick = async () => {
		const number = (id) => $(id).value === '' ? undefined : Number($(id).value);
		const activity = $('#pActivity').value;
		const made = {
			name: $('#pName').value.trim(), note: $('#pNote').value.trim(), exe: $('#pExe').value, start: $('#pStart').value,
			activity, activityType: (activities.find((item) => item.name === activity) || {}).type || 'GAScripted', scene: $('#pScene').value,
			timeOfDay: number('#pTime'), weather: number('#pWeather'), preset: $('#pPreset').value || undefined, mods: $('#pMods').value || undefined,
			modList: [...document.querySelectorAll('#pModList input:checked')].map((box) => box.value), width: number('#pWidth'), height: number('#pHeight'),
			graphicsLab: $('#pLab').checked || undefined, worldDebug: $('#pWorld').checked || undefined,
		};
		if (made.start !== 'activity') { delete made.activity; delete made.activityType; delete made.scene; }
		if (made.mods !== 'list') delete made.modList;
		const reply = await ask({ action: 'saveProfile', profile: made, original: editing.original });
		if (!reply.error) closeEditor();
	};
}
function closeEditor() { editing = null; $('#editor').style.display = 'none'; drawLaunch(); }

// ---------------------------------------------------------------- Live

function gameControl(game, kind, extra) { return ask(Object.assign({ action: 'gameCommand', instance: game, kind }, extra)); }

function makeCard(game) {
	const card = document.createElement('div');
	card.className = 'card game';
	card.dataset.game = game.id;
	card.innerHTML = `<div class="bar" style="margin-bottom: 6px"><h2 style="margin: 0">${text(game.profile)} <span class="muted">· ${game.exe === 'debug' ? 'debug build' : 'game build'} · #${game.id}</span></h2><span class="grow"></span><span class="pill" data-f="status"></span></div>
		<div class="muted" data-f="where"></div>
		<div class="stats"><div><b data-f="fps">-</b><span>frames/s</span></div><div><b data-f="units">-</b><span>units</span></div><div><b data-f="particles">-</b><span>particles</span></div><div><b data-f="up">-</b><span>running</span></div></div>
		<div class="controls">
			<label>Time of day <output data-f="timeOut"></output><input type="range" min="0" max="24" step="0.25" data-set="TimeOfDay"></label>
			<label>Weather<select data-set="WeatherType">${WEATHER.map((name, index) => option(index, name)).join('')}</select></label>
			<label>Weather strength <output data-f="weatherOut"></output><input type="range" min="0" max="1" step="0.05" data-set="WeatherIntensity"></label>
			<label>Game speed <output data-f="speedOut"></output><input type="range" min="0.1" max="2" step="0.1" data-lua="TimerMan.TimeScale = VALUE"></label>
			<label>Zoom <output data-f="zoomOut"></output><input type="range" min="0.4" max="2" step="0.05" data-lua="FrameMan.CameraZoom = VALUE"></label>
			<label>Look<select data-preset><option value="">Apply a preset...</option></select></label>
		</div>
		<div class="bar" style="margin: 12px 0 0">
			<button class="act small" data-game-do="shot">Screenshot</button>
			<button class="act small" data-game-lua="DebugMan:ShowGraphicsLab()">Graphics Lab</button>
			<button class="act small" data-game-do="savePreset">Save look as preset...</button>
			<button class="act small" data-game-do="console">Console</button>
			<span class="grow"></span>
			<button class="act small" data-game-do="quit">Close game</button>
			<button class="act small" data-game-do="kill" title="For a game that has stopped answering.">Force stop</button>
		</div>
		<div class="ended" data-f="ended"></div>`;
	return card;
}

function drawLive() {
	const box = $('#games');
	const ids = new Set(data.instances.map((game) => game.id));
	for (const [id, card] of liveCards) if (!ids.has(id)) { card.remove(); liveCards.delete(id); }
	$('#noGames').style.display = data.instances.length ? 'none' : 'block';
	for (const game of data.instances) {
		let card = liveCards.get(game.id);
		if (!card) { card = makeCard(game); liveCards.set(game.id, card); box.prepend(card); }
		const set = (name, value) => { const node = card.querySelector(`[data-f="${name}"]`); if (node && node.textContent !== String(value)) node.textContent = value; };
		const state = game.state;
		const status = !game.running ? (game.abort ? 'crashed' : 'closed') : game.answering ? (state && state.inGame ? 'in game' : 'in menus') : 'loading';
		const pill = card.querySelector('[data-f="status"]');
		pill.textContent = status;
		pill.className = 'pill ' + ({ 'in game': 'done', 'in menus': 'queued', loading: 'running', crashed: 'failed', closed: 'untested' })[status];
		set('where', state && state.inGame ? `${state.activity} on ${state.scene}${state.paused ? ' (paused)' : ''}` : game.running ? (game.answering ? 'In the menus' : 'Loading' + (game.loading ? ' ' + game.loading : '') + '... The window does not respond until it has finished; with many mods that takes a minute or two.') : '');
		set('fps', state ? state.fps : '-'); set('units', state ? state.units : '-'); set('particles', state ? state.particles : '-'); set('up', duration(game.seconds));
		card.classList.toggle('over', !game.running);
		card.querySelectorAll('.controls input, .controls select, [data-game-do="shot"], [data-game-lua], [data-game-do="savePreset"], [data-game-do="quit"], [data-game-do="kill"]').forEach((node) => { node.disabled = !game.running || !game.answering && node.dataset.gameDo !== 'kill'; });
		if (state) {
			const sync = (selector, value, out, shown) => { const node = card.querySelector(selector); if (node && document.activeElement !== node && !node.dataset.held) node.value = value; if (out) set(out, shown); };
			sync('[data-set="TimeOfDay"]', state.timeOfDay, 'timeOut', `${Math.floor(state.timeOfDay)}:${String(Math.round((state.timeOfDay % 1) * 60)).padStart(2, '0')}`);
			sync('[data-set="WeatherType"]', state.weatherType);
			sync('[data-set="WeatherIntensity"]', state.weatherIntensity, 'weatherOut', Math.round(state.weatherIntensity * 100) + '%');
			sync('[data-lua^="TimerMan"]', state.simSpeed, 'speedOut', state.simSpeed.toFixed(1) + 'x');
			sync('[data-lua^="FrameMan"]', state.zoom, 'zoomOut', state.zoom.toFixed(2) + 'x');
		}
		const presetSelect = card.querySelector('[data-preset]');
		if (presetSelect.options.length !== data.presets.length + 1) presetSelect.innerHTML = '<option value="">Apply a preset...</option>' + data.presets.map((item) => option(item.name)).join('');
		card.querySelector('[data-f="ended"]').innerHTML = game.running ? '' : (game.abort ? `<b>It stopped with an error:</b><div class="problems-box">${text(game.abort)}</div>` : `<span class="muted">Closed (exit code ${game.exitCode}).</span>`);
	}
	drawConsole();
}

async function drawConsole() {
	const panel = $('#consolePanel');
	if (consoleFor === null || !data.instances.some((game) => game.id === consoleFor)) { panel.style.display = 'none'; consoleFor = null; return; }
	panel.style.display = 'block';
	$('#consoleTitle').textContent = 'Console of game #' + consoleFor;
	const reply = await fetch('/api/console?instance=' + consoleFor).then((r) => r.json()).catch(() => null);
	if (!reply) return;
	const box = $('#consoleLog'), atBottom = box.scrollTop + box.clientHeight >= box.scrollHeight - 30;
	box.textContent = reply.lines.join('\n');
	if (atBottom) box.scrollTop = box.scrollHeight;
}

// ---------------------------------------------------------------- Graphics

async function selectPreset(name) {
	const reply = await fetch('/api/preset?name=' + encodeURIComponent(name)).then((r) => r.json()).catch(() => ({ error: 'The Workbench is not answering.' }));
	if (reply.error) return toast(reply.error);
	preset.name = name; preset.values = Object.assign({}, reply.values); preset.saved = Object.assign({}, reply.values);
	drawPresetList(); drawEditor();
}

function drawPresetList() {
	const games = runningGames();
	const target = $('#liveTo');
	const wanted = '<option value="0">Not connected to a game</option>' + games.map((game) => option(game.id, `Game #${game.id}: ${game.profile}`)).join('');
	if (target.dataset.made !== wanted) { target.innerHTML = wanted; target.dataset.made = wanted; target.value = games.some((game) => game.id === preset.liveTo) ? preset.liveTo : (games[0] ? games[0].id : 0); preset.liveTo = Number(target.value); }
	put($('#presetList')).innerHTML = data.presets.map((item) => `<div class="preset ${item.name === preset.name ? 'sel' : ''}" data-preset-name="${text(item.name)}"><b>${text(item.name)}</b><div class="sub">${text(item.note || '')}</div></div>`).join('') || '<p class="muted">No presets yet.</p>';
	const changed = preset.name && Object.keys(preset.values).some((key) => preset.values[key] !== preset.saved[key]);
	$('#presetTitle').textContent = preset.name ? preset.name + (changed ? ' (changed)' : '') : 'Pick a preset on the left';
	document.querySelectorAll('[data-preset-do]').forEach((button) => {
		const needsGame = ['apply', 'pull'].includes(button.dataset.presetDo), needsPreset = button.dataset.presetDo !== 'new';
		button.disabled = (needsPreset && !preset.name) || (needsGame && !preset.liveTo) || (button.dataset.presetDo === 'save' && !changed);
	});
}

function colourToHex(value) {
	// Linear light 0..1 (may go above 1) to a display colour for the picker.
	return '#' + value.split(/\s+/).slice(0, 3).map((part) => Math.round(Math.min(Math.max(Math.pow(Math.max(Number(part), 0), 1 / 2.2), 0), 1) * 255).toString(16).padStart(2, '0')).join('');
}

function drawEditor() {
	const box = $('#presetEditor');
	if (!preset.name || !meta) { box.innerHTML = ''; return; }
	const groups = new Map();
	for (const key of Object.keys(preset.values)) {
		const info = meta.graphics[key] || { label: key, group: 'Other', kind: 'text' };
		if (!groups.has(info.group)) groups.set(info.group, []);
		groups.get(info.group).push([key, info]);
	}
	let html = '';
	for (const [group, items] of groups) {
		html += `<h3>${text(group)}</h3><div class="settings">`;
		for (const [key, info] of items) {
			const value = preset.values[key];
			let control;
			if (info.kind === 'switch') control = `<input type="checkbox" data-key="${key}" ${Number(value) ? 'checked' : ''}>`;
			else if ((info.kind === 'number' || info.kind === 'whole') && info.max !== undefined) {
				const step = info.kind === 'whole' ? 1 : (info.max - info.min) / 200;
				control = `<input type="range" data-key="${key}" min="${info.min}" max="${Math.max(info.max, Number(value))}" step="${step}" value="${Number(value)}"><input type="number" data-key="${key}" data-twin="1" step="${step}" value="${Number(value)}">`;
			} else if (info.kind === 'colour') {
				const parts = value.split(/\s+/);
				control = `<span class="colour"><i style="background: ${colourToHex(value)}"></i>${[0, 1, 2].map((index) => `<input type="number" data-key="${key}" data-part="${index}" step="0.01" min="0" value="${Number(parts[index] ?? 0)}">`).join('')}</span>`;
			} else control = `<input type="text" data-key="${key}" value="${text(value)}">`;
			html += `<label class="setting ${value !== preset.saved[key] ? 'changed' : ''}" title="${key}"><span>${text(info.label)}</span>${control}</label>`;
		}
		html += '</div>';
	}
	box.innerHTML = html;
}

let sendTimer = null, pendingSend = {};
function changeSetting(key, value) {
	preset.values[key] = value;
	if (preset.liveTo) {
		// Sliders fire many times a second; the game is sent the newest value of each setting a few times a second.
		pendingSend[key] = value;
		if (!sendTimer) sendTimer = setTimeout(() => {
			const lines = Object.entries(pendingSend); pendingSend = {}; sendTimer = null;
			for (const [name, newest] of lines) fetch('/api/do', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ action: 'gameCommand', instance: preset.liveTo, kind: 'set', key: name, value: newest }) });
		}, 120);
	}
	drawPresetList();
}

document.addEventListener('input', (event) => {
	const node = event.target;
	if (node.closest('#presetEditor') && node.dataset.key) {
		const key = node.dataset.key;
		let value;
		if (node.type === 'checkbox') value = node.checked ? '1' : '0';
		else if (node.dataset.part !== undefined) {
			const parts = preset.values[key].split(/\s+/); parts[Number(node.dataset.part)] = String(Number(node.value) || 0); value = parts.join(' ');
			node.parentElement.querySelector('i').style.background = colourToHex(value);
		} else {
			value = node.type === 'text' ? node.value : String(Number(node.value));
			const twin = node.parentElement.querySelector(node.dataset.twin ? 'input[type=range]' : 'input[data-twin]');
			if (twin) twin.value = node.value;
		}
		node.closest('.setting').classList.toggle('changed', value !== preset.saved[key]);
		changeSetting(key, value);
	} else if (node.closest('.game') && (node.dataset.set || node.dataset.lua) && node.type === 'range') {
		node.dataset.held = '1';
		clearTimeout(node.holdTimer); node.holdTimer = setTimeout(() => delete node.dataset.held, 1500);
		const game = Number(node.closest('.game').dataset.game);
		clearTimeout(node.sendTimer);
		node.sendTimer = setTimeout(() => node.dataset.set ? gameControl(game, 'set', { key: node.dataset.set, value: node.value }) : gameControl(game, 'lua', { code: node.dataset.lua.replace('VALUE', node.value) }), 80);
	}
});

document.addEventListener('change', (event) => {
	const node = event.target;
	if (node.closest('.game') && node.tagName === 'SELECT') {
		const game = Number(node.closest('.game').dataset.game);
		if (node.dataset.set) gameControl(game, 'set', { key: node.dataset.set, value: node.value });
		else if (node.dataset.preset !== undefined && node.value) { ask({ action: 'applyPreset', name: node.value, instance: game }); node.value = ''; }
	} else if (node.id === 'liveTo') { preset.liveTo = Number(node.value); drawPresetList(); }
});

// Questions are asked in a box on the page itself. (The browser's own pop-ups don't exist in every browser this page is opened in.)
function dialog(question, withInput, suggestion) {
	return new Promise((resolve) => {
		const box = $('#question');
		box.innerHTML = `<div class="dialog" style="width: min(460px, 92vw)"><p style="margin: 0 0 12px">${text(question)}</p>
			${withInput ? `<input type="text" id="answer" value="${text(suggestion || '')}" style="width: 100%">` : ''}
			<div class="bar" style="margin: 16px 0 0"><span class="grow"></span><button class="act" id="answerNo">Cancel</button><button class="act primary" id="answerYes">${withInput ? 'Save' : 'Yes'}</button></div></div>`;
		box.style.display = 'flex';
		const finish = (value) => { box.style.display = 'none'; box.innerHTML = ''; document.removeEventListener('keydown', keys, true); resolve(value); };
		const yes = () => finish(withInput ? ($('#answer').value.trim() || null) : true);
		const keys = (event) => { if (event.key === 'Enter') { event.preventDefault(); yes(); } else if (event.key === 'Escape') finish(withInput ? null : false); };
		document.addEventListener('keydown', keys, true);
		$('#answerYes').onclick = yes;
		$('#answerNo').onclick = () => finish(withInput ? null : false);
		if (withInput) { $('#answer').focus(); $('#answer').select(); } else $('#answerYes').focus();
	});
}
function sure(question) { return dialog(question, false); }
function namePreset(question, suggestion) { return dialog(question, true, suggestion); }

document.addEventListener('click', async (event) => {
	const node = event.target;
	if (node.dataset.launch) { const reply = await ask({ action: 'launch', profile: node.dataset.launch }); if (reply.instance) { document.querySelector('nav button[data-tab="live"]').click(); } }
	else if (node.dataset.edit) openEditor(data.profiles.find((profile) => profile.name === node.dataset.edit), node.dataset.edit);
	else if (node.dataset.copy) openEditor(Object.assign({}, data.profiles.find((profile) => profile.name === node.dataset.copy), { name: node.dataset.copy + ' copy' }), null);
	else if (node.dataset.delprofile) { if (await sure(`Delete the profile "${node.dataset.delprofile}"?`)) ask({ action: 'deleteProfile', profile: node.dataset.delprofile }); }
	else if (node.id === 'newProfile') openEditor({ exe: 'play', start: 'activity', activity: 'Sandbox', scene: 'Ketanot Hills' }, null);
	else if (node.id === 'resetProfiles') { if (await sure('Replace all profiles with the built-in ones?')) ask({ action: 'resetProfiles' }); }
	else if (node.dataset.gameLua) gameControl(Number(node.closest('.game').dataset.game), 'lua', { code: node.dataset.gameLua });
	else if (node.dataset.gameDo) {
		const game = Number(node.closest('.game').dataset.game), action = node.dataset.gameDo;
		if (action === 'console') { consoleFor = consoleFor === game ? null : game; drawConsole(); }
		else if (action === 'savePreset') {
			const name = await namePreset('Save this game\'s current look as a preset called:');
			if (name) { let reply = await ask({ action: 'savePreset', name, instance: game }); if (reply.exists && await sure(`Replace the preset "${name}"?`)) reply = await ask({ action: 'savePreset', name, instance: game, overwrite: true }); }
		} else if (action === 'kill') { if (await sure('Force this game to stop? Anything unsaved in it is lost.')) gameControl(game, 'kill'); }
		else { const reply = await gameControl(game, action); if (action === 'shot' && reply.ok) toast('Saved to the ScreenShots folder.'); }
	}
	else if (node.closest('[data-preset-name]')) {
		const name = node.closest('[data-preset-name]').dataset.presetName;
		const changed = preset.name && Object.keys(preset.values).some((key) => preset.values[key] !== preset.saved[key]);
		if (!changed || await sure(`Leave "${preset.name}" without saving your changes?`)) selectPreset(name);
	}
	else if (node.dataset.presetDo) {
		const action = node.dataset.presetDo;
		if (action === 'apply') await applyEdited();
		else if (action === 'save') { const reply = await ask({ action: 'savePreset', name: preset.name, values: preset.values, overwrite: true, note: (data.presets.find((item) => item.name === preset.name) || {}).note }); if (!reply.error) { preset.saved = Object.assign({}, preset.values); drawEditor(); drawPresetList(); } }
		else if (action === 'saveAs' || action === 'new') {
			const name = await namePreset(action === 'new' ? 'Name for a new preset, started from your saved settings:' : 'Save these settings as a new preset called:', action === 'new' ? '' : preset.name + ' 2');
			if (!name) return;
			const reply = await ask(action === 'new' ? { action: 'savePreset', name } : { action: 'savePreset', name, values: preset.values });
			if (!reply.error) { await refresh(); selectPreset(name); }
		}
		else if (action === 'pull') { const reply = await fetch('/api/graphics?instance=' + preset.liveTo).then((r) => r.json()); if (reply.error) return toast(reply.error); for (const key of Object.keys(preset.values)) if (reply.values[key] !== undefined) preset.values[key] = reply.values[key]; drawEditor(); drawPresetList(); }
		else if (action === 'rename') { const name = await namePreset('New name:', preset.name); if (name && name !== preset.name) { const reply = await ask({ action: 'renamePreset', name: preset.name, to: name }); if (!reply.error) { preset.name = name; await refresh(); } } }
		else if (action === 'delete') { if (await sure(`Delete the preset "${preset.name}"?`)) { await ask({ action: 'deletePreset', name: preset.name }); preset.name = null; preset.values = {}; drawEditor(); } }
		else if (action === 'default') { if (await sure(`Write "${preset.name}" into your Settings.ini, so the game starts with this look?`)) { const reply = await ask({ action: 'defaultPreset', name: preset.name }); if (reply.ok) toast('Done. The game will start with this look.'); } }
	}
	else if (node.id === 'consoleSend') sendConsole();
});

async function applyEdited() {
	// What is on screen in the editor, saved or not, goes to the game.
	const lines = Object.entries(preset.values);
	for (let index = 0; index < lines.length; index += 20) {
		await Promise.all(lines.slice(index, index + 20).map(([key, value]) => fetch('/api/do', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ action: 'gameCommand', instance: preset.liveTo, kind: 'set', key, value }) })));
	}
	toast('Sent to game #' + preset.liveTo + '.');
}

async function sendConsole() {
	const input = $('#consoleInput');
	if (!input.value.trim() || consoleFor === null) return;
	await gameControl(consoleFor, 'lua', { code: input.value });
	input.value = '';
	drawConsole();
}
document.addEventListener('keydown', (event) => { if (event.key === 'Enter' && event.target.id === 'consoleInput') sendConsole(); if (event.key === 'Escape' && editing) closeEditor(); });

function drawCompanion() {
	if (!meta || (data.catalogueReady && !meta.scenes.length)) loadMeta();
	drawLaunch(); drawLive(); drawPresetList();
	const live = document.querySelector('nav button[data-tab="live"]');
	const count = runningGames().length;
	live.textContent = count ? `Live (${count})` : 'Live';
}
loadMeta();
