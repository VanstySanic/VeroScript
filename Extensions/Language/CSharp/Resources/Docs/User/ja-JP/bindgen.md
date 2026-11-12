# C# BindGen

`VeroScriptCSharpBindGen` は C# の意味解析モデルから Binding Catalog v1 メタデータを生成します。必要に応じて `VeroScriptCSharp` 用のリフレクション不要の登録コード、API リファレンス、生成 C# プログラム用 AOT ブリッジも出力します。

## 出力の選択

CLI では `--catalog` が必須です。`--registration`、`--aot-bridge`、`--api-reference`、`--api-reference-dir` は追加出力を選びます。メタデータのみの場合は登録とブリッジのオプションを省略します。Catalog は契約を記述し、実行コールバックを登録しません。登録コードは `VeroScript.CSharp` と、AOT ブリッジは元のマネージド宣言および生成 C# プログラムと共にコンパイルします。AOT はリフレクションや `VeroScriptInterop` を使わず直接呼び出します。

ライブラリの既定値は異なります。`CSharpBindGenOptions.EmitRegistrationSource` と `EmitApiReference` は `true`、`EmitAotBridgeSource` は `false` です。実行アダプターを生成せずメタデータだけを得るには、`EmitRegistrationSource=false` と `EmitAotBridgeSource=false` の両方を明示します。追加アダプターの制限が、そのまま Catalog メタデータの制限になるわけではありません。

## 共通の入力規則

既定では公開クラス、構造体、列挙型と公開メンバーを対象にします。`--require-attribute` は `[VeroScriptBinding]` 付きの型を選び、`[VeroScriptIgnore]` は宣言を除外します。ジェネリックなホスト型・メンバー、async、ポインター、nullable・delegate 型、省略可能または `params` 引数、演算子、参照戻り値・フィールド、インデクサー、必須メンバー、表現不能なオーバーロードは拒否します。抽象クラスのコンストラクターは省略します。追加出力にはさらに個別の制限があります。

### コンテナーのキー

map と ordered-map の key、および set element には `sbyte`、`int`、`uint`、`long`、
`ulong`、`string`、または宣言済み enum を使用できます。`sbyte` は VeroScript の signed
8-bit `char` に対応します。ほかの key shape は Binding Catalog が runtime importer に到達する前に fail closed になります。

## 実行時登録コードの制限

登録コードのコンテナは一段の配列／リスト、map、set、queue、stack で、要素はスカラー、文字列または宣言済み列挙型です。map キーと set 要素には上記のキー規則も適用します。入れ子コンテナとホストオブジェクト要素は非対応です。multi-map、multi-set、priority queue、deque には SDK の値 API または明示的ホストラッパーを使います。

