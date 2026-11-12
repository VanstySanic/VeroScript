# C++ BindGen

`VeroScriptCppBindGen` は対応する C++ 宣言を VeroScript Binding Catalog に変換
します。catalog は compiler/tooling metadata であり、runtime 実装ではありません。
生成 callback stub は native thunk ではないため、実際の C++ 関数は host adapter
が登録します。

`GenerateBindings` は AST を、`GenerateBindingsFromSource`、`GenerateBindingsFromFile`、
`GenerateBindingsFromFiles` は source を受け取ります。`GenerateBindingProject` は
複数の入力を統合し、一つの `CatalogJson`、入力ごとの任意の `ApiReferences`、決定的な `ManifestJson`、診断、`Succeeded()` による成功状態を返します。
`FBindGenOptions::bEmitApiReference` で API リファレンスの生成を制御します。カタログ、リファレンス、マニフェストは一つのトランザクションとして確定します。入力ファイル名が衝突すると決定的なパスハッシュを付けるため、出力は走査順序に依存しません。`FBindGenProjectLayout` は統合カタログ、リファレンスディレクトリ、プロジェクトマニフェストの出力先を指定します。コンパイラーと IDE には統合カタログを渡してください。入力ごとのリファレンスは人が読む文書であり、実装としてコンパイルするものではありません。

project manifest の安定したフィールドは `version`、`kind`、`catalogRole`、
`catalog`、`inputs`、`apiReferences` です。`apiReferences` の各要素には `input` と
`path` が含まれます。merge 済み catalog は一度だけ出力され、reference は任意の
人間向けビューです。

project mode では、merge 済み catalog、入力ごとの API reference、manifest を
決定的な結果セットとして出力します。manifest の例：

```json
{
	"version": 1,
	"kind": "veroscript.cpp-binding-project",
	"catalogRole": "merged",
	"catalog": "bindings.json",
	"inputs": ["Include/Types.hpp"],
	"apiReferences": [
		{
			"input": "Include/Types.hpp",
			"path": "api/Types.vs.txt"
		}
	]
}
```

```powershell
VeroScriptCppBindGen Resources/Examples/BindingInput.hpp Resources/Examples/BindingExtras.hpp `
  --catalog build/BindingInput.bindings.json `
  --api-reference build/BindingInput.vs.txt
```

入力ごとの API reference と project manifest を生成する場合：

```powershell
VeroScriptCppBindGen Include/Types.hpp Include/Api.hpp `
  --catalog build/bindings.json `
  --api-reference-dir build/api `
  --manifest build/bindings.manifest.json
```

CLI では `--catalog <output.json>` が必須です。任意の `--api-reference <output.vs.txt>` は単一のリファレンス、`--api-reference-dir <directory>` は入力ごとのリファレンス、`--manifest <output.json>` はプロジェクトマニフェストを出力します。`--version` と `--help` も使用できます。二つのリファレンスオプションは併用できません。

修飾名、enum の identity と underlying type、member 種別、constructor、operator、
const method、`ref`/`const ref`、type alias、alias template を保持します。alias の
default は catalog schema に表現できないため拒否されます。type、enum、field、function、
parameter の C++ attribute は構造化 annotation として保持され、予約 namespace
`veroscript::`/`vero::` も扱えます。enum initializer は整数、portable な単一文字、
前の enumerator、括弧、単項、算術、シフト、bitwise、boolean、logical、relational、
equality 演算を検査付きで評価し、`int64` のオーバーフローを検出します。値を持たない
attribute は flag として、名前付き attribute の値は catalog に保持されます。raw pointer、return reference、
host-to-host inheritance、deleted member、reference field、free global、名前付き
method の overload、free function の overload、default argument、internal-linkage の
free function、基底型の範囲外の enum 値は拒否します。enum 初期化子は検査付きで評価
されます。
完全な JSON の項目と登録境界は[ホスト バインディング](bindings.md)を参照してください。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/bindgen.md) · [fr-FR](../fr-FR/bindgen.md) · [ja-JP](bindgen.md) · [ko-KR](../ko-KR/bindgen.md) · [zh-CN](../zh-CN/bindgen.md) · [zh-TW](../zh-TW/bindgen.md)
