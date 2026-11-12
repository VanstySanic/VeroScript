// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "VeroScriptAst.h"
#include "VeroScriptBuiltins.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptMethodBinding.h"
#include "VeroScriptMemberAccess.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptStandardLibrary.h"

namespace VeroScript
{
	namespace
	{
		template<typename T, typename... Args>
		std::unique_ptr<T> Located(const FSourceLocation& Location, Args&&... Arguments)
		{
			auto Node = std::make_unique<T>(std::forward<Args>(Arguments)...);
			Node->Location = Location;
			return Node;
		}
	}

	bool VeroScriptCompiler::IsMethodBindingFactoryDeclaration(const FFunctionDecl& Function) const
	{
		return StandardLibrary::IsMethodBindingFactoryDeclaration(Function);
	}

	bool VeroScriptCompiler::ResolveMethodBindingFactoryType(const FCallExpr& Call, std::string& OutType,
		std::string& OutReceiver, std::string& OutFunctionType) const
	{
		const FFunctionDecl* Declaration = FindUniqueFreeFunction(Call.Callee, CurrentModulePath);
		if (!Declaration || !IsMethodBindingFactoryDeclaration(*Declaration))
		{
			ReportError("method binding factory declaration is not imported", Call.Location);
			return false;
		}
		if (Call.TypeArgs.empty() || Call.TypeArgs.size() > 2 || Call.GetArguments().size() != 2)
		{
			ReportError("method binding expects a result type, an object and a method name", Call.Location);
			return false;
		}
		const bool bWeak = Declaration->Name == "__std_make_weak_attribute_by_name";
		const bool bAttribute = Declaration->Name != "__std_bind_method";
		const auto Concrete = [&](const std::string& Type)
		{
			return QualifyTypeName(NormalizeTypeAlias(SubstituteTypeName(Type)), CurrentModulePath);
		};
		std::string TargetType;
		if (!TryResolveExprTypeName(*Call.GetArguments()[0], TargetType)) return false;
		TargetType = Concrete(TargetType);
		if (bWeak)
		{
			std::string Base;
			std::vector<std::string> Arguments;
			if (GenericTemplateBaseLinkage(TargetType) == "weak")
			{
				Arguments = DecodeGenericConcreteTypes(TargetType);
			}

			else if (!SplitUserGenericTypeApp(TargetType, Base, Arguments) || Base != "weak")
			{
				Arguments.clear();
			}

			if (Arguments.size() != 1)
			{
				ReportError("weak method binding requires weak<U>", Call.Location);
				return false;
			}
			OutReceiver = Concrete(Arguments[0]);
		}
		else
		{
			OutReceiver = TargetType;
		}

		if (Call.TypeArgs.size() == 2)
		{
			OutReceiver = Concrete(Call.TypeArgs[1]);
		}

		const std::string ValueType = Concrete(Call.TypeArgs.front());
		OutFunctionType = bAttribute ? Runtime::MakeFunctionTypeName({}, ValueType) : ValueType;
		std::vector<std::string> Parameters;
		std::string ReturnType;
		if (!Runtime::ParseFunctionType(OutFunctionType, Parameters, ReturnType) ||
			(bAttribute && (ValueType == "void" || Runtime::IsReferenceTypeName(ValueType))))
		{
			if (!HasError())
			{
				ReportError("method binding requires a concrete callable signature", Call.Location);
			}

			return false;
		}
		const auto* Host = ActiveBindings ? ActiveBindings->FindType(OutReceiver) : nullptr;
		const bool bWrapped = GenericTemplateBaseLinkage(OutReceiver) == "handle";
		if (!bWrapped && (IsStructType(OutReceiver) || (Host && Host->Kind != EHostTypeKind::Class)))
		{
			ReportError("value-type method binding requires shared<T> or handle<T>", Call.Location);
			return false;
		}
		OutType = bAttribute ? Concrete("attribute<" + ValueType + ">") : OutFunctionType;
		return true;
	}

