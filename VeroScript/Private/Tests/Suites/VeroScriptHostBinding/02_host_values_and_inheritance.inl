// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Internal fragment: VeroScriptHostBindingTests / host values and inheritance suite. Included by the owning translation unit.

{
	int DestroyCount = 0;
	FValue Owned = std::make_shared<FScriptObject>();
	auto& Object = std::get<FObjectHandle>(Owned);
	Object->SetHostBase(MakeHostObject(1, "OwnedHost", &DestroyCount,
			[](void* Payload)
			{
				++*static_cast<int*>(Payload);
			}));
	const std::weak_ptr<FScriptObject> ScriptLifetime = Object;
	const std::weak_ptr<FHostObject> HostLifetime = Object->GetHostBase();
	std::string Error;
	if (!CoerceValueToNativeType(ENativeType::HostObject, Owned, Error)
			|| DestroyCount != 0 || !ScriptLifetime.expired() || HostLifetime.expired()
			|| !std::holds_alternative<FHostObjectHandle>(Owned))
	{
		return Fail("unwrapping the last script owner must retain its host payload");
	}
	Owned = std::int32_t{0};
	if (DestroyCount != 1 || !HostLifetime.expired())
	{
		return Fail("an unwrapped host payload must be released exactly once");
	}
}

{
	FHostBindingRegistry Source;
	FHostTypeId ColorId;
	if (!Source.RegisterType("Color", EHostTypeKind::Enum, ColorId, ErrorId))
	{
		return Fail("catalog export Color type should register");
	}
	FHostMemberDesc Red;
	Red.Kind = EHostMemberKind::EnumMember;
	Red.Name = "Red";
	Red.EnumValue = 1;
	if (!Source.RegisterMember(ColorId, Red, ErrorId))
	{
		return Fail("catalog export Color.Red should register");
	}
	if (!Source.RegisterNative(
				"host_add",
				ENativeType::Int,
			{ENativeType::Int, ENativeType::Int},
				HostAdd,
				nullptr,
				ErrorId))
	{
		return Fail("catalog export host_add should register");
	}
	if (!Source.RegisterAnnotation("VSTAG", "annotation (Editor)", ErrorId)
			|| !Source.RegisterAnnotation("@hook", "annotation ()", ErrorId))
	{
		return Fail("catalog export annotations should register");
	}
	if (Source.RegisterAnnotation("VSTAG", "annotation ()", ErrorId))
	{
		return Fail("duplicate annotation name should be rejected");
	}
	if (Source.RegisterAnnotation("1bad", {}, ErrorId)
			|| Source.RegisterAnnotation("@", {}, ErrorId)
			|| Source.RegisterAnnotation("has space", {}, ErrorId))
	{
		return Fail("invalid annotation names should be rejected");
	}

	std::string Json;
	if (!ExportBindingCatalogJson(Source, Json) || Json.find("\"host_add\"") == std::string::npos)
	{
		return Fail("ExportBindingCatalogJson should include host_add");
	}

	FHostBindingRegistry Imported;
	std::string ImportError;
	if (!ImportBindingCatalogJson(Json, Imported, ImportError, true))
	{
		std::cerr << ImportError << '\n';
		return Fail("ImportBindingCatalogJson should succeed");
	}
	if (Imported.FindFunction("host_add") == nullptr || Imported.FindType("Color") == nullptr)
	{
		return Fail("imported catalog should contain host_add and Color");
	}
	{
		bool bFoundTag = false;
		bool bFoundHook = false;
		for (const FHostAnnotationDesc& Annotation : Imported.GetAnnotations())
		{
			bFoundTag = bFoundTag
					|| (Annotation.Name == "VSTAG" && Annotation.Detail == "annotation (Editor)");
			bFoundHook = bFoundHook || Annotation.Name == "@hook";
		}
		if (!bFoundTag || !bFoundHook)
		{
			return Fail("imported catalog should contain registered annotations");
		}
	}

	{
		const std::string TempPath = "veroscript_bindings_roundtrip_tmp.json";
		std::string SaveError;
		if (!SaveBindingCatalogFile(Imported, TempPath, SaveError))
		{
			std::cerr << SaveError << '\n';
			return Fail("SaveBindingCatalogFile should succeed");
		}
		FHostBindingRegistry Reloaded;
		std::string ReloadError;
		if (!LoadBindingCatalogFile(TempPath, Reloaded, ReloadError, true))
		{
			std::cerr << ReloadError << '\n';
			std::remove(TempPath.c_str());
			return Fail("reload after SaveBindingCatalogFile should succeed");
		}
		std::remove(TempPath.c_str());
		if (Reloaded.FindFunction("host_add") == nullptr || Reloaded.FindType("Color") == nullptr)
		{
			return Fail("saved catalog should roundtrip host_add and Color");
		}
		if (Reloaded.GetAnnotations().size() != 2)
		{
			return Fail("saved catalog should roundtrip annotations");
		}
	}

	FCompileOptions CatalogRoundtripOptions = VeroScript::Test::MakeCompileOptions();
	CatalogRoundtripOptions.Bindings = &Imported;
	const FCompileResult Compile = VeroScriptCompile::CompileSource(
			"void main() { int x = host_add(1, 2); Color c = Color.Red; }\n",
			"<catalog_roundtrip>",
			CatalogRoundtripOptions);
	if (!Compile.bSuccess)
	{
		std::cerr << Compile.Error.Message << '\n';
		return Fail("imported catalog should compile host_add / Color.Red");
	}
}

