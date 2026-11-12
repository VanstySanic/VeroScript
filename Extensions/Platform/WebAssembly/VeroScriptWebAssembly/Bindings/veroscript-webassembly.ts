// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import type {
	VeroScriptEngineModule,
	VeroScriptEngineModuleOptions,
} from "./veroscript-engine.mjs";

export interface VeroScriptDiagnostic {
	severity: VeroScriptDiagnosticSeverity;
	message: string;
	code: string;
	file: string;
	line: number;
	column: number;
}

export interface VeroScriptResult {
	abiVersion: string;
	operation: "compile" | "run" | "check" | "api";
	success: boolean;
	exitCode: number;
	exitRequested: boolean;
	/** Requested run policy; null for check/API errors. Mixed uses VM without a backend. */
	backend: VeroScriptExecutionBackend | null;
	output: string;
	errorOutput: string;
	diagnostics: VeroScriptDiagnostic[];
	programHandle: number | null;
	timings: { compileMs: number | null; executeMs: number | null } | null;
	/** Per-run JIT activity. Published modules have no backend and report zeroed counters. */
	jit: VeroScriptJitStatistics | null;
}

export interface VeroScriptJitStatistics {
	compileAttempts: number;
	compiledEntries: number;
	bridgeTrampolineEntries: number;
	linearTrampolineEntries: number;
	vmTrampolineEntries: number;
	unsupportedEntries: number;
	dispatchAttempts: number;
	dispatches: number;
	fallbacks: number;
	hotFunctions: number;
	directNativeCalls: number;
}

export interface VeroScriptSourceOptions {
	fileLabel?: string;
	/** Additional in-memory modules used to resolve imports. */
	files?: readonly VeroScriptVirtualFile[];
}

export interface VeroScriptVirtualFile {
	path: string;
	source: string;
}

export interface VeroScriptRunOptions extends VeroScriptSourceOptions {
	input?: string;
	/** Arguments passed to `main(string[] args)`. */
	args?: readonly string[];
	/** Defaults to mixed (VM execution). jit returns a structured missing-backend failure. */
	backend?: VeroScriptExecutionBackend;
}

export interface VeroScriptInputRequest {
	/** Zero-based request index for this run. */
	index: number;
}

export interface VeroScriptInteractiveRunOptions extends VeroScriptSourceOptions {
	/** Arguments passed to `main(string[] args)`. */
	args?: readonly string[];
	/** Defaults to mixed (VM execution). jit returns a structured missing-backend failure. */
	backend?: VeroScriptExecutionBackend;
	/**
	 * Supply one submitted line. The SDK waits for the returned promise without
	 * blocking the browser event loop; null signals end-of-input.
	 * Provider failures stop this execution and reject with the original error.
	 */
	readLine(request: VeroScriptInputRequest): string | null | Promise<string | null>;
}

export interface VeroScriptProgram {
	readonly disposed: boolean;
	run(options?: VeroScriptProgramRunOptions): VeroScriptResult;
	runInteractive(options: VeroScriptProgramInteractiveRunOptions): Promise<VeroScriptResult>;
	dispose(): void;
}

export interface VeroScriptCompileResult {
	result: VeroScriptResult;
	program: VeroScriptProgram | null;
}

export interface VeroScriptRuntime {
	readonly memory: VeroScriptMemory;
	readonly abiVersion: string;
	/** Whether this module can suspend execution while awaiting frontend input. */
	readonly supportsInteractiveInput: boolean;
	/** Whether this artifact supports script-level pthread-backed concurrency. */
	readonly supportsThreads: boolean;
	/** Whether this artifact supports compile-once, run-many program handles. */
	readonly supportsCompiledPrograms: boolean;
	readonly language: VeroScriptLanguage;
	readonly availableLanguages: readonly VeroScriptLanguage[];
	setLanguage(language: VeroScriptLanguage): void;
	/**
	 * Per-operation text bytes, bulk-read bytes and split-part limits, in that order.
	 * Zero adds no host limit. Applies to subsequent runs, including existing programs.
	 * These limits are independent from managed-memory quotas and Core numeric limits.
	 */
	setStandardLibraryLimits(maxTextBytes?: bigint, maxReadBytes?: bigint, maxSplitParts?: bigint): void;
	runSource(source: string, options?: VeroScriptRunOptions): VeroScriptResult;
	compileSource(source: string, options?: VeroScriptSourceOptions): VeroScriptCompileResult;
	runSourceInteractive(
		source: string,
		options: VeroScriptInteractiveRunOptions,
	): Promise<VeroScriptResult>;
	checkSource(source: string, options?: VeroScriptSourceOptions): VeroScriptResult;
	dispose(): void;
}

export type VeroScriptDiagnosticSeverity = "error" | "warning" | "info" | "hint";

export type VeroScriptExecutionBackend = "vm" | "mixed" | "jit";

/** Canonical BCP 47 tag discovered from the deployed localization catalogs. */
export type VeroScriptLanguage = string;

export type VeroScriptProgramRunOptions = Omit<VeroScriptRunOptions, "fileLabel" | "files">;

export type VeroScriptProgramInteractiveRunOptions = Omit<
	VeroScriptInteractiveRunOptions,
	"fileLabel" | "files"
>;

export type VeroScriptMemoryScalar =
	| "int8" | "uint8" | "int16" | "uint16" | "int32" | "uint32"
	| "int64" | "uint64" | "float" | "double";
type MemoryCall = (
	op: number, a: number, b: number, x: bigint, y: bigint, z: bigint, n: bigint,
) => bigint[];

