// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"

#include "VeroScriptAst.h"
#include "VeroScriptBuiltins.h"
#include "VeroScriptMemberAccess.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptStandardLibrary.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace VeroScript
{
	namespace
	{
		// Repeated overload/const checks can ask for the same indexed receiver
		// within one type query. Keep successful raw types only for that query;
		// statements, expected types and compiler reuse never share this cache.
		struct FExpressionTypeQuery
		{
			const VeroScriptCompiler* Compiler = nullptr;
			// Some standard libraries allocate buckets even for an empty map.
			// Ordinary type queries and nested scopes need no cache allocation.
			std::optional<std::unordered_map<const FExpr*, std::pair<std::string, std::string>>> IndexTypes;
		};
		thread_local FExpressionTypeQuery* ActiveExpressionTypeQuery = nullptr;

		class FExpressionTypeQueryScope
		{
		public:
			explicit FExpressionTypeQueryScope(const VeroScriptCompiler* Compiler)
			    : Previous(ActiveExpressionTypeQuery)
			{
				if (!Previous || Previous->Compiler != Compiler)
				{
					Local.Compiler = Compiler;
					ActiveExpressionTypeQuery = &Local;
				}
			}
			~FExpressionTypeQueryScope()
			{
				if (ActiveExpressionTypeQuery == &Local)
				{
					ActiveExpressionTypeQuery = Previous;
				}
			}
		private:
			FExpressionTypeQuery Local;
			FExpressionTypeQuery* Previous;
		};
	}

	bool VeroScriptCompiler::TryResolveExprTypeName(
	    const FExpr& Expression,
	    std::string& OutTypeName) const
	{
		if (!TryResolveExprRawTypeName(Expression, OutTypeName))
		{
			return false;
		}
		// A reference return is an lvalue whose ordinary expression type is the
		// referenced value type. Only the compiler's explicit binding/assignment
		// paths request the raw `ref T` signature.
		if (Runtime::IsReferenceTypeName(OutTypeName))
		{
			OutTypeName.assign(Runtime::StripReferenceTypeName(OutTypeName));
		}
		return true;
	}

	bool VeroScriptCompiler::TryResolveExprRawTypeName(
	    const FExpr& Expression,
	    std::string& OutTypeName) const
	{
		if (!ActiveProgram)
		{
			return false;
		}
		FExpressionTypeQueryScope QueryScope(this);
		switch (Expression.Kind)
		{
		case EExprKind::IntegerLiteral:
			{
				const FIntegerLiteralExpr& Literal = static_cast<const FIntegerLiteralExpr&>(Expression);
				OutTypeName = Literal.bIs64
				    ? (Literal.bUnsigned ? "uint64" : "int64")
				    : (Literal.bUnsigned ? "unsigned int" : "int");
				return true;
			}
		case EExprKind::CharacterLiteral:
			{
				OutTypeName = "char";
				return true;
			}
		case EExprKind::FloatLiteral:
			{
				OutTypeName = static_cast<const FFloatLiteralExpr&>(Expression).bFloat32
				    ? "float"
				    : "double";
				return true;
			}
		case EExprKind::StringLiteral:
			{
				OutTypeName = "string";
				return true;
			}
		case EExprKind::WStringLiteral:
			{
				OutTypeName = "wstring";
				return true;
			}
		case EExprKind::BooleanLiteral:
			{
				OutTypeName = "bool";
				return true;
			}
		case EExprKind::NullptrLiteral:
			{
				OutTypeName = "nullptr";
				return true;
			}
		case EExprKind::ArrayInit:
			{
				const FArrayInitExpr& Init = static_cast<const FArrayInitExpr&>(Expression);
				if (Init.TypeName.empty())
				{
					return false;
				}
				OutTypeName = NormalizeTypeAlias(SubstituteTypeName(Init.TypeName));
				return !OutTypeName.empty();
			}
		case EExprKind::MapInit:
			{
				const FMapInitExpr& Init = static_cast<const FMapInitExpr&>(Expression);
				if (Init.TypeName.empty())
				{
					return false;
				}
				OutTypeName = NormalizeTypeAlias(SubstituteTypeName(Init.TypeName));
				return !OutTypeName.empty();
			}
		case EExprKind::Ref:
			return TryResolveExprTypeName(
			    *static_cast<const FRefExpr&>(Expression).Operand,
			    OutTypeName);
		case EExprKind::This:
			{
				if (!CurrentFunction || !CurrentFunction->bIsMethod)
				{
					return false;
				}
				if (CurrentFunction->OwnerTypeIndex < Module->Types.size())
				{
					// Keep linkage name so private types are not rebound to an exported same-name type.
					OutTypeName = Module->Types[CurrentFunction->OwnerTypeIndex].Name;
					return !OutTypeName.empty();
				}
				// Interface default methods: OwnerTypeIndex is 0xFFFF; type `this` as the interface.
				if (!CurrentInterfaceOwnerLinkage.empty())
				{
					OutTypeName = CurrentInterfaceOwnerLinkage;
					return true;
				}
				return false;
			}
		case EExprKind::Lambda:
			{
				const FLambdaExpr& Lambda = static_cast<const FLambdaExpr&>(Expression);
				std::vector<std::string> ExpectedParamTypes;
				std::string ExpectedReturnType;
				const bool bHaveExpectedFn = !ExpressionExpectedType.empty() && Runtime::ParseFunctionType(ExpressionExpectedType, ExpectedParamTypes, ExpectedReturnType);
				bool bNeedsContext = Lambda.ReturnType.empty();
				for (const FFunctionParam& Parameter : Lambda.Parameters)
				{
					if (Parameter.TypeName.empty())
					{
						bNeedsContext = true;
						break;
					}
				}
				if (bNeedsContext)
				{
					if (!bHaveExpectedFn || Lambda.Parameters.size() != ExpectedParamTypes.size())
					{
						return false;
					}
				}
				std::vector<std::string> ParamTypes;
				ParamTypes.reserve(Lambda.Parameters.size());
				for (std::size_t Index = 0; Index < Lambda.Parameters.size(); ++Index)
				{
					std::string ParamType = Lambda.Parameters[Index].TypeName;
					const bool bContextTyped = ParamType.empty();
					if (ParamType.empty())
					{
						ParamType = ExpectedParamTypes[Index];
					}
					if (ParamType.empty())
					{
						return false;
					}
					if (!bContextTyped && Lambda.Parameters[Index].bIsRef)
					{
						ParamType = Lambda.Parameters[Index].bIsConst
						    ? "const ref " + ParamType
						    : "ref " + ParamType;
					}
					ParamTypes.push_back(QualifyTypeName(
					    NormalizeTypeAlias(SubstituteTypeName(ParamType)),
					    CurrentModulePath));
				}
				std::string ReturnType = Lambda.ReturnType;
				if (ReturnType.empty())
				{
					if (!bHaveExpectedFn)
					{
						return false;
					}
					ReturnType = ExpectedReturnType;
				}
				if (ReturnType.empty())
				{
					return false;
				}
				OutTypeName = Runtime::MakeFunctionTypeName(
				    ParamTypes,
				    QualifyTypeName(
				        NormalizeTypeAlias(SubstituteTypeName(ReturnType)),
				        CurrentModulePath));
				return true;
			}
		case EExprKind::Identifier:
			{
				const auto& Identifier = static_cast<const FIdentifierExpr&>(Expression);
				if (Identifier.bExplicitTemplateArguments)
				{
					const FFunctionDecl* Function = nullptr;
					std::vector<std::string> Arguments;
					return ResolveTemplateFunctionReference(Identifier, Function, Arguments, OutTypeName);
				}
				const std::string& Name = Identifier.Name;
				const auto Binding = ActiveTypeSubst.find(Name);
				FTemplateArgument Argument;
				if (Binding != ActiveTypeSubst.end() && Binding->second.rfind("@value:", 0) == 0 && DecodeTemplateArgument(std::string_view(Binding->second).substr(7), Argument) && Argument.Kind == ETemplateParameterKind::Value)
				{
					const auto* Enum = Argument.Value.Type.bEnum ? FindEnumDeclaration(Argument.Value.Type.Name) : nullptr;
					OutTypeName = Enum && EnclosingTemplateParameterNames(Enum->Scope).empty()
					    ? (Enum->bScoped ? Enum->Name : Enum->UnderlyingTypeName)
					    : Argument.Value.Type.Name;
					return true;
				}

				// Match identifier emission: locals, implicit fields, enums, free
				// functions, globals, then implicit host properties.
				std::uint16_t LocalSlot = 0;
				if ((ResolveLocal(Name, LocalSlot) || !ResolveLocalStaticLinkageName(Name).empty()) && TryResolveVariableTypeName(Name, OutTypeName))
				{
					OutTypeName = NormalizeTypeAlias(OutTypeName);
					return true;
				}
				std::uint16_t FieldIndex = 0;
				if (ResolveLocalEnumConstantLinkageName(Name).empty() && ResolveThisField(Name, FieldIndex) && CurrentFunction && CurrentFunction->OwnerTypeIndex < Module->Types.size())
				{
					const FBytecodeTypeDesc& OwnerTypeDesc =
					    Module->Types[CurrentFunction->OwnerTypeIndex];
					// Prefer bytecode field types (already substituted for generic instances).
					for (const FBytecodeFieldDesc& Field : OwnerTypeDesc.Fields)
					{
						if (Field.Name == Name)
						{
							OutTypeName = NormalizeTypeAlias(Field.TypeName);
							return true;
						}
					}
					const std::string& OwnerLinkageName = OwnerTypeDesc.Name;
					std::string AstOwnerLinkage = OwnerLinkageName;
					const std::string TemplateBase = GenericTemplateBaseLinkage(OwnerLinkageName);
					if (!TemplateBase.empty())
					{
						AstOwnerLinkage = TemplateBase;
					}
					for (const FStructDecl& StructDecl : ActiveProgram->Structs)
					{
						if (GetSymbolLinkageName(
						        StructDecl.ModulePath,
						        StructDecl.bExported,
						        StructDecl.Name) != AstOwnerLinkage)
						{
							continue;
						}
						if (!FindFieldTypeOnOwnerType(StructDecl.Name, Name, OutTypeName))
						{
							return false;
						}
						OutTypeName = NormalizeTypeAlias(
						    SubstituteTypeName(OutTypeName));
						return true;
					}
					for (const FClassDecl& ClassDecl : ActiveProgram->Classes)
					{
						if (GetSymbolLinkageName(
						        ClassDecl.ModulePath,
						        ClassDecl.bExported,
						        ClassDecl.Name) != AstOwnerLinkage)
						{
							continue;
						}
						if (!FindFieldTypeOnOwnerType(ClassDecl.Name, Name, OutTypeName))
						{
							return false;
						}
						OutTypeName = NormalizeTypeAlias(
						    SubstituteTypeName(OutTypeName));
						return true;
					}
				}
				const std::string EnumConstantLinkage =
				    ResolveEnumConstantLinkageName(Name, CurrentModulePath);
				if (!EnumConstantLinkage.empty() && EnumConstants.find(EnumConstantLinkage) != EnumConstants.end())
				{
					const auto TypeIt =
					    EnumConstantTypeNames.find(EnumConstantLinkage);
					OutTypeName = TypeIt == EnumConstantTypeNames.end()
					    ? std::string{"int"}
					    : TypeIt->second;
					return true;
				}
				if (const FFunctionDecl* const FreeFunction =
				        FindUniqueFreeFunction(Name, CurrentModulePath))
				{
					if (TemplateParameterNames(*FreeFunction).empty())
					{
						OutTypeName = MakeFunctionTypeFromDecl(*FreeFunction);
						return true;
					}
				}
				if (TryResolveVariableTypeName(Name, OutTypeName))
				{
					OutTypeName = NormalizeTypeAlias(OutTypeName);
					return true;
				}
				// Implicit host base property (see the Identifier compile fallback).
				if (const std::string HostBase = CurrentMethodHostBaseTypeName();
				    !HostBase.empty())
				{
					std::uint16_t HostGetterIndex = static_cast<std::uint16_t>(-1);
					if (const FNativeFunction* const HostGetter = FindNative(
					        MakeHostPropertyGetterNativeName(HostBase, Name),
					        HostGetterIndex))
					{
						OutTypeName = ResolveNativeReturnTypeName(*HostGetter);
						return true;
					}
				}
				return false;
			}
		case EExprKind::MemberAccess:
			{
				const FMemberAccessExpr& MemberAccess = static_cast<const FMemberAccessExpr&>(Expression);
				std::string QualifiedFreeName;
				std::string QualifiedGlobalName;
				if (MemberAccess.Object && !IsSuperReceiver(*MemberAccess.Object) && TryBuildNamespaceQualifiedName(*MemberAccess.Object, MemberAccess.Member, QualifiedFreeName) && FindFunctionDeclForReference(QualifiedFreeName, CurrentModulePath) && (MemberAccess.bExplicitTemplateArguments || (ResolveEnumConstantLinkageName(QualifiedFreeName, CurrentModulePath).empty() && !TryResolveNamespaceQualifiedGlobal(*MemberAccess.Object, MemberAccess.Member, QualifiedGlobalName))))
				{
					// Namespace function values use the same signature and overload rules as identifiers.
					FIdentifierExpr Reference(std::move(QualifiedFreeName));
					Reference.Location = Expression.Location;
					Reference.TypeArgs = MemberAccess.TypeArgs;
					Reference.bExplicitTemplateArguments = MemberAccess.bExplicitTemplateArguments;
					return TryResolveExprRawTypeName(Reference, OutTypeName);
				}
				if (MemberAccess.bExplicitTemplateArguments)
				{
					const FMethodDecl* Method = nullptr;
					std::string Owner;
					return ResolveTemplateMethodReference(MemberAccess, Method, Owner, OutTypeName);
				}
				if (MemberAccess.Object && IsSuperReceiver(*MemberAccess.Object))
				{
					if (const std::string HostBase = CurrentMethodHostBaseTypeName();
					    !HostBase.empty())
					{
						std::uint16_t HostGetterIndex = static_cast<std::uint16_t>(-1);
						if (const FNativeFunction* const HostGetter = FindNative(
						        MakeHostPropertyGetterNativeName(HostBase, MemberAccess.Member),
						        HostGetterIndex))
						{
							OutTypeName = ResolveNativeReturnTypeName(*HostGetter);
							return true;
						}
					}
					if (!CurrentFunction || !Module || CurrentFunction->OwnerTypeIndex >= Module->Types.size())
					{
						return false;
					}
					if (!FindFieldTypeOnOwnerType(
					        Module->Types[CurrentFunction->OwnerTypeIndex].Name,
					        MemberAccess.Member,
					        OutTypeName))
					{
						return false;
					}
					OutTypeName = NormalizeTypeAlias(OutTypeName);
					return true;
				}
				std::int64_t EnumValue = 0;
				std::string DisplayName;
				std::string ScopedEnumType;
				if (TryResolveEnumQualifiedConstant(
				        *MemberAccess.Object,
				        MemberAccess.Member,
				        EnumValue,
				        DisplayName,
				        &ScopedEnumType))
				{
					OutTypeName = ScopedEnumType.empty() ? "int" : ScopedEnumType;
					if (ScopedEnumType.empty() && MemberAccess.Object->Kind == EExprKind::Identifier && ActiveProgram)
					{
						const std::string& EnumName =
						    static_cast<const FIdentifierExpr&>(
						        *MemberAccess.Object)
						        .Name;
						for (const FEnumDecl& EnumDecl : ActiveProgram->Enums)
						{
							if (EnumDecl.Name == EnumName)
							{
								OutTypeName = EnumDecl.UnderlyingTypeName;
								break;
							}
						}
					}
					return true;
				}
				{
					// C++-style static data member: `Type.Field` / `instance.Field` type.
					std::string StaticGlobalName;
					std::string StaticTypeName;
					bool bStaticConst = false;
					std::string StaticAccessOwner;
					bool bStaticTypeNameReceiver = false;
					if (TryResolveStaticField(
					        *MemberAccess.Object,
					        MemberAccess.Member,
					        StaticGlobalName,
					        StaticTypeName,
					        bStaticConst,
					        StaticAccessOwner,
					        bStaticTypeNameReceiver))
					{
						OutTypeName = NormalizeTypeAlias(StaticTypeName);
						return true;
					}
				}
				{
					std::uint16_t StaticConstantIndex = static_cast<std::uint16_t>(-1);
					if (const FNativeFunction* const StaticConstant =
					        TryResolveHostStaticConstant(
					            *MemberAccess.Object,
					            MemberAccess.Member,
					            StaticConstantIndex))
					{
						OutTypeName = ResolveNativeReturnTypeName(*StaticConstant);
						return true;
					}
				}
				// Namespace-qualified global: `Ns.Name` / `Ns.Sub.Name`.
				{
					std::string GlobalLinkageName;
					if (TryResolveNamespaceQualifiedGlobal(
					        *MemberAccess.Object,
					        MemberAccess.Member,
					        GlobalLinkageName))
					{
						const auto TypeIt =
						    GlobalVarTypes.find(GlobalLinkageName);
						if (TypeIt != GlobalVarTypes.end() && !TypeIt->second.empty())
						{
							OutTypeName = NormalizeTypeAlias(TypeIt->second);
							return true;
						}
					}
				}
				std::string OwnerTypeName;
				if (!TryResolveExprTypeName(*MemberAccess.Object, OwnerTypeName))
				{
					return false;
				}
				if (!FindFieldTypeOnOwnerType(OwnerTypeName, MemberAccess.Member, OutTypeName))
				{
					return false;
				}
				OutTypeName = NormalizeTypeAlias(OutTypeName);
				return true;
			}
		case EExprKind::IndexAccess:
			{
				auto& IndexTypes = ActiveExpressionTypeQuery->IndexTypes;
				if (!IndexTypes)
				{
					IndexTypes.emplace();
				}
				auto& Cached = *IndexTypes;
				const auto Found = Cached.find(&Expression);
				if (Found != Cached.end() && Found->second.first == CurrentModulePath)
				{
					OutTypeName = Found->second.second;
					return true;
				}
				const std::string QueryModule = CurrentModulePath;
				// Generated memberwise operations can index every native-array rank.
				// Resolve receivers first without retaining one large semantic frame
				// per index on hosts with a small default thread stack.
				std::vector<const FIndexAccessExpr*> Pending;
				for (const FExpr* Receiver = &Expression; Receiver->Kind == EExprKind::IndexAccess;)
				{
					const auto Entry = Cached.find(Receiver);
					if (Entry != Cached.end() && Entry->second.first == QueryModule)
					{
						break;
					}
					const auto* Index = static_cast<const FIndexAccessExpr*>(Receiver);
					Pending.push_back(Index);
					Receiver = Index->Object.get();
				}
				for (auto Index = Pending.rbegin(); Index != Pending.rend(); ++Index)
				{
					const FIndexAccessExpr& IndexAccess = **Index;
					std::string ContainerTypeName;
					if (TryResolveExprTypeName(*IndexAccess.Object, ContainerTypeName) && TypeSupportsOperatorOverload(ContainerTypeName))
					{
						FResolvedScriptOperator Resolved;
						bool bAmbiguous = false;
						if (!ResolveScriptOperator("operator[]", *IndexAccess.Object,
						        {IndexAccess.Index.get()}, Resolved, bAmbiguous))
						{
							return false;
						}
						OutTypeName = Resolved.ReturnType;
					}
					else if (!TryResolveIndexElementTypeName(*IndexAccess.Object, OutTypeName))
					{
						return false;
					}
					Cached.insert_or_assign(*Index, std::make_pair(QueryModule, OutTypeName));
				}
				return true;
			}
		case EExprKind::Call:
			{
				const FCallExpr& Call = static_cast<const FCallExpr&>(Expression);
				const bool bIntrinsicCast = !Call.TypeArgs.empty() &&
				    (Call.Callee == "cast" || Call.Callee == "try_cast" || Call.Callee == "const_cast");
				if (!Call.CalleeExpr && !Call.bStandardLibraryQualified && !Call.bImplementationBuiltinAccess && !bIntrinsicCast &&
				    !ResolveLocalEnumConstantLinkageName(Call.Callee).empty())
				{
					ReportError("enum constant is not callable: " + Call.Callee, Expression.Location);
					return false;
				}
				if (auto StaticCall = MakeLexicalStaticCall(Call))
				{
					return TryResolveExprRawTypeName(*StaticCall, OutTypeName);
				}
				const auto ResolveOperatorCallType = [&](const std::string& CalleeType) -> bool
				{
					if (!TypeSupportsOperatorOverload(CalleeType)) return false;
					FIdentifierExpr NamedReceiver(Call.Callee);
					const FExpr& Receiver = Call.CalleeExpr ? *Call.CalleeExpr : NamedReceiver;
					std::vector<const FExpr*> Args;
					for (const FExpr* Arg : Call.GetArguments())
					{
						Args.push_back(Arg);
					}
					FResolvedScriptOperator Resolved;
					bool bAmbiguous = false;
					if (!ResolveScriptOperator("operator()", Receiver, Args, Resolved, bAmbiguous)) return false;
					OutTypeName = Resolved.ReturnType;
					return true;
				};
				if (Call.CalleeExpr)
				{
					std::string CalleeType;
					if (!TryResolveExprTypeName(*Call.CalleeExpr, CalleeType))
					{
						return false;
					}
					if (ResolveOperatorCallType(CalleeType))
					{
						return true;
					}
					if (!Runtime::IsFunctionTypeName(CalleeType))
					{
						return false;
					}
					std::vector<std::string> ParamTypes;
					std::string ReturnType;
					if (!Runtime::ParseFunctionType(CalleeType, ParamTypes, ReturnType))
					{
						return false;
					}
					OutTypeName = ReturnType;
					return true;
				}
				if (!Call.TypeArgs.empty())
				{
					const auto* Declaration = FindUniqueFreeFunction(Call.Callee, CurrentModulePath);
					if (Declaration && IsMethodBindingFactoryDeclaration(*Declaration))
					{
						std::string ReceiverType, FunctionType;
						return ResolveMethodBindingFactoryType(Call, OutTypeName, ReceiverType, FunctionType);
					}
				}
				std::string EffectiveCallee = SubstituteTypeName(Call.Callee);
				if (Call.Callee == "variant" || Call.Callee == "variant_is" || Call.Callee == "variant_has_value" || Call.Callee == "variant_type_name")
				{
					OutTypeName = Call.Callee == "variant" ? "variant" : Call.Callee == "variant_type_name" ? "string"
					                                                                                        : "bool";
					return true;
				}
				if (!Call.TypeArgs.empty() || (Call.bExplicitTemplateArguments && IsTypeNameWithoutValueBinding(Call.Callee)))
				{
					std::string TypeApp = EffectiveCallee;
					TypeApp.push_back('<');
					for (std::size_t TypeArgIndex = 0; TypeArgIndex < Call.TypeArgs.size(); ++TypeArgIndex)
					{
						if (TypeArgIndex > 0)
						{
							TypeApp.push_back(',');
						}
						TypeApp += Call.TypeArgs[TypeArgIndex];
					}
					TypeApp.push_back('>');
					EffectiveCallee = SubstituteTypeName(TypeApp);
				}
				{
					const std::string ExpandedCallee =
					    ExpandUserTypeAlias(EffectiveCallee, CurrentModulePath);
					if (ExpandedCallee != EffectiveCallee)
					{
						EffectiveCallee = ExpandedCallee;
					}
				}
				if (Call.bStandardLibraryQualified && (Call.Callee == "__std_spawn" || Call.Callee == "__std_spawn_task") && !Call.GetArguments().empty())
				{
					std::string CallableType;
					const bool bDirectFreeFunction =
					    Call.GetArguments().front()->Kind == EExprKind::Identifier && FindFunctionDeclForReference(static_cast<const FIdentifierExpr&>(*Call.GetArguments().front()).Name, CurrentModulePath) != nullptr;
					const bool bDirectStaticMethod =
					    Call.GetArguments().front()->Kind == EExprKind::MemberAccess;
					if ((TryResolveExprTypeName(
					         *Call.GetArguments().front(), CallableType) &&
					        Runtime::IsFunctionTypeName(CallableType)) ||
					    bDirectFreeFunction || bDirectStaticMethod)
					{
						if (!FindFunctionDeclForReference(Call.Callee, CurrentModulePath))
						{
							return false;
						}
						if (Call.Callee == "__std_spawn")
						{
							OutTypeName = "int";
							return Call.TypeArgs.empty();
						}
						if (Call.TypeArgs.size() != 1)
						{
							return false;
						}
						const std::string ResultType = QualifyTypeName(
						    NormalizeTypeAlias(SubstituteTypeName(Call.TypeArgs.front())),
						    CurrentModulePath);
						OutTypeName = QualifyTypeName(
						    "task<" + ResultType + ">", CurrentModulePath);
						return true;
					}
				}
				if ((Call.Callee == "cast" || Call.Callee == "try_cast" || Call.Callee == "const_cast") && !Call.TypeArgs.empty())
				{
					if (Call.TypeArgs.size() != 1 || Call.GetArguments().size() != 1)
					{
						return false;
					}
					std::string TargetType = SubstituteTypeName(Call.TypeArgs.front());
					TargetType = QualifyTypeName(
					    NormalizeTypeAlias(TargetType, CurrentModulePath), CurrentModulePath);
					if (Call.Callee == "cast" || Call.Callee == "const_cast")
					{
						OutTypeName = std::move(TargetType);
						return true;
					}
					const std::string OptionalType = "optional<" + TargetType + ">";
					// This is a side-effect-free type query. The emission path resolves
					// and instantiates optional<T> before it writes TryCast bytecode;
					// doing that work here would mutate compiler state from a const
					// query and makes auto try_cast<T>(...) ill-formed at C++ level.
					OutTypeName = QualifyTypeName(OptionalType, CurrentModulePath);
					return true;
				}
				// These calls are compiler intrinsics. Keep type resolution in the
				// same order as bytecode emission: multi-argument print and the
				// dedicated format family take precedence over same-named values.
				if ((Call.Callee == "print" && Call.GetArguments().size() > 1) || Call.Callee == "printf" || Call.Callee == "format" || Call.Callee == "__format" || Call.Callee == "formatf" || Call.Callee == "__format_printf")
				{
					OutTypeName =
					    Call.Callee == "print" || Call.Callee == "printf"
					    ? "void"
					    : "string";
					return true;
				}
				{
					std::string CalleeType;
					if (TryResolveVariableTypeName(Call.Callee, CalleeType))
					{
						if (ResolveOperatorCallType(CalleeType))
						{
							return true;
						}
						if (Runtime::IsFunctionTypeName(CalleeType))
						{
							std::vector<std::string> ParamTypes;
							std::string ReturnType;
							if (Runtime::ParseFunctionType(CalleeType, ParamTypes, ReturnType))
							{
								OutTypeName = ReturnType;
								return true;
							}
						}
					}
				}
				const bool bBuiltinAccessible =
				    Call.bStandardLibraryQualified || Call.bImplementationBuiltinAccess || StandardLibrary::RequiredNamespaceForFunction(Call.Callee).empty();
				if (const Builtins::FDescriptor* const Builtin =
				        bBuiltinAccessible ? Builtins::Lookup(Call.Callee) : nullptr)
				{
					if (Builtin->bPushVoidResult)
					{
						OutTypeName = "void";
						return true;
					}
					switch (Builtin->Id)
					{
					case EBuiltinId::MethodBindingInvoke:
						{
							std::vector<std::string> Params;
							return Call.bImplementationBuiltinAccess && Call.TypeArgs.size() == 1 && Runtime::ParseFunctionType(Call.TypeArgs[0], Params, OutTypeName);
						}
					case EBuiltinId::ScalarParse:
					case EBuiltinId::SynchronizedLoad:
					case EBuiltinId::TypedTaskAwait:
					case EBuiltinId::SynchronizedExchange:
					case EBuiltinId::SynchronizedFetchAdd:
					case EBuiltinId::MemoryRead:
						{

							if (Call.TypeArgs.size() != 1)
							{
								return false;
							}
							OutTypeName = CanonicalTypeNameForCoercion(NormalizeTypeAlias(SubstituteTypeName(Call.TypeArgs[0])));
							return true;
						}
					case EBuiltinId::StringEncodeUtf8:
					case EBuiltinId::ReadBytes:
					case EBuiltinId::MemoryClone:
					case EBuiltinId::MemoryFromArray:
						{

							OutTypeName = "std.memory.buffer";
							return true;
						}
					case EBuiltinId::MemoryToArray:
						{

							OutTypeName = "array<unsigned char>";
							return true;
						}
					case EBuiltinId::MemoryAddress:
					case EBuiltinId::MemoryObjectAddress:
					case EBuiltinId::MemoryObjectId:
					case EBuiltinId::MemoryDataAddress:
						{

							OutTypeName = "uint64";
							return true;
						}
					case EBuiltinId::ScalarTryParse:
					case EBuiltinId::TypedTaskValid:
					case EBuiltinId::TypedTaskDone:
					case EBuiltinId::MutexTryLock:
					case EBuiltinId::MutexLockFor:
					case EBuiltinId::SemaphoreTryAcquire:
					case EBuiltinId::SemaphoreAcquireFor:
					case EBuiltinId::PathIsAbsolute:
					case EBuiltinId::StringStartsWith:
					case EBuiltinId::StringEndsWith:
					case EBuiltinId::StringIsValidUtf8:
					case EBuiltinId::MemoryAvailable:
					case EBuiltinId::MemoryEqual:
						{

							OutTypeName = "bool";
							return true;
						}
					case EBuiltinId::SourceMetadataNode:
						{
							OutTypeName = "source_node_info";
							return true;
						}
					case EBuiltinId::SourceMetadataAnnotation:
						{
							OutTypeName = "annotation_info";
							return true;
						}
					case EBuiltinId::SourceMetadataArgument:
						{
							OutTypeName = "annotation_argument_info";
							return true;
						}
					case EBuiltinId::SourceMetadataComment:
						{
							OutTypeName = "comment_info";
							return true;
						}
					case EBuiltinId::SourceMetadataDefinition:
						{
							OutTypeName = "annotation_definition_info";
							return true;
						}
					case EBuiltinId::StringRepeat:
					case EBuiltinId::PathRoot:
					case EBuiltinId::PathNormalize:
					case EBuiltinId::StringReplace:
					case EBuiltinId::StringTrim:
					case EBuiltinId::StringDecodeUtf8:
					case EBuiltinId::ToString:
					case EBuiltinId::ReadFile:
					case EBuiltinId::Substr:
					case EBuiltinId::Join:
					case EBuiltinId::GetEnv:
					case EBuiltinId::ReadLine:
					case EBuiltinId::Format:
					case EBuiltinId::FormatPrintf:
					case EBuiltinId::ReflectionString:
					case EBuiltinId::SourceMetadataString:
						{
							OutTypeName = "string";
							return true;
						}
					case EBuiltinId::TypedTaskAwaitFor:
						{
							if (Call.TypeArgs.size() != 1) return false;
							OutTypeName = "optional<" + CanonicalTypeNameForCoercion(NormalizeTypeAlias(SubstituteTypeName(Call.TypeArgs[0]))) + ">";
							return true;
						}
					case EBuiltinId::TaskGroupAwaitAll:
					case EBuiltinId::TaskArrayAwaitAll:
						{
							if (Call.TypeArgs.size() != 1) return false;
							OutTypeName = "array<" + CanonicalTypeNameForCoercion(NormalizeTypeAlias(SubstituteTypeName(Call.TypeArgs[0]))) + ">";
							return true;
						}
					case EBuiltinId::TryReadLine:
					case EBuiltinId::TryReadToken:
					case EBuiltinId::TryGetEnv:
						{
							OutTypeName = "optional<string>";
							return true;
						}
					case EBuiltinId::Split:
						{
							OutTypeName = "array<string>";
							return true;
						}
					case EBuiltinId::ToWString:
						{
							OutTypeName = "wstring";
							return true;
						}
					case EBuiltinId::ToUnsigned:
						{
							OutTypeName = "unsigned int";
							return true;
						}
					case EBuiltinId::ToInt64:
					case EBuiltinId::ReflectionInt64:
					case EBuiltinId::SourceMetadataInt64:
						{
							OutTypeName = "int64";
							return true;
						}
					case EBuiltinId::ToUInt64:
					case EBuiltinId::RandomNextUInt:
					case EBuiltinId::SourceMetadataUInt64:
						{
							OutTypeName = Builtin->Id == EBuiltinId::RandomNextUInt ? "unsigned int" : "uint64";
							return true;
						}
					case EBuiltinId::ArrayPrepareSort:
					case EBuiltinId::Len:
					case EBuiltinId::StringLastFind:
					case EBuiltinId::RandomIndex:
					case EBuiltinId::Find:
						{
							OutTypeName = "int64";
							return true;
						}
					case EBuiltinId::ConsoleWidth:
					case EBuiltinId::ConsoleHeight:
					case EBuiltinId::ToInt:
					case EBuiltinId::CharCode:
					case EBuiltinId::ReflectionInt:
					case EBuiltinId::SourceMetadataInt:
						{
							OutTypeName = "int";
							return true;
						}
					case EBuiltinId::ToDouble:
					case EBuiltinId::SourceMetadataFloat:
					case EBuiltinId::Sqrt:
					case EBuiltinId::Sin:
					case EBuiltinId::Cos:
					case EBuiltinId::Tan:
					case EBuiltinId::Floor:
					case EBuiltinId::Ceil:
					case EBuiltinId::Round:
					case EBuiltinId::Trunc:
					case EBuiltinId::Pow:
					case EBuiltinId::Exp:
					case EBuiltinId::Log:
					case EBuiltinId::Log2:
					case EBuiltinId::Asin:
					case EBuiltinId::Acos:
					case EBuiltinId::Atan2:
						{
							OutTypeName = "double";
							return true;
						}
					case EBuiltinId::ToFloat:
						{
							OutTypeName = "float";
							return true;
						}
					case EBuiltinId::Exists:
					case EBuiltinId::DeleteFile:
					case EBuiltinId::ConsoleClear:
					case EBuiltinId::ConsoleIsInteractive:
					case EBuiltinId::ConsoleSupportsColor:
					case EBuiltinId::ConsoleSupportsCursor:
					case EBuiltinId::ConsoleSupportsClear:
					case EBuiltinId::ConsoleIsOutputTerminal:
					case EBuiltinId::ConsoleSetColor:
					case EBuiltinId::ConsoleResetStyle:
					case EBuiltinId::ConsoleClearLine:
					case EBuiltinId::ConsoleMoveCursor:
					case EBuiltinId::ConsoleSetCursorVisible:
					case EBuiltinId::IsFinite:
					case EBuiltinId::IsNan:
					case EBuiltinId::ReflectionBool:
					case EBuiltinId::SourceMetadataBool:
						{
							OutTypeName = "bool";
							return true;
						}
					case EBuiltinId::IsFile:
						{
							OutTypeName = "bool";
							return true;
						}
					case EBuiltinId::IsDirectory:
						{
							OutTypeName = "bool";
							return true;
						}
					case EBuiltinId::RemoveDir:
						{
							OutTypeName = "bool";
							return true;
						}
					case EBuiltinId::CancelRequested:
						{
							OutTypeName = "bool";
							return true;
						}
					case EBuiltinId::Abs:
						{
							if (!Call.GetArguments().empty() && TryResolveExprTypeName(*Call.GetArguments().front(), OutTypeName))
							{
								return true;
							}
							return false;
						}
					case EBuiltinId::Min:
					case EBuiltinId::Max:
						{
							if (Call.GetArguments().size() < 2)
							{
								return false;
							}
							std::string LeftType;
							std::string RightType;
							if (!TryResolveExprTypeName(*Call.GetArguments()[0], LeftType) || !TryResolveExprTypeName(*Call.GetArguments()[1], RightType))
							{
								return false;
							}
							const bool bLeftUnsigned = Runtime::IsUnsignedTypeName(LeftType);
							const bool bRightUnsigned = Runtime::IsUnsignedTypeName(RightType);
							const bool bLeftSigned = IsSignedIntegerTypeName(LeftType);
							const bool bRightSigned = IsSignedIntegerTypeName(RightType);
							const bool bLeftFloat = LeftType == "float" || LeftType == "double";
							const bool bRightFloat = RightType == "float" || RightType == "double";
							if ((bLeftUnsigned && bRightSigned) || (bRightUnsigned && bLeftSigned))
							{
								return false;
							}
							if (bLeftFloat || bRightFloat)
							{
								OutTypeName = LeftType == "double" || RightType == "double"
								    ? "double"
								    : "float";
								return true;
							}
							if (bLeftUnsigned && bRightUnsigned)
							{
								OutTypeName =
								    LeftType == "uint64" || LeftType == "unsigned long" || RightType == "uint64" || RightType == "unsigned long"
								    ? "uint64"
								    : "unsigned int";
								return true;
							}
							if (bLeftSigned && bRightSigned)
							{
								OutTypeName =
								    LeftType == "int64" || LeftType == "long" || RightType == "int64" || RightType == "long"
								    ? "int64"
								    : "int";
								return true;
							}
							return false;
						}
					case EBuiltinId::ListDir:
						{
							OutTypeName = "array<string>";
							return true;
						}
					case EBuiltinId::Range:
						{
							OutTypeName = "array<int>";
							return true;
						}
					case EBuiltinId::Cwd:
						{
							OutTypeName = "string";
							return true;
						}
					case EBuiltinId::Now:
					case EBuiltinId::SteadyNow:
						{
							OutTypeName = "double";
							return true;
						}
					case EBuiltinId::ToLower:
					case EBuiltinId::ToUpper:
						{
							OutTypeName = "string";
							return true;
						}
					case EBuiltinId::MakeMutex:
					case EBuiltinId::MakeSemaphore:
						{
							OutTypeName = "channel<bool>";
							return true;
						}
					case EBuiltinId::MakeSynchronized:
						{
							if (Call.TypeArgs.size() != 1) return false;
							OutTypeName = "channel<" + CanonicalTypeNameForCoercion(NormalizeTypeAlias(SubstituteTypeName(Call.TypeArgs[0]))) + ">";
							return true;
						}
					case EBuiltinId::ChannelRecv:
						{
							if (!Call.GetArguments().empty() && TryResolveExprTypeName(*Call.GetArguments()[0], OutTypeName) && Runtime::IsChannelTypeName(OutTypeName))
							{
								std::string ElementType;
								if (Runtime::ParseChannelElementType(OutTypeName, ElementType))
								{
									OutTypeName = ElementType;
									return true;
								}
							}
							return false;
						}
					case EBuiltinId::ChannelTryRecv:
					case EBuiltinId::ChannelRecvFor:
						{
							if (!Call.GetArguments().empty() && TryResolveExprTypeName(*Call.GetArguments()[0], OutTypeName) && Runtime::IsChannelTypeName(OutTypeName))
							{
								std::string ElementType;
								if (Runtime::ParseChannelElementType(OutTypeName, ElementType))
								{
									OutTypeName = MakeDynamicArrayTypeName(ElementType);
									return true;
								}
							}
							return false;
						}
					case EBuiltinId::ChannelTrySend:
					case EBuiltinId::ChannelSendFor:
					case EBuiltinId::ChannelIsClosed:
					case EBuiltinId::JoinTaskFor:
					case EBuiltinId::TaskDone:
						{
							OutTypeName = "bool";
							return true;
						}
					case EBuiltinId::ChannelSize:
					case EBuiltinId::ChannelCapacity:
						{
							OutTypeName = "int64";
							return true;
						}
					case EBuiltinId::Spawn:
					case EBuiltinId::SpawnResult:
						{
							OutTypeName = "int";
							return true;
						}
					case EBuiltinId::WeakCreate:
						{
							OutTypeName = "int64";
							return true;
						}
					case EBuiltinId::WeakLock:
						{
							if (!CurrentFunctionReturnType.empty() && CurrentFunctionReturnType != "void")
							{
								OutTypeName = CurrentFunctionReturnType;
								return true;
							}
							return false;
						}
					case EBuiltinId::WeakExpired:
						{
							OutTypeName = "bool";
							return true;
						}
					case EBuiltinId::ReadToken:
					case EBuiltinId::ReadString:
						{
							OutTypeName = "string";
							return true;
						}
					case EBuiltinId::ReadInt:
						{
							OutTypeName = "int";
							return true;
						}
					case EBuiltinId::ReadDouble:
						{
							OutTypeName = "double";
							return true;
						}
					case EBuiltinId::ReadBool:
						{
							OutTypeName = "bool";
							return true;
						}
					default:
						return false;
					}
				}
				if (Runtime::IsChannelTypeName(EffectiveCallee))
				{
					OutTypeName = QualifyTypeName(EffectiveCallee, CurrentModulePath);
					return true;
				}
				if (Call.TypeArgs.empty() && CanonicalTypeNameForCoercion(EffectiveCallee) == "variant")
				{
					OutTypeName = "variant";
					return true;
				}
				if (Runtime::IsDynamicArrayTypeName(EffectiveCallee))
				{
					OutTypeName = QualifyTypeName(EffectiveCallee, CurrentModulePath);
					return true;
				}
				if (Runtime::IsCommonContainerTypeName(EffectiveCallee))
				{
					OutTypeName = QualifyTypeName(NormalizeTypeAlias(EffectiveCallee), CurrentModulePath);
					return true;
				}
				const std::string HostConstructorType =
				    CanonicalTypeNameForCoercion(EffectiveCallee);
				if (ActiveBindings && ActiveBindings->FindType(HostConstructorType))
				{
					std::vector<std::string> ArgTypeNames;
					ArgTypeNames.reserve(Call.GetArguments().size());
					for (const FExpr* Argument : Call.GetArguments())
					{
						std::string ArgType;
						if (!Argument || !TryResolveExprTypeName(UnwrapRefOperand(*Argument), ArgType))
						{
							ArgType.clear();
						}
						else if (Argument->Kind == EExprKind::Ref && !ArgType.empty())
						{
							ArgType = EncodeRefCallArgType(
							    UnwrapRefOperand(*Argument), std::move(ArgType));
						}
						ArgTypeNames.push_back(std::move(ArgType));
					}
					bool bAmbiguousConstructor = false;
					if (ResolveHostConstructor(
					        HostConstructorType,
					        ArgTypeNames,
					        bAmbiguousConstructor))
					{
						OutTypeName = HostConstructorType;
						return true;
					}
					if (bAmbiguousConstructor)
					{
						return false;
					}
				}
				std::uint16_t NativeIndex = static_cast<std::uint16_t>(-1);
				if (const FNativeFunction* const Native = FindNativeForCall(Call.Callee, Call.GetArguments(), NativeIndex, Expression.Location, false))
				{
					if (Native->bGenericHostObjectCast)
					{
						if (Call.TypeArgs.size() != Native->TypeParams.size() || Call.TypeArgs.empty() || Native->ReturnType != ENativeType::HostObject || Native->ParamTypes.size() != 1 || Native->ParamTypes.front() != ENativeType::HostObject || Call.TypeArgs.size() >= std::numeric_limits<std::uint8_t>::max())
						{
							return false;
						}
						const auto ReturnParameter = std::find(Native->TypeParams.begin(),
						    Native->TypeParams.end(), Native->ScriptReturnTypeName);
						if (ReturnParameter == Native->TypeParams.end())
						{
							return false;
						}
						const auto ReturnIndex = static_cast<std::size_t>(ReturnParameter - Native->TypeParams.begin());
						const std::string TargetType = CanonicalTypeNameForCoercion(
						    QualifyTypeName(Call.TypeArgs[ReturnIndex], CurrentModulePath));
						const FHostTypeDesc* const HostType =
						    ActiveBindings ? ActiveBindings->FindType(TargetType) : nullptr;
						const bool bScriptClass =
						    IsClassOrStructType(TargetType) && !IsStructType(TargetType);
						const bool bHostClass =
						    HostType && HostType->Kind == EHostTypeKind::Class;
						if (!bScriptClass && !bHostClass)
						{
							return false;
						}
						OutTypeName = TargetType;
						return true;
					}
					OutTypeName = ResolveNativeReturnTypeName(*Native);
					return true;
				}
				{
					const FFunctionDecl* FuncDecl = nullptr;
					// Prefer concrete free overloads when TypeArgs are absent (matches Call).
					if (Call.TypeArgs.empty() && !Call.bExplicitTemplateArguments)
					{
						std::vector<std::string> ArgTypeNames;
						ArgTypeNames.reserve(Call.GetArguments().size());
						for (const FExpr* ArgExpr : Call.GetArguments())
						{
							if (!ArgExpr)
							{
								ArgTypeNames.emplace_back();
								continue;
							}
							const bool bArgIsRef = ArgExpr->Kind == EExprKind::Ref;
							const FExpr& Arg = UnwrapRefOperand(*ArgExpr);
							std::string ArgType;
							if (!TryResolveExprTypeName(Arg, ArgType))
							{
								ArgType.clear();
							}
							else if (bArgIsRef && !ArgType.empty())
							{
								ArgType = EncodeRefCallArgType(Arg, std::move(ArgType));
							}
							ArgTypeNames.push_back(std::move(ArgType));
						}
						if (const FFunctionDecl* const Resolved =
						        SelectFreeFunctionOverload(Call.Callee, ArgTypeNames))
						{
							OutTypeName = Resolved->ReturnType.empty()
							    ? "void"
							    : QualifyTypeName(
							          NormalizeTypeAlias(
							              Resolved->ReturnType,
							              Resolved->ModulePath),
							          Resolved->ModulePath);
							return true;
						}
					}
					if (Call.bExplicitTemplateArguments || !Call.TypeArgs.empty())
					{
						std::vector<std::string> GenericArgTypeNames;
						GenericArgTypeNames.reserve(Call.GetArguments().size());
						for (const FExpr* ArgExpr : Call.GetArguments())
						{
							if (!ArgExpr)
							{
								GenericArgTypeNames.emplace_back();
								continue;
							}
							const bool bArgIsRef = ArgExpr->Kind == EExprKind::Ref;
							const FExpr& Arg = UnwrapRefOperand(*ArgExpr);
							std::string ArgType;
							if (!TryResolveExprTypeName(Arg, ArgType))
							{
								ArgType.clear();
							}
							else if (bArgIsRef && !ArgType.empty())
							{
								ArgType = EncodeRefCallArgType(Arg, std::move(ArgType));
							}
							GenericArgTypeNames.push_back(std::move(ArgType));
						}
						FuncDecl = FindGenericFreeFunctionDecl(Call.Callee, CurrentModulePath, Call.TypeArgs,
						    GenericArgTypeNames);
					}
					else if (ActiveProgram)
					{
						std::vector<std::string> GenericArgTypes;
						GenericArgTypes.reserve(Call.GetArguments().size());
						for (const FExpr* Argument : Call.GetArguments())
						{
							std::string Type;
							if (!Argument || !TryResolveExprTypeName(UnwrapRefOperand(*Argument), Type))
							{
								return false;
							}
							if (Argument->Kind == EExprKind::Ref)
							{
								Type = EncodeRefCallArgType(UnwrapRefOperand(*Argument), std::move(Type));
							}
							GenericArgTypes.push_back(std::move(Type));
						}
						std::vector<std::string> ConcreteTypes;
						FuncDecl =
						    SelectGenericFreeFunctionOverload(Call.Callee, GenericArgTypes, ConcreteTypes);
					}
					if (FuncDecl && !TemplateParameterNames(*FuncDecl).empty())
					{
						if (!ParamsAcceptArgCount(FuncDecl->Parameters, Call.GetArguments().size()))
						{
							return false;
						}
						std::vector<std::string> ArgTypeNames;
						ArgTypeNames.reserve(Call.GetArguments().size());
						for (const FExpr* ArgExpr : Call.GetArguments())
						{
							if (!ArgExpr)
							{
								return false;
							}
							const bool bArgIsRef = ArgExpr->Kind == EExprKind::Ref;
							const FExpr& Argument = UnwrapRefOperand(*ArgExpr);
							std::string ArgType;
							if (!TryResolveExprTypeName(Argument, ArgType))
							{
								return false;
							}
							if (bArgIsRef)
							{
								ArgType = EncodeRefCallArgType(Argument, std::move(ArgType));
							}
							ArgTypeNames.push_back(std::move(ArgType));
						}
						std::vector<std::string> ConcreteTypes;
						if (!TryInferGenericFunctionTypeParams(
						        *FuncDecl,
						        ArgTypeNames,
						        Call.TypeArgs,
						        ConcreteTypes))
						{
							return false;
						}
						std::unordered_map<std::string, std::string> FunctionSubst;
						FunctionSubst.reserve(TemplateParameterNames(*FuncDecl).size());
						for (std::size_t TypeIndex = 0;
						    TypeIndex < TemplateParameterNames(*FuncDecl).size();
						    ++TypeIndex)
						{
							FunctionSubst.emplace(
							    TemplateParameterNames(*FuncDecl)[TypeIndex],
							    ConcreteTypes[TypeIndex]);
						}
						OutTypeName = FuncDecl->ReturnType.empty()
						    ? "void"
						    : QualifyTypeName(
						          NormalizeTypeAlias(
						              SubstituteTypeNameWith(
						                  FuncDecl->ReturnType,
						                  FunctionSubst),
						              FuncDecl->ModulePath),
						          FuncDecl->ModulePath);
						return true;
					}
				}
				// An explicit generic application denotes its concrete instance, even
				// when linkage lookup can only find the generic template so far.
				if (TryInferBuiltinHandleConstructorType(Call, EffectiveCallee, OutTypeName)) return true;
				std::string GenericBase;
				std::vector<std::string> GenericArgs;
				if (SplitUserGenericTypeApp(EffectiveCallee, GenericBase, GenericArgs))
				{
					if (!ResolveTypeLinkageName(GenericBase, CurrentModulePath, GenericArgs.size()).empty())
					{
						OutTypeName = QualifyTypeName(EffectiveCallee, CurrentModulePath);
						return !OutTypeName.empty();
					}
				}
				// Bare spellings still need lexical lookup: a local private type
				// can hide a same-named imported type already in the module table.
				if (Module && (EffectiveCallee != Call.Callee || ActiveTypeSubst.count(Call.Callee) != 0 || EffectiveCallee.find('\x1F') != std::string::npos))
				{
					const auto Type = std::find_if(Module->Types.begin(), Module->Types.end(),
					    [&](const auto& Candidate)
					{
						return Candidate.Name == EffectiveCallee;
					});
					if (Type != Module->Types.end())
					{
						OutTypeName = Type->Name;
						return true;
					}
				}
				if (!GenericTemplateBaseLinkage(EffectiveCallee).empty() && !IsInterfaceTypeName(EffectiveCallee))
				{
					// Substituting T in T() can yield a closed internal identity,
					// without a source-level <...> application to resolve again.
					std::unordered_map<std::string, std::string> ConstructorBindings;
					if (TryBuildGenericOwnerSubst(EffectiveCallee, ConstructorBindings, Call.Location))
					{
						OutTypeName = EffectiveCallee;
						return true;
					}
				}
				const std::string TypeLinkageName = ResolveTypeLinkageName(EffectiveCallee, CurrentModulePath);
				if (!TypeLinkageName.empty())
				{
					if (IsInterfaceTypeName(TypeLinkageName))
					{
						return false;
					}
					if (Call.TypeArgs.empty() && !ExpressionExpectedType.empty() && MatchesExpectedGenericCtor(Call.Callee, ExpressionExpectedType))
					{
						OutTypeName = ExpressionExpectedType;
						return true;
					}
					// Arity-0 decl may share the name with generic overloads.
					if (FindGenericStructDecl(EffectiveCallee, CurrentModulePath, 0) || FindGenericClassDecl(EffectiveCallee, CurrentModulePath, 0) || FindGenericStructDecl(Call.Callee, CurrentModulePath, 0) || FindGenericClassDecl(Call.Callee, CurrentModulePath, 0))
					{
						OutTypeName = TypeLinkageName;
						return true;
					}
					// Name resolves only via generic overload fallback — not a bare ctor type.
					return false;
				}
				if (Call.GetArguments().empty() && Call.TypeArgs.empty() && !ExpressionExpectedType.empty() && MatchesExpectedGenericCtor(Call.Callee, ExpressionExpectedType))
				{
					OutTypeName = ExpressionExpectedType;
					return true;
				}
				// Only after every ordinary call target has failed, reinterpret a
				// bare call in an instance body as `this.Method(...)`. Reuse the
				// explicit method-call type path so overloads, inheritance, const,
				// and generic-owner substitution stay identical.
				{
					std::string ImplicitOwner;
					const FMethodDecl* ImplicitMethod = nullptr;
					if (ResolveImplicitThisMethodForCall(
					        Call,
					        ImplicitOwner,
					        ImplicitMethod) &&
					    ImplicitMethod)
					{
						auto ThisExpr = std::make_unique<FThisExpr>();
						ThisExpr->Location = Expression.Location;
						FMethodCallExpr ImplicitCall(
						    std::move(ThisExpr),
						    Call.Callee,
						    {});
						ImplicitCall.Location = Expression.Location;
						ImplicitCall.TypeArgs = Call.TypeArgs;
						ImplicitCall.bExplicitTemplateArguments = Call.bExplicitTemplateArguments;
						ImplicitCall.BorrowArgumentsFrom(Call.GetArguments());
						return TryResolveExprRawTypeName(ImplicitCall, OutTypeName);
					}
				}
				// Implicit host base method (see the bare-call compile fallback).
				if (Call.TypeArgs.empty() && !Call.CalleeExpr)
				{
					if (const std::string HostBase = CurrentMethodHostBaseTypeName();
					    !HostBase.empty())
					{
						std::uint16_t HostNativeIndex = static_cast<std::uint16_t>(-1);
						if (const FNativeFunction* const HostNative = FindNativeForCall(
						        MakeHostMethodNativeName(HostBase, Call.Callee),
						        Call.GetArguments(), HostNativeIndex, Expression.Location, false, 0,
						        CurrentFunction && CurrentFunction->bIsConstMethod))
						{
							OutTypeName = ResolveNativeReturnTypeName(*HostNative);
							return true;
						}
					}
				}
				return false;
			}
		case EExprKind::MethodCall:
			{
				const FMethodCallExpr& MethodCall = static_cast<const FMethodCallExpr&>(Expression);
				const auto& MethodArguments = MethodCall.GetArguments();
				if (MethodCall.Object && IsSuperReceiver(*MethodCall.Object))
				{
					if (!MethodCall.TypeArgs.empty())
					{
						return false;
					}
					std::vector<std::string> SuperArgTypeNames;
					SuperArgTypeNames.reserve(MethodArguments.size());
					for (const FExpr* ArgExpr : MethodArguments)
					{
						std::string ArgType;
						if (!ArgExpr || !TryResolveExprTypeName(UnwrapRefOperand(*ArgExpr), ArgType))
						{
							ArgType.clear();
						}
						else if (ArgExpr->Kind == EExprKind::Ref && !ArgType.empty())
						{
							ArgType = EncodeRefCallArgType(UnwrapRefOperand(*ArgExpr), std::move(ArgType));
						}
						SuperArgTypeNames.push_back(std::move(ArgType));
					}
					std::string BaseOwnerLinkage;
					const FMethodDecl* BaseMethod = nullptr;
					if (TryResolveSuperScriptMethod(
					        MethodCall.Method,
					        SuperArgTypeNames,
					        Expression.Location,
					        BaseOwnerLinkage,
					        BaseMethod) &&
					    BaseMethod)
					{
						if (BaseMethod->ReturnType.empty())
						{
							OutTypeName = "void";
							return true;
						}
						const std::string BaseModulePath = [&]() -> std::string
						{
							const std::string Resolved = ResolveOwnerModulePath(BaseOwnerLinkage);
							return Resolved.empty() ? CurrentModulePath : Resolved;
						}();
						std::unordered_map<std::string, std::string> OwnerSubst;
						if (!TryBuildGenericOwnerSubst(BaseOwnerLinkage, OwnerSubst, Expression.Location))
						{
							return false;
						}
						OutTypeName = QualifyMethodTypeName(
						    *BaseMethod, BaseMethod->ReturnType, BaseModulePath, OwnerSubst);
						return true;
					}
					if (const std::string HostBase = CurrentMethodHostBaseTypeName();
					    !HostBase.empty())
					{
						std::uint16_t HostNativeIndex = static_cast<std::uint16_t>(-1);
						if (const FNativeFunction* const HostNative = FindNativeForCall(
						        MakeHostMethodNativeName(HostBase, MethodCall.Method),
						        MethodArguments, HostNativeIndex, Expression.Location, false, 0,
						        CurrentFunction && CurrentFunction->bIsConstMethod))
						{
							OutTypeName = ResolveNativeReturnTypeName(*HostNative);
							return true;
						}
					}
					return false;
				}
				// Namespace-qualified free calls use the same semantic resolver as an
				// ordinary named call. This also supports arbitrarily nested namespaces.
				std::string QualifiedFreeName;
				std::uint16_t QualifiedNativeIndex = 0xFFFF;
				if (TryBuildNamespaceQualifiedName(
				        *MethodCall.Object,
				        MethodCall.Method,
				        QualifiedFreeName) &&
				    (FindFunctionDeclForReference(
				         QualifiedFreeName,
				         CurrentModulePath) ||
				        FindNative(QualifiedFreeName, QualifiedNativeIndex)))
				{
					FCallExpr QualifiedCall(QualifiedFreeName, {});
					QualifiedCall.Location = Expression.Location;
					QualifiedCall.TypeArgs = MethodCall.TypeArgs;
					QualifiedCall.bExplicitTemplateArguments = MethodCall.bExplicitTemplateArguments;
					QualifiedCall.BorrowArgumentsFrom(MethodArguments);
					return TryResolveExprRawTypeName(QualifiedCall, OutTypeName);
				}
				std::string OwnerTypeName;
				// `Type.staticMethod(...)`: bare type-name receiver → static method result.
				std::string StaticObjName;
				if (TryResolveTypeNameReceiver(*MethodCall.Object, StaticObjName) && !IsEnumTypeName(StaticObjName))
				{
					{
						const std::string HostNativeName =
						    MakeHostMethodNativeName(
						        StaticObjName.substr(FindLinkageIdentityStart(StaticObjName)), MethodCall.Method);
						std::uint16_t HostNativeIndex = static_cast<std::uint16_t>(-1);
						if (const FNativeFunction* const HostNative =
						        FindNativeForCall(HostNativeName, MethodArguments, HostNativeIndex, Expression.Location, false, 1))
						{
							const bool bHostStatic =
							    HostNative->bHostStaticMethod || HostNative->ParamTypes.empty() || HostNative->ParamTypes[0] != ENativeType::HostObject;
							if (bHostStatic)
							{
								if (HostNative->ReturnType == ENativeType::Void)
								{
									OutTypeName = "void";
									return true;
								}
								OutTypeName = ResolveNativeReturnTypeName(*HostNative);
								return true;
							}
						}
					}
					OwnerTypeName = StaticObjName;
				}
				if (OwnerTypeName.empty() && !TryResolveExprTypeName(*MethodCall.Object, OwnerTypeName))
				{
					return false;
				}
				if (MethodCall.TypeArgs.size() == 1)
				{
					const std::string DescriptorBase = GenericTemplateBaseLinkage(
					    QualifyTypeName(OwnerTypeName, CurrentModulePath));
					const bool bFieldOperation = DescriptorBase == "field_info" && (MethodCall.Method == "get" || MethodCall.Method == "set");
					const bool bMethodOperation = DescriptorBase == "method_info" && (MethodCall.Method == "invoke" || MethodCall.Method == "invoke_static");
					if (bFieldOperation || bMethodOperation)
					{
						OutTypeName = MethodCall.Method == "set" ? "void"
						                                         : QualifyTypeName(
						                                               NormalizeTypeAlias(SubstituteTypeName(MethodCall.TypeArgs[0])),
						                                               CurrentModulePath);
						return true;
					}
				}
				std::vector<std::string> ArgTypeNames;
				ArgTypeNames.reserve(MethodArguments.size());
				for (const FExpr* Argument : MethodArguments)
				{
					std::string ArgType;
					if (!Argument || !TryResolveExprTypeName(UnwrapRefOperand(*Argument), ArgType))
					{
						ArgType.clear();
					}
					else if (Argument->Kind == EExprKind::Ref && !ArgType.empty())
					{
						ArgType = EncodeRefCallArgType(UnwrapRefOperand(*Argument), std::move(ArgType));
					}
					ArgTypeNames.push_back(std::move(ArgType));
				}
				std::string DeclOwner;
				const FMethodDecl* MethodDecl = nullptr;
				// Match emission's overload selection and declaration context, including
				// inherited generic methods and const-qualified reference results.
				if (ResolveMethodForCall(
				        OwnerTypeName, MethodCall.Method, ArgTypeNames, DeclOwner, MethodDecl,
				        Expression.Location, true, false, nullptr,
				        IsConstAssignTarget(*MethodCall.Object),
				        (MethodCall.bExplicitTemplateArguments || !MethodCall.TypeArgs.empty()) ? &MethodCall.TypeArgs : nullptr) &&
				    MethodDecl)
				{
					if (MethodDecl->ReturnType.empty())
					{
						OutTypeName = "void";
						return true;
					}
					const std::string& SignatureOwner = DeclOwner.empty() ? OwnerTypeName : DeclOwner;
					std::string OwnerModulePath = ResolveOwnerModulePath(SignatureOwner);
					if (OwnerModulePath.empty())
					{
						OwnerModulePath = CurrentModulePath;
					}
					std::unordered_map<std::string, std::string> OwnerSubst;
					if (!TryBuildGenericOwnerSubst(SignatureOwner, OwnerSubst, Expression.Location))
					{
						return false;
					}
					OutTypeName = QualifyMethodTypeName(
					    *MethodDecl, MethodDecl->ReturnType, OwnerModulePath, OwnerSubst);
					return true;
				}
				{
					const std::string HostDispatchType =
					    ResolveHostDispatchTypeName(OwnerTypeName);
					const std::string HostNativeName = HostDispatchType.empty()
					    ? MakeHostMethodNativeName(
					          OwnerTypeName.substr(FindLinkageIdentityStart(OwnerTypeName)), MethodCall.Method)
					    : MakeHostMethodNativeName(HostDispatchType, MethodCall.Method);
					std::uint16_t HostNativeIndex = static_cast<std::uint16_t>(-1);
					if (const FNativeFunction* const HostNative =
					        FindNativeForCall(HostNativeName, MethodArguments, HostNativeIndex, Expression.Location,
					            false, 0, IsConstAssignTarget(*MethodCall.Object)))
					{
						if (HostNative->ReturnType == ENativeType::Void)
						{
							OutTypeName = "void";
							return true;
						}
						OutTypeName = ResolveNativeReturnTypeName(*HostNative);
						return true;
					}
				}
				if (Runtime::IsDynamicArrayTypeName(OwnerTypeName) && (MethodCall.Method == "push" || MethodCall.Method == "clear" || MethodCall.Method == "reserve" || MethodCall.Method == "remove_at"))
				{
					OutTypeName = OwnerTypeName;
					return true;
				}
				if (Runtime::IsDynamicArrayTypeName(OwnerTypeName) && MethodCall.Method == "capacity")
				{
					OutTypeName = "int64";
					return true;
				}
				if (Runtime::IsDynamicArrayTypeName(OwnerTypeName) && MethodCall.Method == "remove")
				{
					OutTypeName = "bool";
					return true;
				}
				if (Runtime::IsMapTypeName(OwnerTypeName))
				{
					if (Runtime::IsMultiMapTypeName(OwnerTypeName) && MethodCall.Method == "add")
					{
						OutTypeName = OwnerTypeName;
						return true;
					}
					if (MethodCall.Method == "has" || MethodCall.Method == "remove")
					{
						OutTypeName = "bool";
						return true;
					}
					if (MethodCall.Method == "clear" || MethodCall.Method == "reserve")
					{
						OutTypeName = OwnerTypeName;
						return true;
					}
					if (MethodCall.Method == "keys")
					{
						std::string KeyType;
						std::string ValueType;
						if (!ParseMapTypeForCompile(OwnerTypeName, KeyType, ValueType))
						{
							return false;
						}
						OutTypeName = "array<" + KeyType + ">";
						return true;
					}
					if (MethodCall.Method == "values")
					{
						std::string KeyType;
						std::string ValueType;
						if (!ParseMapTypeForCompile(OwnerTypeName, KeyType, ValueType))
						{
							return false;
						}
						OutTypeName = "array<" + ValueType + ">";
						return true;
					}
					if (MethodCall.Method == "entries")
					{
						std::string KeyType;
						std::string ValueType;
						if (!ParseMapTypeForCompile(OwnerTypeName, KeyType, ValueType))
						{
							return false;
						}
						OutTypeName = "array<" + Runtime::MakePairTypeName(KeyType, ValueType) + ">";
						return true;
					}
				}
				if (Runtime::IsSetTypeName(OwnerTypeName))
				{
					if (MethodCall.Method == "has" || MethodCall.Method == "remove")
					{
						OutTypeName = "bool";
						return true;
					}
					if (MethodCall.Method == "add" || MethodCall.Method == "clear" || MethodCall.Method == "reserve")
					{
						OutTypeName = OwnerTypeName;
						return true;
					}
					if (MethodCall.Method == "values")
					{
						std::string ElementType;
						if (!ParseSetElementTypeForCompile(OwnerTypeName, ElementType))
						{
							return false;
						}
						OutTypeName = "array<" + ElementType + ">";
						return true;
					}
				}
				if (Runtime::IsMultiSetTypeName(OwnerTypeName) || Runtime::IsOrderedMultiSetTypeName(OwnerTypeName))
				{
					if (MethodCall.Method == "has" || MethodCall.Method == "remove")
					{
						OutTypeName = "bool";
						return true;
					}
					if (MethodCall.Method == "count")
					{
						OutTypeName = "int64";
						return true;
					}
					if (MethodCall.Method == "add" || MethodCall.Method == "clear" || MethodCall.Method == "reserve")
					{
						OutTypeName = OwnerTypeName;
						return true;
					}
					if (MethodCall.Method == "values")
					{
						std::string ElementType;
						if (!Runtime::ParseMultiSetElementType(
						        OwnerTypeName,
						        ElementType))
						{
							return false;
						}
						OutTypeName = "array<" + ElementType + ">";
						return true;
					}
				}
				if (Runtime::IsQueueTypeName(OwnerTypeName))
				{
					if (MethodCall.Method == "push" || MethodCall.Method == "clear")
					{
						OutTypeName = OwnerTypeName;
						return true;
					}
					if (MethodCall.Method == "pop" || (MethodCall.Method == "front" && !Runtime::IsPriorityQueueTypeName(OwnerTypeName)) || (MethodCall.Method == "top" && Runtime::IsPriorityQueueTypeName(OwnerTypeName)))
					{
						return Runtime::ParseQueueElementType(OwnerTypeName, OutTypeName);
					}
					if (MethodCall.Method == "values")
					{
						std::string ElementType;
						if (!Runtime::ParseQueueElementType(OwnerTypeName, ElementType))
						{
							return false;
						}
						OutTypeName = "array<" + ElementType + ">";
						return true;
					}
				}
				if (Runtime::IsStackTypeName(OwnerTypeName))
				{
					if (MethodCall.Method == "push" || MethodCall.Method == "clear")
					{
						OutTypeName = OwnerTypeName;
						return true;
					}
					if (MethodCall.Method == "pop" || MethodCall.Method == "top")
					{
						return Runtime::ParseStackElementType(OwnerTypeName, OutTypeName);
					}
					if (MethodCall.Method == "values")
					{
						std::string ElementType;
						if (!Runtime::ParseStackElementType(OwnerTypeName, ElementType))
						{
							return false;
						}
						OutTypeName = "array<" + ElementType + ">";
						return true;
					}
				}
				if (Runtime::IsOrderedMapTypeName(OwnerTypeName))
				{
					if (Runtime::IsOrderedMultiMapTypeName(OwnerTypeName) && MethodCall.Method == "add")
					{
						OutTypeName = OwnerTypeName;
						return true;
					}
					if (MethodCall.Method == "has" || MethodCall.Method == "remove")
					{
						OutTypeName = "bool";
						return true;
					}
					if (MethodCall.Method == "clear" || MethodCall.Method == "reserve")
					{
						OutTypeName = OwnerTypeName;
						return true;
					}
					if (MethodCall.Method == "keys")
					{
						std::string KeyType;
						std::string ValueType;
						if (!ParseOrderedMapTypeForCompile(OwnerTypeName, KeyType, ValueType))
						{
							return false;
						}
						OutTypeName = "array<" + KeyType + ">";
						return true;
					}
					if (MethodCall.Method == "values")
					{
						std::string KeyType;
						std::string ValueType;
						if (!ParseOrderedMapTypeForCompile(OwnerTypeName, KeyType, ValueType))
						{
							return false;
						}
						OutTypeName = "array<" + ValueType + ">";
						return true;
					}
					if (MethodCall.Method == "entries")
					{
						std::string KeyType;
						std::string ValueType;
						if (!ParseOrderedMapTypeForCompile(OwnerTypeName, KeyType, ValueType))
						{
							return false;
						}
						OutTypeName = "array<" + Runtime::MakePairTypeName(KeyType, ValueType) + ">";
						return true;
					}
				}
				if (Runtime::IsDequeTypeName(OwnerTypeName))
				{
					if (MethodCall.Method == "push_back" || MethodCall.Method == "push_front" || MethodCall.Method == "clear")
					{
						OutTypeName = OwnerTypeName;
						return true;
					}
					if (MethodCall.Method == "pop_back" || MethodCall.Method == "pop_front" || MethodCall.Method == "front" || MethodCall.Method == "back")
					{
						return Runtime::ParseDequeElementType(OwnerTypeName, OutTypeName);
					}
					if (MethodCall.Method == "values")
					{
						std::string ElementType;
						if (!Runtime::ParseDequeElementType(OwnerTypeName, ElementType))
						{
							return false;
						}
						OutTypeName = "array<" + ElementType + ">";
						return true;
					}
				}
				std::vector<std::string> ExtensionArgTypes;
				ExtensionArgTypes.reserve(MethodArguments.size() + 1);
				ExtensionArgTypes.push_back(OwnerTypeName);
				for (const FExpr* Argument : MethodArguments)
				{
					std::string ArgumentType;
					if (!Argument || !TryResolveExprTypeName(UnwrapRefOperand(*Argument), ArgumentType))
					{
						ArgumentType.clear();
					}
					ExtensionArgTypes.push_back(std::move(ArgumentType));
				}
				std::vector<std::string> RefExtensionArgTypes = ExtensionArgTypes;
				RefExtensionArgTypes[0] = EncodeRefCallArgType(
				    *MethodCall.Object,
				    OwnerTypeName);
				const bool bReceiverCanBindRef = CanBindImplicitRefArgument(
				    *MethodCall.Object,
				    !IsConstAssignTarget(*MethodCall.Object));
				const bool bBindReceiverByRef = bReceiverCanBindRef && HasExtensionMethodCandidate(MethodCall.Method, MethodCall.TypeArgs, RefExtensionArgTypes);
				if (bBindReceiverByRef || HasExtensionMethodCandidate(MethodCall.Method, MethodCall.TypeArgs, ExtensionArgTypes))
				{
					FCallExpr ExtensionCall(MethodCall.Method, {});
					ExtensionCall.Location = Expression.Location;
					ExtensionCall.TypeArgs = MethodCall.TypeArgs;
					ExtensionCall.bExplicitTemplateArguments = MethodCall.bExplicitTemplateArguments;
					ExtensionCall.BorrowArgumentsFrom(MethodArguments);
					ExtensionCall.BorrowLeadingArgument(*MethodCall.Object);
					ExtensionCall.bImplicitReceiverArgument = true;
					ExtensionCall.bImplicitReceiverRefBinding = bBindReceiverByRef;
					return TryResolveExprRawTypeName(ExtensionCall, OutTypeName);
				}
				return false;
			}
		case EExprKind::Unary:
			{
				const FUnaryExpr& Unary = static_cast<const FUnaryExpr&>(Expression);
				if (TryResolveOverloadedUnaryType(Unary, OutTypeName))
				{
					return true;
				}
				if (Unary.Op == EUnaryOp::Not)
				{
					OutTypeName = "bool";
					return true;
				}
				std::string OperandType;
				if (!TryResolveExprTypeName(*Unary.Operand, OperandType))
				{
					return false;
				}
				if (Unary.Op == EUnaryOp::BitNot)
				{
					if (Runtime::IsUnsignedTypeName(OperandType))
					{
						OutTypeName = OperandType == "uint64" || OperandType == "unsigned long"
						    ? "uint64"
						    : "unsigned int";
						return true;
					}
					if (IsSignedIntegerTypeName(OperandType))
					{
						OutTypeName = OperandType == "int64" || OperandType == "long"
						    ? "int64"
						    : "int";
						return true;
					}
					return false;
				}
				if (Runtime::IsUnsignedTypeName(OperandType))
				{
					OutTypeName = OperandType == "uint64" || OperandType == "unsigned long"
					    ? "uint64"
					    : "unsigned int";
					return true;
				}
				if (IsSignedIntegerTypeName(OperandType))
				{
					OutTypeName = OperandType == "int64" || OperandType == "long"
					    ? "int64"
					    : "int";
					return true;
				}
				if (OperandType == "float" || OperandType == "double")
				{
					// Unary +/- preserves the floating operand width.  The VM keeps a
					// binary32 operand as float, so promoting only the inferred type to
					// double would make overload resolution disagree with execution.
					OutTypeName = OperandType;
					return true;
				}
				return false;
			}
		case EExprKind::Binary:
			{
				const FBinaryExpr& Binary = static_cast<const FBinaryExpr&>(Expression);
				// `a + b + c + ...` is represented as a left-deep binary tree.  This
				// resolver is intentionally comprehensive (it carries the overload
				// candidate machinery in its stack frame), so recursively resolving a
				// modestly long primitive chain can exhaust the native compiler stack in
				// Debug builds.  Reduce a proven built-in add chain iteratively.  Any
				// overloadable or unsupported operand deliberately falls through to the
				// ordinary recursive path, preserving user operator semantics.
				if (Binary.Op == EBinaryOp::Add)
				{
					std::vector<const FExpr*> Operands;
					const FExpr* Cursor = &Expression;
					while (Cursor && Cursor->Kind == EExprKind::Binary)
					{
						const FBinaryExpr& Node = static_cast<const FBinaryExpr&>(*Cursor);
						if (Node.Op != EBinaryOp::Add)
						{
							break;
						}
						Operands.push_back(Node.Right.get());
						Cursor = Node.Left.get();
					}
					if (Cursor)
					{
						Operands.push_back(Cursor);
					}
					std::reverse(Operands.begin(), Operands.end());
					if (Operands.size() >= 3)
					{
						auto IsConcatScalar = [](const std::string& TypeName) -> bool
						{
							return TypeName == "bool" || TypeName == "float" || TypeName == "double" || TypeName == "int" || TypeName == "char" || TypeName == "short" || TypeName == "long" || TypeName == "int64" || Runtime::IsUnsignedTypeName(TypeName);
						};
						auto TryReduceBuiltinAdd = [&](const std::string& LeftType,
						                               const std::string& RightType, std::string& OutType) -> bool
						{
							if (LeftType == "wstring" && RightType == "wstring")
							{
								OutType = "wstring";
								return true;
							}
							if (LeftType == "string" && RightType == "string")
							{
								OutType = "string";
								return true;
							}
							if ((LeftType == "string" && IsConcatScalar(RightType)) || (RightType == "string" && IsConcatScalar(LeftType)))
							{
								OutType = "string";
								return true;
							}
							const bool bLeftUnsigned = Runtime::IsUnsignedTypeName(LeftType);
							const bool bRightUnsigned = Runtime::IsUnsignedTypeName(RightType);
							const bool bLeftSigned = IsSignedIntegerTypeName(LeftType);
							const bool bRightSigned = IsSignedIntegerTypeName(RightType);
							const bool bLeftFloat = LeftType == "float" || LeftType == "double";
							const bool bRightFloat = RightType == "float" || RightType == "double";
							if (bLeftFloat || bRightFloat)
							{
								OutType = LeftType == "double" || RightType == "double"
								    ? "double"
								    : "float";
								return true;
							}
							if (bLeftUnsigned && bRightUnsigned)
							{
								OutType = LeftType == "uint64" || LeftType == "unsigned long" || RightType == "uint64" || RightType == "unsigned long"
								    ? "uint64"
								    : "unsigned int";
								return true;
							}
							if (bLeftSigned && bRightSigned)
							{
								OutType = LeftType == "int64" || LeftType == "long" || RightType == "int64" || RightType == "long"
								    ? "int64"
								    : "int";
								return true;
							}
							return false;
						};

						std::string AccumulatedType;
						bool bBuiltInChain = true;
						for (const FExpr* const Operand : Operands)
						{
							std::string OperandType;
							if (!Operand || !TryResolveExprTypeName(*Operand, OperandType) || TypeSupportsOperatorOverload(OperandType))
							{
								bBuiltInChain = false;
								break;
							}
							if (AccumulatedType.empty())
							{
								AccumulatedType = std::move(OperandType);
							}
							else if (!TryReduceBuiltinAdd(
							             AccumulatedType, OperandType, AccumulatedType))
							{
								bBuiltInChain = false;
								break;
							}
						}
						if (bBuiltInChain)
						{
							OutTypeName = std::move(AccumulatedType);
							return true;
						}
					}
				}
				std::string LeftType;
				std::string RightType;
				const bool bLeftTypeKnown = TryResolveExprTypeName(*Binary.Left, LeftType);
				const bool bRightTypeKnown = TryResolveExprTypeName(*Binary.Right, RightType);
				// Built-in boolean short-circuit operators are final once both operands
				// are known bools. Sending a left-deep &&/|| chain through user-operator
				// resolution at every node recursively re-infers all prefixes and can
				// grow exponentially for ordinary validation expressions.
				if (bLeftTypeKnown && bRightTypeKnown && LeftType == "bool" && RightType == "bool" && (Binary.Op == EBinaryOp::LogicalAnd || Binary.Op == EBinaryOp::LogicalOr))
				{
					OutTypeName = "bool";
					return true;
				}
				if (bLeftTypeKnown && bRightTypeKnown && TryResolveOverloadedBinaryType(Binary, OutTypeName, &LeftType, &RightType))
				{
					return true;
				}
				if (Binary.Op == EBinaryOp::LogicalAnd || Binary.Op == EBinaryOp::LogicalOr || Binary.Op == EBinaryOp::Equal || Binary.Op == EBinaryOp::NotEqual || Binary.Op == EBinaryOp::Less || Binary.Op == EBinaryOp::LessEqual || Binary.Op == EBinaryOp::Greater || Binary.Op == EBinaryOp::GreaterEqual || Binary.Op == EBinaryOp::In)
				{
					OutTypeName = "bool";
					return true;
				}

				if (!bLeftTypeKnown || !bRightTypeKnown)
				{
					return false;
				}

				if (Binary.Op == EBinaryOp::Add)
				{
					if (LeftType == "wstring" && RightType == "wstring")
					{
						OutTypeName = "wstring";
						return true;
					}
					if (LeftType == "string" && RightType == "string")
					{
						OutTypeName = "string";
						return true;
					}
					const auto IsConcatScalar = [](const std::string& TypeName) -> bool
					{
						return TypeName == "bool" || TypeName == "float" || TypeName == "double" || TypeName == "int" || TypeName == "char" || TypeName == "short" || TypeName == "long" || TypeName == "int64" || Runtime::IsUnsignedTypeName(TypeName);
					};
					if ((LeftType == "string" && IsConcatScalar(RightType)) || (RightType == "string" && IsConcatScalar(LeftType)))
					{
						OutTypeName = "string";
						return true;
					}
				}

				const bool bLeftUnsigned = Runtime::IsUnsignedTypeName(LeftType);
				const bool bRightUnsigned = Runtime::IsUnsignedTypeName(RightType);
				const bool bLeftSigned = IsSignedIntegerTypeName(LeftType);
				const bool bRightSigned = IsSignedIntegerTypeName(RightType);
				const bool bLeftFloat = LeftType == "float" || LeftType == "double";
				const bool bRightFloat = RightType == "float" || RightType == "double";
				if (Binary.Op == EBinaryOp::BitAnd || Binary.Op == EBinaryOp::BitOr || Binary.Op == EBinaryOp::BitXor || Binary.Op == EBinaryOp::ShiftLeft || Binary.Op == EBinaryOp::ShiftRight)
				{
					if (!(bLeftSigned || bLeftUnsigned) || !(bRightSigned || bRightUnsigned))
					{
						return false;
					}
					if (Binary.Op == EBinaryOp::ShiftLeft || Binary.Op == EBinaryOp::ShiftRight)
					{
						OutTypeName =
						    LeftType == "uint64" || LeftType == "unsigned long" ? "uint64"
						    : LeftType == "int64" || LeftType == "long"         ? "int64"
						    : bLeftUnsigned                                     ? "unsigned int"
						                                                        : "int";
						return true;
					}
					if (bLeftUnsigned && bRightUnsigned)
					{
						OutTypeName =
						    LeftType == "uint64" || LeftType == "unsigned long" || RightType == "uint64" || RightType == "unsigned long"
						    ? "uint64"
						    : "unsigned int";
						return true;
					}
					if (bLeftSigned && bRightSigned)
					{
						OutTypeName =
						    LeftType == "int64" || LeftType == "long" || RightType == "int64" || RightType == "long"
						    ? "int64"
						    : "int";
						return true;
					}
					return false;
				}
				if (bLeftFloat || bRightFloat)
				{
					OutTypeName = LeftType == "double" || RightType == "double"
					    ? "double"
					    : "float";
					return true;
				}
				if (bLeftUnsigned && bRightUnsigned)
				{
					OutTypeName =
					    LeftType == "uint64" || LeftType == "unsigned long" || RightType == "uint64" || RightType == "unsigned long"
					    ? "uint64"
					    : "unsigned int";
					return true;
				}
				if (bLeftSigned && bRightSigned)
				{
					OutTypeName =
					    LeftType == "int64" || LeftType == "long" || RightType == "int64" || RightType == "long"
					    ? "int64"
					    : "int";
					return true;
				}
				return false;
			}
		case EExprKind::Assign:
			{
				const FAssignExpr& Assign = static_cast<const FAssignExpr&>(Expression);
				if (TryResolveOverloadedAssignmentType(Assign, OutTypeName)) return true;
				if (TryResolveExprTypeName(*Assign.Target, OutTypeName))
				{
					return true;
				}
				return TryResolveExprTypeName(*Assign.Value, OutTypeName);
			}
		case EExprKind::Conditional:
			{
				const FConditionalExpr& Conditional = static_cast<const FConditionalExpr&>(Expression);
				std::string ThenType;
				std::string ElseType;
				const bool bThenKnown = TryResolveExprTypeName(*Conditional.ThenExpr, ThenType);
				const bool bElseKnown = TryResolveExprTypeName(*Conditional.ElseExpr, ElseType);
				if (bThenKnown && bElseKnown)
				{
					if (ThenType != ElseType)
					{
						return false;
					}
					OutTypeName = ThenType;
					return true;
				}
				if (bThenKnown)
				{
					OutTypeName = ThenType;
					return true;
				}
				if (bElseKnown)
				{
					OutTypeName = ElseType;
					return true;
				}
				return false;
			}
		default:
			return false;
		}
	}

}
