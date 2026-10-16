// Cortex Mod Studio: the game's INI format, read and written back without losing comments or layout.
//
// The game reads INI as "Key = Value" lines nested by tab indentation (Source/System/Reader.cpp): "//" comments run to the end of
// the line, "/* */" comments can span lines and nest, and a value stops at the first tab. Every line a modder wrote is kept: a node
// that hasn't been changed is written back exactly as it was read, comments and blank lines before it included, and only the nodes
// Studio changed or added are written fresh.
'use strict';
window.MS = window.MS || {};

(function () {
	class IniNode {
		constructor(key, value) {
			this.key = key;
			this.value = value;
			this.children = [];
			this.parent = null;
			this.doc = null;
			this.lead = []; // Comment and blank lines written just before this node, kept as they were.
			this.raw = null; // The original line, written back as it was while the node is unchanged.
			this.comment = ''; // A comment after the value on the same line.
			this.line = 0; // Line number in the file it was read from (1-based), 0 for new nodes.
		}

		get depth() {
			let depth = -1;
			for (let node = this.parent; node; node = node.parent) {
				depth++;
			}
			return depth;
		}

		// The node a preset's properties hang off: its top-level "AddActor = AHuman" style line.
		get topLevel() {
			let node = this;
			while (node.parent && node.parent.parent) {
				node = node.parent;
			}
			return node;
		}

		child(key) {
			for (let i = this.children.length - 1; i >= 0; i--) {
				if (this.children[i].key === key) {
					return this.children[i];
				}
			}
			return null;
		}

		childValue(key, fallback) {
			const node = this.child(key);
			return node ? node.value : fallback;
		}

		isWithin(ancestor) {
			for (let node = this; node; node = node.parent) {
				if (node === ancestor) {
					return true;
				}
			}
			return false;
		}

		touch() {
			this.raw = null;
			if (this.doc) {
				this.doc.markDirty();
			}
		}

		willChange() {
			if (this.doc && this.doc.onWillChange) {
				this.doc.onWillChange(this.doc);
			}
		}

		setValue(value) {
			value = String(value);
			if (value !== this.value) {
				this.willChange();
				this.value = value;
				this.touch();
			}
		}

		// Adds a child; with no index it goes last. Returns the new node.
		add(key, value, index) {
			const node = key instanceof IniNode ? key : new IniNode(key, String(value));
			this.willChange();
			node.parent = this;
			setDoc(node, this.doc);
			if (index === undefined || index < 0 || index >= this.children.length) {
				this.children.push(node);
			} else {
				this.children.splice(index, 0, node);
			}
			if (this.doc) {
				this.doc.markDirty();
			}
			return node;
		}

		remove() {
			if (!this.parent) {
				return;
			}
			this.willChange();
			const siblings = this.parent.children;
			const index = siblings.indexOf(this);
			if (index >= 0) {
				siblings.splice(index, 1);
				// Comments that sat above a removed node stay where they were.
				if (this.lead.length && siblings[index]) {
					siblings[index].lead = this.lead.concat(siblings[index].lead);
				}
			}
			if (this.doc) {
				this.doc.markDirty();
			}
			this.parent = null;
		}

		// A deep copy, written fresh (its indentation will differ from the original's).
		clone() {
			const copy = new IniNode(this.key, this.value);
			copy.comment = this.comment;
			for (const child of this.children) {
				const childCopy = child.clone();
				childCopy.parent = copy;
				copy.children.push(childCopy);
			}
			return copy;
		}

		// Sets (or adds) a "Key = Value" child.
		set(key, value) {
			const existing = this.child(key);
			if (existing) {
				existing.setValue(value);
				return existing;
			}
			return this.add(key, value, this.insertIndexFor(key));
		}

		// Where a new property goes: after CopyOf and PresetName, which must come first, otherwise last.
		insertIndexFor(key) {
			if (key === 'CopyOf') {
				return 0;
			}
			if (key === 'PresetName') {
				return this.children.length && this.children[0].key === 'CopyOf' ? 1 : 0;
			}
			return this.children.length;
		}

		// Reads "Key = Vector" with X and Y children.
		vector(key) {
			const node = key ? this.child(key) : this;
			return node ? readVector(node) : null;
		}

		setVector(key, x, y) {
			let node = this.child(key);
			if (!node) {
				node = this.add(key, 'Vector', this.insertIndexFor(key));
			} else if (node.value !== 'Vector') {
				node.setValue('Vector');
			}
			for (const [axis, value] of [['X', x], ['Y', y]]) {
				const axisNode = node.child(axis);
				if (axisNode) {
					axisNode.setValue(formatNumber(value));
				} else {
					node.add(axis, formatNumber(value), axis === 'X' ? 0 : undefined);
				}
			}
			return node;
		}

		path() {
			const parts = [];
			for (let node = this; node && node.parent; node = node.parent) {
				parts.unshift(node.key);
			}
			return parts.join(' > ');
		}
	}

	function setDoc(node, doc) {
		node.doc = doc;
		for (const child of node.children) {
			setDoc(child, doc);
		}
	}

	function readVector(node) {
		let x = 0;
		let y = 0;
		for (const child of node.children) {
			if (child.key === 'X') {
				x = parseFloat(child.value) || 0;
			} else if (child.key === 'Y') {
				y = parseFloat(child.value) || 0;
			}
		}
		return { x, y };
	}

	function formatNumber(value) {
		const rounded = Math.round(Number(value) * 1000) / 1000;
		return Object.is(rounded, -0) ? '0' : String(rounded);
	}

	class IniDoc {
		constructor(path, text) {
			this.path = path; // Module path, e.g. "Ronin.rte/Actors.ini".
			this.root = new IniNode('', '');
			this.root.doc = this;
			this.tail = [];
			this.eol = '\r\n';
			this.dirty = false;
			this.isNew = text === undefined;
			this.problems = [];
			this.onDirty = null;
			this.onWillChange = null;
			this.savedText = null; // What's on disk (as Studio would write it), to tell whether there is anything to save.
			if (text !== undefined) {
				this.parse(text);
				this.savedText = this.serialize();
			}
		}

		// Replaces the whole file's contents (undo and redo).
		reset(text) {
			this.root = new IniNode('', '');
			this.root.doc = this;
			this.tail = [];
			this.problems = [];
			this.parse(text);
			this.dirty = this.savedText === null || text !== this.savedText;
		}

		get module() {
			return this.path.split('/')[0];
		}

		markDirty() {
			if (!this.dirty) {
				this.dirty = true;
				if (this.onDirty) {
					this.onDirty(this);
				}
			}
		}

		parse(text) {
			if (text.charCodeAt(0) === 0xfeff) {
				text = text.slice(1);
			}
			this.eol = text.includes('\r\n') ? '\r\n' : '\n';
			const lines = text.split(/\r?\n/);
			if (lines.length && lines[lines.length - 1] === '') {
				lines.pop();
			}
			let pending = [];
			let blockDepth = 0;
			const stack = [{ node: this.root, indent: -1 }];

			lines.forEach((line, index) => {
				if (blockDepth > 0) {
					blockDepth = scanBlockComment(line, 0, blockDepth);
					pending.push(line);
					return;
				}
				let indent = 0;
				let pos = 0;
				let spaces = 0;
				while (pos < line.length && (line[pos] === '\t' || line[pos] === ' ')) {
					if (line[pos] === '\t') {
						indent++;
					} else if (++spaces === 4) {
						indent++;
						spaces = 0;
					}
					pos++;
				}
				const content = line.slice(pos);
				if (content === '' || content.startsWith('//')) {
					pending.push(line);
					return;
				}
				if (content.startsWith('/*')) {
					blockDepth = scanBlockComment(content, 2, 1);
					pending.push(line);
					return;
				}
				// A data line. A value stops at a tab or a comment.
				let data = content;
				let comment = '';
				const lineComment = data.indexOf('//');
				const blockComment = data.indexOf('/*');
				const cut = [lineComment, blockComment].filter((n) => n >= 0).sort((a, b) => a - b)[0];
				if (cut !== undefined) {
					comment = data.slice(cut);
					data = data.slice(0, cut);
					if (cut === blockComment) {
						blockDepth = scanBlockComment(comment, 2, 1);
					}
				}
				const tab = data.indexOf('\t');
				if (tab >= 0) {
					data = data.slice(0, tab);
				}
				const equals = data.indexOf('=');
				const key = (equals >= 0 ? data.slice(0, equals) : data).trim();
				const value = equals >= 0 ? data.slice(equals + 1).trim() : '';
				if (equals < 0) {
					this.problems.push({ line: index + 1, text: 'No "=" on this line: "' + content.trim() + '"' });
				}

				const node = new IniNode(key, value);
				node.raw = line;
				node.comment = comment.trim();
				node.lead = pending;
				node.line = index + 1;
				node.doc = this;
				pending = [];

				while (stack.length > 1 && stack[stack.length - 1].indent >= indent) {
					stack.pop();
				}
				const parentEntry = stack[stack.length - 1];
				if (indent > parentEntry.indent + 1) {
					this.problems.push({ line: index + 1, text: 'Indented too far (the game stops reading here)' });
				}
				node.parent = parentEntry.node;
				parentEntry.node.children.push(node);
				stack.push({ node, indent });
			});
			this.tail = pending;
		}

		serialize() {
			const out = [];
			const write = (node, depth) => {
				for (const line of node.lead) {
					out.push(line);
				}
				if (node.raw !== null) {
					out.push(node.raw);
				} else {
					// A block with no value (DataModule) is written as its key alone: the game refuses "DataModule = ".
					let line = '\t'.repeat(depth) + node.key + (node.value === '' && node.children.length ? '' : ' = ' + node.value);
					if (node.comment) {
						line += ' ' + node.comment;
					}
					out.push(line);
				}
				for (const child of node.children) {
					write(child, depth + 1);
				}
			};
			for (const node of this.root.children) {
				write(node, 0);
			}
			for (const line of this.tail) {
				out.push(line);
			}
			return out.join(this.eol) + this.eol;
		}

		// Every node in the file, depth first.
		*walk(start) {
			const stack = [...(start || this.root).children].reverse();
			while (stack.length) {
				const node = stack.pop();
				yield node;
				for (let i = node.children.length - 1; i >= 0; i--) {
					stack.push(node.children[i]);
				}
			}
		}

		// Adds a top-level block, with a blank line before it so the file stays readable.
		addTopLevel(key, value) {
			const node = new IniNode(key, value);
			if (this.root.children.length) {
				node.lead = ['', ''];
			}
			return this.root.add(node);
		}
	}

	// Returns how many block comments are still open after scanning the line from "pos".
	function scanBlockComment(text, pos, depth) {
		for (let i = pos; i < text.length - 1 && depth > 0; i++) {
			if (text[i] === '/' && text[i + 1] === '*') {
				depth++;
				i++;
			} else if (text[i] === '*' && text[i + 1] === '/') {
				depth--;
				i++;
			}
		}
		return depth;
	}

	MS.IniNode = IniNode;
	MS.IniDoc = IniDoc;
	MS.readVector = readVector;
	MS.formatNumber = formatNumber;
})();
