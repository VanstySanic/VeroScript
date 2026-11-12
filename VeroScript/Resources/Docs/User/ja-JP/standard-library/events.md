# イベントモジュール

モジュールファイル: `delegate.vs`

次のようにインポートします。

```VeroScript
import "delegate.vs";
```

公開スクリプトでは `std::delegate` と `std::multicast_delegate` を使用します。非修飾名も同じ型を表し、どちらも `delegate.vs` のインポートが必要です。メンバー名はすべて小文字です。

## ユニキャストデリゲート

`delegate` には、最大 3 つの引数を持つ 1 つのハンドラーが格納されます。最初の型パラメータは戻り値の型で、その後に呼び出し引数の型が続きます。

```VeroScript
std::delegate<int, int> mitigate = std::delegate<int, int>();
mitigate.bind(function(int value) -> int => value / 2);
int result = mitigate.execute(10);
```

このモジュールは、`delegate<R>`、`delegate<R, A>`、`delegate<R, A, B>`、および `delegate<R, A, B, C>` を提供します。メンバーには以下が含まれます:

|メンバー |説明 |
| --- | --- |
| `bind(handler)` |ハンドラーをバインドまたは置換します。 |
| `unbind()` |現在のハンドラーを削除します。 |
| `is_bound()` |ハンドラーがバインドされているかどうかをテストします。 |
| `execute(...)` |ハンドラーを実行します。アンバインド時にアサートします。 |
| `execute_if_bound(...)` |バインドされた場合にのみ実行され、実行されたかどうかを返します。 |

`execute_if_bound` は、実行が行われたかどうかを報告します。ハンドラーの結果は返されません。

`delegate` のハンドラースロットは非公開です。`unbind()` はデリゲートが保存したハンドラー参照を空にします。状態は `is_bound()` で確認します。

## マルチキャストデリゲート

`multicast_delegate` は、`void` ハンドラーのリストを保存し、0 ～ 3 つの引数をサポートします。

```VeroScript
std::multicast_delegate<int> on_score = std::multicast_delegate<int>();
on_score.add(function(int value) -> void => print(value));
on_score.broadcast(42);
```

メンバーには以下が含まれます:

|メンバー |説明 |
| --- | --- |
| `add(handler)` |ハンドラーを追加します。 |
| `remove(handler)` |一致するハンドラーを削除します。 |
| `broadcast(...)` |すべてのハンドラーを登録順に呼び出します。 |
| `count()` | ハンドラー数を非負の `int64` として返します。 |
| `clear()` |すべてのハンドラーを削除します。 |

ブロードキャストでは最初にハンドラ リストのスナップショットが取得されるため、ブロードキャスト中に変更が加えられても、そのブロードキャストのコール セットは変更されません。

`multicast_delegate` の `handlers` フィールドは、ソース モジュールで公開されています。登録順序とスナップショットのセマンティクスを保持する必要がある場合は、`add`、`remove`、および `clear` を優先します。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/standard-library/events.md) · [fr-FR](../../fr-FR/standard-library/events.md) · [ja-JP](events.md) · [ko-KR](../../ko-KR/standard-library/events.md) · [zh-CN](../../zh-CN/standard-library/events.md) · [zh-TW](../../zh-TW/standard-library/events.md)
