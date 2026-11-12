# 문서 유지보수

[GitHub 소스 코드](https://github.com/VanstySanic/VeroScript)

<a id="documentation-ownership"></a>

## 원본과 관리 책임

| 내용 | 유지보수하는 원본 |
| --- | --- |
| 공통 에디터 통합 | [에디터 통합 유지보수](editors.md) |
| 공통 읽기 순서, 모듈 간 관계, 튜토리얼과 이 프로젝트의 개발 및 기여 규약 | 루트 `Resources/Docs` |
| 모듈 동작, 공개 API, 기본값과 제한 및 구조, 빌드와 유지보수 | 소유 모듈의 `Resources/Docs/User/<locale>` 및 `Developer/<locale>` |
| 멀티미디어 공급자 공통 유지보수 | [멀티미디어 유지보수](multimedia.md) |
| 내보낸 All/User 트리와 ZIP 파일 | 생성물이며 참조된 원본을 수정한 뒤 다시 출판 |

주제와 언어마다 정본 하나를 유지합니다. 진입 페이지는 요약하고 정본으로 연결하며 전체 계약·절차·변경되는 기본값은 소유 위치에 둡니다. 여섯 언어의 병행 정본을 함께 갱신합니다. 공통 절차는 여기, 프로토콜과 SDK 세부 내용은 해당 모듈에 두고 이전 개정은 버전 관리에 보존합니다.

`documentation.json`은 전체 문서의 언어, 모듈 매니페스트와 보조 리소스를 등록합니다. 각 모듈의 `manifest.json`은 User 원본을 안정적인 출판 경로에 연결하며 Developer 페이지는 해당 모듈에 속합니다. 각 소스는 All 컬렉션에 한 번만 출판됩니다. User 패키지는 같은 User/Tutorials 내용을 추출합니다. 출판된 사본은 별도의 유지보수 원본이 아닙니다.

## 현지화와 경로

Overview를 포함해 여섯 언어의 파일명과 의미를 맞춥니다. API 이름, 진단 ID, JSON 키, 명령과 실행 코드는 유지합니다. 이전 검증 결과는 기록된 소스·산출물·환경에만 적용됩니다.

매니페스트는 User 정본을 안정된 출판 경로에 연결합니다. 포함된 페이지/리소스 링크는 언어·앵커·인코딩·쿼리·이미지·참조를 유지한 상대 URL로 바꾸고 코드와 HTML 주석은 보존합니다. 소스 전용 참조는 온라인 저장소를 엽니다. 이전 문서는 버전 관리로 조회하며 출판물에 복사하지 않습니다.

관리하는 문서와 현지화 파일의 형식은 [공통 줄 끝 규칙](../../Standards/ko-KR/GENERAL.md)을 따릅니다. 이 저장소 규칙은 언어와 도구의 CRLF 입력 지원을 바꾸지 않습니다. 출판은 정규화된 복사본을 생성하며 관리하는 정본이나 바이너리 리소스를 다시 쓰지 않습니다.

## 검증과 출판

아래 명령은 저장소 루트에서 실행합니다. 매번 현재 원본에서 다시 통합하므로 소스 페이지를 수정한 뒤 동기화할 필요가 없습니다. 관리하는 원본만 커밋합니다. 생성 중 원본이 바뀌면 출판을 거부하며 성공 또는 실패 후 임시 디렉터리를 정리합니다. CI는 언어별 파일 일치, 목록, 출판 바이트와 링크를 검증합니다. 출판 Markdown/JSON 텍스트는 LF를 사용하며 라이선스 고지 원문, 바이너리 에셋과 원본 바이트는 유지합니다.

```powershell
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
python Resources/Tools/Packaging/veroscript_docs.py publish --audience All --output .tmp/docs-preview
python Resources/Tools/Packaging/veroscript_docs.py verify-publication --audience All --output .tmp/docs-preview
./Resources/Tools/Packaging/package-veroscript-docs.ps1 -OutRoot .tmp/docs-collection-package
./Resources/Tools/Packaging/package-veroscript-docs.ps1 -Audience User -OutRoot .tmp/docs-user-package
```

`build`와 `publish`의 출력은 저장소 `.tmp` 또는 `dist` 안의 전용 빈 하위 디렉터리여야 합니다. 소스 `Release`나 기존 관리 모음 아래로 직접 출력하면 거부되며 Resources는 트랜잭션 방식의 제품 게시 도구로 배포합니다. 미리 보기마다 새 디렉터리를 사용하세요. 독립 ZIP을 다시 내보낼 때는 새 패키지를 준비하고 검증한 뒤 이전 결과물을 교체하는 `package-veroscript-docs.ps1`을 사용하세요.

<a id="integrated-publication-views"></a>

`package-veroscript-docs.ps1`의 기본 All은 현행 Overview, User, Developer, Standards, 전체 Tutorials와 링크 리소스를 포함합니다. `-Audience User`는 참조를 `docs/<locale>/...`, 전체 튜토리얼을 `docs/Tutorials/<locale>/...`에 의존 리소스와 함께 출판합니다. 두 대상 모두 History를 포함하지 않습니다.

이 스크립트는 독립 문서를 내보내며 관리되는 제품 모음에 게시하지 않습니다. 기본 출력은 `dist/docs/<Audience>/<version>/`이고 `-SourceRoot`를 사용하면 `dist/docs/Custom/<version>/`입니다. 다른 전용 출력 디렉터리는 `-OutRoot`로 지정하세요. 전체 Resources 배포는 제품 게시 도구로 User 보기를 조립합니다.

전체 릴리스는 Resources를 통해 User 보기를 `Release/Resources/any/<version>/Resources/Docs/`에 게시하며 `Resources/Scripts`, `Resources/Assets`를 나란히 배치합니다. 현재 Runtime/SDK/확장 압축 파일과 설치 프로그램에는 작성된 문서와 예제를 포함하지 않습니다. 페이로드 정책은 Installer Developer의 [release-layout.md](../../../../VeroScriptInstaller/Resources/Docs/Developer/ko-KR/release-layout.md)를 참조하십시오.

[이 프로젝트의 개발 및 기여 규약](../../Standards/ko-KR/README.md)은 VeroScript 저장소의 개발과 기여에만 적용되며 SDK 사용 요건이나 MIT 라이선스의 추가 조건이 아닙니다. `documentation.json`의 `standardsAudience`는 `Developer`여야 합니다. Standards는 개발자 자료로 `All`에만 포함하며 `User`, Resources의 사용자 문서, Runtime/SDK/확장 배포 파일 또는 설치 관리자에는 포함하지 않습니다. 소스의 저작권 및 라이선스 문구는 보존합니다.

이 프로젝트의 개발 및 기여 규약은 `Resources/Docs/Standards/<locale>`에서 관리합니다. `zh-CN`이 규칙을 정의하고 나머지 다섯 언어는 동등하게 유지합니다. 출판은 원본을 직접 읽습니다. 이 독립 저장소에는 IDE 규칙 복사본이 없습니다. `sync-standards`는 문서 카탈로그로 ValenceStudio 소비자가 발견된 경우에만 관리 참조를 갱신하며, 그 외에는 복사본을 만들지 않습니다.

Python 3.11+가 필요합니다. `check`는 소스와 발견된 규약 사용처를, `check-collection`은 임시 전체 집합을 검증합니다. `build/verify`는 User 추출 미리 보기이고 `publish/verify-publication`은 현재 원본에서 All 또는 User를 출판하고 확인합니다. `-SourceRoot`는 명시한 트리를 내보내며 `-Audience`와 함께 쓸 수 없습니다. 누락되거나 범위를 벗어난 링크, 없는 앵커, 빈 트리, 파일 시스템 링크와 특수 항목을 거부하며 소스와 출력은 겹칠 수 없습니다. ZIP에는 숨김 리소스와 빈 디렉터리가 포함되고 교체 실패 시 이전 아티팩트를 복원합니다. 복원도 실패하면 복구 백업을 보존합니다. 온라인 소스 참조에는 네트워크가 필요하며 HTML 렌더러는 `.md` 링크를 변환하고 제목 ID를 유지해야 합니다. 제품 빌드, 페이로드 검사 및 런타임 인수 결과 기록은 [배포 검증 절차](../../../../VeroScriptInstaller/Resources/Docs/Developer/ko-KR/release-validation.md)를 따르세요.

## 모듈 매니페스트

상위 `Resources/Docs/documentation.json`에서 `$module/` 접두사는 소유한 소스 모듈의 루트를 뜻합니다. `VeroScript` 같은 제품 디렉터리 이름은 유지하며 각 모듈 매니페스트는 계속 자신의 Docs 디렉터리에 대한 상대 경로를 사용합니다. 바깥 디렉터리를 이동하거나 이름을 바꾸면 원본 문서의 명령과 저장소 워크플로의 실제 경로도 갱신합니다. Windows PowerShell 5.1에서 사용하는 스크립트에 비 ASCII 문자가 있으면 UTF-8 BOM을 유지해야 합니다.

```json
{
  "schemaVersion": 1,
  "module": "VeroScriptCli",
  "user": [
    {
      "source": "User/{locale}/index.md",
      "output": "{locale}/toolchain/cli.md"
    },
    {
      "source": "User/{locale}/packages.md",
      "output": "{locale}/toolchain/packages.md"
    }
  ],
  "developer": true
}
```

## 문서 목적

User는 사용자 작업·공개 API·설정·제한, Developer는 구조·구현 책임·빌드·검증·출판을 다룹니다. Tutorials는 전제·단계·예상 결과로 가르치고 Standards는 이 프로젝트의 개발과 기여에 대해 승인된 규칙만 담습니다. 이전 계획과 증거는 버전 관리 또는 임시 출력에 보관하며 변경된 작업 공간의 인증으로 삼지 않습니다.

본문, 제목, 파일명에 달력 날짜나 불필요한 개인 식별자를 넣지 않습니다. 일반 설명은 현재 릴리스 번호를 반복하지 않고 동작을 설명합니다. 호환성, 도구 요구 사항, ABI·형식 계약, 실행 가능한 예제, 산출물 선택 또는 원본 라이선스 고지에 필요한 정확한 버전은 유지합니다. 변경되는 식별자를 복사하기보다 관리되는 메타데이터를 연결합니다. 날짜가 있는 로그와 검증 기록은 무시되는 임시 출력에 보관합니다. User에는 테스트 방법이나 내부 구현을 반복하지 않습니다.

## 링크와 독자 구분

User의 상대 링크는 User 보기, Tutorials 또는 등록된 보조 리소스 안에서 해석되어야 합니다. User에서 Developer 또는 Standards로 향하는 상대 링크는 All과 User 게시를 모두 실패하게 합니다. 서버 구현, 유지보수 및 검증 절차는 Developer에, 사용자가 관찰할 수 있는 동작은 User에 둡니다. Developer는 해당 User 계약을 링크할 수 있습니다. 모듈 간 링크를 바꾼 후에는 원본 문서 모음과 두 게시 패키지를 모두 검사하세요.

## 모듈 참조

| 분야 | 참조 |
| --- | --- |
| 언어와 도구 | [VeroScript](../../../../VeroScript/Resources/Docs/Developer/ko-KR/index.md) · [CLI](../../../../VeroScriptCli/Resources/Docs/Developer/ko-KR/index.md) · [Tooling](../../../../VeroScriptTooling/Resources/Docs/Developer/ko-KR/index.md) |
| 호스트 SDK | [Interop](../../../../VeroScriptInterop/Resources/Docs/Developer/ko-KR/index.md) · [C++](../../../../Extensions/Language/Cpp/Resources/Docs/Developer/ko-KR/index.md) · [Python](../../../../Extensions/Language/Python/Resources/Docs/Developer/ko-KR/index.md) · [C#](../../../../Extensions/Language/CSharp/Resources/Docs/Developer/ko-KR/index.md) · [WebAssembly](../../../../Extensions/Platform/WebAssembly/Resources/Docs/Developer/ko-KR/index.md) |
| 실행 엔진 | [Bytecode VM](../../../../Modules/Execution/VeroScriptBytecodeVm/Resources/Docs/Developer/ko-KR/index.md) · [AsmJit](../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/Developer/ko-KR/index.md) |
| 패키지와 애플리케이션 | [PackageManager](../../../../VeroScriptPackageManager/Resources/Docs/Developer/ko-KR/index.md) · [Application](../../../../VeroScriptApplication/Resources/Docs/Developer/ko-KR/index.md) · [Build](../../../../VeroScriptBuild/Resources/Docs/Developer/ko-KR/index.md) · [Installer](../../../../VeroScriptInstaller/Resources/Docs/Developer/ko-KR/index.md) |
| 에디터 | [VS Code](../../../../Extensions/Editor/VSCode/VeroScriptVSCode/Resources/Docs/Developer/ko-KR/index.md) · [VS Code GUI](../../../../Extensions/Editor/VSCode/VeroScriptGuiVSCode/Resources/Docs/Developer/ko-KR/index.md) · [Rider](../../../../Extensions/Editor/JetBrains/VeroScriptJetBrains/Resources/Docs/Developer/ko-KR/index.md) · [Rider GUI](../../../../Extensions/Editor/JetBrains/VeroScriptGuiJetBrains/Resources/Docs/Developer/ko-KR/index.md) |
| 임베디드 플랫폼 | [Mobile](../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/Developer/ko-KR/index.md) · [Android](../../../../Extensions/Platform/Mobile/VeroScriptAndroid/Resources/Docs/Developer/ko-KR/index.md) · [iOS](../../../../Extensions/Platform/Mobile/VeroScriptIOS/Resources/Docs/Developer/ko-KR/index.md) |
| 데이터와 네트워크 | [JSON](../../../../Modules/Network/VeroScriptJson/Resources/Docs/Developer/ko-KR/index.md) · [Network](../../../../Modules/Network/VeroScriptNetwork/Resources/Docs/Developer/ko-KR/index.md) · [HTTP](../../../../Modules/Network/VeroScriptHttp/Resources/Docs/Developer/ko-KR/index.md) · [WebSocket](../../../../Modules/Network/VeroScriptWebSocket/Resources/Docs/Developer/ko-KR/index.md) · [Boost](../../../../Modules/Network/Backend/VeroScriptBoost/Resources/Docs/Developer/ko-KR/index.md) · [Curl](../../../../Modules/Network/Backend/VeroScriptCurl/Resources/Docs/Developer/ko-KR/index.md) |
| 멀티미디어와 GUI | [Platform](../../../../Modules/Multimedia/VeroScriptPlatform/Resources/Docs/Developer/ko-KR/index.md) · [Graphics](../../../../Modules/Multimedia/VeroScriptGraphics/Resources/Docs/Developer/ko-KR/index.md) · [Audio](../../../../Modules/Multimedia/VeroScriptAudio/Resources/Docs/Developer/ko-KR/index.md) · [Image](../../../../Modules/Multimedia/VeroScriptImage/Resources/Docs/Developer/ko-KR/index.md) · [Font](../../../../Modules/Multimedia/VeroScriptFont/Resources/Docs/Developer/ko-KR/index.md) · [Gui](../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/Developer/ko-KR/index.md) · [Sdl3](../../../../Modules/Multimedia/Backend/VeroScriptSdl3/Resources/Docs/Developer/ko-KR/index.md) · [Stb](../../../../Modules/Multimedia/Backend/VeroScriptStb/Resources/Docs/Developer/ko-KR/index.md) · [FontBackend](../../../../Modules/Multimedia/Backend/VeroScriptFontBackend/Resources/Docs/Developer/ko-KR/index.md) · [RmlUi](../../../../Modules/Multimedia/Backend/VeroScriptRmlUi/Resources/Docs/Developer/ko-KR/index.md) |

같은 저장소의 현행 안내는 상대 링크를 사용합니다. 온라인 URL로 독자 경계를 우회하거나 포함된 문서를 네트워크에 의존하게 하지 마세요. 소스 코드 참조 동작은 유지합니다.

<a id="current-publication-policy"></a>

All과 User는 현행 문서만 출판합니다. History 디렉터리, 이전 계획과 검증 스냅샷은 출판 입력이 아닙니다. 이전 개정은 버전 관리에, 정확한 검증 증거는 무시된 임시 출력에 보존합니다. History 경로가 있는 소스나 사용자 지정 SourceRoot는 대소문자와 관계없이 거부합니다. 선택한 독자에 맞게 Overview, User, Developer, Standards, 전체 Tutorials와 의존 리소스를 출판합니다.

<a id="module-entrypoints"></a>

모듈 루트 README는 목적, 관련 모듈, 문서와 라이선스의 진입점입니다. Resources/Docs/README는 통합 출판에서 사용하는 여섯 언어와 독자별 선택 페이지입니다. 상세 사용법은 User, 구현·빌드·검증은 Developer, 단계별 학습은 Tutorials에서 관리합니다. 언어 도구 하위 모듈은 그룹의 정본 안내에 연결합니다. 진입점은 간결하게 유지하고 파일 이동 시 상대 링크를 갱신하며 README에 별도 전체 설명서나 검증 기록을 유지하지 않습니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md)

언어: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
