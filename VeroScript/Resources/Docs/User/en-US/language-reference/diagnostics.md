# Diagnostics and errors

VeroScript problems generally fall into compile diagnostics and runtime errors. Compile diagnostics must be fixed before execution; runtime errors occur only when the relevant path executes.

## Compile diagnostics

Compilation checks lexical and syntactic structure, name lookup, conversions, overloads, generic constraints, interface implementation, module visibility, and declaration rules. Use `--check` to compile without running the script:

```powershell
veroscript --check script.vs
```

Check mode does not call `main`, making it suitable for save-time editor checks, CI validation, and pre-package verification. Diagnostics normally include a file, line and column information, and an explanation. One root cause can produce several related diagnostics, so fix the earliest error first.

## Runtime errors

Runtime failures can come from:

- an out-of-range array, string, or container index;
- an `assert` whose condition is `false`;
- an attempt to call an unbound delegate or perform another invalid runtime operation;
- a failed file, directory, or working-directory operation;
- an invalid operation on a closed channel;
- recursion depth, type-operation, or host-binding limits.

A runtime failure terminates the current execution or is reported through the host's run result and error stream. An embedding host should inspect `FRunResult` and preserve error text and diagnostic locations when needed.

VM, mixed, and JIT execution use the same language diagnostics. When a JIT
kernel reports an arithmetic or conversion failure, the runtime maps it back
to the best available source location and keeps any more specific helper
diagnostic. Hosts should consume the structured diagnostic and location rather
than matching the localized display text.

## Script exceptions

`throw` raises a script value that can propagate through calls and be handled by
`try`/`catch`. A typed handler or `catch (...)` can handle script exceptions, but
it cannot intercept engine/runtime faults, including a failed `assert`. A
`finally` block still runs while either kind of failure propagates. See
[Control flow](../language-basics/control-flow.md#exceptions) for the complete
syntax and matching rules.

## Language and output

The command-line tool can select the diagnostic language:

```powershell
veroscript --lang en --check script.vs
```

`VERO_LANG` can select the default language as well. Script standard output and diagnostic error output are separate streams and should remain separate in a host integration.

## Investigation order

Reference pages should establish declarations, scopes, types, and module visibility first. For an actual failure, use `--check` to eliminate compilation issues, reproduce runtime failures with minimal input, and then inspect host permissions, import paths, and standard-library search paths.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](diagnostics.md) · [fr-FR](../../fr-FR/language-reference/diagnostics.md) · [ja-JP](../../ja-JP/language-reference/diagnostics.md) · [ko-KR](../../ko-KR/language-reference/diagnostics.md) · [zh-CN](../../zh-CN/language-reference/diagnostics.md) · [zh-TW](../../zh-TW/language-reference/diagnostics.md)
