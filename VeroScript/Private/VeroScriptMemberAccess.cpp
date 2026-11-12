// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptMemberAccess.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleVisibility.h"

#include <cctype>

namespace VeroScript
{
	namespace
	{
		// Strip `Type#args` instantiation suffix so access checks match open AST decls.
		std::string AccessLinkageKey(std::string Linkage)
		{
			const std::size_t HashPos = FindGenericTypeMarker(Linkage);
			if (HashPos != std::string::npos && HashPos > 0)
			{
				Linkage.resize(HashPos);
			}
			return Linkage;
		}

		// `#…` mangled concrete-args suffix (empty for non-generic linkage).
		std::string LinkageMangleSuffix(const std::string& Linkage)
		{
			const std::size_t HashPos = FindGenericTypeMarker(Linkage);
			if (HashPos == std::string::npos || HashPos == 0)
			{
				return {};
			}
			return Linkage.substr(HashPos);
		}

		// Same template family + same concrete instantiation (or both non-generic).
		bool SameAccessTypeIdentity(const std::string& A, const std::string& B)
		{
			return AccessLinkageKey(A) == AccessLinkageKey(B) && LinkageMangleSuffix(A) == LinkageMangleSuffix(B);
		}

		/**
		 * Arity encoded in `Type#len:payload…` mangling. Non-generic / unknown → 0.
		 * Length-prefixed payload walk matches VeroScriptCompiler::DecodeGenericConcreteTypes.
		 */
		std::size_t MangledLinkageArity(const std::string& Linkage)
		{
			const std::size_t Marker = FindGenericTypeMarker(Linkage);
			if (Marker == std::string::npos || Marker + 1 >= Linkage.size())
			{
				return 0;
			}
			std::size_t Count = 0;
			std::size_t Position = Marker + 1;
			while (Position < Linkage.size())
			{
				const std::size_t LengthStart = Position;
				while (Position < Linkage.size() && Linkage[Position] >= '0' && Linkage[Position] <= '9')
				{
					++Position;
				}
				if (Position == LengthStart || Position >= Linkage.size() || Linkage[Position] != ':')
				{
					return 0;
				}
				std::size_t Length = 0;
				for (std::size_t Digit = LengthStart; Digit < Position; ++Digit)
				{
					Length = Length * 10u + static_cast<std::size_t>(Linkage[Digit] - '0');
				}
				++Position;
				if (Length > Linkage.size() - Position)
				{
					return 0;
				}
				Position += Length;
				++Count;
			}
			return Count;
		}

		std::vector<std::string> DecodeMangledTypeArgs(const std::string& Linkage)
		{
			std::vector<std::string> Args;
			const std::size_t Marker = FindGenericTypeMarker(Linkage);
			if (Marker == std::string::npos)
			{
				return Args;
			}
			std::size_t Position = Marker + 1;
			while (Position < Linkage.size())
			{
				const std::size_t LengthStart = Position;
				std::size_t Length = 0;
				while (Position < Linkage.size() && std::isdigit(static_cast<unsigned char>(Linkage[Position])))
				{
					Length = Length * 10u + static_cast<std::size_t>(Linkage[Position++] - '0');
				}
				if (Position == LengthStart || Position >= Linkage.size() || Linkage[Position++] != ':' || Length > Linkage.size() - Position)
				{
					Args.clear();
					return Args;
				}
				Args.push_back(Linkage.substr(Position, Length));
				Position += Length;
			}
			return Args;
		}

		std::string LinkageSourceIdentity(std::string Linkage)
		{
			Linkage = AccessLinkageKey(std::move(Linkage));
			const std::size_t Start = FindLinkageIdentityStart(Linkage);
			return Linkage.substr(Start);
		}

