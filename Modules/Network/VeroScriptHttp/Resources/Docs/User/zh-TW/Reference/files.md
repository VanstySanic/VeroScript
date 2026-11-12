# 檔案與原子輸出

MakeFileUploadFactory 每嘗試/rewind 新建非同步檔案來源，不同步 stat/read、不重啟期限；未指定 Length 從 Offset 至開啟時末尾，0 也驗開啟/EOF，取消結束來源、重放明確授權。set_upload_file 清舊正文/framing，multipart *_path 保重複/二進位。

DownloadFile 要求 client/files 同 context、無上傳 GET，自管 Range/If-Range/prefix，identity 表示、有界流、驗證後 commit。HTTP 正文總限及檔 MaxBytes 都有效，request 期限覆蓋排隊/網路/複製/提交，忽略檔獨立期限。全量200/無Content-Range/長完整；續傳須相同 Offset 長度的 prefix 與 strong ETag 或 IMF-fixdate。206 要 validator/起點/末尾/長一致；200 拒絕；416 僅總長=Offset且validator符合才提交既完 prefix，排空不追加錯誤正文。其他狀態/壓縮/範圍錯均保 target/prefix 失敗。

Operation 終態、頭不變，成功 Response/File 與空 Body，BodyBytes 為網路量。Progress 含 File/ReceivedBytes/RepresentationBytes/Resumed/AlreadyComplete；File.Bytes 含 prefix、ReceivedBytes 含本次416丟棄正文。網路 EOF 非 commit，查 PhysicalComplete/Commit。腳本檔範圍/限額/期限/prefix/提交及 download set_file_options/set_buffer_bytes/set_resume/clear_resume，HostDriven 仍須持續推進。

## 範例

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

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/files.md) · [fr-FR](../../fr-FR/Reference/files.md) · [ja-JP](../../ja-JP/Reference/files.md) · [ko-KR](../../ko-KR/Reference/files.md) · [zh-CN](../../zh-CN/Reference/files.md) · [zh-TW](files.md)
