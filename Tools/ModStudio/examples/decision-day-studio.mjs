// Builds "Decision Day (Studio)": Decision Day remade with Mission Editor nodes only, on the game's own Decision Day map. Writes the scenario's INI
// and script (the graph lives in the script's Studio flow block, so the Mission Editor opens it to keep editing).
import fs from 'node:fs';
import vm from 'node:vm';

globalThis.window = globalThis;
window.MS = {};
const JS = new URL('../js/', import.meta.url);
for (const file of ['flowmodel.js', 'flowlua.js']) {
	vm.runInThisContext(fs.readFileSync(new URL(file, JS), 'utf8'), { filename: file });
}
const F = window.MS.flow;
// usage: node examples/decision-day-studio.mjs <your mod's Activities folder>  (then add IncludeFile = <mod>/Activities/DecisionDayStudio.ini to its Index.ini)
const out = process.argv[2] || '.';
const CLS = 'DecisionDayStudio';

const g = F.create();
const num = (n, x) => ({ k: 'num', n, x: !!x });
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
	for (let i = 0; i + 1 < steps.length; i++) {
		wire(steps[i], 'then', steps[i + 1]);
	}
	return steps[0];
};

// ---- the opening ----
const defenders = (area, group) => node('spawn', { source: 'group', group: group || 'Actors - Heavy', team: 1, area, perBox: true, arrive: 'appear', facing: 'auto', ai: 'sentry', tag: 'Defenders' });
const start = node('start', {});
wire(start, 'then', chain(
	node('fog', { do: 'hideAll', team: 0 }),
	node('fog', { do: 'reveal', area: 'Initial Human FOW Area', team: 0 }),
	node('fog', { do: 'reveal', area: 'Initial Extra FOW Reveal', team: 0 }),
	node('lz', { team: 0, area: 'Initial Human Spawn' }),
	node('buyMenu', { on: true }),
	defenders('Front Bunker Left Defenders', 'Actors - Light'),
	defenders('Front Bunker Right Defenders', 'Actors - Light'),
	defenders('Front Bunker Operations Defenders'),
	defenders('Front Bunker Small Vault Defenders', 'Actors - Light'),
	defenders('Middle Bunker Left Defenders', 'Actors - Light'),
	defenders('Middle Bunker Right Defenders', 'Actors - Light'),
	defenders('Middle Bunker Operations Defenders'),
	defenders('Main Bunker Left Defenders'),
	defenders('Main Bunker Right Defenders'),
	defenders('Main Bunker Door Controls Defenders'),
	defenders('Main Bunker Shield Generator Defenders'),
	node('spawn', { source: 'group', group: 'Brains', team: 1, area: 'Main Bunker Command Center Brain', perBox: true, arrive: 'appear', facing: 'auto', ai: 'stay', tag: 'Enemy Brain' }),
	Object.assign(node('dialogue', { who: 'all' }), {}),
));
const briefing = g.nodes[g.nodes.length - 1];
briefing.lists = { lines: [
	{ text: 'Commander, the Defenders have dug in across three bunkers.', seconds: 6 },
	{ text: 'Take the front bunker\'s operations room, and your craft can land closer in.', seconds: 7 },
	{ text: 'Their brain is deep in the main bunker. Get to it.', seconds: 6 },
] };

// ---- capture points ----
const capture = (name, base, active, text) => node('capture', { name, area: base + ' Capture', region: base, owner: 1, attackers: 0, seconds: num(12, true), decays: true, active, marker: true, text: text || 'Capture' });
const front = capture('Front bunker', 'Front Bunker Operations', true);
const vault = capture('Front vault', 'Front Bunker Small Vault', true, 'Vault');
const middle = capture('Middle bunker', 'Middle Bunker Operations', false);
const doorsPoint = capture('Door controls', 'Main Bunker Door Controls', false);
const shield = capture('Shield generator', 'Main Bunker Shield Generator', false);

wire(front, 'taken', chain(
	node('lz', { team: 0, area: 'Front Bunker LZ' }),
	node('fog', { do: 'reveal', area: 'Front Bunker', team: 0 }),
	node('doors', { area: 'Front Bunker', do: 'open', team: 0 }),
	node('message', { text: 'Front bunker taken: your craft can land there now.', who: 'all', seconds: 5 }),
));
wire(front, 'retaken', chain(
	node('lz', { team: 0, area: 'Initial Human Spawn' }),
	node('message', { text: 'The Defenders took the front bunker back!', who: 'all', seconds: 5 }),
));
wire(vault, 'taken', chain(
	node('setVar', { name: 'vaultIncome', op: 'set', value: num(4) }),
	node('setVar', { name: 'vaultIncome', op: 'mul', value: { k: 'setting', name: 'VaultIncome', n: 1 } }),
	node('income', { team: 0, amount: { k: 'var', name: 'vaultIncome' } }),
	node('message', { text: 'The vault is yours: {vaultIncome} gold a second.', who: 'all', seconds: 5 }),
));
wire(vault, 'retaken', chain(
	node('income', { team: 0, amount: num(0) }),
	node('message', { text: 'You lost the vault.', who: 'all', seconds: 4 }),
));
wire(middle, 'taken', chain(
	node('lz', { team: 0, area: 'Middle Bunker LZ' }),
	node('fog', { do: 'reveal', area: 'Middle Bunker', team: 0 }),
	node('doors', { area: 'Middle Bunker', do: 'open', team: 0 }),
	node('message', { text: 'Middle bunker taken.', who: 'all', seconds: 4 }),
));
wire(doorsPoint, 'taken', chain(
	node('doors', { area: 'Main Bunker Front Doors', do: 'open', team: 0 }),
	node('fog', { do: 'reveal', area: 'Main Bunker', team: 0 }),
	node('message', { text: 'The main bunker\'s doors are open!', who: 'all', seconds: 5 }),
));
wire(shield, 'taken', chain(
	node('doors', { area: 'Main Bunker Brain Doors', do: 'open', team: 0 }),
	node('doors', { area: 'Main Bunker Command Center Brain Door', do: 'open', team: 0 }),
	node('message', { text: 'The shield is down. The brain is exposed.', who: 'all', seconds: 5 }),
));

