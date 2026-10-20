// Cortex Mod Studio: reading and writing the game's folder from the browser, with nothing to install.
//
// Started from Mod Studio.bat or the launcher, the page is served by Start-ModStudio.ps1, which reads and saves the game folder
// for it. Opened straight from disk, a page can only read a folder the user picks; saved files are then collected and downloaded
// as one zip to unpack over the game folder. (Chrome and Edge's own folder access refuses .ini files, so it can't be used.)
//
// Paths are written the way the game's INI files write them: "Ronin.rte/Actors/Infantry/RoninLight/Torso000.png". The module
// (the first part) is looked up in Data/ first, then Mods/, as PresetMan::GetFullModulePath does.
'use strict';
window.MS = window.MS || {};

(function () {
	const lower = (s) => s.toLowerCase();

	class GameFS {
		constructor() {
			this.server = null; // Token for Start-ModStudio.ps1 (served mode).
			this.texts = new Map(); // Served mode: lowercased path -> text of every .ini, read in one go.
			this.root = null; // FileSystemDirectoryHandle of the game folder (writable mode).
			this.files = null; // Map of lowercased relative path -> File (read-only mode).
			this.dirCache = new Map(); // Lowercased relative directory path -> Map(lowercased name -> handle).
			this.pendingWrites = new Map(); // Read-only mode: path -> {bytes} waiting to be downloaded.
			this.moduleHome = new Map(); // Lowercased module name -> "Data" or "Mods".
			this.name = '';
		}

		// The browsers' own folder access hides .ini files and refuses to open them ("Name is not allowed"), so it is never used.
		static get canWrite() {
			return false;
		}

		// The token Start-ModStudio.ps1 put in the page's address, when the page came from it.
		static get serverToken() {
			const match = /[?&]token=([0-9a-f]+)/i.exec(location.search);
			return /^https?:$/.test(location.protocol) && match ? match[1] : null;
		}

		get writable() {
			return !!this.root || !!this.server;
		}

		async api(method, action, path, body) {
			const response = await fetch('/api/' + action + '?path=' + encodeURIComponent(path || ''), {
				method,
				body,
				headers: { 'X-ModStudio-Token': this.server },
				cache: 'no-store',
			});
			if (response.status === 404 && action === 'file') {
				return null;
			}
			if (!response.ok) {
				throw new Error('Mod Studio\'s server said: ' + (await response.text()));
			}
			return response;
		}

		// Served mode: the game folder comes from Start-ModStudio.ps1.
		async useServer(token) {
			this.server = token;
			this.root = null;
			this.files = null;
			this.dirCache.clear();
			let info;
			try {
				info = await (await this.api('GET', 'info')).json();
			} catch (e) {
				throw new Error('Mod Studio\'s window has closed. Start Mod Studio.bat (or the launcher\'s Mod Studio button) again.');
			}
			this.name = info.name;
			this.texts = parseBundle(new Uint8Array(await (await this.api('GET', 'inis')).arrayBuffer()));
			await this.findModules();
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
			if (this.server) {
				for (const entry of await (await this.api('GET', 'list', diskDir)).json()) {
					map.set(lower(entry.name), entry);
				}
			} else if (this.root) {
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

		// Asks the browser for each folder by name, for when a listing didn't have it. Names are tried as written, then as listed.
		async walkToFile(diskPath) {
			const parts = diskPath.split('/').filter(Boolean);
			const fileName = parts.pop();
			let dir = this.root;
			let soFar = '';
			try {
				for (const part of parts) {
					const listed = (await this.dirMap(soFar)).get(lower(part));
					dir = await dir.getDirectoryHandle(listed ? listed.name : part);
					soFar = soFar ? soFar + '/' + part : part;
				}
				const listed = (await this.dirMap(soFar)).get(lower(fileName));
				return await dir.getFileHandle(listed ? listed.name : fileName);
			} catch (e) {
				return null;
			}
		}

		async exists(modulePath) {
			const disk = this.toDiskPath(modulePath);
			if (this.pendingWrites.has(lower(disk))) {
				return true;
			}
			return !!(await this.fileHandle(disk)) || (!!this.files && this.files.has(lower(disk)));
		}

		async readFile(modulePath) {
			return this.readDiskFile(this.toDiskPath(modulePath));
		}

		// A file by its path from the game folder ("Userdata/BattlePresets/Ketanot Hills.txt"), as readFile.
		async readDiskFile(disk) {
			const pending = this.pendingWrites.get(lower(disk));
			if (pending) {
				return new Blob([pending.bytes]);
			}
			if (this.server) {
				const response = await this.api('GET', 'file', disk);
				if (!response) {
					throw new Error('File not found: ' + disk);
				}
				const modified = Number(response.headers.get('X-Last-Modified')) || 0;
				return new File([await response.arrayBuffer()], disk.split('/').pop(), { lastModified: modified });
			}
			if (this.root) {
				let handle = await this.fileHandle(disk);
				if (!handle) {
					handle = await this.walkToFile(disk);
				}
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
			const text = this.texts.get(lower(this.toDiskPath(modulePath)));
			if (text !== undefined) {
				return text;
			}
			const file = await this.readFile(modulePath);
			return await file.text();
		}

		async readDiskText(disk) {
			return await (await this.readDiskFile(disk)).text();
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
			// Studio's requests to a game linked to it (ModStudio/Preview.ini...): a Community Edition game never reads them, so its
			// folder isn't given them
			if (MS.target && MS.target.classic() && /^modstudio\//i.test(disk)) {
				return;
			}
			const bytes = typeof data === 'string' ? new TextEncoder().encode(data) : data;
			if (this.server) {
				await this.api('PUT', 'file', disk, bytes);
				this.texts.delete(lower(disk));
				const parts = disk.split('/').filter(Boolean);
				const fileName = parts.pop();
				const folder = parts.join('/');
				if (this.dirCache.has(lower(folder))) {
					this.dirCache.get(lower(folder)).set(lower(fileName), { kind: 'file', name: fileName });
				}
				for (let i = 0; i < parts.length; i++) {
					const parent = lower(parts.slice(0, i).join('/'));
					if (this.dirCache.has(parent) && !this.dirCache.get(parent).has(lower(parts[i]))) {
						this.dirCache.get(parent).set(lower(parts[i]), { kind: 'directory', name: parts[i] });
						this.dirCache.delete(lower(parts.slice(0, i + 1).join('/')));
					}
				}
				if (parts.length >= 2 && !this.moduleHome.has(lower(parts[1])) && lower(parts[1]).endsWith('.rte')) {
					this.moduleHome.set(lower(parts[1]), { home: parts[0], name: parts[1] });
				}
				return;
			}
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

		// Every .lua in Data and Mods, for the script tools: Map(lowercased disk path -> {path, text}). Served mode only; null otherwise.
		async luaBundle() {
			if (!this.server) {
				return null;
			}
			const bytes = new Uint8Array(await (await this.api('GET', 'luas')).arrayBuffer());
			return parseBundle(bytes, true);
		}

		// Every file under a folder (a disk path like "Mods/MyMod.rte"), as [{path, size}] with paths from that folder. Served mode only.
		async tree(diskDir) {
			if (!this.server) {
				const prefix = diskDir.toLowerCase().replace(/\/$/, '') + '/';
				return this.files ? [...this.files.keys()].filter((p) => p.startsWith(prefix)).map((p) => ({ path: this.files.get(p).webkitRelativePath.split('/').slice(1).join('/').slice(prefix.length), size: this.files.get(p).size })) : [];
			}
			return await (await this.api('GET', 'tree', diskDir)).json();
		}

		// Every file in Data and Mods as module paths ("Base.rte/Devices/x.png"), from one request; null when the server is older or
		// there's no server.
		async allFiles() {
			if (!this.server) {
				return this.files ? [...this.files.values()].map((f) => f.webkitRelativePath.split('/').slice(2).join('/')) : null;
			}
			const response = await fetch('/api/files?path=', { headers: { 'X-ModStudio-Token': this.server }, cache: 'no-store' });
			if (!response.ok) {
				return null;
			}
			return (await response.text()).split('\n').filter(Boolean).map((line) => line.slice(0, line.indexOf('\t')).replace(/^(Data|Mods)\//i, ''));
		}

		// The part of a growing file (ModStudio/Console.log) after byte "from": {text, size, from}. size < from means it started over.
		async tail(disk, from) {
			const response = await fetch('/api/tail?path=' + encodeURIComponent(disk) + '&from=' + from, { headers: { 'X-ModStudio-Token': this.server }, cache: 'no-store' });
			if (!response.ok) {
				throw new Error(await response.text());
			}
			return { text: new TextDecoder().decode(await response.arrayBuffer()), size: Number(response.headers.get('X-Size')) || 0, from: Number(response.headers.get('X-From')) || 0 };
		}

		// Deletes a file in Mods (the server refuses anything else).
		async deleteFile(modulePath) {
			const disk = this.toDiskPath(modulePath);
			if (!this.server) {
				throw new Error('Deleting needs Studio started from Mod Studio.bat.');
			}
			await this.api('POST', 'delete', disk);
			this.texts.delete(lower(disk));
			const slash = disk.lastIndexOf('/');
			const folder = this.dirCache.get(lower(disk.slice(0, slash)));
			if (folder) {
				folder.delete(lower(disk.slice(slash + 1)));
			}
		}

		// Forgets cached folder listings, so the next look reads the disk again (after the game or another program changed files).
		forgetListings() {
			this.dirCache.clear();
		}

		// Read-only mode: everything saved so far, as a zip to unpack over the game folder.
		pendingZip() {
			const entries = [...this.pendingWrites.values()].map((w) => ({ path: w.path, bytes: w.bytes }));
			return entries.length ? makeZip(entries) : null;
		}
	}

	// Start-ModStudio.ps1's /api/inis answer: for each file a "path<TAB>byte count<LF>" line, then its bytes. -> Map(lowercased path -> text)
	function parseBundle(bytes, keepPaths) {
		const texts = new Map();
		const decoder = new TextDecoder();
		let at = 0;
		while (at < bytes.length) {
			const lineEnd = bytes.indexOf(10, at);
			if (lineEnd < 0) {
				break;
			}
			const header = decoder.decode(bytes.subarray(at, lineEnd));
			const tab = header.lastIndexOf('\t');
			const size = Number(header.slice(tab + 1));
			const start = lineEnd + 1;
			const text = decoder.decode(bytes.subarray(start, start + size));
			texts.set(lower(header.slice(0, tab)), keepPaths ? { path: header.slice(0, tab), text } : text);
			at = start + size;
		}
		return texts;
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
	MS.makeZip = makeZip;
})();