{
	// Class ctor must round-trip without polluting GetFunctions() / duplicate RegisterNative.
	FHostBindingRegistry Source;
	FHostTypeId CounterId;
	if (!Source.RegisterType("Counter", EHostTypeKind::Class, CounterId, ErrorId))
	{
		return Fail("ctor roundtrip: RegisterType Counter");
	}
	FHostMemberDesc Ctor;
	Ctor.Kind = EHostMemberKind::Constructor;
	Ctor.ParamTypes = {ENativeType::Int};
	Ctor.Callback = CounterCtor;
	if (!Source.RegisterMember(CounterId, Ctor, ErrorId))
	{
		return Fail("ctor roundtrip: RegisterMember ctor");
	}
	if (Source.FindFunction("Counter") != nullptr)
	{
		return Fail("ctor roundtrip: Counter must not appear in GetFunctions()");
	}

	std::string Json;
	if (!ExportBindingCatalogJson(Source, Json) || Json.find("\"Counter\"") == std::string::npos)
	{
		return Fail("ctor roundtrip: export should include Counter type");
	}
	if (Json.find("\"functions\"") != std::string::npos
			&& Json.find("\"name\": \"Counter\"") != std::string::npos
			&& Json.find("\"types\"") != std::string::npos)
	{
		// Ensure Counter is not listed as a free function entry.
		const std::size_t FunctionsPos = Json.find("\"functions\"");
		const std::size_t TypesPos = Json.find("\"types\"");
		const std::size_t CounterAsFn = Json.find("\"name\": \"Counter\"", FunctionsPos);
		if (CounterAsFn != std::string::npos && CounterAsFn < TypesPos)
		{
			return Fail("ctor roundtrip: Counter must not be exported under functions[]");
		}
	}

	FHostBindingRegistry Imported;
	std::string ImportError;
	if (!ImportBindingCatalogJson(Json, Imported, ImportError, true))
	{
		std::cerr << ImportError << '\n';
		return Fail("ctor roundtrip: import should succeed");
	}
	if (Imported.FindType("Counter") == nullptr)
	{
		return Fail("ctor roundtrip: imported Counter type missing");
	}
	if (Imported.FindFunction("Counter") != nullptr)
	{
		return Fail("ctor roundtrip: imported Counter must not be a free function");
	}
}

{
	// Cross-type HostObject refs require two-pass import (Actor after Widget).
	const char* CrossTypeJson = R"({
  "version": 1,
  "functions": [],
  "types": [
    {
      "name": "Actor",
      "kind": "class",
      "members": [
        {
          "kind": "method",
          "name": "attach",
          "returnType": "void",
          "paramTypes": ["object"],
          "enumValue": 0,
          "returnHostTypeName": "",
          "paramHostTypeNames": ["Widget"]
        }
      ]
    },
    {
      "name": "Widget",
      "kind": "class",
      "members": []
    }
  ]
})";
	FHostBindingRegistry Imported;
	std::string ImportError;
	if (!ImportBindingCatalogJson(CrossTypeJson, Imported, ImportError, true))
	{
		std::cerr << ImportError << '\n';
		return Fail("cross-type catalog import should succeed (Widget after Actor)");
	}
	if (Imported.FindType("Actor") == nullptr || Imported.FindType("Widget") == nullptr)
	{
		return Fail("cross-type catalog should register Actor and Widget");
	}
}

{
	// inheritable / hasClone / hasSubclass must survive catalog export and import.
	// so IDE/LSP Analyze can accept `class X : Host` without a live Session.
	FHostBindingRegistry Source;
	FHostTypeId ActorId;
	FHostTypeId VecId;
	if (!Source.RegisterType("AActor", EHostTypeKind::Class, ActorId, ErrorId)
			|| !Source.SetTypeInheritable(ActorId, true, ErrorId)
			|| !Source.RegisterTypeSubclassFactory(
				ActorId, ActorSubclassFactory, nullptr, ErrorId)
					|| !Source.RegisterType("Vec2", EHostTypeKind::Struct, VecId, ErrorId)
					|| !Source.SetTypeInheritable(VecId, true, ErrorId)
					|| !Source.RegisterTypeClone(VecId, Vec2Clone, nullptr, ErrorId)
					|| !Source.RegisterTypeEquals(VecId, Vec2Equals, nullptr, ErrorId)
					|| !Source.RegisterTypeHash(VecId, Vec2Hash, nullptr, ErrorId))
	{
		return Fail("inheritable catalog source registry setup should succeed");
	}
	FHostMemberDesc GetValue;
	GetValue.Kind = EHostMemberKind::Method;
	GetValue.Name = "get_value";
	GetValue.ReturnType = ENativeType::Int;
	GetValue.bConstMethod = true;
	GetValue.Callback = ActorGetValue;
	if (!Source.RegisterMember(ActorId, GetValue, ErrorId))
	{
		return Fail("inheritable catalog: RegisterMember get_value");
	}

	std::string Json;
	if (!ExportBindingCatalogJson(Source, Json)
			|| Json.find("\"inheritable\": true") == std::string::npos
			|| Json.find("\"constMethod\": true") == std::string::npos
			|| Json.find("\"hasSubclass\": true") == std::string::npos
			|| Json.find("\"hasClone\": true") == std::string::npos
			|| Json.find("\"hasEquals\": true") == std::string::npos
			|| Json.find("\"hasHash\": true") == std::string::npos)
	{
		return Fail("export should emit inheritable and host value capabilities");
	}

	FHostBindingRegistry Imported;
	std::string ImportError;
	if (!ImportBindingCatalogJson(Json, Imported, ImportError, true))
	{
		std::cerr << ImportError << '\n';
		return Fail("import inheritable catalog should succeed");
	}
	const FHostTypeDesc* const ImportedActor = Imported.FindType("AActor");
	const FHostTypeDesc* const ImportedVec = Imported.FindType("Vec2");
	bool bImportedConstMethod = false;
	if (ImportedActor)
	{
		for (const FHostMemberDesc& Member : ImportedActor->Members)
		{
			bImportedConstMethod = bImportedConstMethod
					|| (Member.Name == "get_value" && Member.bConstMethod
						&& FormatHostMemberSignature(Member, "AActor").find(") const")
							!= std::string::npos);
		}
	}
	if (!ImportedActor || !ImportedActor->bInheritable || !bImportedConstMethod
			|| ImportedActor->SubclassFactory == nullptr
			|| !ImportedVec || !ImportedVec->bInheritable
			|| ImportedVec->CloneCallback == nullptr
			|| ImportedVec->EqualsCallback == nullptr
			|| ImportedVec->HashCallback == nullptr)
	{
		return Fail("imported catalog should restore inheritable and stub factories");
	}

	FCompileOptions InheritableCatalogOptions = VeroScript::Test::MakeCompileOptions();
	InheritableCatalogOptions.Bindings = &Imported;
	const FCompileResult Compile = VeroScriptCompile::CompileSource(
			"class MyActor : AActor {\n"
			"public:\n"
			"\tMyActor() {}\n"
			"}\n"
			"struct MyVec : Vec2 {}\n"
			"void main() {}\n",
			"<catalog_inheritable_roundtrip>",
			InheritableCatalogOptions);
	if (!Compile.bSuccess)
	{
		std::cerr << Compile.Error.Message << '\n';
		return Fail("imported inheritable catalog should compile host subclass decls");
	}

	// Append mode must also restore flags onto a fresh target type.
	FHostBindingRegistry AppendTarget;
	FHostTypeId PreId;
	if (!AppendTarget.RegisterType("Other", EHostTypeKind::Class, PreId, ErrorId))
	{
		return Fail("append catalog: seed Other type");
	}
	std::string AppendError;
	if (!ImportBindingCatalogJson(Json, AppendTarget, AppendError, false))
	{
		std::cerr << AppendError << '\n';
		return Fail("append import inheritable catalog should succeed");
	}
	const FHostTypeDesc* const AppendedActor = AppendTarget.FindType("AActor");
	if (!AppendedActor || !AppendedActor->bInheritable
			|| AppendedActor->SubclassFactory == nullptr
			|| AppendTarget.FindType("Other") == nullptr)
	{
		return Fail("append import should keep prior types and restore inheritable flags");
	}
}