	bool VeroScriptCompiler::CompileMethodBindingFactory(const FCallExpr& Call, FEmitter& Emitter)
	{
		const FFunctionDecl* Declaration = FindUniqueFreeFunction(Call.Callee, CurrentModulePath);
		if (!Declaration) return false;
		std::string ResultType, ReceiverType, FunctionType;
		if (!ResolveMethodBindingFactoryType(Call, ResultType, ReceiverType, FunctionType)) return false;
		std::string Resolved;
		if (!ValidateFieldTypeName(FunctionType, Call.Location)) return false;
		if (EnsureStructTypeResolved(ReceiverType, Resolved, nullptr, Call.Location))
		{
			ReceiverType = Resolved;
		}

		else if (HasError())
		{
			return false;
		}

		if (Declaration->Name != "__std_bind_method")
		{
			if (!EnsureStructTypeResolved(ResultType, Resolved, nullptr, Call.Location)) return false;
			ResultType = Resolved;
		}
		struct FScopeRestore
		{
			~FScopeRestore()
			{
				Stack.resize(Size);
			}

			std::vector<FScope>& Stack;

			std::size_t Size;
		} ScopeRestore{Scopes, Scopes.size()};
		if (Scopes.empty())
		{
			Scopes.emplace_back();
		}

		const bool bWeak = Declaration->Name == "__std_make_weak_attribute_by_name";
		const bool bAttribute = Declaration->Name != "__std_bind_method";
		const bool bConst = !bWeak && IsConstAssignTarget(*Call.GetArguments()[0]);
		const bool bWrapped = GenericTemplateBaseLinkage(ReceiverType) == "handle";
		std::string MethodReceiverType = ReceiverType;
		if (bWrapped)
		{
			const auto Arguments = DecodeGenericConcreteTypes(ReceiverType);
			if (Arguments.size() != 1) return false;
			MethodReceiverType = Arguments[0];
		}
		std::vector<FMethodBindingSelection> Candidates;
		std::string BindingError;
		if (!CollectMethodBindingCandidates(
				*Module, ActiveBindings, MethodReceiverType, Candidates, BindingError))
		{
			ReportError(BindingError, Call.Location);
			return false;
		}
		FMethodBindingCodegenInfo Codegen;
		Codegen.SourceFile = Call.Location.FileLabel;
		Codegen.Line = Call.Location.Line;
		Codegen.Column = Call.Location.Column;
		Codegen.FactoryName = Declaration->Name;
		Codegen.ReceiverType = MethodReceiverType;
		Codegen.FunctionType = FunctionType;
		Codegen.bWrappedReceiver = bWrapped;
		Codegen.bConstReceiver = bConst;
		const FMethodDecl* GenericMethod = nullptr;
		std::string GenericOwner, GenericName;
		std::vector<std::string> GenericArguments;
		if (Call.GetArguments()[1]->Kind == EExprKind::StringLiteral)
		{
			auto& Selection = Codegen.Selection;
			Codegen.bConstantName = true;
			FMethodBindingRequest Request{MethodReceiverType,
				static_cast<const FStringLiteralExpr&>(*Call.GetArguments()[1]).Value, FunctionType, bConst};
			std::vector<std::string> Parameters;
			std::string Return;
			Runtime::ParseFunctionType(FunctionType, Parameters, Return);
			const FMethodDecl* Selected = nullptr;
			if (ResolveMethodForCall(MethodReceiverType, Request.MethodName, Parameters, GenericOwner,
				Selected, Call.Location, false, false, nullptr, bConst, nullptr, &Return)
				&& Selected && MethodSpecializationKeys.count(Selected) != 0)
			{
				const auto& Instance = MethodSpecializations.at(MethodSpecializationKeys.at(Selected));
				FMemberAccessInfo Access;
				if (Selected->bIsStatic || Selected->bDeleted
					|| !LookupMemberAccess(MethodReceiverType, Request.MethodName, true, Access, 0, Parameters.size(), Instance.Declaration)
					|| Access.EffectiveAccess != EMemberAccess::Public
					|| Runtime::MakeFunctionTypeName(CanonicalMethodParamTypeNames(*Selected, ResolveOwnerModulePath(GenericOwner), {}),
						QualifyTypeName(Selected->ReturnType, ResolveOwnerModulePath(GenericOwner))) != FunctionType)
				{
					ReportError("no public instance template method matches the binding signature", Call.Location);
					return false;
				}
				if (!EnsureMethodSpecialization(*Selected, GenericOwner, Call.Location)) return false;
				GenericMethod = Selected;
				GenericName = Request.MethodName;
				for (const auto& Name : TemplateParameterNames(*Instance.Declaration))
				{
					GenericArguments.push_back(Instance.Bindings.at(Name));
				}
				Selection.OwnerType = GenericOwner;
				Selection.MethodName = Request.MethodName;
				Selection.FunctionType = FunctionType;
				Selection.bPublic = true;
				Selection.bConst = Selected->bIsConst;
				Selection.LinkageName = MakeMethodFunctionName(GenericOwner, Selected->Name,
					CanonicalMethodParamTypeNames(*Selected, Instance.ModulePath, {}), Selected->bIsConst);
				Selection.FunctionIndex = FindFunctionIndex(Selection.LinkageName);
			}
			else if (!SelectBindableMethod(Request, Candidates, Selection, BindingError))
			{
				ReportError(BindingError, Call.Location);
				return false;
			}
		}
		MethodBindingCodegenInfo.push_back(std::move(Codegen));
		const std::string Suffix = std::to_string(NextLambdaId++);
		const std::string TargetName = "$bound_target" + Suffix;
		const std::string TokenName = "$bound_token" + Suffix;
		const std::string ReaderName = "$bound_reader" + Suffix;
		const std::string AttributeName = "$bound_attribute" + Suffix;
		const auto Local = [&](const std::string& Name, const std::string& Type, bool ReadOnly = false)
		{
			const auto Slot = AllocateLocal(Name);
			Scopes.back().LocalTypes[Name] = Type;
			if (ReadOnly)
			{
				Scopes.back().LocalConsts.insert(Name);
			}

			return Slot;
		};
		std::string TargetType = ReceiverType;
		if (bWeak)
		{
			TargetType = "weak<" + ReceiverType + ">";
			std::string Concrete;
			if (!EnsureStructTypeResolved(TargetType, Concrete, nullptr, Call.Location)) return false;
			TargetType = Concrete;
		}
		const auto TargetSlot = Local(TargetName, TargetType, bConst);
		if (!CompileExpression(*Call.GetArguments()[0]) ||
			!EmitValueCoercionForTarget(Emitter, TargetType, *Call.GetArguments()[0], Call.Location))
		{
			return false;
		}

		if (bWeak)
		{
			Emitter.Emit(EOpcode::CloneStruct);
		}

		Emitter.EmitU16Operand(EOpcode::StoreLocal, TargetSlot);
		const auto PushString = [&](const std::string& Value)
		{
			FBytecodeConstant Constant;
			Constant.Kind = EConstantKind::Str;
			Constant.bIsLinkageString = true;
			Constant.StringIndex = AddString(Value);
			Emitter.EmitU16Operand(EOpcode::ConstStr, AddConstant(Constant));
		};
		if (GenericMethod)
		{
			// A literal template binding is already closed. Dynamic lookup never
			// consults the compiler's incidental specialization cache.
			Emitter.Emit(EOpcode::ConstNull);
		}
		else
		{
			PushString(MethodReceiverType);
			PushString(FunctionType);
			Emitter.EmitU8Operand(EOpcode::ConstBool, bConst ? 1 : 0);
			if (!CompileExpression(*Call.GetArguments()[1]) ||
				!EmitValueCoercionForTarget(Emitter, "string", *Call.GetArguments()[1], Call.Location))
			{
				return false;
			}
	
			Emitter.EmitCallBuiltin(EBuiltinId::MethodBindingResolve, 4);
		}
		const auto TokenSlot = Local(TokenName, "__std_method_binding");
		Emitter.EmitU16Operand(EOpcode::StoreLocal, TokenSlot);
		if (!bWeak)
		{
			// Check a strong receiver at binding time, without running its method.
			Emitter.EmitU16Operand(EOpcode::LoadLocal, TargetSlot);
			Emitter.Emit(EOpcode::ConstNull);
			Emitter.Emit(EOpcode::Ne);
			Emitter.EmitCallBuiltin(EBuiltinId::Assert, 1);
		}
		std::vector<std::string> ParameterTypes;
		std::string ReturnType;
		Runtime::ParseFunctionType(FunctionType, ParameterTypes, ReturnType);
		FLambdaExpr Lambda;
		Lambda.Location = Call.Location;
		Lambda.CaptureKind = ELambdaCaptureKind::Explicit;
		Lambda.bMutable = true;
		Lambda.Captures.push_back({TokenName, false, nullptr, Call.Location});
		Lambda.Captures.push_back({TargetName, false, nullptr, Call.Location});
		Lambda.ReturnType = bWeak ? "optional<" + ReturnType + ">" : ReturnType;
		Lambda.Body = Located<FBlockStmt>(Call.Location);
		const auto Identifier = [&](const std::string& Name)
		{
			return Located<FIdentifierExpr>(Call.Location, Name);
		};
		std::string ActiveTarget = TargetName;
		if (bWeak)
		{
			if (!GenericMethod)
			{
				std::vector<std::unique_ptr<FExpr>> Check;
			Check.push_back(Identifier(TokenName));
			auto Validate = Located<FCallExpr>(Call.Location, "__method_binding_validate", std::move(Check));
			Validate->bImplementationBuiltinAccess = true;
			Lambda.Body->Statements.push_back(Located<FExprStmt>(Call.Location, std::move(Validate)));
			}
			ActiveTarget = "$locked";
			Lambda.Body->Statements.push_back(Located<FVarDeclStmt>(Call.Location, ReceiverType, ActiveTarget,
				Located<FMethodCallExpr>(Call.Location, Identifier(TargetName), "lock", std::vector<std::unique_ptr<FExpr>>{})));
			auto Empty = Located<FBlockStmt>(Call.Location);
			Empty->Statements.push_back(Located<FReturnStmt>(Call.Location,
				Located<FCallExpr>(Call.Location, Lambda.ReturnType, std::vector<std::unique_ptr<FExpr>>{})));
			Lambda.Body->Statements.push_back(Located<FIfStmt>(Call.Location,
				Located<FBinaryExpr>(Call.Location, EBinaryOp::Equal, Identifier(ActiveTarget),
					Located<FNullptrLiteralExpr>(Call.Location)), std::move(Empty), nullptr));
		}
		std::vector<std::unique_ptr<FExpr>> Arguments;
		Arguments.push_back(Identifier(TokenName));
		std::unique_ptr<FExpr> Receiver = Identifier(ActiveTarget);
		if (bWrapped)
		{
			Receiver = Located<FMemberAccessExpr>(Call.Location, std::move(Receiver), "value");
			Receiver = Located<FRefExpr>(Call.Location, std::move(Receiver));
		}
		Arguments.push_back(std::move(Receiver));
		for (std::size_t Index = 0; Index < ParameterTypes.size(); ++Index)
		{
			FFunctionParam Parameter;
			Parameter.Name = "$arg" + std::to_string(Index);
			Parameter.TypeName = std::string(Runtime::StripReferenceTypeName(ParameterTypes[Index]));
			Parameter.bIsRef = Runtime::IsReferenceTypeName(ParameterTypes[Index]);
			Parameter.bIsConst = ParameterTypes[Index].rfind("const ref ", 0) == 0;
			std::unique_ptr<FExpr> Argument = Identifier(Parameter.Name);
			if (Parameter.bIsRef)
			{
				Argument = Located<FRefExpr>(Call.Location, std::move(Argument));
			}

			Arguments.push_back(std::move(Argument));
			Lambda.Parameters.push_back(std::move(Parameter));
		}
		std::unique_ptr<FExpr> Value;
		if (GenericMethod)
		{
			auto GenericReceiver = std::move(Arguments[1]);
			if (GenericReceiver->Kind == EExprKind::Ref)
			{
				GenericReceiver = std::move(static_cast<FRefExpr&>(*GenericReceiver).Operand);
			}
			Arguments.erase(Arguments.begin(), Arguments.begin() + 2);
			auto Invoke = Located<FMethodCallExpr>(Call.Location, std::move(GenericReceiver), GenericName, std::move(Arguments));
			Invoke->TypeArgs = GenericArguments;
			Invoke->bExplicitTemplateArguments = true;
			Value = std::move(Invoke);
		}
		else
		{
			auto Invoke = Located<FCallExpr>(Call.Location, "__method_binding_invoke", std::move(Arguments));
			Invoke->bImplementationBuiltinAccess = true;
			Invoke->TypeArgs = {FunctionType};
			Value = std::move(Invoke);
		}
		if (bWeak)
		{
			std::vector<std::unique_ptr<FExpr>> Values;
			Values.push_back(std::move(Value));
			Value = Located<FCallExpr>(Call.Location, Lambda.ReturnType, std::move(Values));
		}
		Lambda.Body->Statements.push_back(Located<FReturnStmt>(Call.Location, std::move(Value)));
		const std::string PreviousExpected = ExpressionExpectedType;
		ExpressionExpectedType = bWeak ? Runtime::MakeFunctionTypeName({}, Lambda.ReturnType) : FunctionType;
		const bool bCompiled = CompileLambdaExpression(Lambda, Emitter, true);
		ExpressionExpectedType = PreviousExpected;
		if (!bCompiled) return false;
		if (bAttribute)
		{
			const std::string ReaderType =
				bWeak ? Runtime::MakeFunctionTypeName({}, Lambda.ReturnType) : FunctionType;
			const auto ReaderSlot = Local(ReaderName, ReaderType);
			Emitter.EmitU16Operand(EOpcode::StoreLocal, ReaderSlot);
			FCallExpr Construct(ResultType, {});
			Construct.Location = Call.Location;
			if (!CompileExpression(Construct)) return false;
			const auto AttributeSlot = Local(AttributeName, ResultType);
			Emitter.EmitU16Operand(EOpcode::StoreLocal, AttributeSlot);
			std::vector<std::unique_ptr<FExpr>> Readers;
			Readers.push_back(Identifier(ReaderName));
			FMethodCallExpr Bind(Identifier(AttributeName), bWeak ? "bind_optional" : "bind", std::move(Readers));
			Bind.Location = Call.Location;
			if (!CompileExpression(Bind)) return false;
			Emitter.Emit(EOpcode::Pop);
			Emitter.EmitU16Operand(EOpcode::LoadLocal, AttributeSlot);
			for (const auto Slot : {ReaderSlot, AttributeSlot})
			{
				Emitter.Emit(EOpcode::ConstNull);
				Emitter.EmitU16Operand(EOpcode::StoreLocal, Slot);
			}
		}
		for (const auto Slot : {TargetSlot, TokenSlot})
		{
			Emitter.Emit(EOpcode::ConstNull);
			Emitter.EmitU16Operand(EOpcode::StoreLocal, Slot);
		}
		return true;
	}

