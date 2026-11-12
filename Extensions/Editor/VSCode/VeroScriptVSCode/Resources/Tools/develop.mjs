// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as fs from 'node:fs/promises';
import * as path from 'node:path';
import { moduleRoot } from './paths.mjs';

const directory = path.join(moduleRoot, '.vscode');
await fs.mkdir(directory, { recursive: true });
const files = {
	'launch.json': { version: '0.2.0', configurations: [{ name: 'VeroScript Extension', type: 'extensionHost', request: 'launch',
		args: ['--extensionDevelopmentPath=${workspaceFolder}'], outFiles: ['${workspaceFolder}/dist/**/*.cjs'], preLaunchTask: 'Build extension' }] },
	'tasks.json': { version: '2.0.0', tasks: [{ label: 'Build extension', type: 'npm', script: 'build', problemMatcher: [] }] }
};
for (const [file, content] of Object.entries(files)) {
	try {
		await fs.writeFile(path.join(directory, file), JSON.stringify(content, null, 2) + '\n', { flag: 'wx' });
		console.log(`Created .vscode/${file}`);
	}
	catch (error)
	{
		if (error.code !== 'EEXIST') {
			throw error;
		}
		console.log(`Kept existing .vscode/${file}`);
	}
}