const expectedAbiVersion = "0.1.1";
const maxWasm32AllocationBytes = 0xffffffff;

function backendId(backend: VeroScriptExecutionBackend): number {
	switch (backend) {
		case "vm":
			return 0;
		case "mixed":
			return 1;
		case "jit":
			return 2;
		default: {
			throw new Error(`Unsupported VeroScript execution backend: ${String(backend)}`);
		}
	}
}

function withCString<T>(
	module: VeroScriptEngineModule,
	value: string,
	callback: (pointer: number) => T,
): T {
	if (value.includes("\0")) {
		throw new Error("VeroScript WebAssembly strings must not contain NUL characters");
	}
	const capacity = cStringCapacity(module, value);
	const pointer = module._malloc(capacity);
	if (pointer === 0) {
		throw new Error("VeroScript WebAssembly string allocation failed");
	}
	try {
		module.stringToUTF8(value, pointer, capacity);
		return callback(pointer);
	}
	finally
	{
		module._free(pointer);
	}
}

async function withCStringAsync<T>(
	module: VeroScriptEngineModule,
	value: string,
	callback: (pointer: number) => T | Promise<T>,
): Promise<T> {
	if (value.includes("\0")) {
		throw new Error("VeroScript WebAssembly strings must not contain NUL characters");
	}
	const capacity = cStringCapacity(module, value);
	const pointer = module._malloc(capacity);
	if (pointer === 0) {
		throw new Error("VeroScript WebAssembly string allocation failed");
	}
	try {
		module.stringToUTF8(value, pointer, capacity);
		return await callback(pointer);
	}
	finally
	{
		module._free(pointer);
	}
}

function cStringCapacity(module: VeroScriptEngineModule, value: string): number {
	const byteLength = module.lengthBytesUTF8(value);
	if (
		!Number.isSafeInteger(byteLength) ||
		byteLength < 0 ||
		byteLength >= maxWasm32AllocationBytes
	) {
		throw new RangeError("VeroScript WebAssembly string exceeds the Wasm32 allocation limit");
	}
	return byteLength + 1;
}

function parseResult(module: VeroScriptEngineModule, pointer: number): VeroScriptResult {
	if (pointer === 0) {
		throw new Error("VeroScript WebAssembly returned a null result");
	}
	const parsed: unknown = JSON.parse(module.UTF8ToString(pointer));
	if (typeof parsed !== "object" || parsed === null) {
		throw new Error("VeroScript WebAssembly returned an invalid result");
	}
	const result = parsed as Partial<VeroScriptResult>;
	// Compiled-program support is optional and is discovered from these fields.
	result.programHandle ??= null;
	result.timings ??= null;
	result.jit ??= null;
	if (result.jit !== null && typeof result.jit === "object") {
		// Omitted optional directNativeCalls telemetry defaults to zero.
		result.jit.directNativeCalls ??= 0;
	}
	const diagnosticsAreValid =
		Array.isArray(result.diagnostics) &&
		result.diagnostics.every(
			(diagnostic) =>
				typeof diagnostic === "object" &&
				diagnostic !== null &&
				(diagnostic.severity === "error" ||
					diagnostic.severity === "warning" ||
					diagnostic.severity === "info" ||
					diagnostic.severity === "hint") &&
				typeof diagnostic.message === "string" &&
				typeof diagnostic.code === "string" &&
				typeof diagnostic.file === "string" &&
				Number.isInteger(diagnostic.line) &&
				diagnostic.line >= 0 &&
				Number.isInteger(diagnostic.column) &&
				diagnostic.column >= 0,
		);
	if (
		result.abiVersion !== expectedAbiVersion ||
		(result.operation !== "compile" &&
			result.operation !== "run" &&
			result.operation !== "check" &&
			result.operation !== "api") ||
		typeof result.abiVersion !== "string" ||
		typeof result.success !== "boolean" ||
		typeof result.exitCode !== "number" ||
		!Number.isInteger(result.exitCode) ||
		typeof result.exitRequested !== "boolean" ||
		(result.backend !== null &&
			result.backend !== "vm" &&
			result.backend !== "mixed" &&
			result.backend !== "jit") ||
		typeof result.output !== "string" ||
		typeof result.errorOutput !== "string" ||
		(result.programHandle !== null &&
			(!Number.isInteger(result.programHandle) || result.programHandle <= 0)) ||
		(result.timings !== null &&
			(typeof result.timings !== "object" ||
				(result.timings.compileMs !== null &&
					(typeof result.timings.compileMs !== "number" ||
						!Number.isFinite(result.timings.compileMs) ||
						result.timings.compileMs < 0)) ||
				(result.timings.executeMs !== null &&
					(typeof result.timings.executeMs !== "number" ||
						!Number.isFinite(result.timings.executeMs) ||
						result.timings.executeMs < 0)))) ||
		(result.jit !== null &&
			(typeof result.jit !== "object" ||
				![
					result.jit.compileAttempts,
					result.jit.compiledEntries,
					result.jit.bridgeTrampolineEntries,
					result.jit.linearTrampolineEntries,
					result.jit.vmTrampolineEntries,
					result.jit.unsupportedEntries,
					result.jit.dispatchAttempts,
					result.jit.dispatches,
					result.jit.fallbacks,
					result.jit.hotFunctions,
					result.jit.directNativeCalls,
				].every((value) => Number.isSafeInteger(value) && value >= 0))) ||
		!diagnosticsAreValid
	) {
		throw new Error("VeroScript WebAssembly returned a malformed result");
	}
	return result as VeroScriptResult;
}

