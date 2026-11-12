# Script mapping

`json::converter<T>` converts script objects in an active VM and generates input/output Schema. Register the native extension, then pass its bindings, processors, dependencies and Owners into the module environment before compiling. The project owner declares the `json` provider dependency. A catalog alone cannot install the compile processor. Native C++ mapping is independent.

`write(ref value)` takes const ref explicitly. `write_with_options/read_with_options/try_write/try_read` and their options forms share the contract: ordinary failure returns false with json::error and unchanged output; success clears error. Cancellation, timeout and unrecoverable VM failures still terminate execution.

Scalar bool/integer/finite-float/string values retain JSON types and checked ranges without integer-to-double conversion. Enums use member names, optionally JsonName. Arrays/deques/queues/stacks/priority queues use values() order, stacks bottom-to-top. Sets reject duplicates; multisets retain multiplicity. String-key map/ordered_map use objects; other maps and all multimaps require key_value_pairs arrays. Public struct/class instance fields are mapped, classes may be null, and json::value is unchanged. Hash-container order follows its contract; use ordered containers for insertion order. Special host/function types need explicit converters.

Annotations: JsonName(value) renames fields/enum members; JsonIgnore skips input/output; JsonRequired requires presence, with nullability decided separately; JsonSerializeOnly rejects input; JsonDeserializeOnly omits output; JsonProperty(name,setter) adds an explicit parameterless const getter (no setter means output-only); JsonFactory(type) uses create(value,context); JsonConverter(type) selects an adapter. Accessible inherited public fields are included, static/inaccessible fields excluded, const fields output-only. Missing optional fields retain normal constructed defaults. Duplicate names and conflicting rules fail. Unknown fields fail unless set_ignore_unknown(true).

mapping_options exposes ignore_unknown/numeric_enums/key_value_pairs/exact_floating and setters plus set_limits(depth,nodes): depth 1–256, nodes positive. Each value-copy configuration creates an independent conversion context. instance_pointer uses JSON Pointer escaping. Class cycles fail; shared references in separate branches repeat values. Decode commits a newly constructed value only on success; factories/properties/converters can still change external state.

Adapters implement static encode(const Target&,json::value&,mapping_context), decode(json::value,Target&,mapping_context), schema(mapping_context). custom_converter<Target,Adapter> also requires create(value,context) -> Target. Non-default structs need a factory/create; classes may start with null. Use context.fail/fail_range/adopt(error); false without a diagnostic becomes Callback. Nested converter::encode/decode/schema_nested shares context. Custom Schema must describe the actual conversion.

schema() returns input Schema; schema_with_options(options,false) returns output Schema. Required/ignored/unknown/forbidden fields, integer ranges, elements, enums and nullability follow the selected direction. Recursive types use $defs without running constructors. schema_nested caches by type AND direction; switching set_schema_input cannot reuse the wrong definitions.

Automatic mappings target types declared directly in a module/namespace. Local/nested data types do not receive module helpers; JsonFactory/JsonConverter there report the scope restriction. Use custom_converter in a legal visible scope. JsonPolymorphic instead generates static methods in a nontemplate table class, which may be local/nested. Generated declarations obey ownership, visibility, construction and access rules. Resolve fields, aliases, bases and adapters in their declaring module; follow direct imports/re-exports, template arity/defaults and module precedence. Preserve type/value template parameters, including integer/bool/enum values and qualified constexpr expressions. Distinct closed types have distinct Schema caches; static constexpr fields remain excluded.

Libraries built with the JSON processor retain generated conversion methods. Ordinary .vslib libraries can receive consumer mappings for accessible public fields, subject to construction/access rules. All generation remains subject to processor operations, source bytes and work budgets. Standalone AOT and other hosts must declare their actual supported conversion path; VM coverage is not a cross-platform guarantee.

```cpp
Description.Bindings = Registry.GetBindings();
Description.CompileProcessing.Registry = Registry.GetProcessors();
Description.AvailableDependencies = Registry.GetDependencies();
Description.Owners = Registry.GetOwners();
```

```VeroScript
struct Record
{
public:
	@JsonName(value="record_id") @JsonRequired int64 id = 7;
	string name = "default";
	@JsonIgnore int transient = 0;
};

int main()
{
	Record source = Record();
	json::value document = json::converter<Record>::write(ref source);
	Record result = json::converter<Record>::read(document);
	json::schema shape = json::compile_schema(json::converter<Record>::schema());
	assert(result.id == 7 && shape.is_valid(document));
	return 0;
}
```

```cpp
static bool encode(const Target& input, json::value& output, json::mapping_context context);
static bool decode(json::value input, Target& output, json::mapping_context context);
static json::value schema(json::mapping_context context);
```

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](script-mapping.md) · [fr-FR](../../fr-FR/Reference/script-mapping.md) · [ja-JP](../../ja-JP/Reference/script-mapping.md) · [ko-KR](../../ko-KR/Reference/script-mapping.md) · [zh-CN](../../zh-CN/Reference/script-mapping.md) · [zh-TW](../../zh-TW/Reference/script-mapping.md)
