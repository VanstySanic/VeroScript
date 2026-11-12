# Third-party notices

The source distribution retains the complete RmlUi 6.3 release with local paragraph-layout and text-editor integration patches.

- RmlUi Core: MIT, copyright CodePoint Ltd, Shift Technology Ltd, the RmlUi Team and contributors. Full text: `ThirdParty/RmlUi/LICENSE.txt`.
- Integrated Core containers: third-party MIT texts in `ThirdParty/RmlUi/Include/RmlUi/Core/Containers/LICENSE.txt`.
- Upstream Debugger font assets: notices and SIL Open Font License in `ThirdParty/RmlUi/Source/Debugger/LICENSE.txt`. The Gui runtime does not link the Debugger.

The native SDK installs these texts under `licenses/RmlUi` as `LICENSE.txt`, `LICENSE.Containers.txt` and `LICENSE.Debugger.txt`, together with the pinned-source manifest. Preserve the notices applicable to the components redistributed by an application.

## Local RmlUi patches

`ThirdParty/RmlUi.version.json` records the upstream release and the local modifications. Its archive URL and hash identify the original upstream archive, before these changes. Local changes add optional paragraph and visual-line queries to the font engine, retain normalized source mappings and paragraph ownership through inline layout, and use those mappings for text rendering and editing. The default font engine returns unsupported for the optional queries and retains the original path.

When updating RmlUi, reapply and review these hooks against the new layout, selection and IME implementations, and run the paragraph/font, inline-layout and editor regressions. Rebuild the patched headers and Core library together; an unpatched upstream Core binary does not implement the added font-engine interface.

The inline-layout patches are in `Source/Core/Layout/BlockContainer.h/.cpp`, `BlockFormattingContext.cpp`, `InlineBox.h/.cpp`, `InlineContainer.cpp`, `InlineLevelBox.h/.cpp` and `LineBox.h/.cpp`. The font-query, normalization and editing hooks are in the corresponding Core `FontEngineInterface`, `TextShapingContext`, `ElementText`, `StringUtilities`, `TextInputContext` and `Elements/WidgetTextInput*` files.

## Text and image dependencies

- FreeType 2.13.3: distributed under the FreeType Project License (FTL), in `Modules/Multimedia/Backend/VeroScriptFontBackend/ThirdParty/FreeType/docs/FTL.TXT` and `Modules/Multimedia/Backend/VeroScriptFontBackend/ThirdParty/FreeType/LICENSE.TXT`. Portions of this software are copyright © The FreeType Project (www.freetype.org). All rights reserved. Additional BDF/PCF notices and the bundled gzip implementation's zlib notice are installed as `LICENSE.BDF.txt`, `LICENSE.PCF.txt` and `LICENSE.ZLIB.header.txt` in `licenses/FreeType`.
- HarfBuzz 10.4.0: MIT-style licenses and copyright notices in `Modules/Multimedia/Backend/VeroScriptFontBackend/ThirdParty/HarfBuzz/COPYING`.
- SheenBidi 3.0.0: Apache-2.0, owned by `Modules/Multimedia/Backend/VeroScriptFontBackend/ThirdParty/SheenBidi`; its Unicode data notices and complete licenses are installed under `licenses/SheenBidi` by VeroScriptFontBackend.
- stb_image: pinned by `Modules/Multimedia/Backend/VeroScriptStb/ThirdParty/stb.version.json`; the MIT option and full upstream license text are retained in `Modules/Multimedia/Backend/VeroScriptStb/ThirdParty/stb/LICENSE`.
- The embedded, unmodified `LatoLatin-Regular.ttf` is copyright (c) 2010–2015 Łukasz Dziedzic, Reserved Font Name Lato, SIL Open Font License 1.1. The original complete font notices are in `Modules/Multimedia/VeroScriptFont/Resources/Assets/Fonts/LICENSE.txt`. `Vero Sans` is a runtime registration key; it does not rename or modify the font file.
- The font engine adapter in `Private/Font` contains adapted copies of RmlUi's HarfBuzz sample, covered by RmlUi's license; upstream originals remain unmodified in `ThirdParty`.

SDK installation includes `licenses/FreeType`, `licenses/HarfBuzz`, `licenses/stb`, `licenses/SheenBidi` and `licenses/Fonts/LICENSE.Lato.txt`. Preserve applicable notices when redistributing linked libraries, executables or embedded fonts. Host-supplied fonts/images retain their own licenses.

VeroScriptStb owns the stb notices; VeroScriptFontBackend owns the FreeType/HarfBuzz, SheenBidi and Unicode notices; VeroScriptFont owns the default-font notices. Their install components are included in the Gui SDK bundle. `Private/Font` is adapted from the RmlUi 6.3 HarfBuzz sample (MIT); it retains family/fallback, atlas, effects and geometry integration while using VeroScriptFont for native font operations.