		bool SplitWrittenTypeArgs(
		    const std::string& Written,
		    std::string& OutBase,
		    std::vector<std::string>& OutArgs)
		{
			OutBase = Written;
			OutArgs.clear();
			const std::size_t Lt = Written.find('<');
			if (Lt == std::string::npos)
			{
				return true;
			}
			if (Written.empty() || Written.back() != '>')
			{
				return false;
			}
			OutBase = Written.substr(0, Lt);
			std::size_t Begin = Lt + 1;
			int Depth = 0;
			int ParenthesisDepth = 0;
			for (std::size_t Index = Begin; Index + 1 < Written.size(); ++Index)
			{
				if (Written[Index] == '<')
				{
					++Depth;
				}
				else if (Written[Index] == '>' && (Index == 0 || Written[Index - 1] != '-'))
				{
					--Depth;
				}
				else if (Written[Index] == '(')
				{
					++ParenthesisDepth;
				}
				else if (Written[Index] == ')' && ParenthesisDepth > 0)
				{
					--ParenthesisDepth;
				}
				else if (Written[Index] == ',' && Depth == 0 && ParenthesisDepth == 0)
				{
					OutArgs.push_back(Written.substr(Begin, Index - Begin));
					Begin = Index + 1;
				}
			}
			if (Begin < Written.size() - 1)
			{
				OutArgs.push_back(Written.substr(Begin, Written.size() - 1 - Begin));
			}
			return !OutArgs.empty();
		}

		std::string ResolveAccessTypeAlias(
		    const FProgram& Program,
		    const ModuleVisibility::FImportIndex* ImportIndex,
		    const std::string& TypeName,
		    const std::string& ReferenceModulePath,
		    const int Depth = 0)
		{
			if (TypeName.empty() || Depth >= 16)
			{
				return TypeName;
			}
			std::string Base;
			std::vector<std::string> Args;
			if (!SplitWrittenTypeArgs(TypeName, Base, Args))
			{
				return TypeName;
			}

			const FTypeAliasDecl* Match = nullptr;
			for (const FTypeAliasDecl& Alias : Program.TypeAliases)
			{
				if (Alias.Name != Base)
				{
					continue;
				}
				if (Alias.ModulePath == ReferenceModulePath)
				{
					Match = &Alias;
					break;
				}
				if (ModuleVisibility::CanAccessSymbol(
				        Program,
				        Alias.ModulePath,
				        Alias.bExported,
				        ReferenceModulePath, ImportIndex) &&
				    !Match)
				{
					Match = &Alias;
				}
			}
			if (Match)
			{
				std::string Expanded = Match->TypeName;
				// Aliases are not templates today, but retaining written arguments here
				// keeps lookup deterministic for parser recovery and future alias templates.
				if (!Args.empty() && Expanded.find('<') == std::string::npos)
				{
					Expanded.push_back('<');
					for (std::size_t Index = 0; Index < Args.size(); ++Index)
					{
						if (Index > 0)
						{
							Expanded.push_back(',');
						}
						Expanded += Args[Index];
					}
					Expanded.push_back('>');
				}
				return ResolveAccessTypeAlias(
				    Program, ImportIndex, Expanded, Match->ModulePath, Depth + 1);
			}

			if (Args.empty())
			{
				return Base;
			}
			std::string Resolved = Base + '<';
			for (std::size_t Index = 0; Index < Args.size(); ++Index)
			{
				if (Index > 0)
				{
					Resolved.push_back(',');
				}
				Resolved += ResolveAccessTypeAlias(
				    Program, ImportIndex, Args[Index], ReferenceModulePath, Depth + 1);
			}
			Resolved.push_back('>');
			return Resolved;
		}

		std::string SubstituteTypeParams(
		    const std::string& Type,
		    const std::vector<std::string>& Params,
		    const std::vector<std::string>& Args)
		{
			std::string Result;
			Result.reserve(Type.size());
			for (std::size_t Position = 0; Position < Type.size();)
			{
				const unsigned char Ch = static_cast<unsigned char>(Type[Position]);
				if (!(std::isalpha(Ch) || Type[Position] == '_'))
				{
					Result.push_back(Type[Position++]);
					continue;
				}
				const std::size_t Begin = Position++;
				while (Position < Type.size())
				{
					const unsigned char Next = static_cast<unsigned char>(Type[Position]);
					if (!(std::isalnum(Next) || Type[Position] == '_'))
					{
						break;
					}
					++Position;
				}
				const std::string Token = Type.substr(Begin, Position - Begin);
				bool bReplaced = false;
				for (std::size_t Index = 0; Index < Params.size() && Index < Args.size(); ++Index)
				{
					if (Token == Params[Index])
					{
						Result += Args[Index];
						bReplaced = true;
						break;
					}
				}
				if (!bReplaced)
				{
					Result += Token;
				}
			}
			return Result;
		}

