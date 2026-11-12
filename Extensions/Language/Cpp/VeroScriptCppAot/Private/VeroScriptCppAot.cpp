// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNumericBindings.h"
#include "VeroScriptCppAot.h"
#include "VeroScriptConsoleSource.h"
#include "VeroScriptFloatParsingSource.h"

#include "VeroScriptAst.h"
#include "VeroScriptAstTraversal.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptConstantEvaluation.h"
#include "VeroScriptLexer.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptParser.h"
#include "VeroScriptPath.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptStandardLibrary.h"
#include "VeroScriptVariant.h"
#include "VeroScriptStandardLibraryServices.h"
#include "VeroScriptVersion.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace VeroScript
{
	namespace
	{
		constexpr const char* kScriptMainName = "vs_script_main";

		bool HasIncludeDirective(
		    const std::string_view Source,
		    const std::string_view Header)
		{
			std::size_t LineStart = 0;
			bool bInBlockComment = false;
			while (LineStart <= Source.size())
			{
				const std::size_t LineEnd = Source.find('\n', LineStart);
				const std::string_view RawLine = Source.substr(
				    LineStart,
				    LineEnd == std::string_view::npos
				        ? Source.size() - LineStart
				        : LineEnd - LineStart);
				std::string Filtered;
				Filtered.reserve(RawLine.size());
				for (std::size_t Index = 0; Index < RawLine.size(); ++Index)
				{
					if (bInBlockComment)
					{
						if (Index + 1 < RawLine.size() && RawLine[Index] == '*' && RawLine[Index + 1] == '/')
						{
							bInBlockComment = false;
							++Index;
							Filtered.push_back(' ');
						}
						continue;
					}
					if (Index + 1 < RawLine.size() && RawLine[Index] == '/' && RawLine[Index + 1] == '/')
					{
						break;
					}
					if (Index + 1 < RawLine.size() && RawLine[Index] == '/' && RawLine[Index + 1] == '*')
					{
						bInBlockComment = true;
						++Index;
						Filtered.push_back(' ');
						continue;
					}
					Filtered.push_back(RawLine[Index]);
				}
				std::string_view Line(Filtered);
				while (!Line.empty() && (Line.front() == ' ' || Line.front() == '\t'))
				{
					Line.remove_prefix(1);
				}
				if (!Line.empty() && Line.back() == '\r')
				{
					Line.remove_suffix(1);
				}
				if (!Line.empty() && Line.front() == '#')
				{
					Line.remove_prefix(1);
					while (!Line.empty() && (Line.front() == ' ' || Line.front() == '\t'))
					{
						Line.remove_prefix(1);
					}
					constexpr std::string_view Include = "include";
					if (Line.rfind(Include, 0) == 0)
					{
						Line.remove_prefix(Include.size());
						while (!Line.empty() && (Line.front() == ' ' || Line.front() == '\t'))
						{
							Line.remove_prefix(1);
						}
						if (Line.rfind(Header, 0) == 0)
						{
							Line.remove_prefix(Header.size());
							while (!Line.empty() && (Line.front() == ' ' || Line.front() == '\t'))
							{
								Line.remove_prefix(1);
							}
							if (Line.empty())
							{
								return true;
							}
						}
					}
				}
				if (LineEnd == std::string_view::npos)
				{
					break;
				}
				LineStart = LineEnd + 1;
			}
			return false;
		}

		bool IsFormatFamilyIntrinsicCall(const FCallExpr& Call)
		{
			// Keep this precedence identical to the bytecode compiler:
			// multi-argument print and the dedicated formatting names are
			// intrinsics, while single-argument print may still be shadowed by a
			// local function value like other ordinary builtins.
			return (Call.Callee == "print" && Call.Arguments.size() > 1) || Call.Callee == "printf" || Call.Callee == "format" || Call.Callee == "__format" || Call.Callee == "formatf" || Call.Callee == "__format_printf";
		}

		template <typename TValue>
		class TValueScopeRestore
		{
		public:
			explicit TValueScopeRestore(TValue& InValue)
			    : Value(InValue), Saved(InValue)
			{
			}

			~TValueScopeRestore()
			{
				Value = std::move(Saved);
			}

			TValueScopeRestore(const TValueScopeRestore&) = delete;
			TValueScopeRestore& operator=(const TValueScopeRestore&) = delete;

		private:
			TValue& Value;
			TValue Saved;
		};

		struct FUserTypeInfo
		{
			std::string Name;
			std::string ModulePath;
			bool bExported = false;
			bool bFinal = false;
			bool bIsAbstract = false;
			bool bHasPureVirtualDestructor = false;
			const std::vector<FFieldDecl>* Fields = nullptr;
			const std::vector<FMethodDecl>* Methods = nullptr;
			const std::vector<FConstructorDecl>* Constructors = nullptr;
			const FDestructorDecl* Destructor = nullptr;
			bool bIsClass = false;
			bool bIsInterface = false;
			// Class: BaseTypeNames; struct: BaseTypeName (zero or one); interface: BaseInterfaceNames.
			std::vector<std::string> BaseTypeNames;
			std::vector<std::string> InterfaceTypeNames;
			// Open-generic source type params (empty for concrete / monomorph).
			std::vector<std::string> TypeParams;
			// Concrete type args when this entry is a monomorph of an open generic.
			std::vector<std::string> MonoTypeArgs;
			// Open generic simple name (e.g. "Box") when this is a monomorph.
			std::string OpenGenericName;
			// Semantic-only specialization containing an enclosing template parameter
			// (for example Base<T> while emitting Derived<T>). Open C++ template
			// helpers cover it, so no standalone helper definitions may be emitted.
			bool bResolutionOnlyMonomorph = false;
		};

		struct FSpecialization
		{
			const FFunctionDecl* Generic = nullptr;
			std::vector<std::string> TypeArgs;
			std::string CppName;
			std::string ReturnType;
			std::vector<std::string> ParamTypes;
		};

		struct FOpenGenericType
		{
			std::string Name;
			std::string ModulePath;
			bool bExported = false;
			bool bFinal = false;
			bool bIsAbstract = false;
			bool bHasPureVirtualDestructor = false;
			bool bIsClass = false;
			bool bIsInterface = false;
			std::vector<std::string> TypeParams;
			std::vector<std::string> TypeParamDefaults;
			std::vector<std::string> BaseTypeNames;
			std::vector<std::string> InterfaceTypeNames;
			const std::vector<FFieldDecl>* Fields = nullptr;
			const std::vector<FMethodDecl>* Methods = nullptr;
			const std::vector<FConstructorDecl>* Constructors = nullptr;
			const FDestructorDecl* Destructor = nullptr;
			std::vector<FTypeParamBound> Bounds;
		};

		struct FCppEmitter
		{
			std::ostringstream Out;
			mutable std::string Error;
			mutable bool bFailed = false;
			mutable bool bNeedsVector = false;
			mutable bool bNeedsArray = false;
			mutable bool bNeedsAssert = false;
			mutable bool bNeedsMap = false;
			mutable bool bNeedsDeque = false;
			mutable bool bNeedsSet = false;
			mutable bool bNeedsQueue = false;
			mutable bool bNeedsStack = false;
			mutable bool bNeedsMemory = false;
			mutable bool bNeedsObjectBase = false;
			bool bNeedsConstructionPhase = false;
			mutable bool bNeedsFunctional = false;
			bool bNeedsLocalStatics = false;
			mutable bool bNeedsHandle = false;
			mutable bool bNeedsEvents = false;
			bool bHasStandardAttribute = false;
			mutable bool bNeedsChannel = false;
			mutable bool bNeedsThread = false;
			mutable bool bNeedsChrono = false;
			mutable bool bNeedsConsole = false;
			mutable bool bNeedsCstdlib = false;
			mutable bool bNeedsUtility = false;
			mutable bool bNeedsCmath = false;
			mutable bool bNeedsFstream = false;
			mutable bool bNeedsFilesystem = false;
			mutable bool bNeedsCctype = false;
			mutable bool bNeedsTypeTraits = false;
			std::uint32_t NextFinallyId = 0;
			std::uint32_t NextLocalInitializerId = 0;
			std::string EntryModulePath;
			std::string ActiveModulePath;
			std::string ActiveNamespace;
			std::string ActiveFunctionName;
			// Top-level script variables are per-VM state. AOT workers execute on
			// separate native threads, so their corresponding C++ storage must be
			// thread-local rather than shared process globals.
			bool bEmittingTopLevelVariable = false;
			std::string ActiveReturnType;
			std::vector<std::string> ActiveCatchRefRebinds;
			const FProgram* Program = nullptr;
			std::vector<FMethodBindingCodegenInfo> MethodBindings;
			const IHostBindingCatalog* HostBindings = nullptr;

			std::unordered_map<std::string, std::string> LocalTypes;
			std::unordered_map<std::string, bool> LocalConstness;
			std::unordered_map<std::string, FUserTypeInfo> UserTypes;
			std::unordered_map<std::string, FOpenGenericType> OpenGenericTypes;
			std::unordered_map<std::string, std::string> TypeSubst;
			// `using`/`typedef` aliases (program + local): alias -> unresolved target.
			std::unordered_map<std::string, std::string> TypeAliases;
			// Immutable root declaration aliases. A leading `::Alias` must bypass a
			// same-spelled local alias temporarily installed in TypeAliases.
			std::unordered_map<std::string, std::string> RootTypeAliases;
			std::unordered_set<std::string> EnumTypeNames;
			// Scoped (`enum class`) type names — members resolve via Enum.Member / Enum::Member.
			std::unordered_set<std::string> ScopedEnumTypeNames;
			std::unordered_map<std::string, std::string> EnumUnderlyingTypeNames;
			// "Color.Red" -> numeric value for emit/switch.
			std::unordered_map<std::string, std::int64_t> EnumMemberValues;
			// Unscoped enum member -> C++ constant name when unique across the program.
			std::unordered_map<std::string, std::string> EnumBareMemberCppNames;
			std::unordered_map<std::string, std::string> EnumBareMemberTypeNames;
			std::string SelfTypeName;
			std::string SelfReceiverName = "self";
			std::string CapturedThisName = "vs_this";
			std::unordered_set<std::string> GeneratedLocalConflicts;
			mutable std::unordered_map<std::string, std::string> GeneratedLocalNameCache;
			bool bInSelfContext = false;
			bool bSelfIsConst = false;
			// Method/ctor body emitted as C++ member (OOP / interface layout).
			bool bMemberMethodBody = false;
			// A shared_ptr cannot be reacquired from enable_shared_from_this after the
			// last strong owner has entered destruction.  Lambdas that capture a class
			// receiver therefore require a distinct destructor-safe lowering.
			bool bInDestructorBody = false;
			// Constructor initializer helpers are free functions whose `self` is a
			// direct object reference.  Keep this separate from member bodies, where
			// the corresponding expression is `*this`.
			bool bCtorInitDirectSelf = false;
			// Class-this lambdas own their receiver through `vs_this`. Nested lambdas
			// capture that handle rather than the C++ closure object's `this`.
			std::size_t CapturedClassThisLambdaDepth = 0;
			// Struct-this lambdas capture a value snapshot, matching VM CloneStruct
			// semantics.  This is distinct from the managed class receiver above.
			std::size_t CapturedStructThisLambdaDepth = 0;
			// Class locals proven not to escape their lexical block may use direct
			// automatic storage.  The declaration and every receiver emission consult
			// this set; an inconclusive proof never inserts the local.
			std::unordered_set<std::string> StackClassLocals;
			// Active only while emitting the declaration selected by the escape proof.
			// Keeping this separate from StackClassLocals preserves an outer stack
			// object's receiver semantics inside a shadowing initializer.
			std::string ActiveStackClassDeclaration;
			// Entry `main(string[] args)` parameter name when present.
			std::string MainArgsParamName;
			bool bMainReturnsInt = false;
			bool bEmitImplicitMainReturnZero = false;
			// Struct?interface boxing helpers to emit (`StructName\nIfaceName`).
			std::unordered_set<std::string> PendingStructBoxes;

			std::vector<const FFunctionDecl*> ConcreteFunctions;
			std::vector<const FFunctionDecl*> GenericFunctions;
			std::vector<FSpecialization> Specializations;
			std::unordered_set<std::string> SpecializationKeys;
			std::unordered_set<const FFunctionDecl*> EmittedGenericTemplates;
			std::unordered_set<const FFunctionDecl*> EmittedGenericTemplateBodies;
			std::vector<std::string> TypeEmitOrder;
			std::unordered_set<std::string> TypeMonoKeys;
			// Populated by the mandatory bytecode compile check so AOT lowering uses
			// exactly the compiler's effective (including inherited) abstractness.
			std::unordered_set<std::string> AbstractTypeNames;
			// While emitting an open `template <typename T>`, MapType leaves T as T.
			mutable std::unordered_set<std::string> ActiveOpenTypeParams;
			// Active `where` bounds while emitting an open template (for method-on-T).
			std::vector<FTypeParamBound> ActiveOpenBounds;

			#include "Internal/EmitterAttributes.inl"
			#include "Internal/EmitterTypeSystem.inl"
			#include "Internal/EmitterContainers.inl"
			#include "Internal/EmitterTypeResolution.inl"
			#include "Internal/EmitterSpecializations.inl"
			#include "Internal/EmitterCalls.inl"
			#include "Internal/EmitterEscapeAnalysis.inl"
			#include "Internal/EmitterLambdaCaptures.inl"
			#include "Internal/EmitterEvaluation.inl"
			#include "Internal/EmitterReferenceOwners.inl"
			#include "Internal/EmitterExpressions.inl"
			#include "Internal/EmitterStatements.inl"
			#include "Internal/EmitterMethods.inl"
			#include "Internal/EmitterProgram.inl"
		};

		bool IsValidAotMain(const FFunctionDecl& Function)
		{
			// The mandatory bytecode CompileCheck validates effective return/parameter
			// types, including user aliases. Eligibility only validates the AOT shape.
			if (Function.Name != "main" || !Function.TypeParams.empty())
			{
				return false;
			}
			if (Function.Parameters.empty())
			{
				return true;
			}
			if (Function.Parameters.size() == 1 && !Function.Parameters[0].bIsRef && !Function.Parameters[0].bIsVariadic && !Function.Parameters[0].DefaultValue)
			{
				return true;
			}
			return false;
		}

		bool ValidateAotEligibility(const FProgram& Program, std::string& OutError)
		{
			if (RequiresCoreTemplateExport(Program))
			{
				OutError = "AOT: extended template applications require the Core AOT exporter";
				return false;
			}
			if (HasScopedTypeDeclarations(Program))
			{
				OutError = "AOT: nested and local types require the VeroScript runtime; use the Core AOT exporter";
				return false;
			}
			// These declarations are valid in the execution core, but the source
			// emitter cannot yet preserve their binding/result semantics.
			const auto UnsupportedOperator = [](const auto& Decl, const bool bMember)
			{
				if (Decl.bDeleted) return false;
				const std::size_t Start = Decl.Name.find_last_of(":.");
				const std::string_view Name = std::string_view(Decl.Name).substr(Start == std::string::npos ? 0 : Start + 1);
				if (Name.rfind("operator", 0) != 0 || Name.size() <= 8 || std::string_view(" +-*/%&|^!~<>=([,").find(Name[8]) == std::string_view::npos)
				{
					return false;
				}
				return Name == "operator=" || Runtime::IsReferenceTypeName(Decl.ReturnType) || ((Name == "operator++" || Name == "operator--") && Decl.Parameters.size() == (bMember ? 1u : 2u)) || std::any_of(Decl.Parameters.begin(), Decl.Parameters.end(), [](const FFunctionParam& Param)
				{
					return Param.bIsRef || Param.bIsVariadic || Param.DefaultValue;
				});
			};
			const auto UnsupportedType = [&](const auto& Type)
			{
				return std::any_of(Type.Methods.begin(), Type.Methods.end(),
				    [&](const FMethodDecl& Method)
				{
					return UnsupportedOperator(Method, true);
				});
			};
			if (std::any_of(Program.Functions.begin(), Program.Functions.end(),
			        [&](const FFunctionDecl& Function)
			{
				return UnsupportedOperator(Function, false);
			}) ||
			    std::any_of(Program.Classes.begin(), Program.Classes.end(), UnsupportedType) || std::any_of(Program.Structs.begin(), Program.Structs.end(), UnsupportedType) || std::any_of(Program.Interfaces.begin(), Program.Interfaces.end(), UnsupportedType))
			{
				OutError = "AOT: reference operators, operator=, postfix int tags and operator() defaults/rest "
				           "are not supported by C++ source emission; use the VM, mixed or JIT backend";
				return false;
			}
			// Interfaces / inheritance / virtual / open generics emit as C++ templates.
			// Soft event builtins stay glue-only.
			const FFunctionDecl* EntryMain = nullptr;
			for (const FFunctionDecl& Function : Program.Functions)
			{
				if (Function.Name != "main")
				{
					continue;
				}
				if (!IsValidAotMain(Function))
				{
					OutError =
					    "AOT: main must return void or int and optionally take string[] args";
					return false;
				}
				const bool bEntry = Program.EntryModulePath.empty() || Function.ModulePath == Program.EntryModulePath;
				if (bEntry)
				{
					if (EntryMain)
					{
						OutError = "AOT: multiple entry main() declarations";
						return false;
					}
					EntryMain = &Function;
				}
				else
				{
					OutError =
					    "AOT: imported main() is not supported; rename the imported function";
					return false;
				}
			}
			if (!EntryMain)
			{
				OutError = "AOT: requires void/int main() or main(string[] args) in the entry module";
				return false;
			}
			return true;
		}

		bool CompileCheck(
		    const FProgram& Program,
		    std::string& OutError,
		    const IHostBindingCatalog* const Bindings,
		    std::unordered_set<std::string>& OutAbstractTypeNames, std::vector<FMethodBindingCodegenInfo>& OutBindings, std::unique_ptr<FBytecodeModule>& OutModule)
		{
			OutAbstractTypeNames.clear();
			VeroScriptCompiler Compiler;
			Compiler.SetCollectRuntimeMemoryTypes(true);
			std::unique_ptr<FBytecodeModule> Module =
			    Compiler.Compile(
			        Program,
			        Bindings ? &Bindings->GetNativeFunctions() : nullptr,
			        Bindings);
			if (!Module || Compiler.HasError())
			{
				OutError = Compiler.HasError()
				    ? Compiler.GetError().Message
				    : "AOT: compile check failed";
				return false;
			}
			if (Numerics::UsesRuntimeNumerics(*Module, Bindings ? &Bindings->GetNativeFunctions() : nullptr))
			{
				OutError = "AOT: bigint/rational require the VeroScript numeric runtime; standalone output is unsupported";
				return false;
			}
			if (Variant::UsesRuntime(*Module, Bindings ? &Bindings->GetNativeFunctions() : nullptr))
			{
				OutError = "diag.aot_variant_unsupported: variant requires the VeroScript runtime; standalone output is unsupported";
				return false;
			}
			if (StandardLibrary::UsesRuntimeFoundation(*Module))
			{
				OutError = "AOT: standard-library foundation operations require the VeroScript runtime; standalone output is unsupported";
				return false;
			}
			if (Compiler.UsesRuntimeMemoryTypes() ||
			    Memory::UsesRuntimeMemory(*Module, Bindings ? &Bindings->GetNativeFunctions() : nullptr))
			{
				OutError =
				    "AOT: std::memory requires the VeroScript runtime; standalone output is unsupported";
				return false;
			}

			OutBindings = Compiler.GetMethodBindingCodegenInfo();
			for (const FBytecodeTypeDesc& Type : Module->Types)
			{
				if (Type.bIsAbstract)
				{
					OutAbstractTypeNames.insert(Type.Name);
					const std::string::size_type Separator = Type.Name.rfind('\x1F');
					if (Separator != std::string::npos && Separator + 1 < Type.Name.size())
					{
						// AOT deliberately rejects duplicate source type names across its
						// flattened translation unit, so retaining the source leaf is
						// unambiguous and bridges compiler module linkage names.
						OutAbstractTypeNames.insert(Type.Name.substr(Separator + 1));
					}
				}
			}
			OutModule = std::move(Module);
			return true;
		}

		struct FEmitterRuntimeFlags
		{
			bool bNeedsVector = false;
			bool bNeedsArray = false;
			bool bNeedsAssert = false;
			bool bNeedsMap = false;
			bool bNeedsDeque = false;
			bool bNeedsSet = false;
			bool bNeedsQueue = false;
			bool bNeedsStack = false;
			bool bNeedsMemory = false;
			bool bNeedsObjectBase = false;
			bool bNeedsConstructionPhase = false;
			bool bNeedsFunctional = false;
			bool bNeedsLocalStatics = false;
			bool bNeedsHandle = false;
			bool bNeedsEvents = false;
			bool bNeedsAttribute = false;
			bool bNeedsChannel = false;
			bool bNeedsThread = false;
			bool bNeedsUtility = false;
			bool bNeedsChrono = false;
			bool bNeedsConsole = false;
			bool bNeedsCstdlib = false;
			bool bNeedsFstream = false;
			bool bNeedsFilesystem = false;
			bool bNeedsCctype = false;
		};

		std::string MakeRuntimeSupport(const FEmitterRuntimeFlags& Flags)
		{
			std::ostringstream Support;
			Support << VsFloatParsingSource << "\n";
			if (Flags.bNeedsConsole)
			{
				Support << VsConsoleSource << "\n";
			}

			Support
			    << "struct VsExitException {\n"
			    << "\tint Code = 0;\n"
			    << "\texplicit VsExitException(int InCode) : Code(InCode) {}\n"
			    << "};\n\n"
			    << "[[noreturn]] static inline void VsScriptFail(const char* Msg) {\n"
			    << "\tstd::cerr << Msg << '\\n';\n"
			    << "\tthrow VsExitException(1);\n"
			    << "}\n\n"
			    << "static inline std::int64_t VsCheckedLength(std::uint64_t Length) {\n"
			    << "\tif (Length > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) {\n"
			    << "\t\tVsScriptFail(\"len result exceeds int64 range\");\n"
			    << "\t}\n"
			    << "\treturn static_cast<std::int64_t>(Length);\n"
			    << "}\n\n";
			Support << "template <typename T> static inline std::size_t VsCheckedNativeSize(T Value) {\n"
			        << "\tstatic_assert(std::is_integral_v<T>, \"size must be an integer\");\n"
			        << "\tif constexpr (std::is_same_v<T, bool>) VsScriptFail(\"size or index must be an integer\");\n"
			        << "\tif constexpr (std::is_signed_v<T>) { if (Value < 0) VsScriptFail(\"size or index cannot be negative\"); }\n"
			        << "\tconst std::uint64_t Size = static_cast<std::uint64_t>(Value);\n"
			        << "\tif (Size > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()) || Size > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)())) VsScriptFail(\"size or index exceeds platform range\");\n"
			        << "\treturn static_cast<std::size_t>(Size);\n"
			        << "}\n\n";
			if (Flags.bNeedsConstructionPhase)
			{
				Support
				    << "template <typename T> static inline const void* VsConstructionAddress(T& Value)\n"
				    << "{\n"
				    << "\tif constexpr (std::is_polymorphic_v<T>)\n"
				    << "\t{\n\t\treturn dynamic_cast<const void*>(&Value);\n\t}\n"
				    << "\telse\n\t{\n\t\treturn static_cast<const void*>(&Value);\n\t}\n"
				    << "}\n"
				    << "struct VsConstructionValueTag\n{\n};\n"
				    << "struct VsConstructionScope\n"
				    << "{\n"
				    << "\tVsConstructionScope(const void* Receiver, const std::type_info* const* Hierarchy, std::size_t Size)\n"
				    << "\t\t: Object(Receiver), Types(Hierarchy), Count(Size), Previous(Current)\n"
				    << "\t{\n\t\tCurrent = this;\n\t}\n"
				    << "\t~VsConstructionScope()\n\t{\n\t\tCurrent = Previous;\n\t}\n"
				    << "\tbool Contains(const std::type_info& Type) const\n\t{\n"
				    << "\t\tfor (std::size_t I = 0; I < Count; ++I)\n\t\t{\n"
				    << "\t\t\tif (*Types[I] == Type) return true;\n\t\t}\n"
				    << "\t\treturn false;\n\t}\n"
				    << "\tstatic const VsConstructionScope* Find(const void* Receiver)\n\t{\n"
				    << "\t\tfor (auto Scope = Current; Scope; Scope = Scope->Previous)\n\t\t{\n"
				    << "\t\t\tif (Scope->Object == Receiver) return Scope;\n\t\t}\n"
				    << "\t\treturn nullptr;\n\t}\n\n"
				    << "\tinline static thread_local const VsConstructionScope* Current = nullptr;\n"
				    << "\tconst void* Object;\n"
				    << "\tconst std::type_info* const* Types;\n"
				    << "\tstd::size_t Count;\n"
				    << "\tconst VsConstructionScope* Previous;\n"
				    << "};\n"
				    << "struct VsConstructionState\n"
				    << "{\n"
				    << "\tVsConstructionState() = default;\n"
				    << "\tVsConstructionState(const VsConstructionState&) noexcept\n"
				    << "\t\t: Complete(true)\n\t{\n\t}\n"
				    << "\tVsConstructionState(VsConstructionState&& Other) noexcept\n"
				    << "\t\t: Complete(std::exchange(Other.Complete, false))\n\t{\n\t}\n"
				    << "\tVsConstructionState& operator=(const VsConstructionState&) noexcept\n"
				    << "\t{\n\t\treturn *this;\n\t}\n\n"
				    << "\tbool Complete = false;\n"
				    << "};\n"
				    << "struct VsConstructionCompletion\n"
				    << "{\n"
				    << "\texplicit VsConstructionCompletion(bool& Value)\n"
				    << "\t\t: Complete(Value), Exceptions(std::uncaught_exceptions())\n"
				    << "\t{\n\t\tComplete = false;\n\t}\n"
				    << "\t~VsConstructionCompletion()\n\t{\n"
				    << "\t\tif (std::uncaught_exceptions() == Exceptions)\n\t\t{\n\t\t\tComplete = true;\n\t\t}\n\t}\n\n"
				    << "\tbool& Complete;\n"
				    << "\tint Exceptions;\n"
				    << "};\n"
				    << "template <typename T, typename FInitializer>\n"
				    << "static inline void VsInitializeConstructedField(T& Storage, FInitializer&& Initialize)\n"
				    << "{\n\tstd::destroy_at(std::addressof(Storage));\n"
				    << "\ttry\n\t{\n\t\tInitialize(static_cast<void*>(std::addressof(Storage)));\n\t}\n"
				    << "\tcatch (...)\n\t{\n\t\t::new (static_cast<void*>(std::addressof(Storage))) T{};\n\t\tthrow;\n\t}\n"
				    << "}\n\n";
			}

			if (Flags.bNeedsCstdlib)
			{
				Support
				    << "static inline std::mutex& VsEnvironmentMutex() {\n"
				    << "\tstatic std::mutex Mutex;\n"
				    << "\treturn Mutex;\n"
				    << "}\n\n";
			}
			if (Flags.bNeedsMemory)
			{
				Support
				    << "template <typename T>\n"
				    << "static inline T& VsRequireObject(const std::shared_ptr<T>& Value) {\n"
				    << "\tif (!Value) VsScriptFail(\"null object access\");\n"
				    << "\treturn *Value;\n"
				    << "}\n\n";
			}
			if (Flags.bNeedsObjectBase)
			{
				Support
				    << "struct VsObjectBase : std::enable_shared_from_this<VsObjectBase> {\n"
				    << "\tvirtual ~VsObjectBase() = default;\n"
				    << "};\n"
				    << "template <typename T>\n"
				    << "static inline std::shared_ptr<T> VsRetainObject(T* Value) {\n"
				    << "\tif (!Value) VsScriptFail(\"null object access\");\n"
				    << "\tauto Lifetime = Value->VsObjectBase::weak_from_this().lock();\n"
				    << "\tif (!Lifetime) VsScriptFail(\"object is not owned by the script runtime\");\n"
				    << "\treturn std::shared_ptr<T>(std::move(Lifetime), Value);\n"
				    << "}\n\n";
			}
			if (Flags.bNeedsVector)
			{
				Support
				    // std::vector<bool> cannot supply the real bool references required
				    // by script indexing and ref arguments. Keep each bool in a value slot.
				    << "struct VsArrayBoolSlot {\n"
				    << "\tbool Value;\n"
				    << "\tVsArrayBoolSlot(bool value = false) : Value(value) {}\n"
				    << "\toperator bool&() { return Value; }\n"
				    << "\toperator const bool&() const { return Value; }\n"
				    << "};\n"
				    << "template <typename T>\n"
				    << "class vs_Array {\n"
				    << "public:\n"
				    << "\tusing storage_type = std::vector<std::conditional_t<std::is_same_v<T, bool>, VsArrayBoolSlot, T>>;\n"
				    << "\tusing iterator = typename storage_type::iterator;\n"
				    << "\tusing const_iterator = typename storage_type::const_iterator;\n"
				    << "\tvs_Array() : data_(std::make_shared<storage_type>()) {}\n"
				    << "\tvs_Array(std::initializer_list<T> init)\n"
				    << "\t\t: data_(std::make_shared<storage_type>(init.begin(), init.end())) {}\n"
				    << "\texplicit vs_Array(storage_type values)\n"
				    << "\t\t: data_(std::make_shared<storage_type>(std::move(values))) {}\n"
				    << "\tT& operator[](std::size_t i) { if (i >= data_->size()) VsScriptFail(\"array index out of bounds\"); return (*data_)[i]; }\n"
				    << "\tconst T& operator[](std::size_t i) const { if (i >= data_->size()) VsScriptFail(\"array index out of bounds\"); return (*data_)[i]; }\n"
				    << "\tvoid push_back(T value) { data_->push_back(std::move(value)); }\n"
				    << "\tvoid reserve(std::int64_t capacity) { if (capacity < 0) VsScriptFail(\"reserve capacity cannot be negative\"); data_->reserve(VsCheckedNativeSize(capacity)); }\n"
				    << "\tstd::int64_t capacity() const { return VsCheckedLength(data_->capacity()); }\n"
				    << "\tT pop_value() { if (data_->empty()) VsScriptFail(\"pop from empty array\"); T value = std::move(data_->back()); data_->pop_back(); return value; }\n"
				    << "\tbool remove_value(const T& value) { auto it = std::find(data_->begin(), data_->end(), value); if (it == data_->end()) return false; data_->erase(it); return true; }\n"
				    << "\tvoid remove_at(std::size_t i) { if (i >= data_->size()) VsScriptFail(\"array index out of bounds\"); data_->erase(data_->begin() + static_cast<std::ptrdiff_t>(i)); }\n"
				    << "\tT& back() { if (data_->empty()) VsScriptFail(\"pop from empty array\"); return data_->back(); }\n"
				    << "\tconst T& back() const { if (data_->empty()) VsScriptFail(\"pop from empty array\"); return data_->back(); }\n"
				    << "\tvoid pop_back() { if (data_->empty()) VsScriptFail(\"pop from empty array\"); data_->pop_back(); }\n"
				    << "\tvoid clear() { data_->clear(); }\n"
				    << "\tbool empty() const { return data_->empty(); }\n"
				    << "\tstd::size_t size() const { return data_->size(); }\n"
				    << "\titerator begin() { return data_->begin(); }\n"
				    << "\titerator end() { return data_->end(); }\n"
				    << "\tconst_iterator begin() const { return data_->begin(); }\n"
				    << "\tconst_iterator end() const { return data_->end(); }\n"
				    << "\tfriend bool operator==(const vs_Array& a, const vs_Array& b) { return a.data_ == b.data_; }\n"
				    << "\tfriend bool operator!=(const vs_Array& a, const vs_Array& b) { return !(a == b); }\n"
				    << "private:\n"
				    << "\tstd::shared_ptr<storage_type> data_;\n"
				    << "};\n\n";
			}
			if (Flags.bNeedsVector)
			{
				Support << R"VS(
static inline std::uint32_t VsRandomNext(vs_Array<std::uint32_t> e) {
	if (e.empty()) e.push_back(0x6d2b79f5u);
	auto x=e[0]; if (!x) VsScriptFail("random engine state must be nonzero");
	x ^= x << 13; x ^= x >> 17; x ^= x << 5; e[0]=x; return x;
}
static inline std::int64_t VsRandomIndex(vs_Array<std::uint32_t> e, std::int64_t n) {
	if (n<=0) VsScriptFail("random bound must be positive");
	constexpr std::uint64_t b=4294967295ULL; const bool wide=n>INT32_MAX;
	const std::uint64_t range=wide?b*b:b, bound=static_cast<std::uint64_t>(n), limit=range-range%bound;
	for (;;) { std::uint64_t x=VsRandomNext(e)-std::uint64_t{1};
		if (wide) x=x*b+VsRandomNext(e)-std::uint64_t{1};
		if (x<limit) return static_cast<std::int64_t>(x%bound); }
}
template<class T> static inline void VsArrayReverse(vs_Array<T> a) { std::reverse(a.begin(),a.end()); }
template<class T> static inline void VsArrayRotate(vs_Array<T> a, std::int64_t m) {
	if (m<0 || static_cast<std::uint64_t>(m)>a.size()) VsScriptFail("rotate middle out of range");
	std::rotate(a.begin(),a.begin()+static_cast<std::ptrdiff_t>(m),a.end());
}
template<class T> static inline void VsArrayShuffle(vs_Array<T> a, vs_Array<std::uint32_t> e) {
	if constexpr (std::is_same_v<T,std::uint32_t>) if (a==e) VsScriptFail("shuffle target aliases engine");
	for (std::size_t n=a.size();n>1;--n) { auto j=VsRandomIndex(e,VsCheckedLength(n)); std::iter_swap(a.begin()+static_cast<std::ptrdiff_t>(n-1),a.begin()+static_cast<std::ptrdiff_t>(j)); }
}
)VS";
			}
			if (Flags.bNeedsMap)
			{
				Support
				    << "template <typename K, typename V>\n"
				    << "class vs_Map {\n"
				    << "public:\n"
				    << "\tusing storage_type = std::unordered_map<K, V>;\n"
				    << "\tusing iterator = typename storage_type::iterator;\n"
				    << "\tusing const_iterator = typename storage_type::const_iterator;\n"
				    << "\tvs_Map() : data_(std::make_shared<storage_type>()) {}\n"
				    << "\tvs_Map(std::initializer_list<typename storage_type::value_type> init)\n"
				    << "\t\t: data_(std::make_shared<storage_type>(init)) {}\n"
				    << "\tV& operator[](const K& key) { auto it = data_->find(key); if (it == data_->end()) VsScriptFail(\"map key not found\"); return it->second; }\n"
				    << "\tconst V& operator[](const K& key) const { auto it = data_->find(key); if (it == data_->end()) VsScriptFail(\"map key not found\"); return it->second; }\n"
				    << "\tV& get_or_insert(const K& key) { return (*data_)[key]; }\n"
				    << "\tvoid reserve(std::int64_t capacity) { if (capacity < 0) VsScriptFail(\"reserve capacity cannot be negative\"); data_->reserve(VsCheckedNativeSize(capacity)); }\n"
				    << "\tstd::size_t count(const K& key) const { return data_->count(key); }\n"
				    << "\tstd::size_t erase(const K& key) { return data_->erase(key); }\n"
				    << "\tvoid clear() { data_->clear(); }\n"
				    << "\tbool empty() const { return data_->empty(); }\n"
				    << "\tstd::size_t size() const { return data_->size(); }\n"
				    << "\titerator begin() { return data_->begin(); }\n"
				    << "\titerator end() { return data_->end(); }\n"
				    << "\tconst_iterator begin() const { return data_->begin(); }\n"
				    << "\tconst_iterator end() const { return data_->end(); }\n"
				    << "\tfriend bool operator==(const vs_Map& a, const vs_Map& b) { return a.data_ == b.data_; }\n"
				    << "\tfriend bool operator!=(const vs_Map& a, const vs_Map& b) { return !(a == b); }\n"
				    << "private:\n"
				    << "\tstd::shared_ptr<storage_type> data_;\n"
				    << "};\n\n"
				    << "template <typename K, typename V>\n"
				    << "class vs_OrderedMap {\n"
				    << "public:\n"
				    << "\tstruct Entry { K first{}; V second{}; };\n"
				    << "\tusing storage_type = std::vector<Entry>;\n"
				    << "\tusing iterator = typename storage_type::iterator;\n"
				    << "\tusing const_iterator = typename storage_type::const_iterator;\n"
				    << "\tvs_OrderedMap() : entries_(std::make_shared<storage_type>()) {}\n"
				    << "\tvs_OrderedMap(std::initializer_list<Entry> init) {\n"
				    << "\t\tentries_ = std::make_shared<storage_type>();\n"
				    << "\t\tfor (const auto& e : init) get_or_insert(e.first) = e.second;\n"
				    << "\t}\n"
				    << "\tV& operator[](const K& key) {\n"
				    << "\t\tauto it = find(key); if (it == end()) VsScriptFail(\"ordered_map key not found\"); return it->second;\n"
				    << "\t}\n"
				    << "\tconst V& operator[](const K& key) const {\n"
				    << "\t\tauto it = find(key); if (it == end()) VsScriptFail(\"ordered_map key not found\"); return it->second;\n"
				    << "\t}\n"
				    << "\tV& get_or_insert(const K& key) {\n"
				    << "\t\tauto it = find(key); if (it != end()) return it->second;\n"
				    << "\t\tentries_->push_back(Entry{key, V{}}); return entries_->back().second;\n"
				    << "\t}\n"
				    << "\tvoid reserve(std::int64_t capacity) { if (capacity < 0) VsScriptFail(\"reserve capacity cannot be negative\"); entries_->reserve(VsCheckedNativeSize(capacity)); }\n"
				    << "\titerator find(const K& key) {\n"
				    << "\t\treturn std::find_if(entries_->begin(), entries_->end(),\n"
				    << "\t\t\t[&](const Entry& e) { return e.first == key; });\n"
				    << "\t}\n"
				    << "\tconst_iterator find(const K& key) const {\n"
				    << "\t\treturn std::find_if(entries_->begin(), entries_->end(),\n"
				    << "\t\t\t[&](const Entry& e) { return e.first == key; });\n"
				    << "\t}\n"
				    << "\tstd::size_t count(const K& key) const { return find(key) == end() ? 0 : 1; }\n"
				    << "\tstd::size_t erase(const K& key) {\n"
				    << "\t\tauto it = find(key); if (it == end()) return 0;\n"
				    << "\t\tentries_->erase(it); return 1;\n"
				    << "\t}\n"
				    << "\tvoid clear() { entries_->clear(); }\n"
				    << "\tbool empty() const { return entries_->empty(); }\n"
				    << "\tstd::size_t size() const { return entries_->size(); }\n"
				    << "\titerator begin() { return entries_->begin(); }\n"
				    << "\titerator end() { return entries_->end(); }\n"
				    << "\tconst_iterator begin() const { return entries_->begin(); }\n"
				    << "\tconst_iterator end() const { return entries_->end(); }\n"
				    << "\tfriend bool operator==(const vs_OrderedMap& a, const vs_OrderedMap& b) { return a.entries_ == b.entries_; }\n"
				    << "\tfriend bool operator!=(const vs_OrderedMap& a, const vs_OrderedMap& b) { return !(a == b); }\n"
				    << "private:\n"
				    << "\tstd::shared_ptr<storage_type> entries_;\n"
				    << "};\n\n"
				    << "template <typename T>\n"
				    << "class vs_MultiSet {\n"
				    << "public:\n"
				    << "\tusing storage_type = std::unordered_map<T, std::uint64_t>;\n"
				    << "\tvs_MultiSet() : data_(std::make_shared<storage_type>()) {}\n"
				    << "\tvs_MultiSet(std::initializer_list<T> init) : vs_MultiSet() { for (const T& value : init) add(value); }\n"
				    << "\tvoid add(const T& value) { auto& count = (*data_)[value]; if (count == static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) VsScriptFail(\"multiset count exceeds int64 range\"); ++count; }\n"
				    << "\tvoid reserve(std::int64_t capacity) { if (capacity < 0) VsScriptFail(\"reserve capacity cannot be negative\"); data_->reserve(VsCheckedNativeSize(capacity)); }\n"
				    << "\tbool has(const T& value) const { return data_->find(value) != data_->end(); }\n"
				    << "\tstd::int64_t count(const T& value) const { auto it = data_->find(value); return it == data_->end() ? 0 : VsCheckedLength(it->second); }\n"
				    << "\tbool remove_one(const T& value) { auto it = data_->find(value); if (it == data_->end()) return false; if (--it->second == 0) data_->erase(it); return true; }\n"
				    << "\tvoid clear() { data_->clear(); }\n"
				    << "\tstd::int64_t size() const { std::uint64_t total = 0; for (const auto& entry : *data_) { if (entry.second > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()) - total) VsScriptFail(\"multiset size exceeds int64 range\"); total += entry.second; } return VsCheckedLength(total); }\n"
				    << "\tstd::vector<T> values() const { std::vector<T> out; out.reserve(VsCheckedNativeSize(size())); for (const auto& entry : *data_) for (std::uint64_t i = 0; i < entry.second; ++i) out.push_back(entry.first); return out; }\n"
				    << "\tfriend bool operator==(const vs_MultiSet& a, const vs_MultiSet& b) { return a.data_ == b.data_; }\n"
				    << "\tfriend bool operator!=(const vs_MultiSet& a, const vs_MultiSet& b) { return !(a == b); }\n"
				    << "private: std::shared_ptr<storage_type> data_;\n"
				    << "};\n\n"
				    << "template <typename T>\n"
				    << "class vs_OrderedMultiSet {\n"
				    << "public:\n"
				    << "\tstruct Entry { T value{}; std::uint64_t count = 0; };\n"
				    << "\tusing storage_type = std::vector<Entry>;\n"
				    << "\tvs_OrderedMultiSet() : entries_(std::make_shared<storage_type>()) {}\n"
				    << "\tvs_OrderedMultiSet(std::initializer_list<T> init) : vs_OrderedMultiSet() { for (const T& value : init) add(value); }\n"
				    << "\tvoid add(const T& value)\n\t{\n\t\tauto it = find(value);\n\t\tif (it == entries_->end()) entries_->push_back(Entry{value, 1});\n\t\telse\n\t\t{\n\t\t\tif (it->count == static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) VsScriptFail(\"multiset count exceeds int64 range\");\n\t\t\t++it->count;\n\t\t}\n\t}\n"
				    << "\tvoid reserve(std::int64_t capacity) { if (capacity < 0) VsScriptFail(\"reserve capacity cannot be negative\"); entries_->reserve(VsCheckedNativeSize(capacity)); }\n"
				    << "\tbool has(const T& value) const { return find(value) != entries_->end(); }\n"
				    << "\tstd::int64_t count(const T& value) const { auto it = find(value); return it == entries_->end() ? 0 : VsCheckedLength(it->count); }\n"
				    << "\tbool remove_one(const T& value) { auto it = find(value); if (it == entries_->end()) return false; if (--it->count == 0) entries_->erase(it); return true; }\n"
				    << "\tvoid clear() { entries_->clear(); }\n"
				    << "\tstd::int64_t size() const { std::uint64_t total = 0; for (const auto& entry : *entries_) { if (entry.count > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()) - total) VsScriptFail(\"multiset size exceeds int64 range\"); total += entry.count; } return VsCheckedLength(total); }\n"
				    << "\tstd::vector<T> values() const { std::vector<T> out; out.reserve(VsCheckedNativeSize(size())); for (const auto& entry : *entries_) for (std::uint64_t i = 0; i < entry.count; ++i) out.push_back(entry.value); return out; }\n"
				    << "\tfriend bool operator==(const vs_OrderedMultiSet& a, const vs_OrderedMultiSet& b) { return a.entries_ == b.entries_; }\n"
				    << "\tfriend bool operator!=(const vs_OrderedMultiSet& a, const vs_OrderedMultiSet& b) { return !(a == b); }\n"
				    << "private:\n"
				    << "\ttypename storage_type::iterator find(const T& value) { return std::find_if(entries_->begin(), entries_->end(), [&](const Entry& entry) { return entry.value == value; }); }\n"
				    << "\ttypename storage_type::const_iterator find(const T& value) const { return std::find_if(entries_->begin(), entries_->end(), [&](const Entry& entry) { return entry.value == value; }); }\n"
				    << "\tstd::shared_ptr<storage_type> entries_;\n"
				    << "};\n\n";
			}
			if (Flags.bNeedsSet)
			{
				Support
				    << "template <typename T>\n"
				    << "class vs_Set {\n"
				    << "public:\n"
				    << "\tusing storage_type = std::unordered_set<T>;\n"
				    << "\tusing iterator = typename storage_type::iterator;\n"
				    << "\tusing const_iterator = typename storage_type::const_iterator;\n"
				    << "\tvs_Set() : data_(std::make_shared<storage_type>()) {}\n"
				    << "\tvs_Set(std::initializer_list<T> init) : data_(std::make_shared<storage_type>(init)) {}\n"
				    << "\tauto insert(T value) { return data_->insert(std::move(value)); }\n"
				    << "\tvoid reserve(std::int64_t capacity) { if (capacity < 0) VsScriptFail(\"reserve capacity cannot be negative\"); data_->reserve(VsCheckedNativeSize(capacity)); }\n"
				    << "\tstd::size_t count(const T& value) const { return data_->count(value); }\n"
				    << "\tstd::size_t erase(const T& value) { return data_->erase(value); }\n"
				    << "\tvoid clear() { data_->clear(); }\n"
				    << "\tstd::size_t size() const { return data_->size(); }\n"
				    << "\titerator begin() { return data_->begin(); }\n"
				    << "\titerator end() { return data_->end(); }\n"
				    << "\tconst_iterator begin() const { return data_->begin(); }\n"
				    << "\tconst_iterator end() const { return data_->end(); }\n"
				    << "\tfriend bool operator==(const vs_Set& a, const vs_Set& b) { return a.data_ == b.data_; }\n"
				    << "\tfriend bool operator!=(const vs_Set& a, const vs_Set& b) { return !(a == b); }\n"
				    << "private: std::shared_ptr<storage_type> data_;\n"
				    << "};\n\n";
			}
			if (Flags.bNeedsDeque)
			{
				Support
				    << "template <typename T>\n"
				    << "class vs_Deque {\n"
				    << "public:\n"
				    << "\tusing storage_type = std::deque<T>;\n"
				    << "\tusing iterator = typename storage_type::iterator;\n"
				    << "\tusing const_iterator = typename storage_type::const_iterator;\n"
				    << "\tvs_Deque() : data_(std::make_shared<storage_type>()) {}\n"
				    << "\tvs_Deque(std::initializer_list<T> init) : data_(std::make_shared<storage_type>(init)) {}\n"
				    << "\tT& operator[](std::size_t i) { if (i >= data_->size()) VsScriptFail(\"deque index out of bounds\"); return (*data_)[i]; }\n"
				    << "\tconst T& operator[](std::size_t i) const { if (i >= data_->size()) VsScriptFail(\"deque index out of bounds\"); return (*data_)[i]; }\n"
				    << "\tvoid push_back(T value) { data_->push_back(std::move(value)); }\n"
				    << "\tvoid push_front(T value) { data_->push_front(std::move(value)); }\n"
				    << "\tT pop_back_value() { if (data_->empty()) VsScriptFail(\"pop_back from empty deque\"); T value = std::move(data_->back()); data_->pop_back(); return value; }\n"
				    << "\tT pop_front_value() { if (data_->empty()) VsScriptFail(\"pop_front from empty deque\"); T value = std::move(data_->front()); data_->pop_front(); return value; }\n"
				    << "\tT& front() { if (data_->empty()) VsScriptFail(\"front from empty deque\"); return data_->front(); }\n"
				    << "\tT& back() { if (data_->empty()) VsScriptFail(\"back from empty deque\"); return data_->back(); }\n"
				    << "\tvoid clear() { data_->clear(); }\n"
				    << "\tstd::size_t size() const { return data_->size(); }\n"
				    << "\titerator begin() { return data_->begin(); }\n"
				    << "\titerator end() { return data_->end(); }\n"
				    << "\tconst_iterator begin() const { return data_->begin(); }\n"
				    << "\tconst_iterator end() const { return data_->end(); }\n"
				    << "\tfriend bool operator==(const vs_Deque& a, const vs_Deque& b) { return a.data_ == b.data_; }\n"
				    << "\tfriend bool operator!=(const vs_Deque& a, const vs_Deque& b) { return !(a == b); }\n"
				    << "private: std::shared_ptr<storage_type> data_;\n"
				    << "};\n\n";
			}
			if (Flags.bNeedsQueue)
			{
				Support
				    << "template <typename T>\n"
				    << "class vs_Queue {\n"
				    << "public:\n"
				    << "\tvs_Queue() : data_(std::make_shared<std::queue<T>>()) {}\n"
				    << "\tvs_Queue(std::initializer_list<T> init) : vs_Queue() { for (T value : init) push(std::move(value)); }\n"
				    << "\tvoid push(T value) { data_->push(std::move(value)); }\n"
				    << "\tT pop_value() { if (data_->empty()) VsScriptFail(\"pop from empty queue\"); T value = std::move(data_->front()); data_->pop(); return value; }\n"
				    << "\tT& front() { if (data_->empty()) VsScriptFail(\"front from empty queue\"); return data_->front(); }\n"
				    << "\tvoid clear() { *data_ = {}; }\n"
				    << "\tstd::size_t size() const { return data_->size(); }\n"
				    << "\tstd::vector<T> values() const { std::vector<T> out; auto copy = *data_; out.reserve(copy.size()); while (!copy.empty()) { out.push_back(copy.front()); copy.pop(); } return out; }\n"
				    << "\tfriend bool operator==(const vs_Queue& a, const vs_Queue& b) { return a.data_ == b.data_; }\n"
				    << "\tfriend bool operator!=(const vs_Queue& a, const vs_Queue& b) { return !(a == b); }\n"
				    << "private: std::shared_ptr<std::queue<T>> data_;\n"
				    << "};\n\n"
				    << "template <typename T>\n"
				    << "class vs_PriorityQueue {\n"
				    << "public:\n"
				    << "\tvs_PriorityQueue() : data_(std::make_shared<std::priority_queue<T>>()) {}\n"
				    << "\tvs_PriorityQueue(std::initializer_list<T> init) : vs_PriorityQueue() { for (T value : init) push(std::move(value)); }\n"
				    << "\tvoid push(T value) { data_->push(std::move(value)); }\n"
				    << "\tT pop_value() { if (data_->empty()) VsScriptFail(\"pop from empty priority_queue\"); T value = data_->top(); data_->pop(); return value; }\n"
				    << "\tconst T& top() const { if (data_->empty()) VsScriptFail(\"top from empty priority_queue\"); return data_->top(); }\n"
				    << "\tvoid clear() { *data_ = {}; }\n"
				    << "\tstd::size_t size() const { return data_->size(); }\n"
				    << "\tstd::vector<T> values() const { std::vector<T> out; auto copy = *data_; out.reserve(copy.size()); while (!copy.empty()) { out.push_back(copy.top()); copy.pop(); } return out; }\n"
				    << "\tfriend bool operator==(const vs_PriorityQueue& a, const vs_PriorityQueue& b) { return a.data_ == b.data_; }\n"
				    << "\tfriend bool operator!=(const vs_PriorityQueue& a, const vs_PriorityQueue& b) { return !(a == b); }\n"
				    << "private: std::shared_ptr<std::priority_queue<T>> data_;\n"
				    << "};\n\n";
			}
			if (Flags.bNeedsStack)
			{
				Support
				    << "template <typename T>\n"
				    << "class vs_Stack {\n"
				    << "public:\n"
				    << "\tvs_Stack() : data_(std::make_shared<std::stack<T>>()) {}\n"
				    << "\tvoid push(T value) { data_->push(std::move(value)); }\n"
				    << "\tT pop_value() { if (data_->empty()) VsScriptFail(\"pop from empty stack\"); T value = std::move(data_->top()); data_->pop(); return value; }\n"
				    << "\tT& top() { if (data_->empty()) VsScriptFail(\"top from empty stack\"); return data_->top(); }\n"
				    << "\tvoid clear() { *data_ = {}; }\n"
				    << "\tstd::size_t size() const { return data_->size(); }\n"
				    << "\tstd::vector<T> values() const { std::vector<T> out; auto copy = *data_; out.reserve(copy.size()); while (!copy.empty()) { out.push_back(copy.top()); copy.pop(); } return out; }\n"
				    << "\tfriend bool operator==(const vs_Stack& a, const vs_Stack& b) { return a.data_ == b.data_; }\n"
				    << "\tfriend bool operator!=(const vs_Stack& a, const vs_Stack& b) { return !(a == b); }\n"
				    << "private: std::shared_ptr<std::stack<T>> data_;\n"
				    << "};\n\n";
			}
			if (Flags.bNeedsUtility)
			{
				Support
				    << "template <typename K, typename V>\n"
				    << "struct vs_InternalPair {\n"
				    << "\tK first{};\n"
				    << "\tV second{};\n"
				    << "};\n\n"
				    << "template <typename T>\n"
				    << "struct vs_Optional {\n"
				    << "\tstd::optional<T> value;\n"
				    << "\tvs_Optional() = default;\n"
				    << "\texplicit vs_Optional(T InValue) : value(std::move(InValue)) {}\n"
				    << "\tbool has_value() const { return value.has_value(); }\n"
				    << "\tT get() const { if (!value) VsScriptFail(\"optional has no value\"); return *value; }\n"
				    << "\tT& value_ref() { if (!value) VsScriptFail(\"optional has no value\"); return *value; }\n"
				    << "\tconst T& value_ref() const { if (!value) VsScriptFail(\"optional has no value\"); return *value; }\n"
				    << "\tT value_or(T fallback) const { return value ? *value : std::move(fallback); }\n"
				    << "\tvoid reset() { value.reset(); }\n"
				    << "};\n\n"
				    << "template <typename T>\n"
				    << "struct vs_Readonly {\n"
				    << "\tT value;\n"
				    << "\tvs_Readonly(T InValue) : value(std::move(InValue)) {}\n"
				    << "\tconst T& get() const { return value; }\n"
				    << "};\n\n"
				    << "template <typename T>\n"
				    << "struct vs_TypeToken {\n"
				    << "\tstd::string name;\n"
				    << "\tstd::string qualified_name;\n"
				    << "};\n\n"
				    << "template <typename Shared>\n"
				    << "struct vs_Weak {\n"
				    << "\tusing element_type = typename Shared::element_type;\n"
				    << "\tstd::weak_ptr<element_type> target;\n"
				    << "\tvs_Weak() = default;\n"
				    << "\texplicit vs_Weak(const Shared& InTarget) : target(InTarget) {}\n"
				    << "\tShared lock() const { return target.lock(); }\n"
				    << "\tbool expired() const { return target.expired(); }\n"
				    << "\tvoid reset() { target.reset(); }\n"
				    << "};\n\n"
				    << "template <typename K, typename V>\n"
				    << "struct vs_MapEntryRef {\n"
				    << "\tconst K& first;\n"
				    << "\tV& second;\n"
				    << "};\n\n"
				    << "template <typename F>\n"
				    << "struct vs_FinallyGuard {\n"
				    << "\tF fn; bool* active;\n"
				    << "\tvs_FinallyGuard(F InFn, bool& InActive) : fn(std::move(InFn)), active(&InActive) {}\n"
				    << "\tvs_FinallyGuard(const vs_FinallyGuard&) = delete;\n"
				    << "\t~vs_FinallyGuard() noexcept(false) { if (*active) fn(); }\n"
				    << "};\n"
				    << "template <typename F>\n"
				    << "static inline vs_FinallyGuard<F> vs_MakeFinally(F fn, bool& active) {\n"
				    << "\treturn vs_FinallyGuard<F>(std::move(fn), active);\n"
				    << "}\n\n"
				    << "template <typename... T> struct vs_TypeList {};\n"
				    << "struct vs_ExceptionCarrier;\n"
				    << "using vs_ExceptionRebindFn = void(*)(vs_ExceptionCarrier&, const void*);\n"
				    << "struct vs_ExceptionView { std::type_index type; void* value; };\n"
				    << "struct vs_ExceptionRebinder { std::type_index type; vs_ExceptionRebindFn fn; };\n"
				    << "struct vs_ExceptionCarrier {\n"
				    << "\tstd::vector<std::any> values;\n"
				    << "\tstd::shared_ptr<void> owner;\n"
				    << "\tstd::vector<vs_ExceptionView> views;\n"
				    << "\tstd::vector<vs_ExceptionRebinder> rebinders;\n"
				    << "\ttemplate <typename T> T* get() {\n"
				    << "\t\tfor (auto& view : views) { if (view.type == std::type_index(typeid(T))) return static_cast<T*>(view.value); }\n"
				    << "\t\tfor (auto& value : values) { if (auto* found = std::any_cast<T>(&value)) return found; }\n"
				    << "\t\treturn nullptr;\n"
				    << "\t}\n"
				    << "\ttemplate <typename T> void rebind(const T& value) {\n"
				    << "\t\tfor (const auto& entry : rebinders) { if (entry.type == std::type_index(typeid(T))) { entry.fn(*this, &value); return; } }\n"
				    << "\t}\n"
				    << "};\n"
				    << "template <typename Target, typename Source>\n"
				    << "static inline void vs_AddReboundHandle(vs_ExceptionCarrier& result, const Source& source) {\n"
				    << "\tif constexpr (std::is_same_v<Target, Source>)\n"
				    << "\t{\n"
				    << "\t\tresult.values.emplace_back(source);\n"
				    << "\t}\n"
				    << "\telse if (auto converted = std::dynamic_pointer_cast<typename Target::element_type>(source))\n"
				    << "\t{\n"
				    << "\t\tresult.values.emplace_back(std::move(converted));\n"
				    << "\t}\n"
				    << "}\n"
				    << "template <typename Source, typename... Targets>\n"
				    << "static inline void vs_RebindExceptionHandles(vs_ExceptionCarrier& result, const void* raw) {\n"
				    // `raw` normally points into result.values. Preserve the handle before
				    // clearing that vector; otherwise the catch-reference rebind would read
				    // a destroyed std::shared_ptr.
				    << "\tconst Source source = *static_cast<const Source*>(raw);\n"
				    << "\tresult.values.clear();\n"
				    << "\t(vs_AddReboundHandle<Targets>(result, source), ...);\n"
				    << "}\n"
				    << "template <typename T>\n"
				    << "static inline vs_ExceptionCarrier vs_MakeException(T value) {\n"
				    << "\tvs_ExceptionCarrier result; result.values.emplace_back(std::move(value)); return result;\n"
				    << "}\n"
				    << "template <typename T, typename... Bases>\n"
				    << "static inline vs_ExceptionCarrier vs_MakeExceptionAs(T value) {\n"
				    << "\tvs_ExceptionCarrier result; result.values.emplace_back(value);\n"
				    << "\t(result.values.emplace_back(Bases(value)), ...);\n"
				    << "\tresult.rebinders.push_back({std::type_index(typeid(T)), &vs_RebindExceptionHandles<T, T, Bases...>});\n"
				    << "\t(result.rebinders.push_back({std::type_index(typeid(Bases)), &vs_RebindExceptionHandles<Bases, T, Bases...>}), ...);\n"
				    << "\treturn result;\n"
				    << "}\n"
				    << "template <typename T, typename... ValueBases, typename... HandleBases>\n"
				    << "static inline vs_ExceptionCarrier vs_MakeValueExceptionAs(\n"
				    << "\tT value, vs_TypeList<ValueBases...>, vs_TypeList<HandleBases...>) {\n"
				    << "\tvs_ExceptionCarrier result;\n"
				    << "\tauto owner = std::make_shared<T>(std::move(value));\n"
				    << "\tresult.owner = owner;\n"
				    << "\tresult.views.push_back({std::type_index(typeid(T)), owner.get()});\n"
				    << "\t(result.views.push_back({std::type_index(typeid(ValueBases)), static_cast<ValueBases*>(owner.get())}), ...);\n"
				    << "\t(result.values.emplace_back(std::shared_ptr<HandleBases>(owner)), ...);\n"
				    << "\treturn result;\n"
				    << "}\n\n";
			}
			if (Flags.bNeedsChannel || Flags.bNeedsThread)
			{
				// AOT workers use OS threads. Script global/static storage is emitted as
				// thread-local state; channels are the explicit synchronized sharing path.
				// Channel / join / spawn glue mirrors VM __channel_* / __join_task semantics.
				Support
				    << "template <typename T>\n"
				    << "struct vs_Channel {\n"
				    << "\tstd::mutex mu;\n"
				    << "\tstd::condition_variable can_send;\n"
				    << "\tstd::condition_variable can_receive;\n"
				    << "\tstd::queue<T> q;\n"
				    << "\tstd::size_t max_capacity = 0;\n"
				    << "\tbool capacity_configured = false;\n"
				    << "\tbool used = false;\n"
				    << "\tbool closed = false;\n"
				    << "\tbool send(T v) {\n"
				    << "\t\t{\n"
				    << "\t\t\tstd::unique_lock<std::mutex> lock(mu);\n"
				    << "\t\t\tused = true;\n"
				    << "\t\t\tcan_send.wait(lock, [&]{ return closed || max_capacity == 0 || q.size() < max_capacity; });\n"
				    << "\t\t\tif (closed) return false;\n"
				    << "\t\t\tq.push(std::move(v));\n"
				    << "\t\t}\n"
				    << "\t\tcan_receive.notify_one();\n"
				    << "\t\treturn true;\n"
				    << "\t}\n"
				    << "\tbool try_send(T v) {\n"
				    << "\t\t{\n"
				    << "\t\t\tstd::lock_guard<std::mutex> lock(mu);\n"
				    << "\t\t\tused = true;\n"
				    << "\t\t\tif (closed || (max_capacity != 0 && q.size() >= max_capacity)) return false;\n"
				    << "\t\t\tq.push(std::move(v));\n"
				    << "\t\t}\n"
				    << "\t\tcan_receive.notify_one();\n"
				    << "\t\treturn true;\n"
				    << "\t}\n"
				    << "\tbool send_for(T v, int timeout_ms) {\n"
				    << "\t\tif (timeout_ms < 0) VsScriptFail(\"channel timeout must be non-negative\");\n"
				    << "\t\t{\n"
				    << "\t\t\tstd::unique_lock<std::mutex> lock(mu);\n"
				    << "\t\t\tused = true;\n"
				    << "\t\t\tif (!can_send.wait_for(lock, std::chrono::milliseconds(timeout_ms),\n"
				    << "\t\t\t\t[&]{ return closed || max_capacity == 0 || q.size() < max_capacity; })) return false;\n"
				    << "\t\t\tif (closed) return false;\n"
				    << "\t\t\tq.push(std::move(v));\n"
				    << "\t\t}\n"
				    << "\t\tcan_receive.notify_one();\n"
				    << "\t\treturn true;\n"
				    << "\t}\n"
				    << "\tbool recv(T& Out) {\n"
				    << "\t\tstd::unique_lock<std::mutex> lock(mu);\n"
				    << "\t\tused = true;\n"
				    << "\t\tcan_receive.wait(lock, [&]{ return closed || !q.empty(); });\n"
				    << "\t\tif (q.empty()) return false;\n"
				    << "\t\tOut = std::move(q.front()); q.pop();\n"
				    << "\t\tlock.unlock(); can_send.notify_one(); return true;\n"
				    << "\t}\n"
				    << "\tbool try_recv(T& Out) {\n"
				    << "\t\t{\n"
				    << "\t\t\tstd::lock_guard<std::mutex> lock(mu);\n"
				    << "\t\t\tused = true;\n"
				    << "\t\t\tif (q.empty()) return false;\n"
				    << "\t\t\tOut = std::move(q.front()); q.pop();\n"
				    << "\t\t}\n"
				    << "\t\tcan_send.notify_one(); return true;\n"
				    << "\t}\n"
				    << "\tbool recv_for(T& Out, int timeout_ms) {\n"
				    << "\t\tif (timeout_ms < 0) VsScriptFail(\"channel timeout must be non-negative\");\n"
				    << "\t\tstd::unique_lock<std::mutex> lock(mu);\n"
				    << "\t\tused = true;\n"
				    << "\t\tif (!can_receive.wait_for(lock, std::chrono::milliseconds(timeout_ms),\n"
				    << "\t\t\t[&]{ return closed || !q.empty(); })) return false;\n"
				    << "\t\tif (q.empty()) return false;\n"
				    << "\t\tOut = std::move(q.front()); q.pop();\n"
				    << "\t\tlock.unlock(); can_send.notify_one(); return true;\n"
				    << "\t}\n"
				    << "\tvoid configure_capacity(std::int64_t capacity) {\n"
				    << "\t\tif (capacity <= 0) VsScriptFail(\"channel capacity must be positive\");\n"
				    << "\t\tstd::lock_guard<std::mutex> lock(mu);\n"
				    << "\t\tif (closed || used || capacity_configured) VsScriptFail(\"channel capacity can only be configured before first use\");\n"
				    << "\t\tmax_capacity = VsCheckedNativeSize(capacity);\n"
				    << "\t\tcapacity_configured = true;\n"
				    << "\t}\n"
				    << "\tstd::int64_t size() {\n"
				    << "\t\tstd::lock_guard<std::mutex> lock(mu);\n"
				    << "\t\tif (q.size() > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) VsScriptFail(\"channel size exceeds int64 range\");\n"
				    << "\t\treturn VsCheckedLength(q.size());\n"
				    << "\t}\n"
				    << "\tstd::int64_t capacity() {\n"
				    << "\t\tstd::lock_guard<std::mutex> lock(mu);\n"
				    << "\t\tif (max_capacity > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) VsScriptFail(\"channel capacity exceeds int64 range\");\n"
				    << "\t\treturn VsCheckedLength(max_capacity);\n"
				    << "\t}\n"
				    << "\tbool is_closed() {\n"
				    << "\t\tstd::lock_guard<std::mutex> lock(mu); return closed;\n"
				    << "\t}\n"
				    << "\tvoid close() {\n"
				    << "\t\t{\n"
				    << "\t\t\tstd::lock_guard<std::mutex> lock(mu);\n"
				    << "\t\t\tclosed = true;\n"
				    << "\t\t}\n"
				    << "\t\tcan_send.notify_all();\n"
				    << "\t\tcan_receive.notify_all();\n"
				    << "\t}\n"
				    << "};\n\n"
				    << "template <typename T>\n"
				    << "static inline void VsChannelSend(const std::shared_ptr<vs_Channel<T>>& Ch, T v) {\n"
				    << "\tif (!Ch || !Ch->send(std::move(v))) VsScriptFail(\"channel is closed\");\n"
				    << "}\n"
				    << "template <typename T>\n"
				    << "static inline T VsChannelRecv(const std::shared_ptr<vs_Channel<T>>& Ch) {\n"
				    << "\tT v{};\n"
				    << "\tif (!Ch || !Ch->recv(v)) VsScriptFail(\"channel is closed\");\n"
				    << "\treturn v;\n"
				    << "}\n"
				    << "template <typename T>\n"
				    << "static inline bool VsChannelTrySend(const std::shared_ptr<vs_Channel<T>>& Ch, T v) {\n"
				    << "\treturn Ch && Ch->try_send(std::move(v));\n"
				    << "}\n"
				    << "template <typename T>\n"
				    << "static inline bool VsChannelSendFor(const std::shared_ptr<vs_Channel<T>>& Ch, T v, int timeout_ms) {\n"
				    << "\treturn Ch && Ch->send_for(std::move(v), timeout_ms);\n"
				    << "}\n"
				    << "template <typename T>\n"
				    << "static inline vs_Array<T> VsChannelTryRecv(const std::shared_ptr<vs_Channel<T>>& Ch) {\n"
				    << "\tvs_Array<T> result; T v{};\n"
				    << "\tif (Ch && Ch->try_recv(v)) result.push_back(std::move(v));\n"
				    << "\treturn result;\n"
				    << "}\n"
				    << "template <typename T>\n"
				    << "static inline vs_Array<T> VsChannelRecvFor(const std::shared_ptr<vs_Channel<T>>& Ch, int timeout_ms) {\n"
				    << "\tvs_Array<T> result; T v{};\n"
				    << "\tif (Ch && Ch->recv_for(v, timeout_ms)) result.push_back(std::move(v));\n"
				    << "\treturn result;\n"
				    << "}\n\n"
				    << "struct VsTaskJoinState {\n"
				    << "\tstd::mutex Mu;\n"
				    << "\tstd::condition_variable Cv;\n"
				    << "\tbool Done = false;\n"
				    << "\tbool Failed = false;\n"
				    << "\tbool CancelRequested = false;\n"
				    << "};\n"
				    << "static thread_local VsTaskJoinState* VsCurrentTaskState = nullptr;\n"
				    << "static inline bool VsCancelRequested() {\n"
				    << "\tif (!VsCurrentTaskState) return false;\n"
				    << "\tstd::lock_guard<std::mutex> Lock(VsCurrentTaskState->Mu);\n"
				    << "\treturn VsCurrentTaskState->CancelRequested;\n"
				    << "}\n"
				    << "static inline std::mutex& VsTaskMu() {\n"
				    << "\tstatic std::mutex Mu; return Mu;\n"
				    << "}\n"
				    << "static inline std::unordered_map<int, std::thread>& VsTasks() {\n"
				    << "\tstatic std::unordered_map<int, std::thread> Tasks; return Tasks;\n"
				    << "}\n"
				    << "static inline std::unordered_map<int, std::shared_ptr<VsTaskJoinState>>& "
				    << "VsTaskStates() {\n"
				    << "\tstatic std::unordered_map<int, std::shared_ptr<VsTaskJoinState>> States;\n"
				    << "\treturn States;\n"
				    << "}\n"
				    << "static inline std::unordered_map<int, bool>& VsCompletedTaskResults() {\n"
				    << "\tstatic std::unordered_map<int, bool> Results; return Results;\n"
				    << "}\n"
				    << "static inline std::unordered_map<int, std::function<void()>>& "
				    << "VsSpawnChannelClosers() {\n"
				    << "\tstatic std::unordered_map<int, std::function<void()>> Closers;\n"
				    << "\treturn Closers;\n"
				    << "}\n"
				    << "static inline int& VsNextTaskId() {\n"
				    << "\tstatic int Next = 0; return Next;\n"
				    << "}\n"
				    << "static inline bool& VsConcurrentShutdownComplete() {\n"
				    << "\tstatic bool Complete = false; return Complete;\n"
				    << "}\n"
				    << "struct VsTimedOutTask {\n"
				    << "\tstd::thread Thread;\n"
				    << "\tstd::shared_ptr<VsTaskJoinState> State;\n"
				    << "};\n"
				    << "static inline std::unordered_map<int, VsTimedOutTask>& VsTimedOutTasks() {\n"
				    << "\tstatic std::unordered_map<int, VsTimedOutTask> Tasks; return Tasks;\n"
				    << "}\n"
				    << "static inline void VsRetainTimedOutTask(\n"
				    << "\tint Id, std::thread Thread, std::shared_ptr<VsTaskJoinState> State) noexcept {\n"
				    << "\ttry {\n"
				    << "\t\tstd::lock_guard<std::mutex> Lock(VsTaskMu());\n"
				    << "\t\tauto Added = VsTimedOutTasks().try_emplace(Id);\n"
				    << "\t\tif (!Added.second) std::_Exit(EXIT_FAILURE);\n"
				    << "\t\tAdded.first->second.State = std::move(State);\n"
				    << "\t\tAdded.first->second.Thread = std::move(Thread);\n"
				    << "\t}\n\tcatch (...)\n\t{\n\t\tstd::_Exit(EXIT_FAILURE);\n\t}\n"
				    << "}\n"
				    << "static inline std::mutex& VsChannelRegistryMu() {\n"
				    << "\tstatic std::mutex Mu; return Mu;\n"
				    << "}\n"
				    << "static inline bool& VsChannelsShuttingDown() {\n"
				    << "\tstatic bool ShuttingDown = false; return ShuttingDown;\n"
				    << "}\n"
				    << "struct VsChannelCloserEntry {\n"
				    << "\tstd::weak_ptr<void> Lifetime;\n"
				    << "\tstd::function<void()> Close;\n"
				    << "};\n"
				    << "static inline std::vector<VsChannelCloserEntry>& VsChannelClosers() {\n"
				    << "\tstatic std::vector<VsChannelCloserEntry> Closers; return Closers;\n"
				    << "}\n"
				    << "template <typename T>\n"
				    << "static inline std::shared_ptr<vs_Channel<T>> VsMakeChannel() {\n"
				    << "\tauto Ch = std::make_shared<vs_Channel<T>>();\n"
				    << "\tbool ShuttingDown = false;\n"
				    << "\t{\n"
				    << "\t\tstd::lock_guard<std::mutex> Lock(VsChannelRegistryMu());\n"
				    << "\t\tShuttingDown = VsChannelsShuttingDown();\n"
				    << "\t\tauto& Closers = VsChannelClosers();\n"
				    << "\t\tClosers.erase(std::remove_if(Closers.begin(), Closers.end(),\n"
				    << "\t\t\t[](const VsChannelCloserEntry& Entry) { return Entry.Lifetime.expired(); }),\n"
				    << "\t\t\tClosers.end());\n"
				    << "\t\tstd::weak_ptr<vs_Channel<T>> Weak = Ch;\n"
				    << "\t\tClosers.push_back({Ch, [Weak]() {\n"
				    << "\t\t\tif (auto P = Weak.lock()) P->close();\n"
				    << "\t\t}});\n"
				    << "\t}\n"
				    << "\tif (ShuttingDown) Ch->close();\n"
				    << "\treturn Ch;\n"
				    << "}\n"
				    << "static inline void VsCloseAllChannels() {\n"
				    << "\tstd::vector<std::function<void()>> Closers;\n"
				    << "\t{\n"
				    << "\t\tstd::lock_guard<std::mutex> Lock(VsChannelRegistryMu());\n"
				    << "\t\tVsChannelsShuttingDown() = true;\n"
				    << "\t\tauto& Entries = VsChannelClosers();\n"
				    << "\t\tfor (auto It = Entries.begin(); It != Entries.end();) {\n"
				    << "\t\t\tif (It->Lifetime.expired()) { It = Entries.erase(It); continue; }\n"
				    << "\t\t\tClosers.push_back(It->Close);\n"
				    << "\t\t\t++It;\n"
				    << "\t\t}\n"
				    << "\t}\n"
				    << "\tfor (auto& Closer : Closers) { if (Closer) Closer(); }\n"
				    << "}\n"
				    << "static inline long long VsResolveJoinTimeoutMs() {\n"
				    << "\tstd::lock_guard<std::mutex> Lock(VsEnvironmentMutex());\n"
				    << "\tstd::string EnvText;\n"
				    << "#if defined(_WIN32)\n"
				    << "\tchar* RawEnv = nullptr; std::size_t RawEnvLength = 0;\n"
				    << "\tif (_dupenv_s(&RawEnv, &RawEnvLength, \"VERO_JOIN_TIMEOUT_MS\") != 0 || !RawEnv) return 30000LL;\n"
				    << "\tEnvText.assign(RawEnv); std::free(RawEnv);\n"
				    << "#else\n"
				    << "\tconst char* RawEnv = std::getenv(\"VERO_JOIN_TIMEOUT_MS\");\n"
				    << "\tif (!RawEnv) return 30000LL;\n"
				    << "\tEnvText.assign(RawEnv);\n"
				    << "#endif\n"
				    << "\tif (EnvText.empty()) return 30000LL;\n"
				    << "\tconst std::string_view Text(EnvText);\n"
				    << "\tlong long V = 0;\n"
				    << "\tconst auto Parsed = std::from_chars(Text.data(), Text.data() + Text.size(), V, 10);\n"
				    << "\tif (Parsed.ec != std::errc{} || Parsed.ptr != Text.data() + Text.size() || V < 0) return 30000LL;\n"
				    << "\treturn V;\n"
				    << "}\n"
				    << "static constexpr long long VsJoinGraceMs = 1000;\n"
				    << "template <typename F>\n"
				    << "static inline int VsSpawn(F&& Fn) {\n"
				    << "\tstd::lock_guard<std::mutex> Lock(VsTaskMu());\n"
				    << "\tif (VsNextTaskId() < 0 || VsNextTaskId() == std::numeric_limits<int>::max()) {\n"
				    << "\t\tVsScriptFail(\"task id space exhausted\");\n"
				    << "\t}\n"
				    << "\tconst int Id = VsNextTaskId()++;\n"
				    << "\tauto State = std::make_shared<VsTaskJoinState>();\n"
				    << "\tauto TaskSlot = VsTasks().emplace(Id, std::thread{});\n"
				    << "\tif (!TaskSlot.second) VsScriptFail(\"duplicate task id\");\n"
				    << "\ttry {\n"
				    << "\t\tauto StateSlot = VsTaskStates().emplace(Id, State);\n"
				    << "\t\tif (!StateSlot.second) {\n"
				    << "\t\t\tVsScriptFail(\"duplicate task state id\");\n"
				    << "\t\t}\n"
				    << "\t}\n\tcatch (...)\n\t{\n"
				    << "\t\tVsTasks().erase(TaskSlot.first);\n"
				    << "\t\tthrow;\n"
				    << "\t}\n"
				    << "\ttry {\n"
				    << "\t\tTaskSlot.first->second = std::thread([State, Fn = "
				    << "std::decay_t<F>(std::forward<F>(Fn))]() mutable {\n"
				    << "\t\tbool Failed = false;\n"
				    << "\t\tVsCurrentTaskState = State.get();\n"
				    << "\t\ttry {\n"
				    << (Flags.bNeedsConsole ? "\t\t\tVsConsoleRuntime::Detail::FScopedRestore ConsoleRestore(std::cout);\n" : "")
				    << "\t\t\tFn();\n"
				    << "\t\t}\n\t\tcatch (...)\n\t\t{\n\t\t\tFailed = true;\n\t\t}\n"
				    << "\t\tVsCurrentTaskState = nullptr;\n"
				    << "\t\t{\n"
				    << "\t\t\tstd::lock_guard<std::mutex> DoneLock(State->Mu);\n"
				    << "\t\t\tState->Failed = Failed;\n"
				    << "\t\t\tState->Done = true;\n"
				    << "\t\t}\n"
				    << "\t\t\tState->Cv.notify_all();\n"
				    << "\t\t});\n"
				    << "\t}\n\tcatch (...)\n\t{\n"
				    << "\t\tVsTaskStates().erase(Id);\n"
				    << "\t\tVsTasks().erase(TaskSlot.first);\n"
				    << "\t\tthrow;\n"
				    << "\t}\n"
				    << "\treturn Id;\n"
				    << "}\n"
				    << "static inline void VsJoinTaskAfterShutdown(int Id) noexcept;\n"
				    << "static inline int VsSpawnWithChannelCloser(\n"
				    << "\tstd::function<void()> OnJoinTimeoutClose, std::function<void()> Fn) {\n"
				    << "\tconst int Id = VsSpawn(std::move(Fn));\n"
				    << "\tbool JoinImmediately = false;\n"
				    << "\t{\n"
				    << "\t\tstd::lock_guard<std::mutex> Lock(VsTaskMu());\n"
				    << "\t\tif (OnJoinTimeoutClose) VsSpawnChannelClosers()[Id] = std::move(OnJoinTimeoutClose);\n"
				    << "\t\tJoinImmediately = VsConcurrentShutdownComplete();\n"
				    << "\t}\n"
				    << "\tif (JoinImmediately) VsJoinTaskAfterShutdown(Id);\n"
				    << "\treturn Id;\n"
				    << "}\n"
				    << "static inline int VsSpawnResultWithChannelCloser(\n"
				    << "\tstd::function<void()> Close, std::function<void()> Fn) {\n"
				    << "\tstd::function<void()> TimeoutClose = Close;\n"
				    << "\treturn VsSpawnWithChannelCloser(std::move(TimeoutClose), "
				    << "[Close = std::move(Close), Fn = std::move(Fn)]() mutable {\n"
				    << "\t\ttry {\n"
				    << "\t\t\tFn();\n"
				    << "\t\t}\n\t\tcatch (...)\n\t\t{\n"
				    << "\t\t\tif (Close) Close();\n"
				    << "\t\t\tthrow;\n"
				    << "\t\t}\n"
				    << "\t\tif (Close) Close();\n"
				    << "\t});\n"
				    << "}\n"
				    << "static inline bool VsJoinTask(int Id) {\n"
				    << "\tstd::thread Th;\n"
				    << "\tstd::shared_ptr<VsTaskJoinState> State;\n"
				    << "\t{\n"
				    << "\t\tstd::lock_guard<std::mutex> Lock(VsTaskMu());\n"
				    << "\t\tauto It = VsTasks().find(Id);\n"
				    << "\t\tif (It != VsTasks().end()) {\n"
				    << "\t\t\tTh = std::move(It->second);\n"
				    << "\t\t\tVsTasks().erase(It);\n"
				    << "\t\t\tauto Sit = VsTaskStates().find(Id);\n"
				    << "\t\t\tif (Sit != VsTaskStates().end()) {\n"
				    << "\t\t\t\tState = Sit->second;\n"
				    << "\t\t\t\tVsTaskStates().erase(Sit);\n"
				    << "\t\t\t}\n"
				    << "\t\t}\n\t\telse\n\t\t{\n"
				    << "\t\t\tauto TimedOut = VsTimedOutTasks().find(Id);\n"
				    << "\t\t\tif (TimedOut == VsTimedOutTasks().end()) {\n"
				    << "\t\t\t\tauto Completed = VsCompletedTaskResults().find(Id);\n"
				    << "\t\t\t\tif (Completed != VsCompletedTaskResults().end()) return Completed->second;\n"
				    << "\t\t\t\tVsScriptFail(\"invalid task id\");\n"
				    << "\t\t\t}\n"
				    << "\t\t\tTh = std::move(TimedOut->second.Thread);\n"
				    << "\t\t\tState = std::move(TimedOut->second.State);\n"
				    << "\t\t\tVsTimedOutTasks().erase(TimedOut);\n"
				    << "\t\t}\n"
				    << "\t}\n"
				    << "\tif (!Th.joinable()) return true;\n"
				    << "\tbool Ready = true;\n"
				    << "\tif (State) {\n"
				    << "\t\tstd::unique_lock<std::mutex> Lock(State->Mu);\n"
				    << "\t\tReady = State->Cv.wait_for(\n"
				    << "\t\t\tLock,\n"
				    << "\t\t\tstd::chrono::milliseconds(VsResolveJoinTimeoutMs()),\n"
				    << "\t\t\t[&]{ return State->Done; });\n"
				    << "\t}\n"
				    << "\tif (!Ready) {\n"
				    << "\t\tstd::function<void()> Closer;\n"
				    << "\t\t{\n"
				    << "\t\t\tstd::lock_guard<std::mutex> Lock(VsTaskMu());\n"
				    << "\t\t\tauto Cit = VsSpawnChannelClosers().find(Id);\n"
				    << "\t\t\tif (Cit != VsSpawnChannelClosers().end()) {\n"
				    << "\t\t\t\tCloser = std::move(Cit->second);\n"
				    << "\t\t\t\tVsSpawnChannelClosers().erase(Cit);\n"
				    << "\t\t\t}\n"
				    << "\t\t}\n"
				    << "\t\tif (Closer) Closer();\n"
				    << "\t\tif (State) {\n"
				    << "\t\t\tstd::unique_lock<std::mutex> Lock(State->Mu);\n"
				    << "\t\t\tReady = State->Cv.wait_for(\n"
				    << "\t\t\t\tLock,\n"
				    << "\t\t\t\tstd::chrono::milliseconds(VsJoinGraceMs),\n"
				    << "\t\t\t\t[&]{ return State->Done; });\n"
				    << "\t\t}\n"
				    << "\t\tif (!Ready) {\n"
				    << "\t\t\tVsRetainTimedOutTask(Id, std::move(Th), std::move(State));\n"
				    << "\t\t\treturn false;\n"
				    << "\t\t}\n"
				    << "\t}\n"
				    << "\t{\n"
				    << "\t\tstd::lock_guard<std::mutex> Lock(VsTaskMu());\n"
				    << "\t\tVsSpawnChannelClosers().erase(Id);\n"
				    << "\t}\n"
				    << "\tif (Th.joinable()) Th.join();\n"
				    << "\tbool Succeeded = true;\n"
				    << "\tif (State) {\n"
				    << "\t\tstd::lock_guard<std::mutex> StateLock(State->Mu);\n"
				    << "\t\tSucceeded = !State->Failed;\n"
				    << "\t}\n"
				    << "\t{\n"
				    << "\t\tstd::lock_guard<std::mutex> Lock(VsTaskMu());\n"
				    << "\t\tVsCompletedTaskResults()[Id] = Succeeded;\n"
				    << "\t}\n"
				    << "\treturn Succeeded;\n"
				    << "}\n"
				    << "static inline void VsJoinTaskAfterShutdown(int Id) noexcept {\n"
				    << "\t// Global TLS destructors can spawn after the main shutdown guard.\n"
				    << "\ttry {\n"
				    << "\t\t(void)VsJoinTask(Id);\n"
				    << "\t\tstd::lock_guard<std::mutex> Lock(VsTaskMu());\n"
				    << "\t\tif (!VsTimedOutTasks().empty()) std::_Exit(EXIT_FAILURE);\n"
				    << "\t}\n\tcatch (...)\n\t{\n\t\tstd::_Exit(EXIT_FAILURE);\n\t}\n"
				    << "}\n"
				    << "static inline std::shared_ptr<VsTaskJoinState> VsFindTaskState(int Id) {\n"
				    << "\tstd::lock_guard<std::mutex> Lock(VsTaskMu());\n"
				    << "\tauto Active = VsTaskStates().find(Id);\n"
				    << "\tif (Active != VsTaskStates().end()) return Active->second;\n"
				    << "\tauto TimedOut = VsTimedOutTasks().find(Id);\n"
				    << "\treturn TimedOut == VsTimedOutTasks().end() ? nullptr : TimedOut->second.State;\n"
				    << "}\n"
				    << "static inline bool VsTaskDone(int Id) {\n"
				    << "\tauto State = VsFindTaskState(Id);\n"
				    << "\tif (!State) {\n"
				    << "\t\tstd::lock_guard<std::mutex> Lock(VsTaskMu());\n"
				    << "\t\tif (VsCompletedTaskResults().find(Id) != VsCompletedTaskResults().end()) return true;\n"
				    << "\t\tVsScriptFail(\"invalid task id\");\n"
				    << "\t}\n"
				    << "\tstd::lock_guard<std::mutex> Lock(State->Mu);\n"
				    << "\treturn State->Done;\n"
				    << "}\n"
				    << "static inline bool VsJoinTaskForOrFail(int Id, int timeout_ms) {\n"
				    << "\tif (timeout_ms < 0) VsScriptFail(\"task timeout must be non-negative\");\n"
				    << "\tauto State = VsFindTaskState(Id);\n"
				    << "\tif (State) {\n"
				    << "\t\tstd::unique_lock<std::mutex> Lock(State->Mu);\n"
				    << "\t\tif (!State->Cv.wait_for(Lock, std::chrono::milliseconds(timeout_ms),\n"
				    << "\t\t\t[&]{ return State->Done; })) return false;\n"
				    << "\t}\n"
				    << "\tif (!VsJoinTask(Id)) VsScriptFail(\"spawned task failed\");\n"
				    << "\treturn true;\n"
				    << "}\n"
				    << "static inline void VsJoinTaskOrFail(int Id) {\n"
				    << "\tif (!VsJoinTask(Id)) {\n"
				    << "\t\tVsScriptFail(\n"
				    << "\t\t\t\"spawned task failed or join_task timed out\");\n"
				    << "\t}\n"
				    << "}\n"
				    << "static inline void VsCancelTask(int Id) {\n"
				    << "\tstd::thread Th;\n"
				    << "\tstd::shared_ptr<VsTaskJoinState> State;\n"
				    << "\tstd::function<void()> Closer;\n"
				    << "\t{\n"
				    << "\t\tstd::lock_guard<std::mutex> Lock(VsTaskMu());\n"
				    << "\t\tauto It = VsTasks().find(Id);\n"
				    << "\t\tif (It != VsTasks().end()) {\n"
				    << "\t\t\tTh = std::move(It->second);\n"
				    << "\t\t\tVsTasks().erase(It);\n"
				    << "\t\t\tauto Sit = VsTaskStates().find(Id);\n"
				    << "\t\t\tif (Sit != VsTaskStates().end()) {\n"
				    << "\t\t\t\tState = Sit->second;\n"
				    << "\t\t\t\tVsTaskStates().erase(Sit);\n"
				    << "\t\t\t}\n"
				    << "\t\t}\n\t\telse\n\t\t{\n"
				    << "\t\t\tauto TimedOut = VsTimedOutTasks().find(Id);\n"
				    << "\t\t\tif (TimedOut != VsTimedOutTasks().end()) {\n"
				    << "\t\t\t\tTh = std::move(TimedOut->second.Thread);\n"
				    << "\t\t\t\tState = std::move(TimedOut->second.State);\n"
				    << "\t\t\t\tVsTimedOutTasks().erase(TimedOut);\n"
				    << "\t\t\t}\n"
				    << "\t\t}\n"
				    << "\t\tauto Cit = VsSpawnChannelClosers().find(Id);\n"
				    << "\t\tif (Cit != VsSpawnChannelClosers().end()) {\n"
				    << "\t\t\tCloser = std::move(Cit->second);\n"
				    << "\t\t\tVsSpawnChannelClosers().erase(Cit);\n"
				    << "\t\t}\n"
				    << "\t}\n"
				    << "\tif (State) {\n"
				    << "\t\tstd::lock_guard<std::mutex> StateLock(State->Mu);\n"
				    << "\t\tState->CancelRequested = true;\n"
				    << "\t}\n"
				    << "\tif (Closer) Closer();\n"
				    << "\tbool Ready = true;\n"
				    << "\tif (State) {\n"
				    << "\t\tstd::unique_lock<std::mutex> Lock(State->Mu);\n"
				    << "\t\tReady = State->Cv.wait_for(\n"
				    << "\t\t\tLock, std::chrono::milliseconds(VsJoinGraceMs),\n"
				    << "\t\t\t[&]{ return State->Done; });\n"
				    << "\t}\n"
				    << "\tif (Th.joinable()) {\n"
				    << "\t\tif (Ready) {\n"
				    << "\t\t\tTh.join();\n"
				    << "\t\t\tbool Succeeded = true;\n"
				    << "\t\t\tif (State) {\n"
				    << "\t\t\t\tstd::lock_guard<std::mutex> StateLock(State->Mu);\n"
				    << "\t\t\t\tSucceeded = !State->Failed;\n"
				    << "\t\t\t}\n"
				    << "\t\t\tstd::lock_guard<std::mutex> Lock(VsTaskMu());\n"
				    << "\t\t\tVsCompletedTaskResults()[Id] = Succeeded;\n"
				    << "\t\t}\n\t\telse VsRetainTimedOutTask(Id, std::move(Th), std::move(State));\n"
				    << "\t}\n"
				    << "}\n"
				    << "static inline void VsShutdownConcurrent() {\n"
				    << "\tstd::vector<int> Ids;\n"
				    << "\t{\n"
				    << "\t\tstd::lock_guard<std::mutex> Lock(VsTaskMu());\n"
				    << "\t\tfor (const auto& Entry : VsTimedOutTasks()) {\n"
				    << "\t\t\tIds.push_back(Entry.first);\n"
				    << "\t\t}\n"
				    << "\t}\n"
				    << "\tfor (;;) {\n"
				    << "\t\tVsCloseAllChannels();\n"
				    << "\t\t{\n"
				    << "\t\t\tstd::lock_guard<std::mutex> Lock(VsTaskMu());\n"
				    << "\t\t\tIds.reserve(Ids.size() + VsTasks().size());\n"
				    << "\t\t\tfor (const auto& Entry : VsTasks()) {\n"
				    << "\t\t\t\tIds.push_back(Entry.first);\n"
				    << "\t\t\t}\n"
				    << "\t\t\tif (Ids.empty()) {\n"
				    << "\t\t\t\tif (VsTimedOutTasks().empty()) VsConcurrentShutdownComplete() = true;\n"
				    << "\t\t\t\treturn;\n"
				    << "\t\t\t}\n"
				    << "\t\t}\n"
				    << "\t\tfor (int Id : Ids) {\n"
				    << "\t\t\t(void)VsJoinTask(Id);\n"
				    << "\t\t}\n"
				    << "\t\tIds.clear();\n"
				    << "\t\t// Workers may have spawned another generation while being joined.\n"
				    << "\t\t// Retained timeouts must reach the failure guard, not be retried forever.\n"
				    << "\t\tstd::lock_guard<std::mutex> Lock(VsTaskMu());\n"
				    << "\t\tif (!VsTimedOutTasks().empty()) return;\n"
				    << "\t}\n"
				    << "}\n"
				    << "struct VsConcurrentShutdownGuard final {\n"
				    << "\t~VsConcurrentShutdownGuard() noexcept {\n"
				    << "\t\ttry {\n"
				    << "\t\t\tVsShutdownConcurrent();\n"
				    << "\t\t\tbool HasTimedOutTasks = false;\n"
				    << "\t\t\t{\n"
				    << "\t\t\t\tstd::lock_guard<std::mutex> Lock(VsTaskMu());\n"
				    << "\t\t\t\tHasTimedOutTasks = !VsTimedOutTasks().empty();\n"
				    << "\t\t\t}\n"
				    << "\t\t\tif (HasTimedOutTasks) {\n"
				    << "\t\t\t\tstd::cerr << \"background task did not stop before shutdown\\n\";\n"
				    << "\t\t\t\tstd::cout.flush();\n"
				    << "\t\t\t\tstd::cerr.flush();\n"
				    << "\t\t\t\tstd::_Exit(EXIT_FAILURE);\n"
				    << "\t\t\t}\n"
				    << "\t\t}\n\t\tcatch (...)\n\t\t{\n\t\t\tstd::_Exit(EXIT_FAILURE);\n\t\t}\n"
				    << "\t}\n"
				    << "};\n\n";
			}
			if (Flags.bNeedsHandle)
			{
				Support
				    << "template <typename T>\n"
				    << "struct vs_SoftHandle {\n"
				    << "\tT value{};\n"
				    << "\tvs_SoftHandle() = default;\n"
				    << "\texplicit vs_SoftHandle(T InValue) : value(std::move(InValue)) {}\n"
				    << "};\n\n";
			}
			if (Flags.bNeedsFunctional)
			{
				Support
				    << "template <typename Signature>\n"
				    << "class vs_Function;\n\n"
				    << "template <typename R, typename... Args>\n"
				    << "class vs_Function<R(Args...)> {\n"
				    << "public:\n"
				    << "\tvs_Function() = default;\n"
				    << "\tvs_Function(std::nullptr_t) noexcept {}\n"
				    << "\tvs_Function(std::function<R(Args...)> fn)\n"
				    << "\t\t: handler_(std::make_shared<std::function<R(Args...)>>(std::move(fn))) {}\n"
				    << "\ttemplate <typename F, typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, vs_Function>\n"
				    << "\t\t&& !std::is_same_v<std::decay_t<F>, std::function<R(Args...)>>>>\n"
				    << "\tvs_Function(F&& fn)\n"
				    << "\t\t: vs_Function(std::function<R(Args...)>(std::forward<F>(fn))) {}\n"
				    << "\tvs_Function& operator=(std::nullptr_t) noexcept { handler_.reset(); return *this; }\n"
				    << "\ttemplate <typename F, typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, vs_Function>\n"
				    << "\t\t&& !std::is_same_v<std::decay_t<F>, std::function<R(Args...)>>>>\n"
				    << "\tvs_Function& operator=(F&& fn) { *this = vs_Function(std::forward<F>(fn)); return *this; }\n"
				    << "\texplicit operator bool() const noexcept { return handler_ && static_cast<bool>(*handler_); }\n"
				    << "\tR operator()(Args... args) const {\n"
				    << "\t\tconst auto active = handler_;\n"
				    << "\t\tif (!active || !*active) VsScriptFail(\"function is not bound\");\n"
				    << "\t\treturn (*active)(std::forward<Args>(args)...);\n"
				    << "\t}\n"
				    << "\tfriend bool operator==(const vs_Function& a, const vs_Function& b) noexcept {\n"
				    << "\t\tif (a.handler_ == b.handler_) return true;\n"
				    << "\t\tif (!a.handler_ || !b.handler_) return false;\n"
				    << "\t\tusing Pointer = R(*)(Args...);\n"
				    << "\t\tconst auto left = a.handler_->template target<Pointer>();\n"
				    << "\t\tconst auto right = b.handler_->template target<Pointer>();\n"
				    << "\t\treturn left && right && *left == *right;\n"
				    << "\t}\n"
				    << "\tfriend bool operator!=(const vs_Function& a, const vs_Function& b) noexcept { return !(a == b); }\n"
				    << "private:\n"
				    << "\tstd::shared_ptr<std::function<R(Args...)>> handler_;\n"
				    << "};\n\n";
			}
			#include "Internal/AttributeRuntime.inl"
			#include "Internal/LocalStaticRuntime.inl"
			if (Flags.bNeedsEvents)
			{
				Support
				    << "template <typename R>\n"
				    << "struct vs_Delegate0 {\n"
				    << "\tvs_Function<R()> handler;\n"
				    << "\tbool bound = false;\n"
				    << "\tvoid bind(vs_Function<R()> h) { handler = std::move(h); bound = true; }\n"
				    << "\tvoid unbind() { handler = vs_Function<R()>(); bound = false; }\n"
				    << "\tbool is_bound() const { return bound; }\n"
				    << "\tR execute() { if (!bound) VsScriptFail(\"delegate is not bound\"); return handler(); }\n"
				    << "\tbool execute_if_bound() { if (!bound) return false; (void)handler(); return true; }\n"
				    << "};\n\n"
				    << "template <typename R, typename A>\n"
				    << "struct vs_Delegate1 {\n"
				    << "\tvs_Function<R(A)> handler;\n"
				    << "\tbool bound = false;\n"
				    << "\tvoid bind(vs_Function<R(A)> h) { handler = std::move(h); bound = true; }\n"
				    << "\tvoid unbind() { handler = vs_Function<R(A)>(); bound = false; }\n"
				    << "\tbool is_bound() const { return bound; }\n"
				    << "\tR execute(A a) { if (!bound) VsScriptFail(\"delegate is not bound\"); return handler(a); }\n"
				    << "\tbool execute_if_bound(A a) { if (!bound) return false; (void)handler(a); return true; }\n"
				    << "};\n\n"
				    << "template <typename R, typename A, typename B>\n"
				    << "struct vs_Delegate2 {\n"
				    << "\tvs_Function<R(A, B)> handler;\n"
				    << "\tbool bound = false;\n"
				    << "\tvoid bind(vs_Function<R(A, B)> h) { handler = std::move(h); bound = true; }\n"
				    << "\tvoid unbind() { handler = vs_Function<R(A, B)>(); bound = false; }\n"
				    << "\tbool is_bound() const { return bound; }\n"
				    << "\tR execute(A a, B b) { if (!bound) VsScriptFail(\"delegate is not bound\"); return handler(a, b); }\n"
				    << "\tbool execute_if_bound(A a, B b) { if (!bound) return false; (void)handler(a, b); return true; }\n"
				    << "};\n\n"
				    << "template <typename R, typename A, typename B, typename C>\n"
				    << "struct vs_Delegate3 {\n"
				    << "\tvs_Function<R(A, B, C)> handler;\n"
				    << "\tbool bound = false;\n"
				    << "\tvoid bind(vs_Function<R(A, B, C)> h) { handler = std::move(h); bound = true; }\n"
				    << "\tvoid unbind() { handler = vs_Function<R(A, B, C)>(); bound = false; }\n"
				    << "\tbool is_bound() const { return bound; }\n"
				    << "\tR execute(A a, B b, C c) { if (!bound) VsScriptFail(\"delegate is not bound\"); return handler(a, b, c); }\n"
				    << "\tbool execute_if_bound(A a, B b, C c) { if (!bound) return false; (void)handler(a, b, c); return true; }\n"
				    << "};\n\n"
				    << "struct vs_MulticastDelegate0 {\n"
				    << "\tstd::vector<vs_Function<void()>> handlers;\n"
				    << "\tvoid add(vs_Function<void()> h) { handlers.push_back(std::move(h)); }\n"
				    << "\tvoid remove(const vs_Function<void()>& h) {\n"
				    << "\t\tfor (auto it = handlers.begin(); it != handlers.end(); ++it) {\n"
				    << "\t\t\tif (*it == h) { handlers.erase(it); break; }\n"
				    << "\t\t}\n"
				    << "\t}\n"
				    << "\tvoid broadcast() {\n"
				    << "\t\tstd::vector<vs_Function<void()>> Snapshot = handlers;\n"
				    << "\t\tfor (auto& h : Snapshot) { h(); }\n"
				    << "\t}\n"
				    << "\tstd::int64_t count() const { return VsCheckedLength(handlers.size()); }\n"
				    << "\tvoid clear() { handlers.clear(); }\n"
				    << "};\n\n"
				    << "template <typename A>\n"
				    << "struct vs_MulticastDelegate1 {\n"
				    << "\tstd::vector<vs_Function<void(A)>> handlers;\n"
				    << "\tvoid add(vs_Function<void(A)> h) { handlers.push_back(std::move(h)); }\n"
				    << "\tvoid remove(const vs_Function<void(A)>& h) {\n"
				    << "\t\tfor (auto it = handlers.begin(); it != handlers.end(); ++it) {\n"
				    << "\t\t\tif (*it == h) { handlers.erase(it); break; }\n"
				    << "\t\t}\n"
				    << "\t}\n"
				    << "\tvoid broadcast(A a) {\n"
				    << "\t\tstd::vector<vs_Function<void(A)>> Snapshot = handlers;\n"
				    << "\t\tfor (auto& h : Snapshot) { h(a); }\n"
				    << "\t}\n"
				    << "\tstd::int64_t count() const { return VsCheckedLength(handlers.size()); }\n"
				    << "\tvoid clear() { handlers.clear(); }\n"
				    << "};\n\n"
				    << "template <typename A, typename B>\n"
				    << "struct vs_MulticastDelegate2 {\n"
				    << "\tstd::vector<vs_Function<void(A, B)>> handlers;\n"
				    << "\tvoid add(vs_Function<void(A, B)> h) { handlers.push_back(std::move(h)); }\n"
				    << "\tvoid remove(const vs_Function<void(A, B)>& h) {\n"
				    << "\t\tfor (auto it = handlers.begin(); it != handlers.end(); ++it) {\n"
				    << "\t\t\tif (*it == h) { handlers.erase(it); break; }\n"
				    << "\t\t}\n"
				    << "\t}\n"
				    << "\tvoid broadcast(A a, B b) {\n"
				    << "\t\tstd::vector<vs_Function<void(A, B)>> Snapshot = handlers;\n"
				    << "\t\tfor (auto& h : Snapshot) { h(a, b); }\n"
				    << "\t}\n"
				    << "\tstd::int64_t count() const { return VsCheckedLength(handlers.size()); }\n"
				    << "\tvoid clear() { handlers.clear(); }\n"
				    << "};\n\n"
				    << "template <typename A, typename B, typename C>\n"
				    << "struct vs_MulticastDelegate3 {\n"
				    << "\tstd::vector<vs_Function<void(A, B, C)>> handlers;\n"
				    << "\tvoid add(vs_Function<void(A, B, C)> h) { handlers.push_back(std::move(h)); }\n"
				    << "\tvoid remove(const vs_Function<void(A, B, C)>& h) {\n"
				    << "\t\tfor (auto it = handlers.begin(); it != handlers.end(); ++it) {\n"
				    << "\t\t\tif (*it == h) { handlers.erase(it); break; }\n"
				    << "\t\t}\n"
				    << "\t}\n"
				    << "\tvoid broadcast(A a, B b, C c) {\n"
				    << "\t\tstd::vector<vs_Function<void(A, B, C)>> Snapshot = handlers;\n"
				    << "\t\tfor (auto& h : Snapshot) { h(a, b, c); }\n"
				    << "\t}\n"
				    << "\tstd::int64_t count() const { return VsCheckedLength(handlers.size()); }\n"
				    << "\tvoid clear() { handlers.clear(); }\n"
				    << "};\n\n";
			}
			Support
			    << "static inline void VsPrintTypeName(const char* TypeName) {\n"
			    << "\tstd::cout << TypeName << \"{...}\" << '\\n';\n"
			    << "}\n"
			    << "template <typename T, typename U>\n"
			    << "static inline T VsCheckedNumericCast(U Value) {\n"
			    << "\tstatic_assert(std::is_arithmetic<T>::value\n"
			    << "\t\t&& (std::is_arithmetic<U>::value || std::is_enum<U>::value),\n"
			    << "\t\t\"numeric cast requires arithmetic types\");\n"
			    << "\tif constexpr (std::is_enum<U>::value) {\n"
			    << "\t\treturn VsCheckedNumericCast<T>(static_cast<typename std::underlying_type<U>::type>(Value));\n"
			    << "\t}\n\telse if constexpr (std::is_floating_point<T>::value)\n\t{\n"
			    << "\t\treturn static_cast<T>(Value);\n"
			    << "\t}\n\telse if constexpr (std::is_floating_point<U>::value)\n\t{\n"
			    << "\t\tif (!std::isfinite(Value)) VsScriptFail(\"numeric conversion overflow\");\n"
			    << "\t\tconst long double Truncated = std::trunc(static_cast<long double>(Value));\n"
			    << "\t\tbool OutOfRange = false;\n"
			    << "\t\tif constexpr (std::is_unsigned<T>::value && sizeof(T) == sizeof(std::uint64_t)) {\n"
			    << "\t\t\tOutOfRange = Truncated < 0.0L || Truncated >= 18446744073709551616.0L;\n"
			    << "\t\t}\n\t\telse if constexpr (std::is_signed<T>::value && sizeof(T) == sizeof(std::int64_t))\n\t\t{\n"
			    << "\t\t\tOutOfRange = Truncated < -9223372036854775808.0L\n"
			    << "\t\t\t\t|| Truncated >= 9223372036854775808.0L;\n"
			    << "\t\t}\n\t\telse\n\t\t{\n"
			    << "\t\t\tOutOfRange = Truncated < static_cast<long double>((std::numeric_limits<T>::lowest)())\n"
			    << "\t\t\t\t|| Truncated > static_cast<long double>((std::numeric_limits<T>::max)());\n"
			    << "\t\t}\n"
			    << "\t\tif (OutOfRange) VsScriptFail(\"numeric conversion overflow\");\n"
			    << "\t\treturn static_cast<T>(Truncated);\n"
			    << "\t}\n\telse if constexpr (std::is_signed<U>::value)\n\t{\n"
			    << "\t\tif (Value < 0) {\n"
			    << "\t\t\tif constexpr (std::is_unsigned<T>::value) {\n"
			    << "\t\t\t\tVsScriptFail(\"numeric conversion overflow\");\n"
			    << "\t\t\t}\n\t\t\telse if (static_cast<std::int64_t>(Value)\n"
			    << "\t\t\t\t< static_cast<std::int64_t>((std::numeric_limits<T>::lowest)())) {\n"
			    << "\t\t\t\tVsScriptFail(\"numeric conversion overflow\");\n"
			    << "\t\t\t}\n"
			    << "\t\t}\n\t\telse if (static_cast<std::uint64_t>(Value)\n"
			    << "\t\t\t> static_cast<std::uint64_t>((std::numeric_limits<T>::max)())) {\n"
			    << "\t\t\tVsScriptFail(\"numeric conversion overflow\");\n"
			    << "\t\t}\n"
			    << "\t\treturn static_cast<T>(Value);\n"
			    << "\t}\n\telse\n\t{\n"
			    << "\t\tif (static_cast<std::uint64_t>(Value)\n"
			    << "\t\t\t> static_cast<std::uint64_t>((std::numeric_limits<T>::max)())) {\n"
			    << "\t\t\tVsScriptFail(\"numeric conversion overflow\");\n"
			    << "\t\t}\n"
			    << "\t\treturn static_cast<T>(Value);\n"
			    << "\t}\n"
			    << "}\n"
			    << "template <typename T, typename U>\n"
			    << "static inline bool VsTryNumericCast(U Value, T& Out) {\n"
			    << "\tstatic_assert(std::is_arithmetic<T>::value\n"
			    << "\t\t&& (std::is_arithmetic<U>::value || std::is_enum<U>::value),\n"
			    << "\t\t\"numeric cast requires arithmetic types\");\n"
			    << "\tif constexpr (std::is_enum<U>::value) {\n"
			    << "\t\treturn VsTryNumericCast<T>(static_cast<typename std::underlying_type<U>::type>(Value), Out);\n"
			    << "\t}\n\telse if constexpr (std::is_floating_point<T>::value)\n\t{\n"
			    << "\t\tOut = static_cast<T>(Value); return true;\n"
			    << "\t}\n\telse if constexpr (std::is_floating_point<U>::value)\n\t{\n"
			    << "\t\tif (!std::isfinite(Value)) return false;\n"
			    << "\t\tconst long double Truncated = std::trunc(static_cast<long double>(Value));\n"
			    << "\t\tbool OutOfRange = false;\n"
			    << "\t\tif constexpr (std::is_unsigned<T>::value && sizeof(T) == sizeof(std::uint64_t)) {\n"
			    << "\t\t\tOutOfRange = Truncated < 0.0L || Truncated >= 18446744073709551616.0L;\n"
			    << "\t\t}\n\t\telse if constexpr (std::is_signed<T>::value && sizeof(T) == sizeof(std::int64_t))\n\t\t{\n"
			    << "\t\t\tOutOfRange = Truncated < -9223372036854775808.0L\n"
			    << "\t\t\t\t|| Truncated >= 9223372036854775808.0L;\n"
			    << "\t\t}\n\t\telse\n\t\t{\n"
			    << "\t\t\tOutOfRange = Truncated < static_cast<long double>((std::numeric_limits<T>::lowest)())\n"
			    << "\t\t\t\t|| Truncated > static_cast<long double>((std::numeric_limits<T>::max)());\n"
			    << "\t\t}\n"
			    << "\t\tif (OutOfRange) return false;\n"
			    << "\t\tOut = static_cast<T>(Truncated); return true;\n"
			    << "\t}\n\telse if constexpr (std::is_signed<U>::value)\n\t{\n"
			    << "\t\tif (Value < 0) {\n"
			    << "\t\t\tif constexpr (std::is_unsigned<T>::value) { return false; }\n"
			    << "\t\t\telse if (static_cast<std::int64_t>(Value)\n"
			    << "\t\t\t\t< static_cast<std::int64_t>((std::numeric_limits<T>::lowest)())) { return false; }\n"
			    << "\t\t}\n\t\telse if (static_cast<std::uint64_t>(Value)\n"
			    << "\t\t\t> static_cast<std::uint64_t>((std::numeric_limits<T>::max)())) { return false; }\n"
			    << "\t\tOut = static_cast<T>(Value); return true;\n"
			    << "\t}\n\telse\n\t{\n"
			    << "\t\tif (static_cast<std::uint64_t>(Value)\n"
			    << "\t\t\t> static_cast<std::uint64_t>((std::numeric_limits<T>::max)())) { return false; }\n"
			    << "\t\tOut = static_cast<T>(Value); return true;\n"
			    << "\t}\n"
			    << "}\n"
			    << "template <typename T, typename U>\n"
			    << "static inline T VsCheckedShiftLeft(T Left, U Right) {\n"
			    << "\tstatic_assert(std::is_integral<T>::value && std::is_integral<U>::value,\n"
			    << "\t\t\"shift requires integer operands\");\n"
			    << "\tif constexpr (std::is_signed<U>::value) {\n"
			    << "\t\tif (Right < 0) VsScriptFail(\"shift count out of range\");\n"
			    << "\t}\n"
			    << "\tconst std::uint64_t Count = static_cast<std::uint64_t>(Right);\n"
			    << "\tif (Count >= sizeof(T) * 8u) VsScriptFail(\"shift count out of range\");\n"
			    << "\tusing UnsignedT = typename std::make_unsigned<T>::type;\n"
			    << "\treturn static_cast<T>(static_cast<UnsignedT>(Left) << static_cast<unsigned>(Count));\n"
			    << "}\n"
			    << "template <typename T, typename U>\n"
			    << "static inline T VsCheckedShiftRight(T Left, U Right) {\n"
			    << "\tstatic_assert(std::is_integral<T>::value && std::is_integral<U>::value,\n"
			    << "\t\t\"shift requires integer operands\");\n"
			    << "\tif constexpr (std::is_signed<U>::value) {\n"
			    << "\t\tif (Right < 0) VsScriptFail(\"shift count out of range\");\n"
			    << "\t}\n"
			    << "\tconst std::uint64_t Count = static_cast<std::uint64_t>(Right);\n"
			    << "\tif (Count >= sizeof(T) * 8u) VsScriptFail(\"shift count out of range\");\n"
			    << "\treturn static_cast<T>(Left >> static_cast<unsigned>(Count));\n"
			    << "}\n"
			    << "static inline int VsCheckedAdd(int Left, int Right) {\n"
			    << "\tconst long long Result = static_cast<long long>(Left) + static_cast<long long>(Right);\n"
			    << "\tif (Result < static_cast<long long>(std::numeric_limits<int>::min())\n"
			    << "\t\t|| Result > static_cast<long long>(std::numeric_limits<int>::max())) {\n"
			    << "\t\tstd::cerr << \"integer overflow\\n\";\n"
			    << "\t\tthrow VsExitException(1);\n"
			    << "\t}\n"
			    << "\treturn static_cast<int>(Result);\n"
			    << "}\n"
			    << "static inline int VsCheckedSub(int Left, int Right) {\n"
			    << "\tconst long long Result = static_cast<long long>(Left) - static_cast<long long>(Right);\n"
			    << "\tif (Result < static_cast<long long>(std::numeric_limits<int>::min())\n"
			    << "\t\t|| Result > static_cast<long long>(std::numeric_limits<int>::max())) {\n"
			    << "\t\tstd::cerr << \"integer overflow\\n\";\n"
			    << "\t\tthrow VsExitException(1);\n"
			    << "\t}\n"
			    << "\treturn static_cast<int>(Result);\n"
			    << "}\n"
			    << "static inline int VsCheckedMul(int Left, int Right) {\n"
			    << "\tconst long long Result = static_cast<long long>(Left) * static_cast<long long>(Right);\n"
			    << "\tif (Result < static_cast<long long>(std::numeric_limits<int>::min())\n"
			    << "\t\t|| Result > static_cast<long long>(std::numeric_limits<int>::max())) {\n"
			    << "\t\tstd::cerr << \"integer overflow\\n\";\n"
			    << "\t\tthrow VsExitException(1);\n"
			    << "\t}\n"
			    << "\treturn static_cast<int>(Result);\n"
			    << "}\n"
			    << "static inline std::int64_t VsCheckedAdd(std::int64_t Left, std::int64_t Right) {\n"
			    << "\tif ((Right > 0 && Left > std::numeric_limits<std::int64_t>::max() - Right)\n"
			    << "\t\t|| (Right < 0 && Left < std::numeric_limits<std::int64_t>::min() - Right)) {\n"
			    << "\t\tstd::cerr << \"integer overflow\\n\"; throw VsExitException(1);\n"
			    << "\t}\n"
			    << "\treturn Left + Right;\n"
			    << "}\n"
			    << "static inline std::int64_t VsCheckedSub(std::int64_t Left, std::int64_t Right) {\n"
			    << "\tif ((Right < 0 && Left > std::numeric_limits<std::int64_t>::max() + Right)\n"
			    << "\t\t|| (Right > 0 && Left < std::numeric_limits<std::int64_t>::min() + Right)) {\n"
			    << "\t\tstd::cerr << \"integer overflow\\n\"; throw VsExitException(1);\n"
			    << "\t}\n"
			    << "\treturn Left - Right;\n"
			    << "}\n"
			    << "static inline std::int64_t VsCheckedMul(std::int64_t Left, std::int64_t Right) {\n"
			    << "\tif (Left != 0 && Right != 0) {\n"
			    << "\t\tconst auto Min = std::numeric_limits<std::int64_t>::min();\n"
			    << "\t\tconst auto Max = std::numeric_limits<std::int64_t>::max();\n"
			    << "\t\tconst bool Overflow = (Left == -1 && Right == Min) || (Right == -1 && Left == Min)\n"
			    << "\t\t\t|| (Left > 0 && Right > 0 && Left > Max / Right)\n"
			    << "\t\t\t|| (Left > 0 && Right < 0 && Right < Min / Left)\n"
			    << "\t\t\t|| (Left < 0 && Right > 0 && Left < Min / Right)\n"
			    << "\t\t\t|| (Left < 0 && Right < 0 && Left < Max / Right);\n"
			    << "\t\tif (Overflow) { std::cerr << \"integer overflow\\n\"; throw VsExitException(1); }\n"
			    << "\t}\n"
			    << "\treturn Left * Right;\n"
			    << "}\n"
			    << "static inline int VsCheckedNeg(int Value) {\n"
			    << "\tif (Value == std::numeric_limits<int>::min()) {\n"
			    << "\t\tstd::cerr << \"integer overflow\\n\"; throw VsExitException(1);\n"
			    << "\t}\n"
			    << "\treturn -Value;\n"
			    << "}\n"
			    << "static inline std::int64_t VsCheckedNeg(std::int64_t Value) {\n"
			    << "\tif (Value == std::numeric_limits<std::int64_t>::min()) {\n"
			    << "\t\tstd::cerr << \"integer overflow\\n\"; throw VsExitException(1);\n"
			    << "\t}\n"
			    << "\treturn -Value;\n"
			    << "}\n"
			    << "static inline int VsCheckedDiv(int Left, int Right) {\n"
			    << "\tif (Right == 0) { std::cerr << \"division by zero\\n\"; throw VsExitException(1); }\n"
			    << "\tif (Left == std::numeric_limits<int>::min() && Right == -1) {\n"
			    << "\t\tstd::cerr << \"integer overflow\\n\"; throw VsExitException(1);\n"
			    << "\t}\n"
			    << "\treturn Left / Right;\n"
			    << "}\n"
			    << "static inline int VsCheckedMod(int Left, int Right) {\n"
			    << "\tif (Right == 0) { std::cerr << \"division by zero\\n\"; throw VsExitException(1); }\n"
			    << "\tif (Left == std::numeric_limits<int>::min() && Right == -1) {\n"
			    << "\t\tstd::cerr << \"integer overflow\\n\"; throw VsExitException(1);\n"
			    << "\t}\n"
			    << "\treturn Left % Right;\n"
			    << "}\n"
			    << "static inline std::int64_t VsCheckedDiv(std::int64_t Left, std::int64_t Right) {\n"
			    << "\tif (Right == 0) { std::cerr << \"division by zero\\n\"; throw VsExitException(1); }\n"
			    << "\tif (Left == std::numeric_limits<std::int64_t>::min() && Right == -1) {\n"
			    << "\t\tstd::cerr << \"integer overflow\\n\"; throw VsExitException(1);\n"
			    << "\t}\n"
			    << "\treturn Left / Right;\n"
			    << "}\n"
			    << "static inline std::int64_t VsCheckedMod(std::int64_t Left, std::int64_t Right) {\n"
			    << "\tif (Right == 0) { std::cerr << \"division by zero\\n\"; throw VsExitException(1); }\n"
			    << "\tif (Left == std::numeric_limits<std::int64_t>::min() && Right == -1) {\n"
			    << "\t\tstd::cerr << \"integer overflow\\n\"; throw VsExitException(1);\n"
			    << "\t}\n"
			    << "\treturn Left % Right;\n"
			    << "}\n"
			    << "static inline std::uint32_t VsCheckedDiv(std::uint32_t Left, std::uint32_t Right) {\n"
			    << "\tif (Right == 0) { std::cerr << \"division by zero\\n\"; throw VsExitException(1); }\n"
			    << "\treturn Left / Right;\n"
			    << "}\n"
			    << "static inline std::uint32_t VsCheckedMod(std::uint32_t Left, std::uint32_t Right) {\n"
			    << "\tif (Right == 0) { std::cerr << \"division by zero\\n\"; throw VsExitException(1); }\n"
			    << "\treturn Left % Right;\n"
			    << "}\n"
			    << "static inline std::uint64_t VsCheckedDiv(std::uint64_t Left, std::uint64_t Right) {\n"
			    << "\tif (Right == 0) { std::cerr << \"division by zero\\n\"; throw VsExitException(1); }\n"
			    << "\treturn Left / Right;\n"
			    << "}\n"
			    << "static inline std::uint64_t VsCheckedMod(std::uint64_t Left, std::uint64_t Right) {\n"
			    << "\tif (Right == 0) { std::cerr << \"division by zero\\n\"; throw VsExitException(1); }\n"
			    << "\treturn Left % Right;\n"
			    << "}\n"
			    << "static inline double VsCheckedDiv(double Left, double Right) {\n"
			    << "\tif (Right == 0.0) { std::cerr << \"division by zero\\n\"; throw VsExitException(1); }\n"
			    << "\treturn Left / Right;\n"
			    << "}\n"
			    << "static inline float VsCheckedDiv(float Left, float Right) {\n"
			    << "\tif (Right == 0.0f) { std::cerr << \"division by zero\\n\"; throw VsExitException(1); }\n"
			    << "\treturn Left / Right;\n"
			    << "}\n"
			    << "template <bool Increment, bool Postfix, typename T>\n"
			    << "static inline T VsCheckedIncDec(T& Target) {\n"
			    << "\tstatic_assert(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>, \"AOT increment/decrement requires a scalar numeric type\");\n"
			    << "\tconst T Old = Target;\n"
			    << "\tif constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {\n"
			    << "\t\tusing Arithmetic = std::conditional_t<(sizeof(T) > sizeof(std::int32_t)), std::int64_t, int>;\n"
			    << "\t\tTarget = VsCheckedNumericCast<T>(Increment ? VsCheckedAdd(static_cast<Arithmetic>(Old), Arithmetic{1}) : VsCheckedSub(static_cast<Arithmetic>(Old), Arithmetic{1}));\n"
			    << "\t}\n\telse if constexpr (std::is_integral_v<T>)\n\t{\n"
			    << "\t\tusing Arithmetic = std::conditional_t<(sizeof(T) > sizeof(std::uint32_t)), std::uint64_t, std::uint32_t>;\n"
			    << "\t\tTarget = VsCheckedNumericCast<T>(Increment ? static_cast<Arithmetic>(Old) + Arithmetic{1} : static_cast<Arithmetic>(Old) - Arithmetic{1});\n"
			    << "\t}\n\telse\n\t{\n"
			    << "\t\tTarget = Increment ? Old + T{1} : Old - T{1};\n"
			    << "\t}\n"
			    << "\treturn Postfix ? Old : Target;\n"
			    << "}\n"
			    << "static inline std::string VsStr(const std::string& Value) { return Value; }\n"
			    << "static inline std::string VsStr(int Value) { return std::to_string(Value); }\n"
			    << "static inline std::string VsStr(std::uint32_t Value) { return std::to_string(Value); }\n"
			    << "static inline std::string VsStr(std::int64_t Value) { return std::to_string(Value); }\n"
			    << "static inline std::string VsStr(std::uint64_t Value) { return std::to_string(Value); }\n"
			    << "static inline std::string VsStr(double Value) {\n"
			    << "\tstd::ostringstream Stream;\n"
			    << "\tStream << std::setprecision(15) << Value;\n"
			    << "\treturn Stream.str();\n"
			    << "}\n"
			    << "static inline std::string VsStr(bool Value) { return Value ? \"true\" : \"false\"; }\n"
			    << "static inline void VsAppendUtf8(std::string& Out, std::uint32_t CodePoint) {\n"
			    << "\tif (CodePoint < 0x80u) {\n"
			    << "\t\tOut.push_back(static_cast<char>(CodePoint));\n"
			    << "\t}\n\telse if (CodePoint < 0x800u)\n\t{\n"
			    << "\t\tOut.push_back(static_cast<char>(0xC0u | (CodePoint >> 6)));\n"
			    << "\t\tOut.push_back(static_cast<char>(0x80u | (CodePoint & 0x3Fu)));\n"
			    << "\t}\n\telse if (CodePoint < 0x10000u)\n\t{\n"
			    << "\t\tOut.push_back(static_cast<char>(0xE0u | (CodePoint >> 12)));\n"
			    << "\t\tOut.push_back(static_cast<char>(0x80u | ((CodePoint >> 6) & 0x3Fu)));\n"
			    << "\t\tOut.push_back(static_cast<char>(0x80u | (CodePoint & 0x3Fu)));\n"
			    << "\t}\n\telse if (CodePoint <= 0x10FFFFu)\n\t{\n"
			    << "\t\tOut.push_back(static_cast<char>(0xF0u | (CodePoint >> 18)));\n"
			    << "\t\tOut.push_back(static_cast<char>(0x80u | ((CodePoint >> 12) & 0x3Fu)));\n"
			    << "\t\tOut.push_back(static_cast<char>(0x80u | ((CodePoint >> 6) & 0x3Fu)));\n"
			    << "\t\tOut.push_back(static_cast<char>(0x80u | (CodePoint & 0x3Fu)));\n"
			    << "\t}\n\telse\n\t{\n"
			    << "\t\tVsScriptFail(\"invalid wide string code point\");\n"
			    << "\t}\n"
			    << "}\n"
			    << "static inline std::string VsStr(const std::wstring& Value) {\n"
			    << "\tstd::string Out;\n"
			    << "\tOut.reserve(Value.size());\n"
			    << "\tfor (std::size_t Index = 0; Index < Value.size(); ++Index) {\n"
			    << "\t\tstd::uint32_t CodePoint = static_cast<std::uint32_t>(Value[Index]);\n"
			    << "\t\tif (CodePoint >= 0xD800u && CodePoint <= 0xDBFFu && Index + 1 < Value.size()) {\n"
			    << "\t\t\tconst std::uint32_t Low = static_cast<std::uint32_t>(Value[Index + 1]);\n"
			    << "\t\t\tif (Low >= 0xDC00u && Low <= 0xDFFFu) {\n"
			    << "\t\t\t\tCodePoint = 0x10000u + ((CodePoint - 0xD800u) << 10) + (Low - 0xDC00u);\n"
			    << "\t\t\t\t++Index;\n"
			    << "\t\t\t}\n"
			    << "\t\t}\n"
			    << "\t\tVsAppendUtf8(Out, CodePoint);\n"
			    << "\t}\n"
			    << "\treturn Out;\n"
			    << "}\n"
			    << "static inline void VsPrint(const std::string& Value) { std::cout << Value << '\\n'; }\n"
			    << "static inline void VsPrint(int Value) { std::cout << Value << '\\n'; }\n"
			    << "static inline void VsPrint(std::uint32_t Value) { std::cout << Value << '\\n'; }\n"
			    << "static inline void VsPrint(std::int64_t Value) { std::cout << Value << '\\n'; }\n"
			    << "static inline void VsPrint(std::uint64_t Value) { std::cout << Value << '\\n'; }\n"
			    << "static inline void VsPrint(double Value) { std::cout << VsStr(Value) << '\\n'; }\n"
			    << "static inline void VsPrint(bool Value) { std::cout << (Value ? \"true\" : \"false\") << '\\n'; }\n"
			    << "static inline void VsPrint(const std::wstring& Value) { std::cout << VsStr(Value) << '\\n'; }\n";
			if (Flags.bNeedsConsole)
			{
				Support
				    << "template <typename T> static inline void VsConsoleWrite(const T& Value) { std::cout << VsStr(Value); }\n"
				    << "template <typename T> static inline void VsConsoleError(const T& Value) { std::cerr << VsStr(Value); }\n"
				    << "static inline void VsConsoleFlush() { std::cout.flush(); }\n"
				    << "static inline bool VsConsoleIsInteractive() { return VsConsoleRuntime::IsInputTerminal(std::cin) && VsConsoleRuntime::Query(std::cout).bOutputTerminal; }\n"
				    << "static inline bool VsConsoleClear() { return VsConsoleRuntime::Execute(std::cout, VsConsoleRuntime::EOperation::Clear); }\n";
			}

			Support
			    << "template <typename T> static inline int VsToInt(T Value) { return VsCheckedNumericCast<int>(Value); }\n"
			    << "template <typename T> static inline double VsToDouble(T Value) { return VsCheckedNumericCast<double>(Value); }\n"
			    << "template <typename T> static inline float VsToFloat(T Value) { return VsCheckedNumericCast<float>(Value); }\n"
			    << "template <typename T> static inline std::uint32_t VsToUnsigned(T Value) { return VsCheckedNumericCast<std::uint32_t>(Value); }\n"
			    << "template <typename T> static inline std::int64_t VsToInt64(T Value) { return VsCheckedNumericCast<std::int64_t>(Value); }\n"
			    << "template <typename T> static inline std::uint64_t VsToUInt64(T Value) { return VsCheckedNumericCast<std::uint64_t>(Value); }\n"
			    << "static inline std::string VsSubstr(const std::string& S, std::int64_t Start, std::int64_t Len) {\n"
			    << "\tif (Start < 0 || Len < 0 || VsCheckedNativeSize(Start) > S.size())\n"
			    << "\t\tVsScriptFail(\"substr start/count out of range\");\n"
			    << "\tconst std::size_t Max = S.size() - VsCheckedNativeSize(Start);\n"
			    << "\tconst std::size_t Use = static_cast<std::uint64_t>(Len) < Max\n"
			    << "\t\t? VsCheckedNativeSize(Len) : Max;\n"
			    << "\treturn S.substr(VsCheckedNativeSize(Start), Use);\n"
			    << "}\n"
			    << "static inline std::int64_t VsFind(const std::string& Haystack, const std::string& Needle) {\n"
			    << "\tif (Needle.empty()) return 0;\n"
			    << "\tconst std::size_t Pos = Haystack.find(Needle);\n"
			    << "\treturn Pos == std::string::npos ? std::int64_t{-1} : VsCheckedLength(Pos);\n"
			    << "}\n"
			    << "static inline int VsCharCode(const std::string& S, std::int64_t Index) {\n"
			    << "\tif (Index < 0 || VsCheckedNativeSize(Index) >= S.size())\n"
			    << "\t\tVsScriptFail(\"char index out of range\");\n"
			    << "\treturn static_cast<int>(static_cast<unsigned char>(\n"
			    << "\t\tS[static_cast<std::size_t>(Index)]));\n"
			    << "}\n";
			Support << R"VS(
template<class T> static inline void VsNumericType() { static_assert(std::is_arithmetic_v<T> && !std::is_same_v<T,bool>, "numeric type required"); }
static inline bool VsStringStarts(const std::string& s,const std::string& p) { return s.size()>=p.size() && s.compare(0,p.size(),p)==0; }
static inline bool VsStringEnds(const std::string& s,const std::string& p) { return s.size()>=p.size() && s.compare(s.size()-p.size(),p.size(),p)==0; }
static inline std::int64_t VsStringLastFind(const std::string& s,const std::string& p) { auto n=s.rfind(p); return n==std::string::npos?-1:VsCheckedLength(n); }
static inline std::string VsStringRepeat(const std::string& s,std::int64_t count) {
	if(count<0) VsScriptFail("repeat count must be non-negative");
	if(s.empty() || !count) return {};
	const auto n=VsCheckedNativeSize(count);
	if(n>std::string{}.max_size()/s.size() || n>static_cast<std::uint64_t>(INT64_MAX)/s.size()) VsScriptFail("repeat size overflow");
	std::string r; r.reserve(n*s.size()); for(std::size_t i=0;i<n;++i) r+=s; return r;
}
static inline std::string VsStringReplace(const std::string& s,const std::string& from,const std::string& to) {
	if(from.empty()) return s;
	std::string r; std::size_t start=0;
	for(;;) { auto n=s.find(from,start); if(n==std::string::npos) { r.append(s,start,std::string::npos); break; }
		r.append(s,start,n-start); r+=to; start=n+from.size(); }
	return r;
}
static inline std::string VsStringTrim(const std::string& s,bool left,bool right) {
	const auto space=[](char c){return c==' ' || (c>='\t' && c<='\r');};
	std::size_t begin=0,end=s.size(); while(left && begin<end && space(s[begin])) ++begin;
	while(right && end>begin && space(s[end-1])) --end; return s.substr(begin,end-begin);
}
)VS";
			if (Flags.bNeedsCctype)
			{
				Support
				    << "static inline std::string VsToLower(std::string S) {\n"
				    << "\tfor (char& Ch : S) {\n"
				    << "\t\tconst unsigned char Byte = static_cast<unsigned char>(Ch);\n"
				    << "\t\tif (Byte >= 'A' && Byte <= 'Z') Ch = static_cast<char>(Byte + ('a' - 'A'));\n"
				    << "\t}\n"
				    << "\treturn S;\n"
				    << "}\n"
				    << "static inline std::string VsToUpper(std::string S) {\n"
				    << "\tfor (char& Ch : S) {\n"
				    << "\t\tconst unsigned char Byte = static_cast<unsigned char>(Ch);\n"
				    << "\t\tif (Byte >= 'a' && Byte <= 'z') Ch = static_cast<char>(Byte - ('a' - 'A'));\n"
				    << "\t}\n"
				    << "\treturn S;\n"
				    << "}\n";
			}
			Support << R"VSCPP(
static inline bool VsTryParseSigned64(const std::string&, long long&);
static inline bool VsTryParseFiniteDouble(const std::string&, double&);
static inline std::string VsReadLine() {
	std::string Line;
	if (!std::getline(std::cin, Line)) {
		if (std::cin.eof() && !std::cin.bad()) return {};
		VsScriptFail("input read failed");
	}
	if (!Line.empty() && Line.back() == '\r') Line.pop_back();
	return Line;
}
static inline std::string VsReadToken() {
	const auto Space = [](int C) { return C == 32 || (C >= 9 && C <= 13); };
	const auto Peek = []() {
		const auto C = std::cin.peek();
		if (std::cin.bad() || (std::cin.fail() && !std::cin.eof())) VsScriptFail("input read failed");
		return C;
	};
	auto C = Peek();
	while (C != std::char_traits<char>::eof() && Space(C)) { std::cin.get(); C = Peek(); }
	std::string Token;
	while (C != std::char_traits<char>::eof() && !Space(C)) {
		Token += static_cast<char>(std::cin.get()); C = Peek();
	}
	if (Token.empty()) VsScriptFail("unexpected end of input");
	return Token;
}
static inline int VsReadInt() {
	long long Value = 0;
	if (!VsTryParseSigned64(VsReadToken(), Value) || Value < INT32_MIN || Value > INT32_MAX)
		VsScriptFail("invalid int token");
	return static_cast<int>(Value);
}
static inline double VsReadDouble() {
	double Value = 0;
	if (!VsTryParseFiniteDouble(VsReadToken(), Value)) VsScriptFail("invalid double token");
	return Value;
}
static inline bool VsReadBool() {
	const auto Token = VsReadToken();
	if (Token == "true") return true;
	if (Token == "false") return false;
	VsScriptFail("invalid bool token");
	return false;
}
)VSCPP";
			if (Flags.bNeedsAssert)
			{
				Support
				    << "static inline void VsAssert(bool Condition, const std::string& Message = {}) {\n"
				    << "\tif (Condition) return;\n"
				    << "\tif (Message.empty()) VsScriptFail(\"assertion failed\");\n"
				    << "\tconst std::string Detail = \"assertion failed: \" + Message;\n"
				    << "\tVsScriptFail(Detail.c_str());\n"
				    << "}\n";
			}
			else
			{
				Support
				    << "static inline void VsAssert(bool Condition, const std::string& Message = {}) { (void)Condition; (void)Message; }\n";
			}
			Support << R"VSCPP(
			static constexpr std::size_t VsMaxFormatWidth = 1024;

			static inline bool VsTryParseSigned64(const std::string& Text, long long& Out) {
				if (Text.empty()) return false;
				std::string_view Body(Text);
				if (Body.front() == '+') {
					Body.remove_prefix(1);
					if (Body.empty() || Body.front() == '+' || Body.front() == '-') return false;
				}
				const char* Begin = Body.data();
				const char* End = Begin + Body.size();
				const auto Parsed = std::from_chars(Begin, End, Out, 10);
				return Parsed.ec == std::errc{} && Parsed.ptr == End;
			}

			static inline bool VsTryParseUnsigned64(
			const std::string& Text, unsigned long long& Out) {
				if (Text.empty()) return false;
				std::string_view Body(Text);
				if (Body.front() == '+') {
					Body.remove_prefix(1);
					if (Body.empty() || Body.front() == '+' || Body.front() == '-') return false;
				}
				if (Body.front() == '-') return false;
				const char* Begin = Body.data();
				const char* End = Begin + Body.size();
				const auto Parsed = std::from_chars(Begin, End, Out, 10);
				return Parsed.ec == std::errc{} && Parsed.ptr == End;
			}

			static inline bool VsTryParseFiniteDouble(const std::string& Text, double& Out) {
				if (Text.empty()) return false;
				std::string_view Body(Text);
				if (Body.front() == '+') {
					Body.remove_prefix(1);
					if (Body.empty() || Body.front() == '+' || Body.front() == '-') return false;
				}
				const char* Begin = Body.data();
				const char* End = Begin + Body.size();
				const auto Parsed = VsFloatParsingDetail::FromCharsDecimal(Body, Out);
				return Parsed.ec == std::errc{} && Parsed.ptr == End && std::isfinite(Out);
			}

			static inline std::size_t VsParseFormatDecimal(
			const std::string& Text,
			std::size_t& Cursor,
			const char* Error) {
				if (Cursor >= Text.size() || Text[Cursor] < '0' || Text[Cursor] > '9')
				VsScriptFail(Error);
				std::size_t Value = 0;
				while (Cursor < Text.size() && Text[Cursor] >= '0' && Text[Cursor] <= '9') {
					const std::size_t Digit = static_cast<std::size_t>(Text[Cursor] - '0');
					if (Value > (VsMaxFormatWidth - Digit) / 10)
					VsScriptFail(Error);
					Value = Value * 10 + Digit;
					++Cursor;
				}
				return Value;
			}

			static inline std::string VsApplyBraceFormatSpec(
			std::string Value,
			const char Align,
			const std::size_t Width,
			const bool HasPrecision,
			const std::size_t Precision,
			const char TypeCode) {
				if (TypeCode == 'x' || TypeCode == 'X') {
					unsigned long long Bits = 0;
					if (!Value.empty() && Value.front() == '-') {
						long long Signed = 0;
						if (!VsTryParseSigned64(Value, Signed)) VsScriptFail("format: {:x} requires integer value");
						Bits = static_cast<unsigned long long>(Signed);
					}
					else if (!VsTryParseUnsigned64(Value, Bits)) VsScriptFail("format: {:x} requires integer value");
					std::ostringstream Stream;
					Stream << std::hex;
					if (TypeCode == 'X') Stream << std::uppercase;
					Stream << Bits;
					Value = Stream.str();
				}
				else if (HasPrecision)
				{
					double Parsed = 0.0;
					if (!VsTryParseFiniteDouble(Value, Parsed))
					VsScriptFail("format: precision requires numeric value");
					std::ostringstream Stream;
					Stream.setf(std::ios::fixed, std::ios::floatfield);
					Stream << std::setprecision(static_cast<int>(Precision)) << Parsed;
					Value = Stream.str();
				}
				if (Width <= Value.size()) return Value;
				const std::size_t Pad = Width - Value.size();
				if (Align == '<') {
					Value.append(Pad, ' ');
					return Value;
				}
				if (Align == '^') {
					const std::size_t Left = Pad / 2;
					const std::size_t Right = Pad - Left;
					return std::string(Left, ' ') + Value + std::string(Right, ' ');
				}
				return std::string(Pad, ' ') + Value;
			}

			static inline std::string VsFormatBraceStrings(
			const std::string& Fmt, const std::vector<std::string>& Args) {
				std::string Out;
				Out.reserve(Fmt.size());
				std::size_t AutoIndex = 0;
				for (std::size_t Index = 0; Index < Fmt.size();) {
					const char Ch = Fmt[Index];
					if (Ch == '{') {
						if (Index + 1 >= Fmt.size()) VsScriptFail("format: trailing '{'");
						if (Fmt[Index + 1] == '{') {
							Out += '{';
							Index += 2;
							continue;
						}
						std::size_t Cursor = Index + 1;
						std::size_t ArgIndex = AutoIndex;
						bool ExplicitIndex = false;
						if (Cursor < Fmt.size() && Fmt[Cursor] >= '0' && Fmt[Cursor] <= '9') {
							ArgIndex = VsParseFormatDecimal(
							Fmt, Cursor, "format: argument index out of range");
							ExplicitIndex = true;
						}
						char Align = '>';
						std::size_t Width = 0;
						bool HasPrecision = false;
						std::size_t Precision = 0;
						char TypeCode = 0;
						if (Cursor < Fmt.size() && Fmt[Cursor] == ':') {
							++Cursor;
							if (Cursor < Fmt.size()
							&& (Fmt[Cursor] == '<' || Fmt[Cursor] == '>'
							|| Fmt[Cursor] == '^')) {
								Align = Fmt[Cursor++];
							}
							if (Cursor < Fmt.size() && Fmt[Cursor] >= '0' && Fmt[Cursor] <= '9')
							Width = VsParseFormatDecimal(
							Fmt, Cursor, "format: width out of range");
							if (Cursor < Fmt.size() && Fmt[Cursor] == '.') {
								++Cursor;
								Precision = VsParseFormatDecimal(
								Fmt, Cursor, "format: precision out of range");
								HasPrecision = true;
							}
							if (Cursor < Fmt.size() && (Fmt[Cursor] == 'x' || Fmt[Cursor] == 'X'))
							TypeCode = Fmt[Cursor++];
						}
						if (Cursor >= Fmt.size() || Fmt[Cursor] != '}')
						VsScriptFail("format: invalid placeholder");
						++Cursor;
						if (ArgIndex >= Args.size()) VsScriptFail("format: not enough arguments");
						Out += VsApplyBraceFormatSpec(
						Args[ArgIndex], Align, Width, HasPrecision, Precision, TypeCode);
						if (!ExplicitIndex) ++AutoIndex;
						Index = Cursor;
						continue;
					}
					if (Ch == '}') {
						if (Index + 1 < Fmt.size() && Fmt[Index + 1] == '}') {
							Out += '}';
							Index += 2;
							continue;
						}
						VsScriptFail("format: unmatched '}'");
					}
					Out += Ch;
					++Index;
				}
				return Out;
			}

			static inline void VsCollectFormatArgs(std::vector<std::string>&) {}
			template <typename T, typename... Rest>
			static inline void VsCollectFormatArgs(
			std::vector<std::string>& Out, const T& Value, Rest&&... Args) {
				Out.push_back(VsStr(Value));
				VsCollectFormatArgs(Out, std::forward<Rest>(Args)...);
			}
			template <typename... Rest>
			static inline std::string VsFormatBrace(const std::string& Fmt, Rest&&... Args) {
				std::vector<std::string> Values;
				Values.reserve(sizeof...(Args));
				VsCollectFormatArgs(Values, std::forward<Rest>(Args)...);
				return VsFormatBraceStrings(Fmt, Values);
			}
			template <typename... Rest>
			static inline void VsPrintFormat(const std::string& Fmt, Rest&&... Args) {
				VsPrint(VsFormatBrace(Fmt, std::forward<Rest>(Args)...));
			}
			)VSCPP";
			Support << R"VSCPP(
			template <typename T>
			static inline std::string VsSnprintfOne(const std::string& Spec, T Value) {
				const int Count = std::snprintf(nullptr, 0, Spec.c_str(), Value);
				if (Count < 0) VsScriptFail("formatf: conversion failed");
				std::vector<char> Buffer(static_cast<std::size_t>(Count) + 1);
				if (std::snprintf(Buffer.data(), Buffer.size(), Spec.c_str(), Value) != Count)
				VsScriptFail("formatf: conversion failed");
				return std::string(Buffer.data(), static_cast<std::size_t>(Count));
			}
			static inline long long VsPrintfSigned(const std::string& Text) {
				long long Value = 0;
				if (!VsTryParseSigned64(Text, Value))
				VsScriptFail("formatf: expected integer value");
				return Value;
			}
			static inline unsigned long long VsPrintfUnsigned(const std::string& Text) {
				unsigned long long Value = 0;
				if (!VsTryParseUnsigned64(Text, Value))
				VsScriptFail("formatf: expected non-negative integer value");
				return Value;
			}
			static inline double VsPrintfDouble(const std::string& Text) {
				double Value = 0.0;
				if (!VsTryParseFiniteDouble(Text, Value))
				VsScriptFail("formatf: expected finite floating value");
				return Value;
			}
			static inline std::string VsFormatPrintfStrings(
			const std::string& Fmt, const std::vector<std::string>& Args) {
				std::string Out;
				Out.reserve(Fmt.size());
				std::size_t ArgIndex = 0;
				for (std::size_t Index = 0; Index < Fmt.size();) {
					if (Fmt[Index] != '%') { Out += Fmt[Index++]; continue; }
					if (Index + 1 >= Fmt.size()) VsScriptFail("formatf: trailing '%'");
					if (Fmt[Index + 1] == '%') { Out += '%'; Index += 2; continue; }
					std::size_t Cursor = Index + 1;
					std::string Flags;
					while (Cursor < Fmt.size()
					&& (Fmt[Cursor] == '-' || Fmt[Cursor] == '+' || Fmt[Cursor] == ' '
					|| Fmt[Cursor] == '#' || Fmt[Cursor] == '0')) {
						Flags += Fmt[Cursor++];
					}
					if (Cursor < Fmt.size() && Fmt[Cursor] == '*')
					VsScriptFail("formatf: dynamic width is not supported");
					std::string Width;
					while (Cursor < Fmt.size() && Fmt[Cursor] >= '0' && Fmt[Cursor] <= '9')
					Width += Fmt[Cursor++];
					if (!Width.empty()) {
						std::size_t WidthCursor = 0;
						(void)VsParseFormatDecimal(
						Width, WidthCursor, "formatf: width out of range");
					}
					std::string Precision;
					if (Cursor < Fmt.size() && Fmt[Cursor] == '.') {
						Precision += Fmt[Cursor++];
						if (Cursor < Fmt.size() && Fmt[Cursor] == '*')
						VsScriptFail("formatf: dynamic precision is not supported");
						const std::size_t DigitsBegin = Cursor;
						while (Cursor < Fmt.size() && Fmt[Cursor] >= '0' && Fmt[Cursor] <= '9')
						Precision += Fmt[Cursor++];
						if (Cursor == DigitsBegin)
						VsScriptFail("formatf: precision requires decimal digits");
						std::size_t PrecisionCursor = 1;
						(void)VsParseFormatDecimal(
						Precision, PrecisionCursor, "formatf: precision out of range");
					}
					if (Cursor < Fmt.size() && (Fmt[Cursor] == 'h' || Fmt[Cursor] == 'l')) {
						const char Modifier = Fmt[Cursor++];
						if (Cursor < Fmt.size() && Fmt[Cursor] == Modifier) ++Cursor;
					}
					else if (Cursor < Fmt.size()
					&& (Fmt[Cursor] == 'j' || Fmt[Cursor] == 'z' || Fmt[Cursor] == 't'))
					{
						++Cursor;
					}
					if (Cursor >= Fmt.size()) VsScriptFail("formatf: incomplete conversion");
					const char Conversion = Fmt[Cursor++];
					if (std::string("diuoxXfFeEgGcs").find(Conversion) == std::string::npos)
					VsScriptFail("formatf: unsupported conversion");
					if ((Conversion == 'c' || Conversion == 's')
					&& Flags.find_first_of("+ #0") != std::string::npos) {
						VsScriptFail("formatf: only '-' and width are valid flags for %c/%s");
					}
					if (Conversion == 'c' && !Precision.empty())
					VsScriptFail("formatf: precision is not valid for %c");
					if (ArgIndex >= Args.size()) VsScriptFail("formatf: not enough arguments");
					const std::string& Arg = Args[ArgIndex++];
					std::string Spec = "%" + Flags + Width + Precision;
					if (Conversion == 'd' || Conversion == 'i') {
						Spec += "ll"; Spec += Conversion;
						Out += VsSnprintfOne(Spec, VsPrintfSigned(Arg));
					}
					else if (Conversion == 'u' || Conversion == 'o'
					|| Conversion == 'x' || Conversion == 'X')
					{
						Spec += "ll"; Spec += Conversion;
						Out += VsSnprintfOne(Spec, VsPrintfUnsigned(Arg));
					}
					else if (Conversion == 'f' || Conversion == 'F'
					|| Conversion == 'e' || Conversion == 'E'
					|| Conversion == 'g' || Conversion == 'G')
					{
						Spec += Conversion;
						Out += VsSnprintfOne(Spec, VsPrintfDouble(Arg));
					}
					else if (Conversion == 'c')
					{
						Spec += 'c';
						long long Parsed = 0;
						int Value = 0;
						if (VsTryParseSigned64(Arg, Parsed)) {
							if (Parsed < 0 || Parsed > 255)
							VsScriptFail("formatf: %c value outside byte range");
							Value = static_cast<int>(Parsed);
						}
						else
						{
							if (Arg.size() != 1)
							VsScriptFail("formatf: %c requires an integer or one-byte string");
							Value = static_cast<unsigned char>(Arg.front());
						}
						Out += VsSnprintfOne(Spec, Value);
					}
					else
					{
						Spec += 's';
						Out += VsSnprintfOne(Spec, Arg.c_str());
					}
					Index = Cursor;
				}
				return Out;
			}
			static inline void VsCollectPrintfArgs(std::vector<std::string>&) {}
			template <typename T, typename... Rest>
			static inline void VsCollectPrintfArgs(
			std::vector<std::string>& Out, const T& Value, Rest&&... Args) {
				Out.push_back(VsStr(Value));
				VsCollectPrintfArgs(Out, std::forward<Rest>(Args)...);
			}
			template <typename... Rest>
			static inline std::string VsFormatPrintf(const std::string& Fmt, Rest&&... Args) {
				std::vector<std::string> Values;
				Values.reserve(sizeof...(Args));
				VsCollectPrintfArgs(Values, std::forward<Rest>(Args)...);
				return VsFormatPrintfStrings(Fmt, Values);
			}
			template <typename... Rest>
			static inline void VsPrintfFormat(const std::string& Fmt, Rest&&... Args) {
				VsPrint(VsFormatPrintf(Fmt, std::forward<Rest>(Args)...));
			}
			)VSCPP";
			if (Flags.bNeedsVector)
			{
				Support
				    << "template <typename T>\n"
				    << "static inline void VsPrint(const vs_Array<T>& Value) {\n"
				    << "\tstd::cout << \"array<?>\" << '[' << Value.size() << ']' << '\\n';\n"
				    << "}\n"
				    << "template <typename T>\n"
				    << "static inline std::string VsStr(const vs_Array<T>& Value) {\n"
				    << "\treturn std::string(\"array<?>[\" ) + std::to_string(Value.size()) + \"]\";\n"
				    << "}\n"
				    << "static inline vs_Array<int> VsRange(std::int64_t Begin, std::int64_t End, std::int64_t Step) {\n"
				    << "\tvs_Array<int> Out;\n"
				    << "\tif (End > 1000000) VsScriptFail(\"range count too large (max 1000000)\");\n"
				    << "\tif (Step == 0) return Out;\n"
				    << "\tif (Step > 0) { for (int I = Begin; I < End; I += Step) Out.push_back(I); }\n"
				    << "\telse\n\t{\n\t\tfor (int I = Begin; I > End; I += Step) Out.push_back(I);\n\t}\n"
				    << "\treturn Out;\n"
				    << "}\n"
				    << "static inline vs_Array<std::string> VsSplit(\n"
				    << "\tconst std::string& Text, const std::string& Separator) {\n"
				    << "\tvs_Array<std::string> Out;\n"
				    << "\tif (Separator.empty()) { Out.push_back(Text); return Out; }\n"
				    << "\tstd::size_t Start = 0;\n"
				    << "\twhile (true) {\n"
				    << "\t\tconst std::size_t Found = Text.find(Separator, Start);\n"
				    << "\t\tif (Found == std::string::npos) {\n"
				    << "\t\t\tOut.push_back(Text.substr(Start));\n"
				    << "\t\t\tbreak;\n"
				    << "\t\t}\n"
				    << "\t\tOut.push_back(Text.substr(Start, Found - Start));\n"
				    << "\t\tStart = Found + Separator.size();\n"
				    << "\t}\n"
				    << "\treturn Out;\n"
				    << "}\n"
				    << "static inline std::string VsJoin(\n"
				    << "\tconst vs_Array<std::string>& Parts, const std::string& Separator) {\n"
				    << "\tstd::string Result;\n"
				    << "\tfor (std::size_t Index = 0; Index < Parts.size(); ++Index) {\n"
				    << "\t\tif (Index > 0) Result += Separator;\n"
				    << "\t\tResult += Parts[Index];\n"
				    << "\t}\n"
				    << "\treturn Result;\n"
				    << "}\n"
				    << "static inline std::string VsFormatArray(\n"
				    << "\tconst std::string& Fmt, const vs_Array<std::string>& Args) {\n"
				    << "\treturn VsFormatBraceStrings(\n"
				    << "\t\tFmt, std::vector<std::string>(Args.begin(), Args.end()));\n"
				    << "}\n"
				    << "static inline std::string VsFormatPrintfArray(\n"
				    << "\tconst std::string& Fmt, const vs_Array<std::string>& Args) {\n"
				    << "\treturn VsFormatPrintfStrings(\n"
				    << "\t\tFmt, std::vector<std::string>(Args.begin(), Args.end()));\n"
				    << "}\n";
			}
			if (Flags.bNeedsFstream || Flags.bNeedsFilesystem)
			{
				// Match Core's public DOS/UNC spelling while preserving names that
				// require the verbatim namespace (reserved devices, trailing dots, etc.).
				Support << R"VSCPP(#if defined(_WIN32)
static inline void VsNormalizeWindowsPublicSpelling(std::string& Path) {
	if (Path.compare(0, 4u, "//?/") != 0) return;
	const bool Drive = Path.size() >= 7u &&
		((Path[4] >= 'A' && Path[4] <= 'Z') || (Path[4] >= 'a' && Path[4] <= 'z')) &&
		Path[5] == ':' && Path[6] == '/';
	const bool Unc = Path.size() > 8u &&
		(Path[4] == 'U' || Path[4] == 'u') && (Path[5] == 'N' || Path[5] == 'n') &&
		(Path[6] == 'C' || Path[6] == 'c') && Path[7] == '/';
	if (!Drive && !Unc) return;
	std::size_t Begin = Drive ? 7u : 8u;
	if (Unc) {
		const auto ServerEnd = Path.find('/', Begin);
		if (ServerEnd == std::string::npos || ServerEnd == Begin ||
			ServerEnd + 1u >= Path.size() || Path[ServerEnd + 1u] == '/') return;
		const auto ShareEnd = Path.find('/', ServerEnd + 1u);
		const auto LastShare = ShareEnd == std::string::npos ? Path.size() - 1u : ShareEnd - 1u;
		if (Path[ServerEnd - 1u] == '.' || Path[ServerEnd - 1u] == ' ' ||
			Path[LastShare] == '.' || Path[LastShare] == ' ') return;
		Begin = ShareEnd == std::string::npos ? Path.size() : ShareEnd + 1u;
	}
	for (std::size_t Start = Begin; Start < Path.size();) {
		const auto End = Path.find('/', Start);
		const auto Component = std::string_view(Path).substr(Start,
			End == std::string::npos ? End : End - Start);
		if (!Component.empty()) {
			if (Component.back() == '.' || Component.back() == ' ' ||
				Component.find_first_of(":*?\"<>|") != std::string_view::npos) return;
			const auto Base = Component.substr(0, Component.find('.'));
			if (!Base.empty() && Base.back() == ' ') return;
			const auto IsName = [&](const std::string_view Name) {
				if (Base.size() != Name.size()) return false;
				for (std::size_t I = 0; I < Base.size(); ++I) {
					const auto C = Base[I];
					if ((C >= 'a' && C <= 'z' ? static_cast<char>(C - 'a' + 'A') : C) != Name[I]) return false;
				}
				return true;
			};
			if (IsName("CON") || IsName("PRN") || IsName("AUX") || IsName("NUL") ||
				IsName("CONIN$") || IsName("CONOUT$")) return;
			if (Base.size() >= 4u) {
				const auto Number = Base.substr(3u);
				const auto IsPort = [&](const std::string_view Prefix) {
					for (std::size_t I = 0; I < 3u; ++I) {
						const auto C = Base[I];
						if ((C >= 'a' && C <= 'z' ? static_cast<char>(C - 'a' + 'A') : C) != Prefix[I]) return false;
					}
					return true;
				};
				if ((IsPort("COM") || IsPort("LPT")) &&
					((Number.size() == 1u && Number[0] >= '0' && Number[0] <= '9') ||
					Number == "\xc2\xb9" || Number == "\xc2\xb2" || Number == "\xc2\xb3")) return;
			}
		}
		if (End == std::string::npos) break;
		Start = End + 1u;
	}
	if (Drive) Path.erase(0, 4u);
	else Path.replace(0, 8u, "//");
}
#endif
)VSCPP";
				Support
				    << "static inline std::filesystem::path VsPathFromUtf8(const std::string& Text) {\n"
				    << "\tif (Text.find('\\0') != std::string::npos) VsScriptFail(\"file path cannot contain NUL characters\");\n"
				    << "#if defined(_WIN32) && defined(__cpp_char8_t) && __cpp_char8_t >= 201811L\n"
				    << "\tstd::u8string Bytes; Bytes.reserve(Text.size());\n"
				    << "\tfor (const unsigned char Byte : Text) Bytes.push_back(static_cast<char8_t>(Byte));\n"
				    << "\tauto Result = std::filesystem::path(Bytes);\n"
				    << "#elif defined(_WIN32)\n"
				    << "\tauto Result = std::filesystem::u8path(Text);\n"
				    << "#else\n"
				    << "\treturn std::filesystem::path(Text);\n"
				    << "#endif\n"
				    << "#if defined(_WIN32)\n"
				    << "\tResult.make_preferred();\n"
				    << "\tconst auto& Native = Result.native();\n"
				    << "\tif (Result.is_absolute() && Native.size() >= 248 && Native.rfind(L\"\\\\\\\\?\\\\\", 0) != 0) {\n"
				    << "\t\tif (Native.rfind(L\"\\\\\\\\\", 0) == 0) return std::filesystem::path(L\"\\\\\\\\?\\\\UNC\\\\\" + Native.substr(2));\n"
				    << "\t\treturn std::filesystem::path(L\"\\\\\\\\?\\\\\" + Native);\n"
				    << "\t}\n"
				    << "\treturn Result;\n"
				    << "#endif\n"
				    << "}\n"
				    << "static inline std::string VsPathToUtf8(const std::filesystem::path& Path) {\n"
				    << "#if defined(_WIN32)\n"
				    << "\tconst auto Bytes = Path.generic_u8string();\n"
				    << "\tstd::string Result; Result.reserve(Bytes.size());\n"
				    << "\tfor (const auto Byte : Bytes) Result.push_back(static_cast<char>(Byte));\n"
				    << "\tVsNormalizeWindowsPublicSpelling(Result);\n"
				    << "\treturn Result;\n"
				    << "#else\n"
				    << "\treturn Path.generic_string();\n"
				    << "#endif\n"
				    << "}\n";
			}
			if (Flags.bNeedsFstream)
			{
				Support
				    << "static inline std::string VsReadFile(const std::string& Path) {\n"
				    << "\tconst auto NativePath = VsPathFromUtf8(Path); std::error_code Ec;\n"
				    << "\tif (std::filesystem::is_directory(NativePath, Ec) || Ec) VsScriptFail(\"cannot read file\");\n"
				    << "\tstd::ifstream In(NativePath, std::ios::binary);\n"
				    << "\tif (!In) VsScriptFail(\"cannot read file\");\n"
				    << "\tstd::string Text; char Chunk[8192];\n"
				    << "\twhile (In.read(Chunk, sizeof(Chunk)) || In.gcount() > 0) Text.append(Chunk, static_cast<std::size_t>(In.gcount()));\n"
				    << "\tif (In.bad() || !In.eof()) VsScriptFail(\"cannot read file\");\n"
				    << "\treturn Text;\n"
				    << "}\n"
				    << "static inline void VsWriteFile(\n"
				    << "\tconst std::string& Path, const std::string& Content) {\n"
				    << "\tstd::ofstream Out(VsPathFromUtf8(Path), std::ios::binary);\n"
				    << "\tif (!Out) VsScriptFail(\"cannot write file\");\n"
				    << "\tOut << Content; Out.flush();\n"
				    << "\tif (!Out) VsScriptFail(\"cannot write file\");\n"
				    << "}\n"
				    << "static inline void VsAppendFile(\n"
				    << "\tconst std::string& Path, const std::string& Content) {\n"
				    << "\tstd::ofstream Out(VsPathFromUtf8(Path), std::ios::binary | std::ios::app);\n"
				    << "\tif (!Out) VsScriptFail(\"cannot append file\");\n"
				    << "\tOut << Content; Out.flush();\n"
				    << "\tif (!Out) VsScriptFail(\"cannot append file\");\n"
				    << "}\n";
			}
			if (Flags.bNeedsFilesystem)
			{
				Support
				    << "static inline bool VsExists(const std::string& Path) {\n"
				    << "\tstd::error_code Ec;\n"
				    << "\tconst bool Found = std::filesystem::exists(VsPathFromUtf8(Path), Ec);\n"
				    << "\tif (Ec) VsScriptFail(\"cannot inspect path\");\n"
				    << "\treturn Found;\n"
				    << "}\n"
				    << "static inline bool VsIsFile(const std::string& Path) {\n"
				    << "\tstd::error_code Ec;\n"
				    << "\tconst auto Status = std::filesystem::status(VsPathFromUtf8(Path), Ec);\n"
				    << "\tif (Ec && Ec != std::errc::no_such_file_or_directory && Ec != std::errc::not_a_directory) VsScriptFail(\"cannot inspect path\");\n"
				    << "\treturn !Ec && std::filesystem::is_regular_file(Status);\n"
				    << "}\n"
				    << "static inline bool VsIsDirectory(const std::string& Path) {\n"
				    << "\tstd::error_code Ec;\n"
				    << "\tconst auto Status = std::filesystem::status(VsPathFromUtf8(Path), Ec);\n"
				    << "\tif (Ec && Ec != std::errc::no_such_file_or_directory && Ec != std::errc::not_a_directory) VsScriptFail(\"cannot inspect path\");\n"
				    << "\treturn !Ec && std::filesystem::is_directory(Status);\n"
				    << "}\n"
				    << "static inline bool VsRemoveDir(const std::string& Path) {\n"
				    << "\tconst auto DirPath = VsPathFromUtf8(Path);\n"
				    << "\tstd::error_code StatusError;\n"
				    << "\tconst auto Status = std::filesystem::symlink_status(DirPath, StatusError);\n"
				    << "\tif (StatusError == std::errc::no_such_file_or_directory || StatusError == std::errc::not_a_directory || Status.type() == std::filesystem::file_type::not_found) return false;\n"
				    << "\tif (StatusError) VsScriptFail(\"cannot inspect directory\");\n"
				    << "\tif (!std::filesystem::is_directory(Status)) VsScriptFail(\"remove_dir requires a directory\");\n"
				    << "\tstd::error_code Ec;\n"
				    << "\tconst bool Removed = std::filesystem::remove(DirPath, Ec);\n"
				    << "\tif (Ec && Ec != std::errc::no_such_file_or_directory) VsScriptFail(\"cannot remove directory\");\n"
				    << "\treturn Removed;\n"
				    << "}\n"
				    << "static inline bool VsDeleteFile(const std::string& Path) {\n"
				    << "\tconst auto FilePath = VsPathFromUtf8(Path);\n"
				    << "\tstd::error_code StatusError;\n"
				    << "\tconst auto Status = std::filesystem::symlink_status(FilePath, StatusError);\n"
				    << "\tif (StatusError && StatusError != std::errc::no_such_file_or_directory) VsScriptFail(\"cannot inspect file\");\n"
				    << "\tif (std::filesystem::is_directory(Status)) VsScriptFail(\"delete_file cannot delete a directory\");\n"
				    << "\tstd::error_code Ec;\n"
				    << "\tconst bool Removed = std::filesystem::remove(FilePath, Ec);\n"
				    << "\tif (Ec) VsScriptFail(\"cannot delete file\");\n"
				    << "\treturn Removed;\n"
				    << "}\n"
				    << "static inline void VsMkDir(const std::string& Path) {\n"
				    << "\tstd::error_code Ec; std::filesystem::create_directories(VsPathFromUtf8(Path), Ec);\n"
				    << "\tif (Ec) VsScriptFail(\"cannot create directory\");\n"
				    << "}\n"
				    << "static inline std::string VsCwd() {\n"
				    << "\tstd::error_code Ec;\n"
				    << "\tconst auto Current = std::filesystem::current_path(Ec);\n"
				    << "\tif (Ec) VsScriptFail(\"cannot get current directory\");\n"
				    << "\treturn VsPathToUtf8(Current);\n"
				    << "}\n"
				    << "static inline void VsChdir(const std::string& Path) {\n"
				    << "\tstd::error_code Ec; std::filesystem::current_path(VsPathFromUtf8(Path), Ec);\n"
				    << "\tif (Ec) VsScriptFail(\"cannot change directory\");\n"
				    << "}\n";
				if (Flags.bNeedsVector)
				{
					Support
					    << "static inline vs_Array<std::string> VsListDir(const std::string& Path) {\n"
					    << "\tvs_Array<std::string> Out;\n"
					    << "\tstd::error_code Ec;\n"
					    << "\tstd::filesystem::directory_iterator It(VsPathFromUtf8(Path), Ec), End;\n"
					    << "\twhile (!Ec && It != End) {\n"
					    << "\t\tOut.push_back(VsPathToUtf8(It->path().filename()));\n"
					    << "\t\tIt.increment(Ec);\n"
					    << "\t}\n"
					    << "\tif (Ec) VsScriptFail(\"cannot list directory\");\n"
					    << "\treturn Out;\n"
					    << "}\n";
				}
			}
			Support
			    << "static inline void VsEprint(const std::string& Value) { std::cerr << Value << '\\n'; }\n"
			    << "static inline void VsEprint(int Value) { std::cerr << Value << '\\n'; }\n"
			    << "static inline int VsAbs(int Value) {\n"
			    << "\tif (Value == std::numeric_limits<int>::min()) VsScriptFail(\"integer overflow\");\n"
			    << "\treturn Value < 0 ? -Value : Value;\n"
			    << "}\n"
			    << "static inline std::int64_t VsAbs(std::int64_t Value) {\n"
			    << "\tif (Value == std::numeric_limits<std::int64_t>::min()) VsScriptFail(\"integer overflow\");\n"
			    << "\treturn Value < 0 ? -Value : Value;\n"
			    << "}\n"
			    << "static inline std::uint32_t VsAbs(std::uint32_t Value) { return Value; }\n"
			    << "static inline std::uint64_t VsAbs(std::uint64_t Value) { return Value; }\n"
			    << "static inline double VsAbs(double Value) { return Value < 0 ? -Value : Value; }\n"
			    << "template <typename T>\n"
			    << "static inline T VsMin(T A, T B) { return (A < B) ? A : B; }\n"
			    << "template <typename T>\n"
			    << "static inline T VsMax(T A, T B) { return (A > B) ? A : B; }\n"
			    << "static inline std::wstring VsToWString(const std::string& Value) {\n"
			    << "\tstd::wstring Out;\n"
			    << "\tOut.reserve(Value.size());\n"
			    << "\tfor (std::size_t Index = 0; Index < Value.size();) {\n"
			    << "\t\tconst std::uint32_t Byte = static_cast<unsigned char>(Value[Index]);\n"
			    << "\t\tstd::uint32_t CodePoint = 0;\n"
			    << "\t\tstd::size_t Width = 0;\n"
			    << "\t\tif (Byte < 0x80u) {\n"
			    << "\t\t\tCodePoint = Byte; Width = 1;\n"
			    << "\t\t}\n\t\telse if ((Byte & 0xE0u) == 0xC0u && Index + 1 < Value.size())\n\t\t{\n"
			    << "\t\t\tconst std::uint32_t B1 = static_cast<unsigned char>(Value[Index + 1]);\n"
			    << "\t\t\tif ((B1 & 0xC0u) == 0x80u) {\n"
			    << "\t\t\t\tCodePoint = ((Byte & 0x1Fu) << 6) | (B1 & 0x3Fu);\n"
			    << "\t\t\t\tif (CodePoint >= 0x80u) Width = 2;\n"
			    << "\t\t\t}\n"
			    << "\t\t}\n\t\telse if ((Byte & 0xF0u) == 0xE0u && Index + 2 < Value.size())\n\t\t{\n"
			    << "\t\t\tconst std::uint32_t B1 = static_cast<unsigned char>(Value[Index + 1]);\n"
			    << "\t\t\tconst std::uint32_t B2 = static_cast<unsigned char>(Value[Index + 2]);\n"
			    << "\t\t\tif ((B1 & 0xC0u) == 0x80u && (B2 & 0xC0u) == 0x80u) {\n"
			    << "\t\t\t\tCodePoint = ((Byte & 0x0Fu) << 12) | ((B1 & 0x3Fu) << 6) | (B2 & 0x3Fu);\n"
			    << "\t\t\t\tif (CodePoint >= 0x800u && (CodePoint < 0xD800u || CodePoint > 0xDFFFu)) Width = 3;\n"
			    << "\t\t\t}\n"
			    << "\t\t}\n\t\telse if ((Byte & 0xF8u) == 0xF0u && Index + 3 < Value.size())\n\t\t{\n"
			    << "\t\t\tconst std::uint32_t B1 = static_cast<unsigned char>(Value[Index + 1]);\n"
			    << "\t\t\tconst std::uint32_t B2 = static_cast<unsigned char>(Value[Index + 2]);\n"
			    << "\t\t\tconst std::uint32_t B3 = static_cast<unsigned char>(Value[Index + 3]);\n"
			    << "\t\t\tif ((B1 & 0xC0u) == 0x80u && (B2 & 0xC0u) == 0x80u && (B3 & 0xC0u) == 0x80u) {\n"
			    << "\t\t\t\tCodePoint = ((Byte & 0x07u) << 18) | ((B1 & 0x3Fu) << 12)\n"
			    << "\t\t\t\t\t| ((B2 & 0x3Fu) << 6) | (B3 & 0x3Fu);\n"
			    << "\t\t\t\tif (CodePoint >= 0x10000u && CodePoint <= 0x10FFFFu) Width = 4;\n"
			    << "\t\t\t}\n"
			    << "\t\t}\n"
			    << "\t\tif (Width == 0) VsScriptFail(\"invalid UTF-8 sequence\");\n"
			    << "\t\tif (CodePoint < 0x10000u) {\n"
			    << "\t\t\tOut.push_back(static_cast<wchar_t>(CodePoint));\n"
			    << "\t\t}\n\t\telse\n\t\t{\n"
			    << "\t\t\tCodePoint -= 0x10000u;\n"
			    << "\t\t\tOut.push_back(static_cast<wchar_t>(0xD800u + (CodePoint >> 10)));\n"
			    << "\t\t\tOut.push_back(static_cast<wchar_t>(0xDC00u + (CodePoint & 0x3FFu)));\n"
			    << "\t\t}\n"
			    << "\t\tIndex += Width;\n"
			    << "\t}\n"
			    << "\treturn Out;\n"
			    << "}\n"
			    << "static inline std::wstring VsToWString(const std::wstring& Value) { return Value; }\n";
			if (Flags.bNeedsChrono || Flags.bNeedsThread)
			{
				Support
				    << "static inline double VsNowSeconds() {\n"
				    << "\tusing clock = std::chrono::system_clock;\n"
				    << "\treturn std::chrono::duration<double>(\n"
				    << "\t\tclock::now().time_since_epoch()).count();\n"
				    << "}\n"
				    << "static inline double VsSteadyNowSeconds() {\n"
				    << "\tusing clock = std::chrono::steady_clock;\n"
				    << "\treturn std::chrono::duration<double>(\n"
				    << "\t\tclock::now().time_since_epoch()).count();\n"
				    << "}\n";
			}
			if (Flags.bNeedsThread)
			{
				Support
				    << "static inline void VsSleepMs(int Ms) {\n"
				    << "\tif (Ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(Ms));\n"
				    << "}\n";
			}
			if (Flags.bNeedsCstdlib)
			{
				Support
				    << "static inline std::string VsGetenv(const std::string& Name) {\n"
				    << "\tif (Name.empty() || Name.find('=') != std::string::npos\n"
				    << "\t\t|| Name.find('\\0') != std::string::npos) return {};\n"
				    << "\tstd::lock_guard<std::mutex> Lock(VsEnvironmentMutex());\n"
				    << "#if defined(_WIN32)\n"
				    << "\tconst std::wstring WideName = VsToWString(Name);\n"
				    << "\tSetLastError(ERROR_SUCCESS);\n"
				    << "\tDWORD Capacity = GetEnvironmentVariableW(WideName.c_str(), nullptr, 0);\n"
				    << "\tif (Capacity == 0) return {};\n"
				    << "\tfor (int Attempt = 0; Attempt < 4; ++Attempt) {\n"
				    << "\t\tstd::wstring WideValue(static_cast<std::size_t>(Capacity), L'\\0');\n"
				    << "\t\tconst DWORD Written = GetEnvironmentVariableW(\n"
				    << "\t\t\tWideName.c_str(), WideValue.data(), Capacity);\n"
				    << "\t\tif (Written == 0) return {};\n"
				    << "\t\tif (Written < Capacity) {\n"
				    << "\t\t\tWideValue.resize(static_cast<std::size_t>(Written));\n"
				    << "\t\t\treturn VsStr(WideValue);\n"
				    << "\t\t}\n"
				    << "\t\tCapacity = Written;\n"
				    << "\t}\n"
				    << "\treturn {};\n"
				    << "#else\n"
				    << "\tconst char* V = std::getenv(Name.c_str());\n"
				    << "\treturn V ? std::string(V) : std::string();\n"
				    << "#endif\n"
				    << "}\n"
				    << "static inline void VsSetenv(const std::string& Name, const std::string& Value) {\n"
				    << "\tif (Name.empty() || Name.find('=') != std::string::npos\n"
				    << "\t\t|| Name.find('\\0') != std::string::npos\n"
				    << "\t\t|| Value.find('\\0') != std::string::npos) {\n"
				    << "\t\tVsScriptFail(\"invalid environment variable name or value\");\n"
				    << "\t}\n"
				    << "\tstd::lock_guard<std::mutex> Lock(VsEnvironmentMutex());\n"
				    << "#if defined(_WIN32)\n"
				    << "\tconst std::wstring WideName = VsToWString(Name);\n"
				    << "\tconst std::wstring WideValue = VsToWString(Value);\n"
				    << "\tconst BOOL Ok = SetEnvironmentVariableW(\n"
				    << "\t\tWideName.c_str(), WideValue.c_str());\n"
				    << "\tif (!Ok) {\n"
				    << "\t\tVsScriptFail(\"setenv failed\");\n"
				    << "\t}\n"
				    << "#else\n"
				    << "\tconst int Code = setenv(Name.c_str(), Value.c_str(), 1);\n"
				    << "\tif (Code != 0) VsScriptFail(\"setenv failed\");\n"
				    << "#endif\n"
				    << "}\n";
			}
			Support << '\n';
			return Support.str();
		}
	}

	// Input has completed both core preparation and codegen signature normalization.
	static bool EmitPreparedCppFromProgramWithBindings(
	    const FProgram& Program,
	    std::string& OutCpp,
	    std::string& OutError,
	    const IHostBindingCatalog* const Bindings,
	    std::string* const OutRuntimeSupport = nullptr,
	    std::string* const OutProgramSource = nullptr)
	{
		OutCpp.clear();
		OutError.clear();
		if (OutRuntimeSupport)
		{
			OutRuntimeSupport->clear();
		}
		if (OutProgramSource)
		{
			OutProgramSource->clear();
		}
		if (Bindings)
		{
			std::unordered_set<std::string> Names;
			for (const auto& F : Bindings->GetFunctions())
			{
				if (!Names.insert(F.Name).second)
				{
					OutError = "AOT: overloaded native free functions require native Core AOT: " + F.Name;
					return false;
				}
			}
			for (const auto& T : Bindings->GetTypes())
			{
				Names.clear();
				for (const auto& M : T.Members)
				{
					if (!M.bOperator && (M.Kind == EHostMemberKind::Method || M.Kind == EHostMemberKind::StaticMethod) && !Names.insert(M.Name).second)
					{
						OutError = "AOT: overloaded named host methods require native Core AOT: " + T.Name + "." + M.Name;
						return false;
					}
				}
			}
		}

		if (Program.Functions.empty())
		{
			OutError = "AOT: no functions to emit";
			return false;
		}
		std::unordered_set<std::string> AbstractTypeNames;
		std::vector<FMethodBindingCodegenInfo> MethodBindings;
		std::unique_ptr<FBytecodeModule> CheckedModule;
		if (!CompileCheck(Program, OutError, Bindings, AbstractTypeNames, MethodBindings, CheckedModule))
		{
			return false;
		}
		if (RequiresCoreTemplateExport(Program))
		{
			OutError = "AOT: extended template applications require the Core AOT exporter";
			return false;
		}
		for (const auto& Function : CheckedModule->Functions)
		{
			for (std::size_t Ip = 0; Ip < Function.Code.size();)
			{
				const auto Op = static_cast<EOpcode>(Function.Code[Ip]);
				if (Op == EOpcode::LoadGlobal || Op == EOpcode::StoreGlobal || Op == EOpcode::MakeGlobalRef)
				{
					const auto Slot = Bytecode::ReadU16LE(Function.Code.data() + Ip + 1u);
					if (Slot < CheckedModule->GlobalMetadata.size() && CheckedModule->GlobalMetadata[Slot].bLocalStatic && std::none_of(Function.LocalStaticInitializers.begin(), Function.LocalStaticInitializers.end(), [&](const auto& Initializer)
					{
						return Initializer.GlobalSlot == Slot;
					}))
					{
						OutError = "AOT: references to enclosing local static storage require the Core AOT exporter";
						return false;
					}
				}
				Ip += Bytecode::InstructionSize(Op);
			}
		}
		auto ConstantProgram = CloneProgramWithConstantValues(Program, *CheckedModule, OutError);
		if (!ConstantProgram) return false;
		if (!ValidateAotEligibility(*ConstantProgram, OutError)) return false;
		if (Variant::UsesRuntime(*ConstantProgram))
		{
			OutError = "diag.aot_variant_unsupported: variant requires the VeroScript runtime; standalone output is unsupported";
			return false;
		}
		FCppEmitter Emitter;
		Emitter.Program = ConstantProgram.get();
		Emitter.MethodBindings = std::move(MethodBindings);
		Emitter.HostBindings = Bindings;
		Emitter.EntryModulePath = Program.EntryModulePath;
		Emitter.AbstractTypeNames = std::move(AbstractTypeNames);
		Emitter.IndexProgram();
		if (Emitter.bFailed)
		{
			OutError = Emitter.Error;
			return false;
		}
		Emitter.SelectGeneratedLocalNames();
		Emitter.RefreshEffectiveAbstractTypes();
		Emitter.CollectAllSpecializations();
		if (Emitter.bFailed)
		{
			OutError = Emitter.Error;
			return false;
		}
		// Specialization discovery may materialize additional generic user types.
		// Re-run the abstract-slot merge after that fixed point so instantiated
		// generic bases/interfaces cannot lose their pure-virtual requirements.
		Emitter.RefreshEffectiveAbstractTypes();
		Emitter.ValidateEmittedCppSignatures();
		if (Emitter.bFailed)
		{
			OutError = Emitter.Error;
			return false;
		}

		Emitter.EmitEnumsPreamble();
		if (Emitter.bFailed)
		{
			OutError = Emitter.Error;
			return false;
		}

		for (const std::unique_ptr<FStmt>& Top : Program.TopLevelStatements)
		{
			if (!Top)
			{
				continue;
			}
			if (Top->Kind != EStmtKind::TypeAlias && Top->Kind != EStmtKind::VarDecl)
			{
				OutError = "AOT: unsupported top-level statement";
				return false;
			}
		}

		// Forward-declare all concrete and open type layouts before any constructor
		// or free-function signature can mention them.
		for (const auto& Entry : Emitter.UserTypes)
		{
			const FUserTypeInfo& Type = Entry.second;
			if (!Type.OpenGenericName.empty() || (!Type.TypeParams.empty() && Type.MonoTypeArgs.empty()))
			{
				continue;
			}
			Emitter.Out << "struct " << Emitter.UserCppType(Type.Name) << ";\n";
		}
		for (const auto& Entry : Emitter.OpenGenericTypes)
		{
			Emitter.EmitOpenGenericForwardDecl(Entry.second);
		}
		Emitter.EmitAliasesPreamble();
		if (Emitter.bFailed)
		{
			OutError = Emitter.Error;
			return false;
		}
		const auto SnapshotUserTypeNames = [&]()
		{
			std::vector<std::string> Names;
			Names.reserve(Emitter.UserTypes.size());
			for (const auto& Entry : Emitter.UserTypes)
			{
				Names.push_back(Entry.first);
			}
			return Names;
		};
		const auto AppendNewTypeNames = [&](std::vector<std::string>& Names, std::size_t PreviousCount)
		{
			for (; PreviousCount < Emitter.TypeEmitOrder.size(); ++PreviousCount)
			{
				Names.push_back(Emitter.TypeEmitOrder[PreviousCount]);
			}
		};
		// Forward-declare free ctors before type structs: member methods may construct
		// the same type (e.g. `return Vec(x + n)` inside operator+).
		auto ConstructorTypeNames = SnapshotUserTypeNames();
		for (std::size_t Index = 0; Index < ConstructorTypeNames.size(); ++Index)
		{
			const std::string Name = ConstructorTypeNames[Index];
			const FUserTypeInfo& Type = Emitter.UserTypes.at(Name);
			const auto PreviousCount = Emitter.TypeEmitOrder.size();
			if (Type.bIsInterface)
			{
				continue;
			}
			if (!Type.TypeParams.empty() && Type.MonoTypeArgs.empty())
			{
				continue;
			}
			if (!Type.OpenGenericName.empty())
			{
				continue;
			}
			if (Type.Constructors->empty())
			{
				Emitter.EmitCtorInitSignature(Type.Name, Type, {}, true);
				if (!Type.bIsAbstract)
				{
					Emitter.EmitCtorSignature(Type.Name, {}, {}, true);
				}
			}
			else
			{
				for (const FConstructorDecl& Ctor : *Type.Constructors)
				{
					if (Ctor.bDeleted)
					{
						continue;
					}
					std::vector<std::string> ParamTypes;
					for (const FFunctionParam& Param : Ctor.Parameters)
					{
						ParamTypes.push_back(Param.TypeName);
					}
					Emitter.EmitCtorInitSignature(
					    Type.Name, Type, Ctor.Parameters, true);
					if (!Type.bIsAbstract)
					{
						Emitter.EmitCtorSignature(
						    Type.Name, ParamTypes, Ctor.Parameters, true);
					}
				}
			}
			if (Emitter.bFailed)
			{
				OutError = Emitter.Error;
				return false;
			}
			AppendNewTypeNames(ConstructorTypeNames, PreviousCount);
		}
		for (const auto& Entry : Emitter.OpenGenericTypes)
		{
			Emitter.EmitOpenGenericCtorPrototypes(Entry.second);
		}
		// Open constructor definitions are emitted with their type layouts below.
		// Their non-dependent free-function calls require these declarations now.
		for (const FFunctionDecl* Function : Emitter.ConcreteFunctions)
		{
			Emitter.EmitFunctionSignature(*Function, true);
		}
		for (const FSpecialization& Spec : Emitter.Specializations)
		{
			Emitter.EmitSpecializationSignature(Spec, true);
		}
		if (Emitter.bFailed)
		{
			OutError = Emitter.Error;
			return false;
		}
		if (!Emitter.UserTypes.empty())
		{
			Emitter.Out << '\n';
		}

		// User type structs (bases before derived).
		std::unordered_set<std::string> EmittedTypes;
		std::function<bool(const std::string&)> EmitTypeByName;
		EmitTypeByName = [&](const std::string& Name) -> bool
		{
			if (EmittedTypes.count(Name))
			{
				return true;
			}
			const auto It = Emitter.UserTypes.find(Name);
			if (It == Emitter.UserTypes.end())
			{
				return true;
			}
			// Type translation can register monomorphs and rehash UserTypes. Its
			// nodes remain stable, whereas map iterators and TypeEmitOrder do not.
			const FUserTypeInfo& Type = It->second;
			// Monomorphs / bare open names are covered by open template emission.
			if (!Type.OpenGenericName.empty() || (!Type.TypeParams.empty() && Type.MonoTypeArgs.empty()))
			{
				EmittedTypes.insert(Name);
				return true;
			}
			EmittedTypes.insert(Name);
			for (const std::string& Base : Type.BaseTypeNames)
			{
				if (!EmitTypeByName(Base))
				{
					return false;
				}
			}
			for (const std::string& Iface : Type.InterfaceTypeNames)
			{
				if (!EmitTypeByName(Iface))
				{
					return false;
				}
			}
			Emitter.EmitUserTypeStruct(Type);
			return !Emitter.bFailed;
		};

		// Concrete interfaces that do not depend on open generics first (open templates may
		// inherit them). Then open templates. Then remaining concrete interfaces that extend
		// open apps (e.g. `interface Extra : IBoxable<int>`).
		auto OpenBaseKey = [](const std::string& Raw) -> std::string
		{
			const std::string::size_type Lt = Raw.find('<');
			if (Lt != std::string::npos && !Raw.empty() && Raw.back() == '>')
			{
				return Raw.substr(0, Lt);
			}
			return Raw;
		};
		auto TypeDependsOnOpenGeneric = [&](const FUserTypeInfo& Type) -> bool
		{
			auto Consider = [&](const std::string& Raw) -> bool
			{
				if (Raw.empty())
				{
					return false;
				}
				return Emitter.OpenGenericTypes.find(OpenBaseKey(Raw)) != Emitter.OpenGenericTypes.end();
			};
			for (const std::string& Base : Type.BaseTypeNames)
			{
				if (Consider(Base))
				{
					return true;
				}
			}
			for (const std::string& Iface : Type.InterfaceTypeNames)
			{
				if (Consider(Iface))
				{
					return true;
				}
			}
			return false;
		};

		for (std::size_t Index = 0; Index < Emitter.TypeEmitOrder.size(); ++Index)
		{
			const std::string Name = Emitter.TypeEmitOrder[Index];
			const auto It = Emitter.UserTypes.find(Name);
			if (It == Emitter.UserTypes.end() || !It->second.bIsInterface || TypeDependsOnOpenGeneric(It->second))
			{
				continue;
			}
			if (!EmitTypeByName(Name))
			{
				OutError = Emitter.Error;
				return false;
			}
		}

		// Open generic types as C++ templates. Topo-order by open bases/interfaces so
		// `IChild<T> : IParent<T>` / `Derived<T> : Base<T>` never emit before parents.
		{
			auto CollectOpenDeps =
			    [&](const FOpenGenericType& Open, std::vector<std::string>& OutDeps)
			{
				OutDeps.clear();
				auto Consider = [&](const std::string& Raw)
				{
					if (Raw.empty())
					{
						return;
					}
					const std::string Key = OpenBaseKey(Raw);
					if (Key == Open.Name)
					{
						return;
					}
					if (Emitter.OpenGenericTypes.find(Key) != Emitter.OpenGenericTypes.end())
					{
						OutDeps.push_back(Key);
					}
				};
				for (const std::string& Base : Open.BaseTypeNames)
				{
					Consider(Base);
				}
				for (const std::string& Iface : Open.InterfaceTypeNames)
				{
					Consider(Iface);
				}
			};

			std::unordered_set<std::string> EmittedOpen;
			std::vector<std::string> Deps;
			while (EmittedOpen.size() < Emitter.OpenGenericTypes.size())
			{
				bool bProgress = false;
				// Prefer interfaces in each wave, then class/struct.
				for (const bool bIfacePass : {true, false})
				{
					for (const auto& Entry : Emitter.OpenGenericTypes)
					{
						if (EmittedOpen.count(Entry.first) || Entry.second.bIsInterface != bIfacePass)
						{
							continue;
						}
						CollectOpenDeps(Entry.second, Deps);
						bool bReady = true;
						for (const std::string& Base : Entry.second.BaseTypeNames)
						{
							const std::string Key = OpenBaseKey(Base);
							if (Emitter.OpenGenericTypes.find(Key) == Emitter.OpenGenericTypes.end())
							{
								if (!EmitTypeByName(Base))
								{
									OutError = Emitter.Error;
									return false;
								}
							}
						}
						for (const std::string& Iface : Entry.second.InterfaceTypeNames)
						{
							const std::string Key = OpenBaseKey(Iface);
							if (Emitter.OpenGenericTypes.find(Key) == Emitter.OpenGenericTypes.end())
							{
								if (!EmitTypeByName(Iface))
								{
									OutError = Emitter.Error;
									return false;
								}
							}
						}
						for (const std::string& Dep : Deps)
						{
							if (!EmittedOpen.count(Dep))
							{
								bReady = false;
								break;
							}
						}
						if (!bReady)
						{
							continue;
						}
						Emitter.EmitOpenGenericType(Entry.second);
						if (Emitter.bFailed)
						{
							OutError = Emitter.Error;
							return false;
						}
						EmittedOpen.insert(Entry.first);
						bProgress = true;
					}
				}
				if (!bProgress)
				{
					std::string Remaining;
					for (const auto& Entry : Emitter.OpenGenericTypes)
					{
						if (EmittedOpen.count(Entry.first))
						{
							continue;
						}
						if (!Remaining.empty())
						{
							Remaining += ", ";
						}
						Remaining += Entry.first;
					}
					OutError =
					    "AOT: cyclic or unresolved open-generic inheritance among: " + Remaining;
					return false;
				}
			}
		}

		for (std::size_t Index = 0; Index < Emitter.TypeEmitOrder.size(); ++Index)
		{
			const std::string Name = Emitter.TypeEmitOrder[Index];
			const auto It = Emitter.UserTypes.find(Name);
			if (It == Emitter.UserTypes.end() || !It->second.bIsInterface)
			{
				continue;
			}
			if (!EmitTypeByName(Name))
			{
				OutError = Emitter.Error;
				return false;
			}
		}

		std::size_t NextTypeIndex = 0;
		const auto EmitPendingTypes = [&]()
		{
			while (NextTypeIndex < Emitter.TypeEmitOrder.size())
			{
				const std::string Name = Emitter.TypeEmitOrder[NextTypeIndex++];
				if (!EmitTypeByName(Name)) return false;
			}
			return true;
		};
		if (!EmitPendingTypes())
		{
			OutError = Emitter.Error;
			return false;
		}
		for (const std::string& Name : SnapshotUserTypeNames())
		{
			if (!EmitTypeByName(Name))
			{
				OutError = Emitter.Error;
				return false;
			}
		}
		if (!EmitPendingTypes())
		{
			OutError = Emitter.Error;
			return false;
		}
		Emitter.EmitStructInterfaceBoxes();
		if (Emitter.bFailed)
		{
			OutError = Emitter.Error;
			return false;
		}

		// Prototypes: methods, ctors, concrete functions, specializations.
		auto PrototypeTypeNames = SnapshotUserTypeNames();
		for (std::size_t Index = 0; Index < PrototypeTypeNames.size(); ++Index)
		{
			const std::string Name = PrototypeTypeNames[Index];
			const FUserTypeInfo& Type = Emitter.UserTypes.at(Name);
			const auto PreviousCount = Emitter.TypeEmitOrder.size();
			if (Type.bIsInterface)
			{
				continue;
			}
			if (Type.bResolutionOnlyMonomorph)
			{
				continue;
			}
			// Bare open generic: members/ctors live in the template struct.
			if (!Type.TypeParams.empty() && Type.MonoTypeArgs.empty())
			{
				continue;
			}
			for (const FMethodDecl& Method : *Type.Methods)
			{
				Emitter.EmitMethodSignature(Type.Name, Method, true);
				if (Emitter.bFailed)
				{
					OutError = Emitter.Error;
					return false;
				}
			}
			if (Type.Constructors->empty())
			{
				Emitter.EmitCtorInitSignature(Type.Name, Type, {}, true);
				if (!Type.bIsAbstract)
				{
					Emitter.EmitCtorSignature(Type.Name, {}, {}, true);
				}
			}
			else
			{
				for (const FConstructorDecl& Ctor : *Type.Constructors)
				{
					if (Ctor.bDeleted)
					{
						continue;
					}
					std::vector<std::string> ParamTypes;
					for (const FFunctionParam& Param : Ctor.Parameters)
					{
						ParamTypes.push_back(Param.TypeName);
					}
					Emitter.EmitCtorInitSignature(
					    Type.Name, Type, Ctor.Parameters, true);
					if (!Type.bIsAbstract)
					{
						Emitter.EmitCtorSignature(
						    Type.Name, ParamTypes, Ctor.Parameters, true);
					}
				}
			}
			if (Emitter.bFailed)
			{
				OutError = Emitter.Error;
				return false;
			}
			AppendNewTypeNames(PrototypeTypeNames, PreviousCount);
		}
		for (const FFunctionDecl* Function : Emitter.ConcreteFunctions)
		{
			Emitter.EmitFunctionSignature(*Function, true);
			if (Emitter.bFailed)
			{
				OutError = Emitter.Error;
				return false;
			}
		}
		for (const FSpecialization& Spec : Emitter.Specializations)
		{
			Emitter.EmitSpecializationSignature(Spec, true);
			if (Emitter.bFailed)
			{
				OutError = Emitter.Error;
				return false;
			}
		}
		// All user layouts and callable prototypes are complete now. Declare every
		// global before defining any of them so initializers may refer forward without
		// instantiating wrappers such as optional<UserStruct> on an incomplete type.
		// Global storage is function-local TLS to avoid MSVC static-CRT dynamic TLS
		// initialization hazards. The declaration-order initialization barrier is
		// emitted below, after every accessor has been defined.
		Emitter.Out << "static void vs_ensure_globals();\n";
		for (const std::unique_ptr<FStmt>& Top : Program.TopLevelStatements)
		{
			if (!Top || Top->Kind != EStmtKind::VarDecl)
			{
				continue;
			}
			const FVarDeclStmt& Global = static_cast<const FVarDeclStmt&>(*Top);
			Emitter.ActiveModulePath = Emitter.GlobalModulePath(Global);
			Emitter.ActiveNamespace = FCppEmitter::NamespaceOfSymbol(Global.Name);
			Emitter.EmitGlobalDeclaration(Global);
		}
		// Member definitions need complete layouts and all global accessor
		// declarations, including non-dependent names in open templates.
		Emitter.Out << "template <typename T> inline void VsInitializeDefaultStorage(T&) {}\n";
		if (Emitter.bNeedsArray)
		{
			Emitter.Out
			    << "template <typename T, std::size_t N> inline void VsInitializeDefaultStorage(std::array<T, N>& Values) "
			       "{ for (auto& Value : Values) VsInitializeDefaultStorage(Value); }\n";
		}

		for (const auto& Definition : Emitter.DeferredTypeDefinitions)
		{
			Emitter.Out << Definition << '\n';
		}

		if (Emitter.bFailed)
		{
			OutError = Emitter.Error;
			return false;
		}
		for (const std::unique_ptr<FStmt>& Top : Program.TopLevelStatements)
		{
			if (!Top || Top->Kind != EStmtKind::VarDecl)
			{
				continue;
			}
			const FVarDeclStmt& Global = static_cast<const FVarDeclStmt&>(*Top);
			Emitter.LocalTypes.clear();
			Emitter.LocalConstness.clear();
			Emitter.TypeSubst.clear();
			Emitter.bInSelfContext = false;
			Emitter.bSelfIsConst = false;
			Emitter.SelfTypeName.clear();
			Emitter.ActiveFunctionName.clear();
			Emitter.ActiveModulePath = Emitter.GlobalModulePath(Global);
			Emitter.ActiveNamespace = FCppEmitter::NamespaceOfSymbol(Global.Name);
			Emitter.bEmittingTopLevelVariable = true;
			Emitter.EmitStmt(*Top, 0);
			Emitter.bEmittingTopLevelVariable = false;
			if (Emitter.bFailed)
			{
				OutError = Emitter.Error;
				return false;
			}
		}
		Emitter.Out << "static void vs_ensure_globals()\n"
		            << "{\n"
		            << "\tthread_local bool bInitialized = false;\n"
		            << "\tthread_local bool bInitializing = false;\n"
		            << "\tif (bInitialized || bInitializing) { return; }\n"
		            << "\tbInitializing = true;\n"
		            << "\ttry {\n";
		for (const std::unique_ptr<FStmt>& Top : Program.TopLevelStatements)
		{
			if (!Top || Top->Kind != EStmtKind::VarDecl)
			{
				continue;
			}
			const FVarDeclStmt& Global = static_cast<const FVarDeclStmt&>(*Top);
			Emitter.Out << "\t\t(void)" << Emitter.MangleGlobal(Global) << "();\n";
		}
		Emitter.Out << "\t\tbInitialized = true;\n"
		            << "\t}\n\tcatch (...)\n\t{\n"
		            << "\t\tbInitializing = false;\n"
		            << "\t\tthrow;\n"
		            << "\t}\n"
		            << "\tbInitializing = false;\n"
		            << "}\n\n";
		Emitter.Out << '\n';
		Emitter.EmitSpawnDispatchers();
		if (Emitter.bFailed)
		{
			OutError = Emitter.Error;
			return false;
		}

		// Definitions.
		auto DefinitionTypeNames = SnapshotUserTypeNames();
		for (std::size_t Index = 0; Index < DefinitionTypeNames.size(); ++Index)
		{
			const std::string Name = DefinitionTypeNames[Index];
			const FUserTypeInfo& Type = Emitter.UserTypes.at(Name);
			const auto PreviousCount = Emitter.TypeEmitOrder.size();
			if (Type.bIsInterface)
			{
				continue;
			}
			if (Type.bResolutionOnlyMonomorph)
			{
				continue;
			}
			if (!Type.TypeParams.empty() && Type.MonoTypeArgs.empty())
			{
				continue;
			}
			for (const FMethodDecl& Method : *Type.Methods)
			{
				Emitter.EmitMethod(Type.Name, Method);
				if (Emitter.bFailed)
				{
					OutError = Emitter.Error;
					return false;
				}
			}
			if (Type.Constructors->empty())
			{
				Emitter.EmitCtor(Type.Name, Type, nullptr);
			}
			else
			{
				for (const FConstructorDecl& Ctor : *Type.Constructors)
				{
					if (Ctor.bDeleted)
					{
						continue;
					}
					Emitter.EmitCtor(Type.Name, Type, &Ctor);
				}
			}
			if (Emitter.bFailed)
			{
				OutError = Emitter.Error;
				return false;
			}
			AppendNewTypeNames(DefinitionTypeNames, PreviousCount);
		}
		for (const FFunctionDecl* Function : Emitter.ConcreteFunctions)
		{
			Emitter.EmitFunction(*Function);
			if (Emitter.bFailed)
			{
				OutError = Emitter.Error;
				return false;
			}
		}
		for (const FSpecialization& Spec : Emitter.Specializations)
		{
			Emitter.EmitSpecialization(Spec);
			if (Emitter.bFailed)
			{
				OutError = Emitter.Error;
				return false;
			}
		}

		// Console validation and host callbacks can throw standard exceptions.
		// Handle them at the entry boundary so terminal guards always unwind.
		const char* ConsoleFailureHandlers = Emitter.bNeedsConsole
		    ? "\n\tcatch (const std::exception& Error)\n\t{\n"
		      "\t\ttry { std::cerr << Error.what() << '\\n'; }\n\t\tcatch (...)\n\t\t{\n\t\t}\n"
		      "\t\treturn 1;\n"
		      "\t}\n\tcatch (...)\n\t{\n"
		      "\t\ttry { std::cerr << \"native runtime exception\\n\"; }\n\t\tcatch (...)\n\t\t{\n\t\t}\n"
		      "\t\treturn 1;\n"
		      "\t}"
		    : "";
		if (!Emitter.MainArgsParamName.empty())
		{
			Emitter.Out << "int main(int argc, char** argv) {\n";
			Emitter.Out << "\ttry {\n";
			if (Emitter.bNeedsLocalStatics)
			{
				Emitter.Out << "\tVsLocalStaticScope LocalStatics;\n";
			}
			if (Emitter.bNeedsConsole)
			{
				Emitter.Out << "\tVsConsoleRuntime::Detail::FScopedRestore ConsoleRestore(std::cout);\n";
			}

			if (Emitter.bNeedsChannel || Emitter.bNeedsThread)
			{
				Emitter.Out << "\tVsConcurrentShutdownGuard ConcurrentShutdown;\n";
			}
			Emitter.Out
			    << "\tvs_ensure_globals();\n"
			    // Script parameter names live in the script function, not this wrapper.
			    // Use a private vector so argc/argv/i and guard names cannot be shadowed.
			    << "\tstd::vector<std::string> VsMainArgs;\n"
			    << "\tfor (int i = 1; i < argc; ++i) {\n"
			    << "\t\tVsMainArgs.emplace_back(argv[i] ? argv[i] : \"\");\n"
			    << "\t}\n"
			    << "\t";
			if (Emitter.bMainReturnsInt)
			{
				Emitter.Out << "const int ___veroscript_main_exit_code = ";
			}
			Emitter.Out << kScriptMainName << "(VsMainArgs);\n";
			Emitter.Out << "\treturn "
			            << (Emitter.bMainReturnsInt ? "___veroscript_main_exit_code" : "0")
			            << ";\n"
			            << "\t}\n\tcatch (const VsExitException& Exit)\n\t{\n"
			            << "\t\treturn Exit.Code;\n"
			            << "\t}" << ConsoleFailureHandlers << "\n"
			            << "}\n";
			Emitter.bNeedsVector = true;
		}
		else
		{
			Emitter.Out << "int main() {\n";
			Emitter.Out << "\ttry {\n";
			if (Emitter.bNeedsLocalStatics)
			{
				Emitter.Out << "\tVsLocalStaticScope LocalStatics;\n";
			}
			if (Emitter.bNeedsConsole)
			{
				Emitter.Out << "\tVsConsoleRuntime::Detail::FScopedRestore ConsoleRestore(std::cout);\n";
			}

			if (Emitter.bNeedsChannel || Emitter.bNeedsThread)
			{
				Emitter.Out << "\tVsConcurrentShutdownGuard ConcurrentShutdown;\n";
			}
			Emitter.Out
			    << "\tvs_ensure_globals();\n"
			    << "\t";
			if (Emitter.bMainReturnsInt)
			{
				Emitter.Out << "const int ___veroscript_main_exit_code = ";
			}
			Emitter.Out << kScriptMainName << "();\n";
			Emitter.Out << "\treturn "
			            << (Emitter.bMainReturnsInt ? "___veroscript_main_exit_code" : "0")
			            << ";\n"
			            << "\t}\n\tcatch (const VsExitException& Exit)\n\t{\n"
			            << "\t\treturn Exit.Code;\n"
			            << "\t}" << ConsoleFailureHandlers << "\n"
			            << "}\n";
		}

		std::ostringstream Header;
		Header << "// SPDX-FileCopyrightText: VanstySanic\n// SPDX-License-Identifier: MIT\n\n// Generated by VeroScriptCppAot " << FormatVersion(VEROSCRIPT_VERSION) << "\n"
		       << "// Open generics -> C++ templates; where T:I -> enable_if_t/is_convertible_v (C++17).\n"
		       << "// Concurrent: OS threads, thread-local script state, and vs_Channel sharing.\n"
		       << "#include <charconv>\n"
		       << "#include <cmath>\n"
		       << "#include <cstddef>\n"
		       << "#include <cstdint>\n"
		       << "#include <cstdio>\n"
		       << "#include <cstdlib>\n"
		       << "#include <exception>\n"
		       << "#include <iomanip>\n"
		       << "#include <iostream>\n"
		       << "#include <limits>\n"
		       << "#include <sstream>\n"
		       << "#include <string>\n"
		       << "#include <string_view>\n"
		       << "#include <system_error>\n"
		       << "#include <type_traits>\n"
		       << "#include <optional>\n"
		       << "#include <utility>\n"
		       << "#include <vector>\n";
		if (Emitter.bNeedsChannel || Emitter.bNeedsThread)
		{
			Emitter.bNeedsVector = true;
			Emitter.bNeedsChrono = true;
			Emitter.bNeedsCstdlib = true;
		}
		if (Emitter.bNeedsMap)
		{
			// vs_OrderedMap is part of map runtime support and uses vector/find_if.
			Emitter.bNeedsVector = true;
		}
		if (Emitter.bNeedsVector)
		{
			Emitter.bNeedsMemory = true;
		}
		// Utility support contains weak/shared ownership and the exception carrier,
		// even for a scalar-only script. Do not rely on an unrelated class/container
		// to pull in <memory> accidentally.
		if (Emitter.bNeedsUtility)
		{
			Emitter.bNeedsMemory = true;
		}
		if (Emitter.bNeedsCctype)
		{
			Header << "#include <cctype>\n";
		}
		if (Emitter.bNeedsTypeTraits)
		{
			Header << "#include <type_traits>\n";
		}
		if (Emitter.bNeedsConstructionPhase)
		{
			Header << "#include <typeinfo>\n#include <memory>\n#include <new>\n";
		}
		if (Emitter.bNeedsCmath)
		{
			Header << "#include <cmath>\n";
		}
		if (Emitter.bNeedsFstream)
		{
			Header << "#include <fstream>\n"
			       << "#include <sstream>\n";
		}
		if (Emitter.bNeedsFilesystem || Emitter.bNeedsFstream)
		{
			Header << "#include <filesystem>\n";
		}
		if (Emitter.bNeedsVector)
		{
			Header << "#include <vector>\n"
			       << "#include <algorithm>\n";
		}
		if (Emitter.bNeedsArray)
		{
			Header << "#include <array>\n";
		}
		if (Emitter.bNeedsMap || Emitter.bNeedsChannel || Emitter.bNeedsThread)
		{
			Header << "#include <map>\n"
			       << "#include <unordered_map>\n";
		}
		if (Emitter.bNeedsSet)
		{
			Header << "#include <unordered_set>\n";
		}
		if (Emitter.bNeedsDeque)
		{
			Header << "#include <deque>\n";
		}
		if (Emitter.bNeedsQueue || Emitter.bNeedsChannel)
		{
			Header << "#include <queue>\n";
		}
		if (Emitter.bNeedsStack)
		{
			Header << "#include <stack>\n";
		}
		if (Emitter.bNeedsMemory)
		{
			Header << "#include <memory>\n";
		}
		if (Emitter.bNeedsFunctional || Emitter.bNeedsChannel || Emitter.bNeedsThread)
		{
			Header << "#include <functional>\n";
		}
		if (Emitter.bNeedsUtility)
		{
			Header << "#include <any>\n"
			       << "#include <typeindex>\n"
			       << "#include <utility>\n";
		}
		if (Emitter.bNeedsChannel || Emitter.bNeedsThread || Emitter.bNeedsCstdlib)
		{
			Header << "#include <mutex>\n";
		}
		if (Emitter.bNeedsChannel || Emitter.bNeedsThread)
		{
			Header << "#include <thread>\n";
			Header << "#include <condition_variable>\n"
			       << "#include <type_traits>\n";
		}
		if (Emitter.bNeedsChrono || Emitter.bNeedsThread)
		{
			Header << "#include <chrono>\n";
		}
		if (Emitter.bNeedsCstdlib || Emitter.bNeedsConsole)
		{
			Header << "#include <cstdlib>\n"
			       << "#if defined(_WIN32)\n"
			       << "#ifndef WIN32_LEAN_AND_MEAN\n"
			       << "#define WIN32_LEAN_AND_MEAN\n"
			       << "#endif\n"
			       << "#ifndef NOMINMAX\n"
			       << "#define NOMINMAX\n"
			       << "#endif\n"
			       << "#include <io.h>\n"
			       << "#include <windows.h>\n"
			       << "#else\n"
			       << "#include <unistd.h>\n"
			       << "#endif\n";
		}
		FEmitterRuntimeFlags RuntimeFlags;
		RuntimeFlags.bNeedsVector = Emitter.bNeedsVector;
		RuntimeFlags.bNeedsArray = Emitter.bNeedsArray;
		RuntimeFlags.bNeedsAssert = Emitter.bNeedsAssert;
		RuntimeFlags.bNeedsMap = Emitter.bNeedsMap;
		RuntimeFlags.bNeedsDeque = Emitter.bNeedsDeque;
		RuntimeFlags.bNeedsSet = Emitter.bNeedsSet;
		RuntimeFlags.bNeedsQueue = Emitter.bNeedsQueue;
		RuntimeFlags.bNeedsStack = Emitter.bNeedsStack;
		RuntimeFlags.bNeedsMemory = Emitter.bNeedsMemory;
		RuntimeFlags.bNeedsObjectBase = Emitter.bNeedsObjectBase;
		RuntimeFlags.bNeedsConstructionPhase = Emitter.bNeedsConstructionPhase;
		RuntimeFlags.bNeedsFunctional = Emitter.bNeedsFunctional;
		RuntimeFlags.bNeedsLocalStatics = Emitter.bNeedsLocalStatics;
		RuntimeFlags.bNeedsHandle = Emitter.bNeedsHandle;
		RuntimeFlags.bNeedsEvents = Emitter.bNeedsEvents;
		RuntimeFlags.bNeedsAttribute = Emitter.bHasStandardAttribute;
		RuntimeFlags.bNeedsChannel = Emitter.bNeedsChannel;
		RuntimeFlags.bNeedsThread = Emitter.bNeedsThread;
		RuntimeFlags.bNeedsUtility = Emitter.bNeedsUtility;
		RuntimeFlags.bNeedsChrono = Emitter.bNeedsChrono;
		RuntimeFlags.bNeedsConsole = Emitter.bNeedsConsole;
		RuntimeFlags.bNeedsCstdlib = Emitter.bNeedsCstdlib;
		RuntimeFlags.bNeedsFstream = Emitter.bNeedsFstream;
		RuntimeFlags.bNeedsFilesystem = Emitter.bNeedsFilesystem;
		RuntimeFlags.bNeedsCctype = Emitter.bNeedsCctype;
		Header << '\n'
		       << MakeRuntimeSupport(RuntimeFlags);
		std::string RuntimeSupport = Header.str();
		std::string ProgramSource = Emitter.Out.str();
		OutCpp = RuntimeSupport + ProgramSource;
		if (OutRuntimeSupport)
		{
			*OutRuntimeSupport = std::move(RuntimeSupport);
		}
		if (OutProgramSource)
		{
			*OutProgramSource = std::move(ProgramSource);
		}
		return true;
	}

	static bool EmitCppFromProgramWithBindings(
	    const FProgram& InputProgram,
	    std::string& OutCpp,
	    std::string& OutError,
	    const IHostBindingCatalog* const Bindings,
	    std::string* const OutRuntimeSupport = nullptr,
	    std::string* const OutProgramSource = nullptr)
	{
		OutCpp.clear();
		OutError.clear();
		if (OutRuntimeSupport)
		{
			OutRuntimeSupport->clear();
		}

		if (OutProgramSource)
		{
			OutProgramSource->clear();
		}

		auto Prepared = PrepareCodegenProgram(InputProgram, {}, Bindings, OutError);
		return Prepared && EmitPreparedCppFromProgramWithBindings(*Prepared, OutCpp, OutError, Bindings, OutRuntimeSupport, OutProgramSource);
	}

	bool EmitCppFromProgram(
	    const FProgram& Program,
	    std::string& OutCpp,
	    std::string& OutError)
	{
		return EmitCppFromProgramWithBindings(Program, OutCpp, OutError, nullptr);
	}

	bool BuildCppSourceBundle(
	    const std::string_view GeneratedCpp,
	    const std::string_view BundleName,
	    FCppAotSourceBundle& OutBundle,
	    std::string& OutError)
	{
		// Inputs may refer to files or metadata in the bundle being replaced.
		std::string OwnedCpp(GeneratedCpp);
		const std::string Name(BundleName);
		OutBundle = {};
		OutError.clear();
		if (Name.empty())
		{
			OutError = "AOT: source-bundle name must not be empty";
			return false;
		}
		for (const unsigned char Character : Name)
		{
			if (!((Character >= 'a' && Character <= 'z') || (Character >= 'A' && Character <= 'Z') || (Character >= '0' && Character <= '9') || Character == '_' || Character == '-'))
			{
				OutError = "AOT: source-bundle name must be a portable file stem";
				return false;
			}
		}
		if (Name.size() <= 4)
		{
			std::string Folded(Name);
			for (char& Character : Folded)
			{
				if (Character >= 'A' && Character <= 'Z')
				{
					Character += 'a' - 'A';
				}
			}

			const bool Reserved = Folded == "con" || Folded == "prn" || Folded == "aux" || Folded == "nul" ||
			    (Folded.size() == 4 && Folded[3] >= '1' && Folded[3] <= '9' &&
			        (Folded.compare(0, 3, "com") == 0 || Folded.compare(0, 3, "lpt") == 0));
			if (Reserved)
			{
				OutError = "AOT: source-bundle name is a reserved Windows device name";
				return false;
			}
		}
		if (OwnedCpp.empty())
		{
			OutError = "AOT: generated C++ source must not be empty";
			return false;
		}
		const bool bRequiresThreads =
		    HasIncludeDirective(OwnedCpp, "<thread>");
		const std::string ImplementationName = Name + ".generated.inl";
		const std::string EntryName = Name + ".cpp";
		std::string Entry = "// SPDX-FileCopyrightText: VanstySanic\n// SPDX-License-Identifier: MIT\n\n// Generated by VeroScriptCppAot " + FormatVersion(VEROSCRIPT_VERSION) + ".\n#include \"";
		Entry += ImplementationName;
		Entry += "\"\n";
		OutBundle.Files.push_back({EntryName, std::move(Entry)});
		OutBundle.Files.push_back({ImplementationName, std::move(OwnedCpp)});

		const auto EscapeJson = [](const std::string_view Value)
		{
			std::ostringstream Out;
			for (const unsigned char Character : Value)
			{
				switch (Character)
				{
				case '\\':
					{
						Out << "\\\\";
						break;
					}
				case '"':
					{
						Out << "\\\"";
						break;
					}
				case '\n':
					{
						Out << "\\n";
						break;
					}
				case '\r':
					{
						Out << "\\r";
						break;
					}
				case '\t':
					{
						Out << "\\t";
						break;
					}
				default:
					{
						Out << static_cast<char>(Character);
						break;
					}
				}
			}
			return Out.str();
		};
		OutBundle.ManifestJson = "{\"version\":1,\"kind\":\"veroscript.cpp-aot-source-bundle\","
		                         "\"requirements\":{\"cxxStandard\":17,\"threads\":" +
		    std::string(bRequiresThreads ? "true" : "false") + "},"
		                                                       "\"entry\":\"" +
		    EscapeJson(EntryName) + "\",\"files\":[\"" + EscapeJson(EntryName) + "\",\"" + EscapeJson(ImplementationName) + "\"],\"translationUnits\":[\"" + EscapeJson(EntryName) + "\"]}";
		return true;
	}

	bool EmitCppSourceBundleFromProgram(
	    const FProgram& Program,
	    const std::string_view BundleName,
	    FCppAotSourceBundle& OutBundle,
	    std::string& OutError)
	{
		const std::string OwnedName(BundleName);
		std::string Generated;
		std::string RuntimeSupport;
		std::string ProgramSource;
		if (!EmitCppFromProgramWithBindings(
		        Program, Generated, OutError, nullptr, &RuntimeSupport, &ProgramSource))
		{
			OutBundle = {};
			return false;
		}
		if (!BuildCppSourceBundle(Generated, OwnedName, OutBundle, OutError))
		{
			return false;
		}
		const bool bRequiresThreads =
		    HasIncludeDirective(RuntimeSupport, "<thread>");
		const std::string& Name = OwnedName;
		const std::string EntryName = Name + ".cpp";
		const std::string RuntimeName = Name + ".runtime.generated.inl";
		const std::string ProgramName = Name + ".program.generated.inl";
		std::string Entry = "// SPDX-FileCopyrightText: VanstySanic\n// SPDX-License-Identifier: MIT\n\n// Generated by VeroScriptCppAot " + FormatVersion(VEROSCRIPT_VERSION) + ".\n#include \"";
		Entry += RuntimeName;
		Entry += "\"\n#include \"";
		Entry += ProgramName;
		Entry += "\"\n";
		OutBundle.Files.clear();
		OutBundle.Files.push_back({EntryName, std::move(Entry)});
		OutBundle.Files.push_back({RuntimeName, std::move(RuntimeSupport)});
		OutBundle.Files.push_back({ProgramName, std::move(ProgramSource)});
		OutBundle.ManifestJson = "{\"version\":1,\"kind\":\"veroscript.cpp-aot-source-bundle\","
		                         "\"requirements\":{\"cxxStandard\":17,\"threads\":" +
		    std::string(bRequiresThreads ? "true" : "false") + "},"
		                                                       "\"entry\":\"" +
		    EntryName + "\",\"files\":[\"" + EntryName + "\",\"" + RuntimeName + "\",\"" + ProgramName + "\"],\"translationUnits\":[\"" + EntryName + "\"]}";
		return true;
	}

	static bool EmitCppFromSourceWithBindings(
	    std::string OwnedSource,
	    std::string Label,
	    std::string& OutCpp,
	    std::string& OutError,
	    const std::vector<std::string>& ImportPaths,
	    const std::vector<std::string>& Defines,
	    const std::vector<FPredefinedMacro>& PredefinedMacros,
	    const std::vector<std::string>& HostTypeNames,
	    const IHostBindingCatalog* const Bindings,
	    const std::vector<std::string>& ImplicitImportPaths,
	    const bool bAllowCircularImports,
	    const bool bAllowProjectWideAccess,
	    const FAnnotationProcessingOptions& Processing = {},
	    const FCompileProcessingOptions& CompileProcessing = {},
	    const FScriptCompileExtensionsOptions& ScriptExtensions = {}, const FCodegenOptions* SourceOptions = nullptr,
	    const bool bLoadFile = false)
	{
		OutCpp.clear();
		OutError.clear();
		if (Label.empty())
		{
			Label = "aot.vs";
		}

		VeroScriptModuleLoader Loader(Label, CompileProcessing.ModuleEnvironment);
		auto EffectiveProcessing = CompileProcessing;
		{
			const FCompileProcessingOptions Defaults;
			const FAnnotationProcessingOptions AnnotationDefaults;
			if (!EffectiveProcessing.Registry && EffectiveProcessing.MaxOperations == Defaults.MaxOperations)
			{
				EffectiveProcessing.MaxOperations = Processing.MaxOperations;
			}

			else if (Processing.MaxOperations != AnnotationDefaults.MaxOperations)
			{
				EffectiveProcessing.MaxOperations =
				    std::min(EffectiveProcessing.MaxOperations, Processing.MaxOperations);
			}

			if (!EffectiveProcessing.Registry &&
			    EffectiveProcessing.MaxSourceBytes == Defaults.MaxSourceBytes)
			{
				EffectiveProcessing.MaxSourceBytes = Processing.MaxSourceBytes;
			}

			else if (Processing.MaxSourceBytes != AnnotationDefaults.MaxSourceBytes)
			{
				EffectiveProcessing.MaxSourceBytes =
				    std::min(EffectiveProcessing.MaxSourceBytes, Processing.MaxSourceBytes);
			}
		}
		if (Bindings)
		{
			for (auto Definition : GetAnnotationDefinitions(*Bindings))
			{
				EffectiveProcessing.AnnotationDefinitions.push_back(std::move(Definition));
			}
		}

		Loader.SetCompileProcessing(std::move(EffectiveProcessing));
		Loader.SetScriptCompileExtensions(ScriptExtensions);
		if (SourceOptions)
		{
			for (const auto& Overlay : SourceOptions->SourceOverlays)
			{
				Loader.SetSourceOverlay(Overlay.Path, Overlay.Source);
			}

			for (const auto& Path : SourceOptions->StdlibSearchPaths)
			{
				Loader.AddStdlibSearchRoot(Path);
			}
		}
		for (const std::string& Path : ImportPaths)
		{
			Loader.AddImportPath(Path);
		}
		for (const std::string& Path : ImplicitImportPaths)
		{
			Loader.AddImplicitImportPath(Path);
		}
		Loader.SetAllowCircularImports(bAllowCircularImports);
		Loader.SetAllowProjectWideAccess(bAllowProjectWideAccess);
		Loader.SetDefines(Defines);
		Loader.SetPredefinedMacros(PredefinedMacros);
		if (!HostTypeNames.empty())
		{
			std::unordered_set<std::string> TypeSet(HostTypeNames.begin(), HostTypeNames.end());
			Loader.SetHostTypeNames(std::move(TypeSet));
		}
		if (!bLoadFile)
		{
			Loader.SetSourceOverlay(Label, OwnedSource);
		}

		std::unique_ptr<FProgram> Program =
		    bLoadFile ? Loader.LoadMainModule() : Loader.LoadSourceModule(OwnedSource, Label);
		if (!Program || Loader.HasError())
		{
			const auto& Diagnostics = Loader.GetDiagnostics();
			const auto Primary = std::find_if(Diagnostics.begin(), Diagnostics.end(),
			    [](const auto& D)
			{
				return D.Severity == ECompileDiagnosticSeverity::Error;
			});
			OutError = Primary != Diagnostics.end() ? Primary->Message
			    : Loader.HasError()                 ? Loader.GetError().Message
			                                        : "AOT: module load failed";
			return false;
		}
		// Owned preparation preserves processor state across phases and closes the
		// extension VM before normalizing the codegen copy. Prepare only once.
		auto Prepared = PrepareCodegenProgram(std::move(Program), Processing, Bindings, OutError);
		if (!Prepared) return false;
		return EmitPreparedCppFromProgramWithBindings(*Prepared, OutCpp, OutError, Bindings);
	}

	bool EmitCppFromSource(
	    const std::string_view Source,
	    const std::string_view SourceLabel,
	    std::string& OutCpp,
	    std::string& OutError,
	    const std::vector<std::string>& ImportPaths,
	    const std::vector<std::string>& Defines,
	    const std::vector<std::string>& HostTypeNames,
	    const std::vector<std::string>& ImplicitImportPaths,
	    const bool bAllowCircularImports,
	    const bool bAllowProjectWideAccess)
	{
		return EmitCppFromSourceWithBindings(
		    std::string(Source),
		    std::string(SourceLabel),
		    OutCpp,
		    OutError,
		    ImportPaths,
		    Defines,
		    {},
		    HostTypeNames,
		    nullptr,
		    ImplicitImportPaths,
		    bAllowCircularImports,
		    bAllowProjectWideAccess);
	}

	bool EmitCppFromFile(
	    const std::string_view ScriptPath,
	    const std::string_view OutputPath,
	    std::string& OutError,
	    const std::vector<std::string>& ImportPaths,
	    const std::vector<std::string>& Defines,
	    const std::vector<std::string>& ImplicitImportPaths,
	    const bool bAllowCircularImports,
	    const bool bAllowProjectWideAccess)
	{
		const std::string Path(ScriptPath);
		const std::string OutPath(OutputPath);
		OutError.clear();
		if (Path.find('\0') != std::string::npos || OutPath.find('\0') != std::string::npos)
		{
			OutError = "AOT: file paths cannot contain NUL characters";
			return false;
		}
		if (PathUtil::Equal(Path, OutPath))
		{
			OutError = "AOT: output path must differ from the input script: " + Path;
			return false;
		}
		const std::filesystem::path NativePath = PathUtil::FromUtf8(Path);
		std::error_code StatusError;
		if (std::filesystem::is_directory(NativePath, StatusError))
		{
			OutError = "AOT: cannot open script: " + Path;
			return false;
		}
		std::ifstream Input(
		    NativePath,
		    std::ios::in | std::ios::binary);
		if (!Input)
		{
			OutError = "AOT: cannot open script: " + Path;
			return false;
		}
		// Extract through the input stream so a partial read failure cannot be
		// mistaken for a complete script. streambuf insertion flags its destination.
		std::string Buffer;
		char Chunk[8192];
		while (Input.read(Chunk, sizeof(Chunk)) || Input.gcount() > 0)
		{
			Buffer.append(Chunk, static_cast<std::size_t>(Input.gcount()));
		}
		if (Input.bad() || !Input.eof())
		{
			OutError = "AOT: failed while reading script: " + Path;
			return false;
		}
		std::string Cpp;
		if (!EmitCppFromSource(
		        Buffer,
		        Path,
		        Cpp,
		        OutError,
		        ImportPaths,
		        Defines,
		        {},
		        ImplicitImportPaths,
		        bAllowCircularImports,
		        bAllowProjectWideAccess))
		{
			return false;
		}
		const std::filesystem::path FinalPath = PathUtil::FromUtf8(OutPath);
		std::error_code FileError;
		if (!FinalPath.parent_path().empty())
		{
			std::filesystem::create_directories(FinalPath.parent_path(), FileError);
			if (FileError)
			{
				OutError = "AOT: cannot create output directory: " + PathUtil::ToUtf8(FinalPath.parent_path()) + ": " + FileError.message();
				return false;
			}
		}
		const std::filesystem::path TempPath =
		    PathUtil::MakeTemporarySiblingPath(FinalPath);
		std::ofstream Output(
		    TempPath,
		    std::ios::out | std::ios::binary | std::ios::trunc);
		if (!Output)
		{
			OutError = "AOT: cannot write output: " + OutPath;
			return false;
		}
		Output << Cpp;
		Output.close();
		if (!Output)
		{
			std::filesystem::remove(TempPath, FileError);
			OutError = "AOT: failed while writing output: " + OutPath;
			return false;
		}
		if (!PathUtil::ReplaceFileAtomically(TempPath, FinalPath, FileError))
		{
			const std::string ReplaceError = FileError.message();
			std::error_code CleanupError;
			std::filesystem::remove(TempPath, CleanupError);
			OutError = "AOT: cannot replace output: " + OutPath;
			if (!ReplaceError.empty())
			{
				OutError += ": " + ReplaceError;
			}
			return false;
		}
		return true;
	}

	static bool EmitCppWithOptions(
	    const std::string_view Source,
	    const std::string_view SourceLabel,
	    const FCodegenOptions& Options,
	    FCodegenResult& OutResult,
	    const bool bLoadFile)
	{
		// Keep views into a previous result alive before resetting that result.
		std::string OwnedSource(Source);
		std::string Label(SourceLabel);
		OutResult = {};
		if (bLoadFile && Label.find('\0') != std::string::npos)
		{
			OutResult.Error = "AOT: script path cannot contain NUL characters";
			return false;
		}
		if (bLoadFile && Label.empty())
		{
			OutResult.Error = "AOT: script path must not be empty";
			return false;
		}
		std::vector<std::string> HostTypeNames;
		if (Options.Bindings)
		{
			for (const FHostTypeDesc& Type : Options.Bindings->GetTypes())
			{
				if (!Type.Name.empty())
				{
					HostTypeNames.push_back(Type.Name);
				}
			}
			for (const FHostTypeAliasDesc& Alias : Options.Bindings->GetTypeAliases())
			{
				if (!Alias.Name.empty())
				{
					HostTypeNames.push_back(Alias.Name);
				}
			}
			for (const FGenericHostTypeTemplateDesc& Template :
			    Options.Bindings->GetGenericTypeTemplates())
			{
				if (!Template.Name.empty())
				{
					HostTypeNames.push_back(Template.Name);
				}
			}
		}
		if (!EmitCppFromSourceWithBindings(
		        std::move(OwnedSource),
		        std::move(Label),
		        OutResult.Output,
		        OutResult.Error,
		        Options.ImportPaths,
		        Options.Defines,
		        Options.PredefinedMacros,
		        HostTypeNames,
		        Options.Bindings,
		        Options.ImplicitImportPaths,
		        Options.bAllowCircularImports,
		        Options.bAllowProjectWideAccess, Options.AnnotationProcessing, Options.CompileProcessing, Options.ScriptCompileExtensions, &Options, bLoadFile))
		{
			OutResult.bSuccess = false;
			OutResult.Output.clear();
			return false;
		}
		if (Options.Bindings && (!Options.Bindings->GetFunctions().empty() || !Options.Bindings->GetTypes().empty()))
		{
			auto NativeTypeToCpp = [](const ENativeType Type, const std::string& HostName) -> std::string
			{
				switch (Type)
				{
				case ENativeType::Void:
					return "void";
				case ENativeType::Bool:
					return "bool";
				case ENativeType::Int:
					return "std::int32_t";
				case ENativeType::Unsigned:
					return "std::uint32_t";
				case ENativeType::Int64:
					return "std::int64_t";
				case ENativeType::UInt64:
					return "std::uint64_t";
				case ENativeType::Double:
					return "double";
				case ENativeType::Float:
					return "float";
				case ENativeType::Int8:
					return "std::int8_t";
				case ENativeType::UInt8:
					return "std::uint8_t";
				case ENativeType::Int16:
					return "std::int16_t";
				case ENativeType::UInt16:
					return "std::uint16_t";
				case ENativeType::String:
					return "const char*";
				case ENativeType::HostObject:
					return HostName.empty() ? "void*" : ("void* /* " + HostName + " */");
				default:
					return "void*";
				}
			};
			auto NativeParamToCpp = [&NativeTypeToCpp](
			                            const ENativeType Type,
			                            const std::string& HostName,
			                            const std::uint8_t Flag) -> std::string
			{
				std::string Result = NativeTypeToCpp(Type, HostName);
				if (NativeParamFlagIsRef(Flag))
				{
					if ((Flag & NativeParamFlagConstRef) != 0)
					{
						Result += " const";
					}
					Result.push_back('&');
				}
				return Result;
			};
			const auto SanitizeIdent = [](const std::string& Name)
			{
				return FCppEmitter::SanitizeForMangle(Name);
			};

			std::string Stubs;
			Stubs += "// Host binding stubs (In catalog → Out emit).\n";
			Stubs += "// Link these symbols from the host; AOT call sites use the same names.\n";
			Stubs += "#include <cstdint>\n";
			Stubs += "extern \"C\" {\n";
			for (const FHostFunctionDesc& Function : Options.Bindings->GetFunctions())
			{
				Stubs += "// ";
				Stubs += FormatHostFunctionSignature(Function);
				Stubs += "\n";
				Stubs += NativeTypeToCpp(Function.ReturnType, Function.ReturnHostTypeName);
				Stubs += ' ';
				Stubs += SanitizeIdent(Function.Name);
				Stubs += '(';
				for (std::size_t Index = 0; Index < Function.ParamTypes.size(); ++Index)
				{
					if (Index > 0)
					{
						Stubs += ", ";
					}
					const std::string HostName =
					    Index < Function.ParamHostTypeNames.size()
					    ? Function.ParamHostTypeNames[Index]
					    : std::string{};
					const std::uint8_t Flag = Index < Function.ParamFlags.size()
					    ? Function.ParamFlags[Index]
					    : std::uint8_t{0};
					Stubs += NativeParamToCpp(Function.ParamTypes[Index], HostName, Flag);
				}
				Stubs += ");\n";
			}
			for (const FHostTypeDesc& Type : Options.Bindings->GetTypes())
			{
				Stubs += "// host ";
				Stubs += HostTypeKindName(Type.Kind);
				Stubs += ' ';
				Stubs += Type.Name;
				Stubs += "\n";
				for (const FHostMemberDesc& Member : Type.Members)
				{
					Stubs += "//   ";
					Stubs += FormatHostMemberSignature(Member, Type.Name);
					Stubs += "\n";
					if (Member.Kind == EHostMemberKind::EnumMember)
					{
						continue;
					}
					if (Member.Kind == EHostMemberKind::Constructor)
					{
						Stubs += "void* ";
						Stubs += SanitizeIdent(
						    Member.NativeName.empty() ? Type.Name : Member.NativeName);
						Stubs += '(';
						for (std::size_t Index = 0; Index < Member.ParamTypes.size(); ++Index)
						{
							if (Index > 0)
							{
								Stubs += ", ";
							}
							const std::string HostName =
							    Index < Member.ParamHostTypeNames.size()
							    ? Member.ParamHostTypeNames[Index]
							    : std::string{};
							const std::uint8_t Flag = Index < Member.ParamFlags.size()
							    ? Member.ParamFlags[Index]
							    : std::uint8_t{0};
							Stubs += NativeParamToCpp(Member.ParamTypes[Index], HostName, Flag);
						}
						Stubs += ");\n";
						continue;
					}

					const bool bStatic = Member.Kind == EHostMemberKind::StaticMethod || Member.Kind == EHostMemberKind::StaticConstant;
					const bool bSetter = Member.Kind == EHostMemberKind::PropertySetter;
					std::string SymbolName;
					if (Member.bOperator && !Member.NativeName.empty())
					{
						SymbolName = SanitizeIdent(Member.NativeName);
					}
					else
					{
						SymbolName = SanitizeIdent(Type.Name);
						SymbolName += '_';
					}
					if (!Member.bOperator && (Member.Kind == EHostMemberKind::PropertyGetter || Member.Kind == EHostMemberKind::Field))
					{
						SymbolName += "get_";
					}
					else if (!Member.bOperator && bSetter)
					{
						SymbolName += "set_";
					}
					if (!Member.bOperator)
					{
						SymbolName += SanitizeIdent(
						    Member.Name.empty() ? Type.Name : Member.Name);
					}

					Stubs += NativeTypeToCpp(
					    bSetter ? ENativeType::Void : Member.ReturnType,
					    Member.ReturnHostTypeName.empty() ? Type.Name : Member.ReturnHostTypeName);
					Stubs += ' ';
					Stubs += SymbolName;
					Stubs += '(';
					bool bNeedComma = false;
					if (!bStatic)
					{
						Stubs += NativeTypeToCpp(ENativeType::HostObject, Type.Name);
						bNeedComma = true;
					}
					for (std::size_t Index = 0; Index < Member.ParamTypes.size(); ++Index)
					{
						if (bNeedComma)
						{
							Stubs += ", ";
						}
						bNeedComma = true;
						const std::string HostName =
						    Index < Member.ParamHostTypeNames.size()
						    ? Member.ParamHostTypeNames[Index]
						    : std::string{};
						const std::uint8_t Flag = Index < Member.ParamFlags.size()
						    ? Member.ParamFlags[Index]
						    : std::uint8_t{0};
						Stubs += NativeParamToCpp(Member.ParamTypes[Index], HostName, Flag);
					}
					Stubs += ");\n";
				}
			}
			Stubs += "}\n\n";
			OutResult.Output.insert(0, std::move(Stubs));
		}
		OutResult.bSuccess = true;
		return true;
	}

	bool FCppCodegenBackend::EmitFromSource(
	    const std::string_view Source,
	    const std::string_view SourceLabel,
	    const FCodegenOptions& Options,
	    FCodegenResult& OutResult)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			std::string Error;
			if (!ApplyCodegenModuleEnvironment(Effective, Error))
			{
				OutResult = {};
				OutResult.Error = std::move(Error);
				return false;
			}
			return EmitFromSource(Source, SourceLabel, Effective, OutResult);
		}
		return EmitCppWithOptions(Source, SourceLabel, Options, OutResult, false);
	}

	bool FCppCodegenBackend::EmitFromFile(
	    const std::string_view ScriptPath,
	    const FCodegenOptions& Options,
	    FCodegenResult& OutResult)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			std::string Error;
			if (!ApplyCodegenModuleEnvironment(Effective, Error))
			{
				OutResult = {};
				OutResult.Error = std::move(Error);
				return false;
			}
			return EmitFromFile(ScriptPath, Effective, OutResult);
		}
		// Let the core loader resolve entry overlays, imports and compile extensions
		// exactly as it does for Engine and Session file compilation.
		return EmitCppWithOptions({}, ScriptPath, Options, OutResult, true);
	}
}
