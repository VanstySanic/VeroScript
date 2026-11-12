# Reflection

VeroScript provides runtime reflection for querying modules, types, inheritance, globals, free functions, fields, methods, enum members, and declaration metadata. Typed operations can also access public script instance fields and call public methods declared on script classes or interfaces. Reflection does not bypass normal access control.

## Reflection objects

The language provides these public information types:

- `type<T>` describes the type `T` itself.
- `field_info<T>` describes one field of `T`.
- `method_info<T>` describes one method of `T`.
- `enum_member_info<T>` describes one enumerator of enum type `T`.
- `module_info` describes the current module view.
- `function_info` describes one visible free function.
- `global_info` describes one visible top-level global.

These types are provided by the compiler and require no import. Queries use a qualified owner name and a zero-based index. An invalid index is reported as a runtime error.

## Type information

The public members of `type<T>` are:

| Member | Purpose |
| --- | --- |
| `name` | Short type name. |
| `qualified_name` | Qualified type name, suitable for constructing `field_info<T>` and `method_info<T>`. |
| `kind()` | Returns the type category. |
| `is_abstract()` | Reports whether the type is abstract. |
| `base_count()` / `base_name(i)` | Queries the number and names of direct base types. |
| `field_count()` | Returns the number of reflectable fields. |
| `method_count()` | Returns the number of reflectable methods. |
| `metadata_count()`, `metadata_key(i)`, `metadata_value(i)` | Queries metadata on the type declaration. |
| `has_metadata(key)`, `metadata(key)` | Looks up metadata without manually enumerating it. |
| `has_annotation(name)` | Tests an annotation name; both `name` and `@name` are accepted. |
| `enum_member_count()`, `enum_member_name(i)`, `enum_member_value(i)` | Queries enum members. |
| `enum_underlying_type()` | Returns the underlying enum type. |
| `is_scoped_enum()` | Reports whether the enum is scoped. |

Enum-specific queries are meaningful only for enum types. Check `kind()` before using them.

## Field information

Construct a field query with `field_info<T>(owner, index)`:

```VeroScript
type<Record> info = type<Record>();
field_info<Record> field = field_info<Record>(info.qualified_name, 0);

print(field.name());
print(field.declared_type());
print(field.runtime_type());
print(field.is_static());
print(field.is_mutable());
```

Field information provides:

| Member | Purpose |
| --- | --- |
| `name()` | Field name. |
| `declared_type()` | Field type as declared in source. |
| `runtime_type()` | Type recorded by the runtime. |
| `is_static()` | Reports whether the field is static. |
| `is_mutable()` | Reports whether the field uses the contextual `mutable` modifier. |
| `is_declared_on_type()` | Reports whether the field is declared directly on the queried type. |
| `metadata_count()`, `metadata_key(i)`, `metadata_value(i)` | Queries metadata on the field declaration. |
| `has_metadata(key)`, `metadata(key)`, `has_annotation(name)` | Looks up field metadata and annotations by name. |

## Method information

Construct a method query with `method_info<T>(owner, index)`:

```VeroScript
method_info<Record> method = method_info<Record>(info.qualified_name, 0);

print(method.name());
print(method.return_type());
print(method.parameter_count());
print(method.parameter_type(0));
print(method.parameter_is_ref(0));
print(method.parameter_is_const(0));
```

Method information provides:

| Member | Purpose |
| --- | --- |
| `name()` | Method name. |
| `return_type()` | Return type. |
| `parameter_count()` | Number of parameters. |
| `parameter_type(i)` | Base type of parameter `i`. |
| `parameter_name(i)` | Name of parameter `i`. |
| `parameter_is_ref(i)` | Reports whether parameter `i` is a reference. |
| `parameter_is_const(i)` | Reports whether parameter `i` is const or a read-only reference. |
| `is_static()` | Reports whether the method is static. |
| `is_virtual()` | Reports whether the method is virtual. |
| `is_abstract()` | Reports whether the method is abstract. |
| `is_const()` | Reports whether the member method is const. |
| `is_deleted()` | Reports whether the method is deleted with `= delete`. |
| `is_constructor()` / `is_destructor()` | Reports whether the method is a constructor or destructor. |
| `metadata_count()`, `metadata_key(i)`, `metadata_value(i)` | Queries metadata on the method declaration. |
| `has_metadata(key)`, `metadata(key)`, `has_annotation(name)` | Looks up method metadata and annotations by name. |

`parameter_type(i)` returns the underlying type after removing `ref` or `const ref`. To reconstruct the full parameter semantics, also inspect `parameter_is_ref(i)` and `parameter_is_const(i)`.

## Executable reflection

`set<V>` cannot write a field declared `const`, even through a writable receiver.
For a `const` receiver, only a `mutable` field can be written. `get<V>` can
still read these fields.

After selecting descriptors for `Counter.value`, `Counter.add`, and
`Counter.twice` and creating an instance named `counter`, use an explicit value
or return type:

```VeroScript
field.set<int>(counter, 10);
int before = field.get<int>(counter);
int after = add.invoke<int>(counter, 5);
int doubled = twice.invoke_static<int>(21);
```