		// Arity of a type name as written (`Foo` → 0, `Foo<A,B>` → 2).
		std::size_t WrittenGenericArity(const std::string& Written)
		{
			const std::size_t Lt = Written.find('<');
			if (Lt == std::string::npos || Lt + 1 >= Written.size())
			{
				return 0;
			}
			std::size_t Depth = 0;
			std::size_t ParenthesisDepth = 0;
			std::size_t Count = 0;
			bool bArgStarted = false;
			for (std::size_t Index = Lt + 1; Index < Written.size(); ++Index)
			{
				const char Ch = Written[Index];
				if (Ch == '<')
				{
					++Depth;
					bArgStarted = true;
				}
				else if (Ch == '>' && (Index == 0 || Written[Index - 1] != '-'))
				{
					if (Depth == 0)
					{
						if (bArgStarted)
						{
							++Count;
						}
						return Count;
					}
					--Depth;
				}
				else if (Ch == '(')
				{
					++ParenthesisDepth;
					bArgStarted = true;
				}
				else if (Ch == ')' && ParenthesisDepth > 0)
				{
					--ParenthesisDepth;
				}
				else if (Ch == ',' && Depth == 0 && ParenthesisDepth == 0)
				{
					if (bArgStarted)
					{
						++Count;
					}
					bArgStarted = false;
				}
				else if (!std::isspace(static_cast<unsigned char>(Ch)))
				{
					bArgStarted = true;
				}
			}
			return 0;
		}

		std::string SymbolLinkage(
		    const std::string& ModulePath,
		    const bool bExported,
		    const std::string& SimpleName)
		{
			if (bExported || ModulePath.empty())
			{
				return SimpleName;
			}
			return ModulePath + '\x1F' + SimpleName;
		}

		// Strip `Foo<...>` → `Foo` for base-name lookup.
		std::string BaseNameWithoutGenerics(std::string Name)
		{
			const std::size_t Lt = Name.find('<');
			if (Lt != std::string::npos)
			{
				Name.resize(Lt);
			}
			return Name;
		}

		/**
		 * Resolve a base type name as written in an inheritance clause to its linkage name.
		 * Prefer a declaration in FromModulePath, then an exported type of the same name.
		 * Matches template-parameter arity so `Box` / `Box<T>` / `Box<T,U>` stay distinct.
		 */
		std::string ResolveBaseTypeLinkage(
		    const FProgram& Program,
		    const std::string& WrittenBaseName,
		    const std::string& FromModulePath)
		{
			const std::string Name = BaseNameWithoutGenerics(WrittenBaseName);
			if (Name.empty())
			{
				return {};
			}
			const std::size_t WantedArity = WrittenGenericArity(WrittenBaseName);

			std::string ExportedMatch;
			std::string AnyMatch;
			for (const FStructDecl& StructDecl : Program.Structs)
			{
				if (StructDecl.Name != Name || TemplateParameterNames(StructDecl).size() != WantedArity)
				{
					continue;
				}
				const std::string Link =
				    SymbolLinkage(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name);
				if (StructDecl.ModulePath == FromModulePath)
				{
					return Link;
				}
				if (StructDecl.bExported && ExportedMatch.empty())
				{
					ExportedMatch = Link;
				}
				if (AnyMatch.empty())
				{
					AnyMatch = Link;
				}
			}
			for (const FClassDecl& ClassDecl : Program.Classes)
			{
				if (ClassDecl.Name != Name || TemplateParameterNames(ClassDecl).size() != WantedArity)
				{
					continue;
				}
				const std::string Link =
				    SymbolLinkage(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name);
				if (ClassDecl.ModulePath == FromModulePath)
				{
					return Link;
				}
				if (ClassDecl.bExported && ExportedMatch.empty())
				{
					ExportedMatch = Link;
				}
				if (AnyMatch.empty())
				{
					AnyMatch = Link;
				}
			}
			if (!ExportedMatch.empty())
			{
				return ExportedMatch;
			}
			return AnyMatch;
		}

