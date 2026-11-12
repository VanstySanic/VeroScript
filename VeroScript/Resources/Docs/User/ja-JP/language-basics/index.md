# 言語の基礎

このセクションでは、VeroScript の基本的な言語規則をトピックごとに説明します。各ページでは 1 つの比較的独立した主題を取り上げ、小さな例を使用して構文と動作を定義します。完全な API とランタイム メカニズムは個別に文書化されています。

## ソース、モジュール、スコープ

- [基本構文](basic-syntax.md): ソース ファイル、ステートメント、ブロック、識別子、およびキーワード。
- [コメント](comments.md): 単一行およびブロックコメント。
- [スコープ](scope.md): モジュール、名前空間、関数、およびブロックのスコープ。
- [名前空間](namespaces.md): 名前空間宣言、修飾名、および `using namespace`。
- [`std` 名前空間](standard-namespace.md): 組み込み型、ランタイム関数、および互換性エイリアスの完全なリスト。
- [モジュール](modules.md): モジュールのインポート、エクスポート、再エクスポート、およびライブラリ パッケージのインポート。
- [プリプロセッサ](preprocessor.md): マクロ、条件付きコンパイル、文字列化、トークンの貼り付け、およびモジュールのインクルード。

## 値と型

- [リテラル](literals.md): 整数、浮動小数点値、文字列、ワイド文字列、ブール値、および null 値。
- [データ型](data-types.md): 組み込み型、固定幅整数、コンテナ、および型推論。
- [値ユーティリティ型](value-utility-types.md): optional、weak、共有構造体ハンドル、およびマップ エントリの pair。
- [変数](variables.md): 宣言、初期化、割り当て、および推論された変数。
- [定数](constants.md): `const` 変数および定数メンバー。
- [宣言](declarations.md): `using`、`typedef`、`= default`、および `= delete`。
- [構造体](structs.md): 値の型、フィールド、メソッド、構築、および構造体コピーのセマンティクス。
- [クラス](classes.md): 共有 ID、メンバー アクセス、構築、継承、および仮想メソッド。
- [インターフェイス](interfaces.md): メソッド コントラクト、デフォルト実装、インターフェイスの継承、および変換。
- [Enums](enums.md): 列挙子、スコープ、基礎となる整数型、および列挙値。
- [配列](arrays.md): 動的配列、固定サイズのネイティブ配列、インデックス付け、および範囲の反復。
- [コンテナ](containers.md): マップ、セット、キュー、スタック、両端キュー、およびチャネル。
- [文字列](strings.md): `string`、`wstring`、文字列操作、および標準ライブラリ ヘルパー。
- [参照](references.md): 参照パラメータ、読み取り専用参照、および参照範囲の反復。
- [リフレクション](reflection.md): `type<T>`、フィールドとメソッドの情報、列挙型情報、および宣言メタデータ。
- [静的メンバー](static-members.md): 静的ユーティリティ型、型レベルのフィールド、静的メソッド、およびジェネリック型の独立したストレージ。

## 式と演算子

- [演算子](operators.md): 算術、比較、論理、ビット単位、代入、および演算子のオーバーロード。

## ジェネリック、関数、および制御フロー

- [ジェネリック](generics.md): ジェネリック型、ジェネリック関数、デフォルトの型引数、および `where` 制約。
- [関数](functions.md): 宣言、パラメータ、戻り値、デフォルト、およびオーバーロード。
- [ラムダと関数の値](lambdas.md): 関数タイプ、ラムダ式、キャプチャ、および参照パラメータ。
- [制御フロー](control-flow.md): 条件、ループ、`switch`、ジャンプ、およびリターン。

[注釈とソースメタデータ](annotations.md)

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/index.md) · [fr-FR](../../fr-FR/language-basics/index.md) · [ja-JP](index.md) · [ko-KR](../../ko-KR/language-basics/index.md) · [zh-CN](../../zh-CN/language-basics/index.md) · [zh-TW](../../zh-TW/language-basics/index.md)
