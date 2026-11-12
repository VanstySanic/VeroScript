// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "ConstantEvaluation/VeroScriptConstantBranches.h"
#include "VeroScriptCompilerHostLookup.h"
#include "VeroScriptCompilerTemplateBinding.h"
#include "VeroScriptMemberAccess.h"
#include "TypeSupport/VeroScriptTypeMemberBinding.h"

#include <algorithm>
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParser.h"

#include <deque>

namespace VeroScript
{
	using CompilerDetail::FTemplateBindingScope;

	bool VeroScriptCompiler::ValidateConstexprFunction(const std::string &ReturnType,
	    const std::vector<FFunctionParam> &Parameters, const FBlockStmt &Body, const std::string &ModulePath)
	{
		FConstantFunction Function;
		Function.Identity = CurrentAccessFunctionLinkage.empty() ? "$constexpr-validation" : CurrentAccessFunctionLinkage;
		Function.Body = &Body;
		if (!ResolveTemplateValueType(ReturnType, ModulePath, ActiveTypeSubst, Function.ReturnType))
		{
			ReportError("constexpr requires a supported scalar return type", Body.Location);
			return false;
		}
		for (std::size_t I = 0; I < Parameters.size(); ++I)
		{
			const auto &Parameter = Parameters[I];
			FTemplateValueType Type;
			if (Parameter.bIsRef || Parameter.bIsVariadic || !ResolveTemplateValueType(Parameter.TypeName, ModulePath, ActiveTypeSubst, Type))
			{
				ReportError("constexpr parameters must be scalar values", Body.Location);
				return false;
			}
			Function.ParameterTypes.push_back(Type);
			Function.ParameterNames.push_back(Body.DefinitionParameterNames.empty() ? Parameter.Name : Body.DefinitionParameterNames[I]);
			Function.ParameterConsts.push_back(Parameter.bIsConst);
		}
		FBooleanLiteralExpr Dummy(false);
		const auto Result = EvaluateTemplateExpression(Dummy, ModulePath, ActiveTypeSubst, CurrentNamespacePrefix, false, &Function);
		if (!Result.Succeeded())
		{
			ReportError("invalid constexpr function: " + Result.Reason, Result.Location.Line ? Result.Location : Body.Location);
			return false;
		}
		return true;
	}

	bool VeroScriptCompiler::ResolveTemplateValueType(const std::string &Name, const std::string &ModulePath,
	    const std::unordered_map<std::string, std::string> &Bindings,
	    FTemplateValueType &OutType) const
	{
		const auto Type = ExpandHostTypeAlias(ExpandUserTypeAlias(SubstituteTypeNameWith(Name, Bindings), ModulePath));
		if (TryGetTemplateValueType(Type, OutType))
		{
			return true;
		}
		if (const auto *Host = CompilerDetail::FindHostTypeWithNamespaceSpelling(ActiveBindings, Type);
		    Host && Host->Kind == EHostTypeKind::Enum)
		{
			if (!TryGetTemplateValueType(Host->EnumUnderlyingTypeName, OutType))
			{
				return false;
			}
			OutType.Name = Host->Name;
			OutType.bEnum = true;
			OutType.bScopedEnum =
			    Host->bScopedEnum ||
			    std::any_of(
			        Host->Members.begin(), Host->Members.end(), [&](const auto &Member)
			{
				return Member.Kind == EHostMemberKind::EnumMember && Member.ReturnHostTypeName == Host->Name;
			});
			if (Module && std::none_of(Module->Enums.begin(), Module->Enums.end(), [&](const auto &Declaration)
			{
				return Declaration.Name == Host->Name;
			}))
			{
				FBytecodeEnumDesc Declaration;
				Declaration.Name = Host->Name;
				Declaration.bScoped = OutType.bScopedEnum;
				Declaration.UnderlyingTypeName = Host->EnumUnderlyingTypeName;
				for (const auto &Member : Host->Members)
				{
					if (Member.Kind == EHostMemberKind::EnumMember)
					{
						Declaration.Members.push_back({Member.Name, Member.EnumValue, {}});
					}
				}
				Module->Enums.push_back(std::move(Declaration));
			}
			return true;
		}
		const auto Qualified = [&]
		{
			auto &Compiler = *const_cast<VeroScriptCompiler *>(this);
			FTemplateBindingScope Scope(Compiler.ActiveTypeSubst, Bindings);
			return QualifyTypeName(Type, ModulePath);
		}();
		const auto Linkage = ResolveTypeLinkageName(Type, ModulePath, 0);
		if (!ActiveProgram)
		{
			return false;
		}
		if (const auto *Declaration = FindEnumDeclaration(!Linkage.empty() && Qualified.find_first_of("#<") == std::string::npos ? Linkage : Qualified))
		{
			std::string Base;
			std::vector<std::string> Arguments;
			if (!SplitUserGenericTypeApp(Qualified, Base, Arguments))
			{
				Arguments = DecodeGenericConcreteTypes(Qualified);
			}
			if (Arguments.empty())
			{
				for (const auto &Parameter : EnclosingTemplateParameterNames(Declaration->Scope))
				{
					const auto Value = Bindings.find(Parameter);
					if (Value == Bindings.end())
					{
						break;
					}
					Arguments.push_back(Value->second);
				}
			}
			if (Arguments.size() != EnclosingTemplateParameterNames(Declaration->Scope).size() ||
			    !TryGetTemplateValueType(Declaration->UnderlyingTypeName, OutType))
			{
				return false;
			}
			OutType.Name = Arguments.empty() ? GetSymbolLinkageName(Declaration->ModulePath, Declaration->bExported, Declaration->Name)
			                                 : MangleGenericInstanceName(GetSymbolLinkageName(Declaration->ModulePath, Declaration->bExported, Declaration->Name), Arguments);
			OutType.bEnum = true;
			OutType.bScopedEnum = Declaration->bScoped;
			return true;
		}
		if (!CurrentOwnerTypeLinkage.empty() && Type.find_first_of(".:<#") == std::string::npos)
		{
			auto Owner = GenericTemplateBaseLinkage(CurrentOwnerTypeLinkage);
			if (Owner.empty())
			{
				Owner = CurrentOwnerTypeLinkage;
			}
			auto OwnerModule = ModulePath;
			if (const auto Separator = Owner.find('\x1F'); Separator != std::string::npos)
			{
				OwnerModule = Owner.substr(0, Separator);
				Owner.erase(0, Separator + 1);
			}
			const auto Member = FindTypeMember(*ActiveProgram, nullptr, Owner,
			    DecodeGenericConcreteTypes(CurrentOwnerTypeLinkage), Type, OwnerModule);
			if (!Member.bAmbiguous && !Member.Name.empty())
			{
				for (const auto &Check : Member.AccessChecks)
				{
					if (!CanAccessMemberFromContext(Check, CurrentOwnerTypeLinkage, *ActiveProgram,
					        CurrentOwnerTypeLinkage, true, ActiveImportIndex) &&
					    !IsCurrentFunctionFriendOf(Check, CurrentOwnerTypeLinkage))
					{
						return false;
					}
				}
				for (const auto &Declaration : ActiveProgram->Enums)
				{
					if (Declaration.Name != Member.Name || Declaration.ModulePath != Member.ModulePath ||
					    !TryGetTemplateValueType(Declaration.UnderlyingTypeName, OutType))
					{
						continue;
					}
					OutType.Name = EnsureEnumMetadata(Declaration, Member.OwnerArguments);
					OutType.bEnum = true;
					OutType.bScopedEnum = Declaration.bScoped;
					return true;
				}
			}
		}
		return false;
	}

