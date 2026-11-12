# 실험적 모바일 임베딩

Android와 iOS 어댑터는 실험 단계입니다. 대상에 맞는 SDK 산출물을 사용하고 실제 배포 환경에서 앱을 검증하세요. `FSessionOptions`는 절대 표준 라이브러리 디렉터리와 소스 리소스 스냅샷을 지정합니다. 모든 가상 소스를 `Sources`로 제공하면 실제/가상 경로를 사용할 수 있으며 진입과 상대 import의 절대 라벨을 일치시킵니다. 선택적 캐시는 실제 쓰기 가능한 절대 디렉터리여야 하며 빈 값은 비활성화합니다. 리소스 갱신 후 runner를 다시 만듭니다.

```cpp
VeroScript::Mobile::FSessionOptions options;
options.StdlibDirectory = "/app/resources/stdlib";
VeroScript::Mobile::FRunner runner(std::move(options));
auto result = runner.Execute("int main(){ print(42); return 0; }", "/app/scripts/main.vs");
```

`FRunner`는 한 번에 하나만 실행하고 호출마다 새 Session을 만들므로 전역 상태가 유지되지 않습니다. Execute 호출은 동기식입니다. Android 및 Apple에서는 내부 작업자에서 실행할 수 있으므로 호스트 콜백은 호출 스레드나 UI 스레드에서 실행된다고 가정하면 안 됩니다. 생성과 실행은 읽기나 컴파일을 수행하므로 UI 스레드 밖에 배치하세요. 객체 수명을 유지하면 다른 스레드에서 취소를 요청할 수 있습니다. 실행/취소 호출 종료 후 파괴합니다. `FSession` 생성, 일반 호출, 파괴는 생성 스레드가 소유하며 호스트가 최소 8 MiB의 네이티브 컴파일 스택을 제공해야 합니다. 취소 상태는 유지됩니다. 콜백은 자체 권한과 취소를 구현해야 합니다. 취소는 컴파일을 선점하거나 블로킹 네이티브 콜백을 강제 종료하지 않습니다.

캐시를 켜면 진입 소스와 import한 소스의 내용이 무효화 판단에 포함됩니다. 적중 시 바이트코드를 재사용하지만 현재 내용을 확인하기 위해 리소스를 읽고 파싱할 수 있으므로 시작 작업이 모두 생략된다고 가정하지 마세요. 사용할 수 없거나 손상된 캐시는 정상 컴파일 경로로 돌아갑니다.

기본값은 VM, 1,000,000 명령, 100 ms, 호출 깊이 256입니다. 콘솔 출력만 허용하고 입력, 파일, 환경, 디렉터리 변경과 작업 생성은 막습니다. 출력/진단은 합계 1 MiB, 오류는 최대 64 KiB입니다. 리소스 소스와 각 진입은 16 MiB, 최대 4096 리소스와 4096바이트 경로입니다. VM 힙 할당량이나 완전한 샌드박스가 아닙니다. 호스트는 풀어 놓은 카탈로그를 `vs_interop_locale_load_catalog_directory`로 명시 로드하고 언어를 선택합니다. 환경, cwd, 실행 파일 주변 자동 검색은 없으며 미설정 시 영어입니다. iOS 기기/시뮬레이터는 동적 JIT와 네이티브 생성을 금지합니다.

## 모바일 언어 명시 설정

