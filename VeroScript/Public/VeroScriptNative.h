// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptMemory.h"
#include "VeroScriptExternalDependency.h"
#include "VeroScriptI18n.h"

#include "VeroScriptValue.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace VeroScript
{
	class IScriptCallInvoker;
	class FValueRoot;
	class IMemoryCallServices;
	class INumericCallServices;
	struct FBytecodeModule;
	struct FBytecodeNativeReference;

	/**
	 * Supported native parameter / return types for RegisterNative / host members.
	 */
	enum class ENativeType : std::uint8_t
	{
		Void = 0,
		Int = 1,
		Bool = 2,
		Double = 3,
		String = 4,
		Unsigned = 5,
		// Opaque host object (`FHostObjectHandle`).
		HostObject = 6,
		// Script `array<T>` (`FDynamicArrayHandle`). Element type is named separately
		// (ReturnElementTypeName / ParamElementTypeNames), mirroring HostObject names.
		Array = 7,
		// Script `map<K,V>` (`FMapHandle`). Key/value names are separate parallels.
		Map = 8,
		// Script `set<T>` (`FSetHandle`). Element type reuses ElementTypeName fields.
		Set = 9,
		Int64 = 10,
		UInt64 = 11,
		// Strong script `enum class` (`FEnumValue`). The enum type name is carried
		// through the named-type metadata used by HostObject.
		Enum = 12,
		Queue = 13,
		Stack = 14,
		OrderedMap = 15,
		Deque = 16,
		Channel = 17,
		// Append-only ABI values: both reuse the existing map handle variants.
		MultiMap = 18,
		OrderedMultiMap = 19,
		PriorityQueue = 20,
		MultiSet = 21,
		OrderedMultiSet = 22,
		// First-class script callable (`FFunctionHandle`). The canonical
		// `fn(...)->T` signature is carried in the named-type metadata.
		Function = 23,
		// IEEE-754 binary32. Kept distinct from Double in FValue, bytecode,
		// overload resolution, containers, and the host ABI.
		Float = 24,
		Int8 = 25,
		UInt8 = 26,
		Int16 = 27,
		UInt16 = 28,
		Variant = 29,
	};

	/**
	 * Structured native type metadata. Prefer this over manually maintaining the
	 * parallel HostTypeName / ElementTypeName / KeyTypeName vectors.
	 */
	struct FNativeTypeDesc
	{
		static FNativeTypeDesc Variant()
		{
			return Scalar(ENativeType::Variant);
		}

		static FNativeTypeDesc Scalar(const ENativeType TypeValue)
		{
			FNativeTypeDesc Desc;
			Desc.Type = TypeValue;
			return Desc;
		}

		static FNativeTypeDesc Host(std::string TypeName)
		{
			FNativeTypeDesc Desc;
			Desc.Type = ENativeType::HostObject;
			Desc.HostTypeName = std::move(TypeName);
			return Desc;
		}

		static FNativeTypeDesc Enum(std::string TypeName)
		{
			FNativeTypeDesc Desc;
			Desc.Type = ENativeType::Enum;
			Desc.HostTypeName = std::move(TypeName);
			return Desc;
		}

		static FNativeTypeDesc Function(std::string Signature)
		{
			FNativeTypeDesc Desc;
			Desc.Type = ENativeType::Function;
			Desc.HostTypeName = std::move(Signature);
			return Desc;
		}

		static FNativeTypeDesc ArrayOf(std::string ElementType)
		{
			FNativeTypeDesc Desc;
			Desc.Type = ENativeType::Array;
			Desc.ElementTypeName = std::move(ElementType);
			return Desc;
		}

		static FNativeTypeDesc ArrayOf(FNativeTypeDesc ElementType)
		{
			FNativeTypeDesc Desc;
			Desc.Type = ENativeType::Array;
			Desc.TypeArguments.push_back(std::move(ElementType));
			return Desc;
		}

		static FNativeTypeDesc SetOf(std::string ElementType)
		{
			FNativeTypeDesc Desc;
			Desc.Type = ENativeType::Set;
			Desc.ElementTypeName = std::move(ElementType);
			return Desc;
		}

		static FNativeTypeDesc SetOf(FNativeTypeDesc ElementType)
		{
			FNativeTypeDesc Desc;
			Desc.Type = ENativeType::Set;
			Desc.TypeArguments.push_back(std::move(ElementType));
			return Desc;
		}

		static FNativeTypeDesc MapOf(std::string KeyType, std::string ValueType)
		{
			FNativeTypeDesc Desc;
			Desc.Type = ENativeType::Map;
			Desc.KeyTypeName = std::move(KeyType);
			Desc.ValueTypeName = std::move(ValueType);
			return Desc;
		}

		static FNativeTypeDesc MapOf(FNativeTypeDesc KeyType, FNativeTypeDesc ValueType)
		{
			FNativeTypeDesc Desc;
			Desc.Type = ENativeType::Map;
			Desc.TypeArguments.push_back(std::move(KeyType));
			Desc.TypeArguments.push_back(std::move(ValueType));
			return Desc;
		}

		static FNativeTypeDesc QueueOf(FNativeTypeDesc ElementType)
		{
			FNativeTypeDesc Desc;
			Desc.Type = ENativeType::Queue;
			Desc.TypeArguments.push_back(std::move(ElementType));
			return Desc;
		}

		static FNativeTypeDesc StackOf(FNativeTypeDesc ElementType)
		{
			FNativeTypeDesc Desc;
			Desc.Type = ENativeType::Stack;
			Desc.TypeArguments.push_back(std::move(ElementType));
			return Desc;
		}

		static FNativeTypeDesc OrderedMapOf(
		    FNativeTypeDesc KeyType,
		    FNativeTypeDesc ValueType)
		{
			FNativeTypeDesc Desc;
			Desc.Type = ENativeType::OrderedMap;
			Desc.TypeArguments.push_back(std::move(KeyType));
			Desc.TypeArguments.push_back(std::move(ValueType));
			return Desc;
		}

		static FNativeTypeDesc PriorityQueueOf(FNativeTypeDesc ElementType)
		{
			FNativeTypeDesc Desc;
			Desc.Type = ENativeType::PriorityQueue;
			Desc.TypeArguments.push_back(std::move(ElementType));
			return Desc;
		}

		static FNativeTypeDesc MultiSetOf(FNativeTypeDesc ElementType)
		{
			FNativeTypeDesc Desc;
			Desc.Type = ENativeType::MultiSet;
			Desc.TypeArguments.push_back(std::move(ElementType));
			return Desc;
		}

		static FNativeTypeDesc OrderedMultiSetOf(FNativeTypeDesc ElementType)
		{
			FNativeTypeDesc Desc;
			Desc.Type = ENativeType::OrderedMultiSet;
			Desc.TypeArguments.push_back(std::move(ElementType));
			return Desc;
		}

		static FNativeTypeDesc MultiMapOf(
		    FNativeTypeDesc KeyType,
		    FNativeTypeDesc ValueType)
		{
			FNativeTypeDesc Desc;
			Desc.Type = ENativeType::MultiMap;
			Desc.TypeArguments.push_back(std::move(KeyType));
			Desc.TypeArguments.push_back(std::move(ValueType));
			return Desc;
		}

		static FNativeTypeDesc OrderedMultiMapOf(
		    FNativeTypeDesc KeyType,
		    FNativeTypeDesc ValueType)
		{
			FNativeTypeDesc Desc;
			Desc.Type = ENativeType::OrderedMultiMap;
			Desc.TypeArguments.push_back(std::move(KeyType));
			Desc.TypeArguments.push_back(std::move(ValueType));
			return Desc;
		}

		static FNativeTypeDesc DequeOf(FNativeTypeDesc ElementType)
		{
			FNativeTypeDesc Desc;
			Desc.Type = ENativeType::Deque;
			Desc.TypeArguments.push_back(std::move(ElementType));
			return Desc;
		}

		static FNativeTypeDesc ChannelOf(FNativeTypeDesc ElementType)
		{
			FNativeTypeDesc Desc;
			Desc.Type = ENativeType::Channel;
			Desc.TypeArguments.push_back(std::move(ElementType));
			return Desc;
		}

		ENativeType Type = ENativeType::Void;
		// HostObject / Enum type name, or canonical Function signature.
		std::string HostTypeName;
		// Array / Set element type.
		std::string ElementTypeName;
		// Map key/value types.
		std::string KeyTypeName;
		std::string ValueTypeName;
		/**
		 * Recursive type arguments. New code should use these instead of embedding
		 * nested containers in the legacy string fields. Unary containers have one
		 * argument; Map/OrderedMap have key then value.
		 */
		std::vector<FNativeTypeDesc> TypeArguments;
	};

	struct FNativeParamDesc
	{
		FNativeTypeDesc Type;
		// NativeParamFlagRef / NativeParamFlagConstRef.
		std::uint8_t Flags = 0;
	};

	/**
	 * Arguments passed to a host native callback.
	 */
	struct FNativeCallContext
	{
		// Use for intermediate replacements that may own script values. Raw
		// MutableArg pointers remain valid for the callback, then are validated.
		void AssignValue(FValue& Destination, FValue Value) const
		{
			if (DeferredValues)
			{
				DeferredValues->push_back(std::move(Destination));
			}
			Destination = std::move(Value);
		}
		std::vector<FValue>* DeferredValues = nullptr;
		FValue* MutableArg(const std::size_t Index) const
		{
			return MutableArgs && Index < ArgCount ? MutableArgs[Index] : nullptr;
		}

		const FValue* Args = nullptr;
		/**
		 * Parallel to Args. A slot is non-null only for a mutable `ref`
		 * parameter. Hosts may replace the pointed-to FValue; the VM validates
		 * every replacement and, when the call supplied addressable `ref` storage,
		 * writes it back only after the callback succeeds. Legacy direct container
		 * arguments retain their existing shared-handle mutation semantics, but a
		 * whole-handle replacement has no caller slot to update.
		 */
		FValue* const* MutableArgs = nullptr;
		std::size_t ArgCount = 0;
		/**
		 * Concrete type arguments supplied at this call site. Only populated for
		 * generic host-object cast natives; ordinary natives receive an empty view.
		 */
		const std::string* TypeArguments = nullptr;
		std::size_t TypeArgumentCount = 0;
		void* UserData = nullptr;
		/**
		 * For an instance host member, set to the implicit receiver HostObject's
		 * Payload (nullptr if the handle is empty). Always null for a static host
		 * method, even when its first explicit parameter is HostObject.
		 */
		void* HostThis = nullptr;
		/**
		 * Non-owning active-VM invoker. Valid only until the native callback returns.
		 * Hosts may use it to execute a typed function value or a loaded free function
		 * without recursively entering Session::Run().
		 */
		IScriptCallInvoker* ScriptInvoker = nullptr;
		// Borrowed for this callback only. Args/MutableArgs are prepared copies.
		IMemoryCallServices* MemoryServices = nullptr;
		// Both are borrowed for this callback only. Failure diagnostics are owned
		// by the caller and submitted only when the callback returns false.
		I18n::II18nCallServices* I18nServices = nullptr;
		FScriptError* DiagnosticOutput = nullptr;
		INumericCallServices* NumericServices = nullptr;
	};

	/**
	 * Host callback: return false and set OutError on failure.
	 * For ENativeType::Void, OutReturn is ignored.
	 * A callback reachable from a spawned worker may run concurrently on that
	 * worker thread. UserData and captured host state must remain valid for the
	 * complete synchronous VM/session execution and protect shared mutations.
	 */
	// FNativeFunction stores this callback alias, so it must be defined first.
	using FNativeCallback = FHostValueCallback;

	/**
	 * One registered native function (owned by FHostBindingRegistry / Session).
	 */
	struct FNativeFunction
	{
		std::shared_ptr<const void> ProviderIdentity;
		FBindingOrigin Origin;
		std::string Name;
		ENativeType ReturnType = ENativeType::Void;
		std::vector<ENativeType> ParamTypes;
		FNativeCallback Callback = nullptr;
		void* UserData = nullptr;
		/**
		 * Default script type name for HostObject slots (receiver / ctor return).
		 * Used when ReturnHostTypeName / ParamHostTypeNames entries are empty.
		 */
		std::string HostTypeName;
		// HostObject / Enum type name or canonical Function signature.
		// Empty HostObject return falls back to HostTypeName.
		std::string ReturnHostTypeName;
		// Concrete generic HostObject return identity, when applicable.
		std::uint32_t ReturnGenericTemplateId = 0;
		std::vector<std::string> ReturnGenericTypeArguments;
		// Parallel to ParamTypes for HostObject / Enum / Function slots.
		// Empty HostObject entry falls back to HostTypeName.
		std::vector<std::string> ParamHostTypeNames;
		/**
		 * Element type name for unary containers (Array, Set, Queue, Stack,
		 * Deque, Channel, PriorityQueue, and the multiset variants).
		 */
		std::string ReturnElementTypeName;
		// Parallel to ParamTypes for unary-container slots.
		std::vector<std::string> ParamElementTypeNames;
		// Key/value names for binary map and multimap variants.
		std::string ReturnKeyTypeName;
		std::string ReturnValueTypeName;
		// Parallel to ParamTypes for Map slots.
		std::vector<std::string> ParamKeyTypeNames;
		std::vector<std::string> ParamValueTypeNames;
		/**
		 * Fully composed runtime identities after recursively expanding transparent
		 * host aliases. The declared metadata above remains unchanged for compiler,
		 * tooling, and ABI reflection. Empty entries retain compatibility with
		 * manually supplied native tables.
		 */
		std::string RuntimeReturnTypeName;
		std::vector<std::string> RuntimeParamTypeNames;
		/**
		 * Parallel to ParamTypes. Same encoding as bytecode ParamFlags:
		 * bit0 = ref, bit1 = const-ref (const ref sets both). Empty = all by-value.
		 * Any non-void parameter may be a ref. Mutable callback access is exposed
		 * through FNativeCallContext::MutableArg.
		 */
		std::vector<std::uint8_t> ParamFlags;
		/**
		 * Explicit host-member identity. Static methods have no implicit receiver;
		 * every ParamTypes entry is a script-visible argument. The compiler retains
		 * its legacy parameter-shape fallback when this is false so manually supplied
		 * native tables remain compatible.
		 */
		bool bHostStaticMethod = false;
		/**
		 * Generic host-object cast contract. The script-visible signature is
		 * `ScriptReturnTypeName Name<TypeParams...>(ParamTypes...)`; concrete type
		 * arguments are passed through FNativeCallContext without appearing in
		 * ParamTypes. The callback may return a host handle, a script object handle,
		 * or an empty host handle for a failed cast.
		 */
		std::vector<std::string> TypeParams;
		std::string ScriptReturnTypeName;
		bool bGenericHostObjectCast = false;
		/**
		 * Lossless recursive metadata retained by the structured registration API.
		 * Legacy/manual tables leave bHasStructuredTypeDescs false and continue to
		 * use the parallel string fields above.
		 */
		FNativeTypeDesc StructuredReturnType;
		std::vector<FNativeTypeDesc> StructuredParamTypes;
		bool bHasStructuredTypeDescs = false;
		// Optional owned bridge state copied into host value protocol stamps.
		std::shared_ptr<void> CallbackOwner;
	};

	inline std::size_t NativeRuntimeArgumentCount(const FNativeFunction& Native)
	{
		return Native.ParamTypes.size() + (Native.bGenericHostObjectCast ? Native.TypeParams.size() : 0u);
	}

	// Keep an existing bridge alive until before its owning module is released.
	std::shared_ptr<void> JoinCallbackOwners(std::shared_ptr<void> Module, std::shared_ptr<void> Bridge);

	// Match VeroScriptBytecode.h ParamFlags (kept here so Native.h stays independent).
	inline constexpr std::uint8_t NativeParamFlagRef = 0x01u;
	inline constexpr std::uint8_t NativeParamFlagConstRef = 0x02u;

	inline std::uint8_t NativeParamFlagAt(
	    const FNativeFunction& Native,
	    const std::size_t ParamIndex)
	{
		if (ParamIndex >= Native.ParamFlags.size())
		{
			return 0;
		}
		return Native.ParamFlags[ParamIndex];
	}

	inline bool NativeParamFlagIsRef(const std::uint8_t Flag)
	{
		return (Flag & NativeParamFlagRef) != 0;
	}

	inline bool NativeParamFlagIsMutableRef(const std::uint8_t Flag)
	{
		return NativeParamFlagIsRef(Flag) && (Flag & NativeParamFlagConstRef) == 0;
	}

	inline bool NativeTypeIsContainer(const ENativeType Type)
	{
		return Type == ENativeType::Array || Type == ENativeType::Map || Type == ENativeType::Set || Type == ENativeType::Queue || Type == ENativeType::Stack || Type == ENativeType::OrderedMap || Type == ENativeType::Deque || Type == ENativeType::Channel || Type == ENativeType::MultiMap || Type == ENativeType::OrderedMultiMap || Type == ENativeType::PriorityQueue || Type == ENativeType::MultiSet || Type == ENativeType::OrderedMultiSet;
	}

	inline bool NativeTypeIsBinaryMap(const ENativeType Type)
	{
		return Type == ENativeType::Map || Type == ENativeType::OrderedMap || Type == ENativeType::MultiMap || Type == ENativeType::OrderedMultiMap;
	}

	inline bool NativeTypeSupportsRef(const ENativeType Type)
	{
		return Type != ENativeType::Void;
	}

	/**
	 * Validate raw native metadata as a defensive boundary. Registry-created
	 * entries are checked while registering, but callers may also pass a public
	 * FNativeFunction table directly to the compiler / VM.
	 */
	inline bool NativeParamFlagsAreValid(const FNativeFunction& Native)
	{
		if (Native.ParamFlags.empty())
		{
			return true;
		}
		if (Native.ParamFlags.size() != Native.ParamTypes.size())
		{
			return false;
		}
		constexpr std::uint8_t KnownFlags =
		    NativeParamFlagRef | NativeParamFlagConstRef;
		for (std::size_t Index = 0; Index < Native.ParamFlags.size(); ++Index)
		{
			const std::uint8_t Flag = Native.ParamFlags[Index];
			if ((Flag & static_cast<std::uint8_t>(~KnownFlags)) != 0)
			{
				return false;
			}
			if ((Flag & NativeParamFlagConstRef) != 0 && (Flag & NativeParamFlagRef) == 0)
			{
				return false;
			}
			if (Flag != 0 && !NativeTypeSupportsRef(Native.ParamTypes[Index]))
			{
				return false;
			}
		}
		return true;
	}

	inline std::string MakeNativeArrayTypeName(const std::string_view ElementTypeName)
	{
		std::string Name;
		Name.reserve(ElementTypeName.size() + 8);
		Name += "array<";
		Name.append(ElementTypeName.data(), ElementTypeName.size());
		Name += '>';
		return Name;
	}

	inline std::string MakeNativeSetTypeName(const std::string_view ElementTypeName)
	{
		std::string Name;
		Name.reserve(ElementTypeName.size() + 6);
		Name += "set<";
		Name.append(ElementTypeName.data(), ElementTypeName.size());
		Name += '>';
		return Name;
	}

	inline std::string MakeNativeMapTypeName(
	    const std::string_view KeyTypeName,
	    const std::string_view ValueTypeName)
	{
		std::string Name;
		Name.reserve(KeyTypeName.size() + ValueTypeName.size() + 7);
		Name += "map<";
		Name.append(KeyTypeName.data(), KeyTypeName.size());
		Name += ',';
		Name.append(ValueTypeName.data(), ValueTypeName.size());
		Name += '>';
		return Name;
	}

	inline std::string MakeNativeUnaryContainerTypeName(
	    const std::string_view Container,
	    const std::string_view ElementTypeName)
	{
		std::string Name;
		Name.reserve(Container.size() + ElementTypeName.size() + 2);
		Name.append(Container.data(), Container.size());
		Name += '<';
		Name.append(ElementTypeName.data(), ElementTypeName.size());
		Name += '>';
		return Name;
	}

	inline std::string MakeNativeBinaryContainerTypeName(
	    const std::string_view Container,
	    const std::string_view KeyTypeName,
	    const std::string_view ValueTypeName)
	{
		std::string Name;
		Name.reserve(Container.size() + KeyTypeName.size() + ValueTypeName.size() + 3);
		Name.append(Container.data(), Container.size());
		Name += '<';
		Name.append(KeyTypeName.data(), KeyTypeName.size());
		Name += ',';
		Name.append(ValueTypeName.data(), ValueTypeName.size());
		Name += '>';
		return Name;
	}

	// Source compatibility for hosts built against the original Array-only ABI.
	[[deprecated("Use MakeNativeArrayTypeName")]]
	inline std::string MakeNativeDynamicArrayTypeName(const std::string_view ElementTypeName)
	{
		return MakeNativeArrayTypeName(ElementTypeName);
	}

	[[deprecated("Use MakeNativeSetTypeName")]]
	inline std::string MakeNativeDynamicSetTypeName(const std::string_view ElementTypeName)
	{
		return MakeNativeSetTypeName(ElementTypeName);
	}

	[[deprecated("Use MakeNativeMapTypeName")]]
	inline std::string MakeNativeDynamicMapTypeName(
	    const std::string_view KeyTypeName,
	    const std::string_view ValueTypeName)
	{
		return MakeNativeMapTypeName(KeyTypeName, ValueTypeName);
	}

	inline std::string_view TrimNativeTypeName(std::string_view TypeName)
	{
		while (!TypeName.empty() && (TypeName.front() == ' ' || TypeName.front() == '\t' || TypeName.front() == '\r' || TypeName.front() == '\n'))
		{
			TypeName.remove_prefix(1);
		}
		while (!TypeName.empty() && (TypeName.back() == ' ' || TypeName.back() == '\t' || TypeName.back() == '\r' || TypeName.back() == '\n'))
		{
			TypeName.remove_suffix(1);
		}
		return TypeName;
	}

	// Allocation-free aliases for a trimmed scalar name. The result borrows the
	// input or a static canonical spelling; generic names are handled below.
	inline std::string_view CanonicalizeNativeScalarTypeName(const std::string_view TypeName)
	{
		if (TypeName == "std::variant" || TypeName == "std.variant") return "variant";
		if (TypeName == "long") return "int64";
		if (TypeName == "unsigned long") return "uint64";
		if (TypeName == "int8") return "char";
		if (TypeName == "uint8") return "unsigned char";
		if (TypeName == "int16") return "short";
		if (TypeName == "uint16") return "unsigned short";
		if (TypeName == "int32") return "int";
		if (TypeName == "uint32" || TypeName == "unsigned") return "unsigned int";
		return TypeName;
	}

	// Map host aliases recursively to the script canonical spelling. Nested
	// container metadata is part of the public native ABI, so aliases inside
	// array<...>, map<...,...>, and set<...> must compare consistently too.
	inline std::string CanonicalizeNativeElementTypeName(
	    const std::string_view ElementTypeName,
	    const std::size_t RecursionDepth = 0)
	{
		if (RecursionDepth > 64)
		{
			return {};
		}
		const std::string_view TypeName = TrimNativeTypeName(ElementTypeName);
		const auto Scalar = CanonicalizeNativeScalarTypeName(TypeName);
		if (Scalar != TypeName) return std::string(Scalar);
		// Reject excessive generic nesting before recursive canonicalization. An
		// empty recursive result is also used to mean "not this container kind",
		// so performing this scan at the outer boundary prevents an over-depth
		// child from falling through and returning the original untrusted string.
		std::size_t GenericDepth = 0;
		for (std::size_t Index = 0; Index < TypeName.size(); ++Index)
		{
			if (TypeName[Index] == '<')
			{
				if (++GenericDepth > 64)
				{
					return {};
				}
			}
			else if (TypeName[Index] == '>' && (Index == 0 || TypeName[Index - 1] != '-'))
			{
				if (GenericDepth > 0)
				{
					--GenericDepth;
				}
			}
		}
		const auto CanonicalizeUnary =
		    [&TypeName, RecursionDepth](const std::string_view Prefix) -> std::string
		{
			if (TypeName.size() <= Prefix.size() + 2 || TypeName.substr(0, Prefix.size()) != Prefix || TypeName[Prefix.size()] != '<' || TypeName.back() != '>')
			{
				return {};
			}
			const std::string_view Inner =
			    TypeName.substr(Prefix.size() + 1, TypeName.size() - Prefix.size() - 2);
			const std::string CanonicalInner =
			    CanonicalizeNativeElementTypeName(Inner, RecursionDepth + 1);
			return CanonicalInner.empty()
			    ? std::string{}
			    : std::string(Prefix) + '<' + CanonicalInner + '>';
		};
		if (std::string Array = CanonicalizeUnary("array"); !Array.empty())
		{
			return Array;
		}
		if (std::string Set = CanonicalizeUnary("set"); !Set.empty())
		{
			return Set;
		}
		for (const std::string_view Unary :
		    {"queue", "priority_queue", "stack", "deque", "channel",
		        "multiset", "ordered_multiset"})
		{
			if (std::string Canonical = CanonicalizeUnary(Unary); !Canonical.empty())
			{
				return Canonical;
			}
		}
		const auto CanonicalizeBinary =
		    [&TypeName, RecursionDepth](const std::string_view Prefix) -> std::string
		{
			if (TypeName.size() <= Prefix.size() + 3 || TypeName.substr(0, Prefix.size()) != Prefix || TypeName[Prefix.size()] != '<' || TypeName.back() != '>')
			{
				return {};
			}
			const std::string_view Inner =
			    TypeName.substr(Prefix.size() + 1, TypeName.size() - Prefix.size() - 2);
			std::size_t Depth = 0;
			std::size_t ParenthesisDepth = 0;
			for (std::size_t Index = 0; Index < Inner.size(); ++Index)
			{
				if (Inner[Index] == '<')
				{
					++Depth;
				}
				else if (Inner[Index] == '>' && (Index == 0 || Inner[Index - 1] != '-'))
				{
					if (Depth == 0)
					{
						break;
					}
					--Depth;
				}
				else if (Inner[Index] == '(')
				{
					++ParenthesisDepth;
				}
				else if (Inner[Index] == ')')
				{
					if (ParenthesisDepth == 0)
					{
						break;
					}
					--ParenthesisDepth;
				}
				else if (Inner[Index] == ',' && Depth == 0 && ParenthesisDepth == 0)
				{
					const std::string CanonicalKey = CanonicalizeNativeElementTypeName(
					    Inner.substr(0, Index), RecursionDepth + 1);
					const std::string CanonicalValue = CanonicalizeNativeElementTypeName(
					    Inner.substr(Index + 1), RecursionDepth + 1);
					if (CanonicalKey.empty() || CanonicalValue.empty())
					{
						return {};
					}
					return MakeNativeBinaryContainerTypeName(
					    Prefix,
					    CanonicalKey,
					    CanonicalValue);
				}
			}
			return {};
		};
		if (std::string Map = CanonicalizeBinary("map"); !Map.empty())
		{
			return Map;
		}
		if (std::string MultiMap = CanonicalizeBinary("multimap"); !MultiMap.empty())
		{
			return MultiMap;
		}
		if (std::string OrderedMap = CanonicalizeBinary("ordered_map");
		    !OrderedMap.empty())
		{
			return OrderedMap;
		}
		if (std::string OrderedMultiMap = CanonicalizeBinary("ordered_multimap");
		    !OrderedMultiMap.empty())
		{
			return OrderedMultiMap;
		}
		return std::string{TypeName};
	}

	inline bool NativeElementTypeNamesEqual(
	    const std::string_view Left,
	    const std::string_view Right)
	{
		const std::string CanonicalLeft =
		    CanonicalizeNativeElementTypeName(Left);
		const std::string CanonicalRight =
		    CanonicalizeNativeElementTypeName(Right);
		// Empty is the canonicalizer's invalid/over-depth sentinel, not a type.
		// In particular, two hostile over-depth spellings must not compare equal.
		return !CanonicalLeft.empty() && !CanonicalRight.empty() && CanonicalLeft == CanonicalRight;
	}

	// True when a value may safely cross an isolated-VM channel boundary.
	inline bool NativeChannelElementTypeIsValid(const std::string_view TypeName)
	{
		// Peel channel wrappers before calling the recursive general-purpose
		// canonicalizer. Besides enforcing the language nesting limit, this keeps
		// untrusted host/catalog metadata from exhausting the native call stack.
		std::string_view Current = TypeName;
		for (std::size_t Depth = 0; Depth <= 64; ++Depth)
		{
			if (Current.size() > 9 && Current.compare(0, 8, "channel<") == 0 && Current.back() == '>')
			{
				Current = Current.substr(8, Current.size() - 9);
				continue;
			}

			// A transferable leaf cannot itself be a generic/container spelling.
			// Canonicalizing only this leaf is therefore non-recursive.
			if (Current.find_first_of("<>") != std::string_view::npos)
			{
				return false;
			}
			const std::string Canonical =
			    CanonicalizeNativeElementTypeName(Current);
			return Canonical == "bool" || Canonical == "char" || Canonical == "short" || Canonical == "int" || Canonical == "long" || Canonical == "int64" || Canonical == "unsigned char" || Canonical == "unsigned short" || Canonical == "unsigned int" || Canonical == "unsigned long" || Canonical == "uint64" || Canonical == "float" || Canonical == "double" || Canonical == "string" || Canonical == "wstring";
		}
		return false;
	}

	inline bool NativeFunctionContainerMetadataIsValid(const FNativeFunction& Native)
	{
		if (Native.bHasStructuredTypeDescs)
		{
			if (Native.StructuredReturnType.Type != Native.ReturnType || Native.StructuredParamTypes.size() != Native.ParamTypes.size())
			{
				return false;
			}
			for (std::size_t Index = 0; Index < Native.ParamTypes.size(); ++Index)
			{
				if (Native.StructuredParamTypes[Index].Type != Native.ParamTypes[Index])
				{
					return false;
				}
			}
		}
		const auto IsSupportedKeyType =
		    [](const std::string_view TypeName) -> bool
		{
			const std::string Canonical =
			    CanonicalizeNativeElementTypeName(TypeName);
			if (Canonical == "char" || Canonical == "int" || Canonical == "unsigned int" || Canonical == "int64" || Canonical == "uint64" || Canonical == "string" || Canonical == "wstring")
			{
				return true;
			}
			// Registry/compiler semantic validation decides whether a named host
			// type is a Class. This metadata-only check must retain such names,
			// while still rejecting scalar and nested-container key spellings.
			return !Canonical.empty() && Canonical != "void" && Canonical != "variant" && Canonical != "bool" && Canonical != "short" && Canonical != "long" && Canonical != "unsigned char" && Canonical != "unsigned short" && Canonical != "unsigned long" && Canonical != "float" && Canonical != "double" && Canonical.find('<') == std::string::npos;
		};
		const auto HasParamName =
		    [](const std::vector<std::string>& Names, const std::size_t Index) -> bool
		{
			return Index < Names.size() && !Names[Index].empty();
		};
		const auto IsValidValueType =
		    [](const std::string_view TypeName) -> bool
		{
			const std::string_view Trimmed = TrimNativeTypeName(TypeName);
			return !Trimmed.empty() && Trimmed != "void";
		};
		const auto IsValidChannelElementType =
		    [](const std::string_view TypeName) -> bool
		{
			return NativeChannelElementTypeIsValid(TypeName);
		};
		const auto ParseRuntimeContainer = [](
		                                       const std::string_view RawTypeName,
		                                       const std::string_view ExpectedBase,
		                                       std::vector<std::string>& OutArguments) -> bool
		{
			OutArguments.clear();
			const std::string Canonical =
			    CanonicalizeNativeElementTypeName(RawTypeName);
			const std::size_t Open = Canonical.find('<');
			if (Open == std::string::npos || Canonical.back() != '>' || std::string_view(Canonical).substr(0, Open) != ExpectedBase)
			{
				return false;
			}
			std::size_t Start = Open + 1;
			std::size_t Depth = 0;
			std::size_t ParenthesisDepth = 0;
			for (std::size_t Index = Start; Index + 1 < Canonical.size(); ++Index)
			{
				if (Canonical[Index] == '<')
				{
					++Depth;
				}
				else if (Canonical[Index] == '>' && (Index == 0 || Canonical[Index - 1] != '-'))
				{
					if (Depth == 0)
					{
						return false;
					}
					--Depth;
				}
				else if (Canonical[Index] == '(')
				{
					++ParenthesisDepth;
				}
				else if (Canonical[Index] == ')')
				{
					if (ParenthesisDepth == 0)
					{
						return false;
					}
					--ParenthesisDepth;
				}
				else if (Canonical[Index] == ',' && Depth == 0 && ParenthesisDepth == 0)
				{
					const std::string Argument = Canonical.substr(Start, Index - Start);
					if (Argument.empty())
					{
						return false;
					}
					OutArguments.push_back(Argument);
					Start = Index + 1;
				}
			}
			if (Depth != 0 || ParenthesisDepth != 0 || Start + 1 >= Canonical.size())
			{
				return false;
			}
			OutArguments.push_back(
			    Canonical.substr(Start, Canonical.size() - Start - 1));
			return !OutArguments.back().empty();
		};
		const auto RuntimeSlotIsValid =
		    [&ParseRuntimeContainer,
		        &IsSupportedKeyType,
		        &IsValidValueType,
		        &IsValidChannelElementType](
		        const ENativeType Type,
		        const std::string_view RuntimeTypeName) -> bool
		{
			std::string GenericCanonical;
			std::string_view Canonical = CanonicalizeNativeScalarTypeName(TrimNativeTypeName(RuntimeTypeName));
			if (Canonical.find('<') != std::string_view::npos)
			{
				GenericCanonical = CanonicalizeNativeElementTypeName(Canonical);
				Canonical = GenericCanonical;
			}
			if (Canonical.empty())
			{
				return false;
			}
			const auto IsOpaqueNamedType = [&Canonical]() -> bool
			{
				if (Canonical == "variant" || Canonical == "void" || Canonical == "bool" || Canonical == "char" || Canonical == "short" || Canonical == "int" || Canonical == "long" || Canonical == "int64" || Canonical == "unsigned char" || Canonical == "unsigned short" || Canonical == "unsigned int" || Canonical == "unsigned long" || Canonical == "uint64" || Canonical == "float" || Canonical == "double" || Canonical == "string" || Canonical == "wstring" || Canonical.rfind("fn(", 0) == 0)
				{
					return false;
				}
				for (const std::string_view Container : {
				         "array", "map", "set", "queue", "priority_queue",
				         "stack", "ordered_map", "deque", "channel", "multimap",
				         "ordered_multimap", "multiset", "ordered_multiset"})
				{
					if (Canonical.size() > Container.size() && std::string_view(Canonical).substr(0, Container.size()) == Container && Canonical[Container.size()] == '<')
					{
						return false;
					}
				}
				return true;
			};
			switch (Type)
			{
			case ENativeType::Variant:
				return Canonical == "variant";
			case ENativeType::Void:
				return Canonical == "void";
			case ENativeType::Int:
				return Canonical == "int";
			case ENativeType::Bool:
				return Canonical == "bool";
			case ENativeType::Double:
				return Canonical == "double";
			case ENativeType::Float:
				return Canonical == "float";
			case ENativeType::Int8:
				return Canonical == "char";
			case ENativeType::UInt8:
				return Canonical == "unsigned char";
			case ENativeType::Int16:
				return Canonical == "short";
			case ENativeType::UInt16:
				return Canonical == "unsigned short";
			case ENativeType::String:
				return Canonical == "string";
			case ENativeType::Unsigned:
				return Canonical == "unsigned int";
			case ENativeType::Int64:
				return Canonical == "int64";
			case ENativeType::UInt64:
				return Canonical == "uint64";
			case ENativeType::Enum:
			case ENativeType::HostObject:
				return IsOpaqueNamedType();
			case ENativeType::Function:
				return Canonical.rfind("fn(", 0) == 0 && Canonical.find(")->") != std::string::npos;
			default:
				{
					break;
				}
			}

			const char* Base = nullptr;
			bool bBinary = false;
			bool bKeyElement = false;
			switch (Type)
			{
			case ENativeType::Array:
				{
					Base = "array";
					break;
				}
			case ENativeType::Queue:
				{
					Base = "queue";
					break;
				}
			case ENativeType::PriorityQueue:
				{
					Base = "priority_queue";
					break;
				}
			case ENativeType::Stack:
				{
					Base = "stack";
					break;
				}
			case ENativeType::Deque:
				{
					Base = "deque";
					break;
				}
			case ENativeType::Channel:
				{
					Base = "channel";
					break;
				}
			case ENativeType::Set:
				{
					Base = "set";
					bKeyElement = true;
					break;
				}
			case ENativeType::MultiSet:
				{
					Base = "multiset";
					bKeyElement = true;
					break;
				}
			case ENativeType::OrderedMultiSet:
				{
					Base = "ordered_multiset";
					bKeyElement = true;
					break;
				}
			case ENativeType::Map:
				{
					Base = "map";
					bBinary = true;
					break;
				}
			case ENativeType::OrderedMap:
				{
					Base = "ordered_map";
					bBinary = true;
					break;
				}
			case ENativeType::MultiMap:
				{
					Base = "multimap";
					bBinary = true;
					break;
				}

			case ENativeType::OrderedMultiMap:
				{
					Base = "ordered_multimap";
					bBinary = true;
					break;
				}
			default:
				return false;
			}
			std::vector<std::string> Arguments;
			if (!ParseRuntimeContainer(Canonical, Base, Arguments) || Arguments.size() != (bBinary ? 2u : 1u))
			{
				return false;
			}
			if (bBinary)
			{
				return IsSupportedKeyType(Arguments[0]) && IsValidValueType(Arguments[1]);
			}
			if (bKeyElement)
			{
				return IsSupportedKeyType(Arguments[0]);
			}
			if (Type == ENativeType::Channel)
			{
				return IsValidChannelElementType(Arguments[0]);
			}
			return IsValidValueType(Arguments[0]);
		};
		// Runtime identities are authoritative when supplied, but public native
		// tables may be assembled manually. Reject partial metadata and verify that
		// every composed identity agrees with its ENativeType before compiler or VM
		// code is allowed to consume it.
		if (!Native.RuntimeReturnTypeName.empty() || !Native.RuntimeParamTypeNames.empty())
		{
			if (Native.RuntimeReturnTypeName.empty() || Native.RuntimeParamTypeNames.size() != Native.ParamTypes.size() || !RuntimeSlotIsValid(Native.ReturnType, Native.RuntimeReturnTypeName))
			{
				return false;
			}
			for (std::size_t Index = 0; Index < Native.ParamTypes.size(); ++Index)
			{
				if (!RuntimeSlotIsValid(
				        Native.ParamTypes[Index], Native.RuntimeParamTypeNames[Index]))
				{
					return false;
				}
			}
			return true;
		}
		const auto ValidContainerSlot =
		    [&IsSupportedKeyType,
		        &HasParamName,
		        &IsValidValueType,
		        &IsValidChannelElementType](
		        const ENativeType Type,
		        const std::size_t Index,
		        const std::vector<std::string>& ElementNames,
		        const std::vector<std::string>& KeyNames,
		        const std::vector<std::string>& ValueNames) -> bool
		{
			if (Type == ENativeType::Array || Type == ENativeType::Queue || Type == ENativeType::PriorityQueue || Type == ENativeType::Stack || Type == ENativeType::Deque)
			{
				return HasParamName(ElementNames, Index) && IsValidValueType(ElementNames[Index]);
			}
			if (Type == ENativeType::Channel)
			{
				return HasParamName(ElementNames, Index) && IsValidChannelElementType(ElementNames[Index]);
			}
			if (Type == ENativeType::Set || Type == ENativeType::MultiSet || Type == ENativeType::OrderedMultiSet)
			{
				return HasParamName(ElementNames, Index) && IsSupportedKeyType(ElementNames[Index]);
			}
			if (Type == ENativeType::Map || Type == ENativeType::OrderedMap || Type == ENativeType::MultiMap || Type == ENativeType::OrderedMultiMap)
			{
				return HasParamName(KeyNames, Index) && HasParamName(ValueNames, Index) && IsSupportedKeyType(KeyNames[Index]) && IsValidValueType(ValueNames[Index]);
			}
			return true;
		};

		if ((Native.ReturnType == ENativeType::Array || Native.ReturnType == ENativeType::Queue || Native.ReturnType == ENativeType::PriorityQueue || Native.ReturnType == ENativeType::Stack || Native.ReturnType == ENativeType::Deque) && !IsValidValueType(Native.ReturnElementTypeName))
		{
			return false;
		}
		if (Native.ReturnType == ENativeType::Channel && !IsValidChannelElementType(Native.ReturnElementTypeName))
		{
			return false;
		}
		if ((Native.ReturnType == ENativeType::Set || Native.ReturnType == ENativeType::MultiSet || Native.ReturnType == ENativeType::OrderedMultiSet) && (Native.ReturnElementTypeName.empty() || !IsSupportedKeyType(Native.ReturnElementTypeName)))
		{
			return false;
		}
		if ((Native.ReturnType == ENativeType::Map || Native.ReturnType == ENativeType::OrderedMap || Native.ReturnType == ENativeType::MultiMap || Native.ReturnType == ENativeType::OrderedMultiMap) && (Native.ReturnKeyTypeName.empty() || !IsValidValueType(Native.ReturnValueTypeName) || !IsSupportedKeyType(Native.ReturnKeyTypeName)))
		{
			return false;
		}
		for (std::size_t Index = 0; Index < Native.ParamTypes.size(); ++Index)
		{
			if (!ValidContainerSlot(
			        Native.ParamTypes[Index],
			        Index,
			        Native.ParamElementTypeNames,
			        Native.ParamKeyTypeNames,
			        Native.ParamValueTypeNames))
			{
				return false;
			}
		}
		return true;
	}

	inline const char* NativeTypeName(const ENativeType Type)
	{
		switch (Type)
		{
		case ENativeType::Variant:
			return "variant";
		case ENativeType::Void:
			return "void";
		case ENativeType::Int:
			return "int";
		case ENativeType::Bool:
			return "bool";
		case ENativeType::Double:
			return "double";
		case ENativeType::Float:
			return "float";
		case ENativeType::Int8:
			return "char";
		case ENativeType::UInt8:
			return "unsigned char";
		case ENativeType::Int16:
			return "short";
		case ENativeType::UInt16:
			return "unsigned short";
		case ENativeType::String:
			return "string";
		case ENativeType::Unsigned:
			return "unsigned int";
		case ENativeType::Int64:
			return "int64";
		case ENativeType::UInt64:
			return "uint64";
		case ENativeType::Enum:
			return "enum";
		case ENativeType::HostObject:
			return "object";
		case ENativeType::Array:
			return "array";
		case ENativeType::Map:
			return "map";
		case ENativeType::Set:
			return "set";
		case ENativeType::Queue:
			return "queue";
		case ENativeType::Stack:
			return "stack";
		case ENativeType::OrderedMap:
			return "ordered_map";
		case ENativeType::Deque:
			return "deque";
		case ENativeType::Channel:
			return "channel";
		case ENativeType::MultiMap:
			return "multimap";
		case ENativeType::OrderedMultiMap:
			return "ordered_multimap";
		case ENativeType::PriorityQueue:
			return "priority_queue";
		case ENativeType::MultiSet:
			return "multiset";
		case ENativeType::OrderedMultiSet:
			return "ordered_multiset";
		case ENativeType::Function:
			return "function";
		default:
			return "void";
		}
	}

	inline std::string ResolveNativeTypeDescName(const FNativeTypeDesc& Desc)
	{
		if (Desc.Type == ENativeType::HostObject || Desc.Type == ENativeType::Enum || Desc.Type == ENativeType::Function)
		{
			return Desc.HostTypeName;
		}
		const bool bBinary = Desc.Type == ENativeType::Map || Desc.Type == ENativeType::OrderedMap || Desc.Type == ENativeType::MultiMap || Desc.Type == ENativeType::OrderedMultiMap;
		if (bBinary && Desc.TypeArguments.size() == 2)
		{
			return MakeNativeBinaryContainerTypeName(
			    NativeTypeName(Desc.Type),
			    ResolveNativeTypeDescName(Desc.TypeArguments[0]),
			    ResolveNativeTypeDescName(Desc.TypeArguments[1]));
		}
		if (NativeTypeIsContainer(Desc.Type) && Desc.Type != ENativeType::Map && Desc.Type != ENativeType::OrderedMap && Desc.Type != ENativeType::MultiMap && Desc.Type != ENativeType::OrderedMultiMap && Desc.TypeArguments.size() == 1)
		{
			return MakeNativeUnaryContainerTypeName(
			    NativeTypeName(Desc.Type),
			    ResolveNativeTypeDescName(Desc.TypeArguments[0]));
		}
		if (bBinary && !Desc.KeyTypeName.empty() && !Desc.ValueTypeName.empty())
		{
			return MakeNativeBinaryContainerTypeName(
			    NativeTypeName(Desc.Type),
			    CanonicalizeNativeElementTypeName(Desc.KeyTypeName),
			    CanonicalizeNativeElementTypeName(Desc.ValueTypeName));
		}
		if (NativeTypeIsContainer(Desc.Type) && !Desc.ElementTypeName.empty())
		{
			return MakeNativeUnaryContainerTypeName(
			    NativeTypeName(Desc.Type),
			    CanonicalizeNativeElementTypeName(Desc.ElementTypeName));
		}
		return NativeTypeName(Desc.Type);
	}

	inline std::string ResolveNativeReturnTypeName(const FNativeFunction& Native)
	{
		if (!Native.RuntimeReturnTypeName.empty())
		{
			return Native.RuntimeReturnTypeName;
		}
		if (Native.ReturnType == ENativeType::Void)
		{
			return "void";
		}
		if (Native.ReturnType == ENativeType::Array)
		{
			if (!Native.ReturnElementTypeName.empty())
			{
				return MakeNativeArrayTypeName(
				    CanonicalizeNativeElementTypeName(Native.ReturnElementTypeName));
			}
			return "array";
		}
		if (Native.ReturnType == ENativeType::Set || Native.ReturnType == ENativeType::MultiSet || Native.ReturnType == ENativeType::OrderedMultiSet)
		{
			if (!Native.ReturnElementTypeName.empty())
			{
				return MakeNativeUnaryContainerTypeName(
				    NativeTypeName(Native.ReturnType),
				    CanonicalizeNativeElementTypeName(Native.ReturnElementTypeName));
			}
			return NativeTypeName(Native.ReturnType);
		}
		if (Native.ReturnType == ENativeType::Map || Native.ReturnType == ENativeType::MultiMap)
		{
			if (!Native.ReturnKeyTypeName.empty() && !Native.ReturnValueTypeName.empty())
			{
				return MakeNativeBinaryContainerTypeName(
				    NativeTypeName(Native.ReturnType),
				    CanonicalizeNativeElementTypeName(Native.ReturnKeyTypeName),
				    CanonicalizeNativeElementTypeName(Native.ReturnValueTypeName));
			}
			return NativeTypeName(Native.ReturnType);
		}
		if (Native.ReturnType == ENativeType::OrderedMap || Native.ReturnType == ENativeType::OrderedMultiMap)
		{
			if (!Native.ReturnKeyTypeName.empty() && !Native.ReturnValueTypeName.empty())
			{
				return MakeNativeBinaryContainerTypeName(
				    NativeTypeName(Native.ReturnType),
				    CanonicalizeNativeElementTypeName(Native.ReturnKeyTypeName),
				    CanonicalizeNativeElementTypeName(Native.ReturnValueTypeName));
			}
			return NativeTypeName(Native.ReturnType);
		}
		if (Native.ReturnType == ENativeType::Queue || Native.ReturnType == ENativeType::PriorityQueue || Native.ReturnType == ENativeType::Stack || Native.ReturnType == ENativeType::Deque || Native.ReturnType == ENativeType::Channel)
		{
			if (!Native.ReturnElementTypeName.empty())
			{
				return MakeNativeUnaryContainerTypeName(
				    NativeTypeName(Native.ReturnType),
				    CanonicalizeNativeElementTypeName(Native.ReturnElementTypeName));
			}
			return NativeTypeName(Native.ReturnType);
		}
		if (Native.ReturnType != ENativeType::HostObject && Native.ReturnType != ENativeType::Enum && Native.ReturnType != ENativeType::Function)
		{
			return NativeTypeName(Native.ReturnType);
		}
		if (!Native.ReturnHostTypeName.empty())
		{
			return Native.ReturnHostTypeName;
		}
		if (!Native.HostTypeName.empty())
		{
			return Native.HostTypeName;
		}
		return "object";
	}

	inline std::string ResolveNativeParamTypeName(
	    const FNativeFunction& Native,
	    const std::size_t ParamIndex)
	{
		if (ParamIndex >= Native.ParamTypes.size())
		{
			return {};
		}
		if (ParamIndex < Native.RuntimeParamTypeNames.size() && !Native.RuntimeParamTypeNames[ParamIndex].empty())
		{
			return Native.RuntimeParamTypeNames[ParamIndex];
		}
		if (Native.ParamTypes[ParamIndex] == ENativeType::Array)
		{
			if (ParamIndex < Native.ParamElementTypeNames.size() && !Native.ParamElementTypeNames[ParamIndex].empty())
			{
				return MakeNativeArrayTypeName(
				    CanonicalizeNativeElementTypeName(Native.ParamElementTypeNames[ParamIndex]));
			}
			return "array";
		}
		if (Native.ParamTypes[ParamIndex] == ENativeType::Set || Native.ParamTypes[ParamIndex] == ENativeType::MultiSet || Native.ParamTypes[ParamIndex] == ENativeType::OrderedMultiSet)
		{
			if (ParamIndex < Native.ParamElementTypeNames.size() && !Native.ParamElementTypeNames[ParamIndex].empty())
			{
				return MakeNativeUnaryContainerTypeName(
				    NativeTypeName(Native.ParamTypes[ParamIndex]),
				    CanonicalizeNativeElementTypeName(Native.ParamElementTypeNames[ParamIndex]));
			}
			return NativeTypeName(Native.ParamTypes[ParamIndex]);
		}
		if (Native.ParamTypes[ParamIndex] == ENativeType::Map || Native.ParamTypes[ParamIndex] == ENativeType::MultiMap)
		{
			const std::string Key =
			    ParamIndex < Native.ParamKeyTypeNames.size()
			    ? Native.ParamKeyTypeNames[ParamIndex]
			    : std::string{};
			const std::string Value =
			    ParamIndex < Native.ParamValueTypeNames.size()
			    ? Native.ParamValueTypeNames[ParamIndex]
			    : std::string{};
			if (!Key.empty() && !Value.empty())
			{
				return MakeNativeBinaryContainerTypeName(
				    NativeTypeName(Native.ParamTypes[ParamIndex]),
				    CanonicalizeNativeElementTypeName(Key),
				    CanonicalizeNativeElementTypeName(Value));
			}
			return NativeTypeName(Native.ParamTypes[ParamIndex]);
		}
		if (Native.ParamTypes[ParamIndex] == ENativeType::OrderedMap || Native.ParamTypes[ParamIndex] == ENativeType::OrderedMultiMap)
		{
			const std::string Key =
			    ParamIndex < Native.ParamKeyTypeNames.size()
			    ? Native.ParamKeyTypeNames[ParamIndex]
			    : std::string{};
			const std::string Value =
			    ParamIndex < Native.ParamValueTypeNames.size()
			    ? Native.ParamValueTypeNames[ParamIndex]
			    : std::string{};
			if (!Key.empty() && !Value.empty())
			{
				return MakeNativeBinaryContainerTypeName(
				    NativeTypeName(Native.ParamTypes[ParamIndex]),
				    CanonicalizeNativeElementTypeName(Key),
				    CanonicalizeNativeElementTypeName(Value));
			}
			return NativeTypeName(Native.ParamTypes[ParamIndex]);
		}
		if (Native.ParamTypes[ParamIndex] == ENativeType::Queue || Native.ParamTypes[ParamIndex] == ENativeType::PriorityQueue || Native.ParamTypes[ParamIndex] == ENativeType::Stack || Native.ParamTypes[ParamIndex] == ENativeType::Deque || Native.ParamTypes[ParamIndex] == ENativeType::Channel)
		{
			if (ParamIndex < Native.ParamElementTypeNames.size() && !Native.ParamElementTypeNames[ParamIndex].empty())
			{
				return MakeNativeUnaryContainerTypeName(
				    NativeTypeName(Native.ParamTypes[ParamIndex]),
				    CanonicalizeNativeElementTypeName(
				        Native.ParamElementTypeNames[ParamIndex]));
			}
			return NativeTypeName(Native.ParamTypes[ParamIndex]);
		}
		if (Native.ParamTypes[ParamIndex] != ENativeType::HostObject && Native.ParamTypes[ParamIndex] != ENativeType::Enum && Native.ParamTypes[ParamIndex] != ENativeType::Function)
		{
			return NativeTypeName(Native.ParamTypes[ParamIndex]);
		}
		if (ParamIndex < Native.ParamHostTypeNames.size() && !Native.ParamHostTypeNames[ParamIndex].empty())
		{
			return Native.ParamHostTypeNames[ParamIndex];
		}
		if (!Native.HostTypeName.empty())
		{
			return Native.HostTypeName;
		}
		return "object";
	}

	inline std::string ResolveNativeParamElementTypeName(
	    const FNativeFunction& Native,
	    const std::size_t ParamIndex)
	{
		if (ParamIndex < Native.ParamElementTypeNames.size())
		{
			return Native.ParamElementTypeNames[ParamIndex];
		}
		return {};
	}

	bool NativeTypeMatchesValue(ENativeType Type, const FValue& Value);
	bool NativeTypeMatchesTypeName(ENativeType Type, std::string_view TypeName);

	// Coerce a value into the canonical runtime form for a native slot (e.g. int -> double).
	// HostObject also unwraps script objects that inherit a host type (HostBase).
	// When bDeepCopyContainers is false, container slots keep the same shared handle
	// (mutable ref); by-value / const-ref paths pass true.
	bool CoerceValueToNativeType(
	    ENativeType Type,
	    FValue& Value,
	    std::string& OutError,
	    bool bDeepCopyContainers = true);

	// Build a host-object handle; Destroy is invoked when the last reference is released.
	FHostObjectHandle MakeHostObject(
	    std::uint32_t TypeId,
	    std::string TypeName,
	    void* Payload,
	    void (*Destroy)(void*) = nullptr,
	    bool (*IsType)(
	        void* Payload,
	        void* UserData,
	        std::string_view ExpectedTypeName) = nullptr,
	    void* IsTypeUserData = nullptr);

	inline bool HostObjectMatchesType(
	    const FHostObjectHandle& Host,
	    const std::string_view ExpectedTypeName)
	{
		if (!Host || ExpectedTypeName.empty() || ExpectedTypeName == "object")
		{
			return static_cast<bool>(Host);
		}
		if (Host->TypeName == ExpectedTypeName)
		{
			return true;
		}
		return Host->IsTypeCallback && Host->IsTypeCallback(Host->Payload, Host->IsTypeUserData, ExpectedTypeName);
	}

	/**
	 * Script instance that owns this host payload (HostBase), if still alive.
	 * Empty handle when the host object is null or ScriptOwner has expired.
	 */
	inline FObjectHandle GetScriptOwner(const FHostObjectHandle& Host)
	{
		if (!Host)
		{
			return {};
		}
		return Host->ScriptOwner.lock();
	}

	// Payload of a HostObject value, or of a script object with HostBase.
	inline void* HostObjectPayload(const FValue& Value)
	{
		if (const auto* Handle = std::get_if<FHostObjectHandle>(&Value);
		    Handle && *Handle)
		{
			return (*Handle)->Payload;
		}
		if (const auto* Object = std::get_if<FObjectHandle>(&Value);
		    Object && *Object && (*Object)->GetHostBase())
		{
			return (*Object)->GetHostBase()->Payload;
		}
		return nullptr;
	}

	/**
	 * Memory operations borrowed for the duration of a host native callback.
	 */
	class IMemoryCallServices
	{
	public:
		virtual ~IMemoryCallServices() = default;
		virtual FMemoryContext& GetMemoryContext() = 0;
		virtual FMemoryWork GetMemoryWork() = 0;
		virtual bool MemoryAddressesAllowed() const = 0;
		virtual const IHostBindingCatalog* MemoryHostBindings() const
		{
			return nullptr;
		}
		virtual FMemoryResult MemoryArgumentAddress(std::size_t Index, std::uint64_t& OutAddress) = 0;
	};

	// Parameter identity excludes the return type and parameter names. Length
	// prefixes make nested function/container types unambiguous. Runtime aliases
	// are expanded by the registry before this key is used for registration.
	std::string NativeParameterSignature(const FNativeFunction& Native);
	FBytecodeNativeReference DescribeNativeFunction(const FNativeFunction& Native);
	// Resolve a module's stable native references against a merged host/standard
	// catalog. Commit the dense call table only after every reference is matched.
	bool BindBytecodeNativeReferences(const FBytecodeModule& Module,
	    const std::vector<FNativeFunction>& Available, std::vector<FNativeFunction>& Out, std::string& Error);

	/**
	 * VM-owned invocation surface exposed only for the lifetime of a native call.
	 * It executes on the active VM stack; it never starts a second Session::Run().
	 * Mutable ref parameters are written back only after the script call succeeds.
	 */
	class IScriptCallInvoker
	{
	public:
		virtual ~IScriptCallInvoker() = default;
		virtual bool InvokeCallable(
		    const FFunctionHandle& Callable,
		    std::vector<FValue>& InOutArguments,
		    FValue& OutReturn,
		    std::string& OutError) = 0;
		virtual bool InvokeFunction(
		    std::string_view FunctionName,
		    std::vector<FValue>& InOutArguments,
		    FValue& OutReturn,
		    std::string& OutError) = 0;
		virtual std::uint64_t GetProgramGeneration() const = 0;
		virtual std::shared_ptr<const void> GetProgramOwner() const = 0;
		/** Borrowed semantic metadata for this native callback only. Null means unavailable. */
		virtual const FBytecodeModule* GetActiveModule() const noexcept
		{
			return nullptr;
		}
		// Called immediately before a user native hook; pure value preparation does not commit.
		virtual void NotifyExecutionCommitted() noexcept
		{
		}
		/** Retain a value until retirement or program replacement. The returned lease
		 * may be retired on any thread; access and language cleanup stay on the owner.
		 * This never extends the lifetime of this borrowed invocation service. */
		virtual bool RetainForGeneration(const FValue&, FValueRoot&, std::string& Error)
		{
			Error = "generation-scoped value retention is unavailable in this execution host";
			return false;
		}
		/** Logical module of a script caller, innermost first. Empty means unavailable or out of range.
		 * Valid during this native callback; independent of optional debugger/source text. */
		virtual std::string GetCallingModulePath(std::size_t Depth) const
		{
			(void)Depth;
			return {};
		}
	};
}
