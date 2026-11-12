// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptRmlUiContent.h"
#include "VeroScriptFont.h"
#include "Font/VeroScriptRmlUiFontBridge.h"
#include "FontEngineInterfaceHarfBuzz.h"
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Filter.h>
#include <RmlUi/Core/PropertyDefinition.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/StyleSheetSpecification.h>
#include <RmlUi/Core/StringUtilities.h>
#include <locale.h>
#include <algorithm>
#include <cmath>
#include <map>

namespace VeroScript::RmlUi
{
	namespace
	{
		struct FFontSource
		{
			Gui::FFontFace Description;
			std::vector<std::uint8_t> Bytes;
			bool bLoaded = false;
		};
		std::vector<FFontSource>& FontSources()
		{
			static auto* Sources = new std::vector<FFontSource>;
			return *Sources;
		}

		// Raw style functions use a scoped numeric locale on this thread only.
		class FNumericLocale
		{
		public:
			FNumericLocale()
			{
				#if defined(_WIN32)
				PreviousMode = _configthreadlocale(_ENABLE_PER_THREAD_LOCALE);
				Previous = setlocale(LC_NUMERIC, nullptr);
				setlocale(LC_NUMERIC, "C");
				#else
				Locale = newlocale(LC_NUMERIC_MASK, "C", nullptr);
				if (Locale)
				{
					Previous = uselocale(Locale);
				}
				#endif
			}
			~FNumericLocale()
			{
				#if defined(_WIN32)
				setlocale(LC_NUMERIC, Previous.c_str());
				_configthreadlocale(PreviousMode);
				#else
				if (Locale)
				{
					uselocale(Previous);
					freelocale(Locale);
				}
				#endif
			}

		private:
			#if defined(_WIN32)
			std::string Previous;
			int PreviousMode = 0;
			#else
			locale_t Locale = nullptr, Previous = nullptr;
			#endif
		};
		Rml::Colourb Color(const Graphics::FColor& Value)
		{
			const auto Encode = [](float X)
			{
				return static_cast<Rml::byte>(std::lround(
					std::clamp(X <= .0031308f ? 12.92f * X : 1.055f * std::pow(X, 1.f / 2.4f) - .055f, 0.f, 1.f) *
					255));
			};
			return {Encode(Value.R), Encode(Value.G), Encode(Value.B),
					static_cast<Rml::byte>(std::lround(Value.A * 255))};
		}
		std::string NormalizeDropShadow(std::string Value)
		{
			std::size_t At = 0;
			while ((At = Value.find("drop-shadow(", At)) != std::string::npos)
			{
				const auto Begin = At + 12;
				std::size_t End = Begin;
				int Depth = 0;
				std::vector<std::string> Tokens;
				std::string Token;
				for (; End < Value.size(); ++End)
				{
					const char C = Value[End];
					if (C == ')' && Depth == 0)
					{
						break;
					}
					if (C == '(')
					{
						++Depth;
					}
					else if (C == ')')
					{
						--Depth;
					}
					if ((C == ' ' || C == '\t' || C == '\n') && Depth == 0)
					{
						if (!Token.empty())
						{
							Tokens.push_back(std::move(Token));
							Token.clear();
						}
					}
					else
					{
						Token += C;
					}
				}
				if (!Token.empty())
				{
					Tokens.push_back(std::move(Token));
				}
				if (End == Value.size())
				{
					break;
				}
				Rml::PropertyDictionary Parsed;
				if (Tokens.size() >= 3 && Tokens.size() <= 4 &&
					Rml::StyleSheetSpecification::ParsePropertyDeclaration(Parsed, "color", Tokens.back()))
				{
					std::string Parameters = Tokens.back();
					for (std::size_t I = 0; I + 1 < Tokens.size(); ++I)
					{
						Parameters += ' ';
						Parameters += Tokens[I];
					}
					Value.replace(Begin, End - Begin, Parameters);
					At = Begin + Parameters.size() + 1;
				}
				else
				{
					At = End + 1;
				}
			}
			return Value;
		}
		std::string ImageDecorators(std::string Value)
		{
			char Quote = 0;
			for (std::size_t At = 0; At < Value.size(); ++At)
			{
				const char C = Value[At];
				if (Quote)
				{
					if (C == '\\' && At + 1 < Value.size())
					{
						++At;
					}
					else if (C == Quote)
					{
						Quote = 0;
					}
					continue;
				}
				if (C == '\'' || C == '"')
				{
					Quote = C;
					continue;
				}
				if ((C == 'u' || C == 'U') && At + 3 < Value.size() && (Value[At + 1] == 'r' || Value[At + 1] == 'R') &&
					(Value[At + 2] == 'l' || Value[At + 2] == 'L') && Value[At + 3] == '(')
				{
					Value.replace(At, 3, "image");
					At += 5;
				}
			}
			return Value;
		}
	} // namespace

