// Cortex Mod Studio: sprites, the game palette, and writing new sprites the game can load.
//
// The game's sprites are 8-bit palette images (PNG or BMP) and it uses their colour numbers with its own palette
// (Data/Base.rte/palette.bmp), whatever colours the file itself lists; number 0 is see-through. So to show a sprite exactly as the
// game will, Studio reads the numbers itself and colours them from the game palette. Images that aren't palette images are shown
// as the browser decodes them.
//
// Animation frames are numbered files: "Torso.png" with FrameCount = 3 means Torso000.png, Torso001.png and Torso002.png
// (ContentFile::GetAsAnimation).
'use strict';
window.MS = window.MS || {};

(function () {
	class Images {
		constructor(fs) {
			this.fs = fs;
			this.palette = null; // Uint8Array(256 * 3)
			this.cache = new Map(); // lowercased path -> Promise<Sprite|null>
			this.missing = new Set();
		}

		async loadPalette() {
			try {
				const bytes = await this.fs.readBytes('Base.rte/palette.bmp');
				this.palette = readBmpPalette(bytes);
			} catch (e) {
				this.palette = null;
			}
			if (!this.palette) {
				// A grey ramp, so palette sprites still show something if the palette can't be read.
				this.palette = new Uint8Array(768);
				for (let i = 0; i < 256; i++) {
					this.palette.set([i, i, i], i * 3);
				}
			}
		}

		// Paths of a sprite's frames.
		static framePaths(path, frameCount) {
			if (!path) {
				return [];
			}
			if (!frameCount || frameCount <= 1) {
				return [path];
			}
			const dot = path.lastIndexOf('.');
			const stem = dot > path.lastIndexOf('/') ? path.slice(0, dot) : path;
			const ext = dot > path.lastIndexOf('/') ? path.slice(dot) : '.png';
			const paths = [];
			for (let i = 0; i < frameCount; i++) {
				paths.push(stem + String(i).padStart(3, '0') + ext);
			}
			return paths;
		}

		// A loaded frame: {canvas, width, height, indices (Uint8Array|null)}, or null if it can't be read.
		get(path) {
			const key = path.toLowerCase();
			if (!this.cache.has(key)) {
				this.cache.set(key, this.load(path));
			}
			return this.cache.get(key);
		}

		forget(path) {
			this.cache.delete(path.toLowerCase());
			this.missing.delete(path.toLowerCase());
		}

		forgetAll() {
			this.cache.clear();
			this.missing.clear();
		}

		async load(path) {
			let bytes;
			try {
				bytes = await this.fs.readBytes(path);
			} catch (e) {
				// The game tries the other extension when a file is missing.
				const alt = path.replace(/\.(png|bmp)$/i, (m) => (m.toLowerCase() === '.png' ? '.bmp' : '.png'));
				try {
					bytes = alt !== path ? await this.fs.readBytes(alt) : null;
				} catch (e2) {
					bytes = null;
				}
				if (!bytes) {
					this.missing.add(path.toLowerCase());
					return null;
				}
			}
			try {
				const indexed = isPng(bytes) ? await decodeIndexedPng(bytes) : isBmp(bytes) ? decodeIndexedBmp(bytes) : null;
				if (indexed) {
					return this.fromIndices(indexed.width, indexed.height, indexed.indices);
				}
				const bitmap = await createImageBitmap(new Blob([bytes]));
				const canvas = makeCanvas(bitmap.width, bitmap.height);
				const ctx = canvas.getContext('2d');
				ctx.drawImage(bitmap, 0, 0);
				// Magenta is the see-through colour in true-colour images.
				const image = ctx.getImageData(0, 0, canvas.width, canvas.height);
				const d = image.data;
				for (let i = 0; i < d.length; i += 4) {
					if (d[i] === 255 && d[i + 1] === 0 && d[i + 2] === 255) {
						d[i + 3] = 0;
					}
				}
				ctx.putImageData(image, 0, 0);
				return { canvas, width: canvas.width, height: canvas.height, indices: null };
			} catch (e) {
				this.missing.add(path.toLowerCase());
				return null;
			}
		}

		fromIndices(width, height, indices) {
			const canvas = makeCanvas(width, height);
			const ctx = canvas.getContext('2d');
			const image = ctx.createImageData(width, height);
			const d = image.data;
			const pal = this.palette;
			for (let i = 0; i < indices.length; i++) {
				const c = indices[i];
				if (c !== 0) {
					d[i * 4] = pal[c * 3];
					d[i * 4 + 1] = pal[c * 3 + 1];
					d[i * 4 + 2] = pal[c * 3 + 2];
					d[i * 4 + 3] = 255;
				}
			}
			ctx.putImageData(image, 0, 0);
			return { canvas, width, height, indices };
		}

		// ---- import ----

		// Turns any picture into game-palette colour numbers. Returns {width, height, indices, changed} where changed counts the
		// pixels whose colour isn't exactly in the palette.
		toPalette(imageData) {
			const { width, height, data } = imageData;
			const indices = new Uint8Array(width * height);
			const pal = this.palette;
			const memo = new Map();
			let changed = 0;
			for (let i = 0; i < width * height; i++) {
				const r = data[i * 4];
				const g = data[i * 4 + 1];
				const b = data[i * 4 + 2];
				const a = data[i * 4 + 3];
				if (a < 128 || (r === 255 && g === 0 && b === 255)) {
					continue;
				}
				const key = (r << 16) | (g << 8) | b;
				let best = memo.get(key);
				if (best === undefined) {
					let bestDist = Infinity;
					best = 1;
					// Colour 0 is see-through and isn't a choice for a visible pixel.
					for (let c = 1; c < 256; c++) {
						const dr = r - pal[c * 3];
						const dg = g - pal[c * 3 + 1];
						const db = b - pal[c * 3 + 2];
						// Weighted towards green, as the eye is.
						const dist = dr * dr * 3 + dg * dg * 4 + db * db * 2;
						if (dist < bestDist) {
							bestDist = dist;
							best = c;
						}
					}
					memo.set(key, best);
					if (bestDist > 0) {
						memo.set('c' + key, 1);
					}
				}
				if (memo.has('c' + key)) {
					changed++;
				}
				indices[i] = best;
			}
			return { width, height, indices, changed };
		}

		// An 8-bit palette PNG with the game palette, as the game's own sprites are.
		async encodePng(width, height, indices) {
			const raw = new Uint8Array((width + 1) * height);
			for (let y = 0; y < height; y++) {
				raw[y * (width + 1)] = 0;
				raw.set(indices.subarray(y * width, (y + 1) * width), y * (width + 1) + 1);
			}
			const idat = await deflate(raw);
			const ihdr = new Uint8Array(13);
			const view = new DataView(ihdr.buffer);
			view.setUint32(0, width);
			view.setUint32(4, height);
			ihdr[8] = 8; // bit depth
			ihdr[9] = 3; // palette colour
			const trns = new Uint8Array(1); // colour 0 see-through for other image programs
			const parts = [new Uint8Array([137, 80, 78, 71, 13, 10, 26, 10]), pngChunk('IHDR', ihdr), pngChunk('PLTE', this.palette), pngChunk('tRNS', trns), pngChunk('IDAT', idat), pngChunk('IEND', new Uint8Array(0))];
			const total = parts.reduce((n, p) => n + p.length, 0);
			const out = new Uint8Array(total);
			let offset = 0;
			for (const p of parts) {
				out.set(p, offset);
				offset += p.length;
			}
			return out;
		}
	}

	function makeCanvas(width, height) {
		const canvas = document.createElement('canvas');
		canvas.width = Math.max(1, width);
		canvas.height = Math.max(1, height);
		return canvas;
	}

	const isPng = (b) => b.length > 8 && b[0] === 137 && b[1] === 80 && b[2] === 78 && b[3] === 71;
	const isBmp = (b) => b.length > 54 && b[0] === 0x42 && b[1] === 0x4d;

	async function inflate(bytes) {
		const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('deflate'));
		return new Uint8Array(await new Response(stream).arrayBuffer());
	}

	async function deflate(bytes) {
		const stream = new Blob([bytes]).stream().pipeThrough(new CompressionStream('deflate'));
		return new Uint8Array(await new Response(stream).arrayBuffer());
	}

	// Decodes palette PNGs (any bit depth, not interlaced) to colour numbers; returns null for any other kind.
	async function decodeIndexedPng(bytes) {
		const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
		let pos = 8;
		let width = 0;
		let height = 0;
		let depth = 0;
		let colorType = 0;
		let interlace = 0;
		const idat = [];
		while (pos + 8 <= bytes.length) {
			const length = view.getUint32(pos);
			const type = String.fromCharCode(bytes[pos + 4], bytes[pos + 5], bytes[pos + 6], bytes[pos + 7]);
			const data = bytes.subarray(pos + 8, pos + 8 + length);
			if (type === 'IHDR') {
				width = view.getUint32(pos + 8);
				height = view.getUint32(pos + 12);
				depth = data[8];
				colorType = data[9];
				interlace = data[12];
				if (colorType !== 3 || interlace !== 0) {
					return null;
				}
			} else if (type === 'IDAT') {
				idat.push(data);
			} else if (type === 'IEND') {
				break;
			}
			pos += 12 + length;
		}
		if (!width || !idat.length) {
			return null;
		}
		const joined = new Uint8Array(idat.reduce((n, c) => n + c.length, 0));
		let offset = 0;
		for (const chunk of idat) {
			joined.set(chunk, offset);
			offset += chunk.length;
		}
		const raw = await inflate(joined);
		const stride = Math.ceil((width * depth) / 8);
		const bpp = 1;
		const out = new Uint8Array(stride * height);
		let prev = new Uint8Array(stride);
		for (let y = 0; y < height; y++) {
			const filter = raw[y * (stride + 1)];
			const line = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
			const cur = out.subarray(y * stride, (y + 1) * stride);
			for (let x = 0; x < stride; x++) {
				const a = x >= bpp ? cur[x - bpp] : 0;
				const b = prev[x];
				const c = x >= bpp ? prev[x - bpp] : 0;
				let v = line[x];
				if (filter === 1) {
					v += a;
				} else if (filter === 2) {
					v += b;
				} else if (filter === 3) {
					v += (a + b) >> 1;
				} else if (filter === 4) {
					const p = a + b - c;
					const pa = Math.abs(p - a);
					const pb = Math.abs(p - b);
					const pc = Math.abs(p - c);
					v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
				}
				cur[x] = v & 255;
			}
			prev = cur;
		}
		if (depth === 8) {
			return { width, height, indices: out };
		}
		const indices = new Uint8Array(width * height);
		const perByte = 8 / depth;
		const mask = (1 << depth) - 1;
		for (let y = 0; y < height; y++) {
			for (let x = 0; x < width; x++) {
				const byte = out[y * stride + Math.floor(x / perByte)];
				const shift = 8 - depth * ((x % perByte) + 1);
				indices[y * width + x] = (byte >> shift) & mask;
			}
		}
		return { width, height, indices };
	}

	// Decodes uncompressed 8-bit BMPs to colour numbers; returns null for any other kind.
	function decodeIndexedBmp(bytes) {
		const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
		const dataOffset = view.getUint32(10, true);
		const width = view.getInt32(18, true);
		const rawHeight = view.getInt32(22, true);
		const bits = view.getUint16(28, true);
		const compression = view.getUint32(30, true);
		if (bits !== 8 || compression !== 0) {
			return null;
		}
		const height = Math.abs(rawHeight);
		const stride = (width + 3) & ~3;
		const indices = new Uint8Array(width * height);
		for (let y = 0; y < height; y++) {
			const row = rawHeight > 0 ? height - 1 - y : y;
			indices.set(bytes.subarray(dataOffset + row * stride, dataOffset + row * stride + width), y * width);
		}
		return { width, height, indices };
	}

	function readBmpPalette(bytes) {
		if (!isBmp(bytes)) {
			return null;
		}
		const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
		const headerSize = view.getUint32(14, true);
		const start = 14 + headerSize;
		const palette = new Uint8Array(768);
		for (let i = 0; i < 256; i++) {
			palette[i * 3] = bytes[start + i * 4 + 2];
			palette[i * 3 + 1] = bytes[start + i * 4 + 1];
			palette[i * 3 + 2] = bytes[start + i * 4];
		}
		return palette;
	}

	const crcTable = new Uint32Array(256).map((_, n) => {
		let c = n;
		for (let k = 0; k < 8; k++) {
			c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
		}
		return c >>> 0;
	});

	function pngChunk(type, data) {
		const out = new Uint8Array(12 + data.length);
		const view = new DataView(out.buffer);
		view.setUint32(0, data.length);
		for (let i = 0; i < 4; i++) {
			out[4 + i] = type.charCodeAt(i);
		}
		out.set(data, 8);
		let c = 0xffffffff;
		for (let i = 4; i < 8 + data.length; i++) {
			c = crcTable[(c ^ out[i]) & 0xff] ^ (c >>> 8);
		}
		view.setUint32(8 + data.length, (c ^ 0xffffffff) >>> 0);
		return out;
	}

	MS.Images = Images;
	MS.decodeIndexedPng = decodeIndexedPng;
})();
