# ホストバインディングとバインディングカタログ

ホスト バインディングは、関数、型、メンバー、およびメタデータを C++ ホストから VeroScript に公開します。バインディング カタログは、そのパブリック インターフェイスの JSON 記述です。 CLI は、スクリプトを検証してフォーマットし、スクリプトをチェックまたは実行するときに 1 つ以上のカタログをロードできます。

カタログはインターフェイス コントラクトです。これは C++ 登録コードを置き換えるものではなく、それ自体でランタイム実装を提供するものでもありません。カタログ内のエントリは、コンパイラがそのエントリを参照できることを意味します。ホストは、スクリプトを実行する前に、一致する実装を登録する必要があります。

## カタログ ファイルとバージョン

最上位オブジェクトには次のフィールドが含まれます。カタログをインポートする場合、配列フィールドは省略できます。ホストエクスポートはそれらすべてを書き込みます。 `version` が存在する場合は、整数 `1` である必要があります。

|フィールド |タイプ |説明 |
| --- | --- | --- |
| `version` |整数 |バインディングカタログ形式のバージョン。現在は `1` です。 |
| `functions` |配列 |スクリプトから呼び出し可能なホスト関数の説明。 |
| `genericTypeTemplates` |配列 |一般的なホストタイプのテンプレートの説明。 |
| `types` |配列 |ホストクラス、構造体、列挙型、およびそれらのメンバー。 |
| `typeAliases` |配列 |ホストのタイプとエイリアスの説明。 |
| `annotations` |配列 |リフレクション、ツール、ドキュメント用の注釈ボキャブラリー。 |

完全に空のカタログは次のように記述できます。

```json
{
	"version": 1,
	"functions": [],
	"genericTypeTemplates": [],
	"types": [],
	"typeAliases": [],
	"annotations": []
}
```

ファイルを `.json` として保存します。コード ブロック上の `json` ラベルは、ドキュメントの構文を強調表示するラベルにすぎません。 JSON が `.vs` スクリプトになるわけではありません。

## 関数の説明

`functions` の各オブジェクトには、少なくとも `name`、`returnType`、および `paramTypes` が必要です。

|フィールド |タイプ |必須 |説明 |
| --- | --- | --- | --- |
| `name` |文字列 |はい |スクリプトに表示される名前。 |
| `returnType` |文字列 |はい |スクリプト タイプ名 (`int`、`string`、`object` など)。 |
| `paramTypes` |文字列配列 |はい |パラメータの型は C++ 登録と同じ順序です。 |
| `documentation` |文字列 |いいえ |関数のドキュメント。 |
| `paramNames` |文字列配列 |いいえ |パラメーター名はパラメーターの順序で並べられます。 |
| `paramDocumentation` |文字列配列 |いいえ |パラメーターのドキュメントをパラメーターの順序で示します。 |
| `returnDocumentation` |文字列 |いいえ |戻り値のドキュメント。 |
| `annotations` |オブジェクト配列 |いいえ |関数の注釈インスタンス。 |
| `paramAnnotations` |オブジェクト配列の配列 |いいえ |パラメータの注釈。外側の位置は `paramTypes` に対応します。 |
| `returnHostTypeName` |文字列 |いいえ | `object`、`enum`、または `function` の戻り値のホスト タイプ名または署名。 |
| `paramHostTypeNames` |文字列配列 |いいえ | `paramTypes` に相当するホスト タイプ名。 |
| `returnElementTypeName` |文字列 |いいえ |単項コンテナの戻り値の要素タイプ。 |
| `paramElementTypeNames` |文字列配列 |いいえ |単項コンテナパラメータの要素タイプ。 |
| `returnKeyTypeName` |文字列 |いいえ |マップ戻り値のキーのタイプ。 |
| `returnValueTypeName` |文字列 |いいえ |マップの戻り値のタイプ。 |
| `paramKeyTypeNames` |文字列配列 |いいえ |マップパラメータの主要なタイプ。 |
| `paramValueTypeNames` |文字列配列 |いいえ |マップパラメータの値のタイプ。 |
| `paramFlags` |整数配列 |いいえ |パラメータ受け渡しモード。省略された場合は、すべてのパラメータが値によって渡されることを意味します。 |
| `typeParams` |文字列配列 |いいえ |汎用ホスト関数の型パラメータ。 |
| `scriptReturnTypeName` |文字列 |いいえ |汎用呼び出しのスクリプト側で使用される戻り値の型。 |
| `genericHostObjectCast` |ブール値 |いいえ |対応する汎用登録の汎用ホスト/オブジェクト変換を有効にします。 |

