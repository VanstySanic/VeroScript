# 네임스페이스

네임스페이스는 이름을 구성하고 스크립트의 개별 영역 간의 충돌을 방지합니다. 네임스페이스에는 변수, 함수, 구조체, 클래스, 인터페이스, 열거형 및 기타 선언이 포함될 수 있습니다.

## 이름 선언 및 한정

`namespace`을 사용하여 네임스페이스를 선언하고 `::`을 사용하여 해당 멤버에 액세스합니다.

```VeroScript
namespace Util
{
	int Seed = 10;

	int next_seed()
	{
		Seed = Seed + 1;
		return Seed;
	}
}

int current = Util::Seed;
int next = Util::next_seed();
```

정규화된 이름은 소유하는 네임스페이스를 명시적으로 만듭니다. 유형 멤버, 열거형 멤버 및 정적 멤버에도 `::`을 선호합니다.

```VeroScript
enum class State
{
	Ready,
	Running
};

State state = State::Running;
```

## 전역 한정

앞에 `::`를 붙이면 전역/루트 범위에서 이름 검색을 시작합니다.

```VeroScript
int GlobalValue = 3;

namespace Feature
{
	int GlobalValue = 9;

	int read_root_value()
	{
		return ::GlobalValue;
	}
}
```

`::GlobalValue`는 현재 네임스페이스, 로컬 별칭 및 `using namespace` 대체 검색을 건너뜁니다. 루트 타입이나 네임스페이스에도 사용할 수 있으며 `using namespace ::Name`도 유효합니다.

## 익명 네임스페이스

`namespace { ... }`는 익명 네임스페이스를 선언합니다. VeroScript에서는 투명한 선언 그룹으로 처리되므로 합성 네임스페이스를 만들지 않으며 C++와 같은 번역 단위 내부 링크도 제공하지 않습니다. 멤버는 바깥쪽 네임스페이스에 그대로 속합니다.

전역 범위에서는 익명 네임스페이스의 멤버를 추가 한정 없이 사용할 수 있습니다.

```VeroScript
namespace
{
	int transparent_helper()
	{
		return 40;
	}
}

int main()
{
	print(transparent_helper() + 2);
	return 0;
}
```

이름이 있는 네임스페이스 안에서도 같은 규칙이 적용됩니다. 멤버는 바깥쪽 네임스페이스의 한정을 유지합니다.

```VeroScript
namespace Outer
{
	namespace
	{
		int nested_helper()
		{
			return 9;
		}
	}
}

int main()
{
	print(Outer::nested_helper());
	return 0;
}
```

익명 네임스페이스는 내보내기 또는 모듈 가시성을 변경하지 않습니다. 다른 모듈에서 사용할 선언에는 개별적으로 `export`를 지정하십시오. 소스 구성을 개선하기 위해 투명한 그룹이 필요할 때 익명 네임스페이스를 사용하십시오.

## 표준 네임스페이스

내장 컨테이너, 유틸리티 값 형식 및 공개 런타임 함수에는 예약된 `std` 네임스페이스에서 권장되는 표기가 있습니다.

```VeroScript
import "string.vs";

std::array<int> values = {1, 2, 3};
std::optional<int> selected = std::optional<int>(values[0]);
std::print(std::format("selected: {}", selected.get()));
```

`array`, `optional`, `print` 및 `format`과 같은 기존 전역 표기는 호환성 별칭으로 유지됩니다. `std::`는 컴파일 타임에 확인되며 VM 또는 JIT 디스패치를 추가하지 않습니다. `std::sqrt`와 같은 소스 라이브러리 함수에는 여전히 일반적인 모듈 가져오기가 필요하며, 이름 한정은 `import`/`export`를 우회하지 않습니다.
전역 `std` 네임스페이스는 예약되어 있으며 스크립트로 확장할 수 없습니다.

네임스페이스는 이름을 구성하지만 내보내기 상태를 자동으로 변경하지는 않습니다. 선언이 모듈 외부에 표시되는지 여부는 여전히 모듈의 `export` 규칙에 의해 제어됩니다.

전체 유형 및 함수 별칭 표를 보려면 [`std` 네임스페이스](standard-namespace.md)를 참조하세요.

## 네임스페이스 사용

`using namespace`을 사용하면 포함 범위에서 네임스페이스의 직접 멤버를 사용할 수 있습니다.

```VeroScript
namespace MathTools
{
	int base = 2;
}

using namespace MathTools;

int value = base + 3;
```

지시어는 포함 범위와 그 뒤에 오는 코드에 영향을 미칩니다. 이름을 다시 내보내거나 형제 네임스페이스에 영향을 주지 않습니다. 이름이 충돌할 수 있는 경우 전체 자격을 유지하십시오.

```VeroScript
int value = MathTools::base + 3;
```

안정적인 이름 경계에는 네임스페이스를 사용합니다. 다른 소스 파일에서 선언을 가져오는 것이 목표인 경우 `import` 모듈을 사용하십시오.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/namespaces.md) · [fr-FR](../../fr-FR/language-basics/namespaces.md) · [ja-JP](../../ja-JP/language-basics/namespaces.md) · [ko-KR](namespaces.md) · [zh-CN](../../zh-CN/language-basics/namespaces.md) · [zh-TW](../../zh-TW/language-basics/namespaces.md)
