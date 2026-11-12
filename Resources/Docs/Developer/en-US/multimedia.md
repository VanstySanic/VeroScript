# Multimedia maintenance

This shared maintainer guide owns the provider boundaries and verification procedure used across multimedia modules. Public behavior and APIs remain in each module’s User guide; implementation details remain in its Developer guide.

Keep public headers independent of third-party types. Generic contracts belong to their Media module; concrete libraries and original notices belong to the selected backend. Preserve error/output and ownership contracts when updating a provider. Rebuild provider and consumer together after a service contract or Core identity change.

Use the module CMake entry and the tests registered by its CMakeLists.txt when execution is permitted. Validate installed CMake consumers, optional script/native-provider consumers and each claimed platform separately. Static document/resource checks do not certify rendering, fonts, devices, mobile targets or IDE integration. Keep temporary measurements out of User guides.

Preserve the original licenses and pinned-source inventories of every redistributed dependency. Third-party legal texts retain their original language.

## Module guides

| Module | Usage | Maintenance |
| --- | --- | --- |
| VeroScriptPlatform | [User](../../../../Modules/Multimedia/VeroScriptPlatform/Resources/Docs/User/en-US/index.md) | [Developer](../../../../Modules/Multimedia/VeroScriptPlatform/Resources/Docs/Developer/en-US/index.md) |
| VeroScriptGraphics | [User](../../../../Modules/Multimedia/VeroScriptGraphics/Resources/Docs/User/en-US/index.md) | [Developer](../../../../Modules/Multimedia/VeroScriptGraphics/Resources/Docs/Developer/en-US/index.md) |
| VeroScriptAudio | [User](../../../../Modules/Multimedia/VeroScriptAudio/Resources/Docs/User/en-US/index.md) | [Developer](../../../../Modules/Multimedia/VeroScriptAudio/Resources/Docs/Developer/en-US/index.md) |
| VeroScriptImage | [User](../../../../Modules/Multimedia/VeroScriptImage/Resources/Docs/User/en-US/index.md) | [Developer](../../../../Modules/Multimedia/VeroScriptImage/Resources/Docs/Developer/en-US/index.md) |
| VeroScriptFont | [User](../../../../Modules/Multimedia/VeroScriptFont/Resources/Docs/User/en-US/index.md) | [Developer](../../../../Modules/Multimedia/VeroScriptFont/Resources/Docs/Developer/en-US/index.md) |
| VeroScriptGui | [User](../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/User/en-US/index.md) | [Developer](../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/Developer/en-US/index.md) |
| VeroScriptSdl3 | [User](../../../../Modules/Multimedia/Backend/VeroScriptSdl3/Resources/Docs/User/en-US/index.md) | [Developer](../../../../Modules/Multimedia/Backend/VeroScriptSdl3/Resources/Docs/Developer/en-US/index.md) |
| VeroScriptStb | [User](../../../../Modules/Multimedia/Backend/VeroScriptStb/Resources/Docs/User/en-US/index.md) | [Developer](../../../../Modules/Multimedia/Backend/VeroScriptStb/Resources/Docs/Developer/en-US/index.md) |
| VeroScriptFontBackend | [User](../../../../Modules/Multimedia/Backend/VeroScriptFontBackend/Resources/Docs/User/en-US/index.md) | [Developer](../../../../Modules/Multimedia/Backend/VeroScriptFontBackend/Resources/Docs/Developer/en-US/index.md) |
| VeroScriptRmlUi | [User](../../../../Modules/Multimedia/Backend/VeroScriptRmlUi/Resources/Docs/User/en-US/index.md) | [Developer](../../../../Modules/Multimedia/Backend/VeroScriptRmlUi/Resources/Docs/Developer/en-US/index.md) |

GUI and foundation aggregate component installation uses CMake 3.25+ `block(SCOPE_FOR VARIABLES ...)` scopes. Restore each nested component selection while propagating `CMAKE_INSTALL_MANIFEST_FILES` and `CMAKE_ABSOLUTE_DESTINATION_FILES`. This keeps dependency installation and its file inventory intact across nested Gui/RmlUi/resource components; installed SDK consumers and final release filtering remain separate checks.

<!-- docs-navigation -->

---

[Documentation maintenance](index.md)

[en-US](multimedia.md) · [fr-FR](../fr-FR/multimedia.md) · [ja-JP](../ja-JP/multimedia.md) · [ko-KR](../ko-KR/multimedia.md) · [zh-CN](../zh-CN/multimedia.md) · [zh-TW](../zh-TW/multimedia.md)