すべての並列配列は同じパラメータ位置を使用します。`paramHostTypeNames[1]` と `paramFlags[1]` は `paramTypes[1]` を表します。一貫性のない長さまたは位置は、CLI バリデーターの診断に従って修正する必要があります。

### 型名とコンテナーのメタデータ

`returnType` および `paramTypes` は、ホスト バインディングでサポートされている型名を使用します。

|タイプカテゴリ |受け入れられる名前 |
| --- | --- |
|プリミティブ型と呼び出し可能型 | `void`、`bool`、`char`、`short`、`int`、`long`、`float`、`double`、`string`、`wstring`、`unsigned`、 `unsigned int`、`unsigned char`、`unsigned short`、`unsigned long`、`int8`、`uint8`、`int16`、`uint16`、`int32`、`uint32`、`int64`、 `uint64`、`enum`、`function`、`fn` |
|ホスト オブジェクト | `object`、`HostObject` |
|単項コンテナ | `array`、`DynamicArray`、`set`、`DynamicSet`、`multiset`、`MultiSet`、`ordered_multiset`、`OrderedMultiSet`、`queue`、`stack`、`deque`、 `channel`、`priority_queue`、`PriorityQueue` |
|地図 | `map`、`DynamicMap`、`ordered_map`、`OrderedMap`、`multimap`、`MultiMap`、`ordered_multimap`、`OrderedMultiMap` |

コンテナ要素、キー、および値のタイプは、別個のメタデータ フィールドに保存されます。

|タイプ |フィールドを返す |パラメータフィールド |
| --- | --- | --- |
| `array`、セット、キュー、スタック、デキュー、`channel`、および優先キュー | `returnElementTypeName` | `paramElementTypeNames` |
| `map`、`ordered_map`、`multimap`、および `ordered_multimap` | `returnKeyTypeName`、`returnValueTypeName` | `paramKeyTypeNames`、`paramValueTypeNames` |
| `object`、`enum`、および `function` | `returnHostTypeName` | `paramHostTypeNames` |

たとえば、`Widget` ホスト オブジェクトを返し、`array<int>` および `map<string,double>` を受け入れる関数は、その型を次のように記述することができます。

```json
{
	"name": "inspect_widget",
	"returnType": "object",
	"paramTypes": ["array", "map"],
	"returnHostTypeName": "Widget",
	"paramElementTypeNames": ["int", ""],
	"paramKeyTypeNames": ["", "string"],
	"paramValueTypeNames": ["", "double"]
}
```

### パラメータフラグ

各 `paramFlags` 要素はビット マスクです。

|値 |意味 |
| --- | --- |
| `0` |値によって渡されます。 |
| `1` (`0x01`) | `const` 以外の参照。 |
| `3` (`0x01 | 0x02`) | `const` 参照。 |

他のビットの組み合わせは無効です。 `const` ビットは、リファレンス ビットと一緒にのみ有効です。フラグは、登録されている C++ 署名と一致する必要があります。カタログでは、値渡しの C++ 関数を参照関数に変換できません。

## ホストの種類とメンバー

`types` の各オブジェクトには、`name` および `kind` が必要です。 `kind` は、`enum`、`struct`、または `class` である必要があります。

