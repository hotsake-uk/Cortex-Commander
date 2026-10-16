// Cortex Mod Studio: window zoom, kept between runs. In the Cortex Studio app, Ctrl (Cmd on macOS) with + or -, 0, or the mouse wheel zooms the
// window as in a browser, and the level is remembered: the page works out the new zoom and asks Studio's server (/api/zoom), which applies it to
// the window and saves it. In a plain browser, or the headless file bridge, there is no window to zoom and the browser's own zoom is left alone.
'use strict';
window.MS = window.MS || {};

(function () {
	// The steps a browser goes through.
	const STEPS = [0.5, 0.67, 0.75, 0.8, 0.9, 1, 1.1, 1.25, 1.5, 1.75, 2, 2.5, 3];
	let factor = 1;
	let wheel = 0;

	const headers = () => ({ 'X-ModStudio-Token': MS.app.fs.server, 'Content-Type': 'application/json' });

	async function ask(method, body) {
		const response = await fetch('/api/zoom', { method, headers: headers(), body: body ? JSON.stringify(body) : undefined, cache: 'no-store' });
		return response.ok ? response.json() : null;
	}

	function nearest() {
		let best = 0;
		STEPS.forEach((s, i) => {
			if (Math.abs(s - factor) < Math.abs(STEPS[best] - factor)) {
				best = i;
			}
		});
		return best;
	}

	async function set(next) {
		const answer = await ask('PUT', { factor: next }).catch(() => null);
		if (answer && answer.native) {
			factor = answer.factor;
			if (MS.app && MS.app.status) {
				MS.app.status('Zoom ' + Math.round(factor * 100) + '%');
			}
		}
	}

	function step(direction) {
		const i = Math.max(0, Math.min(STEPS.length - 1, nearest() + direction));
		set(STEPS[i]);
	}

	function install() {
		window.addEventListener('keydown', (e) => {
			if (!(e.ctrlKey || e.metaKey) || e.altKey) {
				return;
			}
			if (e.key === '+' || e.key === '=') {
				step(1);
			} else if (e.key === '-' || e.key === '_') {
				step(-1);
			} else if (e.key === '0') {
				set(1);
			} else {
				return;
			}
			e.preventDefault();
			e.stopPropagation();
		}, true);
		window.addEventListener('wheel', (e) => {
			if (!(e.ctrlKey || e.metaKey)) {
				return;
			}
			e.preventDefault();
			// A touchpad pinch sends many small turns: one step for each bit of travel.
			wheel += e.deltaY;
			if (Math.abs(wheel) >= 40) {
				step(wheel < 0 ? 1 : -1);
				wheel = 0;
			}
		}, { passive: false, capture: true });
	}

	// Waits for the page to know Studio's server, then asks whether there is a window to zoom.
	let tries = 0;
	const start = async () => {
		if (!MS.app || !MS.app.fs || !MS.app.fs.server) {
			if (tries++ < 100) {
				setTimeout(start, 200);
			}
			return;
		}
		const answer = await ask('GET').catch(() => null);
		if (answer && answer.native) {
			factor = answer.factor;
			install();
		}
	};
	start();
})();
