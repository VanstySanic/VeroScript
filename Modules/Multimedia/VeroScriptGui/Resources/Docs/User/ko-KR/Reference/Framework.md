# GUI 프레임워크 계약

## 범위

네이티브 API는 `VeroScript::Gui`, 스크립트 API는 `VS::Gui`를 사용합니다. 위젯 트리, 선언별 id 범위, 수명 주기, 형식화된 속성, Attribute, 이벤트, Context/Application, 테마, 표시 애니메이션, i18n 스냅샷을 제공합니다. 기본, 컬렉션, 탐색 컨트롤과 모달 대화 상자를 제공합니다. 에디터 연동은 별도 가이드에서 설명합니다.

## 컴파일과 구성

호스트는 카탈로그에 RegisterScriptBindings를 등록하고 소스 로드 전에 ConfigureGuiScriptEngine을 호출합니다. 게시된 환경에서는 권한 있는 카탈로그에도 동일한 GUI 바인딩이 필요합니다. 구성 변경 시 환경을 복사하며 게시된 환경을 직접 수정하지 않습니다.

독립적인 `VeroScriptSession::CallFunction` 호출 사이에 전역 상태를 유지하려면 초기화 전에 `session.SetHotReloadEnabled(true)`를 설정합니다. 그렇지 않으면 각 진입에서 모듈 전역 변수가 새로 만들어집니다. 같은 스크립트 진입 안의 `Application.Run/Tick`은 이미 상태를 공유합니다.

GUI 지원 CLI는 일반 컴파일 또는 `--check`에서 GUI가 필요할 때 스크립트 프레임워크 의존성을 자동 구성합니다. 기존 GUI 프로그램에 추가 스위치는 필요하지 않습니다. 명시적인 `gui-*` 작업과 네이티브 `ConfigureGuiScriptEngine`은 프레임워크의 암시적 가져오기를 유지합니다.

일반 변수, 필드 또는 매개변수 이름이 `Gui`라는 이유만으로 프레임워크를 가져오지 않습니다. 네임스페이스 참조, GUI 주석, 매크로 및 검사할 수 없는 가져오기는 기존의 필요성 판단과 보수적인 처리를 유지합니다.

@GuiWidget, @GuiProperty, @GuiMethod, @GuiEvent는 선택적인 Mark를 공통으로 사용합니다. GuiWidget은 Id, GuiProperty는 Style 플래그도 허용합니다. 주석 이름과 옵션은 대소문자를 구분하지 않지만 값과 ML/CSS 이름은 구분합니다. 원래 이름과 Mark를 모두 사용할 수 있습니다. 일반 속성은 값만 받고 std::attribute<T>만 Getter 바인딩을 받습니다.

이 ASCII 대소문자 무시 규칙은 GUI 주석과 그 매개변수에 적용됩니다. Core 주석 이름은 기본적으로 대소문자를 구분하며 GUI 처리기는 옵션 이름을 ASCII 대소문자 구분 없이 인식합니다. 리플렉션의 `find_arguments(name)`은 GUI 주석에서도 저장된 인수 이름의 표기와 정확하게 일치해야 합니다.

Type::Create().Property(value)[{ children }]의 Builder는 마지막 Build 없이 위젯으로 암시적 변환됩니다. 구성을 적용한 후 전체를 초기화합니다. 생성자는 내부 상태를 준비하고 변경 훅이 유효 값을 내부 요소에 동기화합니다. GetX는 기본 유효 값을 읽고 애니메이션은 별도의 표시 값을 생성합니다.

초기화는 자식에서 부모 순서로 인스턴스마다 성공 시 한 번 수행합니다. 마운트는 부모에서 자식, 해제는 자식에서 부모 순서입니다. 마운트 훅의 상태 변경은 레이아웃 전에, 레이아웃 훅의 변경은 다음 `Update`에서 동기화합니다. 실패했거나 아직 실행하지 않은 레이아웃 알림은 이후 업데이트에서 재시도하며, 성공한 알림은 레이아웃이 다시 바뀌기 전에는 반복하지 않습니다. 레이아웃 콜백은 현재 전체 지오메트리를 읽을 수 있고 입력과 렌더링에는 업데이트 성공이 필요합니다.

## ML과 CSS

.vswml은 임의 위젯을 루트로 하는 페이지 또는 template을 포함한 component입니다. component의 type/base/mark가 실제 VS 형식을 정의하며 VS/ML 상속과 합성을 지원합니다. 일반 페이지에는 독립적인 비공개 상태가 있습니다. import/script/style은 Gui가 처리하며 내용을 그대로 RML/RCSS로 해석하지 않습니다.

일반 속성은 고정 값입니다. bind:gap="{ Count }"는 형식화된 Getter를, on:pointer-down="Increase" 또는 중괄호 문장 블록은 이벤트를 설치합니다. bind/on은 기본 제공 접두사이므로 xmlns가 필요 없습니다. 내부 스크립트는 페이지나 컴포넌트 자체를, 외부 스크립트는 선언된 컨텍스트를 사용합니다. 접근 가능한 외부 멤버는 이름 또는 context를 통해 참조하고 지역 변수 범위는 유지됩니다. 표현식은 VS이며 일반 필드의 암시적 변경 감시는 없습니다.

사용자 이벤트는 정확한 매개 변수, 반환 형식과 delegate/multicast_delegate 종류를 따릅니다. 네이티브 입력은 캡처/대상/버블 라우터를 사용합니다. 전파 중지, 즉시 중지, 기본 동작 취소는 별개입니다. ML 콜백은 약한 참조를 사용하며 명시적인 Core 강한 바인딩은 원래 소유권을 유지합니다.

각 선언에는 비공개 id 범위가 있습니다. 사용 위치의 자식은 내부 템플릿의 비공개 요소가 되지 않습니다. elements.Require("part")는 선언 범위를 조회합니다. 리소스 상대 경로는 소유 ML/CSS/VS 모듈을 기준으로 하며 현재 작업 디렉터리를 사용하지 않습니다. `elements.Find("part")`는 위젯을 찾지 못하면 `nullptr`를 반환하며 `elements.Require("part")`는 조회 성공을 단언합니다.

VSWCSS는 형식/Mark, *, class/id, 복합 선택자, 후손/자식, 상태와 제한된 var를 지원합니다. 지원하지 않는 구문은 진단합니다. 우선순위는 형식 기본값, 테마, 로컬 시트, 로컬 구성/바인딩입니다. Setter는 Getter를 교체하며 ClearX는 이전 Getter를 복원하지 않습니다. Style만으로 사용자 형식에 애니메이션 매핑을 부여하지 않습니다.

