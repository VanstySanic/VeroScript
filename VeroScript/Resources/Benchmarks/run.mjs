// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Run serial measurement sessions and preserve all samples and build metadata.
import { spawnSync } from 'node:child_process';
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const [executable, output, ...args] = process.argv.slice(2);
if (!executable || !output || !['matrix', 'source', 'host'].includes(args[0])) {
	throw new Error('usage: node run.mjs <executable> <new-output.json> <matrix|source|host> <benchmark arguments>');
}
if (existsSync(output)) {
	throw new Error(`refusing to overwrite existing measurement: ${output}`);
}
const affinityMaskHex = process.env.VEROSCRIPT_BENCH_AFFINITY;
if (affinityMaskHex !== undefined && (process.platform !== 'win32'
		|| !/^[0-9a-f]+$/i.test(affinityMaskHex) || BigInt(`0x${affinityMaskHex}`) === 0n)) {
	throw new Error('VEROSCRIPT_BENCH_AFFINITY requires a nonzero Windows hexadecimal processor mask');
}
const resolvedExecutable = path.resolve(executable);
if (affinityMaskHex && [resolvedExecutable, ...args].some(value => /["&|<>%^!\r\n]/.test(value))) {
	throw new Error('unsupported shell metacharacter in pinned benchmark arguments');
}
const hash = file => createHash('sha256').update(readFileSync(file)).digest('hex');
const corpus = args[0] === 'source' ? args[1]
	: path.join(path.dirname(fileURLToPath(import.meta.url)), args[0] === 'host' ? 'HostBenchmarks.cpp' : 'ReviewBaseline/2026-09-04/cases.inc');
const result = {
	schema: 1, timestamp: new Date().toISOString(), executable: resolvedExecutable,
	executableSha256: hash(executable), corpus: path.resolve(corpus), corpusSha256: hash(corpus),
	argv: args, node: process.version, os: { platform: os.platform(), release: os.release(),
		architecture: os.arch(), cpus: os.cpus().map(cpu => cpu.model), memory: os.totalmem() },
	measurementNotes: args[0] === 'host'
		? 'Hot host-call batches; load_ms/compile_ms and non-compile/non-dispatch diagnostic columns are unused zeros, not measurements. Requested persistent-session backends all execute in VM.'
		: 'Execute timing includes a fresh VM, JIT preparation, initialization and teardown; work_batch normalizes only in-script work repetitions.',
	affinityMaskHex: affinityMaskHex ?? null,
	environmentNotes: (affinityMaskHex
		? 'Only the benchmark child is pinned using Windows start /b /wait /affinity. '
		: 'No affinity control. ')
		+ 'No frequency/sibling control; background applications not stopped. Runtime build options must be recorded in the implementation log.',
	sessions: [], summary: [],
};
for (let session = 0; session < 3; ++session) {
	process.stderr.write(`measurement session ${session + 1}/3: ${args.join(' ')}\n`);
	const options = { encoding: 'utf8', maxBuffer: 32 * 1024 * 1024,
		env: { ...process.env, VEROSCRIPT_BENCH_ROTATION: String(session) } };
	const run = affinityMaskHex
		? spawnSync('cmd.exe', ['/d', '/s', '/c',
			`start "" /b /wait /affinity ${affinityMaskHex} "${resolvedExecutable}" ${args.map(arg => `"${arg}"`).join(' ')}`],
			{ ...options, windowsVerbatimArguments: true })
		: spawnSync(resolvedExecutable, args, options);
	if (run.error || run.status !== 0) {
		throw new Error(run.error?.message ?? run.stderr);
	}
	const lines = run.stdout.trim().split(/\r?\n/);
	const columns = lines.shift().split(',');
	const rows = lines.map(line => Object.fromEntries(line.split(',').map((value, index) =>
		[columns[index], index < 2 ? value : Number(value)])));
	if (!rows.length || rows.some(row => Object.entries(row).some(([key, value]) =>
			key !== 'case' && key !== 'backend' && !Number.isFinite(value)))) {
		throw new Error('invalid benchmark CSV');
	}
	const expectedSamples = Number(args[args[0] === 'matrix' ? 2 : args[0] === 'host' ? 1 : 4]);
	const expectedCases = args[0] === 'host' ? 4 : args[0] === 'matrix' && args[1] === 'all' ? 74 : 1;
	if (rows.length !== expectedCases * 3 * expectedSamples
			|| new Set(rows.map(row => `${row.case}/${row.backend}/${row.sample}`)).size !== rows.length
			|| rows.some(row => !['vm', 'mixed', 'jit'].includes(row.backend)
				|| !Number.isInteger(row.sample) || row.sample < 0 || row.sample >= expectedSamples)) {
		throw new Error('incomplete or duplicate benchmark samples');
	}
	result.sessions.push({ index: session, csv: run.stdout, rows });
}
const grouped = Map.groupBy(result.sessions.flatMap(session => session.rows), row => `${row.case}/${row.backend}`);
for (const [key, rows] of grouped) {
	const median = field => { const values = rows.map(row => row[field]).sort((a, b) => a - b);
		const middle = Math.floor(values.length / 2);
		return values.length % 2 ? values[middle] : (values[middle - 1] + values[middle]) / 2; };
	result.summary.push({ key, samples: rows.length, executePerWorkMs: median('execute_per_work_ms'),
		compileMs: median('compile_ms'), compileAttempts: median('compile_attempts'),
		costFallbacks: median('cost_fallbacks'), dispatches: median('dispatches'),
		minExecutePerWorkMs: Math.min(...rows.map(row => row.execute_per_work_ms)),
		maxExecutePerWorkMs: Math.max(...rows.map(row => row.execute_per_work_ms)) });
}
writeFileSync(output, JSON.stringify(result, null, 2) + '\n', { flag: 'wx' });
process.stdout.write(JSON.stringify(result.summary, null, 2) + '\n');
