# GUI framework contracts

## Scope

The framework includes native and script widget trees, declaration scopes, Schema generation, ML/CSS compilation, typed factories, bindings/events, Context/Application, themes, basic presentation animation, i18n frame snapshots and offline application baking. Basic, collection and navigation controls are supplied, including modal dialogs. Optional editor companions are documented separately.

## Script compilation

Register native GUI bindings and configure the engine before compilation. The framework is implicitly imported. Native `VeroScript::Gui` and script `VS::Gui` are separate APIs.

A GUI-enabled CLI configures script-framework dependencies automatically when ordinary compilation or `--check` needs GUI features. Existing GUI programs need no additional switch. Explicit `gui-*` tasks and native `ConfigureGuiScriptEngine` keep their implicit framework import.

A variable, field or parameter named `Gui` alone does not import the framework. Namespace references, GUI annotations, macros and imports that cannot be inspected keep their required or conservative import behavior.

```cpp
@GuiWidget(Mark="frame")
export class Frame : public VS::Gui::Widget
{
public:
	Frame()
	{
		part = VS::Gui::Widget::Create();
		AddInternalChild(part);
	}
	@GuiMethod(Mark="advance")
	void Advance()
	{
		SetAmount(GetAmount() + 1);
	}
private:
	void OnAmountChanged()
	{
		part.SetGap(GetAmount());
	}

	@GuiProperty(Style, Mark="amount")
	std::attribute<double> Amount = std::attribute<double>(2);
	VS::Gui::Widget part;
};

void main()
{
	Frame frame = Frame::Create().Amount(4)[{
		VS::Gui::Widget::Create().Id("content")
	}];
}
```

GUI registers annotation names for ASCII case-insensitive matching; its compile processor also recognizes parameter names without ASCII case distinctions. Core annotation names are case-sensitive by default. Reflection through `find_arguments(name)` still matches the stored parameter spelling exactly, including for GUI annotations. Mark/Id values, ML tags/attributes and CSS identifiers remain case-sensitive. Original declaration names and optional Mark values both work. ML uses lowercase `id` and `class`.

Builders hold independent configurations and snapshot child arrays. Conversion creates a fresh widget, applies its template, then caller configuration, then initial synchronization. Constructors may create internal children. Getter evaluation and change hooks are deferred until the configuration is complete. External content replaces external children only.

Each declaring widget type has its own internal name scope. `elements.Find`/ `Require` resolve within that scope. Existing nested component scopes remain private. Ordinary pages own a separate internal script state and return the tag's actual root type. `elements.Find("part")` returns `nullptr` when no widget is found; `elements.Require("part")` asserts that the lookup succeeds.

## Compiled ML and CSS

`page.vswml` can import the Frame type shown above:

```xml
<Widget Layout="Vertical">
    <import src="frame.vs" />
    <style src="page.vswcss" />
    <script><![CDATA[
    public:
        double Counter = 3;
        @GuiMethod(Mark="read-amount")
        double ReadAmount() const { return Counter; }
        @GuiMethod void Increment() { Counter++; }
    ]]></script>
    <frame id="card" class="card"
           bind:amount="read-amount"
           on:pointer-down="Increment">
        <Widget id="extra" Height="20px" />
    </frame>
</Widget>
```

`page.vswcss`:

```css
:root { padding: 12px; gap: 8px; }
.card { amount: 5; }
.card:hover { background-color: #335577; }
```

The binding takes priority over the style's amount value. Child elements remain normal content. The event modifies page state; a later GUI update reevaluates the binding. `bind:` and `on:` are built-in prefixes without xmlns declarations. Plain properties accept fixed literals only; Attribute properties also accept named getters or `{ expression }` bindings.

Named handlers use GuiMethod; custom delegate events use GuiEvent. Native routed handlers accept no arguments, Event, a compatible sender, or sender plus Event. Custom events preserve their declared return and parameter types. Inline event blocks use `{ statements }`; unrecognized attributes and unsupported CSS are diagnosed.

## Properties, updates and lifecycle

Sources are type defaults, styles, local values/bindings, and an independent animation presentation. SetX replaces a local binding; ClearX removes local configuration and reveals the current style/default. An optional Getter without a value falls through; exceptions fail the update.

Getters are evaluated in safe synchronization rounds and cached by round/revision. Outside those rounds, bound properties return the last committed value. Same-property recursion and mutation from a Getter fail. Custom change hooks commit their cache only after successful completion.

Initialization runs children before parents and completes once per instance; mounting runs parents before children; unmounting runs children before parents. State changes from mounted hooks are synchronized before layout; changes from layout hooks are synchronized on the next `Update`. Failed and undelivered layout notifications remain pending for a later update; successful notifications are not repeated without another layout change. Layout callbacks can read the complete current geometry, while input and rendering require a successful update.

Context.Update takes monotonically increasing elapsed seconds. Post is thread safe; accepted callbacks and their cleanup run on the owner thread, in stable batches. Work posted by work runs on a later update. Structural edits during evaluation/lifecycle must be posted.

Closing rejects new operations even if backend cleanup needs a retry. Final references released on another thread are retired; call Context/Application/adapter collection on the creator thread before host shutdown.

Application converts provider cleanup exceptions to errors and retains unfinished resources and its event-pump lease. Explicit Close can be retried; failed cleanup during final release is retried through CollectRetired on the owner thread.

## Themes, animation and localization

Context.SetTheme accepts typed CSS; ClearTheme removes it. Themes are the lowest style layer inside each declaration scope, including scopes without local stylesheets. Local styles and Setter values retain priority. Invalid style candidates do not publish a partial style round.

