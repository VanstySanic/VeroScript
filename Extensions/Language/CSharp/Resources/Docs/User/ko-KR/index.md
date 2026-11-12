# C# SDK

C# 소스 생성, bundle 및 NativeAOT 경계는 [C# AOT](aot.md)도 참조하십시오.

`VeroScriptCSharp`는 안정적인 VeroScript Interop C ABI를 사용하는 NativeAOT 친화적 .NET 어댑터입니다. 관리 세션, 값, 콜백, 호스트 바인딩, 패키지, 리플렉션, 지역화와 JIT 통계를 제공합니다. 관리 패키지는 .NET 8과 .NET 9를 대상으로 하며 네이티브 `VeroScriptInterop`은 별도로 배포하고 프로세스 아키텍처를 맞춰야 합니다.

현재 Interop ABI는 `0x00000101`(ABI 0.1.1)입니다. 관리 어댑터는 ABI 버전의 정확한 일치를 요구합니다. 세션을 만들기 전에 `VeroScriptRuntime.AbiVersion`을 확인하고 관리 패키지와 네이티브 Interop 라이브러리를 일치하는 릴리스에서 배포하십시오.

```CSharp
using System;
using VeroScript;

using var session = new VeroScriptSession(new()
{
	Print = Console.WriteLine,
	Diagnostic = Console.Error.WriteLine,
});
session.ApplyConfiguration(new VeroScriptConfiguration
{
	ExecutionBackend = VeroScriptExecutionBackend.Mixed,
});

var loaded = session.LoadSource("""
    int Add(int left, int right)
    {
        return left + right;
    }

    int main()
    {
        print(Add(20, 22));
        return 0;
    }
    """, "main.vs");
if (!loaded.Success)
{
	throw new VeroScriptException(VeroScriptStatus.EngineError, "load failed");
}

var run = session.Run();
if (!run.Success)
{
	throw new VeroScriptException(VeroScriptStatus.EngineError, "execution failed");
}
```

`VeroScriptSessionOptions.Print`는 스크립트 출력 줄을 받고 `VeroScriptSessionOptions.Diagnostic`은 진단 줄을 받습니다. 두 콜백 모두 선택 사항이며 세션이 해제될 때까지 참조가 유지됩니다.

[세션과 런타임](session.md), [값과 소유권](values.md), [호스트 바인딩](host-bindings.md), [패키지와 리플렉션](packages.md), [C# AOT](aot.md)을 참조하십시오.

[C# 프런트엔드](frontend.md)와 [C# BindGen](bindgen.md)에서 선언 분석,
Catalog 및 AOT bridge를 설명합니다.

[C# 트랜스파일러](transpiler.md)에서는 지원되는 C# 부분집합을 VeroScript
소스로 변환하는 의미 변환을 설명합니다.

- [Android 및 iOS / MAUI](mobile.md)

## 관리 메모리

런타임은 `VeroScriptCapabilities.Memory`를 제공해야 합니다. 호환되는 이전 런타임에서는 네이티브 호출이나 고정 전에 `NotSupportedException`이 발생합니다. 콜백에서는 `context.Memory`, 유휴 상태에서는 `session.Memory`를 사용합니다. 사용자 지정 `Memory<byte>` 관리자는 `Pin`을 지원하고 마지막 참조를 해제하는 스레드에서 `Unpin`을 허용해야 합니다. 등록 실패 시 고정이 해제됩니다.

콜백 메모리 작업과 `GetArgumentAddress`는 해당 콜백의 관리 스레드에서 실행해야 합니다. 다른 스레드에서 접근하면 `WrongThread`가 보고됩니다. 사용을 마친 임대는 다른 스레드에서 해제할 수 있습니다.

호스트 API: `session.Memory` / `SetMemoryLimits`. 소유권, 할당량, 뷰 만료와 임대는 [`std::memory`](../../../../../../../VeroScript/Resources/Docs/User/ko-KR/standard-library/memory.md) 문서를 참고하세요.

- [호스트에서 사용하는 관리 메모리](memory.md)

## 소스 도구의 진단 언어

C# 프런트엔드와 C# BindGen/Transpiler의 자체 도움말 및 진단은 현재 영어이며 Roslyn 진단은 고정 문화권으로 수집합니다. BindGen/Transpiler CLI는 `--lang`을 받지 않습니다. `--language-version`은 표시 언어가 아닌 C# 구문 버전을 선택합니다. `VeroScriptRuntime.CurrentLanguage`로 지정하는 네이티브 런타임 진단 언어와는 별개입니다.

## variant

[인터롭 SDK — variant](../../../../../../../VeroScriptInterop/Resources/Docs/User/ko-KR/index.md#exported-value-lifetime)을 참조하세요.

```csharp
using var payload = new VeroScriptValue();
payload.Set(42);
using var box = session.CreateVariant("int", payload);
string type = box.VariantTypeName;
using var extracted = box.GetVariantValue();
```

`VeroScriptSession.LoadApplication(absolutePath, optionsJson)`로 애플리케이션을 로드하고 결과를 확인한 뒤 `Run()`을 호출합니다. 호스트 옵션의 경로는 절대 경로여야 합니다. 환경, 서명 및 신뢰 설정은 [Application 로드 계약](../../../../../../../VeroScriptApplication/Resources/Docs/User/ko-KR/index.md)을 따릅니다.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
