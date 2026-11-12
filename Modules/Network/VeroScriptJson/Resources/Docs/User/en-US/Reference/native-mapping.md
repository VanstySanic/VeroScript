# C++ mapping

Include `VeroScriptJsonMapping.h` and link `VeroScript::Json`; no Core is required. Register fields/properties explicitly with `TObjectMapping<T>` and call `TryEncode/TryDecode`. Output changes only on success. Decode prepares conversions before normal construction and assignment; a non-default type may supply a `shared_ptr<T>` factory. Constructor/property/converter side effects outside the result are not rolled back.

Field names allow valid UTF-8 and NUL; paths use RFC 6901 escaping. Unknown fields fail unless `bIgnoreUnknownFields`; `FFieldOptions` controls required/ignore/serialize/deserialize. An output-only field in input fails; ignored fields are skipped. Duplicate/conflicting directions fail registration. Add base fields explicitly; static members and properties are not reflected automatically, and only registered Property accessors execute.

Booleans, integers up to 64 bits, finite floats and owned UTF-8 strings preserve their JSON kind. Integers do not pass through double. `bExactFloatingPoint` rejects rounding; otherwise checked float rounding is allowed. vector/list/deque/fixed arrays map to arrays, fixed sizes are checked. set rejects duplicates, multiset retains them; iteration order is the container’s contract. Unique string-key maps default to objects; other maps/multimaps need `bMapAsPairs`. optional/shared_ptr/unique_ptr accept null, ordinary values do not. Registered enums use names unless `bEnumsAsNumbers`, and still reject unregistered values. Unsupported unregistered types fail rather than emit empty objects.

`RegisterConverter<T>` supplies typed encode/decode/Schema callbacks. Recursive `FMappingContext::Encode/Decode/Schema` shares path, budget and cancellation. Each call freezes a registry snapshot; registry copies share registration state, but replacement does not alter an active call. Converter Owner must retain code, destructor and RTTI. Returned and nested mapped shared_ptrs retain the call’s converter set, including copies and aliases, after registry/outer destruction. Moving objects out by value or using another control block requires your own code owner. The first type key’s RTTI Owner remains until the registry/snapshots are gone; callbacks/factories are destroyed before Owners, outside the registry lock, including failed insertion.

Cycles on the active path report `json.cycle`; sharing in separate branches repeats values, without graph identity restoration. `TryMappingSchema<T>(Input/Output, ...)` uses the same fields/directions, `$defs/$ref`, integer ranges, lengths and enums. Missing custom Schema is an error. Construction invariants, custom comparisons, pair-map key uniqueness and exact floating conversion remain decoder checks. `FMappingOptions::Limits` and borrowed FWork bound compact JSON bytes, depth, nodes, text and numbers. Errors preserve the first error/path; cancellation commits no output.

```cpp
#include <VeroScriptJsonMapping.h>
using namespace VeroScript::Json;

struct Settings
{
	std::string Name;
	int Port = 8080;
};
FMappingRegistry registry;
FError error;
FFieldOptions required;
required.bRequired = true;
bool registered = TObjectMapping<Settings>("app::Settings")
	.Field("name", &Settings::Name, required)
	.Field("port", &Settings::Port)
	.Register(registry, error);

FJsonValue input;
std::shared_ptr<Settings> settings;
bool decoded = registered && TryParse("{\"name\":\"Vero\"}", input, error)
	&& TryDecode(input, settings, error, registry);
// Successful decoding preserves the constructed default port, 8080.
```

```cpp
FJsonValue input_schema, output_schema;
bool ok = TryMappingSchema<Settings>(EMappingDirection::Input, input_schema, error, registry)
	&& TryMappingSchema<Settings>(EMappingDirection::Output, output_schema, error, registry);
```

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](native-mapping.md) · [fr-FR](../../fr-FR/Reference/native-mapping.md) · [ja-JP](../../ja-JP/Reference/native-mapping.md) · [ko-KR](../../ko-KR/Reference/native-mapping.md) · [zh-CN](../../zh-CN/Reference/native-mapping.md) · [zh-TW](../../zh-TW/Reference/native-mapping.md)
