# Host bindings and binding catalogs

Host bindings expose functions, types, members, and metadata from a C++ host to VeroScript. A binding catalog is a JSON description of that public interface. The CLI can validate and format it, and can load one or more catalogs when checking or running a script.

A catalog is an interface contract. It does not replace the C++ registration code and does not provide a runtime implementation by itself. An entry in a catalog means that the compiler can see the entry; the host must still register the matching implementation before the script runs.

## Catalog file and version

The top-level object contains the following fields. The array fields may be omitted when importing a catalog; host export writes all of them. If `version` is present, it must be the integer `1`.

| Field | Type | Description |
| --- | --- | --- |
| `version` | Integer | Binding-catalog format version; currently `1`. |
| `functions` | Array | Descriptions of host functions callable from scripts. |
| `genericTypeTemplates` | Array | Descriptions of generic host-type templates. |
| `types` | Array | Host classes, structs, enums, and their members. |
| `typeAliases` | Array | Host type-alias descriptions. |
| `annotations` | Array | Annotation vocabulary for reflection, tools, and documentation. |

A complete empty catalog can be written as:

```json
{
	"version": 1,
	"functions": [],
	"genericTypeTemplates": [],
	"types": [],
	"typeAliases": [],
	"annotations": []
}
```

Save the file as `.json`. The `json` label on the code block is only the documentation syntax-highlighting label; it does not make the JSON a `.vs` script.

## Function descriptions

Each object in `functions` requires at least `name`, `returnType`, and `paramTypes`:

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `name` | String | Yes | Name visible to scripts. |
| `returnType` | String | Yes | Script type name, such as `int`, `string`, or `object`. |
| `paramTypes` | String array | Yes | Parameter types in the same order as the C++ registration. |
| `documentation` | String | No | Function documentation. |
| `paramNames` | String array | No | Parameter names in parameter order. |
| `paramDocumentation` | String array | No | Parameter documentation in parameter order. |
| `returnDocumentation` | String | No | Return-value documentation. |
| `annotations` | Object array | No | Annotation instances on the function. |
| `paramAnnotations` | Array of object arrays | No | Parameter annotations; the outer positions correspond to `paramTypes`. |
| `returnHostTypeName` | String | No | Host type name or signature for an `object`, `enum`, or `function` return. |
| `paramHostTypeNames` | String array | No | Host type names parallel to `paramTypes`. |
| `returnElementTypeName` | String | No | Element type of a unary container return. |
| `paramElementTypeNames` | String array | No | Element types of unary container parameters. |
| `returnKeyTypeName` | String | No | Key type of a map return. |
| `returnValueTypeName` | String | No | Value type of a map return. |
| `paramKeyTypeNames` | String array | No | Key types of map parameters. |
| `paramValueTypeNames` | String array | No | Value types of map parameters. |
| `paramFlags` | Integer array | No | Parameter passing modes; omitted means all parameters are passed by value. |
| `typeParams` | String array | No | Type parameters of a generic host function. |
| `scriptReturnTypeName` | String | No | Return type used on the script side of a generic call. |
| `genericHostObjectCast` | Boolean | No | Enables generic host-object conversion for the corresponding generic registration. |

Every parallel array uses the same parameter positions: `paramHostTypeNames[1]` and `paramFlags[1]` describe `paramTypes[1]`. Inconsistent lengths or positions should be corrected according to the CLI validator's diagnostic.

### Type names and container metadata

`returnType` and `paramTypes` use the type names supported by host bindings:

