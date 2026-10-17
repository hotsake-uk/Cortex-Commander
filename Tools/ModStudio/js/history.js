// Cortex Studio: backups of a whole mod. Files save straight to your mods folder and Studio keeps no history of its own (use git or anything
// else beside it if you want one); what it does keep is a backup, a copy of a mod's whole folder taken whenever you ask, and it can put any
// backup back (after backing up how the mod is now, so a restore can be undone). Backups live in Studio's own folder, not in the mod.
'use strict';
window.MS = window.MS || {};

(function () {
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

	async function call(action, params, method) {
		const response = await fetch('/api/' + action + '?' + new URLSearchParams(params || {}).toString(), {
			method: method || 'GET',
			headers: { 'X-ModStudio-Token': app().fs.server },
			cache: 'no-store',
		});
		if (!response.ok) {
			throw new Error(await response.text());
		}
		return response;
	}

	const H = {};
	MS.history = H;

	H.available = () => !!(app().fs && app().fs.server);

	// 2026-10-17_153045 -> "17 Oct 2026, 15:30" (a name starting with before-restore- says so).
	function nice(name) {
		const m = /^(before-restore-)?(\d{4})-(\d\d)-(\d\d)_(\d\d)(\d\d)(\d\d)/.exec(name);
		if (!m) {
			return name;
		}
		const months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
		return (m[1] ? 'Before a restore · ' : '') + Number(m[4]) + ' ' + months[Number(m[3]) - 1] + ' ' + m[2] + ', ' + m[5] + ':' + m[6];
	}

	const size = (bytes) => (bytes > 1048576 ? (bytes / 1048576).toFixed(1) + ' MB' : Math.max(1, Math.round(bytes / 1024)) + ' KB');

	// Your mods: every module that isn't one of the game's own.
	function yourMods() {
		const a = app();
		return a.lib ? a.lib.modules.map((m) => m.name).filter((n) => !a.fs.isOfficial(n)).sort((x, y) => x.localeCompare(y)) : [];
	}

	// The Backups window. path (optional) is a file of the mod to start on.
	H.open = function (path) {
		const a = app();
		if (!H.available()) {
			a.toast('Backups need Cortex Studio running (not the file-only version).', 'bad');
			return;
		}
		const mods = yourMods();
		const startMod = a.project || (path ? path.split('/')[0] : a.actor ? a.actor.path.split('/')[0] : '');
		const pick = el('select', { className: 'ed-input' }, ...mods.map((m) => el('option', { value: m, textContent: m })));
		pick.value = mods.includes(startMod) ? startMod : mods[0] || '';
		const list = el('div', { className: 'bk-list' });
		const note = el('p', { className: 'small' });
		const make = el('button', { className: 'tb primary', textContent: 'Back up this mod now', onclick: async () => {
			if (!pick.value) {
				return;
			}
			make.disabled = true;
			try {
				// What Studio has changed but not saved goes to disk first, so the backup is what you are looking at.
				await a.save(true);
				await call('backup', { mod: pick.value }, 'POST');
				a.toast('Backed up ' + pick.value + '.', 'good');
			} catch (e) {
				a.toast('Couldn\'t back it up: ' + e.message, 'bad');
			}
			make.disabled = false;
			draw();
		} });

		async function draw() {
			list.replaceChildren(el('p', { className: 'small', textContent: 'Reading…' }));
			let answer;
			try {
				answer = await (await call('backups', { mod: pick.value })).json();
			} catch (e) {
				list.replaceChildren(el('p', { className: 'small', textContent: 'Couldn\'t read the backups: ' + e.message }));
				return;
			}
			note.textContent = answer.folder ? 'Backups are kept in ' + answer.folder + '.' : '';
			if (!answer.backups.length) {
				list.replaceChildren(el('p', { className: 'small', textContent: 'No backups of ' + pick.value + ' yet.' }));
				return;
			}
			list.replaceChildren(...answer.backups.map((b) => el('div', { className: 'bk-row' },
				el('div', { className: 'bk-name' }, el('b', { textContent: nice(b.name) }), el('small', { textContent: b.files + ' file' + (b.files === 1 ? '' : 's') + ', ' + size(b.bytes) })),
				el('button', { className: 'tb', textContent: 'Restore', title: 'Put ' + pick.value + ' back as it was then', onclick: () => confirmRestore(pick.value, b) }))));
		}

		function confirmRestore(module, b) {
			MS.dialogs.open('Put ' + module + ' back?', el('div', {},
				el('p', { textContent: module + ' goes back to how it was on ' + nice(b.name) + ': files changed since are put back, and files made since are taken out.' }),
				el('p', { className: 'small', textContent: 'Studio backs up how it is now first, so you can undo this from the same list. Studio reloads afterwards.' })), [
				{ text: 'Cancel', action: () => H.open(module + '/') },
				{
					text: 'Put it back',
					primary: true,
					action: async () => {
						try {
							await a.save(true);
							await call('backup-restore', { mod: module, name: b.name }, 'POST');
						} catch (e) {
							a.toast('Couldn\'t restore it: ' + e.message, 'bad');
							return;
						}
						MS.leaving = true;
						location.reload();
					},
				},
			]);
		}

		pick.onchange = draw;
		MS.dialogs.open('Backups', el('div', {},
			el('p', { textContent: 'A backup is a copy of a whole mod, taken when you ask. Studio saves your changes straight to your mod\'s files and keeps no history of them; if you want one, use git or any other tool alongside.' }),
			mods.length ? el('div', { className: 'bk-head' }, pick, make) : el('p', { className: 'small', textContent: 'No mods of yours yet.' }),
			list, note), [{ text: 'Close' }], true);
		if (mods.length) {
			draw();
		}
	};
})();
