# 예시 색인

예제 파일은 언어 및 런타임 동작의 유효성을 검사하고 문서에 대한 실행 가능한 상호 참조를 제공합니다. 이는 API 사양이 아닙니다. 서명, 오류 조건 및 호환성 요구 사항에 대해서는 언어, 표준 라이브러리 및 도구 체인 참조를 사용하십시오. 예제는 Resources 또는 소스 저장소에서 제공됩니다. 실행할 때 아래 파일 이름을 실제 위치로 바꾸십시오.

예제를 실행해 보세요:

```powershell
veroscript hello.vs
veroscript --check reflection.vs
```

[속성과 메서드 함수 값](standard-library/attribute.md)：`attribute_demo.vs`、`attribute_functions.vs`、`attribute_methods.vs`、`attribute_weak.vs`。

## 실행 백엔드, 리소스 및 권한

설치된 모듈 환경이 있으면 `--jit-backend` 생략 시 JIT 설정을 상속하고 미설정 시 기본값은 `auto`입니다. 자동 선택으로 사용 가능한 백엔드가 없으면 기본 `mixed` 정책은 VM을 사용합니다. 호환 백엔드 파일이나 디렉터리를 수동으로 고정하는 예:

```powershell
veroscript --jit-backend="/absolute/path/to/backend" --backend=jit integer_aliases.vs
```

경로를 실제 백엔드 설치 위치로 바꾸세요. 표준 라이브러리가 Runtime 리소스 배치 밖에 있으면 `--stdlib-path`를 사용합니다. 임베딩 호스트는 모듈 또는 가상 소스 리소스를 명시적으로 제공하고 출력을 캡처합니다.

임베딩 및 모바일 통합은 `portable_basics.vs`로 시작할 수 있습니다. `numeric.vs` 가져오기와 값 캡처 Lambda를 검증하고 `portable basics: 42`를 출력하며, 스크립트에는 출력 권한만 필요합니다. `runtime_utilities.vs`의 `std::filesystem::cwd()`는 파일 읽기, `script_tool.vs`는 환경 변수 읽기, `concurrent_demo.vs`는 작업 생성 권한과 격리된 VM 작업자가 필요합니다. 대기 시간이 0이어도 이미 완료된 결과를 받을 수 있으므로 결과는 한 번만 소비합니다. `path_helpers.vs`와 `windows_path_text.vs`는 텍스트만 처리하며 파일에 접근하지 않습니다. 앱의 작업 스레드에서 적절한 실행 예산을 설정해 실행하세요.

C# 모바일 가이드는 MAUI 예제를, Python 모바일 가이드는 임베디드 CPython 인수 검사 스크립트를 제공합니다. 모바일 빌드, 앱 통합과 기기 검증은 데스크톱 스크립트 테스트와 별도로 필요합니다.

## 예제 읽기

주제를 선택하고 파일 주석과 연결된 참조 문서를 읽은 다음 입력을 바꿔 다시 실행하세요. `int main()`은 프로세스 종료 코드를 반환하고 `int main(string[] args)`는 스크립트 인수도 받습니다. 반환값이 필요하지 않으면 `void main()`도 사용할 수 있습니다.

## 시작하기 및 결합된 기능

| 예 | 주요 주제 |
| --- | --- |
| `hello.vs` | 최소 스크립트, `main` 및 출력. |
| `portable_basics.vs` | 명시적 숫자 모듈 가져오기, 배열, 값 캡처 Lambda와 결과 검증. 출력 권한만 사용. |
| `argv_echo.vs` | `int main(string[] args)` 및 명령줄 인수. |
| `object_basics.vs` | 구조체 메서드, 형식 선언과 함께 정의하는 인스턴스, null을 허용하는 클래스 참조. |
| `language_features.vs` | 제약 조건, 박싱, 오버로드, 기본값 및 가변 변수. |
| `fib.vs` | 재귀 함수 및 조건부 제어 흐름. |
| `goto_demo.vs` | `goto` 및 라벨. |
| `exceptions.vs` | 형식이 지정된 예외, 포괄 처리기, 다시 던지기 및 `finally`. |

