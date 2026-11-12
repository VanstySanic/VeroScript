# 유형 별칭 및 특수 선언

VeroScript는 유형 별칭을 지원하고 `= default` 또는 `= delete`을 사용하여 생성자, 소멸자, 메서드, 연산자 및 자유 함수의 상태를 명시적으로 만듭니다.

## 유형 별칭

기존 유형에 다른 이름을 지정하려면 `using`을 사용하십시오.

```VeroScript
using Ints = array<int>;
using Scores = map<string, int>;

Ints values = Ints{1, 2, 3};
Scores scores = Scores();
```

`typedef`은 유형 별칭에도 지원됩니다.

```VeroScript
typedef Names = array<string>;
Names names = Names();
```

유형 별칭은 새로운 런타임 유형을 생성하지 않습니다. 별칭은 모듈 범위나 로컬 블록에서 선언될 수 있습니다. 로컬 별칭은 해당 범위에서만 표시됩니다.

## 별칭 내보내기

네임스페이스 내부를 포함한 모듈 범위에서 `export using` 또는 `export typedef`를 사용합니다. 두 형식 모두 `Alias = Type` 구문을 사용합니다. 일반 별칭은 해당 모듈에서만, 지역 별칭은 해당 범위에서만 보입니다. 가져오는 모듈은 네임스페이스로 한정한 공개 별칭을 사용할 수 있습니다. `export import`는 공개 별칭을 다시 내보내며 비공개 별칭은 공개하지 않습니다. `using namespace`는 해당 모듈과 범위의 이름 검색에만 영향을 주며 `export using namespace`는 거부됩니다.

다음 파일을 aliases.vs로 저장합니다.

```VeroScript
namespace Measures
{
	export using Count = int;
	export typedef Label = string;
	using InternalCount = int;
}
```

```VeroScript
import "aliases.vs";

int main()
{
	Measures::Count count = 42;
	Measures::Label label = "total";
	print(count);
	return 0;
}
```

사용하는 코드를 main.vs로 저장하고 `veroscript main.vs`를 실행하면 `42`가 출력됩니다. `export import "aliases.vs";`를 포함하는 파사드를 가져와도 됩니다. 바이너리 라이브러리를 배포하려면 `veroscript --emit-package aliases.vslib aliases.vs`를 실행한 다음 가져오기 대상을 `"aliases.vslib"`로 변경합니다.

## 기본 선언

생성자 또는 소멸자는 `= default`을 사용하여 컴파일러 생성 구현을 요청할 수 있습니다.

```VeroScript
struct Token
{
	int value = 7;

	Token() = default;
	~Token() = default;
};
```

기본 구성은 필드 기본값을 유형의 일반 기본 규칙과 결합합니다. `= default`을 작성하면 생성된 구현 의도가 명시적으로 나타나고 선언이 유형 인터페이스에 표시됩니다.

## 삭제된 선언

해당 호출 양식을 명시적으로 금지하면서 서명을 유지하려면 `= delete`을 사용하십시오.

```VeroScript
struct Token
{
	Token() = default;
	Token(string text) = delete;

	int read() const
	{
		return 7;
	}
	int read(string mode) const = delete;
};
```

삭제된 생성자, 메서드, 연산자 또는 함수를 호출하면 컴파일 진단이 생성됩니다. 삭제된 선언은 원치 않는 암시적 호출이나 잘못된 오버로드를 거부하는 데 유용합니다. 빈 런타임 함수를 생성하지 않습니다.

`= default` 및 `= delete`은 선언의 일부이며 함수 본문과 결합할 수 없습니다. 가상 기능과 인터페이스 슬롯은 여전히 상속 및 구현 규칙을 충족해야 합니다.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/declarations.md) · [fr-FR](../../fr-FR/language-basics/declarations.md) · [ja-JP](../../ja-JP/language-basics/declarations.md) · [ko-KR](declarations.md) · [zh-CN](../../zh-CN/language-basics/declarations.md) · [zh-TW](../../zh-TW/language-basics/declarations.md)
