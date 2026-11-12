// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiMarkup.h"
#include "VeroScriptGuiContext.h"
#include <iostream>
#include <stdexcept>

namespace
{
	using namespace VeroScript;
	using namespace Gui;

	void Require(bool Value, const std::string& Message)
	{
		if (!Value)
		{
			throw std::runtime_error(Message);
		}
	}

	class FBackend final : public IBackendContext
	{
	public:
		//~ Begin IBackendContext Interface
		FCapabilities GetCapabilities() const noexcept override
		{
			return {true};
		}
		bool Synchronize(const FViewport&, const std::vector<FBackendNode>& Value, std::string&) override
		{
			Nodes = Value;
			return true;
		}
		bool Update(double, std::vector<FLayoutBox>& Boxes, std::string&) override
		{
			for (const auto& Node : Nodes)
			{
				Boxes.push_back({Node.Id, {0, 0, 100, 100}, {0, 0, 100, 100}});
			}
			return true;
		}
		bool Render(bool& bPresented, std::string&) override
		{
			bPresented = false;
			return true;
		}
		bool Close(std::string&) override
		{
			return true;
		}
		//~ End IBackendContext Interface

		std::vector<FBackendNode> Nodes;
	};

	std::shared_ptr<FWidget> Compile(const std::string& Text, FMarkupCompileOptions Options = {})
	{
		FMarkupDiagnostic Diagnostic;
		auto Program = FMarkupProgram::Compile({"page.vswml", Text}, Options, Diagnostic);
		Require(Program != nullptr, Diagnostic.Format());
		auto Root = Program->Create({}, Diagnostic);
		Require(Root != nullptr, Diagnostic.Format());
		return Root;
	}

	double Number(const std::shared_ptr<FWidget>& Widget, EProperty Property = EProperty::Gap)
	{
		std::string Error;
		FPropertyValue Value;
		Require(Widget && Widget->Get(Property, Value, Error), Error);
		return std::get<double>(Value);
	}

	void CheckStyles()
	{
		auto Root = Compile(R"(<Widget id="root"><style>
            :root { --space: 3px; padding: 2px 4px; }
            Widget { gap: 1px; }
            .row { Gap: var(--space); }
            #missing, .row { gap: 4px; }
            Widget > .row { gap: 5px; }
            .outer .row { gap: 6px; }
            .row:hover { opacity: 0.5; }
            .row:active { gap: 7px; }
            .row:focus { gap: 8px; }
            .row:disabled { gap: 9px; }
        </style><Widget class="outer"><Widget id="row" class="row" Focusable="true" /></Widget></Widget>)");
		std::string Error;
		auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		Require(Context->SetRoot(Root, Error) && Context->Update(0, Error), Error);
		auto Row = FMarkupProgram::Find(Root, "row");
		Require(Number(Row) == 6, "selector cascade or descendant matching");
		Require(Row->Set(EProperty::Gap, 42.0, Error) && Context->Update(1, Error) && Number(Row) == 42,
		    "local value did not override CSS");
		Require(Row->Clear(EProperty::Gap, Error) && Row->SetClasses({"other"}, Error) && Context->Update(2, Error) &&
		        Number(Row) == 1,
		    "class mutation did not remove stale style");
		Require(Row->SetClasses({"row"}, Error) && Context->Update(3, Error), Error);
		Platform::FEvent Event;
		Event.WindowId = 1;
		Event.Type = Platform::EEventType::PointerMove;
		Event.X = Event.Y = 20;
		bool bHandled = false;
		Require(Context->ProcessEvent(Event, bHandled, Error) && Context->Update(4, Error), Error);
		Require(Number(Row, EProperty::Opacity) == 0.5,
		    "hover selector was not live: " + std::to_string(Number(Row, EProperty::Opacity)));
		Event.Type = Platform::EEventType::PointerDown;
		Event.Button = 1;
		Require(Context->ProcessEvent(Event, bHandled, Error) && Context->Update(5, Error) && Number(Row) == 7,
		    "active selector");
		Require(Context->SetFocus(Row, Error) && Context->Update(6, Error) && Number(Row) == 8, "focus selector");
		Require(Context->CapturePointer(Event.PointerId, Row, Error), Error);
		Event.Type = Platform::EEventType::InputStateReset;
		Event.WindowId = 0;
		Require(Context->ProcessEvent(Event, bHandled, Error) && !Context->GetFocus() && Context->Update(6.5, Error) &&
		        Number(Row) == 6 && Number(Row, EProperty::Opacity) == 1,
		    "input reset retained CSS state");
		Require(Row->Set(EProperty::Enabled, false, Error) && Context->Update(7, Error) && Number(Row) == 9,
		    "disabled selector");
		Event.Type = Platform::EEventType::Minimized;
		Event.WindowId = 1;
		Require(Context->ProcessEvent(Event, bHandled, Error) && !Context->GetFocus(), "minimize retained focus");
		Require(Context->Close(Error), Error);
	}