	Rml::FontEngineInterface* GetFontEngine()
	{
		static FontEngineInterfaceHarfBuzz Engine;
		return &Engine;
	}
	bool InitializeDefaultFont(std::string& Error)
	{
		const auto& DefaultFont = Font::GetDefaultFontData();
		if (!Rml::LoadFontFace({DefaultFont.data(), DefaultFont.size()}, Font::DefaultFontFamily,
							   Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, true))
		{
			Error = "rmlui.default_font_failed";
			return false;
		}
		return true;
	}
	void ReleaseFontSources()
	{
		FontSources().clear();
		ReleaseFontServices();
	}
	bool LoadFontSource(const Gui::FFontFace& Font, const Gui::FResourceReader& Reader, std::string& Error)
	try
	{
		if (!Gui::ValidateFontFace(Font, Error))
		{
			return false;
		}
		std::vector<std::uint8_t> Bytes;
		if (!(Reader ? Reader(Font.Source, Bytes, Error) : Gui::ReadGuiResourceFile(Font.Source, Bytes, Error)))
		{
			if (Error.empty())
			{
				Error = "rmlui.font_read_failed: " + Font.Source;
			}
			return false;
		}
		std::uint64_t Total = Bytes.size();
		for (const auto& Existing : FontSources())
		{
			const auto& D = Existing.Description;
			if (Rml::StringUtilities::ToLower(D.Family) == Rml::StringUtilities::ToLower(Font.Family) &&
				D.Weight == Font.Weight && D.bItalic == Font.bItalic && D.FaceIndex == Font.FaceIndex &&
				D.bFallback == Font.bFallback)
			{
				if (Existing.Bytes == Bytes)
				{
					if (!Existing.bLoaded)
					{
						Error = "rmlui.invalid_font: " + Font.Source;
					}
					return Existing.bLoaded;
				}
				Error = "rmlui.font_identity_conflict: " + Font.Family;
				return false;
			}
			Total += Existing.Bytes.size();
		}
		if (Bytes.empty() || Total > 128 * 1024 * 1024)
		{
			Error = "rmlui.font_resource_limit";
			return false;
		}
		// Retain bytes before registration: a collection font may register some faces before failing.
		FontSources().push_back({Font, std::move(Bytes)});
		auto& Source = FontSources().back();
		if (!Rml::LoadFontFace({Source.Bytes.data(), Source.Bytes.size()}, Font.Family,
							   Font.bItalic ? Rml::Style::FontStyle::Italic : Rml::Style::FontStyle::Normal,
							   static_cast<Rml::Style::FontWeight>(Font.Weight), Font.bFallback, Font.FaceIndex))
		{
			Error = "rmlui.invalid_font: " + Font.Source;
			return false;
		}
		Source.bLoaded = true;
		return true;
	}
	catch (const std::exception& Exception)
	{
		Error = std::string("rmlui.font_exception: ") + Exception.what();
		return false;
	}
	catch (...)
	{
		Error = "rmlui.font_exception";
		return false;
	}

