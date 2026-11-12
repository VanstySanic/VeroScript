# 22: 애플리케이션 문구와 스냅샷

i18n.vs와 VM을 사용합니다. 앱 언어는 컴파일러 진단 언어와 독립이며 리터럴과 이름 있는 치환에는 외부 리소스 공급자가 필요 없습니다.

## 1. 저장과 실행

application-text.vs로 저장하고 veroscript --backend=vm application-text.vs를 실행하세요. 출력은 다음과 같습니다.

```VeroScript
import "i18n.vs";

void main()
{
	std::i18n::context app = std::i18n::context();
	std::i18n::arguments args = std::i18n::arguments();
	args.set("name", std::i18n::argument("Ada"));
	args.set("count", std::i18n::argument(18446744073709551615u64));
	auto captured = app.snapshot();
	print(captured.format("Hello {name}: {count}", args));
	print(captured.resolve(std::i18n::text("{literal}")));
	app.set_language("fr-FR");
	assert(app.version() != captured.version());
}
```

```text
Hello Ada: 18446744073709551615
{literal}
```

## 2. 관찰과 연습

uint64가 정확히 출력되고 리터럴 중괄호도 보존됩니다. app.language 변경은 버전을 바꾸지만 캡처한 스냅샷은 바꾸지 않습니다. 호스트가 리졸버를 제공한 뒤에 리소스 키를 사용하세요. 언어를 선택하는 것만으로 하드코딩된 문자열이 번역되지는 않습니다.

[참조](../../../../../VeroScript/Resources/Docs/User/ko-KR/standard-library/i18n.md)

<!-- docs-navigation -->

---

[튜토리얼](../index.md)

언어: [en-US](../../en-US/standard-library/22-application-i18n.md) · [fr-FR](../../fr-FR/standard-library/22-application-i18n.md) · [ja-JP](../../ja-JP/standard-library/22-application-i18n.md) · [ko-KR](22-application-i18n.md) · [zh-CN](../../zh-CN/standard-library/22-application-i18n.md) · [zh-TW](../../zh-TW/standard-library/22-application-i18n.md)
