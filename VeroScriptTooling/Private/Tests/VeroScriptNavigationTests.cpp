// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptToolingTestVm.h"
#include "VeroScriptTooling.h"
#include "VeroScriptPath.h"
#include "VeroScriptModuleLoader.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
	int Fail(const char* Message)
	{
		std::cerr << "VeroScriptNavigationTests: " << Message << '\n';
		return EXIT_FAILURE;
	}

	bool Expect(const bool Condition, const char* Message)
	{
		return Condition ? true : (Fail(Message), false);
	}

	// Find 1-based line/column of the first occurrence of Needle in Source.
	bool FindPosition(
	    const std::string_view Source,
	    const std::string_view Needle,
	    VeroScript::Tooling::FTextPosition& OutPosition)
	{
		const std::size_t Offset = Source.find(Needle);
		if (Offset == std::string_view::npos)
		{
			return false;
		}

		std::uint32_t Line = 1;
		std::uint32_t Column = 1;
		for (std::size_t Index = 0; Index < Offset; ++Index)
		{
			if (Source[Index] == '\n')
			{
				++Line;
				Column = 1;
			}
			else
			{
				++Column;
			}
		}
		OutPosition.Line = Line;
		OutPosition.Column = Column;
		return true;
	}
}

int main()
{
	using namespace VeroScript::Tooling;

	for (const std::string_view ConstrainedSource : {
	         "interface Guard<F>{};void Inspect<T>(T item) where T:Guard<fn()->int>{print(item);}",
	         "interface Guard<F>{};fn()->int Inspect<T>(T item) where T:Guard<fn(int)->int>{print(item);throw 0;}",
	         "interface Guard<F>{};class Holder<T>{public:void Inspect(T item) where T:Guard<fn()->int>{print(item);}};",
	         "interface Guard<F>{};class Earlier<T> where T:Guard<fn()->int>{};"
	         "void Inspect(int item){class Local<T>{public:void Method(T value) where T:Guard<fn()->int>{}};print(item);}"})
	{
		FTextPosition Position, Definition;
		if (!Expect(FindPosition(ConstrainedSource, "print(item)", Position) && FindPosition(ConstrainedSource, " item", Definition),
		        "locate parameter around function types in trailing constraints"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("print(").size());
		++Definition.Column;
		const auto Result = VeroScriptNavigation::QueryAtSource(ConstrainedSource, Position, "<where-parameter-definition>");
		if (!Expect(Result.bSuccess && Result.Definitions.size() == 1 && Result.Definitions.front().DefinitionRange.StartLine == Definition.Line && Result.Definitions.front().DefinitionRange.StartColumn == Definition.Column,
		        "trailing constraints, return types and nested bodies do not replace the callable's parameter list"))
		{
			return EXIT_FAILURE;
		}
	}

	for (const bool bMethod : {false, true})
	{
		std::string DefaultSource =
		    "const int second=4;\nint Combine(int left,int right){return left+right;}\n";
		if (bMethod)
		{
			DefaultSource += "struct Holder{\n";
		}
		DefaultSource +=
		    "void Inspect(int first=second,int second=Combine(1,2),bool test=1<2,int last=3){\n"
		    "print(first);print(second);print(test);print(last);\n}\n";
		if (bMethod)
		{
			DefaultSource += "};\n";
		}
		for (const char* Name : {"first", "second", "test", "last"})
		{
			FTextPosition Position, Definition;
			if (!Expect(FindPosition(DefaultSource, std::string("print(") + Name + ")", Position) && FindPosition(DefaultSource, std::string(Name) + "=", Definition),
			        "locate defaulted parameter reference and declaration"))
			{
				return EXIT_FAILURE;
			}
			Position.Column += static_cast<std::uint32_t>(std::string_view("print(").size());
			// The global homonym intentionally appears in an earlier default.
			if (std::string_view(Name) == "second")
			{
				if (!Expect(FindPosition(DefaultSource, "int second=Combine", Definition),
				        "locate parameter rather than its global homonym"))
				{
					return EXIT_FAILURE;
				}
				Definition.Column += static_cast<std::uint32_t>(std::string_view("int ").size());
			}
			const auto Result = VeroScriptNavigation::QueryAtSource(DefaultSource, Position, "<default-parameter-definition>");
			if (!Expect(Result.bSuccess && Result.Definitions.size() == 1 && Result.Definitions.front().DefinitionRange.StartLine == Definition.Line && Result.Definitions.front().DefinitionRange.StartColumn == Definition.Column,
			        "default values preserve this and following parameter definitions without binding expression names"))
			{
				std::cerr << "parameter=" << Name << ", method=" << bMethod << ", success=" << Result.bSuccess
				          << ", definitions=" << Result.Definitions.size() << ", expected=" << Definition.Line << ':' << Definition.Column << '\n';
				for (const auto& Hit : Result.Definitions)
				{
					std::cerr << Hit.Name << " at " << Hit.DefinitionRange.StartLine << ':' << Hit.DefinitionRange.StartColumn << '\n';
				}
				return EXIT_FAILURE;
			}
		}
	}

	for (const std::string ArraySource : {
	         std::string("void Inspect(int values[2],int count){print(values);print(count);}\n"),
	         std::string("struct Holder{void Inspect(int values[2],int count);};\n"
	                     "void Holder::Inspect(int values[2],int count){print(values);print(count);}\n")})
	{
		for (const char* Name : {"values", "count"})
		{
			FTextPosition Position, Definition;
			const std::string Header = ArraySource.find("Holder::Inspect") == std::string::npos
			    ? "void Inspect("
			    : "void Holder::Inspect(";
			const std::string Declaration = Header + (std::string_view(Name) == "values" ? "int values" : "int values[2],int count");
			if (!Expect(FindPosition(ArraySource, std::string("print(") + Name + ")", Position) && FindPosition(ArraySource, Declaration, Definition),
			        "locate array parameter reference and declaration"))
			{
				return EXIT_FAILURE;
			}
			Position.Column += static_cast<std::uint32_t>(std::string_view("print(").size());
			Definition.Column += static_cast<std::uint32_t>(Declaration.size() - std::string_view(Name).size());
			const auto Result = VeroScriptNavigation::QueryAtSource(ArraySource, Position, "<array-parameter-definition>");
			if (!Expect(Result.bSuccess && Result.Definitions.size() == 1 && Result.Definitions.front().DefinitionRange.StartLine == Definition.Line && Result.Definitions.front().DefinitionRange.StartColumn == Definition.Column,
			        "postfix array syntax preserves this and following parameter definitions"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		const std::string Source =
		    "class Base{protected:Base(int value){}};\n"
		    "class Derived:public Base{int marker;public:Derived(int actual_value);};\n"
		    "Derived::Derived(int actual_value):Base(actual_value),marker(actual_value){}\n";
		FTextPosition Position, Definition;
		if (!Expect(FindPosition(Source, "marker(actual_value)", Position) && FindPosition(Source, "Derived::Derived(int actual_value)", Definition),
		        "locate constructor initializer reference and definition parameter"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("marker(").size());
		Definition.Column += static_cast<std::uint32_t>(std::string_view("Derived::Derived(int ").size());
		const auto Result = VeroScriptNavigation::QueryAtSource(Source, Position, "<constructor-parameter-definition>");
		if (!Expect(Result.bSuccess && Result.Definitions.size() == 1 && Result.Definitions.front().DefinitionRange.StartLine == Definition.Line && Result.Definitions.front().DefinitionRange.StartColumn == Definition.Column,
		        "initializer references navigate to the constructor header rather than another initializer"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* Source = R"(
struct Point {
	int x;
	int Sum() { return x; }
}

void Helper() {}

void main() {
	Point p = Point();
	Helper();
	int n = p.x;
	p.Sum();
}
)";

	{
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "Helper();", Position), "locate Helper() call"))
		{
			return EXIT_FAILURE;
		}
		// Caret on 'H' of Helper
		const FNavigationResult Result = VeroScriptNavigation::QueryAtSource(
		    Source,
		    Position,
		    "<nav>");
		if (!Expect(Result.bSuccess, "query Helper should succeed"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Result.bHasToken, "Helper call should hit a token"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Result.TokenText == "Helper", "token text should be Helper"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Result.Definitions.size() == 1, "Helper should resolve to one definition"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        Result.Definitions.front().Kind == EOutlineSymbolKind::Function,
		        "Helper definition should be Function"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        Result.Definitions.front().DefinitionRange.StartLine >= 1,
		        "Helper definition should have a line"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "Point p", Position), "locate Point type use"))
		{
			return EXIT_FAILURE;
		}
		const FNavigationResult Result = VeroScriptNavigation::QueryAtSource(
		    Source,
		    Position,
		    "<nav>");
		if (!Expect(Result.bSuccess && Result.bHasToken, "Point query should hit token"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Result.TokenText == "Point", "token should be Point"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Result.Definitions.size() >= 1, "Point should resolve"))
		{
			return EXIT_FAILURE;
		}
		bool bFoundStruct = false;
		for (const FNavigationHit& Hit : Result.Definitions)
		{
			if (Hit.Kind == EOutlineSymbolKind::Struct)
			{
				bFoundStruct = true;
			}
		}
		if (!Expect(bFoundStruct, "Point should resolve to Struct"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "Sum()", Position), "locate Sum method decl"))
		{
			return EXIT_FAILURE;
		}
		const FNavigationResult Result = VeroScriptNavigation::QueryAtSource(
		    Source,
		    Position,
		    "<nav>");
		if (!Expect(Result.bSuccess && Result.bHasToken, "Sum query should hit token"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Result.TokenText == "Sum", "token should be Sum"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Result.Definitions.size() >= 1, "Sum should resolve"))
		{
			return EXIT_FAILURE;
		}
		const FNavigationHit& Hit = Result.Definitions.front();
		if (!Expect(Hit.Kind == EOutlineSymbolKind::Method, "Sum should be Method"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Hit.ContainerName == "Point", "Sum container should be Point"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "p.x;", Position), "locate p.x use"))
		{
			return EXIT_FAILURE;
		}
		Position.Column = Position.Column + static_cast<std::uint32_t>(std::string_view("p.").size());
		const FNavigationResult Result = VeroScriptNavigation::QueryAtSource(
		    Source,
		    Position,
		    "<nav>");
		if (!Expect(Result.bSuccess && Result.bHasToken, "p.x query should hit token"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Result.TokenText == "x", "token should be x"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Result.Definitions.size() == 1, "p.x should resolve to one field"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        Result.Definitions.front().Kind == EOutlineSymbolKind::Field && Result.Definitions.front().ContainerName == "Point",
		        "p.x should resolve to Point.x"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "p.Sum()", Position), "locate p.Sum use"))
		{
			return EXIT_FAILURE;
		}
		Position.Column = Position.Column + static_cast<std::uint32_t>(std::string_view("p.").size());
		const FNavigationResult Result = VeroScriptNavigation::QueryAtSource(
		    Source,
		    Position,
		    "<nav>");
		if (!Expect(Result.bSuccess && Result.TokenText == "Sum", "p.Sum token"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        Result.Definitions.size() == 1 && Result.Definitions.front().Kind == EOutlineSymbolKind::Method && Result.Definitions.front().ContainerName == "Point",
		        "p.Sum should resolve to Point.Sum"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* ShadowSource = R"(
struct Point {
	int x;
}

void main() {
	int x = 1;
	Point p = Point();
	int n = p.x;
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(ShadowSource, "p.x;", Position), "locate shadowed p.x"))
		{
			return EXIT_FAILURE;
		}
		Position.Column = Position.Column + static_cast<std::uint32_t>(std::string_view("p.").size());
		const FNavigationResult Result = VeroScriptNavigation::QueryAtSource(
		    ShadowSource,
		    Position,
		    "<shadow>");
		if (!Expect(
		        Result.bSuccess && Result.Definitions.size() == 1 && Result.Definitions.front().Kind == EOutlineSymbolKind::Field && Result.Definitions.front().ContainerName == "Point",
		        "p.x should prefer field over local x"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "Point p", Position), "locate Point variable use again"))
		{
			return EXIT_FAILURE;
		}
		// Same line, column 1 (indent / line start) should not hit an identifier.
		Position.Column = 1;
		const FNavigationResult Result = VeroScriptNavigation::QueryAtSource(
		    Source,
		    Position,
		    "<nav>");
		if (!Expect(Result.bSuccess, "indent query should succeed"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!Result.bHasToken, "line start should not hit an identifier"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Result.Definitions.empty(), "line start should have no definitions"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const FNavigationResult Bad = VeroScriptNavigation::QueryAtSource(
		    "struct {",
		    FTextPosition{1, 1},
		    "<bad-nav>");
		if (!Expect(!Bad.bSuccess, "parse failure should fail navigation"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Bad.Diagnostics.size() == 1, "navigation failure should report diagnostic"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* LocalSource = R"(
int value = 1;

void Use(int count) {
	int total = count;
	print(total);
	print(value);
}

void main() {
	Use(2);
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(LocalSource, "print(total)", Position), "locate total use"))
		{
			return EXIT_FAILURE;
		}
		// Move caret onto 'total' inside print(total)
		Position.Column = Position.Column + static_cast<std::uint32_t>(std::string_view("print(").size());
		const FNavigationResult TotalResult = VeroScriptNavigation::QueryAtSource(
		    LocalSource,
		    Position,
		    "<local>");
		if (!Expect(TotalResult.bSuccess && TotalResult.bHasToken, "total query should hit token"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(TotalResult.TokenText == "total", "token should be total"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(TotalResult.Definitions.size() == 1, "total should resolve to one local"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        TotalResult.Definitions.front().Kind == EOutlineSymbolKind::Variable && TotalResult.Definitions.front().ContainerName == "Use",
		        "total should be a local in Use"))
		{
			return EXIT_FAILURE;
		}

		if (!Expect(FindPosition(LocalSource, "total = count", Position), "locate count use"))
		{
			return EXIT_FAILURE;
		}
		Position.Column = Position.Column + static_cast<std::uint32_t>(std::string_view("total = ").size());
		const FNavigationResult CountResult = VeroScriptNavigation::QueryAtSource(
		    LocalSource,
		    Position,
		    "<local>");
		if (!Expect(CountResult.bSuccess && CountResult.bHasToken, "count query should hit token"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(CountResult.TokenText == "count", "token should be count"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(CountResult.Definitions.size() == 1, "count should resolve to parameter"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        CountResult.Definitions.front().Detail.find("parameter") != std::string::npos && CountResult.Definitions.front().ContainerName == "Use",
		        "count should be parameter of Use"))
		{
			return EXIT_FAILURE;
		}

		if (!Expect(FindPosition(LocalSource, "print(value)", Position), "locate value use"))
		{
			return EXIT_FAILURE;
		}
		Position.Column = Position.Column + static_cast<std::uint32_t>(std::string_view("print(").size());
		const FNavigationResult ValueResult = VeroScriptNavigation::QueryAtSource(
		    LocalSource,
		    Position,
		    "<local>");
		if (!Expect(ValueResult.bSuccess && ValueResult.bHasToken, "value query should hit token"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(ValueResult.TokenText == "value", "token should be value"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(ValueResult.Definitions.size() >= 1, "value should resolve"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        ValueResult.Definitions.front().Kind == EOutlineSymbolKind::Variable && ValueResult.Definitions.front().ContainerName.empty(),
		        "value should resolve to global (no local shadow)"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* ScopeSource = R"(
void main() {
	int outer = 1;
	if (true) {
		int inner = 2;
		print(inner);
	}
	print(outer);
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(ScopeSource, "print(outer)", Position), "locate outer use"))
		{
			return EXIT_FAILURE;
		}
		Position.Column =
		    Position.Column + static_cast<std::uint32_t>(std::string_view("print(").size());
		const FNavigationResult OuterResult = VeroScriptNavigation::QueryAtSource(
		    ScopeSource,
		    Position,
		    "<scope>");
		if (!Expect(OuterResult.bSuccess && OuterResult.TokenText == "outer", "outer token"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        OuterResult.Definitions.size() == 1 && OuterResult.Definitions.front().Kind == EOutlineSymbolKind::Variable,
		        "outer should resolve to the outer local only"))
		{
			return EXIT_FAILURE;
		}

		if (!Expect(FindPosition(ScopeSource, "print(inner)", Position), "locate inner use"))
		{
			return EXIT_FAILURE;
		}
		Position.Column =
		    Position.Column + static_cast<std::uint32_t>(std::string_view("print(").size());
		const FNavigationResult InnerResult = VeroScriptNavigation::QueryAtSource(
		    ScopeSource,
		    Position,
		    "<scope>");
		if (!Expect(InnerResult.bSuccess && InnerResult.TokenText == "inner", "inner token"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        InnerResult.Definitions.size() == 1 && InnerResult.Definitions.front().DefinitionRange.StartLine != OuterResult.Definitions.front().DefinitionRange.StartLine,
		        "inner should resolve inside the if-block"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* InheritSource = R"(
struct Base {
	int baseField;
	int BaseSum() { return baseField; }
}

struct Derived : Base {
	int derivedField;
}

void main() {
	Derived d = Derived();
	int a = d.baseField;
	int b = d.BaseSum();
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(InheritSource, "d.baseField;", Position), "locate d.baseField"))
		{
			return EXIT_FAILURE;
		}
		Position.Column =
		    Position.Column + static_cast<std::uint32_t>(std::string_view("d.").size());
		const FNavigationResult FieldResult = VeroScriptNavigation::QueryAtSource(
		    InheritSource,
		    Position,
		    "<inherit>");
		if (!Expect(
		        FieldResult.bSuccess && FieldResult.Definitions.size() == 1 && FieldResult.Definitions.front().Kind == EOutlineSymbolKind::Field && FieldResult.Definitions.front().ContainerName == "Base",
		        "inherited field should resolve to Base.baseField"))
		{
			std::cerr << "success=" << FieldResult.bSuccess
			          << " defs=" << FieldResult.Definitions.size()
			          << " token=" << FieldResult.TokenText << '\n';
			for (const FDiagnostic& Diagnostic : FieldResult.Diagnostics)
			{
				std::cerr << " diag: " << Diagnostic.Message << '\n';
			}
			for (const FNavigationHit& Hit : FieldResult.Definitions)
			{
				std::cerr << " hit name=" << Hit.Name
				          << " container=[" << Hit.ContainerName << "]"
				          << " kind=" << static_cast<int>(Hit.Kind) << '\n';
			}
			return EXIT_FAILURE;
		}

		if (!Expect(FindPosition(InheritSource, "d.BaseSum()", Position), "locate d.BaseSum"))
		{
			return EXIT_FAILURE;
		}
		Position.Column =
		    Position.Column + static_cast<std::uint32_t>(std::string_view("d.").size());
		const FNavigationResult MethodResult = VeroScriptNavigation::QueryAtSource(
		    InheritSource,
		    Position,
		    "<inherit>");
		if (!Expect(
		        MethodResult.bSuccess && MethodResult.Definitions.size() == 1 && MethodResult.Definitions.front().Kind == EOutlineSymbolKind::Method && MethodResult.Definitions.front().ContainerName == "Base",
		        "inherited method should resolve to Base.BaseSum"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* InheritCompleteSource = R"(
struct Base {
	int baseField;
	int BaseSum() { return baseField; }
}

struct Derived : Base {
	int derivedField;
}

void main() {
	Derived d = Derived();
	d.
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(InheritCompleteSource, "\td.", Position), "locate d. completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\td.").size());
		const FCompletionResult CompleteResult = VeroScriptComplete::CompleteAtSource(
		    InheritCompleteSource,
		    Position,
		    "<inherit>");
		if (!Expect(CompleteResult.bSuccess, "inherited member completion"))
		{
			return EXIT_FAILURE;
		}
		bool bHasBaseField = false;
		bool bHasBaseSum = false;
		bool bHasDerivedField = false;
		for (const FCompletionItem& Item : CompleteResult.Items)
		{
			if (Item.Label == "baseField")
			{
				bHasBaseField = true;
			}
			if (Item.Label == "BaseSum")
			{
				bHasBaseSum = true;
			}
			if (Item.Label == "derivedField")
			{
				bHasDerivedField = true;
			}
		}
		if (!Expect(bHasBaseField && bHasBaseSum && bHasDerivedField, "completion lists base + derived"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* AliasNavSource = R"(
namespace Util {
	class Pack { public: int x;
		Pack(int v) { x = v; }
		int get() { return x; } }
}
using Pack = Util.Pack;

void main() {
	Pack p = Pack(1);
	p.get();
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(AliasNavSource, "Pack p", Position), "locate Pack type use"))
		{
			return EXIT_FAILURE;
		}
		const FNavigationResult AliasNav =
		    VeroScriptNavigation::QueryAtSource(AliasNavSource, Position, "<alias-nav>");
		if (!Expect(AliasNav.bSuccess && !AliasNav.Definitions.empty(), "goto Pack alias"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        AliasNav.Definitions[0].Kind == EOutlineSymbolKind::TypeAlias || AliasNav.Definitions[0].Name.find("Pack") != std::string::npos,
		        "Pack should resolve to alias or Util.Pack"))
		{
			std::cerr << "got kind/name Detail=" << AliasNav.Definitions[0].Name << '\n';
			return EXIT_FAILURE;
		}

		if (!Expect(FindPosition(AliasNavSource, "p.get()", Position), "locate p.get call"))
		{
			return EXIT_FAILURE;
		}
		// caret on 'g' of get
		Position.Column += static_cast<std::uint32_t>(std::string_view("p.").size());
		const FNavigationResult MemberNav =
		    VeroScriptNavigation::QueryAtSource(AliasNavSource, Position, "<alias-nav>");
		if (!Expect(
		        MemberNav.bSuccess && !MemberNav.Definitions.empty() && MemberNav.Definitions[0].Name == "get",
		        "goto get via alias-typed receiver"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* UsingNamespaceSource = R"(
namespace Library {
	struct Selected {}
	int calculate() { return 1; }
}
using namespace Library;
void main() {
	Selected chosen = Selected();
	calculate();
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(UsingNamespaceSource, "Selected chosen", Position),
		        "locate using namespace type use"))
		{
			return EXIT_FAILURE;
		}
		const FNavigationResult TypeResult = VeroScriptNavigation::QueryAtSource(
		    UsingNamespaceSource, Position, "<using-namespace-nav>");
		if (!Expect(
		        TypeResult.bSuccess && TypeResult.Definitions.size() == 1 && TypeResult.Definitions[0].Name == "Library.Selected",
		        "using namespace type navigation resolves the selected namespace"))
		{
			return EXIT_FAILURE;
		}

		if (!Expect(FindPosition(UsingNamespaceSource, "calculate();", Position),
		        "locate using namespace call"))
		{
			return EXIT_FAILURE;
		}
		const FNavigationResult CallResult = VeroScriptNavigation::QueryAtSource(
		    UsingNamespaceSource, Position, "<using-namespace-nav>");
		if (!Expect(
		        CallResult.bSuccess && CallResult.Definitions.size() == 1 && CallResult.Definitions[0].Name == "Library.calculate",
		        "using namespace function navigation resolves the selected namespace"))
		{
			return EXIT_FAILURE;
		}

		if (!Expect(FindPosition(UsingNamespaceSource, "Library;", Position),
		        "locate using namespace directive target"))
		{
			return EXIT_FAILURE;
		}
		const FNavigationResult NamespaceResult = VeroScriptNavigation::QueryAtSource(
		    UsingNamespaceSource, Position, "<using-namespace-target-nav>");
		if (!Expect(
		        NamespaceResult.bSuccess && !NamespaceResult.Definitions.empty() && NamespaceResult.Definitions[0].Kind == EOutlineSymbolKind::Namespace && NamespaceResult.Definitions[0].Name == "Library" && NamespaceResult.Definitions[0].DefinitionRange.StartLine == 2,
		        "using namespace target navigates to the namespace declaration"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* NoUsingSource = R"(
namespace Hidden { int hidden_call() { return 1; } }
void main() { hidden_call(); }
)";
		if (!Expect(FindPosition(NoUsingSource, "hidden_call();", Position),
		        "locate unimported namespace call"))
		{
			return EXIT_FAILURE;
		}
		const FNavigationResult NoUsingResult = VeroScriptNavigation::QueryAtSource(
		    NoUsingSource, Position, "<no-using-namespace-nav>");
		if (!Expect(
		        NoUsingResult.bSuccess && NoUsingResult.Definitions.empty(),
		        "bare navigation does not resolve unrelated namespace members"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* ForLocalSource = R"(
void main() {
	for (int i = 0; i < 1; i = i + 1) {
		int inside = 42;
		print(inside);
	}
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(ForLocalSource, "print(inside)", Position), "locate inside use"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("print(").size());
		const FNavigationResult ForNav =
		    VeroScriptNavigation::QueryAtSource(ForLocalSource, Position, "<for-local>");
		if (!Expect(
		        ForNav.bSuccess && !ForNav.Definitions.empty() && ForNav.Definitions[0].Name == "inside" && ForNav.Definitions[0].Kind == EOutlineSymbolKind::Variable,
		        "for-body local inside should resolve"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* LocalAliasSource = R"(
class Pack { public: int x;
	Pack(int v) { x = v; }
	int get() { return x; } }

void main() {
	using LocalPack = Pack;
	LocalPack p = LocalPack(1);
	p.get();
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(LocalAliasSource, "p.get()", Position), "locate local-alias get"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("p.").size());
		const FNavigationResult LocalAliasNav =
		    VeroScriptNavigation::QueryAtSource(LocalAliasSource, Position, "<local-alias>");
		if (!Expect(
		        LocalAliasNav.bSuccess && !LocalAliasNav.Definitions.empty() && LocalAliasNav.Definitions[0].Name == "get",
		        "goto get via local typedef receiver"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* GotoNavSource = R"(
void main() {
	int n = 0;
again:
	n = n + 1;
	goto again;
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(GotoNavSource, "goto again", Position), "locate goto again"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("goto ").size());
		const FNavigationResult GotoNav =
		    VeroScriptNavigation::QueryAtSource(GotoNavSource, Position, "<goto-nav>");
		if (!Expect(
		        GotoNav.bSuccess && !GotoNav.Definitions.empty() && GotoNav.Definitions[0].Name == "again" && GotoNav.Definitions[0].Kind == EOutlineSymbolKind::Label,
		        "goto again should resolve to label"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* OpNavSource = R"(
class Vec { public: int x;
	Vec(int v) { x = v; }
	Vec operator+(Vec other) { return Vec(x + other.x); } }
)";
		FTextPosition Position;
		if (!Expect(FindPosition(OpNavSource, "operator+", Position), "locate operator+"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("operator").size());
		const FNavigationResult OpNav =
		    VeroScriptNavigation::QueryAtSource(OpNavSource, Position, "<op-nav>");
		if (!Expect(
		        OpNav.bSuccess && OpNav.bHasToken && OpNav.TokenText == "operator+" && !OpNav.Definitions.empty() && OpNav.Definitions[0].Name == "operator+",
		        "operator+ caret should resolve method"))
		{
			std::cerr << "token=" << OpNav.TokenText
			          << " defs=" << OpNav.Definitions.size() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* GotoShadowSource = R"(
void main() {
	int again = 0;
again:
	again = again + 1;
	goto again;
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(GotoShadowSource, "goto again", Position), "locate shadowed goto"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("goto ").size());
		const FNavigationResult GotoShadow =
		    VeroScriptNavigation::QueryAtSource(GotoShadowSource, Position, "<goto-shadow>");
		if (!Expect(
		        GotoShadow.bSuccess && !GotoShadow.Definitions.empty() && GotoShadow.Definitions[0].Kind == EOutlineSymbolKind::Label && GotoShadow.Definitions[0].Name == "again",
		        "goto again should prefer label over local"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* AfterForSource = R"(
void main() {
	for (int i = 0; i < 1; i = i + 1) {
		int inside = 1;
	}
	int after = 2;
	print(after);
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(AfterForSource, "print(after)", Position), "locate after for"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("print(").size());
		const FNavigationResult AfterFor =
		    VeroScriptNavigation::QueryAtSource(AfterForSource, Position, "<after-for>");
		if (!Expect(
		        AfterFor.bSuccess && !AfterFor.Definitions.empty() && AfterFor.Definitions[0].Name == "after",
		        "after for should resolve outer local, not loop locals"))
		{
			return EXIT_FAILURE;
		}

		if (!Expect(FindPosition(AfterForSource, "print(after)", Position), "locate i misuse"))
		{
			return EXIT_FAILURE;
		}
		// Query identifier i on after-for line should not bind loop i as sole hit via local at after=
		FTextPosition AfterDecl;
		if (!Expect(FindPosition(AfterForSource, "int after", AfterDecl), "locate after decl"))
		{
			return EXIT_FAILURE;
		}
		const FCompletionResult CompleteAtAfter = VeroScriptComplete::CompleteAtSource(
		    AfterForSource,
		    AfterDecl,
		    "<after-for>");
		if (!Expect(CompleteAtAfter.bSuccess, "completion after for"))
		{
			return EXIT_FAILURE;
		}
		bool bHasInside = false;
		for (const FCompletionItem& Item : CompleteAtAfter.Items)
		{
			if (Item.Label == "inside")
			{
				bHasInside = true;
			}
		}
		if (!Expect(!bHasInside, "loop-body locals should not leak after for"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* MapDestructureSource = R"(
void main() {
	map<string, int> m = map<string, int>();
	m["a"] = 1;
	for ((string k, int& v) : m) {
		print(k);
		print(v);
	}
}
)";
		FTextPosition KeyPos;
		if (!Expect(FindPosition(MapDestructureSource, "print(k)", KeyPos), "locate print(k)"))
		{
			return EXIT_FAILURE;
		}
		KeyPos.Column += static_cast<std::uint32_t>(std::string_view("print(").size());
		const FNavigationResult KeyNav =
		    VeroScriptNavigation::QueryAtSource(MapDestructureSource, KeyPos, "<map-key>");
		if (!Expect(
		        KeyNav.bSuccess && !KeyNav.Definitions.empty() && KeyNav.Definitions[0].Name == "k",
		        "map destructure key k should resolve"))
		{
			return EXIT_FAILURE;
		}

		FTextPosition ValPos;
		if (!Expect(FindPosition(MapDestructureSource, "print(v)", ValPos), "locate print(v)"))
		{
			return EXIT_FAILURE;
		}
		ValPos.Column += static_cast<std::uint32_t>(std::string_view("print(").size());
		const FNavigationResult ValNav =
		    VeroScriptNavigation::QueryAtSource(MapDestructureSource, ValPos, "<map-val>");
		if (!Expect(
		        ValNav.bSuccess && !ValNav.Definitions.empty() && ValNav.Definitions[0].Name == "v" && ValNav.Definitions[0].Detail.find("int&") != std::string::npos,
		        "map destructure value v should resolve as reference"))
		{
			std::cerr << "detail=["
			          << (ValNav.Definitions.empty() ? "" : ValNav.Definitions[0].Detail)
			          << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* OpUseSource = R"(
class Vec { public: int x;
	Vec(int v) { x = v; }
	Vec operator+(Vec other) { return Vec(x + other.x); } }

void main() {
	Vec a = Vec(1);
	Vec b = Vec(2);
	Vec c = a + b;
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(OpUseSource, "a + b", Position), "locate a + b"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("a ").size());
		const FNavigationResult OpNav =
		    VeroScriptNavigation::QueryAtSource(OpUseSource, Position, "<op-use>");
		if (!Expect(
		        OpNav.bSuccess && OpNav.bHasToken && OpNav.TokenText == "operator+" && !OpNav.Definitions.empty(),
		        "use-site + should resolve to operator+"))
		{
			std::cerr << "token=[" << OpNav.TokenText << "] defs="
			          << OpNav.Definitions.size() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* OpMirrorNav = R"(
class Vec { public: int x;
	Vec(int v) { x = v; }
	bool operator==(Vec other) { return x == other.x; }
	bool operator<(Vec other) { return x < other.x; }
}
void main() {
	Vec a = Vec(1);
	Vec b = Vec(2);
	bool ne = a != b;
	bool gt = b > a;
}
)";
		FTextPosition NePos;
		if (!Expect(FindPosition(OpMirrorNav, "a != b", NePos), "locate != nav"))
		{
			return EXIT_FAILURE;
		}
		NePos.Column += static_cast<std::uint32_t>(std::string_view("a ").size());
		const FNavigationResult NeNav =
		    VeroScriptNavigation::QueryAtSource(OpMirrorNav, NePos, "<op-ne>");
		if (!Expect(
		        NeNav.bSuccess && NeNav.TokenText == "operator!=" && !NeNav.Definitions.empty() && NeNav.Definitions[0].Name == "operator==",
		        "!= use-site Go-to resolves operator=="))
		{
			std::cerr << "token=[" << NeNav.TokenText << "] defs=" << NeNav.Definitions.size();
			if (!NeNav.Definitions.empty())
			{
				std::cerr << " name=[" << NeNav.Definitions[0].Name << "]";
			}
			std::cerr << '\n';
			return EXIT_FAILURE;
		}
		FTextPosition GtPos;
		if (!Expect(FindPosition(OpMirrorNav, "b > a", GtPos), "locate > nav"))
		{
			return EXIT_FAILURE;
		}
		GtPos.Column += static_cast<std::uint32_t>(std::string_view("b ").size());
		const FNavigationResult GtNav =
		    VeroScriptNavigation::QueryAtSource(OpMirrorNav, GtPos, "<op-gt>");
		if (!Expect(
		        GtNav.bSuccess && GtNav.TokenText == "operator>" && !GtNav.Definitions.empty() && GtNav.Definitions[0].Name == "operator<",
		        "> use-site Go-to resolves operator<"))
		{
			std::cerr << "token=[" << GtNav.TokenText << "] defs=" << GtNav.Definitions.size();
			if (!GtNav.Definitions.empty())
			{
				std::cerr << " name=[" << GtNav.Definitions[0].Name << "]";
			}
			std::cerr << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* HostSource = R"(
void main() {
	print(1);
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(HostSource, "print(", Position), "locate print nav"))
		{
			return EXIT_FAILURE;
		}
		const FNavigationResult Nav =
		    VeroScriptNavigation::QueryAtSource(HostSource, Position, "<nav-print>");
		if (!Expect(
		        Nav.bSuccess && Nav.bHasToken && Nav.TokenText == "print" && Nav.Definitions.empty(),
		        "soft host print has no Go-to target"))
		{
			std::cerr << "defs=" << Nav.Definitions.size() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		std::error_code RemoveError;
		const std::filesystem::path TempRoot =
		    std::filesystem::temp_directory_path() / "VeroScriptTooling-import-nav";
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot / "lib");
		{
			std::ofstream Lib(TempRoot / "lib" / "math.vs");
			Lib << "export int add(int a, int b) { return a + b; }\n";
		}
		FNavigationOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		const std::string LibPath = (TempRoot / "lib").string();
		Options.ImportPaths.push_back(LibPath);
		for (const std::string ImportSource : {
		         std::string("import \"math.vs\";\nvoid main() { add(1, 2); }\n"),
		         std::string("#include \"math.vs\"\nvoid main() { add(1, 2); }\n")})
		{
			FTextPosition Position;
			if (!Expect(
			        FindPosition(ImportSource, "math.vs", Position),
			        "locate module import path"))
			{
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
			const FNavigationResult Nav = VeroScriptNavigation::QueryAtSource(
			    ImportSource,
			    Position,
			    (TempRoot / "app.vs").string(),
			    Options);
			if (!Expect(
			        Nav.bSuccess && Nav.bHasToken && !Nav.Definitions.empty() && Nav.Definitions.front().FileLabel.find("math.vs") != std::string::npos,
			        "module import path Go-to resolves math.vs"))
			{
				std::cerr << "token=[" << Nav.TokenText << "] defs="
				          << Nav.Definitions.size() << '\n';
				if (!Nav.Definitions.empty())
				{
					std::cerr << "file=[" << Nav.Definitions.front().FileLabel << "]\n";
				}
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
		}
		std::filesystem::remove_all(TempRoot, RemoveError);
	}

	// Built-in stdlib remains navigable from the module-owned Resources/Scripts/stdlib tree.
	{
		constexpr const char* ImportSource = "import \"math.vs\";\n";
		FTextPosition Position;
		if (!Expect(FindPosition(ImportSource, "math.vs", Position), "locate stdlib import path"))
		{
			return EXIT_FAILURE;
		}
		const FNavigationResult Nav = VeroScriptNavigation::QueryAtSource(
		    ImportSource,
		    Position,
		    "<stdlib-import-nav>");
		if (!Expect(
		        Nav.bSuccess && Nav.bHasToken && !Nav.Definitions.empty() && Nav.Definitions.front().FileLabel.find("math.vs") != std::string::npos,
		        "default stdlib import Go-to resolves math.vs"))
		{
			std::cerr << "token=[" << Nav.TokenText << "] defs=" << Nav.Definitions.size()
			          << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* EnumClassSource = R"(
enum class Color { Red, Green }
void main() {
	Color c = Color.Green;
	Color d = Color::Green;
	Green;
}
)";
		FTextPosition QualifiedPos;
		if (!Expect(
		        FindPosition(EnumClassSource, "Color.Green", QualifiedPos),
		        "locate Color.Green nav"))
		{
			return EXIT_FAILURE;
		}
		QualifiedPos.Column += static_cast<std::uint32_t>(std::string_view("Color.").size());
		const FNavigationResult Qualified =
		    VeroScriptNavigation::QueryAtSource(
		        EnumClassSource, QualifiedPos, "<nav-enum-class-qualified>");
		if (!Expect(
		        Qualified.bSuccess && Qualified.Definitions.size() == 1 && Qualified.Definitions.front().Kind == EOutlineSymbolKind::EnumMember && Qualified.Definitions.front().Name == "Green",
		        "qualified enum class member Go-to resolves"))
		{
			std::cerr << "qualified defs=" << Qualified.Definitions.size() << '\n';
			return EXIT_FAILURE;
		}

		FTextPosition ScopePos;
		if (!Expect(
		        FindPosition(EnumClassSource, "Color::Green", ScopePos),
		        "locate Color::Green nav"))
		{
			return EXIT_FAILURE;
		}
		ScopePos.Column += static_cast<std::uint32_t>(std::string_view("Color::").size());
		const FNavigationResult Scoped =
		    VeroScriptNavigation::QueryAtSource(
		        EnumClassSource, ScopePos, "<nav-enum-class-scope>");
		if (!Expect(
		        Scoped.bSuccess && Scoped.Definitions.size() == 1 && Scoped.Definitions.front().Kind == EOutlineSymbolKind::EnumMember && Scoped.Definitions.front().Name == "Green",
		        ":: qualified enum class member Go-to resolves"))
		{
			std::cerr << "scope defs=" << Scoped.Definitions.size() << '\n';
			return EXIT_FAILURE;
		}

		FTextPosition DeclPos;
		if (!Expect(FindPosition(EnumClassSource, "Green }", DeclPos), "locate Green decl nav"))
		{
			return EXIT_FAILURE;
		}
		const FNavigationResult Decl =
		    VeroScriptNavigation::QueryAtSource(
		        EnumClassSource, DeclPos, "<nav-enum-class-decl>");
		if (!Expect(
		        Decl.bSuccess && Decl.Definitions.size() == 1 && Decl.Definitions.front().Kind == EOutlineSymbolKind::EnumMember,
		        "declaration enum class member Go-to resolves"))
		{
			std::cerr << "decl defs=" << Decl.Definitions.size() << '\n';
			return EXIT_FAILURE;
		}

		FTextPosition BarePos;
		if (!Expect(FindPosition(EnumClassSource, "\tGreen;", BarePos), "locate bare Green nav"))
		{
			return EXIT_FAILURE;
		}
		BarePos.Column += 1;
		const FNavigationResult Bare =
		    VeroScriptNavigation::QueryAtSource(
		        EnumClassSource, BarePos, "<nav-enum-class-bare>");
		if (!Expect(
		        Bare.bSuccess && Bare.bHasToken && Bare.TokenText == "Green" && Bare.Definitions.empty(),
		        "bare enum class member Go-to stays unresolved"))
		{
			std::cerr << "bare defs=" << Bare.Definitions.size() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* CatchLocalSource = R"(void main()
{
	try { throw "x"; }
	catch (const string& message)
	{
		print(message);
	}
}
)";
		FTextPosition UsePosition;
		if (!Expect(
		        FindPosition(CatchLocalSource, "message);", UsePosition),
		        "locate catch-local use"))
		{
			return EXIT_FAILURE;
		}
		const FNavigationResult CatchLocal = VeroScriptNavigation::QueryAtSource(
		    CatchLocalSource, UsePosition, "<nav-catch-local>");
		if (!Expect(
		        CatchLocal.bSuccess && CatchLocal.bHasToken && CatchLocal.Definitions.size() == 1 && CatchLocal.Definitions.front().Name == "message" && CatchLocal.Definitions.front().DefinitionRange.StartLine == 4,
		        "catch-local Go-to resolves to the declared identifier"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* NamespaceSource = R"(
namespace A {
    namespace B {
        void Run() {}
    }
}
void main() { A::B::Run(); }
)";
		FTextPosition UsePosition;
		FTextPosition DeclarationPosition;
		if (!Expect(
		        FindPosition(NamespaceSource, "A::B::Run", UsePosition) && FindPosition(NamespaceSource, "namespace B", DeclarationPosition),
		        "locate user namespace navigation positions"))
		{
			return EXIT_FAILURE;
		}
		UsePosition.Column += static_cast<std::uint32_t>(std::string_view("A::").size());
		DeclarationPosition.Column += static_cast<std::uint32_t>(std::string_view("namespace ").size());
		const FNavigationResult NamespaceNavigation = VeroScriptNavigation::QueryAtSource(
		    NamespaceSource, UsePosition, "<nav-user-namespace>");
		if (!Expect(
		        NamespaceNavigation.bSuccess && NamespaceNavigation.Definitions.size() == 1 && NamespaceNavigation.Definitions.front().Kind == EOutlineSymbolKind::Namespace && NamespaceNavigation.Definitions.front().Name == "A.B" && NamespaceNavigation.Definitions.front().DefinitionRange.StartLine == DeclarationPosition.Line,
		        "user namespace Go-to-definition resolves the fully-qualified declaration"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// A standard namespace remains read-only, but when its owning stdlib
		// module is present on the configured search path Go-to-definition must
		// land on that real module instead of emitting a fabricated URI.
		const std::filesystem::path Root = std::filesystem::temp_directory_path() / ("veroscript-nav-standard-namespace-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())) / VeroScript::PathUtil::FromUtf8("\xE6\xA8\xA1\xE5\x9D\x97-\xF0\x9F\x98\x80");
		const std::filesystem::path Module = Root / "stdlib" / "algorithm.vs";
		std::error_code Error;
		std::filesystem::remove_all(Root, Error);
		std::filesystem::create_directories(Module.parent_path(), Error);
		if (Error)
		{
			return Fail("create standard namespace navigation fixture");
		}
		{
			std::ofstream Out(Module, std::ios::binary);
			Out << "// standard algorithm module\n";
		}

		constexpr const char* StandardSource =
		    "void main() { std::algorithm::sort(); }\n";
		FTextPosition AlgorithmPosition;
		if (!Expect(
		        FindPosition(StandardSource, "std::algorithm", AlgorithmPosition),
		        "locate standard namespace navigation position"))
		{
			std::filesystem::remove_all(Root, Error);
			return EXIT_FAILURE;
		}
		AlgorithmPosition.Column += static_cast<std::uint32_t>(std::string_view("std::").size());
		for (const bool bStdlibOnly : {false, true})
		{
			for (const auto& SearchRoot : {Root, Root / "stdlib"})
			{
				const std::string RootString = VeroScript::PathUtil::ToUtf8(SearchRoot);
				FNavigationOptions Options;
				Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
				(bStdlibOnly ? Options.StdlibSearchPaths : Options.ImportPaths) = {"", RootString};
				const FNavigationResult Navigation = VeroScriptNavigation::QueryAtSource(
				    StandardSource, AlgorithmPosition, "<nav-standard-namespace>", Options);
				if (!Expect(Navigation.bSuccess && Navigation.Definitions.size() == 1 && Navigation.Definitions.front().Kind == EOutlineSymbolKind::Namespace && Navigation.Definitions.front().Name == "std::algorithm" && Navigation.Definitions.front().FileLabel == VeroScript::PathUtil::ToUtf8(Module),
				        bStdlibOnly ? "standard namespace navigation uses UTF-8 stdlib-only roots"
				                    : "standard namespace navigation preserves UTF-8 import roots and file paths"))
				{
					std::filesystem::remove_all(Root.parent_path(), Error);
					return EXIT_FAILURE;
				}
			}
		}
		const auto OverrideRoot = Root / "override" / "stdlib";
		std::filesystem::create_directories(OverrideRoot, Error);
		{
			std::ofstream Out(OverrideRoot / "algorithm.vs", std::ios::binary);
			Out << "// alternate standard algorithm module\n";
		}
		const std::string FirstRoot = VeroScript::PathUtil::ToUtf8(Module.parent_path());
		const std::string LastRoot = VeroScript::PathUtil::ToUtf8(OverrideRoot);
		const std::string FirstRootAlias = FirstRoot + "/.";
		for (const auto& Roots : {
		         std::vector<std::string_view>{FirstRoot, LastRoot},
		         std::vector<std::string_view>{FirstRoot, LastRoot, FirstRoot},
		         std::vector<std::string_view>{FirstRootAlias, LastRoot, FirstRoot}})
		{
			FNavigationOptions Options;
			Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
			Options.StdlibSearchPaths = Roots;
			VeroScript::VeroScriptModuleLoader Loader("");
			for (const std::string_view SearchRoot : Roots)
			{
				Loader.AddStdlibSearchRoot(std::string(SearchRoot));
			}
			std::string Resolved;
			if (!Expect(Loader.ResolveImportPath("algorithm.vs", "", Resolved), "resolve stdlib priority fixture"))
			{
				std::filesystem::remove_all(Root.parent_path(), Error);
				return EXIT_FAILURE;
			}
			const auto StdlibPriority = VeroScriptNavigation::QueryAtSource(
			    StandardSource, AlgorithmPosition, "<nav-standard-namespace>", Options);
			Options.ImportPaths = {FirstRoot, LastRoot};
			const auto ImportPriority = VeroScriptNavigation::QueryAtSource(
			    StandardSource, AlgorithmPosition, "<nav-standard-namespace>", Options);
			if (!Expect(StdlibPriority.bSuccess && StdlibPriority.Definitions.size() == 1 && StdlibPriority.Definitions.front().FileLabel == Resolved && ImportPriority.bSuccess && ImportPriority.Definitions.size() == 1 && ImportPriority.Definitions.front().FileLabel == VeroScript::PathUtil::ToUtf8(Module),
			        "namespace navigation keeps ModuleLoader priority after normalized root deduplication"))
			{
				std::filesystem::remove_all(Root.parent_path(), Error);
				return EXIT_FAILURE;
			}
		}
		std::filesystem::remove_all(Root.parent_path(), Error);
	}

	{
		constexpr const char* ReferenceOperatorSource = R"(
class Cell { public: int value = 0;
    int& operator[](const int& index) { return value; }
    Cell& operator=(const int& next) { value = next; return this; }
};
void main() { Cell cell = Cell(); cell[0] = 3; cell = 7; }
)";
		for (const auto& Test : {std::pair{"cell[0]", "operator[]"}, std::pair{"cell = 7", "operator="}})
		{
			FTextPosition Position;
			if (!Expect(FindPosition(ReferenceOperatorSource, Test.first, Position), "locate reference operator use")) return EXIT_FAILURE;
			Position.Column += std::string_view(Test.first) == "cell[0]" ? 4u : 5u;
			const FNavigationResult Navigation = VeroScriptNavigation::QueryAtSource(ReferenceOperatorSource, Position, "<operator-reference>");
			if (!Expect(Navigation.bSuccess && !Navigation.Definitions.empty() && Navigation.Definitions.front().Name == Test.second, "navigate reference operator and assignment"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		const std::string Source = "template<typename T,int Count=3> struct Box{T data[Count];template<typename U,int Amount=Count> U echo(U value){return value;}};struct Other{int Count;};void main(){Box<int> item=Box<int>();}";
		for (const auto* Needle : {"Count]", "U echo"})
		{
			FTextPosition Position;
			if (!FindPosition(Source, Needle, Position)) return EXIT_FAILURE;
			const auto Result = VeroScriptNavigation::QueryAtSource(Source, Position, "template-navigation.vs");
			if (!Expect(Result.bSuccess && Result.Definitions.size() == 1,
			        "mixed and method template parameters navigate to their own declarations"))
			{
				return EXIT_FAILURE;
			}
		}
	}
	return EXIT_SUCCESS;
}
