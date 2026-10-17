// Cortex Mod Studio: an actor's parts and where the game puts them.
//
// This mirrors the engine's placement so a modder sees what the game will show:
//   - Attachable::UpdatePositionAndJointPositionBasedOnOffsets: a part's joint sits at its parent's position plus the parent's
//     rotated ParentOffset, and the part's position is the joint minus its own rotated JointOffset.
//   - MOSprite::RotateOffset: an offset is mirrored when the object faces left, then rotated (positive angles turn anticlockwise on
//     screen); the sprite is drawn at SpriteOffset from the object's position.
//   - Arm::UpdateHandCurrentOffset / UpdateArmFrame: the hand goes to the held item's StanceOffset (or the arm's IdleOffset), kept
//     between half and all of MaxLength; the arm points at the hand and its frame follows how far it reaches.
//   - Leg::UpdateCurrentAnkleOffset / UpdateLegRotation / UpdateFootFrameAndRotation: the ankle goes to the foot's place on the
//     actor's LimbPath; the leg's frame and extra turn follow how far it stretches between ContractedOffset and ExtendedOffset.
//   - AHuman: head turns towards the aim, the body leans by the state's RotAngleTarget, and the draw order puts the front arm and
//     its held item on top.
// What it leaves out is physics: in the game the body's height and lean settle from the legs pushing on the ground, and walking
// legs follow the path by pushing. The in-game preview shows that exactly.
'use strict';
window.MS = window.MS || {};

