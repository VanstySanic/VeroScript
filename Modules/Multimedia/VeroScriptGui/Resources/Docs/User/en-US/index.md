# GUI

Gui owns widget trees, builders, attributes, events and ML/CSS. RmlUi is a separately selected layout and rendering implementation.

- [Framework and RmlUi integration](Reference/Framework.md)
- [Markup and styles](Reference/Markup.md)

`vs-gui` / `vs-rmlui`. CMake: `VeroScriptGui` / `VeroScriptRmlUi`; VS: `VS::Gui`.

[VS Code](../../../../../../../Extensions/Editor/VSCode/VeroScriptGuiVSCode/Resources/Docs/User/en-US/index.md) · [Rider](../../../../../../../Extensions/Editor/JetBrains/VeroScriptGuiJetBrains/Resources/Docs/User/en-US/index.md)

`gui-compile` requires Application support and produces the standard ZIP `.vsapp` application container. It contains the startup descriptor and the baked GUI image, including its resource metadata and explicitly embedded assets. GUI check/preview and ordinary CLI check/run/inspect read the same container. A Runtime built with GUI scripting can also run it. Recompile earlier raw GUI `.vsapp` files. Backends follow the installed environment’s service-selection policy; host, project and command options can override it. Editors do not hardcode RmlUi.

[Tutorials: Collections, navigation and dialogs](../../../../../../../Resources/Docs/Tutorials/en-US/toolchain/24-gui-collections.md)

Unified automatic, explicit and disabled selection rules are documented in [Service backend selection](../../../../../../../VeroScript/Resources/Docs/User/en-US/introduction.md#backend-selection).

<!-- docs-navigation -->

---

[User documentation](../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
