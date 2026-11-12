# 02: Checking, running, and reading arguments

As soon as a script accepts different input, hard-coding data in the source becomes inconvenient. This tutorial reads command-line arguments and uses `--check` to validate the program before running it.

## Step 1: accept arguments

Create `argv_echo.vs`:

```VeroScript
int main(string[] args)
{
	print("argc=" + to_string(len(args)));
	for (int i = 0; i < len(args); i = i + 1)
	{
		print(args[i]);
	}
	return 0;
}
```

`main(string[] args)` receives the arguments after the script path. The script filename is not included in `args`, so `len(args)` is the number of values passed to the script.

## Step 2: pass arguments

```powershell
veroscript --check argv_echo.vs
veroscript argv_echo.vs red blue
```

The output is similar to:

```text
argc=2
red
blue
```

Arguments arrive as strings. Numeric conversions such as `to_int` accept numeric or Boolean values; they do not parse strings. If an argument must contain a number, define and validate its text format before calculating with it. See [Conversion functions](../../../../../VeroScript/Resources/Docs/User/en-US/standard-library/builtins.md#conversions).

## Step 3: build a greeting tool

Save the following program as `greet.vs` in the same directory as `argv_echo.vs`:

```VeroScript
int main(string[] args)
{
	string name = "world";
	if (len(args) > 0)
	{
		name = args[0];
	}

	print("hello, " + name);
	return 0;
}
```

Check the program, then run it without and with an argument:

```powershell
veroscript --check greet.vs
veroscript greet.vs
veroscript greet.vs VeroScript
```

The first run uses the default; the second uses the first argument instead.

## Step 4: choose an execution backend

Normal execution uses the mixed backend by default. You can select a backend explicitly when comparing execution modes:

```powershell
veroscript --vm-backend=vs-bytecode --backend=vm argv_echo.vs one two
veroscript --jit-backend="/path/to/Jit" --backend=jit argv_echo.vs one two
```

Use the VM command with Runtime’s supplied interpreter. Before trying the JIT command, replace `/path/to/Jit` with a compatible backend; a loading failure stops before the script runs. On success, both commands pass `one` and `two` to `argv_echo.vs` as before. For project defaults, automatic selection and disabling JIT, see the [CLI backend reference](../../../../../VeroScriptCli/Resources/Docs/User/en-US/index.md#cli-backend-policy).

Debugging uses the VM debug path. For now, remember the workflow: use `--check` during development, and select a backend only when you have a reason to compare or constrain execution.

## Exercises

1. If no argument is supplied, print `usage: greet.vs <name>` and return a non-zero code.
2. Read a second argument, check that it exists, and print its length with `len`.
3. Compare diagnostics from `--lang en-US` and `--lang zh-CN`.

Next: [Values, conditions, and loops](../language/03-values-and-control-flow.md).

The complete example is [`examples/argv_echo.vs`](../../../../../VeroScript/Resources/Scripts/examples/argv_echo.vs). See the [CLI reference](../../../../../VeroScriptCli/Resources/Docs/User/en-US/index.md) for options.

For performance comparisons, use a normal run with fixed inputs and backend, and allow warm-up. Debugging, retained initialization and bounded cleanup use the VM; eligible ordinary functions in persistent sessions may use the selected JIT. The bytecode cache reduces startup compilation work. Session JIT-cache behavior and limits are described in the [C++ embedding guide](../../../../../Extensions/Language/Cpp/Resources/Docs/User/en-US/embedding.md#threads-and-hot-reload).

<!-- docs-navigation -->

---

[Tutorials](../index.md)

Language: [en-US](02-cli-and-arguments.md) · [fr-FR](../../fr-FR/getting-started/02-cli-and-arguments.md) · [ja-JP](../../ja-JP/getting-started/02-cli-and-arguments.md) · [ko-KR](../../ko-KR/getting-started/02-cli-and-arguments.md) · [zh-CN](../../zh-CN/getting-started/02-cli-and-arguments.md) · [zh-TW](../../zh-TW/getting-started/02-cli-and-arguments.md)
