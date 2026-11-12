// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as path from 'node:path';

export interface ConfigurationReader {
	get<T>(section: string, defaultValue: T): T;
}

export interface PredefinedMacro {
	readonly name: string;
	readonly parameters: readonly string[];
	readonly variadic: boolean;
	readonly body: string;
}

export interface CompileOptions {
	readonly stdlibSearchPaths: readonly string[];
	readonly importPaths: readonly string[];
	readonly implicitImportPaths: readonly string[];
	readonly defines: readonly string[];
	readonly predefinedMacros: readonly PredefinedMacro[];
	readonly bindingCatalogPaths: readonly string[];
	readonly allowCircularImports: boolean;
	readonly allowProjectWideAccess: boolean;
}

export interface RuntimeSettings extends CompileOptions {
	readonly runtimePath: string;
}

export interface AnalysisSettings extends RuntimeSettings {
	readonly enabled: boolean;
	readonly executablePath: string;
	readonly arguments: readonly string[];
	readonly limits: Readonly<Record<string, number | boolean>>;
}

export interface Settings extends RuntimeSettings {
	readonly backend: string;
	readonly jitBackendPath: string;
	readonly args: readonly string[];
	readonly cwd: string;
	readonly env: Readonly<Record<string, string>>;
}

export const limitDefaults: Readonly<Record<string, readonly [number, number, number]>> = {
	lspRequestTimeoutMs: [2000, 50, 30000],
	lspAnalysisTimeoutMs: [5000, 50, 30000],
	maxCompletionItems: [512, 32, 5000],
	maxWorkspaceSymbolResults: [2000, 64, 20000],
	maxReferenceResults: [5000, 64, 50000],
	maxWorkspaceFiles: [10000, 1, 100000],
	maxWorkspaceFileBytes: [2097152, 1024, 67108864]
};

export const compileFields = [
	'stdlibSearchPaths', 'importPaths', 'implicitImportPaths', 'defines',
	'predefinedMacros', 'bindingCatalogPaths', 'allowCircularImports', 'allowProjectWideAccess'
] as const;

export function text(value: unknown, label: string): string {
	if (typeof value !== 'string' || value.includes('\0')) {
		throw new Error(`${label}: expected a string without NUL.`);
	}
	return value;
}

export function strings(value: unknown, label: string): string[] {
	if (!Array.isArray(value)) {
		throw new Error(`${label}: expected an array of strings.`);
	}
	return value.map((item: unknown) => text(item, label));
}

export function boolean(value: unknown, label: string): boolean {
	if (typeof value !== 'boolean') {
		throw new Error(`${label}: expected a boolean.`);
	}
	return value;
}

export function expand(value: string, base: string, file?: string): string {
	return value.replace(/\$\{([^}]+)\}/g, (_, variable: string) => {
		if (variable === 'workspaceFolder') return base;
		if (variable === 'file' && file) return file;
		if (variable === 'fileDirname' && file) return path.dirname(file);
		if (variable.startsWith('env:')) {
			const result = process.env[variable.slice(4)];
			if (result !== undefined) return result;
		}
		throw new Error(`Unresolved variable: ${variable}`);
	});
}

export function resolvePath(value: unknown, base: string, file?: string, workspaceBase = base): string {
	const result = expand(text(value, 'path'), workspaceBase, file);
	if (result.length === 0) {
		throw new Error('A path must not be empty.');
	}
	return path.resolve(base, result);
}

export function environment(value: unknown): Record<string, string> {
	if (!value || typeof value !== 'object' || Array.isArray(value)) {
		throw new Error('env: expected an object of strings.');
	}
	const result: Record<string, string> = {};
	for (const [key, entry] of Object.entries(value)) {
		if (!key || /[=\0]/.test(key)) {
			throw new Error('Invalid environment variable name.');
		}
		const comparison = process.platform === 'win32' ? key.toUpperCase() : key;
		if (comparison === 'VERO_PATH' || comparison === 'VERO_STDLIB_PATH') {
			throw new Error(`Use VeroScript path settings instead of ${key}.`);
		}
		for (const previous of Object.keys(result)) {
			if (process.platform === 'win32' && previous.toUpperCase() === comparison) {
				delete result[previous];
			}
		}
		Object.defineProperty(result, key, { value: text(entry, `env.${key}`), enumerable: true, writable: true, configurable: true });
	}
	return result;
}

