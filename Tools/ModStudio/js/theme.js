// Themes: recolour the whole of Studio. A theme is just values for the base colour tokens on :root (see studio.css); everything
// else (tinted notices, the canvases, the code editor) is derived from those. The chosen theme is saved in localStorage, along
// with its finished CSS, so the small script in ModStudio.html's <head> can apply it before the first paint.
(function () {
	const MS = (window.MS = window.MS || {});
	const KEY = 'ms.theme';
	const CSS_KEY = 'ms.theme.css';

	const hsl = (h, s, l) => 'hsl(' + Math.round(((h % 360) + 360) % 360) + ' ' + Math.round(s * 100) + '% ' + Math.round(l * 100) + '%)';

	// Builds every base token from a surface hue, an accent hue and a mode.
	function tokens(t) {
		const sh = t.surface, ah = t.accent, ss = t.sat;
		if (t.light) {
			return {
				'--bg': hsl(sh, ss, 0.91), '--panel': hsl(sh, ss, 0.965), '--panel2': hsl(sh, ss, 0.935), '--panel3': hsl(sh, ss, 0.875),
				'--line': hsl(sh, ss, 0.82), '--line2': hsl(sh, ss, 0.72),
				'--text': hsl(sh, ss * 1.5, 0.13), '--dim': hsl(sh, ss, 0.36), '--faint': hsl(sh, ss * 0.8, 0.5),
				'--accent': hsl(ah, t.accentSat, 0.4), '--accent-ink': '#ffffff',
				'--blue': hsl(205, 0.85, 0.38), '--good': hsl(145, 0.6, 0.3), '--bad': hsl(5, 0.7, 0.45), '--warn': hsl(33, 0.9, 0.36),
				'--view-bg': hsl(sh, ss, 0.86), '--ov-rgb': '0,0,0', '--shade-rgb': '255,255,255', '--scheme': 'light',
			};
		}
		return {
			'--bg': hsl(sh, ss, 0.06), '--panel': hsl(sh, ss, 0.09), '--panel2': hsl(sh, ss, 0.12), '--panel3': hsl(sh, ss, 0.15),
			'--line': hsl(sh, ss, 0.19), '--line2': hsl(sh, ss, 0.25),
			'--text': hsl(sh, ss * 0.8, 0.92), '--dim': hsl(sh, ss * 0.6, 0.64), '--faint': hsl(sh, ss * 0.45, 0.45),
			'--accent': hsl(ah, t.accentSat, 0.6), '--accent-ink': hsl(ah, 0.7, 0.08),
			'--blue': hsl(200, 1, 0.75), '--good': hsl(145, 0.5, 0.52), '--bad': hsl(6, 0.82, 0.64), '--warn': hsl(38, 0.72, 0.55),
			'--view-bg': hsl(sh, ss, 0.07), '--ov-rgb': '255,255,255', '--shade-rgb': '0,0,0', '--scheme': 'dark',
		};
	}

	// "Studio" is the original look: its values live in studio.css, so it sets nothing.
	const PRESETS = [
		{ id: 'studio', name: 'Studio', swatch: ['#15181e', '#f2b63d'], vars: null },
		{ id: 'ember', name: 'Ember', t: { surface: 22, accent: 28, sat: 0.18, accentSat: 0.92 } },
		{ id: 'forest', name: 'Forest', t: { surface: 150, accent: 140, sat: 0.16, accentSat: 0.6 } },
		{ id: 'ocean', name: 'Ocean', t: { surface: 200, accent: 190, sat: 0.2, accentSat: 0.85 } },
		{ id: 'violet', name: 'Violet', t: { surface: 265, accent: 270, sat: 0.18, accentSat: 0.85 } },
		{ id: 'rose', name: 'Rose', t: { surface: 335, accent: 340, sat: 0.15, accentSat: 0.85 } },
		{ id: 'mono', name: 'Mono', t: { surface: 0, accent: 0, sat: 0, accentSat: 0 } },
		{ id: 'light', name: 'Light', t: { surface: 215, accent: 215, sat: 0.2, accentSat: 0.8, light: true } },
		{ id: 'paper', name: 'Paper', t: { surface: 38, accent: 24, sat: 0.28, accentSat: 0.8, light: true } },
	];
	for (const p of PRESETS) {
		if (p.t) {
			p.vars = tokens(p.t);
			p.swatch = [p.vars['--panel'], p.vars['--accent']];
		}
	}

	const custom = (hue, light) => tokens({ surface: hue, accent: hue, sat: 0.18, accentSat: 0.85, light: !!light });

	let state = { preset: 'studio', hue: 40, light: false };
	let cache = null;
	const listeners = [];

	function load() {
		try {
			const s = JSON.parse(localStorage.getItem(KEY));
			if (s && typeof s === 'object') {
				state = { preset: String(s.preset || 'studio'), hue: Number(s.hue) || 0, light: !!s.light };
			}
		} catch (e) { /* no storage: the default theme */ }
	}

	function currentVars() {
		if (state.preset === 'custom') {
			return custom(state.hue, state.light);
		}
		const p = PRESETS.find((x) => x.id === state.preset) || PRESETS[0];
		return p.vars;
	}

	function apply(save) {
		const root = document.documentElement;
		const old = root.style.cssText;
		root.style.cssText = '';
		const vars = currentVars();
		let text = '';
		if (vars) {
			for (const k of Object.keys(vars)) {
				if (k !== '--scheme') {
					root.style.setProperty(k, vars[k]);
				}
			}
			text = root.style.cssText;
		}
		root.style.colorScheme = vars ? vars['--scheme'] : 'dark';
		text = root.style.cssText;
		cache = null;
		if (save) {
			try {
				localStorage.setItem(KEY, JSON.stringify(state));
				localStorage.setItem(CSS_KEY, text);
			} catch (e) { /* not saved */ }
		}
		if (root.style.cssText !== old || save) {
			defineMonaco();
			for (const fn of listeners) {
				try { fn(); } catch (e) { console.error(e); }
			}
			if (MS.app && MS.app.requestDraw) {
				MS.app.requestDraw();
			}
			document.dispatchEvent(new CustomEvent('ms-theme'));
		}
		syncPanel();
	}

	// ---- reading the theme from canvas code ----
	function css(name) {
		if (!cache) {
			cache = { cs: getComputedStyle(document.documentElement), v: {} };
		}
		if (!(name in cache.v)) {
			cache.v[name] = cache.cs.getPropertyValue(name).trim();
		}
		return cache.v[name];
	}
	// Computed colours can come back as hsl() or color-mix(); the canvas takes those as they are, but not alpha-adjusted, so
	// the rgb forms below go through a 1px canvas to get channels.
	let probe = null;
	function rgb(name) {
		const key = 'rgb:' + name;
		css(name);
		if (cache.v[key]) {
			return cache.v[key];
		}
		probe = probe || document.createElement('canvas').getContext('2d', { willReadFrequently: true });
		probe.canvas.width = probe.canvas.height = 1;
		probe.clearRect(0, 0, 1, 1);
		probe.fillStyle = '#888';
		probe.fillStyle = css(name);
		probe.fillRect(0, 0, 1, 1);
		const d = probe.getImageData(0, 0, 1, 1).data;
		return (cache.v[key] = d[0] + ',' + d[1] + ',' + d[2]);
	}
	const theme = {
		css,
		accent: () => css('--accent'),
		accentA: (a) => 'rgba(' + rgb('--accent') + ',' + a + ')',
		blue: () => css('--blue'),
		blueA: (a) => 'rgba(' + rgb('--blue') + ',' + a + ')',
		bad: () => css('--bad'),
		dim: () => css('--dim'),
		text: () => css('--text'),
		// White on the dark themes, black on the light ones: for grid lines and hover outlines.
		ov: (a) => 'rgba(' + css('--ov-rgb') + ',' + a + ')',
		// The opposite, for halos under labels.
		shade: (a) => 'rgba(' + css('--shade-rgb') + ',' + a + ')',
		isLight: () => css('color-scheme') === 'light' || css('--shade-rgb') === '255,255,255',
		onChange: (fn) => listeners.push(fn),
		presets: PRESETS,
		get state() { return state; },
		set(next) {
			state = Object.assign({}, state, next);
			apply(true);
		},
	};
	MS.theme = theme;

	// ---- the code editor ----
	function hex(name) {
		const c = rgb(name).split(',').map(Number);
		return '#' + c.map((n) => n.toString(16).padStart(2, '0')).join('');
	}
	let flip = false;
	function defineMonaco(monaco) {
		monaco = monaco || (MS.ide && MS.ide.monaco) || window.monaco;
		if (!monaco || !monaco.editor) {
			return;
		}
		const light = theme.isLight();
		const mix = (a, b, pct) => 'color-mix(in srgb, ' + css(a) + ' ' + pct + '%, ' + css(b) + ')';
		const mixHex = (a, b, pct) => {
			probe = probe || document.createElement('canvas').getContext('2d', { willReadFrequently: true });
			probe.clearRect(0, 0, 1, 1);
			probe.fillStyle = '#888';
			probe.fillStyle = mix(a, b, pct);
			probe.fillRect(0, 0, 1, 1);
			const d = probe.getImageData(0, 0, 1, 1).data;
			return '#' + [d[0], d[1], d[2]].map((n) => n.toString(16).padStart(2, '0')).join('');
		};
		const strip = (h) => h.slice(1);
		const def = {
			base: light ? 'vs' : 'vs-dark',
			inherit: true,
			rules: [
				{ token: 'key', foreground: strip(hex('--blue')) },
				{ token: 'type', foreground: strip(hex('--accent')) },
				{ token: 'string.path', foreground: strip(mixHex('--good', '--text', light ? 100 : 75)) },
				{ token: 'comment', foreground: strip(hex('--faint')), fontStyle: 'italic' },
			],
			colors: {
				'editor.background': hex('--view-bg'),
				'editor.foreground': hex('--text'),
				'editor.lineHighlightBackground': mixHex('--panel3', '--view-bg', 45),
				'editorLineNumber.foreground': hex('--faint'),
				'editorLineNumber.activeForeground': hex('--dim'),
				'editorCursor.foreground': hex('--accent'),
				'editor.selectionBackground': mixHex('--accent', '--view-bg', 28),
				'editorWidget.background': hex('--panel2'),
				'editorSuggestWidget.background': hex('--panel2'),
				'editorSuggestWidget.selectedBackground': hex('--panel3'),
				'editorHoverWidget.background': hex('--panel2'),
				'editorGutter.background': hex('--view-bg'),
			},
		};
		// Monaco ignores a redefinition of the theme that is already showing, so the live one alternates between two names;
		// 'cortex' itself is kept current for editors made later.
		monaco.editor.defineTheme('cortex', def);
		flip = !flip;
		const live = flip ? 'cortex-a' : 'cortex-b';
		monaco.editor.defineTheme(live, def);
		monaco.editor.setTheme(live);
	}
	theme.defineMonaco = defineMonaco;

	// ---- the picker ----
	let panel = null, button = null, hueInput = null;
	function syncPanel() {
		if (!panel) {
			return;
		}
		for (const b of panel.querySelectorAll('.theme-preset')) {
			b.classList.toggle('sel', b.dataset.id === state.preset);
		}
		hueInput.value = state.hue;
		panel.querySelector('.theme-hue-box').classList.toggle('sel', state.preset === 'custom');
		panel.querySelector('.theme-light').checked = state.light;
	}
	function build() {
		const mk = (tag, cls, text) => {
			const e = document.createElement(tag);
			if (cls) { e.className = cls; }
			if (text) { e.textContent = text; }
			return e;
		};
		panel = mk('div', 'ctx-menu theme-panel');
		panel.hidden = true;
		panel.appendChild(mk('div', 'theme-head', 'Theme'));
		const grid = mk('div', 'theme-grid');
		for (const p of PRESETS) {
			const b = mk('button', 'theme-preset');
			b.dataset.id = p.id;
			b.title = p.name;
			const sw = mk('span', 'theme-swatch');
			sw.style.background = 'linear-gradient(135deg,' + p.swatch[0] + ' 55%,' + p.swatch[1] + ' 55%)';
			b.append(sw, mk('span', 'theme-name', p.name));
			b.onclick = () => theme.set({ preset: p.id });
			grid.appendChild(b);
		}
		panel.appendChild(grid);
		const box = mk('div', 'theme-hue-box');
		box.appendChild(mk('div', 'theme-sub', 'Or step through the hues'));
		const row = mk('div', 'theme-hue-row');
		const step = (d) => theme.set({ preset: 'custom', hue: (((state.hue + d) % 360) + 360) % 360 });
		const prev = mk('button', 'tb icon', '◀');
		prev.title = 'Previous hue';
		prev.onclick = () => step(-15);
		const next = mk('button', 'tb icon', '▶');
		next.title = 'Next hue';
		next.onclick = () => step(15);
		hueInput = mk('input', 'theme-hue');
		hueInput.type = 'range';
		hueInput.min = 0;
		hueInput.max = 359;
		hueInput.oninput = () => theme.set({ preset: 'custom', hue: Number(hueInput.value) });
		row.append(prev, hueInput, next);
		box.appendChild(row);
		const lab = mk('label', 'theme-light-row');
		const cb = mk('input', 'theme-light');
		cb.type = 'checkbox';
		cb.onchange = () => theme.set({ preset: 'custom', light: cb.checked });
		lab.append(cb, document.createTextNode(' Light'));
		box.appendChild(lab);
		panel.appendChild(box);
		document.body.appendChild(panel);

		const close = () => { panel.hidden = true; };
		document.addEventListener('mousedown', (e) => {
			if (!panel.hidden && !panel.contains(e.target) && e.target !== button) {
				close();
			}
		}, true);
		document.addEventListener('keydown', (e) => {
			if (e.key === 'Escape' && !panel.hidden) {
				close();
			}
		});
		window.addEventListener('blur', close);
	}
	function mountButton() {
		const help = document.getElementById('btnHelp');
		if (!help || button) {
			return;
		}
		build();
		button = document.createElement('button');
		button.className = 'tb icon';
		button.id = 'btnTheme';
		button.title = 'Theme: recolour Studio';
		button.textContent = '◐';
		button.onclick = () => {
			if (!panel.hidden) {
				panel.hidden = true;
				return;
			}
			const r = button.getBoundingClientRect();
			panel.hidden = false;
			panel.style.top = r.bottom + 4 + 'px';
			panel.style.left = Math.max(8, Math.min(r.right - panel.offsetWidth, innerWidth - panel.offsetWidth - 8)) + 'px';
			syncPanel();
		};
		help.parentNode.insertBefore(button, help);
		syncPanel();
	}

	load();
	apply(false);
	if (document.readyState === 'loading') {
		document.addEventListener('DOMContentLoaded', mountButton);
	} else {
		mountButton();
	}
})();
