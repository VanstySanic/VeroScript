# C# の値と所有権

`VeroScriptValue` は独立した Interop 値ハンドルを所有し、セッションの子オブジェクトではありません。所有する値は `Dispose()` または `using` で解放します。`Clone()` はランタイムの複製規則に従う別の所有ハンドルを作り、深いコピーを保証しません。`Reset()` は空の値に置き換えます。変更や解放の同時実行は呼び出し側で同期してください。

## モジュールに依存する内容

ハンドルを所有しても、値の内容がセッションから独立するわけではありません。
スクリプトのオブジェクトや関数、モジュール値のスロットを持つ可変コンテナーは、
空でもセッションの作成スレッドに制限され、セッション終了後は利用できません。
ホストが作った同様の格納領域は、セッションで初めて使われると関連付けられ、
既存の共有エイリアスにも同じ所有権が適用されます。`Clone()` はこの制限を解除しません。
VM ワーカーのコールバックから所有値として取り出せるのは独立した値だけです。
variant/関数スロットを持つ空コンテナーも外部の所有領域には複製できません。
非公開の借用戻り値はワーカー内で値を転送できます。セッション終了後も、
使い終わった所有ハンドルは解放してください。

## スカラー値

読み取りメソッドはスクリプトの型に合わせます。

| 設定 | 取得 | スクリプト型 |
| --- | --- | --- |
| `Set(sbyte)` | `GetInt8()` | `int8` |
| `Set(byte)` | `GetUInt8()` | `uint8` |
| `Set(short)` | `GetInt16()` | `int16` |
| `Set(ushort)` | `GetUInt16()` | `uint16` |
| `Set(int)` | `GetInt32()` | `int32` |
| `Set(uint)` | `GetUInt32()` | `uint32` |
| `Set(long)` | `GetInt64()` | `int64` |
| `Set(ulong)` | `GetUInt64()` | `uint64` |
| `Set(float)` | `GetFloat()` | `float` |
| `Set(double)` | `GetDouble()` | `double` |
| `Set(bool)` | `GetBool()` | `bool` |
| `SetString` | `GetString` | `string` |
| `SetWideString` | `GetWideString` | `wstring` |

型付き列挙値は `SetEnum(typeName, value)` / `GetEnum()` で扱います。`Kind` と `TypeName` は現在の表現を示します。`SetNull()` は `nullptr` を作り、`IsNull` はその値だけを判定します。`Reset()` 後は `Kind` が `Invalid`、`IsNull` が false です。解放後のアクセスは `ObjectDisposedException` になります。null のホスト参照を受け入れるかは受信側の引数契約に従います。

`Set(VeroScriptValue)` は値全体を代入し、コンテナー、ホストオブジェクト、関数、チャネルのランタイム上の同一性を維持します。代入先は書き込み可能である必要があり、読み取り専用の借用値は使えません。

## コンテナーとチャネル

`SetArray`、`SetSequence`、`SetMap`、`SetSet`、`SetChannel` には明示的な型名を指定します。`VeroScriptValueKind` を取るオーバーロードで種類を選択します。`SetSequence` は `Queue`、`PriorityQueue`、`Stack`、`Deque` などのシーケンス、`SetMap` は `Map`、`OrderedMap`、`MultiMap`、`OrderedMultiMap`、`SetSet` は `Set`、`MultiSet`、`OrderedMultiSet` に対応します。

通常の map は重複キーの値を置き換え、multi-map は各キーと値の対応を保持します。通常の set は重複を除き、multi-set は個数を保持します。優先度キューはランタイムの `top` / `pop` で最も優先度の高い要素から取り出します。

シーケンスは `SequenceCount` / `GetSequenceElement`、map は `MapCount` / `TryGetMapValue`、set は `SetCount` / `SetContains` で読み取ります。`EnumerateMap` と `EnumerateSet` には `VeroScriptCapabilities.ContainerEnumeration` が必要です。読み取り専用のスナップショットを列挙し、`VeroScriptBorrowedValue` 引数は各コールバックの終了時に失効します。保持する場合はそのコールバック内で `Clone()` してください。

```CSharp
using var key = new VeroScriptValue();
key.SetString("answer");
using var number = new VeroScriptValue();
number.Set(42);
using var map = new VeroScriptValue();
map.SetMap("string", "int", [key], [number]);

map.EnumerateMap((entryKey, entryValue) =>
{
	Console.WriteLine(entryKey.GetString());
	Console.WriteLine(entryValue.GetInt32());
});
```

チャネルは `ChannelSend`、`ChannelTrySend`、`ChannelSendFor`、`ChannelReceive`、`ChannelTryReceive`、`ChannelReceiveFor`、`ChannelClose` を提供します。`ChannelTrySend` と `ChannelSendFor` は bool、待機時間はミリ秒です。非ブロック受信と時間制限付き受信は所有値または `null` を返します。状態は `ChannelCount`、`ChannelCapacity`、`IsChannelClosed` で確認します。応答性が必要なスレッドでブロック受信しないでください。チャネル操作はランタイムが同期しますが、並行使用中にラッパーをリセット、置換、解放してはいけません。

## ホストオブジェクトと保持 callable

`SetHostObject(typeId, typeName, managedObject)` はネイティブ値が保持する間、マネージドオブジェクトを生存させます。`Func<string, bool> isType` オーバーロードは互換サブタイプを報告します。`GetHostObject()` は `VeroScriptHostObjectInfo` を返し、`GetHostObject<T>()` はマネージド型を検証します。

`VeroScriptCallable` は保持した関数またはクロージャーを所有します。使用後に解放してください。既定では作成スレッドだけで呼び出せます。`RetainCallable(..., ownerThreadOnly: false)` で別スレッドを許可しても、ホスト側で関連セッションの全操作との直列化と状態の同期が必要です。callable 内部のロックはセッション全体を保護しません。

## 借用と有効期間

ネイティブ関数、インターセプター、ジェネリックファクトリー、シリアライザー、変更通知、列挙の借用コンテキストと値は、現在のコールバックスレッド内だけで使えます。別スレッドでは `WrongThread` の `VeroScriptException`、終了後は `InvalidOperationException` です。転送可能な独立した値だけをコールバックスレッドで複製してから他のスレッドへ渡してください。複製してもモジュール依存の制限は解除されません。

## 厳密数値 API

C#のCore bigint/rational結果は不透明VeroScriptValueのhost値です。VeroScriptCapabilities.Numericsはnative能力を示し、System.Numerics.BigInteger自動変換や公開managed数値serviceは追加しません。型付きscript関数または公開C ABIを使用します。

<a id="managed-call-lifetime"></a>

## マネージド呼び出しの寿命

ネイティブ操作はハンドルの供給元である値・パッケージのラッパーを、呼び出しと返されたネイティブビューのコピーが完了するまで保持します。`IReadOnlyList<VeroScriptValue>` のインデクサーが生成する値や、ブロックする channel 操作・visitor のレシーバーも含みます。保持するラッパーはスナップショットなので、コールバック中に引数配列の要素を交換してもネイティブの `ref`/`out` 書き戻し先は交換後の値になりません。この寿命保証は並行した明示破棄を許可せず、コールバックの借用期間も延長しません。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/values.md) · [fr-FR](../fr-FR/values.md) · [ja-JP](values.md) · [ko-KR](../ko-KR/values.md) · [zh-CN](../zh-CN/values.md) · [zh-TW](../zh-TW/values.md)
