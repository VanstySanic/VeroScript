// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Windows-only diagnostic A/B replay. Pin only the benchmark child, alternate
// binaries, and keep all samples. The historical review executable is optional.
import { spawnSync } from 'node:child_process';
import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { createHash } from 'node:crypto';
import path from 'node:path';
const [before, after, output, mask, beforeMode, ...cases] = process.argv.slice(2);
if (process.platform !== 'win32' || !before || !after || !output || !/^[0-9a-f]+$/i.test(mask ?? '') || BigInt(`0x${mask}`) === 0n
		|| !['review', 'runtime', 'review-review'].includes(beforeMode) || !cases.length
		|| cases.some(name => !/^[a-z0-9_]+$/.test(name) || ['export', 'sizes'].includes(name))) {
	throw new Error('usage: node compare-pinned.mjs <before.exe> <after.exe> <new-output.json> <hex-affinity> <review|runtime|review-review> <case...>');
}
if (existsSync(output)) {
	throw new Error('output already exists');
}
const hash = file => createHash('sha256').update(readFileSync(file)).digest('hex');
const binaries = [before, after].map(file => path.resolve(file));
if (binaries.some(file => /["&|<>%^!\r\n]/.test(file))) {
	throw new Error('unsupported shell metacharacter in binary path');
}
const result = { schema: 1, timestamp: new Date().toISOString(), binaries,
	binaryHashes: binaries.map(hash), affinityMaskHex: mask, beforeMode, cases,
	currentCorpusSha256: hash(new URL('./ReviewBaseline/2026-09-04/cases.inc', import.meta.url)),
	notes: 'Windows start /b /wait /affinity; three serial processes per case and binary, seven samples/backend plus excluded warmup. Same selected logical processor, no control over sibling/background/frequency. '
		+ (beforeMode === 'review' ? 'Historical review versus runtime timing boundaries have minor harness differences.' : 'Both binaries use the same harness kind; preserve the baseline binary and verify matching source corpus/build flags.'),
	runs: [], summary: [] };
for (let session = 0; session < 3; ++session) {
	for (const [caseIndex, name] of cases.entries()) {
		for (let position = 0; position < 2; ++position) {
			const binary = (position + session + caseIndex) % 2;
			const useReview = beforeMode === 'review-review' || (binary === 0 && beforeMode === 'review');
			const args = useReview ? [name, '7'] : ['matrix', name, '7'];
			process.stderr.write(`pinned session ${session + 1}/3 ${name} ${binary ? 'after' : 'before'}\n`);
			const command = `start "" /b /wait /affinity ${mask} "${binaries[binary]}" ${args.map(arg => `"${arg}"`).join(' ')}`;
			const run = spawnSync('cmd.exe', ['/d', '/s', '/c', command],
				{ encoding: 'utf8', windowsVerbatimArguments: true, maxBuffer: 16 * 1024 * 1024 });
			if (run.error || run.status !== 0) {
				throw new Error(run.error?.message ?? run.stderr);
			}
			const lines = run.stdout.trim().split(/\r?\n/), columns = lines.shift().split(',');
			const rows = lines.map(line => Object.fromEntries(line.split(',').map((value, index) =>
				[columns[index], index < 2 ? value : Number(value)])));
			const names = new Set(rows.map(row => row.case));
			const expectedCases = name === 'all' ? 74 : 1;
			if (names.size !== expectedCases || rows.length !== expectedCases * 21
					|| (name !== 'all' && !names.has(name))
					|| rows.some(row => !['vm', 'mixed', 'jit'].includes(row.backend)
						|| !Number.isInteger(row.sample) || row.sample < 0 || row.sample >= 7
						|| Object.entries(row).some(([key, value]) => key !== 'case' && key !== 'backend' && !Number.isFinite(value)))
					|| new Set(rows.map(row => `${row.case}/${row.backend}/${row.sample}`)).size !== rows.length) {
				throw new Error('invalid/incomplete pinned replay');
			}
			result.runs.push({ session, binary, case: name, csv: run.stdout, rows });
			if (expectedCases === 1) {
				const centers = ['vm', 'mixed', 'jit'].map(backend => {
					const values = rows.filter(row => row.backend === backend).map(row => row.execute_ms).sort((a, b) => a - b);
					return `${backend}=${values[3]}ms`;
				});
				process.stderr.write(`  ${centers.join(' ')}\n`);
			}
		}
	}
}
const median = values => { const sorted = [...values].sort((a, b) => a - b); return sorted[Math.floor(sorted.length / 2)]; };
const groups = Map.groupBy(result.runs.flatMap(run => run.rows.map(row => ({ ...row, session: run.session, binary: run.binary }))),
	row => `${row.case}/${row.backend}`);
for (const [key, rows] of groups) {
	const stats = [0, 1].map(binary => {
		const selected = rows.filter(row => row.binary === binary), values = selected.map(row => row.execute_ms);
		const center = median(values);
		return { medianMs: center, madMs: median(values.map(value => Math.abs(value - center))),
			minMs: Math.min(...values), maxMs: Math.max(...values), samples: values.length,
			sessionMediansMs: [0, 1, 2].map(session => median(selected.filter(row => row.session === session).map(row => row.execute_ms))) };
	});
	result.summary.push({ key, before: stats[0], after: stats[1], changePercent: 100 * (stats[1].medianMs / stats[0].medianMs - 1) });
}
writeFileSync(output, JSON.stringify(result, null, 2) + '\n', { flag: 'wx' });
console.log(JSON.stringify(result.summary, null, 2));
