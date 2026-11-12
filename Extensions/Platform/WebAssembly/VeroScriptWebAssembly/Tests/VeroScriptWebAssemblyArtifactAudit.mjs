// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import { readFile, stat } from "node:fs/promises";
import { resolve } from "node:path";

const outputDirectory = process.argv[2];
const configuration = process.argv[3] ?? "Release";
if (!outputDirectory) {
	throw new Error(
		"usage: node VeroScriptWebAssemblyArtifactAudit.mjs <web-sdk-output-directory> [Debug|Release]",
	);
}
if (configuration !== "Debug" && configuration !== "Release") {
	throw new Error(`unsupported WebAssembly artifact configuration: ${configuration}`);
}

const requiredFiles = [
	"veroscript-engine.mjs",
	"veroscript-engine.wasm",
	"veroscript-engine.d.mts",
	"veroscript-engine-fast.mjs",
	"veroscript-engine-fast.wasm",
	"veroscript-engine-fast.d.mts",
	"veroscript-webassembly.ts",
	"veroscript-webassembly.d.ts",
];

// These are release guardrails, not ABI limits. Raise them intentionally when a
// reviewed feature justifies the browser download cost.
const sizeBudgets = new Map([
	// Includes script compile extensions, attributes/method callables, typed i18n
	// and the core module/resource contracts, in addition to managed memory.
	// This baseline also includes exact numerics and nested/local/generic type scopes.
	// Includes Core reference ownership, bounded reentry/cleanup and updated VM dispatch.
	["veroscript-engine.wasm", 17 * 1024 * 1024],
	// Per-query indexed-type reuse bounds native/overloaded subscript analysis.
	["veroscript-engine-fast.wasm", 11 * 1024 * 1024 + 64 * 1024],
]);

const sizes = {};
for (const fileName of requiredFiles) {
	const file = await stat(resolve(outputDirectory, fileName));
	if (!file.isFile() || file.size === 0) {
		throw new Error(`required WebAssembly SDK artifact is missing or empty: ${fileName}`);
	}
	sizes[fileName] = file.size;

	const budget = sizeBudgets.get(fileName);
	if (configuration === "Release" && budget !== undefined && file.size > budget) {
		throw new Error(
			`${fileName} is ${file.size} bytes, exceeding its ${budget}-byte release budget`,
		);
	}
}

// Build output has no package metadata; a staged SDK carries its legal notice.
// Authored documentation, including README, is not a distribution dependency.
const packageMetadataNames = ["LICENSE"];
const packageMetadata = await Promise.all(
	packageMetadataNames.map(async (fileName) => {
		try {
			return await stat(resolve(outputDirectory, fileName));
		}
		catch (error)
		{
			if (error?.code === "ENOENT") return null;
			throw error;
		}
	}),
);
if (packageMetadata.some((file) => file !== null)) {
	for (let index = 0; index < packageMetadataNames.length; ++index) {
		const file = packageMetadata[index];
		if (file === null || !file.isFile() || file.size === 0) {
			throw new Error(
				`packaged WebAssembly SDK metadata is missing or empty: ${packageMetadataNames[index]}`,
			);
		}
	}
}

const [interactiveLoader, fastLoader] = await Promise.all([
	readFile(resolve(outputDirectory, "veroscript-engine.mjs"), "utf8"),
	readFile(resolve(outputDirectory, "veroscript-engine-fast.mjs"), "utf8"),
]);
for (const [fileName, loader] of [
	["veroscript-engine.mjs", interactiveLoader],
	["veroscript-engine-fast.mjs", fastLoader],
]) {
	if (
		!loader.includes("Copyright 2010 The Emscripten Authors") ||
		!loader.includes("SPDX-License-Identifier: MIT")
	) {
		throw new Error(`${fileName} is missing the required Emscripten license notice`);
	}
}
if (
	!interactiveLoader.includes("__asyncjs__VeroWebAwaitInputLineAsync") ||
	!/StackSize\s*:\s*1048576/.test(interactiveLoader)
) {
	throw new Error("interactive WebAssembly artifact is missing its audited 1 MiB Asyncify bridge");
}
if (
	fastLoader.includes("__asyncjs__VeroWebAwaitInputLineAsync") ||
	fastLoader.includes("Asyncify.handleAsync")
) {
	throw new Error("fast WebAssembly artifact unexpectedly contains the interactive Asyncify bridge");
}

const [interactiveWasm, fastWasm] = await Promise.all([
	WebAssembly.compile(await readFile(resolve(outputDirectory, "veroscript-engine.wasm"))),
	WebAssembly.compile(await readFile(resolve(outputDirectory, "veroscript-engine-fast.wasm"))),
]);
const wasmExports = (module) => new Set(
	WebAssembly.Module.exports(module).map((entry) => entry.name),
);
const interactiveExports = wasmExports(interactiveWasm);
const fastExports = wasmExports(fastWasm);
// The loader can still exist while an interrupted link leaves an uninstrumented
// binary. Validate the actual Asyncify exports before trusting its input bridge.
for (const exportName of [
	"asyncify_start_unwind",
	"asyncify_stop_unwind",
	"asyncify_start_rewind",
	"asyncify_stop_rewind",
]) {
	if (!interactiveExports.has(exportName)) {
		throw new Error(`interactive WebAssembly binary is missing Asyncify instrumentation: ${exportName}`);
	}
	if (fastExports.has(exportName)) {
		throw new Error(`fast WebAssembly binary unexpectedly contains Asyncify instrumentation: ${exportName}`);
	}
}
const commonAbiExports = [
	"malloc",
	"free",
	"vero_web_memory_operation",
	"vero_web_memory_result",
	"vero_web_set_stdlib_limits",
	"vero_web_memory_release_lease",
	"vero_web_abi_version",
	"vero_web_supports_threads",
	"vero_web_create",
	"vero_web_destroy",
	"vero_web_set_language",
	"vero_web_get_language",
	"vero_web_get_available_languages",
	"vero_web_run_source",
	"vero_web_run_source_with_args",
	"vero_web_run_source_with_options",
	"vero_web_run_source_with_context",
	"vero_web_compile_source_with_context",
	"vero_web_run_program",
	"vero_web_destroy_program",
	"vero_web_check_source",
	"vero_web_check_source_with_context",
];
for (const exportName of commonAbiExports) {
	if (!interactiveExports.has(exportName) || !fastExports.has(exportName)) {
		throw new Error(`required WebAssembly ABI export is missing: ${exportName}`);
	}
}
for (const exportName of [
	"vero_web_run_program_interactive",
	"vero_web_run_source_interactive_with_context",
]) {
	if (!interactiveExports.has(exportName)) {
		throw new Error(`interactive WebAssembly ABI export is missing: ${exportName}`);
	}
	if (fastExports.has(exportName)) {
		throw new Error(`fast WebAssembly artifact exposes an interactive-only ABI: ${exportName}`);
	}
}

console.log(
	`VeroScript WebAssembly ${configuration} artifact audit passed: ` +
		`interactive=${sizes["veroscript-engine.wasm"]} bytes, ` +
		`fast=${sizes["veroscript-engine-fast.wasm"]} bytes`,
);
