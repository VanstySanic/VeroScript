// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCppFrontend.h"
#include <algorithm>
#include "VeroScriptCppToolOutput.h"
#include "../VeroScriptCppPath.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <type_traits>

namespace
{
	using FMutableReference = int&;
	using FReadonlyReference = const int&;
	template <typename T>
	using FConstQualified = const T;
	template <typename T>
	using FVolatileQualified = volatile T;
	static_assert(std::is_same_v<FConstQualified<FMutableReference>, int&>);
	static_assert(std::is_same_v<FVolatileQualified<FMutableReference>, int&>);
	static_assert(std::is_same_v<FConstQualified<FReadonlyReference>, const int&>);

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
	using namespace VeroScript::CppFrontend;
	using namespace VeroScript::CppTools;
	bool Ok = true;
	{
		const auto First = ParseSource("namespace Shared {} using namespace Shared; using namespace Shared;",
		    "first-namespace-import.cpp");
		const auto Second = ParseSource("namespace Shared {} using namespace Shared;", "second-namespace-import.cpp");
		const auto Merged = MergeTranslationUnits({First.Unit, Second.Unit});
		Ok &= Expect(First.Succeeded() && First.Unit.UsingNamespaces.size() == 1 && Second.Succeeded() && Merged.Succeeded() && Merged.Unit.UsingNamespaces.size() == 2 && Merged.Unit.UsingNamespaces[0].Location.File == First.Unit.SourcePath && Merged.Unit.UsingNamespaces[1].Location.File == Second.Unit.SourcePath,
		    "namespace imports deduplicate within one input while retaining each source file's lookup context");
	}
	{
		const auto ReferenceAliases = ParseSource(
		    "using MutableRef = int&; using ReadonlyRef = const int&; "
		    "void Mutate(const MutableRef Value); void Inspect(const ReadonlyRef Value); "
		    "void StillMutable(volatile MutableRef Value);",
		    "reference-aliases.hpp");
		const auto HasReferenceQualifiers = [](const FParseResult& Parsed)
		{
			return Parsed.Succeeded() && Parsed.Unit.Functions.size() == 3 && std::all_of(Parsed.Unit.Functions.begin(), Parsed.Unit.Functions.end(), [](const FFunction& Function)
			{
				return Function.Parameters.size() == 1;
			}) &&
			    Parsed.Unit.Functions[0].Parameters[0].Type.Reference == EReferenceKind::LValue && !Parsed.Unit.Functions[0].Parameters[0].Type.bConst && Parsed.Unit.Functions[1].Parameters[0].Type.bConst && !Parsed.Unit.Functions[2].Parameters[0].Type.bVolatile;
		};
		Ok &= Expect(HasReferenceQualifiers(ReferenceAliases),
		    "cv applied to reference aliases must not qualify the referred-to type");
		const auto Declarations = ParseSource(
		    "using MutableRef = int&; using ReadonlyRef = const int&;", "reference-types.hpp");
		const auto Uses = ParseSource(
		    "void Mutate(const MutableRef Value); void Inspect(const ReadonlyRef Value); "
		    "void StillMutable(volatile MutableRef Value);",
		    "reference-uses.hpp");
		Ok &= Expect(Declarations.Succeeded() && Uses.Succeeded() && HasReferenceQualifiers(MergeTranslationUnits({Declarations.Unit, Uses.Unit})),
		    "cross-file reference aliases must use the same cv rules as local aliases");
	}
	for (const std::string Newline : {std::string("\n"), std::string("\r\n")})
	{
		const auto ContinuedComment = ParseSource("// hidden \\" + Newline + "int Hidden(); \\" + Newline + "int AlsoHidden();" + Newline + "int Visible();", "continued-comment.cpp");
		Ok &= Expect(ContinuedComment.Succeeded() && ContinuedComment.Unit.Functions.size() == 1 && ContinuedComment.Unit.Functions.front().Name == "Visible" && ContinuedComment.Unit.Functions.front().Location.Line == 4,
		    "continued line comments must hide declarations and preserve physical source locations");
	}
	{
		FType First;
		First.BaseName = "int";
		First.Spelling = "int";
		FType Second;
		Second.BaseName = "std::string";
		Second.Spelling = "std::string";
		FType Type;
		Type.BaseName = "std::pair";
		Type.Spelling = "std::pair<int, std::string>";
		Type.TemplateArguments = {First, Second};
		const auto Mapped = MapTypeToVeroScript(Type);
		Ok &= Expect(Mapped.bSuccess && Mapped.VeroType == "pair<int,string>", "std::pair maps to the lowercase script pair");
	}
	{
		FTypeMapOptions Names;
		Names.KnownHostTypes = {"Item", "A::Item", "A::Nested::Node", "B::Nested::Node", "Model::string"};
		FType Type;
		Type.BaseName = "Item";
		Type.Spelling = Type.BaseName;
		Type.DeclarationScope = "A";
		Ok &= Expect(MapTypeToVeroScript(Type, Names).VeroType == "A::Item", "lexical types take precedence over global types");
		Type.BaseName = "::Item";
		Ok &= Expect(MapTypeToVeroScript(Type, Names).VeroType == "Item", "absolute type names bypass lexical lookup");
		Type.BaseName = "Nested::Node";
		Ok &= Expect(MapTypeToVeroScript(Type, Names).VeroType == "A::Nested::Node", "relative qualified types resolve in their declaration scope");
		Type.BaseName = "Missing::Item";
		Ok &= Expect(!MapTypeToVeroScript(Type, Names).bSuccess, "unknown qualified names cannot bind unrelated leaf matches");
		Type.BaseName = "string";
		Type.DeclarationScope = "Model";
		Ok &= Expect(MapTypeToVeroScript(Type, Names).VeroType == "Model::string", "declared types take precedence over convenience builtin spellings");
		Type.BaseName = "Nested::Node";
		Type.DeclarationScope = "A";
		Type.PointerDepth = 1;
		Names.bAllowRawHostPointers = true;
		Ok &= Expect(MapTypeToVeroScript(Type, Names).VeroType == "A::Nested::Node", "pointer policy uses the same scoped type lookup");
		Names.KnownHostTypes = {"Item"};
		Names.KnownEnumTypes = {"A::Item"};
		Type.BaseName = "Item";
		Type.PointerDepth = 0;
		Ok &= Expect(MapTypeToVeroScript(Type, Names).VeroType == "A::Item", "record and enum lookup share lexical precedence");
		Type.PointerDepth = 1;
		Ok &= Expect(!MapTypeToVeroScript(Type, Names).bSuccess, "an enum pointer cannot select a shadowed host record");
	}
	const std::string Source = R"cpp(
#include <cstdint>
namespace Demo
{
	enum class EMode : std::uint8_t { Off = 0, On = 1 };
	struct Widget
	{
		std::vector<std::vector<const std::int32_t>> Values;
		explicit Widget(std::int32_t Initial);
		double Scale(float Factor) const;
	};
	std::int32_t Add(std::int32_t Left, std::int32_t Right)
	{
		return Left + Right;
	}
	void AcceptUnnamed(unsigned int, const Widget);
}
)cpp";

	const FParseResult Parsed = ParseSource(Source, "frontend_test.cpp");
	if (!Parsed.Succeeded())
	{
		for (const FDiagnostic& Diagnostic : Parsed.Diagnostics)
		{
			std::cerr << FormatDiagnostic(Diagnostic) << '\n';
		}
		return 1;
	}
	Ok &= Expect(Parsed.Unit.Enums.size() == 1, "expected one enum");
	if (!Parsed.Unit.Enums.empty())
	{
		std::vector<std::int64_t> EnumValues;
		std::size_t FailedIndex = 0;
		std::string EnumError;
		Ok &= Expect(EvaluateEnumValues(
		                 Parsed.Unit.Enums.front(), EnumValues, FailedIndex, EnumError) &&
		        EnumValues == std::vector<std::int64_t>({0, 1}),
		    "shared enum evaluator must retain explicit integer values");
	}
	Ok &= Expect(Parsed.Unit.Records.size() == 1, "expected one record");
	Ok &= Expect(Parsed.Unit.Functions.size() == 2, "expected two free functions");
	const FParseResult EnumExpressions = ParseSource(R"cpp(
namespace Flags
{
    enum class EBits : int
    {
        None = 0,
        Read = 1 << 0,
        Write = 1 << 1,
        Both = ::Flags::EBits::Read | Write,
		Next,
		LetterA = 'A',
		Newline = '\n',
		Enabled = true,
		Disabled = !Enabled,
		Ordered = Write > Read,
		Equal = Both == 3
    };
}
)cpp",
	    "enum_expression.cpp");
	if (EnumExpressions.Succeeded() && EnumExpressions.Unit.Enums.size() == 1)
	{
		std::vector<std::int64_t> Values;
		std::size_t FailedIndex = 0;
		std::string Error;
		Ok &= Expect(EvaluateEnumValues(
		                 EnumExpressions.Unit.Enums.front(), Values, FailedIndex, Error) &&
		        Values == std::vector<std::int64_t>({0, 1, 2, 3, 4, 65, 10, 1, 0, 1, 1}),
		    "enum evaluator must support prior names, shifts, bitwise/comparison expressions, booleans, and implicit successors");
	}
	else
	{
		Ok &= Expect(false, "enum constant-expression source should parse");
	}
	const FParseResult TypedEnumExpressions = ParseSource(R"cpp(
enum class UnsignedBits : unsigned int { All=~0u, Wrap=0xffffffffu+1u, Greater=All>1, Mixed=-1<1u, Shift=~0u>>31 };
enum class WideBits : int { Mixed=0xffffffffu+1ll==4294967296ll, Not=~0ull>1ull, Wrap=0xffffffffffffffffull+1ull==0ull, Signed=-1ll<1u };
enum class SignedBits : int { Min=1<<31, Negative=(1<<31)<0 };
enum LooseBits { LooseUnsigned=1u, LooseCompare=(LooseUnsigned-2)>0, LooseWide=1ll, LooseMixed=(LooseWide-2u)<0 };
enum FixedBits : int { FixedUnsigned=1u, FixedCompare=(FixedUnsigned-2)>0 };
)cpp",
	    "typed_enum.cpp");
	// Native constant expressions independently check integer promotion and width.
	enum class ENativeUnsigned : unsigned int
	{
		All = ~0u,
		Wrap = 0xffffffffu + 1u,
		Greater = All > 1,
		Mixed = -1 < 1u,
		Shift = ~0u >> 31
	};
	enum class ENativeWide : int
	{
		Mixed = 0xffffffffu + 1ll == 4294967296ll,
		Not = ~0ull > 1ull,
		Wrap = 0xffffffffffffffffull + 1ull == 0ull,
		Signed = -1ll < 1u
	};
	enum class ENativeSigned : int
	{
		Min = 1 << 31,
		Negative = (1 << 31) < 0
	};
	#if !defined(VEROSCRIPT_CPP_TEST_LEGACY_ENUM_TYPES)
	enum ENativeLoose
	{
		LooseUnsigned = 1u,
		LooseCompare = (LooseUnsigned - 2) > 0,
		LooseWide = 1ll,
		LooseMixed = (LooseWide - 2u) < 0
	};
	#endif
	enum ENativeFixed : int
	{
		FixedUnsigned = 1u,
		FixedCompare = (FixedUnsigned - 2) > 0
	};
	const std::vector<std::vector<std::int64_t>> NativeEnumValues = {
	    {static_cast<std::int64_t>(ENativeUnsigned::All), static_cast<std::int64_t>(ENativeUnsigned::Wrap),
	        static_cast<std::int64_t>(ENativeUnsigned::Greater),
	        static_cast<std::int64_t>(ENativeUnsigned::Mixed),
	        static_cast<std::int64_t>(ENativeUnsigned::Shift)},
	    {static_cast<std::int64_t>(ENativeWide::Mixed), static_cast<std::int64_t>(ENativeWide::Not),
	        static_cast<std::int64_t>(ENativeWide::Wrap), static_cast<std::int64_t>(ENativeWide::Signed)},
	    {static_cast<std::int64_t>(ENativeSigned::Min), static_cast<std::int64_t>(ENativeSigned::Negative)},
	#if defined(VEROSCRIPT_CPP_TEST_LEGACY_ENUM_TYPES)
	    // Older MSVC has no conforming mode for unfixed enumerator types.
	    {1, 1, 1, 1},
	#else
	    {LooseUnsigned, LooseCompare, LooseWide, LooseMixed},
	#endif
	    {FixedUnsigned, FixedCompare}};
	Ok &= Expect(TypedEnumExpressions.Succeeded() && TypedEnumExpressions.Unit.Enums.size() == NativeEnumValues.size(), "typed enum expressions should parse");
	for (std::size_t I = 0; I < TypedEnumExpressions.Unit.Enums.size() && I < NativeEnumValues.size(); ++I)
	{
		std::vector<std::int64_t> Values;
		std::size_t FailedIndex = 0;
		std::string Error;
		const bool Evaluated = EvaluateEnumValues(TypedEnumExpressions.Unit.Enums[I], Values, FailedIndex, Error);
		if (!Evaluated || Values != NativeEnumValues[I])
		{
			std::cerr << TypedEnumExpressions.Unit.Enums[I].Name << ": " << Error << " got";
			for (const auto V : Values)
			{
				std::cerr << ' ' << V;
			}

			std::cerr << " expected";
			for (const auto V : NativeEnumValues[I])
			{
				std::cerr << ' ' << V;
			}

			std::cerr << '\n';
		}
		Ok &= Expect(Evaluated && Values == NativeEnumValues[I], "enum values must match native C++ integer promotion and width");
	}
	const FParseResult InvalidEnumExpression = ParseSource(
	    "enum class EBad : int { Value = 1 / 0 };",
	    "invalid_enum_expression.cpp");
	if (InvalidEnumExpression.Succeeded() && InvalidEnumExpression.Unit.Enums.size() == 1)
	{
		std::vector<std::int64_t> Values;
		std::size_t FailedIndex = 0;
		std::string Error;
		Ok &= Expect(!EvaluateEnumValues(
		                 InvalidEnumExpression.Unit.Enums.front(), Values, FailedIndex, Error) &&
		        FailedIndex == 0 && Error.find("division by zero") != std::string::npos,
		    "enum evaluator must reject invalid arithmetic with a stable failing index");
	}
	else
	{
		Ok &= Expect(false, "invalid enum expression should remain parseable for evaluator diagnostics");
	}
	for (const char* Invalid : {"2147483647+1", "(1<<31)/-1", "(1u<<32)==0", "1uu"})
	{
		const auto InvalidParsed = ParseSource(std::string("enum class Invalid : int { Value=") + Invalid + " };", "invalid_typed_enum.cpp");
		std::vector<std::int64_t> Values;
		std::size_t Index = 0;
		std::string Error;
		Ok &= Expect(InvalidParsed.Succeeded() && InvalidParsed.Unit.Enums.size() == 1 && !EvaluateEnumValues(InvalidParsed.Unit.Enums.front(), Values, Index, Error) && Values.empty() && !Error.empty(),
		    "undefined arithmetic, invalid shift widths and malformed literal suffixes must be rejected");
	}
	Ok &= Expect(!ParseSource("enum Mixed { A=1u }; enum Mixed : int;", "fixed_enum_conflict.cpp").Succeeded(),
	    "an unfixed enum cannot merge with a fixed underlying declaration");
	const auto UnfixedUnit = ParseSource("enum Mixed { A=1u };", "unfixed.cpp");
	const auto FixedUnit = ParseSource("enum Mixed : int;", "fixed.cpp");
	Ok &= Expect(!MergeTranslationUnits({UnfixedUnit.Unit, FixedUnit.Unit}).Succeeded(),
	    "project merging must retain the fixed underlying enum distinction");
	const auto GlobalEnumQualifier = ParseSource("namespace N { enum class Scoped : int { A=1, B=::Scoped::A }; }", "global_enum_scope.cpp");
	{
		std::vector<std::int64_t> Values;
		std::size_t Index = 0;
		std::string Error;
		Ok &= Expect(GlobalEnumQualifier.Succeeded() && GlobalEnumQualifier.Unit.Enums.size() == 1 && !EvaluateEnumValues(GlobalEnumQualifier.Unit.Enums.front(), Values, Index, Error) && Index == 1,
		    "explicit global enum lookup must not resolve a relative enum name");
	}
	const FParseResult WrongEnumQualifier = ParseSource(
	    "enum class EBad : int { First = 1, Second = Other::First };",
	    "wrong_enum_qualifier.cpp");
	if (WrongEnumQualifier.Succeeded() && WrongEnumQualifier.Unit.Enums.size() == 1)
	{
		std::vector<std::int64_t> Values;
		std::size_t FailedIndex = 0;
		std::string Error;
		Ok &= Expect(!EvaluateEnumValues(
		                 WrongEnumQualifier.Unit.Enums.front(), Values, FailedIndex, Error) &&
		        FailedIndex == 1,
		    "a wrong enum qualifier must not resolve by enumerator leaf name");
	}
	else
	{
		Ok &= Expect(false, "wrongly qualified enum expression should parse for evaluator diagnostics");
	}
	const FParseResult MultiCharacterEnum = ParseSource(
	    "enum class EBad : int { Value = 'AB' };",
	    "multi_character_enum.cpp");
	if (MultiCharacterEnum.Succeeded() && MultiCharacterEnum.Unit.Enums.size() == 1)
	{
		std::vector<std::int64_t> Values;
		std::size_t FailedIndex = 0;
		std::string Error;
		Ok &= Expect(!EvaluateEnumValues(
		                 MultiCharacterEnum.Unit.Enums.front(), Values, FailedIndex, Error),
		    "implementation-defined multi-character literals must remain rejected");
	}
	if (Parsed.Unit.Functions.size() == 2)
	{
		const FFunction& Unnamed = Parsed.Unit.Functions[1];
		Ok &= Expect(Unnamed.Parameters.size() == 2, "unnamed parameters were not retained");
		if (Unnamed.Parameters.size() == 2)
		{
			Ok &= Expect(Unnamed.Parameters[0].Name == "Arg0" && Unnamed.Parameters[0].Type.BaseName == "unsigned int",
			    "unnamed compound builtin parameter was parsed as a name");
			Ok &= Expect(Unnamed.Parameters[1].Name == "Arg1" && Unnamed.Parameters[1].Type.BaseName == "Widget",
			    "unnamed const user type parameter was parsed as a name");
		}
	}
	if (!Parsed.Unit.Records.empty() && !Parsed.Unit.Records.front().Fields.empty())
	{
		const FType& Type = Parsed.Unit.Records.front().Fields.front().Type;
		Ok &= Expect(!Type.bConst, "nested const must not qualify the outer vector");
		Ok &= Expect(Type.TemplateArguments.size() == 1, "outer vector argument was not parsed");
		if (!Type.TemplateArguments.empty())
		{
			const FType& Inner = Type.TemplateArguments.front();
			Ok &= Expect(Inner.TemplateArguments.size() == 1, "nested vector argument was not parsed");
			if (!Inner.TemplateArguments.empty())
			{
				Ok &= Expect(Inner.TemplateArguments.front().bConst,
				    "const qualifier must remain on the nested element type");
			}
		}
		FTypeMapOptions MapOptions;
		const FTypeMapResult Mapped = MapTypeToVeroScript(Type, MapOptions);
		Ok &= Expect(Mapped.bSuccess && Mapped.VeroType == "array<array<readonly<int>>>",
		    "nested const value type must map to a readonly container element");
	}

	const FParseResult Templates = ParseSource(R"cpp(
template<class T, typename U = int>
struct Box
{
    T Value;
    template<typename V> V Convert(V Input) { return Input; }
};
template<typename T> T Identity(T Value) { return Value; }
)cpp",
	    "templates.cpp");
	Ok &= Expect(Templates.Succeeded() && Templates.Unit.Records.size() == 1 && Templates.Unit.Records[0].TemplateParameters.size() == 2 && Templates.Unit.Records[0].TemplateParameters[1].bHasDefault && Templates.Unit.Records[0].Methods.size() == 1 && Templates.Unit.Records[0].Methods[0].TemplateParameters.size() == 1 && Templates.Unit.Functions.size() == 1 && Templates.Unit.Functions[0].TemplateParameters.size() == 1,
	    "type-parameter templates must be retained in the frontend AST");
	Ok &= Expect(!ParseSource(
	                 "template<int N> struct Fixed {};", "non_type_template.cpp")
	                 .Succeeded(),
	    "non-type template parameters must fail closed");
	Ok &= Expect(!ParseSource(
	                 "template<typename... T> struct Pack {};", "template_pack.cpp")
	                 .Succeeded(),
	    "template parameter packs must fail closed");
	const FParseResult Destructors = ParseSource(
	    "struct Base { virtual ~Base() = 0; }; "
	    "struct Value : Base { ~Value() override { } }; "
	    "struct Defaulted { ~Defaulted() = default; };",
	    "destructors.cpp");
	Ok &= Expect(Destructors.Succeeded() && Destructors.Unit.Records.size() == 3 && Destructors.Unit.Records[0].Methods.size() == 1 && Destructors.Unit.Records[0].Methods[0].bDestructor && Destructors.Unit.Records[0].Methods[0].bVirtual && Destructors.Unit.Records[0].Methods[0].bPureVirtual && Destructors.Unit.Records[1].Methods[0].bDestructor && Destructors.Unit.Records[1].Methods[0].bOverride && Destructors.Unit.Records[2].Methods[0].bDefaulted,
	    "destructor declarations must retain lifetime and virtual metadata");
	Ok &= Expect(!ParseSource(
	                 "struct Bad { ~Other(); };", "bad_destructor.cpp")
	                 .Succeeded(),
	    "a destructor name must match its owning record");
	const FParseResult OutOfClass = ParseSource(R"cpp(
namespace Demo
{
struct Outside
{
    int Value;
    Outside(int Initial);
    ~Outside();
    int Read(int Offset = 1) const;
};
Outside::Outside(int Initial) { Value = Initial; }
Outside::~Outside() { Value = 0; }
int Outside::Read(int Offset) const { return Value + Offset; }
}
)cpp",
	    "out_of_class.cpp");
	Ok &= Expect(OutOfClass.Succeeded() && OutOfClass.Unit.Functions.empty() && OutOfClass.Unit.Records.size() == 1 && OutOfClass.Unit.Records[0].Methods.size() == 3 && OutOfClass.Unit.Records[0].Methods[0].bHasBody && OutOfClass.Unit.Records[0].Methods[1].bDestructor && OutOfClass.Unit.Records[0].Methods[1].bHasBody && OutOfClass.Unit.Records[0].Methods[2].bHasBody && !OutOfClass.Unit.Records[0].Methods[2].Parameters[0].DefaultValueTokens.empty(),
	    "same-file out-of-class definitions must merge into their declared methods");
	FParseOptions DeferredMemberOptions;
	DeferredMemberOptions.bDeferOutOfClassMemberResolution = true;
	const FParseResult MemberDeclarationUnit = ParseSource(R"cpp(
namespace Demo
{
struct Split
{
    int Read(int Offset = 1) const;
};
}
)cpp",
	    "split.hpp", DeferredMemberOptions);
	const FParseResult MemberDefinitionUnit = ParseSource(R"cpp(
namespace Demo
{
int Split::Read(int Amount) const { return Amount + 1; }
}
)cpp",
	    "split.cpp", DeferredMemberOptions);
	const FParseResult SplitMemberProject =
	    MemberDeclarationUnit.Succeeded() && MemberDefinitionUnit.Succeeded()
	    ? MergeTranslationUnits({MemberDeclarationUnit.Unit, MemberDefinitionUnit.Unit})
	    : FParseResult{};
	Ok &= Expect(SplitMemberProject.Succeeded() && SplitMemberProject.Unit.Functions.empty() && SplitMemberProject.Unit.Records.size() == 1 && SplitMemberProject.Unit.Records[0].Methods.size() == 1 && SplitMemberProject.Unit.Records[0].Methods[0].bHasBody && SplitMemberProject.Unit.Records[0].Methods[0].Parameters[0].Name == "Amount" && !SplitMemberProject.Unit.Records[0].Methods[0].Parameters[0].DefaultValueTokens.empty(),
	    "project merging must attach a cross-file member definition and retain declaration defaults");
	Ok &= Expect(!ParseSource(
	                 "int MissingOwner::Read() { return 1; }",
	                 "missing_owner.cpp")
	                 .Succeeded(),
	    "single-source parsing must still reject a missing out-of-class owner");
	Ok &= Expect(!ParseSource(
	                 "struct Missing { int Value; }; int Missing::Read() { return Value; }",
	                 "missing_member_decl.cpp")
	                 .Succeeded(),
	    "an out-of-class definition must match a member declaration");
	const FParseResult PureVirtualBodies = ParseSource(R"cpp(
struct AbstractValue
{
    virtual int Read(int Value) const = 0;
    virtual ~AbstractValue() = 0;
};
int AbstractValue::Read(int) const { return 1; }
AbstractValue::~AbstractValue() { }
)cpp",
	    "pure_virtual_bodies.cpp");
	Ok &= Expect(PureVirtualBodies.Succeeded() && PureVirtualBodies.Unit.Records.size() == 1 && PureVirtualBodies.Unit.Records[0].Methods.size() == 2 && PureVirtualBodies.Unit.Records[0].Methods[0].bPureVirtual && PureVirtualBodies.Unit.Records[0].Methods[0].bHasBody && PureVirtualBodies.Unit.Records[0].Methods[0].Parameters[0].Name == "Value" && PureVirtualBodies.Unit.Records[0].Methods[1].bPureVirtual && PureVirtualBodies.Unit.Records[0].Methods[1].bHasBody,
	    "pure virtual methods and destructors must retain same-file base definitions and declaration parameter names");
	Ok &= Expect(!ParseSource(
	                 "struct Bad { int Read(); }; double Bad::Read() { return 1.0; }",
	                 "out_of_class_return_mismatch.cpp")
	                 .Succeeded(),
	    "out-of-class definitions must not silently replace a mismatched return type");
	Ok &= Expect(!ParseSource(
	                 "struct Bad { int Read() noexcept; }; int Bad::Read() { return 1; }",
	                 "out_of_class_noexcept_mismatch.cpp")
	                 .Succeeded(),
	    "out-of-class definitions must preserve noexcept consistency");
	Ok &= Expect(!ParseSource(
	                 "struct Bad { static int Read(); }; static int Bad::Read() { return 1; }",
	                 "out_of_class_static.cpp")
	                 .Succeeded(),
	    "member-only declaration specifiers must not be repeated on out-of-class definitions");
	const FParseResult FriendParsed = ParseSource(
	    "struct Box { friend int Inspect(const Box& Value); int Kept; };",
	    "friend.cpp");
	Ok &= Expect(FriendParsed.Succeeded() && FriendParsed.Unit.Records.size() == 1 && FriendParsed.Unit.Records[0].Methods.empty() && FriendParsed.Unit.Records[0].FriendDeclarations.size() == 1 && FriendParsed.Unit.Records[0].Fields.size() == 1,
	    "friend declarations must remain record declarations instead of becoming members");
	Ok &= Expect(!ParseSource(
	                 "struct Box { friend int Inspect(const Box&) { return 1; } };",
	                 "friend_definition.cpp")
	                 .Succeeded(),
	    "inline friend definitions must fail rather than silently changing scope");
	const FParseResult SemanticFlags = ParseSource(
	    "consteval int Immediate(); constexpr int Constant(); "
	    "int Stable() noexcept; constinit int Global = 1;",
	    "semantic_flags.cpp");
	Ok &= Expect(SemanticFlags.Succeeded() && SemanticFlags.Unit.Functions.size() == 3 && SemanticFlags.Unit.Functions[0].bConsteval && SemanticFlags.Unit.Functions[1].bConstexpr && SemanticFlags.Unit.Functions[2].bNoexcept && SemanticFlags.Unit.Variables.size() == 1 && SemanticFlags.Unit.Variables[0].bConstinit,
	    "semantic declaration modifiers must be retained in the frontend AST");
	Ok &= Expect(!ParseSource(
	                 "virtual int Free();", "invalid_free.cpp")
	                 .Succeeded(),
	    "member-only specifiers must be rejected on free functions");
	Ok &= Expect(!ParseSource(
	                 "struct Bad { static int Read() const; };", "invalid_static.cpp")
	                 .Succeeded(),
	    "invalid static member cv qualification must be rejected");
	Ok &= Expect(!ParseSource(
	                 "struct Bad { static mutable int Value; };", "invalid_mutable.cpp")
	                 .Succeeded(),
	    "invalid mutable static fields must be rejected");
	const FParseResult ForwardMerged = ParseSource(R"cpp(
class Widget;
enum class EState;
struct Widget { EState State; };
enum class EState : int { Ready = 1 };
)cpp",
	    "forward.cpp");
	Ok &= Expect(ForwardMerged.Succeeded() && ForwardMerged.Unit.Records.size() == 1 && !ForwardMerged.Unit.Records[0].bForwardDeclaration && ForwardMerged.Unit.Enums.size() == 1 && !ForwardMerged.Unit.Enums[0].bForwardDeclaration,
	    "forward declarations must merge with same-unit definitions");
	Ok &= Expect(!ParseSource(
	                 "enum MissingUnderlying;", "bad_enum_forward.cpp")
	                 .Succeeded(),
	    "unscoped enum forward declarations must require an underlying type");
	Ok &= Expect(!ParseSource(
	                 "struct Duplicate {}; struct Duplicate {};", "duplicate.cpp")
	                 .Succeeded(),
	    "duplicate record definitions must be diagnosed during collection");
	const FParseResult GuardedExtern = ParseSource(R"cpp(
#ifndef DEMO_BINDING_HPP
#define DEMO_BINDING_HPP
extern "C"
{
    int StableApi(int Value);
}
#endif // DEMO_BINDING_HPP
)cpp",
	    "guarded.hpp");
	Ok &= Expect(GuardedExtern.Succeeded() && GuardedExtern.Unit.Functions.size() == 1 && GuardedExtern.Unit.Functions[0].Name == "StableApi" && GuardedExtern.Unit.Functions[0].bCLinkage && !GuardedExtern.Unit.Functions[0].bExtern,
	    "a conventional whole-file header guard and extern C block should be transparent");
	const FParseResult SingleExternC = ParseSource(
	    "extern \"C\" int StableSingle(int Value);", "extern_c.hpp");
	Ok &= Expect(SingleExternC.Succeeded() && SingleExternC.Unit.Functions.size() == 1 && SingleExternC.Unit.Functions[0].bCLinkage && !SingleExternC.Unit.Functions[0].bExtern,
	    "a single-declaration extern C linkage specification should parse");
	Ok &= Expect(!ParseSource(R"cpp(
#ifdef PLATFORM_A
int PlatformValue();
#else
double PlatformValue();
#endif
)cpp",
	                 "conditional.hpp")
	                 .Succeeded(),
	    "ordinary conditional compilation must remain rejected");
	Ok &= Expect(!ParseSource(
	                 "namespace { int Hidden(); }", "anonymous_namespace.cpp")
	                 .Succeeded(),
	    "anonymous namespaces must not lose their internal-linkage semantics");
	Ok &= Expect(!ParseSource(
	                 "struct MissingSemicolon {}", "missing_semicolon.cpp")
	                 .Succeeded(),
	    "C++ record definitions must require their trailing semicolon");
	Ok &= Expect(!ParseSource(
	                 "enum class Bad : float { Value = 1 };", "bad_underlying.cpp")
	                 .Succeeded(),
	    "non-integral enum underlying types must be rejected by the frontend");
	Ok &= Expect(!ParseSource(
	                 "struct Bad { extern int Value; };", "extern_member.cpp")
	                 .Succeeded(),
	    "extern data members must be rejected");
	Ok &= Expect(!ParseSource(
	                 "register int Invalid();", "register_function.cpp")
	                 .Succeeded(),
	    "invalid function declaration specifiers must not be silently erased");
	Ok &= Expect(!ParseSource(
	                 "mutable int InvalidGlobal;", "mutable_global.cpp")
	                 .Succeeded(),
	    "mutable namespace variables must be rejected");
	Ok &= Expect(!ParseSource(
	                 "struct Bad { constexpr int Value = 1; };", "constexpr_field.cpp")
	                 .Succeeded(),
	    "non-static constexpr data members must be rejected");
	Ok &= Expect(!ParseSource(
	                 "struct Bad { constinit int Value; };", "constinit_field.cpp")
	                 .Succeeded(),
	    "non-static constinit data members must be rejected");
	Ok &= Expect(!ParseSource(
	                 "static extern int Invalid;", "conflicting_linkage.cpp")
	                 .Succeeded(),
	    "conflicting namespace-variable linkage must be rejected");
	Ok &= Expect(!ParseSource(
	                 "struct Bad { inline int Value; };", "inline_field.cpp")
	                 .Succeeded(),
	    "inline non-static data members must be rejected");
	Ok &= Expect(!ParseSource(
	                 "thread_local int Value;", "thread_local.cpp")
	                 .Succeeded(),
	    "unsupported thread-local storage must fail in the strict declaration parser");
	const FParseResult ExternGlobal = ParseSource(
	    "extern int Shared;", "extern_global.cpp");
	Ok &= Expect(ExternGlobal.Succeeded() && ExternGlobal.Unit.Variables.size() == 1 && ExternGlobal.Unit.Variables[0].bExtern,
	    "extern global storage semantics must remain visible in the AST");
	const FParseResult BomAndWide = ParseSource(
	    "\xEF\xBB\xBFstd::wstring Message = L\"hello\";", "bom.cpp");
	Ok &= Expect(BomAndWide.Succeeded() && BomAndWide.Unit.Variables.size() == 1 && JoinTokens(BomAndWide.Unit.Variables[0].InitializerTokens) == "L\"hello\"",
	    "UTF-8 BOM and prefixed literals should be handled without token splitting");
	const FParseResult RawString = ParseSource(R"source(
std::string Text = u8R"tag(first line
"quoted"\path)tag";
)source",
	    "raw_string.cpp");
	Ok &= Expect(RawString.Succeeded() && RawString.Unit.Variables.size() == 1 && JoinTokens(RawString.Unit.Variables[0].InitializerTokens) == "\"first line\\n\\\"quoted\\\"\\\\path\"",
	    "ordinary and u8 raw strings must normalize without changing their UTF-8 contents");
	const auto MaximumRawDelimiter = ParseSource(
	    "std::string Text = R\"abcdefghijklmnop(kept)abcdefghijklmnop\";",
	    "maximum-raw-delimiter.cpp");
	Ok &= Expect(MaximumRawDelimiter.Succeeded() && MaximumRawDelimiter.Unit.Variables.size() == 1 && JoinTokens(MaximumRawDelimiter.Unit.Variables[0].InitializerTokens) == "\"kept\"",
	    "valid raw string delimiters of the maximum length must retain their contents");
	const auto InvalidRawDelimiter = ParseSource(
	    "std::string Text = R\"" + std::string(128, ')') + "(" + std::string(4096, ')') + "\";\n/* unterminated comment", "invalid-raw-delimiter.cpp");
	Ok &= Expect(!InvalidRawDelimiter.Succeeded() && std::any_of(InvalidRawDelimiter.Diagnostics.begin(), InvalidRawDelimiter.Diagnostics.end(), [](const FDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == "cpp.lex.invalid_raw_string_delimiter";
	}) && std::any_of(InvalidRawDelimiter.Diagnostics.begin(), InvalidRawDelimiter.Diagnostics.end(), [](const FDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == "cpp.lex.unterminated_comment";
	}),
	    "invalid raw delimiters must fail while lexing continues to report subsequent errors");
	Ok &= Expect(!ParseSource(
	                 "std::wstring Text = LR\"(wide)\";", "wide_raw_string.cpp")
	                 .Succeeded(),
	    "target-dependent wide raw strings must remain rejected");

	const FParseResult Aliases = ParseSource(R"cpp(
namespace A { using Id = int; Id First(Id Value); }
namespace B { using Id = std::string; Id Second(Id Value); }
namespace A { Id Reopened(Id Value); }
)cpp",
	    "alias_scope.cpp");
	Ok &= Expect(Aliases.Succeeded() && Aliases.Unit.Functions.size() == 3,
	    "namespace-scoped aliases should parse across reopened namespaces");
	Ok &= Expect(Aliases.Unit.TypeAliases.size() == 2,
	    "compatible repeated using aliases must produce stable public AST declarations");
	if (Aliases.Unit.Functions.size() == 3)
	{
		Ok &= Expect(Aliases.Unit.Functions[0].ReturnType.BaseName == "int" && Aliases.Unit.Functions[1].ReturnType.BaseName == "std::string" && Aliases.Unit.Functions[2].ReturnType.BaseName == "int",
		    "a using alias leaked into a different namespace");
	}
	const FParseResult TypedefAliases = ParseSource(
	    "typedef unsigned int Count; namespace A { typedef std::vector<Count> Counts; Counts Read(); }",
	    "typedef_aliases.cpp");
	Ok &= Expect(TypedefAliases.Succeeded() && TypedefAliases.Unit.TypeAliases.size() == 2 && TypedefAliases.Unit.Functions.size() == 1 && TypedefAliases.Unit.Functions[0].ReturnType.BaseName == "std::vector",
	    "ordinary typedef aliases must share the using-alias AST and lexical lookup rules");
	Ok &= Expect(!ParseSource(
	                 "typedef int First, Second;", "multi_typedef.cpp")
	                 .Succeeded(),
	    "multi-declarator typedefs must remain explicit instead of being partially accepted");
	const FParseResult ScopedTypes = ParseSource(R"cpp(
namespace A { struct Item { int Value; }; Item Make(); }
namespace B { struct Item { int Value; }; Item Make(); }
)cpp",
	    "scoped_types.cpp");
	Ok &= Expect(ScopedTypes.Succeeded() && ScopedTypes.Unit.Functions.size() == 2,
	    "duplicate leaf type names in distinct namespaces should parse");
	if (ScopedTypes.Unit.Functions.size() == 2)
	{
		FTypeMapOptions ScopedOptions;
		ScopedOptions.KnownHostTypes = {"A::Item", "B::Item"};
		Ok &= Expect(MapTypeToVeroScript(
		                 ScopedTypes.Unit.Functions[0].ReturnType, ScopedOptions)
		                 .VeroType == "A::Item" &&
		        MapTypeToVeroScript(
		            ScopedTypes.Unit.Functions[1].ReturnType, ScopedOptions)
		                .VeroType == "B::Item",
		    "unqualified user types should resolve against their declaration namespace");
	}
	const FParseResult GenericAlias = ParseSource(
	    "template<typename T> using Values = std::vector<T>;",
	    "generic_alias.cpp");
	Ok &= Expect(GenericAlias.Succeeded() && GenericAlias.Unit.TypeAliases.size() == 1 && GenericAlias.Unit.TypeAliases[0].TemplateParameters.size() == 1 && GenericAlias.Unit.TypeAliases[0].TargetType.TemplateArguments.size() == 1,
	    "C++ alias templates must retain target and type parameters in the public AST");
	Ok &= Expect(!ParseSource(
	                 "template<typename T = int, typename U> struct Invalid {};",
	                 "non_trailing_template_default.cpp")
	                 .Succeeded(),
	    "template defaults that VeroScript cannot preserve must fail before generation");
	const FParseResult UsingNamespace = ParseSource(R"cpp(
namespace Values { struct Item { int Value; }; }
namespace Consumer { using namespace Values; Item Make(Item Value); }
)cpp",
	    "using_namespace.cpp");
	Ok &= Expect(UsingNamespace.Succeeded() && UsingNamespace.Unit.UsingNamespaces.size() == 1 && UsingNamespace.Unit.UsingNamespaces[0].Scope == "Consumer" && UsingNamespace.Unit.UsingNamespaces[0].NamespaceName == "Values",
	    "using namespace declarations must retain their lexical scope");
	const FParseResult Attributes = ParseSource(R"cpp(
[[veroscript::Type(Serializable, Version=1)]]
struct Reflected
{
    [[veroscript::Field(Replicated)]] int Value;
    [[veroscript::Call(Category="Test")]]
    int Read([[veroscript::Param(Input)]] int Amount) const;
};
[[veroscript::Function(Category="Utility")]] int Utility(int Value);
)cpp",
	    "attributes.cpp");
	if (!Attributes.Succeeded())
	{
		for (const FDiagnostic& Diagnostic : Attributes.Diagnostics)
		{
			std::cerr << FormatDiagnostic(Diagnostic) << '\n';
		}
	}
	Ok &= Expect(Attributes.Succeeded() && Attributes.Unit.Records.size() == 1 && Attributes.Unit.Records[0].Attributes.size() == 1 && Attributes.Unit.Records[0].Attributes[0].NamespaceName == "veroscript" && Attributes.Unit.Records[0].Attributes[0].Arguments.size() == 2 && Attributes.Unit.Records[0].Fields[0].Attributes.size() == 1 && Attributes.Unit.Records[0].Methods[0].Attributes.size() == 1 && Attributes.Unit.Records[0].Methods[0].Parameters[0].Attributes.size() == 1 && Attributes.Unit.Functions[0].Attributes.size() == 1,
	    "C++ declaration and parameter attributes must remain structured in the frontend AST");

	FType RawPointer;
	RawPointer.Spelling = "Widget*";
	RawPointer.BaseName = "Widget";
	RawPointer.PointerDepth = 1;
	FType PlainChar;
	PlainChar.Spelling = "char";
	PlainChar.BaseName = "char";
	Ok &= Expect(MapTypeToVeroScript(PlainChar).bSuccess && MapTypeToVeroScript(PlainChar).VeroType == (std::numeric_limits<char>::is_signed ? "char" : "uint8"),
	    "plain C++ char must follow the target ABI's signedness");
	std::int64_t CharMinimum = 0;
	std::int64_t CharMaximum = 0;
	Ok &= Expect(TryGetEnumValueRange("char", CharMinimum, CharMaximum) && CharMinimum == static_cast<std::int64_t>(std::numeric_limits<char>::min()) && CharMaximum == static_cast<std::int64_t>(std::numeric_limits<char>::max()),
	    "plain-char enum range must follow the target ABI's signedness");
	FTypeMapOptions PointerOptions;
	PointerOptions.KnownHostTypes.push_back("Widget");
	Ok &= Expect(!MapTypeToVeroScript(RawPointer, PointerOptions).bSuccess,
	    "raw pointers must require an explicit policy");
	PointerOptions.bAllowRawHostPointers = true;
	Ok &= Expect(MapTypeToVeroScript(RawPointer, PointerOptions).bSuccess,
	    "explicit raw-pointer policy should permit a known host-object pointer");
	FType ConstPointee = RawPointer;
	ConstPointee.Spelling = "const Widget*";
	ConstPointee.bConst = true;
	Ok &= Expect(!MapTypeToVeroScript(ConstPointee, PointerOptions).bSuccess,
	    "raw-pointer mapping must not erase pointee constness");
	FType ConstPointer = RawPointer;
	ConstPointer.Spelling = "Widget* const";
	ConstPointer.bPointerConst = true;
	Ok &= Expect(!MapTypeToVeroScript(ConstPointer, PointerOptions).bSuccess,
	    "raw-pointer mapping must not erase pointer storage qualifiers");
	FType ScalarPointer;
	ScalarPointer.Spelling = "int*";
	ScalarPointer.BaseName = "int";
	ScalarPointer.PointerDepth = 1;
	Ok &= Expect(!MapTypeToVeroScript(ScalarPointer, PointerOptions).bSuccess,
	    "raw-pointer policy must not erase a scalar pointer to a scalar value");
	FType PointerToPointer = RawPointer;
	PointerToPointer.Spelling = "Widget**";
	PointerToPointer.PointerDepth = 2;
	Ok &= Expect(!MapTypeToVeroScript(PointerToPointer, PointerOptions).bSuccess,
	    "raw-pointer policy must not erase multi-level pointer semantics");
	FType SizeType;
	SizeType.Spelling = "std::size_t";
	SizeType.BaseName = "std::size_t";
	Ok &= Expect(MapTypeToVeroScript(SizeType).bSuccess,
	    "std::size_t must map according to the target pointer width");
	FType SharedType;
	SharedType.Spelling = "std::shared_ptr<Widget>";
	SharedType.BaseName = "std::shared_ptr";
	FType SharedArgument;
	SharedArgument.Spelling = "Widget";
	SharedArgument.BaseName = "Widget";
	SharedType.TemplateArguments.push_back(SharedArgument);
	Ok &= Expect(!MapTypeToVeroScript(SharedType, PointerOptions).bSuccess,
	    "shared ownership must not be erased to the pointee type");

	const FParseResult FirstUnit = ParseSource(R"cpp(
class Shared;
int Compute(const int Value = 3);
)cpp",
	    "first.hpp");
	const FParseResult SecondUnit = ParseSource(R"cpp(
struct Shared { int Value; };
int Compute(int Value) { return Value; }
)cpp",
	    "second.cpp");
	const FParseResult Merged = MergeTranslationUnits(
	    {FirstUnit.Unit, SecondUnit.Unit});
	Ok &= Expect(FirstUnit.Succeeded() && SecondUnit.Succeeded() && Merged.Succeeded() && Merged.Unit.Records.size() == 1 && !Merged.Unit.Records[0].bForwardDeclaration && Merged.Unit.Functions.size() == 1 && Merged.Unit.Functions[0].bHasBody && Merged.Unit.Functions[0].Parameters[0].DefaultValue == "3",
	    "translation-unit merge must combine forward types and compatible function redeclarations");
	const FParseResult TemplateDeclaration = ParseSource(
	    "template<typename T> T Identity(T Value);", "template_decl.hpp");
	const FParseResult TemplateDefinition = ParseSource(
	    "template<typename U> U Identity(U Value) { return Value; }",
	    "template_def.cpp");
	const FParseResult MergedTemplate = MergeTranslationUnits(
	    {TemplateDeclaration.Unit, TemplateDefinition.Unit});
	Ok &= Expect(TemplateDeclaration.Succeeded() && TemplateDefinition.Succeeded() && MergedTemplate.Succeeded() && MergedTemplate.Unit.Functions.size() == 1 && MergedTemplate.Unit.Functions[0].bHasBody,
	    "template parameter spelling must not split compatible function redeclarations");
	const FParseResult AliasUnit = ParseSource(
	    "namespace Shared { using Count = int; "
	    "template<typename T = int> using List = std::vector<T>; }",
	    "aliases.hpp");
	const FParseResult AliasUseUnit = ParseSource(
	    "Shared::Count CountValues(Shared::List<> Values);",
	    "alias_use.cpp");
	const FParseResult MergedAliases = MergeTranslationUnits(
	    {AliasUnit.Unit, AliasUseUnit.Unit});
	Ok &= Expect(AliasUnit.Succeeded() && AliasUseUnit.Succeeded() && MergedAliases.Succeeded() && MergedAliases.Unit.TypeAliases.size() == 2 && MergedAliases.Unit.Functions.size() == 1 && MergedAliases.Unit.Functions[0].ReturnType.BaseName == "int" && MergedAliases.Unit.Functions[0].Parameters[0].Type.BaseName == "std::vector" && MergedAliases.Unit.Functions[0].Parameters[0].Type.TemplateArguments.size() == 1 && MergedAliases.Unit.Functions[0].Parameters[0].Type.TemplateArguments[0].BaseName == "int",
	    "project merge must resolve ordinary and generic aliases across input files");
	const FParseResult ScopedAliasUnit = ParseSource(R"cpp(
namespace Owner { struct Item { int Value; }; using ItemAlias = Item; }
namespace Owner { using IntRef = int&; }
)cpp",
	    "scoped_alias.hpp");
	const FParseResult ScopedAliasUseUnit = ParseSource(R"cpp(
namespace Consumer
{
    struct Item { int Other; };
    Owner::ItemAlias Make();
    Owner::IntRef&& Collapse();
}
)cpp",
	    "scoped_alias_use.cpp");
	const FParseResult MergedScopedAliases = MergeTranslationUnits(
	    {ScopedAliasUnit.Unit, ScopedAliasUseUnit.Unit});
	FTypeMapOptions ScopedAliasMapOptions;
	ScopedAliasMapOptions.KnownHostTypes = {"Owner::Item", "Consumer::Item"};
	Ok &= Expect(ScopedAliasUnit.Succeeded() && ScopedAliasUseUnit.Succeeded() && MergedScopedAliases.Succeeded() && MergedScopedAliases.Unit.Functions.size() == 2 && MergedScopedAliases.Unit.Functions[0].ReturnType.DeclarationScope == "Owner" && MapTypeToVeroScript(MergedScopedAliases.Unit.Functions[0].ReturnType, ScopedAliasMapOptions).VeroType == "Owner::Item" && MergedScopedAliases.Unit.Functions[1].ReturnType.Reference == EReferenceKind::LValue,
	    "cross-file aliases must keep declaration lookup scope and apply C++ reference collapsing");
	const auto GlobalAliasDeclarations = ParseSource(R"cpp(
struct RootItem { int Value; };
using RootAlias = RootItem;
namespace N {
    struct RootItem { double Other; };
    template<typename T> using Identity = T;
}
using NestedIdentity = N::Identity<RootItem>;
)cpp",
	    "global_alias.hpp");
	const auto GlobalAliasUses = ParseSource(
	    "namespace N { RootAlias Make(); NestedIdentity MakeNested(); }", "global_alias_use.cpp");
	const auto MergedGlobalAliases = MergeTranslationUnits({GlobalAliasDeclarations.Unit, GlobalAliasUses.Unit});
	FTypeMapOptions GlobalAliasOptions;
	GlobalAliasOptions.KnownHostTypes = {"RootItem", "N::RootItem"};
	GlobalAliasOptions.KnownTypeAliases = GlobalAliasDeclarations.Unit.TypeAliases;
	Ok &= Expect(GlobalAliasDeclarations.Succeeded() && GlobalAliasUses.Succeeded() && MergedGlobalAliases.Succeeded() && MergedGlobalAliases.Unit.Functions.size() == 2 && MapTypeToVeroScript(MergedGlobalAliases.Unit.Functions[0].ReturnType, GlobalAliasOptions).VeroType == "RootItem" && MapTypeToVeroScript(MergedGlobalAliases.Unit.Functions[1].ReturnType, GlobalAliasOptions).VeroType == "RootItem",
	    "global alias targets and substituted global arguments must not inherit the caller namespace");
	FType GlobalAliasUse;
	GlobalAliasUse.BaseName = "RootAlias";
	GlobalAliasUse.DeclarationScope = "N";
	Ok &= Expect(MapTypeToVeroScript(GlobalAliasUse, GlobalAliasOptions).VeroType == "RootItem",
	    "manual alias mapping must preserve an empty global declaration scope");
	GlobalAliasOptions.bQualifyNamedTypes = true;
	Ok &= Expect(MapTypeToVeroScript(GlobalAliasUse, GlobalAliasOptions).VeroType == "::RootItem",
	    "source emission can request absolute names while catalogs retain canonical names by default");
	FType GlobalArray;
	GlobalArray.BaseName = "std::vector";
	GlobalArray.TemplateArguments.push_back(GlobalAliasUse);
	Ok &= Expect(MapTypeToVeroScript(GlobalArray, GlobalAliasOptions).VeroType == "array<::RootItem>",
	    "absolute name emission must also qualify nested template arguments");
	const auto BaseDeclaration = ParseSource("namespace N{class Base{};using Alias=Base;}", "base_decl.hpp");
	const auto BaseUse = ParseSource("namespace N{class Child:public Alias{};}", "base_use.cpp");
	const auto MergedBases = MergeTranslationUnits({BaseDeclaration.Unit, BaseUse.Unit});
	Ok &= Expect(BaseDeclaration.Succeeded() && BaseUse.Succeeded() && MergedBases.Succeeded() && MergedBases.Unit.Records.size() == 2 && MergedBases.Unit.Records[1].BaseTypes.size() == 1 && MergedBases.Unit.Records[1].BaseTypes[0].Type.BaseName == "Base" && MergedBases.Unit.Records[1].BaseTypes[0].Type.DeclarationScope == "N",
	    "base types must resolve aliases across files without losing their declaration scope");
	const std::string QualifiedAliasDeclarations = R"cpp(