{
	FHostBindingRegistry InheritRegistry;
	FHostTypeId ActorId;
	if (!InheritRegistry.RegisterType("AActor", EHostTypeKind::Class, ActorId, ErrorId))
	{
		return Fail("RegisterType AActor should succeed");
	}
	FHostMemberDesc GetValue;
	GetValue.Kind = EHostMemberKind::Method;
	GetValue.Name = "get_value";
	GetValue.ReturnType = ENativeType::Int;
	GetValue.Callback = ActorGetValue;
	if (!InheritRegistry.RegisterMember(ActorId, GetValue, ErrorId))
	{
		return Fail("RegisterMember AActor.get_value should succeed");
	}
	if (!InheritRegistry.RegisterNative(
				"expect_eq",
				ENativeType::Void,
			{ENativeType::Int, ENativeType::Int},
				ExpectEq,
				nullptr,
				ErrorId))
	{
		return Fail("RegisterNative expect_eq (inherit) should succeed");
	}

	{
		FCompileOptions BadOptions = VeroScript::Test::MakeCompileOptions();
		BadOptions.Bindings = &InheritRegistry;
		const FCompileResult BadCompile = VeroScriptCompile::CompileSource(
				"class MyActor : AActor {}\n"
				"void main() {}\n",
				"<host_inherit_not_marked>",
				BadOptions);
		if (BadCompile.bSuccess)
		{
			return Fail("inheriting non-inheritable host type should fail");
		}
	}

	if (!InheritRegistry.SetTypeInheritable(ActorId, true, ErrorId))
	{
		return Fail("SetTypeInheritable AActor should succeed");
	}
	{
		FHostTypeId EnumId;
		if (!InheritRegistry.RegisterType("Color", EHostTypeKind::Enum, EnumId, ErrorId))
		{
			return Fail("RegisterType Color should succeed");
		}
		if (InheritRegistry.SetTypeInheritable(EnumId, true, ErrorId))
		{
			return Fail("SetTypeInheritable on enum should fail");
		}
		FCompileOptions EnumBaseOptions = VeroScript::Test::MakeCompileOptions();
		EnumBaseOptions.Bindings = &InheritRegistry;
		const FCompileResult EnumBaseCompile = VeroScriptCompile::CompileSource(
				"class Bad : Color {}\n"
				"void main() {}\n",
				"<host_inherit_enum>",
				EnumBaseOptions);
		if (EnumBaseCompile.bSuccess)
		{
			return Fail("inheriting host enum should fail");
		}
	}

	ActorSubclassCalls = 0;
	if (!InheritRegistry.RegisterTypeSubclassFactory(
				ActorId, ActorSubclassFactory, nullptr, ErrorId))
	{
		std::cerr << ErrorId << '\n';
		return Fail("RegisterTypeSubclassFactory AActor should succeed");
	}

	{
		FCompileOptions MixOptions = VeroScript::Test::MakeCompileOptions();
		MixOptions.Bindings = &InheritRegistry;
		const FCompileResult MixCompile = VeroScriptCompile::CompileSource(
				"class ScriptBase {}\n"
				"class Mixed : ScriptBase, AActor {}\n"
				"void main() {}\n",
				"<host_inherit_mix>",
				MixOptions);
		if (MixCompile.bSuccess)
		{
			return Fail("mixing script and host bases should fail");
		}
	}

	FCompileOptions ClassOptions = VeroScript::Test::MakeCompileOptions();
	ClassOptions.Bindings = &InheritRegistry;
	const FCompileResult ClassCompile = VeroScriptCompile::CompileSource(
			"class MyActor : AActor {\n"
			"public:\n"
			"\tint Extra = 3;\n"
			"}\n"
			"void main() {\n"
			"\tMyActor a = MyActor();\n"
			"\texpect_eq(a.get_value(), 42);\n"
			"\texpect_eq(a.Extra, 3);\n"
			"}\n",
			"<host_inherit_class>",
			ClassOptions);
	if (!ClassCompile.bSuccess || !ClassCompile.Module)
	{
		std::cerr << "host inherit class compile failed: " << ClassCompile.Error.Message << '\n';
		return Fail("MyActor : AActor should compile");
	}
	bool bFoundHostBase = false;
	for (const FBytecodeTypeDesc& TypeDesc : ClassCompile.Module->Types)
	{
		if (TypeDesc.Name.find("MyActor") == std::string::npos)
		{
			continue;
		}
		if (TypeDesc.HostBaseTypeName != "AActor"
				|| TypeDesc.HostBaseTypeId != ActorId.Value
				|| !TypeDesc.BaseTypeIndices.empty())
		{
			return Fail("MyActor should record HostBase metadata only");
		}
		bFoundHostBase = true;
	}
	if (!bFoundHostBase)
	{
		return Fail("MyActor type desc missing");
	}

	{
		const std::string CachePath = "host_inherit_class.vsbc";
		if (!VeroScriptBytecodeIO::WriteFile(CachePath, *ClassCompile.Module, 1))
		{
			return Fail("write host inherit bytecode should succeed");
		}
		std::string ReadError;
		const std::unique_ptr<FBytecodeModule> Reloaded =
				VeroScriptBytecodeIO::ReadFile(CachePath, 1, ReadError);
		std::remove(CachePath.c_str());
		if (!Reloaded)
		{
			std::cerr << ReadError << '\n';
			return Fail("read host inherit bytecode should succeed");
		}
		bool bRoundTrip = false;
		for (const FBytecodeTypeDesc& TypeDesc : Reloaded->Types)
		{
			if (TypeDesc.Name.find("MyActor") == std::string::npos)
			{
				continue;
			}
			if (TypeDesc.HostBaseTypeName != "AActor"
					|| TypeDesc.HostBaseTypeId != ActorId.Value)
			{
				return Fail("HostBase metadata should survive bytecode round-trip");
			}
			bRoundTrip = true;
		}
		if (!bRoundTrip)
		{
			return Fail("round-trip MyActor type missing");
		}
	}

	VeroScriptVm ClassVm(std::cout);
		VeroScript::Test::Configure(ClassVm);
	if (!ClassVm.Execute(
				*ClassCompile.Module,
				ERunMode::Script,
			{},
					&InheritRegistry.GetNativeFunctions()))
	{
		std::cerr << "host inherit class run failed: " << ClassVm.GetError().Message << '\n';
		return Fail("MyActor : AActor should run");
	}
	if (ActorSubclassCalls != 1)
	{
		return Fail("AActor @subclass factory should run once");
	}
}

