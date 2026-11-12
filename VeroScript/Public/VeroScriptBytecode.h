// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptExternalDependency.h"
#include "VeroScriptMacro.h"
#include "VeroScriptCompileProcessing.h"

#include "VeroScriptObject.h"
#include "VeroScriptTypes.h"
#include "VeroScriptTypeScope.h"
#include "VeroScriptValue.h"
#include "VeroScriptVersion.h"
#include "VeroScriptTemplate.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace VeroScript
{
	// Reserved type metadata key whose value is the exact hidden function linkage
	// for a host-invoked class-default phase. Using declaration metadata keeps the
	// information portable through packages without introducing an ABI index.
	inline constexpr std::string_view ClassDefaultsLinkageMetaKey =
		"__veroscript.class_defaults_linkage";
	// Reserved suffix for a lazily generated zero-argument value factory. Its
	// canonical function name avoids changing type identity when first used.
	inline constexpr std::string_view DefaultConstructionFactorySuffix = "$default_value";
	// Compiler-owned optional payload slots retain empty storage until selected.
	inline constexpr std::string_view UninitializedPayloadMetaKey =
		"__veroscript.uninitialized_payload";
	inline constexpr std::uint32_t VEROSCRIPT_BC_MAGIC =
		0x43425356; // Fixed "VSBC" identifier (little endian); format version is stored separately in the header.

	/**
	 * VM instruction opcode for stack operations, control flow, and calls.
	 */
	enum class EOpcode : std::uint8_t
	{
		Nop = 0,
		ConstI32,
		ConstF64,
		ConstBool,
		ConstStr,
		LoadLocal,
		StoreLocal,
		LoadGlobal,
		StoreGlobal,
		LoadField,
		StoreField,
		LoadThisField,
		StoreThisField,
		CloneStruct,
		Add,
		Sub,
		Mul,
		Div,
		Mod,
		Eq,
		Ne,
		Lt,
		Le,
		Gt,
		Ge,
		Not,
		Neg,
		Jmp,
		JmpIfFalse,
		Call,
		CallMethod,
		CallBuiltin,
		CallNative,
		NewObject,
		NewNativeArray,
		// Push a by-value element copy (rvalue index). Prefer GetIndexRef for aliases.
		GetIndexCopy,
		GetIndex = GetIndexCopy,
		SetIndex,
		NewDynamicArray,
		NewMap,
		NewSet,
		NewQueue,
		NewStack,
		NewOrderedMap,
		NewDeque,
		NewChannel,
		LoadFieldKeep,
		Dup,
		Return,
		Pop,
		ConstNull,
		LoadThis,
		BitAnd,
		BitOr,
		BitXor,
		BitNot,
		Shl,
		Shr,
		MakeLocalRef,
		MakeGlobalRef,
		MakeThisFieldRef,
		CallBoundMethod,
		MakeClosure,
		CallIndirect,
		/**
		 * Pop index; u16 = local slot holding array/deque (or IndexRef/ref thereto).
		 * Push FIndexRef (element alias). Chained `ref a[i][j]` BindLocal's intermediate refs.
		 */
		GetIndexRef,
		MakeLocalIndexRef = GetIndexRef,
		/**
		 * Pop value into Locals[slot] without resolving through an existing ref binding.
		 * Used to rebind `for (ref T x : xs)` each iteration (StoreLocal would write-through).
		 */
		BindLocal,
		/**
		 * Pop object handle; u16 = StringPool field name. Push FThisFieldRef to that field
		 * (seeds GetIndexRef chains for `obj.field[i][j]` / `ref obj.field[i]`).
		 */
		MakeFieldRef,
		/**
		 * Pop key; u16 = local slot holding map/ordered_map (or ref thereto).
		 * Key must exist. Push FMapEntryRef (value alias).
		 */
		GetMapEntryRef,
		/**
		 * u16 = local slot. Prefer i32 ++ with CheckedAdd (no operand stack).
		 * Non-int locals fall back to full Add semantics in the VM.
		 */
		IncLocal,
		/**
		 * u16 slot + i32 imm. Prefer locals[slot] += imm for i32; other numerics use Add.
		 */
		AddLocalImm,
		/**
		 * u16 dest + u16 src. Prefer locals[dest] += locals[src] for i32; else full Add.
		 */
		AddLocalLocal,
		/**
		 * u16 slot + i32 imm + i32 rel. Jump iff !(local < imm) (peephole of Lt + JmpIfFalse).
		 * Signed/unsigned integers compare in their own domain; floating-point uses
		 * inverted Lt (NaN-safe, not >=). Unsigned compiler immediates are exact and
		 * non-negative, represented by the common i32 field.
		 */
		JmpIfLocalGeImm,
		/**
		 * Pop object; u16 = StringPool field name. Field++ with no push (i32 CheckedAdd;
		 * u32 wrap; double += 1.0; other numerics via Add).
		 * Used only inside a proven scalar numeric field update. A compiler must
		 * not replace a method call with this opcode because calls are observable
		 * through the instance-call interceptor.
		 */
		IncField,
		// Pop a reference binding and push a by-value view of its current storage.
		DerefRef,
		// Pop a value, validate it against the StringPool target type, and push it.
		// Used only by the language-level `cast<T>` / `try_cast<T>` intrinsics.
		CheckedCast,
		// Pop a value, perform the non-throwing form of a checked cast, and push
		// a freshly created optional<T>. Operands: u16 target StringPool index,
		// then u16 instantiated optional<T> Type index.
		TryCast,
		// Pop and raise a script exception value. Runtime faults remain non-catchable.
		Throw,
		// Complete a protected-region cleanup and resume its pending transfer.
		EndFinally,
		// Core memory member ordinal (u16) + argument count (u8); independent of host registrations.
		CallMemory,
		// Pop a concrete user value; u16 canonical source type in StringPool.
		BoxVariant,
		// Pop variant; u16 exact canonical payload type in StringPool; push bool.
		VariantIs,
		// Raw object storage is followed by the selected ordinary or copy
		// constructor; no field initializer or host factory runs twice.
		NewUninitializedObject,
		DefaultCopyConstruct,
		SameObject,
		// Transfer the core's owned temporary out of a local; no script move hook.
		TakeLocal,
		AssignHostBase,
		EqualHostBase,
		AssignCoreMetadata,
		CloneValueOfType,
		InitIndex,
		// Allocate fixed-array slots for InitIndex without creating their values.
		NewUninitializedNativeArray,
		// Initialize this.field from the stack and stamp its container origin,
		// without publishing an assignment mutation. Operand: u16 field name.
		InitializeThisField,
		// A guarded, declaration-site initialization of Core-owned static storage.
		BeginLocalStatic,
		CommitLocalStatic,
		// Compiler-only indexed assignment writeback; follows its local storage path.
		// User references use GetIndexRef and bind the original element identity.
		GetIndexPathRef,
	};

	/**
	 * Host builtin invoked by CallBuiltin (print, I/O, math, string helpers).
	 */
	enum class EBuiltinId : std::uint8_t
	{
		Print = 0,
		ToString,
		Len,
		Sqrt,
		Sin,
		Cos,
		ReadFile,
		WriteFile,
		DeleteFile,
		Exists,
		Substr,
		Find,
		CharCode,
		Abs,
		Min,
		Max,
		ListDir,
		ToInt,
		ToDouble,
		ToUnsigned,
		ToWString,
		Split,
		Join,
		Tan,
		Floor,
		Ceil,
		ToLower,
		ToUpper,
		ReadLine,
		Exit,
		GetEnv,
		MkDir,
		Assert,
		Eprint,
		AppendFile,
		Sleep,
		Format,
		SetEnv,
		Range,
		Cwd,
		ChDir,
		Now,
		MakeChannel,
		ChannelSend,
		ChannelRecv,
		Spawn,
		JoinTask,
		ChannelClose,
		ReadToken,
		ReadInt,
		ReadDouble,
		ReadString,
		ReadBool,
		ToInt64,
		ToUInt64,
		CancelTask,
		WeakCreate,
		WeakLock,
		WeakExpired,
		WeakReset,
		SpawnResult,
		FormatPrintf,
		ReflectionString,
		ReflectionInt,
		ReflectionBool,
		ReflectionInt64,
		ToFloat,
		ToInt8,
		ToUInt8,
		ToInt16,
		ToUInt16,
		ChannelTrySend,
		ChannelSendFor,
		ChannelTryRecv,
		ChannelRecvFor,
		ChannelIsClosed,
		ChannelSize,
		ChannelCapacity,
		ChannelSetCapacity,
		JoinTaskFor,
		TaskDone,
		// Namespace-only terminal helpers. These are intentionally distinct from
		// print/eprint: they do not append a newline and can report capability.
		ConsoleWrite,
		ConsoleError,
		ConsoleFlush,
		ConsoleClear,
		ConsoleIsInteractive,
		// Extended numeric primitives. Keep new builtin tags appended so the
		// 0.1.1 bytecode baseline has one stable, deterministic encoding.
		SteadyNow,
		Round,
		Trunc,
		Pow,
		Exp,
		Log,
		Log2,
		Asin,
		Acos,
		Atan2,
		IsFinite,
		IsNan,
		IsFile,
		IsDirectory,
		RemoveDir,
		CancelRequested,
		SpawnCallable,
		SpawnCallableResult,
		ReflectionBind,
		ReflectionGet,
		ReflectionSet,
		ReflectionInvoke,
		ReflectionInvokeStatic,
		SourceMetadataString, SourceMetadataInt, SourceMetadataBool,
		SourceMetadataInt64, SourceMetadataUInt64, SourceMetadataFloat,
		SourceMetadataNode, SourceMetadataAnnotation, SourceMetadataArgument,
		SourceMetadataComment, SourceMetadataDefinition,
		MemoryAvailable,
		MemoryAddress,
		MemoryObjectAddress,
		MemoryObjectId,
		MemoryDataAddress,
		MemoryCopy,
		MemoryFill,
		MemoryEqual,
		MemoryClone,
		MemoryFromArray,
		MemoryToArray,
		MemoryRead,
		MemoryWrite,
		// Append new console tags without renumbering existing bytecode operations.
		ConsoleSupportsColor,
		ConsoleSupportsCursor,
		ConsoleSupportsClear,
		ConsoleIsOutputTerminal,
		ConsoleSetColor,
		ConsoleResetStyle,
		ConsoleClearLine,
		ConsoleMoveCursor,
		ConsoleSetCursorVisible,
		ConsoleWidth,
		ConsoleHeight,
		MethodBindingResolve,
		MethodBindingValidate,
		MethodBindingInvoke,
		ScalarParse,
		ScalarTryParse,
		StringStartsWith,
		StringEndsWith,
		StringLastFind,
		StringRepeat,
		StringReplace,
		StringTrim,
		StringEncodeUtf8,
		StringDecodeUtf8,
		StringIsValidUtf8,
		TryReadLine,
		TryReadToken,
		ReadBytes,
		WriteBytes,
		AppendBytes,
		PathIsAbsolute,
		PathRoot,
		PathNormalize,
		TryGetEnv,
		UnsetEnv,
		NumericTypeValidate,
		MakeMutex,
		MutexLock,
		MutexTryLock,
		MutexLockFor,
		MutexUnlock,
		MakeSemaphore,
		SemaphoreAcquire,
		SemaphoreTryAcquire,
		SemaphoreAcquireFor,
		SemaphoreRelease,
		MakeSynchronized,
		SynchronizedLoad,
		SynchronizedStore,
		SynchronizedExchange,
		SynchronizedFetchAdd,
		TypedTaskBind,
		TypedTaskValid,
		TypedTaskDone,
		TypedTaskAwait,
		TypedTaskAwaitFor,
		TypedTaskCancel,
		TaskGroupAwaitAll,
		TaskGroupCancelAll,
		TaskArrayAwaitAll,
		TaskArrayCancelAll,
		ArrayReverse,
		ArrayRotate,
		ArrayShuffle,
		RandomNextUInt,
		RandomIndex,
		VariantEmpty,
		VariantHasValue,
		VariantTypeName,
		ArrayPrepareSort,
		Count,
	};

	/**
	 * Tagged constant pool entry kind, including full-width integers and typed enums.
	 * Values are serialized directly into bytecode. Keep these explicit tags stable
	 * for the lifetime of the current bytecode format.
	 */
	enum class EConstantKind : std::uint8_t
	{
		I8 = 0,
		U8 = 1,
		I16 = 2,
		U16 = 3,
		I32 = 4,
		F64 = 5,
		Bool = 6,
		Str = 7,
		U32 = 8,
		WStr = 9,
		I64 = 10,
		U64 = 11,
		Enum = 12,
		F32 = 13,
	};

	/**
	 * One entry in the module constant pool.
	 */
	struct FBytecodeConstant
	{
		EConstantKind Kind = EConstantKind::I32;
		std::int8_t I8 = 0;
		std::uint8_t U8 = 0;
		std::int16_t I16 = 0;
		std::uint16_t U16 = 0;
		std::int32_t I32 = 0;
		std::uint32_t U32 = 0;
		std::int64_t I64 = 0;
		std::uint64_t U64 = 0;
		float F32 = 0.0f;
		double F64 = 0.0;
		bool Bool = false;
		// Compiler-owned symbolic strings follow package linkage rewriting; ordinary
		// string data retains its exact bytes even when the pool storage is shared.
		bool bIsLinkageString = false;
		std::uint16_t StringIndex = 0;
	};

	/**
	 * Field metadata: name, type, optional default constant, optional init bytecode.
	 */
	struct FBytecodeFieldDesc
	{
		std::string Name;
		// VM storage/compatibility identity (transparent host aliases expanded).
		std::string TypeName;
		// Source-facing identity retained for reflection and package interfaces.
		std::string DeclaredTypeName;
		FBytecodeConstant DefaultValue;
		bool bHasDefault = false;
		std::vector<std::uint8_t> InitCode;
		bool bHasInitCode = false;
		// Source `mutable` field qualifier. It affects compile-time const access,
		// is retained for package interfaces and embed reflection, and does not
		// alter object storage.
		bool bIsMutable = false;
		// Source `const` field qualifier; required for executable reflection writes.
		bool bIsConst = false;
		// Executable reflection treats missing/unknown access as non-public.
		EMemberAccess Access = EMemberAccess::Private;
		std::uint16_t InitLocalCount = 0;
		FDeclMeta DeclMeta;
		bool bConstexpr = false;
	};

	/**
	 * User-defined struct or class type description in a bytecode module.
	 */
	enum class ESpecialMemberKind : std::uint8_t
	{
		None, CopyConstructor, CopyAssignment, Equality, Inequality
	};

	struct FBytecodeTypeDesc
	{
		struct FUsingMember
		{
			std::string BaseTypeName;
			std::string MemberName;
			EMemberAccess Access = EMemberAccess::Private;
		};

		FTemplateInstanceInfo TemplateInstance;
		FTypeDeclarationScope Scope;
		std::vector<FNestedTypeDeclaration> NestedTypes;
		std::uint16_t CopyConstructorIndex = 0xFFFF;

		EScriptObjectKind Kind = EScriptObjectKind::Class;
		std::string Name;
		// Indices into Types for direct bases. Empty means no base.
		std::vector<std::uint16_t> BaseTypeIndices;
		std::vector<EMemberAccess> BaseInheritanceAccess;
		std::vector<FUsingMember> UsingMembers;
		/**
		 * Optional direct host-class base (`class MyActor : AActor`). Mutually exclusive with
		 * mixing multiple script bases in the MVP: at most one host base, and no script base
		 * alongside it. Host fields are not flattened into Fields.
		 */
		std::string HostBaseTypeName;
		std::uint32_t HostBaseTypeId = 0;
		// Indices into FBytecodeModule::Interfaces; inherited bases come first.
		std::vector<std::uint16_t> ImplementedInterfaceIndices;
		std::vector<FBytecodeFieldDesc> Fields;
		// First field index owned by this type (after inherited base fields).
		std::uint16_t OwnFieldStartIndex = 0;
		// Index into Functions for `~Type()`, or 0xFFFF if none.
		std::uint16_t DestructorFunctionIndex = static_cast<std::uint16_t>(-1);
		bool bDestructorVirtual = false;
		bool bDestructorPureVirtual = false;
		// True when at least one effective virtual slot remains pure. Abstract
		// types are reflectable and may be used through references, but cannot be
		// instantiated by source code or the public runtime construction API.
		bool bIsAbstract = false;
		// `static class` / `static struct`: a reflectable type qualifier used only
		// as a static-member container. It has no object instances and must never
		// be constructed.
		bool bIsStatic = false;
		FDeclMeta DeclMeta;
		/**
		 * `static` data members (shared globals). Not part of instance layout / Fields.
		 * Name/TypeName/DeclMeta are for embed reflection; defaults live in InitCode globals.
		 */
		std::vector<FBytecodeFieldDesc> StaticFields;
	};

	/**
	 * One enumerator name/value in a bytecode enum table.
	 */
	struct FBytecodeEnumMemberDesc
	{
		std::string Name;
		std::int64_t Value = 0;
		// Opaque member annotations (e.g. UMETA(DisplayName=...)); embed reflection only.
		FDeclMeta DeclMeta;
	};

	/**
	 * Enum declaration preserved for embed reflection.
	 */
	struct FBytecodeEnumDesc
	{
		FTemplateInstanceInfo TemplateInstance;
		FTypeDeclarationScope Scope;
		std::string Name;
		/**
		 * True for `enum class` (scoped). Members are not bare module-scope constants;
		 * `Name.Member` and `Name::Member` both resolve. Values retain enum identity.
		 */
		bool bScoped = false;
		std::string UnderlyingTypeName = "int";
		std::vector<FBytecodeEnumMemberDesc> Members;
		FDeclMeta DeclMeta;
	};

	/**
	 * Interface type metadata. Default bodies live in Functions as Iface::Method.
	 */
	struct FBytecodeInterfaceDesc
	{
		FTemplateInstanceInfo TemplateInstance;
		FTypeDeclarationScope Scope;
		std::vector<FNestedTypeDeclaration> NestedTypes;
		std::string Name;
		// Indices into Interfaces for direct parents. Empty means no parents.
		std::vector<std::uint16_t> BaseInterfaceIndices;
		// Declared interface fields (state slice); implementors also merge these into Types[].Fields.
		std::vector<FBytecodeFieldDesc> Fields;
		/**
		 * `static` data members on the interface (shared globals). Not part of implementor layout.
		 * Name/TypeName/DeclMeta are for embed reflection; defaults live in InitCode globals.
		 */
		std::vector<FBytecodeFieldDesc> StaticFields;
		FDeclMeta DeclMeta;
	};

	/**
	 * Per-opcode debug mapping. Indexed by bytecode IP of the opcode byte.
	 * Line == 0 means no entry at that offset.
	 */
	struct FDebugLine
	{
		std::uint32_t Line = 0;
		std::uint32_t Column = 1;
		std::uint16_t FileIndex = 0;
	};

	struct FBytecodeCatchHandler
	{
		// Empty means catch (...). Otherwise this is the canonical declared type.
		std::string TypeName;
		// User-visible binding (or the hidden binding for an anonymous handler).
		std::uint16_t LocalSlot = 0;
		// Original exception object used by `throw;`. Equals LocalSlot for a
		// reference/anonymous handler; differs for a by-value catch binding.
		std::uint16_t RethrowSlot = 0;
		std::uint32_t HandlerOffset = 0;
	};

	struct FBytecodeExceptionRegion
	{
		// Protected instruction range [BeginOffset, EndOffset).
		std::uint32_t BeginOffset = 0;
		std::uint32_t EndOffset = 0;
		std::vector<FBytecodeCatchHandler> Catches;
		std::uint32_t FinallyOffset = std::numeric_limits<std::uint32_t>::max();
		std::uint32_t FinallyEndOffset = std::numeric_limits<std::uint32_t>::max();
	};

	/**
	 * Compiled function or method: signature, locals, and instruction bytecode.
	 */
	enum class EClosureCaptureMode : std::uint8_t
	{
		Value,
		MutableValue,
		Reference,
	};

	struct FBytecodeCapture
	{
		std::string Name;
		std::string TypeName;
		EClosureCaptureMode Mode = EClosureCaptureMode::Value;
		bool bWritable = false;
		bool bStableStorage = false;

		std::string BindingTypeName() const
		{
			return Mode == EClosureCaptureMode::Value ? TypeName
				: (bWritable ? "ref " : "const ref ") + TypeName;
		}

		bool operator==(const FBytecodeCapture& Other) const
		{
			return Name == Other.Name && TypeName == Other.TypeName
					&& Mode == Other.Mode && bWritable == Other.bWritable && bStableStorage == Other.bStableStorage;
		}
	};

	struct FBytecodeLocalStaticInitializer
	{
		std::uint16_t GlobalSlot = 0;
		std::uint32_t BeginOffset = 0;
		std::uint32_t CommitOffset = 0;
		std::uint32_t EndOffset = 0;
		// Lexical visibility ends here; this is distinct from the initializer guard.
		std::uint32_t ScopeEndOffset = 0;
		std::string Name;

		bool operator==(const FBytecodeLocalStaticInitializer& Other) const
		{
			return GlobalSlot == Other.GlobalSlot && BeginOffset == Other.BeginOffset
				&& CommitOffset == Other.CommitOffset && EndOffset == Other.EndOffset
				&& ScopeEndOffset == Other.ScopeEndOffset && Name == Other.Name;
		}
	};

	struct FBytecodeFunction
	{
		FTemplateInstanceInfo TemplateInstance;
		ESpecialMemberKind SpecialMemberKind = ESpecialMemberKind::None;
		bool bDefaulted = false;
		bool bExplicitCopy = false;
		// A valid default declaration may depend on unavailable subobject operations.
		// Keep its signature in lookup and diagnose only when the operation is used.
		std::string UnavailableReason;
		std::string Name;
		// Logical source module retained independently of debug information so
		// reflection remains complete in stripped/source-free packages.
		std::string ModulePath;
		bool bReturnsVoid = true;
		bool bReturnsRef = false;
		bool bIsMethod = false;
		bool bIsVirtual = false;
		// Explicit pure-virtual declaration. It remains a dispatch contract even
		// when a same-file out-of-class definition supplies callable base bytecode.
		bool bIsAbstract = false;
		// Declaration-only overload (`= delete`). It remains reflectable and in
		// overload metadata but has no executable bytecode.
		bool bDeleted = false;
		// True for a trailing const method declaration.
		bool bIsConstMethod = false;
		bool bIsStaticMethod = false;
		bool bUsesLocalStaticStorage = false;
		EMemberAccess Access = EMemberAccess::Private;
		// Part of the binary-library public surface selected by the root module's
		// export / export-import graph. Private and ordinary imported functions
		// remain linkable implementation details but are not exposed by .vslib.
		bool bPackageExported = false;
		std::uint16_t OwnerTypeIndex = 0;
		std::uint8_t ParamCount = 0;
		std::uint16_t LocalCount = 0;
		std::vector<std::uint8_t> Code;
		// Parallel to local slots; empty or shorter than LocalCount -> unnamed.
		std::vector<std::string> LocalNames;
		// Sparse: DebugLines[ip] valid when Line != 0. Size may be <= Code.size().
		std::vector<FDebugLine> DebugLines;
		// Sorted instruction offsets; execution cost metadata survives debug stripping.
		std::vector<std::uint32_t> PauseMarkerOffsets;
		std::vector<FBytecodeExceptionRegion> ExceptionRegions;
		FSourceLocation Location = {};
		// Canonical fn(...)->T for MakeClosure (in-memory; not required for call).
		std::string ClosureTypeName;
		/**
		 * Captured-local schema in the exact slot order consumed by MakeClosure.
		 * Names make hot reload resilient to capture reordering; types
		 * prevent a retained closure from binding a value to an incompatible slot.
		 */
		std::vector<FBytecodeCapture> Captures;
		std::vector<FBytecodeLocalStaticInitializer> LocalStaticInitializers;
		std::uint64_t LocalStaticSemanticIdentity = 0;
		/**
		 * Per-parameter flags; size == ParamCount when present.
		 * Bit0 = ref, Bit1 = const-ref. Empty is valid for manually built modules
		 * whose parameter reference metadata is unavailable.
		 */
		std::vector<std::uint8_t> ParamFlags;
		FDeclMeta DeclMeta;
		bool bConstexpr = false;
	};

	// ParamFlags bit0: parameter is `ref T`.
	inline constexpr std::uint8_t BytecodeParamFlagRef = 0x01u;
	// ParamFlags bit1: parameter is `const ref T`.
	inline constexpr std::uint8_t BytecodeParamFlagConstRef = 0x02u;

	inline bool BytecodeFunctionHasRefParams(const FBytecodeFunction& Function)
	{
		for (const std::uint8_t Flag : Function.ParamFlags)
		{
			if ((Flag & (BytecodeParamFlagRef | BytecodeParamFlagConstRef)) != 0)
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Runtime-only bytecode span for one top-level global initializer.
	 */
	struct FBytecodeGlobalInitRange
	{
		std::uint16_t GlobalSlot = 0;
		std::size_t BeginOffset = 0;
		std::size_t EndOffset = 0;
		bool bDemandInitialized = false;
	};

	/**
	 * Source-facing reflection data parallel to FBytecodeModule::GlobalNames.
	 * Runtime storage continues to use GlobalTypeNames; transparent aliases and
	 * declaration annotations must remain available to source-free packages.
	 */
	struct FBytecodeGlobalMeta
	{
		std::string ModulePath;
		std::string DeclaredTypeName;
		bool bConst = false;
		bool bExported = false;
		// Static data members share the global slot table but are reflected as fields.
		bool bStaticMember = false;
		bool bLocalStatic = false;
		FDeclMeta DeclMeta;
		bool bConstexpr = false;
		std::optional<FTemplateConstant> ConstantValue;
	};

	struct FBytecodePackageTemplateImport
	{
		std::string ResolvedModulePath;
		bool bReExport = false;
	};

	/**
	 * A preprocessed template module embedded only when a binary library exports
	 * an open generic. Consumers parse it at compile time; all non-generic
	 * declarations are linked bytecode stubs and are never compiled twice.
	 */
	struct FBytecodePackageTemplateModule
	{
		std::string SourceOwnerId;
		std::string ModulePath;
		std::string Source;
		std::vector<FBytecodePackageTemplateImport> Imports;
		std::string FacadePath;
		std::string OwnerComponentId;
		std::vector<FAnnotationGenerationOperation> GeneratedOperations;
		std::vector<FCompileGenerationOperation> CompileGeneratedOperations;
		std::shared_ptr<const FSourceMetadataSnapshot> InitialSourceMetadata;
		bool bDeclarationOnly = false;
		bool bFromPackage = false;
		bool bPackageSurface = false;
	};

	/**
	 * Stable identity of one library component already linked into a module.
	 * The fingerprint is computed before the component annotates itself, avoiding
	 * a self-referential hash while still detecting two different builds that
	 * incorrectly reuse the same package name/version identity.
	 */
	struct FBytecodePackageComponent
	{
		std::string Id;
		std::uint64_t Fingerprint = 0;
	};

	/**
	 * Contiguous initializer bytecode owned by one packaged component. These
	 * ranges let the linker execute a shared dependency exactly once in diamond
	 * dependency graphs. Unowned gaps are ordinary source-module initializer code.
	 */
	struct FBytecodePackageInitSegment
	{
		std::string OwnerId;
		std::size_t BeginOffset = 0;
		std::size_t EndOffset = 0;
	};

	/**
	 * Full compiled module: pools, types, functions, globals, and module init code.
	 */
	struct FBytecodeNativeReference
	{
		bool operator==(const FBytecodeNativeReference& Other) const
		{
			return Name == Other.Name && Signature == Other.Signature && ProviderId == Other.ProviderId
			    && ArgumentCount == Other.ArgumentCount;
		}

		std::string Name;
		std::string Signature;
		std::string ProviderId;
		std::uint16_t ArgumentCount = 0;
	};

	struct FBytecodeModule
	{
		struct FCompiledConstant
		{
			std::string Name;
			FSourceLocation Location;
			FTemplateConstant Value;
		};
		struct FCompiledDeclarationType
		{
			std::string Name;
			FSourceLocation Location;
			std::string TypeName;
		};
		struct FCompiledConstantBranch
		{
			std::string Identity;
			bool bThen = false;
		};

		// Compilation-only lowering data for source exporters. Runtime packages
		// retain literal bytecode and their normal constexpr source dependency graph.
		std::vector<FCompiledConstant> CompiledConstants;
		// Resolved local/global array types for standalone source emission only.
		std::vector<FCompiledDeclarationType> CompiledDeclarationTypes;
		// Transient source-export lowering only; packages store selected bytecode.
		std::vector<FCompiledConstantBranch> CompiledConstantBranches;

		std::vector<FTemplateDeclarationInfo> TemplateDeclarations;
		std::vector<FExternalDependency> RequiredExternalDependencies;
		// CallNative indexes this stable symbol table. Runtime slots are resolved
		// against the host's current registrations before any initialization.
		std::vector<FBytecodeNativeReference> NativeReferences;
		std::shared_ptr<const FSourceMetadataSnapshot> SourceMetadata;
		std::vector<FPublicMacro> PublicMacros;
		// Runtime source locations contain string_view file labels. Preserve the
		// parser backing storage for in-memory compiled modules; serialized modules
		// rebuild their labels from owned DebugFiles and leave this empty.
		std::vector<std::shared_ptr<const void>> LifetimeAnchors;
		FSemanticVersion Version = VEROSCRIPT_BC_VERSION;
		// Logical root compilation unit. Reflection uses this to avoid exposing
		// private declarations belonging to ordinary imports or binary libraries.
		std::string EntryModulePath;
		std::vector<std::string> StringPool;
		std::vector<std::u16string> WStringPool;
		std::vector<FBytecodeConstant> Constants;
		std::vector<FBytecodeTypeDesc> Types;
		// Interface table, written after Types.
		std::vector<FBytecodeInterfaceDesc> Interfaces;
		// Enum metadata; scoped enum constants also carry their runtime type identity.
		std::vector<FBytecodeEnumDesc> Enums;
		std::deque<FBytecodeFunction> Functions;
		std::unordered_map<std::string, std::uint16_t> FunctionIndices;
		std::vector<std::string> GlobalNames;
		/**
		 * Canonical declared type parallel to GlobalNames. Serialized for JIT type
		 * specialization and source-free state-preserving package reload.
		 */
		std::vector<std::string> GlobalTypeNames;
		// Empty only for deliberately hand-authored in-memory fixtures. Compilers,
		// serializers, packages, and linkers keep this parallel to GlobalNames.
		std::vector<FBytecodeGlobalMeta> GlobalMetadata;
		std::unordered_map<std::string, std::uint16_t> GlobalSlots;
		std::vector<std::uint8_t> InitCode;
		/**
		 * Top-level / static-field initializer spans used to skip retained-global
		 * RHS evaluation during hot reload. Sorted by BeginOffset (longer End first
		 * on ties) for stable tooling iteration. The VM builds a punch-hole byte map
		 * from these ranges (not a binary search). Empty spans (End <= Begin) are
		 * never recorded. Serialized so source-free package reloads retain identical
		 * initializer migration semantics.
		 */
		std::vector<FBytecodeGlobalInitRange> GlobalInitRanges;
		std::uint16_t InitLocalCount = 0;
		std::vector<FDebugLine> InitDebugLines;
		std::vector<std::uint32_t> InitPauseMarkerOffsets;
		// Owned file labels for FDebugLine::FileIndex.
		std::vector<std::string> DebugFiles;
		std::int32_t MainFunctionIndex = -1;
		/**
		 * Parser-readable declaration surface embedded in a binary library.
		 * It contains declarations only; executable bodies remain in Functions.
		 */
		std::string PackageInterfaceSource;
		// Non-empty when source exports cannot be represented safely in a package.
		// Source execution remains valid; library packaging reports this message.
		std::string PackageInterfaceError;
		// Complete template/module graph required to instantiate exported open
		// generics without accessing the original source tree.
		std::vector<FBytecodePackageTemplateModule> PackageTemplateModules;
		// Package ownership is parallel to the corresponding definition tables.
		// A shorter vector means newly compiled/unpackaged entries have no owner.
		std::vector<FBytecodePackageComponent> PackageComponents;
		std::vector<std::string> PackageTypeOwners;
		std::vector<std::string> PackageInterfaceOwners;
		std::vector<std::string> PackageEnumOwners;
		std::vector<std::string> PackageFunctionOwners;
		std::vector<std::string> PackageGlobalOwners;
		std::vector<FBytecodePackageInitSegment> PackageInitSegments;
		// The module statically contains one or more binary-package dependencies.
		// This is retained for diagnostics, tooling, and package graph inspection.
		bool bContainsPackageDependencies = false;
	};

	namespace Bytecode
	{
		inline void WriteU8(std::vector<std::uint8_t>& Buffer, const std::uint8_t Value)
		{
			Buffer.push_back(Value);
		}

		inline void WriteU16LE(std::vector<std::uint8_t>& Buffer, const std::uint16_t Value)
		{
			Buffer.push_back(static_cast<std::uint8_t>(Value & 0xFFu));
			Buffer.push_back(static_cast<std::uint8_t>((Value >> 8) & 0xFFu));
		}

		inline void WriteI32LE(std::vector<std::uint8_t>& Buffer, const std::int32_t Value)
		{
			const auto Unsigned = static_cast<std::uint32_t>(Value);
			Buffer.push_back(static_cast<std::uint8_t>(Unsigned & 0xFFu));
			Buffer.push_back(static_cast<std::uint8_t>((Unsigned >> 8) & 0xFFu));
			Buffer.push_back(static_cast<std::uint8_t>((Unsigned >> 16) & 0xFFu));
			Buffer.push_back(static_cast<std::uint8_t>((Unsigned >> 24) & 0xFFu));
		}

		inline void WriteU32LE(std::vector<std::uint8_t>& Buffer, const std::uint32_t Value)
		{
			const auto Unsigned = Value;
			Buffer.push_back(static_cast<std::uint8_t>(Unsigned & 0xFFu));
			Buffer.push_back(static_cast<std::uint8_t>((Unsigned >> 8) & 0xFFu));
			Buffer.push_back(static_cast<std::uint8_t>((Unsigned >> 16) & 0xFFu));
			Buffer.push_back(static_cast<std::uint8_t>((Unsigned >> 24) & 0xFFu));
		}

		inline void WriteU64LE(std::vector<std::uint8_t>& Buffer, const std::uint64_t Value)
		{
			for (int Shift = 0; Shift < 64; Shift += 8)
			{
				Buffer.push_back(static_cast<std::uint8_t>((Value >> Shift) & 0xFFu));
			}
		}

		inline void WriteF64LE(std::vector<std::uint8_t>& Buffer, const double Value)
		{
			std::uint64_t Bits = 0;
			static_assert(sizeof(double) == sizeof(std::uint64_t));
			std::memcpy(&Bits, &Value, sizeof(double));
			for (int Shift = 0; Shift < 64; Shift += 8)
			{
				Buffer.push_back(static_cast<std::uint8_t>((Bits >> Shift) & 0xFFu));
			}
		}

		inline void WriteF32LE(std::vector<std::uint8_t>& Buffer, const float Value)
		{
			std::uint32_t Bits = 0;
			static_assert(sizeof(float) == sizeof(std::uint32_t));
			std::memcpy(&Bits, &Value, sizeof(float));
			WriteU32LE(Buffer, Bits);
		}

		inline std::uint16_t ReadU16LE(const std::uint8_t* Data)
		{
			return static_cast<std::uint16_t>(Data[0])
				| (static_cast<std::uint16_t>(Data[1]) << 8);
		}

		inline std::uint32_t ReadU32LE(const std::uint8_t* Data)
		{
			return static_cast<std::uint32_t>(Data[0])
				| (static_cast<std::uint32_t>(Data[1]) << 8)
				| (static_cast<std::uint32_t>(Data[2]) << 16)
				| (static_cast<std::uint32_t>(Data[3]) << 24);
		}

		inline std::uint64_t ReadU64LE(const std::uint8_t* Data)
		{
			std::uint64_t Value = 0;
			for (int Shift = 0; Shift < 64; Shift += 8)
			{
				Value |= static_cast<std::uint64_t>(Data[Shift >> 3]) << Shift;
			}
			return Value;
		}

		inline std::int32_t ReadI32LE(const std::uint8_t* Data)
		{
			const auto Unsigned = static_cast<std::uint32_t>(Data[0])
				| (static_cast<std::uint32_t>(Data[1]) << 8)
				| (static_cast<std::uint32_t>(Data[2]) << 16)
				| (static_cast<std::uint32_t>(Data[3]) << 24);
			return static_cast<std::int32_t>(Unsigned);
		}

		inline double ReadF64LE(const std::uint8_t* Data)
		{
			std::uint64_t Bits = 0;
			for (int Shift = 0; Shift < 64; Shift += 8)
			{
				Bits |= static_cast<std::uint64_t>(Data[Shift >> 3]) << Shift;
			}
			double Value = 0.0;
			std::memcpy(&Value, &Bits, sizeof(double));
			return Value;
		}

		inline float ReadF32LE(const std::uint8_t* Data)
		{
			const std::uint32_t Bits = ReadU32LE(Data);
			float Value = 0.0f;
			std::memcpy(&Value, &Bits, sizeof(float));
			return Value;
		}

		constexpr std::size_t OpcodeOperandSize(const EOpcode Opcode)
		{
			switch (Opcode)
			{
			case EOpcode::ConstI32:
			case EOpcode::ConstF64:
			case EOpcode::ConstStr:
			case EOpcode::LoadLocal:
			case EOpcode::StoreLocal:
			case EOpcode::LoadGlobal:
			case EOpcode::StoreGlobal:
			case EOpcode::CommitLocalStatic:
			case EOpcode::LoadField:
			case EOpcode::LoadFieldKeep:
			case EOpcode::StoreField:
			case EOpcode::LoadThisField:
			case EOpcode::StoreThisField:
			case EOpcode::InitializeThisField:
			case EOpcode::NewObject:
			case EOpcode::NewUninitializedObject:
			case EOpcode::TakeLocal:
			case EOpcode::NewNativeArray:
			case EOpcode::NewUninitializedNativeArray:
			case EOpcode::NewDynamicArray:
			case EOpcode::NewMap:
			case EOpcode::NewSet:
			case EOpcode::NewQueue:
			case EOpcode::NewStack:
			case EOpcode::NewOrderedMap:
			case EOpcode::NewDeque:
			case EOpcode::NewChannel:
			case EOpcode::MakeLocalRef:
			case EOpcode::MakeGlobalRef:
			case EOpcode::MakeThisFieldRef:
			case EOpcode::MakeFieldRef:
			case EOpcode::GetIndexRef:
			case EOpcode::GetIndexPathRef:
			case EOpcode::GetMapEntryRef:
			case EOpcode::BindLocal:
			case EOpcode::IncLocal:
			case EOpcode::IncField:
			case EOpcode::CheckedCast:
			case EOpcode::BoxVariant:
			case EOpcode::VariantIs:
				return 2;
			case EOpcode::AddLocalLocal:
			case EOpcode::TryCast:
			case EOpcode::CloneValueOfType:
				return 4;
			case EOpcode::AddLocalImm:
			case EOpcode::BeginLocalStatic:
				return 6;
			case EOpcode::JmpIfLocalGeImm:
				return 10; // u16 + i32 + i32
			case EOpcode::ConstBool:
				return 1;
			case EOpcode::Jmp:
			case EOpcode::JmpIfFalse:
				return 4;
			case EOpcode::Call:
			case EOpcode::CallMethod:
			case EOpcode::CallBoundMethod:
			case EOpcode::CallNative:
			case EOpcode::CallMemory:
				return 3;
			case EOpcode::CallBuiltin:
				return 2;
			case EOpcode::MakeClosure:
				return 3;
			case EOpcode::CallIndirect:
				return 1;
			default:
				return 0;
			}
		}

		constexpr std::size_t InstructionSize(const EOpcode Opcode)
		{
			return 1 + OpcodeOperandSize(Opcode);
		}
	}
}
