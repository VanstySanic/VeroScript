// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import { createServer } from "node:http";
import { readFile, stat } from "node:fs/promises";
import { extname, resolve, sep } from "node:path";
import { transform } from "esbuild";
import { chromium } from "playwright";
import {
	compileExtensionFailureFixtures, consoleFailureFixtures, currentCoreFixtures, incompatibleReflectionSource,
	incompleteDeclarationSource, interactiveConsoleSource, interactiveStateFixtures, invalidConsoleSources,
} from "./VeroScriptWebAssemblyCoreFixtures.mjs";

const outputDirectoryArgument = process.argv[2];
if (!outputDirectoryArgument) {
	throw new Error("usage: node VeroScriptWebAssemblyBrowserSmoke.mjs <web-sdk-output-directory>");
}

const outputDirectory = resolve(outputDirectoryArgument);
const requiredFiles = [
	"veroscript-engine.mjs",
	"veroscript-engine.wasm",
	"veroscript-engine-fast.mjs",
	"veroscript-engine-fast.wasm",
	"veroscript-webassembly.ts",
];
for (const fileName of requiredFiles) {
	const file = await stat(resolve(outputDirectory, fileName));
	if (!file.isFile() || file.size === 0) {
		throw new Error(`browser smoke input is missing or empty: ${fileName}`);
	}
}

const bindingsSource = await readFile(resolve(outputDirectory, "veroscript-webassembly.ts"), "utf8");
const transformedBindings = await transform(bindingsSource, {
	format: "esm",
	loader: "ts",
	sourcemap: "inline",
	target: "es2022",
});

const mimeTypes = new Map([
	[".html", "text/html; charset=utf-8"],
	[".js", "text/javascript; charset=utf-8"],
	[".mjs", "text/javascript; charset=utf-8"],
	[".wasm", "application/wasm"],
]);

const server = createServer(async (request, response) => {
	try {
		const requestUrl = new URL(request.url ?? "/", "http://127.0.0.1");
		if (requestUrl.pathname === "/") {
			response.writeHead(200, { "Content-Type": "text/html; charset=utf-8" });
			response.end("<!doctype html><meta charset=utf-8><title>VeroScript WebAssembly smoke</title>");
			return;
		}
		if (requestUrl.pathname === "/veroscript-webassembly.js") {
			response.writeHead(200, { "Content-Type": "text/javascript; charset=utf-8" });
			response.end(transformedBindings.code);
			return;
		}

		const relativePath = decodeURIComponent(requestUrl.pathname).replace(/^\/+/, "");
		const filePath = resolve(outputDirectory, relativePath);
		if (filePath !== outputDirectory && !filePath.startsWith(`${outputDirectory}${sep}`)) {
			response.writeHead(403);
			response.end("forbidden");
			return;
		}
		const contents = await readFile(filePath);
		response.writeHead(200, {
			"Cache-Control": "no-store",
			"Content-Type": mimeTypes.get(extname(filePath)) ?? "application/octet-stream",
		});
		response.end(contents);
	}
	catch (error)
	{
		response.writeHead(error?.code === "ENOENT" ? 404 : 500);
		response.end("not found");
	}
});

await new Promise((resolveListen, rejectListen) => {
	server.once("error", rejectListen);
	server.listen(0, "127.0.0.1", resolveListen);
});
const address = server.address();
if (address === null || typeof address === "string") {
	server.close();
	throw new Error("unable to determine browser smoke server address");
}
const origin = `http://127.0.0.1:${address.port}`;

const standardLibrarySource = String.raw`
import "core.vs";
int main()
{
    array<int> values = {9, 2, 7, 2, 5};
    std::algorithm::sort(values);
    array<int> first_copy = std::algorithm::copy(values);
    array<int> second_copy = std::algorithm::copy(values);
    array<unsigned int> first = std::random::make_engine(2026u);
    array<unsigned int> second = std::random::make_engine(2026u);
    std::algorithm::shuffle(first_copy, first);
    std::algorithm::shuffle(second_copy, second);
    assert(std::algorithm::equal(first_copy, second_copy));
    assert(std::numeric::sum(values) == 25);
    channel<int> mailbox = std::concurrent::make_channel<int>(1);
    assert(std::concurrent::try_send(mailbox, 42));
    print(std::concurrent::recv(mailbox));
    return std::random::next_int(first, 1, 7);
}
`;