	FTemplateEvaluationResult VeroScriptCompiler::EvaluateTemplateSource(
	    const std::string &Source, const std::string &ModulePath,
	    const std::unordered_map<std::string, std::string> &Bindings, const FSourceLocation &Location,
	    const std::string *Namespace, const bool AllowLocals) const
	{
		FTemplateArgument Encoded;
		if (Source.rfind("@value:", 0) == 0 && DecodeTemplateArgument(std::string_view(Source).substr(7), Encoded) &&
		    Encoded.Kind == ETemplateParameterKind::Value)
		{
			return {ETemplateEvaluationStatus::Constant, Encoded.Value, {}, Location};
		}
		VeroScriptLexer Lexer(Source, ModulePath);
		VeroScriptParser Parser(Lexer.Tokenize(), ModulePath, ActiveProgram);
		if (ActiveBindings)
		{
			std::unordered_set<std::string> Names;
			for (const auto &Type : ActiveBindings->GetTypes())
			{
				Names.insert(Type.Name);
			}
			for (const auto &Alias : ActiveBindings->GetTypeAliases())
			{
				Names.insert(Alias.Name);
			}
			Parser.SetHostTypeNames(std::move(Names));
		}
		const auto LookupNamespace = Namespace ? *Namespace : CurrentNamespacePrefix;
		auto Expression = Parser.ParseExpressionFragment(LookupNamespace, {});
		if (!Expression || Lexer.HasError() || Parser.HasError())
		{
			return {ETemplateEvaluationStatus::Invalid, {}, "invalid template constant syntax", Location};
		}
		if (AllowLocals && ActiveProgram)
		{
			auto Fragment = Parser.TakeFragmentDeclarations();
			if (Fragment)
			{
				for (auto &Use : Fragment->ScopedTypeUses)
				{
					Use.ContextType = CurrentOwnerTypeLinkage;
					Use.ContextModule = ModulePath;
				}
				FScriptError AccessError;
				if (!ValidateScopedTypeDeclarations(
				        *ActiveProgram, ActiveImportIndex, AccessError, [&](const auto &Info, const auto &)
				{
					return IsCurrentFunctionFriendOf(Info, CurrentOwnerTypeLinkage);
				}, &Fragment->ScopedTypeUses, [&](std::string_view Scope)
				{
					return !ConstantBranchState || ConstantBranchState->Includes(Scope);
				}))
				{
					ReportError(AccessError.Message, Location);
					return {ETemplateEvaluationStatus::Invalid, {}, AccessError.Message, Location};
				}
			}
		}
		auto Result = EvaluateTemplateExpression(*Expression, ModulePath, Bindings, LookupNamespace, AllowLocals);
		Result.Location = Location;
		return Result;
	}

