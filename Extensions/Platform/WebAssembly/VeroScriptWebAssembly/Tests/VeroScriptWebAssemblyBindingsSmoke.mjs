// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import { rmSync } from "node:fs";
import { copyFile, mkdir, mkdtemp, readFile, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { pathToFileURL } from "node:url";
import { transform } from "esbuild";

const bindingsPath = process.argv[2];
if (!bindingsPath) {
	throw new Error(
		"usage: node VeroScriptWebAssemblyBindingsSmoke.mjs <veroscript-webassembly.ts>",
	);
}

const resolvedBindingsPath = resolve(bindingsPath);
const artifactDirectory = dirname(resolvedBindingsPath);
const temporaryDirectory = await mkdtemp(join(tmpdir(), "veroscript-webassembly-bindings-"));
let temporaryDirectoryExists = true;
const cleanupTemporaryDirectory = () => {
	if (!temporaryDirectoryExists) return;
	rmSync(temporaryDirectory, { force: true, recursive: true });
	temporaryDirectoryExists = false;
};
process.once("exit", cleanupTemporaryDirectory);

const transformedBindings = await transform(await readFile(resolvedBindingsPath, "utf8"), {
	format: "esm",
	loader: "ts",
	target: "es2022",
});
await Promise.all(
	[
		"veroscript-engine.mjs",
		"veroscript-engine.wasm",
		"veroscript-engine-fast.mjs",
		"veroscript-engine-fast.wasm",
	].map((fileName) =>
		copyFile(join(artifactDirectory, fileName), join(temporaryDirectory, fileName)),
	),
);
const runnableBindingsPath = join(temporaryDirectory, "veroscript-webassembly.mjs");
await writeFile(runnableBindingsPath, transformedBindings.code, "utf8");

const { createVeroScriptRuntime, createFastVeroScriptRuntime } = await import(
	pathToFileURL(runnableBindingsPath).href
);
const runtimeOptions = Object.freeze({});
const runtime = await createVeroScriptRuntime(runtimeOptions);
// Native boundary fault injection uses the implementation's module, not options:
// public configuration objects must remain reusable and free of native exports.
const runtimeModule = runtime.module;

function assertMissingBackend(result) {
	if (result.success || result.backend !== "jit" || result.output !== "" || !result.diagnostics.some(
		(diagnostic) => diagnostic.code === "diag.jit_execution_requires_an_explicitly_selected_backend_file_directory_or_factory"
	)) {
		throw new Error(`wrapper hid missing JIT backend: ${JSON.stringify(result)}`);
	}
}

function assertDiagnosticSeverities(runtime, module) {
	const baseline = runtime.checkSource("void main() {}");
	const check = module._vero_web_check_source_with_context;
	try {
		for (const severity of ["error", "warning", "info", "hint", "unsupported"]) {
			const result = {
				...baseline,
				success: severity !== "error",
				exitCode: severity === "error" ? 3 : 0,
				diagnostics: [{ severity, message: "core diagnostic", code: "", file: "diagnostic.vs", line: 1, column: 1 }],
			};
			const json = JSON.stringify(result);
			const capacity = module.lengthBytesUTF8(json) + 1;
			const pointer = module._malloc(capacity);
			if (!pointer) {
				throw new Error("diagnostic fixture allocation failed");
			}
			try {
				module.stringToUTF8(json, pointer, capacity);
				module._vero_web_check_source_with_context = () => pointer;
				if (severity === "unsupported") {
					let rejected = false;
					try {
						runtime.checkSource("void main() {}");
					}
					catch (error)
					{
						rejected = error instanceof Error && error.message.includes("malformed result");
					}
					if (!rejected) {
						throw new Error("unsupported diagnostic severity was accepted");
					}
				}
				else
				{
					const actual = runtime.checkSource("void main() {}");
					if (actual.diagnostics[0].severity !== severity || actual.success !== result.success) {
						throw new Error("core diagnostic severity or success was changed by the bindings");
					}
				}
			}
			finally
			{
				module._vero_web_check_source_with_context = check;
				module._free(pointer);
			}
		}
	}
	finally
	{
		module._vero_web_check_source_with_context = check;
	}
}

async function assertArgumentTableBounds(runtime, module) {
	const source = "int main(string[] args) { return len(args); }";
	const compiled = runtime.compileSource(source);
	if (!compiled.program) {
		throw new Error("argument table fixture did not compile");
	}
	const malloc = module._malloc;
	const heap = module.HEAPU32;
	const set = heap.set;
	const allocations = new Map();
	module._malloc = size => {
		const pointer = malloc(size);
		allocations.set(pointer, size);
		return pointer;
	};
	heap.set = function(values, offset) {
		const capacity = allocations.get(offset * Uint32Array.BYTES_PER_ELEMENT);
		if (values.length * Uint32Array.BYTES_PER_ELEMENT > capacity) {
			throw new Error("argument table write exceeds its native allocation");
		}
		return set.call(this, values, offset);
	};
	try {
		const runs = [
			args => runtime.runSource(source, { args }),
			args => compiled.program.run({ args }),
		];
		if (runtime.supportsInteractiveInput) {
			runs.push(
				args => runtime.runSourceInteractive(source, { args, readLine() { return null; } }),
				args => compiled.program.runInteractive({ args, readLine() { return null; } }),
			);
		}
		for (const declaredLength of [0, 1]) {
			for (const count of [9, 0]) {
				const args = Array(declaredLength).fill("declared");
				args[Symbol.iterator] = function* () {
					for (let index = 0; index < count; ++index) {
						yield "a";
					}
				};
				for (const run of runs) {
					const result = await run(args);
					if (!result.success || result.exitCode !== count) {
						throw new Error(`argument iterator was marshalled incorrectly (length ${declaredLength}, count ${count}): ${JSON.stringify(result)}`);
					}
				}
			}
		}
	}
	finally
	{
		module._malloc = malloc;
		heap.set = set;
		compiled.program.dispose();
	}
}

async function assertInteractiveProviderAbort(runtime, module) {
	const source = 'import "io.vs"; int main() { std::io::read_line(); print("after input"); return 42; }';
	const compiled = runtime.compileSource(source);
	if (!compiled.program) {
		throw new Error("provider abort fixture did not compile");
	}
	const originalCall = module.ccall;
	let nativeResult;
	module.ccall = async (...args) => {
		const pointer = await originalCall(...args);
		nativeResult = JSON.parse(module.UTF8ToString(pointer));
		return pointer;
	};
	const providerError = new Error("provider abort fixture");
	const failures = [
		{ readLine() { throw providerError; }, matches: error => error === providerError },
		{ async readLine() { await Promise.resolve(); throw providerError; }, matches: error => error === providerError },
		{ readLine() { throw undefined; }, matches: error => error === undefined },
		{ readLine() { return "bad\0line"; }, matches: error => error instanceof TypeError },
		{ readLine() { return false; }, matches: error => error instanceof TypeError },
	];
	try {
		for (const backend of ["vm", "mixed"]) {
			const runs = [
				options => runtime.runSourceInteractive(source, options),
				options => compiled.program.runInteractive(options),
			];
			for (const run of runs) {
				for (const failure of failures) {
					nativeResult = undefined;
					let rejected = false;
					try {
						await run({ backend, readLine: failure.readLine });
					}
					catch (error)
					{
						rejected = failure.matches(error);
					}
					if (!rejected || !nativeResult || nativeResult.success ||
							nativeResult.output.includes("after input") || module.veroWebInputProvider !== undefined) {
						throw new Error(`provider failure continued execution: ${JSON.stringify({ rejected, nativeResult })}`);
					}
					const recovered = compiled.program.run({ backend, input: "recovered\n" });
					if (!recovered.success || recovered.exitCode !== 42 || recovered.output !== "after input\n") {
						throw new Error(`provider abort left a stale compiled program: ${JSON.stringify(recovered)}`);
					}
				}
				const eof = await run({ backend, readLine() { return null; } });
				if (!eof.success || eof.exitCode !== 42 || eof.output !== "after input\n") {
					throw new Error(`normal EOF changed after provider abort: ${JSON.stringify(eof)}`);
				}
			}
		}
	}
	finally
	{
		module.ccall = originalCall;
		compiled.program.dispose();
	}
}

async function assertInteractiveOptionReentry(runtime, module) {
	const source = `import "io.vs";
int main()
{
    print(std::io::read_line());
    print(std::io::read_line());
    return 23;
}`;
	const compiled = runtime.compileSource(source);
	if (!compiled.program) {
		throw new Error("interactive option reentry fixture did not compile");
	}
	try {
		for (const scenario of ["source backend", "program backend", "file path", "file source"]) {
			let resume;
			const firstLine = new Promise(resolve => { resume = resolve; });
			let inner;
			let activeProvider;
			let requests = 0;
			const startInner = () => {
				inner = compiled.program.runInteractive({
					readLine() { return ++requests === 1 ? firstLine : "second"; },
				});
				void inner.catch(() => {});
				activeProvider = module.veroWebInputProvider;
			};
			const options = {
				readLine() { throw new Error("outer run must be rejected before requesting input"); },
			};
			if (scenario.endsWith("backend")) {
				Object.defineProperty(options, "backend", {
					get() { startInner(); return "vm"; },
				});
			}
			else
			{
				const file = { path: "unused.vs", source: "export int unused() { return 0; }" };
				const property = scenario === "file path" ? "path" : "source";
				const value = file[property];
				Object.defineProperty(file, property, {
					get() { startInner(); return value; },
				});
				options.files = [file];
			}
			let rejected = false;
			let preserved = false;
			let result;
			try {
				try {
					if (scenario === "program backend") {
						await compiled.program.runInteractive(options);
					}
					else
					{
						await runtime.runSourceInteractive(source, options);
					}
				}
				catch (error)
				{
					rejected = error instanceof Error && error.message.includes("awaiting interactive input");
				}
				preserved = typeof activeProvider === "function" && module.veroWebInputProvider === activeProvider;
			}
			finally
			{
				resume("first");
				result = await inner;
			}
			if (!rejected || !preserved || requests !== 2 || !result?.success ||
					result.exitCode !== 23 || result.output !== "first\nsecond\n") {
				throw new Error(`interactive option getter stole the input provider (${scenario}): ${JSON.stringify({ rejected, preserved, requests, result })}`);
			}
		}
	}
	finally
	{
		compiled.program.dispose();
	}
}

function assertCompiledFailureRecovery(runtime) {
	const compiled = runtime.compileSource(`import "rational.vs";
int starts = 0;
std::bigint exact = std::bigint("18446744073709551616");
std::rational fraction = std::rational(1, 3);
int nested(int depth, bool fail)
{
    array<string> values = {"owned local"};
    if (depth > 0) return nested(depth - 1, fail);
    assert(exact.to_string() == "18446744073709551616");
    assert(fraction == std::rational(1, 3));
    exact += std::bigint(42);
    fraction += std::rational(1, 6);
    assert(fraction == std::rational(1, 2));
    assert(!fail);
    return len(values);
}
int main(string[] args)
{
    print(++starts);
    print(nested(4, args[0] == "fail"));
    return len(args);
}`);
	if (!compiled.result.success || !compiled.program) {
		throw new Error(`recovery fixture did not compile: ${JSON.stringify(compiled.result)}`);
	}
	try {
		// Session may reuse stack/frame capacity, but every Script run must have
		// fresh globals, arguments and locals even after failure inside nested calls.
		for (const backend of ["mixed", "vm"]) {
			const failed = compiled.program.run({ backend, args: ["fail"] });
			const recovered = compiled.program.run({ backend, args: ["ok", "next"] });
			if (failed.success || failed.output !== "1\n" || failed.diagnostics.length === 0 ||
					!recovered.success || recovered.output !== "1\n1\n" || recovered.exitCode !== 2 ||
					recovered.errorOutput !== "" || recovered.diagnostics.length !== 0) {
				throw new Error(`compiled run did not recover fresh state: ${JSON.stringify({ failed, recovered })}`);
			}
		}
	}
	finally
	{
		compiled.program.dispose();
	}
}

async function assertStandardLibraryLimits(runtime) {
	const assertFailure = (result) => {
		if (result.success || result.output !== "" || !result.diagnostics.some(
			diagnostic => diagnostic.severity === "error" && diagnostic.code === "diag.stdlib_limit",
		)) {
			throw new Error(`standard-library limit did not reject execution: ${JSON.stringify(result)}`);
		}
	};
	const assertRecovery = (result, output = "") => {
		if (!result.success || result.exitCode !== 42 || result.exitRequested ||
				result.output !== output || result.errorOutput !== "" ||
				result.diagnostics.some(diagnostic => diagnostic.severity === "error")) {
			throw new Error(`standard-library limit left stale state: ${JSON.stringify(result)}`);
		}
	};
	for (let position = 0; position < 3; ++position) {
		for (const invalid of [-1n, 1n << 64n, 1]) {
			const limits = [0n, 0n, 0n];
			limits[position] = invalid;
			let rejected = false;
			try {
				runtime.setStandardLibraryLimits(...limits);
			}
			catch (error)
			{
				rejected = error instanceof RangeError;
			}
			if (!rejected) {
				throw new Error("invalid standard-library quota was not rejected before Wasm conversion");
			}
		}
	}
	const fixtures = [
		{
			limits: [5n, 0n, 0n],
			source: 'int main() { print(format("{}{}", "abc", "abc")); return 42; }',
			output: "abcabc\n",
		},
		{
			limits: [0n, 2n, 0n],
			source: 'import "io.vs"; int main() { assert(std::io::read_bytes("/stdlib/io.vs").size() > 2i64); return 42; }',
		},
		{
			limits: [0n, 0n, 2n],
			source: 'import "string.vs"; int main() { assert(len(std::strings::split("a,b,c", ",")) == 3); return 42; }',
		},
	];
	try {
		for (const fixture of fixtures) {
			// Compile before changing limits: existing handles must use the new run policy.
			const compiled = runtime.compileSource(fixture.source);
			if (!compiled.result.success || !compiled.program) {
				throw new Error(`standard-library fixture did not compile: ${JSON.stringify(compiled.result)}`);
			}
			try {
				for (const backend of ["vm", "mixed"]) {
					for (const invoke of [
						() => runtime.runSource(fixture.source, { backend }),
						() => compiled.program.run({ backend }),
					]) {
						runtime.setStandardLibraryLimits(...fixture.limits);
						assertFailure(invoke());
						runtime.setStandardLibraryLimits();
						assertRecovery(invoke(), fixture.output ?? "");
					}
				}
			}
			finally
			{
				compiled.program.dispose();
			}
		}
		if (runtime.supportsInteractiveInput) {
			const source = 'import "io.vs"; int main() { print(std::io::read_line()); return 42; }';
			const compiled = runtime.compileSource(source);
			if (!compiled.result.success || !compiled.program) {
				throw new Error(`interactive quota fixture did not compile: ${JSON.stringify(compiled.result)}`);
			}
			try {
				for (const backend of ["vm", "mixed"]) {
					for (const invoke of [
						readLine => runtime.runSourceInteractive(source, { backend, readLine }),
						readLine => compiled.program.runInteractive({ backend, readLine }),
					]) {
						runtime.setStandardLibraryLimits(2n);
						let requests = 0;
						const failed = await invoke(() => {
							++requests;
							let blocked = false;
							try {
								runtime.setStandardLibraryLimits();
							}
							catch
							{
								blocked = true;
							}
							if (!blocked || requests !== 1) {
								throw new Error("interactive input did not preserve its standard-library policy");
							}
							return "abc";
						});
						assertFailure(failed);
						if (requests !== 1) {
							throw new Error("interactive text quota failed before reading its input");
						}
						runtime.setStandardLibraryLimits();
						assertRecovery(await invoke(() => "abc"), "abc\n");
					}
				}
			}
			finally
			{
				compiled.program.dispose();
			}
		}
	}
	finally
	{
		runtime.setStandardLibraryLimits();
	}
}

function assertMemoryBoundaries(runtime, module) {
	const memory = runtime.memory;
	const expectFailure = (call, label) => {
		let rejected = false;
		try {
			call();
		}
		catch
		{
			rejected = true;
		}
		if (!rejected) {
			throw new Error(`memory boundary accepted ${label}`);
		}
	};
	const region = memory.buffer(16n);
	try {
		for (const order of ["little", "big"]) {
			for (const [type, value] of [
				["int8", -128], ["uint8", 255], ["int16", -32768], ["uint16", 65535],
				["int32", -2147483648], ["uint32", 4294967295],
				["int64", -0x8000000000000000n], ["uint64", 0xffffffffffffffffn],
				["float", -1.5], ["double", 1.25],
			]) {
				memory.write(region, 1n, type, value, order);
				if (memory.read(region, 1n, type, order) !== value) {
					throw new Error(`lossy ${type} ${order} scalar round trip`);
				}
			}
		}
		expectFailure(() => memory.write(region, 0n, "uint8", 256), "overflow");
		expectFailure(() => memory.read(region, 16n, "int8"), "out-of-range read");
		const readOnly = memory.slice(region, 0n, 16n, true);
		try {
			const lease = memory.acquireLease(readOnly);
			try {
				lease.withBytes(bytes => bytes.fill(0));
			}
			finally
			{
				lease.dispose();
			}
			if (memory.read(region, 1n, "double", "big") !== 1.25) {
				throw new Error("read-only lease exposed writable backing storage");
			}
			expectFailure(() => memory.acquireLease(readOnly, true), "read-only write lease");
		}
		finally
		{
			readOnly.dispose();
		}
		for (const [order, expected] of [["big", "1,2,3,4,5,6,7,8"], ["little", "8,7,6,5,4,3,2,1"]]) {
			memory.write(region, 0n, "uint64", 0x0102030405060708n, order);
			if (String(memory.toArray(region).subarray(0, 8)) !== expected) {
				throw new Error(`incorrect ${order} wire bytes`);
			}
		}
		const lease = memory.acquireLease(region, true);
		lease.withBytes(bytes => {
			expectFailure(() => lease.dispose(), "release during a borrowed callback");
			bytes[0] = 99;
		});
		expectFailure(() => lease.withBytes(() => Promise.resolve()), "asynchronous callback");
		expectFailure(() => lease.withBytes(() => Promise.reject(new Error("callback failure"))),
			"rejected asynchronous callback");
		expectFailure(() => memory.resize(region, 17n), "resize while leased");
		lease.dispose();
		lease.dispose();
		expectFailure(() => lease.withBytes(() => {}), "closed lease");
		if (memory.read(region, 0n, "uint8") !== 99) {
			throw new Error("failed lease release invalidated active bytes");
		}

		// Buffer.slice aliases source memory. Mutate it at the allocation boundary
		// to prove fromArray takes an owned snapshot before calling native code.
		const source = Buffer.from([3, 5, 7]);
		const operation = module._vero_web_memory_operation;
		module._vero_web_memory_operation = (...args) => {
			if (args[1] === 1) {
				source.fill(42);
			}
			return operation(...args);
		};
		let snapshot;
		try {
			snapshot = memory.fromArray(source);
		}
		finally
		{
			module._vero_web_memory_operation = operation;
		}
		try {
			if (String(memory.toArray(snapshot)) !== "3,5,7") {
				throw new Error("fromArray retained Node Buffer aliases");
			}
			const copy = memory.toArray(snapshot);
			copy[0] = 11;
			if (memory.read(snapshot, 0n, "uint8") !== 3) {
				throw new Error("toArray exposed leased backing storage");
			}
			snapshot.dispose();
			if (String(copy) !== "11,5,7") {
				throw new Error("toArray snapshot expired with its source region");
			}
		}
		finally
		{
			snapshot.dispose();
		}
	}
	finally
	{
		region.dispose();
	}
	const baseline = memory.stats.ownedBytes;
	const overlapping = memory.fromArray(new Uint8Array([1, 2, 3, 4, 5]));
	const staleView = memory.slice(overlapping, 0n, 5n);
	try {
		memory.copy(overlapping, 1n, overlapping, 0n, 4n);
		if (String(memory.toArray(overlapping)) !== "1,1,2,3,4") {
			throw new Error("backward overlapping copy corrupted source bytes");
		}
		memory.copy(overlapping, 0n, overlapping, 1n, 4n);
		if (String(memory.toArray(overlapping)) !== "1,2,3,4,4") {
			throw new Error("forward overlapping copy corrupted source bytes");
		}
		memory.resize(overlapping, 5n, true);
		if (memory.size(staleView) !== 5n) {
			throw new Error("no-op reserve invalidated a view");
		}
		memory.resize(overlapping, 8n, true);
		expectFailure(() => memory.size(staleView), "view after capacity change");
		if (memory.size(overlapping) !== 5n || memory.capacity(overlapping) !== 8n) {
			throw new Error("reserve changed logical size or lost requested capacity");
		}
		memory.resize(overlapping, 3n);
		memory.resize(overlapping, 5n);
		if (String(memory.toArray(overlapping)) !== "1,2,3,0,0") {
			throw new Error("resize exposed bytes hidden by an earlier shrink");
		}
		const lease = memory.acquireLease(overlapping);
		try {
			overlapping.dispose();
			expectFailure(() => memory.size(overlapping), "disposed region");
			if (memory.stats.ownedBytes !== baseline + 8n ||
					lease.withBytes(bytes => String(bytes)) !== "1,2,3,0,0") {
				throw new Error("region disposal released storage retained by a lease");
			}
		}
		finally
		{
			lease.dispose();
		}
	}
	finally
	{
		staleView.dispose();
		overlapping.dispose();
	}
	if (memory.stats.ownedBytes !== baseline) {
		throw new Error("disposed regions and leases retained managed storage");
	}
	memory.setLimits(8n, 12n);
	const limited = memory.buffer(8n);
	try {
		expectFailure(() => memory.buffer(9n), "single-buffer quota");
		expectFailure(() => memory.buffer(5n), "retained quota");
		expectFailure(() => memory.setLimits(7n, 12n), "quota below live storage");
		if (memory.stats.ownedBytes !== 8n || memory.size(limited) !== 8n) {
			throw new Error("rejected quota change modified live storage");
		}
	}
	finally
	{
		limited.dispose();
		memory.setLimits(16n * 1024n * 1024n, 32n * 1024n * 1024n);
	}
	for (const backend of ["vm", "mixed"]) {
		const run = runtime.runSource(`import "memory.vs";
int main() {
  auto bytes = new std::memory::buffer(8);
  std::memory::write<int64>(bytes, 0, -9223372036854775807i64 - 1i64);
  assert(std::memory::read<int64>(bytes, 0) == -9223372036854775807i64 - 1i64);
  assert(!std::memory::native_addresses_available());
  return 17;
}`, { backend });
		if (!run.success || run.exitCode !== 17) {
			throw new Error(`script memory failed in ${backend}: ${JSON.stringify(run)}`);
		}
	}
	const compiled = runtime.compileSource(`import "memory.vs";
auto bytes = new std::memory::buffer(16);
int main(string[] args) {
  assert(bytes.get(0) == 0u);
  bytes.set(0, 9u);
  assert(args[0] != "fail");
  return bytes.size();
}`);
	if (!compiled.program) {
		throw new Error(`memory recovery fixture did not compile: ${JSON.stringify(compiled.result)}`);
	}
	try {
		for (const backend of ["vm", "mixed"]) {
			const failed = compiled.program.run({ backend, args: ["fail"] });
			const recovered = compiled.program.run({ backend, args: ["ok"] });
			if (failed.success || failed.diagnostics.length === 0 ||
					!recovered.success || recovered.exitCode !== 16 || recovered.diagnostics.length !== 0) {
				throw new Error(`memory globals survived a failed Script run: ${JSON.stringify({ failed, recovered })}`);
			}
		}
	}
	finally
	{
		compiled.program.dispose();
	}
}

async function assertReusableOptions(factory) {
	class Options {
		#located = 0;

		get located() { return this.#located; }

		locateFile(path, prefix) {
			++this.#located;
			return prefix + path;
		}
	}
	const options = Object.freeze(new Options());
	const first = await factory(options);
	const second = await factory(options);
	try {
		if (options.located < 2 || Object.keys(options).length !== 0) {
			throw new Error("runtime creation changed caller options or lost inherited locateFile");
		}
		const firstBytes = first.memory.fromArray(new Uint8Array([11]));
		const secondBytes = second.memory.fromArray(new Uint8Array([22]));
		try {
			if (first.memory.stats.ownedBytes !== 1n || second.memory.stats.ownedBytes !== 1n ||
					first.memory.read(firstBytes, 0n, "uint8") !== 11 ||
					second.memory.read(secondBytes, 0n, "uint8") !== 22) {
				throw new Error("runtime instances shared memory after reusing their configuration");
			}
		}
		finally
		{
			firstBytes.dispose();
			secondBytes.dispose();
		}
		first.dispose();
		const result = second.runSource("int main() { return 42; }");
		if (!result.success || result.exitCode !== 42) {
			throw new Error("disposing one runtime invalidated another runtime");
		}
	}
	finally
	{
		first.dispose();
		second.dispose();
	}
}

async function assertInteractiveAllocationFailures() {
	const directory = join(temporaryDirectory, "input-allocation");
	await mkdir(directory);
	let loader = await readFile(join(artifactDirectory, "veroscript-engine.mjs"), "utf8");
	// Inject only the input bridge's allocation, leaving compiler/VM allocation intact.
	for (const [pattern, replacement] of [
		[/const\s+result\s*=\s*_malloc\(size\)/g,
			"const result=Module.veroWebTestInputAllocationFailure ? 0 : _malloc(size)"],
		[/const\s+byteLength\s*=\s*lengthBytesUTF8\(line\)/g,
			"const byteLength=Module.veroWebTestInputLengthOverflow ? 0xffffffff : lengthBytesUTF8(line)"],
	]) {
		if ([...loader.matchAll(pattern)].length !== 1) {
			throw new Error("Expected one interactive input allocation site for fault injection");
		}
		loader = loader.replace(pattern, replacement);
	}
	await writeFile(join(directory, "veroscript-engine.mjs"), loader);
	await copyFile(join(artifactDirectory, "veroscript-engine.wasm"), join(directory, "veroscript-engine.wasm"));
	await copyFile(runnableBindingsPath, join(directory, "veroscript-webassembly.mjs"));
	const { createVeroScriptRuntime: createFaultRuntime } = await import(
		pathToFileURL(join(directory, "veroscript-webassembly.mjs")).href
	);
	const faultRuntime = await createFaultRuntime();
	const module = faultRuntime.module;
	const source = `import "io.vs";
import "memory.vs";
auto bytes = new std::memory::buffer(16);
int main()
{
    array<string> values = {"owned local"};
    assert(bytes.get(0) == 0u);
    bytes.set(0, 7u);
    print(std::io::read_line());
    return 23;
}`;
	try {
		const compiled = faultRuntime.compileSource(source);
		if (!compiled.program) {
			throw new Error(`interactive allocation fixture failed to compile: ${JSON.stringify(compiled.result)}`);
		}
		try {
			for (const flag of ["veroWebTestInputAllocationFailure", "veroWebTestInputLengthOverflow"]) {
				for (const backend of ["vm", "mixed"]) {
					for (const run of [
						options => faultRuntime.runSourceInteractive(source, options),
						options => compiled.program.runInteractive(options),
					]) {
						let requests = 0;
						module[flag] = true;
						const failed = await run({ backend, readLine() { ++requests; return "provided input"; } });
						if (failed.success || failed.operation !== "api" || failed.exitCode === 0 ||
								!failed.errorOutput || requests !== 1) {
							throw new Error(`input allocation failure became EOF: ${JSON.stringify(failed)}`);
						}
						module[flag] = false;
						const recovered = await run({ backend, readLine() { return "recovered input"; } });
						if (!recovered.success || recovered.exitCode !== 23 || recovered.output !== "recovered input\n") {
							throw new Error(`input allocation failure left the runtime unusable: ${JSON.stringify(recovered)}`);
						}
					}
				}
			}
		}
		finally
		{
			compiled.program.dispose();
		}
	}
	finally
	{
		delete module.veroWebTestInputAllocationFailure;
		delete module.veroWebTestInputLengthOverflow;
		faultRuntime.dispose();
	}
}

try {
	assertDiagnosticSeverities(runtime, runtimeModule);
	const originalLengthBytesUTF8 = runtimeModule.lengthBytesUTF8;
	if (typeof originalLengthBytesUTF8 !== "function") {
		throw new Error("Emscripten module did not expose UTF-8 sizing");
	}
	let oversizedStringRejected = false;
	runtimeModule.lengthBytesUTF8 = () => 0xffffffff;
	try {
		runtime.checkSource("void main() {}");
	}
	catch (error)
	{
		oversizedStringRejected = error instanceof RangeError;
	}
	finally
	{
		runtimeModule.lengthBytesUTF8 = originalLengthBytesUTF8;
	}
	if (!oversizedStringRejected) {
		throw new Error("Wasm32 string-size overflow was not rejected before allocation");
	}

	if (runtime.supportsThreads) {
		throw new Error("default wrapper runtime unexpectedly advertises pthread support");
	}
	const expectedLanguages = ["en-US", "fr-FR", "ja-JP", "ko-KR", "zh-CN", "zh-TW"];
	if (
		!expectedLanguages.every((language) => runtime.availableLanguages.includes(language))
	) {
		throw new Error(`unexpected wrapper languages: ${JSON.stringify(runtime.availableLanguages)}`);
	}
	const result = runtime.runSource(
		`import "helper.vs";
int main(string[] args)
{
    return answer() + len(args);
}`,
		{
			fileLabel: "main.vs",
			args: ["x", "y"],
			backend: "mixed",
			files: [
				{
					path: "helper.vs",
					source: `export int answer()
{
    return 40;
}`,
				},
			],
		},
	);
	if (!result.success || result.exitCode !== 42 || result.backend !== "mixed") {
		throw new Error(`unexpected wrapper run result: ${JSON.stringify(result)}`);
	}

	if (!runtime.supportsCompiledPrograms) {
		throw new Error("wrapper runtime does not expose compiled program handles");
	}
	assertCompiledFailureRecovery(runtime);
	await assertStandardLibraryLimits(runtime);
	await assertArgumentTableBounds(runtime, runtimeModule);
	await assertInteractiveOptionReentry(runtime, runtimeModule);
	await assertInteractiveProviderAbort(runtime, runtimeModule);
	const compiled = runtime.compileSource(`int increment(int value) { return value + 1; }
int main(string[] args)
{
    int value = 0;
    for (int i = 0; i < 1000; i++) value = increment(value);
    return len(args);
}`, { fileLabel: "compiled-wrapper.vs" });
	if (!compiled.result.success || !compiled.program || compiled.result.timings?.compileMs == null) {
		throw new Error(`unexpected wrapper compile result: ${JSON.stringify(compiled.result)}`);
	}
	assertMissingBackend(runtime.runSource(`print("must not run");`, { backend: "jit" }));
	assertMissingBackend(compiled.program.run({ backend: "jit" }));
	assertMissingBackend(await runtime.runSourceInteractive(`import "io.vs"; void main() { print(std::io::read_line()); }`, {
		backend: "jit",
		readLine() { throw new Error("JIT rejection must precede input"); },
	}));
	assertMissingBackend(await compiled.program.runInteractive({
		backend: "jit",
		readLine() { throw new Error("JIT rejection must precede input"); },
	}));
	const compiledMixed = compiled.program.run({ args: ["x", "y"], backend: "mixed" });
	const compiledVm = compiled.program.run({ args: ["x"], backend: "vm" });
	if (
		!compiledMixed.success || compiledMixed.exitCode !== 2 ||
		compiledMixed.timings?.executeMs == null ||
		!compiledMixed.jit ||
		compiledMixed.jit.dispatches !== 0 ||
		!compiledVm.success || compiledVm.exitCode !== 1 ||
		compiledVm.jit?.dispatches !== 0
	) {
		throw new Error(`unexpected compiled runs: ${JSON.stringify({ compiledMixed, compiledVm })}`);
	}
	compiled.program.dispose();
	let disposedProgramRejected = false;
	try {
		compiled.program.run();
	}
	catch
	{
		disposedProgramRejected = true;
	}
	if (!disposedProgramRejected) {
		throw new Error("disposed compiled program remained runnable");
	}
	const failedCompile = runtime.compileSource("void main( {");
	if (failedCompile.result.success || failedCompile.program !== null) {
		throw new Error(`failed compilation retained a program: ${JSON.stringify(failedCompile.result)}`);
	}

	runtime.setLanguage("zh_Hans.UTF-8");
	if (runtime.language !== "zh-CN") {
		throw new Error(`wrapper did not expose canonical language: ${runtime.language}`);
	}
	const invalid = runtime.checkSource('import "missing.vs";');
	const localizedImportError = invalid.diagnostics.some(
		(diagnostic) =>
			diagnostic.code === "diag.cannot_resolve_import" &&
			/[\u4e00-\u9fff]/u.test(diagnostic.message),
	);
	if (invalid.success || !localizedImportError) {
		throw new Error(`unexpected localized check result: ${JSON.stringify(invalid)}`);
	}

	const lines = ["first", "second"];
	const requests = [];
	let reentryBlocked = false;
	let disposeBlocked = false;
	const interactive = await runtime.runSourceInteractive(
		`import "io.vs";
int main()
{
    print(std::io::read_line());
    print(std::io::read_line());
    return 9;
}`,
		{
			fileLabel: "interactive-wrapper.vs",
			backend: "mixed",
			async readLine(request) {
				requests.push(request.index);
				if (request.index === 0) {
					try {
						runtime.checkSource(`void main()
{
}`);
					}
					catch
					{
						reentryBlocked = true;
					}
					try {
						runtime.dispose();
					}
					catch
					{
						disposeBlocked = true;
					}
				}
				await Promise.resolve();
				return lines.shift() ?? null;
			},
		},
	);
	if (
		!interactive.success ||
		interactive.output !== "first\nsecond\n" ||
		interactive.exitCode !== 9 ||
		JSON.stringify(requests) !== "[0,1]" ||
		!reentryBlocked ||
		!disposeBlocked
	) {
		throw new Error(`unexpected interactive wrapper result: ${JSON.stringify(interactive)}`);
	}

	const interactiveCompiled = runtime.compileSource(`import "io.vs";
int main()
{
    print(std::io::read_line());
    return 7;
}`);
	if (!interactiveCompiled.program) {
		throw new Error(`interactive program did not compile: ${JSON.stringify(interactiveCompiled.result)}`);
	}
	let interactiveProgramDisposeBlocked = false;
	const interactiveProgramResult = await interactiveCompiled.program.runInteractive({
		backend: "mixed",
		readLine() {
			try {
				interactiveCompiled.program.dispose();
			}
			catch
			{
				interactiveProgramDisposeBlocked = true;
			}
			return "compiled interactive";
		},
	});
	if (
		!interactiveProgramResult.success ||
		interactiveProgramResult.exitCode !== 7 ||
		interactiveProgramResult.output !== "compiled interactive\n" ||
		!interactiveProgramDisposeBlocked
	) {
		throw new Error(`unexpected interactive compiled result: ${JSON.stringify(interactiveProgramResult)}`);
	}

	// A failed input provider must release Asyncify without losing the compiled
	// program or carrying captured output into its next execution.
	const compiledProviderError = new Error("compiled input provider failed");
	for (const failure of [
		{
			readLine() { throw compiledProviderError; },
			matches: (error) => error === compiledProviderError,
		},
		{
			readLine() { return "invalid\0line"; },
			matches: (error) => error instanceof TypeError,
		},
		{
			readLine() { return false; },
			matches: (error) => error instanceof TypeError,
		},
	]) {
		let inputFailureRejected = false;
		try {
			await interactiveCompiled.program.runInteractive({ readLine: failure.readLine });
		}
		catch (error)
		{
			inputFailureRejected = failure.matches(error);
		}
		const recovered = interactiveCompiled.program.run({ input: "recovered\n" });
		if (!inputFailureRejected || !recovered.success || recovered.exitCode !== 7 ||
				recovered.output !== "recovered\n" || interactiveCompiled.program.disposed) {
			throw new Error(`compiled input failure did not recover: ${JSON.stringify(recovered)}`);
		}
	}
	interactiveCompiled.program.dispose();

	const providerError = new Error("input provider failed");
	let rejectedWithProviderError = false;
	try {
		await runtime.runSourceInteractive(`import "io.vs";
void main()
{
    std::io::pause();
}`, {
			readLine() {
				throw providerError;
			},
		});
	}
	catch (error)
	{
		rejectedWithProviderError = error === providerError;
	}
	if (!rejectedWithProviderError || !runtime.checkSource(`void main()
{
}`).success) {
		throw new Error("interactive provider failure did not reject and release the module");
	}

	let nulInputRejected = false;
	try {
		await runtime.runSourceInteractive(`import "io.vs";
void main()
{
    std::io::pause();
}`, {
			readLine() {
				return "invalid\0line";
			},
		});
	}
	catch (error)
	{
		nulInputRejected = error instanceof TypeError;
	}
	if (!nulInputRejected || !runtime.checkSource(`void main()
{
}`).success) {
		throw new Error("interactive NUL input did not reject and release the module");
	}


	assertMemoryBoundaries(runtime, runtimeModule);
	const scriptMemory = runtime.runSource(`import "memory.vs";
void main() {
  assert(!std::memory::native_addresses_available());
  auto bytes=new std::memory::buffer(8);
  std::memory::write<uint64>(bytes,0,18446744073709551615u64);
  auto view=bytes.readonly_slice(0,8);
  assert(std::memory::read<uint64>(view,0)==18446744073709551615u64);
  print("memory script passed");
}`, { backend: "vm" });
	if (!scriptMemory.success || scriptMemory.output !== "memory script passed\n") {
		throw new Error(`Script memory services failed: ${JSON.stringify(scriptMemory)}`);
	}
	const unavailableAddress = runtime.runSource("void main(){int n=1;std::memory::address_of(&n);}");
	if (unavailableAddress.success) {
		throw new Error("Wasm advertised a native address");
	}

	const memory = runtime.memory;
	memory.setLimits(2n ** 63n, 2n ** 63n);
	const bytes = memory.buffer(16n);
	memory.write(bytes, 1n, "uint64", 0xffffffffffffffffn, "big");
	if (memory.read(bytes, 1n, "uint64", "big") !== 0xffffffffffffffffn) {
		throw new Error("memory uint64 was truncated");
	}
	const view = memory.slice(bytes, 0n, 8n, true);
	let readonlyRejected = false;
	try {
		memory.fill(view, 0n, 0n, 1);
	}
	catch
	{
		readonlyRejected = true;
	}
	if (!readonlyRejected) {
		throw new Error("read-only region accepted a write");
	}
	const clone = memory.clone(bytes);
	if (memory.objectId(bytes) === memory.objectId(clone)) {
		throw new Error("clone reused object identity");
	}
	const lease = memory.acquireLease(bytes, true);
	lease.withBytes(data => {
		data[0] = 42;
	});
	// Allocate enough to grow the heap; the existing lease must rebuild its view.
	const growth = memory.buffer(96n * 1024n * 1024n);
	lease.withBytes(data => {
		if (data[0] !== 42) {
			throw new Error("lease view detached after heap growth");
		}
	});
	growth.dispose();
	lease.dispose();
	const other = await createFastVeroScriptRuntime();
	try {
		let foreignRejected = false;
		try {
			other.memory.size(bytes);
		}
		catch
		{
			foreignRejected = true;
		}
		if (!foreignRejected) {
			throw new Error("foreign region accepted");
		}
	}
	finally
	{
		other.dispose();
	}
	bytes.dispose();
	view.dispose();
	clone.dispose();
	const retained = memory.buffer(1n), finalLease = memory.acquireLease(retained);
	runtime.dispose();
	finalLease.withBytes(data => {
		if (data.length !== 1) {
			throw new Error("lease did not survive runtime");
		}
	});
	finalLease.dispose();
	retained.dispose();
}
finally
{
	runtime.dispose();
}

const fastRuntime = await createFastVeroScriptRuntime(runtimeOptions);
const fastModule = fastRuntime.module;
try {
	assertDiagnosticSeverities(fastRuntime, fastModule);
	await assertArgumentTableBounds(fastRuntime, fastModule);
	if (fastRuntime.supportsThreads) {
		throw new Error("fast wrapper runtime unexpectedly advertises pthread support");
	}
	if (fastRuntime.supportsInteractiveInput) {
		throw new Error("fast runtime incorrectly advertises interactive input");
	}
	if (!fastRuntime.supportsCompiledPrograms) {
		throw new Error("fast runtime does not support compiled programs");
	}
	assertCompiledFailureRecovery(fastRuntime);
	await assertStandardLibraryLimits(fastRuntime);
	assertMemoryBoundaries(fastRuntime, fastModule);
	const fastResult = fastRuntime.runSource(`int main()
{
    return 42;
}`);
	if (!fastResult.success || fastResult.exitCode !== 42) {
		throw new Error(`unexpected fast wrapper result: ${JSON.stringify(fastResult)}`);
	}
	assertMissingBackend(fastRuntime.runSource(`print("must not run");`, { backend: "jit" }));
	const fastCompiled = fastRuntime.compileSource(`int main() { return 33; }`);
	if (!fastCompiled.program || fastCompiled.program.run({ backend: "mixed" }).exitCode !== 33) {
		throw new Error("fast compiled program did not run");
	}
	let interactiveRejected = false;
	try {
		await fastRuntime.runSourceInteractive(`void main()
{
}`, { readLine() { return null; } });
	}
	catch
	{
		interactiveRejected = true;
	}
	if (!interactiveRejected) {
		throw new Error("fast runtime accepted interactive execution");
	}
}
finally
{
	fastRuntime.dispose();
}

await assertReusableOptions(createFastVeroScriptRuntime);
await assertReusableOptions(createVeroScriptRuntime);
await assertInteractiveAllocationFailures();

cleanupTemporaryDirectory();
process.removeListener("exit", cleanupTemporaryDirectory);
console.log("VeroScript TypeScript bindings smoke test passed");
