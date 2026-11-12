# VeroScript Interop 技術文書

`VeroScriptInterop` は non-C++ host 用 stable C11 process ABI です。この文書は binding maintainer 向けです。ABI header、implementation、test、generated consumer binding が authoritative で、C contract にない managed-language abstraction を作りません。

## ABI の公開インターフェイス

public header は opaque session/value/callable/call-context/buffer/package/interception/generic-host-type/serialization context、string/byte view、status/enum、session option、source overlay、runtime configuration、JIT policy/statistic、native signature、host-type description、mutation event、visitor callback を定義します。optional session JIT-cache capability は ABI 0.1.1 と original 32-byte policy layout を維持しながら append-only switch、retained-resource limit、cumulative cache-context statistic を追加します。

exported function は runtime/version/capability query、buffer、locale catalog、session create/destroy、configuration、source/file loading、execution、value、call、package、host binding、interceptor、mutation、reflection、serialization を扱います。exported name、layout、calling convention、enum value、status meaning は prose ではなく ABI です。

## ownership と view

opaque handle には create、borrow、retain、destroy rule があります。view は owner ではありません。returned string/byte view は owner を超えて生存せず、buffer は owner release 前に copy します。callback context は documented callback call 中だけ valid です。binding は C ABI が保証しない garbage collector/reference-counting assumption に頼らず制約を公開します。

## callback と thread

callback result は ABI callback status contract 経由です。callback exception、failed conversion、invalid user data は native resource を漏らさず report します。host callback は script worker から入るため host-owned state は独自 synchronization が必要です。creator-thread restriction/callback reentrancy は core と各 language adapter に合わせます。

## バージョン管理

`vs_interop_get_abi_version()` は ABI contract を返します。runtime semantic version、capability bit、package metadata、ABI version は別です。user-facing version prose に pre-release numeric marker を置かず ABI number を product version と扱いません。additive field/function は compatibility decision、layout/enum change は ABI review/consumer update が必要です。

## ローカライズ

locale function は canonical BCP 47 catalog directory を select/enumerate します。locale discovery は fixed language count を想定しません。catalog key、exported function name、status code、ABI value、diagnostic ID、version string は translate しません。

## session、value、callback の詳細

session API は owner-thread operation、idle-session operation、run 中許可 operation を区別します。loading、clearing、reflection、package management、configuration、binding change は precondition 未充足なら reject します。`vs_interop_session_copy_last_error()` は operation-level error fallback で、structured diagnostic は count、argument、formatted-diagnostic API で読みます。

value surface は scalar/string/enum/host object/array/map/ordered map/set/queue/stack/deque/channel を含みます。enumeration callback は borrowed view を受けます。channel capacity は first use 前に設定し、close は blocked operation を wake し send/receive failure は optional owned UTF-8 error buffer を伴う status を返します。nested capability transfer/invalid type conversion は successful coercion ではなく engine error です。

callback context helper は native host function、generic type factory、instance interceptor、external serialization/deserialization callback から error を返す supported path です。implementation が engine error として明示しない限り callback は owning session を reentrant に destroy/clear しません。callback user data、host-object payload、managed root は全 callback entry の間生存します。

## 詳細ページ

- [Interop ABI と lifetime](abi-and-lifetime.md)
- [Interop value、callback、package](values-and-callbacks.md)

## 仕様の基準となるソース

`Public/VeroScriptInterop.h`、private implementation/test、CMake package configuration、ABI を消費する C#/Python adapter を使います。ownership、layout、callback、threading、status、locale、capability、versioning の変更とともに更新します。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
