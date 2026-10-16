// Cortex Mod Studio: the Profiler panel (beside Console, Problems and Watch in Script mode). A game started from Studio with -modstudio
// writes ModStudio/Profile.ini twice a second (ModStudioPreview::UpdateProfile): frame, update and draw milliseconds, what's in the
// world, the engine's named timings and each script's Lua time a sim update. This reads it while the panel is open and shows the
// numbers, a graph of the last minute's frame times, where each frame's time goes, and the costliest scripts (click one to open it).
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

	const FILE = 'ModStudio/Profile.ini';
	const HISTORY = 120; // samples, at two a second: a minute
	const P = { timer: 0, history: [], paused: false, last: null, lastModified: 0 };

	function parse(text) {
		const out = { stages: [], scripts: [] };
		for (const line of text.split(/\r?\n/)) {
			const at = line.indexOf(' = ');
			if (at < 0 || line.startsWith('//')) {
				continue;
			}
			const key = line.slice(0, at);
			const value = line.slice(at + 3);
			if (key === 'Stage') {
				const [name, ms] = value.split('\t');
				out.stages.push({ name, ms: Number(ms) || 0 });
			} else if (key === 'Script') {
				const [path, ms, calls] = value.split('\t');
				out.scripts.push({ path, ms: Number(ms) || 0, calls: Number(calls) || 0 });
			} else {
				out[key] = Number(value);
			}
		}
		// The frame averages are whole milliseconds; the named timings for all of a frame's updating and drawing are exact.
		const stage = (name) => out.stages.find((s) => s.name === name);
		if (stage('Frame: all sim updates')) {
			out.Update = stage('Frame: all sim updates').ms;
		}
		if (stage('Frame: all drawing')) {
			out.Draw = stage('Frame: all drawing').ms;
		}
		return out;
	}

	async function poll() {
		clearTimeout(P.timer);
		if (MS.ide.panel !== 'profiler' || MS.ide.mode !== 'deep' || !app().fs || !app().fs.server) {
			return;
		}
		if (!P.paused) {
			try {
				const response = await app().fs.api('GET', 'file', FILE);
				if (response) {
					const modified = Number(response.headers.get('X-Last-Modified')) || 0;
					if (modified !== P.lastModified) {
						P.lastModified = modified;
						P.last = parse(await response.text());
						P.history.push({ frame: P.last.Frame, update: P.last.Update, draw: P.last.Draw });
						if (P.history.length > HISTORY) {
							P.history.shift();
						}
					}
				}
			} catch (e) {
				// no file yet: the game isn't running from Studio
			}
			render();
		}
		P.timer = setTimeout(poll, 500);
	}

	const fixed = (n, d) => (Number.isFinite(n) ? n.toFixed(d === undefined ? 1 : d) : '–');

	function render() {
		const body = $('profilerBody');
		const d = P.last;
		const stale = !P.lastModified || Date.now() - P.lastModified > 3000;
		const pause = el('button', { className: 'ico-btn', textContent: P.paused ? '▶ Resume' : '❚❚ Pause', title: 'Hold the numbers still to read them', onclick: () => { P.paused = !P.paused; render(); if (!P.paused) poll(); } });
		const clear = el('button', { className: 'ico-btn', textContent: 'Clear graph', onclick: () => { P.history = []; render(); } });
		const head = el('div', { className: 'prof-head' },
			el('span', { className: stale ? 'prof-state off' : 'prof-state on', textContent: stale ? '○ No game running from Studio' : '● Live' }),
			stale ? el('span', { className: 'prof-hint', textContent: 'Start one with ▶ Play ▾ › Test in game or Sandbox (they add -modstudio); this fills in as it runs.' }) : null,
			el('span', { className: 'spacer' }), pause, clear);
		if (!d) {
			body.replaceChildren(head);
			return;
		}
		const fps = d.Frame > 0 ? 1000 / d.Frame : NaN;
		const tile = (label, value, sub, bad) => el('div', { className: 'prof-tile' + (bad ? ' bad' : '') }, el('b', { textContent: value }), el('span', { textContent: label }), sub ? el('small', { textContent: sub }) : null);
		const tiles = el('div', { className: 'prof-tiles' },
			tile('frames a second', fixed(fps, 0), fixed(d.Frame) + ' ms a frame', fps < 50),
			tile('updating', fixed(d.Update) + ' ms', fixed(d.SimUpdate) + ' ms a sim update', d.Update > 12),
			tile('drawing', fixed(d.Draw) + ' ms', '', d.Draw > 12),
			tile('actors', fixed(d.Actors, 0)),
			tile('particles', fixed(d.Particles, 0)),
			tile('MOIDs', fixed(d.MOIDs, 0), 'of 255', d.MOIDs > 240),
			tile('sounds playing', fixed(d.SoundChannels, 0)));
		const graph = el('canvas', { className: 'prof-graph', height: 90 });
		const maxStage = Math.max(0.001, ...d.stages.map((s) => s.ms));
		const stages = el('div', { className: 'prof-list' }, el('div', { className: 'prof-list-head', textContent: 'Where a frame\'s time goes (ms)' }),
			...d.stages.slice(0, 16).map((s) => el('div', { className: 'prof-row', title: s.name }, el('span', { className: 'prof-name', textContent: s.name }), el('span', { className: 'prof-bar' }, el('i', { style: 'width:' + (100 * s.ms) / maxStage + '%' })), el('span', { className: 'prof-num', textContent: fixed(s.ms, 2) }))));
		const maxScript = Math.max(0.001, ...d.scripts.map((s) => s.ms));
		const scripts = el('div', { className: 'prof-list' }, el('div', { className: 'prof-list-head', textContent: 'Scripts: Lua time a sim update (ms) · calls' }),
			...(d.scripts.length
				? d.scripts.slice(0, 20).map((s) => el('div', { className: 'prof-row link', title: s.path + ' (click to open)', onclick: () => MS.ide.openFile(s.path) },
					el('span', { className: 'prof-name', textContent: s.path.split('/').pop() }), el('span', { className: 'prof-bar' }, el('i', { style: 'width:' + (100 * s.ms) / maxScript + '%' })), el('span', { className: 'prof-num', textContent: fixed(s.ms, 3) + ' · ' + fixed(s.calls, 0) })))
				: [el('div', { className: 'pempty', textContent: 'No scripts ran.' })]));
		body.replaceChildren(head, tiles, graph, el('div', { className: 'prof-cols' }, stages, scripts));
		requestAnimationFrame(() => drawGraph(graph));
	}

	function drawGraph(canvas) {
		const w = (canvas.width = canvas.clientWidth * devicePixelRatio);
		const h = (canvas.height = 90 * devicePixelRatio);
		const ctx = canvas.getContext('2d');
		const top = Math.max(20, ...P.history.map((s) => s.frame || 0)) * 1.15;
		const y = (ms) => h - (ms / top) * h;
		ctx.clearRect(0, 0, w, h);
		// 60 and 30 frames a second
		for (const [ms, label] of [[1000 / 60, '60 fps'], [1000 / 30, '30 fps']]) {
			if (ms < top) {
				ctx.strokeStyle = 'rgba(255,255,255,.12)';
				ctx.setLineDash([4, 4]);
				ctx.beginPath();
				ctx.moveTo(0, y(ms));
				ctx.lineTo(w, y(ms));
				ctx.stroke();
				ctx.setLineDash([]);
				ctx.fillStyle = 'rgba(255,255,255,.35)';
				ctx.font = 10 * devicePixelRatio + 'px sans-serif';
				ctx.fillText(label, 4, y(ms) - 3);
			}
		}
		const line = (key, colour) => {
			ctx.strokeStyle = colour;
			ctx.lineWidth = 1.5 * devicePixelRatio;
			ctx.beginPath();
			P.history.forEach((s, i) => {
				const x = w - ((P.history.length - 1 - i) / (HISTORY - 1)) * w;
				const v = s[key] || 0;
				i ? ctx.lineTo(x, y(v)) : ctx.moveTo(x, y(v));
			});
			ctx.stroke();
		};
		line('draw', '#7fd1ff');
		line('update', '#4fc37a');
		line('frame', '#f2b63d');
	}

	MS.profiler = {
		show() {
			render();
			poll();
		},
	};
})();
