// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import { resolve } from "node:path";
import { pathToFileURL } from "node:url";
import {
	compileExtensionFailureFixtures, consoleFailureFixtures, currentCoreFixtures, incompatibleReflectionSource,
	incompleteDeclarationSource, interactiveConsoleSource, interactiveStateFixtures, invalidConsoleSources,
} from "./VeroScriptWebAssemblyCoreFixtures.mjs";

const modulePath = process.argv[2];
const flags = new Set(process.argv.slice(3));
const supportsInteractiveInput = !flags.has("--non-interactive");
if (!modulePath) {
	throw new Error(
		"usage: node VeroScriptWebAssemblySmoke.mjs <veroscript-engine.mjs> " +
			"[--non-interactive]",
	);
}

const { default: createEngine } = await import(pathToFileURL(resolve(modulePath)).href);
const engine = await createEngine();
const abiVersion = engine.UTF8ToString(engine._vero_web_abi_version());
if (abiVersion !== "0.1.1") {
	throw new Error(`unexpected WebAssembly ABI ${abiVersion}`);
}
if (engine._vero_web_supports_threads() !== 0) {
	throw new Error("default WebAssembly artifact unexpectedly advertises pthread support");
}

const handle = engine._vero_web_create();
if (handle === 0) {
	throw new Error("unable to create WebAssembly runtime");
}
let auxiliaryHandle = 0;

function allocString(value) {
	const capacity = engine.lengthBytesUTF8(value) + 1;
	const pointer = engine._malloc(capacity);
	if (pointer === 0) {
		throw new Error("Wasm string allocation failed");
	}
	engine.stringToUTF8(value, pointer, capacity);
	return pointer;
}

function withStrings(values, callback) {
	const pointers = [];
	let table = 0;
	try {
		for (const value of values) {
			pointers.push(allocString(value));
		}
		if (pointers.length !== 0) {
			table = engine._malloc(pointers.length * 4);
			if (table === 0) {
				throw new Error("Wasm string table allocation failed");
			}
			engine.HEAPU32.set(pointers, table >>> 2);
		}
		return callback(table, pointers.length);
	}
	finally
	{
		if (table !== 0) {
			engine._free(table);
		}
		for (const pointer of pointers) {
			engine._free(pointer);
		}
	}
}

async function withStringsAsync(values, callback) {
	const pointers = [];
	let table = 0;
	try {
		for (const value of values) {
			pointers.push(allocString(value));
		}
		if (pointers.length !== 0) {
			table = engine._malloc(pointers.length * 4);
			if (table === 0) {
				throw new Error("Wasm string table allocation failed");
			}
			engine.HEAPU32.set(pointers, table >>> 2);
		}
		return await callback(table, pointers.length);
	}
	finally
	{
		if (table !== 0) {
			engine._free(table);
		}
		for (const pointer of pointers) {
			engine._free(pointer);
		}
	}
}

function parse(pointer) {
	if (pointer === 0) {
		throw new Error("Wasm returned a null result");
	}
	return JSON.parse(engine.UTF8ToString(pointer));
}

function setLanguage(language) {
	return withStrings([language], (table) => {
		const languagePointer = engine.HEAPU32[table >>> 2];
		return engine._vero_web_set_language(handle, languagePointer) === 1;
	});
}

function run(source, { backend = 1, files = [] } = {}) {
	return withStrings([source, "smoke-main.vs", ""], (fixedTable) => {
		const base = fixedTable >>> 2;
		const sourcePointer = engine.HEAPU32[base];
		const labelPointer = engine.HEAPU32[base + 1];
		const inputPointer = engine.HEAPU32[base + 2];
		return withStrings(files.map((file) => file.path), (pathTable, fileCount) =>
			withStrings(files.map((file) => file.source), (sourceTable, sourceCount) => {
				if (fileCount !== sourceCount) {
					throw new Error("virtual file count mismatch");
				}
				return parse(
					engine._vero_web_run_source_with_context(
						handle,
						sourcePointer,
						labelPointer,
						inputPointer,
						0,
						0,
						backend,
						pathTable,
						sourceTable,
						fileCount,
					),
				);
			}),
		);
	});
}

function check(source, { files = [] } = {}) {
	return withStrings([source, "smoke-check.vs"], (table) => {
		const base = table >>> 2;
		const sourcePointer = engine.HEAPU32[base];
		const labelPointer = engine.HEAPU32[base + 1];
		return withStrings(files.map((file) => file.path), (pathTable, fileCount) =>
			withStrings(files.map((file) => file.source), (sourceTable, sourceCount) => {
				if (fileCount !== sourceCount) {
					throw new Error("virtual file count mismatch");
				}
				return parse(engine._vero_web_check_source_with_context(
					handle, sourcePointer, labelPointer, pathTable, sourceTable, fileCount,
				));
			}),
		);
	});
}

async function runInteractive(source, readLine, backend = 1) {
	engine.veroWebInputProvider = async (runtimeHandle) => {
		if (runtimeHandle !== handle) {
			throw new Error("interactive runtime handle mismatch");
		}
		return await readLine();
	};
	try {
		return await withStringsAsync([source, "interactive.vs"], async (fixedTable) => {
			const base = fixedTable >>> 2;
			return parse(await engine.ccall(
				"vero_web_run_source_interactive_with_context",
				"number",
				["number", "number", "number", "number", "number", "number", "number", "number", "number"],
				[handle, engine.HEAPU32[base], engine.HEAPU32[base + 1], 0, 0, backend, 0, 0, 0],
				{ async: true },
			));
		});
	}
	finally
	{
		delete engine.veroWebInputProvider;
	}
}

async function runProgramInteractive(programHandle, readLine, backend = 1, args = []) {
	engine.veroWebInputProvider = async (runtimeHandle) => {
		if (runtimeHandle !== handle) {
			throw new Error("interactive runtime handle mismatch");
		}
		return await readLine();
	};
	try {
		return await withStringsAsync(args, async (table, count) => parse(await engine.ccall(
			"vero_web_run_program_interactive",
			"number",
			["number", "number", "number", "number", "number"],
			[handle, programHandle, table, count, backend],
			{ async: true },
		)));
	}
	finally
	{
		delete engine.veroWebInputProvider;
	}
}

function compile(source, { files = [] } = {}) {
	return withStrings([source, "compiled-smoke.vs"], (table) => {
		const base = table >>> 2;
		return withStrings(files.map((file) => file.path), (paths, count) =>
			withStrings(files.map((file) => file.source), (sources) =>
				parse(
					engine._vero_web_compile_source_with_context(
						handle,
						engine.HEAPU32[base],
						engine.HEAPU32[base + 1],
						paths,
						sources,
						count,
					),
				),
			),
		);
	});
}

function runProgram(programHandle, backend) {
	return withStrings([""], (table) =>
		parse(
			engine._vero_web_run_program(
				handle,
				programHandle,
				engine.HEAPU32[table >>> 2],
				0,
				0,
				backend,
			),
		),
	);
}

