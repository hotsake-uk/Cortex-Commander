// Cortex Studio: opening files in other programs. A setting (⚙ in the top bar) says, for each kind of file, which program opens it: Studio's
// own editor, a program on this computer (the ones installed in the usual places are offered), or the system's default. Opening a file of that
// kind anywhere in Studio (the sprite editor, a file in the explorer or the browser, a search result) then starts that program on it, through
// Studio's server (/api/open-with). Programs with ways of their own are used that way (the server knows them): Aseprite gets a sprite's frames
// as one animation, Visual Studio Code and Notepad++ open a script at its line. Saving in the other program shows in Studio as any change on
// disk does.
'use strict';
window.MS = window.MS || {};

(function () {
	const KEY = 'ms.external';
	const KINDS = [
		['image', 'Pictures', 'sprites: instead of Studio\'s sprite editor'],
		['script', 'Scripts', '.lua files: instead of Studio\'s script editor'],
		['ini', 'INI files', 'instead of Studio\'s INI editor'],
		['sound', 'Sounds', 'instead of playing them in Studio'],
	];
	const EXT_KIND = { png: 'image', bmp: 'image', lua: 'script', ini: 'ini', wav: 'sound', ogg: 'sound', flac: 'sound', mp3: 'sound' };
	const app = () => MS.app;

	function read() {
		try {
			return JSON.parse(localStorage.getItem(KEY) || '{}') || {};
		} catch (e) {
			return {};
		}
	}
	function write(all) {
		try {
			localStorage.setItem(KEY, JSON.stringify(all));
		} catch (e) {
			// kept for this run only
		}
	}

	// The program a kind of file opens in ({ program, name }), or null for Studio's own.
	const programFor = (kind) => read()[kind] || null;
	const kindOf = (path) => EXT_KIND[String(path || '').split('.').pop().toLowerCase()] || null;
	const wants = (kind) => !!programFor(kind);

	// Opens a file in its kind's program. True when it went there (so Studio's own editor stays shut). opts: { frames: [paths], line }.
	async function open(path, opts) {
		const kind = kindOf(path);
		const choice = kind && programFor(kind);
		if (!choice) {
			return false;
		}
		const a = app();
		const fs = a.fs;
		const body = { program: choice.program };
		if (opts && opts.line) {
			body.line = opts.line;
		}
		if (opts && opts.frames && opts.frames.length > 1) {
			body.frames = opts.frames.map((f) => fs.toDiskPath(f));
		}
		try {
			const response = await fs.api('POST', 'open-with', fs.toDiskPath(path), JSON.stringify(body));
			if (!response.ok) {
				throw new Error(await response.text());
			}
			a.status('Opened ' + path.split('/').pop() + ' in ' + choice.name + '. What you save there shows here. (⚙ Settings to change which program.)');
		} catch (e) {
			a.toast('Couldn\'t open it in ' + choice.name + ': ' + (e.message || e), 'bad');
		}
		return true;
	}

	// A sprite (its first frame, and the rest of its frames) in the picture program.
	function openSprite(file, frameCount) {
		const frames = MS.Images.framePaths(file, Math.max(1, frameCount || 1));
		return open(frames[0], { frames });
	}

	// ---- the setting ----

	let found = null;
	async function programsOnComputer() {
		if (!found) {
			try {
				const response = await app().fs.api('GET', 'find-programs', '');
				found = response.ok ? await response.json() : [];
			} catch (e) {
				found = [];
			}
		}
		return found;
	}

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

	async function dialog() {
		const programs = await programsOnComputer();
		const rows = KINDS.map(([kind, title, note]) => {
			const current = programFor(kind);
			const offered = programs.filter((p) => p.kinds.includes(kind));
			const options = [['', 'Studio'], ...offered.map((p) => [p.path, p.name]), ['default', 'The system\'s default program'], ['other', 'Another program…']];
			const known = !current || options.some(([v]) => v === current.program);
			const select = el('select', { className: 'ed-input' }, ...options.map(([v, t]) => el('option', { value: v, textContent: t, selected: current ? (known ? v === current.program : v === 'other') : v === '' })));
			const other = el('input', { type: 'text', className: 'ed-input', placeholder: 'The program\'s full path, e.g. C:\\Program Files\\Aseprite\\Aseprite.exe', value: current && !known ? current.program : '' });
			other.hidden = select.value !== 'other';
			const save = () => {
				const all = read();
				if (select.value === '') {
					delete all[kind];
				} else if (select.value === 'other') {
					const path = other.value.trim().replace(/^"|"$/g, '');
					if (path) {
						all[kind] = { program: path, name: path.split(/[\\/]/).pop().replace(/\.(exe|app)$/i, '') };
					} else {
						delete all[kind];
					}
				} else {
					all[kind] = { program: select.value, name: select.value === 'default' ? 'the system\'s default program' : select.options[select.selectedIndex].textContent };
				}
				write(all);
			};
			select.onchange = () => {
				other.hidden = select.value !== 'other';
				save();
				if (!other.hidden) {
					other.focus();
				}
			};
			other.onchange = save;
			return el('div', { className: 'ext-row' }, el('div', { className: 'ext-label' }, el('b', { textContent: title }), el('span', { textContent: note })), el('div', { className: 'ext-pick' }, select, other));
		});
		const hint = programs.length
			? 'Found on this computer: ' + programs.map((p) => p.name).join(', ') + '. Aseprite opens a sprite\'s frames as one animation (saving writes them back); Visual Studio Code and Notepad++ open a script at the line.'
			: 'No picture or code editors were found in the usual places: pick "Another program…" and give its path.';
		MS.dialogs.open('Settings: open files in other programs', el('div', { className: 'ext-settings' }, el('p', { className: 'ed-note', textContent: 'Each kind of file can open in a program of your choice instead of Studio\'s own editor. What you save there shows in Studio straight away.' }), ...rows, el('p', { className: 'ed-note', textContent: hint })), [{ text: 'Done', primary: true }]);
	}

	function mountButton() {
		const help = document.getElementById('btnHelp');
		if (!help || document.getElementById('btnSettings')) {
			return;
		}
		const button = el('button', { className: 'tb icon', id: 'btnSettings', title: 'Settings: open pictures, scripts and sounds in other programs (Aseprite, VS Code…)', textContent: '⚙', onclick: () => dialog() });
		help.parentNode.insertBefore(button, help);
	}
	if (document.readyState === 'loading') {
		document.addEventListener('DOMContentLoaded', mountButton);
	} else {
		setTimeout(mountButton, 0);
	}

	MS.external = { open, openSprite, wants, kindOf, programFor, dialog };
})();
