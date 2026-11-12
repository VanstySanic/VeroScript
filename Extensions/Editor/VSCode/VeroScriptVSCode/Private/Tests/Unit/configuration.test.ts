// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as assert from 'node:assert/strict';
import { execFile } from 'node:child_process';
import * as fs from 'node:fs/promises';
import * as path from 'node:path';
import { test } from 'node:test';
import { backend, cliArguments, compileArguments, compileOptions, containsPath, environment, expand, macros, mergedCompileOptions, processEnvironment, readAnalysisSettings, readRuntimeSettings, readSettings, resolvePath, watchDirectories } from '../../configuration';
import { RuntimeResolver } from '../../runtime';

const base = path.resolve('fixture workspace');
const defaults = readSettings({ get: <T>(_key: string, fallback: T) => fallback }, base);

test('add-on compiler flags preserve CLI order without commands, files or execution settings', () => {
	const options = mergedCompileOptions(defaults, { importPaths: ['中文 imports'], defines: ['GUI=1'],
		implicitImportPaths: ['helpers.vs'], stdlibSearchPaths: ['stdlib'] }, base, path.join(base, 'runtime/stdlib'));
	const flags = compileArguments(options);
	const program = path.join(base, 'page.vswml');
	assert.deepEqual(cliArguments(options, program, 'check', 'vm', []), ['--backend=vm', ...flags, '--check', program]);
	assert.deepEqual(cliArguments(options, program, 'run', 'mixed', ['one argument']), ['--backend=mixed', ...flags, program, 'one argument']);
	assert.ok(!flags.some(flag => flag.startsWith('--backend') || flag === '--check' || flag === program));
	assert.ok(flags.includes(path.join(base, '中文 imports')));
});

test('normalizes paths and replaces arrays consistently across process interfaces', () => {
	const settings = { ...defaults, importPaths: [path.join(base, 'old')], defines: ['OLD=1'] };
	const options = mergedCompileOptions(settings, { importPaths: [], defines: ['NEW=2'], implicitImportPaths: ['common.vs'], stdlibSearchPaths: [] }, base, path.join(base, 'runtime/stdlib'));
	assert.deepEqual(options.importPaths, []);
	assert.deepEqual(options.defines, ['NEW=2']);
	assert.deepEqual(options.implicitImportPaths, [path.join(base, 'common.vs')]);
	assert.deepEqual(options.stdlibSearchPaths, [path.join(base, 'runtime/stdlib')]);
	assert.equal(readAnalysisSettings({ get: <T>(_key: string, fallback: T) => fallback }, base).limits.lspAnalysisTimeoutMs, 5000);
	assert.throws(() => readAnalysisSettings({ get: <T>(key: string, fallback: T) => (key === 'lspRequestTimeoutMs' ? 49 : fallback) as T }, base));
	assert.throws(() => backend('auto'));
	assert.throws(() => compileOptions({ allowCircularImports: 'yes' }, base));
});

test('run-only errors do not disable analysis or debugging', () => {
	for (const field of ['run.cwd', 'run.jitBackendPath']) {
		const reader = { get: <T>(key: string, fallback: T) => (key === field ? '${command:unknown}' : fallback) as T };
		assert.throws(() => readSettings(reader, base));
		assert.ok(readAnalysisSettings(reader, base).enabled);
		assert.equal(readRuntimeSettings(reader, base).runtimePath, '');
	}
});