function cStringTableBytes(count: number): number {
	const bytes = count * Uint32Array.BYTES_PER_ELEMENT;
	if (!Number.isSafeInteger(count) || count < 0 ||
			!Number.isSafeInteger(bytes) || bytes > maxWasm32AllocationBytes) {
		throw new RangeError("VeroScript WebAssembly string table exceeds the Wasm32 allocation limit");
	}
	return bytes;
}

function withCStringArray<T>(
	module: VeroScriptEngineModule,
	values: readonly string[],
	callback: (pointer: number, count: number) => T,
): T {
	cStringTableBytes(values.length);

	const pointers: number[] = [];
	let tablePointer = 0;
	try {
		for (const value of values) {
			if (value.includes("\0")) {
				throw new Error("VeroScript WebAssembly strings must not contain NUL characters");
			}
			const capacity = cStringCapacity(module, value);
			const pointer = module._malloc(capacity);
			if (pointer === 0) {
				throw new Error("VeroScript WebAssembly argument allocation failed");
			}
			pointers.push(pointer);
			module.stringToUTF8(value, pointer, capacity);
		}

		// Custom/live array iteration can differ from the initial declared length.
		const tableBytes = cStringTableBytes(pointers.length);
		if (tableBytes === 0) {
			return callback(0, 0);
		}
		tablePointer = module._malloc(tableBytes);
		if (tablePointer === 0) {
			throw new Error("VeroScript WebAssembly argument table allocation failed");
		}
		module.HEAPU32.set(pointers, tablePointer >>> 2);
		return callback(tablePointer, pointers.length);
	}
	finally
	{
		if (tablePointer !== 0) {
			module._free(tablePointer);
		}
		for (const pointer of pointers) {
			module._free(pointer);
		}
	}
}

function withOptionalCString<T>(
	module: VeroScriptEngineModule,
	value: string,
	callback: (pointer: number) => T,
): T {
	return value.length === 0 ? callback(0) : withCString(module, value, callback);
}

async function withOptionalCStringAsync<T>(
	module: VeroScriptEngineModule,
	value: string,
	callback: (pointer: number) => T | Promise<T>,
): Promise<T> {
	return value.length === 0 ? await callback(0) : withCStringAsync(module, value, callback);
}

async function withCStringArrayAsync<T>(
	module: VeroScriptEngineModule,
	values: readonly string[],
	callback: (pointer: number, count: number) => T | Promise<T>,
): Promise<T> {
	cStringTableBytes(values.length);

	const pointers: number[] = [];
	let tablePointer = 0;
	try {
		for (const value of values) {
			if (value.includes("\0")) {
				throw new Error("VeroScript WebAssembly strings must not contain NUL characters");
			}
			const capacity = cStringCapacity(module, value);
			const pointer = module._malloc(capacity);
			if (pointer === 0) {
				throw new Error("VeroScript WebAssembly argument allocation failed");
			}
			pointers.push(pointer);
			module.stringToUTF8(value, pointer, capacity);
		}
		const tableBytes = cStringTableBytes(pointers.length);
		if (tableBytes === 0) {
			return await callback(0, 0);
		}
		tablePointer = module._malloc(tableBytes);
		if (tablePointer === 0) {
			throw new Error("VeroScript WebAssembly argument table allocation failed");
		}
		module.HEAPU32.set(pointers, tablePointer >>> 2);
		return await callback(tablePointer, pointers.length);
	}
	finally
	{
		if (tablePointer !== 0) {
			module._free(tablePointer);
		}
		for (const pointer of pointers) {
			module._free(pointer);
		}
	}
}

class Runtime implements VeroScriptRuntime {
	//~ Begin VeroScriptRuntime Interface
	get language(): VeroScriptLanguage {
		return this.currentLanguage;
	}

	get availableLanguages(): readonly VeroScriptLanguage[] {
		this.ensureReady();
		const pointer = this.module._vero_web_get_available_languages(this.handle);
		if (pointer === 0) {
			throw new Error("Unable to query VeroScript localization catalogs");
		}
		const parsed: unknown = JSON.parse(this.module.UTF8ToString(pointer));
		if (!Array.isArray(parsed) || !parsed.every((value) => typeof value === "string")) {
			throw new Error("VeroScript WebAssembly returned malformed language metadata");
		}
		return parsed;
	}

	setLanguage(language: VeroScriptLanguage): void {
		this.ensureReady();
		withCString(this.module, language, (languagePointer) => {
			if (this.module._vero_web_set_language(this.handle, languagePointer) === 0) {
				throw new Error(`Unsupported VeroScript language: ${language}`);
			}
		});
		const selectedPointer = this.module._vero_web_get_language(this.handle);
		if (selectedPointer === 0) {
			throw new Error("Unable to query the selected VeroScript language");
		}
		this.currentLanguage = this.module.UTF8ToString(selectedPointer);
	}

