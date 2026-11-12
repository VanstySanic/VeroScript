# VSWML and VSWCSS

> This page describes the native `FMarkupProgram` loader. For ordinary VS imports, annotation Schema, generated builders, custom properties/events and typed factories, see the [framework guide](Framework.md). The native adapter restrictions below apply to that loading API.

Use the public API in `VeroScriptGuiMarkup.h` to load markup into an `FWidget` tree with typed properties and routed events, then attach it to an existing context. Text, fonts, images, custom drawing and basic controls are supported. The native loader registers only `Widget` by default; use the Schema-aware VS compilation path for the built-in control library and its typed properties/events, or explicitly supply native type factories. The markup format does not require a particular layout or graphics backend.

## Loading

```cpp
#include "VeroScriptGuiMarkup.h"
#include <utility>

bool LoadMarkupUi(const std::string& Entry,
				  std::shared_ptr<VeroScript::Gui::FWidget>& OutRoot,
				  std::string& Error)
{
	using namespace VeroScript;
	Gui::FMarkupSource Source;
	Gui::FMarkupDiagnostic Diagnostic;
	if (!Gui::ReadMarkupFile({}, Entry, Source, Error))
	{
		return false;
	}
	Gui::FMarkupCompileOptions Options;
	Options.Read = Gui::ReadMarkupFile;
	auto Program = Gui::FMarkupProgram::Compile(Source, Options, Diagnostic);
	if (!Program)
	{
		Error = Diagnostic.Format();
		return false;
	}
	auto Root = Program->Create({}, Diagnostic);
	if (!Root)
	{
		Error = Diagnostic.Format();
		return false;
	}
	OutRoot = std::move(Root);
	Error.clear();
	return true;
}
```

Attach the returned root to an existing `FContext` with `SetRoot`, and complete a successful `Update` before rendering. The loading function itself creates no window, device or backend.

The host chooses the entry path. Internal `src` references resolve relative to the declaring file, independently of the working directory. A custom `FMarkupReader` may use opaque module/resource identities instead. `GetDependencies` exposes immutable source snapshots read by the frontend; transitive VS imports remain Core's responsibility. Compilation needs no window/GPU. Each `Create` produces independent widgets and script state. Instances outlive the compiled Program. Reloading/recompilation is explicit; no automatic hot reload or state migration is provided.

Filesystem paths passed as strings are UTF-8. When MSVC sources or generated macro definitions contain non-ASCII paths, compile those sources with `/utf-8`.

## Markup and components

- UTF-8, one root, case-sensitive XML-style syntax, quoted attributes, comments, CDATA, five named XML entities and numeric entities. `bind:` and `on:` are built in; do not declare `xmlns`.
- Any registered widget may be the ordinary instance root. Every widget may contain multiple children; paired and self-closing tags are equivalent.
- Root directives: multiple `import`/`style`, at most one `script`. `.vswml` imports must contain components; `.vs` imports contain script. Styles accept `src` or inline content. Directives nested inside widgets are rejected.
- Original property names and Marks work, such as `Width`/`width`, `HitTest`/`hit-test`; duplicate spellings for one property are rejected. ML reserves lowercase `id` and `class`; values are case-sensitive.
- Ordinary attributes are fixed literals. `bind:gap="ReadGap"` or `bind:gap="{ Count + 1 }"` installs a getter. `on:pointer-down="Increment"` or `on:pointer-down="{ Count = Count + 1; }"` installs a handler. Braced code uses normal VS expressions/statements.

```xml
<Widget id="root">
    <import src="card.vswml" />
    <style src="page.vswcss" />
    <preview-card id="first"><Widget Height="20px" /></preview-card>
    <PreviewCard id="second" />
</Widget>
```

```xml
<!-- card.vswml -->
<component type="Demo::PreviewCard" mark="preview-card" base="Widget">
	<template><Widget id="content" Height="40px" /></template>
	<style>:root { padding: 8px; } #content { background-color: #4080c0; }</style>
</component>
```

`type` is the qualified VS type name; its short name and optional `mark` are tags. A component has exactly one `template`. `base` may refer to an imported ML component or registered native factory. Templates/styles expand from base to derived; recursive templates/inheritance are rejected. This is frontend template inheritance, not the complete arbitrary VS/ML class export/inheritance protocol.

Component templates have private ID, style and script scopes. Pages see component roots, not internals. `FMarkupProgram::Find(componentRoot, "content")` queries the component's scope. Children appended at a usage site retain their declaring scope. Native factories' existing internal children are private too. Arbitrary native `AddChild` calls after creation do not automatically add ML scope membership. Duplicate IDs fail, including atomic ID changes.

## Styles and properties

```css
:root { --space: 8px; layout: vertical; padding: 12px; gap: var(--space); }
preview-card { height: 80px; background-color: #203040; }
#first { padding: 10px 12px; }
Widget > .accent { width: 50%; }
preview-card:hover { background-color: #4080c0; }
```