	void CheckTextDirection()
	{
		auto Root = Compile(R"(<Widget Direction="rtl" Language="ar"><style>
            .auto { direction: auto; language: he; }
        </style><Widget id="inherited"/><Widget id="styled" class="auto"/></Widget>)");
		std::string Error;
		auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		Require(Context && Context->SetRoot(Root, Error) && Context->Update(0, Error), Error);
		auto Inherited = FMarkupProgram::Find(Root, "inherited");
		auto Styled = FMarkupProgram::Find(Root, "styled");
		auto TextProperty = [&](const auto& Widget, EProperty Property)
		{
			FPropertyValue Value;
			Require(Widget && Widget->Get(Property, Value, Error), Error);
			return std::get<std::string>(Value);
		};
		Require(TextProperty(Inherited, EProperty::Direction) == "rtl" &&
		        TextProperty(Inherited, EProperty::Language) == "ar" &&
		        TextProperty(Styled, EProperty::Direction) == "auto" &&
		        TextProperty(Styled, EProperty::Language) == "he",
		    "ML/CSS text direction or language inheritance");
		Require(Root->Set(EProperty::Direction, std::string("ltr"), Error) &&
		        Root->Set(EProperty::Language, std::string("en"), Error) && Context->Update(1, Error),
		    Error);
		Require(TextProperty(Inherited, EProperty::Direction) == "ltr" &&
		        TextProperty(Inherited, EProperty::Language) == "en" &&
		        TextProperty(Styled, EProperty::Direction) == "auto",
		    "direction mutation retained a stale inherited style");
		Require(!Root->Set(EProperty::Direction, std::string("sideways"), Error), "invalid text direction accepted");
		Require(Context->Close(Error), Error);
	}

