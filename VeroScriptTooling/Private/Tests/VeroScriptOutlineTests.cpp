// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptToolingTestVm.h"
#include "VeroScriptTooling.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
	int Fail(const char* Message)
	{
		std::cerr << "VeroScriptOutlineTests: " << Message << '\n';
		return EXIT_FAILURE;
	}

	bool Expect(const bool Condition, const char* Message)
	{
		return Condition ? true : (Fail(Message), false);
	}

	const VeroScript::Tooling::FOutlineSymbol* FindByName(
	    const std::vector<VeroScript::Tooling::FOutlineSymbol>& Symbols,
	    const std::string& Name)
	{
		for (const VeroScript::Tooling::FOutlineSymbol& Symbol : Symbols)
		{
			if (Symbol.Name == Name)
			{
				return &Symbol;
			}
		}
		return nullptr;
	}
}

int main()
{
	using namespace VeroScript::Tooling;
	{
		const auto Result = VeroScriptOutline::CollectFromSource(
		    "struct Record{int value;bool operator==(const Record& other)const=default;};",
		    "<defaulted-method-outline>");
		const auto* Type = FindByName(Result.Symbols, "Record");
		bool bDefaulted = false;
		if (Type)
		{
			for (const auto& Child : Type->Children)
			{
				bDefaulted |= Child.Kind == EOutlineSymbolKind::Method && Child.Name == "operator==" && Child.Detail.find("default") != std::string::npos;
			}
		}
		if (!Expect(Result.bSuccess && bDefaulted, "outline retains defaulted method state")) return EXIT_FAILURE;
	}

	{
		const auto Constants = VeroScriptOutline::CollectFromSource(
		    "constexpr int twice(int n){return n*2;}constexpr int count=twice(3);void main(){constexpr int local=count;}", "<constexpr-outline>");
		const auto* Value = FindByName(Constants.Symbols, "count");
		if (!Expect(Constants.bSuccess && Value && Value->Detail.find("constexpr") != std::string::npos,
		        "outline preserves constexpr declaration qualification"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* SelfContained = R"(
enum Color { Red, Green }

struct Point {
	int x;
	int y;
	Point(int x, int y) {}
	int Sum() { return x + y; }
}

class Counter { public: int value;
	void Inc() { value = value + 1; } }

interface Drawable {
	virtual void Draw() = 0;
}

const int Max = 10;
int Score = 0;

export void Helper() {}

void main() {}
)";

	{
		const FOutlineResult Result = VeroScriptOutline::CollectFromSource(SelfContained, "<outline>");
		if (!Expect(Result.bSuccess, "self-contained outline should succeed"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Result.Diagnostics.empty(), "successful outline should have no diagnostics"))
		{
			return EXIT_FAILURE;
		}

		const FOutlineSymbol* Color = FindByName(Result.Symbols, "Color");
		if (!Expect(Color != nullptr, "outline should include enum Color"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Color->Kind == EOutlineSymbolKind::Enum, "Color should be Enum"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Color->Children.size() == 2, "Color should have two members"))
		{
			return EXIT_FAILURE;
		}

		const FOutlineSymbol* Point = FindByName(Result.Symbols, "Point");
		if (!Expect(Point != nullptr, "outline should include struct Point"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Point->Kind == EOutlineSymbolKind::Struct, "Point should be Struct"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Point->Children.size() >= 3, "Point should list fields/ctor/method"))
		{
			return EXIT_FAILURE;
		}
		bool bHasSum = false;
		bool bHasCtor = false;
		for (const FOutlineSymbol& Child : Point->Children)
		{
			if (Child.Kind == EOutlineSymbolKind::Method && Child.Name == "Sum")
			{
				bHasSum = true;
			}
			if (Child.Kind == EOutlineSymbolKind::Constructor && Child.Name == "Point")
			{
				bHasCtor = true;
			}
		}
		if (!Expect(bHasSum && bHasCtor, "Point children should include Sum and constructor"))
		{
			return EXIT_FAILURE;
		}

		const FOutlineSymbol* Counter = FindByName(Result.Symbols, "Counter");
		if (!Expect(Counter != nullptr && Counter->Kind == EOutlineSymbolKind::Class, "class Counter"))
		{
			return EXIT_FAILURE;
		}

		const FOutlineSymbol* Drawable = FindByName(Result.Symbols, "Drawable");
		if (!Expect(
		        Drawable != nullptr && Drawable->Kind == EOutlineSymbolKind::Interface,
		        "interface Drawable"))
		{
			return EXIT_FAILURE;
		}

		const FOutlineSymbol* Max = FindByName(Result.Symbols, "Max");
		if (!Expect(Max != nullptr && Max->Kind == EOutlineSymbolKind::Constant, "const Max"))
		{
			return EXIT_FAILURE;
		}

		const FOutlineSymbol* Score = FindByName(Result.Symbols, "Score");
		if (!Expect(Score != nullptr && Score->Kind == EOutlineSymbolKind::Variable, "var Score"))
		{
			return EXIT_FAILURE;
		}

		const FOutlineSymbol* Helper = FindByName(Result.Symbols, "Helper");
		if (!Expect(
		        Helper != nullptr && Helper->Kind == EOutlineSymbolKind::Function && Helper->bExported,
		        "export Helper"))
		{
			return EXIT_FAILURE;
		}

		const FOutlineSymbol* Main = FindByName(Result.Symbols, "main");
		if (!Expect(Main != nullptr && Main->Kind == EOutlineSymbolKind::Function, "function main"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const FOutlineResult Bad = VeroScriptOutline::CollectFromSource(
		    "struct {",
		    "<bad-outline>");
		if (!Expect(!Bad.bSuccess, "parse failure should fail outline"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Bad.Diagnostics.size() == 1, "outline failure should report one diagnostic"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Bad.Symbols.empty(), "failed outline should not return symbols"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* Concepts = R"(
interface IId {
	virtual int id() = 0;
}

interface IName {
	virtual string name() = 0;
}

class Person : public IId, public IName {
	int id() { return 1; }
	string name() { return "a"; }
}

string describe<T>(T x) where T : IId, IName {
	return x.name();
}

class TagBox<T> where T : IId { public: T item; }
)";
		const FOutlineResult Result = VeroScriptOutline::CollectFromSource(Concepts, "<concepts>");
		if (!Expect(Result.bSuccess, "concepts outline should succeed"))
		{
			return EXIT_FAILURE;
		}
		const FOutlineSymbol* Person = FindByName(Result.Symbols, "Person");
		if (!Expect(Person != nullptr && Person->Kind == EOutlineSymbolKind::Class, "Person class"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        Person->Detail.find("IId") != std::string::npos && Person->Detail.find("IName") != std::string::npos,
		        "Person detail should list implemented interfaces"))
		{
			std::cerr << "Person.Detail=[" << Person->Detail << "]\n";
			return EXIT_FAILURE;
		}

		const FOutlineSymbol* Describe = FindByName(Result.Symbols, "describe<T>");
		if (!Expect(Describe != nullptr, "describe<T> function"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        Describe->Detail.find("where T : IId") != std::string::npos && Describe->Detail.find("IName") != std::string::npos,
		        "describe detail should include where-bounds"))
		{
			std::cerr << "describe.Detail=[" << Describe->Detail << "]\n";
			return EXIT_FAILURE;
		}

		const FOutlineSymbol* TagBox = FindByName(Result.Symbols, "TagBox<T>");
		if (!Expect(TagBox != nullptr && TagBox->Kind == EOutlineSymbolKind::Class, "TagBox<T>"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        TagBox->Detail.find("where T : IId") != std::string::npos,
		        "TagBox detail should include where-bounds"))
		{
			std::cerr << "TagBox.Detail=[" << TagBox->Detail << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* AliasAndNamespace = R"(
namespace Util {
	export class Pack { public: int x;
		Pack(int v) { x = v; }
		int get() { return x; } }
}

using Pack = Util.Pack;
typedef IntAlias = int;

void take(int... values) {}
)";
		const FOutlineResult Result =
		    VeroScriptOutline::CollectFromSource(AliasAndNamespace, "<alias-ns>");
		if (!Expect(Result.bSuccess, "alias/namespace outline should succeed"))
		{
			return EXIT_FAILURE;
		}

		const FOutlineSymbol* PackType = FindByName(Result.Symbols, "Util.Pack");
		if (!Expect(
		        PackType != nullptr && PackType->Kind == EOutlineSymbolKind::Class,
		        "namespace class Util.Pack"))
		{
			return EXIT_FAILURE;
		}

		const FOutlineSymbol* PackAlias = FindByName(Result.Symbols, "Pack");
		if (!Expect(
		        PackAlias != nullptr && PackAlias->Kind == EOutlineSymbolKind::TypeAlias && PackAlias->Detail == "Util.Pack",
		        "using Pack = Util.Pack"))
		{
			return EXIT_FAILURE;
		}

		const FOutlineSymbol* IntAlias = FindByName(Result.Symbols, "IntAlias");
		if (!Expect(
		        IntAlias != nullptr && IntAlias->Kind == EOutlineSymbolKind::TypeAlias && IntAlias->Detail == "int",
		        "typedef IntAlias"))
		{
			return EXIT_FAILURE;
		}

		const FOutlineSymbol* Take = FindByName(Result.Symbols, "take");
		if (!Expect(Take != nullptr, "take function"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        Take->Detail.find("int... values") != std::string::npos,
		        "variadic param formatting"))
		{
			std::cerr << "take.Detail=[" << Take->Detail << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* Operators = R"(
class Vec { public: int x;
	Vec(int v) { x = v; }
	Vec operator+(Vec other) { return Vec(x + other.x); }
	int operator[](int i) { return x + i; }
	bool operator bool() { return x != 0; } }

Vec operator+(int n, Vec v) { return Vec(n + v.x); }
)";
		const FOutlineResult Result =
		    VeroScriptOutline::CollectFromSource(Operators, "<operators>");
		if (!Expect(Result.bSuccess, "operators outline should succeed"))
		{
			return EXIT_FAILURE;
		}

		const FOutlineSymbol* Vec = FindByName(Result.Symbols, "Vec");
		if (!Expect(Vec != nullptr, "Vec class"))
		{
			return EXIT_FAILURE;
		}
		bool bHasPlus = false;
		bool bHasIndex = false;
		bool bHasConv = false;
		for (const FOutlineSymbol& Child : Vec->Children)
		{
			if (Child.Kind == EOutlineSymbolKind::Method && Child.Name == "operator+")
			{
				bHasPlus = true;
			}
			if (Child.Kind == EOutlineSymbolKind::Method && Child.Name == "operator[]")
			{
				bHasIndex = true;
			}
			if (Child.Kind == EOutlineSymbolKind::Method && Child.Name == "operator bool")
			{
				bHasConv = true;
			}
		}
		if (!Expect(bHasPlus && bHasIndex, "Vec should outline operator+ and operator[]"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(bHasConv, "conversion operator displays as operator bool"))
		{
			return EXIT_FAILURE;
		}

		const FOutlineSymbol* FreePlus = FindByName(Result.Symbols, "operator+");
		if (!Expect(
		        FreePlus != nullptr && FreePlus->Kind == EOutlineSymbolKind::Function,
		        "free operator+"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* GotoLabels = R"(
void main() {
	int n = 0;
again:
	n = n + 1;
	if (n >= 3) {
		return;
	}
	goto again;
}
)";
		const FOutlineResult Result =
		    VeroScriptOutline::CollectFromSource(GotoLabels, "<goto>");
		if (!Expect(Result.bSuccess, "goto outline should succeed"))
		{
			return EXIT_FAILURE;
		}
		const FOutlineSymbol* Main = FindByName(Result.Symbols, "main");
		if (!Expect(Main != nullptr, "main function"))
		{
			return EXIT_FAILURE;
		}
		bool bHasAgain = false;
		for (const FOutlineSymbol& Child : Main->Children)
		{
			if (Child.Kind == EOutlineSymbolKind::Label && Child.Name == "again")
			{
				bHasAgain = true;
			}
		}
		if (!Expect(bHasAgain, "main should outline label again"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* ConstMethod = R"(
class Box { public: int n;
	int get() const { return n; } }
)";
		const FOutlineResult Result =
		    VeroScriptOutline::CollectFromSource(ConstMethod, "<const-method>");
		if (!Expect(Result.bSuccess, "const method outline should succeed"))
		{
			return EXIT_FAILURE;
		}
		const FOutlineSymbol* Box = FindByName(Result.Symbols, "Box");
		if (!Expect(Box != nullptr, "Box class"))
		{
			return EXIT_FAILURE;
		}
		bool bHasConstGet = false;
		for (const FOutlineSymbol& Child : Box->Children)
		{
			if (Child.Kind == EOutlineSymbolKind::Method && Child.Name == "get" && Child.Detail.find(" const") != std::string::npos)
			{
				bHasConstGet = true;
			}
		}
		if (!Expect(bHasConstGet, "get() const should appear in method detail"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* DefaultDelete = R"(
struct Token {
	Token() = default;
	Token(string text) = delete;
	~Token() = default;
	int removed(int value) = delete;
}
int removed(int value) = delete;
void main() {}
)";
		const FOutlineResult Result =
		    VeroScriptOutline::CollectFromSource(DefaultDelete, "<default-delete>");
		if (!Expect(Result.bSuccess, "default/delete outline should succeed"))
		{
			return EXIT_FAILURE;
		}
		const FOutlineSymbol* Token = FindByName(Result.Symbols, "Token");
		if (!Expect(Token != nullptr, "Token struct"))
		{
			return EXIT_FAILURE;
		}
		bool bHasDefaultCtor = false;
		bool bHasDeletedCtor = false;
		bool bHasDefaultDtor = false;
		bool bHasDeletedMethod = false;
		for (const FOutlineSymbol& Child : Token->Children)
		{
			bHasDefaultCtor = bHasDefaultCtor || (Child.Kind == EOutlineSymbolKind::Constructor && Child.Detail.find("default") != std::string::npos);
			bHasDeletedCtor = bHasDeletedCtor || (Child.Kind == EOutlineSymbolKind::Constructor && Child.Detail.find("delete") != std::string::npos);
			bHasDefaultDtor = bHasDefaultDtor || (Child.Kind == EOutlineSymbolKind::Destructor && Child.Detail.find("default") != std::string::npos);
			bHasDeletedMethod = bHasDeletedMethod || (Child.Kind == EOutlineSymbolKind::Method && Child.Name == "removed" && Child.Detail.find("delete") != std::string::npos);
		}
		if (!Expect(
		        bHasDefaultCtor && bHasDeletedCtor && bHasDefaultDtor && bHasDeletedMethod,
		        "outline should expose default/delete declaration state"))
		{
			return EXIT_FAILURE;
		}
		const FOutlineSymbol* Removed = FindByName(Result.Symbols, "removed");
		if (!Expect(
		        Removed != nullptr && Removed->Kind == EOutlineSymbolKind::Function && Removed->Detail.find("delete") != std::string::npos,
		        "deleted free function outline detail"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* LambdaLabel = R"(
void main() {
	fn()->void f = fn() -> void {
	inner:
		return;
	};
	fn()->void first = fn() -> void {
	group_first:
		return;
	}, second = fn() -> void {
	group_second:
		return;
	};
	do {
	loop_body:
		break;
	} while (false);
}
)";
		const FOutlineResult Result =
		    VeroScriptOutline::CollectFromSource(LambdaLabel, "<lambda-label>");
		if (!Expect(Result.bSuccess, "lambda label outline should succeed"))
		{
			if (!Result.Diagnostics.empty())
			{
				std::cerr << Result.Diagnostics.front().Message << '\n';
			}
			return EXIT_FAILURE;
		}
		const FOutlineSymbol* Main = FindByName(Result.Symbols, "main");
		if (!Expect(Main != nullptr, "main for lambda label"))
		{
			return EXIT_FAILURE;
		}
		bool bHasInner = false;
		bool bHasGroupFirst = false;
		bool bHasGroupSecond = false;
		bool bHasLoopBody = false;
		for (const FOutlineSymbol& Child : Main->Children)
		{
			if (Child.Kind == EOutlineSymbolKind::Label && Child.Name == "inner")
			{
				bHasInner = true;
			}
			else if (Child.Kind == EOutlineSymbolKind::Label && Child.Name == "group_first")
			{
				bHasGroupFirst = true;
			}
			else if (Child.Kind == EOutlineSymbolKind::Label && Child.Name == "group_second")
			{
				bHasGroupSecond = true;
			}
			else if (Child.Kind == EOutlineSymbolKind::Label && Child.Name == "loop_body")
			{
				bHasLoopBody = true;
			}
		}
		if (!Expect(bHasInner, "label inside lambda body should outline under main") || !Expect(bHasGroupFirst && bHasGroupSecond, "labels inside grouped declaration initializers should outline under main") || !Expect(bHasLoopBody, "label inside do-while body should outline under main"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* LocalTypedef = R"(
void main() {
	typedef Num = int;
	Num n = 1;
}
)";
		const FOutlineResult Result =
		    VeroScriptOutline::CollectFromSource(LocalTypedef, "<local-typedef>");
		if (!Expect(Result.bSuccess, "local typedef outline"))
		{
			if (!Result.Diagnostics.empty())
			{
				std::cerr << Result.Diagnostics.front().Message << '\n';
			}
			return EXIT_FAILURE;
		}
		const FOutlineSymbol* Main = FindByName(Result.Symbols, "main");
		if (!Expect(Main != nullptr, "main for local typedef"))
		{
			return EXIT_FAILURE;
		}
		bool bHasNum = false;
		for (const FOutlineSymbol& Child : Main->Children)
		{
			if (Child.Kind == EOutlineSymbolKind::TypeAlias && Child.Name == "Num")
			{
				bHasNum = true;
			}
		}
		if (!Expect(bHasNum, "local typedef Num under main"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* MultiParentIface = R"(
interface IA { virtual void a() = 0; }
interface IB { virtual void b() = 0; }
interface IC : IA, IB { virtual void c() = 0; }
)";
		const FOutlineResult Result =
		    VeroScriptOutline::CollectFromSource(MultiParentIface, "<mp-iface>");
		if (!Expect(Result.bSuccess, "multi-parent interface outline"))
		{
			return EXIT_FAILURE;
		}
		const FOutlineSymbol* IC = FindByName(Result.Symbols, "IC");
		if (!Expect(IC != nullptr && IC->Kind == EOutlineSymbolKind::Interface, "outline IC"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        IC->Detail.find("IA") != std::string::npos && IC->Detail.find("IB") != std::string::npos,
		        "IC outline detail lists both parents"))
		{
			std::cerr << "Detail=[" << IC->Detail << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* MacroPad = R"(
#define PAD \
    int pad = 0;

void main() {
    PAD
}
)";
		const FOutlineResult Result =
		    VeroScriptOutline::CollectFromSource(MacroPad, "<macro-outline>");
		if (!Expect(Result.bSuccess, "macro-padded outline should succeed"))
		{
			return EXIT_FAILURE;
		}
		const FOutlineSymbol* Main = FindByName(Result.Symbols, "main");
		if (!Expect(Main != nullptr, "main in macro-padded outline"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Main->Range.StartLine == 5, "main outline line is physical not preprocessed"))
		{
			std::cerr << "main line=" << Main->Range.StartLine << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* MacroBadOutline = R"(
#define PAD
void pad() {}
void main() { print(; }
)";
		const FOutlineResult BadResult =
		    VeroScriptOutline::CollectFromSource(MacroBadOutline, "<macro-outline-bad>");
		if (!Expect(!BadResult.bSuccess, "macro parse error outline should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(BadResult.Diagnostics.size() >= 1, "macro outline failure has diagnostic"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        BadResult.Diagnostics.front().Range.StartLine == 4,
		        "macro outline diagnostic line is physical"))
		{
			std::cerr << "line=" << BadResult.Diagnostics.front().Range.StartLine << '\n';
			return EXIT_FAILURE;
		}
	}

	// 1.0 language surface: final types/methods, destructors, interface defaults/fields.
	{
		constexpr const char* SealedSurface = R"(
interface IState final {
	int value = 1;
	virtual int peek() = 0;
	int read() { return this.value; }
	virtual int get() final { return this.value; }
}

class Owner final {
public:
	virtual ~Owner() {}
	virtual int get() final { return 2; }
}
)";
		const FOutlineResult Result =
		    VeroScriptOutline::CollectFromSource(SealedSurface, "<sealed-surface>");
		if (!Expect(Result.bSuccess, "sealed language surface outline"))
		{
			return EXIT_FAILURE;
		}
		const FOutlineSymbol* IState = FindByName(Result.Symbols, "IState");
		const FOutlineSymbol* Owner = FindByName(Result.Symbols, "Owner");
		if (!Expect(
		        IState && IState->Detail.find("final") != std::string::npos,
		        "final interface detail"))
		{
			return EXIT_FAILURE;
		}
		const FOutlineSymbol* Value = FindByName(IState->Children, "value");
		const FOutlineSymbol* Peek = FindByName(IState->Children, "peek");
		const FOutlineSymbol* Read = FindByName(IState->Children, "read");
		const FOutlineSymbol* InterfaceGet = FindByName(IState->Children, "get");
		if (!Expect(
		        Value && Value->Kind == EOutlineSymbolKind::Field && Peek && Peek->Detail.find("abstract") != std::string::npos && Read && Read->Detail.find("default") != std::string::npos && Read->Detail.find("virtual") == std::string::npos && InterfaceGet && InterfaceGet->Detail.find("virtual") != std::string::npos && InterfaceGet->Detail.find("final") != std::string::npos,
		        "interface field / abstract / static-default / virtual-final detail"))
		{
			const auto PrintDetail = [](const char* const Label, const FOutlineSymbol* const Symbol)
			{
				std::cerr << Label << '='
				          << (Symbol ? Symbol->Detail : std::string("<missing>")) << '\n';
			};
			PrintDetail("value", Value);
			PrintDetail("peek", Peek);
			PrintDetail("read", Read);
			PrintDetail("get", InterfaceGet);
			return EXIT_FAILURE;
		}
		if (!Expect(
		        Owner && Owner->Detail.find("final") != std::string::npos,
		        "final class detail"))
		{
			return EXIT_FAILURE;
		}
		const FOutlineSymbol* Destructor = FindByName(Owner->Children, "~Owner");
		const FOutlineSymbol* OwnerGet = FindByName(Owner->Children, "get");
		if (!Expect(
		        Destructor && Destructor->Kind == EOutlineSymbolKind::Destructor && Destructor->Detail.find("virtual") != std::string::npos && OwnerGet && OwnerGet->Detail.find("virtual") != std::string::npos && OwnerGet->Detail.find("final") != std::string::npos,
		        "destructor and final method outline detail"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* Source = R"(
namespace Library { struct Point {} }
using namespace Library;
void main() {}
)";
		const FOutlineResult Result =
		    VeroScriptOutline::CollectFromSource(Source, "<using-namespace-outline>");
		const FOutlineSymbol* Directive = FindByName(Result.Symbols, "Library");
		if (!Expect(
		        Result.bSuccess && Directive && Directive->Kind == EOutlineSymbolKind::Import && Directive->Detail == "using namespace",
		        "using namespace appears as an import in the outline"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* Annotated = R"(
UINTERFACE()
interface IDamageable {
	@rpc(reliable)
	virtual void Apply(int Delta) = 0;
}
UCLASS()
class CombatActor : public IDamageable {
public:
	UPROPERTY(EditAnywhere, Category="Combat", meta=(ClampMin="0"))
	int Amount = 0;
	UFUNCTION(BlueprintCallable)
	CombatActor() {}
	@cleanup()
	~CombatActor() {}
	void Apply(int Delta) { Amount = Amount + Delta; }
}
UENUM(BlueprintType)
enum class EDamageType {
	UMETA(DisplayName="Physical Damage")
	Physical,
	Fire = 2
}
USTRUCT(BlueprintType)
struct FHitInfo { int Power; }
void main() { print(1); }
)";
		const FOutlineResult Result =
		    VeroScriptOutline::CollectFromSource(Annotated, "<decl-meta>");
		if (!Expect(Result.bSuccess, "annotated outline should succeed"))
		{
			return EXIT_FAILURE;
		}
		const FOutlineSymbol* Combat = FindByName(Result.Symbols, "CombatActor");
		if (!Expect(
		        Combat && Combat->Detail.find("[UCLASS()]") != std::string::npos,
		        "class Detail includes UCLASS annotation"))
		{
			std::cerr << "Combat Detail=[" << (Combat ? Combat->Detail : "") << "]\n";
			return EXIT_FAILURE;
		}
		const FOutlineSymbol* Amount = FindByName(Combat->Children, "Amount");
		if (!Expect(
		        Amount && Amount->Detail.find("[UPROPERTY(") != std::string::npos && Amount->Detail.find("EditAnywhere") != std::string::npos && Amount->Detail.find("Category=Combat") != std::string::npos && Amount->Detail.find("meta=(ClampMin=0)") != std::string::npos,
		        "field Detail includes UPROPERTY args"))
		{
			std::cerr << "Amount Detail=[" << (Amount ? Amount->Detail : "") << "]\n";
			return EXIT_FAILURE;
		}
		const FOutlineSymbol* Ctor = FindByName(Combat->Children, "CombatActor");
		if (!Expect(
		        Ctor && Ctor->Kind == EOutlineSymbolKind::Constructor && Ctor->Detail.find("[UFUNCTION(BlueprintCallable)]") != std::string::npos,
		        "ctor Detail includes UFUNCTION"))
		{
			std::cerr << "Ctor Detail=[" << (Ctor ? Ctor->Detail : "") << "]\n";
			return EXIT_FAILURE;
		}
		const FOutlineSymbol* Dtor = FindByName(Combat->Children, "~CombatActor");
		if (!Expect(
		        Dtor && Dtor->Kind == EOutlineSymbolKind::Destructor && Dtor->Detail.find("[@cleanup()]") != std::string::npos,
		        "dtor Detail includes @cleanup"))
		{
			std::cerr << "Dtor Detail=[" << (Dtor ? Dtor->Detail : "") << "]\n";
			return EXIT_FAILURE;
		}
		const FOutlineSymbol* Iface = FindByName(Result.Symbols, "IDamageable");
		if (!Expect(
		        Iface && Iface->Detail.find("[UINTERFACE()]") != std::string::npos,
		        "interface Detail includes UINTERFACE"))
		{
			std::cerr << "Iface Detail=[" << (Iface ? Iface->Detail : "") << "]\n";
			return EXIT_FAILURE;
		}
		const FOutlineSymbol* Damage = FindByName(Result.Symbols, "EDamageType");
		if (!Expect(
		        Damage && Damage->Detail.find("enum class") == 0 && Damage->Detail.find("[UENUM(BlueprintType)]") != std::string::npos,
		        "enum class Detail includes scoped kind and UENUM"))
		{
			std::cerr << "Damage Detail=[" << (Damage ? Damage->Detail : "") << "]\n";
			return EXIT_FAILURE;
		}
		const FOutlineSymbol* Physical = FindByName(Damage->Children, "Physical");
		if (!Expect(
		        Physical && Physical->Detail.find("[UMETA(DisplayName=Physical Damage)]") != std::string::npos,
		        "enum member Detail includes UMETA"))
		{
			std::cerr << "Physical Detail=[" << (Physical ? Physical->Detail : "") << "]\n";
			return EXIT_FAILURE;
		}
		const FOutlineSymbol* Hit = FindByName(Result.Symbols, "FHitInfo");
		if (!Expect(
		        Hit && Hit->Detail.find("[USTRUCT(BlueprintType)]") != std::string::npos,
		        "struct Detail includes USTRUCT"))
		{
			std::cerr << "Hit Detail=[" << (Hit ? Hit->Detail : "") << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* GenericDefaults = R"(
template<T, U = int>
class PairBox { public: T first; U second; }
template<T = int, U = T>
U DefaultValue() { return 6; }
void main() {}
)";
		const FOutlineResult Result = VeroScriptOutline::CollectFromSource(
		    GenericDefaults, "<outline-generic-defaults>");
		if (!Expect(Result.bSuccess, "generic-default outline should succeed") || !Expect(FindByName(Result.Symbols, "PairBox<T, U = int>") != nullptr, "class outline should display default type arguments") || !Expect(FindByName(Result.Symbols, "DefaultValue<T = int, U = T>") != nullptr, "function outline should display dependent default type arguments"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* ReferenceReturns = R"(
int Value = 1;
int& mutable_ref() { return Value; }
const int& readonly_ref() { return Value; }
void mutate(int& value, const int& readonly) { value = readonly; }
class Holder {
public:
	int value = 2;
	int& get() { return value; }
};
void main() {}
)";
		const FOutlineResult Result = VeroScriptOutline::CollectFromSource(
		    ReferenceReturns,
		    "<outline-reference-returns>");
		const FOutlineSymbol* MutableRef = FindByName(Result.Symbols, "mutable_ref");
		const FOutlineSymbol* ReadonlyRef = FindByName(Result.Symbols, "readonly_ref");
		const FOutlineSymbol* Mutate = FindByName(Result.Symbols, "mutate");
		const FOutlineSymbol* Holder = FindByName(Result.Symbols, "Holder");
		const FOutlineSymbol* Get = Holder ? FindByName(Holder->Children, "get") : nullptr;
		if (!Expect(Result.bSuccess, "reference-return outline should succeed") || !Expect(MutableRef && MutableRef->Detail.find("int&(") == 0, "outline should render mutable reference return syntax") || !Expect(ReadonlyRef && ReadonlyRef->Detail.find("const int&(") == 0, "outline should render const reference return syntax") || !Expect(Mutate && Mutate->Detail.find("void(int& value, const int& readonly)") == 0, "outline should render current reference parameter syntax") || !Expect(Get && Get->Detail.find("int&(") == 0, "method outline should render reference return syntax"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* StaticTypes = R"(
static struct Utilities {
    int Value = 1;
    int Twice(int input) { return input + input; }
}
static class Registry {
    string Name = "default";
    string GetName() { return Name; }
}
void main() {}
)";
		const FOutlineResult Result = VeroScriptOutline::CollectFromSource(
		    StaticTypes, "<outline-static-types>");
		const FOutlineSymbol* Utilities = FindByName(Result.Symbols, "Utilities");
		const FOutlineSymbol* Registry = FindByName(Result.Symbols, "Registry");
		if (!Expect(Result.bSuccess, "static type outline should succeed") || !Expect(Utilities && Utilities->Kind == EOutlineSymbolKind::Struct && Utilities->Detail.find("static struct") != std::string::npos, "static struct outline keeps its source qualifier") || !Expect(Registry && Registry->Kind == EOutlineSymbolKind::Class && Registry->Detail.find("static class") != std::string::npos, "static class outline keeps its source qualifier"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const auto Result = VeroScriptOutline::CollectFromSource(
		    "#define PREFIX ;;;;;;;;;;;;;;;;;\nPREFIX import \"missing.vs\";\n",
		    "macro-outline/entry.vs");
		if (!Expect(!Result.bSuccess && !Result.Diagnostics.empty() &&
		            Result.Diagnostics.front().bPhysicalLocation &&
		            Result.Diagnostics.front().Range.StartLine == 2 &&
		            Result.Diagnostics.front().Range.StartColumn == 8,
		        "outline preserves physical import error location"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		const std::string Imported = "#define LONGTYPE int\nexport class Imported { LONGTYPE field; }";
		FOutlineOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.bIncludeImportedExports = true;
		Options.SourceOverlays = {{"outline-range/library.vs", Imported}};
		const auto Result = VeroScriptOutline::CollectFromSource(
		    "import \"library.vs\";\nvoid main(){}", "outline-range/entry.vs", Options);
		const auto* Type = FindByName(Result.Symbols, "Imported");
		const auto* Field = Type ? FindByName(Type->Children, "field") : nullptr;
		if (!Expect(Result.bSuccess && Type && Field && Type->DeclarationRange.StartLine == 2 && Type->DeclarationRange.EndLine == 2 && Type->DeclarationRange.EndColumn == Imported.size() - Imported.find('\n') && Field->DeclarationRange.StartLine == 2 && Field->DeclarationRange.EndColumn <= Type->DeclarationRange.EndColumn,
		        "imported outline maps half-open EOF ranges through a length-changing macro"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		const auto Result = VeroScriptOutline::CollectFromSource(
		    "#define a(x) a\nenum E { a   (1), b }\n", "<outline-function-macro-end>");
		const auto* Enumeration = FindByName(Result.Symbols, "E");
		const auto* Member = Enumeration ? FindByName(Enumeration->Children, "a") : nullptr;
		if (!Expect(Result.bSuccess && Enumeration && Member && Enumeration->DeclarationRange.StartLine == 2 && Member->DeclarationRange.StartLine == 0,
		        "a function-like macro origin cannot truncate a complete enum member range"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		const auto Result =
		    VeroScriptOutline::CollectFromSource("class Outer<T>{public:struct Entry<U>{T key;U value;};};"
		                                         "void main(){struct Local{int value=7;};Local item=Local();}",
		        "scoped-outline.vs");
		const auto* Outer = FindByName(Result.Symbols, "Outer<T>");
		const auto* Function = FindByName(Result.Symbols, "main");
		if (!Expect(Result.bSuccess && Outer && Function, "scoped type outline should succeed") ||
		    !Expect(FindByName(Outer->Children, "Entry<U>") != nullptr,
		        "nested type is owned by its outer type and shows only its own parameters") ||
		    !Expect(FindByName(Function->Children, "Local") != nullptr,
		        "local type is owned by its function and keeps its source name"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const auto Result = VeroScriptOutline::CollectFromSource(
		    "class A{public:struct X{};};class A<T>{public:T value;struct Y{};};void main(){using Value=int;}",
		    "scoped-arity-outline.vs");
		const auto* Plain = FindByName(Result.Symbols, "A");
		const auto* Generic = FindByName(Result.Symbols, "A<T>");
		const auto* Function = FindByName(Result.Symbols, "main");
		if (!Expect(Result.bSuccess && Plain && Generic && Function && FindByName(Plain->Children, "X") &&
		            !FindByName(Plain->Children, "Y") && FindByName(Generic->Children, "Y") &&
		            !FindByName(Generic->Children, "X"),
		        "nested outline owners use declaration identity including generic arity"))
		{
			return EXIT_FAILURE;
		}
		std::size_t AliasCount = 0;
		for (const auto& Child : Function->Children)
		{
			if (Child.Name == "Value")
			{
				++AliasCount;
			}
		}
		if (!Expect(AliasCount == 1, "a local alias is listed once at its statement position"))
		{
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}
