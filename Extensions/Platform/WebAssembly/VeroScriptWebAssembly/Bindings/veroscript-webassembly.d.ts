// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import type { VeroScriptEngineModule, VeroScriptEngineModuleOptions } from "./veroscript-engine.mjs";
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
	timings: {
		compileMs: number | null;
		executeMs: number | null;
	} | null;
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
	runSourceInteractive(source: string, options: VeroScriptInteractiveRunOptions): Promise<VeroScriptResult>;
	checkSource(source: string, options?: VeroScriptSourceOptions): VeroScriptResult;
	dispose(): void;
}
export type VeroScriptDiagnosticSeverity = "error" | "warning" | "info" | "hint";
export type VeroScriptExecutionBackend = "vm" | "mixed" | "jit";
/** Canonical BCP 47 tag discovered from the deployed localization catalogs. */
export type VeroScriptLanguage = string;
export type VeroScriptProgramRunOptions = Omit<VeroScriptRunOptions, "fileLabel" | "files">;
export type VeroScriptProgramInteractiveRunOptions = Omit<VeroScriptInteractiveRunOptions, "fileLabel" | "files">;
export type VeroScriptMemoryScalar = "int8" | "uint8" | "int16" | "uint16" | "int32" | "uint32" | "int64" | "uint64" | "float" | "double";
type MemoryCall = (op: number, a: number, b: number, x: bigint, y: bigint, z: bigint, n: bigint) => bigint[];
export declare function createVeroScriptRuntime(options?: VeroScriptEngineModuleOptions): Promise<VeroScriptRuntime>;
/**
 * Creates the non-Asyncify runtime intended for latency-sensitive Web IDE runs.
 * Use createVeroScriptRuntime instead when a script must await browser input.
 */
export declare function createFastVeroScriptRuntime(options?: VeroScriptEngineModuleOptions): Promise<VeroScriptRuntime>;
/** Runtime-owned regions. Native addresses are unavailable in Wasm; IDs remain bigint. */
export declare class VeroScriptMemory {
	constructor(call: MemoryCall, module: VeroScriptEngineModule, alive: () => boolean);
	buffer(size?: bigint): VeroScriptMemoryRegion;
	view(readOnly?: boolean): VeroScriptMemoryRegion;
	slice(region: VeroScriptMemoryRegion, offset: bigint, count: bigint, readOnly?: boolean): VeroScriptMemoryRegion;
	resize(region: VeroScriptMemoryRegion, size: bigint, reserveOnly?: boolean): void;
	copy(destination: VeroScriptMemoryRegion, offset: bigint, source: VeroScriptMemoryRegion, sourceOffset: bigint, count: bigint): void;
	fill(destination: VeroScriptMemoryRegion, offset: bigint, count: bigint, value: number): void;
	equal(left: VeroScriptMemoryRegion, offset: bigint, right: VeroScriptMemoryRegion, rightOffset: bigint, count: bigint): boolean;
	clone(region: VeroScriptMemoryRegion): VeroScriptMemoryRegion;
	size(region: VeroScriptMemoryRegion): bigint;
	capacity(region: VeroScriptMemoryRegion): bigint;
	objectId(region: VeroScriptMemoryRegion): bigint;
	mutationVersion(region: VeroScriptMemoryRegion): bigint;
	read(region: VeroScriptMemoryRegion, offset: bigint, type: VeroScriptMemoryScalar, order?: "little" | "big"): number | bigint;
	write(region: VeroScriptMemoryRegion, offset: bigint, type: VeroScriptMemoryScalar, value: number | bigint, order?: "little" | "big"): void;
	acquireLease(region: VeroScriptMemoryRegion, write?: boolean): VeroScriptMemoryLease;
	release(region: VeroScriptMemoryRegion): void;
	/** Applies to host-owned regions; script executions retain their own limits. Zero means unlimited. */
	setLimits(maxBufferBytes: bigint, maxRetainedBytes: bigint): void;
	get stats(): {
		ownedBytes: bigint;
		borrowedBytes: bigint;
		peakOwnedBytes: bigint;
	};
	fromArray(source: Uint8Array): VeroScriptMemoryRegion;
	toArray(region: VeroScriptMemoryRegion): Uint8Array;
	private operation;
	private create;
	private scalar;
	private readonly call;
	private readonly module;
	private readonly alive;
}
export declare class VeroScriptMemoryRegion {
	constructor(memory: VeroScriptMemory, handle: number);
	getHandle(memory: VeroScriptMemory): number;
	dispose(): void;
	private readonly memory;
	private handle;
}
/** Explicit lease. Finish native work before dispose; release survives runtime disposal. */
export declare class VeroScriptMemoryLease {
	constructor(module: VeroScriptEngineModule, offset: number, size: bigint, runtimeToken: bigint, serialToken: bigint, writable: boolean);
	/** Borrowed view for a synchronous callback. Do not retain writable views or grow the heap inside it. */
	withBytes<T>(use: (bytes: Uint8Array) => T): T;
	dispose(): void;
	readonly size: bigint;
	readonly writable: boolean;
	private closed;
	private activeCallbacks;
	private readonly module;
	private readonly offset;
	private readonly runtimeToken;
	private readonly serialToken;
}
export {};