using Choice = int;
namespace Nested { using Choice = bool; }
namespace N {
    using Choice = double;
    namespace Nested { using Choice = std::string; }
}
)cpp";
	const std::string QualifiedAliasUses = R"cpp(
namespace N {
    ::Choice Absolute();
    Nested::Choice Relative();
    ::Nested::Choice GlobalQualified();
    Choice Local();
}
)cpp";
	const FParseResult QualifiedAliasUnit = ParseSource(
	    QualifiedAliasDeclarations, "qualified_alias.hpp");
	const FParseResult QualifiedAliasUseUnit = ParseSource(
	    QualifiedAliasUses, "qualified_alias_use.cpp");
	const FParseResult MergedQualifiedAliases = MergeTranslationUnits(
	    {QualifiedAliasUnit.Unit, QualifiedAliasUseUnit.Unit});
	const auto HasQualifiedAliases = [](const FParseResult& Result)
	{
		return Result.Succeeded() && Result.Unit.Functions.size() == 4 && Result.Unit.Functions[0].ReturnType.BaseName == "int" && Result.Unit.Functions[1].ReturnType.BaseName == "std::string" && Result.Unit.Functions[2].ReturnType.BaseName == "bool" && Result.Unit.Functions[3].ReturnType.BaseName == "double";
	};
	Ok &= Expect(QualifiedAliasUnit.Succeeded() && QualifiedAliasUseUnit.Succeeded() && HasQualifiedAliases(MergedQualifiedAliases),
	    "cross-file aliases must distinguish absolute and relative qualified names");
	Ok &= Expect(HasQualifiedAliases(ParseSource(
	                 QualifiedAliasDeclarations + QualifiedAliasUses, "qualified_alias.cpp")),
	    "single-file aliases must distinguish absolute and relative qualified names");
	FTypeMapOptions QualifiedAliasOptions;
	QualifiedAliasOptions.KnownTypeAliases = QualifiedAliasUnit.Unit.TypeAliases;
	FType QualifiedAliasType;
	QualifiedAliasType.BaseName = "::Choice";
	QualifiedAliasType.DeclarationScope = "N";
	Ok &= Expect(MapTypeToVeroScript(QualifiedAliasType, QualifiedAliasOptions).VeroType == "int",
	    "direct alias mapping must preserve the absolute qualifier");
	QualifiedAliasType.BaseName = "Nested::Choice";
	Ok &= Expect(MapTypeToVeroScript(QualifiedAliasType, QualifiedAliasOptions).VeroType == "string",
	    "direct alias mapping must search relative qualified names in lexical scopes");
	QualifiedAliasType.BaseName = "Missing::Choice";
	Ok &= Expect(!MapTypeToVeroScript(QualifiedAliasType, QualifiedAliasOptions).bSuccess,
	    "unknown qualified aliases must not bind an unrelated alias with the same leaf name");
	const std::string ShadowedAliasDeclarations = R"cpp(
