# Files and atomic output

MakeFileUploadFactory(files,readOptions) creates a fresh async source per attempt/redirect without synchronous stat/read or deadline restart. Unspecified Length reads Offset to the opened file end; Length=0 still opens/verifies EOF, so a missing file cannot be ignored. Cancel ends the active source. Authorize retries/replay explicitly. Script set_upload_file clears prior body sources/framing; multipart add_field_path/add_file_path preserves binary/repeated fields.

DownloadFile(context,client,files,request,options) requires the same context and GET without upload body. Download owns Range/If-Range and PrefixPath/PrefixLength. It requests identity encoding, uses bounded streams, and commits only verified content. Request body/total-body and file MaxBytes all apply. The request deadline covers queue/network/prefix-copy/commit; File.Operation’s independent deadline is ignored.

Full download requires 200 without Content-Range and verified length/completeness. Resume requires PartialPath/Offset, exact prefix length, strong ETag or valid IMF-fixdate Last-Modified. A 206 must match validator and Content-Range offset through representation end, with consistent optional Content-Length. A 200 resume fails instead of appending a full body. A 416 commits an already complete prefix only when total length equals Offset and validator matches; drain/discard its error body. Other status/compression/range mismatch fails, removing temporary output by default while preserving target/prefix.

Operation is terminal state; ResponseHead remains an immutable snapshot. Success contains Response and File, empty Response.Body, and BodyBytes for network response bytes. Progress includes File/ReceivedBytes/optional RepresentationBytes/Resumed/AlreadyComplete. File.Bytes includes copied prefix, ReceivedBytes only current response (including discarded 416 body). Network EOF does not prove commit; inspect physical completion/commit from Network files. Script download_options supports set_file_options/set_buffer_bytes/set_resume/clear_resume; file options set ranges/limits/time/expected length/prefix/commit. Accessors are explicit calls and HostDriven still needs polling.

## Examples

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

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](files.md) · [fr-FR](../../fr-FR/Reference/files.md) · [ja-JP](../../ja-JP/Reference/files.md) · [ko-KR](../../ko-KR/Reference/files.md) · [zh-CN](../../zh-CN/Reference/files.md) · [zh-TW](../../zh-TW/Reference/files.md)