		bool IsDerivedFromImpl(
		    const FProgram& Program,
		    const std::string& DerivedLinkage,
		    const std::size_t DerivedArity,
		    const std::string& BaseLinkage,
		    const std::size_t BaseArity,
		    const int Depth)
		{
			if (Depth > 64 || DerivedLinkage.empty() || BaseLinkage.empty())
			{
				return false;
			}
			if (AccessLinkageKey(DerivedLinkage) == AccessLinkageKey(BaseLinkage) && DerivedArity == BaseArity)
			{
				if (SameAccessTypeIdentity(DerivedLinkage, BaseLinkage) || LinkageMangleSuffix(BaseLinkage).empty())
				{
					return true;
				}
			}

			const std::string DerivedKey = AccessLinkageKey(DerivedLinkage);

			for (const FStructDecl& StructDecl : Program.Structs)
			{
				const std::string StructLinkage =
				    SymbolLinkage(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name);
				if (AccessLinkageKey(StructLinkage) != DerivedKey || TemplateParameterNames(StructDecl).size() != DerivedArity || StructDecl.BaseTypeName.empty())
				{
					continue;
				}
				const std::string ParentLinkage = ResolveBaseTypeLinkage(
				    Program,
				    StructDecl.BaseTypeName,
				    StructDecl.ModulePath);
				if (ParentLinkage.empty())
				{
					continue;
				}
				const std::size_t ParentArity = WrittenGenericArity(StructDecl.BaseTypeName);
				if (IsDerivedFromImpl(
				        Program,
				        ParentLinkage,
				        ParentArity,
				        BaseLinkage,
				        BaseArity,
				        Depth + 1))
				{
					return true;
				}
			}

			for (const FClassDecl& ClassDecl : Program.Classes)
			{
				const std::string ClassLinkage =
				    SymbolLinkage(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name);
				if (AccessLinkageKey(ClassLinkage) != DerivedKey || TemplateParameterNames(ClassDecl).size() != DerivedArity)
				{
					continue;
				}
				for (const std::string& BaseName : ClassDecl.BaseTypeNames)
				{
					const std::string ParentLinkage =
					    ResolveBaseTypeLinkage(Program, BaseName, ClassDecl.ModulePath);
					if (ParentLinkage.empty())
					{
						continue;
					}
					const std::size_t ParentArity = WrittenGenericArity(BaseName);
					if (IsDerivedFromImpl(
					        Program,
					        ParentLinkage,
					        ParentArity,
					        BaseLinkage,
					        BaseArity,
					        Depth + 1))
					{
						return true;
					}
				}
				for (const std::string& InterfaceName : ClassDecl.InterfaceTypeNames)
				{
					const std::string ParentLinkage =
					    ResolveBaseTypeLinkage(Program, InterfaceName, ClassDecl.ModulePath);
					if (ParentLinkage.empty())
					{
						continue;
					}
					const std::size_t ParentArity = WrittenGenericArity(InterfaceName);
					if (IsDerivedFromImpl(
					        Program,
					        ParentLinkage,
					        ParentArity,
					        BaseLinkage,
					        BaseArity,
					        Depth + 1))
					{
						return true;
					}
				}
			}
			for (const FStructDecl& StructDecl : Program.Structs)
			{
				const std::string StructLinkage =
				    SymbolLinkage(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name);
				if (AccessLinkageKey(StructLinkage) != DerivedKey || TemplateParameterNames(StructDecl).size() != DerivedArity)
				{
					continue;
				}
				for (const std::string& InterfaceName : StructDecl.InterfaceTypeNames)
				{
					const std::string ParentLinkage =
					    ResolveBaseTypeLinkage(Program, InterfaceName, StructDecl.ModulePath);
					if (ParentLinkage.empty())
					{
						continue;
					}
					const std::size_t ParentArity = WrittenGenericArity(InterfaceName);
					if (IsDerivedFromImpl(
					        Program,
					        ParentLinkage,
					        ParentArity,
					        BaseLinkage,
					        BaseArity,
					        Depth + 1))
					{
						return true;
					}
				}
			}
			for (const FInterfaceDecl& InterfaceDecl : Program.Interfaces)
			{
				const std::string InterfaceLinkage =
				    SymbolLinkage(InterfaceDecl.ModulePath, InterfaceDecl.bExported, InterfaceDecl.Name);
				if (AccessLinkageKey(InterfaceLinkage) != DerivedKey || TemplateParameterNames(InterfaceDecl).size() != DerivedArity)
				{
					continue;
				}
				for (const std::string& ParentName : InterfaceDecl.BaseInterfaceNames)
				{
					const std::string ParentLinkage =
					    ResolveBaseTypeLinkage(Program, ParentName, InterfaceDecl.ModulePath);
					if (ParentLinkage.empty())
					{
						continue;
					}
					const std::size_t ParentArity = WrittenGenericArity(ParentName);
					if (IsDerivedFromImpl(
					        Program,
					        ParentLinkage,
					        ParentArity,
					        BaseLinkage,
					        BaseArity,
					        Depth + 1))
					{
						return true;
					}
				}
			}
			return false;
		}