{
	FHostBindingRegistry NullFactoryRegistry;
	FHostTypeId ActorId;
	if (!NullFactoryRegistry.RegisterType("AActor", EHostTypeKind::Class, ActorId, ErrorId)
			|| !NullFactoryRegistry.SetTypeInheritable(ActorId, true, ErrorId)
			|| !NullFactoryRegistry.RegisterTypeSubclassFactory(
				ActorId, ActorSubclassFactoryNull, nullptr, ErrorId))
	{
		return Fail("null-factory registry setup should succeed");
	}
	FCompileOptions NullFactoryOptions = VeroScript::Test::MakeCompileOptions();
	NullFactoryOptions.Bindings = &NullFactoryRegistry;
	const FCompileResult Compile = VeroScriptCompile::CompileSource(
			"class MyActor : AActor {}\n"
			"void main() { MyActor a = MyActor(); }\n",
			"<host_inherit_null_factory>",
			NullFactoryOptions);
	if (!Compile.bSuccess || !Compile.Module)
	{
		return Fail("null-factory suite should compile");
	}
	VeroScriptVm Vm(std::cout);
		VeroScript::Test::Configure(Vm);
	if (Vm.Execute(
				*Compile.Module,
				ERunMode::Script,
			{},
					&NullFactoryRegistry.GetNativeFunctions()))
	{
		return Fail("null subclass factory should fail at runtime");
	}
}

{
	FHostBindingRegistry NoFactoryRegistry;
	FHostTypeId ActorId;
	if (!NoFactoryRegistry.RegisterType("AActor", EHostTypeKind::Class, ActorId, ErrorId)
			|| !NoFactoryRegistry.SetTypeInheritable(ActorId, true, ErrorId))
	{
		return Fail("no-factory registry setup should succeed");
	}
	FCompileOptions NoFactoryOptions = VeroScript::Test::MakeCompileOptions();
	NoFactoryOptions.Bindings = &NoFactoryRegistry;
	const FCompileResult Compile = VeroScriptCompile::CompileSource(
			"class MyActor : AActor {}\n"
			"void main() { MyActor a = MyActor(); }\n",
			"<host_inherit_no_factory>",
			NoFactoryOptions);
	if (!Compile.bSuccess || !Compile.Module)
	{
		return Fail("no-factory suite should compile");
	}
	VeroScriptVm Vm(std::cout);
		VeroScript::Test::Configure(Vm);
	if (Vm.Execute(
				*Compile.Module,
				ERunMode::Script,
			{},
					&NoFactoryRegistry.GetNativeFunctions()))
	{
		return Fail("missing subclass factory should fail at runtime");
	}
}