(function () {
	const HALF_PI = Math.PI / 2;
	const PPM = 20; // pixels per metre (Constants.h c_PPM)

	const rotate = (v, a) => {
		const c = Math.cos(a);
		const s = Math.sin(a);
		return { x: v.x * c + v.y * s, y: -v.x * s + v.y * c };
	};
	const flipX = (v, f) => (f ? { x: -v.x, y: v.y } : { x: v.x, y: v.y });
	const add = (a, b) => ({ x: a.x + b.x, y: a.y + b.y });
	const sub = (a, b) => ({ x: a.x - b.x, y: a.y - b.y });
	const scale = (a, s) => ({ x: a.x * s, y: a.y * s });
	const len = (v) => Math.hypot(v.x, v.y);
	const absAngle = (v) => {
		const a = -Math.atan2(v.y, v.x);
		return a < -HALF_PI ? a + Math.PI * 2 : a;
	};
	const clampLen = (v, min, max) => {
		const l = len(v);
		if (l === 0) {
			return { x: min, y: 0 };
		}
		const t = Math.min(Math.max(l, min), max);
		return scale(v, t / l);
	};
	const easeOut = (start, end, p) => (p <= 0 ? start : p >= 1 ? end : (end - start) * -Math.sin(-p * HALF_PI) + start);
	const rotOff = (obj, v) => rotate(flipX(v, obj.flip), obj.rot);

	// Keys that hold one part each, and what they are.
	// Craft, crabs and turrets have their own part keys (each with a short form the game also reads); their parts sit on their parent like
	// any attachment.
	const SINGLE_PARTS = {
		AHuman: { Head: 'head', Jetpack: 'jetpack', FGArm: 'fgarm', BGArm: 'bgarm', FGLeg: 'fgleg', BGLeg: 'bgleg' },
		Leg: { Foot: 'foot' },
		HDFirearm: { Magazine: 'magazine', Flash: 'flash' },
		ACrab: { Turret: 'turret', Jetpack: 'jetpack', LeftFGLeg: 'leg', LFGLeg: 'leg', LeftBGLeg: 'leg', LBGLeg: 'leg', RightFGLeg: 'leg', RFGLeg: 'leg', RightBGLeg: 'leg', RBGLeg: 'leg' },
		Turret: { MountedDevice: 'mounted' },
		ACDropShip: { RightEngine: 'engine', RThruster: 'engine', RightThruster: 'engine', LeftEngine: 'engine', LThruster: 'engine', LeftThruster: 'engine', UpRightThruster: 'thruster', URThruster: 'thruster', UpLeftThruster: 'thruster', ULThruster: 'thruster', RightHatchDoor: 'door', RHatchDoor: 'door', LeftHatchDoor: 'door', LHatchDoor: 'door' },
		ACRocket: { RightLeg: 'leg', RLeg: 'leg', LeftLeg: 'leg', LLeg: 'leg', MainThruster: 'engine', MThruster: 'engine', RightThruster: 'thruster', RThruster: 'thruster', LeftThruster: 'thruster', LThruster: 'thruster', UpRightThruster: 'thruster', URThruster: 'thruster', UpLeftThruster: 'thruster', ULThruster: 'thruster' },
	};
	const LIST_PARTS = ['AddAttachable', 'AddAEmitter', 'AddEmitter', 'AddMountedDevice'];
	const ROLE_NAMES = { body: 'Body', head: 'Head', jetpack: 'Jetpack', fgarm: 'Front arm', bgarm: 'Back arm', fgleg: 'Front leg', bgleg: 'Back leg', foot: 'Foot', magazine: 'Magazine', flash: 'Muzzle flash', device: 'Held item', attachable: 'Attachment', leg: 'Leg', turret: 'Turret', mounted: 'Mounted gun', engine: 'Engine', thruster: 'Thruster', door: 'Hatch door' };
	// Roles placed by their own rules (AHuman limbs, the held item); every other part hangs on its parent and can be turned.
	const POSED_ROLES = new Set(['body', 'fgarm', 'bgarm', 'fgleg', 'bgleg', 'foot', 'device', 'head']);
	// Objects held in a hand: designed on their own, they turn with the aim and show where they're held.
	const DEVICE_CLASSES = new Set(['HDFirearm', 'HeldDevice', 'TDExplosive', 'ThrownDevice']);

	// "UpRightThruster" or "URThruster" -> " (up right)".
	function sideOf(key) {
		let k = key.replace(/(Thruster|Engine|HatchDoor|Leg)$/, '');
		const words = [];
		const take = (re, word) => {
			const m = re.exec(k);
			if (m) {
				words.push(word);
				k = k.slice(m[0].length);
			}
			return !!m;
		};
		take(/^(Up|U)(?=[A-Z]|$)/, 'up');
		take(/^(Right|R)(?=[A-Z]|$)/, 'right') || take(/^(Left|L)(?=[A-Z]|$)/, 'left') || take(/^(Main|M)(?=[A-Z]|$)/, 'main');
		take(/^FG/, 'front') || take(/^BG/, 'back');
		return words.length ? ' (' + words.join(' ') + ')' : '';
	}

	class Part {
		constructor(rig, role, ent, path, parent, anchor) {
			this.rig = rig;
			this.role = role;
			this.ent = ent;
			this.path = path; // steps from the anchor block, for editing
			this.anchor = anchor; // the block edits are made from (the actor, or the held item's preset)
			this.parent = parent;
			this.children = [];
			const steps = path.map((st) => st.key + (st.index ? '#' + st.index : '')).join('/');
			this.id = (anchor === rig.actorNode ? '' : 'item:') + (steps || role);
		}

		get name() {
			const preset = this.ent.presetName || (this.ent.node.child('CopyOf') ? this.ent.node.child('CopyOf').value : '');
			const step = this.path[this.path.length - 1];
			// Parts that come in left and right pairs are told apart by their key.
			const side = step && ['leg', 'engine', 'thruster', 'door'].includes(this.role) ? sideOf(step.key) : '';
			return ROLE_NAMES[this.role] + side + (preset && this.role !== 'body' ? ' · ' + preset : '');
		}

		// A terrain object (a piece of a building) has three pictures instead of one: foreground, background and the material map.
		get layers() {
			if (this.ent.cls !== 'TerrainObject') {
				return null;
			}
			return [['FGColorFile', 'Foreground'], ['BGColorFile', 'Background'], ['MaterialFile', 'Material']].map(([key, label]) => ({ key, label, file: this.ent.file(key) }));
		}

		get sprite() {
			if (this.ent.cls === 'TerrainObject') {
				const fg = this.ent.file('FGColorFile');
				return { file: fg || this.ent.file('BGColorFile'), key: fg ? 'FGColorFile' : 'BGColorFile', frameCount: 1, offset: null };
			}
			return {
				file: this.ent.file('SpriteFile'),
				frameCount: Math.max(1, Math.round(this.ent.num('FrameCount', 1))),
				offset: this.ent.vec('SpriteOffset', null),
			};
		}

		get drawAfterParent() {
			if (this.role === 'fgarm') {
				return true;
			}
			if (this.role === 'bgarm' || this.role === 'bgleg') {
				return false;
			}
			return this.ent.bool('DrawAfterParent', true);
		}
	}

	class Rig {
		// An attachment's turn relative to its parent, in radians: InheritedRotAngleDegOffset, or the radians version
		// (InheritedRotAngleOffset, once InheritedRotAngleRadOffset). Positive turns anticlockwise; mirrored when facing left.
		static rotationOffset(ent) {
			if (ent.has('InheritedRotAngleDegOffset')) {
				return (ent.num('InheritedRotAngleDegOffset', 0) * Math.PI) / 180;
			}
			return ent.has('InheritedRotAngleOffset') ? ent.num('InheritedRotAngleOffset', 0) : ent.num('InheritedRotAngleRadOffset', 0);
		}

		constructor(library, actorNode) {
			this.library = library;
			this.actorNode = actorNode;
			this.parts = [];
			this.device = null;
			this.build();
		}

		build() {
			const lib = this.library;
			const ent = lib.resolve(this.actorNode);
			this.ent = ent;
			this.cls = ent.cls;
			this.isDevice = DEVICE_CLASSES.has(ent.cls);
			this.parts = [];
			this.body = this.makePart('body', ent, [], null, this.actorNode);
			this.byRole = {};
			for (const part of this.parts) {
				if (!this.byRole[part.role]) {
					this.byRole[part.role] = part;
				}
			}
			this.paths = this.readPaths();
		}

		makePart(role, ent, path, parent, anchor) {
			const part = new Part(this, role, ent, path, parent, anchor);
			this.parts.push(part);
			const singles = Object.assign({}, SINGLE_PARTS[ent.cls] || {});
			// Each single key's last entry counts; list keys count every entry, in the order they come.
			const lastIndex = new Map();
			ent.entries.forEach((e, i) => {
				if (singles[e.key]) {
					lastIndex.set(e.key, i);
				}
			});
			const counts = {};
			ent.entries.forEach((e, i) => {
				const isList = LIST_PARTS.includes(e.key);
				if (!isList && !(singles[e.key] && lastIndex.get(e.key) === i)) {
					return;
				}
				const index = isList ? (counts[e.key] = (counts[e.key] || 0) + 1) - 1 : ent.all(e.key).length - 1;
				if (!isList && /^none$/i.test(e.node.value)) {
					return;
				}
				const childRole = isList ? 'attachable' : singles[e.key];
				part.children.push(this.makePart(childRole, this.library.resolve(e.node), path.concat([{ key: e.key, index }]), part, anchor));
			});
			return part;
		}

		// The held item to show in the front hand: an HDFirearm/HeldDevice preset block, or null.
		setDevice(deviceNode) {
			this.parts = this.parts.filter((p) => p.anchor === this.actorNode);
			this.device = null;
			if (deviceNode) {
				const ent = this.library.resolve(deviceNode);
				this.device = this.makePart('device', ent, [], null, deviceNode);
			}
		}

		readPaths() {
			const ent = this.ent;
			const read = (key) => {
				const sub = ent.sub(key);
				if (!sub) {
					return null;
				}
				const start = sub.vec('StartOffset');
				const segments = sub.all('AddSegment').map((e) => MS.readVector(e.node));
				return {
					key,
					node: ent.sourceOf(key),
					start,
					segments,
					startSegCount: Math.round(sub.num('StartSegCount', 0)),
					travelSpeed: sub.num('TravelSpeed', sub.num('NormalTravelSpeed', 1)),
				};
			};
			const paths = {};
			for (const key of ['StandLimbPath', 'StandLimbPathBG', 'WalkLimbPath', 'RunLimbPath', 'CrouchLimbPath', 'CrouchLimbPathBG', 'CrouchWalkLimbPath', 'CrawlLimbPath', 'ClimbLimbPath', 'JumpLimbPath']) {
				paths[key] = read(key);
			}
			return paths;
		}

		// ---- posing ----
		//
		// state: {flip, aim (radians, up is positive), pose ('stand' | 'walk' | 'run' | 'crouch' | 'jump'), time (seconds),
		//         sharp (sharp-aiming), frames: {partId: frame}}
		pose(state) {
			const out = new Map();
			const flip = !!state.flip;
			const flipF = flip ? -1 : 1;
			const adjAim = state.aim * flipF;
			const ent = this.ent;
			const targets = { stand: 'StandRotAngleTarget', walk: 'WalkRotAngleTarget', run: 'RunRotAngleTarget', crouch: 'CrouchRotAngleTarget', jump: 'JumpRotAngleTarget' };
			const defaultTarget = state.pose === 'crouch' ? Math.PI / 4 * 0.85 : 0;
			const target = ent.num(targets[state.pose] || 'StandRotAngleTarget', defaultTarget);
			const bodyRot = target * (state.aim > 0 ? 1 - state.aim / HALF_PI : 1) * flipF;

			const legInfo = this.legTargets(state, flip);
			const body = { pos: { x: 0, y: legInfo.bodyDrop }, rot: bodyRot, flip, frame: this.frameOf(this.body, state) };
			out.set(this.body, body);
			if (this.isDevice) {
				// A held item on its own: it turns with the aim about where it's held, and shows the points that matter for it.
				body.rot = adjAim;
				body.joint = add(body.pos, rotOff(body, ent.vec('JointOffset')));
				body.muzzle = ent.cls === 'HDFirearm' ? add(body.pos, rotOff(body, ent.vec('MuzzleOffset'))) : null;
				body.eject = ent.cls === 'HDFirearm' && ent.has('EjectionOffset') ? add(body.pos, rotOff(body, ent.vec('EjectionOffset'))) : null;
				body.support = ent.bool('Supportable', true) && ent.has('SupportOffset') ? add(body.pos, rotOff(body, ent.vec('SupportOffset'))) : null;
			}

			const placeChildren = (part, parentPose) => {
				for (const child of part.children) {
					if (child.role === 'fgarm' || child.role === 'bgarm' || child.role === 'fgleg' || child.role === 'bgleg') {
						continue; // posed below
					}
					const pose = this.placeAttachable(child, parentPose, state);
					if (child.role === 'head') {
						pose.rot = adjAim * ent.num('LookToAimRatio', 0.7) + bodyRot * (0.9 - ent.num('LookToAimRatio', 0.7));
						this.reposition(child, parentPose, pose);
					}
					out.set(child, pose);
					placeChildren(child, pose);
				}
			};
			placeChildren(this.body, body);

			// Legs.
			const legRots = {};
			for (const role of ['fgleg', 'bgleg']) {
				const leg = this.byRole[role];
				if (!leg) {
					continue;
				}
				const joint = add(body.pos, rotOff(body, leg.ent.vec('ParentOffset')));
				const contracted = leg.ent.vec('ContractedOffset');
				const extended = leg.ent.vec('ExtendedOffset');
				const minExt = len(contracted);
				const maxExt = Math.max(len(extended), minExt + 0.2);
				let ankle = sub(legInfo[role], joint);
				const rotated = rotate(ankle, body.rot);
				if (leg.ent.bool('WillIdle', true) && rotated.y < -Math.abs(rotated.x)) {
					ankle = rotOff(body, leg.ent.vec('IdleOffset'));
				}
				ankle = clampLen(ankle, minExt + 0.1, maxExt);
				const n = Math.min(Math.max((len(ankle) - minExt) / (maxExt - minExt), 0), 1);
				const ratio = (easeOut(minExt, maxExt, n) - minExt) / (maxExt - minExt);
				let extra = -absAngle(contracted);
				extra -= (absAngle(extended) - absAngle(contracted)) * ratio;
				const legPose = { flip, rot: absAngle(ankle) + (flip ? Math.PI : 0) + extra * flipF, joint };
				legPose.pos = sub(joint, rotOff(legPose, leg.ent.vec('JointOffset')));
				const frames = leg.sprite.frameCount;
				legPose.frame = frames !== 1 ? Math.min(frames - 1, Math.floor(n * frames)) : 0;
				legPose.frame = this.frameOf(leg, state, legPose.frame);
				legPose.ankle = add(joint, ankle);
				legPose.reach = { min: minExt, max: maxExt, now: len(ankle) };
				out.set(leg, legPose);
				legRots[role] = legPose.rot;
				for (const child of leg.children) {
					if (child.role === 'foot') {
						const foot = { flip, joint: add(joint, ankle) };
						const maxX = maxExt;
						if (ankle.y > Math.abs(ankle.x * 0.3)) {
							const hx = flipX(ankle, flip).x;
							foot.frame = hx < -maxX * 0.6 ? 3 : hx < -maxX * 0.4 ? 2 : hx > maxX * 0.4 ? 1 : 0;
							foot.rot = 0;
						} else {
							foot.frame = 0;
							foot.rot = legPose.rot + HALF_PI * flipF;
						}
						foot.frame = Math.min(foot.frame, child.sprite.frameCount - 1);
						foot.frame = this.frameOf(child, state, foot.frame);
						foot.pos = sub(foot.joint, rotOff(foot, child.ent.vec('JointOffset')));
						out.set(child, foot);
						placeChildren(child, foot);
					} else {
						const pose = this.placeAttachable(child, legPose, state);
						out.set(child, pose);
						placeChildren(child, pose);
					}
				}
			}

			// Arms and the held item.
			const device = this.device;
			let devicePose = null;
			for (const role of ['fgarm', 'bgarm']) {
				const arm = this.byRole[role];
				if (!arm) {
					continue;
				}
				const joint = add(body.pos, rotOff(body, arm.ent.vec('ParentOffset')));
				const maxLength = Math.max(arm.ent.num('MaxLength', 0), 1);
				let target;
				let supporting = false;
				if (role === 'fgarm' && device) {
					const stance = device.ent.vec(state.sharp && device.ent.has('SharpStanceOffset') ? 'SharpStanceOffset' : 'StanceOffset');
					target = rotate(flipX(stance, flip), adjAim);
				} else if (role === 'bgarm' && devicePose && device.ent.bool('Supportable', true)) {
					const supportPos = add(devicePose.pos, rotOff(devicePose, device.ent.vec('SupportOffset')));
					target = sub(supportPos, joint);
					supporting = len(target) <= maxLength * 2;
					if (!supporting) {
						target = null;
					}
				}
				if (!target) {
					target = rotate(flipX(arm.ent.vec('IdleOffset', arm.ent.vec('HandIdleOffset')), flip), bodyRot);
					// Walking swings the arms with the opposite leg.
					if (state.pose !== 'stand' && state.pose !== 'crouch') {
						const swingLeg = role === 'fgarm' ? legRots.bgleg ?? legRots.fgleg : legRots.fgleg ?? legRots.bgleg;
						if (swingLeg !== undefined) {
							const rate = role === 'fgarm' && device ? 0 : ent.num('ArmSwingRate', 1);
							target = rotate(target, Math.sin(swingLeg + HALF_PI * flipF) * rate);
						}
					}
				}
				const hand = clampLen(target, maxLength / 2, maxLength);
				const armPose = { flip, rot: absAngle(hand) + (flip ? Math.PI : 0), joint };
				armPose.pos = sub(joint, rotOff(armPose, arm.ent.vec('JointOffset')));
				const frames = arm.sprite.frameCount;
				const half = maxLength / 2;
				armPose.frame = Math.min(Math.max(Math.floor(((len(hand) - half) / half) * frames), 0), frames - 1);
				armPose.frame = this.frameOf(arm, state, armPose.frame);
				armPose.hand = add(joint, hand);
				armPose.handFile = arm.ent.file('Hand') || arm.ent.file('HandSprite');
				armPose.supporting = supporting;
				armPose.reach = { min: half, max: maxLength, now: len(hand) };
				out.set(arm, armPose);
				for (const child of arm.children) {
					const pose = this.placeAttachable(child, armPose, state);
					out.set(child, pose);
					placeChildren(child, pose);
				}
				if (role === 'fgarm' && device) {
					devicePose = { flip, rot: adjAim, joint: armPose.hand, frame: this.frameOf(device, state) };
					devicePose.pos = sub(armPose.hand, rotOff(devicePose, device.ent.vec('JointOffset')));
					devicePose.muzzle = add(devicePose.pos, rotOff(devicePose, device.ent.vec('MuzzleOffset')));
					devicePose.support = add(devicePose.pos, rotOff(devicePose, device.ent.vec('SupportOffset')));
					out.set(device, devicePose);
					placeChildren(device, devicePose);
				}
			}
			out.ground = legInfo.ground;
			out.travel = legInfo.travel;
			return out;
		}

		frameOf(part, state, computed) {
			const forced = state.frames && state.frames[part.id];
			if (forced !== undefined) {
				return Math.min(forced, part.sprite.frameCount - 1);
			}
			if (computed !== undefined) {
				return computed;
			}
			// Parts that animate on their own (SpriteAnimMode).
			const mode = Math.round(part.ent.num('SpriteAnimMode', 0));
			const count = part.sprite.frameCount;
			if (count > 1 && state.animateSprites && (mode === 1 || mode === 3 || mode === 4)) {
				const duration = part.ent.num('SpriteAnimDuration', 500) / 1000;
				const t = ((state.time || 0) % duration) / duration;
				if (mode === 3) {
					const f = Math.floor(t * (count * 2 - 2));
					return f < count ? f : count * 2 - 2 - f;
				}
				return Math.min(count - 1, Math.floor(t * count));
			}
			return 0;
		}

		placeAttachable(part, parentPose, state) {
			const ent = part.ent;
			const inheritsFlip = Math.round(ent.num('InheritsHFlipped', 1));
			const pose = {};
			pose.flip = inheritsFlip === 1 ? parentPose.flip : inheritsFlip === 0 ? ent.bool('HFlipped', false) : !parentPose.flip;
			if (ent.bool('InheritsRotAngle', true)) {
				const offset = MS.Rig.rotationOffset(ent);
				pose.rot = parentPose.rot + offset * (parentPose.flip ? -1 : 1);
			} else {
				pose.rot = 0;
			}
			pose.frame = this.frameOf(part, state);
			this.reposition(part, parentPose, pose);
			return pose;
		}

		reposition(part, parentPose, pose) {
			// The game puts a gun's muzzle flash at the gun's MuzzleOffset (HDFirearm sets the flash's ParentOffset from it), so that is where it shows.
			const onParent = part.role === 'flash' && part.parent ? part.parent.ent.vec('MuzzleOffset') : part.ent.vec('ParentOffset');
			pose.joint = add(parentPose.pos, rotOff(parentPose, onParent));
			pose.pos = sub(pose.joint, rotOff(pose, part.ent.vec('JointOffset')));
		}

		// Where each foot goes for the pose, and how far the body sits from its standing height.
		legTargets(state, flip) {
			const ent = this.ent;
			const result = { fgleg: { x: 0, y: 20 }, bgleg: { x: 0, y: 20 }, bodyDrop: 0, ground: 20, travel: 0 };
			const fg = this.byRole.fgleg;
			const bg = this.byRole.bgleg;
			const jointOf = (leg) => (leg ? flipX(leg.ent.vec('ParentOffset'), flip) : { x: 0, y: 0 });
			const footGroupOffset = (key) => {
				const group = ent.sub(key);
				return group ? flipX(group.vec('JointOffset'), flip) : { x: 0, y: 0 };
			};
			const stand = this.paths.StandLimbPath;
			const standBG = this.paths.StandLimbPathBG || stand;
			const standPoint = (path) => (path ? endOfStart(path) : { x: 0, y: 18 });
			// Ground: where a standing front foot rests, relative to the body.
			const standFG = add(add(jointOf(fg || bg), flipX(standPoint(stand), flip)), footGroupOffset('FGFootGroup'));
			result.ground = standFG.y;
			const place = (leg, point, groupKey) => add(add(jointOf(leg), flipX(point, flip)), footGroupOffset(groupKey));

			if (state.pose === 'walk' || state.pose === 'run') {
				const path = (state.pose === 'run' && this.paths.RunLimbPath) || this.paths.WalkLimbPath;
				if (path && path.segments.length) {
					const loop = pathLoop(path);
					const speed = path.travelSpeed * PPM;
					const t = (state.time || 0) * speed;
					for (const [role, leg, offset, group] of [['fgleg', fg, 0, 'FGFootGroup'], ['bgleg', bg, 0.5, 'BGFootGroup']]) {
						if (!leg) {
							continue;
						}
						const local = pointAlong(loop, (t + offset * loop.length) % loop.length);
						const groundLocal = result.ground - jointOf(leg).y - footGroupOffset(group).y;
						local.y = Math.min(local.y, groundLocal);
						result[role] = place(leg, local, group);
					}
					result.travel = speed * 0.85 * (state.time || 0);
					return result;
				}
			}
			let fgPoint = standPoint(stand);
			let bgPoint = standPoint(standBG);
			if (state.pose === 'crouch') {
				const crouch = this.paths.CrouchLimbPath;
				const crouchBG = this.paths.CrouchLimbPathBG || crouch;
				if (crouch) {
					fgPoint = endOfStart(crouch);
					bgPoint = endOfStart(crouchBG);
				}
			} else if (state.pose === 'jump' && this.paths.JumpLimbPath) {
				fgPoint = endOfStart(this.paths.JumpLimbPath);
				bgPoint = fgPoint;
			}
			result.fgleg = place(fg, fgPoint, 'FGFootGroup');
			result.bgleg = place(bg, bgPoint, 'BGFootGroup');
			if (state.pose === 'crouch') {
				// Feet stay on the ground; the body comes down.
				const drop = result.ground - result.fgleg.y;
				result.bodyDrop = drop;
				result.fgleg.y += drop;
				result.bgleg.y += drop;
			} else if (state.pose === 'jump') {
				result.ground += 14;
			}
			return result;
		}
	}

	// A static path's foot point, or where a moving path's start segments end.
	function endOfStart(path) {
		let p = { x: path.start.x, y: path.start.y };
		for (let i = 0; i < Math.min(path.startSegCount, path.segments.length); i++) {
			p = add(p, path.segments[i]);
		}
		return p;
	}

	// The foot's loop on a walking path: along every segment, then back through the air to the start.
	function pathLoop(path) {
		const points = [{ x: path.start.x, y: path.start.y }];
		for (const s of path.segments) {
			points.push(add(points[points.length - 1], s));
		}
		points.push({ x: path.start.x, y: path.start.y });
		const lengths = [];
		let total = 0;
		for (let i = 1; i < points.length; i++) {
			const l = len(sub(points[i], points[i - 1]));
			lengths.push(l);
			total += l;
		}
		return { points, lengths, length: Math.max(total, 1) };
	}

	function pointAlong(loop, distance) {
		let d = distance;
		for (let i = 0; i < loop.lengths.length; i++) {
			if (d <= loop.lengths[i] || i === loop.lengths.length - 1) {
				const t = loop.lengths[i] ? Math.min(d / loop.lengths[i], 1) : 0;
				const a = loop.points[i];
				const b = loop.points[i + 1];
				return { x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t };
			}
			d -= loop.lengths[i];
		}
		return { x: loop.points[0].x, y: loop.points[0].y };
	}

	// Back-to-front draw list for a pose: [{part, kind: 'sprite' | 'hand', pose}].
	function drawOrder(rig, poses) {
		const list = [];
		const drawPart = (part, skip) => {
			const pose = poses.get(part);
			if (!pose) {
				return;
			}
			for (const child of part.children) {
				if (!child.drawAfterParent && !(skip && skip.has(child))) {
					drawPart(child);
				}
			}
			list.push({ part, kind: 'sprite', pose });
			if (pose.handFile) {
				list.push({ part, kind: 'hand', pose });
			}
			for (const child of part.children) {
				if (child.drawAfterParent && !(skip && skip.has(child))) {
					drawPart(child);
				}
			}
		};
		const body = rig.body;
		const head = rig.byRole.head;
		const fgArm = rig.byRole.fgarm;
		const late = new Set([fgArm, head && head.drawAfterParent ? head : null].filter(Boolean));
		drawPart(body, late);
		if (head && late.has(head)) {
			drawPart(head);
		}
		if (fgArm) {
			const armPose = poses.get(fgArm);
			const device = rig.device;
			if (device && !device.drawAfterParent) {
				drawPart(device);
			}
			drawPart(fgArm);
			if (device && device.drawAfterParent) {
				drawPart(device);
			}
			const bgArm = rig.byRole.bgarm;
			const bgPose = bgArm && poses.get(bgArm);
			if (device && bgPose && bgPose.supporting && armPose) {
				list.push({ part: bgArm, kind: 'hand', pose: bgPose });
			}
		}
		return list;
	}

	Rig.POSED_ROLES = POSED_ROLES;
	Rig.ROLE_NAMES = ROLE_NAMES;
	Rig.DEVICE_CLASSES = DEVICE_CLASSES;
	MS.Rig = Rig;
	MS.drawOrder = drawOrder;
	MS.geom = { rotate, flipX, add, sub, scale, len, absAngle, rotOff };
})();