		bool IsAccessibleBaseImpl(
		    const FProgram& Program,
		    const std::string& DerivedLinkage,
		    const std::size_t DerivedArity,
		    const std::string& BaseLinkage,
		    const std::size_t BaseArity,
		    const int Depth)
		{
			if (Depth > 64 || DerivedLinkage.empty() || BaseLinkage.empty())
			{
				return false;
			}
			if (AccessLinkageKey(DerivedLinkage) == AccessLinkageKey(BaseLinkage) && DerivedArity == BaseArity)
			{
				return true;
			}

			const std::string DerivedKey = AccessLinkageKey(DerivedLinkage);
			for (const FStructDecl& StructDecl : Program.Structs)
			{
				const std::string StructLinkage =
				    SymbolLinkage(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name);
				if (AccessLinkageKey(StructLinkage) != DerivedKey || TemplateParameterNames(StructDecl).size() != DerivedArity || StructDecl.BaseTypeName.empty())
				{
					continue;
				}
				const std::string ParentLinkage = ResolveBaseTypeLinkage(
				    Program,
				    StructDecl.BaseTypeName,
				    StructDecl.ModulePath);
				if (ParentLinkage.empty())
				{
					continue;
				}
				const std::size_t ParentArity = WrittenGenericArity(StructDecl.BaseTypeName);
				if (AccessLinkageKey(ParentLinkage) == AccessLinkageKey(BaseLinkage) && ParentArity == BaseArity)
				{
					// Direct base of the original type is always accessible for `using`.
					// Private bases of an intermediate type are not accessible further out.
					if (Depth == 0 || StructDecl.BaseInheritanceAccess != EMemberAccess::Private)
					{
						return true;
					}
					continue;
				}
				if (StructDecl.BaseInheritanceAccess == EMemberAccess::Private)
				{
					continue;
				}
				if (IsAccessibleBaseImpl(
				        Program,
				        ParentLinkage,
				        ParentArity,
				        BaseLinkage,
				        BaseArity,
				        Depth + 1))
				{
					return true;
				}
			}
			for (const FClassDecl& ClassDecl : Program.Classes)
			{
				const std::string ClassLinkage =
				    SymbolLinkage(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name);
				if (AccessLinkageKey(ClassLinkage) != DerivedKey || TemplateParameterNames(ClassDecl).size() != DerivedArity)
				{
					continue;
				}
				for (std::size_t BaseIndex = 0; BaseIndex < ClassDecl.BaseTypeNames.size();
				    ++BaseIndex)
				{
					const std::string& BaseName = ClassDecl.BaseTypeNames[BaseIndex];
					const std::string ParentLinkage =
					    ResolveBaseTypeLinkage(Program, BaseName, ClassDecl.ModulePath);
					if (ParentLinkage.empty())
					{
						continue;
					}
					const std::size_t ParentArity = WrittenGenericArity(BaseName);
					const EMemberAccess InheritAccess =
					    BaseIndex < ClassDecl.BaseInheritanceAccess.size()
					    ? ClassDecl.BaseInheritanceAccess[BaseIndex]
					    : EMemberAccess::Private;
					if (AccessLinkageKey(ParentLinkage) == AccessLinkageKey(BaseLinkage) && ParentArity == BaseArity)
					{
						if (Depth == 0 || InheritAccess != EMemberAccess::Private)
						{
							return true;
						}
						continue;
					}
					if (InheritAccess == EMemberAccess::Private)
					{
						continue;
					}
					if (IsAccessibleBaseImpl(
					        Program,
					        ParentLinkage,
					        ParentArity,
					        BaseLinkage,
					        BaseArity,
					        Depth + 1))
					{
						return true;
					}
				}
			}
			return false;
		}

