// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBytecode.h"
#include "VeroScriptValue.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript
{
	/**
	 * CallMethod missing-method reporting. Prefer HasInstanceMethod when probing
	 * every frame; Quiet avoids writing to the host diagnostic stream.
	 */
	enum class EMissingMethodPolicy : std::uint8_t
	{
		// Fail with ExitCode 4, MessageId diag.callmethod_unknown_method, write diagnostic.
		Error = 0,
		// Same result as Error but do not write to the host diagnostic stream.
		Quiet = 1,
	};

	/**
	 * Outcome of name+arity method resolution for embeds.
	 */
	enum class EMethodResolveStatus : std::uint8_t
	{
		NotFound = 0,
		Found,
		/**
		 * Multiple same-arity overloads; pass a mangled leaf (`Name#3:int`) or rely on
		 * runtime argument types when they uniquely select one overload.
		 */
		Ambiguous,
	};

	/**
	 * Snapshot of one compiled script class/struct for embed hosts (UClass bridge, etc.).
	 * Copied so it remains valid after the host finishes iterating; reload invalidates
	 * TypeIndex against a new module.
	 */
	struct FLoadedTypeInfo
	{
		FTemplateInstanceInfo TemplateInstance;
		FTypeDeclarationScope Scope;
		std::vector<FNestedTypeDeclaration> NestedTypes;
		bool HasHostBase() const
		{
			return !HostBaseTypeName.empty();
		}

		std::uint16_t TypeIndex = 0;
		EScriptObjectKind Kind = EScriptObjectKind::Class;
		// Full linkage name as stored in the bytecode module.
		std::string TypeName;
		// Leaf name after namespace/`\\x1F` separator.
		std::string SimpleName;
		std::string HostBaseTypeName;
		std::uint32_t HostBaseTypeId = 0;
		bool bIsAbstract = false;
		bool bIsStatic = false;
		bool bDestructorPureVirtual = false;
		std::vector<std::uint16_t> BaseTypeIndices;
		// First field index owned by this type (after inherited base fields).
		std::uint16_t OwnFieldStartIndex = 0;
		FDeclMeta DeclMeta;
	};

	/**
	 * One script field on a type (flattened layout includes inherited fields).
	 */
	struct FLoadedFieldInfo
	{
		std::string Name;
		// Source-facing spelling (including transparent host aliases).
		std::string TypeName;
		// VM storage/compatibility identity after alias expansion.
		std::string RuntimeTypeName;
		std::uint16_t FieldIndex = 0;
		// True when the field is declared on this type (not only inherited).
		bool bDeclaredOnType = false;
		// True for `static` data members (FieldIndex is 0xFFFF; not for GetField).
		bool bIsStatic = false;
		// Declared with contextual `mutable`; writable through a const receiver.
		bool bIsMutable = false;
		FDeclMeta DeclMeta;
		bool bConstexpr = false;
	};

	/**
	 * One instance method (or optional ctor) visible on a type for embed metadata.
	 * Name is the bare leaf (`Tick`), not the mangled `Owner::Tick#...` key.
	 */
	struct FInstanceMethodInfo
	{
		FTemplateInstanceInfo TemplateInstance;
		std::string Name;
		std::uint8_t ParamCount = 0;
		/**
		 * Decoded param type names from mangling (`float` for `Tick#5:float`).
		 * Empty when the key is arity-only (`Name#1`) or undecodable; ParamCount
		 * remains authoritative for CallMethod / HasInstanceMethod.
		 */
		std::vector<std::string> ParamTypeNames;
		bool bReturnsVoid = true;
		bool bReturnsRef = false;
		bool bReturnsConstRef = false;
		bool bIsVirtual = false;
		bool bIsAbstract = false;
		bool bIsConst = false;
		bool bIsConstructor = false;
		bool bIsDestructor = false;
		bool bDeleted = false;
		std::uint16_t FunctionIndex = static_cast<std::uint16_t>(-1);
		std::uint16_t DeclaringTypeIndex = static_cast<std::uint16_t>(-1);
		std::string DeclaringTypeName;
		/**
		 * Canonical return type name: "void" for void functions, otherwise the
		 * top-level return of the fn(...)->T signature (handles nested fn types).
		 * Empty when undecodable; bReturnsVoid stays authoritative for void-ness.
		 */
		std::string ReturnTypeName;
		// Declared parameter names (size == ParamCount); empty string when unnamed.
		std::vector<std::string> ParamNames;
		/**
		 * Per-parameter BytecodeParamFlag* bits (size == ParamCount when present).
		 * Empty means unknown/legacy bytecode (assume no refs).
		 */
		std::vector<std::uint8_t> ParamFlags;
		FDeclMeta DeclMeta;
		bool bConstexpr = false;
	};

	/**
	 * One free function for embed metadata.
	 * Name is the bare leaf (`Add`), not a mangled or unit-prefixed linkage name.
	 */
	struct FFreeFunctionInfo
	{
		FTemplateInstanceInfo TemplateInstance;
		std::string Name;
		std::uint8_t ParamCount = 0;
		std::vector<std::string> ParamTypeNames;
		bool bReturnsVoid = true;
		bool bReturnsRef = false;
		bool bReturnsConstRef = false;
		bool bDeleted = false;
		std::uint16_t FunctionIndex = static_cast<std::uint16_t>(-1);
		// Full FunctionIndices key when available.
		std::string LinkageName;
		std::string ModulePath;
		bool bExported = false;
		// Same contract as FInstanceMethodInfo::ReturnTypeName / ParamNames / ParamFlags.
		std::string ReturnTypeName;
		std::vector<std::string> ParamNames;
		std::vector<std::uint8_t> ParamFlags;
		FDeclMeta DeclMeta;
		bool bConstexpr = false;
	};

	/**
	 * One top-level global variable. Static data members are omitted by default
	 * because they are already exposed by EnumerateFields.
	 */
	struct FLoadedGlobalInfo
	{
		std::string Name;
		std::string LinkageName;
		std::string ModulePath;
		std::string DeclaredTypeName;
		std::string RuntimeTypeName;
		std::uint16_t GlobalSlot = static_cast<std::uint16_t>(-1);
		bool bConst = false;
		bool bExported = false;
		bool bStaticMember = false;
		FDeclMeta DeclMeta;
		bool bConstexpr = false;
	};

	/**
	 * One enumerator name/value for embed metadata.
	 */
	struct FLoadedEnumMemberInfo
	{
		std::string Name;
		std::int64_t Value = 0;
		// Declaration annotations and metadata (for example, display names).
		FDeclMeta DeclMeta;
	};

	/**
	 * Script enum declaration for embed metadata.
	 */
	struct FLoadedEnumInfo
	{
		FTypeDeclarationScope Scope;
		std::string Name;
		std::string SimpleName;
		// True when declared as `enum class` (scoped enumerators).
		bool bScoped = false;
		std::string UnderlyingTypeName = "int";
		std::vector<FLoadedEnumMemberInfo> Members;
		FDeclMeta DeclMeta;
	};

	/**
	 * Snapshot of one compiled script interface for embed hosts.
	 */
	struct FLoadedInterfaceInfo
	{
		FTypeDeclarationScope Scope;
		std::vector<FNestedTypeDeclaration> NestedTypes;
		std::uint16_t InterfaceIndex = 0;
		std::string Name;
		std::string SimpleName;
		std::vector<std::uint16_t> BaseInterfaceIndices;
		std::vector<FLoadedFieldInfo> Fields;
		FDeclMeta DeclMeta;
	};

	/**
	 * One `static` method on a class/struct (or interface) for embed metadata.
	 * Name is the bare leaf (`Seed`), not the mangled `Owner::Seed#...` key.
	 */
	struct FStaticMethodInfo
	{
		FTemplateInstanceInfo TemplateInstance;
		std::string Name;
		std::uint8_t ParamCount = 0;
		std::vector<std::string> ParamTypeNames;
		bool bReturnsVoid = true;
		bool bReturnsRef = false;
		bool bReturnsConstRef = false;
		bool bIsConst = false;
		bool bDeleted = false;
		std::uint16_t FunctionIndex = static_cast<std::uint16_t>(-1);
		std::uint16_t DeclaringTypeIndex = static_cast<std::uint16_t>(-1);
		std::string DeclaringTypeName;
		std::string LinkageName;
		// Same contract as FInstanceMethodInfo::ReturnTypeName / ParamNames / ParamFlags.
		std::string ReturnTypeName;
		std::vector<std::string> ParamNames;
		std::vector<std::uint8_t> ParamFlags;
		FDeclMeta DeclMeta;
		bool bConstexpr = false;
	};

	namespace ModuleMeta
	{
		/** Owned snapshots, including open declarations with no compiled instances. */
		void EnumerateTemplateDeclarations(const FBytecodeModule& Module,
			std::vector<FTemplateDeclarationInfo>& Out, bool ExportedOnly = false);
		// Leaf type name from linkage (`unit\\x1Fns.Name` / `ns::Name` -> `Name`).
		std::string TypeSimpleName(std::string_view TypeLinkageName);

		/**
		 * Match Wanted against full linkage, post-unit name (`Alpha.Dup`), or leaf (`Dup`)
		 * (same rules as ConstructObject).
		 */
		bool TypeNameMatches(std::string_view TypeLinkageName, std::string_view Wanted);

		/**
		 * Resolve TypeName to a unique Types[] index. Prefers exact linkage match.
		 * Multiple leaf matches -> false and *OutAmbiguous=true when provided.
		 */
		bool FindTypeIndex(
			const FBytecodeModule& Module,
			std::string_view TypeName,
			std::uint16_t& OutTypeIndex,
			bool* OutAmbiguous = nullptr);

		/**
		 * Resolve TypeName to a unique Interfaces[] index (same matching rules as FindTypeIndex).
		 */
		bool FindInterfaceIndex(
			const FBytecodeModule& Module,
			std::string_view TypeName,
			std::uint16_t& OutInterfaceIndex,
			bool* OutAmbiguous = nullptr);

		/**
		 * Return whether a runtime script object of ActualTypeName can be stored in
		 * a slot declared as ExpectedTypeName. This is the bytecode/runtime counterpart
		 * of compiler subtype conversion and covers script base classes/structs and
		 * implemented (including inherited) interfaces.
		 */
		bool IsObjectTypeAssignableTo(
			const FBytecodeModule& Module,
			std::string_view ActualTypeName,
			std::string_view ExpectedTypeName);

		/**
		 * Decode length-prefixed param types from a mangled function key / leaf
		 * (`...#5:float`, `...#3:int6:string`). Arity-only `#N` yields an empty list.
		 */
		bool DecodeMethodParamTypeNames(
			std::string_view MangledFunctionName,
			std::vector<std::string>& OutParamTypeNames);

		/**
		 * Fill Out with all Types (or only those with HostBase when bHostSubclassesOnly).
		 */
		void EnumerateTypes(
			const FBytecodeModule& Module,
			std::vector<FLoadedTypeInfo>& Out,
			bool bHostSubclassesOnly = false);

		/**
		 * Flattened fields for TypeName. When bDeclaredOnly, only OwnFieldStartIndex...
		 */
		bool EnumerateFields(
			const FBytecodeModule& Module,
			std::string_view TypeName,
			std::vector<FLoadedFieldInfo>& Out,
			bool bDeclaredOnly = false);

		/**
		 * Resolve an instance method (ctors/dtors excluded) by bare or mangled name + arity.
		 * Walks bases and interface defaults. Same-arity overloads -> Ambiguous unless
		 * Arguments uniquely match mangled param types (or a mangled leaf is passed).
		 */
		EMethodResolveStatus TryResolveInstanceMethodOnType(
			const FBytecodeModule& Module,
			std::string_view TypeName,
			std::string_view MethodName,
			std::uint8_t ArgumentCount,
			std::uint16_t& OutFunctionIndex,
			const FValue* Arguments = nullptr);

		/**
		 * Resolve a constructor for TypeName by argument count (name = type leaf).
		 * Optional Arguments disambiguate same-arity constructor overloads.
		 */
		EMethodResolveStatus TryResolveConstructorOnType(
			const FBytecodeModule& Module,
			std::string_view TypeName,
			std::uint8_t ArgumentCount,
			std::uint16_t& OutFunctionIndex,
			const FValue* Arguments = nullptr);

		/**
		 * True when TypeName exposes MethodName with ArgumentCount (unique or overloaded).
		 * Same-arity overloads still count as present; CallMethod disambiguates by args.
		 */
		bool HasInstanceMethodOnType(
			const FBytecodeModule& Module,
			std::string_view TypeName,
			std::string_view MethodName,
			std::uint8_t ArgumentCount);

		/**
		 * Resolve a free function by bare or mangled name + arity.
		 * Prefers exact FunctionIndices key match. Same-arity overloads -> Ambiguous
		 * unless Arguments uniquely match (`Name#fn#3:int` also disambiguates).
		 */
		EMethodResolveStatus TryResolveFreeFunction(
			const FBytecodeModule& Module,
			std::string_view FunctionName,
			std::uint8_t ArgumentCount,
			std::uint16_t& OutFunctionIndex,
			const FValue* Arguments = nullptr);

		/**
		 * True when a free function matches name + arity (unique or overloaded).
		 * Overloads still count as present; CallFunction disambiguates by args.
		 */
		bool HasFreeFunction(
			const FBytecodeModule& Module,
			std::string_view FunctionName,
			std::uint8_t ArgumentCount);

		/**
		 * Find field index on TypeName's flattened Fields.
		 */
		bool FindFieldIndex(
			const FBytecodeModule& Module,
			std::string_view TypeName,
			std::string_view FieldName,
			std::uint16_t& OutFieldIndex);

		/**
		 * List instance methods for TypeName. Most-derived override wins when
		 * bIncludeInherited (includes interface defaults). Ctors/dtors optional.
		 * TypeName may also be an interface (abstract + default methods).
		 */
		bool EnumerateInstanceMethods(
			const FBytecodeModule& Module,
			std::string_view TypeName,
			std::vector<FInstanceMethodInfo>& Out,
			bool bIncludeInherited = true,
			bool bIncludeConstructors = false,
			bool bIncludeDestructors = false);

		/**
		 * List `static` methods declared on TypeName (class/struct or interface).
		 * Does not walk bases; each type owns its own static methods.
		 */
		bool EnumerateStaticMethods(
			const FBytecodeModule& Module,
			std::string_view TypeName,
			std::vector<FStaticMethodInfo>& Out);

		/**
		 * List free functions in the module (deduped by FunctionIndex).
		 * Overloads appear as separate entries when mangling differs.
		 */
		void EnumerateFreeFunctions(
			const FBytecodeModule& Module,
			std::vector<FFreeFunctionInfo>& Out,
			bool bVisibleFromEntryOnly = false);

		// List top-level globals in slot order.
		void EnumerateGlobals(
			const FBytecodeModule& Module,
			std::vector<FLoadedGlobalInfo>& Out,
			bool bVisibleFromEntryOnly = false,
			bool bIncludeStaticMembers = false);

		/**
		 * List script interfaces from Module.Interfaces (fields + DeclMeta).
		 */
		void EnumerateInterfaces(
			const FBytecodeModule& Module,
			std::vector<FLoadedInterfaceInfo>& Out);

		/**
		 * List script enums from Module.Enums (metadata; values are int constants at runtime).
		 */
		void EnumerateEnums(
			const FBytecodeModule& Module,
			std::vector<FLoadedEnumInfo>& Out);
	}
}
