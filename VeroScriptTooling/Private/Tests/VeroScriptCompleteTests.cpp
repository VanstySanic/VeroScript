// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptToolingTestVm.h"
#include "VeroScriptTooling.h"
#include "VeroScriptCompile.h"
#include "VeroScriptHostBindingCatalog.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
	int Fail(const char* Message)
	{
		std::cerr << "VeroScriptCompleteTests: " << Message << '\n';
		return EXIT_FAILURE;
	}

	bool Expect(const bool Condition, const char* Message)
	{
		return Condition ? true : (Fail(Message), false);
	}

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

	bool HasLabel(
	    const std::vector<VeroScript::Tooling::FCompletionItem>& Items,
	    const std::string& Label)
	{
		for (const VeroScript::Tooling::FCompletionItem& Item : Items)
		{
			if (Item.Label == Label)
			{
				return true;
			}
		}
		return false;
	}

	bool HasDetail(
	    const std::vector<VeroScript::Tooling::FCompletionItem>& Items,
	    const std::string_view Label,
	    const std::string_view Detail)
	{
		for (const VeroScript::Tooling::FCompletionItem& Item : Items)
		{
			if (Item.Label == Label && Item.Detail.find(Detail) != std::string::npos)
			{
				return true;
			}
		}
		return false;
	}

	std::size_t CountLabel(
	    const std::vector<VeroScript::Tooling::FCompletionItem>& Items,
	    const std::string_view Label)
	{
		std::size_t Count = 0;
		for (const VeroScript::Tooling::FCompletionItem& Item : Items)
		{
			Count += Item.Label == Label ? 1u : 0u;
		}
		return Count;
	}

	int LabelIndex(
	    const std::vector<VeroScript::Tooling::FCompletionItem>& Items,
	    const std::string& Label)
	{
		for (std::size_t Index = 0; Index < Items.size(); ++Index)
		{
			if (Items[Index].Label == Label)
			{
				return static_cast<int>(Index);
			}
		}
		return -1;
	}
}

