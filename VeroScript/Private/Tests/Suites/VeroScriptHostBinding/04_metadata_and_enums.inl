// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Internal fragment: VeroScriptHostBindingTests / metadata and enums suite. Included by the owning translation unit.

	// Recursive descriptors are the authoritative public spelling for nested
	// container ABI types; no parallel string arrays are needed at the call site.
	{
		FHostBindingRegistry RecursiveRegistry;
		std::string RecursiveErrorId;
		const FNativeTypeDesc NestedReturn = FNativeTypeDesc::QueueOf(
			FNativeTypeDesc::ArrayOf(
				FNativeTypeDesc::Scalar(ENativeType::Int)));
		const FNativeTypeDesc NestedParam = FNativeTypeDesc::OrderedMapOf(
			FNativeTypeDesc::Scalar(ENativeType::String),
			FNativeTypeDesc::DequeOf(
				FNativeTypeDesc::Scalar(ENativeType::Int64)));
		if (!RecursiveRegistry.RegisterNative(
				"recursive_container_desc",
				NestedReturn,
				{FNativeParamDesc{
					NestedParam,
					NativeParamFlagRef | NativeParamFlagConstRef}},
				[](const FNativeCallContext&, FValue& Out, std::string&)
				{
					auto Queue = std::make_shared<FQueue>();
					Queue->TypeName = "queue<array<int>>";
					Queue->ElementTypeName = "array<int>";
					Out = std::move(Queue);
					return true;
				},
				nullptr,
				RecursiveErrorId))
		{
			std::cerr << RecursiveErrorId << '\n';
			return Fail("recursive FNativeTypeDesc registration should succeed");
		}
		const auto& Natives = RecursiveRegistry.GetNativeFunctions();
		if (Natives.size() != 1
			|| ResolveNativeReturnTypeName(Natives[0]) != "queue<array<int>>"
			|| ResolveNativeParamTypeName(Natives[0], 0)
				!= "ordered_map<string,deque<int64>>"
			|| Natives[0].ParamFlags.size() != 1
			|| Natives[0].ParamFlags[0] !=
				(NativeParamFlagRef | NativeParamFlagConstRef))
		{
			return Fail("recursive FNativeTypeDesc should flatten to canonical ABI metadata");
		}
		if (!RecursiveRegistry.RegisterNative(
				"native_wide_channels",
				FNativeTypeDesc::Scalar(ENativeType::Void),
				{
					FNativeParamDesc{
						FNativeTypeDesc::ChannelOf(
							FNativeTypeDesc::Scalar(ENativeType::Int64)),
						NativeParamFlagRef | NativeParamFlagConstRef},
					FNativeParamDesc{
						FNativeTypeDesc::ChannelOf(
							FNativeTypeDesc::Scalar(ENativeType::UInt64)),
						NativeParamFlagRef | NativeParamFlagConstRef},
				},
				[](const FNativeCallContext&, FValue&, std::string&)
				{
					return true;
				},
				nullptr,
				RecursiveErrorId))
		{
			std::cerr << RecursiveErrorId << '\n';
			return Fail("int64/uint64 channel native ABI registration should succeed");
		}
		if (!RecursiveRegistry.RegisterNative(
				"native_queue_size",
				FNativeTypeDesc::Scalar(ENativeType::Int),
				{FNativeParamDesc{
					FNativeTypeDesc::QueueOf(
						FNativeTypeDesc::Scalar(ENativeType::Int)),
					NativeParamFlagRef | NativeParamFlagConstRef}},
				[](const FNativeCallContext& Context, FValue& Out, std::string& Error)
				{
					const auto* const Queue =
								Context.ArgCount == 1
									? std::get_if<FQueueHandle>(&Context.Args[0])
									: nullptr;
					if (!Queue || !*Queue)
					{
						Error = "expected queue<int>";
						return false;
					}
					Out = static_cast<std::int32_t>((*Queue)->Elements.size());
					return true;
				},
				nullptr,
				RecursiveErrorId))
		{
			std::cerr << RecursiveErrorId << '\n';
			return Fail("queue native ABI registration should succeed");
		}
		if (!RecursiveRegistry.RegisterNative(
				"native_priority_queue",
				FNativeTypeDesc::PriorityQueueOf(
					FNativeTypeDesc::Scalar(ENativeType::Int)),
				{},
				[](const FNativeCallContext&, FValue& Out, std::string&)
				{
					auto Queue = std::make_shared<FQueue>();
					Queue->TypeName = "priority_queue<int>";
					Queue->ElementTypeName = "int";
					// Public host values need not already satisfy the VM's heap layout.
					Queue->Elements = {
						std::int32_t{1}, std::int32_t{9}, std::int32_t{3}};
					Out = std::move(Queue);
					return true;
				},
				nullptr,
				RecursiveErrorId))
		{
			std::cerr << RecursiveErrorId << '\n';
			return Fail("priority_queue native ABI registration should succeed");
		}
		if (!RecursiveRegistry.RegisterNative(
				"native_multimap",
				FNativeTypeDesc::MultiMapOf(
					FNativeTypeDesc::Scalar(ENativeType::String),
					FNativeTypeDesc::Scalar(ENativeType::Int)),
				{},
				[](const FNativeCallContext&, FValue& Out, std::string&)
				{
					auto Values = std::make_shared<FDynamicArray>();
					Values->TypeName = "array<int>";
					Values->ElementTypeName = "int";
					Values->Elements = {std::int32_t{4}, std::int32_t{8}};
					auto Map = std::make_shared<FMap>();
					Map->TypeName = "multimap<string,int>";
					Map->KeyTypeName = "string";
					Map->ValueTypeName = "array<int>";
					Map->StrEntries.emplace("a", FValue{std::move(Values)});
					Out = std::move(Map);
					return true;
				},
				nullptr,
				RecursiveErrorId))
		{
			std::cerr << RecursiveErrorId << '\n';
			return Fail("multimap native ABI registration should succeed");
		}
		FCompileOptions RecursiveOptions = VeroScript::Test::MakeCompileOptions();
		RecursiveOptions.Bindings = &RecursiveRegistry;
		const FCompileResult RecursiveCompile = VeroScriptCompile::CompileSource(
			"void main() {\n"
			"  queue<int> q = queue<int>();\n"
			"  q.push(3); q.push(4);\n"
			"  print(native_queue_size(ref q));\n"
			"  priority_queue<int> p = native_priority_queue();\n"
			"  print(p.pop()); print(p.pop()); print(p.pop());\n"
			"  multimap<string,int> grouped = native_multimap();\n"
			"  print(len(grouped)); print(grouped[\"a\"][1]);\n"
			"}\n",
			"<queue_native_abi>",
			RecursiveOptions);
		if (!RecursiveCompile.bSuccess || !RecursiveCompile.Module)
		{
			std::cerr << RecursiveCompile.Error.Message << '\n';
			return Fail("queue native ABI script should compile");
		}
		std::ostringstream RecursiveOutput;
		VeroScriptVm Vm(RecursiveOutput);
		VeroScript::Test::Configure(Vm);
		if (!Vm.Execute(
				*RecursiveCompile.Module,
				ERunMode::Script,
				{},
				&RecursiveRegistry.GetNativeFunctions()))
		{
			std::cerr << Vm.GetError().Message << '\n';
			return Fail("queue native ABI script should execute");
		}
		if (RecursiveOutput.str() != "2\n9\n3\n1\n1\n8\n")
		{
			return Fail("native queue ABI should preserve queue invariants");
		}
	}

	{
		// Tooling documentation and structured annotation arguments must survive
		// catalog export/import without affecting runtime registration.
		const std::string FunctionDocumentation =
			std::string("Returns a documented host value.") + '\b' + '\f' + '\x01';
		FHostBindingRegistry DocumentedRegistry;
		std::string DocumentationErrorId;
		FHostFunctionDesc DocumentedFunction;
		DocumentedFunction.Name = "documented_host";
		DocumentedFunction.Documentation = FunctionDocumentation;
		DocumentedFunction.ParamNames = {"value"};
		DocumentedFunction.ParamDocumentation =
			{"Input value supplied by the caller."};
		DocumentedFunction.ReturnDocumentation = "The transformed value.";
		DocumentedFunction.ReturnType = ENativeType::Int;
		DocumentedFunction.ParamTypes = {ENativeType::Int};
		DocumentedFunction.Annotations = {{"UFUNCTION",
			{{"BlueprintCallable", std::nullopt},
				{"Category", std::string("VeroScript|Network")},
				{"Empty", std::string{}},
				{"Control", std::string("line\n`tick")}}}};
		DocumentedFunction.ParamAnnotations = {
			{{"UPARAM", {{"DisplayName", std::string("Score")}}}}};
		if (!DocumentedRegistry.RegisterNative(
				std::move(DocumentedFunction),
				BindingCatalogStubNative,
				nullptr,
				DocumentationErrorId))
		{
			std::cerr << DocumentationErrorId << '\n';
			return Fail("register documented host function");
		}
		FHostTypeId DocumentedTypeId;
		if (!DocumentedRegistry.RegisterType(
				"DocumentedType",
				EHostTypeKind::Class,
				DocumentedTypeId,
				DocumentationErrorId,
				{{"UCLASS", {{"BlueprintType", std::nullopt}}}})
			|| !DocumentedRegistry.SetTypeDocumentation(
				DocumentedTypeId,
				"A documented host class.",
				DocumentationErrorId))
		{
			std::cerr << DocumentationErrorId << '\n';
			return Fail("register documented host type");
		}
		FHostMemberDesc Member;
		Member.Kind = EHostMemberKind::Method;
		Member.Name = "work";
		Member.Documentation = "Performs documented host work.";
		Member.ReturnType = ENativeType::Void;
		Member.ParamTypes = {ENativeType::Int};
		Member.ParamNames = {"amount"};
		Member.Annotations = {{"UFUNCTION", {{"BlueprintCallable", std::nullopt}}}};
		Member.ParamAnnotations = {
			{{"UPARAM", {{"ClampMin", std::string("0")}}}}};
		Member.Callback = BindingCatalogStubNative;
		FHostMemberDesc Constant;
		Constant.Kind = EHostMemberKind::StaticConstant;
		Constant.Name = "DefaultCount";
		Constant.Documentation = "The default item count.";
		Constant.ReturnType = ENativeType::Int;
		Constant.Annotations = {{"UPROPERTY", {{"VisibleAnywhere", std::nullopt}}}};
		Constant.Callback = BindingCatalogStubNative;
		if (!DocumentedRegistry.RegisterMember(
				DocumentedTypeId, Member, DocumentationErrorId)
			|| !DocumentedRegistry.RegisterMember(
				DocumentedTypeId, Constant, DocumentationErrorId)
			|| !DocumentedRegistry.RegisterAnnotation(
				"UFUNCTION",
				"annotation",
				DocumentationErrorId,
				"Exposes a reflected function.",
				{FHostAnnotationArgumentDesc{
					"BlueprintCallable",
					"flag",
					"Makes the function callable from Blueprint."}}))
		{
			std::cerr << DocumentationErrorId << '\n';
			return Fail("register documented member and annotation");
		}

		std::string Json;
		FHostBindingRegistry Imported;
		std::string ImportError;
		if (!ExportBindingCatalogJson(DocumentedRegistry, Json)
			|| !ImportBindingCatalogJson(Json, Imported, ImportError))
		{
			std::cerr << ImportError << '\n';
			return Fail("roundtrip documented binding catalog");
		}
		const FHostFunctionDesc* Function = Imported.FindFunction("documented_host");
		const FHostTypeDesc* Type = Imported.FindType("DocumentedType");
		const auto& Annotations = Imported.GetAnnotations();
		if (!Function
			|| Function->Documentation != FunctionDocumentation
			|| Function->ParamNames != std::vector<std::string>{"value"} || Function->ParamDocumentation
				!= std::vector<std::string>{"Input value supplied by the caller."} || Function->Annotations.size() != 1
			|| Function->Annotations.front().Arguments.size() != 4
			|| Function->Annotations.front().Arguments[0].Value.has_value()
			|| !Function->Annotations.front().Arguments[1].Value.has_value()
			|| *Function->Annotations.front().Arguments[1].Value
				!= "VeroScript|Network"
			|| !Function->Annotations.front().Arguments[2].Value.has_value()
			|| !Function->Annotations.front().Arguments[2].Value->empty()
			|| !Function->Annotations.front().Arguments[3].Value.has_value()
			|| *Function->Annotations.front().Arguments[3].Value != "line\n`tick"
			|| Function->ParamAnnotations.size() != 1
			|| Function->ParamAnnotations.front().front().Name != "UPARAM"
			|| !Type
			|| Type->Documentation != "A documented host class."
			|| Type->Annotations.size() != 1
			|| Type->Members.size() != 2
			|| Type->Members.front().Documentation != "Performs documented host work."
			|| Type->Members.front().Annotations.size() != 1
			|| Type->Members.front().ParamAnnotations.size() != 1
			|| Type->Members[1].Kind != EHostMemberKind::StaticConstant
			|| Type->Members[1].Name != "DefaultCount"
			|| Annotations.size() != 1
			|| Annotations.front().Arguments.size() != 1
			|| Annotations.front().Arguments.front().Name != "BlueprintCallable"
			|| Annotations.front().Arguments.front().Documentation.find("Blueprint")
				== std::string::npos)
		{
			return Fail("documented binding metadata should roundtrip exactly");
		}
		if (FormatHostDocumentation(
				"Indexed parameters.",
				{},
				{"First unnamed input."},
				{})
				.find("Parameter `#1`: First unnamed input.") == std::string::npos)
		{
			return Fail("unnamed host parameter documentation should remain visible");
		}
		const std::string RenderedMetadata = FormatHostDocumentation(
			Function->Documentation,
			Function->ParamNames,
			Function->ParamDocumentation,
			Function->ReturnDocumentation,
			Function->Annotations,
			Function->ParamAnnotations);
		if (RenderedMetadata.find("UFUNCTION(BlueprintCallable, Category=\"VeroScript|Network\"")
				== std::string::npos
			|| RenderedMetadata.find("UPARAM(DisplayName=\"Score\")")
				== std::string::npos
			|| RenderedMetadata.find("Control=\"line\\n\\x60tick\"")
				== std::string::npos)
		{
			return Fail("declaration annotations should be rendered for tooling");
		}
		FHostBindingRegistry Appended;
		FHostTypeId ExistingTypeId;
		if (!Appended.RegisterType(
				"ExistingType",
				EHostTypeKind::Class,
				ExistingTypeId,
				DocumentationErrorId)
			|| !ImportBindingCatalogJson(Json, Appended, ImportError, false))
		{
			std::cerr << ImportError << '\n';
			return Fail("append binding catalog with declaration annotations");
		}
		const FHostFunctionDesc* AppendedFunction =
			Appended.FindFunction("documented_host");
		const FHostTypeDesc* AppendedType = Appended.FindType("DocumentedType");
		if (!Appended.FindType("ExistingType") || !AppendedFunction || !AppendedType
			|| AppendedFunction->Annotations.size() != 1
			|| AppendedFunction->ParamAnnotations.size() != 1
			|| AppendedType->Annotations.size() != 1
			|| AppendedType->Members.empty()
			|| AppendedType->Members.front().Annotations.size() != 1)
		{
			return Fail("append catalog merge must preserve declaration annotations");
		}

		FHostBindingRegistry InvalidAnnotations;
		FHostFunctionDesc InvalidFunction;
		InvalidFunction.Name = "bad_annotation_count";
		InvalidFunction.ReturnType = ENativeType::Void;
		InvalidFunction.ParamTypes = {ENativeType::Int};
		InvalidFunction.ParamAnnotations = {};
		InvalidFunction.ParamAnnotations.resize(2);
		if (InvalidAnnotations.RegisterNative(
				std::move(InvalidFunction),
				BindingCatalogStubNative,
				nullptr,
				DocumentationErrorId)
			|| DocumentationErrorId
				!= "diag.hostbinding_param_annotation_count_mismatch")
		{
			return Fail("misaligned parameter annotations must be rejected");
		}
		FHostFunctionDesc DuplicateArgumentFunction;
		DuplicateArgumentFunction.Name = "bad_duplicate_argument";
		DuplicateArgumentFunction.Annotations = {
			{"TAG", {{"Name", std::nullopt}, {"Name", std::string("again")}}}};
		if (InvalidAnnotations.RegisterNative(
				std::move(DuplicateArgumentFunction),
				BindingCatalogStubNative,
				nullptr,
				DocumentationErrorId)
			|| DocumentationErrorId
				!= "diag.hostbinding_duplicate_declaration_annotation_argument"
			|| !InvalidAnnotations.GetNativeFunctions().empty())
		{
			return Fail("invalid declaration annotation registration must be transactional");
		}
		FHostTypeId InvalidTypeId;
		if (InvalidAnnotations.RegisterType(
				"BadAnnotatedType",
				EHostTypeKind::Class,
				InvalidTypeId,
				DocumentationErrorId,
				{{"bad name", {}}})
			|| InvalidTypeId.IsValid()
			|| !InvalidAnnotations.GetTypes().empty())
		{
			return Fail("invalid type annotation registration must not consume a type id");
		}
		FHostFunctionDesc InvalidGenericCast;
		InvalidGenericCast.Name = "bad_generic_signature";
		InvalidGenericCast.bGenericHostObjectCast = true;
		InvalidGenericCast.TypeParams = {"T"};
		InvalidGenericCast.ScriptReturnTypeName = "T";
		if (InvalidAnnotations.RegisterNative(
				std::move(InvalidGenericCast),
				BindingCatalogStubNative,
				nullptr,
				DocumentationErrorId)
			|| DocumentationErrorId
				!= "diag.hostbinding_generic_cast_has_invalid_signature"
			|| InvalidAnnotations.RegisterGenericHostObjectCast(
				"bad_generic_parameter", {"bad name"},
				"bad name",
				BindingCatalogStubNative,
				nullptr,
				DocumentationErrorId)
			|| DocumentationErrorId
				!= "diag.hostbinding_generic_cast_has_invalid_type_parameters"
			|| !InvalidAnnotations.GetNativeFunctions().empty())
		{
			return Fail("generic host cast metadata must match its canonical ABI");
		}
	}

	{
		// A host cast is genuinely generic: T is the compile-time result type while
		// the concrete target name reaches the callback without becoming a visible
		// value parameter. Failed casts return nullptr.
		FHostBindingRegistry CastRegistry;
		std::string CastErrorId;
		FHostTypeId BaseId;
		FHostTypeId DerivedId;
		FHostTypeId OtherId;
		FHostTypeId LegacyEnumId;
		FHostTypeId NamespaceEnumId;
		FHostTypeId ReasonEnumId;
		FHostTypeId TypedEnumId;
		if (!CastRegistry.RegisterType(
				"BaseHost", EHostTypeKind::Class, BaseId, CastErrorId)
			|| !CastRegistry.RegisterType(
				"DerivedHost", EHostTypeKind::Class, DerivedId, CastErrorId)
			|| !CastRegistry.RegisterType(
				"OtherHost", EHostTypeKind::Class, OtherId, CastErrorId)
			|| !CastRegistry.RegisterType(
				"LegacyEnum", EHostTypeKind::Enum, LegacyEnumId, CastErrorId)
			|| !CastRegistry.RegisterType(
				"ENamespace::Type",
				EHostTypeKind::Enum,
				NamespaceEnumId,
				CastErrorId)
			|| !CastRegistry.RegisterType(
				"EReason::Type",
				EHostTypeKind::Enum,
				ReasonEnumId,
				CastErrorId)
			|| !CastRegistry.RegisterType(
				"TypedEnum", EHostTypeKind::Enum, TypedEnumId, CastErrorId)
			|| !CastRegistry.RegisterTypeAlias(
				"TEnumAsByte",
				{"T"},
				"T",
				CastErrorId,
				"Byte-backed enum source spelling."))
		{
			return Fail("register generic cast host types");
		}
		FHostMemberDesc LegacyOne;
		LegacyOne.Kind = EHostMemberKind::EnumMember;
		LegacyOne.Name = "One";
		LegacyOne.EnumValue = 1;
		if (!CastRegistry.RegisterMember(LegacyEnumId, LegacyOne, CastErrorId))
		{
			return Fail("register host enum used through a generic type alias");
		}
		FHostMemberDesc NamespaceOne = LegacyOne;
		if (!CastRegistry.RegisterMember(
				NamespaceEnumId, NamespaceOne, CastErrorId))
		{
			return Fail("register C++ namespace-style host enum");
		}
		FHostMemberDesc ReasonOne = LegacyOne;
		if (!CastRegistry.RegisterMember(ReasonEnumId, ReasonOne, CastErrorId))
		{
			return Fail("register scoped host enum method parameter");
		}
		FHostMemberDesc TypedOne = LegacyOne;
		TypedOne.ReturnHostTypeName = "TypedEnum";
		if (!CastRegistry.RegisterMember(TypedEnumId, TypedOne, CastErrorId))
		{
			return Fail("register typed host enum");
		}
		FHostMemberDesc DerivedCtor;
		DerivedCtor.Kind = EHostMemberKind::Constructor;
		DerivedCtor.ReturnType = ENativeType::HostObject;
		DerivedCtor.Callback =
			[](const FNativeCallContext&, FValue& Out, std::string&)
			{
				auto* Payload = new std::int32_t{7};
				Out = MakeHostObject(
						2,
						"DerivedHost",
						Payload,
						[](void* Value)
						{
							delete static_cast<std::int32_t*>(Value);
						},
						[](void*, void*, const std::string_view Expected)
						{
							return Expected == "DerivedHost" || Expected == "BaseHost";
						});
				return true;
			};
		FHostMemberDesc EnumMethod;
		EnumMethod.Kind = EHostMemberKind::Method;
		EnumMethod.Name = "Mode";
		EnumMethod.ReturnType = ENativeType::Enum;
		EnumMethod.ReturnHostTypeName = "TypedEnum";
		EnumMethod.Callback =
			[](const FNativeCallContext&, FValue& Out, std::string&)
			{
				Out = FEnumValue{"TypedEnum", 1};
				return true;
			};
		if (!CastRegistry.RegisterMember(DerivedId, DerivedCtor, CastErrorId)
			|| !CastRegistry.RegisterMember(DerivedId, EnumMethod, CastErrorId)
			|| !CastRegistry.RegisterGenericHostObjectCast(
				"Cast", {"T"},
				"T",
				[](const FNativeCallContext& Context, FValue& Out, std::string& Error)
				{
					if (Context.ArgCount != 1
								|| Context.TypeArgumentCount != 1
								|| !Context.TypeArguments)
					{
						Error = "bad generic cast context";
						return false;
					}
					const auto* Host =
								std::get_if<FHostObjectHandle>(&Context.Args[0]);
					if (!Host || !*Host)
					{
						Out = FHostObjectHandle{};
						return true;
					}
					Out = HostObjectMatchesType(*Host, Context.TypeArguments[0])
								? FValue{*Host}
						: FValue{FHostObjectHandle{}};
					return true;
				},
				nullptr,
				CastErrorId))
		{
			std::cerr << CastErrorId << '\n';
			return Fail("register generic host cast");
		}
		FCompileOptions CastOptions = VeroScript::Test::MakeCompileOptions();
		CastOptions.Bindings = &CastRegistry;
		const FCompileResult Compile = VeroScriptCompile::CompileSource(
			"class EnumAliasApi {\n"
			"public:\n"
			"  TEnumAsByte<ENamespace::Type> Echo("
				"TEnumAsByte<ENamespace::Type> Value) {\n"
			"    return Value;\n"
			"  }\n"
			"}\n"
			"class ScopedReasonApi {\n"
			"public:\n"
			"  @rpc(reliable)\n"
			"  EReason::Type EndPlay(EReason::Type EndPlayReason) {\n"
			"    return EndPlayReason;\n"
			"  }\n"
			"  static EReason::Type StaticEcho(EReason::Type Value) {\n"
			"    return Value;\n"
			"  }\n"
			"}\n"
			"EReason::Type FreeEcho(EReason::Type Value) { return Value; }\n"
			"void main() {\n"
			"  DerivedHost Source = DerivedHost();\n"
			"  BaseHost Base = Cast<BaseHost>(Source);\n"
			"  OtherHost Other = Cast<OtherHost>(Source);\n"
			"  BaseHost GenericNull = Cast<BaseHost>(nullptr);\n"
			"  auto NullHost = try_cast<BaseHost>(nullptr);\n"
			"  TEnumAsByte<LegacyEnum> Legacy = LegacyEnum.One;\n"
			"  TEnumAsByte<ENamespace::Type> NamespaceValue = ENamespace::One;\n"
			"  EnumAliasApi Api = EnumAliasApi();\n"
			"  print(Base != nullptr);\n"
			"  print(Other == nullptr);\n"
			"  print(GenericNull == nullptr);\n"
			"  print(NullHost.has_value());\n"
			"  print(NullHost.get() == nullptr);\n"
			"  print(Legacy);\n"
			"  print(NamespaceValue);\n"
			"  print(Api.Echo(NamespaceValue));\n"
			"  print(Source.Mode());\n"
			"  print(Source.Mode() == TypedEnum.One);\n"
			"}\n",
			"<generic_host_cast>",
			CastOptions);
		if (!Compile.bSuccess || !Compile.Module)
		{
			std::cerr << Compile.Error.Message << '\n';
			return Fail("generic host cast script should compile");
		}
		const std::string NestedScopedKey = MakeMethodFunctionName(
			"EnumAliasApi",
			"Echo",
			{"TEnumAsByte<EAutoReceiveInput::Type>"});
		std::vector<std::string> NestedScopedParams;
		if (MethodLinkageBareLeaf(NestedScopedKey) != "Echo"
			|| !ModuleMeta::DecodeMethodParamTypeNames(
				NestedScopedKey, NestedScopedParams)
			|| NestedScopedParams
				!= std::vector<std::string>{"TEnumAsByte<EAutoReceiveInput::Type>"})
		{
			return Fail("nested scoped parameter linkage should decode without changing leaf");
		}
		const std::string GenericOwnerKey = MakeMethodFunctionName(
			"Box#13:EReason::Type",
			"Echo",
			{"TEnumAsByte<EAutoReceiveInput::Type>"});
		if (FindFunctionSignatureMarker(GenericOwnerKey)
				== GenericOwnerKey.find('#')
			|| MethodLinkageBareLeaf(GenericOwnerKey) != "Echo"
			|| !MethodLinkageLeafMatches(GenericOwnerKey, "Echo"))
		{
			return Fail("generic owner marker must not hide a scoped-parameter method leaf");
		}
		const std::string NestedUnitType =
			std::string("<arg>") + '\x1F' + "EReason::Type";
		const std::string ExportedGenericOwner =
			"Box#" + std::to_string(NestedUnitType.size()) + ':' + NestedUnitType;
		const std::string ExportedGenericMethod = MakeMethodFunctionName(
			ExportedGenericOwner, "Echo", {NestedUnitType});
		std::vector<std::string> ExportedGenericParams;
		if (FindLinkageIdentityStart(ExportedGenericMethod) != 0
			|| MethodLinkageBareLeaf(ExportedGenericMethod) != "Echo"
			|| !ModuleMeta::DecodeMethodParamTypeNames(
				ExportedGenericMethod, ExportedGenericParams)
			|| ExportedGenericParams != std::vector<std::string>{NestedUnitType} || ModuleMeta::TypeSimpleName(ExportedGenericOwner) != "Box")
		{
			return Fail("unit separators inside generic payloads must stay nested");
		}
		const std::string PrivateGenericOwner =
			std::string("C:/outer#2/module.vs") + '\x1F' + ExportedGenericOwner;
		std::string PairFirst;
		std::string PairSecond;
		if (Runtime::MakePairTypeName("int", "string") != "pair<int,string>"
			|| Runtime::ParsePairType("Pair<int,string>", PairFirst, PairSecond))
		{
			return Fail("pair runtime identities are lowercase without a legacy builtin alias");
		}
		std::vector<std::string> CallableParams;
		std::string CallableReturn;
		if (FindLinkageIdentityStart(PrivateGenericOwner)
				!= std::string("C:/outer#2/module.vs").size() + 1
			|| ModuleMeta::TypeSimpleName(PrivateGenericOwner) != "Box"
			|| !ModuleMeta::TypeNameMatches(
				PrivateGenericOwner, ExportedGenericOwner)
			|| !Runtime::ParsePairType(
				"pair<" + NestedUnitType + ",int>", PairFirst, PairSecond)
			|| PairFirst != NestedUnitType
			|| PairSecond != "int"
			|| !Runtime::ParseFunctionType(
				"fn(array<fn(int,string)->bool>,pair<fn()->void,int>)"
				"->map<string,fn(int,int)->void>",
				CallableParams,
				CallableReturn)
			|| CallableParams != std::vector<std::string>{"array<fn(int,string)->bool>",
				"pair<fn()->void,int>"} || CallableReturn != "map<string,fn(int,int)->void>"
			|| !Runtime::ParsePairType(
				"pair<fn(int,string)->bool,array<fn()->void>>",
				PairFirst,
				PairSecond)
			|| PairFirst != "fn(int,string)->bool"
			|| PairSecond != "array<fn()->void>")
		{
			return Fail("nested callable and generic type metadata must parse structurally");
		}
		const std::string HashPathPrefix =
			std::string("C:/cache#1/owner::part.vs") + '\x1F';
		const std::string HashPathMethod = HashPathPrefix + MakeMethodFunctionName(
			"ScopedReasonApi", "EndPlay", {"EReason::Type"});
		std::vector<std::string> HashPathParams;
		if (FindLinkageIdentityStart(HashPathMethod) != HashPathPrefix.size()
			|| MethodLinkageBareLeaf(HashPathMethod) != "EndPlay"
			|| !ModuleMeta::DecodeMethodParamTypeNames(HashPathMethod, HashPathParams)
			|| HashPathParams != std::vector<std::string>{"EReason::Type"})
		{
			return Fail("module path punctuation must not affect method linkage parsing");
		}
		const std::string HashPathFree = HashPathPrefix
			+ MakeFreeFunctionName("FreeEcho", {"EReason::Type"});
		if (FindFunctionSignatureMarker(HashPathFree)
				!= HashPathFree.find("#fn#", HashPathPrefix.size()))
		{
			return Fail("module path hash must not replace free-function signature marker");
		}
		const std::string GenericTypeLinkage =
			std::string("<unit>") + '\x1F' + "Box#13:EReason::Type";
		if (ModuleMeta::TypeSimpleName(GenericTypeLinkage) != "Box"
			|| !Runtime::IsMulticastDelegateTypeName(
				std::string("<unit>") + '\x1F'
					+ "multicast_delegate#13:EReason::Type")
			|| !Runtime::IsMulticastDelegateTypeName(
				"multicast_delegate<EReason::Type>")
			|| Runtime::IsMulticastDelegateTypeName("MulticastDelegate")
			|| Runtime::IsMulticastDelegateTypeName("MulticastDelegateOneParam<int>")
			|| Runtime::IsMulticastDelegateTypeName("MulticastDelegateTwoParams<int,int>"))
		{
			return Fail("scoped generic arguments must not replace their outer type name");
		}
		std::vector<std::string> MalformedParams;
		if (ModuleMeta::DecodeMethodParamTypeNames(
				"ScopedReasonApi::Broken#999999999999999999999999999999:x",
				MalformedParams)
			|| !MalformedParams.empty())
		{
			return Fail("overflowing parameter type length must be rejected");
		}
		// Host-generated metadata may retain the declared scoped enum spelling in
		// linkage keys. Rebuild those three keys exactly as a host/UE catalog does,
		// while keeping the compiler-produced bytecode bodies and arity aliases.
		const auto RewriteFunctionKey = [&Compile](
			const std::string_view Prefix,
			const std::string& NewKey)
		{
			for (std::uint16_t Index = 0; Index < Compile.Module->Functions.size(); ++Index)
			{
				FBytecodeFunction& Function = Compile.Module->Functions[Index];
				const std::size_t PrefixPosition = Function.Name.find(Prefix);
				if (PrefixPosition == std::string::npos)
				{
					continue;
				}
				Compile.Module->FunctionIndices.erase(Function.Name);
				Function.Name = Function.Name.substr(0, PrefixPosition) + NewKey;
				Compile.Module->FunctionIndices[Function.Name] = Index;
				return true;
			}
			return false;
		};
		if (!RewriteFunctionKey(
				"ScopedReasonApi::EndPlay#",
				MakeMethodFunctionName(
					"ScopedReasonApi", "EndPlay", {"EReason::Type"}))
			|| !RewriteFunctionKey(
				"ScopedReasonApi::StaticEcho#",
				MakeMethodFunctionName(
					"ScopedReasonApi", "StaticEcho", {"EReason::Type"}))
			|| !RewriteFunctionKey(
				"FreeEcho#",
				MakeFreeFunctionName("FreeEcho", {"EReason::Type"})))
		{
			return Fail("locate scoped enum regression functions");
		}
		std::vector<FInstanceMethodInfo> AliasMethods;
		if (!ModuleMeta::EnumerateInstanceMethods(
				*Compile.Module,
				"EnumAliasApi",
				AliasMethods,
				true)
			|| AliasMethods.size() != 1
			|| AliasMethods[0].Name != "Echo"
			|| AliasMethods[0].ReturnTypeName
				!= "TEnumAsByte<ENamespace::Type>"
			|| AliasMethods[0].ParamTypeNames.size() != 1
			|| AliasMethods[0].ParamTypeNames[0]
				!= "TEnumAsByte<ENamespace::Type>")
		{
			return Fail("generic host alias method spelling should survive metadata");
		}
		std::vector<FInstanceMethodInfo> ReasonMethods;
		if (!ModuleMeta::EnumerateInstanceMethods(
				*Compile.Module,
				"ScopedReasonApi",
				ReasonMethods,
				true)
			|| ReasonMethods.size() != 1
			|| ReasonMethods[0].Name != "EndPlay"
			|| ReasonMethods[0].ReturnTypeName != "EReason::Type"
			|| ReasonMethods[0].ParamTypeNames
				!= std::vector<std::string>{"EReason::Type"})
		{
			std::cerr << "reason methods=" << ReasonMethods.size();
			for (const FInstanceMethodInfo& EnumeratedMethod : ReasonMethods)
			{
				std::cerr << " [" << EnumeratedMethod.Name << " ret="
					<< EnumeratedMethod.ReturnTypeName
					<< " params=";
				for (const std::string& Param : EnumeratedMethod.ParamTypeNames)
				{
					std::cerr << Param << ',';
				}
				std::cerr << ']';
			}
			std::cerr << '\n';
			return Fail("scoped enum parameter must not replace the instance method leaf");
		}
		bool bSawRpcAnnotation = false;
		for (const auto& Meta : ReasonMethods[0].DeclMeta)
		{
			bSawRpcAnnotation = bSawRpcAnnotation
				|| (Meta.first == "__annot" && Meta.second == "@rpc");
		}
		if (!bSawRpcAnnotation)
		{
			return Fail("scoped enum method must retain its annotation association");
		}
		std::vector<FStaticMethodInfo> ReasonStaticMethods;
		if (!ModuleMeta::EnumerateStaticMethods(
				*Compile.Module,
				"ScopedReasonApi",
				ReasonStaticMethods)
			|| ReasonStaticMethods.size() != 1
			|| ReasonStaticMethods[0].Name != "StaticEcho"
			|| ReasonStaticMethods[0].ReturnTypeName != "EReason::Type"
			|| ReasonStaticMethods[0].ParamTypeNames
				!= std::vector<std::string>{"EReason::Type"})
		{
			return Fail("scoped enum parameter must not replace the static method leaf");
		}
		std::vector<FFreeFunctionInfo> ReasonFreeFunctions;
		ModuleMeta::EnumerateFreeFunctions(*Compile.Module, ReasonFreeFunctions);
		bool bSawFreeEcho = false;
		for (const FFreeFunctionInfo& Function : ReasonFreeFunctions)
		{
			bSawFreeEcho = bSawFreeEcho
				|| (Function.Name == "FreeEcho"
					&& Function.ReturnTypeName == "EReason::Type"
					&& Function.ParamTypeNames
						== std::vector<std::string>{"EReason::Type"});
		}
		if (!bSawFreeEcho)
		{
			return Fail("scoped enum parameter must not affect free-function metadata");
		}
		std::uint16_t AliasFunctionIndex = 0;
		const FValue AliasArgument = std::int32_t{1};
		if (ModuleMeta::TryResolveInstanceMethodOnType(
				*Compile.Module,
				"EnumAliasApi",
				"Echo",
				1,
				AliasFunctionIndex,
				&AliasArgument)
			!= EMethodResolveStatus::Found)
		{
			return Fail("generic host alias method should resolve from its enum value");
		}
		const FValue ReasonArgument = FEnumValue{"EReason::Type", 1};
		std::uint16_t ReasonFunctionIndex = 0;
		if (ModuleMeta::TryResolveInstanceMethodOnType(
				*Compile.Module,
				"ScopedReasonApi",
				"EndPlay",
				1,
				ReasonFunctionIndex,
				&ReasonArgument)
			!= EMethodResolveStatus::Found)
		{
			return Fail("scoped enum instance method should resolve by its true name");
		}
		std::ostringstream CallOutput;
		VeroScriptVm CallVm(CallOutput);
		VeroScript::Test::Configure(CallVm);
		FObjectHandle ReasonObject;
		if (!CallVm.ConstructObject(
				*Compile.Module,
				"ScopedReasonApi",
				{},
				ReasonObject,
				nullptr,
				&CastRegistry.GetNativeFunctions()))
		{
			return Fail("construct scoped enum method receiver");
		}
		std::uint16_t VmReasonFunctionIndex = 0;
		if (!VeroScriptVm::TryResolveInstanceMethod(
				*Compile.Module,
				ReasonObject,
				"EndPlay",
				1,
				VmReasonFunctionIndex)
			|| VmReasonFunctionIndex != ReasonFunctionIndex)
		{
			return Fail("VM should resolve scoped enum method by its true name");
		}
		FValue ReasonReturn;
		if (!CallVm.ExecuteInstanceMethod(
				*Compile.Module,
				ReasonObject,
				"EndPlay",
				{ReasonArgument},
				nullptr,
				&CastRegistry.GetNativeFunctions(),
				&ReasonReturn))
		{
			std::cerr << CallVm.GetError().Message << '\n';
			return Fail("CallInstanceMethod should invoke scoped enum method");
		}
		const auto* const ReturnedReason = std::get_if<FEnumValue>(&ReasonReturn);
		if (!ReturnedReason
			|| ReturnedReason->TypeName != "EReason::Type"
			|| ReturnedReason->Value != 1)
		{
			return Fail("scoped enum method return value should preserve type and value");
		}
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		if (!Vm.Execute(
				*Compile.Module,
				ERunMode::Script,
				{},
				&CastRegistry.GetNativeFunctions()))
		{
			std::cerr << Vm.GetError().Message << '\n';
			return Fail("generic host cast script should execute");
		}
		if (Output.str() != "true\ntrue\ntrue\ntrue\ntrue\n1\n1\n1\nTypedEnum(1)\ntrue\n")
		{
			std::cerr << "generic host cast output: " << Output.str();
			return Fail("generic host cast should preserve success/failure semantics");
		}

		std::string Json;
		FHostBindingRegistry Imported;
		std::string ImportError;
		if (!ExportBindingCatalogJson(CastRegistry, Json)
			|| !ImportBindingCatalogJson(Json, Imported, ImportError))
		{
			std::cerr << ImportError << '\n';
			return Fail("generic host cast metadata should roundtrip");
		}
		const FHostFunctionDesc* CastDesc = Imported.FindFunction("Cast");
		const FHostTypeAliasDesc* AliasDesc =
			Imported.FindTypeAlias("TEnumAsByte");
		if (!CastDesc
			|| !CastDesc->bGenericHostObjectCast
			|| CastDesc->TypeParams != std::vector<std::string>{"T"} || CastDesc->ScriptReturnTypeName != "T"
			|| !AliasDesc
			|| AliasDesc->TypeParams != std::vector<std::string>{"T"} || AliasDesc->TargetTypeName != "T")
		{
			return Fail("generic host metadata should survive catalog roundtrip");
		}
	}

	{
		FHostBindingRegistry ScopedEnumRegistry;
		std::string ScopedError;
		FHostTypeId ColorId;
		if (!ScopedEnumRegistry.RegisterType(
				"HostColor", EHostTypeKind::Enum, ColorId, ScopedError)
			|| !ScopedEnumRegistry.SetEnumScoped(ColorId, true, ScopedError)
			|| !ScopedEnumRegistry.SetEnumUnderlyingType(
				ColorId, "int64", ScopedError))
		{
			return Fail("register scoped host enum");
		}
		if (ScopedEnumRegistry.SetEnumUnderlyingType(ColorId, "uint64", ScopedError))
		{
			return Fail("host enum ABI must reject uint64 until FEnumValue can preserve its full range");
		}
		for (const auto& [Name, Value] :
			std::vector<std::pair<const char*, std::int64_t>>{{"Red", 1}, {"Blue", 2}})
		{
			FHostMemberDesc Member;
			Member.Kind = EHostMemberKind::EnumMember;
			Member.Name = Name;
			Member.EnumValue = Value;
			if (!ScopedEnumRegistry.RegisterMember(ColorId, std::move(Member), ScopedError))
			{
				return Fail("register scoped host enum member");
			}
		}
		if (!ScopedEnumRegistry.RegisterNative(
				"consume_enum_map",
				FNativeTypeDesc::Scalar(ENativeType::Void),
				{{FNativeTypeDesc::MapOf(
					FNativeTypeDesc::Enum("HostColor"),
					FNativeTypeDesc::Scalar(ENativeType::Int)),
					0}},
				[](const FNativeCallContext& Context, FValue&, std::string& Error)
				{
					if (Context.ArgCount != 1 || !std::holds_alternative<FMapHandle>(Context.Args[0]))
					{
						Error = "expected typed enum map";
						return false;
					}
					return true;
				},
				nullptr,
				ScopedError))
		{
			std::cerr << ScopedError << '\n';
			return Fail("register native with scoped enum key metadata");
		}
		if (!ScopedEnumRegistry.RegisterNative(
				"consume_enum_multiset",
				FNativeTypeDesc::Scalar(ENativeType::Void),
				{{FNativeTypeDesc::MultiSetOf(
					FNativeTypeDesc::Enum("HostColor")),
					0}},
				[](const FNativeCallContext& Context, FValue&, std::string& Error)
				{
					const auto* const Bag =
								Context.ArgCount == 1
								? std::get_if<FMapHandle>(&Context.Args[0])
								: nullptr;
					if (!Bag || !*Bag || (*Bag)->EnumEntries.size() != 2)
					{
						Error = "expected typed enum multiset";
						return false;
					}
					return true;
				},
				nullptr,
				ScopedError)
			|| !ScopedEnumRegistry.RegisterNative(
				"consume_enum_ordered_multiset",
				FNativeTypeDesc::Scalar(ENativeType::Void),
				{{FNativeTypeDesc::OrderedMultiSetOf(
					FNativeTypeDesc::Enum("HostColor")),
					0}},
				[](const FNativeCallContext& Context, FValue&, std::string& Error)
				{
					const auto* const Bag =
								Context.ArgCount == 1
								? std::get_if<FOrderedMapHandle>(&Context.Args[0])
								: nullptr;
					if (!Bag || !*Bag || (*Bag)->EnumEntries.size() != 2
								|| (*Bag)->EnumKeyOrder.size() != 2)
					{
						Error = "expected typed enum ordered multiset";
						return false;
					}
					return true;
				},
				nullptr,
				ScopedError))
		{
			std::cerr << ScopedError << '\n';
			return Fail("register native with scoped enum multiset metadata");
		}

		FCompileOptions ScopedOptions = VeroScript::Test::MakeCompileOptions();
		ScopedOptions.Bindings = &ScopedEnumRegistry;
		const FCompileResult Compile = VeroScriptCompile::CompileSource(
			"void main() {\n"
			"  map<HostColor, int> values = map<HostColor, int>();\n"
			"  set<HostColor> seen = set<HostColor>();\n"
			"  HostColor castRed = cast<HostColor>(1);\n"
			"  auto maybeBlue = try_cast<HostColor>(2);\n"
			"  assert(castRed == HostColor.Red && maybeBlue.has_value() && maybeBlue.get() == HostColor.Blue);\n"
			"  values[HostColor.Red] = 7;\n"
			"  values[HostColor.Blue] = 9;\n"
			"  seen.add(HostColor.Red);\n"
			"  seen.add(HostColor.Blue);\n"
			"  multiset<HostColor> bag = {HostColor.Red, HostColor.Blue, HostColor.Red};\n"
			"  ordered_multiset<HostColor> orderedBag = {HostColor.Blue, HostColor.Red, HostColor.Blue};\n"
			"  consume_enum_map(values);\n"
			"  consume_enum_multiset(bag);\n"
			"  consume_enum_ordered_multiset(orderedBag);\n"
			"  print(values[HostColor.Red]);\n"
			"  print(values[HostColor.Blue]);\n"
			"  print(len(seen));\n"
			"}\n",
			"<scoped_host_enum_keys>",
			ScopedOptions);
		if (!Compile.bSuccess || !Compile.Module)
		{
			std::cerr << Compile.Error.Message << '\n';
			return Fail("scoped host enum should compile as map/set key");
		}
		const auto CompiledEnum = std::find_if(
			Compile.Module->Enums.begin(),
			Compile.Module->Enums.end(),
			[](const FBytecodeEnumDesc& Enum)
			{
				return Enum.Name == "HostColor";
			});
		if (CompiledEnum == Compile.Module->Enums.end()
			|| CompiledEnum->UnderlyingTypeName != "long")
		{
			return Fail("scoped host enum underlying type should enter bytecode metadata");
		}
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		if (!Vm.Execute(
				*Compile.Module,
				ERunMode::Script,
				{},
				&ScopedEnumRegistry.GetNativeFunctions())
			|| Output.str() != "7\n9\n2\n")
		{
			std::cerr << Vm.GetError().Message << " output=" << Output.str();
			return Fail("scoped host enum map/set keys should execute");
		}

		std::string Json;
		FHostBindingRegistry Imported;
		std::string ImportError;
		if (!ExportBindingCatalogJson(ScopedEnumRegistry, Json)
			|| !ImportBindingCatalogJson(Json, Imported, ImportError))
		{
			return Fail("scoped host enum catalog roundtrip");
		}
		const FHostTypeDesc* ImportedColor = Imported.FindType("HostColor");
		if (!ImportedColor || !ImportedColor->bScopedEnum
			|| ImportedColor->EnumUnderlyingTypeName != "long"
			|| Json.find("\"enumUnderlyingType\": \"long\"") == std::string::npos)
		{
			return Fail("scoped host enum ABI should survive catalog roundtrip");
		}
	}

	{
		FHostBindingRegistry CastRegistry;
		std::string CastError;
		std::size_t Calls = 0;
		if (!CastRegistry.RegisterGenericHostObjectCast("SelectCast", {"T", "U"}, "U",
			[](const FNativeCallContext& Context, FValue& Out, std::string& Error)
			{
				if (Context.ArgCount != 1 || Context.TypeArgumentCount != 2 || !Context.TypeArguments
					|| (Context.TypeArguments[0] != "CastA" && Context.TypeArguments[0] != "int")
					|| Context.TypeArguments[1] != "CastB")
				{
					Error = "generic cast lost its declared type argument order";
					return false;
				}
				++*static_cast<std::size_t*>(Context.UserData);
				Out = Context.Args[0];
				return true;
			}, &Calls, CastError))
		{
			return Fail("register multi-parameter generic cast");
		}
		const std::string Types =
			"export class CastA{public:int first(){return 11;}};"
			"export class CastB{public:int second(){return 22;}};"
			"export class CastDerived:public CastB{};";
		FCompileOptions CastOptions = VeroScript::Test::MakeCompileOptions();
		CastOptions.Bindings = &CastRegistry;
		const auto Compiled = VeroScriptCompile::CompileSource(Types +
			"void main(){CastB object=CastB();"
			"CastB typed=SelectCast<CastA,CastB>(object);"
			"auto inferred=SelectCast<CastA,CastB>(object);"
			"print(typed.second()+inferred.second());"
			"print(SelectCast<CastA,CastB>(object).second());"
			"print(SelectCast<int,CastB>(object).second());"
			"CastB empty=SelectCast<CastA,CastB>(nullptr);print(empty==nullptr);"
			"CastB child=SelectCast<CastA,CastB>(CastDerived());print(child.second());}",
			"<multi_parameter_host_cast>", CastOptions);
		if (!Compiled.bSuccess || !Compiled.Module)
		{
			std::cerr << Compiled.Error.Message << '\n';
			return Fail("generic cast must infer the declared return parameter");
		}
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		if (!Vm.Execute(*Compiled.Module, ERunMode::Script, {}, &CastRegistry.GetNativeFunctions())
			|| Output.str() != "44\n22\n22\ntrue\n22\n" || Calls != 6)
		{
			std::cerr << Vm.GetError().Message << " output=" << Output.str();
			return Fail("generic cast must pass every type argument and preserve its selected return type");
		}
		const auto WrongClass = VeroScriptCompile::CompileSource(Types +
			"void main(){SelectCast<CastA,CastB>(CastA());print(99);}",
			"<generic_host_cast_wrong_return>", CastOptions);
		if (!WrongClass.bSuccess || !WrongClass.Module)
		{
			std::cerr << WrongClass.Error.Message << '\n';
			return Fail("generic cast source and target classes must be checked independently");
		}
		std::ostringstream WrongOutput;
		VeroScriptVm WrongVm(WrongOutput);
		VeroScript::Test::Configure(WrongVm);
		if (WrongVm.Execute(*WrongClass.Module, ERunMode::Script, {}, &CastRegistry.GetNativeFunctions())
			|| WrongVm.GetError().Message.find("generic host cast returned an incompatible object") == std::string::npos
			|| !WrongOutput.str().empty() || Calls != 7)
		{
			std::cerr << WrongVm.GetError().Message << " output=" << WrongOutput.str();
			return Fail("generic cast must reject a callback result outside the declared class hierarchy");
		}

		std::vector<std::string> Parameters;
		std::string Arguments;
		for (std::size_t Index = 0; Index < 254; ++Index)
		{
			Parameters.push_back("T" + std::to_string(Index));
			if (Index != 0)
			{
				Arguments += ',';
			}
			Arguments += "CastB";
		}
		std::size_t LimitCalls = 0;
		const auto LimitCallback = [](const FNativeCallContext& Context, FValue& Out, std::string& Error)
		{
			if (Context.ArgCount != 1 || Context.TypeArgumentCount != 254 || !Context.TypeArguments
				|| std::any_of(Context.TypeArguments, Context.TypeArguments + Context.TypeArgumentCount,
					[](const auto& Type)
					{
						return Type != "CastB";
					}))
			{
				Error = "generic cast truncated its runtime argument count";
				return false;
			}
			++*static_cast<std::size_t*>(Context.UserData);
			Out = Context.Args[0];
			return true;
		};
		if (!CastRegistry.RegisterGenericHostObjectCast("CastAtLimit", Parameters, Parameters.back(),
			LimitCallback, &LimitCalls, CastError))
		{
			return Fail("register generic cast at the bytecode argument limit");
		}
		const auto AtLimit = VeroScriptCompile::CompileSource(Types +
			"void main(){CastB object=CastB();print(3+CastAtLimit<" + Arguments + ">(object).second());}",
			"<generic_host_cast_limit>", CastOptions);
		std::ostringstream LimitOutput;
		VeroScriptVm LimitVm(LimitOutput);
		VeroScript::Test::Configure(LimitVm);
		if (!AtLimit.bSuccess || !AtLimit.Module
			|| !LimitVm.Execute(*AtLimit.Module, ERunMode::Script, {}, &CastRegistry.GetNativeFunctions())
			|| LimitOutput.str() != "25\n" || LimitCalls != 1)
		{
			std::cerr << AtLimit.Error.Message << ' ' << LimitVm.GetError().Message << '\n';
			return Fail("255 native runtime arguments must preserve the caller operand prefix");
		}
		Parameters.push_back("T254");
		Arguments += ",CastB";
		if (!CastRegistry.RegisterGenericHostObjectCast("CastPastLimit", Parameters, Parameters.back(),
			LimitCallback, &LimitCalls, CastError))
		{
			return Fail("register generic cast beyond the bytecode call argument limit");
		}
		const auto PastLimit = VeroScriptCompile::CompileSource(Types +
			"void main(){CastB object=CastB();CastPastLimit<" + Arguments + ">(object);}",
			"<generic_host_cast_over_limit>", CastOptions);
		if (PastLimit.bSuccess || PastLimit.Error.Message.find("255") == std::string::npos)
		{
			return Fail("generic hidden arguments must not wrap the bytecode argument count");
		}

		FNativeFunction Malformed;
		Malformed.Name = "MissingCastSource";
		Malformed.ReturnType = ENativeType::HostObject;
		Malformed.HostTypeName = "object";
		Malformed.ReturnHostTypeName = "object";
		Malformed.TypeParams = {"T"};
		Malformed.ScriptReturnTypeName = "T";
		Malformed.bHostStaticMethod = true;
		Malformed.bGenericHostObjectCast = true;
		Malformed.Callback = [](const FNativeCallContext&, FValue& Out, std::string&)
		{
			Out = FHostObjectHandle{};
			return true;
		};
		const std::vector<FNativeFunction> MalformedNatives{Malformed};
		FCompileOptions MalformedOptions = VeroScript::Test::MakeCompileOptions();
		MalformedOptions.Natives = &MalformedNatives;
		const auto Invalid = VeroScriptCompile::CompileSource(Types +
			"void main(){MissingCastSource<CastA>();}", "<invalid_generic_host_cast>", MalformedOptions);
		if (Invalid.bSuccess || Invalid.Error.MessageId != "diag.hostbinding_generic_cast_has_invalid_signature")
		{
			return Fail("malformed direct native casts must fail before indexing their source argument");
		}
	}
