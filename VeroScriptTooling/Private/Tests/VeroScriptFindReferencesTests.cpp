// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptToolingTestVm.h"
#include "VeroScriptTooling.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
	int Fail(const char* Message)
	{
		std::cerr << "VeroScriptFindReferencesTests: " << Message << '\n';
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
	{
		const auto Root = std::filesystem::temp_directory_path() / "vero-reference-recovery";
		const std::string LibraryPath = (Root / "library.vs").generic_string();
		const std::string BrokenPath = (Root / "broken.vs").generic_string();
		const std::string ValidPath = (Root / "valid.vs").generic_string();
		const std::string UnrelatedPath = (Root / "unrelated.vs").generic_string();
		const std::string MalformedPath = (Root / "malformed.vs").generic_string();
		const std::string Library = "export int compute(int value){return value;}";
		const std::string Broken = "import \"library.vs\";void main(){print(compute(1));print(compute(2))}";
		const std::string Valid = "import \"library.vs\";void extra(){print(compute(3));}";
		const std::string Unrelated = "int compute(int value){return value;}void main(){print(compute(4))}";
		const std::string Malformed = "void main(){print(\"compute";
		FReferencesOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.SourceOverlays = {{LibraryPath, Library}, {BrokenPath, Broken},
			{ValidPath, Valid}, {UnrelatedPath, Unrelated}, {MalformedPath, Malformed}};
		for (const bool bFromBroken : {false, true})
		{
			const std::string& Source = bFromBroken ? Broken : Library;
			const std::string& Path = bFromBroken ? BrokenPath : LibraryPath;
			FTextPosition Position;
			if (!Expect(FindPosition(Source, "compute", Position), "locate recovered reference"))
			{
				return EXIT_FAILURE;
			}
			const auto References = VeroScriptFindReferences::FindAtSource(Source, Position, Path, Options);
			std::size_t BrokenUses = 0;
			std::size_t ValidUses = 0;
			for (const auto& Reference : References.References)
			{
				if (Reference.FileLabel == BrokenPath)
				{
					++BrokenUses;
				}
				if (Reference.FileLabel == ValidPath)
				{
					++ValidUses;
				}
			}
			if (!Expect(References.bSuccess && References.References.size() == 4
				&& BrokenUses == 2 && ValidUses == 1,
				"recovered references retain reverse imports and exclude unrelated broken homonyms"))
			{
				return EXIT_FAILURE;
			}
		}
	}
	{
		const auto Root = std::filesystem::temp_directory_path() / "vero-reference-storage";
		const std::string LibraryPath = (Root / "library.vs").generic_string();
		const std::string UsePath = (Root / "uses.vs").generic_string();
		const std::string Library =
			"export class Functor {public: int operator()(int v){return v;}"
			"int operator[](int v){return v;}};";
		std::string Uses = "import \"library.vs\";void main(){Functor f=Functor();";
		for (int Index = 0; Index < 32; ++Index)
		{
			Uses += "print(f(1));print(f[2]);";
		}
		Uses += "}";
		FReferencesOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.SourceOverlays = {{LibraryPath, Library}, {UsePath, Uses}};
		for (const char* Operator : {"operator()", "operator[]"})
		{
			FTextPosition Declaration;
			if (!Expect(FindPosition(Library, Operator, Declaration), "locate cross-file operator"))
			{
				return EXIT_FAILURE;
			}
			const auto References = VeroScriptFindReferences::FindAtSource(
				Library, Declaration, LibraryPath, Options);
			std::size_t UsesFound = 0;
			for (const auto& Reference : References.References)
			{
				if (Reference.FileLabel == UsePath && !Reference.bIsDefinition)
				{
					++UsesFound;
				}
			}
			if (!Expect(References.bSuccess && UsesFound == 32,
				"cached cross-file tokens retain all typed operator uses"))
			{
				std::cerr << Operator << " uses=" << UsesFound << '\n';
				return EXIT_FAILURE;
			}
		}
	}

	constexpr const char* Source = R"(
void Helper() {}

void main() {
	Helper();
	Helper();
}
)";

	FTextPosition Position;
	if (!Expect(FindPosition(Source, "Helper();", Position), "locate Helper call"))
	{
		return EXIT_FAILURE;
	}

	const FReferencesResult Result =
		VeroScriptFindReferences::FindAtSource(Source, Position, "<refs>");
	if (!Expect(Result.bSuccess && Result.bHasToken, "find references"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(Result.TokenText == "Helper", "token Helper"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(Result.References.size() == 3, "decl + two calls"))
	{
		std::cerr << "count=" << Result.References.size() << '\n';
		return EXIT_FAILURE;
	}

	int Definitions = 0;
	for (const FReferenceHit& Hit : Result.References)
	{
		if (Hit.bIsDefinition)
		{
			++Definitions;
		}
	}
	if (!Expect(Definitions == 1, "one definition marked"))
	{
		return EXIT_FAILURE;
	}

	constexpr const char* OpSource = R"(
class Vec { public: int x;
	Vec(int v) { x = v; }
	Vec operator+(Vec other) { return Vec(x + other.x); } }

void main() {
	Vec a = Vec(1);
	Vec b = Vec(2);
	Vec c = a + b;
}
)";

	{
		FTextPosition OpPos;
		if (!Expect(FindPosition(OpSource, "operator+", OpPos), "locate operator+ decl"))
		{
			return EXIT_FAILURE;
		}
		const FReferencesResult OpRefs =
			VeroScriptFindReferences::FindAtSource(OpSource, OpPos, "<op-refs>");
		if (!Expect(OpRefs.bSuccess && OpRefs.bHasToken, "operator refs"))
		{
			return EXIT_FAILURE;
		}
		bool bHasUseSite = false;
		FTextPosition UsePos;
		if (!Expect(FindPosition(OpSource, "a + b", UsePos), "locate + use site"))
		{
			return EXIT_FAILURE;
		}
		UsePos.Column += static_cast<std::uint32_t>(std::string_view("a ").size());
		for (const FReferenceHit& Hit : OpRefs.References)
		{
			if (!Hit.bIsDefinition
				&& Hit.Range.StartLine == UsePos.Line
				&& Hit.Range.StartColumn == UsePos.Column)
			{
				bHasUseSite = true;
			}
		}
		if (!Expect(OpRefs.References.size() >= 2 && bHasUseSite, "refs include + use site"))
		{
			std::cerr << "count=" << OpRefs.References.size() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* IndexOpSource = R"(
class Vec { public: int x;
	Vec(int v) { x = v; }
	int operator[](int i) { return x + i; } }

void main() {
	Vec a = Vec(1);
	int n = a[0];
}
)";
		FTextPosition OpPos;
		if (!Expect(FindPosition(IndexOpSource, "operator[]", OpPos), "locate operator[] decl"))
		{
			return EXIT_FAILURE;
		}
		const FReferencesResult IndexRefs =
			VeroScriptFindReferences::FindAtSource(IndexOpSource, OpPos, "<index-op-refs>");
		if (!Expect(IndexRefs.bSuccess && IndexRefs.bHasToken, "operator[] refs"))
		{
			return EXIT_FAILURE;
		}
		bool bHasIndexUse = false;
		FTextPosition UsePos;
		if (!Expect(FindPosition(IndexOpSource, "a[0]", UsePos), "locate [] use site"))
		{
			return EXIT_FAILURE;
		}
		UsePos.Column += static_cast<std::uint32_t>(std::string_view("a").size());
		for (const FReferenceHit& Hit : IndexRefs.References)
		{
			if (!Hit.bIsDefinition
				&& Hit.Range.StartLine == UsePos.Line
				&& Hit.Range.StartColumn == UsePos.Column)
			{
				bHasIndexUse = true;
			}
		}
		if (!Expect(IndexRefs.References.size() >= 2 && bHasIndexUse, "refs include typed [] use"))
		{
			std::cerr << "count=" << IndexRefs.References.size() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* FunctorSource = R"(
class Functor { public: int base;
	Functor(int v) { base = v; }
	int operator()(int n) { return base + n; } }

void main() {
	Functor f = Functor(1);
	int a = f(2);
	int b = f(3);
}
)";
		FTextPosition OpPos;
		if (!Expect(FindPosition(FunctorSource, "operator()", OpPos), "locate operator() decl"))
		{
			return EXIT_FAILURE;
		}
		const FReferencesResult FunctorRefs =
			VeroScriptFindReferences::FindAtSource(FunctorSource, OpPos, "<functor-refs>");
		if (!Expect(FunctorRefs.bSuccess && FunctorRefs.bHasToken, "functor operator refs"))
		{
			return EXIT_FAILURE;
		}
		FTextPosition Use2;
		FTextPosition Use3;
		if (!Expect(FindPosition(FunctorSource, "f(2)", Use2), "locate f(2)"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(FindPosition(FunctorSource, "f(3)", Use3), "locate f(3)"))
		{
			return EXIT_FAILURE;
		}
		Use2.Column += static_cast<std::uint32_t>(std::string_view("f").size());
		Use3.Column += static_cast<std::uint32_t>(std::string_view("f").size());
		int UseSites = 0;
		for (const FReferenceHit& Hit : FunctorRefs.References)
		{
			if (Hit.bIsDefinition)
			{
				continue;
			}
			if ((Hit.Range.StartLine == Use2.Line && Hit.Range.StartColumn == Use2.Column)
				|| (Hit.Range.StartLine == Use3.Line && Hit.Range.StartColumn == Use3.Column))
			{
				++UseSites;
			}
		}
		if (!Expect(UseSites == 2 && FunctorRefs.References.size() == 3,
			"functor references include two calls and exclude construction"))
		{
			std::cerr << "uses=" << UseSites << " total=" << FunctorRefs.References.size() << '\n';
			return EXIT_FAILURE;
		}
	}

	// Homonyms: local `value` must not pick up field `value`.
	constexpr const char* HomonymSource = R"(
class Box { public: int value;
	void set(int value) {
		this.value = value;
	} }
)";
	{
		FTextPosition LocalPos;
		if (!Expect(FindPosition(HomonymSource, "int value) {", LocalPos), "locate param value"))
		{
			return EXIT_FAILURE;
		}
		// Caret on the parameter name inside `int value)`.
		LocalPos.Column += 4; // after "int "
		const FReferencesResult LocalRefs =
			VeroScriptFindReferences::FindAtSource(HomonymSource, LocalPos, "<homonym>");
		if (!Expect(LocalRefs.bSuccess && LocalRefs.TokenText == "value", "homonym token"))
		{
			return EXIT_FAILURE;
		}
		FTextPosition FieldPos;
		if (!Expect(FindPosition(HomonymSource, "int value;", FieldPos), "locate field value"))
		{
			return EXIT_FAILURE;
		}
		FieldPos.Column += 4; // after "int "
		int FieldHits = 0;
		for (const FReferenceHit& Hit : LocalRefs.References)
		{
			if (Hit.Range.StartLine == FieldPos.Line
				&& Hit.Range.StartColumn == FieldPos.Column)
			{
				++FieldHits;
			}
		}
		if (!Expect(FieldHits == 0, "param refs must not include field decl"))
		{
			std::cerr << "refs=" << LocalRefs.References.size() << '\n';
			return EXIT_FAILURE;
		}
	}

	// Inactive `#ifdef` body must not contribute lexical references.
	constexpr const char* InactiveSource = R"(
void Helper() {}

#ifdef OFF
void Helper() {}
#endif

void main() {
	Helper();
}
)";
	{
		FTextPosition Pos;
		if (!Expect(FindPosition(InactiveSource, "\tHelper();", Pos), "locate Helper call"))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += 1; // skip tab → H
		const FReferencesResult InactiveRefs =
			VeroScriptFindReferences::FindAtSource(InactiveSource, Pos, "<ifdef-refs>");
		if (!Expect(InactiveRefs.bSuccess && InactiveRefs.TokenText == "Helper", "ifdef token"))
		{
			return EXIT_FAILURE;
		}
		// Active decl + one call (inactive duplicate decl dropped).
		if (!Expect(InactiveRefs.References.size() == 2, "inactive ifdef Helper decl dropped"))
		{
			std::cerr << "count=" << InactiveRefs.References.size() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* MulticastAssign = R"(
void main() {
	multicast_delegate a = multicast_delegate();
	fn()->void h = fn() -> void {};
	int n = 0;
	n += 1;
	a += h;
	a -= h;
}
)";
		FTextPosition Pos;
		FTextPosition IntPlusPos;
		if (!Expect(FindPosition(MulticastAssign, "a += h", Pos), "locate multicast_delegate +="))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(FindPosition(MulticastAssign, "n += 1", IntPlusPos), "locate int +="))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("a ").size());
		const FReferencesResult Refs =
			VeroScriptFindReferences::FindAtSource(MulticastAssign, Pos, "<md-plus-refs>");
		if (!Expect(
				Refs.bSuccess && Refs.TokenText == "operator+=",
				"multicast_delegate += keeps operator+= token"))
		{
			std::cerr << "token=[" << Refs.TokenText << "]\n";
			return EXIT_FAILURE;
		}
		std::uint32_t PlusHits = 0;
		bool bHasIntPlus = false;
		for (const FReferenceHit& Hit : Refs.References)
		{
			if (Hit.Range.StartLine == Pos.Line)
			{
				++PlusHits;
			}
			if (Hit.Range.StartLine == IntPlusPos.Line)
			{
				bHasIntPlus = true;
			}
		}
		if (!Expect(PlusHits >= 1, "refs include multicast_delegate += use"))
		{
			std::cerr << "refs=" << Refs.References.size() << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(!bHasIntPlus, "refs must not include unrelated int +="))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* NestedFieldAssign = R"(
