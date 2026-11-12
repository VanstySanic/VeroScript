# 교체 가능한 VM 백엔드

<a id="vm-provider-assembly"></a>

Core는 프로그램, 값, 논리 프레임, 언어 연산, 실행 제어와 디스패치를 소유합니다. 전체 바이트코드 해석 루프는 Modules/Execution/VeroScriptBytecodeVm의 VeroScriptBytecodeVm에 속합니다. 백엔드 ID는 vs-bytecode이고 패키지 이름은 vs-bytecode-vm입니다. Core는 VM이나 JIT를 암시적으로 선택하지 않습니다. Runtime은 Vm/의 공식 동적 라이브러리를 명시적으로 구성하며 Interop, Web 및 모바일 정적 제품도 VM을 명시적으로 주입합니다.

## 정적 구성

```cmake
find_package(VeroScript 0.1.1 CONFIG REQUIRED)
find_package(VeroScriptBytecodeVm 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::VeroScript VeroScript::VeroScriptBytecodeVm)
```

```cpp
#include "VeroScriptBytecodeVm.h"
#include "VeroScriptVm.h"

auto factory = VeroScript::BytecodeVm::CreateFactory(VeroScript::GetVmHostServices());
VeroScript::VeroScriptVm vm(output);
std::string error;
if (!vm.SetVmBackendFactory(factory, error))
{
	throw std::runtime_error(error);
}
```

## 동적 구성

```cpp
if (!vm.SetVmBackendPath("./Vm", error))
{
	throw std::runtime_error(error);
}
```

```text
veroscript --vm-backend vs-bytecode app.vs
veroscript --vm-backend ./custom-vm app.vs
veroscript --vm-backend ./Vm/VeroScriptVmBytecode.dll app.vs
```

선택은 실행 도메인에 속하며 스크립트 실행이 시작되면 고정됩니다. Session.Clear()로 해제되지 않습니다. 활성 컨텍스트마다 별도 인스턴스를 사용하며 동기 재진입은 같은 활성 인스턴스를 사용합니다. 공식 백엔드는 같은 도메인과 스레드에서 완전히 유휴 상태인 인스턴스 하나를 재사용할 수 있습니다. 구성 실패는 이전 선택을 유지하며 진입 전 실패는 수정할 수 있습니다.

스크립트 컴파일 처리기에도 FCompileOptions::ScriptCompileExtensions.VmBackendFactory 또는 .VmBackendPath가 필요합니다. 처리기를 실행하지 않는 파싱, 일반 컴파일 및 분석에는 VM이 필요하지 않습니다. worker와 격리된 정리 컨텍스트는 선택과 기존 실행 제어를 상속합니다.

## 독립 빌드와 SDK

실행 계약은 헤더 전용이며 Core 바이너리를 링크하지 않습니다. 백엔드는 실제 소유 Core가 제공하는 서비스를 호출합니다. 호스트는 Core와 정적 VM을 링크하거나 Core에서 동적 VM을 로드합니다. 결합 SDK에 둘 다 포함할 수 있지만 링크만으로 자동 등록되지는 않습니다.

```text
cmake --install <build> --component VeroScriptExecutionContract --prefix <sdk>
cmake --install <build> --component VeroScriptBytecodeVmSdk --prefix <sdk>
cmake -S Modules/Execution/VeroScriptBytecodeVm -B <vm-build> -DCMAKE_PREFIX_PATH=<sdk>
```

## 실행 및 배포 제약

EExecutionBackend::Vm/Mixed/Jit는 별도 정책이며 기본 JIT는 없습니다. VM이 없거나 기능이 부족하면 명시적으로 실패하며 인수를 반복 준비하거나 숨은 폴백을 실행하지 않습니다. 제한, 취소, 권한 및 공유된 유한 정리는 Core 의미를 유지합니다. 불완전한 정리나 잘못된 백엔드 상태에는 Session 초기화가 필요합니다. 버전은 0.1.1이며 공개 헤더 지문, 컴파일러, STL, CRT, 플랫폼 및 레이아웃도 일치해야 합니다. Windows는 일치하는 /MD를, iOS/Web은 정적 구성을 사용합니다. 데스크톱 테스트는 모바일 도구 체인과 장치 검증을 대체하지 않습니다.

독립 배포는 이식 가능한 ZIP/tar와 루트에 매니페스트가 있는 설치용 `.vspkg`를 제공합니다. `veroscript install ./vs-bytecode-vm-0.1.1-<target>.vspkg --path=<project>`로 설치합니다. 설치는 `vm` 서비스와 `vs-bytecode` ID를 기록하며 기존 실행 도메인의 선택을 바꾸지 않습니다. 압축을 푼 라이브러리는 파일이나 디렉터리로 직접 지정할 수도 있습니다.

## VM 없이 컴파일

```cmake
find_package(VeroScript 0.1.1 CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::VeroScript)
```

```cpp
#include "VeroScriptCompile.h"

auto compiled = VeroScript::VeroScriptCompile::CompileSource(
	"int answer(){return 42;}", "answer.vs");
// compiled.bSuccess / compiled.Error / compiled.Module
```

GUI 등 C++ SDK 호스트는 선택한 VM을 `FEngineConfig`로 전달합니다. 마크업 스크립트는 `Gui::FMarkupScriptOptions::Engine`을 사용합니다. 어댑터는 호스트의 선택을 유지하며, C++ SDK 자체는 백엔드를 암묵적으로 선택하지 않습니다. 저장소의 독립 모듈 테스트에는 `-DVEROSCRIPT_TEST_VM_LIBRARY=<일치하는 동적 백엔드 파일>`을 명시할 수 있습니다.