| Type category | Accepted names |
| --- | --- |
| Primitive and callable types | `void`, `bool`, `char`, `short`, `int`, `long`, `float`, `double`, `string`, `wstring`, `unsigned`, `unsigned int`, `unsigned char`, `unsigned short`, `unsigned long`, `int8`, `uint8`, `int16`, `uint16`, `int32`, `uint32`, `int64`, `uint64`, `enum`, `function`, `fn` |
| Host object | `object`, `HostObject` |
| Unary containers | `array`, `DynamicArray`, `set`, `DynamicSet`, `multiset`, `MultiSet`, `ordered_multiset`, `OrderedMultiSet`, `queue`, `stack`, `deque`, `channel`, `priority_queue`, `PriorityQueue` |
| Maps | `map`, `DynamicMap`, `ordered_map`, `OrderedMap`, `multimap`, `MultiMap`, `ordered_multimap`, `OrderedMultiMap` |

Container element, key, and value types are stored in separate metadata fields:

| Type | Return fields | Parameter fields |
| --- | --- | --- |
| `array`, sets, queues, stacks, deques, `channel`, and priority queues | `returnElementTypeName` | `paramElementTypeNames` |
| `map`, `ordered_map`, `multimap`, and `ordered_multimap` | `returnKeyTypeName`, `returnValueTypeName` | `paramKeyTypeNames`, `paramValueTypeNames` |
| `object`, `enum`, and `function` | `returnHostTypeName` | `paramHostTypeNames` |

For example, a function returning a `Widget` host object and accepting `array<int>` and `map<string,double>` can describe its types as follows:

```json
{
	"name": "inspect_widget",
	"returnType": "object",
	"paramTypes": ["array", "map"],
	"returnHostTypeName": "Widget",
	"paramElementTypeNames": ["int", ""],
	"paramKeyTypeNames": ["", "string"],
	"paramValueTypeNames": ["", "double"]
}
```

### Parameter flags

Each `paramFlags` element is a bit mask:

| Value | Meaning |
| --- | --- |
| `0` | Passed by value. |
| `1` (`0x01`) | Non-`const` reference. |
| `3` (`0x01 | 0x02`) | `const` reference. |

Other bit combinations are invalid; the `const` bit is valid only together with the reference bit. The flags must match the registered C++ signature. A catalog cannot turn a by-value C++ function into a reference function.

## Host types and members

Each object in `types` requires `name` and `kind`. `kind` must be `enum`, `struct`, or `class`:

| Field | Type | Description |
| --- | --- | --- |
| `name` | String | Name visible to scripts. |
| `kind` | String | `enum`, `struct`, or `class`. |
| `documentation` | String | Type documentation. |
| `annotations` | Object array | Annotation instances on the type. |
| `inheritable` | Boolean | Whether the type permits derivation. |
| `scopedEnum` | Boolean | Whether an enum uses scoped access semantics. |
| `hasClone` | Boolean | Whether copy behavior is provided. |
| `hasEquals` | Boolean | Whether equality comparison is provided. |
| `hasHash` | Boolean | Whether hashing is provided. |
| `hasSubclass` | Boolean | Whether host subclass construction is supported. |
| `genericTemplateName` | String | Generic host template from which this type was instantiated. |
| `genericTypeArguments` | String array | Actual type arguments for the generic template. |
| `members` | Object array | Constructors, methods, properties, fields, or enum members. |

Each member requires `kind`; ordinary members also require `name`. The supported member kinds are:

| `kind` | Meaning |
| --- | --- |
| `constructor` | Constructor; its name may be an empty string. |
| `method` | Instance method. |
| `staticMethod` | Static method. |
| `propertyGetter` | Property getter. |
| `propertySetter` | Property setter. |
| `enumMember` | Enum member. |
| `field` | Instance field. |
| `staticConstant` | Static constant. |

Members accept the function documentation, annotation, parameter, return, and type-metadata fields. They additionally support:

| Field | Type | Description |
| --- | --- | --- |
| `enumValue` | Integer | Enum value; it must be an exact JSON integer within the safe JSON-number range. |
| `enumValueInt64` | String | Full signed 64-bit enum value; use it outside the exact JSON-number range. |
| `operator` | Boolean | Whether the member is an operator. |
| `constMethod` | Boolean | Whether the method is `const`. |
| `mutableThis` | Boolean | Whether the method may mutate the current object. |
| `nativeName` | String | Native host name; the script-visible name is `name`. |