	bool VeroScriptCompiler::CompileBoundMethodInvoke(const FCallExpr& Call, FEmitter& Emitter)
	{
		if (!Call.bImplementationBuiltinAccess || Call.TypeArgs.size() != 1)
		{
			ReportError("method binding invocation is a compiler implementation service", Call.Location);
			return false;
		}
		std::vector<std::string> Parameters;
		std::string Return;
		if (!Runtime::ParseFunctionType(Call.TypeArgs[0], Parameters, Return) ||
			Call.GetArguments().size() != Parameters.size() + 2 || Parameters.size() > 253)
		{
			ReportError(
				"invalid bound method signature or argument count (maximum 253 parameters)", Call.Location);
			return false;
		}
		for (std::size_t Index = 0; Index < Call.GetArguments().size(); ++Index)
		{
			const auto& Argument = *Call.GetArguments()[Index];
			if (Index == 1 && Argument.Kind == EExprKind::Ref)
			{
				if (!CompileRefArgument(Emitter, UnwrapRefOperand(Argument), Call.Location, false))
				{
					return false;
				}
			}
			else if (Index >= 2 && Runtime::IsReferenceTypeName(Parameters[Index - 2]))
			{
				if (!CompileRefArgument(Emitter, UnwrapRefOperand(Argument), Call.Location,
						Parameters[Index - 2].rfind("const ref ", 0) != 0))
				{
					return false;
				}

			}
			else if (!CompileExpression(Argument))
			{
				return false;
			}
		}
		EmitClonePassByValueArgs(Emitter, Parameters, static_cast<std::uint8_t>(Parameters.size()));
		Emitter.EmitCallBuiltin(
			EBuiltinId::MethodBindingInvoke, static_cast<std::uint8_t>(Call.GetArguments().size()));
		return true;
	}
}
