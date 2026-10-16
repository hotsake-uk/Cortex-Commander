// Cortex Mod Studio: the words beside the Skeleton view (viewport.js drawSkeleton). How a unit is put together is hard to see from its
// INI: every part hangs on a parent at a joint, and three offsets place it. This shows what the marks mean, the selected part's own
// numbers in plain words, and (How a unit is put together…) the whole unit as a tree, each part with its offsets.
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
	const names = () => MS.Rig.ROLE_NAMES;
	const roleName = (part) => names()[part.role] || part.role;
	const vec = (part, key) => {
		const v = part.ent.vec(key, null);
		return v ? '(' + v.x + ', ' + v.y + ')' : 'not set, so (0, 0)';
	};

	const ROLE_NOTES = {
		body: 'The body is the root: every other part hangs on it, and its origin is where the unit is in the world.',
		head: 'The head turns a little with the aim, around its joint.',
		jetpack: 'The jetpack\'s flames come out of it pointing down; it hangs on the body like any attachment.',
		fgarm: 'An arm reaches from the shoulder (its joint) to the hand. IdleOffset is where the hand rests; the game bends the arm to reach, up to its reach (the rings with Reach on).',
		bgarm: 'An arm reaches from the shoulder (its joint) to the hand. IdleOffset is where the hand rests; the game bends the arm to reach.',
		fgleg: 'A leg reaches from the hip (its joint) to the ankle, where the foot is. The feet follow the unit\'s walk paths (LimbPath) and the leg stretches to them.',
		bgleg: 'A leg reaches from the hip (its joint) to the ankle, where the foot is. The feet follow the unit\'s walk paths (LimbPath).',
		foot: 'The foot is placed at the leg\'s ankle and kept flat on the ground.',
		device: 'A held item is held at its grip (its JointOffset) in the front hand and turns with the aim; shots leave from MuzzleOffset.',
		magazine: 'The magazine hangs on the gun at ParentOffset; it\'s what reloading swaps.',
		flash: 'The muzzle flash is shown at the muzzle for a moment when it fires.',
		turret: 'A turret turns with the aim, around its joint, and carries mounted guns.',
		mounted: 'A mounted gun sits on a turret and fires where the turret points.',
		attachable: 'An attachment hangs on its parent at ParentOffset and goes wherever its parent goes.',
	};

	function legend() {
		const item = (mark, text) => el('div', { className: 'sk-legend-row' }, el('span', { className: 'sk-mark', textContent: mark }), el('span', { textContent: text }));
		return el('div', { className: 'sk-legend' },
			item('■', 'origin: a part\'s own centre. Its picture is drawn from here (SpriteOffset).'),
			item('●', 'joint: where it joins its parent, and what it turns around.'),
			item('━', 'from the parent\'s origin to the joint: the parent-side spot (ParentOffset).'),
			item('┄', 'from the joint back to the part\'s origin: the part-side spot (JointOffset).'),
			item('○', 'hand and ankle: where arms reach and feet land.'));
	}

	function partWords(part) {
		const parent = part.parent;
		const lines = [el('b', {}, roleName(part), part.ent.presetName ? el('small', { textContent: ' ' + part.ent.presetName }) : null)];
		if (parent) {
			lines.push(el('p', { textContent: 'Hangs on the ' + roleName(parent).toLowerCase() + '.' }));
			lines.push(el('div', { className: 'sk-num' }, el('code', { textContent: 'ParentOffset ' + vec(part, 'ParentOffset') }), el('span', { textContent: 'where on the ' + roleName(parent).toLowerCase() + ' it joins, from the ' + roleName(parent).toLowerCase() + '\'s origin.' })));
			lines.push(el('div', { className: 'sk-num' }, el('code', { textContent: 'JointOffset ' + vec(part, 'JointOffset') }), el('span', { textContent: 'the same spot, from its own origin: it turns around this.' })));
		}
		lines.push(el('div', { className: 'sk-num' }, el('code', { textContent: 'SpriteOffset ' + vec(part, 'SpriteOffset') }), el('span', { textContent: 'where its picture\'s top-left corner is, from its origin.' })));
		const note = ROLE_NOTES[part.role];
		if (note) {
			lines.push(el('p', { className: 'sk-note', textContent: note }));
		}
		lines.push(el('p', { className: 'sk-tip', textContent: 'Drag the joint (●) to move where it joins; the numbers are in Properties › Placement.' }));
		return lines;
	}

	// Open shows the marks and the selected part's numbers; closed is one line, so the bones stay in view on a narrow screen.
	let open = app() && app().readLocal ? app().readLocal('ms.skeletonHelpOpen') !== false : true;

	MS.skeletonHelp = function (box, part) {
		if (app().readLocal('ms.skeletonHelpOpen') === false) {
			open = false;
		}
		const toggle = el('button', { className: 'ico-btn', textContent: open ? '▾' : '▸', title: open ? 'Fold this away' : 'What the marks mean, and the selected part\'s numbers', onclick: () => {
			open = !open;
			app().writeLocal('ms.skeletonHelpOpen', open);
			MS.skeletonHelp(box, part);
		} });
		box.classList.toggle('closed', !open);
		box.replaceChildren(
			el('div', { className: 'sk-head' }, toggle, el('b', { textContent: 'Skeleton' }), part ? el('span', { className: 'sk-sel', textContent: '· ' + roleName(part) }) : null, el('span', { className: 'spacer' }), el('button', { className: 'ico-btn', textContent: 'How a unit is put together…', onclick: explain })),
			open ? legend() : null,
			open ? (part ? el('div', { className: 'sk-part' }, ...partWords(part)) : el('p', { className: 'sk-tip', textContent: 'Click a part, here or in Parts, to see how it joins its parent.' })) : null);
	};

	// The whole unit as a tree, each part with its offsets; clicking one selects it.
	function explain() {
		const a = app();
		if (!a.rig) {
			return;
		}
		const rows = [];
		const add = (part, depth) => {
			const row = el('button', { className: 'sk-tree-row', style: 'padding-left:' + (8 + depth * 18) + 'px', onclick: () => {
				a.select(part);
				MS.dialogs.close();
			} },
			el('b', { textContent: roleName(part) }),
			el('span', { className: 'sk-preset', textContent: part.ent.presetName || '' }),
			part.parent ? el('code', { textContent: 'joins at ' + vec(part, 'ParentOffset') + ', turns at ' + vec(part, 'JointOffset') }) : el('code', { textContent: 'the root' }));
			rows.push(row);
			for (const child of part.children) {
				add(child, depth + 1);
			}
		};
		add(a.rig.body, 0);
		if (a.rig.device) {
			add(a.rig.device, 0);
		}
		const p = (text) => el('p', { textContent: text });
		MS.dialogs.open('How a unit is put together', el('div', { className: 'sk-explain' },
			p('A unit is a tree of parts. The body is the root; a head, arms, legs, a jetpack and other attachments each hang on a parent, and they can have parts of their own (a foot on a leg, a magazine on a gun). Each part is its own preset, often shared between units: the Parts list and Properties show which.'),
			p('Three offsets place each part, all in pixels with x to the right and y down:'),
			el('ul', {},
				el('li', {}, el('b', { textContent: 'ParentOffset' }), ': where on the parent it joins, measured from the parent\'s origin.'),
				el('li', {}, el('b', { textContent: 'JointOffset' }), ': the same spot measured from the part\'s own origin. The part turns around it.'),
				el('li', {}, el('b', { textContent: 'SpriteOffset' }), ': where the picture\'s top-left corner is, from the part\'s origin (usually minus half its size, to centre it).')),
			p('Arms and legs are posed by the game: an arm bends to put its hand where it should be (IdleOffset, or on the gun), and the feet walk along the unit\'s limb paths while the legs stretch to reach them. Held items hang in the front hand by their grip.'),
			p('This unit, part by part (click one to select it):'),
			el('div', { className: 'sk-tree' }, ...rows)), [{ text: 'Close' }], true);
	}
})();
