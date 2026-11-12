# 바이트코드 VM

`VeroScriptBytecodeVm`은 백엔드 ID `vs-bytecode`, 배포 이름 `vs-bytecode-vm`인 인터프리터입니다. 인터프리터 선택은 `Vm`/`Mixed`/`Jit` 실행 정책과 독립적입니다. 원시 Core/C++ 호스트에는 암시적 인터프리터가 없습니다.

## 설치된 Runtime 사용

`Vm/`를 포함한 일치하는 전체 Runtime을 유지하세요. 자동 선택은 선택된 설치 환경의 활성 후보를 먼저 사용하고 사용할 자동 VM 후보가 없을 때 동봉 인터프리터를 제품 대체 VM으로 사용합니다. 명시적 선택이나 비활성화가 우선하며 이 대체 VM이 이를 덮어쓰지 않습니다. 다음 명령은 동봉 ID 또는 호환되는 압축 해제 디렉터리를 선택합니다. `.vspkg`의 백엔드 ID를 선택하기 전에 해당 환경에 설치하세요. 설치만으로 고정된 실행 도메인은 바뀌지 않습니다. 사용자 지정 ID에는 패키지 지원과 선택 환경이 필요합니다.

```sh
veroscript --vm-backend=vs-bytecode script.vs
veroscript --vm-backend=./custom-vm script.vs
```

## 정적 SDK 통합

기존 CMake 호스트 대상 `app`에서 호환되는 두 SDK 패키지를 찾고 두 대상을 링크합니다. 실행 전 각 원시 VM을 명시적으로 설정하세요. 링크만으로는 선택되지 않습니다. C++ Session/Engine은 `FEngineConfig` 또는 대응 선택 API를 사용합니다.

```cmake
find_package(VeroScript 0.1.1 EXACT CONFIG REQUIRED)
find_package(VeroScriptBytecodeVm 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE
	VeroScript::VeroScript VeroScript::VeroScriptBytecodeVm)
```

```cpp
#include "VeroScriptVm.h"
#include "VeroScriptBytecodeVmAssembly.h"
#include <sstream>

std::ostringstream output;
VeroScript::VeroScriptVm vm(output);
VeroScript::BytecodeVm::Configure(vm);
```

## 선택과 호환성

실행 확정 시 인터프리터가 고정되며 Session `Clear()`/다시 로드도 선택을 유지합니다. 설정 실패는 이전 선택을 유지합니다. 고정 후 변경하려면 새 Session을 만드세요. 컴파일 스크립트는 명시적 VM이 필요하지만 이를 실행하지 않는 일반 파싱/컴파일은 필요 없습니다. SDK 헤더, Core, 컴파일러/런타임, 아키텍처, 백엔드 식별자를 맞추세요. 같은 버전 문자열만으로 바이너리 호환성이 보장되지는 않습니다. 모바일/Web 정적 제품은 데스크톱 라이브러리 대신 명시적 정적 통합을 사용합니다.

[C++](../../../../../../../Extensions/Language/Cpp/Resources/Docs/User/ko-KR/embedding.md) · [CLI](../../../../../../../VeroScriptCli/Resources/Docs/User/ko-KR/index.md)

자동 선택, 명시 선택 및 비활성화의 공통 규칙은 다음을 참조하세요: [서비스 백엔드 선택](../../../../../../../VeroScript/Resources/Docs/User/ko-KR/introduction.md#backend-selection).

`BytecodeVm::Configure(FEngineConfig&)`는 제품 대체 필드를 설정합니다. Session 오버로드는 `SetFallbackVmBackendFactory`를 사용하여 명시적 선택과 환경 후보를 유지합니다. 반면 원시 `VeroScriptVm`의 `Configure(vm)`는 `SetVmBackendFactory`를 직접 호출하며 이 저수준 VM 자체는 설치된 후보를 해석하지 않습니다. 위 정적 SDK 예제에도 이 구분이 적용됩니다.

<!-- docs-navigation -->

---

[User](../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