	FTemplateEvaluationResult VeroScriptCompiler::EvaluateTemplateExpression(
	    const FExpr &Expression, const std::string &ModulePath,
	    const std::unordered_map<std::string, std::string> &Bindings, const std::string &Namespace,
	    const bool AllowLocals, const FConstantFunction *Validation, const bool AllowFunctionCalls) const
	{
		if (!ConstantEvaluationState)
		{
			ConstantEvaluationState = std::make_shared<FConstantEvaluationState>();
			ConstantEvaluationState->Limits = ConstantEvaluationLimits;
		}
		auto &CompilerNamespace = const_cast<VeroScriptCompiler *>(this)->CurrentNamespacePrefix;
		struct FConstantLexicalScope
		{
			VeroScriptCompiler &Compiler;
			std::vector<FScope> PreviousScopes;
			std::vector<std::unordered_map<std::string, std::string>> PreviousAliases;
			FBytecodeFunction *PreviousFunction = nullptr;
			FSourceLocation PreviousLocation;
			bool bActive = false, bValid = true;
			std::uint64_t Memory = 0;
			FConstantLexicalScope(VeroScriptCompiler &C, bool WithLocals, const FLocalConstantInitializer *Environment) : Compiler(C)
			{
				if (WithLocals && !Environment) return;
				std::vector<FScope> Filtered;
				if (WithLocals)
				{
					const auto *SourceScopes = &C.Scopes;
					if (!Environment->FunctionName.empty() && (!C.CurrentFunction || C.CurrentFunction->Name != Environment->FunctionName))
					{
						SourceScopes = nullptr;
						for (auto Scope = C.EnclosingConstantScopes.rbegin(); Scope != C.EnclosingConstantScopes.rend(); ++Scope)
						{
							if (Scope->FunctionName == Environment->FunctionName)
							{
								SourceScopes = Scope->Scopes;
								break;
							}
						}
						if (!SourceScopes)
						{
							bValid = false;
							return;
						}
					}
					const auto Count = (std::min)(Environment->ScopeCount, SourceScopes->size());
					// Bound the temporary lexical snapshot before copying its maps.
					auto &State = *C.ConstantEvaluationState;
					for (std::size_t I = 0; I < Count; ++I)
					{
						for (const auto &Local : (*SourceScopes)[I].Locals)
						{
							CheckAnalysisCancellation();
							if (State.Work >= State.Limits.MaxWork)
							{
								bValid = false;
								return;
							}
							++State.Work;
							Memory += 1024 + Local.first.size();
							if (const auto Pending = (*SourceScopes)[I].ConstantInitializers.find(Local.first); Pending != (*SourceScopes)[I].ConstantInitializers.end())
							{
								for (const auto &Binding : Pending->second.Bindings)
								{
									Memory += Binding.first.size() + Binding.second.size() + 128;
								}
								for (const auto &Scope : Pending->second.EnumScopes)
								{
									for (const auto &Entry : Scope)
									{
										CheckAnalysisCancellation();
										if (State.Work >= State.Limits.MaxWork)
										{
											bValid = false;
											Memory = 0;
											return;
										}
										++State.Work;
										Memory += 128 + Entry.first.size() + Entry.second.size();
										if (Memory > State.Limits.MaxMemoryBytes || State.MemoryBytes > State.Limits.MaxMemoryBytes - Memory)
										{
											bValid = false;
											Memory = 0;
											return;
										}
									}
								}
							}
							if (Memory > State.Limits.MaxMemoryBytes || State.MemoryBytes > State.Limits.MaxMemoryBytes - Memory)
							{
								bValid = false;
								Memory = 0;
								return;
							}
						}
					}
					for (const auto &Scope : Environment->EnumScopes)
					{
						for (const auto &Entry : Scope)
						{
							CheckAnalysisCancellation();
							if (State.Work >= State.Limits.MaxWork)
							{
								bValid = false;
								Memory = 0;
								return;
							}
							++State.Work;
							Memory += 128 + Entry.first.size() + Entry.second.size();
							if (Memory > State.Limits.MaxMemoryBytes || State.MemoryBytes > State.Limits.MaxMemoryBytes - Memory)
							{
								bValid = false;
								Memory = 0;
								return;
							}
						}
					}
					for (std::size_t Index = 0; Index < Count; ++Index)
					{
						for (const auto &Binding : (*SourceScopes)[Index].StaticLocals)
						{
							CheckAnalysisCancellation();
							if (State.Work >= State.Limits.MaxWork)
							{
								bValid = false;
								Memory = 0;
								return;
							}
							++State.Work;
							Memory += 256 + Binding.first.size() + Binding.second.size();
							if (Memory > State.Limits.MaxMemoryBytes || State.MemoryBytes > State.Limits.MaxMemoryBytes - Memory)
							{
								bValid = false;
								Memory = 0;
								return;
							}
						}
					}
					Filtered.assign(SourceScopes->begin(), SourceScopes->begin() + Count);
					for (std::size_t I = 0; I < Count; ++I)
					{
						Filtered[I].EnumConstants = I < Environment->EnumScopes.size()
						    ? Environment->EnumScopes[I]
						    : decltype(Filtered[I].EnumConstants){};
					}
					for (auto &Scope : Filtered)
					{
						for (auto It = Scope.StaticLocals.begin(); It != Scope.StaticLocals.end();)
						{
							const auto Slot = C.Module->GlobalSlots.find(It->second);
							if (Slot != C.Module->GlobalSlots.end() && Slot->second < Environment->GlobalSlotBound)
							{
								++It;
								continue;
							}
							Scope.TemplateConstants.erase(It->first);
							Scope.ConstantInitializers.erase(It->first);
							It = Scope.StaticLocals.erase(It);
						}
					}
					for (auto &Scope : Filtered)
					{
						for (auto I = Scope.Locals.begin(); I != Scope.Locals.end();)
						{
							if (I->second < Environment->SlotBound)
							{
								++I;
								continue;
							}
							const auto &Name = I->first;
							Scope.LocalTypes.erase(Name);
							Scope.LocalConsts.erase(Name);
							Scope.TemplateConstants.erase(Name);
							Scope.ConstantInitializers.erase(Name);
							Scope.LocalRefs.erase(Name);
							Scope.LocalMapEntryViews.erase(Name);
							I = Scope.Locals.erase(I);
						}
					}
				}
				if (Memory)
				{
					C.ConstantEvaluationState->MemoryBytes += Memory;
				}
				PreviousScopes = std::move(C.Scopes);
				PreviousAliases = std::move(C.LocalTypeAliasScopes);
				PreviousFunction = C.CurrentFunction;
				PreviousLocation = C.CurrentEmitLocation;
				C.Scopes = std::move(Filtered);
				if (!WithLocals)
				{
					C.CurrentFunction = nullptr;
				}
				if (Environment && Environment->Expression)
				{
					C.CurrentEmitLocation = Environment->Expression->Location;
				}
				bActive = true;
			}
			~FConstantLexicalScope()
			{
				if (!bActive) return;
				Compiler.ConstantEvaluationState->MemoryBytes -= Memory;
				Compiler.Scopes = std::move(PreviousScopes);
				Compiler.LocalTypeAliasScopes = std::move(PreviousAliases);
				Compiler.CurrentFunction = PreviousFunction;
				Compiler.CurrentEmitLocation = PreviousLocation;
			}
		};
		// A deque keeps each borrowed declaration context stable throughout evaluation.
		std::deque<FTemplateEvaluationContext> Contexts;
		std::unordered_map<std::string, const FTemplateEvaluationContext *> DeclarationContexts;
		std::deque<FLocalConstantInitializer> LocalEnvironments;
		std::uint64_t ContextMemory = 0;
		struct FReleaseContextMemory
		{
			FConstantEvaluationState &State;
			std::uint64_t &Bytes;
			~FReleaseContextMemory()
			{
				State.MemoryBytes -= Bytes;
			}
		} ReleaseMemory{*ConstantEvaluationState, ContextMemory};
		using FBindings = std::unordered_map<std::string, std::string>;
		std::function<const FTemplateEvaluationContext *(const std::string &, const std::string &, const FBindings &,
		    const std::string &, bool, const std::string &, const FLocalConstantInitializer *)>
		    ContextFor;
		ContextFor = [&](const std::string &ReferenceModule, const std::string &ReferenceNamespace,
		                 const FBindings &ReferenceBindings, const std::string &ReferenceOwner, const bool WithLocals, const std::string &ReferenceAccess, const FLocalConstantInitializer *InputEnvironment) -> const FTemplateEvaluationContext *
		{
			std::string ContextKey;
			if (!WithLocals)
			{
				const auto Append = [&](const std::string &Text)
				{
					ContextKey += std::to_string(Text.size()) + ':' + Text;
				};
				Append(ReferenceModule);
				Append(ReferenceNamespace);
				Append(ReferenceOwner);
				Append(ReferenceAccess);
				std::vector<std::pair<std::string, std::string>> SortedBindings(ReferenceBindings.begin(), ReferenceBindings.end());
				std::sort(SortedBindings.begin(), SortedBindings.end());
				for (const auto &Binding : SortedBindings)
				{
					CheckAnalysisCancellation();
					Append(Binding.first);
					Append(Binding.second);
				}
				if (const auto Found = DeclarationContexts.find(ContextKey); Found != DeclarationContexts.end()) return Found->second;
			}
			const FLocalConstantInitializer *LocalEnvironment = nullptr;
			if (InputEnvironment)
			{
				LocalEnvironments.push_back(*InputEnvironment);
				LocalEnvironment = &LocalEnvironments.back();
			}
			const auto Bytes = std::uint64_t(1024 + ReferenceModule.size() + ReferenceNamespace.size() + ReferenceOwner.size() + ContextKey.size());
			std::uint64_t BindingBytes = 0;
			for (const auto &Binding : ReferenceBindings)
			{
				BindingBytes += 6 * (Binding.first.size() + Binding.second.size() + 64);
			}
			ContextMemory += Bytes + BindingBytes;
			ConstantEvaluationState->MemoryBytes += Bytes + BindingBytes;
			Contexts.emplace_back();
			auto *Context = &Contexts.back();
			if (!WithLocals)
			{
				DeclarationContexts.emplace(std::move(ContextKey), Context);
			}
			Context->RecordConstant = [&](std::string_view Identity, const FTemplateConstant &Value)
			{
				if (!Module) return;
				const auto Slot = Module->GlobalSlots.find(std::string(Identity));
				if (Slot != Module->GlobalSlots.end() && Slot->second < Module->GlobalMetadata.size() && Module->GlobalMetadata[Slot->second].bConstexpr)
				{
					Module->GlobalMetadata[Slot->second].ConstantValue = Value;
				}
			};
			Context->CheckWork = [](std::uint64_t)
			{
				CheckAnalysisCancellation();
				return true;
			};
			Context->ResolveType = [&, ReferenceModule, ReferenceNamespace, ReferenceBindings, ReferenceOwner, ReferenceAccess, LocalEnvironment,
			                           WithLocals](const std::string_view Type, FTemplateValueType &Out)
			{
				FConstantLexicalScope LexicalScope(*const_cast<VeroScriptCompiler *>(this), WithLocals, LocalEnvironment);
				if (!LexicalScope.bValid) return ETemplateEvaluationStatus::LimitExceeded;
				auto PreviousNamespace = CompilerNamespace;
				struct FRestoreNamespace
				{
					~FRestoreNamespace()
					{
						Target = std::move(Previous);
					}
					std::string &Target;
					std::string &Previous;
				} Restore{CompilerNamespace, PreviousNamespace};
				CompilerNamespace = ReferenceNamespace;
				auto &Compiler = *const_cast<VeroScriptCompiler *>(this);
				auto PreviousModule = Compiler.CurrentModulePath;
				FRestoreNamespace RestoreModule{Compiler.CurrentModulePath, PreviousModule};
				Compiler.CurrentModulePath = ReferenceModule;
				auto PreviousOwner = Compiler.CurrentOwnerTypeLinkage;
				auto PreviousAccess = Compiler.CurrentAccessFunctionLinkage;
				FRestoreNamespace RestoreOwner{Compiler.CurrentOwnerTypeLinkage, PreviousOwner};
				FRestoreNamespace RestoreAccess{Compiler.CurrentAccessFunctionLinkage, PreviousAccess};
				Compiler.CurrentOwnerTypeLinkage = ReferenceOwner;
				if (!WithLocals)
				{
					Compiler.CurrentAccessFunctionLinkage = ReferenceAccess;
				}
				if (SubstituteTypeNameWith(std::string(Type), ReferenceBindings).find("@type_parameter_") !=
				    std::string::npos)
				{
					return ETemplateEvaluationStatus::Dependent;
				}
				return ResolveTemplateValueType(std::string(Type), ReferenceModule, ReferenceBindings, Out)
				    ? ETemplateEvaluationStatus::Constant
				    : ETemplateEvaluationStatus::Invalid;
			};
			#include "ConstantEvaluation/VeroScriptConstantBinding.inl"
			Context->ResolveName = [&, ReferenceModule, ReferenceNamespace, ReferenceBindings, ReferenceOwner, ReferenceAccess, LocalEnvironment,
			                           WithLocals](const std::string_view WrittenName, const FSourceLocation &)
			{
				FConstantLexicalScope LexicalScope(*const_cast<VeroScriptCompiler *>(this), WithLocals, LocalEnvironment);
				if (!LexicalScope.bValid)
				{
					FTemplateConstantSymbol Failed;
					Failed.Status = ETemplateEvaluationStatus::LimitExceeded;
					return Failed;
				}
				FTemplateConstantSymbol Symbol;
				auto PreviousNamespace = CompilerNamespace;
				struct FRestoreNamespace
				{
					~FRestoreNamespace()
					{
						Target = std::move(Previous);
					}
					std::string &Target;
					std::string &Previous;
				} Restore{CompilerNamespace, PreviousNamespace};
				CompilerNamespace = ReferenceNamespace;
				auto &Compiler = *const_cast<VeroScriptCompiler *>(this);
				auto PreviousModule = Compiler.CurrentModulePath;
				FRestoreNamespace RestoreModule{Compiler.CurrentModulePath, PreviousModule};
				Compiler.CurrentModulePath = ReferenceModule;
				auto PreviousOwner = Compiler.CurrentOwnerTypeLinkage;
				auto PreviousAccess = Compiler.CurrentAccessFunctionLinkage;
				FRestoreNamespace RestoreOwner{Compiler.CurrentOwnerTypeLinkage, PreviousOwner};
				FRestoreNamespace RestoreAccess{Compiler.CurrentAccessFunctionLinkage, PreviousAccess};
				Compiler.CurrentOwnerTypeLinkage = ReferenceOwner;
				if (!WithLocals)
				{
					Compiler.CurrentAccessFunctionLinkage = ReferenceAccess;
				}
				std::string QualifiedName(WrittenName);
				bool LocalEnumerator = false;
				if (WithLocals)
				{
					const auto Count = LocalEnvironment ? (std::min)(LocalEnvironment->ScopeCount, Scopes.size()) : Scopes.size();
					for (std::size_t Index = Count; Index-- > 0;)
					{
						const auto &Scope = Scopes[Index];
						if (const auto Static = Scope.StaticLocals.find(std::string(WrittenName));
						    Static != Scope.StaticLocals.end() && !Scope.Locals.count(std::string(WrittenName)))
						{
							if (const auto Binding = LocalStaticConstants.find(Static->second); Binding != LocalStaticConstants.end())
							{
								if (Binding->second.Value)
								{
									Symbol.Status = ETemplateEvaluationStatus::Constant;
									Symbol.Value = *Binding->second.Value;
								}
								else
								{
									const auto &Declaration = Binding->second.Initializer;
									Symbol.Status = ETemplateEvaluationStatus::Constant;
									Symbol.Identity = Declaration.Identity;
									Symbol.Expression = Declaration.Expression;
									Symbol.DeclaredTypeName = Declaration.TypeName;
									Symbol.DeclarationContext = ContextFor(Declaration.ModulePath, Declaration.Namespace, Declaration.Bindings,
									    Declaration.Owner, true, Declaration.Access, &Declaration);
								}
								return Symbol;
							}
							const auto Global = Module->GlobalSlots.find(Static->second);
							if (Global != Module->GlobalSlots.end() && Global->second < Module->GlobalMetadata.size())
							{
								if (const auto &Constant = Module->GlobalMetadata[Global->second].ConstantValue)
								{
									Symbol.Status = ETemplateEvaluationStatus::Constant;
									Symbol.Value = *Constant;
								}
							}
							return Symbol;
						}
						const auto Slot = Scope.Locals.find(std::string(WrittenName));
						if (Slot == Scope.Locals.end() || (LocalEnvironment && Slot->second >= LocalEnvironment->SlotBound))
						{
							if (const auto Enum = Scope.EnumConstants.find(std::string(WrittenName)); Enum != Scope.EnumConstants.end())
							{
								QualifiedName = Enum->second;
								LocalEnumerator = true;
								break;
							}
							continue;
						}
						if (const auto Known = Scope.TemplateConstants.find(std::string(WrittenName)); Known != Scope.TemplateConstants.end())
						{
							Symbol.Status = ETemplateEvaluationStatus::Constant;
							Symbol.Value = Known->second;
							return Symbol;
						}
						if (const auto Pending = Scope.ConstantInitializers.find(std::string(WrittenName)); Pending != Scope.ConstantInitializers.end())
						{
							const auto &Declaration = Pending->second;
							Symbol.Status = ETemplateEvaluationStatus::Constant;
							Symbol.Identity = Declaration.Identity;
							Symbol.Expression = Declaration.Expression;
							Symbol.DeclaredTypeName = Declaration.TypeName;
							Symbol.DeclarationContext = ContextFor(Declaration.ModulePath, Declaration.Namespace, Declaration.Bindings, Declaration.Owner, true, Declaration.Access, &Declaration);
						}
						return Symbol;
					}
				}
				if (!LocalEnumerator && !ReferenceBindings.empty())
				{
					const auto Binding = ReferenceBindings.find(std::string(WrittenName));
					if (Binding != ReferenceBindings.end())
					{
						FTemplateArgument Argument;
						if (Binding->second.rfind("@dependent-value:", 0) == 0)
						{
							Symbol.Status = ETemplateEvaluationStatus::Dependent;
							ResolveTemplateValueType(Binding->second.substr(17), ReferenceModule, ReferenceBindings,
							    Symbol.Value.Type);
							return Symbol;
						}
						if (Binding->second.rfind("@value:", 0) == 0 &&
						    DecodeTemplateArgument(std::string_view(Binding->second).substr(7), Argument) &&
						    Argument.Kind == ETemplateParameterKind::Value)
						{
							Symbol.Status = ETemplateEvaluationStatus::Constant;
							Symbol.Value = Argument.Value;
						}
						return Symbol;
					}
				}
				if (!LocalEnumerator)
				{
					if (const auto *Enum = FindEnumDeclaration(ReferenceNamespace))
					{
						for (const auto &Member : Enum->Members)
						{
							if (Member.Name == WrittenName)
							{
								QualifiedName = ReferenceNamespace + "." + Member.Name;
								break;
							}
						}
					}
				}
				const std::string_view Name = QualifiedName;
				const auto BindEnumInitializer = [&](const FEnumDecl &Declaration, const FEnumMemberDecl &Member)
				{
					if (!Member.Initializer) return;
					const auto EnumIdentity = Symbol.Value.Type.Name;
					const bool WithinOwnInitializer = ReferenceAccess == "@enum-initializer:" + EnumIdentity;
					if (WithinOwnInitializer)
					{
						TryGetTemplateValueType(Declaration.UnderlyingTypeName, Symbol.Value.Type);
					}
					if (Declaration.bPrecompiledPackage && Module)
					{
						for (const auto &Enum : Module->Enums)
						{
							if (Enum.Name == EnumIdentity)
							{
								for (const auto &Value : Enum.Members)
								{
									if (Value.Name == Member.Name)
									{
										Symbol.Value.Bits = static_cast<std::uint64_t>(Value.Value);
										if (Symbol.Value.Type.Width < 64)
										{
											Symbol.Value.Bits &= (std::uint64_t{1} << Symbol.Value.Type.Width) - 1;
										}
										return;
									}
								}
							}
						}
					}
					const auto Cached = EnumConstants.find(EnumIdentity + "." + Member.Name);
					if (Cached != EnumConstants.end())
					{
						Symbol.Value.Bits = static_cast<std::uint64_t>(Cached->second);
						if (Symbol.Value.Type.Width < 64)
						{
							Symbol.Value.Bits &= (std::uint64_t{1} << Symbol.Value.Type.Width) - 1;
						}
						return;
					}
					Symbol.Expression = Member.Initializer.get();
					Symbol.DeclaredTypeName = Symbol.Value.Type.Name;
					Symbol.bExplicitConversion = true;
					Symbol.DeclarationGroup = EnumIdentity;
					Symbol.DeclarationOrder = static_cast<std::size_t>(&Member - Declaration.Members.data());
					Symbol.DeclarationContext = ContextFor(Declaration.ModulePath, Declaration.Name, ReferenceBindings, ReferenceOwner, false, "@enum-initializer:" + EnumIdentity, nullptr);
				};

				const auto Separator = Name.rfind('.');
				if (Separator != std::string_view::npos)
				{
					const std::string Owner(Name.substr(0, Separator));
					const std::string Member(Name.substr(Separator + 1));
					FTemplateValueType EnumType;
					if (ResolveTemplateValueType(Owner, ReferenceModule, ReferenceBindings, EnumType) && EnumType.bEnum)
					{
						const auto Set = [&](const std::int64_t Value)
						{
							Symbol.Status = ETemplateEvaluationStatus::Constant;
							Symbol.Identity = EnumType.Name + "." + Member;
							Symbol.Value = {EnumType, static_cast<std::uint64_t>(Value)};
							if (EnumType.Width != 64)
							{
								Symbol.Value.Bits &= (std::uint64_t{1} << EnumType.Width) - 1;
							}
						};
						if (const auto *Host =
						        CompilerDetail::FindHostTypeWithNamespaceSpelling(ActiveBindings, EnumType.Name))
						{
							for (const auto &Entry : Host->Members)
							{
								if (Entry.Kind == EHostMemberKind::EnumMember && Entry.Name == Member)
								{
									Set(Entry.EnumValue);
									return Symbol;
								}
							}
						}
						if (const auto *Declaration = FindEnumDeclaration(EnumType.Name))
						{
							for (const auto &Entry : Declaration->Members)
							{
								if (Entry.Name != Member)
								{
									continue;
								}
								Set(Entry.Value);
								BindEnumInitializer(*Declaration, Entry);
								for (const auto &Argument : DecodeGenericConcreteTypes(EnumType.Name))
								{
									FTemplateArgument Concrete;
									if (Argument.rfind("@value:", 0) != 0 ||
									    !DecodeTemplateArgument(std::string_view(Argument).substr(7), Concrete))
									{
										if (ReferenceBindings.count(Argument) ||
										    Argument.find("@type_parameter_") != std::string::npos ||
										    Argument.find("@dependent-value:") != std::string::npos)
										{
											Symbol.Status = ETemplateEvaluationStatus::Dependent;
										}
									}
								}
								return Symbol;
							}
						}
					}
				}
				const auto EnumLinkage = ResolveEnumConstantLinkageName(std::string(Name), ReferenceModule);
				const auto EnumConstant = EnumConstants.find(EnumLinkage);
				if (EnumConstant != EnumConstants.end())
				{
					// Unscoped runtime values use an integer ABI; template constants still
					// retain the enum declaration that owns the injected name.
					if (ActiveProgram)
					{
						for (const auto &Declaration : ActiveProgram->Enums)
						{
							if (Declaration.bScoped || !EnclosingTemplateParameterNames(Declaration.Scope).empty())
							{
								continue;
							}
							for (const auto &Member : Declaration.Members)
							{
								const auto Dot = Declaration.Name.rfind('.');
								const auto Injected = Dot == std::string::npos
								    ? Member.Name
								    : Declaration.Name.substr(0, Dot + 1) + Member.Name;
								if ((GetSymbolLinkageName(Declaration.ModulePath, Declaration.bExported, Injected) ==
								            EnumLinkage ||
								        GetSymbolLinkageName(Declaration.ModulePath, Declaration.bExported,
								            Declaration.Name + "." + Member.Name) == EnumLinkage) &&
								    ResolveTemplateValueType(GetSymbolLinkageName(Declaration.ModulePath,
								                                 Declaration.bExported,
								                                 Declaration.Name),
								        ReferenceModule, {}, Symbol.Value.Type))
								{
									Symbol.Status = ETemplateEvaluationStatus::Constant;
									Symbol.Identity = EnumLinkage;
									Symbol.Value.Bits = static_cast<std::uint64_t>(Member.Value);
									if (Symbol.Value.Type.Width != 64)
									{
										Symbol.Value.Bits &= (std::uint64_t{1} << Symbol.Value.Type.Width) - 1;
									}
									BindEnumInitializer(Declaration, Member);
									return Symbol;
								}
							}
						}
					}
					const auto Type = EnumConstantTypeNames.find(EnumLinkage);
					if (Type != EnumConstantTypeNames.end() &&
					    ResolveTemplateValueType(Type->second, ReferenceModule, {}, Symbol.Value.Type))
					{
						Symbol.Status = ETemplateEvaluationStatus::Constant;
						Symbol.Identity = EnumLinkage;
						Symbol.Value.Bits = static_cast<std::uint64_t>(EnumConstant->second);
						if (Symbol.Value.Type.Width != 64)
						{
							Symbol.Value.Bits &= (std::uint64_t{1} << Symbol.Value.Type.Width) - 1;
						}
					}
					return Symbol;
				}
				// Read the declaration initializer, never VM globals or a native callback.
				// The field's own closed environment controls nested constant lookup.
				std::string FieldOwner = ReferenceOwner;
				std::string FieldName(Name);
				if (Separator != std::string_view::npos)
				{
					FieldOwner = QualifyTypeName(
					    SubstituteTypeNameWith(std::string(Name.substr(0, Separator)), ReferenceBindings),
					    ReferenceModule);
					FieldName = Name.substr(Separator + 1);
				}
				std::string FieldGlobal, FieldType;
				bool FieldConst = false;
				if (!FieldOwner.empty() &&
				    FindStaticFieldOnTypeLinkage(FieldOwner, FieldName, FieldGlobal, FieldType, FieldConst))
				{
					if (!ValidateMemberAccess(Expression.Location, FieldOwner, FieldName, false))
					{
						Symbol.Status = ETemplateEvaluationStatus::Invalid;
						return Symbol;
					}
					if (FieldConst && Module)
					{
						const auto Slot = Module->GlobalSlots.find(FieldGlobal);
						if (Slot != Module->GlobalSlots.end() && Slot->second < Module->GlobalMetadata.size())
						{
							if (const auto &Value = Module->GlobalMetadata[Slot->second].ConstantValue)
							{
								Symbol.Status = ETemplateEvaluationStatus::Constant;
								Symbol.Identity = FieldGlobal;
								Symbol.Value = *Value;
								return Symbol;
							}
						}
					}
					const auto InspectFields = [&](const auto &Declarations)
					{
						for (const auto &Declaration : Declarations)
						{
							const auto Dot = FieldGlobal.rfind('.');
							const auto DeclaringOwner = FieldGlobal.substr(0, Dot);
							const auto Base = GenericTemplateBaseLinkage(DeclaringOwner);
							if (GetSymbolLinkageName(Declaration.ModulePath, Declaration.bExported, Declaration.Name) !=
							    (Base.empty() ? DeclaringOwner : Base))
							{
								continue;
							}
							const auto Arguments = DecodeGenericConcreteTypes(DeclaringOwner);
							const auto Names = TemplateParameterNames(Declaration);
							if (Arguments.size() != Names.size())
							{
								continue;
							}
							FBindings FieldBindings;
							for (std::size_t Index = 0; Index < Names.size(); ++Index)
							{
								FieldBindings.emplace(Names[Index], Arguments[Index]);
							}
							for (const auto &Field : Declaration.Fields)
							{
								if (Field.Name != FieldName || !Field.bIsStatic)
								{
									continue;
								}
								if (!FieldConst || !Field.DefaultValue)
								{
									return true;
								}
								Symbol.Status = ETemplateEvaluationStatus::Constant;
								Symbol.Identity = FieldGlobal;
								Symbol.Expression = Field.DefaultValue.get();
								Symbol.DeclaredTypeName = SubstituteTypeNameWith(Field.TypeName, FieldBindings);
								Symbol.DeclarationContext = ContextFor(Declaration.ModulePath, Declaration.Name,
								    FieldBindings, DeclaringOwner, false, {}, nullptr);
								return true;
							}
						}
						return false;
					};
					if (ActiveProgram &&
					    (InspectFields(ActiveProgram->Classes) || InspectFields(ActiveProgram->Structs) ||
					        InspectFields(ActiveProgram->Interfaces)))
					{
						return Symbol;
					}
				}
				const auto Global = ResolveGlobalLinkageName(std::string(Name), ReferenceModule);
				const auto Inspect = [&](const FVarDeclStmt &Variable)
				{
					if (!Variable.bIsConst || (!Variable.Initializer && !Variable.bPrecompiledPackage) || Variable.bIsRef ||
					    GetSymbolLinkageName(Variable.ModulePath, Variable.bExported, Variable.Name) != Global)
					{
						return false;
					}
					Symbol.Status = ETemplateEvaluationStatus::Constant;
					Symbol.Identity = Global;
					if (Variable.bConstexpr && Module)
					{
						const auto Slot = Module->GlobalSlots.find(Global);
						if (Slot != Module->GlobalSlots.end() && Slot->second < Module->GlobalMetadata.size())
						{
							if (const auto &Value = Module->GlobalMetadata[Slot->second].ConstantValue)
							{
								Symbol.Value = *Value;
								return true;
							}
						}
					}
					if (!Variable.Initializer) return false;
					Symbol.Expression = Variable.Initializer.get();
					Symbol.DeclaredTypeName = Variable.TypeName == "auto" ? std::string{} : Variable.TypeName;
					Symbol.DeclarationContext =
					    ContextFor(Variable.ModulePath,
					        Variable.Name.find_last_of('.') == std::string::npos
					            ? std::string{}
					            : Variable.Name.substr(0, Variable.Name.find_last_of('.')),
					        {}, {}, false, {}, nullptr);
					return true;
				};
				if (ActiveProgram)
				{
					for (const auto &Statement : ActiveProgram->TopLevelStatements)
					{
						if (Statement->Kind == EStmtKind::VarDecl &&
						    Inspect(static_cast<const FVarDeclStmt &>(*Statement)))
						{
							return Symbol;
						}
						if (Statement->Kind == EStmtKind::VarDeclGroup)
						{
							for (const auto &Variable : static_cast<const FVarDeclGroupStmt &>(*Statement).Declarations)
							{
								if (Inspect(*Variable))
								{
									return Symbol;
								}
							}
						}
					}
				}
				return Symbol;
			};
			return Context;
		};
		std::string Owner = (AllowLocals || Validation) ? CurrentOwnerTypeLinkage : std::string{};
		if (Owner.empty() && !AllowLocals && ActiveProgram)
		{
			const auto FindOwner = [&](const auto &Declarations)
			{
				for (const auto &Declaration : Declarations)
				{
					if (Declaration.Name != Namespace || Declaration.ModulePath != ModulePath)
					{
						continue;
					}
					Owner = GetSymbolLinkageName(ModulePath, Declaration.bExported, Declaration.Name);
					std::vector<std::string> Arguments;
					for (const auto &Name : TemplateParameterNames(Declaration))
					{
						const auto Bound = Bindings.find(Name);
						if (Bound == Bindings.end())
						{
							return;
						}
						Arguments.push_back(Bound->second);
					}
					if (!Arguments.empty())
					{
						Owner = MangleGenericInstanceName(Owner, Arguments);
					}
					return;
				}
			};
			FindOwner(ActiveProgram->Classes);
			FindOwner(ActiveProgram->Structs);
			FindOwner(ActiveProgram->Interfaces);
		}
		const auto *Context = ContextFor(ModulePath, Namespace, Bindings, Owner, AllowLocals, (AllowLocals || Validation) ? CurrentAccessFunctionLinkage : std::string{}, nullptr);
		if (Validation)
		{
			auto Function = *Validation;
			Function.DeclarationContext = Context;
			return ValidateConstantFunction(Function, *Context);
		}
		return EvaluateTemplateConstant(Expression, *Context);
	}