using RecordValue = int;
using EnumValue = int;
namespace N {
    struct RecordValue { RecordValue* next; };
    enum class EnumValue { Item };
}
)cpp";
	const std::string ShadowedAliasUses = R"cpp(
namespace N {
    RecordValue Make();
    EnumValue Mode();
    ::RecordValue Global();
}
)cpp";
	const auto ShadowedAliasUnit = ParseSource(ShadowedAliasDeclarations, "shadowed_alias.hpp");
	const auto ShadowedAliasUseUnit = ParseSource(ShadowedAliasUses, "shadowed_alias_use.cpp");
	const auto CheckShadowedAliases = [&](const FParseResult& Result)
	{
		FTypeMapOptions Names;
		Names.KnownHostTypes = {"N::RecordValue"};
		Names.KnownEnumTypes = {"N::EnumValue"};
		Names.KnownTypeAliases = Result.Unit.TypeAliases;
		auto Pointers = Names;
		Pointers.bAllowRawHostPointers = true;
		return Result.Succeeded() && Result.Unit.Functions.size() == 3 && Result.Unit.Records.size() == 1 && MapTypeToVeroScript(Result.Unit.Functions[0].ReturnType, Names).VeroType == "N::RecordValue" && MapTypeToVeroScript(Result.Unit.Functions[1].ReturnType, Names).VeroType == "N::EnumValue" && MapTypeToVeroScript(Result.Unit.Functions[2].ReturnType, Names).VeroType == "int" && MapTypeToVeroScript(Result.Unit.Records[0].Fields[0].Type, Pointers).VeroType == "N::RecordValue";
	};
	Ok &= Expect(CheckShadowedAliases(ParseSource(ShadowedAliasDeclarations + ShadowedAliasUses,
	                 "shadowed_alias.cpp")),
	    "single-file records and enums must shadow outer aliases, including injected class names");
	Ok &= Expect(ShadowedAliasUnit.Succeeded() && ShadowedAliasUseUnit.Succeeded() && CheckShadowedAliases(MergeTranslationUnits({ShadowedAliasUnit.Unit, ShadowedAliasUseUnit.Unit})),
	    "cross-file records and enums must shadow outer aliases");
	FType ShadowedAliasType;
	ShadowedAliasType.BaseName = "RecordValue";
	ShadowedAliasType.Spelling = "RecordValue";
	ShadowedAliasType.DeclarationScope = "N";
	FTypeMapOptions ShadowedAliasOptions;
	ShadowedAliasOptions.KnownHostTypes = {"N::RecordValue"};
	ShadowedAliasOptions.KnownTypeAliases = ShadowedAliasUnit.Unit.TypeAliases;
	Ok &= Expect(MapTypeToVeroScript(ShadowedAliasType, ShadowedAliasOptions).VeroType == "N::RecordValue",
	    "direct type mapping must consider records before selecting an outer alias");
	Ok &= Expect(ResolveTypeAliases(ShadowedAliasType, ShadowedAliasOptions.KnownTypeAliases,
	                 ShadowedAliasOptions)
	                 .BaseName == "RecordValue",
	    "context-aware alias resolution must preserve a nearer record");
	{
		const auto Parameters = ParseSource("using T=int; template<typename T> struct Box {T Value;}; "
		                                    "template<typename T> T Identity(T Value); template<typename T> using List=std::vector<T>;",
		    "parameter_alias_shadow.cpp");
		const auto MergedParameters = MergeTranslationUnits({Parameters.Unit});
		Ok &= Expect(Parameters.Succeeded() && MergedParameters.Succeeded() && MergedParameters.Unit.Records[0].Fields[0].Type.BaseName == "T" && Parameters.Unit.Functions[0].ReturnType.BaseName == "T" && Parameters.Unit.Functions[0].Parameters[0].Type.BaseName == "T" && Parameters.Unit.TypeAliases[1].TargetType.TemplateArguments[0].BaseName == "T",
		    "template parameters must shadow outer type aliases in records, functions and aliases");
		FType ParameterType;
		ParameterType.BaseName = "T";
		FTypeMapOptions ParameterOptions;
		ParameterOptions.KnownTypeParameters = {"T"};
		ParameterOptions.KnownTypeAliases = Parameters.Unit.TypeAliases;
		Ok &= Expect(MapTypeToVeroScript(ParameterType, ParameterOptions).VeroType == "T",
		    "direct type mapping must not bind a template parameter to an outer alias");
		ParameterType.BaseName = "::T";
		Ok &= Expect(MapTypeToVeroScript(ParameterType, ParameterOptions).VeroType == "int",
		    "absolute aliases bypass a same-named template parameter");
		const auto GenericUses = ParseSource("template<typename T> List<T> Gather(List<T> Values);",
		    "parameter_alias_use.cpp");
		const auto MergedUses = MergeTranslationUnits({Parameters.Unit, GenericUses.Unit});
		Ok &= Expect(GenericUses.Succeeded() && MergedUses.Succeeded() && MergedUses.Unit.Functions.size() == 2 && MergedUses.Unit.Functions[1].ReturnType.TemplateArguments[0].BaseName == "T",
		    "generic alias substitution must preserve the bound argument instead of resolving its name again");
	}
	const FParseResult RecordTemplateForward = ParseSource(
	    "template<typename T, typename U = T> struct PairBox;",
	    "template_record_decl.hpp");
	const FParseResult RecordTemplateDefinition = ParseSource(
	    "template<typename A, typename B> struct PairBox { A First; B Second; };",
	    "template_record_def.hpp");
	const FParseResult MergedRecordTemplate = MergeTranslationUnits(
	    {RecordTemplateForward.Unit, RecordTemplateDefinition.Unit});
	Ok &= Expect(RecordTemplateForward.Succeeded() && RecordTemplateDefinition.Succeeded() && MergedRecordTemplate.Succeeded() && MergedRecordTemplate.Unit.Records.size() == 1 && MergedRecordTemplate.Unit.Records[0].TemplateParameters[1].bHasDefault && MergedRecordTemplate.Unit.Records[0].TemplateParameters[1].DefaultType.BaseName == "A",
	    "record template defaults must survive renamed forward/definition parameters");
	Ok &= Expect(!MergeTranslationUnits({ParseSource("template<typename T> struct Wrong;", "wrong_a.hpp").Unit,
	                                        ParseSource("template<typename T, typename U> struct Wrong;", "wrong_b.hpp").Unit})
	                 .Succeeded(),
	    "incompatible record template parameter lists must be diagnosed");
	const FParseResult ThirdUnit = ParseSource(
	    "int Compute(int Value) { return Value + 1; }", "third.cpp");
	Ok &= Expect(!MergeTranslationUnits(
	                 {FirstUnit.Unit, SecondUnit.Unit, ThirdUnit.Unit})
	                 .Succeeded(),
	    "translation-unit merge must reject duplicate function definitions");

	const FParseResult VariableDeclaration = ParseSource(R"cpp(
extern int SharedValue;
int Named(int Value);
)cpp",
	    "variable_decl.hpp");
	const FParseResult VariableDefinition = ParseSource(R"cpp(
int SharedValue = 4;
int Named(int) { return 1; }
)cpp",
	    "variable_def.cpp");
	const FParseResult MergedVariables = MergeTranslationUnits(
	    {VariableDeclaration.Unit, VariableDefinition.Unit});
	Ok &= Expect(VariableDeclaration.Succeeded() && VariableDefinition.Succeeded() && MergedVariables.Succeeded() && MergedVariables.Unit.Variables.size() == 1 && !MergedVariables.Unit.Variables[0].InitializerTokens.empty() && MergedVariables.Unit.Functions.size() == 1 && MergedVariables.Unit.Functions[0].Parameters.size() == 1 && MergedVariables.Unit.Functions[0].Parameters[0].Name == "Value",
	    "translation-unit merge must combine extern variables and retain declaration parameter names");
	Ok &= Expect(!MergeTranslationUnits({ParseSource("int Duplicate = 1;", "variable_a.cpp").Unit,
	                                        ParseSource("int Duplicate = 2;", "variable_b.cpp").Unit})
	                 .Succeeded(),
	    "translation-unit merge must reject duplicate variable definitions");
	Ok &= Expect(!MergeTranslationUnits({ParseSource("extern int Conflict;", "variable_type_a.hpp").Unit,
	                                        ParseSource("double Conflict = 2.0;", "variable_type_b.cpp").Unit})
	                 .Succeeded(),
	    "translation-unit merge must reject conflicting variable types");
	Ok &= Expect(!MergeTranslationUnits({ParseSource("static int Hidden = 1;", "static_a.cpp").Unit,
	                                        ParseSource("static int Hidden = 2;", "static_b.cpp").Unit})
	                 .Succeeded(),
	    "translation-unit merge must reject cross-file internal-linkage variable collisions");
	Ok &= Expect(!MergeTranslationUnits({ParseSource("int ValueOrFunction;", "value_first.cpp").Unit,
	                                        ParseSource("int ValueOrFunction();", "function_second.cpp").Unit})
	                 .Succeeded(),
	    "translation-unit merge must reject a variable followed by a function with the same name");
	Ok &= Expect(!MergeTranslationUnits({ParseSource("int FunctionOrValue();", "function_first.cpp").Unit,
	                                        ParseSource("int FunctionOrValue;", "value_second.cpp").Unit})
	                 .Succeeded(),
	    "translation-unit merge must reject a function followed by a variable with the same name");
	Ok &= Expect(!ParseSource(
	                 "int SameFileValue; int SameFileValue();", "same_file_value.cpp")
	                 .Succeeded(),
	    "single-source parsing must apply the same variable/function conflict validation");
	Ok &= Expect(!ParseSource(
	                 "int SameFileDefinition() { return 1; } "
	                 "int SameFileDefinition() { return 2; }",
	                 "same_file_definition.cpp")
	                 .Succeeded(),
	    "single-source parsing must reject duplicate function definitions");
	const FParseResult SameFileRedeclarations = ParseSource(
	    "extern int SameFileExternal; int SameFileExternal = 4; "
	    "int SameFileFunction(int Value); "
	    "int SameFileFunction(int Value) { return Value; }",
	    "same_file_redeclarations.cpp");
	Ok &= Expect(SameFileRedeclarations.Succeeded() && SameFileRedeclarations.Unit.Variables.size() == 1 && SameFileRedeclarations.Unit.Functions.size() == 1 && SameFileRedeclarations.Unit.Functions[0].bHasBody,
	    "single-source parsing must normalize compatible declaration/definition pairs");
	Ok &= Expect(!ParseSource(
	                 "using Number = int; using Number = double;",
	                 "conflicting_alias.cpp")
	                 .Succeeded(),
	    "conflicting using aliases in one scope must be rejected");
	Ok &= Expect(ParseSource(
	                 "using Number = int; using Number = signed int; Number Value = 1;",
	                 "compatible_alias.cpp")
	                 .Succeeded(),
	    "equivalent repeated using aliases must remain valid across C++ spellings");
	Ok &= Expect(!ParseSource(
	                 "using Name = int; int Name();",
	                 "alias_then_function.cpp")
	                 .Succeeded(),
	    "a using alias must not share its name with a function");
	Ok &= Expect(!ParseSource(
	                 "int Value = 1; using Value = int;",
	                 "variable_then_alias.cpp")
	                 .Succeeded(),
	    "alias/value conflict validation must be independent of declaration order");
	Ok &= Expect(!ParseSource(
	                 "struct Record {}; using Record = int;",
	                 "record_then_alias.cpp")
	                 .Succeeded(),
	    "a using alias must not share its name with a record");
	const FParseResult EquivalentParameterTypes = ParseSource(
	    "int Equivalent(int Value); int Equivalent(signed int Value) { return Value; }",
	    "equivalent_parameter_types.cpp");
	Ok &= Expect(EquivalentParameterTypes.Succeeded() && EquivalentParameterTypes.Unit.Functions.size() == 1 && EquivalentParameterTypes.Unit.Functions[0].bHasBody,
	    "equivalent fundamental spellings must identify one function declaration");
	const FParseResult FixedWidthParameterTypes = ParseSource(
	    "int FixedWidth(std::int32_t Value); int FixedWidth(int Value) { return Value; }",
	    "fixed_width_parameter_types.cpp");
	Ok &= Expect(FixedWidthParameterTypes.Succeeded() && FixedWidthParameterTypes.Unit.Functions.size() == 1,
	    "fixed-width typedefs must use their target-platform C++ type identity");
	const FParseResult DistinctSameWidthTypes = ParseSource(
	    "int Distinct(int Value); int Distinct(long Value);",
	    "distinct_same_width_types.cpp");
	Ok &= Expect(DistinctSameWidthTypes.Succeeded() && DistinctSameWidthTypes.Unit.Functions.size() == 2,
	    "different fundamental types must remain overload-distinct even when equally wide");
	Ok &= Expect(!ParseSource(
	                 "struct DuplicateField { int Value; int Value; };",
	                 "duplicate_record_field.cpp")
	                 .Succeeded(),
	    "record fields must have unique names");
	Ok &= Expect(!ParseSource(
	                 "struct FieldMethodConflict { int Value; int Value(); };",
	                 "record_field_method_conflict.cpp")
	                 .Succeeded(),
	    "a record field and method must not share a name");
	Ok &= Expect(!ParseSource(
	                 "struct DuplicateMethod { int Run(int Value); double Run(signed int Value); };",
	                 "duplicate_record_method.cpp")
	                 .Succeeded(),
	    "return type and equivalent parameter spelling must not create a member overload");
	const FParseResult LegalMemberOverloads = ParseSource(
	    "struct MemberOverloads { "
	    "int Run(int Value); int Run(double Value); "
	    "int Read(); int Read() const; };",
	    "legal_member_overloads.cpp");
	Ok &= Expect(LegalMemberOverloads.Succeeded() && LegalMemberOverloads.Unit.Records.size() == 1 && LegalMemberOverloads.Unit.Records[0].Methods.size() == 4,
	    "parameter and const-qualified member overloads must remain distinct");
	Ok &= Expect(!ParseSource(
	                 "struct InvalidConstant { static constexpr int Value; };",
	                 "constexpr_without_initializer.cpp")
	                 .Succeeded(),
	    "constexpr fields without an initializer must be rejected");
	Ok &= Expect(!ParseSource(
	                 "enum class DuplicateEnum { Value, Value };",
	                 "duplicate_enumerator.cpp")
	                 .Succeeded(),
	    "enumerator names must be unique within an enum");
	Ok &= Expect(!ParseSource(
	                 "struct RepeatedBase : public Base, private Base {};",
	                 "duplicate_base.cpp")
	                 .Succeeded(),
	    "a record must not declare the same direct base more than once");
	Ok &= Expect(!ParseSource(
	                 "using Parent = Base; struct RepeatedAliasBase : Base, Parent {};",
	                 "duplicate_alias_base.cpp")
	                 .Succeeded(),
	    "base aliases must be resolved before duplicate-base validation");
	Ok &= Expect(!ParseSource(
	                 "struct Recursive : Recursive {};",
	                 "self_inheritance.cpp")
	                 .Succeeded(),
	    "a record must not derive directly from itself");
	Ok &= Expect(!ParseSource(
	                 "struct SameName { int SameName; };",
	                 "field_matches_record.cpp")
	                 .Succeeded(),
	    "a field must not use its enclosing record name");
	Ok &= Expect(!ParseSource(
	                 "void DuplicateParameter(int Value, double Value);",
	                 "duplicate_parameter.cpp")
	                 .Succeeded(),
	    "explicit parameter names must be unique within a function");
	Ok &= Expect(!ParseSource(
	                 "void EmptyDefault(int Value =);",
	                 "empty_default.cpp")
	                 .Succeeded(),
	    "a default argument must contain an expression");
	Ok &= Expect(!ParseSource(
	                 "void MissingTrailingDefault(int First = 1, int Second);",
	                 "missing_trailing_default.cpp")
	                 .Succeeded(),
	    "parameters following a default argument must also have defaults");
	const FParseResult AccumulatedDefaults = ParseSource(
	    "void Accumulated(int First, int Second = 2); "
	    "void Accumulated(int First = 1, int Second);",
	    "accumulated_defaults.cpp");
	Ok &= Expect(AccumulatedDefaults.Succeeded() && AccumulatedDefaults.Unit.Functions.size() == 1 && !AccumulatedDefaults.Unit.Functions[0].Parameters[0].DefaultValueTokens.empty() && !AccumulatedDefaults.Unit.Functions[0].Parameters[1].DefaultValueTokens.empty(),
	    "defaults accumulated from earlier declarations must satisfy C++ ordering rules");
	const FParseResult TopLevelPointerConst = ParseSource(
	    "struct Host {}; void PointerCv(Host* const Value); "
	    "void PointerCv(Host* Value);",
	    "top_level_pointer_const.cpp");
	Ok &= Expect(TopLevelPointerConst.Succeeded() && TopLevelPointerConst.Unit.Functions.size() == 1,
	    "top-level cv on a by-value pointer must not create a C++ overload");
	const FParseResult PointeeConstOverload = ParseSource(
	    "struct Host {}; void PointeeCv(const Host* Value); "
	    "void PointeeCv(Host* Value);",
	    "pointee_const_overload.cpp");
	Ok &= Expect(PointeeConstOverload.Succeeded() && PointeeConstOverload.Unit.Functions.size() == 2,
	    "pointee constness must remain part of pointer-parameter overload identity");
	const FParseResult PointerAliasCv = ParseSource(
	    "struct Host {}; using HostPtr = Host*; "
	    "void AliasCv(const HostPtr Value); void AliasCv(HostPtr Value);",
	    "pointer_alias_cv.cpp");
	Ok &= Expect(PointerAliasCv.Succeeded() && PointerAliasCv.Unit.Functions.size() == 1,
	    "cv applied to a pointer alias must qualify the pointer rather than its pointee");
	Ok &= Expect(!ParseSource(
	                 "int Demo::Run() { return 1; }",
	                 "out_of_class_method.cpp")
	                 .Succeeded(),
	    "out-of-class qualified method definitions must fail closed");
	Ok &= Expect(!ParseSource(
	                 "int Demo::operator+(int Value) { return Value; }",
	                 "out_of_class_operator.cpp")
	                 .Succeeded(),
	    "out-of-class qualified operator definitions must fail closed");
	const FParseResult ComparisonOperator = ParseSource(
	    "struct Comparable { bool operator<(const Comparable& Other) const; };",
	    "comparison_operator.cpp");
	Ok &= Expect(ComparisonOperator.Succeeded() && ComparisonOperator.Unit.Records.size() == 1 && ComparisonOperator.Unit.Records[0].Methods.size() == 1 && ComparisonOperator.Unit.Records[0].Methods[0].Name == "operator<",
	    "operator< must not be misclassified as a template argument list");
	const FParseResult BracketOperators = ParseSource(R"cpp(
namespace Demo {
struct Callable {
    int operator()(const int& Value) const;
    int operator()() const { return 2; }
    int& operator[](const int& Index) { return Value; }
    int Value = 1;
};
int Callable::operator()(const int& Value) const { return Value; }
}
)cpp",
	    "bracket_operators.cpp");
	Ok &= Expect(BracketOperators.Succeeded() && BracketOperators.Unit.Records.size() == 1 && BracketOperators.Unit.Records[0].Methods.size() == 3 && BracketOperators.Unit.Records[0].Methods[0].Name == "operator()" && BracketOperators.Unit.Records[0].Methods[0].bHasBody && BracketOperators.Unit.Records[0].Methods[0].Parameters.size() == 1 && BracketOperators.Unit.Records[0].Methods[0].Parameters[0].Type.bConst && BracketOperators.Unit.Records[0].Methods[0].Parameters[0].Type.Reference == EReferenceKind::LValue && BracketOperators.Unit.Records[0].Methods[1].Name == "operator()" && BracketOperators.Unit.Records[0].Methods[1].Parameters.empty() && BracketOperators.Unit.Records[0].Methods[2].Name == "operator[]" && BracketOperators.Unit.Records[0].Methods[2].ReturnType.Reference == EReferenceKind::LValue,
	    "call/subscript operators must retain overloads, references, and qualified definitions");
	Ok &= Expect(!ParseSource(
	                 "struct InvalidMutable { mutable const int Value = 1; };",
	                 "mutable_const.cpp")
	                 .Succeeded(),
	    "mutable const data members must be rejected as invalid C++");
	Ok &= Expect(!ParseSource(
	                 "struct InvalidMutableRef { mutable int& Value; };",
	                 "mutable_reference.cpp")
	                 .Succeeded(),
	    "mutable reference data members must be rejected as invalid C++");
	const FParseResult SimpleDeclaratorLists = ParseSource(
	    "struct Coordinates { int X = 1, Y = 2; }; int First = 3, Second = 4;",
	    "simple_declarator_lists.cpp");
	Ok &= Expect(SimpleDeclaratorLists.Succeeded() && SimpleDeclaratorLists.Unit.Records.size() == 1 && SimpleDeclaratorLists.Unit.Records[0].Fields.size() == 2 && SimpleDeclaratorLists.Unit.Variables.size() == 2,
	    "simple same-type field and variable declarator lists must expand deterministically");
	Ok &= Expect(!ParseSource(
	                 "int* Pointer, Scalar;", "mixed_declarators.cpp")
	                 .Succeeded(),
	    "declarator lists with per-name pointer semantics must remain rejected");
	Ok &= Expect(!ParseSource(
	                 "template<typename T> T First, Second;",
	                 "variable_template_declarators.cpp")
	                 .Succeeded(),
	    "multi-declarator variable templates must not bypass the variable-template restriction");
	Ok &= Expect(!ParseSource(
	                 "struct Invalid { template<typename T> static T First, Second; };",
	                 "member_variable_template_declarators.cpp")
	                 .Succeeded(),
	    "multi-declarator member variable templates must not bypass the variable-template restriction");
	const FParseResult InlineStaticField = ParseSource(
	    "struct InlineStorage { inline static int Value = 1; };",
	    "inline_static_field.cpp");
	Ok &= Expect(InlineStaticField.Succeeded() && InlineStaticField.Unit.Records.size() == 1 && InlineStaticField.Unit.Records[0].Fields.size() == 1 && InlineStaticField.Unit.Records[0].Fields[0].bInline,
	    "inline static data-member definition metadata must be retained");
	const FParseResult ExternalStaticField = ParseSource(
	    "struct ExternalStorage { static int Value; }; inline int ExternalStorage::Value = 7;",
	    "external_static_field.cpp");
	Ok &= Expect(ExternalStaticField.Succeeded() && ExternalStaticField.Unit.Variables.empty() && ExternalStaticField.Unit.Records.size() == 1 && ExternalStaticField.Unit.Records[0].Fields.size() == 1 && ExternalStaticField.Unit.Records[0].Fields[0].bDefinedOutOfClass && ExternalStaticField.Unit.Records[0].Fields[0].bInline && !ExternalStaticField.Unit.Records[0].Fields[0].InitializerTokens.empty(),
	    "an out-of-class static data-member definition must merge into its field declaration");
	Ok &= Expect(!ParseSource(
	                 "struct InvalidConstexprStorage { static int Value; }; "
	                 "constexpr int InvalidConstexprStorage::Value = 1;",
	                 "external_static_field_constexpr_mismatch.cpp")
	                 .Succeeded(),
	    "an out-of-class constexpr specifier must not be added after a non-constexpr member declaration");
	const FParseResult QualifiedNamespaceDefinitions = ParseSource(R"cpp(
namespace Qualified { extern int Value; int Read(); }
int Qualified::Value = 4;
int Qualified::Read() { return Value; }
)cpp",
	    "qualified_namespace_definitions.cpp");
	Ok &= Expect(QualifiedNamespaceDefinitions.Succeeded() && QualifiedNamespaceDefinitions.Unit.Variables.size() == 1 && QualifiedNamespaceDefinitions.Unit.Variables[0].QualifiedName == "Qualified::Value" && QualifiedNamespaceDefinitions.Unit.Functions.size() == 1 && QualifiedNamespaceDefinitions.Unit.Functions[0].QualifiedName == "Qualified::Read" && !QualifiedNamespaceDefinitions.Unit.Functions[0].bOutOfClassMember,
	    "qualified namespace definitions must not be mistaken for class members");
	const FParseResult InlineDeclarations = ParseSource(
	    "inline int Helper() { return 1; } inline int SharedValue = 1;",
	    "inline_declarations.cpp");
	Ok &= Expect(InlineDeclarations.Succeeded() && InlineDeclarations.Unit.Functions.size() == 1 && InlineDeclarations.Unit.Functions[0].bInline && InlineDeclarations.Unit.Variables.size() == 1 && InlineDeclarations.Unit.Variables[0].bInline,
	    "inline function and namespace-variable metadata must be retained");
	const FParseResult CLinkage = ParseSource(
	    "extern \"C\" { int Definition; extern int Declaration; int Function(); }",
	    "c_linkage.cpp");
	Ok &= Expect(CLinkage.Succeeded() && CLinkage.Unit.Variables.size() == 2 && CLinkage.Unit.Variables[0].bCLinkage && !CLinkage.Unit.Variables[0].bExtern && CLinkage.Unit.Variables[1].bCLinkage && CLinkage.Unit.Variables[1].bExtern && CLinkage.Unit.Functions.size() == 1 && CLinkage.Unit.Functions[0].bCLinkage,
	    "C language linkage must remain distinct from extern storage duration");
	const FParseResult SingleCDefinition = ParseSource(
	    "extern \"C\" int SingleDefinition;",
	    "single_c_linkage.cpp");
	Ok &= Expect(SingleCDefinition.Succeeded() && SingleCDefinition.Unit.Variables.size() == 1 && SingleCDefinition.Unit.Variables[0].bCLinkage && !SingleCDefinition.Unit.Variables[0].bExtern,
	    "a single C-linkage variable definition must not become an extern declaration");
	Ok &= Expect(!ParseSource(
	                 "extern \"C\" int MixedLinkage(); int MixedLinkage();",
	                 "mixed_function_linkage.cpp")
	                 .Succeeded(),
	    "conflicting function language linkage must be rejected");
	Ok &= Expect(!ParseSource(
	                 "void Removed(); void Removed() = delete;",
	                 "late_deleted_definition.cpp")
	                 .Succeeded(),
	    "a deleted function definition must be rejected when it is not the first declaration");
	const FParseResult DeletedThenDeclared = ParseSource(
	    "void RemovedFirst() = delete; void RemovedFirst();",
	    "deleted_then_declared.cpp");
	Ok &= Expect(DeletedThenDeclared.Succeeded() && DeletedThenDeclared.Unit.Functions.size() == 1 && DeletedThenDeclared.Unit.Functions[0].bDeleted,
	    "a declaration following an initial deleted definition must retain deleted metadata");
	Ok &= Expect(!ParseSource(
	                 "struct InvalidDefault { int Run() = default; };",
	                 "defaulted_ordinary_method.cpp")
	                 .Succeeded(),
	    "ordinary C++ member functions must not be explicitly defaulted");
	Ok &= Expect(!ParseSource(
	                 "struct InvalidDefaultCtor { InvalidDefaultCtor(int Value) = default; };",
	                 "defaulted_converting_constructor.cpp")
	                 .Succeeded(),
	    "non-special converting constructors must not be explicitly defaulted");
	const FParseResult DefaultedSpecialMembers = ParseSource(
	    "struct Specials { Specials() = default; "
	    "Specials(const Specials& Other) = default; "
	    "Specials(Specials&& Other) = default; "
	    "Specials& operator=(const Specials& Other) = default; };",
	    "defaulted_special_members.cpp");
	Ok &= Expect(DefaultedSpecialMembers.Succeeded() && DefaultedSpecialMembers.Unit.Records.size() == 1 && DefaultedSpecialMembers.Unit.Records[0].Methods.size() == 4,
	    "supported default, copy, and move special members must remain defaultable");
	Ok &= Expect(!ParseSource(
	                 "struct BadSuffix { void Run() = delete = default; };",
	                 "duplicate_function_suffix.cpp")
	                 .Succeeded(),
	    "multiple special function suffixes must be rejected");
	Ok &= Expect(!ParseSource(
	                 "struct BadBody { void Run() = delete { } };",
	                 "deleted_function_body.cpp")
	                 .Succeeded(),
	    "a deleted/defaulted/pure declaration must not also contain an inline body");
	const FParseResult PostfixAttribute = ParseSource(
	    "int Read() [[nodiscard]] { return 1; }",
	    "nodiscard_function.cpp");
	Ok &= Expect(PostfixAttribute.Succeeded() && PostfixAttribute.Unit.Functions[0].Attributes.size() == 1,
	    "postfix function attributes must be represented explicitly");
	Ok &= Expect(!ParseSource(
	                 "int Read() nodiscard { return 1; }",
	                 "bare_nodiscard.cpp")
	                 .Succeeded(),
	    "bare attribute identifiers must not be silently ignored");
	Ok &= Expect(ParseSource(
	                 "int Read() [[maybe_unused]] { return 1; }",
	                 "supported_function_attribute.cpp")
	                 .Succeeded(),
	    "supported postfix function attributes must parse structurally");
	const FParseResult PrefixAttribute = ParseSource(
	    "[[nodiscard]] int PrefixedRead() { return 1; }",
	    "prefixed_function_attribute.cpp");
	Ok &= Expect(PrefixAttribute.Succeeded() && PrefixAttribute.Unit.Functions[0].Attributes.size() == 1,
	    "prefix function attributes must be represented explicitly");
	const FParseResult ParameterAttribute = ParseSource(
	    "void AnnotatedParameter([[maybe_unused]] int Value);",
	    "parameter_attribute.cpp");
	Ok &= Expect(ParameterAttribute.Succeeded() && ParameterAttribute.Unit.Functions[0].Parameters[0].Attributes.size() == 1,
	    "parameter attributes must be represented explicitly");
	const FParseResult FieldAttribute = ParseSource(
	    "struct AnnotatedField { [[maybe_unused]] int Value; };",
	    "field_attribute.cpp");
	Ok &= Expect(FieldAttribute.Succeeded() && FieldAttribute.Unit.Records[0].Fields[0].Attributes.size() == 1,
	    "field attributes must be represented explicitly");

	Ok &= Expect(!ParseSource("template<typename T> struct Box { T Read(); }; "
	                          "template<typename T> T Box<T>::Read(){return T();}",
	                 "generic_owner.cpp")
	                 .Succeeded(),
	    "unsupported class-template member definitions must not be silently discarded");
	const std::filesystem::path OutputRoot = std::filesystem::temp_directory_path() / ("veroscript-cpp-output-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::error_code FileError;
	std::filesystem::create_directories(OutputRoot, FileError);
	Ok &= Expect(!ParseFile(Path::ToUtf8(OutputRoot)).Succeeded(),
	    "reading a directory must fail rather than parse as an empty source");
	const auto EmptyInput = OutputRoot / "empty.h";
	{
		std::ofstream EmptyFile(EmptyInput, std::ios::binary);
	}
	Ok &= Expect(ParseFile(Path::ToUtf8(EmptyInput)).Succeeded(),
	    "an empty source file must remain valid");
	const auto ChunkedInput = OutputRoot / "chunked.h";
	{
		std::ofstream ChunkedFile(ChunkedInput, std::ios::binary);
		ChunkedFile << "/*" << std::string(20000, 'x') << "*/ int AfterChunks();";
	}
	const auto Chunked = ParseFile(Path::ToUtf8(ChunkedInput));
	Ok &= Expect(Chunked.Succeeded() && Chunked.Unit.Functions.size() == 1 && Chunked.Unit.Functions.front().Name == "AfterChunks",
	    "source reads must retain complete contents across multiple chunks");
	const std::filesystem::path FirstOutput = OutputRoot / "first.txt";
	const std::filesystem::path SecondOutput = OutputRoot / "second.txt";
	{
		std::ofstream Existing(FirstOutput, std::ios::binary);
		Existing << "old";
	}
	std::string OutputError;
	Ok &= Expect(WriteOutputFilesAtomically({{Path::ToUtf8(FirstOutput), "new-first"},
	                                            {Path::ToUtf8(SecondOutput), "new-second"}},
	                 OutputError),
	    "transactional generated-file write should commit every output");
	const auto ReadFile = [](const std::filesystem::path& Path)
	{
		std::ifstream Input(Path, std::ios::binary);
		std::ostringstream Contents;
		Contents << Input.rdbuf();
		return Contents.str();
	};
	Ok &= Expect(ReadFile(FirstOutput) == "new-first" && ReadFile(SecondOutput) == "new-second",
	    "transactional output contents were not committed correctly");
	FFilePathSet IndexedPaths;
	const FOutputFileNameLess NameLess;
	Ok &= Expect(!NameLess("Shared", "shared") && !NameLess("shared", "Shared") && NameLess("a", "B"), "generated-name ordering preserves ASCII case collision groups");
	#if defined(_WIN32)
	Ok &= Expect(!NameLess("\xC3\x89", "\xC3\xA9") && !NameLess("\xC3\xA9", "\xC3\x89"),
	    "generated-name ordering recognizes Windows ordinal Unicode case collisions");
	#endif
	Ok &= Expect(IndexedPaths.Insert(Path::ToUtf8(FirstOutput)) && IndexedPaths.Insert(Path::ToUtf8(SecondOutput)) &&
	        !IndexedPaths.Insert(Path::ToUtf8(OutputRoot / "." / "first.txt")),
	    "indexed path checks preserve existing-file and normalized-path identities");
	Ok &= Expect(IndexedPaths.Insert(Path::ToUtf8(OutputRoot / "missing.txt")) &&
	        IndexedPaths.Contains(Path::ToUtf8(OutputRoot / "child" / ".." / "missing.txt")),
	    "nonexistent output paths use normalized identities");
	const auto HardLink = OutputRoot / "first-alias.txt";
	std::filesystem::create_hard_link(FirstOutput, HardLink, FileError);
	Ok &= Expect(!FileError && IndexedPaths.Contains(Path::ToUtf8(HardLink)) && !IndexedPaths.Insert(Path::ToUtf8(HardLink)), "indexed path checks recognize hard-link aliases");
	Ok &= Expect(!WriteOutputFilesAtomically({{Path::ToUtf8(FirstOutput), "must-not-write"},
	                                             {Path::ToUtf8(HardLink), "must-not-write"}},
	                 OutputError) &&
	        ReadFile(FirstOutput) == "new-first",
	    "hard-linked output collisions are rejected before staging or replacing files");
	#if defined(_WIN32)
	Ok &= Expect(IndexedPaths.Contains(Path::ToUtf8(OutputRoot / "MISSING.TXT")),
	    "Windows nonexistent path identities retain ordinal case-insensitive matching");
	#endif
	const auto SourceInput = OutputRoot / "input.h";
	const auto SourceAlias = OutputRoot / "input-alias.h";
	{
		std::ofstream SourceFile(SourceInput);
		SourceFile << "int Declared();";
	}
	std::string NulInputPath = Path::ToUtf8(SourceInput);
	NulInputPath.push_back('\0');
	NulInputPath += "ignored-suffix";
	const auto NulInput = ParseFile(NulInputPath);
	Ok &= Expect(!NulInput.Succeeded() && NulInput.Unit.Functions.empty() && NulInput.Diagnostics.size() == 1 && NulInput.Diagnostics.front().Code == "cpp.io.read_failed" && NulInput.Diagnostics.front().Location.File == NulInputPath,
	    "NUL-containing input paths must fail instead of parsing a different prefix file");
	Ok &= Expect(!ParseFiles({NulInputPath}).Succeeded(),
	    "batch parsing must reject NUL-containing input paths");
	std::filesystem::create_hard_link(SourceInput, SourceAlias, FileError);
	const auto DuplicateInput = ParseFiles({Path::ToUtf8(SourceInput), Path::ToUtf8(SourceAlias)});
	Ok &= Expect(!FileError && !DuplicateInput.Succeeded() &&
	        std::any_of(DuplicateInput.Diagnostics.begin(), DuplicateInput.Diagnostics.end(),
	            [](const auto& Diagnostic)
	{
		return Diagnostic.Code == "cpp.merge.duplicate_input_file";
	}),
	    "batch parsing rejects physical aliases of the same input");
	const std::filesystem::path Blocker = OutputRoot / "blocker";
	{
		std::ofstream BlockerFile(Blocker, std::ios::binary);
		BlockerFile << "not-a-directory";
	}
	Ok &= Expect(!WriteOutputFilesAtomically({{Path::ToUtf8(FirstOutput), "must-not-commit"},
	                                             {Path::ToUtf8(Blocker / "child.txt"), "unwritable"}},
	                 OutputError) &&
	        ReadFile(FirstOutput) == "new-first",
	    "a staging failure must leave every existing output unchanged");
	std::string NulPath = Path::ToUtf8(FirstOutput);
	NulPath.push_back('\0');
	NulPath += "ignored-suffix";
	const auto UnstagedDirectory = OutputRoot / "no-staging";
	const bool NulWritten = WriteOutputFilesAtomically({{Path::ToUtf8(UnstagedDirectory / "new.txt"), "must-not-stage"},
	                                                       {NulPath, "must-not-truncate-original"}},
	    OutputError);
	Ok &= Expect(!NulWritten && !OutputError.empty(), "NUL-containing output paths must return a failure diagnostic");
	Ok &= Expect(ReadFile(FirstOutput) == "new-first", "NUL-containing paths must not truncate the original file");
	Ok &= Expect(!std::filesystem::exists(UnstagedDirectory), "invalid output paths must reject the whole batch before staging");
	#if defined(_WIN32)
	std::size_t AliasIndex = 0;
	for (const char* Suffix : {".", " ", ". "})
	{
		const auto NewOutput = OutputRoot / ("new-alias-" + std::to_string(AliasIndex++) + ".txt");
		Ok &= Expect(!WriteOutputFilesAtomically({{Path::ToUtf8(NewOutput), "first"},
		                                             {Path::ToUtf8(NewOutput) + Suffix, "second"}},
		                 OutputError) &&
		        !OutputError.empty() && !std::filesystem::exists(NewOutput),
		    "Windows normalized output aliases must reject before any new output is written");
	}
	#endif
	std::filesystem::remove_all(OutputRoot, FileError);

	return Ok ? 0 : 1;
}
