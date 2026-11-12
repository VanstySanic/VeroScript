// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

export interface VeroScriptEngineModule {
	_vero_web_memory_operation?(
		runtime: number, operation: number, a: number, b: number,
		x: bigint, y: bigint, z: bigint, value: bigint,
	): number;
	_vero_web_memory_result?(runtime: number, field: number): bigint;
	_vero_web_set_stdlib_limits?(runtime: number, maxTextBytes: bigint, maxReadBytes: bigint, maxSplitParts: bigint): number;
	_vero_web_memory_release_lease?(runtimeToken: bigint, serialToken: bigint): number;
	_malloc(size: number): number;
	_free(pointer: number): void;
	_vero_web_abi_version(): number;
	/** Reports thread support; an omitted field means non-threaded. */
	_vero_web_supports_threads?(): number;
	_vero_web_create(): number;
	_vero_web_destroy(runtime: number): void;
	_vero_web_set_language(runtime: number, languageTag: number): number;
	_vero_web_get_language(runtime: number): number;
	_vero_web_get_available_languages(runtime: number): number;
	_vero_web_run_source(
		runtime: number,
		source: number,
		fileLabel: number,
		input: number,
	): number;
	_vero_web_run_source_with_args(
		runtime: number,
		source: number,
		fileLabel: number,
		input: number,
		args: number,
		argCount: number,
	): number;
	_vero_web_run_source_with_options(
		runtime: number,
		source: number,
		fileLabel: number,
		input: number,
		args: number,
		argCount: number,
		backend: number,
	): number;
	_vero_web_run_source_with_context(
		runtime: number,
		source: number,
		fileLabel: number,
		input: number,
		args: number,
		argCount: number,
		backend: number,
		filePaths: number,
		fileSources: number,
		fileCount: number,
	): number;
	_vero_web_compile_source_with_context?(
		runtime: number,
		source: number,
		fileLabel: number,
		filePaths: number,
		fileSources: number,
		fileCount: number,
	): number;
	_vero_web_run_program?(
		runtime: number,
		programHandle: number,
		input: number,
		args: number,
		argCount: number,
		backend: number,
	): number;
	_vero_web_run_program_interactive?(
		runtime: number,
		programHandle: number,
		args: number,
		argCount: number,
		backend: number,
	): number;
	_vero_web_destroy_program?(runtime: number, programHandle: number): number;
	_vero_web_run_source_interactive_with_context?(
		runtime: number,
		source: number,
		fileLabel: number,
		args: number,
		argCount: number,
		backend: number,
		filePaths: number,
		fileSources: number,
		fileCount: number,
	): number;
	_vero_web_check_source(
		runtime: number,
		source: number,
		fileLabel: number,
	): number;
	_vero_web_check_source_with_context(
		runtime: number,
		source: number,
		fileLabel: number,
		filePaths: number,
		fileSources: number,
		fileCount: number,
	): number;
	UTF8ToString(pointer: number): string;
	stringToUTF8(value: string, pointer: number, capacity: number): void;
	lengthBytesUTF8(value: string): number;
	HEAPU32: Uint32Array;
	ccall(
		name: string,
		returnType: "number",
		argumentTypes: readonly "number"[],
		arguments_: readonly number[],
		options: { async: true },
	): Promise<number>;
	/** Internal bridge installed for the duration of an interactive source/program run. */
	veroWebInputProvider?: (runtime: number) => Promise<string | null>;
}

export interface VeroScriptEngineModuleOptions {
	locateFile?: (path: string, prefix: string) => string;
	wasmBinary?: ArrayBuffer | Uint8Array;
}

export default function createVeroScriptEngine(
	options?: VeroScriptEngineModuleOptions,
): Promise<VeroScriptEngineModule>;
