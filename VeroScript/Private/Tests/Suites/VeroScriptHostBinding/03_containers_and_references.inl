// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Internal fragment: VeroScriptHostBindingTests / containers and references suite. Included by the owning translation unit.

{
	FHostBindingRegistry ArrayRegistry;
	if (!ArrayRegistry.RegisterNative(
				"expect_eq",
				ENativeType::Void,
			{ENativeType::Int, ENativeType::Int},
				[](const FNativeCallContext& Context, FValue&, std::string& OutError) -> bool
				{
					const auto A = std::get<std::int32_t>(Context.Args[0]);
					const auto B = std::get<std::int32_t>(Context.Args[1]);
					if (A != B)
					{
						OutError = "expect_eq failed: "
										+ std::to_string(A) + " != " + std::to_string(B);
						return false;
					}
					return true;
				},
				nullptr,
				ErrorId))
	{
		return Fail("RegisterNative expect_eq should succeed");
	}
	if (!ArrayRegistry.RegisterNative(
				"sum_ints",
				ENativeType::Int,
			{ENativeType::Array},
				[](const FNativeCallContext& Context, FValue& OutReturn, std::string& OutError) -> bool
				{
					const auto* const Handle =
									std::get_if<FDynamicArrayHandle>(&Context.Args[0]);
					if (!Handle || !*Handle)
					{
						OutError = "expected array<int>";
						return false;
					}
					std::int32_t Sum = 0;
					for (const FValue& Element : (*Handle)->Elements)
					{
						Sum += std::get<std::int32_t>(Element);
					}
					OutReturn = Sum;
					return true;
				},
				nullptr,
				ErrorId,
			{},
			{},
			{},
			{"int"}))
	{
		std::cerr << ErrorId << '\n';
		return Fail("RegisterNative sum_ints should succeed");
	}
	if (!ArrayRegistry.RegisterNative(
				"tag_strings",
				ENativeType::Array,
			{ENativeType::Array},
				[](const FNativeCallContext& Context, FValue& OutReturn, std::string& OutError) -> bool
				{
					const auto* const Handle =
									std::get_if<FDynamicArrayHandle>(&Context.Args[0]);
					if (!Handle || !*Handle)
					{
						OutError = "expected array<string>";
						return false;
					}
					auto Result = std::make_shared<FDynamicArray>();
					Result->ElementTypeName = "string";
					Result->TypeName = "array<string>";
					std::string Joined;
					for (const FValue& Element : (*Handle)->Elements)
					{
						Joined += std::get<std::string>(Element);
						Joined.push_back('|');
					}
					Result->Elements.push_back(Joined);
					OutReturn = std::move(Result);
					return true;
				},
				nullptr,
				ErrorId,
			{},
			{},
				"string",
			{"string"}))
	{
		std::cerr << ErrorId << '\n';
		return Fail("RegisterNative tag_strings should succeed");
	}
	FHostTypeId WidgetId;
	if (!ArrayRegistry.RegisterType("Widget", EHostTypeKind::Class, WidgetId, ErrorId))
	{
		return Fail("RegisterType Widget should succeed");
	}
	FHostMemberDesc WidgetCtor;
	WidgetCtor.Kind = EHostMemberKind::Constructor;
	WidgetCtor.Callback =
			[](const FNativeCallContext&, FValue& OutReturn, std::string&) -> bool
			{
				OutReturn = MakeHostObject(
							1,
							"Widget",
							new int{0},
							[](void* P)
							{
								delete static_cast<int*>(P);
							});
				return true;
			};
	if (!ArrayRegistry.RegisterMember(WidgetId, WidgetCtor, ErrorId))
	{
		return Fail("RegisterMember Widget ctor should succeed");
	}
	if (!ArrayRegistry.RegisterNative(
				"count_widgets",
				ENativeType::Int,
			{ENativeType::Array},
				[](const FNativeCallContext& Context, FValue& OutReturn, std::string& OutError) -> bool
				{
					const auto* const Handle =
									std::get_if<FDynamicArrayHandle>(&Context.Args[0]);
					if (!Handle || !*Handle)
					{
						OutError = "expected array<Widget>";
						return false;
					}
					std::int32_t Count = 0;
					for (const FValue& Element : (*Handle)->Elements)
					{
						const auto* const Host = std::get_if<FHostObjectHandle>(&Element);
						if (!Host || !*Host || (*Host)->TypeName != "Widget")
						{
							OutError = "element is not Widget";
							return false;
						}
						++Count;
					}
					OutReturn = Count;
					return true;
				},
				nullptr,
				ErrorId,
			{},
			{},
			{},
			{"Widget"}))
	{
		std::cerr << ErrorId << '\n';
		return Fail("RegisterNative count_widgets should succeed");
	}

	// Reject Array without element type name.
	if (ArrayRegistry.RegisterNative(
				"bad_array",
				ENativeType::Array,
			{},
				[](const FNativeCallContext&, FValue&, std::string&)
				{
					return true;
				},
				nullptr,
				ErrorId))
	{
		return Fail("Array return without element type should fail");
	}

	FCompileOptions ArrayOptions = VeroScript::Test::MakeCompileOptions();
	ArrayOptions.Bindings = &ArrayRegistry;
	const FCompileResult ArrayCompile = VeroScriptCompile::CompileSource(
			R"(
void main() {
	array<int> nums = array<int>();
	nums.push(1);
	nums.push(2);
	nums.push(3);
	expect_eq(sum_ints(nums), 6);
	expect_eq(len(nums), 3);

	array<string> words = array<string>();
	words.push("a");
	words.push("b");
	array<string> tagged = tag_strings(words);
	expect_eq(len(tagged), 1);

	array<Widget> widgets = array<Widget>();
	Widget direct_widget();
	widgets.push(direct_widget);
	widgets.push(Widget());
	expect_eq(count_widgets(widgets), 2);
}
)",
			"<native_array_abi>",
			ArrayOptions);
	if (!ArrayCompile.bSuccess || !ArrayCompile.Module)
	{
		std::cerr << "array ABI compile failed: " << ArrayCompile.Error.Message << '\n';
		return Fail("Array CallNative script should compile");
	}
	VeroScriptVm ArrayVm(std::cout);
		VeroScript::Test::Configure(ArrayVm);
	if (!ArrayVm.Execute(
				*ArrayCompile.Module,
				ERunMode::Script,
			{},
					&ArrayRegistry.GetNativeFunctions()))
	{
		std::cerr << "array ABI run failed: " << ArrayVm.GetError().Message << '\n';
		return Fail("Array CallNative round-trip should run");
	}

	// Catalog round-trip preserves Array element metadata.
	std::string CatalogJson;
	if (!ExportBindingCatalogJson(ArrayRegistry, CatalogJson))
	{
		return Fail("ExportBindingCatalogJson with Array should succeed");
	}
	FHostBindingRegistry Imported;
	std::string ImportError;
	if (!ImportBindingCatalogJson(CatalogJson, Imported, ImportError))
	{
		std::cerr << ImportError << '\n';
		return Fail("ImportBindingCatalogJson with Array should succeed");
	}
	bool bFoundSum = false;
	for (const FHostFunctionDesc& Fn : Imported.GetFunctions())
	{
		if (Fn.Name == "sum_ints")
		{
			bFoundSum = true;
			if (Fn.ParamTypes.size() != 1
					|| Fn.ParamTypes[0] != ENativeType::Array
					|| Fn.ParamElementTypeNames.size() < 1
					|| Fn.ParamElementTypeNames[0] != "int")
			{
				return Fail("imported sum_ints should keep array<int> element metadata");
			}
		}
	}
	if (!bFoundSum)
	{
		return Fail("imported catalog should contain sum_ints");
	}

	// `unsigned` host alias must match script `unsigned int` element names.
	if (!ArrayRegistry.RegisterNative(
				"sum_unsigned",
				ENativeType::Int,
			{ENativeType::Array},
				[](const FNativeCallContext& Context, FValue& OutReturn, std::string& OutError) -> bool
				{
					const auto* const Handle =
									std::get_if<FDynamicArrayHandle>(&Context.Args[0]);
					if (!Handle || !*Handle)
					{
						OutError = "expected array<unsigned int>";
						return false;
					}
					std::int32_t Sum = 0;
					for (const FValue& Element : (*Handle)->Elements)
					{
						if (const auto* U = std::get_if<std::uint32_t>(&Element))
						{
							Sum += static_cast<std::int32_t>(*U);
						}
						else if (const auto* I = std::get_if<std::int32_t>(&Element))
						{
							Sum += *I;
						}
						else
						{
							OutError = "expected unsigned elements";
							return false;
						}
					}
					OutReturn = Sum;
					return true;
				},
				nullptr,
				ErrorId,
			{},
			{},
			{},
			{"unsigned"}))
	{
		std::cerr << ErrorId << '\n';
		return Fail("RegisterNative sum_unsigned should accept 'unsigned' alias");
	}
	{
		bool bFound = false;
		for (const FNativeFunction& Fn : ArrayRegistry.GetNativeFunctions())
		{
			if (Fn.Name == "sum_unsigned")
			{
				bFound = true;
				if (Fn.ParamElementTypeNames.empty()
						|| Fn.ParamElementTypeNames[0] != "unsigned int")
				{
					return Fail("sum_unsigned should canonicalize element type to 'unsigned int'");
				}
			}
		}
		if (!bFound)
		{
			return Fail("sum_unsigned should be registered");
		}
	}

	// Returning a shared array must deep-copy; metadata stamp must not mutate the host original.
	auto HostSharedArray = std::make_shared<FDynamicArray>();
	HostSharedArray->TypeName = "array";
	HostSharedArray->ElementTypeName.clear();
	HostSharedArray->Elements.push_back(std::uint32_t{3});
	HostSharedArray->Elements.push_back(std::uint32_t{4});
	if (!ArrayRegistry.RegisterNative(
				"make_unsigned_array",
				ENativeType::Array,
			{},
				[](const FNativeCallContext& Context, FValue& OutReturn, std::string&) -> bool
				{
					OutReturn = *static_cast<FDynamicArrayHandle*>(Context.UserData);
					return true;
				},
				&HostSharedArray,
				ErrorId,
			{},
			{},
				"unsigned"))
	{
		std::cerr << ErrorId << '\n';
		return Fail("RegisterNative make_unsigned_array should succeed");
	}

	FCompileOptions UnsignedOptions = VeroScript::Test::MakeCompileOptions();
	UnsignedOptions.Bindings = &ArrayRegistry;
	const FCompileResult UnsignedCompile = VeroScriptCompile::CompileSource(
			R"(
void main() {
	array<unsigned int> xs = array<unsigned int>();
	xs.push(1u);
	xs.push(2u);
	expect_eq(sum_unsigned(xs), 3);

	array<unsigned int> ys = make_unsigned_array();
	expect_eq(len(ys), 2);
	expect_eq(sum_unsigned(ys), 7);
}
)",
			"<native_array_unsigned>",
			UnsignedOptions);
	if (!UnsignedCompile.bSuccess || !UnsignedCompile.Module)
	{
		std::cerr << "unsigned array ABI compile failed: "
				<< UnsignedCompile.Error.Message << '\n';
		return Fail("unsigned Array CallNative script should compile");
	}
	VeroScriptVm UnsignedVm(std::cout);
		VeroScript::Test::Configure(UnsignedVm);
	if (!UnsignedVm.Execute(
				*UnsignedCompile.Module,
				ERunMode::Script,
			{},
				&ArrayRegistry.GetNativeFunctions()))
	{
		std::cerr << "unsigned array ABI run failed: "
				<< UnsignedVm.GetError().Message << '\n';
		return Fail("unsigned Array CallNative round-trip should run");
	}
	if (!HostSharedArray->ElementTypeName.empty()
			|| HostSharedArray->TypeName != "array")
	{
		return Fail(
				"CallNative must not stamp Array metadata onto the host's shared array");
	}

	// Mutable ref shares the handle; by-value / const ref keep isolation.
	if (!ArrayRegistry.RegisterNative(
				"bump_first",
				ENativeType::Void,
			{ENativeType::Array},
				[](const FNativeCallContext& Context, FValue&, std::string& OutError) -> bool
				{
					const auto* const Handle =
									std::get_if<FDynamicArrayHandle>(&Context.Args[0]);
					if (!Handle || !*Handle || (*Handle)->Elements.empty())
					{
						OutError = "expected non-empty array<int>";
						return false;
					}
					auto* const IntValue = std::get_if<std::int32_t>(&(*Handle)->Elements[0]);
					if (!IntValue)
					{
						OutError = "expected int element";
						return false;
					}
					*IntValue += 1;
					return true;
				},
				nullptr,
				ErrorId,
			{},
			{},
			{},
			{"int"},
			{NativeParamFlagRef}))
	{
		std::cerr << ErrorId << '\n';
		return Fail("RegisterNative bump_first (mutable ref) should succeed");
	}
	if (!ArrayRegistry.RegisterNative(
				"bump_first_const",
				ENativeType::Void,
			{ENativeType::Array},
				[](const FNativeCallContext& Context, FValue&, std::string& OutError) -> bool
				{
					const auto* const Handle =
									std::get_if<FDynamicArrayHandle>(&Context.Args[0]);
					if (!Handle || !*Handle || (*Handle)->Elements.empty())
					{
						OutError = "expected non-empty array<int>";
						return false;
					}
					auto* const IntValue = std::get_if<std::int32_t>(&(*Handle)->Elements[0]);
					if (!IntValue)
					{
						OutError = "expected int element";
						return false;
					}
					*IntValue += 1;
					return true;
				},
				nullptr,
				ErrorId,
			{},
			{},
			{},
			{"int"},
			{static_cast<std::uint8_t>(NativeParamFlagRef | NativeParamFlagConstRef)}))
	{
		std::cerr << ErrorId << '\n';
		return Fail("RegisterNative bump_first_const should succeed");
	}
	if (!ArrayRegistry.RegisterNative(
				"bump_ref_int",
				ENativeType::Void,
			{ENativeType::Int},
				[](const FNativeCallContext& Context, FValue&, std::string& OutError)
				{
					FValue* const Mutable = Context.MutableArg(0);
					auto* const Value = Mutable ? std::get_if<std::int32_t>(Mutable) : nullptr;
					if (!Value)
					{
						OutError = "missing mutable int argument";
						return false;
					}
					*Value += 7;
					return true;
				},
				nullptr,
				ErrorId,
			{},
			{},
			{},
			{},
			{NativeParamFlagRef}))
	{
		std::cerr << ErrorId << '\n';
		return Fail("RegisterNative ref on Int should succeed");
	}
	if (!ArrayRegistry.RegisterNative(
				"set_blue",
				FNativeTypeDesc::Scalar(ENativeType::Void),
			{FNativeParamDesc{
				FNativeTypeDesc::Enum("Color"),
					NativeParamFlagRef}},
				[](const FNativeCallContext& Context, FValue&, std::string& OutError)
				{
					FValue* const Mutable = Context.MutableArg(0);
					auto* const Value = Mutable ? std::get_if<FEnumValue>(Mutable) : nullptr;
					if (!Value || Value->TypeName != "Color")
					{
						OutError = "missing mutable Color argument";
						return false;
					}
					Value->Value = 2;
					return true;
				},
				nullptr,
				ErrorId))
	{
		std::cerr << ErrorId << '\n';
		return Fail("structured native enum ref registration should succeed");
	}
	if (!ArrayRegistry.RegisterNative(
				"echo_color",
				FNativeTypeDesc::Enum("Color"),
			{FNativeParamDesc{FNativeTypeDesc::Enum("Color"), 0}},
				[](const FNativeCallContext& Context, FValue& Out, std::string& OutError)
				{
					if (Context.MutableArgs != nullptr || Context.MutableArg(0) != nullptr)
					{
						OutError = "by-value native should not allocate mutable argument slots";
						return false;
					}
					Out = Context.Args[0];
					return true;
				},
				nullptr,
				ErrorId))
	{
		std::cerr << ErrorId << '\n';
		return Fail("structured native enum return registration should succeed");
	}
	if (ArrayRegistry.RegisterNative(
				"bad_unknown_array_flag",
				ENativeType::Void,
			{ENativeType::Array},
				[](const FNativeCallContext&, FValue&, std::string&)
				{
					return true;
				},
				nullptr,
				ErrorId,
			{},
			{},
			{},
			{"int"},
			{std::uint8_t{0x80}}))
	{
		return Fail("RegisterNative unknown ParamFlags bits should fail");
	}
	if (ErrorId != "diag.hostbinding_param_flags_unknown_bits")
	{
		return Fail("unknown ParamFlags bits should report the dedicated diagnostic");
	}
	if (ArrayRegistry.RegisterNative(
				"bad_const_without_ref",
				ENativeType::Void,
			{ENativeType::Array},
				[](const FNativeCallContext&, FValue&, std::string&)
				{
					return true;
				},
				nullptr,
				ErrorId,
			{},
			{},
			{},
			{"int"},
			{NativeParamFlagConstRef}))
	{
		return Fail("RegisterNative const-ref without ref bit should fail");
	}
	if (ErrorId != "diag.hostbinding_param_flags_const_ref_requires_ref")
	{
		return Fail("const-ref without ref bit should report the dedicated diagnostic");
	}

	// Direct native tables bypass FHostBindingRegistry, so both compile and
	// execution boundaries must still reject malformed public metadata.
	FNativeFunction RawNative;
	RawNative.Name = "raw_array";
	RawNative.ReturnType = ENativeType::Void;
	RawNative.ParamTypes = {ENativeType::Array};
	RawNative.ParamElementTypeNames = {"int"};
	RawNative.Callback =
			[](const FNativeCallContext&, FValue&, std::string&)
			{
				return true;
			};
	std::vector<FNativeFunction> RawNatives{RawNative};
	FCompileOptions RawOptions = VeroScript::Test::MakeCompileOptions();
	RawOptions.Natives = &RawNatives;
	const std::string RawSource =
			"void main() { array<int> a = array<int>(); raw_array(a); }\n";
	const FCompileResult RawCompile = VeroScriptCompile::CompileSource(
			RawSource, "<raw_native_flags_valid>", RawOptions);
	if (!RawCompile.bSuccess || !RawCompile.Module)
	{
		return Fail("well-formed direct native metadata should compile");
	}

	RawNatives[0].ParamFlags = {std::uint8_t{0x80}};
	const FCompileResult RawInvalidCompile = VeroScriptCompile::CompileSource(
			RawSource, "<raw_native_flags_invalid>", RawOptions);
	if (RawInvalidCompile.bSuccess
			|| RawInvalidCompile.Error.MessageId != "diag.invalid_native_parameter_flags")
	{
		return Fail("compiler should reject malformed direct native ParamFlags");
	}
	VeroScriptVm RawVm(std::cout);
		VeroScript::Test::Configure(RawVm);
	if (RawVm.Execute(*RawCompile.Module, ERunMode::Script, {}, &RawNatives))
	{
		return Fail("VM should reject malformed direct native ParamFlags");
	}
	if (RawVm.GetError().Message.find("invalid native parameter flags")
			== std::string::npos)
	{
		return Fail("VM should report malformed direct native ParamFlags clearly");
	}

	RawNatives[0].ParamFlags.clear();
	RawNatives[0].ParamElementTypeNames.clear();
	const FCompileResult RawMissingContainerCompile =
			VeroScriptCompile::CompileSource(
				RawSource, "<raw_native_container_metadata_invalid>", RawOptions);
	if (RawMissingContainerCompile.bSuccess
			|| RawMissingContainerCompile.Error.MessageId
				!= "diag.invalid_native_container_metadata")
	{
		return Fail("compiler should reject incomplete direct native container metadata");
	}
	VeroScriptVm RawContainerVm(std::cout);
		VeroScript::Test::Configure(RawContainerVm);
	if (RawContainerVm.Execute(
				*RawCompile.Module, ERunMode::Script, {}, &RawNatives))
	{
		return Fail("VM should reject incomplete direct native container metadata");
	}
	if (RawContainerVm.GetError().Message.find("invalid native container metadata")
			== std::string::npos)
	{
		return Fail("VM should report incomplete direct native container metadata clearly");
	}

	RawNatives[0].ParamElementTypeNames = {"int"};
	RawNatives[0].RuntimeReturnTypeName = "void";
	RawNatives[0].RuntimeParamTypeNames = {"int"};
	const FCompileResult RawMismatchedRuntimeCompile =
			VeroScriptCompile::CompileSource(
				RawSource, "<raw_native_runtime_metadata_mismatch>", RawOptions);
	if (RawMismatchedRuntimeCompile.bSuccess
			|| RawMismatchedRuntimeCompile.Error.MessageId
				!= "diag.invalid_native_container_metadata")
	{
		return Fail("compiler should reject runtime metadata that disagrees with ENativeType");
	}
	VeroScriptVm RawRuntimeMetadataVm(std::cout);
		VeroScript::Test::Configure(RawRuntimeMetadataVm);
	if (RawRuntimeMetadataVm.Execute(
				*RawCompile.Module, ERunMode::Script, {}, &RawNatives))
	{
		return Fail("VM should reject runtime metadata that disagrees with ENativeType");
	}
	RawNatives[0].RuntimeParamTypeNames = {"array<fn(int,string)->bool>"};
	if (!NativeFunctionContainerMetadataIsValid(RawNatives[0]))
	{
		return Fail("nested callable parameter commas must not split outer container metadata");
	}
	RawNatives[0].RuntimeReturnTypeName.clear();
	RawNatives[0].RuntimeParamTypeNames.clear();

	RawNatives[0].ParamFlags = {NativeParamFlagConstRef};
	if (NativeParamFlagsAreValid(RawNatives[0]))
	{
		return Fail("direct native const-ref without ref should be invalid");
	}
	RawNatives[0].ParamTypes = {ENativeType::Int};
	RawNatives[0].ParamFlags = {NativeParamFlagRef};
	if (!NativeParamFlagsAreValid(RawNatives[0]))
	{
		return Fail("direct native ref on scalar should be valid");
	}
	RawNatives[0].ParamFlags = {0, 0};
	if (NativeParamFlagsAreValid(RawNatives[0]))
	{
		return Fail("direct native ParamFlags size mismatch should be invalid");
	}

	FCompileOptions RefOptions = VeroScript::Test::MakeCompileOptions();
	RefOptions.Bindings = &ArrayRegistry;
	const FCompileResult RefCompile = VeroScriptCompile::CompileSource(
			R"(
enum class Color { Red = 1, Blue = 2 }
void main() {
	Color color = Color::Red;
	set_blue(ref color);
	Color echoed = echo_color(color);
	if (color != Color::Blue || echoed != Color::Blue) {
		int zero = 0;
		int fail = 1 / zero;
	}

	int scalar = 5;
	bump_ref_int(ref scalar);
	expect_eq(scalar, 12);

	array<int> a = array<int>();
	a.push(10);
	bump_first(ref a);
	expect_eq(a[0], 11);

	array<int> b = array<int>();
	b.push(10);
	bump_first_const(ref b);
	expect_eq(b[0], 10);

	array<int> c = array<int>();
	c.push(10);
	bump_first(c);
	expect_eq(c[0], 11);
}
)",
			"<native_array_ref>",
			RefOptions);
	if (!RefCompile.bSuccess || !RefCompile.Module)
	{
		std::cerr << "array ref ABI compile failed: " << RefCompile.Error.Message << '\n';
		return Fail("Array ref CallNative script should compile");
	}
	VeroScriptVm RefVm(std::cout);
		VeroScript::Test::Configure(RefVm);
	if (!RefVm.Execute(
				*RefCompile.Module,
				ERunMode::Script,
			{},
				&ArrayRegistry.GetNativeFunctions()))
	{
		std::cerr << "array ref ABI run failed: " << RefVm.GetError().Message << '\n';
		return Fail("Array ref CallNative round-trip should run");
	}

	{
		const FCompileResult RejectRef = VeroScriptCompile::CompileSource(
				R"(
void main() {
	array<int> a = array<int>();
	a.push(1);
	sum_ints(ref a);
}
)",
				"<native_array_ref_reject>",
				RefOptions);
		if (RejectRef.bSuccess)
		{
			return Fail("by-value Array native should reject ref arguments");
		}
	}

	{
		bool bFound = false;
		for (const FHostFunctionDesc& Fn : ArrayRegistry.GetFunctions())
		{
			if (Fn.Name == "bump_first")
			{
				bFound = true;
				if (Fn.ParamFlags.size() != 1
						|| Fn.ParamFlags[0] != NativeParamFlagRef)
				{
					return Fail("catalog Desc should keep ParamFlags for bump_first");
				}
			}
		}
		if (!bFound)
		{
			return Fail("GetFunctions should include bump_first");
		}
	}
}

