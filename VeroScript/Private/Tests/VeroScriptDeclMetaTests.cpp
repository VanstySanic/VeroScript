// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptAst.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptLexer.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptParser.h"

#include "VeroScriptTestTemporaryDirectory.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace
{
	int Fail(const char* Message)
	{
		std::cerr << "VeroScriptDeclMetaTests: " << Message << '\n';
		return EXIT_FAILURE;
	}

	bool Expect(const bool Condition, const char* Message)
	{
		return Condition ? true : (Fail(Message), false);
	}

	bool HasMetaPair(
		const VeroScript::FDeclMeta& Meta,
		const std::string& Key,
		const std::string& Value)
	{
		for (const auto& Pair : Meta)
		{
			if (Pair.first == Key && Pair.second == Value)
			{
				return true;
			}
		}
		return false;
	}

	std::unique_ptr<VeroScript::FBytecodeModule> CompileSource(const std::string& Source)
	{
		VeroScript::VeroScriptLexer Lexer(Source, "<test>");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			std::cerr << "VeroScriptDeclMetaTests: lex error: " << Lexer.GetErrorMessage() << '\n';
			return nullptr;
		}

		VeroScript::VeroScriptParser Parser(Tokens, "<test>");
		std::unique_ptr<VeroScript::FProgram> Program = Parser.ParseProgram();
		if (Parser.HasError() || !Program)
		{
			std::cerr << "VeroScriptDeclMetaTests: parse error: " << Parser.GetError().Message << '\n';
			return nullptr;
		}

		VeroScript::VeroScriptCompiler Compiler;
		std::unique_ptr<VeroScript::FBytecodeModule> Module = Compiler.Compile(*Program);
		if (Compiler.HasError() || !Module)
		{
			std::cerr << "VeroScriptDeclMetaTests: compile error: " << Compiler.GetError().Message << '\n';
			return nullptr;
		}
		return Module;
	}
}

