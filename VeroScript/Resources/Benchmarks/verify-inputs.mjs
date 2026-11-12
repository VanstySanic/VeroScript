// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Exact full-output checks, not a statistical timing run.
import { spawnSync } from 'node:child_process';
import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { createHash } from 'node:crypto';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const [runtime, output] = process.argv.slice(2);
if (!runtime || !output || existsSync(output)) {
	throw new Error('usage: verify-inputs.mjs <runtime-benchmark.exe> <new-output.json>');
}
const root = path.dirname(fileURLToPath(import.meta.url));
const workloads = [
	['escaping', 'ReviewBaseline/2026-09-04/cross/escaping.vs', 5, 19083680],
	['reverse-1000', 'Inputs/owned-reverse-1000.vs', 100, 19194400],
	['reverse-40000', 'Inputs/owned-reverse-40000.vs', 5, 38173120],
	['forward-heldout', 'Inputs/owned-forward-heldout.vs', 5, 9895005],
	...JSON.parse(readFileSync(path.join(root, 'Inputs/set-heldout.json'), 'utf8')),
];
const hash = file => createHash('sha256').update(readFileSync(file)).digest('hex');
const executable = path.resolve(runtime);
const result = { schema: 1, timestamp: new Date().toISOString(), executable,
	executableSha256: hash(executable),
	notes: 'Correctness-only run: exact full output checked by RuntimeBenchmarks, one excluded warmup and one reported sample per VM/Mixed/JIT backend. Captured timings are not performance evidence.',
	runs: [] };
for (const [name, relative, batch, expected] of workloads) {
	const file = path.join(root, relative);
	const args = ['source', file, String(expected), String(batch), '1'];
	const run = spawnSync(executable, args, { encoding: 'utf8', maxBuffer: 4 * 1024 * 1024 });
	if (run.error || run.status !== 0) {
		throw new Error(run.error?.message ?? run.stderr);
	}
	const lines = run.stdout.trim().split(/\r?\n/), columns = lines.shift().split(',');
	const rows = lines.map(line => Object.fromEntries(line.split(',').map((value, index) =>
		[columns[index], index < 2 ? value : Number(value)])));
	if (rows.length !== 3 || new Set(rows.map(row => row.backend)).size !== 3
			|| rows.some(row => !['vm', 'mixed', 'jit'].includes(row.backend) || row.sample !== 0)) {
		throw new Error('incomplete full-output validation');
	}
	result.runs.push({ case: name, source: file, sourceSha256: hash(file), args,
		csv: run.stdout, stderr: run.stderr, rows });
	console.log(`${name}: exact output passed on VM, Mixed and JIT`);
}
writeFileSync(output, JSON.stringify(result, null, 2) + '\n', { flag: 'wx' });
