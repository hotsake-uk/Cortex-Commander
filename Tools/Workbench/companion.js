// The Launch page: profiles, and the games started from them. Uses data, ask, text, toast, duration and refresh from the page's main script.
let meta = null, editing = null;
const WEATHER = ['Clear', 'Rain', 'Snow', 'Ash fall', 'Dust storm'];

async function loadMeta() {
	const fresh = await fetch('/api/meta').then((r) => r.json()).catch(() => null);
	if (fresh) meta = fresh;
}
function option(value, label, selected) { return `<option value="${text(value)}" ${selected ? 'selected' : ''}>${text(label ?? value)}</option>`; }

// ---------------------------------------------------------------- Launch

function profileChips(profile) {
	const chips = [profile.exe === 'debug' ? 'Debug build' : 'Game build'];
	chips.push(profile.start === 'activity' ? `${profile.activity || 'Sandbox'} on ${profile.scene || '?'}` : 'Main menu');
	if (profile.timeOfDay !== undefined) chips.push(`${profile.timeOfDay}:00`.replace(/\.5:00$/, ':30'));
	if (profile.weather !== undefined) chips.push(WEATHER[profile.weather] || 'Weather');
	if (profile.preset) chips.push('Preset: ' + profile.preset);
	if (profile.mods === 'none') chips.push('No mods');
	if (profile.mods === 'list') chips.push((profile.modList || []).length + ' mods');
	if (profile.tools || profile.graphicsLab || profile.worldDebug) chips.push('Settings panel open');
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
		${field('Preset', `<select id="pPreset">${option('', 'Your own settings', !profile.preset)}${data.presets.map((name) => option(name, name, name === profile.preset)).join('')}</select>`, 'Presets are saved from the settings panel in the game (F6).')}
		${field('Mods', `<select id="pMods">${option('', 'Your active mods', !profile.mods)}${option('none', 'None', profile.mods === 'none')}${option('list', 'Only the ones ticked below', profile.mods === 'list')}</select>`)}
		${field('Window size', `<span><input type="number" id="pWidth" min="320" max="7680" value="${profile.width ?? ''}" placeholder="yours" style="width: 90px"> x <input type="number" id="pHeight" min="240" max="4320" value="${profile.height ?? ''}" placeholder="yours" style="width: 90px"></span>`)}
		<label class="field"><span>Open on start</span><span><label><input type="checkbox" id="pTools" ${profile.tools || profile.graphicsLab || profile.worldDebug ? 'checked' : ''}> The settings panel (F6)</label></span></label>
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
			tools: $('#pTools').checked || undefined,
		};
		if (made.start !== 'activity') { delete made.activity; delete made.activityType; delete made.scene; }
		if (made.mods !== 'list') delete made.modList;
		const reply = await ask({ action: 'saveProfile', profile: made, original: editing.original });
		if (!reply.error) closeEditor();
	};
}
function closeEditor() { editing = null; $('#editor').style.display = 'none'; drawLaunch(); }

// ---------------------------------------------------------------- The games started from here

function drawGames() {
	const games = data.instances;
	put($('#games')).innerHTML = games.length ? '<h3>Started from here</h3>' + games.map((game) => {
		const state = game.state;
		const status = !game.running ? (game.abort ? 'crashed' : 'closed') : game.answering ? (state && state.inGame ? 'in game' : 'in menus') : 'loading';
		const pill = ({ 'in game': 'done', 'in menus': 'queued', loading: 'running', crashed: 'failed', closed: 'untested' })[status];
		const where = state && state.inGame ? `${state.activity} on ${state.scene}` : game.running && !game.answering ? 'Loading' + (game.loading ? ' ' + game.loading : '') + '. The window does not respond until it has finished; with many mods that takes a minute or two.' : '';
		const end = game.running ? '' : game.abort ? `<div class="problems-box">${text(game.abort)}</div>` : '';
		return `<div class="card" style="margin-bottom: 8px; ${game.running ? '' : 'opacity: .7'}"><div class="bar" style="margin: 0">
			<b>${text(game.profile)}</b><span class="muted">${game.exe === 'debug' ? 'debug build' : 'game build'} · #${game.id} · ${duration(game.seconds)}</span>
			<span class="pill ${pill}">${status}</span><span class="muted">${text(where)}</span><span class="grow"></span>
			${game.running ? `<button class="act small" data-game="${game.id}" data-game-do="quit" ${game.answering ? '' : 'disabled'}>Close game</button>
			<button class="act small" data-game="${game.id}" data-game-do="kill" title="For a game that has stopped answering.">Force stop</button>` : ''}</div>${end}</div>`;
	}).join('') : '';
}

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

document.addEventListener('click', async (event) => {
	const node = event.target;
	if (node.dataset.launch) ask({ action: 'launch', profile: node.dataset.launch });
	else if (node.dataset.edit) openEditor(data.profiles.find((profile) => profile.name === node.dataset.edit), node.dataset.edit);
	else if (node.dataset.copy) openEditor(Object.assign({}, data.profiles.find((profile) => profile.name === node.dataset.copy), { name: node.dataset.copy + ' copy' }), null);
	else if (node.dataset.delprofile) { if (await sure(`Delete the profile "${node.dataset.delprofile}"?`)) ask({ action: 'deleteProfile', profile: node.dataset.delprofile }); }
	else if (node.id === 'newProfile') openEditor({ exe: 'play', start: 'activity', activity: 'Sandbox', scene: 'Ketanot Hills' }, null);
	else if (node.id === 'resetProfiles') { if (await sure('Replace all profiles with the built-in ones?')) ask({ action: 'resetProfiles' }); }
	else if (node.dataset.gameDo) {
		const game = Number(node.dataset.game);
		if (node.dataset.gameDo === 'kill') { if (await sure('Force this game to stop? Anything unsaved in it is lost.')) ask({ action: 'gameCommand', instance: game, kind: 'kill' }); }
		else ask({ action: 'gameCommand', instance: game, kind: 'quit' });
	}
});
document.addEventListener('keydown', (event) => { if (event.key === 'Escape' && editing) closeEditor(); });

function drawCompanion() {
	if (!meta || (data.catalogueReady && !meta.scenes.length)) loadMeta();
	drawLaunch(); drawGames();
}
loadMeta();