const unsupportedThreadingSource = String.raw`
import "concurrent.vs";
void worker(channel<int> output)
{
    output.send(42);
}
void main()
{
    channel<int> output = channel<int>();
    int task = std::concurrent::spawn("worker", output);
    std::concurrent::join_task(task);
}
`;

const currentCoreSource = String.raw`
import "io.vs";
import "concurrent.vs";
int length_kind(int value) { return 1; }
int length_kind(int64 value) { return 2; }
struct Counter
{
    int value;
    int add(int delta)
    {
        value += delta;
        return value;
    }
};
int main()
{
    assert(std::filesystem::is_file("/stdlib/io.vs"));
    assert(std::filesystem::is_directory("/stdlib"));
    array<int> values = {3, 7};
    assert(length_kind(len(values)) == 2);
    assert(length_kind(len("abc")) == 2);
    assert(length_kind(values.capacity()) == 2);
    values.reserve(8i64);
    int64 index = 1i64;
    values[index] += 2;
    int narrowed = len(values);
    assert(narrowed == 2 && values[index] == 9);
    channel<int> wide_capacity = std::concurrent::make_channel<int>(2147483648i64);
    assert(wide_capacity.capacity() == 2147483648i64);
    type<Counter> info = type<Counter>();
    int field_index = -1, method_index = -1;
    for (int i = 0; i < info.field_count(); i++)
    {
        field_info<Counter> candidate = field_info<Counter>(info.qualified_name, i);
        if (candidate.name() == "value")
        {
            field_index = i;
        }
    }
    for (int i = 0; i < info.method_count(); i++)
    {
        method_info<Counter> candidate = method_info<Counter>(info.qualified_name, i);
        if (candidate.name() == "add")
        {
            method_index = i;
        }
    }
    assert(field_index >= 0 && method_index >= 0);
    field_info<Counter> field = field_info<Counter>(info.qualified_name, field_index);
    method_info<Counter> method = method_info<Counter>(info.qualified_name, method_index);
    Counter counter = Counter();
    field.set<int>(counter, 40);
    return method.invoke<int>(counter, 2);
}
`;

const unsupportedCallableSource = String.raw`
import "concurrent.vs";
int double_value(int value)
{
    return value * 2;
}
void main()
{
    task<int> pending = std::concurrent::spawn_task<int>(double_value, 21);
    print(pending.await());
}
`;

async function launchTestBrowser() {
	const configuredChannel = process.env.VEROSCRIPT_WEBASSEMBLY_BROWSER_CHANNEL;
	if (configuredChannel) {
		return chromium.launch({ channel: configuredChannel, headless: true });
	}

	try {
		return await chromium.launch({ headless: true });
	}
	catch (bundledBrowserError)
	{
		for (const channel of ["chrome", "msedge"]) {
			try {
				return await chromium.launch({ channel, headless: true });
			}
			catch
			{
				// Try the next installed Chromium channel.
			}
		}
		throw bundledBrowserError;
	}
}

