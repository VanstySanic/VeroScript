// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Internal fragment: VeroScriptHostBindingTests / catalog and registry suite. Included by the owning translation unit.

	using namespace VeroScript;
	{
		std::string DeepCatalog(300, '[');
		DeepCatalog += "null";
		DeepCatalog.append(300, ']');
		FHostBindingRegistry DeepRegistry;
		std::string DeepError;
		if (ImportBindingCatalogJson(
				DeepCatalog, DeepRegistry, DeepError)
			|| DeepError.find("nested too deeply") == std::string::npos)
		{
			return Fail("binding catalog parser must reject pathological JSON nesting");
		}
		if (ImportBindingCatalogJson("-", DeepRegistry, DeepError)
			|| ImportBindingCatalogJson(
				"{\"schemaVersion\":1,\"schemaVersion\":1}",
				DeepRegistry,
				DeepError))
		{
			return Fail("binding catalog parser must reject malformed numbers and duplicate keys");
		}
		const auto MustRejectCatalog =
			[&DeepRegistry, &DeepError](const std::string_view Json) -> bool
			{
				DeepError.clear();
				return !ImportBindingCatalogJson(Json, DeepRegistry, DeepError)
					&& !DeepError.empty();
			};
		if (!MustRejectCatalog(R"({"version":"1"})")
			|| !MustRejectCatalog(R"({"version":1.5})")
			|| !MustRejectCatalog(R"({"version":1,"functions":{}})")
			|| !MustRejectCatalog(
				R"({"version":1,"functions":[{"name":"f","returnType":"void","paramTypes":["int"],"paramFlags":[1.5]}]})")
			|| !MustRejectCatalog(
				R"({"version":1,"types":[{"name":"E","kind":"enum","members":[{"kind":"enumMember","name":"A","enumValue":1.5}]}]})")
			|| !MustRejectCatalog(
				R"({"version":1,"types":[{"name":"E","kind":"enum","members":[{"kind":"enumMember","name":"A","enumValueInt64":1}]}]})"))
		{
			std::cerr << DeepError << '\n';
			return Fail("binding catalog schema must reject invalid field kinds and lossy integers");
		}
		FHostBindingRegistry AtomicAppendRegistry;
		FHostTypeId ExistingId;
		if (!AtomicAppendRegistry.RegisterType(
				"Existing", EHostTypeKind::Class, ExistingId, DeepError))
		{
			return Fail("seed atomic append registry");
		}
		constexpr std::string_view FailingAppendCatalog = R"({
  "version": 1,
  "types": [
    {"name":"WouldLeak","kind":"class","members":[]},
    {"name":"Existing","kind":"class","members":[]}
  ]
})";
		if (ImportBindingCatalogJson(
				FailingAppendCatalog,
				AtomicAppendRegistry,
				DeepError,
				false)
			|| AtomicAppendRegistry.FindType("WouldLeak") != nullptr
			|| AtomicAppendRegistry.FindType("Existing") == nullptr
			|| AtomicAppendRegistry.GetTypes().size() != 1)
		{
			return Fail("failed append catalog import must leave the registry unchanged");
		}
		FHostTypeId AfterFailureId;
		if (!AtomicAppendRegistry.RegisterType(
				"AfterFailure", EHostTypeKind::Class, AfterFailureId, DeepError)
			|| AfterFailureId.Value != 2)
		{
			return Fail("failed append catalog import must not consume registry ids");
		}
		constexpr std::string_view DependentCatalog = R"({"version":1,"functions":[{
			"name":"dependent_api","returnType":"object","returnHostTypeName":"Existing","paramTypes":[]
		}]})";
		if (!AtomicAppendRegistry.RegisterNative(
				"existing_native", ENativeType::Int, {ENativeType::Int, ENativeType::Int},
				HostAdd, nullptr, DeepError))
		{
			return Fail("seed live callback before catalog append");
		}
		if (!ImportBindingCatalogJson(DependentCatalog, AtomicAppendRegistry, DeepError, false)
			|| AtomicAppendRegistry.FindType("Existing") == nullptr
			|| AtomicAppendRegistry.GetFunctions().size() != 2
			|| AtomicAppendRegistry.GetNativeFunctions().front().Callback != HostAdd)
		{
			return Fail("append catalog signatures must resolve previously registered dependency types");
		}
	}

	FHostBindingRegistry Registry;
	std::string ErrorId;
	FHostTypeId TypeId;

	if (!NativeTypeMatchesTypeName(ENativeType::Int8, "int8")
		|| !NativeTypeMatchesTypeName(ENativeType::UInt8, "uint8")
		|| !NativeTypeMatchesTypeName(ENativeType::Int16, "int16")
		|| !NativeTypeMatchesTypeName(ENativeType::UInt16, "uint16")
		|| !NativeTypeMatchesTypeName(ENativeType::Int, "int32")
		|| !NativeTypeMatchesTypeName(ENativeType::Unsigned, "uint32")
		|| !NativeTypeMatchesTypeName(ENativeType::Int64, "int64")
		|| !NativeTypeMatchesTypeName(ENativeType::Int64, "long")
		|| !NativeTypeMatchesTypeName(ENativeType::UInt64, "uint64")
		|| !NativeTypeMatchesTypeName(ENativeType::UInt64, "unsigned long")
		|| !NativeTypeMatchesTypeName(ENativeType::Float, "float")
		|| !NativeTypeMatchesTypeName(ENativeType::Double, "double")
		|| NativeTypeMatchesTypeName(ENativeType::Int, "int8")
		|| NativeTypeMatchesTypeName(ENativeType::Double, "float")
		|| NativeTypeMatchesTypeName(ENativeType::Int16, "int8")
		|| NativeTypeMatchesTypeName(ENativeType::Unsigned, "uint16"))
	{
		return Fail("native type identity must remain exact across primitive widths");
	}
	if (CanonicalizeNativeElementTypeName("int8") != "char"
		|| CanonicalizeNativeElementTypeName("uint8") != "unsigned char"
		|| CanonicalizeNativeElementTypeName("int16") != "short"
		|| CanonicalizeNativeElementTypeName("uint16") != "unsigned short"
		|| CanonicalizeNativeElementTypeName("array<uint8>") != "array<unsigned char>"
		|| CanonicalizeNativeElementTypeName("map<int16,array<uint16>>")
			!= "map<short,array<unsigned short>>")
	{
		return Fail("native integer aliases should canonicalize recursively");
	}
	{
		FValue Int8Value = std::int8_t{-12};
		FValue FloatValue = 1.25f;
		if (!NativeTypeMatchesValue(ENativeType::Int8, Int8Value)
			|| NativeTypeMatchesValue(ENativeType::Int, Int8Value)
			|| !NativeTypeMatchesValue(ENativeType::Float, FloatValue)
			|| NativeTypeMatchesValue(ENativeType::Double, FloatValue))
		{
			return Fail("native values must retain exact primitive runtime identity");
		}
		std::string CoerceError;
		if (!CoerceValueToNativeType(ENativeType::Int, Int8Value, CoerceError)
			|| !std::holds_alternative<std::int32_t>(Int8Value)
			|| std::get<std::int32_t>(Int8Value) != -12
			|| !CoerceValueToNativeType(ENativeType::Double, FloatValue, CoerceError)
			|| !std::holds_alternative<double>(FloatValue)
			|| std::get<double>(FloatValue) != 1.25)
		{
			std::cerr << CoerceError << '\n';
			return Fail("native widening must convert values without erasing source identity");
		}
	}
	if (!NativeTypeMatchesTypeName(ENativeType::MultiMap, "multimap<string,int>")
		|| NativeTypeMatchesTypeName(ENativeType::Map, "multimap<string,int>")
		|| !NativeTypeMatchesTypeName(
			ENativeType::OrderedMultiMap, "ordered_multimap<string,int>")
		|| NativeTypeMatchesTypeName(
			ENativeType::OrderedMap, "ordered_multimap<string,int>")
		|| !NativeTypeMatchesTypeName(ENativeType::PriorityQueue, "priority_queue<int>")
		|| NativeTypeMatchesTypeName(ENativeType::Queue, "priority_queue<int>")
		|| !NativeTypeMatchesTypeName(ENativeType::MultiSet, "multiset<int>")
		|| !NativeTypeMatchesTypeName(
			ENativeType::OrderedMultiSet, "ordered_multiset<int>"))
	{
		return Fail("native container identities should remain exact");
	}
	if (ResolveNativeTypeDescName(FNativeTypeDesc::PriorityQueueOf(
			FNativeTypeDesc::Scalar(ENativeType::Int))) != "priority_queue<int>"
		|| ResolveNativeTypeDescName(FNativeTypeDesc::MultiSetOf(
			FNativeTypeDesc::Scalar(ENativeType::String))) != "multiset<string>"
		|| ResolveNativeTypeDescName(FNativeTypeDesc::OrderedMultiSetOf(
			FNativeTypeDesc::Scalar(ENativeType::UInt64)))
			!= "ordered_multiset<uint64>")
	{
		return Fail("structured native descriptors should resolve new containers");
	}

	if (!Registry.RegisterNative(
			"host_add",
			ENativeType::Int,
			{ENativeType::Int, ENativeType::Int},
			HostAdd,
			nullptr,
			ErrorId))
	{
		return Fail("RegisterNative host_add should succeed");
	}
	if (Registry.GetFunctions().size() != 1 || Registry.FindFunction("host_add") == nullptr)
	{
		return Fail("function catalog should contain host_add");
	}
	if (!Registry.RegisterType("Actor", EHostTypeKind::Class, TypeId, ErrorId) || !TypeId.IsValid())
	{
		return Fail("RegisterType Actor should succeed");
	}

	FHostMemberDesc Method;
	Method.Kind = EHostMemberKind::Method;
	Method.Name = "get_id";
	Method.ReturnType = ENativeType::Int;
	if (!Registry.RegisterMember(TypeId, Method, ErrorId))
	{
		return Fail("RegisterMember get_id should succeed");
	}
	if (Registry.FindType("Actor") == nullptr || Registry.FindType("Actor")->Members.size() != 1)
	{
		return Fail("type catalog should list Actor.get_id");
	}

	{
		std::string DupError;
		FHostTypeId DupId;
		if (Registry.RegisterType("Actor", EHostTypeKind::Class, DupId, DupError))
		{
			return Fail("duplicate type name should fail");
		}
	}

	FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
	Options.Bindings = &Registry;
	const FCompileResult Ok = VeroScriptCompile::CompileSource(
		"void main() { int x = host_add(1, 2); }\n",
		"<host_binding_ok>",
		Options);
	if (!Ok.bSuccess)
	{
		std::cerr << "compile with bindings failed: " << Ok.Error.Message << '\n';
		return Fail("host_add should compile via BindingCatalog");
	}

	{
		FHostBindingRegistry NamespacedRegistry;
		FHostTypeId BaseId;
		if (!NamespacedRegistry.RegisterType(
				"Host::Base", EHostTypeKind::Class, BaseId, ErrorId)
			|| !NamespacedRegistry.SetTypeInheritable(BaseId, true, ErrorId))
		{
			return Fail("namespaced host base registration should succeed");
		}
		FCompileOptions NamespacedOptions = VeroScript::Test::MakeCompileOptions();
		NamespacedOptions.Bindings = &NamespacedRegistry;
		const FCompileResult NamespacedCompile = VeroScriptCompile::CompileSource(
			"using namespace Host;\n"
			"class Derived : Base {}\n"
			"void main() {}\n",
			"<namespaced_host_using>",
			NamespacedOptions);
		if (!NamespacedCompile.bSuccess || !NamespacedCompile.Module)
		{
			std::cerr << "namespaced host using compile failed: "
				<< NamespacedCompile.Error.Message << '\n';
			return Fail("using namespace should resolve C++-spelled host types");
		}
	}

	{
		FHostBindingRegistry Int64Registry;
		if (!Int64Registry.RegisterNative(
				"echo_i64",
				ENativeType::Int64,
				{ENativeType::Int64},
				HostEchoInt64,
				nullptr,
				ErrorId)
			|| !Int64Registry.RegisterNative(
				"echo_u64",
				ENativeType::UInt64,
				{ENativeType::UInt64},
				HostEchoUInt64,
				nullptr,
				ErrorId))
		{
			return Fail("int64/uint64 native ABI registration should succeed");
		}
		FCompileOptions Int64Options = VeroScript::Test::MakeCompileOptions();
		Int64Options.Bindings = &Int64Registry;
		const FCompileResult Int64Compile = VeroScriptCompile::CompileSource(
			"void main() {\n"
			"  int64 i = echo_i64(9223372036854775807i64);\n"
			"  uint64 u = echo_u64(18446744073709551615u64);\n"
			"}\n",
			"<native_int64_abi>",
			Int64Options);
		if (!Int64Compile.bSuccess || !Int64Compile.Module)
		{
			return Fail("int64/uint64 native ABI script should compile");
		}
		VeroScriptVm Int64Vm(std::cout);
		VeroScript::Test::Configure(Int64Vm);
		if (!Int64Vm.Execute(
				*Int64Compile.Module,
				ERunMode::Script,
				{},
				&Int64Registry.GetNativeFunctions()))
		{
			std::cerr << Int64Vm.GetError().Message << '\n';
			return Fail("int64/uint64 native ABI should preserve full precision");
		}
	}

	const FCompileResult Missing = VeroScriptCompile::CompileSource(
		"void main() { int x = host_add(1, 2); }\n",
		"<host_binding_missing>",
		{});
	if (Missing.bSuccess)
	{
		return Fail("host_add without bindings should fail to compile");
	}

	{
		FHostBindingRegistry ClassRegistry;
		FHostTypeId CounterId;
		if (!ClassRegistry.RegisterType("Counter", EHostTypeKind::Class, CounterId, ErrorId))
		{
			return Fail("RegisterType Counter should succeed");
		}

		FHostMemberDesc Ctor;
		Ctor.Kind = EHostMemberKind::Constructor;
		Ctor.ReturnType = ENativeType::HostObject;
		Ctor.ParamTypes = {ENativeType::Int};
		Ctor.Callback = CounterCtor;
		if (!ClassRegistry.RegisterMember(CounterId, Ctor, ErrorId))
		{
			std::cerr << ErrorId << '\n';
			return Fail("RegisterMember Counter ctor should succeed");
		}
		FHostMemberDesc StringCtor = Ctor;
		StringCtor.ParamTypes = {ENativeType::String};
		StringCtor.Callback = CounterStringCtor;
		if (!ClassRegistry.RegisterMember(CounterId, StringCtor, ErrorId))
		{
			std::cerr << ErrorId << '\n';
			return Fail("RegisterMember overloaded Counter ctor should succeed");
		}

		FHostMemberDesc Inc;
		Inc.Kind = EHostMemberKind::Method;
		Inc.Name = "inc";
		Inc.ReturnType = ENativeType::Void;
		Inc.Callback = CounterInc;
		if (!ClassRegistry.RegisterMember(CounterId, Inc, ErrorId))
		{
			return Fail("RegisterMember Counter.inc should succeed");
		}

		FHostMemberDesc Get;
		Get.Kind = EHostMemberKind::Method;
		Get.Name = "get";
		Get.ReturnType = ENativeType::Int;
		Get.Callback = CounterGet;
		if (!ClassRegistry.RegisterMember(CounterId, Get, ErrorId))
		{
			return Fail("RegisterMember Counter.get should succeed");
		}

		FHostMemberDesc Add;
		Add.Name = "operator+";
		Add.ReturnType = ENativeType::HostObject;
		Add.ReturnHostTypeName = "Counter";
		Add.ParamTypes = {ENativeType::HostObject};
		Add.ParamHostTypeNames = {"Counter"};
		Add.Callback = CounterAdd;
		if (!ClassRegistry.RegisterOperator(CounterId, Add, ErrorId))
		{
			std::cerr << ErrorId << '\n';
			return Fail("RegisterOperator Counter.operator+ should succeed");
		}

		FHostMemberDesc Equal = Add;
		Equal.Name = "operator==";
		Equal.ReturnType = ENativeType::Bool;
		Equal.ReturnHostTypeName.clear();
		Equal.Callback = CounterEqual;
		if (!ClassRegistry.RegisterOperator(CounterId, Equal, ErrorId))
		{
			return Fail("RegisterOperator Counter.operator== should succeed");
		}

		FCompileOptions ClassOptions = VeroScript::Test::MakeCompileOptions();
		ClassOptions.Bindings = &ClassRegistry;
		const FCompileResult ClassCompile = VeroScriptCompile::CompileSource(
			R"(
void main() {
	Counter c(40);
	c.inc();
	c.inc();
	int n = c.get();
	Counter sum = Counter(1) + Counter(2);
	int sumValue = sum.get();
	bool equals = sum == Counter(3);
	Counter fromString = Counter("abcd");
	int fromStringValue = fromString.get();
}
)",
			"<host_class>",
			ClassOptions);
		if (!ClassCompile.bSuccess || !ClassCompile.Module)
		{
			std::cerr << "host class compile failed: " << ClassCompile.Error.Message << '\n';
			return Fail("Counter class binding should compile");
		}

		GCounterOperatorAddCalls = 0;
		GCounterOperatorEqualCalls = 0;
		GCounterOperatorAddResult = 0;
		GCounterOperatorEqualResult = false;
		GCounterStringConstructorCalls = 0;
		VeroScriptVm Vm(std::cout);
		VeroScript::Test::Configure(Vm);
		if (!Vm.Execute(
				*ClassCompile.Module,
				ERunMode::Script,
				{},
					&ClassRegistry.GetNativeFunctions()))
		{
			std::cerr << "host class run failed: " << Vm.GetError().Message << '\n';
			return Fail("Counter class binding should run");
		}
		if (GCounterOperatorAddCalls != 1
			|| GCounterOperatorEqualCalls != 1
			|| GCounterOperatorAddResult != 3
			|| !GCounterOperatorEqualResult
			|| GCounterStringConstructorCalls != 1)
		{
			return Fail("registered host operators should execute with typed operands");
		}

		const FCompileResult KeyCompile = VeroScriptCompile::CompileSource(
			R"(
void bump(ref int value) {
	value += 1;
}
void main() {
	Counter a = Counter(1);
	Counter b = Counter(1);
	map<Counter,int> m = map<Counter,int>();
	m[a] = 10;
	m[b] = 20;
	print(m[a]);
	print(m[b]);
	print(len(m));
	print(m.has(a));
	print(len(m.keys()));
	print(len(m.values()));
	print(len(m.entries()));
	bump(ref m[b]);
	print(m[b]);
	set<Counter> s = set<Counter>();
	s.add(a);
	s.add(a);
	s.add(b);
	print(len(s));
	print(s.has(a));
	print(len(s.values()));
	ordered_map<Counter,int> om = ordered_map<Counter,int>();
	om[a] = 30;
	om[b] = 40;
	print(om[a]);
	print(len(om));
	print(om.has(b));
	print(len(om.keys()));
	print(len(om.values()));
	print(len(om.entries()));
	print(m.remove(a));
	print(len(m));
	print(s.remove(a));
	print(len(s));
	print(om.remove(a));
	print(len(om));
}
)",
			"<host_class_keys>",
			ClassOptions);
		if (!KeyCompile.bSuccess || !KeyCompile.Module)
		{
			std::cerr << "host class key compile failed: " << KeyCompile.Error.Message << '\n';
			return Fail("Host Class should be accepted as map/set key");
		}
		std::ostringstream KeyOutput;
		VeroScriptVm KeyVm(KeyOutput);
		VeroScript::Test::Configure(KeyVm);
		if (!KeyVm.Execute(
				*KeyCompile.Module,
				ERunMode::Script,
				{},
				&ClassRegistry.GetNativeFunctions())
			|| KeyOutput.str()
				!= "10\n20\n2\ntrue\n2\n2\n2\n21\n2\ntrue\n2\n30\n2\ntrue\n2\n2\n2\ntrue\n1\ntrue\n1\ntrue\n1\n")
		{
			std::cerr << "host class key run failed: " << KeyVm.GetError().Message
				<< " output=[" << KeyOutput.str() << "]\n";
			return Fail("Host Class keys should use handle identity");
		}
	}

	{
		FHostBindingRegistry EnumRegistry;
		FHostTypeId ColorId;
		if (!EnumRegistry.RegisterType("Color", EHostTypeKind::Enum, ColorId, ErrorId))
		{
			return Fail("RegisterType Color should succeed");
		}

		auto AddEnumMember = [&](const char* Name, const std::int64_t Value) -> bool
		{
			FHostMemberDesc Member;
			Member.Kind = EHostMemberKind::EnumMember;
			Member.Name = Name;
			Member.EnumValue = Value;
			return EnumRegistry.RegisterMember(ColorId, Member, ErrorId);
		};
		if (!AddEnumMember("Red", 1)
			|| !AddEnumMember("Green", 2)
			|| !AddEnumMember("Blue", 4)
			|| !AddEnumMember("Huge", std::numeric_limits<std::int64_t>::max()))
		{
			std::cerr << ErrorId << '\n';
			return Fail("RegisterMember Color.* should succeed");
		}

		{
			FHostMemberDesc Bad;
			Bad.Kind = EHostMemberKind::EnumMember;
			Bad.Name = "Nope";
			Bad.EnumValue = 0;
			FHostTypeId ClassId;
			if (!EnumRegistry.RegisterType("NotEnum", EHostTypeKind::Class, ClassId, ErrorId))
			{
				return Fail("RegisterType NotEnum should succeed");
			}
			if (EnumRegistry.RegisterMember(ClassId, Bad, ErrorId))
			{
				return Fail("EnumMember on class type should fail");
			}
		}

		FCompileOptions EnumOptions = VeroScript::Test::MakeCompileOptions();
		EnumOptions.Bindings = &EnumRegistry;
		const FCompileResult EnumCompile = VeroScriptCompile::CompileSource(
			R"(
void main() {
	Color c = Color.Green;
	int n = Color.Red + Color.Blue;
}
)",
			"<host_enum>",
			EnumOptions);
		if (!EnumCompile.bSuccess || !EnumCompile.Module)
		{
			std::cerr << "host enum compile failed: " << EnumCompile.Error.Message << '\n';
			return Fail("Color enum binding should compile");
		}

		VeroScriptVm EnumVm(std::cout);
		VeroScript::Test::Configure(EnumVm);
		if (!EnumVm.Execute(
				*EnumCompile.Module,
				ERunMode::Script,
				{},
					&EnumRegistry.GetNativeFunctions()))
		{
			std::cerr << "host enum run failed: " << EnumVm.GetError().Message << '\n';
			return Fail("Color enum binding should run");
		}
		std::string EnumCatalog;
		if (!ExportBindingCatalogJson(EnumRegistry, EnumCatalog))
		{
			return Fail("full-width host enum catalog export should succeed");
		}
		FHostBindingRegistry ImportedEnums;
		std::string EnumCatalogError;
		if (!ImportBindingCatalogJson(
				EnumCatalog, ImportedEnums, EnumCatalogError))
		{
			std::cerr << EnumCatalogError << '\n';
			return Fail("full-width host enum catalog import should succeed");
		}
		const FHostTypeDesc* const ImportedColor =
			ImportedEnums.FindType("Color");
		bool bFoundHuge = false;
		if (ImportedColor)
		{
			for (const FHostMemberDesc& Member : ImportedColor->Members)
			{
				bFoundHuge = bFoundHuge
					|| (Member.Name == "Huge"
						&& Member.EnumValue
							== std::numeric_limits<std::int64_t>::max());
			}
		}
		if (!bFoundHuge)
		{
			return Fail("host enum catalog should preserve int64 values exactly");
		}
	}

	{
		FHostBindingRegistry PropRegistry;
		FHostTypeId CounterId;
		if (!PropRegistry.RegisterType("Counter", EHostTypeKind::Class, CounterId, ErrorId))
		{
			return Fail("RegisterType Counter (props) should succeed");
		}

		FHostMemberDesc Ctor;
		Ctor.Kind = EHostMemberKind::Constructor;
		Ctor.ReturnType = ENativeType::HostObject;
		Ctor.ParamTypes = {ENativeType::Int};
		Ctor.Callback = CounterCtor;
		if (!PropRegistry.RegisterMember(CounterId, Ctor, ErrorId))
		{
			return Fail("RegisterMember Counter ctor (props) should succeed");
		}

		FHostMemberDesc Getter;
		Getter.Kind = EHostMemberKind::PropertyGetter;
		Getter.Name = "value";
		Getter.ReturnType = ENativeType::Int;
		Getter.Callback = CounterGetValue;
		if (!PropRegistry.RegisterMember(CounterId, Getter, ErrorId))
		{
			std::cerr << ErrorId << '\n';
			return Fail("RegisterMember Counter.value getter should succeed");
		}

		FHostMemberDesc Setter;
		Setter.Kind = EHostMemberKind::PropertySetter;
		Setter.Name = "value";
		Setter.ParamTypes = {ENativeType::Int};
		Setter.Callback = CounterSetValue;
		if (!PropRegistry.RegisterMember(CounterId, Setter, ErrorId))
		{
			std::cerr << ErrorId << '\n';
			return Fail("RegisterMember Counter.value setter should succeed");
		}

		FCompileOptions PropOptions = VeroScript::Test::MakeCompileOptions();
		PropOptions.Bindings = &PropRegistry;
		if (!PropRegistry.RegisterNative(
				"expect_eq",
				ENativeType::Void,
				{ENativeType::Int, ENativeType::Int},
				ExpectEq,
				nullptr,
				ErrorId))
		{
			return Fail("RegisterNative expect_eq (props) should succeed");
		}
		const FCompileResult PropCompile = VeroScriptCompile::CompileSource(
			R"(
void main() {
	Counter c = Counter(10);
	c.value = 20;
	c.value += 5;
	expect_eq(c.value, 25);
}
)",
			"<host_property>",
			PropOptions);
		if (!PropCompile.bSuccess || !PropCompile.Module)
		{
			std::cerr << "host property compile failed: " << PropCompile.Error.Message << '\n';
			return Fail("Counter property binding should compile");
		}

		VeroScriptVm PropVm(std::cout);
		VeroScript::Test::Configure(PropVm);
		if (!PropVm.Execute(
				*PropCompile.Module,
				ERunMode::Script,
				{},
					&PropRegistry.GetNativeFunctions()))
		{
			std::cerr << "host property run failed: " << PropVm.GetError().Message << '\n';
			return Fail("Counter property binding should run");
		}
	}

	{
		FHostBindingRegistry StructRegistry;
		FHostTypeId VecId;
		if (!StructRegistry.RegisterType("Vec2", EHostTypeKind::Struct, VecId, ErrorId))
		{
			return Fail("RegisterType Vec2 should succeed");
		}

		FHostMemberDesc Ctor;
		Ctor.Kind = EHostMemberKind::Constructor;
		Ctor.ReturnType = ENativeType::HostObject;
		Ctor.ParamTypes = {ENativeType::Int, ENativeType::Int};
		Ctor.Callback = Vec2Ctor;
		if (!StructRegistry.RegisterMember(VecId, Ctor, ErrorId))
		{
			return Fail("RegisterMember Vec2 ctor should succeed");
		}

		FHostMemberDesc GetX;
		GetX.Kind = EHostMemberKind::PropertyGetter;
		GetX.Name = "x";
		GetX.ReturnType = ENativeType::Int;
		GetX.Callback = Vec2GetX;
		FHostMemberDesc SetX;
		SetX.Kind = EHostMemberKind::PropertySetter;
		SetX.Name = "x";
		SetX.ParamTypes = {ENativeType::Int};
		SetX.Callback = Vec2SetX;
		if (!StructRegistry.RegisterMember(VecId, GetX, ErrorId)
			|| !StructRegistry.RegisterMember(VecId, SetX, ErrorId))
		{
			std::cerr << ErrorId << '\n';
			return Fail("RegisterMember Vec2.x should succeed");
		}

		if (!StructRegistry.RegisterTypeClone(VecId, Vec2Clone, nullptr, ErrorId))
		{
			std::cerr << ErrorId << '\n';
			return Fail("RegisterTypeClone Vec2 should succeed");
		}

		if (!StructRegistry.RegisterNative(
				"expect_eq",
				ENativeType::Void,
				{ENativeType::Int, ENativeType::Int},
				ExpectEq,
				nullptr,
				ErrorId))
		{
			return Fail("RegisterNative expect_eq should succeed");
		}

		{
			FHostTypeId ClassId;
			if (!StructRegistry.RegisterType("NotStruct", EHostTypeKind::Class, ClassId, ErrorId))
			{
				return Fail("RegisterType NotStruct should succeed");
			}
			if (StructRegistry.RegisterTypeClone(ClassId, Vec2Clone, nullptr, ErrorId))
			{
				return Fail("RegisterTypeClone on class should fail");
			}
		}

		FCompileOptions StructOptions = VeroScript::Test::MakeCompileOptions();
		StructOptions.Bindings = &StructRegistry;
		const FCompileResult StructKeyCompile = VeroScriptCompile::CompileSource(
			"void main() { map<Vec2,int> m = map<Vec2,int>(); }\n",
			"<host_struct_key_rejected>",
			StructOptions);
		if (StructKeyCompile.bSuccess)
		{
			return Fail("Host Struct must not be accepted as a map key");
		}
		FHostMemberDesc StructKeyProperty;
		StructKeyProperty.Kind = EHostMemberKind::PropertyGetter;
		StructKeyProperty.Name = "invalidKeyMap";
		StructKeyProperty.ReturnType = ENativeType::Map;
		StructKeyProperty.ReturnKeyTypeName = "Vec2";
		StructKeyProperty.ReturnValueTypeName = "int";
		StructKeyProperty.Callback =
			[](const FNativeCallContext &, FValue &, std::string &)
		{
			return true;
		};
		if (StructRegistry.RegisterMember(VecId, StructKeyProperty, ErrorId) || ErrorId != "diag.hostbinding_unsupported_map_or_set_key_type")
		{
			return Fail("native Map<HostStruct, ...> metadata must be rejected");
		}
		if (!StructRegistry.RegisterTypeEquals(VecId, Vec2Equals, nullptr, ErrorId)
			|| !StructRegistry.RegisterTypeHash(VecId, Vec2Hash, nullptr, ErrorId))
		{
			std::cerr << ErrorId << '\n';
			return Fail("host struct key protocols should register");
		}
		if (!StructRegistry.RegisterNative(
				"native_vec_map",
				FNativeTypeDesc::MapOf(
					FNativeTypeDesc::Host("Vec2"),
					FNativeTypeDesc::Scalar(ENativeType::Int)),
				{},
				NativeVec2Map,
				nullptr,
				ErrorId)
			|| !StructRegistry.RegisterNative(
				"native_vec_ordered_map",
				FNativeTypeDesc::OrderedMapOf(
					FNativeTypeDesc::Host("Vec2"),
					FNativeTypeDesc::Scalar(ENativeType::Int)),
				{},
				NativeVec2OrderedMap,
				nullptr,
				ErrorId)
			|| !StructRegistry.RegisterNative(
				"native_vec_set",
				FNativeTypeDesc::SetOf(FNativeTypeDesc::Host("Vec2")),
				{},
				NativeVec2Set,
				nullptr,
				ErrorId))
		{
			std::cerr << ErrorId << '\n';
			return Fail("native host-key containers should register");
		}
		const FCompileResult StructValueKeyCompile = VeroScriptCompile::CompileSource(
			R"(
void main() {
	map<Vec2,int> m = map<Vec2,int>();
	ordered_map<Vec2,int> ordered = ordered_map<Vec2,int>();
	set<Vec2> s = set<Vec2>();
	Vec2 inserted = Vec2(3, 4);
	m[inserted] = 17;
	ordered[inserted] = 23;
	s.add(inserted);
	inserted.x = 99;
	Vec2 equalKey = Vec2(3, 4);
	expect_eq(m[equalKey], 17);
	expect_eq(ordered[equalKey], 23);
	expect_eq(s.has(equalKey) ? 1 : 0, 1);
	expect_eq(s.has(inserted) ? 1 : 0, 0);
	array<Vec2> mapKeys = m.keys();
	mapKeys[0].x = 40;
	array<pair<Vec2,int>> mapEntries = m.entries();
	mapEntries[0].first.x = 41;
	array<Vec2> orderedKeys = ordered.keys();
	orderedKeys[0].x = 42;
	array<pair<Vec2,int>> orderedEntries = ordered.entries();
	orderedEntries[0].first.x = 43;
	array<Vec2> setValues = s.values();
	setValues[0].x = 44;
	expect_eq(m[equalKey], 17);
	expect_eq(ordered[equalKey], 23);
	expect_eq(s.has(equalKey) ? 1 : 0, 1);
	expect_eq(ordered.remove(equalKey) ? 1 : 0, 1);
	expect_eq(len(ordered), 0);
	expect_eq(len(ordered.keys()), 0);
	expect_eq(len(ordered.values()), 0);
	expect_eq(len(ordered.entries()), 0);
	map<Vec2,int> nativeMap = native_vec_map();
	ordered_map<Vec2,int> nativeOrdered = native_vec_ordered_map();
	set<Vec2> nativeSet = native_vec_set();
	expect_eq(nativeMap[Vec2(7, 8)], 31);
	expect_eq(nativeOrdered[Vec2(9, 10)], 37);
	expect_eq(nativeSet.has(Vec2(11, 12)) ? 1 : 0, 1);
	optional<Vec2> maybeVec = optional<Vec2>(Vec2(13, 14));
	expect_eq(maybeVec.get().x, 13);
	maybeVec.reset();
	expect_eq(maybeVec.has_value() ? 1 : 0, 0);
}
)",
			"<host_struct_value_key>",
			StructOptions);
		if (!StructValueKeyCompile.bSuccess || !StructValueKeyCompile.Module)
		{
			std::cerr << "host struct value key compile failed: "
				<< StructValueKeyCompile.Error.Message << '\n';
			return Fail("fully capable Host Struct should be accepted as a map/set key");
		}
		VeroScriptVm StructValueKeyVm(std::cout);
		VeroScript::Test::Configure(StructValueKeyVm);
		if (!StructValueKeyVm.Execute(
				*StructValueKeyCompile.Module,
				ERunMode::Script,
				{},
				&StructRegistry.GetNativeFunctions()))
		{
			std::cerr << "host struct value key run failed: "
				<< StructValueKeyVm.GetError().Message << '\n';
			return Fail("host struct map/set keys must use copy/hash/equality semantics");
		}
		const FCompileResult StructCompile = VeroScriptCompile::CompileSource(
			R"(
void take(Vec2 v) {
	v.x = 99;
}

void main() {
	Vec2 a = Vec2(1, 2);
	take(a);
	expect_eq(a.x, 1);
	Vec2 b = a;
	b.x = 7;
	expect_eq(a.x, 1);
	expect_eq(b.x, 7);
}
)",
			"<host_struct>",
			StructOptions);
		if (!StructCompile.bSuccess || !StructCompile.Module)
		{
			std::cerr << "host struct compile failed: " << StructCompile.Error.Message << '\n';
			return Fail("Vec2 struct binding should compile");
		}

		VeroScriptVm StructVm(std::cout);
		VeroScript::Test::Configure(StructVm);
		if (!StructVm.Execute(
				*StructCompile.Module,
				ERunMode::Script,
				{},
					&StructRegistry.GetNativeFunctions()))
		{
			std::cerr << "host struct run failed: " << StructVm.GetError().Message << '\n';
			return Fail("Vec2 struct binding should run");
		}

		{
			// Nested host-struct field must deep-copy via stamped @clone (not share).
			const FCompileResult NestedCompile = VeroScriptCompile::CompileSource(
				R"(
struct Holder {
	Vec2 v = Vec2(0, 0);
}

void main() {
	Holder a = Holder();
	a.v = Vec2(1, 2);
	Holder b = a;
	b.v.x = 9;
	expect_eq(a.v.x, 1);
	expect_eq(b.v.x, 9);
}
)",
				"<host_struct_nested>",
				StructOptions);
			if (!NestedCompile.bSuccess || !NestedCompile.Module)
			{
				std::cerr << "nested host struct compile failed: "
					<< NestedCompile.Error.Message << '\n';
				return Fail("nested Vec2 field copy should compile");
			}
			VeroScriptVm NestedVm(std::cout);
		VeroScript::Test::Configure(NestedVm);
			if (!NestedVm.Execute(
					*NestedCompile.Module,
					ERunMode::Script,
					{},
						&StructRegistry.GetNativeFunctions()))
			{
				std::cerr << "nested host struct run failed: "
					<< NestedVm.GetError().Message << '\n';
				return Fail("nested Vec2 field copy should run");
			}
		}

		{
			// Bare host-struct field without initializer must be rejected at compile time.
			const FCompileResult BareFieldCompile = VeroScriptCompile::CompileSource(
				R"(
struct BadHolder {
	Vec2 v;
}
void main() {}
)",
				"<host_struct_bare_field>",
				StructOptions);
			if (BareFieldCompile.bSuccess)
			{
				return Fail("bare host struct field without initializer should fail compile");
			}
		}

		{
			// Fixed array of host structs must deep-copy elements on assign.
			const FCompileResult ArrayCompile = VeroScriptCompile::CompileSource(
				R"(
void main() {
	Vec2 a[2] = { Vec2(1, 2), Vec2(3, 4) };
	Vec2 b[2] = a;
	b[0].x = 9;
	expect_eq(a[0].x, 1);
	expect_eq(b[0].x, 9);
}
)",
				"<host_struct_array>",
				StructOptions);
			if (!ArrayCompile.bSuccess || !ArrayCompile.Module)
			{
				std::cerr << "host struct array compile failed: "
					<< ArrayCompile.Error.Message << '\n';
				return Fail("Vec2[N] copy should compile");
			}
			VeroScriptVm ArrayVm(std::cout);
		VeroScript::Test::Configure(ArrayVm);
			if (!ArrayVm.Execute(
					*ArrayCompile.Module,
					ERunMode::Script,
					{},
						&StructRegistry.GetNativeFunctions()))
			{
				std::cerr << "host struct array run failed: "
					<< ArrayVm.GetError().Message << '\n';
				return Fail("Vec2[N] copy should run");
			}
		}

		{
			// Dynamic array of host structs must deep-copy on push (not share).
			const FCompileResult DynCompile = VeroScriptCompile::CompileSource(
				R"(
void main() {
	array<Vec2> a = array<Vec2>();
	Vec2 v = Vec2(1, 2);
	a.push(v);
	a[0].x = 9;
	expect_eq(v.x, 1);
	expect_eq(a[0].x, 9);
}
)",
				"<host_struct_dynarray>",
				StructOptions);
			if (!DynCompile.bSuccess || !DynCompile.Module)
			{
				std::cerr << "host struct dynarray compile failed: "
					<< DynCompile.Error.Message << '\n';
				return Fail("array<Vec2>.push should compile");
			}
			VeroScriptVm DynVm(std::cout);
		VeroScript::Test::Configure(DynVm);
			if (!DynVm.Execute(
					*DynCompile.Module,
					ERunMode::Script,
					{},
						&StructRegistry.GetNativeFunctions()))
			{
				std::cerr << "host struct dynarray run failed: "
					<< DynVm.GetError().Message << '\n';
				return Fail("array<Vec2>.push should deep-copy");
			}
		}
	}

	{
		FHostBindingRegistry StaticRegistry;
		FHostTypeId MathId;
		FHostStaticProbe StaticProbe;
		if (!StaticRegistry.RegisterType("HostMath", EHostTypeKind::Class, MathId, ErrorId))
		{
			return Fail("RegisterType HostMath should succeed");
		}
		if (!StaticRegistry.SetTypeInheritable(MathId, true, ErrorId))
		{
			return Fail("HostMath should be inheritable for Super static-method coverage");
		}
		FHostTypeId BoxId;
		if (!StaticRegistry.RegisterType(
				"StaticHostBox",
				EHostTypeKind::Class,
				BoxId,
				ErrorId))
		{
			return Fail("RegisterType StaticHostBox should succeed");
		}

		FHostMemberDesc BoxCtor;
		BoxCtor.Kind = EHostMemberKind::Constructor;
		BoxCtor.ReturnType = ENativeType::HostObject;
		BoxCtor.ParamTypes = {ENativeType::Int};
		BoxCtor.Callback = StaticHostBoxCtor;
		if (!StaticRegistry.RegisterMember(BoxId, BoxCtor, ErrorId))
		{
			return Fail("RegisterMember StaticHostBox constructor should succeed");
		}

		FHostMemberDesc BoxGet;
		BoxGet.Kind = EHostMemberKind::Method;
		BoxGet.Name = "get";
		BoxGet.ReturnType = ENativeType::Int;
		BoxGet.Callback = StaticHostBoxInstanceGet;
		BoxGet.UserData = &StaticProbe;
		if (!StaticRegistry.RegisterMember(BoxId, BoxGet, ErrorId))
		{
			return Fail("RegisterMember StaticHostBox.get should succeed");
		}

		FHostMemberDesc Add;
		Add.Kind = EHostMemberKind::StaticMethod;
		Add.Name = "add";
		Add.ReturnType = ENativeType::Int;
		Add.ParamTypes = {ENativeType::Int, ENativeType::Int};
		Add.Callback = HostAdd;
		if (!StaticRegistry.RegisterMember(MathId, Add, ErrorId))
		{
			std::cerr << ErrorId << '\n';
			return Fail("RegisterMember HostMath.add should succeed");
		}

		FHostMemberDesc Answer;
		Answer.Kind = EHostMemberKind::StaticConstant;
		Answer.Name = "Answer";
		Answer.ReturnType = ENativeType::Int;
		Answer.Callback = HostAnswer;
		if (!StaticRegistry.RegisterMember(MathId, Answer, ErrorId))
		{
			std::cerr << ErrorId << '\n';
			return Fail("RegisterMember HostMath.Answer should succeed");
		}

		FHostMemberDesc Inspect;
		Inspect.Kind = EHostMemberKind::StaticMethod;
		Inspect.Name = "inspect";
		Inspect.ReturnType = ENativeType::Int;
		Inspect.ParamTypes = {ENativeType::HostObject, ENativeType::Int};
		Inspect.ParamHostTypeNames = {"StaticHostBox", ""};
		Inspect.Callback = StaticHostBoxInspect;
		Inspect.UserData = &StaticProbe;
		if (!StaticRegistry.RegisterMember(MathId, Inspect, ErrorId))
		{
			std::cerr << ErrorId << '\n';
			return Fail("RegisterMember HostMath.inspect should succeed");
		}
		FHostMemberDesc AcceptNullable;
		AcceptNullable.Kind = EHostMemberKind::StaticMethod;
		AcceptNullable.Name = "accept_null";
		AcceptNullable.ReturnType = ENativeType::Int;
		AcceptNullable.ParamTypes = {ENativeType::HostObject};
		AcceptNullable.ParamHostTypeNames = {"StaticHostBox"};
		AcceptNullable.Callback = StaticHostBoxAcceptNullable;
		if (!StaticRegistry.RegisterMember(MathId, AcceptNullable, ErrorId))
		{
			std::cerr << ErrorId << '\n';
			return Fail("RegisterMember HostMath.accept_null should succeed");
		}

		if (!StaticRegistry.RegisterNative(
				"expect_eq",
				ENativeType::Void,
				{ENativeType::Int, ENativeType::Int},
				ExpectEq,
				nullptr,
				ErrorId))
		{
			return Fail("RegisterNative expect_eq (static) should succeed");
		}

		FCompileOptions StaticOptions = VeroScript::Test::MakeCompileOptions();
		StaticOptions.Bindings = &StaticRegistry;
		const FCompileResult StaticCompile = VeroScriptCompile::CompileSource(
			R"(
class StaticMathUser : HostMath {
public:
	int inspectBase(StaticHostBox box) { return Super.inspect(box, 2); }
}

void main() {
	expect_eq(HostMath.add(2, 3), 5);
	expect_eq(HostMath::Answer, 42);
	StaticHostBox box = StaticHostBox(7);
	expect_eq(HostMath.inspect(box, 5), 12);
	expect_eq(HostMath.accept_null(nullptr), 1);
	expect_eq(box.get(), 7);
}
)",
			"<host_static>",
			StaticOptions);
		if (!StaticCompile.bSuccess || !StaticCompile.Module)
		{
			std::cerr << "host static compile failed: " << StaticCompile.Error.Message << '\n';
			return Fail("HostMath.static method should compile");
		}

		VeroScriptVm StaticVm(std::cout);
		VeroScript::Test::Configure(StaticVm);
		if (!StaticVm.Execute(
				*StaticCompile.Module,
				ERunMode::Script,
				{},
					&StaticRegistry.GetNativeFunctions()))
		{
			std::cerr << "host static run failed: " << StaticVm.GetError().Message << '\n';
			return Fail("HostMath.static method should run");
		}
		if (!GStaticConstantContextValid
			|| StaticProbe.StaticCalls != 1
			|| StaticProbe.InstanceCalls != 1
			|| !StaticProbe.bStaticHostThisWasNull
			|| !StaticProbe.bStaticReceivedEveryArgument
			|| !StaticProbe.bInstanceReceivedHostThis)
		{
			return Fail("host static/instance call contexts should preserve receiver semantics");
		}

		StaticProbe.bCorruptTypeAfterStaticCall = true;
		const FCompileResult ValidationCompile = VeroScriptCompile::CompileSource(
			R"(
void main() {
	StaticHostBox box = StaticHostBox(9);
	HostMath.inspect(box, 1);
	HostMath.inspect(box, 1);
}
)",
			"<host_static_object_validation>",
			StaticOptions);
		if (!ValidationCompile.bSuccess || !ValidationCompile.Module)
		{
			return Fail("static HostObject validation script should compile");
		}
		VeroScriptVm ValidationVm(std::cout);
		VeroScript::Test::Configure(ValidationVm);
		if (ValidationVm.Execute(
				*ValidationCompile.Module,
				ERunMode::Script,
				{},
				&StaticRegistry.GetNativeFunctions()))
		{
			return Fail("static HostObject runtime type mismatch should fail");
		}
		if (ValidationVm.GetError().Message.find("host object type mismatch") == std::string::npos
			|| StaticProbe.StaticCalls != 2)
		{
			std::cerr << ValidationVm.GetError().Message << '\n';
			return Fail("static HostObject type validation must run before callback");
		}

		// A manually supplied legacy native table has no explicit static-member
		// marker. Preserve the pre-existing parameter-shape inference for it.
		FNativeFunction LegacyAdd;
		LegacyAdd.Name = MakeHostMethodNativeName("LegacyMath", "add");
		LegacyAdd.ReturnType = ENativeType::Int;
		LegacyAdd.ParamTypes = {ENativeType::Int, ENativeType::Int};
		LegacyAdd.Callback = HostAdd;
		std::vector<FNativeFunction> LegacyNatives;
		LegacyNatives.push_back(std::move(LegacyAdd));
		FCompileOptions LegacyOptions = VeroScript::Test::MakeCompileOptions();
		LegacyOptions.Natives = &LegacyNatives;
		const FCompileResult LegacyCompile = VeroScriptCompile::CompileSource(
			R"(
class LegacyMath {}
class Reader<T> { public: T pad;
    int read() { int n = T.add(4, 5); return n; }
};
void main() {
    int n = LegacyMath.add(2, 3); print(n);
    Reader<LegacyMath> reader = Reader<LegacyMath>(); print(reader.read());
}
)",
			"<legacy_host_static_fallback>",
			LegacyOptions);
		if (!LegacyCompile.bSuccess || !LegacyCompile.Module)
		{
			std::cerr << LegacyCompile.Error.Message << '\n';
			return Fail("legacy host static parameter-shape fallback should compile");
		}
		std::ostringstream LegacyOutput;
		VeroScriptVm LegacyVm(LegacyOutput);
		VeroScript::Test::Configure(LegacyVm);
		if (!LegacyVm.Execute(
				*LegacyCompile.Module,
				ERunMode::Script,
				{},
				&LegacyNatives)
			|| LegacyOutput.str() != "5\n9\n")
		{
			return Fail("legacy host static parameter-shape fallback should run");
		}
	}

	{
		FHostBindingRegistry FieldRegistry;
		FHostTypeId CounterId;
		if (!FieldRegistry.RegisterType("Counter", EHostTypeKind::Class, CounterId, ErrorId))
		{
			return Fail("RegisterType Counter (field) should succeed");
		}

		FHostMemberDesc Ctor;
		Ctor.Kind = EHostMemberKind::Constructor;
		Ctor.ReturnType = ENativeType::HostObject;
		Ctor.ParamTypes = {ENativeType::Int};
		Ctor.Callback = CounterCtor;
		if (!FieldRegistry.RegisterMember(CounterId, Ctor, ErrorId))
		{
			return Fail("RegisterMember Counter ctor (field) should succeed");
		}

		FHostMemberDesc Field;
		Field.Kind = EHostMemberKind::Field;
		Field.Name = "value";
		Field.ReturnType = ENativeType::Int;
		Field.Callback = CounterGetValue;
		if (!FieldRegistry.RegisterMember(CounterId, Field, ErrorId))
		{
			std::cerr << ErrorId << '\n';
			return Fail("RegisterMember Counter.value Field should succeed");
		}

		FHostMemberDesc Setter;
		Setter.Kind = EHostMemberKind::PropertySetter;
		Setter.Name = "value";
		Setter.ParamTypes = {ENativeType::Int};
		Setter.Callback = CounterSetValue;
		if (!FieldRegistry.RegisterMember(CounterId, Setter, ErrorId))
		{
			return Fail("RegisterMember Counter.value setter (field) should succeed");
		}

		if (!FieldRegistry.RegisterNative(
				"expect_eq",
				ENativeType::Void,
				{ENativeType::Int, ENativeType::Int},
				ExpectEq,
				nullptr,
				ErrorId))
		{
			return Fail("RegisterNative expect_eq (field) should succeed");
		}

		FCompileOptions FieldOptions = VeroScript::Test::MakeCompileOptions();
		FieldOptions.Bindings = &FieldRegistry;
		const FCompileResult FieldCompile = VeroScriptCompile::CompileSource(
			R"(
void main() {
	Counter c = Counter(3);
	expect_eq(c.value, 3);
	c.value = 9;
	expect_eq(c.value, 9);
}
)",
			"<host_field>",
			FieldOptions);
		if (!FieldCompile.bSuccess || !FieldCompile.Module)
		{
			std::cerr << "host field compile failed: " << FieldCompile.Error.Message << '\n';
			return Fail("Counter Field binding should compile");
		}

		VeroScriptVm FieldVm(std::cout);
		VeroScript::Test::Configure(FieldVm);
		if (!FieldVm.Execute(
				*FieldCompile.Module,
				ERunMode::Script,
				{},
					&FieldRegistry.GetNativeFunctions()))
		{
			std::cerr << "host field run failed: " << FieldVm.GetError().Message << '\n';
			return Fail("Counter Field binding should run");
		}
	}
