// Cortex Mod Studio: the Weapons data editor (Data view). A gun is a chain: the gun (HDFirearm) holds a magazine, the magazine holds rounds, a round
// fires particles, and each link has numbers that make the weapon what it is. This editor shows the chain in one place, whichever mod each link
// lives in, and lets it be changed without touching the shared links:
//   Balance table  every gun side by side: fire rate, reload, magazine, round, speed, what each shot carries, how far it flies
//   Gun            the chain as fields: gun, magazine, round, particle, with a ballistics picture of one shot flying
// A change goes into the gun's own block, as an override of the link it copies (Magazine = Magazine, CopyOf = ..., RoundCount = 40), so other
// guns that share the magazine or round are left as they are. The game's own guns are read only until copied into the mod (Make my own copy).
// The figures are Studio's estimates from the numbers (see ballistics): good for comparing guns, not a promise about the game's physics.
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
	const locked = (doc) => app().lib.isProtected(doc);
	const GUNS = new Set(['HDFirearm']);
	const PX_PER_M = 20; // the game's scale: 20 pixels to a metre
	const FRAME = 1 / 60;

	// ---- the numbers ----

	// What a link in the chain reads as, and where its fields are written. Each field: [key, label, help, fallback shown when nothing says].
	const GUN_FIELDS = [
		['RateOfFire', 'Fire rate', 'Rounds per minute', 300],
		['ReloadTime', 'Reload (ms)', 'How long a reload takes', 1500],
		['ShakeRange', 'Shake', 'How far the aim wanders (radians, in degrees of spread)', 6],
		['SharpShakeRange', 'Shake when aiming', 'The same when sighting', 2],
		['SharpLength', 'Sight range (px)', 'How far it can be aimed along', 100],
		['Mass', 'Weight', 'In kg', 5],
	];
	const MAG_FIELDS = [
		['RoundCount', 'Rounds', 'In a full magazine (0 or less: never runs out)', 30],
		['Mass', 'Weight', 'Of the magazine', 1],
	];
	const ROUND_FIELDS = [
		['FireVelocity', 'Speed', 'Metres per second as it leaves', 30],
		['ParticleCount', 'Particles', 'Fired at once (a shotgun fires many)', 1],
		['Separation', 'Spacing', 'How far apart the particles start (px)', 5],
	];
	const PARTICLE_FIELDS = [
		['Mass', 'Mass', 'Heavier hits harder', 0.01],
		['Sharpness', 'Sharpness', 'How well it goes through things', 1],
		['AirResistance', 'Air drag', 'Slows it each frame', 0.01],
		['LifeTime', 'Life (ms)', 'How long it flies before it goes (0 is forever)', 0],
		['GlobalAccScalar', 'Gravity', 'How much gravity pulls it (1 is normal, 0 flies straight)', 1],
	];

	// A gun's chain, as resolved: each link's entity (or null).
	function chainOf(p) {
		const lib = app().lib;
		const gun = lib.resolve(p.node);
		const magazine = gun.sub('Magazine');
		const round = magazine ? magazine.sub('RegularRound') || magazine.sub('Round') : null;
		const particle = round ? round.sub('Particle') : null;
		return { gun, magazine, round, particle };
	}

	// Metres per second and gravity into a path: positions every frame from the muzzle, level.
	function fly(round, particle, angle, g) {
		const v = (round ? round.num('FireVelocity', 30) : 30) * PX_PER_M;
		const drag = particle ? particle.num('AirResistance', 0) : 0;
		const lifeMs = particle ? particle.num('LifeTime', 0) : 0;
		const accScalar = particle ? particle.num('GlobalAccScalar', 1) : 1;
		const points = [];
		let x = 0;
		let y = 0;
		let vx = Math.cos(angle) * v;
		let vy = -Math.sin(angle) * v;
		const frames = lifeMs > 0 ? Math.ceil(lifeMs / 1000 / FRAME) : 600;
		for (let i = 0; i < Math.min(frames, 1500); i++) {
			points.push({ x, y, t: i * FRAME });
			x += vx * FRAME;
			y += vy * FRAME;
			vx *= 1 - drag;
			vy = vy * (1 - drag) + g * PX_PER_M * accScalar * FRAME;
			if (Math.hypot(vx, vy) < 8) {
				break; // it has stopped
			}
		}
		return points;
	}

	function statsOf(p, g) {
		const c = chainOf(p);
		const rate = c.gun.num('RateOfFire', 0);
		const mag = c.magazine ? c.magazine.num('RoundCount', 0) : 0;
		const velocity = c.round ? c.round.num('FireVelocity', 0) : 0;
		const count = c.round ? c.round.num('ParticleCount', 1) : 0;
		const mass = c.particle ? c.particle.num('Mass', 0) : 0;
		const sharp = c.particle ? c.particle.num('Sharpness', 0) : 0;
		const punch = mass * velocity * count;
		const level = c.round ? fly(c.round, c.particle, 0, g) : [];
		const reach = level.length ? Math.round(level[level.length - 1].x) : 0;
		const at400 = level.find((pt) => pt.x >= 400);
		return {
			chain: c,
			rate,
			reload: c.gun.num('ReloadTime', 0),
			mag,
			velocity,
			count,
			mass,
			sharp,
			punch,
			perSecond: punch * (rate / 60),
			magSeconds: rate > 0 && mag > 0 ? mag / (rate / 60) : 0,
			reach,
			drop400: at400 ? Math.round(at400.y) : null,
			ammo: c.magazine ? (c.magazine.str('PresetName', '') || '') : '',
			roundName: c.round ? c.round.str('PresetName', '') : '',
		};
	}

	const sceneGravity = () => 20; // the game's usual; scenes can change it

	// ---- changing the chain ----

	// The block the gun's own change goes in, made level by level: each missing link becomes "Key = Class / CopyOf = what it copied", so only the
	// changed number is the gun's own. Returns the node to set values on, or null if a link can't be copied (written inline in the original).
	function overrideNode(p, levels) {
		const lib = app().lib;
		let node = p.node;
		for (const key of levels) {
			let child = node.child(key);
			if (!child) {
				const entity = lib.resolve(node);
				const entry = entity.last(key);
				if (!entry) {
					return null;
				}
				const copy = entry.node.child('CopyOf');
				const ref = copy ? copy.value : entry.node.child('PresetName') ? lib.reference(entry.node.child('PresetName').value, p.module, p.module) : null;
				if (!ref) {
					return null;
				}
				child = node.add(key, entry.node.value);
				child.add('CopyOf', ref);
			}
			node = child;
		}
		return node;
	}

	// The levels below the gun for each link ("RegularRound" or "Round" for the round, whichever the magazine uses).
	function levelsFor(link, c) {
		const roundKey = c.magazine && c.magazine.last('RegularRound') ? 'RegularRound' : 'Round';
		return { gun: [], magazine: ['Magazine'], round: ['Magazine', roundKey], particle: ['Magazine', roundKey, 'Particle'] }[link];
	}

	function setField(p, c, link, key, label, value) {
		const text = String(value).trim();
		app().edit('Change ' + p.name + '\'s ' + label.toLowerCase(), () => {
			const node = overrideNode(p, levelsFor(link, c));
			if (!node) {
				app().toast('This link is written inline in the original gun, so Studio can\'t override it from here: use Its lines to change it.', 'bad');
				return;
			}
			if (text === '') {
				const old = node.child(key);
				if (old) {
					old.remove();
				}
			} else {
				node.set(key, text);
			}
		});
	}

	// ---- the editor ----

	function render(module) {
		return (host, tab) => {
			const a = app();
			if (!a.lib || !a.lib.presets.length) {
				host.replaceChildren(el('p', { className: 'ed ed-empty', textContent: 'Studio is still reading the game…' }));
				return;
			}
			const s = tab.state || (tab.state = { filter: '', selected: null, view: 'table', sort: 'perSecond', desc: true });
			const guns = a.lib.presets.filter((p) => p.topLevel && p.module === module && GUNS.has(p.cls)).sort((x, y) => x.name.localeCompare(y.name));
			const keyOf = (p) => p.doc.path + ':' + p.node.line + ':' + p.name;
			const q = s.filter.trim().toLowerCase();
			const shown = guns.filter((p) => !q || p.name.toLowerCase().includes(q));
			const filter = el('input', { type: 'search', placeholder: 'Filter ' + guns.length + ' guns…', value: s.filter, className: 'ed-input' });
			filter.dataset.fk = 'filter';
			filter.oninput = () => {
				s.filter = filter.value;
				clearTimeout(s.timer);
				s.timer = setTimeout(() => MS.ide.refreshForm(), 200);
			};
			const chip = (id, text) => el('button', { className: s.view === id ? 'sel' : '', textContent: text, onclick: () => { s.view = id; MS.ide.refreshForm(); } });
			const selected = guns.find((p) => keyOf(p) === s.selected) || shown[0] || null;
			host.replaceChildren(el('div', { className: 'ed ed-split' },
				el('div', { className: 'ed-head' }, el('h2', { textContent: 'Weapons · ' + module }), el('span', { className: 'chips' }, chip('table', 'Balance table'), chip('sounds', 'Sounds'), chip('gun', 'One gun')), el('span', { className: 'spacer' }), filter),
				guns[0] && locked(guns[0].doc) ? el('div', { className: 'ed-banner', textContent: module + ' is read only: guns can be compared here. Make my own copy of one to change it.' }) : null,
				s.view === 'table' ? table(shown, s, keyOf) : s.view === 'sounds' ? soundTable(shown, s, keyOf) : gunView(shown, selected, s, keyOf, module)));
		};
	}

	const COLUMNS = [
		['rate', 'Fire rate', 'Rounds per minute'],
		['reload', 'Reload', 'Milliseconds'],
		['mag', 'Mag', 'Rounds in a magazine'],
		['magSeconds', 'Mag lasts', 'Seconds of continuous fire'],
		['velocity', 'Speed', 'Metres per second'],
		['count', 'Per shot', 'Particles in a shot'],
		['punch', 'Punch', 'Mass × speed × particles: how much a shot carries'],
		['perSecond', 'Per second', 'Punch × shots a second: sustained output'],
		['reach', 'Reach', 'Pixels a level shot flies before it stops or is gone (estimate)'],
		['drop400', 'Drop @400', 'How far a level shot has fallen by 400 px (estimate)'],
	];

	function table(guns, s, keyOf) {
		const g = sceneGravity();
		const rows = guns.map((p) => ({ p, st: statsOf(p, g) }));
		const value = (r, key) => (r.st[key] === null || r.st[key] === undefined ? -1 : r.st[key]);
		rows.sort((x, y) => (s.sort === 'name' ? x.p.name.localeCompare(y.p.name) : value(x, s.sort) - value(y, s.sort)) * (s.desc ? -1 : 1));
		const max = {};
		for (const [key] of COLUMNS) {
			max[key] = Math.max(1e-9, ...rows.map((r) => value(r, key)));
		}
		const head = el('tr', {}, el('th', { textContent: 'Gun', onclick: () => sortBy(s, 'name') }), el('th', { textContent: 'Round' }),
			...COLUMNS.map(([key, text, help]) => el('th', { textContent: text + (s.sort === key ? (s.desc ? ' ▼' : ' ▲') : ''), title: help, className: 'wn', onclick: () => sortBy(s, key) })));
		const body = rows.map((r) => el('tr', { onclick: () => { s.selected = keyOf(r.p); s.view = 'gun'; MS.ide.refreshForm(); } },
			el('td', { textContent: r.p.name }), el('td', { className: 'dim', textContent: r.st.roundName.replace(/^Round /, '') }),
			...COLUMNS.map(([key]) => {
				const v = r.st[key];
				const cell = el('td', { className: 'wn', textContent: v === null || v === undefined || (v === 0 && key !== 'count') ? '·' : Math.abs(v) < 10 && v % 1 ? v.toFixed(2) : String(Math.round(v)) });
				if (v > 0 && key !== 'reload' && key !== 'drop400') {
					cell.style.background = 'rgba(242, 182, 61, ' + (0.05 + 0.3 * (v / max[key])).toFixed(2) + ')';
				}
				return cell;
			})));
		return el('div', { className: 'ed-detail wide' },
			el('p', { className: 'ed-note', textContent: 'Estimates from each gun\'s numbers, for comparing guns. Click a heading to sort, a row to open the gun. Warmer cells are the bigger numbers.' }),
			guns.length ? el('table', { className: 'wtable' }, el('thead', {}, head), el('tbody', {}, ...body)) : el('p', { className: 'ed-empty', textContent: 'No guns in this mod.' }));
	}

	// ---- the sounds of every gun side by side ----

	const SOUND_SLOTS = [['FireSound', 'Fire'], ['EmptySound', 'Empty'], ['ReloadStartSound', 'Reload start'], ['ReloadEndSound', 'Reload end']];

	// One gun's sounds: for each slot the container (or null), its files, and the numbers that say how it is heard.
	function soundsOf(p) {
		const gun = app().lib.resolve(p.node);
		const out = {};
		for (const [key] of SOUND_SLOTS) {
			const sound = gun.sub(key);
			out[key] = sound ? { sound, files: MS.editors.soundFiles(sound) } : null;
		}
		const fire = out.FireSound && out.FireSound.sound;
		return {
			slots: out,
			variants: out.FireSound ? out.FireSound.files.length : 0,
			reach: fire ? Number(fire.str('AttenuationStartDistance', '0')) || 0 : 0,
			volume: fire ? Number(fire.str('Volume', '1')) || 1 : 0,
			pitch: fire ? Number(fire.str('Pitch', '1')) || 1 : 0,
			pitchVar: fire ? Number(fire.str('PitchVariation', '0')) || 0 : 0,
			priority: fire ? Number(fire.str('Priority', '128')) || 0 : 0,
		};
	}

	const SOUND_COLUMNS = [
		['variants', 'Fire variants', 'Different recordings the fire sound picks from'],
		['reach', 'Heard from', 'Pixels it carries at full volume before it fades (AttenuationStartDistance)'],
		['volume', 'Volume', '1 is as recorded'],
		['pitch', 'Pitch', '1 is as recorded'],
		['pitchVar', 'Pitch var', 'Random change each shot'],
		['priority', 'Priority', '0 is most important, 256 least'],
	];

	function soundTable(guns, s, keyOf) {
		const rows = guns.map((p) => ({ p, snd: soundsOf(p) }));
		const key = s.soundSort || 'reach';
		const value = (r) => (key === 'name' ? 0 : r.snd[key] || 0);
		rows.sort((x, y) => (key === 'name' ? x.p.name.localeCompare(y.p.name) : value(x) - value(y)) * (s.soundDesc === false ? 1 : -1));
		const sort = (k) => {
			s.soundDesc = s.soundSort === k ? !(s.soundDesc !== false) : k !== 'name';
			s.soundSort = k;
			MS.ide.refreshForm();
		};
		const max = {};
		for (const [k] of SOUND_COLUMNS) {
			max[k] = Math.max(1e-9, ...rows.map((r) => r.snd[k] || 0));
		}
		const head = el('tr', {}, el('th', { textContent: 'Gun', onclick: () => sort('name') }),
			...SOUND_COLUMNS.map(([k, text, help]) => el('th', { textContent: text + (key === k ? (s.soundDesc === false ? ' ▲' : ' ▼') : ''), title: help, className: 'wn', onclick: () => sort(k) })),
			...SOUND_SLOTS.map(([k, text]) => el('th', { textContent: text, className: 'wn', title: 'Click ▶ to hear it' })));
		const body = rows.map((r) => el('tr', { onclick: () => { s.selected = keyOf(r.p); s.view = 'gun'; MS.ide.refreshForm(); } },
			el('td', { textContent: r.p.name }),
			...SOUND_COLUMNS.map(([k]) => {
				const v = r.snd[k];
				const cell = el('td', { className: 'wn', textContent: v ? (v % 1 ? v.toFixed(2) : String(v)) : '·' });
				if (v > 0) {
					cell.style.background = 'rgba(91, 192, 235, ' + (0.05 + 0.3 * (v / max[k])).toFixed(2) + ')';
				}
				return cell;
			}),
			...SOUND_SLOTS.map(([k]) => {
				const slot = r.snd.slots[k];
				const cell = el('td', { className: 'wn' });
				if (slot && slot.files.length) {
					cell.append(el('button', { className: 'ico-btn', textContent: '▶', title: slot.files.length + ' file' + (slot.files.length > 1 ? 's' : '') + ': click to hear one', onclick: (e) => {
						e.stopPropagation();
						MS.editors.play(slot.files[Math.floor(Math.random() * slot.files.length)].path, slot.sound);
					} }));
				} else {
					cell.textContent = '·';
				}
				return cell;
			})));
		return el('div', { className: 'ed-detail wide' },
			el('p', { className: 'ed-note', textContent: 'How each gun sounds. Bluer cells are bigger numbers; ▶ plays one of the recordings with its volume and pitch. Click a row to open the gun.' }),
			guns.length ? el('table', { className: 'wtable' }, el('thead', {}, head), el('tbody', {}, ...body)) : el('p', { className: 'ed-empty', textContent: 'No guns in this mod.' }));
	}

	function sortBy(s, key) {
		s.desc = s.sort === key ? !s.desc : key !== 'name';
		s.sort = key;
		MS.ide.refreshForm();
	}

	function gunView(shown, selected, s, keyOf, module) {
		const items = shown.slice(0, 500).map((p) => el('button', { className: 'ed-item' + (p === selected ? ' sel' : ''), textContent: p.name, onclick: () => { s.selected = keyOf(p); MS.ide.refreshForm(); } }));
		return el('div', { className: 'ed-panes' },
			el('div', { className: 'ed-list' }, ...(items.length ? items : [el('p', { className: 'ed-empty', textContent: 'No guns.' })])),
			el('div', { className: 'ed-detail' }, selected ? gunDetail(selected, module) : null));
	}

	function field(p, c, link, [key, label, help, fallback], entity, ro) {
		const own = entity ? entity.has(key) : false;
		const now = entity ? entity.str(key, '') : '';
		const input = el('input', { type: 'number', step: 'any', className: 'ed-input num', disabled: ro || !entity, value: now, placeholder: String(fallback) });
		input.onchange = () => setField(p, c, link, key, label, input.value);
		input.dataset.fk = link + ':' + key;
		return el('label', { className: 'ed-setting', title: help }, el('span', { textContent: label }), input, el('small', { textContent: own ? help : help + (now ? '' : ' · game default') }));
	}

	function gunDetail(p, module) {
		const a = app();
		const c = chainOf(p);
		const ro = locked(p.doc);
		const st = statsOf(p, sceneGravity());
		const link = (title, name, fields, which, entity) => el('div', { className: 'chain-link' },
			el('div', { className: 'chain-head' }, el('b', { textContent: title }), el('span', { className: 'chain-name', textContent: name || '(none)' })),
			entity ? el('div', { className: 'ed-settings' }, ...fields.map((f) => field(p, c, which, f, entity, ro))) : el('p', { className: 'ed-empty', textContent: 'This gun has no ' + title.toLowerCase() + '.' }));
		const used = a.lib.presets.filter((x) => x !== p && x.topLevel && x.cls === 'HDFirearm' && x.node.value && st.ammo && chainOf(x).magazine && chainOf(x).magazine.str('PresetName', '') === st.ammo).length;
		const own = !ro;
		const copy = el('button', { className: 'tb', textContent: 'Make my own copy…', title: 'Start a gun in your mod that copies this one', onclick: () => ownCopy(p) });
		return el('div', {},
			el('div', { className: 'ed-card-head' }, el('h3', { textContent: p.name }), el('span', { className: 'spacer' }),
				ro ? copy : null,
				el('button', { className: 'tb', textContent: 'Open in designer', onclick: () => a.openActor(p) }),
				el('button', { className: 'ico-btn', textContent: 'Its lines', onclick: () => MS.ide.openFile(p.doc.path, p.node.line) })),
			el('div', { className: 'ed-label', textContent: 'One shot' }), ballistics(c, st),
			el('div', { className: 'chain' },
				link('Gun', p.name, GUN_FIELDS, 'gun', c.gun),
				link('Magazine', st.ammo, MAG_FIELDS, 'magazine', c.magazine),
				link('Round', st.roundName, ROUND_FIELDS, 'round', c.round),
				link('Particle', c.particle ? c.particle.str('PresetName', '') || c.particle.value : '', PARTICLE_FIELDS, 'particle', c.particle)),
			own && used ? el('p', { className: 'ed-note', textContent: 'Changes here go in this gun\'s own block and leave the ' + used + ' other gun' + (used > 1 ? 's' : '') + ' using the same magazine alone.' }) : null);
	}

	// A picture of the shot: where it goes level and aimed a little up, with the distance along the bottom and the fall marked.
	function ballistics(c, st) {
		const g = sceneGravity();
		const canvas = el('canvas', { className: 'ballistic', width: 640, height: 190, title: 'A level shot (white), and one aimed 5° up (amber). Gridlines every 100 px.' });
		const ctx = canvas.getContext('2d');
		ctx.fillStyle = '#0d1117';
		ctx.fillRect(0, 0, canvas.width, canvas.height);
		if (!c.round) {
			return canvas;
		}
		const paths = [[0, '#ffffff'], [(5 * Math.PI) / 180, '#f2b63d'], [(-5 * Math.PI) / 180, '#5bc0eb']].map(([angle, color]) => ({ points: fly(c.round, c.particle, angle, g), color }));
		const maxX = Math.max(200, ...paths.map((r) => (r.points.length ? r.points[r.points.length - 1].x : 0)));
		const ys = paths.flatMap((r) => r.points.map((pt) => pt.y));
		const minY = Math.min(0, ...ys);
		const maxY = Math.max(40, ...ys);
		const pad = 18;
		const sx = (canvas.width - pad * 2) / maxX;
		const sy = Math.min(sx, (canvas.height - pad * 2) / (maxY - minY));
		const X = (x) => pad + x * sx;
		const Y = (y) => pad + (y - minY) * sy;
		ctx.strokeStyle = 'rgba(255,255,255,.08)';
		ctx.fillStyle = 'rgba(255,255,255,.4)';
		ctx.font = '10px sans-serif';
		for (let x = 0; x <= maxX; x += maxX > 1500 ? 500 : maxX > 600 ? 200 : 100) {
			ctx.beginPath();
			ctx.moveTo(X(x), 0);
			ctx.lineTo(X(x), canvas.height);
			ctx.stroke();
			ctx.fillText(x + ' px', X(x) + 3, canvas.height - 4);
		}
		for (const { points, color } of paths) {
			ctx.strokeStyle = color;
			ctx.lineWidth = 1.5;
			ctx.beginPath();
			points.forEach((pt, i) => (i ? ctx.lineTo(X(pt.x), Y(pt.y)) : ctx.moveTo(X(pt.x), Y(pt.y))));
			ctx.stroke();
		}
		ctx.fillStyle = '#fff';
		ctx.fillText('speed ' + st.velocity + ' m/s · reaches ' + st.reach + ' px' + (st.drop400 !== null ? ' · falls ' + st.drop400 + ' px by 400' : ''), 8, 12);
		return canvas;
	}

	// A copy of a locked gun in the project's mod.
	function ownCopy(p) {
		const a = app();
		const module = a.project || (a.lib.modules.find((m) => !m.official) || {}).name;
		if (!module) {
			a.toast('Choose the mod you are working on first (Project, at the top).', 'bad');
			return;
		}
		const name = el('input', { type: 'text', value: p.name + ' Mk2' });
		MS.dialogs.open('Make my own copy of ' + p.name, MS.dialogs.parts.field('Name', name, 'It goes in ' + module + ' and copies ' + p.name + '. Change what you like after.'), [
			{ text: 'Cancel' },
			{ text: 'Make it', primary: true, action: async () => {
				const n = name.value.trim();
				if (!n) {
					return false;
				}
				const file = n.replace(/[^\w -]/g, '').trim().replace(/\s+/g, '_') || 'MyGun';
				return MS.ide.makePresetFile(module + '/Devices/' + file + '.ini', 'gun', p.name);
			} },
		]);
	}

	if (MS.editors && MS.editors.register) {
		MS.editors.register(['weapons', 'Weapons', 'Guns end to end: balance table, rounds and ballistics', '⌖', render, (module) => app().lib.presets.filter((p) => p.topLevel && p.module === module && GUNS.has(p.cls)).length]);
	}
	MS.weapons = { render, statsOf, fly };
})();
