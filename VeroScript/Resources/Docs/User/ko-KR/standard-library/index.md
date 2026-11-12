# 표준 라이브러리 참조

VeroScript 표준 라이브러리는 일반 문자열, 수학, 파일, 경로, 동시성 및 이벤트 작업을 위한 모듈을 제공합니다. 스크립트는 런타임 모듈을 한 번에 사용하도록 `core.vs`를 가져오거나 필요한 모듈만 가져올 수 있습니다.

모듈 소유 함수는 `std::strings::join` 또는 `std::concurrent::spawn`처럼 전체 공개 네임스페이스로 호출하세요. 한정되지 않은 전역 이름은 거부됩니다. `array<T>` 및 `print` 같은 기본 타입과 직접 제공 함수는 정식 이름도 사용할 수 있습니다. 네임스페이스 별칭은 필요한 모듈 가져오기를 대체하지 않습니다. [std](../language-basics/standard-namespace.md).

- [관리 메모리](memory.md)：`std::memory`

- [속성과 메서드 함수 값](attribute.md)：`std::attribute<T>` · `std::bind_method<F>`

## 모듈 인덱스

- [내장 함수](builtins.md): 출력, 변환, 수치 및 환경 런타임 함수. 입력 래퍼에는 io.vs가 필요합니다.
- [핵심 진입점](core.md): 참조 페이지에 나열된 런타임 모듈을 다시 내보냅니다. compiler.vs는 명시적으로 가져와야 합니다.
- [문자열](string.md): 검색, 슬라이싱, 분할, 결합, 자르기, 대소문자 변환, 교체 및 서식 지정.
- [수학](math.md): 삼각법, 제곱근, 반올림 및 클램핑.
- [알고리즘](algorithm.md): 배열 정렬, 뒤집기, 검색 및 개수 계산.
- [수치 집계](numeric.md): 누적, 합계 및 평균.
- [난수](random.md): 명시적 상태를 사용하는 결정론적 의사 난수 스트림.
- [I/O](io.md): 파일, 디렉터리, 표준 입력 및 작업 디렉터리.
- [경로](path.md): 파일 이름, 디렉터리 이름, 확장자 및 경로 결합.
- [동시성](concurrent.md): 채널, 격리된 작업, 작업 대기 및 협력 취소.
- [이벤트](events.md): 유니캐스트 및 멀티캐스트 대리자.

## 가져오기 규칙

전체 라이브러리를 가져옵니다.

```VeroScript
import "core.vs";
```

또는 개별 모듈을 가져옵니다.

```VeroScript
import "string.vs";
import "path.vs";
```

표준 라이브러리 함수는 일반적으로 내보내진 선언입니다. 해당 매개변수와 반환 유형은 해당 모듈 구현에 의해 정의됩니다. 파일, 스레드 또는 호스트 환경과 관련된 작업은 런타임 권한 및 호스트 구성에 따라 달라질 수도 있습니다.

- [응용 프로그램 국제화 — std::i18n](i18n.md): `std::i18n`

컴파일 확장에서는 `compiler.vs`를 명시적으로 import하세요. `core.vs`는 이를 재내보내지 않으며 일반 실행 코드에서 import해도 컴파일 권한이 생기지 않습니다.

[컴파일 확장](compiler.md)


- [표준 라이브러리 기반](foundation.md)

- [variant — 동적 값 상자](variant.md)
<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/standard-library/index.md) · [fr-FR](../../fr-FR/standard-library/index.md) · [ja-JP](../../ja-JP/standard-library/index.md) · [ko-KR](index.md) · [zh-CN](../../zh-CN/standard-library/index.md) · [zh-TW](../../zh-TW/standard-library/index.md)
