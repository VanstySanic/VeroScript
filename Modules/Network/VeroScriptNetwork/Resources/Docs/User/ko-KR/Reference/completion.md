# 완료 알림과 프로그램 수명

스크립트의 값 캡처는 Core의 읽기 전용 바인딩 규칙을 따릅니다. 캡처한 호스트 값을 변경하는 네이티브 메서드를 호출하려면 `mutable` 람다 또는 쓰기 가능한 바인딩의 참조 캡처를 사용합니다. 초기화 캡처와 로컬 static은 일반 Core 의미 규칙을 따릅니다. 로컬 static에 저장한 context도 프로그램 세대에 속하며 해당 세대가 끝나면 닫힙니다.

세 프로토콜 operation 은 delegate/lambda/바인딩 메서드로 on_complete(fn(operation)->void) 하나를 등록합니다. Core 세대 루트가 함수/캡처를 유지하며 완료 뒤 등록도 즉시 호출하지 않고 Dispatch 를 기다립니다. 콜백의 operation 은 독립 소유하여 보관할 수 있습니다.

poll 은 HostDriven I/O 만 진행하고 두 모드 모두 생성 스크립트 스레드의 dispatch(max_callbacks)가 필요합니다. 0 은 실행 없음, 최대 1,048,576 이며 완료 기록 처리 수를 반환합니다. 종결만으로 큐 등록을 가정하지 않습니다. 새 기록은 다음 배치, 같은 context 의 재귀는 불가입니다. 인수/할당/네이티브를 포함한 콜백 실패는 스크립트 호출을 실패시키고 네트워크 결과는 유지하며 재실행 없이 나머지 배치 콜백을 건너뜁니다. Dispatch 상태는 복구되고 VM 예산/기한/취소는 협력적이므로 임의의 차단 C++을 강제 중단하지 않습니다.

I/O 는 빌린 VM/ScriptInvoker/Args/ref 를 유지하지 않습니다. 파견 중 현재 서비스만 빌리고 다른 스레드 정리는 소유자 안전 지점으로 루트를 퇴역합니다. 영속 Session 함수 반환은 같은 세대를 보존합니다. 원시 VM 최상위 종료, Session close, 교체/reload 진입은 루트/context 무효화와 취소입니다. 교체 전 컴파일 실패는 이전 상태 보존, 마이그레이션 롤백은 네트워크를 부활시키지 않으므로 context 를 다시 만듭니다. 외부 네이티브 참조도 콜백을 부활시키지 않으며 퇴역 뒤 등록/파견은 세대 오류입니다.

콜백 안의 shutdown 은 자신을 기다리지 않고 큐를 버리며 보유 결과는 유지하고 캡처 순환을 해제합니다. 보통 호스트 루트는 교체 전에 해제, 내부 완료 루트는 능동 무효화합니다. 영속 초기화 실패는 임시 프레임/전역 소멸 전에 루트를 철회합니다. 정리 오류는 표시되며 정리/소멸 중 새 async 루트 등록은 불가합니다. 다른 호스트/AOT 는 별도 어댑터가 필요합니다.

## 예제

```VeroScript
class CompletionFlag
{
public:
	bool done = false;
};
Network::context context = Network::create_context(false);
CompletionFlag flag = CompletionFlag();
Network::operation operation = Network::resolve(context, "localhost", 80, 5000);
operation.on_complete([flag](Network::operation completed) mutable -> void
{
	assert(completed.done());
	print(completed.message_id());
	flag.done = true;
});
for (int round = 0; round < 10000 && !flag.done; ++round)
{
	context.dispatch(64);
	if (!flag.done)
	{
		std::this_thread::sleep(1);
	}
}
assert(flag.done);
context.shutdown();
```

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/completion.md) · [fr-FR](../../fr-FR/Reference/completion.md) · [ja-JP](../../ja-JP/Reference/completion.md) · [ko-KR](completion.md) · [zh-CN](../../zh-CN/Reference/completion.md) · [zh-TW](../../zh-TW/Reference/completion.md)
