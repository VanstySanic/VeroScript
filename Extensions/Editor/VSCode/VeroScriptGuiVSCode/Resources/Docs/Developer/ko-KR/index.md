# VeroScript GUI VS Code 개발

## 구조

네이티브 GUI 검증에는 일치하는 Core, Tooling, VM 및 공급자 빌드가 필요합니다. 동일한 제품 버전만으로 ABI 호환성을 판단할 수 없습니다. 편집기 메타데이터 검증과 실제 검사, 애플리케이션 컨테이너 컴파일, 제한된 프레임 미리보기를 각각 검증하세요.

산출물 회귀 테스트는 공급자를 사용하는 `gui-check`, 독립적인 일반 `--check`, 소스 프로젝트 환경과 컴파일 인수를 상속한 일반 `--check`로 `.vsapp`을 검증합니다. 명시적인 표준 라이브러리 경로와 `--path`를 함께 사용하여 네이티브 표준 라이브러리 루트 중복 제거도 검증합니다. 전체 기한은 네이티브 작업과 아카이브 검증을 포함하며 각 명령에도 별도 기한을 적용합니다. Core／ABI 구성이 바뀌면 설치된 테스트 환경을 다시 생성하십시오.

Runtime／LSP／DAP는 기본 확장에 둡니다. 종속 확장을 활성화하고 `Public/api.d.ts` API 1로 명시적 실행 때만 도구 체인을 확인합니다. 소스 URI는 실행 변수, 소속 폴더는 설정을 결정합니다. 편집과 자동 작업 열거는 네이티브 도구를 실행하지 않으며 설정 작업을 확인할 때 CLI 버전을 조회할 수 있습니다. 네이티브 GUI 코드는 별도로 관리합니다.

문서 버전별 모델을 캐시하고 분석을 제한합니다. 수정ㆍ취소 뒤의 비동기 메타데이터／경로 결과는 버립니다. 캐시와 감시자는 해석된 설정별로 분리합니다. 이름／Mark 대소문자를 유지하고 모호한 별칭을 제외하며 공유 조상 중복 탐색을 막습니다. 편집 힌트를 컴파일러 의미 판단으로 사용하지 마세요.

## 빌드와 검증

Node.js 22.18+ 및 데스크톱 VS Code 1.106+가 필요합니다. 각 모듈에서 아래 명령을 실행합니다. 공유 선언이 기본 확장 SDK 형식을 참조하므로 양쪽 종속성을 설치합니다. 개발 테스트는 두 확장을 로드하고 패키지 테스트는 격리 프로필에 로컬 VSIX 둘을 순서대로 설치합니다. 미게시 종속성을 Marketplace에서 가져오지 않도록 합니다.

```sh
# VeroScriptVSCode
npm ci
npm run typecheck
npm run package
# VeroScriptGuiVSCode
npm ci
npm run typecheck
npm run test:unit
npm run test:integration
npm run package
npm run package:contents
node Resources/Tools/test.mjs --packaged --skip-build
```

데스크톱 Extension Host 테스트 모음은 하나씩 실행합니다. 여러 VS Code 테스트 창을 동시에 열면 완성 확인, 실행 취소 및 Enter 테스트에 필요한 편집기 포커스가 서로 바뀔 수 있습니다.

네이티브 GUI 승인 테스트는 검사, 컴파일, 미리 보기 작업마다 600초를 허용하고 해당 테스트의 전체 제한 시간만 작업 순서에 맞춥니다. 편집기 공급자의 짧은 대기 시간은 유지합니다. 명시적인 GUI 명령은 전체 프레임워크를 처음 컴파일하여 정상 종료까지 1분 이상 걸릴 수 있습니다. 제품 작업은 비동기로 실행되며 VS Code에서 중지할 수 있습니다. 0이 아닌 종료 코드, 출력 누락 또는 테스트 시간 초과는 계속 승인 실패로 처리합니다.

단위 테스트는 구조, UTF-16, CSS, 메타데이터, 모호성／순환, CLI 순서와 TextMate를 다룹니다. Extension Host는 종속성, 언어, 완성, 호버, 링크, 태그, 진단 및 네이티브 서비스 휴면을 다룹니다. `VEROSCRIPT_TEST_RUNTIME`에 일치하는 GUI Runtime을 지정하고 `VEROSCRIPT_GUI_TEST_NATIVE=1`로 실제 검사／컴파일을 요구합니다. 호환 공급자가 있는 데스크톱에서 `VEROSCRIPT_GUI_TEST_PREVIEW=1`을 추가하면 2프레임 마크업 미리 보기도 요구합니다. Windows／Linux／macOS, 공백ㆍUnicode 경로, 폴더 없음, 다중 루트, 원격 호스트, 실제 창을 각각 검증하세요. 건너뛴 테스트는 네이티브 합격이 아닙니다.

`VEROSCRIPT_TEST_DIRECTORY`는 기본 확장과 마찬가지로 격리된 테스트 작업 공간과 프로필의 절대 상위 디렉터리를 지정할 수 있습니다. 네이티브 패키지 환경 검증에서는 생성된 GUI 소스가 해당 환경에 속하도록 `VEROSCRIPT_GUI_TEST_ENVIRONMENT_PATH`로 선택한 프로젝트 안에 배치하세요. 관련 없는 임시 작업 공간은 네이티브 소스 소유권 검사에서 거부될 수 있으므로 이 테스트 구성 불일치를 우회하려고 컴파일러 접근 범위를 넓히지 마세요.

