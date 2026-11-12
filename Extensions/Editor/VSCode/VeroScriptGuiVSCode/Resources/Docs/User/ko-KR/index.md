# VS Code의 VeroScript GUI

**VeroScript** 다음에 **VeroScript GUI**를 설치합니다. 로컬 버전은 도구 API 1을 제공하는 기본 확장과 함께 갱신해야 합니다. GUI 확장은 `.vswml`, `.vswcss`를 담당하며 `.vs`, LSP, DAP, Runtime 선택과 컴파일 설정은 기본 확장이 담당합니다. 색상은 VS Code 테마를 따릅니다.

위젯, 속성, `bind:`／`on:`, 열거형 값과 경로 완성, 호버, 개요, 접기, 대응 태그 강조, 6／8자리 16진수 색상 편집을 제공합니다. 원래 이름과 Mark 별칭은 대소문자를 구분하며 `bind:`／`on:`에는 `xmlns`가 필요 없습니다. VSWCSS는 네이티브 GUI의 지원 범위입니다. 구조 진단은 정보 수준이며 최종 판단은 네이티브 컴파일러가 합니다. 인라인 스크립트는 구문 색상만 제공하고 별도 GUI LSP나 식 의미 분석은 없습니다.

내장 힌트는 ListView/ListItem, TreeView/TreeItem, TableView/TableColumn/TableRow, ComboBox, TabView/TabItem, Menu/MenuItem, Dialog 및 상속 속성과 이벤트를 지원합니다. SelectionColor / selection-color는 마크업 색상 편집을 지원하며 Popup Placement는 below, right, center를 제안합니다. 실행에는 같은 소스로 다시 빌드한 GUI 공급자와 스크립트 라이브러리가 필요합니다. 이전 0.1.1 바이너리를 변경된 네이티브 구조와 섞지 마세요. [GUI 프레임워크](../../../../../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/User/ko-KR/Reference/Framework.md).

**VeroScript GUI**의 검사, 컴파일, 미리 보기, 진입점 선택, 출력 표시 명령을 사용합니다. 미리 보기는 네이티브 앱을 실행합니다. `veroscript.runtimePath`에 GUI 및 GUI 스크립트를 지원하는 Runtime을 지정하고 필요한 네이티브 공급자를 준비하세요. 편집만으로는 네이티브 프로세스를 시작하지 않습니다.

명시적인 GUI 명령은 처음 실행할 때 전체 프레임워크를 컴파일하여 1분 이상 걸릴 수 있습니다. 작업은 비동기로 실행되며 필요하면 VS Code의 작업 명령으로 중지할 수 있습니다.

## 작업 영역 설정

`veroscriptGui.entry`는 `.vs`／`.vswml`, 검사／미리 보기에서는 `.vsapp`도 지정할 수 있습니다. 비어 있으면 선택 파일, 스타일이면 같은 이름의 `.vswml`을 사용합니다. 공유 스타일은 진입점을 명시하세요. `assets`는 컴파일에 포함할 파일, `packageName`은 패키지 이름, `schemaFiles`는 추가 편집 메타데이터입니다. 배열은 기본값을 대체합니다.

`backends`는 `platform`, `graphics`, `gui`, `image`, `font`, `text-layout` 키를 사용합니다. 확장 값을 생략하거나 비우면 네이티브 환경을 상속합니다. 명시한 백엔드 ID는 해당 백엔드를 선택하고, `auto`는 네이티브 후보 선택을 요청하며, `none`은 서비스를 비활성화합니다. 미리 보기에 필수인 서비스를 끄면 네이티브 오류가 발생합니다. `environmentPath`는 CLI `--path`로 패키지 환경을 명시적으로 불러옵니다. 백엔드 선택이 환경 로드보다 먼저 전달되며 LSP／디버거 설정은 변경하지 않습니다. 인수, 환경, 작업 디렉터리, 컴파일 설정은 기본 확장을 상속합니다. 검사／컴파일은 JIT 모듈 없이 VM을 사용하고 미리 보기는 실행 백엔드를 상속합니다. `previewFrames: 0`은 제한을 전달하지 않으며 양의 안전한 정수는 `.vswml`에만 적용됩니다. `.vs`는 자체 루프를 관리합니다.

표준 네이티브 Context 팩터리는 생성자 인수, 호출 설정, 애플리케이션／환경 기본값 모두에 선택이 없을 때만 `rmlui`를 사용하며, 명시적 `auto`와 `none`은 유지합니다. 확장의 빈 설정은 재정의를 생략하지만 네이티브 환경에 명시적으로 저장한 빈 백엔드 값은 서비스를 비활성화합니다. VM/JIT 실행 옵션은 기본 확장이 담당하며 GUI 서비스 역할에 포함되지 않습니다.

