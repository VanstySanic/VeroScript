// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptPackageInterface.h"

#include "VeroScriptAst.h"
#include "VeroScriptBytecode.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptFloatFormatting.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace VeroScript
{
	namespace
	{
		bool IsVisibleExport(
		    const std::string& ModulePath,
		    const bool bExported,
		    const std::unordered_set<std::string>& SurfaceModules)
		{
			return bExported && SurfaceModules.find(ModulePath) != SurfaceModules.end();
		}

		const char* AccessName(const EMemberAccess Access)
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

		std::string SourceQualifiedName(std::string Name)
		{
			std::replace(Name.begin(), Name.end(), '.', ':');
			for (std::size_t Index = 0; Index + 1 < Name.size(); ++Index)
			{
				if (Name[Index] == ':' && Name[Index + 1] != ':')
				{
					Name.insert(Index, 1, ':');
					++Index;
				}
			}
			return Name;
		}

		std::string BeginQualifiedDeclaration(
		    std::string& Out,
		    const std::string& QualifiedName)
		{
			const std::size_t Separator = QualifiedName.rfind('.');
			if (Separator == std::string::npos)
			{
				return QualifiedName;
			}
			Out += "namespace " + SourceQualifiedName(QualifiedName.substr(0, Separator)) + "\n{\n";
			return QualifiedName.substr(Separator + 1);
		}

		void EndQualifiedDeclaration(
		    std::string& Out,
		    const std::string& QualifiedName)
		{
			if (QualifiedName.find('.') != std::string::npos)
			{
				Out += "}\n";
			}
		}

		std::string NormalizeQualifiedName(std::string Name)
		{
			for (std::size_t Index = 0; Index + 1 < Name.size(); ++Index)
			{
				if (Name[Index] == ':' && Name[Index + 1] == ':')
				{
					Name.replace(Index, 2, ".");
				}
			}
			return Name;
		}

		std::vector<std::string> ExtractNamedTypeReferences(
		    const std::string_view TypeName)
		{
			const auto IsStart = [](const unsigned char Character)
			{
				return std::isalpha(Character) || Character == '_';
			};
			const auto IsContinue = [&](const unsigned char Character)
			{
				return IsStart(Character) || std::isdigit(Character);
			};

			std::vector<std::string> References;
			for (std::size_t Offset = 0; Offset < TypeName.size();)
			{
				if (!IsStart(static_cast<unsigned char>(TypeName[Offset])))
				{
					++Offset;
					continue;
				}

				std::string Reference;
				do
				{
					const std::size_t SegmentBegin = Offset++;
					while (Offset < TypeName.size() && IsContinue(static_cast<unsigned char>(TypeName[Offset])))
					{
						++Offset;
					}
					if (!Reference.empty())
					{
						Reference.push_back('.');
					}
					Reference.append(
					    TypeName.substr(SegmentBegin, Offset - SegmentBegin));

					std::size_t SeparatorBytes = 0;
					if (Offset < TypeName.size() && TypeName[Offset] == '.')
					{
						SeparatorBytes = 1;
					}
					else if (Offset + 1 < TypeName.size() && TypeName[Offset] == ':' && TypeName[Offset + 1] == ':')
					{
						SeparatorBytes = 2;
					}
					if (SeparatorBytes == 0 || Offset + SeparatorBytes >= TypeName.size() || !IsStart(static_cast<unsigned char>(TypeName[Offset + SeparatorBytes])))
					{
						break;
					}
					Offset += SeparatorBytes;
				}
				while (true);
				References.push_back(std::move(Reference));
			}
			return References;
		}

		void AppendEscapedString(std::string& Out, const std::string_view Value)
		{
			Out.push_back('"');
			for (const unsigned char Character : Value)
			{
				switch (Character)
				{
				case '\\':
					{
						Out += "\\\\";
						break;
					}
				case '"':
					{
						Out += "\\\"";
						break;
					}
				case '\n':
					{
						Out += "\\n";
						break;
					}
				case '\r':
					{
						Out += "\\r";
						break;
					}
				case '\t':
					{
						Out += "\\t";
						break;
					}
				default:
					{
						Out.push_back(static_cast<char>(Character));
						break;
					}
				}
			}
			Out.push_back('"');
		}

		bool AppendLiteral(
		    std::string& Out,
		    const FExpr& Expression,
		    std::string& OutError)
		{
			switch (Expression.Kind)
			{
			case EExprKind::IntegerLiteral:
				{
					const auto& Literal =
					    static_cast<const FIntegerLiteralExpr&>(Expression);
					if (Literal.bIs64 && Literal.bUnsigned)
					{
						Out += std::to_string(Literal.UnsignedValue64) + "u64";
					}
					else if (Literal.bIs64)
					{
						Out += std::to_string(Literal.Value64) + "i64";
					}
					else if (Literal.bUnsigned)
					{
						Out += std::to_string(Literal.UnsignedValue) + "u";
					}
					else
					{
						Out += std::to_string(Literal.Value);
					}
					return true;
				}
			case EExprKind::CharacterLiteral:
				{
					Out += std::to_string(static_cast<int>(
					    static_cast<const FCharacterLiteralExpr&>(Expression).Value));
					return true;
				}
			case EExprKind::FloatLiteral:
				{
					const double Value =
					    static_cast<const FFloatLiteralExpr&>(Expression).Value;
					if (!std::isfinite(Value))
					{
						OutError =
						    "package interface cannot encode a non-finite default value";
						return false;
					}
					// Package interfaces are part of CanonicalSignedBytes. Floating
					// formatting must therefore be independent of the process locale
					// (iostreams may emit a comma decimal separator) and preserve the
					// exact double value on every supported standard library.
					char Buffer[128];
					const auto Result = FloatFormattingDetail::ToCharsGeneral(
					    Buffer,
					    Buffer + sizeof(Buffer),
					    Value,
					    std::numeric_limits<double>::max_digits10);
					if (Result.ec != std::errc{})
					{
						OutError = "package interface cannot encode a floating default value";
						return false;
					}
					const std::string_view Encoded(
					    Buffer, static_cast<std::size_t>(Result.ptr - Buffer));
					Out.append(Encoded.data(), Encoded.size());
					if (Encoded.find_first_of(".eE") == std::string_view::npos)
					{
						Out += ".0";
					}
					return true;
				}
			case EExprKind::BooleanLiteral:
				{
					Out += static_cast<const FBooleanLiteralExpr&>(Expression).Value
					    ? "true"
					    : "false";
					return true;
				}
			case EExprKind::StringLiteral:
				{
					AppendEscapedString(
					    Out, static_cast<const FStringLiteralExpr&>(Expression).Value);
					return true;
				}
			case EExprKind::WStringLiteral:
				{
					Out.push_back('L');
					AppendEscapedString(
					    Out,
					    Runtime::Utf16ToUtf8(
					        static_cast<const FWStringLiteralExpr&>(Expression).Value));
					return true;
				}
			case EExprKind::NullptrLiteral:
				{
					Out += "nullptr";
					return true;
				}
			default:
				{
					OutError =
					    "package interface parameter defaults must be literal values";
					return false;
				}
			}
		}

		bool AppendParameters(
		    std::string& Out,
		    const std::vector<FFunctionParam>& Parameters,
		    std::string& OutError)
		{
			Out.push_back('(');
			for (std::size_t Index = 0; Index < Parameters.size(); ++Index)
			{
				if (Index != 0)
				{
					Out += ", ";
				}
				const FFunctionParam& Param = Parameters[Index];
				if (Param.bIsConst)
				{
					Out += "const ";
				}
				if (Param.bIsRef)
				{
					Out += "ref ";
				}
				Out += Param.TypeName;
				if (Param.bIsVariadic)
				{
					Out += "...";
				}
				Out.push_back(' ');
				Out += Param.Name.empty()
				    ? "$package_arg" + std::to_string(Index)
				    : Param.Name;
				if (Param.DefaultValue)
				{
					Out += " = ";
					if (!AppendLiteral(Out, *Param.DefaultValue, OutError))
					{
						return false;
					}
				}
			}
			Out.push_back(')');
			return true;
		}

		bool AppendMethod(
		    std::string& Out,
		    const FMethodDecl& Method,
		    std::string& OutError)
		{
			Out += "\t";
			Out += AccessName(Method.Access);
			Out += ":\n\t";
			if (Method.bIsStatic)
			{
				Out += "static ";
			}
			if (Method.bConstexpr)
			{
				Out += "constexpr ";
			}
			if (Method.bVirtual)
			{
				Out += "virtual ";
			}
			Out += Method.ReturnType + " " + Method.Name;
			if (!AppendParameters(Out, Method.Parameters, OutError))
			{
				return false;
			}
			if (Method.bIsConst)
			{
				Out += " const";
			}
			if (Method.bOverride)
			{
				Out += " override";
			}
			if (Method.bFinal)
			{
				Out += " final";
			}
			if (Method.bDeleted)
			{
				Out += " = delete;\n";
			}
			else if (Method.bDefaulted)
			{
				Out += " = default;\n";
			}
			else if (Method.bConstexpr)
			{
				Out += ";\n";
			}
			else if (Method.bPureVirtual)
			{
				Out += " = 0;\n";
			}
			else
			{
				Out += " {}\n";
			}
			return true;
		}

		void AppendField(
		    std::string& Out,
		    const FFieldDecl& Field,
		    const bool bHideImplementationType)
		{
			Out += "\t";
			Out += AccessName(Field.Access);
			Out += ":\n\t";
			if (Field.bIsStatic)
			{
				Out += "static ";
			}
			if (Field.bIsMutable)
			{
				Out += "mutable ";
			}
			if (Field.bConstexpr)
			{
				Out += "constexpr ";
			}
			else if (Field.bIsConst)
			{
				Out += "const ";
			}
			// `int` is a parser-only opaque placeholder. Prelinked bytecode keeps
			// the real field type and layout; access validation rejects the member
			// before an importing module can observe this placeholder type.
			Out += (bHideImplementationType ? "int" : Field.TypeName);
			Out += " " + Field.Name + ";\n";
		}

		bool AppendTypeBody(
		    std::string& Out,
		    const std::string& SimpleName,
		    const std::vector<FFieldDecl>& Fields,
		    const std::vector<FMethodDecl>& Methods,
		    const std::vector<FConstructorDecl>* Constructors,
		    const FDestructorDecl* Destructor,
		    const std::vector<FUsingMemberDecl>* UsingMembers,
		    std::string& OutError)
		{
			Out += "\n{\n";
			// Closed package interfaces deliberately omit friend declarations. Their
			// private field types are opaque placeholders and private methods are not
			// part of the consumable ABI, so granting new consumer code friend access
			// would expose an invalid implementation view. Open generic declarations
			// retain their original source in PackageTemplateModules instead.
			if (UsingMembers)
			{
				for (const FUsingMemberDecl& Using : *UsingMembers)
				{
					if (Using.Access == EMemberAccess::Private)
					{
						continue;
					}
					Out += "\t";
					Out += AccessName(Using.Access);
					Out += ":\n\tusing ";
					if (!Using.BaseTypeName.empty())
					{
						Out += SourceQualifiedName(Using.BaseTypeName);
						Out += "::";
					}
					Out += Using.MemberName + ";\n";
				}
			}
			for (const FFieldDecl& Field : Fields)
			{
				// Keep the private member name/access boundary so the consumer
				// compiler cannot fall back to bytecode layout metadata and treat
				// it as public. Its implementation type is intentionally opaque.
				AppendField(
				    Out,
				    Field,
				    Field.Access == EMemberAccess::Private);
			}
			if (Constructors)
			{
				for (const FConstructorDecl& Constructor : *Constructors)
				{
					Out += "\t" + std::string(AccessName(Constructor.Access)) + ":\n\t";
					if (Constructor.bExplicit)
					{
						Out += "explicit ";
					}
					Out += SimpleName;
					if (!AppendParameters(
					        Out, Constructor.Parameters, OutError))
					{
						return false;
					}
					if (Constructor.bDeleted)
					{
						Out += " = delete;\n";
					}
					else if (Constructor.bDefaulted)
					{
						Out += " = default;\n";
					}
					else
					{
						Out += " {}\n";
					}
				}
			}
			for (const FMethodDecl& Method : Methods)
			{
				if (Method.Access == EMemberAccess::Private)
				{
					continue;
				}
				if (!AppendMethod(Out, Method, OutError))
				{
					return false;
				}
			}
			if (Destructor)
			{
				Out += "\t";
				Out += AccessName(Destructor->Access);
				Out += ":\n\t";
				if (Destructor->bVirtual)
				{
					Out += "virtual ";
				}
				Out += "~" + SimpleName;
				if (Destructor->bPureVirtual)
				{
					Out += "() = 0;\n";
				}
				else
				{
					Out += Destructor->bDefaulted ? "() = default;\n" : "() {}\n";
				}
			}
			Out += "}\n";
			return true;
		}
	}

	bool BuildPackageInterfaceSource(
	    const FProgram& Program,
	    const std::unordered_set<std::string>& SurfaceModules,
	    std::string& OutSource,
	    std::string& OutError, const FBytecodeModule* Compiled)
	{
		OutSource.clear();
		OutError.clear();
		struct FDeclaredType
		{
			std::string Name;
			std::string ModulePath;
			bool bAvailable = false;
		};
		std::vector<FDeclaredType> DeclaredTypes;
		std::unordered_map<std::string, std::vector<std::size_t>> DeclaredTypeIndices;
		const auto CollectType =
		    [&](const std::string& Name,
		        const std::string& ModulePath,
		        const bool bExported)
		{
			DeclaredTypes.push_back(FDeclaredType{
			    NormalizeQualifiedName(Name),
			    ModulePath,
			    IsVisibleExport(ModulePath, bExported, SurfaceModules)});
			DeclaredTypeIndices[DeclaredTypes.back().Name].push_back(DeclaredTypes.size() - 1);
		};
		for (const FTypeAliasDecl& Alias : Program.TypeAliases)
		{
			CollectType(Alias.Name, Alias.ModulePath, Alias.bExported);
		}
		for (const FEnumDecl& Enum : Program.Enums)
		{
			CollectType(Enum.Name, Enum.ModulePath, Enum.bExported);
		}
		for (const FInterfaceDecl& Interface : Program.Interfaces)
		{
			CollectType(
			    Interface.Name, Interface.ModulePath, Interface.bExported);
		}
		for (const FStructDecl& Struct : Program.Structs)
		{
			CollectType(Struct.Name, Struct.ModulePath, Struct.bExported);
		}
		for (const FClassDecl& Class : Program.Classes)
		{
			CollectType(Class.Name, Class.ModulePath, Class.bExported);
		}

		enum class ETypeAvailability
		{
			NotDeclared,
			Available,
			Unavailable,
		};
		const auto ResolveCandidate =
		    [&](const std::string& Candidate,
		        const std::string& OwnerModulePath) -> ETypeAvailability
		{
			const auto Found = DeclaredTypeIndices.find(Candidate);
			if (Found == DeclaredTypeIndices.end()) return ETypeAvailability::NotDeclared;
			for (const auto Position : Found->second)
			{
				const auto& Declared = DeclaredTypes[Position];
				if (Declared.Name == Candidate && Declared.ModulePath == OwnerModulePath)
				{
					return Declared.bAvailable
					    ? ETypeAvailability::Available
					    : ETypeAvailability::Unavailable;
				}
			}
			for (const auto Position : Found->second)
			{
				const auto& Declared = DeclaredTypes[Position];
				if (Declared.Name == Candidate && Declared.bAvailable)
				{
					return ETypeAvailability::Available;
				}
			}
			// A same-name declaration exists, but neither local ownership nor
			// an available export resolved it. Preserve the third-stage result.
			return ETypeAvailability::Unavailable;
		};

		const auto RequireAvailableType =
		    [&](const std::string& TypeName,
		        const std::string& OwnerName,
		        const std::string& OwnerModulePath,
		        const std::string& Context,
		        const std::vector<std::string>* const TypeParams = nullptr) -> bool
		{
			const std::string NormalizedOwner =
			    NormalizeQualifiedName(OwnerName);
			std::string OwnerNamespace;
			const std::size_t OwnerSeparator = NormalizedOwner.rfind('.');
			if (OwnerSeparator != std::string::npos)
			{
				OwnerNamespace = NormalizedOwner.substr(0, OwnerSeparator);
			}
			for (const std::string& Reference :
			    ExtractNamedTypeReferences(TypeName))
			{
				if (TypeParams && std::find(TypeParams->begin(), TypeParams->end(), Reference) != TypeParams->end())
				{
					continue;
				}
				std::vector<std::string> Candidates;
				if (Reference.find('.') != std::string::npos)
				{
					Candidates.push_back(Reference);
				}
				else
				{
					std::string Namespace = OwnerNamespace;
					while (!Namespace.empty())
					{
						Candidates.push_back(Namespace + "." + Reference);
						const std::size_t Separator = Namespace.rfind('.');
						Namespace = Separator == std::string::npos
						    ? std::string{}
						    : Namespace.substr(0, Separator);
					}
					Candidates.push_back(Reference);
				}

				for (const std::string& Candidate : Candidates)
				{
					const ETypeAvailability Availability =
					    ResolveCandidate(Candidate, OwnerModulePath);
					if (Availability == ETypeAvailability::Available)
					{
						break;
					}
					if (Availability == ETypeAvailability::Unavailable)
					{
						OutError = Context + " references type that is not exported by this package: " + Candidate;
						return false;
					}
				}
			}
			return true;
		};
		const auto RequireAvailableParameters =
		    [&](const std::vector<FFunctionParam>& Parameters,
		        const std::string& OwnerName,
		        const std::string& OwnerModulePath,
		        const std::string& Context,
		        const std::vector<std::string>* const TypeParams = nullptr) -> bool
		{
			for (const FFunctionParam& Parameter : Parameters)
			{
				if (!RequireAvailableType(
				        Parameter.TypeName,
				        OwnerName,
				        OwnerModulePath,
				        Context,
				        TypeParams))
				{
					return false;
				}
			}
			return true;
		};
		const auto RequireAvailableMembers =
		    [&](const std::vector<FFieldDecl>& Fields,
		        const std::vector<FMethodDecl>& Methods,
		        const std::vector<FConstructorDecl>* Constructors,
		        const std::string& OwnerName,
		        const std::string& OwnerModulePath,
		        const std::string& Context,
		        const std::vector<std::string>* const TypeParams = nullptr) -> bool
		{
			for (const FFieldDecl& Field : Fields)
			{
				if (Field.Access != EMemberAccess::Private && !RequireAvailableType(Field.TypeName, OwnerName, OwnerModulePath, Context, TypeParams))
				{
					return false;
				}
			}
			for (const FMethodDecl& Method : Methods)
			{
				if (Method.Access != EMemberAccess::Private && (!RequireAvailableType(Method.ReturnType, OwnerName, OwnerModulePath, Context, TypeParams) || !RequireAvailableParameters(Method.Parameters, OwnerName, OwnerModulePath, Context, TypeParams)))
				{
					return false;
				}
			}
			if (Constructors)
			{
				for (const FConstructorDecl& Constructor : *Constructors)
				{
					if (!RequireAvailableParameters(
					        Constructor.Parameters,
					        OwnerName,
					        OwnerModulePath,
					        Context,
					        TypeParams))
					{
						return false;
					}
				}
			}
			return true;
		};

		for (const FTypeAliasDecl& Alias : Program.TypeAliases)
		{
			if (IsVisibleExport(
			        Alias.ModulePath, Alias.bExported, SurfaceModules))
			{
				if (!RequireAvailableType(
				        Alias.TypeName,
				        Alias.Name,
				        Alias.ModulePath,
				        "exported alias '" + Alias.Name + "'"))
				{
					return false;
				}
				const std::string SimpleName =
				    BeginQualifiedDeclaration(OutSource, Alias.Name);
				OutSource += "export using " + SimpleName + " = " + Alias.TypeName + ";\n";
				EndQualifiedDeclaration(OutSource, Alias.Name);
			}
		}
		for (const FEnumDecl& Enum : Program.Enums)
		{
			if (!IsVisibleExport(
			        Enum.ModulePath, Enum.bExported, SurfaceModules))
			{
				continue;
			}
			const std::string SimpleName =
			    BeginQualifiedDeclaration(OutSource, Enum.Name);
			OutSource += "export enum ";
			if (Enum.bScoped)
			{
				OutSource += "class ";
			}
			OutSource += SimpleName;
			if (!Enum.UnderlyingTypeName.empty() && Enum.UnderlyingTypeName != "int")
			{
				OutSource += " : " + Enum.UnderlyingTypeName;
			}
			OutSource += "\n{\n";
			for (std::size_t Index = 0; Index < Enum.Members.size(); ++Index)
			{
				const FEnumMemberDecl& Member = Enum.Members[Index];
				std::int64_t Number = Member.Value;
				if (Compiled)
				{
					for (const auto& Type : Compiled->Enums)
					{
						if (Type.Name == Enum.Name || Type.Name == Enum.ModulePath + '\x1F' + Enum.Name)
						{
							for (const auto& Value : Type.Members)
							{
								if (Value.Name == Member.Name)
								{
									Number = Value.Value;
								}
							}
						}
					}
				}
				OutSource += "\t" + Member.Name + " = " + std::to_string(Number);
				OutSource += Index + 1 == Enum.Members.size() ? "\n" : ",\n";
			}
			OutSource += "}\n";
			EndQualifiedDeclaration(OutSource, Enum.Name);
		}
		for (const FInterfaceDecl& Interface : Program.Interfaces)
		{
			if (!IsVisibleExport(
			        Interface.ModulePath,
			        Interface.bExported,
			        SurfaceModules))
			{
				continue;
			}
			for (const std::string& Base : Interface.BaseInterfaceNames)
			{
				if (!RequireAvailableType(
				        Base,
				        Interface.Name,
				        Interface.ModulePath,
				        "exported interface '" + Interface.Name + "'",
				        &TemplateParameterNames(Interface)))
				{
					return false;
				}
			}
			if (!RequireAvailableMembers(
			        Interface.Fields,
			        Interface.Methods,
			        nullptr,
			        Interface.Name,
			        Interface.ModulePath,
			        "exported interface '" + Interface.Name + "'",
			        &TemplateParameterNames(Interface)))
			{
				return false;
			}
			if (!TemplateParameterNames(Interface).empty())
			{
				// The complete declaration and implementation are carried by
				// PackageTemplateModules; the closed interface source omits it.
				continue;
			}
			const std::string SimpleName =
			    BeginQualifiedDeclaration(OutSource, Interface.Name);
			OutSource += "export interface " + SimpleName;
			if (Interface.bFinal)
			{
				OutSource += " final";
			}
			for (std::size_t Index = 0;
			    Index < Interface.BaseInterfaceNames.size();
			    ++Index)
			{
				OutSource += Index == 0 ? " : " : ", ";
				OutSource += Interface.BaseInterfaceNames[Index];
			}
			if (!AppendTypeBody(
			        OutSource,
			        SimpleName,
			        Interface.Fields,
			        Interface.Methods,
			        nullptr,
			        nullptr,
			        nullptr,
			        OutError))
			{
				return false;
			}
			EndQualifiedDeclaration(OutSource, Interface.Name);
		}
		for (const FStructDecl& Struct : Program.Structs)
		{
			if (!IsVisibleExport(
			        Struct.ModulePath, Struct.bExported, SurfaceModules))
			{
				continue;
			}
			if ((!Struct.BaseTypeName.empty() && !RequireAvailableType(Struct.BaseTypeName, Struct.Name, Struct.ModulePath, "exported struct '" + Struct.Name + "'", &TemplateParameterNames(Struct))) || !RequireAvailableMembers(Struct.Fields, Struct.Methods, &Struct.Constructors, Struct.Name, Struct.ModulePath, "exported struct '" + Struct.Name + "'", &TemplateParameterNames(Struct)))
			{
				return false;
			}
			for (const std::string& InterfaceName : Struct.InterfaceTypeNames)
			{
				if (!RequireAvailableType(
				        InterfaceName,
				        Struct.Name,
				        Struct.ModulePath,
				        "exported struct '" + Struct.Name + "'",
				        &TemplateParameterNames(Struct)))
				{
					return false;
				}
			}
			if (!TemplateParameterNames(Struct).empty())
			{
				continue;
			}
			const std::string SimpleName =
			    BeginQualifiedDeclaration(OutSource, Struct.Name);
			OutSource += Struct.bIsStatic
			    ? "export static struct " + SimpleName
			    : "export struct " + SimpleName;
			if (Struct.bFinal)
			{
				OutSource += " final";
			}
			bool bHasBase = false;
			if (!Struct.BaseTypeName.empty())
			{
				OutSource += " : ";
				OutSource += AccessName(Struct.BaseInheritanceAccess);
				OutSource += " " + Struct.BaseTypeName;
				bHasBase = true;
			}
			for (const std::string& InterfaceName : Struct.InterfaceTypeNames)
			{
				OutSource += bHasBase ? ", " : " : ";
				OutSource += InterfaceName;
				bHasBase = true;
			}
			if (!AppendTypeBody(
			        OutSource,
			        SimpleName,
			        Struct.Fields,
			        Struct.Methods,
			        &Struct.Constructors,
			        Struct.Destructor.get(),
			        &Struct.UsingMembers,
			        OutError))
			{
				return false;
			}
			EndQualifiedDeclaration(OutSource, Struct.Name);
		}
		for (const FClassDecl& Class : Program.Classes)
		{
			if (!IsVisibleExport(
			        Class.ModulePath, Class.bExported, SurfaceModules))
			{
				continue;
			}
			if (!RequireAvailableMembers(
			        Class.Fields,
			        Class.Methods,
			        &Class.Constructors,
			        Class.Name,
			        Class.ModulePath,
			        "exported class '" + Class.Name + "'",
			        &TemplateParameterNames(Class)))
			{
				return false;
			}
			for (const std::string& Base : Class.BaseTypeNames)
			{
				if (!RequireAvailableType(
				        Base,
				        Class.Name,
				        Class.ModulePath,
				        "exported class '" + Class.Name + "'",
				        &TemplateParameterNames(Class)))
				{
					return false;
				}
			}
			for (const std::string& InterfaceName : Class.InterfaceTypeNames)
			{
				if (!RequireAvailableType(
				        InterfaceName,
				        Class.Name,
				        Class.ModulePath,
				        "exported class '" + Class.Name + "'",
				        &TemplateParameterNames(Class)))
				{
					return false;
				}
			}
			if (!TemplateParameterNames(Class).empty())
			{
				continue;
			}
			const std::string SimpleName =
			    BeginQualifiedDeclaration(OutSource, Class.Name);
			OutSource += Class.bIsStatic
			    ? "export static class " + SimpleName
			    : "export class " + SimpleName;
			if (Class.bFinal)
			{
				OutSource += " final";
			}
			bool bHasBase = false;
			for (std::size_t Index = 0; Index < Class.BaseTypeNames.size(); ++Index)
			{
				OutSource += bHasBase ? ", " : " : ";
				const EMemberAccess Access =
				    Index < Class.BaseInheritanceAccess.size()
				    ? Class.BaseInheritanceAccess[Index]
				    : EMemberAccess::Private;
				OutSource += AccessName(Access);
				OutSource += " " + Class.BaseTypeNames[Index];
				bHasBase = true;
			}
			for (const std::string& InterfaceName : Class.InterfaceTypeNames)
			{
				OutSource += bHasBase ? ", " : " : ";
				OutSource += InterfaceName;
				bHasBase = true;
			}
			if (!AppendTypeBody(
			        OutSource,
			        SimpleName,
			        Class.Fields,
			        Class.Methods,
			        &Class.Constructors,
			        Class.Destructor.get(),
			        &Class.UsingMembers,
			        OutError))
			{
				return false;
			}
			EndQualifiedDeclaration(OutSource, Class.Name);
		}
		for (const std::unique_ptr<FStmt>& Statement : Program.TopLevelStatements)
		{
			if (!Statement || Statement->Kind != EStmtKind::VarDecl)
			{
				continue;
			}
			const auto& Global = static_cast<const FVarDeclStmt&>(*Statement);
			if (!IsVisibleExport(
			        Global.ModulePath, Global.bExported, SurfaceModules))
			{
				continue;
			}
			std::string GlobalType = Global.TypeName;
			if (GlobalType == "auto" && Global.bConstexpr && Compiled)
			{
				const auto Slot = Compiled->GlobalSlots.find(Global.Name);
				if (Slot != Compiled->GlobalSlots.end() && Slot->second < Compiled->GlobalTypeNames.size())
				{
					GlobalType = Compiled->GlobalTypeNames[Slot->second];
				}
			}
			if (GlobalType == "auto")
			{
				OutError =
				    "exported package global requires an explicit type: " + Global.Name;
				return false;
			}
			if (!RequireAvailableType(
			        GlobalType,
			        Global.Name,
			        Global.ModulePath,
			        "exported global '" + Global.Name + "'"))
			{
				return false;
			}
			const std::string SimpleName =
			    BeginQualifiedDeclaration(OutSource, Global.Name);
			OutSource += "export ";
			if (Global.bConstexpr)
			{
				OutSource += "constexpr ";
			}
			else if (Global.bIsConst)
			{
				OutSource += "const ";
			}
			// Top-level declarations syntactically require an initializer. The
			// imported declaration is marked precompiled and never emits this
			// placeholder; the dependency module owns the real initializer.
			OutSource += GlobalType + " " + SimpleName + " = 0;\n";
			EndQualifiedDeclaration(OutSource, Global.Name);
		}
		for (const FFunctionDecl& Function : Program.Functions)
		{
			if (!IsVisibleExport(
			        Function.ModulePath,
			        Function.bExported,
			        SurfaceModules))
			{
				continue;
			}
			if (!RequireAvailableType(
			        Function.ReturnType,
			        Function.Name,
			        Function.ModulePath,
			        "exported function '" + Function.Name + "'",
			        &TemplateParameterNames(Function)) ||
			    !RequireAvailableParameters(
			        Function.Parameters,
			        Function.Name,
			        Function.ModulePath,
			        "exported function '" + Function.Name + "'",
			        &TemplateParameterNames(Function)))
			{
				return false;
			}
			if (!TemplateParameterNames(Function).empty())
			{
				continue;
			}
			const std::string SimpleName =
			    BeginQualifiedDeclaration(OutSource, Function.Name);
			OutSource += std::string("export ") + (Function.bConstexpr ? "constexpr " : "") + Function.ReturnType + " " + SimpleName;
			if (!AppendParameters(
			        OutSource, Function.Parameters, OutError))
			{
				return false;
			}
			OutSource += Function.bDeleted ? " = delete;\n" : Function.bConstexpr ? ";\n"
			                                                                      : " {}\n";
			EndQualifiedDeclaration(OutSource, Function.Name);
		}
		return true;
	}
}
