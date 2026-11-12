# C# binding, package, 호환성

## 호스트 바인딩 모델

managed definition은 host function/type/member/operator, annotation, enum value, documentation, type parameter, protocol callback을 나타냅니다. host type protocol은 clone, equality, hashing, subclass factory를 포함할 수 있습니다. generic host-type template은 Interop callback contract로 instantiate하고 catalog의 type identity를 보존합니다. generated catalog는 metadata이며 registration callback만 live managed object/function을 연결하므로 catalog만으로 C# method를 invoke하지 않습니다.

## 패키지

package build option은 package kind/name/version, host metadata, debug-info policy를 포함합니다. package는 byte에서 읽어 metadata와 canonical signed byte를 공개하고 explicit dispose합니다. session이 package와 kind/entry shape를 수락할 때만 compiled module을 load합니다.

## API 호환성

checked API contract는 public managed type, constructor, property, enum value, callback signature, target framework의 compatibility boundary입니다. native layout 변경은 `VeroScriptInterop.h`와 함께 review합니다. wrapper 변경은 managed test, public API contract, package verification, native ABI consumer가 모두 일치해야 완료됩니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/bindings-and-packages.md) · [fr-FR](../fr-FR/bindings-and-packages.md) · [ja-JP](../ja-JP/bindings-and-packages.md) · [ko-KR](bindings-and-packages.md) · [zh-CN](../zh-CN/bindings-and-packages.md) · [zh-TW](../zh-TW/bindings-and-packages.md)
