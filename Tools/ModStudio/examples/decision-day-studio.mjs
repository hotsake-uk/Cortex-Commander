// Builds "Decision Day (Studio)": Decision Day remade with Mission Editor nodes only, on the game's own Decision Day map (or a copy of it). It
// follows Decision Day's own script closely: the same defenders in the same places at the same moments, the 13 capture regions with their
// screens, terminals and painted areas, reinforcements coming out of the bunkers' doors, enemy rocket waves, allied dropships fighting beside you,
// vault income, the camera line that moves as the attack moves in, the tunnel crate, the doors and the brain at the end.
// Writes the scenario's INI and script (the graph lives in the script's Studio flow block, so the Mission Editor opens it to keep editing).
//
// usage: node examples/decision-day-studio.mjs <your mod's Activities folder> [map]  (then add IncludeFile = <mod>/Activities/DecisionDayStudio.ini to its Index.ini)
import fs from 'node:fs';
import vm from 'node:vm';

globalThis.window = globalThis;
window.MS = {};
const JS = new URL('../js/', import.meta.url);
for (const file of ['flowmodel.js', 'flowlua.js']) {
	vm.runInThisContext(fs.readFileSync(new URL(file, JS), 'utf8'), { filename: file });
}
const F = window.MS.flow;
const out = process.argv[2] || '.';
// the map it plays on: Decision Day, or a copy of it (one that CopyOf's it keeps every area), e.g. "Decision Day (1234)"
const SCENE = process.argv[3] || 'Decision Day';
const CLS = 'DecisionDayStudio';
const MODULE = 'Missions.rte';

const g = F.create();
const num = (n, x) => ({ k: 'num', n, x: !!x }); // x: times the difficulty (0.5 at the easiest, 2 at the hardest), as Decision Day scales by it
const node = (kind, p) => F.addNode(g, kind, 0, 0, p);
const wire = (a, pin, b) => {
	const why = F.connect(g, a.id, pin, b.id);
	if (why) {
		throw new Error(a.kind + '.' + pin + ' -> ' + b.kind + ': ' + why);
	}
	return b;
};
// a chain of steps: each one's "then" leads to the next; returns the first
const chain = (...steps) => {
	steps = steps.flat().filter(Boolean);
	for (let i = 0; i + 1 < steps.length; i++) {
		wire(steps[i], 'then', steps[i + 1]);
	}
	return steps[0];
};
const cond = (branch, kind, p) => branch.lists.conds.push(Object.assign(F.condDefaults(kind), p));
const say = (text, seconds, blink) => node('message', { text, who: 'all', seconds: seconds || 8, blink: !!blink });

// one area, or a list of them for a step stacked over several
const stack = (areas) => (areas.length === 1 ? areas[0] : areas);

// ---- spawning, as Decision Day's ----
// one unit in each box of an area, standing guard
const guards = (area, arm, facing, group) => node('spawn', { source: 'group', group: group || (arm === 'heavy' || arm === 'cqb' ? 'Actors - Heavy' : 'Actors - Light'), team: 1, area, perBox: true, arrive: 'appear', facing: facing || 'middle', ai: 'sentry', tag: 'Defenders', arm });
const turrets = (area) => node('spawn', { source: 'preset', preset: 'ACrab|TradeStar Turret|Base.rte', team: 1, area, perBox: true, arrive: 'appear', facing: 'middle', ai: 'sentry', tag: 'Turrets' });
// a bunker's defenders: snipers on its left and right, close-quarters troops inside, turrets, and each capture region's guard
const bunkerDefenders = (bunker, regions, withTurrets) => [
	guards(bunker + ' Left Defenders', 'sniper', 'left', 'Actors - Light'),
	guards(bunker + ' Right Defenders', 'sniper', 'right', 'Actors - Light'),
	// close-quarters troops inside and in each capture region: one spawn over all those areas
	guards(stack([bunker + ' Internal Defenders', ...regions.map((r) => r + ' Defenders')]), 'cqb', 'middle'),
	withTurrets ? turrets(bunker + ' Internal Turrets') : null,
];

