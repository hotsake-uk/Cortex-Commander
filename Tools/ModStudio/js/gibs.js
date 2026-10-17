// Cortex Studio: Make gibs from a part's sprite. A part that is blown apart throws gibs; a custom picture needs gibs that look like it. This cuts the
// part's current picture (after any resize or edit) into a few irregular shards, saves each as its own small picture and gib preset in your project,
// and lists them in the part's gibs, placed where each shard sat. Run it again after changing the picture and the shards are made afresh.
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

	// A small repeatable random number generator, so the same seed gives the same shards.
	function random(seed) {
		let a = seed >>> 0;
		return () => {
			a = (a + 0x6d2b79f5) >>> 0;
			let t = a;
			t = Math.imul(t ^ (t >>> 15), t | 1);
			t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
			return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
		};
	}

	// Cuts a picture (palette frame {w, h, indices}) into `count` shards: seeds spread over the solid pixels, each solid pixel going to the nearest seed.
	// Returns [{ x, y, w, h, indices, area }]: each shard cropped to its own box, see-through everywhere it is not.
	function cut(frame, count, seed) {
		const { w, h, indices } = frame;
		const solid = [];
		for (let i = 0; i < indices.length; i++) {
			if (indices[i] !== 0) {
				solid.push(i);
			}
		}
		if (solid.length < 2) {
			return [];
		}
		const rand = random(seed);
		count = Math.max(1, Math.min(count, Math.floor(solid.length / 3)));
		// Seeds: the first anywhere, each next the solid pixel furthest from all so far, among a few random tries (so the shards differ each time).
		const seeds = [solid[Math.floor(rand() * solid.length)]];
		while (seeds.length < count) {
			let best = -1;
			let bestDist = -1;
			for (let t = 0; t < 14; t++) {
				const candidate = solid[Math.floor(rand() * solid.length)];
				const cx = candidate % w;
				const cy = Math.floor(candidate / w);
				let nearest = Infinity;
				for (const s of seeds) {
					nearest = Math.min(nearest, (cx - (s % w)) ** 2 + (cy - Math.floor(s / w)) ** 2);
				}
				if (nearest > bestDist) {
					bestDist = nearest;
					best = candidate;
				}
			}
			seeds.push(best);
		}
		const owner = new Int16Array(indices.length).fill(-1);
		for (const i of solid) {
			const x = i % w;
			const y = Math.floor(i / w);
			let which = 0;
			let nearest = Infinity;
			seeds.forEach((s, k) => {
				const d = (x - (s % w)) ** 2 + (y - Math.floor(s / w)) ** 2;
				if (d < nearest) {
					nearest = d;
					which = k;
				}
			});
			owner[i] = which;
		}
		const shards = [];
		for (let k = 0; k < seeds.length; k++) {
			let x0 = w;
			let y0 = h;
			let x1 = -1;
			let y1 = -1;
			let area = 0;
			for (const i of solid) {
				if (owner[i] === k) {
					const x = i % w;
					const y = Math.floor(i / w);
					x0 = Math.min(x0, x);
					y0 = Math.min(y0, y);
					x1 = Math.max(x1, x);
					y1 = Math.max(y1, y);
					area++;
				}
			}
			if (area < 2) {
				continue;
			}
			const sw = x1 - x0 + 1;
			const sh = y1 - y0 + 1;
			const out = new Uint8Array(sw * sh);
			for (let y = y0; y <= y1; y++) {
				for (let x = x0; x <= x1; x++) {
					if (owner[y * w + x] === k) {
						out[(y - y0) * sw + (x - x0)] = indices[y * w + x];
					}
				}
			}
			shards.push({ x: x0, y: y0, w: sw, h: sh, indices: out, area, color: k });
		}
		return shards;
	}

	let S = null;

	async function open(part) {
		const a = app();
		if (a.needsCopy()) {
			a.copyThen(part, (copied) => open(copied));
			return;
		}
		const reason = a.lockReason(part);
		if (reason) {
			a.toast(reason, 'bad');
			return;
		}
		if (!part.sprite.file) {
			a.toast(part.name + ' has no picture to make gibs from.', 'bad');
			return;
		}
		const frames = await MS.pixel.loadFrames(part.sprite.file, part.sprite.frameCount);
		if (!frames || !frames.length) {
			a.toast('Studio can\'t read ' + part.sprite.file + '.', 'bad');
			return;
		}
		const frame = frames[0];
		S = { part, frame, count: 4, seed: 1, shards: [] };
		const canvas = el('canvas', { className: 'gib-preview' });
		const count = el('input', { type: 'number', min: 2, max: 10, value: S.count, className: 'ed-input num' });
		const info = el('div', { className: 'small' });
		const palette = (k) => 'hsl(' + ((k * 137.5 + 20) % 360) + ', 80%, 60%)';
		const draw = () => {
			S.shards = cut(frame, S.count, S.seed);
			const z = Math.max(2, Math.min(12, Math.floor(260 / Math.max(frame.w, frame.h))));
			canvas.width = frame.w * z;
			canvas.height = frame.h * z;
			const ctx = canvas.getContext('2d');
			ctx.imageSmoothingEnabled = false;
			ctx.fillStyle = '#15171c';
			ctx.fillRect(0, 0, canvas.width, canvas.height);
			ctx.drawImage(a.images.fromIndices(frame.w, frame.h, frame.indices).canvas, 0, 0, canvas.width, canvas.height);
			// Each shard tinted in its own colour over the picture, so the cuts show.
			for (const shard of S.shards) {
				ctx.fillStyle = palette(shard.color);
				ctx.globalAlpha = 0.45;
				for (let y = 0; y < shard.h; y++) {
					for (let x = 0; x < shard.w; x++) {
						if (shard.indices[y * shard.w + x] !== 0) {
							ctx.fillRect((shard.x + x) * z, (shard.y + y) * z, z, z);
						}
					}
				}
			}
			ctx.globalAlpha = 1;
			info.textContent = S.shards.length + ' gibs from the ' + frame.w + '×' + frame.h + ' picture' + (part.sprite.frameCount > 1 ? ' (its first frame)' : '') + '.';
		};
		count.oninput = () => {
			S.count = Math.max(2, Math.min(10, Number(count.value) || 4));
			draw();
		};
		const again = el('button', { className: 'tb', textContent: 'Different cuts', onclick: () => { S.seed++; draw(); } });
		const body = el('div', {},
			el('p', { textContent: 'Cuts ' + part.name + '\'s picture, as it is now, into gibs that fly out when it is blown apart.' }),
			el('div', { className: 'gib-row' }, canvas),
			el('div', { className: 'gib-row' }, 'Pieces', count, again),
			info,
			el('p', { className: 'small', textContent: 'They are added to the gibs it already has.' }));
		MS.dialogs.open('Make gibs from ' + part.name, body, [
			{ text: 'Cancel' },
			{ text: 'Make the gibs', primary: true, action: () => apply() },
		], true);
		draw();
	}

	// Makes sure the mod's Index.ini lists the gib file before the unit's file (an earlier run may have put it after).
	function listBefore(mod, file, unitFile) {
		const a = app();
		const index = a.lib.docFor(mod + '/Index.ini');
		if (!index) {
			return;
		}
		const dm = index.root.children.find((n) => n.key === 'DataModule') || index.root;
		const same = (n, path) => n.key === 'IncludeFile' && n.value.split(String.fromCharCode(92)).join('/').toLowerCase() === path.toLowerCase();
		const gib = dm.children.find((n) => same(n, file));
		const unit = dm.children.find((n) => same(n, unitFile));
		if (gib && unit && dm.children.indexOf(gib) > dm.children.indexOf(unit)) {
			a.edit('list the gibs before the unit', () => {
				gib.remove();
				dm.add('IncludeFile', file, dm.children.indexOf(unit));
			});
		}
	}

	// The gib pictures and presets in the project, and the part's gib lines.
	async function apply() {
		const a = app();
		const { part, shards, frame } = S;
		if (!shards.length) {
			a.toast('Nothing to cut.', 'bad');
			return;
		}
		const mod = a.project || part.anchor.doc.module;
		const unitName = a.actor ? a.actor.name : 'Unit';
		const safe = MS.dialogs.parts.safeName;
		const partName = safe(MS.Rig.ROLE_NAMES[part.role] || part.role);
		const folder = mod + '/Sprites/' + safe(unitName) + '/Gibs';
		const docPath = mod + '/Effects/' + safe(unitName) + 'Gibs.ini';
		// The picture's origin: where its top-left sits relative to the part (an unset offset is centred).
		const off = part.sprite.offset && (part.sprite.offset.x || part.sprite.offset.y) ? part.sprite.offset : { x: -frame.w / 2, y: -frame.h / 2 };
		const material = (() => {
			const atoms = part.ent.sub('AtomGroup');
			const m = atoms && atoms.sub('Material');
			const c = m && m.node.child('CopyOf');
			return c ? c.value : 'Bone';
		})();
		const prefix = unitName + ' ' + (MS.Rig.ROLE_NAMES[part.role] || part.role) + ' Gib ';
		// Pictures first.
		try {
			for (let i = 0; i < shards.length; i++) {
				const s = shards[i];
				await a.fs.writeFile(folder + '/' + partName + 'Gib' + (i + 1) + '.png', await a.images.encodePng(s.w, s.h, s.indices));
			}
		} catch (e) {
			a.toast('Couldn\'t save the gib pictures: ' + e.message, 'bad');
			return;
		}
		a.forgetSprites(shards.map((s, i) => folder + '/' + partName + 'Gib' + (i + 1) + '.png'));
		let doc = a.lib.docFor(docPath);
		if (!doc) {
			// Read before the unit that carries the gibs: a CopyOf needs its preset to exist already, or the game refuses the whole mod.
			doc = await a.lib.newDoc(docPath, part.anchor.doc.module.toLowerCase() === mod.toLowerCase() ? part.anchor.doc.path : null);
			a.hookDoc(doc);
			a.hookDoc(a.lib.docFor(mod + '/Index.ini'));
		}
		listBefore(mod, docPath, part.anchor.doc.path);
		const names = shards.map((s, i) => prefix + (i + 1));
		a.edit('make gibs for ' + part.name, () => {
			// The shards of an earlier run come out first, so running it again makes them afresh.
			for (const node of [...doc.root.children]) {
				if (node.key === 'AddEffect') {
					const n = node.child('PresetName');
					if (n && n.value.startsWith(prefix)) {
						node.remove();
					}
				}
			}
			shards.forEach((s, i) => {
				const block = doc.addTopLevel('AddEffect', 'MOSRotating');
				block.add('PresetName', names[i]);
				block.add('Mass', String(Math.max(1, Math.round(s.area * 0.2))));
				block.add('HitsMOs', '0');
				block.add('GetsHitByMOs', '1');
				const sf = block.add('SpriteFile', 'ContentFile');
				sf.add('FilePath', folder + '/' + partName + 'Gib' + (i + 1) + '.png');
				block.add('FrameCount', '1');
				block.add('AngularVel', '6');
				const atoms = block.add('AtomGroup', 'AtomGroup');
				atoms.add('AutoGenerate', '1');
				const mat = atoms.add('Material', 'Material');
				mat.add('CopyOf', material);
				atoms.add('Resolution', '4');
				atoms.add('Depth', '0');
				block.add('DeepCheck', '0');
			});
			const own = a.ownBlock(part);
			if (!own) {
				return;
			}
			const refs = names.map((n) => a.lib.reference(n, mod, own.doc.module));
			// An earlier run's lines are replaced; every other gib the part has stays as it is.
			for (const node of [...own.children]) {
				if (node.key === 'AddGib') {
					const p = node.child('GibParticle');
					const c = p && p.child('CopyOf');
					if (c && c.value.split('/').pop().startsWith(prefix)) {
						node.remove();
					}
				}
			}
			shards.forEach((s, i) => {
				const gib = own.add('AddGib', 'Gib');
				const particle = gib.add('GibParticle', 'MOSRotating');
				particle.add('CopyOf', refs[i]);
				// Where the shard sat on the part, from the part's origin.
				gib.setVector('Offset', Math.round((off.x + s.x + s.w / 2) * 10) / 10, Math.round((off.y + s.y + s.h / 2) * 10) / 10);
			});
		});
		try {
			await a.save(true, [doc, a.lib.docFor(mod + '/Index.ini'), a.lib.docFor(part.anchor.doc.path)].filter(Boolean));
		} catch (e) {
			// autosave will try again
		}
		a.toast('Made ' + shards.length + ' gibs for ' + part.name + ' in ' + mod + '.', 'good');
		S = null;
	}

	MS.gibs = { open, cut };
})();
