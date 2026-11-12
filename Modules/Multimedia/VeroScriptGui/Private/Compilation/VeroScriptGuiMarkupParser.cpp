// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiMarkupInternal.h"
#include <charconv>

namespace VeroScript::Gui
{
	namespace
	{
		class FParser
		{
		public:
			FParser(const FMarkupSource& InSource, const FMarkupCompileOptions& InOptions,
					FMarkupDiagnostic& InDiagnostic)
				: Source(InSource), Locations(InSource), Options(InOptions), Diagnostic(InDiagnostic)
			{
			}

			bool Parse(FMarkupElement& Root)
			{
				if (Source.Identity.empty())
				{
					return Fail("gui.source_identity_required");
				}
				if (Source.Text.size() > Options.MaxSourceBytes)
				{
					return Fail("gui.source_limit");
				}
				if (!ValidateMarkupUtf8(Source, Diagnostic))
				{
					return false;
				}
				if (At("\xef\xbb\xbf"))
				{
					Offset += 3;
				}
				SkipSpace();
				if (At("<?xml"))
				{
					const auto End = Source.Text.find("?>", Offset + 5);
					if (End == std::string::npos)
					{
						return Fail("gui.unclosed_xml_declaration");
					}
					const auto Declaration = Source.Text.substr(Offset, End + 2 - Offset);
					if (Declaration.find("encoding") != std::string::npos &&
						Declaration.find("utf-8") == std::string::npos &&
						Declaration.find("UTF-8") == std::string::npos)
					{
						return Fail("gui.utf8_required");
					}
					Offset = End + 2;
				}
				if (!SkipTrivia() || !Element(Root, 1) || !SkipTrivia())
				{
					return false;
				}
				if (Offset != Source.Text.size())
				{
					return Fail("gui.single_root_required");
				}
				return true;
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

			void SkipSpace()
			{
				while (Offset < Source.Text.size() &&
					   std::string_view(" \r\n\t").find(Source.Text[Offset]) != std::string_view::npos)
				{
					++Offset;
				}
			}

			bool SkipTrivia()
			{
				SkipSpace();
				while (At("<!--"))
				{
					if (!Comment())
					{
						return false;
					}
					SkipSpace();
				}
				return true;
			}

			bool Comment()
			{
				const auto End = Source.Text.find("-->", Offset + 4);
				if (End == std::string::npos || Source.Text.find("--", Offset + 4) != End)
				{
					return Fail("gui.invalid_comment");
				}
				Offset = End + 3;
				return true;
			}

			bool Name(std::string& Value)
			{
				const auto Begin = Offset;
				while (Offset < Source.Text.size())
				{
					const unsigned char C = static_cast<unsigned char>(Source.Text[Offset]);
					if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_' ||
						  C == '-' || C == '.' || C == ':' || C >= 128))
					{
						break;
					}
					++Offset;
				}
				Value = Source.Text.substr(Begin, Offset - Begin);
				const auto Colon = Value.find(':');
				if (Colon == std::string::npos)
				{
					return IsMarkupName(Value) || Fail("gui.invalid_name");
				}
				return (IsMarkupName(std::string_view(Value).substr(0, Colon)) &&
						IsMarkupName(std::string_view(Value).substr(Colon + 1))) ||
					   Fail("gui.invalid_name");
			}

			void MapText(std::vector<FMarkupTextSpan>& Spans, std::size_t Begin, std::size_t End,
						 std::size_t SourceBegin)
			{
				if (Begin == End)
				{
					return;
				}
				if (!Spans.empty() && Spans.back().End == Begin &&
					Spans.back().SourceBegin + Begin - Spans.back().Begin == SourceBegin)
				{
					Spans.back().End = End;
					return;
				}
				Spans.push_back({Begin, End, SourceBegin});
			}

			bool Decode(std::string_view Text, std::string& Value, std::size_t Begin,
						std::vector<FMarkupTextSpan>& Spans)
			{
				for (std::size_t Index = 0; Index < Text.size(); ++Index)
				{
					if ((Index & 4095) == 0 && Options.CheckCancelled)
					{
						Options.CheckCancelled();
					}
					const auto OutputBegin = Value.size();
					if (Text[Index] != '&')
					{
						Value += Text[Index];
						MapText(Spans, OutputBegin, Value.size(), Begin + Index);
						continue;
					}
					const auto End = Text.find(';', Index + 1);
					const auto Entity = Text.substr(Index + 1, End == std::string_view::npos ? 0 : End - Index - 1);
					if (End == std::string_view::npos || Entity.empty())
					{
						Offset = Begin + Index;
						return Fail("gui.invalid_entity");
					}
					if (Entity == "amp")
					{
						Value += '&';
					}
					else if (Entity == "lt")
					{
						Value += '<';
					}
					else if (Entity == "gt")
					{
						Value += '>';
					}
					else if (Entity == "quot")
					{
						Value += '"';
					}
					else if (Entity == "apos")
					{
						Value += '\'';
					}
					else
					{
						std::uint32_t Code = 0;
						const bool bHex = Entity.size() > 2 && Entity[1] == 'x';
						const auto Number = Entity.substr(bHex ? 2 : 1);
						const auto Result =
							std::from_chars(Number.data(), Number.data() + Number.size(), Code, bHex ? 16 : 10);
						if (Entity.empty() || Entity.front() != '#' || Number.empty() || Result.ec != std::errc{} ||
							Result.ptr != Number.data() + Number.size() || Code > 0x10ffff ||
							(Code >= 0xd800 && Code <= 0xdfff) || (Code < 32 && Code != 9 && Code != 10 && Code != 13))
						{
							Offset = Begin + Index;
							return Fail("gui.invalid_entity");
						}
						if (Code < 128)
						{
							Value += static_cast<char>(Code);
						}
						else
						{
							const int Count = Code < 0x800 ? 2 : Code < 0x10000 ? 3 : 4;
							Value += static_cast<char>((Count == 2	 ? 0xc0
														: Count == 3 ? 0xe0
																	 : 0xf0) |
													   (Code >> (6 * (Count - 1))));
							for (int Part = Count - 2; Part >= 0; --Part)
							{
								Value += static_cast<char>(0x80 | ((Code >> (6 * Part)) & 0x3f));
							}
						}
					}
					MapText(Spans, OutputBegin, Value.size(), Begin + Index);
					Index = End;
				}
				return true;
			}

