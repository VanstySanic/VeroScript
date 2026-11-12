# 관리되는 바이트 버퍼 — std::memory

`memory.vs` 또는 `core.vs`를 가져오고 한정된 `std::memory` 이름을 사용합니다. 버퍼는 이진 데이터에, 뷰는 버퍼의 제한된 범위에 사용하며 독립 복사본이 필요하면 `clone`을 사용합니다.

## 버퍼와 뷰

버퍼 대입은 바이트를 공유하며 복사하지 않습니다. 뷰는 소유자를 유지하고 고정된 오프셋과 길이를 나타냅니다. `readonly_view`는 해당 뷰를 통한 쓰기를 금지합니다. 기본 빈 뷰는 유효합니다. 크기나 용량이 바뀌면 기존 뷰가 영구적으로 만료되며 이전 크기로 돌아와도 복구되지 않습니다. 구조 변경 후 뷰를 다시 만드세요. 변경이 없는 `resize` / `reserve`는 뷰를 유지하고 실패는 버퍼 상태를 유지합니다. 새로 노출되는 바이트는 0이 됩니다.

## 바이트 연산

오프셋과 개수는 음수가 아닌 `int64`이며 선택한 영역 안에 들어가야 합니다. 길이가 0이어도 영역, 경계, 쓰기 권한을 검사합니다. `copy`는 겹치는 범위를 지원합니다. `read<T>` / `write<T>`는 부호 있는/없는 8/16/32/64비트 정수와 별칭, `float`, `double`을 지원하며 기본 바이트 순서는 `little`입니다. 정렬되지 않은 오프셋도 지원합니다. `from_array`는 `array<uint8>`(`array<unsigned char>`)를 복사하고 `to_array`는 독립 스냅샷을 반환합니다.

## API 참조

다음 블록은 시그니처 설명입니다. `region`, `object`, `value`, `source`, `destination`은 호환되는 인수를 나타내며 완전한 스크립트가 아닙니다.

```text
bool std::memory::native_addresses_available();
uint64 std::memory::address_of(&value);
uint64 std::memory::object_id(object);
uint64 std::memory::object_address(object);
uint64 std::memory::data_address(region);
void std::memory::copy(destination, int64 offset, source, int64 source_offset, int64 count);
void std::memory::fill(destination, int64 offset, int64 count, uint8 value);
bool std::memory::equal(left, int64 offset, right, int64 right_offset, int64 count);
std::memory::buffer std::memory::clone(region);
std::memory::buffer std::memory::from_array(array<uint8> bytes);
array<uint8> std::memory::to_array(region);
T std::memory::read<T>(region, int64 offset, std::memory::byte_order order = std::memory::byte_order::little);
void std::memory::write<T>(region, int64 offset, T value, std::memory::byte_order order = std::memory::byte_order::little);
```

| 타입 | 멤버 |
| --- | --- |
| `buffer` | `new buffer()`, `new buffer(int64 size)`, `size()`, `capacity()`, `get(int64 offset)`, `set(int64 offset, uint8 value)`, `resize(int64 size)`, `reserve(int64 capacity)`, `slice(int64 offset, int64 count)`, `readonly_slice(int64 offset, int64 count)` |
| `view` | `view()`, `size()`, `get`, `set`, `slice`, `readonly_slice` |
| `readonly_view` | `readonly_view()`, `size()`, `get`, `slice` → `readonly_view` |
| `byte_order` | `little = 0`, `big = 1` |

## 조회와 권한

`object_id`는 한 런타임 인스턴스에서 살아 있는 공유 객체의 안정적인 `uint64` ID를 반환합니다. 클래스, 지원되는 호스트 클래스, 공유 컨테이너, 채널, 함수, 버퍼가 대상입니다. null 공유 핸들은 0을 반환합니다. 스칼라, 문자열, struct, 뷰, 약한 핸들은 지원하지 않습니다. ID는 수명을 늘리지 않으며 영구 식별자나 런타임 간 비교에 사용할 수 없습니다.

