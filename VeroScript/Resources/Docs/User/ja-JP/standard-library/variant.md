# variant — 動的な値ボックス

variant は import 不要の Core 型です。std::variant と variant は同じ型です。正規化された元の型と値を一つ保持するか、空の状態になります。

variant が代入先と確定している場合は暗黙にボックス化できます。読み取りには cast<T> または try_cast<T> が必要です。variant_is<T> は変換せず正確な型を判定します。`optional<T>` と `try_cast` も組み込みで、標準ライブラリの import は不要です。空のボックスからの `cast<T>` は失敗し、空のボックスや通常の変換不一致に対する `try_cast<T>` は空の optional を返します。複製失敗、キャンセル、制限超過はエラーです。

コピーは不変ボックスを共有します。スクリプトの struct は格納時と抽出時に値を複製し、class とコンテナは既存の参照意味論を保ちます。基底型や型付き null を含む静的な宣言型を保持します。型付き null は値ありで空と異なります。裸の nullptr は先に具体的な型の変数に格納してください。

variant 同士の等値比較のみ定義し、正規型と既存の値の等値条件が一致する必要があります。動的演算、メンバー検索、呼び出し、暗黙の取り出し、順序やハッシュはありません。variant キー、channel 転送、variant を含む worker の引数・結果・キャプチャ・グローバルは拒否します。worker 内部のローカル値は利用でき、array/map の値にも格納できます。

```vero
import "core.vs";
variant empty;
variant answer = 42;
assert(!variant_has_value(empty));
assert(variant_is<int>(answer));
int exact = cast<int>(answer);
optional<double> converted = try_cast<double>(answer);
map<string, variant> properties = map<string, variant>();
properties["width"] = answer;
properties["text"] = "Save";
assert(cast<int>(properties["width"]) == exact);
```

VM は全意味論を実装します。Mixed/asmjit は未対応の命令を安全に VM で実行します。可搬バイトコードとパッケージは宣言と命令を保持します。独立 C++・C#・Python AOT は出力生成前に variant 依存を明確に拒否します。デバッガーは読み取り専用の $present、$type、$value を表示し、type<variant>().kind() は "variant" を返します。

## SDK

[相互運用 SDK — variant](../../../../../../VeroScriptInterop/Resources/Docs/User/ja-JP/index.md#exported-value-lifetime) を参照してください。

公開オブジェクトがホスト呼び出し後も生存し、メソッドやデストラクターがスクリプトのグローバル状態を使う場合は、既存の永続セッションモードを使用してください。C++ は SetHotReloadEnabled(true)、C# は HotReloadEnabled、Python は hot_reload_enabled=True です。通常の一時実行は呼び出し終了時にグローバル状態を解放します。

```text
C:      vs_interop_value_set_variant_empty / vs_interop_value_set_variant
        vs_interop_variant_has_value / vs_interop_variant_get_type_name / vs_interop_variant_get
C#:     value.SetVariantEmpty(); session.CreateVariant(typeName, payload);
        value.VariantHasValue; value.VariantTypeName; value.GetVariantValue();
Python: Value.empty_variant(); session.variant(type_name, payload_value);
        value.set_variant_empty(); value.assign_variant(other_box);
        value.variant_has_value(); value.variant_type_name(); value.variant_get();
C++:    VeroScriptVariant.h, VeroScriptValueRoot.h
        session.MakeVariant(typeName, payload, output, error);
        session.RetainValue(output, root, error); root.Read(); root.Reset();
```

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/standard-library/variant.md) · [fr-FR](../../fr-FR/standard-library/variant.md) · [ja-JP](variant.md) · [ko-KR](../../ko-KR/standard-library/variant.md) · [zh-CN](../../zh-CN/standard-library/variant.md) · [zh-TW](../../zh-TW/standard-library/variant.md)