			bool Element(FMarkupElement& Result, std::size_t Depth)
			{
				if (Options.CheckCancelled)
				{
					Options.CheckCancelled();
				}
				if (Depth > Options.MaxDepth || ++Nodes > Options.MaxNodes)
				{
					return Fail("gui.tree_limit");
				}
				Result.Location = Locations.At(Offset);
				if (!At("<"))
				{
					return Fail("gui.element_expected");
				}
				++Offset;
				if (!Name(Result.Name))
				{
					return false;
				}
				std::set<std::string> Names;
				while (true)
				{
					const auto Before = Offset;
					SkipSpace();
					if (At("/>"))
					{
						Result.TextLocation = Locations.At(Offset);
						Offset += 2;
						return true;
					}
					if (At(">"))
					{
						++Offset;
						break;
					}
					if (Before == Offset)
					{
						return Fail("gui.attribute_separator_expected");
					}
					FMarkupAttribute Attribute;
					Attribute.Location = Locations.At(Offset);
					if (!Name(Attribute.Name))
					{
						return false;
					}
					if (!Names.insert(Attribute.Name).second)
					{
						return Fail("gui.duplicate_attribute: " + Attribute.Name);
					}
					SkipSpace();
					if (!At("="))
					{
						return Fail("gui.attribute_equals_expected");
					}
					++Offset;
					SkipSpace();
					if (!At("\"") && !At("'"))
					{
						return Fail("gui.quoted_attribute_required");
					}
					const char Quote = Source.Text[Offset++];
					const auto Begin = Offset;
					Attribute.ValueLocation = Locations.At(Begin);
					const auto End = Source.Text.find(Quote, Begin);
					if (End == std::string::npos)
					{
						return Fail("gui.unclosed_attribute");
					}
					if (Source.Text.find('<', Begin) < End)
					{
						return Fail("gui.attribute_lt_requires_entity");
					}
					if (!Decode(std::string_view(Source.Text).substr(Begin, End - Begin), Attribute.Value, Begin,
								Attribute.ValueSpans))
					{
						return false;
					}
					Offset = End + 1;
					Result.Attributes.push_back(std::move(Attribute));
				}
				Result.TextLocation = Locations.At(Offset);
				while (Offset < Source.Text.size())
				{
					if (At("</"))
					{
						Offset += 2;
						std::string Closing;
						if (!Name(Closing))
						{
							return false;
						}
						SkipSpace();
						if (Closing != Result.Name || !At(">"))
						{
							return Fail("gui.mismatched_close_tag: " + Result.Name);
						}
						++Offset;
						return true;
					}
					if (At("<!--"))
					{
						if (!Comment())
						{
							return false;
						}
					}
					else if (At("<![CDATA["))
					{
						Offset += 9;
						if (Result.Text.empty())
						{
							Result.TextLocation = Locations.At(Offset);
						}
						const auto End = Source.Text.find("]]>", Offset);
						if (End == std::string::npos)
						{
							return Fail("gui.unclosed_cdata");
						}
						const auto OutputBegin = Result.Text.size();
						Result.Text += Source.Text.substr(Offset, End - Offset);
						MapText(Result.TextSpans, OutputBegin, Result.Text.size(), Offset);
						Offset = End + 3;
					}
					else if (At("<"))
					{
						FMarkupElement Child;
						if (!Element(Child, Depth + 1))
						{
							return false;
						}
						Result.Children.push_back(std::move(Child));
					}
					else
					{
						const auto End = Source.Text.find('<', Offset);
						if (End == std::string::npos)
						{
							return Fail("gui.unclosed_element: " + Result.Name);
						}
						if (!Decode(std::string_view(Source.Text).substr(Offset, End - Offset), Result.Text, Offset,
									Result.TextSpans))
						{
							return false;
						}
						Offset = End;
					}
				}
				return Fail("gui.unclosed_element: " + Result.Name);
			}

			const FMarkupSource& Source;
			FMarkupLocator Locations;
			const FMarkupCompileOptions& Options;
			FMarkupDiagnostic& Diagnostic;
			std::size_t Offset = 0, Nodes = 0;
		};
	} // namespace

	bool ParseMarkup(const FMarkupSource& Source, const FMarkupCompileOptions& Options, FMarkupElement& Root,
					 FMarkupDiagnostic& Diagnostic)
	{
		return FParser(Source, Options, Diagnostic).Parse(Root);
	}
} // namespace VeroScript::Gui