const performancePathSource = String.raw`
class Counter
{
    public: int value = 0;
    int add(int amount)
    {
        value += amount;
        return value;
    }
}
class FieldValue
{
    public: int value = 2;
}
int method_work(int count)
{
    Counter counter = Counter();
    int total = 0;
    for (int i = 0; i < count; i++) total += counter.add(1);
    return total;
}
int field_work(int count)
{
    FieldValue value = FieldValue();
    int total = 0;
    for (int i = 0; i < count; i++) total += value.value;
    return total;
}
int array_work(int count)
{
    array<int> values = array<int>();
    values.reserve(count);
    for (int i = 0; i < count; i++) values.push(i);
    int total = 0;
    for (int i = 0; i < count; i++) total += values[i];
    return total;
}
int map_work(int count)
{
    map<int, int> values = map<int, int>();
    values.reserve(count);
    for (int i = 0; i < count; i++) values[i] = i + 1;
    int total = 0;
    for (int i = 0; i < count; i++) total += values[i];
    return total;
}
int set_work(int count)
{
    set<int> values = set<int>();
    for (int i = 0; i < count; i++) values.add(i);
    int total = 0;
    for (int i = 0; i < count; i++) if (values.has(i)) total++;
    return total;
}
int string_map_work()
{
    map<string, int> values = map<string, int>();
    values["a"] = 1;
    values["b"] = 2;
    values["c"] = 3;
    values["d"] = 4;
    array<string> keys = {"a", "b", "c", "d"};
    int total = 0;
    for (int i = 0; i < 64; i++) total += values[keys[i % 4]];
    return total;
}
void main()
{
    print(method_work(256));
    print(field_work(256));
    print(array_work(256));
    print(map_work(256));
    print(set_work(256));
    print(string_map_work());
}
`;
const performancePathOutput = "32896\n512\n32640\n32896\n256\n160\n";
const overflowPathSource = String.raw`
void main()
{
    array<int> values = {1};
    int total = 2147483647;
    for (int i = 0; i < 2; i++) total += values[0];
}
`;
function runPerformancePaths(backend, name) {
	const start = performance.now();
	const result = run(performancePathSource, { backend });
	const elapsedMilliseconds = performance.now() - start;
	if (
		!result.success ||
		result.output !== performancePathOutput ||
		!result.jit ||
		!Number.isSafeInteger(result.jit.directNativeCalls) ||
		result.jit.directNativeCalls < 0
	) {
		throw new Error(`${name} performance-path execution failed: ${JSON.stringify(result)}`);
	}

	for (const [overflowSource, expectedOutput] of [
		[overflowPathSource, ""],
		["int accumulate(int value,int step){value+=step;return value;}int main(){return accumulate(2147483647,1);}", ""],
		["int accumulate(int value,int step){value+=step;return value;}int main(){return accumulate(-2147483647-1,-1);}", ""],
		["int accumulate(int value){value+=value;return value;}int main(){return accumulate(1073741824);}", ""],
		["class State{public:int value=1;int step(int n){value=value+n;return(n+1)*n;}};int main(){State state=State();try{return state.step(46341);}finally{print(state.value);}}", "46342\n"],
	]) {
		const overflow = run(overflowSource, { backend });
		if (
			overflow.success ||
			overflow.output !== expectedOutput ||
			!overflow.diagnostics.some((diagnostic) => diagnostic.code === "diag.integer_overflow")
		) {
			throw new Error(`${name} performance-path overflow diverged: ${JSON.stringify(overflow)}`);
		}
	}
	return elapsedMilliseconds;
}

function executionSignature(result) {
	return JSON.stringify({
		success: result.success,
		exitCode: result.exitCode,
		exitRequested: result.exitRequested,
		output: result.output,
		errorOutput: result.errorOutput,
		diagnostics: result.diagnostics,
	});
}

function expectVmCounters(result) {
	if (!result.jit || Object.values(result.jit).some((value) => value !== 0)) {
		throw new Error(`execution without a backend reported JIT activity: ${JSON.stringify(result)}`);
	}
}

function expectMissingJitBackend(result) {
	if (
		result.success || result.backend !== "jit" || result.output !== "" ||
		!result.diagnostics.some((diagnostic) =>
			diagnostic.code === "diag.jit_execution_requires_an_explicitly_selected_backend_file_directory_or_factory"
		)
	) {
		throw new Error(`missing JIT backend contract changed: ${JSON.stringify(result)}`);
	}
	expectVmCounters(result);
}

function expectMixedVmParity(source, label, verifyVm) {
	const vm = run(source, { backend: 0 });
	verifyVm(vm);
	const expected = executionSignature(vm);
	for (const [backend, name] of [[1, "mixed"]]) {
		const actual = run(source, { backend });
		if (executionSignature(actual) !== expected) {
			throw new Error(
				`${label} diverged for ${name}: ${JSON.stringify({ vm, actual })}`,
			);
		}
		expectVmCounters(actual);
	}
}

