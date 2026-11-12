# 從 C++ 使用安裝環境

連結相符 SDK，使用絕對根路徑。`Load` 回傳具租約的不可變安裝快照，`BuildCoreEnvironment` 準備模組、服務及執行解析器；將同一環境交給編譯與執行。安裝本身不匯入宣告，也不開始執行。Engine／Session 使用該環境時，省略的後端選擇遵循環境預設值與自動候選，明確選擇優先。

將以下內容分別儲存為同一目錄的 CMakeLists.txt 和 main.cpp。設定時將 CMAKE_PREFIX_PATH 指向相符的已安裝 Core SDK，再建置 package-manager-embed 目標。

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

範例接收專案根目錄與入口檔案的絕對路徑，可選的第三個參數為標準函式庫目錄的絕對路徑。匯入 core.vs 或 compiler.vs 時，傳入相符 Core SDK 的 assets/stdlib；在 BuildCoreEnvironment 前將其加入 FCoreEnvironmentOptions.StdlibSearchPaths，使環境授權該目錄。安裝擴充不會自動部署標準函式庫。

Combine(base, overlay) 必須明確呼叫，不下載、不改鎖、不執行處理器。既有繫結/處理器先由 BaseBindings/BaseProcessors 合併，PrepareBindings 提供所需具體原生泛型。發布後不要再給 Session 加入另一套私有目錄。切換環境前 Clear 或建立新 Session。

`entries.native` 宣告 `id/path/linkage/roles`，分析還需要 `catalog` 與 `componentVersion`。`AnalysisOnly` 不執行目標工廠。靜態模組需要宿主的 `StaticExtensions` 查詢與一致的 `ContentIdentity`。物件、回呼和提供者保留擁有者；原生／GUI 服務實作仍是可信宿主整合。

LoadDeployment 讀取已驗證離線閉包，不需 SDK/快取/網路。entries.compiler 原始碼編譯需要編譯環境；RuntimeOnly 不註冊處理器，但能執行產生的可攜位元組碼。宿主重新載入建立新快照並切換閒置使用端，不修改執行中的 Session。

[SDK 範例](../../../Examples/Embed/main.cpp) · [C++](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-TW/embedding.md)

## 透過 C++ 安裝相依列表

在既有宿主函式內設定 `FRequest.RequirementFiles` 並呼叫 `Execute`。檔案路徑基於 `Environment.WorkingDirectory`，不是所選專案根；巢狀檔案與本機分發基於其所在列表檔案。此欄位僅用於 Install。以下片段以前述相依列表指南的有效本機範例與 requirements.txt 為前提，請替換兩個絕對工作區路徑。

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

## VM 與已安裝編譯器原則

本例明確連結並選擇靜態 `VeroScriptBytecodeVm` SDK。若使用已安裝直譯器，呼叫 `FInstalledEnvironment::LoadVmBackend(id, factory, error)`，成功後將工廠交給執行主機；安裝後端不會自動為原始 Core VM 選取它。執行期間保留工廠與環境擁有者。

`FCoreEnvironmentOptions.ScriptCompileExtensions` 提供已安裝編譯擴充的可信主機原則。根目錄來自 `entries.compiler`，呼叫者根不能取代這些項目；Core 與各編譯要求合併時保留較嚴格的有限預算，並取允許原始碼根的交集。編譯指令碼 VM 的工廠與路徑只能指定其一。安裝鉤子使用獨立受治理執行域，由 `FClientOptions.VmBackendFactory`／`VmBackendPath` 設定；省略時僅在已建置對應元件的套件管理產品使用其靜態 VM。

自動、明確選擇及停用的統一規則請參閱 [服務後端選擇](../../../../../VeroScript/Resources/Docs/User/zh-TW/introduction.md#backend-selection).

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/User/zh-TW/index.md) · [en-US](../en-US/embedding.md) · [fr-FR](../fr-FR/embedding.md) · [ja-JP](../ja-JP/embedding.md) · [ko-KR](../ko-KR/embedding.md) · [zh-CN](../zh-CN/embedding.md) · [zh-TW](embedding.md)
