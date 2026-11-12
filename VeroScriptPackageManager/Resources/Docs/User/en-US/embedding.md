# Consume installed environments from C++

Link the matching SDK and use absolute roots. `Load` returns an immutable installed snapshot with leases; `BuildCoreEnvironment` prepares modules, services and execution resolvers. Give the same environment to compilation and execution. Installation alone does not import declarations or start execution. When an Engine/Session uses the environment, omitted backend choices follow its defaults and automatic candidates; explicit choices take priority.

Save the following files as CMakeLists.txt and main.cpp in the same directory. Set CMAKE_PREFIX_PATH to the matching installed Core SDK when configuring, then build the package-manager-embed target.

```cmake
cmake_minimum_required(VERSION 3.25)
project(PackageManagerEmbed LANGUAGES CXX)
find_package(VeroScript 0.1.1 EXACT CONFIG REQUIRED)
find_package(VeroScriptPackageManager 0.1.1 EXACT CONFIG REQUIRED)
find_package(VeroScriptBytecodeVm 0.1.1 EXACT CONFIG REQUIRED)
add_executable(package-manager-embed main.cpp)
target_link_libraries(package-manager-embed PRIVATE
	VeroScript::VeroScriptPackageManager VeroScript::VeroScriptBytecodeVm)
```

```cpp
#include "VeroScriptPackageManager.h"
#include "VeroScriptCompile.h"
#include "VeroScriptVm.h"
#include "VeroScriptBytecodeVmAssembly.h"
#include <iostream>
int main(int argc, char** argv)
{
	if (argc < 3 || argc > 4)
	{
		std::cerr << "Use absolute project root, entry script and optional stdlib directory paths\n";
		return 2;
	}
	using namespace VeroScript::Packages;
	FPackageManager Manager;
	FEnvironmentInput Input;
	Input.Root = argv[1];
	FDiagnostic Error;
	std::shared_ptr<const FInstalledEnvironment> Installed;
	if (!Manager.Load(Input, Installed, Error))
	{
		std::cerr << Error.Code << ": " << Error.Message;
		return 1;
	}
	FCoreEnvironmentOptions Options;
	Options.EntryRoot = argv[1];
	if (argc == 4)
	{
		Options.StdlibSearchPaths = {argv[3]};
	}
	std::shared_ptr<const VeroScript::FModuleEnvironment> Environment;
	if (!Installed->BuildCoreEnvironment(Options, Environment, Error))
	{
		std::cerr << Error.Code << ": " << Error.Message;
		return 1;
	}
	VeroScript::FCompileOptions Compile;
	Compile.ScriptCompileExtensions.VmBackendFactory = VeroScript::BytecodeVm::CreateFactory(VeroScript::GetVmHostServices());
	Compile.ModuleEnvironment = Environment;
	auto Code = VeroScript::VeroScriptCompile::CompileFile(argv[2], Compile);
	if (!Code.bSuccess)
	{
		std::cerr << Code.Error.Message;
		return 1;
	}
	VeroScript::VeroScriptVm Vm(std::cout);
	VeroScript::BytecodeVm::Configure(Vm);
	std::string Failure;
	if (!Vm.SetModuleEnvironment(Environment, Failure))
	{
		std::cerr << Failure;
		return 1;
	}
	if (!Vm.Execute(*Code.Module, VeroScript::ERunMode::Script))
	{
		std::cerr << Vm.GetError().Message;
		return 1;
	}
	return Vm.GetProcessExitCode();
}
```

The sample accepts absolute project root and entry paths, plus an optional absolute standard-library directory as its third argument. For imports such as core.vs or compiler.vs, pass the matching Core SDK assets/stdlib directory; add it to FCoreEnvironmentOptions.StdlibSearchPaths before BuildCoreEnvironment so the environment authorizes it. Installing an extension does not deploy the standard library automatically.

Combine(base, overlay) is explicit and does not download, rewrite locks or execute processors. Merge existing native bindings/processors through BaseBindings/BaseProcessors before publishing; PrepareBindings supplies required concrete native generic types. Do not add a different private Session registry afterwards. Clear or replace a Session before applying a different environment.

`entries.native` declares `id/path/linkage/roles`; analysis also requires `catalog` and `componentVersion`. `AnalysisOnly` does not execute target factories. Static modules need the host’s `StaticExtensions` query and a consistent `ContentIdentity`. Objects, callbacks and providers retain their owners. Native/GUI service implementations remain trusted host integrations.

LoadDeployment reads a validated offline closure without SDK/cache/network. Source compilation with entries.compiler needs its compile environment; RuntimeOnly does not register those processors, but generated portable bytecode can execute. Host reload builds a new snapshot and switches an idle consumer; it does not mutate running sessions.

[SDK example](../../../Examples/Embed/main.cpp) · [C++](../../../../../Extensions/Language/Cpp/Resources/Docs/User/en-US/embedding.md)

## Install from a list through C++

Inside an existing host function, set `FRequest.RequirementFiles` and call `Execute`. Paths use `Environment.WorkingDirectory`, not the selected project root; nested files and local distributions use their containing file. Use this field only with Install. The fragment below assumes the same valid local example and requirements.txt as the dependency-list guide. Replace both absolute workspace paths.

```cpp
using namespace VeroScript::Packages;
FRequest Request;
Request.Operation = EOperation::Install;
Request.Environment.WorkingDirectory = "/absolute/workspace";
Request.Environment.Root = "/absolute/workspace/app";
Request.Options.Offline = true;
Request.RequirementFiles = {"requirements.txt"};
FResult Result;
FDiagnostic Error;
if (!FPackageManager{}.Execute(Request, Result, Error))
{
	std::cerr << Error.Code << ": " << Error.Message << " " << Error.Field;
}
```

## VM and installed compiler policy

This example explicitly links and selects the static `VeroScriptBytecodeVm` SDK. To use an installed interpreter instead, call `FInstalledEnvironment::LoadVmBackend(id, factory, error)` and, after success, pass that factory to the execution host. Installing a backend does not select it for a raw Core VM. Keep the factory and environment owners alive during execution.

`FCoreEnvironmentOptions.ScriptCompileExtensions` supplies the trusted policy for installed compile extensions. Their roots come from `entries.compiler`; caller-provided roots do not replace those entries. Core combines this policy with each compile request, preserving the stricter finite budgets and intersecting allowed source roots. Supply a VM factory or path for compile scripts, and never both. Install hooks have a separate governed domain configured with `FClientOptions.VmBackendFactory`/`VmBackendPath`; omission uses the package-manager product's static VM only when that component was built.

Unified automatic, explicit and disabled selection rules are documented in [Service backend selection](../../../../../VeroScript/Resources/Docs/User/en-US/introduction.md#backend-selection).

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/User/en-US/index.md) · [en-US](embedding.md) · [fr-FR](../fr-FR/embedding.md) · [ja-JP](../ja-JP/embedding.md) · [ko-KR](../ko-KR/embedding.md) · [zh-CN](../zh-CN/embedding.md) · [zh-TW](../zh-TW/embedding.md)
