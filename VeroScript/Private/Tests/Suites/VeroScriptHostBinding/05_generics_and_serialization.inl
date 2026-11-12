// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Internal fragment: VeroScriptHostBindingTests / generics and serialization suite. Included by the owning translation unit.

	{
		FHostBindingRegistry InvokeRegistry;
		struct FStoredCallable
		{
			FFunctionHandle Value;
		} StoredCallable;
		std::string InvokeError;
		if (!InvokeRegistry.RegisterNative(
				"invoke_callable",
				FNativeTypeDesc::Scalar(ENativeType::Int),
				{
					{FNativeTypeDesc::Function("fn(int)->int"), 0},
					{FNativeTypeDesc::Scalar(ENativeType::Int), 0},
				},
				[](const FNativeCallContext& Context, FValue& Out, std::string& Error)
				{
					if (!Context.ScriptInvoker || Context.ArgCount != 2)
					{
						Error = "missing active script invoker";
						return false;
					}
					const auto* Callable = std::get_if<FFunctionHandle>(&Context.Args[0]);
					if (!Callable || !*Callable)
					{
						Error = "missing callable";
						return false;
					}
					std::vector<FValue> Args{Context.Args[1]};
					return Context.ScriptInvoker->InvokeCallable(
								*Callable, Args, Out, Error);
				},
				nullptr,
				InvokeError)
			|| !InvokeRegistry.RegisterNative(
				"identity_callable",
				FNativeTypeDesc::Function("fn(int)->int"),
				{{FNativeTypeDesc::Function("fn(int)->int"), 0}},
				[](const FNativeCallContext& Context, FValue& Out, std::string& Error)
				{
					if (Context.ArgCount != 1
								|| !std::holds_alternative<FFunctionHandle>(Context.Args[0]))
					{
						Error = "expected typed callable";
						return false;
					}
					Out = Context.Args[0];
					return true;
				},
				nullptr,
				InvokeError)
			|| !InvokeRegistry.RegisterNative(
				"invoke_named",
				FNativeTypeDesc::Scalar(ENativeType::Int),
				{{FNativeTypeDesc::Scalar(ENativeType::Int), 0}},
				[](const FNativeCallContext& Context, FValue& Out, std::string& Error)
				{
					if (!Context.ScriptInvoker || Context.ArgCount != 1)
					{
						Error = "missing active script invoker";
						return false;
					}
					std::vector<FValue> Args{Context.Args[0]};
					return Context.ScriptInvoker->InvokeFunction(
								"add_ten", Args, Out, Error);
				},
				nullptr,
				InvokeError)
			|| !InvokeRegistry.RegisterNative(
				"invoke_ref",
				FNativeTypeDesc::Scalar(ENativeType::Int),
				{
					{FNativeTypeDesc::Function("fn(ref int)->void"), 0},
					{FNativeTypeDesc::Scalar(ENativeType::Int), 0},
				},
				[](const FNativeCallContext& Context, FValue& Out, std::string& Error)
				{
					if (!Context.ScriptInvoker || Context.ArgCount != 2)
					{
						Error = "missing active script invoker";
						return false;
					}
					const auto* Callable = std::get_if<FFunctionHandle>(&Context.Args[0]);
					if (!Callable || !*Callable)
					{
						Error = "missing ref callable";
						return false;
					}
					std::vector<FValue> Args{Context.Args[1]};
					FValue Ignored = std::int32_t{0};
					if (!Context.ScriptInvoker->InvokeCallable(
									*Callable, Args, Ignored, Error))
					{
						return false;
					}
					Out = Args[0];
					return true;
				},
				nullptr,
				InvokeError))
		{
			std::cerr << InvokeError << '\n';
			return Fail("register typed script invocation natives");
		}
		std::string InvokeCatalogJson;
		FHostBindingRegistry ImportedInvokeRegistry;
		std::string InvokeCatalogError;
		if (!ExportBindingCatalogJson(InvokeRegistry, InvokeCatalogJson)
			|| !ImportBindingCatalogJson(
				InvokeCatalogJson, ImportedInvokeRegistry, InvokeCatalogError))
		{
			std::cerr << InvokeCatalogError << '\n';
			return Fail("function native metadata catalog roundtrip");
		}
		const FHostFunctionDesc* ImportedIdentity = nullptr;
		for (const FHostFunctionDesc& Function : ImportedInvokeRegistry.GetFunctions())
		{
			if (Function.Name == "identity_callable")
			{
				ImportedIdentity = &Function;
				break;
			}
		}
		if (!ImportedIdentity
			|| ImportedIdentity->ReturnType != ENativeType::Function
			|| ImportedIdentity->ReturnHostTypeName != "fn(int)->int"
			|| ImportedIdentity->ParamTypes.size() != 1
			|| ImportedIdentity->ParamTypes[0] != ENativeType::Function
			|| ImportedIdentity->ParamHostTypeNames.size() != 1
			|| ImportedIdentity->ParamHostTypeNames[0] != "fn(int)->int")
		{
			return Fail("function native signature must survive catalog roundtrip");
		}
		if (!InvokeRegistry.RegisterNative(
				"consume_callables",
				FNativeTypeDesc::Scalar(ENativeType::Void),
				{{FNativeTypeDesc::ArrayOf(
					FNativeTypeDesc::Function("fn(int)->int")),
					0}},
				[](const FNativeCallContext& Context, FValue&, std::string& Error)
				{
					if (Context.ArgCount != 1
								|| !std::holds_alternative<FDynamicArrayHandle>(Context.Args[0]))
					{
						Error = "expected callable array";
						return false;
					}
					return true;
				},
				nullptr,
				InvokeError))
		{
			std::cerr << InvokeError << '\n';
			return Fail("register structured callable container native");
		}
		if (!InvokeRegistry.RegisterNative(
				"reject_bad_capture",
				FNativeTypeDesc::Scalar(ENativeType::Bool),
				{{FNativeTypeDesc::Function("fn(int)->int"), 0}},
				[](const FNativeCallContext& Context, FValue& Out, std::string& Error)
				{
					if (!Context.ScriptInvoker || Context.ArgCount != 1)
					{
						Error = "missing active script invoker";
						return false;
					}
					const auto* const Callable =
								std::get_if<FFunctionHandle>(&Context.Args[0]);
					if (!Callable || !*Callable || (*Callable)->Captures.empty())
					{
						Error = "expected a captured callable";
						return false;
					}
					auto Forged = std::make_shared<FFunctionValue>(**Callable);
					Forged->Captures[0] = std::string("wrong capture type");
					std::vector<FValue> Args{std::int32_t{1}};
					FValue Ignored = std::int32_t{0};
					std::string InvokeError;
					const bool bInvoked = Context.ScriptInvoker->InvokeCallable(
								Forged, Args, Ignored, InvokeError);
					if (bInvoked
								|| InvokeError.find("capture type mismatch") == std::string::npos)
					{
						Error = "malformed capture was not rejected at the ABI boundary";
						return false;
					}
					Out = true;
					return true;
				},
				nullptr,
				InvokeError)
			|| !InvokeRegistry.RegisterNative(
				"reject_bad_named_return",
				FNativeTypeDesc::Scalar(ENativeType::Bool),
				{},
				[](const FNativeCallContext& Context, FValue& Out, std::string& Error)
				{
					if (!Context.ScriptInvoker)
					{
						Error = "missing active script invoker";
						return false;
					}
					std::vector<FValue> Args;
					FValue ReturnValue = std::int32_t{0};
					std::string InvokeError;
					const bool bInvoked = Context.ScriptInvoker->InvokeFunction(
								"bad_return", Args, ReturnValue, InvokeError);
					if (bInvoked
								|| InvokeError.find("returned a value incompatible")
									== std::string::npos)
					{
						Error = "malformed return was not rejected at the ABI boundary";
						return false;
					}
					Out = true;
					return true;
				},
				nullptr,
				InvokeError))
		{
			std::cerr << InvokeError << '\n';
			return Fail("register script invocation boundary-validation natives");
		}
		if (!InvokeRegistry.RegisterNative(
				"reentrant_ref_write",
				FNativeTypeDesc::Scalar(ENativeType::Void),
				{
					{FNativeTypeDesc::Scalar(ENativeType::Int), NativeParamFlagRef},
					{FNativeTypeDesc::Function("fn()->void"), 0},
				},
				[](const FNativeCallContext& Context, FValue&, std::string& Error)
				{
					if (!Context.ScriptInvoker || Context.ArgCount != 2
								|| !Context.MutableArg(0))
					{
						Error = "missing reentrant mutable-ref context";
						return false;
					}
					const auto* const Callable =
								std::get_if<FFunctionHandle>(&Context.Args[1]);
					if (!Callable || !*Callable)
					{
						Error = "missing reentrant callback";
						return false;
					}
					std::vector<FValue> Args;
					FValue Ignored = std::int32_t{0};
					if (!Context.ScriptInvoker->InvokeCallable(
									*Callable, Args, Ignored, Error))
					{
						return false;
					}
					*Context.MutableArg(0) = std::int32_t{42};
					return true;
				},
				nullptr,
				InvokeError))
		{
			std::cerr << InvokeError << '\n';
			return Fail("register reentrant mutable-ref native");
		}
		if (!InvokeRegistry.RegisterNative(
				"store_callable",
				FNativeTypeDesc::Scalar(ENativeType::Void),
				{{FNativeTypeDesc::Function("fn(int)->int"), 0}},
				[](const FNativeCallContext& Context, FValue&, std::string& Error)
				{
					auto* const Stored = static_cast<FStoredCallable*>(Context.UserData);
					const auto* const Callable = Context.ArgCount == 1
								? std::get_if<FFunctionHandle>(&Context.Args[0])
						: nullptr;
					if (!Stored || !Callable || !*Callable)
					{
						Error = "missing callable to store";
						return false;
					}
					Stored->Value = *Callable;
					return true;
				},
				&StoredCallable,
				InvokeError)
			|| !InvokeRegistry.RegisterNative(
				"invoke_stored_callable",
				FNativeTypeDesc::Scalar(ENativeType::Int),
				{},
				[](const FNativeCallContext& Context, FValue& Out, std::string& Error)
				{
					auto* const Stored = static_cast<FStoredCallable*>(Context.UserData);
					if (!Stored || !Stored->Value || !Context.ScriptInvoker)
					{
						Error = "missing stored callable or active invoker";
						return false;
					}
					std::vector<FValue> Args{std::int32_t{7}};
					return Context.ScriptInvoker->InvokeCallable(
								Stored->Value, Args, Out, Error);
				},
				&StoredCallable,
				InvokeError))
		{
			std::cerr << InvokeError << '\n';
			return Fail("register stored callable generation natives");
		}

		FCompileOptions InvokeOptions = VeroScript::Test::MakeCompileOptions();
		InvokeOptions.Bindings = &InvokeRegistry;
		FCompileResult Compile = VeroScriptCompile::CompileSource(
			"int saved_target(int x) { return x * 3; }\n"
			"int add_ten(int x) { return x + 10; }\n"
			"int bad_return() { return 7; }\n"
			"void add_five(ref int x) { x = x + 5; }\n"
			"void main() {\n"
			"  function(int)->int twice = function(int x)->int => x * 2;\n"
			"  function(int)->int saved = saved_target;\n"
			"  store_callable(saved);\n"
			"  function(ref int)->void mutate = add_five;\n"
			"  int capturedBase = 8;\n"
			"  function(int)->int captured = function(int x)->int => capturedBase + x;\n"
			"  array<function(int)->int> callables = array<function(int)->int>();\n"
			"  callables.push(twice);\n"
			"  array<int> values = {1};\n"
			"  function()->void grow = function() mutable -> void {\n"
			"    for (int i = 0; i < 128; i = i + 1) { values.push(i); }\n"
			"  };\n"
			"  consume_callables(callables);\n"
			"  print(invoke_callable(twice, 21));\n"
			"  print(invoke_callable(identity_callable(twice), 21));\n"
			"  print(invoke_named(5));\n"
			"  print(invoke_ref(mutate, 7));\n"
			"  print(invoke_callable(captured, 2));\n"
			"  print(reject_bad_capture(captured));\n"
			"  print(reject_bad_named_return());\n"
			"  reentrant_ref_write(ref values[0], grow);\n"
			"  print(values[0]);\n"
			"}\n",
			"<nested_host_script_invocation>",
			InvokeOptions);
		if (!Compile.bSuccess || !Compile.Module)
		{
			std::cerr << Compile.Error.Message << '\n';
			return Fail("typed nested host script invocation should compile");
		}
		bool bCorruptedReturnMetadata = false;
		for (FBytecodeFunction& Function : Compile.Module->Functions)
		{
			if (Function.Name.find("bad_return#fn#0") != std::string::npos)
			{
				Function.ClosureTypeName = "fn()->string";
				bCorruptedReturnMetadata = true;
				break;
			}
		}
		if (!bCorruptedReturnMetadata)
		{
			return Fail("locate nested-invocation return metadata test function");
		}
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		if (!Vm.Execute(
				*Compile.Module,
				ERunMode::Script,
				{},
				&InvokeRegistry.GetNativeFunctions())
			|| Output.str() != "42\n42\n15\n12\n10\ntrue\ntrue\n42\n")
		{
			std::cerr << Vm.GetError().Message << " output=" << Output.str();
			return Fail("nested host script invocation should preserve return/ref values");
		}
		if (!StoredCallable.Value)
		{
			return Fail("host should retain the first-generation callable");
		}
		const FCompileResult ReusedIdentityCompile = VeroScriptCompile::CompileSource(
			"int saved_target(int x) { return x * 30; }\n"
			"void main() { print(invoke_stored_callable()); }\n",
			"<nested_host_script_invocation>",
			InvokeOptions);
		if (!ReusedIdentityCompile.bSuccess || !ReusedIdentityCompile.Module)
		{
			return Fail("stale callable generation regression script should compile");
		}
		if (StoredCallable.Value->FunctionIndex
				>= ReusedIdentityCompile.Module->Functions.size()
			|| ReusedIdentityCompile.Module->Functions[
				StoredCallable.Value->FunctionIndex].Name
				!= StoredCallable.Value->FunctionName
			|| ReusedIdentityCompile.Module->Functions[
				StoredCallable.Value->FunctionIndex].ClosureTypeName
				!= StoredCallable.Value->TypeName)
		{
			return Fail("stale callable test requires a reused index/name/signature identity");
		}
		std::ostringstream StaleOutput;
		VeroScriptVm NewGenerationVm(StaleOutput);
		VeroScript::Test::Configure(NewGenerationVm);
		NewGenerationVm.SetProgramGeneration(2);
		if (NewGenerationVm.Execute(
				*ReusedIdentityCompile.Module,
				ERunMode::Script,
				{},
				&InvokeRegistry.GetNativeFunctions())
			|| NewGenerationVm.GetError().Message.find("stale") == std::string::npos)
		{
			std::cerr << NewGenerationVm.GetError().Message
				<< " output=" << StaleOutput.str();
			return Fail("callable handles must not cross program generations");
		}

		const FCompileResult FailingCompile = VeroScriptCompile::CompileSource(
			"int explode(int x) { assert(false); return x; }\n"
			"void main() {\n"
			"  function(int)->int callback = explode;\n"
			"  print(invoke_callable(callback, 1));\n"
			"}\n",
			"<nested_host_script_error>",
			InvokeOptions);
		if (!FailingCompile.bSuccess || !FailingCompile.Module)
		{
			return Fail("nested invocation error propagation script should compile");
		}
		std::ostringstream FailingOutput;
		VeroScriptVm FailingVm(FailingOutput);
		VeroScript::Test::Configure(FailingVm);
		if (FailingVm.Execute(
				*FailingCompile.Module,
				ERunMode::Script,
				{},
				&InvokeRegistry.GetNativeFunctions()))
		{
			return Fail("nested script runtime error must fail the outer native call");
		}
	}

	// A callback may extend the host registry. That can reallocate the native
	// table, so post-callback validation must only read metadata snapshots.
	{
		FHostBindingRegistry GrowingRegistry;
		std::string GrowthError;
		if (!GrowingRegistry.RegisterNative(
				"grow_binding_table",
				FNativeTypeDesc::ArrayOf(
					FNativeTypeDesc::Scalar(ENativeType::Int)),
				{},
				[](const FNativeCallContext& Context, FValue& Out, std::string& Error)
				{
					auto* const Registry =
								static_cast<FHostBindingRegistry*>(Context.UserData);
					if (!Registry)
					{
						Error = "missing registry";
						return false;
					}
					for (int Index = 0; Index < 64; ++Index)
					{
						std::string AddError;
						if (!Registry->RegisterNative(
										"late_native_" + std::to_string(Index),
										FNativeTypeDesc::Scalar(ENativeType::Void),
								{},
										[](const FNativeCallContext&, FValue&, std::string&)
										{
											return true;
										},
										nullptr,
										AddError))
						{
							Error = AddError;
							return false;
						}
					}
					auto Result = std::make_shared<FDynamicArray>();
					Result->ElementTypeName = "int";
					Result->TypeName = "array<int>";
					Result->Elements.push_back(std::int32_t{7});
					Out = std::move(Result);
					return true;
				},
				&GrowingRegistry,
				GrowthError))
		{
			return Fail("register native-table growth regression callback");
		}
		FCompileOptions GrowthOptions = VeroScript::Test::MakeCompileOptions();
		GrowthOptions.Bindings = &GrowingRegistry;
		const FCompileResult Compile = VeroScriptCompile::CompileSource(
			"void main() { print(len(grow_binding_table())); }\n",
			"<native_table_growth>",
			GrowthOptions);
		if (!Compile.bSuccess || !Compile.Module)
		{
			return Fail("native-table growth regression script should compile");
		}
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		if (!Vm.Execute(
				*Compile.Module,
				ERunMode::Script,
				{},
				&GrowingRegistry.GetNativeFunctions())
			|| Output.str() != "1\n")
		{
			std::cerr << Vm.GetError().Message << " output=" << Output.str();
			return Fail("native-table growth must not invalidate active call metadata");
		}
	}

	{
		FHostBindingRegistry RefCtorRegistry;
		FHostTypeId RefCtorId;
		std::string RefCtorError;
		std::int32_t ValueCtorCalls = 0;
		std::int32_t RefCtorCalls = 0;
		const auto RegisterCtor = [&RefCtorError](
			FHostBindingRegistry& Registry,
			const FHostTypeId TypeId,
			const std::uint8_t Flag,
			std::int32_t* const Counter)
		{
			FHostMemberDesc Ctor;
			Ctor.Kind = EHostMemberKind::Constructor;
			Ctor.ParamTypes = {ENativeType::Int};
			if (Flag != 0)
			{
				Ctor.ParamFlags = {Flag};
			}
			Ctor.Callback = [](
				const FNativeCallContext& Context,
				FValue& OutReturn,
				std::string&)
			{
				++*static_cast<std::int32_t*>(Context.UserData);
				OutReturn = MakeHostObject(
					0,
					{},
					new std::int32_t(1),
					[](void* Payload)
					{
						delete static_cast<std::int32_t*>(Payload);
					});
				return true;
			};
			Ctor.UserData = Counter;
			return Registry.RegisterMember(TypeId, std::move(Ctor), RefCtorError);
		};
		if (!RefCtorRegistry.RegisterType(
				"RefCtor", EHostTypeKind::Struct, RefCtorId, RefCtorError)
			|| !RegisterCtor(RefCtorRegistry, RefCtorId, 0, &ValueCtorCalls)
			|| !RegisterCtor(
				RefCtorRegistry, RefCtorId, NativeParamFlagRef, &RefCtorCalls))
		{
			std::cerr << RefCtorError << '\n';
			return Fail("value/ref host constructor overloads should register");
		}
		const FHostTypeDesc* RefCtorType = RefCtorRegistry.FindType(RefCtorId);
		std::string ValueNativeName;
		std::string RefNativeName;
		if (RefCtorType)
		{
			for (const FHostMemberDesc& Member : RefCtorType->Members)
			{
				if (Member.Kind != EHostMemberKind::Constructor)
				{
					continue;
				}
				if (Member.ParamFlags.empty())
				{
					ValueNativeName = Member.NativeName;
				}
				else
				{
					RefNativeName = Member.NativeName;
				}
			}
		}
		if (ValueNativeName.empty() || RefNativeName.empty()
			|| ValueNativeName == RefNativeName)
		{
			return Fail("constructor native identity must include ref qualifiers");
		}

		FHostBindingRegistry ReorderedRegistry;
		FHostTypeId ReorderedId;
		if (!ReorderedRegistry.RegisterType(
				"RefCtor", EHostTypeKind::Struct, ReorderedId, RefCtorError)
			|| !RegisterCtor(
				ReorderedRegistry, ReorderedId, NativeParamFlagRef, &RefCtorCalls)
			|| !RegisterCtor(ReorderedRegistry, ReorderedId, 0, &ValueCtorCalls))
		{
			return Fail("reordered constructor overloads should register");
		}
		const FHostTypeDesc* ReorderedType = ReorderedRegistry.FindType(ReorderedId);
		std::string ReorderedValueName;
		std::string ReorderedRefName;
		if (ReorderedType)
		{
			for (const FHostMemberDesc& Member : ReorderedType->Members)
			{
				if (Member.Kind != EHostMemberKind::Constructor)
				{
					continue;
				}
				if (Member.ParamFlags.empty())
				{
					ReorderedValueName = Member.NativeName;
				}
				else
				{
					ReorderedRefName = Member.NativeName;
				}
			}
		}
		if (ValueNativeName != ReorderedValueName || RefNativeName != ReorderedRefName)
		{
			return Fail("host overload native identity must not depend on registration order");
		}

		FCompileOptions RefCtorOptions = VeroScript::Test::MakeCompileOptions();
		RefCtorOptions.Bindings = &RefCtorRegistry;
		const FCompileResult RefCtorCompile = VeroScriptCompile::CompileSource(
			"void main() { int value = 7; RefCtor a(value); RefCtor b(ref value); }\n",
			"<host_ref_constructor>",
			RefCtorOptions);
		if (!RefCtorCompile.bSuccess || !RefCtorCompile.Module)
		{
			std::cerr << RefCtorCompile.Error.Message << '\n';
			return Fail("value/ref host constructor overloads should resolve");
		}
		VeroScriptVm RefCtorVm(std::cout);
		VeroScript::Test::Configure(RefCtorVm);
		if (!RefCtorVm.Execute(
				*RefCtorCompile.Module,
				ERunMode::Script,
				{},
				&RefCtorRegistry.GetNativeFunctions())
			|| ValueCtorCalls != 1 || RefCtorCalls != 1)
		{
			std::cerr << RefCtorVm.GetError().Message << '\n';
			return Fail("value/ref host constructor overloads should dispatch distinctly");
		}
	}

	{
		FHostBindingRegistry GenericRegistry;
		FGenericHostTypeTemplateId TemplateId;
		FHostTypeId BoxIntId;
		std::string GenericError;
		if (!GenericRegistry.RegisterGenericHostTypeTemplate(
				"Box",
				1,
				InstantiateGenericBox,
				nullptr,
				TemplateId,
				GenericError,
				"A real generic host value template")
			|| !GenericRegistry.InstantiateGenericHostType(
				TemplateId, {"int"}, BoxIntId, GenericError))
		{
			std::cerr << GenericError << '\n';
			return Fail("generic host value template should instantiate transactionally");
		}
		const FHostTypeDesc* BoxInt = GenericRegistry.FindType(BoxIntId);
		if (!BoxInt || BoxInt->Name != "Box<int>"
			|| BoxInt->GenericTemplateId != TemplateId
			|| BoxInt->GenericTypeArguments != std::vector<std::string>{"int"} || BoxInt->Annotations.size() != 1
			|| BoxInt->Members.size() != 2
			|| BoxInt->Members.front().ParamAnnotations.size() != 1
			|| BoxInt->Members.back().Annotations.size() != 1)
		{
			return Fail("generic host concrete type must preserve template identity and arguments");
		}
		FGenericHostTypeTemplateId CanonicalTemplateId;
		FHostTypeId CanonicalAliasId;
		FHostTypeId CanonicalSpellingId;
		std::int32_t CanonicalInstantiateCalls = 0;
		if (!GenericRegistry.RegisterGenericHostTypeTemplate(
				"CanonicalBox",
				1,
				[](const std::vector<std::string>& Arguments,
					FHostTypeDesc& OutType,
					std::string& OutError,
					void* UserData)
				{
					if (Arguments != std::vector<std::string>{"unsigned int"})
					{
						OutError = "generic argument was not canonicalized";
						return false;
					}
					++*static_cast<std::int32_t*>(UserData);
					OutType.Kind = EHostTypeKind::Struct;
					return true;
				},
				&CanonicalInstantiateCalls,
				CanonicalTemplateId,
				GenericError)
			|| !GenericRegistry.InstantiateGenericHostType(
				CanonicalTemplateId, {"uint32"}, CanonicalAliasId, GenericError)
			|| !GenericRegistry.InstantiateGenericHostType(
				CanonicalTemplateId, {"unsigned int"}, CanonicalSpellingId, GenericError)
			|| CanonicalAliasId != CanonicalSpellingId
			|| CanonicalInstantiateCalls != 1)
		{
			std::cerr << GenericError << '\n';
			return Fail("generic host identity must use canonical type arguments");
		}
		FCompileOptions GenericOptions = VeroScript::Test::MakeCompileOptions();
		GenericOptions.Bindings = &GenericRegistry;
		FCompileResult Compile = VeroScriptCompile::CompileSource(
			"void main() { Box<int> value = Box<int>(41); print(value.get() + 1); }\n",
			"<generic_host_value>",
			GenericOptions);
		if (!Compile.bSuccess || !Compile.Module)
		{
			std::cerr << Compile.Error.Message << '\n';
			return Fail("concrete generic host value should compile as a real type");
		}
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		if (!Vm.Execute(
				*Compile.Module,
				ERunMode::Script,
				{},
				&GenericRegistry.GetNativeFunctions())
			|| Output.str() != "42\n")
		{
			std::cerr << Vm.GetError().Message << " output=" << Output.str();
			return Fail("generic host value constructor and instance method should execute");
		}
		std::string Json;
		FHostBindingRegistry Imported;
		if (!ExportBindingCatalogJson(GenericRegistry, Json)
			|| !ImportBindingCatalogJson(Json, Imported, GenericError))
		{
			std::cerr << GenericError << '\n';
			return Fail("generic host type catalog should roundtrip");
		}
		const FHostTypeDesc* ImportedBox = Imported.FindType("Box<int>");
		const FGenericHostTypeTemplateDesc* ImportedTemplate =
			Imported.FindGenericTypeTemplate("Box");
		if (!ImportedBox || !ImportedTemplate
			|| ImportedBox->GenericTemplateId != ImportedTemplate->Id
			|| ImportedBox->GenericTypeArguments != std::vector<std::string>{"int"} || ImportedBox->Annotations.size() != 1
			|| ImportedBox->Members.size() != 2
			|| ImportedBox->Members.front().ParamAnnotations.size() != 1
			|| ImportedBox->Members.back().Annotations.size() != 1)
		{
			return Fail("catalog must preserve generic host instance ABI identity");
		}
	}

	{
		// Transparent aliases keep their source identity while every runtime path
		// (including nested containers and native calls) uses the recursive target.
		FHostBindingRegistry AliasRegistry;
		FHostTypeId PathId;
		FHostTypeId EnumId;
		std::string AliasError;
		if (!AliasRegistry.RegisterType(
				"RuntimePath", EHostTypeKind::Struct, PathId, AliasError)
			|| !AliasRegistry.RegisterType(
				"AliasEnum", EHostTypeKind::Enum, EnumId, AliasError)
			|| !AliasRegistry.SetEnumScoped(EnumId, true, AliasError)
			|| !AliasRegistry.RegisterTypeAlias(
				"SoftRef", {"T"}, "RuntimePath", AliasError)
			|| !AliasRegistry.RegisterTypeAlias(
				"EnumByte", {"T"}, "T", AliasError))
		{
			std::cerr << AliasError << '\n';
			return Fail("transparent host aliases should register");
		}
		FHostMemberDesc One;
		One.Kind = EHostMemberKind::EnumMember;
		One.Name = "One";
		One.EnumValue = 1;
		FHostMemberDesc PathCtor;
		PathCtor.Kind = EHostMemberKind::Constructor;
		PathCtor.ReturnType = ENativeType::HostObject;
		PathCtor.Callback = [](
			const FNativeCallContext&, FValue& Out, std::string&)
		{
			Out = MakeHostObject(
					0,
					"RuntimePath",
					new std::int32_t(7),
					[](void* Payload)
					{
						delete static_cast<std::int32_t*>(Payload);
					});
			return true;
		};
		if (!AliasRegistry.RegisterMember(EnumId, One, AliasError)
			|| !AliasRegistry.RegisterMember(PathId, PathCtor, AliasError))
		{
			std::cerr << AliasError << '\n';
			return Fail("transparent alias target members should register");
		}

		std::string Resolved;
		if (!ResolveHostRuntimeTypeName(
				AliasRegistry,
				"map<string,array<SoftRef<AliasEnum>>>",
				Resolved,
				AliasError)
			|| Resolved != "map<string,array<RuntimePath>>")
		{
			std::cerr << AliasError << " resolved=" << Resolved << '\n';
			return Fail("transparent aliases must resolve recursively in nested generics");
		}
		if (!ResolveHostRuntimeTypeName(
				AliasRegistry,
				"fn(SoftRef<AliasEnum>,array<EnumByte<AliasEnum>>)->SoftRef<AliasEnum>",
				Resolved,
				AliasError)
			|| Resolved != "fn(RuntimePath,array<AliasEnum>)->RuntimePath")
		{
			std::cerr << AliasError << " resolved=" << Resolved << '\n';
			return Fail("transparent aliases must resolve through callable signatures");
		}
		if (!ResolveHostRuntimeTypeName(
				AliasRegistry,
				"array<fn(SoftRef<AliasEnum>,EnumByte<AliasEnum>)->EnumByte<AliasEnum>>",
				Resolved,
				AliasError)
			|| Resolved != "array<fn(RuntimePath,AliasEnum)->AliasEnum>")
		{
			std::cerr << AliasError << " resolved=" << Resolved << '\n';
			return Fail("callable arrows and parameter commas must not corrupt nested generic alias parsing");
		}

		const auto EchoValue = [](
			const FNativeCallContext& Context, FValue& Out, std::string&) -> bool
		{
			Out = Context.Args[0];
			return true;
		};
		if (!AliasRegistry.RegisterNative(
				"echo_alias",
				ENativeType::HostObject,
				{ENativeType::HostObject},
				EchoValue,
				nullptr,
				AliasError,
				"SoftRef<AliasEnum>",
				{"SoftRef<AliasEnum>"})
			|| !AliasRegistry.RegisterNative(
				"echo_alias_array",
				ENativeType::Array,
				{ENativeType::Array},
				EchoValue,
				nullptr,
				AliasError,
				{},
				{},
				"SoftRef<AliasEnum>",
				{"SoftRef<AliasEnum>"}))
		{
			std::cerr << AliasError << '\n';
			return Fail("native signatures should accept transparent host aliases");
		}

		FCompileOptions AliasOptions = VeroScript::Test::MakeCompileOptions();
		AliasOptions.Bindings = &AliasRegistry;
		const FCompileResult AliasCompile = VeroScriptCompile::CompileSource(
			"struct AliasHolder {\n"
			"  SoftRef<AliasEnum> path = RuntimePath();\n"
			"  array<SoftRef<AliasEnum>> paths;\n"
			"  EnumByte<AliasEnum> mode = AliasEnum::One;\n"
			"}\n"
			"void main() {\n"
			"  AliasHolder holder = AliasHolder();\n"
			"  holder.paths.push(RuntimePath());\n"
			"  SoftRef<AliasEnum> copy = echo_alias(holder.path);\n"
			"  array<SoftRef<AliasEnum>> copies = echo_alias_array(holder.paths);\n"
			"  print(len(copies));\n"
			"}\n",
			"<transparent_alias_runtime>",
			AliasOptions);
		if (!AliasCompile.bSuccess || !AliasCompile.Module)
		{
			std::cerr << AliasCompile.Error.Message << '\n';
			return Fail("transparent alias fields and native calls should compile");
		}
		const FBytecodeTypeDesc* Holder = nullptr;
		for (const FBytecodeTypeDesc& Type : AliasCompile.Module->Types)
		{
			if (ModuleMeta::TypeSimpleName(Type.Name) == "AliasHolder")
			{
				Holder = &Type;
				break;
			}
		}
		if (!Holder || Holder->Fields.size() != 3
			|| Holder->Fields[0].DeclaredTypeName != "SoftRef<AliasEnum>"
			|| Holder->Fields[0].TypeName != "RuntimePath"
			|| Holder->Fields[1].DeclaredTypeName != "array<SoftRef<AliasEnum>>"
			|| Holder->Fields[1].TypeName != "array<RuntimePath>"
			|| Holder->Fields[2].DeclaredTypeName != "EnumByte<AliasEnum>"
			|| Holder->Fields[2].TypeName != "AliasEnum")
		{
			if (Holder)
			{
				for (const FBytecodeFieldDesc& Field : Holder->Fields)
				{
					std::cerr << Field.Name << " declared=" << Field.DeclaredTypeName
						<< " runtime=" << Field.TypeName << '\n';
				}
			}
			return Fail("bytecode fields must separate declared and runtime type identities");
		}
		std::vector<std::uint8_t> AliasBytes;
		if (!VeroScriptBytecodeIO::SerializeBytecode(
				*AliasCompile.Module, 17, AliasBytes, AliasError))
		{
			std::cerr << AliasError << '\n';
			return Fail("alias bytecode metadata should serialize");
		}
		std::unique_ptr<FBytecodeModule> AliasRoundTrip =
			VeroScriptBytecodeIO::DeserializeBytecode(
				FByteView(AliasBytes), std::uint64_t{17}, AliasError);
		if (!AliasRoundTrip || AliasRoundTrip->Types.empty())
		{
			std::cerr << AliasError << '\n';
			return Fail("alias bytecode metadata should deserialize");
		}
		std::ostringstream AliasOutput;
		VeroScriptVm AliasVm(AliasOutput);
		VeroScript::Test::Configure(AliasVm);
		if (!AliasVm.Execute(
				*AliasRoundTrip,
				ERunMode::Script,
				{},
				&AliasRegistry.GetNativeFunctions())
			|| AliasOutput.str() != "1\n")
		{
			std::cerr << AliasVm.GetError().Message << " output=" << AliasOutput.str();
			return Fail("alias-expanded runtime values should execute after bytecode roundtrip");
		}
		std::string AliasCatalogJson;
		FHostBindingRegistry ImportedAliases;
		if (!ExportBindingCatalogJson(AliasRegistry, AliasCatalogJson)
			|| !ImportBindingCatalogJson(
				AliasCatalogJson, ImportedAliases, AliasError)
			|| !ImportedAliases.FindTypeAlias("SoftRef")
			|| !ImportedAliases.FindFunction("echo_alias"))
		{
			std::cerr << AliasError << '\n';
			return Fail("binding catalog must preserve transparent alias signatures");
		}
		bool bImportedRuntimeSignature = false;
		for (const FNativeFunction& Native : ImportedAliases.GetNativeFunctions())
		{
			if (Native.Name == "echo_alias"
				&& Native.RuntimeReturnTypeName == "RuntimePath"
				&& Native.RuntimeParamTypeNames == std::vector<std::string>{"RuntimePath"})
			{
				bImportedRuntimeSignature = true;
				break;
			}
		}
		if (!bImportedRuntimeSignature)
		{
			return Fail("imported aliases must rebuild native runtime signatures");
		}

		if (!AliasRegistry.RegisterTypeAlias("CycleA", {"T"}, "CycleB<T>", AliasError)
			|| AliasRegistry.RegisterTypeAlias("CycleB", {"T"}, "CycleA<T>", AliasError)
			|| AliasError != "diag.hostbinding_type_alias_cycle")
		{
			return Fail("transparent alias cycles must be rejected transactionally");
		}

		FHostBindingRegistry ForwardAliasRegistry;
		if (!ForwardAliasRegistry.RegisterTypeAlias(
				"Forward", {"T"}, "Target<T,T>", AliasError)
			|| ForwardAliasRegistry.RegisterTypeAlias(
				"Target", {"T"}, "T", AliasError)
			|| AliasError != "diag.hostbinding_type_alias_arity_mismatch")
		{
			return Fail("resolved forward aliases must be revalidated transactionally");
		}
		FHostBindingRegistry ValidForwardAliasRegistry;
		if (!ValidForwardAliasRegistry.RegisterTypeAlias(
				"Forward", {"T"}, "Target<T>", AliasError)
			|| !ValidForwardAliasRegistry.RegisterTypeAlias(
				"Target", {"T"}, "T", AliasError)
			|| !ResolveHostRuntimeTypeName(
				ValidForwardAliasRegistry, "Forward<int>", Resolved, AliasError)
			|| Resolved != "int")
		{
			return Fail("valid forward aliases should remain supported");
		}
		if (ValidForwardAliasRegistry.RegisterTypeAlias(
				"InvalidParam", {"T-value"}, "int", AliasError)
			|| AliasError
				!= "diag.hostbinding_type_alias_has_invalid_type_parameters")
		{
			return Fail("host alias type parameters must be valid identifiers");
		}
	}

	{
		// Existing implicit numeric conversions participate in host overloads.
		FHostBindingRegistry NumericRegistry;
		FHostTypeId VectorId;
		std::string NumericError;
		std::int32_t DoubleCtorCalls = 0;
		if (!NumericRegistry.RegisterType(
				"NumericVector", EHostTypeKind::Struct, VectorId, NumericError))
		{
			return Fail("numeric overload host type should register");
		}
		FHostMemberDesc DoubleCtor;
		DoubleCtor.Kind = EHostMemberKind::Constructor;
		DoubleCtor.ParamTypes = {
			ENativeType::Double, ENativeType::Double, ENativeType::Double};
		DoubleCtor.UserData = &DoubleCtorCalls;
		DoubleCtor.Callback = [](
			const FNativeCallContext& Context, FValue& Out, std::string&) -> bool
		{
			++*static_cast<std::int32_t*>(Context.UserData);
			if (!std::holds_alternative<double>(Context.Args[0])
					|| !std::holds_alternative<double>(Context.Args[1])
					|| !std::holds_alternative<double>(Context.Args[2]))
			{
				return false;
			}
			Out = MakeHostObject(
					0, "NumericVector", nullptr, [](void*)
					{
					});
			return true;
		};
		if (!NumericRegistry.RegisterMember(VectorId, DoubleCtor, NumericError))
		{
			std::cerr << NumericError << '\n';
			return Fail("double host constructor should register");
		}
		FCompileOptions NumericOptions = VeroScript::Test::MakeCompileOptions();
		NumericOptions.Bindings = &NumericRegistry;
		const FCompileResult NumericCompile = VeroScriptCompile::CompileSource(
			"void main() { float x = 1.0; NumericVector value(x, x, x); }\n",
			"<numeric_host_overload>",
			NumericOptions);
		if (!NumericCompile.bSuccess || !NumericCompile.Module)
		{
			std::cerr << NumericCompile.Error.Message << '\n';
			return Fail("float arguments should select a double host overload");
		}
		VeroScriptVm NumericVm(std::cout);
		VeroScript::Test::Configure(NumericVm);
		if (!NumericVm.Execute(
				*NumericCompile.Module,
				ERunMode::Script,
				{},
				&NumericRegistry.GetNativeFunctions())
			|| DoubleCtorCalls != 1)
		{
			std::cerr << NumericVm.GetError().Message << '\n';
			return Fail("numeric host overload must receive coerced runtime values");
		}

		FHostTypeId RankId;
		std::int32_t ExactRankCalls = 0;
		std::int32_t WidenedRankCalls = 0;
		const auto RegisterRankCtor = [&NumericError](
			FHostBindingRegistry& Registry,
			const FHostTypeId TypeId,
			const ENativeType ParamType,
			std::int32_t* const Calls)
		{
			FHostMemberDesc Ctor;
			Ctor.Kind = EHostMemberKind::Constructor;
			Ctor.ParamTypes = {ParamType};
			Ctor.UserData = Calls;
			Ctor.Callback = [](
				const FNativeCallContext& Context, FValue& Out, std::string&) -> bool
			{
				++*static_cast<std::int32_t*>(Context.UserData);
				Out = MakeHostObject(0, {}, nullptr, [](void*)
				{
				});
				return true;
			};
			return Registry.RegisterMember(TypeId, std::move(Ctor), NumericError);
		};
		if (!NumericRegistry.RegisterType(
				"NumericRank", EHostTypeKind::Struct, RankId, NumericError)
			|| !RegisterRankCtor(
				NumericRegistry, RankId, ENativeType::Int, &ExactRankCalls)
			|| !RegisterRankCtor(
				NumericRegistry, RankId, ENativeType::Int64, &WidenedRankCalls))
		{
			return Fail("numeric ranking overloads should register");
		}
		const FCompileResult RankCompile = VeroScriptCompile::CompileSource(
			"void main() { NumericRank value(1); }\n",
			"<numeric_exact_rank>",
			NumericOptions);
		VeroScriptVm RankVm(std::cout);
		VeroScript::Test::Configure(RankVm);
		if (!RankCompile.bSuccess || !RankCompile.Module
			|| !RankVm.Execute(
				*RankCompile.Module,
				ERunMode::Script, {},
				&NumericRegistry.GetNativeFunctions())
			|| ExactRankCalls != 1 || WidenedRankCalls != 0)
		{
			return Fail("exact numeric overload must outrank safe widening");
		}

		FHostBindingRegistry AmbiguousRegistry;
		FHostTypeId AmbiguousId;
		std::int32_t NeverCalled = 0;
		if (!AmbiguousRegistry.RegisterType(
				"AmbiguousNumeric", EHostTypeKind::Struct, AmbiguousId, NumericError)
			|| !RegisterRankCtor(
				AmbiguousRegistry, AmbiguousId, ENativeType::Int, &NeverCalled)
			|| !RegisterRankCtor(
				AmbiguousRegistry, AmbiguousId, ENativeType::Unsigned, &NeverCalled))
		{
			return Fail("ambiguous numeric overloads should register");
		}
		FCompileOptions AmbiguousOptions = VeroScript::Test::MakeCompileOptions();
		AmbiguousOptions.Bindings = &AmbiguousRegistry;
		const FCompileResult AmbiguousCompile = VeroScriptCompile::CompileSource(
			"void main() { uint16 x = 1; AmbiguousNumeric value(x); }\n",
			"<numeric_ambiguous_rank>",
			AmbiguousOptions);
		if (AmbiguousCompile.bSuccess
			|| AmbiguousCompile.Error.Message.find("ambiguous") == std::string::npos)
		{
			return Fail("equal-rank safe numeric overloads must report ambiguity");
		}

		FHostBindingRegistry RefNumericRegistry;
		FHostTypeId RefNumericId;
		if (!RefNumericRegistry.RegisterType(
				"RefNumeric", EHostTypeKind::Struct, RefNumericId, NumericError))
		{
			return Fail("numeric ref overload host type should register");
		}
		FHostMemberDesc RefDoubleCtor = DoubleCtor;
		RefDoubleCtor.ParamTypes = {ENativeType::Double};
		RefDoubleCtor.ParamFlags = {NativeParamFlagRef};
		if (!RefNumericRegistry.RegisterMember(
				RefNumericId, RefDoubleCtor, NumericError))
		{
			return Fail("numeric ref overload should register");
		}
		FCompileOptions RefNumericOptions = VeroScript::Test::MakeCompileOptions();
		RefNumericOptions.Bindings = &RefNumericRegistry;
		const FCompileResult RefNumericCompile = VeroScriptCompile::CompileSource(
			"void main() { float x = 1.0; RefNumeric value(ref x); }\n",
			"<numeric_ref_overload>",
			RefNumericOptions);
		if (RefNumericCompile.bSuccess)
		{
			return Fail("ref parameters must not bind through numeric temporaries");
		}
	}

	{
		std::vector<FValue> Owners;
		Owners.reserve(1);
		auto Source = std::make_shared<FHostObject>();
		Source->TypeName = "ReentrantValue";
		Source->CloneCallback = &ReentrantStampedClone;
		Source->CloneUserData = &Owners;
		Owners.emplace_back(std::move(Source));
		FValue Cloned;
		if (!Runtime::TryCloneValue(Owners.front(), Cloned))
		{
			return Fail("stamped clone should survive source-owner reallocation");
		}
		const auto* const ClonedHandle = std::get_if<FHostObjectHandle>(&Cloned);
		if (!ClonedHandle || !*ClonedHandle
			|| (*ClonedHandle)->TypeName != "ReentrantValue"
			|| (*ClonedHandle)->CloneCallback != &ReentrantStampedClone
			|| (*ClonedHandle)->CloneUserData != &Owners)
		{
			return Fail("stamped clone metadata should survive host reentry");
		}
	}

	{
		auto Source = std::make_shared<FScriptObject>();
		Source->Kind = EScriptObjectKind::Struct;
		Source->SetTypeName("ReentrantStruct");
		auto ReentrantValue = std::make_shared<FHostObject>();
		ReentrantValue->TypeName = "ReentrantValue";
		ReentrantValue->CloneCallback = &ReentrantStampedClone;
		ReentrantValue->CloneUserData = &Source->Fields;
		Source->Fields.emplace_back(std::move(ReentrantValue));
		Source->Fields.emplace_back(std::int32_t{9});

		FValue ClonedValue;
		if (!Runtime::TryCloneValue(FValue{Source}, ClonedValue))
		{
			return Fail("struct clone should survive source-field reallocation");
		}
		const auto* const Cloned = std::get_if<FObjectHandle>(&ClonedValue);
		const auto* const ClonedHost = Cloned && *Cloned
			&& (*Cloned)->Fields.size() == 2
			? std::get_if<FHostObjectHandle>(&(*Cloned)->Fields[0])
			: nullptr;
		if (Source->Fields.size() != 66 || !ClonedHost || !*ClonedHost
			|| (*ClonedHost)->TypeName != "ReentrantValue"
			|| std::get<std::int32_t>((*Cloned)->Fields[1]) != 9)
		{
			return Fail("struct clone should use the entry field snapshot");
		}
	}

	{
		auto Cyclic = std::make_shared<FScriptObject>();
		Cyclic->Kind = EScriptObjectKind::Struct;
		Cyclic->SetTypeName("MalformedCyclicStruct");
		Cyclic->Fields.emplace_back(Cyclic);
		FValue Cloned;
		const bool bCloned = Runtime::TryCloneValue(FValue{Cyclic}, Cloned);
		Cyclic->Fields.clear();
		if (bCloned)
		{
			return Fail("cyclic struct value containment must be rejected");
		}
	}

	{
		FValue Nested = std::int32_t{1};
		for (std::size_t Depth = 0; Depth < 130; ++Depth)
		{
			auto Wrapper = std::make_shared<FScriptObject>();
			Wrapper->Kind = EScriptObjectKind::Struct;
			Wrapper->SetTypeName("DeepStruct");
			Wrapper->Fields.push_back(std::move(Nested));
			Nested = std::move(Wrapper);
		}
		FValue Cloned;
		if (Runtime::TryCloneValue(Nested, Cloned))
		{
			return Fail("excessively deep struct value containment must be rejected");
		}
	}

	{
		FBytecodeModule EqualityModule;
		FBytecodeTypeDesc Type;
		Type.Kind = EScriptObjectKind::Struct;
		Type.Name = "ReentrantEqualityStruct";
		Type.Fields.resize(2);
		EqualityModule.Types.push_back(std::move(Type));

		auto Left = std::make_shared<FScriptObject>();
		auto Right = std::make_shared<FScriptObject>();
		Left->Kind = Right->Kind = EScriptObjectKind::Struct;
		Left->SetTypeName("ReentrantEqualityStruct");
		Right->SetTypeName("ReentrantEqualityStruct");
		auto LeftHost = std::make_shared<FHostObject>();
		auto RightHost = std::make_shared<FHostObject>();
		LeftHost->TypeId = RightHost->TypeId = 1;
		LeftHost->TypeName = RightHost->TypeName = "ReentrantEqualityValue";
		LeftHost->EqualsCallback = RightHost->EqualsCallback = &ReentrantHostEquals;
		LeftHost->HashCallback = RightHost->HashCallback = &StableHostHash;
		LeftHost->EqualsUserData = RightHost->EqualsUserData = &Left->Fields;
		Left->Fields.emplace_back(std::move(LeftHost));
		Left->Fields.emplace_back(std::int32_t{9});
		Right->Fields.emplace_back(std::move(RightHost));
		Right->Fields.emplace_back(std::int32_t{9});
		if (!Runtime::ValuesEqual(EqualityModule, FValue{Left}, FValue{Right})
			|| Left->Fields.size() != 66)
		{
			return Fail("struct equality should use entry field snapshots during host reentry");
		}
	}

	{
		FBytecodeModule EqualityModule;
		FBytecodeTypeDesc Type;
		Type.Kind = EScriptObjectKind::Struct;
		Type.Name = "MalformedCyclicEqualityStruct";
		Type.Fields.resize(1);
		EqualityModule.Types.push_back(std::move(Type));
		auto Left = std::make_shared<FScriptObject>();
		auto Right = std::make_shared<FScriptObject>();
		Left->Kind = Right->Kind = EScriptObjectKind::Struct;
		Left->SetTypeName("MalformedCyclicEqualityStruct");
		Right->SetTypeName("MalformedCyclicEqualityStruct");
		Left->Fields.emplace_back(Left);
		Right->Fields.emplace_back(Right);
		const bool bReflexive = Runtime::ValuesEqual(
			EqualityModule, FValue{Left}, FValue{Left});
		const bool bEqual = Runtime::ValuesEqual(EqualityModule, FValue{Left}, FValue{Right});
		Left->Fields.clear();
		Right->Fields.clear();
		if (!bReflexive || bEqual)
		{
			return Fail("cyclic struct equality must remain bounded and identity-safe");
		}
	}

	{
		auto Source = std::make_shared<FHostObject>();
		Source->TypeName = "ThrowingValue";
		Source->CloneCallback = &ThrowingStampedClone;
		FValue Cloned;
		if (Runtime::TryCloneValue(FValue{std::move(Source)}, Cloned))
		{
			return Fail("foreign exceptions from stamped clone callbacks must be contained");
		}
	}

	{
		FNativeFunction ThrowingNative;
		ThrowingNative.Name = "throwing_native";
		ThrowingNative.ReturnType = ENativeType::Int;
		ThrowingNative.Callback =
			[](const FNativeCallContext&, FValue&, std::string&) -> bool
			{
				throw 9;
			};
		std::vector<FNativeFunction> ThrowingNatives{ThrowingNative};
		FCompileOptions ThrowingNativeOptions = VeroScript::Test::MakeCompileOptions();
		ThrowingNativeOptions.Natives = &ThrowingNatives;
		const FCompileResult Compile = VeroScriptCompile::CompileSource(
			"void main() { print(throwing_native()); }",
			"<throwing_native>",
			ThrowingNativeOptions);
		if (!Compile.Module)
		{
			return Fail("throwing native regression source should compile");
		}
		for (const EExecutionBackend Backend : {
				 EExecutionBackend::Vm,
			EExecutionBackend::Mixed})
		{
			std::ostringstream Output;
			VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
			Vm.SetExecutionBackend(Backend);
			if (Vm.Execute(
					*Compile.Module, ERunMode::Script, {}, &ThrowingNatives)
				|| Vm.GetError().Message.find("native callback threw")
					== std::string::npos)
			{
				return Fail("foreign native exceptions must become VM errors on every backend");
			}
		}
	}

	return EXIT_SUCCESS;