// ---- stages ----
const stage = (kind, p) => node(kind, Object.assign({ marker: true, viewer: 0, failAfter: 0 }, p));
const s1 = stage('stageCapture', { name: 'Take the front bunker', text: 'Capture the front bunker', target: front.id, team: 0 });
const s2 = stage('stageCapture', { name: 'Take the middle bunker', text: 'Capture the middle bunker', target: middle.id, team: 0 });
const s3 = stage('stageReach', { name: 'Find the hidden tunnel', text: 'Find the tunnel', area: 'Hidden Tunnel', team: 0 });
const s4 = stage('stageCapture', { name: 'Open the main doors', text: 'Take the door controls', target: doorsPoint.id, team: 0 });
const s5 = stage('stageCapture', { name: 'Bring down the shield', text: 'Take the shield generator', target: shield.id, team: 0 });
const s6 = stage('stageBrain', { name: 'Destroy the enemy brain', text: 'Destroy their brain', team: 1 });
wire(s1, 'next', s2);
wire(s2, 'next', s3);
wire(s3, 'next', s4);
wire(s4, 'next', s5);
wire(s5, 'next', s6);
wire(s2, 'started', chain(node('captureSet', { target: middle.id, do: 'on', team: 0 }), node('message', { text: 'Next: the middle bunker.', who: 'all', seconds: 4 })));
wire(s3, 'started', node('message', { text: 'Scouts report a hidden tunnel into the main bunker. Find it.', who: 'all', seconds: 6 }));
wire(s3, 'done', node('message', { text: 'You\'re in the tunnel. Take the door controls.', who: 'all', seconds: 5 }));
wire(s4, 'started', node('captureSet', { target: doorsPoint.id, do: 'on', team: 0 }));
wire(s5, 'started', node('captureSet', { target: shield.id, do: 'on', team: 0 }));
wire(s6, 'done', node('win', { team: 0, after: 6, text: 'Decision Day: the fortress has fallen.' }));

// ---- enemy waves ----
const wave = node('timer', { seconds: 60, repeat: true });
const notTooMany = node('branch', {});
wire(wave, 'then', notTooMany);
notTooMany.lists.conds.push(Object.assign(F.condDefaults('unitsWho'), { team: 1, which: 'any', op: '<', count: 40 }));
wire(notTooMany, 'true', chain(
	node('setVar', { name: 'wave', op: 'set', value: num(3) }),
	node('setVar', { name: 'wave', op: 'mul', value: { k: 'setting', name: 'AttackersPerSpawn', n: 1 } }),
	node('setVar', { name: 'wave', op: 'mul', value: { k: 'difficulty' } }),
	node('spawn', { source: 'group', group: 'Actors - Light', team: 1, area: 'Main Bunker LZ', count: { k: 'var', name: 'wave' }, arrive: 'dropship', facing: 'auto', ai: 'brainhunt', tag: 'Attackers' }),
));
// when one of their drops lands, say so (once a wave)
const arrived = node('unitArrives', { team: 1, which: 'craft' });
wire(arrived, 'then', chain(node('doOnce', {}), node('message', { text: 'Enemy dropship inbound!', who: 'all', seconds: 3 })));

F.layout(g);
const problems = F.problems(g, {});
if (problems.some((p) => p.level === 'error')) {
	console.error(problems);
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
const script = F.apply(base, CLS, g);
const ini = [
	'AddActivity = GAScripted',
	'\tPresetName = Decision Day (Studio)',
	'\tSceneName = Decision Day',
	'\tDescription = Decision Day, remade with the Mission Editor: take the bunkers one by one, find the tunnel, bring down the shield and destroy the brain.',
	'\tScriptPath = 123.rte/Activities/DecisionDayStudio.lua',
	'\tLuaClassName = ' + CLS,
	'\tAddScenarioSetting = AttackersPerSpawn | Enemy attackers per drop | Multiplier | 0.5 | 3 | 0.25 | 1',
	'\tAddScenarioSetting = VaultIncome | Vault income | Multiplier | 0 | 3 | 0.25 | 1',
	'\tCPUTeam = 1',
	'\tTeam1Name = Attackers',
	'\tTeam2Name = Defenders',
	'\tMinTeamsRequired = 1',
	'\tDefaultGoldCakeDifficulty = 4000',
	'\tDefaultGoldEasyDifficulty = 3000',
	'\tDefaultGoldMediumDifficulty = 2500',
	'\tDefaultGoldHardDifficulty = 2000',
	'\tDefaultGoldNutsDifficulty = 1500',
	'\tDefaultGoldMaxDifficulty = 1000',
	'\tDefaultRequireClearPathToOrbit = 1',
	'\tDefaultFogOfWar = 1',
	'\tDefaultDeployUnits = 1',
	'',
].join('\n');
fs.writeFileSync(out + '/DecisionDayStudio.lua', script.replace(/\n/g, '\r\n'));
fs.writeFileSync(out + '/DecisionDayStudio.ini', ini.replace(/\n/g, '\r\n'));
console.log('nodes', g.nodes.length, 'problems', problems.length, problems.map((p) => p.message).slice(0, 5));
