# Python toolchain and package contract

## Strict static-tool contract

The frontend reports `PYF0003` when a class repeats a field, enum value, property, or method declaration; only the matching single property setter is allowed. The Transpiler accepts `None` only as a callable return, requires every constructor to declare `-> None`, requires an instance receiver to be a required positional `self`, and accepts only `@staticmethod` as a method decorator.

Before runtime binding, BindGen also validates the complete Binding Catalog and the actual Python callables. It rejects duplicate constructors or visible member names, mismatched field/getter/setter signatures, and constructors, functions, methods, and property accessors whose required positional shape does not exactly match the catalog. These failures do not partially register bindings or replace generated artifacts.

## Native extension

`VeroScriptPython` is a CPython extension, not an embedded Python distribution.
CMake requires a CPython interpreter and `Development.Module` for Python 3.11
through 3.15. The selected implementation must be CPython; another Python
implementation is rejected rather than silently producing an incompatible
extension.

The extension links `VeroScriptInterop`, uses the platform's CPython ABI suffix,
and places the Interop library beside the importable package. The build also
deploys the matching `localization/` and `stdlib/` trees beside the package so
tests and local imports do not require global PATH or library-path changes.

## Tools and components

Frontend, BindGen, Transpiler, and Python AOT packages are installed as Python
tool components. Their CMake targets use the selected interpreter for tests and
install relative site-package paths so a staged prefix remains relocatable.
The runtime wheel carries the native extension, Interop library, Python source,
type information, standard library, and Catalog assets.

## Release verification

Distribution validation checks importability, native-library placement,
version agreement, package contents, wheel/source-distribution behavior, and
the supported interpreter/platform matrix. A package that works only from the
repository source tree is not a valid release artifact.

## Wheel matrix publication

Complete Python business publication derives its interpreter matrix from `Extensions/Language/Python/pyproject.toml`: `requires-python` and the `cibuildwheel` selection must agree, currently covering CPython 3.11–3.15. Prepare all five native interpreters for the target OS and architecture; discovery uses versioned commands and the Windows `py` launcher. Windows full publication accepts `-PythonExecutables`; Unix accepts repeated `--python-executable` arguments. `VEROSCRIPT_PYTHON_EXECUTABLES` is an alternative list, separated by `;` on Windows or `:` on Unix. Missing versions, debug builds, free-threaded interpreters and architecture mismatches fail preflight; interpreters are not installed automatically.

Run the following from the repository root to check the Windows x64 interpreter set without building. Select the target matching the native host; the standalone entry point accepts repeated `--python` arguments for explicit paths.

```text
python Resources/Tools/Packaging/build-veroscript-python-wheels.py --target win-x64 --check-interpreters
```

The pipeline creates one source distribution and builds each wheel from it in a separate source/build directory. Every wheel is installed into a fresh environment with the matching interpreter, then checked by every installed-package suite selected by `Extensions/Language/Python/Resources/Tools/Packaging/test-wheel.py` and `pip check`. Complete publication requires the full wheel ABI set, the source distribution, consistent tags and native identities, and packaged licenses. `pythonWheelVersions` in the business manifest records the matrix for audits without a checkout; partial component publication remains partial. Selecting Python still builds and validates this matrix with `-SkipBuild` or `--skip-build`.

The Python sdist is stored once at `Release/Extensions/Language/Python/any/<version>/packages/`; wheels are selected by their actual platform/ABI tags and may share a manifest only when those tags justify it. Target indexes reference the sdist and wheel manifests together. Audit the actual sdist members for `.cxx`, `.gradle` and `.externalNativeBuild` caches even when the checkout is clean; these are explicitly excluded in `pyproject.toml` and rejected by source-distribution verification. Keep the sources, headers, build scripts, metadata and legal notices needed to build after isolated extraction.

SDK/installer native modules retain the main build's single CPython ABI. Linux wheels keep their actual native tags; the matrix does not establish `manylinux` or `musllinux` compatibility.

## Localization maintenance

Each Python tool owns its `Resources/Localization/<culture>/` catalogs; only shared messages belong to the Python area. Deployment combines them without duplicate `python_tools.*` IDs. Files must be UTF-8 JSON objects with unique IDs and nonblank string values. A corrupt culture is skipped and the next matching culture is tried.

English catalogs define the accepted message IDs and positional argument contract. Translations may reorder `{0}`, `{1}`, and other numbered arguments, but must preserve their index set; named fields and attribute/index access are not supported. Escape literal braces as `{{` and `}}`. Missing or invalid templates fall back to the effective English catalog, then to built-in English if formatting fails. Keep the Frontend localization regression tests and the six-language deployment checks passing before publication.

Malformed JSON, duplicate message IDs, non-string or blank values, and invalid UTF-8 reject the affected culture. Translation-only IDs are ignored.

<a id="python-package"></a>

The Python group owns the PEP 517 project in `Extensions/Language/Python/pyproject.toml`. From the repository root, the commands below build and audit a local wheel/source-distribution candidate. Installing from the checkout uses `python -m pip install ./Extensions/Language/Python`. Source-distribution native inputs are self-contained; the build still needs the selected CPython development files and native toolchain. `VEROSCRIPT_PYTHON_INSTALL_DIR` is a relative install destination so CMake staging remains relocatable. Run `cibuildwheel` from the repository root when producing the configured wheel matrix; its builds and the validator do not upload a release. Version metadata derives from `VeroScriptVersion.h` and must agree with the loaded runtime.

```sh
python -m pip install build
python -m build Extensions/Language/Python --outdir .tmp/python-dist
python Extensions/Language/Python/VeroScriptPython/Resources/Tools/Packaging/validate_distribution.py .tmp/python-dist --expected-version 0.1.1
```

## Distribution metadata audits

The wheel and source distribution `Requires-Python` bounds must match the entire declared matrix, currently `>=3.11,<3.16`. Every supplied artifact is checked even during partial publication. Windows extension suffixes must match `win32`, `win_amd64` or `win_arm64` and the selected CPython ABI. A macOS wheel’s minimum-version tag must cover the minimum OS required by both its extension and bundled Interop library. Cross assembly preserves the configured compiler, toolchain, generator platform and deployment settings and checks target CPython headers before compiling the extension.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](toolchain-and-packaging.md) · [fr-FR](../fr-FR/toolchain-and-packaging.md) · [ja-JP](../ja-JP/toolchain-and-packaging.md) · [ko-KR](../ko-KR/toolchain-and-packaging.md) · [zh-CN](../zh-CN/toolchain-and-packaging.md) · [zh-TW](../zh-TW/toolchain-and-packaging.md)