## 실행과 배포

Context는 호스트에 삽입된 UI를 구동하고 Application은 Platform/Graphics/Gui 서비스를 구성합니다. 이벤트 펌프의 소유자는 하나입니다. MakeRegisteredServiceFactories는 일치하는 네이티브 제공자를 사용하고 코드 소유권을 유지합니다. 어댑터는 빌린 장치나 렌더 대상을 닫지 않습니다. GUI 공급자는 `IBackendService`를 반환합니다.

Application은 제공자의 정리 예외를 오류로 변환하고 남은 자원과 이벤트 펌프 사용권을 유지합니다. Close는 재시도할 수 있으며, 마지막 참조를 해제할 때 정리가 실패하면 소유 스레드에서 CollectRetired로 재시도합니다.

SetLocalization은 Core i18n context를 연결합니다. GetLocalizationSnapshot은 프레임 시작 시 고정된 스냅샷을 반환하며 변경은 다음 프레임에 반영됩니다. 형식화된 메시지와 인수는 Core가 유지하며 Gui가 ICU를 다시 구현하지 않습니다.

`BakeGuiApplication`은 GUI 메타데이터가 포함된 내부 Core 앱 이미지를 생성합니다. 네이티브 이미지 소비자는 `ReadPackage`, `RegisterGuiArtifactBindings`, Session의 `LoadPackage`를 사용합니다. `gui-compile` 명령은 Application 서비스로 이를 표준 ZIP `.vsapp` 컨테이너에 넣으며, 이 컨테이너는 `FApplication`/Session의 `LoadApplication`으로 읽습니다. 컨테이너 생성과 로드에는 Application 지원이 필요합니다. 소스 검사와 미리 보기는 이 서비스 없이도 사용할 수 있습니다.

`CompileGuiSource`/`CompileGuiFile`은 초기화자를 실행하거나 창/장치를 만들지 않고 전체 컴파일을 수행합니다. 네이티브에서 이미지를 직접 로드할 때는 Session `LoadPackage` 전에 `RegisterGuiArtifactBindings`를 호출하세요. 리소스 목록은 소스/디버그 정보 제거 후에도 호출자 기준 상대 검색을 유지합니다. 구운 스크립트와 스타일은 소스가 필요 없지만 외부 자산에는 공급자가 필요합니다.

호환되는 GUI 지원 Runtime은 `gui-check INPUT`, `gui-compile INPUT --output app.vsapp`, `gui-preview INPUT [--frames N] [-- SCRIPT_ARGS]`를 제공합니다. 검사와 컴파일은 초기화 코드를 실행하거나 UI 서비스를 만들지 않습니다. `--frames N`은 생성된 ML 미리보기 루프만 제한합니다. VS 진입점은 자체 실행 수명을 관리하며 이 옵션을 거부합니다.

`--platform-backend`, `--graphics-backend`, `--gui-backend`는 명령이나 스크립트 앞에 지정하며 일반 VS 애플리케이션에도 적용됩니다. 스크립트의 명시적 설정이 호출／환경 설정보다 우선합니다. 빈 값은 상속, `auto`는 사용 가능한 공급자 선택, `none`은 서비스 비활성화를 뜻합니다. 창, GUI 또는 렌더링에 필요한 Graphics를 비활성화하면 오류입니다. 해당 서비스의 기본 선택이 없는 네이티브 호스트는 sdl3／sdl3／rmlui를 기존 대체값으로 사용합니다. `--backend`는 계속 VM／JIT 용도입니다.

CreateCommandLineService는 같은 작업을 호스트에 제공합니다. 빌린 설명자를 사용하는 동안 Owner를 유지합니다. SetEngineOptions/SetRuntimeServices/RegisterBindings/ConfigureEngine/UseArtifact는 호출 구성을 공유합니다. GuiToolsModule은 호스트 서비스로 전달하며 Core를 중복 링크하지 않고, 호스트가 이미 등록했다면 재사용합니다.

네이티브 `FMarkupProgram` API는 네이티브 프로필과 선택적 `IMarkupScriptContext`로 계속 사용할 수 있습니다. VS 형식 내보내기, Builder 생성 및 사용자 정의 속성/이벤트에는 전체 컴파일 경로를 사용합니다. [프레임워크 예제](../../../../Examples/Framework/main.vs)를 참고하세요. Windows/Linux 검증은 macOS, Android, iOS 또는 브라우저 실행을 보증하지 않습니다.

호스트는 `FGuiCompilationConfiguration`에서 필수 업무 컨텍스트와 값 코덱을 선언할 수 있습니다. 리터럴/스타일 파서는 `string`을 받아 선언된 값 형식을 반환하는 일반 공개 범위의 VS 함수입니다. 불변 프로필은 컴파일 식별 정보에 포함됩니다.

공통 예제: [main.vs](../../../../Examples/Framework/main.vs), [page.vswml](../../../../Examples/Framework/page.vswml), [page.vswcss](../../../../Examples/Framework/page.vswcss). check/preview는 컴파일된 GUI .vsapp도 받습니다.

## 텍스트, 글꼴, 이미지 및 그리기

FreeType 2.13.3과 HarfBuzz 10.4.0을 사용하며 수정하지 않은 Lato Latin을 Vero Sans로 포함합니다. 다른 문자는 Context.LoadFont로 적절한 글꼴과 대체 글꼴을 등록합니다. 6개 인수 버전은 굵기, 기울임, 대체 여부와 컬렉션 번호를 받습니다.

Text는 UTF-8 문자열이며 attribute 바인딩을 지원합니다. RML로 해석하지 않고 ML 본문이나 혼합 인라인 태그는 지원하지 않습니다. 글꼴·텍스트 스타일·방향·언어는 부모를 상속합니다. Direction은 문단 기본 방향 auto/ltr/rtl이며 auto는 문단의 첫 강한 방향 문자를 사용하고 없으면 LTR입니다. Language는 언어별 글리프 성형에만 사용합니다. RmlUi는 Unicode 양방향 구간을 해석하고 성형한 뒤 표시 줄마다 재배치합니다. UTF-8 값은 논리 순서를 유지하며 left/right 정렬은 물리 방향입니다.

<a id="image-resources"></a>