	runSource(source: string, options: VeroScriptRunOptions = {}): VeroScriptResult {
		this.ensureReady();
		const selectedBackend = options.backend ?? "mixed";
		const selectedBackendId = backendId(selectedBackend);
		const files = options.files ?? [];
		const filePaths = files.map((file) => file.path);
		const fileSources = files.map((file) => file.source);
		const result = withCString(this.module, source, (sourcePointer) =>
			withOptionalCString(this.module, options.fileLabel ?? "", (labelPointer) =>
				withOptionalCString(this.module, options.input ?? "", (inputPointer) =>
					withCStringArray(this.module, options.args ?? [], (argsPointer, argCount) =>
						withCStringArray(this.module, filePaths, (filePathsPointer, fileCount) =>
							withCStringArray(this.module, fileSources, (fileSourcesPointer, sourceCount) => {
								if (fileCount !== sourceCount) {
									throw new Error("VeroScript virtual file path/source count mismatch");
								}
								return parseResult(
									this.module,
									this.module._vero_web_run_source_with_context(
										this.handle,
										sourcePointer,
										labelPointer,
										inputPointer,
										argsPointer,
										argCount,
										selectedBackendId,
										filePathsPointer,
										fileSourcesPointer,
										fileCount,
									),
								);
							}),
						),
					),
				),
			),
		);
		if (result.operation === "run" && result.backend !== selectedBackend) {
			throw new Error(
				`VeroScript WebAssembly selected backend ${String(result.backend)}; expected ${selectedBackend}`,
			);
		}
		return result;
	}

	compileSource(
		source: string,
		options: VeroScriptSourceOptions = {},
	): VeroScriptCompileResult {
		this.ensureReady();
		const compile = this.module._vero_web_compile_source_with_context;
		if (!this.supportsCompiledPrograms || compile === undefined) {
			throw new Error("This VeroScript module does not support compiled program handles");
		}
		const files = options.files ?? [];
		const filePaths = files.map((file) => file.path);
		const fileSources = files.map((file) => file.source);
		const result = withCString(this.module, source, (sourcePointer) =>
			withOptionalCString(this.module, options.fileLabel ?? "", (labelPointer) =>
				withCStringArray(this.module, filePaths, (filePathsPointer, fileCount) =>
					withCStringArray(this.module, fileSources, (fileSourcesPointer, sourceCount) => {
						if (fileCount !== sourceCount) {
							throw new Error("VeroScript virtual file path/source count mismatch");
						}
						return parseResult(
							this.module,
							compile(
								this.handle,
								sourcePointer,
								labelPointer,
								filePathsPointer,
								fileSourcesPointer,
								fileCount,
							),
						);
					}),
				),
			),
		);
		if (!result.success) return { result, program: null };
		if (result.operation !== "compile" || result.programHandle === null) {
			throw new Error("VeroScript WebAssembly returned no compiled program handle");
		}
		const program = new Program(this, result.programHandle);
		this.programs.add(program);
		return { result, program };
	}

	async runSourceInteractive(
		source: string,
		options: VeroScriptInteractiveRunOptions,
	): Promise<VeroScriptResult> {
		this.ensureAlive();
		if (!this.supportsInteractiveInput) {
			throw new Error(
				"This VeroScript runtime uses the fast module and does not support interactive input",
			);
		}
		if (typeof options?.readLine !== "function") {
			throw new Error("VeroScript interactive execution requires a readLine provider");
		}
		if (this.module.veroWebInputProvider !== undefined) {
			throw new Error("A VeroScript interactive execution is already active on this module");
		}

		const selectedBackend = options.backend ?? "mixed";
		const selectedBackendId = backendId(selectedBackend);
		const files = options.files ?? [];
		const filePaths = files.map((file) => file.path);
		const fileSources = files.map((file) => file.source);
		// Option getters can start another run or dispose this runtime.
		// Recheck before taking ownership of the module's input provider.
		this.ensureReady();
		let requestIndex = 0;
		let providerFailure: unknown;
		let providerFailed = false;
		// The bridge reports marked failures on the resumed native stack.
		this.module.veroWebInputProvider = Object.assign(async (runtimeHandle: number) => {
			if (runtimeHandle !== this.handle) {
				return null;
			}
			try {
				const value = await options.readLine({ index: requestIndex++ });
				if (value !== null && typeof value !== "string") {
					throw new TypeError("VeroScript readLine must return a string or null");
				}
				if (value?.includes("\0")) {
					throw new TypeError("VeroScript interactive input lines must not contain NUL characters");
				}
				return value;
			}
			catch (error)
			{
				providerFailed = true;
				providerFailure = error;
				throw error;
			}
		}, { __veroWebAbortOnInputError: true });

		try {
			const result = await withCStringAsync(this.module, source, (sourcePointer) =>
				withOptionalCStringAsync(this.module, options.fileLabel ?? "", (labelPointer) =>
					withCStringArrayAsync(this.module, options.args ?? [], (argsPointer, argCount) =>
						withCStringArrayAsync(this.module, filePaths, (filePathsPointer, fileCount) =>
							withCStringArrayAsync(
								this.module,
								fileSources,
								async (fileSourcesPointer, sourceCount) => {
									if (fileCount !== sourceCount) {
										throw new Error("VeroScript virtual file path/source count mismatch");
									}
									const pointer = await this.module.ccall(
										"vero_web_run_source_interactive_with_context",
										"number",
										[
											"number",
											"number",
											"number",
											"number",
											"number",
											"number",
											"number",
											"number",
											"number",
										],
										[
											this.handle,
											sourcePointer,
											labelPointer,
											argsPointer,
											argCount,
											selectedBackendId,
											filePathsPointer,
											fileSourcesPointer,
											fileCount,
										],
										{ async: true },
									);
									return parseResult(this.module, pointer);
								},
							),
						),
					),
				),
			);
			if (providerFailed) {
				throw providerFailure;
			}
			if (result.operation === "run" && result.backend !== selectedBackend) {
				throw new Error(
					`VeroScript WebAssembly selected backend ${String(result.backend)}; expected ${selectedBackend}`,
				);
			}
			return result;
		}
		finally
		{
			delete this.module.veroWebInputProvider;
		}
	}

