// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiMarkupInternal.h"

namespace VeroScript::Gui
{
	namespace
	{
		class FStyleParser
		{
		public:
			FStyleParser(const FMarkupSource& InSource, FMarkupDiagnostic& InDiagnostic, bool InValidateProperties)
			    : Source(InSource), Locations(InSource), Diagnostic(InDiagnostic),
			      bValidateProperties(InValidateProperties)
			{
			}

			bool Parse(FStyleSheetData& Result)
			{
				if (Source.Text.size() > 16 * 1024 * 1024)
				{
					return Fail("gui.style_source_limit");
				}
				if (!ValidateMarkupUtf8(Source, Diagnostic))
				{
					return false;
				}
				Result.Source = Source;
				if (At("\xef\xbb\xbf"))
				{
					Offset += 3;
				}
				while (Skip())
				{
					if (Offset == Source.Text.size())
					{
						return true;
					}
					FStyleRule Rule;
					if (At("@"))
					{
						return Fail("gui.unsupported_style_at_rule");
					}
					do
					{
						FStyleSelector Selector;
						if (!ParseSelector(Selector))
						{
							return false;
						}
						Rule.Selectors.push_back(std::move(Selector));
						if (!At(","))
						{
							break;
						}
						++Offset;
						if (!Skip())
						{
							return false;
						}
					}
					while (true);
					if (!At("{"))
					{
						return Fail("gui.style_block_expected");
					}
					++Offset;
					while (Skip() && !At("}"))
					{
						FStyleDeclaration Declaration;
						Declaration.Location = Locations.At(Offset);
						Declaration.Name = Identifier(true);
						if (Declaration.Name.empty() || !Skip() || !At(":"))
						{
							return Fail("gui.style_declaration_expected");
						}
						++Offset;
						if (!Value(Declaration.Value))
						{
							return false;
						}
						if (Declaration.Value.empty())
						{
							return Fail("gui.empty_style_value");
						}
						if (Declaration.Name.compare(0, 2, "--") != 0)
						{
							EProperty Property;
							FPropertyValue Parsed;
							std::string Error;
							const bool Known = FindMarkupProperty(Declaration.Name, Property, true, Error);
							if (!Known && bValidateProperties)
							{
								return MarkupFail(Diagnostic, Declaration.Location, Error);
							}
							if (Known && Declaration.Value.find("var(") == std::string::npos)
							{
								if (ParseMarkupProperty(Declaration.Name, Declaration.Value, Property, Parsed, true, Error))
								{
									if (Property != EProperty::BackgroundImage && Property != EProperty::MaskImage)
									{
										Declaration.Literal = std::make_pair(Property, std::move(Parsed));
									}
								}
								else if (bValidateProperties)
								{
									return MarkupFail(Diagnostic, Declaration.Location, Error);
								}
							}
						}
						else if (Declaration.Name.compare(0, 2, "--") == 0 && Declaration.Name.size() == 2)
						{
							return Fail("gui.invalid_style_variable");
						}
						Rule.Declarations.push_back(std::move(Declaration));
						if (At(";"))
						{
							++Offset;
						}
						if (Rule.Declarations.size() > 100000)
						{
							return Fail("gui.style_declaration_limit");
						}
					}
					if (!At("}"))
					{
						return Fail("gui.unclosed_style_block");
					}
					++Offset;
					Result.Rules.push_back(std::move(Rule));
					if (Result.Rules.size() > 100000)
					{
						return Fail("gui.style_rule_limit");
					}
				}
				return false;
			}

		private:
			bool At(std::string_view Value) const
			{
				return Source.Text.compare(Offset, Value.size(), Value) == 0;
			}

			bool Fail(std::string Message)
			{
				return MarkupFail(Diagnostic, Locations.At(Offset), std::move(Message));
			}

			bool Skip()
			{
				while (Offset < Source.Text.size())
				{
					if (std::string_view(" \r\n\t\f").find(Source.Text[Offset]) != std::string_view::npos)
					{
						++Offset;
					}
					else if (At("/*"))
					{
						const auto End = Source.Text.find("*/", Offset + 2);
						if (End == std::string::npos)
						{
							return Fail("gui.unclosed_style_comment");
						}
						Offset = End + 2;
					}
					else
					{
						break;
					}
				}
				return true;
			}

