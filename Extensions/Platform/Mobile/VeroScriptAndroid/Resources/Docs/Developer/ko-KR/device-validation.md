# Android AAR 대상 검증

## 전제 조건과 빌드

이 독립 앱은 빌드한 기본 AAR을 Java/JNI와 C11/C++17 Prefab으로 소비하며 두 번째 Core를 빌드하지 않습니다. 기본 release APK는 R8을 켜고 이 검증 앱에만 debug 서명 키를 사용합니다. Python 3.11+와 [Android 빌드 안내](index.md)의 JDK·Gradle·SDK·NDK·CMake 버전을 사용하고 일치하는 SDK 산출물을 새 출력 디렉터리에 먼저 빌드합니다. `--with-asmjit`는 선택적 JIT AAR도 준비합니다.

```text
python Resources/Tools/Packaging/package-veroscript-mobile.py android --with-asmjit --out-root dist/mobile/android-acceptance --gradle <Gradle-8.11.1-executable>
```

## 명시한 대상에서 실행

에뮬레이터를 시작하거나 디버깅을 허용한 장치를 연결한 뒤 `adb devices`의 정확한 시리얼을 선택합니다. 일치하는 기본 AAR을 제공하세요. 예제는 설치 ABI로 x86_64를 명시합니다.

```text
python Extensions/Platform/Mobile/VeroScriptAndroid/Resources/Tests/Device/run.py --aar <absolute-base.aar> --sdk <Android-Sdk> --gradle <Gradle-8.11.1-executable> --java-home <JDK-17> --serial <adb-serial> --abi x86_64 --out <report-directory>
```

선택적 백엔드와 제어 네이티브 실행 카운터는 `--jit-aar <absolute-jit.aar>`를 추가합니다. ARM64는 `--abi arm64-v8a`, Bionic `sysconf` 페이지 크기 확인은 `--page-size 4096` 또는 `--page-size 16384`, 비축소 변형은 `--debug`를 사용합니다. API 26과 이후 버전, 두 페이지 크기를 구성별 새 출력에서 검증합니다. JNI 검증 시 CheckJNI는 테스트 에뮬레이터에서 별도로 켜세요.

## 결과와 검증 범위

실행기는 [빌드 설정](../../../../Resources/Tests/Device/build.gradle.kts)에 선언된 테스트 앱과 그 데이터만 제거/교체하고 선택한 장치/에뮬레이터는 종료하지 않습니다. APK, 로그, `commands.json`, `device-smoke.json`을 보존합니다. 최종 APK 16 KB ZIP 정렬, 반환한 사례와 `INSTRUMENTATION_CODE: -1`을 확인하므로 adb 종료 코드만으로는 통과하지 않습니다. 시도 시작 시 `acceptancePassed`를 false로 하고 요청한 ABI/페이지 크기를 포함한 모든 검사 뒤에만 true로 바꿉니다.

사례는 자산 import/Unicode 이름, UTF-8/NUL 출력, float32 반올림, 메모리/실행/출력 제한, 캐시, 권한, 복구, 명시적 백엔드, 취소, 동시 실행 거부, 종료, 현지화와 JNI AOT 카탈로그 수명을 다룹니다. 카탈로그 사례는 생성 AOT 업무 코드를 실행하지 않습니다. 에뮬레이터 네이티브 변환으로 실행한 ARM64 라이브러리는 실제 ARM64 하드웨어 검증이 아닙니다. 실제 대상 정보와 산출물 해시를 보존하세요. Python·C#·그래픽·최종 앱 검증은 별도로 필요합니다.

아래 Python 전용 회귀는 네이티브 명령을 모의하여 실행기 로직만 검증하며 Android 런타임을 검증하지 않습니다. 검증 앱의 존재, 호스트 검사 또는 빌드만으로 대상 검증이 완료되지 않습니다.

```text
python -m unittest discover -s Extensions/Platform/Mobile/VeroScriptAndroid/Resources/Tests/Device -p test_run.py
```

<!-- docs-navigation -->

---

[Android](index.md) · [VeroScript](../../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/device-validation.md) · [fr-FR](../fr-FR/device-validation.md) · [ja-JP](../ja-JP/device-validation.md) · [ko-KR](device-validation.md) · [zh-CN](../zh-CN/device-validation.md) · [zh-TW](../zh-TW/device-validation.md)
