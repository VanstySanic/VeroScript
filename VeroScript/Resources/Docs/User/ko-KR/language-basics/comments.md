# 주석

주석은 소스 파일에 메모를 추가합니다. VeroScript는 한 줄 주석과 블록 주석을 지원합니다.

## 한 줄 주석

한 줄 주석은 `//`으로 시작하고 현재 줄의 끝에서 끝납니다.

```VeroScript
// Explain the purpose of the variable below.
int retry_count = 3;

int value = 1; // A comment may follow a statement.
```

## 블록 주석

블록 주석은 `/*`으로 시작하고 `*/`로 끝납니다. 여러 줄에 걸쳐 있을 수 있습니다.

```VeroScript
/*
	This note applies to the following code block.
*/
int result = 0;
```

주석 자체는 실행문이 아닙니다. 닫히지 않은 블록 주석은 어휘 오류입니다. 호스트는 전체 주석, 문서 주석만 또는 주석 없음 중에서 선택할 수 있으며 보존된 주석과 문서는 소스 메타데이터에서 조회할 수 있습니다. 활성 컴파일 확장은 이 정보를 코드 생성에 사용할 수 있습니다. 애너테이션 보존은 주석 보존과 독립적입니다.

## 편집기 도구용 소스 문서

Tooling hover는 소스 선언 바로 앞에 연속해서 놓인 주석을 읽습니다. 소스에서 정의한 전처리기 매크로의 completion item에도 앞선 주석이 포함될 수 있으며, 호스트 completion 문서는 binding metadata에서 가져옵니다. `//`(`///` 포함) 또는 `/* ... */` 블록을 사용하고 주석과 선언 사이에 빈 줄을 두지 마세요. 이는 Tooling 규약이며 컴파일러는 여전히 주석을 일반 어휘 주석으로 처리합니다.

`@param 이름 설명`은 매개변수를 설명하고, `@return 설명` 및 `@returns 설명`은 반환값을 설명합니다. Hover는 활성 언어 카탈로그의 레이블로 이 태그를 표시합니다. 알 수 없는 태그는 일반 주석 텍스트로 남으며 매크로 completion 문서는 주석을 작성된 그대로 유지합니다.

```VeroScript
// Adds two integers.
// @param left First addend.
// @param right Second addend.
// @return The sum.
int Add(int left, int right)
{
	return left + right;
}
```

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/comments.md) · [fr-FR](../../fr-FR/language-basics/comments.md) · [ja-JP](../../ja-JP/language-basics/comments.md) · [ko-KR](comments.md) · [zh-CN](../../zh-CN/language-basics/comments.md) · [zh-TW](../../zh-TW/language-basics/comments.md)
