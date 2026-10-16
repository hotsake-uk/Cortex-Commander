// Cortex Mod Studio: resizable panels. Each splitter is a thin handle on a panel's edge; dragging it sets a CSS variable on #app that
// the grid in studio.css uses for that column (--lib-size for the list or side bar, --right-w / --right-w-script for the parts and
// properties panel in Design and Script, --tree-w for the parts list beside the properties). Sizes are remembered; double-click a
// splitter to go back to the default. The console panel's own handle (#panelResize, ide.js) works the same way for its height.
'use strict';
window.MS = window.MS || {};

(function () {
	const $ = (id) => document.getElementById(id);
	const KEY = 'ms.layout';
	const MIN_CENTER = 320; // the view or the editor never gets narrower than this

	function load() {
		try {
			return JSON.parse(localStorage.getItem(KEY)) || {};
		} catch (e) {
			return {};
		}
	}

	function save(sizes) {
		try {
			localStorage.setItem(KEY, JSON.stringify(sizes));
		} catch (e) {
			// Private windows can refuse storage; sizes then last until the page reloads.
		}
	}

	const sizes = load();

	function apply(name, px) {
		const app = $('app');
		if (px === undefined || px === null) {
			app.style.removeProperty(name);
		} else {
			app.style.setProperty(name, Math.round(px) + 'px');
		}
	}

	function relayout() {
		if (MS.app && MS.app.requestDraw) {
			MS.app.requestDraw();
		}
		placeTreeSplitter();
	}

	const rightVar = () => ($('app').classList.contains('deep') ? '--right-w-script' : '--right-w');

	// side: which edge of the panel the handle sits on. grow: +1 when dragging right makes the panel wider, -1 when it makes it narrower.
	function splitter(panel, side, cssVar, grow, limits) {
		const handle = document.createElement('div');
		handle.className = 'splitter ' + side;
		handle.title = 'Drag to resize; double-click for the usual size';
		panel.append(handle);
		handle.addEventListener('pointerdown', (e) => {
			if (e.button !== 0) {
				return;
			}
			e.preventDefault();
			const name = typeof cssVar === 'function' ? cssVar() : cssVar;
			const start = e.clientX;
			const width = limits.measure ? limits.measure() : panel.getBoundingClientRect().width;
			handle.setPointerCapture(e.pointerId);
			document.body.classList.add('resizing');
			const move = (ev) => {
				const [min, max] = limits.range();
				const px = Math.max(min, Math.min(max, width + grow * (ev.clientX - start)));
				apply(name, px);
				sizes[name] = Math.round(px);
				relayout();
			};
			const up = () => {
				handle.removeEventListener('pointermove', move);
				handle.removeEventListener('pointerup', up);
				document.body.classList.remove('resizing');
				save(sizes);
			};
			handle.addEventListener('pointermove', move);
			handle.addEventListener('pointerup', up);
		});
		handle.addEventListener('dblclick', () => {
			const name = typeof cssVar === 'function' ? cssVar() : cssVar;
			delete sizes[name];
			apply(name, null);
			save(sizes);
			relayout();
		});
		return handle;
	}

	const width = (id) => {
		const el = $(id);
		return el && el.offsetParent !== null ? el.getBoundingClientRect().width : 0;
	};
	const sideWidth = () => Math.max(width('left'), width('side'));

	let treeHandle = null;

	// The parts list and the properties sit side by side on wide screens (or with the view hidden), else one above the other.
	function placeTreeSplitter() {
		if (!treeHandle) {
			return;
		}
		const right = $('right');
		const sideBySide = getComputedStyle(right).display === 'grid';
		treeHandle.hidden = !sideBySide;
		if (sideBySide) {
			treeHandle.style.left = $('tree').getBoundingClientRect().right - right.getBoundingClientRect().left - 3 + 'px';
		}
	}

	// Save ▾: the rarer things to do with your changes (Discard, History), out of the top bar.
	function saveMenu() {
		const menu = $('saveMenu');
		const close = () => {
			menu.hidden = true;
			document.removeEventListener('pointerdown', outside, true);
		};
		const outside = (e) => {
			if (!menu.contains(e.target) && !$('btnSaveMenu').contains(e.target)) {
				close();
			}
		};
		$('btnSaveMenu').onclick = () => {
			if (!menu.hidden) {
				close();
				return;
			}
			menu.hidden = false;
			const r = $('saveSplit').getBoundingClientRect();
			menu.style.left = Math.min(r.left, window.innerWidth - menu.offsetWidth - 8) + 'px';
			menu.style.top = r.bottom + 4 + 'px';
			setTimeout(() => document.addEventListener('pointerdown', outside, true), 0);
		};
		// The items keep their own handlers (app.js); choosing one closes the menu.
		menu.addEventListener('click', (e) => {
			if (e.target.closest('button')) {
				close();
			}
		});
	}

	MS.layout = {
		init() {
			saveMenu();
			for (const [name, px] of Object.entries(sizes)) {
				apply(name, px);
			}
			const listRange = () => [160, Math.max(160, window.innerWidth - width('activity') - width('right') - MIN_CENTER)];
			splitter($('left'), 'right', '--lib-size', 1, { range: listRange });
			splitter($('side'), 'right', '--lib-size', 1, { range: listRange });
			splitter($('right'), 'left', rightVar, -1, {
				range: () => [260, Math.max(260, window.innerWidth - width('activity') - sideWidth() - MIN_CENTER)],
			});
			treeHandle = splitter($('right'), 'tree', '--tree-w', 1, {
				measure: () => width('tree'),
				range: () => [160, Math.max(160, width('right') - 260)],
			});
			new ResizeObserver(placeTreeSplitter).observe($('right'));
			new ResizeObserver(placeTreeSplitter).observe($('tree'));
			window.addEventListener('resize', placeTreeSplitter);
			placeTreeSplitter();
		},
	};

	window.addEventListener('DOMContentLoaded', () => MS.layout.init());
})();
