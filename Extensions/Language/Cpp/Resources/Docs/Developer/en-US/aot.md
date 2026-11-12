# C++ AOT emitter

`VeroScriptCppAot` emits C++ from a compile-checked VeroScript program. The
public API supports source strings, files, programs, a single generated source
string, and `FCppAotSourceBundle` results containing relative files and a JSON
manifest.

## Bundle invariant

The generated bundle intentionally uses one C++ translation unit plus generated
`.inl` fragments. Splitting textual output into independently linked modules
would change template instantiation, internal linkage, and global initialization
ordering. The bundle layout is therefore an output contract, not an optional
formatting choice.

## Eligibility and runtime support

The emitter validates the entry module and accepts only the supported `main`
shapes: `void main()`, `int main()`, or the corresponding `string[] args`
forms. It emits compatibility support only for features used by the program,
including values, containers, channels, and worker tasks. AOT worker globals and
static fields are isolated per native thread; channels are the explicit
synchronized sharing capability.

String-named task entries, cooperative `std::concurrent::cancel_task()` / `std::concurrent::cancel_requested()`, and the
`std::filesystem::is_file()`, `std::filesystem::is_directory()`, and `std::filesystem::remove_dir()` filesystem operations are
supported. Callable task entries and executable `field_info<T>` /
`method_info<T>` reflection are rejected with an AOT diagnostic.

The emitter must reject a program when lowering would alter VeroScript numeric,
ownership, exception, destruction, module, or concurrency semantics. It must not
return partial source after a failed compile, eligibility check, lowering step,
or output write.

Generated `array<bool>` uses one addressable boolean slot per element so indexing
and reference arguments can provide a real `bool&`. Its element storage is not
bit-packed and uses more memory than `std::vector<bool>` at equal capacity.

## Maintenance

The public emitter header, emitter implementation, generated-source tests, bundle
tests, and the generated-file validation path are authoritative. Any new
runtime helper must be added only when a test demonstrates the corresponding
VeroScript behavior and the helper remains self-contained in the generated
bundle.

See [C++ AOT — User pair templates](../../User/en-US/aot.md#cleanup-container-operations).

Keep standalone decimal conversion aligned with `Shared/Private/VeroScriptFloatParsing.h`. The emitter’s `VeroScriptFloatParsingSource.h`, `MakeRuntimeSupport` and generated `VsFloatParsingDetail` form the self-contained output path. When moving the shared input, update its CMake read path and configure dependency together; generated consumers must not need a private source-tree header. Validate standalone target-width rounding and locale/range behavior separately from Core AOT execution.

<a id="standalone-file-paths"></a>

Generated `VsPathFromUtf8` remains self-contained. On Windows both the char8_t and C++17 u8path branches construct a path, make separators native, and add an extended prefix only to absolute paths of at least 248 native UTF-16 units. Complete UNC uses the UNC extended form; an existing extended prefix is retained and relative paths are not made absolute. Keep embedded-NUL rejection before conversion. Update file-I/O generation and regression coverage together for Unicode, long absolute/UNC paths, relative paths and both language-standard branches; do not require a private source header in emitted consumers.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](aot.md) · [fr-FR](../fr-FR/aot.md) · [ja-JP](../ja-JP/aot.md) · [ko-KR](../ko-KR/aot.md) · [zh-CN](../zh-CN/aot.md) · [zh-TW](../zh-TW/aot.md)
