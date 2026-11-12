// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

/**
 * In (host -> VM) binding contracts - language-agnostic.
 *
 * Adapters (VeroScriptCpp, future C#/Python, ...) fill a registry; the compiler/VM
 * consume free-function natives and host class ctor/method natives synthesized here.
 *
 * Tooling/LSP should read IHostBindingCatalog only - they do not register bindings.
 */

#include "VeroScriptNative.h"
#include "VeroScriptAnnotation.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript
{
	enum class EHostTypeKind : std::uint8_t
	{
		Enum = 0,
		Struct,
		Class,
	};

	enum class EHostMemberKind : std::uint8_t
	{
		Constructor = 0,
		Method,
		StaticMethod,
		PropertyGetter,
		PropertySetter,
		EnumMember,
		/**
		 * Data member. With Callback -> live getter (`@get.Name`); write via PropertySetter.
		 * Catalog-only (no Callback) still participates in type resolve / completion.
		 */
		Field,
		// Read-only host value exposed as `Type.Name`; callback takes no arguments.
		StaticConstant,
	};

	struct FHostTypeId
	{
		friend bool operator==(FHostTypeId A, FHostTypeId B)
		{
			return A.Value == B.Value;
		}
		friend bool operator!=(FHostTypeId A, FHostTypeId B)
		{
			return A.Value != B.Value;
		}

		bool IsValid() const
		{
			return Value != 0;
		}

		std::uint32_t Value = 0;

	};

	struct FGenericHostTypeTemplateId
	{
		friend bool operator==(FGenericHostTypeTemplateId Left,
			FGenericHostTypeTemplateId Right)
		{
			return Left.Value == Right.Value;
		}
		friend bool operator!=(FGenericHostTypeTemplateId Left,
			FGenericHostTypeTemplateId Right)
		{
			return !(Left == Right);
		}

		bool IsValid() const
		{
			return Value != 0;
		}

		std::uint32_t Value = 0;

	};

	/**
	 * One argument on an annotation attached to a concrete host declaration.
	 * An absent Value represents a flag argument (`BlueprintCallable`), while an
	 * explicitly empty value represents `Name=""`. The core keeps both opaque.
	 */
	struct FHostAnnotationArgument
	{
		std::string Name;
		std::optional<std::string> Value;
	};

	/**
	 * An actual annotation instance on a host declaration. This is deliberately
	 * separate from FHostAnnotationDesc, which describes the annotation vocabulary
	 * supported by a host for completion and validation assistance.
	 */
	struct FHostAnnotationInstance
	{
		std::string Name;
		std::vector<FHostAnnotationArgument> Arguments;
	};

	/**
	 * Read-only free-function descriptor (no host callback).
	 */
	struct FHostFunctionDesc
	{
		FBindingOrigin Origin;
		std::string Name;
		std::vector<FHostAnnotationInstance> Annotations;
		// Parallel to ParamTypes when non-empty.
		std::vector<std::vector<FHostAnnotationInstance>> ParamAnnotations;
		// Tooling documentation supplied by the host/catalog.
		std::string Documentation;
		std::vector<std::string> ParamNames;
		std::vector<std::string> ParamDocumentation;
		std::string ReturnDocumentation;
		ENativeType ReturnType = ENativeType::Void;
		std::vector<ENativeType> ParamTypes;
		// HostObject / Enum type name or canonical Function signature.
		std::string ReturnHostTypeName;
		// Parallel to ParamTypes for HostObject / Enum / Function slots.
		std::vector<std::string> ParamHostTypeNames;
		// Element type name for unary containers.
		std::string ReturnElementTypeName;
		// Parallel to ParamTypes for unary-container slots.
		std::vector<std::string> ParamElementTypeNames;
		// Key/value names for binary map and multimap variants.
		std::string ReturnKeyTypeName;
		std::string ReturnValueTypeName;
		std::vector<std::string> ParamKeyTypeNames;
		std::vector<std::string> ParamValueTypeNames;
		// Parallel to ParamTypes; see FNativeFunction::ParamFlags.
		std::vector<std::uint8_t> ParamFlags;
		// Generic host-call metadata. Empty for ordinary functions.
		std::vector<std::string> TypeParams;
		std::string ScriptReturnTypeName;
		bool bGenericHostObjectCast = false;
	};

	struct FHostMemberDesc
	{
		std::shared_ptr<void> CallbackOwner;
		EHostMemberKind Kind = EHostMemberKind::Method;
		std::string Name;
		std::vector<FHostAnnotationInstance> Annotations;
		// Parallel to ParamTypes when non-empty.
		std::vector<std::vector<FHostAnnotationInstance>> ParamAnnotations;
		// Tooling documentation supplied by the host/catalog.
		std::string Documentation;
		std::vector<std::string> ParamNames;
		std::vector<std::string> ParamDocumentation;
		std::string ReturnDocumentation;
		ENativeType ReturnType = ENativeType::Void;
		std::vector<ENativeType> ParamTypes;
		/**
		 * Required for live ctor/method/static-method/property/field dispatch.
		 * Catalog-only members (no Callback) remain tooling metadata.
		 */
		FNativeCallback Callback = nullptr;
		void* UserData = nullptr;
		// Full-width signed value for EHostMemberKind::EnumMember.
		std::int64_t EnumValue = 0;
		/**
		 * For HostObject returns: script type name of the returned object
		 * (empty means the owning host type). For Enum / EnumMember: the enum
		 * type name. For Function: the canonical function signature.
		 */
		std::string ReturnHostTypeName;
		/**
		 * Parallel to ParamTypes for HostObject / Enum / Function value parameters
		 * (not the implicit receiver). Empty HostObject entry means owning type.
		 */
		std::vector<std::string> ParamHostTypeNames;
		// Element type name for unary containers.
		std::string ReturnElementTypeName;
		// Parallel to ParamTypes for unary-container slots.
		std::vector<std::string> ParamElementTypeNames;
		// Key/value names for binary map and multimap variants.
		std::string ReturnKeyTypeName;
		std::string ReturnValueTypeName;
		std::vector<std::string> ParamKeyTypeNames;
		std::vector<std::string> ParamValueTypeNames;
		// Parallel to ParamTypes (not including implicit receiver); see FNativeFunction::ParamFlags.
		std::vector<std::uint8_t> ParamFlags;
		/**
		 * True for an operator supplied by the host value protocol. Operators use
		 * ordinary script spellings (`operator+`, `operator==`, `operator conv T`)
		 * and participate in overload resolution, but remain separate from named
		 * method lookup. NativeName is assigned by the registry for methods,
		 * operators and constructors, and serialized in binding catalogs so overloads
		 * have stable, unambiguous dispatch identity.
		 */
		bool bOperator = false;
		// True when an instance method is read-only (`...() const`). This is
		// independent from bMutableThis, which controls host value write-back.
		bool bConstMethod = false;
		bool bMutableThis = false;
		std::string NativeName;
	};

	struct FHostTypeDesc
	{
		std::shared_ptr<void> ModuleOwner;
		std::shared_ptr<const void> ProviderIdentity;
		FBindingOrigin Origin;
		FHostTypeId Id;
		std::string Name;
		std::vector<FHostAnnotationInstance> Annotations;
		// Tooling documentation supplied by the host/catalog.
		std::string Documentation;
		EHostTypeKind Kind = EHostTypeKind::Class;
		// Concrete generic identity. Ordinary host types leave both empty/zero.
		FGenericHostTypeTemplateId GenericTemplateId;
		std::vector<std::string> GenericTypeArguments;
		std::vector<FHostMemberDesc> Members;
		/**
		 * When true, script classes may declare `class MyType : HostName`.
		 * Only Class kinds may be inheritable; structs/enums reject inheritance.
		 */
		bool bInheritable = false;
		/**
		 * Optional deep-copy for host structs (value semantics).
		 * Callback receives Args[0]=source HostObject and must return a new HostObject.
		 * Class types ignore this (shared handles).
		 */
		FNativeCallback CloneCallback = nullptr;
		void* CloneUserData = nullptr;
		/**
		 * Optional value equality/hash protocol. Host structs require clone,
		 * equals, and hash callbacks before they may be used as Map/Set keys.
		 */
		FNativeCallback EqualsCallback = nullptr;
		void* EqualsUserData = nullptr;
		FNativeCallback HashCallback = nullptr;
		void* HashUserData = nullptr;
		/**
		 * Factory for script subclasses of this host type (`new MyActor()`).
		 * Args[0] = script type linkage name (string). Must return a HostObject of this type.
		 * Required when a script class inherits this type and is constructed.
		 */
		FNativeCallback SubclassFactory = nullptr;
		void* SubclassFactoryUserData = nullptr;
		/**
		 * Strong/scoped enum value semantics. When enabled, enum members are exposed
		 * as FEnumValue{Name, int64} and the enum may be used as a Map/Set key.
		 * Unscoped host enums retain their historic integer behavior.
		 */
		bool bScopedEnum = false;
		/**
		 * Canonical integral storage type used by checked numeric/enum conversions.
		 * Meaningful only for Enum kinds. Keeping it on the type descriptor lets
		 * serialized bytecode preserve the host enum's representable integral
		 * domain without a live binding registry. Full uint64 is rejected because
		 * FEnumValue intentionally stores an int64 payload.
		 */
		std::string EnumUnderlyingTypeName = "int";
		// Generic prototypes retain the owners of their generated native bridges.
		std::vector<std::pair<void*, std::shared_ptr<void>>> CallbackLifetimes;
	};

	/**
	 * Instantiate callback for a generic host value template. It describes one
	 * concrete type; the registry owns identity assignment and validates/registers
	 * every returned member transactionally. Returning false must set OutError.
	 */
	// FGenericHostTypeTemplateDesc stores this callback alias.
	using FGenericHostTypeInstantiateCallback = bool (*)(
		const std::vector<std::string>& TypeArguments,
		FHostTypeDesc& OutConcreteType,
		std::string& OutError,
		void* UserData);

	struct FGenericHostTypeTemplateDesc
	{
		std::shared_ptr<void> ModuleOwner;
		std::shared_ptr<const void> ProviderIdentity;
		FBindingOrigin Origin;
		FGenericHostTypeTemplateId Id;
		std::string Name;
		std::size_t Arity = 0;
		std::string Documentation;
		FGenericHostTypeInstantiateCallback Instantiate = nullptr;
		void* UserData = nullptr;
	};

	/**
	 * A host-provided source-level type alias.
	 *
	 * TypeParams may be empty for an ordinary alias or contain generic parameter
	 * names used by TargetTypeName. For example, `TEnumAsByte<T> -> T`.
	 * The declared spelling is retained for reflection/package ABI while the VM
	 * recursively uses TargetTypeName as its transparent runtime representation.
	 * Aliases introduce no independent payload or object lifetime.
	 */
	struct FHostTypeAliasDesc
	{
		FBindingOrigin Origin;
		std::string Name;
		std::vector<std::string> TypeParams;
		std::string TargetTypeName;
		std::string Documentation;
	};

	/**
	 * A declaration annotation the host recognizes (`UCLASS()`, `@rpc(reliable)`).
	 *
	 * The language stores annotations opaquely in FDeclMeta and never validates
	 * names, so this exists purely so tooling can offer completion and hover for
	 * the host's own vocabulary instead of hard-coding one.
	 */
	struct FHostAnnotationArgumentDesc
	{
		// Argument/specifier spelling, e.g. `BlueprintCallable` or `Category`.
		std::string Name;
		// Compact value hint, e.g. `flag` or `string`.
		std::string Detail;
		std::string Documentation;
		std::vector<FHostAnnotationArgumentDesc> Children;
	};

	struct FHostAnnotationDesc
	{
		// `UCLASS` or `@rpc`. Leading `@` selects the `@name(...)` spelling.
		std::string Name;
		// Completion/hover hint, e.g. `annotation (BlueprintType)`.
		std::string Detail;
		std::string Documentation;
		std::vector<FHostAnnotationArgumentDesc> Arguments;
		std::uint32_t Targets = 131071;
		// Applies to the annotation name only; source spelling is retained.
		bool CaseSensitive = true;
	};

	/**
	 * Read-only view of host bindings shared by compile, VM, tooling, and codegen.
	 */
	class IHostBindingCatalog
	{
	public:
		virtual ~IHostBindingCatalog() = default;

		// Runtime native table (callbacks included). Never null; may be empty.
		virtual const std::vector<FNativeFunction>& GetNativeFunctions() const = 0;
		/** Nonzero runtime revision enables Session code reuse. Change it whenever
		 * callbacks, signatures or their lifetime owners change. Zero keeps custom
		 * catalogs conservative; the address of a catalog is never a revision. */
		virtual std::uint64_t GetRevision() const noexcept
		{
			return 0;
		}

		// Metadata mirror of free functions (no callbacks).
		virtual const std::vector<FHostFunctionDesc>& GetFunctions() const = 0;

		virtual const std::vector<FHostTypeDesc>& GetTypes() const = 0;

		virtual const std::vector<FHostTypeAliasDesc>& GetTypeAliases() const
		{
			static const std::vector<FHostTypeAliasDesc> None;
			return None;
		}
		virtual const std::vector<FGenericHostTypeTemplateDesc>&
		GetGenericTypeTemplates() const
		{
			static const std::vector<FGenericHostTypeTemplateDesc> None;
			return None;
		}

		/**
		 * Tooling-only annotation vocabulary. Not pure so existing catalogs that
		 * declare no annotations keep compiling.
		 */
		virtual const std::vector<FHostAnnotationDesc>& GetAnnotations() const
		{
			static const std::vector<FHostAnnotationDesc> None;
			return None;
		}

		// Name-only lookup succeeds only for one signature. Enumerate GetFunctions()
		// to inspect all overloads; return types alone never distinguish overloads.
		virtual const FHostFunctionDesc* FindFunction(std::string_view Name) const = 0;
		virtual const FHostTypeDesc* FindType(std::string_view Name) const = 0;
		virtual const FHostTypeDesc* FindType(FHostTypeId Id) const = 0;
		virtual const FGenericHostTypeTemplateDesc* FindGenericTypeTemplate(
			std::string_view Name) const
		{
			for (const FGenericHostTypeTemplateDesc& Desc : GetGenericTypeTemplates())
			{
				if (Desc.Name == Name)
				{
					return &Desc;
				}
			}
			return nullptr;
		}
		virtual const FHostTypeAliasDesc* FindTypeAlias(std::string_view Name) const
		{
			for (const FHostTypeAliasDesc& Alias : GetTypeAliases())
			{
				if (Alias.Name == Name)
				{
					return &Alias;
				}
			}
			return nullptr;
		}
	};

	/**
	 * Resolve transparent host aliases to the type used by the VM.
	 *
	 * Resolution is recursive through generic arguments, function signatures,
	 * references, and native arrays. Unknown (non-alias) type names are preserved.
	 * Alias cycles and arity mismatches fail with a stable diagnostic id.
	 */
	bool ResolveHostRuntimeTypeName(
		const IHostBindingCatalog& Catalog,
		std::string_view DeclaredTypeName,
		std::string& OutRuntimeTypeName,
		std::string& OutErrorMessageId);

	// Internal native name for instance/static host methods (not intended for script source).
	inline std::string MakeHostMethodNativeName(
		const std::string_view TypeName,
		const std::string_view MethodName)
	{
		std::string Name;
		Name.reserve(TypeName.size() + MethodName.size() + 8);
		Name.push_back('\x1F');
		Name += "host.";
		Name.append(TypeName.data(), TypeName.size());
		Name.push_back('.');
		Name.append(MethodName.data(), MethodName.size());
		return Name;
	}

	inline std::string MakeHostMethodOverloadNativeName(
		const std::string_view TypeName,
		const std::string_view MethodName,
		const std::string_view ParameterSignature)
	{
		return MakeHostMethodNativeName(TypeName, MethodName) + '#'
			+ std::to_string(ParameterSignature.size()) + ':' + std::string(ParameterSignature);
	}

	inline bool NativeNameMatchesLookup(std::string_view Native, std::string_view Lookup)
	{
		return Native == Lookup || (Lookup.substr(0, 6) == std::string_view("\x1f" "host.", 6)
			&& Native.size() > Lookup.size() && Native.substr(0, Lookup.size()) == Lookup
			&& Native[Lookup.size()] == '#');
	}

	inline std::string MakeHostOperatorNativeName(
		const std::string_view TypeName,
		const std::size_t OverloadIndex)
	{
		std::string Name;
		Name.reserve(TypeName.size() + 32);
		Name.push_back('\x1F');
		Name += "hostop.";
		Name.append(TypeName.data(), TypeName.size());
		Name.push_back('.');
		Name += std::to_string(OverloadIndex);
		return Name;
	}

	// Signature-derived overload identity used by the registry. Length prefixes
	// keep the name unambiguous without relying on platform-specific hashing.
	inline std::string MakeHostOperatorNativeName(
		const std::string_view TypeName,
		const std::string_view CanonicalSignature)
	{
		std::string Name;
		Name.reserve(TypeName.size() + CanonicalSignature.size() + 48);
		Name.push_back('\x1F');
		Name += "hostop.";
		Name += std::to_string(TypeName.size());
		Name.push_back(':');
		Name.append(TypeName.data(), TypeName.size());
		Name.push_back('.');
		Name += std::to_string(CanonicalSignature.size());
		Name.push_back(':');
		Name.append(CanonicalSignature.data(), CanonicalSignature.size());
		return Name;
	}

	inline std::string MakeHostConstructorNativeName(
		const std::string_view TypeName,
		const std::size_t OverloadIndex)
	{
		std::string Name;
		Name.reserve(TypeName.size() + 32);
		Name.push_back('\x1F');
		Name += "hostctor.";
		Name.append(TypeName.data(), TypeName.size());
		Name.push_back('.');
		Name += std::to_string(OverloadIndex);
		return Name;
	}

	inline std::string MakeHostConstructorNativeName(
		const std::string_view TypeName,
		const std::string_view CanonicalSignature)
	{
		std::string Name;
		Name.reserve(TypeName.size() + CanonicalSignature.size() + 48);
		Name.push_back('\x1F');
		Name += "hostctor.";
		Name += std::to_string(TypeName.size());
		Name.push_back(':');
		Name.append(TypeName.data(), TypeName.size());
		Name.push_back('.');
		Name += std::to_string(CanonicalSignature.size());
		Name.push_back(':');
		Name.append(CanonicalSignature.data(), CanonicalSignature.size());
		return Name;
	}

	inline std::string MakeHostPropertyGetterNativeName(
		const std::string_view TypeName,
		const std::string_view PropertyName)
	{
		return MakeHostMethodNativeName(TypeName, std::string("@get.") + std::string(PropertyName));
	}

	inline std::string MakeHostStaticConstantNativeName(
		const std::string_view TypeName,
		const std::string_view ConstantName)
	{
		return MakeHostMethodNativeName(
			TypeName,
			std::string("@static.") + std::string(ConstantName));
	}

	inline std::string MakeHostPropertySetterNativeName(
		const std::string_view TypeName,
		const std::string_view PropertyName)
	{
		return MakeHostMethodNativeName(TypeName, std::string("@set.") + std::string(PropertyName));
	}

	inline std::string MakeHostCloneNativeName(const std::string_view TypeName)
	{
		return MakeHostMethodNativeName(TypeName, "@clone");
	}

	inline std::string MakeHostSubclassNativeName(const std::string_view TypeName)
	{
		return MakeHostMethodNativeName(TypeName, "@subclass");
	}

	inline std::string MakeHostEqualsNativeName(const std::string_view TypeName)
	{
		return MakeHostMethodNativeName(TypeName, "@equals");
	}

	inline std::string MakeHostHashNativeName(const std::string_view TypeName)
	{
		return MakeHostMethodNativeName(TypeName, "@hash");
	}

	/**
	 * Mutable host binding table. Owned by embed sessions / tooling hosts.
	 * Class/struct ctor/method/static-constant/property/field members with Callback are
	 * synthesized into GetNativeFunctions().
	 */
	class FHostBindingRegistry final : public IHostBindingCatalog
	{
	public:
		FHostBindingRegistry() = default;
		FHostBindingRegistry(const FHostBindingRegistry&) = default;
		FHostBindingRegistry(FHostBindingRegistry&& Other) noexcept;
		FHostBindingRegistry& operator=(FHostBindingRegistry Other) noexcept;

		//~ Begin IHostBindingCatalog Interface
		const std::vector<FNativeFunction>& GetNativeFunctions() const override
		{
			return Natives;
		}
		std::uint64_t GetRevision() const noexcept override
		{
			return Revision;
		}


		const std::vector<FHostFunctionDesc>& GetFunctions() const override
		{
			return Functions;
		}

		const std::vector<FHostTypeDesc>& GetTypes() const override
		{
			return Types;
		}

		const std::vector<FHostTypeAliasDesc>& GetTypeAliases() const override
		{
			return TypeAliases;
		}

		const std::vector<FGenericHostTypeTemplateDesc>& GetGenericTypeTemplates()
			const override
		{
			return GenericTypeTemplates;
		}

		const std::vector<FHostAnnotationDesc>& GetAnnotations() const override
		{
			return Annotations;
		}

		const FHostFunctionDesc* FindFunction(std::string_view Name) const override;

		const FHostTypeDesc* FindType(std::string_view Name) const override;

		const FHostTypeDesc* FindType(FHostTypeId Id) const override;

		const FHostTypeAliasDesc* FindTypeAlias(std::string_view Name) const override;

		const FGenericHostTypeTemplateDesc* FindGenericTypeTemplate(
			std::string_view Name) const override;
		//~ End IHostBindingCatalog Interface

		// Scoped by the core registration service; reset before publishing a registry.
		void SetRegistrationOwner(std::shared_ptr<void> Owner, FBindingOrigin Origin = {},
			std::shared_ptr<const void> Identity = {})
		{
			InvalidateRuntimeBindings();
			RegistrationProviderIdentity = Identity ? std::move(Identity) : Owner;
			RegistrationOwner = std::move(Owner);
			RegistrationOrigin = std::move(Origin);
		}

		void RetainNativeCallback(void* UserData, const std::shared_ptr<void>& Owner)
		{
			InvalidateRuntimeBindings();
			for (FNativeFunction& Native : Natives)
			{
				if (Native.UserData == UserData)
				{
					Native.CallbackOwner = JoinCallbackOwners(Native.CallbackOwner, Owner);
				}
			}
			for (auto& Type : Types)
			{
				bool Used = (Type.CloneCallback && Type.CloneUserData == UserData) ||
					(Type.EqualsCallback && Type.EqualsUserData == UserData) ||
					(Type.HashCallback && Type.HashUserData == UserData) ||
					(Type.SubclassFactory && Type.SubclassFactoryUserData == UserData);
				for (const auto& Member : Type.Members)
				{
					Used = Used || (Member.Callback && Member.UserData == UserData);
				}

				if (Used)
				{
					Type.ModuleOwner = JoinCallbackOwners(Type.ModuleOwner, Owner);
				}
			}
		}

		/**
		 * Register a free function. Returns false and sets OutErrorMessageId (diag.*) on failure.
		 * Distinct canonical parameter types/ref qualifiers may share a name.
		 * Duplicate parameter signatures, including return-type-only changes, fail.
		 * Does not check language builtins - adapters should reject builtin name clashes.
		 * When ReturnType/ParamTypes contain HostObject or Enum, pass
		 * ReturnHostTypeName / ParamHostTypeNames. HostObject names must already be
		 * registered; Enum names identify a script `enum class`.
		 * When they contain DynamicArray, pass ReturnElementTypeName / ParamElementTypeNames
		 * (`int` / `bool` / `double` / `string` / `unsigned int` / registered host type).
		 * ParamFlags: optional parallel to ParamTypes (ref / const ref on any
		 * non-void parameter). Mutable callbacks use FNativeCallContext::MutableArg.
		 * DynamicMap slots also need ReturnKey/ValueTypeName or ParamKey/ValueTypeNames.
		 */
		bool RegisterNative(
			std::string Name,
			ENativeType ReturnType,
			std::vector<ENativeType> ParamTypes,
			FNativeCallback Callback,
			void* UserData,
			std::string& OutErrorMessageId,
			std::string ReturnHostTypeName = {},
			std::vector<std::string> ParamHostTypeNames = {},
			std::string ReturnElementTypeName = {},
			std::vector<std::string> ParamElementTypeNames = {},
			std::vector<std::uint8_t> ParamFlags = {},
			std::string ReturnKeyTypeName = {},
			std::string ReturnValueTypeName = {},
			std::vector<std::string> ParamKeyTypeNames = {},
			std::vector<std::string> ParamValueTypeNames = {});

		/**
		 * Structured registration API. Converts to the same validated storage as
		 * the legacy parallel-metadata overload.
		 */
		bool RegisterNative(
			std::string Name,
			FNativeTypeDesc ReturnType,
			std::vector<FNativeParamDesc> Params,
			FNativeCallback Callback,
			void* UserData,
			std::string& OutErrorMessageId);

		/**
		 * Register a free function together with its complete declaration metadata.
		 * This is the preferred API when documentation or declaration/parameter
		 * annotations must be retained without a name-based second lookup.
		 */
		bool RegisterNative(
			FHostFunctionDesc Function,
			FNativeCallback Callback,
			void* UserData,
			std::string& OutErrorMessageId);

		/**
		 * Register a safe, explicitly-typed host-object cast.
		 *
		 * The callback receives the normal value arguments plus the concrete type
		 * arguments in FNativeCallContext::TypeArguments. A failed cast returns an
		 * empty FHostObjectHandle; a successful cast may return either the source
		 * host handle or its owning script object.
		 */
		bool RegisterGenericHostObjectCast(
			std::string Name,
			std::vector<std::string> TypeParams,
			std::string ScriptReturnTypeName,
			FNativeCallback Callback,
			void* UserData,
			std::string& OutErrorMessageId);

		/**
		 * Register a host type name for the catalog and runtime.
		 * On success OutTypeId is set; OutErrorMessageId cleared.
		 */
		bool RegisterType(
			std::string Name,
			EHostTypeKind Kind,
			FHostTypeId& OutTypeId,
			std::string& OutErrorMessageId);

		bool RegisterType(
			std::string Name,
			EHostTypeKind Kind,
			FHostTypeId& OutTypeId,
			std::string& OutErrorMessageId,
			std::vector<FHostAnnotationInstance> TypeAnnotations);

		bool RegisterGenericHostTypeTemplate(
			std::string Name,
			std::size_t Arity,
			FGenericHostTypeInstantiateCallback Instantiate,
			void* UserData,
			FGenericHostTypeTemplateId& OutTemplateId,
			std::string& OutErrorMessageId,
			std::string Documentation = {});

		// Tooling/catalog form: preserves a template declaration without a live callback.
		bool RegisterGenericHostTypeTemplateMetadata(
			std::string Name,
			std::size_t Arity,
			FGenericHostTypeTemplateId& OutTemplateId,
			std::string& OutErrorMessageId,
			std::string Documentation = {});

		bool InstantiateGenericHostType(
			FGenericHostTypeTemplateId TemplateId,
			std::vector<std::string> TypeArguments,
			FHostTypeId& OutTypeId,
			std::string& OutErrorMessageId);

		bool SetTypeGenericInstanceInfo(
			FHostTypeId TypeId,
			FGenericHostTypeTemplateId TemplateId,
			std::vector<std::string> TypeArguments,
			std::string& OutErrorMessageId);

		bool RegisterTypeAlias(
			std::string Name,
			std::vector<std::string> TypeParams,
			std::string TargetTypeName,
			std::string& OutErrorMessageId,
			std::string Documentation = {});

		/**
		 * Append a member. Constructor, Method, StaticMethod, StaticConstant,
		 * PropertyGetter, PropertySetter, and Field with Callback become live natives.
		 * EnumMember stores an int constant resolved as Type.Member.
		 */
		bool RegisterMember(
			FHostTypeId TypeId,
			FHostMemberDesc Member,
			std::string& OutErrorMessageId);

		// Register an instance host-value operator with normal overload semantics.
		bool RegisterOperator(
			FHostTypeId TypeId,
			FHostMemberDesc Operator,
			std::string& OutErrorMessageId);

		/**
		 * Deep-copy callback for host structs (`CloneStruct` / pass-by-value).
		 * Required for correct value semantics; without it, copies share the handle.
		 */
		bool RegisterTypeClone(
			FHostTypeId TypeId,
			FNativeCallback Callback,
			void* UserData,
			std::string& OutErrorMessageId);

		/**
		 * Value equality for a host struct. Callback receives Args[0]/Args[1] as
		 * HostObject values and returns bool. This callback participates in
		 * unordered-container operations: it must be deterministic, symmetric,
		 * side-effect-free, and must not fail or throw.
		 */
		bool RegisterTypeEquals(
			FHostTypeId TypeId,
			FNativeCallback Callback,
			void* UserData,
			std::string& OutErrorMessageId);

		/**
		 * Stable hash for a host struct. Callback receives Args[0] as HostObject
		 * and returns uint64 (int64/int are accepted by the runtime adapter).
		 * It must be deterministic, side-effect-free, must not fail or throw, and
		 * values considered equal must produce the same hash.
		 */
		bool RegisterTypeHash(
			FHostTypeId TypeId,
			FNativeCallback Callback,
			void* UserData,
			std::string& OutErrorMessageId);

		/**
		 * Allow script classes to inherit this host class (`class X : HostName`).
		 * Rejects non-Class kinds.
		 */
		bool SetTypeInheritable(
			FHostTypeId TypeId,
			bool bInheritable,
			std::string& OutErrorMessageId);

		/**
		 * Select strong/scoped semantics for a host enum. Enabling this stamps all
		 * enum members with the registered canonical type name. Disabling is rejected
		 * after typed enum members exist so value identity cannot silently change.
		 */
		bool SetEnumScoped(
			FHostTypeId TypeId,
			bool bScoped,
			std::string& OutErrorMessageId);

		bool SetEnumUnderlyingType(
			FHostTypeId TypeId,
			std::string UnderlyingTypeName,
			std::string& OutErrorMessageId);

		/**
		 * Factory invoked when constructing a script class that inherits this host type.
		 * Synthesizes an internal `@subclass` native. Callback Args[0] = script type name.
		 */
		bool RegisterTypeSubclassFactory(
			FHostTypeId TypeId,
			FNativeCallback Callback,
			void* UserData,
			std::string& OutErrorMessageId);

		/**
		 * Register an annotation name policy and documentation. Accepts
		 * `Name(...)` and `@name(...)` spellings. The name policy also controls
		 * processor subscriptions and reflection; it does not execute a processor.
		 */
		bool RegisterAnnotation(
			std::string Name,
			std::string Detail,
			std::string& OutErrorMessageId,
			std::string Documentation = {},
			std::vector<FHostAnnotationArgumentDesc> Arguments = {},
			std::uint32_t Targets = 131071,
			bool CaseSensitive = true);

		/**
		 * Attach optional tooling documentation after normal validated registration.
		 * Metadata never affects runtime dispatch or compiler semantics.
		 */
		bool SetFunctionDocumentation(
			std::string_view Name,
			std::string Documentation,
			std::vector<std::string> ParamNames,
			std::vector<std::string> ParamDocumentation,
			std::string ReturnDocumentation,
			std::string& OutErrorMessageId);

		bool SetTypeDocumentation(
			FHostTypeId TypeId,
			std::string Documentation,
			std::string& OutErrorMessageId);

		void Clear();
		// Publish changes to host-owned callback state that affect cached code.
		// Ordinary state read by a callback does not require invalidation.
		void InvalidateRuntimeBindings() noexcept;

		// Exchange all registered bindings with Another (used for atomic catalog import).
		void Swap(FHostBindingRegistry& Other) noexcept;

	private:
		static bool IsValidParamType(ENativeType Type);
		static bool IsValidReturnType(ENativeType Type);
		bool TryAddNative(
			FNativeFunction Entry,
			std::string& OutErrorMessageId,
			bool bExposeAsFreeFunction = true);

		std::shared_ptr<void> RegistrationOwner;
		std::shared_ptr<const void> RegistrationProviderIdentity;
		FBindingOrigin RegistrationOrigin;
		std::vector<FNativeFunction> Natives;
		std::vector<FHostFunctionDesc> Functions;
		std::vector<FHostTypeDesc> Types;
		std::vector<FHostTypeAliasDesc> TypeAliases;
		std::vector<FGenericHostTypeTemplateDesc> GenericTypeTemplates;
		std::vector<FHostAnnotationDesc> Annotations;
		std::uint32_t NextTypeId = 1;
		std::uint32_t NextGenericTemplateId = 1;
		std::uint64_t Revision = 0;
	};

	// Human-readable signature, e.g. `int (int, string)` or `Actor (Widget)`.
	std::string FormatHostFunctionSignature(const FHostFunctionDesc& Function);

	std::string FormatHostMemberSignature(
		const FHostMemberDesc& Member,
		std::string_view OwnerTypeName = {});

	// Source-like, host-agnostic rendering for hover/completion/symbol details.
	std::string FormatHostAnnotations(
		const std::vector<FHostAnnotationInstance>& Annotations);

	// Markdown-friendly documentation used by completion/hover frontends.
	std::string FormatHostDocumentation(
		std::string_view Documentation,
		const std::vector<std::string>& ParamNames = {},
		const std::vector<std::string>& ParamDocumentation = {},
		std::string_view ReturnDocumentation = {});

	std::string FormatHostDocumentation(
		std::string_view Documentation,
		const std::vector<std::string>& ParamNames,
		const std::vector<std::string>& ParamDocumentation,
		std::string_view ReturnDocumentation,
		const std::vector<FHostAnnotationInstance>& Annotations,
		const std::vector<std::vector<FHostAnnotationInstance>>& ParamAnnotations);

	inline const char* HostTypeKindName(const EHostTypeKind Kind)
	{
		switch (Kind)
		{
		case EHostTypeKind::Enum:
			return "enum";
		case EHostTypeKind::Struct:
			return "struct";
		case EHostTypeKind::Class:
			return "class";
		default:
			return "type";
		}
	}
}