## 값, 배열, 컨테이너 및 참조

| 예 | 주요 주제 |
| --- | --- |
| `arrays.vs` | 기본 배열 및 배열 액세스. |
| `dynamic_array.vs` | 동적 배열. |
| `brace_initialization.vs` | 중괄호 초기화. |
| `integer_aliases.vs` | 정수 별칭 및 명시적 유형. |
| `numeric_primitives.vs` | 구별되는 `float`/`double`, 숫자 접미사, 정수부를 생략한 부동 소수점 리터럴 및 8/16/32/64비트 정수. |
| `pair_map.vs` | `pair`, 맵 항목 뷰, 구조 분해, 구조체 및 약한 참조 값의 첫 쓰기. |
| `multimap.vs` | 다중 지도. |
| `multiset.vs` | 다중 세트 및 중복 요소. |
| `priority_queue.vs` | 우선순위 대기열. |
| `ref_foreach.vs` | 참조 변수 및 범위 반복. |
| `reference_returns.vs` | 참조 별칭, 참조 반환 및 클래스 핸들 슬롯 리바인딩. |

## 유형, 제네릭 및 호출 가능한 값

| 예 | 주요 주제 |
| --- | --- |
| `operators.vs` | 연산자 참조 인수, 쓰기 가능/const 인덱스 참조, 대입 참조와 후위 연산 값 복사. |
| `static_members.vs` | 정적 멤버 및 초기화. |
| `default_delete.vs` | 특별 회원을 기본으로 지정하고 삭제했습니다. |
| `constructor_initializers.vs` | 생성자 초기화 목록, `const` 필드 및 초기화 순서. |
| `friend_access.vs` | `friend` 액세스 규칙. |
| `handle_struct.vs` | `shared<T>` / `handle<T>`의 구조체 공유 정체성, 복사 생성과 weak 만료. |
| `pure_virtual_out_of_class.vs` | 클래스 외부 정의 및 순수 가상 구문. |
| `template_prefix.vs` | 템플릿 멤버 정의의 접두사 구문입니다. |
| `method_templates.vs` | 메서드 자체 매개변수와 callable 참조. |
| `value_template_parameters.vs` | 검사된 값 매개변수, 열거형 정책과 고정 배열 길이. |
| `constexpr.vs` | 컴파일 시간 스칼라 값, 순수 함수 및 일반 런타임 호출. |
| `template_methods_main.vs` | 파일 간 템플릿 메서드 정의. |
| `generic_constraints.vs` | 일반 제약 조건 및 `where`. |
| `lambda.vs` | Lambda, 값／참조 초기화 캡처, 공유 mutable 카운터와 최초 도달 시 초기화되는 지역 static. |
| `delegates.vs` | `std::delegate` / `std::multicast_delegate`, 소문자 멤버, 캡처, 재바인딩 및 브로드캐스트 스냅샷. |
| `delegate_functions.vs` | 명명된 함수 바인딩, 0~3개 인수; VM 및 C++/C#/Python AOT. |
| `reflection.vs` | 유형, 필드, 메소드, 선언 메타데이터 및 `mutable` 필드 리플렉션. |
| `reflection_executable.vs` | 형식화된 필드 읽기/쓰기, 인스턴스/정적/기본 인터페이스 호출, 참조 인수와 반환값, 값 인수 격리. |

## 모듈, 전처리 및 패키지

