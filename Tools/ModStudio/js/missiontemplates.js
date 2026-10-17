// Cortex Mod Studio: ready-made missions. Each asks a few questions and builds stages (and sometimes rules) in the older rules-and-stages form, which
// flowmodel.js's migrate turns into graph nodes; the Mission Editor adds them to the mission's graph, where they can be changed like any others.
'use strict';
window.MS = window.MS || {};

(function () {
	// ---- ready-made missions ----
	// Each template asks a few questions and builds stages (and sometimes rules) that can then be changed like any others.

	const msg = (text, who) => ({ kind: 'message', text, who: who === undefined ? 'all' : who, seconds: 5 });
	const win = (team) => ({ kind: 'win', team });
	const stage = (name, goal, p, extra) => Object.assign({ name, goal, text: '', marker: true, viewer: Number(p.team) || 0, failAfter: 0, onStart: [], onDone: [], onFail: [] }, extra || {});
	const battleStage = (name, mode, p) => stage(name, { kind: 'battle', team: Number(p.team) }, p, {
		marker: false,
		onStart: [msg(name + '!'), { kind: 'battle', mode, size: Number(p.size) || 12, byShip: false, layout: true, seed: Number(p.seed) || 1 }],
		onDone: [msg('Victory!'), win(Number(p.team))],
		failAfter: Math.max(0, Number(p.minutes) || 0) * 60,
		onFail: [msg('Out of time.'), { kind: 'end' }],
	});
	const battleParams = [['team', 'team', 'you are', 0], ['size', 'number', 'units a side', 12], ['minutes', 'number', 'time limit in minutes (0: none)', 0], ['seed', 'number', 'layout seed (a different number lays the map out differently)', 1]];

	const TEMPLATES = [
		{ id: 'hold', name: 'Hold the hill', blurb: 'Reach an area and keep it, free of enemies, for a while.', params: [['area', 'area', 'the hill', ''], ['team', 'team', 'you are', 0], ['seconds', 'number', 'hold it for (seconds)', 120]],
			build: (p) => ({ rules: [], stages: [
				stage('Reach the hill', { kind: 'reach', area: p.area, team: Number(p.team) }, p, { text: 'Take the hill', onDone: [msg('Now hold it!')] }),
				stage('Hold the hill', { kind: 'hold', area: p.area, team: Number(p.team), seconds: Number(p.seconds) }, p, { text: 'Hold it!', onDone: [msg('Hill held!'), win(Number(p.team))] })] }) },
		{ id: 'brain', name: 'Destroy the enemy brain', blurb: 'Hunt down and destroy the other team\'s brain, before time runs out.', params: [['team', 'team', 'you are', 0], ['enemy', 'team', 'the enemy', 1], ['minutes', 'number', 'time limit in minutes (0: none)', 15]],
			build: (p) => ({ rules: [], stages: [
				stage('Destroy the brain', { kind: 'brain', team: Number(p.enemy) }, p, { text: 'Terminate!', failAfter: Math.max(0, Number(p.minutes) || 0) * 60, onStart: [msg('Find and destroy their brain.')], onDone: [msg('The brain is gone.'), win(Number(p.team))], onFail: [msg('Time is up.'), win(Number(p.enemy))] })] }) },
		{ id: 'retrieve', name: 'Retrieve the item', blurb: 'An item appears in one area. Take it, and bring it to another.', params: [['item', 'item', 'the item', ''], ['from', 'area', 'it starts in', ''], ['area', 'area', 'bring it to', ''], ['team', 'team', 'you are', 0], ['minutes', 'number', 'time limit in minutes (0: none)', 0]],
			build: (p) => ({ rules: [], stages: [
				stage('Retrieve the item', { kind: 'fetch', item: p.item, from: p.from, area: p.area }, p, { text: 'Bring it here', failAfter: Math.max(0, Number(p.minutes) || 0) * 60, onStart: [msg('Find it and bring it back.')], onDone: [msg('Delivered.'), win(Number(p.team))], onFail: [msg('Too late.'), { kind: 'end' }] })] }) },
		{ id: 'survive', name: 'Survive', blurb: 'Stay alive for a set time.', params: [['team', 'team', 'you are', 0], ['seconds', 'number', 'seconds', 300]],
			build: (p) => ({ rules: [], stages: [stage('Survive', { kind: 'survive', seconds: Number(p.seconds) }, p, { marker: false, onStart: [msg('Survive!')], onDone: [msg('You made it.'), win(Number(p.team))] })] }) },
		{ id: 'assault', name: 'Take the position, clear it, hold it', blurb: 'Reach one area, destroy the enemy team, then hold another area for a while.', params: [['area', 'area', 'first, reach', ''], ['hill', 'area', 'finally, hold', ''], ['team', 'team', 'you are', 0], ['enemy', 'team', 'the enemy', 1], ['seconds', 'number', 'hold it for (seconds)', 90]],
			build: (p) => ({ rules: [], stages: [
				stage('Reach the position', { kind: 'reach', area: p.area, team: Number(p.team) }, p, { text: 'Move up', onDone: [msg('Clear them out.')] }),
				stage('Clear the enemy', { kind: 'destroy', team: Number(p.enemy) }, p, { text: 'Eliminate', onDone: [msg('All clear. Now take and hold the final position.')] }),
				stage('Hold the position', { kind: 'hold', area: p.hill, team: Number(p.team), seconds: Number(p.seconds) }, p, { text: 'Hold!', onDone: [msg('Mission complete.'), win(Number(p.team))] })] }) },
		{ id: 'ctf', name: 'Capture the flag match', blurb: 'The game\'s capture the flag, with a victory at the end. Needs a game with battle modes.', params: battleParams, build: (p) => ({ rules: [], stages: [battleStage('Capture the flag', 1, p)] }) },
		{ id: 'koth', name: 'King of the hill match', blurb: 'The game\'s king of the hill. Needs a game with battle modes.', params: battleParams, build: (p) => ({ rules: [], stages: [battleStage('King of the hill', 2, p)] }) },
		{ id: 'assaultmode', name: 'Assault match', blurb: 'The game\'s assault: take each objective in turn against the clock. Needs a game with battle modes.', params: battleParams, build: (p) => ({ rules: [], stages: [battleStage('Assault', 3, p)] }) },
		{ id: 'campaign', name: 'Two battles in a row', blurb: 'Win capture the flag, then king of the hill, then the mission is won.', params: battleParams,
			build: (p) => ({ rules: [], stages: [
				Object.assign(battleStage('Capture the flag', 1, p), { onDone: [msg('Flag war won. Now hold the hill.')] }),
				battleStage('King of the hill', 2, p)] }) },
	];

	MS.missionTemplates = TEMPLATES;
})();
