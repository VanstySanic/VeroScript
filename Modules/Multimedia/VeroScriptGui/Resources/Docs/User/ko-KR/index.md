# GUI

Gui는 위젯 트리, 빌더, 속성, 이벤트와 ML/CSS를 제공합니다. RmlUi는 별도로 선택하는 레이아웃 및 렌더링 구현입니다.

- [프레임워크와 RmlUi 통합](Reference/Framework.md)
- [마크업과 스타일](Reference/Markup.md)

`vs-gui` / `vs-rmlui`. CMake: `VeroScriptGui` / `VeroScriptRmlUi`; VS: `VS::Gui`.

[VS Code](../../../../../../../Extensions/Editor/VSCode/VeroScriptGuiVSCode/Resources/Docs/User/ko-KR/index.md) · [Rider](../../../../../../../Extensions/Editor/JetBrains/VeroScriptGuiJetBrains/Resources/Docs/User/ko-KR/index.md)

`gui-compile`에는 Application 지원이 필요하며 표준 ZIP `.vsapp` 응용 프로그램 컨테이너를 생성합니다. 시작 설명, 컴파일된 GUI 이미지, 리소스 메타데이터와 명시적으로 포함한 자산을 보존합니다. GUI 검사/미리 보기와 일반 CLI 검사/실행/inspect는 같은 컨테이너를 읽습니다. GUI 스크립트를 지원하는 Runtime에서도 실행할 수 있습니다. 이전 원시 GUI `.vsapp` 파일은 다시 컴파일하십시오. 백엔드는 설치 환경의 서비스 선택 규칙을 따르며 호스트, 프로젝트 설정 및 명령 옵션으로 재정의할 수 있습니다. 편집기는 RmlUi를 고정하지 않습니다.

[튜토리얼: 컬렉션, 탐색 및 대화 상자](../../../../../../../Resources/Docs/Tutorials/ko-KR/toolchain/24-gui-collections.md)

자동 선택, 명시 선택 및 비활성화의 공통 규칙은 다음을 참조하세요: [서비스 백엔드 선택](../../../../../../../VeroScript/Resources/Docs/User/ko-KR/introduction.md#backend-selection).

<!-- docs-navigation -->

---

[사용자 문서](../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
