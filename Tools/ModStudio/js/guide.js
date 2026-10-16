// Cortex Mod Studio: the guide. After New… makes a unit (or from the Guide button), a card over the unit view walks through its parts one
// by one, on the skeleton: what the part is, what its picture needs, and a place to drop your own picture (a PNG or a sprite sheet).
// Each picture goes through the same Replace-the-sprite code as the parts list (dialogs.js), is saved straight away, and the guide moves on.
// Then it plays the unit walking so you can check the pieces line up, and offers to try it in the game.
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

	// What each kind of part is, in plain words, and what its picture needs.
	const ROLE_HELP = {
		body: ['The body', 'Everything else is fixed to this. Draw it facing right; the game turns it to face left.'],
		head: ['The head', 'It sits on the body at the neck. Draw it facing right.'],
		jetpack: ['The jetpack', 'The pack on the unit\'s back, drawn behind the body.'],
		fgarm: ['The front arm', 'The arm nearest the camera, which holds the gun. It stretches to reach, so it needs several frames: the first is the most bent, the last the straightest.'],
		bgarm: ['The back arm', 'The far arm, drawn behind the body. Frames work like the front arm: first most bent, last straightest.'],
		fgleg: ['The front leg', 'The leg nearest the camera. Several frames: the first is the most bent, the last the straightest.'],
		bgleg: ['The back leg', 'The far leg, drawn behind. Several frames: the first is the most bent, the last the straightest.'],
		foot: ['A foot', 'Feet use up to four frames: flat on the ground, toes forward, heel back and far back.'],
		attachable: ['An attachment', 'A piece fixed to the unit, like a helmet or a pack.'],
		leg: ['A leg', 'A leg of the walker. Several frames, from most bent to straightest.'],
		turret: ['The turret', 'The part that aims. Draw it facing right.'],
	};
	const SKIP_ROLES = new Set(['device', 'magazine', 'flash']);
	const POSES = [['stand', 'Stand'], ['walk', 'Walk'], ['run', 'Run'], ['crouch', 'Crouch'], ['jump', 'Jump']];

	let g = null; // the running guide: {ids, i, view, done, skipped, saved, card, modName, unitName}

	function modOf() {
		return app().actor ? app().actor.path.split('/')[0] : '';
	}

	function stepParts() {
		const a = app();
		return a.rig ? a.rig.parts.filter((p) => p.sprite.file && !SKIP_ROLES.has(p.role)) : [];
	}

	function partFor(id) {
		return app().rig ? app().rig.parts.find((p) => p.id === id) : null;
	}

	function isMine(part) {
		const file = part.sprite.file;
		return !!file && file.toLowerCase().startsWith(g.modName.toLowerCase() + '/');
	}

	// ---- the view while guiding: bones on, pictures kept, pose standing ----

	function viewOn() {
		const a = app();
		const v = a.view;
		g.saved = { skeleton: v.options.skeleton, pictures: v.skeletonPictures };
		if (!v.options.skeleton) {
			const b = document.querySelector('#viewOptions [data-opt="skeleton"]');
			b ? b.click() : (v.options.skeleton = true);
		}
		v.skeletonPictures = 'shown';
		pose('stand');
		a.requestDraw();
	}

	function viewOff() {
		const a = app();
		const v = a.view;
		if (g.saved) {
			if (!g.saved.skeleton && v.options.skeleton) {
				const b = document.querySelector('#viewOptions [data-opt="skeleton"]');
				b ? b.click() : (v.options.skeleton = false);
			}
			v.skeletonPictures = g.saved.pictures;
		}
		a.requestDraw();
	}

	function pose(name) {
		const b = document.querySelector('#poseButtons [data-pose="' + name + '"]');
		if (b) {
			b.click();
		}
	}

	// ---- pictures in ----

	function pictureStrip(part) {
		const s = part.sprite;
		const strip = el('div', { className: 'guide-frames' });
		const count = Math.min(s.frameCount, 8);
		for (let i = 0; i < count; i++) {
			const c = el('canvas', { width: 44, height: 44 });
			MS.panels.queueThumb(c, () => {
				const frame = app().sprite(s.file, s.frameCount, i);
				if (!frame) {
					return false;
				}
				const ctx = c.getContext('2d');
				ctx.imageSmoothingEnabled = false;
				ctx.clearRect(0, 0, 44, 44);
				const z = Math.max(1, Math.min(5, Math.floor(42 / Math.max(frame.width, frame.height))));
				ctx.drawImage(frame.canvas, Math.floor((44 - frame.width * z) / 2), Math.floor((44 - frame.height * z) / 2), frame.width * z, frame.height * z);
				return true;
			});
			strip.append(c);
		}
		if (s.frameCount > count) {
			strip.append(el('span', { className: 'guide-more', textContent: '+' + (s.frameCount - count) }));
		}
		return strip;
	}

	function loadInto(part, files) {
		const images = [...files].filter((f) => /^image\//.test(f.type) || /\.(png|bmp|gif|webp|jpe?g)$/i.test(f.name));
		if (!images.length) {
			app().toast('That isn\'t a picture. Use a PNG (or BMP, GIF, JPG).', 'bad');
			return;
		}
		MS.dialogs.importSprite(part, {
			files: images,
			onDone: async () => {
				const a = app();
				// Straight to disk, so closing Studio half way loses nothing.
				const doc = a.lib.docFor(a.actor.path);
				await a.save(true, doc ? [doc] : undefined);
				g.skipped.delete(part.id);
				if (g && g.view === 'parts') {
					next();
				}
			},
		});
	}

	function choosePicture(part) {
		const input = el('input', { type: 'file', accept: 'image/png,image/bmp,image/gif,image/webp,image/jpeg', multiple: true });
		input.onchange = () => input.files.length && loadInto(part, input.files);
		input.click();
	}

	// ---- steps ----

	function go(i) {
		g.i = Math.max(0, Math.min(g.ids.length - 1, i));
		g.view = 'parts';
		const part = partFor(g.ids[g.i]);
		if (part) {
			app().select(part);
		}
		render();
	}

	function next() {
		if (g.i < g.ids.length - 1) {
			go(g.i + 1);
		} else {
			startWalk();
		}
	}

	function startWalk() {
		g.view = 'walk';
		pose('walk');
		render();
	}

	function finish() {
		g.view = 'done';
		pose('stand');
		render();
	}

	function close() {
		if (!g) {
			return;
		}
		const wrap = $('viewWrap');
		wrap.removeEventListener('dragover', g.onDragOver);
		wrap.removeEventListener('drop', g.onDrop);
		wrap.classList.remove('guide-drop');
		viewOff();
		$('app').classList.remove('guiding');
		g.card.remove();
		g = null;
		requestAnimationFrame(() => app().view.fit());
	}

	// ---- the card ----

	function chips() {
		return el('div', { className: 'guide-chips' }, ...g.ids.map((id, i) => {
			const p = partFor(id);
			const mine = p && isMine(p);
			const state = mine ? 'mine' : g.skipped.has(id) ? 'kept' : '';
			const chip = el('button', { className: 'guide-chip ' + state + (g.view === 'parts' && i === g.i ? ' cur' : ''), title: (p ? p.name : '') + (mine ? ': your picture' : g.skipped.has(id) ? ': keeping the original' : ''), textContent: mine ? '✓' : String(i + 1) });
			chip.onclick = () => go(i);
			return chip;
		}));
	}

	function partPanel(part) {
		let [title, help] = ROLE_HELP[part.role] || [part.name, 'A part of the unit.'];
		if (part.role === 'body' && MS.Rig.DEVICE_CLASSES.has(app().actor.cls)) {
			// A gun or a grenade has one main picture.
			const thrown = /Explosive|Thrown/.test(app().actor.cls);
			title = thrown ? 'The grenade' : 'The gun';
			help = thrown ? 'Draw it facing right, as it looks in the hand. Several frames can show it spinning or a fuse burning down.' : 'Draw it pointing right, with the muzzle at the right-hand end. Extra frames can show the action working. You can move the grip and muzzle points afterwards.';
		}
		const mine = isMine(part);
		const s = part.sprite;
		const first = s.file ? app().sprite(s.file, s.frameCount, 0) : null;
		const drop = el('div', { className: 'guide-drop-zone' },
			el('b', { textContent: mine ? 'Drop a different picture to replace it' : 'Drop your picture here' }),
			el('span', { textContent: 'a PNG, or a sprite sheet with the frames side by side' }),
			el('button', { className: 'tb primary', textContent: 'Choose a picture…', onclick: () => choosePicture(part) }));
		drop.ondragover = (e) => {
			e.preventDefault();
			drop.classList.add('over');
		};
		drop.ondragleave = () => drop.classList.remove('over');
		drop.ondrop = (e) => {
			e.preventDefault();
			e.stopPropagation();
			drop.classList.remove('over');
			loadInto(part, e.dataTransfer.files);
		};
		const lock = app().lockReason(part);
		return el('div', { className: 'guide-part guide-cols' },
			el('div', { className: 'guide-left' },
				el('h3', {}, title, el('small', { textContent: part.name.includes(' · ') ? ' ' + part.name.split(' · ').slice(1).join(' · ') : '' })),
				el('p', { className: 'guide-help', textContent: help }),
				el('div', { className: 'guide-now' },
					pictureStrip(part),
					el('div', { className: 'guide-caption', textContent: (mine ? 'Your picture' : 'The original, from ' + (s.file.split('/')[0] || 'the game')) + (first ? ' · ' + first.width + '×' + first.height + ' pixels · ' + s.frameCount + ' frame' + (s.frameCount > 1 ? 's' : '') : '') }))),
			el('div', { className: 'guide-right' },
				lock ? el('p', { className: 'guide-lock', textContent: lock }) : drop,
				el('div', { className: 'guide-nav' },
					el('button', { className: 'tb', textContent: '‹ Back', disabled: g.i === 0, onclick: () => go(g.i - 1) }),
					el('span', { className: 'spacer' }),
					el('button', { className: 'tb', textContent: mine ? 'Next ›' : 'Keep the original ›', onclick: () => { if (!mine) { g.skipped.add(part.id); } next(); } }))));
	}

	function walkPanel() {
		return el('div', { className: 'guide-part' },
			el('h3', { textContent: 'Watch it move' }),
			el('p', { className: 'guide-help', textContent: 'Check the arms, legs and feet line up while it moves. If a piece floats or sits wrong, go back to that part, or drag it in the view.' }),
			el('div', { className: 'guide-poses' }, ...POSES.map(([id, text]) => el('button', { className: 'tb', textContent: text, onclick: () => pose(id) }))),
			el('div', { className: 'guide-nav' },
				el('button', { className: 'tb', textContent: '‹ Back to the parts', onclick: () => go(g.ids.length - 1) }),
				el('span', { className: 'spacer' }),
				el('button', { className: 'tb primary', textContent: 'Looks good ›', onclick: finish })));
	}

	function donePanel() {
		const mine = g.ids.map(partFor).filter((p) => p && isMine(p)).length;
		return el('div', { className: 'guide-part' },
			el('h3', { textContent: g.unitName + ' is ready' }),
			el('p', { className: 'guide-help', textContent: (mine ? mine + ' of its ' + g.ids.length + ' parts use your pictures. ' : 'It still uses the original pictures. ') + 'It is saved in ' + g.modName + '. The game loads new mods when it starts, so restart it once before it shows up.' }),
			el('div', { className: 'guide-nav' },
				el('button', { className: 'tb', textContent: 'Keep editing', onclick: close }),
				el('span', { className: 'spacer' }),
				el('button', { className: 'tb primary', textContent: '▶ Try it in the game', onclick: () => { close(); const b = $('btnPreview'); if (b && !b.disabled) { b.click(); } } })));
	}

	function render() {
		if (!g) {
			return;
		}
		const part = g.view === 'parts' ? partFor(g.ids[g.i]) : null;
		const progress = g.view === 'parts' ? 'Part ' + (g.i + 1) + ' of ' + g.ids.length : g.view === 'walk' ? 'Check it' : 'Done';
		g.card.replaceChildren(
			el('div', { className: 'guide-head' },
				el('b', { textContent: 'Give ' + g.unitName + ' your pictures' }),
				el('span', { className: 'spacer' }),
				el('span', { className: 'guide-progress', textContent: progress }),
				el('button', { className: 'guide-x', title: 'Close the guide (you can start it again with Guide)', textContent: '×', onclick: close })),
			chips(),
			g.view === 'parts' && part ? partPanel(part) : g.view === 'walk' ? walkPanel() : donePanel());
	}

	// ---- start ----

	function start() {
		const a = app();
		if (!a.rig || !a.actor) {
			a.toast('Pick a unit first.', 'bad');
			return;
		}
		const lock = a.lockReason(a.rig.body);
		if (lock) {
			a.toast('Make your own copy first, then the guide can put your pictures in it.', 'bad');
			MS.dialogs.copyUnit();
			return;
		}
		if (g) {
			close();
		}
		const ids = stepParts().map((p) => p.id);
		if (!ids.length) {
			a.toast('This has no pictures to replace.', 'bad');
			return;
		}
		// Docked under the view's toolbar, so it never covers the unit; the view refits to what is left.
		const card = el('div', { className: 'guide-card' });
		$('viewTools').after(card);
		$('app').classList.add('guiding');
		requestAnimationFrame(() => a.view.fit());
		g = { ids, i: 0, view: 'parts', skipped: new Set(), saved: null, card, modName: modOf(), unitName: a.actor.name };
		// Drop a picture anywhere on the unit view: it goes to the part being guided.
		g.onDragOver = (e) => {
			if (g.view === 'parts') {
				e.preventDefault();
				$('viewWrap').classList.add('guide-drop');
			}
		};
		g.onDrop = (e) => {
			$('viewWrap').classList.remove('guide-drop');
			if (g.view !== 'parts' || !e.dataTransfer.files.length) {
				return;
			}
			e.preventDefault();
			const part = partFor(g.ids[g.i]);
			if (part) {
				loadInto(part, e.dataTransfer.files);
			}
		};
		$('viewWrap').addEventListener('dragover', g.onDragOver);
		$('viewWrap').addEventListener('drop', g.onDrop);
		$('viewWrap').addEventListener('dragleave', () => $('viewWrap').classList.remove('guide-drop'));
		viewOn();
		go(0);
	}

	MS.guide = { start, close, active: () => !!g };
	const button = $('btnGuide');
	if (button) {
		button.onclick = () => (g ? close() : start());
	}
})();
