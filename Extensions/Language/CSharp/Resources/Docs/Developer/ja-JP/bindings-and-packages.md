# C# binding、package、互換性

## ホストバインディングのモデル

managed definition は host function/type/member/operator、annotation、enum value、documentation、type parameter、protocol callback を表します。host type protocol には clone、equality、hash、subclass factory を含められます。generic host-type template は Interop callback contract で instantiate し、catalog の type identity を保持します。generated catalog は metadata であり、live managed object/function を接続する registration callback なしに C# method を呼び出しません。

## パッケージ

package build option は kind、name、version、host metadata、debug-info policy を含みます。package は byte から読み、metadata と canonical signed byte を公開し、明示的に dispose します。session が package と kind/entry shape を受け入れたときだけ compiled module を load します。

## API 互換性

checked API contract は public managed type、constructor、property、enum value、callback signature、target framework の compatibility boundary です。native layout change は `VeroScriptInterop.h` と共に review します。wrapper change は managed test、public API contract、package verification、native ABI consumer がすべて整合して初めて完了です。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/bindings-and-packages.md) · [fr-FR](../fr-FR/bindings-and-packages.md) · [ja-JP](bindings-and-packages.md) · [ko-KR](../ko-KR/bindings-and-packages.md) · [zh-CN](../zh-CN/bindings-and-packages.md) · [zh-TW](../zh-TW/bindings-and-packages.md)