test('a custom stdio server keeps argv boundaries and resolves independently of Runtime', () => {
	const options: Record<string, unknown> = {
		'lsp.executablePath': '${workspaceFolder}/UE host/Analysis.exe',
		'lsp.arguments': ['--project', 'C:\\UE Projects\\Game.uproject', '', '${literal}'],
		runtimePath: '${env:VEROSCRIPT_TEST_UNDEFINED_VARIABLE}'
	};
	const reader = { get: <T>(key: string, fallback: T) => (options[key] ?? fallback) as T };
	const analysis = readAnalysisSettings(reader, base);
	assert.equal(analysis.executablePath, path.join(base, 'UE host/Analysis.exe'));
	assert.deepEqual(analysis.arguments, options['lsp.arguments']);
	assert.throws(() => readAnalysisSettings(reader, base, false), /owning workspace folder/);
	assert.throws(() => readRuntimeSettings(reader, base), /Unresolved variable/);
	options['lsp.arguments'] = ['bad\0argument'];
	assert.throws(() => readAnalysisSettings(reader, base), /without NUL/);
	delete options['lsp.executablePath'];
	delete options.runtimePath;
	assert.deepEqual(readAnalysisSettings(reader, base).arguments, []);
});

test('resolves explicit JIT module paths and keeps them out of compile-only arguments', () => {
	assert.equal(defaults.jitBackendPath, '');
	const read = (value: unknown, hasWorkspace = true) => readSettings({
		get: <T>(key: string, fallback: T) => (key === 'run.jitBackendPath' ? value : fallback) as T
	}, base, path.join(base, 'scripts/main.vs'), hasWorkspace);
	assert.equal(read('Jit/中文 backend').jitBackendPath, path.join(base, 'Jit/中文 backend'));
	assert.equal(read('${fileDirname}/Jit').jitBackendPath, path.join(base, 'scripts/Jit'));
	assert.throws(() => read('${workspaceFolder}/Jit', false));
	assert.throws(() => read(5));
	assert.throws(() => read('bad\0path'));
	const module = path.join(base, 'Jit/中文 backend');
	const program = path.join(base, 'main.vs');
	const run = cliArguments(defaults, program, 'run', 'jit', ['--jit-backend=script-argument'], module);
	assert.deepEqual(run.slice(0, 3), ['--backend=jit', '--jit-backend', module]);
	assert.deepEqual(run.slice(-2), [program, '--jit-backend=script-argument']);
	assert.equal(cliArguments(defaults, program, 'check', 'jit', [], module).includes('--jit-backend'), false);
	assert.equal(cliArguments(defaults, program, 'run', 'mixed', []).includes('--jit-backend'), false);
});

test('expands supported variables and rejects missing context', () => {
	assert.equal(expand('${workspaceFolder}/Scripts', base), `${base}/Scripts`);
	assert.equal(expand('${fileDirname}', base, path.join(base, 'sub/main.vs')), path.join(base, 'sub'));
	assert.throws(() => expand('${fileDirname}', base));
	assert.throws(() => expand('${command:anything}', base));
	assert.throws(() => expand('${env:VEROSCRIPT_TEST_UNDEFINED_VARIABLE}', base));
	assert.equal(resolvePath('${workspaceFolder}/main.vs', path.join(base, 'build'), undefined, base), path.join(base, 'main.vs'));
	assert.throws(() => readSettings({ get: <T>(key: string, fallback: T) => (key === 'runtimePath' ? '${workspaceFolder}/runtime' : fallback) as T }, base, undefined, false));
	assert.equal(containsPath(base, path.join(base, 'sub/main.vs')), true);
	assert.equal(containsPath(base, path.resolve(base, '../fixture workspace2/main.vs')), false);
});

test('validates macro signatures and environment without changing process state', () => {
	assert.deepEqual(macros([{ name: 'F', parameters: ['x'], variadic: true, body: 'x' }])[0]?.parameters, ['x']);
	assert.throws(() => macros([{ name: 'F', parameters: ['x', 'x'], body: '' }]));
	assert.throws(() => macros([{ name: 'F', body: '' }, { name: 'F', body: '' }]));
	assert.throws(() => macros([{ name: 'F', body: '', sourceName: 'custom' }]));
	assert.throws(() => environment({ VERO_PATH: 'imports' }));
	assert.throws(() => environment({ X: null }));
	assert.throws(() => environment({ 'X=Y': 'value' }));
	const before = process.env.VERO_PATH;
	const result = processEnvironment(environment({ VERO_TEST_VALUE: '中文 value' }));
	assert.equal(result.VERO_PATH, '');
	assert.equal(result.VERO_TEST_VALUE, '中文 value');
	assert.equal(process.env.VERO_PATH, before);
	if (process.platform === 'win32') {
		assert.throws(() => environment({ vero_stdlib_path: 'stdlib' }));
		assert.deepEqual(environment({ Path: 'a', PATH: 'b' }), { PATH: 'b' });
	}
});

