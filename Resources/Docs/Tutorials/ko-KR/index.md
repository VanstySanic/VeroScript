# VeroScript 튜토리얼

이 페이지는 작업 완료를 중심으로 구성되어 있습니다. 각 튜토리얼은 작은 목표로 시작하여 프로그램을 단계별로 성장시키고 연습으로 끝납니다. 언어 규칙 및 전체 API 세부정보를 보려면 [문서](../../User/ko-KR/index.md)를 사용하세요.

## 시작하기 전에

당신은 다음이 필요합니다

1. `veroscript --version`을 통해 사용할 수 있는 VeroScript 명령줄 도구.
2. 스크립트 파일의 작업 디렉터리.
3. 일반 텍스트 파일을 저장할 수 있는 편집기입니다.

코드 블록의 레이블은 언어나 shell을 나타냅니다. `powershell` 명령은 PowerShell에서, `sh`/`bash` 명령은 해당 shell에서 실행하고 OS에 맞게 경로를 바꿉니다. `text`는 예상 출력입니다. 완전한 프로그램은 보통 `int main`과 명시적 종료 상태를 사용하며, 종료 상태가 필요 없는 짧은 예제에는 `void main`도 사용할 수 있습니다.

`--check`는 앱 `main`을 호출하지 않고 프로그램을 검사합니다. 활성화된 [스크립트 컴파일 확장](../../../../VeroScript/Resources/Docs/User/ko-KR/standard-library/compiler.md)은 컴파일러의 한도와 권한 아래 실행될 수 있습니다.

## 추천 경로

### 1단계: 첫 번째 스크립트 실행

- [01: 첫 번째 스크립트](getting-started/01-first-script.md)
- [02: 인수 확인, 실행 및 읽기](getting-started/02-cli-and-arguments.md)

### 2단계: 언어 기본 학습

- [03: 값, 조건 및 루프](language/03-values-and-control-flow.md)
- [04: 함수 및 컬렉션](language/04-functions-and-collections.md)
- [05: 구조체, 클래스 및 인터페이스](language/05-structs-classes-and-interfaces.md)
- [06: 참조, 참조 반환 및 람다 캡처](language/06-references-and-captures.md)
- [07: 모듈, 가져오기 및 전처리기](language/07-modules-and-preprocessor.md)

### 3단계: 유용한 프로그램 구축

- [08: 문자열, 파일 및 경로](standard-library/08-strings-files-and-paths.md)
- [09: 동시성 및 이벤트](standard-library/09-concurrency-and-events.md)
- [20: 이진 패킷 읽기와 복사](standard-library/20-managed-memory.md)
- [22: 앱 텍스트와 스냅샷](standard-library/22-application-i18n.md)

### 4단계: 출시 및 통합

- [10: 패키지 구축 및 애플리케이션 배포](toolchain/10-packages-and-distribution.md)
- [11: 디버깅 및 AOT 생성](toolchain/11-debugging-and-aot.md)
- [12: 호스트 바인딩 및 스크립트 통합](advanced/12-host-bindings.md)
- [13: 런타임 반영](advanced/13-reflection.md)
- [16: WebAssembly SDK 대화형 입력](toolchain/16-web-interactive-input.md)
- [17: 선택적 값, weak 참조 및 공유 구조체 핸들](advanced/17-value-utility-types.md)
- [18: C++ 도구 체인 모듈 사용하기](cpp/index.md)
- [Python 소스 도구](python/index.md)
- [C# 소스 도구](csharp/index.md)
- [Tooling 워크플로: editor와 debugger 연결하기](toolchain/17-tooling-workflow.md)
- [19: 모바일 앱에서 스크립트 실행](toolchain/19-mobile-embedding.md)
- [21: 로컬 패키지 설치](toolchain/21-package-manager.md)
- [23: 라이브러리와 앱 빌드](toolchain/23-application-build.md)
- [24: 컬렉션·탐색·대화 상자](toolchain/24-gui-collections.md)


### 5단계: 프로젝트 표준화

- [14: `std` 표준 네임스페이스 사용](advanced/14-standard-namespace.md)
- [15: 작업 영역 및 진단 현지화 구성](toolchain/15-workspace-and-localization.md)

## 이 튜토리얼을 사용하는 방법

처음 두 단계를 순서대로 완료하세요. 3단계 튜토리얼은 작업 중심이며 필요에 따라 선택할 수 있습니다. 4단계는 전달 또는 삽입 준비가 완료된 스크립트를 위한 것입니다.

모든 튜토리얼은 독립적으로 읽을 수 있지만 `--check`을 건너뛰지 마십시오. 실행 전에 구문, 유형 및 가져오기 문제를 찾습니다. 변화가 작더라도 연습을 하십시오. 입력을 수정하거나 분기를 추가하는 것만으로도 예제를 이해했는지 확인할 수 있습니다.

## 참고자료

- [언어 기본](../../../../VeroScript/Resources/Docs/User/ko-KR/language-basics/index.md): 구문 및 의미.
- [표준 라이브러리 참조](../../../../VeroScript/Resources/Docs/User/ko-KR/standard-library/index.md): 함수 서명 및 경계.
- [툴체인](../../User/ko-KR/toolchain/index.md): CLI, 패키지, 디버깅 및 호스트 통합.
- [실행 가능한 예제 색인](../../../../VeroScript/Resources/Docs/User/ko-KR/examples.md): 주제별로 그룹화된 배포 예제입니다.

<!-- docs-navigation -->

---

[튜토리얼](index.md)

언어: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
