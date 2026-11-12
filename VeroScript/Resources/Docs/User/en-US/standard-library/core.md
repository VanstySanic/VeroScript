# Core entry module

Module file: `core.vs`

`core.vs` is the aggregate standard-library entry point. It re-exports these modules with `export import`:

- `i18n.vs`
- `memory.vs`
- `math.vs`
- `algorithm.vs`
- `numeric.vs`
- `random.vs`
- `io.vs`
- `string.vs`
- `path.vs`
- `concurrent.vs`
- `attribute.vs`
- `callable.vs`
- `delegate.vs`

Import the runtime modules listed above with:

```VeroScript
import "core.vs";
```

Individual modules can also be imported directly to make dependencies and name scope explicit. `core.vs` does not introduce a separate API; it exposes the public declarations of the other standard-library modules.

See the corresponding module pages for the signatures of formatting, file, concurrency, and event operations.

- [Application internationalization — std::i18n](i18n.md): `std::i18n`

Import `compiler.vs` explicitly in a compile extension. It is not re-exported by `core.vs`, and importing it in ordinary runtime code does not grant compilation authority.

[Compile extensions](compiler.md)

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](core.md) · [fr-FR](../../fr-FR/standard-library/core.md) · [ja-JP](../../ja-JP/standard-library/core.md) · [ko-KR](../../ko-KR/standard-library/core.md) · [zh-CN](../../zh-CN/standard-library/core.md) · [zh-TW](../../zh-TW/standard-library/core.md)
