// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptToolingTestVm.h"
#include "VeroScriptLocale.h"
#include "VeroScriptCompile.h"
#include "VeroScriptTooling.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	int Fail(const char* Message)
	{
		std::cerr << "VeroScriptHoverTests: " << Message << '\n';
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

	bool Contains(const std::string& Haystack, const std::string_view Needle)
	{
		return Haystack.find(Needle) != std::string::npos;
	}

	bool ContainsOnce(const std::string& Haystack, const std::string_view Needle)
	{
		const std::size_t First = Haystack.find(Needle);
		return First != std::string::npos && Haystack.find(Needle, First + Needle.size()) == std::string::npos;
	}
}

int main()
{

	{
		using namespace VeroScript::Tooling;
		const std::string Source = "void main(){int outer=7;auto f=[copy=outer,&alias=outer]() mutable ->int{return copy+alias;};}";
		for (const auto* Name : {"copy", "alias"})
		{
			FTextPosition Position;
			if (!FindPosition(Source, std::string("return ") + "copy+alias", Position)) return EXIT_FAILURE;
			Position.Column += 7u + (std::string_view(Name) == "alias" ? 5u : 0u);
			const auto Hover = VeroScriptHover::HoverAtSource(Source, Position, "initialized-capture-hover.vs");
			FTextPosition Declaration;
			if (!FindPosition(Source, std::string(Name) + "=outer", Declaration)) return EXIT_FAILURE;
			if (!Expect(Hover.bSuccess && !Hover.Definitions.empty() && Hover.Definitions.front().DefinitionRange.StartColumn == Declaration.Column && Contains(Hover.Contents, "int"), "initialized capture hover resolves the capture declaration"))
			{
				std::cerr << Hover.Contents << '\n';
				return EXIT_FAILURE;
			}
		}
	}

	using namespace VeroScript::Tooling;
	{
		const std::string Source =
		    "namespace Inner{int value=1;int make(){return 1;}}\n"
		    "namespace Outer{namespace Inner{int value=2;int make(){return 2;}}\n"
		    "int read(){return Inner::value+Inner::make();}}\n";
		for (const char* Name : {"value", "make"})
		{
			FTextPosition Position;
			if (!FindPosition(Source, std::string("Inner::") + Name, Position)) return EXIT_FAILURE;
			Position.Column += 7;
			const auto Hover = VeroScriptHover::HoverAtSource(Source, Position, "relative-namespace-hover.vs");
			if (!Expect(Hover.bSuccess && Hover.Definitions.size() == 1
			        && Hover.Definitions.front().DefinitionRange.StartLine == 2,
			        "qualified namespace hover agrees with nearest compiler prefix"))
			{
				std::cerr << Name << ": " << Hover.Contents << '\n';
				for (const auto &Hit : Hover.Definitions)
				{
					std::cerr << Hit.Name << " " << Hit.ContainerName << " " << Hit.DefinitionRange.StartLine << '\n';
				}
				return EXIT_FAILURE;
			}
		}
	}
	{
		const std::string Source =
		    "namespace A{class Parent<T>{public:T wrong;};class Box<T>:public Parent<T>{public:T value;};}\n"
		    "namespace B{class Parent<U>{public:U expected;};class Box<U>:public Parent<U>{public:U value;};}\n"
		    "void use(B::Box<int> box){print(box.value);print(box.expected);}\n";
		for (const char* Name : {"value", "expected"})
		{
			FTextPosition Position;
			if (!FindPosition(Source, std::string("box.") + Name, Position)) return EXIT_FAILURE;
			Position.Column += static_cast<std::uint32_t>(std::string_view("box.").size());
			const auto Hover = VeroScriptHover::HoverAtSource(Source, Position, "qualified-generic-hover.vs");
			if (!Expect(Hover.bSuccess && Hover.Definitions.size() == 1 && Hover.Definitions.front().Name == Name && Hover.Definitions.front().Detail == "int" && Hover.Definitions.front().ContainerName.rfind("B.", 0) == 0,
			        "qualified generic hover preserves the actual declaration and inherited member specialization"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		const std::string Source = "template<typename T,int Count=4> struct Buffer{T values[Count];};\n"
		                           "void main(){Buffer<int> value=Buffer<int>();}";
		FTextPosition Position;
		if (!FindPosition(Source, "Buffer<int>", Position)) return EXIT_FAILURE;
		const auto Hover = VeroScriptHover::HoverAtSource(Source, Position, "template-hover.vs");
		if (!Expect(Hover.bSuccess && Contains(Hover.Contents, "int Count = 4"), "mixed template parameter hover"))
		{
			std::cerr << Hover.Contents << '\n';
			return EXIT_FAILURE;
		}
		if (!FindPosition(Source, "Count];", Position)) return EXIT_FAILURE;
		const auto Value = VeroScriptHover::HoverAtSource(Source, Position, "template-hover.vs");
		if (!Expect(Value.bSuccess && Contains(Value.Contents, "template value parameter (int)"), "value parameter identity"))
		{
			std::cerr << Value.Contents << '\n';
			return EXIT_FAILURE;
		}
		if (!FindPosition(Source, "T values", Position)) return EXIT_FAILURE;
		const auto Type = VeroScriptHover::HoverAtSource(Source, Position, "template-hover.vs");
		if (!Expect(Type.bSuccess && Contains(Type.Contents, "generic type parameter"), "typename parameter identity")) return EXIT_FAILURE;
	}
	{
		const std::string MethodSource = "class Box{public:template<typename U> U echo(U value){return value;}};\n"
		                                 "void main(){Box object=Box();print(object.echo<int>(7));}";
		FTextPosition Position;
		if (!FindPosition(MethodSource, "echo<int>", Position)) return EXIT_FAILURE;
		const auto Hover = VeroScriptHover::HoverAtSource(MethodSource, Position, "method-template-hover.vs");
		if (!Expect(Hover.bSuccess && Contains(Hover.Contents, "template<U>"), "method template signature hover"))
		{
			std::cerr << Hover.Contents << '\n';
			return EXIT_FAILURE;
		}
	}
	if (!Expect(VeroScript::Locale::TrySetLanguageFromTag("en-US"),
	        "select English for hover assertions"))
	{
		return EXIT_FAILURE;
	}

	{
		constexpr const char* PrintSource = "print(\"hello, VeroScript\");\n";
		const FHoverResult Print = VeroScriptHover::HoverAtSource(
		    PrintSource, FTextPosition{1, 2}, "hello.vs");
		const std::string Markdown = VeroScriptHover::FormatMarkdown(Print);
		if (!Expect(Print.bSuccess && Contains(Markdown, "function print") && Contains(Markdown, "```veroscript\nvoid print(any value);\nvoid print(string fmt, ...);\n```") && Contains(Markdown, "\n\n*Built-in symbol.*") && !Contains(Markdown, "soft builtin") && Contains(Print.Contents, "\n(soft builtin)") && !Contains(Print.Contents, "```"),
		        "Markdown keeps print overload lines while native hover stays plain text"))
		{
			std::cerr << Markdown << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		FHoverResult Result;
		FHoverHit Hit;
		Hit.Name = "Value";
		Hit.Detail = "string Value = \"```\";";
		Hit.Documentation = "Preserve **documented** markup.\n\n@param Input a value\n@returns a result";
		Hit.FileLabel = "path/with_underscore.vs";
		Hit.DefinitionRange.StartLine = 7;
		Result.Definitions.push_back(Hit);
		Hit.Detail = "int Value(int Input)";
		Result.Definitions.push_back(Hit);
		const std::string Markdown = VeroScriptHover::FormatMarkdown(Result);
		if (!Expect(Contains(Markdown, "````veroscript\nstring Value = \"```\";\n````") && Contains(Markdown, "Preserve **documented** markup.") && Contains(Markdown, "Parameter `Input`: a value") && Contains(Markdown, "Returns: a result") && Contains(Markdown, "` defined at with_underscore.vs:7 `") && Contains(Markdown, "\n\n---\n\n") && VeroScriptHover::FormatMarkdown(FHoverResult{}).empty(),
		        "Markdown preserves code literals, documentation, source and overload boundaries"))
		{
			std::cerr << Markdown << '\n';
			return EXIT_FAILURE;
		}
		Result.Definitions.resize(1);
		Result.Definitions.front().Detail = "soft builtin (delegate.vs; delegate<R>)";
		if (!Expect(Contains(VeroScriptHover::FormatMarkdown(Result),
		                "` (delegate.vs; delegate<R>) `"),
		        "built-in metadata preserves generic notation as literal code"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* LabelSource =
		    "#define ENABLED 1\n"
		    "void main() {\n"
		    "  goto forward;\n"
		    "  forward: print(1);\n"
		    "  goto forward;\n"
		    "}\n";
		for (const FTextPosition Position : {FTextPosition{3, 8}, FTextPosition{5, 8}})
		{
			const FHoverResult Label = VeroScriptHover::HoverAtSource(LabelSource, Position, "goto-demo.vs");
			const FNavigationResult Navigation = VeroScriptNavigation::QueryAtSource(LabelSource, Position, "goto-demo.vs");
			if (!Expect(Label.bSuccess && Label.Definitions.size() == 1 && Label.Definitions.front().Kind == EOutlineSymbolKind::Label && Label.Definitions.front().DefinitionRange.StartLine == 4 && ContainsOnce(Label.Contents, "label main.forward") && Navigation.bSuccess && Navigation.Definitions.size() == 1,
			        "forward and backward goto resolve one label with physical definition lines"))
			{
				std::cerr << Label.Contents << '\n';
				return EXIT_FAILURE;
			}
		}
	}

	constexpr const char* Source = R"(
struct Point {
	int x;
	int Sum() { return x; }
}

/// Run the shared helper.
/// This text is shown by Hover.
void Helper() {}

/**
 * Return a documented value.
 * Supports block documentation.
 */
int BlockDoc() { return 7; }

/**
 * Add two documented values.
 * @note This unsupported tag remains visible.
 * @param Left first value.
 *   It may be negative.
 * @param Right second value.
 * @returns the combined value.
 */
int Tagged(int Left, int Right) { return Left + Right; }

// This comment is intentionally detached.

int Undocumented() { return 0; }

void main() {
	Point p = Point();
	int total = 1;
	Helper();
	BlockDoc();
	Tagged(1, 2);
	Undocumented();
}
)";

	for (const std::string TypeName : {"handle", "shared", "std::handle", "std::shared"})
	{
		const std::string SoftBuiltinSource = "struct Point { int x; }\n"
		                                      "void main() { " +
		    TypeName + "<Point> h = " + TypeName + "<Point>(); print(h); std::chrono::now(); }\n";
		for (const std::string Name : {TypeName + "<Point>", std::string("print(h)")})
		{
			FTextPosition Position;
			if (!Expect(FindPosition(SoftBuiltinSource, Name, Position),
			        "locate soft builtin hover"))
			{
				return EXIT_FAILURE;
			}
			// Hover the actual type leaf in a qualified spelling, rather than its namespace.
			if (Name == TypeName + "<Point>" && TypeName.find("::") != std::string::npos)
			{
				Position.Column += 5;
			}
			const FHoverResult Result = VeroScriptHover::HoverAtSource(
			    SoftBuiltinSource, Position, "<hover-soft-builtin>");
			if (!Expect(Result.bSuccess && ContainsOnce(Result.Contents, "soft builtin"),
			        "soft builtin hover includes its source once"))
			{
				std::cerr << Result.Contents << '\n';
				return EXIT_FAILURE;
			}
		}
	}

	for (const std::string TypeName : {"handle", "shared", "std::handle", "std::shared"})
	{
		for (const std::string Payload : {"int", "array<int>"})
		{
			const std::string Document = "void main() { " + TypeName + "<" + Payload + "> h = " + TypeName + "<" + Payload + ">(); print(h.value); }\n";
			FTextPosition Position;
			if (!Expect(FindPosition(Document, "value", Position), "locate soft generic value"))
			{
				return EXIT_FAILURE;
			}
			const FHoverResult Result = VeroScriptHover::HoverAtSource(
			    Document, Position, "<hover-soft-generic-value>");
			if (!Expect(Result.bSuccess && Result.Definitions.size() == 1 && Result.Definitions.front().Kind == EOutlineSymbolKind::Field && Result.Definitions.front().Detail == Payload,
			        "soft generic value hover specializes the payload type"))
			{
				std::cerr << Document << Result.Contents << '\n';
				return EXIT_FAILURE;
			}
		}
	}

	{
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "Helper();", Position), "locate Helper()"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult Result = VeroScriptHover::HoverAtSource(Source, Position, "<hover>");
		if (!Expect(Result.bSuccess && Result.bHasToken, "hover Helper"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Result.TokenText == "Helper", "token Helper"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Result.Definitions.size() == 1, "one Helper definition"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        Result.Definitions.front().Kind == EOutlineSymbolKind::Function,
		        "Helper is function"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        Contains(Result.Contents, "function Helper") && Contains(Result.Contents, "void") && Contains(Result.Contents, "Run the shared helper.") && Contains(Result.Contents, "This text is shown by Hover."),
		        "Contents should summarize and document Helper"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* BraceSource = R"(
struct Point {
	int x;
	Point(int value) { x = value; }
}
void main() {
	Point p = Point{7};
	int value = Point{7}.x;
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(BraceSource, "Point{7}", Position),
		        "locate brace constructor type"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult Result =
		    VeroScriptHover::HoverAtSource(BraceSource, Position, "<hover-brace-constructor>");
		if (!Expect(
		        Result.bSuccess && !Result.Definitions.empty() && Result.Definitions.front().Kind == EOutlineSymbolKind::Struct && Contains(Result.Contents, "struct Point"),
		        "brace constructor should resolve type hover"))
		{
			return EXIT_FAILURE;
		}

		if (!Expect(FindPosition(BraceSource, "Point{7}.x", Position),
		        "locate brace-constructed member"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("Point{7}.").size());
		const FHoverResult MemberResult =
		    VeroScriptHover::HoverAtSource(BraceSource, Position, "<hover-brace-member>");
		if (!Expect(
		        MemberResult.bSuccess && !MemberResult.Definitions.empty() && MemberResult.Definitions.front().Kind == EOutlineSymbolKind::Field && Contains(MemberResult.Contents, "Point.x") && Contains(MemberResult.Contents, "int"),
		        "brace construction should preserve receiver type for member hover"))
		{
			std::cerr << "Success=" << MemberResult.bSuccess
			          << " Definitions=" << MemberResult.Definitions.size()
			          << " Contents=[" << MemberResult.Contents << "]\n";
			if (!MemberResult.Definitions.empty())
			{
				std::cerr << "Kind="
				          << static_cast<int>(MemberResult.Definitions.front().Kind)
				          << " Detail=[" << MemberResult.Definitions.front().Detail << "]\n";
			}
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* GroupInitializerScopeSource = R"(
void main() {
	int outer_value = 7;
	fn()->int first = [&]() -> int { int outer_value = 9; return 0; },
		second = [&]() -> int { return outer_value; };
	print(second());
}
)";
		FTextPosition OuterDefinition;
		FTextPosition SecondInitializerUse;
		if (!Expect(
		        FindPosition(GroupInitializerScopeSource, "outer_value = 7", OuterDefinition),
		        "locate outer declaration-group variable") ||
		    !Expect(
		        FindPosition(
		            GroupInitializerScopeSource,
		            "return outer_value",
		            SecondInitializerUse),
		        "locate second declaration initializer"))
		{
			return EXIT_FAILURE;
		}
		SecondInitializerUse.Column +=
		    static_cast<std::uint32_t>(std::string_view("return ").size());
		const FHoverResult GroupInitializerResult = VeroScriptHover::HoverAtSource(
		    GroupInitializerScopeSource,
		    SecondInitializerUse,
		    "<hover-group-initializer-scope>");
		if (!Expect(
		        GroupInitializerResult.bSuccess && !GroupInitializerResult.Definitions.empty() && GroupInitializerResult.Definitions.front().DefinitionRange.StartLine == OuterDefinition.Line,
		        "a later declaration initializer must not inherit locals from an earlier lambda"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* ForStepScopeSource = R"(
void apply(fn()->void action) {}
void main() {
	int shadowed = 7;
	for (int i = 0; i < 1;
		apply(fn() -> void { int shadowed = 9; }), print(shadowed)) {}
}
)";
		FTextPosition OuterDefinition;
		FTextPosition StepUse;
		if (!Expect(
		        FindPosition(ForStepScopeSource, "shadowed = 7", OuterDefinition),
		        "locate outer for-step variable") ||
		    !Expect(
		        FindPosition(ForStepScopeSource, "print(shadowed)", StepUse),
		        "locate later for-step expression"))
		{
			return EXIT_FAILURE;
		}
		StepUse.Column += static_cast<std::uint32_t>(std::string_view("print(").size());
		const FHoverResult StepResult = VeroScriptHover::HoverAtSource(
		    ForStepScopeSource, StepUse, "<hover-for-step-scope>");
		if (!Expect(
		        StepResult.bSuccess && !StepResult.Definitions.empty() && StepResult.Definitions.front().DefinitionRange.StartLine == OuterDefinition.Line,
		        "a later for-step expression must not inherit locals from an earlier lambda"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "Tagged(1, 2)", Position),
		        "locate tagged documentation"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult Result =
		    VeroScriptHover::HoverAtSource(Source, Position, "<hover>");
		if (!Expect(
		        Result.bSuccess && Contains(Result.Contents, "Add two documented values.") && Contains(Result.Contents, "@note This unsupported tag remains visible.") && Contains(Result.Contents, "Parameter `Left`: first value.") && Contains(Result.Contents, "It may be negative.") && Contains(Result.Contents, "Parameter `Right`: second value.") && Contains(Result.Contents, "Returns: the combined value.") && !Contains(Result.Contents, "@param") && !Contains(Result.Contents, "@returns"),
		        "documentation tags should be parsed into structured hover text"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "BlockDoc();", Position), "locate BlockDoc()"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult Result = VeroScriptHover::HoverAtSource(Source, Position, "<hover>");
		if (!Expect(
		        Result.bSuccess && Contains(Result.Contents, "Return a documented value.") && Contains(Result.Contents, "Supports block documentation.") && !Contains(Result.Contents, "/**"),
		        "block documentation should be cleaned and displayed"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "Undocumented();", Position), "locate Undocumented()"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult Result = VeroScriptHover::HoverAtSource(Source, Position, "<hover>");
		if (!Expect(
		        Result.bSuccess && !Contains(Result.Contents, "intentionally detached"),
		        "blank line should detach documentation"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "Point p", Position), "locate Point type"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult Result = VeroScriptHover::HoverAtSource(Source, Position, "<hover>");
		if (!Expect(
		        Result.bSuccess && !Result.Definitions.empty() && Result.Definitions.front().Kind == EOutlineSymbolKind::Struct,
		        "hover Point struct"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Contains(Result.Contents, "struct Point"), "Contents struct Point"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "total = 1", Position), "locate total"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult Result = VeroScriptHover::HoverAtSource(Source, Position, "<hover>");
		if (!Expect(
		        Result.bSuccess && !Result.Definitions.empty() && Result.Definitions.front().Kind == EOutlineSymbolKind::Variable,
		        "hover local total"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        Contains(Result.Contents, "total") && Contains(Result.Contents, "int"),
		        "Contents local total"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* NewSyntaxSource = R"(
void main() {
	int first = 1, second = first + 1;
	do { print(second); } while (second < 3);
	for (int i = 0, j = i + 1; i < 2; i++, j++) { print(j); }
}
)";
		FTextPosition GroupPosition;
		FTextPosition ForPosition;
		if (!Expect(
		        FindPosition(NewSyntaxSource, "print(second)", GroupPosition),
		        "locate declaration-group use") ||
		    !Expect(
		        FindPosition(NewSyntaxSource, "print(j)", ForPosition),
		        "locate for declaration-group use"))
		{
			return EXIT_FAILURE;
		}
		GroupPosition.Column += static_cast<std::uint32_t>(std::string_view("print(").size());
		ForPosition.Column += static_cast<std::uint32_t>(std::string_view("print(").size());
		const FHoverResult GroupResult = VeroScriptHover::HoverAtSource(
		    NewSyntaxSource, GroupPosition, "<hover-new-syntax>");
		const FHoverResult ForResult = VeroScriptHover::HoverAtSource(
		    NewSyntaxSource, ForPosition, "<hover-new-syntax>");
		if (!Expect(
		        GroupResult.bSuccess && !GroupResult.Definitions.empty() && Contains(GroupResult.Contents, "second") && Contains(GroupResult.Contents, "int"),
		        "declaration-group local should resolve inside do-while") ||
		    !Expect(
		        ForResult.bSuccess && !ForResult.Definitions.empty() && Contains(ForResult.Contents, "j") && Contains(ForResult.Contents, "int"),
		        "for declaration-group local should resolve in the body"))
		{
			std::cerr << "group contents=[" << GroupResult.Contents
			          << "] definitions=" << GroupResult.Definitions.size()
			          << "\nfor contents=[" << ForResult.Contents
			          << "] definitions=" << ForResult.Definitions.size() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "Sum()", Position), "locate Sum method"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult Result = VeroScriptHover::HoverAtSource(Source, Position, "<hover>");
		if (!Expect(
		        Result.bSuccess && !Result.Definitions.empty() && Result.Definitions.front().Kind == EOutlineSymbolKind::Method,
		        "hover method Sum"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Contains(Result.Contents, "Sum"), "Contents mentions Sum"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* ReferenceSource = R"(
void mutate(int& value, const int& readonly) {
	int storage = 1;
	int& alias = storage;
	value = alias + readonly;
}
void main() {}
)";
		FTextPosition ParamPosition;
		FTextPosition AliasPosition;
		if (!Expect(
		        FindPosition(ReferenceSource, "value = alias", ParamPosition),
		        "locate reference parameter") ||
		    !Expect(
		        FindPosition(ReferenceSource, "alias + readonly", AliasPosition),
		        "locate reference local"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult ParamResult = VeroScriptHover::HoverAtSource(
		    ReferenceSource, ParamPosition, "<hover-reference-param>");
		const FHoverResult AliasResult = VeroScriptHover::HoverAtSource(
		    ReferenceSource, AliasPosition, "<hover-reference-local>");
		if (!Expect(
		        ParamResult.bSuccess && Contains(ParamResult.Contents, "int&") && !Contains(ParamResult.Contents, "ref int"),
		        "reference parameter hover should use current source spelling") ||
		    !Expect(
		        AliasResult.bSuccess && Contains(AliasResult.Contents, "int&") && !Contains(AliasResult.Contents, "ref int"),
		        "reference local hover should use current source spelling"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* ConvSource = R"(
class Flag { public: int v;
	Flag(int n) { v = n; }
	bool operator bool() { return v != 0; } }
)";
		FTextPosition Position;
		if (!Expect(FindPosition(ConvSource, "operator bool", Position), "locate operator bool"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult Result =
		    VeroScriptHover::HoverAtSource(ConvSource, Position, "<hover-conv>");
		if (!Expect(Result.bSuccess && !Result.Definitions.empty(), "hover conversion op"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        Result.Definitions.front().Name == "operator bool",
		        "conversion hover displays operator bool"))
		{
			std::cerr << "name=[" << Result.Definitions.front().Name << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
		        !Contains(Result.Contents, "operator conv"),
		        "Contents should not expose operator conv"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* HostSource = R"(
void main() {
	formatf("x=%d", 1);
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(HostSource, "formatf(", Position), "locate formatf"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult Result =
		    VeroScriptHover::HoverAtSource(HostSource, Position, "<hover-format-f>");
		if (!Expect(
		        Result.bSuccess && !Result.Definitions.empty() && Contains(Result.Contents, "formatf"),
		        "hover soft host formatf"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(Contains(Result.Contents, "soft builtin"), "formatf marked soft"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(Contains(Result.Contents, "..."), "formatf hover shows varargs"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* CastSource = R"(
void main() {
	int exact = cast<int>(1.5);
	optional<int> checked = try_cast<int>(exact);
	const int immutable = 2;
	int mutable_value = const_cast<int>(immutable);
}
)";
		FTextPosition CastPosition;
		FTextPosition TryCastPosition;
		FTextPosition ConstCastPosition;
		if (!Expect(
		        FindPosition(CastSource, "cast<", CastPosition) && FindPosition(CastSource, "try_cast<", TryCastPosition) && FindPosition(CastSource, "const_cast<", ConstCastPosition),
		        "locate checked cast intrinsics"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult CastResult = VeroScriptHover::HoverAtSource(
		    CastSource, CastPosition, "<hover-cast>");
		const FHoverResult TryCastResult = VeroScriptHover::HoverAtSource(
		    CastSource, TryCastPosition, "<hover-cast>");
		const FHoverResult ConstCastResult = VeroScriptHover::HoverAtSource(
		    CastSource, ConstCastPosition, "<hover-cast>");
		if (!Expect(
		        CastResult.bSuccess && !CastResult.Definitions.empty() && Contains(CastResult.Contents, "T cast<T>") && Contains(CastResult.Contents, "soft builtin") && TryCastResult.bSuccess && !TryCastResult.Definitions.empty() && Contains(TryCastResult.Contents, "optional<T> try_cast<T>") && Contains(TryCastResult.Contents, "soft builtin") && ConstCastResult.bSuccess && !ConstCastResult.Definitions.empty() && Contains(ConstCastResult.Contents, "T const_cast<T>") && Contains(ConstCastResult.Contents, "soft builtin"),
		        "cast intrinsics expose tooling signatures"))
		{
			std::cerr << "cast=[" << CastResult.Contents << "]\n"
			          << "try_cast=[" << TryCastResult.Contents << "]\n"
			          << "const_cast=[" << ConstCastResult.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* WideConversionSource = R"(
void main() {
	int64 signed_value = to_int64("9223372036854775807");
	uint64 unsigned_value = to_uint64("18446744073709551615");
	float precise_value = to_float("1.25");
}
)";
		FTextPosition SignedPosition;
		FTextPosition UnsignedPosition;
		FTextPosition FloatPosition;
		if (!Expect(
		        FindPosition(WideConversionSource, "to_int64(", SignedPosition) && FindPosition(WideConversionSource, "to_uint64(", UnsignedPosition) && FindPosition(WideConversionSource, "to_float(", FloatPosition),
		        "locate numeric conversion builtins"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult SignedResult = VeroScriptHover::HoverAtSource(
		    WideConversionSource, SignedPosition, "<hover-wide-conversions>");
		const FHoverResult UnsignedResult = VeroScriptHover::HoverAtSource(
		    WideConversionSource, UnsignedPosition, "<hover-wide-conversions>");
		const FHoverResult FloatResult = VeroScriptHover::HoverAtSource(
		    WideConversionSource, FloatPosition, "<hover-wide-conversions>");
		if (!Expect(
		        SignedResult.bSuccess && !SignedResult.Definitions.empty() && Contains(SignedResult.Contents, "to_int64") && Contains(SignedResult.Contents, "int64 (any value)") && UnsignedResult.bSuccess && !UnsignedResult.Definitions.empty() && Contains(UnsignedResult.Contents, "to_uint64") && Contains(UnsignedResult.Contents, "uint64 (any value)") && FloatResult.bSuccess && !FloatResult.Definitions.empty() && Contains(FloatResult.Contents, "to_float") && Contains(FloatResult.Contents, "float (any value)"),
		        "numeric conversion builtins expose signatures in hover"))
		{
			std::cerr << "signed=[" << SignedResult.Contents << "]\n"
			          << "unsigned=[" << UnsignedResult.Contents << "]\n"
			          << "float=[" << FloatResult.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* PauseSource = R"(
void main() {
	std::io::pause();
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(PauseSource, "pause(", Position), "locate qualified pause"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult Result =
		    VeroScriptHover::HoverAtSource(PauseSource, Position, "<hover-pause>");
		if (!Expect(
		        Result.bSuccess && !Result.Definitions.empty() && Contains(Result.Contents, "pause") && Contains(Result.Contents, "input line") && Contains(Result.Contents, "soft builtin"),
		        "pause hover exposes signature and behavior"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* SoftMethodSource = R"(
void main() {
	array<int> xs = array<int>();
	xs.push(1);
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(SoftMethodSource, "xs.push", Position), "locate xs.push"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("xs.").size());
		const FHoverResult Result =
		    VeroScriptHover::HoverAtSource(SoftMethodSource, Position, "<hover-push>");
		if (!Expect(
		        Result.bSuccess && !Result.Definitions.empty() && Contains(Result.Contents, "push"),
		        "hover soft container push"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
		        Contains(Result.Contents, "array<int>"),
		        "push Detail shows chaining return type"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* StandardLibrarySource = R"(
void main() {
	std::algorithm::shuffle(array<int>(), array<unsigned int>());
	std::random::next_int(array<unsigned int>(), 1, 10);
	std::concurrent::make_mutex();
}
)";
		FTextPosition AlgorithmPosition;
		FTextPosition RandomPosition;
		FTextPosition MutexPosition;
		if (!Expect(
		        FindPosition(StandardLibrarySource, "shuffle(", AlgorithmPosition) && FindPosition(StandardLibrarySource, "next_int(", RandomPosition) && FindPosition(StandardLibrarySource, "make_mutex(", MutexPosition),
		        "locate expanded standard-library symbols"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult AlgorithmResult = VeroScriptHover::HoverAtSource(
		    StandardLibrarySource, AlgorithmPosition, "<hover-standard-library>");
		const FHoverResult RandomResult = VeroScriptHover::HoverAtSource(
		    StandardLibrarySource, RandomPosition, "<hover-standard-library>");
		const FHoverResult MutexResult = VeroScriptHover::HoverAtSource(
		    StandardLibrarySource, MutexPosition, "<hover-standard-library>");
		if (!Expect(
		        AlgorithmResult.bSuccess && Contains(AlgorithmResult.Contents, "array<T> values") && Contains(AlgorithmResult.Contents, "array<unsigned int> engine") && RandomResult.bSuccess && Contains(RandomResult.Contents, "int minimum") && MutexResult.bSuccess && Contains(MutexResult.Contents, "mutex std::concurrent::make_mutex();"),
		        "expanded standard-library hover exposes typed signatures"))
		{
			std::cerr << "algorithm=[" << AlgorithmResult.Contents << "]\n"
			          << "random=[" << RandomResult.Contents << "]\n"
			          << "mutex=[" << MutexResult.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* MacroSource = R"(
/// The answer to everything.
#define ANSWER 42
void main() {
	print(ANSWER);
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(MacroSource, "print(ANSWER)", Position), "locate ANSWER use"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("print(").size());
		const FHoverResult Result =
		    VeroScriptHover::HoverAtSource(MacroSource, Position, "<hover-macro>");
		if (!Expect(
		        Result.bSuccess && !Result.Definitions.empty() && Contains(Result.Contents, "ANSWER") && Contains(Result.Contents, "42") && Contains(Result.Contents, "The answer to everything."),
		        "hover object-like macro"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* FnMacroSource = R"(
#define MAX(a, b) ((a) > (b) ? (a) : (b))
void main() {
	print(MAX(1, 2));
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(FnMacroSource, "print(MAX", Position), "locate MAX use"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("print(").size());
		const FHoverResult Result =
		    VeroScriptHover::HoverAtSource(FnMacroSource, Position, "<hover-fn-macro>");
		if (!Expect(
		        Result.bSuccess && !Result.Definitions.empty() && Contains(Result.Contents, "MAX") && Contains(Result.Contents, "(a, b)") && Contains(Result.Contents, "#define"),
		        "hover function-like macro"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* UndefMacroSource = R"(
#define TEMP 1
#undef TEMP
void main() {
	print(TEMP);
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(UndefMacroSource, "print(TEMP)", Position), "locate TEMP after undef"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("print(").size());
		const FHoverResult Result =
		    VeroScriptHover::HoverAtSource(UndefMacroSource, Position, "<hover-undef-macro>");
		bool bMacroHover = false;
		for (const auto& Def : Result.Definitions)
		{
			if (Def.Detail.rfind("#define", 0) == 0 && Def.Name == "TEMP")
			{
				bMacroHover = true;
				break;
			}
		}
		if (!Expect(!bMacroHover, "hover does not resolve undef'd macro"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* UseBeforeUndefSource = R"(
#define TEMP 1
void main() {
	print(TEMP);
}
#undef TEMP
)";
		FTextPosition Position;
		if (!Expect(FindPosition(UseBeforeUndefSource, "print(TEMP)", Position), "locate TEMP before undef"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("print(").size());
		const FHoverResult Result =
		    VeroScriptHover::HoverAtSource(UseBeforeUndefSource, Position, "<hover-before-undef-macro>");
		if (!Expect(
		        Result.bSuccess && !Result.Definitions.empty() && Contains(Result.Contents, "TEMP") && Contains(Result.Contents, "1"),
		        "hover resolves macro before later undef"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* VaMacroSource = R"(
#define LOG(fmt, ...) print(fmt)
void main() {
	LOG("x");
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(VaMacroSource, "LOG(\"x\")", Position), "locate LOG use"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult Result =
		    VeroScriptHover::HoverAtSource(VaMacroSource, Position, "<hover-va-macro>");
		if (!Expect(
		        Result.bSuccess && !Result.Definitions.empty() && Contains(Result.Contents, "LOG") && Contains(Result.Contents, "...") && Contains(Result.Contents, "fmt"),
		        "hover variadic macro"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	// Contextual lambda parameters also resolve through the `function` source alias;
	// tooling displays the canonical `fn(...)->R` signature.
	{
		constexpr const char* ContextualLambda = R"(
void main() {
	function(int)->int f = function(x) => x + 1;
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(ContextualLambda, "x) =>", Position), "locate contextual x"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult Result =
		    VeroScriptHover::HoverAtSource(ContextualLambda, Position, "<hover-ctx-lambda>");
		if (!Expect(
		        Result.bSuccess && !Result.Definitions.empty() && Contains(Result.Contents, "int"),
		        "hover contextual lambda param shows expected int"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* MultiParentHover = R"(
interface IA<T> { virtual T getA() = 0; }
interface IB { virtual void b() = 0; }
interface IC : IA<int>, IB { virtual void c() = 0; }

void use(IC x) {
	x.getA();
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(MultiParentHover, "x.getA()", Position), "locate x.getA hover"))
		{
			return EXIT_FAILURE;
		}
		// Land on the call-site member name, not the IA declaration.
		Position.Column += static_cast<std::uint32_t>(std::string_view("x.").size());
		const FHoverResult Result =
		    VeroScriptHover::HoverAtSource(MultiParentHover, Position, "<hover-mp>");
		if (!Expect(
		        Result.bSuccess && Contains(Result.Contents, "getA") && Contains(Result.Contents, "int"),
		        "hover inherited IA<int>.getA specializes T"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* MulticastPlusEq = R"(
void main() {
	multicast_delegate a = multicast_delegate();
	fn()->void h = fn() -> void {};
	a += h;
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(MulticastPlusEq, "a += h", Position), "locate multicast_delegate +="))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("a ").size());
		const FHoverResult Result =
		    VeroScriptHover::HoverAtSource(MulticastPlusEq, Position, "<hover-md-plus>");
		if (!Expect(
		        Result.bSuccess && Contains(Result.Contents, "multicast_delegate.add"),
		        "hover multicast_delegate += resolves to add"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* AutoForeachSource = R"(
void main() {
	ordered_map<string, int> m = ordered_map<string, int>();
	for (auto& [k, v] : m) {
		print(k);
		print(v);
	}
}
)";
		FTextPosition KeyPos;
		FTextPosition ValuePos;
		if (!Expect(FindPosition(AutoForeachSource, "print(k)", KeyPos), "locate auto foreach k"))
		{
			return EXIT_FAILURE;
		}
		KeyPos.Column += static_cast<std::uint32_t>(std::string_view("print(").size());
		if (!Expect(FindPosition(AutoForeachSource, "print(v)", ValuePos), "locate auto foreach v"))
		{
			return EXIT_FAILURE;
		}
		ValuePos.Column += static_cast<std::uint32_t>(std::string_view("print(").size());
		const FHoverResult KeyHover =
		    VeroScriptHover::HoverAtSource(AutoForeachSource, KeyPos, "<hover-auto-k>");
		const FHoverResult ValueHover =
		    VeroScriptHover::HoverAtSource(AutoForeachSource, ValuePos, "<hover-auto-v>");
		if (!Expect(
		        KeyHover.bSuccess && Contains(KeyHover.Contents, "string"),
		        "auto foreach key infers string"))
		{
			std::cerr << "Key Contents=[" << KeyHover.Contents << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
		        ValueHover.bSuccess && Contains(ValueHover.Contents, "int") && !Contains(ValueHover.Contents, "auto"),
		        "auto foreach value infers int (not auto)"))
		{
			std::cerr << "Value Contents=[" << ValueHover.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* EntriesForeachSource = R"(
void main() {
	ordered_map<string, int> m = ordered_map<string, int>();
	for (auto p : m.entries()) {
		print(p);
	}
}
)";
		FTextPosition Pos;
		if (!Expect(FindPosition(EntriesForeachSource, "print(p)", Pos), "locate entries foreach p"))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("print(").size());
		const FHoverResult Result =
		    VeroScriptHover::HoverAtSource(EntriesForeachSource, Pos, "<hover-entries-p>");
		if (!Expect(
		        Result.bSuccess && Contains(Result.Contents, "pair"),
		        "entries() foreach auto infers pair"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* MultiSetForeachSource = R"(
void main() {
	ordered_multiset<string> counts = {"alpha", "beta", "alpha"};
	for (auto value : counts) {
		print(value);
	}
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(MultiSetForeachSource, "print(value)", Position),
		        "locate multiset auto foreach value"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("print(").size());
		const FHoverResult Result = VeroScriptHover::HoverAtSource(
		    MultiSetForeachSource,
		    Position,
		    "<hover-multiset-auto>");
		if (!Expect(
		        Result.bSuccess && Contains(Result.Contents, "string") && !Contains(Result.Contents, "auto"),
		        "multiset foreach auto binding infers string"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		std::error_code RemoveError;
		const std::filesystem::path TempRoot =
		    std::filesystem::temp_directory_path() / "VeroScriptTooling-format-hover";
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot / "lib");
		{
			std::ofstream Lib(TempRoot / "lib" / "string.vs");
			Lib << "export string format(string fmt, array<string> args) { return fmt; }\n";
		}
		constexpr const char* FormatImportSource = R"(
import "string.vs";
void main() {
	format("x={}", 1);
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(FormatImportSource, "format(", Position), "locate imported format hover"))
		{
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}
		const std::string LibPath = (TempRoot / "lib").string();
		const std::string AppPath = (TempRoot / "app.vs").string();
		FHoverOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.ImportPaths.push_back(LibPath);
		const FHoverResult Result = VeroScriptHover::HoverAtSource(
		    FormatImportSource,
		    Position,
		    AppPath,
		    Options);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(
		        Result.bSuccess && Contains(Result.Contents, "...") && Contains(Result.Contents, "soft builtin") && !Contains(Result.Contents, "array<string>"),
		        "imported format hover prefers soft varargs"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		std::error_code RemoveError;
		const std::filesystem::path TempRoot =
		    std::filesystem::temp_directory_path() / "VeroScriptTooling-import-doc-hover";
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot / "lib");
		{
			std::ofstream Lib(TempRoot / "lib" / "documented.vs");
			Lib << "/// Documentation from an imported module.\n"
			    << "export int documented() { return 1; }\n";
		}
		constexpr const char* ImportSource = R"(
import "documented.vs";
void main() {
	documented();
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(ImportSource, "documented();", Position),
		        "locate imported documented hover"))
		{
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}
		const std::string LibPath = (TempRoot / "lib").string();
		const std::string AppPath = (TempRoot / "app.vs").string();
		FHoverOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.ImportPaths.push_back(LibPath);
		const FHoverResult Result =
		    VeroScriptHover::HoverAtSource(ImportSource, Position, AppPath, Options);
		constexpr const char* IncludeSource = "#include \"documented.vs\"\n";
		FTextPosition IncludePosition;
		if (!Expect(
		        FindPosition(IncludeSource, "documented.vs", IncludePosition),
		        "locate include path hover"))
		{
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}
		const FHoverResult IncludeResult =
		    VeroScriptHover::HoverAtSource(IncludeSource, IncludePosition, AppPath, Options);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(
		        Result.bSuccess && Contains(Result.Contents, "Documentation from an imported module.") && Contains(Result.Contents, "exported | defined at documented.vs:2"),
		        "hover should show imported documentation and definition metadata"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
		        IncludeResult.bSuccess && Contains(IncludeResult.Contents, "re-exported module") && Contains(IncludeResult.Contents, "Forwards declarations exported by the target module.") && Contains(IncludeResult.Contents, "Private declarations remain hidden."),
		        "include path hover should explain re-export visibility"))
		{
			std::cerr << "Contents=[" << IncludeResult.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* OpMirrorSource = R"(
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
		if (!Expect(FindPosition(OpMirrorSource, "a != b", NePos), "locate !="))
		{
			return EXIT_FAILURE;
		}
		NePos.Column += static_cast<std::uint32_t>(std::string_view("a ").size());
		const FHoverResult NeHover =
		    VeroScriptHover::HoverAtSource(OpMirrorSource, NePos, "<hover-ne>");
		if (!Expect(
		        NeHover.bSuccess && Contains(NeHover.Contents, "operator=="),
		        "!= use-site hovers operator=="))
		{
			std::cerr << "Ne Contents=[" << NeHover.Contents << "]\n";
			return EXIT_FAILURE;
		}
		FTextPosition GtPos;
		if (!Expect(FindPosition(OpMirrorSource, "b > a", GtPos), "locate >"))
		{
			return EXIT_FAILURE;
		}
		GtPos.Column += static_cast<std::uint32_t>(std::string_view("b ").size());
		const FHoverResult GtHover =
		    VeroScriptHover::HoverAtSource(OpMirrorSource, GtPos, "<hover-gt>");
		if (!Expect(
		        GtHover.bSuccess && Contains(GtHover.Contents, "operator<"),
		        "> use-site hovers operator<"))
		{
			std::cerr << "Gt Contents=[" << GtHover.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	// A grouping / ordinary-call close paren is not an `operator()` use-site.
	{
		constexpr const char* GroupParenSource = R"(
void main() {
	int x = (1 + 2);
	print(x);
}
)";
		FTextPosition ClosePos;
		if (!Expect(FindPosition(GroupParenSource, "1 + 2)", ClosePos), "locate group )"))
		{
			return EXIT_FAILURE;
		}
		// Land the caret directly on the closing ')'.
		ClosePos.Column += static_cast<std::uint32_t>(std::string_view("1 + 2").size());
		const FHoverResult CloseHover =
		    VeroScriptHover::HoverAtSource(GroupParenSource, ClosePos, "<hover-group>");
		if (!Expect(
		        CloseHover.bSuccess && CloseHover.TokenText != "operator()",
		        ") should not classify as operator() use-site"))
		{
			std::cerr << "TokenText=[" << CloseHover.TokenText << "]\n";
			return EXIT_FAILURE;
		}
	}

	// Destructor declaration token + locals inside destructor body.
	{
		constexpr const char* DtorSource = R"(
class Owner {
public:
	virtual ~Owner() {
		int leftover = 1;
		print(leftover);
	}
}
)";
		FTextPosition DtorPos;
		if (!Expect(FindPosition(DtorSource, "~Owner()", DtorPos), "locate ~Owner"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult DtorHover =
		    VeroScriptHover::HoverAtSource(DtorSource, DtorPos, "<hover-dtor>");
		if (!Expect(
		        DtorHover.bSuccess && DtorHover.TokenText == "~Owner" && !DtorHover.Definitions.empty() && DtorHover.Definitions.front().Kind == EOutlineSymbolKind::Destructor && Contains(DtorHover.Contents, "~Owner") && Contains(DtorHover.Contents, "destructor") && !Contains(DtorHover.Contents, "constructor"),
		        "hover destructor declaration"))
		{
			std::cerr << "Token=[" << DtorHover.TokenText << "] Contents=["
			          << DtorHover.Contents << "]\n";
			return EXIT_FAILURE;
		}

		FTextPosition LocalPos;
		if (!Expect(FindPosition(DtorSource, "print(leftover)", LocalPos), "locate leftover use"))
		{
			return EXIT_FAILURE;
		}
		LocalPos.Column += static_cast<std::uint32_t>(std::string_view("print(").size());
		const FHoverResult LocalHover =
		    VeroScriptHover::HoverAtSource(DtorSource, LocalPos, "<hover-dtor-local>");
		if (!Expect(
		        LocalHover.bSuccess && LocalHover.TokenText == "leftover" && !LocalHover.Definitions.empty() && Contains(LocalHover.Contents, "int"),
		        "hover local inside destructor body"))
		{
			std::cerr << "Contents=[" << LocalHover.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	// Interface method kinds: abstract / static default / virtual default.
	{
		constexpr const char* IfaceSource = R"(
interface IOps {
	virtual int peek() = 0;
	int read() { return 1; }
	virtual int get() { return 2; }
}
)";
		FTextPosition PeekPos;
		if (!Expect(FindPosition(IfaceSource, "peek()", PeekPos), "locate abstract peek"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult PeekHover =
		    VeroScriptHover::HoverAtSource(IfaceSource, PeekPos, "<hover-iface-abstract>");
		if (!Expect(
		        PeekHover.bSuccess && Contains(PeekHover.Contents, "abstract"),
		        "hover abstract interface method"))
		{
			std::cerr << "Contents=[" << PeekHover.Contents << "]\n";
			return EXIT_FAILURE;
		}

		FTextPosition ReadPos;
		if (!Expect(FindPosition(IfaceSource, "read()", ReadPos), "locate static default read"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult ReadHover =
		    VeroScriptHover::HoverAtSource(IfaceSource, ReadPos, "<hover-iface-default>");
		if (!Expect(
		        ReadHover.bSuccess && Contains(ReadHover.Contents, "default") && !Contains(ReadHover.Contents, "virtual"),
		        "hover non-virtual interface default"))
		{
			std::cerr << "Contents=[" << ReadHover.Contents << "]\n";
			return EXIT_FAILURE;
		}

		FTextPosition GetPos;
		if (!Expect(FindPosition(IfaceSource, "get()", GetPos), "locate virtual default get"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult GetHover =
		    VeroScriptHover::HoverAtSource(IfaceSource, GetPos, "<hover-iface-virtual>");
		if (!Expect(
		        GetHover.bSuccess && Contains(GetHover.Contents, "virtual"),
		        "hover virtual interface default"))
		{
			std::cerr << "Contents=[" << GetHover.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		// Unrelated syntax junk must not wipe soft-builtin hover (IDE: stray `no`).
		constexpr const char* BrokenBuffer = R"(
void main(string[] args) {
	print("argc=" + to_string(len(args)));
	for (int i = 0; i < len(args); ++i) {
		print(args[i]);
	}
	no
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(BrokenBuffer, "to_string(", Position), "locate to_string in broken buffer"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult Result =
		    VeroScriptHover::HoverAtSource(BrokenBuffer, Position, "<hover-broken-to-string>");
		if (!Expect(
		        Result.bSuccess && !Result.Definitions.empty() && Contains(Result.Contents, "to_string") && Contains(Result.Contents, "soft builtin"),
		        "hover soft to_string despite unrelated syntax error"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		// Unfinished statement without ';' (IDE: lone `to` before `}`) must not wipe
		// hover elsewhere — semicolon recovery should re-parse the buffer.
		constexpr const char* MissingSemi = R"(
void main(string[] args) {
	print("argc=" + to_string(len(args)));
	for (int i = 0; i < len(args); ++i) {
		print(args[i]);
	}
	to
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(MissingSemi, "to_string(", Position), "locate to_string with missing semi"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult Result =
		    VeroScriptHover::HoverAtSource(MissingSemi, Position, "<hover-missing-semi>");
		if (!Expect(
		        Result.bSuccess && !Result.Definitions.empty() && Contains(Result.Contents, "to_string") && Contains(Result.Contents, "soft builtin"),
		        "hover to_string despite unfinished statement without semicolon"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}

		FTextPosition MainPos;
		if (!Expect(FindPosition(MissingSemi, "main(", MainPos), "locate main with missing semi"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult MainHover =
		    VeroScriptHover::HoverAtSource(MissingSemi, MainPos, "<hover-missing-semi-main>");
		if (!Expect(
		        MainHover.bSuccess && !MainHover.Definitions.empty() && Contains(MainHover.Contents, "main"),
		        "hover user symbol main despite unfinished statement without semicolon"))
		{
			std::cerr << "Contents=[" << MainHover.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		// MI: declaration-order DFS must match compiler (first base subtree before
		// later bases). BFS would wrongly pick ILike::m over Base::m.
		constexpr const char* MiSource = R"(
class Base {
public:
	int m() { return 1; }
}

class A : public Base {
}

interface ILike {
	int m() { return 2; }
}

class Root : public A, public ILike {
}

void main() {
	Root r = Root();
	r.m();
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(MiSource, "r.m()", Position), "locate r.m in MI hover"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("r.").size());
		const FHoverResult Result =
		    VeroScriptHover::HoverAtSource(MiSource, Position, "<hover-mi-dfs>");
		if (!Expect(
		        Result.bSuccess && !Result.Definitions.empty() && Result.Definitions.front().Name == "m" && Result.Definitions.front().ContainerName == "Base",
		        "hover r.m binds Base::m via DFS (not ILike::m via BFS)"))
		{
			std::cerr << "name=[" << (Result.Definitions.empty() ? "" : Result.Definitions.front().Name)
			          << "] container=["
			          << (Result.Definitions.empty() ? "" : Result.Definitions.front().ContainerName)
			          << "] Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FHostMemberDesc ReadOnlyMethod;
		ReadOnlyMethod.Kind = VeroScript::EHostMemberKind::Method;
		ReadOnlyMethod.Name = "read_value";
		ReadOnlyMethod.ReturnType = VeroScript::ENativeType::Int;
		ReadOnlyMethod.bConstMethod = true;
		if (!Expect(
		        Contains(
		            VeroScript::FormatHostMemberSignature(ReadOnlyMethod, "Counter"),
		            ") const"),
		        "const host method signature includes const"))
		{
			return EXIT_FAILURE;
		}

		ReadOnlyMethod.bConstMethod = false;
		if (!Expect(
		        !Contains(
		            VeroScript::FormatHostMemberSignature(ReadOnlyMethod, "Counter"),
		            ") const"),
		        "non-const host method signature omits const"))
		{
			return EXIT_FAILURE;
		}

		ReadOnlyMethod.Kind = VeroScript::EHostMemberKind::StaticMethod;
		if (!Expect(
		        VeroScript::FormatHostMemberSignature(ReadOnlyMethod, "Counter")
		                .rfind("static ", 0) == 0,
		        "static host method signature includes static"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		VeroScript::FHostTypeId CounterId;
		if (!Expect(
		        Registry.RegisterType("Counter", VeroScript::EHostTypeKind::Class, CounterId, ErrorId),
		        "RegisterType Counter for hover"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::FHostMemberDesc Getter;
		Getter.Kind = VeroScript::EHostMemberKind::PropertyGetter;
		Getter.Name = "value";
		Getter.Documentation = "Current counter value supplied by the host.";
		Getter.ReturnType = VeroScript::ENativeType::Int;
		Getter.Annotations = {{"UPROPERTY",
		    {{"VisibleAnywhere", std::nullopt},
		        {"Category", std::string("Host|State")}}}};
		if (!Expect(Registry.RegisterMember(CounterId, Getter, ErrorId), "value getter for hover"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::FHostMemberDesc Inventory;
		Inventory.Kind = VeroScript::EHostMemberKind::PropertyGetter;
		Inventory.Name = "inventory";
		Inventory.ReturnType = VeroScript::ENativeType::Map;
		Inventory.ReturnKeyTypeName = "int";
		Inventory.ReturnValueTypeName = "array<unsigned>";
		if (!Expect(
		        Registry.RegisterMember(CounterId, Inventory, ErrorId),
		        "inventory getter for container hover"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::FHostMemberDesc Answer;
		Answer.Kind = VeroScript::EHostMemberKind::StaticConstant;
		Answer.Name = "Answer";
		Answer.Documentation = "The host-defined answer.";
		Answer.ReturnType = VeroScript::ENativeType::Int;
		if (!Expect(
		        Registry.RegisterMember(CounterId, Answer, ErrorId),
		        "static constant for hover"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::FGenericHostTypeTemplateId TemplateId;
		if (!Expect(
		        Registry.RegisterGenericHostTypeTemplateMetadata(
		            "HostPair", 2, TemplateId, ErrorId, "A generic host pair."),
		        "generic host template for hover"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* HostMemberSource = R"(
void foo(Counter c) {
	c.value;
	c.inventory;
	Counter.Answer;
	c.Answer;
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(HostMemberSource, "c.value", Position), "locate c.value hover"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("c.").size());
		FHoverOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;
		const FHoverResult Result = VeroScriptHover::HoverAtSource(
		    HostMemberSource, Position, "<hover-host-member>", Options);
		if (!Expect(
		        Result.bSuccess && Contains(Result.Contents, "value") && Contains(Result.Contents, "int") && Contains(Result.Contents, "Current counter value") && Contains(Result.Contents, "UPROPERTY(VisibleAnywhere") && Contains(Result.Contents, "Category=\"Host|State\""),
		        "hover host property c.value includes catalog documentation"))
		{
			std::cerr << "Contents=[" << Result.Contents << "] docs=["
			          << (Result.Definitions.empty()
			                     ? std::string{}
			                     : Result.Definitions.front().Documentation)
			          << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
		        FindPosition(HostMemberSource, "c.inventory", Position),
		        "locate c.inventory hover"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("c.").size());
		const FHoverResult ContainerResult = VeroScriptHover::HoverAtSource(
		    HostMemberSource, Position, "<hover-host-container-member>", Options);
		if (!Expect(
		        ContainerResult.bSuccess && Contains(ContainerResult.Contents, "map<int,array<unsigned int>>"),
		        "hover host container property preserves canonical nested generic type"))
		{
			std::cerr << "Contents=[" << ContainerResult.Contents << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
		        FindPosition(HostMemberSource, "Counter.Answer", Position),
		        "locate Counter.Answer hover"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("Counter.").size());
		const FHoverResult StaticResult = VeroScriptHover::HoverAtSource(
		    HostMemberSource, Position, "<hover-host-static-constant>", Options);
		if (!Expect(
		        StaticResult.bSuccess && !StaticResult.Definitions.empty() && StaticResult.Definitions.front().Kind == EOutlineSymbolKind::Constant && Contains(StaticResult.Contents, "The host-defined answer"),
		        "type-qualified static constant hover"))
		{
			std::cerr << "Contents=[" << StaticResult.Contents << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(FindPosition(HostMemberSource, "c.Answer", Position), "locate invalid c.Answer"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("c.").size());
		const FHoverResult InvalidInstanceResult = VeroScriptHover::HoverAtSource(
		    HostMemberSource, Position, "<hover-invalid-instance-static>", Options);
		bool bResolvedStaticConstant = false;
		for (const FHoverHit& Hit : InvalidInstanceResult.Definitions)
		{
			bResolvedStaticConstant = bResolvedStaticConstant || Hit.Kind == EOutlineSymbolKind::Constant;
		}
		if (!Expect(!bResolvedStaticConstant, "instance must not resolve a static constant"))
		{
			return EXIT_FAILURE;
		}
		constexpr const char* GenericTemplateSource = "void main() { HostPair; }\n";
		if (!Expect(FindPosition(GenericTemplateSource, "HostPair", Position), "locate HostPair hover"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult TemplateResult = VeroScriptHover::HoverAtSource(
		    GenericTemplateSource, Position, "<hover-generic-host-template>", Options);
		if (!Expect(
		        TemplateResult.bSuccess && Contains(TemplateResult.Contents, "HostPair<T1, T2>") && Contains(TemplateResult.Contents, "A generic host pair."),
		        "generic host template hover includes arity and documentation"))
		{
			std::cerr << "Contents=[" << TemplateResult.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		VeroScript::FHostTypeId ActorId;
		if (!Expect(
		        Registry.RegisterType("AActor", VeroScript::EHostTypeKind::Class, ActorId, ErrorId) && Registry.SetTypeInheritable(ActorId, true, ErrorId),
		        "register inheritable AActor for hover"))
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
		if (!Expect(Registry.RegisterMember(ActorId, GetValue, ErrorId), "AActor.get_value hover"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* InheritSource = R"(
class MyActor : AActor {
public:
	MyActor() {}
}
void foo(MyActor a) {
	a.get_value;
}
)";
		FTextPosition Position;
		if (!Expect(FindPosition(InheritSource, "a.get_value", Position), "locate a.get_value hover"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("a.").size());
		FHoverOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;
		const FHoverResult Result = VeroScriptHover::HoverAtSource(
		    InheritSource, Position, "<hover-host-inherit>", Options);
		if (!Expect(
		        Result.bSuccess && Contains(Result.Contents, "get_value"),
		        "hover inherited host method a.get_value"))
		{
			std::cerr << "Contents=[" << Result.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* Annotated = R"(
UCLASS()
class CombatActor {
public:
	UPROPERTY(EditAnywhere, __veroscript, Category="Combat")
	int Amount = 0;
	@rpc(reliable)
	void ApplyDamage(int Delta) { Amount = Amount + Delta; }
}
void main() { print(1); }
)";
		FTextPosition ClassPos;
		if (!Expect(FindPosition(Annotated, "CombatActor", ClassPos), "locate CombatActor hover"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult ClassHover =
		    VeroScriptHover::HoverAtSource(Annotated, ClassPos, "<hover-decl-meta-class>");
		if (!Expect(
		        ClassHover.bSuccess && Contains(ClassHover.Contents, "[UCLASS()]"),
		        "hover class shows UCLASS DeclMeta"))
		{
			std::cerr << "Contents=[" << ClassHover.Contents << "]\n";
			return EXIT_FAILURE;
		}

		FTextPosition FieldPos;
		if (!Expect(FindPosition(Annotated, "Amount", FieldPos), "locate Amount hover"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult FieldHover =
		    VeroScriptHover::HoverAtSource(Annotated, FieldPos, "<hover-decl-meta-field>");
		if (!Expect(
		        FieldHover.bSuccess && Contains(FieldHover.Contents, "[UPROPERTY(") && Contains(FieldHover.Contents, "EditAnywhere") && !Contains(FieldHover.Contents, "__veroscript"),
		        "hover field shows UPROPERTY DeclMeta"))
		{
			std::cerr << "Contents=[" << FieldHover.Contents << "]\n";
			return EXIT_FAILURE;
		}

		FTextPosition MethodPos;
		if (!Expect(FindPosition(Annotated, "ApplyDamage", MethodPos), "locate ApplyDamage hover"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult MethodHover =
		    VeroScriptHover::HoverAtSource(Annotated, MethodPos, "<hover-decl-meta-method>");
		if (!Expect(
		        MethodHover.bSuccess && Contains(MethodHover.Contents, "[@rpc(reliable)]"),
		        "hover method shows @rpc DeclMeta"))
		{
			std::cerr << "Contents=[" << MethodHover.Contents << "]\n";
			return EXIT_FAILURE;
		}

		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		if (!Expect(
		        Registry.RegisterAnnotation("@rpc", "annotation (reliable)", ErrorId),
		        "RegisterAnnotation @rpc for hover"))
		{
			return EXIT_FAILURE;
		}
		FTextPosition RpcPos;
		if (!Expect(FindPosition(Annotated, "@rpc(reliable)", RpcPos), "locate @rpc token"))
		{
			return EXIT_FAILURE;
		}
		RpcPos.Column += static_cast<std::uint32_t>(std::string_view("@rp").size());
		FHoverOptions RpcOptions;
		RpcOptions.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		RpcOptions.Bindings = &Registry;
		const FHoverResult RpcHover =
		    VeroScriptHover::HoverAtSource(Annotated, RpcPos, "<hover-at-rpc>", RpcOptions);
		if (!Expect(
		        RpcHover.bSuccess && Contains(RpcHover.Contents, "@rpc") && Contains(RpcHover.Contents, "annotation"),
		        "hover on @rpc token resolves host annotation"))
		{
			std::cerr << "Contents=[" << RpcHover.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		// enum class: type/qualified/declaration hover work; bare use stays unresolved.
		constexpr const char* EnumClassSource = R"(
enum class Color {
	Red,
	UMETA(DisplayName="Green Light")
	Green
}
void main() {
	Color c = Color.Green;
	Color d = Color::Green;
	Green;
}
)";
		FTextPosition TypePos;
		if (!Expect(FindPosition(EnumClassSource, "enum class Color", TypePos), "locate enum class"))
		{
			return EXIT_FAILURE;
		}
		TypePos.Column += static_cast<std::uint32_t>(std::string_view("enum class ").size());
		const FHoverResult TypeHover =
		    VeroScriptHover::HoverAtSource(EnumClassSource, TypePos, "<hover-enum-class>");
		if (!Expect(
		        TypeHover.bSuccess && Contains(TypeHover.Contents, "enum class"),
		        "hover Color shows enum class kind"))
		{
			std::cerr << "Contents=[" << TypeHover.Contents << "]\n";
			return EXIT_FAILURE;
		}

		FTextPosition QualifiedPos;
		if (!Expect(FindPosition(EnumClassSource, "Color.Green", QualifiedPos), "locate Color.Green"))
		{
			return EXIT_FAILURE;
		}
		QualifiedPos.Column += static_cast<std::uint32_t>(std::string_view("Color.").size());
		const FHoverResult QualifiedHover =
		    VeroScriptHover::HoverAtSource(EnumClassSource, QualifiedPos, "<hover-qualified-enum-class>");
		if (!Expect(
		        QualifiedHover.bSuccess && Contains(QualifiedHover.Contents, "enum member Color.Green") && Contains(QualifiedHover.Contents, "[UMETA(DisplayName=Green Light)]"),
		        "qualified Green hover includes container and UMETA"))
		{
			std::cerr << "Contents=[" << QualifiedHover.Contents << "]\n";
			return EXIT_FAILURE;
		}

		FTextPosition ScopePos;
		if (!Expect(FindPosition(EnumClassSource, "Color::Green", ScopePos), "locate Color::Green hover"))
		{
			return EXIT_FAILURE;
		}
		ScopePos.Column += static_cast<std::uint32_t>(std::string_view("Color::").size());
		const FHoverResult ScopeHover =
		    VeroScriptHover::HoverAtSource(EnumClassSource, ScopePos, "<hover-scope-enum-class>");
		if (!Expect(
		        ScopeHover.bSuccess && Contains(ScopeHover.Contents, "enum member Color.Green") && Contains(ScopeHover.Contents, "[UMETA(DisplayName=Green Light)]"),
		        ":: qualified Green hover matches ."))
		{
			std::cerr << "Contents=[" << ScopeHover.Contents << "]\n";
			return EXIT_FAILURE;
		}

		FTextPosition DeclPos;
		if (!Expect(FindPosition(EnumClassSource, "\n\tGreen\n", DeclPos), "locate declared Green"))
		{
			return EXIT_FAILURE;
		}
		DeclPos.Line += 1;
		DeclPos.Column = 2;
		const FHoverResult DeclHover =
		    VeroScriptHover::HoverAtSource(EnumClassSource, DeclPos, "<hover-declared-enum-class>");
		if (!Expect(
		        DeclHover.bSuccess && Contains(DeclHover.Contents, "enum member Color.Green") && Contains(DeclHover.Contents, "[UMETA(DisplayName=Green Light)]"),
		        "declaration Green hover resolves scoped member"))
		{
			std::cerr << "Contents=[" << DeclHover.Contents << "]\n";
			return EXIT_FAILURE;
		}

		FTextPosition DeclEndPos = DeclPos;
		DeclEndPos.Column += static_cast<std::uint32_t>(std::string_view("Green").size());
		const FHoverResult DeclEndHover =
		    VeroScriptHover::HoverAtSource(EnumClassSource, DeclEndPos, "<hover-declared-end-enum-class>");
		if (!Expect(
		        DeclEndHover.bSuccess && Contains(DeclEndHover.Contents, "enum member Color.Green"),
		        "caret after declared Green still resolves"))
		{
			std::cerr << "Contents=[" << DeclEndHover.Contents << "]\n";
			return EXIT_FAILURE;
		}

		FTextPosition BarePos;
		if (!Expect(FindPosition(EnumClassSource, "\tGreen;", BarePos), "locate bare Green"))
		{
			return EXIT_FAILURE;
		}
		BarePos.Column += 1; // past tab
		const FHoverResult BareHover =
		    VeroScriptHover::HoverAtSource(EnumClassSource, BarePos, "<hover-bare-enum-class>");
		bool bResolvedMember = false;
		for (const FHoverHit& Hit : BareHover.Definitions)
		{
			if (Hit.Kind == EOutlineSymbolKind::EnumMember && Hit.Name == "Green")
			{
				bResolvedMember = true;
			}
		}
		if (!Expect(!bResolvedMember, "bare Green must not resolve enum class member"))
		{
			std::cerr << "Contents=[" << BareHover.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* NamespaceSource = R"(
namespace Library { int value = 1; }
using namespace Library;
)";
		FTextPosition Position;
		if (!Expect(FindPosition(NamespaceSource, "Library;", Position),
		        "locate using namespace hover target"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult Hover = VeroScriptHover::HoverAtSource(
		    NamespaceSource, Position, "<hover-using-namespace>");
		if (!Expect(
		        Hover.bSuccess && Contains(Hover.Contents, "Library") && Contains(Hover.Contents, "namespace") && !Hover.Definitions.empty() && Hover.Definitions[0].DefinitionRange.StartLine == 2,
		        "using namespace target hover resolves its namespace declaration"))
		{
			std::cerr << "Contents=[" << Hover.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* GenericDefaults = R"(
template<T, U = int>
class PairBox { public: T first; U second; }
void main() { PairBox<string> value = PairBox<string>(); }
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(GenericDefaults, "PairBox<string>", Position),
		        "locate generic type with a default argument"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult Hover = VeroScriptHover::HoverAtSource(
		    GenericDefaults, Position, "<hover-generic-defaults>");
		if (!Expect(
		        Hover.bSuccess && Contains(Hover.Contents, "PairBox<T, U = int>"),
		        "generic type hover should display default type arguments"))
		{
			std::cerr << "Token=[" << Hover.TokenText << "] Contents=["
			          << Hover.Contents << "]\n";
			for (const FDiagnostic& Diagnostic : Hover.Diagnostics)
			{
				std::cerr << Diagnostic.Message << '\n';
			}
			return EXIT_FAILURE;
		}

		if (!Expect(
		        FindPosition(GenericDefaults, "T first", Position),
		        "locate template type parameter use"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult ParameterHover = VeroScriptHover::HoverAtSource(
		    GenericDefaults, Position, "<hover-generic-parameter>");
		if (!Expect(
		        ParameterHover.bSuccess && Contains(ParameterHover.Contents, "T") && Contains(ParameterHover.Contents, "generic type parameter") && !ParameterHover.Definitions.empty() && ParameterHover.Definitions[0].DefinitionRange.StartLine == 2,
		        "template type parameter use resolves to its declaration"))
		{
			std::cerr << "Token=[" << ParameterHover.TokenText << "] Contents=["
			          << ParameterHover.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* StandardNamespaceSource = R"(
void main() {
    std::array<int> values = {1, 2};
    std::print(values);
    std::strings::contains("abc", "b");
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(StandardNamespaceSource, "std::array", Position),
		        "locate standard namespace hover"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult NamespaceHover = VeroScriptHover::HoverAtSource(
		    StandardNamespaceSource, Position, "<hover-standard-namespace>");
		if (!Expect(
		        NamespaceHover.bSuccess && Contains(NamespaceHover.Contents, "namespace std") && !Contains(NamespaceHover.Contents, "import std"),
		        "standard namespace hover should use namespace semantics"))
		{
			std::cerr << "Contents=[" << NamespaceHover.Contents << "]\n";
			return EXIT_FAILURE;
		}

		if (!Expect(
		        FindPosition(StandardNamespaceSource, "array<int>", Position),
		        "locate standard type alias hover"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult TypeHover = VeroScriptHover::HoverAtSource(
		    StandardNamespaceSource, Position, "<hover-standard-type>");
		if (!Expect(
		        TypeHover.bSuccess && Contains(TypeHover.Contents, "array") && Contains(TypeHover.Contents, "standard"),
		        "standard type alias should be visible in hover"))
		{
			std::cerr << "Contents=[" << TypeHover.Contents << "]\n";
			return EXIT_FAILURE;
		}

		if (!Expect(
		        FindPosition(StandardNamespaceSource, "print(values)", Position),
		        "locate standard function alias hover"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult FunctionHover = VeroScriptHover::HoverAtSource(
		    StandardNamespaceSource, Position, "<hover-standard-function>");
		if (!Expect(
		        FunctionHover.bSuccess && Contains(FunctionHover.Contents, "print") && FunctionHover.Definitions.size() == 1 && FunctionHover.Definitions.front().Detail == "void std::print(any value);\nvoid std::print(string fmt, ...);" && Contains(FunctionHover.Contents, "soft builtin"),
		        "standard function alias should retain builtin hover information"))
		{
			std::cerr << "Contents=[" << FunctionHover.Contents << "]\n";
			return EXIT_FAILURE;
		}

		if (!Expect(
		        FindPosition(StandardNamespaceSource, "contains(\"abc\"", Position),
		        "locate nested standard function alias hover"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult NestedFunctionHover = VeroScriptHover::HoverAtSource(
		    StandardNamespaceSource,
		    Position,
		    "<hover-nested-standard-function>");
		if (!Expect(
		        NestedFunctionHover.bSuccess && Contains(NestedFunctionHover.Contents, "contains") && Contains(NestedFunctionHover.Contents, "bool std::strings::contains(string value, string part);"),
		        "nested standard function alias should be visible in hover"))
		{
			std::cerr << "Contents=[" << NestedFunctionHover.Contents << "]\n";
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
void main() {
    Counter counter();
    counter.grow(2);
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(ExtensionMethodSource, "grow(2)", Position),
		        "locate extension-method hover"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult Hover = VeroScriptHover::HoverAtSource(
		    ExtensionMethodSource,
		    Position,
		    "<hover-extension-method>");
		if (!Expect(
		        Hover.bSuccess && Contains(Hover.Contents, "grow") && Contains(Hover.Contents, "extension"),
		        "extension method should resolve to its free-function declaration"))
		{
			std::cerr << "Contents=[" << Hover.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* NamespacedExtensionSource = R"(
struct Counter { int value = 0; };
namespace Visible { int grow(Counter self, int amount) { return amount; } }
namespace Hidden { string grow(Counter self, int amount) { return "hidden"; } }
using namespace Visible;
void main() {
    Counter counter();
    counter.grow(2);
}
)";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(NamespacedExtensionSource, "grow(2)", Position),
		        "locate namespaced extension hover"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult Hover = VeroScriptHover::HoverAtSource(
		    NamespacedExtensionSource,
		    Position,
		    "<hover-namespaced-extension>");
		if (!Expect(
		        Hover.bSuccess && Contains(Hover.Contents, "int grow") && !Contains(Hover.Contents, "string grow"),
		        "extension hover should resolve only the visible namespace"))
		{
			std::cerr << "Contents=[" << Hover.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		// Each segment of a qualified standard path is a distinct first-class
		// namespace/function hover target.  In particular, hover ranges must not
		// swallow the rest of `std::chrono::now`.
		constexpr const char* NamespaceSource = R"(
/// User namespace documentation.
namespace A {
    /// Nested namespace documentation.
    namespace B {
        void Run() {}
    }
}
void main() {
    std::chrono::now();
    A::B::Run();
}
)";
		FTextPosition StandardPosition;
		if (!Expect(
		        FindPosition(NamespaceSource, "std::chrono::now", StandardPosition),
		        "locate qualified standard namespace hover"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult StdHover = VeroScriptHover::HoverAtSource(
		    NamespaceSource, StandardPosition, "<hover-standard-namespace>");
		FTextPosition ChronoPosition = StandardPosition;
		ChronoPosition.Column += static_cast<std::uint32_t>(std::string_view("std::").size());
		const FHoverResult ChronoHover = VeroScriptHover::HoverAtSource(
		    NamespaceSource, ChronoPosition, "<hover-standard-namespace>");
		FTextPosition NowPosition = ChronoPosition;
		NowPosition.Column += static_cast<std::uint32_t>(std::string_view("chrono::").size());
		const FHoverResult NowHover = VeroScriptHover::HoverAtSource(
		    NamespaceSource, NowPosition, "<hover-standard-namespace>");
		if (!Expect(
		        StdHover.bSuccess && StdHover.Definitions.size() == 1 && StdHover.Definitions.front().Kind == EOutlineSymbolKind::Namespace && Contains(StdHover.Contents, "namespace std") && StdHover.TokenRange.StartColumn == StandardPosition.Column && StdHover.TokenRange.EndColumn == StandardPosition.Column + 3 && ChronoHover.bSuccess && ChronoHover.Definitions.size() == 1 && ChronoHover.Definitions.front().Kind == EOutlineSymbolKind::Namespace && Contains(ChronoHover.Contents, "namespace std::chrono") && Contains(ChronoHover.Contents, "now") && ChronoHover.TokenRange.StartColumn == ChronoPosition.Column && ChronoHover.TokenRange.EndColumn == ChronoPosition.Column + 6 && NowHover.bSuccess && Contains(NowHover.Contents, "std::chrono::now") && NowHover.TokenRange.StartColumn == NowPosition.Column && NowHover.TokenRange.EndColumn == NowPosition.Column + 3,
		        "standard namespace hover resolves each path segment with an exact range"))
		{
			std::cerr << "std=[" << StdHover.Contents << "] chrono=["
			          << ChronoHover.Contents << "] now=[" << NowHover.Contents << "]\n";
			return EXIT_FAILURE;
		}

		FTextPosition UserPosition;
		if (!Expect(
		        FindPosition(NamespaceSource, "A::B::Run", UserPosition),
		        "locate user namespace hover"))
		{
			return EXIT_FAILURE;
		}
		UserPosition.Column += static_cast<std::uint32_t>(std::string_view("A::").size());
		const FHoverResult UserHover = VeroScriptHover::HoverAtSource(
		    NamespaceSource, UserPosition, "<hover-user-namespace>");
		if (!Expect(
		        UserHover.bSuccess && UserHover.Definitions.size() == 1 && UserHover.Definitions.front().Kind == EOutlineSymbolKind::Namespace && Contains(UserHover.Contents, "namespace A::B") && Contains(UserHover.Contents, "Nested namespace documentation."),
		        "user namespace hover includes its declaration documentation"))
		{
			std::cerr << "user namespace hover=[" << UserHover.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		// Namespace metadata is locale-aware and always has an English fallback;
		// a missing catalog entry must never turn Hover into an empty response.
		constexpr const char* LocaleSource = "void main() { std::chrono::now(); }\n";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(LocaleSource, "std::chrono", Position),
		        "locate localized namespace hover"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("std::").size());
		(void)VeroScript::Locale::TrySetLanguageFromTag("zh-CN");
		const FHoverResult Chinese = VeroScriptHover::HoverAtSource(
		    LocaleSource, Position, "<hover-localized-namespace>");
		(void)VeroScript::Locale::TrySetLanguageFromTag("fr-FR");
		const FHoverResult Fallback = VeroScriptHover::HoverAtSource(
		    LocaleSource, Position, "<hover-localized-namespace>");
		(void)VeroScript::Locale::TrySetLanguageFromTag("en-US");
		if (!Expect(
		        Chinese.bSuccess && Contains(Chinese.Contents, "时间相关标准库入口。") && Fallback.bSuccess && Contains(Fallback.Contents, "Time-related standard-library entry point."),
		        "namespace hover localizes and falls back to English"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Built-in types use catalog entries rather than hard-coded English
		// hover text. Keep every shipped tooling locale covered because this result
		// is exposed both directly and through the LSP adapter.
		constexpr const char* BuiltinLocaleSource =
		    "void main() { priority_queue<int> values(); }\n";
		struct FLocaleExpectation
		{
			const char* Tag;
			const char* Detail;
			const char* Documentation;
		};
		constexpr FLocaleExpectation LocaleExpectations[] = {
		    {"en-US", "builtin type", "Built-in VeroScript type."},
		    {"fr-FR", "type intégré", "Type VeroScript intégré."},
		    {"ja-JP", "組み込み型", "VeroScript の組み込み型。"},
		    {"ko-KR", "기본 제공 형식", "VeroScript 기본 제공 형식입니다."},
		    {"zh-CN", "内置类型", "VeroScript 内置类型。"},
		    {"zh-TW", "內建型別", "VeroScript 內建型別。"},
		};

		FTextPosition Position;
		if (!Expect(
		        FindPosition(BuiltinLocaleSource, "priority_queue", Position),
		        "locate localized builtin type hover"))
		{
			return EXIT_FAILURE;
		}
		FTextPosition PrimitivePosition;
		if (!Expect(
		        FindPosition(BuiltinLocaleSource, "int> values", PrimitivePosition),
		        "locate localized primitive type hover"))
		{
			return EXIT_FAILURE;
		}

		bool bAllLocalesLocalized = true;
		for (const FLocaleExpectation& Expected : LocaleExpectations)
		{
			const bool bSelected = VeroScript::Locale::TrySetLanguageFromTag(Expected.Tag);
			const FHoverResult Result = VeroScriptHover::HoverAtSource(
			    BuiltinLocaleSource, Position, "<hover-localized-builtin-type>");
			const FHoverResult PrimitiveResult = VeroScriptHover::HoverAtSource(
			    BuiltinLocaleSource, PrimitivePosition, "<hover-localized-builtin-type>");
			bAllLocalesLocalized = bAllLocalesLocalized && bSelected && Result.bSuccess && !Result.Definitions.empty() && Result.Definitions.front().Kind == EOutlineSymbolKind::TypeAlias && Contains(Result.Contents, "type alias priority_queue") && Contains(Result.Contents, Expected.Detail) && Contains(Result.Contents, Expected.Documentation) && PrimitiveResult.bSuccess && !PrimitiveResult.Definitions.empty() && PrimitiveResult.Definitions.front().Kind == EOutlineSymbolKind::Keyword && Contains(PrimitiveResult.Contents, "keyword int") && Contains(PrimitiveResult.Contents, Expected.Detail) && Contains(PrimitiveResult.Contents, Expected.Documentation);
		}
		(void)VeroScript::Locale::TrySetLanguageFromTag("en-US");
		if (!Expect(
		        bAllLocalesLocalized,
		        "builtin type hover localizes for every shipped tooling locale"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// The trailing separator is a normal in-progress edit. Hover must resolve
		// the confirmed namespace segment from the live text rather than returning
		// a stale result or failing the whole document query.
		constexpr const char* IncompleteSource = "void main() { std::chrono:: }\n";
		FTextPosition Position;
		if (!Expect(
		        FindPosition(IncompleteSource, "std::chrono", Position),
		        "locate incomplete namespace hover"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("std::").size());
		const FHoverResult Hover = VeroScriptHover::HoverAtSource(
		    IncompleteSource, Position, "<hover-incomplete-namespace>");
		if (!Expect(
		        Hover.bSuccess && Contains(Hover.Contents, "namespace std::chrono") && Hover.TokenRange.StartColumn == Position.Column && Hover.TokenRange.EndColumn == Position.Column + 6,
		        "unfinished namespace Hover remains exact and stable"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* NewNamespaceSource =
		    "void main() { std::console::write(\"ready\"); std::process::exit(0); }\n";
		FTextPosition ConsolePosition;
		if (!Expect(
		        FindPosition(NewNamespaceSource, "std::console", ConsolePosition),
		        "locate console namespace hover"))
		{
			return EXIT_FAILURE;
		}
		ConsolePosition.Column += static_cast<std::uint32_t>(std::string_view("std::").size());
		const FHoverResult ConsoleHover = VeroScriptHover::HoverAtSource(
		    NewNamespaceSource, ConsolePosition, "<hover-standard-console>");
		FTextPosition WritePosition;
		if (!Expect(
		        FindPosition(NewNamespaceSource, "write(\"ready\")", WritePosition),
		        "locate console write hover"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult WriteHover = VeroScriptHover::HoverAtSource(
		    NewNamespaceSource, WritePosition, "<hover-standard-console>");
		if (!Expect(
		        ConsoleHover.bSuccess && Contains(ConsoleHover.Contents, "namespace std::console") && Contains(ConsoleHover.Contents, "Console output") && WriteHover.bSuccess && Contains(WriteHover.Contents, "void std::console::write(any value)"),
		        "new runtime namespaces provide metadata-backed Hover and signatures"))
		{
			std::cerr << "console=[" << ConsoleHover.Contents << "] write=["
			          << WriteHover.Contents << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		const std::string MacroSource =
		    "import \"library.vs\"; int main(){print(PUBLIC_VALUE);return 0;}";
		FHoverOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.SourceOverlays = {
		    {"macro-hover/library.vs", "/// Public constant.\n#define_export PUBLIC_VALUE 42\n"}};
		FTextPosition Position;
		if (!FindPosition(MacroSource, "PUBLIC_VALUE", Position))
		{
			return EXIT_FAILURE;
		}

		const auto Result =
		    VeroScriptHover::HoverAtSource(MacroSource, Position, "macro-hover/main.vs", Options);
		if (!Expect(Result.bSuccess && Contains(Result.Contents, "42") &&
		            Contains(Result.Contents, "#define_export PUBLIC_VALUE 42") &&
		            !Result.Definitions.empty() &&
		            Result.Definitions.front().bExported &&
		            Result.Definitions.front().FileLabel == "macro-hover/library.vs",
		        "imported macro hover uses original definition and unsaved overlay"))
		{
			std::cerr << Result.Contents << '\n';
			return EXIT_FAILURE;
		}
		Options.ImplicitImportPaths = {"macro-hover/library.vs"};
		const std::string ImplicitSource = "int main(){return PUBLIC_VALUE;}";
		if (!FindPosition(ImplicitSource, "PUBLIC_VALUE", Position)) return EXIT_FAILURE;
		const auto Implicit = VeroScriptHover::HoverAtSource(
		    ImplicitSource, Position, "macro-hover/main.vs", Options);
		if (!Expect(Implicit.bSuccess && Contains(Implicit.Contents, "#define_export PUBLIC_VALUE 42") &&
		            !Implicit.Definitions.empty() && Implicit.Definitions.front().FileLabel == "macro-hover/library.vs",
		        "implicit macro hover keeps the original definition without a source import"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::string Inner = "class Inner{public:const int value;Inner():value(1){}int get(){return value;}};";
		const std::vector<std::string> Sources = {
		    Inner + "class Outer{public:Inner child;};Outer make(){return Outer();}"
		            "void use(){print(make().child.value);}void main(){}",
		    Inner + "class Outer{public:Inner child;};void use(array<Outer> x){print(x[0].child.value);}void main(){}",
		    Inner + "class Outer<T>{public:T child;};void use(Outer<Inner> x){print(x.child.value);}void main(){}",
		    Inner + "class Base{public:Inner child;};class Outer:public Base{};"
		            "void use(Outer x){print(x.child.value);}void main(){}",
		    Inner + "class Outer{public:Inner child;int get(){return this.child.value;}};void main(){}",
		    Inner + "class Outer{public:Inner child;int get(){return child.value;}};void main(){}",
		    Inner + "class Outer{public:Inner child;Inner read(){return child;}int get(){return read().value;}};void main(){}",
		    Inner + "void use(){using Local=Inner;print(Local().value);}void main(){}",
		    Inner + "void use(fn()->Inner factory){print(factory().value);}void main(){}",
		    Inner + "void use(fn()->const Inner& factory){print(factory().value);}void main(){}",
		    Inner + "void use(fn()->fn()->const Inner& factory){print(factory()().value);}void main(){}",
		    Inner + "T make<T>(T value){return value;}void main(){using Local=Inner;print(make<Local>(Local()).value);}",
		    Inner + "T make<T>(T value){return value;}void main(){using Local=Inner;print(make<handle<Local>>(handle<Local>()).value.value);}",
		    Inner + "Inner& borrow(Inner& x){return x;}void use(Inner& x){print(borrow(ref x).value);}void main(){}",
		    Inner + "const Inner& borrow(const Inner& x){return x;}void use(const Inner& x){print(borrow(ref x).value);}void main(){}",
		    Inner + "void use(fn()->fn()->Inner factory){print(factory()().value);}void main(){}",
		    Inner + "class Outer{public:fn()->Inner factory;};void use(Outer x){print((x.factory)().value);}void main(){}",
		    Inner + "using Result=Inner;class Wrong{public:string wrong;};"
		            "void use(fn()->Result factory){using Result=Wrong;print(factory().value);}void main(){}",
		};
		for (const auto& ChainSource : Sources)
		{
			const auto Compiled = VeroScript::VeroScriptCompile::CompileSource(ChainSource, "receiver-chain.vs");
			if (!Expect(Compiled.bSuccess, "receiver chain fixture is valid core source"))
			{
				std::cerr << ChainSource << '\n'
				          << Compiled.Error.Message << '\n';
				return EXIT_FAILURE;
			}
			const std::size_t Offset = ChainSource.rfind(".value");
			const auto Hover = VeroScriptHover::HoverAtSource(ChainSource,
			    {1, static_cast<std::uint32_t>(Offset + 2)}, "receiver-chain.vs");
			if (!Expect(Hover.bSuccess && Contains(Hover.Contents, "const int") && Hover.Definitions.size() == 1 && Hover.Definitions.front().Name == "value" && Hover.Definitions.front().ContainerName == "Inner",
			        "valid receiver chains preserve the actual readonly field identity"))
			{
				std::cerr << ChainSource << '\n'
				          << Hover.Contents << '\n';
				return EXIT_FAILURE;
			}
		}
	}

	{
		const std::string Inner = "export class Inner{public:const int value;Inner():value(1){}};";
		const std::vector<std::pair<std::string, std::string>> Cases = {
		    {Inner + "using Result=Inner;export Result make(){return Inner();}",
		        "void main(){print(make().value);}"},
		    {Inner + "using Result=Inner;export class Outer{public:Result child;};",
		        "void use(Outer x){print(x.child.value);}void main(){}"},
		    {Inner + "using Result=Inner;export class Outer{public:Result get(){return Inner();}};",
		        "void use(Outer x){print(x.get().value);}void main(){}"},
		    {"using Inner=string;export T make<T>(T value){return value;}",
		        "class Inner{public:const int value;Inner():value(1){}};void main(){print(make<Inner>(Inner()).value);}"},
		    {"class Local{public:string wrong;};export void unused(){}",
		        "class Inner{public:const int value;Inner():value(1){}};using Local=Inner;void main(){print(Local().value);}"},
		};
		for (const auto& [Library, Main] : Cases)
		{
			const auto CaseSource = "import \"library.vs\";" + Main;
			VeroScript::FCompileOptions CompileOptions;
			VeroScript::Test::Configure(CompileOptions);
			CompileOptions.SourceOverlays = {{"alias-chain/library.vs", Library}};
			const auto Compiled = VeroScript::VeroScriptCompile::CompileSource(CaseSource, "alias-chain/main.vs", CompileOptions);
			if (!Expect(Compiled.bSuccess, "cross-module receiver fixture compiles"))
			{
				std::cerr << CaseSource << '\n'
				          << Compiled.Error.Message << '\n';
				return EXIT_FAILURE;
			}
			FHoverOptions Options;
			Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
			Options.SourceOverlays = {{"alias-chain/library.vs", Library}};
			const auto Hover = VeroScriptHover::HoverAtSource(CaseSource,
			    {1, static_cast<std::uint32_t>(CaseSource.rfind(".value") + 2)}, "alias-chain/main.vs", Options);
			if (!Expect(Hover.bSuccess && Contains(Hover.Contents, "const int") && Hover.Definitions.size() == 1 && Hover.Definitions.front().ContainerName == "Inner",
			        "cross-module receivers retain declaration scope and caller generic arguments"))
			{
				std::cerr << CaseSource << '\n'
				          << Hover.Contents << '\n';
				return EXIT_FAILURE;
			}
		}
	}

	{
		const std::string ShadowSource = "class Inner{public:int value;};class Base{public:Inner child;};"
		                                 "class Outer:public Base{private:string child;};void use(Outer x){print(x.child.value);}void main(){}";
		const auto Compiled = VeroScript::VeroScriptCompile::CompileSource(ShadowSource, "private-shadow.vs");
		const auto Hover = VeroScriptHover::HoverAtSource(ShadowSource,
		    {1, static_cast<std::uint32_t>(ShadowSource.rfind(".value") + 2)}, "private-shadow.vs");
		if (!Expect(!Compiled.bSuccess && Hover.Definitions.empty(),
		        "inaccessible closer declaration cannot expose a hidden base receiver"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const auto Root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
		const std::string Stdlib = (Root / "VeroScript/Resources/Scripts/stdlib").string();
		FHoverOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.StdlibSearchPaths = {Stdlib};
		const std::string AttributeSource = "import \"attribute.vs\";class Model{public:int read(){return 3;}};void main(){Model m=Model();auto a=std::make_attribute_by_name<int>(m,\"read\");print(a.get());}";
		FTextPosition Position;
		FindPosition(AttributeSource, "get()", Position);
		const auto Hover = VeroScriptHover::HoverAtSource(AttributeSource, Position, "attribute-hover.vs", Options);
		if (!Expect(Hover.bSuccess && Contains(Hover.Contents, "int") && !Hover.Definitions.empty(),
		        "attribute method hover infers the generated factory result"))
		{
			std::cerr << Hover.Contents << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FHostBindingRegistry Registry;
		std::string Error;
		for (const auto Type : {VeroScript::ENativeType::Int, VeroScript::ENativeType::String})
		{
			VeroScript::FHostFunctionDesc F;
			F.Name = "host_overload";
			F.ReturnType = Type;
			F.ParamTypes = {Type};
			F.Documentation = Type == VeroScript::ENativeType::Int ? "Integer host overload." : "String host overload.";
			if (!Registry.RegisterNative(F, [](const VeroScript::FNativeCallContext& C, VeroScript::FValue& R, std::string&)
			{
				R = C.Args[0];
				return true;
			}, nullptr, Error))
			{
				return Fail("register hover overloads");
			}
		}
		const std::string Source = "void main(){auto a=host_overload(1);auto b=host_overload(\"x\");}";
		FTextPosition Position;
		if (!FindPosition(Source, "host_overload(1)", Position)) return Fail("locate host overload");
		FHoverOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;
		const auto Hover = VeroScriptHover::HoverAtSource(Source, Position, "<host-overloads>", Options);
		if (!Expect(Hover.bSuccess && Contains(Hover.Contents, "Integer host overload.") && Contains(Hover.Contents, "String host overload."), "hover preserves every host overload"))
		{
			std::cerr << Hover.Contents;
			return EXIT_FAILURE;
		}
	}
	{
		const std::string Source =
		    "class A{public:struct Item{int value;};};void main(){struct Local{int value;};Local item=Local();}";
		for (const auto& Needle : {"Item{", "Local{"})
		{
			FTextPosition Position;
			if (!FindPosition(Source, Needle, Position))
			{
				return EXIT_FAILURE;
			}
			const auto Hover = VeroScriptHover::HoverAtSource(Source, Position, "scoped-hover.vs");
			if (!Expect(Hover.bSuccess && Hover.Definitions.size() == 1 &&
			            Hover.Definitions.front().Kind == EOutlineSymbolKind::Struct,
			        "nested and local type declarations resolve to their actual type") ||
			    !Expect(Hover.Contents.find("$arity") == std::string::npos &&
			            Hover.Contents.find("$local") == std::string::npos,
			        "type hover hides private linkage markers"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		const std::string Source = "class A{public:using Value=int;typedef Other=string;};void main(){A::Value value=7;A::Other text=\"ok\";}";
		for (const auto& Needle : {"Value=int", "Other=string"})
		{
			FTextPosition Position;
			if (!FindPosition(Source, Needle, Position)) return EXIT_FAILURE;
			const auto Hover = VeroScriptHover::HoverAtSource(Source, Position, "scoped-alias-hover.vs");
			if (!Expect(Hover.bSuccess && Hover.Definitions.size() == 1 &&
			            Hover.Definitions.front().Kind == EOutlineSymbolKind::TypeAlias,
			        "nested using and typedef declarations resolve at their source names"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	for (const auto* Invocation : {"box.echo(item)", "box.echo<Payload>(item)"})
	{
		const std::string ChainSource = "struct Payload{int value=7;};class Box{public:template<typename T> T echo(T value){return value;}};"
		                                "void main(){Box box=Box();Payload item=Payload();print(" +
		    std::string(Invocation) + ".value);}";
		FTextPosition Position;
		if (!FindPosition(ChainSource, ".value)", Position)) return EXIT_FAILURE;
		++Position.Column;
		const auto Hover = VeroScriptHover::HoverAtSource(ChainSource, Position, "template-chain-hover.vs");
		if (!Expect(Hover.bSuccess && Hover.Definitions.size() == 1 && Contains(Hover.Contents, "int"),
		        "template method result participates in chained member queries"))
		{
			std::cerr << Hover.Contents << '\n';
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}
