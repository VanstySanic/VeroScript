# JSON maintenance

The data layer owns DOM/text/stream semantics. Schema uses jsoncons 1.10.0 with the recorded private patch and the standalone QuickJS-ng 0.17.0 regex/Unicode library, without a JavaScript runtime. Preserve exact numeric adapters. Native C++ mapping stays independent of Core; script mapping belongs to the module processor. Small plain-struct modules use reduced generated support; imports, generics, inheritance, annotations, executable members or complex defaults require the full support set. Preserve declaration order, owner scopes and closed-type identities.

Build the data layer from the module CMake entry. Root integration uses VEROSCRIPT_BUILD_JSON; VEROSCRIPT_JSON_BUILD_BINDINGS controls static registration/dynamic extension builds. An installed matching Core SDK can supply bindings without rebuilding Core. Consumers use VeroScript::Json, and COMPONENTS Bindings adds VeroScript::JsonBindings. The Bindings install component supplements the data component and its licenses; dynamic modules install under lib/veroscript/modules. Register code with an implementation identity, not product version alone.

See [Network: Source ownership — Build and native module registration](../../../../../VeroScriptNetwork/Resources/Docs/Developer/en-US/index.md#network-execution-assembly).

When execution is permitted, run native data, static/dynamic registration, source-module and binary-library consumers separately. VEROSCRIPT_JSON_SCHEMA_CORPUS selects the pinned JSON Schema Test Suite, including format assertions; unknown-format cases use annotation mode. VEROSCRIPT_JSON_NORMALIZATION_CORPUS selects Unicode 17 NormalizationTest.txt; VEROSCRIPT_JSON_CORPUS selects JSONTestSuite. Fetch only through the explicit Resources/Tools/Tests helpers; normal builds do not download corpora. Record actual corpus/source/toolchain identities and real results. Old pass counts do not certify current code, JIT/AOT, hosts, mobile or browser targets.

Maintain six json.json catalogs, stable IDs/argument counts and installed share/VeroScriptJson/Resources/Localization. The json staging profile replaces the destination profile rather than cumulatively overlaying it. Generate Unicode tables with Resources/Tools/Unicode/generate_schema_unicode.py from pinned inputs; no Python runtime becomes a product dependency. Preserve original notices and exact source inventories.

```sh
cmake -S Modules/Network/VeroScriptJson -B build/json -DCMAKE_BUILD_TYPE=Release
cmake --build build/json --config Release
ctest --test-dir build/json -C Release --output-on-failure
cmake --install build/json --config Release --prefix /absolute/sdk
```

[User](../../User/en-US/index.md) · [Standards](../../../../../../../Resources/Docs/Standards/en-US/GENERAL.md)
