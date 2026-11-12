# C# BindGen

`VeroScriptCSharpBindGen`은 C# 의미 모델에서 Binding Catalog v1 메타데이터를 생성합니다. 선택적으로 `VeroScriptCSharp`용 리플렉션 없는 등록 코드, API 참조와 생성 C# 프로그램용 AOT 브리지도 출력합니다.

## 출력 선택

CLI에서는 `--catalog`가 필수입니다. `--registration`, `--aot-bridge`, `--api-reference`, `--api-reference-dir`는 추가 출력을 선택합니다. 메타데이터만 생성하려면 등록과 브리지 옵션을 생략합니다. Catalog는 계약을 설명하며 실행 콜백을 등록하지 않습니다. 등록 코드는 `VeroScript.CSharp`와 컴파일합니다. AOT 브리지는 원래 관리 선언 및 생성 C# 프로그램과 컴파일하며 리플렉션이나 `VeroScriptInterop` 없이 직접 호출합니다.

라이브러리 기본값은 다릅니다. `CSharpBindGenOptions.EmitRegistrationSource`와 `EmitApiReference`는 `true`이고 `EmitAotBridgeSource`는 `false`입니다. 실행 어댑터 없이 메타데이터만 생성하려면 `EmitRegistrationSource=false`와 `EmitAotBridgeSource=false`를 모두 명시합니다. 선택적 어댑터의 제한이 곧 해당 Catalog 메타데이터의 제한은 아닙니다.

## 공통 입력 규칙

기본적으로 공개 클래스, 구조체, 열거형 및 공개 멤버를 대상으로 합니다. `--require-attribute`는 `[VeroScriptBinding]` 형식을 선택하고 `[VeroScriptIgnore]`는 선언을 제외합니다. 제네릭 호스트 형식이나 멤버, async, 포인터, nullable 또는 delegate 형식, 선택적 또는 `params` 매개변수, 연산자, 참조 반환과 필드, 인덱서, 필수 멤버 및 표현 불가능한 오버로드는 거부합니다. 추상 클래스 생성자는 생략합니다. 추가 출력에는 별도 제한이 있습니다.

### 컨테이너 키

map 및 ordered-map key와 set element에는 `sbyte`, `int`, `uint`, `long`, `ulong`,
`string` 또는 선언된 enum을 사용할 수 있습니다. `sbyte`는 VeroScript의 signed 8-bit
`char`에 매핑됩니다. 다른 key shape은 Binding Catalog가 runtime importer에 도달하기 전에 fail closed 됩니다.

## 실시간 등록 코드의 제한

등록 코드의 컨테이너는 한 단계 배열/리스트, map, set, queue, stack이며 요소는 스칼라, 문자열 또는 선언된 열거형이어야 합니다. map 키와 set 요소에는 위의 키 규칙도 적용됩니다. 중첩 컨테이너와 호스트 객체 요소는 지원하지 않습니다. multi-map, multi-set, priority queue 및 deque에는 SDK 값 API나 명시적 호스트 래퍼를 사용합니다.

