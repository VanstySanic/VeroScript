# 01: 첫 번째 스크립트

이 튜토리얼의 목표는 하나입니다. 스크립트를 작성하고 실행하고 출력 라인을 확인하는 것입니다.

## 무엇을 배울 것인가

- 최소한의 VeroScript 프로그램에는 무엇이 포함되어 있습니까?
- 스크립트를 실행하는 방법.
- 스크립트를 실행하지 않고 확인하는 방법.
- `main` 및 `print`의 기능.

## 1단계: 파일 만들기

작업 디렉터리에 `hello.vs`을 만듭니다.

```VeroScript
int main()
{
	print("hello, VeroScript");
	return 0;
}
```

두 가지 중요한 부분이 있습니다. `main`은 스크립트 진입점이고 `int`은 프로세스 종료 상태를 반환합니다. `print`는 표준 출력에 문자열을 씁니다. `void main`은 계속 지원되지만 전체 프로그램은 일반적으로 `int main`를 사용하고 성공을 위해 명시적으로 `0`를 반환합니다.

## 2단계: 확인 후 실행

`hello.vs`이 포함된 디렉터리에서 다음 명령을 실행합니다.

```powershell
veroscript --check hello.vs
veroscript hello.vs
```

출력은 다음과 같습니다.

```text
hello, VeroScript
```

`--check`은 `main`을 실행하지 않고 파일을 컴파일하고 문제를 보고합니다. 개발 중에 먼저 사용하고 검사가 성공한 후 스크립트를 실행하십시오.

## 3단계: 출력을 유용하게 만들기

스크립트를 다음으로 변경합니다.

```VeroScript
int main()
{
	string name = "Ada";
	print("hello, " + name);
	return 0;
}
```

이제 다음이 표시됩니다.

```text
hello, Ada
```

`string name`은 문자열 변수를 선언합니다. `+` 연산자는 두 문자열을 연결합니다.

## 무슨 일이 생기면

- `veroscript`을 찾을 수 없는 경우 CLI가 설치되어 있고 `PATH`에 있는지 확인합니다.
- `hello.vs`를 찾을 수 없는 경우, 터미널의 현재 디렉터리를 확인하거나 절대 경로를 전달하세요.
- `veroscript --lang en-US --check hello.vs`을 실행하여 영어 진단을 요청합니다.

## 연습

1. 이름을 자신의 이름으로 바꾸십시오.
2. `string language = "VeroScript";`을 추가하고 두 변수를 모두 사용하여 문장을 인쇄합니다.
3. 세미콜론을 제거하고 `--check`을 실행한 후 진단 내용을 읽고 파일을 수정합니다.

다음: [인수 확인, 실행 및 읽기](02-cli-and-arguments.md).

전체 최소 예는 [`examples/hello.vs`](../../../../../VeroScript/Resources/Scripts/examples/hello.vs)입니다. 구문은 [기본 구문](../../../../../VeroScript/Resources/Docs/User/ko-KR/language-basics/basic-syntax.md)을 참조하세요.

<!-- docs-navigation -->

---

[튜토리얼](../index.md)

언어: [en-US](../../en-US/getting-started/01-first-script.md) · [fr-FR](../../fr-FR/getting-started/01-first-script.md) · [ja-JP](../../ja-JP/getting-started/01-first-script.md) · [ko-KR](01-first-script.md) · [zh-CN](../../zh-CN/getting-started/01-first-script.md) · [zh-TW](../../zh-TW/getting-started/01-first-script.md)