|フィールド |タイプ |説明 |
| --- | --- | --- |
| `name` |文字列 |スクリプトに表示される名前。 |
| `kind` |文字列 | `enum`、`struct`、または `class`。 |
| `documentation` |文字列 |ドキュメントを入力します。 |
| `annotations` |オブジェクト配列 |型の注釈インスタンス。 |
| `inheritable` |ブール値 |型が派生を許可するかどうか。 |
| `scopedEnum` |ブール値 |列挙型がスコープ付きアクセス セマンティクスを使用するかどうか。 |
| `hasClone` |ブール値 |コピー動作が提供されるかどうか。 |
| `hasEquals` |ブール値 |等価比較が提供されるかどうか。 |
| `hasHash` |ブール値 |ハッシュが提供されるかどうか。 |
| `hasSubclass` |ブール値 |ホストのサブクラス構築がサポートされているかどうか。 |
| `genericTemplateName` |文字列 |このタイプがインスタンス化された汎用ホスト テンプレート。 |
| `genericTypeArguments` |文字列配列 |汎用テンプレートの実際の型引数。 |
| `members` |オブジェクト配列 |コンストラクター、メソッド、プロパティ、フィールド、または列挙メンバー。 |

各メンバーには `kind` が必要です。通常のメンバーにも `name` が必要です。サポートされているメンバーの種類は次のとおりです。

| `kind` |意味 |
| --- | --- |
| `constructor` |コンストラクタ;その名前は空の文字列である可能性があります。 |
| `method` |インスタンスメソッド。 |
| `staticMethod` |静的メソッド。 |
| `propertyGetter` |プロパティゲッター。 |
| `propertySetter` |プロパティセッター。 |
| `enumMember` | Enum メンバー。 |
| `field` |インスタンスフィールド。 |
| `staticConstant` |静的定数。 |

メンバーは、関数のドキュメント、注釈、パラメーター、戻り値、および型メタデータのフィールドを受け入れます。さらに以下をサポートします。

|フィールド |タイプ |説明 |
| --- | --- | --- |
| `enumValue` |整数 |列挙値。安全な JSON 番号範囲内の正確な JSON 整数である必要があります。 |
| `enumValueInt64` |文字列 |完全な符号付き 64 ビット列挙値。正確な JSON 番号の範囲外で使用してください。 |
| `operator` |ブール値 |メンバーがオペレーターであるかどうか。 |
| `constMethod` |ブール値 |メソッドが `const` かどうか。 |
| `mutableThis` |ブール値 |メソッドが現在のオブジェクトを変更できるかどうか。 |
| `nativeName` |文字列 |ネイティブホスト名。スクリプトに表示される名前は `name` です。 |

メンバーは、`returnType`、`paramTypes`、および `paramFlags` に対して関数と同じ意味を使用します。 enum エントリには `enumMember` を使用します。正確な JSON 整数の範囲外の値については、`enumValue` におおよその JSON 数値を書き込まないでください。代わりに、その 10 進表現を `enumValueInt64` に書き込みます。

## 汎用テンプレートと型エイリアス

### `genericTypeTemplates`

各汎用テンプレートには次のものが含まれます。

|フィールド |タイプ |必須 |説明 |
| --- | --- | --- | --- |
| `name` |文字列 |はい |テンプレート名。 |
| `arity` |整数 `1` ～ `255` |はい |型パラメータの数。 |
| `documentation` |文字列 |いいえ |テンプレートのドキュメント。 |

### `typeAliases`

各エイリアスには次のものが含まれます。

|フィールド |タイプ |必須 |説明 |
| --- | --- | --- | --- |
| `name` |文字列 |はい |別名。 |
| `typeParams` |文字列配列 |はい |エイリアス型パラメータ。空いているかもしれません。 |
| `targetTypeName` |文字列 |はい |ターゲットのタイプ名。 |
| `documentation` |文字列 |いいえ |エイリアスのドキュメント。 |

例えば：

```json
{
	"genericTypeTemplates": [
		{ "name": "Box", "arity": 1, "documentation": "A generic host box." }
	],
	"typeAliases": [
		{
			"name": "WidgetId",
			"typeParams": [],
			"targetTypeName": "int",
			"documentation": "The identifier used by the host."
		}
	]
}
```

## 注釈の語彙とインスタンス

最上位の `annotations` 配列は、注釈ボキャブラリです。各エントリは以下をサポートします。

