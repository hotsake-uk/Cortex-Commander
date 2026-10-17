// Cortex Mod Studio: the Effects data editor (Data view): sparks, smoke, flames, explosions, muzzle flashes, anything the game spawns for show.
// Each effect is shown running, so a change to its numbers can be judged by looking:
//   Emitters (AEmitter)  what comes out of them: rate, bursts, spread and speed of each emission, with the Emit/Burst buttons
//   Particles            single sparks and puffs (MOPixel, MOSParticle): the colour or the frames, how long they last, their glow
// The preview is Studio's own small simulation (gravity, air drag, life, the particle's frames and its glow added on top), good for judging
// look and spread; the game decides the exact motion.
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
	const change = (label, fn) => app().edit(label, fn);
	const EMITTERS = new Set(['AEmitter']);
	const PARTICLES = new Set(['MOPixel', 'MOSParticle', 'MOSRotating']);
	const PPM = 20; // pixels to the metre
	const FRAME = 1 / 60;

	// ---- reading an effect ----

	// A particle (MOPixel / MOSParticle / MOSRotating) as the preview draws it.
	function particleOf(ent) {
		if (!ent) {
			return null;
		}
		const color = ent.sub('Color');
		const glow = ent.file('ScreenEffect');
		return {
			cls: ent.cls,
			name: ent.str('PresetName', ''),
			rgb: color ? [color.num('R', 255), color.num('G', 255), color.num('B', 255)] : [255, 255, 255],
			file: ent.cls === 'MOPixel' ? null : ent.file('SpriteFile'),
			frames: Math.max(1, Math.round(ent.num('FrameCount', 1))),
			offset: ent.vec('SpriteOffset', null),
			life: ent.num('LifeTime', 0),
			drag: ent.num('AirResistance', 0),
			gravity: ent.num('GlobalAccScalar', 1),
			glow,
			glowStrength: ent.num('EffectStartStrength', 0.5),
			glowAlways: ent.bool('EffectAlwaysShows', false),
		};
	}

	// An emitter's emissions. The emitter's own ParticlesPerMinute / BurstSize, when set, are shared out between its emissions (the game does the same).
	function emissionsOf(p) {
		const a = app();
		const ent = a.lib.resolve(p.node);
		const entries = ent.all('AddEmission');
		const n = Math.max(1, entries.length);
		const ownPpm = ent.has('ParticlesPerMinute') ? ent.num('ParticlesPerMinute', 0) / n : null;
		const ownBurst = ent.has('BurstSize') ? Math.ceil(ent.num('BurstSize', 0) / n) : null;
		return {
			ent,
			list: entries.map((entry) => {
				const em = a.lib.resolve(entry.node);
				return {
					node: entry.node,
					own: entry.node.isWithin(p.node),
					ppm: ownPpm !== null ? ownPpm : em.num('ParticlesPerMinute', 0),
					burst: ownBurst !== null ? ownBurst : em.num('BurstSize', 0),
					spread: em.num('Spread', 0),
					min: em.num('MinVelocity', 0),
					max: em.num('MaxVelocity', 0),
					lifeVar: em.num('LifeVariation', 0),
					offset: em.vec('Offset', { x: 0, y: 0 }),
					particle: particleOf(em.sub('EmittedParticle')),
				};
			}),
		};
	}

	// ---- the little simulation ----

	class Sim {
		constructor(setup) {
			this.setup = setup; // {emissions: [...], single: particle|null}
			this.particles = [];
			this.time = 0;
			this.owed = this.setup.emissions.map(() => 0);
			this.emitting = true;
		}

		spawn(emission, angle, count) {
			for (let i = 0; i < count; i++) {
				const v = (emission.min + Math.random() * (emission.max - emission.min)) * PPM;
				const a = angle + (Math.random() * 2 - 1) * emission.spread;
				const life = emission.particle && emission.particle.life ? emission.particle.life * (1 + (Math.random() * 2 - 1) * emission.lifeVar) : 0;
				this.particles.push({ x: emission.offset.x, y: emission.offset.y, vx: Math.cos(a) * v, vy: -Math.sin(a) * v, age: 0, life, p: emission.particle, spin: Math.random() * 6.28 });
			}
		}

		burst(angle) {
			this.setup.emissions.forEach((e) => e.particle && this.spawn(e, angle, Math.max(1, Math.round(e.burst))));
		}

		step(dt, angle, gravity) {
			this.time += dt;
			this.setup.emissions.forEach((e, i) => {
				if (this.emitting && e.ppm > 0 && e.particle) {
					this.owed[i] += (e.ppm / 60) * dt;
					const whole = Math.floor(this.owed[i]);
					if (whole > 0) {
						this.owed[i] -= whole;
						this.spawn(e, angle, whole);
					}
				}
			});
			for (const q of this.particles) {
				q.age += dt * 1000;
				q.x += q.vx * dt;
				q.y += q.vy * dt;
				const drag = q.p ? q.p.drag : 0;
				q.vx *= 1 - drag;
				q.vy = q.vy * (1 - drag) + (gravity ? 20 * PPM * (q.p ? q.p.gravity : 1) * dt : 0);
			}
			this.particles = this.particles.filter((q) => (q.life ? q.age < q.life : q.age < 3000) && Math.abs(q.x) < 400 && q.y < 300 && q.y > -400);
		}
	}

	// ---- the preview ----

	function drawSim(canvas, sim, zoom, ox, oy) {
		const a = app();
		const ctx = canvas.getContext('2d');
		ctx.fillStyle = '#0b0e14';
		ctx.fillRect(0, 0, canvas.width, canvas.height);
		ctx.strokeStyle = 'rgba(255,255,255,.08)';
		ctx.beginPath();
		ctx.moveTo(ox - 6, oy);
		ctx.lineTo(ox + 6, oy);
		ctx.moveTo(ox, oy - 6);
		ctx.lineTo(ox, oy + 6);
		ctx.stroke();
		ctx.imageSmoothingEnabled = false;
		const glows = [];
		for (const q of sim.particles) {
			const p = q.p;
			if (!p) {
				continue;
			}
			const x = ox + q.x * zoom;
			const y = oy + q.y * zoom;
			if (p.file) {
				const index = p.frames > 1 && q.life ? Math.min(p.frames - 1, Math.floor((q.age / q.life) * p.frames)) : 0;
				const frame = a.sprite(p.file, p.frames, index);
				if (frame) {
					const off = p.offset || { x: -frame.width / 2, y: -frame.height / 2 };
					ctx.drawImage(frame.canvas, x + off.x * zoom, y + off.y * zoom, frame.width * zoom, frame.height * zoom);
				}
			} else {
				ctx.fillStyle = 'rgb(' + p.rgb.map(Math.round).join(',') + ')';
				ctx.fillRect(Math.round(x), Math.round(y), Math.max(1, zoom), Math.max(1, zoom));
			}
			if (p.glow && (p.glowAlways || true)) {
				glows.push([p, x, y]);
			}
		}
		// glow pictures are added on top, as the game's lighting would
		ctx.globalCompositeOperation = 'lighter';
		for (const [p, x, y] of glows.slice(0, 200)) {
			const frame = a.sprite(p.glow, 1, 0);
			if (frame) {
				ctx.globalAlpha = Math.min(1, p.glowStrength);
				ctx.drawImage(frame.canvas, x - (frame.width * zoom) / 2, y - (frame.height * zoom) / 2, frame.width * zoom, frame.height * zoom);
			}
		}
		ctx.globalCompositeOperation = 'source-over';
		ctx.globalAlpha = 1;
	}

	// ---- the editor ----

	function render(module) {
		return (host, tab) => {
			const a = app();
			if (!a.lib || !a.lib.presets.length) {
				host.replaceChildren(el('p', { className: 'ed ed-empty', textContent: 'Studio is still reading the game…' }));
				return;
			}
			const s = tab.state || (tab.state = { filter: '', selected: null, kind: 'emitters', angle: -90, gravity: true, zoom: 3 });
			const wanted = s.kind === 'emitters' ? EMITTERS : PARTICLES;
			const all = a.lib.presets.filter((p) => p.topLevel && p.module === module && wanted.has(p.cls) && p.node.key === 'AddEffect').sort((x, y) => x.name.localeCompare(y.name));
			const q = s.filter.trim().toLowerCase();
			const list = all.filter((p) => !q || p.name.toLowerCase().includes(q));
			const keyOf = (p) => p.doc.path + ':' + p.name;
			const selected = all.find((p) => keyOf(p) === s.selected) || list[0] || null;
			const filter = el('input', { type: 'search', placeholder: 'Filter ' + all.length + ' effects…', value: s.filter, className: 'ed-input' });
			filter.dataset.fk = 'filter';
			filter.oninput = () => {
				s.filter = filter.value;
				clearTimeout(s.timer);
				s.timer = setTimeout(() => MS.ide.refreshForm(), 200);
			};
			const chip = (id, text) => el('button', { className: s.kind === id ? 'sel' : '', textContent: text, onclick: () => { s.kind = id; s.selected = null; MS.ide.refreshForm(); } });
			const items = list.slice(0, 500).map((p) => el('button', { className: 'ed-item' + (p === selected ? ' sel' : ''), textContent: p.name, onclick: () => { s.selected = keyOf(p); MS.ide.refreshForm(); } }));
			host.replaceChildren(el('div', { className: 'ed ed-split' },
				el('div', { className: 'ed-head' }, el('h2', { textContent: 'Effects · ' + module }), el('span', { className: 'chips' }, chip('emitters', 'Emitters'), chip('particles', 'Particles'))),
				all[0] && locked(all[0].doc) ? el('div', { className: 'ed-banner', textContent: module + ' is read only: its effects can be watched. Copy one into your mod to change it.' }) : null,
				el('div', { className: 'ed-panes' },
					el('div', { className: 'ed-list' }, filter, ...(items.length ? items : [el('p', { className: 'ed-empty', textContent: 'No ' + s.kind + (q ? ' match.' : ' in this mod.') })])),
					el('div', { className: 'ed-detail' }, selected ? detail(selected, s) : null))));
		};
	}

	function detail(p, s) {
		const a = app();
		const ro = locked(p.doc);
		const isEmitter = EMITTERS.has(p.cls);
		let setup;
		let emissions = null;
		if (isEmitter) {
			emissions = emissionsOf(p);
			setup = { emissions: emissions.list };
		} else {
			// A single particle: shown as a spray of itself
			const particle = particleOf(a.lib.resolve(p.node));
			setup = { emissions: [{ ppm: 0, burst: 14, spread: 0.5, min: 3, max: 9, lifeVar: 0.2, offset: { x: 0, y: 0 }, particle }] };
		}
		const sim = new Sim(setup);
		const canvas = el('canvas', { className: 'fx-canvas', width: 640, height: 340, title: 'The effect running. Burst fires one burst; Emit keeps it going.' });
		const angle = () => (s.angle * Math.PI) / 180;
		const ox = canvas.width / 2;
		const oy = canvas.height - 50;
		const emit = el('label', { className: 'ed-check' }, el('input', { type: 'checkbox', checked: isEmitter && setup.emissions.some((e) => e.ppm > 0), onchange: (e) => { sim.emitting = e.target.checked; } }), 'Keep emitting');
		sim.emitting = emit.firstChild.checked;
		const burst = el('button', { className: 'tb', textContent: 'Burst', onclick: () => sim.burst(angle()) });
		const dir = el('input', { type: 'range', min: -180, max: 180, value: s.angle, title: 'Which way it points' });
		dir.oninput = () => { s.angle = Number(dir.value); };
		const grav = el('label', { className: 'ed-check' }, el('input', { type: 'checkbox', checked: s.gravity, onchange: (e) => { s.gravity = e.target.checked; } }), 'Gravity');
		const zoom = el('input', { type: 'range', min: 1, max: 6, value: s.zoom, title: 'Zoom' });
		zoom.oninput = () => { s.zoom = Number(zoom.value); };
		sim.burst(angle());
		let last = performance.now();
		const tick = (now) => {
			if (!canvas.isConnected) {
				return;
			}
			const dt = Math.min(0.05, (now - last) / 1000);
			last = now;
			sim.step(dt, angle(), s.gravity);
			drawSim(canvas, sim, s.zoom, ox, oy);
			requestAnimationFrame(tick);
		};
		requestAnimationFrame(tick);

		const rows = emissions ? emissions.list.map((e, i) => emissionRows(p, e, i, ro)) : [];
		return el('div', {},
			el('div', { className: 'ed-card-head' }, el('h3', { textContent: p.name }), el('span', { className: 'spacer' }),
				el('button', { className: 'ico-btn', textContent: 'Its lines', onclick: () => MS.ide.openFile(p.doc.path, p.node.line) })),
			canvas,
			el('div', { className: 'fx-tools' }, burst, emit, grav, el('label', { className: 'ed-check' }, 'Direction', dir), el('label', { className: 'ed-check' }, 'Zoom', zoom)),
			isEmitter ? el('div', {}, el('div', { className: 'ed-label', textContent: 'Emissions (' + rows.length + ')' }), ...(rows.length ? rows : [el('p', { className: 'ed-empty', textContent: 'This emitter has no emissions of its own.' })])) : particleInfo(setup.emissions[0].particle));
	}

	function particleInfo(p) {
		if (!p) {
			return el('p', { className: 'ed-empty', textContent: 'Nothing to show.' });
		}
		return el('div', { className: 'ed-settings' },
			el('div', { className: 'ed-setting' }, el('span', { textContent: 'Kind' }), el('b', { textContent: p.cls })),
			p.cls === 'MOPixel' ? el('div', { className: 'ed-setting' }, el('span', { textContent: 'Colour' }), el('span', { className: 'mat-chip', style: 'background:rgb(' + p.rgb.join(',') + ')' })) : el('div', { className: 'ed-setting' }, el('span', { textContent: 'Frames' }), el('b', { textContent: String(p.frames) })),
			el('div', { className: 'ed-setting' }, el('span', { textContent: 'Lasts (ms)' }), el('b', { textContent: p.life ? String(p.life) : 'until it stops' })),
			el('div', { className: 'ed-setting' }, el('span', { textContent: 'Glow' }), el('b', { textContent: p.glow ? p.glow.split('/').pop() : 'none' })));
	}

	// The numbers of one emission. Written on the emission's own block; an inherited emission is read only.
	function emissionRows(p, e, i, ro) {
		const editable = e.own && !ro;
		const set = (label, key, value) => change('Change ' + p.name + '\'s ' + label.toLowerCase(), () => e.node.set(key, String(value)));
		const num = (label, key, value, help, step) => {
			const input = el('input', { type: 'number', step: step || 'any', className: 'ed-input num', value, disabled: !editable, title: help });
			input.onchange = () => set(label, key, Number(input.value) || 0);
			return el('label', { className: 'ed-setting', title: help }, el('span', { textContent: label }), input);
		};
		const kind = e.particle ? e.particle.name || e.particle.cls : 'nothing';
		return el('div', { className: 'chain-link' },
			el('div', { className: 'chain-head' }, el('b', { textContent: 'Emission ' + (i + 1) }), el('span', { className: 'chain-name', textContent: kind + (e.own ? '' : ' (from what it copies)') })),
			el('div', { className: 'ed-settings' },
				num('Per minute', 'ParticlesPerMinute', e.ppm, 'Particles a minute while emitting (0: only in bursts)'),
				num('Burst', 'BurstSize', e.burst, 'Particles in one burst', 1),
				num('Spread', 'Spread', e.spread, 'Half-width of the cone, in radians'),
				num('Min speed', 'MinVelocity', e.min, 'Metres a second'),
				num('Max speed', 'MaxVelocity', e.max, 'Metres a second'),
				num('Life variation', 'LifeVariation', e.lifeVar, 'How much each particle\'s life differs (0 to 1)')));
	}

	if (MS.editors && MS.editors.register) {
		MS.editors.register(['effects', 'Effects', 'Emitters and particles, running: sparks, smoke, flames, blasts', '✺', render, (module) => app().lib.presets.filter((p) => p.topLevel && p.module === module && (EMITTERS.has(p.cls) || PARTICLES.has(p.cls)) && p.node.key === 'AddEffect').length]);
	}
	MS.effects = { render };
})();