int main()
{
	using namespace VeroScript;
	const Tests::FScopedTemporaryDirectory TemporaryDirectory(
		"VeroScript-decl-meta-");
	if (!Expect(static_cast<bool>(TemporaryDirectory),
		"create unique declaration metadata test directory"))
	{
		return EXIT_FAILURE;
	}
	const std::filesystem::path& TemporaryRoot = TemporaryDirectory.Get();

	{
		const std::string Source =
			"UCLASS()\n"
			"class CombatActor {\n"
			"public:\n"
			"    UPROPERTY(EditAnywhere, Category=\"Combat\", meta=(ClampMin=\"0\"))\n"
			"    int Amount = 0;\n"
			"    @rpc(reliable)\n"
			"    void ApplyDamage(int Delta) { Amount = Amount + Delta; }\n"
			"}\n"
			"UENUM(BlueprintType)\n"
			"enum EDamageType { Physical, Fire = 2 }\n"
			"USTRUCT(BlueprintType)\n"
			"struct FHitInfo { int Power; }\n"
			"void main() {\n"
			"    print(1);\n"
			"}\n";

		std::unique_ptr<FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "annotated source should compile"))
		{
			return EXIT_FAILURE;
		}

		std::vector<FLoadedTypeInfo> Types;
		ModuleMeta::EnumerateTypes(*Module, Types);
		const FLoadedTypeInfo* Combat = nullptr;
		const FLoadedTypeInfo* HitInfo = nullptr;
		for (const FLoadedTypeInfo& Type : Types)
		{
			if (Type.SimpleName == "CombatActor")
			{
				Combat = &Type;
			}
			if (Type.SimpleName == "FHitInfo")
			{
				HitInfo = &Type;
			}
		}
		if (!Expect(Combat != nullptr, "CombatActor type present"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				HasMetaPair(Combat->DeclMeta, "__annot", "UCLASS"),
				"CombatActor DeclMeta has UCLASS"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(HitInfo != nullptr, "FHitInfo type present"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				HasMetaPair(HitInfo->DeclMeta, "__annot", "USTRUCT")
					&& HasMetaPair(HitInfo->DeclMeta, "BlueprintType", ""),
				"FHitInfo DeclMeta has USTRUCT(BlueprintType)"))
		{
			return EXIT_FAILURE;
		}

		std::vector<FLoadedFieldInfo> Fields;
		if (!Expect(
				ModuleMeta::EnumerateFields(*Module, "CombatActor", Fields, true),
				"EnumerateFields CombatActor"))
		{
			return EXIT_FAILURE;
		}
		const FLoadedFieldInfo* Amount = nullptr;
		for (const FLoadedFieldInfo& Field : Fields)
		{
			if (Field.Name == "Amount")
			{
				Amount = &Field;
			}
		}
		if (!Expect(Amount != nullptr, "Amount field present"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				HasMetaPair(Amount->DeclMeta, "__annot", "UPROPERTY")
					&& HasMetaPair(Amount->DeclMeta, "EditAnywhere", "")
					&& HasMetaPair(Amount->DeclMeta, "Category", "Combat")
					&& HasMetaPair(Amount->DeclMeta, "meta.ClampMin", "0"),
				"Amount DeclMeta flattened correctly"))
		{
			return EXIT_FAILURE;
		}

		std::vector<FInstanceMethodInfo> Methods;
		if (!Expect(
				ModuleMeta::EnumerateInstanceMethods(*Module, "CombatActor", Methods),
				"EnumerateInstanceMethods CombatActor"))
		{
			return EXIT_FAILURE;
		}
		const FInstanceMethodInfo* Apply = nullptr;
		for (const FInstanceMethodInfo& Method : Methods)
		{
			if (Method.Name == "ApplyDamage")
			{
				Apply = &Method;
			}
		}
		if (!Expect(Apply != nullptr, "ApplyDamage method present"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				HasMetaPair(Apply->DeclMeta, "__annot", "@rpc")
					&& HasMetaPair(Apply->DeclMeta, "reliable", ""),
				"ApplyDamage DeclMeta has @rpc(reliable)"))
		{
			return EXIT_FAILURE;
		}

		std::vector<FLoadedEnumInfo> Enums;
		ModuleMeta::EnumerateEnums(*Module, Enums);
		const FLoadedEnumInfo* Damage = nullptr;
		for (const FLoadedEnumInfo& EnumInfo : Enums)
		{
			if (EnumInfo.SimpleName == "EDamageType")
			{
				Damage = &EnumInfo;
			}
		}
		if (!Expect(Damage != nullptr, "EDamageType enum present"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				HasMetaPair(Damage->DeclMeta, "__annot", "UENUM")
					&& HasMetaPair(Damage->DeclMeta, "BlueprintType", "")
					&& Damage->Members.size() == 2
					&& Damage->Members[0].Name == "Physical"
					&& Damage->Members[0].Value == 0
					&& Damage->Members[1].Name == "Fire"
					&& Damage->Members[1].Value == 2,
				"EDamageType DeclMeta + members"))
		{
			return EXIT_FAILURE;
		}

		const std::filesystem::path TempPath =
			TemporaryRoot / "veroscript_declmeta_roundtrip.vsbc";
		if (!Expect(
				VeroScriptBytecodeIO::WriteFile(TempPath.string(), *Module, 0),
				"write bytecode with DeclMeta"))
		{
			return EXIT_FAILURE;
		}
		std::string ReadError;
		std::unique_ptr<FBytecodeModule> Reloaded =
			VeroScriptBytecodeIO::ReadFile(TempPath.string(), 0, ReadError);
		std::error_code RemoveError;
		std::filesystem::remove(TempPath, RemoveError);
		if (!Expect(static_cast<bool>(Reloaded), "read bytecode with DeclMeta"))
		{
			std::cerr << ReadError << '\n';
			return EXIT_FAILURE;
		}
		std::vector<FLoadedFieldInfo> ReloadedFields;
		if (!Expect(
				ModuleMeta::EnumerateFields(*Reloaded, "CombatActor", ReloadedFields, true)
					&& !ReloadedFields.empty()
					&& HasMetaPair(ReloadedFields.front().DeclMeta, "meta.ClampMin", "0"),
				"DeclMeta survives BC roundtrip"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::string Source =
			"class Plain {\n"
			"public:\n"
			"    int Value;\n"
			"    void Tick() {}\n"
			"}\n"
			"enum EPlain { A }\n"
			"void main() { print(1); }\n";
		std::unique_ptr<FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "plain source should compile"))
		{
			return EXIT_FAILURE;
		}
		std::vector<FLoadedTypeInfo> Types;
		ModuleMeta::EnumerateTypes(*Module, Types);
		for (const FLoadedTypeInfo& Type : Types)
		{
			if (Type.SimpleName == "Plain"
				&& !Expect(Type.DeclMeta.empty(), "no DeclMeta on plain type"))
			{
				return EXIT_FAILURE;
			}
		}
		std::vector<FLoadedFieldInfo> Fields;
		ModuleMeta::EnumerateFields(*Module, "Plain", Fields, true);
		for (const FLoadedFieldInfo& Field : Fields)
		{
			if (!Expect(Field.DeclMeta.empty(), "no DeclMeta on plain field"))
			{
				return EXIT_FAILURE;
			}
		}
		std::vector<FLoadedEnumInfo> Enums;
		ModuleMeta::EnumerateEnums(*Module, Enums);
		for (const FLoadedEnumInfo& EnumInfo : Enums)
		{
			if (EnumInfo.SimpleName == "EPlain"
				&& !Expect(EnumInfo.DeclMeta.empty(), "no DeclMeta on plain enum"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Expression Ident( must not be treated as a declaration annotation.
		const std::string Source =
			"void main() {\n"
			"    print(1);\n"
			"}\n";
		std::unique_ptr<FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "print(1) in main should still compile"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::string Source =
			"print(1);\n"
			"void main() {}\n";
		std::unique_ptr<FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "top-level print(1) must not be eaten as annotation"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::string Source =
			"UCLASS()\n"
			"export class ExportedActor {\n"
			"public:\n"
			"    int X;\n"
			"}\n"
			"export USTRUCT(BlueprintType)\n"
			"struct ExportedHit { int Y; }\n"
			"@rpc(reliable)\n"
			"void Fire(int Amount) {}\n"
			"void main() { print(1); }\n";
		std::unique_ptr<FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "annot before/after export + free fn should compile"))
		{
			return EXIT_FAILURE;
		}
		std::vector<FLoadedTypeInfo> Types;
		ModuleMeta::EnumerateTypes(*Module, Types);
		bool bFoundActor = false;
		bool bFoundHit = false;
		for (const FLoadedTypeInfo& Type : Types)
		{
			if (Type.SimpleName == "ExportedActor")
			{
				bFoundActor = true;
				if (!Expect(
						HasMetaPair(Type.DeclMeta, "__annot", "UCLASS"),
						"UCLASS() before export class"))
				{
					return EXIT_FAILURE;
				}
			}
			if (Type.SimpleName == "ExportedHit")
			{
				bFoundHit = true;
				if (!Expect(
						HasMetaPair(Type.DeclMeta, "__annot", "USTRUCT")
							&& HasMetaPair(Type.DeclMeta, "BlueprintType", ""),
						"export USTRUCT(...) struct"))
				{
					return EXIT_FAILURE;
				}
			}
		}
		if (!Expect(bFoundActor && bFoundHit, "exported annotated types present"))
		{
			return EXIT_FAILURE;
		}
		std::vector<FFreeFunctionInfo> Fns;
		ModuleMeta::EnumerateFreeFunctions(*Module, Fns);
		const FFreeFunctionInfo* Fire = nullptr;
		for (const FFreeFunctionInfo& Fn : Fns)
		{
			if (Fn.Name == "Fire")
			{
				Fire = &Fn;
			}
		}
		if (!Expect(Fire != nullptr, "Fire free function present"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				HasMetaPair(Fire->DeclMeta, "__annot", "@rpc")
					&& HasMetaPair(Fire->DeclMeta, "reliable", ""),
				"free function DeclMeta"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::string Source =
			"class Base {\n"
			"public:\n"
			"    UPROPERTY(EditAnywhere)\n"
			"    int Health;\n"
			"}\n"
			"class Derived : public Base {\n"
			"public:\n"
			"    int Armor;\n"
			"}\n"
			"void main() { print(1); }\n";
		std::unique_ptr<FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "inherited field DeclMeta source should compile"))
		{
			return EXIT_FAILURE;
		}
		std::vector<FLoadedFieldInfo> Fields;
		if (!Expect(
				ModuleMeta::EnumerateFields(*Module, "Derived", Fields, false),
				"EnumerateFields Derived inherited"))
		{
			return EXIT_FAILURE;
		}
		const FLoadedFieldInfo* Health = nullptr;
		for (const FLoadedFieldInfo& Field : Fields)
		{
			if (Field.Name == "Health")
			{
				Health = &Field;
			}
		}
		if (!Expect(
				Health != nullptr
					&& HasMetaPair(Health->DeclMeta, "__annot", "UPROPERTY")
					&& HasMetaPair(Health->DeclMeta, "EditAnywhere", ""),
				"inherited field keeps DeclMeta"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Generic free-function instantiate must keep DeclMeta on the mangled body.
		const std::string Source =
			"@rpc(reliable)\n"
			"T Identity<T>(T Value) { return Value; }\n"
			"void main() {\n"
			"    print(Identity<int>(1));\n"
			"}\n";
		std::unique_ptr<FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "generic free fn with DeclMeta should compile"))
		{
			return EXIT_FAILURE;
		}
		bool bFoundInstantiated = false;
		for (const auto& Entry : Module->FunctionIndices)
		{
			if (Entry.second >= Module->Functions.size())
			{
				continue;
			}
			const FBytecodeFunction& Fn = Module->Functions[Entry.second];
			if (Fn.bIsMethod || Fn.Code.empty())
			{
				continue;
			}
			if (Entry.first.find("Identity") == std::string::npos)
			{
				continue;
			}
			if (HasMetaPair(Fn.DeclMeta, "__annot", "@rpc")
				&& HasMetaPair(Fn.DeclMeta, "reliable", ""))
			{
				bFoundInstantiated = true;
				break;
			}
		}
		if (!Expect(bFoundInstantiated, "generic free-fn instantiate keeps DeclMeta"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// ctor / dtor / static field / static method / abstract interface DeclMeta.
		const std::string Source =
			"UINTERFACE()\n"
			"interface IDamageable {\n"
			"    UPROPERTY(EditAnywhere)\n"
			"    int InterfaceHp;\n"
			"    UPROPERTY(EditAnywhere)\n"
			"    static int SharedCap = 10;\n"
			"    @rpc(reliable)\n"
			"    virtual void Apply(int Delta) = 0;\n"
			"}\n"
			"UCLASS()\n"
			"class CombatActor : public IDamageable {\n"
			"public:\n"
			"    UPROPERTY(EditAnywhere)\n"
			"    static int SharedSeed = 1;\n"
			"    UFUNCTION(BlueprintCallable)\n"
			"    static int NextId() { return 1; }\n"
			"    UFUNCTION(BlueprintCallable)\n"
			"    CombatActor() {}\n"
			"    @cleanup()\n"
			"    ~CombatActor() {}\n"
			"    void Apply(int Delta) {}\n"
			"}\n"
			"void main() { print(1); }\n";
		std::unique_ptr<FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "ctor/dtor/static/iface DeclMeta source should compile"))
		{
			return EXIT_FAILURE;
		}

		std::vector<FLoadedInterfaceInfo> Interfaces;
		ModuleMeta::EnumerateInterfaces(*Module, Interfaces);
		const FLoadedInterfaceInfo* Damageable = nullptr;
		for (const FLoadedInterfaceInfo& Iface : Interfaces)
		{
			if (Iface.SimpleName == "IDamageable")
			{
				Damageable = &Iface;
			}
		}
		if (!Expect(
				Damageable != nullptr
					&& HasMetaPair(Damageable->DeclMeta, "__annot", "UINTERFACE")
					&& Damageable->Fields.size() == 2,
				"EnumerateInterfaces exposes UINTERFACE + fields"))
		{
			return EXIT_FAILURE;
		}
		const FLoadedFieldInfo* InterfaceHp = nullptr;
		const FLoadedFieldInfo* SharedCap = nullptr;
		for (const FLoadedFieldInfo& Field : Damageable->Fields)
		{
			if (Field.Name == "InterfaceHp")
			{
				InterfaceHp = &Field;
			}
			if (Field.Name == "SharedCap")
			{
				SharedCap = &Field;
			}
		}
		if (!Expect(
				InterfaceHp != nullptr
					&& !InterfaceHp->bIsStatic
					&& HasMetaPair(InterfaceHp->DeclMeta, "__annot", "UPROPERTY"),
				"interface instance field DeclMeta"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				SharedCap != nullptr
					&& SharedCap->bIsStatic
					&& SharedCap->FieldIndex == static_cast<std::uint16_t>(-1)
					&& HasMetaPair(SharedCap->DeclMeta, "__annot", "UPROPERTY")
					&& HasMetaPair(SharedCap->DeclMeta, "EditAnywhere", ""),
				"interface static field DeclMeta + bIsStatic"))
		{
			return EXIT_FAILURE;
		}

		std::vector<FInstanceMethodInfo> IfaceMethods;
		if (!Expect(
				ModuleMeta::EnumerateInstanceMethods(*Module, "IDamageable", IfaceMethods),
				"EnumerateInstanceMethods on interface name"))
		{
			return EXIT_FAILURE;
		}
		const FInstanceMethodInfo* AbstractApply = nullptr;
		for (const FInstanceMethodInfo& Method : IfaceMethods)
		{
			if (Method.Name == "Apply")
			{
				AbstractApply = &Method;
			}
		}
		if (!Expect(
				AbstractApply != nullptr
					&& HasMetaPair(AbstractApply->DeclMeta, "__annot", "@rpc")
					&& HasMetaPair(AbstractApply->DeclMeta, "reliable", ""),
				"abstract iface method DeclMeta via EnumerateInstanceMethods"))
		{
			return EXIT_FAILURE;
		}

		std::vector<FLoadedFieldInfo> Fields;
		if (!Expect(
				ModuleMeta::EnumerateFields(*Module, "CombatActor", Fields, true),
				"EnumerateFields CombatActor static"))
		{
			return EXIT_FAILURE;
		}
		const FLoadedFieldInfo* Seed = nullptr;
		for (const FLoadedFieldInfo& Field : Fields)
		{
			if (Field.Name == "SharedSeed")
			{
				Seed = &Field;
			}
		}
		if (!Expect(
				Seed != nullptr
					&& Seed->bIsStatic
					&& Seed->FieldIndex == static_cast<std::uint16_t>(-1)
					&& HasMetaPair(Seed->DeclMeta, "__annot", "UPROPERTY")
					&& HasMetaPair(Seed->DeclMeta, "EditAnywhere", ""),
				"static field DeclMeta + bIsStatic"))
		{
			return EXIT_FAILURE;
		}

		std::vector<FStaticMethodInfo> StaticMethods;
		if (!Expect(
				ModuleMeta::EnumerateStaticMethods(*Module, "CombatActor", StaticMethods),
				"EnumerateStaticMethods CombatActor"))
		{
			return EXIT_FAILURE;
		}
		const FStaticMethodInfo* NextId = nullptr;
		for (const FStaticMethodInfo& Method : StaticMethods)
		{
			if (Method.Name == "NextId")
			{
				NextId = &Method;
			}
		}
		if (!Expect(
				NextId != nullptr
					&& HasMetaPair(NextId->DeclMeta, "__annot", "UFUNCTION")
					&& HasMetaPair(NextId->DeclMeta, "BlueprintCallable", ""),
				"static method DeclMeta"))
		{
			return EXIT_FAILURE;
		}

		std::vector<FInstanceMethodInfo> Methods;
		if (!Expect(
				ModuleMeta::EnumerateInstanceMethods(
					*Module, "CombatActor", Methods, true, true, true),
				"EnumerateInstanceMethods with ctor/dtor"))
		{
			return EXIT_FAILURE;
		}
		const FInstanceMethodInfo* Ctor = nullptr;
		const FInstanceMethodInfo* Dtor = nullptr;
		for (const FInstanceMethodInfo& Method : Methods)
		{
			if (Method.bIsConstructor)
			{
				Ctor = &Method;
			}
			if (Method.bIsDestructor)
			{
				Dtor = &Method;
			}
		}
		if (!Expect(
				Ctor != nullptr
					&& HasMetaPair(Ctor->DeclMeta, "__annot", "UFUNCTION")
					&& HasMetaPair(Ctor->DeclMeta, "BlueprintCallable", ""),
				"constructor DeclMeta"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Dtor != nullptr && HasMetaPair(Dtor->DeclMeta, "__annot", "@cleanup"),
				"destructor DeclMeta"))
		{
			return EXIT_FAILURE;
		}

		const FInstanceMethodInfo* ApplyOnActor = nullptr;
		for (const FInstanceMethodInfo& Method : Methods)
		{
			if (Method.Name == "Apply" && !Method.bIsConstructor && !Method.bIsDestructor)
			{
				ApplyOnActor = &Method;
			}
		}
		if (!Expect(
				ApplyOnActor != nullptr
					&& HasMetaPair(ApplyOnActor->DeclMeta, "__annot", "@rpc")
					&& HasMetaPair(ApplyOnActor->DeclMeta, "reliable", ""),
				"implementor inherits interface method DeclMeta"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Nested groups, trailing commas, string escapes + BC roundtrip.
		const std::string Source =
			"UCLASS(meta=(DisplayName=\"Hi\\nThere\", Nested=(A=1, B=\"x\\\"y\",),),)\n"
			"class Fancy {\n"
			"public:\n"
			"    int X;\n"
			"}\n"
			"void main() { print(1); }\n";
		std::unique_ptr<FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "nested/trailing/escape annot should compile"))
		{
			return EXIT_FAILURE;
		}
		std::vector<FLoadedTypeInfo> Types;
		ModuleMeta::EnumerateTypes(*Module, Types);
		const FLoadedTypeInfo* Fancy = nullptr;
		for (const FLoadedTypeInfo& Type : Types)
		{
			if (Type.SimpleName == "Fancy")
			{
				Fancy = &Type;
			}
		}
		if (!Expect(Fancy != nullptr, "Fancy type present"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				HasMetaPair(Fancy->DeclMeta, "__annot", "UCLASS")
					&& HasMetaPair(Fancy->DeclMeta, "meta", "__annot")
					&& HasMetaPair(Fancy->DeclMeta, "meta.DisplayName", "Hi\nThere")
					&& HasMetaPair(Fancy->DeclMeta, "meta.Nested", "__annot")
					&& HasMetaPair(Fancy->DeclMeta, "meta.Nested.A", "1")
					&& HasMetaPair(Fancy->DeclMeta, "meta.Nested.B", "x\"y"),
				"nested / trailing-comma / escape DeclMeta flatten"))
		{
			return EXIT_FAILURE;
		}

		const std::filesystem::path NestedPath =
			TemporaryRoot / "veroscript_declmeta_nested.vsbc";
		if (!Expect(
				VeroScriptBytecodeIO::WriteFile(NestedPath.string(), *Module, 0),
				"write nested DeclMeta bytecode"))
		{
			return EXIT_FAILURE;
		}
		std::string NestedReadError;
		std::unique_ptr<FBytecodeModule> NestedReloaded =
			VeroScriptBytecodeIO::ReadFile(NestedPath.string(), 0, NestedReadError);
		std::error_code NestedRemoveError;
		std::filesystem::remove(NestedPath, NestedRemoveError);
		if (!Expect(static_cast<bool>(NestedReloaded), "read nested DeclMeta bytecode"))
		{
			std::cerr << NestedReadError << '\n';
			return EXIT_FAILURE;
		}
		std::vector<FLoadedTypeInfo> ReloadedTypes;
		ModuleMeta::EnumerateTypes(*NestedReloaded, ReloadedTypes);
		const FLoadedTypeInfo* ReloadedFancy = nullptr;
		for (const FLoadedTypeInfo& Type : ReloadedTypes)
		{
			if (Type.SimpleName == "Fancy")
			{
				ReloadedFancy = &Type;
			}
		}
		if (!Expect(
				ReloadedFancy != nullptr
					&& HasMetaPair(ReloadedFancy->DeclMeta, "meta", "__annot")
					&& HasMetaPair(ReloadedFancy->DeclMeta, "meta.Nested", "__annot")
					&& HasMetaPair(ReloadedFancy->DeclMeta, "meta.DisplayName", "Hi\nThere")
					&& HasMetaPair(ReloadedFancy->DeclMeta, "meta.Nested.B", "x\"y"),
				"nested DeclMeta survives BC roundtrip"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Generic class instantiate must keep type / field / method DeclMeta.
		const std::string Source =
			"template<T>\n"
			"UCLASS(BlueprintType)\n"
			"class Box {\n"
			"public:\n"
			"    UPROPERTY(EditAnywhere)\n"
			"    T Value;\n"
			"    @rpc(reliable)\n"
			"    void Set(T Next) { Value = Next; }\n"
			"}\n"
			"void main() {\n"
			"    Box<int> B = Box<int>();\n"
			"    B.Set(1);\n"
			"    print(B.Value);\n"
			"}\n";
		std::unique_ptr<FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "generic class DeclMeta source should compile"))
		{
			return EXIT_FAILURE;
		}
		std::vector<FLoadedTypeInfo> Types;
		ModuleMeta::EnumerateTypes(*Module, Types);
		const FLoadedTypeInfo* BoxInst = nullptr;
		for (const FLoadedTypeInfo& Type : Types)
		{
			if (Type.SimpleName.find("Box") != std::string::npos
				&& Type.TypeName.find("int") != std::string::npos)
			{
				BoxInst = &Type;
				break;
			}
		}
		if (!Expect(
				BoxInst != nullptr
					&& HasMetaPair(BoxInst->DeclMeta, "__annot", "UCLASS")
					&& HasMetaPair(BoxInst->DeclMeta, "BlueprintType", ""),
				"generic instantiate keeps type DeclMeta"))
		{
			return EXIT_FAILURE;
		}
		std::vector<FLoadedFieldInfo> Fields;
		if (!Expect(
				ModuleMeta::EnumerateFields(*Module, BoxInst->TypeName, Fields, true),
				"EnumerateFields Box<int>"))
		{
			return EXIT_FAILURE;
		}
		const FLoadedFieldInfo* Value = nullptr;
		for (const FLoadedFieldInfo& Field : Fields)
		{
			if (Field.Name == "Value")
			{
				Value = &Field;
			}
		}
		if (!Expect(
				Value != nullptr
					&& HasMetaPair(Value->DeclMeta, "__annot", "UPROPERTY")
					&& HasMetaPair(Value->DeclMeta, "EditAnywhere", ""),
				"generic instantiate keeps field DeclMeta"))
		{
			return EXIT_FAILURE;
		}
		std::vector<FInstanceMethodInfo> Methods;
		if (!Expect(
				ModuleMeta::EnumerateInstanceMethods(*Module, BoxInst->TypeName, Methods),
				"EnumerateInstanceMethods Box<int>"))
		{
			return EXIT_FAILURE;
		}
		const FInstanceMethodInfo* Set = nullptr;
		for (const FInstanceMethodInfo& Method : Methods)
		{
			if (Method.Name == "Set")
			{
				Set = &Method;
			}
		}
		if (!Expect(
				Set != nullptr
					&& HasMetaPair(Set->DeclMeta, "__annot", "@rpc")
					&& HasMetaPair(Set->DeclMeta, "reliable", ""),
				"generic instantiate keeps method DeclMeta"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Generic interface instantiate must keep static field DeclMeta.
		const std::string Source =
			"UINTERFACE()\n"
			"interface IBox<T> {\n"
			"    UPROPERTY(EditAnywhere)\n"
			"    static int Cap = 3;\n"
			"    virtual void Touch(T Value) = 0;\n"
			"}\n"
			"class Holder : public IBox<int> {\n"
			"public:\n"
			"    void Touch(int Value) {}\n"
			"}\n"
			"void main() { print(1); }\n";
		std::unique_ptr<FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "generic interface static DeclMeta should compile"))
		{
			return EXIT_FAILURE;
		}
		std::vector<FLoadedInterfaceInfo> Interfaces;
		ModuleMeta::EnumerateInterfaces(*Module, Interfaces);
		const FLoadedInterfaceInfo* BoxIface = nullptr;
		for (const FLoadedInterfaceInfo& Iface : Interfaces)
		{
			if (Iface.SimpleName.find("IBox") != std::string::npos
				&& Iface.Name.find("int") != std::string::npos)
			{
				BoxIface = &Iface;
				break;
			}
		}
		if (!Expect(BoxIface != nullptr, "instantiated IBox<int> present"))
		{
			return EXIT_FAILURE;
		}
		const FLoadedFieldInfo* Cap = nullptr;
		for (const FLoadedFieldInfo& Field : BoxIface->Fields)
		{
			if (Field.Name == "Cap")
			{
				Cap = &Field;
			}
		}
		if (!Expect(
				Cap != nullptr
					&& Cap->bIsStatic
					&& HasMetaPair(Cap->DeclMeta, "__annot", "UPROPERTY")
					&& HasMetaPair(Cap->DeclMeta, "EditAnywhere", ""),
				"generic interface static field DeclMeta"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Base field that already satisfies an interface field inherits DeclMeta.
		const std::string Source =
			"class Base {\n"
			"public:\n"
			"    int Id = 1;\n"
			"}\n"
			"interface INamed {\n"
			"    UPROPERTY(EditAnywhere)\n"
			"    int Id;\n"
			"}\n"
			"class Named : public Base, public INamed {\n"
			"}\n"
			"void main() { print(1); }\n";
		std::unique_ptr<FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "iface field clash DeclMeta source should compile"))
		{
			return EXIT_FAILURE;
		}
		std::vector<FLoadedFieldInfo> Fields;
		if (!Expect(
				ModuleMeta::EnumerateFields(*Module, "Named", Fields, false),
				"EnumerateFields Named"))
		{
			return EXIT_FAILURE;
		}
		const FLoadedFieldInfo* Id = nullptr;
		for (const FLoadedFieldInfo& Field : Fields)
		{
			if (Field.Name == "Id")
			{
				Id = &Field;
			}
		}
		if (!Expect(
				Id != nullptr
					&& HasMetaPair(Id->DeclMeta, "__annot", "UPROPERTY")
					&& HasMetaPair(Id->DeclMeta, "EditAnywhere", ""),
				"base field inherits interface DeclMeta on clash"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Enum member DeclMeta: parse, expose via EnumerateEnums, BC roundtrip.
		const std::string Source =
			"UENUM(BlueprintType)\n"
			"enum EDamage {\n"
			"    UMETA(DisplayName=\"None at all\")\n"
			"    None,\n"
			"    Fire = 3,\n"
			"    @hidden()\n"
			"    Secret = -1,\n"
			"}\n"
			"void main() { print(1); }\n";
		std::unique_ptr<FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "enum member DeclMeta source should compile"))
		{
			return EXIT_FAILURE;
		}
		const auto CheckEnums = [](
			const FBytecodeModule& Checked,
			const char* Label) -> bool
		{
			std::vector<FLoadedEnumInfo> Enums;
			ModuleMeta::EnumerateEnums(Checked, Enums);
			const FLoadedEnumInfo* Damage = nullptr;
			for (const FLoadedEnumInfo& EnumInfo : Enums)
			{
				if (EnumInfo.SimpleName == "EDamage")
				{
					Damage = &EnumInfo;
				}
			}
			if (!Damage || Damage->Members.size() != 3)
			{
				return Expect(false, Label);
			}
			const FLoadedEnumMemberInfo& None = Damage->Members[0];
			const FLoadedEnumMemberInfo& Fire = Damage->Members[1];
			const FLoadedEnumMemberInfo& Secret = Damage->Members[2];
			return Expect(
				None.Name == "None"
					&& None.Value == 0
					&& HasMetaPair(None.DeclMeta, "__annot", "UMETA")
					&& HasMetaPair(None.DeclMeta, "DisplayName", "None at all")
					&& Fire.Name == "Fire"
					&& Fire.Value == 3
					&& Fire.DeclMeta.empty()
					&& Secret.Name == "Secret"
					&& Secret.Value == -1
					&& HasMetaPair(Secret.DeclMeta, "__annot", "@hidden")
					&& !Damage->bScoped,
				Label);
		};
		if (!CheckEnums(*Module, "enum member DeclMeta via EnumerateEnums"))
		{
			return EXIT_FAILURE;
		}

		const std::filesystem::path EnumPath =
			TemporaryRoot / "veroscript_declmeta_enum_member.vsbc";
		if (!Expect(
				VeroScriptBytecodeIO::WriteFile(EnumPath.string(), *Module, 0),
				"write enum member DeclMeta bytecode"))
		{
			return EXIT_FAILURE;
		}
		std::string EnumReadError;
		std::unique_ptr<FBytecodeModule> EnumReloaded =
			VeroScriptBytecodeIO::ReadFile(EnumPath.string(), 0, EnumReadError);
		std::error_code EnumRemoveError;
		std::filesystem::remove(EnumPath, EnumRemoveError);
		if (!Expect(static_cast<bool>(EnumReloaded), "read enum member DeclMeta bytecode"))
		{
			std::cerr << EnumReadError << '\n';
			return EXIT_FAILURE;
		}
		if (!CheckEnums(*EnumReloaded, "enum member DeclMeta survives BC roundtrip"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// enum class: scoped flag + member DeclMeta roundtrip.
		const std::string Source =
			"UENUM(BlueprintType)\n"
			"enum class EDamage {\n"
			"    UMETA(DisplayName=\"None at all\")\n"
			"    None,\n"
			"    Fire = 3,\n"
			"}\n"
			"void main() { print(EDamage.Fire); }\n";
		std::unique_ptr<FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "enum class DeclMeta source should compile"))
		{
			return EXIT_FAILURE;
		}
		std::vector<FLoadedEnumInfo> Enums;
		ModuleMeta::EnumerateEnums(*Module, Enums);
		const FLoadedEnumInfo* Damage = nullptr;
		for (const FLoadedEnumInfo& EnumInfo : Enums)
		{
			if (EnumInfo.SimpleName == "EDamage")
			{
				Damage = &EnumInfo;
			}
		}
		if (!Expect(
				Damage != nullptr
					&& Damage->bScoped
					&& Damage->Members.size() == 2
					&& HasMetaPair(Damage->DeclMeta, "__annot", "UENUM")
					&& HasMetaPair(Damage->Members[0].DeclMeta, "DisplayName", "None at all"),
				"enum class exposes bScoped + member DeclMeta"))
		{
			return EXIT_FAILURE;
		}

		const std::filesystem::path EnumPath =
			TemporaryRoot / "veroscript_declmeta_enum_class.vsbc";
		if (!Expect(
				VeroScriptBytecodeIO::WriteFile(EnumPath.string(), *Module, 0),
				"write enum class DeclMeta bytecode"))
		{
			return EXIT_FAILURE;
		}
		std::string EnumReadError;
		std::unique_ptr<FBytecodeModule> EnumReloaded =
			VeroScriptBytecodeIO::ReadFile(EnumPath.string(), 0, EnumReadError);
		std::error_code EnumRemoveError;
		std::filesystem::remove(EnumPath, EnumRemoveError);
		if (!Expect(static_cast<bool>(EnumReloaded), "read enum class DeclMeta bytecode"))
		{
			std::cerr << EnumReadError << '\n';
			return EXIT_FAILURE;
		}
		std::vector<FLoadedEnumInfo> ReloadedEnums;
		ModuleMeta::EnumerateEnums(*EnumReloaded, ReloadedEnums);
		bool bFoundScoped = false;
		for (const FLoadedEnumInfo& EnumInfo : ReloadedEnums)
		{
			if (EnumInfo.SimpleName == "EDamage" && EnumInfo.bScoped)
			{
				bFoundScoped = true;
			}
		}
		if (!Expect(bFoundScoped, "enum class bScoped survives BC roundtrip"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Method signature extras: ReturnTypeName / ParamNames / ParamFlags on
		// instance, static, and free function enumeration.
		const std::string Source =
			"int SharedAmmo = 7;\n"
			"class Weapon {\n"
			"public:\n"
			"    int Ammo = 0;\n"
			"    float Reload(int Rounds, ref float Elapsed, const ref int Cap) {\n"
			"        Elapsed = Elapsed + 1.0;\n"
			"        return Elapsed;\n"
			"    }\n"
			"    int& AmmoRef() { return Ammo; }\n"
			"    const int& ReadAmmo() const { return Ammo; }\n"
			"    void Drop() {}\n"
			"    static int MakeId(string Tag) { return 1; }\n"
			"    static int& SharedRef() { return SharedAmmo; }\n"
			"}\n"
			"int PickTotal(int Left, ref int Right) { Right = Right + Left; return Right; }\n"
			"const int& PickShared() { return SharedAmmo; }\n"
			"fn(int)->int MakeAdder(int Base) {\n"
			"    return fn(int X) -> int => Base + X;\n"
			"}\n"
			"fn(int)->fn(int)->int MakeNest() {\n"
			"    return fn(int X) -> fn(int)->int => fn(int Y) -> int => X + Y;\n"
			"}\n"
			"void main() { print(1); }\n";
		std::unique_ptr<FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "signature extras source should compile"))
		{
			return EXIT_FAILURE;
		}

		std::vector<FInstanceMethodInfo> Methods;
		if (!Expect(
				ModuleMeta::EnumerateInstanceMethods(*Module, "Weapon", Methods),
				"EnumerateInstanceMethods Weapon"))
		{
			return EXIT_FAILURE;
		}
		const FInstanceMethodInfo* Reload = nullptr;
		const FInstanceMethodInfo* Drop = nullptr;
		const FInstanceMethodInfo* AmmoRef = nullptr;
		const FInstanceMethodInfo* ReadAmmo = nullptr;
		for (const FInstanceMethodInfo& Method : Methods)
		{
			if (Method.Name == "Reload")
			{
				Reload = &Method;
			}
			if (Method.Name == "Drop")
			{
				Drop = &Method;
			}
			if (Method.Name == "AmmoRef")
			{
				AmmoRef = &Method;
			}
			if (Method.Name == "ReadAmmo")
			{
				ReadAmmo = &Method;
			}
		}
		if (!Expect(
				Reload != nullptr
					&& Reload->ReturnTypeName == "float"
					&& Reload->ParamNames.size() == 3
					&& Reload->ParamNames[0] == "Rounds"
					&& Reload->ParamNames[1] == "Elapsed"
					&& Reload->ParamNames[2] == "Cap"
					&& Reload->ParamFlags.size() == 3
					&& Reload->ParamFlags[0] == 0
					&& Reload->ParamFlags[1] == BytecodeParamFlagRef
					&& Reload->ParamFlags[2]
						== (BytecodeParamFlagRef | BytecodeParamFlagConstRef),
				"instance method ReturnTypeName/ParamNames/ParamFlags"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Drop != nullptr && Drop->bReturnsVoid && Drop->ReturnTypeName == "void",
				"void method ReturnTypeName is 'void'"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				AmmoRef != nullptr
					&& AmmoRef->ReturnTypeName == "ref int"
					&& AmmoRef->bReturnsRef
					&& !AmmoRef->bReturnsConstRef
					&& ReadAmmo != nullptr
					&& ReadAmmo->ReturnTypeName == "const ref int"
					&& ReadAmmo->bReturnsRef
					&& ReadAmmo->bReturnsConstRef,
				"instance reference-return metadata"))
		{
			return EXIT_FAILURE;
		}

		std::vector<FStaticMethodInfo> StaticMethods;
		if (!Expect(
				ModuleMeta::EnumerateStaticMethods(*Module, "Weapon", StaticMethods),
				"EnumerateStaticMethods Weapon"))
		{
			return EXIT_FAILURE;
		}
		const FStaticMethodInfo* MakeId = nullptr;
		const FStaticMethodInfo* SharedRef = nullptr;
		for (const FStaticMethodInfo& Method : StaticMethods)
		{
			if (Method.Name == "MakeId")
			{
				MakeId = &Method;
			}
			if (Method.Name == "SharedRef")
			{
				SharedRef = &Method;
			}
		}
		if (!Expect(
				MakeId != nullptr
					&& MakeId->ReturnTypeName == "int"
					&& MakeId->ParamNames.size() == 1
					&& MakeId->ParamNames[0] == "Tag"
					&& MakeId->ParamFlags.size() == 1
					&& MakeId->ParamFlags[0] == 0,
				"static method ReturnTypeName/ParamNames/ParamFlags"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				SharedRef != nullptr
					&& SharedRef->ReturnTypeName == "ref int"
					&& SharedRef->bReturnsRef
					&& !SharedRef->bReturnsConstRef,
				"static reference-return metadata"))
		{
			return EXIT_FAILURE;
		}

		std::vector<FFreeFunctionInfo> FreeFunctions;
		ModuleMeta::EnumerateFreeFunctions(*Module, FreeFunctions);
		const FFreeFunctionInfo* PickTotal = nullptr;
		const FFreeFunctionInfo* PickShared = nullptr;
		for (const FFreeFunctionInfo& Function : FreeFunctions)
		{
			if (Function.Name == "PickTotal")
			{
				PickTotal = &Function;
			}
			if (Function.Name == "PickShared")
			{
				PickShared = &Function;
			}
		}
		if (!Expect(
				PickTotal != nullptr
					&& PickTotal->ReturnTypeName == "int"
					&& PickTotal->ParamNames.size() == 2
					&& PickTotal->ParamNames[0] == "Left"
					&& PickTotal->ParamNames[1] == "Right"
					&& PickTotal->ParamFlags.size() == 2
					&& PickTotal->ParamFlags[0] == 0
					&& PickTotal->ParamFlags[1] == BytecodeParamFlagRef,
				"free function ReturnTypeName/ParamNames/ParamFlags"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				PickShared != nullptr
					&& PickShared->ReturnTypeName == "const ref int"
					&& PickShared->bReturnsRef
					&& PickShared->bReturnsConstRef,
				"free const-reference-return metadata"))
		{
			return EXIT_FAILURE;
		}
		const FFreeFunctionInfo* MakeAdder = nullptr;
		const FFreeFunctionInfo* MakeNest = nullptr;
		for (const FFreeFunctionInfo& Function : FreeFunctions)
		{
			if (Function.Name == "MakeAdder")
			{
				MakeAdder = &Function;
			}
			if (Function.Name == "MakeNest")
			{
				MakeNest = &Function;
			}
		}
		if (!Expect(
				MakeAdder != nullptr && MakeAdder->ReturnTypeName == "fn(int)->int",
				"MakeAdder ReturnTypeName is fn(int)->int"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				MakeNest != nullptr && MakeNest->ReturnTypeName == "fn(int)->fn(int)->int",
				"nested fn ReturnTypeName keeps full RHS"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// A concrete override and its interface declaration describe one callable
		// signature. Interface-only overloads must remain independently visible.
		const std::string Source =
			"interface IDamageable {\n"
			"    @rpc(reliable)\n"
			"    virtual void ApplyDamage(float InterfaceAmount) = 0;\n"
			"    void ApplyDamage(int InterfaceCount) {}\n"
			"}\n"
			"class DamageActor : public IDamageable {\n"
			"public:\n"
			"    DamageActor() {}\n"
			"    void ApplyDamage(float DamageAmount) {}\n"
			"}\n"
			"class DerivedDamageActor : public DamageActor {}\n"
			"void main() {}\n";
		std::unique_ptr<FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "interface override resolution source"))
		{
			return EXIT_FAILURE;
		}

		std::uint16_t ResolvedIndex = static_cast<std::uint16_t>(-1);
		if (!Expect(
				ModuleMeta::TryResolveInstanceMethodOnType(
					*Module,
					"DamageActor",
					"ApplyDamage#5:float",
					1,
					ResolvedIndex)
					== EMethodResolveStatus::Found,
				"exact override linkage resolves uniquely"))
		{
			return EXIT_FAILURE;
		}
		std::uint16_t DamageActorIndex = static_cast<std::uint16_t>(-1);
		if (!Expect(
				ModuleMeta::FindTypeIndex(*Module, "DamageActor", DamageActorIndex)
					&& ResolvedIndex < Module->Functions.size()
					&& Module->Functions[ResolvedIndex].OwnerTypeIndex == DamageActorIndex,
				"class override wins over matching interface declaration"))
		{
			return EXIT_FAILURE;
		}
		std::uint16_t InheritedResolvedIndex = static_cast<std::uint16_t>(-1);
		if (!Expect(
				ModuleMeta::TryResolveInstanceMethodOnType(
					*Module,
					"DerivedDamageActor",
					"ApplyDamage#5:float",
					1,
					InheritedResolvedIndex)
					== EMethodResolveStatus::Found
					&& InheritedResolvedIndex == ResolvedIndex,
				"nearest inherited class implementation wins over interface declaration"))
		{
			return EXIT_FAILURE;
		}

		std::vector<FInstanceMethodInfo> Methods;
		if (!Expect(
				ModuleMeta::EnumerateInstanceMethods(*Module, "DamageActor", Methods),
				"enumerate override plus interface overload"))
		{
			return EXIT_FAILURE;
		}
		std::size_t ApplyDamageCount = 0;
		bool bSawClassFloat = false;
		bool bSawInheritedAnnotation = false;
		bool bSawInterfaceInt = false;
		for (const FInstanceMethodInfo& Method : Methods)
		{
			if (Method.Name != "ApplyDamage" || Method.ParamTypeNames.size() != 1)
			{
				continue;
			}
			++ApplyDamageCount;
			bSawClassFloat = bSawClassFloat
				|| (Method.ParamTypeNames[0] == "float"
					&& Method.DeclaringTypeName.find("DamageActor") != std::string::npos);
			bSawInheritedAnnotation = bSawInheritedAnnotation
				|| (Method.ParamTypeNames[0] == "float"
					&& HasMetaPair(Method.DeclMeta, "__annot", "@rpc")
					&& HasMetaPair(Method.DeclMeta, "reliable", ""));
			bSawInterfaceInt = bSawInterfaceInt
				|| (Method.ParamTypeNames[0] == "int"
					&& Method.DeclaringTypeName.find("IDamageable") != std::string::npos);
		}
		if (!Expect(
				ApplyDamageCount == 2
					&& bSawClassFloat
					&& bSawInheritedAnnotation
					&& bSawInterfaceInt,
				"deduplicate override while preserving interface metadata and overload"))
		{
			return EXIT_FAILURE;
		}

		std::vector<FInstanceMethodInfo> InterfaceMethods;
		if (!Expect(
				ModuleMeta::EnumerateInstanceMethods(
					*Module, "IDamageable", InterfaceMethods),
				"enumerate interface parameter metadata"))
		{
			return EXIT_FAILURE;
		}
		bool bPreservedAbstractParamName = false;
		for (const FInstanceMethodInfo& Method : InterfaceMethods)
		{
			if (Method.Name == "ApplyDamage"
				&& Method.bIsAbstract
				&& Method.ParamTypeNames == std::vector<std::string>{"float"} && Method.ParamNames == std::vector<std::string>{"InterfaceAmount"})
			{
				bPreservedAbstractParamName = true;
			}
		}
		if (!Expect(
				bPreservedAbstractParamName,
				"abstract interface parameter name is preserved"))
		{
			return EXIT_FAILURE;
		}

		std::vector<std::uint8_t> Bytes;
		std::string RoundTripError;
		std::unique_ptr<FBytecodeModule> Reloaded;
		if (VeroScriptBytecodeIO::SerializeBytecode(
				*Module, 42, Bytes, RoundTripError))
		{
			Reloaded = VeroScriptBytecodeIO::DeserializeBytecode(
				FByteView(Bytes), std::uint64_t{42}, RoundTripError);
		}
		std::vector<FInstanceMethodInfo> ReloadedInterfaceMethods;
		bool bRoundTripParamName = Reloaded
			&& ModuleMeta::EnumerateInstanceMethods(
				*Reloaded, "IDamageable", ReloadedInterfaceMethods);
		if (bRoundTripParamName)
		{
			bRoundTripParamName = std::any_of(
				ReloadedInterfaceMethods.begin(),
				ReloadedInterfaceMethods.end(),
				[](const FInstanceMethodInfo& Method)
				{
					return Method.Name == "ApplyDamage"
							&& Method.bIsAbstract
							&& Method.ParamNames
								== std::vector<std::string>{"InterfaceAmount"};
				});
		}
		if (!Expect(
				bRoundTripParamName,
				"abstract parameter metadata survives bytecode roundtrip"))
		{
			std::cerr << RoundTripError << '\n';
			return EXIT_FAILURE;
		}
	}

	std::cout << "VeroScriptDeclMetaTests: ok\n";
	return EXIT_SUCCESS;
}
