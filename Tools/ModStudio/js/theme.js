// Themes: recolour the whole of Studio. A theme is just values for the base colour tokens on :root (see studio.css); everything
// else (tinted notices, the canvases, the code editor) is derived from those. The theme picker (the ◐ button) also tunes them:
// a colour picker per token, text size, spacing, code size, your own saved themes, and export/import as JSON.
// The choice is saved in localStorage, along with its finished CSS, so the small script in ModStudio.html's <head> can apply it
// before the first paint.
(function () {
	const MS = (window.MS = window.MS || {});
	const KEY = 'ms.theme';
	const CSS_KEY = 'ms.theme.css';
	const DEFAULT_UI = { font: 13, spacing: 'comfortable', code: 13 };
	const SPACING = { compact: 1.2, comfortable: 1.4, roomy: 1.65 };

	// The colours you can change: [token, plain name, help]. --sel and --focus fall back to --panel3 and --accent when not set.
	const TOKENS = [
		['--bg', 'Background', 'Behind everything'],
		['--panel', 'Panels', 'Side lists, the top bar, dialogs'],
		['--panel2', 'Buttons and inputs', 'Raised parts on a panel'],
		['--panel3', 'Hover', 'What the mouse is over'],
		['--sel', 'Selected row', 'The row you picked in a list'],
		['--line', 'Lines', 'Dividers and borders'],
		['--line2', 'Strong lines', 'Borders on dialogs and menus'],
		['--text', 'Text', ''],
		['--dim', 'Dim text', 'Less important words'],
		['--faint', 'Faint text', 'Hints and disabled things'],
		['--accent', 'Accent', 'Highlights, the Play button, the marks on the picture'],
		['--accent-ink', 'Text on accent', ''],
		['--focus', 'Focus', 'The box you are typing in'],
		['--blue', 'Info blue', 'Notes and the Reach marks'],
		['--good', 'Good', 'Saved, own, on'],
		['--warn', 'Warning', ''],
		['--bad', 'Problem', ''],
		['--view-bg', 'Design view', 'Behind the picture, and the code editor'],
	];
	const TOKEN_SET = new Set(TOKENS.map((t) => t[0]));

	const hsl = (h, s, l) => 'hsl(' + Math.round(((h % 360) + 360) % 360) + ' ' + Math.round(s * 100) + '% ' + Math.round(l * 100) + '%)';

	// Builds the base tokens from a surface hue, an accent hue and a mode.
	function tokens(t) {
		const sh = t.surface, ah = t.accent, ss = t.sat;
		if (t.light) {
			return {
				'--bg': hsl(sh, ss, 0.91), '--panel': hsl(sh, ss, 0.965), '--panel2': hsl(sh, ss, 0.935), '--panel3': hsl(sh, ss, 0.875),
				'--line': hsl(sh, ss, 0.82), '--line2': hsl(sh, ss, 0.72),
				'--text': hsl(sh, ss * 1.5, 0.13), '--dim': hsl(sh, ss, 0.36), '--faint': hsl(sh, ss * 0.8, 0.5),
				'--accent': hsl(ah, t.accentSat, 0.4), '--accent-ink': '#ffffff',
				'--blue': hsl(205, 0.85, 0.38), '--good': hsl(145, 0.6, 0.3), '--bad': hsl(5, 0.7, 0.45), '--warn': hsl(33, 0.9, 0.36),
				'--view-bg': hsl(sh, ss, 0.86),
			};
		}
		return {
			'--bg': hsl(sh, ss, 0.06), '--panel': hsl(sh, ss, 0.09), '--panel2': hsl(sh, ss, 0.12), '--panel3': hsl(sh, ss, 0.15),
			'--line': hsl(sh, ss, 0.19), '--line2': hsl(sh, ss, 0.25),
			'--text': hsl(sh, ss * 0.8, 0.92), '--dim': hsl(sh, ss * 0.6, 0.64), '--faint': hsl(sh, ss * 0.45, 0.45),
			'--accent': hsl(ah, t.accentSat, 0.6), '--accent-ink': hsl(ah, 0.7, 0.08),
			'--blue': hsl(200, 1, 0.75), '--good': hsl(145, 0.5, 0.52), '--bad': hsl(6, 0.82, 0.64), '--warn': hsl(38, 0.72, 0.55),
			'--view-bg': hsl(sh, ss, 0.07),
		};
	}

	// The first few are made from a hue (see tokens()); the rest follow well-known editor themes' published palettes.
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
		{ id: 'darcula', name: 'Darcula', vars: { '--bg': '#262626', '--panel': '#3c3f41', '--panel2': '#45494a', '--panel3': '#4e5254', '--line': '#323232', '--line2': '#515151', '--text': '#bbbbbb', '--dim': '#909090', '--faint': '#606366', '--accent': '#cc7832', '--accent-ink': '#1e1e1e', '--blue': '#6897bb', '--good': '#6a8759', '--warn': '#ffc66d', '--bad': '#ff6b68', '--view-bg': '#2b2b2b' } },
		{ id: 'material', name: 'Material', vars: { '--bg': '#1e272c', '--panel': '#263238', '--panel2': '#2e3c43', '--panel3': '#37474f', '--line': '#2b3a42', '--line2': '#3f545f', '--text': '#eeffff', '--dim': '#b0bec5', '--faint': '#546e7a', '--accent': '#80cbc4', '--accent-ink': '#1e272c', '--blue': '#82aaff', '--good': '#c3e88d', '--warn': '#ffcb6b', '--bad': '#f07178', '--view-bg': '#212b30' } },
		{ id: 'material-ocean', name: 'Material Ocean', vars: { '--bg': '#090b10', '--panel': '#0f111a', '--panel2': '#1a1c25', '--panel3': '#1f2233', '--line': '#232637', '--line2': '#2f3350', '--text': '#a9b0c4', '--dim': '#7a7f93', '--faint': '#464b5d', '--accent': '#84ffff', '--accent-ink': '#090b10', '--blue': '#82aaff', '--good': '#c3e88d', '--warn': '#ffcb6b', '--bad': '#f07178', '--view-bg': '#0b0d15' } },
		{ id: 'material-palenight', name: 'Material Palenight', vars: { '--bg': '#1c1f2b', '--panel': '#292d3e', '--panel2': '#303348', '--panel3': '#3a3f58', '--line': '#32364a', '--line2': '#444a66', '--text': '#a6accd', '--dim': '#838bb5', '--faint': '#676e95', '--accent': '#c792ea', '--accent-ink': '#1c1f2b', '--blue': '#82aaff', '--good': '#c3e88d', '--warn': '#ffcb6b', '--bad': '#f07178', '--view-bg': '#232635' } },
		{ id: 'nord', name: 'Nord', vars: { '--bg': '#242933', '--panel': '#2e3440', '--panel2': '#3b4252', '--panel3': '#434c5e', '--line': '#3b4252', '--line2': '#4c566a', '--text': '#d8dee9', '--dim': '#9aa5b8', '--faint': '#616e88', '--accent': '#88c0d0', '--accent-ink': '#2e3440', '--blue': '#81a1c1', '--good': '#a3be8c', '--warn': '#ebcb8b', '--bad': '#bf616a', '--view-bg': '#2b303b' } },
		{ id: 'one-dark', name: 'One Dark', vars: { '--bg': '#1e2227', '--panel': '#21252b', '--panel2': '#282c34', '--panel3': '#2c313a', '--line': '#333842', '--line2': '#3e4451', '--text': '#abb2bf', '--dim': '#7f848e', '--faint': '#5c6370', '--accent': '#61afef', '--accent-ink': '#21252b', '--blue': '#56b6c2', '--good': '#98c379', '--warn': '#e5c07b', '--bad': '#e06c75', '--view-bg': '#282c34' } },
		{ id: 'dracula', name: 'Dracula', vars: { '--bg': '#21222c', '--panel': '#282a36', '--panel2': '#2f3241', '--panel3': '#393c4e', '--line': '#343746', '--line2': '#44475a', '--text': '#f8f8f2', '--dim': '#9ea8d4', '--faint': '#6272a4', '--accent': '#bd93f9', '--accent-ink': '#282a36', '--blue': '#8be9fd', '--good': '#50fa7b', '--warn': '#ffb86c', '--bad': '#ff5555', '--view-bg': '#1e1f29' } },
		{ id: 'gruvbox-dark', name: 'Gruvbox (dark)', vars: { '--bg': '#1d2021', '--panel': '#282828', '--panel2': '#32302f', '--panel3': '#3c3836', '--line': '#45403d', '--line2': '#504945', '--text': '#ebdbb2', '--dim': '#a89984', '--faint': '#7c6f64', '--accent': '#fabd2f', '--accent-ink': '#282828', '--blue': '#83a598', '--good': '#b8bb26', '--warn': '#fe8019', '--bad': '#fb4934', '--view-bg': '#212121' } },
		{ id: 'gruvbox-light', name: 'Gruvbox (light)', vars: { '--bg': '#ebdbb2', '--panel': '#f9f5d7', '--panel2': '#f2e5bc', '--panel3': '#d5c4a1', '--line': '#d9c9a0', '--line2': '#bdae93', '--text': '#3c3836', '--dim': '#665c54', '--faint': '#928374', '--accent': '#b57614', '--accent-ink': '#fbf1c7', '--blue': '#076678', '--good': '#79740e', '--warn': '#af3a03', '--bad': '#9d0006', '--view-bg': '#fbf1c7' } },
		{ id: 'solarized-dark', name: 'Solarized (dark)', vars: { '--bg': '#00212b', '--panel': '#002b36', '--panel2': '#073642', '--panel3': '#0d414f', '--line': '#0a3a46', '--line2': '#1c5563', '--text': '#93a1a1', '--dim': '#839496', '--faint': '#586e75', '--accent': '#268bd2', '--accent-ink': '#002b36', '--blue': '#2aa198', '--good': '#859900', '--warn': '#b58900', '--bad': '#dc322f', '--view-bg': '#00252f' } },
		{ id: 'solarized-light', name: 'Solarized (light)', vars: { '--bg': '#eee8d5', '--panel': '#fdf6e3', '--panel2': '#f5eed9', '--panel3': '#e6dfc8', '--line': '#ddd6c1', '--line2': '#c9c3ad', '--text': '#586e75', '--dim': '#657b83', '--faint': '#93a1a1', '--accent': '#268bd2', '--accent-ink': '#fdf6e3', '--blue': '#2aa198', '--good': '#859900', '--warn': '#b58900', '--bad': '#dc322f', '--view-bg': '#f8f1dc' } },
		{ id: 'monokai', name: 'Monokai', vars: { '--bg': '#1e1f1c', '--panel': '#272822', '--panel2': '#2e2f29', '--panel3': '#3e3d32', '--line': '#3a3b34', '--line2': '#49483e', '--text': '#f8f8f2', '--dim': '#b3b3a6', '--faint': '#75715e', '--accent': '#fd971f', '--accent-ink': '#272822', '--blue': '#66d9ef', '--good': '#a6e22e', '--warn': '#e6db74', '--bad': '#f92672', '--view-bg': '#232420' } },
		{ id: 'tokyo-night', name: 'Tokyo Night', vars: { '--bg': '#14151f', '--panel': '#1a1b26', '--panel2': '#1f2335', '--panel3': '#292e42', '--line': '#24283b', '--line2': '#3b4261', '--text': '#c0caf5', '--dim': '#9aa5ce', '--faint': '#565f89', '--accent': '#7aa2f7', '--accent-ink': '#16161e', '--blue': '#7dcfff', '--good': '#9ece6a', '--warn': '#e0af68', '--bad': '#f7768e', '--view-bg': '#16161e' } },
		{ id: 'catppuccin-mocha', name: 'Catppuccin Mocha', vars: { '--bg': '#11111b', '--panel': '#181825', '--panel2': '#1e1e2e', '--panel3': '#313244', '--line': '#2a2b3c', '--line2': '#45475a', '--text': '#cdd6f4', '--dim': '#a6adc8', '--faint': '#6c7086', '--accent': '#cba6f7', '--accent-ink': '#11111b', '--blue': '#89b4fa', '--good': '#a6e3a1', '--warn': '#f9e2af', '--bad': '#f38ba8', '--view-bg': '#1e1e2e' } },
		{ id: 'catppuccin-latte', name: 'Catppuccin Latte', vars: { '--bg': '#dce0e8', '--panel': '#eff1f5', '--panel2': '#e6e9ef', '--panel3': '#ccd0da', '--line': '#ccd0da', '--line2': '#acb0be', '--text': '#4c4f69', '--dim': '#6c6f85', '--faint': '#9ca0b0', '--accent': '#8839ef', '--accent-ink': '#eff1f5', '--blue': '#1e66f5', '--good': '#40a02b', '--warn': '#df8e1d', '--bad': '#d20f39', '--view-bg': '#e9ecf2' } },
		{ id: 'github-dark', name: 'GitHub (dark)', vars: { '--bg': '#010409', '--panel': '#0d1117', '--panel2': '#161b22', '--panel3': '#21262d', '--line': '#262c36', '--line2': '#30363d', '--text': '#e6edf3', '--dim': '#8b949e', '--faint': '#6e7681', '--accent': '#58a6ff', '--accent-ink': '#0d1117', '--blue': '#79c0ff', '--good': '#3fb950', '--warn': '#d29922', '--bad': '#f85149', '--view-bg': '#0d1117' } },
		{ id: 'github-light', name: 'GitHub (light)', vars: { '--bg': '#eaeef2', '--panel': '#f6f8fa', '--panel2': '#ffffff', '--panel3': '#e6eaef', '--line': '#d8dee4', '--line2': '#afb8c1', '--text': '#1f2328', '--dim': '#656d76', '--faint': '#8c959f', '--accent': '#0969da', '--accent-ink': '#ffffff', '--blue': '#0550ae', '--good': '#1a7f37', '--warn': '#9a6700', '--bad': '#cf222e', '--view-bg': '#ffffff' } },
	];
	for (const p of PRESETS) {
		if (p.t) {
			p.vars = tokens(p.t);
		}
		if (p.vars && !p.swatch) {
			p.swatch = [p.vars['--panel'], p.vars['--accent']];
		}
	}

	// ---- colours ----
	let probe = null;
	function probeEl() {
		if (!probe) {
			probe = document.createElement('i');
			probe.style.cssText = 'position:fixed;left:-99px;top:0;width:0;height:0;visibility:hidden';
			document.documentElement.appendChild(probe);
		}
		return probe;
	}
	// Any CSS colour (hsl(), var(--x), color-mix()) as [r, g, b] 0..255.
	function toRGB(value) {
		const el = probeEl();
		el.style.color = '';
		el.style.color = value;
		const m = (getComputedStyle(el).color || '').match(/-?[\d.]+/g);
		if (!m || m.length < 3) {
			return [136, 136, 136];
		}
		const scale = /^color\(/.test(getComputedStyle(el).color) ? 255 : 1;
		return m.slice(0, 3).map((n) => Math.max(0, Math.min(255, Math.round(Number(n) * scale))));
	}
	const toHex = (rgbArr) => '#' + rgbArr.map((n) => n.toString(16).padStart(2, '0')).join('');
	const luminance = ([r, g, b]) => (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255;
	const validColour = (v) => typeof v === 'string' && v.length < 80 && !/var\(|url\(|;/.test(v) && window.CSS && CSS.supports('color', v);

	// ---- state ----
	// active: a built-in id, a saved theme's id, or 'draft' (unsaved changes). user: [{ id, name, vars }].
	let state = { active: 'studio', draft: null, user: [], ui: Object.assign({}, DEFAULT_UI) };
	let cache = null;
	const listeners = [];

	function cleanVars(vars) {
		const out = {};
		for (const k of Object.keys(vars || {})) {
			if (TOKEN_SET.has(k) && validColour(vars[k])) {
				out[k] = vars[k];
			}
		}
		return out;
	}
	function cleanUI(ui) {
		const u = Object.assign({}, DEFAULT_UI, ui || {});
		u.font = Math.max(10, Math.min(20, Number(u.font) || DEFAULT_UI.font));
		u.code = Math.max(8, Math.min(32, Number(u.code) || DEFAULT_UI.code));
		u.spacing = SPACING[u.spacing] ? u.spacing : DEFAULT_UI.spacing;
		return u;
	}
	function load() {
		try {
			const s = JSON.parse(localStorage.getItem(KEY));
			if (s && typeof s === 'object') {
				if (s.preset) { // the first version: a preset id or a generated hue
					state.active = s.preset === 'custom' ? 'draft' : String(s.preset);
					if (s.preset === 'custom') {
						state.draft = { name: 'Custom', vars: generated(Number(s.hue) || 0, !!s.light) };
					}
				} else {
					state.active = String(s.active || 'studio');
					state.draft = s.draft && s.draft.vars ? { name: 'Custom', vars: cleanVars(s.draft.vars) } : null;
					state.user = (Array.isArray(s.user) ? s.user : []).filter((u) => u && u.id && u.name).map((u) => ({ id: String(u.id), name: String(u.name).slice(0, 40), vars: cleanVars(u.vars) }));
					state.ui = cleanUI(s.ui);
				}
			}
		} catch (e) { /* no storage: the default theme */ }
		if (!find(state.active) && !(state.active === 'draft' && state.draft)) {
			state.active = 'studio';
		}
	}
	const generated = (hue, light) => tokens({ surface: hue, accent: hue, sat: 0.18, accentSat: 0.85, light: !!light });
	function find(id) {
		return PRESETS.find((p) => p.id === id) || state.user.find((u) => u.id === id) || null;
	}
	const isBuiltin = (id) => PRESETS.some((p) => p.id === id);
	function activeVars() {
		if (state.active === 'draft') {
			return state.draft ? state.draft.vars : null;
		}
		const t = find(state.active);
		return t ? t.vars : null;
	}
	function activeName() {
		if (state.active === 'draft') {
			return 'Custom (not saved)';
		}
		const t = find(state.active);
		return t ? t.name : 'Studio';
	}

	function apply(save) {
		const root = document.documentElement;
		root.style.cssText = '';
		const vars = activeVars();
		let dark = true;
		if (vars) {
			for (const k of Object.keys(vars)) {
				root.style.setProperty(k, vars[k]);
			}
			dark = luminance(toRGB(vars['--bg'] || '#0d0f13')) < 0.5;
			root.style.setProperty('--ov-rgb', dark ? '255,255,255' : '0,0,0');
			root.style.setProperty('--shade-rgb', dark ? '0,0,0' : '255,255,255');
		}
		root.style.colorScheme = dark ? 'dark' : 'light';
		const ui = state.ui;
		if (ui.font !== DEFAULT_UI.font) {
			root.style.setProperty('--ui-font', ui.font + 'px');
		}
		if (ui.spacing !== DEFAULT_UI.spacing) {
			root.style.setProperty('--ui-lh', String(SPACING[ui.spacing]));
		}
		cache = null;
		if (save) {
			try {
				localStorage.setItem(KEY, JSON.stringify(state));
				localStorage.setItem(CSS_KEY, root.style.cssText);
			} catch (e) { /* not saved */ }
		}
		defineMonaco();
		for (const fn of listeners) {
			try { fn(); } catch (e) { console.error(e); }
		}
		if (MS.app && MS.app.requestDraw) {
			MS.app.requestDraw();
		}
		document.dispatchEvent(new CustomEvent('ms-theme'));
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
	function rgb(name) {
		const key = 'rgb:' + name;
		css(name);
		return cache.v[key] || (cache.v[key] = toRGB('var(' + name + ')').join(','));
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
		isLight: () => css('--shade-rgb') === '255,255,255',
		codeSize: () => state.ui.code,
		onChange: (fn) => listeners.push(fn),
		presets: PRESETS,
		get state() { return state; },
	};
	MS.theme = theme;

	// ---- the code editor ----
	let flip = false;
	function defineMonaco(monaco) {
		monaco = monaco || (MS.ide && MS.ide.monaco) || window.monaco;
		if (!monaco || !monaco.editor) {
			return;
		}
		const hex = (name) => toHex(toRGB('var(' + name + ')'));
		const mixHex = (a, b, pct) => toHex(toRGB('color-mix(in srgb, var(' + a + ') ' + pct + '%, var(' + b + '))'));
		const strip = (h) => h.slice(1);
		const def = {
			base: theme.isLight() ? 'vs' : 'vs-dark',
			inherit: true,
			rules: [
				{ token: 'key', foreground: strip(hex('--blue')) },
				{ token: 'type', foreground: strip(hex('--accent')) },
				{ token: 'string.path', foreground: strip(mixHex('--good', '--text', theme.isLight() ? 100 : 75)) },
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
		if (monaco.editor.getEditors) {
			for (const ed of monaco.editor.getEditors()) {
				ed.updateOptions({ fontSize: state.ui.code });
			}
		}
	}
	theme.defineMonaco = defineMonaco;

	// ---- changing things ----
	function commit() {
		apply(true);
	}
	theme.select = (id) => {
		if (find(id)) {
			state.active = id;
			commit();
		}
	};
	// Colours as they are on screen now, as hex: the start of any edit to a built-in theme.
	function currentHexes() {
		const out = {};
		for (const [k] of TOKENS) {
			if (k !== '--sel' && k !== '--focus') {
				out[k] = toHex(toRGB('var(' + k + ')'));
			}
		}
		return out;
	}
	// Edits go to your own theme in place, or to an unsaved "Custom" one forked from a built-in.
	function editable() {
		if (state.active === 'draft') {
			return state.draft;
		}
		const saved = state.user.find((u) => u.id === state.active);
		if (saved) {
			return saved;
		}
		state.draft = { name: 'Custom', vars: currentHexes() };
		state.active = 'draft';
		return state.draft;
	}
	theme.setColour = (token, value) => {
		if (!TOKEN_SET.has(token) || !validColour(value)) {
			return;
		}
		editable().vars[token] = value;
		commit();
	};
	theme.clearColour = (token) => {
		if (token === '--sel' || token === '--focus') {
			delete editable().vars[token];
			commit();
		}
	};
	theme.generate = (hue, light) => {
		state.draft = { name: 'Custom', vars: generated(hue, light) };
		state.active = 'draft';
		state.hue = hue;
		state.light = !!light;
		commit();
	};
	theme.setUI = (next) => {
		state.ui = cleanUI(Object.assign({}, state.ui, next));
		commit();
	};
	const newId = () => 'u' + Date.now().toString(36) + Math.floor(Math.random() * 1296).toString(36);
	function uniqueName(name) {
		const base = (name || 'My theme').trim().slice(0, 40) || 'My theme';
		const names = new Set(PRESETS.map((p) => p.name).concat(state.user.map((u) => u.name)));
		let n = base, i = 2;
		while (names.has(n)) {
			n = base + ' ' + i++;
		}
		return n;
	}
	// Saves the colours now showing as a theme of your own (also how a built-in theme is duplicated).
	theme.saveAs = (name) => {
		const vars = activeVars() ? cleanVars(activeVars()) : currentHexes();
		const t = { id: newId(), name: uniqueName(name), vars };
		state.user.push(t);
		state.active = t.id;
		state.draft = null;
		commit();
		return t;
	};
	theme.remove = (id) => {
		const i = state.user.findIndex((u) => u.id === id);
		if (i >= 0) {
			state.user.splice(i, 1);
			if (state.active === id) {
				state.active = 'studio';
			}
			commit();
		}
	};
	theme.rename = (id, name) => {
		const t = state.user.find((u) => u.id === id);
		if (t && name.trim()) {
			t.name = name.trim().slice(0, 40);
			commit();
		}
	};
	theme.reset = () => {
		state.active = 'studio';
		state.draft = null;
		state.ui = Object.assign({}, DEFAULT_UI);
		commit();
	};
	theme.exportJSON = () => {
		const vars = activeVars() ? cleanVars(activeVars()) : currentHexes();
		return JSON.stringify({ cortexStudioTheme: 1, name: activeName().replace(/ \(not saved\)$/, ''), colours: vars, ui: state.ui }, null, 2);
	};
	// Adds a theme from JSON text; throws a sentence the person can read when it isn't one.
	theme.importJSON = (text) => {
		let data;
		try {
			data = JSON.parse(text);
		} catch (e) {
			throw new Error('That is not a theme: it is not JSON.');
		}
		if (!data || data.cortexStudioTheme !== 1 || typeof data.colours !== 'object') {
			throw new Error('That is not a Studio theme (no "cortexStudioTheme" and "colours" in it).');
		}
		const vars = cleanVars(data.colours);
		if (!vars['--bg'] || !vars['--text'] || !vars['--accent']) {
			throw new Error('That theme is missing its background, text or accent colour.');
		}
		const t = { id: newId(), name: uniqueName(String(data.name || 'Imported')), vars };
		state.user.push(t);
		state.active = t.id;
		state.draft = null;
		if (data.ui) {
			state.ui = cleanUI(data.ui);
		}
		commit();
		return t;
	};

	// ---- the picker ----
	let panel = null, button = null;
	const mk = (tag, cls, text) => {
		const e = document.createElement(tag);
		if (cls) { e.className = cls; }
		if (text) { e.textContent = text; }
		return e;
	};
	function themeList() {
		const list = PRESETS.map((p) => ({ id: p.id, name: p.name, swatch: p.swatch }));
		for (const u of state.user) {
			const v = u.vars;
			list.push({ id: u.id, name: u.name, swatch: [v['--panel'] || v['--bg'], v['--accent']], mine: true });
		}
		if (state.draft && state.active === 'draft') {
			const v = state.draft.vars;
			list.push({ id: 'draft', name: 'Custom', swatch: [v['--panel'] || v['--bg'], v['--accent']], draft: true });
		}
		return list;
	}
	function syncPanel() {
		if (!panel || panel.hidden) {
			return;
		}
		// The theme grid
		const grid = panel.querySelector('.theme-grid');
		grid.replaceChildren();
		for (const t of themeList()) {
			const b = mk('button', 'theme-preset' + (t.id === state.active ? ' sel' : ''));
			b.title = t.name + (t.mine ? ' (yours)' : '');
			const sw = mk('span', 'theme-swatch');
			sw.style.background = 'linear-gradient(135deg,' + t.swatch[0] + ' 55%,' + t.swatch[1] + ' 55%)';
			b.append(sw, mk('span', 'theme-name', t.name));
			b.onclick = () => theme.select(t.id);
			grid.appendChild(b);
		}
		const mine = state.user.find((u) => u.id === state.active);
		panel.querySelector('.theme-current').textContent = activeName();
		panel.querySelector('.theme-del').hidden = !mine;
		panel.querySelector('.theme-rename').hidden = !mine;
		panel.querySelector('.theme-light').checked = !!state.light;
		if (typeof state.hue === 'number') {
			panel.querySelector('.theme-hue').value = state.hue;
		}
		// Colour rows: only rebuild when the set of values changed from outside (not while dragging a picker)
		for (const row of panel.querySelectorAll('.theme-colour')) {
			const token = row.dataset.token;
			const hex = toHex(toRGB('var(' + token + ')'));
			const input = row.querySelector('input[type=color]');
			if (input !== document.activeElement) {
				input.value = hex;
			}
			row.querySelector('.theme-hex').textContent = hex;
			const opt = token === '--sel' || token === '--focus';
			const set = activeVars() && activeVars()[token];
			row.querySelector('.theme-unset').hidden = !(opt && set);
			row.classList.toggle('auto', opt && !set);
		}
		const ui = state.ui;
		panel.querySelector('.theme-font').value = ui.font;
		panel.querySelector('.theme-font-n').textContent = ui.font + ' px';
		panel.querySelector('.theme-code').value = ui.code;
		panel.querySelector('.theme-code-n').textContent = ui.code + ' px';
		panel.querySelector('.theme-spacing').value = ui.spacing;
	}
	function section(title) {
		const s = mk('div', 'theme-sec');
		s.appendChild(mk('div', 'theme-sub', title));
		return s;
	}
	function build() {
		panel = mk('div', 'ctx-menu theme-panel');
		panel.hidden = true;
		const head = mk('div', 'theme-head');
		head.append(mk('span', '', 'Theme: '), mk('b', 'theme-current'));
		panel.appendChild(head);
		const body = mk('div', 'theme-body');
		panel.appendChild(body);

		body.appendChild(mk('div', 'theme-grid'));

		// Save, duplicate, rename, delete
		const mgmt = mk('div', 'theme-sec theme-mgmt');
		const nameIn = mk('input', 'theme-name-in text');
		nameIn.placeholder = 'Name for a theme of your own';
		nameIn.maxLength = 40;
		const save = mk('button', 'tb', 'Save as new');
		save.title = 'Keep the colours showing now as a theme of your own. From a built-in theme this makes a copy to change.';
		save.onclick = () => {
			theme.saveAs(nameIn.value || (state.active === 'draft' ? 'My theme' : activeName() + ' copy'));
			nameIn.value = '';
		};
		const rename = mk('button', 'tb theme-rename', 'Rename');
		rename.onclick = () => {
			if (nameIn.value.trim()) {
				theme.rename(state.active, nameIn.value);
				nameIn.value = '';
			} else {
				nameIn.focus();
			}
		};
		const del = mk('button', 'tb theme-del', 'Delete');
		del.onclick = () => theme.remove(state.active);
		mgmt.append(nameIn, save, rename, del);
		body.appendChild(mgmt);

		// Generate from a hue
		const gen = section('Start from a hue');
		const row = mk('div', 'theme-hue-row');
		const hueNow = () => (typeof state.hue === 'number' ? state.hue : 40);
		const step = (d) => theme.generate((((hueNow() + d) % 360) + 360) % 360, !!state.light);
		const prev = mk('button', 'tb icon', '◀');
		prev.title = 'Previous hue';
		prev.onclick = () => step(-15);
		const next = mk('button', 'tb icon', '▶');
		next.title = 'Next hue';
		next.onclick = () => step(15);
		const hue = mk('input', 'theme-hue');
		hue.type = 'range';
		hue.min = 0;
		hue.max = 359;
		hue.value = 40;
		hue.oninput = () => theme.generate(Number(hue.value), !!state.light);
		row.append(prev, hue, next);
		gen.appendChild(row);
		const lab = mk('label', 'theme-light-row');
		const cb = mk('input', 'theme-light');
		cb.type = 'checkbox';
		cb.onchange = () => theme.generate(hueNow(), cb.checked);
		lab.append(cb, document.createTextNode(' Light'));
		gen.appendChild(lab);
		body.appendChild(gen);

		// Colours
		const cols = section('Colours');
		for (const [token, name, help] of TOKENS) {
			const r = mk('div', 'theme-colour');
			r.dataset.token = token;
			r.title = help;
			const input = mk('input');
			input.type = 'color';
			input.oninput = () => theme.setColour(token, input.value);
			const un = mk('button', 'theme-unset', '↺');
			un.title = 'Go back to following ' + (token === '--sel' ? 'the hover colour' : 'the accent');
			un.onclick = () => theme.clearColour(token);
			r.append(input, mk('span', 'theme-cname', name), mk('span', 'theme-hex'), un);
			cols.appendChild(r);
		}
		body.appendChild(cols);

		// Text and layout
		const txt = section('Text and spacing');
		const slider = (cls, label, min, max, onchange) => {
			const l = mk('label', 'theme-range');
			const i = mk('input', cls);
			i.type = 'range';
			i.min = min;
			i.max = max;
			i.step = 1;
			i.oninput = () => onchange(Number(i.value));
			l.append(mk('span', 'theme-rname', label), i, mk('span', cls + '-n theme-n'));
			return l;
		};
		txt.appendChild(slider('theme-font', 'Text size', 11, 18, (v) => theme.setUI({ font: v })));
		const sp = mk('label', 'theme-range');
		const sel = mk('select', 'theme-spacing');
		for (const [id, label] of [['compact', 'Compact'], ['comfortable', 'Comfortable'], ['roomy', 'Roomy']]) {
			const o = mk('option', '', label);
			o.value = id;
			sel.appendChild(o);
		}
		sel.onchange = () => theme.setUI({ spacing: sel.value });
		sp.append(mk('span', 'theme-rname', 'Spacing'), sel);
		txt.appendChild(sp);
		txt.appendChild(slider('theme-code', 'Code size', 9, 28, (v) => theme.setUI({ code: v })));
		body.appendChild(txt);

		// Share
		const share = section('Share a theme');
		const area = mk('textarea', 'theme-json');
		area.rows = 4;
		area.spellcheck = false;
		area.placeholder = 'Export fills this with the theme. To import, paste a theme here.';
		const msg = mk('div', 'theme-msg');
		const ex = mk('button', 'tb', 'Export');
		ex.title = 'Put the theme showing now into the box, and copy it';
		ex.onclick = async () => {
			area.value = theme.exportJSON();
			area.select();
			try {
				await navigator.clipboard.writeText(area.value);
				msg.textContent = 'Copied.';
			} catch (e) {
				msg.textContent = 'Select the text and copy it.';
			}
		};
		const im = mk('button', 'tb', 'Import');
		im.title = 'Add the theme pasted in the box to your themes';
		im.onclick = () => {
			try {
				const t = theme.importJSON(area.value);
				msg.textContent = 'Added "' + t.name + '".';
				area.value = '';
			} catch (e) {
				msg.textContent = e.message;
			}
		};
		const bar = mk('div', 'theme-share-bar');
		bar.append(ex, im);
		share.append(area, bar, msg);
		body.appendChild(share);

		const foot = mk('div', 'theme-sec theme-foot');
		const reset = mk('button', 'tb', 'Reset to the default');
		reset.title = 'Back to the Studio theme with the usual text size and spacing. Your saved themes stay.';
		reset.onclick = () => theme.reset();
		foot.appendChild(reset);
		body.appendChild(foot);

		document.body.appendChild(panel);

		const close = () => { panel.hidden = true; };
		document.addEventListener('mousedown', (e) => {
			if (!panel.hidden && !panel.contains(e.target) && !(button && button.contains(e.target))) {
				close();
			}
		}, true);
		document.addEventListener('keydown', (e) => {
			if (e.key === 'Escape' && !panel.hidden) {
				close();
			}
		});
		window.addEventListener('blur', (e) => {
			// Native colour pickers take focus away from the page: stay open then.
			if (!panel.contains(document.activeElement)) {
				close();
			}
		});
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
		button.title = 'Theme: colours, text size and spacing';
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
			panel.style.maxHeight = innerHeight - r.bottom - 16 + 'px';
			syncPanel();
		};
		help.parentNode.insertBefore(button, help);
	}

	load();
	apply(false);
	if (document.readyState === 'loading') {
		document.addEventListener('DOMContentLoaded', mountButton);
	} else {
		mountButton();
	}
})();
