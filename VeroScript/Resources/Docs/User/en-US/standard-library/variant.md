# variant — Dynamic value box

variant is a Core type; no import is required. std::variant and variant name the same type. It stores one value with its canonical source type, or an empty state.

A known variant destination boxes a concrete value implicitly. Reading requires an explicit cast<T> or try_cast<T>; variant_is<T> checks the exact stored type and does not convert. `optional<T>` and `try_cast` are also built in and require no standard-library import. `cast<T>` from an empty box fails; `try_cast<T>` from an empty box or an ordinary conversion mismatch returns an empty optional. Clone failures, cancellation and limits remain errors.

A variant copy shares an immutable box. Boxing/extracting a script struct takes a value snapshot; classes and containers retain their normal reference semantics. The static declared type is preserved, including a base type and typed null. A typed null is present and differs from an empty variant. Raw nullptr requires a concrete typed variable first.

Only variant-to-variant equality is defined: the canonical type and normal payload equality must both match. There is no dynamic arithmetic, member lookup, call, implicit unboxing, ordering or hashing. Variant keys, channel transfer and worker arguments/results/captures/globals containing variant are rejected; worker-local values are allowed. NativeArray and ordinary array/map values can contain variant.

```vero
import "core.vs";
variant empty;
variant answer = 42;
assert(!variant_has_value(empty));
assert(variant_is<int>(answer));
int exact = cast<int>(answer);
optional<double> converted = try_cast<double>(answer);
map<string, variant> properties = map<string, variant>();
properties["width"] = answer;
properties["text"] = "Save";
assert(cast<int>(properties["width"]) == exact);
```

VM supports the full semantics. Mixed/asmjit safely fall back to VM for unsupported variant instructions. Portable bytecode and packages preserve declarations and instructions. Standalone C++, C# and Python AOT reject variant dependencies explicitly before generating output. The debugger exposes read-only $present, $type and $value children; type<variant>().kind() returns "variant".

## SDK

See [Interop SDK — variant](../../../../../../VeroScriptInterop/Resources/Docs/User/en-US/index.md#exported-value-lifetime).

When an exported object outlives a host call and its methods or destructor need script globals, use the existing persistent session mode: C++ SetHotReloadEnabled(true), C# HotReloadEnabled, or Python hot_reload_enabled=True. Ordinary temporary execution releases its global state at the end of the call.

```text
C:      vs_interop_value_set_variant_empty / vs_interop_value_set_variant
        vs_interop_variant_has_value / vs_interop_variant_get_type_name / vs_interop_variant_get
C#:     value.SetVariantEmpty(); session.CreateVariant(typeName, payload);
        value.VariantHasValue; value.VariantTypeName; value.GetVariantValue();
Python: Value.empty_variant(); session.variant(type_name, payload_value);
        value.set_variant_empty(); value.assign_variant(other_box);
        value.variant_has_value(); value.variant_type_name(); value.variant_get();
C++:    VeroScriptVariant.h, VeroScriptValueRoot.h
        session.MakeVariant(typeName, payload, output, error);
        session.RetainValue(output, root, error); root.Read(); root.Reset();
```

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](variant.md) · [fr-FR](../../fr-FR/standard-library/variant.md) · [ja-JP](../../ja-JP/standard-library/variant.md) · [ko-KR](../../ko-KR/standard-library/variant.md) · [zh-CN](../../zh-CN/standard-library/variant.md) · [zh-TW](../../zh-TW/standard-library/variant.md)