경로와 프로세스는 원격 환경을 포함해 작업 영역 확장 호스트에 속합니다. 상대 경로는 소속 폴더, 폴더가 없으면 선택 파일의 디렉터리를 기준으로 합니다. `${workspaceFolder}`, `${file}`, `${fileDirname}`, `${env:NAME}`를 지원하며 각 컨텍스트가 필요합니다. 문서 리소스는 선언 파일을 기준으로 하고 불투명 리소스 ID는 파일 경로로 바꾸지 않습니다.

## 작업과 메타데이터

작업에는 `type: "veroscript-gui"`, `action: "check" | "compile" | "preview"`, `input`이 필요합니다. 컴파일에는 입력과 다른 `.vsapp`의 `output`도 필요합니다. `assets`, `name`, `frames`, `args`, `backends`, `environmentPath`로 설정을 덮어쓰고 배열은 상속 값을 대체합니다. `args`는 미리 보기용입니다. 명령은 관련 미저장 소스의 저장을 확인하고 설정 작업은 VS Code 저장 정책을 따릅니다. 범위 밖 종속 파일은 별도로 저장하세요. 네이티브 오류는 파일과 행으로 이동하며 UTF-8 바이트 열을 UTF-16 열로 해석하지 않습니다.

`*.veroscript-gui.json`을 `schemaFiles`에 추가해 JSON 검증과 함께 힌트를 확장할 수 있습니다. `schemaVersion: 1`, `types`를 사용합니다. 형식에는 `name`과 선택적 `mark`, `bases`, `properties`, `events`, 멤버에는 `name`, `type`과 선택적 `mark`, `bindable`, `style`, `values`, `description`이 있습니다. 실행 바인딩이나 컴파일 프로세서를 등록하지 않으며 모호한 태그 별칭은 후보에서 제외됩니다.

```json
{
	"schemaVersion": 1,
	"types": [
		{
			"name": "Demo::Card",
			"mark": "card",
			"bases": ["VS::Gui::Widget"],
			"properties": [
				{ "name": "Title", "mark": "title", "type": "string", "bindable": true }
			]
		}
	]
}
```

GUI `.vs` 의미 분석／디버깅에는 기본 확장에서 호환 호스트와 바인딩 카탈로그를 설정해야 합니다. GUI 프로세서 자동 시작, 별도 GUI 디버거, 시각적 디자이너는 없습니다. 2 Mi UTF-16 코드 단위를 넘는 파일은 로컬 구조 분석을 중지합니다.

미완성 헤더와 EOF CSS 선언에서도 완성하고 `=`／`:` 앞 공백을 유지하며 숫자 치수 전체를 대체합니다. 닫힌 헤더에는 속성을, 클래스／ID 선택자에는 위젯 형식을 제안하지 않습니다. CSS URL 링크는 네이티브 대소문자ㆍ이스케이프 규칙을 따르고 script／style 안 XML 주석의 태그로 구조를 잘못 판단하지 않습니다.

GUI 컴파일에는 네이티브 Application 지원이 필요하며 표준 `.vsapp` 컨테이너를 생성합니다. GUI 지원 호스트와 필요한 제공자가 있으면 같은 결과물을 GUI 검사/미리 보기와 일반 CLI inspect/검사/실행에 사용할 수 있습니다. 이전 원시 GUI `.vsapp` 결과물은 다시 컴파일하십시오. 백엔드는 직접 선택하며 확장은 RmlUi를 자동 선택하지 않습니다.

배포된 `Vm/` 모듈을 포함한 호환 GUI Runtime 전체를 사용하세요. GUI 검사와 컴파일은 스크립트로 작성된 컴파일 프로세서를 실행할 수 있으므로 선택한 GUI 제공자와 Runtime VM이 모두 필요합니다. 제공자는 명시적으로 선택하며 편집기 메타데이터는 편집 가능한 선언을 설명하고 런타임 알림 구현을 반영하지 않습니다.

일반 CLI의 `--check`는 생성된 `.vsapp`을 독립 컨테이너로 검사하거나 `--path`로 호환 패키지 환경을 선택할 수 있습니다. GUI 검사는 `environmentPath`로 해당 환경을 선택합니다. 수정된 Runtime과 호환되는 설치 환경을 사용하고, Core／ABI 업데이트 후에는 이전 빌드를 대상으로 하는 환경을 다시 생성하십시오.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