function runVmSemanticRegressions() {
	let state = 1;
	let affineHits = 0;
	for (let i = 0; i < 257; i += 1) {
		state = (state * 13 + 7) % 32;
		if (state % 3 === 0) {
			affineHits += 1;
		}
	}

	expectMixedVmParity(
		`struct Payload {
    int sum = 0;
    Payload(int left, int right) { sum = left + right; }
}
class BaseValue {
public:
    int baseValue = 0;
    BaseValue(int input) { baseValue = input; }
}
class ConstHolder : BaseValue {
public:
    const int value;
    const Payload payload;
    ConstHolder(int input) : BaseValue(input + 1), payload(input, 2), value(input) {}
    int total() const { return value + payload.sum + baseValue; }
}
void main() { ConstHolder holder = ConstHolder(5); print(holder.total()); }`,
		"constructor field initializer list",
		(result) => {
			if (!result.success || result.output !== "18\n") {
				throw new Error(`unexpected VM constructor-initializer result: ${JSON.stringify(result)}`);
			}
		},
	);

	expectMixedVmParity(
		`int constructorInitOrder = 0;
int markConstructorInit(int digit) {
    constructorInitOrder = constructorInitOrder * 10 + digit;
    return digit;
}
class InitOrderBase {
public:
    InitOrderBase(int ignored) {}
}
class InitOrderDerived : InitOrderBase {
public:
    int first;
    int second;
    InitOrderDerived()
        : second(markConstructorInit(2)),
          InitOrderBase(markConstructorInit(9)),
          first(markConstructorInit(1)) {}
}
void main() {
    InitOrderDerived value = InitOrderDerived();
    print(constructorInitOrder);
}`,
		"constructor initializer execution order",
		(result) => {
			if (!result.success || result.output !== "912\n") {
				throw new Error(`unexpected VM constructor-initializer order: ${JSON.stringify(result)}`);
			}
		},
	);

	expectMixedVmParity(
		`int invokeClosure() {
    fn(int)->int transform = fn(int value) -> int => value + 1;
    return transform(4);
}
void main() { print(invokeClosure()); }`,
		"capture-free closure ownership classification",
		(result) => {
			if (!result.success || result.output !== "5\n") {
				throw new Error(`unexpected VM capture-free closure result: ${JSON.stringify(result)}`);
			}
		},
	);

	expectMixedVmParity(
		`int invokeCapture(int seed) {
    fn(int)->int transform = fn(int value) -> int => value + seed;
    return transform(4);
}
void main() { print(invokeCapture(3)); }`,
		"capturing closure ownership classification",
		(result) => {
			if (!result.success || result.output !== "7\n") {
				throw new Error(`unexpected VM capturing closure result: ${JSON.stringify(result)}`);
			}
		},
	);

	expectMixedVmParity(
		`int stopFromClosure() { exit(17); return 0; }
int invokeClosureExit() {
    fn()->int callback = fn() -> int => stopFromClosure();
    return 1 + callback();
}
void main() { print(invokeClosureExit()); assert(false); }`,
		"capture-free closure exit propagation",
		(result) => {
			if (
				!result.success ||
				!result.exitRequested ||
				result.exitCode !== 17 ||
				result.output !== ""
			) {
				throw new Error(`unexpected VM closure-exit result: ${JSON.stringify(result)}`);
			}
		},
	);

	expectMixedVmParity(
		`class Item { public: int value = 0; int get() { return value; } }
int retained_work() {
    array<Item> values = array<Item>();
    for (int i = 0; i < 4; i++) {
        Item item = Item(); item.value = i; values.push(item);
    }
    int total = 0;
    for (int i = 0; i < 4; i++) { values[i].value += 1; total += values[i].get(); }
    return total;
}
int affine_work() {
    set<int> values = set<int>();
    for (int i = 0; i < 32; i++) if ((i % 3) == 0) values.add(i);
    int state = 1; int total = 0; int i = 0;
    while (i < 257) {
        state = (state * 13 + 7) % 32;
        if (values.has(state)) total++;
        i++;
    }
    return total;
}
void main() { print(retained_work()); print(affine_work()); }`,
		"retained-object and affine-set success paths",
		(result) => {
			const expectedOutput = `10\n${affineHits}\n`;
			if (!result.success || result.output !== expectedOutput) {
				throw new Error(`unexpected VM reference result: ${JSON.stringify(result)}`);
			}
		},
	);

	expectMixedVmParity(
		`class PeriodicItem {
public:
    int x = 0;
    int y = 0;
    int sum() { return x + y; }
}
int periodic(int count) {
    array<PeriodicItem> values = array<PeriodicItem>();
    int i = 0;
    while (i < count) {
        PeriodicItem item = PeriodicItem();
        item.x = i % 97; item.y = (i % 13) * 3;
        values.push(item); i++;
    }
    int total = 0;
    for (int j = 0; j < count; j++) total += values[j].sum();
    return total;
}
void main() { print(periodic(0)); print(periodic(-3)); print(periodic(8)); }`,
		"retained-object cross-loop scalar replacement",
		(result) => {
			if (!result.success || result.output !== "0\n0\n112\n") {
				throw new Error(`unexpected VM retained-object result: ${JSON.stringify(result)}`);
			}
		},
	);

	expectMixedVmParity(
		`interface IndexedOperation { virtual int step(int value) = 0; }
class IndexedAdd3 : IndexedOperation {
public: int step(int value) { return (value + 3) % 1000000007; }
}
class IndexedAdd5 : IndexedOperation {
public: int step(int value) { return (value + 5) % 1000000007; }
}
class IndexedAdd7 : IndexedOperation {
public: int step(int value) { return (value + 7) % 1000000007; }
}
class IndexedAdd11 : IndexedOperation {
public: int step(int value) { return (value + 11) % 1000000007; }
}
int indexedVirtualParams(IndexedOperation a, IndexedOperation b,
    IndexedOperation c, IndexedOperation d, int count) {
    array<int> order = {1, 1, 2, 0, 2, 2, 3};
    IndexedOperation current = a;
    int state = 9601; int total = 0; int i = 0;
    while (i < count) {
        state = (state * 469 + 1242) % 14581;
        int selector = order[(state + (i % 7)) % 7];
        if (selector == 0) current = a; else if (selector == 1) current = b;
        else if (selector == 2) current = c; else current = d;
        total = current.step(total); i++;
    }
    return total + state;
}
void main() {
    IndexedOperation a = IndexedAdd3(); IndexedOperation b = IndexedAdd5();
    IndexedOperation c = IndexedAdd7(); IndexedOperation d = IndexedAdd11();
    print(indexedVirtualParams(a, b, c, d, 100));
}`,
		"indexed virtual-parameter VM execution",
		(result) => {
			if (!result.success || result.output !== "11878\n") {
				throw new Error(`unexpected VM indexed-virtual result: ${JSON.stringify(result)}`);
			}
		},
	);

	const selectorChoices = [0, 2, 2, 1, 2, 0, 0, 0, 0, 2, 2, 1, 3, 1, 1, 3, 0];
	const selectorDeltas = [3, 5, 1, 2];
	let selectorState = 1;
	let selectorTotal = 0;
	for (let i = 0; i < 10000; i += 1) {
		selectorState = (selectorState * 7 + 13) % selectorChoices.length;
		selectorTotal = (selectorTotal + selectorDeltas[selectorChoices[selectorState]]) % 1000003;
	}
	expectMixedVmParity(
		`interface RandomOp { virtual int step(int value) = 0; }
class RandomAddA : RandomOp { public: int step(int value) { return (value + 3) % 1000003; } }
class RandomAddB : RandomOp { public: int step(int value) { return (value + 5) % 1000003; } }
class RandomAddC : RandomOp { public: int step(int value) { return (value + 1) % 1000003; } }
class RandomAddD : RandomOp { public: int step(int value) { return (value + 2) % 1000003; } }
int random_dispatch() {
    array<int> choices = {0, 2, 2, 1, 2, 0, 0, 0, 0, 2, 2, 1, 3, 1, 1, 3, 0};
    RandomOp a = RandomAddA(); RandomOp b = RandomAddB();
    RandomOp c = RandomAddC(); RandomOp d = RandomAddD(); RandomOp current = a;
    int state = 1; int total = 0; int i = 0;
    while (i < 10000) {
        state = (state * 7 + 13) % 17; int choice = choices[state];
        if (choice == 0) current = a; else if (choice == 1) current = b;
        else if (choice == 2) current = c; else current = d;
        total = current.step(total); i++;
    }
    return total + state;
}
void main() { print(random_dispatch()); }`,
		"virtual-selector VM execution",
		(result) => {
			const expectedOutput = `${selectorTotal + selectorState}\n`;
			if (!result.success || result.output !== expectedOutput) {
				throw new Error(`unexpected VM virtual-selector result: ${JSON.stringify(result)}`);
			}
		},
	);

	expectMixedVmParity(
		`class Item { public: int value = 0; int get() { return value; } }
int work() {
    array<Item> values = array<Item>();
    for (int i = 0; i < 4; i++) { Item item = Item(); item.value = i; values.push(item); }
    int total = 0;
    for (int i = 0; i < 5; i++) { print(i); total += values[i].get(); }
    return total;
}
void main() { print(work()); }`,
		"retained-object bounds failure",
		(result) => {
			if (
				result.success ||
				result.output !== "0\n1\n2\n3\n4\n" ||
				result.diagnostics.length === 0
			) {
				throw new Error(`unexpected VM bounds reference: ${JSON.stringify(result)}`);
			}
		},
	);

	for (const [label, source, prefix] of [
		[
			"reference-capture scalar fallback preserves the original side effect",
			`void main() { int value = 1;
fn()->int read = [&value]() -> int { value += 6; return 2147483647 + value; };
try { print(read()); } finally { print(value); } }`,
			"7\n",
		],
		[
			"affine-set state-add overflow",
			`int work() { set<int> values = {0}; int state = 1073741823;
int total = 0; int i = 0; while (i < 2) { state = (state * 2 + 2) % 7;
if (values.has(state)) total++; i++; } return total; }
void main() { print(74); print(work()); }`,
			"74\n",
		],
		[
			"affine-set accumulator overflow",
			`int work() { set<int> values = {3}; int state = 1;
int total = 2147483647; int i = 0; while (i < 2) { state = (state * 2 + 1) % 5;
if (values.has(state)) total++; i++; } return total; }
void main() { print(77); print(work()); }`,
			"77\n",
		],
	]) {
		expectMixedVmParity(source, label, (result) => {
			if (
				result.success ||
				result.output !== prefix ||
				!result.diagnostics.some((diagnostic) => diagnostic.code === "diag.integer_overflow")
			) {
				throw new Error(`unexpected VM overflow reference: ${JSON.stringify(result)}`);
			}
		});
	}
}