int main()
{
	using namespace VeroScript::Tooling;
	{
		const std::string Buffer = "template<int N> struct Buffer{};";
		for (auto Source : std::vector<std::string>{
		         Buffer + "void use(){Buffer<2> na|me=Buffer<2>();}",
		         Buffer + "void use(Buffer<2> na|me){}",
		         Buffer + "void use(Buffer<2>& na|me){}",
		         Buffer + "void use(){Buffer < 2 > na|me=Buffer<2>();}",
		         Buffer + "struct Holder<T>{};void use(){Holder<Buffer<2>> na|me=Holder<Buffer<2>>();}",
		         Buffer + "void use(){Buffer<(1<2?3:4)> na|me=Buffer<(1<2?3:4)>();}",
		         Buffer + "void use(){Buffer<(8>>1)> na|me=Buffer<(8>>1)>();}",
		         "template<typename T,int N> struct Buffer{};void use(){Buffer<int,2> na|me=Buffer<int,2>();}",
		         "template<bool B> struct Flag{};void use(){Flag<true> na|me=Flag<true>();}",
		         "template<char C> struct Flag{};void use(){Flag<'a'> na|me=Flag<'a'>();}"})
		{
			const auto Caret = Source.find('|');
			Source.erase(Caret, 1);
			const auto Compiled = VeroScript::VeroScriptCompile::CompileSource(Source, "value-declarator.vs");
			const auto Result = VeroScriptComplete::CompleteAtSource(Source,
			    {1, static_cast<std::uint32_t>(Caret + 1)}, "value-declarator.vs");
			if (!Expect(Compiled.bSuccess && Result.bSuccess && Result.Items.empty(),
			        "compiled value-template declarators suppress existing symbols and keywords"))
			{
				std::cerr << Source << '\n'
				          << Compiled.Error.Message << '\n';
				return EXIT_FAILURE;
			}
		}
	}
	{
		const std::string Leaf = "struct Leaf{int value=7;};";
		const std::string Nested = "template<typename T=Leaf> struct Outer{template<typename U=T> struct Box{U child;};};";
		for (const auto& Source : std::vector<std::string>{
		         Leaf + Nested + "void main(){Outer<>::Box<>().child.value;}",
		         Leaf + Nested + "void main(){Outer<>::Box<> box=Outer<>::Box<>();box.child.value;}",
		         Leaf + Nested + "void main(){using Pick=Outer<>::Box<>;auto box=Pick();box.child.value;}",
		         Leaf + Nested + "void main(){::Outer<>::Box<>().child.value;}",
		         Leaf + "template<typename T=Leaf> struct Outer{struct Box{T child;};};void main(){Outer<>::Box().child.value;}",
		         Leaf + "template<typename T=Leaf> struct Outer{template<typename U=T> struct Middle{template<typename V=U> struct Box{V child;};};};void main(){Outer<>::Middle<>::Box<>().child.value;}",
		         Leaf + "template<typename T=Leaf,int N=2> struct Outer{template<typename U=T,int M=(N+1)> struct Box{U children[M];};};void main(){Outer<>::Box<>().children[0].value;}",
		         "export struct T{int value=7;};template<typename U=T> struct Box{U child;};template<typename T> int read(T unused){return Box<>().child.value;}void main(){read(0);}",
		         "export struct T{int value=7;};template<typename U=T> U make(){return U();}template<typename T> int read(T unused){return make<>().value;}void main(){read(0);}"})
		{
			const auto Compiled = VeroScript::VeroScriptCompile::CompileSource(Source, "nested-default-contract.vs");
			const FTextPosition Position{1, static_cast<std::uint32_t>(Source.rfind("value") + 3)};
			const auto Result = VeroScriptComplete::CompleteAtSource(Source, Position, "nested-default-contract.vs");
			if (!Expect(Compiled.bSuccess && Result.bSuccess && HasDetail(Result.Items, "value", "int"),
			        "nested default member completion agrees with Core declaration binding"))
			{
				std::cerr << Source << '\n'
				          << Compiled.Error.Message << '\n';
				return EXIT_FAILURE;
			}
		}
	}
	{
		// Cold compiler queries and Tooling must agree before any explicitly
		// typed local has caused the generic owner's layout to be instantiated.
		const std::string Inner = "struct Inner{int value=7;};";
		const std::string Box = "struct Box<T=Inner>{T child=T();};";
		const std::string Exact = "struct Box<T>{T child=T();};";
		const std::string WithDefault = "struct Box<T,U=int>{U child;};";
		for (const auto& Source : {
		         Inner + Box + "void main(){Box<>().child.value;}",
		         "namespace N{" + Inner + Box + "}void main(){N::Box<>().child.value;}",
		         Inner + Box + "void main(){auto child=Box<>().child;child.value;}",
		         Inner + "class Base<T>{public:T child;};class Box<T=Inner>:public Base<T>{};void main(){Box<>().child.value;}",
		         Inner + WithDefault + Exact + "void main(){Box<Inner>().child.value;}",
		         Inner + Exact + WithDefault + "void main(){Box<Inner>().child.value;}"})
		{
			const auto Compiled = VeroScript::VeroScriptCompile::CompileSource(Source, "generic-temporary-contract.vs");
			const FTextPosition Position{1, static_cast<std::uint32_t>(Source.rfind("value") + 3)};
			const auto Result = VeroScriptComplete::CompleteAtSource(Source, Position, "generic-temporary-contract.vs");
			if (!Expect(Compiled.bSuccess && Result.bSuccess && HasDetail(Result.Items, "value", "int"),
			        "generic temporary member completion agrees with cold Core field queries"))
			{
				std::cerr << Source << '\n'
				          << Compiled.Error.Message << '\n';
				return EXIT_FAILURE;
			}
		}
	}
	{
		// These alias-qualified calls are compiled by Core as static calls, not
		// constructors. Their results must retain the same concrete member types.
		const std::string Prefix = "struct Inner{int value=7;};"
		                           "struct Factory<T>{static T create(){return T();}"
		                           "static U identity<U>(U input){return input;}};"
		                           "using First=Factory<Inner>;using Maker=First;";
		for (const auto& Body : {"auto result=Maker::create();result.value;",
		         "Maker::identity<Inner>(Inner()).value;", "Maker::identity(Inner()).value;",
		         "auto result=Maker::identity<Inner>(Inner());result.value;",
		         "using Local=Factory<Inner>;auto result=Local::create();result.value;"})
		{
			const auto Source = Prefix + "void main(){" + Body + "}";
			const auto Compiled = VeroScript::VeroScriptCompile::CompileSource(Source, "alias-static-contract.vs");
			const FTextPosition Position{1, static_cast<std::uint32_t>(Source.rfind("value") + 3)};
			const auto Result = VeroScriptComplete::CompleteAtSource(Source, Position, "alias-static-contract.vs");
			if (!Expect(Compiled.bSuccess && Result.bSuccess && HasDetail(Result.Items, "value", "int"),
			        "alias-qualified static result completion agrees with Core compilation"))
			{
				std::cerr << Source << '\n'
				          << Compiled.Error.Message << '\n';
				return EXIT_FAILURE;
			}
		}
	}
	{
		// Qualified paths bind the nearest visible first namespace. Missing
		// members in that namespace never fall back to a same-named root path.
		const std::string Inner = "struct Inner{int value=7;};";
		const std::vector<std::pair<std::string, bool>> Cases = {
		    {Inner + "namespace N{Inner item=Inner();}namespace A{namespace N{}void use(){N::item.value;}}void main(){}", false},
		    {Inner + "struct Other{};namespace N{Inner item=Inner();}namespace A{namespace N{Other item=Other();}void use(){N::item.value;}}void main(){}", false},
		    {Inner + "namespace N{namespace M{Inner item=Inner();}}namespace A{namespace N{}void use(){N::M::item.value;}}void main(){}", false},
		    {Inner + "namespace A{namespace N{Inner item=Inner();}void use(){N::item.value;}}void main(){}", true},
		    {Inner + "namespace N{Inner item=Inner();}namespace A{namespace N{}void use(){::N::item.value;}}void main(){}", true},
		    {Inner + "namespace A{namespace N{Inner item=Inner();}void use(){A::N::item.value;}}void main(){}", true},
		    {Inner + "namespace A{namespace N{Inner item=Inner();void use(){item.value;}}}void main(){}", true},
		};
		for (const auto& [Source, bCompiles] : Cases)
		{
			const auto Compiled = VeroScript::VeroScriptCompile::CompileSource(Source, "qualified-global-contract.vs");
			FTextPosition Position;
			if (!FindPosition(Source, "item.value", Position)) return EXIT_FAILURE;
			Position.Column += static_cast<std::uint32_t>(std::string_view("item.va").size());
			const auto Result = VeroScriptComplete::CompleteAtSource(Source, Position, "qualified-global-contract.vs");
			if (!Expect(Compiled.bSuccess == bCompiles && Result.bSuccess && HasDetail(Result.Items, "value", "int") == bCompiles,
			        "qualified global completion agrees with Core name lookup"))
			{
				std::cerr << Source << '\n'
				          << Compiled.Error.Message << '\n';
				return EXIT_FAILURE;
			}
		}
	}

	{
		const std::string Source =
		    "namespace A{class Parent<T>{public:T wrong;};class Box<T>:public Parent<T>{public:T value;};}\n"
		    "namespace B{class Parent<U>{public:U expected;};class Box<U>:public Parent<U>{public:U value;};}\n"
		    "void use(B::Box<int> box){print(box.value);}\n";
		FTextPosition Position;
		if (!FindPosition(Source, "box.value", Position)) return EXIT_FAILURE;
		Position.Column += static_cast<std::uint32_t>(std::string_view("box.").size());
		const auto Result = VeroScriptComplete::CompleteAtSource(Source, Position, "<qualified-generic-members>");
		if (!Expect(Result.bSuccess && HasDetail(Result.Items, "value", "int") && HasDetail(Result.Items, "expected", "int") && !HasLabel(Result.Items, "wrong"),
		        "qualified generic completion preserves the actual parameter names and inheritance tree"))
		{
			return EXIT_FAILURE;
		}
	}

	// Mid-edit buffer without trailing ';' — recovery must still complete.
	constexpr const char* IncompleteSource = R"(
struct Point {
	int x;
}

void Helper() {}

void main() {
	int total = 1;
	Hel
}
)";

	{
		FTextPosition Position;
		if (!Expect(FindPosition(IncompleteSource, "\tHel", Position), "locate Hel prefix"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\tHel").size());

		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    IncompleteSource,
		    Position,
		    "<complete>");
		if (!Expect(Result.bSuccess, "completion should succeed with recovery"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Result.Prefix == "Hel", "prefix should be Hel"))
		{
			std::cerr << "prefix=[" << Result.Prefix << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(Result.bHasReplaceRange, "replace range present"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(HasLabel(Result.Items, "Helper"), "Helper should be suggested"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "total"), "total should not match Hel"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "Point"), "Point should not match Hel"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		FTextPosition HelStart;
		if (!Expect(FindPosition(IncompleteSource, "\tHel", HelStart), "locate Hel start"))
		{
			return EXIT_FAILURE;
		}
		HelStart.Column += 2; // caret after 'H' → prefix "H"

		const FCompletionResult AtH = VeroScriptComplete::CompleteAtSource(
		    IncompleteSource,
		    HelStart,
		    "<complete>");
		if (!Expect(AtH.bSuccess && AtH.Prefix == "H", "prefix H after first letter"))
		{
			std::cerr << "prefix=[" << AtH.Prefix << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(HasLabel(AtH.Items, "Helper"), "Helper for prefix H"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* ValidSource = R"(
struct Point {
	int x;
}

void Helper() {}

void main() {
	int total = 1;
}
)";

	{
		FTextPosition AfterDecl;
		if (!Expect(FindPosition(ValidSource, "int total = 1;", AfterDecl), "locate total decl"))
		{
			return EXIT_FAILURE;
		}
		AfterDecl.Column += static_cast<std::uint32_t>(std::string_view("int total = 1;").size());

		const FCompletionResult Empty = VeroScriptComplete::CompleteAtSource(
		    ValidSource,
		    AfterDecl,
		    "<complete>");
		if (!Expect(Empty.bSuccess, "empty-prefix completion"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Empty.Prefix.empty(), "no identifier prefix"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(HasLabel(Empty.Items, "total"), "total local visible"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(HasLabel(Empty.Items, "Helper"), "Helper top-level visible"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(HasLabel(Empty.Items, "Point"), "Point type visible"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        !HasLabel(Empty.Items, "__sqrt") && !HasLabel(Empty.Items, "__read_int") && !HasLabel(Empty.Items, "__channel_send") && !HasLabel(Empty.Items, "now") && !HasLabel(Empty.Items, "sleep") && !HasLabel(Empty.Items, "getenv") && !HasLabel(Empty.Items, "setenv") && !HasLabel(Empty.Items, "read_int") && !HasLabel(Empty.Items, "pause") && !HasLabel(Empty.Items, "sqrt") && !HasLabel(Empty.Items, "read_file") && !HasLabel(Empty.Items, "contains") && !HasLabel(Empty.Items, "spawn_task"),
		        "internal and namespace-only builtins should not be exposed globally"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* MemberSource = R"(
struct Point {
	int x;
	int Sum() { return x; }
}

void main() {
	Point p = Point();
	p.
}
)";

	{
		FTextPosition Position;
		if (!Expect(FindPosition(MemberSource, "\tp.", Position), "locate p."))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\tp.").size());

		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    MemberSource,
		    Position,
		    "<complete>");
		if (!Expect(Result.bSuccess, "member completion after p."))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Result.Prefix.empty(), "empty member prefix after dot"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(HasLabel(Result.Items, "x"), "field x suggested"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(HasLabel(Result.Items, "Sum"), "method Sum suggested"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "Helper"), "top-level Helper not in member list"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "p"), "receiver itself not in member list"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* BraceMemberSource = R"(
struct Point {
	int x;
	Point(int value) { x = value; }
	int Sum() { return x; }
}
class Box<T> {
public:
	T value;
	Box(T input) { value = input; }
}

void main() {
	Point{7}.x;
	Box<int>{9}.value;
	array<int>{1, 2}.push(3);
}
)";

	{
		FTextPosition Position;
		if (!Expect(FindPosition(BraceMemberSource, "\tPoint{7}.", Position),
		        "locate brace-constructed member access"))
		{
			return EXIT_FAILURE;
		}
		Position.Column +=
		    static_cast<std::uint32_t>(std::string_view("\tPoint{7}.").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    BraceMemberSource,
		    Position,
		    "<complete-brace-member>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "x") && HasLabel(Result.Items, "Sum"),
		        "brace construction should preserve receiver type for member completion"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		FTextPosition Position;
		if (!Expect(FindPosition(BraceMemberSource, "\tBox<int>{9}.", Position),
		        "locate generic brace-constructed member access"))
		{
			return EXIT_FAILURE;
		}
		Position.Column +=
		    static_cast<std::uint32_t>(std::string_view("\tBox<int>{9}.").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    BraceMemberSource,
		    Position,
		    "<complete-generic-brace-member>");
		if (!Expect(Result.bSuccess && HasLabel(Result.Items, "value"),
		        "generic brace construction should preserve receiver type"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		FTextPosition Position;
		if (!Expect(FindPosition(BraceMemberSource, "\tarray<int>{1, 2}.", Position),
		        "locate container brace-constructed member access"))
		{
			return EXIT_FAILURE;
		}
		Position.Column +=
		    static_cast<std::uint32_t>(std::string_view("\tarray<int>{1, 2}.").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    BraceMemberSource,
		    Position,
		    "<complete-container-brace-member>");
		if (!Expect(Result.bSuccess && HasLabel(Result.Items, "push"),
		        "container brace construction should preserve soft member completion"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* MemberScopeSource = R"(
struct Point {
	int x;
	int Sum() { return x; }
}

void main() {
	Point p = Point();
	p::
}
)";

	{
		FTextPosition Position;
		if (!Expect(FindPosition(MemberScopeSource, "\tp::", Position), "locate p::"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\tp::").size());

		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    MemberScopeSource,
		    Position,
		    "<complete-scope>");
		if (!Expect(Result.bSuccess, "member completion after p::"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(HasLabel(Result.Items, "x") && HasLabel(Result.Items, "Sum"),
		        ":: member completion matches ."))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* MemberPrefixSource = R"(
struct Point {
	int x;
	int Sum() { return x; }
}

void main() {
	Point p = Point();
	p.Su
}
)";

	{
		FTextPosition Position;
		if (!Expect(FindPosition(MemberPrefixSource, "\tp.Su", Position), "locate p.Su"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\tp.Su").size());

		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    MemberPrefixSource,
		    Position,
		    "<complete>");
		if (!Expect(Result.bSuccess && Result.Prefix == "Su", "prefix Su"))
		{
			std::cerr << "prefix=[" << Result.Prefix << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(HasLabel(Result.Items, "Sum"), "Sum for Su"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "x"), "x should not match Su"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* GotoSource = R"(
void main() {
	int n = 0;
again:
	n = n + 1;
done:
	if (n < 3) {
		goto 
	}
}
)";

	{
		FTextPosition Position;
		if (!Expect(FindPosition(GotoSource, "\t\tgoto ", Position), "locate goto "))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\t\tgoto ").size());

		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    GotoSource,
		    Position,
		    "<goto-complete>");
		if (!Expect(Result.bSuccess, "goto completion should succeed"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(HasLabel(Result.Items, "again"), "label again suggested"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(HasLabel(Result.Items, "done"), "forward label done suggested"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "main"), "non-labels excluded after goto"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "n"), "locals excluded after goto"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* GotoPrefixSource = R"(
void main() {
again:
done:
	goto ag
}
)";

	{
		FTextPosition Position;
		if (!Expect(FindPosition(GotoPrefixSource, "\tgoto ag", Position), "locate goto ag"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\tgoto ag").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    GotoPrefixSource,
		    Position,
		    "<goto-prefix-complete>");
		if (!Expect(
		        Result.bSuccess && Result.Prefix == "ag" && HasLabel(Result.Items, "again"),
		        "goto prefix at EndExclusive stays label-only"))
		{
			std::cerr << "prefix=[" << Result.Prefix << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "done"), "done filtered by ag prefix"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "main"), "non-labels excluded for goto ag|"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* GotoBeforeLexErrorSource = R"(
void main() {
again:
	goto ag
}
/*
)";

	{
		FTextPosition Position;
		if (!Expect(
		        FindPosition(GotoBeforeLexErrorSource, "\tgoto ag", Position),
		        "locate goto ag before lex error"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\tgoto ag").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    GotoBeforeLexErrorSource,
		    Position,
		    "<goto-before-lex-error>");
		if (!Expect(
		        Result.bSuccess && Result.Prefix == "ag" && HasLabel(Result.Items, "again"),
		        "goto complete despite later lex error"))
		{
			std::cerr << "prefix=[" << Result.Prefix << "]\n";
			return EXIT_FAILURE;
		}
	}

	// Soft goto must not treat ternary `? then : else` arms as statement labels.
	constexpr const char* GotoSoftTernarySource = R"(
void main() {
	int flag = 1;
	int x = flag ? agelse : 0;
again:
	goto ag
}
/*
)";

	{
		FTextPosition Position;
		if (!Expect(FindPosition(GotoSoftTernarySource, "\tgoto ag", Position), "locate goto ag ternary"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\tgoto ag").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    GotoSoftTernarySource,
		    Position,
		    "<goto-soft-ternary>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "again"),
		        "soft goto keeps real label"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "agelse"), "soft goto ignores ternary then-arm"))
		{
			return EXIT_FAILURE;
		}
	}

	for (const std::string TypeName : {"handle", "shared", "std::handle", "std::shared"})
	{
		const std::string Source = "struct Point { int x; }\nvoid main() {\n" + TypeName + "<Point> h = " + TypeName + "<Point>();\n\th.\n}\n";
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "\th.", Position), "locate shared/handle member completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += 3;
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    Source, Position, "<shared-handle-complete>");
		const int ValueIndex = LabelIndex(Result.Items, "value");
		if (!Expect(Result.bSuccess && ValueIndex >= 0 && Result.Items[static_cast<std::size_t>(ValueIndex)].Detail == "Point",
		        "shared/handle.value completion specializes the payload type"))
		{
			std::cerr << TypeName << '\n';
			return EXIT_FAILURE;
		}
	}

	constexpr const char* ThisMemberSource = R"(
class Box { public: int n;
	int get() const {
		return this.
	} }
)";

	{
		FTextPosition Position;
		if (!Expect(FindPosition(ThisMemberSource, "this.", Position), "locate this."))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("this.").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    ThisMemberSource,
		    Position,
		    "<this-complete>");
		if (!Expect(Result.bSuccess && HasLabel(Result.Items, "n"), "this. field n"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(HasLabel(Result.Items, "get"), "this. method get"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* ContainerSource = R"(
void main() {
	array<int> xs = array<int>();
	xs.
}
)";

	{
		FTextPosition Position;
		if (!Expect(FindPosition(ContainerSource, "\txs.", Position), "locate xs."))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\txs.").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    ContainerSource,
		    Position,
		    "<array-complete>");
		if (!Expect(Result.bSuccess && HasLabel(Result.Items, "push"), "array.push"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(HasLabel(Result.Items, "clear"), "array.clear"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        HasLabel(Result.Items, "reserve") && HasLabel(Result.Items, "capacity"),
		        "array reserve/capacity"))
		{
			return EXIT_FAILURE;
		}
		std::string PushDetail;
		std::string ReserveDetail;
		std::string CapacityDetail;
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.Label == "push")
			{
				PushDetail = Item.Detail;
			}
			else if (Item.Label == "reserve")
			{
				ReserveDetail = Item.Detail;
			}
			else if (Item.Label == "capacity")
			{
				CapacityDetail = Item.Detail;
			}
		}
		if (!Expect(
		        PushDetail.find("array<int>") != std::string::npos,
		        "push Detail chains as array<int>"))
		{
			std::cerr << "detail=[" << PushDetail << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
		        ReserveDetail.find("int64 capacity") != std::string::npos && CapacityDetail.find("int64") != std::string::npos,
		        "array reserve/capacity details"))
		{
			return EXIT_FAILURE;
		}
	}

	// Force soft lexical member path: parse must fail, but lexer must succeed so
	// member context + TryLexResolveBindingTypeName can see `array<int> xs`.
	constexpr const char* SoftLexicalArraySource = R"(
array<int> xs = array<int>();
void main() {
	xs.pu
}
struct Broken {
)";

	{
		FTextPosition Position;
		if (!Expect(FindPosition(SoftLexicalArraySource, "\txs.pu", Position), "locate xs.pu soft-lex"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\txs.pu").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    SoftLexicalArraySource,
		    Position,
		    "<array-soft-lex-complete>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "push"),
		        "soft-lex array<int> xs.push via type keywords"))
		{
			return EXIT_FAILURE;
		}
	}

	// Later lex error must not hide member context before the fault (same policy as
	// FindIdentifierAt). Unterminated /* after the caret still yields xs.push.
	constexpr const char* MemberBeforeLexErrorSource = R"(
array<int> xs = array<int>();
void main() {
	xs.pu
}
/*
)";

	{
		FTextPosition Position;
		if (!Expect(
		        FindPosition(MemberBeforeLexErrorSource, "\txs.pu", Position),
		        "locate xs.pu before lex error"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\txs.pu").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    MemberBeforeLexErrorSource,
		    Position,
		    "<member-before-lex-error>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "push"),
		        "member complete despite later lex error"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* IndexChainSource = R"(
struct Point {
	int x;
}

void main() {
	array<Point> pts = array<Point>();
	pts.push(Point());
	pts[0].
}
)";

	{
		FTextPosition Position;
		if (!Expect(FindPosition(IndexChainSource, "pts[0].", Position), "locate pts[0]."))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("pts[0].").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    IndexChainSource,
		    Position,
		    "<index-chain>");
		if (!Expect(Result.bSuccess && HasLabel(Result.Items, "x"), "pts[0].x field"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* HostCompleteSource = R"(
void main() {
	pri
}
)";

	{
		FTextPosition Position;
		if (!Expect(FindPosition(HostCompleteSource, "\tpri", Position), "locate pri"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\tpri").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    HostCompleteSource,
		    Position,
		    "<host-complete>");
		if (!Expect(Result.bSuccess && HasLabel(Result.Items, "print"), "host print"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(HasLabel(Result.Items, "printf"), "host printf"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        !HasLabel(Result.Items, "private"),
		        "statement completion must not offer type-body access keyword private"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* FormatFCompleteSource = R"(
void main() {
	formatf
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(FormatFCompleteSource, "\tformatf", Position),
		        "locate formatf"))
		{
			return EXIT_FAILURE;
		}
		Position.Column +=
		    static_cast<std::uint32_t>(std::string_view("\tformatf").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    FormatFCompleteSource,
		    Position,
		    "<format-f-complete>");
		if (!Expect(Result.bSuccess && HasLabel(Result.Items, "formatf"), "host formatf"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* CastCompleteSource = R"(
void main() {
	cast
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(CastCompleteSource, "\tcast", Position),
		        "locate cast"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\tcast").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    CastCompleteSource,
		    Position,
		    "<cast-complete>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "cast") && HasLabel(Result.Items, "try_cast") && HasLabel(Result.Items, "const_cast"),
		        "checked cast intrinsics complete"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* ForConditionNoKeywordSource = R"(
void main() {
	for (int i = 0; i) {
	}
}
)";

	{
		FTextPosition Position;
		if (!Expect(FindPosition(ForConditionNoKeywordSource, "0; i", Position), "locate for-cond i"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("0; i").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    ForConditionNoKeywordSource,
		    Position,
		    "<for-cond-i>");
		if (!Expect(Result.bSuccess, "for-cond completion succeeds"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(HasLabel(Result.Items, "i"), "for-cond offers local i"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "if"), "for-cond must not offer if"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "int"), "for-cond must not offer int"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "import"), "for-cond must not offer import"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "interface"), "for-cond must not offer interface"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* ForInitNameNoKeywordSource = R"(
void main() {
	for (int i
}
)";

	{
		FTextPosition Position;
		if (!Expect(FindPosition(ForInitNameNoKeywordSource, "int i", Position), "locate for-init i"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("int i").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    ForInitNameNoKeywordSource,
		    Position,
		    "<for-init-i>");
		if (!Expect(Result.bSuccess, "for-init name completion succeeds"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "if"), "for-init name must not offer if"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "int"), "for-init name must not offer int"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "import"), "for-init name must not offer import"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* KeywordCompleteSource = R"(
void main() {
	ret
}
)";

	{
		FTextPosition Position;
		if (!Expect(FindPosition(KeywordCompleteSource, "\tret", Position), "locate ret"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\tret").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    KeywordCompleteSource,
		    Position,
		    "<keyword-complete>");
		if (!Expect(Result.bSuccess && HasLabel(Result.Items, "return"), "keyword return"))
		{
			return EXIT_FAILURE;
		}
		bool bReturnIsKeyword = false;
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.Label == "return" && Item.Kind == EOutlineSymbolKind::Keyword)
			{
				bReturnIsKeyword = true;
				break;
			}
		}
		if (!Expect(bReturnIsKeyword, "return completion kind is Keyword"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(HasLabel(Result.Items, "true") == false, "true should not match ret"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* FullKeywordCompleteSource = R"(
void main() {
	return
}
)";

	{
		FTextPosition Position;
		if (!Expect(FindPosition(FullKeywordCompleteSource, "\treturn", Position), "locate return"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\treturn").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    FullKeywordCompleteSource,
		    Position,
		    "<full-keyword-complete>");
		if (!Expect(Result.bSuccess && Result.Prefix == "return", "full keyword prefix"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Result.bHasReplaceRange, "full keyword replace range"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(HasLabel(Result.Items, "return"), "full keyword still lists return"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* BuiltinTypeKeywordSource = R"(
void main() {
	arr
}
)";

	{
		FTextPosition ArrayPos;
		if (!Expect(FindPosition(BuiltinTypeKeywordSource, "\tarr", ArrayPos), "locate arr"))
		{
			return EXIT_FAILURE;
		}
		ArrayPos.Column += static_cast<std::uint32_t>(std::string_view("\tarr").size());
		const FCompletionResult ArrayResult = VeroScriptComplete::CompleteAtSource(
		    BuiltinTypeKeywordSource,
		    ArrayPos,
		    "<array-keyword>");
		if (!Expect(ArrayResult.bSuccess, "array keyword completion succeeds"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(HasLabel(ArrayResult.Items, "array"), "keyword array"))
		{
			std::cerr << "prefix=[" << ArrayResult.Prefix << "] items=";
			for (const FCompletionItem& Item : ArrayResult.Items)
			{
				std::cerr << Item.Label << ',';
			}
			std::cerr << '\n';
			return EXIT_FAILURE;
		}
		bool bArrayIsType = false;
		for (const FCompletionItem& Item : ArrayResult.Items)
		{
			if (Item.Label == "array" && Item.Kind == EOutlineSymbolKind::TypeAlias)
			{
				bArrayIsType = true;
				break;
			}
		}
		if (!Expect(bArrayIsType, "array completion kind is TypeAlias"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Textual match quality is the primary ranking key. Context remains the
		// tie-breaker, so callable prefixes beat an equally prefixed builtin type.
		const auto CompletePrefix = [](const std::string_view Prefix)
		{
			const std::string Source = "void main() {\n\t" + std::string(Prefix) + "\n}\n";
			FTextPosition Position;
			(void)FindPosition(Source, "\t" + std::string(Prefix), Position);
			Position.Column += static_cast<std::uint32_t>(Prefix.size() + 1u);
			return VeroScriptComplete::CompleteAtSource(Source, Position, "<match-ranking>");
		};

		const FCompletionResult Pr = CompletePrefix("pr");
		const int PrintIndex = LabelIndex(Pr.Items, "print");
		const int PrintfIndex = LabelIndex(Pr.Items, "printf");
		const int PriorityQueueIndex = LabelIndex(Pr.Items, "priority_queue");
		const int ProcessIndex = LabelIndex(Pr.Items, "std.process");
		if (!Expect(
		        Pr.bSuccess && PrintIndex >= 0 && PrintfIndex >= 0 && PriorityQueueIndex >= 0 && PrintIndex < PriorityQueueIndex && PrintfIndex < PriorityQueueIndex && (ProcessIndex < 0 || PriorityQueueIndex < ProcessIndex),
		        "pr ranks print/printf before priority_queue and weak namespace leaves"))
		{
			for (std::size_t Index = 0; Index < Pr.Items.size(); ++Index)
			{
				std::cerr << Index << ':' << Pr.Items[Index].Label
				          << " match=" << static_cast<unsigned>(Pr.Items[Index].MatchQuality)
				          << " context=" << Pr.Items[Index].SortPriority << '\n';
			}
			return EXIT_FAILURE;
		}

		const FCompletionResult Pri = CompletePrefix("pri");
		const int PriPrintIndex = LabelIndex(Pri.Items, "print");
		const int PriPrintfIndex = LabelIndex(Pri.Items, "printf");
		const int PriPriorityQueueIndex = LabelIndex(Pri.Items, "priority_queue");
		const int PriContainsIndex = LabelIndex(Pri.Items, "eprint");
		if (!Expect(
		        Pri.bSuccess && PriPrintIndex >= 0 && PriPrintfIndex >= 0 && PriPriorityQueueIndex >= 0 && PriContainsIndex >= 0 && Pri.Items[static_cast<std::size_t>(PriPrintIndex)].MatchQuality == 2 && Pri.Items[static_cast<std::size_t>(PriPrintfIndex)].MatchQuality == 2 && Pri.Items[static_cast<std::size_t>(PriPriorityQueueIndex)].MatchQuality == 2 && PriPrintIndex < PriContainsIndex && PriPrintfIndex < PriContainsIndex && PriPriorityQueueIndex < PriContainsIndex,
		        "pri prefix matches rank before contains matches"))
		{
			return EXIT_FAILURE;
		}

		const FCompletionResult H = CompletePrefix("h");
		const int HandleIndex = LabelIndex(H.Items, "handle");
		const int ThisIndex = LabelIndex(H.Items, "this");
		const int WeakNamespaceIndex = LabelIndex(H.Items, "std.this_thread");
		if (!Expect(
		        H.bSuccess && HandleIndex >= 0 && (ThisIndex < 0 || HandleIndex < ThisIndex) && (WeakNamespaceIndex < 0 || HandleIndex < WeakNamespaceIndex),
		        "h ranks the handle prefix before contains/subsequence matches"))
		{
			return EXIT_FAILURE;
		}

		const FCompletionResult Std = CompletePrefix("std");
		if (!Expect(
		        Std.bSuccess && LabelIndex(Std.Items, "std") == 0,
		        "std exact namespace match ranks first"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* CamelSource = R"(
void SetNextValue() {}
void main() {
	SetV
}
)";
		FTextPosition CamelPosition;
		if (!Expect(FindPosition(CamelSource, "\tSetV", CamelPosition), "locate camel prefix"))
		{
			return EXIT_FAILURE;
		}
		CamelPosition.Column += 5;
		const FCompletionResult Camel = VeroScriptComplete::CompleteAtSource(
		    CamelSource,
		    CamelPosition,
		    "<camel-ranking>");
		const int CamelIndex = LabelIndex(Camel.Items, "SetNextValue");
		if (!Expect(
		        Camel.bSuccess && CamelIndex >= 0 && Camel.Items[static_cast<std::size_t>(CamelIndex)].MatchQuality == 3,
		        "camel segment matches use the word-boundary tier"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* LocalTypeSource = R"(
class ScopeGlobal {}
void main() {
	using ScopeOuter = int;
	{
		using ScopeInner = int;
		array<Sco> value;
	}
}
)";
		FTextPosition LocalTypePosition;
		if (!Expect(
		        FindPosition(LocalTypeSource, "array<Sco", LocalTypePosition),
		        "locate local type-alias completion"))
		{
			return EXIT_FAILURE;
		}
		LocalTypePosition.Column +=
		    static_cast<std::uint32_t>(std::string_view("array<Sco").size());
		const FCompletionResult LocalType = VeroScriptComplete::CompleteAtSource(
		    LocalTypeSource,
		    LocalTypePosition,
		    "<local-type-ranking>");
		const int InnerAliasIndex = LabelIndex(LocalType.Items, "ScopeInner");
		const int OuterAliasIndex = LabelIndex(LocalType.Items, "ScopeOuter");
		const int GlobalTypeIndex = LabelIndex(LocalType.Items, "ScopeGlobal");
		if (!Expect(
		        LocalType.bSuccess && InnerAliasIndex >= 0 && OuterAliasIndex >= 0 && GlobalTypeIndex >= 0 && InnerAliasIndex < OuterAliasIndex && OuterAliasIndex < GlobalTypeIndex && LocalType.Items[static_cast<std::size_t>(InnerAliasIndex)].SortPriority < LocalType.Items[static_cast<std::size_t>(OuterAliasIndex)].SortPriority && LocalType.Items[static_cast<std::size_t>(OuterAliasIndex)].SortPriority < LocalType.Items[static_cast<std::size_t>(GlobalTypeIndex)].SortPriority,
		        "type-name context orders inner, outer and global types by scope distance"))
		{
			std::cerr << "inner=" << InnerAliasIndex << " outer=" << OuterAliasIndex
			          << " global=" << GlobalTypeIndex << '\n';
			for (std::size_t Index = 0; Index < LocalType.Items.size(); ++Index)
			{
				const FCompletionItem& Item = LocalType.Items[Index];
				std::cerr << Index << ':' << Item.Label
				          << " match=" << static_cast<unsigned>(Item.MatchQuality)
				          << " sort=" << Item.SortPriority << '\n';
			}
			return EXIT_FAILURE;
		}
	}

	constexpr const char* FunctionKeywordSource = R"(
void main() {
	fun
}
)";
	{
		FTextPosition Pos;
		if (!Expect(FindPosition(FunctionKeywordSource, "fun", Pos), "locate function keyword"))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += 3;
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    FunctionKeywordSource,
		    Pos,
		    "<function-keyword>");
		bool bFunctionIsKeyword = false;
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.Label == "function" && Item.Kind == EOutlineSymbolKind::Keyword)
			{
				bFunctionIsKeyword = true;
				break;
			}
		}
		if (!Expect(
		        Result.bSuccess && Result.Prefix == "fun" && bFunctionIsKeyword,
		        "function alias keyword completion"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* TemplateKeywordSource = R"(
temp
)";
	{
		FTextPosition Pos;
		if (!Expect(FindPosition(TemplateKeywordSource, "temp", Pos), "locate template keyword"))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += 4;
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    TemplateKeywordSource,
		    Pos,
		    "<template-keyword>");
		bool bTemplateIsKeyword = false;
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.Label == "template" && Item.Kind == EOutlineSymbolKind::Keyword)
			{
				bTemplateIsKeyword = true;
				break;
			}
		}
		if (!Expect(
		        Result.bSuccess && Result.Prefix == "temp" && bTemplateIsKeyword,
		        "template keyword completion"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* IntPrefixKeywordSource = R"(
void main() {
	int
}
)";

	{
		// Bare type keyword used to fail recovery (`int;` is invalid). Must still
		// complete with Keyword kind via type-decl recovery or keyword-only fallback.
		FTextPosition IntPos;
		if (!Expect(FindPosition(IntPrefixKeywordSource, "\tint", IntPos), "locate int"))
		{
			return EXIT_FAILURE;
		}
		IntPos.Column += static_cast<std::uint32_t>(std::string_view("\tint").size());
		const FCompletionResult IntResult = VeroScriptComplete::CompleteAtSource(
		    IntPrefixKeywordSource,
		    IntPos,
		    "<int-keyword>");
		bool bIntIsKeyword = false;
		for (const FCompletionItem& Item : IntResult.Items)
		{
			if (Item.Label == "int" && Item.Kind == EOutlineSymbolKind::Keyword && Item.Detail == "builtin type" && Item.InsertText == "int" && !Item.bInsertTextSnippet)
			{
				bIntIsKeyword = true;
				break;
			}
		}
		if (!Expect(
		        IntResult.bSuccess && bIntIsKeyword && HasLabel(IntResult.Items, "int8") && HasLabel(IntResult.Items, "int16") && HasLabel(IntResult.Items, "int32") && HasLabel(IntResult.Items, "int64"),
		        "signed integer builtin type completion"))
		{
			std::cerr << "prefix=[" << IntResult.Prefix << "] ok=" << IntResult.bSuccess << " items=";
			for (const FCompletionItem& Item : IntResult.Items)
			{
				std::cerr << Item.Label << '/' << static_cast<int>(Item.Kind) << ',';
			}
			std::cerr << '\n';
			return EXIT_FAILURE;
		}
	}

	constexpr const char* UIntKeywordSource = R"(
void main() {
	uint
}
)";
	{
		FTextPosition Pos;
		if (!Expect(FindPosition(UIntKeywordSource, "uint", Pos), "locate uint"))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += 4;
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    UIntKeywordSource,
		    Pos,
		    "<uint-alias-keywords>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "uint8") && HasLabel(Result.Items, "uint16") && HasLabel(Result.Items, "uint32") && HasLabel(Result.Items, "uint64"),
		        "unsigned integer alias keyword completion"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* IfKeywordFallbackSource = R"(
void main() {
	if
}
)";

	{
		// Control-flow keyword alone is not a statement; keyword-only fallback must fire.
		FTextPosition IfPos;
		if (!Expect(FindPosition(IfKeywordFallbackSource, "\tif", IfPos), "locate if"))
		{
			return EXIT_FAILURE;
		}
		IfPos.Column += static_cast<std::uint32_t>(std::string_view("\tif").size());
		const FCompletionResult IfResult = VeroScriptComplete::CompleteAtSource(
		    IfKeywordFallbackSource,
		    IfPos,
		    "<if-keyword>");
		bool bIfIsKeyword = false;
		for (const FCompletionItem& Item : IfResult.Items)
		{
			if (Item.Label == "if" && Item.Kind == EOutlineSymbolKind::Keyword)
			{
				bIfIsKeyword = true;
				break;
			}
		}
		if (!Expect(IfResult.bSuccess && bIfIsKeyword, "bare if keyword-only fallback"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		FTextPosition DoPos;
		const std::string DoSource = "void main() {\n\tdo\n}\n";
		if (!Expect(FindPosition(DoSource, "\tdo", DoPos), "locate do"))
		{
			return EXIT_FAILURE;
		}
		DoPos.Column += static_cast<std::uint32_t>(std::string_view("\tdo").size());
		const FCompletionResult DoResult = VeroScriptComplete::CompleteAtSource(
		    DoSource,
		    DoPos,
		    "<do-keyword>");
		if (!Expect(
		        DoResult.bSuccess && HasLabel(DoResult.Items, "do"),
		        "bare do keyword-only fallback"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// The same parse-failure fallback must retain host vocabulary and use the
		// normal fuzzy matcher/ranker instead of the old outer fixed-priority append.
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		if (!Expect(
		        Registry.RegisterNative(
		            "ifHostAction",
		            VeroScript::ENativeType::Void,
		            {},
		            [](const VeroScript::FNativeCallContext&,
		                VeroScript::FValue&,
		                std::string&)
		{
			return true;
		},
		            nullptr,
		            ErrorId),
		        "register fallback host completion"))
		{
			return EXIT_FAILURE;
		}

		FTextPosition IfPos;
		if (!Expect(FindPosition(IfKeywordFallbackSource, "\tif", IfPos), "locate host fallback if"))
		{
			return EXIT_FAILURE;
		}
		IfPos.Column += static_cast<std::uint32_t>(std::string_view("\tif").size());
		FCompletionOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    IfKeywordFallbackSource,
		    IfPos,
		    "<if-host-fallback>",
		    Options);
		const int HostIndex = LabelIndex(Result.Items, "ifHostAction");
		if (!Expect(
		        Result.bSuccess && HostIndex >= 0 && Result.Items[static_cast<std::size_t>(HostIndex)].SortPriority < 100,
		        "keyword-only fallback should include context-ranked host vocabulary"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* DirectiveCompleteSource = R"(
#def
void main() {}
)";

	{
		FTextPosition Pos;
		if (!Expect(FindPosition(DirectiveCompleteSource, "#def", Pos), "locate #def"))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("#def").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    DirectiveCompleteSource,
		    Pos,
		    "<pp-directive-def>");
		bool bDefine = false;
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.Label == "define" && Item.Kind == EOutlineSymbolKind::Keyword)
			{
				bDefine = true;
				break;
			}
		}
		if (!Expect(Result.bSuccess && Result.Prefix == "def" && bDefine, "#def offers define"))
		{
			std::cerr << "prefix=[" << Result.Prefix << "]\n";
			return EXIT_FAILURE;
		}
	}

	constexpr const char* DirectiveHashOnlySource = R"(
#
void main() {}
)";

	{
		FTextPosition Pos;
		if (!Expect(FindPosition(DirectiveHashOnlySource, "#", Pos), "locate #"))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += 1;
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    DirectiveHashOnlySource,
		    Pos,
		    "<pp-directive-hash>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "define") && HasLabel(Result.Items, "include") && HasLabel(Result.Items, "ifdef") && HasLabel(Result.Items, "endif") && HasLabel(Result.Items, "error"),
		        "# alone offers preprocessor directives"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "return"), "# context excludes language keywords"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* DirectiveMacroNameSource = R"(
#define ANSWER 42
#ifdef AN
void main() {}
#endif
)";

	{
		FTextPosition Pos;
		if (!Expect(FindPosition(DirectiveMacroNameSource, "#ifdef AN", Pos), "locate #ifdef AN"))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("#ifdef AN").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    DirectiveMacroNameSource,
		    Pos,
		    "<pp-ifdef-macro>");
		bool bAnswer = false;
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.Label == "ANSWER")
			{
				bAnswer = true;
				break;
			}
		}
		if (!Expect(Result.bSuccess && Result.Prefix == "AN" && bAnswer, "#ifdef AN offers ANSWER"))
		{
			std::cerr << "prefix=[" << Result.Prefix << "]\n";
			return EXIT_FAILURE;
		}
	}

	// Mid-edit often lacks #endif; partial ActiveMacros must still drive macro-name complete.
	constexpr const char* DirectiveMacroNameOpenIfSource = R"(
#define ANSWER 42
#ifdef AN
)";

	{
		FTextPosition Pos;
		if (!Expect(
		        FindPosition(DirectiveMacroNameOpenIfSource, "#ifdef AN", Pos),
		        "locate #ifdef AN open-if"))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("#ifdef AN").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    DirectiveMacroNameOpenIfSource,
		    Pos,
		    "<pp-ifdef-open-if>");
		bool bAnswer = false;
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.Label == "ANSWER")
			{
				bAnswer = true;
				break;
			}
		}
		if (!Expect(
		        Result.bSuccess && bAnswer,
		        "#ifdef AN without #endif still offers ANSWER"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* DirectiveDefinedSource = R"(
#if def
void main() {}
#endif
)";

	{
		FTextPosition Pos;
		if (!Expect(FindPosition(DirectiveDefinedSource, "#if def", Pos), "locate #if def"))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("#if def").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    DirectiveDefinedSource,
		    Pos,
		    "<pp-if-defined>");
		bool bDefined = false;
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.Label == "defined" && Item.InsertText == "defined(")
			{
				bDefined = true;
				break;
			}
		}
		if (!Expect(Result.bSuccess && bDefined, "#if def offers defined("))
		{
			return EXIT_FAILURE;
		}
	}

	// Function-like macros in #ifdef / defined() must insert bare names, not NAME(.
	constexpr const char* PpBareMacroInsertSource = R"(
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#ifdef MA
#if defined(MA
void main() {}
#endif
#endif
)";

	{
		FTextPosition IfdefPos;
		if (!Expect(FindPosition(PpBareMacroInsertSource, "#ifdef MA", IfdefPos), "locate #ifdef MA"))
		{
			return EXIT_FAILURE;
		}
		IfdefPos.Column += static_cast<std::uint32_t>(std::string_view("#ifdef MA").size());
		const FCompletionResult IfdefResult = VeroScriptComplete::CompleteAtSource(
		    PpBareMacroInsertSource,
		    IfdefPos,
		    "<pp-ifdef-bare-max>");
		bool bIfdefBare = false;
		for (const FCompletionItem& Item : IfdefResult.Items)
		{
			if (Item.Label == "MAX" && Item.InsertText == "MAX")
			{
				bIfdefBare = true;
				break;
			}
		}
		if (!Expect(IfdefResult.bSuccess && bIfdefBare, "#ifdef MA inserts bare MAX"))
		{
			return EXIT_FAILURE;
		}

		FTextPosition DefinedPos;
		if (!Expect(
		        FindPosition(PpBareMacroInsertSource, "#if defined(MA", DefinedPos),
		        "locate #if defined(MA"))
		{
			return EXIT_FAILURE;
		}
		DefinedPos.Column +=
		    static_cast<std::uint32_t>(std::string_view("#if defined(MA").size());
		const FCompletionResult DefinedResult = VeroScriptComplete::CompleteAtSource(
		    PpBareMacroInsertSource,
		    DefinedPos,
		    "<pp-defined-bare-max>");
		bool bDefinedBare = false;
		bool bNestedDefined = false;
		for (const FCompletionItem& Item : DefinedResult.Items)
		{
			if (Item.Label == "MAX" && Item.InsertText == "MAX")
			{
				bDefinedBare = true;
			}
			if (Item.Label == "defined")
			{
				bNestedDefined = true;
			}
		}
		if (!Expect(DefinedResult.bSuccess && bDefinedBare, "#if defined(MA inserts bare MAX"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!bNestedDefined, "#if defined(MA suppresses nested defined"))
		{
			return EXIT_FAILURE;
		}
	}

	// Preprocessor also accepts bare `defined NAME` (no parentheses).
	constexpr const char* PpBareDefinedNameSource = R"(
#define ANSWER 42
#if defined AN
void main() {}
#endif
)";

	{
		FTextPosition Pos;
		if (!Expect(FindPosition(PpBareDefinedNameSource, "#if defined AN", Pos), "locate bare defined AN"))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("#if defined AN").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    PpBareDefinedNameSource,
		    Pos,
		    "<pp-bare-defined-name>");
		bool bAnswer = false;
		bool bNestedDefined = false;
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.Label == "ANSWER" && Item.InsertText == "ANSWER")
			{
				bAnswer = true;
			}
			if (Item.Label == "defined")
			{
				bNestedDefined = true;
			}
		}
		if (!Expect(Result.bSuccess && Result.Prefix == "AN" && bAnswer, "#if defined AN offers ANSWER"))
		{
			std::cerr << "prefix=[" << Result.Prefix << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(!bNestedDefined, "#if defined AN suppresses nested defined"))
		{
			return EXIT_FAILURE;
		}
	}

	// CLI -D macros should appear in #ifdef completion.
	constexpr const char* PpCliDefineSource = R"(
#ifdef FE
void main() {}
#endif
)";

	{
		FTextPosition Pos;
		if (!Expect(FindPosition(PpCliDefineSource, "#ifdef FE", Pos), "locate #ifdef FE"))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("#ifdef FE").size());
		FCompletionOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Defines.push_back("FEATURE");
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    PpCliDefineSource,
		    Pos,
		    "<pp-cli-define>",
		    Options);
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "FEATURE"),
		        "#ifdef FE offers CLI -DFEATURE"))
		{
			return EXIT_FAILURE;
		}
	}

	// #define parameter list must not flood language keywords.
	constexpr const char* PpDefineParamsSource = R"(
#define MAX(a, 
void main() {}
)";

	{
		FTextPosition Pos;
		if (!Expect(FindPosition(PpDefineParamsSource, "#define MAX(a, ", Pos), "locate #define params"))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("#define MAX(a, ").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    PpDefineParamsSource,
		    Pos,
		    "<pp-define-params>");
		if (!Expect(Result.bSuccess && !HasLabel(Result.Items, "return"), "#define params excludes keywords"))
		{
			return EXIT_FAILURE;
		}
	}

	// Indented directives: `#` detection must not stop at the caret.
	constexpr const char* PpIndentedIfdefSource = R"(
#define ANSWER 42
	#ifdef AN
void main() {}
#endif
)";

	{
		FTextPosition Pos;
		if (!Expect(FindPosition(PpIndentedIfdefSource, "\t#ifdef AN", Pos), "locate indented #ifdef AN"))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("\t#ifdef AN").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    PpIndentedIfdefSource,
		    Pos,
		    "<pp-indented-ifdef>");
		if (!Expect(
		        Result.bSuccess && Result.Prefix == "AN" && HasLabel(Result.Items, "ANSWER"),
		        "indented #ifdef AN offers ANSWER"))
		{
			std::cerr << "prefix=[" << Result.Prefix << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		FTextPosition Pos;
		if (!Expect(FindPosition(PpIndentedIfdefSource, "\t#ifdef AN", Pos), "locate indent before #"))
		{
			return EXIT_FAILURE;
		}
		// Caret still on the leading tab — must not flood language keywords.
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    PpIndentedIfdefSource,
		    Pos,
		    "<pp-indent-before-hash>");
		if (!Expect(
		        Result.bSuccess && !HasLabel(Result.Items, "return"),
		        "indent before # excludes language keywords"))
		{
			return EXIT_FAILURE;
		}
	}

	// CLI -D shadowed by later #undef must not reappear.
	constexpr const char* PpCliUndefSource = R"(
#undef FEATURE
#ifdef FE
void main() {}
#endif
)";

	{
		FTextPosition Pos;
		if (!Expect(FindPosition(PpCliUndefSource, "#ifdef FE", Pos), "locate #ifdef FE after undef"))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("#ifdef FE").size());
		FCompletionOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Defines.push_back("FEATURE");
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    PpCliUndefSource,
		    Pos,
		    "<pp-cli-undef>",
		    Options);
		if (!Expect(
		        Result.bSuccess && !HasLabel(Result.Items, "FEATURE"),
		        "#undef FEATURE hides CLI -DFEATURE"))
		{
			return EXIT_FAILURE;
		}
	}

	// Backslash-continued #define body must stay PP-silent (no keyword flood).
	const std::string PpDefineContinueSource =
	    "#define F \\\n"
	    "  pu\n"
	    "void main() {}\n";

	{
		FTextPosition Pos;
		if (!Expect(FindPosition(PpDefineContinueSource, "  pu", Pos), "locate continued define body"))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("  pu").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    PpDefineContinueSource,
		    Pos,
		    "<pp-define-continue>");
		if (!Expect(
		        Result.bSuccess && !HasLabel(Result.Items, "public") && !HasLabel(Result.Items, "return"),
		        "continued #define body excludes language keywords"))
		{
			return EXIT_FAILURE;
		}
	}

	// Continued #if expression still offers macros / defined operands.
	const std::string PpIfContinueSource =
	    "#define ANSWER 42\n"
	    "#if defined(FOO) && \\\n"
	    "  defined(AN\n"
	    "void main() {}\n"
	    "#endif\n";

	{
		FTextPosition Pos;
		if (!Expect(FindPosition(PpIfContinueSource, "  defined(AN", Pos), "locate continued #if"))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("  defined(AN").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    PpIfContinueSource,
		    Pos,
		    "<pp-if-continue>");
		bool bAnswer = false;
		bool bNestedDefined = false;
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.Label == "ANSWER")
			{
				bAnswer = true;
			}
			if (Item.Label == "defined")
			{
				bNestedDefined = true;
			}
		}
		if (!Expect(Result.bSuccess && bAnswer && !bNestedDefined, "continued #if defined(AN offers ANSWER"))
		{
			return EXIT_FAILURE;
		}
	}

	// Token split across `\` cannot use single-line replace — stay silent.
	// Use escaped ordinary strings so translation-phase-2 does not splice the test source.
	// Use TAIL (not SWER) so FindPosition cannot hit inside ANSWER.
	const std::string PpSplitIdentSource =
	    "#define ANSWER 42\n"
	    "#if AN\\\n"
	    "TAIL\n"
	    "void main() {}\n"
	    "#endif\n";

	{
		FTextPosition Pos;
		if (!Expect(FindPosition(PpSplitIdentSource, "TAIL", Pos), "locate split TAIL"))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("TAIL").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    PpSplitIdentSource,
		    Pos,
		    "<pp-split-ident>");
		if (!Expect(
		        Result.bSuccess && !HasLabel(Result.Items, "ANSWER") && !HasLabel(Result.Items, "return"),
		        "split #if AN\\TAIL stays silent"))
		{
			return EXIT_FAILURE;
		}
	}

	// Comments are not `defined` operands (match preprocessor comment stripping).
	constexpr const char* PpDefinedInCommentSource = R"(
#if /* defined( */ def
void main() {}
#endif
)";

	{
		FTextPosition Pos;
		if (!Expect(
		        FindPosition(PpDefinedInCommentSource, "#if /* defined( */ def", Pos),
		        "locate def after comment"))
		{
			return EXIT_FAILURE;
		}
		Pos.Column +=
		    static_cast<std::uint32_t>(std::string_view("#if /* defined( */ def").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    PpDefinedInCommentSource,
		    Pos,
		    "<pp-defined-in-comment>");
		bool bDefined = false;
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.Label == "defined" && Item.InsertText == "defined(")
			{
				bDefined = true;
				break;
			}
		}
		if (!Expect(Result.bSuccess && bDefined, "#if /* defined( */ def offers defined("))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* MacroCompleteSource = R"(
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define ANSWER 42
void main() {
	MA
	ANS
}
)";

	{
		FTextPosition MaxPos;
		if (!Expect(FindPosition(MacroCompleteSource, "\tMA", MaxPos), "locate MA"))
		{
			return EXIT_FAILURE;
		}
		MaxPos.Column += static_cast<std::uint32_t>(std::string_view("\tMA").size());
		const FCompletionResult MaxResult = VeroScriptComplete::CompleteAtSource(
		    MacroCompleteSource,
		    MaxPos,
		    "<macro-fn-complete>");
		bool bMaxOk = false;
		for (const FCompletionItem& Item : MaxResult.Items)
		{
			if (Item.Label == "MAX" && Item.Kind == EOutlineSymbolKind::Macro && Item.InsertText == "MAX(" && Item.Detail.find("#define MAX(a, b)") != std::string::npos)
			{
				bMaxOk = true;
				break;
			}
		}
		if (!Expect(MaxResult.bSuccess && bMaxOk, "function-like macro completion"))
		{
			std::cerr << "items=";
			for (const FCompletionItem& Item : MaxResult.Items)
			{
				std::cerr << Item.Label << '/' << Item.InsertText << '/' << Item.Detail << ',';
			}
			std::cerr << '\n';
			return EXIT_FAILURE;
		}

		FTextPosition AnswerPos;
		if (!Expect(FindPosition(MacroCompleteSource, "\tANS", AnswerPos), "locate ANS"))
		{
			return EXIT_FAILURE;
		}
		AnswerPos.Column += static_cast<std::uint32_t>(std::string_view("\tANS").size());
		const FCompletionResult AnswerResult = VeroScriptComplete::CompleteAtSource(
		    MacroCompleteSource,
		    AnswerPos,
		    "<macro-obj-complete>");
		bool bAnswerOk = false;
		for (const FCompletionItem& Item : AnswerResult.Items)
		{
			if (Item.Label == "ANSWER" && Item.InsertText == "ANSWER" && Item.Detail.find("#define ANSWER") != std::string::npos)
			{
				bAnswerOk = true;
				break;
			}
		}
		if (!Expect(AnswerResult.bSuccess && bAnswerOk, "object-like macro completion"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		struct FMacroDocumentationCase
		{
			const char* Source;
			const char* Expected;
		};
		const FMacroDocumentationCase Cases[] = {
		    {"/// Current macro.\n// Extra detail.\n#define DOC 1\nvoid main() {\n\tDO\n}\n",
		        "Current macro.\nExtra detail."},
		    {"/** Block macro. */\n#define DOC 1\nvoid main() {\n\tDO\n}\n",
		        "Block macro."},
		    {"/// Continued macro.\n#define \\\nDOC 1\nvoid main() {\n\tDO\n}\n",
		        "Continued macro."},
		    {"// Detached.\n\n#define DOC 1\nvoid main() {\n\tDO\n}\n",
		        ""},
		    {"// Old.\n#define DOC 1\n// New.\n#define DOC 2\nvoid main() {\n\tDO\n}\n",
		        "New."},
		};
		for (const FMacroDocumentationCase& Case : Cases)
		{
			FTextPosition Position;
			if (!Expect(FindPosition(Case.Source, "\tDO", Position),
			        "locate documented macro completion"))
			{
				return EXIT_FAILURE;
			}
			Position.Column += 3;
			const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
			    Case.Source, Position, "<macro-documentation>");
			bool bFound = false;
			for (const FCompletionItem& Item : Result.Items)
			{
				if (Item.Label == "DOC" && Item.Kind == EOutlineSymbolKind::Macro)
				{
					bFound = Item.Documentation == Case.Expected;
					break;
				}
			}
			if (!Expect(Result.bSuccess && bFound,
			        "macro completion uses adjacent documentation from active definition"))
			{
				return EXIT_FAILURE;
			}
		}
		constexpr const char* DirectiveSource =
		    "/// Preprocessor macro.\n#define DOC 1\n#ifdef DO\n#endif\n";
		FTextPosition DirectivePosition;
		if (!Expect(FindPosition(DirectiveSource, "#ifdef DO", DirectivePosition),
		        "locate documented preprocessor macro completion"))
		{
			return EXIT_FAILURE;
		}
		DirectivePosition.Column += 9;
		const FCompletionResult DirectiveResult = VeroScriptComplete::CompleteAtSource(
		    DirectiveSource, DirectivePosition, "<macro-directive-documentation>");
		bool bDirectiveDocumented = false;
		for (const FCompletionItem& Item : DirectiveResult.Items)
		{
			if (Item.Label == "DOC" && Item.Kind == EOutlineSymbolKind::Macro)
			{
				bDirectiveDocumented = Item.Documentation == "Preprocessor macro.";
				break;
			}
		}
		if (!Expect(DirectiveResult.bSuccess && bDirectiveDocumented,
		        "preprocessor macro completion includes leading documentation"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* MacroRedefineCompleteSource = R"(
#define VALUE 1
#define VALUE 99
void main() {
	VAL
}
)";

	{
		FTextPosition Pos;
		if (!Expect(FindPosition(MacroRedefineCompleteSource, "\tVAL", Pos), "locate VAL"))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("\tVAL").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    MacroRedefineCompleteSource,
		    Pos,
		    "<macro-redefine-complete>");
		bool bOk = false;
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.Label == "VALUE" && Item.Detail.find("99") != std::string::npos)
			{
				bOk = true;
				break;
			}
		}
		if (!Expect(Result.bSuccess && bOk, "macro redefine completion uses last define"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* MacroUndefCompleteSource = R"(
#define TEMP 1
#undef TEMP
void main() {
	TEM
}
)";

	{
		FTextPosition Pos;
		if (!Expect(FindPosition(MacroUndefCompleteSource, "\tTEM", Pos), "locate TEM"))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("\tTEM").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    MacroUndefCompleteSource,
		    Pos,
		    "<macro-undef-complete>");
		bool bHasTemp = false;
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.Label == "TEMP")
			{
				bHasTemp = true;
				break;
			}
		}
		if (!Expect(Result.bSuccess && !bHasTemp, "undef'd macro not offered in completion"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* MacroUseBeforeUndefCompleteSource = R"(
#define TEMP 1
void main() {
	TEM
}
#undef TEMP
)";

	{
		FTextPosition Pos;
		if (!Expect(FindPosition(MacroUseBeforeUndefCompleteSource, "\tTEM", Pos), "locate TEM before undef"))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("\tTEM").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    MacroUseBeforeUndefCompleteSource,
		    Pos,
		    "<macro-use-before-undef-complete>");
		bool bOk = false;
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.Label == "TEMP" && Item.Detail.find("1") != std::string::npos)
			{
				bOk = true;
				break;
			}
		}
		if (!Expect(Result.bSuccess && bOk, "macro still offered before later undef"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* MacroUndefRedefineCompleteSource = R"(
#define TEMP 1
#undef TEMP
#define TEMP 7
void main() {
	TEM
}
)";

	{
		FTextPosition Pos;
		if (!Expect(FindPosition(MacroUndefRedefineCompleteSource, "\tTEM", Pos), "locate TEM redefine"))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("\tTEM").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    MacroUndefRedefineCompleteSource,
		    Pos,
		    "<macro-undef-redefine-complete>");
		bool bOk = false;
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.Label == "TEMP" && Item.Detail.find("7") != std::string::npos)
			{
				bOk = true;
				break;
			}
		}
		if (!Expect(Result.bSuccess && bOk, "macro after undef+redefine uses latest define"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* FormatCompleteSource = R"(
void main() {
	forma
}
)";

	{
		FTextPosition Position;
		if (!Expect(FindPosition(FormatCompleteSource, "\tforma", Position), "locate forma"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\tforma").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    FormatCompleteSource,
		    Position,
		    "<format-complete>");
		if (!Expect(Result.bSuccess && HasLabel(Result.Items, "format"), "host format"))
		{
			return EXIT_FAILURE;
		}
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.Label == "format" && !Expect(Item.Detail.find("...") != std::string::npos, "format completion Detail has varargs"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	constexpr const char* CaptureSource = R"(
int g_base = 40;

void main() {
	int base = 1;
	fn(int)->int add = [bas
}
)";

	{
		FTextPosition Position;
		if (!Expect(FindPosition(CaptureSource, "[bas", Position), "locate capture prefix"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("[bas").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    CaptureSource,
		    Position,
		    "<capture-complete>");
		if (!Expect(
		        Result.bSuccess && Result.Prefix == "bas" && HasLabel(Result.Items, "base"),
		        "capture local base with EndExclusive prefix"))
		{
			std::cerr << "prefix=[" << Result.Prefix << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "print"), "capture should not flood host builtins"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* CaptureBeforeLexErrorSource = R"(
int g_base = 40;

void main() {
	int base = 1;
	fn(int)->int add = [bas
}
/*
)";

	{
		FTextPosition Position;
		if (!Expect(
		        FindPosition(CaptureBeforeLexErrorSource, "[bas", Position),
		        "locate capture before lex error"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("[bas").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    CaptureBeforeLexErrorSource,
		    Position,
		    "<capture-before-lex-error>");
		if (!Expect(
		        Result.bSuccess && Result.Prefix == "bas" && HasLabel(Result.Items, "base"),
		        "soft capture despite later lex error"))
		{
			std::cerr << "prefix=[" << Result.Prefix << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(HasLabel(Result.Items, "g_base"), "soft capture also offers module binding"))
		{
			return EXIT_FAILURE;
		}
	}

	// Capture / goto / member soft paths must not fall through to keyword-only.
	constexpr const char* CaptureNoKeywordFloodSource = R"(
void main() {
	fn(int)->int add = [pu
}
/*
)";

	{
		FTextPosition Position;
		if (!Expect(
		        FindPosition(CaptureNoKeywordFloodSource, "[pu", Position),
		        "locate capture pu"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("[pu").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    CaptureNoKeywordFloodSource,
		    Position,
		    "<capture-no-keyword-flood>");
		if (!Expect(Result.bSuccess && Result.Prefix == "pu", "capture context keeps prefix"))
		{
			std::cerr << "prefix=[" << Result.Prefix << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
		        !HasLabel(Result.Items, "public") && !HasLabel(Result.Items, "private"),
		        "capture soft must not flood keywords"))
		{
			return EXIT_FAILURE;
		}
	}

	// Same name: local `base` must win over module-level `base` in capture lists.
	constexpr const char* CaptureShadowSource = R"(
string base = "global";

void main() {
	int base = 1;
	fn(int)->int add = [bas
}
)";

	{
		FTextPosition Position;
		if (!Expect(FindPosition(CaptureShadowSource, "[bas", Position), "locate capture shadow"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("[bas").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    CaptureShadowSource,
		    Position,
		    "<capture-shadow>");
		if (!Expect(Result.bSuccess && HasLabel(Result.Items, "base"), "capture shadowed base"))
		{
			return EXIT_FAILURE;
		}
		std::string BaseDetail;
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.Label == "base")
			{
				BaseDetail = Item.Detail;
				break;
			}
		}
		if (!Expect(
		        BaseDetail.find("int") != std::string::npos && BaseDetail.find("string") == std::string::npos,
		        "capture Detail should be local int base, not module string base"))
		{
			std::cerr << "detail=[" << BaseDetail << "]\n";
			return EXIT_FAILURE;
		}
	}

	constexpr const char* CaptureGlobalSource = R"(
int g_base = 40;

void main() {
	fn(int)->int add = [g_
}
)";

	{
		FTextPosition Position;
		if (!Expect(FindPosition(CaptureGlobalSource, "[g_", Position), "locate capture g_"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("[g_").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    CaptureGlobalSource,
		    Position,
		    "<capture-global>");
		if (!Expect(Result.bSuccess && HasLabel(Result.Items, "g_base"), "capture global g_base"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* ReferenceCaptureSource = R"(
int g_base = 40;

void main() {
	int base = 1;
	fn()->int read = [=, &bas
}
)";

	{
		FTextPosition Position;
		if (!Expect(
		        FindPosition(ReferenceCaptureSource, "[=, &bas", Position),
		        "locate reference capture prefix"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("[=, &bas").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    ReferenceCaptureSource,
		    Position,
		    "<reference-capture-complete>");
		if (!Expect(
		        Result.bSuccess && Result.Prefix == "&bas" && HasLabel(Result.Items, "&base") && !HasLabel(Result.Items, "base"),
		        "reference capture should replace the complete &name spelling"))
		{
			std::cerr << "prefix=[" << Result.Prefix << "]\n";
			return EXIT_FAILURE;
		}
	}

	constexpr const char* ReferenceDefaultCaptureSource = R"(
void main() {
	int value = 1;
	fn()->int read = [&
}
)";

	{
		FTextPosition Position;
		if (!Expect(
		        FindPosition(ReferenceDefaultCaptureSource, "[&", Position),
		        "locate reference default capture"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("[&").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    ReferenceDefaultCaptureSource,
		    Position,
		    "<reference-default-capture-complete>");
		if (!Expect(
		        Result.bSuccess && Result.Prefix == "&" && HasLabel(Result.Items, "&") && HasLabel(Result.Items, "&value"),
		        "reference capture should offer default and explicit forms"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* MethodChainSource = R"(
struct Point {
	int x;
}

class Box { public: Box() {}
	Point getPoint() { return Point(); } }

void main() {
	Box b = Box();
	b.getPoint().
}
)";

	{
		FTextPosition Position;
		if (!Expect(FindPosition(MethodChainSource, "b.getPoint().", Position), "locate method chain"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("b.getPoint().").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    MethodChainSource,
		    Position,
		    "<method-chain>");
		if (!Expect(Result.bSuccess && HasLabel(Result.Items, "x"), "user method return chain .x"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::error_code RemoveError;
		const std::filesystem::path TempRoot =
		    std::filesystem::temp_directory_path() / "VeroScriptTooling-import-complete";
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot / "lib");
		{
			std::ofstream Lib(TempRoot / "lib" / "math.vs");
			Lib << "export int add(int a, int b) { return a + b; }\n";
		}
		{
			std::ofstream Lib(TempRoot / "lib" / "io.vs");
			Lib << "export void write(string s) { print(s); }\n";
		}

		FCompletionOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		const std::string LibPath = (TempRoot / "lib").string();
		Options.ImportPaths.push_back(LibPath);
		for (const std::string& Source : {std::string("import \"ma"), std::string("#include \"ma")})
		{
			FTextPosition Position{
			    1,
			    static_cast<std::uint32_t>(Source.size() + 1)};
			const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
			    Source,
			    Position,
			    (TempRoot / "app.vs").string(),
			    Options);
			if (!Expect(
			        Result.bSuccess && HasLabel(Result.Items, "math.vs"),
			        "module import path completion finds math.vs"))
			{
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
			const FCompletionItem* MathItem = nullptr;
			for (const FCompletionItem& Item : Result.Items)
			{
				if (Item.Label == "math.vs")
				{
					MathItem = &Item;
					break;
				}
			}
			const bool bInclude = Source.rfind("#include", 0) == 0;
			if (!Expect(
			        MathItem && MathItem->Detail == (bInclude ? "re-exported module" : "module"),
			        "module path completion should describe include re-export semantics"))
			{
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
			if (!Expect(
			        !HasLabel(Result.Items, "io.vs"),
			        "module import prefix filters io.vs"))
			{
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
		}
		std::filesystem::remove_all(TempRoot, RemoveError);
	}

	// Restructured checkout/runtime layout: Tooling deploys and discovers the built-in stdlib
	// without requiring callers to add VeroScript/Resources/Scripts/stdlib explicitly.
	{
		constexpr const char* Source = "import \"ma";
		FTextPosition Position{
		    1,
		    static_cast<std::uint32_t>(std::string_view(Source).size() + 1)};
		const FCompletionResult Result =
		    VeroScriptComplete::CompleteAtSource(Source, Position, "<stdlib-import-complete>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "math.vs"),
		        "default stdlib import completion finds math.vs"))
		{
			std::cerr << "items=";
			for (const FCompletionItem& Item : Result.Items)
			{
				std::cerr << Item.Label << ',';
			}
			std::cerr << '\n';
			return EXIT_FAILURE;
		}
	}

	// Sealed 2.47: multi-parent interface + specialized inherited member details.
	{
		constexpr const char* MultiParentSource = R"(
interface IA<T> {
	virtual T getA() = 0;
}
interface IB {
	virtual void b() = 0;
}
interface IC : IA<int>, IB {
	virtual void c() = 0;
}

void use(IC x) {
	x.
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(MultiParentSource, "x.", Position), "locate multi-parent x."))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("x.").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    MultiParentSource,
		    Position,
		    "<multi-parent>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "getA") && HasLabel(Result.Items, "b") && HasLabel(Result.Items, "c"),
		        "multi-parent interface members"))
		{
			return EXIT_FAILURE;
		}
		bool bGetASpecialized = false;
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.Label == "getA" && Item.Detail.find("int") != std::string::npos)
			{
				bGetASpecialized = true;
			}
		}
		if (!Expect(bGetASpecialized, "IA<int>.getA detail specializes T→int"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* PairSource = R"(
void main() {
	pair<int, string> p = pair<int, string>();
	p.
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(PairSource, "p.", Position), "locate Pair p."))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("p.").size());
		const FCompletionResult Result =
		    VeroScriptComplete::CompleteAtSource(PairSource, Position, "<pair>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "first") && HasLabel(Result.Items, "second"),
		        "Pair soft fields first/second"))
		{
			return EXIT_FAILURE;
		}
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.Label == "first" && Item.Kind == EOutlineSymbolKind::Method)
			{
				return Fail("Pair.first must remain a field, not a soft method");
			}
		}
	}

	{
		constexpr const char* MacroLocal = R"(
#define LINE \
    int item = 1;

void main() {
    LINE
    ite
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(MacroLocal, "    ite", Position), "locate macro-expanded item prefix"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("    ite").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    MacroLocal,
		    Position,
		    "<macro-local>");
		if (!Expect(Result.bSuccess && HasLabel(Result.Items, "item"), "complete item from macro line"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::error_code RemoveError;
		const std::filesystem::path TempRoot =
		    std::filesystem::temp_directory_path() / "VeroScriptTooling-events-complete";
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot / "lib");
		{
			std::ofstream Lib(TempRoot / "lib" / "delegate.vs");
			Lib << "export class multicast_delegate {\n"
			       "public:\n"
			       "    void broadcast() {}\n"
			       "    void add(fn()->void h) {}\n"
			       "    int count() {}\n"
			       "}\n"
			       "export class delegate<R, A> {\n"
			       "public:\n"
			       "    R execute(A a) {}\n"
			       "    void bind(fn(A)->R h) {}\n"
			       "    bool is_bound() { return false; }\n"
			       "}\n";
		}
		const std::string Source = R"(
import "delegate.vs";
void main() {
	multicast_delegate md = multicast_delegate();
	md.
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "\tmd.", Position), "locate imported multicast_delegate md."))
		{
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\tmd.").size());
		const std::string LibPath = (TempRoot / "lib").string();
		const std::string AppPath = (TempRoot / "app.vs").string();
		FCompletionOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.ImportPaths.push_back(LibPath);
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    Source,
		    Position,
		    AppPath,
		    Options);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "broadcast") && HasLabel(Result.Items, "add") && HasLabel(Result.Items, "count"),
		        "imported multicast_delegate members"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* SoftMulticastSource = R"(
void main() {
	multicast_delegate<int> md = multicast_delegate<int>();
	md.
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(SoftMulticastSource, "\tmd.", Position),
		        "locate soft multicast_delegate<int> md."))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\tmd.").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    SoftMulticastSource,
		    Position,
		    "<soft-multicast>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "broadcast") && HasLabel(Result.Items, "add"),
		        "soft multicast_delegate<int> members without import"))
		{
			return EXIT_FAILURE;
		}
		bool bBroadcastSpecialized = false;
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.Label == "broadcast" && Item.Detail.find("int") != std::string::npos)
			{
				bBroadcastSpecialized = true;
			}
		}
		if (!Expect(bBroadcastSpecialized, "multicast_delegate<int>.broadcast specializes A→int"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* DelegateSource = R"(
void main() {
	delegate<int, int> d = delegate<int, int>();
	d.
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(DelegateSource, "\td.", Position), "locate delegate d."))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\td.").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    DelegateSource,
		    Position,
		    "<delegate>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "execute") && HasLabel(Result.Items, "execute_if_bound") && HasLabel(Result.Items, "bind") && HasLabel(Result.Items, "is_bound") && HasLabel(Result.Items, "unbind") && !HasLabel(Result.Items, "Bind") && !HasLabel(Result.Items, "Execute"),
		        "soft delegate members"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* ThreeParamDelegateSource = R"(
void main() {
	delegate<int, int, string, bool> d = delegate<int, int, string, bool>();
	d.
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(ThreeParamDelegateSource, "\td.", Position),
		        "locate three-param delegate d."))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\td.").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    ThreeParamDelegateSource,
		    Position,
		    "<delegate-three-param>");
		bool bExecuteSpecialized = false;
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.Label == "execute" && Item.Detail.find("string") != std::string::npos && Item.Detail.find("bool") != std::string::npos)
			{
				bExecuteSpecialized = true;
			}
		}
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "execute_if_bound") && bExecuteSpecialized,
		        "soft three-parameter delegate members specialize all arguments"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Namespace-qualified generic args must not break head detection
		// (StripTypeDecorations must only strip qualifiers before '<').
		constexpr const char* NamespacedArgSource = R"(
namespace ns {
	export class Foo {}
}
void main() {
	delegate<ns.Foo, int> d = delegate<ns.Foo, int>();
	d.
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(NamespacedArgSource, "\td.", Position),
		        "locate delegate<ns.Foo, int> d."))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\td.").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    NamespacedArgSource,
		    Position,
		    "<delegate-ns-arg>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "execute") && HasLabel(Result.Items, "bind") && HasLabel(Result.Items, "is_bound"),
		        "soft delegate members with namespaced generic arg"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Nested namespaces: offer the next segment, and keep the full qualifier.
		constexpr const char* NestedNsSource = R"(
namespace A {
	namespace B {
		export class C {
		public:
			int value;
		}
	}
}
void main() {
	A.
}
)";
		FTextPosition MidPos;
		if (!Expect(FindPosition(NestedNsSource, "\tA.", MidPos), "locate nested A."))
		{
			return EXIT_FAILURE;
		}
		MidPos.Column += static_cast<std::uint32_t>(std::string_view("\tA.").size());
		const FCompletionResult MidResult = VeroScriptComplete::CompleteAtSource(
		    NestedNsSource,
		    MidPos,
		    "<nested-ns-mid>");
		if (!Expect(
		        MidResult.bSuccess && HasLabel(MidResult.Items, "B"),
		        "A. offers nested namespace segment B"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* NestedNsDeepSource = R"(
namespace A {
	namespace B {
		export class C {
		public:
			int value;
		}
	}
}
void main() {
	A.B.
}
)";
		FTextPosition DeepPos;
		if (!Expect(FindPosition(NestedNsDeepSource, "\tA.B.", DeepPos), "locate nested A.B."))
		{
			return EXIT_FAILURE;
		}
		DeepPos.Column += static_cast<std::uint32_t>(std::string_view("\tA.B.").size());
		const FCompletionResult DeepResult = VeroScriptComplete::CompleteAtSource(
		    NestedNsDeepSource,
		    DeepPos,
		    "<nested-ns-deep>");
		if (!Expect(
		        DeepResult.bSuccess && HasLabel(DeepResult.Items, "C"),
		        "A.B. offers nested type C"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* NestedNsScopeSource = R"(
namespace A::B {
	export class C {
	public:
		int value;
	}
}
void main() {
	A::B::
}
)";
		FTextPosition ScopePos;
		if (!Expect(FindPosition(NestedNsScopeSource, "\tA::B::", ScopePos), "locate A::B::"))
		{
			return EXIT_FAILURE;
		}
		ScopePos.Column += static_cast<std::uint32_t>(std::string_view("\tA::B::").size());
		const FCompletionResult ScopeResult = VeroScriptComplete::CompleteAtSource(
		    NestedNsScopeSource,
		    ScopePos,
		    "<nested-ns-scope>");
		if (!Expect(
		        ScopeResult.bSuccess && HasLabel(ScopeResult.Items, "C"),
		        "A::B:: offers nested type C"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* ConcurrentSource = R"(
void main() {
	std::concurrent::
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(ConcurrentSource, "\tstd::concurrent::", Position), "locate concurrent namespace"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\tstd::concurrent::").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    ConcurrentSource,
		    Position,
		    "<concurrent-soft>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "spawn") && HasLabel(Result.Items, "make_channel") && HasDetail(Result.Items, "send", "channel<T> ch, T value") && HasDetail(Result.Items, "recv", "T std::concurrent::recv(channel<T> ch);") && HasDetail(Result.Items, "close", "void std::concurrent::close(channel<T> ch);") && HasLabel(Result.Items, "when_all") && HasLabel(Result.Items, "make_mutex") && HasLabel(Result.Items, "make_semaphore") && HasLabel(Result.Items, "make_synchronized") && HasLabel(Result.Items, "mutex") && HasLabel(Result.Items, "counting_semaphore"),
		        "qualified concurrent helpers from concurrent.vs"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* DequeSource = R"(
void main() {
	deque<int> d = deque<int>();
	d.
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(DequeSource, "\td.", Position), "locate deque d."))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\td.").size());
		const FCompletionResult Result =
		    VeroScriptComplete::CompleteAtSource(DequeSource, Position, "<deque>");
		if (!Expect(Result.bSuccess && HasLabel(Result.Items, "values"), "deque.values soft member"))
		{
			return EXIT_FAILURE;
		}
		bool bValuesSpecialized = false;
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.Label == "values" && Item.Detail.find("array<int>") != std::string::npos)
			{
				bValuesSpecialized = true;
			}
		}
		if (!Expect(bValuesSpecialized, "deque<int>.values Detail specializes T→int"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* AliasMulticast = R"(
void main() {
	using Score = multicast_delegate<int>;
	Score s = Score();
	s.
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(AliasMulticast, "\ts.", Position), "locate aliased multicast_delegate s."))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\ts.").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    AliasMulticast,
		    Position,
		    "<alias-multicast>");
		if (!Expect(Result.bSuccess && HasLabel(Result.Items, "broadcast"), "alias multicast_delegate broadcast"))
		{
			return EXIT_FAILURE;
		}
		bool bBroadcastSpecialized = false;
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.Label == "broadcast" && Item.Detail.find("int") != std::string::npos)
			{
				bBroadcastSpecialized = true;
			}
		}
		if (!Expect(bBroadcastSpecialized, "local using Score=multicast_delegate<int> preserves arity"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* SoftTypePrefix = R"(
void main() {
	multi
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(SoftTypePrefix, "\tmulti", Position), "locate multicast_delegate prefix"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\tmulti").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    SoftTypePrefix,
		    Position,
		    "<soft-md-type>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "multicast_delegate"),
		        "soft multicast_delegate type completion label"))
		{
			return EXIT_FAILURE;
		}
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.Label == "multicast_delegate" && Item.InsertText.find('[') != std::string::npos)
			{
				return Fail("multicast_delegate InsertText must not contain fake brackets");
			}
		}
	}

	{
		constexpr const char* SoftDelegateType = R"(
void main() {
	deleg
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(SoftDelegateType, "\tdeleg", Position), "locate delegate prefix"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\tdeleg").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    SoftDelegateType,
		    Position,
		    "<soft-delegate-type>");
		if (!Expect(Result.bSuccess && HasLabel(Result.Items, "delegate"), "soft delegate type label"))
		{
			return EXIT_FAILURE;
		}
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.Label == "delegate" && Item.InsertText != "delegate")
			{
				return Fail("delegate InsertText must be bare delegate");
			}
		}
	}

	// Interface state/default members are available through both interface and
	// concrete receivers, including inherited diamond-deduplicated fields.
	{
		constexpr const char* InterfaceStateSource = R"(
interface IBase {
	int value = 1;
	int get() { return this.value; }
}
interface ILeft : IBase {}
interface IRight : IBase {}
class Box : public ILeft, public IRight {
public:
}
void use(Box box) {
	box.
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(InterfaceStateSource, "box.", Position),
		        "locate interface-state completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("box.").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    InterfaceStateSource,
		    Position,
		    "<interface-state>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "value") && HasLabel(Result.Items, "get"),
		        "interface fields/default methods complete on concrete receiver"))
		{
			return EXIT_FAILURE;
		}
		std::size_t ValueCount = 0;
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.Label == "value")
			{
				++ValueCount;
			}
		}
		if (!Expect(ValueCount == 1, "diamond interface field completion deduplicated"))
		{
			return EXIT_FAILURE;
		}
	}

	// Private interface helpers must not complete outside the interface body.
	{
		constexpr const char* PrivateOutside = R"(
interface ICalc {
public:
	int value() { return this.secret(); }
private:
	int secret() { return 42; }
}
void use(ICalc c) {
	c.
}
)";
		FTextPosition OutsidePos;
		if (!Expect(FindPosition(PrivateOutside, "c.", OutsidePos), "locate private-iface out"))
		{
			return EXIT_FAILURE;
		}
		OutsidePos.Column += static_cast<std::uint32_t>(std::string_view("c.").size());
		const FCompletionResult Outside = VeroScriptComplete::CompleteAtSource(
		    PrivateOutside,
		    OutsidePos,
		    "<private-iface-out>");
		if (!Expect(
		        Outside.bSuccess && HasLabel(Outside.Items, "value") && !HasLabel(Outside.Items, "secret"),
		        "private interface method hidden outside"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* PrivateInside = R"(
interface ICalc {
private:
	int secret() { return 42; }
public:
	int value() {
		this.
	}
}
)";
		FTextPosition InsidePos;
		if (!Expect(FindPosition(PrivateInside, "this.", InsidePos), "locate private-iface in"))
		{
			return EXIT_FAILURE;
		}
		InsidePos.Column += static_cast<std::uint32_t>(std::string_view("this.").size());
		const FCompletionResult Inside = VeroScriptComplete::CompleteAtSource(
		    PrivateInside,
		    InsidePos,
		    "<private-iface-in>");
		if (!Expect(
		        Inside.bSuccess && HasLabel(Inside.Items, "secret"),
		        "private interface method visible inside interface default"))
		{
			std::cerr << "items=";
			for (const FCompletionItem& Item : Inside.Items)
			{
				std::cerr << Item.Label << ',';
			}
			std::cerr << '\n';
			return EXIT_FAILURE;
		}
	}

	// Private inheritance: base public members must not complete on an outside receiver,
	// but remain visible on `this.` inside the derived type.
	{
		constexpr const char* PrivateInheritOut = R"(
class Base {
public:
	int visible() { return 1; }
}

class Child : private Base {
}

void main() {
	Child c = Child();
	c.
}
)";
		FTextPosition OutsidePos;
		if (!Expect(
		        FindPosition(PrivateInheritOut, "c.", OutsidePos),
		        "locate private-inherit out"))
		{
			return EXIT_FAILURE;
		}
		OutsidePos.Column += static_cast<std::uint32_t>(std::string_view("c.").size());
		const FCompletionResult Outside = VeroScriptComplete::CompleteAtSource(
		    PrivateInheritOut,
		    OutsidePos,
		    "<private-inherit-out>");
		if (!Expect(
		        Outside.bSuccess && !HasLabel(Outside.Items, "visible"),
		        "private inheritance hides base public members outside"))
		{
			std::cerr << "items=";
			for (const FCompletionItem& Item : Outside.Items)
			{
				std::cerr << Item.Label << ',';
			}
			std::cerr << '\n';
			return EXIT_FAILURE;
		}

		constexpr const char* PrivateInheritIn = R"(
class Base {
public:
	int visible() { return 1; }
}

class Child : private Base {
public:
	int use() {
		this.
	}
}
)";
		FTextPosition InsidePos;
		if (!Expect(
		        FindPosition(PrivateInheritIn, "this.", InsidePos),
		        "locate private-inherit in"))
		{
			return EXIT_FAILURE;
		}
		InsidePos.Column += static_cast<std::uint32_t>(std::string_view("this.").size());
		const FCompletionResult Inside = VeroScriptComplete::CompleteAtSource(
		    PrivateInheritIn,
		    InsidePos,
		    "<private-inherit-in>");
		if (!Expect(
		        Inside.bSuccess && HasLabel(Inside.Items, "visible"),
		        "private inheritance keeps base members on this inside derived"))
		{
			std::cerr << "items=";
			for (const FCompletionItem& Item : Inside.Items)
			{
				std::cerr << Item.Label << ',';
			}
			std::cerr << '\n';
			return EXIT_FAILURE;
		}
	}

	// multi-level: C : public B : private A — A's public members must NOT complete
	// on `this.` inside C (compiler re-homes private inheritance onto B only).
	{
		constexpr const char* MultiPrivate = R"(
class A {
public:
	int visible() { return 1; }
}

class B : private A {
}

class C : public B {
public:
	int use() {
		this.
	}
}
)";
		FTextPosition Pos;
		if (!Expect(FindPosition(MultiPrivate, "this.", Pos), "locate multi-private this."))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("this.").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    MultiPrivate,
		    Pos,
		    "<multi-private-inherit>");
		if (!Expect(
		        Result.bSuccess && !HasLabel(Result.Items, "visible"),
		        "intermediate private inheritance hides A::visible inside C"))
		{
			std::cerr << "items=";
			for (const FCompletionItem& Item : Result.Items)
			{
				std::cerr << Item.Label << ',';
			}
			std::cerr << '\n';
			return EXIT_FAILURE;
		}
	}

	// Private members of A stay inaccessible inside B even with B : private A.
	{
		constexpr const char* PrivateMember = R"(
class A {
private:
	int secret() { return 1; }
public:
	int ok() { return 2; }
}

class B : private A {
public:
	int use() {
		this.
	}
}
)";
		FTextPosition Pos;
		if (!Expect(FindPosition(PrivateMember, "this.", Pos), "locate private-member this."))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("this.").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    PrivateMember,
		    Pos,
		    "<private-member-via-private-inherit>");
		if (!Expect(
		        Result.bSuccess && !HasLabel(Result.Items, "secret") && HasLabel(Result.Items, "ok"),
		        "A::private stays hidden inside B : private A; public is re-homed"))
		{
			std::cerr << "items=";
			for (const FCompletionItem& Item : Result.Items)
			{
				std::cerr << Item.Label << ',';
			}
			std::cerr << '\n';
			return EXIT_FAILURE;
		}
	}

	// Diamond / first-declared base wins: Mid : private A listed before public A.
	// Outside C must not see A::visible (compiler resolves via Mid's private path).
	{
		constexpr const char* Diamond = R"(
class A {
public:
	int visible() { return 1; }
}

class Mid : private A {
}

class C : public Mid, public A {
}

void main() {
	C c = C();
	c.
}
)";
		FTextPosition Pos;
		if (!Expect(FindPosition(Diamond, "c.", Pos), "locate diamond c."))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("c.").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    Diamond,
		    Pos,
		    "<diamond-private-first>");
		if (!Expect(
		        Result.bSuccess && !HasLabel(Result.Items, "visible"),
		        "first-declared private path hides A::visible outside C"))
		{
			std::cerr << "items=";
			for (const FCompletionItem& Item : Result.Items)
			{
				std::cerr << Item.Label << ',';
			}
			std::cerr << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Underscore-insensitive: tostring → to_string
		constexpr const char* FoldSource = R"(
void main() {
	tostring
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(FoldSource, "tostring", Position), "locate tostring prefix"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("tostring").size());
		const FCompletionResult Result =
		    VeroScriptComplete::CompleteAtSource(FoldSource, Position, "<fold-to-string>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "to_string"),
		        "tostring should fuzzy-match to_string"))
		{
			std::cerr << "items=";
			for (const FCompletionItem& Item : Result.Items)
			{
				std::cerr << Item.Label << ',';
			}
			std::cerr << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Camel segment subsequence: SetValue → SetNextValue
		constexpr const char* CamelSource = R"(
class Box {
public:
	void SetNextValue(int v) {}
	void SetValue(int v) {}
}

void main() {
	Box b = Box();
	b.SetValue
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(CamelSource, "b.SetValue", Position), "locate SetValue prefix"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("b.SetValue").size());
		const FCompletionResult Result =
		    VeroScriptComplete::CompleteAtSource(CamelSource, Position, "<camel-set-value>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "SetValue") && HasLabel(Result.Items, "SetNextValue"),
		        "SetValue prefix should match SetValue and SetNextValue"))
		{
			std::cerr << "items=";
			for (const FCompletionItem& Item : Result.Items)
			{
				std::cerr << Item.Label << ',';
			}
			std::cerr << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Expression site: locals outrank functions / types for the same prefix.
		constexpr const char* RankSource = R"(
class Printer {}

void print_helper() {}

void main() {
	int print_count = 1;
	pri
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(RankSource, "\tpri", Position), "locate pri prefix"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\tpri").size());
		const FCompletionResult Result =
		    VeroScriptComplete::CompleteAtSource(RankSource, Position, "<rank-expr>");
		const int LocalIndex = LabelIndex(Result.Items, "print_count");
		const int FnIndex = LabelIndex(Result.Items, "print_helper");
		const int TypeIndex = LabelIndex(Result.Items, "Printer");
		if (!Expect(Result.bSuccess && LocalIndex >= 0, "print_count present"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        FnIndex < 0 || LocalIndex < FnIndex,
		        "local print_count should rank before print_helper"))
		{
			std::cerr << "local=" << LocalIndex << " fn=" << FnIndex << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
		        TypeIndex < 0 || LocalIndex < TypeIndex,
		        "local print_count should rank before type Printer"))
		{
			std::cerr << "local=" << LocalIndex << " type=" << TypeIndex << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
		        Result.Items[static_cast<std::size_t>(LocalIndex)].SortPriority < 40,
		        "local SortPriority should be in the expression-local band"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Member site: fields before methods when both match.
		constexpr const char* MemberRankSource = R"(
class Box {
public:
	int count;
	void clear() {}
	void countItems() {}
}

void main() {
	Box b = Box();
	b.cou
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(MemberRankSource, "b.cou", Position), "locate b.cou"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("b.cou").size());
		const FCompletionResult Result =
		    VeroScriptComplete::CompleteAtSource(MemberRankSource, Position, "<rank-member>");
		const int FieldIndex = LabelIndex(Result.Items, "count");
		const int MethodIndex = LabelIndex(Result.Items, "countItems");
		if (!Expect(
		        Result.bSuccess && FieldIndex >= 0 && MethodIndex >= 0 && FieldIndex < MethodIndex,
		        "member field count should rank before method countItems"))
		{
			std::cerr << "field=" << FieldIndex << " method=" << MethodIndex << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Equal-kind local candidates use lexical distance: the active function
		// block ranks before the callable parameter scope.
		constexpr const char* ScopeRankSource = R"(
void rank_scope(int valueParam) {
	int valueLocal = 1;
	int sink = valueLocal;
}
)";
		FTextPosition Position;
		constexpr std::string_view ScopeNeedle = "int sink = val";
		if (!Expect(FindPosition(ScopeRankSource, ScopeNeedle, Position), "locate scoped val prefix"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(ScopeNeedle.size());
		const FCompletionResult Result =
		    VeroScriptComplete::CompleteAtSource(ScopeRankSource, Position, "<rank-scope>");
		const int LocalIndex = LabelIndex(Result.Items, "valueLocal");
		const int ParamIndex = LabelIndex(Result.Items, "valueParam");
		if (!Expect(
		        Result.bSuccess && LocalIndex >= 0 && ParamIndex >= 0 && LocalIndex < ParamIndex,
		        "function-block locals should rank before parameters"))
		{
			std::cerr << "local=" << LocalIndex << " param=" << ParamIndex << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Macros (Constant + #define detail) rank below real locals in expressions.
		constexpr const char* MacroRankSource = R"(
#define print_macro 1

void main() {
	int print_local = 2;
	pri
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(MacroRankSource, "\tpri", Position), "locate macro-rank pri"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\tpri").size());
		const FCompletionResult Result =
		    VeroScriptComplete::CompleteAtSource(MacroRankSource, Position, "<rank-macro>");
		const int LocalIndex = LabelIndex(Result.Items, "print_local");
		const int MacroIndex = LabelIndex(Result.Items, "print_macro");
		if (!Expect(Result.bSuccess && LocalIndex >= 0 && MacroIndex >= 0, "local+macro present"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        LocalIndex < MacroIndex,
		        "local print_local should rank before macro print_macro"))
		{
			std::cerr << "local=" << LocalIndex << " macro=" << MacroIndex
			          << " localPri=" << Result.Items[static_cast<std::size_t>(LocalIndex)].SortPriority
			          << " macroPri=" << Result.Items[static_cast<std::size_t>(MacroIndex)].SortPriority
			          << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Comparison `>` is Expression — `true` must still complete (`x > t|`).
		constexpr const char* CmpSource = R"(
void main() {
	int x = 1;
	bool ok = x > t
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(CmpSource, "> t", Position), "locate comparison > t"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("> t").size());
		const FCompletionResult Result =
		    VeroScriptComplete::CompleteAtSource(CmpSource, Position, "<kw-cmp-gt>");
		if (!Expect(Result.bSuccess && HasLabel(Result.Items, "true"), "x > t| should offer true"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "import"), "x > t| should not flood import"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// for-init RHS is Expression — no statement-keyword flood after `=`.
		constexpr const char* ForInitSource = R"(
void main() {
	for (int i = t
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(ForInitSource, "= t", Position), "locate for-init = t"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("= t").size());
		const FCompletionResult Result =
		    VeroScriptComplete::CompleteAtSource(ForInitSource, Position, "<kw-for-init>");
		if (!Expect(Result.bSuccess && HasLabel(Result.Items, "true"), "for (int i = t| should offer true"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "import"), "for-init RHS should not flood import"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "while"), "for-init RHS should not flood while"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Type-argument site keeps type keywords (`array<i|`).
		constexpr const char* TypeArgSource = R"(
void main() {
	array<i
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(TypeArgSource, "array<i", Position), "locate array<i"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("array<i").size());
		const FCompletionResult Result =
		    VeroScriptComplete::CompleteAtSource(TypeArgSource, Position, "<kw-typearg>");
		if (!Expect(Result.bSuccess && HasLabel(Result.Items, "int"), "array<i| should offer int"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// After closed type-args, name site is DeclaratorName (`array<int> n|`).
		constexpr const char* ClosedTypeArgNameSource = R"(
void main() {
	array<int> n
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(ClosedTypeArgNameSource, "int> n", Position), "locate array<int> n"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("int> n").size());
		const FCompletionResult Result =
		    VeroScriptComplete::CompleteAtSource(ClosedTypeArgNameSource, Position, "<kw-closed-typearg>");
		if (!Expect(Result.bSuccess, "array<int> n| completion succeeds"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "import"), "array<int> n| must not offer import"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "if"), "array<int> n| must not offer if"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "int"), "array<int> n| must not offer int"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Nested `>>` also closes type-args (`array<array<int>> n|`).
		constexpr const char* NestedCloseSource = R"(
void main() {
	array<array<int>> n
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(NestedCloseSource, "int>> n", Position), "locate array<array<int>> n"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("int>> n").size());
		const FCompletionResult Result =
		    VeroScriptComplete::CompleteAtSource(NestedCloseSource, Position, "<kw-nested-gtgt>");
		if (!Expect(Result.bSuccess, "nested >> n| completion succeeds"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "import"), "nested >> n| must not offer import"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "while"), "nested >> n| must not offer while"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// for-init after closed generic is DeclaratorName (`for (array<int> i|`).
		constexpr const char* ForGenericNameSource = R"(
void main() {
	for (array<int> i
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(ForGenericNameSource, "int> i", Position), "locate for array<int> i"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("int> i").size());
		const FCompletionResult Result =
		    VeroScriptComplete::CompleteAtSource(ForGenericNameSource, Position, "<kw-for-generic-name>");
		if (!Expect(Result.bSuccess, "for (array<int> i| completion succeeds"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "import"), "for generic name must not offer import"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "int"), "for generic name must not offer int"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// `for (|` still needs type keywords (not call-paren Expression).
		constexpr const char* ForOpenParenSource = R"(
void main() {
	for (
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(ForOpenParenSource, "for (", Position), "locate for ("))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("for (").size());
		const FCompletionResult Result =
		    VeroScriptComplete::CompleteAtSource(ForOpenParenSource, Position, "<kw-for-open>");
		if (!Expect(Result.bSuccess && HasLabel(Result.Items, "int"), "for (| should offer int"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(HasLabel(Result.Items, "array"), "for (| should offer array"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Spaced comparison `x < t|` stays Expression (ident head not glued).
		constexpr const char* CmpLtSource = R"(
void main() {
	int x = 1;
	bool ok = x < t
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(CmpLtSource, "< t", Position), "locate comparison < t"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("< t").size());
		const FCompletionResult Result =
		    VeroScriptComplete::CompleteAtSource(CmpLtSource, Position, "<kw-cmp-lt>");
		if (!Expect(Result.bSuccess && HasLabel(Result.Items, "true"), "x < t| should offer true"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "import"), "x < t| should not flood import"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!HasLabel(Result.Items, "int"), "x < t| should not offer int as type"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Entry main() must appear in identifier completion.
		constexpr const char* MainCompleteSource = R"(
void helper() {}
void main() {
	mai
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(MainCompleteSource, "\tmai", Position), "locate mai in main"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\tmai").size());
		const FCompletionResult Result =
		    VeroScriptComplete::CompleteAtSource(MainCompleteSource, Position, "<complete-main>");
		if (!Expect(Result.bSuccess && HasLabel(Result.Items, "main"), "completion should include main"))
		{
			std::cerr << "prefix=" << Result.Prefix << " items=";
			for (const FCompletionItem& Item : Result.Items)
			{
				std::cerr << Item.Label << ',';
			}
			std::cerr << '\n';
			return EXIT_FAILURE;
		}
		const int MainIndex = LabelIndex(Result.Items, "main");
		if (!Expect(MainIndex >= 0 && MainIndex < 100, "main should rank within first 100 items"))
		{
			std::cerr << "mainIndex=" << MainIndex << " total=" << Result.Items.size() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Empty-prefix completion must still surface entry main (IDE Take(100) contract).
		constexpr const char* EmptyPrefixSource = R"(
void helper() {}
void main() {
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(EmptyPrefixSource, "void main()", Position), "locate main decl"))
		{
			return EXIT_FAILURE;
		}
		// Caret on the blank line inside main's body.
		Position.Line += 1;
		Position.Column = 1;
		const FCompletionResult Result =
		    VeroScriptComplete::CompleteAtSource(EmptyPrefixSource, Position, "<complete-main-empty>");
		const int MainIndex = LabelIndex(Result.Items, "main");
		if (!Expect(
		        Result.bSuccess && MainIndex >= 0 && MainIndex < 100,
		        "empty-prefix completion should include main near the top"))
		{
			std::cerr << "mainIndex=" << MainIndex << " total=" << Result.Items.size() << '\n';
			for (std::size_t Index = 0; Index < Result.Items.size() && Index < 30; ++Index)
			{
				std::cerr << "  [" << Index << "] " << Result.Items[Index].Label
				          << " pri=" << Result.Items[Index].SortPriority << '\n';
			}
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		if (!Expect(
		        Registry.RegisterNative(
		            "host_add",
		            VeroScript::ENativeType::Int,
		            {VeroScript::ENativeType::Int, VeroScript::ENativeType::Int},
		            [](const VeroScript::FNativeCallContext&,
		                VeroScript::FValue&,
		                std::string&)
		{
			return true;
		},
		            nullptr,
		            ErrorId),
		        "register host_add for complete"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::FHostTypeId TypeId;
		if (!Expect(
		        Registry.RegisterType("Actor", VeroScript::EHostTypeKind::Class, TypeId, ErrorId),
		        "register Actor for complete"))
		{
			return EXIT_FAILURE;
		}

		const char* Source = "void main() {\n\thost_\n}\n";
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "\thost_", Position), "locate host_ prefix"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\thost_").size());

		FCompletionOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;
		const FCompletionResult Result =
		    VeroScriptComplete::CompleteAtSource(Source, Position, "<complete-host>", Options);
		if (!Expect(Result.bSuccess, "host binding completion should succeed"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(HasLabel(Result.Items, "host_add"), "host_add should be suggested from Bindings"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		VeroScript::FHostTypeId ColorId;
		if (!Expect(
		        Registry.RegisterType("Color", VeroScript::EHostTypeKind::Enum, ColorId, ErrorId) && Registry.SetEnumScoped(ColorId, true, ErrorId),
		        "RegisterType Color should succeed"))
		{
			return EXIT_FAILURE;
		}
		auto AddEnum = [&](const char* Name, const std::int32_t Value) -> bool
		{
			VeroScript::FHostMemberDesc Member;
			Member.Kind = VeroScript::EHostMemberKind::EnumMember;
			Member.Name = Name;
			Member.EnumValue = Value;
			return Registry.RegisterMember(ColorId, Member, ErrorId);
		};
		if (!Expect(AddEnum("Red", 1) && AddEnum("Green", 2), "Color members should register"))
		{
			return EXIT_FAILURE;
		}

		VeroScript::FHostTypeId CounterId;
		if (!Expect(
		        Registry.RegisterType("Counter", VeroScript::EHostTypeKind::Class, CounterId, ErrorId),
		        "RegisterType Counter should succeed"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::FHostMemberDesc Getter;
		Getter.Kind = VeroScript::EHostMemberKind::PropertyGetter;
		Getter.Name = "value";
		Getter.ReturnType = VeroScript::ENativeType::Int;
		if (!Expect(Registry.RegisterMember(CounterId, Getter, ErrorId), "value getter should register"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::FHostMemberDesc Answer;
		Answer.Kind = VeroScript::EHostMemberKind::StaticConstant;
		Answer.Name = "Answer";
		Answer.ReturnType = VeroScript::ENativeType::Int;
		VeroScript::FHostMemberDesc Reset;
		Reset.Kind = VeroScript::EHostMemberKind::StaticMethod;
		Reset.Name = "reset";
		Reset.ReturnType = VeroScript::ENativeType::Void;
		VeroScript::FHostMemberDesc InstanceReset = Reset;
		InstanceReset.Kind = VeroScript::EHostMemberKind::Method;
		InstanceReset.ParamTypes = {VeroScript::ENativeType::Int};
		VeroScript::FHostMemberDesc Add;
		Add.Kind = VeroScript::EHostMemberKind::Method;
		Add.Name = "operator+";
		Add.bOperator = true;
		Add.ReturnType = VeroScript::ENativeType::HostObject;
		Add.ReturnHostTypeName = "Counter";
		Add.ParamTypes = {VeroScript::ENativeType::Int};
		if (!Expect(
		        Registry.RegisterMember(CounterId, Answer, ErrorId) && Registry.RegisterMember(CounterId, Reset, ErrorId) && Registry.RegisterMember(CounterId, InstanceReset, ErrorId) && Registry.RegisterMember(CounterId, Add, ErrorId),
		        "static constant/same-named static-instance methods/operator should register"))
		{
			std::cerr << ErrorId << '\n';
			return EXIT_FAILURE;
		}

		const std::string EnumSource = "void main() {\n\tColor.\n}\n";
		FTextPosition EnumPos{2, 1};
		EnumPos.Column += static_cast<std::uint32_t>(std::string_view("\tColor.").size());
		FCompletionOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;
		const FCompletionResult EnumResult = VeroScriptComplete::CompleteAtSource(
		    EnumSource, EnumPos, "<complete-host-enum>", Options);
		if (!Expect(EnumResult.bSuccess, "host enum member completion should succeed"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(HasLabel(EnumResult.Items, "Red"), "Color.Red should be suggested"))
		{
			return EXIT_FAILURE;
		}

		const std::string BareEnumSource = "void main() {\n\tRe\n}\n";
		FTextPosition BareEnumPos{2, 1};
		BareEnumPos.Column += static_cast<std::uint32_t>(std::string_view("\tRe").size());
		const FCompletionResult BareEnumResult = VeroScriptComplete::CompleteAtSource(
		    BareEnumSource, BareEnumPos, "<complete-scoped-host-enum>", Options);
		if (!Expect(
		        BareEnumResult.bSuccess && !HasLabel(BareEnumResult.Items, "Red"),
		        "scoped host enum members must require a qualified receiver"))
		{
			return EXIT_FAILURE;
		}

		const std::string PropSource = "void foo(Counter c) {\n\tc.\n}\n";
		FTextPosition PropPos{2, 1};
		PropPos.Column += static_cast<std::uint32_t>(std::string_view("\tc.").size());
		const FCompletionResult PropResult = VeroScriptComplete::CompleteAtSource(
		    PropSource, PropPos, "<complete-host-prop>", Options);
		if (!Expect(PropResult.bSuccess, "host property completion should succeed"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        HasLabel(PropResult.Items, "value") && HasLabel(PropResult.Items, "reset") && !HasLabel(PropResult.Items, "Answer") && !HasLabel(PropResult.Items, "operator+"),
		        "instance receiver should expose only named instance members and permitted statics"))
		{
			return EXIT_FAILURE;
		}

		const std::string StaticSource = "void main() {\n\tCounter.\n}\n";
		FTextPosition StaticPos{2, 1};
		StaticPos.Column += static_cast<std::uint32_t>(std::string_view("\tCounter.").size());
		const FCompletionResult StaticResult = VeroScriptComplete::CompleteAtSource(
		    StaticSource, StaticPos, "<complete-host-static>", Options);
		if (!Expect(
		        StaticResult.bSuccess && HasLabel(StaticResult.Items, "Answer") && HasLabel(StaticResult.Items, "reset") && !HasLabel(StaticResult.Items, "value") && !HasLabel(StaticResult.Items, "operator+"),
		        "type receiver should expose static constants/methods but not instance/operator members"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		VeroScript::FHostTypeId WidgetId;
		VeroScript::FHostTypeId ActorId;
		if (!Expect(
		        Registry.RegisterType("Widget", VeroScript::EHostTypeKind::Class, WidgetId, ErrorId) && Registry.RegisterType("Actor", VeroScript::EHostTypeKind::Class, ActorId, ErrorId),
		        "register Widget/Actor for chain complete"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::FHostMemberDesc Label;
		Label.Kind = VeroScript::EHostMemberKind::Method;
		Label.Name = "label";
		Label.ReturnType = VeroScript::ENativeType::String;
		VeroScript::FHostMemberDesc WidgetFn;
		WidgetFn.Kind = VeroScript::EHostMemberKind::Method;
		WidgetFn.Name = "widget";
		WidgetFn.ReturnType = VeroScript::ENativeType::HostObject;
		WidgetFn.ReturnHostTypeName = "Widget";
		if (!Expect(
		        Registry.RegisterMember(WidgetId, Label, ErrorId) && Registry.RegisterMember(ActorId, WidgetFn, ErrorId),
		        "register Actor.widget / Widget.label"))
		{
			return EXIT_FAILURE;
		}

		const std::string ChainSource = "void foo(Actor a) {\n\ta.widget().\n}\n";
		FTextPosition ChainPos{2, 1};
		ChainPos.Column += static_cast<std::uint32_t>(std::string_view("\ta.widget().").size());
		FCompletionOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;
		const FCompletionResult ChainResult = VeroScriptComplete::CompleteAtSource(
		    ChainSource, ChainPos, "<complete-host-chain>", Options);
		if (!Expect(ChainResult.bSuccess, "host method-return chain completion should succeed"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(HasLabel(ChainResult.Items, "label"), "a.widget().label should be suggested"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		VeroScript::FHostTypeId ActorId;
		if (!Expect(
		        Registry.RegisterType("AActor", VeroScript::EHostTypeKind::Class, ActorId, ErrorId) && Registry.SetTypeInheritable(ActorId, true, ErrorId),
		        "register inheritable AActor for complete"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::FHostMemberDesc GetValue;
		GetValue.Kind = VeroScript::EHostMemberKind::Method;
		GetValue.Name = "get_value";
		GetValue.ReturnType = VeroScript::ENativeType::Int;
		GetValue.Callback =
		    [](const VeroScript::FNativeCallContext&, VeroScript::FValue&, std::string&)
		{
			return true;
		};
		if (!Expect(Registry.RegisterMember(ActorId, GetValue, ErrorId), "AActor.get_value"))
		{
			return EXIT_FAILURE;
		}

		const std::string Source =
		    "class MyActor : AActor {\n"
		    "public:\n"
		    "\tMyActor() {}\n"
		    "}\n"
		    "void foo(MyActor a) {\n"
		    "\ta.\n"
		    "}\n";
		FTextPosition Position{6, 1};
		Position.Column += static_cast<std::uint32_t>(std::string_view("\ta.").size());
		FCompletionOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    Source, Position, "<complete-host-inherit>", Options);
		if (!Expect(Result.bSuccess, "host-inherit member completion should succeed"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(HasLabel(Result.Items, "get_value"), "MyActor should complete host get_value"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		VeroScript::FHostTypeId InventoryId;
		if (!Expect(
		        Registry.RegisterType(
		            "Inventory", VeroScript::EHostTypeKind::Class, InventoryId, ErrorId),
		        "register Inventory for array return complete"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::FHostMemberDesc Items;
		Items.Kind = VeroScript::EHostMemberKind::Method;
		Items.Name = "items";
		Items.ReturnType = VeroScript::ENativeType::Array;
		Items.ReturnElementTypeName = "int";
		Items.Callback =
		    [](const VeroScript::FNativeCallContext&, VeroScript::FValue&, std::string&)
		{
			return true;
		};
		if (!Expect(Registry.RegisterMember(InventoryId, Items, ErrorId), "Inventory.items"))
		{
			std::cerr << "error=" << ErrorId << '\n';
			return EXIT_FAILURE;
		}
		VeroScript::FHostMemberDesc Lookup;
		Lookup.Kind = VeroScript::EHostMemberKind::Method;
		Lookup.Name = "lookup";
		Lookup.ReturnType = VeroScript::ENativeType::Map;
		Lookup.ReturnKeyTypeName = "int";
		Lookup.ReturnValueTypeName = "string";
		Lookup.Callback = Items.Callback;
		if (!Expect(Registry.RegisterMember(InventoryId, Lookup, ErrorId), "Inventory.lookup"))
		{
			std::cerr << "error=" << ErrorId << '\n';
			return EXIT_FAILURE;
		}
		VeroScript::FHostMemberDesc Tags;
		Tags.Kind = VeroScript::EHostMemberKind::Method;
		Tags.Name = "tags";
		Tags.ReturnType = VeroScript::ENativeType::Set;
		Tags.ReturnElementTypeName = "string";
		Tags.Callback = Items.Callback;
		if (!Expect(Registry.RegisterMember(InventoryId, Tags, ErrorId), "Inventory.tags"))
		{
			std::cerr << "error=" << ErrorId << '\n';
			return EXIT_FAILURE;
		}
		constexpr const char* ArrayChainSource = R"(
void foo(Inventory inv) {
	inv.items().
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(ArrayChainSource, "inv.items().", Position),
		        "locate inv.items()."))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("inv.items().").size());
		FCompletionOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    ArrayChainSource, Position, "<complete-dynarray-chain>", Options);
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "push"),
		        "inv.items(). completes array<int> soft members"))
		{
			std::cerr << "items=" << Result.Items.size() << '\n';
			for (const auto& Item : Result.Items)
			{
				std::cerr << "  [" << Item.Label << "]\n";
			}
			return EXIT_FAILURE;
		}
		constexpr const char* MapChainSource = R"(
void foo(Inventory inv) {
	inv.lookup().
}
)";
		if (!Expect(
		        FindPosition(MapChainSource, "inv.lookup().", Position),
		        "locate inv.lookup()."))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(
		    std::string_view("inv.lookup().").size());
		const FCompletionResult MapResult = VeroScriptComplete::CompleteAtSource(
		    MapChainSource, Position, "<complete-map-chain>", Options);
		if (!Expect(
		        MapResult.bSuccess && HasLabel(MapResult.Items, "keys"),
		        "inv.lookup(). completes map<int,string> soft members"))
		{
			return EXIT_FAILURE;
		}
		constexpr const char* SetChainSource = R"(
void foo(Inventory inv) {
	inv.tags().
}
)";
		if (!Expect(
		        FindPosition(SetChainSource, "inv.tags().", Position),
		        "locate inv.tags()."))
		{
			return EXIT_FAILURE;
		}
		Position.Column +=
		    static_cast<std::uint32_t>(std::string_view("inv.tags().").size());
		const FCompletionResult SetResult = VeroScriptComplete::CompleteAtSource(
		    SetChainSource, Position, "<complete-set-chain>", Options);
		if (!Expect(
		        SetResult.bSuccess && HasLabel(SetResult.Items, "has"),
		        "inv.tags(). completes set<string> soft members"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const auto CompleteMember =
		    [](const std::string_view Source, const std::string_view Needle)
		{
			FTextPosition Position;
			if (!FindPosition(Source, Needle, Position))
			{
				return FCompletionResult{};
			}
			Position.Column += static_cast<std::uint32_t>(Needle.size());
			return VeroScriptComplete::CompleteAtSource(
			    Source, Position, "<complete-new-container>");
		};

		constexpr const char* ReadonlySource = R"(
void main() {
	readonly<int> value = 1;
	value.
}
)";
		const FCompletionResult Readonly =
		    CompleteMember(ReadonlySource, "value.");
		if (!Expect(
		        Readonly.bSuccess && HasLabel(Readonly.Items, "get") && !HasLabel(Readonly.Items, "value"),
		        "readonly completion should expose get but not its private storage"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* MultiMapSource = R"(
void main() {
	multimap<string, int> grouped();
	grouped.
}
)";
		const FCompletionResult MultiMap =
		    CompleteMember(MultiMapSource, "grouped.");
		const int AddIndex = LabelIndex(MultiMap.Items, "add");
		const int ValuesIndex = LabelIndex(MultiMap.Items, "values");
		const int EntriesIndex = LabelIndex(MultiMap.Items, "entries");
		const int IndexIndex = LabelIndex(MultiMap.Items, "operator[]");
		if (!Expect(
		        MultiMap.bSuccess && AddIndex >= 0 && ValuesIndex >= 0 && EntriesIndex >= 0 && IndexIndex >= 0 && MultiMap.Items[static_cast<std::size_t>(AddIndex)].Detail == "multimap<string,int> (string key, int value)" && MultiMap.Items[static_cast<std::size_t>(ValuesIndex)].Detail == "array<array<int>> ()" && MultiMap.Items[static_cast<std::size_t>(EntriesIndex)].Detail == "array<pair<string, array<int>>> ()" && MultiMap.Items[static_cast<std::size_t>(IndexIndex)].Detail == "array<int> (string key)",
		        "multimap completion should expose raw add value and grouped returns"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* MultiMapIndexSource = R"(
void main() {
	multimap<string, int> grouped();
	grouped["alpha"].
}
)";
		const FCompletionResult MultiMapIndex =
		    CompleteMember(MultiMapIndexSource, "grouped[\"alpha\"].");
		if (!Expect(
		        MultiMapIndex.bSuccess && HasLabel(MultiMapIndex.Items, "push"),
		        "multimap index should chain as array<V>"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* OrderedMultiMapSource = R"(
void main() {
	ordered_multimap<string, int> grouped();
	grouped.
}
)";
		const FCompletionResult OrderedMultiMap =
		    CompleteMember(OrderedMultiMapSource, "grouped.");
		const int OrderedAddIndex = LabelIndex(OrderedMultiMap.Items, "add");
		if (!Expect(
		        OrderedMultiMap.bSuccess && OrderedAddIndex >= 0 && OrderedMultiMap.Items[static_cast<std::size_t>(OrderedAddIndex)].Detail == "ordered_multimap<string,int> (string key, int value)",
		        "ordered_multimap completion should preserve raw V in add"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* MultiSetSource = R"(
void main() {
	multiset<string> counts();
	counts.
}
)";
		const FCompletionResult MultiSet =
		    CompleteMember(MultiSetSource, "counts.");
		const int CountIndex = LabelIndex(MultiSet.Items, "count");
		const int SetValuesIndex = LabelIndex(MultiSet.Items, "values");
		if (!Expect(
		        MultiSet.bSuccess && CountIndex >= 0 && SetValuesIndex >= 0 && MultiSet.Items[static_cast<std::size_t>(CountIndex)].Detail == "int64 (string value)" && MultiSet.Items[static_cast<std::size_t>(SetValuesIndex)].Detail == "array<string> ()",
		        "multiset completion should substitute its element type"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* PriorityQueueSource = R"(
void main() {
	priority_queue<int> work();
	work.
}
)";
		const FCompletionResult PriorityQueue =
		    CompleteMember(PriorityQueueSource, "work.");
		const int TopIndex = LabelIndex(PriorityQueue.Items, "top");
		if (!Expect(
		        PriorityQueue.bSuccess && TopIndex >= 0 && !HasLabel(PriorityQueue.Items, "front") && PriorityQueue.Items[static_cast<std::size_t>(TopIndex)].Detail == "int ()",
		        "priority_queue completion should offer typed top, not queue front"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* SuperSource = R"(
class Base {
public:
	int base_field;
	void base_method() {}
}
class Derived : Base {
public:
	void tick() {
		Super.
	}
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(SuperSource, "\t\tSuper.", Position), "locate Super."))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\t\tSuper.").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    SuperSource, Position, "<complete-super>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "base_field") && HasLabel(Result.Items, "base_method"),
		        "Super. offers base members"))
		{
			std::cerr << "items=" << Result.Items.size() << '\n';
			for (const auto& Item : Result.Items)
			{
				std::cerr << "  [" << Item.Label << "]\n";
			}
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* Source = R"(
UCLA
class CombatActor {}
void main() {}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "UCLA", Position), "locate UCLA prefix"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("UCLA").size());
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		if (!Expect(
		        Registry.RegisterAnnotation("UCLASS", "annotation ()", ErrorId),
		        "RegisterAnnotation UCLASS should succeed"))
		{
			return EXIT_FAILURE;
		}
		FCompletionOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;
		const FCompletionResult Result =
		    VeroScriptComplete::CompleteAtSource(Source, Position, "<complete-annot>", Options);
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "UCLASS"),
		        "host-registered UCLASS annotation"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* Source = R"(
enum class Color { Red, Green }
void main() {
	Color c = Color.Green;
	Color.
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "\tColor.", Position), "locate enum class member completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\tColor.").size());
		const FCompletionResult Result =
		    VeroScriptComplete::CompleteAtSource(Source, Position, "<complete-enum-class>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "Red") && HasLabel(Result.Items, "Green"),
		        "enum class qualified member completion"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* Source = R"(
enum class Color { Red, Green }
void main() {
	Color c = Color::Green;
	Color::
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "\tColor::", Position), "locate enum class :: completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\tColor::").size());
		const FCompletionResult Result =
		    VeroScriptComplete::CompleteAtSource(Source, Position, "<complete-enum-class-scope>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "Red") && HasLabel(Result.Items, "Green"),
		        "enum class :: qualified member completion"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* ScopedBare = R"(
enum class Color { Red, Green }
void main() {
	Gre
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(ScopedBare, "\tGre", Position), "locate Gre bare scoped"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\tGre").size());
		const FCompletionResult Result =
		    VeroScriptComplete::CompleteAtSource(ScopedBare, Position, "<complete-enum-class-bare>");
		if (!Expect(
		        Result.bSuccess && !HasLabel(Result.Items, "Green"),
		        "enum class members must not appear in bare completion"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* UnscopedBare = R"(
enum Color { Red, Green }
void main() {
	Gre
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(UnscopedBare, "\tGre", Position), "locate Gre bare unscoped"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\tGre").size());
		const FCompletionResult Result =
		    VeroScriptComplete::CompleteAtSource(UnscopedBare, Position, "<complete-enum-bare>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "Green"),
		        "unscoped enum members appear in bare completion"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* Source = R"(
enum class Color {
	UME
	Red
}
void main() {}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "\tUME", Position), "locate UMETA prefix"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\tUME").size());

		// Annotation names are host vocabulary: nothing is offered until registered.
		const FCompletionResult Unregistered =
		    VeroScriptComplete::CompleteAtSource(Source, Position, "<complete-umeta-none>");
		if (!Expect(
		        !HasLabel(Unregistered.Items, "UMETA"),
		        "unregistered annotation must not be suggested"))
		{
			return EXIT_FAILURE;
		}

		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		if (!Expect(
		        Registry.RegisterAnnotation("UMETA", "annotation (DisplayName=\"...\")", ErrorId),
		        "RegisterAnnotation UMETA should succeed"))
		{
			std::cerr << "error=[" << ErrorId << "]\n";
			return EXIT_FAILURE;
		}
		FCompletionOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    Source, Position, "<complete-umeta>", Options);
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "UMETA"),
		        "registered annotation is suggested"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Host-defined vocabulary is not UE-specific: any registered name works.
		constexpr const char* Source = R"(
VSTA
class Widget {}
void main() {}
)";
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		if (!Expect(
		        Registry.RegisterAnnotation("VSTAG", "annotation (Editor)", ErrorId) && Registry.RegisterAnnotation("@hook", "annotation ()", ErrorId),
		        "custom annotations should register"))
		{
			std::cerr << "error=[" << ErrorId << "]\n";
			return EXIT_FAILURE;
		}
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "VSTA", Position), "locate custom annotation prefix"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("VSTA").size());
		FCompletionOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    Source, Position, "<complete-custom-annotation>", Options);
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "VSTAG"),
		        "custom annotation is suggested"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        !HasLabel(Result.Items, "@hook"),
		        "sigil annotation must not be offered without the @"))
		{
			return EXIT_FAILURE;
		}

		// `@name` lexes as `@` + identifier; the sigil joins the replace range.
		constexpr const char* SigilSource = R"(
@ho
class Widget {}
void main() {}
)";
		FTextPosition SigilPos;
		if (!Expect(FindPosition(SigilSource, "@ho", SigilPos), "locate @ho prefix"))
		{
			return EXIT_FAILURE;
		}
		SigilPos.Column += static_cast<std::uint32_t>(std::string_view("@ho").size());
		const FCompletionResult SigilResult = VeroScriptComplete::CompleteAtSource(
		    SigilSource, SigilPos, "<complete-sigil-annotation>", Options);
		if (!Expect(
		        SigilResult.bSuccess && HasLabel(SigilResult.Items, "@hook") && !HasLabel(SigilResult.Items, "VSTAG"),
		        "@-prefixed annotation is suggested"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        SigilResult.bHasReplaceRange && SigilResult.ReplaceRange.StartColumn == SigilPos.Column - 3,
		        "@ annotation replace range covers the sigil"))
		{
			std::cerr << "replaceStart=" << SigilResult.ReplaceRange.StartColumn
			          << " expected=" << (SigilPos.Column - 3) << '\n';
			return EXIT_FAILURE;
		}

		// The LSP advertises `@` as a completion trigger, so the sigil by itself
		// must already return the matching host annotation vocabulary.
		constexpr const char* SigilOnlySource = R"(
@
class Widget {}
void main() {}
)";
		FTextPosition SigilOnlyPos;
		if (!Expect(FindPosition(SigilOnlySource, "@", SigilOnlyPos), "locate bare @ sigil"))
		{
			return EXIT_FAILURE;
		}
		SigilOnlyPos.Column += 1;
		const FCompletionResult SigilOnlyResult = VeroScriptComplete::CompleteAtSource(
		    SigilOnlySource, SigilOnlyPos, "<complete-sigil-only>", Options);
		if (!Expect(
		        SigilOnlyResult.bSuccess && HasLabel(SigilOnlyResult.Items, "@hook") && SigilOnlyResult.bHasReplaceRange && SigilOnlyResult.ReplaceRange.StartColumn == SigilOnlyPos.Column - 1,
		        "bare @ trigger offers sigil annotations and replaces the sigil"))
		{
			return EXIT_FAILURE;
		}

		// Same behaviour when the document parses (`@ho()` is a well-formed annotation),
		// so accepting an item can never leave a stray `@`.
		constexpr const char* LoadedSource = R"(
@ho()
class Widget {}
void main() {}
)";
		FTextPosition LoadedPos;
		if (!Expect(FindPosition(LoadedSource, "@ho()", LoadedPos), "locate parsed @ho prefix"))
		{
			return EXIT_FAILURE;
		}
		LoadedPos.Column += static_cast<std::uint32_t>(std::string_view("@ho").size());
		const FCompletionResult LoadedResult = VeroScriptComplete::CompleteAtSource(
		    LoadedSource, LoadedPos, "<complete-sigil-loaded>", Options);
		if (!Expect(
		        LoadedResult.bSuccess && HasLabel(LoadedResult.Items, "@hook") && LoadedResult.bHasReplaceRange && LoadedResult.ReplaceRange.StartColumn == LoadedPos.Column - 3,
		        "@ annotation completes on the parsed path with the sigil replaced"))
		{
			std::cerr << "replaceStart=" << LoadedResult.ReplaceRange.StartColumn
			          << " expected=" << (LoadedPos.Column - 3) << '\n';
			return EXIT_FAILURE;
		}

		// A bare identifier prefix must never surface the `@` spelling.
		constexpr const char* BareSource = R"(
class Widget {
public:
	void run() { int hooked = 1; ho }
}
void main() {}
)";
		FTextPosition BarePos;
		if (!Expect(FindPosition(BareSource, "1; ho", BarePos), "locate bare ho prefix"))
		{
			return EXIT_FAILURE;
		}
		BarePos.Column += static_cast<std::uint32_t>(std::string_view("1; ho").size());
		const FCompletionResult BareResult = VeroScriptComplete::CompleteAtSource(
		    BareSource, BarePos, "<complete-bare-no-sigil>", Options);
		if (!Expect(
		        !HasLabel(BareResult.Items, "@hook"),
		        "bare prefix must not offer the @ spelling"))
		{
			return EXIT_FAILURE;
		}

		// Host annotations are declaration metadata, not expression identifiers.
		constexpr const char* ExpressionSource = R"(
void main() {
	int value = VSTA
}
)";
		FTextPosition ExpressionPos;
		if (!Expect(FindPosition(ExpressionSource, "VSTA", ExpressionPos), "locate expression VSTA"))
		{
			return EXIT_FAILURE;
		}
		ExpressionPos.Column += 4;
		const FCompletionResult ExpressionResult = VeroScriptComplete::CompleteAtSource(
		    ExpressionSource, ExpressionPos, "<complete-annotation-expression>", Options);
		if (!Expect(
		        !HasLabel(ExpressionResult.Items, "VSTAG"),
		        "annotation must not be suggested inside an expression"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* HostMacroSource =
		    "void main() {\n"
		    "    HOST_\n"
		    "}\n";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(HostMacroSource, "HOST_", Position),
		        "locate predefined macro completion prefix"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += 5;
		FCompletionOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.PredefinedMacros = {
		    {"HOST_LOG", {"format"}, true, "print(format, __VA_ARGS__)", "ide-host"}};
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    HostMacroSource,
		    Position,
		    "<predefined-macro-complete>",
		    Options);
		const int Index = LabelIndex(Result.Items, "HOST_LOG");
		if (!Expect(
		        Result.bSuccess && Index >= 0 && Result.Items[static_cast<std::size_t>(Index)].InsertText == "HOST_LOG(" && Result.Items[static_cast<std::size_t>(Index)].Detail.find("format, ...") != std::string::npos,
		        "IDE completion lists structured predefined function macros"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Bare calls inside an instance method use the same implicit-this lookup as
		// `this.Target()`, so completion must expose inherited methods as identifiers.
		constexpr const char* Source = R"(
class Base {
public:
	void Target() {}
}
class Derived : Base {
public:
	void Run() { Target(); }
}
void main() {}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(Source, "Target();", Position),
		        "locate implicit-this completion prefix"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += 3;
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    Source,
		    Position,
		    "<implicit-this-complete>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "Target"),
		        "identifier completion lists inherited implicit-this methods"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Completion legality must be decided by syntax context before fuzzy ranking.
		// A strong host-function match is still invalid at file/type declaration scope,
		// but remains available at statement/expression sites.
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		if (!Expect(
		        Registry.RegisterNative(
		            "ClassIsChildOf",
		            VeroScript::ENativeType::Bool,
		            {},
		            [](const VeroScript::FNativeCallContext&,
		                VeroScript::FValue& Result,
		                std::string&)
		{
			Result = true;
			return true;
		},
		            nullptr,
		            ErrorId),
		        "register context-filter host function"))
		{
			return EXIT_FAILURE;
		}

		FCompletionOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;
		Options.PredefinedMacros = {
		    {"UCLASS", {}, false, "", "context-test"},
		    {"UPROPERTY", {}, false, "", "context-test"},
		    {"UE_CLOG", {"condition", "format"}, true, "", "context-test"},
		};

		constexpr const char* TopPrefixSource = R"(
cl
void main() {}
)";
		FTextPosition TopPrefixPosition;
		if (!Expect(
		        FindPosition(TopPrefixSource, "cl", TopPrefixPosition),
		        "locate top-level cl"))
		{
			return EXIT_FAILURE;
		}
		TopPrefixPosition.Column += 2;
		const FCompletionResult TopPrefix = VeroScriptComplete::CompleteAtSource(
		    TopPrefixSource,
		    TopPrefixPosition,
		    "<complete-top-context>",
		    Options);
		if (!Expect(
		        TopPrefix.bSuccess && HasLabel(TopPrefix.Items, "class") && !HasLabel(TopPrefix.Items, "ClassIsChildOf"),
		        "top-level completion offers declarations and filters host functions"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* TopBlankSource = "void main() {}\n";
		const FCompletionResult TopBlank = VeroScriptComplete::CompleteAtSource(
		    TopBlankSource,
		    FTextPosition{2, 1},
		    "<complete-top-blank>",
		    Options);
		if (!Expect(
		        TopBlank.bSuccess && HasLabel(TopBlank.Items, "class") && HasLabel(TopBlank.Items, "UCLASS") && !HasLabel(TopBlank.Items, "main") && !HasLabel(TopBlank.Items, "ClassIsChildOf"),
		        "blank top-level completion keeps declaration macros and removes callables"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* TypeBodySource =
		    "class Widget {\n"
		    "public:\n"
		    "\t\n"
		    "}\n"
		    "void Helper() {}\n";
		FTextPosition TypeBodyPosition;
		if (!Expect(
		        FindPosition(TypeBodySource, "\t\n}", TypeBodyPosition),
		        "locate blank type body"))
		{
			return EXIT_FAILURE;
		}
		++TypeBodyPosition.Column;
		const FCompletionResult TypeBody = VeroScriptComplete::CompleteAtSource(
		    TypeBodySource,
		    TypeBodyPosition,
		    "<complete-type-body-context>",
		    Options);
		if (!Expect(
		        TypeBody.bSuccess && HasLabel(TypeBody.Items, "private") && HasLabel(TypeBody.Items, "default") && HasLabel(TypeBody.Items, "explicit") && HasLabel(TypeBody.Items, "mutable") && HasLabel(TypeBody.Items, "UPROPERTY") && !HasLabel(TypeBody.Items, "Helper") && !HasLabel(TypeBody.Items, "ClassIsChildOf"),
		        "type-body completion keeps member declarations and filters callables"))
		{
			for (const FCompletionItem& Item : TypeBody.Items)
			{
				std::cerr << "type-body item=" << Item.Label << '\n';
			}
			return EXIT_FAILURE;
		}

		constexpr const char* StatementSource = R"(
void main() {
	cl
}
)";
		FTextPosition StatementPosition;
		if (!Expect(
		        FindPosition(StatementSource, "\tcl", StatementPosition),
		        "locate statement cl"))
		{
			return EXIT_FAILURE;
		}
		StatementPosition.Column += 3;
		const FCompletionResult Statement = VeroScriptComplete::CompleteAtSource(
		    StatementSource,
		    StatementPosition,
		    "<complete-statement-context>",
		    Options);
		if (!Expect(
		        Statement.bSuccess && HasLabel(Statement.Items, "ClassIsChildOf") && !HasLabel(Statement.Items, "class"),
		        "statement completion retains callable expressions and filters declarations"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* TypeArgumentSource = R"(
class ClassValue {}
void main() {
	array<Cl
}
)";
		FTextPosition TypeArgumentPosition;
		if (!Expect(
		        FindPosition(TypeArgumentSource, "array<Cl", TypeArgumentPosition),
		        "locate type-argument Cl"))
		{
			return EXIT_FAILURE;
		}
		TypeArgumentPosition.Column +=
		    static_cast<std::uint32_t>(std::string_view("array<Cl").size());
		const FCompletionResult TypeArgument = VeroScriptComplete::CompleteAtSource(
		    TypeArgumentSource,
		    TypeArgumentPosition,
		    "<complete-type-name-context>",
		    Options);
		if (!Expect(
		        TypeArgument.bSuccess && HasLabel(TypeArgument.Items, "ClassValue") && !HasLabel(TypeArgument.Items, "ClassIsChildOf"),
		        "type-name completion offers types and filters functions"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* DeclaratorSource = R"(
class SomeType {}
void main() {
	SomeType Cl
}
)";
		FTextPosition DeclaratorPosition;
		if (!Expect(
		        FindPosition(DeclaratorSource, "SomeType Cl", DeclaratorPosition),
		        "locate declarator name Cl"))
		{
			return EXIT_FAILURE;
		}
		DeclaratorPosition.Column +=
		    static_cast<std::uint32_t>(std::string_view("SomeType Cl").size());
		const FCompletionResult Declarator = VeroScriptComplete::CompleteAtSource(
		    DeclaratorSource,
		    DeclaratorPosition,
		    "<complete-declarator-context>",
		    Options);
		if (!Expect(
		        Declarator.bSuccess && !HasLabel(Declarator.Items, "ClassIsChildOf") && !HasLabel(Declarator.Items, "class"),
		        "declarator-name completion does not suggest unrelated existing symbols"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* Source = R"(
namespace Library {
	struct Point {}
	int produce() { return 1; }
	int payload = 2;
}
using namespace Library;
void main() {
	Po
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "\tPo", Position), "locate using namespace prefix"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += 3;
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    Source, Position, "<complete-using-namespace>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "Point"),
		        "using namespace exposes direct namespace members to completion"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* ScopedSource = R"(
namespace Outer {
	namespace Shared { struct Value {} }
	namespace Client {
		using namespace Shared;
		void run() { Va }
	}
}
)";
		if (!Expect(FindPosition(ScopedSource, "Va }", Position),
		        "locate namespace-scoped using prefix"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += 2;
		const FCompletionResult ScopedResult = VeroScriptComplete::CompleteAtSource(
		    ScopedSource, Position, "<complete-scoped-using-namespace>");
		if (!Expect(
		        ScopedResult.bSuccess && HasLabel(ScopedResult.Items, "Value"),
		        "namespace-scoped using participates in completion"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* BeforeDirectiveSource = R"(
namespace Later { struct FutureType {} }
void before() { Fu }
using namespace Later;
)";
		if (!Expect(FindPosition(BeforeDirectiveSource, "Fu }", Position),
		        "locate prefix before using directive"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += 2;
		const FCompletionResult BeforeDirective = VeroScriptComplete::CompleteAtSource(
		    BeforeDirectiveSource, Position, "<complete-before-using-namespace>");
		if (!Expect(
		        BeforeDirective.bSuccess && !HasLabel(BeforeDirective.Items, "FutureType"),
		        "using namespace completion starts at its declaration point"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* OutsideScopedSource = R"(
namespace Outer {
	namespace Shared { struct ScopedValue {} }
	namespace Client { using namespace Shared; }
}
void outside() { Sc }
)";
		if (!Expect(FindPosition(OutsideScopedSource, "Sc }", Position),
		        "locate prefix outside scoped using directive"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += 2;
		const FCompletionResult OutsideScoped = VeroScriptComplete::CompleteAtSource(
		    OutsideScopedSource, Position, "<complete-outside-scoped-using>");
		if (!Expect(
		        OutsideScoped.bSuccess && !HasLabel(OutsideScoped.Items, "ScopedValue"),
		        "namespace-scoped using completion does not leak outside its scope"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* TargetSource = R"(
namespace Library {}
int unrelated() { return 0; }
using namespace Lib
)";
		if (!Expect(FindPosition(TargetSource, "Lib\n", Position),
		        "locate unfinished using namespace target"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += 3;
		const FCompletionResult TargetResult = VeroScriptComplete::CompleteAtSource(
		    TargetSource, Position, "<complete-using-namespace-target>");
		if (!Expect(
		        TargetResult.bSuccess && HasLabel(TargetResult.Items, "Library") && !HasLabel(TargetResult.Items, "unrelated") && !HasLabel(TargetResult.Items, "class"),
		        "using namespace target completion is namespace-only during editing"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* EmptyTargetSource = R"(
namespace Library {}
using namespace )";
		Position = FTextPosition{3, 17};
		const FCompletionResult EmptyTarget = VeroScriptComplete::CompleteAtSource(
		    EmptyTargetSource, Position, "<complete-empty-using-target>");
		if (!Expect(
		        EmptyTarget.bSuccess && HasLabel(EmptyTarget.Items, "Library"),
		        "empty using namespace target offers namespaces at end of file"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* StandardTargetSource = "using namespace st";
		Position = FTextPosition{1, 19};
		const FCompletionResult StandardTarget = VeroScriptComplete::CompleteAtSource(
		    StandardTargetSource, Position, "<complete-standard-using-target>");
		if (!Expect(
		        StandardTarget.bSuccess && HasLabel(StandardTarget.Items, "std") && !HasLabel(StandardTarget.Items, "chrono") && !HasLabel(StandardTarget.Items, "std::chrono"),
		        "using namespace completes only the standard root segment"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* QualifiedTargetSource = R"(
namespace Outer { namespace Shared {} }
using namespace Outer::Sh
)";
		if (!Expect(FindPosition(QualifiedTargetSource, "Sh\n", Position),
		        "locate qualified using namespace target"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += 2;
		const FCompletionResult QualifiedTarget = VeroScriptComplete::CompleteAtSource(
		    QualifiedTargetSource, Position, "<complete-qualified-using-target>");
		if (!Expect(
		        QualifiedTarget.bSuccess && HasLabel(QualifiedTarget.Items, "Shared"),
		        "qualified using namespace target offers the next namespace segment"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FHostBindingRegistry Registry;
		VeroScript::FGenericHostTypeTemplateId TemplateId;
		std::string ErrorId;
		if (!Expect(
		        Registry.RegisterGenericHostTypeTemplateMetadata(
		            "HostPair", 2, TemplateId, ErrorId, "A host-provided generic pair."),
		        "register generic host template metadata"))
		{
			std::cerr << ErrorId << '\n';
			return EXIT_FAILURE;
		}
		constexpr const char* Source = "void main() {\n\tHostP\n}\n";
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "HostP", Position), "locate generic host prefix"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("HostP").size());
		FCompletionOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    Source, Position, "<complete-generic-host>", Options);
		const int Index = LabelIndex(Result.Items, "HostPair");
		if (!Expect(
		        Result.bSuccess && Index >= 0 && Result.Items[static_cast<std::size_t>(Index)].InsertText == "HostPair<${1}, ${2}>" && Result.Items[static_cast<std::size_t>(Index)].bInsertTextSnippet && Result.Items[static_cast<std::size_t>(Index)].Detail.find("T1, T2") != std::string::npos,
		        "generic host template completion should insert a generic snippet"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* ExistingAngleSource = "void main() {\n\tHostP<\n}\n";
		FTextPosition ExistingAnglePosition;
		if (!Expect(
		        FindPosition(ExistingAngleSource, "HostP", ExistingAnglePosition),
		        "locate generic host prefix before existing angle"))
		{
			return EXIT_FAILURE;
		}
		ExistingAnglePosition.Column += static_cast<std::uint32_t>(
		    std::string_view("HostP").size());
		const FCompletionResult ExistingAngle = VeroScriptComplete::CompleteAtSource(
		    ExistingAngleSource,
		    ExistingAnglePosition,
		    "<complete-generic-host-existing-angle>",
		    Options);
		const int ExistingAngleIndex = LabelIndex(ExistingAngle.Items, "HostPair");
		if (!Expect(
		        ExistingAngle.bSuccess && ExistingAngleIndex >= 0 && ExistingAngle.Items[static_cast<std::size_t>(ExistingAngleIndex)].InsertText == "HostPair" && !ExistingAngle.Items[static_cast<std::size_t>(ExistingAngleIndex)].bInsertTextSnippet,
		        "generic completion never duplicates an already typed '<'"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Built-in containers are known-instantiable generic types, unlike ordinary
		// keywords.  They receive a type-argument snippet only at a type/declaration
		// site, and never duplicate an already typed opening angle.
		constexpr const char* GenericContainerSource = "void main() {\n\tar\n}\n";
		FTextPosition Position;
		if (!Expect(FindPosition(GenericContainerSource, "ar\n", Position),
		        "locate container generic prefix"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += 2;
		const FCompletionResult ContainerResult = VeroScriptComplete::CompleteAtSource(
		    GenericContainerSource, Position, "<complete-container-generic-snippet>");
		const int ArrayIndex = LabelIndex(ContainerResult.Items, "array");
		if (!Expect(
		        ContainerResult.bSuccess && ArrayIndex >= 0 && ContainerResult.Items[static_cast<std::size_t>(ArrayIndex)].InsertText == "array<${1}>" && ContainerResult.Items[static_cast<std::size_t>(ArrayIndex)].bInsertTextSnippet,
		        "instantiable container completion inserts a generic snippet"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* ExistingAngleSource = "void main() {\n\tar<\n}\n";
		if (!Expect(FindPosition(ExistingAngleSource, "ar<", Position),
		        "locate container generic prefix before existing angle"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += 2;
		const FCompletionResult ExistingAngle = VeroScriptComplete::CompleteAtSource(
		    ExistingAngleSource,
		    Position,
		    "<complete-container-generic-existing-angle>");
		const int ExistingArrayIndex = LabelIndex(ExistingAngle.Items, "array");
		if (!Expect(
		        ExistingAngle.bSuccess && ExistingArrayIndex >= 0 && ExistingAngle.Items[static_cast<std::size_t>(ExistingArrayIndex)].InsertText == "array" && !ExistingAngle.Items[static_cast<std::size_t>(ExistingArrayIndex)].bInsertTextSnippet,
		        "container completion never duplicates an already typed '<'"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Call sites get parameter placeholders, while a declaration initializer
		// keeps the function name as a value/reference.  This protects callback
		// and function-pointer completion from accidentally inserting `()`.
		constexpr const char* CallSource = R"(
int Helper(int count, string name) { return count; }
void main() {
	Hel;
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(CallSource, "Hel;", Position), "locate callable completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += 3;
		const FCompletionResult CallResult = VeroScriptComplete::CompleteAtSource(
		    CallSource, Position, "<complete-callable-snippet>");
		const int CallIndex = LabelIndex(CallResult.Items, "Helper");
		if (!Expect(
		        CallResult.bSuccess && CallIndex >= 0 && CallResult.Items[static_cast<std::size_t>(CallIndex)].InsertText == "Helper(${1:count}, ${2:name})" && CallResult.Items[static_cast<std::size_t>(CallIndex)].bInsertTextSnippet,
		        "callable completion inserts parameter placeholders only at call sites"))
		{
			return EXIT_FAILURE;
		}

		struct FPrintCompletionCase
		{
			const char* Source;
			const char* ExpectedText;
			bool bSnippet;
		};
		for (const FPrintCompletionCase& Case : {
		         FPrintCompletionCase{"void main() { pri; }", "print(${0})", true},
		         FPrintCompletionCase{"void main() { auto callback = pri; }", "print", false},
		         FPrintCompletionCase{"void main() { pri(); }", "print", false},
		         FPrintCompletionCase{"void main() { print(); }", "print", false},
		         FPrintCompletionCase{"void main() { pri /* note */ (); }", "print", false},
		     })
		{
			if (!Expect(FindPosition(Case.Source, "pri", Position),
			        "locate print completion"))
			{
				return EXIT_FAILURE;
			}
			Position.Column += 3;
			const FCompletionResult PrintResult = VeroScriptComplete::CompleteAtSource(
			    Case.Source, Position, "<complete-print-snippet>");
			const int PrintIndex = LabelIndex(PrintResult.Items, "print");
			const int PrintfIndex = LabelIndex(PrintResult.Items, "printf");
			if (!Expect(PrintResult.bSuccess && PrintIndex >= 0 && PrintfIndex >= 0 && PrintResult.Items[static_cast<std::size_t>(PrintIndex)].InsertText == Case.ExpectedText && PrintResult.Items[static_cast<std::size_t>(PrintIndex)].bInsertTextSnippet == Case.bSnippet && PrintResult.Items[static_cast<std::size_t>(PrintfIndex)].InsertText == (Case.bSnippet ? "printf(${0})" : "printf") && PrintResult.Items[static_cast<std::size_t>(PrintfIndex)].bInsertTextSnippet == Case.bSnippet,
			        "print completion uses empty call parentheses only when needed"))
			{
				return EXIT_FAILURE;
			}
		}

		constexpr const char* ReferenceSource = R"(
int Helper(int count, string name) { return count; }
void main() {
	int value = Hel;
}
)";
		if (!Expect(
		        FindPosition(ReferenceSource, "Hel;", Position),
		        "locate function reference completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += 3;
		const FCompletionResult ReferenceResult = VeroScriptComplete::CompleteAtSource(
		    ReferenceSource, Position, "<complete-function-reference>");
		const int ReferenceIndex = LabelIndex(ReferenceResult.Items, "Helper");
		if (!Expect(
		        ReferenceResult.bSuccess && ReferenceIndex >= 0 && ReferenceResult.Items[static_cast<std::size_t>(ReferenceIndex)].InsertText == "Helper" && !ReferenceResult.Items[static_cast<std::size_t>(ReferenceIndex)].bInsertTextSnippet,
		        "function-reference completion does not insert call parentheses"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* AssignmentCallSource = R"(
int Helper(int count, string name) { return count; }
void main() {
	int value = 0;
	value = Hel;
}
)";
		if (!Expect(
		        FindPosition(AssignmentCallSource, "Hel;", Position),
		        "locate assignment call completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += 3;
		const FCompletionResult AssignmentCallResult = VeroScriptComplete::CompleteAtSource(
		    AssignmentCallSource, Position, "<complete-assignment-call>");
		const int AssignmentCallIndex = LabelIndex(AssignmentCallResult.Items, "Helper");
		if (!Expect(
		        AssignmentCallResult.bSuccess && AssignmentCallIndex >= 0 && AssignmentCallResult.Items[static_cast<std::size_t>(AssignmentCallIndex)].InsertText == "Helper(${1:count}, ${2:name})" && AssignmentCallResult.Items[static_cast<std::size_t>(AssignmentCallIndex)].bInsertTextSnippet,
		        "ordinary assignment expressions retain callable snippets"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* MemberCallSource = R"(
struct Worker {
	void Work(int amount, string note) {}
}
void main() {
	Worker worker = Worker();
	worker.Wor;
}
)";
		if (!Expect(
		        FindPosition(MemberCallSource, "Wor;", Position),
		        "locate member callable completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += 3;
		const FCompletionResult MemberCallResult = VeroScriptComplete::CompleteAtSource(
		    MemberCallSource, Position, "<complete-member-callable-snippet>");
		const int MemberCallIndex = LabelIndex(MemberCallResult.Items, "Work");
		if (!Expect(
		        MemberCallResult.bSuccess && MemberCallIndex >= 0 && MemberCallResult.Items[static_cast<std::size_t>(MemberCallIndex)].InsertText == "Work(${1:amount}, ${2:note})" && MemberCallResult.Items[static_cast<std::size_t>(MemberCallIndex)].bInsertTextSnippet,
		        "member-call completion inserts parameter placeholders"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* OverloadSource = R"(
int Select(int value) { return value; }
int Select(string value) { return 0; }
void main() {
	Sel;
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(OverloadSource, "Sel;", Position), "locate overload completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += 3;
		const FCompletionResult Overloads = VeroScriptComplete::CompleteAtSource(
		    OverloadSource, Position, "<complete-overloads>");
		if (!Expect(
		        Overloads.bSuccess && CountLabel(Overloads.Items, "Select") == 1 && HasDetail(Overloads.Items, "Select", "int Select(int value);\nint Select(string value);") && Overloads.Items[static_cast<std::size_t>(LabelIndex(Overloads.Items, "Select"))].InsertText == "Select(${0})",
		        "completion groups overload signatures in one candidate"))
		{
			return EXIT_FAILURE;
		}
		std::string ExistingCall = OverloadSource;
		ExistingCall.replace(ExistingCall.find("Sel;"), 4, "Sel();");
		FTextPosition CallPosition;
		if (!Expect(FindPosition(ExistingCall, "Sel();", CallPosition),
		        "locate overloaded call before existing parenthesis"))
		{
			return EXIT_FAILURE;
		}
		CallPosition.Column += 3;
		const FCompletionResult Call = VeroScriptComplete::CompleteAtSource(
		    ExistingCall, CallPosition, "<complete-overloads-existing-parenthesis>");
		const int SelectIndex = LabelIndex(Call.Items, "Select");
		if (!Expect(Call.bSuccess && SelectIndex >= 0 && Call.Items[static_cast<std::size_t>(SelectIndex)].InsertText == "Select" && !Call.Items[static_cast<std::size_t>(SelectIndex)].bInsertTextSnippet,
		        "overloaded call does not duplicate an existing parenthesis"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::string Source =
		    "class Reader { public: int fetch(int value) { return value; } "
		    "int fetch(string value) { return 0; } }\n"
		    "void main() { Reader reader = Reader(); reader.fet; }\n";
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "reader.fet", Position),
		        "locate member overload completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(
		    std::string_view("reader.fet").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    Source, Position, "<complete-member-overloads>");
		if (!Expect(Result.bSuccess && CountLabel(Result.Items, "fetch") == 1 && HasDetail(Result.Items, "fetch", "int fetch(int value);\nint fetch(string value);") && Result.Items[static_cast<std::size_t>(LabelIndex(Result.Items, "fetch"))].InsertText == "fetch(${0})",
		        "member overload completion groups source signatures in one candidate"))
		{
			std::cerr << "success=" << Result.bSuccess << " items=" << Result.Items.size()
			          << " prefix=" << Result.Prefix << '\n';
			for (const FCompletionItem& Item : Result.Items)
			{
				std::cerr << Item.Label << " | " << Item.Detail << " | " << Item.InsertText << '\n';
			}
			for (const auto& Diagnostic : Result.Diagnostics)
			{
				std::cerr << Diagnostic.Message << '\n';
			}
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* DeletedSource = "struct Token { Token() = de }\n";
		FTextPosition Position;
		if (!Expect(FindPosition(DeletedSource, "= de", Position), "locate delete suffix"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("= de").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    DeletedSource, Position, "<complete-delete-suffix>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "delete") && !HasLabel(Result.Items, "class") && !HasLabel(Result.Items, "true"),
		        "declaration assignment should offer only default/delete keywords"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* FriendSource = R"(
class Secret {
	friend void Inspect(Secret value);
private:
	int Code = 7;
}
void Inspect(Secret value) { value. }
)";
		FTextPosition Position;
		if (!Expect(FindPosition(FriendSource, "value.", Position), "locate friend completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("value.").size());
		const FCompletionResult FriendResult = VeroScriptComplete::CompleteAtSource(
		    FriendSource, Position, "<complete-friend-function>");
		if (!FriendResult.bSuccess || !HasLabel(FriendResult.Items, "Code"))
		{
			std::cerr << "friend completion success=" << FriendResult.bSuccess << " labels:";
			for (const auto& Item : FriendResult.Items)
			{
				std::cerr << ' ' << Item.Label;
			}
			std::cerr << '\n';
		}
		if (!Expect(
		        FriendResult.bSuccess && HasLabel(FriendResult.Items, "Code"),
		        "exact friend function completion exposes private members"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* NonFriendSource = R"(
class Secret {
	friend void Inspect(Secret value);
private:
	int Code = 7;
}
void Leak(Secret value) { value. }
)";
		if (!Expect(FindPosition(NonFriendSource, "value.", Position), "locate non-friend completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("value.").size());
		const FCompletionResult NonFriendResult = VeroScriptComplete::CompleteAtSource(
		    NonFriendSource, Position, "<complete-non-friend-function>");
		if (!Expect(
		        NonFriendResult.bSuccess && !HasLabel(NonFriendResult.Items, "Code"),
		        "non-friend completion keeps private members hidden"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* AliasFriendSource = R"(
class Reader {
	void Read(Secret value) { value. }
}
using ReaderAlias = Reader;
class Secret {
	friend void ReaderAlias::Read(Secret value);
private:
	int Code = 8;
}
)";
		if (!Expect(
		        FindPosition(AliasFriendSource, "value.", Position),
		        "locate aliased friend completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("value.").size());
		const FCompletionResult AliasFriendResult = VeroScriptComplete::CompleteAtSource(
		    AliasFriendSource, Position, "<complete-aliased-friend>");
		if (!Expect(
		        AliasFriendResult.bSuccess && HasLabel(AliasFriendResult.Items, "Code"),
		        "friend member owner aliases are honored by completion"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* DerivedFriendSource = R"(
class Base {
protected:
	int Code = 9;
}
class Derived : public Base {
	friend void Inspect(Derived value);
}
void Inspect(Derived value) { value. }
)";
		if (!Expect(
		        FindPosition(DerivedFriendSource, "value.", Position),
		        "locate derived friend completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("value.").size());
		const FCompletionResult DerivedFriendResult = VeroScriptComplete::CompleteAtSource(
		    DerivedFriendSource, Position, "<complete-derived-friend>");
		if (!Expect(
		        DerivedFriendResult.bSuccess && HasLabel(DerivedFriendResult.Items, "Code"),
		        "derived friend completion exposes inherited protected members"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* GenericDefaults = R"(
template<T, U = int>
class PairBox { public: T first; U second; }
void main() {
	PairBox<string> value = PairBox<string>();
	value.
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(GenericDefaults, "\tvalue.", Position),
		        "locate member completion on a defaulted generic type"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\tvalue.").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    GenericDefaults, Position, "<complete-generic-defaults>");
		bool bFirstIsString = false;
		bool bSecondIsInt = false;
		for (const FCompletionItem& Item : Result.Items)
		{
			bFirstIsString = bFirstIsString || (Item.Label == "first" && Item.Detail.find("string") != std::string::npos);
			bSecondIsInt = bSecondIsInt || (Item.Label == "second" && Item.Detail.find("int") != std::string::npos);
		}
		if (!Expect(
		        Result.bSuccess && bFirstIsString && bSecondIsInt,
		        "member completion should specialize explicit and default type arguments"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* ExactAritySource = R"(
template<T = int>
class Box { public: T genericValue; }
class Box { public: int plainValue; }
void main() {
	Box value = Box();
	value.
}
)";
		FTextPosition ExactArityPosition;
		if (!Expect(
		        FindPosition(ExactAritySource, "\tvalue.", ExactArityPosition),
		        "locate exact-arity member completion"))
		{
			return EXIT_FAILURE;
		}
		ExactArityPosition.Column += static_cast<std::uint32_t>(
		    std::string_view("\tvalue.").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    ExactAritySource,
		    ExactArityPosition,
		    "<complete-exact-generic-arity>");
		bool bHasPlain = false;
		bool bHasGeneric = false;
		for (const FCompletionItem& Item : Result.Items)
		{
			bHasPlain = bHasPlain || Item.Label == "plainValue";
			bHasGeneric = bHasGeneric || Item.Label == "genericValue";
		}
		if (!Expect(
		        Result.bSuccess && bHasPlain && !bHasGeneric,
		        "bare type completion prefers the exact non-generic declaration"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		for (const auto& [Marker, First, Second] :
		    std::initializer_list<std::tuple<const char*, const char*, const char*>>{
		        {"\tfield.", "get", "set"},
		        {"\tmethod_meta.", "invoke", "invoke_static"}})
		{
			const std::string ReflectionSource = std::string(R"(
struct Counter { int value; int add(int delta) { return value + delta; } }
void main() {
	field_info<Counter> field = field_info<Counter>(type<Counter>().qualified_name, 0);
	method_info<Counter> method_meta = method_info<Counter>(type<Counter>().qualified_name, 0);
)") + Marker + "\n}\n";
			FTextPosition Position;
			if (!Expect(FindPosition(ReflectionSource, Marker, Position),
			        "locate executable reflection completion"))
			{
				return EXIT_FAILURE;
			}
			Position.Column += static_cast<std::uint32_t>(std::string_view(Marker).size());
			const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
			    ReflectionSource, Position, "<complete-executable-reflection>");
			if (!Expect(Result.bSuccess && HasLabel(Result.Items, First) && HasLabel(Result.Items, Second),
			        "executable reflection members should be available to tooling"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		constexpr const char* ReflectionSource = R"(
enum class Status { Ready }
void main() {
	module_info module = module_info();
	module.
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(ReflectionSource, "\tmodule.", Position),
		        "locate module reflection completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\tmodule.").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    ReflectionSource, Position, "<complete-module-reflection>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "function_count") && HasLabel(Result.Items, "global_count") && HasLabel(Result.Items, "type_name"),
		        "module reflection members should be available to tooling"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* ReflectionSource = R"(
void main() {
	function_info function_meta = function_info(0);
	function_meta.
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(ReflectionSource, "\tfunction_meta.", Position),
		        "locate function reflection completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\tfunction_meta.").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    ReflectionSource, Position, "<complete-function-reflection>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "parameter_type") && HasLabel(Result.Items, "has_metadata") && HasLabel(Result.Items, "has_annotation"),
		        "function reflection metadata members should be available to tooling"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* ReflectionSource = R"(
enum class Status { Ready }
void main() {
	type<Status> type_info = type<Status>();
	enum_member_info<Status> member = enum_member_info<Status>(type_info.qualified_name, 0);
	member.
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(ReflectionSource, "\tmember.", Position),
		        "locate enum-member reflection completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("\tmember.").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    ReflectionSource, Position, "<complete-enum-member-reflection>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "value") && HasLabel(Result.Items, "metadata") && HasLabel(Result.Items, "has_annotation"),
		        "enum-member reflection members should be available to tooling"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// A namespace is a valid qualifier in statement/expression/type contexts.
		// Bare completion must offer the root itself, not flatten every qualified
		// child into `std.abs`, `std.array`, ... candidates.
		constexpr const char* BareStandardNamespaceSource = R"(
void main() {
    std
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(BareStandardNamespaceSource, "    std", Position),
		        "locate bare standard namespace completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(
		    std::string_view("    std").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    BareStandardNamespaceSource,
		    Position,
		    "<complete-bare-standard-namespace>");
		const int StdIndex = LabelIndex(Result.Items, "std");
		if (!Expect(
		        Result.bSuccess && StdIndex >= 0 && Result.Items[static_cast<std::size_t>(StdIndex)].Kind == EOutlineSymbolKind::Namespace && Result.Items[static_cast<std::size_t>(StdIndex)].InsertText == "std::" && !HasLabel(Result.Items, "std.abs") && !HasLabel(Result.Items, "std.array"),
		        "bare standard namespace completion remains hierarchical"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* UserNamespaceRootSource = R"(
namespace Tools {
	int helper = 1;
}
void main() {
	Too
}
)";
		FTextPosition RootPosition;
		if (!Expect(
		        FindPosition(UserNamespaceRootSource, "\tToo", RootPosition),
		        "locate user namespace root completion"))
		{
			return EXIT_FAILURE;
		}
		RootPosition.Column += static_cast<std::uint32_t>(std::string_view("\tToo").size());
		const FCompletionResult RootResult = VeroScriptComplete::CompleteAtSource(
		    UserNamespaceRootSource,
		    RootPosition,
		    "<complete-user-namespace-root>");
		const int ToolsIndex = LabelIndex(RootResult.Items, "Tools");
		if (!Expect(
		        RootResult.bSuccess && ToolsIndex >= 0 && RootResult.Items[static_cast<std::size_t>(ToolsIndex)].Kind == EOutlineSymbolKind::Namespace && RootResult.Items[static_cast<std::size_t>(ToolsIndex)].InsertText == "Tools::" && !HasLabel(RootResult.Items, "Tools.helper"),
		        "user namespace root completion remains hierarchical"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* ActiveNamespaceMemberSource = R"(
namespace Tools {
	int helper = 1;
	void use() {
		hel
	}
}
)";
		FTextPosition MemberPosition;
		if (!Expect(
		        FindPosition(ActiveNamespaceMemberSource, "\t\thel", MemberPosition),
		        "locate active namespace member completion"))
		{
			return EXIT_FAILURE;
		}
		MemberPosition.Column += static_cast<std::uint32_t>(std::string_view("\t\thel").size());
		const FCompletionResult MemberResult = VeroScriptComplete::CompleteAtSource(
		    ActiveNamespaceMemberSource,
		    MemberPosition,
		    "<complete-active-namespace-member>");
		if (!Expect(
		        MemberResult.bSuccess && HasLabel(MemberResult.Items, "helper") && !HasLabel(MemberResult.Items, "Tools.helper"),
		        "active namespace members use their unqualified spelling"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* StandardNamespaceSource = R"(
void main() {
    std::
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(StandardNamespaceSource, "    std::", Position),
		        "locate standard namespace completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(
		    std::string_view("    std::").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    StandardNamespaceSource, Position, "<complete-standard-namespace>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "array") && HasLabel(Result.Items, "optional") && HasLabel(Result.Items, "readonly") && HasLabel(Result.Items, "print") && HasLabel(Result.Items, "chrono") && HasLabel(Result.Items, "io") && HasLabel(Result.Items, "filesystem") && HasLabel(Result.Items, "strings") && HasLabel(Result.Items, "concurrent") && HasLabel(Result.Items, "algorithm") && HasLabel(Result.Items, "numeric") && HasLabel(Result.Items, "random") && HasLabel(Result.Items, "environment"),
		        "standard namespace aliases should be available to tooling"))
		{
			return EXIT_FAILURE;
		}
		for (const char* NamespaceLeaf : {
		         "chrono", "io", "filesystem", "strings", "concurrent", "algorithm",
		         "numeric", "random", "environment"})
		{
			const int Index = LabelIndex(Result.Items, NamespaceLeaf);
			if (!Expect(
			        Index >= 0 && Result.Items[static_cast<std::size_t>(Index)].Kind == EOutlineSymbolKind::Namespace && Result.Items[static_cast<std::size_t>(Index)].InsertText == std::string(NamespaceLeaf) + "::",
			        "intermediate standard namespace completion inserts a source qualifier"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		constexpr const char* StandardAlgorithmNamespaceSource = R"(
void main() {
    std::algorithm::
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(StandardAlgorithmNamespaceSource, "    std::algorithm::", Position),
		        "locate algorithm standard namespace completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(
		    std::string_view("    std::algorithm::").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    StandardAlgorithmNamespaceSource,
		    Position,
		    "<complete-standard-algorithm-namespace>");
		if (!Expect(
		        Result.bSuccess && HasDetail(Result.Items, "sort", "void std::algorithm::sort(array<T> values);") && HasDetail(Result.Items, "sort", "Requires import \"stdlib/algorithm.vs\" to compile.") && HasDetail(Result.Items, "binary_search", "bool std::algorithm::binary_search(array<T> values, T value);") && HasDetail(Result.Items, "contains", "bool std::algorithm::contains(array<T> values, T value);") && HasDetail(Result.Items, "shuffle", "array<unsigned int> engine"),
		        "algorithm namespace should expose typed algorithm functions"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* StandardChronoNamespaceSource = R"(
void main() {
    std::chrono::
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(StandardChronoNamespaceSource, "    std::chrono::", Position),
		        "locate chrono standard namespace completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(
		    std::string_view("    std::chrono::").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    StandardChronoNamespaceSource, Position, "<complete-standard-chrono-namespace>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "now") && !HasLabel(Result.Items, "read_int") && !HasLabel(Result.Items, "shuffle") && HasDetail(Result.Items, "now", "double std::chrono::now()"),
		        "chrono namespace completion contains only chrono members with a qualified declaration"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* ScopedStandardNamespaceSource = R"(
void main() {
    std::ch
}
)";
		FTextPosition ScopedPosition;
		if (!Expect(
		        FindPosition(ScopedStandardNamespaceSource, "std::ch", ScopedPosition),
		        "locate scoped standard namespace completion"))
		{
			return EXIT_FAILURE;
		}
		ScopedPosition.Column += static_cast<std::uint32_t>(
		    std::string_view("std::ch").size());
		const FCompletionResult ScopedResult = VeroScriptComplete::CompleteAtSource(
		    ScopedStandardNamespaceSource,
		    ScopedPosition,
		    "<complete-scoped-standard-namespace>");
		const int ChronoIndex = LabelIndex(ScopedResult.Items, "chrono");
		if (!Expect(
		        ScopedResult.bSuccess && ChronoIndex >= 0 && ScopedResult.Items[static_cast<std::size_t>(ChronoIndex)].InsertText == "chrono::",
		        "partial namespace completion appends a source qualifier"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* ExistingSeparatorSource =
		    "void main() { std::chrono::now(); }";
		FTextPosition ExistingPosition;
		if (!Expect(FindPosition(ExistingSeparatorSource, "std::ch", ExistingPosition),
		        "locate namespace completion before existing separator"))
		{
			return EXIT_FAILURE;
		}
		ExistingPosition.Column += static_cast<std::uint32_t>(
		    std::string_view("std::ch").size());
		const FCompletionResult ExistingResult = VeroScriptComplete::CompleteAtSource(
		    ExistingSeparatorSource, ExistingPosition,
		    "<complete-before-existing-namespace-separator>");
		const int ExistingChronoIndex = LabelIndex(ExistingResult.Items, "chrono");
		if (!Expect(ExistingResult.bSuccess && ExistingChronoIndex >= 0 && ExistingResult.Items[static_cast<std::size_t>(ExistingChronoIndex)].InsertText == "chrono",
		        "completion before an existing scope separator does not duplicate it"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* StandardNumericNamespaceSource = R"(
void main() {
    std::numeric::
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(StandardNumericNamespaceSource, "    std::numeric::", Position),
		        "locate numeric standard namespace completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(
		    std::string_view("    std::numeric::").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    StandardNumericNamespaceSource,
		    Position,
		    "<complete-standard-numeric-namespace>");
		if (!Expect(
		        Result.bSuccess && HasDetail(Result.Items, "accumulate", "T std::numeric::accumulate(array<T> values, T initial);") && HasDetail(Result.Items, "sum", "T std::numeric::sum(array<T> values);") && HasDetail(Result.Items, "average", "double std::numeric::average(array<T> values);"),
		        "numeric namespace should expose typed aggregate functions"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* StandardRandomNamespaceSource = R"(
void main() {
    std::random::
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(StandardRandomNamespaceSource, "    std::random::", Position),
		        "locate random standard namespace completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(
		    std::string_view("    std::random::").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    StandardRandomNamespaceSource,
		    Position,
		    "<complete-standard-random-namespace>");
		if (!Expect(
		        Result.bSuccess && HasDetail(Result.Items, "make_engine", "array<unsigned int>") && HasDetail(Result.Items, "next_uint", "array<unsigned int> engine") && HasDetail(Result.Items, "next_bounded_uint", "unsigned int upper_exclusive") && HasDetail(Result.Items, "next_int", "int minimum, int maximum_exclusive") && HasDetail(Result.Items, "next_double", "double std::random::next_double(array<unsigned int> engine);") && HasDetail(Result.Items, "next_double_range", "double minimum, double maximum_exclusive"),
		        "random namespace should expose typed engine functions"))
		{
			for (const FCompletionItem& Item : Result.Items)
			{
				std::cerr << "random completion=[" << Item.Label
				          << "] detail=[" << Item.Detail << "]\n";
			}
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* StandardStringNamespaceSource = R"(
void main() {
    std::strings::
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(StandardStringNamespaceSource, "    std::strings::", Position),
		        "locate nested standard namespace completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(
		    std::string_view("    std::strings::").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    StandardStringNamespaceSource,
		    Position,
		    "<complete-standard-string-namespace>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "contains") && HasLabel(Result.Items, "trim") && HasLabel(Result.Items, "split") && !HasLabel(Result.Items, "sqrt"),
		        "nested standard namespace should expose only its responsibility"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* StandardEnvironmentNamespaceSource = R"(
void main() {
    std::environment::
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(
		            StandardEnvironmentNamespaceSource,
		            "    std::environment::",
		            Position),
		        "locate environment standard namespace completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(
		    std::string_view("    std::environment::").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    StandardEnvironmentNamespaceSource,
		    Position,
		    "<complete-standard-environment-namespace>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "getenv") && HasLabel(Result.Items, "setenv") && !HasLabel(Result.Items, "now"),
		        "environment namespace should expose only environment functions"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* StandardContainerSource = R"(
void main() {
    std::array<int> values = {1, 2};
    values.
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(StandardContainerSource, "    values.", Position),
		        "locate standard container member completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(
		    std::string_view("    values.").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    StandardContainerSource, Position, "<complete-standard-container>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "push") && HasLabel(Result.Items, "clear") && HasLabel(Result.Items, "capacity"),
		        "standard container aliases should retain canonical members"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* ExtensionMethodSource = R"(
struct Counter {
    int value = 0;
};
int grow(ref Counter self, int amount) {
    self.value += amount;
    return self.value;
}
T choose<T>(T self, T other) { return other; }
void main() {
    Counter counter();
    counter.gr
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(ExtensionMethodSource, "    counter.gr", Position),
		        "locate extension-method completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(
		    std::string_view("    counter.gr").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    ExtensionMethodSource,
		    Position,
		    "<complete-extension-method>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "grow"),
		        "applicable free function should complete as an extension method"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* ExtensionChainSource = R"(
struct Counter {
    int value = 0;
};
struct Holder<T> {
    T held;
};
T take<T>(Holder<T> self) { return self.held; }
void main() {
    Holder<Counter> holder();
    holder.take().va
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(ExtensionChainSource, "    holder.take().va", Position),
		        "locate chained extension-method completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(
		    std::string_view("    holder.take().va").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    ExtensionChainSource,
		    Position,
		    "<complete-extension-chain>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "value"),
		        "generic extension return type should drive chained completion"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* NamespacedExtensionSource = R"(
struct Counter { int value = 0; };
namespace Visible { int grow(Counter self, int amount) { return amount; } }
namespace Hidden { int ghost(Counter self) { return -1; } }
using namespace Visible;
void main() {
    Counter counter();
    counter.
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(NamespacedExtensionSource, "    counter.", Position),
		        "locate namespaced extension completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(
		    std::string_view("    counter.").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    NamespacedExtensionSource,
		    Position,
		    "<complete-namespaced-extension>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "grow") && !HasLabel(Result.Items, "ghost"),
		        "extension completion should respect using namespace visibility"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* HiddenUsingExtensionSource = R"(
struct Counter { int value = 0; };
namespace Visible { int grow(Counter self, int amount) { return amount; } }
using namespace Visible;
int grow(int self, int amount) { return self + amount; }
void main() {
    Counter counter();
    counter.
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(HiddenUsingExtensionSource, "    counter.", Position),
		        "locate hidden using extension completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(
		    std::string_view("    counter.").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    HiddenUsingExtensionSource,
		    Position,
		    "<complete-hidden-using-extension>");
		if (!Expect(
		        Result.bSuccess && !HasLabel(Result.Items, "grow"),
		        "lexical free-function declarations must hide using extension overloads"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Runtime-owned namespaces must remain useful before any source stdlib
		// import is available. Their completions come from the same canonical
		// aliases as the compiler, not from an editor-only vocabulary.
		constexpr const char* ConsoleNamespaceSource = R"(
void main() {
    std::console::
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(ConsoleNamespaceSource, "    std::console::", Position),
		        "locate console standard namespace completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(
		    std::string_view("    std::console::").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    ConsoleNamespaceSource, Position, "<complete-standard-console-namespace>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "write") && HasLabel(Result.Items, "clear") && HasLabel(Result.Items, "is_interactive") && HasDetail(Result.Items, "write", "void std::console::write(any value)"),
		        "console namespace completion exposes precise runtime signatures"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* ExtendedMathSource = R"(
void main() {
    std::round
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(ExtendedMathSource, "std::round", Position),
		        "locate extended math completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("std::").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    ExtendedMathSource, Position, "<complete-extended-math>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "round") && HasDetail(Result.Items, "round", "double std::round(double value)"),
		        "extended math completion retains its canonical signature"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* ConstructorInitializerTargetSource = R"(
struct Payload { Payload(int value) {} }
class Base { public: Base() {} }
class Holder : public Base {
public:
	const int fixed = 1;
	const Payload payload;
	int count;
	static int shared;
	Payload payloadFactory(int value);
	Holder(int value) : Base(), pa
	{
	}
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(ConstructorInitializerTargetSource, ": Base(), pa", Position),
		        "locate constructor initializer target completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(
		    std::string_view(": Base(), pa").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    ConstructorInitializerTargetSource,
		    Position,
		    "<complete-constructor-initializer-target>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "payload") && HasDetail(Result.Items, "payload", "Payload") && !HasLabel(Result.Items, "Base") && !HasLabel(Result.Items, "fixed") && !HasLabel(Result.Items, "shared") && !HasLabel(Result.Items, "payloadFactory"),
		        "constructor initializer completion offers only legal matching targets"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* OutOfClassInitializerTargetSource = R"(
struct External {
	const int value;
	External(int input);
}
External::External(int input) : val
{
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(OutOfClassInitializerTargetSource, ": val", Position),
		        "locate out-of-class initializer target completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view(": val").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    OutOfClassInitializerTargetSource,
		    Position,
		    "<complete-out-of-class-initializer-target>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "value"),
		        "out-of-class initializer completion offers direct fields"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* NestedInitializerCallSource = R"(
int nested(int value) { return value; }
struct Payload { Payload(int value) {} }
class Holder {
public:
	const Payload payload;
	const Payload nested;
	Holder(int value) : payload(nested(value)), ne
	{
	}
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(NestedInitializerCallSource, ")), ne", Position),
		        "locate target after nested initializer call"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view(")), ne").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    NestedInitializerCallSource,
		    Position,
		    "<complete-after-nested-initializer-call>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "nested") && !HasLabel(Result.Items, "payload"),
		        "nested argument calls do not consume constructor initializer targets"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* QualifiedOutOfClassInitializerTargetSource = R"(
namespace A {
	struct External { const int wrong; External(int input); }
}
namespace B {
	struct External { const int right; External(int input); }
}
B::External::External(int input) : ri
{
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(QualifiedOutOfClassInitializerTargetSource, ": ri", Position),
		        "locate qualified out-of-class initializer target"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view(": ri").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    QualifiedOutOfClassInitializerTargetSource,
		    Position,
		    "<complete-qualified-out-of-class-initializer-target>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "right") && !HasLabel(Result.Items, "wrong"),
		        "qualified out-of-class completion selects the exact owner type"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* InterfaceInitializerTargetSource = R"(
class Base { public: Base() {} }
interface Contract { int read(); }
class Holder : public Base, Contract {
public:
	Holder() :)"
		                                                         " \n"
		                                                         R"(	{
	}
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(InterfaceInitializerTargetSource, "Holder() : ", Position),
		        "locate initializer target after interface inheritance"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("Holder() : ").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    InterfaceInitializerTargetSource,
		    Position,
		    "<complete-interface-initializer-target>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "Base") && !HasLabel(Result.Items, "Contract"),
		        "constructor initializer completion excludes interfaces"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* NamespaceLocalOutOfClassInitializerSource = R"(
namespace Outer {
	struct External {
		const int value;
		External(int input);
	}
	External::External(int input) : val
	{
	}
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(NamespaceLocalOutOfClassInitializerSource, ": val", Position),
		        "locate namespace-local out-of-class initializer target"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view(": val").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    NamespaceLocalOutOfClassInitializerSource,
		    Position,
		    "<complete-namespace-local-out-of-class-initializer-target>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "value"),
		        "namespace-local out-of-class completion finds its owner type"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* QualifiedGenericInitializerTargetSource = R"(
namespace A {
	template<T> struct Box { const int wrong; Box(int input); }
}
namespace B {
	template<T> struct Box { const int right; Box(int input); }
}
template<T>
B::Box<T>::Box(int input) : ri
{
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(QualifiedGenericInitializerTargetSource, ": ri", Position),
		        "locate qualified generic out-of-class initializer target"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view(": ri").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    QualifiedGenericInitializerTargetSource,
		    Position,
		    "<complete-qualified-generic-initializer-target>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "right") && !HasLabel(Result.Items, "wrong"),
		        "qualified generic completion selects the exact owner type"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* AnnotatedInitializerTargetSource = R"(
struct Payload { Payload(int value) {} }
class Holder {
public:
	@data(primary) const Payload annotated;
	Holder(int value) : ann
	{
	}
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(AnnotatedInitializerTargetSource, ": ann", Position),
		        "locate annotated field initializer target"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view(": ann").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    AnnotatedInitializerTargetSource,
		    Position,
		    "<complete-annotated-initializer-target>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "annotated") && HasDetail(Result.Items, "annotated", "Payload"),
		        "constructor initializer completion retains annotated fields"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* QualifiedBaseInitializerTargetSource = R"(
namespace N {
	class Base { public: Base() {} }
}
class Holder : public N::Base {
public:
	Holder() : N
	{
	}
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(QualifiedBaseInitializerTargetSource, "Holder() : N", Position),
		        "locate initializer target for a qualified base"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(
		    std::string_view("Holder() : N").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    QualifiedBaseInitializerTargetSource,
		    Position,
		    "<complete-qualified-base-initializer-target>");
		if (!Expect(
		        Result.bSuccess && !HasLabel(Result.Items, "N"),
		        "constructor initializer completion does not truncate qualified bases"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* FunctionFieldInitializerTargetSource = R"(
class Holder {
public:
	fn(int)->int callback;
	Holder(fn(int)->int input) : call
	{
	}
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(FunctionFieldInitializerTargetSource, ": call", Position),
		        "locate function field initializer target"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view(": call").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    FunctionFieldInitializerTargetSource,
		    Position,
		    "<complete-function-field-initializer-target>");
		if (!Expect(
		        Result.bSuccess && HasLabel(Result.Items, "callback") && HasDetail(Result.Items, "callback", "fn"),
		        "constructor initializer completion retains function-typed fields"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* InitializerArgumentSource = R"(
struct Payload { Payload(int left, int right) {} }
class Holder {
public:
	const Payload payload;
	const Payload pending;
	Holder(int value) : payload(value, pe
	{
	}
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(InitializerArgumentSource, "payload(value, pe", Position),
		        "locate completion inside initializer arguments"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(
		    std::string_view("payload(value, pe").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    InitializerArgumentSource,
		    Position,
		    "<complete-inside-initializer-arguments>");
		if (!Expect(
		        Result.bSuccess && !HasLabel(Result.Items, "pending"),
		        "initializer argument completion is not replaced by target-only fallback"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::string Source =
		    "class Base{protected:Base(int value){}};\n"
		    "class Derived:public Base{int marker;public:Derived(int actual_value);};\n"
		    "Derived::Derived(int actual_value):Base(actual_value),marker(actual_value){}\n";
		FTextPosition Position;
		if (!Expect(FindPosition(Source, ":Base(actual_", Position), "locate out-of-class initializer parameter completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view(":Base(actual_").size());
		const auto Result = VeroScriptComplete::CompleteAtSource(Source, Position, "<constructor-initializer-parameter>");
		if (!Expect(Result.bSuccess && HasLabel(Result.Items, "actual_value"),
		        "constructor parameters remain visible before the body in out-of-class definitions"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::string Source =
		    "void main(){\nint outer_value=1;\n"
		    "class Local{public:Local(int inner_value){}};\n    out\n}\n";
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "    out", Position), "locate completion after local type"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += 7;
		const auto Result = VeroScriptComplete::CompleteAtSource(Source, Position, "<callable-source-range>");
		if (!Expect(Result.bSuccess && HasLabel(Result.Items, "outer_value") && !HasLabel(Result.Items, "inner_value"),
		        "outer callable resumes after a nested local type's constructor"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::string Source =
		    "void main(){int outer_value=1;class Local{public:Local(int inner_value){inn}};}";
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "{inn}", Position), "locate same-line nested constructor completion"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += 4;
		const auto Result = VeroScriptComplete::CompleteAtSource(Source, Position, "<nested-callable-source-range>");
		if (!Expect(Result.bSuccess && HasLabel(Result.Items, "inner_value"),
		        "innermost callable wins when nested source ranges share the same line"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::string Source =
		    "class Base<T,int N>{public:virtual T value(){return cast<T>(N);}};\n"
		    "class Derived<T,int N>:public Base<T,N>{\npublic:\n    val\n}\n";
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "    val", Position), "locate mixed template override prefix"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += 7;
		const auto Result = VeroScriptComplete::CompleteAtSource(Source, Position, "<value-template-override>");
		bool bOverride = false;
		for (const auto& Item : Result.Items)
		{
			bOverride |= Item.bOverrideMember && Item.Label == "value" && Item.InsertText.find(" override") != std::string::npos;
		}
		if (!Expect(Result.bSuccess && bOverride && Result.bHasReplaceRange && Result.ReplaceRange.StartColumn == 5 && Result.ReplaceRange.EndColumn == 8,
		        "override owner retains both type and value template arguments"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::string Source =
		    "class Left { public: virtual int read(int value) { return value; } "
		    "virtual int read(string value) { return 0; } "
		    "virtual void sealed() final {} "
		    "private: virtual void hidden() {} }\n"
		    "class Right { public: virtual void other() = 0; }\n"
		    "interface Contract<T> { virtual T get() = 0; }\n"
		    "class Derived : public Left, public Right, public Contract<int> {\n"
		    "public:\n    rea\n}\n";
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "    rea", Position),
		        "locate incomplete override prefix"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += 7;
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    Source, Position, "<complete-override-prefix>");
		std::size_t OverrideCount = 0;
		bool bInt = false;
		bool bString = false;
		for (const FCompletionItem& Item : Result.Items)
		{
			if (!Item.bOverrideMember)
			{
				continue;
			}
			++OverrideCount;
			bInt = bInt || Item.Detail.find("read(int value)") != std::string::npos;
			bString = bString || Item.Detail.find("read(string value)") != std::string::npos;
			if (!Expect(Item.Label == "read" && Item.bInsertTextSnippet && Item.InsertText.find(" override") != std::string::npos,
			        "override candidates keep full signatures and editable bodies"))
			{
				return EXIT_FAILURE;
			}
		}
		if (!Expect(Result.bSuccess && OverrideCount == 2 && bInt && bString && Result.bHasReplaceRange && Result.ReplaceRange.StartColumn == 5 && Result.ReplaceRange.EndColumn == 8,
		        "incomplete member prefix yields both overloads with exact replacement"))
		{
			std::cerr << "success=" << Result.bSuccess << " count=" << OverrideCount
			          << " int=" << bInt << " string=" << bString << " range="
			          << Result.bHasReplaceRange << ':' << Result.ReplaceRange.StartColumn
			          << '-' << Result.ReplaceRange.EndColumn << '\n';
			for (const FCompletionItem& Item : Result.Items)
			{
				std::cerr << Item.Label << " | " << Item.Detail
				          << " | override=" << Item.bOverrideMember << '\n';
			}
			for (const auto& Diagnostic : Result.Diagnostics)
			{
				std::cerr << "diagnostic: " << Diagnostic.Message << '\n';
			}
			return EXIT_FAILURE;
		}
		std::string Blank = Source;
		Blank.replace(Blank.find("rea\n"), 3, "   ");
		const FCompletionResult BlankResult = VeroScriptComplete::CompleteAtSource(
		    Blank, Position, "<complete-override-blank>");
		std::size_t BlankOverrides = 0;
		bool bInterface = false;
		bool bOther = false;
		bool bForbidden = false;
		for (const FCompletionItem& Item : BlankResult.Items)
		{
			if (!Item.bOverrideMember)
			{
				continue;
			}
			++BlankOverrides;
			bInterface = bInterface || (Item.Label == "get" && Item.Detail.find("int get()") != std::string::npos && Item.InsertText.find("override") == std::string::npos);
			bOther = bOther || Item.Label == "other";
			bForbidden = bForbidden || Item.Label == "sealed" || Item.Label == "hidden";
		}
		if (!Expect(BlankResult.bSuccess && BlankOverrides == 4 && bInterface && bOther && !bForbidden,
		        "blank type body offers inherited and specialized interface members only"))
		{
			return EXIT_FAILURE;
		}
		std::string AlreadyOverridden = Source;
		AlreadyOverridden.replace(AlreadyOverridden.find("    rea\n"), 7,
		    "    int read(int value) override { return value; }\n    rea");
		FTextPosition AlreadyPosition;
		if (!Expect(FindPosition(AlreadyOverridden, "    rea\n", AlreadyPosition),
		        "locate prefix after one overload was overridden"))
		{
			return EXIT_FAILURE;
		}
		AlreadyPosition.Column += 7;
		const FCompletionResult AlreadyResult = VeroScriptComplete::CompleteAtSource(
		    AlreadyOverridden, AlreadyPosition, "<complete-already-overridden>");
		std::size_t Remaining = 0;
		bool bRemainingString = false;
		for (const FCompletionItem& Item : AlreadyResult.Items)
		{
			if (!Item.bOverrideMember || Item.Label != "read")
			{
				continue;
			}
			++Remaining;
			bRemainingString = bRemainingString || Item.Detail.find("read(string value)") != std::string::npos;
		}
		if (!Expect(AlreadyResult.bSuccess && Remaining == 1 && bRemainingString,
		        "already overridden overload is excluded without hiding other overloads"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		struct FCase
		{
			std::string BaseMembers;
			std::string ImplementedMember;
			std::string ExpectedParameter;
		};
		const std::vector<FCase> Cases = {
		    {"virtual int Read(const int value){return value;}",
		        "int Read(int value) override{return value;}", ""},
		    {"virtual int Read(int value){return value;}",
		        "int Read(const int value) override{return value;}", ""},
		    {"virtual int Read(const int value){return value;}", "", "const int"},
		    {"virtual int Read(int value){return value;}", "", "int"},
		    {"virtual int Read(ref int value){return value;}"
		     "virtual int Read(const ref int value){return value;}",
		        "int Read(ref int value) override{return value;}", "const ref int"},
		    {"virtual int Read(ref int value){return value;}"
		     "virtual int Read(const ref int value){return value;}",
		        "int Read(const ref int value) override{return value;}", "ref int"},
		};
		for (const FCase& Test : Cases)
		{
			const std::string Source = "class Base{public:" + Test.BaseMembers + "};class Derived:public Base{public:" + Test.ImplementedMember + "\n    Rea\n};void main(){}";
			FTextPosition Position;
			if (!Expect(FindPosition(Source, "    Rea\n", Position),
			        "locate override parameter qualifier prefix"))
			{
				return EXIT_FAILURE;
			}
			Position.Column += 7;
			const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
			    Source, Position, "<complete-override-parameter-identity>");
			std::size_t OverrideCount = 0;
			std::string Generated;
			for (const FCompletionItem& Item : Result.Items)
			{
				if (!Item.bOverrideMember || Item.Label != "Read")
				{
					continue;
				}
				++OverrideCount;
				Generated = Item.InsertText;
				if (!Expect(Item.Detail.find("Read(" + Test.ExpectedParameter + " value)") != std::string::npos,
				        "remaining override preserves its parameter qualifier in the detail"))
				{
					return EXIT_FAILURE;
				}
			}
			if (!Expect(Result.bSuccess && OverrideCount == (Test.ExpectedParameter.empty() ? 0 : 1),
			        "value const shares an override slot while reference const distinguishes overloads"))
			{
				std::cerr << Source << '\n'
				          << Generated << '\n';
				return EXIT_FAILURE;
			}
			if (Generated.empty())
			{
				continue;
			}
			if (!Expect(Generated.find("Read(" + Test.ExpectedParameter + " value) override") != std::string::npos,
			        "remaining override preserves its parameter qualifier in the insertion"))
			{
				return EXIT_FAILURE;
			}
			const std::size_t Stub = Generated.find("${0:throw 0;}");
			if (!Expect(Stub != std::string::npos, "parameter qualifier override has an editable body"))
			{
				return EXIT_FAILURE;
			}
			Generated.replace(Stub, 13, "throw 0;");
			std::string Applied = Source;
			Applied.replace(Applied.find("Rea\n"), 3, Generated);
			const auto Compiled = VeroScript::VeroScriptCompile::CompileSource(
			    Applied, "<complete-override-parameter-identity-generated>");
			if (!Expect(Compiled.bSuccess, "remaining parameter qualifier override compiles after applying its edit"))
			{
				std::cerr << Applied << '\n'
				          << Compiled.Error.Message << '\n';
				return EXIT_FAILURE;
			}
		}
	}

	{
		const std::string Source =
		    "class Base<T> { public: virtual T fetch(T value) { return value; } }\n"
		    "class Child<U> : public Base<U> { U stored; public: fet\n}\n";
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "fet\n", Position),
		        "locate generic override prefix"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += 3;
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    Source, Position, "<complete-override-generic>");
		bool bSpecialized = false;
		for (const FCompletionItem& Item : Result.Items)
		{
			bSpecialized = bSpecialized || (Item.bOverrideMember && Item.Label == "fetch" && Item.Detail.find("U fetch(U value)") != std::string::npos && Item.InsertText.find("U fetch(U value) override") != std::string::npos);
		}
		if (!Expect(Result.bSuccess && bSpecialized,
		        "generic inherited method uses the derived type argument"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::string Source =
		    "class Base { public: virtual int inspect(const int& value) const "
		    "{ return value; } }\n"
		    "class Child : public Base {\npublic:\n    ins\n}\n";
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "    ins\n", Position),
		        "locate const reference override prefix"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += 7;
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    Source, Position, "<complete-override-const-ref>");
		std::string Generated;
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.bOverrideMember && Item.Label == "inspect")
			{
				Generated = Item.InsertText;
				break;
			}
		}
		if (!Expect(Generated.find("const ref int value) const override") != std::string::npos,
		        "override skeleton preserves const reference parameter and method qualifier"))
		{
			return EXIT_FAILURE;
		}
		const std::size_t Stub = Generated.find("${0:throw 0;}");
		if (!Expect(Stub != std::string::npos,
		        "const reference override contains editable body"))
		{
			return EXIT_FAILURE;
		}
		Generated.replace(Stub, 13, "throw 0;");
		std::string Compilable = Source;
		Compilable.replace(Compilable.find("ins\n"), 3, Generated);
		const VeroScript::FCompileResult Compiled =
		    VeroScript::VeroScriptCompile::CompileSource(
		        Compilable, "<complete-override-const-ref-generated>");
		if (!Expect(Compiled.bSuccess,
		        "generated const reference override compiles"))
		{
			for (const auto& Diagnostic : Compiled.Diagnostics)
			{
				std::cerr << Diagnostic.Message << '\n';
			}
			return EXIT_FAILURE;
		}
	}

	{
		const std::string Source =
		    "class Base { public: virtual int read(int value) { return value; } }\n"
		    "class Child : public Base {\npublic:\n    \n}\n";
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "    \n", Position),
		        "locate override insertion site"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += 4;
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    Source, Position, "<complete-override-compile>");
		std::string Generated;
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.bOverrideMember && Item.Label == "read")
			{
				Generated = Item.InsertText;
				break;
			}
		}
		if (!Expect(!Generated.empty(), "find generated override"))
		{
			return EXIT_FAILURE;
		}
		const std::size_t Stub = Generated.find("${0:throw 0;}");
		if (!Expect(Stub != std::string::npos, "generated override has editable body"))
		{
			return EXIT_FAILURE;
		}
		Generated.replace(Stub, 13, "throw 0;");
		std::string Compilable = Source;
		Compilable.insert(Compilable.find("    \n") + 4, Generated);
		const VeroScript::FCompileResult Compiled =
		    VeroScript::VeroScriptCompile::CompileSource(
		        Compilable, "<complete-generated-override>");
		if (!Expect(Compiled.bSuccess,
		        "generated override compiles after applying its edit"))
		{
			for (const auto& Diagnostic : Compiled.Diagnostics)
			{
				std::cerr << Diagnostic.Message << '\n';
			}
			return EXIT_FAILURE;
		}
		std::string ExistingHeader = Source;
		ExistingHeader.replace(ExistingHeader.find("    \n"), 4, "    read(");
		FTextPosition HeaderPosition;
		if (!Expect(FindPosition(ExistingHeader, "    read(", HeaderPosition),
		        "locate existing method header"))
		{
			return EXIT_FAILURE;
		}
		HeaderPosition.Column += 8;
		const FCompletionResult HeaderResult = VeroScriptComplete::CompleteAtSource(
		    ExistingHeader, HeaderPosition, "<complete-existing-method-header>");
		bool bUnexpectedOverride = false;
		for (const FCompletionItem& Item : HeaderResult.Items)
		{
			bUnexpectedOverride = bUnexpectedOverride || Item.bOverrideMember;
		}
		if (!Expect(!bUnexpectedOverride,
		        "existing method header does not offer insertion skeletons"))
		{
			return EXIT_FAILURE;
		}
		std::string CrLfSource;
		CrLfSource.reserve(Source.size() + 8);
		for (const char Character : Source)
		{
			if (Character == '\n')
			{
				CrLfSource.push_back('\r');
			}
			CrLfSource.push_back(Character);
		}
		FTextPosition CrLfPosition;
		if (!Expect(FindPosition(CrLfSource, "    \r\n", CrLfPosition),
		        "locate CRLF override insertion site"))
		{
			return EXIT_FAILURE;
		}
		CrLfPosition.Column += 4;
		const FCompletionResult CrLfResult = VeroScriptComplete::CompleteAtSource(
		    CrLfSource, CrLfPosition, "<complete-override-crlf>");
		bool bPreservedCrLf = false;
		for (const FCompletionItem& Item : CrLfResult.Items)
		{
			if (Item.bOverrideMember && Item.Label == "read")
			{
				bPreservedCrLf = Item.InsertText.find("\r\n") != std::string::npos;
				break;
			}
		}
		if (!Expect(bPreservedCrLf,
		        "generated override preserves CRLF line endings"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::string Source =
		    "interface Contract<T> { virtual T get() = 0; }\n"
		    "class Child : public Contract<int> {\npublic:\n    \n}\n";
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "    \n", Position),
		        "locate interface implementation insertion site"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += 4;
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    Source, Position, "<complete-interface-compile>");
		std::string Generated;
		for (const FCompletionItem& Item : Result.Items)
		{
			if (Item.bOverrideMember && Item.Label == "get")
			{
				Generated = Item.InsertText;
				break;
			}
		}
		if (!Expect(Generated.find("int get() {") != std::string::npos && Generated.find("override") == std::string::npos,
		        "interface implementation specializes type without override"))
		{
			return EXIT_FAILURE;
		}
		const std::size_t Stub = Generated.find("${0:throw 0;}");
		if (!Expect(Stub != std::string::npos,
		        "generated interface implementation has editable body"))
		{
			return EXIT_FAILURE;
		}
		Generated.replace(Stub, 13, "throw 0;");
		std::string Compilable = Source;
		Compilable.insert(Compilable.find("    \n") + 4, Generated);
		const VeroScript::FCompileResult Compiled =
		    VeroScript::VeroScriptCompile::CompileSource(
		        Compilable, "<complete-generated-interface>");
		if (!Expect(Compiled.bSuccess,
		        "generated interface implementation compiles after applying its edit"))
		{
			for (const auto& Diagnostic : Compiled.Diagnostics)
			{
				std::cerr << Diagnostic.Message << '\n';
			}
			return EXIT_FAILURE;
		}
	}

	{
		const std::string Source =
		    "interface DefaultContract { virtual int value() { return 1; } "
		    "int fixed() { return 2; } }\n"
		    "class Child : public DefaultContract {\npublic:\n    val\n}\n";
		std::string DefaultBodySource = Source;
		DefaultBodySource.replace(DefaultBodySource.find("    val\n"), 7, "    ");
		const VeroScript::FCompileResult Valid =
		    VeroScript::VeroScriptCompile::CompileSource(
		        DefaultBodySource, "<complete-default-interface-valid>");
		if (!Expect(Valid.bSuccess, "default interface method compiles without implementation"))
		{
			return EXIT_FAILURE;
		}
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "    val", Position),
		        "locate default interface method prefix"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += 7;
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    Source, Position, "<complete-default-interface>");
		bool bOffered = false;
		for (const FCompletionItem& Item : Result.Items)
		{
			bOffered = bOffered || (Item.bOverrideMember && Item.Label == "value" && Item.InsertText.find(" override") != std::string::npos);
		}
		if (!Expect(Result.bSuccess && bOffered,
		        "virtual interface default method offers a genuine override"))
		{
			return EXIT_FAILURE;
		}
		std::string NonvirtualSource = Source;
		NonvirtualSource.replace(NonvirtualSource.find("    val\n"), 7, "    fix");
		FTextPosition NonvirtualPosition;
		if (!Expect(FindPosition(NonvirtualSource, "    fix\n", NonvirtualPosition),
		        "locate nonvirtual interface method prefix"))
		{
			return EXIT_FAILURE;
		}
		NonvirtualPosition.Column += 7;
		const FCompletionResult Nonvirtual = VeroScriptComplete::CompleteAtSource(
		    NonvirtualSource, NonvirtualPosition, "<complete-nonvirtual-interface>");
		bool bNonvirtualOffered = false;
		for (const FCompletionItem& Item : Nonvirtual.Items)
		{
			bNonvirtualOffered = bNonvirtualOffered || (Item.bOverrideMember && Item.Label == "fixed");
		}
		if (!Expect(Nonvirtual.bSuccess && !bNonvirtualOffered,
		        "nonvirtual interface default method has no override candidate"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::string Source =
		    "struct Counter { int value; }\n"
		    "void main() { field_info<Counter> field = "
		    "field_info<Counter>(type<Counter>().qualified_name, 0); field.ge; }\n";
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "field.ge;", Position),
		        "locate reflection method prefix"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += 8;
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    Source, Position, "<complete-reflection-type-argument>");
		const int Index = LabelIndex(Result.Items, "get");
		if (!Expect(Result.bSuccess && Index >= 0 && Result.Items[static_cast<std::size_t>(Index)].Detail.find("get<V>(") != std::string::npos && Result.Items[static_cast<std::size_t>(Index)].InsertText == "get<${1:ValueType}>(${0})" && Result.Items[static_cast<std::size_t>(Index)].bInsertTextSnippet,
		        "reflection completion inserts required explicit type argument"))
		{
			return EXIT_FAILURE;
		}
		std::string WithTypeArgument = Source;
		WithTypeArgument.replace(WithTypeArgument.find("field.ge;"), 9,
		    "field.ge<int>();");
		if (!Expect(FindPosition(WithTypeArgument, "field.ge<int>", Position),
		        "locate existing reflection type argument"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += 8;
		const FCompletionResult Existing = VeroScriptComplete::CompleteAtSource(
		    WithTypeArgument, Position, "<complete-reflection-existing-type-argument>");
		const int ExistingIndex = LabelIndex(Existing.Items, "get");
		if (!Expect(Existing.bSuccess && ExistingIndex >= 0 && Existing.Items[static_cast<std::size_t>(ExistingIndex)].InsertText == "get",
		        "reflection completion does not duplicate existing type arguments"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::string Source =
		    "void main() { std::concurrent::spa; }\n";
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "std::concurrent::spa;", Position),
		        "locate callable task prefix"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(
		    std::string_view("std::concurrent::spa").size());
		const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
		    Source, Position, "<complete-callable-task-overloads>");
		const int Spawn = LabelIndex(Result.Items, "spawn");
		const int Task = LabelIndex(Result.Items, "spawn_task");
		if (!Expect(Result.bSuccess && Spawn >= 0 && Task >= 0 && Result.Items[static_cast<std::size_t>(Spawn)].Detail.find("spawn(fn(args...)->void callable, args...)") != std::string::npos && Result.Items[static_cast<std::size_t>(Spawn)].InsertText == "spawn(${0})" && Result.Items[static_cast<std::size_t>(Task)].Detail.find("spawn_task<T>(fn(args...)->T callable, args...)") != std::string::npos && Result.Items[static_cast<std::size_t>(Task)].InsertText == "spawn_task<${1:ResultType}>(${0})",
		        "standard task completion exposes both core call forms"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::filesystem::path ScriptRoot =
		    std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
		const std::string StdlibPath =
		    (ScriptRoot / "VeroScript" / "Resources" / "Scripts" / "stdlib").string();
		FCompletionOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.StdlibSearchPaths.push_back(StdlibPath);
		const std::string FileLabel =
		    (ScriptRoot / "VeroScriptTooling" / "Private" / "Tests" / "stdlib-import-note.vs").string();
		for (const auto& [ImportLine, bNeedsImport] :
		    std::initializer_list<std::pair<const char*, bool>>{
		        {"import \"concurrent.vs\";\n", false}, {"", true}})
		{
			const std::string Source = std::string(ImportLine) + "void main() { std::concurrent::spa; }\n";
			FTextPosition Position;
			if (!Expect(FindPosition(Source, "std::concurrent::spa;", Position),
			        "locate imported standard task prefix"))
			{
				return EXIT_FAILURE;
			}
			Position.Column += static_cast<std::uint32_t>(
			    std::string_view("std::concurrent::spa").size());
			const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
			    Source, Position, FileLabel, Options);
			const int Index = LabelIndex(Result.Items, "spawn");
			if (!Expect(Result.bSuccess && Index >= 0 && (Result.Items[static_cast<std::size_t>(Index)].Detail.find("Requires import") != std::string::npos) == bNeedsImport,
			        "standard function import note matches the actual import graph"))
			{
				std::cerr << "imported=" << !bNeedsImport << " success=" << Result.bSuccess
				          << " index=" << Index << " stdlib=" << StdlibPath
				          << " exists=" << std::filesystem::exists(StdlibPath)
				          << " file=" << FileLabel << '\n';
				if (Index >= 0)
				{
					std::cerr << Result.Items[static_cast<std::size_t>(Index)].Detail << '\n';
				}
				for (const auto& Diagnostic : Result.Diagnostics)
				{
					std::cerr << Diagnostic.Message << '\n';
				}
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Markers are removed before querying; no pipe is sent as source text.
		for (const std::string Marked : {
		         "void main(){ for(int i=0;i|) }",
		         "void main(){ for(int i=0;i|",
		         "void main(){ for(int i=0;|",
		         "void main(){ for(int i=0;i<10;i|) }",
		         "void main(){ for(int i=0;i<10;i|",
		         "void main(){ for(int i=0;abs(i|)) }",
		         "void main(){ for(int i=0;abs(i|",
		         "void main(){ int before=1\nfor(int i=0;i|) {} }",
		         "void main(){ for(int i=0;i|) {} }\nvoid bad( {",
		         "void main(){\r\nfor(int i=0;\r\ni|) {}\r\n}",
		         "void main(){ for(int i=0,j=1;i|) {} }",
		         "void main(){ for(int outer=0;outer<2;++outer){ for(int i=0;i|) {} } }"})
		{
			std::string Source = Marked;
			const std::size_t Caret = Source.find('|');
			Source.erase(Caret, 1);
			FTextPosition Position{1, 1};
			for (std::size_t Index = 0; Index < Caret; ++Index)
			{
				if (Source[Index] == '\n')
				{
					++Position.Line;
					Position.Column = 1;
				}
				else
				{
					++Position.Column;
				}
			}
			const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
			    Source, Position, "<incomplete-for>");
			if (!Expect(Result.bSuccess && HasLabel(Result.Items, "i"),
			        "incomplete for must retain its initializer in condition and step"))
			{
				std::cerr << Marked << '\n';
				return EXIT_FAILURE;
			}
		}
		for (const std::string Marked : {
		         "void main(){ int value=0|; }",
		         "void main(){ int value=12|34; }",
		         "void main(){ int value=0xF|F; }",
		         "void main(){ int value=0b1|01; }",
		         "void main(){ int value=1_0|00; }",
		         "void main(){ int64 value=12i|64; }",
		         "void main(){ double value=1.|25; }",
		         "void main(){ float value=1.0f|; }",
		         "void main(){ double value=1e+|2; }",
		         "void main(){ int value=0x|",
		         "void main(){ int value=0b|",
		         "void main(){ double value=1e+|",
		         "void bad( {\nvoid main(){ int value=0|; }",
		         "void main(){ for(int i=0|;i) }"})
		{
			std::string Source = Marked;
			const std::size_t Caret = Source.find('|');
			Source.erase(Caret, 1);
			FTextPosition Position{1, 1};
			for (std::size_t Index = 0; Index < Caret; ++Index)
			{
				if (Source[Index] == '\n')
				{
					++Position.Line;
					Position.Column = 1;
				}
				else
				{
					++Position.Column;
				}
			}
			const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
			    Source, Position, "<numeric-completion>");
			if (!Expect(Result.bSuccess && Result.Items.empty(),
			        "numeric literals must not offer names, including on parse failure"))
			{
				std::cerr << Marked << '\n';
				return EXIT_FAILURE;
			}
		}
		for (const std::string Marked : {
		         "void main(){ for(int i=0;i<1;++i) {} i|; }",
		         "void first(){ for(int i=0;i<1;++i) {} } void second(){ i|; }"})
		{
			std::string Source = Marked;
			const std::size_t Caret = Source.find('|');
			Source.erase(Caret, 1);
			const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
			    Source, {1, static_cast<std::uint32_t>(Caret + 1)}, "<for-scope-end>");
			if (!Expect(Result.bSuccess && !HasLabel(Result.Items, "i"),
			        "for initializer must not escape its loop or callable"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		struct FScopeCase
		{
			const char* MarkedSource;
			const char* Required;
			const char* Forbidden = nullptr;
			const char* Detail = nullptr;
		};
		for (const FScopeCase& Case : {
		         FScopeCase{"void main(){ int local=1; if(loc|", "local"},
		         FScopeCase{"void main(){ int local=1; print(lo|cal", "local"},
		         FScopeCase{"void main(){ int local=1; array<int> values={}; values[loc|", "local"},
		         FScopeCase{"void main(){ int local=1; for(int i=loc|", "local"},
		         FScopeCase{"void main(){ int local=1; for(int i=true?0:loc|", "local"},
		         FScopeCase{"void main(){ for(int i=0;i<2;++i){i|", "i"},
		         FScopeCase{"void main(){ int local=1; if(local<loc|){} }", "local"},
		         FScopeCase{"int local=1; void main(){ if(local<loc|", "local"},
		         FScopeCase{"namespace N{ int value=1; } void main(){ int local=1; if(N::value<loc|", "local"},
		         FScopeCase{"class C{ int value=1; void test(int local){ if(this.value<loc|", "local"},
		         FScopeCase{"void main(){ int local=1; if(local<2>loc|", "local"},
		         FScopeCase{"void main(){ int local=1; for(true?0:loc|", "local"},
		         FScopeCase{"void main(){ for(int i=0;[](int param)->int=>par|", "param"},
		         FScopeCase{"void main(){ for(int i=0;[](int param)->int=>par|){} }", "param"},
		         FScopeCase{"void main(){ for(int i=0;true?([](int param)->int=>par|):0){} }", "param"},
		         FScopeCase{"void main(){ for(int i=0;i<1;[](int param)->int=>par|", "param"},
		         FScopeCase{"void main(){ for(int i=0;[](int param)->int=>param;par|){} }", nullptr, "param"},
		         FScopeCase{"void main(){ for(int i=0;[](array<int> items)->int=>items.pu|", "push"},
		         FScopeCase{"void main(){ int local=1; array<int> arr={loc|", "local"},
		         FScopeCase{"void main(){ int local=1; array<int> arr={0,loc|", "local"},
		         FScopeCase{"void main(){ int local=1; array<array<int>> arr={{loc|", "local"},
		         FScopeCase{"void main(){ int local=1; for(array<int> arr={loc|", "local"},
		         FScopeCase{"void main(){ auto f=[](array<int> items)->int{return items.pu|;}; }", "push"},
		         FScopeCase{"void main(){ auto f=[](array<int> items)->int{return items.pu|sh();}; }", "push"},
		         FScopeCase{"void main(){ auto f=[](array<int> items)->int{return items.pu|", "push"},
		         FScopeCase{"void main(){ {int local=1;} loc|", nullptr, "local"},
		         FScopeCase{"void main(){ loc|; int local=1; }", nullptr, "local"},
		         FScopeCase{"void main(){ auto f=[](int param)->int{return par|", "param"},
		         FScopeCase{"void main(){ auto f=[](int param)->int{return par|;}; }", "param"},
		         FScopeCase{"void main(){ auto f=[](int param)->int{return param;}; par|", nullptr, "param"},
		         FScopeCase{"void main(){ array<int> values={}; for(int value:val|", "values", "value"},
		         FScopeCase{"void main(){ array<int> value={}; for(int value:val|ue){} }", "value", nullptr, "array<int>"}})
		{
			std::string Source = Case.MarkedSource;
			const std::size_t Caret = Source.find('|');
			Source.erase(Caret, 1);
			const FCompletionResult Result = VeroScriptComplete::CompleteAtSource(
			    Source, {1, static_cast<std::uint32_t>(Caret + 1)}, "<related-scope>");
			if (!Expect(Result.bSuccess && (!Case.Required || HasLabel(Result.Items, Case.Required)) && (!Case.Forbidden || !HasLabel(Result.Items, Case.Forbidden)) && (!Case.Detail || HasDetail(Result.Items, Case.Required, Case.Detail)),
			        "recovery retains visible values and excludes unavailable bindings"))
			{
				std::cerr << Case.MarkedSource << '\n';
				std::cerr << "success: " << Result.bSuccess << " prefix: " << Result.Prefix << '\n';
				for (const auto& Item : Result.Items)
				{
					std::cerr << Item.Label << ' ';
				}
				std::cerr << '\n';
				return EXIT_FAILURE;
			}
		}
	}

	for (const auto& Case : std::vector<std::pair<std::string, const char*>>{
	         {"void main(){std::memory::|}", "buffer"}, {"void main(){std::memory::|}", "read"},
	         {"void main(){auto b=new std::memory::buffer(8);b.|}", "size"},
	         {"void main(){auto b=new std::memory::buffer(8);b.|}", "readonly_slice"}})
	{
		auto Source = Case.first;
		const auto Caret = Source.find('|');
		Source.erase(Caret, 1);
		const auto Result = VeroScriptComplete::CompleteAtSource(
		    Source, {1, static_cast<std::uint32_t>(Caret + 1)}, "memory-completion.vs");
		if (!Expect(Result.bSuccess && HasLabel(Result.Items, Case.second),
		        "memory standard metadata completion"))
		{
			std::cerr << Case.first << " expected " << Case.second << " success " << Result.bSuccess << '\n';
			for (const auto& Item : Result.Items)
			{
				std::cerr << Item.Label << '\n';
			}
			return EXIT_FAILURE;
		}
	}
	{
		FCompletionOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.SourceOverlays = {
		    {"macro-complete/library.vs", "#define_export PUBLIC_VALUE 42\n"}};
		for (const auto& Case :
		    {std::pair<std::string, bool>{"PUB|; import \"library.vs\";", false},
		        std::pair<std::string, bool>{"import \"library.vs\"; int main(){PUB|;return 0;}",
		            true},
		        std::pair<std::string, bool>{"import \"library.vs\";\n#ifdef PUB|\n", true}})
		{
			auto Source = Case.first;
			const auto Offset = Source.find('|');
			Source.erase(Offset, 1);
			FTextPosition Position{1, 1};
			for (std::size_t I = 0; I < Offset; ++I)
			{
				if (Source[I] == '\n')
				{
					++Position.Line;
					Position.Column = 1;
				}
				else
				{
					++Position.Column;
				}
			}
			const auto Result = VeroScriptComplete::CompleteAtSource(
			    Source, Position, "macro-complete/main.vs", Options);
			if (!Expect(
			        Result.bSuccess && HasLabel(Result.Items, "PUBLIC_VALUE") == Case.second,
			        "imported macro completion observes same-line activation and partial buffers"))
			{
				for (const auto& Item : Result.Items)
				{
					std::cerr << Item.Label << '\n';
				}

				return EXIT_FAILURE;
			}
		}
		Options.ImplicitImportPaths = {"macro-complete/library.vs"};
		const auto Implicit = VeroScriptComplete::CompleteAtSource(
		    "PUB", {1, 4}, "macro-complete/main.vs", Options);
		if (!Expect(Implicit.bSuccess && HasLabel(Implicit.Items, "PUBLIC_VALUE"),
		        "implicit public macro completion is available at the start of an incomplete buffer"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const auto Root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
		const std::string Stdlib = (Root / "VeroScript/Resources/Scripts/stdlib").string();
		FCompletionOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.StdlibSearchPaths = {Stdlib};
		const std::string Source = "import \"attribute.vs\";class Model{public:int read(){return 3;}};void main(){Model m=Model();auto a=std::make_attribute_by_name<int>(m,\"read\");a.;}";
		FTextPosition Position;
		FindPosition(Source, "a.;", Position);
		Position.Column += 2;
		const auto Completion = VeroScriptComplete::CompleteAtSource(Source, Position, "attribute-complete.vs", Options);
		if (!Expect(HasLabel(Completion.Items, "get") && HasLabel(Completion.Items, "try_get") &&
		            HasLabel(Completion.Items, "bind") && HasLabel(Completion.Items, "reset"),
		        "attribute factory result exposes standard members to completion"))
		{
			return EXIT_FAILURE;
		}

		const std::string NamespaceSource = "import \"attribute.vs\";void main(){std::make_attr;}";
		FindPosition(NamespaceSource, "make_attr;", Position);
		Position.Column += 9;
		const auto NamespaceResult = VeroScriptComplete::CompleteAtSource(
		    NamespaceSource, Position, "attribute-namespace.vs", Options);
		if (!Expect(HasLabel(NamespaceResult.Items, "make_attribute_by_name"),
		        "imported attribute factory is discoverable through the std namespace"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::vector<std::pair<std::string, std::string>> Cases = {
		    {"class A{public:struct Item{int value=7;};private:struct Hidden{};};void main(){A::;}", "A::"},
		    {"void main(){struct Local{int value=7;};Local item=Local();item.;}", "item."},
		    {"class A<T>{public:struct B<U>{T key;U value;};};void main(){A<int>::B<string> item=A<int>::B<string>();item.;}", "item."}};
		for (std::size_t Index = 0; Index < Cases.size(); ++Index)
		{
			FTextPosition Position;
			FindPosition(Cases[Index].first, Cases[Index].second + ";", Position);
			Position.Column += static_cast<std::uint32_t>(Cases[Index].second.size());
			const auto Result = VeroScriptComplete::CompleteAtSource(Cases[Index].first, Position, "scoped-completion.vs");
			if (!Expect(HasLabel(Result.Items, Index == 0 ? "Item" : "value"),
			        "nested and local type members are available to completion") ||
			    !Expect(Index != 0 || !HasLabel(Result.Items, "Hidden"),
			        "private nested names are filtered from external completion"))
			{
				return EXIT_FAILURE;
			}
		}
		const std::string Source = "void work(){struct Local{int value;};}void main(){Loc;}";
		FTextPosition Position;
		FindPosition(Source, "Loc;", Position);
		Position.Column += 3;
		const auto Result = VeroScriptComplete::CompleteAtSource(Source, Position, "local-visibility.vs");
		if (!Expect(!HasLabel(Result.Items, "Local"), "local types do not leak into global completion"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Catalogs carry concrete generic host identities; member signatures have
		// already been specialized by the host and must survive type-path lookup.
		VeroScript::FHostBindingRegistry Registry;
		VeroScript::FGenericHostTypeTemplateId TemplateId;
		std::string ErrorId;
		if (!Expect(Registry.RegisterGenericHostTypeTemplateMetadata("HostBox", 1, TemplateId, ErrorId),
		        "register a generic host template for static receiver completion"))
		{
			return EXIT_FAILURE;
		}
		for (const auto& [Argument, NativeType] : std::vector<std::pair<std::string, VeroScript::ENativeType>>{
		         {"int", VeroScript::ENativeType::Int}, {"string", VeroScript::ENativeType::String}})
		{
			const std::string TypeName = "HostBox<" + Argument + ">";
			VeroScript::FHostTypeId TypeId;
			if (!Expect(Registry.RegisterType(TypeName, VeroScript::EHostTypeKind::Class, TypeId, ErrorId) && Registry.SetTypeGenericInstanceInfo(TypeId, TemplateId, {Argument}, ErrorId),
			        "register a concrete host generic identity"))
			{
				return EXIT_FAILURE;
			}
			VeroScript::FHostMemberDesc Answer;
			Answer.Kind = VeroScript::EHostMemberKind::StaticConstant;
			Answer.Name = "answer";
			Answer.ReturnType = NativeType;
			Answer.Callback = [](const VeroScript::FNativeCallContext&, VeroScript::FValue&, std::string& Error)
			{
				Error = "completion metadata control must not execute";
				return false;
			};
			VeroScript::FHostMemberDesc Make;
			Make.Kind = VeroScript::EHostMemberKind::StaticMethod;
			Make.Name = "make";
			Make.ReturnType = VeroScript::ENativeType::HostObject;
			Make.ParamTypes = {NativeType};
			Make.ParamNames = {"value"};
			Make.Annotations = {{"Factory", {}}};
			VeroScript::FHostMemberDesc Singleton;
			Singleton.Kind = VeroScript::EHostMemberKind::StaticConstant;
			Singleton.Name = "singleton";
			Singleton.ReturnType = VeroScript::ENativeType::HostObject;
			Singleton.ReturnHostTypeName = TypeName;
			VeroScript::FHostMemberDesc Value;
			Value.Kind = VeroScript::EHostMemberKind::PropertyGetter;
			Value.Name = "value";
			Value.ReturnType = NativeType;
			VeroScript::FHostMemberDesc Read = Value;
			Read.Kind = VeroScript::EHostMemberKind::Method;
			Read.Name = "read";
			if (!Expect(Registry.RegisterMember(TypeId, Answer, ErrorId) && Registry.RegisterMember(TypeId, Make, ErrorId) && Registry.RegisterMember(TypeId, Singleton, ErrorId) && Registry.RegisterMember(TypeId, Value, ErrorId) && Registry.RegisterMember(TypeId, Read, ErrorId),
			        "register concrete host generic static and instance metadata"))
			{
				return EXIT_FAILURE;
			}
		}
		std::string Catalog;
		VeroScript::FHostBindingRegistry Imported;
		if (!Expect(VeroScript::ExportBindingCatalogJson(Registry, Catalog) && VeroScript::ImportBindingCatalogJson(Catalog, Imported, ErrorId),
		        "generic host static metadata survives the editor catalog boundary"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::IHostBindingCatalog* Bindings : {&Registry, &Imported})
		{
			FCompletionOptions Options;
			Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
			Options.Bindings = Bindings;
			if (Bindings == &Registry)
			{
				// The editor catalog intentionally strips live callbacks. Validate the
				// source spelling against the live registry, which also has native entries.
				VeroScript::FCompileOptions CompileOptions;
				CompileOptions.Bindings = Bindings;
				CompileOptions.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
				const auto Control = VeroScript::VeroScriptCompile::CompileSource(
				    "void main(){HostBox<int>::answer;}", "host-generic-static-control.vs", CompileOptions);
				if (!Expect(Control.bSuccess, "generic host static-member fixture is accepted by Core"))
				{
					std::cerr << "host static control: " << Control.Error.MessageId << " : " << Control.Error.Message << '\n';
					return EXIT_FAILURE;
				}
			}
			for (const std::string Argument : {"int", "string"})
			{
				const std::string TypeName = "HostBox<" + Argument + ">";
				const std::string Receiver = TypeName + "::";
				const std::string Source = "void use(){" + Receiver + ";}";
				FTextPosition Position;
				if (!Expect(FindPosition(Source, Receiver, Position), "locate generic host static scope")) return EXIT_FAILURE;
				Position.Column += static_cast<std::uint32_t>(Receiver.size());
				const auto Completion = VeroScriptComplete::CompleteAtSource(Source, Position, "host-generic-static.vs", Options);
				const int MakeIndex = LabelIndex(Completion.Items, "make");
				const int AnswerIndex = LabelIndex(Completion.Items, "answer");
				if (!Expect(Completion.bSuccess && Completion.Items.size() == 3 && MakeIndex >= 0 && AnswerIndex >= 0 && Completion.Items[static_cast<std::size_t>(MakeIndex)].Detail == "Factory static " + TypeName + " (" + Argument + " value)" && HasLabel(Completion.Items, "singleton") && Completion.Items[static_cast<std::size_t>(AnswerIndex)].Detail == Argument && Completion.Items[static_cast<std::size_t>(AnswerIndex)].InsertText == "answer" && !HasLabel(Completion.Items, "value") && !HasLabel(Completion.Items, "read"),
				        "generic host type scopes expose only statics with concrete member signatures"))
				{
					std::cerr << Source << " success=" << Completion.bSuccess << " prefix=" << Completion.Prefix << '\n';
					for (const auto& Diagnostic : Completion.Diagnostics)
					{
						std::cerr << Diagnostic.Message << '\n';
					}
					for (const auto& Item : Completion.Items)
					{
						std::cerr << Item.Label << " : " << Item.Detail << '\n';
					}
					const std::string CompleteSource = "void use(){" + Receiver + "answer;}";
					FTextPosition CompletePosition;
					FindPosition(CompleteSource, Receiver, CompletePosition);
					CompletePosition.Column += static_cast<std::uint32_t>(Receiver.size() + 2);
					const auto CompleteToken = VeroScriptComplete::CompleteAtSource(
					    CompleteSource, CompletePosition, "host-generic-static-token.vs", Options);
					std::cerr << "complete token: " << CompleteSource << " success=" << CompleteToken.bSuccess
					          << " prefix=" << CompleteToken.Prefix << '\n';
					for (const auto& Diagnostic : CompleteToken.Diagnostics)
					{
						std::cerr << Diagnostic.Message << '\n';
					}
					for (const auto& Item : CompleteToken.Items)
					{
						std::cerr << Item.Label << " : " << Item.Detail << '\n';
					}
					return EXIT_FAILURE;
				}
				const std::string InstanceSource = "void use(" + TypeName + " box){box.;}";
				if (!Expect(FindPosition(InstanceSource, "box.;", Position), "locate generic host instance scope")) return EXIT_FAILURE;
				Position.Column += 4;
				const auto Instance = VeroScriptComplete::CompleteAtSource(InstanceSource, Position, "host-generic-instance.vs", Options);
				if (!Expect(Instance.bSuccess && HasDetail(Instance.Items, "value", Argument) && HasDetail(Instance.Items, "read", Argument + " ()") && HasLabel(Instance.Items, "make") && !HasLabel(Instance.Items, "answer") && !HasLabel(Instance.Items, "singleton"),
				        "generic host values retain instance members and permitted static methods"))
				{
					return EXIT_FAILURE;
				}
				const std::string ValueLiteral = Argument == "string" ? "\"value\"" : "1";
				for (const auto& [Expression, bExpected] : std::vector<std::pair<std::string, bool>>{
				         {TypeName + "::make(" + ValueLiteral + ").va", true},
				         {TypeName + "::singleton.va", true},
				         {"box.make(" + ValueLiteral + ").va", true},
				         {TypeName + "::value.va", false},
				         {"box.singleton.va", false}})
				{
					const std::string ChainSource = "void use(" + TypeName + " box){" + Expression + ";}";
					if (!Expect(FindPosition(ChainSource, Expression, Position), "locate generic host member chain")) return EXIT_FAILURE;
					Position.Column += static_cast<std::uint32_t>(Expression.size());
					const auto Chain = VeroScriptComplete::CompleteAtSource(ChainSource, Position, "host-generic-chain.vs", Options);
					if (!Expect(Chain.bSuccess && (bExpected ? HasDetail(Chain.Items, "value", Argument) : Chain.Items.empty()),
					        "host generic chains require no constructor and retain static/instance rules despite annotations"))
					{
						std::cerr << ChainSource << '\n';
						return EXIT_FAILURE;
					}
				}
			}
		}
	}
	return EXIT_SUCCESS;
}
