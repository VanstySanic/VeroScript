# 02: 인수 확인, 실행 및 읽기

스크립트가 다른 입력을 받아들이자마자 소스의 데이터를 하드 코딩하는 것이 불편해집니다. 이 자습서에서는 명령줄 인수를 읽고 `--check`을 사용하여 프로그램을 실행하기 전에 유효성을 검사합니다.

## 1단계: 인수 수락

`argv_echo.vs`을 생성합니다:

```VeroScript
int main(string[] args)
{
	print("argc=" + to_string(len(args)));
	for (int i = 0; i < len(args); i = i + 1)
	{
		print(args[i]);
	}
	return 0;
}
```

`main(string[] args)`은 스크립트 경로 다음에 인수를 받습니다. `args`에는 스크립트 파일명이 포함되어 있지 않으므로 `len(args)`는 스크립트에 전달된 값의 개수입니다.

## 2단계: 인수 전달

```powershell
veroscript --check argv_echo.vs
veroscript argv_echo.vs red blue
```

출력은 다음과 유사합니다.

```text
argc=2
red
blue
```

인수는 문자열입니다. `to_int` 같은 숫자 변환 함수는 숫자나 불리언 값을 받으며 문자열을 숫자로 파싱하지 않습니다. 인수를 숫자로 사용하려면 허용할 문자열 형식을 정의하고 검증한 뒤 계산하세요. [변환 함수](../../../../../VeroScript/Resources/Docs/User/ko-KR/standard-library/builtins.md)를 참고하세요.

## 3단계: 인사말 도구 구축

다음 프로그램을 `argv_echo.vs`와 같은 디렉터리에 `greet.vs`로 저장하세요.

```VeroScript
int main(string[] args)
{
	string name = "world";
	if (len(args) > 0)
	{
		name = args[0];
	}

	print("hello, " + name);
	return 0;
}
```

프로그램을 검사한 다음 인수 없이 실행하고 인수를 넣어 실행하세요.

```powershell
veroscript --check greet.vs
veroscript greet.vs
veroscript greet.vs VeroScript
```

인수 없는 실행은 기본값을 사용하고, 인수를 넣은 실행은 첫 번째 인수를 대신 사용합니다.

## 4단계: 실행 백엔드 선택

일반 실행에서는 기본적으로 혼합 백엔드를 사용합니다. 실행 모드를 비교할 때 백엔드를 명시적으로 선택할 수 있습니다.

```powershell
veroscript --vm-backend=vs-bytecode --backend=vm argv_echo.vs one two
veroscript --jit-backend="/path/to/Jit" --backend=jit argv_echo.vs one two
```

VM 명령은 Runtime에 포함된 인터프리터로 실행합니다. JIT 명령을 시도하기 전에 `/path/to/Jit`를 호환 백엔드로 바꾸세요. 로드에 실패하면 스크립트 실행 전에 종료합니다. 성공하면 두 명령 모두 앞에서처럼 `one`과 `two`를 `argv_echo.vs`에 전달합니다. 프로젝트 기본값, 자동 선택 및 JIT 비활성화는 [CLI 백엔드 참조](../../../../../VeroScriptCli/Resources/Docs/User/ko-KR/index.md#cli-backend-policy)를 확인하세요.

디버깅에는 VM 디버그 경로가 사용됩니다. 지금은 워크플로를 기억하세요. 개발 중에 `--check`을 사용하고, 실행을 비교하거나 제한할 이유가 있는 경우에만 백엔드를 선택하세요.

## 연습

1. 인수가 제공되지 않으면 `usage: greet.vs <name>`을 인쇄하고 0이 아닌 코드를 반환합니다.
2. 두 번째 인수가 있는지 확인하고 `len`으로 길이를 출력합니다.
3. `--lang en-US`과 `--lang ko-KR`의 진단을 비교합니다.

다음: [값, 조건 및 루프](../language/03-values-and-control-flow.md).

전체 예는 [`examples/argv_echo.vs`](../../../../../VeroScript/Resources/Scripts/examples/argv_echo.vs)입니다. 옵션은 [CLI 참조](../../../../../VeroScriptCli/Resources/Docs/User/ko-KR/index.md)를 참조하세요.

성능 비교에서는 같은 입력과 백엔드로 일반 실행을 사용하고 워밍업을 기다리세요. 디버깅, 유지된 초기화 및 제한된 정리는 VM을 사용하며 영구 세션의 적격 일반 함수는 선택한 JIT를 사용할 수 있습니다. 바이트코드 캐시는 시작 시 컴파일 작업을 줄입니다. 세션 JIT 캐시와 제한은 [C++ 임베딩 가이드](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ko-KR/embedding.md#threads-and-hot-reload)를 참조하세요.

<!-- docs-navigation -->

---

[튜토리얼](../index.md)

언어: [en-US](../../en-US/getting-started/02-cli-and-arguments.md) · [fr-FR](../../fr-FR/getting-started/02-cli-and-arguments.md) · [ja-JP](../../ja-JP/getting-started/02-cli-and-arguments.md) · [ko-KR](02-cli-and-arguments.md) · [zh-CN](../../zh-CN/getting-started/02-cli-and-arguments.md) · [zh-TW](../../zh-TW/getting-started/02-cli-and-arguments.md)
