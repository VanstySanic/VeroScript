# Android — 개발자 문서

Android는 Java/JNI 및 AAR/Prefab 어댑터를 관리합니다. UI 전달과 수명 주기는 어댑터/호스트에 둡니다. JNI는 표준 UTF-8 바이트 배열과 명시적 카탈로그 로드/언어 선택을 사용하며 번역 예외에 JNI modified UTF-8을 사용하지 않습니다. 데스크톱 JNI smoke는 실제 JVM과 테스트 AssetManager를 사용하며 Android 기기 검증을 대신하지 않습니다.

공통 임베디드 빌드, 수명, 패키지 출력과 현지화 책임은 [Mobile](../../../../../VeroScriptMobile/Resources/Docs/Developer/ko-KR/index.md)에서 관리합니다.

```text
python Resources/Tools/Packaging/package-veroscript-mobile.py android --out-root dist/mobile/android
```

Android는 JDK 17, Gradle 8.11.1, AGP 8.9.2, SDK 35, NDK 28.2.13676358, CMake 3.31.6이 필요합니다. 양 ABI JNI/Interop/libc++, Android/API 식별과 16 KB ELF LOAD 정렬을 검사합니다. 기기/에뮬레이터, R8, 최종 APK ZIP 정렬도 검증해야 합니다.

[사용자 문서](../../User/ko-KR/index.md)


## 모바일 리소스와 언어

공통 모바일 카탈로그 목록, 배포 경로와 명시적 로드 절차는 [Mobile](../../../../../VeroScriptMobile/Resources/Docs/Developer/ko-KR/index.md#mobile-localization)에서 관리합니다.

번역 버퍼 소유권, 오류와 내보내기 유지보수는 [Interop](../../../../../../../../VeroScriptInterop/Resources/Docs/Developer/ko-KR/abi-and-lifetime.md#localization-abi)에 정의합니다.

[Android AAR 대상 검증](device-validation.md)

기본 Android AAR은 embedded-only 빌드에서도 Application 지원(`VEROSCRIPT_BUILD_APPLICATION=ON`)을 명시적으로 켭니다. LICENSE 및 NDK 고지와 함께 `assets/veroscript/licenses/MINIZ-LICENSE.txt`, `JSON-LICENSE.txt`, `SHA256-LICENSE.txt`를 보존하세요. 모바일 검증기는 이를 요구합니다. 애플리케이션 컨테이너 지원이 패키지 설치 클라이언트나 데스크톱 도구를 켜지는 않습니다.

<a id="jni-host-fixture"></a>

선택적 데스크톱 JNI 검증은 `VEROSCRIPT_TEST_ANDROID_JNI_HOST=ON`, `BUILD_TESTING=ON`과 JAVA_HOME의 실제 JDK를 사용합니다. VeroScriptAndroidHostSmoke를 빌드하고 CTest의 VeroScriptAndroidJniHostSmoke를 실행합니다. 실제 JNI 어댑터와 테스트 AssetManager로 `-Xcheck:jni` 아래 UTF-8/NUL, import, 예외, 취소와 해제를 확인합니다. Android ABI/API, APK나 장치 검증은 아닙니다. 대상 검증은 [장치 안내](device-validation.md)를 참조하세요.

<!-- docs-navigation -->

---

[개발자 문서](../../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
