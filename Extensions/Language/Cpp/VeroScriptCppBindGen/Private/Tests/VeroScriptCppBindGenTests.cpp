// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCppBindGen.h"

#include "VeroScriptHostBinding.h"
#include "VeroScriptHostBindingCatalog.h"
#include "VeroScriptCppToolOutput.h"
#include "VeroScriptPath.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>

namespace
{
	bool Expect(const bool Condition, const char* Message)
	{
		if (Condition)
		{
			return true;
		}
		std::cerr << Message << '\n';
		return false;
	}
}

int main()
{
	using namespace VeroScript;
	using namespace VeroScript::CppBindGen;
	const std::string Source = R"cpp(
namespace Demo
{
	enum class EMode : unsigned char
	{
		Off = 0,
		Read = 1 << 0,
		Write = 1 << 1,
		All = Read | Write,
		Next
	};
	struct Counter
	{
		int Value;
		Counter(int Initial);
		int Get() const;
		void Set(int& NewValue);
		void Inspect(const Counter& Other) const;
		bool operator<(const Counter& Other) const;
		int operator()(const int& Delta) const;
		int operator[](int& Index);
		static int Twice(int Value);
	};
	int Add(int Left, int Right);
}
)cpp";
	const FBindGenResult Generated = GenerateBindingsFromSource(Source, "bindgen_test.cpp");
	if (!Generated.Succeeded())
	{
		for (const auto& Diagnostic : Generated.Diagnostics)
		{
			std::cerr << CppFrontend::FormatDiagnostic(Diagnostic) << '\n';
		}
		return 1;
	}
	bool Ok = true;
	{
		const auto References = GenerateBindingsFromSource(
			"using MutableRef = int&; using ReadonlyRef = const int&; "
			"void Mutate(const MutableRef Value); void Inspect(const ReadonlyRef Value);",
			"reference-alias-bindings.hpp");
		const auto Declarations = CppFrontend::ParseSource(
			"using MutableRef = int&; using ReadonlyRef = const int&;", "reference-types.hpp");
		const auto Uses = CppFrontend::ParseSource(
			"void Mutate(const MutableRef Value); void Inspect(const ReadonlyRef Value);",
			"reference-uses.hpp");
		const auto Merged = CppFrontend::MergeTranslationUnits({Declarations.Unit, Uses.Unit});
		Ok &= Expect(Declarations.Succeeded() && Uses.Succeeded() && Merged.Succeeded(),
			"cross-file reference alias binding inputs must parse and merge");
		for (const auto& GeneratedReferences : {References, GenerateBindings(Merged.Unit)})
		{
			FHostBindingRegistry Registry;
			std::string Error;
			const bool Loaded = GeneratedReferences.Succeeded()
				&& ImportBindingCatalogJson(GeneratedReferences.CatalogJson, Registry, Error);
			Ok &= Expect(Loaded && Registry.GetFunctions().size() == 2
				&& Registry.GetFunctions()[0].ParamFlags == std::vector<std::uint8_t>{NativeParamFlagRef}
				&& Registry.GetFunctions()[1].ParamFlags == std::vector<std::uint8_t>{NativeParamFlagRef | NativeParamFlagConstRef},
				"local and cross-file reference aliases must preserve mutable and const-reference native parameter flags");
		}
	}
	{
		const auto Root = CppFrontend::ParseSource("struct RootItem{int value;}; using RootAlias=RootItem; "
			"namespace N{struct RootItem{double other;};}", "global-alias.hpp");
		const auto Uses = CppFrontend::ParseSource("namespace N{RootAlias Echo(RootAlias input);}", "global-alias-use.hpp");
		const auto Merged = CppFrontend::MergeTranslationUnits({Root.Unit, Uses.Unit});
		const auto GeneratedAliases = GenerateBindings(Merged.Unit);
		FHostBindingRegistry Registry;
		std::string Error;
		const bool Loaded = Root.Succeeded() && Uses.Succeeded() && Merged.Succeeded()
			&& GeneratedAliases.Succeeded() && ImportBindingCatalogJson(GeneratedAliases.CatalogJson, Registry, Error);
		Ok &= Expect(Loaded && Registry.GetFunctions().size() == 1
			&& Registry.GetFunctions()[0].ReturnHostTypeName == "RootItem"
			&& Registry.GetFunctions()[0].ParamHostTypeNames == std::vector<std::string>{"RootItem"},
			"cross-file binding aliases retain the global target instead of a caller namespace type");
	}
	{
		const auto Shadowed = GenerateBindingsFromSource("using Value=int; namespace N { "
			"struct Value{int field;}; Value Echo(Value input); }", "shadowed-alias.hpp");
		FHostBindingRegistry Registry;
		std::string Error;
		const bool Loaded = Shadowed.Succeeded()
			&& ImportBindingCatalogJson(Shadowed.CatalogJson, Registry, Error);
		Ok &= Expect(Loaded && Registry.GetFunctions().size() == 1
			&& Registry.GetFunctions()[0].ReturnHostTypeName == "N::Value"
			&& Registry.GetFunctions()[0].ParamHostTypeNames == std::vector<std::string>{"N::Value"},
			"bindings must preserve records that shadow an outer scalar alias");
	}
	{
		const auto Scoped = GenerateBindingsFromSource(
			"struct Item{};namespace A{struct Item{};namespace Nested{struct Node{};}"
			"Item Local(Item value);::Item Global(::Item value);Nested::Node Relative(Nested::Node value);}"
			"namespace B{namespace Nested{struct Node{};}Nested::Node Relative(Nested::Node value);}"
			"namespace Model{struct string{};string Echo(string value);}", "scoped-types.hpp");
		FHostBindingRegistry ScopedRegistry;
		std::string ScopedError;
		const bool Loaded = Scoped.Succeeded() && ImportBindingCatalogJson(Scoped.CatalogJson, ScopedRegistry, ScopedError);
		Ok &= Expect(Loaded, "scoped binding types generate an importable catalog");
		if (Loaded)
		{
			for (const auto& Expected : std::vector<std::pair<std::string, std::string>>{
					 {"A::Local", "A::Item"}, {"A::Global", "Item"}, {"A::Relative", "A::Nested::Node"},
					 {"B::Relative", "B::Nested::Node"}, {"Model::Echo", "Model::string"}})
			{
				const auto& Functions = ScopedRegistry.GetFunctions();
				const auto I = std::find_if(Functions.begin(), Functions.end(),
					[&](const auto& F)
					{
						return F.Name == Expected.first;
					});
				Ok &= Expect(I != Functions.end() && I->ReturnType == ENativeType::HostObject &&
						I->ReturnHostTypeName == Expected.second &&
						I->ParamTypes == std::vector<ENativeType>{ENativeType::HostObject} &&
						I->ParamHostTypeNames == std::vector<std::string>{Expected.second},
					"catalog signatures preserve the resolved C++ type identity");
			}
		}

		Ok &= Expect(!GenerateBindingsFromSource("namespace A{struct Item{};}int Read(Missing::Item value);")
						 .Succeeded(),
			"unknown qualified host types must not produce a binding catalog");
	}
	Ok &= Expect(!Generated.CatalogJson.empty(), "catalog JSON must be generated");
	{
		const auto Typed = GenerateBindingsFromSource("enum class Flags : unsigned int { All=~0u, Wrap=0xffffffffu+1u, More=All>1 }; "
			"enum Loose { A=1u, B=(A-2)>0 };", "typed-enum-bindings.cpp");
		FHostBindingRegistry Catalog;
		std::string Error;
		Ok &= Expect(Typed.Succeeded() && ImportBindingCatalogJson(Typed.CatalogJson, Catalog, Error), "typed enum catalog imports");
		const auto* Flags = Catalog.FindType("Flags");
		const auto* Loose = Catalog.FindType("Loose");
		const auto HasValue = [](const FHostTypeDesc* Type, const char* Name, std::int64_t Value)
		{
			return Type &&
				std::any_of(Type->Members.begin(), Type->Members.end(),
					[&](const auto& M)
					{
						return M.Name == Name && M.EnumValue == Value;
					});
		};
		Ok &= Expect(HasValue(Flags,"All",4294967295LL) && HasValue(Flags,"Wrap",0) && HasValue(Flags,"More",1)
			&& HasValue(Loose,"B",1), "binding enum metadata preserves C++ unsigned and unfixed initializer semantics");
	}
	Ok &= Expect(Generated.ApiReference.find("Demo::Counter") != std::string::npos,
		"API reference must contain the qualified type");

	FHostBindingRegistry Imported;
	std::string Error;
	Ok &= Expect(ImportBindingCatalogJson(Generated.CatalogJson, Imported, Error),
		"generated catalog must import successfully");
	const FHostTypeDesc* Counter = Imported.FindType("Demo::Counter");
	const FHostTypeDesc* Mode = Imported.FindType("Demo::EMode");
	Ok &= Expect(Counter != nullptr, "generated record type missing from catalog");
	Ok &= Expect(Mode != nullptr && Mode->bScopedEnum, "generated scoped enum metadata missing");
	if (Mode)
	{
		bool FoundAll = false;
		bool FoundNext = false;
		for (const FHostMemberDesc& Member : Mode->Members)
		{
			FoundAll = FoundAll || (Member.Name == "All" && Member.EnumValue == 3);
			FoundNext = FoundNext || (Member.Name == "Next" && Member.EnumValue == 4);
		}
		Ok &= Expect(FoundAll && FoundNext,
			"integer enum expressions and following implicit values must be evaluated");
	}
	if (Counter)
	{
		bool FoundConstGet = false;
		bool FoundStatic = false;
		bool FoundRef = false;
		bool FoundConstRef = false;
		bool FoundComparisonOperator = false;
		bool FoundCallOperator = false;
		bool FoundSubscriptOperator = false;
		for (const FHostMemberDesc& Member : Counter->Members)
		{
			FoundConstGet = FoundConstGet
				|| (Member.Name == "Get" && Member.bConstMethod);
			FoundStatic = FoundStatic
				|| (Member.Name == "Twice" && Member.Kind == EHostMemberKind::StaticMethod);
			FoundRef = FoundRef || (Member.Name == "Set" && Member.ParamFlags.size() == 1
				&& Member.ParamFlags[0] == NativeParamFlagRef);
			FoundConstRef = FoundConstRef || (Member.Name == "Inspect"
				&& Member.ParamFlags.size() == 1
				&& Member.ParamFlags[0] == (NativeParamFlagRef | NativeParamFlagConstRef));
			FoundComparisonOperator = FoundComparisonOperator
				|| (Member.Name == "operator<" && Member.bOperator
					&& Member.bConstMethod);
			FoundCallOperator = FoundCallOperator
				|| (Member.Name == "operator()" && Member.bOperator && Member.bConstMethod
					&& Member.ParamFlags.size() == 1
					&& Member.ParamFlags[0] == (NativeParamFlagRef | NativeParamFlagConstRef));
			FoundSubscriptOperator = FoundSubscriptOperator
				|| (Member.Name == "operator[]" && Member.bOperator
					&& Member.ParamFlags.size() == 1 && Member.ParamFlags[0] == NativeParamFlagRef);
		}
		Ok &= Expect(FoundConstGet, "const method metadata was not preserved");
		Ok &= Expect(FoundStatic, "static method metadata was not preserved");
		Ok &= Expect(FoundRef, "mutable reference parameter metadata was not preserved");
		Ok &= Expect(FoundConstRef, "const reference parameter metadata was not preserved");
		Ok &= Expect(FoundComparisonOperator,
			"comparison operator metadata was not preserved");
		Ok &= Expect(FoundCallOperator && FoundSubscriptOperator,
			"call/subscript operator reference metadata must survive catalog generation and import");
	}

	const FBindGenResult Annotated = GenerateBindingsFromSource(R"cpp(
[[veroscript::Serializable(Version=1)]]
struct Reflected
{
    [[veroscript::Property(Replicated)]] int Value;
    [[veroscript::Callable(Category="Test")]]
    int Read([[veroscript::Input(Required)]] int Amount) const;
};
[[veroscript::Utility(Category="Numbers")]] int Double(int Value);
)cpp",
		"annotations.hpp");
	Ok &= Expect(Annotated.Succeeded(),
		"structured C++ declaration attributes must generate binding annotations");
	FHostBindingRegistry AnnotatedRegistry;
	std::string AnnotationError;
	Ok &= Expect(Annotated.Succeeded()
		&& ImportBindingCatalogJson(Annotated.CatalogJson, AnnotatedRegistry, AnnotationError),
		"annotated binding catalog must round-trip through JSON");
	const FHostTypeDesc* Reflected = AnnotatedRegistry.FindType("Reflected");
	Ok &= Expect(Reflected && Reflected->Annotations.size() == 1
		&& Reflected->Annotations[0].Name == "Serializable"
		&& Reflected->Annotations[0].Arguments.size() == 1
		&& Reflected->Annotations[0].Arguments[0].Value
		&& *Reflected->Annotations[0].Arguments[0].Value == "1",
		"type annotation arguments were not preserved");
	if (Reflected)
	{
		const auto FieldIt = std::find_if(Reflected->Members.begin(), Reflected->Members.end(),
			[](const FHostMemberDesc& Member)
			{
				return Member.Name == "Value";
			});
		const auto MethodIt = std::find_if(Reflected->Members.begin(), Reflected->Members.end(),
			[](const FHostMemberDesc& Member)
			{
				return Member.Name == "Read";
			});
		Ok &= Expect(FieldIt != Reflected->Members.end()
			&& FieldIt->Annotations.size() == 1
			&& FieldIt->Annotations[0].Name == "Property",
			"field annotations were not preserved");
		Ok &= Expect(MethodIt != Reflected->Members.end()
			&& MethodIt->Annotations.size() == 1
			&& MethodIt->Annotations[0].Arguments[0].Value
			&& *MethodIt->Annotations[0].Arguments[0].Value == "Test"
			&& MethodIt->ParamAnnotations.size() == 1
			&& MethodIt->ParamAnnotations[0].size() == 1
			&& MethodIt->ParamAnnotations[0][0].Name == "Input",
			"method and parameter annotations were not preserved");
	}
	const auto UtilityIt = std::find_if(
		AnnotatedRegistry.GetFunctions().begin(), AnnotatedRegistry.GetFunctions().end(),
		[](const FHostFunctionDesc& Function)
		{
			return Function.Name == "Double";
		});
	Ok &= Expect(UtilityIt != AnnotatedRegistry.GetFunctions().end()
		&& UtilityIt->Annotations.size() == 1
		&& UtilityIt->Annotations[0].Name == "Utility",
		"free-function annotations were not preserved");
	Ok &= Expect(!GenerateBindingsFromSource(
		"[[vendor::Opaque]] struct Bad {};", "vendor_attribute.hpp")
					 .Succeeded(),
		"unknown C++ attribute namespaces must not be conflated with VeroScript annotations");
	const FBindGenResult Aliases = GenerateBindingsFromSource(R"cpp(
typedef int Count;
template<typename T> using List = std::vector<T>;
)cpp",
		"aliases.hpp");
	FHostBindingRegistry AliasRegistry;
	std::string AliasError;
	Ok &= Expect(Aliases.Succeeded()
		&& ImportBindingCatalogJson(Aliases.CatalogJson, AliasRegistry, AliasError)
		&& AliasRegistry.GetTypeAliases().size() == 2
		&& AliasRegistry.GetTypeAliases()[0].Name == "Count"
		&& AliasRegistry.GetTypeAliases()[1].Name == "List"
		&& AliasRegistry.GetTypeAliases()[1].TypeParams.size() == 1
		&& AliasRegistry.GetTypeAliases()[1].TargetTypeName == "array<T>",
		"ordinary and generic C++ aliases must generate catalog type aliases");
	Ok &= Expect(!GenerateBindingsFromSource(
		"template<typename T = int> using DefaultList = std::vector<T>;",
		"default_alias.hpp")
					 .Succeeded(),
		"alias-template defaults must fail until the catalog can preserve them");

	const FBindGenResult PointerRejected = GenerateBindingsFromSource(
		"struct Bad { int* Value; };", "bad_pointer.cpp");
	Ok &= Expect(!PointerRejected.Succeeded(), "raw pointers must fail without an explicit policy");
	Ok &= Expect(!GenerateBindingsFromSource(
		"int Global = 1;", "global.cpp")
					 .Succeeded(),
		"free global variables must not disappear from a generated catalog");
	Ok &= Expect(!GenerateBindingsFromSource(
		"struct Bad { int& Alias; };", "reference_field.cpp")
					 .Succeeded(),
		"reference fields must not lose alias and lifetime semantics in binding metadata");
	Ok &= Expect(!GenerateBindingsFromSource(
		"int WithDefault(int Value = 1);", "default_arg.cpp")
					 .Succeeded(),
		"default arguments must not disappear from binding metadata");
	Ok &= Expect(!GenerateBindingsFromSource(
		"consteval int Immediate();", "consteval.cpp")
					 .Succeeded(),
		"consteval functions cannot be exposed as runtime bindings");
	const FBindGenResult ForwardType = GenerateBindingsFromSource(
		"class Opaque; void Accept(const Opaque& Value);", "forward_type.hpp");
	Ok &= Expect(ForwardType.Succeeded(),
		"binding catalogs should retain forward-declared host type identity");
	if (!ForwardType.Succeeded())
	{
		for (const auto& Diagnostic : ForwardType.Diagnostics)
		{
			std::cerr << CppFrontend::FormatDiagnostic(Diagnostic) << '\n';
		}
	}
	Ok &= Expect(!GenerateBindingsFromSource(
		"static int Hidden();", "internal.cpp")
					 .Succeeded(),
		"internal-linkage free functions must not be exported by BindGen");
	Ok &= Expect(!GenerateBindingsFromSource(
		"enum class Small : unsigned char { TooLarge = 256 };",
		"enum_range.cpp")
					 .Succeeded(),
		"enum values outside the declared underlying range must be rejected");
	{
		namespace fs = std::filesystem;
		const fs::path Directory = fs::temp_directory_path()
			/ ("veroscript_cpp_bindgen_project_"
				+ std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::error_code FileError;
		fs::create_directories(Directory, FileError);
		const fs::path FirstPath = Directory / "first.hpp";
		const fs::path SecondPath = Directory / "second.hpp";
		{
			std::ofstream First(FirstPath, std::ios::binary);
			First << "using Count = int; "
				"template<typename T> using List = std::vector<T>; "
				"struct First { int Value; };\n";
			std::ofstream Second(SecondPath, std::ios::binary);
			Second << "struct Second { First Copy(First Value); "
				"Count Total; List<int> Echo(List<int> Values); };\n";
		}
		const FBindGenProjectResult Project = GenerateBindingProject(
			{FirstPath.string(), SecondPath.string()});
		Ok &= Expect(Project.Succeeded() && Project.ApiReferences.size() == 2,
			"binding projects must emit one API reference per input");
		Ok &= Expect(!Project.CatalogJson.empty()
			&& Project.ManifestJson.find("apiReferences") != std::string::npos,
			"binding projects must retain one merged catalog and a manifest");
		if (Project.ApiReferences.size() == 2)
		{
			Ok &= Expect(Project.ApiReferences[1].Contents.find("First") != std::string::npos,
				"project-wide host types must map in sibling API references");
			Ok &= Expect(Project.ApiReferences[1].Contents.find("<unsupported>")
				== std::string::npos,
				"project-wide aliases must map in sibling API references");
		}
		Ok &= Expect(!GenerateBindingProject(
						 {FirstPath.string(), FirstPath.string()})
						 .Succeeded(),
			"binding projects must reject duplicate input identities");

		const fs::path FirstDirectory = Directory / "first";
		const fs::path SecondDirectory = Directory / "second";
		fs::create_directories(FirstDirectory, FileError);
		fs::create_directories(SecondDirectory, FileError);
		const fs::path FirstSameName = FirstDirectory / "shared.hpp";
		const fs::path SecondSameName = SecondDirectory / "shared.hpp";
		{
			std::ofstream First(FirstSameName, std::ios::binary);
			First << "struct FirstShared { int Value; };\n";
			std::ofstream Second(SecondSameName, std::ios::binary);
			Second << "struct SecondShared { int Value; };\n";
		}
		const FBindGenProjectResult SameNames = GenerateBindingProject(
			{FirstSameName.string(), SecondSameName.string()});
		Ok &= Expect(SameNames.Succeeded() && SameNames.ApiReferences.size() == 2
			&& SameNames.ApiReferences[0].OutputName != SameNames.ApiReferences[1].OutputName
			&& SameNames.ApiReferences[0].OutputName.find("shared.") == 0
			&& SameNames.ApiReferences[1].OutputName.find("shared.") == 0,
			"same-basename binding inputs must receive distinct deterministic reference names");
		const fs::path UnicodeFirst = Directory / fs::u8path("\xC3\x89.h");
		const fs::path UnicodeSecond = Directory / fs::u8path("\xC3\xA9.hpp");
		{
			std::ofstream File(UnicodeFirst);
			File << "struct UnicodeFirst { int Value; };";
		}
		{
			std::ofstream File(UnicodeSecond);
			File << "struct UnicodeSecond { int Value; };";
		}
		const auto UnicodeNames = GenerateBindingProject(
			{VeroScript::PathUtil::ToUtf8(UnicodeFirst), VeroScript::PathUtil::ToUtf8(UnicodeSecond)});
		const VeroScript::CppTools::FOutputFileNameLess NameLess;
		Ok &= Expect(UnicodeNames.Succeeded() && UnicodeNames.ApiReferences.size() == 2
			&& (NameLess(UnicodeNames.ApiReferences[0].OutputName, UnicodeNames.ApiReferences[1].OutputName)
				|| NameLess(UnicodeNames.ApiReferences[1].OutputName, UnicodeNames.ApiReferences[0].OutputName)),
			"Unicode case-colliding binding inputs must generate distinct API reference names");
		fs::remove_all(Directory, FileError);
	}
	return Ok ? 0 : 1;
}
