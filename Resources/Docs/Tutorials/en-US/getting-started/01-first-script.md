# 01: Your first script

This tutorial has one goal: create a script, run it, and see a line of output.

## What you will learn

- What a minimal VeroScript program contains.
- How to run a script.
- How to check a script without executing it.
- What `main` and `print` do.

## Step 1: create a file

Create `hello.vs` in your working directory:

```VeroScript
int main()
{
	print("hello, VeroScript");
	return 0;
}
```

There are two important pieces: `main` is the script entry point, and `int` returns the process exit status. `print` writes a string to standard output. `void main` remains supported, but complete programs generally use `int main` and explicitly return `0` for success.

## Step 2: check, then run

Run these commands from the directory containing `hello.vs`:

```powershell
veroscript --check hello.vs
veroscript hello.vs
```

The output should look like:

```text
hello, VeroScript
```

`--check` compiles the file and reports problems without executing `main`. Use it first during development, then run the script after the check succeeds.

## Step 3: make the output useful

Change the script to:

```VeroScript
int main()
{
	string name = "Ada";
	print("hello, " + name);
	return 0;
}
```

You should now see:

```text
hello, Ada
```

`string name` declares a string variable. The `+` operator concatenates the two strings.

## If something goes wrong

- If `veroscript` cannot be found, confirm that the CLI is installed and is on `PATH`.
- If `hello.vs` cannot be found, check the terminal's current directory or pass an absolute path.
- Run `veroscript --lang en-US --check hello.vs` to request English diagnostics.

## Exercises

1. Replace the name with your own.
2. Add `string language = "VeroScript";` and print a sentence using both variables.
3. Remove a semicolon, run `--check`, read the diagnostic, and fix the file.

Next: [Checking, running, and reading arguments](02-cli-and-arguments.md).

The complete minimal example is [`examples/hello.vs`](../../../../../VeroScript/Resources/Scripts/examples/hello.vs). For syntax, see [Basic syntax](../../../../../VeroScript/Resources/Docs/User/en-US/language-basics/basic-syntax.md).

<!-- docs-navigation -->

---

[Tutorials](../index.md)

Language: [en-US](01-first-script.md) · [fr-FR](../../fr-FR/getting-started/01-first-script.md) · [ja-JP](../../ja-JP/getting-started/01-first-script.md) · [ko-KR](../../ko-KR/getting-started/01-first-script.md) · [zh-CN](../../zh-CN/getting-started/01-first-script.md) · [zh-TW](../../zh-TW/getting-started/01-first-script.md)