class Holder { public: multicast_delegate<map<string, int>> onMap; }

void main() {
	Holder h = Holder();
	fn(map<string, int>)->void cb = fn(map<string, int> m) -> void {};
	int n = 0;
	n += 1;
	h.onMap += cb;
}
)";
		FTextPosition Pos;
		FTextPosition IntPlusPos;
		if (!Expect(FindPosition(NestedFieldAssign, "h.onMap += cb", Pos), "locate nested field +="))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(FindPosition(NestedFieldAssign, "n += 1", IntPlusPos), "locate int += nested"))
		{
			return EXIT_FAILURE;
		}
		Pos.Column += static_cast<std::uint32_t>(std::string_view("h.onMap ").size());
		const FReferencesResult Refs =
			VeroScriptFindReferences::FindAtSource(NestedFieldAssign, Pos, "<md-field-plus>");
		if (!Expect(Refs.bSuccess && Refs.TokenText == "operator+=", "nested field += token"))
		{
			std::cerr << "token=[" << Refs.TokenText << "]\n";
			return EXIT_FAILURE;
		}
		bool bHasFieldPlus = false;
		bool bHasIntPlus = false;
		for (const FReferenceHit& Hit : Refs.References)
		{
			if (Hit.Range.StartLine == Pos.Line)
			{
				bHasFieldPlus = true;
			}
			if (Hit.Range.StartLine == IntPlusPos.Line)
			{
				bHasIntPlus = true;
			}
		}
		if (!Expect(bHasFieldPlus, "refs keep Holder.onMap +="))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!bHasIntPlus, "nested field += must not include int +="))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* OpEqRefs = R"(
