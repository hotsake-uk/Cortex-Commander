// Cortex Mod Studio: the flow model. A mission's logic is a graph of nodes joined by execution wires, like a blueprint: an event node starts a chain
// (when a unit enters an area, when a stage begins...), and the nodes after it run in order, branching and gating as the wires say. This file is only the
// data: what kinds of node there are and what each asks for, the graph and the operations on it (add, connect, remove, move), checking it for mistakes,
// and bringing the older rules-and-stages data into it. It touches no page and no Lua, so it runs the same in a test as in Studio. flowlua.js turns a
// graph into the script; flowview.js draws and edits it.
//
// A graph:   { v: 3, seq: <last id number>, nodes: [ node ] }
// A node:    { id: "n7", kind: "enter", p: { area: "LZ", team: -1 }, x: 120, y: 40, out: { then: "n8" } }
//            p holds the node's settings (the kind says which), out says which node each exit wire goes to (a pin with no wire is missing or null).
// Pins:      an event node has one exit, then. An action or a do-once node has an entrance and one exit, then. A branch has an entrance and two exits,
//            true and false. A stage has the exits started, done and timeout (what it runs when it begins, when its goal is met and when time runs out) and
//            next, which points at the stage to go to when it is done. Execution wires never loop back (checked as they are made); stage wires may.
// Nothing here ever throws on bad data: a graph is always something that can be shown, with problems() saying what is wrong with it.
'use strict';
window.MS = window.MS || {};