{
	FHostBindingRegistry StructInheritRegistry;
	FHostTypeId VecId;
	if (!StructInheritRegistry.RegisterType("Vec2", EHostTypeKind::Struct, VecId, ErrorId))
	{
		return Fail("RegisterType Vec2 (inherit) should succeed");
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
	if (!StructInheritRegistry.RegisterMember(VecId, GetX, ErrorId)
			|| !StructInheritRegistry.RegisterMember(VecId, SetX, ErrorId)
			|| !StructInheritRegistry.RegisterTypeClone(VecId, Vec2Clone, nullptr, ErrorId)
			|| !StructInheritRegistry.SetTypeInheritable(VecId, true, ErrorId)
			|| !StructInheritRegistry.RegisterTypeSubclassFactory(
				VecId, Vec2SubclassFactory, nullptr, ErrorId)
					|| !StructInheritRegistry.RegisterNative(
						"expect_eq",
						ENativeType::Void, {ENativeType::Int, ENativeType::Int},
						ExpectEq,
						nullptr,
						ErrorId))
	{
		std::cerr << ErrorId << '\n';
		return Fail("Vec2 inherit registry setup should succeed");
	}

	{
		FCompileOptions KindOptions = VeroScript::Test::MakeCompileOptions();
		KindOptions.Bindings = &StructInheritRegistry;
		const FCompileResult KindCompile = VeroScriptCompile::CompileSource(
				"class Bad : Vec2 {}\n"
				"void main() {}\n",
				"<host_inherit_kind_mismatch>",
				KindOptions);
		if (KindCompile.bSuccess)
		{
			return Fail("class inheriting host struct should fail");
		}
	}

	Vec2SubclassCalls = 0;
	FCompileOptions StructOptions = VeroScript::Test::MakeCompileOptions();
	StructOptions.Bindings = &StructInheritRegistry;
	const FCompileResult StructCompile = VeroScriptCompile::CompileSource(
			"struct MyVec : Vec2 {\n"
			"\tint Tag = 7;\n"
			"}\n"
			"void main() {\n"
			"\tMyVec v = MyVec();\n"
			"\texpect_eq(v.x, 5);\n"
			"\texpect_eq(v.Tag, 7);\n"
			"\tMyVec copy = v;\n"
			"\tcopy.x = 11;\n"
			"\texpect_eq(v.x, 5);\n"
			"\texpect_eq(copy.x, 11);\n"
			"}\n",
			"<host_inherit_struct>",
			StructOptions);
	if (!StructCompile.bSuccess || !StructCompile.Module)
	{
		std::cerr << "host inherit struct compile failed: "
				<< StructCompile.Error.Message << '\n';
		return Fail("MyVec : Vec2 should compile");
	}
	VeroScriptVm StructVm(std::cout);
		VeroScript::Test::Configure(StructVm);
	if (!StructVm.Execute(
				*StructCompile.Module,
				ERunMode::Script,
			{},
					&StructInheritRegistry.GetNativeFunctions()))
	{
		std::cerr << "host inherit struct run failed: " << StructVm.GetError().Message << '\n';
		return Fail("MyVec : Vec2 should run");
	}
	if (Vec2SubclassCalls != 1)
	{
		return Fail("Vec2 @subclass factory should run once");
	}

	{
		const FCompileResult CopyCompile = VeroScriptCompile::CompileSource(
				"struct B:Vec2{B()=default;B(const B& source)=default;};\n"
				"struct H:B{H()=default;H(const H& source):B(source){}};\n"
				"void main(){H original=H();original.x=11;H copy=original;\n"
				"expect_eq(copy.x,11);copy.x=17;expect_eq(original.x,11);expect_eq(copy.x,17);}\n",
				"<host_inherit_struct_derived_copy>",
				StructOptions);
		if (!CopyCompile.bSuccess || !CopyCompile.Module)
		{
			std::cerr << "host inherit derived copy compile failed: " << CopyCompile.Error.Message << '\n';
			return Fail("derived handwritten copy with defaulted host base copy should compile");
		}
		Vec2SubclassCalls = 0;
		Vec2CloneCalls = 0;
		VeroScriptVm CopyVm(std::cout);
		VeroScript::Test::Configure(CopyVm);
		if (!CopyVm.Execute(*CopyCompile.Module, ERunMode::Script, {}, &StructInheritRegistry.GetNativeFunctions()))
		{
			std::cerr << "host inherit derived copy run failed: " << CopyVm.GetError().Message << '\n';
			return Fail("derived handwritten copy with defaulted host base copy should run");
		}
		if (Vec2SubclassCalls != 1 || Vec2CloneCalls != 1)
		{
			return Fail("derived copy should clone its host base without creating another host payload");
		}
	}

	{
		// Distinct HostBase instances: script fields may match, == must still be false.
		const FCompileResult EqCompile = VeroScriptCompile::CompileSource(
				"struct MyVec : Vec2 {\n"
				"\tint Tag = 7;\n"
				"}\n"
				"void main() {\n"
				"\tMyVec a = MyVec();\n"
				"\tMyVec b = MyVec();\n"
				"\tif (a == b) { expect_eq(0, 1); }\n"
				"}\n",
				"<host_inherit_struct_eq>",
				StructOptions);
		if (!EqCompile.bSuccess || !EqCompile.Module)
		{
			return Fail("host inherit struct equality suite should compile");
		}
		VeroScriptVm EqVm(std::cout);
		VeroScript::Test::Configure(EqVm);
		if (!EqVm.Execute(
					*EqCompile.Module,
					ERunMode::Script,
				{},
						&StructInheritRegistry.GetNativeFunctions()))
		{
			std::cerr << "host inherit struct eq run failed: "
					<< EqVm.GetError().Message << '\n';
			return Fail("MyVec HostBase inequality should hold");
		}
	}
}

{
	// Struct HostBase without @clone must fail at construction (never stamp null clone).
	FHostBindingRegistry NoCloneRegistry;
	FHostTypeId VecId;
	if (!NoCloneRegistry.RegisterType("Vec2", EHostTypeKind::Struct, VecId, ErrorId))
	{
		return Fail("RegisterType Vec2 (no-clone) should succeed");
	}
	FHostMemberDesc GetX;
	GetX.Kind = EHostMemberKind::PropertyGetter;
	GetX.Name = "x";
	GetX.ReturnType = ENativeType::Int;
	GetX.Callback = Vec2GetX;
	if (!NoCloneRegistry.RegisterMember(VecId, GetX, ErrorId)
			|| !NoCloneRegistry.SetTypeInheritable(VecId, true, ErrorId)
			|| !NoCloneRegistry.RegisterTypeSubclassFactory(
				VecId, Vec2SubclassFactory, nullptr, ErrorId))
	{
		return Fail("Vec2 no-clone registry setup should succeed");
	}
	FCompileOptions NoCloneOptions = VeroScript::Test::MakeCompileOptions();
	NoCloneOptions.Bindings = &NoCloneRegistry;
	const FCompileResult NoCloneCompile = VeroScriptCompile::CompileSource(
			"struct MyVec : Vec2 {\n"
			"\tint Tag = 1;\n"
			"}\n"
			"void main() {\n"
			"\tMyVec v = MyVec();\n"
			"}\n",
			"<host_inherit_struct_no_clone>",
			NoCloneOptions);
	if (!NoCloneCompile.bSuccess || !NoCloneCompile.Module)
	{
		return Fail("no-clone struct suite should compile");
	}
	VeroScriptVm NoCloneVm(std::cout);
		VeroScript::Test::Configure(NoCloneVm);
	if (NoCloneVm.Execute(
				*NoCloneCompile.Module,
				ERunMode::Script,
			{},
					&NoCloneRegistry.GetNativeFunctions()))
	{
		return Fail("struct HostBase without @clone should fail at construct");
	}
}

{
	// Script subclass of a host-inheriting script type must keep HostBase + dispatch.
	FHostBindingRegistry ChainRegistry;
	FHostTypeId ActorId;
	if (!ChainRegistry.RegisterType("AActor", EHostTypeKind::Class, ActorId, ErrorId)
			|| !ChainRegistry.SetTypeInheritable(ActorId, true, ErrorId))
	{
		return Fail("chain registry type setup should succeed");
	}
	FHostMemberDesc GetValue;
	GetValue.Kind = EHostMemberKind::Method;
	GetValue.Name = "get_value";
	GetValue.ReturnType = ENativeType::Int;
	GetValue.Callback = ActorGetValue;
	ActorSubclassCalls = 0;
	if (!ChainRegistry.RegisterMember(ActorId, GetValue, ErrorId)
			|| !ChainRegistry.RegisterTypeSubclassFactory(
				ActorId, ActorSubclassFactory, nullptr, ErrorId)
					|| !ChainRegistry.RegisterNative(
						"expect_eq",
						ENativeType::Void, {ENativeType::Int, ENativeType::Int},
						ExpectEq,
						nullptr,
						ErrorId))
	{
		return Fail("chain registry member setup should succeed");
	}
	FCompileOptions ChainOptions = VeroScript::Test::MakeCompileOptions();
	ChainOptions.Bindings = &ChainRegistry;
	const FCompileResult Compile = VeroScriptCompile::CompileSource(
			"class MyActor : AActor {\n"
			"public:\n"
			"\tMyActor() {}\n"
			"}\n"
			"class Child : MyActor {}\n"
			"void main() {\n"
			"\tChild c = Child();\n"
			"\texpect_eq(c.get_value(), 42);\n"
			"}\n",
			"<host_inherit_chain>",
			ChainOptions);
	if (!Compile.bSuccess || !Compile.Module)
	{
		std::cerr << "host inherit chain compile failed: " << Compile.Error.Message << '\n';
		return Fail("Child : MyActor : AActor should compile");
	}
	bool bChildHasHostBase = false;
	for (const FBytecodeTypeDesc& TypeDesc : Compile.Module->Types)
	{
		if (TypeDesc.Name.find("Child") == std::string::npos)
		{
			continue;
		}
		if (TypeDesc.HostBaseTypeName != "AActor")
		{
			return Fail("Child should inherit HostBase from MyActor");
		}
		bChildHasHostBase = true;
	}
	if (!bChildHasHostBase)
	{
		return Fail("Child type missing");
	}
	VeroScriptVm Vm(std::cout);
		VeroScript::Test::Configure(Vm);
	if (!Vm.Execute(
				*Compile.Module,
				ERunMode::Script,
			{},
					&ChainRegistry.GetNativeFunctions()))
	{
		std::cerr << "host inherit chain run failed: " << Vm.GetError().Message << '\n';
		return Fail("Child : MyActor : AActor should run");
	}
	if (ActorSubclassCalls != 1)
	{
		return Fail("chain construction should call @subclass once");
	}

	const FCompileResult BadCtor = VeroScriptCompile::CompileSource(
			"class MyActor : AActor {\n"
			"public:\n"
			"\tMyActor() : AActor() {}\n"
			"}\n"
			"void main() {}\n",
			"<host_inherit_bad_base_init>",
			Options);
	if (BadCtor.bSuccess)
	{
		return Fail("script ctor calling host base initializer should fail");
	}
}

{
	// Mixing a host-carrying base with another script base must be rejected.
	FHostBindingRegistry MixRegistry;
	FHostTypeId ActorId;
	if (!MixRegistry.RegisterType("AActor", EHostTypeKind::Class, ActorId, ErrorId)
			|| !MixRegistry.SetTypeInheritable(ActorId, true, ErrorId)
			|| !MixRegistry.RegisterTypeSubclassFactory(
				ActorId, ActorSubclassFactory, nullptr, ErrorId))
	{
		return Fail("mix-bases registry setup should succeed");
	}
	FCompileOptions MixOptions = VeroScript::Test::MakeCompileOptions();
	MixOptions.Bindings = &MixRegistry;
	const FCompileResult MixCompile = VeroScriptCompile::CompileSource(
			"class ScriptBase {}\n"
			"class Hosted : AActor {}\n"
			"class Bad : Hosted, ScriptBase {}\n"
			"void main() {}\n",
			"<host_mix_bases>",
			MixOptions);
	if (MixCompile.bSuccess)
	{
		return Fail("mixing host and script bases should fail");
	}
	if (MixCompile.Error.MessageId != "diag.cannot_mix_host_and_script_bases"
			&& MixCompile.Error.Message.find("cannot mix host and script bases")
			== std::string::npos)
	{
		std::cerr << "mix-bases error: " << MixCompile.Error.MessageId
				<< " / " << MixCompile.Error.Message << '\n';
		return Fail("mix-bases reject should report cannot_mix_host_and_script_bases");
	}
}

{
	// Stamped @clone failure must hard-fail container push (not soft-share).
	FHostBindingRegistry FailCloneRegistry;
	FHostTypeId VecId;
	if (!FailCloneRegistry.RegisterType("Vec2", EHostTypeKind::Struct, VecId, ErrorId))
	{
		return Fail("RegisterType Vec2 (fail-clone) should succeed");
	}
	FHostMemberDesc Ctor;
	Ctor.Kind = EHostMemberKind::Constructor;
	Ctor.Name = "Vec2";
	Ctor.ParamTypes = {ENativeType::Int, ENativeType::Int};
	Ctor.Callback = Vec2Ctor;
	FHostMemberDesc GetX;
	GetX.Kind = EHostMemberKind::PropertyGetter;
	GetX.Name = "x";
	GetX.ReturnType = ENativeType::Int;
	GetX.Callback = Vec2GetX;
	if (!FailCloneRegistry.RegisterMember(VecId, Ctor, ErrorId)
			|| !FailCloneRegistry.RegisterMember(VecId, GetX, ErrorId)
			|| !FailCloneRegistry.RegisterTypeClone(VecId, Vec2CloneFail, nullptr, ErrorId))
	{
		std::cerr << ErrorId << '\n';
		return Fail("Vec2 fail-clone registry setup should succeed");
	}
	FCompileOptions FailOptions = VeroScript::Test::MakeCompileOptions();
	FailOptions.Bindings = &FailCloneRegistry;
	const FCompileResult FailCompile = VeroScriptCompile::CompileSource(
			R"(
void main() {
	array<Vec2> a = array<Vec2>();
	Vec2 v = Vec2(1, 2);
	a.push(v);
}
)",
			"<host_struct_clone_fail_push>",
			FailOptions);
	if (!FailCompile.bSuccess || !FailCompile.Module)
	{
		std::cerr << "fail-clone push compile failed: " << FailCompile.Error.Message << '\n';
		return Fail("array push with failing @clone should compile");
	}
	VeroScriptVm FailVm(std::cout);
		VeroScript::Test::Configure(FailVm);
	if (FailVm.Execute(
				*FailCompile.Module,
				ERunMode::Script,
			{},
					&FailCloneRegistry.GetNativeFunctions()))
	{
		return Fail("array.push must hard-fail when stamped @clone fails");
	}
	if (FailVm.GetError().Message.find("failed to clone value") == std::string::npos
			&& FailVm.GetError().Message.find("Vec2 @clone forced failure")
			== std::string::npos)
	{
		std::cerr << "fail-clone push error: " << FailVm.GetError().Message << '\n';
		return Fail("clone-fail push should report clone failure");
	}
}

