# VeroScript 문서

[GitHub 소스 코드](https://github.com/VanstySanic/VeroScript)

VeroScript는 임베딩을 위한 스크립트 언어입니다. 작업을 순서대로 배우려면 튜토리얼을, 문법과 공개 API, 설정, 제한을 확인하려면 참조 문서를 선택하세요.

크로스 플랫폼 배포는 Windows, Linux, macOS용 데스크톱 Runtime 및 SDK, 실험 단계의 Android/iOS 임베딩 어댑터, 브라우저와 기타 JavaScript 호스트용 WebAssembly SDK를 제공합니다. 대상 OS와 CPU 아키텍처에 맞는 패키지를 선택하고, 의존성과 사용 가능한 백엔드는 각 모듈 가이드에서 확인하세요.

## 읽기 시작

[튜토리얼](../../Tutorials/ko-KR/index.md) · [소개](../../../../VeroScript/Resources/Docs/User/ko-KR/introduction.md) · [언어 기초](../../../../VeroScript/Resources/Docs/User/ko-KR/language-basics/index.md) · [표준 라이브러리](../../../../VeroScript/Resources/Docs/User/ko-KR/standard-library/index.md) · [언어 참조](../../../../VeroScript/Resources/Docs/User/ko-KR/language-reference/index.md) · [예제](../../../../VeroScript/Resources/Docs/User/ko-KR/examples.md) · [도구 체인](toolchain/index.md)

예제 목록의 프로그램은 완전한 스크립트입니다. 짧은 참조 코드에는 주변 선언이 필요할 수 있습니다. 필요한 구성 요소, 버전, 지원 대상은 선택한 모듈 가이드에서 확인하세요.

## 모듈 참조

| 분야 | 참조 |
| --- | --- |
| 언어와 도구 | [VeroScript](../../../../VeroScript/Resources/Docs/User/ko-KR/index.md) · [CLI](../../../../VeroScriptCli/Resources/Docs/User/ko-KR/index.md) · [Tooling](../../../../VeroScriptTooling/Resources/Docs/User/ko-KR/index.md) |
| 호스트 SDK | [Interop](../../../../VeroScriptInterop/Resources/Docs/User/ko-KR/index.md) · [C++](../../../../Extensions/Language/Cpp/Resources/Docs/User/ko-KR/index.md) · [Python](../../../../Extensions/Language/Python/Resources/Docs/User/ko-KR/index.md) · [C#](../../../../Extensions/Language/CSharp/Resources/Docs/User/ko-KR/index.md) · [WebAssembly](../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/ko-KR/index.md) |
| 실행 엔진 | [Bytecode VM](../../../../Modules/Execution/VeroScriptBytecodeVm/Resources/Docs/User/ko-KR/index.md) · [AsmJit](../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/User/ko-KR/index.md) |
| 패키지와 애플리케이션 | [PackageManager](../../../../VeroScriptPackageManager/Resources/Docs/User/ko-KR/index.md) · [Application](../../../../VeroScriptApplication/Resources/Docs/User/ko-KR/index.md) · [Build](../../../../VeroScriptBuild/Resources/Docs/User/ko-KR/index.md) · [Installer](../../../../VeroScriptInstaller/Resources/Docs/User/ko-KR/index.md) |
| 에디터 | [VS Code](../../../../Extensions/Editor/VSCode/VeroScriptVSCode/Resources/Docs/User/ko-KR/index.md) · [VS Code GUI](../../../../Extensions/Editor/VSCode/VeroScriptGuiVSCode/Resources/Docs/User/ko-KR/index.md) · [Rider](../../../../Extensions/Editor/JetBrains/VeroScriptJetBrains/Resources/Docs/User/ko-KR/index.md) · [Rider GUI](../../../../Extensions/Editor/JetBrains/VeroScriptGuiJetBrains/Resources/Docs/User/ko-KR/index.md) |
| 임베디드 플랫폼 | [Mobile](../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/User/ko-KR/index.md) · [Android](../../../../Extensions/Platform/Mobile/VeroScriptAndroid/Resources/Docs/User/ko-KR/index.md) · [iOS](../../../../Extensions/Platform/Mobile/VeroScriptIOS/Resources/Docs/User/ko-KR/index.md) |
| 데이터와 네트워크 | [JSON](../../../../Modules/Network/VeroScriptJson/Resources/Docs/User/ko-KR/index.md) · [Network](../../../../Modules/Network/VeroScriptNetwork/Resources/Docs/User/ko-KR/index.md) · [HTTP](../../../../Modules/Network/VeroScriptHttp/Resources/Docs/User/ko-KR/index.md) · [WebSocket](../../../../Modules/Network/VeroScriptWebSocket/Resources/Docs/User/ko-KR/index.md) · [Boost](../../../../Modules/Network/Backend/VeroScriptBoost/Resources/Docs/User/ko-KR/index.md) · [Curl](../../../../Modules/Network/Backend/VeroScriptCurl/Resources/Docs/User/ko-KR/index.md) |
| 멀티미디어와 GUI | [Platform](../../../../Modules/Multimedia/VeroScriptPlatform/Resources/Docs/User/ko-KR/index.md) · [Graphics](../../../../Modules/Multimedia/VeroScriptGraphics/Resources/Docs/User/ko-KR/index.md) · [Audio](../../../../Modules/Multimedia/VeroScriptAudio/Resources/Docs/User/ko-KR/index.md) · [Image](../../../../Modules/Multimedia/VeroScriptImage/Resources/Docs/User/ko-KR/index.md) · [Font](../../../../Modules/Multimedia/VeroScriptFont/Resources/Docs/User/ko-KR/index.md) · [Gui](../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/User/ko-KR/index.md) · [Sdl3](../../../../Modules/Multimedia/Backend/VeroScriptSdl3/Resources/Docs/User/ko-KR/index.md) · [Stb](../../../../Modules/Multimedia/Backend/VeroScriptStb/Resources/Docs/User/ko-KR/index.md) · [FontBackend](../../../../Modules/Multimedia/Backend/VeroScriptFontBackend/Resources/Docs/User/ko-KR/index.md) · [RmlUi](../../../../Modules/Multimedia/Backend/VeroScriptRmlUi/Resources/Docs/User/ko-KR/index.md) |

[타사 고지와 원본 라이선스](../../../Licenses/README.md)

<!-- docs-navigation -->

---

[사용자 문서](index.md)

언어: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
