# JSON reference

Script value captures follow Core's readonly binding rules. To call a mutating native method on a captured host value, use a `mutable` lambda or a reference capture of a writable binding. Initialization captures and local statics use ordinary Core semantics.

`VeroScriptJson.h` supplies an independent C++17 data API linked as `VeroScript::Json`; no VM is required. Install the matching `vs-json` native package before compiling scripts using `json`. Script object conversion additionally requires the module compile processor; importing a binding catalog alone does not install it.

Values are immutable shared snapshots with six JSON kinds, original numeric text, exact decimal comparison and checked conversion. Mutating a wrapper commits only on success. Readers may share snapshots; synchronize mutation of the same wrapper or builder. `Try*` clears the error on success and preserves output on failure; constructors/factories can still throw allocation failures. Error text may be empty after allocation failure: inspect `FError::Code` and allocation-free `Id()`. Work callback exceptions report Callback; sink exceptions report Sink, or Allocation for allocation failure. Sizes/indices are `int64`. Default and hard DOM depth limits are 256. `as_double` may round; exact conversion must be requested.

Use `FBuilder` for bulk containers instead of repeated snapshot copies. Copies share builder state; `TryFinish` closes it and `TryReset` reuses it. Pointer (`FPointer`, RFC 6901), Patch (RFC 6902) and Merge Patch (RFC 7396) preserve exact values; root removal is represented by `FDocument::bHasValue`.

`FReader::Feed` reports consumed bytes; retain and resubmit the suffix. Read each EventReady event, mark the final chunk and continue to Done. A nonfinal `1` followed by `2 ` is the number `12`. `Visit` dispatches synchronously; false pauses after the current event without repeating it, exceptions enter Failed until reset. Visitor/work contexts are borrowed for the call only. `FWriter::Write` reports `bAccepted`; once accepted, call Resume rather than resubmit. `0 + NeedOutput` is backpressure; `0 + Progress` is an error. Finish may pause and is repeatable after completion. Streaming preserves input order; use DOM TryWrite for sorted keys.

Script options are value copies: `json::limits`, `parse_options`, `write_options`; update a returned limits copy with `set_limits`. Limits expose `input_bytes/output_bytes/token_bytes/nodes/depth/number_bytes/exponent/work_units` and setters. Counts are nonnegative `int64`; a higher depth does not bypass 256. Duplicate policies are 0 Reject, 1 KeepFirst, 2 KeepLast, 3 ReportAll; ReportAll is reader/SAX only. Parsing supports `allow_leading_bom`; writing supports `pretty/sort_keys/indent` (0–16). Use `parse_with_options`, `try_parse_with_options`, `write_with_options`, `make_reader_with_options`, `make_writer_with_options`.

Nonthrowing functions include `try_parse`, `try_write`, `try_write_with_options`, `try_number`, `try_from_string`, `try_from_double`, `try_compile_pointer`, `try_as_int64/uint64/double/double_exact/bool/string`, `try_resolve`, `try_patch`, `try_merge_patch`. Result references precede the final error reference; options variants follow the documented public declarations. Ordinary data errors return false, preserve results and publish `json::error`; cancellation or inability to deliver wrappers still fails the native call. Error accessors are `code/message/message_id/has_source/byte_offset/line/column/instance_pointer/patch_operation`.

`array_builder/object_builder` expose `push/set/reserve/size/capacity/finish`, `reset_array/reset_object` and matching try forms. Aliases observe the same state; finish returns an independent DOM. For `try_finish/size/capacity`, the result is the first reference and error the last. DOM also has `try_contains/try_keys`. Script reader states: 0 NeedInput, 1 EventReady, 2 Done, 3 Failed; `event()` owns its data. Writer states: 0 Active, 1 NeedOutput, 2 Done, 3 Failed; `write(event)` reports acceptance, `take()` drains at most 64 KiB, then `resume()` continues. Check `error()` after failure and reset explicitly.

Schema uses Draft 2020-12, exact numeric constraints, dynamic references and unevaluated combinations. Pattern matching uses Unicode ECMAScript. Format and content metadata are annotations by default, with no implicit decoding. `schema_options` supplies retrieval URI, limits and resource count; empty retrieval uses `urn:veroscript:json-schema:root`. `compile_schema_with_options(schema, options, resources)` takes a URI-to-Schema object. C++ may borrow a synchronous resolver during compilation; compiled schemas own resolved resources. No references are downloaded and cwd does not resolve them. The standard meta-Schema and eight vocabulary resources are built in.

Compilation/validation check cancellation, work, nodes, text and depth. URI is at most 64 KiB, regex pattern 4096 bytes and one regex execution’s retained allocations 16 MiB; host stack/step protections can also report Limit. Reports contain 1–16384 issues including nested errors; truncation never means success. `schema_report` exposes `valid/truncated/size/issue`, with JSON `details`. Use the topic pages for report/format options.

Additional checked calls are `try_compile_schema`, `try_validate_schema`, `try_compile_schema_with_options(value, options, resources, ref schema, ref error)`, `try_validate_schema_with_limits(schema, value, max_issues, limits, ref report, ref error)`, `try_compile_pointer_with_limits`, `pointer.try_resolve[_with_limits]` and `document.try_get`. Missing document roots preserve the output and return false. Compiled pointers recheck current token/depth/work limits when used.

- [C++ mapping](native-mapping.md)
- [Script mapping](script-mapping.md)
- [Closed polymorphic mapping](polymorphic-mapping.md)
- [Schema annotation reports](schema-annotations.md)
- [Schema formats and dialects](schema-formats.md)
- [Diagnostics and localization](diagnostics.md)

```VeroScript
json::schema schema = json::compile_schema(json::parse("{\"type\":\"integer\",\"minimum\":0}"));
bool valid = schema.is_valid(json::from_number("1.0"));
json::schema_report report = schema.validate(json::from_string("bad"));
print(report.issue(0).get("keyword").as_string());
```

```cmake
find_package(VeroScriptJson 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Json)
```

`FSchemaOptions::MaxResources = 256`

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](README.md) · [fr-FR](../../fr-FR/Reference/README.md) · [ja-JP](../../ja-JP/Reference/README.md) · [ko-KR](../../ko-KR/Reference/README.md) · [zh-CN](../../zh-CN/Reference/README.md) · [zh-TW](../../zh-TW/Reference/README.md)
