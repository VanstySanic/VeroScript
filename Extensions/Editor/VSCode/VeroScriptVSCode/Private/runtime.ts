// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import { execFile } from 'node:child_process';
import { constants } from 'node:fs';
import * as fs from 'node:fs/promises';
import * as path from 'node:path';
import { processEnvironment } from './configuration';

export interface Runtime {
	readonly root: string;
	readonly cli: string;
	readonly lsp: string;
	readonly dap: string;
	readonly stdlib: string;
	readonly version: string;
	readonly description: string;
}

export async function requireDirectory(directory: string): Promise<void> {
	if (!(await fs.stat(directory)).isDirectory()) {
		throw new Error(`Not a directory: ${directory}`);
	}
}

export async function requireFile(file: string, executable = false): Promise<void> {
	if (!(await fs.stat(file)).isFile()) {
		throw new Error(`Not a file: ${file}`);
	}
	if (executable && process.platform !== 'win32') {
		await fs.access(file, constants.X_OK);
	}
}

export class RuntimeResolver {
	async resolve(root: string): Promise<Runtime> {
		const resolved = root || await this.findOnPath();
		const canonical = await fs.realpath(resolved);
		const suffix = process.platform === 'win32' ? '.exe' : '';
		const cli = path.join(canonical, `veroscript${suffix}`);
		const files = [cli, path.join(canonical, `veroscript-lsp${suffix}`), path.join(canonical, `veroscript-dap${suffix}`)];
		await Promise.all([requireDirectory(path.join(canonical, 'stdlib')), requireDirectory(path.join(canonical, 'localization'))]);
		const signature = JSON.stringify(await Promise.all(files.map(async file => {
			await requireFile(file, true);
			const stat = await fs.stat(file);
			return [stat.mtimeMs, stat.size];
		})));
		const previous = this.cache.get(canonical);
		if (previous?.signature === signature) return previous.promise;

		const promise = this.inspect(canonical, suffix);
		this.cache.set(canonical, { signature, promise });
		try {
			return await promise;
		}
		catch (error)
		{
			this.cache.delete(canonical);
			throw error;
		}
	}

	clear(): void {
		this.cache.clear();
	}

	private async findOnPath(): Promise<string> {
		const suffix = process.platform === 'win32' ? '.exe' : '';
		for (const directory of (process.env.PATH ?? '').split(path.delimiter)) {
			if (!directory) {
				continue;
			}
			try {
				const cli = await fs.realpath(path.join(directory.replace(/^"|"$/g, ''), `veroscript${suffix}`));
				await requireFile(cli, true);
				return path.dirname(cli);
			}
			catch
			{
				// Continue through PATH only when no explicit Runtime was configured.
			}
		}
		throw new Error('No VeroScript Runtime found. Use VeroScript: Select Runtime.');
	}

	private async inspect(root: string, suffix: string): Promise<Runtime> {
		const cli = path.join(root, `veroscript${suffix}`);
		const lsp = path.join(root, `veroscript-lsp${suffix}`);
		const dap = path.join(root, `veroscript-dap${suffix}`);
		const stdlib = path.join(root, 'stdlib');
		await Promise.all([requireFile(cli, true), requireFile(lsp, true), requireFile(dap, true), requireDirectory(stdlib), requireDirectory(path.join(root, 'localization'))]);
		const description = await new Promise<string>((resolve, reject) => {
			execFile(cli, ['--version'], { cwd: root, env: processEnvironment(), timeout: 5000, maxBuffer: 65536, windowsHide: true, encoding: 'utf8' }, (error, stdout, stderr) => {
				if (error) {
					reject(new Error(`Cannot start ${cli}: ${stderr || error.message}`));
				}
				else
				{
					resolve(stdout.trim());
				}
			});
		});
		const version = /\b(\d+\.\d+\.\d+)\b/.exec(description)?.[1];
		if (!version) {
			throw new Error(`Unrecognized Runtime version: ${description}`);
		}
		return { root, cli, lsp, dap, stdlib, version, description };
	}

	private readonly cache = new Map<string, { signature: string; promise: Promise<Runtime> }>();
}