Selectors: original type/Mark, `*`, `.class`, `#id`, compounds, descendant, `>`, comma lists; states: `:root`, `:hover`, `:active`, `:focus`, `:focus-within`, `:disabled`, `:checked`, `:read-only`. States refresh on `Update` after input dispatch. Forward normalized Platform cancellation, focus/lifecycle and reset events; input reset clears GUI state.

Precedence: default → theme → template styles → usage styles → local literal/getter. Within a scope, compare ID, class/state and type specificity, then source order. Scope precedence comes before cross-scope specificity. `FMarkupProgram::SetTheme` sets/removes the instance runtime's theme. `SetClasses`/`SetId` affect the next update. Only style-enabled properties are accepted in CSS. Base properties expose getter bindings. The compiler uses GuiProperty schemas to distinguish plain values from bindable Attribute properties on concrete controls.

| Original names | CSS Marks | CSS allowed |
| --- | --- | --- |
| Width, Height, Padding, Gap | width, height, padding, gap | Yes |
| Layout, Visibility, Opacity, Background | layout, visibility, opacity, background-color | Yes |
| HitTest, Enabled, Focusable | hit-test, enabled, focusable | No; ML/local configuration only |

Lengths accept numbers, `px`/`dp`, percentages and `auto`, subject to the property's validation. Padding accepts one to four nonnegative pixel lengths. Background accepts hexadecimal RGB/RGBA, `rgb`/`rgba` and basic named colors, converting encoded sRGB into native linear RGBA. Parsing does not change the process locale. Custom properties inherit their parent's computed values and support `var(--name, fallback)` with nested fallback. Missing variables, cycles and invalid resulting types report source locations. Variable inheritance is within one ML runtime; separately created trees do not implicitly share variables. Typography inherits as described below; other ordinary properties do not acquire HTML inheritance rules.

Unsupported selectors, arbitrary RCSS, `!important`, `@` rules, CSS animations are diagnosed. Unknown controls/properties/events and raw text nodes are errors. `Set` replaces a local getter; `Clear` removes the local source, exposing the current style/default without restoring an old getter. Getter errors fail the update.

## Optional VS scripts

Build with `-DVEROSCRIPT_GUI_SCRIPTING=ON` and link `VeroScript::GuiScript`. It reuses a host's `VeroScriptCpp` target or finds the matching C++ Runtime SDK through `CMAKE_PREFIX_PATH`. Static markup/styles need no Core dependency. Configure `FMarkupScriptOptions.Engine` as for ordinary VS embedding, then pass `MakeMarkupScriptFactory(options)` to `Program->Create`.

```xml
<Widget>
    <script><![CDATA[
    public:
        int64 Count = 0;
        @GuiMethod(Mark = "increment")
        void Increment()
        {
            Count = Count + 1;
        }
    ]]></script>
    <Widget id="bar" Height="32px"
            bind:width="{ VS::Gui::Length::Percent(20 + cast&lt;double&gt;(Count % 8) * 10) }"
            on:pointer-down="increment" />
</Widget>
```

Inline script contains class members. Pages receive an independent state object; component state derives from `VS::Gui::Widget`, referring to the existing native component root. Build the entire tree, construct script state, then install getters/events. Constructors can query initialized `elements`, but layout and mount are not complete. Bindings refresh each Update through Gui's normal cache/dependency/read-only rules.

An ordinary page may instead use `<script src="logic.vs" type="App::Logic" />`. The generated `context` is an instance of that class. Public unqualified members resolve to it, locals shadow normally, and VS access rules still apply. Stateless expressions work without a script block. Hosts may supply an `IMarkupScriptContext` as Create's third argument instead of a script block; the two cannot coexist. Components use their own context.

Built-in names are `self`, `context`, `elements`; handlers also receive `sender`, `event`, `arg0`/`arg1`. Inline context is the current state; a component's `this` exposes base GetX/SetX/ClearX methods. `elements.Find(id)` may return null; `Require(id)` fails when missing. Views retain widgets weakly. Named getter/handler methods require `@GuiMethod`; original name and optional Mark both work. GUI annotation names and their option names ignore ASCII case. Core annotation names remain case-sensitive by default; reflection `find_arguments(name)` still uses the exact stored argument spelling. Mark values stay case-sensitive. Routed handlers accept no arguments, Event, Widget sender, or sender plus Event; incompatible/ambiguous signatures fail. Routed handlers return void. Custom GuiEvent delegates preserve their declared parameter and return types through the compiler path.

Event exposes Target/CurrentTarget, X/Y/DeltaX/DeltaY/Pressure, Type/Key/ScanCode/Button/Modifiers/Buttons, PointerId/DeviceId/WindowId/TimestampNanoseconds, Text/SelectionStart/SelectionLength, Repeat/Synthetic. Numeric values follow Platform contracts. StopPropagation, StopImmediatePropagation and PreventDefault are valid only during synchronous dispatch. A retained event holds input data and weak widget references, not a borrowed native stack frame.

