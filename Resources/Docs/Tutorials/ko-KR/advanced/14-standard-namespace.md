# 14: `std` 표준 네임스페이스를 사용하세요.

이 튜토리얼에서는 정규화되지 않은 모듈 선언을 사용하는 작은 스크립트를 권장되는 공개 `std` 철자로 리팩터링합니다. `std::`은 표준 이름에 대한 소스 수준 별칭입니다. 두 번째 컨테이너 유형을 생성하거나 런타임 동작을 변경하지 않습니다.

## 1단계: 점수 스크립트 준비

`scores.vs`을 생성합니다:

```VeroScript
import "math.vs";
import "string.vs";

int main()
{
	array<int> scores = {81, 92, 88};
	optional<int> selected = optional<int>(scores[1]);
	double root = std::sqrt(81.0);

	print(format("selected={}, root={}", selected.get(), root));
	return 0;
}
```

먼저 확인하고 실행하십시오.

```powershell
veroscript --check scores.vs
veroscript scores.vs
```

첫 번째 버전은 모듈 소유 수학 함수를 이미 정규화합니다. 모듈 소유 표준 라이브러리 함수는 공개 `std` 네임스페이스를 통해서만 사용할 수 있으며, 작은 핵심 내장 API만 전역으로 유지됩니다.

## 2단계: `std::`으로 마이그레이션

알고리즘이 아닌 이름 제한만 변경합니다.

```VeroScript
import "math.vs";
import "string.vs";

int main()
{
	std::array<int> scores = {81, 92, 88};
	std::optional<int> selected = std::optional<int>(scores[1]);
	double root = std::sqrt(81.0);

	std::print(std::format("selected={}, root={}", selected.get(), root));
	std::print(std::strings::contains("scores", "core"));
	return 0;
}
```

동일한 확인 및 실행 명령을 다시 실행하십시오. `std::array<int>`과 `array<int>`은 동일한 언어 유형을 참조하고, `std::print`와 `print`은 동일한 런타임 함수를 참조합니다. `std::`는 컴파일 중에 확인되며 런타임 디스패치를 추가하지 않습니다.

수학, 입출력, 파일 시스템, 문자열, 경로, 동시성, 시간, 스레드, 환경 및 형식화된 입력 진입점은 네임스페이스 전용입니다. 정규화되지 않은 표준 라이브러리 이름과 `std::now` 같은 단순 호환 이름은 제공되지 않습니다. 사용자 코드는 동일한 정규화되지 않은 이름으로 관련 없는 함수를 계속 정의할 수 있습니다.

## 3단계: 가져오기 경계 이해

표준 네임스페이스 별칭은 모듈 가져오기가 아닙니다. 소스 표준 라이브러리 함수에는 여전히 이를 제공하는 모듈이 필요합니다.

- `math.vs`은 `sqrt`, `sin`, `cos`, `tan`, `floor`, `ceil`, `round`, `trunc`, `pow`, `exp`, `log`, `log2`, `asin`, `acos`, `atan2`, `isfinite`, `isnan`, `pi`, `e`, `clamp` 및 `clamp_int`을 제공합니다.
- `string.vs`는 `std::strings`을 통해 `last_find`, `repeat`, `remove_prefix`, `remove_suffix`를 포함한 문자열 쿼리를 제공합니다.
- `io.vs`은 `std::io`를 통해 스트림 작업을 제공하고 `std::filesystem`을 통해 파일 시스템 작업을 제공합니다.
- `path.vs`는 `std::filesystem`에 경로 작업을 추가합니다. `concurrent.vs`은 `std::concurrent`을 사용합니다.

런타임 콘솔 함수는 모듈을 가져올 필요가 없습니다. 줄바꿈 없는 출력에는
`std::console::write`와 `std::console::error`, 대기 중인 출력을 공개하려면
`std::console::flush()`, 대화형 터미널 감지에는 `std::console::is_interactive()`를 사용합니다.

