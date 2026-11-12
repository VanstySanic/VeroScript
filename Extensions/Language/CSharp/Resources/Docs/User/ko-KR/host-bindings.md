# C# 호스트 바인딩

## interception 및 mutation

interceptor는 `SetInstanceCallInterceptor`, mutation handler는
`SetMutationHandler`로 등록합니다. `CallMethodBypassingInterceptor`를 사용하면
interceptor를 거치지 않고 작업을 호출할 수 있습니다.

`VeroScriptNativeType`은 기본 타입, 호스트 타입과 타입 인자를 재귀적으로 설명합니다. 함수의 반환 타입, 인자와 `VeroScriptParameterFlags`를 실제 콜백 계약에 맞추십시오.

호스트 타입, 멤버, 제네릭 타입과 별칭을 등록할 수 있습니다. `Construct`, `CallMethod`, `GetField`, `SetField`는 등록된 멤버만 노출합니다. 인터셉터는 `VeroScriptInterceptResult`로 계속, 대체 또는 거부를 명확히 반환하며 접근 정책을 우회해서는 안 됩니다.

제네릭 타입 팩토리의 `VeroScriptGenericHostTypeContext`는 `ArgumentCount`,
`GetArgument`, `SetType`, `AddMember`, `SetProtocol`, `SetError`를 제공합니다.
인터셉트 컨텍스트는 `Receiver`, `ArgumentCount`, `GetTarget`, `GetArgument`,
`Proceed`, `SetError`를 제공하며 차용 값은 콜백 밖으로 보관할 수 없습니다.

직렬화기와 `SerializeObject`/`DeserializeObject`는 타입, 버전, 데이터 출처를 검증해야 합니다. `SetMutationHandler`는 `VeroScriptMutationKind`를 전달하며, 직렬화 컨텍스트는 `GetField`/`SetBytes`, 역직렬화 컨텍스트는 `EncodedVersion`/`GetBytes`/`AddField`를 사용합니다. 차용 값은 콜백 반환 전에 소비하십시오. 이미 완료된 변경은 알림으로 되돌릴 수 없습니다. 관리 예외가 네이티브 ABI를 넘어가지 않도록 상태 또는 `VeroScriptException`으로 처리합니다.

## 함수와 콜백

`RegisterFunction`에 반환 형식과 매개변수 형식을 명시합니다.

```CSharp
session.RegisterFunction(
	"host_double",
	VeroScriptNativeType.Int32,
	[VeroScriptNativeType.Int32],
	(context, result) => result.Set(context.GetArgument(0).GetInt32() * 2));
```

`GetArgument`는 읽기 전용 인수를 제공합니다. 변경 가능한 참조로 선언된 인수에만 `GetMutableArgument`를 사용하십시오. 콜백은 전달된 borrowed return view에 결과를 기록하고 의도된 실패에는 `context.SetError(...)`를 호출합니다. `ArgumentCount`, `ProgramGeneration`, `GetHostThis`는 호출 정보를 제공합니다. 관리 예외는 콜백 실패로 변환됩니다.

세부 메타데이터에는 `VeroScriptHostFunctionDefinition`을 사용해 문서, 매개변수 이름과 설명, 주석, 형식 매개변수, 제네릭 호스트 객체 플래그를 지정합니다. `ApplyClassDefaults`는 객체 생성 또는 로드 후 등록된 기본값을 적용하고, `EnumerateObjectReferences`는 스크립트 및 호스트 참조를 방문합니다. 방문자에게 전달된 borrowed 값은 콜백 반환 시 만료됩니다. `RetainCallable`은 스크립트 함수 또는 클로저의 소유 참조를 만듭니다.

`RegisterFunction` 외에 `RegisterHostFunction`, `RegisterAnnotation`, `RegisterHostType`, `RegisterHostMember`, `RegisterHostProtocol`, `RegisterTypeAlias`, `RegisterGenericHostTypeTemplate`, `InstantiateGenericHostType`를 사용할 수 있습니다. `VeroScriptNativeParameter`는 형식과 `None`, `Ref`, `ConstRef`를 결합합니다. 콜백이 스크립트 저장소에 write-back해야 하는 경우에만 `Ref`를 선택하십시오. `VeroScriptHostTypeDefinition`과 `VeroScriptHostMemberDefinition`은 노출할 enum/struct/class, 생성자, 메서드, 속성, 필드, 상수, 연산자를 설명합니다.

## 어노테이션 이름 일치 정책

`RegisterAnnotation`의 `VeroScriptAnnotationDefinition.CaseSensitive` 기본값은
`true`입니다. `false`로 설정하면 해당 어노테이션 이름의 ASCII 대소문자를 구분하지
않습니다. 인수 이름과 metadata 키는 정확히 일치해야 하며 리플렉션은 소스 표기를
유지합니다. 정확한 이름 등록과 대소문자를 구분하지 않는 등록이 겹치면 카탈로그를
변경하지 않고 실패합니다. `ExportBindingsJson()`은 `caseSensitive`를 보존합니다.
등록은 컴파일, 리플렉션 및 도구에 문서 정보도 제공하며 프로세서는 별도로 설치합니다.
이름 정책 필드가 추가된 현재 네이티브 SDK와 어댑터를 함께 배포하십시오.

## 시그니처, 중첩 호출과 영속성

`VeroScriptNativeType`은 `Void`, `Bool`, `String`, `HostObject`, `Enum`, `Function` 등을 구별합니다. 컨테이너와 함수는 `TypeArguments`를 사용합니다. 네이티브 `fn(int)->int`는 소스의 `function(int)->int`에 대응하며 이러한 식별자는 번역하지 않습니다.

`VeroScriptCallContext` 콜백 안의 `InvokeFunction`과 `InvokeCallable`은 세션 연산에 재진입하지 않고 스크립트 대상을 호출합니다. 반환된 소유 값은 해제하세요. 빌린 인수와 반환 뷰는 현재 콜백 안에서만 유효합니다. 제네릭 형식의 복제, 동등 비교, 해시, 하위 클래스 프로토콜은 팩터리를 통해 설치되며 세션이 유지합니다.

`VeroScriptInterceptContext`는 `Unhandled`, `Proceed`, 대체 결과, `Failed`를 처리합니다. `RegisterExternalSerializer`는 외부 영속성을 등록합니다. 직렬화 컨텍스트는 `TypeName`, `MutationVersion`, `FieldCount`, `GetField`, `SetBytes`를, 역직렬화는 `EncodedVersion`, `GetBytes`, `AddField`, `SetError`를 제공합니다. 바이트의 형식, 버전, 출처를 검증하세요. 이 API는 패키지나 네이티브 라이브러리를 인증하지 않습니다.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/host-bindings.md) · [fr-FR](../fr-FR/host-bindings.md) · [ja-JP](../ja-JP/host-bindings.md) · [ko-KR](host-bindings.md) · [zh-CN](../zh-CN/host-bindings.md) · [zh-TW](../zh-TW/host-bindings.md)
