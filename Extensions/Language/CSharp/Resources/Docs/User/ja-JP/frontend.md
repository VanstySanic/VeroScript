# C# フロントエンド

`VeroScriptCSharpFrontend` は C# BindGen と C# Transpiler が共有する意味解析フロント
エンドです。内部では Roslyn を使いますが、公開モデルは VeroScript が
所有し、Roslyn の型を公開境界へ出しません。

複数ファイル、namespace、class、struct、interface、record、enum、delegate、
field、property、event、constructor、method、operator、attribute、XML
documentation、generic shape、source location、`ref`/`in`/`out` の情報を
保持します。primitive、1 次元配列、`List<T>`、`Dictionary<TKey,TValue>`、
`SortedDictionary<TKey,TValue>`、`HashSet<T>`、`Queue<T>`、`Stack<T>`、宣言
済み enum、既知の host type を型マッピングします。

nullable、pointer、delegate、`dynamic`、未解決型、open type parameter は
意味を失わせず明示的に失敗します。これは build-time の宣言ライブラリ
であり、Catalog と登録コードには C# BindGen を、C# から VeroScript への厳密な変換と
source map には `VeroScriptCSharpTranspiler` を使用してください。

## 標準コンテナーのジェネリック引数数

<a id="container-mapping-arity"></a>

標準コンテナーのマッピングでは宣言された引数数が必要です。`List<T>`、`HashSet<T>`、`Queue<T>`、`Stack<T>` は1個、`Dictionary<TKey,TValue>` と `SortedDictionary<TKey,TValue>` は2個です。同名でも引数数の異なる独自型を不正な組み込みコンテナーに変換しません。明示的な既知ホスト型マッピングがなければ `VSCF2008` で失敗します。

## 宣言のアクセシビリティ

プロパティと入れ子型のアクセシビリティは個別に評価します。CLR の暗黙の基底型は明示的な継承と区別されるため、非公開のアクセサーを公開したり、`System.ValueType` をホストの基底クラスと解釈したりしないでください。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/frontend.md) · [fr-FR](../fr-FR/frontend.md) · [ja-JP](frontend.md) · [ko-KR](../ko-KR/frontend.md) · [zh-CN](../zh-CN/frontend.md) · [zh-TW](../zh-TW/frontend.md)
