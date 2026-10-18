// Cortex Mod Studio: the game the mods are for (the target). Commander is this game. Classic is a release build of Cortex Command
// Community Edition (6.2.2, or the nightly), which has the official INI and Lua in Data/ but not the engine's source, so what its engine
// reads and offers to Lua comes with Studio, in data/targets/<id>/ (written by tools/make-targets.js). In Classic, Studio hides what only
// Commander's engine does (the live link to the game, missions, lights, Commander's keys) and says when a key is one only Commander reads.
'use strict';
window.MS = window.MS || {};

(function () {
	const TARGETS = [
		{ id: 'commander', name: 'Cortex Commander', short: 'Commander', classic: false, data: 'data/', supportedGameVersion: '6.0.0' },
		// 6.0.0 loads on any 6.x; the nightly is 7.0.0 and refuses a mod made for another major version
		{ id: 'ce-6.2.2', name: 'Community Edition 6.2.2', short: 'CE 6.2.2', classic: true, data: 'data/targets/ce-6.2.2/', supportedGameVersion: '6.0.0' },
		{ id: 'ce-nightly', name: 'Community Edition nightly', short: 'CE nightly', classic: true, data: 'data/targets/ce-nightly/', supportedGameVersion: '7.0.0' },
	];
	const byId = (id) => TARGETS.find((t) => t.id === id);
	const listeners = [];

	const T = {
		list: TARGETS,
		current: TARGETS[0],
		detected: TARGETS[0],
		surface: null, // the target's INI classes: {cls: {parent, properties}}
		commanderSurface: null,
	};
	MS.target = T;

	// settled once the first game folder has been looked at
	T.ready = new Promise((resolve) => {
		T.settle = resolve;
	});

	T.get = () => T.current;
	T.classic = () => !!T.current.classic;
	T.on = (fn) => listeners.push(fn);

	// What the game folder is: Commander has its own modules and Studio's test activity in Data; a Community Edition folder without
	// the nightly's shared AI behaviours is 6.2.2. A choice made in Settings for this folder wins.
	T.detect = async function (fs) {
		const has = async (path) => {
			try {
				return await fs.exists(path);
			} catch (e) {
				return false;
			}
		};
		const official = (name) => fs.moduleHome.has(name) && fs.moduleHome.get(name).home === 'Data';
		if (official('medieval.rte') || official('felllegion.rte') || (await has('Base.rte/Activities/ModStudioTest.lua'))) {
			T.detected = byId('commander');
		} else {
			T.detected = byId((await has('Base.rte/AI/SharedBehaviors.lua')) ? 'ce-nightly' : 'ce-6.2.2');
		}
		T.folder = fs.name || '';
		const chosen = byId(MS.settings.get('target.' + T.folder, ''));
		await T.use(chosen || T.detected);
		T.settle(T.current);
	};

	T.use = async function (target) {
		const changed = target !== T.current;
		T.current = target;
		document.body.classList.toggle('classic', !!target.classic);
		if (target.classic) {
			const load = (data) => fetch(data + 'ini-surface.json', { cache: 'no-cache' }).then((r) => (r.ok ? r.json() : null)).catch(() => null);
			const [own, commander] = await Promise.all([load(target.data), T.commanderSurface ? Promise.resolve({ classes: T.commanderSurface }) : load('data/')]);
			T.surface = own && own.classes;
			T.commanderSurface = commander && commander.classes;
		} else {
			T.surface = null;
		}
		const badge = document.getElementById('targetBadge');
		if (badge) {
			badge.hidden = !target.classic;
			badge.textContent = target.short;
			badge.onclick = () => MS.settings.open();
			badge.title = 'Making mods for ' + target.name + (target === T.detected ? ' (found in this game folder)' : ' (picked in Settings)') + '. Studio shows only what that game supports. Click to change it.';
		}
		if (changed) {
			for (const fn of listeners) {
				fn(target);
			}
		}
	};

	// Every property a class reads, its parents' included, in a surface; null for a class the surface doesn't have.
	function propertiesOf(surface, cls) {
		if (!surface || !surface[cls]) {
			return null;
		}
		const props = new Set();
		const seen = new Set();
		while (cls && surface[cls] && !seen.has(cls)) {
			seen.add(cls);
			for (const p of surface[cls].properties) {
				props.add(p);
			}
			cls = surface[cls].parent;
		}
		return props;
	}
	const cache = new Map();
	const cached = (which, surface, cls) => {
		const key = which + ':' + cls;
		if (!cache.has(key)) {
			cache.set(key, propertiesOf(surface, cls));
		}
		return cache.get(key);
	};
	T.on(() => cache.clear());

	// True when a key in a block of this class is one Commander's engine reads and the target's doesn't, so the target ignores it.
	// Only keys Commander knows count: a key neither knows is a typo, or one this way of reading the source misses, and isn't ours to judge.
	T.commanderOnly = function (cls, key) {
		if (!T.current.classic || !cls || !key) {
			return false;
		}
		const theirs = cached('t', T.surface, cls);
		const ours = cached('c', T.commanderSurface, cls);
		if (!ours || !ours.has(key)) {
			return false;
		}
		// A class only Commander has (AMech, ACShip...) is Commander's from top to bottom.
		return !theirs || !theirs.has(key);
	};

	// A class an INI block can name that the target's engine doesn't have.
	T.commanderOnlyClass = (cls) => !!(T.current.classic && T.surface && T.commanderSurface && cls && T.commanderSurface[cls] && !T.surface[cls]);

	// Settings: which game the mods are for.
	MS.settings.section({
		id: 'target',
		title: 'Game the mods are for',
		first: true,
		render(body, redraw) {
			const el = (tag, props, ...children) => {
				const e = document.createElement(tag);
				Object.assign(e, props || {});
				e.append(...children.filter((c) => c !== null && c !== undefined));
				return e;
			};
			const chosen = MS.settings.get('target.' + T.folder, '');
			body.append(el('p', { className: 'set-note', textContent: 'Which game your mods are for. Studio offers only what that game supports, and its autocomplete and checks follow that game\'s engine.' + (T.folder ? ' This is kept for the game folder ' + T.folder + '.' : '') }));
			const pick = (id) => {
				MS.settings.set('target.' + T.folder, id);
				T.use(byId(id) || T.detected).then(redraw);
			};
			const choice = (id, label, note) => {
				const input = el('input', { type: 'radio', name: 'studio-target', value: id, checked: id === chosen, onchange: () => pick(id) });
				return el('label', { className: 'set-choice' }, input, el('span', {}, el('b', { textContent: label }), note ? el('small', { textContent: note }) : null));
			};
			body.append(choice('', 'Found in the game folder', 'Now: ' + T.detected.name));
			for (const t of TARGETS) {
				body.append(choice(t.id, t.name, t.classic ? 'Classic: a release build of Community Edition, no live link to the game' : 'This game, with everything Studio does'));
			}
		},
	});
})();
