# 从 C++ 消费已安装环境

链接匹配 SDK，使用绝对根路径。`Load` 返回带租约的不可变安装快照，`BuildCoreEnvironment` 准备模块、服务及执行解析器；将同一环境交给编译与执行。安装本身不导入声明，也不开始执行。Engine／Session 使用该环境时，省略的后端选择遵循环境默认值和自动候选，显式选择优先。

将以下内容分别保存为同一目录的 CMakeLists.txt 和 main.cpp。配置时将 CMAKE_PREFIX_PATH 指向匹配的已安装 Core SDK，然后构建 package-manager-embed 目标。

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

示例接收项目根与入口文件的绝对路径，可选第三个参数为标准库目录的绝对路径。导入 core.vs 或 compiler.vs 时，传入匹配 Core SDK 的 assets/stdlib；在 BuildCoreEnvironment 前将其加入 FCoreEnvironmentOptions.StdlibSearchPaths，使环境授权该目录。安装扩展不会自动部署标准库。

Combine(base, overlay) 必须显式调用，不下载、不改锁、不执行处理器。原有绑定/处理器先通过 BaseBindings/BaseProcessors 合并，PrepareBindings 提供所需具体原生泛型类型；发布后不要再给 Session 添加另一套私有目录。切换环境前 Clear 或创建新 Session。

`entries.native` 声明 `id/path/linkage/roles`，分析还需 `catalog` 与 `componentVersion`。`AnalysisOnly` 不执行目标工厂。静态模块需要宿主的 `StaticExtensions` 查询与一致的 `ContentIdentity`。对象、回调和提供者保留所有者；原生／GUI 服务实现仍是可信宿主集成。

LoadDeployment 读取已验证的离线闭包，无需 SDK/缓存/网络。含 entries.compiler 的源码编译需要编译环境；RuntimeOnly 不登记处理器，但可执行生成后的可移植字节码。宿主重载构造新快照并切换空闲消费方，不修改运行中的 Session。

[SDK 示例](../../../Examples/Embed/main.cpp) · [C++](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-CN/embedding.md)

## 通过 C++ 安装依赖列表

在已有宿主函数内设置 `FRequest.RequirementFiles` 并调用 `Execute`。文件路径基于 `Environment.WorkingDirectory`，不是所选项目根；嵌套文件及本地分发基于其所在列表文件。此字段仅用于 Install。以下片段以前述依赖列表指南的有效本地示例及 requirements.txt 为前提，请替换两个绝对工作区路径。

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

## VM 与已安装编译器策略

本例显式链接并选择静态 `VeroScriptBytecodeVm` SDK。若改用已安装的解释器，调用 `FInstalledEnvironment::LoadVmBackend(id, factory, error)`，成功后将工厂交给执行宿主；安装后端不会自动为原始 Core VM 选择它。执行期间保留工厂与环境所有者。

`FCoreEnvironmentOptions.ScriptCompileExtensions` 提供已安装编译扩展的可信宿主策略。其根来自 `entries.compiler`，调用者根不能替换这些条目；Core 与每次编译请求合并时保留更严格的有限预算，并取允许源码根的交集。编译脚本的 VM 工厂与路径只可指定其一。安装钩子另用受治理的执行域，由 `FClientOptions.VmBackendFactory`／`VmBackendPath` 配置；省略时仅在已构建相应组件的包管理产品中使用其静态 VM。

自动、显式选择及禁用的统一规则见 [服务后端选择](../../../../../VeroScript/Resources/Docs/User/zh-CN/introduction.md#backend-selection).

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/User/zh-CN/index.md) · [en-US](../en-US/embedding.md) · [fr-FR](../fr-FR/embedding.md) · [ja-JP](../ja-JP/embedding.md) · [ko-KR](../ko-KR/embedding.md) · [zh-CN](embedding.md) · [zh-TW](../zh-TW/embedding.md)
