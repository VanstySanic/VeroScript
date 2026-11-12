# VeroScriptCurl

`vs-curl`

## バックエンドの選択と導入

Curl は HTTP クライアント、接続プール、Cookie、リダイレクト、再試行、ストリーミングを提供します。`Curl::CreateHttpClientService()` を `Http::FClient` または `Http::FSseClientOptions::Service` に渡します。コンテキストにはソケットを所有しない監視機能とタイマーが必要で、Boost が対応しています。Curl が実行基盤を自動選択・導入することはありません。HTTPS はサーバー TLS と独立した curl の TLS 実装を使用します。

スクリプトでは HTTP クライアントや SSE に `curl` を明示指定し、互換性のある Network コンテキストを別に選びます。選択と継承は [Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/ja-JP/index.md#backend-selection) に従います。`curl` はプロバイダー ID であり、固定の既定値ではありません。

## ネイティブ SDK の利用

```cpp
#include <VeroScriptBoost.h>
#include <VeroScriptCurl.h>
#include <VeroScriptHttp.h>

VeroScript::Network::FContext context(VeroScript::Boost::CreateNetworkService());
VeroScript::Http::FClient client(context, VeroScript::Curl::CreateHttpClientService());
```

```cmake
find_package(VeroScriptBoost 0.1.1 EXACT CONFIG REQUIRED)
find_package(VeroScriptCurl 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Boost VeroScript::Curl)
```

選択した SDK と実行基盤に必要な正確な依存物と原本の告知を導入してください。上の例の Boost 実行基盤を使う場合は、その [配備ガイド](../../../../../VeroScriptBoost/Resources/Docs/User/ja-JP/index.md) に従います。これらのネイティブプロバイダーはブラウザ向け通信を提供しません。

[Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/ja-JP/index.md) · [HTTP](../../../../../../VeroScriptHttp/Resources/Docs/User/ja-JP/index.md) · [WebSocket](../../../../../../VeroScriptWebSocket/Resources/Docs/User/ja-JP/index.md)

<a id="system-trust"></a>

同梱の macOS curl は OpenSSL と Apple SecTrust を有効にします。明示的な `CaFile` がなければネイティブ信頼連携を使い、CA ファイルを指定するとそのリクエストの信頼データを使います。同梱 Windows curl は Schannel、他の同梱ネイティブ対象は設定済み OpenSSL 信頼パスを使います。システム libcurl を使う SDK はそのライブラリの TLS／信頼設定に従います。Boost のサーバー／WSS TLS とは独立です。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
