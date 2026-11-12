# 01: C# 바인딩 산출물 생성

같은 managed 선언에서 catalog, API reference, 실행 등록 소스와 AOT bridge를 만듭니다. semantic frontend는 빌드 시 소스를 분석하며 스크립트 세션을 만들지 않습니다.

일치하는 `veroscript-csharp-bindgen`과 `veroscript`를 PATH에 두고 쓰기 가능한 학습 디렉터리와 SDK에 맞는 .NET host project를 준비합니다. 모든 명령을 이 디렉터리에서 실행합니다. 생성, host 컴파일, callback 등록은 별도 작업입니다.

아래 AOT 실행은 생성된 `net8.0` project를 사용합니다. 해당 target을 빌드할 .NET SDK와 실행할 일치 .NET runtime을 설치합니다.

## 1단계: 입력 준비

host 선언을 `HostApi.cs`로 저장합니다.

```csharp
public sealed class HostApi
{
	public static int Add(int left, int right)
	{
		return left + right;
	}
}
```

호출 스크립트를 `app.vs`로 저장합니다.

```VeroScript
int main()
{
	print(HostApi.Add(20, 22));
	return 0;
}
```

## 2단계: 실행 등록 생성

```powershell
veroscript-csharp-bindgen HostApi.cs `
	--catalog generated/bindings.json `
	--registration generated/Bindings.g.cs `
	--api-reference generated/Bindings.vs.txt
veroscript bindings validate generated/bindings.json
```

`bindings.json`, `Bindings.g.cs`, `Bindings.vs.txt`가 생성됩니다. 등록 소스를 managed C# SDK 및 원본 `HostApi.cs`와 컴파일하고 스크립트 로드 전에 생성된 등록 진입점을 호출합니다. catalog만으로 callback이 설치되지 않습니다. 설정된 host에서 스크립트를 실행하면 `42`를 출력하고 0을 반환합니다.

구성된 VM host에서 `app.vs` 로드 전에 생성된 등록 진입점을 호출합니다. `session`은 host가 이미 보유한 `VeroScriptSession`입니다.

```csharp
VeroScript.GeneratedBindings.VeroScriptCSharpBindings.Register(session);
```

## 3단계: AOT bridge 생성

```powershell
veroscript-csharp-bindgen HostApi.cs `
	--catalog generated/bindings.json `
	--aot-bridge generated/Bindings.Aot.g.cs
veroscript aot --target=csharp app.vs `
	--binding-catalog generated/bindings.json `
	--output-dir generated/app --bundle-name App
```

`HostApi.cs`, `Bindings.Aot.g.cs`, 생성된 모든 앱 단위를 같은 .NET project에서 컴파일합니다. bridge는 `VeroScriptInterop` 없이 managed code를 직접 호출합니다. 생성 project의 runtime 의존성을 보존하며 소스 생성만으로 실행 파일이 되지는 않습니다.

생성된 `generated/app/App.csproj`는 `EnableDefaultCompileItems=false`를 설정하고 생성 단위를 명시합니다. 기존 항목을 보존하고 다음 `ItemGroup`을 `Project` 요소 안에 추가합니다. 경로 기준은 `App.csproj`입니다.

```xml
<ItemGroup>
	<Compile Include="../../HostApi.cs" />
	<Compile Include="../Bindings.Aot.g.cs" />
</ItemGroup>
```

학습 디렉터리에서 managed application을 실행합니다. 예상 출력은 `42`, 종료 코드는 0입니다.

```powershell
dotnet run --project generated/app/App.csproj --configuration Release
```

이 AOT project에는 `Bindings.Aot.g.cs`를 사용합니다. `Bindings.g.cs`는 2단계의 VM host용입니다. bundle 재생성은 `App.csproj`를 교체하므로 추가 입력도 다시 적용합니다.

## 4단계: 프로젝트 생성

다음을 `Models/State.cs`로 저장하고 1단계의 `HostApi.cs`를 유지합니다.

```csharp
public enum HostState
{
	Ready,
	Done
}
```

```powershell
veroscript-csharp-bindgen HostApi.cs Models/State.cs `
	--catalog generated/bindings.json `
	--registration generated/Bindings.g.cs `
	--aot-bridge generated/Bindings.Aot.g.cs `
	--api-reference-dir generated/api `
	--manifest generated/bindings.manifest.json
```

두 파일을 함께 전달하고 같은 실행에서 나온 catalog, 등록, bridge, reference, manifest를 보존합니다. partial/관련 선언은 출력 전에 병합합니다. 같은 stem은 안정적인 path-hash suffix를 받습니다.

project 생성 뒤 AOT application을 다시 빌드하려면 새 merged catalog로 3단계 AOT 명령을 먼저 반복합니다. 빌드 전에 `<Compile Include="../../Models/State.cs" />`를 포함한 project 입력을 다시 추가합니다. catalog, bridge, host 선언, 생성 application을 같은 입력 집합으로 유지합니다.

## 5단계: 계약 확인

catalog 이름/서명을 host source와 비교하고 API reference를 확인합니다. 기본 대상은 공개 선언입니다. `[VeroScriptBinding]`을 정의/사용하고 `--require-attribute`로 표시된 타입을 선택하며 `[VeroScriptIgnore]`로 제외합니다. 지원하지 않는 nullable/pointer/adapter 서명은 진단을 만듭니다. 생성 metadata를 편집해 불일치를 숨기지 말고 명시적 wrapper를 사용합니다.

## 연습

1. 지원되는 읽기 전용 property를 추가하고 계약을 확인합니다.
2. binding attribute를 정의해 타입 하나에 붙이고 `--require-attribute` 결과를 비교합니다.
3. 실행 등록과 AOT bridge를 사용하는 host를 설명합니다.
4. 미지원 nullable 서명의 진단을 확인한 뒤 원래 API로 되돌립니다.

[BindGen reference](../../../../../Extensions/Language/CSharp/Resources/Docs/User/ko-KR/bindgen.md) · [C# AOT](../../../../../Extensions/Language/CSharp/Resources/Docs/User/ko-KR/aot.md) · [C# 소스 변환](../../../../../Extensions/Language/CSharp/Resources/Docs/User/ko-KR/transpiler.md) · [런타임 값과 소유권](../../../../../Extensions/Language/CSharp/Resources/Docs/User/ko-KR/index.md)

<!-- docs-navigation -->

---

[튜토리얼](../index.md)

언어: [en-US](../../en-US/csharp/01-csharp-source-tools.md) · [fr-FR](../../fr-FR/csharp/01-csharp-source-tools.md) · [ja-JP](../../ja-JP/csharp/01-csharp-source-tools.md) · [ko-KR](01-csharp-source-tools.md) · [zh-CN](../../zh-CN/csharp/01-csharp-source-tools.md) · [zh-TW](../../zh-TW/csharp/01-csharp-source-tools.md)
