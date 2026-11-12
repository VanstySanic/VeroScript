# 유한 바이트 스트림

VeroScriptNetworkStream.h 는 소유한 유한 링과 읽기/쓰기 끝점을 제공합니다. VM 을 빌리지 않고 복사는 논리 생산자/소비자당 커서를 공유합니다. context 포함 TryCreateBytePipe 는 전체 용량을 계산하고 없으면 네이티브 호출자가 총량을 관리합니다. 수량은 검사된 int64, 용량은 size_t 도 만족합니다.

Progress=0(비어 있지 않은 요청의 0 바이트 진행 불가), WouldBlock=1(소비 없음, 양보), End=2(생산 종료/버퍼 비움), Failed=3 입니다. 빈 요청은 EOF 조회가 아닙니다. Write 는 일부 수락, WriteAll 은 전부 또는 없음, 전체 용량 초과는 즉시 실패합니다. Finish 는 정상 종료, Fail 은 기존 데이터를 읽은 뒤 오류입니다. reader Cancel/마지막 해제는 큐를 버리고 알리며 미완료 writer 마지막 해제는 Closed 입니다. Owner 는 양 끝과 저장소까지 유지합니다.

create_byte_pipe 는 1 바이트–16 MiB, read/write 는 호출당 최대 64 KiB 이며 NUL 을 보존합니다. 빈 문자열 대신 state/error_code/error 로 EOF 를 판단합니다. buffered/capacity 는 int64, writer.finish/fail 과 reader.close 로 종료합니다. WouldBlock 에서는 스케줄러에 양보하고 HostDriven 소유 스레드에서 poll 합니다.

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/streaming.md) · [fr-FR](../../fr-FR/Reference/streaming.md) · [ja-JP](../../ja-JP/Reference/streaming.md) · [ko-KR](streaming.md) · [zh-CN](../../zh-CN/Reference/streaming.md) · [zh-TW](../../zh-TW/Reference/streaming.md)