test('explicit invalid Runtime never falls back to PATH', async () => {
	await assert.rejects(new RuntimeResolver().resolve(path.join(base, 'missing-runtime')));
});

test('watch roots retain one ancestor when paths repeat or overlap', () => {
	const sibling = path.resolve(base, '../sibling');
	assert.deepEqual(watchDirectories([path.join(base, 'Scripts'), base, base, sibling]), [base, sibling]);
	if (process.platform === 'win32') {
		assert.deepEqual(watchDirectories([base, base.toUpperCase()]), [base]);
	}
});

test('real Runtime preserves Unicode and shell characters in script argv', { skip: !process.env.VEROSCRIPT_TEST_RUNTIME }, async () => {
	const runtime = await new RuntimeResolver().resolve(process.env.VEROSCRIPT_TEST_RUNTIME!);
	const parent = path.resolve('../../../../.tmp/vscode-plugin/unit');
	await fs.mkdir(parent, { recursive: true });
	const directory = await fs.mkdtemp(path.join(parent, 'argv-'));
	const program = path.join(directory, '参数 echo.vs');
	await fs.writeFile(program, 'void main(string[] args) { for (int64 i = 0; i < len(args); ++i) { print(args[i]); } }');
	const args = ['a b', '中文', '--an-argument', 'quote"', "single'", '$(literal)', '`literal`'];
	const options = mergedCompileOptions(defaults, {}, directory, runtime.stdlib);
	const execute = (action: 'run' | 'check') => new Promise<string>((resolve, reject) => {
		// Keep argv validation independent of GUI cold compilation triggered by unused preprocessor inputs.
		execFile(runtime.cli, cliArguments(options, program, action, 'vm', args), { cwd: directory, env: processEnvironment(), encoding: 'utf8', timeout: 180000 }, (error, stdout, stderr) => error ? reject(new Error(stderr || error.message)) : resolve(stdout));
	});
	assert.equal((await execute('run')).trim().replaceAll('\r\n', '\n'), args.join('\n'));
	assert.equal((await execute('check')).trim(), '');
	assert.equal(runtime.version, '0.1.1');
});

test('real Runtime loads an explicit independent JIT file and directory', {
	skip: !process.env.VEROSCRIPT_TEST_RUNTIME || !process.env.VEROSCRIPT_TEST_JIT_BACKEND
}, async () => {
	const runtime = await new RuntimeResolver().resolve(process.env.VEROSCRIPT_TEST_RUNTIME!);
	const module = path.resolve(process.env.VEROSCRIPT_TEST_JIT_BACKEND!);
	const parent = path.resolve('../../../../.tmp/vscode-plugin/unit');
	await fs.mkdir(parent, { recursive: true });
	const directory = await fs.mkdtemp(path.join(parent, 'jit-'));
	const program = path.join(directory, 'jit.vs');
	await fs.writeFile(program, 'int value() { return 42; } void main() { print(value()); }');
	const options = mergedCompileOptions(defaults, {}, directory, runtime.stdlib);
	for (const backendPath of [module, path.dirname(module)]) {
		const output = await new Promise<string>((resolve, reject) => {
			execFile(runtime.cli, cliArguments(options, program, 'run', 'jit', [], backendPath), {
				cwd: directory, env: processEnvironment(), encoding: 'utf8', timeout: 10000
			}, (error, stdout, stderr) => error ? reject(new Error(stderr || error.message)) : resolve(stdout));
		});
		assert.equal(output.trim(), '42');
	}
});