function runLanguageSurfaceRegressions() {
	const languageSurfaceSource = String.raw`
int main()
{
    int once = 0;
    do once++; while (false);

    int first = 1, second = first + 2;
    int left = 0, right = 0, order = 0;
    for (left = 1, right = left + 1; left < 4; left++, right += left)
    {
        order += left + right;
    }

    array<int> binary = {0b1010_0101,};
    const char high = '\xFF';
    map<char, int> scores = {'A': 7, high: 9,};
    set<char> letters = {'A', high,};
    string raw = r"\n";
    string multiline = r"alpha
beta";
    assert(binary[0] == 165);
    assert(len(multiline) == 10);
    return once + second + order + scores['A'] + scores[high]
        + (letters.has(high) ? 1 : 0) + len(raw);
}
`;
	expectMixedVmParity(
		languageSurfaceSource,
		"new language-surface WebAssembly semantics",
		(result) => {
			if (!result.success || result.exitCode !== 42) {
				throw new Error(`unexpected VM language-surface result: ${JSON.stringify(result)}`);
			}
		},
	);

	const boundaryStatementNesting =
		`void main() ${"{".repeat(32)}${"}".repeat(32)}`;
	const boundaryStatements = run(boundaryStatementNesting);
	if (!boundaryStatements.success) {
		throw new Error(
			`documented Web statement-nesting boundary was rejected: ${JSON.stringify(boundaryStatements)}`,
		);
	}

	const excessiveStatementNesting =
		`void main() ${"{".repeat(34)}${"}".repeat(34)}`;
	const nestedStatements = run(excessiveStatementNesting);
	if (
		nestedStatements.success ||
		!nestedStatements.diagnostics.some((diagnostic) =>
			diagnostic.message.includes("statement nested too deeply"))
	) {
		throw new Error(
			`excessive Web statement nesting was not rejected: ${JSON.stringify(nestedStatements)}`,
		);
	}

	const boundaryArrayDimensions =
		`void main() { int value${"[1]".repeat(64)}; }`;
	const boundaryArray = run(boundaryArrayDimensions);
	if (!boundaryArray.success) {
		throw new Error(
			`documented Web native-array dimension boundary was rejected: ${JSON.stringify(boundaryArray)}`,
		);
	}

	const excessiveArrayDimensions =
		`void main() { int value${"[1]".repeat(65)}; }`;
	const nestedArray = run(excessiveArrayDimensions);
	if (
		nestedArray.success ||
		!nestedArray.diagnostics.some((diagnostic) =>
			diagnostic.message.includes("native array dimension count exceeds limit"))
	) {
		throw new Error(
			`excessive Web native-array dimensions were not rejected: ${JSON.stringify(nestedArray)}`,
		);
	}
}

function runAnalysisBoundaryRegressions() {
	const reject = (result, message) => {
		if (result.success || !result.diagnostics.some((diagnostic) =>
			diagnostic.message.includes(message) && diagnostic.file && diagnostic.line > 0
		)) {
			throw new Error(`analysis boundary lost its diagnostic: ${JSON.stringify(result)}`);
		}
	};
	// Exercise the new core limits in Wasm, where stack and size_t differ from desktop hosts.
	for (const operator of ["=", "+="]) {
		reject(check(`void main() { int x = 0; ${`x${operator}`.repeat(10000)}1; }`),
			"expression chain is too long");
	}
	reject(check(`void main() { int x = ${"true ? 1 : ".repeat(10000)}0; }`),
		"expression chain is too long");
	for (const prefix of ["namespace n {", "namespace {"]) {
		reject(check(prefix.repeat(4096) + "}".repeat(4096)), "namespace nested too deeply");
	}
	for (const [type, opening] of [["array<A>", "{"], ["map<int,A>", "{0:"]]) {
		reject(check(`using A = ${type}; void main() { A value = ${opening.repeat(4096)}${"}".repeat(4096)}; }`),
			"initializer nested too deeply");
	}
	const files = Array.from({ length: 128 }, (_, index) => ({
		path: `limits/${index}.vs`,
		source: index === 127 ? "" : `import "${index + 1}.vs";`,
	}));
	const source = 'import "limits/0.vs"; int main() { return 42; }';
	reject(run(source, { files }), "import nesting limit exceeded");
	// Entry plus 127 dependencies exactly fills the inclusive import-depth limit.
	files[126].source = "";
	const recovered = run(source, { files });
	if (!recovered.success || recovered.exitCode !== 42) {
		throw new Error(`runtime did not recover after rejected import graph: ${JSON.stringify(recovered)}`);
	}
}

