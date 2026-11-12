# VSWML과 VSWCSS

> 이 문서는 네이티브 `FMarkupProgram` 로딩 API를 설명합니다. VS import, 주석 Schema, Builder, 사용자 정의 속성/이벤트 및 형식화된 팩터리는 [프레임워크 가이드](Framework.md)를 참고하세요. 아래 제한은 해당 네이티브 API에 적용됩니다.

공개 API `VeroScriptGuiMarkup.h`로 마크업을 형식이 지정된 속성과 라우트 이벤트가 있는 `FWidget` 트리로 로드한 뒤 기존 컨텍스트에 연결합니다. 텍스트, 글꼴, 이미지, 사용자 그리기와 기본 컨트롤을 지원합니다. 네이티브 로더가 기본 등록하는 형식은 `Widget`뿐입니다. 내장 컨트롤과 형식이 지정된 속성·이벤트는 Schema를 지원하는 VS 컴파일 경로를 사용하거나 네이티브 형식 팩터리를 명시적으로 제공하세요. 마크업 형식은 특정 레이아웃 또는 그래픽 백엔드를 요구하지 않습니다.

## 로딩과 문법

`ReadMarkupFile`로 입력을 읽고 `FMarkupCompileOptions.Read`를 설정한 뒤 `FMarkupProgram::Compile`, `Create`를 호출하여 기존 `FContext`에 루트를 전달합니다. 진단은 `FMarkupDiagnostic::Format`으로 확인합니다. `src`는 작업 디렉터리와 무관하게 선언 파일을 기준으로 해석합니다. 사용자 `FMarkupReader`는 불투명한 모듈·리소스 ID도 사용할 수 있습니다. `GetDependencies`는 직접 읽은 소스의 불변 스냅샷을 제공하며 전이적 VS import는 Core가 관리합니다. 컴파일에 창이나 GPU가 필요하지 않습니다. `Create`마다 위젯과 스크립트 상태가 독립적이며 컴파일된 프로그램이 해제된 뒤에도 인스턴스는 유지됩니다. 다시 로드와 다시 컴파일은 명시적으로 요청하며 자동 상태 이전은 제공하지 않습니다.

```cpp
#include "VeroScriptGuiMarkup.h"
#include <utility>

bool LoadMarkupUi(const std::string& Entry,
				  std::shared_ptr<VeroScript::Gui::FWidget>& OutRoot,
				  std::string& Error)
{
	using namespace VeroScript;
	Gui::FMarkupSource Source;
	Gui::FMarkupDiagnostic Diagnostic;
	if (!Gui::ReadMarkupFile({}, Entry, Source, Error))
	{
		return false;
	}
	Gui::FMarkupCompileOptions Options;
	Options.Read = Gui::ReadMarkupFile;
	auto Program = Gui::FMarkupProgram::Compile(Source, Options, Diagnostic);
	if (!Program)
	{
		Error = Diagnostic.Format();
		return false;
	}
	auto Root = Program->Create({}, Diagnostic);
	if (!Root)
	{
		Error = Diagnostic.Format();
		return false;
	}
	OutRoot = std::move(Root);
	Error.clear();
	return true;
}
```

반환된 루트를 `SetRoot`로 기존 `FContext`에 연결하고 `Update`가 성공한 뒤 렌더링하세요. 로딩 함수 자체는 창, 장치 또는 백엔드를 만들지 않습니다.

UTF-8, 단일 루트, 대소문자를 구분하는 XML 형식으로 따옴표 속성, 주석, CDATA, XML/숫자 엔터티를 지원합니다. bind:와 on:은 내장되어 xmlns가 필요하지 않습니다. 등록된 모든 Widget은 루트가 될 수 있고 여러 자식을 가질 수 있습니다.

루트에는 여러 import/style과 최대 하나의 script가 가능합니다. style은 src 또는 인라인입니다. 일반 루트는 인스턴스이고 component에는 type과 단일 template이 필수이며 mark/base/id는 선택 사항입니다. 형식 짧은 이름과 mark를 모두 태그로 사용할 수 있습니다. base는 가져온 ML 구성 요소나 네이티브 팩터리이며 순환은 거부합니다.

속성 원래 이름과 Mark를 모두 허용하지만 같은 속성의 중복 소스는 오류입니다. ML의 id/class는 소문자이며 값은 대소문자를 구분합니다. 템플릿의 ID/스타일/스크립트 범위는 비공개이며 Find(componentRoot, id)로 내부를 조회합니다. 사용 지점에 추가한 자식은 선언 범위를 유지합니다. 네이티브 팩터리 내부도 비공개이며 생성 후 임의 AddChild는 ML 범위에 자동 등록되지 않습니다.

일반 속성은 고정값입니다. `bind:gap="{ Count + 1 }"` 또는 메서드 이름은 getter를, `on:pointer-down="{ Count = Count + 1; }"` 또는 이름은 처리기를 설치합니다. 알 수 없는 컨트롤/속성/이벤트, 원시 텍스트와 미지원 문법은 진단합니다.

