# 11: 디버깅 및 AOT 생성

이 튜토리얼에서는 디버거로 스크립트를 검사하고 배포 대상에 맞는 AOT 대상을 선택하는 방법을 다룹니다.

## 1단계: 관찰 가능한 스크립트 준비

```VeroScript
int calculate(int value)
{
	int result = value * 2;
	return result + 1;
}

int main()
{
	int input = 20;
	int output = calculate(input);
	print(output);
	return 0;
}
```

`debug_demo.vs`으로 저장한 후 정상적으로 확인합니다.

```powershell
veroscript --check debug_demo.vs
```

## 2단계: DAP 디버그 세션 시작

표준 입력/출력 모드는 편집기나 다른 디버그 클라이언트가 실행하고 관리하는 데 적합합니다.

```powershell
veroscript --debug debug_demo.vs
```

TCP를 통해 연결하는 클라이언트의 경우:

```powershell
veroscript --debug --dap-port=4711 --dap-bind=127.0.0.1 debug_demo.vs
```

클라이언트에서 중단점, 단계별 실행 및 변수 검사를 구성합니다. 디버깅은 VM 디버그 경로를 사용하므로 디버그 시간 동작을 일반적인 실행에 대한 성능 측정으로 사용하지 마십시오.

source breakpoint는 active code에 설정하십시오. DAP client에서는
`setBreakpoints`로 전송합니다. launch 전에 설정한 breakpoint는 pending 상태이며 program이 load되면
client는 같은 breakpoint ID와 `reason: "changed"`를 가진 `breakpoint` event를 받습니다. 항상 반환된
verified state와 line을 사용하십시오. `#if`로 제거된 line, source 범위 밖의 line, executable location이
없는 line은 unverified 상태로 남습니다.

## 3단계: AOT 소스 생성

스크립트에 동적 로딩이 필요하지 않다면 배포 대상에 맞는 AOT 대상을 선택합니다. 기본 대상은 C++17입니다.

```powershell
veroscript aot debug_demo.vs -o generated.cpp
```

`generated.cpp`은 연결된 실행 파일이 아닙니다. 호스트 프로젝트에는 여전히 호환 가능한 C++17 컴파일러와 VeroScript 런타임 지원이 필요합니다. 직접 실행하려고 하기보다는 생성된 파일을 검사하여 대상 프로젝트에 빌드합니다.

독립 Python 프로그램은 Python 대상을 명시적으로 선택합니다.

```powershell
veroscript aot --target=python debug_demo.vs -o generated.py
python generated.py
```

C# 또는 NativeAOT 배포에는 소스 bundle을 생성합니다.

```powershell
veroscript aot --target=csharp debug_demo.vs --output-dir generated-csharp --bundle-name Demo
dotnet publish generated-csharp/Demo.csproj -c Release
```

지원 범위와 제한은 [Python AOT](../../../../../Extensions/Language/Python/Resources/Docs/User/ko-KR/aot.md) 및 [C# AOT](../../../../../Extensions/Language/CSharp/Resources/Docs/User/ko-KR/aot.md)을 참조하십시오. 의미를 보존할 수 없는 구문은 진단과 함께 중단됩니다.

## 모드 선택

- 빠른 반복, 핫 리로드 및 대화형 디버깅: VM 또는 기본 혼합 백엔드.
- 중단점 및 변수 검사: `--debug`.
- 네이티브 C++ 빌드: C++ AOT를 사용하고 호스트 바인딩 및 동시성 동작을 확인합니다.
- 독립 Python 소스: `--target=python`을 사용하고 대상 Python에서 테스트합니다.
- .NET 또는 NativeAOT: `--target=csharp`를 사용하고 생성된 bundle을 빌드합니다.

## 연습

1. `calculate` 내부에 중단점을 설정하고 `input` 및 `result`를 검사합니다.
2. 입력을 변경하고 일반 출력과 디버그 출력을 비교합니다.
3. 배열을 사용하는 스크립트에 대한 C++ AOT 소스를 생성하고 대상 프로젝트가 이를 컴파일할 수 있는지 확인합니다.
4. 같은 스크립트를 Python 또는 C#으로 생성하고 각 대상의 제한 진단을 비교합니다.

[디버깅](../../../../../VeroScriptTooling/Resources/Docs/User/ko-KR/debugging.md), [C++ AOT](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ko-KR/aot.md), [Python AOT](../../../../../Extensions/Language/Python/Resources/Docs/User/ko-KR/aot.md), [C# AOT](../../../../../Extensions/Language/CSharp/Resources/Docs/User/ko-KR/aot.md)을 참조하세요. 다음: [호스트 바인딩 및 스크립트 통합](../advanced/12-host-bindings.md).

<!-- docs-navigation -->

---

[튜토리얼](../index.md)

언어: [en-US](../../en-US/toolchain/11-debugging-and-aot.md) · [fr-FR](../../fr-FR/toolchain/11-debugging-and-aot.md) · [ja-JP](../../ja-JP/toolchain/11-debugging-and-aot.md) · [ko-KR](11-debugging-and-aot.md) · [zh-CN](../../zh-CN/toolchain/11-debugging-and-aot.md) · [zh-TW](../../zh-TW/toolchain/11-debugging-and-aot.md)