Members use the same meanings for `returnType`, `paramTypes`, and `paramFlags` as functions. Use `enumMember` for enum entries. For a value outside the exact JSON integer range, do not write an approximate JSON number in `enumValue`; write its decimal representation in `enumValueInt64` instead.

## Generic templates and type aliases

### `genericTypeTemplates`

Each generic template contains:

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `name` | String | Yes | Template name. |
| `arity` | Integer from `1` to `255` | Yes | Number of type parameters. |
| `documentation` | String | No | Template documentation. |

### `typeAliases`

Each alias contains:

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `name` | String | Yes | Alias name. |
| `typeParams` | String array | Yes | Alias type parameters; it may be empty. |
| `targetTypeName` | String | Yes | Target type name. |
| `documentation` | String | No | Alias documentation. |

For example:

```json
{
	"genericTypeTemplates": [
		{ "name": "Box", "arity": 1, "documentation": "A generic host box." }
	],
	"typeAliases": [
		{
			"name": "WidgetId",
			"typeParams": [],
			"targetTypeName": "int",
			"documentation": "The identifier used by the host."
		}
	]
}
```

## Annotation vocabulary and instances

The top-level `annotations` array is an annotation vocabulary. Each entry supports:

| Field | Type | Description |
| --- | --- | --- |
| `name` | String | Annotation name. |
| `detail` | String | Short category or signature description. |
| `documentation` | String | Annotation documentation. |
| `arguments` | Object array | Argument descriptions for the annotation. |
| `caseSensitive` | Boolean | Default `true`; `false` ignores ASCII letter case for this definition. |
| `targets` | Integer | Target bitmask, default `131071` (all supported targets); valid values are `1` through `131071`. |

Names retain their source spelling. Overlapping registrations are rejected when their accepted spellings intersect. Argument names and metadata keys remain case-sensitive. Each argument description can also contain a recursive `children` array of the same shape; it describes nested arguments, not executable values. Use `FHostBindingRegistry::RegisterAnnotation(..., Targets, CaseSensitive)` for native registration. Catalog metadata does not install processor callbacks.

Each vocabulary argument supports `name`, `detail`, and `documentation`. A vocabulary documents available annotation names; it does not apply an annotation to a function or type automatically.

The `annotations` field on a function, type, or member contains annotation instances:

```json
{
	"name": "HOST_FUNCTION",
	"arguments": [
		{ "name": "Category", "value": "Examples|Math" },
		{ "name": "ReadOnly" }
	]
}
```

The `value` field may be omitted. Omission means a flag argument with no value; `"value": ""` means that the argument explicitly has an empty-string value. These meanings are distinct. `paramAnnotations` is a two-dimensional array of parameter annotation instances, and its outer positions must match `paramTypes`.

Obtain the [binding-catalog sample](../../../../../../../VeroScript/Resources/Scripts/examples/bindings.sample.json) from the integrated Resources documentation, and save a copy as `bindings.sample.json` in a writable working directory. It demonstrates custom annotation metadata and host types; do not assume a functional SDK archive contains it. Run the following commands from that directory, and replace `app.vs` with your own script.

## Validation, formatting, and loading

The CLI provides catalog-maintenance commands:

```powershell
veroscript bindings validate bindings.sample.json
veroscript bindings format bindings.sample.json
veroscript bindings format bindings.sample.json -o formatted-bindings.json
```

Without `-o` or `--output`, `bindings format` rewrites the input catalog. With an output path it writes that file instead. Standard output contains a write confirmation, not the formatted JSON; use the output file to read the result.

`validate` checks JSON types, required fields, supported enum values, parallel arrays, and numeric ranges. `format` emits JSON in the toolchain's stable format. When formatting to a new file, use a temporary path and review the result before replacing the original.

