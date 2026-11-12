# iOS — 개발자 문서

iOS는 Objective-C/Objective-C++ 어댑터와 정적 XCFramework 통합을 관리합니다. UI 전달과 수명 주기는 어댑터/호스트에 두며 각 slice의 기기/시뮬레이터 식별을 유지합니다.

공통 임베디드 빌드, 수명, 패키지 출력과 현지화 책임은 [Mobile](../../../../../VeroScriptMobile/Resources/Docs/Developer/ko-KR/index.md)에서 관리합니다.

```text
python Resources/Tools/Packaging/package-veroscript-mobile.py ios --out-root dist/mobile/ios
```

iOS는 macOS/Xcode, 세 slice와 각 객체의 `LC_BUILD_VERSION`/CPU/최소 iOS 검사가 필요하며 macOS/혼합 아카이브를 거부합니다. 실제 Objective-C/Swift 링크, 서명, 백그라운드/복귀, 취소와 측정은 릴리스 조건입니다.

[사용자 문서](../../User/ko-KR/index.md)


## 모바일 리소스와 언어

공통 모바일 카탈로그 목록, 배포 경로와 명시적 로드 절차는 [Mobile](../../../../../VeroScriptMobile/Resources/Docs/Developer/ko-KR/index.md#mobile-localization)에서 관리합니다.

번역 버퍼 소유권, 오류와 내보내기 유지보수는 [Interop](../../../../../../../../VeroScriptInterop/Resources/Docs/Developer/ko-KR/abi-and-lifetime.md#localization-abi)에 정의합니다.

<a id="ios-acceptance-evidence"></a>

정확한 소스/산출물 식별, Xcode/SDK 버전, 최소 배포 OS, 실행 OS와 장치/시뮬레이터 아키텍처별로 증거를 유지합니다. CMAKE_SYSTEM_NAME=iOS와 알맞은 iphoneos/iphonesimulator sysroot로 장치 arm64와 시뮬레이터 arm64/x86_64를 각각 빌드하며 임베디드 빌드는 정적 Interop을 사용합니다. 병합 아카이브와 XCFramework를 확인한 뒤 Objective-C++, Swift, C ABI, 정적 AOT 앱에서 소비합니다. 실제 앱에서 import, NSError/팩터리 브리지, Bundle 리소스, 쓰기 가능한 캐시 재사용, 권한 거부, 취소, 작은 스택 파싱/복구, ARC와 백그라운드/복귀를 확인합니다. 장치 slice 컴파일은 실제 장치 실행이 아니며 시뮬레이터 서명이나 화면은 장치 서명·네이티브 Metal·성능을 검증하지 않습니다. iOS 15.0 대상 컴파일도 iOS 15 실행 검증이 아닙니다. 실행 기록과 소스 차이는 날짜가 포함된 현행 매뉴얼 대신 임시 증거에 보존하고 기존 시뮬레이터 기록으로 이후 빌드를 자동 승인하지 마세요.

<!-- docs-navigation -->

---

[개발자 문서](../../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
