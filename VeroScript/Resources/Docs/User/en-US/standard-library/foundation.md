# Standard library foundation

<a id="foundation-imports"></a>

Import `core.vs` or the individual modules for strings, I/O, paths, numeric aggregates and concurrency. `std::io::read_*` requires `io.vs`; environment, clock and console functions remain runtime-provided. Qualified names do not replace imports.

Parsing accepts fixed-width primitive integers, float, double and bool, including aliases. Trim outer ASCII whitespace and match the whole string. Only one sign is allowed; unsigned values reject minus, including -0. An explicit base (integers only, 2..36) does not strip prefixes. Decimal floats reject NaN, Infinity, hexadecimal text and out-of-range values. Bool accepts only true/false. Normal parse failure makes try_parse return false without changing its output. Invalid arguments, limits and cancellation are errors.

String positions remain UTF-8 byte offsets. replace uses non-overlapping matches; an empty pattern returns the input. split preserves empty parts. trim and case conversion use ASCII rules. UTF-8 validation rejects malformed encodings; encode/decode preserve NUL, BOM and newlines and perform no replacement. region below means an actual overload for buffer, view or readonly_view.

<a id="io-contract"></a>

Only normal EOF produces an empty optional. A blank line is an engaged empty string; an unterminated last line is still returned. LF and one trailing CR are removed. MaxTextBytes counts the remaining line content; an interior CR still counts. Token reads leave the terminating delimiter. Legacy read_line returns an empty string at EOF; strict token reads report EOF as an error. read_bytes with an explicit maximum rejects overflow, rather than truncating; zero allows only an empty file. Byte I/O uses checked memory leases. File writes may already have changed the file when an error occurs; flush is not a durability guarantee.

Path queries are lexical and do not access the filesystem. native uses the host syntax; posix/windows select portable explicit syntax. C:a and /a are not absolute Windows paths; complete UNC paths are absolute. Windows output uses /. Reject NUL, incomplete UNC and device/verbatim prefixes. Normalization does not prove sandbox containment. sum_as converts each element to R before checked addition. sum_as rejects empty arrays; accumulate_as returns its initial value. R must be an explicit non-bool primitive numeric type. clamp infers one common type, rejects mixed implicit types, reversed or NaN bounds, and preserves a NaN input.

Synchronization factories create private channel roles. Generic send/receive/capacity changes reject them; close and read-only queries remain available. Mutexes are non-recursive and owned by the logical executor; foreign unlock is an error, self try/timed lock returns false, and owner exit closes held locks. Semaphores store a permit count and reject excess release. Synchronized load/store/exchange/fetch_add use prepared snapshots and commits; failed arithmetic does not empty the cell. These operations do not promise hardware lock freedom.

Task aliases share Core state. await consumes exactly one result after worker completion; worker failure or missing/extra results is an error, not an endless receive. await_for returns an empty optional only on timeout and keeps the task active. Preparation or limit failure before consumption preserves the result. A group prevalidates null, invalid and duplicate capabilities. Previously consumed tasks stay consumed after partial failure; size reports the remaining range and a failed terminal item is removed from that range. when_all leaves the input array unchanged while consuming task state. cancel_all requests every cancellation before cleanup; workers remain owned until they actually exit. Cancellation cannot force a blocked host call to stop.

Host limits are per operation: MaxTextBytes, MaxReadBytes and MaxSplitParts are uint64; zero adds no limit. Platform and VM limits still apply. Workers inherit this configuration. Chunked native work uses the current execution budget and cancellation. reverse/rotate/shuffle preserve the element multiset on interruption; observer paths prepare a private reorder and publish once without invoking clone. Shuffle rejects an array used as its own random engine and does not roll back consumed randomness. Standalone AOT explicitly rejects operations requiring these Core services before producing output; unused standard-library imports remain allowed.

See [String module — `format` fields](string.md#integer-formatting).

`setenv(name, "")` stores an existing empty value. Use unsetenv to delete; deleting an absent variable succeeds. try_getenv distinguishes absent from empty. Names must be nonempty, without = or NUL; values must not contain NUL. Windows environment names retain platform case-insensitive semantics; POSIX names remain case-sensitive.

## API

```text
// string.vs
T std::strings::parse<T>(string text);
T std::strings::parse<T>(string text, int base);
bool std::strings::try_parse<T>(string text, ref T output);
bool std::strings::try_parse<T>(string text, ref T output, int base);
std::memory::buffer std::strings::encode_utf8(string text);
string std::strings::decode_utf8(region bytes);
bool std::strings::is_valid_utf8(string text);
bool std::strings::is_valid_utf8(region bytes);
// io.vs
optional<string> std::io::try_read_line();
optional<string> std::io::try_read_token();
std::memory::buffer std::io::read_bytes(string path);
std::memory::buffer std::io::read_bytes(string path, int64 max_bytes);
void std::io::write_bytes(string path, region source);
void std::io::append_bytes(string path, region source);
// path.vs: each query has both one- and two-argument overloads
bool std::filesystem::is_absolute(string path);
bool std::filesystem::is_absolute(string path, std::filesystem::path_style style);
string std::filesystem::root_path(string path);
string std::filesystem::root_path(string path, std::filesystem::path_style style);
string std::filesystem::lexically_normal(string path);
string std::filesystem::lexically_normal(string path, std::filesystem::path_style style);
// path_style: native, posix, windows
// numeric.vs / math.vs: R explicit, T inferred
R std::numeric::accumulate_as<R, T>(array<T> values, R initial);
R std::numeric::sum_as<R, T>(array<T> values);
T std::clamp<T>(T value, T low, T high);
// runtime-provided
optional<string> std::environment::try_getenv(string name);
void std::environment::unsetenv(string name);
```

## SDK

```text
C++: FEngineConfig.StandardLibraryLimits;
     VeroScriptSession::SetStandardLibraryLimits(FStandardLibraryLimits);
     VeroScriptVm::SetStandardLibraryLimits(FStandardLibraryLimits);
C:   vs_interop_session_set_stdlib_limits(session, &limits);
     vs_interop_stdlib_limits { struct_size, reserved=0, max_text_bytes,
                                max_read_bytes, max_split_parts };
C#:  VeroScriptSession.SetStandardLibraryLimits(VeroScriptStandardLibraryLimits);
Python: Session.set_stdlib_limits(max_text_bytes=0, max_read_bytes=0, max_split_parts=0);
WebAssembly: runtime.setStandardLibraryLimits(maxTextBytes, maxReadBytes, maxSplitParts);
```

See [Concurrency module — Task cancellation requests](concurrent.md#concurrency-aot-boundary).

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](foundation.md) · [fr-FR](../../fr-FR/standard-library/foundation.md) · [ja-JP](../../ja-JP/standard-library/foundation.md) · [ko-KR](../../ko-KR/standard-library/foundation.md) · [zh-CN](../../zh-CN/standard-library/foundation.md) · [zh-TW](../../zh-TW/standard-library/foundation.md)
