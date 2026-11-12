# ファイルと原子的出力

MakeFileUploadFactory は attempt/rewind ごとに async file source を作り、同期 stat/read や deadline 再開始なし。Length なしは Offset から open 時末尾、0 でも open/EOF 検証します。cancel は source 終了、replay は明示許可。set_upload_file は旧 body/framing を除去し、multipart *_path は反復/binary 保持。

DownloadFile は同 context の client/files、upload なし GET のみ。Range/If-Range/prefix は download が管理、identity 表現、有界 pipe で検証後 commit します。HTTPbody 総量/fileMaxBytes 全部有効、request 期限が queue/network/prefix/commit を覆い file 別期限は無視します。全量は 200/Content-Range なし/長完全一致。resume は同長 prefix と Offset、strongETag または IMF-fixdate。206 は validator/開始/末尾/長一致、200 は拒否、416 は総長=Offset かつ validator 同じ時だけ既完 prefix を commit、errorbody は排出して追記しません。他 status/compress/range 不一致は target/prefix 保持で失敗。

Operation 終端、head 不変、成功 Response/File と空 Body、BodyBytes は network 量。Progress は File/ReceivedBytes/RepresentationBytes/Resumed/AlreadyComplete、File.Bytes は prefix 込み、ReceivedBytes は 416 廃棄 body も含む今回量。network EOF は commit ではなく PhysicalComplete/Commit を検査します。scriptfile range/limit/time/prefix/commit、download set_file_options/set_buffer_bytes/set_resume/clear_resume を使い、HostDriven も継続駆動します。

## 例

```VeroScript
Network::file_executor files = Network::create_file_executor(context);
Network::file_read_options input = Network::make_file_read_options("payload.bin", absolute_base);
input.set_range(0, -1);
http::request request = http::create_request("POST", url);
request.set_upload_file(files, input);

http::download_options output = http::make_download_options("received.bin", absolute_base);
http::download_operation task = client.download_file(files, get_request, output);
http::operation operation = task.operation();
```

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/files.md) · [fr-FR](../../fr-FR/Reference/files.md) · [ja-JP](files.md) · [ko-KR](../../ko-KR/Reference/files.md) · [zh-CN](../../zh-CN/Reference/files.md) · [zh-TW](../../zh-TW/Reference/files.md)