function runCurrentCoreRegressions() {
	for (const fixture of compileExtensionFailureFixtures) {
		for (const [operation, invoke] of [
			["check", () => check(fixture.source, { files: fixture.files })],
			["compile", () => compile(fixture.source, { files: fixture.files })],
			["run", () => run(fixture.source, { files: fixture.files })],
		]) {
			const result = invoke();
			if (result.success || result.programHandle || result.output !== "" ||
					!result.diagnostics.some(diagnostic => diagnostic.severity === "error" &&
						(!fixture.diagnosticMessagePattern ||
							new RegExp(fixture.diagnosticMessagePattern).test(diagnostic.message)))) {
				throw new Error(`${fixture.name} ${operation} rejection changed: ${JSON.stringify(result)}`);
			}
			const recovered = run('int main() { print("recovered"); return 42; }');
			if (!recovered.success || recovered.exitCode !== 42 || recovered.exitRequested ||
					recovered.output !== "recovered\n" || recovered.errorOutput !== "" ||
					recovered.diagnostics.some(diagnostic => diagnostic.severity === "error")) {
				throw new Error(`${fixture.name} ${operation} left stale state: ${JSON.stringify(recovered)}`);
			}
		}
	}
	for (const fixture of currentCoreFixtures) {
		const checked = check(fixture.source, { files: fixture.files });
		if (!checked.success) {
			throw new Error(`${fixture.name} check failed: ${JSON.stringify(checked)}`);
		}
		for (const backend of [0, 1]) {
			const result = run(fixture.source, { backend, files: fixture.files });
			if (!result.success || result.exitCode !== 42 ||
					result.exitRequested !== (fixture.exitRequested ?? false) ||
					result.output !== (fixture.output ?? "") || result.errorOutput !== (fixture.errorOutput ?? "")) {
				throw new Error(`${fixture.name} source failed: ${JSON.stringify(result)}`);
			}
		}
		const program = compile(fixture.source, { files: fixture.files });
		if (!program.success || !program.programHandle) {
			throw new Error(`${fixture.name} compilation failed: ${JSON.stringify(program)}`);
		}
		try {
			for (const backend of [0, 1, 0]) {
				const result = runProgram(program.programHandle, backend);
				if (!result.success || result.exitCode !== 42 ||
						result.exitRequested !== (fixture.exitRequested ?? false) ||
						result.output !== (fixture.output ?? "") || result.errorOutput !== (fixture.errorOutput ?? "")) {
					throw new Error(`${fixture.name} compiled execution failed: ${JSON.stringify(result)}`);
				}
			}
		}
		finally
		{
			engine._vero_web_destroy_program(handle, program.programHandle);
		}
	}
	const assertConsoleFailure = (result, operation) => {
		if (result.success || result.exitCode === 0 || result.output !== "before" ||
				!result.diagnostics.some(diagnostic => diagnostic.severity === "error" &&
					diagnostic.message.includes(`std::console::${operation}`)) ||
				result.errorOutput.includes("\x1b")) {
			throw new Error(`invalid console argument bypassed validation: ${JSON.stringify(result)}`);
		}
		const recovered = run('int main() { print("recovered"); return 42; }');
		if (!recovered.success || recovered.exitCode !== 42 || recovered.output !== "recovered\n" ||
				recovered.errorOutput !== "") {
			throw new Error(`console failure left stale state: ${JSON.stringify(recovered)}`);
		}
	};
	for (const fixture of consoleFailureFixtures) {
		for (const backend of [0, 1]) {
			assertConsoleFailure(run(fixture.source, { backend }), fixture.operation);
		}
		const program = compile(fixture.source);
		if (!program.success || !program.programHandle) {
			throw new Error(`console value-validation fixture did not compile: ${JSON.stringify(program)}`);
		}
		try {
			for (const backend of [0, 1]) {
				assertConsoleFailure(runProgram(program.programHandle, backend), fixture.operation);
			}
		}
		finally
		{
			engine._vero_web_destroy_program(handle, program.programHandle);
		}
	}
	for (const source of invalidConsoleSources) {
		const result = check(source);
		if (result.success || !result.diagnostics.some(diagnostic => diagnostic.severity === "error")) {
			throw new Error(`invalid console type/arity was accepted: ${JSON.stringify(result)}`);
		}
	}
	const incomplete = check(incompleteDeclarationSource);
	if (incomplete.success || incomplete.diagnostics.length === 0) {
		throw new Error(`incomplete declaration was accepted: ${JSON.stringify(incomplete)}`);
	}
	for (const backend of [0, 1]) {
		const incompatible = run(incompatibleReflectionSource, { backend });
		if (incompatible.success || incompatible.diagnostics.length === 0) {
			throw new Error(`reflection accepted incompatible signedness: ${JSON.stringify(incompatible)}`);
		}
	}
	const reflectionSource = String.raw`
struct Counter
{
    int value;
    int add(int delta)
    {
        value += delta;
        return value;
    }
    static int twice(int input)
    {
        return input * 2;
    }
};
int main()
{
    type<Counter> info = type<Counter>();
    int field_index = -1, add_index = -1, twice_index = -1;
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
            add_index = i;
        }
        if (candidate.name() == "twice")
        {
            twice_index = i;
        }
    }
    assert(field_index >= 0 && add_index >= 0 && twice_index >= 0);
    field_info<Counter> field = field_info<Counter>(info.qualified_name, field_index);
    method_info<Counter> add = method_info<Counter>(info.qualified_name, add_index);
    method_info<Counter> twice = method_info<Counter>(info.qualified_name, twice_index);
    Counter counter = Counter();
    field.set<int>(counter, 10);
    assert(field.get<int>(counter) == 10);
    assert(add.invoke<int>(counter, 5) == 15);
    assert(twice.invoke_static<int>(21) == 42);
    return field.get<int>(counter);
}
`;
	for (const [backend, name] of [[0, "vm"], [1, "mixed"]]) {
		const result = run(reflectionSource, { backend });
		if (!result.success || result.exitCode !== 15) {
			throw new Error(`${name} executable reflection failed: ${JSON.stringify(result)}`);
		}
	}
	const compiled = compile(reflectionSource);
	if (!compiled.success || !compiled.programHandle) {
		throw new Error(`executable reflection did not compile: ${JSON.stringify(compiled)}`);
	}
	try {
		for (const backend of [0, 1, 0]) {
			const result = runProgram(compiled.programHandle, backend);
			if (!result.success || result.exitCode !== 15) {
				throw new Error(`compiled executable reflection failed: ${JSON.stringify(result)}`);
			}
		}
	}
	finally
	{
		engine._vero_web_destroy_program(handle, compiled.programHandle);
	}

	const filesystemSource = String.raw`
import "io.vs";
import "path.vs";
import "string.vs";
int main()
{
    assert(std::filesystem::is_file("/stdlib/io.vs"));
    assert(std::filesystem::is_directory("/stdlib"));
    assert(!std::filesystem::is_file("/stdlib"));
    assert(!std::filesystem::is_directory("/stdlib/io.vs"));
    string folder = "/tmp/veroscript-webassembly-empty-directory";
    std::filesystem::mkdir(folder);
    assert(std::filesystem::remove_dir(folder));
    assert(!std::filesystem::is_directory(folder));
    assert(std::filesystem::join_path("left", "/right") == "/right");
    assert(std::strings::trim("\v x \v") == "x");
    return 0;
}
`;
	for (const [backend, name] of [[0, "vm"], [1, "mixed"]]) {
		const result = run(filesystemSource, { backend });
		if (!result.success || result.exitCode !== 0) {
			throw new Error(`${name} embedded filesystem helpers failed: ${JSON.stringify(result)}`);
		}
	}

	const callableSource = String.raw`
import "concurrent.vs";
int double_value(int value)
{
    return value * 2;
}
void main()
{
    assert(!std::concurrent::cancel_requested());
    task<int> pending = std::concurrent::spawn_task<int>(double_value, 21);
    print(pending.await());
}
`;
	const callable = run(callableSource);
	const rejectedCallable = (result) => !result.success && result.diagnostics.some(
		(diagnostic) => diagnostic.code === "diag.failed_to_spawn_task_thread"
	);
	if (!rejectedCallable(callable)) {
		throw new Error(`non-pthread callable task contract changed: ${JSON.stringify(callable)}`);
	}
	const compiledCallable = compile(callableSource);
	if (!compiledCallable.success || !compiledCallable.programHandle) {
		throw new Error(`callable task did not compile: ${JSON.stringify(compiledCallable)}`);
	}
	try {
		const compiledResult = runProgram(compiledCallable.programHandle, 1);
		if (!rejectedCallable(compiledResult)) {
			throw new Error(`compiled non-pthread callable task contract changed: ${JSON.stringify(compiledResult)}`);
		}
	}
	finally
	{
		engine._vero_web_destroy_program(handle, compiledCallable.programHandle);
	}
	if (!setLanguage("zh-CN")) {
		throw new Error("unable to select Chinese diagnostics");
	}
	try {
		const localized = run(callableSource);
		if (localized.success || !localized.diagnostics.some(
			(diagnostic) => diagnostic.code === "diag.failed_to_spawn_task_thread" &&
				diagnostic.message === "无法创建任务线程"
		)) {
			throw new Error(`callable task failure was not localized: ${JSON.stringify(localized)}`);
		}
	}
	finally
	{
		if (!setLanguage("en-US")) {
			throw new Error("unable to restore English diagnostics");
		}
	}
}