Native Animate/StopAnimation/GetPresentation and script AnimateX/StopXAnimation/GetPresentedX support Width, Height, Padding, Gap, Opacity and Background. Length endpoints must share a non-auto unit. AnimationOptions provides Duration, Delay, MaxStep, Easing and HoldEnd. Time starts at the next sample; detached widgets pause. Completion reveals the current base unless HoldEnd is set. GetX continues to return the base property.

Context.SetLocalization attaches a Core i18n context. GetLocalizationSnapshot returns the immutable view captured once at the beginning of the GUI frame. Language/provider changes become visible on the next frame. Core text descriptions and typed arguments are retained; Gui does not parse ICU syntax or treat strings as localization keys.

## Factories and resources

Import an ML resource before loading its factory:

```cpp
import "page.vswml";
using namespace VS::Gui;

void main()
{
	auto factory = VS::Gui::LoadFactory<VS::Gui::Widget>("page.vswml");
	VS::Gui::Widget page = factory.Create();
}
```

LoadFactory validates the requested root type before construction. A page has no context, an automatic external context, or a required provided context. These modes cannot be combined. Components use themselves as context. Provided contexts are currently boxed by their exact static type; upcast to the declared type before passing a derived object.

A host may declare required business contexts and value codecs in FGuiCompilationConfiguration. Literal/style parsers are ordinary visible VS functions from string to the declared value type; the immutable profile participates in compile identity.

Filesystem references are relative to the declaring ML/CSS/VS module. Opaque Core resource identities retain provider/module/group/path and use ReadResource. They are never normalized as filesystem paths. Missing inputs participate in Core's dependency ledger.

`BakeGuiApplication` produces an inner Core application image, retaining GUI metadata. Native image consumers use `ReadPackage`, `RegisterGuiArtifactBindings` and Session `LoadPackage` with that image. The `gui-compile` command additionally uses the Application service to wrap it in a standard ZIP `.vsapp` container; load that container through `FApplication`/Session `LoadApplication`. Container compilation/loading requires Application support. Source checking/previewing remains available without that container service.

`CompileGuiSource`/`CompileGuiFile` use the full compiler without running user initializers or creating windows/devices. For direct native image loading, call `RegisterGuiArtifactBindings` before Session `LoadPackage`. The resource manifest retains caller-relative lookup after source/debug removal; baked scripts and styles need no source files, while external assets still require their providers.

The older native FMarkupProgram API remains supported with native profiles and optional IMarkupScriptContext. Use the compiler path for exported VS component types, generated builders and custom property/event Schema.

## Backends and input

Application accepts separate Platform, Graphics and Gui factories. A host with existing windows uses Context directly. One event source has one pump. MakeRegisteredServiceFactories can create these factories from matched Core native providers; the GUI provider returns IBackendService.

The RmlUi provider retains its Core-owned service lease while a backend or deferred cleanup exists. The loading host owns Core. Backend resources never close the borrowed device or target.

Visibility and HitTest are independent. Hidden retains layout and hides its subtree; Collapsed has empty layout. Input uses the same committed geometry and order as rendering. Routing snapshots the ancestor/listener path; disconnected or disposed listeners are skipped. Event control methods are valid only during dispatch; retained events keep input snapshots.

Native coordinates use GUI units. SetWindowMetrics converts window input by density/DisplayScale; drawing uses DisplayScale. Update metrics after pixel-size or display-scale changes. Do not assume density equals user interface scale.

Windows/Linux validation does not certify macOS, Android, iOS or browser execution. Those targets need their own runtime, rendering, font/input and deployment checks.

## CLI and host tasks

The matching GUI-enabled Runtime provides `gui-check INPUT`, `gui-compile INPUT --output app.vsapp`, and `gui-preview INPUT [--frames N] [-- SCRIPT_ARGS]`. Check/compile do not run initializers or create UI services. A frame bound is available only for the generated ML preview loop; VS entries control their own lifetime.

`--platform-backend`, `--graphics-backend`, and `--gui-backend` precede the command/script and also apply to ordinary script applications. Explicit script options override invocation/environment choices. An empty option inherits; `auto` asks the factory to select an available provider; `none` disables a service. Disabling the platform, GUI, or graphics while rendering is enabled is an error. Native hosts without a default selection for a service retain sdl3/sdl3/rmlui as conventional fallbacks. `--backend` retains its VM/JIT meaning.

Place GUI backend options before options that load the package environment; the CLI rejects a later backend selection after that environment has been prepared.

Embedding hosts use `CreateCommandLineService`, retaining the registration Owner until borrowed descriptors are gone. SetEngineOptions/SetRuntimeServices supply invocation inputs; RegisterBindings/ConfigureEngine prepare ordinary execution, and UseArtifact installs the baked resource manifest. Avoid storing an environment that owns this registration back into its own state.

The optional VeroScriptGuiToolsModule only forwards to the host CLI and typed GUI task services; it never links another Core. Its static query is veroscript_gui_tools_query. A CLI with built-in GUI commands already owns that registration.

Schema distinguishes direct delegates from native routed definitions, phases and default actions. Style does not grant animation support: the six native presentation mappings carry interpolation identities, while custom properties have no implicit interpolation or presentation mapping.

Backend choices must also precede options that load the package environment. The host passes them into package provider selection before publishing the environment. Check/preview accept baked GUI .vsapp inputs as well as sources. An adapter encountering HasHostRegistration reuses the host's command registration.

Shared source example: [main.vs](../../../../Examples/Framework/main.vs), [page.vswml](../../../../Examples/Framework/page.vswml), [page.vswcss](../../../../Examples/Framework/page.vswcss).

