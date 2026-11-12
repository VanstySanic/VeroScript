# 診断とローカライズ

処理分岐には FError::Code/Id()、error.message_id() を使い、表示文を protocol にしません。Resources/Localization/<locale>/json.json が6言語を提供します。分類、path 表示、binding error は翻訳し、backend／ユーザー converter の詳細は原文です。

FScriptError.MessageId/MessageArgs を host diagnostic service に渡せます。通常 JSON error の引数は原詳細と instance JSON Pointer（根は空）の2つ、json.mapping.declaration は詳細1つです。language／catalog 欠落時は基準英語へ fallback し、分類／path を変更しません。表示 metadata の割り当て失敗で元 error を上書きしません。

例外を送出しない `json::error` も、同じ引数規則の `message_id`、`message`、`instance_pointer` を提供します。独立した C++ SDK は Core をリンクせず、グローバルな言語状態も持ちません。インストールした `share/VeroScriptJson/Resources/Localization` をホストのリソースリゾルバーへ登録し、他モジュールのカタログと統合する場合も `json.json` を保持してください。

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/diagnostics.md) · [fr-FR](../../fr-FR/Reference/diagnostics.md) · [ja-JP](diagnostics.md) · [ko-KR](../../ko-KR/Reference/diagnostics.md) · [zh-CN](../../zh-CN/Reference/diagnostics.md) · [zh-TW](../../zh-TW/Reference/diagnostics.md)
