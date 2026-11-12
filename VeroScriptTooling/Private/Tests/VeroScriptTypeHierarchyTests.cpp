// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptToolingTestVm.h"
#include "../Internal/VeroScriptTypeHierarchy.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptCompile.h"

#include <cstdlib>
#include <iostream>

namespace
{
	using namespace VeroScript;
	using namespace VeroScript::Tooling::Detail;

	bool Expect(bool Condition, const char* Message)
	{
		if (!Condition)
		{
			std::cerr << "VeroScriptTypeHierarchyTests: " << Message << '\n';
		}
		return Condition;
	}

	FClassDecl Class(std::string Name, std::vector<std::string> Bases = {})
	{
		FClassDecl Decl;
		Decl.Name = std::move(Name);
		Decl.BaseTypeNames = std::move(Bases);
		Decl.BaseInheritanceAccess.assign(Decl.BaseTypeNames.size(), EMemberAccess::Public);
		return Decl;
	}
}

int main()
{
	{
		for (const auto& [Detail, Expected] : std::vector<std::pair<std::string, std::string>>{
		         {"Leaf[3]", "Leaf[3]"},
		         {"Leaf[(Count + 1)] [Tag()]", "Leaf[(Count + 1)]"},
		         {"Leaf[(Count < 3 ? 2 : 4)] read() [Tag()]", "Leaf[(Count < 3 ? 2 : 4)]"},
		         {"[Tag()] const Leaf[3]", "Leaf[3]"},
		         {"fn(int)->Leaf[3] [Tag()]", "fn(int)->Leaf[3]"}})
		{
			if (!Expect(TypeNameFromDetail(Detail) == Expected,
			        "native array extents survive detail annotations and expression parentheses"))
			{
				return EXIT_FAILURE;
			}
		}
	}
	{
		FProgram Defaults;
		Defaults.Classes.push_back(Class("Box"));
		Defaults.Classes.back().TypeParams = {"T", "U"};
		Defaults.Classes.back().TypeParamDefaults = {"int", "T"};
		if (!Expect(SpecializeMemberDetail(Defaults, "Box<>", "U (field)") == "int (field)",
		        "empty applications bind all available defaults"))
		{
			return EXIT_FAILURE;
		}
		FTypeAliasDecl Alias;
		Alias.Name = "Pick";
		Alias.TypeName = "Box<>";
		Defaults.TypeAliases.push_back(std::move(Alias));
		if (!Expect(ResolveTypeAlias(Defaults, "Pick") == "Box<>" && ResolveDeclarationTypeName(Defaults, {}, {}, "Pick") == "Box<>",
		        "alias resolution preserves an empty explicit application"))
		{
			return EXIT_FAILURE;
		}
		FDocumentSymbol Plain, Generic;
		Plain.Kind = Generic.Kind = EDocumentSymbolKind::Class;
		Plain.Name = "Box";
		Generic.Name = "Box<T = int, U = T>";
		std::vector<FDocumentSymbol> Symbols{Plain, Generic};
		if (!Expect(FindTypeSymbolInTree(Symbols, "Box<>") == &Symbols[0] && FindTypeSymbolInTree(Symbols, "Box") == &Symbols[0] && !FindTypeSymbolInTree(Symbols, "Box<int,int,int>"),
		        "empty applications prefer an exact arity-zero declaration before defaults"))
		{
			return EXIT_FAILURE;
		}
		Symbols.erase(Symbols.begin());
		if (!Expect(FindTypeSymbolInTree(Symbols, "Box<>") == &Symbols[0],
		        "empty applications can use defaults when there is no arity-zero declaration"))
		{
			return EXIT_FAILURE;
		}
	}
	for (const bool bSameParameterName : {false, true})
	{
		FProgram Qualified;
		const std::string Parameter = bSameParameterName ? "T" : "U";
		Qualified.Classes.push_back(Class("A.Box", {"A.Parent<T>"}));
		Qualified.Classes.back().TypeParams = {"T"};
		Qualified.Classes.push_back(Class("B.Box", {"B.Parent<" + Parameter + ">"}));
		Qualified.Classes.back().TypeParams = {Parameter};
		Qualified.Classes.push_back(Class("A.Parent"));
		Qualified.Classes.back().TypeParams = {"T"};
		Qualified.Classes.push_back(Class("B.Parent"));
		Qualified.Classes.back().TypeParams = {Parameter};
		if (!Expect(SpecializeMemberDetail(Qualified, "B.Box<int>", Parameter + " (field)") == "int (field)" && SpecializeMemberDetail(Qualified, "B::Box<int>", Parameter + " (field)") == "int (field)",
		        "qualified generic members use their own declaration's parameter names"))
		{
			return EXIT_FAILURE;
		}
		std::vector<std::string> Hierarchy;
		CollectTypeHierarchy(Qualified, "B.Box<int>", Hierarchy);
		if (!Expect(Hierarchy == std::vector<std::string>{"B.Box<int>", "B.Parent<int>"},
		        "qualified generic inheritance cannot traverse an earlier namespaced homonym"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(SpecializeMemberDetail(Qualified, "Missing.Box<int>", "T (field)") == "T (field)",
		        "unresolved qualified names cannot borrow another namespace's generic parameters"))
		{
			return EXIT_FAILURE;
		}
		Qualified.Classes.push_back(Class("Box"));
		if (!Expect(SpecializeMemberDetail(Qualified, "Box<int>", "T (field)") == "T (field)",
		        "a global declaration with incompatible arity cannot select a namespaced generic"))
		{
			return EXIT_FAILURE;
		}
		Qualified.Classes.push_back(Class("B.Unique"));
		Qualified.Classes.back().TypeParams = {"V"};
		if (!Expect(SpecializeMemberDetail(Qualified, "Unique<int>", "V (field)") == "int (field)",
		        "unqualified leaf recovery still specializes a unique namespaced declaration"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		FProgram Qualified;
		Qualified.Classes.push_back(Class("A.Box"));
		FMethodDecl Wrong;
		Wrong.Name = "wrong";
		Wrong.ReturnType = "int";
		Wrong.bVirtual = true;
		Wrong.Access = EMemberAccess::Public;
		Qualified.Classes.back().Methods.push_back(std::move(Wrong));
		FStructDecl Base;
		Base.Name = "B.Box";
		FMethodDecl Expected;
		Expected.Name = "expected";
		Expected.ReturnType = "int";
		Expected.bVirtual = true;
		Expected.Access = EMemberAccess::Public;
		Base.Methods.push_back(std::move(Expected));
		Qualified.Structs.push_back(std::move(Base));
		FStructDecl Derived;
		Derived.Name = "Derived";
		Derived.BaseTypeName = "B.Box";
		Derived.BaseInheritanceAccess = EMemberAccess::Public;
		Qualified.Structs.push_back(std::move(Derived));
		std::vector<FOverrideMemberCandidate> Candidates;
		CollectOverrideMemberCandidates(Qualified, "Derived", Candidates);
		if (!Expect(Candidates.size() == 1 && Candidates.front().Name == "expected",
		        "override lookup prefers an exact struct declaration over an unrelated class leaf"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		FProgram Scoped;
		Scoped.Classes.push_back(Class("Same"));
		Scoped.Classes.push_back(Class("A.Same"));
		Scoped.Classes.push_back(Class("B.Same"));
		Scoped.Classes.push_back(Class("B.Derived", {"B.Same"}));
		if (!Expect(ResolveTypeAlias(Scoped, "::Same") == "Same", "explicit global receiver names resolve to canonical declarations")) return EXIT_FAILURE;
		FTypeAliasDecl NestedAlias;
		NestedAlias.Name = "A.Pick";
		NestedAlias.TypeName = "Other";
		Scoped.TypeAliases.push_back(std::move(NestedAlias));
		FTypeAliasDecl GlobalAlias;
		GlobalAlias.Name = "Pick";
		GlobalAlias.TypeName = "Same";
		Scoped.TypeAliases.push_back(std::move(GlobalAlias));
		if (!Expect(ResolveTypeAlias(Scoped, "::Pick") == "Same", "global aliases cannot select a namespaced homonym")) return EXIT_FAILURE;
		if (!Expect(!IsMemberAccessibleFromContext(Scoped, "A.Same", EMemberAccess::Private, "B.Same") &&
		            !IsMemberAccessibleFromContext(Scoped, "Same", EMemberAccess::Private, "B.Same") &&
		            !IsMemberAccessibleFromContext(Scoped, "A.Same", EMemberAccess::Protected, "B.Derived") &&
		            IsMemberAccessibleFromContext(Scoped, "A.Same", EMemberAccess::Private, "A::Same") &&
		            IsMemberAccessibleFromContext(Scoped, "B.Same", EMemberAccess::Protected, "B.Derived"),
		        "access identity distinguishes qualified types and their global homonyms"))
		{
			return EXIT_FAILURE;
		}

		FFriendDecl Friend;
		Friend.Kind = FFriendDecl::EKind::Type;
		Friend.TypeName = "B.Friend";
		Scoped.Classes.front().Friends.push_back(Friend);
		if (!Expect(!IsMemberAccessibleFromContext(Scoped, "A.Same", EMemberAccess::Private, "B.Friend"),
		        "a friend of a global homonym is not a friend of a namespaced type"))
		{
			return EXIT_FAILURE;
		}

		Scoped.Classes[1].Friends.push_back(Friend);
		if (!Expect(IsMemberAccessibleFromContext(Scoped, "A.Same", EMemberAccess::Private, "B.Friend"),
		        "the actual declaring type still grants its explicit friend access"))
		{
			return EXIT_FAILURE;
		}

		const std::string Valid =
		    "namespace A{class Same{private:int secret;public:int Read(){return secret;}};}"
		    "namespace B{class Same{public:void Inspect(A::Same value){}};}void main(){}";
		if (!Expect(VeroScriptCompile::CompileSource(Valid).bSuccess, "qualified access fixture compiles")) return EXIT_FAILURE;
		auto Invalid = Valid;
		Invalid.replace(Invalid.find("Inspect(A::Same value){}"), std::string("Inspect(A::Same value){}").size(),
		    "Inspect(A::Same value){print(value.secret);}");
		if (!Expect(!VeroScriptCompile::CompileSource(Invalid).bSuccess,
		        "the compiler rejects private access through a different namespaced homonym"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		FCompileOptions Options;
		VeroScript::Test::Configure(Options);
		Options.SourceOverlays = {
		    {"generic-scope-base.vs",
		        "using Number=int;using D=int;export class B<T>{public:virtual T operator T(){return 1;}};"},
		    {"ordinary-scope-base.vs",
		        "using Number=int;export class B{public:virtual Number Read(Number value){return value;}};"}};
		struct FCase
		{
			std::string Source;
			std::vector<std::string> Signatures;
		};
		const std::vector<FCase> Cases = {
		    {"class B{public:int value=1;virtual int& Read(){return value;}};"
		     "class D:public B{public:\n  \n};void main(){}",
		        {"int& Read()"}},
		    {"class B{public:int value=1;virtual const int& Read() const{return value;}};"
		     "class D:public B{public:\n  \n};void main(){}",
		        {"const int& Read() const"}},
		    {"using Number=int;class B{public:virtual ::Number operator ::Number(){return 1;}};"
		     "class D:public B{public:\n  \n};void main(){}",
		        {"int operator int()"}},
		    {"using Number=int;class B{public:virtual ::Number operator ::Number(){return 1;}};"
		     "class D:public B{public:int operator int(){return 2;}\n  \n};void main(){}",
		        {}},
		    {"using Number=int;namespace N{using Number=double;class B{public:virtual ::Number operator ::Number(){return 1;}};}"
		     "class D:public N::B{public:\n  \n};void main(){}",
		        {"int operator int()"}},
		    {"import \"generic-scope-base.vs\";using Number=double;"
		     "class D:public B<Number>{public:\n  \n};void main(){}",
		        {"double operator double()"}},
		    {"import \"generic-scope-base.vs\";using Number=double;"
		     "class D:public B<Number>{public:double operator double(){return 2.5;}\n  \n};void main(){}",
		        {}},
		    {"import \"generic-scope-base.vs\";using Number=double;class M<U>:public B<U>{};"
		     "class D:public M<Number>{public:\n  \n};void main(){}",
		        {"double operator double()"}},
		    {"import \"ordinary-scope-base.vs\";using Number=double;"
		     "class D:public B{public:\n  \n};void main(){}",
		        {"int Read(int value)"}},
		    {"using Number=int;class B{public:virtual int Read(Number value){return value;}};"
		     "class D:public B{public:int Read(int value){return value;}\n  \n};void main(){}",
		        {}},
		    {"class B{public:virtual int Read(const int value){return value;}};"
		     "class D:public B{public:int Read(int value) override{return value;}\n  \n};void main(){}",
		        {}},
		    {"class B{public:virtual int Read(int value){return value;}};"
		     "class D:public B{public:int Read(const int value) override{return value;}\n  \n};void main(){}",
		        {}},
		    {"class B{public:virtual int Read(const int value){return value;}};"
		     "class D:public B{public:\n  \n};void main(){}",
		        {"int Read(const int value)"}},
		    {"class B{public:virtual int Read(int value){return value;}};"
		     "class D:public B{public:\n  \n};void main(){}",
		        {"int Read(int value)"}},
		    {"class B{public:virtual int Read(ref int value){return value;}"
		     "virtual int Read(const ref int value){return value;}};"
		     "class D:public B{public:int Read(ref int value) override{return value;}\n  \n};void main(){}",
		        {"int Read(const ref int value)"}},
		    {"class B{public:virtual int Read(ref int value){return value;}"
		     "virtual int Read(const ref int value){return value;}};"
		     "class D:public B{public:int Read(const ref int value) override{return value;}\n  \n};void main(){}",
		        {"int Read(ref int value)"}},
		    {"class B<T=int>{public:virtual T Read(T value){return value;}};"
		     "class D:public B<>{public:\n  \n};void main(){}",
		        {"int Read(int value)"}},
		    {"class B<T=int,U=T>{public:virtual U Read(T value){return value;}};using Pick=B<>;"
		     "class D:public Pick{public:\n  \n};void main(){}",
		        {"int Read(int value)"}},
		    {"class Box<T=int>{public:T value;};class B{public:virtual int Read(Box<> value){return 1;}};"
		     "class D:public B{public:int Read(Box<int> value) override{return 2;}\n  \n};void main(){}",
		        {}},
		    {"class Box{};class B{public:virtual int Read(Box<> value){return 1;}};"
		     "class D:public B{public:int Read(Box value) override{return 2;}\n  \n};void main(){}",
		        {}},
		    {"class B<T>{public:T value;virtual const T& Read() const{return value;}};"
		     "class D:public B<int>{public:\n  \n};void main(){}",
		        {"const int& Read() const"}},
		    {"class B<T>{public:virtual T Apply(fn(T)->T cb,T value){return cb(value);}};"
		     "class D:public B<int>{public:\n  \n};void main(){}",
		        {"int Apply(fn(int)->int cb, int value)"}},
		    {"class B<T>{public:T value;virtual fn()->const T& Factory() const{throw 0;}};"
		     "class D:public B<int>{public:\n  \n};void main(){}",
		        {"fn()->const int& Factory() const"}},
		    {"class B<T>{public:virtual int Size(ref T values[2][3]){return 1;}};"
		     "class D:public B<int>{public:\n  \n};void main(){}",
		        {"int Size(ref int[2][3] values)"}},
		    {"class Box<T>{public:T value;};class B{public:virtual Box<long> operator Box<long>(){return Box<long>();}};"
		     "class D:public B{public:Box<int64> operator Box<int64>(){return Box<int64>();}\n  \n};void main(){}",
		        {}},
		    {"class B{public:virtual map<long,int> operator map<long,int>(){return map<long,int>();}};"
		     "class D:public B{public:map<int64,int> operator map<int64,int>(){return map<int64,int>();}\n  \n};void main(){}",
		        {}},
		    {"class B{public:virtual map<int,int> operator map<int,int>(){return map<int,int>();}};"
		     "class D:public B{public:map<int64,int> operator map<int64,int>(){return map<int64,int>();}\n  \n};void main(){}",
		        {"map<int,int> operator map<int,int>()"}},
		    {"class B<T>{public:virtual T operator T(){return T();}};using Values=array<long>;"
		     "class D:public B<Values>{public:array<int64> operator array<int64>(){return array<int64>();}\n  \n};void main(){}",
		        {}},
		};
		for (const auto& Test : Cases)
		{
			auto Compiled = VeroScriptCompile::CompileSource(Test.Source, "override-source-types.vs", Options);
			if (!Expect(Compiled.bSuccess && Compiled.PreparedProgram, "override source-type fixture compiles"))
			{
				std::cerr << Test.Source << '\n'
				          << Compiled.Error.Message << '\n';
				return EXIT_FAILURE;
			}
			std::vector<FOverrideMemberCandidate> Candidates;
			CollectOverrideMemberCandidates(Compiled.PreparedProgram->GetProgram(), "D", Candidates);
			if (!Expect(Candidates.size() == Test.Signatures.size(),
			        "override candidates use the resolved full slot"))
			{
				return EXIT_FAILURE;
			}

			for (std::size_t Index = 0; Index < Candidates.size(); ++Index)
			{
				if (!Expect(Candidates[Index].Signature == Test.Signatures[Index], "override signature preserves source syntax and declaration scopes"))
				{
					std::cerr << Test.Source << '\n'
					          << Candidates[Index].Signature << '\n';
					return EXIT_FAILURE;
				}
				std::string Applied = Test.Source;
				Applied.insert(Applied.find("\n  \n") + 3, Candidates[Index].Declaration);
				auto Generated = VeroScriptCompile::CompileSource(Applied, "override-source-types.vs", Options);
				if (!Expect(Generated.bSuccess, "resolved override source compiles"))
				{
					std::cerr << Applied << '\n'
					          << Generated.Error.Message << '\n';
					return EXIT_FAILURE;
				}
			}
		}
	}
	{
		FCompileOptions ConversionOptions;
		VeroScript::Test::Configure(ConversionOptions);
		ConversionOptions.SourceOverlays = {{"conversion-private-base.vs",
		    "using Number=int;export class B{public:virtual Number operator Number(){return 1;}};"}};
		const std::vector<std::pair<std::string, std::size_t>> Cases = {
		    {"using Number=int;class B{public:virtual Number operator Number(){return 1;}};"
		     "class D:public B{public:\n  \n};void main(){}",
		        1},
		    {"class B<T>{public:virtual T operator T(){return 1;}};"
		     "class D:public B<int>{public:\n  \n};void main(){}",
		        1},
		    {"using Number=int;class B{public:virtual Number operator Number(){return 1;}};"
		     "class D:public B{public:int operator int() override{return 2;}\n  \n};void main(){}",
		        0},
		    {"class B{public:virtual int operator int(){return 1;}"
		     "virtual int operator int() const{return 2;}};class D:public B{public:\n  \n};void main(){}",
		        2},
		    {"class B{public:virtual int operator int() final{return 1;}};"
		     "class D:public B{public:\n  \n};void main(){}",
		        0},
		    {"import \"conversion-private-base.vs\";using Number=double;"
		     "class D:public B{public:Number operator Number(){return 2.5;}\n  \n};void main(){}",
		        1},
		    {"class B{public:virtual long operator long(){return 1;}};"
		     "class D:public B{public:int64 operator int64(){return 2;}\n  \n};void main(){}",
		        0},
		    {"class B{public:virtual long operator long(){return 1;}};"
		     "class D:public B{public:\n  \n};void main(){}",
		        1},
		};
		for (const auto& [Source, Count] : Cases)
		{
			auto Compiled = VeroScriptCompile::CompileSource(Source, "conversion-override.vs", ConversionOptions);
			if (!Expect(Compiled.bSuccess && Compiled.PreparedProgram, "conversion fixture compiles"))
			{
				std::cerr << Source << '\n'
				          << Compiled.Error.Message << '\n';
				return EXIT_FAILURE;
			}
			std::vector<FOverrideMemberCandidate> Candidates;
			CollectOverrideMemberCandidates(Compiled.PreparedProgram->GetProgram(), "D", Candidates);
			if (!Expect(Candidates.size() == Count,
			        "conversion slots respect aliases, generic substitution, const and final"))
			{
				return EXIT_FAILURE;
			}

			for (const auto& Candidate : Candidates)
			{
				if (!Expect(Candidate.Name.rfind("operator ", 0) == 0 &&
				            Candidate.Declaration.find("operator conv ") == std::string::npos &&
				            Candidate.Declaration.find("operator T") == std::string::npos,
				        "conversion candidates contain source names and substituted target types"))
				{
					return EXIT_FAILURE;
				}

				std::string Applied = Source;
				Applied.insert(Applied.find("\n  \n") + 3, Candidate.Declaration);
				auto Generated = VeroScriptCompile::CompileSource(Applied, "conversion-override.vs", ConversionOptions);
				if (!Expect(Generated.bSuccess, "generated conversion override compiles"))
				{
					std::cerr << Applied << '\n'
					          << Generated.Error.Message << '\n';
					return EXIT_FAILURE;
				}
			}
		}
	}
	FProgram Program;
	Program.Classes.push_back(Class("Root"));
	Program.Classes.push_back(Class("Left", {"Root"}));
	Program.Classes.push_back(Class("Right", {"Root"}));
	Program.Classes.push_back(Class("Diamond", {"Left", "Right"}));
	Program.Classes[1].BaseInheritanceAccess[0] = EMemberAccess::Private;
	std::vector<std::string> Hierarchy;
	CollectTypeHierarchy(Program, "Diamond", Hierarchy);
	if (!Expect(Hierarchy == std::vector<std::string>{"Diamond", "Left", "Root", "Right"} && !IsHierarchyMemberAccessible(Program, "Diamond", "Root", EMemberAccess::Public, {}),
	        "declaration-order DFS and first-path inheritance access are preserved"))
	{
		return EXIT_FAILURE;
	}
	Program.Classes[1].BaseInheritanceAccess[0] = EMemberAccess::Public;
	if (!Expect(IsHierarchyMemberAccessible(Program, "Diamond", "Root", EMemberAccess::Public, {}),
	        "public inheritance remains accessible"))
	{
		return EXIT_FAILURE;
	}

	Program.Classes.push_back(Class("Wrap", {"T"}));
	Program.Classes.back().TypeParams = {"T"};
	CollectTypeHierarchy(Program, "Wrap<Wrap<Root>>", Hierarchy);
	if (!Expect(Hierarchy == std::vector<std::string>{"Wrap<Wrap<Root>>", "Wrap<Root>", "Root"},
	        "valid decreasing generic applications must not be mistaken for inheritance cycles"))
	{
		return EXIT_FAILURE;
	}

	Program.Classes.push_back(Class("Expand", {"Expand<array<T>>"}));
	Program.Classes.back().TypeParams = {"T"};
	CollectTypeHierarchy(Program, "Expand<int>", Hierarchy);
	if (!Expect(Hierarchy.empty() && !IsHierarchyMemberAccessible(Program, "Expand<int>", "Expand<int>", EMemberAccess::Public, {}),
	        "invalid growing generic inheritance cannot consume unbounded memory or return partial members"))
	{
		return EXIT_FAILURE;
	}
	for (const auto& Base : {std::string("ArrowExpand<fn()->array<T>>"),
	         std::string("ReturnExpand<fn()->T>")})
	{
		const std::string Name = Base.substr(0, Base.find('<'));
		Program.Classes.push_back(Class(Name, {Base}));
		Program.Classes.back().TypeParams = {"T"};
		int GrowthChecks = 0;
		const int GrowthLimit = Name == "ArrowExpand" ? 256 : 6000;
		bool bGrowthCancelled = false;
		{
			FAnalysisControlScope Control([&]()
			{
				return ++GrowthChecks > GrowthLimit;
			});
			try
			{
				CollectTypeHierarchy(Program, Name + "<int>", Hierarchy);
			}
			catch (const FAnalysisCancelled&)
			{
				bGrowthCancelled = true;
			}
		}
		if (!Expect(!bGrowthCancelled && Hierarchy.empty(),
		        "growing callable generic arguments are bounded without relying on request cancellation"))
		{
			return EXIT_FAILURE;
		}
	}
	Program.Classes.push_back(Class("FnSignature", {"Root"}));
	Program.Classes.back().TypeParams = {"T"};
	std::string FunctionType = "fn(";
	for (int Index = 0; Index < 256; ++Index)
	{
		FunctionType += "fn()->int,";
	}
	FunctionType.back() = ')';
	FunctionType += "->int";
	const std::string FunctionApplication = "FnSignature<" + FunctionType + ">";
	CollectTypeHierarchy(Program, FunctionApplication, Hierarchy);
	if (!Expect(Hierarchy == std::vector<std::string>{FunctionApplication, "Root"},
	        "independent callable parameter types do not count as growing inheritance"))
	{
		return EXIT_FAILURE;
	}

	FProgram Deep;
	constexpr int Depth = 4096;
	for (int Index = 0; Index < Depth; ++Index)
	{
		Deep.Classes.push_back(Class("Type" + std::to_string(Index),
		    Index ? std::vector<std::string>{"Type" + std::to_string(Index - 1)} : std::vector<std::string>{}));
	}
	CollectTypeHierarchy(Deep, "Type4095", Hierarchy);
	if (!Expect(Hierarchy.size() == Depth && Hierarchy.front() == "Type4095" && Hierarchy.back() == "Type0" && IsHierarchyMemberAccessible(Deep, "Type4095", "Type0", EMemberAccess::Public, {}),
	        "long inheritance chains use a bounded native stack in both traversal paths"))
	{
		return EXIT_FAILURE;
	}

	int Checks = 0;
	bool bCancelled = false;
	{
		FAnalysisControlScope Control([&]()
		{
			return ++Checks >= 8;
		});
		try
		{
			CollectTypeHierarchy(Deep, "Type4095", Hierarchy);
		}
		catch (const FAnalysisCancelled&)
		{
			bCancelled = true;
		}
	}
	if (!Expect(bCancelled && Checks >= 8, "hierarchy traversal observes cooperative cancellation")) return EXIT_FAILURE;
	CollectTypeHierarchy(Program, "Root", Hierarchy);
	return Expect(Hierarchy == std::vector<std::string>{"Root"}, "cancelled traversal does not retain state")
	    ? EXIT_SUCCESS
	    : EXIT_FAILURE;
}
