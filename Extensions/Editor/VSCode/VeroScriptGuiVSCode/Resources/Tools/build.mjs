// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as fs from 'node:fs/promises';
import * as path from 'node:path';
import * as esbuild from 'esbuild';
import { moduleRoot, repositoryRoot, runNode } from './paths.mjs';

const locales = { 'en-US': '', 'fr-FR': 'fr', 'ja-JP': 'ja', 'ko-KR': 'ko', 'zh-CN': 'zh-cn', 'zh-TW': 'zh-tw' };
const readJson = async file => JSON.parse(await fs.readFile(file, 'utf8'));

async function resources() {
	// Reuse the maintained product PNG without rasterizing artwork during builds.
	await fs.copyFile(path.join(repositoryRoot, 'Resources/Assets/icons/veroscript-icon-256.png'),
		path.join(moduleRoot, 'Resources/Assets/Icons/product.png'));
	const root = path.join(moduleRoot, 'Resources/Localization');
	const english = await readJson(path.join(root, 'en-US/manifest.json'));
	const messages = await readJson(path.join(root, 'Bundles/bundle.l10n.json'));
	const manifest = await fs.readFile(path.join(moduleRoot, 'package.json'), 'utf8');
	for (const match of manifest.matchAll(/%([\w.]+)%/g)) {
		if (!(match[1] in english)) {
			throw new Error(`Missing GUI manifest translation: ${match[1]}`);
		}
	}
	const placeholders = value => [...value.matchAll(/\{\d+\}/g)].map(match => match[0]).sort().join();
	for (const [locale, suffix] of Object.entries(locales)) {
		const translated = await readJson(path.join(root, locale, 'manifest.json'));
		if (Object.keys(english).sort().join() !== Object.keys(translated).sort().join()) {
			throw new Error(`GUI manifest keys differ: ${locale}`);
		}
		await fs.writeFile(path.join(moduleRoot, `package.nls${suffix ? '.' + suffix : ''}.json`), JSON.stringify(translated, null, 2) + '\n');
		const bundle = await readJson(path.join(root, `Bundles/bundle.l10n${suffix ? '.' + suffix : ''}.json`));
		if (Object.keys(messages).sort().join() !== Object.keys(bundle).sort().join()
			|| Object.entries(bundle).some(([key, value]) => typeof value !== 'string' || placeholders(value) !== placeholders(key))) {
			throw new Error(`GUI message keys or placeholders differ: ${locale}`);
		}
	}
	// Structural and argument messages are dynamic l10n keys defined in the pure models.
	for (const file of (await fs.readdir(path.join(moduleRoot, 'Private'))).filter(file => file.endsWith('.ts'))) {
		const source = await fs.readFile(path.join(moduleRoot, 'Private', file), 'utf8');
		for (const match of source.matchAll(/(?:l10n\.t|new GuiArgumentError|issue)\('([^']+)'/g)) {
			if (!(match[1] in messages)) {
				throw new Error(`Missing GUI message translation: ${match[1]}`);
			}
		}
	}
}

await runNode(path.join(moduleRoot, 'Resources/Tools/schema.mjs'), ['--check']);
await resources();
const options = {
	absWorkingDir: moduleRoot, entryPoints: ['Private/extension.ts'], outfile: 'dist/extension.cjs',
	bundle: true, platform: 'node', format: 'cjs', target: 'node22', external: ['vscode'],
	sourcemap: !process.argv.includes('--release') ? 'linked' : false, metafile: true,
	plugins: [{ name: 'gui-resources', setup(build) {
		build.onStart(resources);
		build.onEnd(async result => {
			if (result.metafile) {
				if (Object.keys(result.metafile.inputs).some(input => /(?:^|[/\\])node_modules[/\\]/.test(input))) {
					throw new Error('Review notices before bundling a new GUI runtime dependency.');
				}
				await fs.writeFile(path.join(moduleRoot, 'THIRD-PARTY-NOTICES.txt'),
					'This extension bundle contains project code only.\nVeroScript and native GUI Runtime components are installed and licensed separately.\n');
			}
		});
	} }]
};
if (process.argv.includes('--watch')) {
	const context = await esbuild.context(options);
	await context.watch();
	const { watch } = await import('node:fs');
	const watchers = ['Localization', 'Schemas'].map(directory => watch(path.join(moduleRoot, 'Resources', directory), { recursive: true }, () => {
		void context.rebuild().catch(error => console.error(error.message));
	}));
	for (const signal of ['SIGINT', 'SIGTERM']) {
		process.once(signal, async () => {
			for (const watcher of watchers) {
				watcher.close();
			}
			await context.dispose();
			process.exit(0);
		});
	}
}
else
{
	await esbuild.build(options);
}
if (process.argv.includes('--tests')) {
	for (const directory of ['Unit', 'Integration']) {
		const folder = path.join(moduleRoot, 'Private/Tests', directory);
		const files = (await fs.readdir(folder)).filter(file => file.endsWith('.test.ts')).map(file => path.join(folder, file));
		await esbuild.build({ absWorkingDir: moduleRoot, entryPoints: files, outdir: `out/${directory}`, outExtension: { '.js': '.cjs' },
			bundle: true, platform: 'node', format: 'cjs', target: 'node22', external: ['vscode', 'mocha'], sourcemap: 'linked' });
	}
}
