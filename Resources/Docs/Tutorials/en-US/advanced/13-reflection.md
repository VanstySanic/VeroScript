# 13: Runtime reflection

This tutorial builds a small type inspector to explain VeroScript's reflection API. Reflection exposes metadata from the current script type system and typed operations for public script fields and methods. It is useful for tools, editors, and serialization adapters; it does not turn arbitrary members into a dynamic string-call interface or bypass access control.

## Step 1: add metadata to declarations

Annotations can be attached to types, fields, methods, free functions, globals, and enum members:

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

	int score;

	int add_score(int amount)
	{
		score += amount;
		return score;
	}
};
```

The language stores annotation names, keys, and values but does not interpret their application-specific meaning. Here `entity`, `data`, and `call` are user-defined metadata conventions, not keywords that generate behavior automatically.

The following steps continue in the same file. Keep this `Record` declaration and replace or extend `main` as you progress.

## Step 2: obtain type information

`type<Record>` describes `Record` itself:

```VeroScript
int main()
{
	type<Record> info = type<Record>();
	print("name={}, qualified={}", info.name, info.qualified_name);
	print("kind={}, abstract={}", info.kind(), info.is_abstract());
	print("fields={}, methods={}", info.field_count(), info.method_count());
	return 0;
}
```

`name` is the short name and `qualified_name` is the qualified name. Field and method information objects are constructed with that qualified name and a zero-based index, so it is common to keep a `type<T>` object first.

## Step 3: enumerate fields

Read each field with `field_info<Record>(info.qualified_name, i)`:

```VeroScript
int main()
{
	type<Record> info = type<Record>();

	for (int i = 0; i < info.field_count(); i++)
	{
		field_info<Record> field = field_info<Record>(info.qualified_name, i);
		print("field {}: declared={}, runtime={}, static={}, mutable={}",
			field.name(), field.declared_type(), field.runtime_type(),
			field.is_static(), field.is_mutable());

		for (int j = 0; j < field.metadata_count(); j++)
		{
			print("  metadata {}={}", field.metadata_key(j), field.metadata_value(j));
		}
	}

	return 0;
}
```

Field information also reports whether a field is declared directly on the current type and whether it uses the contextual `mutable` modifier. `declared_type()` is useful for displaying the source declaration, while `runtime_type()` displays the type recorded by the runtime.

## Step 4: enumerate methods and parameters

`method_info<T>` provides method modifiers and parameter information:

```VeroScript
int main()
{
	type<Record> info = type<Record>();

	for (int i = 0; i < info.method_count(); i++)
	{
		method_info<Record> method = method_info<Record>(info.qualified_name, i);
		print("method {}: returns={}, static={}, virtual={}, const={}",
			method.name(), method.return_type(), method.is_static(),
			method.is_virtual(), method.is_const());

		for (int j = 0; j < method.parameter_count(); j++)
		{
			print("  param {}: name={}, type={}, ref={}, const={}",
				j, method.parameter_name(j), method.parameter_type(j),
				method.parameter_is_ref(j), method.parameter_is_const(j));
		}
	}

	return 0;
}
```

`parameter_type(j)` returns the underlying type after removing `ref` or `const ref`. Check `parameter_is_ref(j)` and `parameter_is_const(j)` separately when a tool needs to reconstruct the full parameter semantics.

## Step 5: inspect the module surface

`module_info` enumerates the free functions, top-level globals, and script types visible from the entry module. Construct `function_info(i)` and `global_info(i)` with indices bounded by its counts:

```VeroScript
module_info module = module_info();
print("module={}, globals={}, types={}",
	module.path(), module.global_count(), module.type_count());

for (int i = 0; i < module.function_count(); i++)
{
	function_info function_meta = function_info(i);
	print("{} -> {}", function_meta.name(), function_meta.return_type());
}

for (int i = 0; i < module.global_count(); i++)
{
	global_info global_meta = global_info(i);
	print("global {}: declared={}, const={}",
		global_meta.name(), global_meta.declared_type(), global_meta.is_const());
}
```

All declaration descriptors provide `has_metadata(key)`, `metadata(key)`, and `has_annotation(name)`. For enum-member annotations, construct `enum_member_info<E>(type<E>().qualified_name, i)`.

For example, read the first member of `RecordState`:

```VeroScript
type<RecordState> state_type = type<RecordState>();
enum_member_info<RecordState> state =
	enum_member_info<RecordState>(state_type.qualified_name, 0);

