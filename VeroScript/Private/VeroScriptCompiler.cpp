// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "TypeSupport/VeroScriptConstantRepresentation.h"
#include "VeroScriptCompiler.h"
#include "ConstantEvaluation/VeroScriptConstantBranches.h"
#include "VeroScriptAstClone.h"
#include "ConstantEvaluation/VeroScriptConstantRequest.h"
#include "VeroScriptStandardLibrary.h"
#include "VeroScriptProgramPreparation.h"
#include "Parser/VeroScriptParserDetail.h"
#include <stdexcept>
#include "Compiler/VeroScriptCompilerAnalysis.h"
#include "ConstantEvaluation/VeroScriptConstantSyntax.h"
#include "VeroScriptBytecodeLinker.h"
#include "ExecutionSupport/VeroScriptInstructionCosts.h"

#include "VeroScriptAst.h"
#include "TypeSupport/VeroScriptRuntimeTypeScope.h"
#include "VeroScriptMemberAccess.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptRuntime.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <string_view>
#include <tuple>
#include <unordered_set>

namespace VeroScript
{

	std::unique_ptr<FBytecodeModule> VeroScriptCompiler::CompilePrepared(const FPreparedProgram& Prepared,
	    const std::vector<FNativeFunction>* Natives, const IHostBindingCatalog* Bindings)
	{
		if (!Prepared.MatchesHost(Natives, Bindings))
		{
			bHasError = false;
			Error = {};
			Diagnostics.clear();
			ReportError("prepared program host configuration does not match");
			return nullptr;
		}
		return Compile(Prepared.GetProgram(), Natives, Bindings);
	}

