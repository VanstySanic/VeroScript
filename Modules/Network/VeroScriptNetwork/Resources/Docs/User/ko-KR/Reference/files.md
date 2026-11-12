# 파일과 원자적 출력

FFileExecutor 는 context 당 공유 풀 하나이며 후속 실행기 옵션은 같아야 합니다. 기본 2 worker/32 task/64 KiB block/2ms 검사, 범위 1–8/1–4096, block 은 context 한도와 16 MiB 이하입니다. 초과 QueueFull, 스트림 대기는 worker 를 차지하지 않습니다.

UTF-8 경로의 상대값은 명시적 절대 BaseDirectory 가 필요하며 cwd/부모 생성 없이 일반 파일만 처리합니다. 열 때 길이를 정하고 나중 축소는 Read 이며 동시 내용 스냅샷은 아닙니다. Read.Body 를 진행 중 비운 뒤 기다립니다. Write 는 미소비 단일 reader 를 인수하고 모든 결과에서 닫으며 재사용/취소 소스는 State 입니다. Offset/Length/MaxBytes, ExpectedLength/MaxBytes 는 네이티브 uint64 와 signed offset 을 검사하고 스크립트는 비음수 int64, -1 은 미지 길이/합계 전용입니다.

옆의 배타적 임시 파일, 기본 flush 후 commit, ReplaceExisting 일 때만 덮어씁니다. 가시성 전환은 원자적이지만 정전 후 디렉터리 내구성은 보장하지 않습니다. PrefixPath/Length 는 정확한 길이의 원본을 제한 블록으로 복사하며 같은 target 은 교체 허용 때만 가능합니다. ExpectedLength 는 prefix 포함, 길이 불일치 Protocol, 상한 Limit 이며 commit 전 결과/할당량을 준비합니다.

미 commit 임시는 기본 삭제, KeepPartial 이면 PartialPath 입니다. 취소는 I/O/rename 과 경합하고 commit 을 되돌리지 않습니다. Progress 는 Bytes/TotalBytes/Path/PartialPath/CleanupSystemCode/PhysicalComplete 와 Commit 0 Uncommitted/1 Committing/2 Committed/3 Failed 입니다. PhysicalComplete 뒤 상태/오류를 조합합니다. Close 는 자신의 작업만, Shutdown 은 모든 worker join 으로 비중단 시스템 호출을 기다릴 수 있습니다. 파일 스레드는 스크립트를 실행하지 않고 OS 캐시/호출자 복사는 할당량 밖입니다. 스크립트 executor[_with_limits]와 read/write 옵션의 범위/한도/기한/기대 길이/prefix/commit 을 쓰며 HostDriven 과 본문 소비를 계속합니다.

## 기본값

| API | 값 |
| --- | --- |
| `FFileReadOptions::MaxBytes` | `256 MiB` |
| `FFileWriteOptions::MaxBytes` | `256 MiB` |
| `bReplaceExisting / bKeepPartial / bFlushToDisk` | `false / false / true` |

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/files.md) · [fr-FR](../../fr-FR/Reference/files.md) · [ja-JP](../../ja-JP/Reference/files.md) · [ko-KR](files.md) · [zh-CN](../../zh-CN/Reference/files.md) · [zh-TW](../../zh-TW/Reference/files.md)