	checkSource(source: string, options: VeroScriptSourceOptions = {}): VeroScriptResult {
		this.ensureReady();
		const files = options.files ?? [];
		const filePaths = files.map((file) => file.path);
		const fileSources = files.map((file) => file.source);
		return withCString(this.module, source, (sourcePointer) =>
			withOptionalCString(this.module, options.fileLabel ?? "", (labelPointer) =>
				withCStringArray(this.module, filePaths, (filePathsPointer, fileCount) =>
					withCStringArray(this.module, fileSources, (fileSourcesPointer, sourceCount) => {
						if (fileCount !== sourceCount) {
							throw new Error("VeroScript virtual file path/source count mismatch");
						}
						return parseResult(
							this.module,
							this.module._vero_web_check_source_with_context(
								this.handle,
								sourcePointer,
								labelPointer,
								filePathsPointer,
								fileSourcesPointer,
								fileCount,
							),
						);
					}),
				),
			),
		);
	}

	dispose(): void {
		if (this.module.veroWebInputProvider !== undefined) {
			throw new Error("Cannot dispose a VeroScript runtime while interactive execution is active");
		}
		if (this.handle !== 0) {
			for (const program of this.programs) {
				program.disposeFromRuntime();
			}
			this.programs.clear();
			this.module._vero_web_destroy(this.handle);
			this.handle = 0;
		}
	}
	//~ End VeroScriptRuntime Interface

	setStandardLibraryLimits(maxTextBytes = 0n, maxReadBytes = 0n, maxSplitParts = 0n): void {
		this.ensureReady();
		for (const value of [maxTextBytes, maxReadBytes, maxSplitParts]) {
			if (typeof value !== "bigint" || value < 0n || value > 0xffffffffffffffffn) {
				throw new RangeError("standard-library limit outside uint64 range");
			}
		}
		const set = this.module._vero_web_set_stdlib_limits;
		if (!set)
		{
			throw new Error("This module does not support standard-library limits");
		}
		const status = set(this.handle, maxTextBytes, maxReadBytes, maxSplitParts);
		if (status !== 0)
		{
			throw new Error(`VeroScript standard-library limit status ${status}`);
		}
	}

	constructor(module: VeroScriptEngineModule, supportsInteractiveInput: boolean) {
		this.module = module;
		this.memory = new VeroScriptMemory((op, a, b, x, y, z, n) => {
			this.ensureReady();
			const call = this.module._vero_web_memory_operation;
			const result = this.module._vero_web_memory_result;
			if (!call || !result || !this.module._vero_web_memory_release_lease) {
				throw new Error("This module does not support std::memory host services");
			}
			const status = call(this.handle, op, a, b, x, y, z, n);
			if (status !== 0) {
				throw new Error(`VeroScript memory status ${status}`);
			}
			// Wasm i64 returns use signed BigInt even when the C declaration is uint64.
			return Array.from({ length: 5 }, (_, field) => BigInt.asUintN(64, result(this.handle, field)));
		}, this.module, () => this.handle !== 0);
		this.supportsInteractiveInput = supportsInteractiveInput;
		const abiVersionPointer = module._vero_web_abi_version();
		if (abiVersionPointer === 0) {
			throw new Error("VeroScript WebAssembly returned no ABI version");
		}
		this.abiVersion = module.UTF8ToString(abiVersionPointer);
		if (this.abiVersion !== expectedAbiVersion) {
			throw new Error(
				`Unsupported VeroScript WebAssembly ABI ${this.abiVersion}; expected ${expectedAbiVersion}`,
			);
		}
		this.supportsThreads = module._vero_web_supports_threads?.() === 1;
		this.supportsCompiledPrograms =
			typeof module._vero_web_compile_source_with_context === "function" &&
			typeof module._vero_web_run_program === "function" &&
			typeof module._vero_web_destroy_program === "function";
		this.handle = module._vero_web_create();
		if (this.handle === 0) {
			throw new Error("Unable to create the VeroScript WebAssembly runtime");
		}
		try {
			this.setLanguage("en-US");
		}
		catch (error)
		{
			module._vero_web_destroy(this.handle);
			this.handle = 0;
			throw error;
		}
	}

	runCompiledProgram(
		programHandle: number,
		options: VeroScriptProgramRunOptions = {},
	): VeroScriptResult {
		this.ensureReady();
		const run = this.module._vero_web_run_program;
		if (run === undefined) {
			throw new Error("Compiled program execution is unavailable");
		}
		const selectedBackend = options.backend ?? "mixed";
		const result = withOptionalCString(this.module, options.input ?? "", (inputPointer) =>
			withCStringArray(this.module, options.args ?? [], (argsPointer, argCount) =>
				parseResult(
					this.module,
					run(
						this.handle,
						programHandle,
						inputPointer,
						argsPointer,
						argCount,
						backendId(selectedBackend),
					),
				),
			),
		);
		if (result.operation === "run" && result.backend !== selectedBackend) {
			throw new Error(
				`VeroScript WebAssembly selected backend ${String(result.backend)}; expected ${selectedBackend}`,
			);
		}
		return result;
	}

