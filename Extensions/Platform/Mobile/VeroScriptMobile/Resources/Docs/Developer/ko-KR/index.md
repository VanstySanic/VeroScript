# Mobile — 개발자 문서

UI 전달과 수명 주기는 어댑터/호스트가 담당하고 Core는 플랫폼 중립을 유지합니다. embedded-only는 데스크톱 CLI/tooling과 언어 exporter를 제외합니다. 정적 Interop은 소스 의존 그래프 또는 병합 iOS 아카이브에 속하며 데스크톱 배포는 공유입니다. Android/Linux, iOS/macOS, 기기/시뮬레이터를 구분합니다. 허용 플래그는 백엔드 빌드/선택이나 기기 검증 증거가 아닙니다. import, 캐시 무효화/손상, 상한, 취소와 파괴를 검증합니다.

```text
cmake -S . -B build/embedded -DVS_BUILD_VEROSCRIPT_EMBEDDED_ONLY=ON -DVEROSCRIPT_BUILD_ASMJIT_BACKEND=OFF -DBUILD_TESTING=ON
cmake --build build/embedded --config Release --target VeroScriptMobileTests
```

`package-veroscript-mobile.py`는 별도 실험 출력을 만들고 기존 목적지를 거부하며 데스크톱 컬렉션에는 게시하지 않습니다.

도구 버전과 플랫폼별 검증 요구 사항은 [Android](../../../../../VeroScriptAndroid/Resources/Docs/Developer/ko-KR/index.md)와 [iOS](../../../../../VeroScriptIOS/Resources/Docs/Developer/ko-KR/index.md)에서 관리합니다.

[사용자 문서](../../User/ko-KR/index.md)


## 모바일 리소스와 언어

<a id="mobile-localization"></a>

`mobile` 프로필은 Core, C++ Session, Interop, `Extensions/Platform/Mobile/VeroScriptMobile/Resources/Localization`을 모으며 데스크톱 Tooling/UI는 제외합니다. Gradle은 같은 catalog를 AAR에 복사하고 packager는 목록과 바이트를 확인합니다. iOS는 `assets/localization`에 복사합니다. 앱은 언어 선택 전에 명시적으로 추출/로드하며 모바일 환경 검색을 도입하지 않습니다. 모바일 패키지는 기능 파일, 런타임 리소스와 라이선스를 포함합니다. README, 설명서와 예제는 제외하며 통합 User 문서는 별도 Resources 배포물에서 받습니다. 네이티브 리소스 경로와 기기 검증 요구 사항은 유지됩니다.

Mobile의 `TranslateMessage`는 공개 C ABI에 위임합니다. 버퍼 소유권과 오류 동작은 [Interop](../../../../../../../../VeroScriptInterop/Resources/Docs/Developer/ko-KR/abi-and-lifetime.md)에서, Android JNI 인코딩은 [Android](../../../../../VeroScriptAndroid/Resources/Docs/Developer/ko-KR/index.md)에서 관리합니다.

Core와 대상 AOT 구현 지문에는 `Shared/Private/VeroScriptFloatParsing.h`, `VeroScriptFloatFormatting.h`, `VeroScriptJsonString.h`를 포함합니다. Python 소스 배포는 Application/패키지 런타임용 `VeroScriptDarwinPaths.h`를 포함한 필수 `Shared/Private` 입력을 보존합니다. Android Python 소스 다이제스트는 Application과 그 C 소스도 포함합니다. 이는 공개 SDK 헤더나 장치 검증 증거가 아닌 내부 빌드 입력입니다.

<a id="mobile-execution-worker"></a>

`Private/VeroScriptMobile.cpp::WithCompilerStack`은 Android/Apple의 호출 스택을 확인합니다. 8 MiB 미만이면 8 MiB pthread를 만들고 동기적으로 join하며, 그 외에는 호출 스레드에서 실행합니다. 파싱만이 아니라 FSession 전체의 생성·실행·파괴를 작업자가 소유하고 예외를 저장하여 join 뒤 다시 던집니다. Session 파괴 전에 취소 뮤텍스 아래에서 활성 포인터를 지우고 모든 종료 경로에서 외부 실행 상태를 정리하세요. 직접 FSession을 호출하면 이 보조 경로를 사용하지 않으며 호스트가 스택을 제공합니다. 언어 중첩·호출 깊이·실행 제한은 늘어나지 않습니다. 작은 스택의 정상 입력, 제한 진단, 예외, 취소, 재사용을 검증합니다. Android 검증 앱은 중첩 후 복구를 다루며 Apple 회귀는 호출 스택을 512 KiB로 명시합니다.

<a id="mobile-message-coverage"></a>

Mobile 카탈로그는 C++ Session/runner, Android AOT/Java 검증, iOS 팩터리/백엔드 오류를 모두 다룹니다. C++와 Java는 기존 번역 진입점을 사용하고 JNI/NSError는 경계에서 예외를 번역합니다. std::system_error는 코드와 범주를 바꾸지 않고 프로젝트 문맥만 번역하며 OS 접미사는 시스템 라이브러리 언어를 따릅니다. 리소스 실패 시 기존 대체 동작을 유지하고 여섯 언어의 키와 자리표시자를 일치시키세요.

Core/C++/C 전용 소스 호스트는 `VEROSCRIPT_BUILD_MOBILE_ADAPTERS=OFF`로 어댑터를 끌 수 있습니다. `VEROSCRIPT_INTEROP_STATIC=ON`은 소스/정적 모바일 빌드 옵션이며 데스크톱 공유 Interop 배포를 바꾸지 않습니다.

<!-- docs-navigation -->

---

[개발자 문서](../../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
