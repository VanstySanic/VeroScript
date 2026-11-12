# 11: Debugging and AOT generation

This tutorial covers two delivery-time tasks: use the debugger to inspect a script, and choose an AOT target for a standalone or native build.

## Step 1: prepare an observable script

```VeroScript
int calculate(int value)
{
	int result = value * 2;
	return result + 1;
}

int main()
{
	int input = 20;
	int output = calculate(input);
	print(output);
	return 0;
}
```

Save it as `debug_demo.vs`, then check it normally:

```powershell
veroscript --check debug_demo.vs
```

## Step 2: start a DAP debug session

The standard-input/output mode is suitable for an editor or another debug client to launch and manage:

```powershell
veroscript --debug debug_demo.vs
```

For a client that connects over TCP:

```powershell
veroscript --debug --dap-port=4711 --dap-bind=127.0.0.1 debug_demo.vs
```

Configure breakpoints, stepping, and variable inspection in the client. Debugging uses the VM debug path, so do not use debug-time behavior as a performance measurement for ordinary execution.

Set source breakpoints on active code. In a DAP client, send them with
`setBreakpoints`. A breakpoint set before launch is pending; after the program
loads, the client receives a `breakpoint` event with `reason: "changed"` and the
same breakpoint ID. Always use the returned verified state and line. A line
removed by `#if`, outside the source, or without an executable location remains
unverified.

## Step 3: generate AOT source

When a script does not need dynamic loading, choose the generated source target that matches the deployment host. C++17 is the default:

```powershell
veroscript aot debug_demo.vs -o generated.cpp
```

`generated.cpp` is not a linked executable. The host project still needs a compatible C++17 compiler and the VeroScript runtime support. Inspect the generated file and build it in the target project rather than trying to execute it directly.

For a standalone Python program, select Python explicitly. The generated program
requires CPython 3.11–3.15 and does not load the VeroScript VM:

```powershell
veroscript aot --target=python debug_demo.vs -o generated.py
python generated.py
```

For a generated C# project or NativeAOT deployment, select C# and emit a bundle:

```powershell
veroscript aot --target=csharp debug_demo.vs --output-dir generated-csharp --bundle-name Demo
dotnet publish generated-csharp/Demo.csproj -c Release
```

Read [Python AOT](../../../../../Extensions/Language/Python/Resources/Docs/User/en-US/aot.md) and [C# AOT](../../../../../Extensions/Language/CSharp/Resources/Docs/User/en-US/aot.md)
for supported subsets and deliberate exclusions. AOT conversion fails with a
diagnostic when the selected target cannot preserve a construct's semantics.

## Choosing a mode

- Fast iteration, hot reload, and interactive debugging: VM or the default mixed backend.
- Breakpoints and variable inspection: `--debug`.
- Integration into a native build: use C++ AOT and verify host bindings and concurrency behavior in the target project.
- Standalone Python source: use `--target=python` and test with the target Python version.
- Generated .NET source or NativeAOT: use `--target=csharp` and build the generated bundle.

## Exercises

1. Set a breakpoint inside `calculate` and inspect `input` and `result`.
2. Change the input and compare normal and debug output.
3. Generate C++ AOT source for an array-using script and confirm that the target project can compile it.
4. Generate Python or C# output for the same script and compare each target's supported-boundary diagnostics.

See [Debugging](../../../../../VeroScriptTooling/Resources/Docs/User/en-US/debugging.md), [C++ AOT](../../../../../Extensions/Language/Cpp/Resources/Docs/User/en-US/aot.md), [Python AOT](../../../../../Extensions/Language/Python/Resources/Docs/User/en-US/aot.md), and [C# AOT](../../../../../Extensions/Language/CSharp/Resources/Docs/User/en-US/aot.md). Next: [Host bindings and script integration](../advanced/12-host-bindings.md).

<!-- docs-navigation -->

---

[Tutorials](../index.md)

Language: [en-US](11-debugging-and-aot.md) · [fr-FR](../../fr-FR/toolchain/11-debugging-and-aot.md) · [ja-JP](../../ja-JP/toolchain/11-debugging-and-aot.md) · [ko-KR](../../ko-KR/toolchain/11-debugging-and-aot.md) · [zh-CN](../../zh-CN/toolchain/11-debugging-and-aot.md) · [zh-TW](../../zh-TW/toolchain/11-debugging-and-aot.md)