{
	// map of host structs must deep-copy values on set (not share).
	FHostBindingRegistry MapRegistry;
	FHostTypeId VecId;
	if (!MapRegistry.RegisterType("Vec2", EHostTypeKind::Struct, VecId, ErrorId))
	{
		return Fail("RegisterType Vec2 (map) should succeed");
	}
	FHostMemberDesc Ctor;
	Ctor.Kind = EHostMemberKind::Constructor;
	Ctor.Name = "Vec2";
	Ctor.ParamTypes = {ENativeType::Int, ENativeType::Int};
	Ctor.Callback = Vec2Ctor;
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
	if (!MapRegistry.RegisterMember(VecId, Ctor, ErrorId)
			|| !MapRegistry.RegisterMember(VecId, GetX, ErrorId)
			|| !MapRegistry.RegisterMember(VecId, SetX, ErrorId)
			|| !MapRegistry.RegisterTypeClone(VecId, Vec2Clone, nullptr, ErrorId)
			|| !MapRegistry.RegisterNative(
				"expect_eq",
				ENativeType::Void, {ENativeType::Int, ENativeType::Int},
				ExpectEq,
				nullptr,
				ErrorId))
	{
		std::cerr << ErrorId << '\n';
		return Fail("Vec2 map registry setup should succeed");
	}
	FCompileOptions MapOptions = VeroScript::Test::MakeCompileOptions();
	MapOptions.Bindings = &MapRegistry;
	const FCompileResult MapCompile = VeroScriptCompile::CompileSource(
			R"(
void main() {
	map<string, Vec2> m = map<string, Vec2>();
	Vec2 v = Vec2(1, 2);
	m["k"] = v;
	v.x = 9;
	expect_eq(v.x, 9);
	expect_eq(m["k"].x, 1);
}
)",
			"<host_struct_map>",
			MapOptions);
	if (!MapCompile.bSuccess || !MapCompile.Module)
	{
		std::cerr << "host struct map compile failed: " << MapCompile.Error.Message << '\n';
		return Fail("map<string, Vec2> set should compile");
	}
	VeroScriptVm MapVm(std::cout);
		VeroScript::Test::Configure(MapVm);
	if (!MapVm.Execute(
				*MapCompile.Module,
				ERunMode::Script,
			{},
					&MapRegistry.GetNativeFunctions()))
	{
		std::cerr << "host struct map run failed: " << MapVm.GetError().Message << '\n';
		return Fail("map<string, Vec2> set should deep-copy");
	}
}