예를 들어 `import "math.vs";`을 제거해도 `std::sqrt`이 수학 모듈을 자동으로 로드하지 않습니다. 먼저 모듈을 가져온 다음 정규화된 이름을 사용하여 종속성을 명시적으로 유지하세요.

## 4단계: 컨테이너 및 값 유형 마이그레이션

표준 네임스페이스에는 맵, 세트, 큐, 채널, 작업 및 리플렉션 정보 유형도 포함됩니다.

```VeroScript
int main()
{
	std::map<string, int> totals = {"math": 2, "io": 1};

	std::print("math={}, io={}", totals["math"], totals["io"]);
	return 0;
}
```

새 코드에서는 그룹화된 `std` 철자를 일관되게 사용합니다. 구현 선언은 모듈 내에서 정규화되지 않은 상태로 유지되므로 네임스페이스 계층은 모든 백엔드를 동일한 기호 ID에 매핑할 수 있습니다.

## 5단계: 값 보호 및 변환을 명시적으로 유지

현재 예제는 `std::readonly<T>` 별칭과 전역 변환 내장 함수도 함께 사용합니다. `std::`를 모든 내장 함수 앞에 붙이지 않으므로 `cast<T>`, `try_cast<T>` 및 `const_cast<T>`는 정규화하지 않고 유지하세요.

```VeroScript
std::readonly<int> published_score = std::readonly<int>(selected.get());
double exact_score = cast<double>(scores[1]);
std::optional<int> narrowed = try_cast<int>(exact_score);

const std::array<int> explicitly_unlocked = {13};
// 기반 저장소가 쓰기 가능하다는 것을 알고 있을 때만 이 const 해제를 사용합니다.
const_cast<std::array<int>>(explicitly_unlocked).push(published_score.get());
```

`cast<T>`는 변환을 요구하고, `try_cast<T>`는 호출자가 검사할 수 있는 `optional<T>`을 반환합니다. `const_cast<T>`는 값을 변환하거나 런타임 유형 검사를 수행하지 않고 컴파일 시 const 액세스만 제거하므로 의도적이고 제어된 경우에만 사용해야 합니다. 전체 규칙은 [형식 변환](../../../../../VeroScript/Resources/Docs/User/ko-KR/language-reference/type-conversions.md)을 참조하세요.

## 루트 한정 이름

앞의 `::`는 전역/루트 범위에서 검색을 시작합니다. 현재 네임스페이스, 로컬 별칭 및 `using namespace` 대체 검색을 건너뜁니다.

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

루트 선언을 의도할 때 `::Name`을 사용하세요. `std::readonly<T>`는 `readonly<T>` 래퍼의 표준 네임스페이스 표기이며 별도의 런타임 유형이 아닙니다.


## 연습

1. 예제를 `std::unordered_map`으로 바꾸지 마십시오. 전체 별칭 표를 참조하고 대신 어떤 지원 컨테이너가 적합한지 설명하세요.
2. `std::set<int>`을 추가하고 이를 사용하여 중복 점수를 제거합니다.
3. `string.vs`를 가져오고 `std::strings::contains` 또는 `std::strings::starts_with`를 호출합니다.

전체 [`std` 표준 네임스페이스](../../../../../VeroScript/Resources/Docs/User/ko-KR/language-basics/standard-namespace.md) 표를 참조하세요. 실행 가능한 예는 [`examples/standard_namespace.vs`](../../../../../VeroScript/Resources/Scripts/examples/standard_namespace.vs)입니다.

<!-- docs-navigation -->

---

[튜토리얼](../index.md)

언어: [en-US](../../en-US/advanced/14-standard-namespace.md) · [fr-FR](../../fr-FR/advanced/14-standard-namespace.md) · [ja-JP](../../ja-JP/advanced/14-standard-namespace.md) · [ko-KR](14-standard-namespace.md) · [zh-CN](../../zh-CN/advanced/14-standard-namespace.md) · [zh-TW](../../zh-TW/advanced/14-standard-namespace.md)
