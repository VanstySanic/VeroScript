# Attributes and method callables

`attribute.vs` · `callable.vs` · `core.vs`

`std::attribute<T>` stores a fixed value or a synchronous getter. Copies have independent configuration; callable captures keep their normal language ownership. Reading never caches a result.

```VeroScript
import "attribute.vs";

int read_count()
{
	return 42;
}
void main()
{
	std::attribute<int> count = 10;
	count.bind(read_count);
	int snapshot = count;
	assert(snapshot == 42 && count() == 42);
	count.reset();
	assert(!count.try_get().has_value());
}
```

`get()` and `operator()` read once; conversion to `T` also reads once. `try_get()` returns an optional; `get_or(fallback)` uses an eagerly evaluated fallback. Empty getters and execution errors propagate. Only unset state, an empty optional reader or an expired weak target means no value.

`set(value)`, `bind(fn()->T)`, `bind_optional(fn()->optional<T>)` and `reset()` replace the complete configuration. `is_set()` and `is_bound()` inspect configuration without evaluating the getter.

`try_get(ref output)` reads once into an existing variable and returns whether a value was available. An unset attribute or empty optional reader leaves `output` unchanged; getter exceptions propagate. Use this overload to write into an existing variable; the optional-returning `try_get()` remains available.

`has_same_configuration(ref other)` compares configuration identity without evaluating either getter. Copies initially share that identity; replacing either configuration makes them different even when the values compare equal. Bound results may change without changing configuration, so this comparison cannot justify skipping dynamic getters.

`std::make_attribute_by_name<T>(target, "read")` binds a strong zero-argument getter. `T` is explicit; the receiver type is inferred or supplied as the second type argument. The name is case-sensitive, resolved at creation, and invocation keeps the ordinary access and virtual-call rules.

`std::make_weak_attribute<T>(weak_target, getter)` locks the target once per read. The getter has signature `fn(U)->T`. `std::make_weak_attribute_by_name<T>(weak_target, "read")` binds a zero-argument method. Expired weak bindings remain configured. Wrap structs in `shared<S>` or `handle<S>`.

`std::bind_method<F>(target, "method")` returns an ordinary typed function value. It requires a public instance method with the exact signature, preserves virtual dispatch and retains its strong receiver. It can be passed to `attribute.bind`, `delegate.bind` or `multicast_delegate.add`. Save the actual callable for later removal. Directly import `callable.vs` when only this factory is needed.

Standalone AOT: C++ supports fixed values, getters and weak targets; C# and Python support fixed values, named getters and literal strong method names. Python source lambdas and C#/Python weak bindings retain their existing unsupported boundary. Dynamic names, host-name bindings and ambiguous generic selections require the runtime and are rejected at export. VM, Mixed and JIT share the core implementation.

The C++ and C# source exporters support `try_get()` and `try_get(ref output)`. Python supports `try_get()` but rejects writable `ref` output. All three exporters support the read-only `has_same_configuration(ref other)` comparison; this special lowering does not add general writable reference support to Python.

[`attribute_demo.vs`](../../../../Scripts/examples/attribute_demo.vs) · [`attribute_functions.vs`](../../../../Scripts/examples/attribute_functions.vs) · [`attribute_methods.vs`](../../../../Scripts/examples/attribute_methods.vs) · [`attribute_weak.vs`](../../../../Scripts/examples/attribute_weak.vs)

Keep one module identity for each standard-library declaration. If a library package already includes and re-exports attribute, use its exported declarations or a shared dependency; importing another source copy of `core.vs` / `attribute.vs` creates duplicate exports.

For several library packages, first build one `shared-stdlib.vslib` from `export import "attribute.vs";`. Each library imports that component, and the application imports the libraries plus the same component instead of the source copy.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](attribute.md) · [fr-FR](../../fr-FR/standard-library/attribute.md) · [ja-JP](../../ja-JP/standard-library/attribute.md) · [ko-KR](../../ko-KR/standard-library/attribute.md) · [zh-CN](../../zh-CN/standard-library/attribute.md) · [zh-TW](../../zh-TW/standard-library/attribute.md)