## Text, fonts, images and drawing

Text and fonts use FreeType 2.13.3 and HarfBuzz 10.4.0. The embedded, unmodified Lato Latin regular font is available as `Vero Sans`; additional scripts require application-supplied fonts and fallback faces. `Context.LoadFont(source, family)` loads a face before updating the tree; the six-argument overload adds weight, italic, fallback and collection face index. Font bytes remain owned until the last RmlUi context shuts down.

`Text` is literal UTF-8, never interpreted as RML. Use `Text="..."` or a string attribute binding; raw ML body text and mixed inline markup remain unsupported. Font family/size/weight/style, text color/alignment, whitespace, ellipsis, line height, letter spacing, direction and language inherit from the parent unless locally configured. `Direction` sets the paragraph base direction: auto/ltr/rtl. Auto resolves the first strong character of each paragraph, with LTR as the fallback; `Language` only selects language-sensitive shaping. The RmlUi backend resolves Unicode bidirectional runs before shaping and reorders each displayed line. Stored text remains in its original logical UTF-8 order; left/right alignment retains its physical meaning.

<a id="image-resources"></a>

`Image` loads a content image. `BackgroundImage` accepts `url(...)` and linear/radial/conic gradients, including repeating gradients. Relative images resolve from their VS/ML/CSS source, including deferred attribute getters. Native hosts configure `FResourceReader` and optionally `FImageDecoder`; decoded pixels use straight-alpha sRGB RGBA8. Supported formats depend on the selected [Image service](../../../../../../VeroScriptImage/Resources/Docs/User/en-US/index.md); see [Stb](../../../../../../Backend/VeroScriptStb/Resources/Docs/User/en-US/index.md) for `image/stb`. SVG and animated images need an appropriate host decoder/player.

Built-in image and text content precede child widgets in the default layout, in image/text/children order; changing properties or rebuilding the tree preserves this order.

Image, BackgroundImage and MaskImage getters return resolved identities/URLs for both literal and bound values, so forwarding them to another module preserves the resource origin.

<a id="compositor-properties"></a>