| 예 | 주요 주제 |
| --- | --- |
| `include_alias.vs` | 별칭을 포함/가져옵니다. |
| `alias_exports.vs`, `alias_reexports.vs` | 파일 간 `export using` / `export typedef`, 네임스페이스 조회와 파사드 재내보내기. 함께 제공되는 모듈/파사드는 가져오기 전용입니다. |
| `macro_scope.vs` 및 `macro_scope_module.vs` | 진입 스크립트로 일반 `#define`의 모듈 격리를 확인합니다. 두 번째 파일은 가져오기 전용입니다. |
| `macro_exports.vs`, `macro_exports_module.vs`, `macro_exports_facade.vs` | 공개 객체·함수·가변 인자 매크로, 파사드 재내보내기와 격리. `macro_exports.vs`를 실행합니다. |
| `macro_implicit.vs` | 소비자에 가져오기를 쓰지 않고 공개 매크로를 사용합니다. `veroscript --implicit-import macro_exports_facade.vs macro_implicit.vs`로 실행합니다. |
| `reexport_facade.vs` | 모듈 재수출 파사드. |
| `reexport_main.vs` | 항목 모듈에서 선언을 다시 내보냅니다. |
| `preprocessor.vs` | 전처리기 조건 및 매크로. |
| `package/package_library.vs` | 패키지 라이브러리 소스. |
| `package/package_app.vs` | 패키지 애플리케이션 진입점. |
| 패키지 예시 참고사항 | 패키지 예제 레이아웃 및 명령. |

## 표준 라이브러리, 런타임 및 도구

| 예 | 주요 주제 |
| --- | --- |
| `memory_operations.vs` | 참조 동일성, 바이너리 버퍼, 바이트 순서, 공유 뷰 및 독립 복사본. |
| `formatting.vs` | `print`, `printf`, `format` 및 `formatf`. |
| `path_helpers.vs` | 경로 표준 라이브러리. |
| `standard_library_algorithms.vs` | 제네릭 알고리즘, 결정적 난수 범위 및 재현 가능한 셔플. |
| `standard_namespace.vs` | 권장되는 `std::` 표기, `readonly` 값 및 명시적 변환. |
| `windows_path_text.vs` | 모든 플랫폼의 Windows 형식 경로 텍스트. 파일 시스템 접근 없음. |
| `runtime_utilities.vs` | `range`, 현재 디렉토리 및 시간 유틸리티. |
| `concurrent_demo.vs` | 채널, 함수·정적 메서드·람다를 사용하는 callable 작업과 협력적 취소. |
| `script_tool.vs` | 스크립트 도구 인수, 환경 변수, 문자열, 어설션 및 종료 상태. |

## 호스트 및 바인딩 구성 샘플

| 파일 | 주요 주제 |
| --- | --- |
| `workspace.analysis.sample.json` | 언어 서버 분석 설정. |
| `bindings.sample.json` | 최소 호스트 바인딩 카탈로그. |

`workspace.analysis.sample.json`의 내용을 `initializationOptions.veroscript` 또는 `workspace/didChangeConfiguration.settings.veroscript`로 전달하거나 해당 IDE 설정을 적용하세요. 언어 서버는 이 파일을 자동으로 읽지 않습니다. `bindingCatalogPaths`에는 실제 카탈로그 위치를 지정하고, 설정을 직접 전달할 때는 절대 경로를 사용하세요.

실행 방법과 전체 규칙은 [예제 디렉터리 안내](../../../Scripts/examples/README.md)를 참고하세요. 언어 규칙에는 [언어 참조](language-reference/index.md), 서명에는 [표준 라이브러리 참조](standard-library/index.md), 옵션에는 [CLI 참조](../../../../../VeroScriptCli/Resources/Docs/User/ko-KR/index.md)를 사용하세요.

- `console_demo.vs`: `std::console` (터미널 제어).

[응용 프로그램 국제화 — std::i18n](standard-library/i18n.md): `i18n_demo.vs` · `VeroScript/Resources/Examples/I18n/`.

- [표준 라이브러리 기반](standard-library/foundation.md)：`standard_library_foundation.vs`

## variant

[variant — 동적 값 상자](standard-library/variant.md): `variant.vs`.

```text
veroscript variant.vs
```
<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/examples.md) · [fr-FR](../fr-FR/examples.md) · [ja-JP](../ja-JP/examples.md) · [ko-KR](examples.md) · [zh-CN](../zh-CN/examples.md) · [zh-TW](../zh-TW/examples.md)
