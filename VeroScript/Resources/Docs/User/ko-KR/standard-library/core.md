# 핵심 엔트리 모듈

모듈 파일: `core.vs`

`core.vs`은 집계 표준 라이브러리 진입점입니다. `export import`을 사용하여 다음 모듈을 다시 내보냅니다.

- `i18n.vs`
- `memory.vs`
- `math.vs`
- `algorithm.vs`
- `numeric.vs`
- `random.vs`
- `io.vs`
- `string.vs`
- `path.vs`
- `concurrent.vs`
- `attribute.vs`
- `callable.vs`
- `delegate.vs`

다음을 사용하여 위의 런타임 모듈을 함께 가져옵니다.

```VeroScript
import "core.vs";
```

종속성 및 이름 범위를 명시적으로 만들기 위해 개별 모듈을 직접 가져올 수도 있습니다. `core.vs`은 별도의 API를 도입하지 않습니다. 이는 다른 표준 라이브러리 모듈의 공개 선언을 노출합니다.

서식 지정, 파일, 동시성 및 이벤트 작업의 서명은 해당 모듈 페이지를 참조하세요.

- [응용 프로그램 국제화 — std::i18n](i18n.md): `std::i18n`

컴파일 확장에서는 `compiler.vs`를 명시적으로 import하세요. `core.vs`는 이를 재내보내지 않으며 일반 실행 코드에서 import해도 컴파일 권한이 생기지 않습니다.

[컴파일 확장](compiler.md)

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/standard-library/core.md) · [fr-FR](../../fr-FR/standard-library/core.md) · [ja-JP](../../ja-JP/standard-library/core.md) · [ko-KR](core.md) · [zh-CN](../../zh-CN/standard-library/core.md) · [zh-TW](../../zh-TW/standard-library/core.md)
