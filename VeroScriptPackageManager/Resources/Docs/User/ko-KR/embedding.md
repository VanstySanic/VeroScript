# C++에서 설치 환경 사용

일치하는 SDK를 연결하고 절대 루트 경로를 사용합니다. `Load`는 소유권을 유지하는 불변 설치 스냅샷을 반환하고 `BuildCoreEnvironment`는 모듈, 서비스, 실행 해석기를 준비합니다. 같은 환경을 컴파일과 실행에 전달하세요. 설치만으로 선언을 가져오거나 실행하지는 않습니다. Engine/Session이 환경을 사용하면 생략한 백엔드는 환경 기본값과 자동 후보를 따르며 명시적 선택이 우선합니다.

아래 파일을 같은 디렉터리에 CMakeLists.txt와 main.cpp로 저장하세요. 구성할 때 CMAKE_PREFIX_PATH에 일치하는 설치된 Core SDK를 지정한 뒤 package-manager-embed 대상을 빌드하세요.

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

예제는 절대 프로젝트 루트와 진입 파일 경로를 받고, 선택적인 세 번째 인수로 표준 라이브러리의 절대 디렉터리를 받습니다. core.vs나 compiler.vs를 가져오려면 일치하는 Core SDK의 assets/stdlib을 전달하세요. 환경이 접근을 허용하도록 BuildCoreEnvironment 전에 FCoreEnvironmentOptions.StdlibSearchPaths에 추가합니다. 확장 설치만으로 표준 라이브러리가 배포되지는 않습니다.

Combine(base, overlay)는 명시적이며 다운로드, 잠금 변경, 프로세서 실행을 하지 않습니다. 게시 전에 BaseBindings/BaseProcessors를 병합하고 PrepareBindings로 필요한 구체적 네이티브 제네릭을 준비합니다. 이후 다른 Session 전용 레지스트리를 추가하지 마세요. 환경을 바꾸기 전 Clear하거나 Session을 새로 만드세요.

`entries.native`는 `id/path/linkage/roles`를 선언하며 분석에는 `catalog`와 `componentVersion`도 필요합니다. `AnalysisOnly`는 대상 팩터리를 실행하지 않습니다. 정적 모듈에는 호스트의 `StaticExtensions` 조회와 일관된 `ContentIdentity`가 필요합니다. 객체, 콜백, 제공자는 소유자를 유지합니다. 네이티브／GUI 서비스는 신뢰하는 호스트 통합으로 취급합니다.

LoadDeployment는 SDK/캐시/네트워크 없이 검증한 오프라인 폐쇄를 읽습니다. entries.compiler 소스 컴파일에는 컴파일 환경이 필요하며 RuntimeOnly는 프로세서를 등록하지 않지만 생성된 이식 바이트코드를 실행할 수 있습니다. 리로드는 새 스냅샷으로 유휴 소비자를 전환하며 실행 중인 Session은 수정하지 않습니다.

[SDK 예제](../../../Examples/Embed/main.cpp) · [C++](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ko-KR/embedding.md)

## C++에서 목록 설치

기존 호스트 함수 안에서 `FRequest.RequirementFiles`를 설정하고 `Execute`를 호출합니다. 경로는 선택된 프로젝트 루트가 아니라 `Environment.WorkingDirectory`를 기준으로 하며 중첩 파일과 로컬 배포본은 자신을 기재한 파일을 기준으로 합니다. Install에서만 사용합니다. 아래 조각은 목록 안내의 유효한 로컬 예제와 requirements.txt를 전제로 합니다. 두 절대 경로를 바꾸십시오.

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

## VM 및 설치된 컴파일러 정책

이 예제는 정적 `VeroScriptBytecodeVm` SDK를 명시적으로 링크하고 선택합니다. 설치된 인터프리터는 `FInstalledEnvironment::LoadVmBackend(id, factory, error)`로 읽고 성공 후 팩터리를 실행 호스트에 전달하세요. 설치만으로 원시 Core VM에서 선택되지는 않습니다. 실행 중 팩터리와 환경 소유자를 유지하세요.

`FCoreEnvironmentOptions.ScriptCompileExtensions`는 설치 확장에 대한 신뢰하는 호스트 정책입니다. 루트는 `entries.compiler`에서 오며 호출자 루트가 이를 대체하지 않습니다. Core는 각 컴파일 요청과 결합하여 더 엄격한 유한 예산 및 허용 루트의 교집합을 사용합니다. 컴파일 스크립트용 VM 팩터리나 경로 중 하나만 지정하세요. 설치 훅은 별도의 관리 도메인이며 `FClientOptions.VmBackendFactory`/`VmBackendPath`로 설정합니다. 생략 시 해당 구성 요소를 빌드한 경우에만 package-manager 제품의 정적 VM을 사용합니다.

자동 선택, 명시 선택 및 비활성화의 공통 규칙은 다음을 참조하세요: [서비스 백엔드 선택](../../../../../VeroScript/Resources/Docs/User/ko-KR/introduction.md#backend-selection).

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/User/ko-KR/index.md) · [en-US](../en-US/embedding.md) · [fr-FR](../fr-FR/embedding.md) · [ja-JP](../ja-JP/embedding.md) · [ko-KR](embedding.md) · [zh-CN](../zh-CN/embedding.md) · [zh-TW](../zh-TW/embedding.md)