등록 코드는 컨테이너를 다루는 정적 필드, 속성 및 메서드를 `VSCB3004`로 거부합니다. 공개 인스턴스 필드, 속성 또는 생성자 이외 메서드가 있는 구조체도 읽기 전용 여부와 관계없이 `VSCB3001`로 거부합니다. 이는 등록 생성기의 제한이며 메타데이터만 생성할 때와 AOT 브리지는 따로 검사합니다. AOT 브리지는 구조체의 공개 인스턴스 메서드, 쓰기 가능한 인스턴스 필드 및 일반 공개 setter를 `VSCB3006`으로 거부합니다. 박싱된 수신 객체의 쓰기 반영을 유지할 수 없기 때문입니다. [C# AOT](aot.md)를 참조하세요.

## 단일 파일 명령

```powershell
veroscript-csharp-bindgen HostApi.cs `
  --catalog generated/bindings.json `
  --registration generated/Bindings.g.cs `
  --aot-bridge generated/Bindings.Aot.g.cs `
  --api-reference generated/Bindings.vs.txt
```

## 프로젝트 명령과 출력

```powershell
veroscript-csharp-bindgen Models/A.cs Models/B.cs `
  --catalog generated/bindings.json `
  --registration generated/Bindings.g.cs `
  --aot-bridge generated/Bindings.Aot.g.cs `
  --api-reference-dir generated/api `
  --manifest generated/bindings.manifest.json
```

프로젝트 모드는 partial 선언을 병합하고 안정적인 참조 이름을 배정한 뒤 선택한 모든 출력을 교체 전에 준비합니다. 생성 실패 시 기존 출력을 유지하고 쓰기 실패 시 복원을 시도합니다. `--reference`는 반복할 수 있습니다. `--language-version`은 C# 구문 버전을 선택하며 기본값은 `latest`입니다. `--api-reference`와 `--api-reference-dir`는 함께 사용할 수 없습니다. API 참조와 `--manifest`를 함께 출력하려면 `--api-reference-dir`를 사용합니다. `--manifest`만 지정할 때는 API 참조 출력이 필요하지 않습니다.

## 실행 경계

등록 콜백은 여러 VM 작업자에서 병렬 실행될 수 있으므로 공유 호스트 상태를 동기화합니다. AOT 브리지는 관리 선언을 직접 호출합니다. 같은 생성 작업의 Catalog, 브리지, 원래 선언과 프로그램을 함께 사용합니다.

## 생성 컨테이너의 실행 경계

Binding Catalog metadata는 ordered map을 표현할 수 있습니다. live registration은 `SortedDictionary<TKey, TValue>`를 `VSCB3002`, 생성 AOT bridge는 `VSCB3003`으로 거부합니다. 실행 통합에는 명시적인 host wrapper를 사용하십시오. live container marshalling은 managed snapshot을 만들며 성공한 콜백의 `ref`/`out` 매개변수만 다시 씁니다. 값으로 전달된 컨테이너의 변경은 script container를 갱신하지 않습니다. AOT bridge는 지원되는 managed container를 직접 전달하여 object identity를 유지합니다.

## 링크된 출력 경로

Windows의 `\\?\`, `\\.\` 및 볼륨 GUID 경로 별칭은 최종 파일 시스템 위치로 비교합니다. 이런 별칭으로 검증을 우회할 수 없으며 서로 다른 출력 경로는 계속 지원합니다.

BindGen과 Transpiler CLI는 출력을 준비하기 전에 기존 디렉터리 및 파일 링크를 확인합니다. symlink나 Windows junction 경로로 입력 소스를 덮어쓰거나 같은 출력을 두 번 지정할 수 없습니다. 링크된 디렉터리 안의 서로 다른 출력은 계속 지원합니다.

추상 호스트 클래스의 지원되는 메서드와 팩터리가 반환한 인스턴스는 사용할 수 있지만 생성자는 Catalog, 실시간 등록 및 AOT 브리지에 내보내지 않습니다. 바깥 형식의 제네릭 매개변수에 의존하는 중첩 형식은 특성으로 선택한 경우에도 `VSCB1001`로 거부합니다.

AOT의 `ref`, `out`, `in`, `ref readonly` 매개변수는 표현이 일치하는 기본 형식과 지원 컨테이너의 원래 저장 위치를 공유하여 인수 간 별칭을 유지합니다. 문자열, 문자, 열거형 또는 호스트 개체처럼 표현 변환이 필요한 참조 매개변수는 `VSCB3007`로 거부합니다. 명시적인 partial 브리지를 제공하려면 `EmitRegistrationSource=false`와 `EmitAotBridgeSource=false`를 모두 설정하여 Catalog를 생성하세요.

## 출력 이름 충돌

BindGen API 참조 이름은 모든 호스트에서 대소문자를 구분하지 않고 비교합니다. 기본 이름이 같으면 안정적인 경로 해시를 붙이고, 충돌이 남으면 숫자 접미사를 추가합니다. 입력 경로 순서로 할당하므로 입력 목록을 재정렬해도 이름은 바뀌지 않습니다. 반환된 API 참조 레코드나 매니페스트에서 `.api.vs.txt` 파일을 찾으세요.

<a id="binding-marker-attributes"></a>

선택용 특성의 정의는 입력 선언에서 제공하며 SDK가 삽입하지 않습니다. 라이브러리 기본값은 `BindingAttributeName="VeroScriptBindingAttribute"`, `IgnoreAttributeName="VeroScriptIgnoreAttribute"`이며 다른 동등한 이름을 지정할 수 있습니다. 생성 등록의 기본 형식은 `VeroScript.GeneratedBindings.VeroScriptCSharpBindings`이며 `RegistrationNamespace`와 `RegistrationClassName`으로 바꿉니다. 이름 설정만으로 비활성 등록 출력을 켜지는 않습니다.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/bindgen.md) · [fr-FR](../fr-FR/bindgen.md) · [ja-JP](../ja-JP/bindgen.md) · [ko-KR](bindgen.md) · [zh-CN](../zh-CN/bindgen.md) · [zh-TW](../zh-TW/bindgen.md)