		int AccessRank(const EMemberAccess Access)
		{
			switch (Access)
			{
			case EMemberAccess::Public:
				return 0;
			case EMemberAccess::Protected:
				return 1;
			case EMemberAccess::Private:
				return 2;
			default:
				return 2;
			}
		}
	}

	EMemberAccess ApplyInheritanceAccess(
	    const EMemberAccess MemberAccess,
	    const EMemberAccess InheritanceAccess)
	{
		if (InheritanceAccess == EMemberAccess::Public)
		{
			return MemberAccess;
		}
		if (InheritanceAccess == EMemberAccess::Protected)
		{
			if (MemberAccess == EMemberAccess::Public)
			{
				return EMemberAccess::Protected;
			}
			return MemberAccess;
		}
		if (MemberAccess == EMemberAccess::Public || MemberAccess == EMemberAccess::Protected)
		{
			return EMemberAccess::Private;
		}
		return MemberAccess;
	}

	bool IsAccessMoreRestricted(const EMemberAccess NewAccess, const EMemberAccess OldAccess)
	{
		return AccessRank(NewAccess) > AccessRank(OldAccess);
	}

	bool IsFriendTypeContext(
	    const FProgram& Program,
	    const std::string& DeclaringOwnerLinkage,
	    const std::size_t DeclaringArity,
	    const std::string& ContextOwnerLinkage,
	    const std::string& ReceiverTypeLinkage,
	    const ModuleVisibility::FImportIndex* ImportIndex)
	{
		if (DeclaringOwnerLinkage.empty() || ContextOwnerLinkage.empty())
		{
			return false;
		}
		const std::string DeclaringBase = AccessLinkageKey(DeclaringOwnerLinkage);
		const std::vector<FFriendDecl>* Friends = nullptr;
		const std::vector<std::string>* DeclaringTypeParams = nullptr;
		std::string DeclaringModule;
		const auto TryDecl = [&](const auto& Decl) -> bool
		{
			const std::string Linkage = SymbolLinkage(Decl.ModulePath, Decl.bExported, Decl.Name);
			if (AccessLinkageKey(Linkage) != DeclaringBase || TemplateParameterNames(Decl).size() != DeclaringArity)
			{
				return false;
			}
			Friends = &Decl.Friends;
			DeclaringTypeParams = &TemplateParameterNames(Decl);
			DeclaringModule = Decl.ModulePath;
			return true;
		};
		for (const FStructDecl& Decl : Program.Structs)
		{
			if (TryDecl(Decl))
			{
				break;
			}
		}
		if (!Friends)
		{
			for (const FClassDecl& Decl : Program.Classes)
			{
				if (TryDecl(Decl))
				{
					break;
				}
			}
		}
		if (!Friends)
		{
			for (const FInterfaceDecl& Decl : Program.Interfaces)
			{
				if (TryDecl(Decl))
				{
					break;
				}
			}
		}
		if (!Friends)
		{
			return false;
		}

		const std::string& ConcreteDeclaringOwner =
		    AccessLinkageKey(ReceiverTypeLinkage) == DeclaringBase
		    ? ReceiverTypeLinkage
		    : DeclaringOwnerLinkage;
		const std::vector<std::string> DeclaringArgs =
		    DecodeMangledTypeArgs(ConcreteDeclaringOwner);
		const std::vector<std::string> ContextArgs = DecodeMangledTypeArgs(ContextOwnerLinkage);
		for (const FFriendDecl& Friend : *Friends)
		{
			if (Friend.Kind != FFriendDecl::EKind::Type)
			{
				continue;
			}
			std::string FriendBase;
			std::vector<std::string> FriendArgs;
			const std::string ResolvedFriendType = ResolveAccessTypeAlias(
			    Program, ImportIndex, Friend.TypeName, DeclaringModule);
			if (!SplitWrittenTypeArgs(ResolvedFriendType, FriendBase, FriendArgs))
			{
				continue;
			}
			std::string TargetLinkage;
			const auto ConsiderTarget = [&](const auto& Target)
			{
				if (Target.Name != FriendBase || TemplateParameterNames(Target).size() != FriendArgs.size())
				{
					return;
				}
				if (Target.ModulePath == DeclaringModule)
				{
					TargetLinkage = SymbolLinkage(
					    Target.ModulePath, Target.bExported, Target.Name);
				}
				else if (ModuleVisibility::CanAccessSymbol(
				             Program,
				             Target.ModulePath,
				             Target.bExported,
			             DeclaringModule, ImportIndex) &&
				    TargetLinkage.empty())
				{
					TargetLinkage = SymbolLinkage(
					    Target.ModulePath, Target.bExported, Target.Name);
				}
			};
			for (const FStructDecl& Target : Program.Structs)
			{
				ConsiderTarget(Target);
			}
			for (const FClassDecl& Target : Program.Classes)
			{
				ConsiderTarget(Target);
			}
			for (const FInterfaceDecl& Target : Program.Interfaces)
			{
				if (Target.Name != FriendBase || TemplateParameterNames(Target).size() != FriendArgs.size())
				{
					continue;
				}
			if (Target.ModulePath == DeclaringModule ||
			    (TargetLinkage.empty() && ModuleVisibility::CanAccessSymbol(
			        Program, Target.ModulePath, Target.bExported, DeclaringModule, ImportIndex)))
				{
					TargetLinkage = SymbolLinkage(
					    Target.ModulePath, Target.bExported, Target.Name);
				}
			}
			if (TargetLinkage.empty() || AccessLinkageKey(TargetLinkage) != AccessLinkageKey(ContextOwnerLinkage) || FriendArgs.size() != ContextArgs.size())
			{
				continue;
			}
			bool bArgsMatch = true;
			for (std::size_t Index = 0; Index < FriendArgs.size(); ++Index)
			{
				const std::string Expected = ResolveAccessTypeAlias(
				    Program,
				    ImportIndex,
				    SubstituteTypeParams(
				        FriendArgs[Index], *DeclaringTypeParams, DeclaringArgs),
				    DeclaringModule);
				if (LinkageSourceIdentity(Expected) != LinkageSourceIdentity(ContextArgs[Index]))
				{
					bArgsMatch = false;
					break;
				}
			}
			if (bArgsMatch)
			{
				return true;
			}
		}
		return false;
	}

