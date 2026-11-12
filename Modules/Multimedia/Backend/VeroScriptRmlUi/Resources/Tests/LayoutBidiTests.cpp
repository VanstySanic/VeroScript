// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiTestResources.h"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Traits.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
	void Require(bool Value, const char* Message)
	{
		if (!Value)
		{
			throw std::runtime_error(Message);
		}
	}
	class FLayoutRenderer final : public Rml::RenderInterface
	{
	public:
		Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override
		{
			return 1;
		}
		void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override
		{
		}
		void ReleaseGeometry(Rml::CompiledGeometryHandle) override
		{
		}
		Rml::TextureHandle LoadTexture(Rml::Vector2i& Size, const Rml::String&) override
		{
			Size = {};
			return 0;
		}
		Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override
		{
			return 1;
		}
		void ReleaseTexture(Rml::TextureHandle) override
		{
		}
		void EnableScissorRegion(bool) override
		{
		}
		void SetScissorRegion(Rml::Rectanglei) override
		{
		}
	};
	struct FLayoutScope
	{
		FLayoutRenderer Renderer;
		std::unique_ptr<VeroScript::Gui::IBackendContext> Backend;
		Rml::Context* Context = nullptr;
		FLayoutScope()
		{
			std::string Error;
			Backend = VeroScript::RmlUi::CreateBackend(VeroScript::GuiTestResourceOptions(), Error);
			if (!Backend)
			{
				throw std::runtime_error(Error);
			}
			Context = Rml::CreateContext("bidi-layout-regressions", {640, 480}, &Renderer);
			Require(Context != nullptr, "could not create layout-only RmlUi context");
		}
		~FLayoutScope()
		{
			if (Context)
			{
				Rml::RemoveContext("bidi-layout-regressions");
			}
			std::string Error;
			if (Backend)
			{
				Backend->Close(Error);
			}
		}
		Rml::ElementDocument* Page(const Rml::String& Contents)
		{
			const Rml::String Source = "<rml><head><style>body { margin: 0; font-family: Vero Sans; font-size: 20px; } "
			                           "div { display: block; width: 500px; -rmlui-direction: ltr; } "
			                           "span { display: inline; }</style></head><body>" +
			    Contents + "</body></rml>";
			auto* Document = Context->LoadDocumentFromMemory(Source);
			Require(Document != nullptr, "could not load layout test document");
			Document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
			Context->Update();
			return Document;
		}
	};
	Rml::ElementText* Text(Rml::Element* Element)
	{
		if (!Element) return nullptr;
		if (auto* Result = rmlui_dynamic_cast<Rml::ElementText*>(Element)) return Result;
		for (int I = 0; I < Element->GetNumChildren(); ++I)
		{
			if (auto* Result = Text(Element->GetChild(I))) return Result;
		}
		return nullptr;
	}
	float Left(Rml::Element* Element)
	{
		return Element->GetAbsoluteOffset(Rml::BoxArea::Border).x;
	}

	void CheckStyledSpans(FLayoutScope& Scope)
	{
		auto* Page = Scope.Page("<div><span id='a'>x אב</span><span id='b'>גד y</span></div>");
		auto* A = Page->GetElementById("a");
		auto* B = Page->GetElementById("b");
		auto* AT = Text(A);
		auto* BT = Text(B);
		Require(A && B && AT && BT, "missing styled inline nodes");
		Require(A->GetNumBoxes() >= 2 && B->GetNumBoxes() >= 2,
		    "a mixed-direction styled span was not split into its separated visual pieces");
		Require(AT->GetLines().size() >= 2 && BT->GetLines().size() >= 2, "missing visual text fragments");
		const auto Paragraph = AT->GetLines().front().paragraph;
		Require(Paragraph && *Paragraph == "x אבגד y", "inline formatting lost logical paragraph text");
		for (const auto* Node : {AT, BT})
		{
			for (const auto& Line : Node->GetLines())
			{
				Require(Line.paragraph == Paragraph, "adjacent styles did not retain one shared paragraph owner");
				Require(Line.line_offset == 0 && Line.line_length == int(Paragraph->size()), "styled fragment lost whole-line L1/L2 range");
			}
		}
		// The two Hebrew spans reverse relative order across the style boundary.
		float AH = -1.f, BH = -1.f;
		for (const auto& Line : AT->GetLines())
		{
			if (Line.text.find("א") != Rml::String::npos)
			{
				AH = AT->GetAbsoluteOffset().x + Line.position.x;
			}
		}
		for (const auto& Line : BT->GetLines())
		{
			if (Line.text.find("ג") != Rml::String::npos)
			{
				BH = BT->GetAbsoluteOffset().x + Line.position.x;
			}
		}
		Require(AH >= 0.f && BH >= 0.f && BH < AH, "directional runs were reordered per node instead of across styles");
		Page->Close();
		Scope.Context->Update();
	}

	void CheckAtomicAndDirection(FLayoutScope& Scope)
	{
		auto* Page = Scope.Page("<div style='-rmlui-direction: rtl'><span id='a'>אב</span>"
		                        "<span id='atom' style='display:inline-block;width:12px;height:12px'></span><span id='b'>גד</span></div>"
		                        "<div>a<span id='isolate' style='-rmlui-direction:rtl'>ABC אב</span>z</div>");
		auto* A = Page->GetElementById("a");
		auto* B = Page->GetElementById("b");
		auto* Atomic = Page->GetElementById("atom");
		Require(A && B && Atomic, "missing atomic inline nodes");
		Require(Left(B) < Left(Atomic) && Left(Atomic) < Left(A), "neutral atomic object did not participate in paragraph reordering");
		auto* AtomicText = Text(A);
		Require(AtomicText && !AtomicText->GetLines().empty(), "missing atomic paragraph text");
		const auto& AtomicParagraph = AtomicText->GetLines().front().paragraph;
		Require(AtomicParagraph && *AtomicParagraph == "אב\ufffcגד", "atomic children leaked into the surrounding paragraph");
		auto* Isolate = Text(Page->GetElementById("isolate"));
		Require(Isolate && !Isolate->GetLines().empty(), "missing explicit-direction text");
		for (const auto& Line : Isolate->GetLines())
		{
			Require(Line.paragraph && *Line.paragraph == "a\u2067ABC אב\u2069z", "explicit inline direction lost its isolation controls");
			Require(Line.paragraph_direction == Rml::Style::Direction::Ltr, "a styled span changed the shared paragraph base direction");
		}
		Page->Close();
		Scope.Context->Update();
	}

	void CheckSoftWrapAndCollapsedContent(FLayoutScope& Scope)
	{
		auto* Page = Scope.Page("<div id='wrap' style='width:70px'>aa אב גד ee xx</div>"
		                        "<div><span id='empty' style='padding-left:7px;padding-right:9px'>visible</span><span id='after'>b</span></div>"
		                        "<div id='break' style='-rmlui-direction:auto'><span id='before'>אב</span><br/><span id='after-br'>ABC</span></div>");
		auto* Wrapped = Text(Page->GetElementById("wrap"));
		Require(Wrapped && !Wrapped->GetLines().empty(), "missing wrapped text");
		const auto Paragraph = Wrapped->GetLines().front().paragraph;
		Require(Paragraph && *Paragraph == "aa אב גד ee xx", "soft wrapping removed spaces from paragraph analysis");
		std::vector<float> Rows;
		for (const auto& Line : Wrapped->GetLines())
		{
			Require(Line.paragraph == Paragraph && Line.line_offset >= 0 && Line.line_length > 0, "wrapped line lost its stable paragraph range");
			if (std::none_of(Rows.begin(), Rows.end(), [&](float Y)
			{
				return std::abs(Y - Line.position.y) < 0.5f;
			}))
			{
				Rows.push_back(Line.position.y);
			}
		}
		Require(Rows.size() >= 2, "soft-wrap regression scenario did not wrap");
		auto* BeforeBreak = Text(Page->GetElementById("before"));
		auto* AfterBreak = Text(Page->GetElementById("after-br"));
		Require(BeforeBreak && AfterBreak && !BeforeBreak->GetLines().empty() && !AfterBreak->GetLines().empty(), "missing forced-line text");
		const auto BreakParagraph = BeforeBreak->GetLines().front().paragraph;
		Require(BreakParagraph && *BreakParagraph == "אב\u2028ABC" && AfterBreak->GetLines().front().paragraph == BreakParagraph,
		    "br incorrectly started a new UAX #9 paragraph instead of preserving its base direction");
		auto* Empty = Page->GetElementById("empty");
		auto* EmptyText = Text(Empty);
		Require(Empty && EmptyText && !EmptyText->GetLines().empty(), "missing collapsed-text regression node");
		EmptyText->SetText(" ");
		Scope.Context->Update();
		Require(EmptyText->GetLines().empty(), "collapsed text retained stale rendered lines");
		Require(Empty->GetBox().GetSize(Rml::BoxArea::Border).x >= 16.f, "an inline box containing only collapsed text lost its padding");
		Require(Left(Page->GetElementById("after")) >= Left(Empty) + 15.5f, "collapsed inline decoration overlapped following text");
		EmptyText->SetText("visible");
		Scope.Context->Update();
		Require(!EmptyText->GetLines().empty(), "text update did not create new lines");
		EmptyText->SetText("");
		Scope.Context->Update();
		Require(EmptyText->GetLines().empty(), "empty text update kept old visual geometry");
		Page->Close();
		Scope.Context->Update();
	}

	void CheckHiddenAndPositionedBreaks(FLayoutScope& Scope)
	{
		auto* Page = Scope.Page("<div><span id='first'>a</span><br style='display:none'/>"
		                        "<br style='position:absolute'/><br style='position:fixed'/><br style='float:left'/>"
		                        "<span id='same-line'>b</span><br/><span id='next-line'>c</span></div>");
		auto* First = Text(Page->GetElementById("first"));
		auto* Same = Text(Page->GetElementById("same-line"));
		auto* Next = Text(Page->GetElementById("next-line"));
		Require(First && Same && Next && !First->GetLines().empty() && !Same->GetLines().empty() && !Next->GetLines().empty(),
		    "missing line-break regression text");
		const auto Paragraph = First->GetLines().front().paragraph;
		Require(Paragraph && *Paragraph == "ab\u2028c" && Same->GetLines().front().paragraph == Paragraph &&
		        Next->GetLines().front().paragraph == Paragraph,
		    "hidden or positioned br entered the logical inline paragraph");
		Require(std::abs(First->GetAbsoluteOffset().y - Same->GetAbsoluteOffset().y) < 0.5f,
		    "hidden or positioned br caused an in-flow line break");
		Require(Next->GetAbsoluteOffset().y > Same->GetAbsoluteOffset().y + 0.5f, "visible br no longer forces a physical line break");
		Page->Close();
		Scope.Context->Update();
	}

	void CheckTransparentInlineJoining(FLayoutScope& Scope)
	{
		VeroScript::Font::FFontFace Face;
		Face.Source = VS_GUI_ARABIC_FONT;
		Face.Family = "Arabic joining regression";
		std::string Error;
		if (!Scope.Backend->LoadFont(Face, Error))
		{
			throw std::runtime_error(Error);
		}
		auto* Page = Scope.Page("<div style='font-family:Arabic joining regression;-rmlui-language:ar;-rmlui-direction:rtl'>"
		                        "<span id='prefix'>بت</span><span id='transparent'></span><span id='suffix'>بت</span></div>"
		                        "<div style='font-family:Arabic joining regression;-rmlui-language:ar;-rmlui-direction:rtl'>"
		                        "<span id='padded-prefix'>بت</span><span id='padded' style='padding-left:7px;padding-right:9px'></span>"
		                        "<span id='padded-suffix'>بت</span></div>");
		auto* Prefix = Page->GetElementById("prefix");
		auto* Suffix = Page->GetElementById("suffix");
		auto* Gap = Page->GetElementById("transparent");
		auto* PrefixText = Text(Prefix);
		Require(Prefix && Suffix && Gap && PrefixText && !PrefixText->GetLines().empty(), "missing transparent joining scenario");
		const auto Paragraph = PrefixText->GetLines().front().paragraph;
		Require(Paragraph && *Paragraph == "بتبت", "a transparent empty inline inserted an artificial joining boundary");
		const Rml::String Language = "ar";
		const auto Font = Rml::GetFontEngineInterface()->GetFontFaceHandle("arabic joining regression", Rml::Style::FontStyle::Normal,
		    Rml::Style::FontWeight::Normal, 20);
		Require(Font != 0, "missing Arabic joining font");
		Rml::TextShapingContext Context{Language, Rml::Style::Direction::Rtl, Rml::Style::FontKerning::Auto, 0.f};
		const float Expected = float(Rml::GetFontEngineInterface()->GetStringWidth(Font, "بتبت", Context));
		const float VisualLeft = std::min(Left(Prefix), Left(Suffix));
		const float VisualRight = std::max(Left(Prefix) + Prefix->GetBox().GetSize(Rml::BoxArea::Border).x,
		    Left(Suffix) + Suffix->GetBox().GetSize(Rml::BoxArea::Border).x);
		Require(Expected > 0.f && std::abs(VisualRight - VisualLeft - Expected) <= 2.f,
		    "an empty inline changed contextual Arabic shaping/advance");
		auto* PaddedText = Text(Page->GetElementById("padded-prefix"));
		Require(PaddedText && !PaddedText->GetLines().empty() && PaddedText->GetLines().front().paragraph &&
		        *PaddedText->GetLines().front().paragraph == "بت\ufffcبت",
		    "a padded empty inline lost its neutral object boundary");
		Require(Page->GetElementById("padded")->GetBox().GetSize(Rml::BoxArea::Border).x >= 16.f,
		    "padded empty inline lost its horizontal extent");
		Gap->SetInnerRML("wide");
		Scope.Context->Update();
		Require(Gap->GetBox().GetSize(Rml::BoxArea::Border).x > 0.f, "inline update did not produce a box");
		Gap->SetInnerRML("");
		Scope.Context->Update();
		Require(Gap->GetBox().GetSize(Rml::BoxArea::Border).x == 0.f, "transparent empty inline retained its old background box");
		Page->Close();
		Scope.Context->Update();
	}

	void CheckZeroWidthControls()
	{
		using namespace VeroScript;
		auto* Engine = Rml::GetFontEngineInterface();
		const auto Handle = Engine->GetFontFaceHandle(Rml::StringUtilities::ToLower(Font::DefaultFontFamily),
		    Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, 20);
		Require(Handle != 0, "missing zero-width control font handle");
		const Rml::String Language = "en";
		const Rml::TextShapingContext Context{Language, Rml::Style::Direction::Ltr,
		    Rml::Style::FontKerning::Normal, 1.25f};
		for (const Rml::String Value : {Rml::String("\u200b"), Rml::String("\u200d"),
		         Rml::String("\ufe0f"), Rml::String("\u200b\ufe0f")})
		{
			Rml::TextLineLayout Layout;
			Require(Engine->GetStringLayout(Handle, Value, Context, Layout) && Layout.width == 0 &&
			        Layout.spans.empty() && !Layout.carets.empty() &&
			        Engine->GetStringWidth(Handle, Value, Context) == 0,
			    "format controls or selectors consumed width or letter spacing");
			Require(std::any_of(Layout.carets.begin(), Layout.carets.end(), [&](const auto& Caret)
			{
				return Caret.byte_offset == static_cast<int>(Value.size()) && Caret.x == 0;
			}),
			    "zero-width controls lost their logical ending caret");
		}
		Rml::TextLineLayout Plain;
		Require(Engine->GetStringLayout(Handle, "X", Context, Plain) && Plain.width > 0,
		    "missing visible reference glyph");
		for (const Rml::String Value : {Rml::String("\u200dX"), Rml::String("X\ufe0f")})
		{
			Rml::TextLineLayout Layout;
			Require(Engine->GetStringLayout(Handle, Value, Context, Layout) && !Layout.spans.empty() &&
			        std::abs(Layout.width - Plain.width) < .001f,
			    "a joiner or selector suppressed its visible cluster or added spacing");
		}
	}

	void CheckWidthCache()
	{
		using namespace VeroScript;
		const auto Options = GuiTestResourceOptions();
		std::ifstream Input(VS_GUI_ARABIC_FONT, std::ios::binary);
		std::vector<std::uint8_t> Bytes((std::istreambuf_iterator<char>(Input)), {});
		std::string Error;
		const auto Face = Options.Fonts->Load(Bytes, 0, Error);
		Require(Face != nullptr, "could not load width-cache reference font");
		auto* Engine = Rml::GetFontEngineInterface();
		const auto Handle = Engine->GetFontFaceHandle("arabic joining regression", Rml::Style::FontStyle::Normal,
		    Rml::Style::FontWeight::Normal, 20);
		Require(Handle != 0, "missing cached-width font handle");
		const Rml::String Language = "ar";
		for (const Rml::String Text : {Rml::String("AV AV"), Rml::String("بتبت"), Rml::String("ABC بت 123")})
		{
			for (const auto Direction : {Rml::Style::Direction::Auto, Rml::Style::Direction::Ltr, Rml::Style::Direction::Rtl})
			{
				for (const float Spacing : {0.f, 1.25f})
				{
					for (const auto Kerning : {Rml::Style::FontKerning::Normal, Rml::Style::FontKerning::None})
					{
						const auto Owner = std::make_shared<const Rml::String>(Text);
						Rml::TextShapingContext Context{Language, Direction, Kerning, Spacing};
						Context.paragraph_owner = Owner;
						Context.paragraph_text = *Owner;
						Context.paragraph_length = static_cast<int>(Text.size());
						Context.line_offset = 0;
						Context.line_length = static_cast<int>(Text.size());
						Font::FShapeOptions Shape;
						Shape.PixelSize = 20;
						Shape.Language = Language;
						Shape.Direction = Direction == Rml::Style::Direction::Rtl ? Font::EDirection::RightToLeft : Direction == Rml::Style::Direction::Ltr ? Font::EDirection::LeftToRight
						                                                                                                                                    : Font::EDirection::Auto;
						Shape.bKerning = Kerning != Rml::Style::FontKerning::None;
						const auto Layout = Font::FTextLayout::Create(Options.TextLayouts, Text, Shape.Direction, Error);
						Font::FTextLine Line;
						Require(Layout && Layout->ShapeLine(Face, {}, Shape, 0, Text.size(), Spacing, 0, Line, Error),
						    "reference shaping failed");
						const auto Expected = static_cast<int>(std::round(Line.Width));
						Require(Engine->GetStringWidth(Handle, Text, Context) == Expected &&
						        Engine->GetStringWidth(Handle, Text, Context) == Expected,
						    "cached width changed shaping context");
						Context.paragraph_owner = std::make_shared<const Rml::String>(Text);
						Context.paragraph_text = *Context.paragraph_owner;
						Require(Engine->GetStringWidth(Handle, Text, Context) == Expected, "recreated paragraph changed cached width");
						Context.paragraph_owner.reset();
						Context.paragraph_text = Text;
						Require(Engine->GetStringWidth(Handle, Text, Context) == Expected, "unowned paragraph changed cached width");
						Context.paragraph_owner = Owner;
						const Rml::String Invalid = "invalid paragraph owner";
						Context.paragraph_text = Invalid;
						bool Rejected = false;
						try
						{
							Engine->GetStringWidth(Handle, Text, Context);
						}
						catch (const std::exception&)
						{
							Rejected = true;
						}
						Require(Rejected, "width cache bypassed paragraph-context validation");
					}
				}
			}
		}
		Rml::TextShapingContext Context{Language, Rml::Style::Direction::Auto, Rml::Style::FontKerning::Auto, 0.f};
		const auto Original = Engine->GetStringWidth(Handle, "cache eviction", Context);
		for (int Index = 0; Index < 2200; ++Index)
		{
			Require(Engine->GetStringWidth(Handle, "cache item " + std::to_string(Index), Context) > 0,
			    "width/paragraph cache eviction failed");
		}
		Require(Engine->GetStringWidth(Handle, "cache eviction", Context) == Original,
		    "eviction changed the width of an earlier value");
	}

	void CheckNavigationAtVisualEdges(FLayoutScope& Scope)
	{
		auto* Page = Scope.Page("<input id='editor' type='text' value='אבג' "
		                        "style='width:200px;height:30px;-rmlui-direction:rtl'/>");
		auto* Editor = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(Page->GetElementById("editor"));
		Require(Editor && Editor->Focus(), "could not focus visual edge regression input");
		Scope.Context->Update();
		Editor->SetSelectionRange(0, 0);
		Require(Scope.Context->ProcessKeyDown(Rml::Input::KI_RIGHT, 0),
		    "Right at the RTL visual edge swallowed navigation instead of propagating it");
		Require(!Scope.Context->ProcessKeyDown(Rml::Input::KI_LEFT, 0), "visual movement within text was not consumed");
		int Begin = -1, End = -1;
		Editor->GetSelection(&Begin, &End, nullptr);
		Require(Begin == 1 && End == 1, "visual edge navigation changed the logical caret unexpectedly");
		Editor->SetSelectionRange(3, 3);
		Require(Scope.Context->ProcessKeyDown(Rml::Input::KI_LEFT, 0),
		    "Left at the RTL visual edge swallowed navigation instead of propagating it");
		Page->Close();
		Scope.Context->Update();
	}

	void CheckStyleScopeAcrossParagraphs(FLayoutScope& Scope)
	{
		auto* Page = Scope.Page("<div><span id='paragraphs' style='-rmlui-direction:rtl;white-space:pre'>ABC אב\u2029DEF גד</span></div>");
		auto* Node = Text(Page->GetElementById("paragraphs"));
		Require(Node && !Node->GetLines().empty(), "missing paragraph-separator style scenario");
		const auto Paragraph = Node->GetLines().front().paragraph;
		Require(Paragraph && *Paragraph == "\u2067ABC אב\u2029\u2067DEF גד\u2069",
		    "a style direction isolate was not reopened after a P1 separator");
		bool FoundSecondParagraph = false;
		for (const auto& Line : Node->GetLines())
		{
			Require(Line.paragraph == Paragraph && Line.paragraph_offset >= 0 &&
			        Paragraph->compare(size_t(Line.paragraph_offset), Line.text.size(), Line.text) == 0,
			    "injected style controls broke source-to-paragraph fragment mapping");
			if (Line.text.find("DEF") != Rml::String::npos)
			{
				FoundSecondParagraph = true;
				Require(Line.paragraph_offset == int(Paragraph->find("DEF")), "P1 style restoration shift was not applied to the next line");
			}
		}
		Require(FoundSecondParagraph, "P1 style restoration lost subsequent text");
		Page->Close();
		Scope.Context->Update();
	}
}

int main()
{
	try
	{
		FLayoutScope Scope;
		CheckStyledSpans(Scope);
		CheckAtomicAndDirection(Scope);
		CheckSoftWrapAndCollapsedContent(Scope);
		CheckHiddenAndPositionedBreaks(Scope);
		CheckTransparentInlineJoining(Scope);
		CheckZeroWidthControls();
		CheckWidthCache();
		CheckNavigationAtVisualEdges(Scope);
		CheckStyleScopeAcrossParagraphs(Scope);
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