try {
	for (const name of [
		"_vero_web_compile_source_with_context",
		"_vero_web_run_program",
		"_vero_web_destroy_program",
	]) {
		if (typeof engine[name] !== "function") {
			throw new Error(`missing compiled-program export ${name}`);
		}
	}
	const compiled = compile(`int hot(int value) { return value + 1; }
int main() { int value = 0; for (int i = 0; i < 1000; i++) value = hot(value); return 17; }`);
	const compiledRun = runProgram(compiled.programHandle, 1);
	if (
		!compiled.success ||
		compiled.timings?.compileMs == null ||
		!compiledRun.success ||
		compiledRun.exitCode !== 17 ||
		compiledRun.timings?.executeMs == null ||
		compiledRun.jit?.dispatches !== 0
	) {
		throw new Error(`compiled-program execution failed: ${JSON.stringify({ compiled, compiledRun })}`);
	}
	if (engine._vero_web_destroy_program(handle, compiled.programHandle) !== 1) {
		throw new Error("compiled program was not released");
	}

	// Explicit JIT must fail before script initialization or input consumption.
	const missingBackendSource = `int starts = 0; print("must not execute"); int main() { return 99 + starts++; }`;
	expectMissingJitBackend(run(missingBackendSource, { backend: 2 }));
	const missingBackendProgram = compile(missingBackendSource);
	if (!missingBackendProgram.success || !missingBackendProgram.programHandle) {
		throw new Error(`missing-backend fixture did not compile: ${JSON.stringify(missingBackendProgram)}`);
	}
	try {
		expectMissingJitBackend(runProgram(missingBackendProgram.programHandle, 2));
		const recovered = runProgram(missingBackendProgram.programHandle, 1);
		if (!recovered.success || recovered.exitCode !== 99 || recovered.output !== "must not execute\n") {
			throw new Error(`compiled program did not recover after JIT rejection: ${JSON.stringify(recovered)}`);
		}
		expectVmCounters(recovered);
		const repeated = runProgram(missingBackendProgram.programHandle, 0);
		if (!repeated.success || repeated.exitCode !== 99 || repeated.output !== "must not execute\n") {
			throw new Error(`compiled program retained script state: ${JSON.stringify(repeated)}`);
		}
	}
	finally
	{
		engine._vero_web_destroy_program(handle, missingBackendProgram.programHandle);
	}

	runVmSemanticRegressions();
	runLanguageSurfaceRegressions();
	runAnalysisBoundaryRegressions();
	runCurrentCoreRegressions();

	const performanceTimings = new Map();
	for (const [backend, name] of [[0, "vm"], [1, "mixed"]]) {
		const result = run(`int main()
{
    return 7;
}`, { backend });
		if (!result.success || result.exitCode !== 7 || result.backend !== name) {
			throw new Error(`${name} Web execution failed: ${JSON.stringify(result)}`);
		}

		const recursion = run(`
int recurse(int depth)
{
    if (depth == 0) return 0;
    return 1 + recurse(depth - 1);
}
int main()
{
    return recurse(2048);
}
`, { backend });
		if (!recursion.success || recursion.exitCode !== 2048) {
			throw new Error(
				`${name} bounded recursion did not fall back safely: ${JSON.stringify(recursion)}`,
			);
		}

		const numeric = run(`
int width(float value)
{
    return 32;
}
int width(double value)
{
    return 64;
}
int main()
{
    int8 narrow = -128;
    uint16 wide = 65535u;
    assert(.5f < 1.f, "Wasm leading-dot float assertion");
    print(narrow);
    print(wide);
    print(std::to_float(1.25));
    return width(.5f) + width(.5);
}
`, { backend });
		if (
			!numeric.success ||
			numeric.exitCode !== 96 ||
			numeric.output !== "-128\n65535\n1.25\n"
		) {
			throw new Error(`${name} numeric-width execution failed: ${JSON.stringify(numeric)}`);
		}

		const modernLanguage = run(`
int DiscardCalls = 0;
int touch()
{
    DiscardCalls += 1;
    return 99;
}
struct Token
{
    mutable int value = 0;
    explicit Token(int input)
    {
        value = input;
    }
    int bump() const
    {
        value += 1;
        return value;
    }
}
int main()
{
    (void)touch();
    const Token token(38);
    readonly<int> published = token.bump();
    double exact = (double)published.get();
    optional<int> restored = try_cast<int>(exact);
    if (!restored.has_value() || DiscardCalls != 1) return -1;
    Token converted = (Token)restored.get();
    const array<int> locked = {1};
    const_cast<array<int>>(locked).push(2);
    return converted.bump() + len(locked);
}
`, { backend });
		if (!modernLanguage.success || modernLanguage.exitCode !== 42) {
			throw new Error(
				`${name} modern-language execution failed: ${JSON.stringify(modernLanguage)}`,
			);
		}

		// Wide scalar dynamic dispatch must preserve VM semantics in both policies.
		const dynamicScalars = run(`
interface IWideScalar
{
    virtual int64 add_i64(int64 left, int64 right) = 0;
    virtual uint64 add_u64(uint64 left, uint64 right) = 0;
    virtual float add_f32(float left, float right) = 0;
}
class WideScalar : IWideScalar
{
    int64 add_i64(int64 left, int64 right)
    {
        return left + right;
    }
    uint64 add_u64(uint64 left, uint64 right)
    {
        return left + right;
    }
    float add_f32(float left, float right)
    {
        return left + right;
    }
}
int main()
{
    IWideScalar value = WideScalar();
    if (value.add_i64(19i64, 23i64) != 42i64) return -1;
    if (value.add_u64(20u64, 22u64) != 42u64) return -2;
    if (value.add_f32(19.5f, 22.5f) != 42.f) return -3;
    return 42;
}
`, { backend });
		if (!dynamicScalars.success || dynamicScalars.exitCode !== 42) {
			throw new Error(
				`${name} dynamic scalar execution failed: ${JSON.stringify(dynamicScalars)}`,
			);
		}

		const exception = run(`
int main()
{
    int result = 0;
    try
    {
        throw 7;
    }
    catch (const int& value)
    {
        result = value;
    }
    finally
    {
        result += 2;
    }
    return result;
}
`, { backend });
		if (!exception.success || exception.exitCode !== 9 || exception.backend !== name) {
			throw new Error(
				`${name} exception execution failed: ${JSON.stringify(exception)}`,
			);
		}

		const nestedFinally = run(`
void deep(int n) { if (n > 0) { deep(n - 1); } }
class Trace {
    public: ~Trace() {
        deep(100);
        try { throw 66; } catch (int value) { if (value != 66) { print(0); } }
        print(77);
    }
};
Trace replaced() { try { return Trace(); } finally { throw 9; } }
class Bad { public: ~Bad() { throw 66; } };
Bad failed_cleanup() { try { return Bad(); } finally { throw 10; } }
int preserved() {
    try { return 5; } finally {
        if (false) { print(0); }
        try { try { throw 17; } finally { print(18); } }
        catch (int value) { print(value); }
    }
}
void main() {
    try { replaced(); } catch (int value) { print(value); }
    print(preserved());
    try { failed_cleanup(); } catch (int value) { print(value); }
}
`, { backend });
		if (!nestedFinally.success || nestedFinally.output !== "77\n9\n18\n17\n5\n66\n") {
			throw new Error(
				`${name} nested finally/reentrant cleanup failed: ${JSON.stringify(nestedFinally)}`,
			);
		}

		const uncaught = run(`void main()
{
    throw "uncaught";
}`, { backend });
		if (uncaught.success || uncaught.backend !== name || uncaught.errorOutput.length === 0) {
			throw new Error(
				`${name} uncaught exception escaped Web containment: ${JSON.stringify(uncaught)}`,
			);
		}

		const refReplacement = run(`void deep(int n) { if (n > 0) { deep(n - 1); } }
class Trace { public: ~Trace() { deep(100); print(78); } };
void replace(ref Trace value) { value = Trace(); }
void main() { Trace value(); replace(ref value); print(79); }`, { backend });
		if (!refReplacement.success || refReplacement.output !== "78\n79\n78\n") {
			throw new Error(
				`${name} reentrant ref replacement failed: ${JSON.stringify(refReplacement)}`,
			);
		}

		const uncaughtObject = run(`void deep(int n) { if (n > 0) { deep(n - 1); } }
class Old { public: ~Old() {
    deep(100); try { throw 77; } catch (int) {} print(78);
} };
void main() { throw Old(); }`, { backend });
		if (uncaughtObject.success || uncaughtObject.output !== "78\n" ||
				uncaughtObject.errorOutput.length === 0) {
			throw new Error(
				`${name} uncaught object cleanup failed: ${JSON.stringify(uncaughtObject)}`,
			);
		}

		const globalCleanup = run(`class Old { public: ~Old() { print(78); } };
class Bad { public: ~Bad() { throw Old(); } };
Bad global(); void main() { print(1); }`, { backend });
		if (globalCleanup.success || globalCleanup.output !== "1\n78\n" ||
				globalCleanup.errorOutput.length === 0) {
			throw new Error(
				`${name} global cleanup exception release failed: ${JSON.stringify(globalCleanup)}`,
			);
		}

		const failedAssert = run(`void main()
{
    assert(false, "wasm assertion detail");
}`, {
			backend,
		});
		if (
			failedAssert.success ||
			failedAssert.backend !== name ||
			!failedAssert.errorOutput.includes("wasm assertion detail")
		) {
			throw new Error(
				`${name} assertion message was not preserved: ${JSON.stringify(failedAssert)}`,
			);
		}

		performanceTimings.set(name, runPerformancePaths(backend, name));
	}

	console.log(
		`VM execution paths: vm=${performanceTimings.get("vm").toFixed(1)}ms ` +
			`mixed=${performanceTimings.get("mixed").toFixed(1)}ms`,
	);

	const compiledPerformance = compile(performancePathSource);
	if (!compiledPerformance.success || !compiledPerformance.programHandle) {
		throw new Error(`performance program compile failed: ${JSON.stringify(compiledPerformance)}`);
	}
	const compiledTimings = new Map();
	for (const [backend, name] of [[0, "vm"], [1, "mixed"]]) {
		const start = performance.now();
		const result = runProgram(compiledPerformance.programHandle, backend);
		compiledTimings.set(name, performance.now() - start);
		if (
			!result.success ||
			result.output !== performancePathOutput ||
			!result.jit ||
			result.jit.dispatches !== 0
		) {
			throw new Error(`${name} compiled performance path failed: ${JSON.stringify(result)}`);
		}
	}
	engine._vero_web_destroy_program(handle, compiledPerformance.programHandle);
	console.log(
		`compiled execution paths: vm=${compiledTimings.get("vm").toFixed(1)}ms ` +
			`mixed=${compiledTimings.get("mixed").toFixed(1)}ms ` +
			`(compile=${compiledPerformance.timings.compileMs.toFixed(1)}ms)`,
	);

	const stdlib = run(
		`import "string.vs";
int main()
{
    return len(std::strings::trim("  x  "));
}`,
	);
	if (!stdlib.success || stdlib.exitCode !== 1) {
		throw new Error(`embedded stdlib import failed: ${JSON.stringify(stdlib)}`);
	}

	const expandedStandardLibrarySource = String.raw`
import "core.vs";
int main()
{
    array<int> values = {9, 2, 7, 2, 5};
    std::algorithm::sort(values);
    print(std::algorithm::lower_bound(values, 5));
    print(std::algorithm::contains(values, 7));
    print(std::numeric::sum(values));

    array<int> shuffled_a = std::algorithm::copy(values);
    array<int> shuffled_b = std::algorithm::copy(values);
    array<unsigned int> first = std::random::make_engine(2026u);
    array<unsigned int> second = std::random::make_engine(2026u);
    std::algorithm::shuffle(shuffled_a, first);
    std::algorithm::shuffle(shuffled_b, second);
    print(std::algorithm::equal(shuffled_a, shuffled_b));

    int sample_a = std::random::next_int(first, -10, 11);
    int sample_b = std::random::next_int(second, -10, 11);
    print(sample_a == sample_b && sample_a >= -10 && sample_a < 11);

    channel<int> mailbox = std::concurrent::make_channel<int>(1);
    assert(std::concurrent::try_send(mailbox, 42));
    print(std::concurrent::recv(mailbox));
    return 0;
}
`;
	const expandedStandardLibraryOutput = "2\ntrue\n25\ntrue\ntrue\n42\n";
	for (const [backend, name] of [[0, "vm"], [1, "mixed"]]) {
		const expandedStandardLibrary = run(expandedStandardLibrarySource, { backend });
		if (
			!expandedStandardLibrary.success ||
			expandedStandardLibrary.output !== expandedStandardLibraryOutput
		) {
			throw new Error(
				`${name} expanded standard library failed: ${JSON.stringify(expandedStandardLibrary)}`,
			);
		}
	}

	const unsupportedThreading = run(String.raw`
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
    print(output.recv());
}
`);
	if (
		unsupportedThreading.success ||
		!unsupportedThreading.diagnostics.some(
			(diagnostic) => diagnostic.code === "diag.failed_to_spawn_task_thread",
		)
	) {
		throw new Error(
			`non-pthread Web concurrency contract changed: ${JSON.stringify(unsupportedThreading)}`,
		);
	}

	const uniformCall = run(`
struct Counter
{
    int value = 1;
};
namespace Extensions
{
    int add(ref Counter self, int amount)
    {
        self.value += amount;
        return self.value;
    }
}
using namespace Extensions;
int main()
{
    Counter counter();
    return counter.add(4);
}
  `);
	if (!uniformCall.success || uniformCall.exitCode !== 5) {
		throw new Error(`uniform-call execution failed: ${JSON.stringify(uniformCall)}`);
	}

	const overlay = run(
		`import "helper.vs";
int main()
{
    return helper_value();
}`,
		{ files: [{ path: "helper.vs", source: `export int helper_value()
{
    return 17;
}` }] },
	);
	if (!overlay.success || overlay.exitCode !== 17) {
		throw new Error(`virtual file import failed: ${JSON.stringify(overlay)}`);
	}

	if (!setLanguage("zh_Hans.UTF-8")) {
		throw new Error("Simplified Chinese selection failed");
	}
	if (!engine.UTF8ToString(engine._vero_web_get_language(handle)).startsWith("zh-")) {
		throw new Error("selected Web language was not canonicalized");
	}
	const languages = parse(engine._vero_web_get_available_languages(handle));
	const expectedLanguages = ["en-US", "fr-FR", "ja-JP", "ko-KR", "zh-CN", "zh-TW"];
	if (!expectedLanguages.every((language) => languages.includes(language))) {
		throw new Error(`unexpected Web languages: ${JSON.stringify(languages)}`);
	}
	if (!setLanguage("zh-CN")) {
		throw new Error("exact zh-CN language selection failed");
	}
	const localized = check('import "missing.vs";');
	if (
		localized.success ||
		localized.diagnostics.length === 0 ||
		!localized.diagnostics[0].message.includes("无法解析")
	) {
		throw new Error(`embedded localization failed: ${JSON.stringify(localized)}`);
	}
	if (!setLanguage("en-US")) {
		throw new Error("en-US language restore failed");
	}

	if (supportsInteractiveInput) {
		for (const fixture of interactiveStateFixtures) {
			const program = compile(fixture.source);
			if (!program.success || !program.programHandle) {
				throw new Error(`${fixture.name} compilation failed: ${JSON.stringify(program)}`);
			}
			try {
				for (const backend of [0, 1]) {
					if (fixture.overflowArgs) {
						let requests = 0;
						const rejected = await runProgramInteractive(program.programHandle,
							() => { ++requests; return null; }, backend, fixture.overflowArgs);
						if (rejected.success || requests !== 0 ||
								!rejected.diagnostics.some(item => item.code === "diag.call_stack_overflow")) {
							throw new Error(`${fixture.name} did not bound native reentry: ${JSON.stringify(rejected)}`);
						}
					}
					for (const invoke of [
						readLine => runInteractive(fixture.source, readLine, backend),
						readLine => runProgramInteractive(program.programHandle, readLine, backend),
					]) {
						let requests = 0;
						const result = await invoke(async () => {
							if (requests >= fixture.lines.length) {
								throw new Error(`${fixture.name} requested excess input`);
							}
							const line = fixture.lines[requests++];
							return await new Promise(resolveInput => setTimeout(() => resolveInput(line), 0));
						});
						if (!result.success || result.exitCode !== 42 || result.exitRequested ||
								result.output !== fixture.output || result.errorOutput !== "" || requests !== fixture.lines.length) {
							throw new Error(`${fixture.name} state was not restored: ${JSON.stringify(result)}`);
						}
						const recovered = run("int main() { return 42; }");
						if (!recovered.success || recovered.exitCode !== 42) {
							throw new Error(`${fixture.name} left stale state: ${JSON.stringify(recovered)}`);
						}
					}
				}
			}
			finally
			{
				engine._vero_web_destroy_program(handle, program.programHandle);
			}
		}
		const consoleProgram = compile(interactiveConsoleSource);
		if (!consoleProgram.success || !consoleProgram.programHandle) {
			throw new Error(`interactive console fixture did not compile: ${JSON.stringify(consoleProgram)}`);
		}
		try {
			const readLine = async () => await new Promise(resolveInput => setTimeout(() => resolveInput("node"), 0));
			const sourceResult = await runInteractive(interactiveConsoleSource, readLine);
			const programResult = await runProgramInteractive(consoleProgram.programHandle, readLine);
			for (const result of [sourceResult, programResult]) {
				if (!result.success || result.exitCode !== 42 || result.output !== "prompt: node" ||
						result.errorOutput !== "") {
					throw new Error(`console state changed across input wait: ${JSON.stringify(result)}`);
				}
			}
		}
		finally
		{
			engine._vero_web_destroy_program(handle, consoleProgram.programHandle);
		}
		auxiliaryHandle = engine._vero_web_create();
		if (auxiliaryHandle === 0) {
			throw new Error("unable to create auxiliary WebAssembly runtime");
		}
		// A frontend may submit a CR-terminated logical line. The bridge must
		// normalize it instead of joining it with the next provider response.
		const submittedLines = ["browser line\r", "37"];
		let inputRequests = 0;
		let rawReentryChecks = 0;
		const interactive = await runInteractive(
			`import "io.vs";
int main()
{
    print(std::io::read_line());
    return std::io::read_int();
}`,
			async () => {
				await Promise.resolve();
				if (inputRequests === 0) {
					if (engine._vero_web_create() !== 0) {
						throw new Error("raw runtime creation was not blocked during interactive execution");
					}
					engine._vero_web_destroy(handle);
					if (
						engine._vero_web_get_language(handle) !== 0 ||
						engine._vero_web_get_available_languages(handle) !== 0
					) {
						throw new Error("raw text getters were not blocked during interactive execution");
					}
					const busy = parse(engine._vero_web_run_source(handle, 0, 0, 0));
					if (busy.success || !busy.errorOutput.includes("awaiting interactive input")) {
						throw new Error(`raw execution reentry was not blocked: ${JSON.stringify(busy)}`);
					}
					engine._vero_web_destroy(auxiliaryHandle);
					if (engine._vero_web_get_language(auxiliaryHandle) !== 0) {
						throw new Error("module-wide raw getters were not blocked during interactive execution");
					}
					const moduleBusy = parse(engine._vero_web_run_source(auxiliaryHandle, 0, 0, 0));
					if (moduleBusy.success || !moduleBusy.errorOutput.includes("awaiting interactive input")) {
						throw new Error(`module-wide raw reentry was not blocked: ${JSON.stringify(moduleBusy)}`);
					}
					rawReentryChecks += 1;
				}
				inputRequests += 1;
				return submittedLines.shift() ?? null;
			},
		);
		if (
			!interactive.success ||
			interactive.output !== "browser line\n" ||
			interactive.exitCode !== 37 ||
			inputRequests !== 2 ||
			rawReentryChecks !== 1
		) {
			throw new Error(`interactive Web input failed: ${JSON.stringify(interactive)}`);
		}
		if (engine._vero_web_get_language(auxiliaryHandle) === 0) {
			throw new Error("auxiliary runtime was destroyed while another runtime was suspended");
		}

		const invalidInput = await runInteractive(
			`import "io.vs";
void main()
{
    print(std::io::read_line());
}`,
			async () => 42,
		);
		if (!invalidInput.success || invalidInput.output !== "\n") {
			throw new Error(`invalid low-level Web input was not handled as EOF: ${JSON.stringify(invalidInput)}`);
		}

		const rejectedInput = await runInteractive(
			`import "io.vs";
void main()
{
    print(std::io::read_line());
}`,
			async () => {
				throw new Error("raw provider rejected");
			},
		);
		if (
			!rejectedInput.success ||
			rejectedInput.output !== "\n" ||
			engine._vero_web_get_language(handle) === 0
		) {
			throw new Error(
				`rejected low-level provider did not resume and release the runtime: ${JSON.stringify(rejectedInput)}`,
			);
		}

		const proxyProvider = new Proxy(async () => {
			throw new Error("raw proxy provider rejected");
		}, {
			get() { throw new Error("raw provider property inspection failed"); },
		});
		const proxyInput = await runInteractive(
			'import "io.vs"; int main() { print(std::io::read_line()); print(std::io::read_line()); return 42; }',
			() => {
				engine.veroWebInputProvider = proxyProvider;
				return "first";
			},
		);
		if (!proxyInput.success || proxyInput.exitCode !== 42 || proxyInput.output !== "first\n\n" ||
				engine._vero_web_get_language(handle) === 0) {
			throw new Error(`raw proxy provider did not preserve EOF cleanup: ${JSON.stringify(proxyInput)}`);
		}

		const compiledInteractive = compile(`import "io.vs";
int main()
{
    print(std::io::read_line());
    return 23;
}`);
		if (!compiledInteractive.success || !compiledInteractive.programHandle) {
			throw new Error(`interactive program did not compile: ${JSON.stringify(compiledInteractive)}`);
		}
		let compiledReentryChecked = false;
		const compiledInteractiveResult = await runProgramInteractive(
			compiledInteractive.programHandle,
			async () => {
				engine._vero_web_destroy(handle);
				if (engine._vero_web_destroy_program(handle, compiledInteractive.programHandle) !== 0) {
					throw new Error("active compiled program was released during interactive execution");
				}
				const busy = parse(engine._vero_web_run_program(
					handle,
					compiledInteractive.programHandle,
					0,
					0,
					0,
					1,
				));
				if (busy.success || !busy.errorOutput.includes("awaiting interactive input")) {
					throw new Error(`compiled-program reentry was not blocked: ${JSON.stringify(busy)}`);
				}
				compiledReentryChecked = true;
				return "compiled guard";
			},
		);
		if (
			!compiledInteractiveResult.success ||
			compiledInteractiveResult.output !== "compiled guard\n" ||
			compiledInteractiveResult.exitCode !== 23 ||
			!compiledReentryChecked ||
			engine._vero_web_destroy_program(handle, compiledInteractive.programHandle) !== 1
		) {
			throw new Error(
				`interactive compiled-program guard failed: ${JSON.stringify(compiledInteractiveResult)}`,
			);
		}
	}
}
finally
{
	if (auxiliaryHandle !== 0) {
		engine._vero_web_destroy(auxiliaryHandle);
	}
	engine._vero_web_destroy(handle);
}

console.log("VeroScript WebAssembly smoke test passed");
