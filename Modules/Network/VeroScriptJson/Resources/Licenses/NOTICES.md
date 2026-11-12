# VeroScriptJson dependency notices

VeroScriptJson uses the following private dependencies for JSON Schema support.
Its public headers do not expose their types.

| Dependency | Version | Original license |
| --- | --- | --- |
| jsoncons | 1.10.0 | Boost Software License 1.0; original embedded notices remain in the headers |
| QuickJS-ng libregexp/libunicode | 0.17.0 | MIT; original embedded notices remain in the source files |
| python-idna tables and adapted validation rules | 3.12, Unicode 17.0.0 | BSD-3-Clause; complete notice retained under `ThirdParty/idna/LICENSE` |
| Unicode character data | 17.0.0 | Unicode-3.0; complete notice retained under `ThirdParty/idna/UNICODE_LICENSE.txt` |

Only the standalone regular expression and Unicode libraries from QuickJS-ng
are built. The JavaScript interpreter and runtime are not included.

IDNA validation and nontransitional UTS #46 processing use private generated C++
tables, without a Python runtime dependency. The offline generator verifies
the pinned archive and Unicode-data hashes before producing either header.

Pinned source URLs, archive checksums and file selections are recorded in
`ThirdParty/sources.json`. The jsoncons adapter patch is recorded at
`ThirdParty/patches/jsoncons-schema-adapter.patch`. It adds private exact decimal,
equality, work and regular expression hooks, fixes subschema locations, and
records schema resources in dynamic scope when a reference skips the resource root.
It also keeps query strings in resource identity and permits URI allocation
failures to propagate. Bounded adapter values use allocation-free iterative
destruction instead of allocating scratch storage from a `noexcept` destructor.

Installed SDKs retain the project license, this notice, and the original complete
dependency licenses under `licenses/VeroScriptJson/`. The source distribution
retains the original license files with each dependency.