export function processEnvironment(overrides: Readonly<Record<string, string>> = {}): NodeJS.ProcessEnv {
	const result = { ...process.env };
	const additions = { VERO_PATH: '', VERO_STDLIB_PATH: '', ...overrides };
	for (const [key, value] of Object.entries(additions)) {
		for (const previous of Object.keys(result)) {
			if (process.platform === 'win32' && previous.toUpperCase() === key.toUpperCase()) {
				delete result[previous];
			}
		}
		result[key] = value;
	}
	return result;
}

export function macros(value: unknown): PredefinedMacro[] {
	if (!Array.isArray(value)) {
		throw new Error('predefinedMacros: expected an array.');
	}
	const names = new Set<string>();
	return value.map((item: unknown) => {
		if (!item || typeof item !== 'object' || Array.isArray(item)) {
			throw new Error('Invalid predefined macro.');
		}
		const entry = item as Record<string, unknown>;
		const name = text(entry.name, 'macro.name');
		const parameters = strings(entry.parameters ?? [], 'macro.parameters');
		if (!/^[A-Za-z_]\w*$/.test(name) || names.has(name)
			|| parameters.some(parameter => !/^[A-Za-z_]\w*$/.test(parameter))
			|| new Set(parameters).size !== parameters.length || entry.sourceName !== undefined) {
			throw new Error(`Invalid or duplicate predefined macro: ${name}`);
		}
		names.add(name);
		return { name, parameters, variadic: boolean(entry.variadic ?? false, 'macro.variadic'), body: text(entry.body, 'macro.body') };
	});
}

export function backend(value: unknown): string {
	if (value !== 'vm' && value !== 'mixed' && value !== 'jit') {
		throw new Error('backend: expected vm, mixed or jit.');
	}
	return value;
}

export function compileOptions(values: Record<string, unknown>, base: string): CompileOptions {
	const paths = (key: string) => strings(values[key] ?? [], key).map(item => resolvePath(item, base));
	return {
		stdlibSearchPaths: paths('stdlibSearchPaths'), importPaths: paths('importPaths'),
		implicitImportPaths: paths('implicitImportPaths'), bindingCatalogPaths: paths('bindingCatalogPaths'),
		defines: strings(values.defines ?? [], 'defines'), predefinedMacros: macros(values.predefinedMacros ?? []),
		allowCircularImports: boolean(values.allowCircularImports ?? true, 'allowCircularImports'),
		allowProjectWideAccess: boolean(values.allowProjectWideAccess ?? false, 'allowProjectWideAccess')
	};
}

export function requireWorkspaceContext(value: unknown, available: boolean): void {
	if (available) return;
	if (typeof value === 'string' && value.includes('${workspaceFolder}')) {
		throw new Error('workspaceFolder requires an owning workspace folder.');
	}
	if (value && typeof value === 'object') {
		for (const child of Object.values(value)) {
			requireWorkspaceContext(child, false);
		}
	}
}

function readCompileOptions(reader: ConfigurationReader, base: string, hasWorkspace: boolean): CompileOptions {
	const values: Record<string, unknown> = {};
	for (const key of compileFields) {
		values[key] = reader.get<unknown>(key, undefined);
		if (key.endsWith('Paths')) {
			requireWorkspaceContext(values[key], hasWorkspace);
		}
	}
	return compileOptions(values, base);
}

export function readRuntimeSettings(reader: ConfigurationReader, base: string, hasWorkspace = true): RuntimeSettings {
	const runtime = text(reader.get('runtimePath', ''), 'runtimePath');
	requireWorkspaceContext(runtime, hasWorkspace);
	return { ...readCompileOptions(reader, base, hasWorkspace), runtimePath: runtime ? resolvePath(runtime, base) : '' };
}

