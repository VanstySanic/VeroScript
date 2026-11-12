# レジストリプロトコル

レジストリーのアーカイブ検証は ZIP ルートの `veroscript-package.json` を読みます。プロジェクト設定による代用はできません。API パス、JSON 形式識別子、形式バージョン `0.1.1` は共通です。

クライアント契約は 0.1.1。設定済み API、HTTPS、UTF-8 JSON、一致する sourceId を使います。サーバー実装向けの契約で、ホスト済みサービスを意味しません。

公式 CLI の API ベースは `https://veroscript.org/api`、論理 `sourceId` は `veroscript.official` です。以下のルートをこのベースに追加します。プロジェクト／ロックファイルのソースと明示的な API・ミラー指定は既存の選択規則に従い、埋め込みホストは独自にソースを設定します。

| メソッド | 経路 | 契約 |
| --- | --- | --- |
| GET | `/search?q=...` | `sourceId + search JSON` |
| GET | `/packages/{name}` | `veroscript-registry-package` |
| GET | `/packages/{name}/versions/{version}` | `veroscript-registry-version` |
| GET | `/artifacts/{id}/download` | `ZIP` |
| POST | `/uploads` | `veroscript-upload` |
| PUT | `{upload.url}` | `ZIP` |
| POST | `/packages/{name}/versions` | `mode=append + sourceId/name/version/dependencies/conflicts/artifacts/idempotencyKey` |

veroscript-registry-version は共通 dependencies/conflicts と artifacts を持ち、各項目は id/size/sha256/manifestSha256/target/requires、任意 blocked を持ちます。veroscript-registry-package は formatVersion/sourceId/name/versions と任意 withdrawn/blocked 状態。veroscript-upload 応答は uploadId/method=PUT/url、任意 headers/expiresAt です。 配布物 ID は空でなく、ASCII 英数字と `.`, `_`, `-` のみを使い、`.` と `..` は禁止です。配布物は ZIP アーカイブで、`kind` フィールドはありません。

一つの portable か、排他的な対象別配布物だけを使い、portable/native の曖昧な重複は不可です。正規名、版、依存、競合は同一です。sha256 は ZIP 全体、manifestSha256 は空白を含む元のルート veroscript-package.json のバイトを対象とします。.vspkg と .zip は同じ契約です。

## 同じバージョンへのプラットフォーム追加

初回公開と後日の追加には同じ `publish` を使い、新しい `.vspkg`／`.zip` だけを渡します。既存と完全に同じ配布物は再アップロードしません。名前、バージョン、共通依存関係と競合、および既存の ID、バイト列、ハッシュ、サイズ、対象と要件を保持します。portable はすべての対象と重複します。OS、アーキテクチャ、environment、coreBuild、共通 ABI キーの異なる等値条件で排他性を示せますが、最低バージョンや必要機能の違いだけでは示せません。

バックエンド側で必要な対応：

1. `POST /packages/{name}/versions` を維持し、`mode: "append"` を必須にします。存在しない版を作成し、既存の版にはアップロード参照を原子的に追加します。リストは置換ではなく差分です。[公開要求スキーマ](../../../Schemas/veroscript-publication.schema.json)に従います。
2. 所有権、アップロード内容／マニフェストと共通メタデータを検証します。既存の ID と不変フィールドを保持し、完全一致なら同じ ID を再利用します。ブロック済みを含む全既存配布物とバッチ内の対象を比較し、変更・削除・重複を競合として拒否します。ブロック／撤回済み版への追加も拒否し、競合時はバッチ全体を失敗させます。
3. 同じトランザクションで検査と挿入を行います。排他的な並行追加は両方成功できます。クライアントの事前検査はサーバーの検査を代替しません。
4. 冪等キーと正規化要求を保存し、同一キー・要求には元の結果を返し、変更は拒否します。アップロード割り当ても同様です。完了参照を保持して再試行し、署名 URL や認証ヘッダーの保存をクライアントに要求しません。 同じ uploadId を維持して期限切れ署名 URL を再発行でき、同一バイト列の PUT 再送は安全でなければなりません。
5. 既存・新規すべての配布物と安定 ID を含む完全な版を返します。クライアントは観測済みの識別情報と全要求を照合し、有効で重複しない並行追加を許可します。
6. オリジンの版 GET は本当に存在しない場合だけ 404 を返し、コミット後は最新の完全な版を返します。事前検査と復旧は `Cache-Control: no-cache` でオリジンへ直接接続し、ミラーとインストールキャッシュを迂回します。応答喪失は完全に一致する GET で確認し、未確認なら参照と元の要求・キーで再試行します。
7. ETag と CDN／ミラーのメタデータを更新し、既存ダウンロードの識別情報・バイト列を保持します。対象別ロックは有効なままです。撤回／ブロックは別の状態操作であり、置換を許可しません。

プロトコルは `0.1.1` のままで、コア・Interop・JIT API は変更しません。実際の公開前にバックエンド側の対応が必要です。

エラーは code/message/requestId と任意詳細です。GET は ETag/If-None-Match と有限再試行を使い、送信/コミット POST を盲目的に再試行しません。認証は正確なホスト/パスに限定し、復旧情報をソース/API/配布物集合で分離します。認可、期限、検証、索引、保存、撤回/拒否、ミラー同期、監査はサーバーの責務です。

[スキーマ](../../../Schemas) · [保守](index.md)

組み込み HTTP 転送は、リダイレクトを含む各リクエストの正規化済み URL に対して認証情報を再検証します。独自の `IHttpTransport` は各リクエストで `FHttpRequest.Credential` を適用し、空の結果なら Authorization を送信しません。既存のオリジン／パス制限も維持します。レジストリが指定するアップロード署名はレジストリの認証情報とは別に扱います。同じパスのクエリパラメーターは認証範囲に含まれます。

パーセントデコード後のパスに独立した `.` または `..` セグメントが現れる場合、エンコードされた区切り文字も含め、リゾルバーの認証情報は送信しません。通常のエンコード済みファイル名と UTF-8 パスは引き続き利用できます。

公式レジストリは `vs-` で始まるパッケージ名を、作成と後続の公開を含め、承認された公式発行者用に予約する必要があります。AsmJit の正規パッケージ名は `vs-asmjit`、サービスのバックエンド ID は `asmjit` です。所有権と公開権限をサーバーで検証します。クライアントは汎用の名前検証を維持し、ローカルまたはプライベートソースの接頭辞だけで公式と判断しません。

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/Developer/ja-JP/index.md) · [en-US](../en-US/registry-protocol.md) · [fr-FR](../fr-FR/registry-protocol.md) · [ja-JP](registry-protocol.md) · [ko-KR](../ko-KR/registry-protocol.md) · [zh-CN](../zh-CN/registry-protocol.md) · [zh-TW](../zh-TW/registry-protocol.md)
