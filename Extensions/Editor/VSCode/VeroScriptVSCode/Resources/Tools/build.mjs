// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as fs from 'node:fs/promises';
import * as path from 'node:path';
import * as esbuild from 'esbuild';
import { moduleRoot, repositoryRoot } from './paths.mjs';

const release = process.argv.includes('--release');
const watch = process.argv.includes('--watch');
const tests = process.argv.includes('--tests');
const locales = { 'en-US': '', 'fr-FR': 'fr', 'ja-JP': 'ja', 'ko-KR': 'ko', 'zh-CN': 'zh-cn', 'zh-TW': 'zh-tw' };
const readJson = async file => JSON.parse(await fs.readFile(file, 'utf8'));

async function resources() {
	// Reuse the maintained product PNG without rasterizing artwork during builds.
	await fs.copyFile(path.join(repositoryRoot, 'Resources/Assets/icons/veroscript-icon-256.png'),
		path.join(moduleRoot, 'Resources/Assets/Icons/product.png'));
	const source = path.join(moduleRoot, 'Resources/Localization');
	const english = await readJson(path.join(source, 'en-US/manifest.json'));
	const manifest = await fs.readFile(path.join(moduleRoot, 'package.json'), 'utf8');
	const bundles = path.join(source, 'Bundles');
	const englishBundle = await readJson(path.join(bundles, 'bundle.l10n.json'));
	const placeholders = value => [...value.matchAll(/\{\d+\}/g)].map(match => match[0]).sort().join();
	for (const match of manifest.matchAll(/%([\w.]+)%/g)) {
		if (!english[match[1]]) {
			throw new Error(`Missing manifest translation: ${match[1]}`);
		}
	}
	for (const [culture, suffix] of Object.entries(locales)) {
		const translated = await readJson(path.join(source, culture, 'manifest.json'));
		if (Object.keys(english).sort().join() !== Object.keys(translated).sort().join()) {
			throw new Error(`Manifest translation keys differ: ${culture}`);
		}
		await fs.writeFile(path.join(moduleRoot, `package.nls${suffix ? '.' + suffix : ''}.json`), JSON.stringify(translated, null, 2) + '\n');
		if (suffix) {
			const bundle = await readJson(path.join(bundles, `bundle.l10n.${suffix}.json`));
			if (Object.keys(englishBundle).sort().join() !== Object.keys(bundle).sort().join()
				|| Object.entries(bundle).some(([key, value]) => typeof value !== 'string' || placeholders(value) !== placeholders(key))) {
				throw new Error(`Runtime translation keys or placeholders differ: ${culture}`);
			}
		}
	}
	const sources = await fs.readdir(path.join(moduleRoot, 'Private'));
	for (const file of sources.filter(file => file.endsWith('.ts'))) {
		const text = await fs.readFile(path.join(moduleRoot, 'Private', file), 'utf8');
		for (const match of text.matchAll(/l10n\.t\('([^']+)'/g)) {
			if (!(match[1] in englishBundle)) {
				throw new Error(`Missing message translation: ${match[1]}`);
			}
		}
	}
}

async function notices(meta) {
	const packages = new Set();
	for (const input of Object.keys(meta.inputs)) {
		const normalized = input.replaceAll('\\', '/');
		const match = /(?:^|\/)node_modules\/((?:@[^/]+\/)?[^/]+)/.exec(normalized);
		if (match) {
			packages.add(match[1]);
		}
	}
	const sections = ['Third-party code included in dist/extension.cjs.\nThe external VeroScript Runtime is distributed separately.'];
	for (const name of [...packages].sort()) {
		const directory = path.join(moduleRoot, 'node_modules', name);
		const pkg = await readJson(path.join(directory, 'package.json'));
		const files = (await fs.readdir(directory)).filter(file => /^(license|copying)(\..*)?$/i.test(file));
		if (!files.length) {
			throw new Error(`No license text found for bundled dependency ${name}.`);
		}
		const texts = await Promise.all(files.map(file => fs.readFile(path.join(directory, file), 'utf8')));
		sections.push(`${name} ${pkg.version} (${pkg.license})\n${texts.join('\n')}`);
	}
	await fs.writeFile(path.join(moduleRoot, 'THIRD-PARTY-NOTICES.txt'), sections.join('\n\n' + '='.repeat(72) + '\n\n') + '\n');
}

await resources();
const options = {
	absWorkingDir: moduleRoot, entryPoints: ['Private/extension.ts'], outfile: 'dist/extension.cjs',
	bundle: true, platform: 'node', format: 'cjs', target: 'node22', external: ['vscode'],
	sourcemap: release ? false : 'linked', metafile: true, logLevel: 'info',
	plugins: [{ name: 'resources', setup(build) {
		build.onStart(resources);
		build.onEnd(async result => {
			if (result.metafile) {
				await notices(result.metafile);
			}
		});
	} }]
};
if (watch) {
	const context = await esbuild.context(options);
	await context.watch();
	const { watch: watchFiles } = await import('node:fs');
	const watcher = watchFiles(path.join(moduleRoot, 'Resources/Localization'), { recursive: true }, () => {
		void context.rebuild().catch(error => console.error(error.message));
	});
	for (const signal of ['SIGINT', 'SIGTERM']) {
		process.once(signal, async () => { watcher.close(); await context.dispose(); process.exit(0); });
	}
}
else
{
	await esbuild.build(options);
}
if (tests) {
	for (const [directory, output] of [['Unit', 'Unit'], ['Integration', 'Integration']]) {
		const source = path.join(moduleRoot, 'Private/Tests', directory);
		const entries = (await fs.readdir(source)).filter(file => file.endsWith('.test.ts')).map(file => path.join(source, file));
		if (entries.length) {
			await esbuild.build({ absWorkingDir: moduleRoot, entryPoints: entries, outdir: `out/${output}`, outExtension: { '.js': '.cjs' },
				bundle: true, platform: 'node', format: 'cjs', target: 'node22', external: ['vscode', 'mocha'], sourcemap: 'linked' });
		}
	}
}
