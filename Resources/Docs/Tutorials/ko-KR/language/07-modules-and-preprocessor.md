# 07: 모듈, 가져오기 및 전처리기

프로그램이 두 개 이상의 파일에 걸쳐 있으면 모듈은 종속성과 공개 API를 구성합니다. 전처리기는 컴파일 전에 소스를 선택합니다. 이 튜토리얼은 비공개 구현으로 시작하여 다른 파일에 필요한 기능만 내보냅니다.

## 1단계: 모듈 생성

`math_utils.vs`을 생성합니다:

```VeroScript
export int square(int value)
{
	return value * value;
}

int hidden_value()
{
	return 99;
}
```

`export`은 `square`을 모듈 공개 선언의 일부로 만듭니다. `hidden_value`는 모듈 전용이며 공용 라이브러리 API가 아닙니다.

`main.vs`을 생성합니다:

```VeroScript
import "math_utils.vs";

int main()
{
	print(square(7));
	return 0;
}
```

동일한 디렉터리에서 두 파일을 모두 실행합니다.

```powershell
veroscript --check main.vs
veroscript main.vs
```

## 2단계: 모듈 다시 내보내기

`facade.vs`을 생성합니다:

```VeroScript
export import "math_utils.vs";
```

진입점은 이제 파사드만 가져올 수 있습니다:

```VeroScript
import "facade.vs";

int main()
{
	print(square(8));
	return 0;
}
```

다시 내보내면 원래 모듈이 내보낸 선언이 계속 노출됩니다. `hidden_value`을 공개하지 않습니다.

## 3단계: 컴파일 시간 조건 추가

전처리기는 컴파일 시간에 소스를 선택합니다. 런타임 판단에는 일반 `if` 문을 사용하세요.

```VeroScript
#define MESSAGE "development"

#ifdef RELEASE
#define MESSAGE "release"
#endif

int main()
{
	print(MESSAGE);
	return 0;
}
```

명령줄에서 `RELEASE`을 정의합니다.

```powershell
veroscript --check main.vs
veroscript -D RELEASE main.vs
```

매크로는 컴파일 전에 확장됩니다. 매크로에 함수와 동일한 이름을 지정하지 말고 논리가 중요해지면 실제 함수를 사용하십시오.

## 4단계: 익명 네임스페이스로 선언 그룹화

익명 네임스페이스는 `namespace { ... }`로 작성합니다. 추가 한정 없이 선언을 그룹화할 수 있습니다. VeroScript에서는 투명하게 처리되므로 합성 네임스페이스를 만들지 않으며 C++와 같은 내부 링크도 제공하지 않습니다.

```VeroScript
namespace
{
	int local_helper()
	{
		return 6;
	}
}

int main()
{
	print(local_helper());
	return 0;
}
```

이름이 있는 네임스페이스 안에서는 멤버가 바깥쪽 네임스페이스의 한정을 유지합니다.

```VeroScript
namespace Diagnostics
{
	namespace
	{
		int code()
		{
			return 200;
		}
	}
}

int main()
{
	print(Diagnostics::code());
	return 0;
}
```

다른 모듈에서 사용해야 하는 개별 선언에 `export`를 지정하십시오. 익명 네임스페이스 자체는 내보낼 수 없으며, 그 자체만으로 선언을 비공개로 만들지도 않습니다.

## 연습

1. `cube` 함수를 내보내고 기본 모듈에서 호출합니다.
2. 문자열 모듈과 수학 모듈을 모두 다시 내보내는 Facade를 만듭니다.
3. 디버그 빌드에 추가 정보를 인쇄하는 `#if` 분기를 추가합니다.
4. 이름이 있는 네임스페이스 안의 익명 네임스페이스에 헬퍼를 배치하고 바깥쪽 네임스페이스 한정을 사용해 호출합니다.

[모듈](../../../../../VeroScript/Resources/Docs/User/ko-KR/language-basics/modules.md), [전처리기](../../../../../VeroScript/Resources/Docs/User/ko-KR/language-basics/preprocessor.md) 및 [네임스페이스](../../../../../VeroScript/Resources/Docs/User/ko-KR/language-basics/namespaces.md)를 참조하세요. 배포 예제는 [`examples/reexport_main.vs`](../../../../../VeroScript/Resources/Scripts/examples/reexport_main.vs) 및 [`examples/preprocessor.vs`](../../../../../VeroScript/Resources/Scripts/examples/preprocessor.vs)입니다.

다음: [문자열, 파일 및 경로](../standard-library/08-strings-files-and-paths.md).

<!-- docs-navigation -->

---

[튜토리얼](../index.md)

언어: [en-US](../../en-US/language/07-modules-and-preprocessor.md) · [fr-FR](../../fr-FR/language/07-modules-and-preprocessor.md) · [ja-JP](../../ja-JP/language/07-modules-and-preprocessor.md) · [ko-KR](07-modules-and-preprocessor.md) · [zh-CN](../../zh-CN/language/07-modules-and-preprocessor.md) · [zh-TW](../../zh-TW/language/07-modules-and-preprocessor.md)
