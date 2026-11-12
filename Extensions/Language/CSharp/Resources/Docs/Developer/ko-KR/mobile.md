# CSharp — Mobile SDK

이 페이지는 C# 모바일 SDK를 빌드하고 배포하는 유지보수자를 위한 문서입니다. 데스크톱과 모바일은 `VeroScriptCSharp.csproj` 및 NuGet ID `VeroScript.CSharp`를 공유합니다. 기본 대상은 .NET 8/9이며 `VeroScriptIncludeMobile=true`는 .NET 10 Android/iOS 대상을 모두 추가합니다. Android는 API 26 및 arm64-v8a/x86_64, iOS는 15.0 및 arm64 기기와 arm64/x86_64 시뮬레이터가 필요합니다. 모바일 패키징에는 네 개의 관리 대상, 해당 워크로드 및 검증된 전체 네이티브 루트가 필요합니다.

`prepare-native.py`는 같은 소스 리비전의 Android AAR 및 정적 `VeroScriptIOS.xcframework` SDK를 사용합니다. 이 입력에는 Core/C++/Interop이 포함됩니다. C# 앱에는 CPython SDK나 Python 모바일 패키지 생성 도구가 필요하지 않으며 Java JNI 래퍼나 네이티브 JIT 모듈도 사용하지 않습니다. 새 디렉터리에 준비하고 검증한 뒤 패키징하세요.

## 네이티브 SDK 준비

```sh
gradle --project-dir Extensions/Platform/Mobile/VeroScriptAndroid --no-daemon assembleRelease
python3 Resources/Tools/Packaging/package-veroscript-mobile.py ios \
  --cmake /path/to/cmake --out-root dist/mobile/ios
python3 Extensions/Language/CSharp/Resources/Tools/Mobile/prepare-native.py prepare \
  --android-aar Extensions/Platform/Mobile/VeroScriptAndroid/build/outputs/aar/VeroScriptAndroid-release.aar \
  --ios-sdk dist/mobile/ios --output dist/csharp-mobile/native
python3 Extensions/Language/CSharp/Resources/Tools/Mobile/prepare-native.py verify \
  dist/csharp-mobile/native
dotnet pack Extensions/Language/CSharp/VeroScriptCSharp/VeroScriptCSharp.csproj \
  -c Release -p:VeroScriptIncludeMobile=true \
  -p:VeroScriptMobileNativeRoot=/absolute/path/to/native \
  -o /absolute/path/to/mobile-nuget
```

Gradle이 다른 이름으로 출력하면 실제 AAR 파일명을 사용하세요. Android는 구성된 SDK/NDK/Gradle, iOS는 macOS 및 전체 Xcode가 필요합니다. 모바일 관리 대상에는 .NET 10 Android/iOS 워크로드가 필요합니다. 준비용 인터프리터 이름이 `python3`가 아니면 `VeroScriptMobilePython`으로 지정하세요. 준비 과정은 SDK를 다운로드하거나 앱에 서명하지 않습니다.

## 통합 계약

준비 단계는 원본 헤더, 표준 라이브러리, 여섯 언어 카탈로그, 재배포 고지 및 SHA-256 파일 목록을 검증합니다. Android ID/API 및 16 KB ELF LOAD 정렬을 확인하고 두 ABI의 `libVeroScriptInterop.so`와 호환되는 `libc++_shared.so`를 보존합니다. iOS 정적 아카이브 객체의 기기/시뮬레이터 아키텍처 및 최소 OS를 검사합니다. iOS SDK에는 Foundation/libc++가 필요합니다. 기존 출력 디렉터리는 거부합니다.

NuGet은 `buildTransitive/VeroScript.CSharp.targets`를 자동으로 가져옵니다. 소스 `ProjectReference` 사용자는 이 파일을 가져오고 `VeroScriptMobileNativeRoot`를 제공해야 합니다. Android 라이브러리/자산 및 iOS의 `__Internal`, `NativeReference`, `ForceLoad`, 생성된 `ReferenceNativeSymbol` 유지를 보존합니다. 참조 해석 후 전이 참조를 포함한 실제 iOS 바인딩 어셈블리를 확인하고 데스크톱/Android 어셈블리는 거부합니다. 표준 라이브러리와 카탈로그는 앱 리소스이며 바이트코드 캐시는 앱이 관리하는 쓰기 가능한 디렉터리를 사용합니다.

