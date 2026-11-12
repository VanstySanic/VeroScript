# Python — Mobile SDK

Materialize header subdirectories: directory symlinks and Windows junction/reparse directories below `includeDirectory` are rejected. `includeDirectory` itself may resolve to a directory inside the SDK. Linked `.h` files are allowed when their targets remain inside the SDK; their contents are included in the SDK fingerprint.

This page is for maintainers building and publishing Python mobile packages with `Extensions/Language/Python/Resources/Tools/Mobile`. Supply the target CPython SDK explicitly; a host interpreter or desktop/macOS binary does not substitute for target assets. Build in a new directory, validate the complete inventory and collect target-platform acceptance evidence before stable publication.

Python: use host CPython matching the target major/minor, CMake 3.25+, Android NDK/Ninja or macOS/full Xcode. Target release/GIL CPython 3.13/3.14; one package per minor, identical full version across all slices. Reject debug/free-threaded builds. SDK descriptor paths are relative to its directory and cannot escape it. Fields: `schemaVersion`, `platform`, `architecture`, `variant`, `minimumVersion`, `includeDirectory`, `library`, `sysconfigData`. The inspector reads target headers and literal `build_time_vars` through AST; it never runs the target interpreter. Validate platform, architecture, minimum OS, Python ABI and iOS framework install name. Android needs arm64-v8a/x86_64 (API 26); iOS needs arm64 device and arm64/x86_64 simulator (15.0).

Python packages own the extension plus facade/resources; the application owns CPython/dependencies. Android requires extracted JNI paths; iOS uses signed dynamic Python extension frameworks and preserved `.fwork`/`.origin`. This is separate from the static C ABI XCFramework and from prohibited native JIT. Each slice has a fresh build nonce and native identity. `python-mobile.json` records exact file hashes, versions, platform/ABI, SDK fingerprint and pending device status.

Run inventory/native audits before publication. `device_smoke.py` rejects desktop execution and invalidates stale proofs before every retry; evidence must match build ID, exact Python/product versions, ABI, architecture and device/simulator variant. Every slice needs evidence; Android arm64 needs both 4 KB and 16 KB page runs. The governed VM smoke checks imports, callbacks, diagnostics, retained values and disposal. Validate final signed applications, dependency alignment, APK ZIP alignment, iOS framework runpaths/resources/signing, cancellation and application tests separately. Host-only tests and source declarations do not complete these gates.

## Production reference