// ---- capture regions ----
const DISPLAY = { progressScreen: 'Login Screen|' + MODULE, idleScreen: 'Fauxdan Screen|' + MODULE, terminalOn: 'Decision Day Computer {side} On|' + MODULE, terminalOff: 'Decision Day Computer {side} Off|' + MODULE };
const region = (name, area, active, markerText, ownedText) => node('capture', Object.assign({
	name, area: area + ' Capture', region: area, owner: 1, attackers: 0, seconds: num(10, true), decays: true, active,
	marker: true, text: markerText, markerWhen: 'select', paint: true, ownedText: 'This region is ' + ownedText,
	progressDisplay: area + ' Capture Display', idleDisplay: area + ' Fauxdan Display',
}, DISPLAY));
const FB = 'Front Bunker';
const MB = 'Middle Bunker';
const XB = 'Main Bunker';
const R = {
	frontOps: region('Front bunker operations', FB + ' Operations', true, 'Capture to secure the bunker', 'keeping the bunker and LZ secured'),
	frontVault: region('Front small vault', FB + ' Small Vault', true, 'Capture to gain funds', 'providing you with funds'),
	middleOps: region('Middle bunker operations', MB + ' Operations', false, 'Capture to secure the bunker', 'keeping the bunker and LZ secured'),
	doors: region('Door controls', XB + ' Door Controls', false, 'Capture to breach the bunker', 'giving you control of the front doors'),
	smallVault: region('Main small vault', XB + ' Small Vault', false, 'Capture to gain funds', 'providing you with funds'),
	tower: region('Security tower', XB + ' Security Tower', false, 'Capture to access cameras and turrets', 'giving you access to cameras and turrets'),
	barracks: region('Barracks', XB + ' Barracks', false, 'Capture to slow enemy internal reinforcements', 'slowing enemy internal reinforcements'),
	armory: region('Armory', XB + ' Armory', false, 'Capture to limit enemy equipment', 'limiting enemy weapons'),
	atc: region('Air traffic control', XB + ' Air Traffic Control', false, 'Capture to control the LZ', 'giving you control of the LZ'),
	shield: region('Shield generator', XB + ' Shield Generator', false, 'Capture to reach the enemy brain', 'giving you access to the enemy brain'),
	command: region('Command center', XB + ' Command Center', false, 'Capture to destroy the enemy brain', 'giving you the command center'),
	mediumVault: region('Main medium vault', XB + ' Medium Vault', false, 'Capture to gain funds', 'providing you with funds'),
	largeVault: region('Main large vault', XB + ' Large Vault', false, 'Capture to gain funds', 'providing you with funds'),
};
const on = (...points) => node('captureSet', { target: points.map((p) => p.id), do: 'on', team: 0 });
const off = (...points) => node('captureSet', { target: points.map((p) => p.id), do: 'off', team: 0 });

// vaults: a bonus the first time you take one, and gold every second while you hold it (Decision Day: 1500 and 6 a second, times the vault's
// size and the Vault income setting)
const vault = (point, size) => {
	wire(point, 'taken', chain(
		node('setVar', { name: 'vaultRate', op: 'add', value: num(6 * size) }),
		node('setVar', { name: 'vaultGold', op: 'set', value: { k: 'var', name: 'vaultRate' } }),
		node('setVar', { name: 'vaultGold', op: 'mul', value: { k: 'setting', name: 'VaultIncome', n: 1 } }),
		node('income', { team: 0, amount: { k: 'var', name: 'vaultGold' } }),
		node('doOnce', {}),
		node('setVar', { name: 'vaultBonus', op: 'set', value: num(1500 * size) }),
		node('setVar', { name: 'vaultBonus', op: 'mul', value: { k: 'setting', name: 'VaultIncome', n: 1 } }),
		node('gold', { team: 0, amount: { k: 'var', name: 'vaultBonus' } }),
		say('The vault is yours: {vaultBonus} gold now, and {vaultGold} a second while you hold your vaults.', 6),
	));
	wire(point, 'retaken', chain(
		node('setVar', { name: 'vaultRate', op: 'sub', value: num(6 * size) }),
		node('setVar', { name: 'vaultGold', op: 'set', value: { k: 'var', name: 'vaultRate' } }),
		node('setVar', { name: 'vaultGold', op: 'mul', value: { k: 'setting', name: 'VaultIncome', n: 1 } }),
		node('income', { team: 0, amount: { k: 'var', name: 'vaultGold' } }),
		say('You lost a vault.', 4),
	));
};
vault(R.frontVault, 1);
vault(R.smallVault, 1);
vault(R.mediumVault, 1.5);
vault(R.largeVault, 2);