`CornerRadius`, `ClipContent`, `Transform`, `Filter`, `BackdropFilter`, `MaskImage` and `BoxShadow` use the same property/binding/style path. The selected backend determines supported composition operations and resource limits; see [RmlUi composition and resources](../../../../../../Backend/VeroScriptRmlUi/Resources/Docs/User/en-US/index.md#composition-resources) for the RmlUi provider.

Override `OnPaint(PaintContext canvas)` for content-local drawing after the element background and before its children. FillRect, DrawLine, DrawImage, PushClip/PopClip and GetWidth/GetHeight are available in VS; native FPaintContext additionally accepts indexed meshes and transforms. Clips use fixed content coordinates; canvas transforms affect subsequent geometry. The canvas expires at callback return and cannot be retained or used to mutate GUI properties/tree. Balanced clips are required. Recorded GPU meshes are reused when commands stay unchanged.

For portable assets, use FGuiAssetOptions with BakeGuiApplication, or repeat `gui-compile main.vs --output app.vsapp --asset image.png --asset font.ttf`. Listed paths are resolved from the command working directory; references inside VS/ML/CSS remain module-relative. Unlisted dynamic assets still need their host provider. SDK installation includes FreeType, HarfBuzz, SheenBidi/Unicode, stb and Lato notices. Native GUI provider contract is 0.1.1.

```cpp
@GuiWidget
export class CanvasView : public VS::Gui::Widget
{
protected:
	void OnPaint(VS::Gui::PaintContext canvas) override
	{
		canvas.FillRect(8, 8, 80, 24, VS::Gui::Color(0, 0.4, 1, 1));
		canvas.DrawLine(8, 40, 88, 40, 2, VS::Gui::Color(1, 1, 1, 1));
	}
};
// String attribute getters may call std::i18n using the frame's localization snapshot.
VS::Gui::Widget title = VS::Gui::Widget::Create().Text("Hello, VeroScript!").FontSize(24);
```

```xml
<Widget>
    <style>
        .title { font-size: 24px; color: #ffffff; }
        .surface { border-radius: 12px; clip-content: true;
                   background-image: linear-gradient(90deg, #173b70, #438bd0); }
    </style>
    <Widget class="title" Text="Hello, VeroScript!" />
    <Widget class="surface" Width="240px" Height="80px" />
    <Widget Image="assets/logo.png" />
</Widget>
```

<a id="native-resource-services"></a>

VeroScriptImage and VeroScriptFont define common resource interfaces. VeroScriptStb supplies `image/stb`; VeroScriptFontBackend supplies `font/font-backend` and the separate `text-layout/font-backend` service. Native GUI hosts inject images and fonts through `FBackendOptions.Images` / `Fonts` / `TextLayouts`. Use a host and SDK set that support the selected Font and text-layout contracts; install the required service/backend packages with their notices.

`ApplicationOptions.FontBackend` and `TextLayoutBackend` select the services independently. Empty values inherit host/CLI choices (`--font-backend`, `--text-layout-backend`); `auto` delegates provider selection, and `none` disables that optional service. Native hosts without a default selection for a service use `font-backend` as the conventional fallback. Native hosts provide `FApplicationFactories.Font`/`TextLayout` or `Fonts`/`TextLayouts` instances. Use compatible providers for GUI `0.1.1`, Font `0.1.2` and text-layout `0.1.0`. Keep the font/layout services alive while their contexts exist and close all contexts before replacing the shared configuration. Pass the selected layout service as the first argument to `FTextLayout::Create`.

## Basic controls

ProgressBar, Slider and ScrollBar clamp GetActualValue to the latest configured bounds before calculating GetNormalizedValue. This also covers range changes that have not yet been synchronized; invalid ranges still fail synchronization.

The embedded `VS::Gui` library includes Text, Image, Border, Spacer, Separator, Stack, Overlay, Button, CheckBox, ToggleSwitch, ProgressBar, Slider, ScrollBar, ScrollView, TextBox and TextArea. Each remains a Widget with external children. Stack uses Layout=Vertical/Horizontal; Overlay overlays its children. Border adds BorderWidth and BorderColor. TextBox/TextArea use Value for editable text; inherited Text remains optional display content.

Use the same generated builders, attributes, events and ML/CSS as custom widgets. Button exposes Clicked and Click; CheckBox/ToggleSwitch expose Checked, CheckedChanged and Toggle. Numeric controls expose Value, Minimum, Maximum, Step, PageSize and Vertical. Step=0 is continuous. Invalid/non-finite ranges fail synchronization. Unbound values are normalized into the range; bound sources remain intact, and GetActualValue returns the rendered normalized value. ProgressBar is read-only. Slider/ScrollBar report ValueChanged only for user input.

User input updates an unbound property and emits its typed event. A bound property keeps its Getter and emits the candidate through the same event; application code updates its model if it accepts that candidate. Programmatic setters and ordinary binding refreshes do not emit user-change events. Attribute properties declared with @GuiProperty expose IsXBound(). Inherited ToggleSwitch and TextArea events retain their CheckBox/TextBox sender signatures. Callbacks should retain other widgets weakly when necessary to avoid ownership cycles.

Mouse/touch activation requires a matching release inside the control. Captured gestures cancel on reset, removal, hiding or disabling. Buttons support Space/Enter; Tab/Shift+Tab follow eligible tree order. Ranges support arrows, Home/End and PageUp/PageDown. Routed PreventDefault suppresses the corresponding default control action. Styles support :checked and :read-only in addition to hover/active/focus/disabled.

ScrollView supports wheel, touch panning and keyboard scrolling. ScrollX/ScrollY are requested logical offsets; GetActualScrollX/Y and GetScrollWidth/Height read the last completed geometry. ScrollTo replaces local bindings explicitly. Clipping, hit testing and rendering use the same scrolled geometry. ScrollBar can be connected through ValueChanged; automatic scrollbar composition is not imposed.

Text editing uses the backend's real text editor, with selection, UTF-8 code-point offsets, clipboard, bounded undo/redo (128 snapshots, 8 MiB per history), single/multiline editing, read-only mode, placeholder and IME preedit/commit. TextChanged reports committed text; Submitted is sent for Enter in TextBox. Select(start,end) uses an exclusive end and logical code-point offsets, even when a selection covers several visual intervals. Arrow navigation, pointer hit-testing and IME caret placement use the same laid-out visual lines; Backspace/Delete retain logical deletion order. Undo, focus changes and rebuilding preserve the active selection endpoint and visual caret affinity. Each text value is limited to 1 MiB UTF-8. Application supplies its window/clipboard services; embedded Context hosts install FTextInputServices. Clipboard failures are reported and the edit is rolled back. Physical IME, accessibility, full grapheme navigation and additional target platforms require separate validation or implementation.

Other backends must declare scrolling/text-editing capabilities and implement the corresponding contracts before accepting those controls. Rebuild all providers/consumers for GUI contract 0.1.1.

Runnable examples: [ML/CSS](../../../../Examples/Controls/main.vs), [page](../../../../Examples/Controls/page.vswml), [styles](../../../../Examples/Controls/page.vswcss), [code construction](../../../../Examples/Controls/code.vs).

Checked, Value, range bounds/step, ReadOnly and scroll offsets are data/configuration properties set in code or ML. Styles configure appearance and orientation; state selectors only read control state. TextBox removes line breaks/tabs from unbound single-line values; GetActualValue reads normalized text without replacing a bound source.

Range endpoints remain reachable when Step does not divide the interval. Slider/ScrollBar touch drags take precedence over ancestor scrolling. Select uses committed UTF-8 code-point indices; an unfocused selection is preserved until focus, and a valid selection cancels preedit. Replacing text or changing editor kind clears undo/redo even during a tree rebuild. The complete control library adds source compilation cost; use gui-compile baking to avoid source compilation at application startup.

ScrollWidth/Height describe content extents without padding, so subtracting the content viewport gives the backend's actual scroll range. For embedded script UI, enter the session once per frame and call Context.Update or Application.Tick/Run from that script entry. FScriptServices.Invoke is a fallback for individual host-driven callbacks and incurs additional session-entry cost. The native-only Context path is independent of this script bridge.

```cpp
// Assign gui during application initialization.
VS::Gui::Context gui = nullptr;
void UpdateGui(double elapsedSeconds)
{
	gui.Update(elapsedSeconds);
}
```

```cpp
auto result = session.CallFunction("UpdateGui", {elapsedSeconds});
```

For independent `VeroScriptSession::CallFunction` entries, enable persistent session state with `session.SetHotReloadEnabled(true)` before initialization. Otherwise the C++ Runtime gives each entry fresh module globals. Running Application.Run/Tick from one script entry already shares that entry's state.

When a requested scroll offset exceeds the current content range, wheel, keyboard and touch input start from the clamped offset; multiple events before the next layout still accumulate. Removing the mouse or touch device that owns a text selection drag ends that drag without cancelling another device's active press. Wheel input passes to a scrollable ancestor when the inner container cannot move; a zero delta does not emit a change. Device-removal cleanup still releases capture if the host text-input service returns an error or throws, and reports that failure.

## Layout, selection, numeric input and floating controls

`Grid` lays out children in equal-width columns (`Columns`, int64, 1–4096), in declaration order. `Gap` separates rows and columns. Child Width is determined by the grid; row height follows the tallest child. Collapsed children do not occupy cells. `Wrap` defaults to horizontal flow and wraps at the content width; use Layout=Vertical with a constrained Height for vertical flow. These containers reuse Widget padding, child ownership and styles. Grid provides equal columns, without CSS grid tracks or spans.

`Splitter` accepts up to two ordinary children. `Value` is the first pane's fraction of the available extent after subtracting `DividerSize` (default 6). Minimum/Maximum constrain this fraction within [0,1]; Step defaults to 0.01. Vertical=false splits left/right, true splits top/bottom. The split axis overrides pane sizes, and the cross axis fills the content viewport. Pointer/touch divider dragging and range keyboard controls emit ValueChanged(Splitter,double). ReadOnly prevents user changes. Additional floating children do not count as panes.

`RadioGroup.SelectedKey` owns selection. Each descendant RadioButton requires a nonempty `Key`, unique within its nearest RadioGroup; nested groups are independent. An empty or unmatched SelectedKey selects nothing. RadioButton exposes IsChecked(), Select(), Focus() and Selected(RadioButton); the group emits SelectionChanged(RadioGroup,string). Space/Enter and pointer activation select a choice; arrow keys move focus and selection between eligible choices, wrapping at the ends. Programmatic configuration emits no user event. Bound SelectedKey remains bound and receives candidate events.

`NumberBox` exposes double Value/Minimum/Maximum/Step (defaults 0/0/100/1), ReadOnly, Placeholder, Focus(), Select(), GetActualValue(), ValueChanged(NumberBox,double) and Submitted(NumberBox,double). Editing retains incomplete drafts. Enter or changing focus commits a finite decimal/exponent number, clamps it and applies Step; an invalid draft restores the last accepted value. Up/Down changes the value by Step (or 1 for Step=0); Escape discards the draft. Parsing and canonical display use a locale-independent decimal point. Format localized display labels separately through the existing core formatting services. `PasswordBox` inherits TextBox, preserving Value, selection, IME and TextChanged/Submitted sender types, while the backend masks displayed characters. GetValue/GetActualValue still return the actual text.

`Popup` is a nonmodal floating child anchored below its logical parent. It retains logical ownership, bindings and event routing, but its backend element is portalled to the document layer, above ordinary content and outside ancestor clipping. OffsetX/OffsetY adjust the position; available viewport space clamps it and can place it above the anchor. Width/Height, padding, border, background and children use normal styles. Open defaults false. Escape closes the top eligible popup; DismissOnOutside defaults true and consumes the dismissing pointer press. OpenChanged(Popup,bool) reports user dismissal; a bound Open keeps its source. Focus is explicit; closing restores the previously focused eligible widget when focus was inside that popup. Escape first cancels an active IME composition. Modal input and focus confinement are enabled by Modal=true; Dialog supplies that default.

`ToolTip` reuses Popup positioning, remains noninteractive and follows hover over its parent after Delay (default 0.5 seconds). AutoOpen defaults true; set it false to control inherited Open explicitly. Leaving the parent or resetting mouse state hides an automatic hint. Tooltip descendants are also excluded from input/focus. IsActuallyOpen() reads the most recent framework state. Visibility, hit testing and floating state are combined consistently.

The same @GuiWidget/@GuiProperty/@GuiEvent schemas generate builders and ML/CSS support. Columns, DividerSize, Vertical, OffsetX/OffsetY and Delay are style properties; values, keys, Open and binding policy are configured in code/ML. Rebuild native providers and consumers for GUI contract **0.1.1**, with bExtendedControls=true for backends implementing these controls.

Examples: [entry](../../../../Examples/AdditionalControls/main.vs), [ML](../../../../Examples/AdditionalControls/page.vswml), [CSS](../../../../Examples/AdditionalControls/page.vswcss).

Literal Image/Text content participates as cells before authored children. Splitter defaults to a 120-unit height.

The divider is clamped to the available split extent when the container is smaller than DividerSize. RadioGroup callbacks observe the complete updated selection; a callback may select another choice without a stale Selected event from the original request. NumberBox also commits its draft when window focus or input state is lost. A failed commit preserves its original error even if host input cleanup also fails; cleanup is retried on a later Update once the host recovers. Numeric input accepts at most one leading sign.

When replacing FTextInputServices, failure to stop the old service preserves that service and logical focus for a retry. Focus changes revalidate the context and target after numeric commit callbacks, since those callbacks can close the context or remove the target.

## Focus visibility and callback errors

After layout, Tab/Shift+Tab and Focus() use the last completed geometry to reveal the target in ancestor ScrollViews with the smallest necessary movement, from inner to outer. Oversized controls do not oscillate between edges. Floating content does not scroll its logical ancestors. Scrolled is emitted only when an offset must change; bound scroll properties keep their Getter and the application accepts candidate offsets through the event. Geometry reflects the change on the next Update. Pointer focus does not scroll the container, keeping the pressed control stationary under the pointer.

Direct Activate/Focus calls and input dispatch share callback exception handling. Standard and other exceptions become gui.callback_exception errors instead of escaping an error-returning interface. Already committed values and application side effects are not rolled back; handle the error before retrying.

The framework retains the last window coordinates of each mouse and re-hit-tests after layout. Hover styles and automatic ToolTips refresh on a subsequent Update without synthesizing pointer input events. Cancellation, device removal, input reset, focus loss and close discard the corresponding positions, preventing stale hover when controls move under a stationary mouse.

## Mouse cursors and capture

`Cursor` is an inherited, style-enabled `VS::Gui::Cursor` property. Code builders and `SetCursor` accept a value or `std::attribute<VS::Gui::Cursor>`; ML accepts `Cursor="pointer"`, and CSS accepts `cursor: pointer`. `ClearCursor()` exposes the inherited/style source. An explicit `Auto` selects control behavior again.

Auto uses a hand for buttons, check boxes, toggles, radio buttons, sliders and scrollbars; a text cursor for text/number/password inputs; and the appropriate resize cursor only over a Splitter divider. Ordinary content uses the arrow. Disabled controls use the arrow unless explicitly styled, for example `:disabled { cursor: not-allowed; }`. Child content uses its interactive ancestor; portalled content does not acquire its anchor's automatic cursor.

Values: Auto, Default, Text, Wait, Crosshair, Progress, ResizeNWSE, ResizeNESW, ResizeEW, ResizeNS, Move, NotAllowed, Pointer, None. CSS marks are auto, default, text, wait, crosshair, progress, nwse-resize, nesw-resize, ew-resize, ns-resize, move, not-allowed, pointer, none. None hides the cursor. The Showcase chart demonstrates a crosshair override.

Application connects cursor and mouse capture through advertised Platform capabilities. Embedded hosts may supply `FContext::SetPointerServices(FPointerServices, Error)`; callbacks run on the owner thread and must not mutate GUI state. Without these optional services, layout and internal pointer routing still work. Cursor changes are cached and begin only after mouse input; touch input never requests mouse capture.

Mouse dragging retains its cursor and window capture outside the client area. Release, cancellation, device removal, focus/lifecycle reset and close release the appropriate state. Layout/style updates also refresh a stationary cursor. Platform `MouseEntered`/`MouseLeft` are window-wide notifications, exposed as root events `mouse-entered`/`mouse-left`; leaving clears hover and ToolTip state without cancelling a drag or keyboard focus. External hosts forward these notifications as well as ordinary pointer events.

Service errors and exceptions are returned through the normal error API. Failed capture acquisition rolls back logical capture. Failed cursor updates are retried by Update; service replacement and Close preserve unfinished cleanup for retry. Detaching restores a visible default cursor and releases capture owned by this context. Native providers and consumers must be rebuilt together for GUI contract **0.1.1**.

Capture synchronization runs before cursor evaluation, so a cursor error cannot prevent release of native mouse capture. A host supplying only SetMouseCapture does not evaluate Cursor for that service. Public pointer operations keep the context alive through host callbacks, including when the host drops its last reference.

If the native window has already closed, Application treats input deactivation, capture release and restoration of the default visible cursor as completed cleanup. Requests to activate input or apply another cursor still fail.

Application.Run treats WaitMilliseconds as the maximum idle frame interval (16 ms by default), subtracting the preceding update and render time before waiting; pending events still wake it early. Tick adds no wait and is scheduled by the host. Unconfigured native properties skip script polling; configured attributes keep their normal synchronization semantics. Direct values and Clear detach the corresponding binding.

`Run(beforeUpdate)` and `Tick(beforeUpdate)` invoke the callback before GUI synchronization, normally after input delivery. When the root has no layout yet, a bootstrap update before input delivery also invokes it; the callback can therefore run twice during the first step. Use the Run overload for animation/model updates with the existing event-wait budget; Tick remains an unpaced single step. Exceptions propagate normally and root-view refresh still runs during cleanup.

Bound getters, including optional readers returning no value, are evaluated in each required synchronization round. Direct setters and Clear replace or remove the local binding. Change notifications commit only after a successful hook; failed hooks can be retried by a later update. Custom property values retain the normal language copy behavior.

GUI inherits the host execution configuration. Use `--backend=vm` or `--jit-backend="/path/to/Jit" --backend=jit`, placing these options before `gui-preview`. C++ hosts set FEngineConfig.ExecutionBackend and JitBackendFactory (from LoadJitBackend). Mixed execution without a JIT factory, debugging, retained initialization and bounded cleanup use the VM. Ordinary functions in a persistent session may use the selected JIT backend. Check the return value of `VeroScriptSession::ApplyConfig` and propagate its rejection reason before loading sources.

Formal archives are `vs-gui` and `vs-rmlui`, with versions defined by release metadata. Gui pins Platform/Graphics/Image/Font; RmlUi pins Gui. Backends remain selectable. Native consumers use `find_package(VeroScriptGui)`; script consumers add `COMPONENTS Bindings` and the matching Core/C++ Runtime prefixes. The `VeroScriptGuiFramework` compatibility entry remains available.

## Script and host contracts

Closed property and event value types preserve Core's type and scalar template arguments, including bool, fixed-width integers and enums. Generated source retains the exact integer type and range, decodes nested types in builtin containers, references and function signatures, and preserves native array dimension order. GuiWidget requires a closed, nonstatic class at namespace scope; local and nested widget declarations are rejected. GuiMethod and property change hooks must also be nontemplate instance methods; neither type nor value template parameters are accepted.

CompileGuiSource/File preserve FCompileOptions.ConstantEvaluationLimits and use Core's constexpr evaluation. Dynamic attributes continue to be sampled by the normal GUI synchronization rounds.

Context.Post keeps posted callbacks and their captures alive through execution or discard. Captured objects may remain alive until deferred cleanup runs. Context.SetFocus delivers control notifications synchronously.

TextArea retains logical CR, LF, CRLF, NEL (U+0085), line separator (U+2028) and paragraph separator (U+2029); each terminator creates one displayed hard break, and CRLF is consumed together by Backspace/Delete. Code-point selections and clipboard text still count/preserve the original sequence. Single-line controls remove these separators. Tabs retain their existing filtering behavior; placeholder whitespace is normalized for display without changing the value.

Caret affinity distinguishes leading/trailing visual positions at directional boundaries. Horizontal navigation crosses soft lines in the paragraph's base direction and enters at the corresponding visual edge, including lines containing only an opposite-direction run. This policy does not promise identical behavior to every system editor. Full grapheme navigation and exact internal ligature carets remain separate capabilities.

Host text-input service callbacks may query GUI state. Context and mounted-widget mutations, service replacement and Close are rejected until the callback returns.

Rebuild GuiScript and native providers with the matching Core and C++ Runtime SDKs after Core public ABI or bytecode-layout changes. Matching product version strings alone does not establish binary compatibility; packaged bytecode must be regenerated for the matching layout.

The CLI demand importer inspects small local `.vslib` macro interfaces before adding the GUI framework. Ordinary library declarations and non-GUI macros do not require another framework import; GUI macros and opaque imports retain the conservative path. This avoids repeating a packaged standard-library declaration.

## Notification bindings and batches

`Observable<T>.Get()/Set()` stores observable data. `Set` notifies only when `==` detects a changed value; call inherited `Notify()` after changing the contents of a mutable object. `ChangeSignal.Notify()` also represents inputs maintained by another service. Use `AsBinding()` for one observable, or `Binding<T>(getter, inputs)` for a computation with an explicit array of signals. Signals are not automatic tracking of arbitrary script fields. Repeated AsBinding calls reuse a live binding through a weak cache, without retaining a source/binding cycle.

Notification getters must be read-only and list every changing input. Results are cached by input revisions, including optional empty results, and a shared Binding instance computes once for its consumers. `Binding<T>::FromAttribute(attribute, inputs)` explicitly opts an existing attribute into this notification contract; unknown dependencies, time and weak-owner expiration still need suitable signals. Direct `std::attribute<T>` bindings retain their existing polling and fallback behavior.

Repeated notifications coalesce in a native dirty-source queue; each affected widget is woken once per publication. `BindingUpdates.Batch(work)` supports nesting and defers GUI work to the next safe update; data itself changes immediately. It does not roll back assignments when work throws. Context.Update, Application.Tick and Application.Run cannot execute inside a batch. Widget construction retains the normal initialization and child/parent lifecycle order.

The queue publishes after every frame service captured its snapshot. Existing widget synchronization reads the affected attributes; unchanged computed values do not emit change hooks or invalidate native properties. Frame-snapshot inputs need signals emitted after capture; unobservable frame data should use ordinary dynamic attributes. Programmatic updates do not emit user input events. SetX/ClearX disconnect the prior subscription, and IsXBound retains its attribute semantics. Disposal or final release disconnects subscriptions; unmounting retains configuration. Failed getters preserve the last committed value and use the existing lifecycle retry path. FromAttribute captures the supplied attribute configuration; replacing the original attribute later does not replace this binding. FromAttribute keeps fallback polling so default attributes and native styles can change independently; its supplied getter result is still cached by notification revisions.

The same setters are available in VS, generated builders and VS code inside .vswml. Existing `bind:` expressions keep their polling semantics; .vswcss remains a style layer. Notifications and flushing belong to the GUI owner thread.

```cpp
auto amount = VS::Gui::Observable<double>(0.25);
VS::Gui::ProgressBar bar = VS::Gui::ProgressBar::Create().Value(amount.AsBinding());

// Setters need a mutable value capture; read-only getters do not.
VS::Gui::BindingUpdates::Batch([amount]() mutable -> void
{
	amount.Set(0.4);
	amount.Set(0.6);
});
// The next Context.Update/Application.Tick commits 0.6 once.

array<VS::Gui::ChangeSignal> inputs = {amount};
VS::Gui::Binding<string> caption = VS::Gui::Binding<string>([amount]() -> string
{
	return to_string(amount.Get());
}, inputs);
VS::Gui::Text label = VS::Gui::Text::Create().Text(caption);
```

## Collections, navigation and dialogs

`ListView` accepts `ListItem`; `TreeView` accepts nested `TreeItem`. Both provide single selection through the string `SelectedKey`, `GetActualSelectedKey()`, `FindItem(key)`, `Select(key)`, `Activate(key)` and typed `SelectionChanged` / `ItemActivated` events. Item `Key` falls back to `Id`, then its ordinal; use explicit stable keys for changing data. Empty or unmatched models select nothing. `Select` requests a keyed selection; `Activate` additionally requires an interactive item. Pointer release or Enter/Space activates; arrows and Home/End navigate. Tree Left/Right collapse/expand or enter/leave a branch. `Expanded` supports attributes, `IsActuallyExpanded()` reads the current candidate, and `ExpandedChanged(TreeItem,bool)` reports requests. Collapsing preserves child instances and their own Visibility properties.

`TableView` uses `TableColumn` and `TableRow`. A column has `Key`, `ColumnWidth` (positive pixels) and `Sortable`; a row has one ordinary widget cell per column and a stable key. Code may use `SetColumns(array<TableColumn>)`, `SetRows(array<array<string>>)`, `GetCellText` / `SetCellText`, and `SortBy(columnKey,ascending)`. Sorting is stable, ordinal UTF-8 text ordering, and preserves row identities/selection; it does not infer numeric or locale collation rules. `AutoSort` defaults true for header clicks. Events include `ColumnClicked`, `Sorted`, `SelectionChanged` and `ItemActivated`. This is a single-selection widget table, with optional data-source virtualization and without spreadsheet editing. Bulk `SetChildren` / `SetRows` avoids rebuilding the index after each individual insertion.

`ComboBox` accepts `ListItem` children and provides `SelectedKey`, `Open`, `Placeholder`, `DropDownHeight`, `Select`, `SetItems`, `GetList`, `RequestOpen`, and selection/open events. It owns its display Text, using the selected item's Text or Placeholder. `TabView` accepts `TabItem` pages with Key/Title. An unbound initial selection chooses the first enabled page; `Select` changes it and raises `SelectionChanged(TabView,string)`. Pages stay mounted; inactive content is suppressed separately from authored Visibility. Headers support Left/Right/Home/End.

`Menu` contains `MenuItem`, whose children form submenus. `Command` identifies an action; `Checkable` / `Checked` support checked actions. `Invoked` reports the command on the item and menu. Submenus use below/right portals, keyboard navigation and Escape; a single outside pointer press closes the complete active chain and is consumed. `Invoke`, `OpenMenu` and `CloseMenu` reuse the same interaction path. Native operating-system menus and accelerator registration are separate host concerns.

`Popup` remains nonmodal by default. `Placement` accepts `below`, `right`, or `center`; `MatchAnchorWidth` is optional. `DismissOnEscape` can disable Escape dismissal. `GetDismissReason()` reads `escape`, `outside`, or an empty string for explicit requests. `Dialog` defaults to Modal=true, centered placement, and DismissOnOutside=false. Use `Show()` and `Close(result)` in the existing application loop; `Closed(Dialog,string)` reports a close request/result. It blocks background pointer/keyboard focus, confines Tab navigation, and restores eligible prior focus. It does not start a nested event loop. Open binding candidates retain their source and are restored by subsequent synchronization.

All controls use the existing builders, attributes, events and ML/CSS. Builders use `.OnClicked(handler)`; instances use `.AddClicked(handler)` and the corresponding AddXxx methods. `SetChildren` on composite widgets targets authored content; `GetChildren` / parent queries retain the actual tree, and `GetContentChildren` returns the authored content container. Structural changes are performed outside getters/update hooks, or via Context.Post. See the [complete example](../../../../Examples/CompositeControls/main.vs), its [ML](../../../../Examples/CompositeControls/page.vswml) and [CSS](../../../../Examples/CompositeControls/page.vswcss). Grid still intentionally supplies equal columns; independent table widths use TableColumn.ColumnWidth.

`MenuItem.IsActuallyChecked()` reads the current input candidate; consecutive requests toggle it until synchronization restores a bound Checked model. Dialog Closed uses a result snapshot taken before OpenChanged callbacks, including callbacks that reopen the dialog.

Data-backed lists and tables support automatic fixed-height virtualization: `ListView.SetVirtualItems(labels, rowHeight=28, overscan=2)` and `TableView.SetVirtualRows(rows, rowHeight=28, overscan=2)` create/recycle only viewport rows. `SetVirtualItemText(index,text)` and `SetCellText(key,column,text)` update stored data, including offscreen rows. `SetVirtualSource(keys,rowHeight,factory,overscan=2)` supports custom keyed rows: the factory receives `(string key, CollectionItem recycled)` and returns the same row or an unattached replacement (TableView requires TableRow). It must fully reset state belonging to the old key. `RefreshVirtualItems()` rebinds visible rows; `SetVirtualOrder(keys)` preserves all keys for external sorting. Built-in virtual tables retain stable text sorting; custom sources handle ColumnClicked and supply their order.

`GetItemCount()` counts logical rows; `GetItems()`, `GetRealizedItemCount()` and `FindItem()` describe realized rows. Selection survives recycling; use `ScrollToItem(key)` to reveal an offscreen key. Focused, pressed or captured rows stay realized until input ownership ends. Realization runs in the safe deferred phase of Context.Update; changes first discovered during layout are realized at the next update. Rows have a fixed border-box height and clip their content; custom row padding must fit that extent. Keys are nonempty/unique, overscan is 0..256, and total row extent is limited to 16,777,216 logical pixels. Source replacement/ordering cannot occur inside its factory or a getter. Factory failures propagate and can retry on a later update; returned rows transfer ownership to the view. Existing authored children, SetItems and SetRows keep their original widget lifetime behavior. Variable-height rows and tree virtualization are not included.

A ScrollToItem request made before the first layout is corrected when the actual viewport is known; a later explicit scroll takes precedence.

## Context backend defaults

For `VS::Gui::Context("", width, height)` using the standard application-factory bridge, an explicit constructor backend wins; otherwise the invocation choice is used, then the application/environment factory default. If neither supplies a choice, GUI falls back to `rmlui`. A host backend-query callback returning an empty string leaves that layer unspecified; it does not disable the service. Explicit `auto` or `none` remains explicit and is not replaced by `rmlui`. Environment configuration that stores an empty backend value is normalized to `none`, as described in the common selection rules. A custom host Context factory owns its own backend-name handling.

<a id="context-callback-reentrancy"></a>

## Context callback restrictions

Keep Context configuration changes outside callbacks invoked while replacing themes, frame services, pointer services or text-input services. Recursive configuration of the same Context is rejected. Use `Post` to schedule later work on the owner thread.

<!-- docs-navigation -->

---

[GUI](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](Framework.md) · [fr-FR](../../fr-FR/Reference/Framework.md) · [ja-JP](../../ja-JP/Reference/Framework.md) · [ko-KR](../../ko-KR/Reference/Framework.md) · [zh-CN](../../zh-CN/Reference/Framework.md) · [zh-TW](../../zh-TW/Reference/Framework.md)
