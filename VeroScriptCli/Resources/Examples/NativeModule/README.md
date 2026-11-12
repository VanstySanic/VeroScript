# Native CLI module

## Normal use after installation

An enabled package declaring the native `cli` role is discovered in the selected
project or Runtime/SDK environment. Once the example is distributed as such a
package, use its registered command directly:

```text
veroscript install <cli-package>
veroscript greet Vero
veroscript --greeting="你好" greet Vero
```

For another project, place `--path <project>` before module options. The command
name comes from its registration, independently of the package or module name.
No module path/name argument is needed for these installed commands.

## Build and load a local library

The example consists of [CMakeLists.txt](CMakeLists.txt) and
[NativeModule.cpp](NativeModule.cpp). The commands below run from a source checkout.
When using the integrated All documentation, copy these two adjacent files to a
local directory and use that directory as the `cmake -S` argument.

This module extends the existing `veroscript` executable through the versioned
`veroscript.cli` host service. Build against the same Core SDK, architecture,
compiler and runtime configuration as the executable. It consumes SDK headers
and the host vtable; do not link another static Core into the shared library.

```powershell
cmake -S VeroScriptCli/Resources/Examples/NativeModule -B .tmp/cli-example -DCMAKE_PREFIX_PATH=D:/VeroScript/SDK/Core/0.1.1
cmake --build .tmp/cli-example --config Release
veroscript --cli-module .tmp/cli-example/Release/VeroScriptCliExample.dll --greeting "你好" greet "Vero"
veroscript --cli-module .tmp/cli-example/Release/VeroScriptCliExample.dll greet --help
```

Linux/macOS use the built `.so`/`.dylib` path. The explicit loader accepts files,
resolves relative paths against the invocation's startup directory, and does not
scan PATH or module directories. Repeat `--cli-module` to load additional modules.

For package installation, declare this native entry with `roles: ["cli"]` in the
ordinary native-target package manifest. Its target must contain the actual
`os`, `arch`, `environment`, `coreBuild` and `abis.native-extension` reported by
the matching `veroscript env --json`. The package manager generates and verifies
the loader view normally. Enabled CLI modules in the selected project/component
environment are then loaded by the CLI; disabled packages contribute no commands.

Global environment, binding-catalog, standard-library and execution-backend
options precede module options. Module options precede the script/command name;
tokens after that name are passed intact to the script/command. Put `--path`
before module options when choosing a project outside the startup directory.
Registered command names take precedence over same-name files/directories; use
an explicit file path such as `./greet` to run that file as a script.
Core commands/options and duplicate names cannot be replaced. Core subcommands
(`aot`, `install`, etc.) retain their own argument grammars; a module extends them
by providing a separate command rather than intercepting their arguments.

Configuration callbacks run once, in module ID order, after parsing and before
ordinary execution or a module command. They validate and store configuration;
service/task execution belongs to the command or script. They only receive their own option
values. Help lists registered entries without configuring or executing them;
`<command> --help` displays that command's registered description and usage.
Basic `--help` also works before a new project's dependencies are installed.
Explicit local module commands can also run at that stage; ordinary script
execution continues to require its declared packages to be installed.
Callbacks may use the context's UTF-8 language tag and streams for localized
messages. Names, exit codes and option syntax remain stable identifiers.

Registration is synchronous during native `Create`; descriptors contain borrowed
state owned by the native registration. Join module-owned workers before native
`Destroy` returns. Do not retain the invocation context or strong references back
to the environment that owns the registration. The host retains native code
through dispatch, rejects late registration, catches callback exceptions, and
propagates command exit codes in `0..255`. Long/blocking native commands own their
cancellation behavior. Ordinary DAP launches do not accept module runtime options.

Modules with combined `bindings`/`compiler`/`cli` roles should tolerate an absent
CLI host service when used in SDK hosts or analysis. A CLI-only module is skipped
by those hosts. The package manager handles installation; Core provides the
native/service contracts; CLI owns registration validation, parsing and dispatch.