Normal standard-library expressions work: after importing `attribute.vs`, `{ Value.get() }` reads a `std::attribute<double>` and an event may call `Value.set(...)` for the next update. Localization/formatting uses ordinary VS calls and the host's Core services; Text and editable controls are available; there is no implicit ML localization-key syntax. Bind text to normal Core localization/formatting calls.

ML import directives use the supplied reader; ReadMarkupFile only resolves filesystem-relative references. To import a standard library through ML, supply a reader that resolves it, or perform the normal Core import inside an external VS file. Core import search settings do not implicitly change the ML reader.

Engine configuration retains its ordinary embedding lifetime requirements. Existing ModuleEnvironment catalogs are composed with Gui's bindings without mutating the published environment; identity/name conflicts are errors. Compilation, creation, calls and destruction are owner-thread operations. If script state is finally released elsewhere, call `CollectMarkupScripts` on its creation thread before shutting down the host.

Native factories must return fresh widgets on the owner thread, including all descendants; initialized or already ML-owned nodes are rejected. In binding expressions/blocks, `self`, `context`, `elements`, `sender`, `event`, `arg0` and `arg1` are reserved. Local variables, lambda parameters, foreach/catch bindings and local type aliases cannot redeclare them.

## Supported scope

GuiWidget/GuiProperty/GuiEvent schemas, builders, baked artifacts, direct ML imports and CLI preview are available. Basic, collection and navigation controls are supplied, including modal dialogs. Optional editor companions are documented separately. The GUI-enabled CLI registers gui-check/gui-compile/gui-preview; commands and typed factories are described in the [framework guide](Framework.md). Unsupported functionality does not silently fall back to HTML/RML.

Use the [counter markup](../../../../Examples/Markup/counter.vswml), [component markup](../../../../Examples/Markup/card.vswml) and [shared stylesheet](../../../../Examples/Markup/layout.vswcss) as inputs. These examples are included with the integrated Resources documentation; do not assume a functional SDK archive contains an examples directory.

## Text, fonts, images and drawing

`Text` is literal UTF-8, never interpreted as RML. Use `Text="..."` or a string attribute binding; raw ML body text and mixed inline markup remain unsupported. Font family/size/weight/style, text color/alignment, whitespace, ellipsis, line height, letter spacing, direction and language inherit from the parent unless locally configured. `Direction` sets the paragraph base direction: auto/ltr/rtl. Auto uses each paragraph's first strong character, falling back to LTR; `Language` selects shaping language and does not override auto direction. RmlUi lays out mixed-direction runs while preserving logical UTF-8 values and code-point selection offsets. ML `Direction="auto" Language="ar"` and CSS `direction: auto; language: ar;` use these same properties; left/right alignment remains physical.

See [GUI framework contracts — Text, fonts, images and drawing](Framework.md#image-resources).

Built-in image and text content precede child widgets in the default layout, in image/text/children order; changing properties or rebuilding the tree preserves this order.

Image, BackgroundImage and MaskImage getters return resolved identities/URLs for both literal and bound values, so forwarding them to another module preserves the resource origin.

See [GUI framework contracts — Text, fonts, images and drawing](Framework.md#compositor-properties).

[Framework](Framework.md)

| Property | CSS Mark | Style |
| --- | --- | --- |
| Text, Image | text, image | No |
| FontFamily, FontSize, FontWeight, FontStyle | font-family, font-size, font-weight, font-style | Yes |
| TextColor, TextAlign, WhiteSpace, TextOverflow | color, text-align, white-space, text-overflow | Yes |
| LineHeight, LetterSpacing, Direction, Language | line-height, letter-spacing, direction, language | Yes |
| CornerRadius, ClipContent | border-radius, clip-content | Yes |
| BorderWidth, BorderColor | border-width, border-color | Yes |
| Cursor | cursor | Yes |
| BackgroundImage, MaskImage, BoxShadow | background-image, mask-image, box-shadow | Yes |
| Transform, Filter, BackdropFilter | transform, filter, backdrop-filter | Yes |

See [GUI framework contracts — Text, fonts, images and drawing](Framework.md#native-resource-services).

Cursor: `VS::Gui::Cursor`; ML `Cursor="pointer"`; CSS `cursor: pointer`. It inherits and supports Attribute bindings. `Auto` selects control behavior, `None` hides the cursor. See [Framework](Framework.md) for pointer services and window-boundary notifications.

<!-- docs-navigation -->

---

[GUI](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](Markup.md) · [fr-FR](../../fr-FR/Reference/Markup.md) · [ja-JP](../../ja-JP/Reference/Markup.md) · [ko-KR](../../ko-KR/Reference/Markup.md) · [zh-CN](../../zh-CN/Reference/Markup.md) · [zh-TW](../../zh-TW/Reference/Markup.md)
