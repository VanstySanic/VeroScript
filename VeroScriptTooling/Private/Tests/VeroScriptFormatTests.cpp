// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTooling.h"
#include "VeroScriptLexer.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
	int Fail(const char* Message)
	{
		std::cerr << "VeroScriptFormatTests: " << Message << '\n';
		return EXIT_FAILURE;
	}

	bool Expect(const bool Condition, const char* Message)
	{
		return Condition ? true : (Fail(Message), false);
	}

	bool SameTokens(const std::string_view Before, const std::string_view After)
	{
		VeroScript::VeroScriptLexer BeforeLexer(Before);
		VeroScript::VeroScriptLexer AfterLexer(After);
		const auto& Left = BeforeLexer.Tokenize();
		const auto& Right = AfterLexer.Tokenize();
		if (BeforeLexer.HasError() || AfterLexer.HasError() || Left.size() != Right.size())
		{
			return false;
		}
		for (std::size_t Index = 0; Index < Left.size(); ++Index)
		{
			if (Left[Index].Kind != Right[Index].Kind || Left[Index].Lexeme != Right[Index].Lexeme)
			{
				return false;
			}
		}
		return true;
	}
}

int main()
{
	using namespace VeroScript::Tooling;
	{
		const std::string Source = "void main(){int n=1;auto next=[n=n+1,&target=n]()mutable->int{static int calls=0;++calls;target+=calls;return ++n;};assert(next()==3);}";
		const auto Formatted = VeroScriptFormat::FormatSource(Source);
		if (!Expect(Formatted.bSuccess && SameTokens(Source, Formatted.FormattedSource) && VeroScriptAnalyzer::AnalyzeSource(Formatted.FormattedSource, "lambda-static-format.vs").bSuccess && VeroScriptFormat::FormatSource(Formatted.FormattedSource).FormattedSource == Formatted.FormattedSource,
		        "initialized captures, mutable and local statics format without changing tokens"))
		{
			return EXIT_FAILURE;
		}
	}
	for (const char* Expression : {"- -x", "+ +x", "- --x", "+ ++x", "! !x", "~ ~x"})
	{
		const std::string Source = "void main(){auto x=" + std::string(Expression[0] == '!' ? "true" : "3") + ";auto y=" + std::string(Expression) + ";print(x);print(y);}";
		const auto Formatted = VeroScriptFormat::FormatSource(Source);
		// Unary plus is an incomplete/invalid-buffer case in the current grammar;
		// formatting must still preserve its tokens. Other chains must compile.
		if (!Expect(Formatted.bSuccess && SameTokens(Source, Formatted.FormattedSource),
		        "formatting must preserve unary operator tokens") ||
		    !Expect(Expression[0] == '+' || VeroScriptAnalyzer::AnalyzeSource(Formatted.FormattedSource, "<fmt-unary-chain>").bSuccess,
		        "formatted unary chains compile") ||
		    !Expect(VeroScriptFormat::FormatSource(Formatted.FormattedSource).FormattedSource == Formatted.FormattedSource, "unary chain formatting is idempotent"))
		{
			std::cerr << Source << '\n'
			          << Formatted.FormattedSource << '\n';
			return EXIT_FAILURE;
		}
	}
	for (const char* Source : {"void main(){print(1 .member);}", "void main(){int x=. 5;}"})
	{
		const auto Formatted = VeroScriptFormat::FormatSource(Source);
		if (!Expect(Formatted.bSuccess && SameTokens(Source, Formatted.FormattedSource),
		        "formatting must preserve separated numeric and dot tokens") ||
		    !Expect(VeroScriptFormat::FormatSource(Formatted.FormattedSource).FormattedSource == Formatted.FormattedSource, "numeric token formatting is idempotent"))
		{
			return EXIT_FAILURE;
		}
	}
	FFormatOptions TabOptions;
	for (const bool Closed : {false, true})
	{
		std::string Source = "void main(){";
		for (int Depth = 0; Depth < 128; ++Depth)
		{
			Source += "Box<";
		}
		Source += "int";
		if (Closed)
		{
			Source.append(128, '>');
		}
		Source += " value;}";
		const auto Formatted = VeroScriptFormat::FormatSource(Source);
		if (!Expect(Formatted.bSuccess && SameTokens(Source, Formatted.FormattedSource),
		        "deep complete and incomplete angle lists preserve their tokens") ||
		    !Expect(VeroScriptFormat::FormatSource(Formatted.FormattedSource).FormattedSource == Formatted.FormattedSource, "deep angle-list formatting is idempotent"))
		{
			return EXIT_FAILURE;
		}
	}

	TabOptions.IndentWidth = 8;
	TabOptions.bInsertSpaces = false;
	const FFormatResult TabFormatted = VeroScriptFormat::FormatSource(
	    "void main(){\nint value=1;\nif(value){\nprint(value);\n}\n}\n", TabOptions);
	if (!Expect(TabFormatted.bSuccess && TabFormatted.FormattedSource.find("\tint value = 1;") != std::string::npos && TabFormatted.FormattedSource.find("\t\tprint(value);") != std::string::npos && VeroScriptFormat::FormatSource(TabFormatted.FormattedSource, TabOptions).FormattedSource == TabFormatted.FormattedSource,
	        "tab indentation follows insertSpaces and remains stable"))
	{
		std::cerr << TabFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}

	constexpr const char* Messy = "void main(){int x=1;if(x>0){print(x);}}";
	const FFormatResult Result = VeroScriptFormat::FormatSource(Messy);
	if (!Expect(Result.bSuccess, "format should succeed"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(!Result.FormattedSource.empty(), "formatted source non-empty"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(
	        Result.FormattedSource.find("void main") != std::string::npos,
	        "keeps void main"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(
	        Result.FormattedSource.find('\n') != std::string::npos,
	        "introduces newlines"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(
	        Result.FormattedSource.find("print(") != std::string::npos,
	        "call sites should not space before '('"))
	{
		std::cerr << Result.FormattedSource << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(
	        Result.FormattedSource.find("main()") != std::string::npos,
	        "main() should not space before '('"))
	{
		std::cerr << Result.FormattedSource << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(
	        Result.FormattedSource.find("if (") != std::string::npos,
	        "control keywords should keep a space before '('"))
	{
		std::cerr << Result.FormattedSource << '\n';
		return EXIT_FAILURE;
	}

	// Re-format is stable enough to keep compiling tokens after a second pass.
	const FFormatResult Second = VeroScriptFormat::FormatSource(Result.FormattedSource);
	if (!Expect(Second.bSuccess, "second format pass"))
	{
		return EXIT_FAILURE;
	}

	const FAnalyzeResult Analyze = VeroScriptAnalyzer::AnalyzeSource(Result.FormattedSource, "<fmt>");
	if (!Expect(Analyze.bSuccess, "formatted source should still analyze clean"))
	{
		for (const FDiagnostic& Diagnostic : Analyze.Diagnostics)
		{
			std::cerr << Diagnostic.Message << '\n';
		}
		return EXIT_FAILURE;
	}

	constexpr const char* ForMessy = "void main(){for(int i=0;i<3;++i){print(i);}}";
	const FFormatResult ForFormatted = VeroScriptFormat::FormatSource(ForMessy);
	if (!Expect(ForFormatted.bSuccess, "for format should succeed"))
	{
		return EXIT_FAILURE;
	}
	const FAnalyzeResult ForAnalyze =
	    VeroScriptAnalyzer::AnalyzeSource(ForFormatted.FormattedSource, "<fmt-for>");
	if (!Expect(ForAnalyze.bSuccess, "formatted for-loop should analyze clean"))
	{
		std::cerr << ForFormatted.FormattedSource << '\n';
		for (const FDiagnostic& Diagnostic : ForAnalyze.Diagnostics)
		{
			std::cerr << Diagnostic.Message << '\n';
		}
		return EXIT_FAILURE;
	}

	const FFormatResult ExceptionFormatted = VeroScriptFormat::FormatSource(
	    "void main(){try{throw 1;}catch(const int& value){float limit=.5f;"
	    "assert(value>limit,\"caught value\");print(value);}finally{print(2);}}");
	if (!Expect(ExceptionFormatted.bSuccess, "exception format should succeed") || !Expect(ExceptionFormatted.FormattedSource.find("catch (const int& value)") != std::string::npos, "catch declarations should use control-keyword and reference spacing") || !Expect(ExceptionFormatted.FormattedSource.find("limit = .5f") != std::string::npos, "leading-dot literals should remain one formatted token") || !Expect(ExceptionFormatted.FormattedSource.find("}\n    catch (const int& value)\n    {\n") != std::string::npos && ExceptionFormatted.FormattedSource.find("}\n    finally\n    {\n") != std::string::npos, "exception clauses and their opening braces must start on aligned new lines") || !Expect(VeroScriptFormat::FormatSource(ExceptionFormatted.FormattedSource).FormattedSource == ExceptionFormatted.FormattedSource, "exception clause formatting must be idempotent") || !Expect(VeroScriptAnalyzer::AnalyzeSource(ExceptionFormatted.FormattedSource, "<fmt-exception>").bSuccess, "formatted exception source should analyze cleanly"))
	{
		std::cerr << ExceptionFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}

	// Dense input deliberately exercises normalization of joined continuation clauses.
	constexpr const char* ClauseMessy =
	    "void main(){int x=0;if(x>0){print(1);}else if(x<0){print(-1);}else{print(0);}"
	    "do{x=x+1;}while(x<2);}";
	const FFormatResult ClauseFormatted = VeroScriptFormat::FormatSource(ClauseMessy);
	if (!Expect(ClauseFormatted.bSuccess, "continuation clause format should succeed") || !Expect(SameTokens(ClauseMessy, ClauseFormatted.FormattedSource), "clause formatting must preserve source tokens") || !Expect(ClauseFormatted.FormattedSource.find("}\n    else if (x < 0)\n    {\n") != std::string::npos && ClauseFormatted.FormattedSource.find("}\n    else\n    {\n") != std::string::npos && ClauseFormatted.FormattedSource.find("}\n    while (x < 2);") != std::string::npos, "else, else-if, and trailing while must follow the brace newline standard") || !Expect(VeroScriptFormat::FormatSource(ClauseFormatted.FormattedSource).FormattedSource == ClauseFormatted.FormattedSource, "continuation clause formatting must be idempotent") || !Expect(VeroScriptAnalyzer::AnalyzeSource(ClauseFormatted.FormattedSource, "<fmt-clauses>").bSuccess, "formatted continuation clauses should analyze cleanly"))
	{
		std::cerr << ClauseFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}

	constexpr const char* CommentedClauses =
	    "void main(){int value=0;if(value>0){value=1;}/* before else */else{value=2;}"
	    "try{throw 1;}/* before catch */catch(int caught){value=caught;}"
	    "/* before finally */finally{value=value+1;}"
	    "do{value=value+1;}/* before while */while(value<3);}";
	const FFormatResult CommentedFormatted = VeroScriptFormat::FormatSource(CommentedClauses);
	if (!Expect(CommentedFormatted.bSuccess && SameTokens(CommentedClauses, CommentedFormatted.FormattedSource),
	        "formatting commented clauses must preserve tokens") ||
	    !Expect(VeroScriptFormat::FormatSource(CommentedFormatted.FormattedSource).FormattedSource == CommentedFormatted.FormattedSource, "commented clause formatting must be idempotent") || !Expect(VeroScriptAnalyzer::AnalyzeSource(CommentedFormatted.FormattedSource, "<fmt-commented-clauses>").bSuccess, "formatted commented clauses should analyze cleanly"))
	{
		std::cerr << CommentedFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}
	for (const char* Expected : {
	         "/* before else */\n    else\n    {",
	         "/* before catch */\n    catch (int caught)\n    {",
	         "/* before finally */\n    finally\n    {",
	         "/* before while */\n    while (value < 3);"})
	{
		if (!Expect(CommentedFormatted.FormattedSource.find(Expected) != std::string::npos,
		        "a block comment must not keep a continuation clause on the same line"))
		{
			std::cerr << CommentedFormatted.FormattedSource << '\n';
			return EXIT_FAILURE;
		}
	}

	constexpr const char* UnaryMessy = "void main(){int x=-1;print(x);}";
	const FFormatResult UnaryFormatted = VeroScriptFormat::FormatSource(UnaryMessy);
	if (!Expect(UnaryFormatted.bSuccess, "unary format should succeed"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(
	        UnaryFormatted.FormattedSource.find("= -1") != std::string::npos || UnaryFormatted.FormattedSource.find("=-1") != std::string::npos,
	        "preserves negative literal"))
	{
		std::cerr << UnaryFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}
	const FAnalyzeResult UnaryAnalyze =
	    VeroScriptAnalyzer::AnalyzeSource(UnaryFormatted.FormattedSource, "<fmt-unary>");
	if (!Expect(UnaryAnalyze.bSuccess, "formatted unary should analyze clean"))
	{
		std::cerr << UnaryFormatted.FormattedSource << '\n';
		for (const FDiagnostic& Diagnostic : UnaryAnalyze.Diagnostics)
		{
			std::cerr << Diagnostic.Message << '\n';
		}
		return EXIT_FAILURE;
	}

	constexpr const char* RecommendedStyleMessy =
	    "struct Point{int x=1;} ;void main(){int total=0;"
	    "for(int i=0;i<3;i=i+1){total=total+i;}print(-total);}";
	const FFormatResult RecommendedStyleFormatted =
	    VeroScriptFormat::FormatSource(RecommendedStyleMessy);
	if (!Expect(RecommendedStyleFormatted.bSuccess, "recommended style format") || !Expect(RecommendedStyleFormatted.FormattedSource.find("};") != std::string::npos, "type trailing semicolon should stay with the closing brace") || !Expect(RecommendedStyleFormatted.FormattedSource.find("i = 0; i < 3; i = i + 1") != std::string::npos, "for clauses and binary operators should use spaces") || !Expect(RecommendedStyleFormatted.FormattedSource.find("-total") != std::string::npos, "unary operators should remain attached") || !Expect(RecommendedStyleFormatted.FormattedSource.find("print(-total)") != std::string::npos, "call parentheses should remain attached"))
	{
		std::cerr << RecommendedStyleFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}

	constexpr const char* OperatorStyleMessy =
	    "class Vec{public:Vec operator+(Vec rhs){return rhs;}};"
	    "void main(){Vec a();Vec b();print(\"sum={}\",(a+b));}";
	const FFormatResult OperatorStyleFormatted =
	    VeroScriptFormat::FormatSource(OperatorStyleMessy);
	if (!Expect(OperatorStyleFormatted.bSuccess, "operator style format") || !Expect(OperatorStyleFormatted.FormattedSource.find("operator+(") != std::string::npos, "operator declarations should not contain a space before the parameter list") || !Expect(OperatorStyleFormatted.FormattedSource.find("\", (a + b)") != std::string::npos, "parenthesized arguments should keep comma and binary-operator spacing"))
	{
		std::cerr << OperatorStyleFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}

	const FFormatResult EnumStyleFormatted = VeroScriptFormat::FormatSource(
	    "enum class State:uint8{Idle=0u,Running=1u};");
	if (!Expect(EnumStyleFormatted.bSuccess, "enum style format") || !Expect(EnumStyleFormatted.FormattedSource.find("Idle = 0u,\n    Running = 1u") != std::string::npos, "enum members should occupy separate lines") || !Expect(EnumStyleFormatted.FormattedSource.find("};") != std::string::npos, "enum trailing semicolon should stay with the closing brace"))
	{
		std::cerr << EnumStyleFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}

	const FFormatResult NestedParenFormatted = VeroScriptFormat::FormatSource(
	    "void main(){for((string key,int value):pairs){print(key,value);}}");
	if (!Expect(NestedParenFormatted.bSuccess, "nested parentheses format") || !Expect(NestedParenFormatted.FormattedSource.find("for ((string key") != std::string::npos, "nested control-flow parentheses should not contain an extra inner space"))
	{
		std::cerr << NestedParenFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}

	const FFormatResult TrailingDeclaratorFormatted = VeroScriptFormat::FormatSource(
	    "struct Config{int value=1;} default_config;void main(){print(default_config.value);}");
	if (!Expect(TrailingDeclaratorFormatted.bSuccess, "trailing type declarator format") || !Expect(TrailingDeclaratorFormatted.FormattedSource.find("} default_config;") != std::string::npos, "a type's trailing variable declarator must remain on the definition line"))
	{
		std::cerr << TrailingDeclaratorFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}

	const FFormatResult CommentSpacingFormatted = VeroScriptFormat::FormatSource(
	    "struct First{};\n\n// Describes the next declaration.\nstruct Second{};\n");
	if (!Expect(CommentSpacingFormatted.bSuccess, "comment spacing format") || !Expect(CommentSpacingFormatted.FormattedSource.find("};\n\n// Describes the next declaration.\nstruct Second") != std::string::npos, "blank lines before declaration comments must not move after the comment"))
	{
		std::cerr << CommentSpacingFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}

	const FFormatResult MapInitializerFormatted = VeroScriptFormat::FormatSource(
	    "void main(){map<string,int> values={\"answer\":42};}");
	if (!Expect(MapInitializerFormatted.bSuccess, "map initializer format") || !Expect(MapInitializerFormatted.FormattedSource.find("{\"answer\": 42}") != std::string::npos, "map initializer separators should not have a leading space"))
	{
		std::cerr << MapInitializerFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}

	const FFormatResult ReferenceStyleFormatted = VeroScriptFormat::FormatSource(
	    "struct Point{};void bump(int & value){}void move(Point & point){}"
	    "void main(){int flags=3;int mask=1;int & alias=flags;"
	    "const int & readonly=flags;function()->int f=[&,mask]()->int{return alias+mask;};"
	    "print(flags&mask);}");
	if (!Expect(ReferenceStyleFormatted.bSuccess, "reference style format") || !Expect(ReferenceStyleFormatted.FormattedSource.find("int& value") != std::string::npos && ReferenceStyleFormatted.FormattedSource.find("Point& point") != std::string::npos && ReferenceStyleFormatted.FormattedSource.find("int& alias") != std::string::npos && ReferenceStyleFormatted.FormattedSource.find("const int& readonly") != std::string::npos, "reference declarators should use Type& name") || !Expect(ReferenceStyleFormatted.FormattedSource.find("flags & mask") != std::string::npos, "binary ampersand should retain spaces"))
	{
		std::cerr << ReferenceStyleFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(
	        VeroScriptAnalyzer::AnalyzeSource(
	            ReferenceStyleFormatted.FormattedSource, "<fmt-reference>")
	            .bSuccess,
	        "formatted local references and reference captures should analyze clean"))
	{
		std::cerr << ReferenceStyleFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}

	const FFormatResult LambdaArgumentFormatted = VeroScriptFormat::FormatSource(
	    "void apply(function(int)->int f,int value){}"
	    "void main(){apply(function(int x)->int{return x+1;},41);}");
	if (!Expect(LambdaArgumentFormatted.bSuccess, "lambda argument format") || !Expect(LambdaArgumentFormatted.FormattedSource.find("}, 41)") != std::string::npos, "a block lambda must stay attached to the following argument comma"))
	{
		std::cerr << LambdaArgumentFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}

	constexpr const char* CommentMessy =
	    "void main(){// keep me\nint x=1;/* block */print(x);}";
	const FFormatResult CommentFormatted = VeroScriptFormat::FormatSource(CommentMessy);
	if (!Expect(CommentFormatted.bSuccess, "comment format should succeed"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(
	        CommentFormatted.FormattedSource.find("// keep me") != std::string::npos,
	        "preserves line comments"))
	{
		std::cerr << CommentFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(
	        CommentFormatted.FormattedSource.find("/* block */") != std::string::npos,
	        "preserves block comments"))
	{
		std::cerr << CommentFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}

	constexpr const char* GenericMessy =
	    "void main(){array < int > xs=array < int >();map < string , int > m=map < string , int >();}";
	const FFormatResult GenericFormatted = VeroScriptFormat::FormatSource(GenericMessy);
	if (!Expect(GenericFormatted.bSuccess, "generic format should succeed"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(
	        GenericFormatted.FormattedSource.find("array<int>") != std::string::npos,
	        "glues array<int> type args"))
	{
		std::cerr << GenericFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(
	        GenericFormatted.FormattedSource.find("map<string, int>") != std::string::npos || GenericFormatted.FormattedSource.find("map<string,int>") != std::string::npos,
	        "glues map<string, int> type args"))
	{
		std::cerr << GenericFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(
	        GenericFormatted.FormattedSource.find("array <") == std::string::npos,
	        "should not leave spaced array <"))
	{
		std::cerr << GenericFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(
	        GenericFormatted.FormattedSource.find("array<int>()") != std::string::npos && GenericFormatted.FormattedSource.find("map<string, int>()") != std::string::npos,
	        "generic calls should keep the argument list attached"))
	{
		std::cerr << GenericFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}
	const FAnalyzeResult GenericAnalyze =
	    VeroScriptAnalyzer::AnalyzeSource(GenericFormatted.FormattedSource, "<fmt-generic>");
	if (!Expect(GenericAnalyze.bSuccess, "formatted generics should analyze clean"))
	{
		std::cerr << GenericFormatted.FormattedSource << '\n';
		for (const FDiagnostic& Diagnostic : GenericAnalyze.Diagnostics)
		{
			std::cerr << Diagnostic.Message << '\n';
		}
		return EXIT_FAILURE;
	}

	constexpr const char* TemplatePrefixMessy =
	    "template < T , U > class PairBox{public:T first;U second;}"
	    "template < T > T identity(T value){return value;}"
	    "template < T = int , U = T > U default_value(){return 6;}"
	    "void main(){PairBox<int,string> p=PairBox<int,string>();"
	    "print(identity<int>(1));print(default_value());}";
	const FFormatResult TemplatePrefixFormatted =
	    VeroScriptFormat::FormatSource(TemplatePrefixMessy);
	if (!Expect(TemplatePrefixFormatted.bSuccess, "template prefix format should succeed") || !Expect(TemplatePrefixFormatted.FormattedSource.find("template<T, U>") != std::string::npos || TemplatePrefixFormatted.FormattedSource.find("template<T,U>") != std::string::npos, "template prefix angle list should format as a type-parameter list"))
	{
		std::cerr << TemplatePrefixFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(
	        TemplatePrefixFormatted.FormattedSource.find("T = int") != std::string::npos && TemplatePrefixFormatted.FormattedSource.find("U = T") != std::string::npos,
	        "generic function defaults should survive formatting"))
	{
		std::cerr << TemplatePrefixFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(
	        TemplatePrefixFormatted.FormattedSource.find("template<T, U>\nclass PairBox") != std::string::npos && TemplatePrefixFormatted.FormattedSource.find("template<T = int, U = T>\nU default_value") != std::string::npos,
	        "template prefixes should stay compact and occupy their own line"))
	{
		std::cerr << TemplatePrefixFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}
	const FAnalyzeResult TemplatePrefixAnalyze = VeroScriptAnalyzer::AnalyzeSource(
	    TemplatePrefixFormatted.FormattedSource,
	    "<fmt-template-prefix>");
	if (!Expect(
	        TemplatePrefixAnalyze.bSuccess,
	        "formatted template prefix should analyze clean"))
	{
		for (const FDiagnostic& Diagnostic : TemplatePrefixAnalyze.Diagnostics)
		{
			std::cerr << Diagnostic.Message << '\n';
		}
		return EXIT_FAILURE;
	}

	constexpr const char* BraceConstructionMessy =
	    "struct Point{int x;Point(int v){x=v;}}"
	    "void main(){Point p=Point{7};array<int> xs=array<int>{1,2};print(p.x+xs[0]);}";
	const FFormatResult BraceConstructionFormatted =
	    VeroScriptFormat::FormatSource(BraceConstructionMessy);
	if (!Expect(
	        BraceConstructionFormatted.bSuccess,
	        "brace construction format should succeed"))
	{
		return EXIT_FAILURE;
	}
	const FAnalyzeResult BraceConstructionAnalyze = VeroScriptAnalyzer::AnalyzeSource(
	    BraceConstructionFormatted.FormattedSource,
	    "<fmt-brace-construction>");
	if (!Expect(
	        BraceConstructionAnalyze.bSuccess,
	        "formatted brace construction should still analyze clean"))
	{
		std::cerr << BraceConstructionFormatted.FormattedSource << '\n';
		for (const FDiagnostic& Diagnostic : BraceConstructionAnalyze.Diagnostics)
		{
			std::cerr << Diagnostic.Message << '\n';
		}
		return EXIT_FAILURE;
	}

	constexpr const char* DirectConstructionMessy =
	    "struct Point{int x;Point(){x=1;}Point(int v){x=v;}}"
	    "Point global(3);void main(){Point a();Point b(7);print(a.x+b.x);}";
	const FFormatResult DirectConstructionFormatted =
	    VeroScriptFormat::FormatSource(DirectConstructionMessy);
	if (!Expect(
	        DirectConstructionFormatted.bSuccess,
	        "direct construction format should succeed"))
	{
		return EXIT_FAILURE;
	}
	const FAnalyzeResult DirectConstructionAnalyze = VeroScriptAnalyzer::AnalyzeSource(
	    DirectConstructionFormatted.FormattedSource,
	    "<fmt-direct-construction>");
	if (!Expect(
	        DirectConstructionAnalyze.bSuccess,
	        "formatted direct construction should still analyze clean"))
	{
		std::cerr << DirectConstructionFormatted.FormattedSource << '\n';
		for (const FDiagnostic& Diagnostic : DirectConstructionAnalyze.Diagnostics)
		{
			std::cerr << Diagnostic.Message << '\n';
		}
		return EXIT_FAILURE;
	}

	constexpr const char* ExplicitConstructionMessy =
	    "struct Token{int value;explicit Token(int input){value=input;}};"
	    "void main(){Token value=cast<Token>(7);print(value.value);}";
	const FFormatResult ExplicitConstructionFormatted =
	    VeroScriptFormat::FormatSource(ExplicitConstructionMessy);
	if (!Expect(
	        ExplicitConstructionFormatted.bSuccess && ExplicitConstructionFormatted.FormattedSource.find("explicit Token(int input)") != std::string::npos,
	        "explicit constructor should survive formatting"))
	{
		std::cerr << ExplicitConstructionFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}
	const FAnalyzeResult ExplicitConstructionAnalyze = VeroScriptAnalyzer::AnalyzeSource(
	    ExplicitConstructionFormatted.FormattedSource,
	    "<fmt-explicit-construction>");
	if (!Expect(
	        ExplicitConstructionAnalyze.bSuccess,
	        "formatted explicit constructor should analyze clean"))
	{
		std::cerr << ExplicitConstructionFormatted.FormattedSource << '\n';
		for (const FDiagnostic& Diagnostic : ExplicitConstructionAnalyze.Diagnostics)
		{
			std::cerr << Diagnostic.Message << '\n';
		}
		return EXIT_FAILURE;
	}

	constexpr const char* CompareMessy = "void main(){for(int i=0;i<3;++i){print(i);}}";
	const FFormatResult CompareFormatted = VeroScriptFormat::FormatSource(CompareMessy);
	if (!Expect(
	        CompareFormatted.bSuccess && CompareFormatted.FormattedSource.find("i < 3") != std::string::npos,
	        "comparison < keeps spaces"))
	{
		std::cerr << CompareFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}

	constexpr const char* ArrowMessy =
	    "void main(){function(int)->int f=function(int x)->int=>x+1;print(f(1));}";
	const FFormatResult ArrowFormatted = VeroScriptFormat::FormatSource(ArrowMessy);
	if (!Expect(ArrowFormatted.bSuccess, "arrow format should succeed"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(
	        ArrowFormatted.FormattedSource.find("->") != std::string::npos && ArrowFormatted.FormattedSource.find("=>") != std::string::npos,
	        "keeps -> and => tokens intact"))
	{
		std::cerr << ArrowFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}
	const FAnalyzeResult ArrowAnalyze =
	    VeroScriptAnalyzer::AnalyzeSource(ArrowFormatted.FormattedSource, "<fmt-arrow>");
	if (!Expect(ArrowAnalyze.bSuccess, "formatted arrows should analyze clean"))
	{
		std::cerr << ArrowFormatted.FormattedSource << '\n';
		for (const FDiagnostic& Diagnostic : ArrowAnalyze.Diagnostics)
		{
			std::cerr << Diagnostic.Message << '\n';
		}
		return EXIT_FAILURE;
	}

	constexpr const char* NestedGeneric =
	    "struct Box<T> { T v; }\nvoid main(){Box < Box < int > > b=Box < Box < int > >();print(0);}\n";
	const FFormatResult NestedFormatted = VeroScriptFormat::FormatSource(NestedGeneric);
	if (!Expect(NestedFormatted.bSuccess, "nested generic format should succeed"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(
	        NestedFormatted.FormattedSource.find("Box<Box<int>>") != std::string::npos || NestedFormatted.FormattedSource.find("Box<Box<int> >") != std::string::npos,
	        "glues nested Box<Box<int>>"))
	{
		std::cerr << NestedFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}

	constexpr const char* BlankSource =
	    "void A() {}\n\nvoid B() {}\n";
	const FFormatResult BlankFormatted = VeroScriptFormat::FormatSource(BlankSource);
	if (!Expect(BlankFormatted.bSuccess, "blank-line format should succeed"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(
	        BlankFormatted.FormattedSource.find("}\n\nvoid B") != std::string::npos,
	        "preserves blank line between top-level decls"))
	{
		std::cerr << BlankFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}

	constexpr const char* DirectiveSource =
	    "#define LONG_NAME \\\n"
	    "	1\n"
	    "void main(){print(LONG_NAME);}\n";
	const FFormatResult DirectiveFormatted = VeroScriptFormat::FormatSource(DirectiveSource);
	if (!Expect(DirectiveFormatted.bSuccess, "directive format should succeed"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(
	        DirectiveFormatted.FormattedSource.find("#define LONG_NAME \\") != std::string::npos && DirectiveFormatted.FormattedSource.find("\\\n") != std::string::npos,
	        "preserves preprocessor line continuation"))
	{
		std::cerr << DirectiveFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}

	for (const char* Comment : {
	         "/* begin\n#define HIDDEN 1\nstill in the comment\n*/",
	         "/* begin\n#define HIDDEN \\\nstill in the comment\n*/",
	         "/* begin\r\n#if HIDDEN\r\nstill in the comment\r\n#endif\r\n*/"})
	{
		const std::string Source = std::string(Comment) + "\nvoid main(){print(1);}\n";
		const auto Formatted = VeroScriptFormat::FormatSource(Source);
		if (!Expect(Formatted.bSuccess && SameTokens(Source, Formatted.FormattedSource) && Formatted.FormattedSource.find(Comment) != std::string::npos,
		        "directive-like lines inside comments remain complete and unchanged") ||
		    !Expect(VeroScriptFormat::FormatSource(Formatted.FormattedSource).FormattedSource == Formatted.FormattedSource, "formatting comments containing hash lines is idempotent"))
		{
			return EXIT_FAILURE;
		}
	}

	for (const std::string& Newline : {std::string("\n"), std::string("\r\n")})
	{
		const std::string Protected = "#define COMMENTED 1 /*" + Newline + "# hidden in a comment" + Newline + "*/" + Newline;
		const std::string Source = Protected + "void main(){print(COMMENTED);}" + Newline;
		const auto Formatted = VeroScriptFormat::FormatSource(Source);
		if (!Expect(Formatted.bSuccess && SameTokens(Source, Formatted.FormattedSource) && Formatted.FormattedSource.find(Protected) == 0,
		        "directives preserve comments that continue on following physical lines") ||
		    !Expect(VeroScriptFormat::FormatSource(Formatted.FormattedSource).FormattedSource == Formatted.FormattedSource, "directive comments format idempotently"))
		{
			return EXIT_FAILURE;
		}
		const auto Partial = VeroScriptFormat::FormatRangeSource(Source, FTextRange{1, 1, 1, 100});
		if (!Expect(Partial.bSuccess && !Partial.bHasFormattedRange,
		        "range formatting never cuts through a directive's trailing block comment"))
		{
			return EXIT_FAILURE;
		}
		const std::string SameLine = "#define COMMENTED 1 /*" + Newline + "*/ void main(){" + Newline + "print(COMMENTED);" + Newline + "}" + Newline;
		const auto SameLineFormatted = VeroScriptFormat::FormatSource(SameLine);
		if (!Expect(SameLineFormatted.bSuccess && SameTokens(SameLine, SameLineFormatted.FormattedSource) && SameLineFormatted.FormattedSource.find("\n    print(COMMENTED);\n") != std::string::npos && VeroScriptFormat::FormatSource(SameLineFormatted.FormattedSource).FormattedSource == SameLineFormatted.FormattedSource,
		        "code after a directive's multiline comment participates in block indentation"))
		{
			return EXIT_FAILURE;
		}

		const std::string SplicedFirst = std::string("#define COMMENTED 1 /* ") + '\\' + Newline;
		const std::string Spliced = SplicedFirst + "keep " + '\\' + Newline + "*/ + 2" + Newline;
		const std::string SplicedSource = Spliced + "void main(){print(COMMENTED);}" + Newline;
		const auto SplicedFormatted = VeroScriptFormat::FormatSource(SplicedSource);
		if (!Expect(SplicedFormatted.bSuccess && SameTokens(SplicedSource, SplicedFormatted.FormattedSource) && SplicedFormatted.FormattedSource.find(Spliced) == 0,
		        "spliced newlines inside comments keep the following expression in the macro body") ||
		    !Expect(VeroScriptFormat::FormatSource(SplicedFormatted.FormattedSource).FormattedSource == SplicedFormatted.FormattedSource, "spliced directive comments format idempotently"))
		{
			return EXIT_FAILURE;
		}
		const auto SplicedPartial = VeroScriptFormat::FormatRangeSource(SplicedSource, FTextRange{2, 1, 2, 100});
		if (!Expect(SplicedPartial.bSuccess && !SplicedPartial.bHasFormattedRange,
		        "range formatting cannot split a spliced comment from its macro suffix"))
		{
			return EXIT_FAILURE;
		}

		const std::string Mixed = SplicedFirst + "not continued" + Newline + "*/";
		const std::string MixedSource = Mixed + " void main(){" + Newline + "print(COMMENTED);" + Newline + "}" + Newline;
		const auto MixedFormatted = VeroScriptFormat::FormatSource(MixedSource);
		if (!Expect(MixedFormatted.bSuccess && SameTokens(MixedSource, MixedFormatted.FormattedSource) && MixedFormatted.FormattedSource.find(Mixed) == 0 && MixedFormatted.FormattedSource.find("\n    print(COMMENTED);\n") != std::string::npos && VeroScriptFormat::FormatSource(MixedFormatted.FormattedSource).FormattedSource == MixedFormatted.FormattedSource,
		        "an unspliced newline ends the macro even after earlier comment continuations"))
		{
			return EXIT_FAILURE;
		}

		for (const char* Trailing : {" ", "\t", " \t"})
		{
			const std::string NotSpliced = std::string("#define UNUSED ") + '\\' + Trailing + Newline;
			const std::string Following = "void main(){" + Newline + "print(1);" + Newline + "}" + Newline;
			const std::string NotSplicedSource = NotSpliced + Following;
			const auto NotSplicedFormatted = VeroScriptFormat::FormatSource(NotSplicedSource);
			if (!Expect(NotSplicedFormatted.bSuccess && SameTokens(NotSplicedSource, NotSplicedFormatted.FormattedSource) && NotSplicedFormatted.FormattedSource.find(NotSpliced) == 0 && NotSplicedFormatted.FormattedSource.find("\n    print(1);\n") != std::string::npos && VeroScriptFormat::FormatSource(NotSplicedFormatted.FormattedSource).FormattedSource == NotSplicedFormatted.FormattedSource,
			        "spaces after a backslash do not hide real code on the next line"))
			{
				return EXIT_FAILURE;
			}

			const std::string CommentEnd = std::string("#define COMMENTED 1 /* ") + '\\' + Trailing + Newline + "*/";
			const std::string CommentEndSource = CommentEnd + " " + Following;
			const auto CommentEndFormatted = VeroScriptFormat::FormatSource(CommentEndSource);
			if (!Expect(CommentEndFormatted.bSuccess && SameTokens(CommentEndSource, CommentEndFormatted.FormattedSource) && CommentEndFormatted.FormattedSource.find(CommentEnd) == 0 && CommentEndFormatted.FormattedSource.find("\n    print(1);\n") != std::string::npos && VeroScriptFormat::FormatSource(CommentEndFormatted.FormattedSource).FormattedSource == CommentEndFormatted.FormattedSource,
			        "whitespace prevents continuation inside a directive's trailing block comment"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	for (const std::string& Newline : {std::string("\n"), std::string("\r\n")})
	{
		const std::string Splice = std::string("\\") + Newline;
		for (const std::string& Source : {
		         "#define X 1 /" + Splice + "* ignored" + Newline + "*/ void main(){print(X);}" + Newline,
		         "void main(){/" + Splice + "* ignored */ print(1);}" + Newline,
		         "void main(){/" + Splice + Splice + "* ignored */ print(1);}" + Newline,
		         "void main(){/" + Splice + "/ ignored" + Newline + "print(1);}" + Newline,
		         "void main(){/* ignored *" + Splice + "/print(1);}"})
		{
			// A split closer can be invalid for the raw lexer while remaining a
			// valid Core-preprocessed comment. Preserve bytes before raw lexing.
			const auto Formatted = VeroScriptFormat::FormatSource(Source);
			const auto Range = VeroScriptFormat::FormatRangeSource(Source, FTextRange{1, 1, 1, 100});
			if (!Expect(Formatted.bSuccess && Formatted.FormattedSource == Source && VeroScriptFormat::FormatSource(Formatted.FormattedSource).FormattedSource == Source,
			        "formatting preserves delimiters formed across physical continuations") ||
			    !Expect(Range.bSuccess && !Range.bHasFormattedRange,
			        "range formatting checks the whole document for spliced comment delimiters"))
			{
				return EXIT_FAILURE;
			}
		}

		for (const char* Trailing : {" ", "\t", " \t"})
		{
			const std::string Source = std::string("void main(){auto text=r\"/") + '\\' + Trailing + Newline + "*\";print(1);}";
			const auto Formatted = VeroScriptFormat::FormatSource(Source);
			if (!Expect(Formatted.bSuccess && SameTokens(Source, Formatted.FormattedSource) && Formatted.FormattedSource.find("\n    print(1);\n") != std::string::npos && VeroScriptFormat::FormatSource(Formatted.FormattedSource).FormattedSource == Formatted.FormattedSource,
			        "whitespace after a backslash does not trigger spliced-delimiter protection"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		const std::string Bom = "\xef\xbb\xbf";
		const std::string Source = Bom + "#define VALUE 1\nvoid main(){print(VALUE);}\n";
		const auto Formatted = VeroScriptFormat::FormatSource(Source);
		if (!Expect(Formatted.bSuccess && Formatted.FormattedSource.find(Bom + "#define VALUE 1\n") == 0 && SameTokens(Source, Formatted.FormattedSource),
		        "a UTF-8 BOM does not hide a directive or disappear during formatting") ||
		    !Expect(VeroScriptFormat::FormatSource(Bom).FormattedSource == Bom,
		        "a BOM-only document remains empty"))
		{
			return EXIT_FAILURE;
		}
	}

	for (const std::string& Newline : {std::string("\n"), std::string("\r\n")})
	{
		const std::string Directive = std::string("#define RANGE_CHOICE(flag) \\") + Newline + "if(flag){print(1);} \\" + Newline + "else{print(2);}" + Newline;
		const std::string Following = "void main(){RANGE_CHOICE(true);}" + Newline;
		const std::string Source = Directive + Following;
		for (const FTextRange Selection : {
		         FTextRange{1, 1, 1, 100}, FTextRange{2, 1, 2, 100}, FTextRange{3, 1, 3, 100}})
		{
			const auto Partial = VeroScriptFormat::FormatRangeSource(Source, Selection);
			if (!Expect(Partial.bSuccess && !Partial.bHasFormattedRange,
			        "range formatting must preserve partial preprocessor directives"))
			{
				std::cerr << Partial.FormattedSource << '\n';
				return EXIT_FAILURE;
			}
		}
		const auto Complete = VeroScriptFormat::FormatRangeSource(
		    Source, FTextRange{1, 1, 3, 100});
		const auto After = VeroScriptFormat::FormatRangeSource(
		    Source, FTextRange{4, 1, 4, 100});
		if (!Expect(Complete.bSuccess && Complete.bHasFormattedRange && Complete.FormattedSource == Directive,
		        "complete macro selections must retain their original bytes") ||
		    !Expect(After.bSuccess && After.bHasFormattedRange && SameTokens(Following, After.FormattedSource) && After.FormattedSource.find("void main()\n{") != std::string::npos,
		        "ordinary code after a macro must still format normally"))
		{
			return EXIT_FAILURE;
		}
	}

	// Access labels outdent relative to members (C++ / stdlib style).
	constexpr const char* AccessMessy =
	    "class C{public:int x;private:int y;protected:void f(){}}";
	const FFormatResult AccessFormatted = VeroScriptFormat::FormatSource(AccessMessy);
	if (!Expect(AccessFormatted.bSuccess, "access-label format should succeed"))
	{
		return EXIT_FAILURE;
	}
	{
		const std::string& Out = AccessFormatted.FormattedSource;
		if (!Expect(
		        Out.find("\npublic:\n") != std::string::npos && Out.find("\nprivate:\n") != std::string::npos && Out.find("\nprotected:\n") != std::string::npos,
		        "access labels sit at class indent (outdented)"))
		{
			std::cerr << Out << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
		        Out.find("public:\n    int x") != std::string::npos && Out.find("private:\n    int y") != std::string::npos,
		        "members stay one indent deeper than access labels"))
		{
			std::cerr << Out << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
		        Out.find("    public:") == std::string::npos && Out.find("    private:") == std::string::npos,
		        "access labels must not share member indent"))
		{
			std::cerr << Out << '\n';
			return EXIT_FAILURE;
		}
	}
	const FFormatResult AccessSecond = VeroScriptFormat::FormatSource(AccessFormatted.FormattedSource);

	constexpr const char* FriendMessy =
	    "class Reader{public:int read(Secret value)const{return value.code;}}"
	    "class Secret{friend class Reader;friend int Reader::read(Secret value)const;private:int code=1;}";
	const FFormatResult FriendFormatted = VeroScriptFormat::FormatSource(FriendMessy);
	if (!Expect(
	        FriendFormatted.bSuccess && FriendFormatted.FormattedSource.find("friend class Reader;") != std::string::npos && FriendFormatted.FormattedSource.find("friend int Reader::read(Secret value) const;") != std::string::npos,
	        "friend declarations format with stable C++ spelling"))
	{
		std::cerr << FriendFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(
	        AccessSecond.bSuccess && AccessSecond.FormattedSource == AccessFormatted.FormattedSource,
	        "access-label format is stable"))
	{
		std::cerr << AccessSecond.FormattedSource << '\n';
		return EXIT_FAILURE;
	}
	const FAnalyzeResult AccessAnalyze =
	    VeroScriptAnalyzer::AnalyzeSource(AccessFormatted.FormattedSource, "<fmt-access>");
	if (!Expect(AccessAnalyze.bSuccess, "formatted access labels should analyze clean"))
	{
		std::cerr << AccessFormatted.FormattedSource << '\n';
		for (const FDiagnostic& Diagnostic : AccessAnalyze.Diagnostics)
		{
			std::cerr << Diagnostic.Message << '\n';
		}
		return EXIT_FAILURE;
	}

	// Nested type: access label outdents relative to the nested body, not to file.
	constexpr const char* NestedAccess =
	    "class Outer{class Inner{public:int x;}}";
	const FFormatResult NestedAccessFormatted = VeroScriptFormat::FormatSource(NestedAccess);
	if (!Expect(NestedAccessFormatted.bSuccess, "nested access format should succeed"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(
	        NestedAccessFormatted.FormattedSource.find("\n    public:\n") != std::string::npos && NestedAccessFormatted.FormattedSource.find("public:\n        int x") != std::string::npos,
	        "nested access outdents one level inside outer body"))
	{
		std::cerr << NestedAccessFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}

	// Inheritance `public Base` must not be treated as an access label.
	constexpr const char* InheritMessy = "class Child:public Base{public:int x;}";
	const FFormatResult InheritFormatted = VeroScriptFormat::FormatSource(InheritMessy);
	if (!Expect(InheritFormatted.bSuccess, "inheritance public format should succeed"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(
	        InheritFormatted.FormattedSource.find(": public Base") != std::string::npos || InheritFormatted.FormattedSource.find(":public Base") != std::string::npos,
	        "keeps inheritance public Base"))
	{
		std::cerr << InheritFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(
	        InheritFormatted.FormattedSource.find("\npublic:\n") != std::string::npos,
	        "body public: still outdents"))
	{
		std::cerr << InheritFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}

	// 1.0 surface: `final` / destructor keep glued `~Type` and readable spacing.
	constexpr const char* SealedMessy =
	    "interface IState final{int value=1;virtual int get()final{return this.value;}}"
	    "class Owner final{public:virtual ~Owner(){}virtual int get()final{return 2;}}";
	const FFormatResult SealedFormatted = VeroScriptFormat::FormatSource(SealedMessy);
	if (!Expect(SealedFormatted.bSuccess, "sealed surface format should succeed"))
	{
		return EXIT_FAILURE;
	}
	const std::string& SealedOut = SealedFormatted.FormattedSource;
	if (!Expect(
	        SealedOut.find("interface IState final") != std::string::npos && SealedOut.find("class Owner final") != std::string::npos,
	        "keeps type-level final"))
	{
		std::cerr << SealedOut << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(
	        SealedOut.find("~Owner()") != std::string::npos && SealedOut.find("~ Owner") == std::string::npos,
	        "destructor keeps glued ~Owner()"))
	{
		std::cerr << SealedOut << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(
	        SealedOut.find("get() final") != std::string::npos || SealedOut.find("get()final") != std::string::npos,
	        "keeps method-level final"))
	{
		std::cerr << SealedOut << '\n';
		return EXIT_FAILURE;
	}
	const FAnalyzeResult SealedAnalyze =
	    VeroScriptAnalyzer::AnalyzeSource(SealedOut, "<fmt-sealed>");
	if (!Expect(SealedAnalyze.bSuccess, "formatted sealed surface should analyze clean"))
	{
		std::cerr << SealedOut << '\n';
		for (const FDiagnostic& Diagnostic : SealedAnalyze.Diagnostics)
		{
			std::cerr << Diagnostic.Message << '\n';
		}
		return EXIT_FAILURE;
	}

	// `::` is glued like `.` (no spaces around the scope operator).
	constexpr const char* ScopeMessy =
	    "namespace N{class C{public:static int V=0;}}\n"
	    "void main(){N :: C x = N :: C();print(N :: C :: V);}\n";
	const FFormatResult ScopeFormatted = VeroScriptFormat::FormatSource(ScopeMessy);
	if (!Expect(ScopeFormatted.bSuccess, "scope :: format should succeed"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(
	        ScopeFormatted.FormattedSource.find("N ::") == std::string::npos && ScopeFormatted.FormattedSource.find(":: C") == std::string::npos && ScopeFormatted.FormattedSource.find("N::C") != std::string::npos,
	        ":: stays glued like ."))
	{
		std::cerr << ScopeFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}
	const FAnalyzeResult ScopeAnalyze =
	    VeroScriptAnalyzer::AnalyzeSource(ScopeFormatted.FormattedSource, "<fmt-scope>");
	if (!Expect(ScopeAnalyze.bSuccess, "formatted :: source should analyze clean"))
	{
		std::cerr << ScopeFormatted.FormattedSource << '\n';
		for (const FDiagnostic& Diagnostic : ScopeAnalyze.Diagnostics)
		{
			std::cerr << Diagnostic.Message << '\n';
		}
		return EXIT_FAILURE;
	}

	constexpr const char* NamespaceMessy =
	    "namespace Library{int value=1;}using namespace Library;void main(){print(value);}";
	const FFormatResult NamespaceFormatted = VeroScriptFormat::FormatSource(NamespaceMessy);
	if (!Expect(
	        NamespaceFormatted.bSuccess && NamespaceFormatted.FormattedSource.find("using namespace Library;") != std::string::npos,
	        "using namespace directive survives formatting"))
	{
		std::cerr << NamespaceFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}

	constexpr const char* DiscardMessy =
	    "int touch(){return 1;}void main(){double value=2.5;float leading=.5f;"
	    "int converted=(int)value;(void)touch();print(leading);print((converted+1)*2);}";
	const FFormatResult DiscardFormatted = VeroScriptFormat::FormatSource(DiscardMessy);
	if (!Expect(
	        DiscardFormatted.bSuccess && DiscardFormatted.FormattedSource.find("(void) touch();") != std::string::npos && DiscardFormatted.FormattedSource.find(".5f") != std::string::npos && (DiscardFormatted.FormattedSource.find("(int) value") != std::string::npos || DiscardFormatted.FormattedSource.find("(int)value") != std::string::npos),
	        "C-style cast and explicit void discard survive formatting"))
	{
		std::cerr << DiscardFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}
	const FAnalyzeResult DiscardAnalyze =
	    VeroScriptAnalyzer::AnalyzeSource(DiscardFormatted.FormattedSource, "<fmt-discard>");
	if (!Expect(DiscardAnalyze.bSuccess, "formatted explicit void discard analyzes cleanly"))
	{
		for (const FDiagnostic& Diagnostic : DiscardAnalyze.Diagnostics)
		{
			std::cerr << Diagnostic.Message << '\n';
		}
		return EXIT_FAILURE;
	}

	// Range formatting replaces complete physical lines, rather than inserting a
	// fully formatted fragment into the middle of an existing line.  The selected
	// block has an enclosing scope, so this also verifies indentation context.
	constexpr const char* RangeMessy =
	    "void main() {\n"
	    "int value=1;\n"
	    "print(value);\n"
	    "}\n";
	FTextRange Range;
	Range.StartLine = 2;
	Range.StartColumn = 1;
	Range.EndLine = 3;
	Range.EndColumn = 14;
	const FRangeFormatResult RangeFormatted = VeroScriptFormat::FormatRangeSource(RangeMessy, Range);
	if (!Expect(
	        RangeFormatted.bSuccess && RangeFormatted.bHasFormattedRange && RangeFormatted.FormattedRange.StartLine == 2 && RangeFormatted.FormattedRange.EndLine == 4 && RangeFormatted.FormattedRange.EndColumn == 1 && RangeFormatted.FormattedSource == "    int value = 1;\n    print(value);\n",
	        "range formatting returns a complete, newline-safe line edit"))
	{
		std::cerr << RangeFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}

	FTextRange ExclusiveRange;
	ExclusiveRange.StartLine = 2;
	ExclusiveRange.StartColumn = 1;
	ExclusiveRange.EndLine = 4;
	ExclusiveRange.EndColumn = 1;
	const FRangeFormatResult ExclusiveFormatted =
	    VeroScriptFormat::FormatRangeSource(RangeMessy, ExclusiveRange);
	if (!Expect(
	        ExclusiveFormatted.bSuccess && ExclusiveFormatted.bHasFormattedRange && ExclusiveFormatted.FormattedRange.EndLine == 4 && ExclusiveFormatted.FormattedRange.EndColumn == 1 && ExclusiveFormatted.FormattedSource == "    int value = 1;\n    print(value);\n",
	        "range formatting honors an exclusive end at the next line start"))
	{
		std::cerr << ExclusiveFormatted.FormattedSource << '\n';
		return EXIT_FAILURE;
	}

	for (const char* Source : {
	         "void main(){\nif(true){print(1);}\n/* range clause */else{print(2);}\n}\n",
	         "void main(){\nint value=0;do{value=value+1;}\n/* range clause */while(value<2);\n}\n"})
	{
		const FRangeFormatResult Formatted = VeroScriptFormat::FormatRangeSource(
		    Source, FTextRange{3, 1, 3, 1});
		if (!Expect(Formatted.bSuccess && Formatted.bHasFormattedRange && Formatted.FormattedSource.find("/* range clause */\n    ") != std::string::npos,
		        "range formatting must put a commented clause on a new line without its preceding brace"))
		{
			std::cerr << Formatted.FormattedSource << '\n';
			return EXIT_FAILURE;
		}
		std::string Applied = Source;
		const std::size_t Begin = Applied.find("/* range clause */");
		const std::size_t End = Applied.find('\n', Begin) + 1;
		Applied.replace(Begin, End - Begin, Formatted.FormattedSource);
		if (!Expect(SameTokens(Source, Applied) && VeroScriptAnalyzer::AnalyzeSource(Applied, "<fmt-range-clause>").bSuccess,
		        "commented range replacements must preserve the surrounding program"))
		{
			std::cerr << Applied << '\n';
			return EXIT_FAILURE;
		}
	}

	constexpr const char* CommentRangeSource =
	    "void main() {\n"
	    "    /* keep this comment\n"
	    "       exactly as authored */\n"
	    "    print(1);\n"
	    "}\n";
	FTextRange CommentRange;
	CommentRange.StartLine = 3;
	CommentRange.StartColumn = 1;
	CommentRange.EndLine = 3;
	CommentRange.EndColumn = 30;
	const FRangeFormatResult CommentRangeFormatted =
	    VeroScriptFormat::FormatRangeSource(CommentRangeSource, CommentRange);
	if (!Expect(
	        CommentRangeFormatted.bSuccess && !CommentRangeFormatted.bHasFormattedRange,
	        "range formatting inside a block comment is a safe no-op"))
	{
		return EXIT_FAILURE;
	}

	for (const std::string& Newline : {std::string("\n"), std::string("\r\n")})
	{
		const std::string RawSource = "void main(){string text=r\"start" + Newline + "  int   x=1;" + Newline + "tail\\\";" + Newline + "int   y=2;" + Newline + "}";
		FTextRange Interior{2, 1, 2, 13};
		const auto Protected = VeroScriptFormat::FormatRangeSource(RawSource, Interior);
		if (!Expect(Protected.bSuccess && !Protected.bHasFormattedRange,
		        "range formatting preserves raw literal contents under LF and CRLF"))
		{
			return EXIT_FAILURE;
		}

		Interior.EndLine = 3;
		Interior.EndColumn = 8;
		const auto Suffix = VeroScriptFormat::FormatRangeSource(RawSource, Interior);
		if (!Expect(Suffix.bSuccess && !Suffix.bHasFormattedRange,
		        "range formatting preserves the closing part of a raw literal"))
		{
			return EXIT_FAILURE;
		}

		FTextRange Following{4, 1, 4, 12};
		const auto After = VeroScriptFormat::FormatRangeSource(RawSource, Following);
		if (!Expect(After.bSuccess && After.bHasFormattedRange &&
		            After.FormattedSource.find("int y = 2;") != std::string::npos,
		        "a raw quote closes even after a backslash and subsequent code formats normally"))
		{
			return EXIT_FAILURE;
		}

		const auto Full = VeroScriptFormat::FormatSource(RawSource);
		if (!Expect(Full.bSuccess &&
		            Full.FormattedSource.find("r\"start" + Newline + "  int   x=1;" + Newline + "tail\\\"") !=
		                std::string::npos,
		        "full formatting preserves the complete raw literal lexeme"))
		{
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}