let browser;
try {
	browser = await launchTestBrowser();
	const page = await browser.newPage();
	const browserErrors = [];
	page.on("pageerror", (error) => browserErrors.push(error.message));
	await page.goto(origin, { waitUntil: "load" });

	const result = await page.evaluate(
		async ({ origin: pageOrigin, standardSource, threadingSource, coreSource, callableSource,
						 coreFixtures, compileExtensionFailures, incompatibleReflection, incompleteDeclaration,
						 consoleFailures, invalidConsole, interactiveConsole, interactiveFixtures }) => {
			const sdk = await import(`${pageOrigin}/veroscript-webassembly.js`);
			const assertMissingBackend = (result) => {
				if (result.success || result.backend !== "jit" || result.output !== "" || !result.diagnostics.some(
					(diagnostic) => diagnostic.code === "diag.jit_execution_requires_an_explicitly_selected_backend_file_directory_or_factory"
				)) {
					throw new Error(`browser hid missing JIT backend: ${JSON.stringify(result)}`);
				}
			};
			const loadedResources = () => performance
				.getEntriesByType("resource")
				.map((entry) => new URL(entry.name).pathname);
			if (
				loadedResources().includes("/veroscript-engine.mjs") ||
				loadedResources().includes("/veroscript-engine-fast.mjs")
			) {
				throw new Error("WebAssembly SDK eagerly loaded an engine before a runtime was selected");
			}
			const fastOptions = Object.freeze({});
			const fastRuntime = await sdk.createFastVeroScriptRuntime(fastOptions);
			let disposedRuntimeRejected = false;
			try {
				if (
					loadedResources().includes("/veroscript-engine.mjs") ||
					!loadedResources().includes("/veroscript-engine-fast.mjs")
				) {
					throw new Error("fast runtime loaded the interactive engine or missed its own loader");
				}
				if (
					fastRuntime.supportsInteractiveInput ||
					fastRuntime.supportsThreads ||
					!fastRuntime.supportsCompiledPrograms
				) {
					throw new Error("fast runtime advertised unsupported browser capabilities");
				}
				const standardResult = fastRuntime.runSource(standardSource, { backend: "mixed" });
				if (
					!standardResult.success ||
					standardResult.output !== "42\n" ||
					standardResult.exitCode < 1 ||
					standardResult.exitCode > 6
				) {
					throw new Error(`browser standard library failed: ${JSON.stringify(standardResult)}`);
				}
				assertMissingBackend(fastRuntime.runSource(`print("must not run");`, { backend: "jit" }));
				const deepAssignment = fastRuntime.checkSource(
					`void main() { int x = 0; ${"x=".repeat(10000)}1; }`,
				);
				if (deepAssignment.success || !deepAssignment.diagnostics.some((diagnostic) =>
					diagnostic.message.includes("expression chain is too long")
				)) {
					throw new Error(`browser parser boundary changed: ${JSON.stringify(deepAssignment)}`);
				}
				for (const fixture of compileExtensionFailures) {
					const options = { files: fixture.files };
					for (const [operation, invoke] of [
						["check", () => fastRuntime.checkSource(fixture.source, options)],
						["compile", () => {
							const compiled = fastRuntime.compileSource(fixture.source, options);
							if (compiled.program) {
								compiled.program.dispose();
								throw new Error(`browser ${fixture.name} created a program`);
							}
							return compiled.result;
						}],
						["run", () => fastRuntime.runSource(fixture.source, options)],
					]) {
						const result = invoke();
						if (result.success || result.output !== "" ||
								!result.diagnostics.some(diagnostic => diagnostic.severity === "error" &&
									(!fixture.diagnosticMessagePattern ||
										new RegExp(fixture.diagnosticMessagePattern).test(diagnostic.message)))) {
							throw new Error(`browser ${fixture.name} ${operation} rejection changed: ${JSON.stringify(result)}`);
						}
						const recovered = fastRuntime.runSource('int main() { print("recovered"); return 42; }');
						if (!recovered.success || recovered.exitCode !== 42 || recovered.exitRequested ||
								recovered.output !== "recovered\n" || recovered.errorOutput !== "" ||
								recovered.diagnostics.some(diagnostic => diagnostic.severity === "error")) {
							throw new Error(`browser ${fixture.name} ${operation} left stale state: ${JSON.stringify(recovered)}`);
						}
					}
				}
				for (const fixture of coreFixtures) {
					const checked = fastRuntime.checkSource(fixture.source, { files: fixture.files });
					if (!checked.success) {
						throw new Error(`browser ${fixture.name} check failed: ${JSON.stringify(checked)}`);
					}
					for (const backend of ["vm", "mixed"]) {
						const result = fastRuntime.runSource(fixture.source, { backend, files: fixture.files });
						if (!result.success || result.exitCode !== 42 ||
								result.exitRequested !== (fixture.exitRequested ?? false) ||
								result.output !== (fixture.output ?? "") || result.errorOutput !== (fixture.errorOutput ?? "")) {
							throw new Error(`browser ${fixture.name} failed: ${JSON.stringify(result)}`);
						}
					}
					const compiled = fastRuntime.compileSource(fixture.source, { files: fixture.files });
					if (!compiled.program) {
						throw new Error(`browser ${fixture.name} compile failed: ${JSON.stringify(compiled.result)}`);
					}
					try {
						for (const backend of ["vm", "mixed"]) {
							const result = compiled.program.run({ backend });
							if (!result.success || result.exitCode !== 42 ||
									result.exitRequested !== (fixture.exitRequested ?? false) ||
									result.output !== (fixture.output ?? "") || result.errorOutput !== (fixture.errorOutput ?? "")) {
								throw new Error(`browser compiled ${fixture.name} failed: ${JSON.stringify(result)}`);
							}
						}
					}
					finally
					{
						compiled.program.dispose();
					}
				}
				const assertConsoleFailure = (result, operation) => {
					if (result.success || result.exitCode === 0 || result.output !== "before" ||
							!result.diagnostics.some(diagnostic => diagnostic.severity === "error" &&
								diagnostic.message.includes(`std::console::${operation}`)) ||
							result.errorOutput.includes("\x1b")) {
						throw new Error(`browser console validation failed: ${JSON.stringify(result)}`);
					}
					const recovered = fastRuntime.runSource('int main() { print("recovered"); return 42; }');
					if (!recovered.success || recovered.exitCode !== 42 || recovered.output !== "recovered\n" ||
							recovered.errorOutput !== "") {
						throw new Error(`browser console failure left stale state: ${JSON.stringify(recovered)}`);
					}
				};
				for (const fixture of consoleFailures) {
					for (const backend of ["vm", "mixed"]) {
						assertConsoleFailure(fastRuntime.runSource(fixture.source, { backend }), fixture.operation);
					}
					const compiled = fastRuntime.compileSource(fixture.source);
					if (!compiled.program) {
						throw new Error(`browser console fixture did not compile: ${JSON.stringify(compiled.result)}`);
					}
					try {
						for (const backend of ["vm", "mixed"]) {
							assertConsoleFailure(compiled.program.run({ backend }), fixture.operation);
						}
					}
					finally
					{
						compiled.program.dispose();
					}
				}
				for (const source of invalidConsole) {
					const result = fastRuntime.checkSource(source);
					if (result.success || !result.diagnostics.some(diagnostic => diagnostic.severity === "error")) {
						throw new Error(`browser accepted an invalid console type/arity: ${JSON.stringify(result)}`);
					}
				}
				const incomplete = fastRuntime.checkSource(incompleteDeclaration);
				if (incomplete.success || incomplete.diagnostics.length === 0) {
					throw new Error("browser accepted an incomplete declaration");
				}
				for (const backend of ["vm", "mixed"]) {
					const result = fastRuntime.runSource(incompatibleReflection, { backend });
					if (result.success || result.diagnostics.length === 0) {
						throw new Error("browser reflection accepted incompatible signedness");
					}
				}
				const currentCore = fastRuntime.runSource(coreSource, { backend: "mixed" });
				if (!currentCore.success || currentCore.exitCode !== 42) {
					throw new Error(`browser current-core integration failed: ${JSON.stringify(currentCore)}`);
				}
				const memorySource = `import "memory.vs";
int main() {
  auto bytes = new std::memory::buffer(8);
  std::memory::write<uint64>(bytes, 0, 18446744073709551615u64);
  auto view = bytes.readonly_slice(0, 8);
  assert(std::memory::read<uint64>(view, 0) == 18446744073709551615u64);
  assert(!std::memory::native_addresses_available());
  return 19;
}`;
				const memoryProgram = fastRuntime.compileSource(memorySource);
				if (!memoryProgram.program) {
					throw new Error(`browser memory compile failed: ${JSON.stringify(memoryProgram.result)}`);
				}
				try {
					for (const backend of ["vm", "mixed"]) {
						const sourceRun = fastRuntime.runSource(memorySource, { backend });
						const compiledRun = memoryProgram.program.run({ backend });
						if (!sourceRun.success || sourceRun.exitCode !== 19 ||
								!compiledRun.success || compiledRun.exitCode !== 19) {
							throw new Error(`browser memory execution failed: ${JSON.stringify({ sourceRun, compiledRun })}`);
						}
					}
				}
				finally
				{
					memoryProgram.program.dispose();
				}
				const hostBytes = fastRuntime.memory.fromArray(new Uint8Array([1, 2, 3, 4, 5, 6, 7, 8]));
				try {
					fastRuntime.memory.write(hostBytes, 0n, "uint64", 0xffffffffffffffffn, "big");
					if (fastRuntime.memory.read(hostBytes, 0n, "uint64", "big") !== 0xffffffffffffffffn ||
							fastRuntime.memory.toArray(hostBytes).some(byte => byte !== 255)) {
						throw new Error("browser host memory lost uint64 bits");
					}
				}
				finally
				{
					hostBytes.dispose();
				}
				const compiledCore = fastRuntime.compileSource(coreSource);
				if (!compiledCore.program) {
					throw new Error(`browser current-core compile failed: ${JSON.stringify(compiledCore.result)}`);
				}
				try {
					for (const backend of ["vm", "mixed"]) {
						const result = compiledCore.program.run({ backend });
						if (!result.success || result.exitCode !== 42) {
							throw new Error(`browser compiled current-core run failed: ${JSON.stringify(result)}`);
						}
					}
				}
				finally
				{
					compiledCore.program.dispose();
				}
				const compiled = fastRuntime.compileSource(
					`int hot(int value) { return value + 1; }
int main() { int value = 0; for (int i = 0; i < 1000; i++) value = hot(value); return 23; }`,
				);
				if (!compiled.program) {
					throw new Error(`browser compile failed: ${JSON.stringify(compiled.result)}`);
				}
				assertMissingBackend(compiled.program.run({ backend: "jit" }));
				const compiledResult = compiled.program.run({ backend: "mixed" });
				compiled.program.dispose();
				if (
					!compiledResult.success || compiledResult.exitCode !== 23 ||
					compiledResult.timings?.executeMs == null ||
					compiledResult.jit?.dispatches !== 0
				) {
					throw new Error(`browser compiled run failed: ${JSON.stringify(compiledResult)}`);
				}
				const threadingResult = fastRuntime.runSource(threadingSource);
				if (
					threadingResult.success ||
					!threadingResult.diagnostics.some(
						(diagnostic) => diagnostic.code === "diag.failed_to_spawn_task_thread",
					)
				) {
					throw new Error(`browser threading contract failed: ${JSON.stringify(threadingResult)}`);
				}
				const compiledThreading = fastRuntime.compileSource(threadingSource);
				if (!compiledThreading.program) {
					throw new Error(
						`browser threading program did not compile: ${JSON.stringify(compiledThreading.result)}`,
					);
				}
				const compiledThreadingResult = compiledThreading.program.run();
				compiledThreading.program.dispose();
				if (
					compiledThreadingResult.success ||
					!compiledThreadingResult.diagnostics.some(
						(diagnostic) => diagnostic.code === "diag.failed_to_spawn_task_thread",
					)
				) {
					throw new Error(
						`compiled browser threading contract failed: ${JSON.stringify(compiledThreadingResult)}`,
					);
				}
				const callableResult = fastRuntime.runSource(callableSource);
				if (callableResult.success || !callableResult.diagnostics.some(
					(diagnostic) => diagnostic.code === "diag.failed_to_spawn_task_thread"
				)) {
					throw new Error(`browser callable task boundary changed: ${JSON.stringify(callableResult)}`);
				}
				const sibling = await sdk.createFastVeroScriptRuntime(fastOptions);
				try {
					const firstBytes = fastRuntime.memory.fromArray(new Uint8Array([11]));
					const secondBytes = sibling.memory.fromArray(new Uint8Array([22]));
					try {
						if (fastRuntime.memory.stats.ownedBytes !== 1n || sibling.memory.stats.ownedBytes !== 1n ||
								fastRuntime.memory.read(firstBytes, 0n, "uint8") !== 11 ||
								sibling.memory.read(secondBytes, 0n, "uint8") !== 22) {
							throw new Error("browser runtimes shared memory after configuration reuse");
						}
					}
					finally
					{
						firstBytes.dispose();
						secondBytes.dispose();
					}
					fastRuntime.dispose();
					const survivor = sibling.runSource("int main() { return 42; }");
					if (!survivor.success || survivor.exitCode !== 42 || Object.keys(fastOptions).length !== 0) {
						throw new Error("browser configuration reuse changed options or runtime ownership");
					}
				}
				finally
				{
					sibling.dispose();
				}
			}
			finally
			{
				fastRuntime.dispose();
			}
			try {
				fastRuntime.checkSource("void main() {}");
			}
			catch
			{
				disposedRuntimeRejected = true;
			}
			if (!disposedRuntimeRejected) {
				throw new Error("disposed browser runtime remained callable");
			}

			const interactiveRuntime = await sdk.createVeroScriptRuntime();
			let eventLoopAdvanced = false;
			try {
				if (!loadedResources().includes("/veroscript-engine.mjs")) {
					throw new Error("interactive runtime did not load its engine on demand");
				}
				if (!interactiveRuntime.supportsInteractiveInput || interactiveRuntime.supportsThreads) {
					throw new Error("interactive runtime advertised incorrect browser capabilities");
				}
				for (const fixture of interactiveFixtures) {
					const compiled = interactiveRuntime.compileSource(fixture.source);
					if (!compiled.program) {
						throw new Error(`browser ${fixture.name} compilation failed: ${JSON.stringify(compiled.result)}`);
					}
					try {
						for (const backend of ["vm", "mixed"]) {
							if (fixture.overflowArgs) {
								let requests = 0;
								const rejected = await compiled.program.runInteractive({
									backend, args: fixture.overflowArgs,
									readLine() { ++requests; return null; },
								});
								if (rejected.success || requests !== 0 ||
										!rejected.diagnostics.some(item => item.code === "diag.call_stack_overflow")) {
									throw new Error(`browser ${fixture.name} did not bound native reentry: ${JSON.stringify(rejected)}`);
								}
							}
							for (const invoke of [
								readLine => interactiveRuntime.runSourceInteractive(fixture.source, { backend, readLine }),
								readLine => compiled.program.runInteractive({ backend, readLine }),
							]) {
								let requests = 0;
								const result = await invoke(async ({ index }) => {
									if (index !== requests || requests >= fixture.lines.length) {
										throw new Error(`browser ${fixture.name} input state was not restored`);
									}
									const line = fixture.lines[requests++];
									return await new Promise(resolveInput => setTimeout(() => resolveInput(line), 0));
								});
								if (!result.success || result.exitCode !== 42 || result.exitRequested ||
										result.output !== fixture.output || result.errorOutput !== "" || requests !== fixture.lines.length) {
									throw new Error(`browser ${fixture.name} state was not restored: ${JSON.stringify(result)}`);
								}
								const recovered = interactiveRuntime.runSource("int main() { return 42; }");
								if (!recovered.success || recovered.exitCode !== 42) {
									throw new Error(`browser ${fixture.name} left stale state: ${JSON.stringify(recovered)}`);
								}
							}
						}
					}
					finally
					{
						compiled.program.dispose();
					}
				}
				assertMissingBackend(await interactiveRuntime.runSourceInteractive(
					'import "io.vs"; void main() { print(std::io::read_line()); }',
					{
						backend: "jit",
						readLine() { throw new Error("JIT rejection must precede input"); },
					},
				));
				let memoryBlocked = false;
				const interactiveResult = await interactiveRuntime.runSourceInteractive(
					'import "io.vs"; int main() { string value = std::io::read_line(); print(value); return len(value); }',
					{
						async readLine() {
							try {
								const unexpected = interactiveRuntime.memory.buffer(1n);
								unexpected.dispose();
							}
							catch
							{
								memoryBlocked = true;
							}
							return await new Promise((resolveInput) => {
								setTimeout(() => {
									eventLoopAdvanced = true;
									resolveInput("browser");
								}, 0);
							});
						},
					},
				);
				if (
					!eventLoopAdvanced ||
					!memoryBlocked ||
					!interactiveResult.success ||
					interactiveResult.output !== "browser\n" ||
					interactiveResult.exitCode !== 7
				) {
					throw new Error(`browser interactive input failed: ${JSON.stringify(interactiveResult)}`);
				}
				const failureSource = 'import "io.vs"; int main() { std::io::read_line(); print("after input"); return 42; }';
				const failureProgram = interactiveRuntime.compileSource(failureSource);
				if (!failureProgram.program) {
					throw new Error("browser input failure fixture did not compile");
				}
				const module = interactiveRuntime.module;
				const originalCall = module.ccall;
				let nativeResult;
				module.ccall = async (...args) => {
					const pointer = await originalCall(...args);
					nativeResult = JSON.parse(module.UTF8ToString(pointer));
					return pointer;
				};
				const providerError = new Error("browser provider failure");
				try {
					const runs = [
						options => interactiveRuntime.runSourceInteractive(failureSource, options),
						options => failureProgram.program.runInteractive(options),
					];
					for (const run of runs) {
						for (const failure of [
							{ async readLine() { await Promise.resolve(); throw providerError; }, matches: error => error === providerError },
							{ readLine() { return "bad\0line"; }, matches: error => error instanceof TypeError },
						]) {
							nativeResult = undefined;
							let rejected = false;
							try {
								await run({ readLine: failure.readLine });
							}
							catch (error)
							{
								rejected = failure.matches(error);
							}
							if (!rejected || !nativeResult || nativeResult.success ||
									nativeResult.output.includes("after input") || module.veroWebInputProvider !== undefined) {
								throw new Error(`browser input failure continued execution: ${JSON.stringify({ rejected, nativeResult })}`);
							}
							const recovered = failureProgram.program.run({ input: "recovered\n" });
							if (!recovered.success || recovered.exitCode !== 42 || recovered.output !== "after input\n") {
								throw new Error(`browser program did not recover after provider abort: ${JSON.stringify(recovered)}`);
							}
						}
						const eof = await run({ readLine() { return null; } });
						if (!eof.success || eof.exitCode !== 42 || eof.output !== "after input\n") {
							throw new Error(`browser EOF contract changed: ${JSON.stringify(eof)}`);
						}
					}
				}
				finally
				{
					module.ccall = originalCall;
					failureProgram.program.dispose();
				}
				const consoleProgram = interactiveRuntime.compileSource(interactiveConsole);
				if (!consoleProgram.program) {
					throw new Error(`browser interactive console did not compile: ${JSON.stringify(consoleProgram.result)}`);
				}
				try {
					const readLine = async () => await new Promise(resolveInput => setTimeout(() => resolveInput("browser"), 0));
					const sourceResult = await interactiveRuntime.runSourceInteractive(interactiveConsole, { readLine });
					const programResult = await consoleProgram.program.runInteractive({ readLine });
					for (const result of [sourceResult, programResult]) {
						if (!result.success || result.exitCode !== 42 || result.output !== "prompt: browser" ||
								result.errorOutput !== "") {
							throw new Error(`browser console state changed across input wait: ${JSON.stringify(result)}`);
						}
					}
				}
				finally
				{
					consoleProgram.program.dispose();
				}
			}
			finally
			{
				interactiveRuntime.dispose();
			}

			const workerModule = `
        import { createFastVeroScriptRuntime } from ${JSON.stringify(`${pageOrigin}/veroscript-webassembly.js`)};
        self.onmessage = async (event) => {
          const runtime = await createFastVeroScriptRuntime();
          try {
            self.postMessage({ kind: "running" });
            self.postMessage({ kind: "result", result: runtime.runSource(event.data.source) });
          } catch (error) {
            self.postMessage({ kind: "error", message: String(error) });
          } finally {
            runtime.dispose();
          }
        };
      `;
			const workerUrl = URL.createObjectURL(new Blob([workerModule], { type: "text/javascript" }));
			try {
				const runWorker = (source) => new Promise((resolveWorker, rejectWorker) => {
					const worker = new Worker(workerUrl, { type: "module" });
					const timer = setTimeout(() => {
						worker.terminate();
						rejectWorker(new Error("Web Worker execution timed out"));
					}, 15000);
					worker.onerror = (event) => {
						clearTimeout(timer);
						worker.terminate();
						rejectWorker(new Error(event.message));
					};
					worker.onmessage = (event) => {
						if (event.data.kind === "result") {
							clearTimeout(timer);
							worker.terminate();
							resolveWorker(event.data.result);
						}
						else if (event.data.kind === "error")
						{
							clearTimeout(timer);
							worker.terminate();
							rejectWorker(new Error(event.data.message));
						}
					};
					worker.postMessage({ source });
				});

				const workerResult = await runWorker('void main() { print("worker"); }');
				if (!workerResult.success || workerResult.output !== "worker\n") {
					throw new Error(`Web Worker execution failed: ${JSON.stringify(workerResult)}`);
				}

				const cancellationWorker = new Worker(workerUrl, { type: "module" });
				let cancellationCompleted = false;
				try {
					await new Promise((resolveStarted, rejectStarted) => {
						const timer = setTimeout(() => {
							rejectStarted(new Error("cancellation worker did not start"));
						}, 15000);
						cancellationWorker.onerror = (event) => {
							cancellationCompleted = true;
							clearTimeout(timer);
							rejectStarted(new Error(event.message));
						};
						cancellationWorker.onmessage = (event) => {
							if (event.data.kind === "running") {
								clearTimeout(timer);
								resolveStarted();
							}
							else if (event.data.kind === "result" || event.data.kind === "error")
							{
								cancellationCompleted = true;
								clearTimeout(timer);
								rejectStarted(new Error("unbounded script completed before Worker termination"));
							}
						};
						cancellationWorker.postMessage({ source: "void main() { while (true) {} }" });
					});
					await new Promise((resolveTimer) => setTimeout(resolveTimer, 50));
					if (cancellationCompleted) {
						throw new Error("unbounded script completed before Worker termination");
					}
				}
				finally
				{
					cancellationWorker.terminate();
				}

				return { eventLoopAdvanced, workerOutput: workerResult.output };
			}
			finally
			{
				URL.revokeObjectURL(workerUrl);
			}
		},
		{
			origin,
			standardSource: standardLibrarySource,
			threadingSource: unsupportedThreadingSource,
			coreSource: currentCoreSource,
			callableSource: unsupportedCallableSource,
			coreFixtures: currentCoreFixtures,
			compileExtensionFailures: compileExtensionFailureFixtures,
			incompatibleReflection: incompatibleReflectionSource,
			incompleteDeclaration: incompleteDeclarationSource,
			consoleFailures: consoleFailureFixtures,
			invalidConsole: invalidConsoleSources,
			interactiveConsole: interactiveConsoleSource,
			interactiveFixtures: interactiveStateFixtures,
		},
	);

	if (!result.eventLoopAdvanced || result.workerOutput !== "worker\n" || browserErrors.length !== 0) {
		throw new Error(
			`unexpected browser smoke result: ${JSON.stringify({ result, browserErrors })}`,
		);
	}
}
finally
{
	if (browser !== undefined) {
		await browser.close();
	}
	await new Promise((resolveClose, rejectClose) => {
		server.close((error) => error ? rejectClose(error) : resolveClose());
	});
}

console.log("VeroScript real-browser and Web Worker smoke test passed");
