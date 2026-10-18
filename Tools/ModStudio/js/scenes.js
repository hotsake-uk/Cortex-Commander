// Cortex Mod Studio: the Scenes data editor (Data view). A scene is a map the game plays on: its terrain, the backdrop layers behind it (sky,
// mountains, clouds, scrolling at their own speeds), and named areas (landing zones and the like). It shows the whole map with its areas, and a
// camera view that scrolls it the way the game does, so a backdrop's speed can be judged by moving the camera, not by guessing a number.
//   Info      description, where it sits on the planet, whether campaigns can use it, gravity
//   Backdrops the layers in front-to-back order: scroll speed, wrapping, order, adding from the game's backdrops
//   Areas     the named boxes (LZ Team 1, and so on): name, corner and size
// Changes are ordinary edits (app.edit), so Undo and Save work as everywhere else.
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
	const AREA_COLORS = ['#f2b63d', '#5bc0eb', '#9bc53d', '#e55934', '#c77dff', '#fa7921'];
	const asked = new Set(); // pictures the editor has already waited for, so one that can't be read doesn't loop
	const CAMERA = { w: 960, h: 540 }; // what the game shows at 1x, in world pixels

	// A copy of one block's lines under another node (to put a layer somewhere else in the list).
	function graft(dst, src) {
		for (const child of src.children) {
			graft(dst.add(child.key, child.value), child);
		}
	}

	// Moves one of a scene's layers a place earlier (-1) or later (+1) among the layers.
	function moveLayer(node, delta) {
		const parent = node.parent;
		const layers = parent.children.filter((c) => c.key === 'AddBackgroundLayer');
		const at = layers.indexOf(node);
		const other = layers[at + delta];
		if (!other) {
			return;
		}
		const copy = node.clone();
		const key = node.key;
		const value = node.value;
		node.remove();
		const index = parent.children.indexOf(other) + (delta > 0 ? 1 : 0);
		graft(parent.add(key, value, index), copy);
	}

	// The terrain's picture. Its preset is usually another block (a "Terrain" the scene copies), which may be declared as any kind of layer.
	function terrainPicture(p, ent) {
		const a = app();
		const entry = ent.last('Terrain');
		if (!entry) {
			return null;
		}
		const resolved = a.lib.resolve(entry.node);
		let file = resolved.file('BitmapFile') || resolved.file('FGColorFile');
		const copy = entry.node.child('CopyOf');
		if (!file && copy) {
			const parts = copy.value.trim().split('/');
			const target = a.lib.find(parts[parts.length - 1], null, parts.length > 1 ? parts[0] : p.module);
			if (target) {
				const r = a.lib.resolve(target.node);
				file = r.file('BitmapFile') || r.file('FGColorFile');
			}
		}
		return file;
	}

	// ---- the scene as the game builds it: terrain with the placed buildings and objects ----

	const pictures = new Map(); // scene key -> { sig, picture, pending }

	// What a placed object looks like: a terrain object's background and foreground pictures (stamped behind and onto the terrain), or a
	// sprite's first frame (actors, doors, other objects). Offsets are from its position.
	function placedLook(entry, found) {
		const a = app();
		const ref = entry.node.child('CopyOf') ? entry.node.child('CopyOf').value.trim() : '';
		const cacheKey = entry.node.value + '|' + ref;
		if (!found.has(cacheKey)) {
			const r = a.lib.resolve(entry.node);
			let look = null;
			if (entry.node.value === 'TerrainObject') {
				const fg = r.file('FGColorFile');
				const bg = r.file('BGColorFile');
				// with no BitmapOffset of its own, the game centres a picture over 24 pixels on its position (by the widest and tallest of its
				// colour, background and material pictures): offset null here, worked out once they are read
				const given = !!r.last('BitmapOffset');
				look = fg || bg ? { fg, bg, mat: r.file('MaterialFile'), offset: given ? r.vec('BitmapOffset', { x: 0, y: 0 }) : null } : null;
			} else {
				const sprite = r.file('SpriteFile');
				look = sprite ? { sprite, frames: Math.max(1, Math.round(r.num('FrameCount', 1))), offset: r.vec('SpriteOffset', { x: 0, y: 0 }) } : null;
			}
			found.set(cacheKey, look);
		}
		return found.get(cacheKey);
	}

	// Each material's look, by its index: its foreground and background textures (files) and its own colour, from the Material presets (the
	// first of an index wins, as the game's own load first). Read once per library.
	let materialMemo = null;
	function materialLooks() {
		const lib = app().lib;
		if (materialMemo && materialMemo.lib === lib && materialMemo.gen === lib.generation) {
			return materialMemo.looks;
		}
		const looks = [];
		for (const p of lib.presets) {
			if (p.cls !== 'Material') {
				continue;
			}
			const r = lib.resolve(p.node);
			const index = Math.round(r.num('Index', -1));
			if (index < 0 || index > 255 || looks[index]) {
				continue;
			}
			const color = r.last('Color');
			const c = color ? [Number(color.node.childValue('R', '0')) || 0, Number(color.node.childValue('G', '0')) || 0, Number(color.node.childValue('B', '0')) || 0] : [128, 128, 128];
			looks[index] = { fg: r.file('FGTextureFile'), bg: r.file('BGTextureFile'), color: c };
		}
		materialMemo = { lib, gen: lib.generation, looks };
		return looks;
	}

	// The ground's foreground and background, from its material map: each material pixel takes its material's texture there (tiled), or its
	// colour when it has none; behind the ground, the material's background texture or the terrain's own. Air is see-through in both.
	function texturize(terrain, materials, loaded, groundBack) {
		const w = terrain.width;
		const h = terrain.height;
		const pixels = new Map(); // file -> { data, w, h }
		const pixelsOf = (file) => {
			const img = file && loaded.get(file);
			if (!img) {
				return null;
			}
			if (!pixels.has(file)) {
				const c = img.canvas.getContext('2d');
				pixels.set(file, { data: c.getImageData(0, 0, img.width, img.height).data, w: img.width, h: img.height });
			}
			return pixels.get(file);
		};
		const back = groundBack ? pixelsOf([...loaded.entries()].find(([, v]) => v === groundBack)[0]) : null;
		const fgOf = [];
		const bgOf = [];
		for (let i = 0; i < 256; i++) {
			const m = materials[i];
			fgOf[i] = m ? pixelsOf(m.fg) : null;
			bgOf[i] = (m && pixelsOf(m.bg)) || back;
		}
		const fgCanvas = document.createElement('canvas');
		const bgCanvas = document.createElement('canvas');
		fgCanvas.width = bgCanvas.width = w;
		fgCanvas.height = bgCanvas.height = h;
		const fgCtx = fgCanvas.getContext('2d');
		const bgCtx = bgCanvas.getContext('2d');
		const fgOut = fgCtx.createImageData(w, h);
		const bgOut = bgCtx.createImageData(w, h);
		const f = fgOut.data;
		const b = bgOut.data;
		const idx = terrain.indices;
		for (let y = 0; y < h; y++) {
			for (let x = 0; x < w; x++) {
				const i = y * w + x;
				const m = idx[i];
				if (m === 0) {
					continue; // air: see-through
				}
				const o = i * 4;
				const t = fgOf[m];
				if (t) {
					const s = ((y % t.h) * t.w + (x % t.w)) * 4;
					f[o] = t.data[s];
					f[o + 1] = t.data[s + 1];
					f[o + 2] = t.data[s + 2];
					f[o + 3] = 255;
				} else {
					const c = materials[m] ? materials[m].color : [128, 128, 128];
					f[o] = c[0];
					f[o + 1] = c[1];
					f[o + 2] = c[2];
					f[o + 3] = 255;
				}
				const g = bgOf[m];
				if (g) {
					const s = ((y % g.h) * g.w + (x % g.w)) * 4;
					b[o] = g.data[s];
					b[o + 1] = g.data[s + 1];
					b[o + 2] = g.data[s + 2];
					b[o + 3] = 255;
				}
			}
		}
		fgCtx.putImageData(fgOut, 0, 0);
		bgCtx.putImageData(bgOut, 0, 0);
		return { fg: fgCanvas, bg: bgCanvas };
	}

	// A material setting's index (a Material, usually a CopyOf), or -1.
	function materialIndex(r, key) {
		const m = r.last(key);
		return m ? Math.round(app().lib.resolve(m.node).num('Index', -1)) : -1;
	}

	// The terrain's frostings and debris, with the game's defaults (TerrainFrosting, TerrainDebris).
	function groundExtras(terrainRes) {
		const lib = app().lib;
		const frost = terrainRes.all('AddTerrainFrosting').map((e) => {
			const r = lib.resolve(e.node);
			return { target: materialIndex(r, 'TargetMaterial'), frost: materialIndex(r, 'FrostingMaterial'), min: Math.round(r.num('MinThickness', 5)), max: Math.round(r.num('MaxThickness', 5)), airOnly: r.bool('InAirOnly', true) };
		});
		const debris = terrainRes.all('AddTerrainDebris').map((e) => {
			const r = lib.resolve(e.node);
			const file = r.file('DebrisFile');
			return {
				frames: file ? MS.Images.framePaths(file, Math.max(1, Math.round(r.num('DebrisPieceCount', 1)))) : [],
				mat: materialIndex(r, 'DebrisMaterial'),
				target: materialIndex(r, 'TargetMaterial'),
				mode: Math.round(r.num('DebrisPlacementMode', 0)),
				buried: r.bool('OnlyBuried', false),
				minDepth: Math.round(r.num('MinDepth', 0)),
				maxDepth: Math.round(r.num('MaxDepth', 10)),
				minRot: r.num('MinRotation', 0),
				maxRot: r.num('MaxRotation', 0),
				hFlip: r.bool('CanHFlip', false),
				vFlip: r.bool('CanVFlip', false),
				flipChance: r.num('FlipChance', 0.5),
				density: r.num('DensityPerMeter', 0.01),
			};
		}).filter((d) => d.frames.length && d.target >= 0);
		return { frost, debris };
	}

	// The same random numbers for the same scene each time it is drawn.
	function seeded(seed) {
		let t = seed >>> 0;
		return () => {
			t = (t + 0x6d2b79f5) >>> 0;
			let r = Math.imul(t ^ (t >>> 15), 1 | t);
			r = (r + Math.imul(r ^ (r >>> 7), 61 | r)) ^ r;
			return ((r ^ (r >>> 14)) >>> 0) / 4294967296;
		};
	}

	// Frosting and debris onto the ground's foreground and material map, as the game does once it has textured it
	// (TerrainFrosting::FrostTerrain, TerrainDebris::ScatterOnTerrain). Random as the game's are: where each plant or rock goes differs from
	// the game's, but not what kinds go where or how many.
	function dressGround(fgCanvas, mat, w, h, extras, materials, loaded, seed) {
		if (!extras.frost.length && !extras.debris.length) {
			return;
		}
		const rnd = seeded(seed);
		const between = (lo, hi) => (hi <= lo ? lo : lo + Math.floor(rnd() * (hi - lo + 1)));
		const ctx = fgCanvas.getContext('2d');
		const out = ctx.getImageData(0, 0, w, h);
		const f = out.data;
		const textureOf = (file) => {
			const img = file && loaded.get(file);
			return img ? { data: img.canvas.getContext('2d').getImageData(0, 0, img.width, img.height).data, w: img.width, h: img.height } : null;
		};
		// frosting: up each column, over the top of each run of its target material, a few pixels thick
		for (const fr of extras.frost) {
			if (fr.target < 0 || fr.frost < 0) {
				continue;
			}
			const look = materials[fr.frost];
			const tex = look ? textureOf(look.fg) : null;
			const color = look ? look.color : [90, 140, 60];
			let found = false;
			let applying = false;
			let applied = 0;
			for (let x = 0; x < w; x++) {
				const goal = between(fr.min, fr.max);
				for (let y = h - 1; y >= 0; y--) {
					const i = y * w + x;
					const m = mat[i];
					if (!found && m === fr.target) {
						found = true;
						applied = 0;
					} else if (found && m !== fr.target && applied <= goal) {
						found = false;
						applying = true;
					}
					if (applying && (m === 0 || !fr.airOnly) && applied <= goal) {
						const o = i * 4;
						if (tex) {
							const s = ((y % tex.h) * tex.w + (x % tex.w)) * 4;
							f[o] = tex.data[s];
							f[o + 1] = tex.data[s + 1];
							f[o + 2] = tex.data[s + 2];
						} else {
							[f[o], f[o + 1], f[o + 2]] = color;
						}
						f[o + 3] = 255;
						mat[i] = fr.frost;
						applied++;
					} else {
						applying = false;
					}
				}
			}
		}
		// debris: a piece at a random column, on (or under) the first pixel of its target material that its placement allows
		const AIR = 0;
		const CAVITY = 1;
		const at = (x, y) => (x >= 0 && x < w && y >= 0 && y < h ? mat[y * w + x] : -1);
		const piece = document.createElement('canvas');
		const pctx = piece.getContext('2d', { willReadFrequently: true });
		for (const d of extras.debris) {
			const imgs = d.frames.map((p) => loaded.get(p)).filter(Boolean);
			if (!imgs.length) {
				continue;
			}
			const overhang = d.mode >= 4;
			const valid = (m, prev) => {
				if (m !== d.target) {
					return false;
				}
				const kind = d.mode >= 4 ? d.mode - 3 : d.mode; // 1 open surface, 2 cave surface, 3 either
				return kind === 1 ? prev === AIR : kind === 2 ? prev === CAVITY : kind === 3 ? prev === AIR || prev === CAVITY : true;
			};
			const count = Math.floor(w / 20 * d.density);
			for (let n = 0; n < count; n++) {
				const img = imgs[between(0, imgs.length - 1)];
				const x = between(0, w);
				const depth = between(d.minDepth, d.maxDepth);
				const buriedBy = d.buried ? Math.floor(img.height * 0.6) : 0;
				let corner = null;
				let prev = -1;
				for (let s = 0, o = h - 1; s < h && o > 0; s++, o--) {
					const m = at(x, overhang ? o : s);
					if (m !== AIR && valid(m, prev)) {
						s += depth + buriedBy;
						o -= depth + buriedBy;
						const left = Math.floor(x - img.width / 2);
						const top = Math.floor((overhang ? o : s) - img.height / 2);
						const inGround = (px, py) => at(px, py) !== AIR;
						if (!d.buried || (inGround(left, top) && inGround(left + img.width, top) && inGround(left, top + img.height) && inGround(left + img.width, top + img.height))) {
							corner = { left, top };
							break;
						}
					}
					prev = m;
				}
				if (!corner) {
					continue;
				}
				const size = 10 + Math.max(img.width, img.height);
				piece.width = piece.height = size;
				pctx.imageSmoothingEnabled = false;
				pctx.translate(size / 2, size / 2);
				if (d.hFlip && d.flipChance >= rnd()) {
					pctx.scale(-1, 1);
				}
				if (d.vFlip && d.flipChance >= rnd()) {
					pctx.scale(1, -1);
				}
				if (d.minRot !== 0 || d.maxRot !== 0) {
					pctx.rotate((between(Math.round(d.minRot), Math.round(d.maxRot)) * Math.PI) / 180);
				}
				pctx.drawImage(img.canvas, -Math.floor(img.width / 2) - (img.width % 2 ? 0.5 : 0), -Math.floor(img.height / 2) - (img.height % 2 ? 0.5 : 0));
				pctx.setTransform(1, 0, 0, 1, 0, 0);
				const px = pctx.getImageData(0, 0, size, size).data;
				const ox = corner.left - Math.floor((size - img.width) / 2);
				const oy = corner.top - Math.floor((size - img.height) / 2);
				const markAs = d.mat > 0 ? d.mat : d.target;
				for (let y = 0; y < size; y++) {
					const ty = oy + y;
					if (ty < 0 || ty >= h) {
						continue;
					}
					for (let xx = 0; xx < size; xx++) {
						const tx = ox + xx;
						const s = (y * size + xx) * 4;
						if (tx < 0 || tx >= w || px[s + 3] < 128) {
							continue;
						}
						const i = ty * w + tx;
						const o = i * 4;
						f[o] = px[s];
						f[o + 1] = px[s + 1];
						f[o + 2] = px[s + 2];
						f[o + 3] = 255;
						mat[i] = markAs;
					}
				}
			}
		}
		ctx.putImageData(out, 0, 0);
	}

	// The scene's background layers (front first), with what places them (SLBackground).
	function backdrops(ent) {
		const lib = app().lib;
		return ent.all('AddBackgroundLayer').map((e) => {
			const r = lib.resolve(e.node);
			return {
				file: r.file('BitmapFile'),
				wrapX: r.bool('WrapX', true),
				wrapY: r.bool('WrapY', false),
				scroll: r.vec('ScrollRatio', { x: 1, y: 1 }),
				scale: r.vec('ScaleFactor', { x: 1, y: 1 }),
				ignoreAutoScale: r.bool('IgnoreAutoScaling', false),
				origin: r.vec('OriginPointOffset', { x: 0, y: 0 }),
			};
		}).filter((b) => b.file);
	}

	// The background layers behind everything, farthest first, where the game shows them to a full-HD screen looking at the ground (a far
	// layer follows the camera, so on the whole map it can only be where it is from one place): each scaled to the screen as the game's
	// default does, repeated across, and its top and bottom edges' colours carried on above and below it.
	const SCREEN_H = 1080;
	function drawBackdrops(ctx, backs, loaded, terrain) {
		const w = terrain.width;
		const h = terrain.height;
		// the ground's usual height: the middle of where each column's ground begins
		const tops = [];
		for (let x = 0; x < w; x += 8) {
			let y = 0;
			while (y < h && terrain.indices[y * w + x] === 0) {
				y++;
			}
			tops.push(y);
		}
		tops.sort((p, q) => p - q);
		const ground = tops.length ? tops[tops.length >> 1] : h / 2;
		const camTop = Math.max(0, Math.min(h - SCREEN_H, ground - SCREEN_H / 2));
		ctx.imageSmoothingEnabled = false;
		for (const b of [...backs].reverse()) {
			const img = loaded.get(b.file);
			if (!img) {
				continue;
			}
			const fit = Math.min(2, Math.max(1, Math.min(h, SCREEN_H) / img.height));
			const sx = b.ignoreAutoScale ? b.scale.x : fit;
			const sy = b.ignoreAutoScale ? b.scale.y : fit;
			const lw = Math.max(1, Math.round(img.width * sx));
			const lh = Math.max(1, Math.round(img.height * sy));
			// SceneLayer::InitScrollRatios, as it is
			const info = b.scroll.y * (b.ignoreAutoScale ? 1 : sy);
			let ratio;
			if (b.wrapY) {
				ratio = info;
			} else {
				ratio = info === -1 || info === 1 ? 1 : info === SCREEN_H ? img.height - SCREEN_H : img.height === SCREEN_H ? 1 / (info - SCREEN_H) : (img.height - SCREEN_H) / (info - SCREEN_H);
				ratio += 1;
			}
			const top = Math.floor(camTop * ratio) - b.origin.y;
			const left = -b.origin.x;
			const c = img.canvas.getContext('2d');
			const edge = (y) => {
				const p = c.getImageData(img.width >> 1, y, 1, 1).data;
				return p[3] > 128 ? 'rgb(' + p[0] + ',' + p[1] + ',' + p[2] + ')' : null;
			};
			if (!b.wrapY) {
				const up = edge(0);
				const down = edge(img.height - 1);
				if (up && top > 0) {
					ctx.fillStyle = up;
					ctx.fillRect(0, 0, w, top);
				}
				if (down && top + lh < h) {
					ctx.fillStyle = down;
					ctx.fillRect(0, top + lh, w, h - top - lh);
				}
			}
			const ys = b.wrapY ? [] : [top];
			if (b.wrapY) {
				for (let y = (((top % lh) + lh) % lh) - lh; y < h; y += lh) {
					ys.push(y);
				}
			}
			for (const y of ys) {
				if (b.wrapX) {
					for (let x = (((left % lw) + lw) % lw) - lw; x < w; x += lw) {
						ctx.drawImage(img.canvas, x, y, lw, lh);
					}
				} else {
					ctx.drawImage(img.canvas, left, y, lw, lh);
				}
			}
		}
	}

	// As the game does once a scene is built (SLTerrain::CleanAir): the material layer is the terrain's (its picture is the material map) with
	// each piece's material stamped over it in turn (colour 0 is see-through), cavity (1) becomes air (0), and wherever there is air the
	// foreground is wiped. That is how pieces carve rooms and shafts out of the ground. mats: [{ img (with indices), x, y }] in placing order.
	function cleanAir(ctx, terrain, base, mats) {
		if (!terrain.indices) {
			return; // a terrain given as a colour picture: no material map to go by
		}
		const w = terrain.width;
		const h = terrain.height;
		const mat = new Uint8Array(base || terrain.indices);
		let x0 = w;
		let y0 = h;
		let x1 = -1;
		let y1 = -1;
		for (const m of mats) {
			if (!m.img || !m.img.indices) {
				continue;
			}
			const ox = Math.floor(m.x);
			const oy = Math.floor(m.y);
			for (let y = 0; y < m.img.height; y++) {
				const ty = oy + y;
				if (ty < 0 || ty >= h) {
					continue;
				}
				for (let x = 0; x < m.img.width; x++) {
					const c = m.img.indices[y * m.img.width + x];
					const tx = ox + x;
					if (c !== 0 && tx >= 0 && tx < w) {
						mat[ty * w + tx] = c;
					}
				}
			}
			x0 = Math.max(0, Math.min(x0, ox));
			y0 = Math.max(0, Math.min(y0, oy));
			x1 = Math.min(w - 1, Math.max(x1, ox + m.img.width - 1));
			y1 = Math.min(h - 1, Math.max(y1, oy + m.img.height - 1));
		}
		// only where pieces went can the foreground differ from the terrain's own (which is see-through wherever it is air)
		if (x1 < x0 || y1 < y0) {
			return;
		}
		const bw = x1 - x0 + 1;
		const bh = y1 - y0 + 1;
		const data = ctx.getImageData(x0, y0, bw, bh);
		const px = data.data;
		for (let y = 0; y < bh; y++) {
			const row = (y0 + y) * w + x0;
			for (let x = 0; x < bw; x++) {
				if (mat[row + x] <= 1) {
					px[(y * bw + x) * 4 + 3] = 0;
				}
			}
		}
		ctx.putImageData(data, x0, y0);
	}

	// The scene's picture with what is placed in it, or null until its pictures are read (onReady is called then). Built once per scene and
	// built again when what is placed in it changes.
	// mode: 'textured' (as the game draws it: each material's texture, the ground's background behind) or 'materials' (the material map's
	// own colours)
	function picture(p, onReady, mode) {
		const a = app();
		const ent = a.lib.resolve(p.node);
		const terrainFile = terrainPicture(p, ent);
		if (!terrainFile) {
			return null;
		}
		const textured = mode !== 'materials';
		const materials = textured ? materialLooks() : [];
		const terrainEntry = ent.last('Terrain');
		const terrainRes = terrainEntry ? a.lib.resolve(terrainEntry.node) : null;
		const groundBack = textured && terrainRes ? terrainRes.file('BackgroundTexture') : null;
		// what the terrain itself adds, as the game builds it: grass and the like over its top (frosting), plants, rocks and such scattered
		// in it (debris), and the pieces it places (a cave), before the scene's own
		const extras = textured && terrainRes ? groundExtras(terrainRes) : { frost: [], debris: [] };
		const backs = textured ? backdrops(ent) : [];
		const groundPlaced = terrainRes ? terrainRes.all('PlaceTerrainObject') : [];
		const placed = ent.all('PlaceSceneObject');
		let hash = 0;
		for (const e of [...groundPlaced, ...placed]) {
			const pos = e.node.vector('Position') || { x: 0, y: 0 };
			for (const ch of e.node.childValue('CopyOf', '') + pos.x + ',' + pos.y + e.node.childValue('HFlipped', '')) {
				hash = (hash * 31 + ch.charCodeAt(0)) | 0;
			}
		}
		const sig = terrainFile + '|' + placed.length + '|' + groundPlaced.length + '|' + hash + '|' + (textured ? 't' : 'm');
		const key = p.doc.path + '::' + p.name + (textured ? '' : '::materials');
		const known = pictures.get(key);
		if (known && known.sig === sig) {
			if (!known.picture && onReady) {
				known.waiting.add(onReady);
			}
			return known.picture;
		}
		const job = { sig, picture: null, waiting: new Set(onReady ? [onReady] : []) };
		pictures.set(key, job);
		const found = new Map();
		const items = [];
		for (const entry of [...groundPlaced, ...placed]) {
			const look = placedLook(entry, found);
			if (look) {
				items.push({ look, pos: entry.node.vector('Position') || { x: 0, y: 0 }, flip: entry.node.childValue('HFlipped', '0').trim() === '1' });
			}
		}
		const files = new Set([terrainFile]);
		for (const m of materials) {
			[m && m.fg, m && m.bg].forEach((f) => f && files.add(f));
		}
		if (groundBack) {
			files.add(groundBack);
		}
		for (const { look } of items) {
			[look.fg, look.bg, look.mat, look.sprite && MS.Images.framePaths(look.sprite, look.frames)[0]].forEach((f) => f && files.add(f));
		}
		for (const d of extras.debris) {
			d.frames.forEach((f) => files.add(f));
		}
		for (const b of backs) {
			files.add(b.file);
		}
		const loaded = new Map();
		Promise.all([...files].map((f) => a.images.get(f).then((img) => loaded.set(f, img), () => loaded.set(f, null)))).then(() => {
			if (pictures.get(key) !== job) {
				return; // the scene changed while its pictures were read
			}
			const terrain = loaded.get(terrainFile);
			if (!terrain) {
				return;
			}
			const canvas = document.createElement('canvas');
			canvas.width = terrain.width;
			canvas.height = terrain.height;
			let ctx = canvas.getContext('2d');
			// drawn where it goes, and again across the edge for a scene that wraps
			const stamp = (img, x, y, flip) => {
				for (const dx of [0, x < 0 ? canvas.width : 0, x + img.width > canvas.width ? -canvas.width : 0].filter((v, i) => i === 0 || v)) {
					if (flip) {
						ctx.save();
						ctx.translate(x + dx + img.width, y);
						ctx.scale(-1, 1);
						ctx.drawImage(img.canvas, 0, 0);
						ctx.restore();
					} else {
						ctx.drawImage(img.canvas, Math.round(x + dx), Math.round(y));
					}
				}
			};
			const at = (it, img) => [it.pos.x + (it.flip ? -it.look.offset.x - img.width : it.look.offset.x), it.pos.y + it.look.offset.y];
			// a terrain object's offset: its own, or the game's default (see placedLook)
			const offsetOf = (look) => {
				if (!look.offset) {
					const imgs = [look.fg, look.bg, look.mat].map((f) => f && loaded.get(f)).filter(Boolean);
					const w = Math.max(0, ...imgs.map((i) => i.width));
					const h = Math.max(0, ...imgs.map((i) => i.height));
					look.offset = { x: w > 24 ? -Math.floor(w / 2) : 0, y: h > 24 ? -Math.floor(h / 2) : 0 };
				}
				return look.offset;
			};
			// textured: the ground's two layers made from the material map, as the game makes them (SLTerrain::TexturizeTerrain)
			const layers = textured && terrain.indices ? texturize(terrain, materials, loaded, groundBack && loaded.get(groundBack)) : null;
			// the material map as the game's ends up once the terrain has dressed itself
			const mat = layers ? new Uint8Array(terrain.indices) : null;
			if (layers) {
				drawBackdrops(ctx, backs, loaded, terrain);
				dressGround(layers.fg, mat, terrain.width, terrain.height, extras, materials, loaded, hash ^ terrain.width);
				ctx.drawImage(layers.bg, 0, 0);
			}
			for (const it of items) {
				const img = it.look.bg && loaded.get(it.look.bg);
				if (img) {
					stamp(img, it.pos.x + offsetOf(it.look).x, it.pos.y + offsetOf(it.look).y);
				}
			}
			// the terrain and the pieces' foregrounds on a layer of their own, as the game keeps them, to be cleaned below
			const fg = document.createElement('canvas');
			fg.width = canvas.width;
			fg.height = canvas.height;
			const main = ctx;
			ctx = fg.getContext('2d');
			ctx.drawImage(layers ? layers.fg : terrain.canvas, 0, 0);
			for (const it of items) {
				const img = it.look.fg && loaded.get(it.look.fg);
				if (img) {
					stamp(img, it.pos.x + offsetOf(it.look).x, it.pos.y + offsetOf(it.look).y);
				}
			}
			cleanAir(ctx, terrain, mat, items.map((it) => ({ img: it.look.mat && loaded.get(it.look.mat), x: it.pos.x + offsetOf(it.look).x, y: it.pos.y + offsetOf(it.look).y })));
			main.drawImage(fg, 0, 0);
			ctx = main;
			for (const it of items) {
				const img = it.look.sprite && loaded.get(MS.Images.framePaths(it.look.sprite, it.look.frames)[0]);
				if (img) {
					const [x, y] = at(it, img);
					stamp(img, x, y, it.flip);
				}
			}
			job.picture = { canvas, width: canvas.width, height: canvas.height };
			for (const fn of job.waiting) {
				fn(job.picture);
			}
			job.waiting.clear();
		});
		return null;
	}

	// The scene's layers, areas and terrain picture, as resolved, with which of them the scene itself holds (the rest come from a scene it copies).
	function read(p) {
		const a = app();
		const ent = a.lib.resolve(p.node);
		const layers = ent.all('AddBackgroundLayer').map((entry) => {
			const r = a.lib.resolve(entry.node);
			const ref = entry.node.child('CopyOf') ? entry.node.child('CopyOf').value : '';
			return {
				node: entry.node,
				own: entry.node.isWithin(p.node),
				name: r.str('PresetName', ref.split('/').pop() || 'Layer'),
				file: r.file('BitmapFile'),
				scroll: r.vec('ScrollRatio', { x: 1, y: 1 }),
				wrapX: r.bool('WrapX', true),
				wrapY: r.bool('WrapY', false),
				offset: r.vec('Offset', { x: 0, y: 0 }),
			};
		});
		const areas = ent.all('AddArea').map((entry) => {
			const box = entry.node.child('AddBox');
			const corner = box ? box.vector('Corner') || { x: 0, y: 0 } : { x: 0, y: 0 };
			return {
				node: entry.node,
				own: entry.node.isWithin(p.node),
				name: entry.node.childValue('Name', 'Area'),
				box,
				x: corner.x,
				y: corner.y,
				w: box ? Number(box.childValue('Width', '0')) || 0 : 0,
				h: box ? Number(box.childValue('Height', '0')) || 0 : 0,
				boxes: entry.node.children.filter((c) => c.key === 'AddBox').length,
			};
		});
		return { ent, layers, areas, terrainFile: terrainPicture(p, ent) };
	}

	// ---- the two pictures ----

	// The whole map: terrain, areas and the camera's frame. Click or drag to move the camera.
	function drawOverview(canvas, info, view, onMove) {
		const ctx = canvas.getContext('2d');
		const terrain = info.built || (info.terrainFile ? app().sprite(info.terrainFile, 1, 0) : null);
		const worldW = view.worldW;
		const worldH = view.worldH;
		const scale = canvas.width / worldW;
		canvas.height = Math.max(40, Math.round(worldH * scale));
		ctx.fillStyle = '#0d1117';
		ctx.fillRect(0, 0, canvas.width, canvas.height);
		if (terrain) {
			ctx.imageSmoothingEnabled = false;
			ctx.drawImage(terrain.canvas, 0, 0, canvas.width, terrain.height * scale);
		}
		ctx.font = '11px sans-serif';
		info.areas.forEach((area, i) => {
			const color = AREA_COLORS[i % AREA_COLORS.length];
			for (const box of area.node.children.filter((c) => c.key === 'AddBox')) {
				const corner = box.vector('Corner') || { x: 0, y: 0 };
				const w = Number(box.childValue('Width', '0')) || 0;
				const h = Number(box.childValue('Height', '0')) || 0;
				ctx.fillStyle = color + '33';
				ctx.fillRect(corner.x * scale, corner.y * scale, w * scale, h * scale);
				ctx.strokeStyle = color;
				ctx.strokeRect(corner.x * scale + 0.5, corner.y * scale + 0.5, Math.max(1, w * scale - 1), Math.max(1, h * scale - 1));
				ctx.fillStyle = color;
				ctx.fillText(area.name, corner.x * scale + 3, corner.y * scale + 12);
			}
		});
		ctx.strokeStyle = '#fff';
		ctx.lineWidth = 1.5;
		ctx.strokeRect(view.x * scale, view.y * scale, CAMERA.w * scale, CAMERA.h * scale);
		canvas.onpointerdown = (e) => {
			canvas.setPointerCapture(e.pointerId);
			const move = (ev) => {
				const r = canvas.getBoundingClientRect();
				onMove(((ev.clientX - r.left) / r.width) * worldW - CAMERA.w / 2, ((ev.clientY - r.top) / r.height) * worldH - CAMERA.h / 2);
			};
			move(e);
			canvas.onpointermove = move;
			canvas.onpointerup = () => {
				canvas.onpointermove = null;
			};
		};
	}

	// What the camera sees at one place: the backdrops, far to near, each scrolled by its own ratio, then the terrain on top. Approximate: the game
	// aligns some backdrops to the bottom and treats big Y ratios as "stays put".
	function drawCamera(canvas, info, view) {
		const ctx = canvas.getContext('2d');
		const scale = canvas.width / CAMERA.w;
		canvas.height = Math.round(CAMERA.h * scale);
		ctx.fillStyle = '#1a2233';
		ctx.fillRect(0, 0, canvas.width, canvas.height);
		ctx.imageSmoothingEnabled = false;
		for (const layer of [...info.layers].reverse()) {
			const frame = layer.file ? app().sprite(layer.file, 1, 0) : null;
			if (!frame) {
				continue;
			}
			const w = frame.width * scale;
			const h = frame.height * scale;
			const sx = layer.scroll.x;
			const sy = Math.abs(layer.scroll.y) > 10 ? 0 : layer.scroll.y;
			let x = -view.x * sx * scale + layer.offset.x * scale;
			const pinned = Math.abs(layer.scroll.y) > 10 || h >= canvas.height;
			const y = pinned ? canvas.height - h : -view.y * sy * scale + layer.offset.y * scale;
			if (layer.wrapX && w > 0) {
				x = ((x % w) + w) % w - w;
			}
			for (let dx = x; dx < canvas.width; dx += layer.wrapX ? w : canvas.width + 1) {
				ctx.drawImage(frame.canvas, dx, y, w, h);
			}
		}
		const terrain = info.built || (info.terrainFile ? app().sprite(info.terrainFile, 1, 0) : null);
		if (terrain) {
			ctx.drawImage(terrain.canvas, view.x, view.y, CAMERA.w, CAMERA.h, 0, 0, canvas.width, canvas.height);
		}
	}

	// ---- the editor ----

	function renderScenes(module) {
		return (host, tab) => {
			const a = app();
			if (!a.lib || !a.lib.presets.length) {
				host.replaceChildren(el('p', { className: 'ed ed-empty', textContent: 'Studio is still reading the game…' }));
				return;
			}
			const s = tab.state || (tab.state = { filter: '', selected: null, view: { x: 0, y: 0 } });
			const all = a.lib.presets.filter((p) => p.topLevel && p.module === module && p.cls === 'Scene').sort((x, y) => x.name.localeCompare(y.name));
			const q = s.filter.trim().toLowerCase();
			const list = all.filter((p) => !q || p.name.toLowerCase().includes(q));
			const keyOf = (p) => p.doc.path + ':' + p.name;
			const selected = all.find((p) => keyOf(p) === s.selected) || list[0] || null;
			const filter = el('input', { type: 'search', placeholder: 'Filter ' + all.length + ' scenes…', value: s.filter, className: 'ed-input' });
			filter.dataset.fk = 'filter';
			filter.oninput = () => {
				s.filter = filter.value;
				clearTimeout(s.timer);
				s.timer = setTimeout(() => MS.ide.refreshForm(), 200);
			};
			const items = list.slice(0, 500).map((p) => el('button', { className: 'ed-item' + (p === selected ? ' sel' : ''), textContent: p.name, onclick: () => { s.selected = keyOf(p); s.view = { x: 0, y: 0 }; MS.ide.refreshForm(); } }));
			host.replaceChildren(el('div', { className: 'ed ed-split' },
				el('div', { className: 'ed-head' }, el('h2', { textContent: 'Scenes · ' + module }), el('span', { className: 'spacer' }),
					el('button', { className: 'tb', textContent: 'Edit the map in the game ▾', title: "Open the game's own map editors: they run from Studio, and what you save there shows here straight away", onclick: (e) => gameEditorsMenu(e.currentTarget) }),
					el('button', { className: 'tb primary', textContent: '+ New scene…', title: "Make a scene: pick its terrain and backdrops, as the game's own scene editor does", disabled: !!(all[0] && locked(all[0].doc)), onclick: () => newScene(module, s) })),
				all[0] && locked(all[0].doc) ? el('div', { className: 'ed-banner', textContent: module + ' is read only: its scenes can be looked at. Copy one into your mod (New… → Scene) to change it.' }) : null,
				el('div', { className: 'ed-panes' },
					el('div', { className: 'ed-list' }, filter, ...(items.length ? items : [el('p', { className: 'ed-empty', textContent: 'No scenes' + (q ? ' match.' : ' in this mod. Make one with New… → Scene.') })])),
					el('div', { className: 'ed-detail' }, selected ? sceneDetail(selected, module, s) : null))));
		};
	}

	function numberField(value, placeholder, disabled, onChange) {
		const input = el('input', { type: 'number', step: 'any', className: 'ed-input num', value, placeholder, disabled });
		input.onchange = () => onChange(input.value.trim());
		return input;
	}

	function sceneDetail(p, module, s) {
		const a = app();
		const info = read(p);
		const ro = locked(p.doc);
		const terrain = info.terrainFile ? a.sprite(info.terrainFile, 1, 0) : null;
		const worldW = terrain ? terrain.width : Math.max(2000, ...info.areas.map((r) => r.x + r.w));
		const worldH = terrain ? terrain.height : Math.max(600, ...info.areas.map((r) => r.y + r.h));
		const view = { x: Math.max(0, Math.min(worldW - CAMERA.w, s.view.x)), y: Math.max(0, Math.min(worldH - CAMERA.h, s.view.y)), worldW, worldH };
		const overview = el('canvas', { className: 'scene-canvas', width: 640, height: 120, title: 'The whole map: click to look somewhere' });
		const camera = el('canvas', { className: 'scene-canvas', width: 640, height: 360, title: 'What the camera sees there, with the backdrops scrolling as set' });
		const redraw = () => {
			drawOverview(overview, info, view, (x, y) => {
				s.view = { x, y };
				view.x = Math.max(0, Math.min(worldW - CAMERA.w, x));
				view.y = Math.max(0, Math.min(worldH - CAMERA.h, y));
				redraw();
			});
			drawCamera(camera, info, view);
		};
		// the buildings and objects placed in it are drawn in once their pictures are read
		info.built = picture(p, (built) => {
			info.built = built;
			redraw();
		});
		redraw();
		// Pictures arrive a little after the editor opens (the map's size comes with the terrain's): draw the editor again once they have, once.
		const waiting = [info.terrainFile, ...info.layers.map((l) => l.file)].filter((f) => f && !a.sprite(f, 1, 0) && !asked.has(f.toLowerCase()));
		if (waiting.length) {
			waiting.forEach((f) => asked.add(f.toLowerCase()));
			Promise.all(waiting.map((f) => a.images.get(f))).then(() => setTimeout(() => MS.ide.refreshForm(), 50));
		}
		const own = (label, fn) => change(label + ' in ' + p.name, fn);

		// info
		const desc = el('textarea', { className: 'ed-input', rows: 3, value: info.ent.str('Description', ''), disabled: ro });
		desc.onchange = () => own('Change the description', () => p.node.set('Description', desc.value.replace(/\r?\n/g, ' ')));
		const loc = info.ent.vec('LocationOnPlanet', { x: 0, y: 0 });
		const playable = el('input', { type: 'checkbox', checked: info.ent.bool('MetagamePlayable', true), disabled: ro });
		playable.onchange = () => own('Change whether campaigns can use it', () => p.node.set('MetagamePlayable', playable.checked ? '1' : '0'));
		const gravity = info.ent.vec('GlobalAcceleration', { x: 0, y: 20 });

		// backdrops
		const layerRows = info.layers.map((layer, i) => {
			const thumb = el('canvas', { width: 56, height: 34, className: 'ed-thumb scene-thumb' });
			const frame = layer.file ? a.sprite(layer.file, 1, 0) : null;
			if (frame) {
				const t = thumb.getContext('2d');
				t.imageSmoothingEnabled = false;
				t.drawImage(frame.canvas, 0, 0, thumb.width, thumb.height);
			}
			const editable = layer.own && !ro;
			const scrollX = numberField(layer.scroll.x, '1', !editable, (v) => own('Set ' + layer.name + '\'s scroll speed', () => layer.node.setVector('ScrollRatio', Number(v) || 0, layer.scroll.y)));
			const scrollY = numberField(layer.scroll.y, '1', !editable, (v) => own('Set ' + layer.name + '\'s scroll speed', () => layer.node.setVector('ScrollRatio', layer.scroll.x, Number(v) || 0)));
			scrollX.title = 'Sideways: 1 moves with the terrain, 0 stays still, 0.25 drifts at a quarter';
			scrollY.title = 'Up and down: the same, for the camera moving up and down';
			const wrap = el('input', { type: 'checkbox', checked: layer.wrapX, disabled: !editable });
			wrap.onchange = () => own('Set whether ' + layer.name + ' repeats', () => layer.node.set('WrapX', wrap.checked ? '1' : '0'));
			const button = (text, title, fn, off) => el('button', { className: 'ico-btn', textContent: text, title, disabled: off || !editable, onclick: fn });
			return el('div', { className: 'scene-row' + (layer.own ? '' : ' inherited') },
				thumb,
				el('span', { className: 'scene-name', textContent: layer.name, title: (layer.file || '') + (layer.own ? '' : ' (from the scene this one copies)') }),
				el('label', { className: 'ed-check', title: 'Sideways and up/down scroll speed' }, 'Scroll', scrollX, scrollY),
				el('label', { className: 'ed-check', title: 'Repeat sideways to fill the width' }, wrap, 'Repeats'),
				button('▲', 'Nearer the front', () => own('Move ' + layer.name + ' forward', () => moveLayer(layer.node, -1)), i === 0),
				button('▼', 'Further back', () => own('Move ' + layer.name + ' back', () => moveLayer(layer.node, 1)), i === info.layers.length - 1),
				button('×', 'Take it out of the scene', () => own('Take ' + layer.name + ' out', () => layer.node.remove())));
		});
		const addLayer = el('button', { className: 'tb', textContent: '+ Add a backdrop…', disabled: ro, onclick: () => {
			const all = a.lib.presets.filter((r) => r.topLevel && r.cls === 'SLBackground' && !!r.node.child('BitmapFile'));
			const modules = [...new Set(all.map((r) => r.module))].sort((x, y) => x.localeCompare(y));
			const draw = (c, r) => {
				const file = a.lib.resolve(r.node).file('BitmapFile');
				const frame = file ? a.sprite(file, 1, 0) : null;
				if (!frame) {
					return !file;
				}
				const ctx = c.getContext('2d');
				ctx.clearRect(0, 0, c.width, c.height);
				ctx.drawImage(frame.canvas, 0, 0, c.width, c.height);
				return true;
			};
			MS.dialogs.open('Add a backdrop to ' + p.name, MS.dialogs.parts.pickerGrid(all, (r) => {
				own('Add ' + r.name, () => {
					const layer = p.node.add('AddBackgroundLayer', 'SLBackground');
					layer.add('CopyOf', a.lib.reference(r.name, r.module, p.module));
				});
			}, draw, false, [{ label: 'Where it comes from', options: [['', 'Everything'], ...modules.map((m) => [m, m.replace(/\.rte$/i, '')])], test: (r, v) => r.module === v }]), [{ text: 'Cancel' }], true);
		} });

		// areas
		const areaRows = info.areas.map((area, i) => {
			const editable = area.own && !ro && area.box;
			const color = AREA_COLORS[i % AREA_COLORS.length];
			const name = el('input', { type: 'text', className: 'ed-input', value: area.name, disabled: !area.own || ro });
			name.onchange = () => own('Rename ' + area.name, () => area.node.set('Name', name.value.trim() || area.name));
			const set = (key, label) => numberField(area[key], '0', !editable, (v) => own('Change ' + area.name + '\'s ' + label, () => {
				if (key === 'x' || key === 'y') {
					area.box.setVector('Corner', key === 'x' ? Number(v) || 0 : area.x, key === 'y' ? Number(v) || 0 : area.y);
				} else {
					area.box.set(key === 'w' ? 'Width' : 'Height', String(Math.max(0, Number(v) || 0)));
				}
			}));
			return el('div', { className: 'scene-row' + (area.own ? '' : ' inherited') },
				el('span', { className: 'scene-swatch', style: 'background:' + color }),
				name,
				el('label', { className: 'ed-check' }, 'x', set('x', 'corner')), el('label', { className: 'ed-check' }, 'y', set('y', 'corner')),
				el('label', { className: 'ed-check' }, 'wide', set('w', 'width')), el('label', { className: 'ed-check' }, 'high', set('h', 'height')),
				area.boxes > 1 ? el('small', { textContent: '+' + (area.boxes - 1) + ' more boxes' }) : null,
				el('button', { className: 'ico-btn', textContent: '×', title: 'Take the area out', disabled: !area.own || ro, onclick: () => own('Take ' + area.name + ' out', () => area.node.remove()) }));
		});
		const addArea = el('button', { className: 'tb', textContent: '+ Add an area', disabled: ro, onclick: () => own('Add an area', () => {
			const area = p.node.add('AddArea', 'Area');
			const box = area.add('AddBox', 'Box');
			box.setVector('Corner', Math.round(view.x), Math.round(view.y));
			box.add('Width', '480');
			box.add('Height', '120');
			area.add('Name', 'New Area');
		}) });

		const row = (label, ...fields) => el('div', { className: 'ed-row' }, el('span', { className: 'ed-label', textContent: label }), ...fields);
		return el('div', {},
			el('div', { className: 'ed-card-head' }, el('h3', { textContent: p.name }), el('span', { className: 'spacer' }),
				el('button', { className: 'ico-btn', textContent: 'Its lines', onclick: () => MS.ide.openFile(p.doc.path, p.node.line) })),
			el('div', { className: 'ed-label', textContent: 'The map (terrain shown as its material picture)' }), overview,
			el('div', { className: 'ed-label', textContent: 'Through the camera (position ' + Math.round(view.x) + ', ' + Math.round(view.y) + ')' }), camera,
			el('div', { className: 'ed-label', textContent: 'About it' }),
			row('Description', desc),
			row('On the planet', 'x', numberField(loc.x, '0', ro, (v) => own('Move it on the planet', () => p.node.setVector('LocationOnPlanet', Number(v) || 0, loc.y))), 'y', numberField(loc.y, '0', ro, (v) => own('Move it on the planet', () => p.node.setVector('LocationOnPlanet', loc.x, Number(v) || 0)))),
			row('Gravity (down)', numberField(gravity.y, '20', ro, (v) => own('Change gravity', () => p.node.setVector('GlobalAcceleration', gravity.x, Number(v) || 0))), el('small', { textContent: '20 is the usual, 0 floats' })),
			row('', el('label', { className: 'ed-check' }, playable, 'Campaigns can use this scene')),
			el('div', { className: 'ed-label', textContent: 'Sky and weather' }), ...atmosphere(p, info.ent, ro),
			el('div', { className: 'ed-label', textContent: 'Backdrops, nearest first (' + info.layers.length + ')' }), ...layerRows, el('div', { className: 'ed-row' }, addLayer),
			el('div', { className: 'ed-label', textContent: 'Areas (' + info.areas.length + ')' }), ...areaRows, el('div', { className: 'ed-row' }, addArea));
	}

	// Time of day, weather and wind: what the scene starts as. Blank is the game's usual (a day that follows its own clock, clear weather).
	const ATMOSPHERE = [
		['TimeOfDay', 'Hour of the day', 'The hour it starts at, 0 to 24', 'number'],
		['DayLengthMinutes', 'Day length (minutes)', 'How long a whole day takes; 0 freezes the time', 'number'],
		['Weather', 'Weather', 'The name of a weather preset, such as Diamond Dust', 'text'],
		['WeatherIntensity', 'Weather strength', '0 to 1', 'number'],
		['Wind', 'Wind', 'Pixels a second, sideways', 'number'],
		['PostShader', 'Look (post shader)', 'The name of a screen look the scene starts with', 'text'],
	];

	function atmosphere(p, ent, ro) {
		return ATMOSPHERE.map(([key, label, help, kind]) => {
			const own = p.node.child(key);
			const input = el('input', { type: kind === 'number' ? 'number' : 'text', step: 'any', className: 'ed-input' + (kind === 'number' ? ' num' : ''), value: own ? own.value : '', placeholder: ent.str(key, ''), disabled: ro });
			input.onchange = () => change('Change ' + label.toLowerCase() + ' in ' + p.name, () => {
				const v = input.value.trim();
				if (v === '') {
					if (own) {
						own.remove();
					}
				} else {
					p.node.set(key, v);
				}
			});
			return el('div', { className: 'ed-row', title: help }, el('span', { className: 'ed-label', textContent: label }), input, el('small', { textContent: help }));
		});
	}

	// ---- a new scene ----

	// The game's own editor asks for a name, a terrain and three backdrops; so does this. The scene gets the usual landing zones across its width.
	function newScene(module, s) {
		const a = app();
		const presets = (cls) => a.lib.presets.filter((p) => p.topLevel && p.cls === cls).sort((x, y) => x.name.localeCompare(y.name));
		const terrains = presets('SLTerrain');
		const backdrops = presets('SLBackground').filter((p) => a.lib.resolve(p.node).file('BitmapFile'));
		const label = (p) => p.name + (p.module === module ? '' : ' (' + p.module.replace(/[.]rte$/i, '') + ')');
		const pick = (list, none) => el('select', { className: 'ed-input' }, ...(none ? [el('option', { value: '', textContent: '(none)' })] : []), ...list.map((p, i) => el('option', { value: String(i), textContent: label(p) })));
		const name = el('input', { type: 'text', className: 'ed-input', placeholder: 'My Scene' });
		const terrain = pick(terrains, false);
		const layers = [pick(backdrops, true), pick(backdrops, true), pick(backdrops, true)];
		const sky = Math.max(backdrops.findIndex((p) => p.module === 'Base.rte' && /sky/i.test(p.name)), backdrops.findIndex((p) => /sky/i.test(p.name)));
		if (sky >= 0) {
			layers[2].value = String(sky);
		}
		const wrap = el('input', { type: 'checkbox', checked: true });
		const { field } = MS.dialogs.parts;
		MS.dialogs.open('New scene', el('div', {},
			field('Name', name, 'What it is called in the game.'),
			field('Terrain', terrain, 'The ground: its shape and what it is made of.'),
			field('Nearest backdrop', layers[0]), field('Middle backdrop', layers[1]), field('Furthest backdrop (the sky)', layers[2]),
			el('label', { className: 'ed-check' }, wrap, 'The map wraps round sideways')), [
			{ text: 'Cancel' },
			{ text: 'Make it', primary: true, action: async () => {
				const n = name.value.trim();
				if (!n) {
					return false;
				}
				if (a.lib.presets.some((p) => p.cls === 'Scene' && p.name.toLowerCase() === n.toLowerCase())) {
					a.toast('There is already a scene called ' + n + '.', 'bad');
					return false;
				}
				const picked = terrains[Number(terrain.value)];
				if (!picked) {
					a.toast('Pick a terrain first.', 'bad');
					return false;
				}
				const resolved = a.lib.resolve(picked.node);
				const file = resolved.file('BitmapFile') || resolved.file('FGColorFile');
				const loaded = file ? await a.images.get(file) : null;
				const width = loaded ? loaded.width : 4800;
				const path = module + '/Scenes/' + (n.replace(/[^A-Za-z0-9 _-]/g, '').trim().replace(/ +/g, '_') || 'NewScene') + '.ini';
				if (await a.fs.exists(path)) {
					a.toast(path + ' is already there.', 'bad');
					return false;
				}
				const doc = await a.lib.newDoc(path);
				a.hookDoc(doc);
				a.hookDoc(a.lib.docFor(module + '/Index.ini'));
				change('New scene ' + n, () => {
					const block = doc.addTopLevel('AddScene', 'Scene');
					block.add('PresetName', n);
					block.add('Description', 'Made with Cortex Mod Studio.');
					block.setVector('LocationOnPlanet', 0, 0);
					block.add('MetagamePlayable', '0');
					block.add('AutoDesigned', '0');
					block.add('Terrain', 'SLTerrain').add('CopyOf', a.lib.reference(picked.name, picked.module, module));
					block.setVector('GlobalAcceleration', 0, 20);
					for (const layer of layers) {
						const chosen = layer.value === '' ? null : backdrops[Number(layer.value)];
						if (chosen) {
							block.add('AddBackgroundLayer', 'SLBackground').add('CopyOf', a.lib.reference(chosen.name, chosen.module, module));
						}
					}
					if (!wrap.checked) {
						block.add('WrapX', '0');
					}
					const zone = (areaName, x, w, y) => {
						const area = block.add('AddArea', 'Area');
						const box = area.add('AddBox', 'Box');
						box.setVector('Corner', Math.round(x), y);
						box.add('Width', String(Math.round(w)));
						box.add('Height', '60');
						area.add('Name', areaName);
					};
					for (let i = 0; i < 4; i++) {
						zone('LZ Team ' + (i + 1), (width / 4) * i, width / 4, 20);
					}
					zone('LZ All', 0, width, 10);
				});
				const made = a.lib.presets.find((p) => p.doc === doc && p.name === n);
				if (made) {
					s.selected = made.doc.path + ':' + made.name;
				}
				await a.save(true, [doc, a.lib.docFor(module + '/Index.ini')].filter(Boolean));
				MS.ide.refreshForm();
				a.toast('Made ' + n + ' in ' + module + '. The game loads new scenes when it starts.', 'good');
				return true;
			} },
		]);
	}

	// The game's map editors (placing buildings and units, drawing areas, assemblies) are best done in the game, which has the whole editing
	// interface; this starts one from Studio. What it saves is picked up here as soon as it is written.
	function gameEditorsMenu(anchor) {
		const choices = [
			['sceneeditor', 'Scene Editor', 'Place buildings, doors, units and brains on a map, and save it'],
			['areaeditor', 'Area Editor', 'Draw the named areas of a map'],
			['assemblyeditor', 'Assembly Editor', 'Build bunker assemblies out of pieces'],
			['gibeditor', 'Gib Editor', 'Place the gibs of something'],
		];
		const box = el('div', { className: 'ed-menu' }, ...choices.map(([mode, label, help]) => el('button', { className: 'ed-menu-item', onclick: () => {
			box.remove();
			MS.launcher.launch(mode);
		} }, el('b', { textContent: label }), el('small', { textContent: help }))));
		const r = anchor.getBoundingClientRect();
		box.style.left = Math.max(8, r.left) + 'px';
		box.style.top = r.bottom + 4 + 'px';
		document.body.append(box);
		const close = (ev) => {
			if (!box.contains(ev.target)) {
				box.remove();
				document.removeEventListener('pointerdown', close, true);
			}
		};
		setTimeout(() => document.addEventListener('pointerdown', close, true), 0);
	}

	// Joins the Data view's list.
	if (MS.editors && MS.editors.register) {
		MS.editors.register(['scenes', 'Scenes', 'Maps: terrain, backdrop layers and areas', '▤', renderScenes, (module) => app().lib.presets.filter((p) => p.topLevel && p.module === module && p.cls === 'Scene').length]);
	}
	MS.scenes = { renderScenes, terrainPicture, picture, forgetAsked: (paths) => paths.forEach((p) => asked.delete(String(p).toLowerCase())) };
})();