{
	// Instance methods of a host subclass may name inherited host members without a
	// receiver, and `Super` reaches the script base / host base explicitly.
	FHostBindingRegistry ImplicitRegistry;
	FHostTypeId ActorId;
	FHostMemberDesc GetName;
	GetName.Kind = EHostMemberKind::Method;
	GetName.Name = "GetName";
	GetName.ReturnType = ENativeType::String;
	GetName.Callback = ActorGetName;
	FHostMemberDesc GetTag;
	GetTag.Kind = EHostMemberKind::PropertyGetter;
	GetTag.Name = "Tag";
	GetTag.ReturnType = ENativeType::Int;
	GetTag.Callback = ActorGetTag;
	FHostMemberDesc SetTag;
	SetTag.Kind = EHostMemberKind::PropertySetter;
	SetTag.Name = "Tag";
	SetTag.ParamTypes = {ENativeType::Int};
	SetTag.Callback = ActorSetTag;
	if (!ImplicitRegistry.RegisterType("AActor", EHostTypeKind::Class, ActorId, ErrorId)
			|| !ImplicitRegistry.SetTypeInheritable(ActorId, true, ErrorId)
			|| !ImplicitRegistry.RegisterTypeSubclassFactory(
				ActorId, ActorSubclassFactory, nullptr, ErrorId)
					|| !ImplicitRegistry.RegisterMember(ActorId, GetName, ErrorId)
					|| !ImplicitRegistry.RegisterMember(ActorId, GetTag, ErrorId)
					|| !ImplicitRegistry.RegisterMember(ActorId, SetTag, ErrorId)
					|| !ImplicitRegistry.RegisterNative(
						"expect_eq",
						ENativeType::Void, {ENativeType::Int, ENativeType::Int},
						ExpectEq,
						nullptr,
						ErrorId)
							|| !ImplicitRegistry.RegisterNative(
								"expect_str",
								ENativeType::Void, {ENativeType::String, ENativeType::String},
								ExpectStr,
								nullptr,
								ErrorId))
	{
		std::cerr << ErrorId << '\n';
		return Fail("implicit host member registry setup should succeed");
	}
	FCompileOptions ImplicitOptions = VeroScript::Test::MakeCompileOptions();
	ImplicitOptions.Bindings = &ImplicitRegistry;

	const auto RunSource =
			[&](const char* Source, const char* Label, const char* What) -> int
			{
				const FCompileResult Compile =
							VeroScriptCompile::CompileSource(Source, Label, ImplicitOptions);
				if (!Compile.bSuccess || !Compile.Module)
				{
					std::cerr << Label << " compile failed: " << Compile.Error.Message << '\n';
					return Fail(What);
				}
				VeroScriptVm Vm(std::cout);
		VeroScript::Test::Configure(Vm);
				if (!Vm.Execute(
								*Compile.Module,
								ERunMode::Script,
						{},
									&ImplicitRegistry.GetNativeFunctions()))
				{
					std::cerr << Label << " run failed: " << Vm.GetError().Message << '\n';
					return Fail(What);
				}
				return EXIT_SUCCESS;
			};

	if (RunSource(
				"class My : AActor {\n"
				"public:\n"
				"\tstring Run() { return GetName(); }\n"
				"\tint ReadTag() { return Tag; }\n"
				"}\n"
				"void main() {\n"
				"\tMy m = My();\n"
				"\texpect_str(m.Run(), \"host\");\n"
				"\texpect_eq(m.ReadTag(), 7);\n"
				"}\n",
				"<host_implicit_member>",
				"bare host method/property names should bind to the host base")
					!= EXIT_SUCCESS)
	{
		return EXIT_FAILURE;
	}

	if (RunSource(
				"class Shadow : AActor {\n"
				"public:\n"
				"\tint Field = 99;\n"
				"\tint ReadLocal() { int Tag = 5; return Tag; }\n"
				"\tint ReadField() { return Field; }\n"
				"}\n"
				"void main() {\n"
				"\tShadow s = Shadow();\n"
				"\texpect_eq(s.ReadLocal(), 5);\n"
				"\texpect_eq(s.ReadField(), 99);\n"
				"}\n",
				"<host_implicit_shadow>",
				"script locals/fields/methods should win over host base members")
					!= EXIT_SUCCESS)
	{
		return EXIT_FAILURE;
	}

	{
		// The language rejects a script field colliding with a host member outright,
		// so the implicit lookup can never be reached through one.
		const FCompileResult Compile = VeroScriptCompile::CompileSource(
				"class Collide : AActor {\n"
				"public:\n"
				"\tint Tag = 99;\n"
				"}\n"
				"void main() {}\n",
				"<host_implicit_field_collision>",
				ImplicitOptions);
		if (Compile.bSuccess)
		{
			return Fail("script field colliding with a host member should not compile");
		}
	}

	if (RunSource(
				"class Base {\n"
				"public:\n"
				"\tvirtual int Value() { return 1; }\n"
				"}\n"
				"class Derived : Base {\n"
				"public:\n"
				"\tint Value() override { return Super.Value() + 10; }\n"
				"\tint Qualified() { return Super::Value() + 100; }\n"
				"}\n"
				"void main() {\n"
				"\tDerived d = Derived();\n"
				"\texpect_eq(d.Value(), 11);\n"
				"\texpect_eq(d.Qualified(), 101);\n"
				"}\n",
				"<super_script_base>",
				"Super.Method() should call the overridden script base method")
					!= EXIT_SUCCESS)
	{
		return EXIT_FAILURE;
	}

	if (RunSource(
				"class HostWrite : AActor {\n"
				"public:\n"
				"\tint WriteImplicit(int V) { Tag = V; return Tag; }\n"
				"\tint WriteSuper(int V) { Super.Tag = V; return Super.Tag; }\n"
				"}\n"
				"void main() {\n"
				"\tHostWrite h = HostWrite();\n"
				"\texpect_eq(h.WriteImplicit(11), 11);\n"
				"\texpect_eq(h.WriteSuper(22), 22);\n"
				"}\n",
				"<host_implicit_super_assign>",
				"implicit / Super host property assign should call the setter")
					!= EXIT_SUCCESS)
	{
		return EXIT_FAILURE;
	}

	if (RunSource(
				"class HostCompound : AActor {\n"
				"public:\n"
				"\tint AddImplicit(int V) { Tag = 10; Tag += V; return Tag; }\n"
				"\tint AddSuper(int V) { Super.Tag = 10; Super.Tag += V; return Super.Tag; }\n"
				"\tint PostIncImplicit() { Tag = 4; return Tag++; }\n"
				"\tint PreIncSuper() { Super.Tag = 4; return ++Super.Tag; }\n"
				"\tint AfterPostInc() { Tag = 4; Tag++; return Tag; }\n"
				"}\n"
				"void main() {\n"
				"\tHostCompound h = HostCompound();\n"
				"\texpect_eq(h.AddImplicit(5), 15);\n"
				"\texpect_eq(h.AddSuper(7), 17);\n"
				"\texpect_eq(h.PostIncImplicit(), 4);\n"
				"\texpect_eq(h.PreIncSuper(), 5);\n"
				"\texpect_eq(h.AfterPostInc(), 5);\n"
				"}\n",
				"<host_implicit_super_compound_assign>",
				"implicit / Super host property compound assign should round-trip the setter")
					!= EXIT_SUCCESS)
	{
		return EXIT_FAILURE;
	}

	if (RunSource(
				"class CompoundBase {\n"
				"public:\n"
				"\tint Count = 1;\n"
				"}\n"
				"class CompoundDerived : CompoundBase {\n"
				"public:\n"
				"\tint Bump(int V) { Super.Count += V; return Super.Count; }\n"
				"\tint BumpPost() { return Super.Count++; }\n"
				"\tint Read() { return Super.Count; }\n"
				"}\n"
				"void main() {\n"
				"\tCompoundDerived d = CompoundDerived();\n"
				"\texpect_eq(d.Bump(4), 5);\n"
				"\texpect_eq(d.Read(), 5);\n"
				"\tCompoundDerived p = CompoundDerived();\n"
				"\texpect_eq(p.BumpPost(), 1);\n"
				"\texpect_eq(p.Read(), 2);\n"
				"}\n",
				"<super_script_field_compound_assign>",
				"Super.Field compound assign should update the flattened base field")
					!= EXIT_SUCCESS)
	{
		return EXIT_FAILURE;
	}

	{
		// A const method may not write the host base property, directly or via Super.
		const FCompileResult ConstWrite = VeroScriptCompile::CompileSource(
				"class HostConst : AActor {\n"
				"public:\n"
				"\tint Bad() const { Tag += 1; return Tag; }\n"
				"}\n"
				"void main() {}\n",
				"<host_implicit_const_compound>",
				ImplicitOptions);
		if (ConstWrite.bSuccess)
		{
			return Fail("const method should not compound-assign a host base property");
		}
		const FCompileResult ConstSuperWrite = VeroScriptCompile::CompileSource(
				"class HostConstSuper : AActor {\n"
				"public:\n"
				"\tint Bad() const { Super.Tag += 1; return Super.Tag; }\n"
				"}\n"
				"void main() {}\n",
				"<super_const_compound>",
				ImplicitOptions);
		if (ConstSuperWrite.bSuccess)
		{
			return Fail("const method should not compound-assign Super host property");
		}
	}

	if (RunSource(
				"class HostSuper : AActor {\n"
				"public:\n"
				"\tstring Describe() { return Super.GetName(); }\n"
				"\tstring Qualified() { return Super::GetName(); }\n"
				"\tint SuperTag() { return Super.Tag; }\n"
				"}\n"
				"void main() {\n"
				"\tHostSuper h = HostSuper();\n"
				"\texpect_str(h.Describe(), \"host\");\n"
				"\texpect_str(h.Qualified(), \"host\");\n"
				"\texpect_eq(h.SuperTag(), 7);\n"
				"}\n",
				"<super_host_base>",
				"Super.HostMethod() should call the host base native")
					!= EXIT_SUCCESS)
	{
		return EXIT_FAILURE;
	}

	{
		// Free functions still win over a same-named host method.
		const FCompileResult Compile = VeroScriptCompile::CompileSource(
				"string GetName() { return \"script\"; }\n"
				"class Free : AActor {\n"
				"public:\n"
				"\tstring Run() { return GetName(); }\n"
				"}\n"
				"void main() {\n"
				"\tFree f = Free();\n"
				"\texpect_str(f.Run(), \"script\");\n"
				"}\n",
				"<host_implicit_free_function>",
				ImplicitOptions);
		if (!Compile.bSuccess || !Compile.Module)
		{
			std::cerr << "free-function priority compile failed: "
					<< Compile.Error.Message << '\n';
			return Fail("free function should shadow the host base method");
		}
		VeroScriptVm Vm(std::cout);
		VeroScript::Test::Configure(Vm);
		if (!Vm.Execute(
					*Compile.Module,
					ERunMode::Script,
				{},
						&ImplicitRegistry.GetNativeFunctions()))
		{
			std::cerr << "free-function priority run failed: " << Vm.GetError().Message << '\n';
			return Fail("free function should shadow the host base method");
		}
	}

	{
		// Outside a host subclass the bare name is still undeclared.
		const FCompileResult Compile = VeroScriptCompile::CompileSource(
				"void main() { string s = GetName(); }\n",
				"<host_implicit_outside_method>",
				ImplicitOptions);
		if (Compile.bSuccess)
		{
			return Fail("bare host method name outside a host subclass should not compile");
		}
	}
}
