# 파일과 원자적 출력

MakeFileUploadFactory 는 매 시도/rewind 마다 비동기 파일 소스를 새로 만들고 동기 stat/read 나 기한 재시작은 없습니다. Length 없으면 Offset 부터 연 파일 끝, 0 도 open/EOF 를 검사합니다. 취소는 소스를 닫고 replay 는 명시 허용합니다. set_upload_file 은 이전 본문/framing 을 지우고 multipart *_path 는 반복/바이너리를 보존합니다.

DownloadFile 은 같은 context 의 client/files 와 upload 없는 GET 만 받습니다. Range/If-Range/prefix 는 다운로드가 소유, identity 표현과 유한 스트림, 검증 후 commit 입니다. HTTP 총량/파일 MaxBytes 가 모두 적용되고 request 기한은 queue/network/prefix/commit 을 포함하며 파일 별도 기한은 무시합니다. 전체는 200/Content-Range 없음/완전 길이 검사입니다. resume 은 Offset 과 같은 prefix 길이 및 strong ETag 또는 IMF-fixdate 입니다. 206 은 validator/시작/끝/길이 일치, 200 거부, 416 은 총길이=Offset 와 validator 일치 때만 완료 prefix 를 commit 하고 오류 본문은 버립니다. 다른 상태/압축/범위는 target/prefix 보존 실패입니다.

Operation 은 종결, head 는 불변, 성공 Response/File 와 빈 Body, BodyBytes 는 네트워크량입니다. Progress 의 File/ReceivedBytes/RepresentationBytes/Resumed/AlreadyComplete 에서 File.Bytes 는 prefix 포함, ReceivedBytes 는 버린 416 본문 포함 이번 값입니다. 네트워크 EOF 는 commit 이 아니며 PhysicalComplete/Commit 을 검사합니다. 파일 범위/한도/기한/prefix/commit 와 다운로드 set_file_options/set_buffer_bytes/set_resume/clear_resume 를 쓰며 HostDriven 을 계속 진행합니다.

## 예제

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

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/files.md) · [fr-FR](../../fr-FR/Reference/files.md) · [ja-JP](../../ja-JP/Reference/files.md) · [ko-KR](files.md) · [zh-CN](../../zh-CN/Reference/files.md) · [zh-TW](../../zh-TW/Reference/files.md)