	async runCompiledProgramInteractive(
		programHandle: number,
		options: VeroScriptProgramInteractiveRunOptions,
	): Promise<VeroScriptResult> {
		this.ensureAlive();
		if (!this.supportsInteractiveInput || this.module._vero_web_run_program_interactive === undefined) {
			throw new Error("This VeroScript runtime does not support interactive compiled programs");
		}
		if (typeof options?.readLine !== "function") {
			throw new Error("VeroScript interactive execution requires a readLine provider");
		}
		if (this.module.veroWebInputProvider !== undefined) {
			throw new Error("A VeroScript interactive execution is already active on this module");
		}
		const selectedBackend = options.backend ?? "mixed";
		// Preserve an active provider if reading options reentered this module.
		this.ensureReady();
		let requestIndex = 0;
		let providerFailure: unknown;
		let providerFailed = false;
		// The bridge reports marked failures on the resumed native stack.
		this.module.veroWebInputProvider = Object.assign(async (runtimeHandle: number) => {
			if (runtimeHandle !== this.handle) return null;
			try {
				const value = await options.readLine({ index: requestIndex++ });
				if (value !== null && typeof value !== "string") {
					throw new TypeError("VeroScript readLine must return a string or null");
				}
				if (value?.includes("\0")) {
					throw new TypeError("VeroScript interactive input lines must not contain NUL characters");
				}
				return value;
			}
			catch (error)
			{
				providerFailed = true;
				providerFailure = error;
				throw error;
			}
		}, { __veroWebAbortOnInputError: true });
		try {
			const pointer = await withCStringArrayAsync(
				this.module,
				options.args ?? [],
				(argsPointer, argCount) =>
					this.module.ccall(
						"vero_web_run_program_interactive",
						"number",
						["number", "number", "number", "number", "number"],
						[this.handle, programHandle, argsPointer, argCount, backendId(selectedBackend)],
						{ async: true },
					),
			);
			if (providerFailed) {
				throw providerFailure;
			}
			const result = parseResult(this.module, pointer);
			if (result.operation === "run" && result.backend !== selectedBackend) {
				throw new Error(
					`VeroScript WebAssembly selected backend ${String(result.backend)}; expected ${selectedBackend}`,
				);
			}
			return result;
		}
		finally
		{
			delete this.module.veroWebInputProvider;
		}
	}

	releaseCompiledProgram(program: Program, programHandle: number): void {
		this.ensureReady();
		const destroy = this.module._vero_web_destroy_program;
		if (destroy === undefined || destroy(this.handle, programHandle) !== 1) {
			throw new Error("Unable to release the VeroScript compiled program");
		}
		this.programs.delete(program);
	}

	readonly memory: VeroScriptMemory;

	private ensureAlive(): void {
		if (this.handle === 0) {
			throw new Error("The VeroScript WebAssembly runtime has been disposed");
		}
	}

	private ensureReady(): void {
		this.ensureAlive();
		if (this.module.veroWebInputProvider !== undefined) {
			throw new Error("The VeroScript WebAssembly module is awaiting interactive input");
		}
	}

	readonly abiVersion: string;
	readonly supportsInteractiveInput: boolean;
	readonly supportsThreads: boolean;
	readonly supportsCompiledPrograms: boolean;
	private currentLanguage: VeroScriptLanguage = "en-US";
	private handle: number;
	private readonly module: VeroScriptEngineModule;
	private readonly programs = new Set<Program>();

}

function copyEngineOptions(options: VeroScriptEngineModuleOptions): VeroScriptEngineModuleOptions {
	// Emscripten fills this object with instance-owned exports and heap views.
	const copy = { ...options };
	// Preserve the declared options when consumers provide prototype properties.
	if (!Object.hasOwn(copy, "locateFile")) {
		copy.locateFile = options.locateFile;
	}
	if (!Object.hasOwn(copy, "wasmBinary")) {
		copy.wasmBinary = options.wasmBinary;
	}
	// Configuration methods may use private fields on their original receiver.
	if (typeof copy.locateFile === "function") {
		copy.locateFile = copy.locateFile.bind(options);
	}
	return copy;
}

export async function createVeroScriptRuntime(
	options: VeroScriptEngineModuleOptions = {},
): Promise<VeroScriptRuntime> {
	const { default: createEngine } = await import("./veroscript-engine.mjs");
	return new Runtime(await createEngine(copyEngineOptions(options)), true);
}

class Program implements VeroScriptProgram {
	//~ Begin VeroScriptProgram Interface
	get disposed(): boolean {
		return this.handle === 0;
	}

	run(options: VeroScriptProgramRunOptions = {}): VeroScriptResult {
		this.ensureAlive();
		return this.runtime.runCompiledProgram(this.handle, options);
	}

	runInteractive(options: VeroScriptProgramInteractiveRunOptions): Promise<VeroScriptResult> {
		this.ensureAlive();
		return this.runtime.runCompiledProgramInteractive(this.handle, options);
	}

	dispose(): void {
		if (this.handle !== 0) {
			this.runtime.releaseCompiledProgram(this, this.handle);
			this.handle = 0;
		}
	}
	//~ End VeroScriptProgram Interface

	constructor(runtime: Runtime, handle: number) {
		this.runtime = runtime;
		this.handle = handle;
	}

	disposeFromRuntime(): void {
		this.handle = 0;
	}

	private ensureAlive(): void {
		if (this.handle === 0) {
			throw new Error("The VeroScript compiled program has been disposed");
		}
	}

	private handle: number;
	private readonly runtime: Runtime;

}

/**
 * Creates the non-Asyncify runtime intended for latency-sensitive Web IDE runs.
 * Use createVeroScriptRuntime instead when a script must await browser input.
 */
export async function createFastVeroScriptRuntime(
	options: VeroScriptEngineModuleOptions = {},
): Promise<VeroScriptRuntime> {
	const { default: createFastEngine } = await import("./veroscript-engine-fast.mjs");
	return new Runtime(await createFastEngine(copyEngineOptions(options)), false);
}

