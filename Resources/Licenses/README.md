# Third-party dependency notices

This index links to notices owned by each component. Vendored source code and
original license files remain with the consuming module.

| Component | Owner | Notice source |
| --- | --- | --- |
| AsmJit | `Modules/Execution/VeroScriptAsmJit` | [Module notice](../../Modules/Execution/VeroScriptAsmJit/Resources/Licenses/NOTICES.md), [original license](../../Modules/Execution/VeroScriptAsmJit/ThirdParty/asmjit/LICENSE.md) and [pinned revision](../../Modules/Execution/VeroScriptAsmJit/ThirdParty/asmjit/VERSION.txt) |
| SDL3 | `Modules/Multimedia/Backend/VeroScriptSdl3` | [Component notices](../../Modules/Multimedia/Backend/VeroScriptSdl3/Resources/Licenses/NOTICES.md), [original license](../../Modules/Multimedia/Backend/VeroScriptSdl3/ThirdParty/SDL3/LICENSE.txt) and [pinned revision](../../Modules/Multimedia/Backend/VeroScriptSdl3/ThirdParty/SDL3.version.json) |
| SDL3 optional WebGPU | `Modules/Multimedia/Backend/VeroScriptSdl3` | [wgpu-native and dependency notices](../../Modules/Multimedia/Backend/VeroScriptSdl3/Resources/Licenses/WGPU-NOTICES.txt), [Emdawn browser port notices](../../Modules/Multimedia/Backend/VeroScriptSdl3/Resources/Licenses/EMDAWN-NOTICES.txt); applicability depends on the selected native/browser backend |
| RmlUi | `Modules/Multimedia/Backend/VeroScriptRmlUi` | [Component notices](../../Modules/Multimedia/Backend/VeroScriptRmlUi/Resources/Licenses/NOTICES.md), [original license](../../Modules/Multimedia/Backend/VeroScriptRmlUi/ThirdParty/RmlUi/LICENSE.txt) and [pinned revision](../../Modules/Multimedia/Backend/VeroScriptRmlUi/ThirdParty/RmlUi.version.json), [Core container licenses](../../Modules/Multimedia/Backend/VeroScriptRmlUi/ThirdParty/RmlUi/Include/RmlUi/Core/Containers/LICENSE.txt) and [Debugger font licenses](../../Modules/Multimedia/Backend/VeroScriptRmlUi/ThirdParty/RmlUi/Source/Debugger/LICENSE.txt) |
| JSON Schema dependencies | `Modules/Network/VeroScriptJson` | [Module notices](../../Modules/Network/VeroScriptJson/Resources/Licenses/NOTICES.md), [pinned sources and patch](../../Modules/Network/VeroScriptJson/ThirdParty/sources.json); [jsoncons license](../../Modules/Network/VeroScriptJson/ThirdParty/jsoncons/LICENSE), [QuickJS-ng license](../../Modules/Network/VeroScriptJson/ThirdParty/quickjs-regexp/LICENSE), [IDNA license](../../Modules/Network/VeroScriptJson/ThirdParty/idna/LICENSE) and [Unicode license](../../Modules/Network/VeroScriptJson/ThirdParty/idna/UNICODE_LICENSE.txt) |
| Boost and curl | `Modules/Network/Backend/VeroScriptBoost`, `Modules/Network/Backend/VeroScriptCurl` | [Boost inventory](../../Modules/Network/Backend/VeroScriptBoost/ThirdParty/sources.json), [Boost license](../../Modules/Network/Backend/VeroScriptBoost/ThirdParty/boost/LICENSE_1_0.txt), [curl inventory and patch](../../Modules/Network/Backend/VeroScriptCurl/ThirdParty/curl-source.json), [curl license](../../Modules/Network/Backend/VeroScriptCurl/ThirdParty/curl/COPYING) |
| OpenSSL (native HTTPS server / WSS) | `Modules/Network/Backend/VeroScriptBoost` | [Dependency configuration](../../Modules/Network/Backend/VeroScriptBoost/ThirdParty/openssl-source.json), [original license](../../Modules/Network/Backend/VeroScriptBoost/Resources/Licenses/OpenSSL-LICENSE.txt) |
| Installer dependencies | `VeroScriptInstaller` | [Notice sources and complete texts](https://github.com/VanstySanic/VeroScript/blob/main/VeroScriptInstaller/Resources/Licenses/README.md) |
| Roslyn | `Extensions/Language/CSharp` | [Notice sources and complete texts](https://github.com/VanstySanic/VeroScript/blob/main/Extensions/Language/CSharp/Resources/Licenses/README.md) |
| Package manager dependencies | `VeroScriptPackageManager` | [Dependency inventory](https://github.com/VanstySanic/VeroScript/blob/main/VeroScriptPackageManager/ThirdParty/sources.json) and the licenses retained with each dependency |
| Application dependencies | `VeroScriptApplication` | [Dependency inventory](https://github.com/VanstySanic/VeroScript/blob/main/VeroScriptApplication/ThirdParty/sources.json); original JSON, miniz and PicoSHA2 licenses remain with the module |
| Build dependencies | `VeroScriptBuild` | [Dependency inventory](https://github.com/VanstySanic/VeroScript/blob/main/VeroScriptBuild/ThirdParty/sources.json) and the module's original JSON license |
| Windows native runtime | Native Windows products | [Redistribution notice](https://github.com/VanstySanic/VeroScript/blob/main/Resources/Tools/CMake/WindowsRuntimeNotice.txt) |

AsmJit source is stored at `Modules/Execution/VeroScriptAsmJit/ThirdParty/asmjit/`.
The backend is optional and is not bundled with Core or Runtime. An enabled,
compatible installation may be selected automatically when a host or CLI uses a
module environment.

Legal notices remain in their original language. Each product retains notices
for its actual dependencies; this index does not replace those texts. IDE
distributions also carry their own dependency notices. The integrated document
package includes the locally linked component notices, original license
texts and pinned-source inventories. External links to other products’ notice
sources may require access to the repository.

## Image and font services

- [stb/LICENSE.txt](../../Modules/Multimedia/Backend/VeroScriptStb/ThirdParty/stb/LICENSE)
- [stb/stb.version.json](../../Modules/Multimedia/Backend/VeroScriptStb/ThirdParty/stb.version.json)
- [freetype/FTL.TXT](../../Modules/Multimedia/Backend/VeroScriptFontBackend/ThirdParty/FreeType/docs/FTL.TXT)
- [freetype/LICENSE.TXT](../../Modules/Multimedia/Backend/VeroScriptFontBackend/ThirdParty/FreeType/LICENSE.TXT)
- [freetype/LICENSE.BDF.txt](../../Modules/Multimedia/Backend/VeroScriptFontBackend/ThirdParty/FreeType/src/bdf/README)
- [freetype/LICENSE.PCF.txt](../../Modules/Multimedia/Backend/VeroScriptFontBackend/ThirdParty/FreeType/src/pcf/README)
- [freetype/LICENSE.ZLIB.header.txt](../../Modules/Multimedia/Backend/VeroScriptFontBackend/ThirdParty/FreeType/src/gzip/zlib.h)
- [freetype/FreeType.version.json](../../Modules/Multimedia/Backend/VeroScriptFontBackend/ThirdParty/FreeType.version.json)
- [harfbuzz/COPYING.txt](../../Modules/Multimedia/Backend/VeroScriptFontBackend/ThirdParty/HarfBuzz/COPYING)
- [harfbuzz/HarfBuzz.version.json](../../Modules/Multimedia/Backend/VeroScriptFontBackend/ThirdParty/HarfBuzz.version.json)
- [sheenbidi/LICENSE.Apache-2.0.txt](../../Modules/Multimedia/Backend/VeroScriptFontBackend/ThirdParty/SheenBidi/LICENSE)
- [sheenbidi/SheenBidi.version.json](../../Modules/Multimedia/Backend/VeroScriptFontBackend/ThirdParty/SheenBidi.version.json)
- [sheenbidi/LICENSE.Unicode-3.0.txt](../../Modules/Multimedia/Backend/VeroScriptFontBackend/Resources/Licenses/LICENSE.Unicode-3.0.txt)
- [sheenbidi/NOTICE.SheenBidi.txt](../../Modules/Multimedia/Backend/VeroScriptFontBackend/Resources/Licenses/NOTICE.SheenBidi.txt)
- [fonts/LICENSE.Lato.txt](../../Modules/Multimedia/VeroScriptFont/Resources/Assets/Fonts/LICENSE.txt)

Product versions and service contract versions are distinct. Original upstream license text is retained unchanged. The RmlUi source manifest records local text-layout integration patches.
