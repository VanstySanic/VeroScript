# 15: Configure project options and diagnostic localization

Use CLI options to select import paths, definitions and script arguments. These toolchain settings do not change VeroScript language semantics.

## Prepare the lesson files

Use a matching Runtime with its supplied VM and localization resources. Create a writable lesson directory containing `main.vs` and the `src`/`shared` subdirectories. Save the three files below and run all commands from that lesson directory. The import paths and definitions are per invocation; they are repeated in every command that checks or runs `main.vs`.

### main.vs

```VeroScript
import "score.vs";

int main(string[] args)
{
#ifdef FEATURE_SCORE
	print("score={}, argc={}", scoreValue(), len(args));
#else
	print("feature disabled");
#endif
	return 0;
}
```

### src/score.vs

```VeroScript
import "settings.vs";

export int scoreValue()
{
	return baseValue() + 2;
}
```

### shared/settings.vs

```VeroScript
export int baseValue()
{
	return 40;
}
```

With `FEATURE_SCORE=1`, the run with arguments `demo 42` prints `score=42, argc=2` and returns zero. Removing the definition selects the `feature disabled` branch. A successful check returns zero without calling `main`.

## Step 1: configure compilation options

Pass import paths with `-I` and definitions with `-D`; repeat these options for multiple values. The CLI reads its options directly.

```powershell
veroscript --check -I src -I shared -D FEATURE_SCORE=1 main.vs
```

## Step 2: choose an execution backend

For this lesson, use Runtime’s supplied VM so no optional JIT package is needed. The command below passes `demo` and `42` to `main.vs` and preserves the script’s ordinary output and exit result. Other policies and module-environment selection are described in the [CLI backend reference](../../../../../VeroScriptCli/Resources/Docs/User/en-US/index.md#cli-backend-policy).

```powershell
veroscript --vm-backend=vs-bytecode --backend=vm -I src -I shared -D FEATURE_SCORE=1 main.vs demo 42
```

## Step 3: check before running

Check the entry before execution:

```powershell
veroscript --check -I src -I shared -D FEATURE_SCORE=1 main.vs
veroscript -I src -I shared -D FEATURE_SCORE=1 main.vs demo 42
```

`main(string[] args)` receives arguments after the script path; the path itself is not in `args`. The CLI process exit code comes from the final result of `int main`, `void main`, or `exit(n)`.

## Step 4: choose a diagnostic language

Use `--lang` to select a language for one CLI invocation:

```powershell
veroscript --lang zh-CN --check -I src -I shared -D FEATURE_SCORE=1 main.vs
veroscript --lang en-US --check -I src -I shared -D FEATURE_SCORE=1 main.vs
```

You can also set `VERO_LANG` for the current PowerShell session:

The environment-variable example restores the previous `VERO_LANG` after checking.

```powershell
$tutorialPreviousLanguage = $env:VERO_LANG
try
{
	$env:VERO_LANG = "zh-CN"
	veroscript --check -I src -I shared -D FEATURE_SCORE=1 main.vs
}
finally
{
	$env:VERO_LANG = $tutorialPreviousLanguage
}
```

Language tags are discovered from deployed catalog directories. Missing translations fall back to English; an unsupported explicit `--lang` value is an argument error.
Parser declaration diagnostics, including invalid `static class` and `static struct` declarations, use the same stable IDs and catalogs in the CLI and LSP. Use the diagnostic ID for tooling logic; treat the translated message as display text.

## Step 5: provide a project localization directory

Copy the distribution’s complete `localization/` tree into the project and retain its full `en-US` baseline. The layout below shows the language catalog; keep the other copied catalog files as well.

```text
localization/
  en-US/
    language.json
    ...
  zh-CN/
    language.json
    ...
```

Select the project tree explicitly with `VERO_LOCALIZATION_DIR`: catalogs beside the executable take priority over automatic discovery in the working directory. The example restores the previous setting after the command.

```powershell
$tutorialPreviousLocalization = $env:VERO_LOCALIZATION_DIR
try {
    $env:VERO_LOCALIZATION_DIR = (Resolve-Path ./localization).Path
    veroscript --lang zh-CN --check -I src -I shared -D FEATURE_SCORE=1 main.vs
}
finally
{
    $env:VERO_LOCALIZATION_DIR = $tutorialPreviousLocalization
}
```

`en-US` defines the complete baseline. Other qualified canonical BCP 47 directories may contain only files and message IDs present in the English tree. Missing translated files or keys fall back to English. Files must be flat UTF-8 JSON objects with string values, no duplicate keys and no trailing content. To add a language, add its canonical directory and JSON files to this selected tree.

## Exercises

1. Remove `-D FEATURE_SCORE=1` from one run and observe `feature disabled`; restore it and confirm `score=42, argc=2` with `demo 42`.
2. Pass three arguments to the entry and print them from `main(string[] args)`.
3. With the project tree selected, remove `diag.expected_expression` from `zh-CN/language.json`, run the command below and confirm that “expected expression” appears in English.

```powershell
$tutorialPreviousLocalization = $env:VERO_LOCALIZATION_DIR
try {
    $env:VERO_LOCALIZATION_DIR = (Resolve-Path ./localization).Path
    veroscript --lang zh-CN -e 'int main() { return +; }'
}
finally
{
    $env:VERO_LOCALIZATION_DIR = $tutorialPreviousLocalization
}
```

[CLI reference](../../../../../VeroScriptCli/Resources/Docs/User/en-US/index.md) · [Localization](../../../../../VeroScript/Resources/Docs/User/en-US/localization.md)

<!-- docs-navigation -->

---

[Tutorials](../index.md)

Language: [en-US](15-workspace-and-localization.md) · [fr-FR](../../fr-FR/toolchain/15-workspace-and-localization.md) · [ja-JP](../../ja-JP/toolchain/15-workspace-and-localization.md) · [ko-KR](../../ko-KR/toolchain/15-workspace-and-localization.md) · [zh-CN](../../zh-CN/toolchain/15-workspace-and-localization.md) · [zh-TW](../../zh-TW/toolchain/15-workspace-and-localization.md)
