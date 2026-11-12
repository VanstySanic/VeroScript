// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTooling.h"
#include "VeroScriptCompile.h"

#include <algorithm>

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
	int Fail(const char* Message)
	{
		std::cerr << "VeroScriptRenameTests: " << Message << '\n';
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
}

int main()
{
	using namespace VeroScript::Tooling;

	for (const char* Name : {"operatorCount", "operator_count", "operator2", "count"})
	{
		const std::string Source = "void " + std::string(Name)
			+ "(){}class Other{public:void " + Name
			+ "(){}};void main(){" + Name + "();Other o=Other();o." + Name + "();}";
		FTextPosition Position;
		if (!Expect(FindPosition(Source, Name, Position), "locate ordinary operator-prefixed name"))
		{
			return EXIT_FAILURE;
		}
		const auto Result = VeroScriptRename::PrepareAtSource(Source, Position, "Assist", "<rename-prefix>");
		if (!Expect(Result.bSuccess && Result.bCanRename && Result.Edits.size() == 2,
			"ordinary operator-prefixed names rename only their own declaration and use"))
		{
			std::cerr << Result.Reason << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* OverloadSource =
			"void foo(){}void foo(int value){}void main(){foo();foo(1);}";
		FTextPosition Position;
		if (!Expect(FindPosition(OverloadSource, "foo", Position), "locate overload declaration"))
		{
			return EXIT_FAILURE;
		}
		const auto Result = VeroScriptRename::PrepareAtSource(
			OverloadSource, Position, "bar", "<rename-overload>");
		if (!Expect(Result.bSuccess && !Result.bCanRename && Result.Edits.empty(),
			"overload declaration must not produce a partial rename"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		constexpr const char* CtorSource = "class Foo{public:Foo(){}};void main(){Foo f=Foo();}";
		FTextPosition Position;
		if (!Expect(FindPosition(CtorSource, "Foo()", Position), "locate constructor declaration"))
		{
			return EXIT_FAILURE;
		}
		const auto Constructor = VeroScriptRename::PrepareAtSource(
			CtorSource, Position, "Bar", "<rename-constructor>");
		if (!Expect(Constructor.bSuccess && !Constructor.bCanRename && Constructor.Edits.empty()
			&& Constructor.Reason.find("type") != std::string::npos,
			"constructor name cannot be renamed independently of its type"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(FindPosition(CtorSource, "Foo{", Position), "locate same-line type declaration"))
		{
			return EXIT_FAILURE;
		}
		const auto Type = VeroScriptRename::PrepareAtSource(CtorSource, Position, "Bar", "<rename-constructor>");
		if (!Expect(Type.bSuccess && Type.bCanRename && Type.Edits.size() == 4,
			"same-line type rename includes its constructor and uses"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* Source = R"(
void Helper() {}

void main() {
	Helper();
}
)";

	{
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "Helper();", Position), "locate Helper call"))
		{
			return EXIT_FAILURE;
		}
		const FRenameResult Result = VeroScriptRename::PrepareAtSource(
			Source,
			Position,
			"Assist",
			"<rename>");
		if (!Expect(Result.bSuccess && Result.bCanRename, "Helper rename should be allowed"))
		{
			std::cerr << Result.Reason << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Result.OldName == "Helper", "old name Helper"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Result.Edits.size() == 2, "decl + call edits"))
		{
			std::cerr << "edits=" << Result.Edits.size() << '\n';
			return EXIT_FAILURE;
		}
		for (const FTextEdit& Edit : Result.Edits)
		{
			if (!Expect(Edit.NewText == "Assist", "new text Assist"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		constexpr const char* Homonym = R"(
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
		if (!Expect(FindPosition(Homonym, "p.x;", Position), "locate p.x"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("p.").size());
		const FRenameResult Result = VeroScriptRename::PrepareAtSource(
			Homonym,
			Position,
			"width",
			"<rename>");
		if (!Expect(Result.bSuccess && Result.bCanRename, "field rename should filter local x"))
		{
			std::cerr << Result.Reason << '\n';
			return EXIT_FAILURE;
		}
		// Field decl + p.x use only (not the local `int x`).
		if (!Expect(Result.Edits.size() == 2, "field rename edits should be decl + member use"))
		{
			std::cerr << "edits=" << Result.Edits.size() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* Locals = R"(
void First() {
	int total = 1;
	print(total);
}

void Second() {
	int total = 2;
	print(total);
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(Locals, "print(total);", Position), "locate First total use"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("print(").size());
		const FRenameResult Result = VeroScriptRename::PrepareAtSource(
			Locals,
			Position,
			"sum",
			"<rename>");
		if (!Expect(Result.bSuccess && Result.bCanRename, "local rename should be allowed"))
		{
			std::cerr << Result.Reason << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Result.Edits.size() == 2, "local rename should touch only First's total"))
		{
			std::cerr << "edits=" << Result.Edits.size() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* Ambiguous = R"(
struct Point {
	int x;
}

int x = 1;

void main() {
	Point p = Point();
	int n = p.x;
	print(x);
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(Ambiguous, "print(x);", Position), "locate bare x use"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("print(").size());
		const FRenameResult Result = VeroScriptRename::PrepareAtSource(
			Ambiguous,
			Position,
			"g",
			"<rename>");
		if (!Expect(Result.bSuccess && !Result.bCanRename, "bare x with field+global should be ambiguous"))
		{
			std::cerr << "reason=[" << Result.Reason << "] edits=" << Result.Edits.size() << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
				Result.Reason.find("ambiguous") != std::string::npos,
				"reason should mention ambiguous"))
		{
			std::cerr << Result.Reason << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		const FRenameResult Bad = VeroScriptRename::PrepareAtSource(
			Source,
			FTextPosition{1, 1},
			"for",
			"<rename>");
		if (!Expect(Bad.bSuccess && !Bad.bCanRename, "keyword new name refused"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const FRenameResult Bad = VeroScriptRename::PrepareAtSource(
			Source,
			FTextPosition{1, 1},
			"1bad",
			"<rename>");
		if (!Expect(Bad.bSuccess && !Bad.bCanRename, "invalid new name refused"))
		{
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
		if (!Expect(FindPosition(HostSource, "print(", Position), "locate print"))
		{
			return EXIT_FAILURE;
		}
		const FRenameResult Result = VeroScriptRename::PrepareAtSource(
			HostSource,
			Position,
			"log",
			"<rename-host>");
		if (!Expect(Result.bSuccess && !Result.bCanRename, "host print rename refused"))
		{
			std::cerr << Result.Reason << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
				Result.Reason.find("soft") != std::string::npos
					|| Result.Reason.find("builtin") != std::string::npos,
				"refuse reason mentions soft/builtin"))
		{
			std::cerr << Result.Reason << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* BuiltinTypeSource = R"(
void main() {
	priority_queue<int> values();
}
)";
		FTextPosition Position;
		if (!Expect(
				FindPosition(BuiltinTypeSource, "priority_queue", Position),
				"locate priority_queue builtin type"))
		{
			return EXIT_FAILURE;
		}
		const FRenameResult Result = VeroScriptRename::CanRenameAtSource(
			BuiltinTypeSource,
			Position,
			"<rename-builtin-type>");
		if (!Expect(
				Result.bSuccess && !Result.bCanRename
					&& Result.Reason.find("builtin") != std::string::npos,
				"builtin type rename refused"))
		{
			std::cerr << Result.Reason << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* MacroSource = R"(
#define ANSWER 42
void main() {
	print(ANSWER);
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(MacroSource, "ANSWER", Position), "locate ANSWER macro"))
		{
			return EXIT_FAILURE;
		}
		const FRenameResult Result = VeroScriptRename::PrepareAtSource(
			MacroSource,
			Position,
			"VALUE",
			"<rename-macro>");
		if (!Expect(Result.bSuccess && !Result.bCanRename, "macro rename refused"))
		{
			std::cerr << Result.Reason << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
				Result.Reason.find("macro") != std::string::npos,
				"refuse reason mentions macro"))
		{
			std::cerr << Result.Reason << '\n';
			return EXIT_FAILURE;
		}

		constexpr const char* TypeMacroSource = R"(
#define FString string
void Send(FString value) {}
)";
		if (!Expect(
				FindPosition(TypeMacroSource, "FString", Position),
				"locate type-shaped macro"))
		{
			return EXIT_FAILURE;
		}
		const FRenameResult TypeMacroResult = VeroScriptRename::PrepareAtSource(
			TypeMacroSource,
			Position,
			"FText",
			"<rename-type-macro>");
		if (!Expect(
				TypeMacroResult.bSuccess && !TypeMacroResult.bCanRename,
				"type-shaped macro rename refused"))
		{
			std::cerr << TypeMacroResult.Reason << '\n';
			return EXIT_FAILURE;
		}
	}

	// Type rename must rewrite destructor name token (`~Owner` → `~Holder`).
	{
		constexpr const char* DtorSource = R"(
class Owner {
public:
	~Owner() {}
}

void main() {
	Owner o = Owner();
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(DtorSource, "class Owner", Position), "locate class Owner"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("class ").size());
		const FRenameResult Result = VeroScriptRename::PrepareAtSource(
			DtorSource,
			Position,
			"Holder",
			"<rename-dtor-type>");
		if (!Expect(Result.bSuccess && Result.bCanRename, "type rename with destructor allowed"))
		{
			std::cerr << Result.Reason << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Result.OldName == "Owner", "old type name Owner"))
		{
			return EXIT_FAILURE;
		}
		bool bRewroteDestructor = false;
		for (const FTextEdit& Edit : Result.Edits)
		{
			if (Edit.NewText != "Holder")
			{
				continue;
			}
			// Destructor name token starts after '~' on the destructor line.
			if (Edit.Range.StartLine == 4 && Edit.Range.StartColumn > 1)
			{
				bRewroteDestructor = true;
				break;
			}
		}
		if (!Expect(bRewroteDestructor, "type rename rewrites ~Owner name token"))
		{
			std::cerr << "edits=" << Result.Edits.size() << '\n';
			for (const FTextEdit& Edit : Result.Edits)
			{
				std::cerr << "  L" << Edit.Range.StartLine << ":" << Edit.Range.StartColumn
					<< "-" << Edit.Range.EndColumn << " -> " << Edit.NewText << '\n';
			}
			return EXIT_FAILURE;
		}

		FTextPosition DtorPos;
		if (!Expect(FindPosition(DtorSource, "~Owner()", DtorPos), "locate ~Owner rename caret"))
		{
			return EXIT_FAILURE;
		}
		const FRenameResult DtorRename = VeroScriptRename::PrepareAtSource(
			DtorSource,
			DtorPos,
			"Holder",
			"<rename-dtor-token>");
		if (!Expect(
				DtorRename.bSuccess && !DtorRename.bCanRename
					&& DtorRename.Reason.find("type") != std::string::npos,
				"direct destructor token rename refused"))
		{
			std::cerr << DtorRename.Reason << '\n';
			return EXIT_FAILURE;
		}
	}

	// Type rename with explicit constructors + destructor must rewrite all name tokens.
	{
		constexpr const char* CtorSource = R"(
struct Point {
	int x;
	Point(int v) { this.x = v; }
	~Point() {}
}

void main() {
	Point p = Point(1);
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(CtorSource, "struct Point", Position), "locate struct Point"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("struct ").size());
		const FRenameResult Result = VeroScriptRename::PrepareAtSource(
			CtorSource,
			Position,
			"Vec2",
			"<rename-ctor-type>");
		if (!Expect(Result.bSuccess && Result.bCanRename, "type with ctor rename allowed"))
		{
			std::cerr << Result.Reason << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Result.OldName == "Point", "old type name Point"))
		{
			return EXIT_FAILURE;
		}
		bool bRewroteType = false;
		bool bRewroteCtorDecl = false;
		bool bRewroteDtor = false;
		bool bRewroteUse = false;
		for (const FTextEdit& Edit : Result.Edits)
		{
			if (Edit.NewText != "Vec2")
			{
				continue;
			}
			if (Edit.Range.StartLine == 2)
			{
				bRewroteType = true;
			}
			else if (Edit.Range.StartLine == 4)
			{
				bRewroteCtorDecl = true;
			}
			else if (Edit.Range.StartLine == 5 && Edit.Range.StartColumn > 1)
			{
				bRewroteDtor = true;
			}
			else if (Edit.Range.StartLine == 9)
			{
				bRewroteUse = true;
			}
		}
		if (!Expect(
				bRewroteType && bRewroteCtorDecl && bRewroteDtor && bRewroteUse,
				"type rename rewrites type/ctor/dtor/use tokens"))
		{
			std::cerr << "edits=" << Result.Edits.size()
				<< " type=" << bRewroteType << " ctor=" << bRewroteCtorDecl
				<< " dtor=" << bRewroteDtor << " use=" << bRewroteUse << '\n';
			for (const FTextEdit& Edit : Result.Edits)
			{
				std::cerr << "  L" << Edit.Range.StartLine << ":" << Edit.Range.StartColumn
					<< "-" << Edit.Range.EndColumn << " -> " << Edit.NewText << '\n';
			}
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
		FTextPosition Position;
		if (!Expect(FindPosition(EnumClassSource, "Green }", Position), "locate enum class rename"))
		{
			return EXIT_FAILURE;
		}
		const FRenameResult Result = VeroScriptRename::PrepareAtSource(
			EnumClassSource,
			Position,
			"Emerald",
			"<rename-enum-class>");
		if (!Expect(
				Result.bSuccess
					&& Result.bCanRename
					&& Result.OldName == "Green"
					&& Result.Edits.size() == 3,
				"enum class rename rewrites declaration and . / :: uses"))
		{
			std::cerr << "canRename=" << Result.bCanRename
				<< " edits=" << Result.Edits.size()
				<< " reason=[" << Result.Reason << "]\n";
			return EXIT_FAILURE;
		}

		FTextPosition DeclEndPos = Position;
		DeclEndPos.Column += static_cast<std::uint32_t>(std::string_view("Green").size());
		const FRenameResult DeclEndResult = VeroScriptRename::PrepareAtSource(
			EnumClassSource,
			DeclEndPos,
			"Emerald",
			"<rename-enum-class-decl-end>");
		if (!Expect(
				DeclEndResult.bSuccess
					&& DeclEndResult.bCanRename
					&& DeclEndResult.Edits.size() == 3,
				"caret after enum class member name can rename"))
		{
			std::cerr << "decl-end canRename=" << DeclEndResult.bCanRename
				<< " edits=" << DeclEndResult.Edits.size()
				<< " reason=[" << DeclEndResult.Reason << "]\n";
			return EXIT_FAILURE;
		}

		FTextPosition ScopePos;
		if (!Expect(
				FindPosition(EnumClassSource, "Color::Green", ScopePos),
				"locate Color::Green rename"))
		{
			return EXIT_FAILURE;
		}
		ScopePos.Column += static_cast<std::uint32_t>(std::string_view("Color::").size());
		const FRenameResult ScopeResult = VeroScriptRename::PrepareAtSource(
			EnumClassSource,
			ScopePos,
			"Emerald",
			"<rename-enum-class-scope>");
		if (!Expect(
				ScopeResult.bSuccess
					&& ScopeResult.bCanRename
					&& ScopeResult.Edits.size() == 3,
				":: qualified enum class member can rename"))
		{
			std::cerr << "scope canRename=" << ScopeResult.bCanRename
				<< " edits=" << ScopeResult.Edits.size()
				<< " reason=[" << ScopeResult.Reason << "]\n";
			return EXIT_FAILURE;
		}

		FTextPosition BarePos;
		if (!Expect(FindPosition(EnumClassSource, "\tGreen;", BarePos), "locate bare Green rename"))
		{
			return EXIT_FAILURE;
		}
		BarePos.Column += 1;
		const FRenameResult BareResult = VeroScriptRename::PrepareAtSource(
			EnumClassSource,
			BarePos,
			"Emerald",
			"<rename-enum-class-bare>");
		if (!Expect(
				BareResult.bSuccess && !BareResult.bCanRename,
				"bare enum class member cannot be renamed"))
		{
			std::cerr << "bare canRename=" << BareResult.bCanRename
				<< " reason=[" << BareResult.Reason << "]\n";
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
		FTextPosition UserNamespacePosition;
		if (!Expect(
				FindPosition(NamespaceSource, "A::B::Run", UserNamespacePosition),
				"locate user namespace rename"))
		{
			return EXIT_FAILURE;
		}
		UserNamespacePosition.Column += static_cast<std::uint32_t>(std::string_view("A::").size());
		const FRenameResult UserNamespaceRename = VeroScriptRename::PrepareAtSource(
			NamespaceSource,
			UserNamespacePosition,
			"C",
			"<rename-user-namespace>");
		if (!Expect(
				UserNamespaceRename.bSuccess
					&& UserNamespaceRename.bCanRename
					&& UserNamespaceRename.OldName == "B"
					&& UserNamespaceRename.Edits.size() >= 2,
				"user namespace rename rewrites the declaration and qualified use"))
		{
			std::cerr << "namespace canRename=" << UserNamespaceRename.bCanRename
				<< " edits=" << UserNamespaceRename.Edits.size()
				<< " reason=[" << UserNamespaceRename.Reason << "]\n";
			return EXIT_FAILURE;
		}

		constexpr const char* StandardNamespaceSource = R"(
void main() { std::chrono::now(); }
)";
		FTextPosition StandardNamespacePosition;
		if (!Expect(
				FindPosition(StandardNamespaceSource, "std::chrono", StandardNamespacePosition),
				"locate standard namespace rename"))
		{
			return EXIT_FAILURE;
		}
		const FRenameResult StandardNamespaceRename = VeroScriptRename::PrepareAtSource(
			StandardNamespaceSource,
			StandardNamespacePosition,
			"system",
			"<rename-standard-namespace>");
		if (!Expect(
				StandardNamespaceRename.bSuccess
					&& !StandardNamespaceRename.bCanRename
					&& StandardNamespaceRename.Reason.find("standard-library namespaces")
						!= std::string::npos,
				"standard namespaces are explicitly read-only for rename"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::string Source =
			"class A{public:struct Item{};};class B{public:struct Item{};};void main(){A::Item "
			"a=A::Item();B::Item b=B::Item();struct Local{};Local x=Local();{struct Local{};Local y=Local();}}";
		for (const auto& Needle : {"Item{};};class B", "Item{};};void main", "Local{};Local x", "Local{};Local y"})
		{
			FTextPosition Position;
			if (!FindPosition(Source, Needle, Position))
			{
				return EXIT_FAILURE;
			}
			const auto Rename = VeroScriptRename::PrepareAtSource(Source, Position, "Renamed", "scoped-rename.vs");
			if (!Expect(Rename.bSuccess && Rename.bCanRename && Rename.Edits.size() == 3,
						"scoped type rename includes its declaration and excludes unrelated same-name types"))
			{
				std::cerr << Needle << ": " << Rename.Reason << " edits=" << Rename.Edits.size() << '\n';
				return EXIT_FAILURE;
			}
			auto Edits = Rename.Edits;
			std::sort(Edits.begin(), Edits.end(),
					  [](const auto& A, const auto& B)
					  {
						  return A.Range.StartColumn > B.Range.StartColumn;
					  });
			std::string Applied = Source;
			for (const auto& Edit : Edits)
			{
				const auto Begin = Edit.Range.StartColumn - 1;
				const auto Length = Edit.Range.EndColumn - Edit.Range.StartColumn;
				if (!Expect(Edit.Range.StartLine == 1 && Edit.Range.EndLine == 1 &&
								Applied.substr(Begin, Length) == Rename.OldName,
							"rename ranges select only the source identifier"))
				{
					return EXIT_FAILURE;
				}
				Applied.replace(Begin, Length, Edit.NewText);
			}
			const auto Compiled = VeroScript::VeroScriptCompile::CompileSource(Applied, "scoped-rename.vs");
			if (!Expect(Compiled.bSuccess, "renamed scoped types compile without changing unrelated declarations"))
			{
				std::cerr << Compiled.Error.Message << '\n';
				return EXIT_FAILURE;
			}
		}
	}

	{
		const std::string TemplateSource = "template<typename T,int Count=3> struct Box{T data[Count];template<typename U,int Amount=Count> U echo(U value){return value;}};struct Other{int Count;};void main(){Box<int> item=Box<int>();}";
		for (const auto* Needle : {"Count=3", "U,int Amount"})
		{
			FTextPosition Position;
			if (!FindPosition(TemplateSource, Needle, Position)) return EXIT_FAILURE;
			const auto Result = VeroScriptRename::PrepareAtSource(TemplateSource, Position, "Renamed", "template-rename.vs");
			if (!Expect(Result.bSuccess && Result.bCanRename && Result.Edits.size() == 3,
				"template parameter rename includes declaration and dependent uses, excluding unrelated names"))
			{
				std::cerr << Needle << ": " << Result.Reason << " edits=" << Result.Edits.size() << '\n';
				return EXIT_FAILURE;
			}
			auto Edits = Result.Edits;
			std::sort(Edits.begin(), Edits.end(), [](const auto& Left, const auto& Right)
			{
				return Left.Range.StartColumn > Right.Range.StartColumn;
			});
			auto Applied = TemplateSource;
			for (const auto& Edit : Edits)
			{
				Applied.replace(Edit.Range.StartColumn - 1, Edit.Range.EndColumn - Edit.Range.StartColumn, Edit.NewText);
			}
			const auto Compiled = VeroScript::VeroScriptCompile::CompileSource(Applied, "template-rename.vs");
			if (!Expect(Compiled.bSuccess, "renamed mixed template remains usable")) return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}