class Vec { public: int x;
	Vec(int v) { x = v; }
	bool operator==(Vec other) { return x == other.x; }
	bool operator<(Vec other) { return x < other.x; }
}
void main() {
	Vec a = Vec(1);
	Vec b = Vec(2);
	bool eq = a == b;
	bool ne = a != b;
	bool lt = a < b;
	bool gt = b > a;
}
)";
		FTextPosition EqDecl;
		if (!Expect(FindPosition(OpEqRefs, "operator==", EqDecl), "locate operator== decl"))
		{
			return EXIT_FAILURE;
		}
		const FReferencesResult EqRefs =
			VeroScriptFindReferences::FindAtSource(OpEqRefs, EqDecl, "<op-eq-refs>");
		if (!Expect(EqRefs.bSuccess && EqRefs.TokenText == "operator==", "operator== refs token"))
		{
			std::cerr << "token=[" << EqRefs.TokenText << "]\n";
			return EXIT_FAILURE;
		}
		FTextPosition EqUse;
		FTextPosition NeUse;
		if (!Expect(FindPosition(OpEqRefs, "a == b", EqUse), "locate =="))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(FindPosition(OpEqRefs, "a != b", NeUse), "locate != refs"))
		{
			return EXIT_FAILURE;
		}
		EqUse.Column += static_cast<std::uint32_t>(std::string_view("a ").size());
		NeUse.Column += static_cast<std::uint32_t>(std::string_view("a ").size());
		bool bHasEqUse = false;
		bool bHasNeUse = false;
		for (const FReferenceHit& Hit : EqRefs.References)
		{
			if (Hit.Range.StartLine == EqUse.Line
				&& Hit.Range.StartColumn == EqUse.Column)
			{
				bHasEqUse = true;
			}
			if (Hit.Range.StartLine == NeUse.Line
				&& Hit.Range.StartColumn == NeUse.Column)
			{
				bHasNeUse = true;
			}
		}
		if (!Expect(bHasEqUse && bHasNeUse, "operator== refs include == and != uses"))
		{
			std::cerr << "eq=" << bHasEqUse << " ne=" << bHasNeUse
				<< " count=" << EqRefs.References.size() << '\n';
			return EXIT_FAILURE;
		}

		FTextPosition LtDecl;
		if (!Expect(FindPosition(OpEqRefs, "operator<", LtDecl), "locate operator< decl"))
		{
			return EXIT_FAILURE;
		}
		const FReferencesResult LtRefs =
			VeroScriptFindReferences::FindAtSource(OpEqRefs, LtDecl, "<op-lt-refs>");
		FTextPosition GtUse;
		if (!Expect(FindPosition(OpEqRefs, "b > a", GtUse), "locate > refs"))
		{
			return EXIT_FAILURE;
		}
		GtUse.Column += static_cast<std::uint32_t>(std::string_view("b ").size());
		bool bHasGtUse = false;
		for (const FReferenceHit& Hit : LtRefs.References)
		{
			if (Hit.Range.StartLine == GtUse.Line
				&& Hit.Range.StartColumn == GtUse.Column)
			{
				bHasGtUse = true;
			}
		}
		if (!Expect(bHasGtUse, "operator< refs include > use-sites"))
		{
			return EXIT_FAILURE;
		}
	}

	// Type-name Find Refs must include the embedded identifier in `~Type(`.
	{
		constexpr const char* DtorRefs = R"(
class Owner {
public:
	~Owner() {}
}

void main() {
	Owner o = Owner();
}
)";
		FTextPosition TypePos;
		if (!Expect(FindPosition(DtorRefs, "class Owner", TypePos), "locate class Owner refs"))
		{
			return EXIT_FAILURE;
		}
		TypePos.Column += static_cast<std::uint32_t>(std::string_view("class ").size());
		const FReferencesResult Refs =
			VeroScriptFindReferences::FindAtSource(DtorRefs, TypePos, "<type-dtor-refs>");
		if (!Expect(Refs.bSuccess, "type refs with destructor succeed"))
		{
			return EXIT_FAILURE;
		}
		FTextPosition DtorName;
		if (!Expect(FindPosition(DtorRefs, "~Owner()", DtorName), "locate ~Owner name"))
		{
			return EXIT_FAILURE;
		}
		DtorName.Column += 1; // skip '~'
		bool bHasDtorName = false;
		for (const FReferenceHit& Hit : Refs.References)
		{
			if (Hit.Range.StartLine == DtorName.Line
				&& Hit.Range.StartColumn == DtorName.Column)
			{
				bHasDtorName = true;
				break;
			}
		}
		if (!Expect(bHasDtorName, "type refs include ~Owner name token"))
		{
			std::cerr << "refs=" << Refs.References.size() << '\n';
			for (const FReferenceHit& Hit : Refs.References)
			{
				std::cerr << "  L" << Hit.Range.StartLine << ":" << Hit.Range.StartColumn
					<< "-" << Hit.Range.EndColumn << '\n';
			}
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		VeroScript::FHostTypeId CounterId;
		if (!Expect(
				Registry.RegisterType("Counter", VeroScript::EHostTypeKind::Class, CounterId, ErrorId),
				"RegisterType Counter for refs"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::FHostMemberDesc Getter;
		Getter.Kind = VeroScript::EHostMemberKind::PropertyGetter;
		Getter.Name = "value";
		Getter.ReturnType = VeroScript::ENativeType::Int;
		if (!Expect(Registry.RegisterMember(CounterId, Getter, ErrorId), "value getter for refs"))
		{
			return EXIT_FAILURE;
		}
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
				"register host_add for refs"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* HostRefs = R"(
void foo(Counter c) {
	int x = c.value;
	int y = c.value;
	int z = host_add(1, 2);
}
)";
		FReferencesOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;

		FTextPosition ValuePos;
		if (!Expect(FindPosition(HostRefs, "c.value;", ValuePos), "locate first c.value"))
		{
			return EXIT_FAILURE;
		}
		ValuePos.Column += static_cast<std::uint32_t>(std::string_view("c.").size());
		const FReferencesResult ValueRefs =
			VeroScriptFindReferences::FindAtSource(HostRefs, ValuePos, "<host-prop-refs>", Options);
		if (!Expect(ValueRefs.bSuccess && ValueRefs.References.size() >= 2, "host property refs"))
		{
			std::cerr << "value refs=" << ValueRefs.References.size() << '\n';
			return EXIT_FAILURE;
		}

		FTextPosition AddPos;
		if (!Expect(FindPosition(HostRefs, "host_add(", AddPos), "locate host_add"))
		{
			return EXIT_FAILURE;
		}
		const FReferencesResult AddRefs =
			VeroScriptFindReferences::FindAtSource(HostRefs, AddPos, "<host-fn-refs>", Options);
		if (!Expect(AddRefs.bSuccess && !AddRefs.References.empty(), "host_add refs"))
		{
			std::cerr << "host_add refs=" << AddRefs.References.size() << '\n';
			return EXIT_FAILURE;
		}

		// Homonym local must not be merged with host property.
		constexpr const char* HomonymHost = R"(
void foo(Counter c) {
	int value = 1;
	int x = c.value;
	int y = value;
}
)";
		FTextPosition HostValuePos;
		if (!Expect(FindPosition(HomonymHost, "c.value", HostValuePos), "locate homonym c.value"))
		{
			return EXIT_FAILURE;
		}
		HostValuePos.Column += static_cast<std::uint32_t>(std::string_view("c.").size());
		const FReferencesResult HomonymRefs = VeroScriptFindReferences::FindAtSource(
			HomonymHost, HostValuePos, "<host-homonym-refs>", Options);
		if (!Expect(HomonymRefs.bSuccess, "host homonym refs succeed"))
		{
			return EXIT_FAILURE;
		}
		std::size_t HostValueHits = 0;
		for (const FReferenceHit& Hit : HomonymRefs.References)
		{
			if (Hit.Range.StartLine == HostValuePos.Line
				&& Hit.Range.StartColumn == HostValuePos.Column)
			{
				++HostValueHits;
			}
		}
		if (!Expect(HomonymRefs.References.size() == 1 && HostValueHits == 1,
				"host c.value refs exclude local value"))
		{
			std::cerr << "homonym refs=" << HomonymRefs.References.size() << '\n';
			for (const FReferenceHit& Hit : HomonymRefs.References)
			{
				std::cerr << "  L" << Hit.Range.StartLine << ":" << Hit.Range.StartColumn << '\n';
			}
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* EnumClassRefs = R"(
enum class Color { Red, Green }
void main() {
	Color c = Color.Green;
	Color d = Color::Green;
	Green;
}
)";
		FTextPosition EnumPosition;
		if (!Expect(
				FindPosition(EnumClassRefs, "Green }", EnumPosition),
				"locate enum class Green declaration"))
		{
			return EXIT_FAILURE;
		}
		const FReferencesResult EnumResult = VeroScriptFindReferences::FindAtSource(
			EnumClassRefs, EnumPosition, "<enum-class-refs>");
		if (!Expect(
				EnumResult.bSuccess && EnumResult.References.size() == 3,
				"enum class member refs include declaration and . / :: uses"))
		{
			std::cerr << "enum class Green refs=" << EnumResult.References.size() << '\n';
			return EXIT_FAILURE;
		}

		FTextPosition DeclEndPos = EnumPosition;
		DeclEndPos.Column += static_cast<std::uint32_t>(std::string_view("Green").size());
		const FReferencesResult DeclEndResult = VeroScriptFindReferences::FindAtSource(
			EnumClassRefs, DeclEndPos, "<enum-class-decl-end-refs>");
		if (!Expect(
				DeclEndResult.bSuccess && DeclEndResult.References.size() == 3,
				"caret after enum class member name still finds refs"))
		{
			std::cerr << "decl-end Green refs=" << DeclEndResult.References.size() << '\n';
			return EXIT_FAILURE;
		}

		FTextPosition ScopePos;
		if (!Expect(
				FindPosition(EnumClassRefs, "Color::Green", ScopePos),
				"locate Color::Green refs"))
		{
			return EXIT_FAILURE;
		}
		ScopePos.Column += static_cast<std::uint32_t>(std::string_view("Color::").size());
		const FReferencesResult ScopeResult = VeroScriptFindReferences::FindAtSource(
			EnumClassRefs, ScopePos, "<enum-class-scope-refs>");
		if (!Expect(
				ScopeResult.bSuccess && ScopeResult.References.size() == 3,
				":: qualified enum class member finds all refs"))
		{
			std::cerr << "scope Green refs=" << ScopeResult.References.size() << '\n';
			return EXIT_FAILURE;
		}

		FTextPosition BarePos;
		if (!Expect(FindPosition(EnumClassRefs, "\tGreen;", BarePos), "locate bare Green refs"))
		{
			return EXIT_FAILURE;
		}
		BarePos.Column += 1;
		const FReferencesResult BareResult = VeroScriptFindReferences::FindAtSource(
			EnumClassRefs, BarePos, "<enum-class-bare-refs>");
		if (!Expect(
				BareResult.bSuccess
					&& BareResult.bHasToken
					&& BareResult.TokenText == "Green"
					&& BareResult.References.empty(),
				"bare enum class member must not yield lexical references"))
		{
			std::cerr << "bare Green refs=" << BareResult.References.size() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* NamespaceRefs = R"(
namespace A {
    namespace B { void Run() {} }
}
void main() {
    A::B::Run();
    A::B::Run();
}
)";
		FTextPosition NamespacePosition;
		if (!Expect(
				FindPosition(NamespaceRefs, "A::B::Run", NamespacePosition),
				"locate user namespace references"))
		{
			return EXIT_FAILURE;
		}
		NamespacePosition.Column += static_cast<std::uint32_t>(std::string_view("A::").size());
		const FReferencesResult NamespaceResult = VeroScriptFindReferences::FindAtSource(
			NamespaceRefs, NamespacePosition, "<namespace-refs>");
		if (!Expect(
				NamespaceResult.bSuccess
					&& NamespaceResult.bHasToken
					&& NamespaceResult.TokenText == "B"
					&& NamespaceResult.References.size() == 3,
				"user namespace references include declaration and qualified uses"))
		{
			std::cerr << "namespace refs=" << NamespaceResult.References.size() << '\n';
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}
