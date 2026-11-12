# 12: ホストバインディングとスクリプトの統合

VeroScript の重要な使用例の 1 つは、VeroScript を C++ ホストに埋め込むことです。ホストはエンジンまたはセッションを作成し、関数と型を登録します。スクリプトは安定したパブリック サーフェスを呼び出します。このチュートリアルでは、まずバインディング カタログを使用してその境界を具体的にし、次に統合の順序について説明します。

## ステップ 1: バインディング カタログを検査する

バインディングカタログは host API の JSON 記述です。文書のリソースまたは別配布の Resources から [bindings.sample.json](../../../../../VeroScript/Resources/Scripts/examples/bindings.sample.json) を書込可能な学習ディレクトリに `bindings.sample.json` としてコピーします。SDK archive は example を含みません。以下は学習ディレクトリから実行します。

```powershell
veroscript bindings validate bindings.sample.json
veroscript bindings format bindings.sample.json -o formatted-bindings.json
```

カタログには、`version`、`functions`、`genericTypeTemplates`、`types`、`typeAliases`、`annotations` などのトップレベルのフィールドが含まれています。インターフェースについて説明します。 C++ 登録コードを置き換えたり、それ自体でランタイム実装を提供したりするものではありません。

カタログをコンパイラから見えるインターフェイス リストとして扱います。関数には少なくとも `name`、`returnType`、`paramTypes` が必要です。ホスト オブジェクトとコンテナには、対応するホスト タイプ、要素、またはキー/値のメタデータも必要です。参照パラメータは `paramTypes` と並行して `paramFlags` を使用し、パラメータの注釈は並行して `paramAnnotations` を使用します。これらのフィールドは C++ 登録署名と一致する必要があります。

たとえば、サンプル カタログの `host_add` 関数エントリでは、2 つの `int` パラメータを使用し、最初のパラメータに注釈を付けています。

```json
{
	"name": "host_add",
	"returnType": "int",
	"paramTypes": [
		"int",
		"int"
	],
	"paramFlags": [
		0,
		0
	],
	"paramAnnotations": [
		[
			{
				"name": "HOST_PARAM",
				"arguments": [
					{
						"name": "DisplayName",
						"value": "Left"
					}
				]
			}
		],
		[]
	]
}
```

カタログ メタデータ配列をランタイム実装として扱わないでください。インターフェースとツールの表面のみを説明します。すべてのフィールド、サポートされている型名、汎用テンプレート、エイリアス、および JSON 制限については、[ホスト バインディング リファレンス](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ja-JP/bindings.md) を参照してください。

## ステップ 2: スクリプトからホスト機能を呼び出す

ホストが関数を登録すると、スクリプトは通常の宣言と同じようにその関数を呼び出すことができます。たとえば、ホストが `HostLog` を登録したとします。

```VeroScript
int main()
{
	HostLog("script started");
	return 0;
}
```

静的メンバーを持つ型の場合、スクリプトは通常、型名を使用します。

```VeroScript
int main()
{
	int value = HostMath.clamp(120, 0, 100);
	print(value);
	return 0;
}
```

これらの名前は呼び出しの形式を示すだけです。実際の名前はホスト登録とカタログから取得する必要があります。

## ステップ 3: 順番に統合する

C++ ホストでは、次のシーケンスを使用します。

実行前に `FEngineConfig::VmBackendFactory`／`VmBackendPath` または Session の選択 API で互換 VM を指定します。C++ SDK のリンクだけではインタープリターは選択されません。コンパイル処理スクリプトにも VM が必要です。SDK ターゲットと設定は [VeroScriptBytecodeVm](../../../../../Modules/Execution/VeroScriptBytecodeVm/Resources/Docs/User/ja-JP/index.md) を参照してください。

1. `VeroScriptEngine` または長期存続する `VeroScriptSession` を作成します。
2. ネイティブ関数、ホストタイプ、メンバー、演算子を登録します。
3. ツールが公開する API のドキュメントとパラメーターのメタデータを追加します。
4. バインディング カタログをエクスポートまたはロードし、`bindings validate` を実行します。
5. 最初に `CheckSource`/`CheckFile` を呼び出し、チェックが成功した後に `RunSource`/`RunFile` を呼び出します。
6. ブール値のみを返すのではなく、チェックと実行の失敗に対する診断を保存します。

スクリプトの有効期間が長いエディタ、ゲーム、ホスト ツールは通常、モジュール、バインディング、実行状態、ホット リロードに `VeroScriptSession` を使用します。ワンショット実行では、`VeroScriptEngine` のソースおよびファイルのエントリ ポイントを使用できます。

## ステップ 4: インターフェースを安定に保つ

バインディングをバージョン管理された API として扱います。関数の追加は通常、互換性のある拡張機能です。メンバーの削除、パラメータ タイプの変更、列挙値の変更、または所有権セマンティクスの変更を行うと、スクリプトが破損する可能性があります。ホスト バージョンを出荷する前に、以下を確認してください。

- バインディング カタログが検証されます。
- スクリプトは `--check` を渡します。
- 登録テーブルはカタログと一致しています。
- オブジェクト、構造体、ハンドルの有効期間が文書化されます。

登録した static host member が明示的な `HostObject` 引数を持つ場合は、`nullptr`
の nullable boundary も確認できます。callback には空の host-object handle が
渡されるため、payload を参照する前に確認してください。null の instance receiver
は無効です。

## 演習

1. 1 つのホスト関数の安定した名前、パラメータ リスト、および戻り値の型を設計します。
2. 短いドキュメントとパラメータの説明をバインディングに追加します。
3. 1 つのカタログ エントリを変更し、カタログ検証とスクリプト チェックの両方を実行します。

完全な境界については、[ホスト統合](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ja-JP/embedding.md) および [ホスト バインディング](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ja-JP/bindings.md) を参照してください。


次: [ランタイム リフレクション](13-reflection.md)。

<!-- docs-navigation -->

---

[チュートリアル](../index.md)

言語: [en-US](../../en-US/advanced/12-host-bindings.md) · [fr-FR](../../fr-FR/advanced/12-host-bindings.md) · [ja-JP](12-host-bindings.md) · [ko-KR](../../ko-KR/advanced/12-host-bindings.md) · [zh-CN](../../zh-CN/advanced/12-host-bindings.md) · [zh-TW](../../zh-TW/advanced/12-host-bindings.md)