export function readAnalysisSettings(reader: ConfigurationReader, base: string, hasWorkspace = true): AnalysisSettings {
	const limits: Record<string, number | boolean> = {};
	for (const [key, [fallback, minimum, maximum]] of Object.entries(limitDefaults)) {
		const value = reader.get<unknown>(key, fallback);
		if (typeof value !== 'number' || !Number.isInteger(value) || value < minimum || value > maximum) {
			throw new Error(`${key}: expected an integer in ${minimum}..${maximum}.`);
		}
		limits[key] = value;
	}
	limits.lspTraceAnalysis = boolean(reader.get('lspTraceAnalysis', false), 'lspTraceAnalysis');
	const executable = text(reader.get('lsp.executablePath', ''), 'lsp.executablePath');
	requireWorkspaceContext(executable, hasWorkspace);
	const executablePath = executable ? resolvePath(executable, base) : '';
	const options = executablePath
		? { ...readCompileOptions(reader, base, hasWorkspace), runtimePath: '' }
		: readRuntimeSettings(reader, base, hasWorkspace);
	return {
		...options, limits, enabled: boolean(reader.get('lsp.enabled', true), 'lsp.enabled'),
		executablePath, arguments: executablePath ? strings(reader.get('lsp.arguments', []), 'lsp.arguments') : []
	};
}

export function readSettings(reader: ConfigurationReader, base: string, file?: string, hasWorkspace = true): Settings {
	const cwd = text(reader.get('run.cwd', ''), 'run.cwd');
	const jitBackendPath = text(reader.get('run.jitBackendPath', ''), 'run.jitBackendPath');
	requireWorkspaceContext(cwd, hasWorkspace);
	requireWorkspaceContext(jitBackendPath, hasWorkspace);
	return {
		...readRuntimeSettings(reader, base, hasWorkspace),
		backend: backend(reader.get('run.backend', 'mixed')),
		jitBackendPath: jitBackendPath ? resolvePath(jitBackendPath, base, file) : '',
		args: strings(reader.get('run.args', []), 'run.args'),
		cwd: cwd ? resolvePath(cwd, base, file) : base,
		env: environment(reader.get('run.env', {}))
	};
}

export function mergedCompileOptions(settings: CompileOptions, overrides: Record<string, unknown>, base: string, stdlib: string): CompileOptions {
	const values: Record<string, unknown> = {};
	for (const key of compileFields) {
		values[key] = overrides[key] === undefined ? settings[key] : overrides[key];
	}
	const result = compileOptions(values, base);
	return { ...result, stdlibSearchPaths: result.stdlibSearchPaths.length ? result.stdlibSearchPaths : [stdlib] };
}

export function compileArguments(options: CompileOptions): string[] {
	const result: string[] = [];
	const add = (flag: string, values: readonly string[]) => {
		for (const value of values) {
			result.push(flag, text(value, flag));
		}
	};
	add('--stdlib-path', options.stdlibSearchPaths);
	add('-I', options.importPaths);
	add('--implicit-import', options.implicitImportPaths);
	add('--binding-catalog', options.bindingCatalogPaths);
	add('-D', options.defines);
	for (const macro of options.predefinedMacros) {
		const parameters = [...macro.parameters, ...(macro.variadic ? ['...'] : [])];
		result.push('--macro', `${macro.name}(${parameters.join(',')})=${macro.body}`);
	}
	if (!options.allowCircularImports) {
		result.push('--no-circular-imports');
	}
	if (options.allowProjectWideAccess) {
		result.push('--allow-project-wide-access');
	}
	return result;
}

export function cliArguments(options: CompileOptions, program: string, action: 'run' | 'check', mode: string, args: readonly string[], jitBackendPath = ''): string[] {
	const result = [`--backend=${backend(mode)}`];
	if (action === 'run' && jitBackendPath) {
		result.push('--jit-backend', text(jitBackendPath, 'jitBackendPath'));
	}
	result.push(...compileArguments(options));
	if (action === 'check') {
		result.push('--check');
	}
	result.push(text(program, 'program'));
	if (action === 'run') {
		result.push(...args.map(value => text(value, 'args')));
	}
	return result;
}

export function containsPath(parent: string, file: string): boolean {
	const relative = path.relative(parent, file);
	return relative === '' || (relative !== '..' && !relative.startsWith(`..${path.sep}`) && !path.isAbsolute(relative));
}

export function globLiteral(value: string): string {
	return value.replace(/[?*\[\]{}]/g, character => `[${character}]`);
}

export function watchDirectories(paths: readonly string[]): string[] {
	const unique: string[] = [];
	for (const directory of paths) {
		if (!unique.some(other => path.relative(other, directory) === '')) {
			unique.push(directory);
		}
	}
	return unique.filter(directory => !unique.some(other => other !== directory && containsPath(other, directory)));
}
