# Standard Library Reference

The VeroScript standard library provides modules for common algorithms, numeric aggregation, deterministic random state, strings, mathematics, files, paths, concurrency, and events. A script can import `core.vs` for the aggregate runtime library or import only the modules it needs.

Use the full public namespace when calling module-owned functions, such as `std::strings::join` or `std::concurrent::spawn`. Bare global spellings are rejected. Core types and direct functions such as `array<T>` and `print` still accept their canonical names. Namespace aliases do not replace the required module import. [std](../language-basics/standard-namespace.md).

- [Managed memory](memory.md)：`std::memory`

- [Attributes and method callables](attribute.md)：`std::attribute<T>` · `std::bind_method<F>`

## Module index

- [Built-ins](builtins.md): runtime output, conversions, numeric and environment functions; input wrappers require io.vs.
- [Core entry point](core.md): re-exports the runtime modules listed in that reference; compiler.vs requires an explicit import.
- [Strings](string.md): searching, slicing, splitting, joining, trimming, case conversion, replacement, and formatting.
- [Math](math.md): trigonometry, square roots, rounding, and clamping.
- [Algorithms](algorithm.md): generic array sorting, reversal, lookup, counting, and binary search.
- [Numeric](numeric.md): generic accumulation, sums, and averages.
- [Random](random.md): deterministic pseudo-random streams with explicit independent state.
- [I/O](io.md): files, directories, standard input, and the working directory.
- [Paths](path.md): file names, directory names, extensions, and path joining.
- [Concurrency](concurrent.md): channels, isolated tasks, task waiting, and cooperative cancellation.
- [Events](events.md): unicast and multicast delegates.

## Import conventions

Import the complete library:

```VeroScript
import "core.vs";
```

Or import individual modules:

```VeroScript
import "string.vs";
import "path.vs";
```

Standard-library functions are normally exported declarations. Their parameter and return types are defined by the corresponding module implementation; operations involving files, threads, or the host environment can also depend on runtime permissions and host configuration.

- [Application internationalization — std::i18n](i18n.md): `std::i18n`

Import `compiler.vs` explicitly in a compile extension. It is not re-exported by `core.vs`, and importing it in ordinary runtime code does not grant compilation authority.

[Compile extensions](compiler.md)


- [Standard library foundation](foundation.md)

- [variant — Dynamic value box](variant.md)
<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](index.md) · [fr-FR](../../fr-FR/standard-library/index.md) · [ja-JP](../../ja-JP/standard-library/index.md) · [ko-KR](../../ko-KR/standard-library/index.md) · [zh-CN](../../zh-CN/standard-library/index.md) · [zh-TW](../../zh-TW/standard-library/index.md)
