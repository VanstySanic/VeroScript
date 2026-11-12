# constexpr

`constexpr`는 컴파일 시점의 스칼라 값을 요구하며 `const`를 포함합니다. 지역, 모듈, 네임스페이스 변수와 명시적인 `static constexpr` 필드에 사용할 수 있습니다.

일반 함수, 네임스페이스 함수, 정적 메서드 및 지원되는 템플릿에 constexpr를 지정할 수 있습니다. 값으로 전달하는 매개변수와 void가 아닌 반환 값은 bool, 고정 폭 정수, char, 열거형이어야 합니다. 초기화된 스칼라 변수, 대입, 분기, 반복문, switch, 제한된 재귀를 지원합니다. 실행 시점 인수를 사용하는 일반 호출도 유지됩니다.

일반 if는 실행하지 않는 분기도 검사하며 if constexpr는 선택한 분기만 검사합니다. 네이티브 호출, 변경 가능한 전역 상태, 객체 생성, 참조, 부동소수점, 문자열, 컨테이너, 예외, 비동기 작업은 사용할 수 없습니다. 일반 매개변수로 명시적인 constexpr 지역 변수를 초기화할 수 없습니다.

상수 초기화, 값 템플릿 인수와 기본값, 고정 배열 길이, 정수／열거형 case와 열거 값은 같은 정확한 평가기를 사용합니다. 오버플로, 잘못된 변환, 0 나누기, 예산 초과는 오류입니다. 단락 평가는 선택하지 않은 분기의 산술을 실행하지 않습니다.

내보낸 선언은 일반 import를 사용합니다. 코어 컴파일 및 패키지 서비스를 통해 `.vslib`를 만들면 정의 환경과 비공개 도우미 의존성을 보존하여 원본 프로젝트 없이 사용할 수 있습니다. 로드할 때 컴파일 프로세서를 다시 실행하지 않습니다.

호스트는 `FCompileOptions::ConstantEvaluationLimits` 또는 직접 컴파일러의 `SetConstantEvaluationLimits`를 사용합니다. 기본값은 작업량 1,000,000, 호출 깊이 64, 식／네이티브 깊이 256, 임시 저장소 16 MiB, 캐시 8 MiB입니다. 제한은 양수이며 캐시 0은 비활성화입니다. 분석 취소와 기한은 유효하며 VM 실행 제한과 별개입니다.

리플렉션 설명자는 `is_constexpr()`, `std::compiler` 선언 뷰는 `is_constexpr`를 제공합니다. 조회는 상수 함수를 실행하지 않습니다. Core AOT는 같은 바이트코드를 사용하며 독립 소스 출력기는 검증된 형식 지정 리터럴과 기존 템플릿 제한을 유지합니다.

```VeroScript
constexpr int square(int value)
{
	return value * value;
}
constexpr int capacity = square(8);

int main()
{
	constexpr int count = capacity + 1;
	int input = 3;
	print(count);         // 65
	print(square(input)); // 9
	return 0;
}
```

[constexpr.vs](../../../../Scripts/examples/constexpr.vs)

독립 소스 출력은 기존 기능 제한을 유지합니다. C++에는 확정된 배열 길이를 전달합니다. C# 또는 Python에서 지원하지 않는 고정 배열과 인스턴스별 제네릭 상수에는 Core AOT를 사용합니다. 함수 기본 인수에는 기존 리터럴 및 템플릿 값 규칙이 적용됩니다.

## if constexpr 분기

조건은 bool, 정수, char 또는 비 scoped 열거형 상수여야 하며 scoped 열거형에는 명시적 변환이 필요합니다. constexpr 호출과 템플릿 값을 지원하지만 일반 런타임 매개변수는 사용할 수 없습니다. 템플릿 인스턴스마다 독립적으로 분기를 선택합니다. 두 분기 모두 구문이 올바라야 하지만 선택한 분기만 형식 검사, 추가 인스턴스화, 반환 경로 및 Lambda 캡처 분석에 참여합니다. 선택한 분기의 블록 범위를 유지합니다. 일반 함수에도 같은 규칙을 적용하며 일반 if는 양쪽을 검사합니다.

```VeroScript
template<int N>
int sum()
{
	if constexpr (N == 0)
		return 0;
	else
		return N + sum<N - 1>();
}
```

constexpr 함수에서도 사용할 수 있으나 일반 매개변수는 상수 호출 중에도 조건으로 사용할 수 없습니다. VM, JIT, Core AOT는 같은 선택 결과를 사용합니다. 인스턴스에 종속된 제네릭 소스 내보내기는 기존 규칙대로 Core AOT를 사용합니다.

지역 비 scoped 열거형의 한정되지 않은 멤버 이름도 상수 조건으로 사용할 수 있으며 일반적인 어휘 범위의 이름 가림 규칙을 따릅니다. 지연 평가되는 const 초기화식은 선언 당시 환경을 유지하므로 나중에 선언한 같은 이름의 영향을 받지 않습니다. Lambda는 실행 시 저장소를 캡처하지 않고 바깥 열거형 상수를 사용할 수 있으며, 템플릿의 지역 열거형은 구체적인 인스턴스마다 평가됩니다.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-reference/constexpr.md) · [fr-FR](../../fr-FR/language-reference/constexpr.md) · [ja-JP](../../ja-JP/language-reference/constexpr.md) · [ko-KR](constexpr.md) · [zh-CN](../../zh-CN/language-reference/constexpr.md) · [zh-TW](../../zh-TW/language-reference/constexpr.md)