	bool CanAccessMemberFromContext(
	    const FMemberAccessInfo& Info,
	    const std::string& ContextOwnerLinkage,
	    const FProgram& Program,
	    const std::string& ReceiverTypeLinkage, const bool bTypeName,
	    const ModuleVisibility::FImportIndex* ImportIndex)
	{
		if (!Info.bFound)
		{
			return false;
		}
		if (Info.EffectiveAccess == EMemberAccess::Public || (!ContextOwnerLinkage.empty() && AccessLinkageKey(Info.DeclaringOwnerLinkage) == AccessLinkageKey(ContextOwnerLinkage) && Info.DeclaringArity == MangledLinkageArity(ContextOwnerLinkage)))
		{
			return true;
		}
		std::string LexicalOwner;
		const auto FindLexicalOwner = [&](const auto& Declarations)
		{
			for (const auto& Declaration : Declarations)
			{
				if (SymbolLinkage(Declaration.ModulePath, Declaration.bExported, Declaration.Name) != AccessLinkageKey(ContextOwnerLinkage) || TemplateParameterNames(Declaration).size() != MangledLinkageArity(ContextOwnerLinkage))
				{
					continue;
				}
				const auto& Parent = Declaration.Scope.AccessTypeName;
				if (Parent.empty() || Parent == Declaration.Name) return;
				const auto BindParent = [&](const auto& Parents)
				{
					for (const auto& Candidate : Parents)
					{
						if (Candidate.Name != Parent || Candidate.ModulePath != Declaration.Scope.AccessModulePath || (Declaration.DeclarationContext && Declaration.DeclarationContext->TypeScope && Candidate.Scope.Id != Declaration.DeclarationContext->TypeScope->Id))
						{
							continue;
						}
						LexicalOwner = SymbolLinkage(Candidate.ModulePath, Candidate.bExported, Candidate.Name);
						const auto Arguments = DecodeMangledTypeArgs(ContextOwnerLinkage);
						for (std::size_t Index = 0; Index < TemplateParameterNames(Candidate).size(); ++Index)
						{
							const auto& Argument = Index < Arguments.size() ? Arguments[Index] : TemplateParameterNames(Candidate)[Index];
							LexicalOwner += (Index == 0 ? "#" : "") + std::to_string(Argument.size()) + ":" + Argument;
						}
					}
				};
				BindParent(Program.Classes);
				BindParent(Program.Structs);
				BindParent(Program.Interfaces);
			}
		};
		FindLexicalOwner(Program.Classes);
		FindLexicalOwner(Program.Structs);
		FindLexicalOwner(Program.Interfaces);
		if (!LexicalOwner.empty() && LexicalOwner != ContextOwnerLinkage)
		{
			thread_local std::size_t LexicalDepth = 0;
			if (LexicalDepth < 64)
			{
				struct FDepthGuard
				{
					std::size_t& Depth;
					~FDepthGuard()
					{
						--Depth;
					}
				} Guard{LexicalDepth};
				++LexicalDepth;
				if (CanAccessMemberFromContext(
				        Info, LexicalOwner, Program, ReceiverTypeLinkage, bTypeName, ImportIndex))
				{
					return true;
				}
			}
		}
		if (Info.EffectiveAccess != EMemberAccess::Public &&
		    IsFriendTypeContext(Program, Info.DeclaringOwnerLinkage, Info.DeclaringArity,
		        ContextOwnerLinkage, ReceiverTypeLinkage, ImportIndex))
		{
			return true;
		}
		// A friend of a derived type has the same protected access as that
		// derived type. The object expression must still be of the befriending
		// type (or, through the normal protected path, one of its derivatives).
		// Private base state is not granted here because its effective access is
		// private and friendship is neither inherited nor transitive.
		if (Info.EffectiveAccess == EMemberAccess::Protected &&
		    AccessLinkageKey(ReceiverTypeLinkage) != AccessLinkageKey(Info.DeclaringOwnerLinkage) &&
		    IsFriendTypeContext(Program, ReceiverTypeLinkage, MangledLinkageArity(ReceiverTypeLinkage),
		        ContextOwnerLinkage, ReceiverTypeLinkage, ImportIndex))
		{
			return true;
		}
		switch (Info.EffectiveAccess)
		{
		case EMemberAccess::Public:
			return true;
		case EMemberAccess::Private:
			// Private names are visible only inside the declaring class template / type
			// (including all instantiations of that arity). Distinct arities do not share access.
			return !ContextOwnerLinkage.empty() && AccessLinkageKey(Info.DeclaringOwnerLinkage) == AccessLinkageKey(ContextOwnerLinkage) && Info.DeclaringArity == MangledLinkageArity(ContextOwnerLinkage);
		case EMemberAccess::Protected:
			{
				if (ContextOwnerLinkage.empty())
				{
					return false;
				}
				const std::string ContextKey = AccessLinkageKey(ContextOwnerLinkage);
				const std::string DeclaringKey = AccessLinkageKey(Info.DeclaringOwnerLinkage);
				// Members of the declaring class template (same arity) may use the name on any receiver.
				if (ContextKey == DeclaringKey && MangledLinkageArity(ContextOwnerLinkage) == Info.DeclaringArity)
				{
					return true;
				}
				// Further derived classes may use it only through an object of their own type
				// (or a type derived from it) — not through a bare base reference (C++).
				if (!IsDerivedFromImpl(
				        Program,
				        ContextOwnerLinkage,
				        MangledLinkageArity(ContextOwnerLinkage),
				        Info.DeclaringOwnerLinkage,
				        Info.DeclaringArity,
				        0))
				{
					return false;
				}
				if (bTypeName) return true;
				const std::string ReceiverLinkage =
				    ReceiverTypeLinkage.empty() ? ContextOwnerLinkage : ReceiverTypeLinkage;
				if (SameAccessTypeIdentity(ReceiverLinkage, ContextOwnerLinkage))
				{
					return true;
				}
				return IsDerivedFromImpl(
				    Program,
				    ReceiverLinkage,
				    MangledLinkageArity(ReceiverLinkage),
				    ContextOwnerLinkage,
				    MangledLinkageArity(ContextOwnerLinkage),
				    0);
			}
		default:
			return false;
		}
	}

	bool IsAccessibleBase(
	    const FProgram& Program,
	    const std::string& DerivedLinkage,
	    const std::size_t DerivedArity,
	    const std::string& BaseLinkage,
	    const std::size_t BaseArity)
	{
		return IsAccessibleBaseImpl(
		    Program,
		    DerivedLinkage,
		    DerivedArity,
		    BaseLinkage,
		    BaseArity,
		    0);
	}
}