print("{}={}, annotated={}",
	state.name(), state.value(), state.has_annotation("display"));
```

## Step 6: handle counts and errors

Reflection queries use counts and indices rather than returning a possibly empty dynamic object. Check field, method, function, global, type, or enum-member counts before indexing. Out-of-range indices, missing type information, and inapplicable queries are runtime errors.

Typed executable reflection is limited to public script instance fields and public script instance or static methods. It does not bypass access control or provide unrestricted string-based lookup and invocation. Names in the form `__reflection_*` are internal helper symbols, not a script API, and should not be used directly.

## Step 7: read, write, and invoke selected members

Select descriptors by their metadata, then provide the exact field or return type to the typed operation. Method arguments are positional:

```VeroScript
int main()
{
	type<Record> info = type<Record>();
	int score_index = -1;
	int add_index = -1;
	int identity_index = -1;
	for (int i = 0; i < info.field_count(); i++)
	{
		field_info<Record> candidate = field_info<Record>(info.qualified_name, i);
		if (candidate.name() == "score")
		{
			score_index = i;
		}
	}
	for (int i = 0; i < info.method_count(); i++)
	{
		method_info<Record> candidate = method_info<Record>(info.qualified_name, i);
		if (candidate.name() == "add_score")
		{
			add_index = i;
		}
		if (candidate.name() == "identity")
		{
			identity_index = i;
		}
	}
	if (score_index < 0 || add_index < 0 || identity_index < 0) return 1;

	field_info<Record> score = field_info<Record>(info.qualified_name, score_index);
	method_info<Record> add = method_info<Record>(info.qualified_name, add_index);
	method_info<Record> identity = method_info<Record>(info.qualified_name, identity_index);
	Record record = Record();
	score.set<int>(record, 10);
	print("score={}", score.get<int>(record));
	print("add_score={}", add.invoke<int>(record, 5));
	print("identity={}", identity.invoke_static<int>(21));
	return 0;
}
```

`get<V>` and `set<V>` operate on an instance field; `invoke<R>` calls an instance method and `invoke_static<R>` calls a static method. The selected member's value or return type must match `V` or `R` exactly. The complete reference documents unsupported signatures and backend-specific limits.

## Exercises

1. Add a static method to `Record` and locate it with `is_static()`.
2. Display `Record`'s direct bases with `base_count()` and `base_name(i)`.
3. Display `RecordState`'s members with `enum_member_count()`, `enum_member_name(i)`, and `enum_member_value(i)`.
4. Write a tool that prints only fields carrying `@data` metadata.

See [Reflection](../../../../../VeroScript/Resources/Docs/User/en-US/language-basics/reflection.md) for the complete rules. The inspection example is [`examples/reflection.vs`](../../../../../VeroScript/Resources/Scripts/examples/reflection.vs), and the executable operations are shown in [`examples/reflection_executable.vs`](../../../../../VeroScript/Resources/Scripts/examples/reflection_executable.vs). Host interface descriptions and script reflection are separate layers; host catalogs are covered in [Binding catalogs](../../../../../Extensions/Language/Cpp/Resources/Docs/User/en-US/bindings.md).

This completes the main tutorial path. Use the [toolchain reference](../../../User/en-US/toolchain/index.md) or [standard-library reference](../../../../../VeroScript/Resources/Docs/User/en-US/standard-library/index.md) whenever you need them.

<!-- docs-navigation -->

---

[Tutorials](../index.md)

Language: [en-US](13-reflection.md) · [fr-FR](../../fr-FR/advanced/13-reflection.md) · [ja-JP](../../ja-JP/advanced/13-reflection.md) · [ko-KR](../../ko-KR/advanced/13-reflection.md) · [zh-CN](../../zh-CN/advanced/13-reflection.md) · [zh-TW](../../zh-TW/advanced/13-reflection.md)
