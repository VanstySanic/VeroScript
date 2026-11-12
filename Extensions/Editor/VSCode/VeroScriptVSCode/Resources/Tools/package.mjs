// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as fs from 'node:fs/promises';
import * as path from 'node:path';
import { createRequire } from 'node:module';
import { moduleRoot, repositoryRoot, temporaryRoot, runNode } from './paths.mjs';

const [localLicense, repositoryLicense] = await Promise.all([moduleRoot, repositoryRoot].map(root => fs.readFile(path.join(root, 'LICENSE'))));
if (!localLicense.equals(repositoryLicense)) {
	throw new Error('The extension LICENSE must match the repository LICENSE.');
}
await runNode(path.join(moduleRoot, 'node_modules/typescript/bin/tsc'), ['--noEmit']);
await runNode(path.join(moduleRoot, 'Resources/Tools/build.mjs'), ['--release']);
const pkg = JSON.parse(await fs.readFile(path.join(moduleRoot, 'package.json'), 'utf8'));
await fs.mkdir(path.join(moduleRoot, 'out'), { recursive: true });
await fs.mkdir(temporaryRoot, { recursive: true });
const stage = await fs.mkdtemp(path.join(temporaryRoot, 'package-'));
try {
	// The root README is the extension Details page; keep it in the VSIX.
	const files = ['package.json', '.vscodeignore', 'README.md', 'LICENSE', 'THIRD-PARTY-NOTICES.txt'];
	files.push(...(await fs.readdir(moduleRoot)).filter(name => /^package\.nls(?:\.[^.]+)?\.json$/.test(name)));
	for (const file of files) {
		await fs.copyFile(path.join(moduleRoot, file), path.join(stage, file));
	}
	for (const directory of ['dist', 'Public', 'Resources/Assets', 'Resources/Language', 'Resources/Syntaxes', 'Resources/Snippets', 'Resources/Localization']) {
		await fs.cp(path.join(moduleRoot, directory), path.join(stage, directory), {
			recursive: true,
			filter: source => !/(^|[/\\])(?:readme|changelog|changes|contributing)(?:[._-]|$)|\.map$/i.test(source),
		});
	}
	// This pinned VSCE entry packages a prebuilt tree without another prepublish.
	const { pack, listFiles } = createRequire(import.meta.url)('@vscode/vsce/out/package.js');
	const options = { cwd: stage, dependencies: false, allowMissingRepository: true,
		packagePath: path.join(moduleRoot, 'out', `${pkg.name}-${pkg.version}.vsix`) };
	if (process.argv.includes('--list')) {
		console.log((await listFiles(options)).join('\n'));
	}
	else
	{
		await pack(options);
	}
}
finally
{
	if (path.dirname(path.resolve(stage)) !== path.resolve(temporaryRoot)) {
		throw new Error('Invalid package staging directory.');
	}
	await fs.rm(stage, { recursive: true, force: true });
}