/** Runtime-owned regions. Native addresses are unavailable in Wasm; IDs remain bigint. */
export class VeroScriptMemory {
	constructor(call: MemoryCall, module: VeroScriptEngineModule, alive: () => boolean) {
		this.call = call;
		this.module = module;
		this.alive = alive;
	}

	buffer(size = 0n): VeroScriptMemoryRegion {
		return this.create(1, undefined, size);
	}

	view(readOnly = false): VeroScriptMemoryRegion {
		return this.create(2, undefined, 0n, 0n, readOnly ? 1n : 0n);
	}

	slice(region: VeroScriptMemoryRegion, offset: bigint, count: bigint, readOnly = false): VeroScriptMemoryRegion {
		return this.create(3, region, offset, count, readOnly ? 1n : 0n);
	}

	resize(region: VeroScriptMemoryRegion, size: bigint, reserveOnly = false): void {
		this.operation(4, region, undefined, size, 0n, 0n, reserveOnly ? 1n : 0n);
	}

	copy(destination: VeroScriptMemoryRegion, offset: bigint, source: VeroScriptMemoryRegion,
			 sourceOffset: bigint, count: bigint): void {
		this.operation(5, destination, source, offset, sourceOffset, count);
	}

	fill(destination: VeroScriptMemoryRegion, offset: bigint, count: bigint, value: number): void {
		if (!Number.isInteger(value) || value < 0 || value > 255) {
			throw new RangeError("byte outside [0,255]");
		}
		this.operation(6, destination, undefined, offset, count, 0n, BigInt(value));
	}

	equal(left: VeroScriptMemoryRegion, offset: bigint, right: VeroScriptMemoryRegion,
				rightOffset: bigint, count: bigint): boolean {
		return this.operation(7, left, right, offset, rightOffset, count)[0] !== 0n;
	}

	clone(region: VeroScriptMemoryRegion): VeroScriptMemoryRegion {
		return this.create(8, region);
	}

	size(region: VeroScriptMemoryRegion): bigint {
		return this.operation(9, region)[0];
	}

	capacity(region: VeroScriptMemoryRegion): bigint {
		return this.operation(10, region)[0];
	}

	objectId(region: VeroScriptMemoryRegion): bigint {
		return this.operation(11, region)[0];
	}

	mutationVersion(region: VeroScriptMemoryRegion): bigint {
		return this.operation(12, region)[0];
	}

	read(region: VeroScriptMemoryRegion, offset: bigint, type: VeroScriptMemoryScalar,
			 order: "little" | "big" = "little"): number | bigint {
		const scalar = this.scalar(type, order);
		const raw = this.operation(17, region, undefined, offset, BigInt(scalar.code), order === "little" ? 0n : 1n)[0];
		if (scalar.code >= 8) {
			const data = new DataView(new ArrayBuffer(8));
			data.setBigUint64(0, raw, true);
			return scalar.code === 8 ? data.getFloat32(0, true) : data.getFloat64(0, true);
		}
		const value = scalar.signed ? BigInt.asIntN(scalar.bits, raw) : raw;
		return scalar.big ? value : Number(value);
	}

	write(region: VeroScriptMemoryRegion, offset: bigint, type: VeroScriptMemoryScalar,
				value: number | bigint, order: "little" | "big" = "little"): void {
		const scalar = this.scalar(type, order);
		let raw: bigint;
		if (scalar.code >= 8) {
			if (typeof value !== "number") {
				throw new TypeError("Floating value must be a number");
			}
			const data = new DataView(new ArrayBuffer(8));
			if (scalar.code === 8) {
				data.setFloat32(0, value, true);
			}
			else
			{
				data.setFloat64(0, value, true);
			}
			raw = data.getBigUint64(0, true);
		}
		else
		{
			if ((typeof value !== "number" && typeof value !== "bigint") ||
					(scalar.big && typeof value !== "bigint") ||
					(typeof value === "number" && !Number.isSafeInteger(value))) {
				throw new TypeError("Use bigint for 64-bit integers and integral numbers for smaller types");
			}
			const integer = BigInt(value);
			const low = scalar.signed ? -(1n << BigInt(scalar.bits - 1)) : 0n;
			const high = (1n << BigInt(scalar.signed ? scalar.bits - 1 : scalar.bits)) - 1n;
			if (integer < low || integer > high) {
				throw new RangeError("Scalar outside its type range");
			}
			raw = BigInt.asUintN(64, integer);
		}
		this.operation(18, region, undefined, offset, BigInt(scalar.code), order === "little" ? 0n : 1n, raw);
	}

	acquireLease(region: VeroScriptMemoryRegion, write = false): VeroScriptMemoryLease {
		const result = this.operation(13, region, undefined, 0n, 0n, 0n, write ? 1n : 0n);
		return new VeroScriptMemoryLease(this.module, Number(result[1]), result[2], result[3], result[4], write);
	}

	release(region: VeroScriptMemoryRegion): void {
		if (this.alive()) {
			this.operation(14, region);
		}
	}

	/** Applies to host-owned regions; script executions retain their own limits. Zero means unlimited. */
	setLimits(maxBufferBytes: bigint, maxRetainedBytes: bigint): void {
		for (const value of [maxBufferBytes, maxRetainedBytes]) {
			if (typeof value !== "bigint" || value < 0n || value > 0xffffffffffffffffn) {
				throw new RangeError("memory quota outside supported range");
			}
		}
		this.call(15, Number(maxRetainedBytes >> 32n), Number(maxRetainedBytes & 0xffffffffn), 0n, 0n, 0n, maxBufferBytes);
	}