`get<V>` reads a public instance field; `set<V>` writes it and updates the
original struct or class object. `invoke<R>` calls a public instance method,
including methods declared on interfaces, and `invoke_static<R>` calls a public
static method. An abstract interface method resolves to the receiver's concrete
implementation; public interface methods with bodies are also executable.
`R` may be `void`. `V` or `R` must exactly match the selected member's runtime
type. Method arguments are positional, evaluated once, and checked against the
descriptor's fixed signature before invocation. Use `parameter_count()` and
`parameter_type(i)` to distinguish overloads. Invalid, forged, stale, foreign,
or inaccessible descriptors fail at runtime. Private or protected members,
host members, static fields, constructors, destructors, and deleted methods
remain queryable but cannot be executed through these APIs. C++, Python and C# AOT currently reject
executable reflection at compile time. Bytecode packages from older development builds are not supported.
Rebuild them from source with the current compiler; the initial-release version remains 0.1.1.
See the [complete executable example](../../../../Scripts/examples/reflection_executable.vs).

`invoke` and `invoke_static` support reference parameters and reference returns. Pass reference arguments explicitly as `&value` or `ref value`; their types must match exactly, and writable references cannot bind read-only storage. Use `invoke<int&>(receiver, ...)` or `invoke<const int&>(receiver, ...)` for reference returns, and the same type arguments with `invoke_static`. Returned references preserve aliasing and constness; ordinary value reads dereference them. Storage uses the same managed reference cells as ordinary calls, without copying arguments to simulate writeback. All arguments are validated before entering the method. Changes made before a method throws follow ordinary call semantics.

To pass a returned reference as another reference argument, first bind it to a local reference, then pass `&alias` or `ref alias`.

Object arguments are also validated using their static types at the call site: valid upcasts and null handles are accepted, while implicit downcasts are rejected before entering the method body. Use an explicit `cast<T>` before passing a downcast value. `set<V>` checks source-type assignability to `V` at compile time. `void` expressions cannot be arguments.

`set<V>` applies the ordinary checked assignment conversion to its value, so an `int` value can be written to an `int64` field with `set<int64>`. Numeric overflow is rejected before the field is changed; `V` must still exactly match the declared field type.

Struct and fixed-array arguments passed by value are copied at each argument evaluation, before later arguments can modify the source. Using a reference return to initialize a value variable, pass a value argument, or produce a value return also copies the value. Class and interface handles retain their ordinary semantics. Rebuild older bytecode packages from source to obtain these compiler fixes.

## Module, free-function, global, and enum-member information

`module_info()` exposes the declarations visible from the entry module. Use `function_count()`, `global_count()`, and `type_count()` as bounds, then construct `function_info(i)` or `global_info(i)`. `type_name(i)` returns each visible script type name. Imported private declarations are not exposed; entry-module private declarations and exported imported declarations remain visible according to the normal import/export rules.

`function_info` reports the name, stable linkage name, module path, return and parameter signature, `ref`/`const` flags, deleted/exported flags, and declaration metadata. `global_info` reports the name, linkage name, module path, declared/runtime types, const/exported flags, and declaration metadata. Static data members remain fields and are not duplicated in `global_info`.

For an enum `E`, construct `enum_member_info<E>(type<E>().qualified_name, i)` to inspect an enumerator's name, full `int64` value, and declaration metadata. This complements the compact enum queries already available on `type<E>`.

## Declaration metadata

Declarations can carry annotations. The language stores annotation names, keys, and values but does not assign them application-specific meaning:

```VeroScript
@entity(sample)
struct BaseRecord
{
	int id;
};

struct Record : BaseRecord
{
	@data(display)
	mutable string label;

	@call(sample)
	virtual int add(const int& value) const
	{
		return id + value;
	}
};
```

A host, editor, or other tool can enumerate this information through `metadata_count()`, `metadata_key(i)`, and `metadata_value(i)`, or use `has_metadata(key)`, `metadata(key)`, and `has_annotation(name)` for direct lookup. A missing key returns an empty string from `metadata(key)`; use `has_metadata(key)` when an empty value and a missing key must be distinguished. In this example `@entity`, `@data`, and `@call` are queryable metadata names, not built-in business behaviors; a script or host integration may define its own conventions.

Annotations in a host binding catalog and runtime script reflection are two separate layers. A binding catalog describes host APIs, while the reflection information types query the current compiled script module.

## Complete example

The following example queries inheritance, fields, methods, static methods, top-level globals, enum members, and declaration metadata:

