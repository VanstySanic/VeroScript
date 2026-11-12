// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"

#include "VeroScriptAst.h"
#include "VeroScriptAstClone.h"
#include "ConstantEvaluation/VeroScriptConstantBranches.h"
#include "VeroScriptBuiltins.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptRuntime.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <tuple>
#include <unordered_set>

namespace VeroScript
{

	bool VeroScriptCompiler::CompileLambdaExpression(const FLambdaExpr& SourceLambda, FEmitter& Emitter,
	    const bool bPreserveBindingIdentity)
	{
		FConstantBranchSelectionScope BranchScope(ConstantBranchState);
		if (ConstantBranchState && SourceLambda.Body)
		{
			ConstantBranchState->SelectEnclosing(SourceLambda.Body->ScopeId);
		}
		std::unique_ptr<FBlockStmt> SpecializedBody;
		if (SourceLambda.Body && !LowerConstantBranches(*SourceLambda.Body, SpecializedBody, &SourceLambda.Parameters)) return false;
		std::unique_ptr<FExpr> SpecializedLambda;
		if (SpecializedBody)
		{
			SpecializedLambda = CloneExpression(SourceLambda);
			static_cast<FLambdaExpr&>(*SpecializedLambda).Body = std::move(SpecializedBody);
		}
		const auto& Lambda = SpecializedLambda ? static_cast<const FLambdaExpr&>(*SpecializedLambda) : SourceLambda;
		if (!Lambda.Body)
		{
			ReportErrorId("diag.lambda_requires_a_body", Lambda.Location);
			return false;
		}

		std::vector<FFunctionParam> Parameters = Lambda.Parameters;
		std::string InferredReturnType = Lambda.ReturnType;
		std::vector<std::string> ExpectedParamTypes;
		std::string ExpectedReturnType;
		const bool bHaveExpectedFn = !ExpressionExpectedType.empty() && Runtime::ParseFunctionType(ExpressionExpectedType, ExpectedParamTypes, ExpectedReturnType);
		bool bNeedsContext = InferredReturnType.empty();
		for (const FFunctionParam& Parameter : Parameters)
		{
			if (Parameter.TypeName.empty())
			{
				bNeedsContext = true;
				break;
			}
		}
		if (bHaveExpectedFn)
		{
			if (Parameters.size() != ExpectedParamTypes.size())
			{
				ReportErrorId(
				    "diag.lambda_parameter_count_does_not_match_expected_function_type",
				    Lambda.Location);
				return false;
			}
			for (std::size_t Index = 0; Index < Parameters.size(); ++Index)
			{
				std::string ExpectedParamType = ExpectedParamTypes[Index];
				const bool bExpectedConstRef =
				    ExpectedParamType.rfind("const ref ", 0) == 0;
				const bool bExpectedRef = bExpectedConstRef || ExpectedParamType.rfind("ref ", 0) == 0;
				if (bExpectedConstRef)
				{
					ExpectedParamType.erase(0, 10);
				}
				else if (bExpectedRef)
				{
					ExpectedParamType.erase(0, 4);
				}
				if (Parameters[Index].TypeName.empty())
				{
					if (!bNeedsContext)
					{
						continue;
					}
					Parameters[Index].TypeName = ExpectedParamType;
					Parameters[Index].bIsRef = bExpectedRef;
					Parameters[Index].bIsConst = bExpectedConstRef;
					continue;
				}
				const std::string Written = QualifyTypeName(
				    NormalizeTypeAlias(SubstituteTypeName(Parameters[Index].TypeName)),
				    CurrentModulePath);
				const std::string Expected = QualifyTypeName(
				    NormalizeTypeAlias(SubstituteTypeName(ExpectedParamType)),
				    CurrentModulePath);
				// Fully written params: require exact match against expected fn type.
				// Partial fill (contextual): allow related types via bidirectional subtype.
				const bool bDirectionMatches =
				    Parameters[Index].bIsRef == bExpectedRef && (!bExpectedRef || Parameters[Index].bIsConst == bExpectedConstRef);
				const bool bCompatible = bDirectionMatches && (Written == Expected || (bNeedsContext && (IsSubtypeOf(Written, Expected) || IsSubtypeOf(Expected, Written))));
				if (!bCompatible)
				{
					const std::string WrittenDisplay = Parameters[Index].bIsRef
					    ? (Parameters[Index].bIsConst ? "const ref " : "ref ") + Written
					    : Written;
					const std::string ExpectedDisplay = bExpectedRef
					    ? (bExpectedConstRef ? "const ref " : "ref ") + Expected
					    : Expected;
					ReportErrorId(
					    "diag.lambda_parameter_type_conflicts_with_expected",
					    Lambda.Location,
					    {WrittenDisplay, ExpectedDisplay});
					return false;
				}
			}
			if (InferredReturnType.empty())
			{
				if (bNeedsContext)
				{
					InferredReturnType = ExpectedReturnType;
				}
			}
			else
			{
				const std::string Written = QualifyTypeName(
				    NormalizeTypeAlias(SubstituteTypeName(InferredReturnType)),
				    CurrentModulePath);
				const std::string Expected = QualifyTypeName(
				    NormalizeTypeAlias(SubstituteTypeName(ExpectedReturnType)),
				    CurrentModulePath);
				const bool bCompatible = Written == Expected || IsSubtypeOf(Written, Expected) || (bNeedsContext && IsSubtypeOf(Expected, Written));
				if (!bCompatible)
				{
					ReportErrorId(
					    "diag.lambda_return_type_conflicts_with_expected",
					    Lambda.Location,
					    {Written, Expected});
					return false;
				}
			}
		}
		else if (bNeedsContext)
		{
			ReportErrorId(
			    "diag.cannot_infer_lambda_parameter_return_types_without_function_typed_context",
			    Lambda.Location);
			return false;
		}

		std::vector<std::pair<std::string, std::string>> UsedCaptures;
		bool bNeedsThis = false;
		if (!CollectLambdaCaptures(Lambda, UsedCaptures, bNeedsThis, Lambda.Location)) return false;
		Lambda.bCapturesThis.store(bNeedsThis, std::memory_order_release);
		if (Lambda.CaptureKind == ELambdaCaptureKind::None && (bNeedsThis || !UsedCaptures.empty()))
		{
			ReportErrorId("diag.lambda_captureless_context", Lambda.Location);
			return false;
		}
		std::vector<FBytecodeCapture> Captures;
		std::unordered_map<std::string, const FLambdaCapture*> CaptureSources;
		std::unordered_set<std::string> ExplicitNames;
		for (const auto& Source : Lambda.Captures)
		{
			if (!ExplicitNames.insert(Source.Name).second)
			{
				ReportErrorId("diag.duplicate_capture_name", Source.Location, {Source.Name});
				return false;
			}
			for (const auto& Parameter : Parameters)
			{
				if (Parameter.Name == Source.Name)
				{
					ReportErrorId("diag.lambda_capture_parameter_conflict", Source.Location, {Source.Name});
					return false;
				}
			}
			if (!Source.Initializer && ((Lambda.CaptureKind == ELambdaCaptureKind::Implicit && !Source.bByReference) || (Lambda.CaptureKind == ELambdaCaptureKind::Reference && Source.bByReference)))
			{
				ReportErrorId("diag.lambda_redundant_capture_default", Source.Location);
				return false;
			}
			std::string Type;
			bool bConst = false;
			if (Source.Initializer)
			{
				// Each capture initializer is inferred independently, like an auto declaration.
				struct FRestoreExpectedType
				{
					std::string& Current;
					std::string Previous;
					~FRestoreExpectedType()
					{
						Current = std::move(Previous);
					}
				} RestoreExpected{ExpressionExpectedType, std::move(ExpressionExpectedType)};
				ExpressionExpectedType.clear();
				if (!TryResolveExprRawTypeName(*Source.Initializer, Type))
				{
					ReportErrorId("diag.lambda_capture_type", Source.Location, {Source.Name});
					return false;
				}
				bConst = Source.bByReference && (Runtime::IsConstReferenceTypeName(Type) || IsConstAssignTarget(*Source.Initializer));
			}
			else
			{
				std::uint16_t Slot = 0;
				if (!ResolveLocal(Source.Name, Slot))
				{
					std::uint16_t Field = 0;
					const auto Global = ResolveGlobalLinkageName(Source.Name, CurrentModulePath);
					if (ResolveThisField(Source.Name, Field) || (!Global.empty() && ResolveGlobal(Global, Slot)))
					{
						continue;
					}
					ReportErrorId("diag.undeclared_capture_name", Source.Location, {Source.Name});
					return false;
				}
				if (!TryResolveVariableTypeName(Source.Name, Type)) return false;
				bConst = IsConstBinding(Source.Name);
			}
			Type = CanonicalTypeNameForCoercion(std::string(Runtime::StripReferenceTypeName(Type)));
			if (Type.empty() || Type == "void")
			{
				ReportErrorId("diag.lambda_capture_value_type", Source.Location, {Source.Name});
				return false;
			}
			const auto Mode = Source.bByReference ? EClosureCaptureMode::Reference
			                                      : (Lambda.bMutable && !bConst && !bPreserveBindingIdentity ? EClosureCaptureMode::MutableValue : EClosureCaptureMode::Value);
			Captures.push_back({Source.Name, Type, Mode, !bConst && (Source.bByReference || Lambda.bMutable)});
			CaptureSources.emplace(Source.Name, &Source);
		}
		for (const auto& Used : UsedCaptures)
		{
			if (ExplicitNames.count(Used.first))
			{
				continue;
			}
			if (Lambda.CaptureKind == ELambdaCaptureKind::Explicit)
			{
				ReportErrorId("diag.lambda_body_uses_unlisted_capture", Lambda.Location, {Used.first});
				return false;
			}
			const bool bReference = Lambda.CaptureKind == ELambdaCaptureKind::Reference;
			const bool bConst = IsConstBinding(Used.first);
			Captures.push_back({Used.first, std::string(Runtime::StripReferenceTypeName(Used.second)),
			    bReference ? EClosureCaptureMode::Reference
			               : (Lambda.bMutable && !bConst && !bPreserveBindingIdentity ? EClosureCaptureMode::MutableValue : EClosureCaptureMode::Value),
			    !bConst && (bReference || Lambda.bMutable)});
		}
		if (IsolatedCallableLambda == &Lambda)
		{
			for (const auto& Capture : Captures)
			{
				if (Capture.Mode == EClosureCaptureMode::Reference || !Runtime::IsAllowedChannelElementType(Capture.TypeName))
				{
					ReportErrorId("diag.lambda_capture_not_transferable", Lambda.Location, {Capture.Name});
					return false;
				}
			}
		}

		std::vector<std::string> ParamTypes;
		ParamTypes.reserve(Parameters.size());
		for (const FFunctionParam& Parameter : Parameters)
		{
			if (!ValidateFieldTypeName(Parameter.TypeName, Lambda.Location))
			{
				return false;
			}
			std::string ParamType = QualifyTypeName(
			    NormalizeTypeAlias(SubstituteTypeName(Parameter.TypeName)),
			    CurrentModulePath);
			if (Parameter.bIsRef)
			{
				ParamType = Parameter.bIsConst
				    ? "const ref " + ParamType
				    : "ref " + ParamType;
			}
			ParamTypes.push_back(std::move(ParamType));
		}
		if (InferredReturnType != "void" && !ValidateFieldTypeName(std::string(Runtime::StripReferenceTypeName(InferredReturnType)), Lambda.Location))
		{
			return false;
		}
		const std::string ReturnType = QualifyTypeName(
		    NormalizeTypeAlias(SubstituteTypeName(InferredReturnType)),
		    CurrentModulePath);
		const std::string FunctionTypeName = Runtime::MakeFunctionTypeName(ParamTypes, ReturnType);
		std::string LambdaOwner = CurrentFunction ? CurrentFunction->Name : CurrentModulePath + "$init";
		if (!CurrentFunction && CurrentInitializerStatement && CurrentInitializerStatement->Kind == EStmtKind::VarDecl)
		{
			const auto& Declaration = static_cast<const FVarDeclStmt&>(*CurrentInitializerStatement);
			LambdaOwner = "$global$" + (Declaration.ModulePath.empty() ? Declaration.Name : GetSymbolLinkageName(Declaration.ModulePath, Declaration.bExported, Declaration.Name));
		}
		else if (!CurrentFunction && CurrentInitializerField)
		{
			LambdaOwner = "$field$" + CurrentOwnerTypeLinkage + "$" + CurrentInitializerField->Name;
		}
		const auto ParentIdentity = ComputeLambdaParentIdentity(LambdaOwner);
		const auto EncodeIdentity = [](std::uint64_t Value)
		{
			constexpr char Digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
			char Buffer[11];
			auto* End = Buffer + sizeof(Buffer);
			auto* Begin = End;
			do
			{
				*--Begin = Digits[Value % 62u];
				Value /= 62u;
			}
			while (Value != 0);
			return std::string(Begin, End);
		};
		// The owner and lexical ordinal give stable names across unrelated edits.
		// Compact names also keep ordinary closure creation within string's small buffer.
		auto OwnerIdentity = Runtime::Fnv1a64(14695981039346656037ull, LambdaOwner);
		if (!bPreserveBindingIdentity && (Lambda.bMutable || std::any_of(Lambda.Captures.begin(), Lambda.Captures.end(), [](const auto& Capture)
		{
			return Capture.Initializer != nullptr;
		})))
		{
			OwnerIdentity = Runtime::Fnv1a64(OwnerIdentity, std::to_string(ParentIdentity));
		}
		const std::string LambdaName = "$L" + EncodeIdentity(OwnerIdentity) + "_" + EncodeIdentity(LambdaOrdinals[LambdaOwner]++);
		LambdaOwnerIdentities[LambdaName].ParentIdentity = ParentIdentity;

		const bool bCompileAsMethod = bNeedsThis;
		const bool bConstThis = bNeedsThis && CurrentFunction && CurrentFunction->bIsConstMethod;
		const std::uint16_t OwnerTypeIndex = bNeedsThis && CurrentFunction
		    ? CurrentFunction->OwnerTypeIndex
		    : static_cast<std::uint16_t>(-1);
		const std::string PreviousExpectedType = ExpressionExpectedType;
		const std::string LexicalAccessFunctionLinkage = CurrentAccessFunctionLinkage;
		ExpressionExpectedType.clear();
		const bool bBodyOk = CompileFunction(
		    LambdaName,
		    InferredReturnType,
		    Parameters,
		    *Lambda.Body,
		    bCompileAsMethod,
		    OwnerTypeIndex,
		    CurrentModulePath,
		    Lambda.Location,
		    false,
		    &Captures,
		    bConstThis,
		    nullptr,
		    &LexicalAccessFunctionLinkage);
		ExpressionExpectedType = PreviousExpectedType;
		if (!bBodyOk)
		{
			return false;
		}

		const std::uint16_t FunctionIndex = FindFunctionIndex(LambdaName);
		if (FunctionIndex == static_cast<std::uint16_t>(-1))
		{
			ReportErrorId("diag.failed_to_compile_lambda", Lambda.Location);
			return false;
		}
		auto& CompiledCaptures = Module->Functions[FunctionIndex].Captures;
		auto& StaticIdentity = Module->Functions[FunctionIndex].LocalStaticSemanticIdentity;
		if (!Module->Functions[FunctionIndex].LocalStaticInitializers.empty())
		{
			StaticIdentity = Runtime::Fnv1a64(ParentIdentity, std::to_string(StaticIdentity));
			if (StaticIdentity == 0)
			{
				StaticIdentity = 1;
			}
		}
		for (std::size_t Ip = 0; Ip < Module->Functions[FunctionIndex].Code.size();)
		{
			const auto& Code = Module->Functions[FunctionIndex].Code;
			const auto Op = static_cast<EOpcode>(Code[Ip]);
			if (Op == EOpcode::MakeLocalRef)
			{
				const auto Slot = Bytecode::ReadU16LE(Code.data() + Ip + 1u);
				if (Slot >= Parameters.size() && Slot - Parameters.size() < CompiledCaptures.size())
				{
					auto& Capture = CompiledCaptures[Slot - Parameters.size()];
					if (bPreserveBindingIdentity && Capture.Mode == EClosureCaptureMode::Value && Capture.bWritable)
					{
						ReportError("generated method binding cannot borrow its receiver binding", Lambda.Location);
						return false;
					}
					Capture.bStableStorage = Capture.Mode == EClosureCaptureMode::Value;
				}
			}
			Ip += Bytecode::InstructionSize(Op);
		}

		if (Captures.size() > 255)
		{
			ReportErrorId("diag.too_many_lambda_captures", Lambda.Location);
			return false;
		}
		for (const FBytecodeCapture& Capture : Captures)
		{
			const auto Found = CaptureSources.find(Capture.Name);
			const FExpr* Initializer = Found != CaptureSources.end() ? Found->second->Initializer.get() : nullptr;
			FIdentifierExpr Source(Capture.Name);
			Source.Location = Lambda.Location;
			const FExpr& Expression = Initializer ? *Initializer : Source;
			if (Capture.Mode == EClosureCaptureMode::Reference)
			{
				if (!CompileRefArgument(Emitter, Expression, Expression.Location, Capture.bWritable)) return false;
			}
			else
			{
				const auto PreviousExpected = ExpressionExpectedType;
				ExpressionExpectedType = Capture.TypeName;
				const bool bCompiled = CompileExpression(Expression) && EmitValueCoercionForTarget(Emitter, Capture.TypeName, Expression, Expression.Location);
				ExpressionExpectedType = PreviousExpected;
				if (!bCompiled) return false;
				if (NeedsPassByValueCopy(Capture.TypeName) && !CanTransferCoercedValue(Expression, Capture.TypeName))
				{
					EmitValueCopy(Emitter, Capture.TypeName);
				}
			}
		}

		if (bNeedsThis && OwnerTypeIndex < Module->Types.size() && Module->Types[OwnerTypeIndex].Kind == EScriptObjectKind::Struct)
		{
			CopyRequirements.push_back({Module->Types[OwnerTypeIndex].Name,
			    CurrentFunction ? CurrentFunction->Name : "", Lambda.Location, false, CurrentAccessFunctionLinkage});
		}
		Emitter.EmitMakeClosure(FunctionIndex, static_cast<std::uint8_t>(Captures.size()));
		(void)FunctionTypeName;
		return true;
	}
	bool VeroScriptCompiler::CollectLambdaCaptures(
	    const FLambdaExpr& Lambda,
	    std::vector<std::pair<std::string, std::string>>& OutCaptures,
	    bool& OutNeedsThis,
	    const FSourceLocation& Location)
	{
		OutCaptures.clear();
		OutNeedsThis = false;
		std::unordered_set<std::string> ParamNames;
		for (const FFunctionParam& Parameter : Lambda.Parameters)
		{
			ParamNames.insert(Parameter.Name);
		}
		// Scoped declaration stacks so nested lambdas / blocks do not suppress outer captures.
		std::vector<std::unordered_set<std::string>> DeclaredStack(1);
		for (const auto& Capture : Lambda.Captures)
		{
			if (Capture.Initializer)
			{
				DeclaredStack.front().insert(Capture.Name);
			}
		}
		std::unordered_set<std::string> SeenCaptures;

		const auto IsDeclaredInside = [&](const std::string& Name) -> bool
		{
			if (ParamNames.find(Name) != ParamNames.end())
			{
				return true;
			}
			for (const std::unordered_set<std::string>& Frame : DeclaredStack)
			{
				if (Frame.find(Name) != Frame.end())
				{
					return true;
				}
			}
			return false;
		};

		const auto PushDeclaredScope = [&]()
		{
			DeclaredStack.emplace_back();
		};
		const auto PopDeclaredScope = [&]()
		{
			if (DeclaredStack.size() > 1)
			{
				DeclaredStack.pop_back();
			}
		};
		const auto DeclareInside = [&](const std::string& Name)
		{
			DeclaredStack.back().insert(Name);
		};

		const auto RequireThisContext = [&]() -> bool
		{
			if (CurrentFunction && (CurrentFunction->bIsMethod || !CurrentInterfaceOwnerLinkage.empty()))
			{
				return true;
			}
			ReportErrorId("diag.lambda_this_capture_requires_enclosing_method", Location);
			return false;
		};

		const auto ConsiderIdentifier = [&](const std::string& Name) -> bool
		{
			if (IsDeclaredInside(Name))
			{
				return true;
			}
			if (!ResolveLocalEnumConstantLinkageName(Name).empty()) return true;
			std::uint16_t Slot = 0;
			if (!ResolveLocal(Name, Slot))
			{
				std::uint16_t FieldIndex = 0;
				if (ResolveThisField(Name, FieldIndex))
				{
					OutNeedsThis = true;
					return RequireThisContext();
				}
				return true;
			}
			if (SeenCaptures.insert(Name).second)
			{
				std::string TypeName;
				if (!TryResolveVariableTypeName(Name, TypeName))
				{
					ReportErrorId("diag.cannot_resolve_capture_type", Location, {Name});
					return false;
				}
				OutCaptures.emplace_back(Name, TypeName);
			}
			return true;
		};

		std::function<bool(const FExpr&)> WalkExpr;
		std::function<bool(const FStmt&)> WalkStmt;
		const auto WalkReceiver = [&](const FExpr& Receiver) -> bool
		{
			if (IsSuperReceiver(Receiver) && !IsDeclaredInside(static_cast<const FIdentifierExpr&>(Receiver).Name))
			{
				OutNeedsThis = true;
				return RequireThisContext();
			}
			return WalkExpr(Receiver);
		};

		WalkExpr = [&](const FExpr& Expression) -> bool
		{
			switch (Expression.Kind)
			{
			case EExprKind::Identifier:
				return ConsiderIdentifier(static_cast<const FIdentifierExpr&>(Expression).Name);
			case EExprKind::Unary:
				return WalkExpr(*static_cast<const FUnaryExpr&>(Expression).Operand);
			case EExprKind::Binary:
				{
					const FBinaryExpr& Binary = static_cast<const FBinaryExpr&>(Expression);
					return WalkExpr(*Binary.Left) && WalkExpr(*Binary.Right);
				}
			case EExprKind::Assign:
				{
					const FAssignExpr& Assign = static_cast<const FAssignExpr&>(Expression);
					return WalkExpr(*Assign.Target) && WalkExpr(*Assign.Value);
				}
			case EExprKind::Conditional:
				{
					const FConditionalExpr& Conditional = static_cast<const FConditionalExpr&>(Expression);
					return WalkExpr(*Conditional.Condition) && WalkExpr(*Conditional.ThenExpr) && WalkExpr(*Conditional.ElseExpr);
				}
			case EExprKind::Ref:
				return WalkExpr(*static_cast<const FRefExpr&>(Expression).Operand);
			case EExprKind::Call:
				{
					const FCallExpr& Call = static_cast<const FCallExpr&>(Expression);
					if (Call.CalleeExpr)
					{
						if (!WalkExpr(*Call.CalleeExpr))
						{
							return false;
						}
					}
					else
					{
						// Locals used as `a(...)` may be fn values or types with operator().
						const bool bDeclaredInside = IsDeclaredInside(Call.Callee);
						if (!ConsiderIdentifier(Call.Callee))
						{
							return false;
						}
						if (!bDeclaredInside && BareCallNeedsImplicitThisCapture(Call))
						{
							OutNeedsThis = true;
							if (!RequireThisContext())
							{
								return false;
							}
						}
					}
					for (const FExpr* Argument : Call.GetArguments())
					{
						if (Argument && !WalkExpr(*Argument))
						{
							return false;
						}
					}
					return true;
				}
			case EExprKind::This:
				{
					OutNeedsThis = true;
					return RequireThisContext();
				}
			case EExprKind::MemberAccess:
				return WalkReceiver(*static_cast<const FMemberAccessExpr&>(Expression).Object);
			case EExprKind::MethodCall:
				{
					const FMethodCallExpr& MethodCall = static_cast<const FMethodCallExpr&>(Expression);
					if (!WalkReceiver(*MethodCall.Object))
					{
						return false;
					}
					for (const FExpr* Argument : MethodCall.GetArguments())
					{
						if (Argument && !WalkExpr(*Argument))
						{
							return false;
						}
					}
					return true;
				}
			case EExprKind::IndexAccess:
				{
					const FIndexAccessExpr& IndexAccess = static_cast<const FIndexAccessExpr&>(Expression);
					return WalkExpr(*IndexAccess.Object) && WalkExpr(*IndexAccess.Index);
				}
			case EExprKind::ArrayInit:
				{
					const FArrayInitExpr& ArrayInit = static_cast<const FArrayInitExpr&>(Expression);
					for (const std::unique_ptr<FExpr>& Element : ArrayInit.Elements)
					{
						if (Element && !WalkExpr(*Element))
						{
							return false;
						}
					}
					return true;
				}
			case EExprKind::MapInit:
				{
					const FMapInitExpr& MapInit = static_cast<const FMapInitExpr&>(Expression);
					for (const FMapInitEntry& Entry : MapInit.Entries)
					{
						if ((Entry.Key && !WalkExpr(*Entry.Key)) || (Entry.Value && !WalkExpr(*Entry.Value)))
						{
							return false;
						}
					}
					return true;
				}
			case EExprKind::Lambda:
				{
					// Walk nested bodies so grandparent free names used ONLY inside
					// nested lambdas are packed into the outer closure
					// (`[=]{ [=]() => x }` needs outer to capture x). Nested params/
					// locals are declared inside so they do not suppress or steal
					// outer captures. Captureless `[] { [=]{ x } }` correctly fails
					// because empty `[]` cannot forward those slots.
					const FLambdaExpr& Nested = static_cast<const FLambdaExpr&>(Expression);
					for (const auto& Capture : Nested.Captures)
					{
						if (Capture.Initializer)
						{
							if (!WalkExpr(*Capture.Initializer)) return false;
						}
						else if (!ConsiderIdentifier(Capture.Name))
						{
							return false;
						}
					}
					PushDeclaredScope();
					for (const auto& Capture : Nested.Captures)
					{
						if (Capture.Initializer)
						{
							DeclareInside(Capture.Name);
						}
					}
					for (const FFunctionParam& Parameter : Nested.Parameters)
					{
						DeclareInside(Parameter.Name);
					}
					bool bOk = true;
					if (Nested.Body)
					{
						bOk = WalkStmt(*Nested.Body);
					}
					if (bOk && Nested.ExpressionBody)
					{
						bOk = WalkExpr(*Nested.ExpressionBody);
					}
					PopDeclaredScope();
					return bOk;
				}
			default:
				return true;
			}
		};

		WalkStmt = [&](const FStmt& Statement) -> bool
		{
			switch (Statement.Kind)
			{
			case EStmtKind::Expr:
				return WalkExpr(*static_cast<const FExprStmt&>(Statement).Expression);
			case EStmtKind::TypeAlias:
				{
					if (const auto& Alias = static_cast<const FTypeAliasStmt&>(Statement); Alias.bTypeDefinition)
					{
						if (const auto* Enum = FindEnumDeclaration(Alias.TypeName); Enum && !Enum->bScoped)
						{
							for (const auto& Member : Enum->Members)
							{
								DeclareInside(Member.Name);
							}
						}
					}
					return true;
				}
			case EStmtKind::VarDecl:
				{
					const FVarDeclStmt& VarDecl = static_cast<const FVarDeclStmt&>(Statement);
					if (VarDecl.Initializer && !WalkExpr(*VarDecl.Initializer))
					{
						return false;
					}
					DeclareInside(VarDecl.Name);
					return true;
				}
			case EStmtKind::VarDeclGroup:
				{
					const FVarDeclGroupStmt& Group = static_cast<const FVarDeclGroupStmt&>(Statement);
					for (const std::unique_ptr<FVarDeclStmt>& Declaration : Group.Declarations)
					{
						if (!Declaration)
						{
							return false;
						}
						if (Declaration->Initializer && !WalkExpr(*Declaration->Initializer))
						{
							return false;
						}
						// Capture analysis follows the same source-order visibility rule as
						// statement compilation.
						DeclareInside(Declaration->Name);
					}
					return true;
				}
			case EStmtKind::Return:
				{
					const FReturnStmt& Return = static_cast<const FReturnStmt&>(Statement);
					return !Return.Value || WalkExpr(*Return.Value);
				}
			case EStmtKind::Throw:
				{
					const FThrowStmt& ThrowStmt = static_cast<const FThrowStmt&>(Statement);
					return !ThrowStmt.Value || WalkExpr(*ThrowStmt.Value);
				}
			case EStmtKind::Try:
				{
					const FTryStmt& TryStmt = static_cast<const FTryStmt&>(Statement);
					if (TryStmt.TryBlock && !WalkStmt(*TryStmt.TryBlock))
					{
						return false;
					}
					for (const FCatchClause& Clause : TryStmt.Catches)
					{
						PushDeclaredScope();
						if (!Clause.Name.empty())
						{
							DeclareInside(Clause.Name);
						}
						const bool bOk = !Clause.Body || WalkStmt(*Clause.Body);
						PopDeclaredScope();
						if (!bOk)
						{
							return false;
						}
					}
					return !TryStmt.FinallyBlock || WalkStmt(*TryStmt.FinallyBlock);
				}
			case EStmtKind::Block:
				{
					PushDeclaredScope();
					bool bOk = true;
					for (const std::unique_ptr<FStmt>& Child : static_cast<const FBlockStmt&>(Statement).Statements)
					{
						if (Child && !WalkStmt(*Child))
						{
							bOk = false;
							break;
						}
					}
					PopDeclaredScope();
					return bOk;
				}
			case EStmtKind::If:
				{
					const FIfStmt& IfStmt = static_cast<const FIfStmt&>(Statement);
					if (!WalkExpr(*IfStmt.Condition))
					{
						return false;
					}
					if (IfStmt.ThenBranch)
					{
						PushDeclaredScope();
						if (!WalkStmt(*IfStmt.ThenBranch))
						{
							PopDeclaredScope();
							return false;
						}
						PopDeclaredScope();
					}
					if (IfStmt.ElseBranch)
					{
						PushDeclaredScope();
						if (!WalkStmt(*IfStmt.ElseBranch))
						{
							PopDeclaredScope();
							return false;
						}
						PopDeclaredScope();
					}
					return true;
				}
			case EStmtKind::While:
				{
					const FWhileStmt& WhileStmt = static_cast<const FWhileStmt&>(Statement);
					if (!WalkExpr(*WhileStmt.Condition))
					{
						return false;
					}
					if (!WhileStmt.Body)
					{
						return true;
					}
					PushDeclaredScope();
					const bool bOk = WalkStmt(*WhileStmt.Body);
					PopDeclaredScope();
					return bOk;
				}
			case EStmtKind::DoWhile:
				{
					const FDoWhileStmt& DoWhileStmt = static_cast<const FDoWhileStmt&>(Statement);
					if (DoWhileStmt.Body)
					{
						PushDeclaredScope();
						const bool bBodyOk = WalkStmt(*DoWhileStmt.Body);
						PopDeclaredScope();
						if (!bBodyOk)
						{
							return false;
						}
					}
					return !DoWhileStmt.Condition || WalkExpr(*DoWhileStmt.Condition);
				}
			case EStmtKind::For:
				{
					const FForStmt& ForStmt = static_cast<const FForStmt&>(Statement);
					PushDeclaredScope();
					bool bOk = true;
					if (ForStmt.Init && !WalkStmt(*ForStmt.Init))
					{
						bOk = false;
					}
					for (const std::unique_ptr<FExpr>& InitExpression : ForStmt.InitExpressions)
					{
						if (bOk && InitExpression && !WalkExpr(*InitExpression))
						{
							bOk = false;
						}
					}
					if (bOk && ForStmt.Condition && !WalkExpr(*ForStmt.Condition))
					{
						bOk = false;
					}
					if (bOk && ForStmt.Step && !WalkExpr(*ForStmt.Step))
					{
						bOk = false;
					}
					for (const std::unique_ptr<FExpr>& StepExpression : ForStmt.StepExpressions)
					{
						if (bOk && StepExpression && !WalkExpr(*StepExpression))
						{
							bOk = false;
						}
					}
					if (bOk && ForStmt.Body && !WalkStmt(*ForStmt.Body))
					{
						bOk = false;
					}
					PopDeclaredScope();
					return bOk;
				}
			case EStmtKind::ForEach:
				{
					const FForEachStmt& ForEachStmt = static_cast<const FForEachStmt&>(Statement);
					if (ForEachStmt.Collection && !WalkExpr(*ForEachStmt.Collection))
					{
						return false;
					}
					PushDeclaredScope();
					DeclareInside(ForEachStmt.ElementName);
					if (ForEachStmt.bHasKeyBinding)
					{
						DeclareInside(ForEachStmt.KeyName);
					}
					const bool bOk = !ForEachStmt.Body || WalkStmt(*ForEachStmt.Body);
					PopDeclaredScope();
					return bOk;
				}
			case EStmtKind::Switch:
				{
					const FSwitchStmt& SwitchStmt = static_cast<const FSwitchStmt&>(Statement);
					if (SwitchStmt.Discriminant && !WalkExpr(*SwitchStmt.Discriminant))
					{
						return false;
					}
					for (const FSwitchCase& Case : SwitchStmt.Cases)
					{
						for (const std::unique_ptr<FExpr>& Label : Case.Labels)
						{
							if (Label && !WalkExpr(*Label))
							{
								return false;
							}
						}
						if (Case.Body)
						{
							PushDeclaredScope();
							if (!WalkStmt(*Case.Body))
							{
								PopDeclaredScope();
								return false;
							}
							PopDeclaredScope();
						}
					}
					return true;
				}
			case EStmtKind::Fallthrough:
			case EStmtKind::Break:
			case EStmtKind::Continue:
			case EStmtKind::Goto:
			case EStmtKind::Label:
				return true;
			default:
				return true;
			}
		};

		if (Lambda.Body && !WalkStmt(*Lambda.Body))
		{
			return false;
		}
		if (Lambda.ExpressionBody && !WalkExpr(*Lambda.ExpressionBody))
		{
			return false;
		}
		return true;
	}

}