## 스타일과 스크립트

선택자는 형식/Mark, *, class, id, 복합, 자손, >, 쉼표 목록을 지원하며 상태는 root/hover/active/focus/focus-within/disabled/checked/read-only입니다. 우선순위는 기본→테마→템플릿→사용 지점→로컬 값/getter이며 같은 범위에서는 명시도와 소스 순서입니다. SetTheme/SetClasses/SetId는 다음 Update에 반영합니다. Set은 getter를 교체하고 Clear는 현재 스타일/기본값을 복원합니다. getter 오류는 업데이트 실패로 전달합니다. 범위 우선순위는 서로 다른 범위 사이의 명시도 비교보다 먼저 적용합니다. `FMarkupProgram::SetTheme`은 인스턴스 런타임의 테마를 설정하거나 제거합니다. CSS에는 스타일을 허용한 속성만 사용할 수 있습니다. 기본 속성은 getter 바인딩을 제공하며 구체적인 컨트롤의 GuiProperty 스키마가 일반 값과 바인딩 가능한 Attribute 속성을 구분합니다.

길이는 숫자, px/dp, %, auto이며 padding은 음수가 아닌 1~4개 값입니다. sRGB 색상을 선형 RGBA로 변환합니다. 변수는 부모의 계산된 값을 상속하며 var/fallback을 지원합니다. 순환, 누락, 형식 오류는 소스 위치와 함께 진단합니다. 변수 상속은 같은 ML 런타임 안에서만 이루어지고 글꼴과 텍스트는 아래 규칙대로 상속하며 다른 일반 속성에 HTML 상속을 추가하지 않습니다. !important, @ 규칙, 형제/속성 선택자, CSS 애니메이션, 임의 RCSS는 미지원입니다. 상태는 Update에서 갱신하며 Platform 취소/포커스 손실/초기화 이벤트도 전달해야 합니다.

정적 ML/CSS에는 Gui만 필요합니다. VEROSCRIPT_GUI_SCRIPTING=ON과 VeroScript::GuiScript를 사용하고 FMarkupScriptOptions.Engine을 구성한 뒤 MakeMarkupScriptFactory를 Create에 전달합니다. 일치하는 C++ Runtime/Core가 필요합니다. 인라인 script는 인스턴스별 클래스 멤버이고 일반 페이지는 `<script src="logic.vs" type="App::Logic" />`도 가능합니다. 전체 트리를 만든 뒤 스크립트를 생성하고 바인딩을 설치하며 마운트/첫 레이아웃은 아직 완료되지 않은 시점입니다. 페이지마다 독립 상태 객체를 만듭니다. 컴포넌트 상태는 `VS::Gui::Widget`을 상속하며 기존 네이티브 루트를 참조합니다. 생성자에서 초기화된 `elements`를 조회할 수 있지만 마운트와 레이아웃은 아직 완료되지 않았습니다.

이 네이티브 API에서는 스크립트 블록 대신 `FMarkupProgram::Create`의 세 번째 인수로 `IMarkupScriptContext`를 전달할 수도 있습니다. 두 컨텍스트 소스는 함께 사용할 수 없으며 컴포넌트는 자체 컨텍스트를 사용합니다.

내장 이름은 self/context/elements이며 이벤트에는 sender/event/arg0/arg1도 있습니다. 외부 context는 일반 VS 접근 권한과 로컬 변수 가리기를 유지합니다. elements.Find는 null을 반환할 수 있고 Require는 없으면 실패합니다. 이름으로 참조하는 메서드에는 @GuiMethod가 필요하며 Mark는 선택 사항입니다. GUI 어노테이션 이름과 옵션 이름은 ASCII 대소문자를 구분하지 않습니다. Core 어노테이션 이름은 기본적으로 구분합니다. 리플렉션 `find_arguments(name)`은 저장된 인수 이름과 정확히 일치해야 하며 Mark 값도 대소문자를 구분합니다. 라우팅 메서드는 void이며 인수 없음, Event, Widget sender 또는 둘 다를 허용합니다. Event는 입력 스냅샷과 약한 Widget 참조를 보관하며 전파/기본 동작 제어는 동기 전달 중에만 유효합니다.

attribute.vs, std::attribute.get/set 및 Core 현지화/서식 지정은 일반 VS 호출로 사용할 수 있습니다. Text와 편집 컨트롤은 제공됩니다. ML 문자열을 현지화 키로 암묵 해석하지 않으며 일반 Core 현지화/서식 지정 호출에 텍스트를 바인딩합니다. 게시된 ModuleEnvironment 카탈로그는 변경하지 않고 Gui와 합성합니다. 소유 스레드에서 작업하며 마지막 참조가 다른 스레드에서 해제되면 종료 전에 CollectMarkupScripts를 호출합니다.