	bool VeroScriptCompiler::CompleteTemplateArguments(
	    const FTemplateParameterList &Parameters, const std::string &DeclarationModule,
	    std::vector<std::string> &Arguments, const FSourceLocation &Location, const bool Report,
	    const std::unordered_map<std::string, std::string> *EnclosingBindings, const bool TypesAreCanonical) const
	{
		auto &CompilerNamespace = const_cast<VeroScriptCompiler *>(this)->CurrentNamespacePrefix;
		const auto &Records = Parameters.GetParameters();
		if (Arguments.size() > Records.size())
		{
			return false;
		}
		// Defaults belong to the declaration, not the caller's template scope.
		// Captured parameters are supplied by the schema or explicit owner bindings.
		auto Bindings = EnclosingBindings ? *EnclosingBindings : std::unordered_map<std::string, std::string>{};
		auto Completed = Arguments;
		Completed.resize(Records.size());
		for (std::size_t Index = 0; Index < Records.size(); ++Index)
		{
			const auto &Parameter = Records[Index];
			auto &Argument = Completed[Index];
			const bool InheritedDefault = Argument.rfind("@default:", 0) == 0;
			const bool Default = Argument.empty() || InheritedDefault;
			auto PreviousNamespace = CompilerNamespace;

			struct FRestoreNamespace
			{
				~FRestoreNamespace()
				{
					Target = std::move(Previous);
				}

				std::string &Target;
				std::string &Previous;
			} Restore{CompilerNamespace, PreviousNamespace};

			if (Default)
			{
				CompilerNamespace = Parameter.Namespace;
			}
			if (Default)
			{
				if ((Index < Parameters.GetEnclosingCount() && !InheritedDefault) || !Parameter.HasDefault())
				{
					return false;
				}
				Argument = Parameter.Kind == ETemplateParameterKind::Type ? Parameter.DefaultTypeName
				                                                          : Parameter.DefaultSource;
			}
			// Explicit arguments retain the caller's bindings; defaults see only
			// their declaration's enclosing and already completed parameters.
			const auto &ReferenceBindings = Default ? Bindings : ActiveTypeSubst;
			FTemplateBindingScope Scope(const_cast<VeroScriptCompiler *>(this)->ActiveTypeSubst, ReferenceBindings, Default);
			if (Parameter.Kind == ETemplateParameterKind::Type)
			{
				if (!Default && TypesAreCanonical)
				{
					// Inference and decoded instances already carry resolved identities.
					// Reapplying the caller's substitution can change a same-named type.
					Argument = CanonicalTypeNameForCoercion(Argument);
				}
				else
				{
					const auto ReferenceModule = Default ? DeclarationModule : CurrentModulePath;
					const auto Type = SubstituteTypeNameWith(Argument, ReferenceBindings);
					FTemplateValueType Enum;
					Argument = ResolveTemplateValueType(Type, ReferenceModule, ReferenceBindings, Enum) && Enum.bEnum
					    ? Enum.Name
					    : CanonicalTypeNameForCoercion(QualifyTypeName(Type, ReferenceModule));
				}
				if (Argument.empty() || Argument.rfind("@value:", 0) == 0)
				{
					return false;
				}
			}
			else
			{
				FTemplateValueType Type;
				if (!ResolveTemplateValueType(Parameter.ValueTypeName, DeclarationModule, Bindings, Type))
				{
					if (Report)
					{
						ReportError("template value parameter requires an integral, bool, or enum type", Parameter.Location);
					}
					return false;
				}
				auto Result = EvaluateTemplateSource(Argument, Default ? DeclarationModule : CurrentModulePath,
				    Default ? Bindings : ActiveTypeSubst, Location,
				    Default ? &Parameter.Namespace : nullptr, !Default);
				if (Result.Succeeded())
				{
					Result = ConvertTemplateConstant(Result.Value, Type, false, Location);
				}
				if (!Result.Succeeded())
				{
					if (Report)
					{
						ReportError("invalid template value argument '" + Parameter.Name + "': " + Result.Reason,
						    Location);
					}
					return false;
				}
				FTemplateArgument Value;
				Value.Kind = ETemplateParameterKind::Value;
				Value.Value = Result.Value;
				Argument = "@value:" + EncodeTemplateArgument(Value);
			}
			Bindings[Parameter.Name] = Argument;
		}
		Arguments = std::move(Completed);
		return true;
	}
} // namespace VeroScript