	void CheckScopes()
	{
		FMarkupCompileOptions Options;
		Options.Read = [](const auto&, const auto& Reference, auto& Source, auto&)
		{
			Source = {Reference,
			    R"(<component type="Card" mark="card"><template><Widget id="inside" class="part" /></template>
                <style>:root { gap: 2px; } .part { gap: 3px; }</style></component>)"};
			return true;
		};
		auto Root =
		    Compile(R"(<Widget><import src="card.vswml"/><style>#one { gap: 10px; } .part { gap: 99px; }</style>
            <card id="one"/><Card id="two"/></Widget>)",
		        Options);
		auto First = FMarkupProgram::Find(Root, "one");
		auto Second = FMarkupProgram::Find(Root, "two");
		Require(First && Second && First != Second && !FMarkupProgram::Find(Root, "inside"),
		    "component query scope leaked");
		auto Inside = FMarkupProgram::Find(First, "inside");
		Require(Inside && FMarkupProgram::Find(Second, "inside") != Inside, "component templates shared nodes");
		std::string Error;
		auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		Require(Context->SetRoot(Root, Error) && Context->Update(0, Error), Error);
		Require(Number(First) == 10 && Number(Second) == 2 && Number(Inside) == 3,
		    "component style scope or usage priority");
		Require(!Second->SetId("one", Error) && Second->GetId() == "two", "duplicate id update was not atomic");
		Require(Second->SetId("renamed", Error) && !FMarkupProgram::Find(Root, "two") &&
		        FMarkupProgram::Find(Root, "renamed") == Second,
		    "id index was stale");
		Require(Context->Close(Error), Error);
	}

	void CheckVariablesAndTheme()
	{
		auto Root = Compile(
		    R"(<Widget><Widget id="child" class="target"/><style>:root { --x: 4px; --alias: var(--x); } .target { --x: 9px; gap: var(--missing, var(--alias)); }</style></Widget>)");
		std::string Error;
		FMarkupDiagnostic Diagnostic;
		auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		Require(Context->SetRoot(Root, Error) && Context->Update(0, Error), Error);
		auto Child = FMarkupProgram::Find(Root, "child");
		Require(Number(Child) == 4, "nested variable fallback or inheritance");
		auto Theme = FStyleSheet::Compile({"theme.vswcss", "* { opacity: 0.25; gap: 90px; }"}, Diagnostic);
		Require(Theme && FMarkupProgram::SetTheme(Root, Theme, Error) && Context->Update(1, Error),
		    Diagnostic.Format() + Error);
		Require(Number(Child) == 4 && Number(Child, EProperty::Opacity) == 0.25, "theme priority");
		Require(FMarkupProgram::SetTheme(Root, {}, Error) && Context->Update(2, Error) &&
		        Number(Child, EProperty::Opacity) == 1,
		    "removed theme retained values");
		Theme = FStyleSheet::Compile({"cycle.vswcss", "* { --a: var(--b); --b: var(--a); opacity: var(--a); }"},
		    Diagnostic);
		Require(Theme && FMarkupProgram::SetTheme(Root, Theme, Error) && !Context->Update(3, Error) &&
		        Error.find("style_variable_cycle") != std::string::npos &&
		        Error.find("cycle.vswcss") != std::string::npos,
		    "variable cycle or source diagnostic missing");
		bool bPresented = true;
		Require(!Context->Render(bPresented, Error) && Context->Close(Error), "invalid stylesheet remained renderable");
	}

	void CheckCompiledStyleReuse()
	{
		std::string Error;
		FMarkupDiagnostic Diagnostic;
		auto Root = FWidget::Create();
		auto Parent = FWidget::Create();
		auto Child = FWidget::Create();
		const auto Scope = FWidget::AllocateNameScope();
		Require(Root->AddChild(Parent, Error) && Parent->AddChild(Child, Error) &&
		        Root->AdoptDeclarationScope(Scope, Error),
		    Error);
		for (const auto& Widget : {Root, Parent, Child})
		{
			Require(Widget->SetStyleType(Scope, "Widget", {}, Error), Error);
		}
		Require(Child->SetClasses({"target"}, Error) && Child->SetId("item", Error), Error);
		auto Sheet = FStyleSheet::Compile({"compiled.vswcss", R"(
            Widget { gap: 1px; }
            .hot .target { gap: 7px; }
            #renamed { gap: 8px; }
            #renamed:disabled { gap: 9px; }
            .target:checked { opacity: 0.4; }
            .target:read-only { padding: 3px; }
            .hot:focus-within { opacity: 0.6; }
            .target:hover { border-width: 2px; }
            .hot:active { border-radius: 5px; }
        )"},
		    Diagnostic);
		Require(Sheet && Root->AttachStyleSheet(Scope, Sheet, Error), Diagnostic.Format() + Error);
		auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		Require(Context && Context->SetRoot(Root, Error) && Context->Update(0, Error) &&
		        Context->Update(1, Error) && Number(Child) == 1,
		    Error);
		Require(Parent->SetClasses({"hot"}, Error) && Context->Update(2, Error) && Number(Child) == 7,
		    "cached selectors missed an ancestor class change");
		Require(Child->SetId("renamed", Error) && Context->Update(3, Error) && Number(Child) == 8,
		    "cached selectors missed an id change");
		Require(Child->Set(EProperty::Focusable, true, Error) && Context->SetFocus(Child, Error) &&
		        Context->Update(4, Error) && Number(Parent, EProperty::Opacity) == 0.6,
		    "cached selectors missed focus-within");
		double FocusTime = 4;
		for (const auto& Invalid : std::vector<std::pair<EProperty, FPropertyValue>>{
		         {EProperty::Enabled, false}, {EProperty::Visibility, EVisibility::Hidden},
		         {EProperty::Focusable, false}})
		{
			Require(Child->Set(Invalid.first, Invalid.second, Error) && Context->Update(FocusTime += 0.1, Error) &&
			        Number(Parent, EProperty::Opacity) == 1,
			    "cached focus-within ignored focused descendant eligibility");
			Require(Child->Clear(Invalid.first, Error) && Child->Set(EProperty::Focusable, true, Error) &&
			        Context->SetFocus(Child, Error) && Context->Update(FocusTime += 0.1, Error) &&
			        Number(Parent, EProperty::Opacity) == 0.6,
			    "cached focus-within did not recover after focused descendant eligibility");
		}
		Require(Context->SetFocus({}, Error) && Context->Update(5, Error) && Number(Parent, EProperty::Opacity) == 1,
		    "cached selectors retained focus-within");
		FControlOptions Control;
		Control.Kind = EControlKind::CheckBox;
		Control.bChecked = true;
		Control.bReadOnly = true;
		Require(Child->SetControlOptions(Control, Error) && Context->Update(6, Error) &&
		        Number(Child, EProperty::Opacity) == 0.4,
		    "cached selectors missed checked state");
		FPropertyValue Padding;
		Require(Child->Get(EProperty::Padding, Padding, Error) && std::get<FInsets>(Padding).Top == 3,
		    "cached selectors missed read-only state");
		Control.bChecked = false;
		Control.bReadOnly = false;
		Require(Child->SetControlOptions(Control, Error) && Context->Update(7, Error) &&
		        Number(Child, EProperty::Opacity) == 1,
		    "cached selectors retained control state");
		Control.Kind = EControlKind::None;
		Platform::FEvent Pointer;
		Pointer.WindowId = 1;
		Pointer.PointerId = 17;
		Pointer.X = 5;
		Pointer.Y = 5;
		Pointer.Type = Platform::EEventType::PointerMove;
		bool Handled = false;
		Require(Child->SetControlOptions(Control, Error) && Context->ProcessEvent(Pointer, Handled, Error) &&
		        Context->Update(7.1, Error) && Number(Child, EProperty::BorderWidth) == 2,
		    "cached selectors missed pointer hover");
		Require(Parent->Set(EProperty::HitTest, EHitTestMode::None, Error) && Context->Update(7.2, Error) &&
		        Number(Child, EProperty::BorderWidth) == 0,
		    "cached selectors ignored ancestor hit-test eligibility");
		Require(Parent->Clear(EProperty::HitTest, Error) && Context->ProcessEvent(Pointer, Handled, Error) &&
		        Context->Update(7.3, Error) && Number(Child, EProperty::BorderWidth) == 2,
		    "cached selectors did not restore pointer hover");
		Pointer.Type = Platform::EEventType::PointerDown;
		Pointer.Button = 1;
		Pointer.Buttons = 1;
		Require(Context->ProcessEvent(Pointer, Handled, Error) && Context->Update(7.4, Error) &&
		        Number(Parent, EProperty::CornerRadius) == 5,
		    "cached selectors missed active input");
		Require(Parent->Set(EProperty::Opacity, 0.8, Error) &&
		        Child->Set(EProperty::Width, FLength{ELengthUnit::Pixel, 40}, Error) && Context->Update(7.5, Error) &&
		        Number(Child, EProperty::BorderWidth) == 2 && Number(Parent, EProperty::CornerRadius) == 5,
		    "unrelated presentation changes altered cached input styles");
		Pointer.Type = Platform::EEventType::PointerUp;
		Pointer.Buttons = 0;
		Require(Context->ProcessEvent(Pointer, Handled, Error) && Parent->Clear(EProperty::Opacity, Error) &&
		        Context->Update(7.6, Error) && Number(Parent, EProperty::CornerRadius) == 0,
		    "cached selectors retained released input");

		bool Enabled = true;
		int GetterCalls = 0;
		Require(Child->Bind(EProperty::Enabled, [&](auto& Value, auto&)
		{
			++GetterCalls;
			Value = FPropertyValue(Enabled);
			return true;
		}, Error) &&
		        Context->Update(8, Error),
		    Error);
		const auto BeforeGetter = GetterCalls;
		Enabled = false;
		Require(Context->Update(9, Error) && Number(Child) == 9 && GetterCalls > BeforeGetter,
		    "cached selector matching froze a dynamic eligibility getter");
		Require(Child->Clear(EProperty::Enabled, Error), Error);

		int DecoderCalls = 0;
		bool FailDecode = false;
		double Accent = 2;
		FStyleProperty Custom;
		Custom.Name = "accent";
		Custom.Decode = [&](std::string_view, FStyleValue& Value, std::string& Failure)
		{
			++DecoderCalls;
			if (FailDecode)
			{
				Failure = "intentional style failure";
				return false;
			}
			Value = FPropertyValue(Accent);
			return true;
		};
		Require(Child->RegisterStyleProperty(std::move(Custom), Error), Error);
		auto Typed = FStyleSheet::CompileTyped({"typed.vswcss", ".target { accent: 2; }"}, Diagnostic);
		Require(Typed && Root->AttachStyleSheet(Scope, Typed, Error) && Context->Update(10, Error),
		    Diagnostic.Format() + Error);
		const auto BeforeDecode = DecoderCalls;
		Require(Context->Update(11, Error) && DecoderCalls > BeforeDecode,
		    "selector reuse incorrectly cached a custom decoder");
		Accent = 3;
		FailDecode = true;
		Require(!Context->Update(12, Error) && Error.find("intentional style failure") != std::string::npos,
		    "cached selector values hid a decoder failure");
		FailDecode = false;
		Require(Context->Update(13, Error), "failed cached style did not retry: " + Error);
		std::optional<FStyleValue> StyledAccent;
		Require(Child->GetStyleValue("accent", StyledAccent, Error) && StyledAccent &&
		        std::get<double>(std::get<FPropertyValue>(*StyledAccent)) == 3,
		    "selector reuse froze external decoder inputs");

		Require(Parent->RemoveChild(Child, Error) && Root->AddChild(Child, Error) && Context->Update(14, Error) &&
		        Number(Child) == 8,
		    "scene topology cache missed reparenting");
		auto Theme = FStyleSheet::Compile({"replacement.vswcss", ".target { gap: 15px; }"}, Diagnostic);
		Require(Theme && Context->SetTheme(Theme, Error) && Context->Update(15, Error) && Number(Child) == 8,
		    "scene cache changed stylesheet/theme precedence");
		Require(Context->SetTheme({}, Error) && Context->Update(16, Error), Error);

		int ResolverCalls = 0;
		std::string ResourcePrefix = "first:";
		auto Images = FStyleSheet::Compile({"resources.vswcss", ".target { --picture: url(image.png); background-image: var(--picture); }"},
		    Diagnostic, [&](const auto&, const auto& Reference, auto& Identity, auto&)
		{
			++ResolverCalls;
			Identity = ResourcePrefix + Reference;
			return true;
		});
		Require(Images && Root->AttachStyleSheet(Scope, Images, Error) && Context->Update(17, Error),
		    Diagnostic.Format() + Error);
		const auto BeforeResolve = ResolverCalls;
		ResourcePrefix = "second:";
		Require(Context->Update(18, Error) && ResolverCalls > BeforeResolve, "selector reuse cached a resource resolver");
		FPropertyValue Image;
		Require(Child->Get(EProperty::BackgroundImage, Image, Error) &&
		        std::get<std::string>(Image).find("second:") != std::string::npos,
		    "selector reuse froze resource resolution");
		Require(Root->RemoveChild(Child, Error), Error);
		std::weak_ptr<FWidget> Removed = Child;
		Child.reset();
		Require(Removed.expired(), "scene cache retained a removed widget");
		Require(Context->Update(19, Error) && Context->SetRoot({}, Error) && Context->Close(Error), Error);

		auto Other = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		auto Plain = FWidget::Create();
		Require(Plain->SetClasses({"target"}, Error) && Other->SetRoot(Plain, Error) &&
		        Other->SetTheme(Theme, Error) && Other->Update(0, Error) && Number(Plain) == 15,
		    "new context reused another scene's styles");
		Theme = FStyleSheet::Compile({"second-theme.vswcss", ".target { gap: 21px; }"}, Diagnostic);
		Require(Theme && Other->SetTheme(Theme, Error) && Other->Update(1, Error) && Number(Plain) == 21 &&
		        Other->SetTheme({}, Error) && Other->Update(2, Error) && Number(Plain) == 0,
		    "scene cache retained a replaced or removed theme");
		Require(Other->SetRoot(Root, Error) && Other->Update(3, Error) && Number(Root) == 1 && Other->Close(Error),
		    "moving a root to another context retained an obsolete scene");
	}

	void CheckDiagnostics()
	{
		FMarkupDiagnostic Diagnostic;
		for (const auto& Source : std::vector<std::string>{
		         "<Widget>", "<Widget/><Widget/>", "<Widget Id='wrong'/>", "<Widget Gap='1' gap='2'/>",
		         "<Widget Gap='1' bind:gap='Read'/>", "<Widget>raw</Widget>", "<Widget xmlns:bind='redefined'/>",
		         "<Widget on:click='not-a-base-event'/>", "<Widget id='&bogus;'/>", "<Widget id='&;'/>",
		         "<Widget><Widget><style>* { gap: 1px; }</style></Widget></Widget>"})
		{
			Require(!FMarkupProgram::Compile({"bad.vswml", Source}, {}, Diagnostic) &&
			        Diagnostic.Location.Source == "bad.vswml" && !Diagnostic.Message.empty(),
			    "invalid ML accepted: " + Source);
		}
		for (const auto& Source :
		    std::vector<std::string>{"* { unknown: 1; }", "* { opacity: 9; }", "* { padding: -1px; }",
		        "* { padding: 1px nope; }", "* { gap: 1px !important; }",
		        "Widget + Widget { gap: 1px; }", "@keyframes x {}", "*:visited { gap: 1px; }"})
		{
			Require(!FStyleSheet::Compile({"bad.vswcss", Source}, Diagnostic) && !Diagnostic.Message.empty(),
			    "unsupported CSS accepted: " + Source);
		}
		auto Root = Compile("<!-- comment --><Widget id='&#x4e2d;&amp;&quot;'><Widget id='&#128512;' /></Widget>");
		Require(Root->GetId() == "中&\"" && FMarkupProgram::Find(Root, "😀"), "Unicode entities changed");
	}

	void CheckFiles()
	{
		std::string Error;
		FMarkupSource Source;
		Require(ReadMarkupFile({}, std::string(VEROSCRIPT_GUI_MARKUP_EXAMPLES) + "/layout.vswml", Source, Error),
		    Error);
		FMarkupCompileOptions Options;
		Options.Read = ReadMarkupFile;
		FMarkupDiagnostic Diagnostic;
		auto Program = FMarkupProgram::Compile(Source, Options, Diagnostic);
		Require(Program && Program->GetDependencies().size() == 3, Diagnostic.Format());
		auto Root = Program->Create({}, Diagnostic);
		Require(Root != nullptr, Diagnostic.Format());
		auto Context = FContext::Create(std::make_unique<FBackend>(), {960, 600, 1}, Error);
		Require(Context->SetRoot(Root, Error) && Context->Update(0, Error), Error);
		Require(FMarkupProgram::Find(Root, "first") && FMarkupProgram::Find(Root, "second") &&
		        FMarkupProgram::Find(FMarkupProgram::Find(Root, "third"), "accent"),
		    "relative imported example");
		Require(Context->Close(Error), Error);
	}

	void CheckDescendantSelectors()
	{
		std::string Source = "<Widget class='node'><style>Missing";
		for (unsigned Index = 0; Index < 32; ++Index)
		{
			Source += " .node";
		}
		Source += " { gap: 2px; } .node { gap: 1px; }</style>";
		for (unsigned Index = 0; Index < 64; ++Index)
		{
			Source += "<Widget class='node'>";
		}
		for (unsigned Index = 0; Index < 65; ++Index)
		{
			Source += "</Widget>";
		}
		auto Root = Compile(Source);
		std::string Error;
		auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		Require(Context->SetRoot(Root, Error) && Context->Update(0, Error) && Number(Root) == 1, Error);
		Root = Compile(R"(<Widget class='anchor'><style>
			.anchor > .branch .leaf { gap: 7px; }
			.anchor > .branch > .leaf { opacity: 0.25; }
			.anchor .branch > .leaf { height: 35px; }
		</style><Widget class='branch'><Widget><Widget class='branch'><Widget id='leaf' class='leaf'/></Widget></Widget></Widget></Widget>)");
		Require(Context->SetRoot(Root, Error) && Context->Update(1, Error), Error);
		auto Leaf = FMarkupProgram::Find(Root, "leaf");
		FPropertyValue Height;
		Require(Leaf && Number(Leaf) == 7 && Number(Leaf, EProperty::Opacity) == 1 &&
		        Leaf->Get(EProperty::Height, Height, Error) &&
		        std::get<FLength>(Height) == FLength{ELengthUnit::Pixel, 35},
		    "mixed descendant/child selector paths changed");
		Require(Context->Close(Error), Error);
	}

	void CheckImageReferences()
	{
		struct FBindings final : IMarkupScriptContext
		{
			bool ResolveGetter(const FMarkupExpression& Expression, std::weak_ptr<FWidget>, FPropertyGetter& Getter,
			    std::string&) override
			{
				Getter = [Property = Expression.Property](auto& Out, auto&)
				{
					Out = Property == EProperty::Image ? std::string("picture.png") : std::string("url(mask.png)");
					return true;
				};
				return true;
			}
			bool ResolveEvent(const FMarkupExpression&, std::weak_ptr<FWidget>, std::function<void(FEvent&)>&,
			    std::string&) override
			{
				return false;
			}
		};
		FMarkupCompileOptions Options;
		Options.ResolveResource = [](const auto& Source, const auto& Reference, auto& Out, auto&)
		{
			Out = Reference.find(':') != std::string::npos ? Reference : "asset:" + Source + "/" + Reference;
			return true;
		};
		Options.Read = [](const auto&, const auto&, auto& Out, auto&)
		{
			Out = {"styles/theme.vswcss", ":root { --picture: URL(\"a(url(b)).png\"); }"};
			return true;
		};
		FMarkupDiagnostic Diagnostic;
		auto Program =
		    FMarkupProgram::Compile({"pages/page.vswml", R"ML(<Widget bind:image="source" bind:mask-image="mask">
                <style src="theme.vswcss"/><style>:root {background-image:var(--picture);}</style>
            </Widget>)ML"},
		        Options, Diagnostic);
		Require(Program != nullptr, Diagnostic.Format());
		auto Root = Program->Create({}, Diagnostic, std::make_shared<FBindings>());
		Require(Root != nullptr, Diagnostic.Format());
		std::string Error;
		auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		Require(Context->SetRoot(Root, Error) && Context->Update(0, Error), Error);
		FPropertyValue Value;
		Require(Root->Get(EProperty::Image, Value, Error) &&
		        std::get<std::string>(Value) == "asset:pages/page.vswml/picture.png",
		    "image getter lost its markup source base");
		Require(Root->Get(EProperty::MaskImage, Value, Error) &&
		        std::get<std::string>(Value) == "url(\"asset:pages/page.vswml/mask.png\")",
		    "mask getter lost its markup source base");
		Require(Root->Get(EProperty::BackgroundImage, Value, Error) &&
		        std::get<std::string>(Value) == "url(\"asset:styles/theme.vswcss/a(url(b)).png\")",
		    "CSS image variable lost its declaration source or quoted URL");
		Require(Context->Close(Error), Error);
	}

	void CheckNativeFactories()
	{
		FMarkupCompileOptions Options;
		Options.Types.push_back({"NativeCard",
		    "native-card",
		    {},
		    []
		{
			auto Root = FWidget::Create();
			auto Child = FWidget::Create();
			std::string Error;
			Require(Child->SetId("private", Error) && Root->AddChild(Child, Error), Error);
			return Root;
		}});
		auto Root = Compile("<Widget><NativeCard id='one'/><native-card id='two'/></Widget>", Options);
		auto One = FMarkupProgram::Find(Root, "one");
		auto Two = FMarkupProgram::Find(Root, "two");
		Require(One && Two && !FMarkupProgram::Find(Root, "private") &&
		        FMarkupProgram::Find(One, "private") != FMarkupProgram::Find(Two, "private"),
		    "native factory private scope");
		std::string Error;
		auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		Require(Context->SetRoot(Root, Error) && Context->Update(0, Error) && Context->Close(Error), Error);

		// A factory must not replace another instance's markup owner, even while detached.
		auto Existing = Compile("<Widget id='existing'><Widget id='part'/></Widget>");
		auto Part = FMarkupProgram::Find(Existing, "part");
		FMarkupCompileOptions InvalidFactory;
		InvalidFactory.Types.push_back({"Cached", {}, {}, [Existing]
		{
			return Existing;
		}});
		FMarkupDiagnostic Diagnostic;
		auto Program = FMarkupProgram::Compile({"cached.vswml", "<Cached/>"}, InvalidFactory, Diagnostic);
		Require(Program && !Program->Create({}, Diagnostic) && Diagnostic.Location.Source == "cached.vswml",
		    "factory reused an existing markup instance or lost its source");
		Require(FMarkupProgram::Find(Existing, "part") == Part, "rejected factory changed the original scope");

		InvalidFactory.Types.clear();
		InvalidFactory.Types.push_back({"CachedChild",
		    {},
		    {},
		    [Existing]
		{
			auto Parent = FWidget::Create();
			std::string ChildError;
			Require(Parent->AddChild(Existing, ChildError), ChildError);
			return Parent;
		}});
		Program = FMarkupProgram::Compile({"cached-child.vswml", "<CachedChild/>"}, InvalidFactory, Diagnostic);
		Require(Program && !Program->Create({}, Diagnostic) && Diagnostic.Location.Source == "cached-child.vswml",
		    "factory adopted an existing markup subtree");
		Require(FMarkupProgram::Find(Existing, "part") == Part, "rejected subtree changed the original scope");
	}
} // namespace

int main()
{
	try
	{
		CheckStyles();
		CheckTextDirection();
		CheckScopes();
		CheckVariablesAndTheme();
		CheckCompiledStyleReuse();
		CheckDiagnostics();
		CheckFiles();
		CheckImageReferences();
		CheckNativeFactories();
		CheckDescendantSelectors();
		std::cout << "GUI markup, style, scope and input contracts passed\n";
		return 0;
	}
	catch (const std::exception& Exception)
	{
		std::cerr << Exception.what() << '\n';
		return 1;
	}
}
