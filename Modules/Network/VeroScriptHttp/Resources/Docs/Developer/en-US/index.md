# HTTP: Source ownership

Http owns request/response and client/server facade contracts, protocol bindings and its package configuration. Its `Internal/` hooks connect the Curl client and Boost server providers; shared transport contracts remain in Network.

Shared context/operation ownership, registry composition, SDK boundaries and build options are maintained in [Network](../../../../../VeroScriptNetwork/Resources/Docs/Developer/en-US/index.md).

[CMakeLists.txt](../../../../CMakeLists.txt) · [README](../../../../README.md) · [User](../../User/en-US/index.md)

<!-- docs-navigation -->

---

[User](../../User/en-US/index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