`address_of(&value)`는 기존 참조 대상의 최종 저장 위치를 관찰하며 숫자 페이로드 자체의 주소가 아닙니다. getter나 값 반환 결과는 이 대상을 제공하지 않습니다. `object_address`는 공유 객체를, `data_address`는 영역 바이트를 관찰합니다. 이 임시 숫자는 스크립트에서 역참조하거나 영구 ID로 보관할 수 없습니다. 먼저 `native_addresses_available()`을 확인하세요. 호스트 정책이 주소를 거부할 수 있고 WebAssembly는 네이티브 주소를 제공하지 않습니다. 객체 ID와 경계가 있는 바이트 연산은 계속 사용할 수 있습니다.

## 제한과 실패

호스트가 버퍼별 및 보유 바이트 총량의 한도를 설정합니다. 거부된 할당이나 구조 변경은 기존 버퍼를 유지합니다. 일괄 쓰기는 취소, 시간 또는 명령 예산으로 중단될 수 있으며 완료된 쓰기는 남습니다. 만료된 뷰나 읽기 전용 뷰에 대한 쓰기는 실패하고 호스트 메모리 대여와 충돌하면 Busy를 반환합니다. 저장하려면 바이트를 복사하세요. 기본 객체 스냅샷은 영역 핸들을 직렬화하지 않습니다. 영역은 작업 캡처, 인수, 결과 또는 채널로 전달할 수 없습니다.

## 실행과 호스트 API

VM 또는 Mixed를 사용합니다. 독립 C++ / C# / Python AOT는 메모리 연산이나 타입의 실제 사용을 거부하지만 사용하지 않는 import는 허용합니다. 모바일 통합은 실험적이며 기본적으로 네이티브 주소를 비활성화합니다. 일시 중지된 디버거는 영역 메타데이터를 표시하지만 DAP의 임의 메모리 읽기/쓰기는 제공하지 않습니다.

## 예제

```VeroScript
import "memory.vs";

int main()
{
	int value = 42;
	ref int alias = value;
	if (std::memory::native_addresses_available())
	{
		assert(std::memory::address_of(&value) == std::memory::address_of(&alias));
	}

	auto bytes = new std::memory::buffer(8);
	auto sharedBytes = bytes;
	assert(std::memory::object_id(bytes) == std::memory::object_id(sharedBytes));
	std::memory::write<uint32>(bytes, 1, 0x01020304u, std::memory::byte_order::big);
	auto header = bytes.readonly_slice(1, 4);
	assert(header.get(0) == 1u);
	assert(std::memory::read<uint32>(header, 0, std::memory::byte_order::big) == 0x01020304u);
	std::memory::copy(bytes, 2, bytes, 1, 4);
	auto snapshot = std::memory::clone(bytes);
	assert(std::memory::object_id(bytes) != std::memory::object_id(snapshot));
	assert(std::memory::equal(bytes, 0, snapshot, 0, 8));
	bytes.resize(16); // header is now expired.
	assert(bytes.get(8) == 0u && snapshot.size() == 8);
	auto packet = std::memory::from_array(std::memory::to_array(snapshot));
	assert(std::memory::equal(packet, 0, snapshot, 0, 8));
	print("memory example passed");
	return 0;
}
```

[단계별 튜토리얼](../../../../../../Resources/Docs/Tutorials/ko-KR/standard-library/20-managed-memory.md)

## 호스트 참조

- [C++](../../../../../../Extensions/Language/Cpp/Resources/Docs/User/ko-KR/embedding.md)
- [C#](../../../../../../Extensions/Language/CSharp/Resources/Docs/User/ko-KR/memory.md)
- [Python](../../../../../../Extensions/Language/Python/Resources/Docs/User/ko-KR/memory.md)
- [Interop](../../../../../../VeroScriptInterop/Resources/Docs/User/ko-KR/index.md)
- [WebAssembly](../../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/ko-KR/index.md)


<!-- docs-navigation -->

---

[VeroScript](index.md) · [en-US](../../en-US/standard-library/memory.md) · [fr-FR](../../fr-FR/standard-library/memory.md) · [ja-JP](../../ja-JP/standard-library/memory.md) · [ko-KR](memory.md) · [zh-CN](../../zh-CN/standard-library/memory.md) · [zh-TW](../../zh-TW/standard-library/memory.md)