登録コードはコンテナを扱う静的フィールド・プロパティ・メソッドを `VSCB3004` で拒否します。公開インスタンスフィールド、プロパティ、コンストラクター以外のメソッドを持つ構造体も、読み取り専用かどうかによらず `VSCB3001` で拒否します。これは登録コード生成の制限で、メタデータのみと AOT ブリッジは別途判定します。AOT ブリッジは構造体の公開インスタンスメソッド、書き込み可能なインスタンスフィールド、通常の公開 setter を `VSCB3006` で拒否します。ボックス化されたレシーバーの書き戻しを保持できないためです。[C# AOT](aot.md) を参照してください。

## 単一ファイルのコマンド

```powershell
veroscript-csharp-bindgen HostApi.cs `
  --catalog generated/bindings.json `
  --registration generated/Bindings.g.cs `
  --aot-bridge generated/Bindings.Aot.g.cs `
  --api-reference generated/Bindings.vs.txt
```

## プロジェクトコマンドと出力

```powershell
veroscript-csharp-bindgen Models/A.cs Models/B.cs `
  --catalog generated/bindings.json `
  --registration generated/Bindings.g.cs `
  --aot-bridge generated/Bindings.Aot.g.cs `
  --api-reference-dir generated/api `
  --manifest generated/bindings.manifest.json
```

プロジェクトモードは部分宣言を結合し、安定した参照名を割り当て、選択された全出力を置換前に準備します。生成失敗時は既存出力を保持し、書き込み失敗時は復元を試みます。`--reference` は繰り返せます。`--language-version` は C# の構文バージョンを選択し、既定値は `latest` です。`--api-reference` と `--api-reference-dir` は併用できません。API リファレンスを `--manifest` と共に出力する場合は `--api-reference-dir` を使います。`--manifest` 単独では API リファレンス出力は不要です。

## 実行時の境界

登録コールバックは複数の VM ワーカーから並行実行されるため、共有ホスト状態を同期します。AOT ブリッジはマネージド宣言を直接呼び出します。同じ生成処理の Catalog、ブリッジ、元の宣言とプログラムをまとめて使用してください。

## 生成コンテナの実行境界

Binding Catalog の metadata は ordered map を表現できます。live registration は `SortedDictionary<TKey, TValue>` を `VSCB3002`、生成 AOT bridge は `VSCB3003` で拒否します。実行時の統合には明示的な host wrapper を使用してください。live container marshalling は managed snapshot を作り、成功したコールバックの `ref`/`out` 引数だけを書き戻します。値渡しコンテナの変更は script container に反映されません。AOT bridge は対応する managed container を直接渡し、object identity を保持します。

## リンクを含む出力パス

Windows の `\\?\`、`\\.\` およびボリューム GUID のパス別名は、最終的なファイルシステム上の位置で比較します。この別名で検証を回避することはできません。別々の出力パスは引き続き使用できます。

BindGen と Transpiler CLI は出力を準備する前に既存のディレクトリとファイルのリンクを解決します。symlink や Windows junction を経由して入力ソースを上書きしたり、同じ出力を二重に指定したりすることはできません。リンクされたディレクトリ内の別々の出力は使用できます。

抽象ホストクラスは対応するメソッドとファクトリが返すインスタンスを利用できますが、コンストラクターは Catalog、ライブ登録、AOT ブリッジに出力しません。外側の型のジェネリックパラメーターに依存する入れ子の型は、属性で選択した場合も `VSCB1001` で拒否します。

AOT の `ref`、`out`、`in`、`ref readonly` パラメーターは、表現が一致する基本型と対応コンテナーで元の格納場所を共有し、引数間のエイリアスを保持します。文字列、文字、列挙型、ホストオブジェクトなど表現変換が必要な参照パラメーターは `VSCB3007` で拒否します。明示的な partial ブリッジを提供する場合は `EmitRegistrationSource=false` と `EmitAotBridgeSource=false` の両方を設定して Catalog を生成してください。

## 出力ファイル名の衝突

BindGen の API リファレンス名は、すべてのホストで大文字と小文字を区別せず比較されます。同じベース名には安定したパスハッシュを付加し、衝突が残る場合は数値の接尾辞を追加します。割り当ては入力パス順なので、入力リストの並べ替えで名前は変わりません。返されたレコードやマニフェストで `.api.vs.txt` ファイルを確認してください。

<a id="binding-marker-attributes"></a>

選択用属性の定義は入力側の宣言に属し、SDK が挿入するものではありません。ライブラリの既定値は `BindingAttributeName="VeroScriptBindingAttribute"` と `IgnoreAttributeName="VeroScriptIgnoreAttribute"` で、同等の名前を指定できます。生成登録の既定型は `VeroScript.GeneratedBindings.VeroScriptCSharpBindings` で、`RegistrationNamespace` と `RegistrationClassName` で変更します。名前の指定だけで無効な登録出力が有効になることはありません。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/bindgen.md) · [fr-FR](../fr-FR/bindgen.md) · [ja-JP](bindgen.md) · [ko-KR](../ko-KR/bindgen.md) · [zh-CN](../zh-CN/bindgen.md) · [zh-TW](../zh-TW/bindgen.md)