네이티브 팩터리는 하위 노드를 포함하여 소유 스레드에서 새로 만든 Widget을 반환해야 합니다. 이미 초기화되었거나 기존 ML 인스턴스에 속한 노드는 거부합니다. 바인딩 식/블록의 `self`, `context`, `elements`, `sender`, `event`, `arg0`, `arg1`은 예약 이름입니다. 지역 변수, lambda 매개변수, foreach/catch 변수와 지역 형식 별칭에서 다시 선언할 수 없습니다.

기본, 컬렉션, 탐색 컨트롤과 모달 대화 상자를 제공합니다. GUI가 활성화된 CLI는 `gui-check`, `gui-compile`, `gui-preview`를 제공합니다. 명령, 스키마와 형식화된 팩터리는 [프레임워크 가이드](Framework.md)를 참고하세요. [카운터](../../../../Examples/Markup/counter.vswml), [컴포넌트](../../../../Examples/Markup/card.vswml), [공유 스타일](../../../../Examples/Markup/layout.vswcss) 예제는 통합 Resources 문서에 포함됩니다. 기능 SDK 아카이브에 예제 디렉터리가 있다고 가정하지 마세요.

## 텍스트, 글꼴, 이미지 및 그리기

Text는 UTF-8 문자열이며 attribute 바인딩을 지원합니다. RML로 해석하지 않고 ML 본문이나 혼합 인라인 태그는 지원하지 않습니다. 글꼴·텍스트 스타일·방향·언어는 부모를 상속합니다. Direction은 문단 기본 방향 auto/ltr/rtl이며 auto는 첫 강한 방향 문자를 사용하고 없으면 LTR입니다. Language는 성형 언어만 정합니다. RmlUi는 혼합 방향을 배치하며 UTF-8 값과 선택 인덱스는 논리 순서를 유지합니다. ML의 `Direction="auto" Language="ar"`와 CSS의 `direction: auto; language: ar;`는 같은 속성이며 left/right는 물리 방향입니다.

[GUI 프레임워크 계약 — 텍스트, 글꼴, 이미지 및 그리기](Framework.md#image-resources)을 참조하세요.

기본 콘텐츠 레이아웃은 이미지, 텍스트, 자식 위젯 순서이며 속성을 변경하거나 트리를 재구성해도 이 순서를 유지합니다.

Image, BackgroundImage, MaskImage getter는 직접 값과 바인딩 값 모두 해석된 식별자/URL을 반환하므로 다른 모듈로 전달해도 리소스 기준이 바뀌지 않습니다.

[GUI 프레임워크 계약 — 텍스트, 글꼴, 이미지 및 그리기](Framework.md#compositor-properties)을 참조하세요.

[Framework](Framework.md)

| 속성 | CSS 이름 | 스타일 지정 |
| --- | --- | --- |
| Text, Image | text, image | 불가 |
| FontFamily, FontSize, FontWeight, FontStyle | font-family, font-size, font-weight, font-style | 가능 |
| TextColor, TextAlign, WhiteSpace, TextOverflow | color, text-align, white-space, text-overflow | 가능 |
| LineHeight, LetterSpacing, Direction, Language | line-height, letter-spacing, direction, language | 가능 |
| CornerRadius, ClipContent | border-radius, clip-content | 가능 |
| BorderWidth, BorderColor | border-width, border-color | 예 |
| Cursor | cursor | 예 |
| BackgroundImage, MaskImage, BoxShadow | background-image, mask-image, box-shadow | 가능 |
| Transform, Filter, BackdropFilter | transform, filter, backdrop-filter | 가능 |

[GUI 프레임워크 계약 — 텍스트, 글꼴, 이미지 및 그리기](Framework.md#native-resource-services)을 참조하세요.

Cursor의 형식은 `VS::Gui::Cursor`입니다. ML에서는 `Cursor="pointer"`, CSS에서는 `cursor: pointer`를 사용합니다. 상속과 Attribute 바인딩을 지원합니다. `Auto`는 컨트롤의 기본 동작을 선택하고 `None`은 커서를 숨깁니다. 포인터 서비스와 창 경계 알림은 [Framework](Framework.md)을 참조하세요.

사용자 속성은 `--`로 시작합니다. `var(--name, fallback)`은 상속 값이나 중첩 가능한 fallback을 사용합니다. 다음 gap은 8 pixels이며 `--space`를 제거하면 fallback인 4 pixels를 사용합니다.

```css
:root
{
	--space: 8px;
	gap: var(--space, 4px);
}
```

<!-- docs-navigation -->

---

[GUI](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/Markup.md) · [fr-FR](../../fr-FR/Reference/Markup.md) · [ja-JP](../../ja-JP/Reference/Markup.md) · [ko-KR](Markup.md) · [zh-CN](../../zh-CN/Reference/Markup.md) · [zh-TW](../../zh-TW/Reference/Markup.md)
