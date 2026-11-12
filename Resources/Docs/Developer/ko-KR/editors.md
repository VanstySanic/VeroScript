# 에디터 통합 유지보수

이 가이드는 에디터 클라이언트와 네이티브 실행/프로토콜 호스트의 공통 경계를 관리합니다. 클라이언트 구현과 리소스 형식은 아래 모듈 가이드에 둡니다.

에디터는 설정, 전송, IDE 표시를 담당하고 의미 분석과 LSP/DAP 계약은 네이티브 Tooling/Core가 관리합니다. 카탈로그는 메타데이터이며 실행 바인딩이 아닙니다. domain 테스트는 프로토콜 fixture로 VM 안전점이나 앱/UE 호스트를 검증하지 않습니다. 실행 전 Session 브리지를 설치하고 호스트가 단일 컨텍스트 직렬화와 취소를 소유합니다. `jitCapability`는 서비스 계약입니다. 일반 Run은 호환 모듈 경로를 명시하거나 CLI 모듈 환경의 선택을 상속할 수 있습니다(미설정 시 기본값 auto). LSP/Check/로컬 VM 디버깅은 JIT를 로드하지 않습니다.

UI와 문서의 여섯 언어를 함께 관리하고 명령 옵션, 메시지 ID와 프로토콜 값을 유지합니다. 클라이언트가 실제로 사용하는 포맷터로 자리표시자를 검증합니다. User는 작업을, Developer는 구조와 빌드, 출판을 설명합니다. SDK, Runtime 및 플랫폼 호환성은 선택한 버전과 실제 검증 기록으로 확인합니다.

## 클라이언트별 구현

- [VS Code](../../../../Extensions/Editor/VSCode/VeroScriptVSCode/Resources/Docs/Developer/ko-KR/index.md)
- [Rider](../../../../Extensions/Editor/JetBrains/VeroScriptJetBrains/Resources/Docs/Developer/ko-KR/index.md)
- [VS Code GUI](../../../../Extensions/Editor/VSCode/VeroScriptGuiVSCode/Resources/Docs/Developer/ko-KR/index.md)
- [Rider GUI](../../../../Extensions/Editor/JetBrains/VeroScriptGuiJetBrains/Resources/Docs/Developer/ko-KR/index.md)

## 공통 네이티브 계약

- [Tooling / LSP / DAP](../../../../VeroScriptTooling/Resources/Docs/Developer/ko-KR/workspace-and-debug.md)
- [C++ Session / DAP](../../../../Extensions/Language/Cpp/Resources/Docs/User/ko-KR/session-debugging.md)

<a id="acceptance-evidence"></a>

검증마다 OS/아키텍처, 편집기와 Runtime 버전, 전체 소스 식별 정보, 패키지 해시, 실제 결과와 건너뛴 항목을 기록합니다. 네이티브 실행에는 일치하는 Runtime을 사용한 별도 검증이 필요합니다. 과거 결과는 기록된 산출물과 환경에만 적용됩니다.

<a id="editor-icon-inputs"></a>

## 공유 아이콘 입력

공통 윤곽과 출력 계열은 `Resources/Tools/Assets/generate-icons.cjs`에서 관리합니다. 제품 이미지는 `#60A5FA`, 파일 표시는 `#5D75AE`를 사용하고 편집기 글리프는 각 테마의 색상과 배치를 유지합니다. 전체 재생성은 제품 및 파일의 SVG, PNG, ICO, ICNS와 편집기 SVG를 갱신합니다. `--brand-only`는 제품 계열을 갱신합니다. Node.js, `sharp`, `--sharp-module`의 사용법은 [자산 재생성 절차](../../../Assets/icons/README.md#regeneration)를 확인하십시오. Runtime 패키징은 관리되는 파일을 사용합니다.

VS Code 확장 빌드는 제품 PNG에서 표시용 PNG를 갱신합니다. JetBrains의 V가 포함된 SVG는 공유 생성기가 관리하며 Gradle은 플러그인 목록 아이콘을 `META-INF`에, 다른 아이콘을 `Icons`에 배치합니다. UI 언어에 따라 경로, 테마 접미사 및 매니페스트 참조를 바꾸지 않습니다. 복사와 패키징의 세부 사항은 각 모듈 가이드에서 관리합니다.

VS Code는 모듈 루트 README를 확장 세부 정보 페이지로 포함하고 JetBrains는 플러그인 설명자의 설명 및 공급자 필드를 사용합니다. 제품 소개를 간결하게 유지하고 여섯 언어 문서 선택 페이지를 연결합니다. 공개 설명서와 유지보수 절차는 기존 정본과 게시 역할을 유지합니다.

<!-- docs-navigation -->

---

[문서 유지보수](index.md)

[en-US](../en-US/editors.md) · [fr-FR](../fr-FR/editors.md) · [ja-JP](../ja-JP/editors.md) · [ko-KR](editors.md) · [zh-CN](../zh-CN/editors.md) · [zh-TW](../zh-TW/editors.md)