{
	FHostBindingRegistry MapSetRegistry;
	std::string MapErrorId;
	if (!NativeElementTypeNamesEqual(
				"array<map<int, unsigned>>",
				"array<map<int,unsigned int>>"))
	{
		return Fail("nested native container aliases should canonicalize recursively");
	}
	if (!MapSetRegistry.RegisterNative(
				"expect_eq",
				ENativeType::Void,
			{ENativeType::Int, ENativeType::Int},
				[](const FNativeCallContext& Context, FValue&, std::string& OutError) -> bool
				{
					const auto A = std::get<std::int32_t>(Context.Args[0]);
					const auto B = std::get<std::int32_t>(Context.Args[1]);
					if (A != B)
					{
						OutError = "expect_eq failed: "
										+ std::to_string(A) + " != " + std::to_string(B);
						return false;
					}
					return true;
				},
				nullptr,
				MapErrorId))
	{
		return Fail("RegisterNative expect_eq (map/set) should succeed");
	}

	if (!MapSetRegistry.RegisterNative(
				"map_put_ref",
				ENativeType::Void,
			{ENativeType::Map, ENativeType::Int, ENativeType::Int},
				[](const FNativeCallContext& Context, FValue&, std::string& OutError) -> bool
				{
					const auto* const Handle = std::get_if<FMapHandle>(&Context.Args[0]);
					if (!Handle || !*Handle)
					{
						OutError = "expected map<int,int>";
						return false;
					}
					const auto Key = std::get<std::int32_t>(Context.Args[1]);
					const auto Value = std::get<std::int32_t>(Context.Args[2]);
					(*Handle)->IntEntries[Key] = Value;
					return true;
				},
				nullptr,
				MapErrorId,
			{},
			{},
			{},
			{},
			{NativeParamFlagRef, 0, 0},
			{},
			{},
			{"int"},
			{"int"}))
	{
		std::cerr << MapErrorId << '\n';
		return Fail("RegisterNative map_put_ref should succeed");
	}

	if (!MapSetRegistry.RegisterNative(
				"map_put_by_value",
				ENativeType::Void,
			{ENativeType::Map, ENativeType::Int, ENativeType::Int},
				[](const FNativeCallContext& Context, FValue&, std::string& OutError) -> bool
				{
					const auto* const Handle = std::get_if<FMapHandle>(&Context.Args[0]);
					if (!Handle || !*Handle)
					{
						OutError = "expected map<int,int>";
						return false;
					}
					const auto Key = std::get<std::int32_t>(Context.Args[1]);
					const auto Value = std::get<std::int32_t>(Context.Args[2]);
					(*Handle)->IntEntries[Key] = Value;
					return true;
				},
				nullptr,
				MapErrorId,
			{},
			{},
			{},
			{},
			{},
			{},
			{},
			{"int"},
			{"int"}))
	{
		std::cerr << MapErrorId << '\n';
		return Fail("RegisterNative map_put_by_value should succeed");
	}
	if (!MapSetRegistry.RegisterNative(
				"corrupt_map_ref",
				ENativeType::Void,
			{ENativeType::Map},
				[](const FNativeCallContext& Context, FValue&, std::string&) -> bool
				{
					const auto* const Handle = std::get_if<FMapHandle>(&Context.Args[0]);
					if (!Handle || !*Handle)
					{
						return false;
					}
					(*Handle)->StrEntries["wrong-bucket"] = std::int32_t{1};
					return true;
				},
				nullptr,
				MapErrorId,
			{},
			{},
			{},
			{},
			{NativeParamFlagRef},
			{},
			{},
			{"int"},
			{"int"}))
	{
		return Fail("RegisterNative corrupt_map_ref fixture should succeed");
	}

	if (!MapSetRegistry.RegisterNative(
				"set_add_ref",
				ENativeType::Void,
			{ENativeType::Set, ENativeType::Int},
				[](const FNativeCallContext& Context, FValue&, std::string& OutError) -> bool
				{
					const auto* const Handle = std::get_if<FSetHandle>(&Context.Args[0]);
					if (!Handle || !*Handle)
					{
						OutError = "expected set<int>";
						return false;
					}
					(*Handle)->IntElements.insert(std::get<std::int32_t>(Context.Args[1]));
					return true;
				},
				nullptr,
				MapErrorId,
			{},
			{},
			{},
			{"int"},
			{NativeParamFlagRef, 0}))
	{
		std::cerr << MapErrorId << '\n';
		return Fail("RegisterNative set_add_ref should succeed");
	}
	if (!MapSetRegistry.RegisterNative(
				"inspect_char_containers",
				FNativeTypeDesc::Scalar(ENativeType::Void),
			{{FNativeTypeDesc::MapOf(
						FNativeTypeDesc::Scalar(ENativeType::Int8),
						FNativeTypeDesc::Scalar(ENativeType::Int))},
				{FNativeTypeDesc::OrderedMapOf(
						FNativeTypeDesc::Scalar(ENativeType::Int8),
						FNativeTypeDesc::Scalar(ENativeType::Int))},
				{FNativeTypeDesc::SetOf(
						FNativeTypeDesc::Scalar(ENativeType::Int8))}},
				[](const FNativeCallContext& Context, FValue&, std::string& OutError) -> bool
				{
					const auto* const Map = std::get_if<FMapHandle>(&Context.Args[0]);
					const auto* const Ordered =
									std::get_if<FOrderedMapHandle>(&Context.Args[1]);
					const auto* const Set = std::get_if<FSetHandle>(&Context.Args[2]);
					if (!Map || !*Map || (*Map)->IntEntries.count(65) != 1
									|| !Ordered || !*Ordered || (*Ordered)->IntEntries.count(66) != 1
									|| (*Ordered)->IntKeyOrder != std::vector<std::int32_t>{66} || !Set || !*Set || (*Set)->IntElements.count(-1) != 1)
					{
						OutError = "char container storage was not preserved at native boundary";
						return false;
					}
					return true;
				},
				nullptr,
				MapErrorId))
	{
		std::cerr << MapErrorId << '\n';
		return Fail("RegisterNative inspect_char_containers should succeed");
	}

	if (!MapSetRegistry.RegisterNative(
				"bump_nested",
				ENativeType::Void,
			{ENativeType::Array},
				[](const FNativeCallContext& Context, FValue&, std::string& OutError) -> bool
				{
					const auto* const Outer = std::get_if<FDynamicArrayHandle>(&Context.Args[0]);
					if (!Outer || !*Outer || (*Outer)->Elements.empty())
					{
						OutError = "expected non-empty array<array<int>>";
						return false;
					}
					auto* const Inner = std::get_if<FDynamicArrayHandle>(&(*Outer)->Elements[0]);
					if (!Inner || !*Inner || (*Inner)->Elements.empty())
					{
						OutError = "expected non-empty inner array<int>";
						return false;
					}
					auto* const IntValue = std::get_if<std::int32_t>(&(*Inner)->Elements[0]);
					if (!IntValue)
					{
						OutError = "expected int";
						return false;
					}
					*IntValue += 10;
					return true;
				},
				nullptr,
				MapErrorId,
			{},
			{},
			{},
			{"array<int>"},
			{NativeParamFlagRef}))
	{
		std::cerr << MapErrorId << '\n';
		return Fail("RegisterNative bump_nested should succeed");
	}

	if (!MapSetRegistry.RegisterNative(
				"bump_nested_by_value",
				ENativeType::Void,
			{ENativeType::Array},
				[](const FNativeCallContext& Context, FValue&, std::string& OutError) -> bool
				{
					const auto* const Outer = std::get_if<FDynamicArrayHandle>(&Context.Args[0]);
					if (!Outer || !*Outer || (*Outer)->Elements.empty())
					{
						OutError = "expected non-empty array<array<int>>";
						return false;
					}
					auto* const Inner = std::get_if<FDynamicArrayHandle>(&(*Outer)->Elements[0]);
					if (!Inner || !*Inner || (*Inner)->Elements.empty())
					{
						OutError = "expected non-empty inner array<int>";
						return false;
					}
					auto* const IntValue = std::get_if<std::int32_t>(&(*Inner)->Elements[0]);
					if (!IntValue)
					{
						OutError = "expected int";
						return false;
					}
					*IntValue += 10;
					return true;
				},
				nullptr,
				MapErrorId,
			{},
			{},
			{},
			{"array<int>"}))
	{
		std::cerr << MapErrorId << '\n';
		return Fail("RegisterNative bump_nested_by_value should succeed");
	}
	if (!MapSetRegistry.RegisterNative(
				"touch_multiset_ref",
				FNativeTypeDesc::Scalar(ENativeType::Void),
			{{FNativeTypeDesc::MultiSetOf(
					FNativeTypeDesc::Scalar(ENativeType::Int)),
				NativeParamFlagRef}},
				[](const FNativeCallContext& Context, FValue&, std::string& OutError) -> bool
				{
					const auto* const Handle = std::get_if<FMapHandle>(&Context.Args[0]);
					if (!Handle || !*Handle)
					{
						OutError = "expected multiset<int>";
						return false;
					}
					const auto It = (*Handle)->IntEntries.find(2);
					const auto* const Occurrences = It == (*Handle)->IntEntries.end()
									? nullptr
						: std::get_if<FDynamicArrayHandle>(&It->second);
					if (!Occurrences || !*Occurrences
									|| (*Occurrences)->HasScalarElements()
									|| (*Occurrences)->Elements.size() != 2)
					{
						OutError = "mutable multiset boundary was not materialized";
						return false;
					}
					return true;
				},
				nullptr,
				MapErrorId))
	{
		std::cerr << MapErrorId << '\n';
		return Fail("RegisterNative touch_multiset_ref should succeed");
	}
	if (!MapSetRegistry.RegisterNative(
				"touch_ordered_multiset_ref",
				FNativeTypeDesc::Scalar(ENativeType::Void),
			{{FNativeTypeDesc::OrderedMultiSetOf(
					FNativeTypeDesc::Scalar(ENativeType::Int)),
				NativeParamFlagRef}},
				[](const FNativeCallContext& Context, FValue&, std::string& OutError) -> bool
				{
					const auto* const Handle =
									std::get_if<FOrderedMapHandle>(&Context.Args[0]);
					if (!Handle || !*Handle)
					{
						OutError = "expected ordered_multiset<int>";
						return false;
					}
					const auto It = (*Handle)->IntEntries.find(3);
					const auto* const Occurrences = It == (*Handle)->IntEntries.end()
									? nullptr
						: std::get_if<FDynamicArrayHandle>(&It->second);
					if (!Occurrences || !*Occurrences
									|| (*Occurrences)->HasScalarElements()
									|| (*Occurrences)->Elements.size() != 2)
					{
						OutError = "mutable ordered_multiset boundary was not materialized";
						return false;
					}
					return true;
				},
				nullptr,
				MapErrorId))
	{
		std::cerr << MapErrorId << '\n';
		return Fail("RegisterNative touch_ordered_multiset_ref should succeed");
	}
	if (!MapSetRegistry.RegisterNative(
				"make_nested_unsigned",
				ENativeType::Array,
			{},
				[](const FNativeCallContext&, FValue& OutReturn, std::string&) -> bool
				{
					auto Inner = std::make_shared<FDynamicArray>();
					Inner->Elements.push_back(std::uint32_t{9});
					auto Outer = std::make_shared<FDynamicArray>();
					Outer->Elements.push_back(std::move(Inner));
					OutReturn = std::move(Outer);
					return true;
				},
				nullptr,
				MapErrorId,
			{},
			{},
				"array<unsigned>"))
	{
		std::cerr << MapErrorId << '\n';
		return Fail("RegisterNative nested unsigned Array return should succeed");
	}
	if (!MapSetRegistry.RegisterNative(
				"make_bad_array",
				ENativeType::Array,
			{},
				[](const FNativeCallContext&, FValue& OutReturn, std::string&) -> bool
				{
					auto Result = std::make_shared<FDynamicArray>();
					Result->Elements.push_back(std::string{"wrong"});
					OutReturn = std::move(Result);
					return true;
				},
				nullptr,
				MapErrorId,
			{},
			{},
				"int"))
	{
		return Fail("RegisterNative malformed Array return fixture should succeed");
	}
	if (!MapSetRegistry.RegisterNative(
				"make_bad_map",
				ENativeType::Map,
			{},
				[](const FNativeCallContext&, FValue& OutReturn, std::string&) -> bool
				{
					auto Result = std::make_shared<FMap>();
					Result->IntEntries[1] = std::int32_t{2};
					OutReturn = std::move(Result);
					return true;
				},
				nullptr,
				MapErrorId,
			{},
			{},
			{},
			{},
			{},
				"string",
				"int"))
	{
		return Fail("RegisterNative malformed Map return fixture should succeed");
	}
	if (!MapSetRegistry.RegisterNative(
				"make_bad_char_map",
				ENativeType::Map,
			{},
				[](const FNativeCallContext&, FValue& OutReturn, std::string&) -> bool
				{
					auto Result = std::make_shared<FMap>();
					Result->IntEntries[128] = std::int32_t{1};
					OutReturn = std::move(Result);
					return true;
				},
				nullptr,
				MapErrorId,
			{},
			{},
			{},
			{},
			{},
				"char",
				"int"))
	{
		return Fail("RegisterNative malformed char Map return fixture should succeed");
	}
	if (!MapSetRegistry.RegisterNative(
				"make_bad_char_scalar_map",
				ENativeType::Map,
			{},
				[](const FNativeCallContext&, FValue& OutReturn, std::string&) -> bool
				{
					auto Result = std::make_shared<FMap>();
					auto& Entries =
									Result->ScalarEntries.emplace<FMap::FIntI32Entries>();
					Entries.InsertOrAssign(128, 1);
					OutReturn = std::move(Result);
					return true;
				},
				nullptr,
				MapErrorId,
			{},
			{},
			{},
			{},
			{},
				"char",
				"int"))
	{
		return Fail(
				"RegisterNative malformed scalar char Map return fixture should succeed");
	}
	if (!MapSetRegistry.RegisterNative(
				"make_bad_char_ordered_map",
				ENativeType::OrderedMap,
			{},
				[](const FNativeCallContext&, FValue& OutReturn, std::string&) -> bool
				{
					auto Result = std::make_shared<FOrderedMap>();
					Result->IntEntries[128] = std::int32_t{1};
					Result->IntKeyOrder.push_back(128);
					OutReturn = std::move(Result);
					return true;
				},
				nullptr,
				MapErrorId,
			{},
			{},
			{},
			{},
			{},
				"char",
				"int"))
	{
		return Fail(
				"RegisterNative malformed char OrderedMap return fixture should succeed");
	}
	if (!MapSetRegistry.RegisterNative(
				"make_bad_char_set",
				ENativeType::Set,
			{},
				[](const FNativeCallContext&, FValue& OutReturn, std::string&) -> bool
				{
					auto Result = std::make_shared<FSet>();
					Result->IntElements.insert(128);
					OutReturn = std::move(Result);
					return true;
				},
				nullptr,
				MapErrorId,
			{},
			{},
				"char"))
	{
		return Fail("RegisterNative malformed char Set return fixture should succeed");
	}

	{
		auto ScalarMap = std::make_shared<FMap>();
		ScalarMap->TypeName = "map<int,int>";
		ScalarMap->KeyTypeName = "int";
		ScalarMap->ValueTypeName = "int";
		auto& Entries =
				ScalarMap->ScalarEntries.emplace<FMap::FIntI32Entries>();
		Entries.InsertOrAssign(4, 7);
		FValue ClonedValue = ScalarMap;
		std::string CloneError;
		if (!CoerceValueToNativeType(
					ENativeType::Map, ClonedValue, CloneError, true))
		{
			std::cerr << CloneError << '\n';
			return Fail("scalar Map native boundary clone should succeed");
		}
		const auto* ClonedMap = std::get_if<FMapHandle>(&ClonedValue);
		if (!ClonedMap || !*ClonedMap || ClonedMap->get() == ScalarMap.get()
				|| (*ClonedMap)->ScalarEntries.index() != 0
				|| (*ClonedMap)->IntEntries.size() != 1
				|| std::get<std::int32_t>((*ClonedMap)->IntEntries.at(4)) != 7)
		{
			return Fail("scalar Map clone should materialize boxed native entries");
		}

		FValue SharedValue = ScalarMap;
		if (!CoerceValueToNativeType(
					ENativeType::Map, SharedValue, CloneError, false)
				|| ScalarMap->ScalarEntries.index() != 0
				|| ScalarMap->IntEntries.size() != 1
				|| std::get<std::int32_t>(ScalarMap->IntEntries.at(4)) != 7)
		{
			return Fail("mutable scalar Map boundary should materialize in place");
		}
	}

	{
		auto ScalarSet = std::make_shared<FSet>();
		ScalarSet->TypeName = "set<int>";
		ScalarSet->ElementTypeName = "int";
		auto& Elements =
				ScalarSet->ScalarElements.emplace<FSet::FIntScalarElements>();
		Elements.Insert(5);
		FValue ClonedValue = ScalarSet;
		std::string CloneError;
		if (!CoerceValueToNativeType(
					ENativeType::Set, ClonedValue, CloneError, true))
		{
			std::cerr << CloneError << '\n';
			return Fail("scalar Set native boundary clone should succeed");
		}
		const auto* ClonedSet = std::get_if<FSetHandle>(&ClonedValue);
		if (!ClonedSet || !*ClonedSet || ClonedSet->get() == ScalarSet.get()
				|| (*ClonedSet)->ScalarElements.index() != 0
				|| (*ClonedSet)->IntElements.size() != 1
				|| (*ClonedSet)->IntElements.count(5) != 1)
		{
			return Fail("scalar Set clone should materialize boxed native elements");
		}
	}

	{
		auto ConflictingArray = std::make_shared<FDynamicArray>();
		ConflictingArray->ElementTypeName = "int";
		ConflictingArray->EnableScalarStorage();
		ConflictingArray->Elements.push_back(std::int32_t{1});
		FValue Value = ConflictingArray;
		std::string Error;
		if (CoerceValueToNativeType(ENativeType::Array, Value, Error, true)
				|| Error.find("conflicting scalar and boxed storage") == std::string::npos)
		{
			return Fail("conflicting Array storage should be rejected");
		}
		Value = ConflictingArray;
		Error.clear();
		if (CoerceValueToNativeType(ENativeType::Array, Value, Error, false)
				|| Error.find("conflicting scalar and boxed storage") == std::string::npos
				|| !ConflictingArray->HasScalarElements()
				|| ConflictingArray->Elements.size() != 1)
		{
			return Fail("mutable conflicting Array storage should be rejected transactionally");
		}

		auto ConflictingMap = std::make_shared<FMap>();
		ConflictingMap->ScalarEntries.emplace<FMap::FIntI32Entries>();
		ConflictingMap->IntEntries[1] = std::int32_t{1};
		Value = ConflictingMap;
		Error.clear();
		if (CoerceValueToNativeType(ENativeType::Map, Value, Error, true)
				|| Error.find("conflicting scalar and boxed storage") == std::string::npos)
		{
			return Fail("conflicting Map storage should be rejected");
		}
		Value = ConflictingMap;
		Error.clear();
		if (CoerceValueToNativeType(ENativeType::Map, Value, Error, false)
				|| Error.find("conflicting scalar and boxed storage") == std::string::npos
				|| ConflictingMap->ScalarEntries.index() == 0
				|| ConflictingMap->IntEntries.size() != 1)
		{
			return Fail("mutable conflicting Map storage should be rejected transactionally");
		}

		auto ConflictingSet = std::make_shared<FSet>();
		ConflictingSet->ScalarElements.emplace<FSet::FIntScalarElements>();
		ConflictingSet->IntElements.insert(1);
		Value = ConflictingSet;
		Error.clear();
		if (CoerceValueToNativeType(ENativeType::Set, Value, Error, true)
				|| Error.find("conflicting scalar and boxed storage") == std::string::npos)
		{
			return Fail("conflicting Set storage should be rejected");
		}
		Value = ConflictingSet;
		Error.clear();
		if (CoerceValueToNativeType(ENativeType::Set, Value, Error, false)
				|| Error.find("conflicting scalar and boxed storage") == std::string::npos
				|| ConflictingSet->ScalarElements.index() == 0
				|| ConflictingSet->IntElements.size() != 1)
		{
			return Fail("mutable conflicting Set storage should be rejected transactionally");
		}

		auto ValidOuter = std::make_shared<FDynamicArray>();
		ValidOuter->Elements.push_back(ConflictingMap);
		Value = ValidOuter;
		Error.clear();
		if (CoerceValueToNativeType(ENativeType::Array, Value, Error, false)
				|| Error.find("map has conflicting scalar and boxed storage")
					== std::string::npos
				|| ConflictingMap->ScalarEntries.index() == 0
				|| ConflictingMap->IntEntries.size() != 1)
		{
			return Fail("nested mutable storage conflict should be rejected transactionally");
		}

		FNativeArray NativeWrapper;
		NativeWrapper.Storage().Elements.push_back(ConflictingMap);
		auto NativeOuter = std::make_shared<FDynamicArray>();
		NativeOuter->Elements.push_back(std::move(NativeWrapper));
		Value = NativeOuter;
		Error.clear();
		if (CoerceValueToNativeType(ENativeType::Array, Value, Error, true)
				|| Error.find("map has conflicting scalar and boxed storage")
					== std::string::npos)
		{
			return Fail("nested value-container storage conflict should be rejected");
		}
	}

	{
		auto SharedInner = std::make_shared<FDynamicArray>();
		SharedInner->TypeName = "array<int>";
		SharedInner->ElementTypeName = "int";
		SharedInner->Elements.push_back(std::int32_t{7});
		auto Outer = std::make_shared<FDynamicArray>();
		Outer->TypeName = "array<array<int>>";
		Outer->ElementTypeName = "array<int>";
		Outer->Elements.push_back(SharedInner);
		Outer->Elements.push_back(SharedInner);
		FValue ClonedValue = Outer;
		std::string CloneError;
		if (!CoerceValueToNativeType(
					ENativeType::Array, ClonedValue, CloneError, true))
		{
			std::cerr << CloneError << '\n';
			return Fail("native boundary graph clone should succeed");
		}
		const auto* ClonedOuter = std::get_if<FDynamicArrayHandle>(&ClonedValue);
		if (!ClonedOuter || !*ClonedOuter || ClonedOuter->get() == Outer.get())
		{
			return Fail("native boundary graph clone should isolate the outer array");
		}
		const auto* FirstInner =
				std::get_if<FDynamicArrayHandle>(&(*ClonedOuter)->Elements[0]);
		const auto* SecondInner =
				std::get_if<FDynamicArrayHandle>(&(*ClonedOuter)->Elements[1]);
		if (!FirstInner || !SecondInner || !*FirstInner || !*SecondInner
				|| FirstInner->get() != SecondInner->get()
				|| FirstInner->get() == SharedInner.get())
		{
			return Fail(
					"native boundary graph clone should preserve internal aliases");
		}
	}

	{
		// A stamped host clone may reenter and relocate the source element vector.
		// Boundary cloning must iterate a stable snapshot rather than stale iterators.
		auto Source = std::make_shared<FDynamicArray>();
		auto ReentrantValue = MakeHostObject(0, "ReentrantValue", nullptr, nullptr);
		ReentrantValue->CloneCallback = &ReentrantStampedClone;
		ReentrantValue->CloneUserData = &Source->Elements;
		Source->Elements.push_back(ReentrantValue);
		Source->Elements.push_back(std::int32_t{9});
		FValue ClonedValue = Source;
		std::string CloneError;
		if (!CoerceValueToNativeType(
					ENativeType::Array, ClonedValue, CloneError, true))
		{
			return Fail("reentrant native boundary clone should succeed safely");
		}
		const auto* const Cloned =
				std::get_if<FDynamicArrayHandle>(&ClonedValue);
		const auto* const ClonedHost = Cloned && *Cloned
				&& (*Cloned)->Elements.size() == 2
				? std::get_if<FHostObjectHandle>(&(*Cloned)->Elements[0])
			: nullptr;
		if (Source->Elements.size() != 66 || !ClonedHost || !*ClonedHost
				|| ClonedHost->get() == ReentrantValue.get()
				|| std::get<std::int32_t>((*Cloned)->Elements[1]) != 9)
		{
			return Fail("reentrant boundary clone should use the entry snapshot");
		}
	}

	{
		auto Cyclic = std::make_shared<FDynamicArray>();
		Cyclic->TypeName = "array<array<int>>";
		Cyclic->ElementTypeName = "array<int>";
		Cyclic->Elements.push_back(Cyclic);
		FValue ClonedValue = Cyclic;
		std::string CloneError;
		if (!CoerceValueToNativeType(
					ENativeType::Array, ClonedValue, CloneError, true))
		{
			Cyclic->Elements.clear();
			return Fail("native boundary cyclic graph clone should terminate");
		}
		auto* Cloned = std::get_if<FDynamicArrayHandle>(&ClonedValue);
		auto* ClonedSelf = Cloned && *Cloned
				? std::get_if<FDynamicArrayHandle>(&(*Cloned)->Elements[0])
				: nullptr;
		if (!ClonedSelf || !*ClonedSelf || ClonedSelf->get() != Cloned->get())
		{
			Cyclic->Elements.clear();
			if (Cloned && *Cloned)
			{
				(*Cloned)->Elements.clear();
			}
			return Fail("native boundary cyclic graph clone should preserve the cycle");
		}
		Cyclic->Elements.clear();
		(*Cloned)->Elements.clear();
	}

	{
		// Fixed native arrays are value containers, but dynamic handles nested in
		// them still belong to the same boundary graph and must resolve through
		// the shared clone memo.
		auto Cyclic = std::make_shared<FDynamicArray>();
		FNativeArray FixedWrapper;
		FixedWrapper.TypeName() = "array<int>[1]";
		FixedWrapper.ElementTypeName() = "array<int>";
		FixedWrapper.Length() = 1;
		FixedWrapper.Storage().Elements.push_back(Cyclic);
		Cyclic->Elements.push_back(std::move(FixedWrapper));
		FValue ClonedValue = Cyclic;
		std::string CloneError;
		if (!CoerceValueToNativeType(
					ENativeType::Array, ClonedValue, CloneError, true))
		{
			Cyclic->Elements.clear();
			return Fail("native boundary cycle through fixed array should terminate");
		}
		auto* const Cloned = std::get_if<FDynamicArrayHandle>(&ClonedValue);
		auto* const ClonedWrapper = Cloned && *Cloned
				? std::get_if<FNativeArray>(&(*Cloned)->Elements[0])
			: nullptr;
		auto* const ClonedSelf = ClonedWrapper
				? std::get_if<FDynamicArrayHandle>(
					&ClonedWrapper->Storage().Elements[0])
			: nullptr;
		if (!ClonedSelf || !*ClonedSelf || ClonedSelf->get() != Cloned->get()
				|| ClonedSelf->get() == Cyclic.get())
		{
			Cyclic->Elements.clear();
			if (Cloned && *Cloned)
			{
				(*Cloned)->Elements.clear();
			}
			return Fail(
					"native boundary fixed-array cycle should target the cloned graph");
		}
		Cyclic->Elements.clear();
		(*Cloned)->Elements.clear();
	}

	{
		// Script structs are value containers too. A dynamic handle reached through
		// a struct field must resolve to the clone already memoized for the root.
		auto Cyclic = std::make_shared<FDynamicArray>();
		auto Struct = std::make_shared<FScriptObject>();
		Struct->Kind = EScriptObjectKind::Struct;
		Struct->SetTypeName("BoundaryValue");
		Struct->Fields.push_back(Cyclic);
		Cyclic->Elements.push_back(Struct);
		FValue ClonedValue = Cyclic;
		std::string CloneError;
		if (!CoerceValueToNativeType(
					ENativeType::Array, ClonedValue, CloneError, true))
		{
			Cyclic->Elements.clear();
			Struct->Fields.clear();
			return Fail("native boundary cycle through script struct should terminate");
		}
		auto* const Cloned = std::get_if<FDynamicArrayHandle>(&ClonedValue);
		auto* const ClonedStruct = Cloned && *Cloned
				? std::get_if<FObjectHandle>(&(*Cloned)->Elements[0])
			: nullptr;
		auto* const ClonedSelf = ClonedStruct && *ClonedStruct
				? std::get_if<FDynamicArrayHandle>(&(*ClonedStruct)->Fields[0])
			: nullptr;
		if (!ClonedStruct || !*ClonedStruct
				|| ClonedStruct->get() == Struct.get()
				|| !ClonedSelf || !*ClonedSelf || ClonedSelf->get() != Cloned->get()
				|| ClonedSelf->get() == Cyclic.get())
		{
			Cyclic->Elements.clear();
			Struct->Fields.clear();
			if (ClonedStruct && *ClonedStruct)
			{
				(*ClonedStruct)->Fields.clear();
			}
			if (Cloned && *Cloned)
			{
				(*Cloned)->Elements.clear();
			}
			return Fail(
					"native boundary struct cycle should target the cloned graph");
		}
		Cyclic->Elements.clear();
		Struct->Fields.clear();
		(*ClonedStruct)->Fields.clear();
		(*Cloned)->Elements.clear();
	}

	{
		FValue DeepValue = std::int32_t{1};
		for (std::size_t Depth = 0; Depth < 160; ++Depth)
		{
			auto Wrapper = std::make_shared<FDynamicArray>();
			Wrapper->Elements.push_back(std::move(DeepValue));
			DeepValue = std::move(Wrapper);
		}
		std::string CloneError;
		if (CoerceValueToNativeType(
					ENativeType::Array, DeepValue, CloneError, true))
		{
			return Fail("over-deep native boundary graph should be rejected");
		}
		if (CloneError.find("nested too deeply") == std::string::npos)
		{
			return Fail("over-deep native graph should report a clear error");
		}
	}

	{
		FValue DeepValue = std::int32_t{1};
		for (std::size_t Depth = 0; Depth < 160; ++Depth)
		{
			FNativeArray Wrapper;
			Wrapper.Length() = 1;
			Wrapper.Storage().Elements.push_back(std::move(DeepValue));
			DeepValue = std::move(Wrapper);
		}
		auto Root = std::make_shared<FDynamicArray>();
		Root->Elements.push_back(std::move(DeepValue));
		FValue RootValue = std::move(Root);
		std::string CloneError;
		if (CoerceValueToNativeType(
					ENativeType::Array, RootValue, CloneError, true))
		{
			return Fail(
					"fixed arrays must not bypass native boundary graph depth limits");
		}
		if (CloneError.find("nested too deeply") == std::string::npos)
		{
			return Fail("over-deep fixed-array graph should report a clear error");
		}
	}

	{
		FValue DeepValue = std::int32_t{1};
		for (std::size_t Depth = 0; Depth < 160; ++Depth)
		{
			auto Wrapper = std::make_shared<FScriptObject>();
			Wrapper->Kind = EScriptObjectKind::Struct;
			Wrapper->Fields.push_back(std::move(DeepValue));
			DeepValue = std::move(Wrapper);
		}
		auto Root = std::make_shared<FDynamicArray>();
		Root->Elements.push_back(std::move(DeepValue));
		FValue RootValue = std::move(Root);
		std::string CloneError;
		if (CoerceValueToNativeType(
					ENativeType::Array, RootValue, CloneError, true))
		{
			return Fail(
					"script structs must not bypass native boundary graph depth limits");
		}
		if (CloneError.find("nested too deeply") == std::string::npos)
		{
			return Fail("over-deep script-struct graph should report a clear error");
		}
	}

	FHostTypeId WidgetId;
	if (!MapSetRegistry.RegisterType("Widget", EHostTypeKind::Class, WidgetId, MapErrorId))
	{
		return Fail("RegisterType Widget (map/set) should succeed");
	}
	FHostMemberDesc Mapping;
	Mapping.Kind = EHostMemberKind::PropertyGetter;
	Mapping.Name = "mapping";
	Mapping.ReturnType = ENativeType::Map;
	Mapping.ReturnKeyTypeName = "int";
	Mapping.ReturnValueTypeName = "int";
	Mapping.Callback =
			[](const FNativeCallContext&, FValue&, std::string&)
			{
				return true;
			};
	if (!MapSetRegistry.RegisterMember(WidgetId, Mapping, MapErrorId))
	{
		std::cerr << MapErrorId << '\n';
		return Fail("RegisterMember Widget.mapping should succeed");
	}
	FHostMemberDesc Tags;
	Tags.Kind = EHostMemberKind::PropertyGetter;
	Tags.Name = "tags";
	Tags.ReturnType = ENativeType::Set;
	Tags.ReturnElementTypeName = "int";
	Tags.Callback =
			[](const FNativeCallContext&, FValue&, std::string&)
			{
				return true;
			};
	if (!MapSetRegistry.RegisterMember(WidgetId, Tags, MapErrorId))
	{
		std::cerr << MapErrorId << '\n';
		return Fail("RegisterMember Widget.tags should succeed");
	}
	if (!MapSetRegistry.RegisterNative(
				"map_widget_key",
				ENativeType::Void,
			{ENativeType::Map},
				[](const FNativeCallContext&, FValue&, std::string&)
				{
					return true;
				},
				nullptr,
				MapErrorId,
			{},
			{},
			{},
			{},
			{},
			{},
			{},
			{"Widget"},
			{"int"}))
	{
		std::cerr << MapErrorId << '\n';
		return Fail("RegisterNative map<HostClass, ...> key should succeed");
	}
	if (!MapSetRegistry.RegisterNative(
				"set_widget",
				ENativeType::Void,
			{ENativeType::Set},
				[](const FNativeCallContext&, FValue&, std::string&)
				{
					return true;
				},
				nullptr,
				MapErrorId,
			{},
			{},
			{},
			{"Widget"}))
	{
		std::cerr << MapErrorId << '\n';
		return Fail("RegisterNative set<HostClass> should succeed");
	}

	FCompileOptions MapOptions = VeroScript::Test::MakeCompileOptions();
	MapOptions.Bindings = &MapSetRegistry;
	const FCompileResult HostContainerPropertyCompile = VeroScriptCompile::CompileSource(
			R"(
void inspect(Widget widget) {
	int value = widget.mapping[1];
	bool present = widget.tags.has(1);
}
)",
			"<native_map_set_property_types>",
			MapOptions);
	if (!HostContainerPropertyCompile.bSuccess)
	{
		std::cerr << "host container property compile failed: "
				<< HostContainerPropertyCompile.Error.Message << '\n';
		return Fail("Map/Set host property types should retain generic metadata");
	}
	const FCompileResult MapCompile = VeroScriptCompile::CompileSource(
			R"(
void main() {
	map<int,int> m = map<int,int>();
	map_put_ref(ref m, 1, 42);
	expect_eq(m[1], 42);

	map<int,int> n = map<int,int>();
	n[2] = 7;
	map_put_by_value(n, 2, 99);
	expect_eq(n[2], 7);

	set<int> s = set<int>();
	set_add_ref(ref s, 5);
	expect_eq(s.has(5) ? 1 : 0, 1);

	map<char,int> charMap = {'A': 7};
	ordered_map<char,int> orderedChars = {'B': 9};
	set<char> charSet = {'\xFF'};
	inspect_char_containers(charMap, orderedChars, charSet);

	array<array<int>> nested = array<array<int>>();
	array<int> row = array<int>();
	row.push(1);
	nested.push(row);
	bump_nested(ref nested);
	expect_eq(nested[0][0], 11);

	array<array<int>> nested2 = array<array<int>>();
	array<int> row2 = array<int>();
	row2.push(1);
	nested2.push(row2);
	bump_nested_by_value(nested2);
	expect_eq(nested2[0][0], 1);

	multiset<int> bag = {2, 2};
	touch_multiset_ref(ref bag);
	bag.add(2);
	expect_eq(bag.count(2), 3);

	ordered_multiset<int> orderedBag = {3, 3};
	touch_ordered_multiset_ref(ref orderedBag);
	orderedBag.add(3);
	expect_eq(orderedBag.count(3), 3);

	auto nestedUnsigned = make_nested_unsigned();
}
)",
			"<native_map_set_abi>",
			MapOptions);
	if (!MapCompile.bSuccess || !MapCompile.Module)
	{
		std::cerr << "map/set ABI compile failed: " << MapCompile.Error.Message << '\n';
		return Fail("map/set/nested Array ABI script should compile");
	}
	VeroScriptVm MapVm(std::cout);
		VeroScript::Test::Configure(MapVm);
	if (!MapVm.Execute(
				*MapCompile.Module,
				ERunMode::Script,
			{},
				&MapSetRegistry.GetNativeFunctions()))
	{
		std::cerr << "map/set ABI run failed: " << MapVm.GetError().Message << '\n';
		return Fail("map/set/nested CallNative round-trip should run");
	}

	for (const std::string_view BadCall : {
			 std::string_view{"make_bad_array();"},
				std::string_view{"make_bad_map();"},
				std::string_view{"make_bad_char_map();"},
				std::string_view{"make_bad_char_scalar_map();"},
				std::string_view{"make_bad_char_ordered_map();"},
				std::string_view{"make_bad_char_set();"}})
	{
		const std::string Source =
				"void main() { " + std::string(BadCall) + " }\n";
		const FCompileResult BadReturnCompile = VeroScriptCompile::CompileSource(
				Source, "<malformed_native_container_return>", MapOptions);
		if (!BadReturnCompile.bSuccess || !BadReturnCompile.Module)
		{
			return Fail("malformed native return fixture should compile");
		}
		VeroScriptVm BadReturnVm(std::cout);
		VeroScript::Test::Configure(BadReturnVm);
		if (BadReturnVm.Execute(
					*BadReturnCompile.Module,
					ERunMode::Script,
				{},
					&MapSetRegistry.GetNativeFunctions()))
		{
			return Fail("VM should reject malformed native container contents");
		}
		if (BadReturnVm.GetError().Message.find("invalid contents")
				== std::string::npos)
		{
			return Fail("malformed native container return should report invalid contents");
		}
	}

	const FCompileResult CorruptRefCompile = VeroScriptCompile::CompileSource(
			"void main() { map<int,int> m = map<int,int>(); corrupt_map_ref(ref m); }\n",
			"<native_corrupt_mutable_ref>",
			MapOptions);
	if (!CorruptRefCompile.bSuccess || !CorruptRefCompile.Module)
	{
		return Fail("mutable-ref corruption fixture should compile");
	}
	VeroScriptVm CorruptRefVm(std::cout);
		VeroScript::Test::Configure(CorruptRefVm);
	if (CorruptRefVm.Execute(
				*CorruptRefCompile.Module,
				ERunMode::Script,
			{},
				&MapSetRegistry.GetNativeFunctions()))
	{
		return Fail("VM should reject a corrupted mutable-ref container");
	}
	if (CorruptRefVm.GetError().Message.find("corrupted mutable-ref container")
			== std::string::npos)
	{
		return Fail("mutable-ref corruption should report a clear boundary error");
	}

	{
		std::string CatalogJson;
		if (!ExportBindingCatalogJson(MapSetRegistry, CatalogJson))
		{
			return Fail("ExportBindingCatalogJson (map/set) should succeed");
		}
		FHostBindingRegistry Imported;
		std::string CatalogError;
		if (!ImportBindingCatalogJson(CatalogJson, Imported, CatalogError))
		{
			std::cerr << CatalogError << '\n';
			return Fail("ImportBindingCatalogJson (map/set) should succeed");
		}
		bool bFoundMap = false;
		for (const FHostFunctionDesc& Fn : Imported.GetFunctions())
		{
			if (Fn.Name == "map_put_ref")
			{
				bFoundMap = true;
				if (Fn.ParamTypes.size() != 3
						|| Fn.ParamTypes[0] != ENativeType::Map
						|| Fn.ParamKeyTypeNames.size() < 1
						|| Fn.ParamKeyTypeNames[0] != "int"
						|| Fn.ParamValueTypeNames.size() < 1
						|| Fn.ParamValueTypeNames[0] != "int"
						|| Fn.ParamFlags.empty()
						|| Fn.ParamFlags[0] != NativeParamFlagRef)
				{
					return Fail("catalog round-trip should keep Map metadata");
				}
			}
		}
		if (!bFoundMap)
		{
			return Fail("catalog should include map_put_ref");
		}
	}
}
