# Diagnostics and localization

Use FError::Code/Id() and script error.message_id() for program logic; raw message text is not a stable protocol. Six locales are supplied in Resources/Localization/<locale>/json.json. Categories/path presentation and binding errors are translated; backend and user-converter details stay in their original form.

FScriptError.MessageId/MessageArgs can be rendered by the host diagnostic service. Ordinary JSON errors take two arguments: raw detail and instance JSON Pointer (empty at root). json.mapping.declaration takes one detail argument. Missing catalogs/languages fall back to authoritative English without changing category/path. Failure to allocate display metadata preserves the original error.

Nonthrowing `json::error` exposes `message_id`, `message` and `instance_pointer` with the same argument rules. The independent C++ SDK neither links Core nor owns global language state. Mount the installed `share/VeroScriptJson/Resources/Localization` directory in the host resource resolver; preserve `json.json` when combining it with other modules’ catalogs.

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](diagnostics.md) · [fr-FR](../../fr-FR/Reference/diagnostics.md) · [ja-JP](../../ja-JP/Reference/diagnostics.md) · [ko-KR](../../ko-KR/Reference/diagnostics.md) · [zh-CN](../../zh-CN/Reference/diagnostics.md) · [zh-TW](../../zh-TW/Reference/diagnostics.md)