// the operations rooms and air traffic control move the landing zones in (yours, your allies', and where the enemy's rockets come down)
wire(R.frontOps, 'taken', chain(node('lz', { team: 0, area: FB + ' LZ' }), node('fog', { do: 'reveal', area: FB, team: 0 }), say('Front bunker secured: your craft can land there now.', 6)));
wire(R.atc, 'taken', chain(node('lz', { team: 0, area: XB + ' LZ' }), say('Air traffic control is yours: your craft land at the main bunker.', 6)));
// the door controls open the front doors and keep them open; taken back, they work for the enemy again
wire(R.doors, 'taken', chain(node('doors', { area: XB + ' Front Doors', do: 'open', team: 0 }), node('doors', { area: XB + ' Front Doors', do: 'lock', team: -1 }), say('The main bunker\'s front doors are open!', 6)));
wire(R.doors, 'retaken', chain(node('doors', { area: XB + ' Front Doors', do: 'unlock', team: 1 }), node('doors', { area: XB + ' Front Doors', do: 'close', team: -1 })));
// the security tower shows you the whole main bunker (the first time) and turns its turrets to your side
wire(R.tower, 'taken', chain(node('changeTeam', { team: 1, which: 'group', group: 'Turrets', area: XB, to: 0 }), node('doOnce', {}), node('fog', { do: 'reveal', area: XB, team: 0 }), say('The security tower is yours: the whole bunker is on your cameras.', 6)));
wire(R.tower, 'retaken', node('changeTeam', { team: 0, which: 'group', group: 'Turrets', area: XB, to: 1 }));
// the command center opens the brain's door
wire(R.command, 'taken', node('doors', { area: XB + ' Command Center Brain Door', do: 'open', team: 0 }));
wire(R.command, 'retaken', node('doors', { area: XB + ' Command Center Brain Door', do: 'close', team: 1 }));

// ---- the opening ----
const start = node('start', {});
wire(start, 'then', chain(
	node('fog', { do: 'hideAll', team: 0 }),
	node('fog', { do: 'surface', team: 0 }),
	node('fog', { do: 'reveal', area: 'Initial Extra FOW Reveal', team: 0 }),
	node('fog', { do: 'reveal', area: 'Initial Human FOW Area', team: 0 }),
	node('cameraLimit', { x: 6245 }),
	node('placeObject', { object: 'Metal Crate Small A FG|Base.rte', area: 'Hidden Tunnel Blocking Crate', at: 'corner' }),
	node('lz', { team: 0, area: 'Initial Human Spawn' }),
	node('buyMenu', { on: true }),
	node('playerStart', { team: 0, area: 'Initial Human Spawn', squad: num(6), group: 'Actors - Heavy', arm: 'mix' }),
	bunkerDefenders(FB, [FB + ' Operations', FB + ' Small Vault'], false),
	say('Finally, the time has come for revenge. The Defenders have dug in across three bunkers.', 8),
	say('Take the front bunker\'s operations room to land closer in. Choose a unit to see your objectives!', 10, true),
));

// ---- stages ----
const stage = (kind, p) => node(kind, Object.assign({ marker: false, viewer: 0, failAfter: 0 }, p));
const s1 = stage('stageCapture', { name: 'Take the front bunker', target: R.frontOps.id, team: 0 });
const s2 = stage('stageCapture', { name: 'Take the middle bunker', target: R.middleOps.id, team: 0 });
const s3 = stage('stageReach', { name: 'Find the hidden tunnel', text: 'Find and enter the abandoned tunnel', area: 'Hidden Tunnel', team: 0, marker: true });
const s4 = stage('stageCapture', { name: 'Open the main doors', target: R.doors.id, team: 0 });
const s5 = stage('stageCapture', { name: 'Bring down the shield', target: R.shield.id, team: 0 });
const s6 = stage('stageBrain', { name: 'Destroy the enemy brain', team: 1 });
wire(s1, 'next', s2);
wire(s2, 'next', s3);
wire(s3, 'next', s4);
wire(s4, 'next', s5);
wire(s5, 'next', s6);
// the middle bunker: its defenders, its region, and the camera's line moves in
wire(s2, 'started', chain(
	node('cameraLimit', { x: 4200 }),
	bunkerDefenders(MB, [MB + ' Operations'], false),
	on(R.middleOps),
	node('setVar', { name: 'allies', op: 'set', value: num(1) }),
	say('Your reinforcements are landing at the front bunker. Next: the middle bunker.', 8),
));
// once it falls: the main bunker's defenders and turrets, the tunnel opens, the camera is free
wire(R.middleOps, 'taken', chain(
	node('lz', { team: 0, area: MB + ' LZ' }),
	node('fog', { do: 'reveal', area: MB, team: 0 }),
	node('cameraLimit', { x: 0 }),
	node('placeObject', { object: 'Empty 1X|Base.rte', area: 'Hidden Tunnel Blocking Crate', at: 'corner' }),
	bunkerDefenders(XB, [XB + ' Door Controls', XB + ' Security Tower', XB + ' Barracks', XB + ' Armory', XB + ' Air Traffic Control', XB + ' Shield Generator', XB + ' Command Center', XB + ' Small Vault', XB + ' Medium Vault', XB + ' Large Vault'], true),
	say('Middle bunker secured. Scouts report an abandoned tunnel into the main bunker.', 8),
));
wire(s4, 'started', chain(on(R.doors, R.smallVault)));
// inside the main bunker: every region at once, the door controls closed off behind you
wire(s5, 'started', chain(
	off(R.doors),
	on(R.tower, R.barracks, R.armory, R.atc, R.shield, R.mediumVault, R.largeVault),
	say('You\'re in. Take the shield generator to reach their brain; the other rooms each help you.', 10),
));
// the brain: its case in the command center, the brain doors gone, the shield region done with
wire(s6, 'started', chain(
	off(R.shield),
	on(R.command),
	node('remove', { area: XB + ' Brain Doors', what: 'units', team: 1, which: 'any' }),
	node('spawn', { source: 'preset', preset: 'Actor|Brain Case|Base.rte', team: 1, area: XB + ' Command Center Brain', perBox: true, arrive: 'appear', facing: 'auto', ai: 'stay', tag: 'Enemy Brain' }),
	say('The shield is down! Take the command center and destroy their brain.', 8),
));
wire(s6, 'done', node('win', { team: 0, after: 10, text: 'Finally, we have revenge. Your outstanding contributions to the assault have earned you a performance bonus!' }));