	bool SynchronizeContent(Rml::ElementDocument& Document, Rml::Element& Element, FContentNodes& Content,
							const Gui::FResolvedProperties& P, const Gui::FResolvedProperties* Previous,
							bool bComposite, std::string& Error)
	{
		using Rml::Property;
		using Rml::PropertyId;
		using Rml::Unit;
		std::pair<PropertyId, Property> Styles[]{
			{PropertyId::FontFamily, {Rml::StringUtilities::ToLower(P.FontFamily), Unit::STRING}},
			{PropertyId::FontSize, {static_cast<float>(P.FontSize), Unit::DP}},
			{PropertyId::FontWeight, Property(static_cast<Rml::Style::FontWeight>(P.FontWeight))},
			{PropertyId::FontStyle,
			 Property(P.FontStyle == "italic" ? Rml::Style::FontStyle::Italic : Rml::Style::FontStyle::Normal)},
			{PropertyId::Color, {Color(P.TextColor), Unit::COLOUR}},
			{PropertyId::TextAlign, Property(P.TextAlign == "right"		? Rml::Style::TextAlign::Right
											 : P.TextAlign == "center"	? Rml::Style::TextAlign::Center
											 : P.TextAlign == "justify" ? Rml::Style::TextAlign::Justify
																		: Rml::Style::TextAlign::Left)},
			{PropertyId::WhiteSpace, Property(P.WhiteSpace == "pre"		   ? Rml::Style::WhiteSpace::Pre
											  : P.WhiteSpace == "nowrap"   ? Rml::Style::WhiteSpace::Nowrap
											  : P.WhiteSpace == "pre-wrap" ? Rml::Style::WhiteSpace::Prewrap
											  : P.WhiteSpace == "pre-line" ? Rml::Style::WhiteSpace::Preline
																		   : Rml::Style::WhiteSpace::Normal)},
			{PropertyId::TextOverflow, Property(P.TextOverflow == "ellipsis" ? Rml::Style::TextOverflow::Ellipsis
																			 : Rml::Style::TextOverflow::Clip)},
			{PropertyId::LineHeight,
			 {P.LineHeight == 0 ? 1.2f : static_cast<float>(P.LineHeight),
			  P.LineHeight == 0 ? Unit::NUMBER : Unit::DP}},
			{PropertyId::LetterSpacing, {static_cast<float>(P.LetterSpacing), Unit::DP}},
			{PropertyId::OverflowX,
			 Property(P.bClipContent ? Rml::Style::Overflow::Hidden : Rml::Style::Overflow::Visible)},
			{PropertyId::OverflowY,
			 Property(P.bClipContent ? Rml::Style::Overflow::Hidden : Rml::Style::Overflow::Visible)}};
		std::sort(std::begin(Styles), std::end(Styles), [](const auto& A, const auto& B) { return A.first < B.first; });
		for (const auto& Entry : Styles)
		{
			const auto* Current = Element.GetLocalProperty(Entry.first);
			if ((!Current || *Current != Entry.second) && !Element.SetProperty(Entry.first, Entry.second))
			{
				Error = "rmlui.content_property_rejected";
				return false;
			}
		}
		for (const auto Id : {PropertyId::BorderTopLeftRadius, PropertyId::BorderTopRightRadius,
							  PropertyId::BorderBottomRightRadius, PropertyId::BorderBottomLeftRadius})
		{
			const Property Radius(static_cast<float>(P.CornerRadius), Unit::DP);
			const auto* Current = Element.GetLocalProperty(Id);
			if (!Current || *Current != Radius)
			{
				Element.SetProperty(Id, Radius);
			}
		}
		// RmlUi dirties inherited text properties even when an equal value is assigned.
		if (!Previous || Previous->Language != P.Language)
		{
			Element.SetAttribute("lang", P.Language);
		}
		if (!Previous || Previous->Direction != P.Direction)
		{
			Element.SetAttribute("dir", P.Direction);
		}
		if (!Previous || Previous->Text != P.Text)
		{
			if (P.Text.empty() && Content.TextBox)
			{
				Element.RemoveChild(Content.TextBox);
				Content.TextBox = nullptr;
				Content.Text = nullptr;
			}
			if (!Content.Text && !P.Text.empty())
			{
				// Text must participate in an inline formatting context inside its flex item.
				auto Box = Document.CreateElement("div");
				Box->SetProperty("display", "block");
				Box->SetProperty("flex-shrink", "0");
				Content.Text = static_cast<Rml::ElementText*>(Box->AppendChild(Document.CreateTextNode(P.Text)));
				// Keep the image/text/children order independent of when each content property is set.
				Content.TextBox = Element.InsertBefore(std::move(Box), Element.GetChild(Content.Image ? 1 : 0));
			}
			else if (Content.Text)
			{
				Content.Text->SetText(P.Text);
			}
		}
		if (!Previous || Previous->Image != P.Image)
		{
			if (Content.Image && P.Image.empty())
			{
				Element.RemoveChild(Content.Image);
				Content.Image = nullptr;
			}
			if (!P.Image.empty())
			{
				if (!Content.Image)
				{
					Content.Image = Element.InsertBefore(Document.CreateElement("img"), Element.GetChild(0));
					Content.Image->SetProperty("display", "block");
				}
				Content.Image->SetAttribute("src", P.Image);
				Content.Image->SetProperty("max-width", "100%");
			}
		}
		if (Content.TextBox)
		{
			const auto& Overflow = std::lower_bound(std::begin(Styles), std::end(Styles), PropertyId::TextOverflow,
			    [](const auto& Entry, PropertyId Id) { return Entry.first < Id; })->second;
			const auto* Current = Content.TextBox->GetLocalProperty(PropertyId::TextOverflow);
			if (!Current || *Current != Overflow)
			{
				Content.TextBox->SetProperty(PropertyId::TextOverflow, Overflow);
			}
		}
		// Most layout, text and background updates do not parse any style
		// function. Avoid changing the thread locale for those nodes.
		if (Previous && Previous->BackdropFilter == P.BackdropFilter &&
		    Previous->BackgroundImage == P.BackgroundImage && Previous->MaskImage == P.MaskImage &&
		    Previous->BoxShadow == P.BoxShadow && Previous->Transform == P.Transform &&
		    Previous->Filter == P.Filter && (!bComposite || Previous->Opacity == P.Opacity))
		{
			return true;
		}
		FNumericLocale Locale;
		const auto Background = ImageDecorators(P.BackgroundImage);
		const auto Mask = ImageDecorators(P.MaskImage);
		const auto Backdrop = NormalizeDropShadow(P.BackdropFilter);
		struct FParsedStyle
		{
			const char* Name;
			const std::string *Value, *Before, *After;
		};
		const FParsedStyle Parsed[] = {
			{"backdrop-filter", &Backdrop, Previous ? &Previous->BackdropFilter : nullptr, &P.BackdropFilter},
			{"decorator", &Background, Previous ? &Previous->BackgroundImage : nullptr, &P.BackgroundImage},
			{"mask-image", &Mask, Previous ? &Previous->MaskImage : nullptr, &P.MaskImage},
			{"box-shadow", &P.BoxShadow, Previous ? &Previous->BoxShadow : nullptr, &P.BoxShadow},
			{"transform", &P.Transform, Previous ? &Previous->Transform : nullptr, &P.Transform}};
		for (const auto& Entry : Parsed)
		{
			if (Entry.Before && *Entry.Before == *Entry.After)
			{
				continue;
			}
			if (Entry.Value->empty() || *Entry.Value == "none")
			{
				Element.RemoveProperty(Entry.Name);
			}
			else if (!Element.SetProperty(Entry.Name, *Entry.Value))
			{
				Error = std::string("rmlui.content_style_rejected: ") + Entry.Name;
				return false;
			}
		}
		if (Previous && Previous->Filter == P.Filter && (!bComposite || Previous->Opacity == P.Opacity))
		{
			return true;
		}
		Rml::PropertyDictionary Dictionary;
		Rml::FiltersPtr Filters;
		if (!P.Filter.empty() && P.Filter != "none")
		{
			if (!Rml::StyleSheetSpecification::ParsePropertyDeclaration(Dictionary, "filter",
																		NormalizeDropShadow(P.Filter)))
			{
				Error = "rmlui.invalid_filter";
				return false;
			}
			Filters = Dictionary.GetProperty(PropertyId::Filter)->Get<Rml::FiltersPtr>();
		}
		if (bComposite && P.Opacity != 1)
		{
			Rml::PropertyDictionary Opacity;
			// The numeric value is assigned directly after parsing the locale-independent function skeleton.
			Rml::StyleSheetSpecification::ParsePropertyDeclaration(Opacity, "filter", "opacity(1)");
			auto List = Rml::MakeShared<Rml::FilterDeclarationList>();
			if (Filters)
			{
				List->list = Filters->list;
			}
			auto Filter = Opacity.GetProperty(PropertyId::Filter)->Get<Rml::FiltersPtr>()->list.front();
			const auto* Value = Filter.instancer->GetPropertySpecification().GetProperty("value");
			Filter.properties.SetProperty(Value->GetId(), Property(static_cast<float>(P.Opacity), Unit::NUMBER));
			List->list.push_back(std::move(Filter));
			Filters = std::move(List);
		}
		Element.SetProperty(PropertyId::Filter, Property(Filters, Unit::FILTER));
		return true;
	}
} // namespace VeroScript::RmlUi
