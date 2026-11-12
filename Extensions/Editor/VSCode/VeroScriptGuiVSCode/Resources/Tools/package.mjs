// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as fs from 'node:fs/promises';
import * as path from 'node:path';
import { createRequire } from 'node:module';
import { moduleRoot, repositoryRoot, temporaryRoot, runNode } from './paths.mjs';

const [license, original] = await Promise.all([moduleRoot, repositoryRoot].map(root => fs.readFile(path.join(root, 'LICENSE'))));
if (!license.equals(original)) {
	throw new Error('GUI extension LICENSE must match the repository LICENSE.');
}
await runNode(path.join(moduleRoot, 'node_modules/typescript/bin/tsc'), ['--noEmit']);
await runNode(path.join(moduleRoot, 'Resources/Tools/build.mjs'), ['--release']);
const pkg = JSON.parse(await fs.readFile(path.join(moduleRoot, 'package.json'), 'utf8'));
await fs.mkdir(path.join(moduleRoot, 'out'), { recursive: true });
await fs.mkdir(temporaryRoot, { recursive: true });
const stage = await fs.mkdtemp(path.join(temporaryRoot, 'package-'));
try {
	// The root README is the extension Details page; keep it in the VSIX.
	const files = ['package.json', '.vscodeignore', 'README.md', 'LICENSE', 'THIRD-PARTY-NOTICES.txt',
		...(await fs.readdir(moduleRoot)).filter(file => /^package\.nls(?:\.[^.]+)?\.json$/.test(file))];
	for (const file of files) {
		await fs.copyFile(path.join(moduleRoot, file), path.join(stage, file));
	}
	for (const directory of ['dist', 'Resources/Assets', 'Resources/Language', 'Resources/Syntaxes', 'Resources/Schemas', 'Resources/Localization']) {
		await fs.cp(path.join(moduleRoot, directory), path.join(stage, directory), { recursive: true,
			filter: source => !/\.map$|(^|[/\\])(?:README|CHANGELOG)\.md$/i.test(source) });
	}
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
		throw new Error('Invalid GUI package staging directory.');
	}
	await fs.rm(stage, { recursive: true, force: true });
}
