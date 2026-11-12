// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Pure post-processing of recorded measurements; never invokes a benchmark.
import { readFileSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import path from 'node:path';

const [mode, first, second] = process.argv.slice(2);
if (!['compare', 'archive'].includes(mode) || !first || !second) {
	throw new Error('usage: node analyze.mjs compare <before.json> <after.json> OR archive <measurement.json> <new-directory>');
}
const read = file => JSON.parse(readFileSync(file, 'utf8'));
const median = values => {
	const sorted = [...values].sort((a, b) => a - b), mid = Math.floor(sorted.length / 2);
	return sorted.length % 2 ? sorted[mid] : (sorted[mid - 1] + sorted[mid]) / 2;
};
const summarize = data => new Map([...Map.groupBy(
	data.sessions.flatMap(s => s.rows.map(row => ({ ...row, session: s.index }))),
	row => `${row.case}/${row.backend}`)].map(([key, rows]) => {
		const values = rows.map(row => row.execute_per_work_ms), center = median(values);
		return [key, { samples: values.length, medianMs: center,
			medianAbsoluteDeviationMs: median(values.map(value => Math.abs(value - center))),
			minMs: Math.min(...values), maxMs: Math.max(...values),
			sessionMediansMs: [...Map.groupBy(rows, row => row.session).values()]
				.map(group => median(group.map(row => row.execute_per_work_ms))),
			compileMs: median(rows.map(row => row.compile_ms)),
			compileAttempts: median(rows.map(row => row.compile_attempts)),
			costFallbacks: median(rows.map(row => row.cost_fallbacks)),
			dispatches: median(rows.map(row => row.dispatches)) }];
	}));
const data = read(first);
if (mode === 'archive') {
	if (existsSync(second)) {
		throw new Error('archive directory already exists');
	}
	mkdirSync(second, { recursive: true });
	const { sessions, runs, summary, ...metadata } = data;
	const captures = sessions ?? runs;
	if (!Array.isArray(captures) || !captures.length) {
		throw new Error('no recorded samples');
	}
	const sampleFiles = captures.map((capture, index) => {
		const { csv, rows, ...identity } = capture;
		return sessions ? { file: `session-${capture.index + 1}.csv`, session: capture.index }
			: { ...identity, file: `run-${index + 1}.csv` };
	});
	writeFileSync(path.join(second, 'metadata.json'), JSON.stringify({ ...metadata,
		summary: sessions ? Object.fromEntries(summarize(data)) : summary, sampleFiles }, null, 2) + '\n', { flag: 'wx' });
	for (const [index, capture] of captures.entries()) {
		writeFileSync(path.join(second, sampleFiles[index].file), capture.csv, { flag: 'wx' });
	}
	console.log(`Archived ${captures.length} runs to ${second}`);
}
else
{
	const afterData = read(second);
	if (data.corpusSha256 !== afterData.corpusSha256 || JSON.stringify(data.argv) !== JSON.stringify(afterData.argv)) {
		throw new Error('comparison requires identical corpus and arguments');
	}
	if ((data.affinityMaskHex ?? null) !== (afterData.affinityMaskHex ?? null)) {
		throw new Error('comparison requires matching affinity; pinned/unpinned results are not interchangeable');
	}
	const before = summarize(data), after = summarize(afterData);
	if (before.size !== after.size || [...before.keys()].some(key => !after.has(key))) {
		throw new Error('comparison coverage mismatch');
	}
	const comparisons = [...before].map(([key, baseline]) => {
		const current = after.get(key);
		return { key, changePercent: 100 * (current.medianMs / baseline.medianMs - 1),
			absoluteChangeMs: current.medianMs - baseline.medianMs, before: baseline, after: current };
	}).sort((a, b) => b.changePercent - a.changePercent);
	console.log(JSON.stringify({ beforeBinary: data.executableSha256, afterBinary: afterData.executableSha256,
		comparisons }, null, 2));
}
