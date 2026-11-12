# 백엔드 유지보수

[코어 JIT 서비스와 동시성](../../../../../../../VeroScript/Resources/Docs/Developer/ko-KR/jit-and-concurrency.md#native-abi-version)을 참조하세요.

Core는 실행 계약, 공개 JIT 계약, 런타임 서비스, 검증, 스케줄링 및 세대 집계를 소유합니다. 구체적인 IR, lowering, 네이티브 생성, 이식 가능한 Linear 실행과 실행 코드 저장은 Core가 아닌 `Modules/Execution/VeroScriptAsmJit`에 속합니다. `Private/JitSupport`는 일반 캐시와 모듈 로더를 구현합니다. 바이너리 호환성은 제품 버전만이 아니라 공개 헤더 지문과 컴파일러/STL/runtime/플랫폼 핸드셰이크로 결정합니다. 바이트코드 인터프리터는 독립 `VeroScriptBytecodeVm` 모듈에 속합니다.

## 빌드와 ABI

`VEROSCRIPT_BUILD_ASMJIT_BACKEND=OFF`는 구체 백엔드 없이 Core를 빌드합니다. `ON`일 때 `VEROSCRIPT_ENABLE_ASMJIT=ON`은 네이티브 생성, `OFF`는 이식 가능한 Linear를 유지합니다. 소스 호스트는 `VeroScriptAsmJit`를 링크하고 `CreateAsmJitBackendFactory()`를 주입합니다. `VeroScriptAsmJitBackend`는 Core 복사본 없는 공유 모듈입니다. 가져온 emitter는 필요한 호환 PIC를 제공하고 소유자가 공유 의존성을 배포합니다. Windows 동적 로드는 `/MD` 또는 `/MDd`가 필요합니다.

```cmake
target_link_libraries(MyHost PRIVATE VeroScriptAsmJit)
```

## 런타임 계약

컴파일 `Ready / Unsupported / Deferred / Failed`와 호출 `NotEntered / Returned / ControlTransfer / Failed`를 구분합니다. `NotEntered`만 VM 재실행을 허용합니다. Core는 게시 전에 소유권, 서비스, 예산을 검증합니다. 실행 산출물은 코드와 모듈을 유지하며 컨텍스트, 프레임, 값은 호출 동안만 빌립니다. 중첩 호출 후 뷰를 다시 얻고 오류/제어/콜백 실시간 플래그를 빠른 경로 판단마다 다시 읽습니다.

Session 코드 재사용 조건, 제어된 실행, 보존 제한과 집계는 [C++ Session](../../../../../../../Extensions/Language/Cpp/Resources/Docs/Developer/ko-KR/session.md#session-jit-cache)에서 관리합니다.

## 게시 검증

`veroscript_jit_query` POD 핸드셰이크는 C++ 팩터리 생성 전에 API, 헤더 지문, 컴파일러/STL, C++ 모드, 런타임과 플랫폼을 검사합니다. 팩터리, 작업자와 실행 산출물의 수명 동안 라이브러리를 유지합니다. Core 단독, 주입 계약, 비호환 라이브러리, 명시적 선택, 재실행 없는 폴백, 자원 회수와 Session 캐시 무효화를 검증합니다. `package-veroscript-jit.py`로 패키징하며 독립 버전도 ABI 일치가 필요합니다. iOS 기기와 시뮬레이터는 동적 로드와 네이티브 생성을 금지하며 정적 이식 백엔드는 별도 검증이 필요합니다.

[Core](../../../../../../../VeroScript/Resources/Docs/Developer/ko-KR/jit-and-concurrency.md)

## 보관 바이트 할당량

`MaxNativeBytes` 또는 `MaxRetainedPayloadBytes`의 0은 해당 바이트 보관을 허용하지 않습니다. 무제한을 뜻하는 정책 예산 0과 다릅니다.

공식 백엔드 아카이브는 `Release/Modules/Execution/Jit/<target>/<version>/archives/`에 게시합니다. Modules에는 새 전용 설치 프로그램이 없습니다. 기능 페이로드는 런타임 의존성과 법적 고지를 유지하며 문서, 예제, 테스트, 벤치마크, 심볼은 제외합니다. 원본 문서는 Resources/Docs에 유지하고 통합 Resources 문서로 게시합니다.

## 포함된 의존성 유지관리

AsmJit는 이 모듈이 소유합니다. 소스를 `ThirdParty/asmjit`에 두고 [원본 라이선스](../../../../ThirdParty/asmjit/LICENSE.md)를 유지하며 [VERSION.txt](../../../../ThirdParty/asmjit/VERSION.txt)에 업스트림 리비전을 기록합니다. 소스를 Resources/Docs나 Core로 옮기지 않습니다. `VEROSCRIPT_ENABLE_ASMJIT=OFF`는 빌드하는 백엔드의 이식 가능한 구현을 선택하고 `VEROSCRIPT_BUILD_ASMJIT_BACKEND=OFF`는 이 백엔드 모듈을 제외합니다.

업데이트할 때 포함된 리비전을 교체하고 고지와 코드 생성 데이터베이스의 라이선스를 유지하며 VERSION.txt와 [모듈 고지](../../../Licenses/NOTICES.md)의 정확한 라이선스 사본을 갱신한 뒤 다시 구성하여 JIT 및 패키징 테스트를 실행합니다. 컴파일러/STL/런타임 호환성을 유지하고 네이티브 생성 및 이식 가능한 구성을 모두 검증합니다. 설치 구성 요소는 LICENSE.md와 VERSION.txt를 `licenses/asmjit/`에 내보내므로 소유 소스와 바이트 단위로 비교합니다. 공통 고지 색인은 Resources/Licenses/README.md에 있으며 의존성 고지는 해당 의존성을 사용하는 모듈이 소유합니다.

## 실행 제어 스칼라 블록과 부분 루프 지원

짧은 정수 루프는 네이티브 CFG와 Core 기본 값 블록으로 실행할 수 있습니다. 각 바이트코드 명령에서 제한, 취소, 시간 초과를 계속 확인하고 계산하며 일괄 선계산하지 않습니다. EnableScalarBlocks는 진입 전에 사용자 호스트 실행 검사와 변경 관찰자를 거절합니다. 지원하지 않는 표현은 현재 명령에서 일반 서비스로 이어져 검사/계산을 중복하거나 함수를 재실행하지 않습니다. 복잡한 루프는 비용에 따라 VM을 선택합니다.

타입이 맞는 문자열 +=는 일반 CFG 검증에 참여합니다. 최적화되지 않은 텍스트 루프가 같은 함수의 다른 네이티브 루프를 막지 않습니다. 실행 제어 Mixed의 명시적 활성화 규칙은 그대로입니다.

분기가 없는 짧은 정수 수신자 식은 네이티브 호출 브리지 대신 Core의 제한된 호출 계획을 우선할 수 있습니다. 일반 백엔드 컴파일과 검증 이후의 비용 선택입니다. 실패, 변경 관찰자 및 실행 제어는 기존 VM 경로를 유지하며 더 큰 본문, 호출 및 루프는 독립적인 계획을 선택합니다.

`bPreferVmForNestedExecution`은 실행 제어가 없는 인터프리터 호출자에만 적용되며, 호스트 직접 호출과 네이티브 호출자는 네이티브 진입점을 유지합니다.

[코어 JIT 서비스와 동시성 — 동시성과 검증](../../../../../../../VeroScript/Resources/Docs/Developer/ko-KR/jit-and-concurrency.md#construction-dispatch)을 참조하세요.

<!-- docs-navigation -->

---

[개발자 문서](../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
