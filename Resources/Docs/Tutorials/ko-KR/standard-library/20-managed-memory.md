# 20: 이진 패킷 읽기와 복사

4바이트 값을 쓰고 읽기 전용 뷰로 확인한 뒤 원래 버퍼를 늘리는 동안 독립 스냅샷을 유지합니다. 표준 라이브러리가 포함된 Runtime을 설치하고 VM 또는 Mixed를 선택하세요. 독립 소스 AOT는 지원하지 않습니다.

## 1. 프로그램 저장

다음을 `memory-packet.vs`로 저장합니다. 0인 8바이트를 할당하고 오프셋 1에서 big-endian으로 쓰면 4바이트 헤더의 첫 바이트는 1입니다.

```VeroScript
import "memory.vs";

int main()
{
	auto packet = new std::memory::buffer(8);
	std::memory::write<uint32>(packet, 1, 0x01020304u, std::memory::byte_order::big);
	auto header = packet.readonly_slice(1, 4);
	print(header.get(0));
	assert(std::memory::read<uint32>(header, 0, std::memory::byte_order::big) == 0x01020304u);
	auto snapshot = std::memory::clone(packet);
	packet.resize(16);
	print(snapshot.size());
	print(packet.size());
	return 0;
}
```

## 2. 검사와 실행

```powershell
veroscript --backend=vm --check memory-packet.vs
veroscript --backend=vm memory-packet.vs
```

```text
1
8
16
```

출력은 차례대로 1, 8, 16입니다. 뷰는 바이트를 공유하고 clone은 복사본을 소유합니다. 16바이트로 늘리면 `header`가 만료되어 더 읽지 않습니다. 스냅샷은 8바이트를 유지합니다.

## 3. 직접 변경

읽기와 쓰기를 모두 little로 바꾸면 첫 바이트가 4가 됩니다. clone을 단순 대입으로 바꾸면 같은 버퍼를 공유하여 두 크기가 모두 16이 됩니다. 마지막으로 resize 후 새 읽기 전용 뷰를 만들고 바이트를 확인하세요. 만료된 뷰는 재사용하지 마세요.

## 4. 연산 선택

임시 유계 접근은 뷰, 복사는 `clone` / `to_array`, 복원은 `from_array`를 사용합니다. 주소나 ID를 영구 저장하지 마세요. 호스트 버퍼와 대여는 해당 SDK 가이드를 참고하세요.

[참조](../../../../../VeroScript/Resources/Docs/User/ko-KR/standard-library/memory.md)

<!-- docs-navigation -->

---

[튜토리얼](../index.md)

언어: [en-US](../../en-US/standard-library/20-managed-memory.md) · [fr-FR](../../fr-FR/standard-library/20-managed-memory.md) · [ja-JP](../../ja-JP/standard-library/20-managed-memory.md) · [ko-KR](20-managed-memory.md) · [zh-CN](../../zh-CN/standard-library/20-managed-memory.md) · [zh-TW](../../zh-TW/standard-library/20-managed-memory.md)
