// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Replay the exact historical cross.cpp harness against two static runtimes.
// Only benchmark children receive a CPU-affinity mask. No input is generated or tuned here.
import { spawnSync } from 'node:child_process';
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import path from 'node:path';
import os from 'node:os';
import { fileURLToPath } from 'node:url';

const [before, after, output, mask, manifest] = process.argv.slice(2);
if (process.platform !== 'win32' || !before || !after || !output
		|| !/^[0-9a-f]+$/i.test(mask ?? '') || BigInt(`0x${mask}`) === 0n) {
	throw new Error('usage: node compare-owned-pinned.mjs <old-cross.exe> <new-cross.exe> <new-output.json> <hex-affinity> [manifest.json]');
}
if (existsSync(output)) {
	throw new Error('output already exists');
}
const root = path.dirname(fileURLToPath(import.meta.url));
const specifications = manifest ? JSON.parse(readFileSync(manifest, 'utf8')) : [
	['escaping', 'ReviewBaseline/2026-09-04/cross/escaping.vs', 5, 19083680],
	['reverse-1000', 'Inputs/owned-reverse-1000.vs', 100, 19194400],
	['reverse-40000', 'Inputs/owned-reverse-40000.vs', 5, 38173120],
	['forward-heldout', 'Inputs/owned-forward-heldout.vs', 5, 9895005],
];
if (!Array.isArray(specifications) || specifications.length === 0
		|| specifications.some(row => !Array.isArray(row) || row.length !== 4
			|| !/^[a-z0-9-]+$/.test(row[0]) || typeof row[1] !== 'string'
			|| !Number.isSafeInteger(row[2]) || row[2] < 1
			|| !Number.isSafeInteger(row[3]) || row[3] < -2147483648 || row[3] > 2147483647)
		|| new Set(specifications.map(row => row[0])).size !== specifications.length) {
	throw new Error('invalid replay workload manifest');
}
const workloads = specifications.map(([name, file, batch, expected]) =>
	({ name, file: path.resolve(root, file), batch, expected }));
const binaries = [before, after].map(file => path.resolve(file));
const hash = file => createHash('sha256').update(readFileSync(file)).digest('hex');
if ([...binaries, ...workloads.map(work => work.file)].some(file => /[",&|<>%^!\r\n]/.test(file))) {
	throw new Error('unsupported shell/CSV metacharacter in path');
}
const result = {
	schema: 1, timestamp: new Date().toISOString(), binaries, binaryHashes: binaries.map(hash),
	harnessSha256: hash(path.join(root, 'ReviewBaseline/2026-09-04/cross.cpp')),
	affinityMaskHex: mask, node: process.version,
	os: { platform: os.platform(), release: os.release(), architecture: os.arch(), cpu: os.cpus()[0].model },
	workloads: workloads.map(work => ({ ...work, sourceSha256: hash(work.file) })),
	notes: 'Both binaries use the archived cross.cpp harness. Each process compiles the module once outside execution timing, then executes a fresh VM per sample. Sample -1 is retained but excluded from summaries. Execution includes VM setup, JIT preparation, init and teardown; normalized by in-script work calls. Integer output checked by the historical harness; full-output correctness checked separately. Three serial processes/backend/binary/workload with seven valid samples each. Binary order alternates; backend order rotates across sessions/workloads. No frequency, sibling or background control.',
	runs: [], summary: [],
};
const columns = ['source', 'backend', 'sample', 'execute_ms', 'work_batch', 'expected',
	'peak_working_set', 'native_cfg', 'linear', 'vm_trampoline', 'typed_loops', 'cost_fallbacks'];
for (let session = 0; session < 3; ++session) {
	for (const [workIndex, work] of workloads.entries()) {
		process.stderr.write(`owned session ${session + 1}/3 ${work.name}\n`);
		for (let position = 0; position < 3; ++position) {
			const backend = ['vm', 'mixed', 'jit'][(position + session + workIndex) % 3];
			for (let order = 0; order < 2; ++order) {
				const binary = (order + session + workIndex + position) % 2;
				const args = [work.file, backend, String(work.batch), String(work.expected), '7'];
				const run = spawnSync('cmd.exe', ['/d', '/s', '/c',
					`start "" /b /wait /affinity ${mask} "${binaries[binary]}" ${args.map(arg => `"${arg}"`).join(' ')}`],
					{ encoding: 'utf8', windowsVerbatimArguments: true, maxBuffer: 4 * 1024 * 1024 });
				if (run.error || run.status !== 0) {
					throw new Error(run.error?.message ?? run.stderr);
				}
				const rows = run.stdout.trim().split(/\r?\n/).map(line => Object.fromEntries(line.split(',')
					.map((value, index) => [columns[index], index < 2 ? value : Number(value)])));
				if (rows.length !== 8 || rows.some((row, index) => row.sample !== index - 1
						|| row.source !== work.file || row.backend !== backend
						|| row.expected !== work.expected || row.work_batch !== work.batch
						|| columns.slice(2).some(key => !Number.isFinite(row[key])))) {
					throw new Error('invalid/incomplete owned replay');
				}
				const compileMatch = /load_ms=([\d.e+-]+) compile_ms=([\d.e+-]+)/.exec(run.stderr);
				if (!compileMatch || !compileMatch.slice(1).every(value => Number.isFinite(Number(value)))) {
					throw new Error('missing load/compile measurement');
				}
				result.runs.push({ session, binary, case: work.name, backend,
					loadMs: Number(compileMatch[1]), compileMs: Number(compileMatch[2]), stderr: run.stderr,
					// Add an explicit header when archiving; preserve the excluded warm row.
					csv: `${columns.join(',')}\n${run.stdout}`, rows });
			}
		}
	}
}
const median = values => { const sorted = [...values].sort((a, b) => a - b); return sorted[Math.floor(sorted.length / 2)]; };
const groups = Map.groupBy(result.runs.flatMap(run => run.rows.filter(row => row.sample >= 0)
	.map(row => ({ ...row, case: run.case, binary: run.binary, session: run.session }))), row => `${row.case}/${row.backend}`);
for (const [key, rows] of groups) {
	const stats = [0, 1].map(binary => {
		const selected = rows.filter(row => row.binary === binary), values = selected.map(row => row.execute_ms);
		const center = median(values);
		return { medianMs: center, madMs: median(values.map(value => Math.abs(value - center))),
			minMs: Math.min(...values), maxMs: Math.max(...values), samples: values.length,
			sessionMediansMs: [0, 1, 2].map(session => median(selected.filter(row => row.session === session).map(row => row.execute_ms))),
			costFallbacks: median(selected.map(row => row.cost_fallbacks)) };
	});
	result.summary.push({ key, before: stats[0], after: stats[1], speedup: stats[0].medianMs / stats[1].medianMs,
		changePercent: 100 * (stats[1].medianMs / stats[0].medianMs - 1) });
}
writeFileSync(output, JSON.stringify(result, null, 2) + '\n', { flag: 'wx' });
console.log(JSON.stringify(result.summary, null, 2));
