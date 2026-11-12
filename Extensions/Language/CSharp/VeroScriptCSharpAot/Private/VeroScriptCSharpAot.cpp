// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNumericBindings.h"
#include "VeroScriptJsonString.h"
#include "VeroScriptCSharpAot.h"

#include "VeroScriptAst.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptConstantEvaluation.h"
#include "VeroScriptLocale.h"
#include "VeroScriptLexer.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptPath.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptStandardLibrary.h"
#include "VeroScriptVariant.h"
#include "VeroScriptStandardLibraryServices.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <type_traits>
#include <utility>
#include <unordered_map>
#include <unordered_set>

#ifndef VEROSCRIPT_CSHARP_AOT_VERSION
#define VEROSCRIPT_CSHARP_AOT_VERSION "0.1.1"
#endif

namespace VeroScript
{
	namespace
	{
		std::string Trim(std::string Text)
		{
			const auto IsSpace = [](const unsigned char Character)
			{
				return std::isspace(Character) != 0;
			};
			Text.erase(Text.begin(), std::find_if(Text.begin(), Text.end(), [&](const char Character)
			{
				return !IsSpace(static_cast<unsigned char>(Character));
			}));
			Text.erase(std::find_if(Text.rbegin(), Text.rend(),
			               [&](const char Character)
			{
				return !IsSpace(static_cast<unsigned char>(Character));
			}).base(),
			    Text.end());
			return Text;
		}

		std::vector<std::string> SplitTypeArguments(const std::string_view Text)
		{
			std::vector<std::string> Result;
			std::size_t Start = 0;
			int GenericDepth = 0;
			int FunctionDepth = 0;
			for (std::size_t Index = 0; Index < Text.size(); ++Index)
			{
				if (Text[Index] == '<')
				{
					++GenericDepth;
				}
				else if (Text[Index] == '>' && (Index == 0 || Text[Index - 1] != '-'))
				{
					--GenericDepth;
				}
				else if (Text[Index] == '(')
				{
					++FunctionDepth;
				}
				else if (Text[Index] == ')')
				{
					--FunctionDepth;
				}
				else if (Text[Index] == ',' && GenericDepth == 0 && FunctionDepth == 0)
				{
					Result.push_back(Trim(std::string(Text.substr(Start, Index - Start))));
					Start = Index + 1;
				}
			}
			Result.push_back(Trim(std::string(Text.substr(Start))));
			return Result;
		}

		bool IsCSharpKeyword(const std::string_view Name)
		{
			static const std::unordered_set<std::string> Keywords = {
			    "abstract", "as", "base", "bool", "break", "byte", "case", "catch",
			    "char", "checked", "class", "const", "continue", "decimal", "default",
			    "delegate", "do", "double", "else", "enum", "event", "explicit", "extern",
			    "false", "finally", "fixed", "float", "for", "foreach", "goto", "if",
			    "implicit", "in", "int", "interface", "internal", "is", "lock", "long",
			    "namespace", "new", "null", "object", "operator", "out", "override",
			    "params", "private", "protected", "public", "readonly", "ref", "return",
			    "sbyte", "sealed", "short", "sizeof", "stackalloc", "static", "string",
			    "struct", "switch", "this", "throw", "true", "try", "typeof", "uint",
			    "ulong", "unchecked", "unsafe", "ushort", "using", "virtual", "void",
			    "volatile", "while"};
			return Keywords.find(std::string(Name)) != Keywords.end();
		}

		std::string Identifier(std::string Name)
		{
			for (std::size_t Position = 0;
			    (Position = Name.find("::", Position)) != std::string::npos;)
			{
				Name.replace(Position, 2, ".");
			}
			std::ostringstream Result;
			std::size_t Start = 0;
			while (Start <= Name.size())
			{
				const std::size_t Dot = Name.find('.', Start);
				std::string Part = Name.substr(Start,
				    Dot == std::string::npos ? std::string::npos : Dot - Start);
				Part.erase(std::remove(Part.begin(), Part.end(), '.'), Part.end());
				if (Part.empty())
				{
					Part = "_";
				}
				for (char& Character : Part)
				{
					const unsigned char Byte = static_cast<unsigned char>(Character);
					if (Byte < 0x80 && Character != '_' && !std::isalnum(Byte))
					{
						Character = '_';
					}
				}
				if (std::isdigit(static_cast<unsigned char>(Part.front())))
				{
					Part.insert(Part.begin(), '_');
				}
				if (IsCSharpKeyword(Part))
				{
					Result << '@';
				}
				Result << Part;
				if (Dot == std::string::npos)
				{
					break;
				}
				Result << '.';
				Start = Dot + 1;
			}
			return Result.str();
		}

		std::string EncodeIdentifierBytes(const std::string_view Text)
		{
			std::ostringstream Result;
			Result << std::uppercase << std::hex << std::setfill('0');
			for (const unsigned char Character : Text)
			{
				const bool bAsciiAlphaNumeric =
				    (Character >= 'a' && Character <= 'z') || (Character >= 'A' && Character <= 'Z') || (Character >= '0' && Character <= '9');
				if (bAsciiAlphaNumeric)
				{
					Result << static_cast<char>(Character);
				}
				else
				{
					Result << '_' << std::setw(2) << static_cast<unsigned int>(Character);
				}
			}
			return Result.str();
		}

		std::string SymbolIdentifier(std::string Name)
		{
			for (std::size_t Position = 0; Position < Name.size(); ++Position)
			{
				if (Name[Position] != '.')
				{
					continue;
				}
				Name.replace(Position, 1, "::");
				++Position;
			}
			return "S" + std::to_string(Name.size()) + "_" + EncodeIdentifierBytes(Name);
		}

		std::string CanonicalSymbol(std::string Name)
		{
			for (std::size_t Position = 0; Position < Name.size(); ++Position)
			{
				if (Name[Position] != '.')
				{
					continue;
				}
				Name.replace(Position, 1, "::");
				++Position;
			}
			return Name;
		}

		std::string EncodeModuleSuffix(const std::string_view Text)
		{
			std::string Encoded = EncodeIdentifierBytes(Text);
			return Encoded.empty() ? "local" : Encoded;
		}

		std::string EscapeCSharpByteString(std::string_view Value);

		std::string EscapeCSharpString(const std::string_view Value)
		{
			return EscapeCSharpByteString(Value);
		}

		std::string EscapeCSharpByteString(const std::string_view Value)
		{
			std::ostringstream Out;
			static constexpr char Hex[] = "0123456789ABCDEF";
			const auto IsContinuation = [](const unsigned char Byte)
			{
				return Byte >= 0x80u && Byte <= 0xbfu;
			};
			const auto EmitCodeUnit = [&](const std::uint16_t CodeUnit)
			{
				Out << "\\u";
				for (int Shift = 12; Shift >= 0; Shift -= 4)
				{
					Out << Hex[(CodeUnit >> Shift) & 0x0fu];
				}
			};
			for (std::size_t Index = 0; Index < Value.size();)
			{
				const unsigned char Character = static_cast<unsigned char>(Value[Index]);
				if (Character < 0x80u)
				{
					switch (Character)
					{
					case '\\':
						{
							Out << "\\\\";
							break;
						}
					case '"':
						{
							Out << "\\\"";
							break;
						}
					case '\n':
						{
							Out << "\\n";
							break;
						}
					case '\r':
						{
							Out << "\\r";
							break;
						}
					case '\t':
						{
							Out << "\\t";
							break;
						}
					case '\0':
						{
							Out << "\\0";
							break;
						}
					default:
						{
							if (Character < 0x20u || Character == 0x7fu)
							{
								Out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
								    << static_cast<unsigned>(Character) << std::dec << std::setfill(' ');
							}
							else
							{
								Out << static_cast<char>(Character);
							}
							break;
						}
					}
					++Index;
					continue;
				}

				std::size_t Length = 0;
				if (Character >= 0xc2u && Character <= 0xdfu && Index + 1 < Value.size() && IsContinuation(static_cast<unsigned char>(Value[Index + 1])))
				{
					Length = 2;
				}
				else if (Character >= 0xe0u && Character <= 0xefu && Index + 2 < Value.size())
				{
					const unsigned char Second = static_cast<unsigned char>(Value[Index + 1]);
					const bool bValidSecond = Character == 0xe0u
					    ? Second >= 0xa0u && Second <= 0xbfu
					    : Character == 0xedu
					    ? Second >= 0x80u && Second <= 0x9fu
					    : IsContinuation(Second);
					if (bValidSecond && IsContinuation(static_cast<unsigned char>(Value[Index + 2])))
					{
						Length = 3;
					}
				}
				else if (Character >= 0xf0u && Character <= 0xf4u && Index + 3 < Value.size())
				{
					const unsigned char Second = static_cast<unsigned char>(Value[Index + 1]);
					const bool bValidSecond = Character == 0xf0u
					    ? Second >= 0x90u && Second <= 0xbfu
					    : Character == 0xf4u
					    ? Second >= 0x80u && Second <= 0x8fu
					    : IsContinuation(Second);
					if (bValidSecond && IsContinuation(static_cast<unsigned char>(Value[Index + 2])) && IsContinuation(static_cast<unsigned char>(Value[Index + 3])))
					{
						Length = 4;
					}
				}

				if (Length != 0)
				{
					std::uint32_t CodePoint = Character & (Length == 2 ? 0x1fu : Length == 3 ? 0x0fu
					                                                                         : 0x07u);
					for (std::size_t Offset = 1; Offset < Length; ++Offset)
					{
						CodePoint = (CodePoint << 6u) | (static_cast<unsigned char>(Value[Index + Offset]) & 0x3fu);
					}
					if (CodePoint == 0x0085u || CodePoint == 0x2028u || CodePoint == 0x2029u)
					{
						EmitCodeUnit(static_cast<std::uint16_t>(CodePoint));
					}
					else
					{
						Out.write(Value.data() + Index, static_cast<std::streamsize>(Length));
					}
					Index += Length;
					continue;
				}

				// VeroScript strings are byte strings and \xNN may deliberately form
				// invalid UTF-8. Mirror VsUtf8Decode's lossless low-surrogate sentinel.
				// Valid C# newline scalars are escaped separately above so U+0085,
				// U+2028, and U+2029 cannot break the generated source file.
				Out << "\\uDC" << Hex[Character >> 4u] << Hex[Character & 0x0fu];
				++Index;
			}
			return Out.str();
		}

		std::string EscapeWideString(const std::u16string_view Value)
		{
			std::ostringstream Out;
			for (const char16_t Character : Value)
			{
				if (Character == u'\\')
				{
					Out << "\\\\";
				}
				else if (Character == u'"')
				{
					Out << "\\\"";
				}
				else if (Character == u'\n')
				{
					Out << "\\n";
				}
				else if (Character == u'\r')
				{
					Out << "\\r";
				}
				else if (Character == u'\t')
				{
					Out << "\\t";
				}
				else if (Character >= 0x20 && Character <= 0x7e)
				{
					Out << static_cast<char>(Character);
				}
				else
				{
					Out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
					    << static_cast<unsigned>(Character) << std::dec << std::setfill(' ');
				}
			}
			return Out.str();
		}

		const char* BinaryOperator(const EBinaryOp Op)
		{
			switch (Op)
			{
			case EBinaryOp::Add:
				return "+";
			case EBinaryOp::Subtract:
				return "-";
			case EBinaryOp::Multiply:
				return "*";
			case EBinaryOp::Divide:
				return "/";
			case EBinaryOp::Modulo:
				return "%";
			case EBinaryOp::Equal:
				return "==";
			case EBinaryOp::NotEqual:
				return "!=";
			case EBinaryOp::Less:
				return "<";
			case EBinaryOp::LessEqual:
				return "<=";
			case EBinaryOp::Greater:
				return ">";
			case EBinaryOp::GreaterEqual:
				return ">=";
			case EBinaryOp::LogicalAnd:
				return "&&";
			case EBinaryOp::LogicalOr:
				return "||";
			case EBinaryOp::BitAnd:
				return "&";
			case EBinaryOp::BitOr:
				return "|";
			case EBinaryOp::BitXor:
				return "^";
			case EBinaryOp::ShiftLeft:
				return "<<";
			case EBinaryOp::ShiftRight:
				return ">>";
			case EBinaryOp::In:
				return nullptr;
			default:
				{
					break;
				}
			}
			return nullptr;
		}

		std::string CSharpDiagnostic(const std::string_view EnglishMessage)
		{
			return "C# AOT: " + Locale::Translate(EnglishMessage);
		}

		bool ValidateBundleName(const std::string_view BundleName, std::string& OutError)
		{
			if (BundleName.empty())
			{
				OutError = CSharpDiagnostic("source-bundle name must not be empty");
				return false;
			}
			for (const unsigned char Character : BundleName)
			{
				if (!((Character >= 'a' && Character <= 'z') || (Character >= 'A' && Character <= 'Z') || (Character >= '0' && Character <= '9') || Character == '_' || Character == '-'))
				{
					OutError = CSharpDiagnostic(
					    "source-bundle name must be a portable file stem");
					return false;
				}
			}
			std::string UpperName(BundleName);
			std::transform(UpperName.begin(), UpperName.end(), UpperName.begin(), [](const char Character)
			{
				return Character >= 'a' && Character <= 'z'
				    ? static_cast<char>(Character - 'a' + 'A')
				    : Character;
			});
			// These Windows device names remain reserved even with .csproj/.cs extensions.
			if (UpperName == "CON" || UpperName == "PRN" || UpperName == "AUX" || UpperName == "NUL" || (UpperName.size() == 4 && (UpperName.compare(0, 3, "COM") == 0 || UpperName.compare(0, 3, "LPT") == 0) && UpperName[3] >= '1' && UpperName[3] <= '9'))
			{
				OutError = CSharpDiagnostic("source-bundle name must be a portable file stem");
				return false;
			}
			return true;
		}

		std::string JsonEscape(const std::string_view Value)
		{
			return JsonStringDetail::Escape(Value, JsonStringDetail::EControlEscapeStyle::Unicode);
		}

		std::string BuildSdkProject(const std::vector<std::string>& CompilationUnits)
		{
			std::ostringstream Project;
			Project << R"XML(<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
	<OutputType>Exe</OutputType>
	<TargetFramework>net8.0</TargetFramework>
	<Nullable>enable</Nullable>
	<ImplicitUsings>disable</ImplicitUsings>
	<TreatWarningsAsErrors>true</TreatWarningsAsErrors>
	<Deterministic>true</Deterministic>
	<EnableDefaultCompileItems>false</EnableDefaultCompileItems>
	<PublishAot>true</PublishAot>
	<InvariantGlobalization>false</InvariantGlobalization>
  </PropertyGroup>
)XML";
			Project << "  <ItemGroup>\n";
			for (const std::string& Unit : CompilationUnits)
			{
				Project << "\t<Compile Include=\"" << Unit << "\" />\n";
			}
			Project << "  </ItemGroup>\n</Project>\n";
			return Project.str();
		}

		void BuildBundleManifest(
		    const std::string_view ProjectName,
		    const std::vector<FCSharpAotSourceFile>& Files,
		    const std::vector<std::string>& CompilationUnits,
		    std::string& OutManifest)
		{
			std::ostringstream Manifest;
			Manifest << "{\"version\":1,\"kind\":\"veroscript.csharp-aot-source-bundle\","
			            "\"entry\":\""
			         << JsonEscape(ProjectName) << "\",\"files\":[";
			for (std::size_t Index = 0; Index < Files.size(); ++Index)
			{
				if (Index != 0)
				{
					Manifest << ',';
				}
				Manifest << '"' << JsonEscape(Files[Index].RelativePath) << '"';
			}
			Manifest << "],\"compilationUnits\":[";
			for (std::size_t Index = 0; Index < CompilationUnits.size(); ++Index)
			{
				if (Index != 0)
				{
					Manifest << ',';
				}
				Manifest << '"' << JsonEscape(CompilationUnits[Index]) << '"';
			}
			Manifest << "]}";
			OutManifest = Manifest.str();
		}

		std::unordered_set<std::string> CollectUnusedCoreImplementations(
		    const FProgram& Program, const FBytecodeModule& CheckedModule);

		class FCSharpEmitter
		{
			friend std::unordered_set<std::string> CollectUnusedCoreImplementations(
			    const FProgram& Program, const FBytecodeModule& CheckedModule);

		public:
			bool Emit()
			{
				IndexAliases();
				if (bFailed || !ValidateProgram())
				{
					return false;
				}
				IndexUserMemberEmissionNames();
				EmitHeader();
				EmitHostBindingContracts();
				if (bFailed)
				{
					return false;
				}
				RuntimeEnd = Offset();
				EmitEnums();
				EmitUserTypes();
				if (bFailed)
				{
					return false;
				}
				EmitGlobals();
				for (const FFunctionDecl& Function : Program->Functions)
				{
					if (!ShouldEmit(Function.ModulePath) || AttributeModule(Function.ModulePath) || (Function.Name == "__std_bind_method" && !Function.Body))
					{
						continue;
					}
					if (!Function.bPrecompiledPackage)
					{
						EmitFunction(Function);
					}
					if (bFailed)
					{
						return false;
					}
				}
				EmitCustomFunctionDelegates();
				if (bFailed)
				{
					return false;
				}
				ProgramEnd = Offset();
				EmitEntryPoint();
				EntryEnd = Offset();
				Out << "}\n";
				return !bFailed;
			}

			std::string RuntimeFragment() const
			{
				return Out.str().substr(0, RuntimeEnd) + "}\n";
			}

			std::string ProgramFragment() const
			{
				return FragmentHeader("program") + Out.str().substr(RuntimeEnd, ProgramEnd - RuntimeEnd) + "}\n";
			}

			std::string EntryFragment() const
			{
				return FragmentHeader("entry") + Out.str().substr(ProgramEnd, EntryEnd - ProgramEnd) + "}\n";
			}

			std::unordered_set<std::string> UnusedModules;
			const FProgram* Program = nullptr;
			const FBytecodeModule* CheckedModule = nullptr;
			std::vector<FMethodBindingCodegenInfo> MethodBindings;
			const IHostBindingCatalog* HostBindings = nullptr;
			std::ostringstream Out;
			std::string Error;
			bool bFailed = false;
			bool bNeedsLocalStatics = false;
			std::size_t RuntimeEnd = 0;
			std::size_t ProgramEnd = 0;
			std::size_t EntryEnd = 0;

		private:
			struct FCustomFunctionType
			{
				std::string FunctionType;
				std::vector<std::string> TypeParameters;
				std::string EmittedName;
			};
			struct FResolvedUserField
			{
				const FFieldDecl* Decl = nullptr;
				std::string OwnerType;
			};
			struct FResolvedUserMethod
			{
				const FMethodDecl* Decl = nullptr;
				std::string OwnerType;
			};
			struct FResolvedUserConstructor
			{
				const FConstructorDecl* Decl = nullptr;
				std::string OwnerType;
			};
			struct FResolvedCall
			{
				std::string Symbol;
				const FFunctionDecl* Decl = nullptr;
			};

			std::size_t Offset()
			{
				const std::streampos Position = Out.tellp();
				return Position == std::streampos(-1) ? 0
				                                      : static_cast<std::size_t>(static_cast<std::streamoff>(Position));
			}

			static std::string FragmentHeader(const std::string_view Fragment)
			{
				std::string Header = "// SPDX-FileCopyrightText: VanstySanic\n// SPDX-License-Identifier: MIT\n\n// <auto-generated/>\n// Generated by VeroScriptCSharpAot " VEROSCRIPT_CSHARP_AOT_VERSION " (";
				Header += Fragment;
				Header += R"CS().
#nullable enable
#pragma warning disable CS8714 // VeroScript validates container key types.
#pragma warning disable CS0108, CS0114 // VeroScript permits C++-style member hiding.
#pragma warning disable CS0162 // VeroScript may require a trailing return after a constant loop.
using System;
using System.Buffers;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Text;

internal static partial class VeroScriptGeneratedProgram
{
)CS";
				return Header;
			}

			void Fail(const std::string& Message, const FSourceLocation& Location = {})
			{
				if (bFailed)
				{
					return;
				}
				bFailed = true;
				Error = CSharpDiagnostic(Message);
				if (!Location.FileLabel.empty())
				{
					Error += " [" + std::string(Location.FileLabel) + ":" + std::to_string(Location.Line) + ":" + std::to_string(Location.Column) + "]";
				}
			}

			void IndexAliases()
			{
				if (HostBindings)
				{
					for (const FHostTypeDesc& Type : HostBindings->GetTypes())
					{
						HostTypes.emplace(CanonicalSymbol(Type.Name), &Type);
					}
				}
				for (const FTypeAliasDecl& Alias : Program->TypeAliases)
				{
					if (Alias.Scope.bLocal || !ShouldEmit(Alias.ModulePath))
					{
						continue;
					}
					if (!TypeAliases.emplace(Alias.Name, Alias.TypeName).second)
					{
						Fail("type alias '" + Alias.Name + "' appears more than once across modules; rename one",
						    Alias.Location);
						return;
					}
				}
				for (const FFunctionDecl& Function : Program->Functions)
				{
					if (!ShouldEmit(Function.ModulePath) || AttributeModule(Function.ModulePath) || (Function.Name == "__std_bind_method" && !Function.Body))
					{
						continue;
					}
					const std::string FunctionName = CanonicalSymbol(Function.Name);
					FunctionNames.insert(FunctionName);
					FunctionDecls[FunctionName].push_back(&Function);
				}
				for (const FEnumDecl& Enum : Program->Enums)
				{
					if (!ShouldEmit(Enum.ModulePath))
					{
						continue;
					}
					const std::string EnumName = CanonicalSymbol(Enum.Name);
					if (TypeAliases.find(Enum.Name) != TypeAliases.end() || !EnumTypeNames.insert(EnumName).second)
					{
						Fail("type name '" + Enum.Name + "' appears more than once across modules; rename one",
						    Enum.Location);
						return;
					}
					EnumUnderlyingTypes[EnumName] = Enum.UnderlyingTypeName;
					if (Enum.bScoped)
					{
						ScopedEnumTypes.insert(EnumName);
					}
					for (const FEnumMemberDecl& Member : Enum.Members)
					{
						std::string Emitted = SymbolIdentifier(EnumName) + "." + Identifier(Member.Name);
						if (!Enum.bScoped)
						{
							Emitted = "((" + MapType(Enum.UnderlyingTypeName) + ")" + Emitted + ")";
						}
						EmittedEnumMemberTypes[Emitted] = Enum.bScoped
						    ? EnumName
						    : Enum.UnderlyingTypeName;
						QualifiedEnumMembers.emplace(EnumName + "::" + Member.Name, Emitted);
						if (Enum.bScoped)
						{
							continue;
						}
						const std::string EnumNamespace = NamespaceOf(EnumName);
						InjectedEnumMembers.emplace(
						    EnumNamespace.empty() ? Member.Name : EnumNamespace + "::" + Member.Name,
						    Emitted);
					}
				}
				const auto IndexUserType = [&](const std::string& Name,
				                               const std::size_t Arity, const FSourceLocation& Location)
				{
					const std::string Canonical = CanonicalSymbol(Name);
					if (TypeAliases.find(Name) != TypeAliases.end() || EnumTypeNames.find(Canonical) != EnumTypeNames.end() || !UserTypeArities[Canonical].insert(Arity).second)
					{
						Fail("type name '" + Name + "' appears more than once across modules; rename one", Location);
					}
					else
					{
						UserTypeNames.insert(Canonical);
					}
				};
				for (const FStructDecl& Struct : Program->Structs)
				{
					if (!ShouldEmit(Struct.ModulePath) || AttributeModule(Struct.ModulePath))
					{
						continue;
					}
					IndexUserType(Struct.Name, Struct.TypeParams.size(), Struct.Location);
				}
				for (const FClassDecl& Class : Program->Classes)
				{
					if (!ShouldEmit(Class.ModulePath))
					{
						continue;
					}
					IndexUserType(Class.Name, Class.TypeParams.size(), Class.Location);
					if (Class.Name == "delegate")
					{
						std::string Path = Class.ModulePath;
						std::replace(Path.begin(), Path.end(), '\\', '/');
						constexpr std::string_view Suffix = "/stdlib/delegate.vs";
						bHasStandardEventDelegate = bHasStandardEventDelegate || Path == "stdlib/delegate.vs" || (Path.size() >= Suffix.size() && Path.compare(Path.size() - Suffix.size(), Suffix.size(), Suffix) == 0);
					}
				}
				if (bFailed)
				{
					return;
				}
				for (const std::unique_ptr<FStmt>& Statement : Program->TopLevelStatements)
				{
					if (!Statement || Statement->Kind != EStmtKind::VarDecl)
					{
						continue;
					}
					const auto& Variable = static_cast<const FVarDeclStmt&>(*Statement);
					Globals.push_back(&Variable);
					GlobalTypes[&Variable] = ResolveAlias(Variable.TypeName);
				}
				for (std::size_t Pass = 0; Pass < Program->TopLevelStatements.size(); ++Pass)
				{
					bool bChanged = false;
					for (const std::unique_ptr<FStmt>& Statement : Program->TopLevelStatements)
					{
						if (!Statement || Statement->Kind != EStmtKind::VarDecl)
						{
							continue;
						}
						const auto& Variable = static_cast<const FVarDeclStmt&>(*Statement);
						if (NormalizeType(GlobalTypes[&Variable]) != "auto" || !Variable.Initializer)
						{
							continue;
						}
						ActiveNamespace = NamespaceOf(Variable.Name);
						ActiveModulePath = Variable.ModulePath;
						ActiveLocation = Variable.Location;
						const std::string Inferred = ResolveAlias(InferExprType(*Variable.Initializer));
						if (Inferred.empty() || NormalizeType(Inferred) == "auto")
						{
							continue;
						}
						GlobalTypes[&Variable] = Inferred;
						bChanged = true;
					}
					if (!bChanged)
					{
						break;
					}
				}
				ActiveNamespace.clear();
				ActiveModulePath.clear();
				ActiveLocation = {};
				for (const auto& [Global, Type] : GlobalTypes)
				{
					if (NormalizeType(Type) != "auto")
					{
						continue;
					}
					Fail("could not resolve inferred type for global variable '" + Global->Name + "'", Global->Location);
					return;
				}
			}

			static std::string NamespaceOf(const std::string_view Name)
			{
				const std::string Canonical = CanonicalSymbol(std::string(Name));
				const std::size_t Separator = Canonical.rfind("::");
				return Separator == std::string::npos
				    ? std::string()
				    : Canonical.substr(0, Separator);
			}

			std::string ResolveInNamespace(
			    const std::string& Name,
			    const std::unordered_set<std::string>& Symbols) const
			{
				const std::string CanonicalName = CanonicalSymbol(Name);
				if (CanonicalName.find("::") == std::string::npos && !ActiveNamespace.empty())
				{
					std::string Scope = ActiveNamespace;
					while (!Scope.empty())
					{
						const std::string Candidate = Scope + "::" + CanonicalName;
						if (Symbols.find(Candidate) != Symbols.end())
						{
							return Candidate;
						}
						const std::size_t Separator = Scope.rfind("::");
						if (Separator == std::string::npos)
						{
							break;
						}
						Scope.erase(Separator);
					}
				}
				if (Symbols.find(CanonicalName) != Symbols.end())
				{
					return CanonicalName;
				}
				return CanonicalName;
			}

			bool IsUsingDirectiveVisible(const FUsingNamespaceDecl& Directive) const
			{
				if (Directive.ModulePath != ActiveModulePath)
				{
					return false;
				}
				const std::string Scope = CanonicalSymbol(Directive.ScopeNamespace);
				if (!Scope.empty() && ActiveNamespace != Scope && ActiveNamespace.rfind(Scope + "::", 0) != 0)
				{
					return false;
				}
				const auto LookupLocation = ResolveProgramLookupLocation(Program, ActiveLocation);
				if (!LookupLocation.FileLabel.empty() && LookupLocation.FileLabel == Directive.Location.FileLabel)
				{
					if (LookupLocation.Line < Directive.Location.Line)
					{
						return false;
					}
					if (LookupLocation.Line == Directive.Location.Line && LookupLocation.Column < Directive.Location.Column)
					{
						return false;
					}
				}
				return true;
			}

			std::vector<std::string> UsingNamespaceTargets(
			    const FUsingNamespaceDecl& Directive) const
			{
				std::vector<std::string> Targets;
				if (Directive.NamespaceName.rfind("::", 0) == 0)
				{
					Targets.push_back(CanonicalSymbol(Directive.NamespaceName.substr(2)));
					return Targets;
				}
				const std::string Written = CanonicalSymbol(Directive.NamespaceName);
				std::string Prefix = CanonicalSymbol(Directive.ScopeNamespace);
				while (!Prefix.empty())
				{
					Targets.push_back(Prefix + "::" + Written);
					const std::size_t Separator = Prefix.rfind("::");
					Prefix = Separator == std::string::npos
					    ? std::string()
					    : Prefix.substr(0, Separator);
				}
				Targets.push_back(Written);
				return Targets;
			}

			std::vector<std::string> ResolveUsingSymbols(
			    const std::string& BareName,
			    const std::unordered_set<std::string>& Symbols) const
			{
				std::vector<std::string> Result;
				if (BareName.find("::") != std::string::npos)
				{
					return Result;
				}
				for (const FUsingNamespaceDecl& Directive : Program->UsingNamespaces)
				{
					if (!ShouldEmit(Directive.ModulePath))
					{
						continue;
					}
					if (!IsUsingDirectiveVisible(Directive))
					{
						continue;
					}
					for (const std::string& Target : UsingNamespaceTargets(Directive))
					{
						const std::string Candidate = Target + "::" + BareName;
						if (Symbols.find(Candidate) == Symbols.end())
						{
							continue;
						}
						if (std::find(Result.begin(), Result.end(), Candidate) == Result.end())
						{
							Result.push_back(Candidate);
						}
						break;
					}
				}
				return Result;
			}

			bool TryFlattenQualifiedName(const FExpr& Expr, std::string& OutName) const
			{
				if (Expr.Kind == EExprKind::Identifier)
				{
					OutName = static_cast<const FIdentifierExpr&>(Expr).Name;
					return true;
				}
				if (Expr.Kind != EExprKind::MemberAccess)
				{
					return false;
				}
				const auto& Member = static_cast<const FMemberAccessExpr&>(Expr);
				if (!TryFlattenQualifiedName(*Member.Object, OutName))
				{
					return false;
				}
				OutName += "::" + Member.Member;
				return true;
			}

			bool IsSideEffectFree(const FExpr& Expr) const
			{
				switch (Expr.Kind)
				{
				case EExprKind::IntegerLiteral:
				case EExprKind::CharacterLiteral:
				case EExprKind::FloatLiteral:
				case EExprKind::StringLiteral:
				case EExprKind::WStringLiteral:
				case EExprKind::BooleanLiteral:
				case EExprKind::NullptrLiteral:
				case EExprKind::Identifier:
				case EExprKind::This:
					return true;
				case EExprKind::Unary:
					return IsSideEffectFree(*static_cast<const FUnaryExpr&>(Expr).Operand);
				case EExprKind::Binary:
					{
						const auto& Binary = static_cast<const FBinaryExpr&>(Expr);
						return IsSideEffectFree(*Binary.Left) && IsSideEffectFree(*Binary.Right);
					}
				case EExprKind::MemberAccess:
					return IsSideEffectFree(*static_cast<const FMemberAccessExpr&>(Expr).Object);
				case EExprKind::IndexAccess:
					{
						const auto& Index = static_cast<const FIndexAccessExpr&>(Expr);
						return IsSideEffectFree(*Index.Object) && IsSideEffectFree(*Index.Index);
					}
				case EExprKind::Conditional:
					{
						const auto& Conditional = static_cast<const FConditionalExpr&>(Expr);
						return IsSideEffectFree(*Conditional.Condition) && IsSideEffectFree(*Conditional.ThenExpr) && IsSideEffectFree(*Conditional.ElseExpr);
					}
				default:
					return false;
				}
			}

			bool IsRepeatableAssignmentTarget(const FExpr& Expr) const
			{
				if (Expr.Kind == EExprKind::Identifier)
				{
					return true;
				}
				if (Expr.Kind == EExprKind::MemberAccess)
				{
					return IsSideEffectFree(*static_cast<const FMemberAccessExpr&>(Expr).Object);
				}
				if (Expr.Kind == EExprKind::IndexAccess)
				{
					const auto& Index = static_cast<const FIndexAccessExpr&>(Expr);
					return IsSideEffectFree(*Index.Object) && IsSideEffectFree(*Index.Index);
				}
				return false;
			}

			bool IsCSharpWritableRefTarget(const FExpr& Expr) const
			{
				if (Expr.Kind == EExprKind::Identifier)
				{
					return true;
				}
				if (Expr.Kind != EExprKind::MemberAccess)
				{
					return false;
				}
				std::string Qualified;
				return TryFlattenQualifiedName(Expr, Qualified) && ResolveGlobal(Qualified) != nullptr;
			}

			bool IsKnownConstRefTarget(const FExpr& Expr) const
			{
				if (Expr.Kind == EExprKind::Identifier)
				{
					const std::string& Name = static_cast<const FIdentifierExpr&>(Expr).Name;
					if (LocalTypes.find(Name) != LocalTypes.end())
					{
						return LocalConstNames.find(Name) != LocalConstNames.end();
					}
					if (const FVarDeclStmt* Global = ResolveGlobal(Name))
					{
						return Global->bIsConst;
					}
					return false;
				}
				if (Expr.Kind == EExprKind::MemberAccess)
				{
					std::string Qualified;
					if (TryFlattenQualifiedName(Expr, Qualified))
					{
						if (const FVarDeclStmt* Global = ResolveGlobal(Qualified))
						{
							return Global->bIsConst;
						}
					}
					return false;
				}
				if (Expr.Kind == EExprKind::IndexAccess)
				{
					return IsKnownConstRefTarget(*static_cast<const FIndexAccessExpr&>(Expr).Object);
				}
				return false;
			}

			bool CanBindImplicitRefReceiver(const FExpr& Expr) const
			{
				if (Expr.Kind == EExprKind::Identifier)
				{
					const std::string& Name = static_cast<const FIdentifierExpr&>(Expr).Name;
					return LocalTypes.find(Name) != LocalTypes.end() || ResolveGlobal(Name) != nullptr;
				}
				if (Expr.Kind == EExprKind::IndexAccess)
				{
					const auto& Index = static_cast<const FIndexAccessExpr&>(Expr);
					return Index.Object && CanBindImplicitRefReceiver(*Index.Object);
				}
				if (Expr.Kind != EExprKind::MemberAccess)
				{
					return false;
				}
				std::string Qualified;
				return TryFlattenQualifiedName(Expr, Qualified) && ResolveGlobal(Qualified) != nullptr;
			}

			std::string EmitScriptArgument(
			    const FExpr& Argument,
			    const FFunctionParam* const Parameter,
			    const FSourceLocation& Location,
			    const std::string& ResolvedType = {})
			{
				const std::string ExpectedType = !ResolvedType.empty()
				    ? ResolvedType
				    : (Parameter ? Parameter->TypeName : std::string{});
				if (!Parameter || !Parameter->bIsRef)
				{
					return EmitExpr(Argument, ExpectedType);
				}
				if (Argument.Kind != EExprKind::Ref)
				{
					Fail("reference parameter requires an explicit ref argument", Location);
					return "default!";
				}
				const FExpr& Operand = *static_cast<const FRefExpr&>(Argument).Operand;
				if (Parameter->bIsConst)
				{
					return EmitExpr(Operand, ExpectedType);
				}
				if (!IsCSharpWritableRefTarget(Operand))
				{
					Fail("writable ref arguments in C# AOT currently require a local or global variable",
					    Location);
					return "default!";
				}
				return "ref " + EmitExpr(Operand, ExpectedType);
			}

			std::string EmitHostArgument(
			    const FExpr& Argument,
			    const std::uint8_t Flag,
			    const FSourceLocation& Location)
			{
				const bool bArgumentIsRef = Argument.Kind == EExprKind::Ref;
				const bool bParameterIsRef = NativeParamFlagIsRef(Flag);
				if (bArgumentIsRef && !bParameterIsRef)
				{
					Fail("host parameter does not accept a ref argument", Location);
					return "default!";
				}
				const FExpr& Operand = bArgumentIsRef
				    ? *static_cast<const FRefExpr&>(Argument).Operand
				    : Argument;
				const bool bWritableRef = bParameterIsRef && (Flag & NativeParamFlagConstRef) == 0;
				if (bWritableRef)
				{
					if (!bArgumentIsRef)
					{
						Fail("writable host ref parameter requires an explicit ref argument", Location);
						return "default!";
					}
					if (!IsCSharpWritableRefTarget(Operand))
					{
						Fail("writable ref arguments in C# AOT currently require a local or global variable",
						    Location);
						return "default!";
					}
					return "ref " + EmitExpr(Operand);
				}
				return EmitExpr(Operand);
			}

			std::string ResolveAlias(std::string Type) const
			{
				std::unordered_set<std::string> Seen;
				while (Seen.insert(Type).second)
				{
					const auto Found = TypeAliases.find(Type);
					if (Found == TypeAliases.end())
					{
						break;
					}
					Type = Found->second;
				}
				return Type;
			}

			static std::string NormalizeScalarType(std::string Type)
			{
				Type = Trim(std::move(Type));
				if (Type.compare(0, 6, "const ") == 0)
				{
					Type = Trim(Type.substr(6));
				}
				if (Type == "int8" || Type == "char")
				{
					return "char";
				}
				if (Type == "uint8" || Type == "unsigned char")
				{
					return "unsigned char";
				}
				if (Type == "int16" || Type == "short")
				{
					return "short";
				}
				if (Type == "uint16" || Type == "unsigned short")
				{
					return "unsigned short";
				}
				if (Type == "int32" || Type == "int")
				{
					return "int";
				}
				if (Type == "uint32" || Type == "unsigned int")
				{
					return "unsigned int";
				}
				if (Type == "int64" || Type == "long")
				{
					return "int64";
				}
				if (Type == "uint64" || Type == "unsigned long")
				{
					return "uint64";
				}
				return Type;
			}

			std::string NormalizeType(std::string Type) const
			{
				Type = Trim(ResolveAlias(Trim(std::move(Type))));
				if (Runtime::IsReferenceTypeName(Type))
				{
					Type = std::string(Runtime::StripReferenceTypeName(Type));
				}
				return NormalizeScalarType(std::move(Type));
			}

			static bool IsReferenceTypeSpelling(std::string Type)
			{
				Type = Trim(std::move(Type));
				return (!Type.empty() && Type.back() == '&') || Type.compare(0, 4, "ref ") == 0 || Type.compare(0, 10, "const ref ") == 0;
			}

			static bool IsSignedIntegerType(const std::string& Type)
			{
				const std::string Normalized = NormalizeScalarType(Type);
				return Normalized == "char" || Normalized == "short" || Normalized == "int" || Normalized == "int64";
			}

			static bool IsUnsignedIntegerType(const std::string& Type)
			{
				const std::string Normalized = NormalizeScalarType(Type);
				return Normalized == "unsigned char" || Normalized == "unsigned short" || Normalized == "unsigned int" || Normalized == "uint64";
			}

			static bool IsNumericType(const std::string& Type)
			{
				const std::string Normalized = NormalizeScalarType(Type);
				return IsSignedIntegerType(Normalized) || IsUnsignedIntegerType(Normalized) || Normalized == "float" || Normalized == "double";
			}

			static std::string HostMethodName(const std::string_view Identity)
			{
				static constexpr char Hex[] = "0123456789ABCDEF";
				const std::string Full = "member:" + std::string(Identity);
				std::string Result = "VsHost_";
				Result.reserve(Result.size() + Full.size() * 2);
				for (const unsigned char Byte : Full)
				{
					Result.push_back(Hex[Byte >> 4]);
					Result.push_back(Hex[Byte & 0x0F]);
				}
				return Result;
			}

			static std::string HostMemberIdentity(
			    const FHostTypeDesc& Owner,
			    const FHostMemberDesc& Member)
			{
				if (!Member.NativeName.empty())
				{
					return Member.NativeName;
				}
				std::ostringstream Identity;
				Identity << Owner.Name << "::" << static_cast<unsigned int>(Member.Kind)
				         << "::" << Member.Name << '(';
				for (std::size_t Index = 0; Index < Member.ParamTypes.size(); ++Index)
				{
					if (Index != 0)
					{
						Identity << ',';
					}
					Identity << static_cast<unsigned int>(Member.ParamTypes[Index]);
					if (Index < Member.ParamHostTypeNames.size())
					{
						Identity << ':' << Member.ParamHostTypeNames[Index];
					}
				}
				Identity << ')';
				return Identity.str();
			}

			const FHostTypeDesc* ResolveHostType(std::string Name) const
			{
				Name = CanonicalSymbol(std::move(Name));
				if (const auto Found = HostTypes.find(Name); Found != HostTypes.end())
				{
					return Found->second;
				}
				if (HostBindings)
				{
					std::string RuntimeName;
					std::string AliasError;
					if (ResolveHostRuntimeTypeName(*HostBindings, Name, RuntimeName, AliasError))
					{
						RuntimeName = CanonicalSymbol(std::move(RuntimeName));
						if (const auto Found = HostTypes.find(RuntimeName); Found != HostTypes.end())
						{
							return Found->second;
						}
					}
				}
				return nullptr;
			}

			std::string NativeScriptType(
			    const ENativeType Type,
			    const std::string& HostName,
			    const std::string& ElementName = {},
			    const std::string& KeyName = {},
			    const std::string& ValueName = {}) const
			{
				switch (Type)
				{
				case ENativeType::Void:
					return "void";
				case ENativeType::Bool:
					return "bool";
				case ENativeType::Int8:
					return "char";
				case ENativeType::UInt8:
					return "unsigned char";
				case ENativeType::Int16:
					return "short";
				case ENativeType::UInt16:
					return "unsigned short";
				case ENativeType::Int:
					return "int";
				case ENativeType::Unsigned:
					return "unsigned int";
				case ENativeType::Int64:
					return "int64";
				case ENativeType::UInt64:
					return "uint64";
				case ENativeType::Float:
					return "float";
				case ENativeType::Double:
					return "double";
				case ENativeType::String:
					return "string";
				case ENativeType::HostObject:
				case ENativeType::Enum:
					return HostName;
				case ENativeType::Array:
					return "array<" + ElementName + ">";
				case ENativeType::Deque:
					return "deque<" + ElementName + ">";
				case ENativeType::Set:
					return "set<" + ElementName + ">";
				case ENativeType::Queue:
					return "queue<" + ElementName + ">";
				case ENativeType::Stack:
					return "stack<" + ElementName + ">";
				case ENativeType::Map:
					return "map<" + KeyName + ',' + ValueName + ">";
				case ENativeType::OrderedMap:
					return "ordered_map<" + KeyName + ',' + ValueName + ">";
				default:
					return {};
				}
			}

			std::string HostReturnScriptType(const FHostFunctionDesc& Function) const
			{
				return NativeScriptType(Function.ReturnType, Function.ReturnHostTypeName,
				    Function.ReturnElementTypeName, Function.ReturnKeyTypeName,
				    Function.ReturnValueTypeName);
			}

			std::string HostReturnScriptType(
			    const FHostTypeDesc& Owner,
			    const FHostMemberDesc& Member) const
			{
				std::string HostName = Member.ReturnHostTypeName;
				if (HostName.empty() && Member.ReturnType == ENativeType::HostObject)
				{
					HostName = Owner.Name;
				}
				return NativeScriptType(Member.ReturnType, HostName,
				    Member.ReturnElementTypeName, Member.ReturnKeyTypeName,
				    Member.ReturnValueTypeName);
			}

			std::string NativeContractType(
			    const ENativeType Type,
			    const std::string& HostName,
			    const std::string& ElementName = {},
			    const std::string& KeyName = {},
			    const std::string& ValueName = {})
			{
				const std::string ScriptType = NativeScriptType(
				    Type, HostName, ElementName, KeyName, ValueName);
				if (ScriptType.empty())
				{
					Fail("host binding native type '" + std::to_string(static_cast<unsigned int>(Type)) + "' is not supported by C# AOT");
					return "object";
				}
				return MapType(ScriptType);
			}

			bool TypeReferencesTypeParameter(
			    std::string Type,
			    const std::string& TypeParameter) const
			{
				Type = Trim(std::move(Type));
				if (Runtime::IsReferenceTypeName(Type))
				{
					return TypeReferencesTypeParameter(
					    std::string(Runtime::StripReferenceTypeName(Type)), TypeParameter);
				}
				std::vector<std::string> FunctionParams;
				std::string FunctionReturn;
				if (Runtime::ParseFunctionType(Type, FunctionParams, FunctionReturn))
				{
					if (TypeReferencesTypeParameter(FunctionReturn, TypeParameter))
					{
						return true;
					}
					return std::any_of(FunctionParams.begin(), FunctionParams.end(),
					    [&](const std::string& Parameter)
					{
						return TypeReferencesTypeParameter(Parameter, TypeParameter);
					});
				}
				if (Type.compare(0, 6, "const ") == 0)
				{
					Type = Trim(Type.substr(6));
				}
				while (!Type.empty() && (Type.back() == '&' || Type.back() == '*'))
				{
					Type = Trim(Type.substr(0, Type.size() - 1));
				}
				if (Type.size() > 2 && Type.compare(Type.size() - 2, 2, "[]") == 0)
				{
					return TypeReferencesTypeParameter(
					    Type.substr(0, Type.size() - 2), TypeParameter);
				}
				const std::size_t Open = Type.find('<');
				if (Open != std::string::npos && Type.back() == '>')
				{
					const std::vector<std::string> Arguments = SplitTypeArguments(
					    std::string_view(Type).substr(Open + 1, Type.size() - Open - 2));
					return std::any_of(Arguments.begin(), Arguments.end(),
					    [&](const std::string& Argument)
					{
						return TypeReferencesTypeParameter(Argument, TypeParameter);
					});
				}
				return NormalizeScalarType(std::move(Type)) == TypeParameter;
			}

			std::string CanonicalFunctionComponent(std::string Type, const std::size_t Depth = 0)
			{
				if (Depth > 256)
				{
					Fail("function type nesting exceeds the C# AOT limit");
					return {};
				}
				Type = Trim(ResolveAlias(Trim(std::move(Type))));
				if (Runtime::IsReferenceTypeName(Type))
				{
					const std::string Prefix = Runtime::IsConstReferenceTypeName(Type) ? "const ref " : "ref ";
					return Prefix + CanonicalFunctionComponent(std::string(Runtime::StripReferenceTypeName(Type)), Depth + 1);
				}
				if (Type.compare(0, 6, "const ") == 0)
				{
					return "const " + CanonicalFunctionComponent(Type.substr(6), Depth + 1);
				}
				std::vector<std::string> Parameters;
				std::string ReturnType;
				if (Runtime::ParseFunctionType(Type, Parameters, ReturnType))
				{
					for (std::string& Parameter : Parameters)
					{
						Parameter = CanonicalFunctionComponent(std::move(Parameter), Depth + 1);
					}
					return Runtime::MakeFunctionTypeName(Parameters,
					    CanonicalFunctionComponent(std::move(ReturnType), Depth + 1));
				}
				if (Type.size() > 2 && Type.compare(Type.size() - 2, 2, "[]") == 0)
				{
					return "array<" + CanonicalFunctionComponent(Type.substr(0, Type.size() - 2), Depth + 1) + ">";
				}
				std::string Base;
				std::vector<std::string> Arguments;
				if (SplitGenericType(Type, Base, Arguments) && (Base == "array" || Base == "queue" || Base == "priority_queue" || Base == "stack" || Base == "deque" || Base == "set" || Base == "multiset" || Base == "ordered_multiset" || Base == "map" || Base == "multimap" || Base == "ordered_map" || Base == "ordered_multimap" || (Base == "pair" && Arguments.size() == 2 && !HasUserPairTemplate())))
				{
					std::string Result = Base + '<';
					for (std::size_t Index = 0; Index < Arguments.size(); ++Index)
					{
						if (Index != 0)
						{
							Result += ',';
						}
						Result += CanonicalFunctionComponent(std::move(Arguments[Index]), Depth + 1);
					}
					return Result + '>';
				}
				// Canonicalize value aliases, retaining nominal object identities and
				// reference qualifiers when sharing a generated CLR delegate type.
				return NormalizeScalarType(std::move(Type));
			}

			static std::string CustomFunctionTypeKey(
			    const std::string& FunctionType,
			    const std::vector<std::string>& TypeParameters)
			{
				std::string Key = std::to_string(FunctionType.size()) + ":" + FunctionType;
				for (const std::string& Parameter : TypeParameters)
				{
					Key += std::to_string(Parameter.size()) + ":" + Parameter;
				}
				return Key;
			}

			bool HasUserPairTemplate() const
			{
				const auto HasPair = [](const auto& Declarations)
				{
					return std::any_of(Declarations.begin(), Declarations.end(),
					    [](const auto& Decl)
					{
						return Decl.Name == "pair" && Decl.TypeParams.size() == 2;
					});
				};
				return HasPair(Program->Structs) || HasPair(Program->Classes);
			}

			bool IsBuiltinHandleType(const std::string& Type, std::string& OutPayload) const
			{
				std::string Base;
				std::vector<std::string> Args;
				if (!SplitGenericType(ResolveAlias(Type), Base, Args) || Base != "handle" || Args.size() != 1)
				{
					return false;
				}
				for (const auto& Decl : Program->Classes)
				{
					if (!ShouldEmit(Decl.ModulePath))
					{
						continue;
					}
					if (Decl.Name == "handle" && Decl.TypeParams.size() == 1) return false;
				}
				for (const auto& Decl : Program->Structs)
				{
					if (!ShouldEmit(Decl.ModulePath))
					{
						continue;
					}
					if (Decl.Name == "handle" && Decl.TypeParams.size() == 1) return false;
				}
				OutPayload = Args.front();
				return true;
			}

			bool HasNonGenericHandleType() const
			{
				for (const auto& Decl : Program->Classes)
				{
					if (!ShouldEmit(Decl.ModulePath))
					{
						continue;
					}
					if (Decl.Name == "handle" && Decl.TypeParams.empty() && IsVisibleFromActiveModule(Decl.ModulePath, Decl.bExported))
					{
						return true;
					}
				}
				for (const auto& Decl : Program->Structs)
				{
					if (!ShouldEmit(Decl.ModulePath))
					{
						continue;
					}
					if (Decl.Name == "handle" && Decl.TypeParams.empty() && IsVisibleFromActiveModule(Decl.ModulePath, Decl.bExported))
					{
						return true;
					}
				}
				return false;
			}

			bool AttributeModule(const std::string& Path) const
			{
				return StandardLibrary::IsAttributeImplementationModule(*Program, Path);
			}

			bool HasStandardAttribute() const
			{
				for (const auto& D : Program->Structs)
				{
					if (D.Name == "attribute" && AttributeModule(D.ModulePath)) return true;
				}

				return false;
			}

			bool IsMethodBindingFactoryCall(const FCallExpr& Call) const
			{
				if (Call.TypeArgs.empty()) return false;
				if (FindMethodBindingCodegenInfo(MethodBindings, Call.Location)) return true;
				return std::any_of(Program->Functions.begin(), Program->Functions.end(),
				    [&](const auto& D)
				{
					return D.Name == Call.Callee && StandardLibrary::IsMethodBindingFactoryDeclaration(D);
				});
			}

			bool IsWeakAttributeFactoryCall(const FCallExpr& Call) const
			{
				return Call.Callee == "__std_make_weak_attribute" && !Call.TypeArgs.empty() &&
				    std::any_of(Program->Functions.begin(), Program->Functions.end(),
				        [&](const auto& D)
				{
					return D.Name == Call.Callee && AttributeModule(D.ModulePath);
				});
			}
			std::string MapType(std::string Type)
			{
				Type = Trim(ResolveAlias(Trim(std::move(Type))));
				if (HostBindings)
				{
					std::string RuntimeType;
					std::string AliasError;
					if (!ResolveHostRuntimeTypeName(*HostBindings, Type, RuntimeType, AliasError))
					{
						Fail("invalid host type alias '" + Type + "': " + AliasError);
						return "object";
					}
					Type = std::move(RuntimeType);
				}
				if (Runtime::IsReferenceTypeName(Type))
				{
					Type = std::string(Runtime::StripReferenceTypeName(Type));
				}
				std::vector<std::string> FunctionParams;
				std::string FunctionReturn;
				if (Runtime::ParseFunctionType(Type, FunctionParams, FunctionReturn))
				{
					Type = CanonicalFunctionComponent(std::move(Type));
					Runtime::ParseFunctionType(Type, FunctionParams, FunctionReturn);
					bool bNeedsCustomDelegate = FunctionParams.size() > 16;
					for (const std::string& Parameter : FunctionParams)
					{
						bNeedsCustomDelegate = bNeedsCustomDelegate || Runtime::IsReferenceTypeName(Parameter);
					}
					bNeedsCustomDelegate = bNeedsCustomDelegate || Runtime::IsReferenceTypeName(FunctionReturn);
					if (bNeedsCustomDelegate)
					{
						std::vector<std::string> TypeParameters;
						for (const auto& [SourceName, EmittedName] : ActiveTypeParameterEmissionNames)
						{
							(void)EmittedName;
							if (TypeReferencesTypeParameter(Type, SourceName))
							{
								TypeParameters.push_back(SourceName);
							}
						}
						std::sort(TypeParameters.begin(), TypeParameters.end());
						const std::string Key = CustomFunctionTypeKey(Type, TypeParameters);
						std::size_t TypeIndex;
						if (const auto Found = IndexedCustomFunctionTypes.find(Key);
						    Found != IndexedCustomFunctionTypes.end())
						{
							TypeIndex = Found->second;
						}
						else
						{
							TypeIndex = CustomFunctionTypes.size();
							std::string EmittedName = "VsFn_" + SymbolIdentifier(Type);
							if (!TypeParameters.empty())
							{
								EmittedName += "_G";
								for (const std::string& Parameter : TypeParameters)
								{
									EmittedName += "S" + std::to_string(Parameter.size()) + "_" + EncodeIdentifierBytes(Parameter);
								}
							}
							CustomFunctionTypes.push_back(
							    {Type, TypeParameters, std::move(EmittedName)});
							IndexedCustomFunctionTypes.emplace(Key, TypeIndex);
						}
						const FCustomFunctionType& Custom = CustomFunctionTypes[TypeIndex];
						std::string Result = Custom.EmittedName;
						if (!Custom.TypeParameters.empty())
						{
							Result += '<';
							for (std::size_t Index = 0;
							    Index < Custom.TypeParameters.size(); ++Index)
							{
								if (Index != 0)
								{
									Result += ", ";
								}
								Result += ActiveTypeParameterEmissionNames.at(
								    Custom.TypeParameters[Index]);
							}
							Result += '>';
						}
						return Result;
					}
					const bool bVoid = NormalizeType(FunctionReturn) == "void";
					if (bVoid && FunctionParams.empty())
					{
						return "Action";
					}
					std::ostringstream Result;
					Result << (bVoid ? "Action<" : "Func<");
					for (std::size_t Index = 0; Index < FunctionParams.size(); ++Index)
					{
						if (Index != 0)
						{
							Result << ", ";
						}
						Result << MapType(FunctionParams[Index]);
					}
					if (!bVoid)
					{
						if (!FunctionParams.empty())
						{
							Result << ", ";
						}
						Result << MapType(FunctionReturn);
					}
					Result << '>';
					return Result.str();
				}
				if (Type.compare(0, 6, "const ") == 0)
				{
					Type = Trim(Type.substr(6));
				}
				while (!Type.empty() && (Type.back() == '&' || Type.back() == '*'))
				{
					Type = Trim(Type.substr(0, Type.size() - 1));
				}
				const auto TypeParameter = ActiveTypeParameterEmissionNames.find(NormalizeType(Type));
				if (TypeParameter != ActiveTypeParameterEmissionNames.end())
				{
					return TypeParameter->second;
				}
				if (Type.size() > 2 && Type.compare(Type.size() - 2, 2, "[]") == 0)
				{
					return "List<" + MapType(Type.substr(0, Type.size() - 2)) + ">";
				}
				if (!Type.empty() && Type.back() == ']')
				{
					const std::size_t Open = Type.rfind('[');
					if (Open != std::string::npos && Open + 1 < Type.size() - 1 && std::all_of(Type.begin() + static_cast<std::ptrdiff_t>(Open + 1), Type.end() - 1, [](const unsigned char Character)
					{
						return std::isdigit(Character) != 0;
					}))
					{
						Fail("fixed-size native arrays are not implemented");
						return "object";
					}
				}

				static const std::unordered_map<std::string, std::string> Scalars = {
				    {"void", "void"}, {"bool", "bool"}, {"int8", "sbyte"}, {"char", "sbyte"},
				    {"uint8", "byte"}, {"unsigned char", "byte"},
				    {"int16", "short"}, {"short", "short"},
				    {"uint16", "ushort"}, {"unsigned short", "ushort"},
				    {"int", "int"}, {"int32", "int"}, {"uint32", "uint"},
				    {"unsigned int", "uint"}, {"int64", "long"}, {"long", "long"},
				    {"uint64", "ulong"}, {"unsigned long", "ulong"},
				    {"float", "float"}, {"double", "double"}, {"string", "VsString"},
				    {"wstring", "string"}, {"auto", "var"}};
				if (const auto Found = Scalars.find(Type); Found != Scalars.end())
				{
					return Found->second;
				}
				if (const FHostTypeDesc* HostType = ResolveHostType(Type))
				{
					if (HostType->Kind == EHostTypeKind::Enum)
					{
						return MapType(HostType->EnumUnderlyingTypeName);
					}
					return "object";
				}
				std::string EnumType = ResolveInNamespace(Type, EnumTypeNames);
				if (EnumTypeNames.find(EnumType) == EnumTypeNames.end() && Type.find("::") == std::string::npos)
				{
					const std::vector<std::string> Imported = ResolveUsingSymbols(Type, EnumTypeNames);
					if (Imported.size() == 1)
					{
						EnumType = Imported.front();
					}
				}
				if (EnumTypeNames.find(EnumType) != EnumTypeNames.end())
				{
					if (ScopedEnumTypes.find(EnumType) != ScopedEnumTypes.end())
					{
						return SymbolIdentifier(EnumType);
					}
					return MapType(EnumUnderlyingTypes.at(EnumType));
				}
				if (const std::string UserType = ResolveUserTypeSymbol(Type);
				    !UserType.empty() && Type.find('<') == std::string::npos)
				{
					return SymbolIdentifier(UserType);
				}

				std::string HandlePayload;
				if (IsBuiltinHandleType(Type, HandlePayload))
				{
					return "VsShared<" + MapType(HandlePayload) + ">";
				}
				const std::size_t Open = Type.find('<');
				if (Open != std::string::npos && Type.back() == '>')
				{
					const std::string Base = Trim(Type.substr(0, Open));
					if (Base == "field_info" || Base == "method_info")
					{
						Fail("reflection descriptors are not supported by C# AOT; "
						     "use VeroScriptCSharp with the VM backend");
						return "object";
					}
					if (Base == "channel")
					{
						Fail("channel and task lowering is not implemented in C# AOT; "
						     "use VeroScriptCSharp with the VM backend");
						return "object";
					}
					const std::vector<std::string> Args = SplitTypeArguments(
					    std::string_view(Type).substr(Open + 1, Type.size() - Open - 2));
					if (Base == "delegate" && bHasStandardEventDelegate && ResolveUserTypeSymbol(Base) == "delegate" && !Args.empty() && NormalizeType(ResolveAlias(Args.front())) == "void")
					{
						std::string Result = "VsVoidDelegate" + std::to_string(Args.size() - 1);
						if (Args.size() > 1)
						{
							Result += '<';
							for (std::size_t Index = 1; Index < Args.size(); ++Index)
							{
								if (Index != 1)
								{
									Result += ", ";
								}
								Result += MapType(Args[Index]);
							}
							Result += '>';
						}
						return Result;
					}
					std::vector<std::string> Mapped;
					for (const std::string& Arg : Args)
					{
						Mapped.push_back(MapType(Arg));
					}
					std::string Target;
					if (Base == "attribute" && HasStandardAttribute())
					{
						Target = "VsAttribute";
					}

					else if (Base == "optional")
					{
						Target = "VsOptional";
					}

					else if (Base == "array" || Base == "deque")
					{
						Target = "List";
					}
					else if (Base == "map")
					{
						Target = "Dictionary";
					}
					else if (Base == "ordered_map")
					{
						Target = "VsOrderedMap";
					}
					else if (Base == "multimap")
					{
						Target = "VsMultiMap";
					}
					else if (Base == "ordered_multimap")
					{
						Target = "VsOrderedMultiMap";
					}
					else if (Base == "set")
					{
						Target = "HashSet";
					}
					else if (Base == "multiset")
					{
						Target = "VsMultiSet";
					}
					else if (Base == "ordered_multiset")
					{
						Target = "VsOrderedMultiSet";
					}
					else if (Base == "queue")
					{
						Target = "Queue";
					}
					else if (Base == "priority_queue")
					{
						Target = "VsPriorityQueue";
					}
					else if (Base == "stack")
					{
						Target = "Stack";
					}
					else if (Base == "pair" && Args.size() == 2 && !HasUserPairTemplate())
					{
						Target = "VsPair";
					}
					else if (const std::string UserType = ResolveUserTypeSymbol(Base);
					    !UserType.empty())
					{
						Target = SymbolIdentifier(UserType);
					}
					else
					{
						Target = Identifier(Base);
					}
					std::ostringstream Result;
					Result << Target << '<';
					for (std::size_t Index = 0; Index < Mapped.size(); ++Index)
					{
						if (Index != 0)
						{
							Result << ", ";
						}
						Result << Mapped[Index];
					}
					Result << '>';
					return Result.str();
				}
				return Identifier(Type);
			}

			bool ShouldEmit(const std::string& ModulePath) const
			{
				return !UnusedModules.count(ModulePath);
			}

			bool ValidateProgram()
			{
				const auto IsOperatorName = [](std::string_view Name)
				{
					const std::size_t Separator = Name.find_last_of(":.");
					Name.remove_prefix(Separator == std::string_view::npos ? 0 : Separator + 1);
					return Name.rfind("operator", 0) == 0 && Name.size() > 8 && std::string_view(" +-*/%&|^!~<>=([,").find(Name[8]) != std::string_view::npos;
				};
				const bool bDeclaresConcurrencyTask = std::any_of(
				    Program->Classes.begin(), Program->Classes.end(),
				    [&](const FClassDecl& Class)
				{
					return ShouldEmit(Class.ModulePath) && (Class.Name == "task" || Class.Name == "task_group");
				});
				const bool bDeclaresConcurrencyHelpers = std::any_of(
				    Program->Functions.begin(), Program->Functions.end(),
				    [&](const FFunctionDecl& Function)
				{
					return ShouldEmit(Function.ModulePath) && (Function.Name == "__std_spawn_task" || Function.Name == "__std_make_channel");
				});
				if (bDeclaresConcurrencyTask && bDeclaresConcurrencyHelpers)
				{
					Fail("concurrent.vs channel and task lowering is not implemented in C# AOT; "
					     "use VeroScriptCSharp with the VM backend");
					return false;
				}
				if (std::any_of(Program->Interfaces.begin(), Program->Interfaces.end(),
				        [&](const FInterfaceDecl& Interface)
				{
					return ShouldEmit(Interface.ModulePath);
				}))
				{
					Fail("interface lowering requires a representation for interface state and default dispatch; "
					     "use classes or structs for C# AOT");
					return false;
				}
				const auto ValidateTypeMembers = [&](const auto& Decl)
				{
					if (!Decl.Bounds.empty())
					{
						Fail("generic type constraints are not implemented yet", Decl.Location);
					}
					if (std::any_of(Decl.TypeParamDefaults.begin(), Decl.TypeParamDefaults.end(),
					        [](const std::string& Value)
					{
						return !Value.empty();
					}))
					{
						Fail("default generic type arguments are not representable in C#", Decl.Location);
					}
					if (!Decl.InterfaceTypeNames.empty())
					{
						Fail("implemented interfaces are not lowered until interface dispatch is available",
						    Decl.Location);
					}
					if (!Decl.UsingMembers.empty() || !Decl.Friends.empty())
					{
						Fail("using-member and friend access declarations are not representable in C#",
						    Decl.Location);
					}
					if (Decl.Destructor && !Decl.Destructor->bDefaulted)
					{
						Fail("deterministic script destructors cannot be represented by C# finalizers",
						    Decl.Destructor->Location);
					}
					std::unordered_set<std::string> MethodSignatures;
					for (const FMethodDecl& Method : Decl.Methods)
					{
						if (!Method.bDeleted && IsOperatorName(Method.Name) && (Method.Name == "operator=" || Runtime::IsReferenceTypeName(Method.ReturnType) || ((Method.Name == "operator++" || Method.Name == "operator--") && Method.Parameters.size() == 1) || std::any_of(Method.Parameters.begin(), Method.Parameters.end(), [](const FFunctionParam& Param)
						{
							return Param.bIsRef || Param.bIsVariadic || Param.DefaultValue;
						})))
						{
							Fail("reference operators, operator=, postfix int tags and operator() defaults/rest "
							     "are not supported by C# source emission; use the VM, mixed or JIT backend",
							    Method.Location);
						}
						if (!Method.bDeleted && !Method.bPureVirtual && !Method.Body)
						{
							Fail("declaration-only methods cannot be emitted", Method.Location);
						}
						if (Method.bDeleted)
						{
							continue;
						}
						std::ostringstream Signature;
						Signature << Method.Name << '#' << Method.Parameters.size();
						for (const FFunctionParam& Parameter : Method.Parameters)
						{
							Signature << '#' << NormalizeType(Parameter.TypeName)
							          << (Parameter.bIsRef ? "#byref" : "#value")
							          << (Parameter.bIsVariadic ? "#params" : "#fixed");
						}
						if (!MethodSignatures.insert(Signature.str()).second)
						{
							Fail("method overloads that differ only by const/ref direction or static shape "
							     "cannot share one C# member name",
							    Method.Location);
						}
					}
					std::unordered_set<std::string> ConstructorSignatures;
					for (const FConstructorDecl& Constructor : Decl.Constructors)
					{
						if (!Constructor.bDeleted && !Constructor.bDefaulted && !Constructor.Body)
						{
							Fail("declaration-only constructors cannot be emitted", Constructor.Location);
						}
						if (Constructor.bDeleted)
						{
							continue;
						}
						std::ostringstream Signature;
						Signature << Constructor.Parameters.size();
						for (const FFunctionParam& Parameter : Constructor.Parameters)
						{
							Signature << '#' << NormalizeType(Parameter.TypeName)
							          << (Parameter.bIsRef ? "#byref" : "#value")
							          << (Parameter.bIsVariadic ? "#params" : "#fixed");
						}
						if (!ConstructorSignatures.insert(Signature.str()).second)
						{
							Fail("constructor overloads that differ only by ref direction "
							     "cannot share one C# constructor signature",
							    Constructor.Location);
						}
					}
				};
				for (const FStructDecl& Struct : Program->Structs)
				{
					if (!ShouldEmit(Struct.ModulePath) || AttributeModule(Struct.ModulePath))
					{
						continue;
					}
					ValidateTypeMembers(Struct);
					if (!Struct.BaseTypeName.empty())
					{
						Fail("struct inheritance is not representable by C# value types", Struct.Location);
					}
					for (const FConstructorDecl& Constructor : Struct.Constructors)
					{
						if (!Constructor.BaseInits.empty())
						{
							Fail("struct base-constructor initialization is not representable in C#",
							    Constructor.Location);
						}
					}
				}
				for (const FClassDecl& Class : Program->Classes)
				{
					if (!ShouldEmit(Class.ModulePath))
					{
						continue;
					}
					ValidateTypeMembers(Class);
					if (Class.BaseTypeNames.size() > 1)
					{
						Fail("C# supports only one concrete class base", Class.Location);
					}
					if (std::any_of(Class.BaseInheritanceAccess.begin(),
					        Class.BaseInheritanceAccess.end(),
					        [](const EMemberAccess Access)
					{
						return Access != EMemberAccess::Public;
					}))
					{
						Fail("protected or private class inheritance cannot be represented in C#",
						    Class.Location);
					}
					if (Class.ClassDefaults)
					{
						Fail("host-invoked class-default bodies are not available in standalone C# AOT",
						    Class.Location);
					}
					for (const FConstructorDecl& Constructor : Class.Constructors)
					{
						if (Constructor.BaseInits.size() > 1)
						{
							Fail("C# constructors can initialize only one concrete base", Constructor.Location);
						}
					}
				}
				if (bFailed)
				{
					return false;
				}
				std::unordered_set<std::string> Signatures;
				for (const FFunctionDecl& Function : Program->Functions)
				{
					if (!ShouldEmit(Function.ModulePath) || AttributeModule(Function.ModulePath) || StandardLibrary::IsMethodBindingFactoryDeclaration(Function))
					{
						continue;
					}
					if (Function.bPrecompiledPackage)
					{
						Fail("precompiled package function bodies cannot be emitted yet", Function.Location);
						return false;
					}
					const std::size_t OperatorSeparator = Function.Name.find_last_of(":.");
					const std::string_view OperatorName = std::string_view(Function.Name).substr(OperatorSeparator == std::string::npos ? 0 : OperatorSeparator + 1);
					if (!Function.bDeleted && IsOperatorName(OperatorName) && (Runtime::IsReferenceTypeName(Function.ReturnType) || ((OperatorName == "operator++" || OperatorName == "operator--") && Function.Parameters.size() == 2) || std::any_of(Function.Parameters.begin(), Function.Parameters.end(), [](const FFunctionParam& Param)
					{
						return Param.bIsRef || Param.bIsVariadic || Param.DefaultValue;
					})))
					{
						Fail("reference operators are not supported by C# source emission; use the VM, mixed or JIT backend", Function.Location);
					}
					if (!Function.Bounds.empty())
					{
						Fail("generic where constraints are not implemented yet", Function.Location);
						return false;
					}
					if (std::any_of(Function.TypeParamDefaults.begin(), Function.TypeParamDefaults.end(),
					        [](const std::string& Value)
					{
						return !Value.empty();
					}))
					{
						Fail("default generic type arguments are not representable in C#", Function.Location);
						return false;
					}
					ActiveTypeParameters.clear();
					for (const std::string& TypeParameter : Function.TypeParams)
					{
						ActiveTypeParameters.insert(NormalizeType(TypeParameter));
					}
					std::unordered_set<std::string> ReservedTypeParameterNames = {
					    FunctionIdentifier(Function)};
					for (std::size_t Index = 0; Index < Function.Parameters.size(); ++Index)
					{
						const FFunctionParam& Parameter = Function.Parameters[Index];
						ReservedTypeParameterNames.insert(Identifier(Parameter.Name));
						if (Parameter.bIsVariadic)
						{
							ReservedTypeParameterNames.insert(
							    "__VeroScriptVarargsArray" + std::to_string(Index));
						}
					}
					ConfigureTypeParameterEmissionNames(
					    Function.TypeParams, std::move(ReservedTypeParameterNames));
					std::ostringstream Signature;
					Signature << FunctionIdentifier(Function) << '`' << Function.TypeParams.size() << '(';
					for (const FFunctionParam& Parameter : Function.Parameters)
					{
						Signature << MapType(Parameter.TypeName) << ';';
					}
					ActiveTypeParameters.clear();
					ActiveTypeParameterEmissionNames.clear();
					if (bFailed)
					{
						return false;
					}
					Signature << ')';
					if (!Function.bExported)
					{
						Signature << "@module:" << Function.ModulePath;
					}
					if (!Signatures.insert(Signature.str()).second)
					{
						Fail("duplicate cross-module function signature: " + Function.Name,
						    Function.Location);
						return false;
					}
					if (Function.Name == "main")
					{
						const bool bEntry = Program->EntryModulePath.empty() || Function.ModulePath == Program->EntryModulePath;
						if (!bEntry)
						{
							Fail("imported main() is not supported; rename the imported function",
							    Function.Location);
							return false;
						}
						if (EntryMain)
						{
							Fail("multiple entry main() declarations", Function.Location);
							return false;
						}
						EntryMain = &Function;
					}
				}
				if (!EntryMain)
				{
					Fail("requires void/int main() or main(string[] args) in the entry module");
					return false;
				}
				if (IsReferenceTypeSpelling(EntryMain->ReturnType))
				{
					Fail("main cannot return a reference", EntryMain->Location);
					return false;
				}
				const std::string ReturnType = MapType(EntryMain->ReturnType);
				if (ReturnType != "void" && ReturnType != "int")
				{
					Fail("main must return void or int", EntryMain->Location);
					return false;
				}
				if (EntryMain->Parameters.size() > 1 || (EntryMain->Parameters.size() == 1 && MapType(EntryMain->Parameters[0].TypeName) != "List<VsString>"))
				{
					Fail("main optionally accepts exactly one string[] argument", EntryMain->Location);
					return false;
				}
				return true;
			}

			void EmitHostBindingContracts()
			{
				if (!HostBindings)
				{
					return;
				}
				std::unordered_set<std::string> Emitted;
				const auto EmitParameters = [&](
				                                const std::vector<ENativeType>& Types,
				                                const std::vector<std::string>& HostNames,
				                                const std::vector<std::string>& ElementNames,
				                                const std::vector<std::string>& KeyNames,
				                                const std::vector<std::string>& ValueNames,
				                                const std::vector<std::uint8_t>& Flags,
				                                const bool bReceiver)
				{
					bool bComma = false;
					if (bReceiver)
					{
						Out << "object __receiver";
						bComma = true;
					}
					for (std::size_t Index = 0; Index < Types.size(); ++Index)
					{
						if (bComma)
						{
							Out << ", ";
						}
						bComma = true;
						const std::uint8_t Flag = Index < Flags.size() ? Flags[Index] : 0;
						if (NativeParamFlagIsRef(Flag))
						{
							Out << (((Flag & NativeParamFlagConstRef) != 0) ? "in " : "ref ");
						}
						Out << NativeContractType(
						           Types[Index],
						           Index < HostNames.size() ? HostNames[Index] : std::string{},
						           Index < ElementNames.size() ? ElementNames[Index] : std::string{},
						           Index < KeyNames.size() ? KeyNames[Index] : std::string{},
						           Index < ValueNames.size() ? ValueNames[Index] : std::string{})
						    << " __arg" << Index;
					}
				};

				Out << "\n\t// Host-call contracts. BindGen --aot-bridge supplies implementations.\n";
				std::unordered_set<std::string> HostFunctionNames;
				for (const FHostFunctionDesc& Function : HostBindings->GetFunctions())
				{
					if (!HostFunctionNames.insert(Function.Name).second)
					{
						Fail("overloaded free host function '" + Function.Name + "' is not supported by C# AOT contracts");
						return;
					}
					const std::string Identity = "function:" + Function.Name;
					const std::string Method = HostMethodName(Identity);
					if (!Emitted.insert(Method).second)
					{
						continue;
					}
					Out << "\tprivate static partial "
					    << NativeContractType(Function.ReturnType, Function.ReturnHostTypeName,
					           Function.ReturnElementTypeName, Function.ReturnKeyTypeName,
					           Function.ReturnValueTypeName)
					    << ' ' << Method << '(';
					EmitParameters(Function.ParamTypes, Function.ParamHostTypeNames,
					    Function.ParamElementTypeNames, Function.ParamKeyTypeNames,
					    Function.ParamValueTypeNames, Function.ParamFlags, false);
					Out << ");\n";
				}
				for (const FHostTypeDesc& Type : HostBindings->GetTypes())
				{
					for (const FHostMemberDesc& Member : Type.Members)
					{
						if (Member.Kind == EHostMemberKind::EnumMember)
						{
							continue;
						}
						if (Member.bOperator)
						{
							Fail("host operators are not implemented by C# AOT");
							return;
						}
						const std::string Method = HostMethodName(HostMemberIdentity(Type, Member));
						if (!Emitted.insert(Method).second)
						{
							continue;
						}
						const bool bReceiver = Member.Kind == EHostMemberKind::Method || Member.Kind == EHostMemberKind::PropertyGetter || Member.Kind == EHostMemberKind::PropertySetter || Member.Kind == EHostMemberKind::Field;
						Out << "\tprivate static partial "
						    << NativeContractType(Member.ReturnType,
						           Member.ReturnHostTypeName.empty() && Member.ReturnType == ENativeType::HostObject
						               ? Type.Name
						               : Member.ReturnHostTypeName,
						           Member.ReturnElementTypeName, Member.ReturnKeyTypeName,
						           Member.ReturnValueTypeName)
						    << ' ' << Method << '(';
						EmitParameters(Member.ParamTypes, Member.ParamHostTypeNames,
						    Member.ParamElementTypeNames, Member.ParamKeyTypeNames,
						    Member.ParamValueTypeNames, Member.ParamFlags, bReceiver);
						Out << ");\n";
					}
				}
				Out << '\n';
			}

			void EmitHeader()
			{
				Out << "// SPDX-FileCopyrightText: VanstySanic\n// SPDX-License-Identifier: MIT\n\n// <auto-generated/>\n// Generated by VeroScriptCSharpAot "
				    << VEROSCRIPT_CSHARP_AOT_VERSION << R"CS(.
#nullable enable
#pragma warning disable CS8714 // VeroScript validates container key types.
#pragma warning disable CS0162 // VeroScript may require a trailing return after a constant loop.
using System;
using System.Buffers;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Text;

internal static partial class VeroScriptGeneratedProgram
{
	private sealed class VsExitException(int code) : Exception
	{
		internal int Code { get; } = code;
	}

	private sealed class VsExceptionCarrier(object? value) : Exception
	{
		internal object? Value { get; private set; } = value;
		internal bool TryGet<T>(out T result)
		{
			if (Value is T typed)
			{
				result = typed;
				return true;
			}
			result = default!;
			return false;
		}
		internal void Rebind<T>(T value) => Value = value;
	}

	private interface IVsCount { long VsCount { get; } }

	private readonly struct VsString : IEquatable<VsString>
	{
		internal VsString(string? value) => Value = value ?? string.Empty;
		internal string Value { get; }
		public bool Equals(VsString other) => string.Equals(
			Value ?? string.Empty, other.Value ?? string.Empty, StringComparison.Ordinal);
		public override bool Equals(object? value) => value is VsString other && Equals(other);
		public override int GetHashCode() => StringComparer.Ordinal.GetHashCode(Value ?? string.Empty);
		public override string ToString() => Value ?? string.Empty;
		public static bool operator ==(VsString left, VsString right) => left.Equals(right);
		public static bool operator !=(VsString left, VsString right) => !left.Equals(right);
		public static VsString operator +(VsString left, VsString right) => new VsString(left.Value + right.Value);
		public static implicit operator VsString(string value) => new VsString(value);
		public static implicit operator string(VsString value) => value.Value ?? string.Empty;
	}

	private struct VsOptional<T>
	{
		private bool present;
		private T value;
		public VsOptional(T input) { value=input; present=true; }
		public bool has_value() => present;
		public T get() => present ? value : throw new InvalidOperationException("optional has no value");
		public T value_or(T fallback) => present ? value : fallback;
		public void reset() { present=false; value=default!; }
	}
	private struct VsAttribute<T>
	{
		private sealed class Config
		{
			public VsOptional<T> Fixed;
			public Func<T>? Getter;
			public Func<VsOptional<T>>? Reader;
		}
		private Config? state;
		public VsAttribute(T value) { state=new Config { Fixed=new VsOptional<T>(value) }; }
		public void set(T value) { state=new Config { Fixed=new VsOptional<T>(value) }; }
		public void bind(Func<T> getter) { state=new Config { Getter=getter }; }
		public void bind_optional(Func<VsOptional<T>> getter) { state=new Config { Reader=getter }; }
		public VsOptional<T> try_get()
		{
			Config? snapshot=state;
			if (snapshot is null) return default;
			if (snapshot.Fixed.has_value()) return snapshot.Fixed;
			if (snapshot.Getter is not null) return new VsOptional<T>(snapshot.Getter());
			if (snapshot.Reader is not null) return snapshot.Reader();
			throw new InvalidOperationException("unbound attribute getter");
		}
		public bool try_get(ref T output)
		{
			var result=try_get();
			if (!result.has_value()) return false;
			output=result.get();
			return true;
		}
		public bool has_same_configuration(VsAttribute<T> other) => ReferenceEquals(state,other.state);
		public T get() { var result=try_get(); return result.has_value() ? result.get()
			: throw new InvalidOperationException("std::attribute has no value"); }
		public T get_or(T fallback) => try_get().value_or(fallback);
		public static implicit operator T(VsAttribute<T> value) => value.get();
		public bool is_set() => state is not null;
		public bool is_bound() => state is not null && !state.Fixed.has_value();
		public void reset() { state=null; }
	}
	private static F VsBind<T,F>(T target, Func<T,F> factory)
	{
		if (target is null) throw new InvalidOperationException("null method binding target");
		return factory(target);
	}
	private static VsAttribute<T> VsBoundAttribute<T>(Func<T> getter)
	{
		VsAttribute<T> result=default; result.bind(getter); return result;
	}
	private sealed class VsShared<T>
	{
		public T @value;
		public VsShared(T value) { this.@value = value; }
	}

	private struct VsPair<TFirst, TSecond> : IEquatable<VsPair<TFirst, TSecond>>
	{
		internal VsPair(TFirst first, TSecond second) { Key = first; Value = second; }
		internal TFirst Key { get; set; }
		internal TSecond Value { get; set; }
		public bool Equals(VsPair<TFirst, TSecond> other) =>
			EqualityComparer<TFirst>.Default.Equals(Key, other.Key)
			&& EqualityComparer<TSecond>.Default.Equals(Value, other.Value);
		public override bool Equals(object? value) =>
			value is VsPair<TFirst, TSecond> other && Equals(other);
		public override int GetHashCode() => HashCode.Combine(Key, Value);
		public static bool operator ==(
			VsPair<TFirst, TSecond> left, VsPair<TFirst, TSecond> right) => left.Equals(right);
		public static bool operator !=(
			VsPair<TFirst, TSecond> left, VsPair<TFirst, TSecond> right) => !left.Equals(right);
	}

	private sealed class VsOrderedMap<TKey, TValue> : IVsCount where TKey : notnull
	{
		public VsOrderedMap() { }
		private readonly Dictionary<TKey, int> indices = new();
		private readonly List<KeyValuePair<TKey, TValue>> entries = new();

		internal TValue this[TKey key]
		{
			get
			{
				if (!indices.TryGetValue(key, out int index))
					throw new InvalidOperationException("ordered_map key not found");
				return entries[index].Value;
			}
			set
			{
				if (indices.TryGetValue(key, out int index))
					entries[index] = new KeyValuePair<TKey, TValue>(key, value);
				else
				{
					indices.Add(key, entries.Count);
					entries.Add(new KeyValuePair<TKey, TValue>(key, value));
				}
			}
		}

		internal int Count => entries.Count;
		long IVsCount.VsCount => entries.Count;
		internal void Add(TKey key, TValue value) => this[key] = value;
		internal bool ContainsKey(TKey key) => indices.ContainsKey(key);
		internal bool Remove(TKey key)
		{
			if (!indices.Remove(key, out int index)) return false;
			entries.RemoveAt(index);
			for (int current = index; current < entries.Count; ++current)
				indices[entries[current].Key] = current;
			return true;
		}
		internal void Clear() { indices.Clear(); entries.Clear(); }
		internal void EnsureCapacity(int count)
		{
			indices.EnsureCapacity(count);
			entries.EnsureCapacity(count);
		}
		internal List<TKey> SnapshotKeys()
		{
			List<TKey> result = new(entries.Count);
			foreach (KeyValuePair<TKey, TValue> entry in entries) result.Add(entry.Key);
			return result;
		}
		internal List<TValue> SnapshotValues()
		{
			List<TValue> result = new(entries.Count);
			foreach (KeyValuePair<TKey, TValue> entry in entries) result.Add(entry.Value);
			return result;
		}
		internal List<VsPair<TKey, TValue>> SnapshotEntries()
		{
			List<VsPair<TKey, TValue>> result = new(entries.Count);
			foreach (KeyValuePair<TKey, TValue> entry in entries)
				result.Add(new VsPair<TKey, TValue>(entry.Key, entry.Value));
			return result;
		}
	}

	private readonly struct VsTypedValue(object? value, string typeName, bool counted)
	{
		internal object? Value { get; } = value;
		internal string TypeName { get; } = typeName;
		internal bool Counted { get; } = counted;
	}

	private static object? VsTyped(object? value, string typeName) => new VsTypedValue(value, typeName, true);
	private static object? VsNamed(object? value, string typeName) => new VsTypedValue(value, typeName, false);
	private static long VsCollectionCount(object? value) => value switch
	{
		ICollection collection => collection.Count,
		IVsCount counted => counted.VsCount,
		_ => throw new InvalidOperationException("value is not a collection")
	};
	private static string VsFloatText(double value, string format)
	{
		if (double.IsNaN(value))
		{
			if (OperatingSystem.IsMacOS() || OperatingSystem.IsIOS() || OperatingSystem.IsMacCatalyst() || OperatingSystem.IsTvOS()) return "nan";
			bool negative = BitConverter.DoubleToInt64Bits(value) < 0;
			return negative ? (OperatingSystem.IsWindows() ? "-nan(ind)" : "-nan") : "nan";
		}
		if (double.IsPositiveInfinity(value)) return "inf";
		if (double.IsNegativeInfinity(value)) return "-inf";
		return value.ToString(format, CultureInfo.InvariantCulture);
	}
	private static string VsFloatText(float value, string format)
	{
		if (float.IsNaN(value))
		{
			if (OperatingSystem.IsMacOS() || OperatingSystem.IsIOS() || OperatingSystem.IsMacCatalyst() || OperatingSystem.IsTvOS()) return "nan";
			bool negative = BitConverter.SingleToInt32Bits(value) < 0;
			return negative ? (OperatingSystem.IsWindows() ? "-nan(ind)" : "-nan") : "nan";
		}
		if (float.IsPositiveInfinity(value)) return "inf";
		if (float.IsNegativeInfinity(value)) return "-inf";
		return value.ToString(format, CultureInfo.InvariantCulture);
	}
	private static bool VsIsEscapedUtf8Byte(string value, int index)
	{
		char character = value[index];
		return character >= '\uDC80' && character <= '\uDCFF'
			&& (index == 0 || !char.IsHighSurrogate(value[index - 1]));
	}
	private static int VsNativeSize(long size)
	{
		if (size < 0 || size > int.MaxValue)
		{
			throw new InvalidOperationException("size or index exceeds CLR container range");
		}
		return (int)size;
	}
	private static int VsNativeIndex(long index) => VsNativeSize(index);
	private static int VsNativeIndex(uint index) => VsNativeIndex((ulong)index);
	private static int VsNativeIndex(ulong index)
	{
		if (index > int.MaxValue)
			throw new InvalidOperationException("size or index exceeds CLR container range");
		return (int)index;
	}
	private static int VsNativeIndex(object index) => index switch
	{
		sbyte value => VsNativeIndex((long)value),
		byte value => VsNativeIndex((long)value),
		short value => VsNativeIndex((long)value),
		ushort value => VsNativeIndex((long)value),
		int value => VsNativeIndex((long)value),
		uint value => VsNativeIndex(value),
		long value => VsNativeIndex(value),
		ulong value => VsNativeIndex(value),
		_ => throw new InvalidOperationException("array index must be an integer")
	};

	private static void VsNumericType<T>()
	{
		var t=typeof(T);
		if(t!=typeof(sbyte) && t!=typeof(byte) && t!=typeof(short) && t!=typeof(ushort)
			&& t!=typeof(int) && t!=typeof(uint) && t!=typeof(long) && t!=typeof(ulong)
			&& t!=typeof(float) && t!=typeof(double)) throw new InvalidOperationException("numeric type required");
	}
	private static uint VsRandomNext(List<uint> e)
	{
		if(e.Count==0) e.Add(0x6d2b79f5u);
		uint x=e[0]; if(x==0) throw new InvalidOperationException("random engine state must be nonzero");
		unchecked { x^=x<<13; x^=x>>17; x^=x<<5; } e[0]=x; return x;
	}
	private static long VsRandomIndex(List<uint> e,long n)
	{
		if(n<=0) throw new InvalidOperationException("random bound must be positive");
		const ulong b=4294967295UL; bool wide=n>int.MaxValue;
		ulong range=wide?b*b:b,bound=(ulong)n,limit=range-range%bound;
		for(;;) { ulong x=(ulong)VsRandomNext(e)-1; if(wide) x=x*b+VsRandomNext(e)-1; if(x<limit) return (long)(x%bound); }
	}
	private static void VsArrayReverse<T>(List<T> a) => a.Reverse();
	private static void VsArrayRotate<T>(List<T> a,long middle)
	{
		if(middle<0 || middle>a.Count) throw new InvalidOperationException("rotate middle out of range");
		int m=(int)middle; a.Reverse(0,m); a.Reverse(m,a.Count-m); a.Reverse();
	}
	private static void VsArrayShuffle<T>(List<T> a,List<uint> e)
	{
		if(ReferenceEquals(a,e)) throw new InvalidOperationException("shuffle target aliases engine");
		for(int n=a.Count;n>1;--n) { int j=(int)VsRandomIndex(e,n); (a[n-1],a[j])=(a[j],a[n-1]); }
	}
	private static bool VsStringStarts(VsString s,VsString p) => VsUtf8Encode(s.Value).AsSpan().StartsWith(VsUtf8Encode(p.Value));
	private static bool VsStringEnds(VsString s,VsString p) => VsUtf8Encode(s.Value).AsSpan().EndsWith(VsUtf8Encode(p.Value));
	private static long VsStringLastFind(VsString s,VsString p) => VsUtf8Encode(s.Value).AsSpan().LastIndexOf(VsUtf8Encode(p.Value));
	private static VsString VsStringRepeat(VsString s,long n)
	{
		if(n<0) throw new InvalidOperationException("repeat count must be non-negative");
		byte[] bytes=VsUtf8Encode(s.Value); if(bytes.Length==0 || n==0) return new VsString("");
		int count=VsNativeSize(checked(bytes.LongLength*n)); byte[] result=new byte[count];
		for(int i=0;i<count;i+=bytes.Length) bytes.CopyTo(result,i);
		return new VsString(VsUtf8Decode(result));
	}
	private static VsString VsStringReplace(VsString s,VsString from,VsString to)
	{
		byte[] source=VsUtf8Encode(s.Value),pattern=VsUtf8Encode(from.Value),replacement=VsUtf8Encode(to.Value);
		if(pattern.Length==0) return s;
		using var result=new global::System.IO.MemoryStream(); int start=0;
		for(;;) { int offset=source.AsSpan(start).IndexOf(pattern); if(offset<0) { result.Write(source.AsSpan(start)); break; }
			result.Write(source.AsSpan(start,offset)); result.Write(replacement); start+=offset+pattern.Length; }
		return new VsString(VsUtf8Decode(result.ToArray()));
	}
	private static VsString VsStringTrim(VsString s,bool left,bool right)
	{
		byte[] bytes=VsUtf8Encode(s.Value); int begin=0,end=bytes.Length;
		bool Space(byte b)=>b==32 || (b>=9 && b<=13);
		while(left && begin<end && Space(bytes[begin])) ++begin;
		while(right && end>begin && Space(bytes[end-1])) --end;
		return new VsString(VsUtf8Decode(bytes.AsSpan(begin,end-begin)));
	}

	private static long VsFind(VsString value, VsString needle)
	{
		return VsUtf8Encode(value.Value).AsSpan().IndexOf(VsUtf8Encode(needle.Value));
	}
	private static long VsCheckedLength(long length)
	{
		if (length < 0)
		{
			throw new InvalidOperationException("len result exceeds int64 range");
		}
		return length;
	}
	private static long VsUtf8ByteCount(string value)
	{
		long count = 0;
		for (int index = 0; index < value.Length; ++index)
		{
			char character = value[index];
			if (VsIsEscapedUtf8Byte(value, index) || character < 0x80) ++count;
			else if (character < 0x800) count += 2;
			else if (char.IsHighSurrogate(character) && index + 1 < value.Length
				&& char.IsLowSurrogate(value[index + 1]))
			{
				count += 4;
				++index;
			}
			else count += 3;
		}
		return VsCheckedLength(count);
	}
	private static byte[] VsUtf8Encode(string value)
	{
		byte[] result = new byte[VsNativeSize(VsUtf8ByteCount(value))];
		int destination = 0;
		int segment = 0;
		for (int index = 0; index < value.Length; ++index)
		{
			char character = value[index];
			if (!VsIsEscapedUtf8Byte(value, index)) continue;
			if (index > segment)
				destination += Encoding.UTF8.GetBytes(
					value.AsSpan(segment, index - segment), result.AsSpan(destination));
			result[destination++] = (byte)(character - '\uDC00');
			segment = index + 1;
		}
		if (segment < value.Length)
			Encoding.UTF8.GetBytes(value.AsSpan(segment), result.AsSpan(destination));
		return result;
	}
	private static string VsUtf8Decode(ReadOnlySpan<byte> value)
	{
		StringBuilder result = new(value.Length);
		while (!value.IsEmpty)
		{
			OperationStatus status = Rune.DecodeFromUtf8(value, out Rune rune, out int consumed);
			if (status == OperationStatus.Done)
			{
				result.Append(rune.ToString());
				value = value[consumed..];
			}
			else
			{
				result.Append((char)('\uDC00' + value[0]));
				value = value[1..];
			}
		}
		return result.ToString();
	}
	private static bool VsHasEscapedUtf8Byte(string value)
	{
		for (int index = 0; index < value.Length; ++index)
			if (VsIsEscapedUtf8Byte(value, index)) return true;
		return false;
	}
	private static void VsWriteLine(bool error, string value)
	{
		if (!VsHasEscapedUtf8Byte(value))
		{
			if (error) Console.Error.WriteLine(value);
			else Console.WriteLine(value);
			return;
		}
		if (error) Console.Error.Flush();
		else Console.Out.Flush();
		byte[] bytes = VsUtf8Encode(value);
		System.IO.Stream stream = error ? Console.OpenStandardError() : Console.OpenStandardOutput();
		stream.Write(bytes);
		foreach (char character in Environment.NewLine)
			stream.WriteByte((byte)character);
		stream.Flush();
	}
	private static void VsConsoleWrite(bool error, string value)
	{
		if (!VsHasEscapedUtf8Byte(value))
		{
			if (error) Console.Error.Write(value);
			else Console.Out.Write(value);
			return;
		}
		byte[] bytes = VsUtf8Encode(value);
		System.IO.Stream stream = error ? Console.OpenStandardError() : Console.OpenStandardOutput();
		stream.Write(bytes);
	}
	[System.Runtime.InteropServices.DllImport("kernel32.dll")]
	private static extern IntPtr GetStdHandle(int value);
	[System.Runtime.InteropServices.DllImport("kernel32.dll")]
	private static extern bool GetConsoleMode(IntPtr handle, out uint mode);
	[System.Runtime.InteropServices.DllImport("kernel32.dll")]
	private static extern bool SetConsoleMode(IntPtr handle, uint mode);
	[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
	private struct VsConsoleBufferInfo
	{
		internal short SizeX, SizeY, CursorX, CursorY;
		internal ushort Attributes;
		internal short Left, Top, Right, Bottom, MaximumX, MaximumY;
	}
	[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
	private struct VsConsoleCursorInfo
	{
		internal uint Size;
		internal int Visible;
	}
	[System.Runtime.InteropServices.DllImport("kernel32.dll")]
	private static extern bool GetConsoleScreenBufferInfo(IntPtr handle, out VsConsoleBufferInfo info);
	[System.Runtime.InteropServices.DllImport("kernel32.dll")]
	private static extern bool SetConsoleTextAttribute(IntPtr handle, ushort attributes);
	[System.Runtime.InteropServices.DllImport("kernel32.dll")]
	private static extern bool GetConsoleCursorInfo(IntPtr handle, out VsConsoleCursorInfo info);
	[System.Runtime.InteropServices.DllImport("kernel32.dll")]
	private static extern bool SetConsoleCursorInfo(IntPtr handle, in VsConsoleCursorInfo info);
	private static bool VsConsoleIsOutputTerminal() => !Console.IsOutputRedirected;
	private static bool VsConsoleIsInteractive() => !Console.IsInputRedirected && VsConsoleIsOutputTerminal();
	private static bool VsConsoleSupportsColor()
	{
		if (!VsConsoleIsOutputTerminal()) return false;
		if (OperatingSystem.IsWindows()) return GetConsoleMode(GetStdHandle(-11), out _);
		string? term = Environment.GetEnvironmentVariable("TERM");
		return !string.IsNullOrEmpty(term) && term != "dumb";
	}
	private static bool VsConsoleSupportsCursor() => VsConsoleSupportsColor();
	private static bool VsConsoleSupportsClear() => VsConsoleSupportsColor();
	private static int VsConsoleWidth()
	{
		if (!VsConsoleIsOutputTerminal()) return 0;
		try { return Console.WindowWidth; }
		catch (System.IO.IOException)
		{
			return 0;
		}
		catch (PlatformNotSupportedException)
		{
			return 0;
		}
	}
	private static int VsConsoleHeight()
	{
		if (!VsConsoleIsOutputTerminal()) return 0;
		try { return Console.WindowHeight; }
		catch (System.IO.IOException)
		{
			return 0;
		}
		catch (PlatformNotSupportedException)
		{
			return 0;
		}
	}
	private static readonly object VsConsoleSync = new();
	private static bool VsConsoleDirtyStyle, VsConsoleDirtyCursor;
	private static IntPtr VsConsoleRestoreHandle;
	private static VsConsoleBufferInfo VsConsoleOriginalBuffer;
	private static VsConsoleCursorInfo VsConsoleOriginalCursor;
	private static bool VsConsoleHaveAttributes, VsConsoleHaveCursor;
	private static void VsConsoleMark(bool style, bool cursor)
	{
		lock (VsConsoleSync)
		{
			if (!VsConsoleDirtyStyle && !VsConsoleDirtyCursor && OperatingSystem.IsWindows())
			{
				VsConsoleRestoreHandle = GetStdHandle(-11);
				VsConsoleHaveAttributes = GetConsoleScreenBufferInfo(VsConsoleRestoreHandle, out VsConsoleOriginalBuffer);
				VsConsoleHaveCursor = GetConsoleCursorInfo(VsConsoleRestoreHandle, out VsConsoleOriginalCursor);
			}
			VsConsoleDirtyStyle |= style;
			VsConsoleDirtyCursor |= cursor;
		}
	}
	private static bool VsConsoleSequence(string sequence)
	{
		lock (VsConsoleSync)
		{
			if (!VsConsoleSupportsColor()) return false;
			IntPtr handle = IntPtr.Zero; uint mode = 0;
			if (OperatingSystem.IsWindows())
			{
				handle = GetStdHandle(-11);
				if (!GetConsoleMode(handle, out mode) || !SetConsoleMode(handle, mode | 4)) return false;
			}
			try { VsConsoleWrite(false, sequence); Console.Out.Flush(); return true; }
			catch (System.IO.IOException)
			{
				return false;
			}
			finally
			{
				if (handle != IntPtr.Zero) SetConsoleMode(handle, mode);
			}
		}
	}
	private static bool VsConsoleClear() => VsConsoleSequence("\u001b[2J\u001b[H");
	private static bool VsConsoleClearLine(string mode)
	{
		int value = mode switch { "after" => 0, "before" => 1, "all" => 2,
			_ => throw new InvalidOperationException("std::console::clear_line: expected all, before or after") };
		return VsConsoleSequence("\u001b[" + value + "K");
	}
	private static bool VsConsoleSetColor(string foreground, string background)
	{
		lock (VsConsoleSync)
		{
			string[] names = { "black", "red", "green", "yellow", "blue", "magenta", "cyan", "white",
				"bright_black", "bright_red", "bright_green", "bright_yellow",
				"bright_blue", "bright_magenta", "bright_cyan", "bright_white" };
			int Color(string name, bool bg)
			{
				if (name == "default") return bg ? 49 : 39;
				int index = Array.IndexOf(names, name);
				if (index < 0) throw new InvalidOperationException("std::console::set_color: unknown color name");
				return index < 8 ? (bg ? 40 : 30) + index : (bg ? 100 : 90) + index - 8;
			}
			int first = Color(foreground, false), second = Color(background, true);
			if (!VsConsoleSupportsColor()) return false;
			VsConsoleMark(true, false);
			if (OperatingSystem.IsWindows() && VsConsoleHaveAttributes)
			{
				int[] palette = { 0, 4, 2, 6, 1, 5, 3, 7 };
				int Bits(string name, bool bg)
				{
					if (name == "default") return VsConsoleOriginalBuffer.Attributes & (bg ? 0xf0 : 0x0f);
					int index = Array.IndexOf(names, name);
					int value = palette[index % 8] | (index >= 8 ? 8 : 0);
					return bg ? value << 4 : value;
				}
				return SetConsoleTextAttribute(VsConsoleRestoreHandle, (ushort)((VsConsoleOriginalBuffer.Attributes & ~0xff)
					| Bits(foreground, false) | Bits(background, true)));
			}
			return VsConsoleSequence("\u001b[" + first + ";" + second + "m");
		}
	}
	private static bool VsConsoleResetStyle()
	{
		lock (VsConsoleSync)
		{
			if (!VsConsoleSupportsColor()) return false;
			VsConsoleMark(true, false);
			if (OperatingSystem.IsWindows() && VsConsoleHaveAttributes)
				return SetConsoleTextAttribute(VsConsoleRestoreHandle, VsConsoleOriginalBuffer.Attributes);
			return VsConsoleSequence("\u001b[0m");
		}
	}
	private static bool VsConsoleMoveCursor(long column, long row)
	{
		if (column < 0 || row < 0 || column > 32766 || row > 32766)
			throw new InvalidOperationException("std::console::move_cursor coordinates must be in 0..32766");
		int width = VsConsoleWidth(), height = VsConsoleHeight();
		if ((width > 0 && column >= width) || (height > 0 && row >= height)) return false;
		return VsConsoleSequence("\u001b[" + (row + 1) + ";" + (column + 1) + "H");
	}
	private static bool VsConsoleSetCursorVisible(bool visible)
	{
		lock (VsConsoleSync)
		{
			if (!VsConsoleSupportsCursor()) return false;
			VsConsoleMark(false, true);
			return VsConsoleSequence(visible ? "\u001b[?25h" : "\u001b[?25l");
		}
	}
	private static void VsConsoleRestore()
	{
		lock (VsConsoleSync)
		{
			bool restoreStyle = VsConsoleDirtyStyle, restoreCursor = VsConsoleDirtyCursor;
			try
			{
				try { Console.Out.Flush(); }
				catch
				{
				}
				if (OperatingSystem.IsWindows())
				{
					try
					{
						if (restoreStyle && VsConsoleHaveAttributes
							&& SetConsoleTextAttribute(VsConsoleRestoreHandle, VsConsoleOriginalBuffer.Attributes))
							restoreStyle = false;
					}
					catch
					{
					}
					try
					{
						if (restoreCursor && VsConsoleHaveCursor
							&& SetConsoleCursorInfo(VsConsoleRestoreHandle, in VsConsoleOriginalCursor))
							restoreCursor = false;
					}
					catch
					{
					}
				}
				try { if (restoreStyle) VsConsoleResetStyle(); }
				catch
				{
				}
				try { if (restoreCursor) VsConsoleSequence("\u001b[?25h"); }
				catch
				{
				}
			}
			finally
			{
				VsConsoleDirtyStyle = VsConsoleDirtyCursor = false;
				VsConsoleHaveAttributes = VsConsoleHaveCursor = false;
				VsConsoleRestoreHandle = IntPtr.Zero;
			}
		}
	}

	private static string VsText(object? value) => value switch
	{
		null => "nullptr",
		VsTypedValue typed when typed.Counted => typed.TypeName + "[" + VsCollectionCount(typed.Value).ToString(CultureInfo.InvariantCulture) + "]",
		VsTypedValue typed => typed.TypeName,
		VsString text => text.Value,
		bool boolean => boolean ? "true" : "false",
		Enum enumValue => VsEnumText(enumValue),
		double number => VsFloatText(number, "G15"),
		float number => VsFloatText(number, "G9"),
		IFormattable formattable => formattable.ToString(null, CultureInfo.InvariantCulture),
		_ => value.ToString() ?? string.Empty
	};
	private static VsString VsConcat(object? left, object? right) => new(VsText(left) + VsText(right));
	private static void VsPrint(object? value) => VsWriteLine(false, VsText(value));
	private static void VsEprint(object? value) => VsWriteLine(true, VsText(value));
	private static VsString VsToString(object? value) => new(VsText(value));
	private static string VsToWString(object? value) => VsText(value);

	private static T VsDefault<
		[DynamicallyAccessedMembers(DynamicallyAccessedMemberTypes.PublicParameterlessConstructor)] T>()
	{
		if (typeof(T) == typeof(VsString)) return (T)(object)new VsString(string.Empty);
		if (typeof(T) == typeof(string)) return (T)(object)string.Empty;
		Type type = typeof(T);
		if (type.IsGenericType)
		{
			Type definition = type.GetGenericTypeDefinition();
			if (definition == typeof(List<>) || definition == typeof(Dictionary<,>)
				|| definition == typeof(VsOrderedMap<,>) || definition == typeof(VsMultiMap<,>)
				|| definition == typeof(VsOrderedMultiMap<,>) || definition == typeof(HashSet<>)
				|| definition == typeof(VsMultiSet<>) || definition == typeof(VsOrderedMultiSet<>)
				|| definition == typeof(Queue<>) || definition == typeof(VsPriorityQueue<>)
				|| definition == typeof(Stack<>))
				return (T)Activator.CreateInstance(type)!;
		}
		return default!;
	}

)CS";
				Out << R"CS(
	private sealed class VsMultiMap<TKey, TValue> : IVsCount, IEnumerable<KeyValuePair<TKey, List<TValue>>> where TKey : notnull
	{
		private readonly Dictionary<TKey, List<TValue>> entries = new();
		internal List<TValue> this[TKey key] => entries.TryGetValue(key, out List<TValue>? values)
			? values : throw new InvalidOperationException("multimap key not found");
		internal int Count => entries.Count;
		long IVsCount.VsCount => entries.Count;
		internal void Add(TKey key, TValue value)
		{
			if (!entries.TryGetValue(key, out List<TValue>? values))
				entries.Add(key, values = new List<TValue>());
			values.Add(value);
		}
		internal void Add(TKey key, IEnumerable<TValue> values)
		{
			foreach (TValue value in values) Add(key, value);
		}
		internal bool ContainsKey(TKey key) => entries.ContainsKey(key);
		internal bool Remove(TKey key) => entries.Remove(key);
		internal void Clear() => entries.Clear();
		internal void EnsureCapacity(int count) => entries.EnsureCapacity(count);
		internal List<TKey> SnapshotKeys() => new(entries.Keys);
		internal List<List<TValue>> SnapshotValues() => new(entries.Values);
		internal List<VsPair<TKey, List<TValue>>> SnapshotEntries()
		{
			List<VsPair<TKey, List<TValue>>> result = new(entries.Count);
			foreach (KeyValuePair<TKey, List<TValue>> entry in entries)
				result.Add(new VsPair<TKey, List<TValue>>(entry.Key, entry.Value));
			return result;
		}
		public IEnumerator<KeyValuePair<TKey, List<TValue>>> GetEnumerator() => entries.GetEnumerator();
		IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
	}

	private sealed class VsOrderedMultiMap<TKey, TValue> : IVsCount, IEnumerable<KeyValuePair<TKey, List<TValue>>> where TKey : notnull
	{
		private readonly VsOrderedMap<TKey, List<TValue>> entries = new();
		internal List<TValue> this[TKey key] => entries[key];
		internal int Count => entries.Count;
		long IVsCount.VsCount => entries.Count;
		internal void Add(TKey key, TValue value)
		{
			if (!entries.ContainsKey(key)) entries.Add(key, new List<TValue>());
			entries[key].Add(value);
		}
		internal void Add(TKey key, IEnumerable<TValue> values)
		{
			foreach (TValue value in values) Add(key, value);
		}
		internal bool ContainsKey(TKey key) => entries.ContainsKey(key);
		internal bool Remove(TKey key) => entries.Remove(key);
		internal void Clear() => entries.Clear();
		internal void EnsureCapacity(int count) => entries.EnsureCapacity(count);
		internal List<TKey> SnapshotKeys() => entries.SnapshotKeys();
		internal List<List<TValue>> SnapshotValues() => entries.SnapshotValues();
		internal List<VsPair<TKey, List<TValue>>> SnapshotEntries() => entries.SnapshotEntries();
		public IEnumerator<KeyValuePair<TKey, List<TValue>>> GetEnumerator()
		{
			foreach (TKey key in entries.SnapshotKeys()) yield return new(key, entries[key]);
		}
		IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
	}

	private sealed class VsMultiSet<T> : IVsCount, IEnumerable<T> where T : notnull
	{
		private readonly Dictionary<T, long> entries = new();
		private long count;
		internal long Count => count;
		long IVsCount.VsCount => count;
		internal void Add(T value)
		{
			if (count == long.MaxValue)
			{
				throw new InvalidOperationException("multiset size exceeds int64 range");
			}
			entries.TryGetValue(value, out long occurrences);
			entries[value] = occurrences + 1;
			++count;
		}
		internal long Occurrences(T value) => entries.TryGetValue(value, out long occurrences) ? occurrences : 0;
		internal bool Contains(T value) => entries.ContainsKey(value);
		internal bool Remove(T value)
		{
			if (!entries.TryGetValue(value, out long occurrences)) return false;
			if (occurrences == 1) entries.Remove(value);
			else entries[value] = occurrences - 1;
			--count;
			return true;
		}
		internal void Clear() { entries.Clear(); count = 0; }
		internal void EnsureCapacity(int capacity) => entries.EnsureCapacity(capacity);
		internal List<T> SnapshotValues()
		{
			List<T> result = new(VsNativeSize(count));
			foreach (KeyValuePair<T, long> entry in entries)
				for (long index = 0; index < entry.Value; ++index) result.Add(entry.Key);
			return result;
		}
		public IEnumerator<T> GetEnumerator() => SnapshotValues().GetEnumerator();
		IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
	}

	private sealed class VsOrderedMultiSet<T> : IVsCount, IEnumerable<T> where T : notnull
	{
		private readonly VsOrderedMap<T, long> entries = new();
		private long count;
		internal long Count => count;
		long IVsCount.VsCount => count;
		internal void Add(T value)
		{
			if (count == long.MaxValue)
			{
				throw new InvalidOperationException("multiset size exceeds int64 range");
			}
			if (entries.ContainsKey(value)) entries[value] = entries[value] + 1;
			else entries.Add(value, 1);
			++count;
		}
		internal long Occurrences(T value) => entries.ContainsKey(value) ? entries[value] : 0;
		internal bool Contains(T value) => entries.ContainsKey(value);
		internal bool Remove(T value)
		{
			if (!entries.ContainsKey(value)) return false;
			long occurrences = entries[value];
			if (occurrences == 1) entries.Remove(value);
			else entries[value] = occurrences - 1;
			--count;
			return true;
		}
		internal void Clear() { entries.Clear(); count = 0; }
		internal void EnsureCapacity(int capacity) => entries.EnsureCapacity(capacity);
		internal List<T> SnapshotValues()
		{
			List<T> result = new(VsNativeSize(count));
			foreach (T value in entries.SnapshotKeys())
				for (long index = 0; index < entries[value]; ++index) result.Add(value);
			return result;
		}
		public IEnumerator<T> GetEnumerator() => SnapshotValues().GetEnumerator();
		IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
	}

	private sealed class VsPriorityQueue<T> : IVsCount, IEnumerable<T>
	{
		private readonly List<T> heap = new();
		internal int Count => heap.Count;
		long IVsCount.VsCount => heap.Count;
		private static int Compare(T left, T right) => Comparer<T>.Default.Compare(left, right);
		internal void Add(T value)
		{
			heap.Add(value);
			for (int child = heap.Count - 1; child > 0;)
			{
				int parent = (child - 1) / 2;
				if (Compare(heap[parent], heap[child]) >= 0) break;
				(heap[parent], heap[child]) = (heap[child], heap[parent]);
				child = parent;
			}
		}
		internal T Top() => heap.Count != 0 ? heap[0]
			: throw new InvalidOperationException("priority_queue is empty");
		internal T Pop()
		{
			T result = Top();
			T last = heap[^1];
			heap.RemoveAt(heap.Count - 1);
			if (heap.Count == 0) return result;
			heap[0] = last;
			for (int parent = 0;;)
			{
				int left = parent * 2 + 1;
				if (left >= heap.Count) break;
				int right = left + 1;
				int child = right < heap.Count && Compare(heap[right], heap[left]) > 0 ? right : left;
				if (Compare(heap[parent], heap[child]) >= 0) break;
				(heap[parent], heap[child]) = (heap[child], heap[parent]);
				parent = child;
			}
			return result;
		}
		internal void Clear() => heap.Clear();
		// Match VeroScript's values(): it returns the sequence produced by
		// repeatedly popping a copy, rather than exposing heap storage order.
		internal List<T> SnapshotValues()
		{
			var pending = new VsPriorityQueue<T>();
			pending.heap.AddRange(heap);
			List<T> result = new(pending.Count);
			while (pending.Count != 0) result.Add(pending.Pop());
			return result;
		}
		public IEnumerator<T> GetEnumerator() => SnapshotValues().GetEnumerator();
		IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
	}

)CS";
				Out << R"CS(
	// Materializes VeroScript's by-value captures before constructing the delegate.
	private static TDelegate VsCapture<TCapture, TDelegate>(
		TCapture capture, Func<TCapture, TDelegate> factory) => factory(capture);

	private interface IVsLocalStaticCell { void Clear(); }
	private sealed class VsLocalStaticState
	{
		public readonly Dictionary<(System.Reflection.MethodInfo, Type), IVsLocalStaticCell> Cells = new();
		public readonly List<IVsLocalStaticCell> Completed = new();
		public bool Entered;
		public bool Destroying;
		public void Clear()
		{
			Destroying = true;
			for (int index = Completed.Count - 1; index >= 0; --index) Completed[index].Clear();
			Completed.Clear();
		}
	}
	private sealed class VsLocalStaticCell<T> : IVsLocalStaticCell
	{
		private readonly VsLocalStaticState owner;
		private bool initializing, initialized;
		public T Value = default!;
		public VsLocalStaticCell(VsLocalStaticState owner) { this.owner = owner; }
		public bool Begin()
		{
			if (initialized) return false;
			if (owner.Destroying) throw new InvalidOperationException("local static initialization during state destruction");
			if (initializing) throw new InvalidOperationException("recursive local static initialization");
			initializing = true;
			return true;
		}
		public void Commit(T value)
		{
			owner.Completed.Add(this);
			Value = value;
			initialized = true;
			initializing = false;
		}
		public void Abort() { initializing = false; }
		public void Clear() { Value = default!; initialized = false; }
	}
	[ThreadStatic] private static VsLocalStaticState? vsLocalStaticState;
	private sealed class VsLocalStaticScope : IDisposable
	{
		private readonly VsLocalStaticState? previous;
		private readonly VsLocalStaticState state;
		public VsLocalStaticScope()
		{
			var current = vsLocalStaticState;
			// Type initializers can reach local statics before Main. Their
			// pending state belongs to the first entry, rather than a new scope.
			if (current is not null && !current.Entered)
			{
				state = current;
				previous = null;
			}
			else
			{
				previous = current;
				state = new();
			}
			state.Entered = true;
			vsLocalStaticState = state;
		}
		public void Dispose() { state.Clear(); vsLocalStaticState = previous; }
	}
	private static VsLocalStaticCell<T> VsLocalStatic<T>(Func<T> marker)
	{
		var state = vsLocalStaticState ??= new VsLocalStaticState();
		var key = (marker.Method, typeof(T));
		if (state.Cells.TryGetValue(key, out var value)) return (VsLocalStaticCell<T>)value;
		var cell = new VsLocalStaticCell<T>(state);
		state.Cells.Add(key, cell);
		return cell;
	}

	private static T VsGenericAdd<T>(T left, T right)
	{
		if (typeof(T) == typeof(VsString))
			return (T)(object)((VsString)(object)left! + (VsString)(object)right!);
		if (typeof(T) == typeof(string))
			return (T)(object)string.Concat((string?)(object?)left, (string?)(object?)right);
		if (typeof(T) == typeof(sbyte))
			return (T)(object)checked((sbyte)((sbyte)(object)left! + (sbyte)(object)right!));
		if (typeof(T) == typeof(byte))
			return (T)(object)checked((byte)((byte)(object)left! + (byte)(object)right!));
		if (typeof(T) == typeof(short))
			return (T)(object)checked((short)((short)(object)left! + (short)(object)right!));
		if (typeof(T) == typeof(ushort))
			return (T)(object)checked((ushort)((ushort)(object)left! + (ushort)(object)right!));
		if (typeof(T) == typeof(int))
			return (T)(object)checked((int)(object)left! + (int)(object)right!);
		if (typeof(T) == typeof(long))
			return (T)(object)checked((long)(object)left! + (long)(object)right!);
		if (typeof(T) == typeof(uint))
			return (T)(object)unchecked((uint)(object)left! + (uint)(object)right!);
		if (typeof(T) == typeof(ulong))
			return (T)(object)unchecked((ulong)(object)left! + (ulong)(object)right!);
		if (typeof(T) == typeof(float))
			return (T)(object)((float)(object)left! + (float)(object)right!);
		if (typeof(T) == typeof(double))
			return (T)(object)((double)(object)left! + (double)(object)right!);
		throw new InvalidOperationException("generic addition requires a numeric or string type");
	}

)CS";
				Out << R"CS(
	private static decimal VsFloatingIntegerValue(double value, decimal minimum, decimal maximum)
	{
		if (!double.IsFinite(value) || (minimum == 0m && value < 0.0))
		{
			throw new OverflowException();
		}
		double truncated = Math.Truncate(value);
		// Integer ranges end just before an exactly representable power of two.
		// Converting the floating value through decimal would round it first.
		if (truncated < (double)minimum || truncated >= (double)(maximum + 1m))
		{
			throw new OverflowException();
		}
		return minimum < 0m
			? (decimal)checked((long)truncated) : (decimal)checked((ulong)truncated);
	}

	private static decimal VsIntegerValue(object? value, decimal minimum, decimal maximum, string name)
	{
		if (value is null) throw new InvalidOperationException(name + " expects numeric or bool argument");
		Type type = value.GetType();
		if (type.IsEnum) value = Convert.ChangeType(value, Enum.GetUnderlyingType(type), CultureInfo.InvariantCulture);
		TypeCode code = Convert.GetTypeCode(value);
		if (code is not (TypeCode.Boolean or TypeCode.SByte or TypeCode.Byte
			or TypeCode.Int16 or TypeCode.UInt16 or TypeCode.Int32 or TypeCode.UInt32
			or TypeCode.Int64 or TypeCode.UInt64 or TypeCode.Single or TypeCode.Double
			or TypeCode.Decimal))
			throw new InvalidOperationException(name + " expects numeric or bool argument");
		try
		{
			if (value is double doubleValue)
			{
				return VsFloatingIntegerValue(doubleValue, minimum, maximum);
			}
			if (value is float floatValue)
			{
				return VsFloatingIntegerValue(floatValue, minimum, maximum);
			}
			decimal truncated = decimal.Truncate(Convert.ToDecimal(value, CultureInfo.InvariantCulture));
			if (truncated < minimum || truncated > maximum) throw new OverflowException();
			return truncated;
		}
		catch (OverflowException)
		{
			throw new InvalidOperationException(name + " overflow");
		}
	}
	private static sbyte VsToInt8(object? value) => decimal.ToSByte(VsIntegerValue(value, sbyte.MinValue, sbyte.MaxValue, "to_int8"));
	private static byte VsToUInt8(object? value) => decimal.ToByte(VsIntegerValue(value, byte.MinValue, byte.MaxValue, "to_uint8"));
	private static short VsToInt16(object? value) => decimal.ToInt16(VsIntegerValue(value, short.MinValue, short.MaxValue, "to_int16"));
	private static ushort VsToUInt16(object? value) => decimal.ToUInt16(VsIntegerValue(value, ushort.MinValue, ushort.MaxValue, "to_uint16"));
	private static int VsToInt(object? value) => decimal.ToInt32(VsIntegerValue(value, int.MinValue, int.MaxValue, "to_int"));
	private static uint VsToUnsigned(object? value) => decimal.ToUInt32(VsIntegerValue(value, uint.MinValue, uint.MaxValue, "to_unsigned"));
	private static long VsToInt64(object? value) => decimal.ToInt64(VsIntegerValue(value, long.MinValue, long.MaxValue, "to_int64"));
	private static ulong VsToUInt64(object? value) => decimal.ToUInt64(VsIntegerValue(value, ulong.MinValue, ulong.MaxValue, "to_uint64"));
	private static double VsToDouble(object? value) => value is bool boolean
		? (boolean ? 1.0 : 0.0) : Convert.ToDouble(value, CultureInfo.InvariantCulture);
	private static float VsToFloat(object? value) => value is bool boolean
		? (boolean ? 1.0f : 0.0f) : (float)Convert.ToDouble(value, CultureInfo.InvariantCulture);

	private static int VsShiftLeft(int value, long count) { VsCheckShift(count, 32); return value << (int)count; }
	private static uint VsShiftLeft(uint value, long count) { VsCheckShift(count, 32); return value << (int)count; }
	private static long VsShiftLeft(long value, long count) { VsCheckShift(count, 64); return value << (int)count; }
	private static ulong VsShiftLeft(ulong value, long count) { VsCheckShift(count, 64); return value << (int)count; }
	private static int VsShiftRight(int value, long count) { VsCheckShift(count, 32); return value >> (int)count; }
	private static uint VsShiftRight(uint value, long count) { VsCheckShift(count, 32); return value >> (int)count; }
	private static long VsShiftRight(long value, long count) { VsCheckShift(count, 64); return value >> (int)count; }
	private static ulong VsShiftRight(ulong value, long count) { VsCheckShift(count, 64); return value >> (int)count; }
	private static void VsCheckShift(long count, int width)
	{
		if (count < 0 || count >= width) throw new InvalidOperationException("shift count out of range");
	}
	private static sbyte VsPostInc(ref sbyte value) { sbyte old = value; value = checked((sbyte)(value + 1)); return old; }
	private static byte VsPostInc(ref byte value) { byte old = value; value = checked((byte)(value + 1)); return old; }
	private static short VsPostInc(ref short value) { short old = value; value = checked((short)(value + 1)); return old; }
	private static ushort VsPostInc(ref ushort value) { ushort old = value; value = checked((ushort)(value + 1)); return old; }
	private static int VsPostInc(ref int value) { int old = value; value = checked(value + 1); return old; }
	private static uint VsPostInc(ref uint value) { uint old = value; value = unchecked(value + 1); return old; }
	private static long VsPostInc(ref long value) { long old = value; value = checked(value + 1); return old; }
	private static ulong VsPostInc(ref ulong value) { ulong old = value; value = unchecked(value + 1); return old; }
	private static sbyte VsPostDec(ref sbyte value) { sbyte old = value; value = checked((sbyte)(value - 1)); return old; }
	private static byte VsPostDec(ref byte value) { byte old = value; value = checked((byte)(value - 1)); return old; }
	private static short VsPostDec(ref short value) { short old = value; value = checked((short)(value - 1)); return old; }
	private static ushort VsPostDec(ref ushort value) { ushort old = value; value = checked((ushort)(value - 1)); return old; }
	private static int VsPostDec(ref int value) { int old = value; value = checked(value - 1); return old; }
	private static uint VsPostDec(ref uint value) { uint old = value; value = unchecked(value - 1); return old; }
	private static long VsPostDec(ref long value) { long old = value; value = checked(value - 1); return old; }
	private static ulong VsPostDec(ref ulong value) { ulong old = value; value = unchecked(value - 1); return old; }
	private static float VsDivide(float left, float right)
	{
		if (right == 0.0f) throw new DivideByZeroException();
		return left / right;
	}
	private static double VsDivide(double left, double right)
	{
		if (right == 0.0) throw new DivideByZeroException();
		return left / right;
	}
	private static float VsRemainder(float left, float right)
	{
		throw new InvalidOperationException("modulo is only defined for integers");
	}
	private static double VsRemainder(double left, double right)
	{
		throw new InvalidOperationException("modulo is only defined for integers");
	}

	private static sbyte VsAbs(sbyte value) => Math.Abs(value);
	private static byte VsAbs(byte value) => value;
	private static short VsAbs(short value) => Math.Abs(value);
	private static ushort VsAbs(ushort value) => value;
	private static int VsAbs(int value) => Math.Abs(value);
	private static uint VsAbs(uint value) => value;
	private static long VsAbs(long value) => Math.Abs(value);
	private static ulong VsAbs(ulong value) => value;
	private static float VsAbs(float value) => Math.Abs(value);
	private static double VsAbs(double value) => Math.Abs(value);
	private static sbyte VsMin(sbyte left, sbyte right) => left < right ? left : right;
	private static byte VsMin(byte left, byte right) => left < right ? left : right;
	private static short VsMin(short left, short right) => left < right ? left : right;
	private static ushort VsMin(ushort left, ushort right) => left < right ? left : right;
	private static int VsMin(int left, int right) => left < right ? left : right;
	private static uint VsMin(uint left, uint right) => left < right ? left : right;
	private static long VsMin(long left, long right) => left < right ? left : right;
	private static ulong VsMin(ulong left, ulong right) => left < right ? left : right;
	private static float VsMin(float left, float right) => left < right ? left : right;
	private static double VsMin(double left, double right) => left < right ? left : right;
	private static sbyte VsMax(sbyte left, sbyte right) => left > right ? left : right;
	private static byte VsMax(byte left, byte right) => left > right ? left : right;
	private static short VsMax(short left, short right) => left > right ? left : right;
	private static ushort VsMax(ushort left, ushort right) => left > right ? left : right;
	private static int VsMax(int left, int right) => left > right ? left : right;
	private static uint VsMax(uint left, uint right) => left > right ? left : right;
	private static long VsMax(long left, long right) => left > right ? left : right;
	private static ulong VsMax(ulong left, ulong right) => left > right ? left : right;
	private static float VsMax(float left, float right) => left > right ? left : right;
	private static double VsMax(double left, double right) => left > right ? left : right;

	private static List<int> VsRange(long count)
	{
		if (count > 1_000_000) throw new InvalidOperationException("range count too large (max 1000000)");
		List<int> result = new(VsNativeSize(Math.Max(0L, count)));
		for (int index = 0; index < count; ++index) result.Add(index);
		return result;
	}

	private static long VsLen(VsString value) => VsUtf8ByteCount(value.Value);
	private static long VsLen(string value) => value.Length;
	private static VsString VsSubstr(VsString value, long start, long count)
	{
		byte[] bytes = VsUtf8Encode(value.Value);
		if (start < 0 || count < 0 || start > bytes.Length)
		{
			throw new InvalidOperationException("substr start/count out of range");
		}
		return new VsString(VsUtf8Decode(bytes.AsSpan(VsNativeSize(start), VsNativeSize(Math.Min(count, bytes.LongLength - start)))));
	}
	private static int VsCharCode(VsString value, long index)
	{
		byte[] bytes = VsUtf8Encode(value.Value);
		if (index < 0 || index >= bytes.Length)
		{
			throw new InvalidOperationException("char index out of range");
		}
		return bytes[index];
	}
	private static long VsLen<T>(List<T> value) => value.Count;
	private static long VsLen<T>(HashSet<T> value) => value.Count;
	private static long VsLen<T>(Queue<T> value) => value.Count;
	private static long VsLen<T>(Stack<T> value) => value.Count;
	private static long VsLen<TKey, TValue>(Dictionary<TKey, TValue> value) where TKey : notnull => value.Count;
	private static long VsLen<TKey, TValue>(VsOrderedMap<TKey, TValue> value) where TKey : notnull => value.Count;
	private static long VsLen<TKey, TValue>(VsMultiMap<TKey, TValue> value) where TKey : notnull => value.Count;
	private static long VsLen<TKey, TValue>(VsOrderedMultiMap<TKey, TValue> value) where TKey : notnull => value.Count;
	private static long VsLen<T>(VsMultiSet<T> value) where T : notnull => value.Count;
	private static long VsLen<T>(VsOrderedMultiSet<T> value) where T : notnull => value.Count;
	private static long VsLen<T>(VsPriorityQueue<T> value) => value.Count;
	private static List<T> VsPush<T>(List<T> value, T item) { value.Add(item); return value; }
	private static Queue<T> VsPush<T>(Queue<T> value, T item) { value.Enqueue(item); return value; }
	private static Stack<T> VsPush<T>(Stack<T> value, T item) { value.Push(item); return value; }
	private static VsPriorityQueue<T> VsPush<T>(VsPriorityQueue<T> value, T item) { value.Add(item); return value; }
	private static T VsPop<T>(Queue<T> value) => value.Dequeue();
	private static T VsPop<T>(Stack<T> value) => value.Pop();
	private static T VsPop<T>(VsPriorityQueue<T> value) => value.Pop();
	private static T VsFront<T>(Queue<T> value) => value.Peek();
	private static T VsFront<T>(List<T> value) => value[0];
	private static T VsTop<T>(Stack<T> value) => value.Peek();
	private static T VsTop<T>(VsPriorityQueue<T> value) => value.Top();
	private static T VsBack<T>(List<T> value) => value[value.Count - 1];
	private static List<T> VsPushBack<T>(List<T> value, T item) { value.Add(item); return value; }
	private static List<T> VsPushFront<T>(List<T> value, T item) { value.Insert(0, item); return value; }
	private static List<T> VsRemoveAt<T>(List<T> value, long index)
	{
		if (index < 0 || index >= value.Count) throw new InvalidOperationException("array index out of range");
		value.RemoveAt(VsNativeSize(index));
		return value;
	}
	private static T VsPopBack<T>(List<T> value) { T item = VsBack(value); value.RemoveAt(value.Count - 1); return item; }
	private static T VsPopFront<T>(List<T> value) { T item = VsFront(value); value.RemoveAt(0); return item; }

)CS";
				Out << R"CS(
	private static List<T> VsClear<T>(List<T> value) { value.Clear(); return value; }
	private static HashSet<T> VsClear<T>(HashSet<T> value) { value.Clear(); return value; }
	private static Queue<T> VsClear<T>(Queue<T> value) { value.Clear(); return value; }
	private static Stack<T> VsClear<T>(Stack<T> value) { value.Clear(); return value; }
	private static VsPriorityQueue<T> VsClear<T>(VsPriorityQueue<T> value) { value.Clear(); return value; }
	private static Dictionary<TKey, TValue> VsClear<TKey, TValue>(Dictionary<TKey, TValue> value) where TKey : notnull { value.Clear(); return value; }
	private static VsOrderedMap<TKey, TValue> VsClear<TKey, TValue>(VsOrderedMap<TKey, TValue> value) where TKey : notnull { value.Clear(); return value; }
	private static VsMultiMap<TKey, TValue> VsClear<TKey, TValue>(VsMultiMap<TKey, TValue> value) where TKey : notnull { value.Clear(); return value; }
	private static VsOrderedMultiMap<TKey, TValue> VsClear<TKey, TValue>(VsOrderedMultiMap<TKey, TValue> value) where TKey : notnull { value.Clear(); return value; }
	private static VsMultiSet<T> VsClear<T>(VsMultiSet<T> value) where T : notnull { value.Clear(); return value; }
	private static VsOrderedMultiSet<T> VsClear<T>(VsOrderedMultiSet<T> value) where T : notnull { value.Clear(); return value; }
	private static bool VsHas<T>(HashSet<T> value, T item) => value.Contains(item);
	private static bool VsHas<TKey, TValue>(Dictionary<TKey, TValue> value, TKey key) where TKey : notnull => value.ContainsKey(key);
	private static bool VsHas<TKey, TValue>(VsOrderedMap<TKey, TValue> value, TKey key) where TKey : notnull => value.ContainsKey(key);
	private static bool VsHas<TKey, TValue>(VsMultiMap<TKey, TValue> value, TKey key) where TKey : notnull => value.ContainsKey(key);
	private static bool VsHas<TKey, TValue>(VsOrderedMultiMap<TKey, TValue> value, TKey key) where TKey : notnull => value.ContainsKey(key);
	private static bool VsHas<T>(VsMultiSet<T> value, T item) where T : notnull => value.Contains(item);
	private static bool VsHas<T>(VsOrderedMultiSet<T> value, T item) where T : notnull => value.Contains(item);
	private static bool VsIn<T>(T item, HashSet<T> value) => value.Contains(item);
	private static bool VsIn<TKey, TValue>(TKey key, Dictionary<TKey, TValue> value) where TKey : notnull => value.ContainsKey(key);
	private static bool VsIn<TKey, TValue>(TKey key, VsOrderedMap<TKey, TValue> value) where TKey : notnull => value.ContainsKey(key);
	private static bool VsIn<TKey, TValue>(TKey key, VsMultiMap<TKey, TValue> value) where TKey : notnull => value.ContainsKey(key);
	private static bool VsIn<TKey, TValue>(TKey key, VsOrderedMultiMap<TKey, TValue> value) where TKey : notnull => value.ContainsKey(key);
	private static bool VsIn<T>(T item, VsMultiSet<T> value) where T : notnull => value.Contains(item);
	private static bool VsIn<T>(T item, VsOrderedMultiSet<T> value) where T : notnull => value.Contains(item);
	private static HashSet<T> VsAdd<T>(HashSet<T> value, T item) { value.Add(item); return value; }
	private static Dictionary<TKey, TValue> VsAdd<TKey, TValue>(Dictionary<TKey, TValue> value, TKey key, TValue item) where TKey : notnull { value.Add(key, item); return value; }
	private static VsOrderedMap<TKey, TValue> VsAdd<TKey, TValue>(VsOrderedMap<TKey, TValue> value, TKey key, TValue item) where TKey : notnull { value.Add(key, item); return value; }
	private static VsMultiMap<TKey, TValue> VsAdd<TKey, TValue>(VsMultiMap<TKey, TValue> value, TKey key, TValue item) where TKey : notnull { value.Add(key, item); return value; }
	private static VsOrderedMultiMap<TKey, TValue> VsAdd<TKey, TValue>(VsOrderedMultiMap<TKey, TValue> value, TKey key, TValue item) where TKey : notnull { value.Add(key, item); return value; }
	private static VsMultiSet<T> VsAdd<T>(VsMultiSet<T> value, T item) where T : notnull { value.Add(item); return value; }
	private static VsOrderedMultiSet<T> VsAdd<T>(VsOrderedMultiSet<T> value, T item) where T : notnull { value.Add(item); return value; }
	private static long VsOccurrences<T>(VsMultiSet<T> value, T item) where T : notnull => value.Occurrences(item);
	private static long VsOccurrences<T>(VsOrderedMultiSet<T> value, T item) where T : notnull => value.Occurrences(item);
	private static List<TKey> VsKeys<TKey, TValue>(Dictionary<TKey, TValue> value) where TKey : notnull => new(value.Keys);
	private static List<TKey> VsKeys<TKey, TValue>(VsOrderedMap<TKey, TValue> value) where TKey : notnull => value.SnapshotKeys();
	private static List<TKey> VsKeys<TKey, TValue>(VsMultiMap<TKey, TValue> value) where TKey : notnull => value.SnapshotKeys();
	private static List<TKey> VsKeys<TKey, TValue>(VsOrderedMultiMap<TKey, TValue> value) where TKey : notnull => value.SnapshotKeys();
	private static List<TValue> VsValues<TKey, TValue>(Dictionary<TKey, TValue> value) where TKey : notnull => new(value.Values);
	private static List<TValue> VsValues<TKey, TValue>(VsOrderedMap<TKey, TValue> value) where TKey : notnull => value.SnapshotValues();
	private static List<List<TValue>> VsValues<TKey, TValue>(VsMultiMap<TKey, TValue> value) where TKey : notnull => value.SnapshotValues();
	private static List<List<TValue>> VsValues<TKey, TValue>(VsOrderedMultiMap<TKey, TValue> value) where TKey : notnull => value.SnapshotValues();
	private static List<T> VsValues<T>(VsMultiSet<T> value) where T : notnull => value.SnapshotValues();
	private static List<T> VsValues<T>(VsOrderedMultiSet<T> value) where T : notnull => value.SnapshotValues();
	private static List<T> VsValues<T>(VsPriorityQueue<T> value) => value.SnapshotValues();
	private static List<T> VsValues<T>(HashSet<T> value) => new(value);
	private static List<T> VsValues<T>(Queue<T> value) => new(value);
	private static List<T> VsValues<T>(Stack<T> value)
	{
		List<T> result = new(value);
		result.Reverse();
		return result;
	}
	private static List<T> VsValues<T>(List<T> value) => new(value);
	private static List<VsPair<TKey, TValue>> VsEntries<TKey, TValue>(Dictionary<TKey, TValue> value) where TKey : notnull
	{
		List<VsPair<TKey, TValue>> result = new(value.Count);
		foreach (KeyValuePair<TKey, TValue> entry in value)
			result.Add(new VsPair<TKey, TValue>(entry.Key, entry.Value));
		return result;
	}
	private static List<VsPair<TKey, TValue>> VsEntries<TKey, TValue>(VsOrderedMap<TKey, TValue> value) where TKey : notnull => value.SnapshotEntries();
	private static List<VsPair<TKey, List<TValue>>> VsEntries<TKey, TValue>(VsMultiMap<TKey, TValue> value) where TKey : notnull => value.SnapshotEntries();
	private static List<VsPair<TKey, List<TValue>>> VsEntries<TKey, TValue>(VsOrderedMultiMap<TKey, TValue> value) where TKey : notnull => value.SnapshotEntries();
	private static List<T> VsReserve<T>(List<T> value, long count) { value.EnsureCapacity(VsNativeSize(count)); return value; }
	private static Dictionary<TKey, TValue> VsReserve<TKey, TValue>(Dictionary<TKey, TValue> value, long count) where TKey : notnull { value.EnsureCapacity(VsNativeSize(count)); return value; }
	private static VsOrderedMap<TKey, TValue> VsReserve<TKey, TValue>(VsOrderedMap<TKey, TValue> value, long count) where TKey : notnull { value.EnsureCapacity(VsNativeSize(count)); return value; }
	private static VsMultiMap<TKey, TValue> VsReserve<TKey, TValue>(VsMultiMap<TKey, TValue> value, long count) where TKey : notnull { value.EnsureCapacity(VsNativeSize(count)); return value; }
	private static VsOrderedMultiMap<TKey, TValue> VsReserve<TKey, TValue>(VsOrderedMultiMap<TKey, TValue> value, long count) where TKey : notnull { value.EnsureCapacity(VsNativeSize(count)); return value; }
	private static VsMultiSet<T> VsReserve<T>(VsMultiSet<T> value, long count) where T : notnull { value.EnsureCapacity(VsNativeSize(count)); return value; }
	private static VsOrderedMultiSet<T> VsReserve<T>(VsOrderedMultiSet<T> value, long count) where T : notnull { value.EnsureCapacity(VsNativeSize(count)); return value; }
	private static HashSet<T> VsReserve<T>(HashSet<T> value, long count) { value.EnsureCapacity(VsNativeSize(count)); return value; }
	private static long VsCapacity<T>(List<T> value) => value.Capacity;

	private static IEnumerable<T> VsIterateArray<T>(List<T> value)
	{
		int length = value.Count;
		for (int index = 0; index < length; ++index) yield return value[index];
	}
	private static List<T> VsSnapshot<T>(List<T> value) => new(value);
	private static List<T> VsSnapshot<T>(HashSet<T> value) => new(value);
	private static List<T> VsSnapshot<T>(Queue<T> value) => new(value);
	private static List<T> VsSnapshot<T>(Stack<T> value) => VsValues(value);
	private static List<T> VsSnapshot<T>(VsPriorityQueue<T> value) => value.SnapshotValues();
	private static List<T> VsSnapshot<T>(VsMultiSet<T> value) where T : notnull => value.SnapshotValues();
	private static List<T> VsSnapshot<T>(VsOrderedMultiSet<T> value) where T : notnull => value.SnapshotValues();
	private static IEnumerable<TValue> VsSnapshot<TKey, TValue>(Dictionary<TKey, TValue> value) where TKey : notnull
	{
		List<TKey> keys = new(value.Keys);
		foreach (TKey key in keys) yield return value[key];
	}
	private static IEnumerable<TValue> VsSnapshot<TKey, TValue>(VsOrderedMap<TKey, TValue> value) where TKey : notnull
	{
		List<TKey> keys = value.SnapshotKeys();
		foreach (TKey key in keys) yield return value[key];
	}
	private static IEnumerable<List<TValue>> VsSnapshot<TKey, TValue>(VsMultiMap<TKey, TValue> value) where TKey : notnull => value.SnapshotValues();
	private static IEnumerable<List<TValue>> VsSnapshot<TKey, TValue>(VsOrderedMultiMap<TKey, TValue> value) where TKey : notnull => value.SnapshotValues();
	private static List<VsPair<TKey, TValue>> VsSnapshotEntries<TKey, TValue>(Dictionary<TKey, TValue> value) where TKey : notnull => VsEntries(value);
	private static List<VsPair<TKey, TValue>> VsSnapshotEntries<TKey, TValue>(VsOrderedMap<TKey, TValue> value) where TKey : notnull => value.SnapshotEntries();
	private static List<VsPair<TKey, TValue>> VsSnapshotEntries<TKey, TValue>(List<VsPair<TKey, TValue>> value) where TKey : notnull => new(value);

)CS";
				Out << R"CS(
	private static int VsParseFormatDecimal(string text, ref int cursor, string error)
	{
		if (cursor >= text.Length || !char.IsAsciiDigit(text[cursor]))
			throw new InvalidOperationException(error);
		int value = 0;
		while (cursor < text.Length && char.IsAsciiDigit(text[cursor]))
		{
			int digit = text[cursor] - '0';
			if (value > (1024 - digit) / 10) throw new InvalidOperationException(error);
			value = value * 10 + digit;
			++cursor;
		}
		return value;
	}
	private static string VsApplyFormat(string value, char align, int width, bool hasPrecision, int precision, char typeCode)
	{
		if (typeCode is 'x' or 'X')
		{
			ulong bits;
			if (value.StartsWith("-", StringComparison.Ordinal))
			{
				if (!VsTryParsePrintfSigned(value, out long parsed))
					throw new InvalidOperationException("format: {:x} requires integer value");
				bits = unchecked((ulong)parsed);
			}
			else if (!VsTryParsePrintfUnsigned(value, out bits))
				throw new InvalidOperationException("format: {:x} requires integer value");
			value = bits.ToString(typeCode == 'X' ? "X" : "x", CultureInfo.InvariantCulture);
		}
		else if (hasPrecision)
		{
			if (!VsTryParseDecimal(value, out double parsed))
				throw new InvalidOperationException("format: precision requires numeric value");
			value = parsed.ToString("F" + precision.ToString(CultureInfo.InvariantCulture), CultureInfo.InvariantCulture);
		}
		long length = VsUtf8ByteCount(value);
		if (width <= length) return value;
		int padding = VsNativeSize(width - length);
		if (align == '<') return value + new string(' ', padding);
		if (align == '^')
		{
			int left = padding / 2;
			return new string(' ', left) + value + new string(' ', padding - left);
		}
		return new string(' ', padding) + value;
	}
	private static VsString VsFormatCore(VsString format, IReadOnlyList<string> arguments)
	{
		string text = format.Value;
		StringBuilder result = new(text.Length);
		int automaticIndex = 0;
		for (int index = 0; index < text.Length;)
		{
			char character = text[index];
			if (character == '{')
			{
				if (index + 1 >= text.Length) throw new InvalidOperationException("format: trailing '{'");
				if (text[index + 1] == '{') { result.Append('{'); index += 2; continue; }
				int cursor = index + 1;
				int argumentIndex = automaticIndex;
				bool explicitIndex = false;
				if (cursor < text.Length && char.IsAsciiDigit(text[cursor]))
				{
					argumentIndex = VsParseFormatDecimal(text, ref cursor, "format: argument index out of range");
					explicitIndex = true;
				}
				char align = '>';
				int width = 0;
				bool hasPrecision = false;
				int precision = 0;
				char typeCode = '\0';
				if (cursor < text.Length && text[cursor] == ':')
				{
					++cursor;
					if (cursor < text.Length && text[cursor] is '<' or '>' or '^') align = text[cursor++];
					if (cursor < text.Length && char.IsAsciiDigit(text[cursor]))
						width = VsParseFormatDecimal(text, ref cursor, "format: width out of range");
					if (cursor < text.Length && text[cursor] == '.')
					{
						++cursor;
						precision = VsParseFormatDecimal(text, ref cursor, "format: precision out of range");
						hasPrecision = true;
					}
					if (cursor < text.Length && text[cursor] is 'x' or 'X') typeCode = text[cursor++];
				}
				if (cursor >= text.Length || text[cursor] != '}')
					throw new InvalidOperationException("format: invalid placeholder");
				if (argumentIndex >= arguments.Count)
					throw new InvalidOperationException("format: not enough arguments");
				result.Append(VsApplyFormat(arguments[argumentIndex], align, width, hasPrecision, precision, typeCode));
				if (!explicitIndex) ++automaticIndex;
				index = cursor + 1;
				continue;
			}
			if (character == '}')
			{
				if (index + 1 < text.Length && text[index + 1] == '}')
				{ result.Append('}'); index += 2; continue; }
				throw new InvalidOperationException("format: unmatched '}'");
			}
			result.Append(character);
			++index;
		}
		return new VsString(result.ToString());
	}
	private static VsString VsFormat(VsString format, params object?[] values)
		=> VsFormatCore(format, Array.ConvertAll(values, VsText));
	private static VsString VsFormat(VsString format, List<VsString> values)
	{
		string[] text = new string[values.Count];
		for (int index = 0; index < values.Count; ++index) text[index] = values[index].Value;
		return VsFormatCore(format, text);
	}
	private static void VsPrintFormat(VsString format, params object?[] values) => VsPrint(VsFormat(format, values));
	private static void VsPrintFormat(VsString format, List<VsString> values) => VsPrint(VsFormat(format, values));

)CS";
				Out << R"CS(
	private static bool VsIntegerGrammar(string text, bool allowMinus)
	{
		if (text.Length == 0) return false;
		int cursor = text[0] == '+' || (allowMinus && text[0] == '-') ? 1 : 0;
		if (cursor == text.Length) return false;
		for (; cursor < text.Length; ++cursor)
			if (text[cursor] < '0' || text[cursor] > '9') return false;
		return true;
	}
	private static bool VsTryParseDecimal(string text, out double value)
	{
		value = 0;
		if (text.Length == 0) return false;
		int cursor = text[0] is '+' or '-' ? 1 : 0;
		int digits = 0;
		bool nonzero = false;
		while (cursor < text.Length && text[cursor] >= '0' && text[cursor] <= '9')
		{ nonzero |= text[cursor++] != '0'; ++digits; }
		if (cursor < text.Length && text[cursor] == '.')
		{
			++cursor;
			while (cursor < text.Length && text[cursor] >= '0' && text[cursor] <= '9')
			{ nonzero |= text[cursor++] != '0'; ++digits; }
		}
		if (digits == 0) return false;
		if (cursor < text.Length && text[cursor] is 'e' or 'E')
		{
			++cursor;
			if (cursor < text.Length && text[cursor] is '+' or '-') ++cursor;
			int start = cursor;
			while (cursor < text.Length && text[cursor] >= '0' && text[cursor] <= '9') ++cursor;
			if (cursor == start) return false;
		}
		return cursor == text.Length
			&& double.TryParse(text, NumberStyles.AllowLeadingSign | NumberStyles.AllowDecimalPoint | NumberStyles.AllowExponent,
				CultureInfo.InvariantCulture, out value)
			&& double.IsFinite(value) && (value != 0 || !nonzero);
	}
	private static bool VsTryParsePrintfSigned(string text, out long value)
	{
		value = 0;
		return VsIntegerGrammar(text, true)
			&& long.TryParse(text, NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out value);
	}
	private static bool VsTryParsePrintfUnsigned(string text, out ulong value)
	{
		value = 0;
		return VsIntegerGrammar(text, false)
			&& ulong.TryParse(text, NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out value);
	}
	private static string VsPrintfUnsignedDigits(ulong value, int radix, bool uppercase)
	{
		const string lower = "0123456789abcdef";
		const string upper = "0123456789ABCDEF";
		string alphabet = uppercase ? upper : lower;
		Span<char> buffer = stackalloc char[64];
		int cursor = buffer.Length;
		do
		{
			buffer[--cursor] = alphabet[(int)(value % (ulong)radix)];
			value /= (ulong)radix;
		}
		while (value != 0);
		return new string(buffer[cursor..]);
	}
	private static int VsPrintfExponentIndex(string value)
	{
		int lower = value.IndexOf('e');
		return lower >= 0 ? lower : value.IndexOf('E');
	}
	private static string VsPrintfTrimFraction(string value)
	{
		int exponent = VsPrintfExponentIndex(value);
		int end = exponent >= 0 ? exponent : value.Length;
		int point = value.IndexOf('.');
		if (point < 0 || point >= end) return value;
		while (end > point + 1 && value[end - 1] == '0') --end;
		if (end == point + 1) --end;
		return exponent >= 0 ? value[..end] + value[exponent..] : value[..end];
	}
	private static string VsPrintfNormalizeExponent(string value)
	{
		int exponent = VsPrintfExponentIndex(value);
		if (exponent < 0 || exponent + 2 >= value.Length) return value;
		int digits = exponent + 2;
		while (value.Length - digits > 2 && value[digits] == '0') ++digits;
		return digits == exponent + 2 ? value : value[..(exponent + 2)] + value[digits..];
	}
	private static int VsPrintfScientificExponent(string value)
	{
		int exponent = VsPrintfExponentIndex(value);
		if (exponent < 0
			|| !int.TryParse(value[(exponent + 1)..], NumberStyles.AllowLeadingSign,
				CultureInfo.InvariantCulture, out int result))
			throw new InvalidOperationException("formatf: conversion failed");
		return result;
	}
	private static string VsPrintfFloat(double value, char conversion, int precision, bool alternate)
	{
		bool uppercase = char.IsUpper(conversion);
		char lower = char.ToLowerInvariant(conversion);
		string formatted;
		if (lower == 'f')
		{
			formatted = value.ToString("F" + precision.ToString(CultureInfo.InvariantCulture), CultureInfo.InvariantCulture);
			if (alternate && precision == 0) formatted += ".";
			return uppercase ? formatted.ToUpperInvariant() : formatted;
		}
		if (lower == 'e')
		{
			formatted = value.ToString((uppercase ? "E" : "e")
				+ precision.ToString(CultureInfo.InvariantCulture), CultureInfo.InvariantCulture);
			formatted = VsPrintfNormalizeExponent(formatted);
			if (alternate && precision == 0)
			{
				int exponent = VsPrintfExponentIndex(formatted);
				formatted = formatted.Insert(exponent, ".");
			}
			return formatted;
		}

		int significant = precision == 0 ? 1 : precision;
		string scientificValue = value.ToString((uppercase ? "E" : "e")
			+ (significant - 1).ToString(CultureInfo.InvariantCulture), CultureInfo.InvariantCulture);
		scientificValue = VsPrintfNormalizeExponent(scientificValue);
		int decimalExponent = VsPrintfScientificExponent(scientificValue);
		bool scientific = decimalExponent < -4 || decimalExponent >= significant;
		if (scientific)
		{
			formatted = scientificValue;
			if (alternate && significant == 1)
			{
				int exponent = VsPrintfExponentIndex(formatted);
				formatted = formatted.Insert(exponent, ".");
			}
		}
		else
		{
			int fractional = Math.Max(0, significant - decimalExponent - 1);
			formatted = value.ToString("F" + fractional.ToString(CultureInfo.InvariantCulture), CultureInfo.InvariantCulture);
			if (alternate && fractional == 0) formatted += ".";
		}
		return alternate ? formatted : VsPrintfTrimFraction(formatted);
	}
	private static string VsPrintfApplyWidth(
		string value, int width, bool left, bool zero, int prefixLength)
	{
		long length = VsUtf8ByteCount(value);
		if (width <= length) return value;
		string padding = new(zero ? '0' : ' ', VsNativeSize(width - length));
		if (left) return value + padding;
		return zero ? value.Insert(Math.Min(prefixLength, value.Length), padding) : padding + value;
	}
	private static string VsPrintfTruncateUtf8(string value, int byteCount)
	{
		byte[] bytes = VsUtf8Encode(value);
		if (bytes.Length <= byteCount) return value;
		return VsUtf8Decode(bytes.AsSpan(0, byteCount));
	}
	private static VsString VsFormatPrintfCore(VsString format, IReadOnlyList<string> arguments)
	{
		string text = format.Value;
		StringBuilder result = new(text.Length);
		int argumentIndex = 0;
		for (int index = 0; index < text.Length;)
		{
			if (text[index] != '%') { result.Append(text[index++]); continue; }
			if (index + 1 >= text.Length) throw new InvalidOperationException("formatf: trailing '%'");
			if (text[index + 1] == '%') { result.Append('%'); index += 2; continue; }

			int cursor = index + 1;
			bool left = false, plus = false, space = false, alternate = false, zero = false;
			while (cursor < text.Length)
			{
				switch (text[cursor])
				{
					case '-': left = true; ++cursor; continue;
					case '+': plus = true; ++cursor; continue;
					case ' ': space = true; ++cursor; continue;
					case '#': alternate = true; ++cursor; continue;
					case '0': zero = true; ++cursor; continue;
				}
				break;
			}
			if (cursor < text.Length && text[cursor] == '*')
				throw new InvalidOperationException("formatf: dynamic width '*' is not supported");
			int width = 0;
			if (cursor < text.Length && char.IsAsciiDigit(text[cursor]))
				width = VsParseFormatDecimal(text, ref cursor, "formatf: width out of range");
			bool hasPrecision = false;
			int precision = 0;
			if (cursor < text.Length && text[cursor] == '.')
			{
				hasPrecision = true;
				++cursor;
				if (cursor < text.Length && text[cursor] == '*')
					throw new InvalidOperationException("formatf: dynamic precision '*' is not supported");
				precision = VsParseFormatDecimal(text, ref cursor, "formatf: precision out of range");
			}
			if (cursor < text.Length && text[cursor] is 'h' or 'l')
			{
				char modifier = text[cursor++];
				if (cursor < text.Length && text[cursor] == modifier) ++cursor;
			}
			else if (cursor < text.Length && text[cursor] is 'j' or 'z' or 't') ++cursor;
			if (cursor >= text.Length) throw new InvalidOperationException("formatf: incomplete conversion");
			char conversion = text[cursor++];
			if ("diuoxXfFeEgGcs".IndexOf(conversion) < 0)
				throw new InvalidOperationException("formatf: unsupported conversion '%" + conversion + "'");
			if (conversion is 'c' or 's' && (plus || space || alternate || zero))
				throw new InvalidOperationException("formatf: only '-' and width are valid flags for %c/%s");
			if (conversion == 'c' && hasPrecision)
				throw new InvalidOperationException("formatf: precision is not valid for %c");
			if (argumentIndex >= arguments.Count)
				throw new InvalidOperationException("formatf: not enough arguments");

			string argument = arguments[argumentIndex++];
			string formatted;
			int prefixLength = 0;
			bool numeric = false;
			bool integerConversion = false;
			if (conversion is 'd' or 'i')
			{
				if (!VsTryParsePrintfSigned(argument, out long signed))
					throw new InvalidOperationException("formatf: integer conversion requires an integer value");
				bool negative = signed < 0;
				ulong magnitude = negative ? unchecked(0UL - (ulong)signed) : (ulong)signed;
				string digits = VsPrintfUnsignedDigits(magnitude, 10, false);
				if (hasPrecision && precision == 0 && magnitude == 0) digits = string.Empty;
				if (hasPrecision && digits.Length < precision) digits = new string('0', precision - digits.Length) + digits;
				string sign = negative ? "-" : plus ? "+" : space ? " " : string.Empty;
				formatted = sign + digits;
				prefixLength = sign.Length;
				numeric = integerConversion = true;
			}
			else if (conversion is 'u' or 'o' or 'x' or 'X')
			{
				if (!VsTryParsePrintfUnsigned(argument, out ulong unsigned))
					throw new InvalidOperationException("formatf: unsigned conversion requires a non-negative integer");
				int radix = conversion == 'o' ? 8 : conversion is 'x' or 'X' ? 16 : 10;
				string digits = VsPrintfUnsignedDigits(unsigned, radix, conversion == 'X');
				if (hasPrecision && precision == 0 && unsigned == 0) digits = string.Empty;
				if (hasPrecision && digits.Length < precision) digits = new string('0', precision - digits.Length) + digits;
				string prefix = alternate && unsigned != 0 && conversion is 'x' or 'X'
					? (conversion == 'X' ? "0X" : "0x")
					: alternate && conversion == 'o' && !digits.StartsWith('0') ? "0" : string.Empty;
				formatted = prefix + digits;
				prefixLength = prefix.Length;
				numeric = integerConversion = true;
			}
			else if (conversion is 'f' or 'F' or 'e' or 'E' or 'g' or 'G')
			{
				if (!VsTryParseDecimal(argument, out double number))
					throw new InvalidOperationException("formatf: floating conversion requires a finite numeric value");
				formatted = VsPrintfFloat(number, conversion, hasPrecision ? precision : 6, alternate);
				if (!formatted.StartsWith('-'))
					formatted = plus ? "+" + formatted : space ? " " + formatted : formatted;
				prefixLength = formatted.StartsWith('-') || formatted.StartsWith('+') || formatted.StartsWith(' ') ? 1 : 0;
				numeric = true;
			}
			else if (conversion == 'c')
			{
				if (VsTryParsePrintfSigned(argument, out long code))
				{
					if (code < 0 || code > 255)
						throw new InvalidOperationException("formatf: %c value is outside byte range");
					formatted = code < 128
						? ((char)code).ToString()
						: ((char)('\uDC00' + code)).ToString();
				}
				else
				{
					if (VsUtf8ByteCount(argument) != 1)
						throw new InvalidOperationException("formatf: %c requires an integer or one-byte string");
					formatted = argument;
				}
			}
			else formatted = hasPrecision ? VsPrintfTruncateUtf8(argument, precision) : argument;

			bool allowZero = zero && numeric && !left && (!hasPrecision || !integerConversion);
			result.Append(VsPrintfApplyWidth(formatted, width, left, allowZero, prefixLength));
			index = cursor;
		}
		return new VsString(result.ToString());
	}
	private static VsString VsFormatPrintf(VsString format, params object?[] values)
		=> VsFormatPrintfCore(format, Array.ConvertAll(values, VsText));
	private static VsString VsFormatPrintf(VsString format, List<VsString> values)
	{
		string[] text = new string[values.Count];
		for (int index = 0; index < values.Count; ++index) text[index] = values[index].Value;
		return VsFormatPrintfCore(format, text);
	}
	private static void VsPrintfFormat(VsString format, params object?[] values) => VsPrint(VsFormatPrintf(format, values));
	private static void VsPrintfFormat(VsString format, List<VsString> values) => VsPrint(VsFormatPrintf(format, values));
	private static void VsAssert(bool condition, VsString message = default)
	{
		if (!condition) throw new InvalidOperationException(message.Value ?? "assertion failed");
	}
	private static int VsExit(int code) => throw new VsExitException(code);

)CS";
				if (!bHasStandardEventDelegate)
				{
					return;
				}
				for (int Arity = 0; Arity <= 3; ++Arity)
				{
					std::string TypeParameters;
					std::string Parameters;
					std::string Arguments;
					for (int Index = 0; Index < Arity; ++Index)
					{
						if (Index != 0)
						{
							TypeParameters += ", ";
							Parameters += ", ";
							Arguments += ", ";
						}
						const char Parameter = static_cast<char>('A' + Index);
						TypeParameters += Parameter;
						Parameters += Parameter;
						Parameters += ' ';
						Parameters += static_cast<char>('a' + Index);
						Arguments += static_cast<char>('a' + Index);
					}
					const std::string Generic = Arity == 0 ? std::string{}
					                                       : '<' + TypeParameters + '>';
					const std::string ActionType = "Action" + Generic;
					Out << "\tprivate sealed class VsVoidDelegate" << Arity << Generic << "\n"
					    << "\t{\n"
					    << "\t\tpublic void bind(" << ActionType << " h)\n"
					    << "\t\t{\n"
					    << "\t\t\t_handler = h;\n"
					    << "\t\t\t_bound = true;\n"
					    << "\t\t}\n"
					    << "\t\tpublic void unbind()\n"
					    << "\t\t{\n"
					    << "\t\t\t_handler = null;\n"
					    << "\t\t\t_bound = false;\n"
					    << "\t\t}\n"
					    << "\t\tpublic bool is_bound() => _bound;\n"
					    << "\t\tpublic void execute(" << Parameters << ")\n"
					    << "\t\t{\n"
					    << "\t\t\tif (!_bound)\n"
					    << "\t\t\t{\n"
					    << "\t\t\t\tthrow new InvalidOperationException(\"delegate is not bound\");\n"
					    << "\t\t\t}\n"
					    << "\t\t\tvar callback = _handler;\n"
					    << "\t\t\tcallback!(" << Arguments << ");\n"
					    << "\t\t}\n"
					    << "\t\tpublic bool execute_if_bound(" << Parameters << ")\n"
					    << "\t\t{\n"
					    << "\t\t\tif (!_bound) return false;\n"
					    << "\t\t\tvar callback = _handler;\n"
					    << "\t\t\tcallback!(" << Arguments << ");\n"
					    << "\t\t\treturn true;\n"
					    << "\t\t}\n"
					    << "\t\tprivate " << ActionType << "? _handler;\n"
					    << "\t\tprivate bool _bound;\n"
					    << "\t}\n";
				}
			}

			void EmitLine(const FSourceLocation& Location)
			{
				ActiveLocation = Location;
				if (Location.FileLabel.empty())
				{
					return;
				}
				// A quoted #line file makes Roslyn's portable-PDB source embedding try
				// to open in-memory labels such as "<memory>". Keep the generated TU
				// self-contained while retaining both the logical label and line.
				Out << "// VeroScript source: " << EscapeCSharpString(Location.FileLabel)
				    << ':' << Location.Line << "\n#line " << Location.Line << "\n";
			}

			void Indent(const int Depth)
			{
				for (int Index = 0; Index < Depth; ++Index)
				{
					Out << "\t";
				}
			}

			bool IsVisibleFromActiveModule(
			    const std::string& ModulePath,
			    const bool bExported) const
			{
				return ModuleVisibility::CanAccessSymbol(
				    *Program, ModulePath, bExported, ActiveModulePath);
			}

			std::string FunctionIdentifier(const FFunctionDecl& Function) const
			{
				std::string Result = "Vs_" + SymbolIdentifier(CanonicalSymbol(Function.Name));
				if (!Function.bExported)
				{
					Result += "_M_";
					Result += EncodeModuleSuffix(Function.ModulePath);
				}
				for (std::size_t Index = 0; Index < Function.Parameters.size(); ++Index)
				{
					const FFunctionParam& Parameter = Function.Parameters[Index];
					if (!Parameter.bIsRef)
					{
						continue;
					}
					Result += Parameter.bIsConst ? "_CRef_" : "_Ref_";
					Result += std::to_string(Index);
				}
				return Result;
			}

			std::string FunctionIdentifierForSymbol(const std::string& Symbol) const
			{
				const auto Found = FunctionDecls.find(Symbol);
				if (Found != FunctionDecls.end())
				{
					const FFunctionDecl* Selected = nullptr;
					for (const FFunctionDecl* Function : Found->second)
					{
						if (!Function || !IsVisibleFromActiveModule(Function->ModulePath, Function->bExported))
						{
							continue;
						}
						if (Function->ModulePath == ActiveModulePath)
						{
							return FunctionIdentifier(*Function);
						}
						if (!Selected)
						{
							Selected = Function;
						}
					}
					if (Selected)
					{
						return FunctionIdentifier(*Selected);
					}
				}
				return "Vs_" + SymbolIdentifier(Symbol);
			}

			const FVarDeclStmt* FindGlobalExact(const std::string& Name) const
			{
				const FVarDeclStmt* Selected = nullptr;
				for (const FVarDeclStmt* Global : Globals)
				{
					if (!Global || CanonicalSymbol(Global->Name) != Name || !IsVisibleFromActiveModule(Global->ModulePath, Global->bExported))
					{
						continue;
					}
					if (Global->ModulePath == ActiveModulePath)
					{
						return Global;
					}
					if (!Selected)
					{
						Selected = Global;
					}
				}
				return Selected;
			}

			const FVarDeclStmt* ResolveGlobal(const std::string& Name) const
			{
				std::string CanonicalName = CanonicalSymbol(Name);
				const bool bGlobalQualified = CanonicalName.rfind("::", 0) == 0;
				if (bGlobalQualified)
				{
					CanonicalName.erase(0, 2);
				}
				if (!bGlobalQualified && CanonicalName.find("::") == std::string::npos && !ActiveNamespace.empty())
				{
					std::string Scope = ActiveNamespace;
					while (!Scope.empty())
					{
						if (const FVarDeclStmt* Global =
						        FindGlobalExact(Scope + "::" + CanonicalName))
						{
							return Global;
						}
						const std::size_t Separator = Scope.rfind("::");
						Scope = Separator == std::string::npos
						    ? std::string()
						    : Scope.substr(0, Separator);
					}
				}
				if (const FVarDeclStmt* Global = FindGlobalExact(CanonicalName))
				{
					return Global;
				}
				if (bGlobalQualified || CanonicalName.find("::") != std::string::npos)
				{
					return nullptr;
				}
				for (const FUsingNamespaceDecl& Directive : Program->UsingNamespaces)
				{
					if (!ShouldEmit(Directive.ModulePath))
					{
						continue;
					}
					if (!IsUsingDirectiveVisible(Directive))
					{
						continue;
					}
					for (const std::string& Target : UsingNamespaceTargets(Directive))
					{
						if (const FVarDeclStmt* Global =
						        FindGlobalExact(Target + "::" + CanonicalName))
						{
							return Global;
						}
					}
				}
				return nullptr;
			}

			static std::string GlobalIdentifier(const FVarDeclStmt& Global)
			{
				std::string Result = "VsGlobal_" + SymbolIdentifier(CanonicalSymbol(Global.Name));
				if (!Global.bExported)
				{
					Result += "_M_";
					Result += EncodeModuleSuffix(Global.ModulePath);
				}
				return Result;
			}

			std::string ResolveEnumMember(const std::string& Name) const
			{
				const std::string CanonicalName = CanonicalSymbol(Name);
				if (const auto Found = QualifiedEnumMembers.find(CanonicalName);
				    Found != QualifiedEnumMembers.end())
				{
					return Found->second;
				}
				if (!ActiveNamespace.empty())
				{
					std::string Scope = ActiveNamespace;
					while (!Scope.empty())
					{
						const std::string Candidate = Scope + "::" + CanonicalName;
						if (const auto Found = QualifiedEnumMembers.find(Candidate);
						    Found != QualifiedEnumMembers.end())
						{
							return Found->second;
						}
						if (const auto Found = InjectedEnumMembers.find(Candidate);
						    Found != InjectedEnumMembers.end())
						{
							return Found->second;
						}
						const std::size_t Separator = Scope.rfind("::");
						if (Separator == std::string::npos)
						{
							break;
						}
						Scope.erase(Separator);
					}
				}
				if (const auto Found = InjectedEnumMembers.find(CanonicalName);
				    Found != InjectedEnumMembers.end())
				{
					return Found->second;
				}

				if (CanonicalName.find("::") == std::string::npos)
				{
					for (const FUsingNamespaceDecl& Directive : Program->UsingNamespaces)
					{
						if (!ShouldEmit(Directive.ModulePath))
						{
							continue;
						}
						if (!IsUsingDirectiveVisible(Directive))
						{
							continue;
						}
						for (const std::string& Target : UsingNamespaceTargets(Directive))
						{
							const auto Found = InjectedEnumMembers.find(Target + "::" + CanonicalName);
							if (Found != InjectedEnumMembers.end())
							{
								return Found->second;
							}
						}
					}
				}
				else
				{
					for (const FUsingNamespaceDecl& Directive : Program->UsingNamespaces)
					{
						if (!ShouldEmit(Directive.ModulePath))
						{
							continue;
						}
						if (!IsUsingDirectiveVisible(Directive))
						{
							continue;
						}
						for (const std::string& Target : UsingNamespaceTargets(Directive))
						{
							const auto Found = QualifiedEnumMembers.find(Target + "::" + CanonicalName);
							if (Found != QualifiedEnumMembers.end())
							{
								return Found->second;
							}
						}
					}
				}
				return {};
			}

			std::string ResolveEnumMemberType(const std::string& Name) const
			{
				const std::string Emitted = ResolveEnumMember(Name);
				if (const auto Found = EmittedEnumMemberTypes.find(Emitted);
				    Found != EmittedEnumMemberTypes.end())
				{
					return Found->second;
				}
				return {};
			}

			bool SplitGenericType(
			    std::string Type,
			    std::string& OutBase,
			    std::vector<std::string>& OutArgs) const
			{
				Type = Trim(ResolveAlias(Trim(std::move(Type))));
				if (Type.size() > 2 && Type.compare(Type.size() - 2, 2, "[]") == 0)
				{
					OutBase = "array";
					OutArgs = {ResolveAlias(Trim(Type.substr(0, Type.size() - 2)))};
					return true;
				}
				const std::size_t Open = Type.find('<');
				if (Open == std::string::npos || Type.back() != '>')
				{
					return false;
				}
				OutBase = Trim(Type.substr(0, Open));
				OutArgs = SplitTypeArguments(
				    std::string_view(Type).substr(Open + 1, Type.size() - Open - 2));
				for (std::string& Arg : OutArgs)
				{
					Arg = ResolveAlias(Trim(std::move(Arg)));
				}
				return true;
			}

			std::string ResolveFunctionSymbol(const std::string& Name) const
			{
				const std::string CanonicalName = CanonicalSymbol(Name);
				if (CanonicalName.rfind("::", 0) == 0) return CanonicalName.substr(2);
				std::string Resolved = ResolveInNamespace(CanonicalName, FunctionNames);
				if (Resolved == CanonicalName && CanonicalName.find("::") == std::string::npos)
				{
					const std::vector<std::string> Imported =
					    ResolveUsingSymbols(CanonicalName, FunctionNames);
					if (Imported.size() == 1)
					{
						Resolved = Imported.front();
					}
				}
				return Resolved;
			}

			std::string ResolveUserTypeSymbol(const std::string& Type) const
			{
				std::string Base;
				std::vector<std::string> Args;
				if (!SplitGenericType(Type, Base, Args))
				{
					Base = Type;
				}
				const std::string Canonical = CanonicalSymbol(Base);
				std::string Resolved = ResolveInNamespace(Canonical, UserTypeNames);
				if (Resolved == Canonical && Canonical.find("::") == std::string::npos)
				{
					const std::vector<std::string> Imported = ResolveUsingSymbols(Canonical, UserTypeNames);
					if (Imported.size() == 1)
					{
						Resolved = Imported.front();
					}
				}
				return UserTypeNames.find(Resolved) == UserTypeNames.end() ? std::string() : Resolved;
			}

			std::string SubstituteUserTypeArguments(
			    const std::string& OwnerType,
			    std::string Type) const
			{
				std::string OwnerBase;
				std::vector<std::string> OwnerArguments;
				if (!SplitGenericType(ResolveAlias(OwnerType), OwnerBase, OwnerArguments))
				{
					OwnerBase = ResolveAlias(OwnerType);
				}
				const std::string Owner = ResolveUserTypeSymbol(OwnerBase);
				std::unordered_map<std::string, std::string> Arguments;
				const auto Collect = [&](const auto& Decl)
				{
					if (CanonicalSymbol(Decl.Name) != Owner || Decl.TypeParams.size() != OwnerArguments.size())
					{
						return;
					}
					for (std::size_t Index = 0; Index < Decl.TypeParams.size(); ++Index)
					{
						Arguments.emplace(Decl.TypeParams[Index], OwnerArguments[Index]);
					}
				};
				for (const FStructDecl& Struct : Program->Structs)
				{
					if (!ShouldEmit(Struct.ModulePath) || AttributeModule(Struct.ModulePath))
					{
						continue;
					}
					Collect(Struct);
				}
				for (const FClassDecl& Class : Program->Classes)
				{
					if (!ShouldEmit(Class.ModulePath))
					{
						continue;
					}
					Collect(Class);
				}
				const bool bReference = Runtime::IsReferenceTypeName(Type);
				const bool bConstReference = bReference && Runtime::IsConstReferenceTypeName(Type);
				if (bReference)
				{
					Type = std::string(Runtime::StripReferenceTypeName(Type));
				}
				Type = SubstituteFunctionTypeArguments(std::move(Type), Arguments);
				if (bReference)
				{
					Type = bConstReference ? "const ref " + Type : "ref " + Type;
				}
				return Type;
			}

			std::size_t UserTypeArity(const std::string& OwnerType) const
			{
				std::string Base;
				std::vector<std::string> Arguments;
				if (OwnerType == ActiveTypeName && !ActiveTypeParameters.empty())
				{
					return ActiveTypeParameters.size();
				}
				return SplitGenericType(ResolveAlias(OwnerType), Base, Arguments)
				    ? Arguments.size()
				    : 0;
			}

			FResolvedUserField ResolveUserField(const std::string& OwnerType,
			    const std::string& Name) const
			{
				const std::string Owner = ResolveUserTypeSymbol(OwnerType);
				const std::size_t Arity = UserTypeArity(OwnerType);
				for (const FStructDecl& Struct : Program->Structs)
				{
					if (!ShouldEmit(Struct.ModulePath) || AttributeModule(Struct.ModulePath))
					{
						continue;
					}
					if (CanonicalSymbol(Struct.Name) == Owner && Struct.TypeParams.size() == Arity)
					{
						for (const FFieldDecl& Field : Struct.Fields)
						{
							if (Field.Name == Name)
							{
								return {&Field, OwnerType};
							}
						}
					}
				}
				for (const FClassDecl& Class : Program->Classes)
				{
					if (!ShouldEmit(Class.ModulePath))
					{
						continue;
					}
					if (CanonicalSymbol(Class.Name) == Owner && Class.TypeParams.size() == Arity)
					{
						for (const FFieldDecl& Field : Class.Fields)
						{
							if (Field.Name == Name)
							{
								return {&Field, OwnerType};
							}
						}
						if (!Class.BaseTypeNames.empty())
						{
							return ResolveUserField(SubstituteUserTypeArguments(
							                            OwnerType, Class.BaseTypeNames.front()),
							    Name);
						}
						return {};
					}
				}
				return {};
			}

			FResolvedUserMethod ResolveUserMethod(const std::string& OwnerType, const std::string& Name,
			    const FExprArgumentView Arguments, const FMethodBindingSelection* Binding = nullptr) const
			{
				const std::string Owner = ResolveUserTypeSymbol(OwnerType);
				const std::size_t Arity = UserTypeArity(OwnerType);
				const FMethodDecl* Best = nullptr;
				int BestScore = -1000;
				bool bTie = false;
				const auto Find = [&](const auto& Decl) -> const FMethodDecl*
				{
					if (CanonicalSymbol(Decl.Name) != Owner || Decl.TypeParams.size() != Arity)
					{
						return nullptr;
					}
					for (const FMethodDecl& Method : Decl.Methods)
					{
						if (Method.Name != Name || Method.bDeleted || (Binding && Method.bIsConst != Binding->bConst))
						{
							continue;
						}
						std::size_t Required = 0;
						for (const FFunctionParam& Parameter : Method.Parameters)
						{
							if (!Parameter.DefaultValue && !Parameter.bIsVariadic)
							{
								++Required;
							}
						}
						const bool bVariadic = !Method.Parameters.empty() && Method.Parameters.back().bIsVariadic;
						if (Arguments.size() < Required || (!bVariadic && Arguments.size() > Method.Parameters.size()))
						{
							continue;
						}
						int Score = 0;
						bool bMatches = true;
						for (std::size_t Index = 0; Index < Arguments.size(); ++Index)
						{
							const std::size_t ParameterIndex = bVariadic
							    ? std::min(Index, Method.Parameters.size() - 1)
							    : Index;
							const FFunctionParam& Parameter = Method.Parameters[ParameterIndex];
							const FExpr* Argument = Arguments[Index];
							const bool bArgumentIsRef = Argument && Argument->Kind == EExprKind::Ref;
							if (bArgumentIsRef != Parameter.bIsRef)
							{
								bMatches = false;
								break;
							}
							bool bArgumentIsConst = false;
							if (bArgumentIsRef)
							{
								const FExpr& Operand = *static_cast<const FRefExpr&>(*Argument).Operand;
								bArgumentIsConst = IsKnownConstRefTarget(Operand);
								if (bArgumentIsConst && !Parameter.bIsConst)
								{
									bMatches = false;
									break;
								}
							}
							const int Match = ParameterMatchScore(SubstituteUserTypeArguments(
							                                          OwnerType, Parameter.TypeName),
							    Argument ? InferExprType(*Argument) : std::string{});
							if (Match < 0)
							{
								bMatches = false;
								break;
							}
							Score += Match + (bArgumentIsRef && bArgumentIsConst == Parameter.bIsConst ? 2 : 0);
						}
						if (!bMatches)
						{
							continue;
						}
						if (Score > BestScore)
						{
							Best = &Method;
							BestScore = Score;
							bTie = false;
						}
						else if (Score == BestScore)
						{
							bTie = true;
						}
					}
					return bTie ? nullptr : Best;
				};
				for (const FStructDecl& Struct : Program->Structs)
				{
					if (!ShouldEmit(Struct.ModulePath) || AttributeModule(Struct.ModulePath))
					{
						continue;
					}
					if (CanonicalSymbol(Struct.Name) == Owner && Struct.TypeParams.size() == Arity)
					{
						return {Find(Struct), OwnerType};
					}
				}
				for (const FClassDecl& Class : Program->Classes)
				{
					if (!ShouldEmit(Class.ModulePath))
					{
						continue;
					}
					if (CanonicalSymbol(Class.Name) == Owner && Class.TypeParams.size() == Arity)
					{
						if (const FMethodDecl* Method = Find(Class))
						{
							return {Method, OwnerType};
						}
						if (!Class.BaseTypeNames.empty())
						{
							return ResolveUserMethod(SubstituteUserTypeArguments(
							                             OwnerType, Class.BaseTypeNames.front()),
							    Name, Arguments, Binding);
						}
						return {};
					}
				}
				return {};
			}

			FResolvedUserConstructor ResolveUserConstructor(
			    const std::string& OwnerType,
			    const FExprArgumentView Arguments) const
			{
				const std::string Owner = ResolveUserTypeSymbol(OwnerType);
				const std::size_t Arity = UserTypeArity(OwnerType);
				const FConstructorDecl* Best = nullptr;
				int BestScore = -1000;
				bool bTie = false;
				const auto Find = [&](const auto& Decl)
				{
					if (CanonicalSymbol(Decl.Name) != Owner || Decl.TypeParams.size() != Arity)
					{
						return;
					}
					for (const FConstructorDecl& Constructor : Decl.Constructors)
					{
						if (Constructor.bDeleted)
						{
							continue;
						}
						std::size_t Required = 0;
						for (const FFunctionParam& Parameter : Constructor.Parameters)
						{
							if (!Parameter.DefaultValue && !Parameter.bIsVariadic)
							{
								++Required;
							}
						}
						const bool bVariadic = !Constructor.Parameters.empty() && Constructor.Parameters.back().bIsVariadic;
						if (Arguments.size() < Required || (!bVariadic && Arguments.size() > Constructor.Parameters.size()))
						{
							continue;
						}
						int Score = 0;
						bool bMatches = true;
						for (std::size_t Index = 0; Index < Arguments.size(); ++Index)
						{
							const std::size_t ParameterIndex = bVariadic
							    ? std::min(Index, Constructor.Parameters.size() - 1)
							    : Index;
							const FFunctionParam& Parameter = Constructor.Parameters[ParameterIndex];
							const FExpr* Argument = Arguments[Index];
							const bool bArgumentIsRef = Argument && Argument->Kind == EExprKind::Ref;
							if (bArgumentIsRef != Parameter.bIsRef)
							{
								bMatches = false;
								break;
							}
							if (bArgumentIsRef)
							{
								const FExpr& Operand = *static_cast<const FRefExpr&>(*Argument).Operand;
								if (IsKnownConstRefTarget(Operand) && !Parameter.bIsConst)
								{
									bMatches = false;
									break;
								}
							}
							const int Match = ParameterMatchScore(SubstituteUserTypeArguments(
							                                          OwnerType, Parameter.TypeName),
							    Argument ? InferExprType(*Argument) : std::string{});
							if (Match < 0)
							{
								bMatches = false;
								break;
							}
							Score += Match;
						}
						if (!bMatches)
						{
							continue;
						}
						if (Score > BestScore)
						{
							Best = &Constructor;
							BestScore = Score;
							bTie = false;
						}
						else if (Score == BestScore)
						{
							bTie = true;
						}
					}
				};
				for (const FStructDecl& Struct : Program->Structs)
				{
					if (!ShouldEmit(Struct.ModulePath) || AttributeModule(Struct.ModulePath))
					{
						continue;
					}
					Find(Struct);
				}
				for (const FClassDecl& Class : Program->Classes)
				{
					if (!ShouldEmit(Class.ModulePath))
					{
						continue;
					}
					Find(Class);
				}
				return {bTie ? nullptr : Best, OwnerType};
			}

			static std::size_t RequiredParameterCount(const FFunctionDecl& Function)
			{
				std::size_t Count = 0;
				for (const FFunctionParam& Parameter : Function.Parameters)
				{
					if (!Parameter.DefaultValue && !Parameter.bIsVariadic)
					{
						++Count;
					}
				}
				return Count;
			}

			int ParameterMatchScore(const std::string& ParameterType,
			    const std::string& ArgumentType) const
			{
				const std::string Parameter = NormalizeType(ParameterType);
				const std::string Argument = NormalizeType(ArgumentType);
				if (Parameter == Argument) return 8;
				if (Parameter.empty() || Argument.empty()) return 1;
				if (Argument == "nullptr")
				{
					std::string Payload;
					if (IsBuiltinHandleType(Parameter, Payload)) return 1;
					const std::string Owner = ResolveUserTypeSymbol(Parameter);
					const std::size_t Arity = UserTypeArity(Parameter);
					for (const auto& Decl : Program->Classes)
					{
						if (!ShouldEmit(Decl.ModulePath))
						{
							continue;
						}
						if (CanonicalSymbol(Decl.Name) == Owner && Decl.TypeParams.size() == Arity) return 1;
					}
					for (const auto& Decl : Program->Interfaces)
					{
						if (!ShouldEmit(Decl.ModulePath))
						{
							continue;
						}
						if (CanonicalSymbol(Decl.Name) == Owner && Decl.TypeParams.size() == Arity) return 1;
					}
				}
				if (IsNumericType(Parameter) && IsNumericType(Argument)) return 3;
				if (EnumTypeNames.find(CanonicalSymbol(Argument)) != EnumTypeNames.end() && IsNumericType(Parameter))
				{
					return 2;
				}
				return -1000;
			}

			int FunctionParameterMatchScore(
			    const FFunctionDecl& Function,
			    const std::string& ParameterType,
			    const std::string& ArgumentType) const
			{
				const std::string Parameter = NormalizeType(ParameterType);
				if (std::find(Function.TypeParams.begin(), Function.TypeParams.end(), Parameter) != Function.TypeParams.end())
				{
					return 5;
				}
				std::string ParameterBase;
				std::string ArgumentBase;
				std::vector<std::string> ParameterArgs;
				std::vector<std::string> ArgumentArgs;
				if (SplitGenericType(Parameter, ParameterBase, ParameterArgs) && SplitGenericType(ArgumentType, ArgumentBase, ArgumentArgs) && ParameterBase == ArgumentBase && ParameterArgs.size() == ArgumentArgs.size())
				{
					int Score = 4;
					for (std::size_t Index = 0; Index < ParameterArgs.size(); ++Index)
					{
						const int Match = FunctionParameterMatchScore(
						    Function, ParameterArgs[Index], ArgumentArgs[Index]);
						if (Match < 0)
						{
							return Match;
						}
						Score += Match;
					}
					return Score;
				}
				return ParameterMatchScore(Parameter, ArgumentType);
			}

			void InferFunctionTypeArguments(
			    const FFunctionDecl& Function,
			    const std::string& ParameterType,
			    const std::string& ArgumentType,
			    std::unordered_map<std::string, std::string>& OutTypes) const
			{
				const std::string Parameter = NormalizeType(ParameterType);
				if (std::find(Function.TypeParams.begin(), Function.TypeParams.end(), Parameter) != Function.TypeParams.end())
				{
					OutTypes.try_emplace(Parameter, ResolveAlias(ArgumentType));
					return;
				}
				std::string ParameterBase;
				std::string ArgumentBase;
				std::vector<std::string> ParameterArgs;
				std::vector<std::string> ArgumentArgs;
				if (!SplitGenericType(Parameter, ParameterBase, ParameterArgs) || !SplitGenericType(ArgumentType, ArgumentBase, ArgumentArgs) || ParameterBase != ArgumentBase || ParameterArgs.size() != ArgumentArgs.size())
				{
					return;
				}
				for (std::size_t Index = 0; Index < ParameterArgs.size(); ++Index)
				{
					InferFunctionTypeArguments(
					    Function, ParameterArgs[Index], ArgumentArgs[Index], OutTypes);
				}
			}

			std::string SubstituteFunctionTypeArguments(
			    std::string Type,
			    const std::unordered_map<std::string, std::string>& Types) const
			{
				Type = ResolveAlias(Trim(std::move(Type)));
				if (const auto Found = Types.find(Type); Found != Types.end())
				{
					return Found->second;
				}
				if (Runtime::IsReferenceTypeName(Type))
				{
					const bool bConst = Runtime::IsConstReferenceTypeName(Type);
					return (bConst ? "const ref " : "ref ") + SubstituteFunctionTypeArguments(std::string(Runtime::StripReferenceTypeName(Type)), Types);
				}
				std::vector<std::string> FunctionParams;
				std::string FunctionReturn;
				if (Runtime::ParseFunctionType(Type, FunctionParams, FunctionReturn))
				{
					for (std::string& Parameter : FunctionParams)
					{
						Parameter = SubstituteFunctionTypeArguments(std::move(Parameter), Types);
					}
					return Runtime::MakeFunctionTypeName(FunctionParams,
					    SubstituteFunctionTypeArguments(std::move(FunctionReturn), Types));
				}
				std::string Base;
				std::vector<std::string> Args;
				if (!SplitGenericType(Type, Base, Args))
				{
					return Type;
				}
				std::ostringstream Result;
				Result << Base << '<';
				for (std::size_t Index = 0; Index < Args.size(); ++Index)
				{
					if (Index != 0)
					{
						Result << ',';
					}
					Result << SubstituteFunctionTypeArguments(Args[Index], Types);
				}
				Result << '>';
				return Result.str();
			}

			FResolvedCall ResolveCall(const FCallExpr& Call) const
			{
				std::vector<std::string> Symbols;
				const std::string CanonicalName = CanonicalSymbol(Call.Callee);
				const std::string Ordinary = ResolveInNamespace(CanonicalName, FunctionNames);
				if (FunctionNames.find(Ordinary) != FunctionNames.end())
				{
					Symbols.push_back(Ordinary);
				}
				else if (CanonicalName.find("::") == std::string::npos)
				{
					Symbols = ResolveUsingSymbols(CanonicalName, FunctionNames);
				}
				if (Symbols.empty())
				{
					return {ResolveFunctionSymbol(Call.Callee), nullptr};
				}

				FResolvedCall Best;
				int BestScore = -100000;
				bool bTie = false;
				for (const std::string& Symbol : Symbols)
				{
					const auto Found = FunctionDecls.find(Symbol);
					if (Found == FunctionDecls.end())
					{
						continue;
					}
					for (const FFunctionDecl* Function : Found->second)
					{
						if (!IsVisibleFromActiveModule(
						        Function->ModulePath, Function->bExported))
						{
							continue;
						}
						const std::size_t ArgumentCount = Call.GetArguments().size();
						const bool bVariadic = !Function->Parameters.empty() && Function->Parameters.back().bIsVariadic;
						if (ArgumentCount < RequiredParameterCount(*Function) || (!bVariadic && ArgumentCount > Function->Parameters.size()))
						{
							continue;
						}
						if (!Call.TypeArgs.empty() && Call.TypeArgs.size() != Function->TypeParams.size())
						{
							continue;
						}
						int Score = 0;
						bool bValid = true;
						for (std::size_t Index = 0; Index < ArgumentCount; ++Index)
						{
							const std::size_t ParameterIndex = bVariadic
							    ? std::min(Index, Function->Parameters.size() - 1)
							    : Index;
							const FFunctionParam& Parameter = Function->Parameters[ParameterIndex];
							const FExpr& RawArgument = *Call.GetArguments()[Index];
							const bool bArgumentIsRef = RawArgument.Kind == EExprKind::Ref;
							if (bArgumentIsRef != Parameter.bIsRef)
							{
								bValid = false;
								break;
							}
							if (bArgumentIsRef)
							{
								const FExpr& Operand = *static_cast<const FRefExpr&>(RawArgument).Operand;
								const bool bArgumentIsConst = IsKnownConstRefTarget(Operand);
								if (bArgumentIsConst && !Parameter.bIsConst)
								{
									bValid = false;
									break;
								}
								Score += bArgumentIsConst == Parameter.bIsConst ? 2 : 1;
							}
							const std::string ArgumentType = InferExprType(*Call.GetArguments()[Index]);
							const std::string& ParameterType = Parameter.TypeName;
							const int Match = FunctionParameterMatchScore(
							    *Function, ParameterType, ArgumentType);
							if (Match < 0)
							{
								bValid = false;
								break;
							}
							Score += Match;
						}
						if (!bValid)
						{
							continue;
						}
						if (Score > BestScore)
						{
							Best = {Symbol, Function};
							BestScore = Score;
							bTie = false;
						}
						else if (Score == BestScore)
						{
							bTie = true;
						}
					}
				}
				if (bTie)
				{
					Best.Decl = nullptr;
				}
				return Best;
			}

			std::string InferFunctionReferenceType(const std::string& Name) const
			{
				const auto Found = FunctionDecls.find(ResolveFunctionSymbol(Name));
				if (Found == FunctionDecls.end()) return {};
				const auto Visible = [&](const FFunctionDecl* Function)
				{
					return Function && IsVisibleFromActiveModule(Function->ModulePath, Function->bExported);
				};
				const bool bHasLocal = std::any_of(Found->second.begin(), Found->second.end(),
				    [&](const FFunctionDecl* Function)
				{
					return Visible(Function) && Function->ModulePath == ActiveModulePath;
				});
				const FFunctionDecl* Selected = nullptr;
				for (const FFunctionDecl* Function : Found->second)
				{
					if (!Visible(Function) || (bHasLocal && Function->ModulePath != ActiveModulePath))
					{
						continue;
					}
					if (Selected || Function->bDeleted || !Function->Body || !Function->TypeParams.empty() || std::any_of(Function->Parameters.begin(), Function->Parameters.end(), [](const FFunctionParam& Parameter)
					{
						return Parameter.bIsVariadic;
					}))
					{
						return {};
					}
					Selected = Function;
				}
				if (!Selected) return {};
				std::vector<std::string> Parameters;
				for (const FFunctionParam& Parameter : Selected->Parameters)
				{
					std::string Type = ResolveAlias(Parameter.TypeName);
					if (Parameter.bIsRef)
					{
						Type = (Parameter.bIsConst ? "const ref " : "ref ") + Type;
					}
					Parameters.push_back(std::move(Type));
				}
				return Runtime::MakeFunctionTypeName(Parameters, ResolveAlias(Selected->ReturnType));
			}

			bool TryNamespaceValueName(const FMemberAccessExpr& Member,
			    const std::string& ObjectType, std::string& OutName) const
			{
				if (!Member.Object) return false;
				const FExpr* Root = Member.Object.get();
				while (Root->Kind == EExprKind::MemberAccess)
				{
					Root = static_cast<const FMemberAccessExpr&>(*Root).Object.get();
				}
				if (Root->Kind == EExprKind::Identifier)
				{
					const auto& Name = static_cast<const FIdentifierExpr&>(*Root).Name;
					if ((bActiveCallableHasThis && Name == "Super") || ResolveHostType(Name) || !ResolveUserTypeSymbol(Name).empty() || EnumTypeNames.count(ResolveInNamespace(Name, EnumTypeNames)))
					{
						return false;
					}
				}
				if (!ObjectType.empty())
				{
					// A function and a nested namespace may share a name. A bound
					// object or global value still keeps ordinary member precedence.
					if (!Runtime::IsFunctionTypeName(ObjectType) || Member.Object->Kind != EExprKind::MemberAccess)
					{
						return false;
					}
					std::string Prefix;
					if (!TryFlattenQualifiedName(*Member.Object, Prefix) || ResolveGlobal(Prefix)) return false;
					if (!InferExprType(*Root).empty()) return false;
				}
				if (!TryFlattenQualifiedName(Member, OutName)) return false;
				OutName = CanonicalSymbol(std::move(OutName));
				return true;
			}

			const FFunctionDecl* ResolveFunctionValue(
			    const std::string& Name,
			    const std::string& FunctionType) const
			{
				std::vector<std::string> ExpectedParams;
				std::string ExpectedReturn;
				if (!Runtime::ParseFunctionType(ResolveAlias(FunctionType),
				        ExpectedParams, ExpectedReturn))
				{
					return nullptr;
				}
				const std::string Symbol = ResolveFunctionSymbol(Name);
				const auto Found = FunctionDecls.find(Symbol);
				if (Found == FunctionDecls.end())
				{
					return nullptr;
				}
				const FFunctionDecl* Selected = nullptr;
				for (const FFunctionDecl* Function : Found->second)
				{
					if (!Function || Function->bDeleted || !Function->Body || !IsVisibleFromActiveModule(Function->ModulePath, Function->bExported) || Function->Parameters.size() != ExpectedParams.size() || std::any_of(Function->Parameters.begin(), Function->Parameters.end(), [](const FFunctionParam& Parameter)
					{
						return Parameter.bIsVariadic;
					}))
					{
						continue;
					}
					std::unordered_map<std::string, std::string> TypeArguments;
					bool bMatches = true;
					for (std::size_t Index = 0; Index < ExpectedParams.size(); ++Index)
					{
						const FFunctionParam& Parameter = Function->Parameters[Index];
						const bool bExpectedRef = Runtime::IsReferenceTypeName(ExpectedParams[Index]);
						if (Parameter.bIsRef != bExpectedRef || (bExpectedRef && Parameter.bIsConst != Runtime::IsConstReferenceTypeName(ExpectedParams[Index])))
						{
							bMatches = false;
							break;
						}
						const std::string Expected = std::string(
						    Runtime::StripReferenceTypeName(ExpectedParams[Index]));
						InferFunctionTypeArguments(*Function, Parameter.TypeName, Expected, TypeArguments);
						if (NormalizeType(SubstituteFunctionTypeArguments(Parameter.TypeName, TypeArguments)) != NormalizeType(Expected))
						{
							bMatches = false;
							break;
						}
					}
					if (!bMatches || NormalizeType(SubstituteFunctionTypeArguments(Function->ReturnType, TypeArguments)) != NormalizeType(ExpectedReturn))
					{
						continue;
					}
					if (Selected)
					{
						return nullptr;
					}
					Selected = Function;
				}
				return Selected;
			}

			FResolvedCall ResolveExtensionCall(const FMethodCallExpr& Call) const
			{
				std::vector<std::string> Symbols;
				const std::string CanonicalName = CanonicalSymbol(Call.Method);
				const std::string Ordinary = ResolveInNamespace(CanonicalName, FunctionNames);
				if (FunctionNames.find(Ordinary) != FunctionNames.end())
				{
					Symbols.push_back(Ordinary);
				}
				else if (CanonicalName.find("::") == std::string::npos)
				{
					Symbols = ResolveUsingSymbols(CanonicalName, FunctionNames);
				}
				if (Symbols.empty() || !Call.Object)
				{
					return {};
				}

				std::vector<std::string> ArgumentTypes = {InferExprType(*Call.Object)};
				for (const FExpr* Argument : Call.GetArguments())
				{
					ArgumentTypes.push_back(Argument ? InferExprType(*Argument) : std::string());
				}

				FResolvedCall Best;
				int BestScore = -100000;
				bool bTie = false;
				for (const std::string& Symbol : Symbols)
				{
					const auto Found = FunctionDecls.find(Symbol);
					if (Found == FunctionDecls.end())
					{
						continue;
					}
					for (const FFunctionDecl* Function : Found->second)
					{
						if (!IsVisibleFromActiveModule(
						        Function->ModulePath, Function->bExported))
						{
							continue;
						}
						const bool bVariadic = !Function->Parameters.empty() && Function->Parameters.back().bIsVariadic;
						if (ArgumentTypes.size() < RequiredParameterCount(*Function) || (!bVariadic && ArgumentTypes.size() > Function->Parameters.size()) || Function->Parameters.empty())
						{
							continue;
						}
						if (!Call.TypeArgs.empty() && Call.TypeArgs.size() != Function->TypeParams.size())
						{
							continue;
						}
						int Score = 0;
						bool bValid = true;
						for (std::size_t Index = 0; Index < ArgumentTypes.size(); ++Index)
						{
							const std::size_t ParameterIndex = bVariadic
							    ? std::min(Index, Function->Parameters.size() - 1)
							    : Index;
							const FFunctionParam& Parameter = Function->Parameters[ParameterIndex];
							const FExpr* RefTarget = nullptr;
							const bool bArgumentIsRef = Index == 0
							    ? Parameter.bIsRef
							    : Call.GetArguments()[Index - 1]->Kind == EExprKind::Ref;
							if (Index == 0)
							{
								RefTarget = Call.Object.get();
							}
							else
							{
								const FExpr& RawArgument = *Call.GetArguments()[Index - 1];
								RefTarget = bArgumentIsRef
								    ? static_cast<const FRefExpr&>(RawArgument).Operand.get()
								    : &RawArgument;
							}
							if (Index == 0 && Parameter.bIsRef && !CanBindImplicitRefReceiver(*Call.Object))
							{
								bValid = false;
								break;
							}
							if (Parameter.bIsRef != bArgumentIsRef)
							{
								bValid = false;
								break;
							}
							if (bArgumentIsRef && RefTarget)
							{
								const bool bArgumentIsConst = IsKnownConstRefTarget(*RefTarget);
								if (bArgumentIsConst && !Parameter.bIsConst)
								{
									bValid = false;
									break;
								}
								Score += bArgumentIsConst == Parameter.bIsConst ? 2 : 1;
							}
							const int Match = FunctionParameterMatchScore(
							    *Function,
							    Parameter.TypeName,
							    ArgumentTypes[Index]);
							if (Match < 0)
							{
								bValid = false;
								break;
							}
							Score += Match;
						}
						if (bValid && Function->Parameters.front().bIsRef)
						{
							Score += 100;
						}
						if (!bValid)
						{
							continue;
						}
						if (Score > BestScore)
						{
							Best = {Symbol, Function};
							BestScore = Score;
							bTie = false;
						}
						else if (Score == BestScore)
						{
							bTie = true;
						}
					}
				}
				if (bTie)
				{
					Best.Decl = nullptr;
				}
				return Best;
			}

			std::unordered_map<std::string, std::string> ResolveFunctionTypeArguments(
			    const FResolvedCall& Resolved,
			    const std::vector<std::string>& ExplicitTypeArgs,
			    const std::vector<std::string>& ArgumentTypes) const
			{
				if (!Resolved.Decl)
				{
					return {};
				}
				std::unordered_map<std::string, std::string> Types;
				for (std::size_t Index = 0;
				    Index < Resolved.Decl->TypeParams.size() && Index < ExplicitTypeArgs.size();
				    ++Index)
				{
					Types.emplace(Resolved.Decl->TypeParams[Index], ExplicitTypeArgs[Index]);
				}
				const bool bVariadic = !Resolved.Decl->Parameters.empty() && Resolved.Decl->Parameters.back().bIsVariadic;
				for (std::size_t Index = 0; Index < ArgumentTypes.size() && !Resolved.Decl->Parameters.empty(); ++Index)
				{
					const std::size_t ParameterIndex = bVariadic
					    ? std::min(Index, Resolved.Decl->Parameters.size() - 1)
					    : Index;
					if (ParameterIndex >= Resolved.Decl->Parameters.size())
					{
						break;
					}
					InferFunctionTypeArguments(
					    *Resolved.Decl,
					    Resolved.Decl->Parameters[ParameterIndex].TypeName,
					    ArgumentTypes[Index],
					    Types);
				}
				return Types;
			}

			std::string ResolveFunctionReturnType(
			    const FResolvedCall& Resolved,
			    const std::vector<std::string>& ExplicitTypeArgs,
			    const std::vector<std::string>& ArgumentTypes) const
			{
				if (!Resolved.Decl)
				{
					return {};
				}
				return SubstituteFunctionTypeArguments(Resolved.Decl->ReturnType,
				    ResolveFunctionTypeArguments(Resolved, ExplicitTypeArgs, ArgumentTypes));
			}

			std::string ResolveCallReturnType(const FCallExpr& Call) const
			{
				const FResolvedCall Resolved = ResolveCall(Call);
				std::vector<std::string> ArgumentTypes;
				ArgumentTypes.reserve(Call.GetArguments().size());
				for (const FExpr* Argument : Call.GetArguments())
				{
					ArgumentTypes.push_back(Argument ? InferExprType(*Argument) : std::string());
				}
				return ResolveFunctionReturnType(Resolved, Call.TypeArgs, ArgumentTypes);
			}

			int HostParameterScore(
			    const ENativeType NativeType,
			    const std::string& HostName,
			    const std::string& ElementName,
			    const std::string& KeyName,
			    const std::string& ValueName,
			    const std::string& ArgumentType) const
			{
				if (ArgumentType.empty())
				{
					return 0;
				}
				const std::string Argument = NormalizeType(ArgumentType);
				const std::string Expected = NormalizeType(NativeScriptType(
				    NativeType, HostName, ElementName, KeyName, ValueName));
				if (Argument == Expected)
				{
					return 4;
				}
				if (NativeType == ENativeType::HostObject)
				{
					return CanonicalSymbol(Argument) == CanonicalSymbol(HostName) ? 4 : -1;
				}
				if (NativeType == ENativeType::Enum)
				{
					return CanonicalSymbol(Argument) == CanonicalSymbol(HostName) ? 4 : -1;
				}
				if (IsNumericType(Argument) && IsNumericType(Expected))
				{
					return 1;
				}
				return -1;
			}

			const FHostMemberDesc* ResolveHostMember(
			    const FHostTypeDesc& Owner,
			    const std::string_view Name,
			    const std::initializer_list<EHostMemberKind> Kinds,
			    const FExprArgumentView Arguments) const
			{
				const FHostMemberDesc* Best = nullptr;
				int BestScore = -1;
				bool bTie = false;
				for (const FHostMemberDesc& Member : Owner.Members)
				{
					if ((Member.Kind != EHostMemberKind::Constructor && Member.Name != Name) || std::find(Kinds.begin(), Kinds.end(), Member.Kind) == Kinds.end() || Member.ParamTypes.size() != Arguments.size())
					{
						continue;
					}
					int Score = 0;
					bool bMatches = true;
					for (std::size_t Index = 0; Index < Arguments.size(); ++Index)
					{
						const std::uint8_t Flag = Index < Member.ParamFlags.size()
						    ? Member.ParamFlags[Index]
						    : 0;
						const bool bArgumentIsRef = Arguments[Index] && Arguments[Index]->Kind == EExprKind::Ref;
						const bool bParameterIsRef = NativeParamFlagIsRef(Flag);
						if (bArgumentIsRef && !bParameterIsRef)
						{
							bMatches = false;
							break;
						}
						const int Match = HostParameterScore(Member.ParamTypes[Index],
						    Index < Member.ParamHostTypeNames.size()
						        ? Member.ParamHostTypeNames[Index]
						        : std::string{},
						    Index < Member.ParamElementTypeNames.size()
						        ? Member.ParamElementTypeNames[Index]
						        : std::string{},
						    Index < Member.ParamKeyTypeNames.size()
						        ? Member.ParamKeyTypeNames[Index]
						        : std::string{},
						    Index < Member.ParamValueTypeNames.size()
						        ? Member.ParamValueTypeNames[Index]
						        : std::string{},
						    Arguments[Index] ? InferExprType(*Arguments[Index]) : std::string{});
						if (Match < 0)
						{
							bMatches = false;
							break;
						}
						Score += Match + (bArgumentIsRef == bParameterIsRef ? 2 : 0);
					}
					if (!bMatches)
					{
						continue;
					}
					if (Score > BestScore)
					{
						Best = &Member;
						BestScore = Score;
						bTie = false;
					}
					else if (Score == BestScore)
					{
						bTie = true;
					}
				}
				return bTie ? nullptr : Best;
			}

			std::string EmitHostMemberCall(
			    const FHostTypeDesc& Owner,
			    const FHostMemberDesc& Member,
			    const FExprArgumentView Arguments,
			    const FExpr* const Receiver = nullptr)
			{
				std::ostringstream Result;
				Result << HostMethodName(HostMemberIdentity(Owner, Member)) << '(';
				bool bComma = false;
				if (Receiver)
				{
					Result << EmitExpr(*Receiver);
					bComma = true;
				}
				for (std::size_t Index = 0; Index < Arguments.size(); ++Index)
				{
					if (bComma)
					{
						Result << ", ";
					}
					bComma = true;
					const std::uint8_t Flag = Index < Member.ParamFlags.size()
					    ? Member.ParamFlags[Index]
					    : 0;
					Result << EmitHostArgument(*Arguments[Index], Flag, Arguments[Index]->Location);
				}
				Result << ')';
				return Result.str();
			}

			std::string InferExprType(const FExpr& Expr) const
			{
				switch (Expr.Kind)
				{
				case EExprKind::IntegerLiteral:
					{
						const auto& Literal = static_cast<const FIntegerLiteralExpr&>(Expr);
						return Literal.bIs64 ? (Literal.bUnsigned ? "uint64" : "int64")
						                     : (Literal.bUnsigned ? "unsigned int" : "int");
					}
				case EExprKind::CharacterLiteral:
					return "char";
				case EExprKind::FloatLiteral:
					return static_cast<const FFloatLiteralExpr&>(Expr).bFloat32 ? "float" : "double";
				case EExprKind::BooleanLiteral:
					return "bool";
				case EExprKind::NullptrLiteral:
					return "nullptr";
				case EExprKind::This:
					return ActiveTypeName;
				case EExprKind::Identifier:
					{
						const std::string& Name = static_cast<const FIdentifierExpr&>(Expr).Name;
						if (const auto Found = LocalTypes.find(Name); Found != LocalTypes.end())
						{
							return ResolveAlias(Found->second);
						}
						if (!ActiveTypeName.empty())
						{
							const FResolvedUserField Field = ResolveUserField(ActiveTypeName, Name);
							if (Field.Decl)
							{
								return SubstituteUserTypeArguments(
								    Field.OwnerType, Field.Decl->TypeName);
							}
						}
						if (const FVarDeclStmt* Global = ResolveGlobal(Name))
						{
							if (const auto Found = GlobalTypes.find(Global); Found != GlobalTypes.end())
							{
								return ResolveAlias(Found->second);
							}
						}
						if (const std::string EnumType = ResolveEnumMemberType(Name); !EnumType.empty())
						{
							return EnumType;
						}
						if (const std::string UserType = ResolveUserTypeSymbol(Name); !UserType.empty())
						{
							return UserType;
						}
						return InferFunctionReferenceType(Name);
					}
				case EExprKind::ArrayInit:
					return ResolveAlias(static_cast<const FArrayInitExpr&>(Expr).TypeName);
				case EExprKind::MapInit:
					return ResolveAlias(static_cast<const FMapInitExpr&>(Expr).TypeName);
				case EExprKind::StringLiteral:
					return "string";
				case EExprKind::WStringLiteral:
					return "wstring";
				case EExprKind::Call:
					{
						const auto& Call = static_cast<const FCallExpr&>(Expr);
						if (Call.Callee == "__std_bind_method" && IsMethodBindingFactoryCall(Call))
						{
							return Call.TypeArgs[0];
						}
						if (IsMethodBindingFactoryCall(Call) && Call.Callee != "__std_bind_method")
						{
							return "attribute<" + Call.TypeArgs[0] + ">";
						}

						std::string ConstructedType = Call.Callee;
						if (!Call.TypeArgs.empty() && ConstructedType.find('<') == std::string::npos)
						{
							ConstructedType += '<';
							for (std::size_t Index = 0; Index < Call.TypeArgs.size(); ++Index)
							{
								if (Index != 0)
								{
									ConstructedType += ',';
								}
								ConstructedType += Call.TypeArgs[Index];
							}
							ConstructedType += '>';
						}
						std::string HandlePayload;
						if (IsBuiltinHandleType(ConstructedType, HandlePayload)) return ConstructedType;
						if (Call.Callee == "handle" && Call.GetArguments().size() == 1 && !HasNonGenericHandleType() && IsBuiltinHandleType("handle<int>", HandlePayload))
						{
							return "handle<" + InferExprType(*Call.GetArguments().front()) + ">";
						}
						if (!ResolveUserTypeSymbol(ConstructedType).empty()) return ConstructedType;
						std::string CallableType;
						if (Call.CalleeExpr)
						{
							CallableType = InferExprType(*Call.CalleeExpr);
						}
						else if (const auto Local = LocalTypes.find(Call.Callee); Local != LocalTypes.end())
						{
							CallableType = Local->second;
						}
						else if (const FResolvedUserField Field = ResolveUserField(
						             ActiveTypeName, Call.Callee);
						    Field.Decl)
						{
							CallableType = SubstituteUserTypeArguments(
							    Field.OwnerType, Field.Decl->TypeName);
						}
						else if (const FVarDeclStmt* Global = ResolveGlobal(Call.Callee))
						{
							if (const auto Found = GlobalTypes.find(Global); Found != GlobalTypes.end())
							{
								CallableType = Found->second;
							}
						}
						std::vector<std::string> CallableParams;
						std::string CallableReturn;
						if (Runtime::ParseFunctionType(ResolveAlias(CallableType),
						        CallableParams, CallableReturn))
						{
							return CallableReturn;
						}
						if (Call.Callee == "to_string" || Call.Callee == "format" || Call.Callee == "__format" || Call.Callee == "formatf" || Call.Callee == "__format_printf")
						{
							return "string";
						}
						if (Call.Callee == "range")
						{
							return "array<int>";
						}
						if (Call.Callee == "to_wstring")
						{
							return "wstring";
						}
						if (Call.Callee == "to_int")
						{
							return "int";
						}
						if (Call.Callee == "cast" && Call.TypeArgs.size() == 1 && Call.GetArguments().size() == 1 &&
						    Call.GetArguments()[0]->Kind == EExprKind::IntegerLiteral)
						{
							return Call.TypeArgs[0];
						}
						if (Call.Callee == "__char_code")
						{
							return "int";
						}
						if (Call.Callee == "__substr")
						{
							return "string";
						}
						if (Call.Callee == "to_unsigned")
						{
							return "unsigned int";
						}
						if (Call.Callee == "len" || Call.Callee == "__array_prepare_sort" || Call.Callee == "__find" || Call.Callee == "to_int64")
						{
							return "int64";
						}
						if (Call.Callee == "to_uint64")
						{
							return "uint64";
						}
						if (Call.Callee == "to_float")
						{
							return "float";
						}
						if (Call.Callee == "to_double" || (Call.bStandardLibraryQualified && (Call.Callee == "now" || Call.Callee == "steady_now")))
						{
							return "double";
						}
						if (Call.Callee == "__sqrt" || Call.Callee == "__sin" || Call.Callee == "__cos" || Call.Callee == "__tan" || Call.Callee == "__floor" || Call.Callee == "__ceil" || Call.Callee == "__round" || Call.Callee == "__trunc" || Call.Callee == "__pow" || Call.Callee == "__exp" || Call.Callee == "__log" || Call.Callee == "__log2" || Call.Callee == "__asin" || Call.Callee == "__acos" || Call.Callee == "__atan2")
						{
							return "double";
						}
						if (Call.Callee == "__isfinite" || Call.Callee == "__isnan")
						{
							return "bool";
						}
						if (Call.Callee == "__to_int8")
						{
							return "char";
						}
						if (Call.Callee == "__to_uint8")
						{
							return "unsigned char";
						}
						if (Call.Callee == "__to_int16")
						{
							return "short";
						}
						if (Call.Callee == "__to_uint16")
						{
							return "unsigned short";
						}
						if (Call.Callee == "abs" && Call.GetArguments().size() == 1)
						{
							return NormalizeType(InferExprType(*Call.GetArguments().front()));
						}
						if (!ActiveTypeName.empty())
						{
							const FResolvedUserMethod Method = ResolveUserMethod(
							    ActiveTypeName, Call.Callee, Call.GetArguments());
							if (Method.Decl)
							{
								return SubstituteUserTypeArguments(
								    Method.OwnerType, Method.Decl->ReturnType);
							}
						}
						if ((Call.Callee == "min" || Call.Callee == "max") && Call.GetArguments().size() == 2)
						{
							const std::string Left = NormalizeType(InferExprType(*Call.GetArguments()[0]));
							const std::string Right = NormalizeType(InferExprType(*Call.GetArguments()[1]));
							if (Left == "double" || Right == "double")
							{
								return "double";
							}
							if (Left == "float" || Right == "float")
							{
								return "float";
							}
							if (IsUnsignedIntegerType(Left) && IsUnsignedIntegerType(Right))
							{
								return Left == "uint64" || Right == "uint64" ? "uint64" : "unsigned int";
							}
							if (IsSignedIntegerType(Left) && IsSignedIntegerType(Right))
							{
								return Left == "int64" || Right == "int64" ? "int64" : "int";
							}
						}
						if (HostBindings)
						{
							if (const FHostFunctionDesc* Function = HostBindings->FindFunction(Call.Callee))
							{
								return HostReturnScriptType(*Function);
							}
							if (const FHostTypeDesc* Type = ResolveHostType(Call.Callee))
							{
								if (ResolveHostMember(*Type, Type->Name,
								        {EHostMemberKind::Constructor}, Call.GetArguments()))
								{
									return Type->Name;
								}
							}
						}
						if (const std::string ReturnType = ResolveCallReturnType(Call); !ReturnType.empty())
						{
							return ReturnType;
						}
						if (Call.Callee.find('<') != std::string::npos)
						{
							return ResolveAlias(Call.Callee);
						}
						return {};
					}
				case EExprKind::MethodCall:
					{
						const auto& Method = static_cast<const FMethodCallExpr&>(Expr);
						if (!Method.Object)
						{
							return {};
						}
						std::string AttrBase;
						std::vector<std::string> AttrArgs;
						if (SplitGenericType(InferExprType(*Method.Object), AttrBase, AttrArgs) &&
						    AttrArgs.size() == 1)
						{
							if (((AttrBase == "attribute" && HasStandardAttribute()) &&
							        (Method.Method == "is_set" || Method.Method == "is_bound" ||
							            Method.Method == "has_same_configuration")) ||
							    (AttrBase == "optional" && Method.Method == "has_value"))
							{
								return "bool";
							}

							if (((AttrBase == "attribute" && HasStandardAttribute()) &&
							        (Method.Method == "get" || Method.Method == "get_or")) ||
							    (AttrBase == "optional" &&
							        (Method.Method == "get" || Method.Method == "value_or")))
							{
								return AttrArgs[0];
							}

							if ((AttrBase == "attribute" && HasStandardAttribute()) &&
							    Method.Method == "try_get")
							{
								return Method.GetArguments().empty() ? "optional<" + AttrArgs[0] + ">" : "bool";
							}
						}
						const std::string UserOwnerType = InferExprType(*Method.Object);
						const FResolvedUserMethod UserMethod =
						    ResolveUserMethod(UserOwnerType, Method.Method, Method.GetArguments());
						if (UserMethod.Decl)
						{
							return SubstituteUserTypeArguments(
							    UserMethod.OwnerType, UserMethod.Decl->ReturnType);
						}
						if (HostBindings)
						{
							std::string OwnerName;
							if (TryFlattenQualifiedName(*Method.Object, OwnerName))
							{
								if (const FHostTypeDesc* Owner = ResolveHostType(OwnerName))
								{
									if (const FHostMemberDesc* Member = ResolveHostMember(*Owner,
									        Method.Method, {EHostMemberKind::StaticMethod},
									        Method.GetArguments()))
									{
										return HostReturnScriptType(*Owner, *Member);
									}
								}
							}
							if (const FHostTypeDesc* Owner = ResolveHostType(InferExprType(*Method.Object)))
							{
								if (const FHostMemberDesc* Member = ResolveHostMember(*Owner,
								        Method.Method, {EHostMemberKind::Method}, Method.GetArguments()))
								{
									return HostReturnScriptType(*Owner, *Member);
								}
							}
						}
						std::string Base;
						std::vector<std::string> Args;
						const bool bContainer = SplitGenericType(
						    InferExprType(*Method.Object), Base, Args);
						const bool bArray = bContainer && Base == "array";
						const bool bMap = bContainer && (Base == "map" || Base == "ordered_map" || Base == "multimap" || Base == "ordered_multimap");
						const bool bMultiMap = bContainer && (Base == "multimap" || Base == "ordered_multimap");
						const bool bSet = bContainer && (Base == "set" || Base == "multiset" || Base == "ordered_multiset");
						const bool bQueue = bContainer && Base == "queue";
						const bool bPriorityQueue = bContainer && Base == "priority_queue";
						const bool bStack = bContainer && Base == "stack";
						const bool bDeque = bContainer && Base == "deque";
						const bool bSupportedContainer = bArray || bMap || bSet || bQueue || bPriorityQueue || bStack || bDeque;
						if (bSupportedContainer && (Method.Method == "size" || Method.Method == "len"))
						{
							return "int64";
						}
						if (bContainer && Method.Method == "keys" && (Base == "map" || Base == "ordered_map" || Base == "multimap" || Base == "ordered_multimap") && Args.size() == 2)
						{
							return "array<" + Args[0] + ">";
						}
						if (bContainer && Method.Method == "values")
						{
							if ((Base == "map" || Base == "ordered_map") && Args.size() == 2)
							{
								return "array<" + Args[1] + ">";
							}
							if (bMultiMap && Args.size() == 2)
							{
								return "array<array<" + Args[1] + ">>";
							}
							if (Args.size() == 1)
							{
								return "array<" + Args[0] + ">";
							}
						}
						if (bContainer && Method.Method == "entries" && (Base == "map" || Base == "ordered_map") && Args.size() == 2)
						{
							return "array<pair<" + Args[0] + "," + Args[1] + ">>";
						}
						if (bContainer && Method.Method == "entries" && bMultiMap && Args.size() == 2)
						{
							return "array<pair<" + Args[0] + ",array<" + Args[1] + ">>>";
						}
						if (bArray && Method.Method == "capacity")
						{
							return "int64";
						}
						if ((Base == "multiset" || Base == "ordered_multiset") && Method.Method == "count")
						{
							return "int64";
						}
						if ((bArray || bMap || bSet) && Method.Method == "remove")
						{
							return "bool";
						}
						if ((bMap || bSet) && Method.Method == "has")
						{
							return "bool";
						}
						if ((bArray && (Method.Method == "push" || Method.Method == "remove_at")) || ((bMap || bSet) && Method.Method == "add") || ((bQueue || bPriorityQueue || bStack) && Method.Method == "push") || (bDeque && (Method.Method == "push_back" || Method.Method == "push_front")) || (bSupportedContainer && Method.Method == "clear") || ((bArray || bMap || bSet) && Method.Method == "reserve"))
						{
							return InferExprType(*Method.Object);
						}
						if (((bQueue && (Method.Method == "pop" || Method.Method == "front")) || (bPriorityQueue && (Method.Method == "pop" || Method.Method == "top")) || (bStack && (Method.Method == "pop" || Method.Method == "top")) || (bDeque && (Method.Method == "front" || Method.Method == "back" || Method.Method == "pop_back" || Method.Method == "pop_front"))) && Args.size() == 1)
						{
							return Args[0];
						}
						const FResolvedCall Resolved = ResolveExtensionCall(Method);
						std::vector<std::string> ArgumentTypes = {InferExprType(*Method.Object)};
						for (const FExpr* Argument : Method.GetArguments())
						{
							ArgumentTypes.push_back(Argument ? InferExprType(*Argument) : std::string());
						}
						return ResolveFunctionReturnType(Resolved, Method.TypeArgs, ArgumentTypes);
					}
				case EExprKind::IndexAccess:
					{
						const auto& Index = static_cast<const FIndexAccessExpr&>(Expr);
						std::string Base;
						std::vector<std::string> Args;
						if (!SplitGenericType(InferExprType(*Index.Object), Base, Args))
						{
							return {};
						}
						if ((Base == "map" || Base == "ordered_map") && Args.size() == 2)
						{
							return Args[1];
						}
						if ((Base == "multimap" || Base == "ordered_multimap") && Args.size() == 2)
						{
							return "array<" + Args[1] + ">";
						}
						if (Args.size() == 1)
						{
							return Args[0];
						}
						return {};
					}
				case EExprKind::MemberAccess:
					{
						const auto& Member = static_cast<const FMemberAccessExpr&>(Expr);
						const std::string ObjectType = InferExprType(*Member.Object);
						std::string HandlePayload;
						if (Member.Member == "value" && IsBuiltinHandleType(ObjectType, HandlePayload))
						{
							return HandlePayload;
						}
						const FResolvedUserField UserField = ResolveUserField(
						    ObjectType, Member.Member);
						if (UserField.Decl)
						{
							return SubstituteUserTypeArguments(
							    UserField.OwnerType, UserField.Decl->TypeName);
						}
						std::string Qualified;
						if (TryFlattenQualifiedName(Expr, Qualified))
						{
							std::string NamespaceName;
							if (TryNamespaceValueName(Member, ObjectType, NamespaceName))
							{
								if (const FVarDeclStmt* Global = ResolveGlobal(NamespaceName))
								{
									if (const auto Found = GlobalTypes.find(Global); Found != GlobalTypes.end())
									{
										return ResolveAlias(Found->second);
									}
								}
								if (const auto FunctionType = InferFunctionReferenceType(NamespaceName); !FunctionType.empty())
								{
									return FunctionType;
								}
							}
							if (const std::string EnumType = ResolveEnumMemberType(Qualified);
							    !EnumType.empty())
							{
								return EnumType;
							}
							const std::size_t Separator = CanonicalSymbol(Qualified).rfind("::");
							if (HostBindings && Separator != std::string::npos)
							{
								const std::string Canonical = CanonicalSymbol(Qualified);
								if (const FHostTypeDesc* Owner = ResolveHostType(Canonical.substr(0, Separator)))
								{
									const std::string Name = Canonical.substr(Separator + 2);
									for (const FHostMemberDesc& Candidate : Owner->Members)
									{
										if (Candidate.Name == Name && (Candidate.Kind == EHostMemberKind::StaticConstant || Candidate.Kind == EHostMemberKind::EnumMember))
										{
											return Candidate.Kind == EHostMemberKind::EnumMember
											    ? Owner->Name
											    : HostReturnScriptType(*Owner, Candidate);
										}
									}
								}
							}
						}
						if (HostBindings)
						{
							if (const FHostTypeDesc* Owner = ResolveHostType(ObjectType))
							{
								for (const FHostMemberDesc& Candidate : Owner->Members)
								{
									if (Candidate.Name == Member.Member && (Candidate.Kind == EHostMemberKind::PropertyGetter || Candidate.Kind == EHostMemberKind::Field) && Candidate.ParamTypes.empty())
									{
										return HostReturnScriptType(*Owner, Candidate);
									}
								}
							}
						}
						std::string Base;
						std::vector<std::string> Args;
						if (SplitGenericType(ObjectType, Base, Args) && Base == "pair" && !HasUserPairTemplate() && Args.size() == 2)
						{
							if (Member.Member == "first")
							{
								return Args[0];
							}
							if (Member.Member == "second")
							{
								return Args[1];
							}
						}
						return {};
					}
				case EExprKind::Unary:
					{
						const auto& Unary = static_cast<const FUnaryExpr&>(Expr);
						if (Unary.Op == EUnaryOp::Not)
						{
							return "bool";
						}
						const std::string Operand = NormalizeType(InferExprType(*Unary.Operand));
						if (Operand == "float" || Operand == "double")
						{
							return Operand;
						}
						if (IsUnsignedIntegerType(Operand))
						{
							return Operand == "uint64" ? "uint64" : "unsigned int";
						}
						if (IsSignedIntegerType(Operand))
						{
							return Operand == "int64" ? "int64" : "int";
						}
						return {};
					}
				case EExprKind::Binary:
					{
						const auto& Binary = static_cast<const FBinaryExpr&>(Expr);
						if (Binary.Op == EBinaryOp::LogicalAnd || Binary.Op == EBinaryOp::LogicalOr || Binary.Op == EBinaryOp::Equal || Binary.Op == EBinaryOp::NotEqual || Binary.Op == EBinaryOp::Less || Binary.Op == EBinaryOp::LessEqual || Binary.Op == EBinaryOp::Greater || Binary.Op == EBinaryOp::GreaterEqual || Binary.Op == EBinaryOp::In)
						{
							return "bool";
						}
						const std::string Left = NormalizeType(InferExprType(*Binary.Left));
						const std::string Right = NormalizeType(InferExprType(*Binary.Right));
						if (Binary.Op == EBinaryOp::Add && (Left == "string" || Right == "string"))
						{
							return "string";
						}
						if (Binary.Op == EBinaryOp::Add && Left == "wstring" && Right == "wstring")
						{
							return "wstring";
						}
						if (Binary.Op == EBinaryOp::ShiftLeft || Binary.Op == EBinaryOp::ShiftRight)
						{
							return Left == "uint64" ? "uint64" : Left == "int64" ? "int64"
							    : IsUnsignedIntegerType(Left)                    ? "unsigned int"
							                                                     : "int";
						}
						if (Left == "double" || Right == "double")
						{
							return "double";
						}
						if (Left == "float" || Right == "float")
						{
							return "float";
						}
						if (IsUnsignedIntegerType(Left) && IsUnsignedIntegerType(Right))
						{
							return Left == "uint64" || Right == "uint64" ? "uint64" : "unsigned int";
						}
						if (IsSignedIntegerType(Left) && IsSignedIntegerType(Right))
						{
							return Left == "int64" || Right == "int64" ? "int64" : "int";
						}
						return {};
					}
				case EExprKind::Assign:
					return InferExprType(*static_cast<const FAssignExpr&>(Expr).Target);
				case EExprKind::Conditional:
					{
						const auto& Conditional = static_cast<const FConditionalExpr&>(Expr);
						const std::string Then = InferExprType(*Conditional.ThenExpr);
						const std::string Else = InferExprType(*Conditional.ElseExpr);
						return !Then.empty() ? Then : Else;
					}
				case EExprKind::Ref:
					return InferExprType(*static_cast<const FRefExpr&>(Expr).Operand);
				case EExprKind::Lambda:
					{
						const auto& Lambda = static_cast<const FLambdaExpr&>(Expr);
						std::vector<std::string> Params;
						Params.reserve(Lambda.Parameters.size());
						for (const FFunctionParam& Parameter : Lambda.Parameters)
						{
							if (Parameter.TypeName.empty())
							{
								return {};
							}
							std::string Type = ResolveAlias(Parameter.TypeName);
							if (Parameter.bIsRef)
							{
								Type = Parameter.bIsConst ? "const ref " + Type : "ref " + Type;
							}
							Params.push_back(std::move(Type));
						}
						if (Lambda.ReturnType.empty())
						{
							return {};
						}
						return Runtime::MakeFunctionTypeName(Params, ResolveAlias(Lambda.ReturnType));
					}
				default:
					return {};
				}
			}

			std::string DefaultValueForType(const std::string& SourceType)
			{
				const std::string Type = MapType(SourceType);
				if (Type == "VsString")
				{
					return "new VsString(string.Empty)";
				}
				if (Type == "string")
				{
					return "string.Empty";
				}
				std::string Base;
				std::vector<std::string> Args;
				if (SplitGenericType(ResolveAlias(SourceType), Base, Args) && Base == "pair" && !HasUserPairTemplate() && Args.size() == 2)
				{
					return "new " + Type + "(" + DefaultValueForType(Args[0]) + ", " + DefaultValueForType(Args[1]) + ")";
				}
				for (const std::string_view Prefix : {
				         std::string_view("List<"), std::string_view("Dictionary<"),
				         std::string_view("VsOrderedMap<"), std::string_view("VsMultiMap<"),
				         std::string_view("VsOrderedMultiMap<"), std::string_view("HashSet<"),
				         std::string_view("VsMultiSet<"), std::string_view("VsOrderedMultiSet<"),
				         std::string_view("Queue<"), std::string_view("VsPriorityQueue<"),
				         std::string_view("Stack<")})
				{
					if (Type.compare(0, Prefix.size(), Prefix) == 0)
					{
						return "new " + Type + "()";
					}
				}
				if (ActiveTypeParameters.find(NormalizeType(SourceType)) != ActiveTypeParameters.end())
				{
					return "VsDefault<" + Type + ">()";
				}
				return "default!";
			}

			std::string VariableValueType(const FVarDeclStmt& Variable)
			{
				std::string Type = ResolveAlias(Variable.TypeName);
				if (NormalizeType(Type) != "auto")
				{
					return Type;
				}
				if (Variable.Initializer)
				{
					Type = ResolveAlias(InferExprType(*Variable.Initializer));
				}
				if (!Type.empty() && NormalizeType(Type) != "auto")
				{
					return Type;
				}
				Fail("could not resolve inferred type for variable '" + Variable.Name + "'",
				    Variable.Location);
				return "object";
			}

			std::string EmitArguments(const FExprArgumentView Arguments)
			{
				std::ostringstream Result;
				for (std::size_t Index = 0; Index < Arguments.size(); ++Index)
				{
					if (Index != 0)
					{
						Result << ", ";
					}
					Result << EmitExpr(*Arguments[Index]);
				}
				return Result.str();
			}

			std::string EmitOwnedArguments(const std::vector<std::unique_ptr<FExpr>>& Arguments)
			{
				return EmitArguments(FExprArgumentView(nullptr, &Arguments));
			}

			std::string DisplayTypeName(std::string Type) const
			{
				Type = Trim(ResolveAlias(Trim(std::move(Type))));
				std::string Base;
				std::vector<std::string> Args;
				if (!SplitGenericType(Type, Base, Args))
				{
					return NormalizeScalarType(std::move(Type));
				}
				std::ostringstream Result;
				Result << Base << '<';
				for (std::size_t Index = 0; Index < Args.size(); ++Index)
				{
					if (Index != 0)
					{
						Result << ',';
					}
					Result << DisplayTypeName(Args[Index]);
				}
				Result << '>';
				return Result.str();
			}

			bool IsDisplayCollection(const std::string& Type) const
			{
				std::string Base;
				std::vector<std::string> Args;
				if (!SplitGenericType(Type, Base, Args))
				{
					return false;
				}
				return Base == "array" || Base == "deque" || Base == "map" || Base == "ordered_map" || Base == "set" || Base == "queue" || Base == "stack";
			}

			std::string EmitTextValue(const FExpr& Expr)
			{
				const std::string Type = InferExprType(Expr);
				const std::string Value = EmitExpr(Expr);
				std::string Base;
				std::vector<std::string> Args;
				if (SplitGenericType(Type, Base, Args) && Base == "pair" && !HasUserPairTemplate() && Args.size() == 2)
				{
					const std::string First = DisplayTypeName(Args[0]);
					const std::string Second = DisplayTypeName(Args[1]);
					const std::string RuntimeName = "pair#" + std::to_string(First.size()) + ':' + First + std::to_string(Second.size()) + ':' + Second;
					return "VsNamed(" + Value + ", \"" + EscapeCSharpString(RuntimeName) + "\")";
				}
				return IsDisplayCollection(Type)
				    ? "VsTyped(" + Value + ", \"" + EscapeCSharpString(DisplayTypeName(Type)) + "\")"
				    : Value;
			}

			std::string EmitFormatArguments(const FCallExpr& Call)
			{
				std::ostringstream Result;
				for (std::size_t Index = 0; Index < Call.GetArguments().size(); ++Index)
				{
					if (Index != 0)
					{
						Result << ", ";
					}
					if (Index == 0)
					{
						Result << EmitExpr(*Call.GetArguments()[Index]);
					}
					else if (Call.GetArguments().size() == 2 && DisplayTypeName(InferExprType(*Call.GetArguments()[Index])) == "array<string>")
					{
						Result << EmitExpr(*Call.GetArguments()[Index]);
					}
					else
					{
						Result << EmitTextValue(*Call.GetArguments()[Index]);
					}
				}
				return Result.str();
			}

			std::string EmitExtensionCall(
			    const FMethodCallExpr& Call,
			    const FResolvedCall& Resolved)
			{
				if (!Call.Object || !Resolved.Decl)
				{
					return {};
				}
				const FFunctionDecl& Function = *Resolved.Decl;
				std::vector<std::string> ArgumentTypes{InferExprType(*Call.Object)};
				for (const FExpr* Argument : Call.GetArguments())
				{
					ArgumentTypes.push_back(InferExprType(*Argument));
				}
				const auto CallTypeArguments = ResolveFunctionTypeArguments(
				    Resolved, Call.TypeArgs, ArgumentTypes);
				const auto ParameterType = [&](const FFunctionParam& Parameter)
				{
					return SubstituteFunctionTypeArguments(Parameter.TypeName, CallTypeArguments);
				};
				std::ostringstream Arguments;
				const FFunctionParam& Receiver = Function.Parameters.front();
				if (Receiver.bIsRef)
				{
					if (Receiver.bIsConst)
					{
						Arguments << EmitExpr(*Call.Object, ParameterType(Receiver));
					}
					else if (!IsCSharpWritableRefTarget(*Call.Object))
					{
						Fail("writable ref extension receivers in C# AOT currently require a local or global variable",
						    Call.Location);
						return "default!";
					}
					else
					{
						Arguments << "ref " << EmitExpr(*Call.Object, ParameterType(Receiver));
					}
				}
				else
				{
					Arguments << EmitExpr(*Call.Object, ParameterType(Receiver));
				}
				for (std::size_t Index = 0; Index < Call.GetArguments().size(); ++Index)
				{
					Arguments << ", ";
					const bool bVariadic = Function.Parameters.back().bIsVariadic;
					const std::size_t ParameterIndex = bVariadic
					    ? std::min(Index + 1, Function.Parameters.size() - 1)
					    : Index + 1;
					const FFunctionParam* const Parameter = ParameterIndex < Function.Parameters.size()
					    ? &Function.Parameters[ParameterIndex]
					    : nullptr;
					Arguments << EmitScriptArgument(*Call.GetArguments()[Index], Parameter, Call.Location,
					    Parameter ? ParameterType(*Parameter) : std::string{});
				}
				for (std::size_t Index = Call.GetArguments().size() + 1;
				    Index < Function.Parameters.size(); ++Index)
				{
					const FFunctionParam& Parameter = Function.Parameters[Index];
					if (Parameter.bIsVariadic || !Parameter.DefaultValue)
					{
						break;
					}
					Arguments << ", " << EmitExpr(*Parameter.DefaultValue, ParameterType(Parameter));
				}

				std::ostringstream Result;
				Result << FunctionIdentifier(*Resolved.Decl);
				if (!Call.TypeArgs.empty())
				{
					Result << '<';
					for (std::size_t Index = 0; Index < Call.TypeArgs.size(); ++Index)
					{
						if (Index != 0)
						{
							Result << ", ";
						}
						Result << MapType(Call.TypeArgs[Index]);
					}
					Result << '>';
				}
				Result << '(' << Arguments.str() << ')';
				return Result.str();
			}

			std::string EmitCall(const FCallExpr& Call)
			{
				if (Call.Callee == "cast" && Call.TypeArgs.size() == 1 && Call.GetArguments().size() == 1 &&
				    Call.GetArguments()[0]->Kind == EExprKind::IntegerLiteral)
				{
					const auto Target = NormalizeType(ResolveAlias(Call.TypeArgs[0]));
					if (IsSignedIntegerType(Target) || IsUnsignedIntegerType(Target) ||
					    EnumUnderlyingTypes.count(CanonicalSymbol(Target)))
					{
						return "checked((" + MapType(Target) + ")(" + EmitExpr(*Call.GetArguments()[0]) + "))";
					}
				}

				if (Call.Callee == "__numeric_type" && Call.TypeArgs.size() == 1) return "VsNumericType<" + MapType(Call.TypeArgs[0]) + ">()";
				if (IsMethodBindingFactoryCall(Call) || IsWeakAttributeFactoryCall(Call))
				{
					const auto* Info = FindMethodBindingCodegenInfo(MethodBindings, Call.Location);
					if (!Info || !Info->bConstantName || Info->Selection.bHost ||
					    Call.Callee.find("weak") != std::string::npos)
					{
						Fail("C# AOT: weak, dynamic or host method binding requires the VeroScript runtime",
						    Call.Location);
						return "default!";
					}
					const bool Attribute = Info->FactoryName != "__std_bind_method";
					const std::string Function = Attribute ? "fn()->" + Call.TypeArgs[0] : Call.TypeArgs[0];
					std::vector<std::string> Parameters;
					std::string Return;
					Runtime::ParseFunctionType(Function, Parameters, Return);
					std::string TargetType = Call.TypeArgs.size() == 2 ? Call.TypeArgs[1] : InferExprType(*Call.GetArguments()[0]);
					std::string MethodType = TargetType, Payload;
					if (Info->bWrappedReceiver && IsBuiltinHandleType(TargetType, Payload))
					{
						MethodType = Payload;
					}

					const auto Saved = LocalTypes;
					std::vector<std::unique_ptr<FExpr>> Arguments;
					for (std::size_t I = 0; I < Parameters.size(); ++I)
					{
						const std::string Name = "___vs_arg" + std::to_string(I);
						LocalTypes[Name] = std::string(Runtime::StripReferenceTypeName(Parameters[I]));
						std::unique_ptr<FExpr> Arg = std::make_unique<FIdentifierExpr>(Name);
						if (Runtime::IsReferenceTypeName(Parameters[I]))
						{
							Arg = std::make_unique<FRefExpr>(std::move(Arg));
						}

						Arguments.push_back(std::move(Arg));
					}
					const auto Method = ResolveUserMethod(MethodType, Info->Selection.MethodName,
					    FExprArgumentView(nullptr, &Arguments), &Info->Selection);
					LocalTypes = Saved;
					if (!Method.Decl)
					{
						Fail("C# AOT: selected bound method cannot be represented", Call.Location);
						return "default!";
					}
					std::string LambdaParams, Forward;
					for (std::size_t I = 0; I < Parameters.size(); ++I)
					{
						if (I)
						{
							LambdaParams += ", ";
							Forward += ", ";
						}
						const std::string Ref = Runtime::IsReferenceTypeName(Parameters[I])
						    ? (Runtime::IsConstReferenceTypeName(Parameters[I]) ? "in " : "ref ")
						    : "";
						const std::string Name = "___vs_arg" + std::to_string(I);
						LambdaParams += Ref + MapType(Parameters[I]) + " " + Name;
						Forward += Ref + Name;
					}
					const std::string CallText = "___vs_target" +
					    std::string(Info->bWrappedReceiver ? ".value" : "") + "." +
					    EmittedUserMethodName(*Method.Decl) + "(" + Forward + ")";
					const std::string RefReturn = Runtime::IsReferenceTypeName(Return) ? "ref " : "";
					const std::string Bound = "VsBind<" + MapType(TargetType) + ", " + MapType(Function) +
					    ">(" + EmitExpr(*Call.GetArguments()[0]) + ", ___vs_target => (" + MapType(Function) +
					    ")((" + LambdaParams + ") => " + RefReturn + CallText + "))";
					return Attribute ? "VsBoundAttribute<" + MapType(Return) + ">(" + Bound + ")" : Bound;
				}

				const std::size_t GenericOpen = Call.Callee.find('<');
				const std::string ConstructorBase = GenericOpen == std::string::npos
				    ? Call.Callee
				    : Call.Callee.substr(0, GenericOpen);
				if ((ConstructorBase == "field_info" || ConstructorBase == "method_info") && (GenericOpen != std::string::npos || !Call.TypeArgs.empty()))
				{
					Fail("reflection descriptors are not supported by C# AOT; "
					     "use VeroScriptCSharp with the VM backend",
					    Call.Location);
					return "default!";
				}
				const FExprArgumentView TaskArgs = Call.GetArguments();
				if (Call.bStandardLibraryQualified && (Call.Callee == "__std_spawn" || Call.Callee == "__std_spawn_task") && !TaskArgs.empty() && ResolveAlias(InferExprType(*TaskArgs[0])) != "string")
				{
					Fail("callable task entry is not supported by C# AOT", Call.Location);
					return "default!";
				}
				std::string CallableType;
				const FExpr* CallableExpression = Call.CalleeExpr.get();
				if (CallableExpression)
				{
					CallableType = InferExprType(*CallableExpression);
				}
				else if (const auto Local = LocalTypes.find(Call.Callee); Local != LocalTypes.end())
				{
					CallableType = Local->second;
				}
				else if (const FResolvedUserField Field = ResolveUserField(
				             ActiveTypeName, Call.Callee);
				    Field.Decl)
				{
					CallableType = SubstituteUserTypeArguments(
					    Field.OwnerType, Field.Decl->TypeName);
				}
				else if (const FVarDeclStmt* Global = ResolveGlobal(Call.Callee))
				{
					if (const auto Found = GlobalTypes.find(Global); Found != GlobalTypes.end())
					{
						CallableType = Found->second;
					}
				}
				std::string AttributeBase;
				std::vector<std::string> AttributeArgs;
				if (SplitGenericType(ResolveAlias(CallableType), AttributeBase, AttributeArgs) &&
				    (AttributeBase == "attribute" && HasStandardAttribute()))
				{
					const std::string Object = CallableExpression
					    ? EmitExpr(*CallableExpression)
					    : (LocalEmissionNames.count(Call.Callee) ? LocalEmissionNames.at(Call.Callee)
					                                             : Identifier(Call.Callee));
					return Object + ".get()";
				}
				std::vector<std::string> CallableParams;
				std::string CallableReturn;
				if (Runtime::ParseFunctionType(ResolveAlias(CallableType),
				        CallableParams, CallableReturn))
				{
					if (CallableParams.size() != Call.GetArguments().size())
					{
						Fail("indirect callable argument count does not match its function type", Call.Location);
						return "default!";
					}
					std::ostringstream Result;
					if (CallableExpression)
					{
						Result << '(' << EmitExpr(*CallableExpression) << ')';
					}
					else if (LocalTypes.find(Call.Callee) != LocalTypes.end())
					{
						const auto Emitted = LocalEmissionNames.find(Call.Callee);
						Result << (Emitted == LocalEmissionNames.end()
						        ? Identifier(Call.Callee)
						        : Emitted->second);
					}
					else if (const FResolvedUserField Field = ResolveUserField(
					             ActiveTypeName, Call.Callee);
					    Field.Decl)
					{
						if (!ActiveThisEmission.empty() && !Field.Decl->bIsStatic)
						{
							Result << ActiveThisEmission << '.';
						}
						Result << EmittedUserFieldName(*Field.Decl);
					}
					else if (const FVarDeclStmt* Global = ResolveGlobal(Call.Callee))
					{
						Result << GlobalIdentifier(*Global);
					}
					Result << '(';
					for (std::size_t Index = 0; Index < Call.GetArguments().size(); ++Index)
					{
						if (Index != 0)
						{
							Result << ", ";
						}
						const std::string& ParameterType = CallableParams[Index];
						const FExpr* Argument = Call.GetArguments()[Index];
						if (Runtime::IsReferenceTypeName(ParameterType))
						{
							Result << (Runtime::IsConstReferenceTypeName(ParameterType) ? "in " : "ref ");
							if (Argument->Kind == EExprKind::Ref)
							{
								Argument = static_cast<const FRefExpr&>(*Argument).Operand.get();
							}
						}
						Result << EmitExpr(*Argument,
						    std::string(Runtime::StripReferenceTypeName(ParameterType)));
					}
					Result << ')';
					return Result.str();
				}
				if (CallableExpression)
				{
					Fail("indirect call target is not function-typed", Call.Location);
					return "default!";
				}
				const FResolvedCall ResolvedCall = ResolveCall(Call);
				std::vector<std::string> ArgumentTypes;
				for (const FExpr* Argument : Call.GetArguments())
				{
					ArgumentTypes.push_back(InferExprType(*Argument));
				}
				const auto TypeArguments = ResolveFunctionTypeArguments(
				    ResolvedCall, Call.TypeArgs, ArgumentTypes);
				std::ostringstream ArgumentStream;
				for (std::size_t Index = 0; Index < Call.GetArguments().size(); ++Index)
				{
					if (Index != 0)
					{
						ArgumentStream << ", ";
					}
					const FFunctionParam* Parameter = nullptr;
					if (ResolvedCall.Decl && !ResolvedCall.Decl->Parameters.empty())
					{
						const std::size_t ParameterIndex = ResolvedCall.Decl->Parameters.back().bIsVariadic
						    ? std::min(Index, ResolvedCall.Decl->Parameters.size() - 1)
						    : Index;
						if (ParameterIndex < ResolvedCall.Decl->Parameters.size())
						{
							Parameter = &ResolvedCall.Decl->Parameters[ParameterIndex];
						}
					}
					ArgumentStream << EmitScriptArgument(*Call.GetArguments()[Index], Parameter, Call.Location,
					    Parameter ? SubstituteFunctionTypeArguments(Parameter->TypeName, TypeArguments) : std::string{});
				}
				if (ResolvedCall.Decl)
				{
					for (std::size_t Index = Call.GetArguments().size();
					    Index < ResolvedCall.Decl->Parameters.size(); ++Index)
					{
						const FFunctionParam& Parameter = ResolvedCall.Decl->Parameters[Index];
						if (Parameter.bIsVariadic)
						{
							break;
						}
						if (!Parameter.DefaultValue)
						{
							break;
						}
						if (ArgumentStream.tellp() > 0)
						{
							ArgumentStream << ", ";
						}
						ArgumentStream << EmitExpr(*Parameter.DefaultValue,
						    SubstituteFunctionTypeArguments(Parameter.TypeName, TypeArguments));
					}
				}
				const std::string Args = ArgumentStream.str();
				const std::string& Name = Call.Callee;
				std::string ConstructedUserType = Name;
				if (!Call.TypeArgs.empty() && Name.find('<') == std::string::npos)
				{
					ConstructedUserType += '<';
					for (std::size_t Index = 0; Index < Call.TypeArgs.size(); ++Index)
					{
						if (Index != 0)
						{
							ConstructedUserType += ',';
						}
						ConstructedUserType += Call.TypeArgs[Index];
					}
					ConstructedUserType += '>';
				}
				std::string BuiltinBase;
				std::vector<std::string> BuiltinArgs;
				if (SplitGenericType(ConstructedUserType, BuiltinBase, BuiltinArgs) &&
				    ((BuiltinBase == "attribute" && HasStandardAttribute()) || BuiltinBase == "optional"))
				{
					return "new " + MapType(ConstructedUserType) + "(" + Args + ")";
				}

				if (BuiltinBase == "pair" && BuiltinArgs.size() == 2 && !HasUserPairTemplate())
				{
					return Call.GetArguments().empty()
					    ? DefaultValueForType(ConstructedUserType)
					    : "new " + MapType(ConstructedUserType) + "(" + Args + ")";
				}
				std::string HandlePayload;
				if (Name == "handle" && Call.TypeArgs.empty() && Call.GetArguments().size() == 1 && !HasNonGenericHandleType() && IsBuiltinHandleType("handle<int>", HandlePayload))
				{
					ConstructedUserType = "handle<" + InferExprType(*Call.GetArguments().front()) + ">";
				}
				if (IsBuiltinHandleType(ConstructedUserType, HandlePayload))
				{
					std::string PayloadValue;
					if (Call.GetArguments().empty())
					{
						// A struct field is initialized without executing its user
						// constructor in the VM. C# new/default cannot reproduce that
						// for arbitrary structs or an open payload parameter.
						if (ActiveTypeParameters.find(NormalizeType(HandlePayload)) != ActiveTypeParameters.end())
						{
							Fail("handle<T>() with an open payload type is not supported by C# source emission; "
							     "supply an explicit payload",
							    Call.Location);
						}
						PayloadValue = DefaultValueForType(HandlePayload);
						const std::string UserPayload = ResolveUserTypeSymbol(HandlePayload);
						for (const auto& Struct : Program->Structs)
						{
							if (!ShouldEmit(Struct.ModulePath) || AttributeModule(Struct.ModulePath))
							{
								continue;
							}
							if (CanonicalSymbol(Struct.Name) == UserPayload)
							{
								Fail("handle<T>() with a struct payload is not supported by C# source emission; "
								     "supply an explicit payload",
								    Call.Location);
							}
						}
					}
					else if (Call.GetArguments().size() == 1)
					{
						PayloadValue = EmitExpr(*Call.GetArguments().front(), HandlePayload);
					}
					else
					{
						Fail("handle<T> constructor accepts at most one payload", Call.Location);
						return "default!";
					}
					return "new " + MapType(ConstructedUserType) + "(" + PayloadValue + ")";
				}
				if (!ResolveUserTypeSymbol(ConstructedUserType).empty())
				{
					const FResolvedUserConstructor Constructor = ResolveUserConstructor(
					    ConstructedUserType, Call.GetArguments());
					if (Constructor.Decl)
					{
						return "new " + MapType(ConstructedUserType) + "(" + EmitUserCallArguments(Call.GetArguments(), Constructor.Decl->Parameters, Constructor.OwnerType, Call.Location) + ")";
					}
					if (Call.GetArguments().empty())
					{
						return "new " + MapType(ConstructedUserType) + "()";
					}
					Fail("could not resolve user constructor for '" + ConstructedUserType + "'",
					    Call.Location);
					return "default!";
				}
				if (!ActiveTypeName.empty())
				{
					const FResolvedUserMethod Method = ResolveUserMethod(
					    ActiveTypeName, Name, Call.GetArguments());
					if (Method.Decl)
					{
						return ((!ActiveThisEmission.empty() && !Method.Decl->bIsStatic)
						               ? ActiveThisEmission + "."
						               : std::string{}) +
						    EmittedUserMethodName(*Method.Decl) + "(" + EmitUserCallArguments(Call.GetArguments(), Method.Decl->Parameters, Method.OwnerType, Call.Location) + ")";
					}
				}
				const std::size_t ContainerOpen = Name.find('<');
				const std::string ContainerBase = ContainerOpen == std::string::npos
				    ? Name
				    : Name.substr(0, ContainerOpen);
				if ((ContainerBase == "array" || ContainerBase == "deque" || ContainerBase == "map" || ContainerBase == "ordered_map" || ContainerBase == "multimap" || ContainerBase == "ordered_multimap" || ContainerBase == "set" || ContainerBase == "multiset" || ContainerBase == "ordered_multiset" || ContainerBase == "queue" || ContainerBase == "priority_queue" || ContainerBase == "stack") && (ContainerOpen != std::string::npos || !Call.TypeArgs.empty()))
				{
					if (ContainerOpen != std::string::npos)
					{
						return "new " + MapType(Name) + "(" + Args + ")";
					}
					std::ostringstream Type;
					Type << ContainerBase << '<';
					for (std::size_t Index = 0; Index < Call.TypeArgs.size(); ++Index)
					{
						if (Index != 0)
						{
							Type << ',';
						}
						Type << Call.TypeArgs[Index];
					}
					Type << '>';
					return "new " + MapType(Type.str()) + "(" + Args + ")";
				}
				if (Name == "print")
				{
					if (Call.GetArguments().size() == 1)
					{
						return "VsPrint(" + EmitTextValue(*Call.GetArguments().front()) + ")";
					}
					return "VsPrintFormat(" + EmitFormatArguments(Call) + ")";
				}
				if (Name == "eprint")
				{
					return "VsEprint(" + EmitTextValue(*Call.GetArguments().front()) + ")";
				}
				if (Call.bStandardLibraryQualified && Name == "console_write")
				{
					return "VsConsoleWrite(false, VsText(" + Args + "))";
				}
				if (Call.bStandardLibraryQualified && Name == "console_error")
				{
					return "VsConsoleWrite(true, VsText(" + Args + "))";
				}
				if (Call.bStandardLibraryQualified && Name == "console_flush")
				{
					return "Console.Out.Flush()";
				}
				if (Call.bStandardLibraryQualified && Name == "console_clear")
				{
					return "VsConsoleClear()";
				}
				if (Call.bStandardLibraryQualified && Name == "console_supports_color")
				{
					return "VsConsoleSupportsColor(" + Args + ")";
				}
				if (Call.bStandardLibraryQualified && Name == "console_supports_cursor")
				{
					return "VsConsoleSupportsCursor(" + Args + ")";
				}
				if (Call.bStandardLibraryQualified && Name == "console_supports_clear")
				{
					return "VsConsoleSupportsClear(" + Args + ")";
				}
				if (Call.bStandardLibraryQualified && Name == "console_is_output_terminal")
				{
					return "VsConsoleIsOutputTerminal(" + Args + ")";
				}
				if (Call.bStandardLibraryQualified && Name == "console_set_color")
				{
					return "VsConsoleSetColor(VsText(" + EmitExpr(*Call.Arguments[0]) + "), VsText(" +
					    EmitExpr(*Call.Arguments[1]) + "))";
				}
				if (Call.bStandardLibraryQualified && Name == "console_reset_style")
				{
					return "VsConsoleResetStyle(" + Args + ")";
				}
				if (Call.bStandardLibraryQualified && Name == "console_clear_line")
				{
					return "VsConsoleClearLine(VsText(" + Args + "))";
				}
				if (Call.bStandardLibraryQualified && Name == "console_move_cursor")
				{
					return "VsConsoleMoveCursor(VsToInt64(" + EmitExpr(*Call.Arguments[0]) + "), VsToInt64(" +
					    EmitExpr(*Call.Arguments[1]) + "))";
				}
				if (Call.bStandardLibraryQualified && Name == "console_set_cursor_visible")
				{
					return "VsConsoleSetCursorVisible(" + Args + ")";
				}
				if (Call.bStandardLibraryQualified && Name == "console_width")
				{
					return "VsConsoleWidth(" + Args + ")";
				}
				if (Call.bStandardLibraryQualified && Name == "console_height")
				{
					return "VsConsoleHeight(" + Args + ")";
				}
				if (Call.bStandardLibraryQualified && Name == "console_is_interactive")
				{
					return "VsConsoleIsInteractive()";
				}
				if (Name == "format" || Name == "__format")
				{
					return "VsFormat(" + EmitFormatArguments(Call) + ")";
				}
				if (Name == "printf" || Name == "formatf" || Name == "__format_printf")
				{
					return Name == "printf"
					    ? "VsPrintfFormat(" + EmitFormatArguments(Call) + ")"
					    : "VsFormatPrintf(" + EmitFormatArguments(Call) + ")";
				}
				if (Name == "len" || Name == "__array_prepare_sort")
				{
					return "VsLen(" + Args + ")";
				}
				if (Name == "__string_starts_with") return "VsStringStarts(" + Args + ")";
				if (Name == "__string_ends_with") return "VsStringEnds(" + Args + ")";
				if (Name == "__string_last_find") return "VsStringLastFind(" + Args + ")";
				if (Name == "__string_repeat") return "VsStringRepeat(" + Args + ")";
				if (Name == "__string_replace") return "VsStringReplace(" + Args + ")";
				if (Name == "__string_trim") return "VsStringTrim(" + Args + ")";
				if (Name == "__array_reverse") return "VsArrayReverse(" + Args + ")";
				if (Name == "__array_rotate") return "VsArrayRotate(" + Args + ")";
				if (Name == "__array_shuffle") return "VsArrayShuffle(" + Args + ")";
				if (Name == "__random_next_uint") return "VsRandomNext(" + Args + ")";
				if (Name == "__random_index") return "VsRandomIndex(" + Args + ")";
				if (Name == "__find")
				{
					return "VsFind(" + Args + ")";
				}
				if (Name == "__substr")
				{
					return "VsSubstr(" + Args + ")";
				}
				if (Name == "__char_code")
				{
					return "VsCharCode(" + Args + ")";
				}
				if (Name == "assert")
				{
					return "VsAssert(" + Args + ")";
				}
				if (Name == "exit")
				{
					return "VsExit(" + Args + ")";
				}
				if (Name == "range")
				{
					return "VsRange(" + Args + ")";
				}
				if (Call.bStandardLibraryQualified && Name == "now")
				{
					return "global::System.DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() / 1000.0";
				}
				if (Call.bStandardLibraryQualified && Name == "steady_now")
				{
					return "(double)global::System.Diagnostics.Stopwatch.GetTimestamp() / global::System.Diagnostics.Stopwatch.Frequency";
				}
				if (Name == "to_string")
				{
					return "VsToString(" + EmitTextValue(*Call.GetArguments().front()) + ")";
				}
				if (Name == "to_wstring")
				{
					return "VsToWString(" + EmitTextValue(*Call.GetArguments().front()) + ")";
				}
				if (Name == "__to_int8")
				{
					return "VsToInt8(" + Args + ")";
				}
				if (Name == "__to_uint8")
				{
					return "VsToUInt8(" + Args + ")";
				}
				if (Name == "__to_int16")
				{
					return "VsToInt16(" + Args + ")";
				}
				if (Name == "__to_uint16")
				{
					return "VsToUInt16(" + Args + ")";
				}
				if (Name == "to_int")
				{
					return "VsToInt(" + Args + ")";
				}
				if (Name == "to_int64")
				{
					return "VsToInt64(" + Args + ")";
				}
				if (Name == "to_unsigned")
				{
					return "VsToUnsigned(" + Args + ")";
				}
				if (Name == "to_uint64")
				{
					return "VsToUInt64(" + Args + ")";
				}
				if (Name == "to_float")
				{
					return "VsToFloat(" + Args + ")";
				}
				if (Name == "to_double")
				{
					return "VsToDouble(" + Args + ")";
				}
				if (Name == "abs")
				{
					return "VsAbs(" + Args + ")";
				}
				if (Name == "min" || Name == "max")
				{
					const std::string Type = NormalizeType(InferExprType(Call));
					if (!Type.empty() && Call.GetArguments().size() == 2)
					{
						return std::string(Name == "min" ? "VsMin(" : "VsMax(") + "(" + MapType(Type) + ")(" + EmitExpr(*Call.GetArguments()[0]) + "), (" + MapType(Type) + ")(" + EmitExpr(*Call.GetArguments()[1]) + "))";
					}
				}
				if (Name == "__sqrt")
				{
					return "Math.Sqrt(VsToDouble(" + Args + "))";
				}
				if (Name == "__sin")
				{
					return "Math.Sin(VsToDouble(" + Args + "))";
				}
				if (Name == "__cos")
				{
					return "Math.Cos(VsToDouble(" + Args + "))";
				}
				if (Name == "__tan")
				{
					return "Math.Tan(VsToDouble(" + Args + "))";
				}
				if (Name == "__floor")
				{
					return "Math.Floor(VsToDouble(" + Args + "))";
				}
				if (Name == "__ceil")
				{
					return "Math.Ceiling(VsToDouble(" + Args + "))";
				}
				if (Name == "__round")
				{
					return "Math.Round(VsToDouble(" + Args + "), MidpointRounding.AwayFromZero)";
				}
				if (Name == "__trunc")
				{
					return "Math.Truncate(VsToDouble(" + Args + "))";
				}
				if (Name == "__pow")
				{
					return "Math.Pow(" + Args + ")";
				}
				if (Name == "__exp")
				{
					return "Math.Exp(VsToDouble(" + Args + "))";
				}
				if (Name == "__log")
				{
					return "Math.Log(VsToDouble(" + Args + "))";
				}
				if (Name == "__log2")
				{
					return "Math.Log2(VsToDouble(" + Args + "))";
				}
				if (Name == "__asin")
				{
					return "Math.Asin(VsToDouble(" + Args + "))";
				}
				if (Name == "__acos")
				{
					return "Math.Acos(VsToDouble(" + Args + "))";
				}
				if (Name == "__atan2")
				{
					if (Call.GetArguments().size() != 2)
					{
						Fail("__atan2 expects two arguments", Call.Location);
						return "default!";
					}
					return "Math.Atan2(VsToDouble(" + EmitExpr(*Call.GetArguments()[0]) + "), VsToDouble(" + EmitExpr(*Call.GetArguments()[1]) + "))";
				}
				if (Name == "__isfinite")
				{
					return "double.IsFinite(VsToDouble(" + Args + "))";
				}
				if (Name == "__isnan")
				{
					return "double.IsNaN(VsToDouble(" + Args + "))";
				}
				if (HostBindings)
				{
					if (const FHostFunctionDesc* Function = HostBindings->FindFunction(Name))
					{
						std::ostringstream Result;
						Result << HostMethodName("function:" + Name) << '(';
						for (std::size_t Index = 0; Index < Call.GetArguments().size(); ++Index)
						{
							if (Index != 0)
							{
								Result << ", ";
							}
							const std::uint8_t Flag = Index < Function->ParamFlags.size()
							    ? Function->ParamFlags[Index]
							    : 0;
							Result << EmitHostArgument(*Call.GetArguments()[Index], Flag, Call.Location);
						}
						Result << ')';
						return Result.str();
					}
					if (const FHostTypeDesc* Type = ResolveHostType(Name))
					{
						if (const FHostMemberDesc* Constructor = ResolveHostMember(*Type, Type->Name,
						        {EHostMemberKind::Constructor}, Call.GetArguments()))
						{
							return EmitHostMemberCall(*Type, *Constructor, Call.GetArguments());
						}
					}
				}

				const std::string FunctionSymbol = ResolvedCall.Symbol.empty()
				    ? ResolveFunctionSymbol(Name)
				    : ResolvedCall.Symbol;
				if (FunctionNames.find(FunctionSymbol) == FunctionNames.end())
				{
					Fail("builtin or intrinsic '" + Name + "' is not implemented", Call.Location);
					return "default!";
				}
				std::ostringstream Result;
				if (ResolvedCall.Decl)
				{
					Result << FunctionIdentifier(*ResolvedCall.Decl);
				}
				else
				{
					Result << FunctionIdentifierForSymbol(FunctionSymbol);
				}
				if (!Call.TypeArgs.empty())
				{
					Result << '<';
					for (std::size_t Index = 0; Index < Call.TypeArgs.size(); ++Index)
					{
						if (Index != 0)
						{
							Result << ", ";
						}
						Result << MapType(Call.TypeArgs[Index]);
					}
					Result << '>';
				}
				Result << '(' << Args << ')';
				return Result.str();
			}

			std::string ConvertForTarget(
			    const std::string& Expression,
			    const std::string& SourceType,
			    const std::string& TargetType)
			{
				std::string AttributeBase;
				std::vector<std::string> AttributeArgs;
				if (SplitGenericType(ResolveAlias(SourceType), AttributeBase, AttributeArgs) &&
				    (AttributeBase == "attribute" && HasStandardAttribute()) &&
				    NormalizeType(ResolveAlias(SourceType)) != NormalizeType(ResolveAlias(TargetType)))
				{
					return "(" + Expression + ").get()";
				}

				if (SplitGenericType(ResolveAlias(TargetType), AttributeBase, AttributeArgs) &&
				    (AttributeBase == "attribute" && HasStandardAttribute()) &&
				    NormalizeType(ResolveAlias(SourceType)) != NormalizeType(ResolveAlias(TargetType)))
				{
					return "new " + MapType(TargetType) + "(" + Expression + ")";
				}

				std::string Target = NormalizeType(ResolveAlias(TargetType));
				std::string Source = NormalizeType(ResolveAlias(SourceType));
				if (Target.empty() || Target == "auto" || Target == Source) return Expression;
				std::string HandlePayload;
				if (Source != "nullptr" && IsBuiltinHandleType(Target, HandlePayload))
				{
					return "new " + MapType(Target) + "(" + ConvertForTarget(Expression, Source, HandlePayload) + ")";
				}
				if (const auto Enum = EnumUnderlyingTypes.find(CanonicalSymbol(Target));
				    Enum != EnumUnderlyingTypes.end() && ScopedEnumTypes.find(CanonicalSymbol(Target)) == ScopedEnumTypes.end())
				{
					Target = NormalizeType(Enum->second);
				}
				if (const auto Enum = EnumUnderlyingTypes.find(CanonicalSymbol(Source));
				    Enum != EnumUnderlyingTypes.end())
				{
					Source = NormalizeType(Enum->second);
				}
				if (!IsNumericType(Target) || !IsNumericType(Source))
				{
					return Expression;
				}
				if (IsSignedIntegerType(Target) || IsUnsignedIntegerType(Target))
				{
					if (Source == "float" || Source == "double")
					{
						const char* Conversion = Target == "char" ? "VsToInt8"
						    : Target == "unsigned char"           ? "VsToUInt8"
						    : Target == "short"                   ? "VsToInt16"
						    : Target == "unsigned short"          ? "VsToUInt16"
						    : Target == "int"                     ? "VsToInt"
						    : Target == "unsigned int"            ? "VsToUnsigned"
						    : Target == "int64"                   ? "VsToInt64"
						                                          : "VsToUInt64";
						return std::string(Conversion) + "(" + Expression + ")";
					}
					return "checked((" + MapType(Target) + ")(" + Expression + "))";
				}
				if (Target == "float")
				{
					return "VsToFloat(" + Expression + ")";
				}
				if (Target == "double")
				{
					return "((double)(" + Expression + "))";
				}
				return Expression;
			}

			std::string EmitFloatingOperation(
			    const std::string& Left,
			    const std::string& LeftType,
			    const std::string& Right,
			    const std::string& RightType,
			    const EBinaryOp Operation,
			    const std::string& PromotedType)
			{
				const std::string ConvertedLeft = ConvertForTarget(Left, LeftType, PromotedType);
				const std::string ConvertedRight = ConvertForTarget(Right, RightType, PromotedType);
				if (Operation == EBinaryOp::Divide || Operation == EBinaryOp::Modulo)
				{
					return std::string(Operation == EBinaryOp::Divide ? "VsDivide(" : "VsRemainder(") + ConvertedLeft + ", " + ConvertedRight + ")";
				}
				return "(" + ConvertedLeft + " " + BinaryOperator(Operation) + " " + ConvertedRight + ")";
			}

			std::string EmitExpr(const FExpr& Expr, const std::string& ExpectedType = {})
			{
				std::string HandlePayload;
				if (Expr.Kind == EExprKind::Call && IsBuiltinHandleType(ExpectedType, HandlePayload))
				{
					const auto& Call = static_cast<const FCallExpr&>(Expr);
					if (Call.Callee == "handle" && Call.TypeArgs.empty() && !HasNonGenericHandleType())
					{
						FCallExpr TypedCall(ResolveAlias(ExpectedType), {});
						TypedCall.Location = Call.Location;
						TypedCall.BorrowArgumentsFrom(Call.GetArguments());
						return EmitCall(TypedCall);
					}
				}
				std::string Expression = EmitExprCore(Expr, ExpectedType);
				if (Expr.Kind == EExprKind::Assign)
				{
					Expression = '(' + Expression + ')';
				}
				return ExpectedType.empty() ? Expression
				                            : ConvertForTarget(Expression, InferExprType(Expr), ExpectedType);
			}

			std::string EmitStatementExpression(const FExpr& Expr)
			{
				// C# permits an assignment as a statement, but not a parenthesized
				// assignment. Nested value expressions still go through EmitExpr.
				return EmitExprCore(Expr);
			}

			void CollectReferencedLocals(
			    const FExpr& Expr,
			    const std::unordered_set<std::string>& Candidates,
			    std::unordered_set<std::string>& OutNames,
			    std::unordered_set<std::string>* const OutTypes = nullptr,
			    std::unordered_set<std::string>* const OutBindingChanges = nullptr) const
			{
				const auto VisitType = [&](const std::string& TypeName)
				{
					if (OutTypes && !TypeName.empty())
					{
						OutTypes->insert(TypeName);
					}
				};
				const auto Visit = [&](const std::unique_ptr<FExpr>& Value)
				{
					if (Value)
					{
						CollectReferencedLocals(*Value, Candidates, OutNames, OutTypes, OutBindingChanges);
					}
				};
				switch (Expr.Kind)
				{
				case EExprKind::CharacterLiteral:
					return;
				case EExprKind::Identifier:
					{
						const std::string& Name = static_cast<const FIdentifierExpr&>(Expr).Name;
						if (Candidates.find(Name) != Candidates.end())
						{
							OutNames.insert(Name);
						}
						return;
					}
				case EExprKind::Unary:
					{
						Visit(static_cast<const FUnaryExpr&>(Expr).Operand);
						return;
					}
				case EExprKind::Binary:
					{
						Visit(static_cast<const FBinaryExpr&>(Expr).Left);
						Visit(static_cast<const FBinaryExpr&>(Expr).Right);
						return;
					}
				case EExprKind::Assign:
					{
						const auto& Assign = static_cast<const FAssignExpr&>(Expr);
						if (OutBindingChanges && !Assign.bIsCompound && Assign.Target && Assign.Target->Kind == EExprKind::Identifier)
						{
							const auto& Name = static_cast<const FIdentifierExpr&>(*Assign.Target).Name;
							if (Candidates.count(Name))
							{
								OutBindingChanges->insert(Name);
							}
						}
						Visit(Assign.Target);
						Visit(static_cast<const FAssignExpr&>(Expr).Value);
						return;
					}
				case EExprKind::Conditional:
					{
						Visit(static_cast<const FConditionalExpr&>(Expr).Condition);
						Visit(static_cast<const FConditionalExpr&>(Expr).ThenExpr);
						Visit(static_cast<const FConditionalExpr&>(Expr).ElseExpr);
						return;
					}
				case EExprKind::Call:
					{
						const auto& Call = static_cast<const FCallExpr&>(Expr);
						for (const auto& Type : Call.TypeArgs)
						{
							VisitType(Type);
						}
						if (Candidates.find(Call.Callee) != Candidates.end())
						{
							OutNames.insert(Call.Callee);
						}
						Visit(Call.CalleeExpr);
						for (const FExpr* Argument : Call.GetArguments())
						{
							if (Argument)
							{
								CollectReferencedLocals(*Argument, Candidates, OutNames, OutTypes, OutBindingChanges);
							}
						}
						return;
					}
				case EExprKind::MemberAccess:
					{
						Visit(static_cast<const FMemberAccessExpr&>(Expr).Object);
						return;
					}
				case EExprKind::MethodCall:
					{
						const auto& Call = static_cast<const FMethodCallExpr&>(Expr);
						for (const auto& Type : Call.TypeArgs)
						{
							VisitType(Type);
						}
						Visit(Call.Object);
						for (const FExpr* Argument : Call.GetArguments())
						{
							if (Argument)
							{
								CollectReferencedLocals(*Argument, Candidates, OutNames, OutTypes, OutBindingChanges);
							}
						}
						return;
					}
				case EExprKind::IndexAccess:
					{
						Visit(static_cast<const FIndexAccessExpr&>(Expr).Object);
						Visit(static_cast<const FIndexAccessExpr&>(Expr).Index);
						return;
					}
				case EExprKind::ArrayInit:
					{
						VisitType(static_cast<const FArrayInitExpr&>(Expr).TypeName);
						for (const auto& Element : static_cast<const FArrayInitExpr&>(Expr).Elements)
						{
							Visit(Element);
						}
						return;
					}
				case EExprKind::MapInit:
					{
						VisitType(static_cast<const FMapInitExpr&>(Expr).TypeName);
						for (const FMapInitEntry& Entry : static_cast<const FMapInitExpr&>(Expr).Entries)
						{
							Visit(Entry.Key);
							Visit(Entry.Value);
						}
						return;
					}
				case EExprKind::Ref:
					{
						const auto& Ref = static_cast<const FRefExpr&>(Expr);
						if (OutBindingChanges && Ref.Operand && Ref.Operand->Kind == EExprKind::Identifier)
						{
							const auto& Name = static_cast<const FIdentifierExpr&>(*Ref.Operand).Name;
							if (Candidates.count(Name))
							{
								OutBindingChanges->insert(Name);
							}
						}
						Visit(Ref.Operand);
						return;
					}
				case EExprKind::Lambda:
					{
						const auto& Nested = static_cast<const FLambdaExpr&>(Expr);
						for (const auto& Capture : Nested.Captures)
						{
							if (Capture.Initializer)
							{
								Visit(Capture.Initializer);
							}
							else if (Candidates.count(Capture.Name))
							{
								OutNames.insert(Capture.Name);
							}
						}
						VisitType(Nested.ReturnType);
						auto NestedCandidates = Candidates;
						for (const auto& Capture : Nested.Captures)
						{
							if (Capture.Initializer)
							{
								NestedCandidates.erase(Capture.Name);
							}
						}
						for (const FFunctionParam& Parameter : Nested.Parameters)
						{
							VisitType(Parameter.TypeName);
							NestedCandidates.erase(Parameter.Name);
						}
						std::unordered_set<std::string> NestedChanges;
						auto* const NestedBindingChanges = OutBindingChanges ? &NestedChanges : nullptr;
						if (Nested.Body)
						{
							CollectReferencedLocals(*Nested.Body, NestedCandidates, OutNames, OutTypes, NestedBindingChanges);
						}
						if (Nested.ExpressionBody)
						{
							CollectReferencedLocals(*Nested.ExpressionBody, NestedCandidates, OutNames, OutTypes, NestedBindingChanges);
						}
						for (const auto& Name : NestedChanges)
						{
							const bool bReferenceDefault = Nested.CaptureKind == ELambdaCaptureKind::Reference && !Nested.HasCapture(Name, false);
							if (bReferenceDefault ||
							    Nested.HasCapture(Name, true))
							{
								OutBindingChanges->insert(Name);
							}
						}
						return;
					}
				default:
					return;
				}
			}

			void CollectReferencedLocals(
			    const FStmt& Statement,
			    const std::unordered_set<std::string>& Candidates,
			    std::unordered_set<std::string>& OutNames,
			    std::unordered_set<std::string>* const OutTypes = nullptr,
			    std::unordered_set<std::string>* const OutBindingChanges = nullptr) const
			{
				const auto VisitType = [&](const std::string& TypeName)
				{
					if (OutTypes && !TypeName.empty())
					{
						OutTypes->insert(TypeName);
					}
				};
				const auto VisitExpr = [&](const std::unique_ptr<FExpr>& Value)
				{
					if (Value)
					{
						CollectReferencedLocals(*Value, Candidates, OutNames, OutTypes, OutBindingChanges);
					}
				};
				const auto VisitBlock = [&](const std::unique_ptr<FBlockStmt>& Value)
				{
					if (Value)
					{
						CollectReferencedLocals(*Value, Candidates, OutNames, OutTypes, OutBindingChanges);
					}
				};
				switch (Statement.Kind)
				{
				case EStmtKind::Block:
					{
						auto BlockCandidates = Candidates;
						for (const auto& Child : static_cast<const FBlockStmt&>(Statement).Statements)
						{
							if (Child)
							{
								CollectReferencedLocals(*Child, BlockCandidates, OutNames, OutTypes, OutBindingChanges);
							}
							if (Child && Child->Kind == EStmtKind::VarDecl)
							{
								BlockCandidates.erase(static_cast<const FVarDeclStmt&>(*Child).Name);
							}
							else if (Child && Child->Kind == EStmtKind::VarDeclGroup)
							{
								for (const auto& Declaration : static_cast<const FVarDeclGroupStmt&>(*Child).Declarations)
								{
									if (Declaration)
									{
										BlockCandidates.erase(Declaration->Name);
									}
								}
							}
						}
						return;
					}
				case EStmtKind::Expr:
					{
						VisitExpr(static_cast<const FExprStmt&>(Statement).Expression);
						return;
					}
				case EStmtKind::TypeAlias:
					{
						VisitType(static_cast<const FTypeAliasStmt&>(Statement).TypeName);
						return;
					}
				case EStmtKind::VarDecl:
					{
						VisitType(static_cast<const FVarDeclStmt&>(Statement).TypeName);
						VisitExpr(static_cast<const FVarDeclStmt&>(Statement).Initializer);
						return;
					}
				case EStmtKind::VarDeclGroup:
					{
						auto GroupCandidates = Candidates;
						for (const auto& Declaration : static_cast<const FVarDeclGroupStmt&>(Statement).Declarations)
						{
							if (!Declaration)
							{
								continue;
							}
							VisitType(Declaration->TypeName);
							if (Declaration->Initializer)
							{
								CollectReferencedLocals(
								    *Declaration->Initializer, GroupCandidates, OutNames, OutTypes, OutBindingChanges);
							}
							GroupCandidates.erase(Declaration->Name);
						}
						return;
					}
				case EStmtKind::Return:
					{
						VisitExpr(static_cast<const FReturnStmt&>(Statement).Value);
						return;
					}
				case EStmtKind::Throw:
					{
						VisitExpr(static_cast<const FThrowStmt&>(Statement).Value);
						return;
					}
				case EStmtKind::If:
					{
						VisitExpr(static_cast<const FIfStmt&>(Statement).Condition);
						VisitBlock(static_cast<const FIfStmt&>(Statement).ThenBranch);
						VisitBlock(static_cast<const FIfStmt&>(Statement).ElseBranch);
						return;
					}
				case EStmtKind::While:
					{
						VisitExpr(static_cast<const FWhileStmt&>(Statement).Condition);
						VisitBlock(static_cast<const FWhileStmt&>(Statement).Body);
						return;
					}
				case EStmtKind::DoWhile:
					{
						VisitBlock(static_cast<const FDoWhileStmt&>(Statement).Body);
						VisitExpr(static_cast<const FDoWhileStmt&>(Statement).Condition);
						return;
					}
				case EStmtKind::For:
					{
						const auto& For = static_cast<const FForStmt&>(Statement);
						auto LoopCandidates = Candidates;
						if (For.Init)
						{
							CollectReferencedLocals(*For.Init, LoopCandidates, OutNames, OutTypes, OutBindingChanges);
						}
						if (For.Init && For.Init->Kind == EStmtKind::VarDecl)
						{
							LoopCandidates.erase(static_cast<const FVarDeclStmt&>(*For.Init).Name);
						}
						else if (For.Init && For.Init->Kind == EStmtKind::VarDeclGroup)
						{
							for (const auto& Declaration : static_cast<const FVarDeclGroupStmt&>(*For.Init).Declarations)
							{
								if (Declaration)
								{
									LoopCandidates.erase(Declaration->Name);
								}
							}
						}
						for (const auto& InitExpression : For.InitExpressions)
						{
							if (InitExpression)
							{
								CollectReferencedLocals(
								    *InitExpression, LoopCandidates, OutNames, OutTypes, OutBindingChanges);
							}
						}
						if (For.Condition)
						{
							CollectReferencedLocals(*For.Condition, LoopCandidates, OutNames, OutTypes, OutBindingChanges);
						}
						if (For.Step)
						{
							CollectReferencedLocals(*For.Step, LoopCandidates, OutNames, OutTypes, OutBindingChanges);
						}
						for (const auto& StepExpression : For.StepExpressions)
						{
							if (StepExpression)
							{
								CollectReferencedLocals(
								    *StepExpression, LoopCandidates, OutNames, OutTypes, OutBindingChanges);
							}
						}
						if (For.Body)
						{
							CollectReferencedLocals(*For.Body, LoopCandidates, OutNames, OutTypes, OutBindingChanges);
						}
						return;
					}
				case EStmtKind::ForEach:
					{
						const auto& For = static_cast<const FForEachStmt&>(Statement);
						VisitType(For.ElementTypeName);
						VisitType(For.KeyTypeName);
						VisitExpr(For.Collection);
						auto LoopCandidates = Candidates;
						LoopCandidates.erase(For.ElementName);
						LoopCandidates.erase(For.KeyName);
						if (For.Body)
						{
							CollectReferencedLocals(*For.Body, LoopCandidates, OutNames, OutTypes, OutBindingChanges);
						}
						return;
					}
				case EStmtKind::Switch:
					{
						const auto& Switch = static_cast<const FSwitchStmt&>(Statement);
						VisitExpr(Switch.Discriminant);
						for (const FSwitchCase& Case : Switch.Cases)
						{
							for (const auto& Label : Case.Labels)
							{
								VisitExpr(Label);
							}
							VisitBlock(Case.Body);
						}
						return;
					}
				case EStmtKind::Try:
					{
						const auto& Try = static_cast<const FTryStmt&>(Statement);
						VisitBlock(Try.TryBlock);
						for (const FCatchClause& Clause : Try.Catches)
						{
							VisitType(Clause.TypeName);
							auto CatchCandidates = Candidates;
							if (!Clause.Name.empty())
							{
								CatchCandidates.erase(Clause.Name);
							}
							if (Clause.Body)
							{
								CollectReferencedLocals(*Clause.Body, CatchCandidates, OutNames, OutTypes, OutBindingChanges);
							}
						}
						VisitBlock(Try.FinallyBlock);
						return;
					}
				default:
					return;
				}
			}

			bool ReferencesActiveReceiver(
			    const FExpr& Expr,
			    const std::unordered_set<std::string>& ShadowedNames) const
			{
				const auto Visit = [&](const std::unique_ptr<FExpr>& Value)
				{
					return Value && ReferencesActiveReceiver(*Value, ShadowedNames);
				};
				switch (Expr.Kind)
				{
				case EExprKind::This:
					return true;
				case EExprKind::CharacterLiteral:
					return false;
				case EExprKind::Identifier:
					{
						const std::string& Name = static_cast<const FIdentifierExpr&>(Expr).Name;
						if (ShadowedNames.find(Name) != ShadowedNames.end())
						{
							return false;
						}
						const FResolvedUserField Field = ResolveUserField(ActiveTypeName, Name);
						return Field.Decl && !Field.Decl->bIsStatic;
					}
				case EExprKind::Unary:
					return Visit(static_cast<const FUnaryExpr&>(Expr).Operand);
				case EExprKind::Binary:
					return Visit(static_cast<const FBinaryExpr&>(Expr).Left) || Visit(static_cast<const FBinaryExpr&>(Expr).Right);
				case EExprKind::Assign:
					return Visit(static_cast<const FAssignExpr&>(Expr).Target) || Visit(static_cast<const FAssignExpr&>(Expr).Value);
				case EExprKind::Conditional:
					return Visit(static_cast<const FConditionalExpr&>(Expr).Condition) || Visit(static_cast<const FConditionalExpr&>(Expr).ThenExpr) || Visit(static_cast<const FConditionalExpr&>(Expr).ElseExpr);
				case EExprKind::Call:
					{
						const auto& Call = static_cast<const FCallExpr&>(Expr);
						if (Visit(Call.CalleeExpr))
						{
							return true;
						}
						if (!Call.CalleeExpr && ShadowedNames.find(Call.Callee) == ShadowedNames.end())
						{
							const FResolvedUserField Field = ResolveUserField(
							    ActiveTypeName, Call.Callee);
							if (Field.Decl && !Field.Decl->bIsStatic)
							{
								return true;
							}
							const FResolvedUserMethod Method = ResolveUserMethod(
							    ActiveTypeName, Call.Callee, Call.GetArguments());
							if (Method.Decl && !Method.Decl->bIsStatic)
							{
								return true;
							}
						}
						for (const auto& Argument : Call.Arguments)
						{
							if (Visit(Argument))
							{
								return true;
							}
						}
						return false;
					}
				case EExprKind::MemberAccess:
					return Visit(static_cast<const FMemberAccessExpr&>(Expr).Object);
				case EExprKind::MethodCall:
					{
						const auto& Call = static_cast<const FMethodCallExpr&>(Expr);
						if (Visit(Call.Object))
						{
							return true;
						}
						for (const auto& Argument : Call.Arguments)
						{
							if (Visit(Argument))
							{
								return true;
							}
						}
						return false;
					}
				case EExprKind::IndexAccess:
					return Visit(static_cast<const FIndexAccessExpr&>(Expr).Object) || Visit(static_cast<const FIndexAccessExpr&>(Expr).Index);
				case EExprKind::ArrayInit:
					{
						for (const auto& Element : static_cast<const FArrayInitExpr&>(Expr).Elements)
						{
							if (Visit(Element))
							{
								return true;
							}
						}
						return false;
					}
				case EExprKind::MapInit:
					{
						for (const FMapInitEntry& Entry : static_cast<const FMapInitExpr&>(Expr).Entries)
						{
							if (Visit(Entry.Key) || Visit(Entry.Value))
							{
								return true;
							}
						}
						return false;
					}
				case EExprKind::Ref:
					return Visit(static_cast<const FRefExpr&>(Expr).Operand);
				case EExprKind::Lambda:
					{
						const auto& Nested = static_cast<const FLambdaExpr&>(Expr);
						auto NestedShadowed = ShadowedNames;
						for (const auto& Capture : Nested.Captures)
						{
							if (Capture.Initializer)
							{
								if (Visit(Capture.Initializer)) return true;
								NestedShadowed.insert(Capture.Name);
							}
						}
						for (const FFunctionParam& Parameter : Nested.Parameters)
						{
							NestedShadowed.insert(Parameter.Name);
						}
						return (Nested.Body && ReferencesActiveReceiver(*Nested.Body, NestedShadowed)) || (Nested.ExpressionBody && ReferencesActiveReceiver(*Nested.ExpressionBody, NestedShadowed));
					}
				default:
					return false;
				}
			}

			bool ReferencesActiveReceiver(
			    const FStmt& Statement,
			    const std::unordered_set<std::string>& ShadowedNames) const
			{
				const auto VisitExpr = [&](const std::unique_ptr<FExpr>& Value)
				{
					return Value && ReferencesActiveReceiver(*Value, ShadowedNames);
				};
				const auto VisitBlock = [&](const std::unique_ptr<FBlockStmt>& Value,
				                            const std::unordered_set<std::string>& Names)
				{
					return Value && ReferencesActiveReceiver(*Value, Names);
				};
				switch (Statement.Kind)
				{
				case EStmtKind::Block:
					{
						auto BlockNames = ShadowedNames;
						for (const auto& Child : static_cast<const FBlockStmt&>(Statement).Statements)
						{
							if (Child && ReferencesActiveReceiver(*Child, BlockNames))
							{
								return true;
							}
							if (Child && Child->Kind == EStmtKind::VarDecl)
							{
								BlockNames.insert(static_cast<const FVarDeclStmt&>(*Child).Name);
							}
							else if (Child && Child->Kind == EStmtKind::VarDeclGroup)
							{
								for (const auto& Declaration : static_cast<const FVarDeclGroupStmt&>(*Child).Declarations)
								{
									if (Declaration)
									{
										BlockNames.insert(Declaration->Name);
									}
								}
							}
						}
						return false;
					}
				case EStmtKind::Expr:
					return VisitExpr(static_cast<const FExprStmt&>(Statement).Expression);
				case EStmtKind::VarDecl:
					return VisitExpr(static_cast<const FVarDeclStmt&>(Statement).Initializer);
				case EStmtKind::VarDeclGroup:
					{
						auto GroupNames = ShadowedNames;
						for (const auto& Declaration : static_cast<const FVarDeclGroupStmt&>(Statement).Declarations)
						{
							if (!Declaration)
							{
								continue;
							}
							if (Declaration->Initializer && ReferencesActiveReceiver(*Declaration->Initializer, GroupNames))
							{
								return true;
							}
							GroupNames.insert(Declaration->Name);
						}
						return false;
					}
				case EStmtKind::Return:
					return VisitExpr(static_cast<const FReturnStmt&>(Statement).Value);
				case EStmtKind::Throw:
					return VisitExpr(static_cast<const FThrowStmt&>(Statement).Value);
				case EStmtKind::If:
					{
						const auto& If = static_cast<const FIfStmt&>(Statement);
						return VisitExpr(If.Condition) || VisitBlock(If.ThenBranch, ShadowedNames) || VisitBlock(If.ElseBranch, ShadowedNames);
					}
				case EStmtKind::While:
					{
						const auto& While = static_cast<const FWhileStmt&>(Statement);
						return VisitExpr(While.Condition) || VisitBlock(While.Body, ShadowedNames);
					}
				case EStmtKind::DoWhile:
					{
						const auto& DoWhile = static_cast<const FDoWhileStmt&>(Statement);
						return VisitBlock(DoWhile.Body, ShadowedNames) || VisitExpr(DoWhile.Condition);
					}
				case EStmtKind::For:
					{
						const auto& For = static_cast<const FForStmt&>(Statement);
						auto LoopNames = ShadowedNames;
						if (For.Init && ReferencesActiveReceiver(*For.Init, LoopNames))
						{
							return true;
						}
						if (For.Init && For.Init->Kind == EStmtKind::VarDecl)
						{
							LoopNames.insert(static_cast<const FVarDeclStmt&>(*For.Init).Name);
						}
						else if (For.Init && For.Init->Kind == EStmtKind::VarDeclGroup)
						{
							for (const auto& Declaration : static_cast<const FVarDeclGroupStmt&>(*For.Init).Declarations)
							{
								if (Declaration)
								{
									LoopNames.insert(Declaration->Name);
								}
							}
						}
						for (const auto& InitExpression : For.InitExpressions)
						{
							if (InitExpression && ReferencesActiveReceiver(*InitExpression, LoopNames))
							{
								return true;
							}
						}
						if ((For.Condition && ReferencesActiveReceiver(*For.Condition, LoopNames)) || (For.Step && ReferencesActiveReceiver(*For.Step, LoopNames)))
						{
							return true;
						}
						for (const auto& StepExpression : For.StepExpressions)
						{
							if (StepExpression && ReferencesActiveReceiver(*StepExpression, LoopNames))
							{
								return true;
							}
						}
						return VisitBlock(For.Body, LoopNames);
					}
				case EStmtKind::ForEach:
					{
						const auto& For = static_cast<const FForEachStmt&>(Statement);
						if (VisitExpr(For.Collection))
						{
							return true;
						}
						auto LoopNames = ShadowedNames;
						LoopNames.insert(For.ElementName);
						return VisitBlock(For.Body, LoopNames);
					}
				case EStmtKind::Switch:
					{
						const auto& Switch = static_cast<const FSwitchStmt&>(Statement);
						if (VisitExpr(Switch.Discriminant))
						{
							return true;
						}
						for (const FSwitchCase& Case : Switch.Cases)
						{
							for (const auto& Label : Case.Labels)
							{
								if (VisitExpr(Label))
								{
									return true;
								}
							}
							if (VisitBlock(Case.Body, ShadowedNames))
							{
								return true;
							}
						}
						return false;
					}
				case EStmtKind::Try:
					{
						const auto& Try = static_cast<const FTryStmt&>(Statement);
						if (VisitBlock(Try.TryBlock, ShadowedNames))
						{
							return true;
						}
						for (const FCatchClause& Clause : Try.Catches)
						{
							auto CatchNames = ShadowedNames;
							if (!Clause.Name.empty())
							{
								CatchNames.insert(Clause.Name);
							}
							if (VisitBlock(Clause.Body, CatchNames))
							{
								return true;
							}
						}
						return VisitBlock(Try.FinallyBlock, ShadowedNames);
					}
				default:
					return false;
				}
			}

			std::string EmitLambda(const FLambdaExpr& Lambda, const std::string& ExpectedType)
			{
				std::vector<std::string> ExpectedParams;
				std::string ExpectedReturn;
				std::string FunctionType = ResolveAlias(ExpectedType);
				if (!Runtime::ParseFunctionType(FunctionType, ExpectedParams, ExpectedReturn))
				{
					FunctionType = InferExprType(Lambda);
					if (!Runtime::ParseFunctionType(FunctionType, ExpectedParams, ExpectedReturn))
					{
						Fail("cannot infer lambda parameter and return types without a function-typed context",
						    Lambda.Location);
						return "default!";
					}
				}
				if (ExpectedParams.size() != Lambda.Parameters.size())
				{
					Fail("lambda parameter count does not match its function type", Lambda.Location);
					return "default!";
				}

				std::vector<std::string> ParamTypes;
				ParamTypes.reserve(Lambda.Parameters.size());
				for (std::size_t Index = 0; Index < Lambda.Parameters.size(); ++Index)
				{
					const FFunctionParam& Parameter = Lambda.Parameters[Index];
					std::string Type = Parameter.TypeName.empty()
					    ? ExpectedParams[Index]
					    : ResolveAlias(Parameter.TypeName);
					if (Parameter.bIsRef)
					{
						Type = Parameter.bIsConst ? "const ref " + Type : "ref " + Type;
					}
					if (CanonicalFunctionComponent(std::string(Runtime::StripReferenceTypeName(Type))) != CanonicalFunctionComponent(std::string(Runtime::StripReferenceTypeName(ExpectedParams[Index]))))
					{
						Fail("lambda parameter type conflicts with its function type", Lambda.Location);
						return "default!";
					}
					ParamTypes.push_back(std::move(Type));
				}
				const std::string ReturnType = Lambda.ReturnType.empty()
				    ? ExpectedReturn
				    : ResolveAlias(Lambda.ReturnType);
				if (CanonicalFunctionComponent(std::string(Runtime::StripReferenceTypeName(ReturnType))) != CanonicalFunctionComponent(std::string(Runtime::StripReferenceTypeName(ExpectedReturn))))
				{
					Fail("lambda return type conflicts with its function type", Lambda.Location);
					return "default!";
				}

				std::unordered_map<std::string, std::string> InitializedTypes;
				std::unordered_map<std::string, std::string> InitializedValues;
				for (const auto& Capture : Lambda.Captures)
				{
					if (!Capture.Initializer)
					{
						continue;
					}
					if (Capture.bByReference)
					{
						Fail("initialized reference captures require the Core AOT exporter", Capture.Location);
						return "default!";
					}
					const auto Type = ResolveAlias(std::string(Runtime::StripReferenceTypeName(InferExprType(*Capture.Initializer))));
					if (Type.empty() || Type == "auto")
					{
						Fail("cannot resolve initialized capture type: " + Capture.Name, Capture.Location);
						return "default!";
					}
					InitializedTypes.emplace(Capture.Name, Type);
					std::string Value = EmitExpr(*Capture.Initializer, Type);
					std::vector<std::string> FunctionParameters;
					std::string FunctionReturn;
					if (Runtime::ParseFunctionType(Type, FunctionParameters, FunctionReturn))
					{
						// A method group has no standalone tuple-element type in C#.
						Value = "((" + MapType(Type) + ")(" + Value + "))";
					}
					InitializedValues.emplace(Capture.Name, std::move(Value));
				}
				std::unordered_set<std::string> ExplicitReferences;
				for (const auto& Name : Lambda.GetCaptureNames(true))
				{
					ExplicitReferences.insert(Name);
				}
				std::unordered_set<std::string> CandidateNames;
				for (const std::string& Name : LocalVariableNames)
				{
					CandidateNames.insert(Name);
				}
				for (const FFunctionParam& Parameter : Lambda.Parameters)
				{
					CandidateNames.erase(Parameter.Name);
				}
				for (const auto& Capture : InitializedTypes)
				{
					CandidateNames.erase(Capture.first);
				}
				std::unordered_set<std::string> ReferencedNames;
				std::unordered_set<std::string> CaptureBindingChanges;
				if (Lambda.Body)
				{
					CollectReferencedLocals(*Lambda.Body, CandidateNames, ReferencedNames, nullptr, &CaptureBindingChanges);
				}
				if (Lambda.ExpressionBody)
				{
					CollectReferencedLocals(*Lambda.ExpressionBody, CandidateNames, ReferencedNames, nullptr, &CaptureBindingChanges);
				}
				std::vector<std::string> ValueCaptures;
				std::unordered_set<std::string> SeenValues;
				for (const auto& Capture : Lambda.Captures)
				{
					if (!Capture.bByReference && SeenValues.insert(Capture.Name).second)
					{
						ValueCaptures.push_back(Capture.Name);
					}
				}
				if (Lambda.CaptureKind == ELambdaCaptureKind::Implicit)
				{
					std::vector<std::string> ImplicitValues(ReferencedNames.begin(), ReferencedNames.end());
					std::sort(ImplicitValues.begin(), ImplicitValues.end());
					for (const auto& Name : ImplicitValues)
					{
						if (!ExplicitReferences.count(Name) && SeenValues.insert(Name).second)
						{
							ValueCaptures.push_back(Name);
						}
					}
				}
				std::unordered_set<std::string> ReferenceCaptures;
				for (const auto& Name : Lambda.GetCaptureNames(true))
				{
					ReferenceCaptures.insert(Name);
				}
				if (Lambda.CaptureKind == ELambdaCaptureKind::Reference)
				{
					std::unordered_set<std::string> ExplicitValues;
					for (const auto& Name : Lambda.GetCaptureNames(false))
					{
						ExplicitValues.insert(Name);
					}
					for (const std::string& Name : ReferencedNames)
					{
						if (ExplicitValues.find(Name) == ExplicitValues.end())
						{
							ReferenceCaptures.insert(Name);
						}
					}
				}
				for (const std::string& Name : ReferenceCaptures)
				{
					if (LocalRefNames.find(Name) != LocalRefNames.end())
					{
						Fail("C# cannot preserve an escaping reference capture of ref parameter '" + Name + "'; capture it by value or use the VM backend", Lambda.Location);
						return "default!";
					}
				}

				std::vector<std::pair<std::string, std::string>> Snapshots;
				for (const std::string& Name : ValueCaptures)
				{
					if (InitializedValues.count(Name))
					{
						Snapshots.emplace_back(Name, InitializedValues.at(Name));
						continue;
					}
					if (LocalVariableNames.find(Name) == LocalVariableNames.end())
					{
						continue;
					}
					const std::string Type = ResolveUserTypeSymbol(LocalTypes.at(Name));
					if (!Lambda.bMutable && CaptureBindingChanges.count(Name) &&
					    std::any_of(Program->Structs.begin(), Program->Structs.end(),
					        [&](const auto& S)
					{
						return CanonicalSymbol(S.Name) == Type &&
						    S.TypeParams.size() == UserTypeArity(LocalTypes.at(Name));
					}))
					{
						Fail("rebinding or borrowing value-captured struct '" + Name + "' requires managed capture storage; use the VM backend", Lambda.Location);
						return "default!";
					}
					const auto Emitted = LocalEmissionNames.find(Name);
					Snapshots.emplace_back(Name, Emitted == LocalEmissionNames.end() ? Identifier(Name) : Emitted->second);
				}
				std::string ReceiverCaptureName;
				const bool bAnnotatedThis = Lambda.bCapturesThis.load(std::memory_order_acquire);
				bool bDetectedThis = false;
				// Semantic analysis annotates ordinary and instantiated lambdas. Only
				// scan the AST for open generic bodies that may not have been analyzed;
				// this avoids copying scope state and walking every normal lambda twice.
				if (!bAnnotatedThis && bActiveCallableHasThis)
				{
					auto ReceiverShadowedNames = LocalVariableNames;
					for (const auto& Capture : InitializedTypes)
					{
						ReceiverShadowedNames.insert(Capture.first);
					}
					for (const FFunctionParam& Parameter : Lambda.Parameters)
					{
						ReceiverShadowedNames.insert(Parameter.Name);
					}
					bDetectedThis = (Lambda.Body && ReferencesActiveReceiver(*Lambda.Body, ReceiverShadowedNames)) || (Lambda.ExpressionBody && ReferencesActiveReceiver(*Lambda.ExpressionBody, ReceiverShadowedNames));
				}
				const bool bCapturesThis = !ActiveTypeName.empty() && (bAnnotatedThis || bDetectedThis);
				if (bCapturesThis && Lambda.CaptureKind == ELambdaCaptureKind::None)
				{
					Fail("a captureless lambda cannot capture this", Lambda.Location);
					return "default!";
				}
				if (bCapturesThis)
				{
					ReceiverCaptureName = "__vs_this";
					const auto NameIsUsed = [&](const std::string& Name)
					{
						return std::any_of(Snapshots.begin(), Snapshots.end(),
						           [&](const auto& Snapshot)
						{
							return Snapshot.first == Name;
						}) ||
						    std::any_of(Lambda.Parameters.begin(), Lambda.Parameters.end(),
						        [&](const FFunctionParam& Parameter)
						{
							return Parameter.Name == Name;
						});
					};
					while (NameIsUsed(ReceiverCaptureName))
					{
						ReceiverCaptureName.push_back('_');
					}
					Snapshots.emplace_back(ReceiverCaptureName,
					    ActiveThisEmission.empty() ? "this" : ActiveThisEmission);
				}
				const std::string CaptureParameter = Snapshots.empty()
				    ? std::string{}
				    : AllocateGeneratedLocalName("__capture");

				const auto SavedLocalTypes = LocalTypes;
				const auto SavedLocalEmissionNames = LocalEmissionNames;
				const auto SavedLocalVariableNames = LocalVariableNames;
				const auto SavedLocalConstNames = LocalConstNames;
				const auto SavedLocalRefNames = LocalRefNames;
				const std::string SavedReturnType = ActiveReturnType;
				const std::string SavedThisEmission = ActiveThisEmission;
				ActiveReturnType = ReturnType;
				for (const auto& Capture : InitializedTypes)
				{
					BindLocal(Capture.first, Capture.second, AllocateSourceLocalName(Capture.first, false), !Lambda.bMutable, false);
				}
				for (std::size_t Index = 0; Index < Lambda.Parameters.size(); ++Index)
				{
					const std::string& Name = Lambda.Parameters[Index].Name;
					BindLocal(Name,
					    std::string(Runtime::StripReferenceTypeName(ParamTypes[Index])),
					    AllocateSourceLocalName(Name, false), Lambda.Parameters[Index].bIsConst,
					    Lambda.Parameters[Index].bIsRef);
				}
				for (std::size_t Index = 0; Index < Snapshots.size(); ++Index)
				{
					LocalEmissionNames[Snapshots[Index].first] = Snapshots.size() == 1
					    ? CaptureParameter
					    : CaptureParameter + ".Item" + std::to_string(Index + 1);
					// A value snapshot of a reference alias owns a new binding.
					LocalRefNames.erase(Snapshots[Index].first);
				}
				if (bCapturesThis)
				{
					ActiveThisEmission = Snapshots.size() == 1
					    ? CaptureParameter
					    : CaptureParameter + ".Item" + std::to_string(Snapshots.size());
					for (const auto& [Name, Type] : LocalTypes)
					{
						if (LocalVariableNames.find(Name) != LocalVariableNames.end())
						{
							continue;
						}
						const FResolvedUserField Field = ResolveUserField(ActiveTypeName, Name);
						if (Field.Decl && !Field.Decl->bIsStatic)
						{
							LocalEmissionNames[Name] = ActiveThisEmission + "." + EmittedUserFieldName(*Field.Decl);
						}
					}
				}

				std::ostringstream LambdaText;
				LambdaText << "((" << MapType(FunctionType) << ")(";
				const bool bHasReferenceCapture = Lambda.CaptureKind == ELambdaCaptureKind::Reference || !Lambda.GetCaptureNames(true).empty();
				if (!bHasReferenceCapture && Snapshots.empty())
				{
					LambdaText << "static ";
				}
				LambdaText << '(';
				for (std::size_t Index = 0; Index < Lambda.Parameters.size(); ++Index)
				{
					if (Index != 0)
					{
						LambdaText << ", ";
					}
					if (Runtime::IsReferenceTypeName(ParamTypes[Index]))
					{
						LambdaText << (Runtime::IsConstReferenceTypeName(ParamTypes[Index]) ? "in " : "ref ");
					}
					LambdaText << MapType(std::string(Runtime::StripReferenceTypeName(ParamTypes[Index])))
					           << ' ' << EmittedLocalName(Lambda.Parameters[Index].Name);
				}
				LambdaText << ") => ";
				if (Lambda.Body)
				{
					std::ostringstream BodyStream;
					Out.swap(BodyStream);
					EmitBlock(*Lambda.Body, 0);
					const std::string Body = Out.str();
					Out.swap(BodyStream);
					LambdaText << Body;
				}
				else if (Lambda.ExpressionBody)
				{
					if (NormalizeType(ReturnType) == "void")
					{
						LambdaText << "{ " << EmitStatementExpression(*Lambda.ExpressionBody) << "; }";
					}
					else
					{
						if (Runtime::IsReferenceTypeName(ReturnType))
						{
							LambdaText << "ref ";
						}
						LambdaText << EmitExpr(*Lambda.ExpressionBody, ReturnType);
					}
				}
				else
				{
					LambdaText << "{ }";
				}
				LambdaText << "))";

				LocalTypes = SavedLocalTypes;
				LocalEmissionNames = SavedLocalEmissionNames;
				LocalVariableNames = SavedLocalVariableNames;
				LocalConstNames = SavedLocalConstNames;
				LocalRefNames = SavedLocalRefNames;
				ActiveReturnType = SavedReturnType;
				ActiveThisEmission = SavedThisEmission;
				if (Snapshots.empty())
				{
					return LambdaText.str();
				}

				std::ostringstream Result;
				Result << "VsCapture(";
				if (Snapshots.size() == 1)
				{
					Result << Snapshots.front().second;
				}
				else
				{
					Result << '(';
					for (std::size_t Index = 0; Index < Snapshots.size(); ++Index)
					{
						if (Index != 0)
						{
							Result << ", ";
						}
						Result << Snapshots[Index].second;
					}
					Result << ')';
				}
				Result << ", ";
				if (!bHasReferenceCapture)
				{
					Result << "static ";
				}
				Result << CaptureParameter << " => " << LambdaText.str() << ')';
				return Result.str();
			}

			std::string EmitUserArgument(
			    const FExpr& Argument,
			    const FFunctionParam* const Parameter,
			    const std::string& ExpectedType,
			    const FSourceLocation& Location)
			{
				if (!Parameter || !Parameter->bIsRef)
				{
					return EmitExpr(Argument, ExpectedType);
				}
				if (Argument.Kind != EExprKind::Ref)
				{
					Fail("reference parameter requires an explicit ref argument", Location);
					return "default!";
				}
				const FExpr& Operand = *static_cast<const FRefExpr&>(Argument).Operand;
				if (Parameter->bIsConst)
				{
					return EmitExpr(Operand, ExpectedType);
				}
				if (!IsCSharpWritableRefTarget(Operand))
				{
					Fail("writable ref arguments in C# AOT currently require a local or global variable",
					    Location);
					return "default!";
				}
				return "ref " + EmitExpr(Operand, ExpectedType);
			}

			std::string EmitUserCallArguments(
			    const FExprArgumentView Arguments,
			    const std::vector<FFunctionParam>& Parameters,
			    const std::string& OwnerType,
			    const FSourceLocation& Location)
			{
				std::ostringstream Result;
				const bool bVariadic = !Parameters.empty() && Parameters.back().bIsVariadic;
				for (std::size_t Index = 0; Index < Arguments.size(); ++Index)
				{
					if (Index != 0)
					{
						Result << ", ";
					}
					const std::size_t ParameterIndex = bVariadic && !Parameters.empty()
					    ? std::min(Index, Parameters.size() - 1)
					    : Index;
					const FFunctionParam* Parameter = ParameterIndex < Parameters.size()
					    ? &Parameters[ParameterIndex]
					    : nullptr;
					const std::string Expected = Parameter
					    ? SubstituteUserTypeArguments(OwnerType, Parameter->TypeName)
					    : std::string{};
					Result << EmitUserArgument(*Arguments[Index], Parameter, Expected, Location);
				}
				for (std::size_t Index = Arguments.size(); Index < Parameters.size(); ++Index)
				{
					const FFunctionParam& Parameter = Parameters[Index];
					if (Parameter.bIsVariadic || !Parameter.DefaultValue)
					{
						break;
					}
					if (Result.tellp() > 0)
					{
						Result << ", ";
					}
					const std::string Expected = SubstituteUserTypeArguments(
					    OwnerType, Parameter.TypeName);
					Result << EmitExpr(*Parameter.DefaultValue, Expected);
				}
				return Result.str();
			}

			std::string EmitExprCore(const FExpr& Expr, const std::string& ExpectedType = {})
			{
				switch (Expr.Kind)
				{
				case EExprKind::IntegerLiteral:
					{
						const auto& Literal = static_cast<const FIntegerLiteralExpr&>(Expr);
						if (Literal.bIs64)
						{
							return Literal.bUnsigned ? std::to_string(Literal.UnsignedValue64) + "UL"
							                         : std::to_string(Literal.Value64) + "L";
						}
						return Literal.bUnsigned ? std::to_string(Literal.UnsignedValue) + "U"
						                         : std::to_string(Literal.Value);
					}
				case EExprKind::CharacterLiteral:
					return "((sbyte)" + std::to_string(static_cast<int>(static_cast<const FCharacterLiteralExpr&>(Expr).Value)) + ")";
				case EExprKind::FloatLiteral:
					{
						const auto& Literal = static_cast<const FFloatLiteralExpr&>(Expr);
						std::ostringstream Value;
						Value << std::setprecision(std::numeric_limits<double>::max_digits10) << Literal.Value;
						std::string Text = Value.str();
						if (Text.find_first_of(".eE") == std::string::npos)
						{
							Text += ".0";
						}
						if (Literal.bFloat32)
						{
							Text += 'f';
						}
						return Text;
					}
				case EExprKind::StringLiteral:
					return "new VsString(\"" + EscapeCSharpByteString(static_cast<const FStringLiteralExpr&>(Expr).Value) + "\")";
				case EExprKind::WStringLiteral:
					return "\"" + EscapeWideString(static_cast<const FWStringLiteralExpr&>(Expr).Value) + "\"";
				case EExprKind::BooleanLiteral:
					return static_cast<const FBooleanLiteralExpr&>(Expr).Value ? "true" : "false";
				case EExprKind::NullptrLiteral:
					return "null!";
				case EExprKind::This:
					return ActiveThisEmission.empty() ? "this" : ActiveThisEmission;
				case EExprKind::Identifier:
					{
						const std::string& Name = static_cast<const FIdentifierExpr&>(Expr).Name;
						if (LocalTypes.find(Name) != LocalTypes.end())
						{
							if (const auto Found = LocalEmissionNames.find(Name);
							    Found != LocalEmissionNames.end())
							{
								return Found->second;
							}
							return Identifier(Name);
						}
						if (!ActiveTypeName.empty())
						{
							const FResolvedUserField Field = ResolveUserField(ActiveTypeName, Name);
							if (Field.Decl)
							{
								const std::string Receiver = !ActiveThisEmission.empty() && !Field.Decl->bIsStatic ? ActiveThisEmission + "." : std::string{};
								return Receiver + EmittedUserFieldName(*Field.Decl);
							}
						}
						if (const FVarDeclStmt* Global = ResolveGlobal(Name))
						{
							return GlobalIdentifier(*Global);
						}
						if (const std::string EnumMember = ResolveEnumMember(Name);
						    !EnumMember.empty())
						{
							return EnumMember;
						}
						if (const std::string UserType = ResolveUserTypeSymbol(Name); !UserType.empty())
						{
							return SymbolIdentifier(UserType);
						}
						const std::string FunctionSymbol = ResolveFunctionSymbol(Name);
						if (FunctionNames.find(FunctionSymbol) != FunctionNames.end())
						{
							if (const FFunctionDecl* Function = ResolveFunctionValue(Name, ExpectedType))
							{
								return FunctionIdentifier(*Function);
							}
							return FunctionIdentifierForSymbol(FunctionSymbol);
						}
						return Identifier(Name);
					}
				case EExprKind::Unary:
					{
						const auto& Unary = static_cast<const FUnaryExpr&>(Expr);
						const std::string Operand = EmitExpr(*Unary.Operand);
						const std::string ResultType = NormalizeType(InferExprType(Expr));
						if (Unary.Op == EUnaryOp::Not)
						{
							return "(!" + Operand + ")";
						}
						if (Unary.Op == EUnaryOp::BitNot && IsUnsignedIntegerType(ResultType))
						{
							return "unchecked((" + MapType(ResultType) + ")(~(" + MapType(ResultType) + ")(" + Operand + ")))";
						}
						if (Unary.Op == EUnaryOp::Negate && IsSignedIntegerType(ResultType))
						{
							return "checked(-(" + MapType(ResultType) + ")(" + Operand + "))";
						}
						const char* Op = Unary.Op == EUnaryOp::Negate ? "-" : "~";
						return std::string("(") + Op + Operand + ")";
					}
				case EExprKind::Binary:
					{
						const auto& Binary = static_cast<const FBinaryExpr&>(Expr);
						if (Binary.Op == EBinaryOp::In)
						{
							std::string ContainerBase;
							std::vector<std::string> ContainerArgs;
							if (!SplitGenericType(
							        ResolveAlias(InferExprType(*Binary.Right)),
							        ContainerBase,
							        ContainerArgs) ||
							    ContainerArgs.empty())
							{
								Fail("could not resolve 'in' container key type", Expr.Location);
								return "false";
							}
							return "VsIn(" + EmitExpr(*Binary.Left, ContainerArgs.front()) + ", " + EmitExpr(*Binary.Right) + ")";
						}
						const char* Op = BinaryOperator(Binary.Op);
						if (!Op)
						{
							Fail("unsupported binary operator", Expr.Location);
							return "false";
						}
						const std::string Left = EmitExpr(*Binary.Left);
						const std::string Right = EmitExpr(*Binary.Right);
						const std::string LeftType = NormalizeType(InferExprType(*Binary.Left));
						const std::string RightType = NormalizeType(InferExprType(*Binary.Right));
						const std::string ResultType = NormalizeType(InferExprType(Expr));
						if (IsNumericType(LeftType) && IsNumericType(RightType) && (LeftType == "float" || RightType == "float" || LeftType == "double" || RightType == "double"))
						{
							// Core compares floating operands through double; arithmetic uses
							// the common floating width and its own integer conversion path.
							const std::string Promoted = ResultType == "bool" || LeftType == "double" || RightType == "double" ? "double" : "float";
							return EmitFloatingOperation(Left, LeftType, Right, RightType, Binary.Op, Promoted);
						}
						const bool bGenericOperands = LeftType == RightType && ActiveTypeParameters.find(LeftType) != ActiveTypeParameters.end();
						if (bGenericOperands)
						{
							const std::string Type = MapType(LeftType);
							if (Binary.Op == EBinaryOp::Equal || Binary.Op == EBinaryOp::NotEqual)
							{
								const std::string Equals = "EqualityComparer<" + Type + ">.Default.Equals(" + Left + ", " + Right + ")";
								return Binary.Op == EBinaryOp::Equal ? Equals : "(!" + Equals + ")";
							}
							if (Binary.Op == EBinaryOp::Less || Binary.Op == EBinaryOp::LessEqual || Binary.Op == EBinaryOp::Greater || Binary.Op == EBinaryOp::GreaterEqual)
							{
								return "(Comparer<" + Type + ">.Default.Compare(" + Left + ", " + Right + ") " + Op + " 0)";
							}
							if (Binary.Op == EBinaryOp::Add)
							{
								return "VsGenericAdd<" + Type + ">(" + Left + ", " + Right + ")";
							}
						}
						if (LeftType == "wstring" && RightType == "wstring" && (Binary.Op == EBinaryOp::Less || Binary.Op == EBinaryOp::LessEqual || Binary.Op == EBinaryOp::Greater || Binary.Op == EBinaryOp::GreaterEqual))
						{
							return "(string.CompareOrdinal(" + Left + ", " + Right + ") " + Op + " 0)";
						}
						if (Binary.Op == EBinaryOp::Add && ResultType == "string")
						{
							return "VsConcat(" + EmitTextValue(*Binary.Left) + ", " + EmitTextValue(*Binary.Right) + ")";
						}
						if (Binary.Op == EBinaryOp::Add && ResultType == "wstring")
						{
							return "VsText(" + Left + ") + VsText(" + Right + ")";
						}
						if (Binary.Op == EBinaryOp::ShiftLeft || Binary.Op == EBinaryOp::ShiftRight)
						{
							return std::string(Binary.Op == EBinaryOp::ShiftLeft
							               ? "VsShiftLeft("
							               : "VsShiftRight(") +
							    "(" + MapType(ResultType) + ")(" + Left + "), Convert.ToInt64(" + Right + ", CultureInfo.InvariantCulture))";
						}
						if (Binary.Op == EBinaryOp::Divide && (ResultType == "float" || ResultType == "double"))
						{
							return "VsDivide((" + MapType(ResultType) + ")(" + Left + "), (" + MapType(ResultType) + ")(" + Right + "))";
						}
						const std::string Plain = "(" + Left + " " + Op + " " + Right + ")";
						if ((Binary.Op == EBinaryOp::Add || Binary.Op == EBinaryOp::Subtract || Binary.Op == EBinaryOp::Multiply) && IsSignedIntegerType(ResultType))
						{
							return "checked((" + MapType(ResultType) + ")" + Plain + ")";
						}
						if ((Binary.Op == EBinaryOp::Add || Binary.Op == EBinaryOp::Subtract || Binary.Op == EBinaryOp::Multiply) && IsUnsignedIntegerType(ResultType))
						{
							return "unchecked((" + MapType(ResultType) + ")" + Plain + ")";
						}
						return Plain;
					}
				case EExprKind::Assign:
					{
						const auto& Assign = static_cast<const FAssignExpr&>(Expr);
						const std::string Target = EmitExpr(*Assign.Target);
						const std::string TargetType = NormalizeType(InferExprType(*Assign.Target));
						if (Assign.bIsCompound && !Assign.bFromPrefixIncDec && !Assign.bFromPostfixIncDec && (Assign.CompoundOp == EBinaryOp::Add || Assign.CompoundOp == EBinaryOp::Subtract) && Runtime::IsMulticastDelegateTypeName(TargetType))
						{
							const std::string MethodName = Assign.CompoundOp == EBinaryOp::Add ? "add" : "remove";
							const std::vector<const FExpr*> Values{Assign.Value.get()};
							const FExprArgumentView Arguments(&Values);
							const FResolvedUserMethod Method = ResolveUserMethod(TargetType, MethodName, Arguments);
							if (!Method.Decl)
							{
								Fail("multicast_delegate method not found: " + MethodName, Expr.Location);
								return "default!";
							}
							return Target + '.' + EmittedUserMethodName(*Method.Decl) + '(' + EmitUserCallArguments(Arguments, Method.Decl->Parameters, Method.OwnerType, Expr.Location) + ')';
						}
						const FHostTypeDesc* HostOwner = nullptr;
						const FHostMemberDesc* HostSetter = nullptr;
						const FExpr* HostReceiver = nullptr;
						if (HostBindings && Assign.Target->Kind == EExprKind::MemberAccess)
						{
							const auto& Member = static_cast<const FMemberAccessExpr&>(*Assign.Target);
							HostOwner = ResolveHostType(InferExprType(*Member.Object));
							if (HostOwner)
							{
								for (const FHostMemberDesc& Candidate : HostOwner->Members)
								{
									if (Candidate.Kind == EHostMemberKind::PropertySetter && Candidate.Name == Member.Member && Candidate.ParamTypes.size() == 1)
									{
										HostSetter = &Candidate;
										HostReceiver = Member.Object.get();
										break;
									}
								}
							}
						}
						if (HostSetter)
						{
							if (Assign.bFromPostfixIncDec || Assign.bFromPrefixIncDec)
							{
								Fail("increment/decrement of host properties is not implemented", Expr.Location);
								return "default!";
							}
							std::string Value;
							if (Assign.bIsCompound)
							{
								if (!HostReceiver || !IsCSharpWritableRefTarget(*HostReceiver))
								{
									Fail("host-property compound assignment currently requires a local or global receiver",
									    Expr.Location);
									return "default!";
								}
								const char* Operator = BinaryOperator(Assign.CompoundOp);
								if (!Operator)
								{
									Fail("unsupported host-property compound assignment", Expr.Location);
									return "default!";
								}
								const std::string Right = EmitExpr(*Assign.Value);
								const std::string RightType = NormalizeType(InferExprType(*Assign.Value));
								if (IsNumericType(TargetType) && IsNumericType(RightType) && (TargetType == "float" || TargetType == "double" || RightType == "float" || RightType == "double"))
								{
									const std::string Promoted = TargetType == "double" || RightType == "double"
									    ? "double"
									    : "float";
									Value = ConvertForTarget(EmitFloatingOperation(Target, TargetType,
									                             Right, RightType, Assign.CompoundOp, Promoted),
									    Promoted, TargetType);
								}
								else if (Assign.CompoundOp == EBinaryOp::ShiftLeft || Assign.CompoundOp == EBinaryOp::ShiftRight)
								{
									const std::string Promoted = TargetType == "uint64" ? "uint64"
									    : TargetType == "int64"                         ? "int64"
									    : IsUnsignedIntegerType(TargetType)             ? "unsigned int"
									                                                    : "int";
									const std::string Shifted = std::string(
									                                Assign.CompoundOp == EBinaryOp::ShiftLeft
									                                    ? "VsShiftLeft("
									                                    : "VsShiftRight(") +
									    "(" + MapType(Promoted) + ")(" + Target + "), Convert.ToInt64(" + Right + ", CultureInfo.InvariantCulture))";
									Value = ConvertForTarget(Shifted, Promoted, TargetType);
								}
								else if (Assign.CompoundOp == EBinaryOp::Divide && (TargetType == "float" || TargetType == "double"))
								{
									Value = "VsDivide((" + MapType(TargetType) + ")(" + Target + "), (" + MapType(TargetType) + ")(" + Right + "))";
								}
								else
								{
									const std::string Operation = "(" + Target + " " + Operator + " " + Right + ")";
									if (IsSignedIntegerType(TargetType) || TargetType == "char" || TargetType == "unsigned char" || TargetType == "short" || TargetType == "unsigned short")
									{
										Value = "checked((" + MapType(TargetType) + ")" + Operation + ")";
									}
									else if (IsUnsignedIntegerType(TargetType))
									{
										Value = "unchecked((" + MapType(TargetType) + ")" + Operation + ")";
									}
									else
									{
										Value = Operation;
									}
								}
							}
							else
							{
								Value = EmitExpr(*Assign.Value, TargetType);
							}
							return HostMethodName(HostMemberIdentity(*HostOwner, *HostSetter)) + "(" + EmitExpr(*HostReceiver) + ", " + Value + ")";
						}
						if (Assign.bFromPostfixIncDec)
						{
							bool bVariableTarget = Assign.Target->Kind == EExprKind::Identifier;
							if (!bVariableTarget && Assign.Target->Kind == EExprKind::MemberAccess)
							{
								std::string Qualified;
								bVariableTarget = TryFlattenQualifiedName(*Assign.Target, Qualified) && ResolveGlobal(Qualified) != nullptr;
							}
							if (!bVariableTarget)
							{
								Fail("postfix increment/decrement currently requires a local or global variable",
								    Expr.Location);
								return "default!";
							}
							return std::string(Assign.CompoundOp == EBinaryOp::Add
							               ? "VsPostInc(ref "
							               : "VsPostDec(ref ") +
							    Target + ")";
						}
						if (Assign.bFromPrefixIncDec)
						{
							if (!IsRepeatableAssignmentTarget(*Assign.Target))
							{
								Fail("prefix increment/decrement target has side effects that cannot be preserved yet",
								    Expr.Location);
								return "default!";
							}
							const std::string Operation = "(" + Target + (Assign.CompoundOp == EBinaryOp::Add ? " + 1" : " - 1") + ")";
							if (IsSignedIntegerType(TargetType) || TargetType == "unsigned char" || TargetType == "unsigned short")
							{
								return Target + " = checked((" + MapType(TargetType) + ")" + Operation + ")";
							}
							if (IsUnsignedIntegerType(TargetType))
							{
								return Target + " = unchecked((" + MapType(TargetType) + ")" + Operation + ")";
							}
							return Target + " = " + Operation;
						}
						const char* Op = Assign.bIsCompound ? BinaryOperator(Assign.CompoundOp) : "";
						if (Assign.bIsCompound && !Op)
						{
							Fail("unsupported compound assignment", Expr.Location);
							return Target;
						}
						if (Assign.bIsCompound)
						{
							if (!IsRepeatableAssignmentTarget(*Assign.Target) || (Assign.Target->Kind == EExprKind::IndexAccess && !IsSideEffectFree(*Assign.Value)))
							{
								Fail("compound assignment target has side effects that cannot be preserved yet",
								    Expr.Location);
								return "default!";
							}
							const std::string Right = EmitExpr(*Assign.Value);
							const std::string RightType = NormalizeType(InferExprType(*Assign.Value));
							if (IsNumericType(TargetType) && IsNumericType(RightType) && (TargetType == "float" || TargetType == "double" || RightType == "float" || RightType == "double"))
							{
								const std::string Promoted = TargetType == "double" || RightType == "double"
								    ? "double"
								    : "float";
								return Target + " = " + ConvertForTarget(EmitFloatingOperation(Target, TargetType, Right, RightType, Assign.CompoundOp, Promoted), Promoted, TargetType);
							}
							if (Assign.CompoundOp == EBinaryOp::Add && ActiveTypeParameters.find(TargetType) != ActiveTypeParameters.end())
							{
								return Target + " = VsGenericAdd<" + MapType(TargetType) + ">(" + Target + ", " + Right + ")";
							}
							if (Assign.CompoundOp == EBinaryOp::ShiftLeft || Assign.CompoundOp == EBinaryOp::ShiftRight)
							{
								const std::string Promoted = TargetType == "uint64" ? "uint64"
								    : TargetType == "int64"                         ? "int64"
								    : IsUnsignedIntegerType(TargetType)             ? "unsigned int"
								                                                    : "int";
								const std::string Shifted = std::string(Assign.CompoundOp == EBinaryOp::ShiftLeft
								                                    ? "VsShiftLeft("
								                                    : "VsShiftRight(") +
								    "(" + MapType(Promoted) + ")(" + Target + "), Convert.ToInt64(" + Right + ", CultureInfo.InvariantCulture))";
								return Target + " = " + ConvertForTarget(Shifted, Promoted, TargetType);
							}
							if (Assign.CompoundOp == EBinaryOp::Divide && (TargetType == "float" || TargetType == "double"))
							{
								return Target + " = VsDivide((" + MapType(TargetType) + ")(" + Target + "), (" + MapType(TargetType) + ")(" + Right + "))";
							}
							const std::string Operation = "(" + Target + " " + std::string(Op) + " " + Right + ")";
							if (Assign.CompoundOp == EBinaryOp::Add || Assign.CompoundOp == EBinaryOp::Subtract || Assign.CompoundOp == EBinaryOp::Multiply)
							{
								if (IsSignedIntegerType(TargetType) || TargetType == "unsigned char" || TargetType == "unsigned short")
								{
									return Target + " = checked((" + MapType(TargetType) + ")" + Operation + ")";
								}
								if (IsUnsignedIntegerType(TargetType))
								{
									return Target + " = unchecked((" + MapType(TargetType) + ")" + Operation + ")";
								}
							}
							if (TargetType == "char" || TargetType == "unsigned char" || TargetType == "short" || TargetType == "unsigned short")
							{
								return Target + " = checked((" + MapType(TargetType) + ")" + Operation + ")";
							}
							return Target + " = " + Operation;
						}
						return Target + " = " + EmitExpr(*Assign.Value, TargetType);
					}
				case EExprKind::Call:
					return EmitCall(static_cast<const FCallExpr&>(Expr));
				case EExprKind::MemberAccess:
					{
						const auto& Member = static_cast<const FMemberAccessExpr&>(Expr);
						const std::string ObjectType = InferExprType(*Member.Object);
						std::string QualifiedName;
						if (TryFlattenQualifiedName(Expr, QualifiedName))
						{
							QualifiedName = CanonicalSymbol(std::move(QualifiedName));
							std::string NamespaceName;
							if (TryNamespaceValueName(Member, ObjectType, NamespaceName))
							{
								if (const FVarDeclStmt* Global = ResolveGlobal(NamespaceName)) return GlobalIdentifier(*Global);
								const auto FunctionSymbol = ResolveFunctionSymbol(NamespaceName);
								if (FunctionNames.count(FunctionSymbol))
								{
									if (const auto* Function = ResolveFunctionValue(NamespaceName, ExpectedType))
									{
										return FunctionIdentifier(*Function);
									}
									return FunctionIdentifierForSymbol(FunctionSymbol);
								}
							}
							if (const std::string EnumMember = ResolveEnumMember(QualifiedName);
							    !EnumMember.empty())
							{
								return EnumMember;
							}
							if (HostBindings)
							{
								const std::size_t Separator = QualifiedName.rfind("::");
								if (Separator != std::string::npos)
								{
									if (const FHostTypeDesc* Owner = ResolveHostType(
									        QualifiedName.substr(0, Separator)))
									{
										const std::string Name = QualifiedName.substr(Separator + 2);
										for (const FHostMemberDesc& Candidate : Owner->Members)
										{
											if (Candidate.Name != Name)
											{
												continue;
											}
											if (Candidate.Kind == EHostMemberKind::EnumMember)
											{
												return "unchecked((" + MapType(Owner->EnumUnderlyingTypeName) + ")(" + std::to_string(Candidate.EnumValue) + "L))";
											}
											if (Candidate.Kind == EHostMemberKind::StaticConstant)
											{
												return EmitHostMemberCall(*Owner, Candidate, FExprArgumentView{});
											}
										}
									}
								}
							}
						}
						if (HostBindings)
						{
							if (const FHostTypeDesc* Owner = ResolveHostType(ObjectType))
							{
								for (const FHostMemberDesc& Candidate : Owner->Members)
								{
									if (Candidate.Name == Member.Member && (Candidate.Kind == EHostMemberKind::PropertyGetter || Candidate.Kind == EHostMemberKind::Field) && Candidate.ParamTypes.empty())
									{
										return EmitHostMemberCall(*Owner, Candidate,
										    FExprArgumentView{}, Member.Object.get());
									}
								}
							}
						}
						const FResolvedUserField UserField = ResolveUserField(
						    ObjectType, Member.Member);
						const std::string MemberName = UserField.Decl
						    ? EmittedUserFieldName(*UserField.Decl)
						    : Member.Member == "first"  ? "Key"
						    : Member.Member == "second" ? "Value"
						                                : Identifier(Member.Member);
						return EmitExpr(*Member.Object) + "." + MemberName;
					}
				case EExprKind::MethodCall:
					{
						const auto& Method = static_cast<const FMethodCallExpr&>(Expr);
						std::string AttributeBase;
						std::vector<std::string> AttributeArgs;
						if (SplitGenericType(InferExprType(*Method.Object), AttributeBase, AttributeArgs) &&
						    ((AttributeBase == "attribute" && HasStandardAttribute()) ||
						        AttributeBase == "optional"))
						{
							if (AttributeBase == "attribute" && Method.GetArguments().size() == 1 &&
							    (Method.Method == "try_get" || Method.Method == "has_same_configuration"))
							{
								FFunctionParam Parameter;
								Parameter.bIsRef = true;
								Parameter.bIsConst = Method.Method == "has_same_configuration";
								const auto Type = Parameter.bIsConst ? InferExprType(*Method.Object) : AttributeArgs[0];
								return EmitExpr(*Method.Object) + "." + Method.Method + "(" +
								    EmitUserArgument(*Method.GetArguments()[0], &Parameter, Type, Method.Location) + ")";
							}
							return EmitExpr(*Method.Object) + "." + Method.Method + "(" +
							    EmitArguments(Method.GetArguments()) + ")";
						}

						std::string DescriptorBase;
						std::vector<std::string> DescriptorArgs;
						const bool bDescriptor = SplitGenericType(
						    InferExprType(*Method.Object), DescriptorBase, DescriptorArgs);
						if (Method.TypeArgs.size() == 1 && bDescriptor && ((DescriptorBase == "field_info" && (Method.Method == "get" || Method.Method == "set")) || (DescriptorBase == "method_info" && (Method.Method == "invoke" || Method.Method == "invoke_static"))))
						{
							Fail("executable reflection is not supported by C# AOT", Method.Location);
							return {};
						}
						std::string QualifiedOwner;
						if (TryFlattenQualifiedName(*Method.Object, QualifiedOwner))
						{
							const std::string QualifiedFunction = CanonicalSymbol(
							    QualifiedOwner + "::" + Method.Method);
							if (FunctionNames.find(QualifiedFunction) != FunctionNames.end())
							{
								FCallExpr QualifiedCall(QualifiedFunction, {});
								QualifiedCall.Location = Method.Location;
								QualifiedCall.TypeArgs = Method.TypeArgs;
								QualifiedCall.BorrowArgumentsFrom(Method.GetArguments());
								return EmitCall(QualifiedCall);
							}
							if (HostBindings)
							{
								if (const FHostTypeDesc* Owner = ResolveHostType(QualifiedOwner))
								{
									if (const FHostMemberDesc* Member = ResolveHostMember(*Owner,
									        Method.Method, {EHostMemberKind::StaticMethod},
									        Method.GetArguments()))
									{
										return EmitHostMemberCall(*Owner, *Member, Method.GetArguments());
									}
								}
							}
						}
						if (HostBindings)
						{
							if (const FHostTypeDesc* Owner = ResolveHostType(InferExprType(*Method.Object)))
							{
								if (const FHostMemberDesc* Member = ResolveHostMember(*Owner,
								        Method.Method, {EHostMemberKind::Method}, Method.GetArguments()))
								{
									return EmitHostMemberCall(*Owner, *Member, Method.GetArguments(),
									    Method.Object.get());
								}
							}
						}
						const FResolvedUserMethod UserMethod = ResolveUserMethod(
						    InferExprType(*Method.Object), Method.Method, Method.GetArguments());
						if (UserMethod.Decl)
						{
							std::ostringstream Result;
							Result << EmitExpr(*Method.Object) << '.'
							       << EmittedUserMethodName(*UserMethod.Decl) << '('
							       << EmitUserCallArguments(Method.GetArguments(),
							              UserMethod.Decl->Parameters, UserMethod.OwnerType, Method.Location)
							       << ')';
							return Result.str();
						}
						const std::string Object = EmitExpr(*Method.Object);
						std::string ContainerBase;
						std::vector<std::string> ContainerArgs;
						SplitGenericType(InferExprType(*Method.Object), ContainerBase, ContainerArgs);
						const bool bArray = ContainerBase == "array";
						const bool bMap = ContainerBase == "map" || ContainerBase == "ordered_map" || ContainerBase == "multimap" || ContainerBase == "ordered_multimap";
						const bool bSet = ContainerBase == "set" || ContainerBase == "multiset" || ContainerBase == "ordered_multiset";
						const bool bMultiSet = ContainerBase == "multiset" || ContainerBase == "ordered_multiset";
						const bool bQueue = ContainerBase == "queue";
						const bool bPriorityQueue = ContainerBase == "priority_queue";
						const bool bStack = ContainerBase == "stack";
						const bool bDeque = ContainerBase == "deque";
						const bool bSupportedContainer = bArray || bMap || bSet || bQueue || bPriorityQueue || bStack || bDeque;
						const bool bBuiltinMethod =
						    (bSupportedContainer && (Method.Method == "size" || Method.Method == "len" || Method.Method == "clear")) || (bArray && (Method.Method == "push" || Method.Method == "remove_at" || Method.Method == "reserve" || Method.Method == "capacity" || Method.Method == "remove")) || (bMap && (Method.Method == "has" || Method.Method == "remove" || Method.Method == "add" || Method.Method == "keys" || Method.Method == "values" || Method.Method == "entries" || Method.Method == "reserve")) || (bSet && (Method.Method == "has" || Method.Method == "remove" || Method.Method == "add" || Method.Method == "values" || (bMultiSet && Method.Method == "count") || Method.Method == "reserve")) || (bQueue && (Method.Method == "push" || Method.Method == "pop" || Method.Method == "front" || Method.Method == "values")) || (bPriorityQueue && (Method.Method == "push" || Method.Method == "pop" || Method.Method == "top" || Method.Method == "values")) || (bStack && (Method.Method == "push" || Method.Method == "pop" || Method.Method == "top" || Method.Method == "values")) || (bDeque && (Method.Method == "push_back" || Method.Method == "push_front" || Method.Method == "pop_back" || Method.Method == "pop_front" || Method.Method == "front" || Method.Method == "back" || Method.Method == "values"));
						if (!bBuiltinMethod)
						{
							if (const FResolvedCall Extension = ResolveExtensionCall(Method); Extension.Decl)
							{
								return EmitExtensionCall(Method, Extension);
							}
							Fail("method '" + Method.Method + "' is not implemented", Method.Location);
							return "default!";
						}
						std::ostringstream MethodArguments;
						for (std::size_t Index = 0; Index < Method.GetArguments().size(); ++Index)
						{
							if (Index != 0)
							{
								MethodArguments << ", ";
							}
							std::string Expected;
							if (Method.Method == "remove_at" || Method.Method == "reserve")
							{
								Expected = "int";
							}
							else if (bMap && ContainerArgs.size() == 2)
							{
								Expected = Index == 0
								    ? ContainerArgs[0]
								    : ContainerArgs[1];
							}
							else if (ContainerArgs.size() == 1)
							{
								Expected = ContainerArgs[0];
							}
							MethodArguments << EmitExpr(*Method.GetArguments()[Index], Expected);
						}
						const std::string Args = MethodArguments.str();
						const std::string Suffix = Args.empty() ? std::string() : ", " + Args;
						if (bSupportedContainer && (Method.Method == "size" || Method.Method == "len"))
						{
							return "VsLen(" + Object + ")";
						}
						if ((bArray || bQueue || bPriorityQueue || bStack) && Method.Method == "push")
						{
							return "VsPush(" + Object + Suffix + ")";
						}
						if ((bQueue || bPriorityQueue || bStack) && Method.Method == "pop")
						{
							return "VsPop(" + Object + ")";
						}
						if ((bQueue || bDeque) && Method.Method == "front")
						{
							return "VsFront(" + Object + ")";
						}
						if ((bStack || bPriorityQueue) && Method.Method == "top")
						{
							return "VsTop(" + Object + ")";
						}
						if (bDeque && Method.Method == "back")
						{
							return "VsBack(" + Object + ")";
						}
						if (bDeque && Method.Method == "push_back")
						{
							return "VsPushBack(" + Object + Suffix + ")";
						}
						if (bDeque && Method.Method == "push_front")
						{
							return "VsPushFront(" + Object + Suffix + ")";
						}
						if (bArray && Method.Method == "remove_at")
						{
							return "VsRemoveAt(" + Object + Suffix + ")";
						}
						if (bDeque && Method.Method == "pop_back")
						{
							return "VsPopBack(" + Object + ")";
						}
						if (bDeque && Method.Method == "pop_front")
						{
							return "VsPopFront(" + Object + ")";
						}
						if (bSupportedContainer && Method.Method == "clear")
						{
							return "VsClear(" + Object + ")";
						}
						if ((bMap || bSet) && Method.Method == "has")
						{
							return "VsHas(" + Object + Suffix + ")";
						}
						if ((bMap || bSet) && Method.Method == "add")
						{
							return "VsAdd(" + Object + Suffix + ")";
						}
						if (bMultiSet && Method.Method == "count")
						{
							return "VsOccurrences(" + Object + Suffix + ")";
						}
						if (bMap && Method.Method == "keys")
						{
							return "VsKeys(" + Object + ")";
						}
						if ((bMap || bSet || bQueue || bPriorityQueue || bStack || bDeque) && Method.Method == "values")
						{
							return "VsValues(" + Object + ")";
						}
						if (bMap && Method.Method == "entries")
						{
							if (HasUserPairTemplate())
							{
								Fail("map entries with a user pair<T,U> template require the VM backend", Method.Location);
								return "default!";
							}
							return "VsEntries(" + Object + ")";
						}
						if ((bArray || bMap || bSet) && Method.Method == "reserve")
						{
							return "VsReserve(" + Object + Suffix + ")";
						}
						if (bArray && Method.Method == "capacity")
						{
							return "VsCapacity(" + Object + ")";
						}
						if ((bArray || bMap || bSet) && Method.Method == "remove")
						{
							return Object + ".Remove(" + Args + ")";
						}
						Fail("method '" + Method.Method + "' is not implemented", Method.Location);
						return "default!";
					}
				case EExprKind::IndexAccess:
					{
						const auto& Index = static_cast<const FIndexAccessExpr&>(Expr);
						const std::string ObjectType = InferExprType(*Index.Object);
						std::string Base;
						std::vector<std::string> Args;
						SplitGenericType(ObjectType, Base, Args);
						const std::string ExpectedIndex = (Base == "map" || Base == "ordered_map" || Base == "multimap" || Base == "ordered_multimap") && Args.size() == 2 ? Args[0] : "int";
						const bool bLinearIndex = Runtime::IsDynamicArrayTypeName(ObjectType) || Runtime::IsNativeArrayTypeName(ObjectType) || Runtime::IsDequeTypeName(ObjectType);
						const std::string EmittedIndex = bLinearIndex
						    ? "VsNativeIndex(" + EmitExpr(*Index.Index) + ")"
						    : EmitExpr(*Index.Index, ExpectedIndex);
						return EmitExpr(*Index.Object) + "[" + EmittedIndex + "]";
					}
				case EExprKind::ArrayInit:
					{
						const auto& Array = static_cast<const FArrayInitExpr&>(Expr);
						const std::string SourceType = !Array.TypeName.empty() ? Array.TypeName : ExpectedType;
						const std::string Type = SourceType.empty() ? std::string() : MapType(SourceType);
						std::ostringstream Result;
						const bool bListLike = Type.compare(0, 5, "List<") == 0 || Type.compare(0, 8, "HashSet<") == 0 || Type.compare(0, 11, "VsMultiSet<") == 0 || Type.compare(0, 18, "VsOrderedMultiSet<") == 0 || Type.compare(0, 16, "VsPriorityQueue<") == 0;
						const bool bQueueLike = Type.compare(0, 6, "Queue<") == 0 || Type.compare(0, 6, "Stack<") == 0;
						std::string ElementSourceType;
						std::string ContainerBase;
						std::vector<std::string> ContainerArgs;
						if (SplitGenericType(SourceType, ContainerBase, ContainerArgs) && ContainerArgs.size() == 1)
						{
							ElementSourceType = ContainerArgs[0];
						}
						if (bListLike)
						{
							Result << "new " << Type << " { ";
						}
						else if (bQueueLike && Array.Elements.empty())
						{
							return "new " + Type + "()";
						}
						else if (bQueueLike)
						{
							Result << "new " << Type << "(new[] { ";
						}
						else
						{
							Result << "new[] { ";
						}
						for (std::size_t Index = 0; Index < Array.Elements.size(); ++Index)
						{
							if (Index != 0)
							{
								Result << ", ";
							}
							Result << EmitExpr(*Array.Elements[Index], ElementSourceType);
						}
						Result << (bQueueLike ? " })" : " }");
						return Result.str();
					}
				case EExprKind::MapInit:
					{
						const auto& Map = static_cast<const FMapInitExpr&>(Expr);
						const std::string SourceType = !Map.TypeName.empty() ? Map.TypeName : ExpectedType;
						if (SourceType.empty())
						{
							Fail("map initializer requires a known target type", Expr.Location);
							return "default!";
						}
						std::ostringstream Result;
						std::string ContainerBase;
						std::vector<std::string> ContainerArgs;
						SplitGenericType(SourceType, ContainerBase, ContainerArgs);
						const bool bMultiMap = ContainerBase == "multimap" || ContainerBase == "ordered_multimap";
						Result << "new " << MapType(SourceType) << " { ";
						for (std::size_t Index = 0; Index < Map.Entries.size(); ++Index)
						{
							if (Index != 0)
							{
								Result << ", ";
							}
							if (bMultiMap)
							{
								Result << "{ ";
							}
							else
							{
								Result << "[";
							}
							Result << EmitExpr(*Map.Entries[Index].Key,
							    ContainerArgs.size() == 2 ? ContainerArgs[0] : std::string());
							if (bMultiMap)
							{
								Result << ", ";
							}
							else
							{
								Result << "] = ";
							}
							Result << EmitExpr(*Map.Entries[Index].Value,
							    ContainerArgs.size() == 2
							        ? (bMultiMap ? "array<" + ContainerArgs[1] + ">" : ContainerArgs[1])
							        : std::string());
							if (bMultiMap)
							{
								Result << " }";
							}
						}
						Result << " }";
						return Result.str();
					}
				case EExprKind::Conditional:
					{
						const auto& Conditional = static_cast<const FConditionalExpr&>(Expr);
						return "(" + EmitExpr(*Conditional.Condition) + " ? " + EmitExpr(*Conditional.ThenExpr) + " : " + EmitExpr(*Conditional.ElseExpr) + ")";
					}
				case EExprKind::Ref:
					return "ref " + EmitExpr(*static_cast<const FRefExpr&>(Expr).Operand);
				case EExprKind::Lambda:
					return EmitLambda(static_cast<const FLambdaExpr&>(Expr), ExpectedType);
				default:
					{
						break;
					}
				}
				Fail("unknown expression kind", Expr.Location);
				return "default!";
			}

			static std::string LocalIdentifierIdentity(const std::string& Name)
			{
				// C#'s verbatim marker is not part of identifier identity: @lock and
				// lock denote the same symbol. Local names have no qualified segments.
				return !Name.empty() && Name.front() == '@' ? Name.substr(1) : Name;
			}

			static const std::unordered_set<std::string>& RecordSynthesizedMemberNames()
			{
				// A nominal record struct also receives equality operators whose CLR
				// metadata names share the declaration space with ordinary methods.
				static const std::unordered_set<std::string> Names = {
				    "Equals", "GetHashCode", "ToString", "PrintMembers",
				    "op_Equality", "op_Inequality"};
				return Names;
			}

			static bool IsRecordSynthesizedMemberName(const std::string& Name)
			{
				const std::string Identity = LocalIdentifierIdentity(Identifier(Name));
				const auto& Names = RecordSynthesizedMemberNames();
				return Names.find(Identity) != Names.end();
			}

			void IndexUserMemberEmissionNames()
			{
				UserFieldEmissionNames.clear();
				UserMethodEmissionNames.clear();
				for (const FStructDecl& Struct : Program->Structs)
				{
					if (!ShouldEmit(Struct.ModulePath) || AttributeModule(Struct.ModulePath))
					{
						continue;
					}
					std::unordered_set<std::string> UsedNames(
					    RecordSynthesizedMemberNames().begin(),
					    RecordSynthesizedMemberNames().end());
					for (const FFieldDecl& Field : Struct.Fields)
					{
						UsedNames.insert(LocalIdentifierIdentity(Identifier(Field.Name)));
					}
					for (const FMethodDecl& Method : Struct.Methods)
					{
						UsedNames.insert(LocalIdentifierIdentity(Identifier(Method.Name)));
					}
					std::unordered_map<std::string, std::string> RenamedMembers;
					std::uint32_t NextMemberId = 0;
					const auto EmittedName = [&](const std::string& Name) -> std::string
					{
						const std::string Identity = LocalIdentifierIdentity(Identifier(Name));
						if (const auto Found = RenamedMembers.find(Identity);
						    Found != RenamedMembers.end())
						{
							return Found->second;
						}
						const std::string Base = Identifier(Name);
						std::string Candidate;
						do
						{
							Candidate = Base + "__vs_member_" + std::to_string(NextMemberId++);
						}
						while (!UsedNames.insert(LocalIdentifierIdentity(Candidate)).second);
						RenamedMembers.emplace(Identity, Candidate);
						return Candidate;
					};
					for (const FFieldDecl& Field : Struct.Fields)
					{
						if (IsRecordSynthesizedMemberName(Field.Name))
						{
							UserFieldEmissionNames.emplace(&Field, EmittedName(Field.Name));
						}
					}
					for (const FMethodDecl& Method : Struct.Methods)
					{
						if (IsRecordSynthesizedMemberName(Method.Name))
						{
							UserMethodEmissionNames.emplace(&Method, EmittedName(Method.Name));
						}
					}
				}
			}

			std::string EmittedUserFieldName(const FFieldDecl& Field) const
			{
				const auto Found = UserFieldEmissionNames.find(&Field);
				return Found == UserFieldEmissionNames.end()
				    ? Identifier(Field.Name)
				    : Found->second;
			}

			std::string EmittedUserMethodName(const FMethodDecl& Method) const
			{
				const auto Found = UserMethodEmissionNames.find(&Method);
				return Found == UserMethodEmissionNames.end()
				    ? Identifier(Method.Name)
				    : Found->second;
			}

			void ConfigureTypeParameterEmissionNames(
			    const std::vector<std::string>& TypeParameters,
			    std::unordered_set<std::string> ReservedNames = {})
			{
				ActiveTypeParameterEmissionNames.clear();
				std::unordered_set<std::string> UsedNames;
				for (const std::string& Name : ReservedNames)
				{
					UsedNames.insert(LocalIdentifierIdentity(Name));
				}
				std::uint32_t NextTypeParameterId = 0;
				for (const std::string& Parameter : TypeParameters)
				{
					const std::string Base = Identifier(Parameter);
					std::string EmittedName = Base;
					while (!UsedNames.insert(LocalIdentifierIdentity(EmittedName)).second)
					{
						EmittedName = Base + "__vs_type_" + std::to_string(NextTypeParameterId++);
					}
					ActiveTypeParameterEmissionNames[NormalizeType(Parameter)] =
					    std::move(EmittedName);
				}
			}

			void ReserveActiveTypeParameterNamesForCallable()
			{
				for (const auto& [SourceName, EmittedName] : ActiveTypeParameterEmissionNames)
				{
					(void)SourceName;
					UsedLocalEmissionNames.insert(LocalIdentifierIdentity(EmittedName));
				}
			}

			std::string AllocateGeneratedLocalName(std::string Base)
			{
				if (UsedLocalEmissionNames.insert(LocalIdentifierIdentity(Base)).second)
				{
					return Base;
				}
				for (;;)
				{
					std::string Candidate = Base + "__vs_" + std::to_string(NextLocalEmissionId++);
					if (UsedLocalEmissionNames.insert(
					                              LocalIdentifierIdentity(Candidate))
					        .second)
					{
						return Candidate;
					}
				}
			}

			std::string AllocateSourceLocalName(
			    const std::string& Name,
			    const bool bProtectFieldBinding = true)
			{
				const std::string Base = Identifier(Name);
				if (bProtectFieldBinding && !ActiveTypeName.empty() && ResolveUserField(ActiveTypeName, Name).Decl)
				{
					UsedLocalEmissionNames.insert(LocalIdentifierIdentity(Base));
				}
				return AllocateGeneratedLocalName(Base);
			}

			std::string EmittedLocalName(const std::string& Name) const
			{
				const auto Found = LocalEmissionNames.find(Name);
				return Found == LocalEmissionNames.end() ? Identifier(Name) : Found->second;
			}

			void BindLocal(
			    const std::string& Name,
			    std::string Type,
			    std::string EmittedName,
			    const bool bConst,
			    const bool bRef)
			{
				LocalTypes[Name] = std::move(Type);
				LocalEmissionNames[Name] = std::move(EmittedName);
				LocalVariableNames.insert(Name);
				if (bConst)
				{
					LocalConstNames.insert(Name);
				}
				else
				{
					LocalConstNames.erase(Name);
				}
				if (bRef)
				{
					LocalRefNames.insert(Name);
				}
				else
				{
					LocalRefNames.erase(Name);
				}
			}

			void EmitBlock(const FBlockStmt& Block, const int Depth)
			{
				const auto SavedAliases = TypeAliases;
				const auto SavedLocalTypes = LocalTypes;
				const auto SavedLocalEmissionNames = LocalEmissionNames;
				const auto SavedLocalVariableNames = LocalVariableNames;
				const auto SavedLocalConstNames = LocalConstNames;
				const auto SavedLocalRefNames = LocalRefNames;
				Indent(Depth);
				Out << "{\n";
				for (const std::unique_ptr<FStmt>& Statement : Block.Statements)
				{
					EmitStmt(*Statement, Depth + 1);
					if (bFailed)
					{
						TypeAliases = SavedAliases;
						LocalTypes = SavedLocalTypes;
						LocalEmissionNames = SavedLocalEmissionNames;
						LocalVariableNames = SavedLocalVariableNames;
						LocalConstNames = SavedLocalConstNames;
						LocalRefNames = SavedLocalRefNames;
						return;
					}
				}
				Indent(Depth);
				Out << "}\n";
				TypeAliases = SavedAliases;
				LocalTypes = SavedLocalTypes;
				LocalEmissionNames = SavedLocalEmissionNames;
				LocalVariableNames = SavedLocalVariableNames;
				LocalConstNames = SavedLocalConstNames;
				LocalRefNames = SavedLocalRefNames;
			}

			std::string EmitForInitializer(const FStmt* Statement)
			{
				if (!Statement)
				{
					return {};
				}
				if (Statement->Kind == EStmtKind::VarDecl)
				{
					const auto& Variable = static_cast<const FVarDeclStmt&>(*Statement);
					const std::string ValueType = VariableValueType(Variable);
					if (bFailed)
					{
						return {};
					}
					const std::string MappedType = MapType(ValueType);
					if (bFailed)
					{
						return {};
					}
					const std::string Initializer = Variable.Initializer
					    ? EmitExpr(*Variable.Initializer, ValueType)
					    : DefaultValueForType(ValueType);
					if (bFailed)
					{
						return {};
					}
					const std::string EmittedName = AllocateSourceLocalName(Variable.Name);
					const std::string Result = MappedType + " " + EmittedName + " = " + Initializer;
					BindLocal(Variable.Name, ValueType, EmittedName, Variable.bIsConst, false);
					return Result;
				}
				if (Statement->Kind == EStmtKind::Expr)
				{
					return EmitStatementExpression(*static_cast<const FExprStmt&>(*Statement).Expression);
				}
				Fail("unsupported for-loop initializer", Statement->Location);
				return {};
			}

			bool EndsControlTransfer(const FBlockStmt& Block) const
			{
				if (Block.Statements.empty())
				{
					return false;
				}
				switch (Block.Statements.back()->Kind)
				{
				case EStmtKind::Break:
				case EStmtKind::Continue:
				case EStmtKind::Return:
				case EStmtKind::Throw:
				case EStmtKind::Goto:
				case EStmtKind::Fallthrough:
					return true;
				default:
					return false;
				}
			}

			void EmitSwitch(const FSwitchStmt& Switch, const int Depth)
			{
				const bool bUtf8String = ResolveAlias(InferExprType(*Switch.Discriminant)) == "string";
				Indent(Depth);
				Out << "switch (";
				if (bUtf8String)
				{
					Out << '(' << EmitExpr(*Switch.Discriminant) << ").Value";
				}
				else
				{
					Out << EmitExpr(*Switch.Discriminant);
				}
				Out << ")\n";
				Indent(Depth);
				Out << "{\n";
				for (std::size_t CaseIndex = 0; CaseIndex < Switch.Cases.size(); ++CaseIndex)
				{
					const auto SavedAliases = TypeAliases;
					const auto SavedLocalTypes = LocalTypes;
					const auto SavedLocalEmissionNames = LocalEmissionNames;
					const auto SavedLocalVariableNames = LocalVariableNames;
					const auto SavedLocalConstNames = LocalConstNames;
					const auto SavedLocalRefNames = LocalRefNames;
					const FSwitchCase& Case = Switch.Cases[CaseIndex];
					for (const std::unique_ptr<FExpr>& Label : Case.Labels)
					{
						Indent(Depth + 1);
						Out << "case ";
						if (bUtf8String && Label->Kind == EExprKind::StringLiteral)
						{
							Out << '"' << EscapeCSharpByteString(static_cast<const FStringLiteralExpr&>(*Label).Value) << '"';
						}
						else
						{
							Out << EmitExpr(*Label);
						}
						Out << ":\n";
					}
					if (Case.bIsDefault)
					{
						Indent(Depth + 1);
						Out << "default:\n";
					}
					Indent(Depth + 2);
					Out << "{\n";
					const bool bFallsThrough = Case.Body && !Case.Body->Statements.empty() && Case.Body->Statements.back()->Kind == EStmtKind::Fallthrough;
					if (Case.Body)
					{
						const std::size_t Count = Case.Body->Statements.size() - (bFallsThrough ? 1u : 0u);
						for (std::size_t Index = 0; Index < Count; ++Index)
						{
							EmitStmt(*Case.Body->Statements[Index], Depth + 3);
						}
					}
					if (bFallsThrough)
					{
						if (CaseIndex + 1 >= Switch.Cases.size())
						{
							Fail("fallthrough cannot target past the last switch case");
							return;
						}
						const FSwitchCase& Next = Switch.Cases[CaseIndex + 1];
						Indent(Depth + 3);
						if (Next.bIsDefault)
						{
							Out << "goto default;\n";
						}
						else if (!Next.Labels.empty())
						{
							Out << "goto case ";
							if (bUtf8String && Next.Labels.front()->Kind == EExprKind::StringLiteral)
							{
								Out << '"' << EscapeCSharpByteString(static_cast<const FStringLiteralExpr&>(*Next.Labels.front()).Value) << '"';
							}
							else
							{
								Out << EmitExpr(*Next.Labels.front());
							}
							Out << ";\n";
						}
					}
					else if (!Case.Body || !EndsControlTransfer(*Case.Body))
					{
						Indent(Depth + 3);
						Out << "break;\n";
					}
					Indent(Depth + 2);
					Out << "}\n";
					TypeAliases = SavedAliases;
					LocalTypes = SavedLocalTypes;
					LocalEmissionNames = SavedLocalEmissionNames;
					LocalVariableNames = SavedLocalVariableNames;
					LocalConstNames = SavedLocalConstNames;
					LocalRefNames = SavedLocalRefNames;
				}
				Indent(Depth);
				Out << "}\n";
			}

			void EmitStmt(const FStmt& Statement, const int Depth)
			{
				EmitLine(Statement.Location);
				switch (Statement.Kind)
				{
				case EStmtKind::Expr:
					{
						Indent(Depth);
						Out << EmitStatementExpression(*static_cast<const FExprStmt&>(Statement).Expression) << ";\n";
						break;
					}
				case EStmtKind::Block:
					{
						EmitBlock(static_cast<const FBlockStmt&>(Statement), Depth);
						break;
					}
				case EStmtKind::VarDecl:
					{
						const auto& Variable = static_cast<const FVarDeclStmt&>(Statement);
						if (Variable.bIsLocalStatic)
						{
							const auto Type = VariableValueType(Variable);
							if (!Runtime::IsPrimitiveValueTypeName(ResolveAlias(Type)))
							{
								Fail("local static objects and dependent types require the Core AOT exporter", Variable.Location);
								return;
							}
							const auto Mapped = MapType(Type);
							const auto Initializer = Variable.Initializer ? EmitExpr(*Variable.Initializer, Type) : DefaultValueForType(Type);
							const auto Slot = AllocateGeneratedLocalName("__static_cell");
							const auto Name = AllocateSourceLocalName(Variable.Name);
							bNeedsLocalStatics = true;
							Indent(Depth);
							Out << "var " << Slot << " = VsLocalStatic<" << Mapped << ">(() => default(" << Mapped << ")!);\n";
							Indent(Depth);
							Out << "if (" << Slot << ".Begin())\n";
							Indent(Depth);
							Out << "{\n";
							Indent(Depth + 1);
							Out << "try { " << Slot << ".Commit(" << Initializer << "); }\n";
							Indent(Depth + 1);
							Out << "catch { " << Slot << ".Abort(); throw; }\n";
							Indent(Depth);
							Out << "}\n";
							Indent(Depth);
							Out << (Variable.bIsConst ? "ref readonly " : "ref ") << Mapped << ' ' << Name
							    << " = ref " << Slot << ".Value;\n";
							BindLocal(Variable.Name, Type, Name, Variable.bIsConst, true);
							break;
						}
						if (Variable.bIsRef)
						{
							if (!Variable.Initializer)
							{
								Fail("local reference alias requires an initializer", Statement.Location);
								return;
							}
							const std::string ValueType = VariableValueType(Variable);
							if (bFailed)
							{
								return;
							}
							const std::string MappedType = MapType(ValueType);
							if (bFailed)
							{
								return;
							}
							const std::string Initializer = EmitExpr(
							    *Variable.Initializer, ValueType);
							if (bFailed)
							{
								return;
							}
							const std::string EmittedName = AllocateSourceLocalName(Variable.Name);
							Indent(Depth);
							Out << (Variable.bIsConst ? "ref readonly " : "ref ")
							    << MappedType << ' ' << EmittedName
							    << " = ref " << Initializer << ";\n";
							BindLocal(Variable.Name, ValueType, EmittedName,
							    Variable.bIsConst, true);
							break;
						}
						const std::string ValueType = VariableValueType(Variable);
						if (bFailed)
						{
							return;
						}
						const std::string MappedType = MapType(ValueType);
						if (bFailed)
						{
							return;
						}
						const std::string Initializer = Variable.Initializer
						    ? EmitExpr(*Variable.Initializer, ValueType)
						    : DefaultValueForType(ValueType);
						if (bFailed)
						{
							return;
						}
						const std::string EmittedName = AllocateSourceLocalName(Variable.Name);
						Indent(Depth);
						Out << MappedType << ' ' << EmittedName
						    << " = " << Initializer << ";\n";
						BindLocal(Variable.Name, ValueType, EmittedName,
						    Variable.bIsConst, false);
						break;
					}
				case EStmtKind::VarDeclGroup:
					{
						const auto& Group = static_cast<const FVarDeclGroupStmt&>(Statement);
						for (const auto& Declaration : Group.Declarations)
						{
							if (Declaration)
							{
								EmitStmt(*Declaration, Depth);
							}
						}
						break;
					}
				case EStmtKind::TypeAlias:
					{
						const auto& Alias = static_cast<const FTypeAliasStmt&>(Statement);
						TypeAliases[Alias.Name] = ResolveAlias(Alias.TypeName);
						break;
					}
				case EStmtKind::Return:
					{
						const auto& Return = static_cast<const FReturnStmt&>(Statement);
						Indent(Depth);
						Out << "return";
						if (Return.Value)
						{
							if (IsReferenceTypeSpelling(ActiveReturnType))
							{
								Out << " ref";
							}
							Out << ' ' << EmitExpr(*Return.Value, ActiveReturnType);
						}
						Out << ";\n";
						break;
					}
				case EStmtKind::If:
					{
						const auto& If = static_cast<const FIfStmt&>(Statement);
						Indent(Depth);
						Out << "if (" << EmitExpr(*If.Condition) << ")\n";
						EmitBlock(*If.ThenBranch, Depth);
						if (If.ElseBranch)
						{
							Indent(Depth);
							Out << "else\n";
							EmitBlock(*If.ElseBranch, Depth);
						}
						break;
					}
				case EStmtKind::While:
					{
						const auto& While = static_cast<const FWhileStmt&>(Statement);
						Indent(Depth);
						Out << "while (" << EmitExpr(*While.Condition) << ")\n";
						EmitBlock(*While.Body, Depth);
						break;
					}
				case EStmtKind::For:
					{
						const auto& For = static_cast<const FForStmt&>(Statement);
						const auto SavedLocalTypes = LocalTypes;
						const auto SavedLocalEmissionNames = LocalEmissionNames;
						const auto SavedLocalVariableNames = LocalVariableNames;
						const auto SavedLocalConstNames = LocalConstNames;
						const auto SavedLocalRefNames = LocalRefNames;
						const auto SavedAliases = TypeAliases;
						int LoopDepth = Depth;
						const bool bGroupedInitializer = For.Init && (For.Init->Kind == EStmtKind::VarDeclGroup || (For.Init->Kind == EStmtKind::VarDecl && static_cast<const FVarDeclStmt&>(*For.Init).bIsLocalStatic));
						std::string Initializer;
						if (bGroupedInitializer)
						{
							Indent(Depth);
							Out << "{\n";
							if (For.Init->Kind == EStmtKind::VarDecl)
							{
								EmitStmt(*For.Init, Depth + 1);
							}
							else
							{
								const auto& Group = static_cast<const FVarDeclGroupStmt&>(*For.Init);
								for (const auto& Declaration : Group.Declarations)
								{
									if (Declaration)
									{
										EmitStmt(*Declaration, Depth + 1);
									}
								}
							}
							LoopDepth = Depth + 1;
						}
						else
						{
							Initializer = EmitForInitializer(For.Init.get());
						}
						for (const auto& InitExpression : For.InitExpressions)
						{
							if (!InitExpression)
							{
								continue;
							}
							if (!Initializer.empty())
							{
								Initializer += ", ";
							}
							Initializer += EmitStatementExpression(*InitExpression);
						}
						Indent(LoopDepth);
						Out << "for (" << Initializer << "; ";
						if (For.Condition)
						{
							Out << EmitExpr(*For.Condition);
						}
						Out << "; ";
						bool bHasStep = false;
						if (For.Step)
						{
							Out << EmitStatementExpression(*For.Step);
							bHasStep = true;
						}
						for (const auto& StepExpression : For.StepExpressions)
						{
							if (!StepExpression)
							{
								continue;
							}
							if (bHasStep)
							{
								Out << ", ";
							}
							Out << EmitStatementExpression(*StepExpression);
							bHasStep = true;
						}
						Out << ")\n";
						EmitBlock(*For.Body, LoopDepth);
						if (bGroupedInitializer)
						{
							Indent(Depth);
							Out << "}\n";
						}
						LocalTypes = SavedLocalTypes;
						LocalEmissionNames = SavedLocalEmissionNames;
						LocalVariableNames = SavedLocalVariableNames;
						LocalConstNames = SavedLocalConstNames;
						LocalRefNames = SavedLocalRefNames;
						TypeAliases = SavedAliases;
						break;
					}
				case EStmtKind::ForEach:
					{
						const auto& For = static_cast<const FForEachStmt&>(Statement);
						if (For.bHasKeyBinding || For.bIsRef)
						{
							Fail("map destructuring and ref foreach are not implemented", Statement.Location);
							return;
						}
						const auto SavedLocalTypes = LocalTypes;
						const auto SavedLocalEmissionNames = LocalEmissionNames;
						const auto SavedLocalVariableNames = LocalVariableNames;
						const auto SavedLocalConstNames = LocalConstNames;
						const auto SavedLocalRefNames = LocalRefNames;
						const std::string ElementType = ResolveAlias(For.ElementTypeName);
						const std::string CollectionType = InferExprType(*For.Collection);
						std::string CollectionBase;
						std::vector<std::string> CollectionArgs;
						if (!SplitGenericType(CollectionType, CollectionBase, CollectionArgs))
						{
							Fail("foreach collection type could not be resolved", Statement.Location);
							return;
						}
						const bool bMap = (CollectionBase == "map" || CollectionBase == "ordered_map") && CollectionArgs.size() == 2;
						std::string ElementBase;
						std::vector<std::string> ElementArgs;
						const bool bMapEntries = bMap && SplitGenericType(ElementType, ElementBase, ElementArgs) && ElementBase == "pair" && ElementArgs.size() == 2 && MapType(ElementArgs[0]) == MapType(CollectionArgs[0]) && MapType(ElementArgs[1]) == MapType(CollectionArgs[1]);
						if (bMapEntries && HasUserPairTemplate())
						{
							Fail("map entries with a user pair<T,U> template require the VM backend", Statement.Location);
							return;
						}
						const bool bArray = CollectionBase == "array";
						const char* Snapshot = bMapEntries ? "VsSnapshotEntries("
						    : bArray                       ? "VsIterateArray("
						                                   : "VsSnapshot(";
						const std::string Collection = EmitExpr(*For.Collection);
						if (bFailed)
						{
							return;
						}
						const std::string EmittedName = AllocateSourceLocalName(For.ElementName);
						Indent(Depth);
						Out << "foreach (" << MapType(For.ElementTypeName) << ' '
						    << EmittedName << " in "
						    << Snapshot
						    << Collection << "))\n";
						BindLocal(For.ElementName, ElementType == "auto" ? (bMap ? CollectionArgs[1] : (!CollectionArgs.empty() ? CollectionArgs[0] : ElementType)) : ElementType, EmittedName, For.bIsConst, false);
						EmitBlock(*For.Body, Depth);
						LocalTypes = SavedLocalTypes;
						LocalEmissionNames = SavedLocalEmissionNames;
						LocalVariableNames = SavedLocalVariableNames;
						LocalConstNames = SavedLocalConstNames;
						LocalRefNames = SavedLocalRefNames;
						break;
					}
				case EStmtKind::Switch:
					{
						EmitSwitch(static_cast<const FSwitchStmt&>(Statement), Depth);
						break;
					}
				case EStmtKind::Break:
					{
						Indent(Depth);
						Out << "break;\n";
						break;
					}
				case EStmtKind::Continue:
					{
						Indent(Depth);
						Out << "continue;\n";
						break;
					}
				case EStmtKind::Fallthrough:
					{
						Fail("fallthrough is only valid as the last switch-case statement", Statement.Location);
						break;
					}
				case EStmtKind::Goto:
					{
						Indent(Depth);
						Out << "goto " << Identifier(static_cast<const FGotoStmt&>(Statement).Name) << ";\n";
						break;
					}
				case EStmtKind::Label:
					{
						Indent(std::max(0, Depth - 1));
						Out << Identifier(static_cast<const FLabelStmt&>(Statement).Name) << ":\n";
						break;
					}
				case EStmtKind::Try:
					{
						const auto& Try = static_cast<const FTryStmt&>(Statement);
						const std::uint32_t ExceptionId = NextExceptionId++;
						const std::string CarrierName = AllocateGeneratedLocalName(
						    "___vs_exception_" + std::to_string(ExceptionId));
						Indent(Depth);
						Out << "try\n";
						EmitBlock(*Try.TryBlock, Depth);
						if (!Try.Catches.empty())
						{
							Indent(Depth);
							Out << "catch (VsExceptionCarrier " << CarrierName << ")\n";
							Indent(Depth);
							Out << "{\n";
							bool bHasConditional = false;
							bool bHasCatchAll = false;
							for (std::size_t CatchIndex = 0;
							    CatchIndex < Try.Catches.size(); ++CatchIndex)
							{
								const FCatchClause& Clause = Try.Catches[CatchIndex];
								const auto SavedAliases = TypeAliases;
								const auto SavedLocalTypes = LocalTypes;
								const auto SavedLocalEmissionNames = LocalEmissionNames;
								const auto SavedLocalVariableNames = LocalVariableNames;
								const auto SavedLocalConstNames = LocalConstNames;
								const auto SavedLocalRefNames = LocalRefNames;
								Indent(Depth + 1);
								if (bHasConditional)
								{
									Out << "else" << (Clause.bCatchAll ? '\n' : ' ');
									if (Clause.bCatchAll)
									{
										Indent(Depth + 1);
									}
								}
								std::string CaughtName;
								std::string MappedType;
								if (Clause.bCatchAll)
								{
									Out << "{\n";
									bHasCatchAll = true;
								}
								else
								{
									MappedType = MapType(Clause.TypeName);
									CaughtName = AllocateGeneratedLocalName(
									    "___vs_caught_" + std::to_string(ExceptionId) + "_" + std::to_string(CatchIndex));
									Out << "if (" << CarrierName << ".TryGet<" << MappedType
									    << ">(out " << MappedType << ' ' << CaughtName << "))\n";
									Indent(Depth + 1);
									Out << "{\n";
									bHasConditional = true;
								}
								if (!Clause.Name.empty())
								{
									const std::string EmittedName =
									    AllocateSourceLocalName(Clause.Name);
									Indent(Depth + 2);
									Out << MappedType << ' '
									    << EmittedName << " = " << CaughtName << ";\n";
									BindLocal(Clause.Name, ResolveAlias(Clause.TypeName),
									    EmittedName, Clause.bIsConst, false);
								}
								const bool bNeedsRefRebind = Clause.bIsRef && !Clause.Name.empty();
								ActiveCatchRefRebinds.push_back(bNeedsRefRebind
								        ? CarrierName + ".Rebind(" + EmittedLocalName(Clause.Name) + ")"
								        : std::string{});
								if (Clause.Body)
								{
									EmitBlock(*Clause.Body, Depth + 2);
								}
								ActiveCatchRefRebinds.pop_back();
								Indent(Depth + 1);
								Out << "}\n";
								TypeAliases = SavedAliases;
								LocalTypes = SavedLocalTypes;
								LocalEmissionNames = SavedLocalEmissionNames;
								LocalVariableNames = SavedLocalVariableNames;
								LocalConstNames = SavedLocalConstNames;
								LocalRefNames = SavedLocalRefNames;
							}
							if (!bHasCatchAll)
							{
								Indent(Depth + 1);
								if (bHasConditional)
								{
									Out << "else\n";
									Indent(Depth + 1);
								}
								Out << "{\n";
								Indent(Depth + 2);
								Out << "throw;\n";
								Indent(Depth + 1);
								Out << "}\n";
							}
							Indent(Depth);
							Out << "}\n";
						}
						if (Try.FinallyBlock)
						{
							Indent(Depth);
							Out << "finally\n";
							EmitBlock(*Try.FinallyBlock, Depth);
						}
						break;
					}
				case EStmtKind::Throw:
					{
						const auto& Throw = static_cast<const FThrowStmt&>(Statement);
						Indent(Depth);
						if (!Throw.Value)
						{
							if (!ActiveCatchRefRebinds.empty() && !ActiveCatchRefRebinds.back().empty())
							{
								Out << ActiveCatchRefRebinds.back() << ";\n";
								Indent(Depth);
							}
							Out << "throw;\n";
						}
						else
						{
							Out << "throw new VsExceptionCarrier("
							    << EmitExpr(*Throw.Value) << ");\n";
						}
						break;
					}
				case EStmtKind::DoWhile:
					{
						const auto& DoWhile = static_cast<const FDoWhileStmt&>(Statement);
						Indent(Depth);
						Out << "do\n";
						EmitBlock(*DoWhile.Body, Depth);
						Indent(Depth);
						Out << "while (" << EmitExpr(*DoWhile.Condition) << ");\n";
						break;
					}
				default:
					{
						break;
					}
				}
			}

			void EmitEnums()
			{
				Out << "\tprivate static string VsEnumText(Enum value)\n\t{\n";
				for (std::size_t EnumIndex = 0; EnumIndex < Program->Enums.size(); ++EnumIndex)
				{
					const FEnumDecl& Enum = Program->Enums[EnumIndex];
					if (!ShouldEmit(Enum.ModulePath))
					{
						continue;
					}
					const std::string Underlying = ResolveAlias(Enum.UnderlyingTypeName);
					const bool bUnsigned = Underlying == "uint8" || Underlying == "unsigned char" || Underlying == "uint16" || Underlying == "unsigned short" || Underlying == "uint32" || Underlying == "unsigned int" || Underlying == "uint64" || Underlying == "unsigned long";
					Out << "\t\tif (value is " << SymbolIdentifier(Enum.Name) << " typed"
					    << EnumIndex << ") return \""
					    << EscapeCSharpString(Enum.Name) << "(\" + Convert.To"
					    << (bUnsigned ? "UInt64" : "Int64")
					    << "(typed" << EnumIndex
					    << ", CultureInfo.InvariantCulture).ToString(CultureInfo.InvariantCulture) + \")\";\n";
				}
				Out << "\t\treturn value.GetType().Name + \"(\" + Convert.ToInt64(value, "
				    << "CultureInfo.InvariantCulture).ToString(CultureInfo.InvariantCulture) + \")\";\n"
				    << "\t}\n\n";
				for (const FEnumDecl& Enum : Program->Enums)
				{
					if (!ShouldEmit(Enum.ModulePath))
					{
						continue;
					}
					EmitLine(Enum.Location);
					Out << "\tprivate enum " << SymbolIdentifier(Enum.Name) << " : "
					    << MapType(Enum.UnderlyingTypeName) << "\n\t{\n";
					for (std::size_t Index = 0; Index < Enum.Members.size(); ++Index)
					{
						Out << "\t\t" << Identifier(Enum.Members[Index].Name) << " = "
						    << Enum.Members[Index].Value;
						if (Index + 1 != Enum.Members.size())
						{
							Out << ',';
						}
						Out << "\n";
					}
					Out << "\t}\n\n";
				}
			}

			void EmitCustomFunctionDelegates()
			{
				for (std::size_t TypeIndex = 0; TypeIndex < CustomFunctionTypes.size(); ++TypeIndex)
				{
					// MapType may discover a nested custom delegate and grow the vector,
					// so retain a value copy rather than a reference into its storage.
					const FCustomFunctionType Custom = CustomFunctionTypes[TypeIndex];
					const auto SavedTypeParameters = ActiveTypeParameters;
					const auto SavedTypeParameterEmissionNames =
					    ActiveTypeParameterEmissionNames;
					ActiveTypeParameters.clear();
					ActiveTypeParameterEmissionNames.clear();
					for (std::size_t Index = 0; Index < Custom.TypeParameters.size(); ++Index)
					{
						ActiveTypeParameters.insert(Custom.TypeParameters[Index]);
						ActiveTypeParameterEmissionNames.emplace(
						    Custom.TypeParameters[Index], "__VsType" + std::to_string(Index));
					}
					std::vector<std::string> Params;
					std::string ReturnType;
					if (!Runtime::ParseFunctionType(Custom.FunctionType, Params, ReturnType))
					{
						Fail("invalid function type while emitting a C# delegate");
						return;
					}
					Out << "\tprivate delegate ";
					if (Runtime::IsReferenceTypeName(ReturnType))
					{
						Out << (Runtime::IsConstReferenceTypeName(ReturnType)
						        ? "ref readonly "
						        : "ref ");
					}
					Out << MapType(ReturnType) << ' ' << Custom.EmittedName;
					if (!Custom.TypeParameters.empty())
					{
						Out << '<';
						for (std::size_t Index = 0; Index < Custom.TypeParameters.size(); ++Index)
						{
							if (Index != 0)
							{
								Out << ", ";
							}
							Out << "__VsType" << Index;
						}
						Out << '>';
					}
					Out << '(';
					for (std::size_t Index = 0; Index < Params.size(); ++Index)
					{
						if (Index != 0)
						{
							Out << ", ";
						}
						if (Runtime::IsReferenceTypeName(Params[Index]))
						{
							Out << (Runtime::IsConstReferenceTypeName(Params[Index]) ? "in " : "ref ");
						}
						Out << MapType(std::string(Runtime::StripReferenceTypeName(Params[Index])))
						    << " __arg" << Index;
					}
					Out << ");\n";
					ActiveTypeParameters = SavedTypeParameters;
					ActiveTypeParameterEmissionNames = SavedTypeParameterEmissionNames;
				}
				if (!CustomFunctionTypes.empty())
				{
					Out << '\n';
				}
			}

			static const char* AccessName(const EMemberAccess Access)
			{
				switch (Access)
				{
				case EMemberAccess::Public:
					return "public";
				case EMemberAccess::Protected:
					return "protected";
				case EMemberAccess::Private:
					return "private";
				default:
					{
						break;
					}
				}
				return "private";
			}

			std::string UserMethodSignature(
			    const FMethodDecl& Method,
			    const std::string& OwnerType) const
			{
				std::ostringstream Signature;
				Signature << Method.Name << '#' << Method.Parameters.size()
				          << (Method.bIsConst ? "#const" : "#mutable");
				for (const FFunctionParam& Parameter : Method.Parameters)
				{
					Signature << '#' << NormalizeType(SubstituteUserTypeArguments(OwnerType, Parameter.TypeName));
					if (Parameter.bIsRef)
					{
						Signature << (Parameter.bIsConst ? "#in" : "#ref");
					}
					else
					{
						Signature << "#value";
					}
				}
				return Signature.str();
			}

			void CollectAbstractUserMethods(
			    const std::string& OwnerType,
			    std::unordered_set<std::string>& OutMethods) const
			{
				const std::string Owner = ResolveUserTypeSymbol(OwnerType);
				for (const FClassDecl& Class : Program->Classes)
				{
					if (!ShouldEmit(Class.ModulePath))
					{
						continue;
					}
					if (CanonicalSymbol(Class.Name) != Owner)
					{
						continue;
					}
					if (!Class.BaseTypeNames.empty())
					{
						CollectAbstractUserMethods(SubstituteUserTypeArguments(
						                               OwnerType, Class.BaseTypeNames.front()),
						    OutMethods);
					}
					for (const FMethodDecl& Method : Class.Methods)
					{
						if (Method.bDeleted || Method.bIsStatic)
						{
							continue;
						}
						const std::string Signature = UserMethodSignature(Method, OwnerType);
						if (Method.bPureVirtual)
						{
							OutMethods.insert(Signature);
						}
						else
						{
							OutMethods.erase(Signature);
						}
					}
					return;
				}
			}

			bool IsAbstractUserClass(const std::string& OwnerType) const
			{
				std::unordered_set<std::string> Methods;
				CollectAbstractUserMethods(OwnerType, Methods);
				return !Methods.empty();
			}

			void BeginUserCallable(
			    const std::vector<FFieldDecl>& Fields,
			    const std::vector<FFunctionParam>& Parameters,
			    const std::string& ReturnType,
			    const bool bHasThis)
			{
				ActiveReturnType = ReturnType;
				bActiveCallableHasThis = bHasThis;
				LocalTypes.clear();
				LocalEmissionNames.clear();
				LocalVariableNames.clear();
				LocalConstNames.clear();
				LocalRefNames.clear();
				UsedLocalEmissionNames.clear();
				NextLocalEmissionId = 0;
				ActiveThisEmission.clear();
				ReserveActiveTypeParameterNamesForCallable();
				for (const FFieldDecl& Field : Fields)
				{
					const std::string Type = ResolveAlias(Field.TypeName);
					LocalTypes[Field.Name] = Type;
					LocalEmissionNames[Field.Name] = EmittedUserFieldName(Field);
					if (Field.bIsConst)
					{
						LocalConstNames.insert(Field.Name);
					}
				}
				for (const FFunctionParam& Parameter : Parameters)
				{
					BindLocal(Parameter.Name, Parameter.bIsVariadic ? "array<" + ResolveAlias(Parameter.TypeName) + ">" : ResolveAlias(Parameter.TypeName),
					    AllocateSourceLocalName(Parameter.Name, false),
					    Parameter.bIsConst, Parameter.bIsRef);
				}
			}

			void EndUserCallable()
			{
				LocalTypes.clear();
				LocalEmissionNames.clear();
				LocalVariableNames.clear();
				LocalConstNames.clear();
				LocalRefNames.clear();
				UsedLocalEmissionNames.clear();
				ActiveReturnType.clear();
				bActiveCallableHasThis = false;
				ActiveThisEmission.clear();
			}

			void EmitUserParameters(const std::vector<FFunctionParam>& Parameters)
			{
				for (std::size_t Index = 0; Index < Parameters.size(); ++Index)
				{
					if (Index != 0)
					{
						Out << ", ";
					}
					const FFunctionParam& Parameter = Parameters[Index];
					if (Parameter.bIsVariadic)
					{
						Out << "params ";
					}
					else if (Parameter.bIsRef)
					{
						Out << (Parameter.bIsConst ? "in " : "ref ");
					}
					Out << MapType(Parameter.TypeName);
					if (Parameter.bIsVariadic)
					{
						Out << "[]";
					}
					Out << ' ' << EmittedLocalName(Parameter.Name);
				}
			}

			template <typename TDecl>
			void EmitUserMethod(const TDecl& Decl, const FMethodDecl& Method)
			{
				if (Method.bDeleted)
				{
					return;
				}
				const auto SavedModule = ActiveModulePath;
				const auto SavedNamespace = ActiveNamespace;
				BeginUserCallable(Decl.Fields, Method.Parameters, Method.ReturnType,
				    !Method.bIsStatic);
				EmitLine(Method.Location);
				const bool bVirtualDispatch = Method.bVirtual || Method.bOverride || Method.bPureVirtual;
				// C# requires one invariant accessibility across an override chain, while
				// VeroScript follows C++ and permits access to change per override. User
				// types are private implementation details of the generated program, so
				// public dispatch preserves calls while the checked source enforces access.
				Out << "\t\t" << (bVirtualDispatch ? "public" : AccessName(Method.Access)) << ' ';
				if (Method.bIsStatic)
				{
					Out << "static ";
				}
				if (Method.bPureVirtual)
				{
					Out << "abstract ";
				}
				else if (Method.bOverride)
				{
					Out << (Method.bFinal ? "sealed override " : "override ");
				}
				else if (Method.bVirtual && (!std::is_same_v<TDecl, FClassDecl> || !Decl.bFinal))
				{
					Out << "virtual ";
				}
				if (IsReferenceTypeSpelling(Method.ReturnType))
				{
					Out << (Runtime::IsConstReferenceTypeName(Method.ReturnType)
					        ? "ref readonly "
					        : "ref ");
				}
				Out << MapType(Method.ReturnType) << ' ' << EmittedUserMethodName(Method) << '(';
				EmitUserParameters(Method.Parameters);
				Out << ')';
				if (Method.bPureVirtual)
				{
					Out << ";\n";
				}
				else if (Method.Body)
				{
					Out << '\n';
					if (!Method.Body->DefinitionModulePath.empty())
					{
						ActiveModulePath = Method.Body->DefinitionModulePath;
						ActiveNamespace = Method.Body->DefinitionNamespace;
					}
					EmitBlock(*Method.Body, 2);
				}
				else
				{
					Fail("method body is unavailable", Method.Location);
				}
				Out << "#line default\n\n";
				EndUserCallable();
				ActiveModulePath = SavedModule;
				ActiveNamespace = SavedNamespace;
			}

			template <typename TDecl>
			void EmitUserConstructor(const TDecl& Decl, const FConstructorDecl& Constructor)
			{
				if (Constructor.bDeleted)
				{
					return;
				}
				BeginUserCallable(Decl.Fields, Constructor.Parameters, "void", true);
				EmitLine(Constructor.Location);
				Out << "\t\tpublic " << SymbolIdentifier(Decl.Name) << '(';
				EmitUserParameters(Constructor.Parameters);
				Out << ')';
				if (!Constructor.BaseInits.empty())
				{
					const FBaseCtorInit& Base = Constructor.BaseInits.front();
					const std::string BaseType = SubstituteUserTypeArguments(
					    Decl.Name, Base.BaseName);
					const FExprArgumentView BaseArguments(nullptr, &Base.Args);
					const FResolvedUserConstructor Resolved = ResolveUserConstructor(
					    BaseType, BaseArguments);
					Out << " : base(";
					if (Resolved.Decl)
					{
						Out << EmitUserCallArguments(BaseArguments, Resolved.Decl->Parameters,
						    Resolved.OwnerType, Constructor.Location);
					}
					else
					{
						Out << EmitArguments(BaseArguments);
					}
					Out << ')';
				}
				else if constexpr (std::is_same_v<TDecl, FClassDecl>)
				{
					if (!Decl.BaseTypeNames.empty())
					{
						const std::string BaseType = SubstituteUserTypeArguments(
						    Decl.Name, Decl.BaseTypeNames.front());
						const FResolvedUserConstructor Resolved = ResolveUserConstructor(
						    BaseType, FExprArgumentView{});
						if (Resolved.Decl && !Resolved.Decl->Parameters.empty())
						{
							Out << " : base(" << EmitUserCallArguments(FExprArgumentView{}, Resolved.Decl->Parameters, Resolved.OwnerType, Constructor.Location) << ')';
						}
					}
				}
				if (Constructor.bDefaulted)
				{
					Out << " { }\n";
				}
				else if (Constructor.Body)
				{
					Out << "\n\t\t{\n";
					for (const FFieldDecl& Field : Decl.Fields)
					{
						const auto InitIt = std::find_if(
						    Constructor.FieldInits.begin(),
						    Constructor.FieldInits.end(),
						    [&](const FFieldCtorInit& Init)
						{
							return Init.FieldName == Field.Name;
						});
						if (InitIt == Constructor.FieldInits.end())
						{
							continue;
						}
						EmitLine(InitIt->Location);
						Out << "\t\t\tthis." << EmittedUserFieldName(Field) << " = ";
						const std::string NormalizedFieldType = NormalizeType(Field.TypeName);
						const bool bOpenTypeParameter = ActiveTypeParameters.find(
						                                    NormalizedFieldType) != ActiveTypeParameters.end();
						const bool bScalarField = IsNumericType(NormalizedFieldType) || NormalizedFieldType == "bool" || NormalizedFieldType == "string" || NormalizedFieldType == "wstring" || EnumUnderlyingTypes.find(CanonicalSymbol(NormalizedFieldType)) != EnumUnderlyingTypes.end();
						if (bOpenTypeParameter && InitIt->Args.size() == 1 && NormalizeType(InferExprType(*InitIt->Args.front())) == NormalizedFieldType)
						{
							Out << EmitExpr(*InitIt->Args.front(), Field.TypeName);
						}
						else if (bOpenTypeParameter)
						{
							Fail(
							    "constructor field initializer for open type parameter '" + NormalizedFieldType + "' requires exactly one argument of the same type",
							    InitIt->Location);
							Out << "default!";
						}
						else if (bScalarField && InitIt->Args.empty())
						{
							Out << DefaultValueForType(Field.TypeName);
						}
						else if (bScalarField && InitIt->Args.size() == 1)
						{
							Out << EmitExpr(*InitIt->Args.front(), Field.TypeName);
						}
						else
						{
							FCallExpr Construction(
							    SubstituteUserTypeArguments(Decl.Name, Field.TypeName), {});
							Construction.BorrowArgumentsFrom(InitIt->Args);
							Construction.Location = InitIt->Location;
							Out << EmitExpr(Construction, Field.TypeName);
						}
						Out << ";\n";
					}
					for (const std::unique_ptr<FStmt>& Statement : Constructor.Body->Statements)
					{
						if (Statement)
						{
							EmitStmt(*Statement, 3);
						}
						if (bFailed)
						{
							break;
						}
					}
					Out << "\t\t}\n";
				}
				else
				{
					Fail("constructor body is unavailable", Constructor.Location);
				}
				Out << "#line default\n\n";
				EndUserCallable();
			}

			template <typename TDecl>
			void EmitUserType(const TDecl& Decl, const bool bStruct)
			{
				ActiveNamespace = NamespaceOf(Decl.Name);
				ActiveModulePath = Decl.ModulePath;
				ActiveTypeName = CanonicalSymbol(Decl.Name);
				ActiveTypeParameters.clear();
				for (const std::string& Parameter : Decl.TypeParams)
				{
					ActiveTypeParameters.insert(NormalizeType(Parameter));
				}
				std::unordered_set<std::string> ReservedTypeParameterNames = {
				    SymbolIdentifier(Decl.Name)};
				if (bStruct)
				{
					// Nominal record structs synthesize these members even though they do
					// not appear in the VeroScript AST. C# places them in the same
					// declaration space as the type parameters.
					ReservedTypeParameterNames.insert(
					    RecordSynthesizedMemberNames().begin(),
					    RecordSynthesizedMemberNames().end());
				}
				for (const FFieldDecl& Field : Decl.Fields)
				{
					ReservedTypeParameterNames.insert(EmittedUserFieldName(Field));
				}
				for (const FMethodDecl& Method : Decl.Methods)
				{
					ReservedTypeParameterNames.insert(EmittedUserMethodName(Method));
				}
				ConfigureTypeParameterEmissionNames(
				    Decl.TypeParams, std::move(ReservedTypeParameterNames));
				const bool bAbstract = !bStruct && IsAbstractUserClass(Decl.Name);
				EmitLine(Decl.Location);
				Out << "\tprivate ";
				if (!bStruct && bAbstract)
				{
					Out << "abstract ";
				}
				if (!bStruct && Decl.bFinal)
				{
					Out << "sealed ";
				}
				Out << (bStruct ? "record struct " : "class ")
				    << SymbolIdentifier(Decl.Name);
				if (!Decl.TypeParams.empty())
				{
					Out << '<';
					for (std::size_t Index = 0; Index < Decl.TypeParams.size(); ++Index)
					{
						if (Index != 0)
						{
							Out << ", ";
						}
						Out << "[DynamicallyAccessedMembers("
						    << "DynamicallyAccessedMemberTypes.PublicParameterlessConstructor)] "
						    << ActiveTypeParameterEmissionNames.at(
						           NormalizeType(Decl.TypeParams[Index]));
					}
					Out << '>';
				}
				if constexpr (std::is_same_v<TDecl, FClassDecl>)
				{
					if (!Decl.BaseTypeNames.empty())
					{
						Out << " : " << MapType(Decl.BaseTypeNames.front());
					}
				}
				Out << "\n\t{\n";
				Out << "#pragma warning disable CS0414 // Core permits intentionally unused script data members.\n";
				for (const FFieldDecl& Field : Decl.Fields)
				{
					EmitLine(Field.Location);
					Out << "\t\t" << AccessName(Field.Access) << ' ';
					if (Field.bIsStatic)
					{
						Out << "static ";
					}
					if (Field.bIsConst && !Field.bIsStatic)
					{
						Out << "readonly ";
					}
					Out << MapType(Field.TypeName) << ' ' << EmittedUserFieldName(Field);
					if (Field.bIsStatic)
					{
						Out << " = default!";
					}
					else if (Field.DefaultValue)
					{
						Out << " = " << EmitExpr(*Field.DefaultValue, Field.TypeName);
					}
					else if (!Field.bIsStatic)
					{
						Out << " = " << DefaultValueForType(Field.TypeName);
					}
					Out << ";\n#line default\n";
				}
				Out << "#pragma warning restore CS0414\n";
				for (const FFieldDecl& Field : Decl.Fields)
				{
					if (!Field.bIsStatic)
					{
						continue;
					}
					std::string Name = "__vs_initialize_" + SymbolIdentifier(Field.Name);
					while (std::any_of(Decl.Fields.begin(), Decl.Fields.end(), [&](const auto& Other)
					{
						return EmittedUserFieldName(Other) == Name;
					}) || std::any_of(Decl.Methods.begin(), Decl.Methods.end(), [&](const auto& Method)
					{
						return EmittedUserMethodName(Method) == Name;
					}) || std::any_of(ActiveTypeParameterEmissionNames.begin(), ActiveTypeParameterEmissionNames.end(), [&](const auto& Parameter)
					{
						return LocalIdentifierIdentity(Parameter.second) == Name;
					}))
					{
						Name += '_';
					}
					StaticFieldInitializerNames.emplace(&Field, Name);
					Out << "\t\tpublic static void " << Name << "()\n\t\t{\n";
					EmitLine(Field.Location);
					Out << "\t\t\t" << EmittedUserFieldName(Field) << " = "
					    << (Field.DefaultValue ? EmitExpr(*Field.DefaultValue, Field.TypeName)
					                           : DefaultValueForType(Field.TypeName)) << ";\n\t\t}\n";
				}
				const bool bHasFieldDefaults = std::any_of(Decl.Fields.begin(), Decl.Fields.end(),
				    [](const FFieldDecl& Field)
				{
					return !Field.bIsStatic && Field.DefaultValue != nullptr;
				});
				const bool bHasInstanceFields = std::any_of(Decl.Fields.begin(), Decl.Fields.end(),
				    [](const FFieldDecl& Field)
				{
					return !Field.bIsStatic;
				});
				const bool bHasConstructor = std::any_of(
				    Decl.Constructors.begin(), Decl.Constructors.end(),
				    [](const FConstructorDecl& Constructor)
				{
					return !Constructor.bDeleted;
				});
				const bool bHasZeroConstructor = std::any_of(Decl.Constructors.begin(), Decl.Constructors.end(),
				    [](const FConstructorDecl& Constructor)
				{
					return !Constructor.bDeleted && Constructor.Parameters.empty();
				});
				if (bStruct && !bHasZeroConstructor && (bHasFieldDefaults || (bHasInstanceFields && !bHasConstructor)))
				{
					Out << "\t\tpublic " << SymbolIdentifier(Decl.Name) << "() { }\n\n";
				}
				for (const FConstructorDecl& Constructor : Decl.Constructors)
				{
					EmitUserConstructor(Decl, Constructor);
					if (bFailed)
					{
						return;
					}
				}
				for (const FMethodDecl& Method : Decl.Methods)
				{
					EmitUserMethod(Decl, Method);
					if (bFailed)
					{
						return;
					}
				}
				Out << "\t}\n\n";
				ActiveTypeName.clear();
				ActiveTypeParameters.clear();
				ActiveTypeParameterEmissionNames.clear();
				ActiveNamespace.clear();
				ActiveModulePath.clear();
			}

			void EmitUserTypes()
			{
				for (const FStructDecl& Struct : Program->Structs)
				{
					if (!ShouldEmit(Struct.ModulePath) || AttributeModule(Struct.ModulePath))
					{
						continue;
					}
					EmitUserType(Struct, true);
					if (bFailed)
					{
						return;
					}
				}
				for (const FClassDecl& Class : Program->Classes)
				{
					if (!ShouldEmit(Class.ModulePath))
					{
						continue;
					}
					EmitUserType(Class, false);
					if (bFailed)
					{
						return;
					}
				}
			}

			std::string CheckedTypeSourceName(const std::string& Name) const
			{
				const auto Found = std::find_if(CheckedModule->Types.begin(), CheckedModule->Types.end(),
				    [&](const auto& Type) { return Type.Name == Name; });
				if (Found != CheckedModule->Types.end())
				{
					std::string Result = Found->Scope.QualifiedName;
					if (!Found->Scope.ConcreteTypeArguments.empty())
					{
						Result += '<';
						for (std::size_t Index = 0; Index < Found->Scope.ConcreteTypeArguments.size(); ++Index)
						{
							if (Index != 0)
							{
								Result += ',';
							}
							Result += CheckedTypeSourceName(Found->Scope.ConcreteTypeArguments[Index]);
						}
						Result += '>';
					}
					return Result;
				}
				const auto Enum = std::find_if(CheckedModule->Enums.begin(), CheckedModule->Enums.end(),
				    [&](const auto& Type) { return Type.Name == Name; });
				if (Enum != CheckedModule->Enums.end()) return Enum->Scope.QualifiedName;
				std::string Base;
				std::vector<std::string> Arguments;
				if (!SplitGenericType(Name, Base, Arguments)) return Name;
				std::string Result = Base + '<';
				for (std::size_t Index = 0; Index < Arguments.size(); ++Index)
				{
					if (Index != 0)
					{
						Result += ',';
					}
					Result += CheckedTypeSourceName(Arguments[Index]);
				}
				return Result + '>';
			}

			void EmitGlobals()
			{
				// Script initialization belongs to the entry exception boundary, not a
				// CLR type initializer that may run after main or wrap script failures.
				Out << "#pragma warning disable CS0414 // Core permits intentionally unused script globals.\n";
				for (const std::unique_ptr<FStmt>& Statement : Program->TopLevelStatements)
				{
					if (Statement->Kind != EStmtKind::VarDecl)
					{
						Fail("unsupported top-level statement", Statement->Location);
						return;
					}
					const auto& Variable = static_cast<const FVarDeclStmt&>(*Statement);
					ActiveNamespace = NamespaceOf(Variable.Name);
					ActiveModulePath = Variable.ModulePath;
					Out << "\tprivate static " << MapType(GlobalTypes[&Variable]) << ' '
					    << GlobalIdentifier(Variable) << " = default!;\n";
				}
				Out << "#pragma warning restore CS0414\n\n\tprivate static void VsInitializeGlobals()\n\t{\n";
				std::unordered_map<std::string, std::pair<std::string, FSourceLocation>> Initializers;
				const auto IndexStatics = [&](const auto& Declarations)
				{
					for (const auto& Decl : Declarations)
					{
						if (!ShouldEmit(Decl.ModulePath) || AttributeModule(Decl.ModulePath) ||
						    std::none_of(Decl.Fields.begin(), Decl.Fields.end(), [](const auto& Field)
						{
							return Field.bIsStatic;
						}))
						{
							continue;
						}
						ActiveNamespace = NamespaceOf(Decl.Name);
						ActiveModulePath = Decl.ModulePath;
						for (const auto& Type : CheckedModule->Types)
						{
							if (Type.Scope.ModulePath != Decl.ModulePath ||
							    CanonicalSymbol(Type.Scope.QualifiedName) != CanonicalSymbol(Decl.Name))
							{
								continue;
							}
							const std::string Owner = MapType(CheckedTypeSourceName(Type.Name));
							for (const auto& Field : Decl.Fields)
							{
								if (!Field.bIsStatic)
								{
									continue;
								}
								Initializers.emplace(Type.Name + "." + Field.Name,
								    std::make_pair("\t\t" + Owner + "." + StaticFieldInitializerNames.at(&Field) + "();\n", Field.Location));
							}
						}
					}
				};
				IndexStatics(Program->Structs);
				IndexStatics(Program->Classes);
				for (const std::unique_ptr<FStmt>& Statement : Program->TopLevelStatements)
				{
					const auto& Variable = static_cast<const FVarDeclStmt&>(*Statement);
					ActiveNamespace = NamespaceOf(Variable.Name);
					ActiveModulePath = Variable.ModulePath;
					const std::string ValueType = GlobalTypes[&Variable];
					std::string Linkage = Variable.Name;
					if (!Variable.bExported && !Variable.ModulePath.empty())
					{
						std::string ModulePath = Variable.ModulePath;
						std::replace(ModulePath.begin(), ModulePath.end(), '\\', '/');
						Linkage = ModulePath + '\x1f' + Linkage;
					}
					Initializers.emplace(Linkage,
					    std::make_pair("\t\t" + GlobalIdentifier(Variable) + " = " +
					    (Variable.Initializer ? EmitExpr(*Variable.Initializer, ValueType)
					                          : DefaultValueForType(ValueType)) + ";\n", Variable.Location));
				}
				// The checked module records static dependencies, including closed
				// generic fields, in the same order as executable global initializers.
				for (const auto& Range : CheckedModule->GlobalInitRanges)
				{
					const auto Found = Initializers.find(CheckedModule->GlobalNames[Range.GlobalSlot]);
					if (Found != Initializers.end())
					{
						EmitLine(Found->second.second);
						Out << Found->second.first;
					}
				}
				ActiveNamespace.clear();
				ActiveModulePath.clear();
				Out << "\t}\n\n";
			}

			void EmitFunction(const FFunctionDecl& Function)
			{
				if (!Function.Body || Function.bDeleted)
				{
					Fail("declaration-only or deleted functions cannot be emitted", Function.Location);
					return;
				}
				ActiveNamespace = NamespaceOf(Function.Name);
				ActiveModulePath = Function.ModulePath;
				ActiveReturnType = Function.ReturnType;
				ActiveTypeParameters.clear();
				for (const std::string& TypeParameter : Function.TypeParams)
				{
					ActiveTypeParameters.insert(NormalizeType(TypeParameter));
				}
				std::unordered_set<std::string> ReservedTypeParameterNames = {
				    FunctionIdentifier(Function)};
				for (std::size_t Index = 0; Index < Function.Parameters.size(); ++Index)
				{
					const FFunctionParam& Parameter = Function.Parameters[Index];
					ReservedTypeParameterNames.insert(Identifier(Parameter.Name));
					if (Parameter.bIsVariadic)
					{
						ReservedTypeParameterNames.insert(
						    "__VeroScriptVarargsArray" + std::to_string(Index));
					}
				}
				ConfigureTypeParameterEmissionNames(
				    Function.TypeParams, std::move(ReservedTypeParameterNames));
				LocalTypes.clear();
				LocalEmissionNames.clear();
				LocalVariableNames.clear();
				LocalConstNames.clear();
				LocalRefNames.clear();
				UsedLocalEmissionNames.clear();
				NextLocalEmissionId = 0;
				bActiveCallableHasThis = false;
				ActiveThisEmission.clear();
				ReserveActiveTypeParameterNamesForCallable();
				for (std::size_t Index = 0; Index < Function.Parameters.size(); ++Index)
				{
					if (Function.Parameters[Index].bIsVariadic)
					{
						UsedLocalEmissionNames.insert("__VeroScriptVarargsArray" + std::to_string(Index));
					}
				}
				for (const FFunctionParam& Parameter : Function.Parameters)
				{
					BindLocal(Parameter.Name, Parameter.bIsVariadic ? "array<" + ResolveAlias(Parameter.TypeName) + ">" : ResolveAlias(Parameter.TypeName),
					    AllocateSourceLocalName(Parameter.Name, false),
					    Parameter.bIsConst, Parameter.bIsRef);
				}
				EmitLine(Function.Location);
				Out << "\tprivate static ";
				if (IsReferenceTypeSpelling(Function.ReturnType))
				{
					Out << (Runtime::IsConstReferenceTypeName(Function.ReturnType)
					        ? "ref readonly "
					        : "ref ");
				}
				Out << MapType(Function.ReturnType) << ' '
				    << FunctionIdentifier(Function);
				if (!Function.TypeParams.empty())
				{
					Out << '<';
					for (std::size_t Index = 0; Index < Function.TypeParams.size(); ++Index)
					{
						if (Index != 0)
						{
							Out << ", ";
						}
						Out << "[DynamicallyAccessedMembers("
						    << "DynamicallyAccessedMemberTypes.PublicParameterlessConstructor)] "
						    << ActiveTypeParameterEmissionNames.at(
						           NormalizeType(Function.TypeParams[Index]));
					}
					Out << '>';
				}
				Out << '(';
				for (std::size_t Index = 0; Index < Function.Parameters.size(); ++Index)
				{
					if (Index != 0)
					{
						Out << ", ";
					}
					const FFunctionParam& Parameter = Function.Parameters[Index];
					if (Parameter.bIsVariadic)
					{
						Out << "params ";
					}
					else if (Parameter.bIsRef)
					{
						Out << (Parameter.bIsConst ? "in " : "ref ");
					}
					std::string Type = MapType(Parameter.TypeName);
					if (Parameter.bIsVariadic)
					{
						Type += "[]";
					}
					Out << Type << ' ';
					if (Parameter.bIsVariadic)
					{
						Out << "__VeroScriptVarargsArray" << Index;
					}
					else
					{
						Out << EmittedLocalName(Parameter.Name);
					}
				}
				Out << ")\n";
				if (!Function.Body->DefinitionModulePath.empty())
				{
					ActiveModulePath = Function.Body->DefinitionModulePath;
					ActiveNamespace = Function.Body->DefinitionNamespace;
				}
				const bool bHasVariadic = std::any_of(Function.Parameters.begin(), Function.Parameters.end(),
				    [](const FFunctionParam& Parameter)
				{
					return Parameter.bIsVariadic;
				});
				if (!bHasVariadic)
				{
					EmitBlock(*Function.Body, 1);
				}
				else
				{
					const auto SavedAliases = TypeAliases;
					Out << "\t{\n";
					for (std::size_t Index = 0; Index < Function.Parameters.size(); ++Index)
					{
						const FFunctionParam& Parameter = Function.Parameters[Index];
						if (!Parameter.bIsVariadic)
						{
							continue;
						}
						Out << "\t\t" << MapType("array<" + Parameter.TypeName + ">")
						    << ' ' << EmittedLocalName(Parameter.Name)
						    << " = new(__VeroScriptVarargsArray"
						    << Index << ");\n";
					}
					for (const std::unique_ptr<FStmt>& Statement : Function.Body->Statements)
					{
						EmitStmt(*Statement, 2);
					}
					Out << "\t}\n";
					TypeAliases = SavedAliases;
				}
				Out << "#line default\n\n";
				LocalTypes.clear();
				LocalEmissionNames.clear();
				LocalVariableNames.clear();
				LocalConstNames.clear();
				LocalRefNames.clear();
				UsedLocalEmissionNames.clear();
				bActiveCallableHasThis = false;
				ActiveThisEmission.clear();
				ActiveNamespace.clear();
				ActiveModulePath.clear();
				ActiveReturnType.clear();
				ActiveTypeParameters.clear();
				ActiveTypeParameterEmissionNames.clear();
			}

			void EmitEntryPoint()
			{
				Out << "\tpublic static int Main(string[] args)\n\t{\n";
				if (bNeedsLocalStatics)
				{
					Out << "\t\tusing var localStatics = new VsLocalStaticScope();\n";
				}
				Out
				    << "\t\tConsole.InputEncoding = Encoding.UTF8;\n"
				    << "\t\tConsole.OutputEncoding = new UTF8Encoding(false);\n"
				    << "\t\tCultureInfo.CurrentCulture = CultureInfo.InvariantCulture;\n"
				    << "\t\tCultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;\n"
				    << "\t\ttry\n\t\t{\n\t\t\tVsInitializeGlobals();\n\t\t\t";
				const bool bReturnsInt = MapType(EntryMain->ReturnType) == "int";
				if (bReturnsInt)
				{
					Out << "return ";
				}
				Out << FunctionIdentifier(*EntryMain) << '(';
				if (!EntryMain->Parameters.empty())
				{
					Out << "new List<VsString>(Array.ConvertAll(args, value => new VsString(value)))";
				}
				Out << ");\n";
				if (!bReturnsInt)
				{
					Out << "\t\t\treturn 0;\n";
				}
				Out << "\t\t}\n"
				    << "\t\tcatch (VsExitException exit)\n"
				    << "\t\t{\n\t\t\treturn exit.Code;\n\t\t}\n"
				    << "\t\tcatch (VsExceptionCarrier error)\n"
				    << "\t\t{\n\t\t\tConsole.Error.WriteLine(\"uncaught script exception: \" + VsText(error.Value)); return 1;\n\t\t}\n"
				    << "\t\tcatch (OverflowException)\n"
				    << "\t\t{\n\t\t\tConsole.Error.WriteLine(\"integer overflow\"); return 1;\n\t\t}\n"
				    << "\t\tcatch (DivideByZeroException)\n"
				    << "\t\t{\n\t\t\tConsole.Error.WriteLine(\"division by zero\"); return 1;\n\t\t}\n"
				    << "\t\tcatch (KeyNotFoundException)\n"
				    << "\t\t{\n\t\t\tConsole.Error.WriteLine(\"map key not found\"); return 1;\n\t\t}\n"
				    << "\t\tcatch (ArgumentOutOfRangeException)\n"
				    << "\t\t{\n\t\t\tConsole.Error.WriteLine(\"array index out of range\"); return 1;\n\t\t}\n"
				    << "\t\tcatch (InvalidOperationException error)\n"
				    << "\t\t{\n\t\t\tConsole.Error.WriteLine(error.Message); return 1;\n\t\t}\n"
				    << "\t\tfinally\n\t\t{\n\t\t\tVsConsoleRestore();\n\t\t}\n"
				    << "\t}\n";
			}

			std::unordered_map<std::string, std::string> TypeAliases;
			std::unordered_set<std::string> EnumTypeNames;
			std::unordered_set<std::string> UserTypeNames;
			std::unordered_set<std::string> FunctionNames;
			std::unordered_map<std::string, std::string> QualifiedEnumMembers;
			std::unordered_map<std::string, std::string> InjectedEnumMembers;
			std::unordered_map<std::string, std::string> EmittedEnumMemberTypes;
			std::vector<const FVarDeclStmt*> Globals;
			std::unordered_map<const FVarDeclStmt*, std::string> GlobalTypes;
			std::unordered_map<std::string, std::string> LocalTypes;
			std::unordered_map<std::string, std::string> LocalEmissionNames;
			// Fields remain in LocalTypes for existing type/call resolution. Keep
			// lexical variables separate so lambda capture lowering never mistakes
			// an instance or static field for a local value capture.
			std::unordered_set<std::string> LocalVariableNames;
			std::unordered_set<std::string> LocalConstNames;
			std::unordered_set<std::string> LocalRefNames;
			std::unordered_map<std::string, std::vector<const FFunctionDecl*>> FunctionDecls;
			std::unordered_map<std::string, std::string> EnumUnderlyingTypes;
			std::unordered_map<std::string, const FHostTypeDesc*> HostTypes;
			std::unordered_set<std::string> ScopedEnumTypes;
			std::unordered_set<std::string> ActiveTypeParameters;
			std::unordered_map<std::string, std::string> ActiveTypeParameterEmissionNames;
			std::unordered_map<const FFieldDecl*, std::string> UserFieldEmissionNames;
			std::unordered_map<const FFieldDecl*, std::string> StaticFieldInitializerNames;
			std::unordered_map<const FMethodDecl*, std::string> UserMethodEmissionNames;
			std::string ActiveTypeName;
			std::vector<FCustomFunctionType> CustomFunctionTypes;
			std::unordered_map<std::string, std::size_t> IndexedCustomFunctionTypes;
			std::vector<std::string> ActiveCatchRefRebinds;
			const FFunctionDecl* EntryMain = nullptr;
			std::uint32_t NextExceptionId = 0;
			std::uint32_t NextLocalEmissionId = 0;
			std::unordered_set<std::string> UsedLocalEmissionNames;
			std::string ActiveNamespace;
			std::string ActiveModulePath;
			std::string ActiveReturnType;
			bool bActiveCallableHasThis = false;
			// Non-empty while a lambda body uses an explicitly captured class
			// receiver or struct value snapshot. Nested lambdas capture this value
			// again instead of referring to C#'s closure-object `this`.
			std::string ActiveThisEmission;
			FSourceLocation ActiveLocation = {};
			std::unordered_map<std::string, std::unordered_set<std::size_t>> UserTypeArities;
			bool bHasStandardEventDelegate = false;
		};

		bool ValidateCSharpInitialization(const FBytecodeModule& Module, std::string& OutError)
		{
			std::vector<bool> Initialized(Module.GlobalNames.size(), false);
			auto Completed = Module.GlobalInitRanges;
			std::sort(Completed.begin(), Completed.end(), [](const auto& Left, const auto& Right)
			{
				return Left.EndOffset < Right.EndOffset;
			});
			std::size_t NextCompleted = 0;
			std::unordered_map<std::uint16_t, std::uint16_t> GlobalClosures;
			std::unordered_map<std::uint16_t, decltype(GlobalClosures)> ActiveFunctions, ActiveTypes;
			const auto CheckInspectionDepth = [&]()
			{
				// Acyclic startup calls and field initializers still consume native
				// recursion frames. Reject them before exhausting the generator's stack.
				constexpr std::size_t MaxInspectionDepth = 256;
				if (ActiveFunctions.size() + ActiveTypes.size() <= MaxInspectionDepth) return true;
				OutError = CSharpDiagnostic("startup function/type inspection depth requires the Core runtime");
				return false;
			};
			bool bTrackGlobalWrites = true;
			const auto IsUnsafe = [&](const std::size_t Slot)
			{
				return !Initialized[Slot] && Module.GlobalTypeNames[Slot] != "int" &&
				    !Module.GlobalMetadata[Slot].bLocalStatic;
			};
			const auto CheckRecursiveContext = [&](const decltype(GlobalClosures)& Previous)
			{
				if (Previous == GlobalClosures) return true;
				for (std::size_t Slot = 0; Slot < Initialized.size(); ++Slot)
				{
					if (IsUnsafe(Slot))
					{
						OutError = CSharpDiagnostic("recursive startup callable binding changes with uninitialized non-int global/static storage require the Core runtime");
						return false;
					}
				}
				return true;
			};
			std::function<bool(const std::vector<std::uint8_t>&, bool)> Inspect;
			const auto InspectFunction = [&](const std::uint16_t Index)
			{
				const auto [Found, Entered] = ActiveFunctions.emplace(Index, GlobalClosures);
				if (!Entered) return CheckRecursiveContext(Found->second);
				if (!CheckInspectionDepth()) return false;
				const bool Result = Inspect(Module.Functions[Index].Code, false);
				ActiveFunctions.erase(Index);
				return Result;
			};
			Inspect = [&](const std::vector<std::uint8_t>& Code, const bool bEntry)
			{
				bool bStraightLine = true;
				for (std::size_t Offset = 0; !bEntry && Offset < Code.size();)
				{
					const auto Opcode = static_cast<EOpcode>(Code[Offset]);
					bStraightLine = bStraightLine && Opcode != EOpcode::Jmp && Opcode != EOpcode::JmpIfFalse &&
					    Opcode != EOpcode::JmpIfLocalGeImm;
					Offset += Bytecode::InstructionSize(Opcode);
				}
				std::unordered_map<std::uint16_t, std::uint16_t> LocalClosures;
				std::optional<std::uint16_t> LastClosure;
				for (std::size_t Offset = 0; Offset < Code.size();)
				{
					if (bEntry)
					{
						ActiveFunctions.clear();
						ActiveTypes.clear();
						while (NextCompleted < Completed.size() && Completed[NextCompleted].EndOffset <= Offset)
						{
							Initialized[Completed[NextCompleted++].GlobalSlot] = true;
							bStraightLine = true;
						}
					}
					const auto Opcode = static_cast<EOpcode>(Code[Offset]);
					const auto Size = Bytecode::InstructionSize(Opcode);
					const auto Operand = Size >= 3 ? Bytecode::ReadU16LE(Code.data() + Offset + 1) : std::uint16_t{0};
					std::optional<std::uint16_t> NextClosure;
					if (bEntry && (Opcode == EOpcode::Jmp || Opcode == EOpcode::JmpIfFalse || Opcode == EOpcode::JmpIfLocalGeImm))
					{
						bStraightLine = false;
					}
					const bool bPreviousWrites = bTrackGlobalWrites;
					bTrackGlobalWrites = bTrackGlobalWrites && bStraightLine;
					if ((Opcode == EOpcode::LoadGlobal || Opcode == EOpcode::MakeGlobalRef) && IsUnsafe(Operand))
					{
						OutError = CSharpDiagnostic("startup reads or references of uninitialized non-int global/static storage require the Core runtime");
						return false;
					}
					if (Opcode == EOpcode::Call || Opcode == EOpcode::CallBoundMethod)
					{
						if (!InspectFunction(Operand)) return false;
					}
					else if (Opcode == EOpcode::MakeClosure)
					{
						// Merely creating a callable does not execute its deferred body.
						NextClosure = Operand;
					}
					else if (Opcode == EOpcode::CallIndirect)
					{
						if (LastClosure && !InspectFunction(*LastClosure)) return false;
						if (!LastClosure)
						{
							for (std::size_t Slot = 0; Slot < Initialized.size(); ++Slot)
							{
								if (IsUnsafe(Slot))
								{
									OutError = CSharpDiagnostic("unresolved startup callable targets with uninitialized non-int global/static storage require the Core runtime");
									return false;
								}
							}
						}
					}
					else if (Opcode == EOpcode::LoadGlobal || Opcode == EOpcode::LoadLocal)
					{
						const auto& Closures = Opcode == EOpcode::LoadGlobal ? GlobalClosures : LocalClosures;
						if (const auto Found = Closures.find(Operand); Found != Closures.end())
						{
							NextClosure = Found->second;
						}
					}
					else if (Opcode == EOpcode::StoreGlobal || Opcode == EOpcode::StoreLocal || Opcode == EOpcode::BindLocal)
					{
						auto& Closures = Opcode == EOpcode::StoreGlobal ? GlobalClosures : LocalClosures;
						if (bStraightLine && LastClosure && (Opcode != EOpcode::StoreGlobal || bTrackGlobalWrites))
						{
							Closures[Operand] = *LastClosure;
						}
						else
						{
							Closures.erase(Operand);
						}
						if (bStraightLine && bTrackGlobalWrites && Opcode == EOpcode::StoreGlobal)
						{
							Initialized[Operand] = true;
						}
					}
					else if (Opcode == EOpcode::CallMethod)
					{
						const bool bPreviousWrites = bTrackGlobalWrites;
						bTrackGlobalWrites = false;
						for (std::size_t Index = 0; Index < Module.Functions.size(); ++Index)
						{
							const auto& Function = Module.Functions[Index];
							if (Function.bIsMethod && MethodLinkageLeafMatches(Function.Name, Module.StringPool[Operand]) &&
								!InspectFunction(static_cast<std::uint16_t>(Index)))
							{
								return false;
							}
						}
						bTrackGlobalWrites = bPreviousWrites;
					}
					else if (Opcode == EOpcode::NewObject)
					{
						const auto [Found, Entered] = ActiveTypes.emplace(Operand, GlobalClosures);
						if (Entered)
						{
							if (!CheckInspectionDepth()) return false;
							for (const auto& Field : Module.Types[Operand].Fields)
							{
								if (Field.bHasInitCode && !Inspect(Field.InitCode, false)) return false;
							}
							ActiveTypes.erase(Operand);
						}
						else if (!CheckRecursiveContext(Found->second))
						{
							return false;
						}
					}
					LastClosure = NextClosure;
					bTrackGlobalWrites = bPreviousWrites;
					if (bStraightLine && (Opcode == EOpcode::Return || Opcode == EOpcode::Throw))
					{
						break;
					}
					Offset += Size;
				}
				return true;
			};
			return Inspect(Module.InitCode, true);
		}

		bool CompileCheck(
		    const FProgram& Program,
		    std::string& OutError,
		    const IHostBindingCatalog* const Bindings,
		    std::unique_ptr<FBytecodeModule>& OutModule, std::vector<FMethodBindingCodegenInfo>& OutBindings)
		{
			if (RequiresCoreTemplateExport(Program))
			{
				OutError = "AOT: extended template applications require the Core AOT exporter";
				return false;
			}
			VeroScriptCompiler Compiler;
			Compiler.SetCollectRuntimeMemoryTypes(true);
			std::unique_ptr<FBytecodeModule> Module = Compiler.Compile(
			    Program,
			    Bindings ? &Bindings->GetNativeFunctions() : nullptr,
			    Bindings);
			if (!Module || Compiler.HasError())
			{
				OutError = Compiler.HasError()
				    ? Compiler.GetError().Message
				    : CSharpDiagnostic("compile check failed");
				return false;
			}
			if (Numerics::UsesRuntimeNumerics(*Module, Bindings ? &Bindings->GetNativeFunctions() : nullptr))
			{
				OutError = "AOT: bigint/rational require the VeroScript numeric runtime; standalone output is unsupported";
				return false;
			}
			if (Variant::UsesRuntime(*Module, Bindings ? &Bindings->GetNativeFunctions() : nullptr))
			{
				OutError = "diag.aot_variant_unsupported: variant requires the VeroScript runtime; standalone output is unsupported";
				return false;
			}
			if (StandardLibrary::UsesRuntimeFoundation(*Module))
			{
				OutError = "AOT: standard-library foundation operations require the VeroScript runtime; standalone output is unsupported";
				return false;
			}
			if (Compiler.UsesRuntimeMemoryTypes() ||
			    Memory::UsesRuntimeMemory(*Module, Bindings ? &Bindings->GetNativeFunctions() : nullptr))
			{
				OutError =
				    "AOT: std::memory requires the VeroScript runtime; standalone output is unsupported";
				return false;
			}
			if (!ValidateCSharpInitialization(*Module, OutError)) return false;

			OutBindings = Compiler.GetMethodBindingCodegenInfo();
			OutModule = std::move(Module);
			return true;
		}

		bool EmitCSharpFromProgramInternal(
		    const FProgram& InputProgram,
		    std::string& OutCSharp,
		    std::string& OutError,
		    const IHostBindingCatalog* const Bindings,
		    std::string* const OutRuntimeFragment = nullptr,
		    std::string* const OutProgramFragment = nullptr,
		    std::string* const OutEntryFragment = nullptr)
		{
			OutCSharp.clear();
			OutError.clear();
			auto PreparedInput = PrepareCodegenProgram(InputProgram, {}, Bindings, OutError);
			if (!PreparedInput) return false;
			const auto& Program = *PreparedInput;

			std::unique_ptr<FBytecodeModule> CheckedModule;
			std::vector<FMethodBindingCodegenInfo> MethodBindings;
			if (!CompileCheck(Program, OutError, Bindings, CheckedModule, MethodBindings))
			{
				return false;
			}
			auto ConstantProgram = CloneProgramWithConstantValues(Program, *CheckedModule, OutError);
			for (const auto& Function : CheckedModule->Functions)
			{
				for (std::size_t Ip = 0; Ip < Function.Code.size();)
				{
					const auto Op = static_cast<EOpcode>(Function.Code[Ip]);
					if (Op == EOpcode::LoadGlobal || Op == EOpcode::StoreGlobal || Op == EOpcode::MakeGlobalRef)
					{
						const auto Slot = Bytecode::ReadU16LE(Function.Code.data() + Ip + 1u);
						if (Slot < CheckedModule->GlobalMetadata.size() && CheckedModule->GlobalMetadata[Slot].bLocalStatic && std::none_of(Function.LocalStaticInitializers.begin(), Function.LocalStaticInitializers.end(), [&](const auto& Initializer)
						{
							return Initializer.GlobalSlot == Slot;
						}))
						{
							OutError = "AOT: references to enclosing local static storage require the Core AOT exporter";
							return false;
						}
					}
					Ip += Bytecode::InstructionSize(Op);
				}
			}
			if (!ConstantProgram) return false;
			if (HasScopedTypeDeclarations(*ConstantProgram))
			{
				OutError = "AOT: nested and local types require the VeroScript runtime; standalone output is unsupported";
				return false;
			}
			if (Variant::UsesRuntime(*ConstantProgram))
			{
				OutError = "diag.aot_variant_unsupported: variant requires the VeroScript runtime; standalone output is unsupported";
				return false;
			}
			FCSharpEmitter Emitter;
			Emitter.Program = ConstantProgram.get();
			Emitter.CheckedModule = CheckedModule.get();
			Emitter.MethodBindings = std::move(MethodBindings);
			Emitter.HostBindings = Bindings;
			Emitter.UnusedModules = CollectUnusedCoreImplementations(Program, *CheckedModule);
			if (!Emitter.Emit())
			{
				OutError = Emitter.Error;
				return false;
			}
			OutCSharp = Emitter.Out.str();
			if (OutRuntimeFragment)
			{
				*OutRuntimeFragment = Emitter.RuntimeFragment();
			}
			if (OutProgramFragment)
			{
				*OutProgramFragment = Emitter.ProgramFragment();
			}
			if (OutEntryFragment)
			{
				*OutEntryFragment = Emitter.EntryFragment();
			}
			return true;
		}

		// Use resolved calls for executable dependencies and scoped AST references
		// for declarations, including uninstantiated generics.
		// The full program is validated first. Globals/static initialization and
		// package declarations remain roots; unknown source graphs are left intact.
		std::unordered_set<std::string> CollectUnusedCoreImplementations(
		    const FProgram& Program, const FBytecodeModule& CheckedModule)
		{
			if (!Program.PrecompiledPackageModules.empty() || Program.PackageSourceModules.empty())
			{
				return {};
			}
			std::unordered_map<std::string, std::unordered_set<std::string>> Definitions;
			std::unordered_map<std::string, std::unordered_set<std::string>> References;
			std::unordered_map<std::string, std::unordered_set<std::string>> Dependencies;
			std::unordered_map<std::string, std::unordered_set<std::string>> FunctionDefinitions;
			std::unordered_set<std::string> Retained{Program.EntryModulePath};
			std::unordered_map<std::string, const FPackageSourceModule*> Sources;
			std::vector<const FVarDeclStmt*> Globals;
			std::unordered_set<std::string> Candidates;
			std::vector<std::string> CoreModules;
			for (const auto& Module : Program.PackageSourceModules)
			{
				Sources.emplace(Module.ModulePath, &Module);
				if (PathUtil::FromUtf8(Module.ModulePath).filename() == "core.vs")
				{
					Candidates.insert(Module.ModulePath);
					CoreModules.push_back(Module.ModulePath);
				}
			}
			if (Candidates.empty()) return {};
			for (std::size_t Index = 0; Index < CoreModules.size(); ++Index)
			{
				const auto Found = Sources.find(CoreModules[Index]);
				if (Found == Sources.end()) return {};
				for (const auto& Import : Found->second->Imports)
				{
					if (Candidates.insert(Import.ResolvedPath).second)
					{
						CoreModules.push_back(Import.ResolvedPath);
					}
				}
			}
			for (const auto& Module : Program.PackageSourceModules)
			{
				if (!Candidates.count(Module.ModulePath))
				{
					Retained.insert(Module.ModulePath);
				}
			}

			// Source type tokens use public std aliases, while AST declarations carry
			// their canonical runtime names. Both spellings must retain the module.
			std::unordered_map<std::string, std::unordered_set<std::string>> StandardSpellings;
			const auto IndexStandardNames = [&](const StandardLibrary::FNameAlias* Aliases, std::size_t Count)
			{
				for (std::size_t Index = 0; Index < Count; ++Index)
				{
					const auto& Alias = Aliases[Index];
					const auto Dot = Alias.QualifiedName.rfind('.');
					StandardSpellings[std::string(Alias.CanonicalName)].insert(
					    std::string(Alias.QualifiedName.substr(Dot == std::string_view::npos ? 0 : Dot + 1)));
				}
			};
			std::size_t AliasCount = 0;
			const auto* TypeAliases = StandardLibrary::GetTypeAliases(AliasCount);
			IndexStandardNames(TypeAliases, AliasCount);

			const auto AddDefinition = [&](const std::string& Name, const std::string& Module)
			{
				Definitions[Name].insert(Module);
				const auto Dot = Name.rfind('.');
				Definitions[Name.substr(Dot == std::string::npos ? 0 : Dot + 1)].insert(Module);
				if (const auto Found = StandardSpellings.find(Name); Found != StandardSpellings.end())
				{
					for (const auto& Spelling : Found->second)
					{
						Definitions[Spelling].insert(Module);
					}
				}
			};
			for (const auto& Function : Program.Functions)
			{
				FunctionDefinitions[Function.Name].insert(Function.ModulePath);
				const auto Dot = Function.Name.rfind('.');
				FunctionDefinitions[Function.Name.substr(Dot == std::string::npos ? 0 : Dot + 1)]
				    .insert(Function.ModulePath);
			}
			for (const auto& Alias : Program.TypeAliases)
			{
				AddDefinition(Alias.Name, Alias.ModulePath);
			}
			for (const auto& Enum : Program.Enums)
			{
				AddDefinition(Enum.Name, Enum.ModulePath);
				for (const auto& Member : Enum.Members)
				{
					AddDefinition(Member.Name, Enum.ModulePath);
				}
			}
			const auto AddTypes = [&](const auto& Types)
			{
				for (const auto& Type : Types)
				{
					AddDefinition(Type.Name, Type.ModulePath);
					for (const auto& Field : Type.Fields)
					{
						if (Field.bIsStatic && Field.DefaultValue)
						{
							Retained.insert(Type.ModulePath);
						}
					}
				}
			};
			AddTypes(Program.Classes);
			AddTypes(Program.Structs);
			for (const auto& Interface : Program.Interfaces)
			{
				AddDefinition(Interface.Name, Interface.ModulePath);
			}
			for (const auto& Statement : Program.TopLevelStatements)
			{
				if (Statement)
				{
					Retained.insert(std::string(Statement->Location.FileLabel));
					if (Statement->Kind == EStmtKind::VarDecl)
					{
						Globals.push_back(&static_cast<const FVarDeclStmt&>(*Statement));
					}
					else if (Statement->Kind == EStmtKind::VarDeclGroup)
					{
						for (const auto& Global : static_cast<const FVarDeclGroupStmt&>(*Statement).Declarations)
						{
							if (Global)
							{
								Globals.push_back(Global.get());
							}
						}
					}
				}
			}
			const auto VisibleNames = [&](std::unordered_set<std::string> Names, const std::string& Module)
			{
				for (const auto* Global : Globals)
				{
					if (ModuleVisibility::CanAccessSymbol(Program, Global->ModulePath, Global->bExported, Module))
					{
						Names.erase(Global->Name);
					}
				}
				return Names;
			};
			const auto AddCodeDependencies = [&](const std::vector<std::uint8_t>& Code,
			                                     const std::string& OwnerModule)
			{
				for (std::size_t Offset = 0; Offset < Code.size();)
				{
					const auto Opcode = static_cast<EOpcode>(Code[Offset]);
					const auto Size = Bytecode::InstructionSize(Opcode);
					if (Size > Code.size() - Offset)
					{
						return false;
					}
					if (Opcode == EOpcode::Call || Opcode == EOpcode::CallBoundMethod ||
					    Opcode == EOpcode::MakeClosure)
					{
						const auto Index = Bytecode::ReadU16LE(Code.data() + Offset + 1);
						if (Index >= CheckedModule.Functions.size())
						{
							return false;
						}
						Dependencies[OwnerModule].insert(CheckedModule.Functions[Index].ModulePath);
					}
					Offset += Size;
				}
				return true;
			};
			if (!AddCodeDependencies(CheckedModule.InitCode, Program.EntryModulePath))
			{
				return {};
			}
			for (const auto& Function : CheckedModule.Functions)
			{
				if (!AddCodeDependencies(Function.Code, Function.ModulePath))
				{
					return {};
				}
			}
			std::unordered_map<std::uint16_t, std::string> TypeModules;
			for (const auto& Function : CheckedModule.Functions)
			{
				if (Function.bIsMethod)
				{
					TypeModules.emplace(Function.OwnerTypeIndex, Function.ModulePath);
				}
			}
			for (std::size_t Index = 0; Index < CheckedModule.Types.size(); ++Index)
			{
				for (const auto& Field : CheckedModule.Types[Index].Fields)
				{
					if (Field.InitCode.empty())
					{
						continue;
					}
					const auto Owner = TypeModules.find(static_cast<std::uint16_t>(Index));
					if (Owner == TypeModules.end() || !AddCodeDependencies(Field.InitCode, Owner->second))
					{
						return {};
					}
				}
			}

			// Open generic bodies have no bytecode until instantiated, but they are
			// emitted as C# declarations. Visit their free-symbol expressions too;
			// the existing scoped walker excludes locals and instance member names.
			std::unordered_set<std::string> FunctionNames;
			for (const auto& [Name, Modules] : FunctionDefinitions)
			{
				FunctionNames.insert(Name);
			}
			FCSharpEmitter ReferenceCollector;
			const auto AddExpressionDependencies = [&](const FExpr& Expression,
			                                           const std::string& OwnerModule, const std::unordered_set<std::string>& Names)
			{
				std::unordered_set<std::string> Used;
				ReferenceCollector.CollectReferencedLocals(Expression, Names, Used);
				for (const auto& Name : Used)
				{
					for (const auto& Module : FunctionDefinitions[Name])
					{
						Dependencies[OwnerModule].insert(Module);
					}
				}
			};
			const auto AddBodyDependencies = [&](const auto& Decl, const std::string& OwnerModule,
			                                     std::unordered_set<std::string> Names)
			{
				std::unordered_set<std::string> Used;
				for (const auto& Parameter : Decl.Parameters)
				{
					if (Parameter.DefaultValue)
					{
						ReferenceCollector.CollectReferencedLocals(*Parameter.DefaultValue, Names, Used);
					}
					Names.erase(Parameter.Name);
				}
				if (Decl.Body)
				{
					ReferenceCollector.CollectReferencedLocals(*Decl.Body, Names, Used);
				}
				for (const auto& Name : Used)
				{
					for (const auto& Module : FunctionDefinitions[Name])
					{
						Dependencies[OwnerModule].insert(Module);
					}
				}
			};
			for (const auto& Function : Program.Functions)
			{
				if (!Function.TypeParams.empty())
				{
					AddBodyDependencies(Function, Function.ModulePath, VisibleNames(FunctionNames, Function.ModulePath));
				}
			}
			const auto AddGenericTypeDependencies = [&](const auto& Types)
			{
				for (const auto& Type : Types)
				{
					if (Type.TypeParams.empty())
					{
						continue;
					}
					auto Names = VisibleNames(FunctionNames, Type.ModulePath);
					for (const auto& Field : Type.Fields)
					{
						Names.erase(Field.Name);
					}
					for (const auto& Method : Type.Methods)
					{
						Names.erase(Method.Name);
					}
					for (const auto& Field : Type.Fields)
					{
						if (Field.DefaultValue)
						{
							AddExpressionDependencies(*Field.DefaultValue, Type.ModulePath, Names);
						}
					}
					for (const auto& Method : Type.Methods)
					{
						AddBodyDependencies(Method, Type.ModulePath, Names);
					}
					for (const auto& Constructor : Type.Constructors)
					{
						AddBodyDependencies(Constructor, Type.ModulePath, Names);
						auto ConstructorNames = Names;
						for (const auto& Parameter : Constructor.Parameters)
						{
							ConstructorNames.erase(Parameter.Name);
						}
						const auto AddInitializerDependencies = [&](const auto& Initializers)
						{
							for (const auto& Initializer : Initializers)
							{
								for (const auto& Argument : Initializer.Args)
								{
									if (Argument)
									{
										AddExpressionDependencies(*Argument, Type.ModulePath, ConstructorNames);
									}
								}
							}
						};
						AddInitializerDependencies(Constructor.BaseInits);
						AddInitializerDependencies(Constructor.FieldInits);
					}
				}
			};
			AddGenericTypeDependencies(Program.Classes);
			AddGenericTypeDependencies(Program.Structs);
			std::unordered_set<std::string> TypeNames;
			for (const auto& [Name, Modules] : Definitions)
			{
				TypeNames.insert(Name);
			}
			std::unordered_map<std::string, std::unordered_set<std::string>> TypeReferences;
			const auto AddTypeExpression = [&](const FExpr& Expression, const std::string& Module,
			                                   const std::unordered_set<std::string>& Names)
			{
				ReferenceCollector.CollectReferencedLocals(Expression, Names, References[Module], &TypeReferences[Module]);
			};
			const auto AddTypedBody = [&](const auto& Decl, const std::string& Module,
			                              std::unordered_set<std::string> Names)
			{
				for (const auto& Parameter : Decl.Parameters)
				{
					TypeReferences[Module].insert(Parameter.TypeName);
					if (Parameter.DefaultValue)
					{
						AddTypeExpression(*Parameter.DefaultValue, Module, Names);
					}
					Names.erase(Parameter.Name);
				}
				if (Decl.Body)
				{
					ReferenceCollector.CollectReferencedLocals(*Decl.Body, Names, References[Module], &TypeReferences[Module]);
				}
			};
			for (const auto& Function : Program.Functions)
			{
				TypeReferences[Function.ModulePath].insert(Function.ReturnType);
				AddTypedBody(Function, Function.ModulePath, VisibleNames(TypeNames, Function.ModulePath));
			}
			for (const auto& Alias : Program.TypeAliases)
			{
				TypeReferences[Alias.ModulePath].insert(Alias.TypeName);
			}
			const auto AddTypeDeclarations = [&](const auto& Types)
			{
				for (const auto& Type : Types)
				{
					auto Names = VisibleNames(TypeNames, Type.ModulePath);
					auto& Annotations = TypeReferences[Type.ModulePath];
					for (const auto& Field : Type.Fields)
					{
						Names.erase(Field.Name);
					}
					for (const auto& Method : Type.Methods)
					{
						Names.erase(Method.Name);
					}
					for (const auto& Field : Type.Fields)
					{
						Annotations.insert(Field.TypeName);
						if (Field.DefaultValue)
						{
							AddTypeExpression(*Field.DefaultValue, Type.ModulePath, Names);
						}
					}
					for (const auto& Method : Type.Methods)
					{
						Annotations.insert(Method.ReturnType);
						AddTypedBody(Method, Type.ModulePath, Names);
					}
					for (const auto& Default : Type.TypeParamDefaults)
					{
						Annotations.insert(Default);
					}
					for (const auto& Bound : Type.Bounds)
					{
						Annotations.insert(Bound.InterfaceTypeName);
					}
					if constexpr (std::is_same_v<std::decay_t<decltype(Type)>, FClassDecl>)
					{
						for (const auto& Base : Type.BaseTypeNames)
						{
							Annotations.insert(Base);
						}
						if (Type.ClassDefaults)
						{
							ReferenceCollector.CollectReferencedLocals(*Type.ClassDefaults, Names,
							    References[Type.ModulePath], &Annotations);
						}
					}
					else
					{
						Annotations.insert(Type.BaseTypeName);
					}
					for (const auto& Interface : Type.InterfaceTypeNames)
					{
						Annotations.insert(Interface);
					}
					for (const auto& Constructor : Type.Constructors)
					{
						AddTypedBody(Constructor, Type.ModulePath, Names);
						auto ConstructorNames = Names;
						for (const auto& Parameter : Constructor.Parameters)
						{
							ConstructorNames.erase(Parameter.Name);
						}
						const auto AddInitializers = [&](const auto& Initializers)
						{
							for (const auto& Initializer : Initializers)
							{
								for (const auto& Argument : Initializer.Args)
								{
									if (Argument)
									{
										AddTypeExpression(*Argument, Type.ModulePath, ConstructorNames);
									}
								}
							}
						};
						AddInitializers(Constructor.BaseInits);
						AddInitializers(Constructor.FieldInits);
					}
					if (Type.Destructor && Type.Destructor->Body)
					{
						ReferenceCollector.CollectReferencedLocals(*Type.Destructor->Body, Names,
						    References[Type.ModulePath], &Annotations);
					}
				}
			};
			AddTypeDeclarations(Program.Classes);
			AddTypeDeclarations(Program.Structs);
			for (const auto& Statement : Program.TopLevelStatements)
			{
				if (Statement)
				{
					const auto Module = Statement->ModulePath.empty()
					    ? std::string(Statement->Location.FileLabel)
					    : Statement->ModulePath;
					ReferenceCollector.CollectReferencedLocals(*Statement, VisibleNames(TypeNames, Module),
					    References[Module], &TypeReferences[Module]);
				}
			}
			for (const auto& [Module, Types] : TypeReferences)
			{
				for (const auto& Type : Types)
				{
					VeroScriptLexer Lexer(Type, Module);
					for (const auto& Token : Lexer.Tokenize())
					{
						if (Token.Kind == ETokenKind::Identifier || Token.Kind == ETokenKind::Keyword)
						{
							References[Module].insert(std::string(Token.Lexeme));
						}
					}
				}
			}
			for (const auto& Module : Program.PackageSourceModules)
			{
				if (Module.bFromPackage || Module.bDeclarationOnly)
				{
					return {};
				}
				References.try_emplace(Module.ModulePath);
			}
			for (const auto& [Name, Modules] : Definitions)
			{
				for (const auto& Module : Modules)
				{
					if (!References.count(Module))
					{
						Retained.insert(Module);
					}
				}
			}
			std::vector<std::string> Pending(Retained.begin(), Retained.end());
			for (std::size_t Index = 0; Index < Pending.size(); ++Index)
			{
				for (const auto& Module : Dependencies[Pending[Index]])
				{
					if (Retained.insert(Module).second)
					{
						Pending.push_back(Module);
					}
				}
				for (const auto& Name : References[Pending[Index]])
				{
					const auto Found = Definitions.find(Name);
					if (Found == Definitions.end())
					{
						continue;
					}
					for (const auto& Module : Found->second)
					{
						if (Retained.insert(Module).second)
						{
							Pending.push_back(Module);
						}
					}
				}
			}
			for (const auto& Module : Retained)
			{
				Candidates.erase(Module);
			}
			return Candidates;
		}

		std::unique_ptr<FProgram> LoadCSharpProgram(
		    const std::string_view Source,
		    const std::string_view SourceLabel,
		    std::string& OutError,
		    const FCodegenOptions& Options,
		    const bool bLoadEntryFile = false)
		{
			if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
			{
				auto Effective = Options;
				if (!ApplyCodegenModuleEnvironment(Effective, OutError)) return {};
				return LoadCSharpProgram(Source, SourceLabel, OutError, Effective, bLoadEntryFile);
			}
			const std::string OwnedSource(Source);
			const std::string Label(SourceLabel.empty() && !bLoadEntryFile ? "csharp_aot.vs" : std::string(SourceLabel));
			VeroScriptModuleLoader Loader(Label, Options.CompileProcessing.ModuleEnvironment);
			Loader.SetCompileProcessing(Options.CompileProcessing);
			Loader.SetScriptCompileExtensions(Options.ScriptCompileExtensions);
			for (const auto& Overlay : Options.SourceOverlays)
			{
				Loader.SetSourceOverlay(Overlay.Path, Overlay.Source);
			}

			for (const auto& Path : Options.StdlibSearchPaths)
			{
				Loader.AddStdlibSearchRoot(Path);
			}

			for (const std::string& Path : Options.ImportPaths)
			{
				Loader.AddImportPath(Path);
			}
			for (const std::string& Path : Options.ImplicitImportPaths)
			{
				Loader.AddImplicitImportPath(Path);
			}
			Loader.SetAllowCircularImports(Options.bAllowCircularImports);
			Loader.SetAllowProjectWideAccess(Options.bAllowProjectWideAccess);
			Loader.SetDefines(Options.Defines);
			Loader.SetPredefinedMacros(Options.PredefinedMacros);
			if (Options.Bindings)
			{
				std::unordered_set<std::string> HostTypeNames;
				for (const FHostTypeDesc& Type : Options.Bindings->GetTypes())
				{
					HostTypeNames.insert(Type.Name);
				}
				for (const FHostTypeAliasDesc& Alias : Options.Bindings->GetTypeAliases())
				{
					HostTypeNames.insert(Alias.Name);
				}
				for (const FGenericHostTypeTemplateDesc& Template :
				    Options.Bindings->GetGenericTypeTemplates())
				{
					HostTypeNames.insert(Template.Name);
				}
				Loader.SetHostTypeNames(std::move(HostTypeNames));
			}
			if (!bLoadEntryFile)
			{
				Loader.SetSourceOverlay(Label, OwnedSource);
			}

			std::unique_ptr<FProgram> Program =
			    bLoadEntryFile ? Loader.LoadMainModule() : Loader.LoadSourceModule(OwnedSource, Label);
			if (!Program || Loader.HasError())
			{
				OutError = Loader.HasError()
				    ? Loader.GetError().Message
				    : CSharpDiagnostic("module load failed");
				return nullptr;
			}
			return PrepareCodegenProgram(std::move(Program), Options.AnnotationProcessing, Options.Bindings, OutError);
		}

		bool EmitCSharpFromSourceInternal(
		    const std::string_view Source,
		    const std::string_view SourceLabel,
		    std::string& OutCSharp,
		    std::string& OutError,
		    const FCodegenOptions& Options)
		{
			OutCSharp.clear();
			OutError.clear();
			const std::unique_ptr<FProgram> Program =
			    LoadCSharpProgram(Source, SourceLabel, OutError, Options);
			if (!Program)
			{
				return false;
			}
			return EmitCSharpFromProgramInternal(*Program, OutCSharp, OutError, Options.Bindings);
		}

		bool ReadTextFile(const std::string& Path, std::string& OutText)
		{
			std::ifstream Input(PathUtil::FromUtf8(Path), std::ios::in | std::ios::binary);
			if (!Input)
			{
				return false;
			}
			std::ostringstream Buffer;
			Buffer << Input.rdbuf();
			OutText = Buffer.str();
			return static_cast<bool>(Input) || Input.eof();
		}

		bool WriteTextFileAtomically(
		    const std::string& Path,
		    const std::string& Text,
		    std::string& OutError)
		{
			const std::filesystem::path FinalPath = PathUtil::FromUtf8(Path);
			std::error_code FileError;
			if (!FinalPath.parent_path().empty())
			{
				std::filesystem::create_directories(FinalPath.parent_path(), FileError);
				if (FileError)
				{
					OutError = CSharpDiagnostic("cannot create output directory: " + PathUtil::ToUtf8(FinalPath.parent_path()) + ": " + FileError.message());
					return false;
				}
			}
			const std::filesystem::path TemporaryPath =
			    PathUtil::MakeTemporarySiblingPath(FinalPath);
			std::ofstream Output(TemporaryPath,
			    std::ios::out | std::ios::binary | std::ios::trunc);
			if (!Output)
			{
				OutError = CSharpDiagnostic("cannot write output: " + Path);
				return false;
			}
			Output.write(Text.data(), static_cast<std::streamsize>(Text.size()));
			Output.close();
			if (!Output)
			{
				std::filesystem::remove(TemporaryPath, FileError);
				OutError = CSharpDiagnostic("failed while writing output: " + Path);
				return false;
			}
			if (!PathUtil::ReplaceFileAtomically(TemporaryPath, FinalPath, FileError))
			{
				const std::string ReplaceError = FileError.message();
				std::error_code CleanupError;
				std::filesystem::remove(TemporaryPath, CleanupError);
				OutError = CSharpDiagnostic("cannot replace output: " + Path + ": " + ReplaceError);
				return false;
			}
			return true;
		}
	}

	bool EmitCSharpFromProgram(
	    const FProgram& Program,
	    std::string& OutCSharp,
	    std::string& OutError)
	{
		return EmitCSharpFromProgramInternal(Program, OutCSharp, OutError, nullptr);
	}

	bool BuildCSharpSourceBundle(
	    const std::string_view GeneratedCSharp,
	    const std::string_view BundleName,
	    FCSharpAotSourceBundle& OutBundle,
	    std::string& OutError)
	{
		OutBundle = {};
		OutError.clear();
		if (!ValidateBundleName(BundleName, OutError))
		{
			return false;
		}
		if (GeneratedCSharp.empty())
		{
			OutError = CSharpDiagnostic("generated C# source must not be empty");
			return false;
		}
		const std::string Name(BundleName);
		const std::string ProjectName = Name + ".csproj";
		const std::string SourceName = Name + ".generated.cs";
		OutBundle.Files.push_back({ProjectName, BuildSdkProject({SourceName})});
		OutBundle.Files.push_back({SourceName, std::string(GeneratedCSharp)});
		BuildBundleManifest(ProjectName, OutBundle.Files, {SourceName}, OutBundle.ManifestJson);
		return true;
	}

	bool EmitCSharpSourceBundleFromProgram(
	    const FProgram& Program,
	    const std::string_view BundleName,
	    FCSharpAotSourceBundle& OutBundle,
	    std::string& OutError)
	{
		const FCodegenOptions Options;
		return EmitCSharpSourceBundleFromProgram(
		    Program, BundleName, Options, OutBundle, OutError);
	}

	bool EmitCSharpSourceBundleFromProgram(
	    const FProgram& Program,
	    const std::string_view BundleName,
	    const FCodegenOptions& Options,
	    FCSharpAotSourceBundle& OutBundle,
	    std::string& OutError)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			OutBundle = {};
			OutError.clear();
			if (!ApplyCodegenModuleEnvironment(Effective, OutError)) return false;
			return EmitCSharpSourceBundleFromProgram(Program, BundleName, Effective, OutBundle, OutError);
		}
		OutBundle = {};
		OutError.clear();
		if (!ValidateBundleName(BundleName, OutError))
		{
			return false;
		}
		std::string Combined;
		std::string Runtime;
		std::string ProgramSource;
		std::string Entry;
		if (!EmitCSharpFromProgramInternal(
		        Program, Combined, OutError, Options.Bindings,
		        &Runtime, &ProgramSource, &Entry))
		{
			return false;
		}
		const std::string Name(BundleName);
		const std::string ProjectName = Name + ".csproj";
		const std::string RuntimeName = Name + ".runtime.g.cs";
		const std::string ProgramName = Name + ".program.g.cs";
		const std::string EntryName = Name + ".entry.g.cs";
		OutBundle.Files.push_back({ProjectName,
		    BuildSdkProject({RuntimeName, ProgramName, EntryName})});
		OutBundle.Files.push_back({RuntimeName, std::move(Runtime)});
		OutBundle.Files.push_back({ProgramName, std::move(ProgramSource)});
		OutBundle.Files.push_back({EntryName, std::move(Entry)});
		BuildBundleManifest(ProjectName, OutBundle.Files,
		    {RuntimeName, ProgramName, EntryName}, OutBundle.ManifestJson);
		return true;
	}

	bool EmitCSharpFromSource(
	    const std::string_view Source,
	    const std::string_view SourceLabel,
	    std::string& OutCSharp,
	    std::string& OutError,
	    const std::vector<std::string>& ImportPaths,
	    const std::vector<std::string>& Defines,
	    const std::vector<std::string>& ImplicitImportPaths,
	    const bool bAllowCircularImports,
	    const bool bAllowProjectWideAccess)
	{
		FCodegenOptions Options;
		Options.ImportPaths = ImportPaths;
		Options.Defines = Defines;
		Options.ImplicitImportPaths = ImplicitImportPaths;
		Options.bAllowCircularImports = bAllowCircularImports;
		Options.bAllowProjectWideAccess = bAllowProjectWideAccess;
		return EmitCSharpFromSourceInternal(Source, SourceLabel, OutCSharp, OutError, Options);
	}

	bool EmitCSharpSourceBundleFromSource(
	    const std::string_view Source,
	    const std::string_view SourceLabel,
	    const std::string_view BundleName,
	    FCSharpAotSourceBundle& OutBundle,
	    std::string& OutError,
	    const std::vector<std::string>& ImportPaths,
	    const std::vector<std::string>& Defines,
	    const std::vector<std::string>& ImplicitImportPaths,
	    const bool bAllowCircularImports,
	    const bool bAllowProjectWideAccess)
	{
		OutBundle = {};
		OutError.clear();
		FCodegenOptions Options;
		Options.ImportPaths = ImportPaths;
		Options.Defines = Defines;
		Options.ImplicitImportPaths = ImplicitImportPaths;
		Options.bAllowCircularImports = bAllowCircularImports;
		Options.bAllowProjectWideAccess = bAllowProjectWideAccess;
		return EmitCSharpSourceBundleFromSource(
		    Source, SourceLabel, BundleName, Options, OutBundle, OutError);
	}

	bool EmitCSharpSourceBundleFromSource(
	    const std::string_view Source,
	    const std::string_view SourceLabel,
	    const std::string_view BundleName,
	    const FCodegenOptions& Options,
	    FCSharpAotSourceBundle& OutBundle,
	    std::string& OutError)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			OutBundle = {};
			OutError.clear();
			if (!ApplyCodegenModuleEnvironment(Effective, OutError)) return false;
			return EmitCSharpSourceBundleFromSource(Source, SourceLabel, BundleName, Effective, OutBundle, OutError);
		}
		OutBundle = {};
		OutError.clear();
		const std::unique_ptr<FProgram> Program =
		    LoadCSharpProgram(Source, SourceLabel, OutError, Options);
		return Program && EmitCSharpSourceBundleFromProgram(*Program, BundleName, Options, OutBundle, OutError);
	}

	bool EmitCSharpFromFile(
	    const std::string_view ScriptPath,
	    const std::string_view OutputPath,
	    std::string& OutError,
	    const std::vector<std::string>& ImportPaths,
	    const std::vector<std::string>& Defines,
	    const std::vector<std::string>& ImplicitImportPaths,
	    const bool bAllowCircularImports,
	    const bool bAllowProjectWideAccess)
	{
		std::string Source;
		const std::string InputPath(ScriptPath);
		const std::string OutPath(OutputPath);
		if (PathUtil::Equal(InputPath, OutPath))
		{
			OutError = CSharpDiagnostic(
			    "output path must differ from the input script: " + InputPath);
			return false;
		}
		if (!ReadTextFile(InputPath, Source))
		{
			OutError = CSharpDiagnostic("cannot open script: " + InputPath);
			return false;
		}
		std::string CSharp;
		if (!EmitCSharpFromSource(Source, InputPath, CSharp, OutError,
		        ImportPaths, Defines, ImplicitImportPaths,
		        bAllowCircularImports, bAllowProjectWideAccess))
		{
			return false;
		}
		return WriteTextFileAtomically(OutPath, CSharp, OutError);
	}

	bool EmitCSharpSourceBundleFromFile(
	    const std::string_view ScriptPath,
	    const std::string_view BundleName,
	    FCSharpAotSourceBundle& OutBundle,
	    std::string& OutError,
	    const std::vector<std::string>& ImportPaths,
	    const std::vector<std::string>& Defines,
	    const std::vector<std::string>& ImplicitImportPaths,
	    const bool bAllowCircularImports,
	    const bool bAllowProjectWideAccess)
	{
		FCodegenOptions Options;
		Options.ImportPaths = ImportPaths;
		Options.Defines = Defines;
		Options.ImplicitImportPaths = ImplicitImportPaths;
		Options.bAllowCircularImports = bAllowCircularImports;
		Options.bAllowProjectWideAccess = bAllowProjectWideAccess;
		return EmitCSharpSourceBundleFromFile(ScriptPath, BundleName, Options, OutBundle, OutError);
	}

	bool EmitCSharpSourceBundleFromFile(
	    const std::string_view ScriptPath,
	    const std::string_view BundleName,
	    const FCodegenOptions& Options,
	    FCSharpAotSourceBundle& OutBundle,
	    std::string& OutError)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			OutBundle = {};
			OutError.clear();
			if (!ApplyCodegenModuleEnvironment(Effective, OutError)) return false;
			return EmitCSharpSourceBundleFromFile(ScriptPath, BundleName, Effective, OutBundle, OutError);
		}
		OutBundle = {};
		OutError.clear();
		const auto Program = LoadCSharpProgram({}, ScriptPath, OutError, Options, true);
		return Program && EmitCSharpSourceBundleFromProgram(*Program, BundleName, Options, OutBundle, OutError);
	}

	bool FCSharpCodegenBackend::EmitFromSource(
	    const std::string_view Source,
	    const std::string_view SourceLabel,
	    const FCodegenOptions& Options,
	    FCodegenResult& OutResult)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			OutResult = {};
			if (!ApplyCodegenModuleEnvironment(Effective, OutResult.Error)) return false;
			return EmitFromSource(Source, SourceLabel, Effective, OutResult);
		}
		OutResult = {};
		OutResult.bSuccess = EmitCSharpFromSourceInternal(
		    Source, SourceLabel, OutResult.Output, OutResult.Error, Options);
		return OutResult.bSuccess;
	}

	bool FCSharpCodegenBackend::EmitFromFile(
	    const std::string_view ScriptPath,
	    const FCodegenOptions& Options,
	    FCodegenResult& OutResult)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			OutResult = {};
			if (!ApplyCodegenModuleEnvironment(Effective, OutResult.Error)) return false;
			return EmitFromFile(ScriptPath, Effective, OutResult);
		}
		OutResult = {};
		const auto Program = LoadCSharpProgram({}, ScriptPath, OutResult.Error, Options, true);
		OutResult.bSuccess = Program && EmitCSharpFromProgramInternal(*Program, OutResult.Output, OutResult.Error, Options.Bindings);
		return OutResult.bSuccess;
	}
}
