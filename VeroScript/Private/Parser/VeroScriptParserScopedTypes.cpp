// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptParser.h"
#include "VeroScriptParserLookup.h"

#include "TypeSupport/VeroScriptTypeMemberBinding.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptAst.h"
#include "VeroScriptParserDetail.h"

#include <algorithm>
#include <utility>

namespace VeroScript
{
	void VeroScriptParser::RestoreDeclarationContext(const FDeferredSyntax &Context, const bool Body)
	{
		FragmentScopeId = Body && !Context.ParentScopeId.empty() ? Context.ParentScopeId : Context.ScopeId;
		ActiveValueParams = Context.ValueParameters;
		FunctionOwner = Context.FunctionOwner;
		FunctionTypeParameters = Context.FunctionTypeParameters;
		CallableTemplateParameters = Context.CallableTemplateParameters;
		RestoreBindings(Context);
		RestoredBlockScope = Body ? Context.ScopeId : std::string{};
		if (!Context.TypeOwner.empty())
		{
			std::string Owner = Context.TypeOwner;
			std::string ExpectedId = Context.TypeScope ? Context.TypeScope->Id : std::string{};
			std::string OwnerModule = Context.TypeScope ? Context.TypeScope->ModulePath : Context.ModulePath;
			for (std::size_t Depth = 0; !Owner.empty() && Depth < 64; ++Depth)
			{
				FTypeFrame Frame;
				Frame.Name = Owner;
				Frame.Scope = Context.TypeScope ? *Context.TypeScope : FTypeDeclarationScope{};
				std::string NextExpectedId;
				const auto Find = [&](const FProgram &Target, const auto &Declarations,
				                      const ETypeMemberDeclarationKind Kind)
				{
					for (const auto Position : GetParsedTypeLookup(Target).Names(Kind, Owner))
					{
						const auto &Declaration = Declarations[Position];
						if (Declaration.Name == Owner && Declaration.ModulePath == OwnerModule &&
						    (ExpectedId.empty() || Declaration.Scope.Id == ExpectedId))
						{
							Frame.Scope = Declaration.Scope;
							Frame.Parameters = Declaration.TypeParams;
							Frame.TemplateParameters = Declaration.TemplateParameters;
							NextExpectedId =
							    Declaration.DeclarationContext && Declaration.DeclarationContext->TypeScope
							    ? Declaration.DeclarationContext->TypeScope->Id
							    : std::string{};
							return;
						}
					}
				};
				if (ExternalTypeProgram)
				{
					Find(*ExternalTypeProgram, ExternalTypeProgram->Classes, ETypeMemberDeclarationKind::Class);
					Find(*ExternalTypeProgram, ExternalTypeProgram->Structs, ETypeMemberDeclarationKind::Struct);
					Find(*ExternalTypeProgram, ExternalTypeProgram->Interfaces, ETypeMemberDeclarationKind::Interface);
				}
				if (Program)
				{
					Find(*Program, Program->Classes, ETypeMemberDeclarationKind::Class);
					Find(*Program, Program->Structs, ETypeMemberDeclarationKind::Struct);
					Find(*Program, Program->Interfaces, ETypeMemberDeclarationKind::Interface);
				}
				Frame.Local = Frame.Scope.bLocal;
				Frame.Exported = Frame.Scope.bExported;
				const auto Parent = Frame.Scope.AccessTypeName;
				const auto ParentModule = Frame.Scope.AccessModulePath;
				Frame.Snapshot = std::make_shared<FTypeDeclarationScope>(Frame.Scope);
				TypeFrames.insert(TypeFrames.begin(), std::move(Frame));
				if (Parent == Owner)
				{
					break;
				}
				ExpectedId = std::move(NextExpectedId);
				Owner = Parent;
				OwnerModule = ParentModule;
			}
			for (std::size_t Index = 0;
			    Index < TypeFrames.size() && Index < Context.TypeAliasBoundaries.size(); ++Index)
			{
				TypeFrames[Index].AliasBoundary =
				    std::min(Context.TypeAliasBoundaries[Index], LocalTypeAliasScopes.size());
			}
		}
	}

	std::unique_ptr<FProgram> VeroScriptParser::TakeFragmentDeclarations()
	{
		return std::move(Program);
	}

