# C# ホストバインディング

## インターセプトと mutation

インターセプターは `SetInstanceCallInterceptor`、mutation handler は
`SetMutationHandler` で登録します。`CallMethodBypassingInterceptor` は
インターセプターを回避して操作を呼び出します。

`VeroScriptNativeType` はプリミティブ、ホスト型、型引数を含む再帰的な型記述です。関数の戻り値、引数、`VeroScriptParameterFlags` を実際の callback 契約と一致させます。

セッション API ではホスト型、メンバー、ジェネリック型、型エイリアスを登録します。`Construct`、`CallMethod`、`GetField`、`SetField` は登録済みのメンバーだけを公開します。インターセプターは `VeroScriptInterceptResult` で続行、置換、拒否を明示し、アクセス制御を迂回してはいけません。

ジェネリック型ファクトリの `VeroScriptGenericHostTypeContext` では
`ArgumentCount`、`GetArgument`、`SetType`、`AddMember`、`SetProtocol`、
`SetError` を使用できます。インターセプト コンテキストは `Receiver`、
`ArgumentCount`、`GetTarget`、`GetArgument`、`Proceed`、`SetError` を公開し、
借用値は callback の外へ持ち出せません。

シリアライザーと `SerializeObject`/`DeserializeObject` は型、バージョン、データの出所を検証します。`SetMutationHandler` は `VeroScriptMutationKind` を通知します。シリアライズ側では `GetField`/`SetBytes`、デシリアライズ側では `EncodedVersion`/`GetBytes`/`AddField` を使用します。callback 中の借用値は戻る前に消費し、同じセッションへ無制限に再入しないでください。完了済みの変更は通知によって取り消せません。

マネージド例外をネイティブ ABI の外へ投げず、ステータスまたは `VeroScriptException` として処理してください。

## 関数と callback

`RegisterFunction` では戻り値と引数の型を明示します。

```CSharp
session.RegisterFunction(
	"host_double",
	VeroScriptNativeType.Int32,
	[VeroScriptNativeType.Int32],
	(context, result) => result.Set(context.GetArgument(0).GetInt32() * 2));
```

`GetArgument` は読み取り専用引数を返します。変更可能な参照として宣言した引数だけに `GetMutableArgument` を使ってください。callback は渡された借用 return view に結果を書き込み、意図した失敗には `context.SetError(...)` を呼びます。`ArgumentCount`、`ProgramGeneration`、`GetHostThis` は呼び出し情報を提供します。managed exception は callback failure に変換されます。

詳細なメタデータには `VeroScriptHostFunctionDefinition` を使い、documentation、引数名と説明、annotation、型パラメーター、generic host object flag を指定します。`ApplyClassDefaults` はオブジェクトの作成または読み込み後に登録済み既定値を適用し、`EnumerateObjectReferences` は script と host の参照を訪問します。visitor の借用値は callback の終了時に無効になります。`RetainCallable` は script 関数または closure の所有参照を作成します。

`RegisterFunction` に加え、`RegisterHostFunction`、`RegisterAnnotation`、`RegisterHostType`、`RegisterHostMember`、`RegisterHostProtocol`、`RegisterTypeAlias`、`RegisterGenericHostTypeTemplate`、`InstantiateGenericHostType` を使います。`VeroScriptNativeParameter` は型と `None`、`Ref`、`ConstRef` を組み合わせます。callback が script storage に書き戻す場合だけ `Ref` を選択してください。`VeroScriptHostTypeDefinition` と `VeroScriptHostMemberDefinition` は enum/struct/class、constructor、method、property、field、constant、operator を記述します。

## 注釈名の照合規則

`RegisterAnnotation` の `VeroScriptAnnotationDefinition.CaseSensitive` は既定で
`true` です。`false` にすると、その注釈名の ASCII 文字の大文字と小文字を同一視します。
引数名と metadata キーは完全一致のままで、リフレクションはソースの表記を保持します。
完全一致と大文字小文字を無視する登録が重複すると、カタログを変更せずに失敗します。
`ExportBindingsJson()` は `caseSensitive` を保持します。登録はコンパイル、
リフレクション、ツールに文書情報も提供しますが、プロセッサは別途設定します。
名前照合用フィールドを追加した、対応する現在のネイティブ SDK と共に配布してください。

## シグネチャ、入れ子の呼び出し、永続化

`VeroScriptNativeType` は `Void`、`Bool`、`String`、`HostObject`、`Enum`、`Function` などを区別します。コンテナーと関数は `TypeArguments` を使います。ネイティブの `fn(int)->int` はソースの `function(int)->int` に対応し、これらの識別子は翻訳しません。

`VeroScriptCallContext` コールバック内の `InvokeFunction` と `InvokeCallable` は、セッション操作へ再入せずスクリプト対象を呼び出します。返された所有値は解放してください。借用引数と戻り値ビューは現在のコールバック内だけで有効です。ジェネリック型の複製、等価比較、ハッシュ、サブクラスのプロトコルはファクトリーから設定され、セッションが保持します。

`VeroScriptInterceptContext` は `Unhandled`、`Proceed`、置換結果、`Failed` を扱います。`RegisterExternalSerializer` は外部永続化を登録します。シリアライズ側は `TypeName`、`MutationVersion`、`FieldCount`、`GetField`、`SetBytes`、復元側は `EncodedVersion`、`GetBytes`、`AddField`、`SetError` を提供します。バイト列の型、版、出所を確認してください。この API はパッケージやネイティブライブラリを認証しません。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/host-bindings.md) · [fr-FR](../fr-FR/host-bindings.md) · [ja-JP](host-bindings.md) · [ko-KR](../ko-KR/host-bindings.md) · [zh-CN](../zh-CN/host-bindings.md) · [zh-TW](../zh-TW/host-bindings.md)
