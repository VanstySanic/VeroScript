# 모듈

모듈은 소스 파일 경계를 중심으로 선언을 구성하고 가시성을 제어합니다. 모듈은 일반적으로 `.vs` 파일로 표시되고 `import`로 가져오고 `export`를 통해 다른 모듈에서 사용할 수 있습니다.

## 모듈 가져오기

`import`을 사용하여 소스 모듈 또는 라이브러리 패키지를 가져옵니다.

```VeroScript
import "string.vs";

int main()
{
	print(std::strings::join(std::strings::split("a,b,c", ","), "-"));
}
```

모듈을 두 번 이상 가져오면 모듈 ID가 중복 로드를 억제합니다. 모듈을 가져오더라도 모든 비공개 선언이 노출되지는 않습니다. 내보낸 선언만 외부 모듈에서 사용할 수 있습니다.

## 선언 내보내기

모듈을 가져올 때 사용할 수 있도록 선언 앞에 `export`을 배치합니다.

```VeroScript
export struct Box<T>
{
	T value;

	Box(T initial)
	{
		value = initial;
	}

	T get() const
	{
		return value;
	}
};

int hidden_value()
{
	return 99;
}
```

`Box`은 모듈 공개 API의 일부로 사용할 수 있는 반면, `hidden_value`은 내보내지 않기 때문에 비공개입니다. 함수, 변수, 구조체, 클래스, 인터페이스, 열거형 및 일반 선언을 내보낼 수 있습니다.

## 모듈 다시 내보내기

`export import`은 모듈을 가져오고 해당 모듈의 내보낸 선언을 현재 모듈을 통해 사용할 수 있도록 만듭니다.

```VeroScript
export import "string.vs";
```

다른 모듈에서는 Facade를 가져오고 다시 내보낸 선언을 사용할 수 있습니다.

```VeroScript
import "string_facade.vs";

string text = std::strings::join(std::strings::split("a,b", ","), "-");
```

다시 내보내도 비공개 선언이 공개 선언으로 바뀌지 않으며 소스 텍스트도 복사되지 않습니다.

## 모듈 및 네임스페이스

모듈은 소스 파일 종속성과 가시성을 처리합니다. 네임스페이스는 모듈 내의 이름을 구성합니다. 모듈은 여러 네임스페이스를 선언할 수 있으며 다른 모듈의 네임스페이스에서 공개 선언을 가져올 수 있습니다.

## 라이브러리 패키지 가져오기

소스 모듈과 바이너리 라이브러리 패키지는 동일한 `import` 구문을 사용합니다.

```VeroScript
import "DemoLibrary.vslib";
```

라이브러리 패키지는 빌드 시 내보낸 선언만 노출할 수 있습니다. 패키지 생성, 확인, 배포는 [패키지 및 배포](../../../../../../VeroScriptCli/Resources/Docs/User/ko-KR/packages.md)를 참조하세요.

## 공개 매크로

[전처리기 — 공개 매크로](preprocessor.md#exported-macro-contract)을 참조하세요.

중첩 형식, 지역 선언, 접근 규칙 및 단계별 제네릭 인수는 [형식 범위](scope.md)를 참조하세요.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/modules.md) · [fr-FR](../../fr-FR/language-basics/modules.md) · [ja-JP](../../ja-JP/language-basics/modules.md) · [ko-KR](modules.md) · [zh-CN](../../zh-CN/language-basics/modules.md) · [zh-TW](../../zh-TW/language-basics/modules.md)
