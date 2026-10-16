// Cortex Mod Studio: reading and writing the game's folder from the browser, with nothing to install.
//
// In Chrome and Edge the page asks for the game folder once (the File System Access API) and then reads and saves files in place.
// Other browsers can only read a folder the user picks; there, saved files are collected and downloaded as one zip to unpack over
// the game folder.
//
// Paths are written the way the game's INI files write them: "Ronin.rte/Actors/Infantry/RoninLight/Torso000.png". The module
// (the first part) is looked up in Data/ first, then Mods/, as PresetMan::GetFullModulePath does.
'use strict';
window.MS = window.MS || {};

(function () {
	const lower = (s) => s.toLowerCase();

	class GameFS {
		constructor() {
			this.root = null; // FileSystemDirectoryHandle of the game folder (writable mode).
			this.files = null; // Map of lowercased relative path -> File (read-only mode).
			this.dirCache = new Map(); // Lowercased relative directory path -> Map(lowercased name -> handle).
			this.pendingWrites = new Map(); // Read-only mode: path -> {bytes} waiting to be downloaded.
			this.moduleHome = new Map(); // Lowercased module name -> "Data" or "Mods".
			this.name = '';
		}

		static get canWrite() {
			return typeof window.showDirectoryPicker === 'function';
		}

		get writable() {
			return !!this.root;
		}

		// Writable mode: ask for the folder. Throws if the user cancels.
		async pickFolder() {
			const handle = await window.showDirectoryPicker({ id: 'cortex-game-folder', mode: 'readwrite' });
			await this.useHandle(handle);
		}

		async useHandle(handle) {
			if (handle.queryPermission && (await handle.queryPermission({ mode: 'readwrite' })) !== 'granted') {
				if ((await handle.requestPermission({ mode: 'readwrite' })) !== 'granted') {
					throw new Error('Studio needs permission to read and save in the game folder.');
				}
			}
			this.root = handle;
			this.files = null;
			this.dirCache.clear();
			this.name = handle.name;
			await this.findModules();
		}

		// Read-only mode: files from <input type=file webkitdirectory>.
		async useFileList(fileList) {
			this.root = null;
			this.files = new Map();
			this.dirCache.clear();
			const list = Array.from(fileList);
			if (!list.length) {
				throw new Error('That folder is empty.');
			}
			// The picked folder's own name is the first part of every path; drop it.
			const first = list[0].webkitRelativePath.split('/')[0];
			this.name = first;
			for (const file of list) {
				const parts = file.webkitRelativePath.split('/');
				parts.shift();
				this.files.set(lower(parts.join('/')), file);
			}
			await this.findModules();
		}

		async findModules() {
			this.moduleHome.clear();
			const data = await this.listDir('Data');
			if (!data.some((e) => lower(e.name) === 'base.rte')) {
				throw new Error('That isn\'t the game folder: it has no Data/Base.rte. Pick the folder with the game\'s program in it.');
			}
			for (const home of ['Data', 'Mods']) {
				for (const entry of await this.listDir(home)) {
					if (entry.kind === 'directory' && lower(entry.name).endsWith('.rte') && !this.moduleHome.has(lower(entry.name))) {
						this.moduleHome.set(lower(entry.name), { home, name: entry.name });
					}
				}
			}
		}

		modules() {
			return [...this.moduleHome.values()];
		}

		isOfficial(modulePath) {
			const info = this.moduleHome.get(lower(modulePath.split('/')[0]));
			return !!info && info.home === 'Data';
		}

		// "Ronin.rte/Actors.ini" -> "Data/Ronin.rte/Actors.ini"; also accepts paths that start with "Mods/" or "Data/".
		toDiskPath(modulePath) {
			let path = modulePath.replace(/\\/g, '/').replace(/^\.\//, '').replace(/\/+/g, '/');
			const top = lower(path.split('/')[0]);
			if (top === 'data' || top === 'mods') {
				return path;
			}
			const info = this.moduleHome.get(top);
			return (info ? info.home : 'Mods') + '/' + path;
		}

		// ---- directory access ----

		async dirMap(diskDir) {
			diskDir = diskDir.split('/').filter(Boolean).join('/');
			const key = lower(diskDir);
			if (this.dirCache.has(key)) {
				return this.dirCache.get(key);
			}
			const map = new Map();
			if (this.root) {
				let handle = this.root;
				const parts = diskDir.split('/').filter(Boolean);
				if (parts.length) {
					const parentMap = await this.dirMap(parts.slice(0, -1).join('/'));
					handle = parentMap.get(lower(parts[parts.length - 1]));
					if (!handle || handle.kind !== 'directory') {
						this.dirCache.set(key, map);
						return map;
					}
				}
				for await (const entry of handle.values()) {
					map.set(lower(entry.name), entry);
				}
			} else if (this.files) {
				const prefix = key ? key + '/' : '';
				for (const path of this.files.keys()) {
					if (path.startsWith(prefix)) {
						const rest = path.slice(prefix.length);
						const slash = rest.indexOf('/');
						const name = slash >= 0 ? rest.slice(0, slash) : rest;
						if (!map.has(name)) {
							const original = this.files.get(path).webkitRelativePath.split('/').slice(1).join('/').slice(prefix.length);
							map.set(name, { kind: slash >= 0 ? 'directory' : 'file', name: slash >= 0 ? original.slice(0, original.indexOf('/')) : original });
						}
					}
				}
			}
			this.dirCache.set(key, map);
			return map;
		}

		async listDir(diskDir) {
			return [...(await this.dirMap(diskDir)).values()].map((e) => ({ name: e.name, kind: e.kind }));
		}

		async fileHandle(diskPath) {
			const slash = diskPath.lastIndexOf('/');
			const map = await this.dirMap(slash >= 0 ? diskPath.slice(0, slash) : '');
			const entry = map.get(lower(diskPath.slice(slash + 1)));
			return entry && entry.kind === 'file' ? entry : null;
		}

		async exists(modulePath) {
			const disk = this.toDiskPath(modulePath);
			if (this.pendingWrites.has(lower(disk))) {
				return true;
			}
			return !!(await this.fileHandle(disk)) || (!!this.files && this.files.has(lower(disk)));
		}

		async readFile(modulePath) {
			const disk = this.toDiskPath(modulePath);
			const pending = this.pendingWrites.get(lower(disk));
			if (pending) {
				return new Blob([pending.bytes]);
			}
			if (this.root) {
				const handle = await this.fileHandle(disk);
				if (!handle) {
					throw new Error('File not found: ' + disk);
				}
				return await handle.getFile();
			}
			const file = this.files && this.files.get(lower(disk));
			if (!file) {
				throw new Error('File not found: ' + disk);
			}
			return file;
		}

		async readText(modulePath) {
			const file = await this.readFile(modulePath);
			return await file.text();
		}

		async readBytes(modulePath) {
			const file = await this.readFile(modulePath);
			return new Uint8Array(await file.arrayBuffer());
		}

		async lastModified(modulePath) {
			try {
				return (await this.readFile(modulePath)).lastModified || 0;
			} catch (e) {
				return 0;
			}
		}

		// Writes a file, making folders as needed. "data" is a string or Uint8Array.
		async writeFile(modulePath, data) {
			return this.writeDisk(this.toDiskPath(modulePath), data);
		}

		// Writes a file by its path from the game folder ("ModStudio/Preview.ini").
		async writeDisk(disk, data) {
			const bytes = typeof data === 'string' ? new TextEncoder().encode(data) : data;
			if (!this.root) {
				this.pendingWrites.set(lower(disk), { path: disk, bytes });
				return;
			}
			const parts = disk.split('/').filter(Boolean);
			const fileName = parts.pop();
			let dir = this.root;
			let soFar = '';
			for (const part of parts) {
				const map = await this.dirMap(soFar);
				const existing = map.get(lower(part));
				dir = existing && existing.kind === 'directory' ? existing : await dir.getDirectoryHandle(part, { create: true });
				if (!existing) {
					map.set(lower(part), dir);
				}
				soFar = soFar ? soFar + '/' + dir.name : dir.name;
			}
			const map = await this.dirMap(soFar);
			const existing = map.get(lower(fileName));
			const handle = existing && existing.kind === 'file' ? existing : await dir.getFileHandle(fileName, { create: true });
			map.set(lower(fileName), handle);
			const writable = await handle.createWritable();
			await writable.write(bytes);
			await writable.close();
			if (parts.length >= 2 && !this.moduleHome.has(lower(parts[1])) && lower(parts[1]).endsWith('.rte')) {
				this.moduleHome.set(lower(parts[1]), { home: parts[0], name: parts[1] });
			}
		}

		// Read-only mode: everything saved so far, as a zip to unpack over the game folder.
		pendingZip() {
			const entries = [...this.pendingWrites.values()].map((w) => ({ path: w.path, bytes: w.bytes }));
			return entries.length ? makeZip(entries) : null;
		}
	}

	// A zip with stored (uncompressed) entries: small enough to write by hand, readable by every unzip tool.
	function makeZip(entries) {
		const crcTable = new Uint32Array(256).map((_, n) => {
			let c = n;
			for (let k = 0; k < 8; k++) {
				c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
			}
			return c >>> 0;
		});
		const crc32 = (bytes) => {
			let c = 0xffffffff;
			for (let i = 0; i < bytes.length; i++) {
				c = crcTable[(c ^ bytes[i]) & 0xff] ^ (c >>> 8);
			}
			return (c ^ 0xffffffff) >>> 0;
		};
		const chunks = [];
		const central = [];
		let offset = 0;
		const enc = new TextEncoder();
		for (const entry of entries) {
			const name = enc.encode(entry.path);
			const crc = crc32(entry.bytes);
			const local = new DataView(new ArrayBuffer(30));
			local.setUint32(0, 0x04034b50, true);
			local.setUint16(4, 20, true);
			local.setUint16(6, 0x0800, true);
			local.setUint32(14, crc, true);
			local.setUint32(18, entry.bytes.length, true);
			local.setUint32(22, entry.bytes.length, true);
			local.setUint16(26, name.length, true);
			chunks.push(new Uint8Array(local.buffer), name, entry.bytes);
			const dir = new DataView(new ArrayBuffer(46));
			dir.setUint32(0, 0x02014b50, true);
			dir.setUint16(4, 20, true);
			dir.setUint16(6, 20, true);
			dir.setUint16(8, 0x0800, true);
			dir.setUint32(16, crc, true);
			dir.setUint32(20, entry.bytes.length, true);
			dir.setUint32(24, entry.bytes.length, true);
			dir.setUint16(28, name.length, true);
			dir.setUint32(42, offset, true);
			central.push(new Uint8Array(dir.buffer), name);
			offset += 30 + name.length + entry.bytes.length;
		}
		const centralSize = central.reduce((n, c) => n + c.length, 0);
		const end = new DataView(new ArrayBuffer(22));
		end.setUint32(0, 0x06054b50, true);
		end.setUint16(8, entries.length, true);
		end.setUint16(10, entries.length, true);
		end.setUint32(12, centralSize, true);
		end.setUint32(16, offset, true);
		return new Blob([...chunks, ...central, new Uint8Array(end.buffer)], { type: 'application/zip' });
	}

	MS.GameFS = GameFS;
})();
