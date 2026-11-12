// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

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

bool EmitAttributeFactory(const FCallExpr& Call)
{
	if (IsWeakAttributeFactoryCall(Call))
	{
		if (Call.TypeArgs.empty() || Call.GetArguments().size() != 2)
		{
			Fail("AOT: invalid weak attribute factory");
			return true;
		}
		Out << "VsWeakAttribute<" << MapType(Call.TypeArgs[0]) << ">(";
		std::vector<std::string> StrongTypes;
		if (!TryParseSoftBuiltinType(InferExprType(*Call.GetArguments()[0]), "weak", StrongTypes) ||
			StrongTypes.size() != 1)
		{
			Fail("AOT: invalid weak attribute target");
			return true;
		}
		std::vector<std::string> Params = {
			InferExprType(*Call.GetArguments()[0]), "fn(" + StrongTypes[0] + ")->" + Call.TypeArgs[0]};
		EmitResolvedCallArgs(Call.GetArguments(), &Params);
		Out << ')';
		return true;
	}
	if (IsMethodBindingFactoryCall(Call))
	{
		const auto* Info = FindMethodBindingCodegenInfo(MethodBindings, Call.Location);
		if (!Info || !Info->bConstantName || Info->Selection.bHost)
		{
			Fail(
				"AOT: method binding requires one constant script method selection; dynamic/host bindings require the VM");
			return true;
		}
		const bool Weak = Info->FactoryName == "__std_make_weak_attribute_by_name";
		const bool Attribute = Info->FactoryName != "__std_bind_method";
		std::string Function =
			Attribute ? "fn()->" + ApplyTypeSubst(Call.TypeArgs[0]) : ApplyTypeSubst(Call.TypeArgs[0]);
		std::vector<std::string> Parameters;
		std::string Result;
		if (!Runtime::ParseFunctionType(Function, Parameters, Result))
		{
			Fail("AOT: invalid method binding signature");
			return true;
		}
		bNeedsMemory = bNeedsFunctional = bNeedsUtility = true;
		std::string TargetType = Call.TypeArgs.size() == 2 ? ApplyTypeSubst(Call.TypeArgs[1])
														   : InferExprType(*Call.GetArguments()[0]);
		if (Weak && Call.TypeArgs.size() != 2)
		{
			std::vector<std::string> Types;
			if (!TryParseSoftBuiltinType(TargetType, "weak", Types) || Types.size() != 1)
			{
				Fail("AOT: invalid weak method binding target");
				return true;
			}
			TargetType = Types[0];
		}
		std::string MethodType = TargetType;
		if (Info->bWrappedReceiver)
		{
			std::vector<std::string> Types;
			if (!TryParseSoftBuiltinType(TargetType, "handle", Types) || Types.size() != 1)
			{
				Fail("AOT: invalid wrapped method binding target");
				return true;
			}
			MethodType = Types[0];
		}
		std::string ResolvedMethodType;
		const auto* Owner = ResolveUserTypeForLookup(MethodType);
		if (!Owner)
		{
			Fail("AOT: bound receiver type cannot be represented");
			return true;
		}
		ResolvedMethodType = Owner->Name;
		const bool MemberLayout = TypeNeedsOopLayout(*Owner);
		Out << "([&]() { " << MapType(Weak ? "weak<" + TargetType + ">" : TargetType)
			<< " __vs_bound_target = (";
		EmitCoercedValue(Weak ? "weak<" + TargetType + ">" : TargetType, *Call.GetArguments()[0]);
		Out << "); ";
		if (!Weak)
		{
			Out << "if (!__vs_bound_target) VsScriptFail(\"null method binding target\"); ";
		}

		if (Attribute)
		{
			Out << (Weak ? "return VsAttributeFromOptionalGetter<" : "return VsAttributeFromGetter<")
				<< MapType(Result) << ">(";
		}

		else
		{
			Out << "return " << MapType(Function) << '(';
		}

		Out << "[__vs_bound_target](";
		for (std::size_t I = 0; I < Parameters.size(); ++I)
		{
			if (I)
			{
				Out << ", ";
			}
			Out << MapType(Parameters[I]) << " __vs_arg" << I;
		}
		Out << ") mutable -> " << (Weak ? "vs_Optional<" + MapType(Result) + ">" : MapType(Result)) << " { ";
		if (Weak)
		{
			Out << "auto __vs_locked=__vs_bound_target.lock(); if (!__vs_locked) return {}; ";
		}

		Out << "return ";
		if (Weak)
		{
			Out << "vs_Optional<" << MapType(Result) << ">(";
		}

		if (!MemberLayout)
		{
			Out << MangleMethod(ResolvedMethodType, Info->Selection.MethodName) << '(';
		}

		if (Info->bConstReceiver)
		{
			Out << "std::as_const(";
		}

		Out << "VsRequireObject(" << (Weak ? "__vs_locked" : "__vs_bound_target") << ")";
		if (Info->bWrappedReceiver)
		{
			Out << ".value";
		}

		if (Info->bConstReceiver)
		{
			Out << ')';
		}

		if (MemberLayout)
		{
			Out << '.' << SanitizeSymbolName(Info->Selection.MethodName) << '(';
		}

		for (std::size_t I = 0; I < Parameters.size(); ++I)
		{
			if (I || !MemberLayout)
			{
				Out << ", ";
			}
			Out << "__vs_arg" << I;
		}
		Out << ')';
		if (Weak)
		{
			Out << ')';
		}

		Out << "; }); }())";
		return true;
	}
	return false;
}
