// Cortex Mod Studio: turns a flow graph (flowmodel.js) into the Lua a scenario runs, and puts that Lua into the scenario's script. It lives in a marked
// block of the script, with the graph itself as JSON on one comment line, so the script is the only place the mission is stored: the editor reads the
// graph back from it, and the Lua next to it is generated afresh from the graph each time. Code you write outside the block is never touched.
// Like flowmodel.js this has no page in it, so a test can run the Lua it makes (with a stand-in for the game) and check what happens.
//
// The block holds, in order: small helper functions (areas, counting units, the game's battle events), one function for each event node that runs
// its chain, five functions for each stage (start, done, time out, marker, check), the table of stages, and RunStudioFlow, which the script's
// UpdateActivity calls every frame: it reads battle events, moves the current stage along, and looks for each event.
'use strict';
window.MS = window.MS || {};

(function () {
	const F = MS.flow;
	const MARK_BEGIN = '-- >>> Studio flow';
	const MARK_END = '-- <<< Studio flow';
	const DATA = '-- studio-flow-data: ';
	// What older versions of Studio wrote: read for the graph (or the rules) they hold, and replaced when the script is written again.
	const LEGACY = [['-- >>> Studio rules', '-- <<< Studio rules', '-- studio-rules-data: '], ['-- >>> Studio triggers', '-- <<< Studio triggers', null]];

	// ---- small Lua writers ----

	const luaStr = (s) => '"' + String(s === undefined || s === null ? '' : s).replace(/\\/g, '\\\\').replace(/"/g, '\\"').replace(/[\r\n]+/g, ' ') + '"';
	const luaNum = (n) => String(Number.isFinite(Number(n)) ? Number(n) : 0);
	const ident = (s) => (String(s || '').replace(/[^A-Za-z0-9_]/g, '') || 'MyFunction').replace(/^[0-9]/, 'F$&');
	// A name a script's class can have: letters, digits and _, not starting with a digit ("1234" becomes "Mission1234").
	const className = (s, fallback) => {
		const c = String(s || '').replace(/[^A-Za-z0-9_]/g, '') || fallback || 'MyMission';
		return /^[0-9]/.test(c) ? 'Mission' + c : c;
	};
	const isClassName = (s) => /^[A-Za-z_][A-Za-z0-9_]*$/.test(String(s || ''));
	const opOf = (o) => (F.OPS.some(([v]) => v === o) ? o : '>=');
	const area = (name) => 'studioArea(' + luaStr(name) + ')';
	const AI_LUA = { sentry: 'AIMODE_SENTRY', goto: 'AIMODE_GOTO', brainhunt: 'AIMODE_BRAINHUNT', patrol: 'AIMODE_PATROL', stay: 'AIMODE_STAY', return: 'AIMODE_RETURN', golddig: 'AIMODE_GOLDDIG' };
	const aiMode = (m) => 'Actor.' + (AI_LUA[m] || 'AIMODE_SENTRY');

	// A value (flowmodel.js): a number, a variable, a scenario setting, the difficulty or the time, perhaps times the difficulty. Plain numbers too.
	function valueLua(v) {
		if (v === undefined || v === null || typeof v !== 'object') {
			return luaNum(v);
		}
		let out;
		switch (v.k) {
			case 'var': out = '(st.vars[' + luaStr(v.name) + '] or 0)'; break;
			case 'setting': out = 'self:GetScenarioSettingOr(' + luaStr(v.name) + ', ' + luaNum(v.n === undefined ? 1 : v.n) + ')'; break;
			case 'difficulty': out = 'studioDifficulty(self)'; break;
			case 'time': out = 'st.clock.ElapsedSimTimeS'; break;
			default: out = luaNum(v.n);
		}
		return v.x ? '(' + out + ' * studioDifficulty(self))' : out;
	}
	const key = (id) => luaStr(id);
	// which units (flowmodel.js WHO_UNITS), as the three arguments studioMatch takes
	const whoArgs = (a) => luaStr(a.which || 'any') + ', ' + luaStr(a.group || '') + ', ' + luaStr(a.named || '');
	// players a step or event is about: all of them, or one (0 to 3)
	const playerTest = (who) => (who === 'all' || who === undefined || who === '' ? 'true' : 'player == ' + luaNum(who));
	const itemName = (item) => String(item || '').split('|')[1] || '';
	// a value of the script's own (self.anyHumanHasDeployedABrain), tested as the settings say
	const scriptTest = (a) => {
		const v = 'studioField(self, ' + luaStr(a.field) + ')';
		if (a.test === 'false') {
			return '(' + v + ' == false)';
		}
		if (a.test === 'op') {
			return '(type(' + v + ') == "number" and ' + v + ' ' + opOf(a.op) + ' ' + valueLua(a.value) + ')';
		}
		return '(' + v + ' == true)';
	};
	// text shown to players: {name} becomes the variable's value as it is then
	const text = (s) => 'studioText(st, ' + luaStr(s) + ')';
	const indent = (lines, n) => lines.map((l) => '\t'.repeat(n || 1) + l);
	const itemCreate = (cls) => 'Create' + (['HeldDevice', 'TDExplosive', 'ThrownDevice'].includes(cls) ? cls : 'HDFirearm');

	// ---- conditions (the lines inside a branch) ----

	function conditionLua(g, c) {
		switch (c.kind) {
			case 'funds': return 'self:GetTeamFunds(' + luaNum(c.team) + ') ' + opOf(c.op) + ' ' + luaNum(c.amount);
			case 'time': return 'st.clock.ElapsedSimTimeS ' + opOf(c.op) + ' ' + luaNum(c.seconds);
			case 'units': return 'studioCount(' + luaNum(c.team) + ', nil) ' + opOf(c.op) + ' ' + luaNum(c.count);
			case 'inArea': return 'studioCount(' + luaNum(c.team) + ', ' + area(c.area) + ') ' + opOf(c.op) + ' ' + luaNum(c.count);
			case 'flag': return '(st.flags[' + luaStr(c.name) + '] == true) == ' + (c.value ? 'true' : 'false');
			case 'stage':
				if (F.isCodeStage(c.target)) {
					// a stage the script runs in its own code (self.stages, self.currentStage)
					return '(self.stages ~= nil and self.currentStage == self.stages[' + luaStr(c.target.slice(5)) + ']) == ' + (c.is === false ? 'false' : 'true');
				}
				return '(st.stage == ' + luaStr(c.target) + ') == ' + (c.is === false ? 'false' : 'true');
			case 'battleScore': return '((st.battle and st.battle.scores[' + luaNum(c.team) + ']) or 0) ' + opOf(c.op) + ' ' + luaNum(c.count);
			case 'var': return '(st.vars[' + luaStr(c.name) + '] or 0) ' + opOf(c.op) + ' ' + valueLua(c.value);
			case 'captureOwner': return 'studioOwner(st, ' + luaStr(c.target) + ') == ' + luaNum(c.team);
			case 'group': return 'studioGroupCount(' + luaStr(c.group) + ') ' + opOf(c.op) + ' ' + luaNum(c.count);
			case 'unitsWho': return 'studioCountWho(' + luaNum(c.team) + ', ' + (c.area ? area(c.area) : 'nil') + ', ' + whoArgs(c) + ') ' + opOf(c.op) + ' ' + luaNum(c.count);
			case 'brain': return 'studioHasBrain(self, ' + (c.who === 'all' || c.who === undefined ? -1 : luaNum(c.who)) + ')';
			case 'difficulty': return 'studioDifficulty(self) ' + opOf(c.op) + ' ' + luaNum(c.value === undefined ? 1 : c.value);
			case 'holding': return 'studioHolds(' + luaNum(c.team) + ', ' + luaStr(itemName(c.item)) + ')';
			case 'controlIn': return 'studioControlIn(self, ' + (c.who === 'all' || c.who === undefined ? -1 : luaNum(c.who)) + ', ' + area(c.area) + ')';
			case 'script': return scriptTest(c);
			default: return 'true';
		}
	}

	// ---- actions: the lines a step runs ----

	function actionLua(g, n) {
		const a = n.p || {};
		switch (n.kind) {
			case 'message': {
				const who = a.who === 'all' || a.who === undefined ? 'true' : 'player == ' + luaNum(a.who);
				return ['for player = 0, 3 do', '\tif self:PlayerActive(player) and self:PlayerHuman(player) and (' + who + ') then', '\t\tlocal screen = self:ScreenOfPlayer(player)', '\t\tFrameMan:ClearScreenText(screen)', '\t\tFrameMan:SetScreenText(' + text(a.text) + ', screen, 0, ' + luaNum(Number(a.seconds) * 1000) + ', false)', '\tend', 'end'];
			}
			case 'gold': return ['self:ChangeTeamFunds(' + valueLua(a.amount) + ', ' + luaNum(a.team) + ')'];
			case 'spawn': return spawnLua(a);
			case 'order': {
				const who = a.who || 'group';
				const test = who === 'group' ? 'actor:IsInGroup(' + luaStr(a.group) + ')' : who === 'area' ? 'actor.Team == ' + luaNum(a.team) + ' and zone ~= nil and zone:IsInside(actor.Pos)' : 'actor.Team == ' + luaNum(a.team);
				const mode = a.ai || 'goto';
				return ['local zone = ' + (who === 'area' ? area(a.area) : 'nil'), 'local target = ' + (mode === 'goto' ? area(a.goTo) : 'nil'),
					'for actor in MovableMan.Actors do', '\tif actor.Health > 0 and ' + test + ' then', '\t\tactor.AIMode = ' + aiMode(mode),
					...(mode === 'goto' ? ['\t\tif target then', '\t\t\tactor:ClearAIWaypoints()', '\t\t\tactor:AddAISceneWaypoint(target.Center)', '\t\tend'] : []), '\tend', 'end'];
			}
			case 'setVar': {
				const cur = '(st.vars[' + luaStr(a.name) + '] or 0)';
				const v = valueLua(a.value);
				const expr = a.op === 'add' ? cur + ' + ' + v : a.op === 'sub' ? cur + ' - ' + v : a.op === 'mul' ? cur + ' * ' + v : v;
				return ['st.vars[' + luaStr(a.name) + '] = ' + expr];
			}
			case 'captureSet': {
				const c = 'st.cap[' + luaStr(a.target) + ']';
				return ['if ' + c + ' then', a.do === 'off' ? '\t' + c + '.active = false' : a.do === 'own' ? '\t' + c + '.owner = ' + luaNum(a.team) + '; ' + c + '.progress = 0' : '\t' + c + '.active = true', 'end'];
			}
			case 'doors': {
				const act = a.do === 'close' ? 'd:CloseDoor()' : a.do === 'lock' ? 'd.Status = Actor.INACTIVE' : a.do === 'unlock' ? 'd.Status = Actor.STABLE' : 'd:OpenDoor()';
				return ['local zone = ' + area(a.area), 'if zone then', '\tfor box in zone.Boxes do', '\t\tfor mo in MovableMan:GetMOsInBox(box, -1, true) do', '\t\t\tif IsADoor(mo) then',
					'\t\t\t\tlocal d = ToADoor(mo)', ...(Number(a.team) >= 0 ? ['\t\t\t\tMovableMan:ChangeActorTeam(d, ' + luaNum(a.team) + ')'] : []), '\t\t\t\t' + act, '\t\t\tend', '\t\tend', '\tend', 'end'];
			}
			case 'fog': {
				if (a.do === 'hideAll') {
					return ['SceneMan:MakeAllUnseen(Vector(4, 4), ' + luaNum(a.team) + ')'];
				}
				const fn = a.do === 'hide' ? 'RestoreUnseenBox' : 'RevealUnseenBox';
				return ['local zone = ' + area(a.area), 'if zone then', '\tfor box in zone.Boxes do', '\t\tSceneMan:' + fn + '(box.Corner.X, box.Corner.Y, box.Width, box.Height, ' + luaNum(a.team) + ')', '\tend', 'end'];
			}
			case 'lz': return ['local zone = ' + area(a.area), 'if zone then self:SetLZArea(' + luaNum(a.team) + ', zone) end'];
			case 'buyMenu': return ['self.BuyMenuEnabled = ' + (a.on ? 'true' : 'false')];
			case 'setFunds': return ['self:SetTeamFunds(' + valueLua(a.amount) + ', ' + luaNum(a.team) + ')'];
			case 'income': return ['st.income[' + luaNum(a.team) + '] = ' + valueLua(a.amount)];
			case 'dialogue': {
				const lines = ((n.lists && n.lists.lines) || []).filter((l) => String(l.text || '').trim());
				const out = ['local at = math.max(st.clock.ElapsedSimTimeS, st.sayEnd or 0)'];
				for (const l of lines) {
					const secs = Math.max(0.5, Number(l.seconds) || 5);
					out.push('table.insert(st.say, { text = ' + luaStr(l.text) + ', at = at, ms = ' + luaNum(secs * 1000) + ', who = ' + (a.who === 'all' || a.who === undefined ? -1 : luaNum(a.who)) + ' })', 'at = at + ' + luaNum(secs));
				}
				out.push('st.sayEnd = at');
				return out;
			}
			case 'music': return ['MusicMan:PlayDynamicSong(' + luaStr(a.song) + ')'];
			case 'killAll': return ['local doomed = {}', 'for actor in MovableMan.Actors do', '\tif actor.Team == ' + luaNum(a.team) + ' then table.insert(doomed, actor) end', 'end', 'for _, actor in ipairs(doomed) do actor:GibThis() end'];
			case 'flag': return ['st.flags[' + luaStr(a.name) + '] = ' + (a.value ? 'true' : 'false')];
			case 'jump':
				if (F.isCodeStage(a.target)) {
					return ['if self.stages ~= nil and self.stages[' + luaStr(a.target.slice(5)) + '] ~= nil then self.currentStage = self.stages[' + luaStr(a.target.slice(5)) + '] end'];
				}
				return a.target && F.find(g, a.target) ? ['st.stage = ' + luaStr(a.target), 'st.stageStarted = false'] : ['st.stage = nil', 'st.stageStarted = false'];
			case 'win': {
				const say = a.text ? ['for player = 0, 3 do', '\tif self:PlayerActive(player) and self:PlayerHuman(player) then', '\t\tlocal screen = self:ScreenOfPlayer(player)', '\t\tFrameMan:ClearScreenText(screen)', '\t\tFrameMan:SetScreenText(' + text(a.text) + ', screen, 0, ' + luaNum(Math.max(1, Number(a.after) || 5) * 1000) + ', true)', '\tend', 'end'] : [];
				return Number(a.after) > 0
					? [...say, 'st.ending = { team = ' + luaNum(a.team) + ', at = st.clock.ElapsedSimTimeS + ' + luaNum(a.after) + ' }']
					: [...say, 'self.WinnerTeam = ' + luaNum(a.team), 'ActivityMan:EndActivity()'];
			}
			case 'end': return ['ActivityMan:EndActivity()'];
			case 'call': return ['self:' + ident(a.fn) + '(actor)'];
			case 'code': {
				// your own Lua, run where the node is: self is the mission, st its state, actor the unit that set the chain off (or nil). An error in it
				// is written to the console and the mission carries on.
				const label = String(a.label || 'My code').replace(/[\r\n]+/g, ' ');
				const body = String(a.code || '').replace(/\r\n/g, '\n').split('\n');
				return ['-- ' + label, 'do', '\tlocal ok, err = pcall(function()', ...indent(body, 2), '\tend)', '\tif not ok then ConsoleMan:PrintString("Studio code (" .. ' + luaStr(label) + ' .. "): " .. tostring(err)) end', 'end'];
			}
			case 'placeItem': {
				const parts = String(a.item || '').split('|');
				return ['local where = ' + area(a.area), 'if where then', '\tfor i = 1, math.floor(' + valueLua(a.count) + ' + 0.5) do', '\t\tlocal item = ' + itemCreate(parts[0]) + '(' + luaStr(parts[1] || '') + ', ' + luaStr(parts[2] || 'Base.rte') + ')', '\t\tif item then', '\t\t\titem.Pos = where.RandomPoint', '\t\t\tMovableMan:AddItem(item)', '\t\tend', '\tend', 'end'];
			}
			case 'remove': {
				const what = a.what || 'units';
				const L = ['local zone = ' + area(a.area), 'if zone then'];
				if (what !== 'items') {
					L.push('\tlocal gone = {}', '\tfor actor in MovableMan.Actors do', '\t\tif (' + luaNum(a.team) + ' < 0 or actor.Team == ' + luaNum(a.team) + ') and zone:IsInside(actor.Pos) and studioMatch(actor, ' + whoArgs(a) + ') then gone[#gone + 1] = actor end', '\tend', '\tfor _, actor in ipairs(gone) do actor.ToDelete = true end');
				}
				if (what !== 'units') {
					L.push('\tfor item in MovableMan.Items do', '\t\tif zone:IsInside(item.Pos) then item.ToDelete = true end', '\tend');
				}
				L.push('end');
				return L;
			}
			case 'changeTeam': return ['local zone = ' + (a.area ? area(a.area) : 'nil'), 'local moving = {}', 'for actor in MovableMan.Actors do',
				'\tif actor.Health > 0 and actor.Team == ' + luaNum(a.team) + ' and (zone == nil or zone:IsInside(actor.Pos)) and studioMatch(actor, ' + whoArgs(a) + ') then moving[#moving + 1] = actor end', 'end',
				'for _, actor in ipairs(moving) do MovableMan:ChangeActorTeam(actor, ' + luaNum(a.to) + ') end'];
			case 'control': {
				const player = a.who === 'all' || a.who === undefined ? 0 : Number(a.who) || 0;
				return ['local zone = ' + (a.area ? area(a.area) : 'nil'), 'for unit in MovableMan.Actors do',
					'\tif unit.Health > 0 and unit.Team == ' + luaNum(a.team) + ' and (zone == nil or zone:IsInside(unit.Pos)) and studioMatch(unit, ' + whoArgs(a) + ') then',
					'\t\tself:SwitchToActor(unit, ' + luaNum(player) + ', unit.Team)', '\t\tbreak', '\tend', 'end'];
			}
			case 'camera': {
				const secs = Math.max(0, Number(a.seconds) || 0);
				const speed = Math.min(1, Math.max(0.01, Number(a.speed) || 0.1));
				return ['local zone = ' + area(a.area), 'if zone then',
					'\tfor player = 0, 3 do', '\t\tif self:PlayerActive(player) and self:PlayerHuman(player) and (' + playerTest(a.who) + ') then CameraMan:SetScrollTarget(zone.Center, ' + luaNum(speed) + ', self:ScreenOfPlayer(player)) end', '\tend',
					...(secs > 0 ? ['\tst.camera = { area = ' + luaStr(a.area) + ', untilT = st.clock.ElapsedSimTimeS + ' + luaNum(secs) + ', speed = ' + luaNum(speed) + ', who = ' + (a.who === 'all' || a.who === undefined ? -1 : luaNum(a.who)) + ' }'] : []), 'end'];
			}
			case 'tech': return ['self:SetTeamTech(' + luaNum(a.team) + ', ' + luaStr(a.tech) + ')'];
			case 'playerStart': return [
				'local zone = ' + area(a.area), 'local team = ' + luaNum(a.team), 'local tech = studioTech(self, team)',
				'local x = zone and zone.Center.X or 0', 'local top = zone and (zone.Center.Y - 40) or 0',
				'for player = 0, 3 do', '\tif self:PlayerActive(player) and self:PlayerHuman(player) then',
				'\t\tlocal brain = PresetMan:GetLoadout("Infantry Brain", tech, false)',
				'\t\tif brain then', '\t\t\tbrain:RemoveInventoryItem("Constructor")', '\t\telse', '\t\t\tbrain = studioUnit("Brains", tech)', '\t\t\tif brain then studioArm(brain, "light", tech) end', '\t\tend',
				'\t\tif brain then',
				'\t\t\tbrain.Team = team', '\t\t\tbrain.AIMode = Actor.AIMODE_SENTRY', '\t\t\tbrain.Pos = studioGround(x, top, 12)', '\t\t\tbrain:AddToGroup("Brain " .. player)',
				'\t\t\tMovableMan:AddActor(brain)', '\t\t\tself:SetPlayerBrain(brain, player)', '\t\t\tself:SwitchToActor(brain, player, team)', '\t\t\tself:SetObservationTarget(brain.Pos, player)',
				'\t\tend',
				'\t\tfor i = 1, math.floor(' + valueLua(a.squad) + ' + 0.5) do',
				'\t\t\tlocal unit = studioUnit(' + luaStr(a.group || 'Actors - Light') + ', tech)',
				'\t\t\tif unit then',
				'\t\t\t\tunit.Team = team', '\t\t\t\tunit.AIMode = Actor.AIMODE_SENTRY',
				...((a.arm || 'light') !== 'none' ? ['\t\t\t\tstudioArm(unit, ' + luaStr(a.arm || 'light') + ', tech)'] : []),
				'\t\t\t\tunit.Pos = studioGround(x + ((i % 2 == 0) and 1 or -1) * (20 + 18 * math.floor(i / 2)) + player * 6, top, 12)',
				'\t\t\t\tMovableMan:AddActor(unit)', '\t\t\tend', '\t\tend',
				'\tend', 'end'];
			case 'scriptSet': return ['studioSetField(self, ' + luaStr(a.field) + ', ' + (a.to === 'false' ? 'false' : a.to === 'value' ? valueLua(a.value) : 'true') + ')'];
			case 'pieAdd': {
				// what the button is and who gets it, kept in the state so units that come later get it too (studioPieGive, every frame)
				const to = a.to || 'brain';
				const spec = '{ label = ' + luaStr(a.label) + ', icon = ' + luaStr(a.icon || 'Land') + ', dir = ' + luaStr(a.dir || 'Up') + ', to = ' + luaStr(to)
					+ ', player = ' + (a.who === 'all' || a.who === undefined ? -1 : luaNum(a.who)) + ', team = ' + luaNum(a.team) + ', group = ' + luaStr(a.group || '') + ' }';
				return a.keep === false ? ['studioPieGive(self, ' + spec + ')'] : ['st.pieGive[' + key(n.id) + '] = ' + spec, 'studioPieGive(self, st.pieGive[' + key(n.id) + '])'];
			}
			case 'pieRemove': return ['studioPieRemove(self, st, ' + luaStr(a.label) + ')'];
			case 'overlay': {
				const k = luaStr(String(a.area || '') + '|' + luaNum(a.team));
				return a.do === 'hide' ? ['st.overlays[' + k + '] = nil'] : ['st.overlays[' + k + '] = { area = ' + luaStr(a.area) + ', team = ' + luaNum(a.team) + ', color = ' + luaNum(a.color === undefined ? 147 : a.color) + ', text = ' + luaStr(a.text || '') + ' }'];
			}
			case 'battle': return [
				'st.battle = { started = false, ended = false, winner = -1, scores = {} }',
				'if SandboxBattleMode then',
				...(a.layout ? ['\tSandboxBattleLayout(' + luaNum(a.mode) + ', ' + luaNum(a.seed) + ', 1)'] : []),
				'\tSandboxBattleMode(' + luaNum(a.mode) + ', ' + luaNum(a.size) + ', ' + (a.byShip ? 'true' : 'false') + ')',
				'else',
				'\tConsoleMan:PrintString("Studio: this game build has no battle modes to start (SandboxBattleMode).")',
				'end',
			];
			case 'battleStop': return ['if SandboxBattleMode then SandboxBattleMode(0, 2, false) end'];
			default: return ['-- (unknown step ' + n.kind + ')'];
		}
	}

	// Spawning: a unit picked, a random one of a group (in the team's tech) or a loadout; in the area at random, or one in each of its boxes; there
	// and then, or brought in by a craft; facing a way; set to an AI mode (going to an area); and put in a group for later orders.
	function spawnLua(a) {
		const source = a.source || 'preset';
		const parts = String(a.preset || '').split('|');
		const make = source === 'group' ? 'studioUnit(' + luaStr(a.group) + ', tech)'
			: source === 'loadout' ? 'PresetMan:GetLoadout(' + luaStr(a.loadout) + ', tech, false)'
			: 'Create' + (parts[0] === 'ACrab' ? 'ACrab' : 'AHuman') + '(' + luaStr(parts[1] || '') + ', ' + luaStr(parts[2] || 'Base.rte') + ')';
		const mode = a.ai || 'sentry';
		const L = [
			'local where = ' + area(a.area),
			'local tech = studioTech(self, ' + luaNum(a.team) + ')',
			'local target = ' + (mode === 'goto' ? area(a.goTo) : 'nil'),
			'if where then',
			'\tlocal spots = {}',
			a.perBox ? '\tfor box in where.Boxes do spots[#spots + 1] = box.Center end' : '\tfor i = 1, math.floor(' + valueLua(a.count) + ' + 0.5) do spots[#spots + 1] = where.RandomPoint end',
		];
		const ship = a.arrive === 'dropship' || a.arrive === 'rocket';
		if (ship) {
			const kind = a.arrive === 'rocket' ? 'RandomACRocket' : 'RandomACDropShip';
			L.push('\tlocal craft = ' + kind + '("Craft", tech) or ' + kind + '("Craft", "Base.rte")', '\tif craft then', '\t\tcraft.Team = ' + luaNum(a.team), '\t\tcraft.Pos = Vector(where.Center.X, -50)', '\t\tcraft.AIMode = Actor.AIMODE_DELIVER');
		}
		const pre = ship ? '\t\t' : '\t';
		L.push(pre + 'for _, spot in ipairs(spots) do', pre + '\tlocal unit = ' + make, pre + '\tif unit then', pre + '\t\tunit.Team = ' + luaNum(a.team), pre + '\t\tunit.AIMode = ' + aiMode(mode));
		if (source === 'group' && (a.arm || 'light') !== 'none') {
			L.push(pre + '\t\tstudioArm(unit, ' + luaStr(a.arm || 'light') + ', tech)');
		}
		if (a.facing === 'left' || a.facing === 'right') {
			L.push(pre + '\t\tunit.HFlipped = ' + (a.facing === 'left' ? 'true' : 'false'));
		}
		if (mode === 'goto') {
			L.push(pre + '\t\tif target then unit:AddAISceneWaypoint(target.Center) end');
		}
		if (a.tag) {
			L.push(pre + '\t\tunit:AddToGroup(' + luaStr(a.tag) + ')');
		}
		L.push(...(ship ? [pre + '\t\tcraft:AddInventoryItem(unit)'] : [pre + '\t\tunit.Pos = spot', pre + '\t\tMovableMan:AddActor(unit)']), pre + '\tend', pre + 'end');
		if (ship) {
			L.push('\t\tMovableMan:AddActor(craft)', '\tend');
		}
		L.push('end');
		return L;
	}

	// ---- chains: a node and everything its then wire leads on to ----

	function chainLua(g, id, depth) {
		const L = [];
		let at = id;
		let guard = 0;
		while (at && guard++ < 300 && (depth || 0) < 40) {
			const n = F.find(g, at);
			const spec = n && F.KINDS[n.kind];
			if (!spec || spec.entrance === false) {
				break;
			}
			if (n.kind === 'branch') {
				const conds = (n.lists && n.lists.conds) || [];
				const test = conds.length ? conds.map((c) => '(' + conditionLua(g, c) + ')').join(' and ') : 'true';
				const yes = chainLua(g, (n.out || {}).true, (depth || 0) + 1);
				const no = conds.length ? chainLua(g, (n.out || {}).false, (depth || 0) + 1) : [];
				L.push('if ' + test + ' then', ...indent(yes.length ? yes : ['-- (nothing)']));
				if (no.length) {
					L.push('else', ...indent(no));
				}
				L.push('end');
				break;
			}
			if (n.kind === 'doOnce') {
				L.push('if not st.once[' + key(n.id) + '] then', '\tst.once[' + key(n.id) + '] = true', ...indent(chainLua(g, (n.out || {}).then, (depth || 0) + 1)), 'end');
				break;
			}
			L.push(...actionLua(g, n));
			at = (n.out || {}).then;
		}
		return L;
	}

	// ---- events: the lines that decide whether one has happened (setting fire) ----

	function eventLua(n) {
		const a = n.p || {};
		const k = key(n.id);
		const team = luaNum(a.team);
		// An event that stays true (a team gone, a timer that has passed) fires once, unless it is set to keep running.
		const latch = (lines) => (a.again ? lines : ['if not st.done[' + k + '] then', ...indent(lines), '\tif fire then st.done[' + k + '] = true end', 'end']);
		switch (n.kind) {
			case 'start': return ['if not st.started then fire = true end'];
			case 'timer': return a.repeat
				? ['local due = st.next[' + k + '] or ' + luaNum(a.seconds), 'if st.clock.ElapsedSimTimeS >= due then fire = true; st.next[' + k + '] = due + ' + luaNum(Math.max(0.1, Number(a.seconds))) + ' end']
				: latch(['if st.clock.ElapsedSimTimeS >= ' + luaNum(a.seconds) + ' then fire = true end']);
			case 'enter': case 'leave': case 'empty': {
				const lines = ['local now, count = studioInside(' + luaStr(a.area) + ', ' + team + (n.kind === 'empty' ? '' : ', ' + whoArgs(a)) + ')', 'local before = st.inside[' + k + '] or {}'];
				if (n.kind === 'enter') {
					lines.push('for id in pairs(now) do if not before[id] then fire = true; st.who[' + k + '] = now[id] end end');
				} else if (n.kind === 'leave') {
					lines.push('for id in pairs(before) do if not now[id] then fire = true end end');
				} else {
					lines.push('if count == 0 and next(before) ~= nil then fire = true end');
				}
				lines.push('st.inside[' + k + '] = now');
				return lines;
			}
			case 'teamGone': return latch(['local alive = studioCount(' + team + ', nil)', 'if alive > 0 then st.seen[' + k + '] = true end', 'if st.seen[' + k + '] and alive == 0 then fire = true end']);
			case 'brainGone': return latch(['local alive = studioBrains(' + team + ')', 'if alive > 0 then st.seen[' + k + '] = true end', 'if st.seen[' + k + '] and alive == 0 then fire = true end']);
			case 'funds': return ['local reached = self:GetTeamFunds(' + team + ') ' + opOf(a.op) + ' ' + luaNum(a.amount), 'if reached and not st.prev[' + k + '] then fire = true end', 'st.prev[' + k + '] = reached'];
			case 'battleStarted': return ['if studioBattleEvent(st, "started", -1) then fire = true end'];
			case 'battleScore': return ['if studioBattleEvent(st, "score", ' + team + ') then fire = true end'];
			case 'battleObjective': return ['if studioBattleEvent(st, "objective", ' + team + ') then fire = true end'];
			case 'battleEnded': return ['if studioBattleEvent(st, "ended", ' + team + ') then fire = true end'];
			case 'varReaches': return ['local reached = (st.vars[' + luaStr(a.name) + '] or 0) ' + opOf(a.op) + ' ' + valueLua(a.value), 'if reached and not st.prev[' + k + '] then fire = true end', 'st.prev[' + k + '] = reached'];
			case 'captureChanged': return ['for _, e in ipairs(st.capNow) do if e.id == ' + luaStr(a.target) + ' and (' + team + ' < 0 or e.team == ' + team + ') then fire = true end end'];
			case 'groupGone': return latch(['local alive = studioGroupCount(' + luaStr(a.group) + ')', 'if alive > 0 then st.seen[' + k + '] = true end', 'if st.seen[' + k + '] and alive == 0 then fire = true end']);
			case 'unitDies': return [
				'local zone = ' + (a.area ? area(a.area) : 'nil'), 'local now = {}',
				'for actor in MovableMan.Actors do', '\tif actor.Health > 0 and (' + team + ' < 0 or actor.Team == ' + team + ') and studioMatch(actor, ' + whoArgs(a) + ') then now[actor.UniqueID] = Vector(actor.Pos.X, actor.Pos.Y) end', 'end',
				'if st.alive[' + k + '] then', '\tfor id, pos in pairs(st.alive[' + k + ']) do', '\t\tif not now[id] and (zone == nil or zone:IsInside(pos)) then fire = true end', '\tend', 'end',
				'st.alive[' + k + '] = now'];
			case 'unitArrives': return [
				'local zone = ' + (a.area ? area(a.area) : 'nil'), 'local now = {}', 'local known = st.known[' + k + ']',
				'for actor in MovableMan.Actors do', '\tif actor.Health > 0 and (' + team + ' < 0 or actor.Team == ' + team + ') and studioMatch(actor, ' + whoArgs(a) + ') then',
				'\t\tnow[actor.UniqueID] = true', '\t\tif known and not known[actor.UniqueID] and (zone == nil or zone:IsInside(actor.Pos)) then fire = true; st.who[' + k + '] = actor end', '\tend', 'end',
				'st.known[' + k + '] = now'];
			case 'hurt': return [
				'st.marks[' + k + '] = st.marks[' + k + '] or {}',
				'for actor in MovableMan.Actors do', '\tif actor.Health > 0 and actor.Health < ' + luaNum(a.below) + ' and (' + team + ' < 0 or actor.Team == ' + team + ') and not st.marks[' + k + '][actor.UniqueID] and studioMatch(actor, ' + whoArgs(a) + ') then',
				'\t\tst.marks[' + k + '][actor.UniqueID] = true', '\t\tfire = true', '\t\tst.who[' + k + '] = actor', '\tend', 'end'];
			case 'brainPlaced': return [
				'st.marks[' + k + '] = st.marks[' + k + '] or {}',
				'for player = 0, 3 do', '\tif self:PlayerActive(player) and self:PlayerHuman(player) and (' + playerTest(a.who) + ') then',
				'\t\tlocal brain = self:GetPlayerBrain(player)', '\t\tlocal has = brain ~= nil and MovableMan:IsActor(brain)',
				'\t\tif has and not st.marks[' + k + '][player] then fire = true; st.who[' + k + '] = brain end', '\t\tst.marks[' + k + '][player] = has', '\tend', 'end'];
			case 'holdArea': return latch([
				'local zone = ' + area(a.area), 'local now = st.clock.ElapsedSimTimeS',
				'if zone and studioCount(' + team + ', zone) > 0 and studioEnemies(' + team + ', zone) == 0 then',
				'\tst.hold[' + k + '] = st.hold[' + k + '] or now', '\tif now - st.hold[' + k + '] >= ' + luaNum(a.seconds) + ' then fire = true end',
				'else', '\tst.hold[' + k + '] = nil', 'end']);
			case 'controls': return [
				'st.ctl[' + k + '] = st.ctl[' + k + '] or {}',
				'for player = 0, 3 do', '\tif self:PlayerActive(player) and self:PlayerHuman(player) and (' + playerTest(a.who) + ') then',
				'\t\tlocal unit = self:GetControlledActor(player)', '\t\tlocal id = unit and unit.UniqueID or -1', '\t\tlocal before = st.ctl[' + k + '][player]',
				'\t\tif before ~= nil and id ~= before and unit and studioMatch(unit, ' + whoArgs(a) + ') then fire = true; st.who[' + k + '] = unit end',
				'\t\tst.ctl[' + k + '][player] = id', '\tend', 'end'];
			case 'selectView': return [
				'local selecting = false',
				'for player = 0, 3 do', '\tif self:PlayerActive(player) and self:PlayerHuman(player) and (' + playerTest(a.who) + ') and self:GetViewState(player) == Activity.ACTORSELECT then selecting = true end', 'end',
				'if selecting and not st.prev[' + k + '] then fire = true end', 'st.prev[' + k + '] = selecting'];
			case 'pickUp': return ['local reached = studioHolds(' + team + ', ' + luaStr(itemName(a.item)) + ')', 'if reached and not st.prev[' + k + '] then fire = true end', 'st.prev[' + k + '] = reached'];
			case 'stageBegins': {
				const on = F.isCodeStage(a.target) ? '(self.stages ~= nil and self.currentStage == self.stages[' + luaStr(a.target.slice(5)) + '])' : '(st.stage == ' + luaStr(a.target) + ' and st.stageStarted == true)';
				return ['local reached = ' + on, 'if reached and not st.prev[' + k + '] then fire = true end', 'st.prev[' + k + '] = reached'];
			}
			case 'flagChanges': return ['local reached = (st.flags[' + luaStr(a.name) + '] == true) == ' + (a.value === false ? 'false' : 'true'), 'if reached and not st.prev[' + k + '] then fire = true end', 'st.prev[' + k + '] = reached'];
			case 'pieUsed': return ['for _, e in ipairs(st.piePressed) do if e.label == ' + luaStr(a.label) + ' then fire = true; st.who[' + k + '] = e.actor end end'];
			case 'scriptValue': return ['local reached = ' + scriptTest(a), 'if reached and not st.prev[' + k + '] then fire = true end', 'st.prev[' + k + '] = reached'];
			default: return ['fire = true']; // always
		}
	}

	// ---- stages ----

	function stageLua(g, cls, n) {
		const a = n.p || {};
		const id = n.id;
		const L = [];
		const wrap = (pin) => {
			const body = chainLua(g, (n.out || {})[pin], 0);
			return body.length ? indent(body) : ['\t-- (nothing)'];
		};
		L.push('-- Stage ' + String(a.name || id).replace(/[\r\n]+/g, ' '));
		L.push('function ' + cls + ':StudioStage_' + id + '_Start()', '\tlocal st = self.StudioState');
		if (n.kind === 'stageFetch' && a.item && a.from) {
			const parts = String(a.item).split('|');
			L.push('\tlocal where = ' + area(a.from), '\tlocal item = ' + itemCreate(parts[0]) + '(' + luaStr(parts[1] || '') + ', ' + luaStr(parts[2] || 'Base.rte') + ')', '\tif item and where then', '\t\titem.Pos = where.RandomPoint', '\t\tMovableMan:AddItem(item)', '\tend');
		}
		L.push(...wrap('started'), 'end');
		L.push('function ' + cls + ':StudioStage_' + id + '_Done()', '\tlocal st = self.StudioState', ...wrap('done'), 'end');
		L.push('function ' + cls + ':StudioStage_' + id + '_Fail()', '\tlocal st = self.StudioState', ...wrap('timeout'), 'end');
		const text = 'studioText(self.StudioState, ' + luaStr(a.text || a.name || '') + ')'; // {name} shows a variable's value
		const viewer = luaNum(a.viewer);
		L.push('function ' + cls + ':StudioStage_' + id + '_Mark()');
		if (a.marker) {
			if (n.kind === 'stageCapture') {
				L.push('\t-- (the capture point marks itself)');
			} else if (['stageReach', 'stageHold', 'stageFetch'].includes(n.kind)) {
				L.push('\tlocal where = ' + area(a.area), '\tif where then self:AddObjectivePoint(' + text + ', where.Center, ' + viewer + ', GameActivity.ARROWDOWN) end');
			} else if (n.kind === 'stageDestroy' || n.kind === 'stageBrain') {
				L.push('\tfor actor in MovableMan.Actors do', '\t\tif actor.Team == ' + luaNum(a.team) + ' and actor.Health > 0' + (n.kind === 'stageBrain' ? ' and actor:IsInGroup("Brains")' : '') + ' then self:AddObjectivePoint(' + text + ', actor.AboveHUDPos, ' + viewer + ', GameActivity.ARROWDOWN) end', '\tend');
			} else {
				L.push('\t-- (the game lights up a battle\'s own objectives; nothing to mark for this goal)');
			}
		} else {
			L.push('\t-- (no marker)');
		}
		L.push('end');
		L.push('function ' + cls + ':StudioStage_' + id + '_Check()', '\tlocal st = self.StudioState', '\tlocal elapsed = st.stageClock.ElapsedSimTimeS');
		switch (n.kind) {
			case 'stageReach': L.push('\treturn studioCount(' + luaNum(a.team) + ', ' + area(a.area) + ') > 0'); break;
			case 'stageHold':
				L.push('\tlocal region = ' + area(a.area), '\tlocal held = region ~= nil and studioCount(' + luaNum(a.team) + ', region) > 0 and studioEnemies(' + luaNum(a.team) + ', region) == 0', '\tif not held then st.holdFrom = nil elseif not st.holdFrom then st.holdFrom = elapsed end', '\treturn st.holdFrom ~= nil and elapsed - st.holdFrom >= ' + luaNum(a.seconds));
				break;
			case 'stageDestroy': L.push('\treturn elapsed > 1 and studioCount(' + luaNum(a.team) + ', nil) == 0'); break;
			case 'stageBrain': L.push('\treturn elapsed > 1 and studioBrains(' + luaNum(a.team) + ') == 0'); break;
			case 'stageFetch': L.push('\treturn studioItemIn(' + luaStr(a.area) + ', ' + luaStr(String(a.item || '').split('|')[1] || '') + ')'); break;
			case 'stageBattle': L.push('\treturn st.battle ~= nil and st.battle.ended and (' + luaNum(a.team) + ' < 0 or st.battle.winner == ' + luaNum(a.team) + ')'); break;
			case 'stageBattleScore': L.push('\treturn ((st.battle and st.battle.scores[' + luaNum(a.team) + ']) or 0) >= ' + luaNum(a.count)); break;
			case 'stageCapture': L.push('\treturn studioOwner(st, ' + luaStr(a.target) + ') == ' + luaNum(a.team)); break;
			default: L.push('\treturn elapsed >= ' + luaNum(a.seconds));
		}
		L.push('end', '');
		return L;
	}

	// ---- the whole block ----

	function helpers() {
		return [
			'local function studioArea(name)', '\tif name ~= "" and SceneMan.Scene:HasArea(name) then return SceneMan.Scene:GetArea(name) end', '\treturn nil', 'end', '',
			'local function studioCount(team, region)', '\tlocal n = 0', '\tfor actor in MovableMan.Actors do', '\t\tif actor.Health > 0 and (team < 0 or actor.Team == team) and (region == nil or region:IsInside(actor.Pos)) then n = n + 1 end', '\tend', '\treturn n', 'end', '',
				'-- A team\'s tech as a module id: the one it plays, or the game\'s own (Base.rte) when it has none.',
				'local function studioTech(act, team)', '\tlocal id = PresetMan:GetModuleID(act:GetTeamTech(team) or "")', '\tif not id or id < 0 then id = PresetMan:GetModuleID("Base.rte") end', '\treturn id', 'end',
				'-- A random unit of a group, from the tech if it has one there, else from any.',
				'local function studioUnit(group, tech)', '\tlocal unit = RandomAHuman(group, tech)', '\tif not unit then unit = RandomAHuman(group, "Base.rte") end', '\tif not unit then unit = RandomAHuman("Actors", tech) end', '\treturn unit', 'end',
				'-- What a unit carries: a random unit of a group comes with nothing, so it is given weapons of its tech (as the game\'s missions do).',
				'local function studioItem(make, group, tech)', '\tlocal item = make(group, tech)', '\tif not item then item = make(group, "Base.rte") end', '\treturn item', 'end',
				'local function studioArm(unit, arm, tech)', '\tif not unit or arm == "none" then return end',
				'\tlocal first = studioItem(RandomHDFirearm, arm == "heavy" and "Weapons - Heavy" or "Weapons - Primary", tech) or studioItem(RandomHDFirearm, "Weapons - Light", tech)',
				'\tif first then unit:AddInventoryItem(first) end', '\tlocal second = studioItem(RandomHDFirearm, "Weapons - Secondary", tech)', '\tif second then unit:AddInventoryItem(second) end',
				'\tif arm == "heavy" then local bomb = studioItem(RandomTDExplosive, "Bombs - Grenades", tech); if bomb then unit:AddInventoryItem(bomb) end end', 'end',
				'-- A point on the ground below (x, y), for a unit to stand on.',
				'local function studioGround(x, y, radius)', '\tlocal p = Vector(x, y)', '\tif SceneMan.MovePointToGround then p = SceneMan:MovePointToGround(p, radius or 10, 5) end', '\treturn p', 'end', '',
				'-- Whether a unit is of the kind asked for: any, a brain, a craft, in a group, or of a name.',
			'local function studioMatch(actor, which, group, named)', '\tif which == "brain" then return actor:IsInGroup("Brains")', '\telseif which == "craft" then return IsACraft(actor)',
			'\telseif which == "group" then return actor:IsInGroup(group)', '\telseif which == "named" then return actor.PresetName == named end', '\treturn true', 'end', '',
			'local function studioInside(name, team, which, group, named)', '\tlocal region = studioArea(name)', '\tlocal now, count = {}, 0', '\tif region then', '\t\tfor actor in MovableMan.Actors do', '\t\t\tif actor.Health > 0 and (team < 0 or actor.Team == team) and region:IsInside(actor.Pos) and (which == nil or studioMatch(actor, which, group, named)) then now[actor.UniqueID] = actor; count = count + 1 end', '\t\tend', '\tend', '\treturn now, count', 'end', '',
			'local function studioCountWho(team, region, which, group, named)', '\tlocal n = 0', '\tfor actor in MovableMan.Actors do', '\t\tif actor.Health > 0 and (team < 0 or actor.Team == team) and (region == nil or region:IsInside(actor.Pos)) and studioMatch(actor, which, group, named) then n = n + 1 end', '\tend', '\treturn n', 'end', '',
			'local function studioHasBrain(act, who)', '\tfor player = 0, 3 do', '\t\tif act:PlayerActive(player) and act:PlayerHuman(player) and (who < 0 or who == player) then', '\t\t\tlocal brain = act:GetPlayerBrain(player)', '\t\t\tif brain ~= nil and MovableMan:IsActor(brain) then return true end', '\t\tend', '\tend', '\treturn false', 'end', '',
			'local function studioControlIn(act, who, region)', '\tif not region then return false end', '\tfor player = 0, 3 do', '\t\tif act:PlayerActive(player) and act:PlayerHuman(player) and (who < 0 or who == player) then', '\t\t\tlocal unit = act:GetControlledActor(player)', '\t\t\tif unit and region:IsInside(unit.Pos) then return true end', '\t\tend', '\tend', '\treturn false', 'end', '',
			'-- Whether a unit of a team (any team: -1) has an item of a name in its hands or its inventory.',
			'local function studioHolds(team, itemName)', '\tfor actor in MovableMan.Actors do', '\t\tif actor.Health > 0 and (team < 0 or actor.Team == team) then', '\t\t\tlocal held = actor.EquippedItem', '\t\t\tif held and held.PresetName == itemName then return true end', '\t\t\tfor item in actor.Inventory do', '\t\t\t\tif item.PresetName == itemName then return true end', '\t\t\tend', '\t\tend', '\tend', '\treturn false', 'end', '',
			'-- The script\'s own values: self.name, or self.a.b for one inside a table.',
			'local function studioField(act, path)', '\tlocal v = act', '\tfor part in path:gmatch("[^%.]+") do', '\t\tif type(v) ~= "table" and type(v) ~= "userdata" then return nil end', '\t\tv = v[part]', '\tend', '\treturn v', 'end',
			'local function studioSetField(act, path, value)', '\tlocal at, last = act, nil', '\tfor part in path:gmatch("[^%.]+") do', '\t\tif last then at[last] = at[last] or {}; at = at[last] end', '\t\tlast = part', '\tend', '\tif last then at[last] = value end', 'end', '',
			'-- Text shown to players: {name} becomes that variable\'s value.',
			'local function studioText(st, text)', '\treturn (text:gsub("{([%w_]+)}", function(name)', '\t\tlocal v = st.vars and st.vars[name]', '\t\tif v == nil then return "{" .. name .. "}" end', '\t\treturn string.format("%g", v)', '\tend))', 'end', '',
			'-- What the game\'s battle mode reports (an older game build has none): kept in st.battle, with this frame\'s events in st.battleNow.',
			'local function studioPump(st)', '\tst.battleNow = {}', '\tif not SandboxBattleEvent then return end', '\tlocal line = SandboxBattleEvent()', '\twhile line ~= "" do',
			'\t\tlocal parts = {}', '\t\tfor token in line:gmatch("%S+") do parts[#parts + 1] = token end', '\t\tlocal name, a, b = parts[1], tonumber(parts[2]) or -1, tonumber(parts[3]) or 0',
			'\t\tst.battle = st.battle or { started = false, ended = false, winner = -1, scores = {} }',
			'\t\tif name == "started" then st.battle = { started = true, ended = false, winner = -1, scores = {} }', '\t\telseif name == "score" or name == "objective" then st.battle.scores[a] = b', '\t\telseif name == "ended" then st.battle.ended = true; st.battle.winner = a end',
			'\t\ttable.insert(st.battleNow, { name = name, team = a })', '\t\tline = SandboxBattleEvent()', '\tend', 'end', '',
			'local function studioBattleEvent(st, name, team)', '\tfor _, e in ipairs(st.battleNow or {}) do', '\t\tif e.name == name and (team < 0 or e.team == team) then return true end', '\tend', '\treturn false', 'end', '',
			'-- The difficulty as a multiplier, as the game\'s own missions use it: 0.5 at the easiest, 1 at medium, 2 at the hardest.',
			'local function studioDifficulty(act)', '\tlocal d = act.Difficulty or 50', '\tif d <= Activity.MINDIFFICULTY then return 0.5 elseif d <= Activity.CAKEDIFFICULTY then return 0.625 elseif d <= Activity.EASYDIFFICULTY then return 0.75', '\telseif d <= Activity.MEDIUMDIFFICULTY then return 1 elseif d <= Activity.HARDDIFFICULTY then return 1.5 elseif d <= Activity.NUTSDIFFICULTY then return 1.75 end', '\treturn 2', 'end', '',
			'local function studioGroupCount(group)', '\tlocal n = 0', '\tfor actor in MovableMan.Actors do', '\t\tif actor.Health > 0 and actor:IsInGroup(group) then n = n + 1 end', '\tend', '\treturn n', 'end', '',
			'local function studioOwner(st, id)', '\tlocal c = st.cap and st.cap[id]', '\treturn c and c.owner or -2', 'end', '',
			'-- A capture point, a frame on: progress grows while the side taking it stands in it (and, to start, nobody of the side holding it is in the',
			'-- area round it), drains when they have gone (if it drains), and the point changes hands when it is full. Returns the new owner when it does.',
			'local function studioCaptureStep(self, st, id, cfg)', '\tlocal c = st.cap[id]', '\tif not c then',
			'\t\tc = { owner = cfg.owner, progress = 0, active = cfg.active }', '\t\tst.cap[id] = c', '\tend', '\tif not c.active then return nil end',
			'\tlocal zone = studioArea(cfg.area)', '\tif not zone then return nil end', '\tlocal around = studioArea(cfg.region) or zone',
			'\tlocal taker = c.owner == cfg.attackers and cfg.owner or cfg.attackers', '\tlocal taking = studioCount(taker, zone) > 0',
			'\tif c.progress > 0 or (taking and studioCount(c.owner, around) == 0) then',
			'\t\tlocal rate = (st.dt or 0) / math.max(0.1, cfg.seconds)',
			'\t\tif taking then c.progress = c.progress + rate elseif cfg.decays then c.progress = math.max(0, c.progress - rate) end', '\tend',
			'\tif cfg.marker and c.owner ~= cfg.attackers then',
			'\t\tself:AddObjectivePoint(studioText(st, cfg.text) .. (c.progress > 0 and taker == cfg.attackers and (" " .. math.floor(c.progress * 100) .. "%") or ""), zone.Center, cfg.attackers, GameActivity.ARROWDOWN)', '\tend',
			'\tif c.progress >= 1 then', '\t\tc.owner = taker', '\t\tc.progress = 0', '\t\treturn taker', '\tend', '\treturn nil', 'end', '',
			'-- Saving: the mission\'s state as one line of text (tables of numbers, text and true/false; the game\'s own objects are left out, and a unit',
			'-- kept by its id is kept as true). Reading it back gives the same tables.',
			'local function studioEsc(s) return (tostring(s):gsub("[%%;=/]", function(c) return string.format("%%%02X", c:byte()) end)) end',
			'local function studioUnesc(s) return (s:gsub("%%(%x%x)", function(h) return string.char(tonumber(h, 16)) end)) end',
			'local function studioEncode(t, path, out)', '\tfor k, v in pairs(t) do', '\t\tlocal key = (path and path .. "/" or "") .. studioEsc(type(k) == "number" and ("#" .. k) or k)', '\t\tlocal kind = type(v)',
			'\t\tif kind == "table" then studioEncode(v, key, out)', '\t\telseif kind == "number" then out[#out + 1] = "n=" .. key .. "=" .. v', '\t\telseif kind == "string" then out[#out + 1] = "s=" .. key .. "=" .. studioEsc(v)',
			'\t\telseif kind == "boolean" then out[#out + 1] = "b=" .. key .. "=" .. (v and "1" or "0")', '\t\telseif kind == "userdata" then out[#out + 1] = "b=" .. key .. "=1" end', '\tend', '\treturn out', 'end',
			'local function studioDecode(text)', '\tlocal root = {}', '\tfor entry in text:gmatch("[^;]+") do', '\t\tlocal kind, key, value = entry:match("^(%a)=([^=]*)=(.*)$")', '\t\tif kind then',
			'\t\t\tlocal at, parts = root, {}', '\t\t\tfor part in key:gmatch("[^/]+") do parts[#parts + 1] = studioUnesc(part) end',
			'\t\t\tfor i, part in ipairs(parts) do', '\t\t\t\tlocal k = part:sub(1, 1) == "#" and tonumber(part:sub(2)) or part',
			'\t\t\t\tif i == #parts then at[k] = kind == "n" and tonumber(value) or kind == "b" and value == "1" or studioUnesc(value)', '\t\t\t\telse at[k] = at[k] or {}; at = at[k] end', '\t\t\tend', '\t\tend', '\tend', '\treturn root', 'end', '',
			'-- Tables a state needs, also for one from an older save.',
			'local function studioFill(st)', '\tfor _, k in ipairs({ "flags", "inside", "who", "seen", "prev", "next", "done", "once", "vars", "cap", "income", "incomeDue", "say", "alive", "known", "marks", "hold", "ctl", "pieGive", "pieQueue", "piePressed", "overlays" }) do st[k] = st[k] or {} end', 'end', '',
			'local function studioEnemies(team, region)', '\tlocal n = 0', '\tfor actor in MovableMan.Actors do', '\t\tif actor.Health > 0 and actor.Team >= 0 and actor.Team ~= team and region:IsInside(actor.Pos) then n = n + 1 end', '\tend', '\treturn n', 'end', '',
			'local function studioBrains(team)', '\tlocal n = 0', '\tfor actor in MovableMan.Actors do', '\t\tif actor.Health > 0 and actor.Team == team and actor:IsInGroup("Brains") then n = n + 1 end', '\tend', '\treturn n', 'end', '',
			'-- Pie menu buttons: one slice made per button (CreatePieSlice) and kept on the mission, which the game copies into each unit\'s menu. Using it',
			'-- runs the global function the block defines (the slice names this script and that function), which queues the press for the next frame.',
			'local function studioPieSlice(act, spec)', '\tact.StudioPieSlices = act.StudioPieSlices or {}', '\tlocal k = spec.label .. "|" .. spec.icon .. "|" .. spec.dir', '\tlocal slice = act.StudioPieSlices[k]',
			'\tif slice == nil then', '\t\tslice = CreatePieSlice(spec.icon, "Base.rte")', '\t\tif slice == nil then return nil end',
			'\t\tslice.PresetName = spec.label', '\t\tslice.Description = spec.label', '\t\tslice.Type = PieSlice.NoType', '\t\tslice.Direction = Directions[spec.dir] or Directions.Any',
			'\t\tif studioPieScript ~= "" then', '\t\t\tslice.ScriptPath = studioPieScript', '\t\t\tslice.FunctionName = studioPieFunction',
			'\t\telse', '\t\t\tConsoleMan:PrintString("Studio: the pie menu button " .. spec.label .. " does nothing: the mission does not know its script\'s path.")', '\t\tend',
			'\t\tact.StudioPieSlices[k] = slice', '\tend', '\treturn slice', 'end',
			'local function studioPieGive(act, spec)', '\tlocal slice = studioPieSlice(act, spec)', '\tif not slice then return end',
			'\tlocal function give(unit) if unit and unit.PieMenu then unit.PieMenu:AddPieSliceIfPresetNameIsUnique(slice, act) end end',
			'\tif spec.to == "brain" then', '\t\tfor player = 0, 3 do', '\t\t\tif act:PlayerActive(player) and act:PlayerHuman(player) and (spec.player < 0 or spec.player == player) then give(act:GetPlayerBrain(player)) end', '\t\tend',
			'\telse', '\t\tfor actor in MovableMan.Actors do', '\t\t\tif actor.Health > 0 and ((spec.to == "team" and (spec.team < 0 or actor.Team == spec.team)) or (spec.to == "group" and actor:IsInGroup(spec.group))) then give(actor) end', '\t\tend', '\tend', 'end',
			'local function studioPieRemove(act, st, label)', '\tfor id, spec in pairs(st.pieGive) do if spec.label == label then st.pieGive[id] = nil end end',
			'\tfor actor in MovableMan.Actors do if actor.PieMenu then actor.PieMenu:RemovePieSlicesByPresetName(label) end end',
			'\tfor player = 0, 3 do', '\t\tlocal brain = act:PlayerActive(player) and act:GetPlayerBrain(player) or nil', '\t\tif brain and brain.PieMenu then brain.PieMenu:RemovePieSlicesByPresetName(label) end', '\tend', 'end', '',
			'-- Areas shaded for a team while its players choose a unit (the unit select view), with a line of text on each.',
			'local function studioOverlays(act, st)', '\tfor _, o in pairs(st.overlays) do', '\t\tlocal zone = studioArea(o.area)', '\t\tif zone then', '\t\t\tfor player = 0, 3 do',
			'\t\t\t\tif act:PlayerActive(player) and act:PlayerHuman(player) and (o.team < 0 or act:GetTeamOfPlayer(player) == o.team) and act:GetViewState(player) == Activity.ACTORSELECT then',
			'\t\t\t\t\tlocal boxes = {}', '\t\t\t\t\tfor box in zone.Boxes do boxes[#boxes + 1] = BoxFillPrimitive(player, box.Corner, Vector(box.Corner.X + box.Width, box.Corner.Y + box.Height), o.color) end',
			'\t\t\t\t\tPrimitiveMan:DrawPrimitives(75, boxes)', '\t\t\t\t\tif o.text ~= "" then PrimitiveMan:DrawTextPrimitive(player, zone.Center, studioText(st, o.text), false, 1) end',
			'\t\t\t\tend', '\t\t\tend', '\t\tend', '\tend', 'end', '',
			'local function studioItemIn(areaName, itemName)', '\tlocal region = studioArea(areaName)', '\tif not region then return false end', '\tfor item in MovableMan.Items do', '\t\tif item.PresetName == itemName and region:IsInside(item.Pos) then return true end', '\tend', '\tfor actor in MovableMan.Actors do', '\t\tif actor.Health > 0 and region:IsInside(actor.Pos) then', '\t\t\tlocal held = actor.EquippedItem', '\t\t\tif held and held.PresetName == itemName then return true end', '\t\t\tfor item in actor.Inventory do', '\t\t\t\tif item.PresetName == itemName then return true end', '\t\t\tend', '\t\tend', '\tend', '\treturn false', 'end', '',
		];
	}

	// The global function a pie menu button runs (the game looks it up by name in the script): StudioPieSlice_<class>.
	const pieFunction = (cls) => 'StudioPieSlice_' + cls;

	// scriptPath: the scenario's script, as its ScriptPath says (Mod.rte/...): a pie menu button names it, so the game can find the function it runs.
	function compile(g, cls, scriptPath) {
		const L = [];
		const pies = g.nodes.some((n) => n.kind === 'pieAdd' || n.kind === 'pieUsed');
		const events = F.entries(g).filter((n) => (n.out || {}).then);
		const stageNodes = F.stages(g);
		const captures = g.nodes.filter((n) => n.kind === 'capture');
		L.push(MARK_BEGIN + ': managed by Cortex Studio (the Scenarios editor, Mission logic). Change the mission there; this block is rewritten. Your own functions go outside it.');
		L.push(DATA + JSON.stringify(g));
		L.push('local studioPieScript, studioPieFunction = ' + luaStr(String(scriptPath || '').replace(/\\/g, '/')) + ', ' + luaStr(pieFunction(cls)));
		L.push(...helpers());
		if (pies) {
			L.push('-- A pie menu button was used: the button and the unit whose menu it was wait in the mission\'s state for the next frame.',
				'function ' + pieFunction(cls) + '(pieMenuOwner, pieMenu, pieSlice)', '\tlocal st = ' + cls + ' and ' + cls + '.StudioState', '\tif st and pieSlice then',
				'\t\tst.pieQueue = st.pieQueue or {}', '\t\ttable.insert(st.pieQueue, { label = pieSlice.PresetName, actor = pieMenuOwner })', '\tend', 'end', '');
		}
		for (const n of stageNodes) {
			L.push(...stageLua(g, cls, n));
		}
		const first = F.firstStage(g);
		L.push(cls + '.StudioFirstStage = ' + (first ? luaStr(first.id) : 'nil'));
		L.push(cls + '.StudioStages = {', ...stageNodes.map((n) => '\t[' + luaStr(n.id) + '] = { next = ' + ((n.out || {}).next && F.find(g, n.out.next) ? luaStr(n.out.next) : 'nil') + ', limit = ' + luaNum((n.p || {}).failAfter) + ' },'), '}', '');
		for (const c of captures) {
			for (const pin of ['taken', 'retaken']) {
				L.push('function ' + cls + ':StudioCapture_' + c.id + '_' + pin + '()', '\tlocal st = self.StudioState', '\tlocal actor = nil', ...indent(chainLua(g, (c.out || {})[pin], 0)), 'end');
			}
			L.push('');
		}
		// saved games: the state goes into the save, and comes back when it is loaded
		L.push('function ' + cls + ':StudioSave()', '\tlocal st = self.StudioState', '\tif not st then return end',
			'\tst.clockS = st.clock.ElapsedSimTimeS', '\tst.stageS = st.stageClock and st.stageClock.ElapsedSimTimeS or nil',
			'\tlocal keep = {}', '\tfor k, v in pairs(st) do if k ~= "clock" and k ~= "stageClock" and k ~= "battleNow" and k ~= "who" and k ~= "capNow" and k ~= "inside" and k ~= "alive" and k ~= "known" and k ~= "ctl" and k ~= "marks" and k ~= "pieQueue" and k ~= "piePressed" then keep[k] = v end end',
			'\tkeep.inside = {}', '\tfor k, units in pairs(st.inside or {}) do keep.inside[k] = {}; for id in pairs(units) do keep.inside[k][id] = true end end',
			'\tself:SaveString("StudioFlow", table.concat(studioEncode(keep, nil, {}), ";"))', 'end',
			'function ' + cls + ':StudioLoad()', '\tlocal text = self:LoadString("StudioFlow")', '\tif not text or text == "" then return end',
			'\tlocal st = studioDecode(text)', '\tst.clock = Timer()', '\tst.clock.ElapsedSimTimeS = st.clockS or 0', '\tst.lastT = st.clockS or 0',
			'\tif st.stageS then st.stageClock = Timer(); st.stageClock.ElapsedSimTimeS = st.stageS end',
			'\tself.StudioState = st', 'end', '',
			'');
		for (const n of events) {
			const label = String(n.note || (F.KINDS[n.kind] || {}).title || n.kind).replace(/[\r\n]+/g, ' ');
			L.push('-- ' + label, 'function ' + cls + ':StudioFlow_' + n.id + '(actor)', '\tlocal st = self.StudioState', ...indent(chainLua(g, n.out.then, 0)), 'end', '');
		}
		L.push('function ' + cls + ':RunStudioFlow()');
		L.push('\tself.StudioState = self.StudioState or { clock = Timer(), flags = {}, inside = {}, who = {}, seen = {}, prev = {}, next = {}, done = {}, once = {}, started = false }');
		L.push('\tlocal st = self.StudioState', '\tstudioFill(st)', '\tlocal now = st.clock.ElapsedSimTimeS', '\tst.dt = now - (st.lastT or now)', '\tst.lastT = now', '\tstudioPump(st)');
		// Objective markers are added afresh every frame, so last frame's go first (else they pile up). In a script with markers of its own,
		// call RunStudioFlow before adding them.
		if (stageNodes.length || captures.length) {
			L.push('\tself:ClearObjectivePoints()');
		}
		// pie menu buttons used since the last frame, and buttons for units that have come since
		L.push('\tst.piePressed = st.pieQueue', '\tst.pieQueue = {}', '\tfor _, spec in pairs(st.pieGive) do studioPieGive(self, spec) end');
		// capture points, every frame
		L.push('\tst.capNow = {}');
		for (const c of captures) {
			const a = c.p || {};
			L.push('\tdo -- capture point ' + c.id + ': ' + String(a.name || '').replace(/[\r\n]+/g, ' '),
				'\t\tlocal taken = studioCaptureStep(self, st, ' + luaStr(c.id) + ', { area = ' + luaStr(a.area) + ', region = ' + luaStr(a.region || '') + ', owner = ' + luaNum(a.owner) + ', attackers = ' + luaNum(a.attackers) + ', seconds = ' + valueLua(a.seconds) + ', decays = ' + (a.decays ? 'true' : 'false') + ', active = ' + (a.active === false ? 'false' : 'true') + ', marker = ' + (a.marker ? 'true' : 'false') + ', text = ' + luaStr(a.text || a.name || 'Capture') + ' })',
				'\t\tif taken then',
				'\t\t\ttable.insert(st.capNow, { id = ' + luaStr(c.id) + ', team = taken })',
				'\t\t\tif taken == ' + luaNum(a.attackers) + ' then self:StudioCapture_' + c.id + '_taken() else self:StudioCapture_' + c.id + '_retaken() end',
				'\t\tend', '\tend');
		}
		if (stageNodes.length) {
			L.push('\tif ' + cls + '.StudioFirstStage and not st.stagesBegun then st.stage = ' + cls + '.StudioFirstStage; st.stagesBegun = true end');
			L.push('\tif st.stage then',
				'\t\tlocal id = st.stage',
				'\t\tlocal info = ' + cls + '.StudioStages[id]',
				'\t\tif not info then',
				'\t\t\tst.stage = nil',
				'\t\telse',
				'\t\t\tif not st.stageStarted then',
				'\t\t\t\tst.stageStarted = true', '\t\t\t\tst.stageClock = Timer()', '\t\t\t\tst.holdFrom = nil',
				'\t\t\t\tself["StudioStage_" .. id .. "_Start"](self)',
				'\t\t\tend',
				'\t\t\tif st.stage == id then',
				'\t\t\t\tself["StudioStage_" .. id .. "_Mark"](self)',
				'\t\t\t\tif self["StudioStage_" .. id .. "_Check"](self) then',
				'\t\t\t\t\tself["StudioStage_" .. id .. "_Done"](self)',
				'\t\t\t\t\tif st.stage == id then st.stage = info.next; st.stageStarted = false end',
				'\t\t\t\telseif info.limit > 0 and st.stageClock.ElapsedSimTimeS >= info.limit then',
				'\t\t\t\t\tself["StudioStage_" .. id .. "_Fail"](self)',
				'\t\t\t\t\tif st.stage == id then st.stage = info.next; st.stageStarted = false end',
				'\t\t\t\tend',
				'\t\t\tend',
				'\t\tend',
				'\tend');
		}
		for (const n of events) {
			L.push('\tdo -- ' + n.id, '\t\tlocal fire = false', ...indent(eventLua(n), 2), '\t\tif fire then self:StudioFlow_' + n.id + '(st.who[' + key(n.id) + ']) end', '\tend');
		}
		// after the events (which may just have said something, or won): steady income, dialogue lines as they come due, areas shaded in the unit
		// select view, and a win that waits
		L.push('\tfor team, perSecond in pairs(st.income) do',
			'\t\tst.incomeDue[team] = (st.incomeDue[team] or 0) + perSecond * st.dt',
			'\t\tlocal whole = math.floor(st.incomeDue[team])',
			'\t\tif whole ~= 0 then st.incomeDue[team] = st.incomeDue[team] - whole; self:SetTeamFunds(self:GetTeamFunds(team) + whole, team) end',
			'\tend',
			'\tnow = st.clock.ElapsedSimTimeS', '\twhile st.say[1] and st.say[1].at <= now do',
			'\t\tlocal line = table.remove(st.say, 1)',
			'\t\tfor player = 0, 3 do',
			'\t\t\tif self:PlayerActive(player) and self:PlayerHuman(player) and (line.who < 0 or line.who == player) then',
			'\t\t\t\tlocal screen = self:ScreenOfPlayer(player)', '\t\t\t\tFrameMan:ClearScreenText(screen)', '\t\t\t\tFrameMan:SetScreenText(studioText(st, line.text), screen, 0, line.ms, true)',
			'\t\t\tend', '\t\tend', '\tend',
			'\tif st.camera then',
			'\t\tlocal zone = studioArea(st.camera.area)',
			'\t\tif zone and now < st.camera.untilT then',
			'\t\t\tfor player = 0, 3 do',
			'\t\t\t\tif self:PlayerActive(player) and self:PlayerHuman(player) and (st.camera.who < 0 or st.camera.who == player) then CameraMan:SetScrollTarget(zone.Center, st.camera.speed, self:ScreenOfPlayer(player)) end',
			'\t\t\tend',
			'\t\telse st.camera = nil end',
			'\tend',
			'\tstudioOverlays(self, st)',
			'\tif st.ending and now >= st.ending.at then', '\t\tself.WinnerTeam = st.ending.team', '\t\tst.ending = nil', '\t\tActivityMan:EndActivity()', '\tend');
		L.push('\tst.started = true', 'end', MARK_END);
		return L.join('\n');
	}

	// ---- reading and writing the script ----

	// The graph in a script, or null when there is none (an older block's rules or stages are turned into a graph).
	function parse(code) {
		if (!code) {
			return null;
		}
		const tryLine = (marker) => {
			const at = code.indexOf(marker);
			if (at < 0) {
				return undefined;
			}
			const end = code.indexOf('\n', at);
			try {
				return JSON.parse(code.slice(at + marker.length, end < 0 ? undefined : end).replace(/\r$/, ''));
			} catch (e) {
				return null;
			}
		};
		const now = tryLine(DATA);
		if (now) {
			return F.migrate(now);
		}
		for (const [, , marker] of LEGACY) {
			const old = marker ? tryLine(marker) : undefined;
			if (old) {
				return F.migrate(old);
			}
		}
		return null;
	}

	// The script with the flow block replaced (or added before UpdateActivity), UpdateActivity running it, and a function written for each of your own
	// functions the graph calls that the script does not have yet.
	function apply(code, cls, g, scriptPath) {
		let text = String(code || '').replace(/\r\n/g, '\n');
		for (const [begin, end] of [[MARK_BEGIN, MARK_END], ...LEGACY.map(([b, e]) => [b, e])]) {
			const from = text.indexOf(begin);
			const to = text.indexOf(end);
			if (from >= 0 && to > from) {
				text = text.slice(0, from).replace(/\s*$/, '') + '\n\n' + text.slice(to + end.length).replace(/^\n+/, '');
			}
		}
		const block = compile(g, cls, scriptPath);
		const update = new RegExp('function\\s+' + cls + ':UpdateActivity\\s*\\(\\s*\\)');
		if (update.test(text)) {
			text = text.replace(update, (m) => block + '\n\n' + m);
		} else {
			text = text.replace(/\s*$/, '') + '\n\n' + block + '\n\nfunction ' + cls + ':UpdateActivity()\n\tself:RunStudioFlow()\nend\n';
		}
		// UpdateActivity runs the flow, and no longer the first versions' functions
		text = text.replace(/\n[ \t]*self:RunStudioRules\(\)/g, '').replace(/\n[ \t]*self:RunStudioTriggers\(\)/g, '');
		const body = new RegExp(cls + ':UpdateActivity\\s*\\(\\s*\\)[^]*?:RunStudioFlow\\(\\)');
		if (!body.test(text)) {
			text = text.replace(update, (m) => m + '\n\tself:RunStudioFlow()');
		}
		// a saved game keeps the mission's progress: OnSave saves it, and StartActivity (loading a save) reads it back
		if (!/:StudioSave\(\)/.test(text.replace(block, ''))) {
			const onSave = new RegExp('function\\s+' + cls + ':OnSave\\s*\\(\\s*\\)');
			text = onSave.test(text) ? text.replace(onSave, (m) => m + '\n\tself:StudioSave() -- Cortex Studio: the mission\'s progress goes into the save') : text.replace(/\s*$/, '') + '\n\nfunction ' + cls + ':OnSave()\n\tself:StudioSave() -- Cortex Studio: the mission\'s progress goes into the save\nend\n';
		}
		if (!/:StudioLoad\(\)/.test(text.replace(block, ''))) {
			const start = new RegExp('function\\s+' + cls + ':StartActivity\\s*\\(\\s*([A-Za-z_][A-Za-z0-9_]*)?\\s*\\)');
			const m = text.match(start);
			if (m) {
				const arg = m[1] || 'isNewGame';
				const header = m[1] ? m[0] : m[0].replace('()', '(isNewGame)');
				text = text.replace(m[0], header + '\n\tif not ' + arg + ' then self:StudioLoad() end -- Cortex Studio: a loaded game carries on where the mission was');
			} else {
				text = text.replace(/\s*$/, '') + '\n\nfunction ' + cls + ':StartActivity(isNewGame)\n\tif not isNewGame then self:StudioLoad() end -- Cortex Studio: a loaded game carries on where the mission was\nend\n';
			}
		}
		for (const n of g.nodes) {
			if (n.kind === 'call') {
				const name = ident((n.p || {}).fn);
				if (!new RegExp('function\\s+' + cls + ':' + name + '\\s*\\(').test(text)) {
					text = text.replace(/\s*$/, '') + '\n\n-- Called by a "Run my own function" step in the mission. actor is the unit that came in, when the chain began with a unit entering an area.\nfunction ' + cls + ':' + name + '(actor)\n\t\nend\n';
				}
			}
		}
		return text;
	}

	MS.flow = Object.assign(MS.flow || {}, { compile, parse, apply, luaStr, ident, className, isClassName, MARK_BEGIN, MARK_END });
})();
