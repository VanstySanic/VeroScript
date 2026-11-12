// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiSource.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptLexer.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptTemplate.h"
#include "VeroScriptTypeScope.h"
#include <charconv>
#include <algorithm>
#include <stdexcept>
#include <sstream>

namespace VeroScript::Gui
{
	namespace
	{
		std::string GuiGenericSourceType(std::string Base, const std::vector<std::string>& Arguments)
		{
			if (Base == "attribute" || Base == "delegate" || Base == "multicast_delegate")
			{
				Base = "std." + Base;
			}
			std::vector<FTypePathSegment> Segments;
			if (!ParseTypePath(Base, Segments))
			{
				throw std::runtime_error("Invalid canonical GUI template path");
			}
			std::string Result;
			std::size_t NextArgument = 0;
			for (std::size_t Index = 0; Index < Segments.size(); ++Index)
			{
				auto Part = Segments[Index].Name;
				std::size_t Count = Index + 1 == Segments.size() ? Arguments.size() - NextArgument : 0;
				const auto Arity = Part.rfind("$arity");
				if (Arity != std::string::npos)
				{
					// Core flattens enclosing arguments before the leaf's own arguments.
					// The marker records how many belong to this particular owner.
					const auto* End = Part.data() + Part.size();
					const auto Parsed = std::from_chars(Part.data() + Arity + 6, End, Count);
					if (Index + 1 == Segments.size() || Parsed.ec != std::errc{} || Parsed.ptr != End ||
						!Arity || Count > Arguments.size() - NextArgument)
					{
						throw std::runtime_error("Invalid canonical GUI template owner arity");
					}
					Part.resize(Arity);
				}
				if (Segments[Index].bApplication)
				{
					throw std::runtime_error("Invalid canonical GUI template path");
				}
				if (!Result.empty())
				{
					Result += "::";
				}
				Result += Part;
				if (Count)
				{
					Result += '<';
					for (std::size_t Argument = 0; Argument < Count; ++Argument)
					{
						if (Argument)
						{
							Result += ',';
						}
						Result += Arguments[NextArgument++];
					}
					Result += '>';
				}
			}
			return Result;
		}
	} // namespace

	bool GuiHasTemplateParameters(const FAnnotationDeclarationView& Declaration)
	{
		return !Declaration.TypeParameters.empty() || !Declaration.TemplateParameters.empty();
	}

	std::string GuiScopeSuffix(const std::string& TypeName)
	{
		// Keep a private declaration's module identity while preserving the
		// normalized public spelling shared with generated markup components.
		const auto Identity = TypeName.substr(0, FindLinkageIdentityStart(TypeName)) + GuiSourceType(TypeName);
		std::uint64_t Hash = 14695981039346656037ull;
		for (const unsigned char C : Identity)
		{
			Hash = (Hash ^ C) * 1099511628211ull;
		}
		std::ostringstream Result;
		Result << "VsguiPage" << std::hex << Hash;
		return Result.str();
	}

	FCompileGenerationOperation GuiBodyOperation(const FCompileProcessingContext& Context,
												 const FAnnotationDeclarationView& Declaration, std::string Body)
	{
		FCompileGenerationOperation Result;
		Result.Kind = ECompileOperation::ReplaceBody;
		Result.TargetId = Declaration.Id;
		Result.Snapshot = Context.Snapshot;
		Result.Scope.ModuleId = Context.ModuleId;
		Result.Source = std::move(Body);
		Result.Origin.Range = Declaration.Metadata.SourceRange;
		const auto& Range = Declaration.Metadata.SourceRange;
		if (const auto* Document = Context.SourceMetadata.FindDocument(Range.SourceId);
			Document && !Declaration.BodySource.empty())
		{
			const auto Input = Document->Text.find(Declaration.BodySource, Range.Begin);
			const auto Output = Result.Source.find(Declaration.BodySource);
			if (Input != std::string::npos && Output != std::string::npos &&
				Input + Declaration.BodySource.size() <= Range.End)
			{
				FSourceOrigin Origin;
				Origin.Range.SourceId = Document->Id;
				Origin.Range.Begin = Input;
				Origin.Range.End = Input + Declaration.BodySource.size();
				Result.SourceMappings.push_back({Output, Output + Declaration.BodySource.size(), std::move(Origin)});
			}
		}
		return Result;
	}