SDK는 여섯 언어 catalog를 제공하지만 자동 로드하지 않습니다. 엔진 생성/실행 전에 전체 catalog를 앱 소유의 실제 디렉터리로 복사하고 로드합니다. APK asset/가상 경로는 사용할 수 없습니다. Android AAR은 `veroscript/localization`, iOS SDK는 `assets/localization`입니다. Android의 `VeroScriptEngine.loadCatalogDirectory(path)`와 `setLanguage(tag)`는 성공 여부를 반환합니다. iOS/C++는 `vs_interop_locale_load_catalog_directory`, `vs_interop_locale_set_language`를 사용하고 오류 버퍼는 `vs_interop_buffer_destroy`로 해제합니다. 반환값을 모두 확인합니다. 언어는 엔진별이 아닌 프로세스 전역이며 실패하면 이전 선택을 유지합니다. catalog가 없으면 알 수 없는 메시지는 영어입니다. 진단과 어댑터 검증은 같은 언어를 사용하고 스크립트 출력은 바뀌지 않습니다. 할당 실패 시 영어 fallback을 유지할 수 있습니다. 어댑터 문맥을 번역해도 OS 오류 세부 내용은 시스템 라이브러리 언어를 따를 수 있습니다.

## 관리 바이트 저장소 한도

기본값은 버퍼당 16 MiB, 보유 총량 32 MiB입니다. Core 모바일 옵션의 `MaxBufferBytes` / `MaxRetainedBytes`로 설정하며 0은 무제한입니다. 소유 용량, 외부 바이트 및 확장 시 최대 사용량을 계산하고 VM heap 전체를 제한하지는 않습니다. 기본 콘솔 출력 전용 정책은 네이티브 주소를 비활성화합니다.

## 실행 제어를 적용한 네이티브 실행과 정적 AOT

<a id="mobile-jit-selection"></a>

모바일 기본값은 VM입니다. Android는 별도의 `vs-asmjit` AAR과 JIT 파일／디렉터리를 명시적으로 선택할 수 있습니다. 호환되는 `libc++_shared.so`는 하나만 포함합니다. 파일 경로로 JIT를 읽는 앱은 네이티브 라이브러리를 추출해야 합니다(앱에서 `jniLibs.useLegacyPackaging = true`). iOS 기기와 시뮬레이터는 동적 JIT를 금지합니다.

<a id="mobile-core-aot"></a>

`VeroScriptCppCoreAot.h`의 `CompileSourceToCoreAot`／`GenerateCoreAotSourceBundle`은 대상 C++, 정확히 일치하는 `.vsapp`／`.vsbc`, 범위 보고서 JSON을 만듭니다. 대상 SDK로 컴파일하고 Mixed를 선택해 해당 패키지를 실행합니다. 미지원 함수와 모듈／필드 초기화는 VM을 사용하며 엄격한 네이티브 모드에서는 거부합니다. Core 의미를 유지하는 경로이며 독립 소스 내보내기와 다릅니다.

플랫폼 API: [Android](../../../../../VeroScriptAndroid/Resources/Docs/User/ko-KR/index.md#android-native-selection) · [iOS](../../../../../VeroScriptIOS/Resources/Docs/User/ko-KR/index.md#ios-native-selection).

[선택적 JIT 백엔드 — 실행 제어를 적용한 네이티브 실행과 정적 AOT](../../../../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/User/ko-KR/index.md#controlled-native-execution)을 참조하세요.

전체 모바일 SDK 패키지는 생성된 C++을 위한 `aot/include`를 제공합니다. 공개 Core 서비스 헤더와 일치하는 대상 빌드 ID를 Interop 헤더와 함께 포함 경로에 추가합니다. 헤더 묶음이므로 Interop 런타임은 하나만 링크합니다. AAR만 설치하면 일치하는 헤더 묶음을 별도로 받아야 합니다.

VM 선택은 `ExecutionBackend` 및 JIT와 독립적입니다. `FSessionOptions.VmBackendPath`가 비어 있으면 제품에 명시적으로 구성된 정적 VM을 유지합니다. 값을 지정하면 네이티브 라이브러리 파일 또는 디렉터리의 절대 경로여야 하며 실패 시 대체하지 않습니다. Android 전체 생성자는 `vmBackendPath`, iOS는 `VSMConfiguration.vmBackendPath`를 제공하며 iOS 기본값은 정적 구성입니다.

<!-- docs-navigation -->

---

[사용자 문서](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
