// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import { fileURLToPath } from 'node:url';
import * as path from 'node:path';
import { spawn } from 'node:child_process';

export const moduleRoot = fileURLToPath(new URL('../..', import.meta.url));
export const repositoryRoot = path.resolve(moduleRoot, '../../../..');
export const temporaryRoot = path.join(repositoryRoot, '.tmp/vscode-plugin');

export function runNode(script, args = [], options = {}) {
	return new Promise((resolve, reject) => {
		const child = spawn(process.execPath, [script, ...args], { cwd: moduleRoot, stdio: 'inherit', ...options });
		child.once('error', reject);
		child.once('exit', code => code === 0 ? resolve() : reject(new Error(`${path.basename(script)} exited with ${code}.`)));
	});
}
