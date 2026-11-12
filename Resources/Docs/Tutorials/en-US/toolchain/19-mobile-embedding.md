# 19: Run a script in a mobile application

Use a matching mobile-enabled SDK in an Android/iOS application. The application must already host target CPython or use .NET 10 MAUI; desktop packages do not supply mobile native binaries. These integrations are experimental.

[Python](../../../../../Extensions/Language/Python/Resources/Docs/User/en-US/mobile.md) · [CSharp](../../../../../Extensions/Language/CSharp/Resources/Docs/User/en-US/mobile.md)

1. Follow the selected binding’s guide to deploy native libraries and install standard-library/localization resources. Complete resource installation before creating a Session.

2. On one worker thread, load catalogs, select the language, create/configure a VM Session, load `import "core.vs"; void main() { print(42); }`, check both load and run results, collect the print callback and close the Session. Return the collected `42` to the UI thread.

3. Retry with `void main() { int x = ; }` and a non-English language. Show the diagnostics and confirm that the UI stays responsive. Restore the valid script, enforce execution limits, and validate the final signed application on each supported target.

[en-US](19-mobile-embedding.md) · [fr-FR](../../fr-FR/toolchain/19-mobile-embedding.md) · [ja-JP](../../ja-JP/toolchain/19-mobile-embedding.md) · [ko-KR](../../ko-KR/toolchain/19-mobile-embedding.md) · [zh-CN](../../zh-CN/toolchain/19-mobile-embedding.md) · [zh-TW](../../zh-TW/toolchain/19-mobile-embedding.md)

<!-- docs-navigation -->

---

[Tutorials](../index.md)

Language: [en-US](19-mobile-embedding.md) · [fr-FR](../../fr-FR/toolchain/19-mobile-embedding.md) · [ja-JP](../../ja-JP/toolchain/19-mobile-embedding.md) · [ko-KR](../../ko-KR/toolchain/19-mobile-embedding.md) · [zh-CN](../../zh-CN/toolchain/19-mobile-embedding.md) · [zh-TW](../../zh-TW/toolchain/19-mobile-embedding.md)
