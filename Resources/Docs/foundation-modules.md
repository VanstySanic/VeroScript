# Multimedia modules and source ownership

Source-tree maintainer index. The localized [shared maintenance guide](Developer/en-US/multimedia.md) owns cross-module provider boundaries and verification. Each module below owns its detailed User contracts and Developer implementation notes; its documentation entry offers all six languages.

| Module | Source owner | Responsibility |
| --- | --- | --- |
| Platform | [Modules/Multimedia/VeroScriptPlatform](../../Modules/Multimedia/VeroScriptPlatform/Resources/Docs/README.md) | Window, input and device contracts |
| Graphics | [Modules/Multimedia/VeroScriptGraphics](../../Modules/Multimedia/VeroScriptGraphics/Resources/Docs/README.md) | Graphics services, Renderer2D and Composition2D contracts |
| Audio | [Modules/Multimedia/VeroScriptAudio](../../Modules/Multimedia/VeroScriptAudio/Resources/Docs/README.md) | Audio device, PCM and WAVE contracts |
| Image | [Modules/Multimedia/VeroScriptImage](../../Modules/Multimedia/VeroScriptImage/Resources/Docs/README.md) | Decoded-image and decoder service contracts |
| Font | [Modules/Multimedia/VeroScriptFont](../../Modules/Multimedia/VeroScriptFont/Resources/Docs/README.md) | Font, shaping and text-layout contracts |
| Gui | [Modules/Multimedia/VeroScriptGui](../../Modules/Multimedia/VeroScriptGui/Resources/Docs/README.md) | GUI application, control, markup and style contracts |
| Sdl3 | [Modules/Multimedia/Backend/VeroScriptSdl3](../../Modules/Multimedia/Backend/VeroScriptSdl3/Resources/Docs/README.md) | SDL-backed platform, graphics and audio implementations |
| Stb | [Modules/Multimedia/Backend/VeroScriptStb](../../Modules/Multimedia/Backend/VeroScriptStb/Resources/Docs/README.md) | Image decoder implementation |
| FontBackend | [Modules/Multimedia/Backend/VeroScriptFontBackend](../../Modules/Multimedia/Backend/VeroScriptFontBackend/Resources/Docs/README.md) | Font shaping, rasterization and text-layout implementations |
| RmlUi | [Modules/Multimedia/Backend/VeroScriptRmlUi](../../Modules/Multimedia/Backend/VeroScriptRmlUi/Resources/Docs/README.md) | GUI rendering, layout and input implementation |

Classification directories organize concrete modules. Keep documentation, localization, examples, tests, build helpers and third-party inputs with their concrete owner. Repository-wide CMake and publication coordination remain under `Resources/Tools`.

- [User entry](User/en-US/index.md): select services, SDKs and supported target environments.
- [Shared multimedia maintenance](Developer/en-US/multimedia.md): provider boundaries, rebuilds, verification and legal resources.
- [Foundation and GUI packaging](../Tools/Packaging/README.md#foundation-resource-and-gui-sdk-packages): aggregate publication and dependency handling.
- [Source layout](source-layout.md): repository structure and dependency boundaries.
- [Documentation maintenance](Developer/en-US/index.md): canonical sources, localization, navigation and All/User publication.

Detailed API defaults, provider capabilities, build commands and platform acceptance belong to the linked module or packaging guides. This index does not record a current runtime acceptance result.
