# C# 프런트엔드

`VeroScriptCSharpFrontend`는 C# BindGen과 C# Transpiler가 공유하는 의미 분석 프런트엔드입니다.
내부적으로 Roslyn을 사용하지만 공개 모델은 VeroScript가 소유하며 Roslyn
객체를 공개 경계에 노출하지 않습니다.

여러 파일, namespace, class, struct, interface, record, enum, delegate,
필드, property, event, 생성자, 메서드, 연산자, attribute, XML 문서, generic
형태, 소스 위치, `ref`/`in`/`out` 메타데이터를 보존합니다. C# primitive,
1차원 배열, `List<T>`, `Dictionary<TKey,TValue>`,
`SortedDictionary<TKey,TValue>`, `HashSet<T>`, `Queue<T>`, `Stack<T>`, 선언된
enum과 알려진 host type을 매핑합니다.

nullable, pointer, delegate, `dynamic`, 해석되지 않은 형식과 열린 형식
매개변수는 의미를 잃지 않도록 명시적으로 실패합니다. 이 모듈은 build-time
선언 라이브러리이며 Catalog와 등록 코드는 C# BindGen이, C#에서 VeroScript로의 엄격한
변환과 source map은 `VeroScriptCSharpTranspiler`가 담당합니다.

## 표준 컨테이너의 제네릭 인수 수

<a id="container-mapping-arity"></a>

표준 컨테이너 매핑은 선언된 인수 수를 요구합니다. `List<T>`, `HashSet<T>`, `Queue<T>`, `Stack<T>`는 한 개, `Dictionary<TKey,TValue>`와 `SortedDictionary<TKey,TValue>`는 두 개입니다. 이름이 같아도 인수 수가 다른 사용자 형식을 잘못된 내장 컨테이너로 변환하지 않습니다. 명시적인 알려진 호스트 형식 매핑이 없으면 `VSCF2008`로 실패합니다.

## 선언 접근성

속성과 중첩 형식의 접근성은 각각 평가합니다. CLR의 암시적 기본 형식은 명시적으로 선언한 상속과 구분되므로, 비공개 접근자를 노출하거나 `System.ValueType`을 호스트 기본 클래스로 해석하면 안 됩니다.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/frontend.md) · [fr-FR](../fr-FR/frontend.md) · [ja-JP](../ja-JP/frontend.md) · [ko-KR](frontend.md) · [zh-CN](../zh-CN/frontend.md) · [zh-TW](../zh-TW/frontend.md)
