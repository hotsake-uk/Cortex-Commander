// Cortex Mod Studio: the actor view. Draws the posed actor pixel for pixel, and turns drags into offset changes.
//
//   drag a part            moves it on its parent (ParentOffset); a held item moves the hand that holds it (StanceOffset)
//   Shift + drag a part    moves its picture around its pivot (SpriteOffset)
//   drag the pivot dot     moves the pivot and keeps the picture where it is (JointOffset, and ParentOffset to match)
//   drag a hand            moves where the hand rests (the arm's IdleOffset), or where it holds the item (StanceOffset / SupportOffset)
//   drag the muzzle dot    moves where shots come from (MuzzleOffset)
//   wheel                  zooms at the cursor; drag empty space, or with the middle or right button, to pan
// Offsets snap to whole pixels; hold Alt for half pixels.
'use strict';
window.MS = window.MS || {};

(function () {
	function readPictures() {
		try {
			return localStorage.getItem('ms.skeletonPictures') || 'faded';
		} catch (e) {
			return 'faded';
		}
	}

	const { rotate, flipX, add, sub, len } = MS.geom;

	// A colour for each part: spread round the colour wheel by its place in the unit, so neighbours differ and a part keeps its colour.
	MS.partColor = (part) => {
		const parts = MS.app && MS.app.rig ? MS.app.rig.parts : [];
		const i = Math.max(0, parts.indexOf(part));
		return 'hsl(' + Math.round((i * 137.508 + 20) % 360) + ', 85%, 62%)';
	};

	class Viewport {
		constructor(canvas, app) {
			this.canvas = canvas;
			this.ctx = canvas.getContext('2d');
			this.app = app;
			this.zoom = 6;
			this.pan = { x: 0, y: 40 };
			this.hover = null;
			this.drag = null;
			this.handles = [];
			this.drawList = [];
			this.poses = null;
			this.masks = new WeakMap();
			this.options = { grid: true, joints: true, ground: true, ghosts: false, reach: true, skeleton: false, outlines: false }
			// With the skeleton on, the pictures are 'faded', 'hidden' or 'shown' (skeleton.js's Pictures switch).
			this.skeletonPictures = readPictures();
			this.skeletonHits = [];;
			this.bind();
		}

		bind() {
			const c = this.canvas;
			c.addEventListener('contextmenu', (e) => e.preventDefault());
			c.addEventListener('wheel', (e) => {
				e.preventDefault();
				const before = this.toWorld(e.offsetX, e.offsetY);
				const factor = e.deltaY < 0 ? 1.15 : 1 / 1.15;
				this.setZoom(this.zoom * factor);
				const after = this.toWorld(e.offsetX, e.offsetY);
				this.pan.x += (after.x - before.x) * this.zoom;
				this.pan.y += (after.y - before.y) * this.zoom;
				this.app.requestDraw();
			}, { passive: false });
			c.addEventListener('pointerdown', (e) => this.onDown(e));
			c.addEventListener('pointermove', (e) => this.onMove(e));
			c.addEventListener('pointerup', (e) => this.onUp(e));
			c.addEventListener('pointercancel', (e) => this.onUp(e));
			c.addEventListener('dblclick', () => this.fit());
			new ResizeObserver(() => this.app.requestDraw()).observe(c);
		}

		setZoom(z) {
			this.zoom = Math.min(Math.max(z, 1), 40);
			this.app.onZoom(this.zoom);
		}

		fit() {
			const box = this.bounds();
			if (!box) {
				return;
			}
			const w = this.canvas.clientWidth;
			const h = this.canvas.clientHeight;
			const zoom = Math.floor(Math.min(w / (box.w + 30), h / (box.h + 30)));
			this.setZoom(Math.max(1, zoom));
			this.pan = { x: -(box.x + box.w / 2) * this.zoom, y: -(box.y + box.h / 2) * this.zoom };
			this.app.requestDraw();
		}

		bounds() {
			let box = null;
			for (const item of this.drawList) {
				const r = this.itemRect(item);
				if (!r) {
					continue;
				}
				for (const corner of r) {
					if (!box) {
						box = { x0: corner.x, y0: corner.y, x1: corner.x, y1: corner.y };
					}
					box.x0 = Math.min(box.x0, corner.x);
					box.y0 = Math.min(box.y0, corner.y);
					box.x1 = Math.max(box.x1, corner.x);
					box.y1 = Math.max(box.y1, corner.y);
				}
			}
			return box ? { x: box.x0, y: box.y0, w: box.x1 - box.x0, h: box.y1 - box.y0 } : null;
		}

		origin() {
			return { x: this.canvas.clientWidth / 2 + this.pan.x, y: this.canvas.clientHeight / 2 + this.pan.y };
		}

		toWorld(sx, sy) {
			const o = this.origin();
			return { x: (sx - o.x) / this.zoom, y: (sy - o.y) / this.zoom };
		}

		toScreen(p) {
			const o = this.origin();
			return { x: o.x + p.x * this.zoom, y: o.y + p.y * this.zoom };
		}

		// ---- drawing ----

		render() {
			const app = this.app;
			const c = this.canvas;
			const dpr = window.devicePixelRatio || 1;
			const w = c.clientWidth;
			const h = c.clientHeight;
			if (c.width !== Math.round(w * dpr) || c.height !== Math.round(h * dpr)) {
				c.width = Math.round(w * dpr);
				c.height = Math.round(h * dpr);
			}
			const ctx = this.ctx;
			ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
			ctx.imageSmoothingEnabled = false;
			const css = getComputedStyle(document.documentElement);
			ctx.fillStyle = css.getPropertyValue('--view-bg') || '#101216';
			ctx.fillRect(0, 0, w, h);
			if (!app.rig) {
				this.drawList = [];
				return;
			}
			const o = this.origin();
			const z = this.zoom;
			if (this.options.grid) {
				this.drawGrid(ctx, o, z, w, h);
			}
			const poses = app.rig.pose(app.poseState);
			this.poses = poses;
			const shown = (item) => !app.isHidden(item.part);
			this.drawList = MS.drawOrder(app.rig, poses).filter(shown);

			ctx.save();
			ctx.translate(o.x, o.y);
			ctx.scale(z, z);
			if (this.options.ground) {
				this.drawGround(ctx, poses, w / z, o);
			}
			if (this.options.ghosts && app.poseState.pose !== 'stand') {
				for (const dt of [-0.12, 0.12]) {
					const ghost = app.rig.pose(Object.assign({}, app.poseState, { time: app.poseState.time + dt }));
					ctx.globalAlpha = 0.22;
					for (const item of MS.drawOrder(app.rig, ghost).filter(shown)) {
						this.drawItem(ctx, item);
					}
				}
				ctx.globalAlpha = 1;
			}
			// With the skeleton on, the sprites fade so the bones read.
			const fade = !this.options.skeleton ? 1 : this.skeletonPictures === 'hidden' ? 0 : this.skeletonPictures === 'shown' ? 1 : 0.3;
			for (const item of this.drawList) {
				ctx.globalAlpha = fade * (this.app.isolate && this.app.selected && item.part !== this.app.selected ? 0.35 : 1);
				this.drawItem(ctx, item);
			}
			ctx.globalAlpha = 1;
			// One outline a part, each in its own colour, as bracket colouring does: it follows the picture's own shape (the see-through
			// is left out).
			if (this.options.outlines) {
				for (const item of this.drawList) {
					const color = MS.partColor(item.part);
					this.drawItem(ctx, item, (frame) => this.silhouette(frame, color));
				}
			}
			ctx.restore();
			if (this.options.skeleton) {
				this.drawSkeleton(ctx, poses);
			}
			this.drawOverlays(ctx, poses);
			this.updateSkeletonHelp();
		}

		// The skeleton: for every part, the bone from its parent's origin (■) to where it joins (●, the parent's ParentOffset), and on to
		// its own origin (its JointOffset, back from the joint). Arms reach to the hand, legs to the ankle. Each role has its colour and name.
		drawSkeleton(ctx, poses) {
			const app = this.app;
			const COLOURS = { body: '#f2b63d', head: '#ff8f6b', jetpack: '#b48cff', fgarm: '#4fc37a', bgarm: '#2f8f57', fgleg: '#7fd1ff', bgleg: '#4a90b8', foot: '#a8e6ff', device: '#ef6a5b', magazine: '#e3a3ff', flash: '#ffd84f', turret: '#ff8f6b', mounted: '#ef6a5b', leg: '#7fd1ff', engine: '#b48cff', thruster: '#b48cff', door: '#c8ccd6' };
			const colourOf = (part) => COLOURS[part.role] || '#c8ccd6';
			const names = MS.Rig.ROLE_NAMES;
			const line = (a, b, colour, width, dash) => {
				ctx.strokeStyle = colour;
				ctx.lineWidth = width;
				ctx.setLineDash(dash || []);
				ctx.beginPath();
				ctx.moveTo(a.x, a.y);
				ctx.lineTo(b.x, b.y);
				ctx.stroke();
				ctx.setLineDash([]);
			};
			const disc = (p, r, fill, stroke) => {
				ctx.beginPath();
				ctx.arc(p.x, p.y, r, 0, Math.PI * 2);
				ctx.fillStyle = fill;
				ctx.fill();
				ctx.lineWidth = 1.5;
				ctx.strokeStyle = stroke || 'rgba(0,0,0,.8)';
				ctx.stroke();
			};
			const square = (p, size, fill) => {
				ctx.fillStyle = fill;
				ctx.fillRect(p.x - size / 2, p.y - size / 2, size, size);
				ctx.strokeStyle = 'rgba(0,0,0,.8)';
				ctx.lineWidth = 1;
				ctx.strokeRect(p.x - size / 2, p.y - size / 2, size, size);
			};
			const labels = [];
			this.skeletonHits = [];
			for (const [part, pose] of poses) {
				if (!pose || !pose.pos || app.isHidden(part)) {
					continue;
				}
				const colour = colourOf(part);
				const selected = part === app.selected;
				const at = this.toScreen(pose.pos);
				const parentPose = part.parent ? poses.get(part.parent) : null;
				if (parentPose && pose.joint) {
					const from = this.toScreen(parentPose.pos);
					const joint = this.toScreen(pose.joint);
					line(from, joint, 'rgba(0,0,0,.6)', selected ? 6 : 5);
					line(from, joint, colour, selected ? 4 : 3);
					line(joint, at, colour, 1.5, [3, 3]);
					disc(joint, selected ? 6.5 : 5, colour, selected ? '#fff' : null);
				}
				if (pose.hand) {
					const hand = this.toScreen(pose.hand);
					line(this.toScreen(pose.joint || pose.pos), hand, colour, 2);
					disc(hand, 3.5, '#fff', colour);
				}
				if (pose.ankle) {
					const ankle = this.toScreen(pose.ankle);
					line(this.toScreen(pose.joint || pose.pos), ankle, colour, 2);
					disc(ankle, 3.5, '#fff', colour);
				}
				square(at, part.role === 'body' ? 9 : 6, colour);
				// Joints and origins can be clicked to pick the part (the pictures may be hidden).
				this.skeletonHits.push({ part, at });
				if (parentPose && pose.joint) {
					this.skeletonHits.push({ part, at: this.toScreen(pose.joint) });
				}
				labels.push({ at: parentPose && pose.joint ? this.toScreen(pose.joint) : at, text: names[part.role] || part.role, colour, selected });
			}
			ctx.font = '11px system-ui, sans-serif';
			ctx.textBaseline = 'middle';
			const taken = [];
			for (const l of labels) {
				const width = ctx.measureText(l.text).width + 8;
				let x = l.at.x + 9;
				let y = l.at.y - 10;
				// Keep labels off each other: step down until this one has room.
				while (taken.some((t) => x < t.x + t.w && x + width > t.x && Math.abs(y - t.y) < 15)) {
					y += 15;
				}
				taken.push({ x, y, w: width });
				ctx.fillStyle = l.selected ? 'rgba(242,182,61,.95)' : 'rgba(0,0,0,.7)';
				ctx.fillRect(x, y - 7.5, width, 15);
				ctx.fillStyle = l.selected ? '#1d1503' : l.colour;
				ctx.fillText(l.text, x + 4, y);
			}
		}

		// The words beside the skeleton: what the marks mean, and the selected part's numbers.
		updateSkeletonHelp() {
			const box = document.getElementById('skeletonHelp');
			if (!box) {
				return;
			}
			box.hidden = !this.options.skeleton || !this.app.rig;
			if (box.hidden) {
				return;
			}
			const sel = this.app.selected;
			const key = sel ? sel.id + '|' + this.app.lib.generation : '-';
			if (box.dataset.key === key) {
				return;
			}
			box.dataset.key = key;
			MS.skeletonHelp(box, sel);
		}

		drawGrid(ctx, o, z, w, h) {
			if (z >= 5) {
				ctx.strokeStyle = 'rgba(255,255,255,0.035)';
				ctx.lineWidth = 1;
				ctx.beginPath();
				const startX = ((o.x % z) + z) % z;
				const startY = ((o.y % z) + z) % z;
				for (let x = startX; x < w; x += z) {
					ctx.moveTo(Math.round(x) + 0.5, 0);
					ctx.lineTo(Math.round(x) + 0.5, h);
				}
				for (let y = startY; y < h; y += z) {
					ctx.moveTo(0, Math.round(y) + 0.5);
					ctx.lineTo(w, Math.round(y) + 0.5);
				}
				ctx.stroke();
			}
			// Every 10 pixels, and the actor's origin.
			ctx.strokeStyle = 'rgba(255,255,255,0.07)';
			ctx.beginPath();
			const step = z * 10;
			for (let x = ((o.x % step) + step) % step; x < w; x += step) {
				ctx.moveTo(Math.round(x) + 0.5, 0);
				ctx.lineTo(Math.round(x) + 0.5, h);
			}
			for (let y = ((o.y % step) + step) % step; y < h; y += step) {
				ctx.moveTo(0, Math.round(y) + 0.5);
				ctx.lineTo(w, Math.round(y) + 0.5);
			}
			ctx.stroke();
		}

		drawGround(ctx, poses, worldWidth, o) {
			const ground = this.app.groundY;
			if (ground === null || ground === undefined) {
				return;
			}
			const left = -o.x / this.zoom - 2;
			const right = left + worldWidth + 4;
			const grad = ctx.createLinearGradient(0, ground, 0, ground + 40);
			grad.addColorStop(0, 'rgba(120,98,70,0.55)');
			grad.addColorStop(1, 'rgba(120,98,70,0)');
			ctx.fillStyle = grad;
			ctx.fillRect(left, ground, right - left, 40);
			ctx.fillStyle = 'rgba(176,146,104,0.9)';
			ctx.fillRect(left, ground, right - left, 1);
			// Pebbles that scroll past as the actor walks.
			const travel = poses.travel || 0;
			ctx.fillStyle = 'rgba(70,56,40,0.9)';
			for (let x = Math.floor((left + travel) / 7) * 7; x < right + travel; x += 7) {
				const jitter = ((x * 7919) % 5 + 5) % 5;
				ctx.fillRect(x - travel, ground + 2 + jitter, 1, 1);
			}
		}

		frameFor(item) {
			const app = this.app;
			if (item.kind === 'hand') {
				return app.sprite(item.pose.handFile, 1, 0);
			}
			const s = item.part.sprite;
			return app.sprite(s.file, s.frameCount, item.pose.frame || 0);
		}

		spriteOffset(item, frame) {
			const s = item.part.sprite;
			if (!s.offset || (s.offset.x === 0 && s.offset.y === 0)) {
				return { x: -frame.width / 2, y: -frame.height / 2 };
			}
			return s.offset;
		}

		drawItem(ctx, item, tint) {
			const frame = this.frameFor(item);
			if (!frame) {
				return;
			}
			const image = tint ? tint(frame) : frame.canvas;
			const pad = tint ? 1 : 0;
			const pose = item.pose;
			ctx.save();
			if (item.kind === 'hand') {
				const x = Math.floor(pose.hand.x - Math.floor(frame.width / 2));
				const y = Math.floor(pose.hand.y - Math.floor(frame.height / 2));
				if (pose.flip) {
					ctx.translate(x + frame.width, y);
					ctx.scale(-1, 1);
					ctx.drawImage(image, -pad, -pad);
				} else {
					ctx.drawImage(image, x - pad, y - pad);
				}
			} else {
				const so = this.spriteOffset(item, frame);
				const rotated = Math.abs(pose.rot % (Math.PI * 2)) > 1e-4;
				ctx.translate(rotated ? pose.pos.x : Math.floor(pose.pos.x), rotated ? pose.pos.y : Math.floor(pose.pos.y));
				ctx.rotate(-pose.rot);
				if (pose.flip) {
					ctx.scale(-1, 1);
				}
				ctx.drawImage(image, so.x - pad, so.y - pad);
			}
			ctx.restore();
		}

		// A one pixel ring just outside a picture's solid pixels, in a colour (cached by picture and colour), for the part outlines.
		silhouette(frame, color) {
			if (!this.rings) {
				this.rings = new WeakMap();
			}
			let byColor = this.rings.get(frame.canvas);
			if (!byColor) {
				byColor = new Map();
				this.rings.set(frame.canvas, byColor);
			}
			if (byColor.has(color)) {
				return byColor.get(color);
			}
			const w = frame.width;
			const h = frame.height;
			const alpha = frame.canvas.getContext('2d').getImageData(0, 0, w, h).data;
			const solid = (x, y) => x >= 0 && y >= 0 && x < w && y < h && alpha[(y * w + x) * 4 + 3] > 0;
			const out = document.createElement('canvas');
			out.width = w + 2;
			out.height = h + 2;
			const octx = out.getContext('2d');
			octx.fillStyle = color;
			for (let y = -1; y <= h; y++) {
				for (let x = -1; x <= w; x++) {
					if (!solid(x, y) && (solid(x - 1, y) || solid(x + 1, y) || solid(x, y - 1) || solid(x, y + 1))) {
						octx.fillRect(x + 1, y + 1, 1, 1);
					}
				}
			}
			byColor.set(color, out);
			return out;
		}

		// The four corners of an item's picture, in world space.
		itemRect(item) {
			const frame = this.frameFor(item);
			if (!frame) {
				return null;
			}
			const pose = item.pose;
			if (item.kind === 'hand') {
				const x = Math.floor(pose.hand.x - Math.floor(frame.width / 2));
				const y = Math.floor(pose.hand.y - Math.floor(frame.height / 2));
				return [{ x, y }, { x: x + frame.width, y }, { x: x + frame.width, y: y + frame.height }, { x, y: y + frame.height }];
			}
			const so = this.spriteOffset(item, frame);
			const local = [so, { x: so.x + frame.width, y: so.y }, { x: so.x + frame.width, y: so.y + frame.height }, { x: so.x, y: so.y + frame.height }];
			return local.map((p) => add(pose.pos, rotate(flipX(p, pose.flip), pose.rot)));
		}

		drawOverlays(ctx, poses) {
			const app = this.app;
			const sel = app.selected;
			this.handles = [];
			const outline = (item, color, width) => {
				const r = this.itemRect(item);
				if (!r) {
					return;
				}
				ctx.strokeStyle = color;
				ctx.lineWidth = width;
				ctx.beginPath();
				r.forEach((p, i) => {
					const s = this.toScreen(p);
					i ? ctx.lineTo(s.x, s.y) : ctx.moveTo(s.x, s.y);
				});
				ctx.closePath();
				ctx.stroke();
			};
			if (this.hover && this.hover !== sel) {
				for (const item of this.drawList) {
					if (item.part === this.hover && item.kind === 'sprite') {
						outline(item, 'rgba(255,255,255,0.35)', 1);
					}
				}
			}
			if (!sel) {
				return;
			}
			const pose = poses.get(sel);
			if (!pose) {
				return;
			}
			for (const item of this.drawList) {
				if (item.part === sel && item.kind === 'sprite') {
					outline(item, 'rgba(242,182,61,0.95)', 1.5);
				}
			}
			const accent = '#f2b63d';
			const dot = (p, color, r, kind, label) => {
				const s = this.toScreen(p);
				ctx.fillStyle = color;
				ctx.strokeStyle = 'rgba(0,0,0,0.75)';
				ctx.lineWidth = 2;
				ctx.beginPath();
				ctx.arc(s.x, s.y, r, 0, Math.PI * 2);
				ctx.stroke();
				ctx.fill();
				if (label && this.options.joints) {
					ctx.font = '11px system-ui, sans-serif';
					ctx.fillStyle = 'rgba(0,0,0,0.6)';
					const tw = ctx.measureText(label).width;
					ctx.fillRect(s.x + 8, s.y - 8, tw + 8, 16);
					ctx.fillStyle = '#e8eaf0';
					ctx.fillText(label, s.x + 12, s.y + 4);
				}
				if (kind) {
					this.handles.push({ kind, at: s, part: sel });
				}
			};
			const ring = (center, radius, color, dash) => {
				const s = this.toScreen(center);
				ctx.strokeStyle = color;
				ctx.lineWidth = 1;
				ctx.setLineDash(dash || []);
				ctx.beginPath();
				ctx.arc(s.x, s.y, radius * this.zoom, 0, Math.PI * 2);
				ctx.stroke();
				ctx.setLineDash([]);
			};
			// Where the part hangs from its parent, and its own origin.
			if (sel.parent || sel.role === 'device') {
				const parentPose = sel.parent ? poses.get(sel.parent) : null;
				if (parentPose && this.options.joints) {
					const a = this.toScreen(parentPose.pos);
					const b = this.toScreen(pose.joint);
					ctx.strokeStyle = 'rgba(242,182,61,0.5)';
					ctx.setLineDash([3, 3]);
					ctx.beginPath();
					ctx.moveTo(a.x, a.y);
					ctx.lineTo(b.x, b.y);
					ctx.stroke();
					ctx.setLineDash([]);
				}
			}
			const origin = this.toScreen(pose.pos);
			ctx.strokeStyle = '#7fd1ff';
			ctx.lineWidth = 1.5;
			ctx.beginPath();
			ctx.moveTo(origin.x - 5, origin.y);
			ctx.lineTo(origin.x + 5, origin.y);
			ctx.moveTo(origin.x, origin.y - 5);
			ctx.lineTo(origin.x, origin.y + 5);
			ctx.stroke();

			const role = sel.role;
			if ((role === 'fgarm' || role === 'bgarm') && pose.hand) {
				if (this.options.reach) {
					ring(pose.joint, pose.reach.max, 'rgba(127,209,255,0.45)');
					ring(pose.joint, pose.reach.min, 'rgba(127,209,255,0.3)', [3, 3]);
				}
				const holding = role === 'fgarm' && app.rig.device;
				const label = holding ? 'Grip (StanceOffset)' : pose.supporting ? 'Support (SupportOffset)' : 'Hand rest (IdleOffset)';
				dot(pose.hand, '#7fd1ff', 5, holding ? 'stance' : pose.supporting ? 'support' : 'idle', label);
			}
			if ((role === 'fgleg' || role === 'bgleg') && pose.ankle && this.options.reach) {
				ring(pose.joint, pose.reach.max, 'rgba(127,209,255,0.45)');
				ring(pose.joint, pose.reach.min, 'rgba(127,209,255,0.3)', [3, 3]);
				dot(pose.ankle, '#7fd1ff', 3, null, null);
			}
			if (role === 'device') {
				if (pose.muzzle) {
					dot(pose.muzzle, '#ef6a5b', 5, 'muzzle', 'Muzzle');
				}
				if (pose.support) {
					dot(pose.support, '#7fd1ff', 4, 'support', 'Support');
				}
			}
			if (role === 'body' && app.rig.isDevice) {
				if (pose.muzzle) {
					dot(pose.muzzle, '#ef6a5b', 5, 'muzzle', 'Muzzle (MuzzleOffset)');
				}
				if (pose.eject) {
					dot(pose.eject, '#c9a0ff', 4, 'eject', 'Shells (EjectionOffset)');
				}
				if (pose.support) {
					dot(pose.support, '#7fd1ff', 4, 'support', 'Support hand (SupportOffset)');
				}
				dot(pose.joint, accent, 6, 'joint', 'Grip (JointOffset)');
			}
			if (pose.joint && (sel.parent || role === 'device')) {
				dot(pose.joint, accent, 6, 'joint', this.options.joints ? 'Pivot (JointOffset)' : null);
			}
		}

		// ---- interaction ----

		handleAt(x, y) {
			for (let i = this.handles.length - 1; i >= 0; i--) {
				const h = this.handles[i];
				if (Math.hypot(h.at.x - x, h.at.y - y) <= 8) {
					return h;
				}
			}
			return null;
		}

		partAt(x, y) {
			if (this.options.skeleton) {
				let best = null;
				let bestDist = 9;
				for (const hit of this.skeletonHits) {
					const d = Math.hypot(hit.at.x - x, hit.at.y - y);
					if (d < bestDist) {
						best = hit.part;
						bestDist = d;
					}
				}
				if (best || this.skeletonPictures === 'hidden') {
					return best;
				}
			}
			const world = this.toWorld(x, y);
			for (let i = this.drawList.length - 1; i >= 0; i--) {
				const item = this.drawList[i];
				const frame = this.frameFor(item);
				if (!frame) {
					continue;
				}
				let px;
				let py;
				if (item.kind === 'hand') {
					const hx = Math.floor(item.pose.hand.x - Math.floor(frame.width / 2));
					const hy = Math.floor(item.pose.hand.y - Math.floor(frame.height / 2));
					px = Math.floor(world.x - hx);
					py = Math.floor(world.y - hy);
					if (item.pose.flip) {
						px = frame.width - 1 - px;
					}
				} else {
					const pose = item.pose;
					const local = flipX(rotate(sub(world, pose.pos), -pose.rot), pose.flip);
					const so = this.spriteOffset(item, frame);
					px = Math.floor(local.x - so.x);
					py = Math.floor(local.y - so.y);
				}
				if (px >= 0 && py >= 0 && px < frame.width && py < frame.height && this.opaque(frame, px, py)) {
					return item.part;
				}
			}
			return null;
		}

		opaque(frame, x, y) {
			let mask = this.masks.get(frame);
			if (!mask) {
				if (frame.indices) {
					mask = frame.indices;
				} else {
					const data = frame.canvas.getContext('2d').getImageData(0, 0, frame.width, frame.height).data;
					mask = new Uint8Array(frame.width * frame.height);
					for (let i = 0; i < mask.length; i++) {
						mask[i] = data[i * 4 + 3] > 0 ? 1 : 0;
					}
				}
				this.masks.set(frame, mask);
			}
			return mask[y * frame.width + x] !== 0;
		}

		onDown(e) {
			this.canvas.setPointerCapture(e.pointerId);
			const pan = e.button === 1 || e.button === 2;
			if (!pan && this.app.rig) {
				const handle = this.handleAt(e.offsetX, e.offsetY);
				const part = handle ? handle.part : this.partAt(e.offsetX, e.offsetY);
				if (part) {
					if (part !== this.app.selected) {
						this.app.select(part);
					}
					const kind = handle ? handle.kind : e.ctrlKey || e.metaKey ? 'rotate' : e.shiftKey ? 'sprite' : 'move';
					this.drag = { kind, part, start: this.toWorld(e.offsetX, e.offsetY), poses: this.poses, began: false };
					return;
				}
				if (!e.shiftKey) {
					this.app.select(null);
				}
			}
			this.drag = { kind: 'pan', sx: e.offsetX, sy: e.offsetY, pan: Object.assign({}, this.pan) };
			// A right-click that isn't a drag opens the layer's menu (a right-drag pans, as before).
			this.rightClick = e.button === 2 && this.app.rig ? { x: e.offsetX, y: e.offsetY, cx: e.clientX, cy: e.clientY, part: (this.handleAt(e.offsetX, e.offsetY) ? this.app.selected : null) || this.partAt(e.offsetX, e.offsetY) } : null;
		}

		onMove(e) {
			const d = this.drag;
			if (!d) {
				const part = this.app.rig ? this.handleAt(e.offsetX, e.offsetY) ? this.app.selected : this.partAt(e.offsetX, e.offsetY) : null;
				const handle = this.app.rig && this.handleAt(e.offsetX, e.offsetY);
				this.canvas.style.cursor = handle ? 'grab' : part ? 'move' : 'default';
				if (part !== this.hover) {
					this.hover = part;
					this.app.onHover(part);
					this.app.requestDraw();
				}
				return;
			}
			if (d.kind === 'pan') {
				this.pan.x = d.pan.x + e.offsetX - d.sx;
				this.pan.y = d.pan.y + e.offsetY - d.sy;
				this.app.requestDraw();
				return;
			}
			const now = this.toWorld(e.offsetX, e.offsetY);
			const delta = sub(now, d.start);
			if (!d.began) {
				if (len(delta) * this.zoom < 2) {
					return;
				}
				if (!this.app.beginDrag(d)) {
					this.drag = null;
					return;
				}
				d.began = true;
			}
			this.app.dragTo(d, delta, e.altKey ? 0.5 : 1);
		}

		onUp(e) {
			const d = this.drag;
			this.drag = null;
			const rc = this.rightClick;
			this.rightClick = null;
			if (rc && d && d.kind === 'pan' && Math.hypot(e.offsetX - rc.x, e.offsetY - rc.y) < 4 && rc.part) {
				MS.panels.showPartMenu(rc.part, rc.cx, rc.cy);
			}
			if (d && d.began) {
				this.app.endDrag(d);
			}
			try {
				this.canvas.releasePointerCapture(e.pointerId);
			} catch (err) {
				// Already released.
			}
		}
	}

	MS.Viewport = Viewport;
})();
