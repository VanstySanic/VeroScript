// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptToolingTestVm.h"
#include "VeroScriptTooling.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptLexer.h"
#include "../Internal/VeroScriptStandardNamespaceMetadata.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <tuple>

namespace
{
	int Fail(const char* Message)
	{
		std::cerr << "VeroScriptCallHelpTests: " << Message << '\n';
		return EXIT_FAILURE;
	}

	bool Expect(const bool Condition, const char* Message)
	{
		return Condition ? true : (Fail(Message), false);
	}

	bool SignatureNames(const std::string_view Label, const std::string_view Name)
	{
		const auto Head = Label.substr(0, Label.find_first_of(" ("));
		const auto Separator = Head.find_last_of(".:");
		return Head.substr(Separator == std::string_view::npos ? 0 : Separator + 1) == Name;
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
	{
		const std::string Prefix =
		    "int Combine(int left,int right){return left+right;}"
		    "void Use(int first,int second){}"
		    "struct Point{int x;Point(int a,int b){x=a+b;}};"
		    "class Indexed{public:int operator[](int index){return index;}};"
		    "void main(){Indexed values=Indexed();";
		struct FCase
		{
			std::string Expression;
			std::string BeforeCaret;
			std::string ExpectedName;
			std::uint32_t Parameter = 0;
		};
		const std::vector<FCase> Cases = {
		    {"Use(1, 2)", "Use(", "Use", 0},
		    {"Use(1, 2)", "Use(1, ", "Use", 1},
		    {"Use(Combine(1, 2), 3)", "Use(Combine(1, ", "Combine", 1},
		    {"Use(Combine(1, 2), 3)", "Use(Combine(1, 2), ", "Use", 1},
		    {"Use(values[Combine(1, 2)], 3)", "Use(values[Combine(1, 2)], ", "Use", 1},
		    {"Point{Combine(1, 2), 3}", "Point{Combine(1, 2), ", "Point", 1},
		    {"values[1]", "values[", "operator[]", 0},
		    {"values[Combine(1, 2) ]", "values[Combine(1, 2)", "operator[]", 0},
		    {"values[Combine(1, 2) ", "values[Combine(1, 2) ", "operator[]", 0},
		    {"Use(Combine(1, 2), ", "Use(Combine(1, 2), ", "Use", 1},
		    {"Use(1, /* keep */", "Use(1, ", "Use", 1},
		    {"Use(1, /* keep */ // keep\n", "Use(1, ", "Use", 1},
		    {"values[1 // keep\n", "values[1", "operator[]", 0},
		    {"Use(1, 2)", "Use(1, 2", "Use", 1},
		    {"Use(1, 2)", "Use(1, 2)", "", 0},
		    {"Point{1, 2}", "Point{1, 2", "Point", 1},
		    {"Point{1, 2}", "Point{1, 2}", "", 0},
		    {"values[1]", "values[1", "", 0},
		    {"values[1]", "values[1]", "", 0},
		};
		bool bPassed = true;
		for (const FCase& Test : Cases)
		{
			const std::string Source = Prefix + Test.Expression + ";}";
			FTextPosition Position;
			if (!Expect(FindPosition(Source, Test.Expression, Position), "locate delimiter signature case"))
			{
				return EXIT_FAILURE;
			}
			Position.Column += static_cast<std::uint32_t>(Test.BeforeCaret.size());
			const auto Help = VeroScriptCallHelp::HelpAtSource(Source, Position, "<signature-delimiters>");
			const bool bExpectedActive = !Test.ExpectedName.empty();
			if (!Expect(Help.bSuccess && Help.bActive == bExpectedActive && (!bExpectedActive || (Help.ActiveParameter == Test.Parameter && Help.Signatures.size() == 1 && SignatureNames(Help.Signatures.front().Label, Test.ExpectedName))),
			        "signature delimiters preserve nesting, argument counts and closing-character boundaries"))
			{
				std::cerr << Source << '\n'
				          << "caret=" << Position.Column
				          << ", success=" << Help.bSuccess << ", active=" << Help.bActive
				          << ", parameter=" << Help.ActiveParameter << '\n';
				for (const auto& Signature : Help.Signatures)
				{
					std::cerr << Signature.Label << '\n';
				}
				for (const auto& Diagnostic : Help.Diagnostics)
				{
					std::cerr << Diagnostic.Message << '\n';
				}
				bPassed = false;
			}
		}
		if (!bPassed) return EXIT_FAILURE;
	}
	{
		const std::string Declarations =
		    "int Make<T,U>(){return 1;}int Value<T,int N>(){return N;}"
		    "void Use(int first,int second,int third){}"
		    "void Use(int first,string second,int third){}"
		    "void Compare(bool first,bool second,int third){}"
		    "namespace N{void Use(int first,int second,int third){}}"
		    "class Receiver{public:void Use(int first,int second,int third){}};"
		    "function(int,int,int)->int Factory(){return function(int a,int b,int c)->int=>a+b+c;}";
		struct FCase
		{
			std::string Expression, BeforeCaret, ExpectedSecondType;
			std::uint32_t Parameter = 1;
			bool bMacro = false;
		};
		const std::vector<FCase> Cases = {
		    {"Use(Make<int,string>(),42,99)", "Use(Make<int,string>(),", "int"},
		    {"Use(Make<int,string>(),42,99)", "Use(Make<int,string>(),42", "int"},
		    {"Use(Make<int,string>(),\"text\",99)", "Use(Make<int,string>(),\"text\"", "string"},
		    {"Use(Make<map<int,string>,array<int>>(),42,99)",
		        "Use(Make<map<int,string>,array<int>>(),42", "int"},
		    {"Use(Make<fn(int,string)->int,array<int>>(),42,99)",
		        "Use(Make<fn(int,string)->int,array<int>>(),42", "int"},
		    {"Use(Value<int,4>(),42,99)", "Use(Value<int,4>(),42", "int"},
		    {"Use(Value<int,(1 < 2 ? 3 : 4)>(),42,99)", "Use(Value<int,(1 < 2 ? 3 : 4)>(),42", "int"},
		    {"Compare(a<b,c>d,99)", "Compare(a<b,c>d", "bool"},
		    {"Compare((a<b),(c>d),99)", "Compare((a<b),(c>d)", "bool"},
		    {"N::Use(Make<int,string>(),42,99)", "N::Use(Make<int,string>(),42", "int"},
		    {"receiver.Use(Make<int,string>(),42,99)", "receiver.Use(Make<int,string>(),42", "int"},
		    {"Factory()(Make<int,string>(),42,99)", "Factory()(Make<int,string>(),42", "int"},
		    {"Use(Make<int,string>() /* , ignored */ ,42,99)",
		        "Use(Make<int,string>() /* , ignored */ ", "", 0},
		    {"Use(Make<int,string>() /* , ignored */ , /* , ignored */ 42,99)",
		        "Use(Make<int,string>() /* , ignored */ , /* , ignored */ ", "int"},
		    {"Use(Make<int,string>(),42", "Use(Make<int,string>(),42", "int"},
		    {"Use(Make<int,string>(), ", "Use(Make<int,string>(), ", "int"},
		    {"Use(Make<int,string>(),42,99)", "Use(Make<int,string>(),42", "int", 1, true},
		    {"Use(Make<int,string>(),\"text\",99)", "Use(Make<int,string>(),\"text\"", "string", 1, true},
		};
		bool bPassed = true;
		for (const FCase& Test : Cases)
		{
			const std::string Source = (Test.bMacro ? "#define LONGTYPE int\n" : "") + Declarations + "void main(){Receiver receiver=Receiver();int a=1,b=2,c=3,d=4;" + (Test.bMacro ? "LONGTYPE lead=0;" : "int lead=0;") + Test.Expression + ";}";
			FTextPosition Position;
			if (!Expect(FindPosition(Source, Test.Expression, Position), "locate parsed argument boundary"))
			{
				return EXIT_FAILURE;
			}
			Position.Column += static_cast<std::uint32_t>(Test.BeforeCaret.size());
			const auto Help = VeroScriptCallHelp::HelpAtSource(Source, Position, "<signature-parsed-arguments>");
			if (!Expect(Help.bSuccess && Help.bActive && Help.ActiveParameter == Test.Parameter && Help.ActiveSignature < Help.Signatures.size() && (Test.Expression.rfind("N::", 0) != 0 || (Help.Signatures.size() == 1 && Help.Signatures.front().Label.rfind("N::Use", 0) == 0)) && Help.Signatures[Help.ActiveSignature].Parameters.size() == 3 && (Test.ExpectedSecondType.empty() || Help.Signatures[Help.ActiveSignature].Parameters[1].Label.find(Test.ExpectedSecondType) != std::string::npos),
			        "parsed argument ranges preserve generic commas, comparisons, overload types and source maps"))
			{
				std::cerr << Source << '\n'
				          << "caret=" << Position.Line << ':' << Position.Column
				          << ", success=" << Help.bSuccess << ", active=" << Help.bActive
				          << ", parameter=" << Help.ActiveParameter << ", signature=" << Help.ActiveSignature << '\n';
				for (const auto& Signature : Help.Signatures)
				{
					std::cerr << Signature.Label << '\n';
				}
				for (const auto& Diagnostic : Help.Diagnostics)
				{
					std::cerr << Diagnostic.Message << '\n';
				}
				bPassed = false;
			}
		}
		if (!bPassed) return EXIT_FAILURE;
	}
	{
		constexpr std::uint32_t Depth = 128;
		std::string Expression;
		for (std::uint32_t Index = 0; Index < Depth; ++Index)
		{
			Expression += "F(";
		}
		Expression += "1" + std::string(Depth, ')');
		const std::string Source = "int F(int value){return value;}void main(){" + Expression + ";}";
		FTextPosition Position;
		if (!Expect(FindPosition(Source, Expression, Position), "locate deep completed call chain"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += 2 * Depth;
		const auto Help = VeroScriptCallHelp::HelpAtSource(Source, Position, "<signature-deep-delimiters>");
		if (!Expect(Help.bSuccess && Help.bActive && Help.ActiveParameter == 0 && Help.Signatures.size() == 1 && SignatureNames(Help.Signatures.front().Label, "F"),
		        "deep completed call chains select their innermost call without recursive delimiter scans"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		std::string Source;
		for (int Index = 0; Index < 8192; ++Index)
		{
			Source += "F(";
		}
		std::size_t LexerChecks = 0;
		{
			VeroScript::FAnalysisControlScope Control([&]()
			{
				++LexerChecks;
				return false;
			});
			VeroScript::VeroScriptLexer Lexer(Source, "<signature-cancel-delimiters>");
			(void)Lexer.Tokenize();
		}
		std::size_t Checks = 0;
		bool bCancelled = false, bDelimiterStage = false, bModuleStage = false;
		{
			VeroScript::FAnalysisControlScope Control(
			    [&]()
			{
				return ++Checks >= LexerChecks + 3;
			},
			    [&](const std::string_view Name, double)
			{
				bDelimiterStage |= Name == "signature.delimiters";
				bModuleStage |= Name == "load" || Name == "parse";
			});
			try
			{
				(void)VeroScriptCallHelp::HelpAtSource(Source,
				    {1, static_cast<std::uint32_t>(Source.size() + 1)}, "<signature-cancel-delimiters>");
			}
			catch (const VeroScript::FAnalysisCancelled&)
			{
				bCancelled = true;
			}
		}
		if (!Expect(bCancelled && bDelimiterStage && !bModuleStage,
		        "deep unclosed call scanning observes cancellation before entering module loading"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		const std::string AccessSource =
		    "class Restricted{\nprivate:\nRestricted(int secret){}\n"
		    "protected:\nRestricted(string guarded){}\npublic:\nRestricted(bool visible){}\n"
		    "static Restricted make(){return Restricted(1);}\n};\n"
		    "class Derived:public Restricted{\npublic:\n"
		    "Derived():Restricted(\"base\"){}\n};\n"
		    "void outside(){Restricted item=Restricted(true);}\n";
		for (const auto& [Needle, Private, Protected] : {
		         std::tuple<const char*, bool, bool>{"Restricted(true)", false, false},
		         std::tuple<const char*, bool, bool>{"Restricted(1)", true, true},
		         std::tuple<const char*, bool, bool>{"Restricted(\"base\")", false, true}})
		{
			FTextPosition Position;
			if (!Expect(FindPosition(AccessSource, Needle, Position), "locate constructor access context"))
			{
				return EXIT_FAILURE;
			}
			Position.Column += static_cast<std::uint32_t>(std::string_view("Restricted(").size());
			const auto Help = VeroScriptCallHelp::HelpAtSource(AccessSource, Position, "<constructor-access>");
			bool bPrivate = false, bProtected = false, bPublic = false;
			for (const auto& Signature : Help.Signatures)
			{
				bPrivate |= Signature.Label.find("secret") != std::string::npos;
				bProtected |= Signature.Label.find("guarded") != std::string::npos;
				bPublic |= Signature.Label.find("visible") != std::string::npos;
			}
			if (!Expect(Help.bSuccess && bPublic && bPrivate == Private && bProtected == Protected,
			        "constructor signatures preserve public, protected and private access"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	for (const auto& [FriendType, FunctionType, bFriend] : {
	         std::tuple<const char*, const char*, bool>{"ref int", "ref int", true},
	         std::tuple<const char*, const char*, bool>{"const ref int", "const ref int", true},
	         std::tuple<const char*, const char*, bool>{"const int", "int", true},
	         std::tuple<const char*, const char*, bool>{"int", "const int", true},
	         std::tuple<const char*, const char*, bool>{"int...", "int...", true},
	         std::tuple<const char*, const char*, bool>{"ref Integer", "ref int", true},
	         std::tuple<const char*, const char*, bool>{"int...", "Integer...", true},
	         std::tuple<const char*, const char*, bool>{"ref int", "refint", false},
	         std::tuple<const char*, const char*, bool>{"refint", "ref int", false},
	         std::tuple<const char*, const char*, bool>{"fn(ref int)->int", "fn(refint)->int", false},
	         std::tuple<const char*, const char*, bool>{"fn(refint)->int", "fn(ref int)->int", false},
	         std::tuple<const char*, const char*, bool>{"fn(ref int)->int", "fn(ref int)->int", true},
	         std::tuple<const char*, const char*, bool>{"int", "ref int", false},
	         std::tuple<const char*, const char*, bool>{"ref int", "int", false},
	         std::tuple<const char*, const char*, bool>{"ref int", "const ref int", false},
	         std::tuple<const char*, const char*, bool>{"int", "int...", false},
	         std::tuple<const char*, const char*, bool>{"int...", "int", false}})
	{
		const std::string Source = std::string("using Integer=int;\nstruct refint{};\nclass Secret{\nfriend Secret Create(") + FriendType + " value);\nprivate:\nSecret(int hidden){}\npublic:\nSecret(bool visible){}\n};\n" + "Secret Create(" + FunctionType + " value){return Secret(1);}\n";
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "Secret(1)", Position), "locate friend constructor call"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("Secret(").size());
		const auto Help = VeroScriptCallHelp::HelpAtSource(Source, Position, "<constructor-friend-identity>");
		bool bHidden = false, bVisible = false;
		for (const auto& Signature : Help.Signatures)
		{
			bHidden |= Signature.Label.find("hidden") != std::string::npos;
			bVisible |= Signature.Label.find("visible") != std::string::npos;
		}
		if (!Expect(Help.bSuccess && Help.bActive && bVisible && bHidden == bFriend && Help.Signatures.size() == (bFriend ? 2u : 1u),
		        "constructor access matches reference, const, variadic and aliased friend parameters"))
		{
			return EXIT_FAILURE;
		}
	}

	for (const auto& [FriendName, FunctionNamespace, bFriend] : {
	         std::tuple<const char*, const char*, bool>{"Create", "", true},
	         std::tuple<const char*, const char*, bool>{"Create", "Other", false},
	         std::tuple<const char*, const char*, bool>{"Trusted::Create", "Trusted", true},
	         std::tuple<const char*, const char*, bool>{"Trusted::Create", "Other", false}})
	{
		std::string Source = std::string("namespace Trusted{}\nclass Secret{friend Secret ") + FriendName + "(int value);private:Secret(int hidden){}public:Secret(bool visible){}};\n";
		if (*FunctionNamespace)
		{
			Source += std::string("namespace ") + FunctionNamespace + "{";
		}
		Source += "Secret Create(int value){return Secret(true);}";
		if (*FunctionNamespace)
		{
			Source += '}';
		}
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "Secret(true)", Position), "locate namespaced friend constructor call"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("Secret(").size());
		const auto Help = VeroScriptCallHelp::HelpAtSource(Source, Position, "<constructor-friend-namespace>");
		if (!Expect(Help.bSuccess && Help.bActive && Help.Signatures.size() == (bFriend ? 2u : 1u),
		        "friendship follows the complete function identity instead of a shared namespace leaf"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::string Source =
		    "namespace Trusted{class Secret{friend Secret Create(int value);private:Secret(int hidden){}"
		    "public:Secret(bool visible){}};Secret Create(int value){return Secret(true);}}";
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "Secret(true)", Position), "locate friend in the declaring namespace"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("Secret(").size());
		const auto Help = VeroScriptCallHelp::HelpAtSource(Source, Position, "<constructor-friend-declaring-namespace>");
		if (!Expect(Help.bSuccess && Help.bActive && Help.Signatures.size() == 2,
		        "unqualified friend declarations retain the declaring namespace"))
		{
			std::cerr << "success=" << Help.bSuccess << ", active=" << Help.bActive
			          << ", signatures=" << Help.Signatures.size() << '\n';
			for (const auto& Signature : Help.Signatures)
			{
				std::cerr << Signature.Label << '\n';
			}
			return EXIT_FAILURE;
		}
	}

	for (const auto& [bExported, bProjectWide, bFriend] : {
	         std::tuple<bool, bool, bool>{false, false, false},
	         std::tuple<bool, bool, bool>{true, false, true},
	         std::tuple<bool, bool, bool>{false, true, true}})
	{
		const std::string DeclarationPath = "friend-module-visibility/declarations.vs";
		const std::string FunctionPath = "friend-module-visibility/functions.vs";
		const std::string DeclarationSource =
		    "import \"functions.vs\";\nexport class Secret{friend Secret Create();"
		    "private:Secret(int hidden){}public:Secret(bool visible){}};\n";
		const std::string FunctionSource = std::string("import \"declarations.vs\";\n") + (bExported ? "export " : "") + "Secret Create(){return Secret(true);}\n";
		FSignatureHelpOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.bAllowCircularImports = true;
		Options.bAllowProjectWideAccess = bProjectWide;
		Options.SourceOverlays = {{DeclarationPath, DeclarationSource}, {FunctionPath, FunctionSource}};
		if (bProjectWide)
		{
			Options.ImplicitImportPaths = {DeclarationPath};
		}
		FTextPosition Position;
		if (!Expect(FindPosition(FunctionSource, "Secret(true)", Position), "locate cross-module friend call"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("Secret(").size());
		const auto Help = VeroScriptCallHelp::HelpAtSource(FunctionSource, Position, FunctionPath, Options);
		bool bHidden = false, bVisible = false;
		for (const auto& Signature : Help.Signatures)
		{
			bHidden |= Signature.Label.find("hidden") != std::string::npos;
			bVisible |= Signature.Label.find("visible") != std::string::npos;
		}
		if (!Expect(Help.bSuccess && Help.bActive && bVisible && bHidden == bFriend && Help.Signatures.size() == (bFriend ? 2u : 1u),
		        "friend functions obey export and project-wide visibility from the granting module"))
		{
			return EXIT_FAILURE;
		}
	}

	for (const char* Suffix : {"Secret global=Secret(true);", "\nSecret global=Secret(true);\n"})
	{
		const std::string Source = std::string("class Secret{public:Secret(bool visible){}private:Secret(int hidden){}};") + Suffix;
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "Secret(true)", Position), "locate global constructor call"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("Secret(").size());
		const auto Help = VeroScriptCallHelp::HelpAtSource(Source, Position, "<global-constructor-access>");
		if (!Expect(Help.bSuccess && Help.bActive && Help.Signatures.size() == 1 && Help.Signatures.front().Label.find("visible") != std::string::npos,
		        "constructor access does not escape the preceding callable's source range"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::string Source =
		    "class Secret{friend class Owner;private:Secret(int hidden){}public:Secret(bool visible){}};\n"
		    "class Owner{public:Secret item=Secret(1);};\n";
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "Secret(1)", Position), "locate friend type field initializer"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view("Secret(").size());
		const auto Help = VeroScriptCallHelp::HelpAtSource(Source, Position, "<field-initializer-access>");
		if (!Expect(Help.bSuccess && Help.bActive && Help.Signatures.size() == 2,
		        "field initializers retain their declaring type's access outside callable bodies"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::string Source =
		    "class Base{private:Base(string hidden){}protected:Base(int guarded){}public:Base(bool visible){}};\n"
		    "class Derived:public Base{int marker;public:Derived(int actual_value);};\n"
		    "Derived::Derived(int actual_value):Base(actual_value),marker(actual_value){}\n";
		FTextPosition Position;
		constexpr std::string_view Initializer = "Derived::Derived(int actual_value):Base(";
		if (!Expect(FindPosition(Source, Initializer, Position), "locate out-of-class protected base initializer"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(Initializer.size());
		const auto Help = VeroScriptCallHelp::HelpAtSource(Source, Position, "<out-of-class-constructor-access>");
		bool bGuarded = false, bVisible = false, bHidden = false;
		for (const auto& Signature : Help.Signatures)
		{
			bGuarded |= Signature.Label.find("guarded") != std::string::npos;
			bVisible |= Signature.Label.find("visible") != std::string::npos;
			bHidden |= Signature.Label.find("hidden") != std::string::npos;
		}
		if (!Expect(Help.bSuccess && Help.bActive && Help.Signatures.size() == 2 && bGuarded && bVisible && !bHidden,
		        "out-of-class constructor initializers retain the derived type's protected access"))
		{
			std::cerr << "success=" << Help.bSuccess << ", active=" << Help.bActive
			          << ", signatures=" << Help.Signatures.size() << ", guarded=" << bGuarded
			          << ", visible=" << bVisible << ", hidden=" << bHidden << '\n';
			for (const auto& Signature : Help.Signatures)
			{
				std::cerr << Signature.Label << '\n';
			}
			return EXIT_FAILURE;
		}
	}

	for (const char* Suffix : {",marker(actual_value){}", " /* keep */ ,marker(actual_value){}",
	         " // keep\n,marker(actual_value){}", " /* keep */ {}"})
	{
		const std::string Source = std::string(
		                               "class Base{private:Base(string hidden){}protected:Base(int guarded,int second){}public:Base(bool visible){}};\n"
		                               "class Derived:public Base{int marker;public:Derived(int actual_value);};\n"
		                               "Derived::Derived(int actual_value):Base(actual_value,)") +
		    Suffix;
		FTextPosition Position;
		if (!Expect(FindPosition(Source, ":Base(actual_value,", Position), "locate incomplete base initializer argument"))
		{
			return EXIT_FAILURE;
		}
		Position.Column += static_cast<std::uint32_t>(std::string_view(":Base(actual_value,").size());
		const auto Help = VeroScriptCallHelp::HelpAtSource(Source, Position, "<constructor-initializer-recovery>");
		if (!Expect(Help.bSuccess && Help.bActive && Help.ActiveParameter == 1 && Help.Signatures.size() == 2 && Help.ActiveSignature < Help.Signatures.size() && Help.Signatures[Help.ActiveSignature].Label.find("guarded") != std::string::npos,
		        "incomplete constructor arguments preserve later initializers, comments and the body"))
		{
			std::cerr << "suffix=" << Suffix << ", success=" << Help.bSuccess << ", active=" << Help.bActive
			          << ", signatures=" << Help.Signatures.size() << ", parameter=" << Help.ActiveParameter << '\n';
			for (const auto& Signature : Help.Signatures)
			{
				std::cerr << Signature.Label << '\n';
			}
			return EXIT_FAILURE;
		}
	}

	if (!Expect(Detail::FormatStandardSourceSpelling(
	                "std.memory.view f(std.memory.buffer bytes, string text = \"std.memory.buffer\", double scale = 1.25) /* std.memory.view */") == "std::memory::view f(std::memory::buffer bytes, string text = \"std.memory.buffer\", double scale = 1.25) /* std.memory.view */",
	        "standard signature spelling preserves literals, comments and numeric defaults"))
	{
		return EXIT_FAILURE;
	}

	for (const std::string TypeName : {"handle", "shared", "std::handle", "std::shared", "std.handle", "std.shared"})
	{
		const std::string Call = TypeName + "<int>(";
		const std::string SharedSource = "void main() { shared<int> owner = " + Call + "7); }";
		FTextPosition SharedPosition;
		if (!Expect(FindPosition(SharedSource, Call, SharedPosition), "locate shared/handle constructor"))
		{
			return EXIT_FAILURE;
		}
		SharedPosition.Column += static_cast<std::uint32_t>(Call.size());
		const auto Help = VeroScriptCallHelp::HelpAtSource(SharedSource, SharedPosition, "<shared-call-help>");
		bool bHasPayloadConstructor = false;
		for (const auto& Signature : Help.Signatures)
		{
			bHasPayloadConstructor = bHasPayloadConstructor || (Signature.Parameters.size() == 1 && Signature.Label.find("int") != std::string::npos);
		}
		if (!Expect(Help.bSuccess && Help.bActive && !Help.Signatures.empty() && bHasPayloadConstructor,
		        "shared/handle constructor help substitutes its payload type"))
		{
			std::cerr << TypeName << " success=" << Help.bSuccess << " active=" << Help.bActive << " signatures=" << Help.Signatures.size() << '\n';
			for (const auto& Signature : Help.Signatures)
			{
				std::cerr << Signature.Label << '\n';
			}
			return EXIT_FAILURE;
		}
	}

	constexpr const char* Source = R"(
int Add(int a, int b) {
	return a + b;
}

void main() {
	Add(1,
}
)";

	FTextPosition Position;
	if (!Expect(FindPosition(Source, "Add(1,", Position), "locate Add(1,"))
	{
		return EXIT_FAILURE;
	}
	Position.Column += static_cast<std::uint32_t>(std::string_view("Add(1,").size());

	const FSignatureHelpResult Result = VeroScriptCallHelp::HelpAtSource(Source, Position, "<sig>");
	if (!Expect(Result.bSuccess && Result.bActive, "signature help active"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(Result.Signatures.size() == 1, "one signature"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(Result.ActiveParameter == 1, "active parameter is second"))
	{
		std::cerr << "active=" << Result.ActiveParameter << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(
	        Result.Signatures.front().Parameters.size() == 2 && Result.Signatures.front().Label.find("Add") != std::string::npos,
	        "Add has two params"))
	{
		std::cerr << "label=[" << Result.Signatures.front().Label << "]\n";
		return EXIT_FAILURE;
	}

	constexpr const char* GenericSource = R"(
class PairBox<T, U> { public: T first;
	U second; }

void Use(PairBox<int, string> a, int b) {}

void main() {
	Use(null,
}
)";

	{
		FTextPosition GenericPosition;
		if (!Expect(
		        FindPosition(GenericSource, "Use(null,", GenericPosition),
		        "locate Use(null,"))
		{
			return EXIT_FAILURE;
		}
		GenericPosition.Column +=
		    static_cast<std::uint32_t>(std::string_view("Use(null,").size());

		const FSignatureHelpResult GenericResult =
		    VeroScriptCallHelp::HelpAtSource(GenericSource, GenericPosition, "<sig-generic>");
		if (!Expect(GenericResult.bSuccess && GenericResult.bActive, "generic sig active"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(GenericResult.ActiveParameter == 1, "active param after generic first arg"))
		{
			std::cerr << "active=" << GenericResult.ActiveParameter << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
		        GenericResult.Signatures.size() == 1 && GenericResult.Signatures.front().Parameters.size() == 2,
		        "generic param list stays two slots"))
		{
			std::cerr << "params="
			          << (GenericResult.Signatures.empty()
			                     ? 0
			                     : GenericResult.Signatures.front().Parameters.size())
			          << '\n';
			return EXIT_FAILURE;
		}
	}

	constexpr const char* GenericCallSource = R"(
void Make<T>(T value, int n) {}

void main() {
	Make<int>(1,
}
)";

	{
		FTextPosition TypeArgPosition;
		if (!Expect(
		        FindPosition(GenericCallSource, "Make<int>(1,", TypeArgPosition),
		        "locate Make<int>"))
		{
			return EXIT_FAILURE;
		}
		TypeArgPosition.Column +=
		    static_cast<std::uint32_t>(std::string_view("Make<int>(1,").size());
		const FSignatureHelpResult TypeArgResult =
		    VeroScriptCallHelp::HelpAtSource(
		        GenericCallSource,
		        TypeArgPosition,
		        "<sig-typeargs>");
		if (!Expect(TypeArgResult.bSuccess && TypeArgResult.bActive, "Make<int> signature active"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(TypeArgResult.ActiveParameter == 1, "second arg after Make<int>(1,"))
		{
			std::cerr << "active=" << TypeArgResult.ActiveParameter << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
		        !TypeArgResult.Signatures.empty() && TypeArgResult.Signatures.front().Label.find("Make") != std::string::npos,
		        "Make signature found despite type args"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* CtorSource = R"(
struct Point {
	int x;
	Point(int v) { x = v; }
	Point(string label) { x = 0; }
}

void main() {
	Point p = Point(
}
)";

	{
		FTextPosition CtorPosition;
		if (!Expect(FindPosition(CtorSource, "Point p = Point(", CtorPosition), "locate Point("))
		{
			return EXIT_FAILURE;
		}
		CtorPosition.Column +=
		    static_cast<std::uint32_t>(std::string_view("Point p = Point(").size());
		const FSignatureHelpResult CtorResult =
		    VeroScriptCallHelp::HelpAtSource(CtorSource, CtorPosition, "<sig-ctor>");
		if (!Expect(CtorResult.bSuccess && CtorResult.bActive, "ctor signature active"))
		{
			std::cerr << "sigs=" << CtorResult.Signatures.size()
			          << " active=" << CtorResult.bActive << '\n';
			if (!CtorResult.Diagnostics.empty())
			{
				std::cerr << CtorResult.Diagnostics.front().Message << '\n';
			}
			return EXIT_FAILURE;
		}
		if (!Expect(
		        CtorResult.Signatures.size() == 2 && CtorResult.Signatures.front().Parameters.size() == 1 && CtorResult.Signatures.back().Parameters.size() == 1,
		        "Point constructor type overloads"))
		{
			return EXIT_FAILURE;
		}
		bool bHasIntCtor = false;
		bool bHasStringCtor = false;
		for (const FSignatureInformation& Signature : CtorResult.Signatures)
		{
			bHasIntCtor = bHasIntCtor || Signature.Label.find("int") != std::string::npos;
			bHasStringCtor =
			    bHasStringCtor || Signature.Label.find("string") != std::string::npos;
		}
		if (!Expect(bHasIntCtor && bHasStringCtor, "constructor overload parameter types"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* DirectCtorSource = R"(
struct Point {
	int x;
	Point(int x, int y) { this.x = x + y; }
}

void main() {
	Point point(1,
}
)";

	{
		FTextPosition CtorPosition;
		if (!Expect(
		        FindPosition(DirectCtorSource, "Point point(1,", CtorPosition),
		        "locate direct-initializer constructor"))
		{
			return EXIT_FAILURE;
		}
		CtorPosition.Column +=
		    static_cast<std::uint32_t>(std::string_view("Point point(1,").size());
		const FSignatureHelpResult CtorResult =
		    VeroScriptCallHelp::HelpAtSource(
		        DirectCtorSource,
		        CtorPosition,
		        "<sig-direct-ctor>");
		if (!Expect(
		        CtorResult.bSuccess && CtorResult.bActive && CtorResult.ActiveParameter == 1 && !CtorResult.Signatures.empty() && CtorResult.Signatures.front().Label.find("Point") != std::string::npos,
		        "direct-initializer constructor signature help"))
		{
			std::cerr << "success=" << CtorResult.bSuccess
			          << " active=" << CtorResult.bActive
			          << " parameter=" << CtorResult.ActiveParameter
			          << " sigs=" << CtorResult.Signatures.size() << '\n';
			for (const FDiagnostic& Diagnostic : CtorResult.Diagnostics)
			{
				std::cerr << Diagnostic.Message << '\n';
			}
			return EXIT_FAILURE;
		}
	}

	constexpr const char* GenericDirectCtorSource = R"(
struct Box<T> {
	T value;
	Box(T first, T second) { value = first; }
}

void main() {
	Box<int> box(1,
}
)";

	{
		FTextPosition CtorPosition;
		if (!Expect(
		        FindPosition(GenericDirectCtorSource, "Box<int> box(1,", CtorPosition),
		        "locate generic direct-initializer constructor"))
		{
			return EXIT_FAILURE;
		}
		CtorPosition.Column +=
		    static_cast<std::uint32_t>(std::string_view("Box<int> box(1,").size());
		const FSignatureHelpResult CtorResult =
		    VeroScriptCallHelp::HelpAtSource(
		        GenericDirectCtorSource,
		        CtorPosition,
		        "<sig-generic-direct-ctor>");
		if (!Expect(
		        CtorResult.bSuccess && CtorResult.bActive && CtorResult.ActiveParameter == 1 && !CtorResult.Signatures.empty() && CtorResult.Signatures.front().Parameters.size() == 2,
		        "generic direct-initializer constructor signature help"))
		{
			std::cerr << "success=" << CtorResult.bSuccess
			          << " active=" << CtorResult.bActive
			          << " parameter=" << CtorResult.ActiveParameter
			          << " sigs=" << CtorResult.Signatures.size() << '\n';
			return EXIT_FAILURE;
		}
	}

	constexpr const char* BraceCtorSource = R"(
struct Point {
	int x;
	Point(int v, string label) { x = v; }
}

void main() {
	Point p = Point{1,
}
)";

	{
		FTextPosition CtorPosition;
		if (!Expect(
		        FindPosition(BraceCtorSource, "Point p = Point{1,", CtorPosition),
		        "locate Point brace constructor"))
		{
			return EXIT_FAILURE;
		}
		CtorPosition.Column +=
		    static_cast<std::uint32_t>(std::string_view("Point p = Point{1,").size());
		const FSignatureHelpResult CtorResult =
		    VeroScriptCallHelp::HelpAtSource(
		        BraceCtorSource,
		        CtorPosition,
		        "<sig-brace-ctor>");
		if (!Expect(
		        CtorResult.bSuccess && CtorResult.bActive && CtorResult.ActiveParameter == 1,
		        "brace constructor signature help active on second parameter"))
		{
			std::cerr << "success=" << CtorResult.bSuccess
			          << " active=" << CtorResult.bActive
			          << " parameter=" << CtorResult.ActiveParameter
			          << " sigs=" << CtorResult.Signatures.size() << '\n';
			for (const FDiagnostic& Diagnostic : CtorResult.Diagnostics)
			{
				std::cerr << Diagnostic.Message << '\n';
			}
			return EXIT_FAILURE;
		}
		if (!Expect(
		        !CtorResult.Signatures.empty() && CtorResult.Signatures.front().Label.find("Point") != std::string::npos,
		        "brace constructor signature found"))
		{
			return EXIT_FAILURE;
		}
	}

	// Same-arity constructor overloads: pick by literal argument type.
	{
		constexpr const char* TypedCtorSource = R"(
struct Point {
	int x;
	Point(int v) { x = v; }
	Point(string label) { x = 0; }
}

void main() {
	Point a = Point("hi");
	Point b = Point(7);
}
)";
		FTextPosition StringPos;
		if (!Expect(
		        FindPosition(TypedCtorSource, "Point(\"hi\")", StringPos),
		        "locate string ctor call"))
		{
			return EXIT_FAILURE;
		}
		StringPos.Column +=
		    static_cast<std::uint32_t>(std::string_view("Point(\"").size());
		const FSignatureHelpResult StringHelp =
		    VeroScriptCallHelp::HelpAtSource(TypedCtorSource, StringPos, "<sig-ctor-str>");
		if (!Expect(
		        StringHelp.bSuccess && StringHelp.bActive && StringHelp.Signatures.size() == 2 && StringHelp.ActiveSignature < StringHelp.Signatures.size() && StringHelp.Signatures[StringHelp.ActiveSignature].Label.find("string") != std::string::npos,
		        "string literal prefers string ctor signature"))
		{
			std::cerr << "active=" << StringHelp.ActiveSignature << '\n';
			return EXIT_FAILURE;
		}

		FTextPosition IntPos;
		if (!Expect(
		        FindPosition(TypedCtorSource, "Point(7)", IntPos),
		        "locate int ctor call"))
		{
			return EXIT_FAILURE;
		}
		IntPos.Column += static_cast<std::uint32_t>(std::string_view("Point(").size());
		const FSignatureHelpResult IntHelp =
		    VeroScriptCallHelp::HelpAtSource(TypedCtorSource, IntPos, "<sig-ctor-int>");
		if (!Expect(
		        IntHelp.bSuccess && IntHelp.bActive && IntHelp.Signatures.size() == 2 && IntHelp.ActiveSignature < IntHelp.Signatures.size() && IntHelp.Signatures[IntHelp.ActiveSignature].Label.find("int") != std::string::npos,
		        "int literal prefers int ctor signature"))
		{
			std::cerr << "active=" << IntHelp.ActiveSignature << '\n';
			return EXIT_FAILURE;
		}
	}

	// Character literals select char overloads rather than falling back to source order.
	{
		constexpr const char* CharacterOverloadSource = R"(
void select(char value) {}
void select(string value) {}
void main() { select('A'); }
)";
		FTextPosition CharacterPos;
		if (!Expect(
		        FindPosition(CharacterOverloadSource, "select('A')", CharacterPos),
		        "locate character overload call"))
		{
			return EXIT_FAILURE;
		}
		CharacterPos.Column += static_cast<std::uint32_t>(std::string_view("select('").size());
		const FSignatureHelpResult CharacterHelp = VeroScriptCallHelp::HelpAtSource(
		    CharacterOverloadSource,
		    CharacterPos,
		    "<sig-character-overload>");
		if (!Expect(
		        CharacterHelp.bSuccess && CharacterHelp.bActive && CharacterHelp.Signatures.size() == 2 && CharacterHelp.ActiveSignature < CharacterHelp.Signatures.size() && CharacterHelp.Signatures[CharacterHelp.ActiveSignature].Label.find("char") != std::string::npos,
		        "character literal prefers char overload signature"))
		{
			return EXIT_FAILURE;
		}
	}

	// Destructors must not appear as extra constructor signatures.
	{
		constexpr const char* DtorCtorSource = R"(
class Owner {
public:
	Owner() {}
	virtual ~Owner() {}
}

void main() {
	Owner o = Owner(
}
)";
		FTextPosition Pos;
		if (!Expect(FindPosition(DtorCtorSource, "Owner o = Owner(", Pos), "locate Owner("))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("Owner o = Owner(").size());
		const FSignatureHelpResult Help =
		    VeroScriptCallHelp::HelpAtSource(DtorCtorSource, Pos, "<sig-no-dtor>");
		if (!Expect(Help.bSuccess && Help.bActive, "Owner ctor help active"))
		{
			return EXIT_FAILURE;
		}
		for (const FSignatureInformation& Signature : Help.Signatures)
		{
			if (Signature.Label.find('~') != std::string::npos)
			{
				return Fail("destructor must not appear in constructor signature help");
			}
		}
		if (!Expect(Help.Signatures.size() == 1, "only one Owner constructor signature"))
		{
			std::cerr << "sigs=" << Help.Signatures.size() << '\n';
			return EXIT_FAILURE;
		}
	}

	constexpr const char* FnLocalSource = R"(
void main() {
	function(int)->int inc = function(int x) -> int => x + 1;
	inc(
}
)";

	{
		FTextPosition FnPosition;
		if (!Expect(FindPosition(FnLocalSource, "\tinc(", FnPosition), "locate inc("))
		{
			return EXIT_FAILURE;
		}
		FnPosition.Column += static_cast<std::uint32_t>(std::string_view("\tinc(").size());
		const FSignatureHelpResult FnResult =
		    VeroScriptCallHelp::HelpAtSource(FnLocalSource, FnPosition, "<sig-fn>");
		if (!Expect(FnResult.bSuccess && FnResult.bActive, "function alias local signature active"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        !FnResult.Signatures.empty() && FnResult.Signatures.front().Parameters.size() == 1,
		        "fn(int) one param"))
		{
			std::cerr << "label=["
			          << (FnResult.Signatures.empty() ? "" : FnResult.Signatures.front().Label)
			          << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* RefFunctionSource = R"(
void main() {
	function(ref int)->void mutate = function(ref int value) -> void {
		value = value + 1;
	};
	mutate(
}
)";
		FTextPosition RefFunctionPosition;
		if (!Expect(
		        FindPosition(RefFunctionSource, "\tmutate(", RefFunctionPosition),
		        "locate ref function invocation"))
		{
			return EXIT_FAILURE;
		}
		RefFunctionPosition.Column +=
		    static_cast<std::uint32_t>(std::string_view("\tmutate(").size());
		const FSignatureHelpResult RefFunctionHelp = VeroScriptCallHelp::HelpAtSource(
		    RefFunctionSource, RefFunctionPosition, "<sig-ref-function>");
		if (!Expect(
		        RefFunctionHelp.bSuccess && RefFunctionHelp.bActive && !RefFunctionHelp.Signatures.empty() && RefFunctionHelp.Signatures.front().Parameters.size() == 1 && RefFunctionHelp.Signatures.front().Parameters.front().Label == "ref int",
		        "function ref parameter signature help"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* ReturnedFunctionSource = R"(
function(int)->int make_callback() {
	return function(int value) -> int => value + 1;
}
void main() {
	make_callback()(
}
)";
		FTextPosition ReturnedFunctionPosition;
		if (!Expect(
		        FindPosition(
		            ReturnedFunctionSource,
		            "\tmake_callback()(",
		            ReturnedFunctionPosition),
		        "locate returned script function invocation"))
		{
			return EXIT_FAILURE;
		}
		ReturnedFunctionPosition.Column += static_cast<std::uint32_t>(
		    std::string_view("\tmake_callback()(").size());
		const FSignatureHelpResult ReturnedFunctionHelp =
		    VeroScriptCallHelp::HelpAtSource(
		        ReturnedFunctionSource,
		        ReturnedFunctionPosition,
		        "<sig-returned-function>");
		if (!Expect(
		        ReturnedFunctionHelp.bSuccess && ReturnedFunctionHelp.bActive && !ReturnedFunctionHelp.Signatures.empty() && ReturnedFunctionHelp.Signatures.front().Parameters.size() == 1,
		        "returned script function retains invocation signature"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* ReserveSource = R"(
void main() {
	array<int> values = array<int>();
	values.reserve(
}
)";
		FTextPosition ReservePosition;
		if (!Expect(
		        FindPosition(ReserveSource, "values.reserve(", ReservePosition),
		        "locate array reserve call"))
		{
			return EXIT_FAILURE;
		}
		ReservePosition.Column +=
		    static_cast<std::uint32_t>(std::string_view("values.reserve(").size());
		const FSignatureHelpResult ReserveHelp = VeroScriptCallHelp::HelpAtSource(
		    ReserveSource, ReservePosition, "<sig-array-reserve>");
		if (!Expect(
		        ReserveHelp.bSuccess && ReserveHelp.bActive && !ReserveHelp.Signatures.empty() && ReserveHelp.Signatures.front().Parameters.size() == 1 && ReserveHelp.Signatures.front().Parameters.front().Label == "int64 capacity",
		        "array reserve signature help"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* CallOpSource = R"(
class Adder { public: int base;
	Adder(int b) { base = b; }
	int operator()(int n) { return base + n; } }

void main() {
	Adder f = Adder(1);
	f(
}
)";

	{
		FTextPosition CallOpPosition;
		if (!Expect(FindPosition(CallOpSource, "\tf(", CallOpPosition), "locate f("))
		{
			return EXIT_FAILURE;
		}
		CallOpPosition.Column += static_cast<std::uint32_t>(std::string_view("\tf(").size());
		const FSignatureHelpResult CallOpResult =
		    VeroScriptCallHelp::HelpAtSource(CallOpSource, CallOpPosition, "<sig-callop>");
		if (!Expect(CallOpResult.bSuccess && CallOpResult.bActive, "operator() signature active"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        !CallOpResult.Signatures.empty() && CallOpResult.Signatures.front().Label.find("operator()") != std::string::npos,
		        "operator() label"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* HostSigSource = R"(
void main() {
	print("x={}",
}
)";

	{
		FTextPosition HostPos;
		if (!Expect(FindPosition(HostSigSource, "print(\"x={}\",", HostPos), "locate print"))
		{
			return EXIT_FAILURE;
		}
		HostPos.Column +=
		    static_cast<std::uint32_t>(std::string_view("print(\"x={}\",").size());
		const FSignatureHelpResult HostResult =
		    VeroScriptCallHelp::HelpAtSource(HostSigSource, HostPos, "<sig-host>");
		if (!Expect(
		        HostResult.bSuccess && HostResult.bActive && HostResult.ActiveSignature < HostResult.Signatures.size() && HostResult.Signatures[HostResult.ActiveSignature].Label.find("string fmt, ...") != std::string::npos,
		        "multi-argument print selects format signature"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* RawPrintSource = R"(
void main() {
	print(
}
)";
		FTextPosition RawPrintPos;
		if (!Expect(
		        FindPosition(RawPrintSource, "print(", RawPrintPos),
		        "locate raw print"))
		{
			return EXIT_FAILURE;
		}
		RawPrintPos.Column +=
		    static_cast<std::uint32_t>(std::string_view("print(").size());
		const FSignatureHelpResult RawPrintResult =
		    VeroScriptCallHelp::HelpAtSource(
		        RawPrintSource,
		        RawPrintPos,
		        "<sig-raw-print>");
		if (!Expect(
		        RawPrintResult.bSuccess && RawPrintResult.bActive && RawPrintResult.ActiveSignature < RawPrintResult.Signatures.size() && RawPrintResult.Signatures[RawPrintResult.ActiveSignature].Label.find("any value") != std::string::npos,
		        "single-argument print selects raw value signature"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* FormatSigSource = R"(
void main() {
	format("x={}",
}
)";

	{
		FTextPosition FormatPos;
		if (!Expect(FindPosition(FormatSigSource, "format(\"x={}\",", FormatPos), "locate format"))
		{
			return EXIT_FAILURE;
		}
		FormatPos.Column +=
		    static_cast<std::uint32_t>(std::string_view("format(\"x={}\",").size());
		const FSignatureHelpResult FormatResult =
		    VeroScriptCallHelp::HelpAtSource(FormatSigSource, FormatPos, "<sig-format>");
		if (!Expect(FormatResult.bSuccess && FormatResult.bActive, "format signature active"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* CtorInitSource = R"(
class Base { public: int v;
	Base(int x) { v = x; } }

class Child : public Base { public: Child(int x) : Base( }
)";

	{
		FTextPosition CtorPos;
		if (!Expect(FindPosition(CtorInitSource, ": Base(", CtorPos), "locate Base("))
		{
			return EXIT_FAILURE;
		}
		CtorPos.Column += static_cast<std::uint32_t>(std::string_view(": Base(").size());
		const FSignatureHelpResult CtorResult =
		    VeroScriptCallHelp::HelpAtSource(CtorInitSource, CtorPos, "<sig-ctor-init>");
		if (!Expect(CtorResult.bSuccess && CtorResult.bActive, "ctor base-init signature active"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        !CtorResult.Signatures.empty() && CtorResult.Signatures.front().Label.find("Base") != std::string::npos,
		        "ctor base-init label mentions Base"))
		{
			std::cerr << "label=["
			          << (CtorResult.Signatures.empty() ? "" : CtorResult.Signatures.front().Label)
			          << "]\n";
			return EXIT_FAILURE;
		}
	}

	constexpr const char* FieldCtorInitSource = R"(
struct Payload { Payload(int left, int right) {} }
class Base { public: Base() {} }
class Holder : public Base { public:
	const Payload payload;
	Holder(int value) : Base(), payload(value, }
)";

	{
		FTextPosition FieldCtorPos;
		if (!Expect(
		        FindPosition(FieldCtorInitSource, "payload(value,", FieldCtorPos),
		        "locate constructor field initializer"))
		{
			return EXIT_FAILURE;
		}
		FieldCtorPos.Column += static_cast<std::uint32_t>(
		    std::string_view("payload(value,").size());
		const FSignatureHelpResult FieldCtorResult =
		    VeroScriptCallHelp::HelpAtSource(
		        FieldCtorInitSource,
		        FieldCtorPos,
		        "<sig-field-ctor-init>");
		if (!Expect(
		        FieldCtorResult.bSuccess && FieldCtorResult.bActive && FieldCtorResult.ActiveParameter == 1 && !FieldCtorResult.Signatures.empty() && FieldCtorResult.Signatures.front().Label.find("payload(") == 0 && FieldCtorResult.Signatures.front().Parameters.size() == 2,
		        "constructor field initializer signature help"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* OutOfClassFieldCtorInitSource = R"(
struct Payload { Payload(int left, int right) {} }
struct External { const Payload payload; External(int value); }
External::External(int value) : payload(value,
)";

	{
		FTextPosition FieldCtorPos;
		if (!Expect(
		        FindPosition(
		            OutOfClassFieldCtorInitSource, "payload(value,", FieldCtorPos),
		        "locate out-of-class constructor field initializer"))
		{
			return EXIT_FAILURE;
		}
		FieldCtorPos.Column += static_cast<std::uint32_t>(
		    std::string_view("payload(value,").size());
		const FSignatureHelpResult FieldCtorResult =
		    VeroScriptCallHelp::HelpAtSource(
		        OutOfClassFieldCtorInitSource,
		        FieldCtorPos,
		        "<sig-out-of-class-field-ctor-init>");
		if (!Expect(
		        FieldCtorResult.bSuccess && FieldCtorResult.bActive && FieldCtorResult.ActiveParameter == 1 && !FieldCtorResult.Signatures.empty() && FieldCtorResult.Signatures.front().Label.find("payload(") == 0,
		        "out-of-class field initializer signature help"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::error_code RemoveError;
		const std::filesystem::path TempRoot =
		    std::filesystem::temp_directory_path() / "VeroScriptTooling-format-sig";
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot / "lib");
		{
			std::ofstream Lib(TempRoot / "lib" / "string.vs");
			Lib << "export string format(string fmt, array<string> args) { return fmt; }\n";
		}
		constexpr const char* FormatImportSource = R"(
import "string.vs";
void main() {
	format("x={}",
}
)";
		FTextPosition FormatPos;
		if (!Expect(
		        FindPosition(FormatImportSource, "format(\"x={}\",", FormatPos),
		        "locate imported format"))
		{
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}
		FormatPos.Column +=
		    static_cast<std::uint32_t>(std::string_view("format(\"x={}\",").size());
		const std::string LibPath = (TempRoot / "lib").string();
		const std::string AppPath = (TempRoot / "app.vs").string();
		FSignatureHelpOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.ImportPaths.push_back(LibPath);
		const FSignatureHelpResult FormatResult = VeroScriptCallHelp::HelpAtSource(
		    FormatImportSource,
		    FormatPos,
		    AppPath,
		    Options);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(FormatResult.bSuccess && FormatResult.bActive, "imported format signature active"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        FormatResult.ActiveSignature < FormatResult.Signatures.size() && FormatResult.Signatures[FormatResult.ActiveSignature].Label.find("...") != std::string::npos,
		        "imported format prefers soft varargs over array wrapper"))
		{
			std::cerr << "active label=["
			          << (FormatResult.Signatures.empty()
			                     ? ""
			                     : FormatResult.Signatures[FormatResult.ActiveSignature].Label)
			          << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		std::error_code RemoveError;
		const std::filesystem::path TempRoot =
		    std::filesystem::temp_directory_path() / "VeroScriptTooling-concurrent-sig";
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot / "lib");
		{
			std::ofstream Lib(TempRoot / "lib" / "concurrent.vs");
			Lib << "export void channel_send(channel<int> ch, int value) {}\n"
			       "export void channel_send(channel<string> ch, string value) {}\n"
			       "export int channel_recv(channel<int> ch) { return 0; }\n"
			       "export string channel_recv(channel<string> ch) { return \"\"; }\n"
			       "export int spawn(string name, channel<int> ch) { return 0; }\n"
			       "export int spawn(string name, channel<string> ch) { return 0; }\n";
		}
		constexpr const char* IntChannelSource = R"(
import "concurrent.vs";
void main() {
	channel<int> ch = channel<int>();
	channel_send(ch,
}
)";
		constexpr const char* StrChannelSource = R"(
import "concurrent.vs";
void main() {
	channel<string> cs = channel<string>();
	channel_send(cs,
}
)";
		FTextPosition IntPos;
		FTextPosition StrPos;
		if (!Expect(FindPosition(IntChannelSource, "channel_send(ch,", IntPos), "locate int channel_send"))
		{
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}
		IntPos.Column +=
		    static_cast<std::uint32_t>(std::string_view("channel_send(ch,").size());
		if (!Expect(FindPosition(StrChannelSource, "channel_send(cs,", StrPos), "locate string channel_send"))
		{
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}
		StrPos.Column +=
		    static_cast<std::uint32_t>(std::string_view("channel_send(cs,").size());
		const std::string LibPath = (TempRoot / "lib").string();
		const std::string AppPath = (TempRoot / "app.vs").string();
		FSignatureHelpOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.ImportPaths.push_back(LibPath);
		const FSignatureHelpResult IntResult = VeroScriptCallHelp::HelpAtSource(
		    IntChannelSource,
		    IntPos,
		    AppPath,
		    Options);
		const FSignatureHelpResult StrResult = VeroScriptCallHelp::HelpAtSource(
		    StrChannelSource,
		    StrPos,
		    AppPath,
		    Options);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(IntResult.bSuccess && IntResult.bActive, "channel_send int sig active"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(StrResult.bSuccess && StrResult.bActive, "channel_send string sig active"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        IntResult.ActiveSignature < IntResult.Signatures.size() && IntResult.Signatures[IntResult.ActiveSignature].Label.find("channel<int>") != std::string::npos,
		        "channel_send on int channel prefers int overload"))
		{
			std::cerr << "int label=["
			          << (IntResult.Signatures.empty()
			                     ? ""
			                     : IntResult.Signatures[IntResult.ActiveSignature].Label)
			          << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
		        StrResult.ActiveSignature < StrResult.Signatures.size() && StrResult.Signatures[StrResult.ActiveSignature].Label.find("channel<string>") != std::string::npos,
		        "channel_send on string channel prefers string overload"))
		{
			std::cerr << "string label=["
			          << (StrResult.Signatures.empty()
			                     ? ""
			                     : StrResult.Signatures[StrResult.ActiveSignature].Label)
			          << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		const std::string RandomSource =
		    "import \"random.vs\";\n"
		    "void main() { std::random::next_bounded_uint(array<unsigned int>(), 10u); }\n";
		FTextPosition RandomPosition;
		if (!Expect(
		        FindPosition(RandomSource, "next_bounded_uint(array<unsigned int>(),", RandomPosition),
		        "locate random bounded call"))
		{
			return EXIT_FAILURE;
		}
		RandomPosition.Column += static_cast<std::uint32_t>(
		    std::string_view("next_bounded_uint(array<unsigned int>(),").size());
		const FSignatureHelpResult Help = VeroScriptCallHelp::HelpAtSource(
		    RandomSource, RandomPosition, "<sig-random-bounded>");
		bool bHasBounded = false;
		for (const FSignatureInformation& Signature : Help.Signatures)
		{
			bHasBounded = bHasBounded || Signature.Label.find("unsigned int upper_exclusive") != std::string::npos;
		}
		if (!Expect(
		        Help.bSuccess && Help.bActive && bHasBounded,
		        "qualified bounded random signature remains precise"))
		{
			for (const FSignatureInformation& Signature : Help.Signatures)
			{
				std::cerr << "random signature=[" << Signature.Label << "]\n";
			}
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		VeroScript::FHostTypeId ActorId;
		VeroScript::FHostTypeId WidgetId;
		VeroScript::FHostTypeId OtherId;
		if (!Expect(
		        Registry.RegisterType("Widget", VeroScript::EHostTypeKind::Class, WidgetId, ErrorId) && Registry.RegisterType("Actor", VeroScript::EHostTypeKind::Class, ActorId, ErrorId) && Registry.RegisterType("Other", VeroScript::EHostTypeKind::Class, OtherId, ErrorId),
		        "register Actor/Widget/Other for host sighelp"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::FHostMemberDesc Ctor;
		Ctor.Kind = VeroScript::EHostMemberKind::Constructor;
		Ctor.ReturnType = VeroScript::ENativeType::HostObject;
		Ctor.ParamTypes = {VeroScript::ENativeType::Int};
		Ctor.ParamNames = {"value"};
		VeroScript::FHostMemberDesc TextCtor = Ctor;
		TextCtor.ParamTypes = {VeroScript::ENativeType::String};
		TextCtor.ParamNames = {"text"};
		VeroScript::FHostMemberDesc Attach;
		Attach.Kind = VeroScript::EHostMemberKind::Method;
		Attach.Name = "attach";
		Attach.ReturnType = VeroScript::ENativeType::Void;
		Attach.ParamTypes = {VeroScript::ENativeType::HostObject};
		Attach.ParamHostTypeNames = {"Widget"};
		VeroScript::FHostMemberDesc OtherAttach = Attach;
		OtherAttach.ParamTypes = {VeroScript::ENativeType::String};
		OtherAttach.ParamHostTypeNames.clear();
		if (!Expect(
		        Registry.RegisterMember(WidgetId, Ctor, ErrorId) && Registry.RegisterMember(WidgetId, TextCtor, ErrorId) && Registry.RegisterMember(ActorId, Attach, ErrorId) && Registry.RegisterMember(OtherId, OtherAttach, ErrorId),
		        "register overloaded Widget ctors / receiver-specific attach methods"))
		{
			return EXIT_FAILURE;
		}

		FSignatureHelpOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;

		constexpr const char* MethodSource = R"(
void foo(Actor a) {
	a.attach(
}
)";
		FTextPosition MethodPos;
		if (!Expect(FindPosition(MethodSource, "a.attach(", MethodPos), "locate a.attach("))
		{
			return EXIT_FAILURE;
		}
		MethodPos.Column += static_cast<std::uint32_t>(std::string_view("a.attach(").size());
		const FSignatureHelpResult MethodHelp = VeroScriptCallHelp::HelpAtSource(
		    MethodSource, MethodPos, "<sig-host-method>", Options);
		if (!Expect(
		        MethodHelp.bSuccess && MethodHelp.bActive && MethodHelp.Signatures.size() == 1 && MethodHelp.Signatures.front().Label.find("attach") != std::string::npos && MethodHelp.Signatures.front().Parameters.size() == 1,
		        "host method a.attach signature"))
		{
			std::cerr << "sigs=" << MethodHelp.Signatures.size()
			          << " label=["
			          << (MethodHelp.Signatures.empty() ? "" : MethodHelp.Signatures.front().Label)
			          << "]\n";
			return EXIT_FAILURE;
		}

		constexpr const char* ScopedMethodSource = R"(
namespace N {
	class Actor {
	public:
		void attach(int value) {}
	}
}
void foo(N::Actor a) {
	a::attach(1);
}
)";
		FTextPosition ScopedMethodPos;
		if (!Expect(
		        FindPosition(ScopedMethodSource, "a::attach(", ScopedMethodPos),
		        "locate scoped a::attach("))
		{
			return EXIT_FAILURE;
		}
		ScopedMethodPos.Column +=
		    static_cast<std::uint32_t>(std::string_view("a::attach(").size());
		const FSignatureHelpResult ScopedMethodHelp = VeroScriptCallHelp::HelpAtSource(
		    ScopedMethodSource,
		    ScopedMethodPos,
		    "<sig-host-scoped-method>",
		    Options);
		if (!Expect(
		        ScopedMethodHelp.bSuccess && ScopedMethodHelp.bActive && !ScopedMethodHelp.Signatures.empty() && ScopedMethodHelp.Signatures.front().Label.find("attach") != std::string::npos,
		        "host method on ::-qualified type resolves signature"))
		{
			std::cerr << "sigs=" << ScopedMethodHelp.Signatures.size()
			          << " label=["
			          << (ScopedMethodHelp.Signatures.empty()
			                     ? ""
			                     : ScopedMethodHelp.Signatures.front().Label)
			          << "]\n";
			return EXIT_FAILURE;
		}

		constexpr const char* ThisMethodSource = R"(
namespace N {
	class Actor {
	public:
		void attach(int value) {}
		void tick() {
			this.attach(1);
		}
	}
}
)";
		FTextPosition ThisMethodPos;
		if (!Expect(
		        FindPosition(ThisMethodSource, "this.attach(", ThisMethodPos),
		        "locate this.attach("))
		{
			return EXIT_FAILURE;
		}
		ThisMethodPos.Column +=
		    static_cast<std::uint32_t>(std::string_view("this.attach(").size());
		const FSignatureHelpResult ThisMethodHelp = VeroScriptCallHelp::HelpAtSource(
		    ThisMethodSource,
		    ThisMethodPos,
		    "<sig-this-ns-method>",
		    Options);
		if (!Expect(
		        ThisMethodHelp.bSuccess && ThisMethodHelp.bActive && !ThisMethodHelp.Signatures.empty() && ThisMethodHelp.Signatures.front().Label.find("attach") != std::string::npos && ThisMethodHelp.Signatures.front().Parameters.size() == 1,
		        "this.attach inside namespaced class resolves signature"))
		{
			std::cerr << "sigs=" << ThisMethodHelp.Signatures.size()
			          << " label=["
			          << (ThisMethodHelp.Signatures.empty()
			                     ? ""
			                     : ThisMethodHelp.Signatures.front().Label)
			          << "]\n";
			return EXIT_FAILURE;
		}

		constexpr const char* HostCtorSource = R"(
void main() {
	Widget(
}
)";
		FTextPosition CtorPos;
		if (!Expect(FindPosition(HostCtorSource, "Widget(", CtorPos), "locate Widget("))
		{
			return EXIT_FAILURE;
		}
		CtorPos.Column += static_cast<std::uint32_t>(std::string_view("Widget(").size());
		const FSignatureHelpResult CtorHelp = VeroScriptCallHelp::HelpAtSource(
		    HostCtorSource, CtorPos, "<sig-host-ctor>", Options);
		if (!Expect(
		        CtorHelp.bSuccess && CtorHelp.bActive && CtorHelp.Signatures.size() == 2 && CtorHelp.Signatures.front().Label.find("Widget") != std::string::npos && CtorHelp.Signatures.front().Parameters.size() == 1,
		        "host ctor overload signatures"))
		{
			std::cerr << "sigs=" << CtorHelp.Signatures.size()
			          << " label=["
			          << (CtorHelp.Signatures.empty() ? "" : CtorHelp.Signatures.front().Label)
			          << "]\n";
			return EXIT_FAILURE;
		}

		constexpr const char* HostBraceCtorSource = R"(
void main() {
	Widget{
}
)";
		FTextPosition BraceCtorPos;
		if (!Expect(
		        FindPosition(HostBraceCtorSource, "Widget{", BraceCtorPos),
		        "locate Widget brace constructor"))
		{
			return EXIT_FAILURE;
		}
		BraceCtorPos.Column += static_cast<std::uint32_t>(std::string_view("Widget{").size());
		const FSignatureHelpResult BraceCtorHelp = VeroScriptCallHelp::HelpAtSource(
		    HostBraceCtorSource,
		    BraceCtorPos,
		    "<sig-host-brace-ctor>",
		    Options);
		if (!Expect(
		        BraceCtorHelp.bSuccess && BraceCtorHelp.bActive && !BraceCtorHelp.Signatures.empty() && BraceCtorHelp.Signatures.front().Label.find("Widget") != std::string::npos && BraceCtorHelp.Signatures.front().Parameters.size() == 1,
		        "host brace constructor Widget signature"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		if (!Expect(
		        Registry.RegisterNative(
		            "sum_array",
		            VeroScript::ENativeType::Int,
		            {VeroScript::ENativeType::Array},
		            [](const VeroScript::FNativeCallContext&, VeroScript::FValue&, std::string&)
		{
			return true;
		},
		            nullptr,
		            ErrorId,
		            {},
		            {},
		            {},
		            {"int"}),
		        "register sum_array(array<int>)"))
		{
			std::cerr << "error=" << ErrorId << '\n';
			return EXIT_FAILURE;
		}
		FSignatureHelpOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;
		constexpr const char* ArraySource = R"(
void main() {
	sum_array(
}
)";
		FTextPosition ArrayPos;
		if (!Expect(FindPosition(ArraySource, "sum_array(", ArrayPos), "locate sum_array("))
		{
			return EXIT_FAILURE;
		}
		ArrayPos.Column += static_cast<std::uint32_t>(std::string_view("sum_array(").size());
		const FSignatureHelpResult ArrayHelp = VeroScriptCallHelp::HelpAtSource(
		    ArraySource, ArrayPos, "<sig-dynarray>", Options);
		if (!Expect(
		        ArrayHelp.bSuccess && ArrayHelp.bActive && !ArrayHelp.Signatures.empty() && ArrayHelp.Signatures.front().Parameters.size() == 1 && ArrayHelp.Signatures.front().Parameters.front().Label == "array<int>",
		        "Array param label is array<int>"))
		{
			std::cerr << "label=["
			          << (ArrayHelp.Signatures.empty() || ArrayHelp.Signatures.front().Parameters.empty()
			                     ? ""
			                     : ArrayHelp.Signatures.front().Parameters.front().Label)
			          << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		if (!Expect(
		        Registry.RegisterNative(
		            "consume_containers",
		            VeroScript::ENativeType::Void,
		            {VeroScript::ENativeType::Map, VeroScript::ENativeType::Set},
		            [](const VeroScript::FNativeCallContext&, VeroScript::FValue&, std::string&)
		{
			return true;
		},
		            nullptr,
		            ErrorId,
		            {},
		            {},
		            {},
		            {{}, "string"},
		            {
		                VeroScript::NativeParamFlagRef,
		                static_cast<std::uint8_t>(
		                    VeroScript::NativeParamFlagRef | VeroScript::NativeParamFlagConstRef),
		            },
		            {},
		            {},
		            {"int", {}},
		            {"string", {}}),
		        "register consume_containers(map<int,string>&, const set<string>&)"))
		{
			std::cerr << "error=" << ErrorId << '\n';
			return EXIT_FAILURE;
		}
		FSignatureHelpOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;
		constexpr const char* ContainerSource = R"(
void main() {
	consume_containers(
}
)";
		FTextPosition ContainerPos;
		if (!Expect(
		        FindPosition(ContainerSource, "consume_containers(", ContainerPos),
		        "locate consume_containers("))
		{
			return EXIT_FAILURE;
		}
		ContainerPos.Column +=
		    static_cast<std::uint32_t>(std::string_view("consume_containers(").size());
		const FSignatureHelpResult Help = VeroScriptCallHelp::HelpAtSource(
		    ContainerSource, ContainerPos, "<sig-containers>", Options);
		if (!Expect(
		        Help.bSuccess && Help.bActive && !Help.Signatures.empty() && Help.Signatures.front().Parameters.size() == 2 && Help.Signatures.front().Label.find("map<int,string>&, const set<string>&") != std::string::npos && Help.Signatures.front().Parameters[0].Label == "map<int,string>&" && Help.Signatures.front().Parameters[1].Label == "const set<string>&",
		        "Map/Set signature help preserves generic names and ref qualifiers"))
		{
			return EXIT_FAILURE;
		}
	}

	for (const auto& [FormatName, FormatSource, FormatNeedle] : {
	         std::tuple{
	             std::string("formatf"),
	             std::string("void main() { formatf(\"x=%d\",\n}\n"),
	             std::string("formatf(\"x=%d\",")},
	         std::tuple{
	             std::string("printf"),
	             std::string("void main() { printf(\"x=%d\",\n}\n"),
	             std::string("printf(\"x=%d\",")}})
	{
		FTextPosition FormatPosition;
		if (!Expect(
		        FindPosition(FormatSource, FormatNeedle, FormatPosition),
		        "locate printf-style format call"))
		{
			return EXIT_FAILURE;
		}
		FormatPosition.Column += static_cast<std::uint32_t>(FormatNeedle.size());
		const FSignatureHelpResult FormatHelp =
		    VeroScriptCallHelp::HelpAtSource(
		        FormatSource,
		        FormatPosition,
		        "<sig-printf-style>");
		if (!Expect(
		        FormatHelp.bSuccess && FormatHelp.bActive && !FormatHelp.Signatures.empty() && FormatHelp.Signatures.front().Label.find(FormatName) != std::string::npos && FormatHelp.Signatures.front().Label.find("...") != std::string::npos,
		        "printf-style format signature active"))
		{
			return EXIT_FAILURE;
		}
	}

	for (const auto& [CastName, CastSource, CastNeedle] : {
	         std::tuple{
	             std::string("cast"),
	             std::string("void main() { int value = cast<int>(\n}\n"),
	             std::string("cast<int>(")},
	         std::tuple{
	             std::string("try_cast"),
	             std::string("void main() { optional<int> value = try_cast<int>(\n}\n"),
	             std::string("try_cast<int>(")},
	         std::tuple{
	             std::string("const_cast"),
	             std::string("void main() { const int source = 1; int value = const_cast<int>(\n}\n"),
	             std::string("const_cast<int>(")}})
	{
		FTextPosition CastPosition;
		if (!Expect(
		        FindPosition(CastSource, CastNeedle, CastPosition),
		        "locate checked cast call"))
		{
			return EXIT_FAILURE;
		}
		CastPosition.Column += static_cast<std::uint32_t>(CastNeedle.size());
		const FSignatureHelpResult CastHelp = VeroScriptCallHelp::HelpAtSource(
		    CastSource, CastPosition, "<sig-cast>");
		if (!Expect(
		        CastHelp.bSuccess && CastHelp.bActive && !CastHelp.Signatures.empty() && CastHelp.Signatures.front().Label.find(CastName) != std::string::npos && CastHelp.Signatures.front().Parameters.size() == 1,
		        "checked cast signature help active"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::string PauseSource = "void main() { std::io::pause(\n);\n}\n";
		FTextPosition PausePosition;
		if (!Expect(
		        FindPosition(PauseSource, "pause(", PausePosition),
		        "locate pause call"))
		{
			return EXIT_FAILURE;
		}
		PausePosition.Column +=
		    static_cast<std::uint32_t>(std::string_view("pause(").size());
		const FSignatureHelpResult Help = VeroScriptCallHelp::HelpAtSource(
		    PauseSource, PausePosition, "<sig-pause>");
		if (!Expect(
		        Help.bSuccess && Help.bActive && !Help.Signatures.empty() && Help.Signatures.front().Label.find("pause") != std::string::npos && Help.Signatures.front().Parameters.empty(),
		        "pause zero-argument signature help"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		using VeroScript::ENativeType;
		using VeroScript::FNativeParamDesc;
		using VeroScript::FNativeTypeDesc;
		const FNativeTypeDesc StringType = FNativeTypeDesc::Scalar(ENativeType::String);
		const FNativeTypeDesc IntType = FNativeTypeDesc::Scalar(ENativeType::Int);
		if (!Expect(
		        Registry.RegisterNative(
		            "consume_new_containers",
		            FNativeTypeDesc::Scalar(ENativeType::Void),
		            {
		                FNativeParamDesc{FNativeTypeDesc::MultiMapOf(StringType, IntType), 0},
		                FNativeParamDesc{
		                    FNativeTypeDesc::OrderedMultiMapOf(StringType, IntType), 0},
		                FNativeParamDesc{FNativeTypeDesc::MultiSetOf(StringType), 0},
		                FNativeParamDesc{FNativeTypeDesc::OrderedMultiSetOf(StringType), 0},
		                FNativeParamDesc{FNativeTypeDesc::PriorityQueueOf(IntType), 0},
		            },
		            [](const VeroScript::FNativeCallContext&, VeroScript::FValue&, std::string&)
		{
			return true;
		},
		            nullptr,
		            ErrorId),
		        "register structured new-container signature"))
		{
			std::cerr << "error=" << ErrorId << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
		        Registry.RegisterNative(
		            "consume_callback",
		            FNativeTypeDesc::Scalar(ENativeType::Void),
		            {FNativeParamDesc{FNativeTypeDesc::Function("fn(int)->int"), 0}},
		            [](const VeroScript::FNativeCallContext&, VeroScript::FValue&, std::string&)
		{
			return true;
		},
		            nullptr,
		            ErrorId),
		        "register structured callable signature"))
		{
			std::cerr << "error=" << ErrorId << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
		        Registry.RegisterNative(
		            "consume_nested",
		            FNativeTypeDesc::Scalar(ENativeType::Void),
		            {FNativeParamDesc{
		                FNativeTypeDesc::ArrayOf(FNativeTypeDesc::ArrayOf(IntType)), 0}},
		            [](const VeroScript::FNativeCallContext&, VeroScript::FValue&, std::string&)
		{
			return true;
		},
		            nullptr,
		            ErrorId),
		        "register recursive structured container signature"))
		{
			std::cerr << "error=" << ErrorId << '\n';
			return EXIT_FAILURE;
		}

		constexpr const char* NewContainerSource = R"(
void main() {
	consume_new_containers(
}
)";
		FTextPosition NewContainerPosition;
		if (!Expect(
		        FindPosition(
		            NewContainerSource,
		            "consume_new_containers(",
		            NewContainerPosition),
		        "locate consume_new_containers("))
		{
			return EXIT_FAILURE;
		}
		NewContainerPosition.Column +=
		    static_cast<std::uint32_t>(std::string_view("consume_new_containers(").size());
		FSignatureHelpOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;
		const FSignatureHelpResult Help = VeroScriptCallHelp::HelpAtSource(
		    NewContainerSource,
		    NewContainerPosition,
		    "<sig-new-containers>",
		    Options);
		const std::vector<std::string> Expected = {
		    "multimap<string,int>",
		    "ordered_multimap<string,int>",
		    "multiset<string>",
		    "ordered_multiset<string>",
		    "priority_queue<int>",
		};
		bool bLabelsMatch = Help.bSuccess && Help.bActive && !Help.Signatures.empty() && Help.Signatures.front().Parameters.size() == Expected.size();
		if (bLabelsMatch)
		{
			for (std::size_t Index = 0; Index < Expected.size(); ++Index)
			{
				bLabelsMatch =
				    Help.Signatures.front().Parameters[Index].Label == Expected[Index];
				if (!bLabelsMatch)
				{
					break;
				}
			}
		}
		if (!Expect(bLabelsMatch, "new container host signature labels"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* CallbackSource = R"(
void main() {
	consume_callback(
}
)";
		FTextPosition CallbackPosition;
		if (!Expect(
		        FindPosition(CallbackSource, "consume_callback(", CallbackPosition),
		        "locate consume_callback("))
		{
			return EXIT_FAILURE;
		}
		CallbackPosition.Column +=
		    static_cast<std::uint32_t>(std::string_view("consume_callback(").size());
		const FSignatureHelpResult CallbackHelp = VeroScriptCallHelp::HelpAtSource(
		    CallbackSource,
		    CallbackPosition,
		    "<sig-host-callback>",
		    Options);
		if (!Expect(
		        CallbackHelp.bSuccess && CallbackHelp.bActive && !CallbackHelp.Signatures.empty() && CallbackHelp.Signatures.front().Parameters.size() == 1 && CallbackHelp.Signatures.front().Parameters.front().Label == "fn(int)->int",
		        "host callable parameter keeps its canonical signature"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* NestedSource = R"(
void main() {
	consume_nested(
}
)";
		FTextPosition NestedPosition;
		if (!Expect(
		        FindPosition(NestedSource, "consume_nested(", NestedPosition),
		        "locate consume_nested("))
		{
			return EXIT_FAILURE;
		}
		NestedPosition.Column +=
		    static_cast<std::uint32_t>(std::string_view("consume_nested(").size());
		const FSignatureHelpResult NestedHelp = VeroScriptCallHelp::HelpAtSource(
		    NestedSource,
		    NestedPosition,
		    "<sig-host-nested-container>",
		    Options);
		if (!Expect(
		        NestedHelp.bSuccess && NestedHelp.bActive && !NestedHelp.Signatures.empty() && NestedHelp.Signatures.front().Parameters.size() == 1 && NestedHelp.Signatures.front().Parameters.front().Label == "array<array<int>>",
		        "host recursive structured container keeps its full signature"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		VeroScript::FHostTypeId ProviderId;
		if (!Expect(
		        Registry.RegisterType(
		            "CallbackProvider",
		            VeroScript::EHostTypeKind::Class,
		            ProviderId,
		            ErrorId),
		        "register callback provider"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::FHostMemberDesc Callback;
		Callback.Kind = VeroScript::EHostMemberKind::Method;
		Callback.Name = "callback";
		Callback.ReturnType = VeroScript::ENativeType::Function;
		Callback.ReturnHostTypeName = "fn(int)->int";
		Callback.Callback =
		    [](const VeroScript::FNativeCallContext&, VeroScript::FValue&, std::string&)
		{
			return true;
		};
		if (!Expect(
		        Registry.RegisterMember(ProviderId, Callback, ErrorId),
		        "register callable-returning host member"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* ReturnedCallbackSource = R"(
void use(CallbackProvider provider) {
	provider.callback()(
}
)";
		FTextPosition ReturnedPosition;
		if (!Expect(
		        FindPosition(ReturnedCallbackSource, "provider.callback()(", ReturnedPosition),
		        "locate returned host callback invocation"))
		{
			return EXIT_FAILURE;
		}
		ReturnedPosition.Column +=
		    static_cast<std::uint32_t>(std::string_view("provider.callback()(").size());
		FSignatureHelpOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;
		const FSignatureHelpResult Help = VeroScriptCallHelp::HelpAtSource(
		    ReturnedCallbackSource,
		    ReturnedPosition,
		    "<sig-host-returned-callback>",
		    Options);
		if (!Expect(
		        Help.bSuccess && Help.bActive && !Help.Signatures.empty() && Help.Signatures.front().Parameters.size() == 1,
		        "returned host callable retains invocation signature"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* SuperSigSource = R"(
class Base {
public:
	void base_method(int value) {}
}
class Derived : Base {
public:
	void tick() {
		Super.base_method(
	}
}
)";
		FTextPosition SuperPos;
		if (!Expect(
		        FindPosition(SuperSigSource, "Super.base_method(", SuperPos),
		        "locate Super.base_method("))
		{
			return EXIT_FAILURE;
		}
		SuperPos.Column +=
		    static_cast<std::uint32_t>(std::string_view("Super.base_method(").size());
		const FSignatureHelpResult SuperHelp = VeroScriptCallHelp::HelpAtSource(
		    SuperSigSource, SuperPos, "<sig-super>");
		if (!Expect(
		        SuperHelp.bSuccess && SuperHelp.bActive && !SuperHelp.Signatures.empty() && SuperHelp.Signatures.front().Label.find("base_method") != std::string::npos && SuperHelp.Signatures.front().Parameters.size() == 1,
		        "Super.base_method signature resolves via base type"))
		{
			std::cerr << "sigs=" << SuperHelp.Signatures.size()
			          << " label=["
			          << (SuperHelp.Signatures.empty() ? "" : SuperHelp.Signatures.front().Label)
			          << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		VeroScript::FHostTypeId ActorId;
		if (!Expect(
		        Registry.RegisterType("AActor", VeroScript::EHostTypeKind::Class, ActorId, ErrorId) && Registry.SetTypeInheritable(ActorId, true, ErrorId),
		        "register inheritable AActor for sighelp"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::FHostMemberDesc Attach;
		Attach.Kind = VeroScript::EHostMemberKind::Method;
		Attach.Name = "attach";
		Attach.ReturnType = VeroScript::ENativeType::Void;
		Attach.ParamTypes = {VeroScript::ENativeType::Int};
		Attach.Callback =
		    [](const VeroScript::FNativeCallContext&, VeroScript::FValue&, std::string&)
		{
			return true;
		};
		if (!Expect(Registry.RegisterMember(ActorId, Attach, ErrorId), "AActor.attach"))
		{
			return EXIT_FAILURE;
		}

		FSignatureHelpOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;
		constexpr const char* InheritSource = R"(
class MyActor : AActor {
public:
	MyActor() {}
}
void foo(MyActor a) {
	a.attach(
}
)";
		FTextPosition Pos;
		if (!Expect(FindPosition(InheritSource, "a.attach(", Pos), "locate inherit a.attach("))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("a.attach(").size());
		const FSignatureHelpResult Help = VeroScriptCallHelp::HelpAtSource(
		    InheritSource, Pos, "<sig-host-inherit>", Options);
		if (!Expect(
		        Help.bSuccess && Help.bActive && !Help.Signatures.empty() && Help.Signatures.front().Label.find("attach") != std::string::npos,
		        "host-inherit method a.attach signature"))
		{
			std::cerr << "sigs=" << Help.Signatures.size()
			          << " label=["
			          << (Help.Signatures.empty() ? "" : Help.Signatures.front().Label)
			          << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* AnnotSource = R"(
UPROPERTY(EditAnywhere,
class CombatActor {
public:
	int Amount = 0;
}
void main() {}
)";
		FTextPosition Pos;
		if (!Expect(FindPosition(AnnotSource, "UPROPERTY(EditAnywhere,", Pos), "locate UPROPERTY("))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("UPROPERTY(EditAnywhere,").size());
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		if (!Expect(
		        Registry.RegisterAnnotation(
		            "UPROPERTY",
		            "reflection property specifier",
		            ErrorId,
		            "Exposes a reflected property.",
		            {
		                {"EditAnywhere", "flag", "Allows editing."},
		                {"Category", "string", "Sets the editor category."},
		            }),
		        "RegisterAnnotation UPROPERTY should succeed"))
		{
			return EXIT_FAILURE;
		}
		FSignatureHelpOptions AnnotOptions;
		AnnotOptions.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		AnnotOptions.Bindings = &Registry;
		const FSignatureHelpResult Help =
		    VeroScriptCallHelp::HelpAtSource(AnnotSource, Pos, "<sig-annot>", AnnotOptions);
		if (!Expect(
		        Help.bSuccess && Help.bActive && !Help.Signatures.empty() && Help.ActiveParameter == 1 && Help.Signatures.front().Label == "UPROPERTY(EditAnywhere: flag, Category: string)" && Help.Signatures.front().Parameters.size() == 2 && Help.Signatures.front().Parameters[0].Label == "EditAnywhere" && Help.Signatures.front().Parameters[1].Label == "Category",
		        "structured UPROPERTY annotation signature"))
		{
			std::cerr << "sigs=" << Help.Signatures.size()
			          << " label=["
			          << (Help.Signatures.empty() ? "" : Help.Signatures.front().Label)
			          << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* SigilAnnotSource = R"(
@rpc(reliable)
class CombatActor {
public:
	void Apply(int Delta) {}
}
void main() {}
)";
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		if (!Expect(
		        Registry.RegisterAnnotation("@rpc", "annotation (reliable)", ErrorId),
		        "RegisterAnnotation @rpc should succeed"))
		{
			return EXIT_FAILURE;
		}
		FTextPosition Pos;
		if (!Expect(FindPosition(SigilAnnotSource, "@rpc(", Pos), "locate @rpc("))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("@rpc(").size());
		FSignatureHelpOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;
		const FSignatureHelpResult Help =
		    VeroScriptCallHelp::HelpAtSource(SigilAnnotSource, Pos, "<sig-at-rpc>", Options);
		if (!Expect(
		        Help.bSuccess && Help.bActive && !Help.Signatures.empty() && Help.Signatures.front().Label == "@rpc(reliable)" && Help.Signatures.front().Parameters.size() == 1 && Help.Signatures.front().Parameters.front().Label == "reliable",
		        "legacy-detail @rpc annotation signature"))
		{
			std::cerr << "sigs=" << Help.Signatures.size()
			          << " label=["
			          << (Help.Signatures.empty() ? "" : Help.Signatures.front().Label)
			          << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* AnnotMethodSource = R"(
class CombatActor {
public:
	@rpc(reliable)
	void ApplyDamage(int Delta) {}
}
void main() {
	CombatActor a = CombatActor();
	a.ApplyDamage(
}
)";
		FTextPosition Pos;
		if (!Expect(FindPosition(AnnotMethodSource, "a.ApplyDamage(", Pos), "locate annotated ApplyDamage("))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("a.ApplyDamage(").size());
		const FSignatureHelpResult Help =
		    VeroScriptCallHelp::HelpAtSource(AnnotMethodSource, Pos, "<sig-annot-method>");
		if (!Expect(
		        Help.bSuccess && Help.bActive && !Help.Signatures.empty() && Help.Signatures.front().Label.find("ApplyDamage") != std::string::npos && Help.Signatures.front().Parameters.size() == 1 && Help.Signatures.front().Parameters.front().Label.find("Delta") != std::string::npos,
		        "annotated method CallHelp ignores DeclMeta brackets"))
		{
			std::cerr << "sigs=" << Help.Signatures.size()
			          << " label=["
			          << (Help.Signatures.empty() ? "" : Help.Signatures.front().Label)
			          << "] params="
			          << (Help.Signatures.empty() ? 0 : Help.Signatures.front().Parameters.size())
			          << "\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* ImplicitMethodSource = R"(
class Base {
public:
	int Target(int Value) { return Value; }
}
class Derived : Base {
public:
	void Run() { Target(1); }
}
)";
		FTextPosition Pos;
		if (!Expect(
		        FindPosition(ImplicitMethodSource, "Target(1)", Pos),
		        "locate implicit-this Target("))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("Target(").size());
		const FSignatureHelpResult Help = VeroScriptCallHelp::HelpAtSource(
		    ImplicitMethodSource,
		    Pos,
		    "<sig-implicit-this>");
		if (!Expect(
		        Help.bSuccess && Help.bActive && !Help.Signatures.empty() && Help.Signatures.front().Label.find("Target") != std::string::npos && Help.Signatures.front().Parameters.size() == 1,
		        "implicit-this method CallHelp"))
		{
			std::cerr << "sigs=" << Help.Signatures.size()
			          << " label=["
			          << (Help.Signatures.empty() ? "" : Help.Signatures.front().Label)
			          << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* FriendCallSource = R"(
class Secret {
	friend int Inspect(Secret value);
private:
	int Hidden(int amount) { return amount; }
}
int Inspect(Secret value) { return value.Hidden(1); }
)";
		FTextPosition Pos;
		if (!Expect(FindPosition(FriendCallSource, "value.Hidden(", Pos),
		        "locate friend private call"))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("value.Hidden(").size());
		const FSignatureHelpResult Help = VeroScriptCallHelp::HelpAtSource(
		    FriendCallSource, Pos, "<sig-friend-function>");
		if (!Expect(
		        Help.bSuccess && Help.bActive && !Help.Signatures.empty() && Help.Signatures.front().Label.find("Hidden") != std::string::npos,
		        "friend function receives signature help for private method"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* DefaultedFunctionSource = R"(
template<T, U = int>
void Use(T first, U second) {}
void main() { Use<string>("value", }
)";
		FTextPosition DefaultFunctionPosition;
		if (!Expect(
		        FindPosition(
		            DefaultedFunctionSource,
		            "Use<string>(\"value\",",
		            DefaultFunctionPosition),
		        "locate defaulted generic function call"))
		{
			return EXIT_FAILURE;
		}
		DefaultFunctionPosition.Column += static_cast<std::uint32_t>(
		    std::string_view("Use<string>(\"value\",").size());
		const FSignatureHelpResult Help = VeroScriptCallHelp::HelpAtSource(
		    DefaultedFunctionSource,
		    DefaultFunctionPosition,
		    "<sig-generic-default-function>");
		if (!Expect(
		        Help.bSuccess && Help.bActive && !Help.Signatures.empty() && Help.Signatures.front().Label.find("string first") != std::string::npos && Help.Signatures.front().Label.find("int second") != std::string::npos,
		        "generic function signature should apply explicit and default type arguments"))
		{
			std::cerr << "label=["
			          << (Help.Signatures.empty() ? "" : Help.Signatures.front().Label)
			          << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* DefaultedConstructorSource = R"(
template<T, U = int>
class PairBox {
public:
	T first;
	U second;
	PairBox(T firstValue, U secondValue) { first = firstValue; second = secondValue; }
}
void main() { PairBox<string> value("item", }
)";
		FTextPosition DefaultConstructorPosition;
		if (!Expect(
		        FindPosition(
		            DefaultedConstructorSource,
		            "value(\"item\",",
		            DefaultConstructorPosition),
		        "locate defaulted generic constructor call"))
		{
			return EXIT_FAILURE;
		}
		DefaultConstructorPosition.Column += static_cast<std::uint32_t>(
		    std::string_view("value(\"item\",").size());
		const FSignatureHelpResult Help = VeroScriptCallHelp::HelpAtSource(
		    DefaultedConstructorSource,
		    DefaultConstructorPosition,
		    "<sig-generic-default-constructor>");
		if (!Expect(
		        Help.bSuccess && Help.bActive && !Help.Signatures.empty() && Help.Signatures.front().Label.find("string firstValue") != std::string::npos && Help.Signatures.front().Label.find("int secondValue") != std::string::npos,
		        "generic constructor signature should apply explicit and default type arguments"))
		{
			std::cerr << "label=["
			          << (Help.Signatures.empty() ? "" : Help.Signatures.front().Label)
			          << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* OverloadedDefaultsSource = R"(
template<T = int>
void Pick(T value) {}
template<T = double, U = string>
void Pick(T value, U second) {}
void main() { Pick(1); }
)";
		FTextPosition OverloadPosition;
		if (!Expect(
		        FindPosition(OverloadedDefaultsSource, "Pick(1);", OverloadPosition),
		        "locate overloaded generic call"))
		{
			return EXIT_FAILURE;
		}
		OverloadPosition.Column += static_cast<std::uint32_t>(
		    std::string_view("Pick(").size());
		const FSignatureHelpResult Help = VeroScriptCallHelp::HelpAtSource(
		    OverloadedDefaultsSource,
		    OverloadPosition,
		    "<sig-overloaded-generic-defaults>");
		bool bHasIntDefault = false;
		bool bHasSecondDefaults = false;
		for (const FSignatureInformation& Signature : Help.Signatures)
		{
			bHasIntDefault = bHasIntDefault || Signature.Label.find("int value") != std::string::npos;
			bHasSecondDefaults = bHasSecondDefaults || (Signature.Label.find("double value") != std::string::npos && Signature.Label.find("string second") != std::string::npos);
		}
		if (!Expect(
		        Help.bSuccess && Help.bActive && bHasIntDefault && bHasSecondDefaults,
		        "each generic overload keeps its own default type arguments"))
		{
			for (const FSignatureInformation& Signature : Help.Signatures)
			{
				std::cerr << "label=[" << Signature.Label << "]\n";
			}
			for (const FDiagnostic& Diagnostic : Help.Diagnostics)
			{
				std::cerr << Diagnostic.Message << '\n';
			}
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* GenericOverloadSource = R"(
T choose<T>(T left, T right) { return left; }
U choose<T, U>(T left, U right) { return right; }
void main() { print(choose<int>(1, "explicit")); }
)";
		FTextPosition GenericOverloadPosition;
		if (!Expect(
		        FindPosition(
		            GenericOverloadSource,
		            "choose<int>(1, \"explicit\"",
		            GenericOverloadPosition),
		        "locate partially explicit generic overload call"))
		{
			return EXIT_FAILURE;
		}
		GenericOverloadPosition.Column += static_cast<std::uint32_t>(
		    std::string_view("choose<int>(1, \"explicit\"").size() - 1);
		const FSignatureHelpResult Help = VeroScriptCallHelp::HelpAtSource(
		    GenericOverloadSource,
		    GenericOverloadPosition,
		    "<sig-generic-overload-inference>");
		if (!Expect(
		        Help.bSuccess && Help.bActive && Help.Signatures.size() == 2 && Help.ActiveSignature < Help.Signatures.size() && Help.Signatures[Help.ActiveSignature].Label.find("string right") != std::string::npos,
		        "signature help should infer the remaining type and select the compatible overload"))
		{
			for (std::size_t Index = 0; Index < Help.Signatures.size(); ++Index)
			{
				std::cerr << "signature[" << Index << "]=["
				          << Help.Signatures[Index].Label << "]\n";
			}
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* GenericVariadicSource = R"(
void show_all<T>(T... values) {}
void main() { show_all(4, 5); }
)";
		FTextPosition GenericVariadicPosition;
		if (!Expect(
		        FindPosition(
		            GenericVariadicSource, "show_all(4, 5", GenericVariadicPosition),
		        "locate inferred generic variadic call"))
		{
			return EXIT_FAILURE;
		}
		GenericVariadicPosition.Column += static_cast<std::uint32_t>(
		    std::string_view("show_all(4, 5").size() - 1);
		const FSignatureHelpResult Help = VeroScriptCallHelp::HelpAtSource(
		    GenericVariadicSource,
		    GenericVariadicPosition,
		    "<sig-generic-variadic-inference>");
		if (!Expect(
		        Help.bSuccess && Help.bActive && !Help.Signatures.empty() && Help.Signatures.front().Label.find("int... values") != std::string::npos,
		        "signature help should infer a generic variadic element type"))
		{
			std::cerr << "label=["
			          << (Help.Signatures.empty() ? "" : Help.Signatures.front().Label)
			          << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* GenericDefaultSource = R"(
template<T = int>
T choose_default(T value) { return value; }
void main() { print(choose_default("text")); }
)";
		FTextPosition GenericDefaultPosition;
		if (!Expect(
		        FindPosition(
		            GenericDefaultSource,
		            "choose_default(\"text\"",
		            GenericDefaultPosition),
		        "locate inferred generic call with declaration default"))
		{
			return EXIT_FAILURE;
		}
		GenericDefaultPosition.Column += static_cast<std::uint32_t>(
		    std::string_view("choose_default(\"text\"").size() - 1);
		const FSignatureHelpResult Help = VeroScriptCallHelp::HelpAtSource(
		    GenericDefaultSource,
		    GenericDefaultPosition,
		    "<sig-generic-default-inference>");
		if (!Expect(
		        Help.bSuccess && Help.bActive && !Help.Signatures.empty() && Help.Signatures.front().Label.find("string value") != std::string::npos,
		        "signature help must infer before applying a generic default"))
		{
			std::cerr << "label=["
			          << (Help.Signatures.empty() ? "" : Help.Signatures.front().Label)
			          << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* ExtensionMethodSource = R"(
struct Counter { int value = 0; };
int grow(ref Counter self, int amount) {
    self.value += amount;
    return self.value;
}
void main() {
    Counter counter();
    counter.grow(2);
}
)";
		FTextPosition ExtensionPosition;
		if (!Expect(
		        FindPosition(ExtensionMethodSource, "grow(2", ExtensionPosition),
		        "locate extension-method call"))
		{
			return EXIT_FAILURE;
		}
		ExtensionPosition.Column += static_cast<std::uint32_t>(
		    std::string_view("grow(2").size() - 1);
		const FSignatureHelpResult Help = VeroScriptCallHelp::HelpAtSource(
		    ExtensionMethodSource,
		    ExtensionPosition,
		    "<sig-extension-method>");
		if (!Expect(
		        Help.bSuccess && Help.bActive && !Help.Signatures.empty() && Help.Signatures.front().Label.find("grow") != std::string::npos && Help.Signatures.front().Parameters.size() == 1,
		        "extension-method signature help should hide the implicit receiver"))
		{
			std::cerr << "label=["
			          << (Help.Signatures.empty() ? "" : Help.Signatures.front().Label)
			          << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* NamespacedExtensionSource = R"(
struct Counter { int value = 0; };
namespace Visible { int grow(Counter self, int amount) { return amount; } }
namespace Hidden { string grow(Counter self, string text) { return text; } }
using namespace Visible;
void main() {
    Counter counter();
    counter.grow(2);
}
)";
		FTextPosition NamespacedPosition;
		if (!Expect(
		        FindPosition(NamespacedExtensionSource, "grow(2", NamespacedPosition),
		        "locate namespaced extension signature help"))
		{
			return EXIT_FAILURE;
		}
		NamespacedPosition.Column += static_cast<std::uint32_t>(
		    std::string_view("grow(2").size() - 1);
		const FSignatureHelpResult Help = VeroScriptCallHelp::HelpAtSource(
		    NamespacedExtensionSource,
		    NamespacedPosition,
		    "<sig-namespaced-extension>");
		if (!Expect(
		        Help.bSuccess && Help.bActive && Help.Signatures.size() == 1 && Help.Signatures.front().Label.find("int grow") != std::string::npos,
		        "extension signature help should include only the visible namespace"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* NumericOverloadSource = R"(
int choose_number(float value) { return 32; }
int choose_number(double value) { return 64; }
int choose_wide(long value) { return 1; }
int choose_wide(unsigned long value) { return 2; }
void main() {
	choose_number(1.0f);
	choose_number(2.0);
	choose_wide(1u64);
}
)";
		const auto ExpectActiveType = [&](
		                                  const std::string_view Needle,
		                                  const std::string_view ExpectedType,
		                                  const char* const Message)
		{
			FTextPosition Position;
			if (!FindPosition(NumericOverloadSource, Needle, Position))
			{
				return Expect(false, Message);
			}
			Position.Column += static_cast<std::uint32_t>(Needle.size() - 1);
			const FSignatureHelpResult Help = VeroScriptCallHelp::HelpAtSource(
			    NumericOverloadSource,
			    Position,
			    "<sig-numeric-literal-width>");
			const bool bMatches = Help.bSuccess && Help.bActive && Help.ActiveSignature < Help.Signatures.size() && Help.Signatures[Help.ActiveSignature].Parameters.size() == 1 && Help.Signatures[Help.ActiveSignature].Parameters.front().Label.find(ExpectedType) == 0;
			if (!bMatches)
			{
				for (std::size_t Index = 0; Index < Help.Signatures.size(); ++Index)
				{
					std::cerr << "signature[" << Index << "]=["
					          << Help.Signatures[Index].Label << "]\n";
				}
			}
			return Expect(bMatches, Message);
		};
		if (!ExpectActiveType("choose_number(1.0f", "float", "float suffix selects float") || !ExpectActiveType("choose_number(2.0", "double", "plain decimal selects double") || !ExpectActiveType("choose_wide(1u64", "unsigned long", "u64 suffix selects unsigned long"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		VeroScript::FHostTypeId PickerId;
		if (!Expect(Registry.RegisterType("Picker", VeroScript::EHostTypeKind::Class,
		                PickerId, ErrorId),
		        "register Picker host type"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::FHostMemberDesc One;
		One.Kind = VeroScript::EHostMemberKind::Constructor;
		One.ReturnType = VeroScript::ENativeType::HostObject;
		One.ParamTypes = {VeroScript::ENativeType::Int};
		VeroScript::FHostMemberDesc Two = One;
		Two.ParamTypes.push_back(VeroScript::ENativeType::Int);
		if (!Expect(Registry.RegisterMember(PickerId, One, ErrorId) && Registry.RegisterMember(PickerId, Two, ErrorId),
		        "register host constructor overloads"))
		{
			return EXIT_FAILURE;
		}
		FSignatureHelpOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;
		const std::string HostMethodSource = "void f() { Picker(1, }";
		FTextPosition HostMethodPosition;
		if (!Expect(FindPosition(HostMethodSource, "Picker(1,", HostMethodPosition),
		        "locate incomplete host constructor call"))
		{
			return EXIT_FAILURE;
		}
		HostMethodPosition.Column += static_cast<std::uint32_t>(
		    std::string_view("Picker(1,").size());
		const FSignatureHelpResult Help = VeroScriptCallHelp::HelpAtSource(
		    HostMethodSource, HostMethodPosition, "<sig-host-member-overloads>", Options);
		if (!Expect(Help.bSuccess && Help.bActive && Help.Signatures.size() == 2 && Help.ActiveParameter == 1 && Help.ActiveSignature < Help.Signatures.size() && Help.Signatures[Help.ActiveSignature].Parameters.size() == 2,
		        "incomplete host constructor selects the overload accepting the active parameter"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* ReflectionSource =
		    "struct Counter { int value; }\n"
		    "void main() { field_info<Counter> field = "
		    "field_info<Counter>(type<Counter>().qualified_name, 0); "
		    "Counter counter(); field.get<int>(counter); }\n";
		FTextPosition ReflectionPosition;
		if (!Expect(FindPosition(ReflectionSource, "field.get<int>(counter", ReflectionPosition),
		        "locate executable reflection call"))
		{
			return EXIT_FAILURE;
		}
		ReflectionPosition.Column += static_cast<std::uint32_t>(
		    std::string_view("field.get<int>(").size());
		const FSignatureHelpResult Help = VeroScriptCallHelp::HelpAtSource(
		    ReflectionSource, ReflectionPosition, "<sig-executable-reflection>");
		if (!Expect(Help.bSuccess && Help.bActive && Help.Signatures.size() == 1 && Help.Signatures.front().Label.find("int get<int>(Counter receiver)") != std::string::npos && Help.Signatures.front().Parameters.size() == 1,
		        "reflection call help presents one specialized source signature"))
		{
			return EXIT_FAILURE;
		}
	}

	for (const std::string& ReturnType : {"int&", "const int&"})
	{
		const std::string ReflectionSource =
		    "class Counter { public: int value; };\n"
		    "void main() { method_info<Counter> method = "
		    "method_info<Counter>(type<Counter>().qualified_name, 0); "
		    "Counter counter = Counter(); int n = 1; method.invoke<" +
		    ReturnType + ">(counter, &n); }\n";
		const std::string CallPrefix = "method.invoke<" + ReturnType + ">(";
		FTextPosition ReferencePosition;
		if (!Expect(FindPosition(ReflectionSource, CallPrefix, ReferencePosition),
		        "locate reflected reference return call"))
		{
			return EXIT_FAILURE;
		}
		ReferencePosition.Column += static_cast<std::uint32_t>(CallPrefix.size());
		const FSignatureHelpResult Help = VeroScriptCallHelp::HelpAtSource(
		    ReflectionSource, ReferencePosition, "<sig-reflection-reference>");
		if (!Expect(Help.bSuccess && Help.bActive && Help.Signatures.size() == 1 && Help.Signatures.front().Label.find(ReturnType + " invoke<" + ReturnType + ">") != std::string::npos,
		        "reflection reference return syntax retains specialized signature help"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* TaskSource =
		    "void work(int value) {}\n"
		    "void main() { std::concurrent::spawn(\"work\", channel<int>()); "
		    "std::concurrent::spawn(work, 1); }\n";
		FTextPosition StringPosition;
		if (!Expect(FindPosition(TaskSource, "spawn(\"work\"", StringPosition),
		        "locate string task call"))
		{
			return EXIT_FAILURE;
		}
		StringPosition.Column += static_cast<std::uint32_t>(
		    std::string_view("spawn(").size());
		const FSignatureHelpResult StringHelp = VeroScriptCallHelp::HelpAtSource(
		    TaskSource, StringPosition, "<sig-task-string>");
		FTextPosition CallablePosition;
		if (!Expect(FindPosition(TaskSource, "spawn(work, 1)", CallablePosition),
		        "locate callable task call"))
		{
			return EXIT_FAILURE;
		}
		CallablePosition.Column += static_cast<std::uint32_t>(
		    std::string_view("spawn(wo").size());
		const FSignatureHelpResult CallableHelp = VeroScriptCallHelp::HelpAtSource(
		    TaskSource, CallablePosition, "<sig-task-callable>");
		if (!Expect(StringHelp.bSuccess && StringHelp.bActive && StringHelp.Signatures.size() == 2 && StringHelp.ActiveSignature == 0 && CallableHelp.bSuccess && CallableHelp.bActive && CallableHelp.Signatures.size() == 2 && CallableHelp.ActiveSignature == 1 && CallableHelp.Signatures[1].Parameters.size() == 2,
		        "task call help separates and selects both core call forms"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* TaskSource =
		    "class Ops { public: static int plus(int x) { return x + 1; } };\n"
		    "void main() { int factor = 2; "
		    "std::concurrent::spawn_task<int>(Ops::plus, 1); "
		    "std::concurrent::spawn_task<int>("
		    "[factor](int x) -> int => x * factor, 1); }\n";
		FTextPosition StaticPosition;
		if (!Expect(FindPosition(TaskSource, "spawn_task<int>(Ops::plus", StaticPosition),
		        "locate static callable task"))
		{
			return EXIT_FAILURE;
		}
		StaticPosition.Column += static_cast<std::uint32_t>(
		    std::string_view("spawn_task<int>(Ops::pl").size());
		const FSignatureHelpResult StaticHelp = VeroScriptCallHelp::HelpAtSource(
		    TaskSource, StaticPosition, "<sig-task-static-method>");
		FTextPosition LambdaPosition;
		if (!Expect(FindPosition(TaskSource, "x * factor, 1", LambdaPosition),
		        "locate lambda callable task"))
		{
			return EXIT_FAILURE;
		}
		LambdaPosition.Column += static_cast<std::uint32_t>(
		    std::string_view("x * factor").size());
		const FSignatureHelpResult LambdaHelp = VeroScriptCallHelp::HelpAtSource(
		    TaskSource, LambdaPosition, "<sig-task-lambda>");
		if (!Expect(StaticHelp.bSuccess && StaticHelp.bActive && StaticHelp.Signatures.size() == 2 && StaticHelp.ActiveSignature == 1 && StaticHelp.Signatures[1].Label.find("task<int> std::concurrent::spawn_task<int>(") == 0 && LambdaHelp.bSuccess && LambdaHelp.bActive && LambdaHelp.Signatures.size() == 2 && LambdaHelp.ActiveSignature == 1,
		        "task call help selects static-method and lambda callable forms"))
		{
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}
