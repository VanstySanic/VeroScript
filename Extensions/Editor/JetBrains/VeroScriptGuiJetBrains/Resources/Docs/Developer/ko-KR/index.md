# Rider GUI 추가 기능 개발

## 소유 범위와 의존성

[플러그인 설명자](../../../META-INF/plugin.xml)가 식별자와 기본 플러그인에 대한 필수 종속성을 정의합니다. 기본 VeroScript 프로젝트에서 Runtime 탐색, 컴파일러 설정 및 VS 어휘 강조를 가져오며 해당 클래스를 다시 패키징하지 않습니다. [Gradle localPlugin 프로젝트 종속성](https://plugins.jetbrains.com/docs/intellij/tools-intellij-platform-gradle-plugin-dependencies-extension.html#local-plugin)은 개발, 테스트 및 IDE 샌드박스에 사용합니다. 설치 ZIP 두 개는 별도로 유지합니다.

JDK/JVM 25, Gradle 9.5.0, Kotlin 2.4.0, IntelliJ Platform Gradle 플러그인 2.19.0을 사용합니다.
Rider SDK는 2026.2.2, 최소 빌드는 `262.10315.191`, 엄격한 상한 계열은 `262.*`입니다.
다른 계열은 별도 검증이 필요합니다. 네이티브 도구나 공급자를 묶지 않습니다.
MIT 라이선스는 `META-INF/LICENSE`, Wrapper 원문 고지는 `Resources/Licenses`에 있습니다.

## 설계와 계약

`gui-compile`은 표준 `.vsapp` 애플리케이션 컨테이너를 출력합니다. Check와 Preview는 이를
네이티브 CLI에 전달하며, CLI가 컨테이너와 Core 바이트코드를 읽습니다.
서로 호환되는 Runtime, VM 라이브러리와 GUI 공급자를 함께 사용합니다.

- `language/`는 대소문자를 구분하는 독립 렉서와 평면 PSI입니다. XML 네임스페이스 검사나
  브라우저 CSS 의미 분석을 도입하지 않습니다. 인라인 코드는 VS/GUI 렉서를 계층화하여 강조합니다.
- `GuiMarkupModel`은 파일별 구조 캐시이며 형식 검사를 하지 않습니다. 편집 중 전체 프로젝트 탐색,
  자동 네이티브 검사 또는 미리 보기를 시작하지 않습니다. 1 MiB 초과나 다중 커서에서는 입력 보조를 생략합니다.
- `Resources/Catalog/gui.json`은 자체 GUI 소스에서 추출한 내장 메타데이터입니다.
  `python Resources/Tools/update-catalog.py`로 갱신하고 차이를 검토하세요. 소스 해시는 변경을 식별합니다.
  `FGuiSchema`를 대체하지 않습니다. 제한된 목록으로 사용자 Schema를 거부하지 말고 Original/Mark,
  상속 속성, 바인딩과 이벤트를 유지하세요.
  `VeroScriptGui.vs`의 로컬 가져오기와 간접 지원 파일을 추적합니다. Mark를 생략하면 선언 이름을
  사용하며, 클래스 경계와 주석/리터럴 마스킹으로 메타데이터가 해당 선언에 속하도록 유지합니다.
  카탈로그 형식 2는 `VS::Gui::Internal`을 포함한 선언의 `qualifiedName`을 보존합니다.
  검색과 문서는 실제 정규화 이름을 사용하며 짧은 이름과 Mark 별칭도 유지합니다.
  카탈로그와 이를 읽는 코드를 함께 생성하고 패키징합니다.
- `run/`은 각 경로의 원래 기준으로 설정을 상속합니다. 인수 순서는 전역 컴파일러/환경 설정(`--path` 포함),
  GUI 백엔드, `gui-*` 작업과 작업 옵션, `--`와 스크립트 인수입니다. 반복 `--asset`과 `int64` 프레임 경계를 유지하세요.
- 검사/베이크는 스크립트를 실행하지 않습니다. 미리 보기 프로세스의 중지/종료는 Rider가 담당합니다.
  백엔드 ID는 선택 사항입니다. GUI DAP 시작은 없으며 준비된 호스트 연결은 기본 플러그인의 담당입니다.
  줄 링크는 ML 코드 포인트 열을 UTF-16 오프셋으로 잘못 변환하지 않습니다.

메타데이터 추출은 `@GuiWidget`, `@ GuiWidget`처럼 `@`와 주석 이름 사이의 유효한 언어 공백을 허용하며 동일한 속성·이벤트·상속 정보를 생성합니다. 노출 형식이 그대로여도 소스 해시가 바뀌면 카탈로그를 갱신하고 변경 내용을 검토해야 합니다.

## 검증과 배포

렉서 token은 각 언어에 속하며 잘린 종료 기호도 지정된 탐색 범위를 벗어나 읽지 않습니다.
인라인 본문은 다음 raw/CDATA 경계에서 멈추며 파일 나머지를 반복해서 탐색하지 않습니다.
문자 읽기 횟수 회귀 사례로 선형 탐색을 확인하고 장치별 실행 시간 임계값은 사용하지 않습니다.
자동 완성은 확정 문자 `=`/`:`를 소비하고 기존 값을 보존하며 Tab으로 전체 이름을 교체합니다.
일반 로컬 `src`는 `GuiStringLiteralManipulator`로 경로 편집/이름 변경을 지원합니다.
삽입 내용을 이스케이프하고 닫히지 않은 따옴표를 보존합니다. 엔티티 경로는 로컬 탐색 대상이 아닙니다.
OS 네이티브 절대 경로는 파일 시스템 루트에서 해석하며 재바인딩 시 전체 루트 접두사를 기록합니다.
SDK의 프로젝트 루트 기준을 사용하지 않으며 상대 경로는 선언 파일 기준을 유지합니다.
SDK의 프로젝트 루트 방식 경로 변환은 비활성화합니다.
네이티브 구분자만 변환하여 POSIX 파일 이름의 역슬래시를 보존합니다.
참조 디코딩은 파일 이름의 `?`와 SDK의 macOS NFC 정규화를 유지합니다.
상대 경로 재바인딩은 SDK 동작을 유지하며 상대 경로를 만들 수 없으면 참조 텍스트를 변경하지 않습니다.
볼륨/파일 시스템 간 이동은 별도 검토와 검증이 필요하며 자동 이전 지원으로 표시하지 않습니다.
문서 대상은 편집된 스냅샷을 거부하고 스타일 문서는 속성 token에만 적용합니다.
컨텍스트 작업은 작업/진입 파일과 맞지 않는 템플릿 옵션을 지우고 적용 가능한 사용자 지정 출력은 유지합니다.
정적 검사는 자체 입력만 탐색하며 Gradle SDK 캐시나 샌드박스를 순회하지 않습니다.

이 디렉터리에서 실행하세요.

```sh
python -B Resources/Tests/test_catalog.py
python -B Resources/Tools/check-static.py
./gradlew :test :verifyPluginProjectConfiguration :verifyPluginStructure :buildPlugin
./gradlew :veroscript:buildPlugin
./gradlew :verifyPlugin
```

Windows에서는 `gradlew.bat`, POSIX에서는 필요시 `bash ./gradlew`를 사용합니다.
`-PlocalIdePath=/absolute/path/to/Rider`로 설치된 SDK를 선택합니다. 명시적 루트 작업은
모든 하위 프로젝트 테스트를 실수로 선택하는 일을 방지합니다. GUI 출력은 독립적인
`VeroScriptGuiJetBrains-0.1.1.zip`입니다. 통합 IDE 패키징은 GuiJetBrains를 독립 제품으로 등록하고,
기본 플러그인도 빌드한 뒤 각 제품 버전에 맞춰 두 로컬 ZIP을 함께 검증합니다.
독립 개발 빌드에는 위 명령을 계속 사용할 수 있습니다.

기본 플러그인이 아직 게시되지 않았다면 `:verifyPlugin`만으로는 Marketplace에서 의존성을 확인할 수 없습니다.
공식 Plugin Verifier CLI로 두 ZIP을 함께 검사하세요. `plugins.txt`에 각 ZIP의 절대 경로를 한 줄씩 적고
아래 명령을 실행합니다. 로컬 기본 플러그인으로 의존성을 확인하며 게시 전에 두 검사 결과를 모두 확인해야 합니다.

```sh
java -jar /absolute/path/to/verifier-cli.jar check-plugin @/absolute/path/to/plugins.txt /absolute/path/to/Rider
```

테스트는 렉서 재시작/불완전 입력, 목록 상속과 스타일 제한, 경로 이동, 옵션 순서/인수 구분,
진입 파일/프레임 제한, 리소스/덮어쓰기, 영속화, 등록과 여섯 언어 리소스를 다룹니다.
GUI Runtime으로 ML/VS/결과물의 Check/Compile/Preview, 이동, 공급자, 공백/비 ASCII 경로,
중지와 진단 링크를 각 OS에서 검증해야 합니다. Rider에서 인라인 강조, 첫 항목 선택, 따옴표/태그,
`src` 탐색과 기본 `.vs` 동작도 확인하세요. SDK 테스트 픽스처는 실제 UI 검증을 대체하지 않습니다.

[에디터 통합 유지보수](../../../../../../../../Resources/Docs/Developer/ko-KR/editors.md#acceptance-evidence)을 참조하세요.

에디터 픽스처는 테스트 전용 번들 SDK 모듈로 일반 Rider 폴더 솔루션을 초기화하고 실제 로컬 파일을 사용합니다.
ReSharper는 테스트 JVM에서만 비활성화합니다. SDK 설정 손상 검사는 초기화된 Rider 설정을 기준으로 유지합니다.

Rider 대화형 UI, 실제 창에서의 Preview/Stop, 이동, 다른 운영체제 및 실제 호스트 통합은 별도 검증이 필요합니다.
이번 결과는 검증한 환경에 적용됩니다.

여섯 언어 UI, User, Developer를 함께 유지하세요. 모듈 매니페스트는 공용 문서 카탈로그에 등록되었습니다.
문서를 수정할 때 출판 검사를 실행하세요.

[사용자 안내](../../User/ko-KR/index.md) · [규칙](../../../../../../../../Resources/Docs/Standards/ko-KR/GENERAL.md)

## 검사/베이크 호출과 저장 경로

`GuiCommand.arguments()`는 `check`와 `compile`에서 `--backend=vm`을 맨 앞에 두어 `--path`가 패키지 환경을 준비하기 전에 실행용 JIT 선택을 억제합니다. 미리 보기는 실행 백엔드나 JIT 재정의를 출력하지 않고 네이티브 기본값을 유지합니다. GUI 백엔드 필드는 별도의 서비스 선택입니다. `GuiCommand.persist()`는 프로젝트 상대 저장 경로에서 `java.io.File.separatorChar`만 `/`로 변환하며 POSIX 파일명의 리터럴 역슬래시는 보존합니다. 옵션 순서, 설정 저장 후 생략/auto/none/사용자 공급자, 네이티브 파일명 왕복을 회귀 대상으로 유지하세요. 각 대상 OS에서 검증해야 하며 Windows 결과로 POSIX를 검증할 수 없습니다.

[Rider SDK](../../../../../VeroScriptJetBrains/Resources/Docs/Developer/ko-KR/index.md#rider-platform-sdk)

<!-- docs-navigation -->

---

[개발자 문서](../../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
