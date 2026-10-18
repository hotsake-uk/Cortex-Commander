// Writes what Studio knows about each game it can make mods for (its "targets"), so Classic mode can work on a release build of
// Cortex Command Community Edition, which has the game's INI and Lua in Data/ but not the engine's source:
//
//   node Tools/ModStudio/tools/make-targets.js --ce <a git clone of cortex-command-community/Cortex-Command-Community-Project>
//
// For this game (Commander) it writes data/ini-surface.json. For each Community Edition release in TARGETS it takes that release's
// source from the clone (git archive of the tag, so the clone's checkout is left alone), and writes data/targets/<id>/: lua-api.json and
// lua-api.d.lua (tools/make-lua-api.js on that source), ini-surface.json, and target.json (the version, the commit it was made from).
// Fetch the tags first (git fetch origin --tags --force: "nightly" moves). Run it again when a new release comes out.
//
// ini-surface.json is every class an INI file can name, its parent class and the properties it reads itself (the MatchProperty names
// in its ReadProperty), the same model as Tools/Mods/engine_surface.py. Studio uses it to say when a key is one Commander reads but
// the target game doesn't.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';

const TARGETS = [
	{ id: 'ce-6.2.2', ref: 'v6.2.2', name: 'Community Edition 6.2.2' },
	{ id: 'ce-nightly', ref: 'nightly', name: 'Community Edition nightly' },
];

const option = (name) => {
	const at = process.argv.indexOf(name);
	return at > 0 ? process.argv[at + 1] : null;
};
const here = path.join(import.meta.dirname, '..');
const repo = path.resolve(here, '..', '..');
const data = path.join(here, 'data');

const read = (file) => fs.readFileSync(file, 'utf8');

// Comments out, string literals kept (they hold the names).
function stripComments(text) {
	return text.replace(/"(?:\\.|[^"\\\n])*"|\/\/[^\n]*|\/\*[\s\S]*?\*\//g, (m) => (m.startsWith('"') ? m : m.startsWith('//') ? ' ' : '\n'.repeat((m.match(/\n/g) || []).length)));
}

function iniSurface(root) {
	const classes = new Map(); // C++ class -> {parent, properties: Set, macro}
	const classNames = new Map(); // C++ class -> INI name
	const bases = new Map();
	(function walk(dir) {
		for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
			const full = path.join(dir, entry.name);
			if (entry.isDirectory()) {
				walk(full);
				continue;
			}
			if (!/\.(cpp|h)$/.test(entry.name)) {
				continue;
			}
			const text = stripComments(read(full));
			for (const m of text.matchAll(/(?:const\s+)?std::string\s+(\w+)::c_ClassName\s*=\s*"(\w+)"/g)) {
				classNames.set(m[1], m[2]);
			}
			for (const m of text.matchAll(/\bclass\s+(\w+)\s*(?:final\s*)?:\s*public\s+(\w+)/g)) {
				if (!bases.has(m[1])) {
					bases.set(m[1], m[2]);
				}
			}
			for (const m of text.matchAll(/int\s+([\w:]+)(?:<[^<>]*>)?::ReadProperty\s*\(\s*const\s+std::string_view\s*&\s*\w+\s*,\s*Reader\s*&\s*\w+\s*\)\s*\{/g)) {
				const cls = m[1].split('::').pop();
				// the function's body: up to its closing brace
				let depth = 1;
				let i = m.index + m[0].length;
				while (depth && i < text.length) {
					depth += text[i] === '{' ? 1 : text[i] === '}' ? -1 : 0;
					i++;
				}
				const body = text.slice(m.index + m[0].length, i);
				const start = /StartPropertyList\(([\s\S]*?)\);\s*\n/.exec(body);
				let parent = null;
				if (start) {
					const p = /(?:return\s+)?([\w:]+)::ReadProperty/.exec(start[1]);
					parent = p ? p[1].split('::').pop() : null;
				}
				if (!classes.has(cls)) {
					classes.set(cls, { parent, properties: new Set(), macro: !!start });
				}
				const entryFor = classes.get(cls);
				for (const prop of body.matchAll(/Match(?:Property|Forwards)\(\s*"(\w+)"/g)) {
					entryFor.properties.add(prop[1]);
				}
				if (parent) {
					entryFor.parent = parent;
				}
			}
		}
	})(path.join(root, 'Source'));
	// Classes that read nothing of their own read what their base class reads.
	for (const [cls, base] of bases) {
		if (!classes.has(cls) && (bases.has(base) || classes.has(base))) {
			classes.set(cls, { parent: base, properties: new Set(), macro: true });
		}
	}
	const reaches = (cls, seen = new Set()) => {
		const entry = classes.get(cls);
		return !!entry && !seen.has(cls) && (entry.properties.size > 0 || reaches(entry.parent, new Set([...seen, cls])));
	};
	const out = {};
	for (const [cls, entry] of [...classes].sort((a, b) => a[0].localeCompare(b[0]))) {
		if (!reaches(cls) || (!entry.macro && !entry.properties.size)) {
			continue;
		}
		out[classNames.get(cls) || cls] = { parent: entry.parent ? classNames.get(entry.parent) || entry.parent : null, properties: [...entry.properties].sort() };
	}
	// DataModule's unmatched properties are presets (AddActor = AHuman), not its parent's.
	if (out.DataModule) {
		out.DataModule.parent = null;
	}
	return out;
}

function version(root) {
	const m = /c_VersionString\s*=\s*"([^"]+)"/.exec(read(path.join(root, 'Source', 'System', 'GameVersion.h')));
	return m ? m[1] : '0.0.0';
}

function writeSurface(root, outDir) {
	const surface = iniSurface(root);
	fs.mkdirSync(outDir, { recursive: true });
	fs.writeFileSync(path.join(outDir, 'ini-surface.json'), JSON.stringify({ version: version(root), classes: surface }));
	return Object.keys(surface).length;
}

console.log('commander: ' + writeSurface(repo, data) + ' INI classes');

const ce = option('--ce');
if (!ce) {
	console.log('No --ce clone given, so only Commander\'s INI surface was written.');
	process.exit(0);
}
for (const target of TARGETS) {
	const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'studio-target-'));
	try {
		const sha = execFileSync('git', ['-C', ce, 'rev-list', '-n', '1', target.ref], { encoding: 'utf8' }).trim();
		const archive = path.join(temp, 'source.tar');
		execFileSync('git', ['-C', ce, 'archive', '-o', archive, target.ref, 'Source']);
		execFileSync('tar', ['-xf', archive, '-C', temp]);
		const outDir = path.join(data, 'targets', target.id);
		execFileSync(process.execPath, [path.join(import.meta.dirname, 'make-lua-api.js'), '--root', temp, '--out', outDir], { stdio: 'inherit' });
		const count = writeSurface(temp, outDir);
		const info = { id: target.id, name: target.name, version: version(temp), ref: target.ref, commit: sha };
		fs.writeFileSync(path.join(outDir, 'target.json'), JSON.stringify(info, null, '\t') + '\n');
		console.log(target.id + ': ' + info.version + ' at ' + sha.slice(0, 10) + ', ' + count + ' INI classes');
	} finally {
		fs.rmSync(temp, { recursive: true, force: true });
	}
}