// ---- reinforcements from inside the bunkers: through their doors ----
// While you are inside a bunker the enemy still holds, fresh close-quarters troops come out of its doors (more for the deeper bunkers, and with
// the difficulty), and go for you; the barracks slows the main bunker's.
const doorWave = (seconds, bunker, ops, doors, count) => {
	const every = node('timer', { seconds, repeat: true });
	const check = node('branch', {});
	wire(every, 'then', check);
	cond(check, 'unitsWho', { team: 0, which: 'any', area: bunker, op: '>=', count: 1 });
	if (ops) {
		cond(check, 'captureOwner', { target: ops.id, team: 1 });
	}
	cond(check, 'unitsWho', { team: 1, which: 'any', op: '<', count: 70 });
	wire(check, 'true', node('spawn', { source: 'group', group: 'Actors - Heavy', team: 1, area: stack(doors), count: num(count, true), arrive: 'door', facing: 'auto', ai: 'goto', goTo: bunker, tag: 'Door Troops', arm: 'cqb' }));
};
doorWave(100, FB, R.frontOps, [FB + ' Internal Reinforcements', FB + ' Small Vault Internal Reinforcements'], 2);
doorWave(100, MB, R.middleOps, [MB + ' Internal Reinforcements', MB + ' Operations Internal Reinforcements'], 4);
doorWave(100, XB, null, [XB + ' Internal Reinforcements', XB + ' Door Controls Internal Reinforcements', XB + ' Barracks Internal Reinforcements', XB + ' Armory Internal Reinforcements', XB + ' Small Vault Internal Reinforcements'], 3);

// ---- each region defends itself: when you start taking it, the enemy's units near it go to it, and troops come out of its doors ----
const defend = (point, area, doors) => {
	const attacked = node('captureAttacked', { target: point.id, cooldown: num(60) });
	wire(attacked, 'then', chain(
		node('order', { who: 'area', team: 1, area, ai: 'goto', goTo: area + ' Capture' }),
		doors.length ? node('spawn', { source: 'group', group: 'Actors - Light', team: 1, area: stack(doors), count: num(2, true), arrive: 'door', facing: 'auto', ai: 'goto', goTo: area + ' Capture', tag: 'Door Troops', arm: 'mix' }) : null,
	));
};
defend(R.frontOps, FB + ' Operations', []);
defend(R.frontVault, FB + ' Small Vault', [FB + ' Small Vault Internal Reinforcements']);
defend(R.middleOps, MB + ' Operations', [MB + ' Operations Internal Reinforcements']);
defend(R.doors, XB + ' Door Controls', [XB + ' Door Controls Internal Reinforcements']);
defend(R.barracks, XB + ' Barracks', [XB + ' Barracks Internal Reinforcements']);
defend(R.armory, XB + ' Armory', [XB + ' Armory Internal Reinforcements']);
defend(R.smallVault, XB + ' Small Vault', [XB + ' Small Vault Internal Reinforcements']);
defend(R.command, XB + ' Command Center', [XB + ' Command Center Internal Reinforcements']);
for (const [p, a] of [[R.tower, 'Security Tower'], [R.atc, 'Air Traffic Control'], [R.shield, 'Shield Generator'], [R.mediumVault, 'Medium Vault'], [R.largeVault, 'Large Vault']]) {
	defend(p, XB + ' ' + a, []);
}