|フィールド |タイプ |説明 |
| --- | --- | --- |
| `name` |文字列 |注釈名。 |
| `detail` |文字列 |カテゴリまたは署名の短い説明。 |
| `documentation` |文字列 |注釈ドキュメント。 |
| `arguments` |オブジェクト配列 |アノテーションの引数の説明。 |
| `caseSensitive` | 真偽値 | 既定値は `true`。`false` はこの定義の ASCII 英字の大文字と小文字を区別しません。 |
| `targets` | 整数 | 対象のビットマスク。既定値は `131071`（対応する全対象）、有効範囲は `1`～`131071` です。 |

名前はソースの表記を保持します。受け付ける表記が重なる登録は拒否されます。引数名とメタデータキーは大文字と小文字を区別します。各引数の説明には、同じ構造の再帰的な `children` 配列も指定できます。これは入れ子の引数の説明であり、実行可能な値ではありません。ネイティブ登録には `FHostBindingRegistry::RegisterAnnotation(..., Targets, CaseSensitive)` を使います。カタログのメタデータはプロセッサのコールバックを登録しません。

各ボキャブラリー引数は、`name`、`detail`、および `documentation` をサポートします。語彙には、利用可能な注釈名が記載されています。注釈は関数または型に自動的に適用されません。

関数、型、またはメンバーの `annotations` フィールドには、注釈インスタンスが含まれます。

```json
{
	"name": "HOST_FUNCTION",
	"arguments": [
		{ "name": "Category", "value": "Examples|Math" },
		{ "name": "ReadOnly" }
	]
}
```

`value` フィールドは省略できます。省略は値のないフラグ引数を意味します。 `"value": ""` は、引数に空の文字列値が明示的に含まれていることを意味します。これらの意味は異なります。 `paramAnnotations` はパラメータ注釈インスタンスの 2 次元配列であり、その外側の位置は `paramTypes` と一致する必要があります。

統合 Resources 文書から[バインディングカタログの例](../../../../../../../VeroScript/Resources/Scripts/examples/bindings.sample.json)を取得し、書き込み可能な作業ディレクトリに `bindings.sample.json` としてコピーします。カスタム注釈のメタデータとホスト型を示す例であり、機能用 SDK アーカイブに含まれるとは限りません。以下のコマンドはそのディレクトリから実行し、`app.vs` は利用者のスクリプトに置き換えてください。

## 検証、フォーマット、ロード

CLI には、カタログ保守コマンドが用意されています。

```powershell
veroscript bindings validate bindings.sample.json
veroscript bindings format bindings.sample.json
veroscript bindings format bindings.sample.json -o formatted-bindings.json
```

`-o` または `--output` を省略すると、`bindings format` は入力カタログを書き換えます。出力パスを指定すると、そのファイルに書き込みます。標準出力は書き込みの確認メッセージであり、整形済み JSON ではありません。結果は出力ファイルから読み取ってください。

`validate` は、JSON タイプ、必須フィールド、サポートされている列挙値、並列配列、および数値範囲をチェックします。 `format` は、ツールチェーンの安定した形式で JSON を出力します。新しいファイルにフォーマットする場合は、一時パスを使用し、元のファイルを置き換える前に結果を確認してください。

反復可能な `--binding-catalog` オプションを使用してスクリプトをチェックまたは実行しながら、1 つ以上のカタログをロードします。

```powershell
veroscript --binding-catalog bindings.sample.json app.vs
```

インポートされたカタログは、コンパイル時の可視性とメタデータを提供します。インポートされた関数とメンバーの説明は、実行時コールバックを置き換えません。ホストが一致する実装を登録していない場合、スクリプトは実行時に失敗する可能性があります。

インポーターは、リソース制限 (最大ファイル サイズ `128 MiB`、JSON 値 `4,194,304`、ネストの深さ `256`) も適用します。これらの制限により、カタログが検証中に過剰なリソースを消費することが防止されます。

## C++ でのバインディングの登録

組み込みホストは通常、`VeroScriptSession` を使用してバインディングを登録します。公的登録の境界には次のものが含まれます。