	std::string GuiScopedBody(const std::string& Source, const std::string& TypeName, bool bAllowInternalChildren)
	{
		VeroScriptLexer Lexer(Source);
		const auto& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			throw std::runtime_error(Lexer.GetErrorMessage());
		}
		const auto Suffix = GuiScopeSuffix(TypeName);
		std::string Result;
		std::size_t Begin = 0;
		bool Elements = false;
		for (std::size_t I = 0; I < Tokens.size(); ++I)
		{
			const auto& Token = Tokens[I];
			if (Token.Kind == ETokenKind::EndOfFile)
			{
				break;
			}
			if (Token.Kind != ETokenKind::Identifier)
			{
				continue;
			}
			const bool Qualified = I > 0 && (Tokens[I - 1].Lexeme == "." || Tokens[I - 1].Lexeme == "::");
			const bool SelfQualified = I >= 2 && Tokens[I - 1].Lexeme == "." && Tokens[I - 2].Lexeme == "this";
			if (!Qualified && Token.Lexeme == "elements")
			{
				Elements = true;
			}
			std::string Replacement;
			if ((!Qualified || SelfQualified) && bAllowInternalChildren &&
				(Token.Lexeme == "AddInternalChild" || Token.Lexeme == "AddInternalChildren"))
			{
				Replacement = "VsguiScoped" + std::string(Token.Lexeme) + Suffix;
			}
			if (Replacement.empty())
			{
				continue;
			}
			const auto Offset = static_cast<std::size_t>(Token.Lexeme.data() - Source.data());
			Result.append(Source, Begin, Offset - Begin);
			Result += Replacement;
			Begin = Offset + Token.Lexeme.size();
		}
		if (!Begin && !Elements)
		{
			return Source;
		}
		Result.append(Source, Begin, std::string::npos);
		return Elements ? "{auto elements=VS::Gui::Elements(this,VsguiGetScope" + Suffix + "());" + Result + '}'
						: Result;
	}

	std::string GuiSourceType(std::string Name)
	{
		// Value arguments carry their type and exact bits, including private enum
		// identities. Decode them before removing an outer module linkage prefix.
		if (Name.compare(0, 7, "@value:") == 0)
		{
			FTemplateArgument Argument;
			if (!DecodeTemplateArgument(std::string_view(Name).substr(7), Argument) ||
				Argument.Kind != ETemplateParameterKind::Value)
			{
				throw std::runtime_error("Invalid canonical GUI template value");
			}
			const auto& Type = Argument.Value.Type;
			if (Type.bBoolean)
			{
				return Argument.Value.Bits ? "true" : "false";
			}
			FTemplateValueType Integer;
			TryGetTemplateValueType(Type.bUnsigned ? "uint64" : "int64", Integer);
			const auto Number = ConvertTemplateConstant(Argument.Value, Integer, true);
			if (!Number.Succeeded())
			{
				throw std::runtime_error("Invalid canonical GUI template value");
			}
			FTemplateArgument Literal;
			Literal.Kind = ETemplateParameterKind::Value;
			Literal.Value = Number.Value;
			auto Source = FormatTemplateArgument(Literal);
			if (Source == "-9223372036854775808")
			{
				Source = "(-9223372036854775807i64-1i64)";
			}
			else
			{
				Source += Type.bUnsigned ? "u64" : "i64";
			}
			// Keep a value expression unambiguous inside member and return type declarations.
			return "(cast<" + GuiSourceType(Type.Name) + ">(" + Source + "))";
		}
		if (Runtime::IsReferenceTypeName(Name))
		{
			return std::string(Runtime::IsConstReferenceTypeName(Name) ? "const ref " : "ref ") +
				   GuiSourceType(std::string(Runtime::StripReferenceTypeName(Name)));
		}
		std::vector<std::string> Parameters;
		std::string ReturnType;
		if (Runtime::ParseFunctionType(Name, Parameters, ReturnType))
		{
			for (auto& Parameter : Parameters)
			{
				Parameter = GuiSourceType(std::move(Parameter));
			}
			return Runtime::MakeFunctionTypeName(Parameters, GuiSourceType(std::move(ReturnType)));
		}
		// Builtin containers retain angle syntax around canonical element types.
		// Decode those elements before looking for an outer mangled instance.
		std::string First, Second;
		for (const auto Parse : {Runtime::ParseDynamicArrayElementType, Runtime::SplitSetElementType,
								 Runtime::ParseMultiSetElementType, Runtime::ParseQueueElementType,
								 Runtime::ParseStackElementType, Runtime::ParseChannelElementType,
								 Runtime::ParseDequeElementType})
		{
			if (Parse(Name, First))
			{
				return GuiSourceType(Name.substr(0, Name.find('<'))) + '<' + GuiSourceType(std::move(First)) + '>';
			}
		}
		for (const auto Parse : {Runtime::SplitMapType, Runtime::SplitOrderedMapType, Runtime::ParsePairType})
		{
			if (Parse(Name, First, Second))
			{
				return GuiSourceType(Name.substr(0, Name.find('<'))) + '<' + GuiSourceType(std::move(First)) + ',' +
					   GuiSourceType(std::move(Second)) + '>';
			}
		}
		const auto Linkage = FindLinkageIdentityStart(Name);
		Name.erase(0, Linkage);
		if (Runtime::IsScriptArgsArrayTypeName(Name))
		{
			return Name;
		}
		const auto ArraySuffix = [&Name](std::size_t Position)
		{
			const auto Suffix = Name.substr(Position);
			auto Remaining = "int" + Suffix;
			std::string Element;
			std::uint32_t Length = 0;
			while (Runtime::ParseNativeArrayType(Remaining, Element, Length))
			{
				Remaining = std::move(Element);
			}
			if (Remaining != "int" || !Runtime::ValidateNativeArrayTypeLimits("int" + Suffix))
			{
				throw std::runtime_error("Invalid canonical GUI array type");
			}
			return Suffix;
		};
		const auto Marker = FindGenericTypeMarker(Name);
		if (Marker != std::string::npos)
		{
			std::vector<std::string> Arguments;
			std::size_t Position = Marker + 1;
			for (; Position < Name.size() && Name[Position] != '[';)
			{
				std::size_t Length = 0;
				const auto Parsed = std::from_chars(Name.data() + Position, Name.data() + Name.size(), Length);
				if (Parsed.ec != std::errc{} || Parsed.ptr == Name.data() + Name.size() || *Parsed.ptr != ':')
				{
					throw std::runtime_error("Invalid canonical GUI type");
				}
				Position = static_cast<std::size_t>(Parsed.ptr - Name.data()) + 1;
				if (!Length || Length > Name.size() - Position)
				{
					throw std::runtime_error("Invalid canonical GUI type argument");
				}
				Arguments.push_back(GuiSourceType(Name.substr(Position, Length)));
				Position += Length;
			}
			return GuiGenericSourceType(Name.substr(0, Marker), Arguments) + ArraySuffix(Position);
		}
		const auto ClosingAngle = Name.rfind('>');
		const auto Bracket = Name.find('[', ClosingAngle == std::string::npos ? 0 : ClosingAngle + 1);
		if (Bracket != std::string::npos)
		{
			return GuiSourceType(Name.substr(0, Bracket)) + ArraySuffix(Bracket);
		}
		Name = StripTypeScopeMarkers(std::move(Name));
		std::string Result;
		for (const char C : Name)
		{
			Result += C == '.' ? "::" : std::string(1, C);
		}
		return Result;
	}

	std::string GuiQuote(std::string_view Text)
	{
		std::string Result = "\"";
		constexpr char Digits[] = "0123456789abcdef";
		for (const unsigned char C : Text)
		{
			if (C == '\\' || C == '"')
			{
				Result += '\\';
				Result += static_cast<char>(C);
			}
			else if (C < 32)
			{
				Result += "\\u00";
				Result += Digits[C >> 4];
				Result += Digits[C & 15];
			}
			else
			{
				Result += static_cast<char>(C);
			}
		}
		return Result + '"';
	}

	void GuiSubmitMembers(const FCompileProcessingContext& Context, FCompileProcessingOutput& Output,
						  const std::string& Owner, const std::string& Source, FAnnotationSourcePosition Location,
						  const std::vector<FSourceDocument::FMapping>& Mappings)
	{
		VeroScriptLexer Lexer(Source);
		const auto& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			throw std::runtime_error(Lexer.GetErrorMessage());
		}
		std::string Access = "private:";
		std::size_t Begin = 0;
		unsigned Braces = 0, Parentheses = 0;
		for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
		{
			const auto& Token = Tokens[Index];
			if (Token.Kind == ETokenKind::EndOfFile)
			{
				break;
			}
			if (!Braces && !Parentheses &&
				(Token.Lexeme == "public" || Token.Lexeme == "protected" || Token.Lexeme == "private") &&
				Index + 1 < Tokens.size() && Tokens[Index + 1].Lexeme == ":")
			{
				Access = std::string(Token.Lexeme) + ':';
				const auto& Colon = Tokens[++Index];
				Begin = static_cast<std::size_t>(Colon.Lexeme.data() - Source.data()) + Colon.Lexeme.size();
				continue;
			}
			if (Token.Kind != ETokenKind::Symbol)
			{
				continue;
			}
			if (Token.Lexeme == "(")
			{
				++Parentheses;
			}
			if (Token.Lexeme == ")")
			{
				--Parentheses;
			}
			if (Token.Lexeme == "{")
			{
				++Braces;
			}
			if (Token.Lexeme == "}")
			{
				--Braces;
			}
			if (!Braces && !Parentheses && (Token.Lexeme == ";" || Token.Lexeme == "}"))
			{
				const auto End = static_cast<std::size_t>(Token.Lexeme.data() - Source.data()) + Token.Lexeme.size();
				FCompileGenerationOperation Operation;
				Operation.Kind = ECompileOperation::AddDeclaration;
				Operation.Scope = {Context.ModuleId, {}, Owner, Context.Snapshot};
				Operation.Source = Access + Source.substr(Begin, End - Begin);
				Operation.Origin.Range.SourceId = Location.FileLabel;
				Operation.Origin.Range.Position.Begin = Location;
				Operation.Origin.Range.Position.End = Location;
				for (const auto& Mapping : Mappings)
				{
					const auto Left = std::max(Begin, Mapping.Begin), Right = std::min(End, Mapping.End);
					if (Left >= Right)
					{
						continue;
					}
					auto Origin = Mapping.Origin;
					Origin.Range.Begin += Left - Mapping.Begin;
					Origin.Range.End = Origin.Range.Begin + Right - Left;
					Operation.SourceMappings.push_back(
						{Access.size() + Left - Begin, Access.size() + Right - Begin, std::move(Origin)});
				}
				Output.SubmitOperation(std::move(Operation));
				Begin = End;
			}
		}
	}

} // namespace VeroScript::Gui