```VeroScript
@setting(primary)
export const int ReflectionVersion = 1;

@utility(sample)
export int echo_value(const int& value)
{
	return value;
}

enum class RecordState
{
	@display(primary)
	Ready,
	Archived
};

struct BaseRecord
{
	int id;

	int read_id() const
	{
		return id;
	}
};

@entity(sample)
struct Record : BaseRecord
{
	@data(display)
	mutable string label;

	@call(sample)
	virtual int add(const int& value) const
	{
		return id + value;
	}

	static int identity(int value)
	{
		return value;
	}
};

int main()
{
	type<Record> info = type<Record>();
	print("{} ({})", info.name, info.kind());
	print("bases={}, fields={}, methods={}",
		info.base_count(), info.field_count(), info.method_count());

	for (int i = 0; i < info.field_count(); i++)
	{
		field_info<Record> field = field_info<Record>(info.qualified_name, i);
		print("field {}: declared={}, runtime={}, static={}",
			field.name(), field.declared_type(), field.runtime_type(), field.is_static());
	}

	for (int i = 0; i < info.method_count(); i++)
	{
		method_info<Record> method = method_info<Record>(info.qualified_name, i);
		print("method {}: returns={}, params={}, static={}, virtual={}",
			method.name(), method.return_type(), method.parameter_count(),
			method.is_static(), method.is_virtual());
	}

	module_info module = module_info();
	print("module={}, functions={}, globals={}, types={}",
		module.path(), module.function_count(), module.global_count(), module.type_count());

	for (int i = 0; i < module.function_count(); i++)
	{
		function_info function_meta = function_info(i);
		print("function {}: returns={}, exported={}",
			function_meta.name(), function_meta.return_type(), function_meta.is_exported());
	}

	for (int i = 0; i < module.global_count(); i++)
	{
		global_info global_meta = global_info(i);
		print("global {}: declared={}, runtime={}, const={}",
			global_meta.name(), global_meta.declared_type(),
			global_meta.runtime_type(), global_meta.is_const());
	}

	type<RecordState> state_type = type<RecordState>();
	enum_member_info<RecordState> state =
		enum_member_info<RecordState>(state_type.qualified_name, 0);
	print("enum member {}={} annotated={}",
		state.name(), state.value(), state.has_annotation("display"));
	return 0;
}
```

See the reflection entry in the [Examples index](../examples.md) for a runnable version. The reflection API is independent of the entry-point return type; the example uses `int main` and returns an explicit process status code.

## Boundaries and errors

- Reflection supports inspection and the typed executable operations shown above; it does not provide unrestricted dynamic access to arbitrary members.
- `field_count()`, `method_count()`, and enum-member counts describe the current runtime type information; check bounds before indexing.
- Missing type information, invalid indices, and inapplicable queries are runtime errors. Do not treat an invalid index as an empty result.

For the underlying type rules, see [Data Types](data-types.md), [Structs](structs.md), [Classes](classes.md), and [Enums](enums.md). Host-side interface descriptions are covered in [Binding catalogs](../../../../../../Extensions/Language/Cpp/Resources/Docs/User/en-US/bindings.md).

## Structured reflection

<a id="reflection-metadata-contract"></a>

`type<T>`, field_info, method_info, module_info, function_info, global_info and enum_member_info expose annotation_count/annotation/annotations, comment_count/comment/comments, documentation, comments_available, comments_retention, declaration and source_metadata. Check counts before indexing. annotation_info and annotation_argument_info expose argument_count/argument/arguments; typed value access must match the scalar kind. definition() returns registered descriptive information; use is_valid() when no definition may be registered.

Comment availability is selected by the host: Full, Documentation or NoComments; Unavailable means source information is absent. NoComments does not remove annotations or separately registered documentation. Metadata objects retain an immutable snapshot, so hot reload does not silently retarget an old object. Metadata queries do not bypass access checks or make a private member callable.

[Annotations and source metadata](annotations.md)

## Nested type reflection

Use `display_name()`, `is_nested()`, `is_local()`, `declaring_type_name()`, `declaring_function_name()` and `access()` for ownership. `access()` returns `none` for a declaration outside a type. `qualified_name` remains the concrete runtime identity; display names are not lookup keys.

`nested_type_count()`, `nested_type_name(i)`, `nested_type_parameter_count(i)` and `nested_type_declaration(i)` enumerate directly owned, accessible type declarations, including unmaterialized templates. Parameter counts exclude captured outer parameters. Aliases are not additional runtime types. The access context is fixed when creating the query. Local/private names are not added to global discovery.

## Template metadata

`module_info.template_declaration_count()` enumerates open declarations without instantiating them. Indexed `template_declaration_id/name/kind/owner` queries identify each record. `template_parameter_count`, `template_own_parameter_count` and `template_enclosing_parameter_count` separate the environments. Parameter queries take `(declaration_index, parameter_index)` and expose `template_parameter_id/name/kind/value_type/default/is_enclosing`; the combined parameter list puts enclosing parameters first.

`type<T>`, `method_info<T>` and `function_info` expose `template_declaration_id()`, `template_argument_count()`, and indexed `template_argument_encoding/name/kind/value_type`. Separate `template_enclosing_argument_count/encoding/name` queries describe enclosing arguments. Ordinary declarations have empty identities and zero counts. Encodings preserve integer width, signedness and enum identity; display names are not identity keys. Open queries do not create executable methods. Existing type-only parameter counts retain their meaning.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](reflection.md) · [fr-FR](../../fr-FR/language-basics/reflection.md) · [ja-JP](../../ja-JP/language-basics/reflection.md) · [ko-KR](../../ko-KR/language-basics/reflection.md) · [zh-CN](../../zh-CN/language-basics/reflection.md) · [zh-TW](../../zh-TW/language-basics/reflection.md)