- 単純なネイティブ関数またはパラメーター メタデータを含む構造化関数の場合は `RegisterNative`。
- ホスト クラス、構造体、列挙型の場合は `RegisterHostType`。
- コンストラクター、メソッド、静的メンバー、プロパティ、フィールドの場合は `RegisterHostMember`。
- ホスト タイプのオペレーターの場合は `RegisterHostOperator`。
- ホストタイプのコピー動作の場合は `RegisterHostTypeClone`。
- 型および列挙型セマンティクスの場合は、`SetHostTypeInheritable` および `SetHostEnumScoped`。
- 派生可能なホスト タイプのインスタンスを構築するための `RegisterHostTypeSubclassFactory`。
- `RegisterGenericHostTypeTemplate` および `InstantiateGenericHostType` (汎用ホスト タイプ テンプレート用)。
- `GetBindingCatalog`: 現在のセッションによってエクスポートされたカタログを読み取ります。

ホストの登録後、カタログをエクスポートし、CLI バリデーターを実行して、ドキュメント、静的チェック、およびランタイム登録の整合性を維持します。静的ホスト メンバーは通常、`Type.staticMethod(...)` としてアクセスされます。ホスト定数は `Type::Constant` を使用します。より広範なエンジンのライフサイクルと `CheckSource`/`RunSource` または `CheckFile`/`RunFile` の使用法については、「[ホスト統合](embedding.md)」を参照してください。

## nullable host-object 引数

明示的な `HostObject` 引数を持つ static host member は、script の `nullptr` を
受け取れます。callback には空の host-object handle が渡されるため、payload を
参照する前に確認してください。null の instance receiver は無効です。この規則は
static call の明示的な引数に適用されます。

## 互換性の原則

バインディング カタログはパブリック インターフェイスの一部です。関数またはメンバーの追加は通常、互換性のある拡張機能です。エントリの削除、パラメータ タイプまたは参照フラグの変更、列挙値の変更、ホスト タイプ名の変更、またはメンバーの可視性の変更を行うと、スクリプトが中断される可能性があります。カタログまたはホスト バインディングをアップグレードするときは、カタログ検証、スクリプト チェック、およびターゲット プロジェクトの統合テストを一緒に実行します。

## ホストのオーバーロード

<a id="host-overload-resolution"></a>

通常のホストのインスタンスメソッド、静的メソッド、ネイティブ自由関数は、正規化した引数型と参照修飾によるオーバーロードに対応します。引数シグネチャの重複や戻り値型だけの違いは登録時に拒否されます。呼び出しは引数型、数値変換、variant の代替規則で選択し、同順位は曖昧性として診断します。名前付きホストメンバーにはシグネチャから安定したネイティブ識別子を生成します。コンストラクターと演算子の既存規約は維持します。名前だけの `FindFunction` は複数候補に null を返します。各宣言と個別の説明は `GetFunctions()` で列挙します。バインディングカタログの入出力は全候補を保持します。単一シグネチャの既存の登録 API は引き続き使用できます。

`ref` の省略時は既存のネイティブ値入力／ハンドル動作を維持します。呼び出し元の保存先の置換には明示的な `ref` が必要です。通常の引数では利用可能な値シグネチャを優先します。明示的な参照は保存先の型と書き込み可否を検査し、`const ref` は const 変数を受け取れます。コンパイル拡張は全候補を保持します。VM、Mixed、JIT、Core AOT はコアの選択を共有します。独立した C++ ソース出力は別の C リンケージ規約を維持し、未対応の名前付きホスト多重定義には Core AOT の使用を案内します。

## ネイティブコールバックのメタデータ

IScriptCallInvoker::GetActiveModule() は現在のネイティブコールバック中だけ、借用した読み取り専用の意味メタデータを提供します。利用できないホストは nullptr を返せます。終了後にポインター、借用した呼び出しサービス、内部参照を保持してはいけません。値やプログラム所有者を保持しても、この借用期間は延長されません。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/bindings.md) · [fr-FR](../fr-FR/bindings.md) · [ja-JP](bindings.md) · [ko-KR](../ko-KR/bindings.md) · [zh-CN](../zh-CN/bindings.md) · [zh-TW](../zh-TW/bindings.md)
