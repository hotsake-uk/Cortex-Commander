// Cortex Mod Studio: every module's presets, and what a preset really holds once CopyOf is followed.
//
// A module is read the way the game reads it: Index.ini, then each IncludeFile in turn. Every block with a PresetName becomes a
// preset, top-level or nested (the game registers nested named presets too, which is what lets "CopyOf = Human Walk Path" work).
//
// "Resolving" a block gives its properties in the order the game applies them: CopyOf first copies everything from the named preset,
// then each line after it adds to or overrides that. The last value of a key wins; list keys (AddAttachable, AddGib, ScriptPath...)
// keep every entry.
'use strict';
window.MS = window.MS || {};

(function () {
	const OFFICIAL_ORDER = ['Base.rte', 'Coalition.rte', 'Imperatus.rte', 'Techion.rte', 'Dummy.rte', 'Ronin.rte', 'Browncoats.rte', 'Uzira.rte', 'MuIlaak.rte', 'Missions.rte'];

	class Entity {
		constructor(library, node, entries, cls) {
			this.library = library;
			this.node = node; // The block this was resolved from.
			this.entries = entries; // [{key, node}] in the order the game applies them.
			this.cls = cls;
		}

		get presetName() {
			return this.str('PresetName', '');
		}

		last(key) {
			for (let i = this.entries.length - 1; i >= 0; i--) {
				if (this.entries[i].key === key) {
					return this.entries[i];
				}
			}
			return null;
		}

		all(key) {
			return this.entries.filter((e) => e.key === key);
		}

		has(key) {
			return !!this.last(key);
		}

		str(key, fallback) {
			const entry = this.last(key);
			return entry ? entry.node.value : fallback;
		}

		num(key, fallback) {
			const entry = this.last(key);
			if (!entry) {
				return fallback;
			}
			const n = parseFloat(entry.node.value);
			return Number.isFinite(n) ? n : fallback;
		}

		bool(key, fallback) {
			const entry = this.last(key);
			if (!entry) {
				return fallback;
			}
			const v = entry.node.value.trim().toLowerCase();
			return !(v === '0' || v === 'false' || v === 'no');
		}

		vec(key, fallback) {
			const entry = this.last(key);
			return entry ? MS.readVector(entry.node) : fallback || { x: 0, y: 0 };
		}

		// A ContentFile property's path ("SpriteFile = ContentFile" with "FilePath = ...").
		file(key) {
			const entry = this.last(key);
			if (!entry) {
				return null;
			}
			const resolved = this.library.resolve(entry.node);
			return resolved.str('FilePath', resolved.str('Path', null));
		}

		// The entity a property defines ("Head = Attachable" with its own lines).
		sub(key) {
			const entry = this.last(key);
			return entry ? this.library.resolve(entry.node) : null;
		}

		// Where a key's current value was written.
		sourceOf(key) {
			const entry = this.last(key);
			return entry ? entry.node : null;
		}
	}

	class Library {
		constructor(fs) {
			this.fs = fs;
			this.modules = []; // [{name, home, official, docs: [IniDoc], loaded}]
			this.docs = new Map(); // lowercased path -> IniDoc
			this.presets = []; // [{name, cls, node, doc, module}]
			this.byName = new Map(); // lowercased name -> [preset]
			this.cache = new Map();
			this.warnings = [];
			this.onDocDirty = null;
		}

		async load(progress) {
			const found = this.fs.modules();
			const order = (m) => {
				const i = OFFICIAL_ORDER.findIndex((n) => n.toLowerCase() === m.name.toLowerCase());
				return m.home === 'Data' ? (i >= 0 ? i : 50) : 100;
			};
			found.sort((a, b) => order(a) - order(b) || a.name.localeCompare(b.name));
			this.modules = found.map((m) => ({ name: m.name, home: m.home, official: m.home === 'Data', docs: [], title: m.name }));
			let done = 0;
			for (const module of this.modules) {
				if (progress) {
					progress('Reading ' + module.name, done / this.modules.length);
				}
				await this.loadModule(module);
				done++;
			}
			this.reindex();
		}

		async loadModule(module) {
			const seen = new Set();
			const readFile = async (path) => {
				const key = path.toLowerCase();
				if (seen.has(key)) {
					return;
				}
				seen.add(key);
				let doc = this.docs.get(key);
				if (!doc) {
					let text;
					try {
						text = await this.fs.readText(path);
					} catch (e) {
						this.warnings.push(module.name + ': couldn\'t read ' + path + ' (' + (e && (e.name + ': ' + e.message)) + ')');
						return;
					}
					doc = new MS.IniDoc(path, text);
					this.adoptDoc(doc);
				}
				module.docs.push(doc);
				for (const node of doc.walk()) {
					if (node.key === 'IncludeFile' && node.value) {
						await readFile(node.value.replace(/\\/g, '/'));
					} else if (node.key === 'ModuleName' && node.parent === doc.root.children.find((n) => n.key === 'DataModule')) {
						module.title = node.value;
					}
				}
			};
			await readFile(module.name + '/Index.ini');
		}

		adoptDoc(doc) {
			doc.onDirty = (d) => {
				if (this.onDocDirty) {
					this.onDocDirty(d);
				}
			};
			this.docs.set(doc.path.toLowerCase(), doc);
		}

		moduleOf(path) {
			const name = path.split('/')[0].toLowerCase();
			return this.modules.find((m) => m.name.toLowerCase() === name) || null;
		}

		// The designer changes only your mods: ones Studio made, and ones you chose to edit (Edit this mod, which keeps the mod as it
		// was in Studio's history first). The game's own files and other people's mods are locked until then. this.editable is the
		// mods with a history, from the server; without a server (read only, saves download a zip) only the game's own are locked.
		isProtected(doc) {
			return !!this.lockKind(doc);
		}

		// '' (yours), 'game' (the game's own files) or 'mod' (someone else's mod, not chosen to edit).
		lockKind(doc) {
			if (!doc) {
				return '';
			}
			if (this.fs.isOfficial(doc.path)) {
				return 'game';
			}
			if (!this.fs.server || !this.editable) {
				return '';
			}
			const module = doc.path.split('/')[0].toLowerCase();
			return this.editable.has(module) || this.madeInStudio(module) ? '' : 'mod';
		}

		madeInStudio(module) {
			const index = this.docFor(module + '/Index.ini');
			return !!index && /Made with Cortex Mod Studio/i.test(index.savedText || index.serialize());
		}

		lockMessage(doc) {
			const module = doc.path.split('/')[0];
			return this.lockKind(doc) === 'game'
				? 'This is in ' + module + ', the game\'s own files. Make a copy in your mod to change it.'
				: module + ' is someone else\'s mod. Choose Edit this mod (Studio keeps it as it is now, so you can go back), or make a copy in your mod.';
		}

		reindex() {
			this.presets = [];
			this.byName.clear();
			this.cache.clear();
			for (const module of this.modules) {
				for (const doc of module.docs) {
					for (const node of doc.walk()) {
						if (!node.children.length) {
							continue;
						}
						const nameNode = node.children.find((c) => c.key === 'PresetName');
						if (nameNode && nameNode.value) {
							const preset = { name: nameNode.value, cls: node.value, node, doc, module: module.name, topLevel: node.parent === doc.root };
							this.presets.push(preset);
							const key = nameNode.value.toLowerCase();
							if (!this.byName.has(key)) {
								this.byName.set(key, []);
							}
							this.byName.get(key).push(preset);
						}
					}
				}
			}
		}

		// Finds a preset the way CopyOf does: an optional "Module.rte/" in front, then the module asking, then the official modules.
		find(name, cls, fromModule) {
			if (!name) {
				return null;
			}
			let wanted = name.trim();
			let module = fromModule;
			const slash = wanted.indexOf('/');
			if (slash > 0 && wanted.slice(0, slash).toLowerCase().endsWith('.rte')) {
				module = wanted.slice(0, slash);
				wanted = wanted.slice(slash + 1);
			}
			const list = this.byName.get(wanted.toLowerCase());
			if (!list || !list.length) {
				return null;
			}
			const sameClass = list.filter((p) => !cls || p.cls === cls);
			const pool = sameClass.length ? sameClass : list;
			const inModule = module ? pool.filter((p) => p.module.toLowerCase() === module.toLowerCase()) : [];
			// Within a module a later definition replaces an earlier one.
			if (inModule.length) {
				return inModule[inModule.length - 1];
			}
			const official = pool.filter((p) => this.fs.isOfficial(p.module));
			return (official.length ? official : pool)[0];
		}

		invalidate() {
			this.cache.clear();
		}

		resolve(node) {
			if (this.cache.has(node)) {
				return this.cache.get(node);
			}
			const visiting = this._visiting || (this._visiting = new Set());
			if (visiting.has(node)) {
				return new Entity(this, node, [], node.value);
			}
			visiting.add(node);
			let entries = [];
			let cls = node.value;
			const module = node.doc ? node.doc.module : null;
			for (const child of node.children) {
				if (child.key === 'CopyOf') {
					const base = this.find(child.value, node.value, module);
					if (base) {
						entries = this.resolve(base.node).entries.slice();
					}
				} else {
					entries.push({ key: child.key, node: child });
				}
			}
			visiting.delete(node);
			const entity = new Entity(this, node, entries, cls);
			this.cache.set(node, entity);
			return entity;
		}

		missingCopyOf(node) {
			const copy = node.child('CopyOf');
			return copy && !this.find(copy.value, node.value, node.doc ? node.doc.module : null) ? copy.value : null;
		}

		// ---- editing ----
		//
		// A part is reached from an actor's block by a path of steps, each {key, index}: index counts that key's entries in the
		// resolved parent (it only matters for list keys like AddAttachable). Edits never touch the game's own files (Data/): a part the
		// actor inherits is copied into the actor's block and changed there (an inherited listed attachment by adopting the lines of
		// the presets above it), so other units that share it are untouched.

		// Returns the block where a part's own properties can be written, copying inherited blocks in as needed, or
		// {readOnly: reason} if the part can't be changed from this actor. With dryRun nothing is changed.
		ownBlock(actorNode, path, dryRun) {
			if (this.isProtected(actorNode.doc)) {
				return { readOnly: this.lockMessage(actorNode.doc) };
			}
			let anchor = actorNode;
			// In a dry run, an inherited block that would be copied in stands for its copy: its lines count as the copy's own.
			let virtual = false;
			for (const step of path) {
				const entry = this.resolve(anchor).all(step.key)[step.index || 0];
				if (!entry) {
					return { readOnly: 'This part no longer exists.' };
				}
				if (entry.node.parent === anchor) {
					anchor = entry.node;
					continue;
				}
				if (dryRun) {
					anchor = entry.node;
					virtual = true;
					continue;
				}
				if (LIST_KEYS.has(step.key)) {
					// A listed attachment is added once per block that lists it, so copying it in would double it. Instead this block
					// takes over the lines of the preset it copies (see adopt), which makes the attachment one of its own lines.
					const own = this.adopt(anchor, entry.node);
					if (!own) {
						return { readOnly: 'This attachment couldn\'t be copied into this unit.' };
					}
					anchor = own;
					continue;
				}
				// A single part (Head, FGArm...) inherited through CopyOf: copy its block into this one. The game reads the copy exactly
				// as it read the original, so nothing changes until a value in it is edited.
				anchor = anchor.add(this.inheritedCopy(entry.node, anchor.doc.module), anchor.children.length);
				this.invalidate();
			}
			if (!virtual && this.isProtected(anchor.doc)) {
				return { readOnly: this.lockMessage(anchor.doc) };
			}
			return { node: anchor };
		}

		canEdit(actorNode, path) {
			const result = this.ownBlock(actorNode, path, true);
			return result.readOnly || null;
		}

		// Makes an inherited line one of the block's own: the lines of the preset the block copies are written into the block, which
		// then copies that preset's own base instead, a level at a time until the line is reached. The game builds the same unit as
		// before, and other units that copy those presets are untouched. Returns the block's copy of the line, or null.
		adopt(block, line) {
			for (let level = 0; level < 32; level++) {
				const copyOf = block.child('CopyOf');
				const base = copyOf && this.find(copyOf.value, block.value, block.doc.module);
				if (!base || base.node === block) {
					return null;
				}
				const baseNode = base.node;
				const fromModule = baseNode.doc.module;
				const toModule = block.doc.module;
				let at = block.children.indexOf(copyOf);
				const baseCopyOf = baseNode.child('CopyOf');
				if (baseCopyOf) {
					copyOf.setValue(this.reference(baseCopyOf.value, fromModule, toModule));
				} else {
					copyOf.remove();
					at--;
				}
				let found = null;
				for (const child of baseNode.children) {
					if (child.key === 'CopyOf' || child.key === 'PresetName') {
						continue;
					}
					const copy = child.children.length ? this.inheritedCopy(child, toModule) : child.clone();
					this.fixReferences(copy, fromModule, toModule);
					block.add(copy, ++at);
					if (child === line) {
						found = copy;
					}
				}
				this.invalidate();
				if (found) {
					return found;
				}
			}
			return null;
		}

		// Rewrites every CopyOf in a copied block so it still names the same presets from its new module.
		fixReferences(node, fromModule, toModule) {
			const stack = [node];
			while (stack.length) {
				const n = stack.pop();
				if (n.key === 'CopyOf' && n.value) {
					n.setValue(this.reference(n.value, fromModule, toModule));
				}
				stack.push(...n.children);
			}
		}

		// A copy of an inherited block for another block to own.
		inheritedCopy(node, toModule) {
			const name = node.child('PresetName');
			if (name && !node.child('CopyOf')) {
				// A fully written-out named preset: point at it rather than copying all of it.
				const copy = new MS.IniNode(node.key, node.value);
				copy.add('CopyOf', this.reference(name.value, node.doc.module, toModule));
				return copy;
			}
			const copy = node.clone();
			const copyName = copy.child('PresetName');
			if (copyName) {
				copyName.remove();
			}
			const copyOf = copy.child('CopyOf');
			if (copyOf && node.doc) {
				copyOf.value = this.reference(copyOf.value, node.doc.module, toModule);
			}
			return copy;
		}

		// How a block in "toModule" names a preset that "fromModule" referred to by "name".
		reference(name, fromModule, toModule) {
			if (name.includes('/') || !fromModule || fromModule.toLowerCase() === (toModule || '').toLowerCase()) {
				return name;
			}
			const preset = this.find(name, null, fromModule);
			if (preset && !this.fs.isOfficial(preset.module) && preset.module.toLowerCase() !== (toModule || '').toLowerCase()) {
				return preset.module + '/' + preset.name;
			}
			return name;
		}

		// Throws away a file's unsaved changes: back to what's on disk, or gone if it was never saved (and its mod with it, if that's now empty).
		discard(doc) {
			if (!doc.isNew && doc.savedText !== null) {
				doc.reset(doc.savedText);
				doc.dirty = false;
				return;
			}
			this.docs.delete(doc.path.toLowerCase());
			const module = this.moduleOf(doc.path);
			if (module) {
				module.docs = module.docs.filter((d) => d !== doc);
				if (!module.docs.length) {
					this.modules = this.modules.filter((m) => m !== module);
				}
			}
		}

		// ---- new files and mods ----

		docFor(path) {
			return this.docs.get(path.toLowerCase()) || null;
		}

		// A new INI file in a mod (made if needed), included from the mod's Index.ini: at the end, or just before `before` (a file that
		// will use presets from the new one, which the game must have read first).
		async newDoc(path, before) {
			const moduleName = path.split('/')[0];
			let module = this.moduleOf(path);
			if (!module) {
				module = { name: moduleName, home: 'Mods', official: false, docs: [], title: moduleName.replace(/\.rte$/i, '') };
				this.modules.push(module);
				const index = new MS.IniDoc(moduleName + '/Index.ini');
				const dm = index.root.add('DataModule', '');
				dm.add('ModuleName', module.title);
				dm.add('SupportedGameVersion', '6.0.0');
				dm.add('Version', '1');
				dm.add('Description', 'Made with Cortex Mod Studio.');
				this.adoptDoc(index);
				module.docs.push(index);
				index.markDirty();
			}
			const doc = new MS.IniDoc(path);
			this.adoptDoc(doc);
			module.docs.push(doc);
			doc.markDirty();
			// Include it from Index.ini.
			const index = this.docFor(moduleName + '/Index.ini');
			if (index) {
				const dm = index.root.children.find((n) => n.key === 'DataModule') || index.root;
				if (!dm.children.some((c) => c.key === 'IncludeFile' && c.value.toLowerCase() === path.toLowerCase())) {
					const at = before ? dm.children.findIndex((c) => c.key === 'IncludeFile' && c.value.toLowerCase() === before.toLowerCase()) : -1;
					dm.add('IncludeFile', path, at >= 0 ? at : undefined);
				}
			}
			return doc;
		}
	}

	const LIST_KEYS = new Set(['AddAttachable', 'AddAEmitter', 'AddEmitter', 'AddGib', 'AddInventory', 'ScriptPath', 'AddToGroup', 'AddWound', 'AddPieSlice']);

	MS.Library = Library;
	MS.Entity = Entity;
	MS.LIST_KEYS = LIST_KEYS;
})();
