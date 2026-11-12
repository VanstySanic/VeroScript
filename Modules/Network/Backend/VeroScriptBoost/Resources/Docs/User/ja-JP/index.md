# VeroScriptBoost

`vs-boost`

## バックエンドの選択と導入

Boost は DNS、TCP、UDP と、導入した SDK に含まれる HTTP サーバー／WebSocket 機能を提供します。`Boost::CreateNetworkService()` を `Network::FContext` に、`CreateHttpServerService()` を `Http::FServerOptions::Service` に、`CreateWebSocketService()` を `WebSocket::FOptions::Service` に渡します。サーバーと WebSocket には互換性のあるコンテキストが必要です。

スクリプトでは、導入済みの Network、HTTP サーバー、WebSocket サービスに `boost` を明示指定できます。選択と継承は [Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/ja-JP/index.md#backend-selection) に従います。`boost` はプロバイダー ID であり、固定の既定値ではありません。

## ネイティブ SDK の利用

```cpp
#include <VeroScriptBoost.h>
#include <VeroScriptNetwork.h>

VeroScript::Network::FContext context(VeroScript::Boost::CreateNetworkService());
```

```cmake
find_package(VeroScriptBoost 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Boost)
```

選択したパッケージに必要な正確な依存関係と原文ライセンスを導入します。共有 `VeroScriptBoostRuntime` は Boost が所有し、Windows では対応 DLL を実行ファイルの隣またはネイティブモジュール用の配置に置きます。Linux/macOS では導入された相対ローダーパスを使います。iOS は同じホストイメージで静的登録します。これらのネイティブバックエンドはブラウザー用通信を提供しません。

[Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/ja-JP/index.md) · [HTTP](../../../../../../VeroScriptHttp/Resources/Docs/User/ja-JP/index.md) · [WebSocket](../../../../../../VeroScriptWebSocket/Resources/Docs/User/ja-JP/index.md)

<a id="system-trust"></a>

クライアントで `bUseSystemTrust` を有効にすると、Boost は OpenSSL の既定パスに加え、Windows ROOT ストアまたは macOS のシステムアンカーを OpenSSL コンテキストへ追加します。macOS からはアンカー証明書を取り込み、検証は OpenSSL トランスポートが行います。明示的な追加ルートは `TrustPem` で渡し、それだけを使う場合はシステム信頼を無効にします。信頼ストアの利用可否と配布構成は実際の対象環境で確認してください。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