The mobile build and deployment path is implemented in
[`Resources/Tools/Mobile/`](https://github.com/VanstySanic/VeroScript/tree/main/Extensions/Language/Python/Resources/Tools/Mobile). This workflow requires cross-building and target acceptance for the exact published artifacts. A successful SDK inventory/native-format audit does not establish application or device readiness; record each platform’s actual build and execution evidence separately.

## Supported build contract

| Target | CPython | Slices | Minimum OS | Native payload |
|---|---|---|---|---|
| Android | 3.13 or 3.14, release build with GIL | arm64-v8a, x86_64 | API 26 | libveroscript_python.so, libc++_shared.so |
| iOS | 3.13 or 3.14, release build with GIL | arm64 device, arm64 and x86_64 simulator | iOS 15.0 | VeroScriptPython.xcframework |

Produce a separate package for each CPython minor version. All slices within a
package must use the same full CPython version. Debug and free-threaded Python
builds are rejected. This is an application embedding payload, separate from
the desktop CPython 3.11–3.15 wheel matrix; `pip install` of a desktop wheel does
not deploy this payload.

Core, C++ session support and Interop are linked statically into the extension.
VM and Mixed execution are available without a native JIT module. The mobile
package installs the `veroscript` runtime facade, typing information, standard
library and localization. Frontend, BindGen, Transpiler and AOT command-line
tools remain host tools and are not installed into the mobile runtime.

## 1. Obtain the build prerequisites

- A normal **host CPython** of the same major/minor version as the target. This
  interpreter runs SDK inspection and build scripts. It is not copied into the
  application. The target interpreter is never executed during configuration.
- CMake 3.25 or newer and the platform toolchain.
- Android: NDK and Ninja, plus a CPython SDK for **each** Android ABI. Use an NDK
  whose `libc++_shared.so` supports 16 KB pages (NDK r28 or newer provides this
  by default). The SDK's libpython and the resulting extension must also pass
  the 16 KB LOAD/RELRO checks. The CPython source build instructions use
  Linux/macOS; an already built target SDK can be consumed on Windows.
- iOS: macOS, full Xcode, installed iOS/simulator SDKs and a CPython cross-build
  SDK for each slice. A universal simulator framework must be split into thin
  arm64 and x86_64 binaries for the descriptor inputs, for example with
  `xcrun lipo -thin`. Preserve its `@rpath/Python.framework/Python` install name.

Build or obtain CPython using the upstream instructions:

- [CPython Android build instructions](https://github.com/python/cpython/blob/3.14/Android/README.md)
- [CPython iOS build instructions](https://github.com/python/cpython/blob/3.14/Apple/iOS/README.md)
- [Embedding Python on Android](https://docs.python.org/3.14/using/android.html)
- [Embedding Python on iOS](https://docs.python.org/3.14/using/ios.html)
- [Android 16 KB page support](https://developer.android.com/guide/practices/page-sizes)

There is no automatic SDK download or license acceptance. The application
owner supplies and redistributes CPython, its standard library, dependent
native libraries and their licenses. The VeroScript package includes its MIT
license, third-party notices and the NDK notice for its redistributed C++ runtime.

## 2. Describe each target SDK

Place a descriptor alongside the SDK root. The paths are **relative to that
descriptor's directory**, must exist and must stay inside that directory.
Point to the target headers and target `_sysconfigdata_*.py`; a desktop
installation's `sysconfig` cannot supply these values.

Android arm64 example (`android-arm64/sdk.json`):

```json
{
	"schemaVersion": 1,
	"platform": "android",
	"architecture": "arm64-v8a",
	"variant": "device",
	"minimumVersion": "26",
	"includeDirectory": "include/python3.14",
	"library": "lib/libpython3.14.so",
	"sysconfigData": "lib/python3.14/_sysconfigdata__android_aarch64-linux-android.py"
}
```

The x86_64 descriptor changes `architecture`, library/header locations and the
sysconfigdata path to the corresponding x86_64 SDK. The exact sysconfigdata
filename varies by SDK; do not rename one ABI's file to describe another ABI.

iOS arm64 device example (`ios-device/sdk.json`):

```json
{
	"schemaVersion": 1,
	"platform": "ios",
	"architecture": "arm64",
	"variant": "device",
	"minimumVersion": "15.0",
	"includeDirectory": "Python.framework/Headers",
	"library": "Python.framework/Python",
	"sysconfigData": "stdlib/_sysconfigdata__ios_arm64-apple-ios.py"
}
```

Simulator descriptors set `variant` to `simulator`, use `arm64` or `x86_64`,
and select that simulator SDK's files. Use the SDK's actual sysconfigdata path;
the framework alone may not contain the required Python standard-library files.

The inspector reads headers and a literal `build_time_vars` dictionary through
AST parsing. It does not import target scripts. It checks release/GIL ABI,
CPython version, native architecture, Android API/alignment or iOS platform and
minimum OS, and the iOS Python framework's install name. It derives SDK and
source fingerprints for the native build identity.

```text
python Extensions/Language/Python/Resources/Tools/Mobile/mobile_sdk.py --sdk <sdk.json> --check-host
```

## 3. Build the complete platform package

Run from the repository root, with the selected host CPython. Output must be a
new directory. The driver builds each slice in temporary directories, audits
the output, then moves the complete result into place. It does not overwrite
an existing package or generate mobile wheels.

Android:

```text
python Extensions/Language/Python/Resources/Tools/Mobile/package_mobile.py android --sdk <android-arm64/sdk.json> --sdk <android-x86_64/sdk.json> --ndk <ndk-directory> --out-root <new-output-directory>
```

iOS (on macOS):

```text
python Extensions/Language/Python/Resources/Tools/Mobile/package_mobile.py ios --sdk <ios-device/sdk.json> --sdk <ios-simulator-arm64/sdk.json> --sdk <ios-simulator-x86_64/sdk.json> --out-root <new-output-directory>
```

`--host-python <executable>` selects the host interpreter; `--cmake <executable>`
selects CMake. Add `--plan` to inspect SDKs and print configure/build/install
commands without building. Android plans also require `--ndk`. Neither mode
can validate a nonexistent SDK descriptor.

Each record in `python-mobile.json` identifies its platform, variant, ABI,
Python/product versions, minimum OS, SDK fingerprint and native build identity. Its file
inventory covers every published file. Device validation is recorded as
`pending`; matching evidence is supplied separately at release acceptance.
The product version comes from `VeroScriptVersion.h`. Each new build directory
uses a new build nonce so that evidence from an earlier toolchain build cannot
approve a newly produced artifact merely because its sources/SDK are unchanged.

## 4. Deploy into the embedding application

### Android

For each ABI, copy the slice's `jniLibs/<abi>/` into the Android app's
`src/main/jniLibs/<abi>/`. Also deploy the target SDK's `libpython3.X.so` and its
native dependencies using the SDK supplier's integration instructions. Resolve
any `libc++_shared.so` duplication by using one compatible runtime across the
app's native libraries; do not hide an incompatible duplicate with `pickFirst`.

Copy one matching slice's `python/veroscript/` into the app's Python resources.
Extract it to an app-private directory if those resources are initially APK
assets. Add its containing `python` directory to the embedded interpreter's
module search path. Preserve `stdlib/`, `localization/` and `py.typed` alongside
the facade, even if they are not all used by a particular script.

The extension loader requires a real path to an extracted native library. Set
the Android Gradle Plugin's JNI packaging option on the app module:

```kotlin
android {
    defaultConfig { minSdk = 26 }
    packaging { jniLibs { useLegacyPackaging = true } }
}
```

This controls native extraction; it does not configure the CPython embedding
SDK. Confirm that `ApplicationInfo.nativeLibraryDir` contains the libraries in
the installed application. Before the first `import veroscript`, pass that
absolute directory to the embedded interpreter and configure:

```python
import os
import sys

os.environ["VEROSCRIPT_ANDROID_NATIVE_LIB_DIR"] = native_library_dir
sys.path.insert(0, application_python_directory)
import veroscript
```

`native_library_dir` comes from `context.applicationInfo.nativeLibraryDir`, not
from a build-machine path. `application_python_directory` is the deployed
directory containing `veroscript/`. The private loader loads
`libveroscript_python.so` with the module name `veroscript._veroscript`, preserving
the Python initializer contract. Run acceptance using the installed APK so this
native library path and dependency resolution are exercised.

### iOS

Add both the external **Python.xcframework** and the generated
**VeroScriptPython.xcframework** to the app target with **Embed & Sign**. Xcode
selects the device framework or the universal arm64/x86_64 simulator framework.
Configure the app's framework runpath search paths to include
`@executable_path/Frameworks`. The generated extension is unsigned; the
application's normal signing/embed phase signs the selected framework.

Copy the corresponding slice's `python/veroscript/` to the app bundle's
`python/veroscript/`. The required final paths are:

```text
Application.app/
  Frameworks/Python.framework/Python
  Frameworks/veroscript._veroscript.framework/veroscript._veroscript
  Frameworks/veroscript._veroscript.framework/veroscript._veroscript.origin
  python/veroscript/__init__.py
  python/veroscript/_veroscript.fwork
  python/veroscript/stdlib/...
  python/veroscript/localization/...
```

Configure CPython embedding and its standard library using the upstream iOS
instructions. Add `<app-bundle>/python` to the interpreter's module search path
and disable bytecode writes into the read-only bundle. The SDK's CPython
`AppleFrameworkLoader` reads `_veroscript.fwork` and loads the framework from the
app's `Frameworks` directory. The generic marker and `.origin` are shared by
both simulator architectures. Do not move these resources to `app_packages`
without updating the origin path. Do not copy `.so` binaries into the resource
directory or reprocess these already prepared markers as native extensions.

## 5. Future acceptance and release

The package driver runs an inventory/native audit, without claiming device
acceptance. It checks ELF ABI, CPython initializer/dependency, Android API and
16 KB alignment; iOS framework platform/minimum OS, initializer/dependencies,
markers and Info.plist; and XCFramework members against the original slices.
It also compares the entire Python facade, standard library and every language
catalog with the matching source release. Package directories must match the
declared slice matrix and XCFramework libraries. Additional native payloads,
including binaries hidden inside framework resources, are rejected even if
their hashes have been added to the inventory. Run the auditor from that source
checkout, or the `_native` root of its unpacked source distribution; a package
inventory alone cannot establish that all required resources were included.

```text
python Extensions/Language/Python/Resources/Tools/Mobile/verify_mobile.py <package-directory>
```

Copy `device_smoke.py` into the application test harness. Call it inside the
actual embedded mobile interpreter, with an app-writable report path:

```python
from device_smoke import run
run(writable_report_path)
```

It checks VM and Mixed execution, initialization, calls, output callbacks,
exception diagnostics and a retained callable. Sessions explicitly allow only
captured console output, with 100,000 instructions, a 5-second timeout, a
256-frame call-depth limit and cooperative cancellation enabled. Both requested
policies therefore execute through the governed VM. Run the harness on an
application worker thread; host callbacks remain responsible for their own
access policy. Checks remain active under
Python optimization. A desktop run is rejected. Every retry first invalidates
the previous report, so failures cannot leave a stale success. Export the JSON reports and
require evidence for every slice; Android arm64 additionally requires successful
runs on **both 4 KB and 16 KB page environments**:

```text
python Extensions/Language/Python/Resources/Tools/Mobile/verify_mobile.py <package-directory> --require-device-tests --evidence <report-1.json> --evidence <report-2.json> --evidence <report-3.json>
```

Each proof must match the native build identity, exact target Python version,
product version, platform, architecture, variant and Interop ABI. A new package
build requires new evidence. Run CPython's and the application's own tests
too: this smoke check covers the binding path and does not replace them.
The report derives the process ABI and device/simulator variant from
`veroscript.HOST_PLATFORM`, and uses `veroscript.__version__` for the semantic
product version. Kernel machine names can identify an iPhone model or a host
CPU under translation; `RUNTIME_VERSION` also includes bytecode information.

Before Android publication, inspect **all** application native libraries,
including CPython SDK dependencies, for 16 KB compatibility. Check the final
APK with SDK Build-Tools `zipalign -v -c -P 16 4 <application.apk>` and test the
signed app produced from the AAB distribution path. Before iOS publication,
check the signed archive's embedded frameworks, resource layout, runtime
search paths and launch/import behavior on the device and both simulator ABIs.
Application store policy compliance belongs to the embedding application.

## Host regression coverage

`Extensions/Language/Python/VeroScriptPython/tests/test_mobile.py` validates SDK inspection, host/target
CMake configuration, resource/native audits and device evidence matching.
It also exercises both package producers with simulated tool outputs, including
the universal iOS simulator framework, temporary-directory cleanup after build
failure and preservation of an output directory created during the build.
These tests run without mobile SDKs and do not establish cross-compilation or
application/device acceptance.

[Python](index.md)


## Publication documents

Mobile packages contain functional files, runtime resources and legal notices. They omit README, manuals and examples; obtain the integrated User documentation from the separate Resources distribution. Native resource paths and device acceptance requirements are unchanged.

<a id="android-build-contract"></a>

Android builds explicitly enable `VEROSCRIPT_BUILD_APPLICATION=ON`; preserve `licenses/application/miniz/LICENSE`, `licenses/application/json/LICENSE.MIT` and `licenses/application/sha256/LICENSE` in `python/veroscript/`. Toolchain validation compares the resolved NDK `ANDROID_ABI` / `ANDROID_PLATFORM_LEVEL` when supplied, falling back to CMake’s Android/system fields; the NDK legacy `CMAKE_SYSTEM_VERSION=1` is not the target API. The 16 KB audit checks LOAD alignment and that page-rounded RELRO protection overlaps no writable data outside RELRO; it does not require every RELRO end to be 16 KB aligned. The supported NDK shared STL can have an unaligned RELRO suffix.

<a id="compiler-nesting-acceptance"></a>

## Native compiler stack

The embedding host must compile on a native worker with at least 8 MiB of stack. Android JNI and Apple dispatch threads can have smaller stacks and overflow before the parser returns its bounded nesting diagnostic. Create, use and destroy each Session on that same worker; never move an existing Session or its callbacks implicitly. Configure Python worker stacks before starting application Python threads, or provide the stack in the native host. Run device_smoke.run on that worker. Its four groups now include depth 200, a depth-limit diagnostic at 512 and subsequent session reuse. New evidence must include compilerNestingChecked=true; older evidence without this gate is rejected.

`compilerNestingChecked` is evidence metadata, not a public Session API. `verify_mobile.py::verify_package` requires it to be boolean true alongside schemaVersion 1 and matching build/runtime/ABI/slice/artifact identities; run emits it only after exercise_runtime succeeds. Direct Python Session/C ABI compilation does not use Mobile::FRunner’s stack adapter. Supply an absolute report_file in an app-owned writable directory to the example below; invoke this blocking supervisor outside the UI thread. Mocked checker regressions validate logic, not a target binary.

```python
import threading
from concurrent.futures import ThreadPoolExecutor
import device_smoke

def validate_on_worker(report_file):
	threading.stack_size(8 * 1024 * 1024)
	with ThreadPoolExecutor(max_workers=1) as worker:
		return worker.submit(device_smoke.run, report_file).result()
```

<!-- docs-navigation -->

---

[Developer documentation](../../../../../../../Resources/Docs/Developer/en-US/index.md)

[en-US](mobile.md) · [fr-FR](../fr-FR/mobile.md) · [ja-JP](../ja-JP/mobile.md) · [ko-KR](../ko-KR/mobile.md) · [zh-CN](../zh-CN/mobile.md) · [zh-TW](../zh-TW/mobile.md)
