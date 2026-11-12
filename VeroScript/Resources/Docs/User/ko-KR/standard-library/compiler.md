# 스크립트 컴파일 확장

<a id="source-extension-activation"></a>

호스트나 앱이 제공하는 확장으로 일반 검사 전에 애너테이션을 처리하거나 선언을 생성할 수 있습니다. 진입 파일의 시작 부분에서 선택적 주석과 앞선 컴파일러 import 다음에 import compiler로 활성화합니다. 일반 import는 확장 등록을 실행하지 않습니다.

## 설명만 등록하는 확장

첫 파일은 notes.vs, 두 번째는 main.vs로 저장하세요. 이 확장은 @Note 설명만 등록하며 업무 동작을 생성하지 않습니다.

```VeroScript
import "compiler.vs";

export void register_extension(std::compiler::registration reg)
{
	std::compiler::annotation_definition note = std::compiler::annotation_definition();
	note.name = "@Note";
	std::compiler::configuration_value label = std::compiler::configuration("note.text");
	note.documentation = label.exists() ? label.value() : "A descriptive annotation.";
	note.targets = array<std::compiler::target>{std::compiler::target::class_type};
	reg.register_annotation(note);
}
```

```VeroScript
import compiler "notes.vs";

@Note class Message {};

int main()
{
	type<Message> info = type<Message>();
	print(info.annotation(0).definition().documentation());
	return 0;
}
```

```sh
veroscript --backend=vm --check main.vs
veroscript --backend=vm main.vs
```

프로그램은 A descriptive annotation.을 출력합니다. Runtime 표준 라이브러리에 compiler.vs가 필요합니다. core.vs는 이 컴파일 전용 진입점을 재내보내지 않습니다.

## 패키지와 제한

compiler-package는 등록을 실행하지 않고 .vslib 확장을 만듭니다. 같은 업무 프로그램에서 import compiler를 뺀 consumer.vs를 저장하고 notes.vslib를 명시적으로 선택하세요. 소스와 패키지 복사본을 함께 활성화하지 마세요.

```sh
veroscript compiler-package notes.vs notes.vslib
veroscript --backend=vm --compiler-extension notes.vslib consumer.vs
```

[명령줄 도구 — 스크립트 컴파일 확장](../../../../../../VeroScriptCli/Resources/Docs/User/ko-KR/index.md#cli-compiler-options)을 참조하세요.

```sh
veroscript --backend=vm --compiler-extension notes.vslib --compiler-config "note.text=Configured annotation." consumer.vs
```

```text
Configured annotation.
```

[애너테이션](../language-basics/annotations.md)

확장 전체의 기본 예산은 VM 명령 1,000,000개와 2,000ms입니다. 0은 해당 예산을 비활성화하며 차단된 네이티브 콜백을 강제로 중단하지는 못합니다.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md) · [en-US](../../en-US/standard-library/compiler.md) · [fr-FR](../../fr-FR/standard-library/compiler.md) · [ja-JP](../../ja-JP/standard-library/compiler.md) · [ko-KR](compiler.md) · [zh-CN](../../zh-CN/standard-library/compiler.md) · [zh-TW](../../zh-TW/standard-library/compiler.md)