`Image`는 콘텐츠 이미지를 로드합니다. `BackgroundImage`는 `url(...)`, 선형·방사형·원뿔형 그라데이션과 반복 그라데이션을 받습니다. 상대 이미지는 지연 속성 getter를 포함해 원래 VS/ML/CSS 소스에서 해석합니다. 네이티브 호스트는 `FResourceReader`와 필요에 따라 `FImageDecoder`를 설정하며 디코딩한 픽셀은 알파를 미리 곱하지 않은 sRGB RGBA8을 사용합니다. 지원 형식은 선택한 [Image 서비스](../../../../../../VeroScriptImage/Resources/Docs/User/ko-KR/index.md)에 따라 달라집니다. `image/stb`는 [Stb](../../../../../../Backend/VeroScriptStb/Resources/Docs/User/ko-KR/index.md)를 참조하세요. SVG와 애니메이션에는 적절한 호스트 디코더/플레이어가 필요합니다.

기본 콘텐츠 레이아웃은 이미지, 텍스트, 자식 위젯 순서이며 속성을 변경하거나 트리를 재구성해도 이 순서를 유지합니다.

Image, BackgroundImage, MaskImage getter는 직접 값과 바인딩 값 모두 해석된 식별자/URL을 반환하므로 다른 모듈로 전달해도 리소스 기준이 바뀌지 않습니다.

<a id="compositor-properties"></a>