NuGet은 준비된 루트의 상대 구조를 `native/` 아래에 유지하며 `android/<abi>`, `ios`, `assets`, `licenses`를 포함합니다. 패키징 중 경로 요소를 중복하지 마세요. MSBuild 대상은 `OutputType=Exe` 또는 SDK 추론 후 Android의 `AndroidApplication=true`로 앱을 판별합니다. Android 앱의 추론 출력 형식이 `Library`여도 네이티브 라이브러리와 MAUI 자산 선택을 유지하며 일반 클래스 라이브러리에는 적용하지 않습니다.

리소스 설치는 경로와 해시를 검증하고 대소문자가 정확한 `stdlib/core.vs`를 요구합니다. 스트림을 해제하고 재시도 시 오래된 파일을 복구합니다. 실패 후 검증된 파일이 남을 수 있으므로 실행 전에 다시 설치하세요. 비동기 설치 완료 후 동일한 작업 스레드에서 세션을 생성, 설정, 사용 및 해제합니다. VM을 명시적으로 선택하세요. 패키지는 네이티브 JIT를 제공하지 않습니다.

[앱 통합](../../User/ko-KR/mobile.md)

## 릴리스 검증

이식 가능한 C# 테스트는 동적 및 `IOS` 정적 가져오기 프로필을 네이티브 내보내기 목록과 대조합니다. 리소스 손상, 경로 이탈, 취소, 스트림 해제, 오래된 파일 복구 및 실제 표준 라이브러리/카탈로그 설치를 검사합니다. Python 테스트는 SDK 준비, MSBuild 자산 선택, 호환되지 않는 RID/OS, 불완전한 대상 구성 및 해석된 iOS 어셈블리의 부적합 판정을 검증합니다. 이는 호스트 테스트입니다.

안정적인 모바일 릴리스 전에 MAUI 예제를 Release로 Android 에뮬레이터/기기 및 iOS 시뮬레이터/기기에서 빌드하고 실행하세요. Android 16 KB 페이지 환경, 종속성과 APK ZIP 정렬, iOS 서명/링크, AOT/트리밍, 취소, 콜백, 리소스 가져오기 및 콜드 시작을 포함해야 합니다. 도구 버전, 패키지 검사 및 실제 결과를 기록하세요. 대상 플랫폼의 검증이 완료될 때까지 모바일 지원은 실험적입니다.
## 배포 문서

모바일 패키지는 기능 파일, 런타임 리소스와 라이선스를 포함합니다. README, 설명서와 예제는 제외하며 통합 User 문서는 별도 Resources 배포물에서 받습니다. 네이티브 리소스 경로와 기기 검증 요구 사항은 유지됩니다.

## 데스크톱 및 모바일 공통 NuGet 피드

위 명령은 모바일 지원 `VeroScript.CSharp` 패키지를 준비합니다. 데스크톱 또는 모바일 컬렉션에 CSharp를 게시할 때 등록된 C# 패키지 ID 여섯 개와 각각의 `.nupkg`, `.snupkg`를 담은 정본 피드를 준비합니다. `VEROSCRIPT_CSHARP_PACKAGE_DIRECTORY`로 피드를 선택합니다. 이미 만든 패키지를 `package-veroscript-mobile-release.py`에서 재사용하려면 같은 모바일 지원 `VeroScript.CSharp` 파일을 `--csharp-package`로 전달합니다. 모든 대상에서 같은 바이트를 재사용합니다. 전체 입력과 예제는 [모바일 게시 절차](../../../../../../../Resources/Tools/Packaging/README.md#mobile-sdk-tooling)를 확인하십시오.

<!-- docs-navigation -->

---

[개발자 문서](../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

[en-US](../en-US/mobile.md) · [fr-FR](../fr-FR/mobile.md) · [ja-JP](../ja-JP/mobile.md) · [ko-KR](mobile.md) · [zh-CN](../zh-CN/mobile.md) · [zh-TW](../zh-TW/mobile.md)