Load one or more catalogs while checking or running a script with the repeatable `--binding-catalog` option:

```powershell
veroscript --binding-catalog bindings.sample.json app.vs
```

An imported catalog provides compile-time visibility and metadata. Imported function and member descriptions do not replace runtime callbacks; if the host has not registered the matching implementation, the script can still fail at runtime.

The importer also enforces resource limits: `128 MiB` maximum file size, `4,194,304` JSON values, and nesting depth `256`. These limits prevent a catalog from consuming excessive resources during validation.

## Registering bindings in C++

An embedded host normally uses `VeroScriptSession` to register bindings. The public registration boundary includes:

- `RegisterNative` for simple native functions or structured functions with parameter metadata.
- `RegisterHostType` for host classes, structs, and enums.
- `RegisterHostMember` for constructors, methods, static members, properties, and fields.
- `RegisterHostOperator` for operators on host types.
- `RegisterHostTypeClone` for host-type copy behavior.
- `SetHostTypeInheritable` and `SetHostEnumScoped` for type and enum semantics.
- `RegisterHostTypeSubclassFactory` for constructing instances of derivable host types.
- `RegisterGenericHostTypeTemplate` and `InstantiateGenericHostType` for generic host-type templates.
- `GetBindingCatalog` for reading the catalog exported by the current session.

After host registration, export the catalog and run the CLI validator so that documentation, static checking, and runtime registration remain aligned. Static host members are normally accessed as `Type.staticMethod(...)`; host constants use `Type::Constant`. For the broader engine lifecycle and `CheckSource`/`RunSource` or `CheckFile`/`RunFile` usage, see [Host integration](embedding.md).

## Nullable host-object arguments

A static host member whose explicit parameter is a `HostObject` may receive
script `nullptr`. The callback receives an empty host-object handle and must
check it before dereferencing the payload. A null instance receiver is still
invalid; this rule applies to the explicit argument of a nullable static host
call.

## Compatibility principles

A binding catalog is part of the public interface. Adding a function or member is normally a compatible extension; removing an entry, changing a parameter type or reference flag, changing an enum value, changing a host type name, or changing member visibility can break scripts. When upgrading a catalog or host bindings, run catalog validation, script checks, and the target project's integration tests together.

## Host overloads

<a id="host-overload-resolution"></a>

Ordinary host instance/static methods and native free functions support overloads by canonical parameter types and reference qualifiers. Registration rejects duplicate parameter signatures and return-type-only differences. Calls use argument types, numeric conversions and variant fallback rules; equally ranked candidates are diagnosed as ambiguous. Named host members carry stable signature-derived native identities. Constructors and operators keep their existing protocols. Name-only `FindFunction` returns null for an overload set; enumerate `GetFunctions()` to inspect each declaration and its own documentation. Binding catalog export/import preserves every overload. Existing single-signature registrations use the same public APIs.

Omitting `ref` preserves native value-input/handle behavior; replacing the caller’s storage still requires explicit `ref`. A value overload wins over a compatible implicit reference input. Explicit references preserve storage type and mutability; `const ref` accepts const variables. Compilation extensions retain every overload. VM, Mixed, JIT and Core AOT share the core selection. Standalone C++ source export keeps its separate C-linkage bridge contract and reports unsupported named host overloads with a Core AOT hint.

## Native callback metadata

IScriptCallInvoker::GetActiveModule() exposes borrowed, read-only semantic metadata during the current native callback. An execution host may return nullptr when unavailable. Do not keep that pointer, the borrowed invoker or references into the metadata after the callback returns; a retained value or program owner does not extend this callback borrowing contract.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](bindings.md) · [fr-FR](../fr-FR/bindings.md) · [ja-JP](../ja-JP/bindings.md) · [ko-KR](../ko-KR/bindings.md) · [zh-CN](../zh-CN/bindings.md) · [zh-TW](../zh-TW/bindings.md)