			std::string Identifier(bool bVariable = false)
			{
				const auto Begin = Offset;
				if (bVariable && At("--"))
				{
					Offset += 2;
				}
				while (Offset < Source.Text.size())
				{
					const unsigned char C = static_cast<unsigned char>(Source.Text[Offset]);
					if (!((C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || C == '_' || C == '-' ||
					        (C >= '0' && C <= '9') || C >= 128))
					{
						break;
					}
					++Offset;
				}
				const auto Name = Source.Text.substr(Begin, Offset - Begin);
				if (!IsMarkupName(bVariable && Name.compare(0, 2, "--") == 0 ? std::string_view(Name).substr(2)
				                                                             : std::string_view(Name)))
				{
					return {};
				}
				return Name;
			}

			bool ParseSelector(FStyleSelector& Selector)
			{
				if (!Skip())
				{
					return false;
				}
				char Relation = ' ';
				while (true)
				{
					FSelectorPart Part;
					Part.Relation = Relation;
					const auto Begin = Offset;
					if (At("*"))
					{
						++Offset;
					}
					else if (!At(".") && !At("#") && !At(":"))
					{
						Part.Type = Identifier();
						if (Part.Type.empty())
						{
							return Fail("gui.style_selector_expected");
						}
						++Selector.Specificity[2];
					}
					while (At(".") || At("#") || At(":"))
					{
						const char Prefix = Source.Text[Offset++];
						const auto Name = Identifier();
						if (Name.empty())
						{
							return Fail("gui.style_selector_name_expected");
						}
						if (Prefix == '.')
						{
							Part.Classes.push_back(Name);
							++Selector.Specificity[1];
						}
						else if (Prefix == '#')
						{
							if (!Part.Id.empty())
							{
								return Fail("gui.duplicate_selector_id");
							}
							Part.Id = Name;
							++Selector.Specificity[0];
						}
						else
						{
							const std::map<std::string, ESelectorState> States{
							    {"root", ESelectorState::Root},
							    {"hover", ESelectorState::Hover},
							    {"active", ESelectorState::Active},
							    {"checked", ESelectorState::Checked},
							    {"read-only", ESelectorState::ReadOnly},
							    {"focus", ESelectorState::Focus},
							    {"focus-within", ESelectorState::FocusWithin},
							    {"disabled", ESelectorState::Disabled}};
							const auto Found = States.find(Name);
							if (Found == States.end())
							{
								return Fail("gui.unsupported_pseudo_class: " + Name);
							}
							Part.States.push_back(Found->second);
							++Selector.Specificity[1];
						}
					}
					if (Offset == Begin)
					{
						return Fail("gui.style_selector_expected");
					}
					Selector.Parts.push_back(std::move(Part));
					if (Selector.Parts.size() > 256)
					{
						return Fail("gui.style_selector_depth");
					}
					const auto BeforeSpace = Offset;
					if (!Skip())
					{
						return false;
					}
					if (At(",") || At("{"))
					{
						return true;
					}
					if (At(">"))
					{
						Relation = '>';
						++Offset;
						if (!Skip())
						{
							return false;
						}
					}
					else if (BeforeSpace != Offset)
					{
						Relation = ' ';
					}
					else
					{
						return Fail("gui.unsupported_selector_syntax");
					}
				}
			}

			bool Value(std::string& Result)
			{
				unsigned Depth = 0;
				char Quote = 0;
				while (Offset < Source.Text.size())
				{
					const char C = Source.Text[Offset];
					if (!Quote && Depth == 0 && (C == ';' || C == '}'))
					{
						Result = TrimMarkup(Result);
						return true;
					}
					if (!Quote && At("/*"))
					{
						if (!Skip())
						{
							return false;
						}
						Result += ' ';
						continue;
					}
					if (C == '\\')
					{
						if (!Quote || Offset + 1 >= Source.Text.size())
						{
							return Fail("gui.unsupported_style_escape");
						}
						Result += C;
						Result += Source.Text[++Offset];
						++Offset;
						continue;
					}
					if (!Quote && (C == '{' || C == '!' || C == '@'))
					{
						return Fail("gui.unsupported_style_value");
					}
					if (Quote)
					{
						if (C == Quote)
						{
							Quote = 0;
						}
					}
					else if (C == '\'' || C == '"')
					{
						Quote = C;
					}
					else if (C == '(')
					{
						if (++Depth > 64)
						{
							return Fail("gui.style_value_depth");
						}
					}
					else if (C == ')')
					{
						if (!Depth)
						{
							return Fail("gui.unexpected_style_parenthesis");
						}
						--Depth;
					}
					Result += C;
					++Offset;
				}
				return Fail("gui.unclosed_style_value");
			}

			const FMarkupSource& Source;
			FMarkupLocator Locations;
			FMarkupDiagnostic& Diagnostic;
			bool bValidateProperties;
			std::size_t Offset = 0;
		};
	} // namespace

	bool ParseStyleSheet(const FMarkupSource& Source, FStyleSheetData& Result, FMarkupDiagnostic& Diagnostic,
	    bool bValidateProperties)
	{
		return FStyleParser(Source, Diagnostic, bValidateProperties).Parse(Result);
	}
} // namespace VeroScript::Gui