	get stats(): { ownedBytes: bigint; borrowedBytes: bigint; peakOwnedBytes: bigint } {
		const result = this.operation(16);
		return { ownedBytes: result[0], borrowedBytes: result[1], peakOwnedBytes: result[2] };
	}

	fromArray(source: Uint8Array): VeroScriptMemoryRegion {
		// Buffer.slice() aliases its source. Copy with the Uint8Array constructor
		// before native allocation can grow linear memory and detach the source.
		const snapshot = new Uint8Array(source);
		const region = this.buffer(BigInt(snapshot.length));
		try {
			const lease = this.acquireLease(region, true);
			try {
				lease.withBytes(bytes => bytes.set(snapshot));
			}
			finally
			{
				lease.dispose();
			}
			return region;
		}
		catch (error)
		{
			region.dispose();
			throw error;
		}
	}

	toArray(region: VeroScriptMemoryRegion): Uint8Array {
		const lease = this.acquireLease(region);
		try {
			// Read-only lease callbacks already receive an owned snapshot.
			return lease.withBytes(bytes => bytes);
		}
		finally
		{
			lease.dispose();
		}
	}

	private operation(op: number, a?: VeroScriptMemoryRegion, b?: VeroScriptMemoryRegion,
										x = 0n, y = 0n, z = 0n, n = 0n): bigint[] {
		for (const value of [x, y, z]) {
			if (typeof value !== "bigint" || value < -0x8000000000000000n || value > 0x7fffffffffffffffn) {
				throw new RangeError("offset/length outside int64");
			}
		}
		if (typeof n !== "bigint" || n < 0n || n > 0xffffffffffffffffn) {
			throw new RangeError("value outside uint64");
		}
		return this.call(op, a ? a.getHandle(this) : 0, b ? b.getHandle(this) : 0, x, y, z, n);
	}

	private create(op: number, region?: VeroScriptMemoryRegion, x = 0n, y = 0n, n = 0n): VeroScriptMemoryRegion {
		return new VeroScriptMemoryRegion(this, Number(this.operation(op, region, undefined, x, y, 0n, n)[0]));
	}

	private scalar(type: VeroScriptMemoryScalar, order: "little" | "big"):
		{ code: number; big: boolean; signed: boolean; bits: number } {
		const code = ["int8", "uint8", "int16", "uint16", "int32", "uint32", "int64", "uint64", "float", "double"].indexOf(type);
		if (code < 0 || (order !== "little" && order !== "big")) {
			throw new TypeError("Invalid memory scalar or byte order");
		}
		return {
			code,
			big: code === 6 || code === 7,
			signed: code % 2 === 0,
			bits: code < 8 ? 8 * (1 << Math.floor(code / 2)) : code === 8 ? 32 : 64,
		};
	}

	private readonly call: MemoryCall;
	private readonly module: VeroScriptEngineModule;
	private readonly alive: () => boolean;
}

export class VeroScriptMemoryRegion {
	constructor(memory: VeroScriptMemory, handle: number) {
		this.memory = memory;
		this.handle = handle;
	}

	getHandle(memory: VeroScriptMemory): number {
		if (this.memory !== memory || this.handle === 0) {
			throw new Error("Expired or foreign memory region");
		}
		return this.handle;
	}

	dispose(): void {
		if (this.handle !== 0) {
			this.memory.release(this);
			this.handle = 0;
		}
	}

	private readonly memory: VeroScriptMemory;
	private handle: number;
}

/** Explicit lease. Finish native work before dispose; release survives runtime disposal. */
export class VeroScriptMemoryLease {
	constructor(module: VeroScriptEngineModule, offset: number, size: bigint,
							runtimeToken: bigint, serialToken: bigint, writable: boolean) {
		this.module = module;
		this.offset = offset;
		this.size = size;
		this.runtimeToken = runtimeToken;
		this.serialToken = serialToken;
		this.writable = writable;
	}

	/** Borrowed view for a synchronous callback. Do not retain writable views or grow the heap inside it. */
	withBytes<T>(use: (bytes: Uint8Array) => T): T {
		if (this.closed) {
			throw new Error("Memory lease is closed");
		}
		const bytes = new Uint8Array(this.module.HEAPU32.buffer, this.offset, Number(this.size));
		++this.activeCallbacks;
		try {
			// Read-only leases expose a copy so JavaScript cannot write through a const lease.
			const result = use(this.writable ? bytes : bytes.slice());
			if (result !== null && (typeof result === "object" || typeof result === "function") &&
					typeof (result as { then?: unknown }).then === "function") {
				// Report the contract violation synchronously without leaving a rejected
				// callback promise unhandled after the borrowed view has been returned.
				void Promise.resolve(result).catch(() => {});
				throw new TypeError("Memory lease callbacks must be synchronous");
			}
			return result;
		}
		finally
		{
			--this.activeCallbacks;
		}
	}

	dispose(): void {
		if (this.activeCallbacks !== 0) {
			throw new Error("Cannot release a memory lease during its callback");
		}
		if (!this.closed) {
			const release = this.module._vero_web_memory_release_lease;
			if (!release || release(this.runtimeToken, this.serialToken) !== 0) {
				throw new Error("Cannot release memory lease");
			}
			this.closed = true;
		}
	}

	readonly size: bigint;
	readonly writable: boolean;
	private closed = false;
	private activeCallbacks = 0;
	private readonly module: VeroScriptEngineModule;
	private readonly offset: number;
	private readonly runtimeToken: bigint;
	private readonly serialToken: bigint;
}