`CornerRadius`, `ClipContent`, `Transform`, `Filter`, `BackdropFilter`, `MaskImage`, `BoxShadow`는 같은 속성·바인딩·스타일 경로를 사용합니다. 지원 합성 연산과 리소스 제한은 선택한 백엔드가 결정합니다. RmlUi 공급자는 [RmlUi 합성과 리소스](../../../../../../Backend/VeroScriptRmlUi/Resources/Docs/User/ko-KR/index.md#composition-resources)를 참조하세요.

OnPaint(PaintContext canvas)는 배경 뒤, 자식 앞에서 콘텐츠 로컬 좌표로 실행됩니다. VS는 FillRect, DrawLine, DrawImage, PushClip/PopClip, GetWidth/GetHeight를 제공하며 네이티브 `FPaintContext`는 메시와 변환도 지원합니다. 클립 좌표는 고정이고 변환은 이후 형상에 적용됩니다. 캔버스는 콜백 동안만 유효하며 GUI 변경과 보관은 불가능합니다.

BakeGuiApplication의 FGuiAssetOptions 또는 gui-compile의 반복 --asset 옵션으로 이미지와 글꼴을 포함합니다. 목록은 작업 디렉터리, 내부 참조는 소유 모듈 기준입니다. SDK는 의존성 라이선스를 포함합니다. GUI 계약은 0.1.1입니다.

```cpp
@GuiWidget
export class CanvasView : public VS::Gui::Widget
{
protected:
	void OnPaint(VS::Gui::PaintContext canvas) override
	{
		canvas.FillRect(8, 8, 80, 24, VS::Gui::Color(0, 0.4, 1, 1));
		canvas.DrawLine(8, 40, 88, 40, 2, VS::Gui::Color(1, 1, 1, 1));
	}
};
// String attribute getters may call std::i18n using the frame's localization snapshot.
VS::Gui::Widget title = VS::Gui::Widget::Create().Text("Hello, VeroScript!").FontSize(24);
```

```xml
<Widget>
    <style>
        .title { font-size: 24px; color: #ffffff; }
        .surface { border-radius: 12px; clip-content: true;
                   background-image: linear-gradient(90deg, #173b70, #438bd0); }
    </style>
    <Widget class="title" Text="Hello, VeroScript!" />
    <Widget class="surface" Width="240px" Height="80px" />
    <Widget Image="assets/logo.png" />
</Widget>
```

<a id="native-resource-services"></a>

VeroScriptImage와 VeroScriptFont는 공통 리소스 인터페이스입니다. VeroScriptStb는 `image/stb`를, VeroScriptFontBackend는 `font/font-backend`와 별도의 `text-layout/font-backend` 서비스를 제공합니다. 네이티브 GUI 호스트는 `FBackendOptions.Images` / `Fonts` / `TextLayouts`로 이미지와 글꼴 서비스를 주입합니다. 선택한 Font 및 텍스트 레이아웃 계약을 지원하는 호스트와 SDK를 사용하고 필요한 서비스·백엔드 패키지와 라이선스를 함께 설치하세요.

`ApplicationOptions.FontBackend`와 `TextLayoutBackend`는 서비스를 독립적으로 선택합니다. 빈 값은 호스트／CLI 선택(`--font-backend`, `--text-layout-backend`)을 상속하며, `auto`는 공급자 선택을 위임하고 `none`은 해당 선택적 서비스를 비활성화합니다. 해당 서비스의 기본 선택이 없는 네이티브 호스트는 `font-backend`를 대체값으로 사용합니다. 네이티브 호스트는 `FApplicationFactories.Font`/`TextLayout` 또는 `Fonts`/`TextLayouts`를 제공합니다. GUI `0.1.1`, Font `0.1.2`, text-layout `0.1.0`과 호환되는 공급자를 사용하세요. 컨텍스트가 있는 동안 서비스를 유지하고 공유 설정 교체 전에 모든 컨텍스트를 닫으세요. 선택한 서비스를 `FTextLayout::Create`의 첫 인수로 전달합니다.

## 기본 컨트롤

ProgressBar, Slider, ScrollBar는 GetNormalizedValue를 계산하기 전에 GetActualValue를 최신 설정 범위로 제한합니다. 아직 동기화하지 않은 범위 변경에도 적용하며 잘못된 범위는 계속 동기화 시 거부합니다.

`VS::Gui`는 Text, Image, Border, Spacer, Separator, Stack, Overlay, Button, CheckBox, ToggleSwitch, ProgressBar, Slider, ScrollBar, ScrollView, TextBox, TextArea를 제공합니다. 자식, Builder, Attribute, 이벤트와 ML/CSS를 공유합니다. Border는 BorderWidth/BorderColor, 편집은 Value를 사용합니다.

Clicked, CheckedChanged, ValueChanged, Scrolled, TextChanged/Submitted는 사용자 동작을 알립니다. 바인딩하지 않은 값은 변경하고 바인딩한 Getter는 유지하며 후보 값을 전달합니다. Setter와 일반 갱신은 이 이벤트를 보내지 않습니다. IsXBound()로 확인합니다. 상속 이벤트의 sender는 CheckBox/TextBox 형식입니다. Minimum/Maximum/Step으로 정규화하며 Step=0은 연속 값, GetActualValue는 표시 값입니다.

마우스, 터치, Space/Enter, Tab/Shift+Tab, 방향키, Home/End, PageUp/PageDown을 지원합니다. PreventDefault는 기본 동작을 막으며 :checked/:read-only 스타일을 추가합니다. ScrollView는 휠/터치/키보드를 지원합니다. ScrollX/Y는 요청 값, GetActualScrollX/Y와 GetScrollWidth/Height는 마지막 레이아웃 결과입니다.

논리 코드 포인트 선택, 클립보드, 실행 취소/다시 실행, 읽기 전용, 자리 표시자와 IME를 지원합니다. Select 끝 위치는 제외하며 논리 선택은 여러 시각 구간에 대응할 수 있습니다. 화살표·포인터 판정·IME 커서는 같은 표시 줄 배치를 사용하고 Backspace/Delete는 논리 순서로 삭제합니다. 복원·포커스 변경·트리 재구성에서도 선택의 활성 끝과 방향 경계 커서 위치를 보존합니다. UTF-8 텍스트는1 MiB, 각 기록은128개/8 MiB입니다. Application은 플랫폼을 연결하고 내장 호스트는 FTextInputServices를 제공합니다. 클립보드 오류는 편집을 복원합니다. 실제 IME, 접근성, 완전한 문자소 탐색과 추가 플랫폼은 별도 검증 또는 구현이 필요합니다. GUI 계약0.1.1의 제공자와 사용자를 함께 재빌드합니다. [ML](../../../../Examples/Controls/main.vs), [페이지](../../../../Examples/Controls/page.vswml), [스타일](../../../../Examples/Controls/page.vswcss), [코드](../../../../Examples/Controls/code.vs).

Checked, Value, 범위/단계, ReadOnly와 스크롤 위치는 코드/ML로 설정하고 스타일은 외관과 방향에 사용합니다. TextBox는 바인딩하지 않은 단일 행의 줄바꿈/탭을 제거하며 GetActualValue가 실제 텍스트를 반환합니다.

Step으로 범위가 나누어지지 않아도 최솟값과 최댓값에 도달할 수 있습니다. Slider／ScrollBar의 터치 드래그는 상위 스크롤보다 우선합니다. Select는 확정된 문자열의 코드 포인트 인덱스를 사용하며, 포커스가 없으면 선택 영역을 저장했다가 복원합니다. 유효한 선택은 조합 중인 입력을 취소합니다. 문자열 교체 또는 편집기 종류 변경은 트리 재구성 중에도 실행 취소／다시 실행 기록을 초기화합니다. 전체 컨트롤 라이브러리는 소스 컴파일 비용을 늘리므로 배포 시 gui-compile 베이킹으로 시작 시 소스 컴파일을 피할 수 있습니다.

ScrollWidth/Height는 Padding을 제외한 콘텐츠 범위이며 콘텐츠 뷰포트 크기를 빼면 백엔드의 실제 스크롤 범위와 일치합니다. 임베디드 스크립트 UI는 프레임당 한 번 세션에 진입하여 그 스크립트 함수에서 Context.Update 또는 Application.Tick/Run을 호출하세요. FScriptServices.Invoke는 개별 호스트 콜백의 대체 경로로, 세션 진입 비용이 추가됩니다. 순수 네이티브 Context는 이 스크립트 브리지를 사용하지 않습니다.

요청한 스크롤 위치가 현재 콘텐츠 범위를 벗어나면 휠, 키보드, 터치 입력은 범위 안으로 제한한 위치에서 계산합니다. 다음 레이아웃 전의 연속 이벤트도 누적됩니다. 텍스트 선택을 드래그하는 마우스 또는 터치 장치를 제거하면 해당 드래그가 끝나며, 다른 장치를 제거해도 현재 누름은 취소되지 않습니다. 내부 컨테이너가 더 스크롤되지 않으면 휠 입력을 스크롤 가능한 조상에게 전달하며, 이동량이 0이면 변경을 알리지 않습니다. 장치 제거 시 호스트 텍스트 입력 서비스가 오류를 반환하거나 예외를 발생시켜도 해당 장치의 캡처를 해제하고 오류를 보고합니다.

## 추가 레이아웃, 입력 및 부동 요소

Grid는 Columns(int64, 1–4096)의 같은 너비 열과 Gap을 사용합니다. 행 높이는 가장 큰 자식에 맞추고 Collapsed는 셀을 차지하지 않습니다. 자식 너비는 Grid가 정하며 트랙/셀 병합은 제공하지 않습니다. Wrap은 기본 가로 줄바꿈이며 세로는 Layout=Vertical과 제한된 Height를 사용합니다. Splitter는 일반 자식 최대 두 개를 받으며 부동 자식은 제외합니다. Value는 DividerSize를 뺀 첫 영역 비율입니다. Minimum/Maximum은 [0,1], Step 기본 0.01, DividerSize 기본 6입니다. Vertical은 위아래 분할을 선택합니다. 드래그와 키보드는 ValueChanged(Splitter,double)를 보내고 ReadOnly는 변경을 막습니다.

RadioGroup.SelectedKey는 가장 가까운 그룹 안의 비어 있지 않고 고유한 RadioButton.Key를 선택합니다. 중첩 그룹은 독립적이며 일치하지 않으면 선택이 없습니다. RadioButton은 IsChecked/Select/Focus와 Selected(RadioButton), 그룹은 SelectionChanged(RadioGroup,string)를 제공합니다. 방향키는 순환 이동하고 Space/Enter와 포인터로 선택합니다. 바인딩은 Getter를 유지하고 후보를 알리며 프로그램 설정은 사용자 이벤트를 발생시키지 않습니다.

NumberBox의 double Value/Minimum/Maximum/Step 기본값은 0/0/100/1입니다. ReadOnly, Placeholder, Focus, Select, GetActualValue, ValueChanged/Submitted(NumberBox,double)를 제공합니다. 미완성 텍스트는 Enter나 포커스 이동까지 보존한 뒤 유한 십진수/지수, 범위와 Step을 적용합니다. 잘못된 입력은 마지막 값으로 복원됩니다. Up/Down은 Step(0이면 1), Escape는 초안 취소입니다. 소수점은 로캘과 무관하며 지역화된 표시는 기존 코어 서식을 사용합니다. PasswordBox는 TextBox를 상속하여 선택, IME, 이벤트 형식을 유지하고 표시를 가립니다. Getter는 실제 텍스트를 반환합니다.

Popup은 논리 부모에 고정된 비모달 요소로 일반 배치와 조상 클리핑을 벗어나 문서 레이어 위에 표시됩니다. 소유권과 이벤트 경로는 유지합니다. OffsetX/Y와 뷰포트로 위치를 제한하고 필요하면 위로 옮깁니다. Open 기본 false이며 Escape나 외부 누름(DismissOnOutside=true)은 OpenChanged(Popup,bool)를 보내고 그 누름을 소비합니다. 바인딩은 유지되고 포커스는 명시적으로 정하며 닫을 때 이전의 유효한 포커스를 복원합니다. IME가 Escape를 먼저 처리합니다. ToolTip과 자식은 비대화형입니다. AutoOpen=true이면 부모 호버와 Delay(0.5초)에 따라 표시하고 떠나거나 마우스가 초기화되면 숨깁니다. false면 Open을 사용합니다. IsActuallyOpen은 최근 상태를 읽습니다. Modal=true는 초점을 제한하며 Dialog의 기본값입니다.

Columns, DividerSize, Vertical, OffsetX/Y, Delay는 CSS 속성입니다. 다른 값과 바인딩 정책은 코드/ML로 지정합니다. 기존 빌더와 이벤트 경로를 재사용합니다. GUI 계약 **0.1.1** 및 bExtendedControls에 맞춰 제공자와 소비자를 함께 재빌드합니다. [진입점](../../../../Examples/AdditionalControls/main.vs), [ML](../../../../Examples/AdditionalControls/page.vswml), [CSS](../../../../Examples/AdditionalControls/page.vswcss).

자체 Image/Text 콘텐츠도 자식 위젯 앞의 같은 너비 셀로 배치됩니다. Splitter의 기본 높이는 GUI 단위로 120입니다.

컨테이너가 DividerSize보다 작으면 구분선 크기를 사용 가능한 범위로 제한합니다. RadioGroup 콜백은 전체 선택 상태가 갱신된 뒤 호출되며, 콜백이 다른 항목을 선택하면 원래 요청의 오래된 Selected 이벤트를 보내지 않습니다. NumberBox는 창 포커스 상실이나 입력 상태 초기화 때도 초안을 확정합니다. 확정과 호스트 입력 정리가 모두 실패하면 원래 확정 오류를 유지하고, 호스트가 복구된 뒤 다음 Update에서 정리를 다시 시도합니다. 숫자 앞의 부호는 하나만 허용합니다.

FTextInputServices 교체 중 기존 서비스를 중지하지 못하면 재시도를 위해 기존 서비스와 논리적 포커스를 유지합니다. 숫자 확정 콜백이 컨텍스트를 닫거나 대상을 제거할 수 있으므로 포커스 전환은 콜백 뒤에 컨텍스트와 대상의 유효성을 다시 확인합니다.

## 포커스 가시성과 콜백 오류

레이아웃 완료 후 Tab/Shift+Tab과 Focus()는 마지막 좌표를 사용하여 안쪽부터 바깥쪽 조상 ScrollView를 필요한 만큼만 스크롤합니다. 큰 컨트롤은 양 끝 사이를 반복 이동하지 않습니다. 플로팅 콘텐츠는 논리적 조상을 스크롤하지 않습니다. 오프셋 변경이 필요할 때만 Scrolled를 보냅니다. 바인딩된 속성은 Getter를 유지하고 앱이 이벤트로 후보 오프셋을 수락합니다. 좌표는 다음 Update에 반영됩니다. 포인터 누름으로 얻는 포커스는 스크롤하지 않아 누른 컨트롤이 포인터 아래에서 움직이지 않습니다.

직접 Activate/Focus 호출과 입력 전달은 같은 콜백 예외 처리를 사용합니다. 표준 및 기타 예외를 gui.callback_exception 오류로 변환하여 오류 반환 인터페이스 밖으로 전달하지 않습니다. 이미 커밋한 값과 앱 부작용은 되돌리지 않으므로 오류를 처리한 뒤 다시 시도합니다.

각 마우스의 마지막 창 좌표를 보관하고 레이아웃 후 다시 적중 검사합니다. 호버 스타일과 자동 ToolTip은 후속 Update에서 갱신하며 포인터 입력 이벤트를 합성하지 않습니다. 취소, 장치 제거, 입력 초기화, 포커스 손실과 종료 시 관련 좌표를 지워 정지한 마우스 아래에서 이동한 컨트롤의 오래된 호버를 방지합니다.

## 마우스 커서와 캡처

Cursor는 상속과 스타일을 지원하는 VS::Gui::Cursor 속성입니다. 빌더와 SetCursor는 값 또는 std::attribute<VS::Gui::Cursor>를 받으며, ML은 Cursor="pointer", CSS는 cursor: pointer를 사용합니다. ClearCursor는 상속/스타일을 복원하고 명시적인 Auto는 컨트롤 기본 동작을 선택합니다.

Auto는 버튼, 체크, 토글, 라디오, 슬라이더와 스크롤바에 손 모양, 입력란에 텍스트, Splitter 구분선에만 방향별 크기 조절 커서를 사용합니다. 일반 영역과 비활성 컨트롤은 화살표이며 :disabled { cursor: not-allowed; } 등으로 덮어쓸 수 있습니다. 자식은 상호작용 조상을 따르고 포털은 앵커 버튼의 자동 커서를 이어받지 않습니다.

값: Auto, Default, Text, Wait, Crosshair, Progress, ResizeNWSE, ResizeNESW, ResizeEW, ResizeNS, Move, NotAllowed, Pointer, None. CSS: auto, default, text, wait, crosshair, progress, nwse-resize, nesw-resize, ew-resize, ns-resize, move, not-allowed, pointer, none. None은 숨김이며 Showcase 차트는 crosshair 예제입니다.

Application은 Platform 능력에 따라 연결합니다. 내장 호스트는 선택적으로 `FContext::SetPointerServices(FPointerServices, Error)`를 제공하며 콜백은 소유 스레드에서 실행되고 GUI를 변경할 수 없습니다. 서비스가 없어도 내부 라우팅은 동작합니다. 동일 설정을 반복하지 않으며 마우스 입력 이후에만 커서를 활성화합니다. 터치는 마우스 캡처를 요청하지 않습니다.

드래그는 창 밖에서도 커서와 캡처를 유지합니다. 놓기, 취소, 장치 제거, 포커스 상실, 초기화와 종료 시 상태를 해제하고 레이아웃 변화도 정지한 커서를 갱신합니다. MouseEntered/MouseLeft는 창 알림이며 GUI 루트 이벤트는 mouse-entered/mouse-left입니다. 창을 나가면 hover와 ToolTip을 지우지만 드래그와 키보드 포커스를 취소하지 않습니다. 외부 호스트도 알림을 전달해야 합니다.

서비스 실패와 예외는 오류로 반환합니다. 캡처 획득 실패는 논리 캡처를 되돌리고 커서 실패는 Update로 재시도합니다. 서비스 교체와 Close는 미완료 정리를 보존하며 분리 시 보이는 기본 커서와 캡처 해제를 복원합니다. GUI 계약 **0.1.1**에 맞춰 공급자와 소비자를 함께 다시 빌드합니다.

캡처 동기화는 커서 평가보다 먼저 실행되므로 커서 오류가 네이티브 마우스 캡처 해제를 막지 않습니다. SetMouseCapture만 제공하는 호스트에서는 해당 서비스를 위해 Cursor를 평가하지 않습니다. 공개 포인터 작업은 호스트가 마지막 참조를 해제하더라도 콜백이 끝날 때까지 컨텍스트를 유지합니다.

네이티브 창이 이미 닫혔다면 Application은 입력 비활성화, 캡처 해제, 보이는 기본 커서 복원을 완료된 정리로 처리합니다. 입력 활성화나 다른 커서 적용은 계속 오류를 반환합니다.

Application.Run의 WaitMilliseconds는 유휴 프레임 간격의 상한(기본 16 ms)이며 이전 프레임의 업데이트와 렌더링 시간을 뺀 뒤 대기합니다. 이벤트가 있으면 일찍 깨어납니다. Tick은 대기하지 않으며 호스트가 실행을 조정합니다. 설정되지 않은 네이티브 속성은 스크립트 폴링을 건너뛰고, 설정된 attribute의 동기화 규칙은 유지합니다. 직접 대입과 Clear는 해당 바인딩을 해제합니다.

`Run(beforeUpdate)`와 `Tick(beforeUpdate)`는 GUI 동기화 전, 보통 입력 전달 후 콜백을 실행합니다. 루트에 아직 레이아웃이 없으면 입력 전달 전 초기 레이아웃 갱신에서도 실행하므로 첫 단계에서 두 번 호출될 수 있습니다. 애니메이션과 모델 갱신에는 기존 이벤트 대기 시간을 사용하는 Run 오버로드를 이용할 수 있습니다. Tick은 대기하지 않는 단일 단계로 유지됩니다. 예외는 전파되며 정리 과정에서도 루트 뷰 참조를 갱신합니다.

빈 optional을 반환하는 경우를 포함하여 바인딩된 Getter는 필요한 동기화 라운드마다 평가됩니다. 직접 Setter와 Clear는 로컬 바인딩을 교체하거나 제거합니다. 변경 알림은 훅이 성공한 뒤에만 확정되며 실패한 훅은 이후 업데이트에서 다시 시도할 수 있습니다. 사용자 값 형식은 일반 언어 복사 규칙을 따릅니다.

GUI는 호스트 실행 설정을 사용합니다. `gui-preview` 앞에 `--backend=vm` 또는 `--jit-backend="/path/to/Jit" --backend=jit`를 지정합니다. C++ 호스트는 FEngineConfig.ExecutionBackend와 JitBackendFactory(LoadJitBackend 결과)를 설정합니다. JIT 팩토리가 없는 mixed, 디버깅, 상태를 유지하는 초기화와 제한된 정리 작업은 VM을 사용합니다. 영구 세션의 일반 함수는 선택한 JIT 백엔드를 사용할 수 있습니다. 소스를 로드하기 전에 `VeroScriptSession::ApplyConfig`의 반환값을 확인하고 설정 거부 사유를 전달합니다.

공식 배포 이름은 `vs-gui`, `vs-rmlui`입니다. Gui는 Platform/Graphics/Image/Font에, RmlUi는 Gui에 의존합니다. 백엔드는 선택할 수 있습니다. 네이티브는 `find_package(VeroScriptGui)`, 스크립트는 `COMPONENTS Bindings`와 일치하는 Core/C++ Runtime SDK를 사용합니다. `VeroScriptGuiFramework` 호환 진입점도 유지합니다.

## 스크립트와 호스트 계약

속성과 이벤트의 닫힌 값 타입은 bool, 고정 폭 정수, 열거형을 포함한 코어의 타입 및 값 템플릿 인수를 보존합니다. 생성된 소스는 정수의 타입과 범위를 정확히 유지하고, 내장 컨테이너, 참조, 함수 시그니처 안의 중첩 타입을 복원하며, 고정 길이 배열의 차원 순서를 보존합니다. GuiWidget은 네임스페이스 범위의 닫힌 비정적 클래스를 요구하며 로컬 및 중첩 클래스는 거부합니다. GuiMethod와 속성 변경 훅도 값 매개변수를 포함한 템플릿이 아닌 인스턴스 메서드여야 합니다.

CompileGuiSource/File은 FCompileOptions.ConstantEvaluationLimits를 보존하고 코어의 constexpr 평가를 사용합니다. 동적 attribute는 일반 GUI 동기화 라운드에서 계속 평가됩니다.

Context.Post는 실행 또는 폐기까지 콜백과 캡처를 유지합니다. 캡처한 객체는 지연 정리가 실행될 때까지 남을 수 있습니다. Context.SetFocus는 컨트롤 알림을 동기식으로 전달합니다.

TextArea는 논리 값의 CR, LF, CRLF, NEL(U+0085), 줄 구분자(U+2028), 문단 구분자(U+2029)를 보존하며 각 종료 시퀀스를 한 번의 줄바꿈으로 표시합니다. Backspace/Delete는 CRLF를 함께 삭제합니다. 코드 포인트 선택과 클립보드는 원래 시퀀스를 계산하고 보존합니다. 한 줄 컨트롤은 이 구분자를 제거합니다. 탭의 기존 필터링은 유지하고 자리 표시자의 공백은 표시할 때만 정규화합니다.

방향 경계의 시각 위치는 leading/trailing 귀속으로 구분합니다. 가로 탐색은 문단 기본 방향에 따라 줄바꿈을 넘어 해당 시각 가장자리로 진입하며 반대 방향 구간만 있는 줄에도 적용합니다. 모든 시스템 편집기와 같은 동작을 보장하지는 않습니다. 완전한 문자소 탐색과 합자 내부의 정밀 커서는 별도 기능입니다.

호스트 텍스트 입력 서비스 콜백에서는 GUI 상태를 조회할 수 있습니다. 컨텍스트와 마운트된 위젯 변경, 서비스 교체 및 Close는 콜백이 반환할 때까지 거부합니다.

코어 공개 ABI 또는 바이트코드 내부 레이아웃이 변경되면 일치하는 Core 및 C++ Runtime SDK로 GuiScript와 네이티브 제공자를 다시 빌드해야 합니다. 제품 버전 0.1.1만으로는 바이너리 호환성이 보장되지 않습니다. 패키지 바이트코드도 해당 레이아웃으로 다시 생성해야 합니다.

## 변경 알림 바인딩과 일괄 업데이트

`Observable<T>.Get()/Set()`은 변경을 알릴 수 있는 데이터를 보관합니다. `Set`은 `==`로 비교하며 같은 값은 알리지 않습니다. 가변 객체의 내부를 수정한 뒤에는 상속한 `Notify()`를 호출합니다. 외부 서비스 입력은 `ChangeSignal.Notify()`로 알릴 수 있습니다. 단일 소스는 `AsBinding()`, 계산식은 signal 배열을 명시하는 `Binding<T>(getter, inputs)`를 사용합니다. 일반 스크립트 필드는 자동 추적하지 않습니다. AsBinding 반복 호출은 약한 캐시로 살아 있는 바인딩을 재사용하며 순환 소유를 만들지 않습니다.

Getter는 읽기 전용이어야 하며 변경 가능한 모든 입력을 선언해야 합니다. optional의 빈 결과까지 입력 리비전으로 캐시하며 같은 Binding을 공유하는 위젯은 계산 결과도 공유합니다. `Binding<T>::FromAttribute(attribute, inputs)`는 알림 계약을 명시적으로 선택합니다. 알 수 없는 의존성, 시간, weak 객체의 만료에도 적절한 알림이 필요합니다. 직접 전달한 `std::attribute<T>`의 폴링 및 대체값 동작은 유지됩니다.

반복 알림은 네이티브 변경 큐에서 병합되며 영향을 받는 각 위젯을 한 번만 깨웁니다. `BindingUpdates.Batch(work)`는 중첩을 지원하고 GUI 작업을 다음 안전한 업데이트로 미룹니다. 데이터 자체는 즉시 변경되고 예외가 발생해도 대입을 되돌리지 않습니다. 배치 안에서 Context.Update, Application.Tick, Application.Run을 실행할 수 없습니다. 생성 시 초기화 및 부모/자식 수명 주기 순서는 유지됩니다.

모든 프레임 서비스가 스냅샷을 캡처한 후 큐를 게시하고 기존 위젯 동기화가 해당 속성을 읽습니다. 결과가 같으면 변경 훅이나 불필요한 네이티브 속성 무효화를 발생시키지 않습니다. 프레임 입력은 캡처 후 알림이 필요하며 관찰할 수 없는 프레임 데이터에는 일반 동적 attribute를 사용합니다. 프로그램 업데이트는 입력 이벤트를 내보내지 않습니다. SetX/ClearX는 이전 구독을 해제하고 IsXBound의 attribute 의미는 유지됩니다. Dispose 또는 최종 해제 시 구독이 끊어지고 언마운트는 설정을 유지합니다. Getter 실패 시 마지막 확정값을 유지하고 기존 수명 주기 재시도를 사용합니다. FromAttribute는 전달된 설정을 보관하므로 원래 attribute 설정을 교체해도 이 Binding은 교체되지 않습니다. FromAttribute는 기본 attribute와 네이티브 스타일의 독립적인 변경을 위해 폴백 폴링을 유지하며 전달된 Getter 결과 자체는 알림 리비전으로 캐시합니다.

VS, 생성 Builder, .vswml의 VS 코드에서 같은 setter를 사용합니다. 기존 `bind:`의 폴링 의미와 .vswcss 스타일 계층을 유지합니다. 알림과 큐 처리는 GUI 소유 스레드에서 수행합니다.

```cpp
auto amount = VS::Gui::Observable<double>(0.25);
VS::Gui::ProgressBar bar = VS::Gui::ProgressBar::Create().Value(amount.AsBinding());

// Setters need a mutable value capture; read-only getters do not.
VS::Gui::BindingUpdates::Batch([amount]() mutable -> void
{
	amount.Set(0.4);
	amount.Set(0.6);
});
// The next Context.Update/Application.Tick commits 0.6 once.

array<VS::Gui::ChangeSignal> inputs = {amount};
VS::Gui::Binding<string> caption = VS::Gui::Binding<string>([amount]() -> string
{
	return to_string(amount.Get());
}, inputs);
VS::Gui::Text label = VS::Gui::Text::Create().Text(caption);
```

## 컬렉션, 탐색과 대화 상자

ListView/ListItem, TreeView/TreeItem은 문자열 SelectedKey 단일 선택, FindItem, GetActualSelectedKey, Select, Activate, SelectionChanged/ItemActivated를 제공합니다. Key는 생략 시 Id, 다음으로 순번이며 변경 데이터에는 안정적인 고유 키를 지정합니다. Select는 기존 항목을 요청하고 Activate는 상호 작용 가능성도 확인합니다. 화살표/Home/End로 이동하고 Enter/Space로 실행합니다. 트리는 Expanded attribute, IsActuallyExpanded, ExpandedChanged와 좌우 펼침/접기를 제공합니다. 자식은 마운트 및 원래 Visibility를 유지합니다.

TableView는 TableColumn/Row, Key, 양수 픽셀 ColumnWidth, Sortable, 열당 하나의 widget 셀을 사용합니다. SetColumns/SetRows, GetCellText/SetCellText, SortBy를 제공합니다. 정렬은 안정적인 UTF-8 문자열 순서이며 숫자/지역 규칙을 추측하지 않고 행과 선택을 유지합니다. AutoSort=true로 머리글 클릭을 처리하고 ColumnClicked/Sorted 등을 알립니다. 데이터 소스의 고정 높이 가상화를 선택할 수 있으며 스프레드시트 편집은 포함하지 않습니다. 직접 작성한 자식 widget은 SetChildren/SetRows로 일괄 변경합니다.

ComboBox는 ListItem, SelectedKey/Open/Placeholder/DropDownHeight, Select/SetItems/GetList/RequestOpen 및 이벤트를 제공하며 표시 Text를 선택 항목으로 관리합니다. TabView의 TabItem에는 Key/Title이 있고 미바인딩 초기 선택은 첫 사용 가능한 페이지입니다. Select/SelectionChanged로 바꿔도 페이지는 마운트되고 Visibility를 덮어쓰지 않습니다. 머리글은 좌우/Home/End를 지원합니다.

Menu/MenuItem은 중첩 메뉴, Command, Checkable/Checked, Invoked, Invoke/OpenMenu/CloseMenu를 제공합니다. 하위 메뉴는 아래/오른쪽 포털이며 한 번의 외부 누름으로 활성 체인을 닫고 누름을 소비합니다. OS 기본 메뉴와 단축키 등록은 호스트 책임입니다.

Popup 기본값은 비모달입니다. Placement=below/right/center, MatchAnchorWidth, DismissOnEscape, GetDismissReason(escape/outside/빈 문자열)을 제공합니다. Dialog는 Modal=true, 가운데, DismissOnOutside=false이며 기존 루프에서 Show/Close(result), Closed(Dialog,string)를 사용합니다. 배경 입력과 Tab 초점을 제한하고 이전 유효 초점을 복구합니다. Open 바인딩 원본은 보존되고 다음 동기화에서 모델로 복원됩니다.

Builder는 OnXxx, 인스턴스는 AddXxx입니다. 복합 위젯 SetChildren은 사용자 콘텐츠로 삽입하고 GetChildren/부모 조회는 실제 트리, GetContentChildren은 콘텐츠 자식을 반환합니다. 구조 변경은 getter/업데이트 밖이나 Context.Post로 수행합니다. Grid는 동일 너비이며 독립 열 너비는 TableColumn을 사용합니다. [예제](../../../../Examples/CompositeControls/main.vs), [ML](../../../../Examples/CompositeControls/page.vswml), [CSS](../../../../Examples/CompositeControls/page.vswcss).

`MenuItem.IsActuallyChecked()`는 현재 체크 후보를 반환합니다. 연속 요청은 후보를 전환하며 다음 동기화에서 바인딩된 Checked 모델을 복원합니다. Closed는 OpenChanged 콜백 전의 결과를 보존하므로 콜백에서 대화 상자를 다시 열어도 이전 종료 결과가 바뀌지 않습니다.

데이터 목록과 표는 고정 행 높이의 자동 가상화를 지원합니다. `ListView.SetVirtualItems(labels,rowHeight=28,overscan=2)`와 `TableView.SetVirtualRows(rows,rowHeight=28,overscan=2)`는 보이는 범위의 행만 생성하고 재사용합니다. `SetVirtualItemText(index,text)`와 `SetCellText(key,column,text)`로 화면 밖 데이터도 바꿀 수 있습니다. 사용자 행은 `SetVirtualSource(keys,rowHeight,factory,overscan=2)`를 사용합니다. factory는 `(string key, CollectionItem recycled)`를 받아 같은 행 또는 연결되지 않은 새 행을 반환하며, 표에는 TableRow가 필요합니다. 이전 키의 상태를 초기화해야 합니다. `RefreshVirtualItems()`는 보이는 행을 다시 바인딩하고, `SetVirtualOrder(keys)`는 모든 키의 순서를 바꿉니다. 내장 가상 표는 안정적인 문자열 정렬을 유지합니다. 사용자 소스는 ColumnClicked를 처리해 순서를 제공합니다.

`GetItemCount()`는 논리 행 수이며, `GetItems()`, `GetRealizedItemCount()`, `FindItem()`는 실체화한 행만 다룹니다. 선택 키는 유지되며 `ScrollToItem(key)`로 화면 밖 행을 표시합니다. 포커스·누름·포인터 캡처를 가진 행은 재사용하지 않습니다. 실체화는 Context.Update의 안전한 지연 단계에서 실행하며 레이아웃 중 발견한 변경은 다음 업데이트에서 처리합니다. 행은 고정 border-box 높이로 내용을 자르고, padding은 해당 높이 안에 들어가야 합니다. 키는 비어 있지 않고 고유해야 하며 overscan은 0..256, 전체 행 높이는 16,777,216 논리 픽셀 이내입니다. factory나 getter 안에서 소스를 교체하거나 재정렬할 수 없습니다. factory 오류는 전파되고 후속 업데이트에서 재시도할 수 있습니다. 반환한 행의 소유권은 뷰에 이전됩니다. 기존 자식 위젯, SetItems, SetRows의 수명은 유지됩니다. 가변 높이 행과 트리 가상화는 포함되지 않습니다.

첫 레이아웃 전에 호출한 ScrollToItem은 실제 표시 영역이 확인되면 보정합니다. 이후 명시적으로 지정한 스크롤 위치가 우선합니다.

## Context 백엔드 기본값

표준 애플리케이션 팩터리 브리지를 사용하는 `VS::Gui::Context("", width, height)`에서는 생성자에 명시한 백엔드가 우선하고, 생략하면 호출 선택을 사용한 뒤 애플리케이션／환경 팩터리 기본값을 사용합니다. 두 계층 모두 지정하지 않으면 `rmlui`로 대체합니다. 호스트 선택 콜백이 빈 문자열을 반환하면 해당 계층에서 미지정이며 서비스 비활성화가 아닙니다. 명시적 `auto`나 `none`은 `rmlui`로 바꾸지 않습니다. 환경 구성에 명시적으로 저장한 빈 값은 공통 규칙에 따라 `none`으로 정규화됩니다. 사용자 호스트 Context 팩터리는 자체 백엔드 이름 처리를 담당합니다.

<a id="context-callback-reentrancy"></a>

## Context 콜백 제한

테마, 프레임 서비스, 포인터 서비스 또는 텍스트 입력 서비스를 교체하는 동안 호출되는 콜백에서는 Context 구성을 변경하지 마세요. 같은 Context를 재귀적으로 구성하면 거부됩니다. 소유 스레드에서 나중에 수행할 작업은 `Post`로 예약하세요.

<!-- docs-navigation -->

---

[GUI](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/Framework.md) · [fr-FR](../../fr-FR/Reference/Framework.md) · [ja-JP](../../ja-JP/Reference/Framework.md) · [ko-KR](Framework.md) · [zh-CN](../../zh-CN/Reference/Framework.md) · [zh-TW](../../zh-TW/Reference/Framework.md)
