// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "ConstantEvaluation/VeroScriptConstantBranches.h"
#include "VeroScriptConstantEvaluation.h"
#include "VeroScriptEnumValueIdentity.h"
#include "VeroScriptCompilerVariantInternal.h"
#include "VeroScriptCompilerAnalysis.h"
#include "VeroScriptBytecodeLinker.h"

#include "VeroScriptAst.h"
#include "VeroScriptBuiltins.h"
#include "VeroScriptMemberAccess.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptPackageInterface.h"
#include "VeroScriptRuntime.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <tuple>
#include <unordered_set>
#include <vector>

namespace VeroScript
{
	bool VeroScriptCompiler::CompileStatement(const FStmt& Statement)
	{
		if (bHasError)
		{
			return false;
		}

		CurrentEmitLocation = Statement.Location;
		FEmitter Emitter = MakeEmitter();

		switch (Statement.Kind)
		{
		case EStmtKind::Expr:
			{
				if (!CompileExpression(*static_cast<const FExprStmt&>(Statement).Expression))
				{
					return false;
				}
				Emitter.Emit(EOpcode::Pop);
				return true;
			}
		case EStmtKind::Block:
			return CompileBlock(static_cast<const FBlockStmt&>(Statement));
		case EStmtKind::TypeAlias:
			{
				if (LocalTypeAliasScopes.empty())
				{
					ReportError("type alias outside scope", Statement.Location);
					return false;
				}
				const FTypeAliasStmt& AliasStmt = static_cast<const FTypeAliasStmt&>(Statement);
				if (AliasStmt.bTypeDefinition && ConstantBranchState && ActiveProgram)
				{
					const auto Instantiate = [&](const auto& Declaration, const auto& Ensure)
					{
						if (Declaration.Name != AliasStmt.TypeName || !ConstantBranchState->Guarded(Declaration.Scope.ScopeId) || Declaration.TemplateParameters.GetOwnCount() != 0)
						{
							return true;
						}
						std::vector<std::string> Arguments;
						for (const auto& Name : TemplateParameterNames(Declaration))
						{
							const auto Bound = ActiveTypeSubst.find(Name);
							if (Bound == ActiveTypeSubst.end()) return true;
							Arguments.push_back(Bound->second);
						}
						std::uint16_t Index = 0;
						return Ensure(Declaration, Arguments, Index, Statement.Location);
					};
					for (const auto& Declaration : ActiveProgram->Structs)
					{
						if (!Instantiate(Declaration, [this](const auto& D, const auto& A, auto& I, const auto& L)
						{
							return EnsureStructInstantiation(D, A, I, L);
						}))
						{
							return false;
						}
					}
					for (const auto& Declaration : ActiveProgram->Classes)
					{
						if (!Instantiate(Declaration, [this](const auto& D, const auto& A, auto& I, const auto& L)
						{
							return EnsureClassInstantiation(D, A, I, L);
						}))
						{
							return false;
						}
					}
					for (const auto& Declaration : ActiveProgram->Interfaces)
					{
						if (!Instantiate(Declaration, [this](const auto& D, const auto& A, auto& I, const auto& L)
						{
							return EnsureInterfaceInstantiation(D, A, I, L);
						}))
						{
							return false;
						}
					}
				}
				if (CompilerDetail::TypeNameMentionsAuto(AliasStmt.TypeName))
				{
					ReportErrorId("diag.auto_not_valid_type_here", Statement.Location);
					return false;
				}
				// Bind the current instantiation before later member lookup expands
				// the alias; the written template parameter is not a concrete owner.
				const std::string AliasTarget = SubstituteTypeName(AliasStmt.TypeName);
				if (!LocalTypeAliasScopes.back().emplace(AliasStmt.Name, AliasTarget).second)
				{
					ReportError(
					    "duplicate type alias in this scope: " + AliasStmt.Name,
					    Statement.Location);
					return false;
				}
				if (AliasStmt.bTypeDefinition && !Scopes.empty())
				{
					const auto* Enum = FindEnumDeclaration(AliasStmt.TypeName);
					std::string EnumLinkage;
					if (Enum)
					{
						std::vector<std::string> Arguments;
						for (const auto& Name : EnclosingTemplateParameterNames(Enum->Scope))
						{
							if (const auto Bound = ActiveTypeSubst.find(Name); Bound != ActiveTypeSubst.end())
							{
								Arguments.push_back(Bound->second);
							}
						}
						EnumLinkage = EnsureEnumMetadata(*Enum, Arguments);
						if (EnumLinkage.empty()) return false;
					}
					if (Enum && !Enum->bScoped)
					{
						for (const auto& Member : Enum->Members)
						{
							if (const auto Bound = ActiveTypeSubst.find(Member.Name);
							    Bound != ActiveTypeSubst.end() && Bound->second.rfind("@value:", 0) == 0)
							{
								ReportError("an enum member cannot shadow a template value parameter: " + Member.Name, Member.Location);
								return false;
							}
							const auto Linkage = EnumLinkage + "." + Member.Name;
							if (Scopes.back().Locals.count(Member.Name) != 0 || Scopes.back().StaticLocals.count(Member.Name) != 0 || LocalTypeAliasScopes.back().count(Member.Name) != 0 || !Scopes.back().EnumConstants.emplace(Member.Name, Linkage).second)
							{
								ReportError("duplicate local enumerator: " + Member.Name, Statement.Location);
								return false;
							}
						}
					}
				}
				return true;
			}
		case EStmtKind::VarDeclGroup:
			{
				const FVarDeclGroupStmt& Group = static_cast<const FVarDeclGroupStmt&>(Statement);
				for (const std::unique_ptr<FVarDeclStmt>& Declaration : Group.Declarations)
				{
					if (!Declaration)
					{
						ReportError("invalid variable declaration group", Statement.Location);
						return false;
					}
					// Deliberately compile each declaration in the current scope instead
					// of using CompileBlock: a preceding name is visible to the next
					// initializer, and the group itself introduces no lifetime boundary.
					if (!CompileStatement(*Declaration))
					{
						return false;
					}
				}
				return true;
			}
		case EStmtKind::VarDecl:
			{
				const auto& SourceDeclaration = static_cast<const FVarDeclStmt&>(Statement);
				if (SourceDeclaration.bIsLocalStatic) return CompileLocalStatic(SourceDeclaration, Emitter);
				FLambdaInitializerContext InitializerContext(*this);
				if (!CurrentFunction && Scopes.empty())
				{
					CurrentInitializerStatement = &SourceDeclaration;
					CurrentInitializerExpression = nullptr;
					CurrentInitializerField = nullptr;
				}
				std::unique_ptr<FVarDeclStmt> LoweredDeclaration;
				std::optional<FTemplateConstant> RequiredConstant;
				if (SourceDeclaration.bConstexpr)
				{
					if (!SourceDeclaration.bIsConst || SourceDeclaration.bIsRef || !SourceDeclaration.Initializer)
					{
						ReportError("constexpr requires an initialized, readonly scalar value", Statement.Location);
						return false;
					}
					FTemplateEvaluationResult Constant;
					if (CurrentFunction == nullptr && Scopes.empty())
					{
						const auto Name = SourceDeclaration.ModulePath.empty() ? SourceDeclaration.Name : GetSymbolLinkageName(SourceDeclaration.ModulePath, SourceDeclaration.bExported, SourceDeclaration.Name);
						const auto Slot = Module->GlobalSlots.find(Name);
						if (Slot != Module->GlobalSlots.end() && Slot->second < Module->GlobalMetadata.size())
						{
							if (const auto& Known = Module->GlobalMetadata[Slot->second].ConstantValue)
							{
								Constant.Status = ETemplateEvaluationStatus::Constant;
								Constant.Value = *Known;
							}
						}
					}
					if (!Constant.Succeeded())
					{
						Constant = EvaluateTemplateExpression(*SourceDeclaration.Initializer, CurrentModulePath, ActiveTypeSubst, CurrentNamespacePrefix, true);
					}
					FTemplateValueType Type = Constant.Value.Type;
					if (Constant.Succeeded() && SourceDeclaration.TypeName != "auto")
					{
						if (!ResolveTemplateValueType(SourceDeclaration.TypeName, CurrentModulePath, ActiveTypeSubst, Type))
						{
							ReportError("unsupported constexpr variable type", Statement.Location);
							return false;
						}
						Constant = ConvertTemplateConstant(Constant.Value, Type, false, Statement.Location);
					}
					if (!Constant.Succeeded())
					{
						ReportError("invalid constexpr initializer: " + Constant.Reason, Statement.Location);
						return false;
					}
					RequiredConstant = Constant.Value;
					Module->CompiledConstants.push_back({SourceDeclaration.Name, SourceDeclaration.Location, Constant.Value});
					LoweredDeclaration = std::make_unique<FVarDeclStmt>(SourceDeclaration.TypeName, SourceDeclaration.Name, MakeConstantExpression(Constant.Value, Statement.Location));
					LoweredDeclaration->bIsConst = true;
					LoweredDeclaration->bConstexpr = true;
					LoweredDeclaration->bExported = SourceDeclaration.bExported;
					LoweredDeclaration->ModulePath = SourceDeclaration.ModulePath;
					LoweredDeclaration->Location = SourceDeclaration.Location;
				}
				const FVarDeclStmt& VarDecl = LoweredDeclaration ? *LoweredDeclaration : SourceDeclaration;
				// Descendants can emit before their root expression (for example,
				// a call argument or unary operand on the next line). Keep the
				// declaration pause point ahead of any initializer opcode.
				if (VarDecl.Initializer && ExpressionCrossesSourceLine(*VarDecl.Initializer, Statement.Location))
				{
					CurrentEmitLocation = Statement.Location;
					Emitter.Emit(EOpcode::Nop);
				}
				const std::string EffectiveTypeName = SubstituteTypeName(VarDecl.TypeName);
				const bool bInferAuto = EffectiveTypeName == "auto";
				if (bInferAuto)
				{
					if (!VarDecl.Initializer)
					{
						ReportErrorId(
						    "diag.auto_variable_requires_an_initializer",
						    Statement.Location);
						return false;
					}
				}
				else if (!ValidateFieldTypeName(EffectiveTypeName, Statement.Location))
				{
					return false;
				}
				const std::string QualifiedTypeName = NormalizeTypeAlias(
				    QualifyTypeName(EffectiveTypeName, CurrentModulePath));
				if (QualifiedTypeName.find('[') != std::string::npos)
				{
					Module->CompiledDeclarationTypes.push_back({SourceDeclaration.Name, SourceDeclaration.Location, QualifiedTypeName});
				}
				if (CompilerDetail::IsVariantIntrinsicName(VarDecl.Name))
				{
					std::string BoundType = QualifiedTypeName;
					if (bInferAuto && VarDecl.Initializer)
					{
						TryResolveExprTypeName(*VarDecl.Initializer, BoundType);
					}
					if (Runtime::IsFunctionTypeName(BoundType))
					{
						ReportErrorId("diag.variant_reserved_name", Statement.Location);
						return false;
					}
				}
				const bool bBraceContainerInit = VarDecl.Initializer && ((VarDecl.Initializer->Kind == EExprKind::ArrayInit && static_cast<const FArrayInitExpr&>(*VarDecl.Initializer).TypeName.empty()) || (VarDecl.Initializer->Kind == EExprKind::MapInit && static_cast<const FMapInitExpr&>(*VarDecl.Initializer).TypeName.empty()));
				if (VarDecl.bIsRef)
				{
					if (CurrentFunction == nullptr && Scopes.empty())
					{
						ReportError("reference variables are currently local-only", Statement.Location);
						return false;
					}
					if (!VarDecl.Initializer)
					{
						ReportError("reference variable requires an initializer", Statement.Location);
						return false;
					}
					std::string StoredTypeName = QualifiedTypeName;
					std::string InitializerType;
					if (!TryResolveExprRawTypeName(*VarDecl.Initializer, InitializerType))
					{
						ReportError("could not resolve reference initializer type", Statement.Location);
						return false;
					}
					InitializerType = CanonicalTypeNameForCoercion(InitializerType);
					if (bInferAuto)
					{
						StoredTypeName.assign(
						    Runtime::StripReferenceTypeName(InitializerType));
					}
					StoredTypeName = CanonicalTypeNameForCoercion(StoredTypeName);
					if (StoredTypeName.empty() || StoredTypeName == "auto" || StoredTypeName == "void" || !ValidateFieldTypeName(StoredTypeName, Statement.Location))
					{
						ReportError("invalid reference variable type", Statement.Location);
						return false;
					}
					const std::string InitializerValueType(
					    Runtime::StripReferenceTypeName(InitializerType));
					const bool bExactType = StoredTypeName == InitializerValueType;
					const bool bReferenceReturningExpression =
					    Runtime::IsReferenceTypeName(InitializerType);
					const bool bAddressable =
					    VarDecl.Initializer->Kind == EExprKind::Identifier || VarDecl.Initializer->Kind == EExprKind::MemberAccess || VarDecl.Initializer->Kind == EExprKind::IndexAccess || bReferenceReturningExpression;
					if (!VarDecl.bIsConst)
					{
						if (!bAddressable)
						{
							ReportError("mutable reference cannot bind to a temporary value", Statement.Location);
							return false;
						}
						if (!bExactType)
						{
							ReportErrorId(
							    "diag.ref_argument_type_mismatch_expected_got",
							    Statement.Location,
							    {DisplayTypeName(StoredTypeName), DisplayTypeName(InitializerType)});
							return false;
						}
						if (!CompileRefArgument(Emitter, *VarDecl.Initializer, Statement.Location, true))
						{
							return false;
						}
					}
					else if (bAddressable && bExactType)
					{
						if (!CompileRefArgument(Emitter, *VarDecl.Initializer, Statement.Location, false))
						{
							return false;
						}
					}
					else
					{
						const std::string PreviousExpectedType = ExpressionExpectedType;
						ExpressionExpectedType = StoredTypeName;
						const bool bCompiled = CompileExpression(*VarDecl.Initializer);
						const bool bCoerced = bCompiled && EmitValueCoercionForTarget(Emitter, StoredTypeName, *VarDecl.Initializer, Statement.Location);
						ExpressionExpectedType = PreviousExpectedType;
						if (!bCompiled || !bCoerced)
						{
							return false;
						}
						if (NeedsPassByValueCopy(StoredTypeName) && !CanTransferCoercedValue(*VarDecl.Initializer, StoredTypeName))
						{
							EmitValueCopy(Emitter, StoredTypeName);
						}
					}

					const std::uint16_t Slot = AllocateLocal(VarDecl.Name);
					if (!Scopes.empty())
					{
						Scopes.back().LocalTypes.emplace(VarDecl.Name, StoredTypeName);
						Scopes.back().LocalRefs.insert(VarDecl.Name);
						if (VarDecl.bIsConst)
						{
							Scopes.back().LocalConsts.insert(VarDecl.Name);
						}
					}
					FLocalInitMarker Marker;
					Marker.Name = VarDecl.Name;
					Marker.Offset = Emitter.CurrentOffset();
					Marker.ScopeDepth = static_cast<int>(Scopes.size());
					LocalInitMarkers.push_back(std::move(Marker));
					Emitter.EmitU16Operand(EOpcode::BindLocal, Slot);
					return true;
				}
				if ((Runtime::IsDynamicArrayTypeName(QualifiedTypeName) || Runtime::IsCommonContainerTypeName(QualifiedTypeName)) && VarDecl.Initializer && !bBraceContainerInit && !ValidateDynamicArrayAssignment(QualifiedTypeName, *VarDecl.Initializer, Statement.Location))
				{
					return false;
				}
				if (Runtime::IsNativeArrayTypeName(QualifiedTypeName))
				{
					if (VarDecl.Initializer)
					{
						if (bBraceContainerInit && VarDecl.Initializer->Kind == EExprKind::ArrayInit)
						{
							if (!CompileNativeArrayInit(
							        QualifiedTypeName,
							        static_cast<const FArrayInitExpr&>(*VarDecl.Initializer),
							        Emitter,
							        Statement.Location))
							{
								return false;
							}
						}
						else
						{
							// Same-typed T[N] expression: deep-copied by CloneStruct below.
							if (!ValidateDynamicArrayAssignment(
							        QualifiedTypeName,
							        *VarDecl.Initializer,
							        Statement.Location))
							{
								return false;
							}
							if (!CompileExpression(*VarDecl.Initializer))
							{
								return false;
							}
						}
					}
					else
					{
						if (!EmitDefaultInitializedValue(Emitter, QualifiedTypeName, Statement.Location)) return false;
					}
				}
				else if (Runtime::IsDynamicArrayTypeName(QualifiedTypeName) || Runtime::IsSetTypeName(QualifiedTypeName) || Runtime::IsMultiSetTypeName(QualifiedTypeName) || Runtime::IsOrderedMultiSetTypeName(QualifiedTypeName) || Runtime::IsDequeTypeName(QualifiedTypeName) || Runtime::IsQueueTypeName(QualifiedTypeName) || Runtime::IsStackTypeName(QualifiedTypeName) || Runtime::IsChannelTypeName(QualifiedTypeName))
				{
					if (bBraceContainerInit && VarDecl.Initializer->Kind == EExprKind::ArrayInit)
					{
						if (Runtime::IsChannelTypeName(QualifiedTypeName))
						{
							ReportError("channel does not support brace initializers", Statement.Location);
							return false;
						}
						if (!CompileContainerListInit(
						        QualifiedTypeName,
						        static_cast<const FArrayInitExpr&>(*VarDecl.Initializer),
						        Emitter,
						        Statement.Location))
						{
							return false;
						}
					}
					else if (VarDecl.Initializer)
					{
						const std::string PreviousExpectedType = ExpressionExpectedType;
						ExpressionExpectedType = QualifiedTypeName;
						const bool bCompiled = CompileExpression(*VarDecl.Initializer);
						const bool bCoerced = bCompiled && EmitValueCoercionForTarget(Emitter, QualifiedTypeName, *VarDecl.Initializer, Statement.Location);
						ExpressionExpectedType = PreviousExpectedType;
						if (!bCompiled || !bCoerced)
						{
							return false;
						}
					}
					else if (Runtime::IsDynamicArrayTypeName(QualifiedTypeName))
					{
						Emitter.EmitU16Operand(EOpcode::NewDynamicArray, AddString(QualifiedTypeName));
					}
					else if (Runtime::IsSetTypeName(QualifiedTypeName))
					{
						Emitter.EmitU16Operand(EOpcode::NewSet, AddString(QualifiedTypeName));
					}
					else if (Runtime::IsMultiSetTypeName(QualifiedTypeName))
					{
						Emitter.EmitU16Operand(EOpcode::NewMap, AddString(QualifiedTypeName));
					}
					else if (Runtime::IsOrderedMultiSetTypeName(QualifiedTypeName))
					{
						Emitter.EmitU16Operand(
						    EOpcode::NewOrderedMap,
						    AddString(QualifiedTypeName));
					}
					else if (Runtime::IsDequeTypeName(QualifiedTypeName))
					{
						Emitter.EmitU16Operand(EOpcode::NewDeque, AddString(QualifiedTypeName));
					}
					else if (Runtime::IsQueueTypeName(QualifiedTypeName))
					{
						Emitter.EmitU16Operand(EOpcode::NewQueue, AddString(QualifiedTypeName));
					}
					else if (Runtime::IsChannelTypeName(QualifiedTypeName))
					{
						Emitter.EmitU16Operand(EOpcode::NewChannel, AddString(QualifiedTypeName));
					}
					else
					{
						Emitter.EmitU16Operand(EOpcode::NewStack, AddString(QualifiedTypeName));
					}
				}
				else if (Runtime::IsMapTypeName(QualifiedTypeName) || Runtime::IsOrderedMapTypeName(QualifiedTypeName))
				{
					if (bBraceContainerInit && VarDecl.Initializer->Kind == EExprKind::MapInit)
					{
						if (!CompileMapInit(
						        QualifiedTypeName,
						        static_cast<const FMapInitExpr&>(*VarDecl.Initializer),
						        Emitter,
						        Statement.Location))
						{
							return false;
						}
					}
					else if (bBraceContainerInit && VarDecl.Initializer->Kind == EExprKind::ArrayInit)
					{
						ReportError("map initializer requires key:value entries", Statement.Location);
						return false;
					}
					else if (VarDecl.Initializer)
					{
						const std::string PreviousExpectedType = ExpressionExpectedType;
						ExpressionExpectedType = QualifiedTypeName;
						const bool bCompiled = CompileExpression(*VarDecl.Initializer);
						const bool bCoerced = bCompiled && EmitValueCoercionForTarget(Emitter, QualifiedTypeName, *VarDecl.Initializer, Statement.Location);
						ExpressionExpectedType = PreviousExpectedType;
						if (!bCompiled || !bCoerced)
						{
							return false;
						}
					}
					else if (Runtime::IsMapTypeName(QualifiedTypeName))
					{
						Emitter.EmitU16Operand(EOpcode::NewMap, AddString(QualifiedTypeName));
					}
					else
					{
						Emitter.EmitU16Operand(EOpcode::NewOrderedMap, AddString(QualifiedTypeName));
					}
				}
				else if (QualifiedTypeName == "variant" && !VarDecl.Initializer)
				{
					Emitter.EmitCallBuiltin(EBuiltinId::VariantEmpty, 0);
				}
				else if (VarDecl.Initializer)
				{
					const std::string PreviousExpectedType = ExpressionExpectedType;
					ExpressionExpectedType = QualifiedTypeName;
					const bool bCompiled = CompileExpression(*VarDecl.Initializer);
					const bool bCoerced = bCompiled && EmitValueCoercionForTarget(Emitter, QualifiedTypeName, *VarDecl.Initializer, Statement.Location);
					ExpressionExpectedType = PreviousExpectedType;
					if (!bCompiled || !bCoerced)
					{
						return false;
					}
				}
				else
				{
					ReportError("variable must be initialized", Statement.Location);
					return false;
				}
				std::string StoredTypeName = NormalizeTypeAlias(QualifiedTypeName);
				if (bInferAuto)
				{
					TryResolveExprTypeName(*VarDecl.Initializer, StoredTypeName);
					// Keep already-resolved export names; do not rebind to a local private.
					StoredTypeName = CanonicalTypeNameForCoercion(StoredTypeName);
					if (StoredTypeName.empty() || StoredTypeName == "auto" || StoredTypeName == "void")
					{
						ReportErrorId(
						    "diag.cannot_infer_type_for_auto_variable",
						    Statement.Location);
						return false;
					}
					if (!ValidateFieldTypeName(StoredTypeName, Statement.Location))
					{
						return false;
					}
				}
				if (bInferAuto && StoredTypeName.find('[') != std::string::npos)
				{
					Module->CompiledDeclarationTypes.push_back({SourceDeclaration.Name, SourceDeclaration.Location, StoredTypeName});
				}
				// Interface / class handles share identity; only struct / T[N] need a value copy.
				// (struct→interface boxing already cloned once in EmitValueCoercionBetweenTypes.)
				// Script free-function returns already clone at the return site — skip the
				// redundant consumer clone when the initializer is such an unaliased rvalue.
				if (VarDecl.Initializer && NeedsPassByValueCopy(StoredTypeName) && !CanTransferCoercedValue(*VarDecl.Initializer, StoredTypeName))
				{
					EmitValueCopy(Emitter, StoredTypeName);
				}
				if (CurrentFunction == nullptr && Scopes.empty())
				{
					const std::string GlobalName = VarDecl.ModulePath.empty()
					    ? VarDecl.Name
					    : GetSymbolLinkageName(VarDecl.ModulePath, VarDecl.bExported, VarDecl.Name);
					GlobalVarTypes.emplace(GlobalName, StoredTypeName);
					if (VarDecl.bIsConst)
					{
						GlobalVarConsts.insert(GlobalName);
					}
					const std::uint16_t Slot = AddGlobal(GlobalName);
					if (RequiredConstant && Slot < Module->GlobalMetadata.size())
					{
						Module->GlobalMetadata[Slot].ConstantValue = *RequiredConstant;
					}
					if (Slot < Module->GlobalTypeNames.size())
					{
						Module->GlobalTypeNames[Slot] = StoredTypeName;
					}
					Emitter.EmitU16Operand(EOpcode::StoreGlobal, Slot);
				}
				else
				{
					FTemplateEvaluationResult TemplateConstant;
					if (VarDecl.bIsConst && VarDecl.Initializer)
					{
						if (RequiredConstant)
						{
							TemplateConstant.Status = ETemplateEvaluationStatus::Constant;
							TemplateConstant.Value = *RequiredConstant;
						}
						else
						{
							TemplateConstant = EvaluateTemplateExpression(*VarDecl.Initializer, CurrentModulePath,
							    ActiveTypeSubst, CurrentNamespacePrefix, true, nullptr, false);
						}
						FTemplateValueType ScalarType;
						if (TemplateConstant.Succeeded() && ResolveTemplateValueType(StoredTypeName, CurrentModulePath, ActiveTypeSubst, ScalarType))
						{
							TemplateConstant = ConvertTemplateConstant(TemplateConstant.Value, ScalarType);
						}
						else
						{
							TemplateConstant.Status = ETemplateEvaluationStatus::NotConstant;
						}
					}
					const std::uint16_t Slot = AllocateLocal(VarDecl.Name);
					if (!Scopes.empty())
					{
						Scopes.back().LocalTypes.emplace(VarDecl.Name, StoredTypeName);
						if (VarDecl.bIsConst)
						{
							Scopes.back().LocalConsts.insert(VarDecl.Name);
							if (TemplateConstant.Succeeded())
							{
								Scopes.back().TemplateConstants.emplace(VarDecl.Name, TemplateConstant.Value);
							}
							else
							{
								FTemplateValueType Scalar;
								if (ResolveTemplateValueType(StoredTypeName, CurrentModulePath, ActiveTypeSubst, Scalar))
								{
									FLocalConstantInitializer Pending;
									Pending.Expression = SourceDeclaration.Initializer.get();
									Pending.TypeName = StoredTypeName;
									Pending.ScopeCount = Scopes.size();
									Pending.SlotBound = Slot;
									Pending.GlobalSlotBound = Module->GlobalNames.size();
									Pending.FunctionName = CurrentFunction ? CurrentFunction->Name : std::string{};
									Pending.ModulePath = CurrentModulePath;
									Pending.Namespace = CurrentNamespacePrefix;
									Pending.Owner = CurrentOwnerTypeLinkage;
									Pending.Access = CurrentAccessFunctionLinkage;
									Pending.Identity = CurrentAccessFunctionLinkage + "$local-constant-" + std::to_string(Slot);
									Pending.Bindings = ActiveTypeSubst;
									for (const auto& Scope : Scopes)
									{
										Pending.EnumScopes.push_back(Scope.EnumConstants);
									}
									for (const auto& Aliases : LocalTypeAliasScopes)
									{
										for (const auto& Alias : Aliases)
										{
											Pending.Bindings[Alias.first] = CanonicalTypeNameForCoercion(QualifyTypeName(SubstituteTypeName(Alias.second), CurrentModulePath));
										}
									}
									Scopes.back().ConstantInitializers.emplace(VarDecl.Name, std::move(Pending));
								}
							}
						}
					}
					{
						FLocalInitMarker Marker;
						Marker.Name = VarDecl.Name;
						Marker.Offset = Emitter.CurrentOffset();
						Marker.ScopeDepth = static_cast<int>(Scopes.size());
						LocalInitMarkers.push_back(std::move(Marker));
					}
					Emitter.EmitU16Operand(EOpcode::StoreLocal, Slot);
				}
				return true;
			}
		case EStmtKind::Return:
			{
				if (ActiveFinallyDepth != 0)
				{
					ReportError("return cannot leave a finally block", Statement.Location);
					return false;
				}
				const FReturnStmt& ReturnStmt = static_cast<const FReturnStmt&>(Statement);
				if (ReturnStmt.Value)
				{
					if (Runtime::IsReferenceTypeName(CurrentFunctionReturnType))
					{
						const FExpr& ReturnExpression = UnwrapRefOperand(*ReturnStmt.Value);
						const auto IsReferenceLocal = [this](const std::string& Name) -> bool
						{
							for (auto It = Scopes.rbegin(); It != Scopes.rend(); ++It)
							{
								if (It->Locals.find(Name) != It->Locals.end())
								{
									const auto Slot = It->Locals.at(Name);
									return It->LocalRefs.find(Name) != It->LocalRefs.end() || (CurrentFunction && Slot >= CurrentFunction->ParamCount && Slot - CurrentFunction->ParamCount < CurrentFunction->Captures.size());
								}
							}
							return false;
						};
						const auto HasStableRoot = [this, &IsReferenceLocal](
						                               const FExpr& Candidate,
						                               const auto& Self) -> bool
						{
							std::string RawType;
							if (TryResolveExprRawTypeName(Candidate, RawType) && Runtime::IsReferenceTypeName(RawType))
							{
								return true;
							}
							switch (Candidate.Kind)
							{
							case EExprKind::This:
								return true;
							case EExprKind::Call:
							case EExprKind::MethodCall:
								return true;
							case EExprKind::Identifier:
								{
									const std::string& Name =
									    static_cast<const FIdentifierExpr&>(Candidate).Name;
									std::uint16_t Slot = 0;
									return !ResolveLocal(Name, Slot) || IsReferenceLocal(Name);
								}
							case EExprKind::MemberAccess:
								{
									const FMemberAccessExpr& Member =
									    static_cast<const FMemberAccessExpr&>(Candidate);
									return Member.Object && Self(*Member.Object, Self);
								}
							case EExprKind::IndexAccess:
								{
									const FIndexAccessExpr& Index =
									    static_cast<const FIndexAccessExpr&>(Candidate);
									return Index.Object && Self(*Index.Object, Self);
								}
							default:
								return false;
							}
						};
						if (!HasStableRoot(ReturnExpression, HasStableRoot))
						{
							ReportError("reference return would outlive stack-owned storage", Statement.Location);
							return false;
						}
						if (ReturnExpression.Kind == EExprKind::Identifier)
						{
							const std::string& Name =
							    static_cast<const FIdentifierExpr&>(ReturnExpression).Name;
							std::uint16_t Slot = 0;
							std::string LocalType;
							if (ResolveLocal(Name, Slot))
							{
								const bool bReferenceParameter = CurrentFunction && Slot < CurrentFunction->ParamFlags.size() && (CurrentFunction->ParamFlags[Slot] & (BytecodeParamFlagRef | BytecodeParamFlagConstRef)) != 0;
								const bool bReferenceLocal = IsReferenceLocal(Name);
								if (!bReferenceParameter && !bReferenceLocal)
								{
									ReportError("cannot return a reference to an ordinary stack local", Statement.Location);
									return false;
								}
							}
						}
						std::string ValueType;
						const std::string ExpectedValueType = CanonicalTypeNameForCoercion(
						    std::string(Runtime::StripReferenceTypeName(CurrentFunctionReturnType)));
						if (!TryResolveExprTypeName(ReturnExpression, ValueType) || CanonicalTypeNameForCoercion(ValueType) != ExpectedValueType)
						{
							ReportError(
							    "reference return type mismatch: expected " + ExpectedValueType + ", got " + (ValueType.empty() ? std::string{"<unknown>"} : ValueType),
							    Statement.Location);
							return false;
						}
						if (!CompileRefArgument(
						        Emitter,
						        ReturnExpression,
						        Statement.Location,
						        !Runtime::IsConstReferenceTypeName(CurrentFunctionReturnType)))
						{
							return false;
						}
						Emitter.Emit(EOpcode::Return);
						return true;
					}
					if (!ValidateDynamicArrayAssignment(
					        CurrentFunctionReturnType,
					        *ReturnStmt.Value,
					        Statement.Location))
					{
						return false;
					}
					const std::string PreviousExpectedType = ExpressionExpectedType;
					ExpressionExpectedType = CurrentFunctionReturnType;
					const bool bCompiled = CompileExpression(*ReturnStmt.Value);
					const bool bCoerced = bCompiled && EmitValueCoercionForTarget(Emitter, CurrentFunctionReturnType, *ReturnStmt.Value, Statement.Location);
					ExpressionExpectedType = PreviousExpectedType;
					if (!bCompiled || !bCoerced)
					{
						return false;
					}
					// Keep the production clone for lvalues / constructors; skip only when
					// returning an already-cloned script free-function result.
					if (NeedsPassByValueCopy(CurrentFunctionReturnType) && !CanTransferCoercedValue(*ReturnStmt.Value, CurrentFunctionReturnType))
					{
						EmitValueCopy(Emitter, CurrentFunctionReturnType);
					}
				}
				else
				{
					if (SubstituteTypeName(CurrentFunctionReturnType) != "void")
					{
						ReportErrorId(
						    "diag.non_void_function_must_return_value",
						    Statement.Location);
						return false;
					}
					FBytecodeConstant Zero;
					Zero.Kind = EConstantKind::I32;
					Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(Zero));
				}
				Emitter.Emit(EOpcode::Return);
				return true;
			}
		case EStmtKind::Throw:
			{
				if (!CurrentFunction)
				{
					ReportError("throw is only valid inside a function", Statement.Location);
					return false;
				}
				const FThrowStmt& ThrowStmt = static_cast<const FThrowStmt&>(Statement);
				if (ThrowStmt.Value)
				{
					if (ThrowStmt.Value->Kind == EExprKind::Ref)
					{
						ReportError(
						    "throw requires a value, not a ref binding",
						    Statement.Location);
						return false;
					}
					std::string TypeName;
					if (!TryResolveExprTypeName(*ThrowStmt.Value, TypeName) || TypeName.empty() || TypeName == "void")
					{
						ReportError("throw requires a non-void value", Statement.Location);
						return false;
					}
					if (!CompileExpression(*ThrowStmt.Value))
					{
						return false;
					}
					TypeName = CanonicalTypeNameForCoercion(TypeName);
					if (NeedsPassByValueCopy(TypeName) && !CanTransferCoercedValue(*ThrowStmt.Value, TypeName))
					{
						EmitValueCopy(Emitter, TypeName);
					}
				}
				else
				{
					if (ActiveCatchSlots.empty())
					{
						ReportError("bare throw is only valid inside a catch handler", Statement.Location);
						return false;
					}
					Emitter.EmitU16Operand(EOpcode::LoadLocal, ActiveCatchSlots.back());
				}
				Emitter.Emit(EOpcode::Throw);
				return true;
			}
		case EStmtKind::Try:
			{
				if (!CurrentFunction)
				{
					ReportError("try is only valid inside a function", Statement.Location);
					return false;
				}
				const FTryStmt& TryStmt = static_cast<const FTryStmt&>(Statement);
				if (!TryStmt.TryBlock || (TryStmt.Catches.empty() && !TryStmt.FinallyBlock))
				{
					ReportError("invalid try statement", Statement.Location);
					return false;
				}

				FBytecodeExceptionRegion TryRegion;
				TryRegion.BeginOffset = static_cast<std::uint32_t>(Emitter.CurrentOffset());
				ActiveExceptionScopePath.push_back(NextExceptionScopeId++);
				const bool bTryCompiled = CompileBlock(*TryStmt.TryBlock);
				ActiveExceptionScopePath.pop_back();
				if (!bTryCompiled)
				{
					return false;
				}
				std::vector<std::size_t> EndJumpOperands;
				EndJumpOperands.push_back(Emitter.EmitI32Operand(EOpcode::Jmp, 0));
				TryRegion.EndOffset = static_cast<std::uint32_t>(Emitter.CurrentOffset());

				std::vector<FBytecodeExceptionRegion> CatchRegions;
				CatchRegions.reserve(TryStmt.Catches.size());
				std::unordered_set<std::string> SeenCatchTypes;
				for (const FCatchClause& Clause : TryStmt.Catches)
				{
					FBytecodeCatchHandler Handler;
					Handler.HandlerOffset = static_cast<std::uint32_t>(Emitter.CurrentOffset());
					BeginScope();
					if (!Clause.bCatchAll)
					{
						Handler.TypeName = CanonicalTypeNameForCoercion(
						    QualifyTypeName(SubstituteTypeName(Clause.TypeName), CurrentModulePath));
						if (Handler.TypeName.empty() || Handler.TypeName == "void" || !ValidateFieldTypeName(Handler.TypeName, Clause.Location))
						{
							EndScope();
							return false;
						}
						if (!SeenCatchTypes.emplace(Handler.TypeName).second)
						{
							ReportError("duplicate catch type: " + Handler.TypeName, Clause.Location);
							EndScope();
							return false;
						}
					}
					// Every handler owns a slot so `throw;` can preserve the caught
					// value. Anonymous handlers use '$', which is not a source identifier,
					// and therefore never leak an implementation name into script scope.
					const std::string LocalName = Clause.Name.empty()
					    ? "$catch" + std::to_string(NextAssignTempId++)
					    : Clause.Name;
					Handler.LocalSlot = AllocateLocal(LocalName);
					Handler.RethrowSlot = Handler.LocalSlot;
					if (!Clause.Name.empty() && !Clause.bIsRef)
					{
						Handler.RethrowSlot = AllocateLocal(
						    "$rethrow" + std::to_string(NextAssignTempId++));
					}
					if (!Scopes.empty() && !Clause.Name.empty())
					{
						Scopes.back().LocalTypes.emplace(
						    LocalName,
						    Handler.TypeName.empty() ? "auto" : Handler.TypeName);
						if (Clause.bIsConst)
						{
							Scopes.back().LocalConsts.insert(LocalName);
						}
					}
					ActiveCatchSlots.push_back(Handler.RethrowSlot);
					ActiveExceptionScopePath.push_back(NextExceptionScopeId++);
					const bool bCompiled = Clause.Body && CompileBlock(*Clause.Body, true);
					ActiveExceptionScopePath.pop_back();
					ActiveCatchSlots.pop_back();
					EndScope();
					if (!bCompiled)
					{
						return false;
					}
					FBytecodeExceptionRegion CatchRegion;
					CatchRegion.BeginOffset = Handler.HandlerOffset;
					EndJumpOperands.push_back(Emitter.EmitI32Operand(EOpcode::Jmp, 0));
					CatchRegion.EndOffset = static_cast<std::uint32_t>(Emitter.CurrentOffset());
					CatchRegions.push_back(std::move(CatchRegion));
					TryRegion.Catches.push_back(std::move(Handler));
				}

				if (TryStmt.FinallyBlock)
				{
					const std::uint32_t FinallyOffset =
					    static_cast<std::uint32_t>(Emitter.CurrentOffset());
					ActiveExceptionScopePath.push_back(NextExceptionScopeId++);
					std::vector<FLoopContext> OuterLoopStack = std::move(LoopStack);
					LoopStack.clear();
					++ActiveFinallyDepth;
					const bool bFinallyCompiled = CompileBlock(*TryStmt.FinallyBlock);
					--ActiveFinallyDepth;
					LoopStack = std::move(OuterLoopStack);
					ActiveExceptionScopePath.pop_back();
					if (!bFinallyCompiled)
					{
						return false;
					}
					Emitter.Emit(EOpcode::EndFinally);
					const std::uint32_t FinallyEndOffset =
					    static_cast<std::uint32_t>(Emitter.CurrentOffset());
					TryRegion.FinallyOffset = FinallyOffset;
					TryRegion.FinallyEndOffset = FinallyEndOffset;
					for (FBytecodeExceptionRegion& Region : CatchRegions)
					{
						Region.FinallyOffset = FinallyOffset;
						Region.FinallyEndOffset = FinallyEndOffset;
					}
				}

				const std::int32_t EndOffset = Emitter.CurrentOffset();
				for (const std::size_t Operand : EndJumpOperands)
				{
					Emitter.PatchI32(
					    Operand,
					    EndOffset - static_cast<std::int32_t>(Operand + 4));
				}
				CurrentFunction->ExceptionRegions.push_back(std::move(TryRegion));
				if (TryStmt.FinallyBlock)
				{
					for (FBytecodeExceptionRegion& Region : CatchRegions)
					{
						CurrentFunction->ExceptionRegions.push_back(std::move(Region));
					}
				}
				return true;
			}
		case EStmtKind::If:
			{
				const FIfStmt& IfStmt = static_cast<const FIfStmt&>(Statement);
				if (IfStmt.bConstexpr)
				{
					FConstantBranchSelectionScope BranchScope(ConstantBranchState);
					const auto Value = EvaluateTemplateExpression(*IfStmt.Condition, CurrentModulePath, ActiveTypeSubst, CurrentNamespacePrefix, true);
					if (!Value.Succeeded() || Value.Value.Type.bScopedEnum)
					{
						ReportError("if constexpr requires an integral or bool constant condition: " + Value.Reason, IfStmt.Condition->Location);
						return false;
					}
					const bool Then = Value.Value.Bits != 0;
					if (ConstantBranchState)
					{
						ConstantBranchState->Selections[FConstantBranchState::Key(IfStmt)] = Then;
					}
					Module->CompiledConstantBranches.push_back({FConstantBranchState::Key(IfStmt), Then});
					const auto* Selected = Then ? IfStmt.ThenBranch.get() : IfStmt.ElseBranch.get();
					if (!Selected) return true;
					std::unique_ptr<FBlockStmt> Specialized;
					return LowerConstantBranches(*Selected, Specialized) && CompileBlock(Specialized ? *Specialized : *Selected);
				}
				if (!RequireBoolCondition(*IfStmt.Condition, Statement.Location))
				{
					return false;
				}
				if (!CompileExpression(*IfStmt.Condition) || !EmitValueCoercionForTarget(Emitter, "bool", *IfStmt.Condition, Statement.Location))
				{
					return false;
				}
				const std::size_t JumpIfFalseOperand = Emitter.EmitI32Operand(EOpcode::JmpIfFalse, 0);
				if (!CompileBlock(*IfStmt.ThenBranch, false))
				{
					return false;
				}
				if (IfStmt.ElseBranch)
				{
					const std::size_t JumpEndOperand = Emitter.EmitI32Operand(EOpcode::Jmp, 0);
					Emitter.PatchI32(
					    JumpIfFalseOperand,
					    Emitter.CurrentOffset() - static_cast<std::int32_t>(JumpIfFalseOperand + 4));
					if (!CompileBlock(*IfStmt.ElseBranch, false))
					{
						return false;
					}
					Emitter.PatchI32(
					    JumpEndOperand,
					    Emitter.CurrentOffset() - static_cast<std::int32_t>(JumpEndOperand + 4));
				}
				else
				{
					Emitter.PatchI32(
					    JumpIfFalseOperand,
					    Emitter.CurrentOffset() - static_cast<std::int32_t>(JumpIfFalseOperand + 4));
				}
				return true;
			}
		case EStmtKind::While:
			{
				const FWhileStmt& WhileStmt = static_cast<const FWhileStmt&>(Statement);
				LoopStack.emplace_back();
				const std::int32_t LoopStart = Emitter.CurrentOffset();
				if (!RequireBoolCondition(*WhileStmt.Condition, Statement.Location))
				{
					LoopStack.pop_back();
					return false;
				}
				if (!CompileExpression(*WhileStmt.Condition) || !EmitValueCoercionForTarget(Emitter, "bool", *WhileStmt.Condition, Statement.Location))
				{
					LoopStack.pop_back();
					return false;
				}
				const std::size_t JumpIfFalseOperand = Emitter.EmitI32Operand(EOpcode::JmpIfFalse, 0);
				if (!CompileBlock(*WhileStmt.Body, false))
				{
					LoopStack.pop_back();
					return false;
				}
				for (const std::size_t ContinuePatch : LoopStack.back().ContinuePatches)
				{
					Emitter.PatchI32(
					    ContinuePatch,
					    LoopStart - static_cast<std::int32_t>(ContinuePatch + 4));
				}
				Emitter.EmitI32Operand(
				    EOpcode::Jmp,
				    LoopStart - Emitter.CurrentOffset() - static_cast<std::int32_t>(Bytecode::InstructionSize(EOpcode::Jmp)));
				CloseLoop(Emitter, JumpIfFalseOperand, true);
				return true;
			}
		case EStmtKind::DoWhile:
			{
				const FDoWhileStmt& DoWhileStmt = static_cast<const FDoWhileStmt&>(Statement);
				if (!RequireBoolCondition(*DoWhileStmt.Condition, Statement.Location))
				{
					return false;
				}
				LoopStack.emplace_back();
				const std::int32_t LoopStart = Emitter.CurrentOffset();
				if (!CompileBlock(*DoWhileStmt.Body, false))
				{
					LoopStack.pop_back();
					return false;
				}
				const std::int32_t ConditionStart = Emitter.CurrentOffset();
				for (const std::size_t ContinuePatch : LoopStack.back().ContinuePatches)
				{
					Emitter.PatchI32(
					    ContinuePatch,
					    ConditionStart - static_cast<std::int32_t>(ContinuePatch + 4));
				}
				LoopStack.back().ContinuePatches.clear();
				CurrentEmitLocation = DoWhileStmt.Condition->Location;
				if (!CompileExpression(*DoWhileStmt.Condition) || !EmitValueCoercionForTarget(Emitter, "bool", *DoWhileStmt.Condition, Statement.Location))
				{
					LoopStack.pop_back();
					return false;
				}
				const std::size_t JumpIfFalseOperand =
				    Emitter.EmitI32Operand(EOpcode::JmpIfFalse, 0);
				Emitter.EmitI32Operand(
				    EOpcode::Jmp,
				    LoopStart - Emitter.CurrentOffset() - static_cast<std::int32_t>(Bytecode::InstructionSize(EOpcode::Jmp)));
				CloseLoop(Emitter, JumpIfFalseOperand, true);
				return true;
			}
		case EStmtKind::For:
			{
				const FForStmt& ForStmt = static_cast<const FForStmt&>(Statement);
				BeginScope();
				if (ForStmt.Init)
				{
					if (ForStmt.Init->Kind == EStmtKind::VarDecl || ForStmt.Init->Kind == EStmtKind::VarDeclGroup || ForStmt.Init->Kind == EStmtKind::Expr)
					{
						if (!CompileStatement(*ForStmt.Init))
						{
							EndScope();
							return false;
						}
					}
					else
					{
						ReportError("invalid for-init statement", Statement.Location);
						EndScope();
						return false;
					}
				}
				for (const std::unique_ptr<FExpr>& InitExpression : ForStmt.InitExpressions)
				{
					if (InitExpression)
					{
						CurrentEmitLocation = InitExpression->Location;
					}
					if (!InitExpression || !CompileExpression(*InitExpression))
					{
						EndScope();
						return false;
					}
					Emitter.Emit(EOpcode::Pop);
				}

				LoopStack.emplace_back();
				const std::int32_t ConditionStart = Emitter.CurrentOffset();
				std::size_t JumpIfFalseOperand = 0;
				const bool bHasCondition = ForStmt.Condition != nullptr;
				if (bHasCondition)
				{
					CurrentEmitLocation = ForStmt.Condition->Location;
					if (!RequireBoolCondition(*ForStmt.Condition, Statement.Location))
					{
						LoopStack.pop_back();
						EndScope();
						return false;
					}
					if (!CompileExpression(*ForStmt.Condition) || !EmitValueCoercionForTarget(Emitter, "bool", *ForStmt.Condition, Statement.Location))
					{
						LoopStack.pop_back();
						EndScope();
						return false;
					}
					JumpIfFalseOperand = Emitter.EmitI32Operand(EOpcode::JmpIfFalse, 0);
				}

				if (!CompileBlock(*ForStmt.Body, false))
				{
					LoopStack.pop_back();
					EndScope();
					return false;
				}

				const std::int32_t StepStart = Emitter.CurrentOffset();
				const std::int32_t ContinueTarget =
				    ForStmt.Step || !ForStmt.StepExpressions.empty() ? StepStart : ConditionStart;
				for (const std::size_t ContinuePatch : LoopStack.back().ContinuePatches)
				{
					Emitter.PatchI32(
					    ContinuePatch,
					    ContinueTarget - static_cast<std::int32_t>(ContinuePatch + 4));
				}
				LoopStack.back().ContinuePatches.clear();

				if (ForStmt.Step)
				{
					CurrentEmitLocation = ForStmt.Step->Location;
					if (!CompileExpression(*ForStmt.Step))
					{
						LoopStack.pop_back();
						EndScope();
						return false;
					}
					Emitter.Emit(EOpcode::Pop);
				}
				for (const std::unique_ptr<FExpr>& StepExpression : ForStmt.StepExpressions)
				{
					if (StepExpression)
					{
						CurrentEmitLocation = StepExpression->Location;
					}
					if (!StepExpression || !CompileExpression(*StepExpression))
					{
						LoopStack.pop_back();
						EndScope();
						return false;
					}
					Emitter.Emit(EOpcode::Pop);
				}

				Emitter.EmitI32Operand(
				    EOpcode::Jmp,
				    ConditionStart - Emitter.CurrentOffset() - static_cast<std::int32_t>(Bytecode::InstructionSize(EOpcode::Jmp)));
				CloseLoop(Emitter, JumpIfFalseOperand, bHasCondition);
				EndScope();
				return true;
			}
		case EStmtKind::ForEach:
			{
				const FForEachStmt& ForEachStmt = static_cast<const FForEachStmt&>(Statement);
				BeginScope();

				std::string CollectionTypeName;
				if (!TryResolveExprTypeName(*ForEachStmt.Collection, CollectionTypeName))
				{
					ReportError("range-for collection type could not be resolved", Statement.Location);
					EndScope();
					return false;
				}

				std::string ElementTypeName;
				bool bMaterializeViaValues = false;
				bool bMapForeach = false;
				bool bMapEntriesForeach = false;
				bool bMapEntryViewForeach = false;
				std::string MapForeachKeyTypeName;
				if (Runtime::IsDynamicArrayTypeName(CollectionTypeName))
				{
					if (!Runtime::ParseDynamicArrayElementType(CollectionTypeName, ElementTypeName))
					{
						ReportError("invalid dynamic array type in range-for", Statement.Location);
						EndScope();
						return false;
					}
				}
				else if (Runtime::IsNativeArrayTypeName(CollectionTypeName) || Runtime::IsScriptArgsArrayTypeName(CollectionTypeName))
				{
					if (Runtime::IsScriptArgsArrayTypeName(CollectionTypeName))
					{
						ElementTypeName = "string";
					}
					else
					{
						std::uint32_t Length = 0;
						if (!Runtime::ParseNativeArrayType(CollectionTypeName, ElementTypeName, Length))
						{
							ReportError("invalid array type in range-for", Statement.Location);
							EndScope();
							return false;
						}
					}
				}
				else if (Runtime::IsDequeTypeName(CollectionTypeName))
				{
					if (!Runtime::ParseDequeElementType(CollectionTypeName, ElementTypeName))
					{
						ReportError("invalid deque type in range-for", Statement.Location);
						EndScope();
						return false;
					}
					// Snapshot via values() only for by-value loops (shrink/grow safety).
					// ref range-for aliases deque elements in place like array.
					bMaterializeViaValues = !ForEachStmt.bIsRef;
				}
				else if (Runtime::IsSetTypeName(CollectionTypeName))
				{
					if (!ParseSetElementTypeForCompile(CollectionTypeName, ElementTypeName))
					{
						ReportError("invalid set type in range-for", Statement.Location);
						EndScope();
						return false;
					}
					bMaterializeViaValues = true;
				}
				else if (Runtime::IsMultiSetTypeName(CollectionTypeName) || Runtime::IsOrderedMultiSetTypeName(CollectionTypeName))
				{
					if (!Runtime::ParseMultiSetElementType(
					        CollectionTypeName,
					        ElementTypeName))
					{
						ReportError("invalid multiset type in range-for", Statement.Location);
						EndScope();
						return false;
					}
					bMaterializeViaValues = true;
				}
				else if (Runtime::IsQueueTypeName(CollectionTypeName))
				{
					if (!Runtime::ParseQueueElementType(CollectionTypeName, ElementTypeName))
					{
						ReportError("invalid queue type in range-for", Statement.Location);
						EndScope();
						return false;
					}
					bMaterializeViaValues = true;
				}
				else if (Runtime::IsStackTypeName(CollectionTypeName))
				{
					if (!Runtime::ParseStackElementType(CollectionTypeName, ElementTypeName))
					{
						ReportError("invalid stack type in range-for", Statement.Location);
						EndScope();
						return false;
					}
					bMaterializeViaValues = true;
				}
				else if (Runtime::IsMapTypeName(CollectionTypeName) || Runtime::IsOrderedMapTypeName(CollectionTypeName))
				{
					std::string KeyTypeName;
					if (Runtime::IsMapTypeName(CollectionTypeName))
					{
						if (!ParseMapTypeForCompile(CollectionTypeName, KeyTypeName, ElementTypeName))
						{
							ReportError("invalid map type in range-for", Statement.Location);
							EndScope();
							return false;
						}
					}
					else if (!ParseOrderedMapTypeForCompile(
					             CollectionTypeName,
					             KeyTypeName,
					             ElementTypeName))
					{
						ReportError("invalid ordered_map type in range-for", Statement.Location);
						EndScope();
						return false;
					}
					bMapForeach = true;
					MapForeachKeyTypeName = std::move(KeyTypeName);
				}
				else
				{
					ReportError(
					    "range-for requires array, deque, set, queue, stack, map, or ordered_map",
					    Statement.Location);
					EndScope();
					return false;
				}

				if (ForEachStmt.bHasKeyBinding && !bMapForeach)
				{
					ReportErrorId("diag.map_destructure_requires_map", Statement.Location);
					EndScope();
					return false;
				}

				std::string DeclaredElementType = SubstituteTypeName(ForEachStmt.ElementTypeName);
				std::string DeclaredKeyType = ForEachStmt.bHasKeyBinding
				    ? SubstituteTypeName(ForEachStmt.KeyTypeName)
				    : std::string{};
				if (bMapForeach && !ForEachStmt.bHasKeyBinding && DeclaredElementType != "auto")
				{
					std::string PairFirst;
					std::string PairSecond;
					bool bDeclaredPair = Runtime::ParsePairType(
					    DeclaredElementType,
					    PairFirst,
					    PairSecond);
					if (!bDeclaredPair)
					{
						std::string PairBase;
						std::vector<std::string> PairArgs;
						if (SplitUserGenericTypeApp(DeclaredElementType, PairBase, PairArgs) && PairBase == "pair" && PairArgs.size() == 2)
						{
							PairFirst = PairArgs[0];
							PairSecond = PairArgs[1];
							bDeclaredPair = true;
						}
					}
					if (bDeclaredPair)
					{
						const std::string CanonicalKey =
						    CanonicalTypeNameForCoercion(
						        QualifyTypeName(MapForeachKeyTypeName, CurrentModulePath));
						const std::string CanonicalValue =
						    CanonicalTypeNameForCoercion(
						        QualifyTypeName(ElementTypeName, CurrentModulePath));
						const std::string CanonicalFirst =
						    CanonicalTypeNameForCoercion(
						        QualifyTypeName(PairFirst, CurrentModulePath));
						const std::string CanonicalSecond =
						    CanonicalTypeNameForCoercion(
						        QualifyTypeName(PairSecond, CurrentModulePath));
						if (CanonicalFirst == CanonicalKey && CanonicalSecond == CanonicalValue)
						{
							ElementTypeName = Runtime::MakePairTypeName(
							    MapForeachKeyTypeName,
							    ElementTypeName);
							std::string ResolvedLinkage;
							if (!EnsureStructTypeResolved(
							        ElementTypeName,
							        ResolvedLinkage,
							        nullptr,
							        Statement.Location))
							{
								EndScope();
								return false;
							}
							if (ForEachStmt.bIsRef)
							{
								// UE-style entry view: keep keys path + MapEntryRef bind.
								bMapEntryViewForeach = true;
							}
							else
							{
								bMapEntriesForeach = true;
								bMapForeach = false;
							}
						}
					}
				}
				const std::string QualifiedCollectionElement =
				    QualifyTypeName(ElementTypeName, CurrentModulePath);
				const bool bNeedsElementCoercion = DeclaredElementType != "auto" && QualifyTypeName(DeclaredElementType, CurrentModulePath) != QualifiedCollectionElement;
				if (DeclaredElementType == "auto")
				{
					DeclaredElementType = QualifiedCollectionElement;
				}
				else if (bNeedsElementCoercion)
				{
					const bool bTargetUnsigned = Runtime::IsUnsignedTypeName(DeclaredElementType);
					const bool bSourceUnsigned = Runtime::IsUnsignedTypeName(ElementTypeName);
					const bool bTargetSigned = IsSignedIntegerTypeName(DeclaredElementType);
					const bool bSourceSigned = IsSignedIntegerTypeName(ElementTypeName);
					const bool bTargetFloat = DeclaredElementType == "float" || DeclaredElementType == "double";
					const bool bSourceFloat = ElementTypeName == "float" || ElementTypeName == "double";
					const bool bSourceNumeric = bSourceSigned || bSourceUnsigned || bSourceFloat || ElementTypeName == "bool";
					const bool bCanCoerce =
					    (bTargetUnsigned && bSourceNumeric && !bSourceUnsigned) || (bTargetSigned && bSourceNumeric && !bSourceSigned) || (bTargetFloat && bSourceNumeric && !bSourceFloat);
					if (!bCanCoerce)
					{
						ReportErrorId("diag.range_for_element_type_mismatch_expected_got", Statement.Location, {ElementTypeName, DeclaredElementType});
						EndScope();
						return false;
					}
				}

				if (ForEachStmt.bHasKeyBinding)
				{
					const std::string QualifiedCollectionKey =
					    QualifyTypeName(MapForeachKeyTypeName, CurrentModulePath);
					if (DeclaredKeyType == "auto")
					{
						DeclaredKeyType = QualifiedCollectionKey;
					}
					else if (QualifyTypeName(DeclaredKeyType, CurrentModulePath) != QualifiedCollectionKey)
					{
						ReportErrorId(
						    "diag.range_for_element_type_mismatch_expected_got",
						    Statement.Location,
						    {MapForeachKeyTypeName, DeclaredKeyType});
						EndScope();
						return false;
					}
				}

				if (ForEachStmt.bIsRef && bMaterializeViaValues)
				{
					ReportErrorId(
					    "diag.ref_range_for_not_supported_on_set_queue_stack_deque",
					    Statement.Location);
					EndScope();
					return false;
				}
				if (ForEachStmt.bIsRef && bMapEntriesForeach)
				{
					// Value-pair snapshots (entries()) cannot write through; use for (ref pair : m).
					ReportErrorId(
					    "diag.ref_range_for_not_supported_on_map_entries_snapshot",
					    Statement.Location);
					EndScope();
					return false;
				}
				if (ForEachStmt.bIsRef && ForEachStmt.Collection->Kind == EExprKind::MethodCall)
				{
					const FMethodCallExpr& MethodCall =
					    static_cast<const FMethodCallExpr&>(*ForEachStmt.Collection);
					if ((MethodCall.Method == "values" || MethodCall.Method == "entries") && MethodCall.Object)
					{
						std::string ReceiverType;
						if (TryResolveExprTypeName(*MethodCall.Object, ReceiverType) && (Runtime::IsMapTypeName(ReceiverType) || Runtime::IsOrderedMapTypeName(ReceiverType)))
						{
							ReportErrorId(
							    MethodCall.Method == "entries"
							        ? "diag.ref_range_for_not_supported_on_map_entries_snapshot"
							        : "diag.ref_range_for_not_supported_on_map_values_snapshot",
							    Statement.Location);
							EndScope();
							return false;
						}
					}
				}
				if (ForEachStmt.bIsRef && bNeedsElementCoercion)
				{
					ReportErrorId(
					    "diag.ref_range_for_cannot_coerce_element_types",
					    Statement.Location);
					EndScope();
					return false;
				}
				// Mutable ref into a const collection would bypass container constness
				// (assigns go through the element binding, not xs[i]).
				if (ForEachStmt.bIsRef && !ForEachStmt.bIsConst && IsConstAssignTarget(*ForEachStmt.Collection))
				{
					ReportErrorId(
					    "diag.cannot_take_mutable_ref_range_for_into_const_collection",
					    Statement.Location);
					EndScope();
					return false;
				}

				// Prefer aliasing a local collection slot for `ref` so NativeArray (value
				// type) is not deep-copied. DynamicArray handles also alias fine.
				bool bAliasLocalCollection = false;
				std::uint16_t CollectionSlot = 0;
				if (ForEachStmt.bIsRef && ForEachStmt.Collection->Kind == EExprKind::Identifier)
				{
					const std::string& CollectionName =
					    static_cast<const FIdentifierExpr&>(*ForEachStmt.Collection).Name;
					if (ResolveLocal(CollectionName, CollectionSlot))
					{
						bAliasLocalCollection = true;
					}
				}
				if (ForEachStmt.bIsRef && (Runtime::IsNativeArrayTypeName(CollectionTypeName) || Runtime::IsScriptArgsArrayTypeName(CollectionTypeName)) && !bAliasLocalCollection)
				{
					ReportErrorId(
					    "diag.ref_range_for_on_native_array_requires_local",
					    Statement.Location);
					EndScope();
					return false;
				}

				if (!bAliasLocalCollection)
				{
					if (!CompileExpression(*ForEachStmt.Collection))
					{
						EndScope();
						return false;
					}
					CollectionSlot = AllocateLocal(
					    "$fe_coll" + std::to_string(NextAssignTempId++));
					Emitter.EmitU16Operand(EOpcode::StoreLocal, CollectionSlot);
				}

				if (bMaterializeViaValues)
				{
					// Snapshot elements into array<T> so GetIndex iteration works.
					Emitter.EmitU16Operand(EOpcode::LoadLocal, CollectionSlot);
					Emitter.EmitCallMethod(AddString("values"), 0);
					Emitter.EmitU16Operand(EOpcode::StoreLocal, CollectionSlot);
				}
				if (bMapEntriesForeach)
				{
					Emitter.EmitU16Operand(EOpcode::LoadLocal, CollectionSlot);
					Emitter.EmitCallMethod(AddString("entries"), 0);
					Emitter.EmitU16Operand(EOpcode::StoreLocal, CollectionSlot);
				}

				std::uint16_t IterationSlot = CollectionSlot;
				std::uint16_t MapSlot = CollectionSlot;
				if (bMapForeach)
				{
					// Snapshot keys once; values are loaded/aliased each iteration from the map.
					Emitter.EmitU16Operand(EOpcode::LoadLocal, CollectionSlot);
					Emitter.EmitCallMethod(AddString("keys"), 0);
					IterationSlot = AllocateLocal(
					    "$fe_keys" + std::to_string(NextAssignTempId++));
					Emitter.EmitU16Operand(EOpcode::StoreLocal, IterationSlot);
					MapSlot = CollectionSlot;
				}

				// Snapshot length once so mutations inside the loop cannot extend iteration.
				Emitter.EmitU16Operand(EOpcode::LoadLocal, IterationSlot);
				Emitter.EmitCallBuiltin(EBuiltinId::Len, 1);
				const std::uint16_t LengthSlot = AllocateLocal(
				    "$fe_len" + std::to_string(NextAssignTempId++));
				Emitter.EmitU16Operand(EOpcode::StoreLocal, LengthSlot);

				FBytecodeConstant Zero;
				Zero.Kind = EConstantKind::I64;
				Zero.I64 = 0;
				Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(Zero));
				const std::uint16_t IndexSlot = AllocateLocal(
				    "$fe_i" + std::to_string(NextAssignTempId++));
				Emitter.EmitU16Operand(EOpcode::StoreLocal, IndexSlot);

				const std::uint16_t ElementSlot = AllocateLocal(ForEachStmt.ElementName);
				std::uint16_t KeyBindSlot = 0;
				if (!Scopes.empty())
				{
					Scopes.back().LocalTypes.emplace(
					    ForEachStmt.ElementName,
					    QualifyTypeName(DeclaredElementType, CurrentModulePath));
					if (ForEachStmt.bIsConst)
					{
						Scopes.back().LocalConsts.insert(ForEachStmt.ElementName);
					}
					if (ForEachStmt.bIsRef)
					{
						Scopes.back().LocalRefs.insert(ForEachStmt.ElementName);
					}
				}
				if (ForEachStmt.bHasKeyBinding)
				{
					KeyBindSlot = AllocateLocal(ForEachStmt.KeyName);
					if (!Scopes.empty())
					{
						Scopes.back().LocalTypes.emplace(
						    ForEachStmt.KeyName,
						    QualifyTypeName(DeclaredKeyType, CurrentModulePath));
						if (ForEachStmt.bKeyIsConst)
						{
							Scopes.back().LocalConsts.insert(ForEachStmt.KeyName);
						}
					}
				}

				LoopStack.emplace_back();
				const std::int32_t ConditionStart = Emitter.CurrentOffset();
				Emitter.EmitU16Operand(EOpcode::LoadLocal, IndexSlot);
				Emitter.EmitU16Operand(EOpcode::LoadLocal, LengthSlot);
				Emitter.Emit(EOpcode::Lt);
				const std::size_t JumpIfFalseOperand = Emitter.EmitI32Operand(EOpcode::JmpIfFalse, 0);

				if (bMapForeach)
				{
					Emitter.EmitU16Operand(EOpcode::LoadLocal, IterationSlot);
					Emitter.EmitU16Operand(EOpcode::LoadLocal, IndexSlot);
					Emitter.Emit(EOpcode::GetIndexCopy);
					const std::uint16_t KeySlot = AllocateLocal(
					    "$fe_key" + std::to_string(NextAssignTempId++));
					Emitter.EmitU16Operand(EOpcode::StoreLocal, KeySlot);
					if (ForEachStmt.bHasKeyBinding)
					{
						Emitter.EmitU16Operand(EOpcode::LoadLocal, KeySlot);
						Emitter.Emit(EOpcode::CloneStruct);
						Emitter.EmitU16Operand(EOpcode::StoreLocal, KeyBindSlot);
					}
					if (ForEachStmt.bIsRef)
					{
						Emitter.EmitU16Operand(EOpcode::LoadLocal, KeySlot);
						Emitter.EmitU16Operand(EOpcode::GetMapEntryRef, MapSlot);
						Emitter.EmitU16Operand(EOpcode::BindLocal, ElementSlot);
						if (bMapEntryViewForeach && !Scopes.empty())
						{
							FScope::FMapEntryViewBinding ViewBinding;
							ViewBinding.KeySlot = KeySlot;
							ViewBinding.RefSlot = ElementSlot;
							Scopes.back().LocalMapEntryViews[ForEachStmt.ElementName] =
							    ViewBinding;
						}
					}
					else
					{
						Emitter.EmitU16Operand(EOpcode::LoadLocal, MapSlot);
						Emitter.EmitU16Operand(EOpcode::LoadLocal, KeySlot);
						Emitter.Emit(EOpcode::GetIndexCopy);

						if (bNeedsElementCoercion)
						{
							if (Runtime::IsUnsignedTypeName(DeclaredElementType))
							{
								Emitter.EmitCallBuiltin(EBuiltinId::ToUnsigned, 1);
							}
							else if (IsSignedIntegerTypeName(DeclaredElementType))
							{
								Emitter.EmitCallBuiltin(EBuiltinId::ToInt, 1);
							}
							else if (DeclaredElementType == "float" || DeclaredElementType == "double")
							{
								Emitter.EmitCallBuiltin(
								    DeclaredElementType == "float"
								        ? EBuiltinId::ToFloat
								        : EBuiltinId::ToDouble,
								    1);
							}
						}
						Emitter.EmitU16Operand(EOpcode::StoreLocal, ElementSlot);
					}
				}
				else if (ForEachStmt.bIsRef)
				{
					Emitter.EmitU16Operand(EOpcode::LoadLocal, IndexSlot);
					Emitter.EmitU16Operand(EOpcode::GetIndexRef, CollectionSlot);
					// BindLocal: must not StoreLocal (write-through old IndexRef into the array).
					Emitter.EmitU16Operand(EOpcode::BindLocal, ElementSlot);
				}
				else
				{
					Emitter.EmitU16Operand(EOpcode::LoadLocal, CollectionSlot);
					Emitter.EmitU16Operand(EOpcode::LoadLocal, IndexSlot);
					Emitter.Emit(EOpcode::GetIndexCopy);

					if (bNeedsElementCoercion)
					{
						if (Runtime::IsUnsignedTypeName(DeclaredElementType))
						{
							Emitter.EmitCallBuiltin(EBuiltinId::ToUnsigned, 1);
						}
						else if (IsSignedIntegerTypeName(DeclaredElementType))
						{
							Emitter.EmitCallBuiltin(EBuiltinId::ToInt, 1);
						}
						else if (DeclaredElementType == "float" || DeclaredElementType == "double")
						{
							Emitter.EmitCallBuiltin(
							    DeclaredElementType == "float"
							        ? EBuiltinId::ToFloat
							        : EBuiltinId::ToDouble,
							    1);
						}
					}
					Emitter.EmitU16Operand(EOpcode::StoreLocal, ElementSlot);
				}

				if (!CompileBlock(*ForEachStmt.Body, false))
				{
					LoopStack.pop_back();
					EndScope();
					return false;
				}

				const std::int32_t StepStart = Emitter.CurrentOffset();
				for (const std::size_t ContinuePatch : LoopStack.back().ContinuePatches)
				{
					Emitter.PatchI32(
					    ContinuePatch,
					    StepStart - static_cast<std::int32_t>(ContinuePatch + 4));
				}
				LoopStack.back().ContinuePatches.clear();

				Emitter.EmitU16Operand(EOpcode::LoadLocal, IndexSlot);
				FBytecodeConstant One;
				One.Kind = EConstantKind::I64;
				One.I64 = 1;
				Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(One));
				Emitter.Emit(EOpcode::Add);
				Emitter.EmitU16Operand(EOpcode::StoreLocal, IndexSlot);