(function () {
	// ---- the kinds of setting ----
	// A setting is [key, type, label, default]. Types: team, area, number, text, op, bool, player (all or 1-4), fn (a function name), preset (a unit),
	// item, mode (a battle mode), stage (another stage node's id, picked by name), capture (a capture point node's id), choice (one of the options
	// given as the setting's fifth element), value (a number worked out as the mission runs: { k: 'num', n } a number, { k: 'var', name } a
	// variable, { k: 'setting', name } a scenario setting, { k: 'difficulty' } the difficulty as a multiplier from 0.5 to 2, { k: 'time' } the
	// seconds since it began; each with x: true to multiply it by the difficulty too). A plain number is read as a value of that number.

	const BATTLE_MODES = [[1, 'Capture the flag'], [2, 'King of the hill'], [3, 'Assault'], [4, 'Last team standing'], [5, 'VIP hunt'], [6, 'One flag'], [7, 'Capture the ship'], [8, 'Wave defence'], [9, 'Escort'], [10, 'Extraction'], [11, 'The point']];
	const OPS = [['>=', 'is at least'], ['<=', 'is at most'], ['==', 'is exactly'], ['>', 'is more than'], ['<', 'is less than']];
	const V = (n) => ({ k: 'num', n });
	const VAR_OPS = [['set', 'set it to'], ['add', 'add'], ['sub', 'take away'], ['mul', 'multiply it by']];
	// Which units an event, condition or action is about: any, a brain, a craft, the units of a group, or units of one name. relevantWho hides the
	// settings the choice makes no use of.
	const WHO_UNITS = [
		['which', 'choice', 'which units', 'any', [['any', 'any unit'], ['brain', 'a brain'], ['craft', 'a craft (dropship or rocket)'], ['group', 'units in a group'], ['named', 'a unit called']]],
		['group', 'text', 'group', ''],
		['named', 'text', 'called', ''],
	];
	const relevantWho = (p, key) => (key === 'group' ? p.which === 'group' : key === 'named' ? p.which === 'named' : true);
	const SCRIPT_TESTS = [['true', 'is true'], ['false', 'is false'], ['op', 'is a number that']];
	const relevantScript = (p, key) => (key === 'op' || key === 'value' ? p.test === 'op' : true);
	// pie menu buttons: the icon (a pie slice of Base.rte's), where in the menu it goes, and which units get it
	const PIE_ICONS = ['Land', 'Launch', 'Cycle', 'Brain', 'Eye', 'Checkmark', 'White Flag', 'Map', 'Go To', 'Follow', 'Return', 'Circuit Board', 'Mine', 'Death', 'Stats'].map((n) => [n, n]);
	const PIE_DIRECTIONS = [['Up', 'at the top'], ['Down', 'at the bottom'], ['Left', 'on the left'], ['Right', 'on the right'], ['Any', 'wherever there is room']];
	const PIE_WHO = [['brain', 'a player\'s brain'], ['team', 'a team\'s units'], ['group', 'units in a group']];
	// colours for shading an area (indices into the game's palette)
	const SHADES = [['13', 'red'], ['147', 'green'], ['210', 'blue'], ['122', 'yellow'], ['48', 'orange'], ['254', 'white']];
	const AI_MODES = [['sentry', 'stand guard'], ['goto', 'go to an area'], ['brainhunt', 'hunt the enemy brain'], ['patrol', 'patrol'], ['stay', 'stay put'], ['return', 'go back'], ['golddig', 'dig for gold']];

	// ---- the registry ----

	const KINDS = {};
	const ORDER = []; // the order kinds are listed in, by category

	function kind(name, category, title, params, pins, extra) {
		// extra: lists (named lists of rows, e.g. a branch's conditions), relevant(p, key) (whether a setting matters for these settings: those that
		// do not are hidden and not checked), optional (settings that may be left empty).
		KINDS[name] = Object.assign({ name, category, title, params, entrance: pins.entrance !== false, exits: pins.exits }, extra || {});
		ORDER.push(name);
	}

	const EVENT = { entrance: false, exits: ['then'] };
	const STEP = { entrance: true, exits: ['then'] };

	// events: how a chain begins
	kind('start', 'event', 'The mission starts', [], EVENT);
	kind('timer', 'event', 'After some seconds', [['seconds', 'number', 'seconds', 30], ['repeat', 'bool', 'and then again every time', false], ['again', 'bool', 'run again every moment it still holds', false]], EVENT);
	kind('enter', 'event', 'A unit enters an area', [['area', 'area', 'area', ''], ['team', 'team', 'whose', -1], ...WHO_UNITS], EVENT, { relevant: relevantWho });
	kind('leave', 'event', 'A unit leaves an area', [['area', 'area', 'area', ''], ['team', 'team', 'whose', -1], ...WHO_UNITS], EVENT, { relevant: relevantWho });
	kind('empty', 'event', 'An area is emptied', [['area', 'area', 'area', ''], ['team', 'team', 'of', -1]], EVENT);
	kind('teamGone', 'event', 'A team has no units left', [['team', 'team', 'team', 0], ['again', 'bool', 'run again every moment it still holds', false]], EVENT);
	kind('brainGone', 'event', 'A team\'s brain is destroyed', [['team', 'team', 'team', 0], ['again', 'bool', 'run again every moment it still holds', false]], EVENT);
	kind('funds', 'event', 'A team\'s gold reaches', [['team', 'team', 'team', 0], ['op', 'op', '', '>='], ['amount', 'number', 'gold', 1000]], EVENT);
	kind('always', 'event', 'Every moment', [], EVENT);
	kind('battleStarted', 'event', 'A battle begins', [], EVENT);
	kind('battleScore', 'event', 'A team scores in the battle', [['team', 'team', 'team', -1]], EVENT);
	kind('battleObjective', 'event', 'An objective is taken in the battle', [['team', 'team', 'team', -1]], EVENT);
	kind('battleEnded', 'event', 'The battle ends', [['team', 'team', 'won by', -1]], EVENT);
	kind('varReaches', 'event', 'A variable reaches a value', [['name', 'text', 'variable', 'count'], ['op', 'op', '', '>='], ['value', 'value', 'value', V(10)]], EVENT);
	kind('captureChanged', 'event', 'A capture point changes hands', [['target', 'capture', 'capture point', ''], ['team', 'team', 'taken by', -1]], EVENT);
	kind('groupGone', 'event', 'A group has no units left', [['group', 'text', 'group', ''], ['again', 'bool', 'run again every moment it still holds', false]], EVENT);
	kind('unitDies', 'event', 'A unit dies', [['team', 'team', 'whose', -1], ...WHO_UNITS, ['area', 'area', 'in area (optional)', '']], EVENT, { relevant: relevantWho, optional: ['area'] });
	kind('unitArrives', 'event', 'A unit arrives', [['team', 'team', 'whose', -1], ...WHO_UNITS, ['area', 'area', 'in area (optional)', '']], EVENT, { relevant: relevantWho, optional: ['area'] });
	kind('hurt', 'event', 'A unit is badly hurt', [['team', 'team', 'whose', -1], ...WHO_UNITS, ['below', 'number', 'health below', 30]], EVENT, { relevant: relevantWho });
	kind('brainPlaced', 'event', 'A player\'s brain is placed', [['who', 'player', 'whose', 'all']], EVENT);
	kind('holdArea', 'event', 'A team holds an area', [['area', 'area', 'area', ''], ['team', 'team', 'team', 0], ['seconds', 'number', 'with nobody else in it for seconds', 10], ['again', 'bool', 'run again every moment it still holds', false]], EVENT);
	kind('controls', 'event', 'A player takes control of a unit', [['who', 'player', 'player', 'all'], ...WHO_UNITS], EVENT, { relevant: relevantWho });
	kind('selectView', 'event', 'A player opens unit select', [['who', 'player', 'player', 'all']], EVENT);
	kind('pickUp', 'event', 'A unit picks up an item', [['item', 'item', 'item', ''], ['team', 'team', 'whose', -1]], EVENT);
	kind('stageBegins', 'event', 'A stage begins', [['target', 'stage', 'stage', '']], EVENT);
	kind('flagChanges', 'event', 'A flag is set', [['name', 'text', 'flag', 'flag'], ['value', 'bool', 'set (off: cleared)', true]], EVENT);
	kind('pieUsed', 'event', 'A player uses a pie menu button', [['label', 'text', 'button', 'My button']], EVENT);
	kind('scriptValue', 'event', 'A value in the script changes', [['field', 'text', 'the script\'s value (self.…)', 'anyHumanHasDeployedABrain'], ['test', 'choice', 'when it', 'true', SCRIPT_TESTS], ['op', 'op', '', '>='], ['value', 'value', 'value', V(1)]], EVENT, { relevant: relevantScript });

	// stages: goals in a sequence. Every stage also has a name, an objective text and marker, whose screen shows it, and a time limit.
	const STAGE_COMMON = [['name', 'text', 'name', 'Stage'], ['text', 'text', 'objective text', ''], ['marker', 'bool', 'show a marker', true], ['viewer', 'team', 'marker seen by', 0], ['failAfter', 'number', 'time limit (seconds, 0: none)', 0]];
	const STAGE_PINS = { entrance: false, exits: ['started', 'done', 'timeout', 'next'] };
	const stage = (name, title, goalParams) => kind(name, 'stage', title, goalParams.concat(STAGE_COMMON), STAGE_PINS);
	stage('stageReach', 'Goal: a unit reaches an area', [['area', 'area', 'area', ''], ['team', 'team', 'whose', 0]]);
	stage('stageHold', 'Goal: hold an area for some seconds', [['area', 'area', 'area', ''], ['team', 'team', 'team', 0], ['seconds', 'number', 'seconds', 60]]);
	stage('stageDestroy', 'Goal: a team has no units left', [['team', 'team', 'team', 1]]);
	stage('stageBrain', 'Goal: a team\'s brain is destroyed', [['team', 'team', 'team', 1]]);
	stage('stageFetch', 'Goal: an item is brought to an area', [['item', 'item', 'item', ''], ['from', 'area', 'put it in', ''], ['area', 'area', 'bring it to', '']]);
	stage('stageSurvive', 'Goal: time passes', [['seconds', 'number', 'seconds', 120]]);
	stage('stageBattle', 'Goal: the battle is won', [['team', 'team', 'by', -1]]);
	stage('stageBattleScore', 'Goal: a team scores in the battle', [['team', 'team', 'team', 0], ['count', 'number', 'points', 1]]);
	stage('stageCapture', 'Goal: a team holds a capture point', [['target', 'capture', 'capture point', ''], ['team', 'team', 'held by', 0]]);

	// capture points: a place a team takes by standing in it while nobody of the side holding it is in the area around it, over some seconds;
	// it can be taken back. They run all the time (while switched on), whatever stage it is, and have two exits: when the attackers take it,
	// and when the side that held it first takes it back.
	kind('capture', 'point', 'Capture point', [
		['name', 'text', 'name', 'Point'],
		['area', 'area', 'stand in', ''],
		['region', 'area', 'defenders in this area stop it (optional)', ''],
		['owner', 'team', 'held at first by', 1],
		['attackers', 'team', 'taken by', 0],
		['seconds', 'value', 'seconds to take', V(10)],
		['decays', 'bool', 'progress drains while nobody is taking it', true],
		['active', 'bool', 'switched on from the start', true],
		['marker', 'bool', 'show an objective marker to the attackers', true],
		['text', 'text', 'marker text', 'Capture'],
	], { entrance: false, exits: ['taken', 'retaken'] }, { optional: ['region'] });

	// flow control
	kind('branch', 'flow', 'Branch: if these hold', [], { entrance: true, exits: ['true', 'false'] }, { lists: ['conds'] });
	kind('doOnce', 'flow', 'Do this only once', [], STEP);

	// actions
	kind('message', 'action', 'Show a message', [['text', 'text', 'text', 'Well done.'], ['who', 'player', 'to', 'all'], ['seconds', 'number', 'for seconds', 5]], STEP);
	kind('gold', 'action', 'Give gold', [['team', 'team', 'to team', 0], ['amount', 'value', 'gold (negative takes it)', V(500)]], STEP);
	kind('spawn', 'action', 'Spawn units', [
		['source', 'choice', 'what', 'preset', [['preset', 'this unit'], ['group', 'a random unit of a group'], ['loadout', 'a loadout']]],
		['preset', 'preset', 'unit', ''],
		['group', 'text', 'group (of the team\'s tech)', 'Actors - Light'],
		['loadout', 'text', 'loadout', 'Infantry Heavy'],
		['team', 'team', 'for team', 0],
		['area', 'area', 'in area', ''],
		['perBox', 'bool', 'one in each box of the area', false],
		['count', 'value', 'how many', V(1)],
		['arrive', 'choice', 'arriving', 'appear', [['appear', 'there and then'], ['dropship', 'by dropship'], ['rocket', 'by rocket']]],
		['facing', 'choice', 'facing', 'auto', [['auto', 'as they come'], ['left', 'left'], ['right', 'right']]],
		['ai', 'choice', 'then they', 'sentry', AI_MODES],
		['goTo', 'area', 'going to', ''],
		['tag', 'text', 'put in the group', ''],
	], STEP, {
		optional: ['goTo', 'tag'],
		relevant: (p, key) => (key === 'preset' ? (p.source || 'preset') === 'preset' : key === 'group' ? p.source === 'group' : key === 'loadout' ? p.source === 'loadout' : key === 'goTo' ? p.ai === 'goto' : key === 'count' ? !p.perBox : true),
	});
	kind('order', 'action', 'Give orders', [
		['who', 'choice', 'who', 'group', [['group', 'a group'], ['team', 'a whole team'], ['area', 'a team in an area']]],
		['group', 'text', 'group', ''],
		['team', 'team', 'team', 1],
		['area', 'area', 'in area', ''],
		['ai', 'choice', 'to', 'goto', AI_MODES],
		['goTo', 'area', 'go to', ''],
	], STEP, { relevant: (p, key) => (key === 'group' ? (p.who || 'group') === 'group' : key === 'team' ? p.who !== 'group' && p.who !== undefined : key === 'area' ? p.who === 'area' : key === 'goTo' ? (p.ai || 'goto') === 'goto' : true) });
	kind('setVar', 'action', 'Set a variable', [['name', 'text', 'variable', 'count'], ['op', 'choice', '', 'set', VAR_OPS], ['value', 'value', 'value', V(1)]], STEP);
	kind('captureSet', 'action', 'Switch a capture point', [['target', 'capture', 'capture point', ''], ['do', 'choice', '', 'on', [['on', 'switch it on'], ['off', 'switch it off'], ['own', 'give it to']]], ['team', 'team', 'team', 0]], STEP, { relevant: (p, key) => (key === 'team' ? p.do === 'own' : true) });
	kind('doors', 'action', 'Doors', [['area', 'area', 'the doors in', ''], ['do', 'choice', '', 'open', [['open', 'open them'], ['close', 'close them'], ['lock', 'lock them (they stop working)'], ['unlock', 'unlock them']]], ['team', 'team', 'and give them to (any team: leave them)', -1]], STEP);
	kind('fog', 'action', 'Fog of war', [['do', 'choice', '', 'reveal', [['reveal', 'reveal an area'], ['hide', 'hide an area'], ['hideAll', 'hide the whole map']]], ['area', 'area', 'area', ''], ['team', 'team', 'for team', 0]], STEP, { relevant: (p, key) => (key === 'area' ? p.do !== 'hideAll' : true) });
	kind('lz', 'action', 'Set a landing zone', [['team', 'team', 'team', 0], ['area', 'area', 'their craft land in', '']], STEP);
	kind('buyMenu', 'action', 'Switch the buy menu', [['on', 'bool', 'players can buy', true]], STEP);
	kind('setFunds', 'action', 'Set a team\'s gold', [['team', 'team', 'team', 0], ['amount', 'value', 'gold', V(0)]], STEP);
	kind('income', 'action', 'Set an income', [['team', 'team', 'team', 0], ['amount', 'value', 'gold a second (0 stops it)', V(5)]], STEP);
	kind('dialogue', 'action', 'Say lines, one after another', [['who', 'player', 'to', 'all']], STEP, { lists: ['lines'] });
	kind('music', 'action', 'Play music', [['song', 'text', 'song', 'Generic Battle Music']], STEP);
	kind('killAll', 'action', 'Destroy a team\'s units', [['team', 'team', 'team', 0]], STEP);
	kind('flag', 'action', 'Set a flag', [['name', 'text', 'name', 'flag'], ['value', 'bool', 'to set', true]], STEP);
	kind('jump', 'action', 'Jump to a stage', [['target', 'stage', 'stage', '']], STEP);
	kind('win', 'action', 'End it: a team wins', [['team', 'team', 'team', 0], ['after', 'number', 'after seconds', 0], ['text', 'text', 'saying', '']], STEP, { optional: ['text'] });
	kind('end', 'action', 'End it with no winner', [], STEP);
	kind('call', 'action', 'Run my own function', [['fn', 'fn', 'function', 'MyFunction']], STEP);
	kind('code', 'action', 'Run my own code', [['label', 'text', 'what it does', 'My code'], ['code', 'lua', 'code', '']], STEP);
	kind('placeItem', 'action', 'Place an item', [['item', 'item', 'item', ''], ['area', 'area', 'in area', ''], ['count', 'value', 'how many', V(1)]], STEP);
	kind('remove', 'action', 'Remove units or items', [['area', 'area', 'in area', ''], ['what', 'choice', 'remove', 'units', [['units', 'units'], ['items', 'items lying there'], ['both', 'units and items']]], ['team', 'team', 'whose units', -1], ...WHO_UNITS], STEP, {
		relevant: (p, key) => (['team', 'which', 'group', 'named'].includes(key) ? p.what !== 'items' && relevantWho(p, key) : true),
	});
	kind('changeTeam', 'action', 'Change units\' team', [['team', 'team', 'units of', 1], ...WHO_UNITS, ['area', 'area', 'in area (optional)', ''], ['to', 'team', 'join team', 0]], STEP, { relevant: relevantWho, optional: ['area'] });
	kind('control', 'action', 'Put a player in control of a unit', [['who', 'player', 'player', 0], ['team', 'team', 'a unit of', 0], ...WHO_UNITS, ['area', 'area', 'in area (optional)', '']], STEP, { relevant: relevantWho, optional: ['area'] });
	kind('camera', 'action', 'Move a player\'s camera', [['who', 'player', 'player', 'all'], ['area', 'area', 'to look at', ''], ['seconds', 'number', 'and stay for seconds (0: just move it)', 3], ['speed', 'number', 'speed (0.01 slow to 1 instant)', 0.1]], STEP);
	kind('tech', 'action', 'Set a team\'s tech', [['team', 'team', 'team', 1], ['tech', 'text', 'tech (a mod, e.g. Coalition.rte)', 'Coalition.rte']], STEP);
	kind('scriptSet', 'action', 'Set a value in the script', [['field', 'text', 'the script\'s value (self.…)', 'myValue'], ['to', 'choice', 'to', 'true', [['true', 'true'], ['false', 'false'], ['value', 'a number']]], ['value', 'value', 'number', V(0)]], STEP, { relevant: (p, key) => (key === 'value' ? p.to === 'value' : true) });
	kind('pieAdd', 'action', 'Add a pie menu button', [
		['label', 'text', 'button text', 'My button'],
		['icon', 'choice', 'icon', 'Land', PIE_ICONS],
		['dir', 'choice', 'in the menu', 'Up', PIE_DIRECTIONS],
		['to', 'choice', 'give it to', 'brain', PIE_WHO],
		['who', 'player', 'player', 'all'],
		['team', 'team', 'team', 0],
		['group', 'text', 'group', ''],
		['keep', 'bool', 'and to units that come later', true],
	], STEP, { relevant: (p, key) => (key === 'who' ? (p.to || 'brain') === 'brain' : key === 'team' ? p.to === 'team' : key === 'group' ? p.to === 'group' : true) });
	kind('pieRemove', 'action', 'Remove a pie menu button', [['label', 'text', 'button text', 'My button']], STEP);
	kind('overlay', 'action', 'Shade an area in unit select', [
		['do', 'choice', '', 'show', [['show', 'shade it'], ['hide', 'stop shading it']]],
		['area', 'area', 'area', ''],
		['team', 'team', 'seen by team', 0],
		['color', 'choice', 'colour', '147', SHADES],
		['text', 'text', 'text on it', ''],
	], STEP, { optional: ['text'], relevant: (p, key) => (key === 'color' || key === 'text' ? p.do !== 'hide' : true) });
	kind('battle', 'action', 'Start a battle', [['mode', 'mode', 'mode', 2], ['size', 'number', 'units a side', 12], ['byShip', 'bool', 'they arrive by ship', false], ['layout', 'bool', 'lay the map out for me', true], ['seed', 'number', 'layout seed', 1]], STEP);
	kind('battleStop', 'action', 'Stop the battle', [], STEP);

	// conditions: not nodes, but the lines inside a branch
	const CONDS = {
		funds: { title: 'a team\'s gold', params: [['team', 'team', 'team', 0], ['op', 'op', '', '>='], ['amount', 'number', 'gold', 1000]] },
		time: { title: 'the time since it started', params: [['op', 'op', '', '>='], ['seconds', 'number', 'seconds', 60]] },
		units: { title: 'units of a team', params: [['team', 'team', 'team', 0], ['op', 'op', '', '>='], ['count', 'number', 'count', 1]] },
		inArea: { title: 'units of a team in an area', params: [['team', 'team', 'team', 0], ['area', 'area', 'area', ''], ['op', 'op', '', '>='], ['count', 'number', 'count', 1]] },
		flag: { title: 'a flag', params: [['name', 'text', 'name', 'flag'], ['value', 'bool', 'is set', true]] },
		stage: { title: 'the stage it is on', params: [['target', 'stage', 'stage', ''], ['is', 'bool', 'is the current one', true]] },
		battleScore: { title: 'a team\'s score in the battle', params: [['team', 'team', 'team', 0], ['op', 'op', '', '>='], ['count', 'number', 'points', 1]] },
		var: { title: 'a variable', params: [['name', 'text', 'variable', 'count'], ['op', 'op', '', '>='], ['value', 'value', 'value', V(1)]] },
		captureOwner: { title: 'who holds a capture point', params: [['target', 'capture', 'capture point', ''], ['team', 'team', 'is held by', 0]] },
		group: { title: 'units in a group', params: [['group', 'text', 'group', ''], ['op', 'op', '', '>='], ['count', 'number', 'count', 1]] },
		unitsWho: { title: 'units of a kind', params: [['team', 'team', 'of team', -1], ...WHO_UNITS, ['area', 'area', 'in area (optional)', ''], ['op', 'op', '', '>='], ['count', 'number', 'count', 1]], relevant: relevantWho, optional: ['area'] },
		brain: { title: 'a player has a brain', params: [['who', 'player', 'player', 'all']] },
		difficulty: { title: 'the difficulty', params: [['op', 'op', '', '>='], ['value', 'choice', '', '1', [['0.5', 'easiest'], ['0.75', 'easy'], ['1', 'medium'], ['1.5', 'hard'], ['1.75', 'nuts'], ['2', 'maximum']]]] },
		holding: { title: 'a team carries an item', params: [['team', 'team', 'team', 0], ['item', 'item', 'item', '']] },
		controlIn: { title: 'a player\'s unit is in an area', params: [['who', 'player', 'player', 'all'], ['area', 'area', 'area', '']] },
		script: { title: 'a value in the script', params: [['field', 'text', 'self.…', 'anyHumanHasDeployedABrain'], ['test', 'choice', '', 'true', SCRIPT_TESTS], ['op', 'op', '', '>='], ['value', 'value', 'value', V(1)]], relevant: relevantScript },
	};

	// the rows of the lists some nodes have: a dialogue's lines
	const LIST_ROWS = {
		lines: [['text', 'text', '', ''], ['seconds', 'number', 'seconds', 5]],
	};

	const CATEGORIES = [['event', 'Events', 'How a chain begins'], ['stage', 'Stages', 'Goals, one after another'], ['point', 'Capture points', 'Places teams take and take back'], ['flow', 'Flow', 'Decide and gate'], ['action', 'Actions', 'What happens']];
	const relevant = (spec, p, key) => !spec.relevant || spec.relevant(p || {}, key);

	const isStage = (k) => !!KINDS[k] && KINDS[k].category === 'stage';
	const isCodeStage = (v) => typeof v === 'string' && v.startsWith('code:');
	const defaults = (params) => Object.assign({}, ...params.map(([key, , , def]) => ({ [key]: def })));
	const condDefaults = (name) => Object.assign({ kind: name }, defaults(CONDS[name].params));

	// ---- the graph ----

	function create() {
		return { v: 3, seq: 0, nodes: [] };
	}

	const find = (g, id) => g.nodes.find((n) => n.id === id) || null;

	// A new node of a kind, at a place. Unknown kinds are refused (null).
	function addNode(g, kindName, x, y, p) {
		const spec = KINDS[kindName];
		if (!spec) {
			return null;
		}
		g.seq = (Number(g.seq) || 0) + 1;
		const node = { id: 'n' + g.seq, kind: kindName, p: Object.assign(defaults(spec.params), p || {}), x: Number.isFinite(x) ? Math.round(x) : null, y: Number.isFinite(y) ? Math.round(y) : null, out: {} };
		if (spec.lists) {
			node.lists = {};
			for (const l of spec.lists) {
				node.lists[l] = l === 'lines' ? [{ text: 'A line to say.', seconds: 5 }] : [];
			}
		}
		g.nodes.push(node);
		return node;
	}

	// The nodes that wire into this one: [{from, pin}].
	function incoming(g, id) {
		const found = [];
		for (const n of g.nodes) {
			for (const [pin, to] of Object.entries(n.out || {})) {
				if (to === id) {
					found.push({ from: n.id, pin });
				}
			}
		}
		return found;
	}

	// Whether following execution wires (not stage next wires) from one node can reach another.
	function reaches(g, fromId, toId, seen) {
		seen = seen || new Set();
		if (fromId === toId) {
			return true;
		}
		if (seen.has(fromId)) {
			return false;
		}
		seen.add(fromId);
		const n = find(g, fromId);
		if (!n) {
			return false;
		}
		return Object.entries(n.out || {}).some(([pin, to]) => to && pin !== 'next' && reaches(g, to, toId, seen));
	}

	// Why a wire cannot be made, or '' when it can.
	function whyNot(g, fromId, pin, toId) {
		const from = find(g, fromId);
		const to = find(g, toId);
		if (!from || !to) {
			return 'That node is not there.';
		}
		const spec = KINDS[from.kind];
		if (!spec || !spec.exits.includes(pin)) {
			return 'That exit does not exist on this node.';
		}
		if (fromId === toId) {
			return 'A node cannot be wired to itself.';
		}
		if (pin === 'next') {
			return isStage(to.kind) ? '' : 'The next stage has to be a stage.';
		}
		const target = KINDS[to.kind];
		if (!target || !target.entrance) {
			return 'Nothing can be wired into ' + (target ? target.title.toLowerCase() : 'that') + ': it starts a chain.';
		}
		if (reaches(g, toId, fromId)) {
			return 'That would make a loop: a chain cannot run back into itself.';
		}
		return '';
	}

	function connect(g, fromId, pin, toId) {
		const why = whyNot(g, fromId, pin, toId);
		if (why) {
			return why;
		}
		find(g, fromId).out[pin] = toId;
		return '';
	}

	function disconnect(g, fromId, pin) {
		const from = find(g, fromId);
		if (from && from.out) {
			delete from.out[pin];
		}
	}

	// Takes a node out and every wire that went to it. With bridge, a step in the middle of a chain is taken out and its neighbours joined up.
	function removeNode(g, id, bridge) {
		const node = find(g, id);
		if (!node) {
			return;
		}
		const after = (node.out || {}).then || null;
		g.nodes = g.nodes.filter((n) => n.id !== id);
		for (const n of g.nodes) {
			for (const [pin, to] of Object.entries(n.out || {})) {
				if (to === id) {
					if (bridge && after && pin !== 'next' && !whyNot(g, n.id, pin, after)) {
						n.out[pin] = after;
					} else {
						delete n.out[pin];
					}
				}
			}
		}
	}

	const clone = (g) => JSON.parse(JSON.stringify(g));

	// A condition line for a branch.
	function addCondition(node, name) {
		if (!CONDS[name]) {
			return null;
		}
		const c = condDefaults(name);
		node.lists.conds.push(c);
		return c;
	}

	// ---- looking at a graph ----

	// The chain of nodes from a wire onward, in the order they run (a branch stops it: its two sides are separate chains).
	function chainFrom(g, id) {
		const chain = [];
		const seen = new Set();
		let at = id;
		while (at && !seen.has(at)) {
			const n = find(g, at);
			if (!n) {
				break;
			}
			seen.add(at);
			chain.push(n);
			at = KINDS[n.kind] && KINDS[n.kind].exits.length === 1 && KINDS[n.kind].exits[0] === 'then' ? (n.out || {}).then : null;
		}
		return chain;
	}

	const entries = (g) => g.nodes.filter((n) => KINDS[n.kind] && !KINDS[n.kind].entrance && KINDS[n.kind].category === 'event');
	const stages = (g) => g.nodes.filter((n) => isStage(n.kind));

	// The stage that starts the mission: the first stage (in the order they were made) that no other stage goes on to.
	function firstStage(g) {
		const targets = new Set();
		for (const s of stages(g)) {
			if (s.out && s.out.next) {
				targets.add(s.out.next);
			}
		}
		return stages(g).find((s) => !targets.has(s.id)) || null;
	}

	// ---- mistakes ----
	// ctx: { areas: [names the scene has, or null if unknown], hasScript: bool }. Each problem: { id, level: 'error' | 'warn', message }.

	function problems(g, ctx) {
		const found = [];
		const areas = ctx && Array.isArray(ctx.areas) ? ctx.areas : null;
		// a stage is one of the graph's, or one the script keeps in its own code ("code:attackBrain")
		const codeStages = ctx && Array.isArray(ctx.codeStages) ? ctx.codeStages : null;
		const stageOk = (v) => {
			if (isCodeStage(v)) {
				return !codeStages || codeStages.includes(v.slice(5));
			}
			return !!v && !!find(g, v) && isStage(find(g, v).kind);
		};
		const add = (id, level, message) => found.push({ id, level, message });
		for (const n of g.nodes) {
			const spec = KINDS[n.kind];
			if (!spec) {
				add(n.id, 'error', 'Studio does not know a "' + n.kind + '" node, so it is left out of the script.');
				continue;
			}
			for (const [key, type, label] of spec.params) {
				const v = (n.p || {})[key];
				if (!relevant(spec, n.p, key) || ((spec.optional || []).includes(key) && !v)) {
					continue;
				}
				if (type === 'capture' && (!v || !find(g, v) || find(g, v).kind !== 'capture')) {
					add(n.id, 'error', 'Pick the ' + label + '.');
				} else if (type === 'text' && (key === 'name' || key === 'group') && ['varReaches', 'setVar', 'groupGone', 'order', 'pieAdd'].includes(n.kind) && !String(v || '').trim()) {
					add(n.id, 'error', 'Give the ' + label + ' a name.');
				} else if (key === 'label' && ['pieAdd', 'pieRemove', 'pieUsed'].includes(n.kind) && !String(v || '').trim()) {
					add(n.id, 'error', 'Write the button\'s text: it is how the button is known.');
				} else if (type === 'area') {
					if (!v) {
						add(n.id, 'error', 'Pick the ' + label + '.');
					} else if (areas && !areas.includes(v)) {
						add(n.id, 'warn', 'The scene has no area called "' + v + '".');
					}
				} else if (type === 'item' || type === 'preset') {
					if (!v) {
						add(n.id, 'error', 'Pick the ' + label + '.');
					}
				} else if (key === 'field' && !/^[A-Za-z_][A-Za-z0-9_.]*$/.test(String(v || ''))) {
					add(n.id, 'error', 'Name the script\'s value: letters, digits and _ (self.' + (v || '…') + ').');
				} else if (type === 'lua' && !String(v || '').trim()) {
					add(n.id, 'warn', 'There is no code in it yet.');
				} else if (type === 'fn' && !/^[A-Za-z_][A-Za-z0-9_ ]*$/.test(String(v || ''))) {
					add(n.id, 'error', 'Give the function a name made of letters and numbers.');
				} else if (type === 'stage' && !stageOk(v)) {
					add(n.id, 'error', isCodeStage(v) ? 'The script has no stage called ' + v.slice(5) + '.' : 'Pick which stage to jump to.');
				}
			}
			if (n.kind === 'branch') {
				const conds = (n.lists && n.lists.conds) || [];
				if (!conds.length) {
					add(n.id, 'warn', 'A branch with no conditions always takes the true side.');
				}
				for (const c of conds) {
					const cspec = CONDS[c.kind];
					for (const [key, type, label] of (cspec ? cspec.params : [])) {
						const v = c[key];
						if ((cspec.relevant && !cspec.relevant(c, key)) || ((cspec.optional || []).includes(key) && !v)) {
							continue;
						}
						if (type === 'item' && !v) {
							add(n.id, 'error', 'A condition needs an item.');
						}
						if (key === 'field' && !/^[A-Za-z_][A-Za-z0-9_.]*$/.test(String(v || ''))) {
							add(n.id, 'error', 'Name the script\'s value: letters, digits and _ (self.' + (v || '…') + ').');
						}
						if (type === 'area' && (!v || (areas && !areas.includes(v)))) {
							add(n.id, v ? 'warn' : 'error', v ? 'The scene has no area called "' + v + '".' : 'A condition needs an area.');
						}
						if (type === 'stage' && !stageOk(v)) {
							add(n.id, 'error', isCodeStage(v) ? 'The script has no stage called ' + v.slice(5) + '.' : 'A condition needs a stage.');
						}
						if (type === 'capture' && (!v || !find(g, v))) {
							add(n.id, 'error', 'A condition needs a capture point.');
						}
					}
				}
			}
			for (const pin of spec.exits) {
				const to = (n.out || {})[pin];
				if (to && !find(g, to)) {
					add(n.id, 'error', 'The ' + pin + ' wire goes to a node that is not there.');
				}
			}
			if (spec.entrance && !incoming(g, n.id).length) {
				add(n.id, 'warn', 'Nothing leads to this, so it never runs.');
			}
			if (spec.category === 'event' && !(n.out || {}).then) {
				add(n.id, 'warn', 'This event does nothing: wire something after it.');
			}
		}
		const starts = stages(g).filter((s) => !incoming(g, s.id).some((i) => i.pin === 'next'));
		if (starts.length > 1) {
			for (const s of starts.slice(1)) {
				add(s.id, 'warn', 'Only the first stage runs from the start. Wire another stage\'s next exit to this one, or jump to it.');
			}
		}
		// A loop of execution wires should never exist (connect refuses them), but data may come from a hand-edited script.
		for (const n of g.nodes) {
			for (const [pin, to] of Object.entries(n.out || {})) {
				if (to && pin !== 'next' && find(g, to) && reaches(g, to, n.id)) {
					add(n.id, 'error', 'This is part of a loop of wires, which cannot run.');
					break;
				}
			}
		}
		return found;
	}

	// ---- laying out ----

	const COL = 330;
	const ROW = 190;

	// Places every node by following the wires: events on the left in rows, what they lead to going right, a branch's false side below its true side,
	// stages in a row along the top with what they run hanging below. Moves only nodes that have no place yet unless all is true.
	function layout(g, all) {
		const placed = new Set();
		const needs = (n) => all || !Number.isFinite(n.x) || !Number.isFinite(n.y);
		let top = 0;
		// stages: a column each, in order of the next wires
		const order = [];
		const seen = new Set();
		const first = firstStage(g);
		for (let s = first; s && !seen.has(s.id); s = find(g, (s.out || {}).next)) {
			seen.add(s.id);
			order.push(s);
		}
		for (const s of stages(g)) {
			if (!seen.has(s.id)) {
				order.push(s);
			}
		}
		let stageBottom = 0;
		order.forEach((s, i) => {
			const x = i * COL;
			if (needs(s)) {
				s.x = x;
				s.y = 0;
			}
			placed.add(s.id);
			let y = ROW * 1.25;
			for (const pin of ['started', 'done', 'timeout']) {
				const chain = chainFrom(g, (s.out || {})[pin]);
				for (const n of chain) {
					if (!placed.has(n.id) && needs(n)) {
						n.x = x;
						n.y = y;
					}
					placed.add(n.id);
					y += ROW * 0.8;
				}
			}
			stageBottom = Math.max(stageBottom, y);
		});
		top = order.length ? stageBottom + ROW * 0.4 : 0;
		// events: a row each
		let row = top;
		const place = (id, x, y) => {
			const n = find(g, id);
			if (!n || placed.has(n.id)) {
				return y;
			}
			placed.add(n.id);
			if (needs(n)) {
				n.x = x;
				n.y = y;
			}
			if (n.kind === 'branch') {
				let end = place((n.out || {}).true, x + COL, y);
				const falseTop = (n.out || {}).false ? Math.max(end, y + ROW * 0.9) : y;
				end = Math.max(end, place((n.out || {}).false, x + COL, falseTop));
				return Math.max(end, y + ROW * 0.9);
			}
			return Math.max(place((n.out || {}).then, x + COL, y), y + ROW * 0.9);
		};
		for (const e of entries(g)) {
			row = Math.max(row, place(e.id, 0, row)) + ROW * 0.15;
		}
		// whatever is left (nothing wires to it): in a row at the bottom
		let x = 0;
		for (const n of g.nodes) {
			if (!placed.has(n.id)) {
				if (needs(n)) {
					n.x = x;
					n.y = row;
				}
				x += COL;
			}
		}
		return g;
	}

	// ---- from the older rules-and-stages data ----
	// v1 was a list of rules; v2 was { rules, stages }. A rule was WHEN event, IF conditions, THEN actions, and once. A stage had a goal, and actions for
	// starting, finishing and running out of time. Both become a graph that does the same: a rule an event, a branch for its conditions, a do-once where
	// it fired once, then its actions; a stage a stage node whose next wire goes to the stage after.

	// Events that fire once by themselves (they latch, unless told to keep running): the start, a team or brain gone, a timer that does not repeat.
	const latched = (e) => e.kind === 'start' || e.kind === 'teamGone' || e.kind === 'brainGone' || (e.kind === 'timer' && !(e.p || {}).repeat);

	function migrate(data) {
		if (data && data.v === 3 && Array.isArray(data.nodes)) {
			return data;
		}
		const rules = Array.isArray(data) ? data : data && Array.isArray(data.rules) ? data.rules : [];
		const stageList = data && Array.isArray(data.stages) ? data.stages : [];
		const g = create();

		const chain = (items, fromNode, pin) => {
			let prev = fromNode;
			let prevPin = pin;
			for (const a of items || []) {
				const actionKind = a.kind === 'stage' ? 'jump' : a.kind;
				if (!KINDS[actionKind]) {
					continue;
				}
				const p = Object.assign({}, a);
				delete p.kind;
				if (a.kind === 'stage') {
					// an old jump named a stage by its number
					const target = stageNodes[Number(a.n) - 1];
					p.target = target ? target.id : '';
					delete p.n;
				}
				const node = addNode(g, actionKind, 0, 0, p);
				prev.out[prevPin] = node.id;
				prev = node;
				prevPin = 'then';
			}
			return prev;
		};

		const stageNodes = [];
		const goalKinds = { reach: 'stageReach', hold: 'stageHold', destroy: 'stageDestroy', brain: 'stageBrain', fetch: 'stageFetch', survive: 'stageSurvive', battle: 'stageBattle', battleScore: 'stageBattleScore' };
		for (const st of stageList) {
			const goal = st.goal || {};
			const p = Object.assign({}, goal, { name: st.name, text: st.text || '', marker: !!st.marker, viewer: Number(st.viewer) || 0, failAfter: Number(st.failAfter) || 0 });
			delete p.kind;
			stageNodes.push(addNode(g, goalKinds[goal.kind] || 'stageSurvive', 0, 0, p));
		}
		stageList.forEach((st, i) => {
			const node = stageNodes[i];
			chain(st.onStart, node, 'started');
			chain(st.onDone, node, 'done');
			chain(st.onFail, node, 'timeout');
			if (stageNodes[i + 1]) {
				node.out.next = stageNodes[i + 1].id;
			}
		});

		for (const r of rules) {
			const w = r.when || { kind: 'start' };
			if (!KINDS[w.kind]) {
				continue;
			}
			const p = Object.assign({}, w);
			delete p.kind;
			const event = addNode(g, w.kind, null, null, p);
			let tail = event;
			let pin = 'then';
			const conds = (r.ifs || []).map((c) => {
				const q = Object.assign({}, c);
				if (c.kind === 'stage') {
					// an old stage test compared the stage number: "is stage n" becomes "is that stage", the others have no direct form and stay as is
					const target = stageNodes[Number(c.n) - 1];
					return { kind: 'stage', target: target ? target.id : '', is: (c.op || '==') !== '!=' };
				}
				return q;
			});
			if (conds.length) {
				const branch = addNode(g, 'branch', 0, 0);
				branch.lists.conds = conds;
				event.out.then = branch.id;
				tail = branch;
				pin = 'true';
			}
			// An old rule whose event stayed true kept being checked every moment until its conditions held and it had run: that is what "run again" and a
			// do-once after the branch say. Without conditions the event itself fires only once.
			if (conds.length && latched(event) && w.kind !== 'start') {
				event.p.again = true;
			}
			const once = r.once || latched(event);
			if (once && !(latched(event) && !conds.length)) {
				const gate = addNode(g, 'doOnce', 0, 0);
				tail.out[pin] = gate.id;
				tail = gate;
				pin = 'then';
			}
			chain(r.then, tail, pin);
			event.note = r.name; // kept on the node so the rule's name is not lost
		}
		layout(g, true);
		return g;
	}

	// Adds another graph's nodes to this one (a ready-made mission), with new ids and the wires and stage references between them kept, below what is
	// there. Returns the old id -> new id map.
	function merge(g, other) {
		const map = {};
		const top = g.nodes.length ? g.nodes.reduce((m, n) => Math.max(m, (Number(n.y) || 0) + 260), 0) : 0;
		const minY = other.nodes.length ? Math.min(...other.nodes.map((n) => Number(n.y) || 0)) : 0;
		for (const n of other.nodes) {
			g.seq = (Number(g.seq) || 0) + 1;
			map[n.id] = 'n' + g.seq;
		}
		for (const n of other.nodes) {
			const copy = clone(n);
			copy.id = map[n.id];
			copy.x = Number(n.x) || 0;
			copy.y = (Number(n.y) || 0) - minY + top;
			copy.out = {};
			for (const [pin, to] of Object.entries(n.out || {})) {
				if (map[to]) {
					copy.out[pin] = map[to];
				}
			}
			if (copy.p && copy.p.target && map[copy.p.target]) {
				copy.p.target = map[copy.p.target];
			}
			for (const c of (copy.lists && copy.lists.conds) || []) {
				if (c.target && map[c.target]) {
					c.target = map[c.target];
				}
			}
			g.nodes.push(copy);
		}
		return map;
	}

	MS.flow = Object.assign(MS.flow || {}, {
		KINDS, ORDER, CONDS, CATEGORIES, BATTLE_MODES, PIE_ICONS, SHADES, OPS, merge, LIST_ROWS, AI_MODES, VAR_OPS, relevant, V,
		create, addNode, find, incoming, whyNot, connect, disconnect, removeNode, clone, addCondition, condDefaults,
		chainFrom, entries, stages, firstStage, isStage, isCodeStage, problems, layout, migrate, reaches,
	});
})();