// ---- the brain's guard, from the command center's doors, while you are in its room ----
const guard = node('timer', { seconds: 10, repeat: true });
const brainCheck = node('branch', {});
wire(guard, 'then', brainCheck);
cond(brainCheck, 'group', { group: 'Enemy Brain', op: '>=', count: 1 });
cond(brainCheck, 'unitsWho', { team: 0, which: 'any', area: XB + ' Command Center', op: '>=', count: 1 });
cond(brainCheck, 'var', { name: 'brainGuard', op: '<', value: num(20, true) });
wire(brainCheck, 'true', chain(
	node('spawn', { source: 'group', group: 'Actors - Heavy', team: 1, area: XB + ' Command Center Internal Reinforcements', perBox: true, arrive: 'door', facing: 'auto', ai: 'goto', goTo: XB + ' Command Center Capture', tag: 'Door Troops', arm: 'cqb' }),
	node('setVar', { name: 'brainGuard', op: 'add', value: num(2) }),
));

// ---- enemy rocket waves, once you hold the front bunker: to the landing zone nearest the front ----
const waves = node('timer', { seconds: 120, repeat: true });
const waveCheck = node('branch', {});
wire(waves, 'then', waveCheck);
cond(waveCheck, 'captureOwner', { target: R.frontOps.id, team: 0 });
cond(waveCheck, 'group', { group: 'Attackers', op: '<', count: 10 });
const waveSize = chain(
	node('setVar', { name: 'wave', op: 'set', value: num(4) }),
	node('setVar', { name: 'wave', op: 'mul', value: { k: 'setting', name: 'AttackersPerSpawn', n: 1 } }),
	node('setVar', { name: 'wave', op: 'mul', value: { k: 'difficulty' } }),
);
wire(waveCheck, 'true', waveSize);
const waveTo = (lz) => node('spawn', { source: 'group', group: 'Actors - Light', team: 1, area: lz, count: { k: 'var', name: 'wave' }, arrive: 'rocket', facing: 'auto', ai: 'brainhunt', tag: 'Attackers', arm: 'mix' });
const viaAtc = node('branch', {});
cond(viaAtc, 'captureOwner', { target: R.atc.id, team: 0 });
wire(g.nodes.find((n) => n.kind === 'setVar' && n.p.name === 'wave' && n.p.value.k === 'difficulty'), 'then', viaAtc);
wire(viaAtc, 'true', waveTo(XB + ' Rear LZ'));
const viaMiddle = node('branch', {});
cond(viaMiddle, 'captureOwner', { target: R.middleOps.id, team: 0 });
wire(viaAtc, 'false', viaMiddle);
wire(viaMiddle, 'true', waveTo(XB + ' LZ'));
wire(viaMiddle, 'false', waveTo(MB + ' LZ'));
// the enemy sends its attackers to win back what you hold, the most valuable first (every 15 seconds)
const RETAKE = [[R.atc, 'Air Traffic Control'], [R.barracks, 'Barracks'], [R.armory, 'Armory'], [R.tower, 'Security Tower'], [R.largeVault, 'Large Vault'], [R.mediumVault, 'Medium Vault'], [R.doors, 'Door Controls'], [R.smallVault, 'Small Vault']];
const retarget = node('timer', { seconds: 15, repeat: true });
let from = { n: retarget, pin: 'then' };
for (const [point, name] of RETAKE) {
	const b = node('branch', {});
	cond(b, 'captureOwner', { target: point.id, team: 0 });
	wire(from.n, from.pin, b);
	wire(b, 'true', node('order', { who: 'group', group: 'Attackers', ai: 'goto', goTo: XB + ' ' + name + ' Capture' }));
	from = { n: b, pin: 'false' };
}
wire(from.n, from.pin, node('order', { who: 'group', group: 'Attackers', ai: 'brainhunt' }));

