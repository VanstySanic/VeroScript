# C++ フロントエンド

## プロジェクト マージと表現の境界

複数ファイルのマージは linker ではありません。入力順で定義を選んだり、
internal linkage のエンティティを改名したり、任意の ownership や storage
規則を解決したりしません。`std::shared_ptr<T>` は `T` に消去されず、
shared ownership には明示的な adapter が必要です。`std::size_t` と
`std::ptrdiff_t` は対象 ABI に従って扱われ、pointer と pointee の cv
qualification は overload identity の一部です。

`VeroScriptCppFrontend` は限定された C++17 サブセットを安定した公開 AST に
解析します。[BindGen](bindgen.md) と [Transpiler](transpiler.md) が共有します。
未対応の構文は診断を出し、暗黙に近似しません。

公開 API は `ParseSource`、`ParseFile`、`ParseFiles`、`MergeTranslationUnits`、
`MapTypeToVeroScript`、`ResolveTypeAliases`、`EvaluateEnumValues`、
`TryGetEnumValueRange`、
`GetFunctionIdentity`、`FormatDiagnostic`、`JoinTokens` です。
AST はソース位置、namespace、アクセス、修飾名、constructor、field、function、
enum、base、参照、pointer の cv qualification、`constexpr`、`consteval`、
`constinit`、`noexcept` などを保持します。

`FParseOptions` は include、`#pragma`、未知の前処理指令の拒否、関数本体の解析を制御します。`FTypeMapOptions` は既知のホスト型への生ポインター、右辺値参照、既知のホスト型・列挙型・型パラメーター、include を無視した際に必要な別名の方針を指定します。各方針を明示的に設定してください。寛容な解析に成功しても、自動バインディングや変換が可能とは限りません。

namespace、`using Name = Type`、`typedef Type Name`、型パラメーターを持つ class/function/alias
template（末尾の既定型を含む）、scoped/unscoped enum、class、struct、アクセス指定子、
field、constructor、method、free function、global、固定幅整数、標準文字列、対応する
標準コンテナを受理します。名前付き forward declaration は定義と統合されます。
通常の raw string と `u8` raw string は UTF-8 のリテラルに正規化されます。wide raw
string は拒否されます。同じ型の単純な宣言子リストは展開されますが、pointer と
reference が混在するリストは拒否されます。クラス外の constructor、destructor、
method、operator、static data 定義は宣言と照合でき、別ファイルにも配置できます。
C++17 attribute は型、enum、enumerator、field、function、parameter の構造情報として
保持されます。enum 初期化子は対象 ABI の整数リテラルの幅と符号を保持し、
C++ の整数昇格を適用します。符号なし演算の折り返しを許可し、符号付きの
オーバーフローと不正なシフト数を拒否します。最終値は公開 `int64` 表現と
マッピング後の基底型の範囲に収まる必要があります。負の符号付き値のシフトと
短絡 `&&`/`||` 式は未対応です。

基底型を明示しない unscoped enum では、先行メンバーの参照に初期化式の型を
保持します。固定基底型ではその型の整数昇格を使います。この差に依存する enum
を使う MSVC ホストは `/Zc:enumTypes`（VS 2022 17.4 以降）を有効にするか、
基底型を明示してください。このオプションは既定で無効で、`/permissive-` でも
有効になりません。SDK はホストのコンパイラオプションを変更しません。

`#pragma once`、include、通常の header guard、`extern "C"` 宣言も受理しますが、
一般的な条件付き preprocessing と未知の directive は拒否します。

複数ファイルの merge は互換性のある外部宣言を統合し、重複定義、同じ入力パス、
internal linkage の衝突、anonymous namespace を拒否します。非型パラメーター、pack、
specialization、SFINAE、concept、依存する compile-time expression、union、C array、
未知の directive、表現できない pointer は拒否します。friend の型・関数・メンバー宣言は
record-level 宣言として保持されますが、inline friend 定義は拒否されます。

型の検索では宣言スコープを優先し、相対修飾名と `::` による絶対修飾名を扱います。宣言済みのユーザー型は `string`、`vector` などの簡便なマッピングより優先します。解決できない修飾型名はエラーになります。

クラス、列挙型、および束縛済みのテンプレートパラメーターは、外側のスコープの別名より優先されます。
別名を手動で解決する場合は、`ResolveTypeAliases` の 3 引数オーバーロードを使用し、
検索オプションに `KnownHostTypes`、`KnownEnumTypes`、`KnownTypeParameters` を指定してください。

空の `FType::DeclarationScope` はグローバルスコープを表し、別名の置換でも保持されます。
`FBaseType::Type` は基底型の検索スコープとテンプレート引数を構造として保持します。
`bQualifyNamedTypes` はソース生成用の絶対修飾型名を指定します。
既定ではカタログ用の正規名を返します。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/frontend.md) · [fr-FR](../fr-FR/frontend.md) · [ja-JP](frontend.md) · [ko-KR](../ko-KR/frontend.md) · [zh-CN](../zh-CN/frontend.md) · [zh-TW](../zh-TW/frontend.md)