	std::unique_ptr<FProgram> VeroScriptParser::ParseTypeFragment(const FTypeDeclarationScope &Owner,
	    const FDeferredSyntax &DeclarationContext)
	{
		RestoreDeclarationContext(DeclarationContext, false);
		InitializeFragment(DeclarationContext.Namespace, DeclarationContext.TypeParameters);
		FragmentOwnerScope = Owner;
		FragmentOwnerDepth = TypeFrames.size();
		std::vector<std::string> Parameters, Defaults;
		FTemplateParameterList TemplateParameters;
		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "template" &&
		    !ParseTemplatePrefix(Parameters, Defaults, &TemplateParameters))
		{
			return nullptr;
		}
		const bool Static = MatchOptionalStatic();
		bool Parsed = false;
		if (Owner.Kind == "struct")
		{
			Parsed = ParseStructDecl(Owner.bExported, {}, std::move(Parameters), std::move(Defaults),
			    EMemberAccess::Public, Static, std::move(TemplateParameters));
		}
		else if (Owner.Kind == "class")
		{
			Parsed = ParseClassDecl(Owner.bExported, {}, std::move(Parameters), std::move(Defaults),
			    EMemberAccess::Private, Static, std::move(TemplateParameters));
		}
		else if (Owner.Kind == "interface")
		{
			Parsed = ParseInterfaceDecl(Owner.bExported, {}, std::move(Parameters), std::move(Defaults), EMemberAccess::Public, std::move(TemplateParameters));
		}
		if (!Parsed || HasError() || !IsAtEnd())
		{
			return nullptr;
		}
		return TakeFragmentDeclarations();
	}

	std::unique_ptr<FProgram> VeroScriptParser::ParseLocalTypeFragment(const FDeferredSyntax &Context,
	    std::unique_ptr<FStmt> &Statement)
	{
		RestoreDeclarationContext(Context, false);
		InitializeFragment(Context.Namespace, Context.TypeParameters);
		BlockScopes = {Context.ScopeId};
		if (LocalTypeAliasScopes.empty())
		{
			PushAliasScope();
		}
		for (const auto *Binding : GetGeneratedBlockBindings(Context.ScopeId))
		{
			if (Binding->Anchor.SourceId != Context.Range.SourceId ||
			    Binding->Anchor.Begin > Context.Range.Begin)
			{
				continue;
			}
			if (LocalTypeAliasScopes.back().count(Binding->Name) == 0)
			{
				AddAliasBinding(Binding->Name, Binding->TypeName);
			}
		}
		Statement = ParseStatement();
		if (HasError() || !Statement || !IsAtEnd() || Statement->Kind != EStmtKind::TypeAlias)
		{
			if (!HasError())
			{
				ReportErrorAt(Peek(), "a block type fragment must contain exactly one "
				                      "type or alias declaration");
			}
			return nullptr;
		}
		return TakeFragmentDeclarations();
	}

	bool VeroScriptParser::IsScopedTypeDeclaration() const
	{
		return Check(ETokenKind::Keyword) &&
		    (Peek().Lexeme == "class" || Peek().Lexeme == "struct" || Peek().Lexeme == "interface" ||
		        Peek().Lexeme == "enum" || Peek().Lexeme == "typedef" ||
		        (Peek().Lexeme == "using" && !LooksLikeUsingMemberDecl()));
	}

	bool VeroScriptParser::ParseScopedTypeDeclaration(const EMemberAccess Access, FDeclMeta &&Meta,
	    const bool Local)
	{
		if (TypeFrames.size() >= 64)
		{
			ReportErrorAt(Peek(), "type declaration nested too deeply");
			return false;
		}
		const auto NameIndex = Current + (Peek().Lexeme == "enum" && Current + 1 < Tokens.size() && Tokens[Current + 1].Lexeme == "class" ? 2 : 1);
		if (NameIndex < Tokens.size() && (ActiveTypeParams.count(std::string(Tokens[NameIndex].Lexeme)) != 0 || ActiveValueParams.count(std::string(Tokens[NameIndex].Lexeme)) != 0))
		{
			ReportErrorAt(Tokens[NameIndex], "type declaration shadows an active generic parameter");
			return false;
		}
		const auto SavedAccess = DeclarationAccess;
		const std::string SavedPrefix = LocalDeclarationPrefix;
		DeclarationAccess = Access;
		if (Local)
		{
			LocalDeclarationPrefix =
			    MakeLocalTypePrefix(BlockScopes.empty() ? FragmentScopeId : BlockScopes.back(), FileLabel);
		}
		const bool Exported = !Local && !TypeFrames.empty() && TypeFrames.back().Exported;
		bool Success = false;
		if (Peek().Lexeme == "struct")
		{
			Success = ParseStructDecl(Exported, std::move(Meta));
		}
		else if (Peek().Lexeme == "class")
		{
			Success = ParseClassDecl(Exported, std::move(Meta));
		}
		else if (Peek().Lexeme == "interface")
		{
			Success = ParseInterfaceDecl(Exported, std::move(Meta));
		}
		else if (Peek().Lexeme == "enum")
		{
			Success = ParseEnumDecl(Exported, std::move(Meta));
		}
		else
		{
			Success = ParseUsingDecl(Exported);
		}
		LocalDeclarationPrefix = SavedPrefix;
		DeclarationAccess = SavedAccess;
		return Success;
	}

	std::unique_ptr<FStmt> VeroScriptParser::ParseLocalTypeDeclaration(FDeclMeta Meta)
	{
		const auto Location = Peek().Location;
		const bool ScopedEnum =
		    Peek().Lexeme == "enum" && Current + 1 < Tokens.size() && Tokens[Current + 1].Lexeme == "class";
		const auto NameIndex = Current + (ScopedEnum ? 2 : 1);
		if (NameIndex >= Tokens.size() || Tokens[NameIndex].Kind != ETokenKind::Identifier)
		{
			ReportErrorAt(Peek(), "expected local type name");
			return nullptr;
		}
		const std::string Name(Tokens[NameIndex].Lexeme);
		if (LocalTypeAliasScopes.empty())
		{
			PushAliasScope();
		}
		const std::string Canonical =
		    MakeLocalTypePrefix(BlockScopes.empty() ? FragmentScopeId : BlockScopes.back(), FileLabel) + "." +
		    Name;
		ForwardTypeNames.insert(Canonical);
		if (!AddAliasBinding(Name, Canonical))
		{
			ReportErrorAt(Peek(), "duplicate local type: " + Name);
			return nullptr;
		}
		if (!ParseScopedTypeDeclaration(EMemberAccess::Public, std::move(Meta), true))
		{
			return nullptr;
		}
		auto Statement = std::make_unique<FTypeAliasStmt>(Name, Canonical);
		Statement->bTypeDefinition = true;
		Statement->Location = Location;
		return Statement;
	}

	std::unique_ptr<FBlockStmt> VeroScriptParser::ParseCallableBlock(
	    const std::string &Name, const std::vector<FFunctionParam> &Parameters, const bool Const,
	    const FTemplateParameterList *TemplateParameters)
	{
		auto PreviousTemplates = CallableTemplateParameters;
		if (TemplateParameters)
		{
			auto Records = CallableTemplateParameters.GetParameters();
			for (const auto &Parameter : TemplateParameters->GetParameters())
			{
				if (std::none_of(Records.begin(), Records.end(),
				        [&](const auto &Existing)
				{
					return Existing.Name == Parameter.Name;
				}))
				{
					Records.push_back(Parameter);
				}
			}
			CallableTemplateParameters.Assign(std::move(Records));
		}
		const std::string PreviousOwner = FunctionOwner;
		auto PreviousParameters = std::move(FunctionTypeParameters);
		FunctionTypeParameters = TypeFrames.empty() ? std::vector<std::string>{} : TypeFrames.back().Parameters;
		std::vector<std::string> AdditionalParameters(ActiveTypeParams.begin(), ActiveTypeParams.end());
		std::sort(AdditionalParameters.begin(), AdditionalParameters.end());
		for (const auto &Parameter : AdditionalParameters)
		{
			if (std::find(FunctionTypeParameters.begin(), FunctionTypeParameters.end(), Parameter) ==
			    FunctionTypeParameters.end())
			{
				FunctionTypeParameters.push_back(Parameter);
			}
		}
		const std::string PreviousScope = FragmentScopeId;
		auto PreviousBlocks = std::move(BlockScopes);
		BlockScopes.clear();
		std::string Identity = CallableScopeName(ActiveNamespacePrefix, Name, Parameters, Const);
		if (Name.rfind("$lambda:", 0) == 0)
		{
			Identity = TypeScopeIdentity(PreviousOwner, "lambda", Identity);
		}
		FunctionOwner = Identity;
		FragmentScopeId = TypeScopeIdentity(FileLabel, "function", Identity);
		PushRuntimeScope();
		for (const auto &Parameter : Parameters)
		{
			AddRuntimeBinding(Parameter.Name);
		}
		auto Result = ParseBlock();
		PopRuntimeScope();
		FunctionOwner = PreviousOwner;
		FunctionTypeParameters = std::move(PreviousParameters);
		CallableTemplateParameters = std::move(PreviousTemplates);
		FragmentScopeId = PreviousScope;
		BlockScopes = std::move(PreviousBlocks);
		return Result;
	}

	void VeroScriptParser::EnterTypeDeclaration(const std::string &Name, const bool Exported,
	    const std::vector<std::string> &Parameters,
	    FTypeDeclarationScope &Scope, FTemplateParameterList &TemplateParameters)
	{
		if (TemplateParameters.IsEmpty() && !Parameters.empty())
		{
			std::vector<FTemplateParameter> Records;
			for (const auto &ParameterName : Parameters)
			{
				FTemplateParameter Parameter;
				Parameter.Name = ParameterName;
				Parameter.Id = Scope.Id + ":parameter:" + ParameterName;
				Records.push_back(std::move(Parameter));
			}
			TemplateParameters.Assign(std::move(Records));
		}
		Scope.OwnTypeParameterCount = static_cast<std::uint32_t>(Parameters.size());
		Scope.OwnTemplateParameterCount = static_cast<std::uint32_t>(TemplateParameters.GetOwnCount());
		std::vector<FTemplateParameter> FullParameters = Scope.EnclosingTemplateParameters.GetParameters();
		const auto EnclosingCount = FullParameters.size();
		const auto &Own = TemplateParameters.GetParameters();
		FullParameters.insert(FullParameters.end(), Own.begin(), Own.end());
		TemplateParameters.Assign(std::move(FullParameters), EnclosingCount);
		for (const auto &Parameter : TemplateParameters.GetParameters())
		{
			if (Parameter.Kind == ETemplateParameterKind::Value)
			{
				ActiveValueParams.emplace(Parameter.Name, Parameter);
			}
		}
		Scope.bExported = Exported;
		Scope.Id += "/arity:" + std::to_string(Scope.OwnTemplateParameterCount);
		FTypeFrame Frame;
		Frame.TemplateParameters = TemplateParameters;
		Frame.Scope = Scope;
		Frame.Snapshot = std::make_shared<FTypeDeclarationScope>(Scope);
		Frame.Name = Name;
		Frame.Namespace = ActiveNamespacePrefix;
		Frame.DeclarationPrefix = LocalDeclarationPrefix;
		Frame.AliasBoundary = LocalTypeAliasScopes.size();
		PushAliasScope();
		Frame.Exported = Exported;
		Frame.Local = Scope.bLocal;
		Frame.Parameters = Scope.EnclosingTypeParameters;
		Frame.Parameters.insert(Frame.Parameters.end(), Parameters.begin(), Parameters.end());
		TypeFrames.push_back(std::move(Frame));
		ActiveNamespacePrefix = Name;
		LocalDeclarationPrefix.clear();
		std::size_t Depth = 0;
		for (std::size_t Index = Current; Index + 1 < Tokens.size(); ++Index)
		{
			if ((Index & 255u) == 0)
			{
				CheckAnalysisCancellation();
			}
			if (Tokens[Index].Lexeme == "{")
			{
				++Depth;
			}
			else if (Tokens[Index].Lexeme == "}")
			{
				if (Depth == 0)
				{
					break;
				}
				--Depth;
			}
			else if (Depth == 0 && Tokens[Index].Kind == ETokenKind::Keyword &&
			    (Tokens[Index].Lexeme == "class" || Tokens[Index].Lexeme == "struct" ||
			        Tokens[Index].Lexeme == "interface" || Tokens[Index].Lexeme == "enum" ||
			        Tokens[Index].Lexeme == "using" || Tokens[Index].Lexeme == "typedef"))
			{
				if (Tokens[Index].Lexeme == "class" && Index > 0 && (Tokens[Index - 1].Lexeme == "<" || Tokens[Index - 1].Lexeme == ","))
				{
					continue;
				}
				auto NameIndex = Tokens[Index].Lexeme == "enum" && Tokens[Index + 1].Lexeme == "class"
				    ? Index + 2
				    : Index + 1;
				if (Tokens[Index].Lexeme == "using" &&
				    (NameIndex + 1 >= Tokens.size() || Tokens[NameIndex + 1].Lexeme != "="))
				{
					continue;
				}
				if (Index > 0 && Tokens[Index - 1].Lexeme == "friend")
				{
					continue;
				}
				if (NameIndex < Tokens.size() && Tokens[NameIndex].Kind == ETokenKind::Identifier)
				{
					const std::string Child = Name + "." + std::string(Tokens[NameIndex].Lexeme);
					ForwardTypeNames.insert(Child);
					ForwardTypeNames.insert(Scope.DisplayName + "." + std::string(Tokens[NameIndex].Lexeme));
					ForwardTypeNames.insert(QualifyTypeDeclName(std::string(Tokens[NameIndex].Lexeme)));
					if (Tokens[Index].Lexeme == "interface")
					{
						ForwardInterfaceNames.insert(Child);
					}
				}
			}
		}
	}

	void VeroScriptParser::LeaveTypeDeclaration()
	{
		const auto &Parameters = TypeFrames.back().TemplateParameters;
		for (std::size_t Index = Parameters.GetEnclosingCount(); Index < Parameters.GetParameters().size(); ++Index)
		{
			ActiveValueParams.erase(Parameters.GetParameters()[Index].Name);
		}
		ActiveNamespacePrefix = std::move(TypeFrames.back().Namespace);
		LocalDeclarationPrefix = std::move(TypeFrames.back().DeclarationPrefix);
		PopAliasScope();
		TypeFrames.pop_back();
	}

	FTypeDeclarationScope VeroScriptParser::MakeTypeScope(const std::string &Name,
	    const std::size_t ParameterCount) const
	{
		if (FragmentOwnerScope && Name == FragmentOwnerScope->QualifiedName &&
		    TypeFrames.size() == FragmentOwnerDepth)
		{
			auto Scope = *FragmentOwnerScope;
			const auto Arity = Scope.Id.rfind("/arity:");
			if (Arity != std::string::npos)
			{
				Scope.Id.resize(Arity);
			}
			return Scope;
		}
		FTypeDeclarationScope Result;
		Result.QualifiedName = Name;
		Result.SimpleName = TypeNameLeaf(Name);
		Result.DisplayName = LocalDeclarationPrefix.empty() && !TypeFrames.empty()
		    ? TypeFrames.back().Scope.DisplayName + "." + Result.SimpleName
		    : Name;
		Result.Access = DeclarationAccess;
		Result.OwnTypeParameterCount = static_cast<std::uint32_t>(ParameterCount);
		Result.ScopeId = LocalDeclarationPrefix.empty() && !TypeFrames.empty() ? TypeFrames.back().Scope.Id
		    : !BlockScopes.empty()                                             ? BlockScopes.back()
		    : ActiveNamespacePrefix.empty()
		    ? std::string(FileLabel)
		    : TypeScopeIdentity(FileLabel, "namespace", ActiveNamespacePrefix);
		Result.ParentScopeId = Result.ScopeId;
		Result.OwnerTypeName =
		    LocalDeclarationPrefix.empty() && !TypeFrames.empty() ? TypeFrames.back().Name : std::string{};
		Result.OwnerFunctionName = FunctionOwner;
		Result.FunctionTypeParameters = FunctionTypeParameters;
		Result.ModulePath = std::string(FileLabel);
		Result.AccessTypeName = TypeFrames.empty() ? std::string{} : TypeFrames.back().Name;
		Result.AccessModulePath = TypeFrames.empty() ? std::string{} : TypeFrames.back().Scope.ModulePath;
		Result.bLocal = !LocalDeclarationPrefix.empty() || (!TypeFrames.empty() && TypeFrames.back().Local);
		Result.Id = TypeScopeIdentity(Result.ScopeId, "type", Result.SimpleName);
		if (!TypeFrames.empty())
		{
			Result.EnclosingTypeParameters = TypeFrames.back().Parameters;
			Result.EnclosingTemplateParameters = TypeFrames.back().TemplateParameters;
		}
		if (Result.bLocal)
		{
			auto Records = Result.EnclosingTemplateParameters.GetParameters();
			for (const auto &Parameter : CallableTemplateParameters.GetParameters())
			{
				if (std::none_of(Records.begin(), Records.end(),
				        [&](const auto &Existing)
				{
					return Existing.Name == Parameter.Name;
				}))
				{
					Records.push_back(Parameter);
				}
			}

			std::vector<std::string> Names;
			for (const auto &Entry : ActiveValueParams)
			{
				Names.push_back(Entry.first);
			}
			std::sort(Names.begin(), Names.end());
			for (const auto &ParameterName : Names)
			{
				if (std::none_of(Records.begin(), Records.end(),
				        [&](const auto &Parameter)
				{
					return Parameter.Name == ParameterName;
				}))
				{
					Records.push_back(ActiveValueParams.at(ParameterName));
				}
			}
			std::vector<std::string> CapturedTypeNames(ActiveTypeParams.begin(), ActiveTypeParams.end());
			std::sort(CapturedTypeNames.begin(), CapturedTypeNames.end());
			for (const auto &ParameterName : CapturedTypeNames)
			{
				if (std::none_of(Records.begin(), Records.end(), [&](const auto &Parameter)
				{
					return Parameter.Name == ParameterName;
				}))
				{
					FTemplateParameter Parameter;
					Parameter.Name = ParameterName;
					Parameter.Id = Result.Id + ":capture:" + ParameterName;
					Records.push_back(std::move(Parameter));
				}
			}
			Result.EnclosingTemplateParameters.Assign(std::move(Records));
			for (const auto &Bindings : RuntimeBindingScopes)
			{
				Result.ForbiddenRuntimeNames.insert(Result.ForbiddenRuntimeNames.end(), Bindings.begin(),
				    Bindings.end());
			}
			std::sort(Result.ForbiddenRuntimeNames.begin(), Result.ForbiddenRuntimeNames.end());
			Result.ForbiddenRuntimeNames.erase(
			    std::unique(Result.ForbiddenRuntimeNames.begin(), Result.ForbiddenRuntimeNames.end()),
			    Result.ForbiddenRuntimeNames.end());
			std::vector<std::string> Parameters(ActiveTypeParams.begin(), ActiveTypeParams.end());
			std::sort(Parameters.begin(), Parameters.end());
			for (const auto &Parameter : Parameters)
			{
				if (std::find(Result.EnclosingTypeParameters.begin(), Result.EnclosingTypeParameters.end(),
				        Parameter) == Result.EnclosingTypeParameters.end())
				{
					Result.EnclosingTypeParameters.push_back(Parameter);
				}
			}
		}
		return Result;
	}

	std::string VeroScriptParser::ResolveScopedTypeApplication(std::string Name, const bool bCanonical)
	{
		if (ParserDetail::IsTypeKeyword(Name))
		{
			return Name;
		}
		thread_local std::size_t Depth = 0;
		if (Depth >= 32)
		{
			ReportErrorAt(Previous(), "cyclic type alias or type path");
			return {};
		}
		struct FDepthGuard
		{
			std::size_t &Value;
			~FDepthGuard()
			{
				--Value;
			}
		} Guard{Depth};
		++Depth;
		std::vector<FTypePathSegment> Segments;
		if (!ParseTypePath(Name, Segments))
		{
			return Name;
		}
		if (Segments.size() == 1 && ParserDetail::IsContainerTypeKeyword(Segments.front().Name))
		{
			return Name;
		}
		if (Name.rfind("::", 0) != 0 && Segments.size() == 1 && !Segments.front().bApplication)
		{
			for (auto I = LocalTypeAliasScopes.rbegin(); I != LocalTypeAliasScopes.rend(); ++I)
			{
				const auto Alias = I->find(Segments.front().Name);
				if (Alias != I->end() && Alias->second.rfind("$local", 0) != 0)
				{
					return Alias->second;
				}
			}
		}
		std::string Path;
		std::vector<std::string> Arguments;
		bool Resolved = false;
		bool ExplicitFinalApplication = false;
		for (std::size_t SegmentIndex = 0; SegmentIndex < Segments.size(); ++SegmentIndex)
		{
			const auto &Segment = Segments[SegmentIndex];
			const auto ParentPath = Path;
			if (!Path.empty())
			{
				Path += '.';
			}
			Path += Segment.Name;
			if (Program && IsForwardTypeName(ParentPath))
			{
				const auto Member = FindScopedTypeMember(ParentPath, Arguments, Segment.Name);
				if (Member.bAmbiguous)
				{
					ReportErrorAt(Previous(), "ambiguous inherited type: " + Path);
					return {};
				}
				if (!Member.Name.empty())
				{
					Path = Member.Name;
					Arguments = Member.OwnerArguments;
				}
			}
			const FTypeAliasDecl *Alias = nullptr;
			const auto FindAlias = [&](const FProgram &Target)
			{
				for (const auto Position : GetParsedTypeLookup(Target).Names(ETypeMemberDeclarationKind::Alias, Path))
				{
					const auto &Candidate = Target.TypeAliases[Position];
					if (Candidate.Name == Path && (Candidate.ModulePath == FileLabel || !Alias))
					{
						Alias = &Candidate;
					}
				}
			};
			if (ExternalTypeProgram)
			{
				FindAlias(*ExternalTypeProgram);
			}
			if (Program)
			{
				FindAlias(*Program);
			}
			if (Alias)
			{
				if (SegmentIndex + 1 == Segments.size() && Alias->Scope.OwnerTypeName.empty() &&
				    !Alias->Scope.bLocal)
				{
					return Name;
				}
				std::string Target = SubstituteScopedTypeParameters(
				    Alias->TypeName, EnclosingTemplateParameterNames(Alias->Scope), Arguments);
				if (SegmentIndex + 1 == Segments.size())
				{
					const auto Base = Target.substr(0, Target.find('<'));
					const bool UserTarget = IsUserTypeName(Base) && !ParserDetail::IsTypeKeyword(Base) &&
					    !ParserDetail::IsContainerTypeKeyword(Base);
					return Name.rfind("::", 0) == 0 && Target.rfind("::", 0) != 0 && UserTarget
					    ? "::" + Target
					    : Target;
				}
				std::vector<FTypePathSegment> TargetParts;
				if (ParseTypePath(Target, TargetParts) && TargetParts.size() > 1 &&
				    !TargetParts.back().Arguments.empty())
				{
					// Stored targets already carry the complete flattened environment.
					// Restore segment applications before appending another child path.
					const auto Values = TargetParts.back().Arguments;
					std::size_t Consumed = 0;
					std::string Prefix;
					for (auto &Part : TargetParts)
					{
						if (!Prefix.empty())
						{
							Prefix += '.';
						}
						Prefix += Part.Name;
						std::size_t Own = 0;
						const auto Find = [&](const FProgram &Target, const auto &Declarations,
						                      const ETypeMemberDeclarationKind Kind)
						{
							for (const auto Position : GetParsedTypeLookup(Target).Names(Kind, Prefix))
							{
								const auto &D = Declarations[Position];
								if (D.Name == Prefix)
								{
									Own = OwnTemplateParameterCount(D.Scope);
								}
							}
						};
						if (ExternalTypeProgram)
						{
							Find(*ExternalTypeProgram, ExternalTypeProgram->Classes, ETypeMemberDeclarationKind::Class);
							Find(*ExternalTypeProgram, ExternalTypeProgram->Structs, ETypeMemberDeclarationKind::Struct);
							Find(*ExternalTypeProgram, ExternalTypeProgram->Interfaces, ETypeMemberDeclarationKind::Interface);
						}
						if (Program)
						{
							Find(*Program, Program->Classes, ETypeMemberDeclarationKind::Class);
							Find(*Program, Program->Structs, ETypeMemberDeclarationKind::Struct);
							Find(*Program, Program->Interfaces, ETypeMemberDeclarationKind::Interface);
						}
						Part.Arguments.clear();
						Part.bApplication = Own != 0;
						for (std::size_t I = 0; I < Own && Consumed < Values.size(); ++I)
						{
							Part.Arguments.push_back(Values[Consumed++]);
						}
					}
					if (Consumed == Values.size())
					{
						Target.clear();
						for (const auto &Part : TargetParts)
						{
							if (!Target.empty())
							{
								Target += '.';
							}
							Target += Part.Name;
							if (Part.bApplication)
							{
								Target += '<';
								for (std::size_t I = 0; I < Part.Arguments.size(); ++I)
								{
									if (I)
									{
										Target += ',';
									}
									Target += Part.Arguments[I];
								}
								Target += '>';
							}
						}
					}
				}
				for (std::size_t Index = SegmentIndex + 1; Index < Segments.size(); ++Index)
				{
					Target += '.' + Segments[Index].Name;
					if (Segments[Index].bApplication)
					{
						Target += '<';
						for (std::size_t Argument = 0; Argument < Segments[Index].Arguments.size();
						    ++Argument)
						{
							if (Argument != 0)
							{
								Target += ',';
							}
							Target += Segments[Index].Arguments[Argument];
						}
						Target += '>';
					}
				}
				return ResolveScopedTypeApplication(std::move(Target), true);
			}
			const FTypeDeclarationScope *Scope = nullptr;
			bool ExactArity = false;
			const auto WantedArity = Arguments.size() + Segment.Arguments.size();
			const std::vector<std::string> *Parameters = nullptr;
			const std::vector<std::string> *Defaults = nullptr;
			const auto Find = [&](const FProgram &Target, const auto &Declarations,
			                      const ETypeMemberDeclarationKind Kind)
			{
				for (const auto Position : GetParsedTypeLookup(Target).Names(Kind, Path))
				{
					const auto &Declaration = Declarations[Position];
					if (Declaration.Name == Path &&
					    (!Scope || (!ExactArity && TemplateParameterNames(Declaration).size() == WantedArity) ||
					        (ExactArity && TemplateParameterNames(Declaration).size() == WantedArity &&
					            Declaration.ModulePath == FileLabel)))
					{
						ExactArity = TemplateParameterNames(Declaration).size() == WantedArity;
						Scope = &Declaration.Scope;
						Parameters = &TemplateParameterNames(Declaration);
						Defaults = &TemplateParameterDefaults(Declaration);
					}
				}
			};
			if (ExternalTypeProgram)
			{
				Find(*ExternalTypeProgram, ExternalTypeProgram->Classes, ETypeMemberDeclarationKind::Class);
				Find(*ExternalTypeProgram, ExternalTypeProgram->Structs, ETypeMemberDeclarationKind::Struct);
				Find(*ExternalTypeProgram, ExternalTypeProgram->Interfaces, ETypeMemberDeclarationKind::Interface);
			}
			if (Program)
			{
				Find(*Program, Program->Classes, ETypeMemberDeclarationKind::Class);
				Find(*Program, Program->Structs, ETypeMemberDeclarationKind::Struct);
				Find(*Program, Program->Interfaces, ETypeMemberDeclarationKind::Interface);
			}
			const FTypeFrame *Frame = nullptr;
			for (auto It = TypeFrames.rbegin(); It != TypeFrames.rend(); ++It)
			{
				if (It->Name == Path || It->Scope.DisplayName == Path ||
				    (!ParentPath.empty() && It->Scope.OwnerTypeName == ParentPath &&
				        It->Scope.SimpleName == Segment.Name))
				{
					Frame = &*It;
					Path = Frame->Name;
					break;
				}
			}
			if (!Scope && Frame)
			{
				Scope = &Frame->Scope;
				Parameters = Frame->TemplateParameters.IsEmpty() ? &Frame->Parameters : &Frame->TemplateParameters.GetNames();
			}
			const auto FindEnum = [&](const FProgram &Target)
			{
				for (const auto Position : GetParsedTypeLookup(Target).Names(ETypeMemberDeclarationKind::Enum, Path))
				{
					const auto &Declaration = Target.Enums[Position];
					if (Declaration.Name == Path && (Declaration.ModulePath == FileLabel || !Scope))
					{
						Scope = &Declaration.Scope;
						Parameters = &EnclosingTemplateParameterNames(Declaration.Scope);
					}
				}
			};
			if (ExternalTypeProgram)
			{
				FindEnum(*ExternalTypeProgram);
			}
			if (Program)
			{
				FindEnum(*Program);
			}
			if (!Scope)
			{
				// A later member has a registered name before its definition is parsed.
				// Its lexical owner still supplies the captured generic environment.
				if (Arguments.empty())
				{
					for (auto Owner = TypeFrames.rbegin(); Owner != TypeFrames.rend(); ++Owner)
					{
						const auto Prefix =
						    Owner->Name + "$arity" + std::to_string(OwnTemplateParameterCount(Owner->Scope)) + ".";
						if (Path.rfind(Prefix, 0) == 0)
						{
							Arguments = Owner->TemplateParameters.IsEmpty() ? Owner->Parameters : Owner->TemplateParameters.GetNames();
							Resolved = true;
							break;
						}
					}
				}
				Arguments.insert(Arguments.end(), Segment.Arguments.begin(), Segment.Arguments.end());
				continue;
			}
			Resolved = true;
			const auto Captured = EnclosingTemplateParameterNames(*Scope).size();
			const bool FlatArguments = bCanonical && Arguments.empty() && Parameters &&
			    Segment.Arguments.size() == Parameters->size();
			if (FlatArguments)
			{
				Arguments.assign(Segment.Arguments.begin(), Segment.Arguments.begin() + Captured);
			}
			// Lexical canonical paths contain owner arity markers, without applications.
			// Their partial forward lookup must not omit an intermediate owner's captures.
			const bool ImplicitOwners = Frame && !FlatArguments &&
			    std::none_of(Segments.begin(), Segments.begin() + SegmentIndex,
			        [](const auto &Part)
			{
				return Part.bApplication;
			});
			if ((Arguments.empty() || ImplicitOwners) && Captured != 0)
			{
				const bool Lexical = Scope->bLocal || Frame ||
				    std::any_of(TypeFrames.begin(), TypeFrames.end(),
				        [&](const auto &Owner)
				{
					return Owner.Name == Scope->OwnerTypeName;
				});
				if (Lexical)
				{
					Arguments = EnclosingTemplateParameterNames(*Scope);
				}
			}
			if (Arguments.size() != Captured && !bDeclarationOutline)
			{
				ReportErrorAt(Previous(), "enclosing generic type requires type arguments: " + Path);
				return {};
			}
			if (Frame && !Segment.bApplication)
			{
				// Keep explicit enclosing arguments; only the current segment is implicit.
				const auto &FrameParameters = Frame->TemplateParameters.IsEmpty()
				    ? Frame->Parameters
				    : Frame->TemplateParameters.GetNames();
				Arguments.insert(Arguments.end(), FrameParameters.begin() + Captured, FrameParameters.end());
			}
			else
			{
				Arguments.insert(Arguments.end(), Segment.Arguments.begin() + (FlatArguments ? Captured : 0),
				    Segment.Arguments.end());
			}
			if (Parameters && Defaults && SegmentIndex + 1 < Segments.size())
			{
				while (Arguments.size() < Parameters->size() && Arguments.size() < Defaults->size() &&
				    !(*Defaults)[Arguments.size()].empty())
				{
					std::string Default =
					    SubstituteScopedTypeParameters((*Defaults)[Arguments.size()], *Parameters, Arguments);
					Arguments.push_back("@default:" + std::move(Default));
				}
			}
			if (SegmentIndex + 1 == Segments.size())
			{
				ExplicitFinalApplication = Segment.bApplication && Parameters && !Parameters->empty();
			}
		}
		if (!Resolved)
		{
			return Name;
		}
		if (!Arguments.empty() || ExplicitFinalApplication)
		{
			Path += '<';
			for (std::size_t Index = 0; Index < Arguments.size(); ++Index)
			{
				if (Index != 0)
				{
					Path += ',';
				}
				Path += Arguments[Index];
			}
			Path += '>';
		}
		return Name.rfind("::", 0) == 0 ? "::" + Path : Path;
	}

	std::string VeroScriptParser::QualifyTypeDeclName(const std::string &Name) const
	{
		if (!LocalDeclarationPrefix.empty() || TypeFrames.empty())
		{
			return QualifyDeclName(Name);
		}
		const auto &Owner = TypeFrames.back();
		return Owner.Name + "$arity" + std::to_string(OwnTemplateParameterCount(Owner.Scope)) + '.' + Name;
	}

	FTypeMemberBinding VeroScriptParser::FindScopedTypeMember(const std::string &Owner,
	    const std::vector<std::string> &Arguments, const std::string &Member) const
	{
		if (!Program) return {};
		FTypeMemberOwnerDeclarations Open;
		for (auto Frame = TypeFrames.rbegin(); Frame != TypeFrames.rend(); ++Frame)
		{
			if (Frame->Name == Owner)
			{
				Open.Struct = Frame->OpenStruct;
				Open.Class = Frame->OpenClass;
				Open.Interface = Frame->OpenInterface;
				break;
			}
		}
		const auto Lookup = GetTypeMemberLookupView();
		return FindTypeMember(*Program, ExternalTypeProgram, Owner, Arguments, Member,
		    std::string(FileLabel), &Open, &Lookup);
	}

	const std::vector<const FGeneratedBlockTypeBinding *> &VeroScriptParser::GetGeneratedBlockBindings(
	    const std::string &Scope)
	{
		if (!bGeneratedBlockIndexBuilt)
		{
			if (ExternalTypeProgram)
			{
				for (const auto &Binding : ExternalTypeProgram->GeneratedBlockTypes)
				{
					GeneratedBlockTypeIndex[Binding.ScopeId].push_back(&Binding);
				}
			}
			bGeneratedBlockIndexBuilt = true;
		}
		static const std::vector<const FGeneratedBlockTypeBinding *> Empty;
		const auto Found = GeneratedBlockTypeIndex.find(Scope);
		return Found == GeneratedBlockTypeIndex.end() ? Empty : Found->second;
	}
} // namespace VeroScript
