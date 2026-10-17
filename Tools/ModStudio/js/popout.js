// Cortex Studio: a tab in its own window. The pop-out button on a sprite tab or a file tab asks Studio's server (/api/popout) to open another window
// on the same page; that window reads what to show from its address (?popout=...), loads Studio as usual, and shows just that tab, with none of
// the rest of the screen. Outside the app (a browser, the headless bridge) the same address opens in a browser tab. Windows tell each other when a
// picture is saved (BroadcastChannel), so the one still showing the old picture lets go of it.
'use strict';
window.MS = window.MS || {};

(function () {
	const params = new URLSearchParams(location.search);
	let request = null;
	try {
		request = params.get('popout') ? JSON.parse(params.get('popout')) : null;
	} catch (e) {
		request = null;
	}

	let channel = null;
	try {
		channel = new BroadcastChannel('cortex-studio');
		channel.onmessage = (e) => {
			const m = e.data || {};
			if (m.type === 'sprites' && Array.isArray(m.paths) && MS.app && MS.app.forgetSprites) {
				MS.app.forgetSprites(m.paths);
				MS.panels && MS.panels.forgetThumbs && MS.panels.forgetThumbs();
				MS.app.requestDraw();
			}
		};
	} catch (e) {
		channel = null;
	}

	// Before the page loads: the window shows only the tab.
	function begin() {
		if (request) {
			document.getElementById('app').classList.add('popout');
			document.title = 'Cortex Studio';
		}
	}

	// After the game is loaded: opens what was asked for.
	async function run() {
		const a = MS.app;
		try {
			if (request.k === 'sprite') {
				await MS.pixel.open({ file: request.file, frameCount: request.frames || 1 });
			} else if (request.k === 'file') {
				MS.ide.setMode('deep', true);
				await MS.ide.openFile(request.path, request.line);
			}
			const tab = MS.ide.active;
			if (tab && tab.title) {
				document.title = tab.title + ' · Cortex Studio';
			} else if (tab && tab.path) {
				document.title = tab.path.split('/').pop() + ' · Cortex Studio';
			}
		} catch (e) {
			a.toast('Couldn\'t open that here: ' + (e.message || e), 'bad');
		}
	}

	// Moves a tab into its own window, when it can go: saved, and about a file.
	async function pop(tab) {
		const a = MS.app;
		let what = null;
		if (tab.sprite && tab.px) {
			if (!tab.px.file) {
				a.toast('A new picture has no file yet. Save it first, then it can go in its own window.', 'bad');
				return;
			}
			if (tab.px.dirty) {
				a.toast('Save the picture first (it has changes the other window wouldn\'t have).', 'bad');
				return;
			}
			what = { k: 'sprite', file: tab.px.file, frames: tab.px.frames.length };
		} else if (tab.kind === 'code' && tab.path && !tab.doc) {
			if (MS.ide.tabIsDirty && MS.ide.tabIsDirty(tab)) {
				a.toast('Save the file first (it has changes the other window wouldn\'t have).', 'bad');
				return;
			}
			what = { k: 'file', path: tab.path };
		} else {
			a.toast('This tab can\'t go in its own window yet. Sprite and file tabs can.', 'bad');
			return;
		}
		const text = JSON.stringify(what);
		let native = false;
		try {
			const response = await a.fs.api('POST', 'popout', '', JSON.stringify({ what: text }));
			native = !!(response && (await response.json()).native);
		} catch (e) {
			native = false;
		}
		if (!native) {
			const url = new URL(location.href);
			url.searchParams.set('popout', text);
			window.open(url.toString(), '_blank');
		}
		MS.ide.closeTab(tab, true);
	}

	function saved(paths) {
		if (channel && paths && paths.length) {
			channel.postMessage({ type: 'sprites', paths });
		}
	}

	MS.popout = { request, begin, run, pop, saved };
})();
