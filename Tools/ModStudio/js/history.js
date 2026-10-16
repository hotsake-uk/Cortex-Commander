// Cortex Mod Studio: your mods' history. Studio's server keeps every version of a file it changes in a mod (Cortex Studio with git, in
// CortexStudio/History; Mod Studio in a browser as plain copies), starting with the mod as it was before Studio first changed it.
// This is the History window (every file Studio changed, each version with what it changed, and Restore), Undo for the last save,
// and Edit this mod, which lets the designer change a mod Studio didn't make.
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

	// The mods you chose to edit (they have a history). Called once the mods are read.
	H.loadEditable = async function () {
		if (!H.available()) {
			return;
		}
		try {
			const names = await (await call('history', { mods: 1 })).json();
			app().lib.editable = new Set(names.map((n) => n.toLowerCase()));
		} catch (e) {
			app().lib.editable = null; // An older server: nothing is locked beyond the game's own files, as before.
		}
	};

	// Edit this mod: Studio keeps the mod as it is now, then the designer can change it.
	H.editMod = async function (module) {
		try {
			await call('history-start', { path: 'Mods/' + module }, 'POST');
		} catch (e) {
			app().toast('Couldn\'t start editing ' + module + ': ' + e.message, 'bad');
			return false;
		}
		app().lib.editable = app().lib.editable || new Set();
		app().lib.editable.add(module.toLowerCase());
		app().toast('You can change ' + module + ' now. Studio kept it as it was: History puts any file back.', 'good');
		app().refreshLocks();
		return true;
	};

	const diskOf = (path) => app().fs.toDiskPath(path);
	const when = (seconds) => new Date(seconds * 1000).toLocaleString(undefined, { dateStyle: 'medium', timeStyle: 'medium' });

	async function versionsOf(disk) {
		return await (await call('history', { key: disk })).json();
	}

	// A version's bytes as kept (restores write them back exactly, whatever the file's encoding); decoded only to show it.
	async function versionBytes(disk, id) {
		return new Uint8Array(await (await call('history-file', { key: disk, id })).arrayBuffer());
	}

	const decode = (bytes) => (bytes === null ? null : new TextDecoder().decode(bytes));
	const sameBytes = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);

	async function currentText(disk) {
		try {
			return await (await app().fs.readFile(disk)).text();
		} catch (e) {
			return null;
		}
	}

	// Puts a version back: a normal save (so it is a version too, and Restore can be undone), then Studio and the game read it again.
	async function restore(disk, bytes) {
		const path = disk.replace(/^(Data|Mods)\//i, '');
		const doc = app().lib.docFor(path);
		if (doc && doc.dirty) {
			app().lib.discard(doc);
		}
		if (bytes === null) {
			await app().fs.api('POST', 'delete', disk);
		} else {
			await app().fs.writeDisk(disk, bytes);
		}
		await MS.ide.reloadFromDisk([{ path: disk, kind: bytes === null ? 'deleted' : 'changed' }], 'restored from History');
		const ext = path.split('.').pop().toLowerCase();
		MS.ide.requestScripts(ext === 'lua' ? { scripts: [path] } : ext === 'ini' ? { files: [path] } : {});
	}

	// Undo for a save: each file goes back to the newest version that differs from what the save wrote (or is deleted, if the save
	// made it).
	H.undoSave = async function (saved) {
		let undone = 0;
		for (const { path, text, isNew } of saved) {
			const disk = diskOf(path);
			const written = new TextEncoder().encode(text);
			try {
				let previous;
				for (const v of await versionsOf(disk)) {
					if (v.deleted) {
						continue;
					}
					const bytes = await versionBytes(disk, v.id);
					if (!sameBytes(bytes, written)) {
						previous = bytes;
						break;
					}
				}
				if (previous === undefined && !isNew) {
					continue;
				}
				await restore(disk, previous === undefined ? null : previous);
				undone++;
			} catch (e) {
				app().toast('Couldn\'t undo the save of ' + path + ': ' + e.message, 'bad');
			}
		}
		if (undone) {
			app().toast('Put back ' + undone + ' file' + (undone > 1 ? 's' : '') + ' as they were before that save.', 'good');
		}
	};

	// ---- the History window ----

	// ---- the diff: Monaco's diff editor, with the file's own colouring ----

	let diffEditor = null;
	let diffModels = [];

	function disposeDiff() {
		if (diffEditor) {
			diffEditor.dispose();
			diffEditor = null;
		}
		diffModels.forEach((m) => m.dispose());
		diffModels = [];
	}

	// What restoring would change: the file as it is now on the left, the version on the right. Built after it is on the page.
	function diffHost(now, version, disk) {
		const sideBySide = app().readLocal('ms.histSideBySide') !== false;
		const host = el('div', { className: 'hist-diff-host' });
		const status = el('span', { className: 'hist-diff-note', textContent: 'Loading the editor…' });
		const mode = el('button', { className: 'ico-btn', textContent: sideBySide ? 'Side by side' : 'Inline', title: 'Switch between two columns and one' });
		const prev = el('button', { className: 'ico-btn', textContent: '↑ Previous change' });
		const next = el('button', { className: 'ico-btn', textContent: '↓ Next change' });
		const tools = el('div', { className: 'hist-diff-tools' }, el('span', { className: 'hist-diff-key', textContent: 'Now → restored', title: 'Left (red): the file as it is now. Right (green): the file if you restore this version.' }), el('span', { className: 'spacer' }), status, prev, next, mode);
		const wrap = el('div', { className: 'hist-diff-wrap' }, tools, host);
		const start = async () => {
			try {
				await MS.ide.loadMonaco();
			} catch (e) {
				status.textContent = 'Couldn\'t load the editor: ' + e.message;
				return;
			}
			const monaco = MS.ide.monaco;
			disposeDiff();
			const language = /\.ini$/i.test(disk) ? 'ccini' : /\.lua$/i.test(disk) ? 'lua' : 'plaintext';
			let inline = !sideBySide;
			diffEditor = monaco.editor.createDiffEditor(host, {
				theme: 'cortex',
				automaticLayout: true,
				readOnly: true,
				originalEditable: false,
				renderSideBySide: !inline,
				renderOverviewRuler: true,
				ignoreTrimWhitespace: false,
				hideUnchangedRegions: { enabled: true, contextLineCount: 3, minimumLineCount: 5, revealLineCount: 20 },
				fontSize: 12.5,
				minimap: { enabled: false },
				scrollBeyondLastLine: false,
				stickyScroll: { enabled: false },
			});
			diffModels = [monaco.editor.createModel(now, language), monaco.editor.createModel(version, language)];
			diffEditor.setModel({ original: diffModels[0], modified: diffModels[1] });
			const count = () => {
				const changes = diffEditor && diffEditor.getLineChanges ? diffEditor.getLineChanges() : null;
				status.textContent = changes ? changes.length + ' change' + (changes.length === 1 ? '' : 's') : '';
			};
			diffEditor.onDidUpdateDiff(count);
			const go = (direction) => diffEditor && diffEditor.goToDiff && diffEditor.goToDiff(direction);
			prev.onclick = () => go('previous');
			next.onclick = () => go('next');
			mode.onclick = () => {
				inline = !inline;
				diffEditor.updateOptions({ renderSideBySide: !inline });
				mode.textContent = inline ? 'Inline' : 'Side by side';
				app().writeLocal('ms.histSideBySide', !inline);
			};
			count();
		};
		setTimeout(start, 0);
		return wrap;
	}

	H.open = async function (focusPath) {
		disposeDiff();
		if (!H.available()) {
			app().toast('History needs Studio\'s server: open Studio with Mod Studio.bat or the Cortex Studio app.', 'bad');
			return;
		}
		const filter = el('input', { type: 'search', className: 'hist-filter', placeholder: 'Filter files…' });
		const fileList = el('div', { className: 'hist-files' });
		const versionList = el('div', { className: 'hist-versions' });
		const view = el('div', { className: 'hist-view' }, el('p', { className: 'hist-empty', textContent: 'Pick a file on the left.' }));
		const body = el('div', { className: 'hist' },
			el('p', { className: 'hist-intro', textContent: 'Every version of every file Studio changed in a mod, starting with the mod as it was before Studio first changed it. Restore puts a version back as a new save, so a restore can be undone too.' }),
			el('div', { className: 'hist-cols' }, el('div', { className: 'hist-left' }, filter, fileList), versionList, view),
		);
		MS.dialogs.open('History', body, [{ text: 'Close', action: () => disposeDiff() }], true);
		$('modalCard').classList.add('hist-card');

		let files = [];
		try {
			files = await (await call('history')).json();
		} catch (e) {
			fileList.append(el('p', { className: 'hist-empty', textContent: 'Couldn\'t read the history: ' + e.message }));
			return;
		}
		const focus = focusPath ? diskOf(focusPath).toLowerCase() : '';
		const drawFiles = () => {
			const q = filter.value.trim().toLowerCase();
			fileList.innerHTML = '';
			const shown = files.filter((f) => !q || f.key.toLowerCase().includes(q));
			if (!shown.length) {
				fileList.append(el('p', { className: 'hist-empty', textContent: files.length ? 'No file matches.' : 'Studio hasn\'t changed any file in a mod yet.' }));
			}
			for (const f of shown) {
				const row = el('button', { className: 'hist-file' }, el('b', { textContent: f.key.split('/').pop() }), el('span', { textContent: f.key.replace(/^Mods\//, '') }), el('span', { textContent: when(f.time) + ' · ' + f.count + ' change' + (f.count > 1 ? 's' : '') }));
				row.onclick = () => {
					fileList.querySelectorAll('.sel').forEach((r) => r.classList.remove('sel'));
					row.classList.add('sel');
					showFile(f.key);
				};
				fileList.append(row);
				if (focus && f.key.toLowerCase() === focus) {
					row.click();
				}
			}
		};
		filter.oninput = drawFiles;
		drawFiles();

		async function showFile(disk) {
			versionList.innerHTML = '';
			view.replaceChildren(el('p', { className: 'hist-empty', textContent: 'Pick a version.' }));
			const [versions, current] = await Promise.all([versionsOf(disk), currentText(disk)]);
			for (const v of versions) {
				const label = v.original ? 'Before Studio changed this mod' : v.message || 'A version';
				const row = el('button', { className: 'hist-version' + (v.original ? ' original' : '') }, el('b', { textContent: label }), el('span', { textContent: when(v.time) }));
				row.onclick = async () => {
					versionList.querySelectorAll('.sel').forEach((r) => r.classList.remove('sel'));
					row.classList.add('sel');
					const bytes = v.deleted ? null : await versionBytes(disk, v.id);
					showVersion(disk, v, bytes, current);
				};
				versionList.append(row);
			}
			if (!versions.length) {
				versionList.append(el('p', { className: 'hist-empty', textContent: 'No versions kept.' }));
			}
		}

		function showVersion(disk, v, bytes, current) {
			const text = decode(bytes);
			const same = text === current;
			const restoreBtn = el('button', { className: 'tb primary', textContent: text === null ? 'Delete the file again' : 'Restore this version' });
			restoreBtn.disabled = same;
			restoreBtn.onclick = async () => {
				restoreBtn.disabled = true;
				try {
					await restore(disk, bytes);
					app().toast('Restored ' + disk.split('/').pop() + ' to the version from ' + when(v.time) + '.', 'good');
					MS.dialogs.close();
				} catch (e) {
					app().toast('Couldn\'t restore it: ' + e.message, 'bad');
					restoreBtn.disabled = false;
				}
			};
			view.replaceChildren(
				el('div', { className: 'hist-view-head' },
					el('span', { textContent: same ? 'This version is what the file has now.' : text === null ? 'The file was deleted here.' : 'What restoring this version changes in the file now:' }),
					el('span', { className: 'spacer' }),
					restoreBtn,
				),
				same || text === null ? '' : diffHost(current || '', text, disk),
			);
		}
	};

	// ---- a small line diff: what turning `from` into `to` changes ----

	function diffView(from, to) {
		const a = from.replace(/\r\n/g, '\n').split('\n');
		const b = to.replace(/\r\n/g, '\n').split('\n');
		// Common start and end first, so only the changed middle needs the table.
		let start = 0;
		while (start < a.length && start < b.length && a[start] === b[start]) {
			start++;
		}
		let endA = a.length;
		let endB = b.length;
		while (endA > start && endB > start && a[endA - 1] === b[endB - 1]) {
			endA--;
			endB--;
		}
		const ma = a.slice(start, endA);
		const mb = b.slice(start, endB);
		const lines = [];
		if (ma.length * mb.length > 4e6) {
			ma.forEach((l) => lines.push(['-', l]));
			mb.forEach((l) => lines.push(['+', l]));
		} else {
			const n = ma.length;
			const m = mb.length;
			const lcs = Array.from({ length: n + 1 }, () => new Uint32Array(m + 1));
			for (let i = n - 1; i >= 0; i--) {
				for (let j = m - 1; j >= 0; j--) {
					lcs[i][j] = ma[i] === mb[j] ? lcs[i + 1][j + 1] + 1 : Math.max(lcs[i + 1][j], lcs[i][j + 1]);
				}
			}
			let i = 0;
			let j = 0;
			while (i < n || j < m) {
				if (i < n && j < m && ma[i] === mb[j]) {
					lines.push([' ', ma[i++]]);
					j++;
				} else if (j < m && (i >= n || lcs[i][j + 1] >= lcs[i + 1][j])) {
					lines.push(['+', mb[j++]]);
				} else {
					lines.push(['-', ma[i++]]);
				}
			}
		}
		const out = el('div', { className: 'hist-diff' });
		const context = 3;
		const show = (from, count, lineNo) => a.slice(from, from + count).forEach((l, k) => out.append(el('div', { className: 'd-ctx', textContent: String(lineNo + k + 1).padStart(5) + '   ' + l })));
		show(Math.max(0, start - context), Math.min(context, start), Math.max(0, start - context));
		let lineNo = start;
		for (const [kind, text] of lines) {
			if (kind === ' ') {
				lineNo++;
				continue; // Unchanged lines inside the change are left out; the + and - lines say what moves.
			}
			out.append(el('div', { className: kind === '+' ? 'd-add' : 'd-del', textContent: (kind === '-' ? String(++lineNo).padStart(5) : '     ') + ' ' + kind + ' ' + text }));
		}
		show(endA, Math.min(context, a.length - endA), endA);
		if (!lines.length) {
			out.append(el('div', { className: 'd-ctx', textContent: 'Only line endings differ.' }));
		}
		return out;
	}
})();
