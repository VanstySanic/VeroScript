# 결정론적 난수 스트림

모듈 파일: `random.vs`

`std::random::make_engine(seed)`로 명시적인 독립 상태를 만들고 같은
`array<unsigned int>`를 이후 API에 전달합니다. 프로세스 전역 생성기가 없으므로
테스트를 재현할 수 있으며 0 seed는 고정된 0이 아닌 상태로 바뀝니다.

| API | 동작 |
| --- | --- |
| `std::random::make_engine(seed)` | 독립 상태를 만듭니다. |
| `std::random::seed(engine, seed)` | 상태를 재설정하고 빈 배열이면 요소를 만듭니다. |
| `std::random::next_uint(engine)` | 상태를 진행하고 부호 없는 정수를 반환합니다. |
| `std::random::next_bounded_uint(engine, upper_exclusive)` | `[0, upper_exclusive)` 값을 반환합니다. 상한은 양수여야 합니다. |
| `std::random::next_int(engine, minimum, maximum_exclusive)` | 반열린 정수 구간의 값을 반환합니다. 구간은 비어 있을 수 없습니다. |
| `std::random::next_double(engine)` | `[0, 1)` 값을 반환합니다. |
| `std::random::next_double_range(engine, minimum, maximum_exclusive)` | 지정한 반열린 부동소수점 구간의 값을 반환합니다. |
| `std::random::next_bool(engine)` | 의사 난수 Boolean을 반환합니다. |

bounded 연산은 modulo bias를 피하기 위해 rejection sampling을 사용합니다.
표본 추출은 xorshift32가 실제로 생성하는 `2^32-1`개의 0이 아닌 출력에 기반합니다. 상태 배열을 직접 수정하지 마세요. 상태가 0이면 `std::random::next_uint`가 오류를 내며 `std::random::seed`로 복구할 수 있습니다. 암호화 용도로는 적합하지 않습니다. `std::random::next_double_range`는 유한하고 엄격히 증가하는 양 끝값을 요구하며 반올림으로 배타적 상한에 도달하면 다시 추출합니다.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/standard-library/random.md) · [fr-FR](../../fr-FR/standard-library/random.md) · [ja-JP](../../ja-JP/standard-library/random.md) · [ko-KR](random.md) · [zh-CN](../../zh-CN/standard-library/random.md) · [zh-TW](../../zh-TW/standard-library/random.md)
