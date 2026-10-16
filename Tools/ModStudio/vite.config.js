// Builds Mod Studio's page into dist/: the scripts in js/ as one file, studio.css, and vendor/ and data/ copied as they are
// (Monaco, cytoscape, luaparse and the Lua API are loaded at run time from those folders). Studio and Start-ModStudio.ps1 serve dist/
// over http; opened straight from disk (file://) the browser won't run it.
import { defineConfig } from 'vite';
import { cpSync } from 'node:fs';
import { resolve } from 'node:path';

const here = import.meta.dirname;

function copyRuntimeFolders() {
	return {
		name: 'copy-runtime-folders',
		apply: 'build',
		writeBundle() {
			for (const folder of ['vendor', 'data']) {
				cpSync(resolve(here, folder), resolve(here, 'dist', folder), { recursive: true });
			}
		},
	};
}

export default defineConfig({
	base: './',
	publicDir: false,
	plugins: [copyRuntimeFolders()],
	build: {
		outDir: 'dist',
		emptyOutDir: true,
		target: 'es2020',
		sourcemap: true,
		modulePreload: false,
		rollupOptions: { input: resolve(here, 'ModStudio.html') },
	},
});