	std::unique_ptr<FBytecodeModule> VeroScriptCompiler::Compile(
	    const FProgram& Program,
	    const std::vector<FNativeFunction>* const Natives,
	    const IHostBindingCatalog* const Bindings)
	{
		FConstantEvaluationRequestScope ConstantRequest(bConstantEvaluationLimitsExplicit ? ConstantEvaluationLimits : Program.ConstantEvaluationLimits);
		if (Program.bDeclarationsPrepared && bConstantEvaluationLimitsExplicit)
		{
			const auto& Policy = Program.ConstantEvaluationLimits;
			if (Policy.MaxWork != ConstantEvaluationLimits.MaxWork || Policy.MaxCallDepth != ConstantEvaluationLimits.MaxCallDepth || Policy.MaxExpressionDepth != ConstantEvaluationLimits.MaxExpressionDepth || Policy.MaxMemoryBytes != ConstantEvaluationLimits.MaxMemoryBytes || Policy.MaxCacheBytes != ConstantEvaluationLimits.MaxCacheBytes)
			{
				bHasError = false;
				Diagnostics.clear();
				ReportError("prepared program constant evaluation policy does not match");
				return nullptr;
			}
		}
		MethodBindingCodegenInfo.clear();
		if (Program.bDeclarationsPrepared &&
		    !VeroScriptProgramPreparation::MatchesHost(Program, Natives, Bindings))
		{
			bHasError = false;
			Error = {};
			Diagnostics.clear();
			ReportError("prepared program host configuration does not match");
			return nullptr;
		}
		if (!Program.bDeclarationsPrepared)
		{
			auto Input = CloneProgram(Program);
			if (bConstantEvaluationLimitsExplicit)
			{
				Input->ConstantEvaluationLimits = ConstantEvaluationLimits;
			}
			auto Prepared = VeroScriptProgramPreparation::Prepare(std::move(Input), {}, Natives, Bindings);
			if (!Prepared.Succeeded())
			{
				bHasError = true;
				Diagnostics = std::move(Prepared.Diagnostics);
				Error = Diagnostics.front();
				return nullptr;
			}
			return CompilePrepared(*Prepared.Prepared, Natives, Bindings);
		}
		// The loader has finished merging imports. Keep the index local to this
		// compilation even when a caller reuses a compiler or prepared program.
		const ModuleVisibility::FImportIndex ImportIndex(Program.Imports);
		struct FActiveProgramScope
		{
			const FProgram*& Program;
			const ModuleVisibility::FImportIndex*& ImportIndex;
			~FActiveProgramScope()
			{
				Program = nullptr;
				ImportIndex = nullptr;
			}
		} ActiveScope{ActiveProgram, ActiveImportIndex};
		try
		{
			ResetCompilationState(Program, Natives, Bindings, ImportIndex);
		}
		catch (const std::invalid_argument& Exception)
		{
			const std::string_view MessageId = Exception.what();
			ReportErrorId(MessageId == "diag.memory_native_limit" || MessageId == "diag.memory_reserved_type" ||
			            MessageId == "diag.stdlib_native_limit"
			        ? MessageId
			        : "diag.stdlib_reserved_binding",
			    {});
			return nullptr;
		}
		FScriptError ScopeError;
		const auto FunctionTypeAccess = [&](const FMemberAccessInfo& Info, const FScopedTypeUse& Use)
		{
			struct FAccessContextGuard
			{
				std::string& Function;
				std::string& Owner;
				std::string& Module;
				std::string& Namespace;
				std::string PreviousFunction, PreviousOwner, PreviousModule, PreviousNamespace;
				~FAccessContextGuard()
				{
					Function = std::move(PreviousFunction);
					Owner = std::move(PreviousOwner);
					Module = std::move(PreviousModule);
					Namespace = std::move(PreviousNamespace);
				}
			} Guard{CurrentAccessFunctionLinkage, CurrentOwnerTypeLinkage, CurrentModulePath, CurrentNamespacePrefix,
			    CurrentAccessFunctionLinkage, CurrentOwnerTypeLinkage, CurrentModulePath, CurrentNamespacePrefix};
			CurrentAccessFunctionLinkage.clear();
			CurrentOwnerTypeLinkage.clear();
			const auto InBody = [&](const FBlockStmt* Body)
			{
				return Body && Use.Context && !Body->ScopeId.empty() && (Use.Context->ScopeId == Body->ScopeId || Use.Context->ScopeId.rfind(Body->ScopeId + "/", 0) == 0);
			};
			const auto FindOwner = [&](const auto& Declarations)
			{
				for (const auto& Declaration : Declarations)
				{
					if (Declaration.Name != Use.ContextType || Declaration.ModulePath != Use.ContextModule)
					{
						continue;
					}
					CurrentOwnerTypeLinkage = GetSymbolLinkageName(Declaration.ModulePath, Declaration.bExported, Declaration.Name);
					if (!TemplateParameterNames(Declaration).empty())
					{
						CurrentOwnerTypeLinkage = MangleGenericInstanceName(CurrentOwnerTypeLinkage, TemplateParameterNames(Declaration));
					}
					for (const auto& Method : Declaration.Methods)
					{
						if (InBody(Method.Body.get()) && Method.Body->CallableOwner == Use.ContextFunction)
						{
							CurrentModulePath = Declaration.ModulePath;
							CurrentAccessFunctionLinkage = MakeMethodFunctionName(CurrentOwnerTypeLinkage, Method.Name,
							    CanonicalMethodParamTypeNames(Method, Declaration.ModulePath, {}), Method.bIsConst);
						}
					}
				}
			};
			FindOwner(Program.Classes);
			FindOwner(Program.Structs);
			FindOwner(Program.Interfaces);
			if (CurrentAccessFunctionLinkage.empty())
			{
				for (const auto& Function : Program.Functions)
				{
					if (InBody(Function.Body.get()) && Function.Body->CallableOwner == Use.ContextFunction)
					{
						CurrentModulePath = Function.ModulePath;
						CurrentAccessFunctionLinkage = MakeFreeFunctionLinkageName(Function);
					}
				}
			}
			return IsCurrentFunctionFriendOf(Info, Info.DeclaringOwnerLinkage);
		};
		std::vector<FScopedTypeUse> UnconditionalUses;
		for (const auto& Use : Program.ScopedTypeUses)
		{
			if (ConstantBranchState->Includes(Use))
			{
				UnconditionalUses.push_back(Use);
			}
		}
		if (!ValidateScopedTypeDeclarations(Program, ActiveImportIndex, ScopeError, FunctionTypeAccess, &UnconditionalUses,
		        [&](std::string_view Scope)
		{
			return ConstantBranchState->Includes(Scope);
		}))
		{
			ReportError(ScopeError.Message, {ScopeError.Location.FileLabel, ScopeError.Location.Line, ScopeError.Location.Column});
			return nullptr;
		}
		const auto ValidateDeferredDeclarations = [this, &Program](const auto& Ds)
		{
			for (const auto& D : Ds)
			{
				if (ConstantBranchState && !ConstantBranchState->Includes(D.Scope.ScopeId))
				{
					continue;
				}
				if (D.bPrecompiledPackage)
				{
					continue;
				}

				for (const auto& M : D.Methods)
				{
					if (!TemplateParameterNames(M).empty() && (M.bVirtual || M.bOverride || M.bFinal || M.bPureVirtual || M.Name.rfind("operator", 0) == 0))
					{
						ReportError("method templates cannot occupy virtual slots or declare member operators", M.Location);
						return false;
					}
					if (!M.Body && !M.bDeleted && !M.bPureVirtual)
					{
						ReportError(
						    "method declared but not defined (use '= 0;' for an explicitly pure virtual declaration): " +
						        M.Name,
						    M.Location);
						return false;
					}
				}

				// Unused template parameters still distinguish concrete instances.
			}
			return true;
		};
		if (!ValidateDeferredDeclarations(Program.Structs) || !ValidateDeferredDeclarations(Program.Classes) || !ValidateDeferredDeclarations(Program.Interfaces)) return nullptr;
		if (!RegisterBuiltinTemplates(Program))
		{
			return nullptr;
		}
		std::vector<const FStructDecl*> AllStructDecls;
		AllStructDecls.reserve(Program.Structs.size() + BuiltinStructTemplates.size());
		for (const FStructDecl& Decl : Program.Structs)
		{
			if (ConstantBranchState && !ConstantBranchState->Includes(Decl.Scope.ScopeId))
			{
				continue;
			}
			AllStructDecls.push_back(&Decl);
		}
		for (const FStructDecl& Decl : BuiltinStructTemplates)
		{
			AllStructDecls.push_back(&Decl);
		}
		const auto ValidateOpenGenericDefinitions = [this](const auto& Declarations) -> bool
		{
			for (const auto& Decl : Declarations)
			{
				if (ConstantBranchState && !ConstantBranchState->Includes(Decl.Scope.ScopeId))
				{
					continue;
				}
				if (TemplateParameterNames(Decl).empty() || Decl.bPrecompiledPackage)
				{
					continue;
				}
				for (const FConstructorDecl& Constructor : Decl.Constructors)
				{
					if (!Constructor.bDeleted && !Constructor.Body)
					{
						ReportError(
						    "constructor declared but not defined in the same file",
						    Constructor.Location);
						return false;
					}
				}
				for (const FMethodDecl& Method : Decl.Methods)
				{
					if (!Method.bDeleted && !Method.bPureVirtual && !Method.Body)
					{
						ReportError(
						    "method declared but not defined in the same file: " + Method.Name,
						    Method.Location);
						return false;
					}
				}
				if (Decl.Destructor && !Decl.Destructor->Body)
				{
					ReportError(
					    "destructor declared but not defined in the same file",
					    Decl.Destructor->Location);
					return false;
				}
			}
			return true;
		};
		if (!ValidateOpenGenericDefinitions(Program.Structs) || !ValidateOpenGenericDefinitions(Program.Classes))
		{
			return nullptr;
		}
		if (!ValidateUsingNamespaceDirectives())
		{
			return nullptr;
		}
		if (!Program.PrecompiledPackageModules.empty())
		{
			std::vector<const FBytecodeModule*> Dependencies;
			Dependencies.reserve(Program.PrecompiledPackageModules.size());
			FBytecodeModule EmptyRoot;
			EmptyRoot.EntryModulePath = Program.EntryModulePath;
			EmptyRoot.LifetimeAnchors = Program.LifetimeAnchors;
			EmptyRoot.RequiredExternalDependencies = Program.RequiredExternalDependencies;
			EmptyRoot.LifetimeAnchors.reserve(
			    EmptyRoot.LifetimeAnchors.size() + Program.PrecompiledPackageModules.size());
			for (const std::shared_ptr<const FBytecodeModule>& Dependency :
			    Program.PrecompiledPackageModules)
			{
				Dependencies.push_back(Dependency.get());
				// Open generic declarations parsed from a package point into the
				// package module's retained template strings. Keep that owner alive in
				// the compiled result as well as linking its bytecode definitions.
				EmptyRoot.LifetimeAnchors.push_back(Dependency);
			}
			std::unique_ptr<FBytecodeModule> SeededModule;
			std::string LinkError;
			if (!LinkBytecodeModules(
			        EmptyRoot,
			        Dependencies,
			        SeededModule,
			        LinkError) ||
			    !SeededModule)
			{
				ReportError("package link failed: " + LinkError);
				ActiveProgram = nullptr;
				Module.reset();
				return nullptr;
			}
			Module = std::move(SeededModule);
			if (Program.SourceMetadata)
			{
				auto Metadata = Module->SourceMetadata
				    ? std::make_shared<FSourceMetadataSnapshot>(*Module->SourceMetadata)
				    : std::make_shared<FSourceMetadataSnapshot>();
				// Package-owned metadata is frozen. Reparsed declaration facades can
				// share its IDs with different fields; retain the package records and
				// append only the newly collected source records.
				const auto AppendSourceRecords = [](auto& Into, const auto& From)
				{
					std::unordered_set<std::string> Ids;
					Ids.reserve(Into.size() + From.size());
					for (const auto& Record : Into)
					{
						Ids.insert(Record.Id);
					}
					for (const auto& Record : From)
					{
						if (Ids.insert(Record.Id).second)
						{
							Into.push_back(Record);
						}
					}
				};
				AppendSourceRecords(Metadata->Documents, Program.SourceMetadata->Documents);
				AppendSourceRecords(Metadata->Nodes, Program.SourceMetadata->Nodes);
				AppendSourceRecords(Metadata->Definitions, Program.SourceMetadata->Definitions);
				Metadata->Retention = Program.SourceMetadata->Retention;
				Module->SourceMetadata = std::move(Metadata);
			}
			Module->PackageInterfaceSource.clear();
			Module->PackageInterfaceError.clear();
			Module->bContainsPackageDependencies = true;

			// Every standalone module synthesizes the non-generic reflection value
			// types. A package-seeded module already owns their validated bytecode;
			// keep the declarations for semantic lookup, but do not emit a second
			// constructor/method set into the same function table.
			for (FStructDecl& Builtin : BuiltinStructTemplates)
			{
				if (!TemplateParameterNames(Builtin).empty())
				{
					continue;
				}
				const std::string LinkageName = GetSymbolLinkageName(
				    Builtin.ModulePath, Builtin.bExported, Builtin.Name);
				bool bAlreadyLinked = std::any_of(
				    Module->Types.begin(),
				    Module->Types.end(),
				    [&](const FBytecodeTypeDesc& Type)
				{
					return Type.Name == LinkageName;
				});
				if (!bAlreadyLinked)
				{
					bAlreadyLinked = std::any_of(
					    Program.PrecompiledPackageModules.begin(),
					    Program.PrecompiledPackageModules.end(),
					    [&](const std::shared_ptr<const FBytecodeModule>& Dependency)
					{
						return Dependency && std::any_of(Dependency->Types.begin(), Dependency->Types.end(), [&](const FBytecodeTypeDesc& Type)
						{
							return Type.Name == LinkageName;
						});
					});
				}
				Builtin.bPrecompiledPackage = bAlreadyLinked;
			}
		}

		CollectTemplateDeclarationMetadata(Program);
		const auto CheckConstexprDeclaration = [&](const auto& Declaration, const std::string& Owner, const std::string& ModulePath, bool Generic)
		{
			if (!Declaration.bConstexpr) return true;
			const auto KnownUnsupported = [](const std::string& Type)
			{
				return Type == "void" || Type == "float" || Type == "double" || Type == "string" || Type == "wstring" || Type == "bigint" || Type == "rational" || Type == "variant" || Runtime::IsReferenceTypeName(Type);
			};
			if ((Owner.empty() && Declaration.Name == "main") || Declaration.bDeleted || KnownUnsupported(Declaration.ReturnType) ||
			    std::any_of(Declaration.Parameters.begin(), Declaration.Parameters.end(), [&](const auto& Parameter)
			{
				return Parameter.bIsRef || Parameter.bIsVariadic || KnownUnsupported(Parameter.TypeName);
			}))
			{
				ReportError("invalid constexpr function signature or specifiers", Declaration.Location);
				return false;
			}
			if (!Generic || !Declaration.Body) return true;
			const auto PreviousNamespace = CurrentNamespacePrefix;
			struct FRestore
			{
				std::string& Value;
				const std::string& Previous;
				~FRestore()
				{
					Value = Previous;
				}
			} Restore{CurrentNamespacePrefix, PreviousNamespace};
			const auto Dot = Declaration.Name.rfind('.');
			CurrentNamespacePrefix = Owner.empty() ? (Dot == std::string::npos ? std::string{} : Declaration.Name.substr(0, Dot)) : Owner;
			const auto Syntax = CheckConstantFunctionSyntax(*Declaration.Body, [&](const FCallExpr& Call)
			{
				if (!Owner.empty() || Call.Callee.find('.') != std::string::npos) return true;
				const auto Names = BuildFreeFunctionLookupNames(Call.Callee, ModulePath);
				return std::any_of(Program.Functions.begin(), Program.Functions.end(), [&](const auto& Function)
				{
					return Function.bConstexpr && std::find(Names.begin(), Names.end(), Function.Name) != Names.end() && CanAccessSymbol(Function.ModulePath, Function.bExported, ModulePath);
				});
			});
			if (!Syntax.Succeeded())
			{
				ReportError("invalid constexpr function: " + Syntax.Reason, Syntax.Location);
				return false;
			}
			return true;
		};
		for (const auto& Function : Program.Functions)
		{
			if (!CheckConstexprDeclaration(Function, {}, Function.ModulePath, !TemplateParameterNames(Function).empty())) return nullptr;
		}
		const auto CheckConstexprMethods = [&](const auto& Declarations)
		{
			for (const auto& Type : Declarations)
			{
				for (const auto& Method : Type.Methods)
				{
					if (ConstantBranchState && !ConstantBranchState->Includes(Type.Scope.ScopeId))
					{
						continue;
					}
					if (Method.bConstexpr && (!Method.bIsStatic || Method.bVirtual || Method.bOverride || Method.bFinal || Method.bDefaulted || Method.bPureVirtual))
					{
						ReportError("constexpr requires an ordinary static method", Method.Location);
						return false;
					}
					if (!CheckConstexprDeclaration(Method, Type.Name, Type.ModulePath, !TemplateParameterNames(Type).empty() || !TemplateParameterNames(Method).empty())) return false;
				}
			}
			return true;
		};
		if (!CheckConstexprMethods(Program.Structs) || !CheckConstexprMethods(Program.Classes) || !CheckConstexprMethods(Program.Interfaces)) return nullptr;

		std::unordered_set<std::string> ModuleSymbolKeys;
		std::unordered_set<std::string> ExportedSimpleNames;
		std::unordered_set<std::string> FunctionModuleKeys;
		std::unordered_set<std::string> ExportedFunctionNames;
		std::unordered_set<std::string> FunctionSignatureKeys;
		std::unordered_set<std::string> ExportedFunctionSignatureKeys;
		const auto MakeArityKey = [](const std::string& SimpleName, const std::size_t Arity) -> std::string
		{
			return SimpleName + '\x1E' + std::to_string(Arity);
		};
		const auto RegisterTopLevelSymbol = [&](const std::string& ModulePath, const bool bExported,
		                                        const std::string& SimpleName, const FSourceLocation& Loc,
		                                        const std::size_t TypeParamArity = 0) -> bool
		{
			if (StandardLibrary::GetBindingCatalog().FindType(SimpleName))
			{
				ReportErrorId("diag.stdlib_reserved_binding", Loc);
				return false;
			}
			const std::string ArityKey = MakeArityKey(SimpleName, TypeParamArity);
			const std::string ModuleKey = ModulePath.empty() ? ArityKey : ModulePath + '\x1F' + ArityKey;
			if (!ModuleSymbolKeys.insert(ModuleKey).second)
			{
				ReportErrorId("diag.duplicate_symbol_in_module", Loc, {SimpleName});
				return false;
			}
			if (bExported && !ExportedSimpleNames.insert(ArityKey).second)
			{
				ReportErrorId("diag.duplicate_exported_symbol", Loc, {SimpleName});
				return false;
			}
			return true;
		};

		for (const FEnumDecl& EnumDecl : Program.Enums)
		{
			if (ConstantBranchState && !ConstantBranchState->Includes(EnumDecl.Scope.ScopeId))
			{
				continue;
			}
			if (!RegisterTopLevelSymbol(EnumDecl.ModulePath, EnumDecl.bExported, EnumDecl.Name, EnumDecl.Location,
			        EnclosingTemplateParameterNames(EnumDecl.Scope).size()))
			{
				return nullptr;
			}
			std::unordered_map<std::string, std::int64_t> MemberValues;
			if (EnclosingTemplateParameterNames(EnumDecl.Scope).empty())
			{
				for (const auto& Member : EnumDecl.Members)
				{
					FIdentifierExpr Reference(EnumDecl.Name + "." + Member.Name);
					Reference.Location = Member.Location;
					const auto Value = EvaluateTemplateExpression(Reference, EnumDecl.ModulePath, {}, EnumDecl.Name, false);
					if (!Value.Succeeded())
					{
						ReportError("invalid enum constant: " + Value.Reason, Member.Location);
						return nullptr;
					}
					auto Bits = Value.Value.Bits;
					const auto& Type = Value.Value.Type;
					if (!Type.bUnsigned && Type.Width < 64 && (Bits & (std::uint64_t{1} << (Type.Width - 1))))
					{
						Bits |= ~((std::uint64_t{1} << Type.Width) - 1);
					}
					MemberValues[Member.Name] = DecodeConstantSignedBits(Bits);
					EnumConstants[Value.Value.Type.Name + "." + Member.Name] = DecodeConstantSignedBits(Bits);
				}
			}
			if (!EnumDecl.bScoped)
			{
				for (const auto& Member : EnumDecl.Members)
				{
					const auto Key = GetSymbolLinkageName(EnumDecl.ModulePath, EnumDecl.bExported, EnumDecl.Name + "." + Member.Name);
					EnumConstants.emplace(Key, MemberValues.count(Member.Name) ? MemberValues.at(Member.Name) : Member.Value);
					EnumConstantTypeNames.emplace(Key, EnumDecl.UnderlyingTypeName);
				}
			}
			if (!EnclosingTemplateParameterNames(EnumDecl.Scope).empty())
			{
				continue;
			}
			const std::string EnumLinkageName =
			    GetSymbolLinkageName(EnumDecl.ModulePath, EnumDecl.bExported, EnumDecl.Name);
			EnumTypeNames.insert(EnumLinkageName);
			EnumTypeNames.insert(EnumDecl.Name);
			// Plain `enum` injects bare member names into module scope; `enum class` is scoped
			// and only resolves via `EnumName.Member` / `EnumName::Member`.
			if (!EnumDecl.bScoped)
			{
				for (const FEnumMemberDecl& Member : EnumDecl.Members)
				{
					std::string InjectedName = Member.Name;
					if (const std::size_t Dot = EnumDecl.Name.rfind('.'); Dot != std::string::npos)
					{
						InjectedName = (EnumDecl.Scope.OwnerTypeName.empty() ? EnumDecl.Name.substr(0, Dot + 1)
						                                                     : EnumDecl.Scope.OwnerTypeName + ".") +
						    Member.Name;
					}
					if (!RegisterTopLevelSymbol(
					        EnumDecl.ModulePath,
					        EnumDecl.bExported,
					        InjectedName,
					        Member.Location))
					{
						return nullptr;
					}
					const std::string MemberLinkageName =
					    GetSymbolLinkageName(
					        EnumDecl.ModulePath, EnumDecl.bExported, InjectedName);
					EnumConstants.emplace(MemberLinkageName, MemberValues.at(Member.Name));
					EnumConstantTypeNames.emplace(
					    MemberLinkageName, EnumDecl.UnderlyingTypeName);
				}
			}
			if (EnumDecl.bPrecompiledPackage)
			{
				continue;
			}
			FBytecodeEnumDesc EnumDesc;
			EnumDesc.Scope = EnumDecl.Scope;
			std::vector<FNestedTypeDeclaration> EnumChildren;
			PopulateSourceTypeScope(EnumDecl.Scope, {}, EnumDesc.Scope, EnumChildren);
			EnumDesc.Name = EnumLinkageName;
			EnumDesc.bScoped = EnumDecl.bScoped;
			EnumDesc.UnderlyingTypeName = EnumDecl.UnderlyingTypeName;
			EnumDesc.DeclMeta = EnumDecl.DeclMeta;
			EnumDesc.Members.reserve(EnumDecl.Members.size());
			for (const FEnumMemberDecl& Member : EnumDecl.Members)
			{
				FBytecodeEnumMemberDesc MemberDesc;
				MemberDesc.Name = Member.Name;
				MemberDesc.Value = MemberValues.at(Member.Name);
				MemberDesc.DeclMeta = Member.DeclMeta;
				EnumDesc.Members.push_back(std::move(MemberDesc));
			}
			Module->Enums.push_back(std::move(EnumDesc));
		}

		if (ActiveBindings)
		{
			for (const FHostTypeDesc& HostType : ActiveBindings->GetTypes())
			{
				if (HostType.Kind != EHostTypeKind::Enum || HostType.Name.empty())
				{
					continue;
				}
				EnumTypeNames.insert(HostType.Name);
				// A VM executing serialized bytecode has no live binding catalog. Preserve
				// strong host-enum identity in module metadata so checked casts can keep
				// producing FEnumValue rather than degrading to a bare integer.
				const bool bStrongHostEnum = HostType.bScopedEnum || std::any_of(HostType.Members.begin(), HostType.Members.end(), [&HostType](const FHostMemberDesc& Member)
				{
					return Member.Kind == EHostMemberKind::EnumMember && Member.ReturnHostTypeName == HostType.Name;
				});
				if (bStrongHostEnum)
				{
					FBytecodeEnumDesc EnumDesc;
					EnumDesc.Name = HostType.Name;
					EnumDesc.bScoped = true;
					EnumDesc.UnderlyingTypeName = HostType.EnumUnderlyingTypeName;
					for (const FHostMemberDesc& HostMember : HostType.Members)
					{
						if (HostMember.Kind != EHostMemberKind::EnumMember || HostMember.Name.empty())
						{
							continue;
						}
						FBytecodeEnumMemberDesc MemberDesc;
						MemberDesc.Name = HostMember.Name;
						MemberDesc.Value = HostMember.EnumValue;
						EnumDesc.Members.push_back(std::move(MemberDesc));
					}
					const auto Existing = std::find_if(Module->Enums.begin(), Module->Enums.end(),
					    [&](const FBytecodeEnumDesc& Enum)
					{
						return Enum.Name == HostType.Name;
					});
					if (Existing != Module->Enums.end())
					{
						const bool Matches = Existing->bScoped == EnumDesc.bScoped && Existing->UnderlyingTypeName == EnumDesc.UnderlyingTypeName && Existing->Members.size() == EnumDesc.Members.size() && std::equal(Existing->Members.begin(), Existing->Members.end(), EnumDesc.Members.begin(), [](const auto& Left, const auto& Right)
						{
							return Left.Name == Right.Name && Left.Value == Right.Value;
						});
						if (!Matches)
						{
							if (StandardLibrary::GetBindingCatalog().FindType(HostType.Name))
							{
								ReportErrorId("diag.stdlib_reserved_binding");
							}
							else
							{
								ReportError("conflicting host enum definition: " + HostType.Name);
							}
							return nullptr;
						}
					}
					else
					{
						Module->Enums.push_back(std::move(EnumDesc));
					}
				}
				for (const FHostMemberDesc& HostMember : HostType.Members)
				{
					if (HostMember.Kind != EHostMemberKind::EnumMember || HostMember.Name.empty())
					{
						continue;
					}
					EnumConstants.emplace(
					    HostType.Name + "." + HostMember.Name,
					    HostMember.EnumValue);
				}
			}
		}

		UserTypeAliases.clear();
		for (const FTypeAliasDecl& AliasDecl : Program.TypeAliases)
		{
			if (ConstantBranchState && !ConstantBranchState->Includes(AliasDecl.Scope.ScopeId))
			{
				continue;
			}
			if (!RegisterTopLevelSymbol(
			        AliasDecl.ModulePath,
			        AliasDecl.bExported,
			        AliasDecl.Name,
			        AliasDecl.Location))
			{
				return nullptr;
			}
			if (CompilerDetail::TypeNameMentionsAuto(AliasDecl.TypeName))
			{
				ReportErrorId("diag.auto_not_valid_type_here", AliasDecl.Location);
				return nullptr;
			}
			FUserTypeAlias Info;
			Info.TargetTypeName = AliasDecl.TypeName;
			Info.ModulePath = AliasDecl.ModulePath;
			Info.EnclosingParameterNames = EnclosingTemplateParameterNames(AliasDecl.Scope);
			Info.bExported = AliasDecl.bExported;
			UserTypeAliases.emplace(AliasDecl.Name, std::move(Info));
		}

		// Static callable declarations do not need an instance layout. Make them
		// available before ordinary field defaults compile their calls.
		const auto RegisterStatics = [&](const auto& Declarations)
		{
			for (const auto& Declaration : Declarations)
			{
				CheckAnalysisCancellation();
				if (!TemplateParameterNames(Declaration).empty() || Declaration.bPrecompiledPackage ||
				    (ConstantBranchState && !ConstantBranchState->Includes(Declaration.Scope.ScopeId)))
				{
					continue;
				}
				if (!RegisterStaticMethodDeclarations(GetSymbolLinkageName(Declaration.ModulePath,
				                                          Declaration.bExported, Declaration.Name),
				        Declaration.ModulePath, Declaration.Methods))
				{
					return false;
				}
			}
			return true;
		};
		if (!RegisterStatics(Program.Structs) || !RegisterStatics(Program.Classes) ||
		    !RegisterStatics(Program.Interfaces) || !RegisterStatics(BuiltinStructTemplates) ||
		    !RegisterStatics(BuiltinClassTemplates))
		{
			return nullptr;
		}

		// Explicit global declaration types are available to field initializers.
		// Reserve storage and type identity before layouts bind their expressions;
		// initialization, auto deduction and metadata still use the normal pass.
		for (const auto& Statement : Program.TopLevelStatements)
		{
			if (!Statement || Statement->Kind != EStmtKind::VarDecl)
			{
				continue;
			}
			const auto& Variable = static_cast<const FVarDeclStmt&>(*Statement);
			const auto Declared = NormalizeTypeAlias(Variable.TypeName, Variable.ModulePath);
			if (Declared == "auto")
			{
				continue;
			}
			const auto Name = GetSymbolLinkageName(Variable.ModulePath, Variable.bExported, Variable.Name);
			const auto Slot = AddGlobal(Name);
			if (HasError()) return nullptr;
			const auto Type = QualifyTypeName(Declared, Variable.ModulePath);
			GlobalVarTypes.emplace(Name, Type);
			Module->GlobalTypeNames[Slot] = Type;
			if (Variable.bIsConst)
			{
				GlobalVarConsts.insert(Name);
			}
		}

		const auto ValidateConversions = [&](const auto& Declarations)
		{
			for (const auto& Decl : Declarations)
			{
				if (!ValidateConversionMethods(Decl.Methods, Decl.ModulePath, {})) return false;
			}

			return true;
		};
		if (!ValidateConversions(Program.Structs) || !ValidateConversions(Program.Classes) ||
		    !ValidateConversions(Program.Interfaces))
		{
			return nullptr;
		}

		for (const FInterfaceDecl& InterfaceDecl : Program.Interfaces)
		{
			if (ConstantBranchState && !ConstantBranchState->Includes(InterfaceDecl.Scope.ScopeId))
			{
				continue;
			}
			if (!RegisterTopLevelSymbol(
			        InterfaceDecl.ModulePath,
			        InterfaceDecl.bExported,
			        InterfaceDecl.Name,
			        InterfaceDecl.Location,
			        TemplateParameterNames(InterfaceDecl).size()))
			{
				return nullptr;
			}
		}
		{
			std::unordered_map<std::string, std::uint16_t> InterfaceIndexByLinkage;
			for (std::size_t Index = 0; Index < Module->Interfaces.size(); ++Index)
			{
				InterfaceIndexByLinkage.emplace(
				    Module->Interfaces[Index].Name,
				    static_cast<std::uint16_t>(Index));
			}
			for (const FInterfaceDecl& InterfaceDecl : Program.Interfaces)
			{
				if (ConstantBranchState && !ConstantBranchState->Includes(InterfaceDecl.Scope.ScopeId))
				{
					continue;
				}
				if (!TemplateParameterNames(InterfaceDecl).empty())
				{
					continue;
				}
				if (InterfaceDecl.bPrecompiledPackage)
				{
					const std::string Linkage = GetSymbolLinkageName(
					    InterfaceDecl.ModulePath,
					    InterfaceDecl.bExported,
					    InterfaceDecl.Name);
					if (InterfaceIndexByLinkage.find(Linkage) == InterfaceIndexByLinkage.end())
					{
						ReportError(
						    "compiled package is missing interface definition: " + InterfaceDecl.Name,
						    InterfaceDecl.Location);
						return nullptr;
					}
					continue;
				}
				FBytecodeInterfaceDesc Desc;
				Desc.Name = GetSymbolLinkageName(
				    InterfaceDecl.ModulePath,
				    InterfaceDecl.bExported,
				    InterfaceDecl.Name);
				Desc.DeclMeta = InterfaceDecl.DeclMeta;
				PopulateSourceTypeScope(InterfaceDecl.Scope, {}, Desc.Scope, Desc.NestedTypes);
				const std::string PreviousModulePath = CurrentModulePath;
				CurrentModulePath = InterfaceDecl.ModulePath;
				for (const FFieldDecl& Field : InterfaceDecl.Fields)
				{
					// Static interface data members are shared globals, not layout state —
					// keep DeclMeta on StaticFields for embed reflection (mirrors Types).
					if (Field.bIsStatic)
					{
						FBytecodeFieldDesc StaticDesc;
						StaticDesc.Name = Field.Name;
						StaticDesc.DeclaredTypeName = QualifyTypeName(
						    Field.TypeName, InterfaceDecl.ModulePath);
						StaticDesc.TypeName = QualifyTypeName(
						    NormalizeTypeAlias(
						        StaticDesc.DeclaredTypeName, InterfaceDecl.ModulePath),
						    InterfaceDecl.ModulePath);
						StaticDesc.bIsConst = Field.bIsConst;
						StaticDesc.bConstexpr = Field.bConstexpr;
						StaticDesc.Access = Field.Access;
						StaticDesc.DeclMeta = Field.DeclMeta;
						Desc.StaticFields.push_back(std::move(StaticDesc));
						continue;
					}
					FBytecodeFieldDesc FieldDesc;
					FieldDesc.Name = Field.Name;
					FieldDesc.DeclaredTypeName = QualifyTypeName(
					    Field.TypeName, InterfaceDecl.ModulePath);
					FieldDesc.TypeName = QualifyTypeName(
					    NormalizeTypeAlias(
					        FieldDesc.DeclaredTypeName, InterfaceDecl.ModulePath),
					    InterfaceDecl.ModulePath);
					if (!CompileFieldDefault(Field, FieldDesc))
					{
						CurrentModulePath = PreviousModulePath;
						return nullptr;
					}
					FieldDesc.DeclMeta = Field.DeclMeta;
					Desc.Fields.push_back(std::move(FieldDesc));
				}
				CurrentModulePath = PreviousModulePath;
				InterfaceIndexByLinkage.emplace(
				    Desc.Name,
				    static_cast<std::uint16_t>(Module->Interfaces.size()));
				Module->Interfaces.push_back(std::move(Desc));
			}
			for (const FInterfaceDecl& InterfaceDecl : Program.Interfaces)
			{
				if (ConstantBranchState && !ConstantBranchState->Includes(InterfaceDecl.Scope.ScopeId))
				{
					continue;
				}
				if (!TemplateParameterNames(InterfaceDecl).empty() || InterfaceDecl.BaseInterfaceNames.empty())
				{
					continue;
				}
				const std::string ChildLinkage = GetSymbolLinkageName(
				    InterfaceDecl.ModulePath,
				    InterfaceDecl.bExported,
				    InterfaceDecl.Name);
				const auto ChildIt = InterfaceIndexByLinkage.find(ChildLinkage);
				if (ChildIt == InterfaceIndexByLinkage.end())
				{
					continue;
				}
				for (const std::string& ParentAsWritten : InterfaceDecl.BaseInterfaceNames)
				{
					const std::string ParentResolved = ExpandUserTypeAlias(
					    ParentAsWritten,
					    InterfaceDecl.ModulePath);
					std::string ParentBase;
					std::vector<std::string> ParentArgs;
					const bool bParentApp = SplitUserGenericTypeApp(
					    ParentResolved,
					    ParentBase,
					    ParentArgs);
					if (bParentApp)
					{
						// Defer Instantiation until types are available (after struct/class registration).
						continue;
					}
					const std::string BaseLinkage = ResolveTypeLinkageName(
					    ParentResolved,
					    InterfaceDecl.ModulePath);
					const auto BaseIt = InterfaceIndexByLinkage.find(BaseLinkage);
					if (BaseIt == InterfaceIndexByLinkage.end())
					{
						const FInterfaceDecl* const ParentDecl =
						    FindGenericInterfaceDecl(ParentResolved, InterfaceDecl.ModulePath, 0);
						if (ParentDecl && !TemplateParameterNames(*ParentDecl).empty())
						{
							ReportErrorId(
							    "diag.generic_interface_requires_type_arguments",
							    InterfaceDecl.Location,
							    {ParentAsWritten});
							return nullptr;
						}
						ReportErrorId(
						    "diag.unknown_parent_interface",
						    InterfaceDecl.Location,
						    {ParentAsWritten});
						return nullptr;
					}
					if (const FInterfaceDecl* const ParentDecl =
					        FindInterfaceDeclByLinkage(BaseLinkage);
					    ParentDecl && ParentDecl->bFinal)
					{
						ReportErrorId(
						    "diag.cannot_extend_final_interface",
						    InterfaceDecl.Location,
						    {ParentAsWritten});
						return nullptr;
					}
					Module->Interfaces[ChildIt->second].BaseInterfaceIndices.push_back(BaseIt->second);
				}
			}
			for (const FInterfaceDecl& InterfaceDecl : Program.Interfaces)
			{
				if (ConstantBranchState && !ConstantBranchState->Includes(InterfaceDecl.Scope.ScopeId))
				{
					continue;
				}
				if (TemplateParameterNames(InterfaceDecl).empty() || InterfaceDecl.BaseInterfaceNames.empty())
				{
					continue;
				}
				for (const std::string& ParentAsWritten : InterfaceDecl.BaseInterfaceNames)
				{
					const std::string ParentResolved = ExpandUserTypeAlias(
					    ParentAsWritten,
					    InterfaceDecl.ModulePath);
					std::string ParentBase;
					std::vector<std::string> ParentArgs;
					const bool bParentApp = SplitUserGenericTypeApp(
					    ParentResolved,
					    ParentBase,
					    ParentArgs);
					const std::string ParentSimple =
					    bParentApp ? ParentBase : ParentResolved;
					const FInterfaceDecl* const ParentDecl =
					    FindGenericInterfaceDecl(
					        ParentSimple,
					        InterfaceDecl.ModulePath,
					        bParentApp ? ParentArgs.size() : 0);
					if (!ParentDecl)
					{
						ReportErrorId(
						    "diag.unknown_parent_interface",
						    InterfaceDecl.Location,
						    {ParentAsWritten});
						return nullptr;
					}
					if (ParentDecl->bFinal)
					{
						ReportErrorId(
						    "diag.cannot_extend_final_interface",
						    InterfaceDecl.Location,
						    {ParentAsWritten});
						return nullptr;
					}
					if (!TemplateParameterNames(*ParentDecl).empty())
					{
						if (!bParentApp || ParentArgs.empty())
						{
							ReportErrorId(
							    "diag.generic_interface_requires_type_arguments",
							    InterfaceDecl.Location,
							    {ParentSimple});
							return nullptr;
						}
					}
					else if (bParentApp)
					{
						ReportErrorId(
						    "diag.type_is_not_generic",
						    InterfaceDecl.Location,
						    {ParentSimple});
						return nullptr;
					}
				}
			}
			if (!CheckInterfaceInheritanceCycles())
			{
				return nullptr;
			}
			for (const FInterfaceDecl& InterfaceDecl : Program.Interfaces)
			{
				if (ConstantBranchState && !ConstantBranchState->Includes(InterfaceDecl.Scope.ScopeId))
				{
					continue;
				}
				if (!TemplateParameterNames(InterfaceDecl).empty())
				{
					continue;
				}
				const std::string IfaceLinkage = GetSymbolLinkageName(
				    InterfaceDecl.ModulePath,
				    InterfaceDecl.bExported,
				    InterfaceDecl.Name);
				for (const FMethodDecl& Method : InterfaceDecl.Methods)
				{
					if (!TemplateParameterNames(Method).empty())
					{
						continue;
					}
					const std::string MethodKey = MakeMethodFunctionName(
					    IfaceLinkage,
					    Method.Name,
					    CanonicalMethodParamTypeNames(
					        Method,
					        InterfaceDecl.ModulePath,
					        {}),
					    Method.bIsConst);
					if (Method.bPureVirtual)
					{
						// Abstract interface method: stub carries DeclMeta for Enumerate*.
						if (!EnsureFunctionStub(
						        MethodKey,
						        Method.ReturnType,
						        static_cast<std::uint8_t>(Method.Parameters.size()),
						        !Method.bIsStatic,
						        Method.bIsStatic ? 0 : 0xFFFF,
						        Method.bIsStatic ? false : Method.bVirtual,
						        Method.Location,
						        Method.bIsConst))
						{
							return nullptr;
						}
						const auto StubIt = Module->FunctionIndices.find(MethodKey);
						if (StubIt != Module->FunctionIndices.end() && StubIt->second < Module->Functions.size())
						{
							FBytecodeFunction& Stub =
							    Module->Functions[StubIt->second];
							SetFunctionDeclarationSignatureMetadata(
							    Stub,
							    Method.ReturnType,
							    Method.Parameters,
							    InterfaceDecl.ModulePath);
							Stub.DeclMeta = Method.DeclMeta;
							Stub.Access = Method.Access;
							Stub.bIsAbstract = true;
						}
						continue;
					}
					if (!EnsureMethodDeclarationStub(
					        MethodKey, Method, 0xFFFF, Method.bVirtual, InterfaceDecl.ModulePath))
					{
						return nullptr;
					}
				}
			}
		}

		for (const FStructDecl* const StructDeclPtr : AllStructDecls)
		{
			const FStructDecl& StructDecl = *StructDeclPtr;
			if (!RegisterTopLevelSymbol(
			        StructDecl.ModulePath,
			        StructDecl.bExported,
			        StructDecl.Name,
			        StructDecl.Location,
			        TemplateParameterNames(StructDecl).size()))
			{
				return nullptr;
			}
			if (!TemplateParameterNames(StructDecl).empty())
			{
				continue;
			}
			const std::string LinkageName =
			    GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name);
			if (FindOrAddType(LinkageName) == static_cast<std::uint16_t>(-1))
			{
				return nullptr;
			}
		}
		for (const FClassDecl& ClassDecl : Program.Classes)
		{
			if (ConstantBranchState && !ConstantBranchState->Includes(ClassDecl.Scope.ScopeId))
			{
				continue;
			}
			if (!RegisterTopLevelSymbol(
			        ClassDecl.ModulePath,
			        ClassDecl.bExported,
			        ClassDecl.Name,
			        ClassDecl.Location,
			        TemplateParameterNames(ClassDecl).size()))
			{
				return nullptr;
			}
			if (!TemplateParameterNames(ClassDecl).empty())
			{
				continue;
			}
			const std::string LinkageName =
			    GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name);
			if (FindOrAddType(LinkageName) == static_cast<std::uint16_t>(-1))
			{
				return nullptr;
			}
		}

		for (const std::unique_ptr<FStmt>& Statement : Program.TopLevelStatements)
		{
			if (Statement->Kind == EStmtKind::VarDecl)
			{
				const FVarDeclStmt& VarDecl = static_cast<const FVarDeclStmt&>(*Statement);
				if (!RegisterTopLevelSymbol(VarDecl.ModulePath, VarDecl.bExported, VarDecl.Name, Statement->Location))
				{
					return nullptr;
				}
				const std::string LinkageName = VarDecl.ModulePath.empty()
				    ? VarDecl.Name
				    : GetSymbolLinkageName(VarDecl.ModulePath, VarDecl.bExported, VarDecl.Name);
				const std::uint16_t GlobalSlot = AddGlobal(LinkageName);
				std::string StoredTypeName = NormalizeTypeAlias(VarDecl.TypeName);
				if (StoredTypeName == "auto" && VarDecl.Initializer)
				{
					TryResolveExprTypeName(*VarDecl.Initializer, StoredTypeName);
					StoredTypeName = CanonicalTypeNameForCoercion(StoredTypeName);
				}
				else
				{
					StoredTypeName = QualifyTypeName(StoredTypeName, VarDecl.ModulePath.empty() ? CurrentModulePath : VarDecl.ModulePath);
				}
				GlobalVarTypes.emplace(LinkageName, StoredTypeName);
				if (GlobalSlot < Module->GlobalTypeNames.size())
				{
					Module->GlobalTypeNames[GlobalSlot] = StoredTypeName;
				}
				if (GlobalSlot < Module->GlobalMetadata.size())
				{
					FBytecodeGlobalMeta& Meta = Module->GlobalMetadata[GlobalSlot];
					Meta.ModulePath = VarDecl.ModulePath;
					Meta.DeclaredTypeName = VarDecl.TypeName == "auto"
					    ? StoredTypeName
					    : QualifyTypeName(VarDecl.TypeName, VarDecl.ModulePath);
					Meta.bConst = VarDecl.bIsConst;
					Meta.bConstexpr = VarDecl.bConstexpr;
					Meta.bExported = VarDecl.bExported;
					Meta.bStaticMember = false;
					Meta.DeclMeta = VarDecl.DeclMeta;
				}
				if (VarDecl.bIsConst)
				{
					GlobalVarConsts.insert(LinkageName);
				}
			}
		}

		// C++-style static data members are shared globals keyed as `TypeLinkage.Field`.
		// Register them here (before bodies compile) so `Type.Field` resolves in any body.
		if (!RegisterStaticFieldGlobals())
		{
			return nullptr;
		}

		if (!CheckScopedTypeRequirements()) return nullptr;

		for (const FFunctionDecl& Function : Program.Functions)
		{
			const std::string ModuleKey = Function.ModulePath.empty()
			    ? Function.Name
			    : Function.ModulePath + '\x1F' + Function.Name;
			std::string SignatureKey = ModuleKey + '\x1E';
			if (!TemplateParameterNames(Function).empty())
			{
				SignatureKey += "#generic";
				SignatureKey += std::to_string(TemplateParameterNames(Function).size());
				// Generic arity alone is not an overload signature. Normalize parameter
				// names by position so alpha-equivalent declarations still conflict.
				std::unordered_map<std::string, std::string> GenericParameters;
				for (std::size_t Index = 0; Index < TemplateParameterNames(Function).size(); ++Index)
				{
					GenericParameters.emplace(TemplateParameterNames(Function)[Index],
					    "@type_parameter_" + std::to_string(Index));
				}
				for (const auto& Parameter : Function.TemplateParameters.GetParameters())
				{
					SignatureKey += Parameter.Kind == ETemplateParameterKind::Type ? ":T" : ":V";
					if (Parameter.Kind == ETemplateParameterKind::Value)
					{
						const auto Domain = CanonicalTypeNameForCoercion(QualifyTypeName(
						    SubstituteTypeNameWith(Parameter.ValueTypeName, GenericParameters), Function.ModulePath));
						SignatureKey += std::to_string(Domain.size()) + ':' + Domain;
					}
				}
				SignatureKey += EncodeMethodParamSuffix(
				    CanonicalParamTypeNames(Function.Parameters, Function.ModulePath, GenericParameters));
			}
			else
			{
				SignatureKey += EncodeMethodParamSuffix(
				    CanonicalParamTypeNames(Function.Parameters, Function.ModulePath, {}));
			}
			if (!FunctionSignatureKeys.insert(SignatureKey).second)
			{
				ReportErrorId("diag.duplicate_function_with_same_signature", Function.Location,
				    {Function.Name});
				return nullptr;
			}
			if (Function.bExported)
			{
				const std::string ExportSigKey =
				    Function.Name + '\x1E' + SignatureKey.substr(ModuleKey.size() + 1);
				if (!ExportedFunctionSignatureKeys.insert(ExportSigKey).second)
				{
					ReportErrorId("diag.duplicate_exported_symbol", Function.Location, {Function.Name});
					return nullptr;
				}
			}
			if (FunctionModuleKeys.count(ModuleKey) > 0)
			{
				// Overload of an existing free function.
				if (Function.bExported)
				{
					ExportedSimpleNames.insert(Function.Name);
					ExportedFunctionNames.insert(Function.Name);
				}
				continue;
			}
			if (ModuleSymbolKeys.count(ModuleKey) > 0)
			{
				ReportErrorId("diag.duplicate_symbol_in_module", Function.Location, {Function.Name});
				return nullptr;
			}
			ModuleSymbolKeys.insert(ModuleKey);
			FunctionModuleKeys.insert(ModuleKey);
			if (Function.bExported)
			{
				if (ExportedSimpleNames.count(Function.Name) > 0 && ExportedFunctionNames.count(Function.Name) == 0)
				{
					ReportErrorId("diag.duplicate_exported_symbol", Function.Location, {Function.Name});
					return nullptr;
				}
				ExportedSimpleNames.insert(Function.Name);
				ExportedFunctionNames.insert(Function.Name);
			}
		}

		// Reserve function/method/ctor indices before compiling any bodies so Call /
		// CallBoundMethod resolve regardless of declaration order.
		for (const FFunctionDecl& Function : Program.Functions)
		{
			if (!Function.Body && !Function.bDeleted && !Function.bPrecompiledPackage &&
			    !IsMethodBindingFactoryDeclaration(Function))
			{
				ReportError("function declared but not defined: " + Function.Name, Function.Location);
				return nullptr;
			}
			const bool bEntryMain = Function.Name == "main" &&
			    (Program.EntryModulePath.empty() || Function.ModulePath == Program.EntryModulePath);
			if (bEntryMain && !TemplateParameterNames(Function).empty())
			{
				ReportErrorId("diag.main_cannot_be_generic", Function.Location);
				return nullptr;
			}
			if (!TemplateParameterNames(Function).empty())
			{
				continue;
			}
			const std::string FunctionLinkageName = MakeFreeFunctionLinkageName(Function);
			if (Function.bDeleted)
			{
				if (!EnsureDeletedFunctionDeclaration(
				        FunctionLinkageName,
				        Function.ReturnType,
				        Function.Parameters,
				        false,
				        0,
				        Function.ModulePath,
				        Function.Location,
				        false,
				        Function.DeclMeta))
				{
					return nullptr;
				}
				continue;
			}
			if (!EnsureFunctionStub(
			        FunctionLinkageName,
			        Function.ReturnType,
			        static_cast<std::uint8_t>(Function.Parameters.size()),
			        false,
			        0,
			        false,
			        Function.Location))
			{
				return nullptr;
			}
		}
		for (const FStructDecl* const StructDeclPtr : AllStructDecls)
		{
			const FStructDecl& StructDecl = *StructDeclPtr;
			if (StructDecl.bPrecompiledPackage)
			{
				continue;
			}
			const std::string TypeLinkageName =
			    GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name);
			std::unordered_set<std::string> ConstructorKeys;
			for (const FConstructorDecl& Constructor : StructDecl.Constructors)
			{
				const std::string CtorName = MakeConstructorKey(
				    TypeLinkageName,
				    StructDecl.Name,
				    Constructor,
				    StructDecl.ModulePath,
				    {});
				if (!ConstructorKeys.insert(CtorName).second || (TemplateParameterNames(StructDecl).empty() && Module->FunctionIndices.find(CtorName) != Module->FunctionIndices.end() && !Module->Functions[Module->FunctionIndices.at(CtorName)].Code.empty()))
				{
					ReportErrorId(
					    "diag.duplicate_constructor",
					    Constructor.Location,
					    {TypeNameLeaf(StructDecl.Name)});
					return nullptr;
				}
			}
			if (!TemplateParameterNames(StructDecl).empty())
			{
				continue;
			}
			const std::uint16_t TypeIndex = FindOrAddType(TypeLinkageName);
			if (TypeIndex == static_cast<std::uint16_t>(-1))
			{
				return nullptr;
			}
			for (const FConstructorDecl& Constructor : StructDecl.Constructors)
			{
				const std::string CtorName = MakeConstructorKey(
				    TypeLinkageName,
				    StructDecl.Name,
				    Constructor,
				    StructDecl.ModulePath,
				    {});
				if (Constructor.bDeleted)
				{
					if (!EnsureDeletedFunctionDeclaration(
					        CtorName,
					        "void",
					        Constructor.Parameters,
					        true,
					        TypeIndex,
					        StructDecl.ModulePath,
					        Constructor.Location,
					        false,
					        Constructor.DeclMeta))
					{
						return nullptr;
					}
					continue;
				}
				if (!EnsureFunctionStub(
				        CtorName,
				        "void",
				        static_cast<std::uint8_t>(Constructor.Parameters.size()),
				        true,
				        TypeIndex,
				        false,
				        Constructor.Location))
				{
					return nullptr;
				}
			}
			for (const FMethodDecl& Method : StructDecl.Methods)
			{
				if (!TemplateParameterNames(Method).empty())
				{
					continue;
				}
				if (ReportIfDeletedMethodOverridesVirtual(
				        TypeLinkageName, Method, StructDecl.ModulePath))
				{
					return nullptr;
				}
				if (Method.bDeleted)
				{
					const std::string DeletedName = MakeMethodFunctionName(
					    TypeLinkageName,
					    Method.Name,
					    CanonicalMethodParamTypeNames(
					        Method, StructDecl.ModulePath, {}),
					    Method.bIsConst);
					if (!EnsureDeletedFunctionDeclaration(
					        DeletedName,
					        Method.ReturnType,
					        Method.Parameters,
					        !Method.bIsStatic,
					        Method.bIsStatic ? 0 : TypeIndex,
					        StructDecl.ModulePath,
					        Method.Location,
					        Method.bIsConst,
					        Method.DeclMeta))
					{
						return nullptr;
					}
					continue;
				}
				// Static member functions have no `this`; they are compiled like free
				// functions and take part in no virtual-override machinery.
				if (Method.bIsStatic)
				{
					// Complete static signatures were registered before field layouts.
					continue;
				}
				std::string BaseLinkage;
				const FMethodDecl* BaseMethod = nullptr;
				const bool bFoundVirtualBase = FindVirtualBaseMethod(
				    TypeLinkageName,
				    Method,
				    StructDecl.ModulePath,
				    BaseLinkage,
				    BaseMethod);
				if (!bFoundVirtualBase && ReportIncompatibleVirtualOverride(TypeLinkageName, Method, StructDecl.ModulePath, Method.Location))
				{
					return nullptr;
				}
				if (Method.bOverride && !bFoundVirtualBase)
				{
					ReportErrorId(
					    "diag.override_method_has_no_matching_virtual_base_method",
					    Method.Location,
					    {Method.Name});
					return nullptr;
				}
				if (ReportIfCannotOverrideFinalMethod(
				        bFoundVirtualBase,
				        BaseMethod,
				        Method,
				        Method.Location))
				{
					return nullptr;
				}
				const bool bIsVirtual = Method.bVirtual || Method.bFinal || bFoundVirtualBase;
				if (Method.bPureVirtual)
				{
					if (!EnsurePureVirtualFunctionDeclaration(
					        MakeMethodFunctionName(
					            TypeLinkageName,
					            Method.Name,
					            CanonicalMethodParamTypeNames(
					                Method, StructDecl.ModulePath, {}),
					            Method.bIsConst),
					        Method.ReturnType,
					        Method.Parameters,
					        TypeIndex,
					        StructDecl.ModulePath,
					        Method.Location,
					        Method.bIsConst,
					        Method.Access,
					        Method.DeclMeta))
					{
						return nullptr;
					}
					continue;
				}
				if (!EnsureMethodDeclarationStub(
				        MakeMethodFunctionName(
				            TypeLinkageName,
				            Method.Name,
				            CanonicalMethodParamTypeNames(Method, StructDecl.ModulePath, {}), Method.bIsConst),
				        Method, TypeIndex, bIsVirtual, StructDecl.ModulePath))
				{
					return nullptr;
				}
			}
		}
		for (const FClassDecl& ClassDecl : Program.Classes)
		{
			if (ConstantBranchState && !ConstantBranchState->Includes(ClassDecl.Scope.ScopeId))
			{
				continue;
			}
			if (ClassDecl.bPrecompiledPackage)
			{
				continue;
			}
			const std::string TypeLinkageName =
			    GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name);
			std::unordered_set<std::string> ConstructorKeys;
			for (const FConstructorDecl& Constructor : ClassDecl.Constructors)
			{
				const std::string CtorName = MakeConstructorKey(
				    TypeLinkageName,
				    ClassDecl.Name,
				    Constructor,
				    ClassDecl.ModulePath,
				    {});
				if (!ConstructorKeys.insert(CtorName).second || (TemplateParameterNames(ClassDecl).empty() && Module->FunctionIndices.find(CtorName) != Module->FunctionIndices.end() && !Module->Functions[Module->FunctionIndices.at(CtorName)].Code.empty()))
				{
					ReportErrorId(
					    "diag.duplicate_constructor",
					    Constructor.Location,
					    {TypeNameLeaf(ClassDecl.Name)});
					return nullptr;
				}
			}
			if (!TemplateParameterNames(ClassDecl).empty())
			{
				continue;
			}
			const std::uint16_t TypeIndex = FindOrAddType(TypeLinkageName);
			if (TypeIndex == static_cast<std::uint16_t>(-1))
			{
				return nullptr;
			}
			if (ClassDecl.ClassDefaults)
			{
				const std::string DefaultsName = MakeMethodFunctionName(
				    TypeLinkageName, "@defaults", {}, false);
				if (!EnsureFunctionStub(
				        DefaultsName,
				        "void",
				        0,
				        true,
				        TypeIndex,
				        false,
				        ClassDecl.ClassDefaults->Location))
				{
					return nullptr;
				}
			}
			for (const FConstructorDecl& Constructor : ClassDecl.Constructors)
			{
				const std::string CtorName = MakeConstructorKey(
				    TypeLinkageName,
				    ClassDecl.Name,
				    Constructor,
				    ClassDecl.ModulePath,
				    {});
				if (Constructor.bDeleted)
				{
					if (!EnsureDeletedFunctionDeclaration(
					        CtorName,
					        "void",
					        Constructor.Parameters,
					        true,
					        TypeIndex,
					        ClassDecl.ModulePath,
					        Constructor.Location,
					        false,
					        Constructor.DeclMeta))
					{
						return nullptr;
					}
					continue;
				}
				if (!EnsureFunctionStub(
				        CtorName,
				        "void",
				        static_cast<std::uint8_t>(Constructor.Parameters.size()),
				        true,
				        TypeIndex,
				        false,
				        Constructor.Location))
				{
					return nullptr;
				}
			}
			for (const FMethodDecl& Method : ClassDecl.Methods)
			{
				if (!TemplateParameterNames(Method).empty())
				{
					continue;
				}
				if (ReportIfDeletedMethodOverridesVirtual(
				        TypeLinkageName, Method, ClassDecl.ModulePath))
				{
					return nullptr;
				}
				if (Method.bDeleted)
				{
					const std::string DeletedName = MakeMethodFunctionName(
					    TypeLinkageName,
					    Method.Name,
					    CanonicalMethodParamTypeNames(
					        Method, ClassDecl.ModulePath, {}),
					    Method.bIsConst);
					if (!EnsureDeletedFunctionDeclaration(
					        DeletedName,
					        Method.ReturnType,
					        Method.Parameters,
					        !Method.bIsStatic,
					        Method.bIsStatic ? 0 : TypeIndex,
					        ClassDecl.ModulePath,
					        Method.Location,
					        Method.bIsConst,
					        Method.DeclMeta))
					{
						return nullptr;
					}
					continue;
				}
				// Static member functions have no `this`; they are compiled like free
				// functions and take part in no virtual-override machinery.
				if (Method.bIsStatic)
				{
					continue;
				}
				std::string BaseLinkage;
				const FMethodDecl* BaseMethod = nullptr;
				const bool bFoundVirtualBase = FindVirtualBaseMethod(
				    TypeLinkageName,
				    Method,
				    ClassDecl.ModulePath,
				    BaseLinkage,
				    BaseMethod);
				if (!bFoundVirtualBase && ReportIncompatibleVirtualOverride(TypeLinkageName, Method, ClassDecl.ModulePath, Method.Location))
				{
					return nullptr;
				}
				if (Method.bOverride && !bFoundVirtualBase)
				{
					ReportErrorId(
					    "diag.override_method_has_no_matching_virtual_base_method",
					    Method.Location,
					    {Method.Name});
					return nullptr;
				}
				if (ReportIfCannotOverrideFinalMethod(
				        bFoundVirtualBase,
				        BaseMethod,
				        Method,
				        Method.Location))
				{
					return nullptr;
				}
				const bool bIsVirtual = Method.bVirtual || Method.bFinal || bFoundVirtualBase;
				if (Method.bPureVirtual)
				{
					if (!EnsurePureVirtualFunctionDeclaration(
					        MakeMethodFunctionName(
					            TypeLinkageName,
					            Method.Name,
					            CanonicalMethodParamTypeNames(
					                Method, ClassDecl.ModulePath, {}),
					            Method.bIsConst),
					        Method.ReturnType,
					        Method.Parameters,
					        TypeIndex,
					        ClassDecl.ModulePath,
					        Method.Location,
					        Method.bIsConst,
					        Method.Access,
					        Method.DeclMeta))
					{
						return nullptr;
					}
					continue;
				}
				if (!EnsureMethodDeclarationStub(
				        MakeMethodFunctionName(
				            TypeLinkageName,
				            Method.Name,
				            CanonicalMethodParamTypeNames(Method, ClassDecl.ModulePath, {}), Method.bIsConst),
				        Method, TypeIndex, bIsVirtual, ClassDecl.ModulePath))
				{
					return nullptr;
				}
			}
		}

		// Compile constructors and methods before free functions so CallBoundMethod can resolve indices.
		// This phase includes built-in declarations as well as the filtered source
		// structs. Index that exact ordered input once, retaining arity selection.
		std::unordered_map<std::string, std::vector<std::size_t>> ConstructorStructIndices;
		for (std::size_t Index = 0; Index < AllStructDecls.size(); ++Index)
		{
			if ((Index & 255u) == 0u)
			{
				CheckAnalysisCancellation();
			}
			const auto& Declaration = *AllStructDecls[Index];
			ConstructorStructIndices[GetSymbolLinkageName(
			                             Declaration.ModulePath, Declaration.bExported, Declaration.Name)]
			    .push_back(Index);
		}
		std::unordered_set<std::string> ConstructorsCompiled;
		const auto EnsureConstructorsCompiled = [&](auto&& Self, const std::string& TypeLinkageName) -> bool
		{
			if (ConstructorsCompiled.count(TypeLinkageName) > 0)
			{
				return true;
			}
			// Non-generic concrete types only (arity-0). Skip same-named generic templates.
			for (const auto Index : FindDeclarationCandidates(ConstructorStructIndices, TypeLinkageName))
			{
				const FStructDecl& StructDecl = *AllStructDecls[Index];
				if (GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name) != TypeLinkageName || !TemplateParameterNames(StructDecl).empty())
				{
					continue;
				}
				if (StructDecl.bPrecompiledPackage)
				{
					ConstructorsCompiled.insert(TypeLinkageName);
					return true;
				}
				if (!StructDecl.BaseTypeName.empty())
				{
					const std::string BaseLinkage =
					    ResolveTypeLinkageName(StructDecl.BaseTypeName, StructDecl.ModulePath);
					if (!BaseLinkage.empty() && !Self(Self, BaseLinkage))
					{
						return false;
					}
				}
				const std::uint16_t TypeIndex = FindOrAddType(TypeLinkageName);
				if (TypeIndex == static_cast<std::uint16_t>(-1))
				{
					return false;
				}
				const std::vector<std::string> DeclaredBases = StructDecl.BaseTypeName.empty()
				    ? std::vector<std::string>{}
				    : std::vector<std::string>{StructDecl.BaseTypeName};
				for (const FConstructorDecl& Constructor : StructDecl.Constructors)
				{
					if (Constructor.bDeleted)
					{
						continue;
					}
					if (!CompileConstructor(
					        TypeLinkageName,
					        StructDecl.Name,
					        Constructor,
					        StructDecl.Fields,
					        DeclaredBases,
					        TypeIndex,
					        StructDecl.ModulePath,
					        Constructor.Location))
					{
						if (!RecoverAfterCompileUnitError())
						{
							return false;
						}
					}
				}
				ConstructorsCompiled.insert(TypeLinkageName);
				return true;
			}
			for (const auto Index : FindDeclarationCandidates(SourceClassIndices, TypeLinkageName))
			{
				const FClassDecl& ClassDecl = Program.Classes[Index];
				if (ConstantBranchState && !ConstantBranchState->Includes(ClassDecl.Scope.ScopeId))
				{
					continue;
				}
				if (GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name) != TypeLinkageName || !TemplateParameterNames(ClassDecl).empty())
				{
					continue;
				}
				if (ClassDecl.bPrecompiledPackage)
				{
					ConstructorsCompiled.insert(TypeLinkageName);
					return true;
				}
				for (const std::string& BaseSimple : ClassDecl.BaseTypeNames)
				{
					const std::string BaseLinkage =
					    ResolveInheritanceBaseLinkage(BaseSimple, ClassDecl.ModulePath);
					if (!BaseLinkage.empty() && !Self(Self, BaseLinkage))
					{
						return false;
					}
				}
				const std::uint16_t TypeIndex = FindOrAddType(TypeLinkageName);
				if (TypeIndex == static_cast<std::uint16_t>(-1))
				{
					return false;
				}
				for (const FConstructorDecl& Constructor : ClassDecl.Constructors)
				{
					if (Constructor.bDeleted)
					{
						continue;
					}
					if (!CompileConstructor(
					        TypeLinkageName,
					        ClassDecl.Name,
					        Constructor,
					        ClassDecl.Fields,
					        ClassDecl.BaseTypeNames,
					        TypeIndex,
					        ClassDecl.ModulePath,
					        Constructor.Location))
					{
						if (!RecoverAfterCompileUnitError())
						{
							return false;
						}
					}
				}
				ConstructorsCompiled.insert(TypeLinkageName);
				return true;
			}
			ConstructorsCompiled.insert(TypeLinkageName);
			return true;
		};

		for (const FStructDecl* const StructDeclPtr : AllStructDecls)
		{
			const FStructDecl& StructDecl = *StructDeclPtr;
			if (!TemplateParameterNames(StructDecl).empty())
			{
				continue;
			}
			if (StructDecl.bPrecompiledPackage)
			{
				continue;
			}
			const std::string TypeLinkageName =
			    GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name);
			if (!EnsureConstructorsCompiled(EnsureConstructorsCompiled, TypeLinkageName))
			{
				return nullptr;
			}
		}
		for (const FClassDecl& ClassDecl : Program.Classes)
		{
			if (ConstantBranchState && !ConstantBranchState->Includes(ClassDecl.Scope.ScopeId))
			{
				continue;
			}
			if (!TemplateParameterNames(ClassDecl).empty())
			{
				continue;
			}
			if (ClassDecl.bPrecompiledPackage)
			{
				continue;
			}
			const std::string TypeLinkageName =
			    GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name);
			if (!EnsureConstructorsCompiled(EnsureConstructorsCompiled, TypeLinkageName))
			{
				return nullptr;
			}
		}

		for (const FStructDecl* const StructDeclPtr : AllStructDecls)
		{
			const FStructDecl& StructDecl = *StructDeclPtr;
			if (!TemplateParameterNames(StructDecl).empty())
			{
				continue;
			}
			if (StructDecl.bPrecompiledPackage)
			{
				continue;
			}
			const std::string TypeLinkageName =
			    GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name);
			const std::uint16_t TypeIndex = FindOrAddType(TypeLinkageName);
			if (TypeIndex == static_cast<std::uint16_t>(-1))
			{
				return nullptr;
			}
			for (const FMethodDecl& Method : StructDecl.Methods)
			{
				if (!TemplateParameterNames(Method).empty())
				{
					continue;
				}
				if (Method.bDeleted)
				{
					continue;
				}
				if (!Method.Body)
				{
					if (Method.bPureVirtual)
					{
						continue;
					}
					ReportError("method declared but not defined in the same file: " + Method.Name, Method.Location);
					return nullptr;
				}
				const std::string MethodName = MakeMethodFunctionName(
				    TypeLinkageName,
				    Method.Name,
				    CanonicalMethodParamTypeNames(Method, StructDecl.ModulePath, {}), Method.bIsConst);
				// Static member functions compile like free functions (no `this`, no
				// virtual dispatch); skip the virtual-override validation entirely.
				if (Method.bIsStatic)
				{
					if (!CompileFunction(
					        MethodName,
					        Method.ReturnType,
					        Method.Parameters,
					        *Method.Body,
					        false,
					        0,
					        StructDecl.ModulePath,
					        Method.Location,
					        false, nullptr, Method.bIsConst, &Method.DeclMeta, nullptr, false, Method.bConstexpr))
					{
						if (!RecoverAfterCompileUnitError())
						{
							return nullptr;
						}
					}
					continue;
				}
				std::string BaseLinkage;
				const FMethodDecl* BaseMethod = nullptr;
				const bool bFoundVirtualBase = FindVirtualBaseMethod(
				    TypeLinkageName,
				    Method,
				    StructDecl.ModulePath,
				    BaseLinkage,
				    BaseMethod);
				if (!bFoundVirtualBase && ReportIncompatibleVirtualOverride(TypeLinkageName, Method, StructDecl.ModulePath, Method.Location))
				{
					return nullptr;
				}
				if (Method.bOverride && !bFoundVirtualBase)
				{
					ReportErrorId(
					    "diag.override_method_has_no_matching_virtual_base_method",
					    Method.Location,
					    {Method.Name});
					return nullptr;
				}
				if (ReportIfCannotOverrideFinalMethod(
				        bFoundVirtualBase,
				        BaseMethod,
				        Method,
				        Method.Location))
				{
					return nullptr;
				}
				const bool bIsVirtual = Method.bVirtual || Method.bFinal || bFoundVirtualBase;
				if (!CompileFunction(
				        MethodName,
				        Method.ReturnType,
				        Method.Parameters,
				        *Method.Body,
				        true,
				        TypeIndex,
				        StructDecl.ModulePath,
				        Method.Location,
				        bIsVirtual, nullptr, Method.bIsConst, &Method.DeclMeta, nullptr, false, Method.bConstexpr))
				{
					if (!RecoverAfterCompileUnitError())
					{
						return nullptr;
					}
				}
				else if (Method.bPureVirtual)
				{
					const std::uint16_t Index = FindFunctionIndex(MethodName);
					if (Index < Module->Functions.size())
					{
						Module->Functions[Index].bIsAbstract = true;
					}
				}
			}
			if (StructDecl.Destructor && !CompileDestructor(TypeLinkageName, StructDecl.Name, *StructDecl.Destructor, TypeIndex, StructDecl.ModulePath))
			{
				if (!RecoverAfterCompileUnitError())
				{
					return nullptr;
				}
			}
		}

		for (const FClassDecl& ClassDecl : Program.Classes)
		{
			if (ConstantBranchState && !ConstantBranchState->Includes(ClassDecl.Scope.ScopeId))
			{
				continue;
			}
			if (!TemplateParameterNames(ClassDecl).empty())
			{
				continue;
			}
			if (ClassDecl.bPrecompiledPackage)
			{
				continue;
			}
			const std::string TypeLinkageName =
			    GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name);
			const std::uint16_t TypeIndex = FindOrAddType(TypeLinkageName);
			if (TypeIndex == static_cast<std::uint16_t>(-1))
			{
				return nullptr;
			}
			if (ClassDecl.ClassDefaults)
			{
				const std::string DefaultsName = MakeMethodFunctionName(
				    TypeLinkageName, "@defaults", {}, false);
				const std::vector<FFunctionParam> NoParameters;
				if (!CompileFunction(
				        DefaultsName,
				        "void",
				        NoParameters,
				        *ClassDecl.ClassDefaults,
				        true,
				        TypeIndex,
				        ClassDecl.ModulePath,
				        ClassDecl.ClassDefaults->Location,
				        false))
				{
					if (!RecoverAfterCompileUnitError())
					{
						return nullptr;
					}
				}
				else
				{
					auto& Meta = Module->Types[TypeIndex].DeclMeta;
					Meta.erase(
					    std::remove_if(
					        Meta.begin(), Meta.end(),
					        [](const auto& Entry)
					{
						return Entry.first == ClassDefaultsLinkageMetaKey;
					}),
					    Meta.end());
					Meta.emplace_back(
					    std::string(ClassDefaultsLinkageMetaKey), DefaultsName);
				}
			}
			for (const FMethodDecl& Method : ClassDecl.Methods)
			{
				if (!TemplateParameterNames(Method).empty())
				{
					continue;
				}
				if (Method.bDeleted)
				{
					continue;
				}
				if (!Method.Body)
				{
					if (Method.bPureVirtual)
					{
						continue;
					}
					ReportError("method declared but not defined in the same file: " + Method.Name, Method.Location);
					return nullptr;
				}
				const std::string MethodName = MakeMethodFunctionName(
				    TypeLinkageName,
				    Method.Name,
				    CanonicalMethodParamTypeNames(Method, ClassDecl.ModulePath, {}), Method.bIsConst);
				// Static member functions compile like free functions (no `this`, no
				// virtual dispatch); skip the virtual-override validation entirely.
				if (Method.bIsStatic)
				{
					if (!CompileFunction(
					        MethodName,
					        Method.ReturnType,
					        Method.Parameters,
					        *Method.Body,
					        false,
					        0,
					        ClassDecl.ModulePath,
					        Method.Location,
					        false, nullptr, Method.bIsConst, &Method.DeclMeta, nullptr, false, Method.bConstexpr))
					{
						if (!RecoverAfterCompileUnitError())
						{
							return nullptr;
						}
					}
					continue;
				}
				std::string BaseLinkage;
				const FMethodDecl* BaseMethod = nullptr;
				const bool bFoundVirtualBase = FindVirtualBaseMethod(
				    TypeLinkageName,
				    Method,
				    ClassDecl.ModulePath,
				    BaseLinkage,
				    BaseMethod);
				if (!bFoundVirtualBase && ReportIncompatibleVirtualOverride(TypeLinkageName, Method, ClassDecl.ModulePath, Method.Location))
				{
					return nullptr;
				}
				if (Method.bOverride && !bFoundVirtualBase)
				{
					ReportErrorId(
					    "diag.override_method_has_no_matching_virtual_base_method",
					    Method.Location,
					    {Method.Name});
					return nullptr;
				}
				if (ReportIfCannotOverrideFinalMethod(
				        bFoundVirtualBase,
				        BaseMethod,
				        Method,
				        Method.Location))
				{
					return nullptr;
				}
				const bool bIsVirtual = Method.bVirtual || Method.bFinal || bFoundVirtualBase;
				if (!CompileFunction(
				        MethodName,
				        Method.ReturnType,
				        Method.Parameters,
				        *Method.Body,
				        true,
				        TypeIndex,
				        ClassDecl.ModulePath,
				        Method.Location,
				        bIsVirtual, nullptr, Method.bIsConst, &Method.DeclMeta, nullptr, false, Method.bConstexpr))
				{
					if (!RecoverAfterCompileUnitError())
					{
						return nullptr;
					}
				}
				else if (Method.bPureVirtual)
				{
					const std::uint16_t Index = FindFunctionIndex(MethodName);
					if (Index < Module->Functions.size())
					{
						Module->Functions[Index].bIsAbstract = true;
					}
				}
			}
			if (ClassDecl.Destructor && !CompileDestructor(TypeLinkageName, ClassDecl.Name, *ClassDecl.Destructor, TypeIndex, ClassDecl.ModulePath))
			{
				if (!RecoverAfterCompileUnitError())
				{
					return nullptr;
				}
			}
		}

		for (const FInterfaceDecl& InterfaceDecl : Program.Interfaces)
		{
			if (ConstantBranchState && !ConstantBranchState->Includes(InterfaceDecl.Scope.ScopeId))
			{
				continue;
			}
			if (!TemplateParameterNames(InterfaceDecl).empty())
			{
				continue;
			}
			if (InterfaceDecl.bPrecompiledPackage)
			{
				continue;
			}
			if (!InterfaceDecl.BaseInterfaceNames.empty())
			{
				const std::string ChildLinkage = GetSymbolLinkageName(
				    InterfaceDecl.ModulePath,
				    InterfaceDecl.bExported,
				    InterfaceDecl.Name);
				const std::uint16_t ChildIndex = FindInterfaceIndex(ChildLinkage);
				if (ChildIndex == static_cast<std::uint16_t>(-1))
				{
					ReportErrorId(
					    "diag.unknown_interface_type",
					    InterfaceDecl.Location,
					    {InterfaceDecl.Name});
					return nullptr;
				}
				// Clear and re-wire in case some non-generic parents were linked earlier.
				Module->Interfaces[ChildIndex].BaseInterfaceIndices.clear();
				for (const std::string& ParentAsWritten : InterfaceDecl.BaseInterfaceNames)
				{
					std::string InstantiatedParent;
					const std::string PreviousModulePath = CurrentModulePath;
					CurrentModulePath = InterfaceDecl.ModulePath;
					const bool bResolved = EnsureInterfaceTypeResolved(
					    ParentAsWritten,
					    InstantiatedParent,
					    nullptr,
					    InterfaceDecl.Location);
					CurrentModulePath = PreviousModulePath;
					if (!bResolved)
					{
						return nullptr;
					}
					const std::uint16_t BaseIndex = FindInterfaceIndex(InstantiatedParent);
					if (BaseIndex == static_cast<std::uint16_t>(-1))
					{
						ReportErrorId(
						    "diag.unknown_parent_interface",
						    InterfaceDecl.Location,
						    {ParentAsWritten});
						return nullptr;
					}
					if (const FInterfaceDecl* const ParentDecl =
					        FindInterfaceDeclByLinkage(InstantiatedParent);
					    ParentDecl && ParentDecl->bFinal)
					{
						ReportErrorId(
						    "diag.cannot_extend_final_interface",
						    InterfaceDecl.Location,
						    {ParentAsWritten});
						return nullptr;
					}
					Module->Interfaces[ChildIndex].BaseInterfaceIndices.push_back(BaseIndex);
				}
			}
		}
		// Parents may now include type-app instances; re-check cycles.
		if (!CheckInterfaceInheritanceCycles())
		{
			return nullptr;
		}
		for (const FInterfaceDecl& InterfaceDecl : Program.Interfaces)
		{
			if (ConstantBranchState && !ConstantBranchState->Includes(InterfaceDecl.Scope.ScopeId))
			{
				continue;
			}
			if (!TemplateParameterNames(InterfaceDecl).empty())
			{
				continue;
			}
			if (InterfaceDecl.bPrecompiledPackage)
			{
				continue;
			}
			const std::string IfaceLinkage = GetSymbolLinkageName(
			    InterfaceDecl.ModulePath,
			    InterfaceDecl.bExported,
			    InterfaceDecl.Name);
			for (const FMethodDecl& Method : InterfaceDecl.Methods)
			{
				if (!TemplateParameterNames(Method).empty())
				{
					continue;
				}
				if (!Method.bIsStatic)
				{
					std::string BaseLinkage;
					const FMethodDecl* BaseMethod = nullptr;
					const bool bFoundVirtualBase = FindVirtualBaseMethod(
					    IfaceLinkage,
					    Method,
					    InterfaceDecl.ModulePath,
					    BaseLinkage,
					    BaseMethod);
					if (Method.bOverride && !bFoundVirtualBase)
					{
						ReportErrorId(
						    "diag.override_method_has_no_matching_virtual_base_method",
						    Method.Location,
						    {Method.Name});
						return nullptr;
					}
					if (ReportIfCannotOverrideFinalMethod(
					        bFoundVirtualBase,
					        BaseMethod,
					        Method,
					        Method.Location))
					{
						return nullptr;
					}
				}
				if (Method.bPureVirtual)
				{
					continue;
				}
				const std::string MethodName = MakeMethodFunctionName(
				    IfaceLinkage,
				    Method.Name,
				    CanonicalMethodParamTypeNames(Method, InterfaceDecl.ModulePath, {}), Method.bIsConst);
				// Static interface member functions compile like free functions (no
				// `this`, no dynamic dispatch); they must have a body (parser enforces).
				CurrentInterfaceOwnerLinkage = Method.bIsStatic ? std::string{} : IfaceLinkage;
				const bool bCompiled = CompileFunction(
				    MethodName,
				    Method.ReturnType,
				    Method.Parameters,
				    *Method.Body,
				    !Method.bIsStatic,
				    Method.bIsStatic ? 0 : 0xFFFF,
				    InterfaceDecl.ModulePath,
				    Method.Location,
				    Method.bIsStatic ? false : Method.bVirtual,
				    nullptr,
				    Method.bIsConst,
				    &Method.DeclMeta, nullptr, false, Method.bConstexpr);
				CurrentInterfaceOwnerLinkage.clear();
				if (!bCompiled)
				{
					if (!RecoverAfterCompileUnitError())
					{
						return nullptr;
					}
				}
			}
			// Validate flatten / default clashes (single or multi parent).
			if (!InterfaceDecl.BaseInterfaceNames.empty())
			{
				std::vector<std::pair<std::string, const FMethodDecl*>> Flattened;
				std::unordered_set<std::string> VisitedInterfaces;
				if (!CollectFlattenedInterfaceMethods(IfaceLinkage, Flattened, VisitedInterfaces))
				{
					return nullptr;
				}
			}
		}

		for (const FInterfaceDecl& InterfaceDecl : Program.Interfaces)
		{
			if (ConstantBranchState && !ConstantBranchState->Includes(InterfaceDecl.Scope.ScopeId))
			{
				continue;
			}
			if (!TemplateParameterNames(InterfaceDecl).empty() && !ValidateTypeParamBoundDeclarations(InterfaceDecl.TypeParams, InterfaceDecl.Bounds, InterfaceDecl.ModulePath, InterfaceDecl.Location, "type", &InterfaceDecl.TemplateParameters))
			{
				return nullptr;
			}
		}
		for (const FStructDecl& StructDecl : Program.Structs)
		{
			if (ConstantBranchState && !ConstantBranchState->Includes(StructDecl.Scope.ScopeId))
			{
				continue;
			}
			if (!TemplateParameterNames(StructDecl).empty() && !ValidateTypeParamBoundDeclarations(StructDecl.TypeParams, StructDecl.Bounds, StructDecl.ModulePath, StructDecl.Location, "type", &StructDecl.TemplateParameters))
			{
				return nullptr;
			}
		}
		for (const FClassDecl& ClassDecl : Program.Classes)
		{
			if (ConstantBranchState && !ConstantBranchState->Includes(ClassDecl.Scope.ScopeId))
			{
				continue;
			}
			if (!TemplateParameterNames(ClassDecl).empty() && !ValidateTypeParamBoundDeclarations(ClassDecl.TypeParams, ClassDecl.Bounds, ClassDecl.ModulePath, ClassDecl.Location, "type", &ClassDecl.TemplateParameters))
			{
				return nullptr;
			}
		}

		for (const FFunctionDecl& Function : Program.Functions)
		{
			const bool bEntryMain = Function.Name == "main" && (Program.EntryModulePath.empty() || Function.ModulePath == Program.EntryModulePath);
			if (!TemplateParameterNames(Function).empty())
			{
				if (!ValidateGenericFunctionBoundDeclarations(Function, Function.Location))
				{
					return nullptr;
				}
				continue;
			}
			if (Function.bPrecompiledPackage)
			{
				continue;
			}
			if (Function.bDeleted)
			{
				continue;
			}
			if (bEntryMain)
			{
				if (Module->MainFunctionIndex >= 0)
				{
					ReportErrorId("diag.duplicate_main", Function.Location);
					return nullptr;
				}
				const std::string MainReturnType = NormalizeTypeAlias(
				    Function.ReturnType,
				    Function.ModulePath);
				if (MainReturnType != "void" && MainReturnType != "int")
				{
					ReportErrorId("diag.main_must_return_void_or_int", Function.Location);
					return nullptr;
				}
				if (Function.Parameters.size() > 1)
				{
					ReportErrorId("diag.main_must_take_at_most_one_parameter", Function.Location);
					return nullptr;
				}
				if (Function.Parameters.size() == 1)
				{
					const FFunctionParam& Parameter = Function.Parameters[0];
					if (!Runtime::IsScriptArgsArrayTypeName(
					        NormalizeTypeAlias(Parameter.TypeName, Function.ModulePath)) ||
					    Parameter.bIsRef || Parameter.bIsVariadic || Parameter.DefaultValue)
					{
						ReportErrorId("diag.main_parameter_must_be_string", Function.Location);
						return nullptr;
					}
				}
			}
			const std::string FunctionLinkageName = MakeFreeFunctionLinkageName(Function);
			const bool bCompiledFunction = CompileFunction(
			    FunctionLinkageName,
			    Function.ReturnType,
			    Function.Parameters,
			    *Function.Body,
			    false,
			    0,
			    Function.ModulePath,
			    Function.Location,
			    false,
			    nullptr,
			    false,
			    &Function.DeclMeta,
			    nullptr,
			    bEntryMain && NormalizeTypeAlias(Function.ReturnType, Function.ModulePath) == "int", Function.bConstexpr);
			if (!bCompiledFunction)
			{
				if (!RecoverAfterCompileUnitError())
				{
					return nullptr;
				}
			}
			else if (bEntryMain)
			{
				const std::uint16_t MainIndex = FindFunctionIndex(FunctionLinkageName);
				Module->MainFunctionIndex = MainIndex == static_cast<std::uint16_t>(-1)
				    ? -1
				    : static_cast<std::int32_t>(MainIndex);
			}
		}

		CurrentFunction = nullptr;
		Scopes.clear();
		LoopStack.clear();
		NextLocalSlot = 0;
		NextAssignTempId = 0;
		// Initialize static data members (into module init code) before top-level statements.
		// Non-generic statics first, then any generic instantiations queued during
		// method/body compile — so `class A { static int X = 10; }` runs before
		// `class B<T> { static int Y = A.X; }` (instantiation-time emit would reverse).
		if (!CompileStaticFieldInitializers())
		{
			return nullptr;
		}
		for (const std::unique_ptr<FStmt>& Statement : Program.TopLevelStatements)
		{
			CurrentModulePath = Statement->ModulePath;
			CurrentNamespacePrefix.clear();
			if (Statement->Kind == EStmtKind::VarDecl)
			{
				const std::string& DeclName =
				    static_cast<const FVarDeclStmt&>(*Statement).Name;
				if (const std::size_t Dot = DeclName.rfind('.'); Dot != std::string::npos)
				{
					CurrentNamespacePrefix = DeclName.substr(0, Dot);
				}
			}
			if (CurrentModulePath.empty())
			{
				ReportError("internal error: top-level statement missing module path", Statement->Location);
				return nullptr;
			}
			if (Statement->Kind == EStmtKind::VarDecl && static_cast<const FVarDeclStmt&>(*Statement).bPrecompiledPackage)
			{
				continue;
			}
			const std::size_t InitBeginOffset = Module->InitCode.size();
			if (!CompileStatement(*Statement))
			{
				if (!RecoverAfterCompileUnitError())
				{
					return nullptr;
				}
			}
			else if (Statement->Kind == EStmtKind::VarDecl)
			{
				const FVarDeclStmt& VarDecl =
				    static_cast<const FVarDeclStmt&>(*Statement);
				const std::string GlobalName = VarDecl.ModulePath.empty()
				    ? VarDecl.Name
				    : GetSymbolLinkageName(
				          VarDecl.ModulePath,
				          VarDecl.bExported,
				          VarDecl.Name);
				const auto Slot = Module->GlobalSlots.find(GlobalName);
				if (Slot != Module->GlobalSlots.end() && Module->InitCode.size() > InitBeginOffset)
				{
					Module->GlobalInitRanges.push_back(
					    {Slot->second, InitBeginOffset, Module->InitCode.size()});
				}
			}
		}
		// Host construction also needs a checked entry when source never constructs
		// a nontrivial concrete type. Plain scalar layouts retain allocator defaults.
		std::unordered_set<std::string> BuiltinOwners;
		for (const auto& Declaration : BuiltinStructTemplates)
		{
			BuiltinOwners.insert(GetSymbolLinkageName(Declaration.ModulePath, Declaration.bExported, Declaration.Name));
		}
		for (const auto& Declaration : BuiltinClassTemplates)
		{
			BuiltinOwners.insert(GetSymbolLinkageName(Declaration.ModulePath, Declaration.bExported, Declaration.Name));
		}
		const auto DefaultConstructionTypeCount = Module->Types.size();
		for (std::size_t Index = 0; Index < DefaultConstructionTypeCount; ++Index)
		{
			if (Module->Types[Index].bIsStatic || Module->Types[Index].bIsAbstract || UsesTrivialDefaultConstruction(static_cast<std::uint16_t>(Index)))
			{
				continue;
			}
			const std::string TypeName = Module->Types[Index].Name;
			const auto Base = GenericTemplateBaseLinkage(TypeName);
			const auto Lookup = Base.empty() ? TypeName : Base;
			if (BuiltinOwners.count(Lookup) != 0 || TypeHasUserConstructors(TypeName))
			{
				continue;
			}
			bool OpenGeneric = false;
			FSourceLocation Location;
			const auto Inspect = [&](const auto& Declarations, const auto& Indices)
			{
				for (const auto Candidate : FindDeclarationCandidates(Indices, Lookup))
				{
					const auto& Declaration = Declarations[Candidate];
					if (GetSymbolLinkageName(Declaration.ModulePath, Declaration.bExported, Declaration.Name) != Lookup)
					{
						continue;
					}
					Location = Declaration.Location;
					OpenGeneric = OpenGeneric || (Base.empty() && !TemplateParameterNames(Declaration).empty());
				}
			};
			Inspect(Program.Structs, SourceStructIndices);
			Inspect(Program.Classes, SourceClassIndices);
			if (OpenGeneric)
			{
				continue;
			}
			std::uint16_t ConstructorIndex = static_cast<std::uint16_t>(-1);
			if (!EnsureImplicitDefaultConstructor(TypeName, static_cast<std::uint16_t>(Index), Location, ConstructorIndex)) return nullptr;
		}
		// Top-level / mid-body Ensures may have queued generic static inits.
		if (!FlushPendingGenericStaticFieldInitializers())
		{
			return nullptr;
		}
		// Ranges are appended in emit order, so BeginOffsets are not necessarily
		// monotonic. Keep them sorted for tooling / stable iteration (Begin asc,
		// longer End first on ties). Hot reload uses the punch-hole byte map, not
		// a binary search over this list.
		std::stable_sort(
		    Module->GlobalInitRanges.begin(),
		    Module->GlobalInitRanges.end(),
		    [](const FBytecodeGlobalInitRange& Lhs, const FBytecodeGlobalInitRange& Rhs)
		{
			if (Lhs.BeginOffset != Rhs.BeginOffset)
			{
				return Lhs.BeginOffset < Rhs.BeginOffset;
			}
			return Lhs.EndOffset > Rhs.EndOffset;
		});
		Module->InitLocalCount =
		    std::max(Module->InitLocalCount, NextLocalSlot);

		if (!FinalizeSpecialMembers()) return nullptr;
		OptimizeInitBytecode();
		OptimizeModuleCalls();
		// Re-run local arithmetic fusion so Call→Add inlining can become IncLocal.
		for (FBytecodeFunction& Function : Module->Functions)
		{
			OptimizeFunctionBytecode(Function);
		}
		OptimizeInitBytecode();

		if (bCollectRuntimeMemoryTypes)
		{
			RecordMemoryTypeUses(Program);
		}
		// Persist resolved scalar domains so installed consumers can validate
		// closed records without the producer's aliases or source files.
		for (auto& Declaration : Module->TemplateDeclarations)
		{
			CurrentModulePath = Declaration.ModulePath;
			std::unordered_map<std::string, std::string> Symbolic;
			for (auto* Schema : {&Declaration.EnclosingParameters, &Declaration.Parameters})
			{
				auto Records = Schema->GetParameters();
				for (auto& Parameter : Records)
				{
					CurrentNamespacePrefix = Parameter.Namespace;
					if (Parameter.Kind == ETemplateParameterKind::Type)
					{
						Symbolic[Parameter.Name] = "@type_parameter_" + Parameter.Id;
					}
					else
					{
						const auto Type = SubstituteTypeNameWith(Parameter.ValueTypeName, Symbolic);
						// Type substitution deliberately leaves value expressions intact.
						// Do not turn an open enum domain such as Owner<N + C>::Tag
						// into a runtime enum before its value parameters are bound.
						const bool DependentExpression = std::any_of(Symbolic.begin(), Symbolic.end(),
						    [&](const auto& Binding)
						{
							return ParserDetail::TypeMentionsParam(Parameter.ValueTypeName, Binding.first);
						});
						FTemplateValueType Domain;
						if (!DependentExpression && Type.find("@type_parameter_") == std::string::npos && Type.find("@dependent-value:") == std::string::npos && ResolveTemplateValueType(Parameter.ValueTypeName, Declaration.ModulePath, Symbolic, Domain))
						{
							Parameter.ValueTypeName = Domain.Name;
						}
						Symbolic[Parameter.Name] = "@dependent-value:" + Parameter.ValueTypeName;
					}
				}
				Schema->Assign(std::move(Records));
			}
		}
		ActiveProgram = nullptr;
		if (!Diagnostics.empty())
		{
			Error = Diagnostics.front();
			bHasError = true;
			Module.reset();
			return nullptr;
		}
		FinalizePackageSurface(Program);
		for (auto& Function : Module->Functions)
		{
			CollectPauseMarkers(Function.Code, Function.DebugLines, Function.PauseMarkerOffsets);
		}
		CollectPauseMarkers(Module->InitCode, Module->InitDebugLines, Module->InitPauseMarkerOffsets);
		return std::move(Module);
	}
}
