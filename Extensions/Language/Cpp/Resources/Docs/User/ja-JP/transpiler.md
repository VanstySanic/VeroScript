# C++ Transpiler

`VeroScriptCppTranspiler` は意味を保持できる C++17 サブセットを VeroScript
source に変換します。[C++ フロントエンド](frontend.md) の AST と type mapping
を共有しますが、BindGen と Interop ABI には依存しません。

```powershell
VeroScriptCppTranspiler Resources/Examples/Basic.cpp `
  -o build/Basic.vs `
  --source-map build/Basic.vs.map.json
```

生成 source と任意の source map を出力します。デフォルトでは VeroScript compiler
で検証し、解析、lowering、検証が成功した後にだけ書き込みます。project mode は
全入力を先に解析・merge し、入力ごとの `.vs` と map、決定的な manifest を生成します。
manifest は生成 module を implicit import として示し、project-wide access を有効にします。

project manifest の安定したフィールドは `version`、`kind`、
`allowProjectWideAccess`、`modulePaths`、`implicitImportPaths`、`files` です。
`files` の各要素には `input`、`source`、`sourceMap` が含まれます。manifest、生成
source、source map は同じ transpilation 実行で作られたものを組み合わせ、生成ファイルの
パスは project-relative として扱います。

project mode の例：

```powershell
VeroScriptCppTranspiler Source/Types.cpp Source/Game.cpp `
  --output-dir build/generated `
  --manifest build/generated/transpile.manifest.json
```

manifest の構造例：

```json
{
	"version": 1,
	"kind": "veroscript.transpiled-project",
	"allowProjectWideAccess": true,
	"modulePaths": ["Types.vs"],
	"implicitImportPaths": ["Types.vs"],
	"files": [
		{
			"input": "Source/Types.cpp",
			"source": "Types.vs",
			"sourceMap": "Types.vs.map.json"
		}
	]
}
```

CLI は繰り返し指定できる `--attribute-map cpp=vero` を受け付けます。
`veroscript::` と `vero::` の attribute は直接変換され、その他の namespace は明示的な
mapping が必要です。生成 source はデフォルトで検証されます。別の検証段階が責任を
持つ場合だけ `--no-validate` を使用してください。

CLI options は単一出力の `-o`/`--output`、source map の `--source-map`、project
mode の `--output-dir`、manifest の `--manifest`、繰り返し指定できる
`--attribute-map`、`--no-validate`、`--version`、`--help` です。project mode では入力
ごとに source と map を生成します。

C++ API の `FTranspileOptions` では、検証、source map と診断に記録する生成
source path、追加の既知 host/enum 型、既知の型 alias、明示的な attribute mapping
を制御します。project 用 options では、検証と attribute mapping が project 出力に
適用されます。

値ベースのコード、namespace、enum、class/struct、constructor、method、global、
container、`this->member`、scalar `static_cast`、`typedef`/`using` alias、通常および
`u8` raw string、typed `throw`、`try`/`catch`、catch-all、rethrow をサポートします。
alias template は構造化された宣言型で展開されます。attribute は VeroScript annotation
に変換され、parameter attribute は BindGen に保持されます。クラス外定義と static
data 定義は project mode で照合されます。

class、struct、free function の template は `typename`/`class` パラメーターだけを持つ
場合、末尾の既定型を含めて VeroScript の open generic に変換されます。

所有権操作、任意の pointer、RTTI、coroutine、高度な C++ template 機能、shared ownership、
未解決 `extern`、allocation、reference field/global、表現できない
`constexpr`/`consteval`/`constinit`/`noexcept`、`goto` および未対応の attribute/friend
形式は拒否します。

非型パラメーター、pack、specialization、SFINAE、concept、依存する compile-time
expression、method-local template、明示的な function-template argument call は対象外です。
opaque な関数本体、friend token sequence、initializer 内で参照される generic alias も
拒否されます。

通常のメソッド宣言は型の宣言モジュールに、本体は実装モジュールに保持します。
コンストラクター、デストラクター、静的データには core の同一ファイル制約が適用されます。
既定の検証は manifest の可視性設定で最終モジュールをコンパイルし、エラーを元の
C++ ファイルに対応付けます。名前検索のスコープを保持できない統合は拒否します。
クラス template のクラス外定義は未対応で、明示的な診断を返します。

プロジェクトの統合はリンカー処理ではありません。名前空間の修飾定義はその名前空間の宣言として扱います。内部リンケージの関数や変数が衝突すると `cpp.merge.internal_linkage_collision` を返し、名前の変更や入力順による定義の選択は行いません。生成ソースとソースマップが食い違わないよう、複数の出力をまとめて確定します。

## 数値型と整数リテラル

ローカル変数と `static_cast` の対象型には、宣言シグネチャと同じプラットフォーム依存の型変換を適用します。整数リテラルの型はツールをビルドしたプラットフォームの C++17 データモデルで選択し、スクリプトの接尾辞に変換します。8 進数、2 進数、桁区切りの値を保持します。16 進浮動小数点と `long double` のリテラルは明示的に拒否します。数値変換には対象型の範囲内の値が必要で、範囲外ではスクリプトのエラーになります。C++ の縮小変換による切り捨てや符号付き・符号なしの混在昇格に依存するコードは明示的な書き換えが必要です。

`std::pair<T,U>` は soft builtin の `pair<T,U>` に対応します。同じ名前と型引数数の可視ユーザーテンプレートがその型を捕捉する場合、プロジェクトモードや生成ソース検証が無効でも `cpp.transpile.standard_pair_shadowed` で拒否します。ユーザーテンプレートを改名するか明示アダプターを使ってください。型引数数が異なる場合や無関係な名前空間では共存できます。

VeroScript のテンプレートパラメーターは、グローバルな型や通常の別名と同名にできません。
該当する C++ 宣言は、プロジェクトモードや生成ソース検証を無効にした場合も、
出力前に `cpp.transpile.template_parameter_shadow_unsupported` で拒否されます。
転換前に C++ のテンプレートパラメーター名を変更してください。
別名テンプレートは構造的に解決され、そのパラメーターの束縛を保持します。

構造化された宣言では、テンプレート引数を含むユーザー型名を絶対修飾し、出力先の名前空間でも型の同一性を保持します。
基底型は元の検索スコープを保持します。対象の継承構文では、内側の同名型に隠されたグローバル基底型を表現できません。
この場合、生成ソース検証を無効にしても、出力前に `cpp.transpile.global_base_shadowed` で拒否されます。
組み込みコンテナーや型パラメーター自体を基底にする宣言は `cpp.transpile.base_type_unsupported` で拒否されます。継承には既知のクラスまたは構造体が必要です。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/transpiler.md) · [fr-FR](../fr-FR/transpiler.md) · [ja-JP](transpiler.md) · [ko-KR](../ko-KR/transpiler.md) · [zh-CN](../zh-CN/transpiler.md) · [zh-TW](../zh-TW/transpiler.md)
