# Versioning and compatibility


Version information affects source files, bytecode caches, application packages, library packages, and host bindings. A deployment should use mutually compatible language and runtime versions for all of these components.

Inspect the installed version with:

```powershell
veroscript --version
```

## File-format compatibility

Product, bytecode, Interop ABI, and WebAssembly ABI use the initial `0.1.1` baseline. Bytecode loaders require an exact format version and magic match. Rebuild `.vsbc`, `.vsapp`, and `.vslib` files made with older development builds from source; compatibility with those binaries is not provided.

- `.vs` is source and should normally be rechecked by the matching compiler;
- `.vsbc` is a bytecode cache and should not be treated as a cross-version distribution format;
- `.vsapp` is a runnable application package and must match its runtime and package version;
- `.vslib` is a library package whose public declarations, package version, and runtime must remain compatible.

After a version upgrade, run `--check` again, rebuild packages, and regenerate or validate native artifacts when using AOT or host bindings.

The current length and linear-index APIs use `int64`. Rebuild binary packages and bytecode caches produced before this change, even if `--version` reports the same product version. Use the matching standard library and regenerate AOT outputs when upgrading.

## Language boundaries

The current implementation does not provide script-pointer semantics or standalone C++ concepts syntax; `__VA_OPT__` for variadic macros, lambda reference captures, reference returns, and runtime reflection are public capabilities. Graphical user interfaces are outside the core runtime. Treat the implemented capability list and version notes as authoritative instead of assuming that C++ syntax is automatically available.

## Host-binding compatibility

Host bindings depend not only on function names but also on parameter types, return types, container-element types, key/value types, and host-object lifetime. When these boundaries change, update the binding catalog, host code, and script callers together, then repeat checking and runtime validation.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](versioning.md) · [fr-FR](../../fr-FR/language-reference/versioning.md) · [ja-JP](../../ja-JP/language-reference/versioning.md) · [ko-KR](../../ko-KR/language-reference/versioning.md) · [zh-CN](../../zh-CN/language-reference/versioning.md) · [zh-TW](../../zh-TW/language-reference/versioning.md)