네이티브 검증마다 Runtime 소스／릴리스 스냅샷을 기록하고 이전 보고서를 새 코드의 증거로 사용하지 마세요. CLI 시간 초과는 검증 실패입니다. 편집 시 자동 미리 보기를 시작하지 않습니다.

## 편집 메타데이터

예제를 `demo.veroscript-gui.json`으로 저장하고 `veroscriptGui.schemaFiles`에 추가합니다. 편집 힌트일 뿐이며 실제 형식과 프로세서는 호스트가 등록해야 합니다. 뒤의 스키마는 같은 정식 이름의 형식 전체를 대체하므로 자체 멤버를 모두 제공하세요. 기본 형식은 정식 이름을 사용하고 `routedEvents`로 공통 이벤트를 추가할 수 있습니다.

```json
{
	"schemaVersion": 1,
	"types": [{
		"name": "Demo::Card",
		"mark": "card",
		"bases": ["VS::Gui::Widget"],
		"properties": [{ "name": "Title", "mark": "title", "type": "string", "bindable": true }],
		"events": [{ "name": "Selected", "mark": "selected", "type": "std::multicast_delegate<void()>" }]
	}]
}
```

내장 메타데이터는 네이티브 `VeroScriptGui.vs` 진입점에서 로컬 `export import`를 따라가며 입력을 `builtin.json`에 기록합니다. `Mark` 별칭이 없는 주석 위젯도 유지하여 복합 컨트롤의 상속을 완성합니다. `npm run schema:update`로 갱신한 뒤 네이티브 리터럴과 style／bindable 플래그를 검토하세요. 일반 빌드는 일치하는 스냅샷을 요구하고 중복 형식과 누락된 기본 형식을 거부합니다. 단위 및 Extension Host 회귀 테스트는 복합 태그, 상속 속성／이벤트, 배치 값, CSS 힌트와 선택 색상을 다룹니다. 메타데이터 검증은 네이티브 실행 검증이 아닙니다. 네이티브 네임스페이스 변경 후에도 정규화된 `VS::Gui::Internal` 기본 형식을 유지하여 상속 힌트를 보존합니다.

메타데이터 추출은 `@GuiWidget`, `@ GuiWidget`처럼 `@`와 주석 이름 사이의 유효한 언어 공백을 허용하며 동일한 속성·이벤트·상속 정보를 생성합니다. 소스 서식을 바꾼 뒤 `npm run schema:check`를 다시 실행하세요. 추출된 메타데이터가 바뀌면 `npm run schema:update`를 사용합니다. `builtin.json`은 소스 경로와 메타데이터를 기록하며 소스 내용의 해시는 보관하지 않습니다.

## 배포

기능 패키지는 bundle, 매니페스트, 번역, 문법, 스키마, 아이콘과 MIT 고지만 포함하며 네이티브 바이너리, 도구, 테스트, 개발 문서는 제외합니다. 현재 GUI 런타임 JavaScript 외부 종속성이 없으며 추가하면 고지를 갱신해야 합니다. LICENSE는 저장소와 일치시키고 6개 언어를 동기화하세요. 독립 VSIX 패키징과 함께 통합 IDE 패키징도 GuiVSCode를 독립 제품으로 등록하고, 기본 종속성을 빌드한 뒤 두 로컬 VSIX를 함께 검증합니다.

정적 회귀 검토는 EOF／태그 경계, 숫자 범위, 공백 포함 대입, 닫는 따옴표 뒤 중복 속성, 인라인 XML 주석과 빈 개요를 다룹니다. CSS URL은 캐시한 스타일 분석에서 한 번 읽고 네이티브 대소문자ㆍ이스케이프를 처리하며 링크 요청은 범위를 재사용합니다. 설정 작업 준비 시 CLI 버전을 조회할 수 있지만 편집과 자동 작업 검색은 GUI 명령을 실행하지 않습니다. 네이티브 검증은 VS Code 등록 공급자에서 설정 작업을 가져와 설치된 확장을 사용합니다. 필요하면 `VEROSCRIPT_GUI_TEST_ENVIRONMENT_PATH`에 준비한 패키지 환경의 절대 경로를 지정합니다. JavaScript 프로토타입 멤버와 이름이 같은 사용자 정의 태그의 속성 완성도 회귀 사례에 포함합니다.  격리 테스트 프로필은 단어 제안을 끄고 지원되는 편집기 닫기 명령을 사용합니다. 네이티브 검증에는 일치하는 Runtime과 검사ㆍ컴파일ㆍ미리 보기의 성공이 필요합니다.

[에디터 통합 유지보수](../../../../../../../../Resources/Docs/Developer/ko-KR/editors.md#acceptance-evidence)을 참조하세요.
publisher는 `package.json`의 선언을 따르며 GUI 종속성은 기본 확장의 식별자와 일치해야 합니다. 루트 README를 세부 정보 페이지로 포함하고 제외된 저장소 파일로 연결하지 마십시오. 기능 패키지의 문서 예외는 `extension/readme.md`만 허용합니다. Marketplace 게시에는 해당 등록 publisher의 권한과 검증 완료가 필요하며 매니페스트 변경으로 publisher가 등록되지는 않습니다.

빌드 시 [공유 256px 제품 PNG](../../../../../../../../Resources/Assets/icons/veroscript-icon-256.png)를 확장 표시 아이콘으로 복사합니다.


[ko-KR](../../User/ko-KR/index.md) · [Documentation](../../README.md)
