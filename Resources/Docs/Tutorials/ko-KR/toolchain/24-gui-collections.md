# 24: 컬렉션, 탐색 및 대화 상자 사용

제공된 GUI 예제를 열고 항목 선택, 가상 테이블 정렬, 모달 대화 상자 결과를 확인합니다. 목록과 테이블에 각각 10,000행을 저장하고 뷰포트 주변 행만 구체화합니다.

## 1. 준비

`Vm/` 디렉터리를 유지한 호환 GUI Runtime과 설정된 플랫폼/그래픽 `sdl3`, GUI `rmlui`, 이미지 `stb`, 글꼴/텍스트 레이아웃 `font-backend` 공급자를 사용하세요. 이 서비스를 제공하는 설치 환경이 있는 기존 GUI 프로젝트에서 작업합니다. 소스 복사만으로 공급자가 설치되지는 않습니다. 전체 Runtime 및 공급자 리소스를 유지하세요.

[main.vs](../../../../../Modules/Multimedia/VeroScriptGui/Resources/Examples/CompositeControls/main.vs), [page.vswml](../../../../../Modules/Multimedia/VeroScriptGui/Resources/Examples/CompositeControls/page.vswml), [page.vswcss](../../../../../Modules/Multimedia/VeroScriptGui/Resources/Examples/CompositeControls/page.vswcss) 세 파일을 쓰기 가능한 같은 프로젝트 디렉터리에 복사하고 이름과 상대 위치를 유지하세요. 해당 디렉터리에서 터미널을 엽니다.

## 2. 검사

아래 명령은 컴파일 프로세서 제한 시간을 20초로 설정합니다. 명령 수 제한과 다른 호스트 정책도 적용되므로 프로젝트와 호스트에 맞게 조정하세요. 검사 성공 시 종료 코드 0이며 창을 열거나 `main`을 실행하지 않습니다.

```sh
veroscript --compiler-timeout 20000 --platform-backend sdl3 --graphics-backend sdl3 --gui-backend rmlui --image-backend stb --font-backend font-backend --text-layout-backend font-backend gui-check main.vs
```

## 3. 실행과 상호작용

같은 옵션으로 스크립트 진입점을 미리 봅니다. 이 진입점은 `Application.Run()`을 호출하므로 창을 닫아 종료하세요. `--frames`는 ML 미리 보기 진입점에서만 허용하며 이 `.vs` 진입점에 지정하면 사용법 오류가 반환됩니다.

```sh
veroscript --compiler-timeout 20000 --platform-backend sdl3 --graphics-backend sdl3 --gui-backend rmlui --image-backend stb --font-backend font-backend --text-layout-backend font-backend gui-preview main.vs
```

ListView/TreeView 행을 선택하고 화살표로 이동합니다. ComboBox 선택과 TabView 페이지를 바꿉니다. 테이블 헤더를 눌러 정렬하되 UTF-8 순서 비교이므로 숫자처럼 보이는 텍스트도 숫자 정렬은 아닙니다. File 메뉴에서 대화 상자를 열고 승인/취소 또는 Escape를 선택하여 상태 텍스트의 결과를 확인하세요. 모달 대화 상자는 배경의 일반 포인터/키보드 입력을 차단합니다.

## 4. 가상화 관찰

두 10,000행 뷰를 스크롤합니다. 논리 데이터는 남고 위젯은 재활용됩니다. 선택은 재활용 인스턴스가 아니라 키에 속합니다. 예제의 행 높이는 고정 28단위이며 사용자 콘텐츠도 그 안에 맞아야 합니다. 가변 높이와 트리 가상화는 포함하지 않습니다.

## 5. 연습

레이블이나 상태 문자열을 바꾸고 다시 검사하세요. `count × rowHeight ≤ 16,777,216`을 유지하며 행 수를 늘리고 스크롤을 확인합니다. 사용자 행 팩터리는 재활용 상태를 완전히 초기화하고 안정적이며 비어 있지 않은 고유 키를 사용해야 합니다. `SetVirtualSource`, 오버스캔, 소유권, 선택은 [컬렉션, 탐색 및 대화 상자](../../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/User/ko-KR/Reference/Framework.md)를 참고하세요.

<!-- docs-navigation -->

---

[튜토리얼](../index.md)

언어: [en-US](../../en-US/toolchain/24-gui-collections.md) · [fr-FR](../../fr-FR/toolchain/24-gui-collections.md) · [ja-JP](../../ja-JP/toolchain/24-gui-collections.md) · [ko-KR](24-gui-collections.md) · [zh-CN](../../zh-CN/toolchain/24-gui-collections.md) · [zh-TW](../../zh-TW/toolchain/24-gui-collections.md)