				Emitter.EmitI32Operand(
				    EOpcode::Jmp,
				    ConditionStart - Emitter.CurrentOffset() - static_cast<std::int32_t>(Bytecode::InstructionSize(EOpcode::Jmp)));
				CloseLoop(Emitter, JumpIfFalseOperand, true);
				EndScope();
				return true;
			}
		case EStmtKind::Switch:
			{
				const FSwitchStmt& SwitchStmt = static_cast<const FSwitchStmt&>(Statement);
				std::string DiscriminantType;
				if (!TryResolveExprTypeName(*SwitchStmt.Discriminant, DiscriminantType))
				{
					ReportError("switch discriminant type could not be resolved", Statement.Location);
					return false;
				}
				DiscriminantType = NormalizeTypeAlias(DiscriminantType);
				const bool bStringDiscriminant = DiscriminantType == "string";
				const bool bWStringDiscriminant = DiscriminantType == "wstring";
				const bool bIntegerDiscriminant = IsSignedIntegerTypeName(DiscriminantType) || Runtime::IsUnsignedTypeName(DiscriminantType);
				const bool bScopedEnumDiscriminant = IsScopedEnumTypeName(DiscriminantType);
				if (!bStringDiscriminant && !bWStringDiscriminant && !bIntegerDiscriminant && !bScopedEnumDiscriminant)
				{
					ReportError(
					    "switch discriminant must be an integer, enum class, string, or wstring",
					    Statement.Location);
					return false;
				}

				bool bUnsignedDiscriminant =
				    bIntegerDiscriminant && Runtime::IsUnsignedTypeName(DiscriminantType);
				bool bInt64Discriminant =
				    DiscriminantType == "int64" || DiscriminantType == "long";
				const bool bUInt64Discriminant =
				    DiscriminantType == "uint64" || DiscriminantType == "unsigned long";
				const std::string ScopedEnumRuntimeTypeName = bScopedEnumDiscriminant
				    ? QualifyTypeName(DiscriminantType, CurrentModulePath)
				    : DiscriminantType;
				if (bScopedEnumDiscriminant && ActiveProgram)
				{
					for (const FEnumDecl& EnumDecl : ActiveProgram->Enums)
					{
						if (EnumDecl.bScoped && (EnumDecl.Name == DiscriminantType || GetSymbolLinkageName(EnumDecl.ModulePath, EnumDecl.bExported, EnumDecl.Name) == DiscriminantType))
						{
							bInt64Discriminant =
							    EnumDecl.UnderlyingTypeName == "int64" || EnumDecl.UnderlyingTypeName == "long";
							bUnsignedDiscriminant =
							    EnumDecl.UnderlyingTypeName == "unsigned char" || EnumDecl.UnderlyingTypeName == "unsigned short" || EnumDecl.UnderlyingTypeName == "unsigned int";
							break;
						}
					}
				}

				std::vector<std::vector<std::int64_t>> IntCaseValues;
				std::vector<std::vector<std::string>> StrCaseValues;
				std::vector<std::vector<std::u16string>> WStrCaseValues;
				std::unordered_set<std::int64_t> SeenIntValues;
				std::unordered_set<std::string> SeenStrValues;
				std::unordered_set<std::u16string> SeenWStrValues;
				if (bStringDiscriminant)
				{
					StrCaseValues.resize(SwitchStmt.Cases.size());
				}
				else if (bWStringDiscriminant)
				{
					WStrCaseValues.resize(SwitchStmt.Cases.size());
				}
				else
				{
					IntCaseValues.resize(SwitchStmt.Cases.size());
				}
				int DefaultIndex = -1;
				for (std::size_t CaseIndex = 0; CaseIndex < SwitchStmt.Cases.size(); ++CaseIndex)
				{
					const FSwitchCase& Case = SwitchStmt.Cases[CaseIndex];
					if (Case.bIsDefault)
					{
						if (DefaultIndex >= 0)
						{
							ReportError("duplicate default in switch", Statement.Location);
							return false;
						}
						DefaultIndex = static_cast<int>(CaseIndex);
					}
					for (const std::unique_ptr<FExpr>& Label : Case.Labels)
					{
						if (bStringDiscriminant)
						{
							std::string Value;
							if (!TryEvalSwitchCaseStringConstant(*Label, Value, Label->Location))
							{
								return false;
							}
							if (!SeenStrValues.insert(Value).second)
							{
								ReportError("duplicate case value in switch", Label->Location);
								return false;
							}
							StrCaseValues[CaseIndex].push_back(std::move(Value));
						}
						else if (bWStringDiscriminant)
						{
							std::u16string Value;
							if (!TryEvalSwitchCaseWStringConstant(*Label, Value, Label->Location))
							{
								return false;
							}
							if (!SeenWStrValues.insert(Value).second)
							{
								ReportError("duplicate case value in switch", Label->Location);
								return false;
							}
							WStrCaseValues[CaseIndex].push_back(std::move(Value));
						}
						else
						{
							if (bScopedEnumDiscriminant)
							{
								std::string LabelType;
								if (!TryResolveExprTypeName(*Label, LabelType) || NormalizeTypeAlias(LabelType) != DiscriminantType)
								{
									ReportError(
									    "case label must belong to enum class '" + DiscriminantType + "'",
									    Label->Location);
									return false;
								}
							}
							std::int64_t Value = 0;
							if (!TryEvalSwitchCaseConstant(*Label, Value, Label->Location))
							{
								return false;
							}
							if (bUnsignedDiscriminant)
							{
								std::string LabelType;
								const bool bLabelIsUInt64 =
								    bUInt64Discriminant && TryResolveExprTypeName(*Label, LabelType) && NormalizeTypeAlias(LabelType) == "uint64";
								if ((!bUInt64Discriminant && (Value < 0 || Value > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()))) || (bUInt64Discriminant && Value < 0 && !bLabelIsUInt64))
								{
									ReportError(
									    bUInt64Discriminant
									        ? "case label must be a uint64 constant"
									        : "case label must be a uint32 constant for unsigned switch",
									    Label->Location);
									return false;
								}
							}
							else if (!bInt64Discriminant && (Value < std::numeric_limits<std::int32_t>::min() || Value > std::numeric_limits<std::int32_t>::max()))
							{
								ReportError("case label out of signed int range", Label->Location);
								return false;
							}
							if (!SeenIntValues.insert(Value).second)
							{
								ReportError("duplicate case value in switch", Label->Location);
								return false;
							}
							IntCaseValues[CaseIndex].push_back(Value);
						}
					}
					if (!Case.bIsDefault && Case.Labels.empty())
					{
						ReportError("switch case requires a label", Statement.Location);
						return false;
					}
				}

				if (bScopedEnumDiscriminant && DefaultIndex < 0 && ActiveProgram)
				{
					const FEnumDecl* Enum = nullptr;
					for (const FEnumDecl& Candidate : ActiveProgram->Enums)
					{
						if (!Candidate.bScoped || !CanAccessSymbol(Candidate.ModulePath, Candidate.bExported, CurrentModulePath))
						{
							continue;
						}
						if (Candidate.Name == DiscriminantType || GetSymbolLinkageName(Candidate.ModulePath, Candidate.bExported, Candidate.Name) == DiscriminantType)
						{
							Enum = &Candidate;
							break;
						}
					}
					if (Enum)
					{
						std::vector<std::string> Missing;
						std::unordered_set<std::int64_t> RequiredValues;
						for (const FEnumMemberDecl& Member : Enum->Members)
						{
							std::int64_t Number = Member.Value;
							for (const auto& Type : Module->Enums)
							{
								if (Type.Name == DiscriminantType)
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
							if (RequiredValues.insert(Number).second && SeenIntValues.find(Number) == SeenIntValues.end())
							{
								Missing.push_back(Member.Name);
							}
						}
						if (!Missing.empty())
						{
							std::string Message =
							    "non-exhaustive switch over enum class '" + DiscriminantType + "'; missing: ";
							for (std::size_t Index = 0; Index < Missing.size(); ++Index)
							{
								if (Index != 0)
								{
									Message += ", ";
								}
								Message += Missing[Index];
							}
							Message += " (add cases or a default)";
							ReportError(Message, Statement.Location);
							return false;
						}
					}
				}

				if (!CompileExpression(*SwitchStmt.Discriminant))
				{
					return false;
				}
				const std::uint16_t DiscSlot = AllocateAssignTemp();
				Emitter.EmitU16Operand(EOpcode::StoreLocal, DiscSlot);

				LoopStack.emplace_back();
				LoopStack.back().bSwitch = true;

				std::vector<std::vector<std::size_t>> MatchJumps(SwitchStmt.Cases.size());
				for (std::size_t CaseIndex = 0; CaseIndex < SwitchStmt.Cases.size(); ++CaseIndex)
				{
					if (bStringDiscriminant)
					{
						for (const std::string& Value : StrCaseValues[CaseIndex])
						{
							Emitter.EmitU16Operand(EOpcode::LoadLocal, DiscSlot);
							FBytecodeConstant Constant;
							Constant.Kind = EConstantKind::Str;
							Constant.StringIndex = AddString(Value);
							Emitter.EmitU16Operand(EOpcode::ConstStr, AddConstant(Constant));
							Emitter.Emit(EOpcode::Eq);
							const std::size_t SkipOperand = Emitter.EmitI32Operand(EOpcode::JmpIfFalse, 0);
							MatchJumps[CaseIndex].push_back(Emitter.EmitI32Operand(EOpcode::Jmp, 0));
							Emitter.PatchI32(
							    SkipOperand,
							    Emitter.CurrentOffset() - static_cast<std::int32_t>(SkipOperand + 4));
						}
					}
					else if (bWStringDiscriminant)
					{
						for (const std::u16string& Value : WStrCaseValues[CaseIndex])
						{
							Emitter.EmitU16Operand(EOpcode::LoadLocal, DiscSlot);
							FBytecodeConstant Constant;
							Constant.Kind = EConstantKind::WStr;
							Constant.StringIndex = AddWString(Value);
							Emitter.EmitU16Operand(EOpcode::ConstStr, AddConstant(Constant));
							Emitter.Emit(EOpcode::Eq);
							const std::size_t SkipOperand = Emitter.EmitI32Operand(EOpcode::JmpIfFalse, 0);
							MatchJumps[CaseIndex].push_back(Emitter.EmitI32Operand(EOpcode::Jmp, 0));
							Emitter.PatchI32(
							    SkipOperand,
							    Emitter.CurrentOffset() - static_cast<std::int32_t>(SkipOperand + 4));
						}
					}
					else
					{
						for (const std::int64_t Value : IntCaseValues[CaseIndex])
						{
							Emitter.EmitU16Operand(EOpcode::LoadLocal, DiscSlot);
							FBytecodeConstant Constant;
							if (bScopedEnumDiscriminant)
							{
								Constant.Kind = EConstantKind::Enum;
								Constant.I64 = Value;
								Constant.StringIndex = AddString(Runtime::EnumValueTypeName(*Module, ScopedEnumRuntimeTypeName));
							}
							else if (bUInt64Discriminant)
							{
								Constant.Kind = EConstantKind::U64;
								Constant.U64 = static_cast<std::uint64_t>(Value);
							}
							else if (bUnsignedDiscriminant)
							{
								Constant.Kind = EConstantKind::U32;
								Constant.U32 = static_cast<std::uint32_t>(Value);
							}
							else if (bInt64Discriminant)
							{
								Constant.Kind = EConstantKind::I64;
								Constant.I64 = Value;
							}
							else
							{
								Constant.Kind = EConstantKind::I32;
								Constant.I32 = static_cast<std::int32_t>(Value);
							}
							Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(Constant));
							Emitter.Emit(EOpcode::Eq);
							const std::size_t SkipOperand = Emitter.EmitI32Operand(EOpcode::JmpIfFalse, 0);
							MatchJumps[CaseIndex].push_back(Emitter.EmitI32Operand(EOpcode::Jmp, 0));
							Emitter.PatchI32(
							    SkipOperand,
							    Emitter.CurrentOffset() - static_cast<std::int32_t>(SkipOperand + 4));
						}
					}
				}
				const std::size_t JumpDefaultOrEnd = Emitter.EmitI32Operand(EOpcode::Jmp, 0);

				std::vector<std::size_t> EndJumps;
				EndJumps.reserve(SwitchStmt.Cases.size());
				std::optional<std::size_t> PendingFallthroughJump;
				for (std::size_t CaseIndex = 0; CaseIndex < SwitchStmt.Cases.size(); ++CaseIndex)
				{
					const std::int32_t BodyOffset = Emitter.CurrentOffset();
					if (PendingFallthroughJump.has_value())
					{
						Emitter.PatchI32(
						    *PendingFallthroughJump,
						    BodyOffset - static_cast<std::int32_t>(*PendingFallthroughJump + 4));
						PendingFallthroughJump.reset();
					}
					for (const std::size_t MatchJump : MatchJumps[CaseIndex])
					{
						Emitter.PatchI32(
						    MatchJump,
						    BodyOffset - static_cast<std::int32_t>(MatchJump + 4));
					}
					if (DefaultIndex >= 0 && static_cast<std::size_t>(DefaultIndex) == CaseIndex)
					{
						Emitter.PatchI32(
						    JumpDefaultOrEnd,
						    BodyOffset - static_cast<std::int32_t>(JumpDefaultOrEnd + 4));
					}

					const FSwitchCase& Case = SwitchStmt.Cases[CaseIndex];
					bool bEndsWithFallthrough = false;
					if (Case.Body)
					{
						const std::vector<std::unique_ptr<FStmt>>& BodyStmts = Case.Body->Statements;
						if (!BodyStmts.empty() && BodyStmts.back() && BodyStmts.back()->Kind == EStmtKind::Fallthrough)
						{
							for (std::size_t StmtIndex = 0; StmtIndex + 1 < BodyStmts.size(); ++StmtIndex)
							{
								if (BodyStmts[StmtIndex] && CompilerDetail::StatementContainsLocalFallthrough(*BodyStmts[StmtIndex]))
								{
									ReportError(
									    "fallthrough must be the last statement in a case body",
									    BodyStmts[StmtIndex]->Location);
									LoopStack.pop_back();
									return false;
								}
							}
							if (BodyStmts.size() >= 2 && BodyStmts[BodyStmts.size() - 2])
							{
								const EStmtKind PrevKind = BodyStmts[BodyStmts.size() - 2]->Kind;
								if (PrevKind == EStmtKind::Break || PrevKind == EStmtKind::Return)
								{
									ReportError(
									    "unreachable fallthrough after break/return",
									    BodyStmts.back()->Location);
									LoopStack.pop_back();
									return false;
								}
							}
							if (CaseIndex + 1 >= SwitchStmt.Cases.size())
							{
								ReportError(
								    "fallthrough is not allowed in the last case/default",
								    BodyStmts.back()->Location);
								LoopStack.pop_back();
								return false;
							}
							bEndsWithFallthrough = true;
						}
						else
						{
							for (const std::unique_ptr<FStmt>& BodyStmt : BodyStmts)
							{
								if (BodyStmt && CompilerDetail::StatementContainsLocalFallthrough(*BodyStmt))
								{
									ReportError(
									    "fallthrough must be the last statement in a case body",
									    BodyStmt->Location);
									LoopStack.pop_back();
									return false;
								}
							}
						}

						BeginScope();
						const std::size_t CompileCount = bEndsWithFallthrough
						    ? BodyStmts.size() - 1
						    : BodyStmts.size();
						for (std::size_t StmtIndex = 0; StmtIndex < CompileCount; ++StmtIndex)
						{
							if (!BodyStmts[StmtIndex] || !CompileStatement(*BodyStmts[StmtIndex]))
							{
								EndScope();
								LoopStack.pop_back();
								return false;
							}
						}
						EndScope();
					}

					if (bEndsWithFallthrough)
					{
						PendingFallthroughJump = Emitter.EmitI32Operand(EOpcode::Jmp, 0);
					}
					else
					{
						EndJumps.push_back(Emitter.EmitI32Operand(EOpcode::Jmp, 0));
					}
				}

				if (PendingFallthroughJump.has_value())
				{
					ReportError("fallthrough is not allowed in the last case/default", Statement.Location);
					LoopStack.pop_back();
					return false;
				}

				const std::int32_t EndOffset = Emitter.CurrentOffset();
				if (DefaultIndex < 0)
				{
					Emitter.PatchI32(
					    JumpDefaultOrEnd,
					    EndOffset - static_cast<std::int32_t>(JumpDefaultOrEnd + 4));
				}
				for (const std::size_t EndJump : EndJumps)
				{
					Emitter.PatchI32(
					    EndJump,
					    EndOffset - static_cast<std::int32_t>(EndJump + 4));
				}
				for (const std::size_t BreakPatch : LoopStack.back().BreakPatches)
				{
					Emitter.PatchI32(
					    BreakPatch,
					    EndOffset - static_cast<std::int32_t>(BreakPatch + 4));
				}
				LoopStack.pop_back();
				return true;
			}
		case EStmtKind::Break:
			{
				if (LoopStack.empty())
				{
					ReportError("break outside loop or switch", Statement.Location);
					return false;
				}
				LoopStack.back().BreakPatches.push_back(Emitter.EmitI32Operand(EOpcode::Jmp, 0));
				return true;
			}
		case EStmtKind::Continue:
			{
				for (std::size_t Index = LoopStack.size(); Index > 0; --Index)
				{
					FLoopContext& Context = LoopStack[Index - 1];
					if (!Context.bSwitch)
					{
						Context.ContinuePatches.push_back(Emitter.EmitI32Operand(EOpcode::Jmp, 0));
						return true;
					}
				}
				ReportError("continue outside loop", Statement.Location);
				return false;
			}
		case EStmtKind::Fallthrough:
			{
				ReportError(
				    "fallthrough must be the last statement in a case/default body",
				    Statement.Location);
				return false;
			}
		case EStmtKind::Label:
			{
				if (ActiveFinallyDepth != 0)
				{
					ReportError("labels are not allowed inside a finally block", Statement.Location);
					return false;
				}
				const FLabelStmt& LabelStmt = static_cast<const FLabelStmt&>(Statement);
				if (LabelTable.find(LabelStmt.Name) != LabelTable.end() && LabelTable[LabelStmt.Name].Offset >= 0)
				{
					ReportErrorId("diag.duplicate_label", Statement.Location, {LabelStmt.Name});
					return false;
				}
				FLabelInfo Info;
				Info.Offset = Emitter.CurrentOffset();
				Info.ScopeDepth = static_cast<int>(Scopes.size());
				Info.ExceptionScopePath = ActiveExceptionScopePath;
				Info.Location = Statement.Location;
				LabelTable[LabelStmt.Name] = Info;
				return true;
			}
		case EStmtKind::Goto:
			{
				if (ActiveFinallyDepth != 0)
				{
					ReportError("goto is not allowed inside a finally block", Statement.Location);
					return false;
				}
				const FGotoStmt& GotoStmt = static_cast<const FGotoStmt&>(Statement);
				FGotoPatch Patch;
				Patch.Name = GotoStmt.Name;
				Patch.GotoIp = Emitter.CurrentOffset();
				Patch.OperandOffset = Emitter.EmitI32Operand(EOpcode::Jmp, 0);
				Patch.ScopeDepth = static_cast<int>(Scopes.size());
				Patch.ExceptionScopePath = ActiveExceptionScopePath;
				Patch.Location = Statement.Location;
				GotoPatches.push_back(std::move(Patch));
				return true;
			}
		default:
			{
				ReportError("unsupported statement", Statement.Location);
				return false;
			}
		}
	}

	bool VeroScriptCompiler::CompileBlock(const FBlockStmt& Block, const bool bPreserveScope)
	{
		if (!bPreserveScope)
		{
			BeginScope();
		}
		for (const std::unique_ptr<FStmt>& Statement : Block.Statements)
		{
			if (!CompileStatement(*Statement))
			{
				if (!bPreserveScope)
				{
					EndScope();
				}
				return false;
			}
		}
		if (!bPreserveScope)
		{
			EndScope();
		}
		return true;
	}

	bool VeroScriptCompiler::CompileFunction(
	    const std::string& Name,
	    const std::string& ReturnType,
	    const std::vector<FFunctionParam>& Parameters,
	    const FBlockStmt& SourceBody,
	    const bool bIsMethod,
	    const std::uint16_t OwnerTypeIndex,
	    const std::string& OwnerModulePath,
	    const FSourceLocation& Location,
	    const bool bIsVirtual,
	    const std::vector<FBytecodeCapture>* CaptureLocals,
	    const bool bIsConstMethod,
	    const FDeclMeta* DeclMeta,
	    const std::string* LexicalAccessFunctionLinkage,
	    const bool bAllowImplicitZeroReturn, const bool bConstexpr)
	{
		FConstantBranchSelectionScope BranchScope(ConstantBranchState);
		if (ConstantBranchState)
		{
			ConstantBranchState->SelectEnclosing(SourceBody.ScopeId);
		}
		if (!SourceBody.DefinitionParameterNames.empty() &&
		    SourceBody.DefinitionParameterNames.size() != Parameters.size())
		{
			ReportError("invalid supplemental definition parameter bindings", Location);
			return false;
		}
		if (Module->Functions.size() >= std::numeric_limits<std::uint16_t>::max())
		{
			ReportError("function table overflow", Location);
			return false;
		}
		for (const FFunctionParam& Param : Parameters)
		{
			if (!Param.DefaultValue)
			{
				continue;
			}
			FBytecodeConstant Unused;
			if (!TryCompileDefaultConstant(*Param.DefaultValue, Unused))
			{
				ReportError("parameter default must be a literal", Param.DefaultValue->Location);
				return false;
			}
		}

		std::uint16_t FunctionIndex = static_cast<std::uint16_t>(-1);
		const auto ExistingIt = Module->FunctionIndices.find(Name);
		if (ExistingIt != Module->FunctionIndices.end())
		{
			FunctionIndex = ExistingIt->second;
			if (FunctionIndex >= Module->Functions.size())
			{
				ReportError("invalid function stub: " + Name, Location);
				return false;
			}
			if (!Module->Functions[FunctionIndex].Code.empty())
			{
				ReportErrorId(
				    "diag.duplicate_function",
				    Location,
				    {DisplayTypeName(Name)});
				return false;
			}
		}

		FBytecodeFunction* const PreviousFunction = CurrentFunction;
		const auto* PreviousFunctionSourceBody = CurrentFunctionSourceBody;
		FLambdaInitializerContext InitializerContext(*this);
		CurrentInitializerStatement = nullptr;
		CurrentInitializerExpression = nullptr;
		CurrentInitializerField = nullptr;
		std::vector<FScope> PreviousScopes = std::move(Scopes);
		// Nested callable compilation keeps the enclosing declaration environment
		// alive without copying it for every deferred local-static initializer.
		const auto EnclosingConstantScopeCount = EnclosingConstantScopes.size();
		if (PreviousFunction)
		{
			EnclosingConstantScopes.push_back({PreviousFunction->Name, &PreviousScopes});
		}
		struct FRestoreEnclosingConstantScopes
		{
			std::vector<FEnclosingConstantScope>& Scopes;
			std::size_t Count;
			~FRestoreEnclosingConstantScopes()
			{
				Scopes.resize(Count);
			}
		} RestoreEnclosingConstantScopes{EnclosingConstantScopes, EnclosingConstantScopeCount};
		std::vector<std::unordered_map<std::string, std::string>> PreviousLocalTypeAliasScopes =
		    std::move(LocalTypeAliasScopes);
		std::vector<FLoopContext> PreviousLoopStack = std::move(LoopStack);
		std::unordered_map<std::string, FLabelInfo> PreviousLabelTable = std::move(LabelTable);
		std::vector<FGotoPatch> PreviousGotoPatches = std::move(GotoPatches);
		std::vector<FLocalInitMarker> PreviousLocalInitMarkers = std::move(LocalInitMarkers);
		std::vector<std::uint16_t> PreviousActiveCatchSlots = std::move(ActiveCatchSlots);
		std::vector<std::uint32_t> PreviousActiveExceptionScopePath =
		    std::move(ActiveExceptionScopePath);
		const std::uint16_t PreviousNextLocalSlot = NextLocalSlot;
		const std::uint32_t PreviousNextAssignTempId = NextAssignTempId;
		const std::uint32_t PreviousNextExceptionScopeId = NextExceptionScopeId;
		const std::uint32_t PreviousActiveFinallyDepth = ActiveFinallyDepth;
		const std::string PreviousReturnType = CurrentFunctionReturnType;
		const std::string PreviousModulePath = CurrentModulePath;
		const std::string PreviousNamespacePrefix = CurrentNamespacePrefix;
		const std::string PreviousOwnerTypeLinkage = CurrentOwnerTypeLinkage;
		const std::string PreviousAccessFunctionLinkage = CurrentAccessFunctionLinkage;
		const FSourceLocation PreviousEmitLocation = CurrentEmitLocation;
		const auto PreviousTemplateBindings = ActiveTypeSubst;
		for (const auto& Alias : SourceBody.DefinitionTemplateAliases)
		{
			const auto Value = PreviousTemplateBindings.find(Alias.second);
			if (Value != PreviousTemplateBindings.end())
			{
				ActiveTypeSubst[Alias.first] = Value->second;
			}
		}

		const auto RestoreOuterFunctionState = [&]()
		{
			ActiveTypeSubst = PreviousTemplateBindings;
			Scopes = std::move(PreviousScopes);
			LocalTypeAliasScopes = std::move(PreviousLocalTypeAliasScopes);
			LoopStack = std::move(PreviousLoopStack);
			LabelTable = std::move(PreviousLabelTable);
			GotoPatches = std::move(PreviousGotoPatches);
			LocalInitMarkers = std::move(PreviousLocalInitMarkers);
			ActiveCatchSlots = std::move(PreviousActiveCatchSlots);
			ActiveExceptionScopePath = std::move(PreviousActiveExceptionScopePath);
			NextLocalSlot = PreviousNextLocalSlot;
			NextAssignTempId = PreviousNextAssignTempId;
			NextExceptionScopeId = PreviousNextExceptionScopeId;
			ActiveFinallyDepth = PreviousActiveFinallyDepth;
			CurrentFunctionReturnType = PreviousReturnType;
			CurrentModulePath = PreviousModulePath;
			CurrentNamespacePrefix = PreviousNamespacePrefix;
			CurrentOwnerTypeLinkage = PreviousOwnerTypeLinkage;
			CurrentAccessFunctionLinkage = PreviousAccessFunctionLinkage;
			CurrentEmitLocation = PreviousEmitLocation;
			CurrentFunction = PreviousFunction;
			CurrentFunctionSourceBody = PreviousFunctionSourceBody;
		};

		// Lambda type names remain visible from their lexical scope without
		// becoming runtime captures or exposing caller locals to ordinary functions.
		if (CaptureLocals)
		{
			LocalTypeAliasScopes = PreviousLocalTypeAliasScopes;
		}

		CurrentEmitLocation = Location;
		CurrentAccessFunctionLinkage = LexicalAccessFunctionLinkage
		    ? *LexicalAccessFunctionLinkage
		    : Name;
		if (Parameters.size() > 255)
		{
			ReportErrorId("diag.too_many_parameters_max_255", Location);
			RestoreOuterFunctionState();
			return false;
		}
		if (NormalizeTypeAlias(ReturnType) == "auto")
		{
			ReportErrorId("diag.function_return_type_cannot_be_auto", Location);
			RestoreOuterFunctionState();
			return false;
		}

		// Signatures precede the definition body and use the declaration module.
		CurrentModulePath = OwnerModulePath;
		CurrentNamespacePrefix = CaptureLocals ? PreviousNamespacePrefix : NamespaceFromFunctionLinkage(Name);
		const std::string EffectiveReturnType = SubstituteTypeName(ReturnType);
		std::string ClosureTypeName;
		// Always record ClosureTypeName (free functions, lambdas, and class/struct methods) so
		// JIT Mixed/native bridges can decode scalar params/returns without guessing.
		{
			std::vector<std::string> ClosureParamTypes;
			ClosureParamTypes.reserve(Parameters.size());
			for (const FFunctionParam& Parameter : Parameters)
			{
				std::string ParamType = QualifyTypeName(
				    NormalizeTypeAlias(
				        SubstituteTypeName(Parameter.TypeName),
				        OwnerModulePath),
				    OwnerModulePath);
				if (Parameter.bIsVariadic)
				{
					ParamType = MakeDynamicArrayTypeName(ParamType);
				}
				if (Parameter.bIsRef)
				{
					ParamType = Parameter.bIsConst
					    ? "const ref " + ParamType
					    : "ref " + ParamType;
				}
				ClosureParamTypes.push_back(std::move(ParamType));
			}
			ClosureTypeName = Runtime::MakeFunctionTypeName(
			    ClosureParamTypes,
			    QualifyTypeName(
			        NormalizeTypeAlias(EffectiveReturnType, OwnerModulePath),
			        OwnerModulePath));
		}
		if (FunctionIndex == static_cast<std::uint16_t>(-1))
		{
			FBytecodeFunction Function;
			Function.Name = Name;
			Function.ModulePath = OwnerModulePath;
			Function.bReturnsVoid = EffectiveReturnType == "void";
			Function.bReturnsRef = Runtime::IsReferenceTypeName(EffectiveReturnType);
			Function.bIsMethod = bIsMethod;
			Function.bIsVirtual = bIsVirtual;
			Function.bIsConstMethod = bIsConstMethod;
			Function.OwnerTypeIndex = OwnerTypeIndex;
			Function.ParamCount = static_cast<std::uint8_t>(Parameters.size());
			Function.Location = Location;
			Function.ClosureTypeName = ClosureTypeName;
			Function.ParamFlags.clear();
			Function.ParamFlags.reserve(Parameters.size());
			for (const FFunctionParam& Parameter : Parameters)
			{
				std::uint8_t Flag = 0;
				if (Parameter.bIsRef)
				{
					Flag |= BytecodeParamFlagRef;
					if (Parameter.bIsConst)
					{
						Flag |= BytecodeParamFlagConstRef;
					}
				}
				Function.ParamFlags.push_back(Flag);
			}
			if (DeclMeta)
			{
				Function.DeclMeta = *DeclMeta;
			}
			Module->Functions.push_back(Function);
			FunctionIndex = static_cast<std::uint16_t>(Module->Functions.size() - 1);
			Module->FunctionIndices.emplace(Name, FunctionIndex);
		}
		else
		{
			FBytecodeFunction& Function = Module->Functions[FunctionIndex];
			Function.Name = Name;
			Function.ModulePath = OwnerModulePath;
			Function.bReturnsVoid = EffectiveReturnType == "void";
			Function.bReturnsRef = Runtime::IsReferenceTypeName(EffectiveReturnType);
			Function.bIsMethod = bIsMethod;
			Function.bIsVirtual = bIsVirtual;
			Function.bIsConstMethod = bIsConstMethod;
			Function.OwnerTypeIndex = OwnerTypeIndex;
			Function.ParamCount = static_cast<std::uint8_t>(Parameters.size());
			Function.Location = Location;
			Function.ClosureTypeName = ClosureTypeName;
			Function.Code.clear();
			Function.LocalNames.clear();
			Function.DebugLines.clear();
			Function.ExceptionRegions.clear();
			Function.LocalStaticInitializers.clear();
			Function.LocalStaticSemanticIdentity = 0;
			Function.bUsesLocalStaticStorage = false;
			Function.LocalCount = 0;
			Function.ParamFlags.clear();
			Function.ParamFlags.reserve(Parameters.size());
			for (const FFunctionParam& Parameter : Parameters)
			{
				std::uint8_t Flag = 0;
				if (Parameter.bIsRef)
				{
					Flag |= BytecodeParamFlagRef;
					if (Parameter.bIsConst)
					{
						Flag |= BytecodeParamFlagConstRef;
					}
				}
				Function.ParamFlags.push_back(Flag);
			}
			if (DeclMeta)
			{
				Function.DeclMeta = *DeclMeta;
			}
		}

		std::string DeclaringOwner;
		// Preserve source access for executable reflection. A declaration's metadata
		// address is stable while its body is compiled, including generic instances.
		if (DeclMeta && FunctionIndex < Module->Functions.size())
		{
			ApplyTemplateInstanceMetadata(Module->Functions[FunctionIndex].TemplateInstance, DeclMeta);
			const auto ApplyMethod = [&](const FMethodDecl& Method, const std::string& Owner,
			                             const std::vector<std::string>& OwnerParameters) -> bool
			{
				auto& Compiled = Module->Functions[FunctionIndex];
				Compiled.Access = Method.Access;
				Compiled.bIsStaticMethod = Method.bIsStatic;
				DeclaringOwner = Owner;
				std::vector<std::string> Arguments;
				for (const auto& Parameter : OwnerParameters)
				{
					const auto Bound = ActiveTypeSubst.find(Parameter);
					if (Bound == ActiveTypeSubst.end()) return false;
					Arguments.push_back(Bound->second);
				}
				DeclaringOwner = MangleGenericInstanceName(DeclaringOwner, Arguments);
				Compiled.bDefaulted = Method.bDefaulted;
				if (Method.bDefaulted)
				{
					Compiled.SpecialMemberKind = Method.Name == "operator=" ? ESpecialMemberKind::CopyAssignment
					    : Method.Name == "operator=="                       ? ESpecialMemberKind::Equality
					    : Method.Name == "operator!="                       ? ESpecialMemberKind::Inequality
					                                                        : ESpecialMemberKind::None;
				}
				return true;
			};
			const auto ApplyBuiltinMethod = [&](const auto& Declarations) -> bool
			{
				for (const auto& Declaration : Declarations)
				{
					for (const auto& Method : Declaration.Methods)
					{
						if (&Method.DeclMeta == DeclMeta)
						{
							return ApplyMethod(Method, GetSymbolLinkageName(Declaration.ModulePath, Declaration.bExported, Declaration.Name), TemplateParameterNames(Declaration));
						}
					}
				}
				return false;
			};
			if (const auto Source = SourceMethodContexts.find(DeclMeta); Source != SourceMethodContexts.end())
			{
				const auto& Context = Source->second;
				(void)ApplyMethod(*Context.Method, Context.OwnerLinkage, Context.OwnerParameters);
			}
			else
			{
				(void)(ApplyBuiltinMethod(BuiltinStructTemplates) || ApplyBuiltinMethod(BuiltinClassTemplates));
			}
		}

		CurrentModulePath = OwnerModulePath;
		CurrentNamespacePrefix = CaptureLocals ? PreviousNamespacePrefix : NamespaceFromFunctionLinkage(Name);
		if (bIsMethod && OwnerTypeIndex < Module->Types.size())
		{
			CurrentOwnerTypeLinkage = Module->Types[OwnerTypeIndex].Name;
		}
		else if (bIsMethod && !CurrentInterfaceOwnerLinkage.empty())
		{
			// Interface default bodies use OwnerTypeIndex == 0xFFFF; keep declaring
			// interface as access context so private/protected members resolve.
			CurrentOwnerTypeLinkage = CurrentInterfaceOwnerLinkage;
		}
		else
		{
			CurrentOwnerTypeLinkage = std::move(DeclaringOwner);
		}
		for (const FFunctionParam& Parameter : Parameters)
		{
			if (!ValidateFieldTypeName(Parameter.TypeName, Location))
			{
				RestoreOuterFunctionState();
				return false;
			}
			const auto& LocalName = SourceBody.DefinitionParameterNames.empty() ? Parameter.Name : SourceBody.DefinitionParameterNames[&Parameter - Parameters.data()];
			if (CompilerDetail::IsVariantIntrinsicName(LocalName) && Runtime::IsFunctionTypeName(NormalizeTypeAlias(SubstituteTypeName(Parameter.TypeName), OwnerModulePath)))
			{
				ReportErrorId("diag.variant_reserved_name", Location);
				RestoreOuterFunctionState();
				return false;
			}
		}

		CurrentFunction = &Module->Functions[FunctionIndex];
		CurrentFunction->Captures = CaptureLocals ? *CaptureLocals : std::vector<FBytecodeCapture>{};
		CurrentFunctionReturnType = QualifyTypeName(
		    NormalizeTypeAlias(EffectiveReturnType),
		    OwnerModulePath);
		Scopes.clear();
		LoopStack.clear();
		LabelTable.clear();
		GotoPatches.clear();
		LocalInitMarkers.clear();
		ActiveCatchSlots.clear();
		ActiveExceptionScopePath.clear();
		ActiveFinallyDepth = 0;
		NextLocalSlot = 0;
		NextAssignTempId = 0;
		NextExceptionScopeId = 1;
		if (CaptureLocals)
		{
			// Enum names are lexical constants, not closure storage. Preserve their
			// declaration identities while excluding names hidden by runtime locals.
			FScope Constants;
			for (const auto& Outer : PreviousScopes)
			{
				for (const auto& Local : Outer.Locals)
				{
					Constants.EnumConstants.erase(Local.first);
				}
				for (const auto& Static : Outer.StaticLocals)
				{
					Constants.EnumConstants.erase(Static.first);
				}
				for (const auto& Enum : Outer.EnumConstants)
				{
					Constants.EnumConstants[Enum.first] = Enum.second;
				}
				for (const auto& Local : Outer.Locals)
				{
					Constants.StaticLocals.erase(Local.first);
				}
				for (const auto& Enum : Outer.EnumConstants)
				{
					Constants.StaticLocals.erase(Enum.first);
				}
				for (const auto& Static : Outer.StaticLocals)
				{
					Constants.StaticLocals[Static.first] = Static.second;
				}
			}
			if (!Constants.EnumConstants.empty() || !Constants.StaticLocals.empty())
			{
				BeginScope();
				Scopes.back().EnumConstants = std::move(Constants.EnumConstants);
				Scopes.back().StaticLocals = std::move(Constants.StaticLocals);
			}
		}
		BeginScope();
		for (const FFunctionParam& Parameter : Parameters)
		{
			AllocateLocal(SourceBody.DefinitionParameterNames.empty() ? Parameter.Name : SourceBody.DefinitionParameterNames[&Parameter - Parameters.data()]);
		}
		if (!Scopes.empty())
		{
			for (const FFunctionParam& Parameter : Parameters)
			{
				std::string LocalType = QualifyTypeName(
				    NormalizeTypeAlias(SubstituteTypeName(Parameter.TypeName), OwnerModulePath),
				    OwnerModulePath);
				if (Parameter.bIsVariadic)
				{
					LocalType = MakeDynamicArrayTypeName(LocalType);
				}
				const std::string& LocalName = SourceBody.DefinitionParameterNames.empty() ? Parameter.Name : SourceBody.DefinitionParameterNames[&Parameter - Parameters.data()];
				Scopes.back().LocalTypes.emplace(LocalName, std::move(LocalType));
				if (Parameter.bIsConst)
				{
					Scopes.back().LocalConsts.insert(LocalName);
				}
				if (Parameter.bIsRef)
				{
					Scopes.back().LocalRefs.insert(LocalName);
				}
			}
		}
		if (CaptureLocals)
		{
			for (const FBytecodeCapture& Capture : *CaptureLocals)
			{
				AllocateLocal(Capture.Name);
				Scopes.back().LocalTypes.emplace(Capture.Name, Capture.TypeName);
				if (Capture.Mode != EClosureCaptureMode::Value)
				{
					Scopes.back().LocalRefs.insert(Capture.Name);
				}
				if (!Capture.bWritable)
				{
					Scopes.back().LocalConsts.insert(Capture.Name);
				}
			}
		}

		if (!SourceBody.DefinitionModulePath.empty())
		{
			CurrentModulePath = SourceBody.DefinitionModulePath;
			CurrentNamespacePrefix = SourceBody.DefinitionNamespace;
		}
		std::unique_ptr<FBlockStmt> SpecializedBody;
		if (!LowerConstantBranches(SourceBody, SpecializedBody))
		{
			RestoreOuterFunctionState();
			return false;
		}
		const FBlockStmt& Body = SpecializedBody ? *SpecializedBody : SourceBody;
		if (bConstexpr && (bIsMethod || bIsVirtual || !ValidateConstexprFunction(ReturnType, Parameters, Body, CurrentModulePath)))
		{
			if (!HasError())
			{
				ReportError("constexpr requires a nonvirtual free or static function", Location);
			}
			RestoreOuterFunctionState();
			return false;
		}
		CurrentFunction->bConstexpr = bConstexpr;
		if (CurrentFunction->bDefaulted)
		{
			const auto DiagnosticCount = Diagnostics.size();
			const auto Kind = CurrentFunction->SpecialMemberKind;
			if (!CompileDefaultedMethodBody(Parameters, Kind, Location))
			{
				// Shape errors are immediate; subobject failures make only this
				// operation unavailable. Finalization propagates that state to uses.
				if (CurrentFunction->SpecialMemberKind == ESpecialMemberKind::None)
				{
					RestoreOuterFunctionState();
					return false;
				}
				CurrentFunction->UnavailableReason = Error.Message;
				bHasError = false;
				Error = {};
				Diagnostics.resize(DiagnosticCount);
				CurrentFunction->Code.clear();
				CurrentFunction->DebugLines.clear();
				CurrentFunction->PauseMarkerOffsets.clear();
				FEmitter Emitter = MakeEmitter();
				Emitter.Emit(EOpcode::ConstNull);
				Emitter.Emit(EOpcode::Return);
			}
			RestoreOuterFunctionState();
			return true;
		}
		CurrentFunctionSourceBody = &Body;
		if (!CompileBlock(Body))
		{
			RestoreOuterFunctionState();
			return false;
		}
		if (EffectiveReturnType == "void" && !CompilerDetail::BlockAlwaysReturns(Body))
		{
			FBytecodeConstant Zero;
			Zero.Kind = EConstantKind::I32;
			FEmitter Emitter = MakeEmitter();
			Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(Zero));
			Emitter.Emit(EOpcode::Return);
		}

		if (EffectiveReturnType != "void" && !CompilerDetail::BlockAlwaysReturns(Body) && bAllowImplicitZeroReturn)
		{
			FBytecodeConstant Zero;
			Zero.Kind = EConstantKind::I32;
			FEmitter Emitter = MakeEmitter();
			Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(Zero));
			Emitter.Emit(EOpcode::Return);
		}

		if (EffectiveReturnType != "void" && !CompilerDetail::BlockAlwaysReturns(Body) && !bAllowImplicitZeroReturn)
		{
			ReportErrorId("diag.missing_return_in_function", Location, {Name});
			RestoreOuterFunctionState();
			return false;
		}

		if (!ResolveAndPatchGotos())
		{
			RestoreOuterFunctionState();
			return false;
		}

		// Exception region offsets are part of the executable format. The current
		// peephole optimizer does not expose a relocation map, so preserve bytecode
		// offsets for protected functions rather than risk stale handlers.
		if (CurrentFunction->ExceptionRegions.empty())
		{
			OptimizeFunctionBytecode(*CurrentFunction);
		}
		if (!CurrentFunction->LocalStaticInitializers.empty())
		{
			CurrentFunction->LocalStaticSemanticIdentity = ComputeCallableSemanticIdentity(&Body);
		}

		RestoreOuterFunctionState();
		return true;
	}

	bool VeroScriptCompiler::ExpressionCrossesSourceLine(
	    const FExpr& Root, const FSourceLocation& SourceLocation)
	{
		if (Root.Location.Line != SourceLocation.Line || Root.Location.FileLabel != SourceLocation.FileLabel)
		{
			return true;
		}
		std::vector<const FExpr*> Pending{&Root};
		const auto Add = [&](const std::unique_ptr<FExpr>& Child)
		{
			if (Child)
			{
				Pending.push_back(Child.get());
			}
		};
		while (!Pending.empty())
		{
			const FExpr* const Expression = Pending.back();
			Pending.pop_back();
			if (Expression->Location.Line != SourceLocation.Line || Expression->Location.FileLabel != SourceLocation.FileLabel)
			{
				return true;
			}
			switch (Expression->Kind)
			{
			case EExprKind::Unary:
				{
					Add(static_cast<const FUnaryExpr*>(Expression)->Operand);
					break;
				}
			case EExprKind::Binary:
				{
					const auto& Binary = *static_cast<const FBinaryExpr*>(Expression);
					Add(Binary.Left);
					Add(Binary.Right);
					break;
				}
			case EExprKind::Assign:
				{
					const auto& Assign = *static_cast<const FAssignExpr*>(Expression);
					Add(Assign.Target);
					Add(Assign.Value);
					break;
				}
			case EExprKind::Conditional:
				{
					const auto& Conditional = *static_cast<const FConditionalExpr*>(Expression);
					Add(Conditional.Condition);
					Add(Conditional.ThenExpr);
					Add(Conditional.ElseExpr);
					break;
				}
			case EExprKind::Ref:
				{
					Add(static_cast<const FRefExpr*>(Expression)->Operand);
					break;
				}
			case EExprKind::Call:
				{
					const auto& Call = *static_cast<const FCallExpr*>(Expression);
					Add(Call.CalleeExpr);
					for (const FExpr* Argument : Call.GetArguments())
					{
						if (Argument)
						{
							Pending.push_back(Argument);
						}
					}
					break;
				}
			case EExprKind::MemberAccess:
				{
					Add(static_cast<const FMemberAccessExpr*>(Expression)->Object);
					break;
				}
			case EExprKind::MethodCall:
				{
					const auto& Call = *static_cast<const FMethodCallExpr*>(Expression);
					Add(Call.Object);
					for (const FExpr* Argument : Call.GetArguments())
					{
						if (Argument)
						{
							Pending.push_back(Argument);
						}
					}
					break;
				}
			case EExprKind::IndexAccess:
				{
					const auto& Index = *static_cast<const FIndexAccessExpr*>(Expression);
					Add(Index.Object);
					Add(Index.Index);
					break;
				}
			case EExprKind::ArrayInit:
				{
					for (const auto& Element : static_cast<const FArrayInitExpr*>(Expression)->Elements)
					{
						Add(Element);
					}
					break;
				}
			case EExprKind::MapInit:
				{
					for (const auto& Entry : static_cast<const FMapInitExpr*>(Expression)->Entries)
					{
						Add(Entry.Key);
						Add(Entry.Value);
					}
					break;
				}
			case EExprKind::Lambda:
				{
					const auto& Lambda = *static_cast<const FLambdaExpr*>(Expression);
					Add(Lambda.ExpressionBody);
					// A body may move the emit location while creating the closure.
					if (Lambda.Body)
					{
						return true;
					}
					break;
				}
			default:
				{
					break;
				}
			}
		}
		return false;
	}
}