// ---- your allies: dropships every 10 seconds once the front bunker is yours, to your landing zone, pushing on ----
const allies = node('timer', { seconds: 10, repeat: true });
const allyCheck = node('branch', {});
wire(allies, 'then', allyCheck);
cond(allyCheck, 'captureOwner', { target: R.frontOps.id, team: 0 });
cond(allyCheck, 'group', { group: 'Allies', op: '<', count: 10 });
const allyDrop = (lz, goTo) => node('spawn', { source: 'group', group: 'Actors - Light', team: 0, area: lz, count: num(3), arrive: 'dropship', facing: 'auto', ai: 'goto', goTo, tag: 'Allies', arm: 'mix' });
const allyAtc = node('branch', {});
cond(allyAtc, 'captureOwner', { target: R.atc.id, team: 0 });
wire(allyCheck, 'true', allyAtc);
wire(allyAtc, 'true', allyDrop(XB + ' LZ', XB + ' Shield Generator Capture'));
const allyDoors = node('branch', {});
cond(allyDoors, 'captureOwner', { target: R.doors.id, team: 0 });
wire(allyAtc, 'false', allyDoors);
wire(allyDoors, 'true', allyDrop(MB + ' LZ', XB + ' Door Controls Capture'));
const allyMiddle = node('branch', {});
cond(allyMiddle, 'captureOwner', { target: R.middleOps.id, team: 0 });
wire(allyDoors, 'false', allyMiddle);
wire(allyMiddle, 'true', allyDrop(MB + ' LZ', XB + ' Front Doors'));
wire(allyMiddle, 'false', allyDrop(FB + ' LZ', MB + ' LZ'));

F.layout(g, true);
const problems = F.problems(g, {});
if (problems.some((p) => p.level === 'error')) {
	console.error(problems.filter((p) => p.level === 'error'));
	process.exit(1);
}

const base = [
	'-- Decision Day, remade in Cortex Studio\'s Mission Editor: everything it does is in the graph below (open it with Missions, Decision Day (Studio)).',
	'-- Decision Day\'s own script is not used; the map, its areas and its settings are.',
	'',
	'function ' + CLS + ':StartActivity(isNewGame)',
	'end',
	'',
	'function ' + CLS + ':UpdateActivity()',
	'\tself:RunStudioFlow()',
	'end',
	'',
].join('\n');
const script = F.apply(base, CLS, g, '123.rte/Activities/DecisionDayStudio.lua');
const ini = [
	'AddActivity = GAScripted',
	'\tPresetName = Decision Day (Studio)',
	'\tSceneName = ' + SCENE,
	'\tDescription = Decision Day, remade with the Mission Editor: take the bunkers one by one, find the tunnel, bring down the shield and destroy the brain.',
	'\tScriptPath = 123.rte/Activities/DecisionDayStudio.lua',
	'\tLuaClassName = ' + CLS,
	'\tAddScenarioSetting = AttackersPerSpawn | Enemy attackers per drop | Multiplier | 0.5 | 3 | 0.25 | 1',
	'\tAddScenarioSetting = VaultIncome | Vault income | Multiplier | 0 | 3 | 0.25 | 1',
	'\tCPUTeam = 1',
	'\tTeam1Name = Attackers',
	'\tTeam2Name = Defenders',
	'\tMinTeamsRequired = 1',
	'\tDefaultGoldCakeDifficulty = 2000',
	'\tDefaultGoldEasyDifficulty = 1500',
	'\tDefaultGoldMediumDifficulty = 1000',
	'\tDefaultGoldHardDifficulty = 500',
	'\tDefaultGoldNutsDifficulty = 250',
	'\tDefaultGoldMaxDifficulty = 0',
	'\tDefaultRequireClearPathToOrbit = 1',
	'\tDefaultFogOfWar = 1',
	'\tDefaultDeployUnits = 1',
	// offered only on maps that have these areas (Decision Day and its copies), as the game's own scenarios are
	...['Initial Human Spawn', 'Front Bunker Operations Capture', 'Middle Bunker Operations Capture', 'Main Bunker Door Controls Capture', 'Main Bunker Shield Generator Capture', 'Main Bunker Command Center Brain'].map((a) => '\tAddRequiredArea = ' + a),
	'',
].join('\n');
fs.writeFileSync(out + '/DecisionDayStudio.lua', script.replace(/\n/g, '\r\n'));
fs.writeFileSync(out + '/DecisionDayStudio.ini', ini.replace(/\n/g, '\r\n'));
console.log('nodes', g.nodes.length, 'problems', problems.length, problems.map((p) => p.message).slice(0, 5));
