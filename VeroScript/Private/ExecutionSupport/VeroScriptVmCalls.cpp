// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptEnumValueIdentity.h"
#include "VeroScriptVm.h"

#include "VeroScriptBuiltins.h"
#include "VeroScriptConcurrent.h"
#include "VeroScriptDebugger.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptHotReloadInternal.h"
#include "VeroScriptJit.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptVmInternal.h"
#include "VeroScriptVmArguments.h"
#include "VeroScriptVmNativeLeaves.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace VeroScript
{
	namespace
	{
		bool IsUnsignedIntTypeName(const std::string_view TypeName)
		{
			return TypeName == "unsigned int" || TypeName == "uint" || TypeName == "unsigned" || TypeName == "uint32";
		}

		void ReserveMapStorage(
		    FMap& Map,
		    const std::size_t Count,
		    const bool bScopedEnumKey)
		{
			Map.ReservedCapacityHint = Count;
			if (Map.ScalarEntries.index() != 0)
			{
				std::visit(
				    [Count](auto& Entries)
				{
					using TEntries = std::decay_t<decltype(Entries)>;
					if constexpr (!std::is_same_v<TEntries, std::monostate>)
					{
						Entries.reserve(Count);
					}
				},
				    Map.ScalarEntries);
				return;
			}
			if (VmDetail::IsSigned32KeyStorageType(Map.KeyTypeName))
			{
				Map.IntEntries.reserve(Count);
			}
			else if (IsUnsignedIntTypeName(Map.KeyTypeName))
			{
				Map.UIntEntries.reserve(Count);
			}
			else if (Map.KeyTypeName == "int64")
			{
				Map.Int64Entries.reserve(Count);
			}
			else if (Map.KeyTypeName == "uint64")
			{
				Map.UInt64Entries.reserve(Count);
			}
			else if (Map.KeyTypeName == "string")
			{
				Map.StrEntries.reserve(Count);
			}
			else if (Map.KeyTypeName == "wstring")
			{
				Map.WStrEntries.reserve(Count);
			}
			else if (bScopedEnumKey || !Map.EnumEntries.empty())
			{
				Map.EnumEntries.reserve(Count);
			}
			else
			{
				Map.HostEntries.reserve(Count);
			}
		}

		void ReserveOrderedMapStorage(
		    FOrderedMap& Map,
		    const std::size_t Count,
		    const bool bScopedEnumKey)
		{
			if (VmDetail::IsSigned32KeyStorageType(Map.KeyTypeName))
			{
				Map.IntKeyOrder.reserve(Count);
				Map.IntEntries.reserve(Count);
			}
			else if (IsUnsignedIntTypeName(Map.KeyTypeName))
			{
				Map.UIntKeyOrder.reserve(Count);
				Map.UIntEntries.reserve(Count);
			}
			else if (Map.KeyTypeName == "int64")
			{
				Map.Int64KeyOrder.reserve(Count);
				Map.Int64Entries.reserve(Count);
			}
			else if (Map.KeyTypeName == "uint64")
			{
				Map.UInt64KeyOrder.reserve(Count);
				Map.UInt64Entries.reserve(Count);
			}
			else if (Map.KeyTypeName == "string")
			{
				Map.StrKeyOrder.reserve(Count);
				Map.StrEntries.reserve(Count);
			}
			else if (Map.KeyTypeName == "wstring")
			{
				Map.WStrKeyOrder.reserve(Count);
				Map.WStrEntries.reserve(Count);
			}
			else if (bScopedEnumKey || !Map.EnumEntries.empty())
			{
				Map.EnumKeyOrder.reserve(Count);
				Map.EnumEntries.reserve(Count);
			}
			else
			{
				Map.HostKeyOrder.reserve(Count);
				Map.HostEntries.reserve(Count);
			}
		}

		void ReserveSetStorage(
		    FSet& Set,
		    const std::size_t Count,
		    const bool bScopedEnumElement)
		{
			Set.ReservedCapacityHint = Count;
			if (Set.ScalarElements.index() != 0)
			{
				std::visit(
				    [Count](auto& Elements)
				{
					using TElements = std::decay_t<decltype(Elements)>;
					if constexpr (!std::is_same_v<TElements, std::monostate>)
					{
						Elements.reserve(Count);
					}
				},
				    Set.ScalarElements);
				return;
			}
			if (VmDetail::IsSigned32KeyStorageType(Set.ElementTypeName))
			{
				Set.IntElements.reserve(Count);
			}
			else if (IsUnsignedIntTypeName(Set.ElementTypeName))
			{
				Set.UIntElements.reserve(Count);
			}
			else if (Set.ElementTypeName == "int64")
			{
				Set.Int64Elements.reserve(Count);
			}
			else if (Set.ElementTypeName == "uint64")
			{
				Set.UInt64Elements.reserve(Count);
			}
			else if (Set.ElementTypeName == "string")
			{
				Set.StrElements.reserve(Count);
			}
			else if (Set.ElementTypeName == "wstring")
			{
				Set.WStrElements.reserve(Count);
			}
			else if (bScopedEnumElement || !Set.EnumElements.empty())
			{
				Set.EnumElements.reserve(Count);
			}
			else
			{
				Set.HostElements.reserve(Count);
			}
		}

		std::string ResolveCallMethodKey(
		    const std::string& OwnerLinkage,
		    const std::string& MethodName,
		    const std::size_t ArgumentCount)
		{
			if (MethodName.find('#') != std::string::npos)
			{
				return OwnerLinkage + "::" + MethodName;
			}
			return MakeArityMethodFunctionName(OwnerLinkage, MethodName, ArgumentCount);
		}

	}

	bool VeroScriptVm::CallNative(
	    const std::uint16_t NativeIndex,
	    const std::uint8_t ArgumentCount,
	    const FSourceLocation& Location)
	{
		FMemoryPollScope MemoryPollScope(*this);
		VmDetail::FArgumentBuffer StackArgs(ArgumentCount <= 8u ? ArgumentCount : 0u);
		std::vector<FValue> HeapArgs;
		FValue* Arguments = nullptr;
		if (!PopOperandArguments(ArgumentCount, StackArgs.data(), HeapArgs, Arguments))
		{
			return false;
		}

		std::vector<FValue> OldArguments, DeferredValues;
		FValue OutReturn = std::int32_t{0};
		const auto Dispatch = [&]() -> bool
		{
			// Coercion may replace an argument (container copy or host-base
			// projection). Keep its original language-owned graph until cleanup.
			// Scalar-only calls retain their allocation-free argument fast path.
			for (std::size_t I = 0; I < ArgumentCount; ++I)
			{
				const auto& V = Arguments[I];
				if (std::holds_alternative<FObjectHandle>(V) || std::holds_alternative<FFunctionHandle>(V) || std::holds_alternative<FScriptVariantHandle>(V) || std::holds_alternative<FNativeArray>(V) || std::holds_alternative<FDynamicArrayHandle>(V) || std::holds_alternative<FMapHandle>(V) || std::holds_alternative<FOrderedMapHandle>(V) || std::holds_alternative<FQueueHandle>(V) || std::holds_alternative<FStackHandle>(V) || std::holds_alternative<FDequeHandle>(V))
				{
					if (OldArguments.empty())
					{
						OldArguments.reserve(ArgumentCount);
					}
					OldArguments.push_back(V);
				}
			}
			if (!ActiveNatives || NativeIndex >= ActiveNatives->size())
			{
				ReportError("invalid native index", Location);
				return false;
			}

			const FNativeFunction& Native = (*ActiveNatives)[NativeIndex];
			if (!Native.Callback)
			{
				ReportError("null native callback: " + Native.Name, Location);
				return false;
			}
			// Core-owned value leaves cannot call script or mutate registrations. Their
			// exact input representations were checked by the owning implementation;
			// retain the ordinary memory/work scopes, root cleanup and error protocol.
			if (Memory::IsValueLeaf(Native, Arguments, ArgumentCount) || Numerics::IsValueLeaf(Native, Arguments, ArgumentCount))
			{
				// Private payload identity does not replace a host registration stamp.
				for (std::size_t Index = 0; Index < ArgumentCount; ++Index)
				{
					const auto* Host = std::get_if<FHostObjectHandle>(&Arguments[Index]);
					const std::string_view Expected = Index < Native.RuntimeParamTypeNames.size() && !Native.RuntimeParamTypeNames[Index].empty() ? Native.RuntimeParamTypeNames[Index]
					    : Index < Native.ParamHostTypeNames.size()                                                                                ? Native.ParamHostTypeNames[Index]
					                                                                                                                              : std::string_view{};
					if (Host && *Host && (*Host)->ProviderIdentity && !HostObjectMatchesType(*Host, Expected))
					{
						ReportError("host object type mismatch or native registration mismatch: " + Native.Name, Location);
						return false;
					}
				}
				// Preserve registration and reference scopes across work checks.
				// These sealed leaves cannot call script. Without an external check or
				// retained native roots, no callback can replace the registration table.
				// Reentrant runs retain the ordinary owned diagnostic snapshot.
				const bool CanBorrowSignature = !ExternalExecutionCheck && !NativeValueRoots;
				const std::string NativeNameStorage = CanBorrowSignature ? std::string{} : Native.Name;
				const std::string& NativeName = CanBorrowSignature ? Native.Name : NativeNameStorage;
				const ENativeType ReturnType = Native.ReturnType;
				const std::string ReturnHost = ReturnType == ENativeType::HostObject ? ResolveNativeReturnTypeName(Native) : std::string{};
				const auto GenericTemplate = Native.ReturnGenericTemplateId;
				const auto GenericArguments = Native.ReturnGenericTypeArguments;
				FScriptError Diagnostic;
				FNativeCallContext Context;
				Context.Args = Arguments;
				Context.ArgCount = ArgumentCount;
				Context.UserData = Native.UserData;
				Context.MemoryServices = this;
				Context.NumericServices = this;
				Context.ScriptInvoker = this;
				Context.I18nServices = this;
				Context.DeferredValues = &DeferredValues;
				Context.DiagnosticOutput = &Diagnostic;
				FNativeReferenceScope References(*this, nullptr, 0);
				FNativeValueScope Values(*this, Arguments, ArgumentCount, OutReturn);
				std::string Error;
				bool Ok = false;
				try
				{
					NotifyExecutionCommitted();
					Ok = Native.Callback(Context, OutReturn, Error);
				}
				catch (const std::exception& Exception)
				{
					Diagnostic = {};
					Error = std::string("native callback threw: ") + Exception.what();
				}
				catch (...)
				{
					Diagnostic = {};
					Error = "native callback threw an unknown exception";
				}
				std::string RootFailure;
				FNativeValueScope Deferred(*this, DeferredValues.data(), DeferredValues.size(), OutReturn);
				const bool RootsCleaned = Values.Previous || DrainNativeValueRoots(RootFailure);
				if (!Ok) return NativeFailure(std::move(Diagnostic), std::move(Error), NativeName, Location);
				if (!RootsCleaned)
				{
					if (!bHasError)
					{
						ReportError(std::move(RootFailure), Location);
					}
					return false;
				}
				if (!CoerceValueToNativeType(ReturnType, OutReturn, Error))
				{
					ReportError(Error.empty() ? "native return type mismatch: " + NativeName
					                          : Error + ": " + NativeName,
					    Location);
					return false;
				}
				if (auto* Host = std::get_if<FHostObjectHandle>(&OutReturn); Host && *Host)
				{
					if (!HostObjectMatchesType(*Host, ReturnHost))
					{
						ReportError("native returned host object of wrong type: " + NativeName, Location);
						return false;
					}
					(*Host)->GenericTemplateId = GenericTemplate;
					(*Host)->GenericTypeArguments = GenericArguments;
					StampHostValueCallbacks(*Host);
				}
				Push(std::move(OutReturn));
				return true;
			}
			const std::size_t VisibleArgumentCount = Native.ParamTypes.size();
			const std::size_t TypeArgumentCount =
			    Native.bGenericHostObjectCast ? Native.TypeParams.size() : 0u;
			if (ArgumentCount != NativeRuntimeArgumentCount(Native))
			{
				ReportError(
				    Native.Name + " expects exactly " + std::to_string(VisibleArgumentCount) + " argument(s)",
				    Location);
				return false;
			}
			std::vector<std::string> ConcreteTypeArguments;
			ConcreteTypeArguments.reserve(TypeArgumentCount);
			for (std::size_t TypeIndex = 0; TypeIndex < TypeArgumentCount; ++TypeIndex)
			{
				const FValue& Encoded = Arguments[VisibleArgumentCount + TypeIndex];
				const auto* const TypeName = std::get_if<std::string>(&Encoded);
				if (!TypeName || TypeName->empty())
				{
					ReportError(
					    "invalid generic native type argument: " + Native.Name,
					    Location);
					return false;
				}
				ConcreteTypeArguments.push_back(*TypeName);
			}
			if (!NativeParamFlagsAreValid(Native))
			{
				ReportError("invalid native parameter flags: " + Native.Name, Location);
				return false;
			}
			if (!NativeFunctionContainerMetadataIsValid(Native))
			{
				ReportError("invalid native container metadata: " + Native.Name, Location);
				return false;
			}
			bool bHasMutableRef = false;
			for (std::size_t ArgumentIndex = 0;
			    ArgumentIndex < VisibleArgumentCount;
			    ++ArgumentIndex)
			{
				if (NativeParamFlagIsMutableRef(
				        NativeParamFlagAt(Native, ArgumentIndex)))
				{
					bHasMutableRef = true;
					break;
				}
			}
			// The overwhelmingly common native signature has no ref/out parameters.
			// Keep that path allocation-free; materialize write-back tracking only when
			// at least one mutable parameter actually requires it.
			std::vector<std::string> MutableRefExpectedTypes;
			std::vector<FValue> MutableRefBindings;
			std::vector<bool> MutableRefArguments;
			std::vector<bool> MutableRefHasAddressableTarget;
			std::vector<FValue*> MutableCallbackArgs;
			std::vector<FValue> OriginalReferences;
			for (std::size_t I = 0; I < VisibleArgumentCount; ++I)
			{
				if (NativeParamFlagIsRef(NativeParamFlagAt(Native, I)))
				{
					OriginalReferences.assign(VisibleArgumentCount, FValue{std::int32_t{0}});
					break;
				}
			}
			if (bHasMutableRef)
			{
				MutableRefExpectedTypes.resize(VisibleArgumentCount);
				MutableRefBindings.assign(
				    VisibleArgumentCount, FValue{std::int32_t{0}});
				MutableRefArguments.assign(VisibleArgumentCount, false);
				MutableRefHasAddressableTarget.assign(VisibleArgumentCount, false);
				MutableCallbackArgs.assign(VisibleArgumentCount, nullptr);
			}
			for (std::size_t ArgumentIndex = 0;
			    ArgumentIndex < VisibleArgumentCount;
			    ++ArgumentIndex)
			{
				const std::uint8_t ParamFlag = NativeParamFlagAt(Native, ArgumentIndex);
				if (NativeParamFlagIsRef(ParamFlag))
				{
					OriginalReferences[ArgumentIndex] = Arguments[ArgumentIndex];
				}
				const bool bMutableRef = NativeParamFlagIsMutableRef(ParamFlag);
				if (bMutableRef)
				{
					MutableRefArguments[ArgumentIndex] = true;
					const FValue& Binding = Arguments[ArgumentIndex];
					if (std::holds_alternative<FLocalRef>(Binding) || std::holds_alternative<FGlobalRef>(Binding) || std::holds_alternative<FThisFieldRef>(Binding) || std::holds_alternative<FIndexRef>(Binding) || std::holds_alternative<FMapEntryRef>(Binding) || IsClosureReferenceCell(Binding))
					{
						MutableRefBindings[ArgumentIndex] = Binding;
						MutableRefHasAddressableTarget[ArgumentIndex] = true;
					}
				}
				// Script `ref` args arrive as FLocalRef / field refs; resolve to storage first.
				// Non-ref values resolve to themselves (&arg).
				if (FValue* const Resolved = ResolveRefStorage(Arguments[ArgumentIndex]))
				{
					Arguments[ArgumentIndex] = *Resolved;
				}
				else
				{
					ReportError("invalid ref argument for native: " + Native.Name, Location);
					return false;
				}

				const bool bDeepCopyContainers = !NativeParamFlagIsMutableRef(ParamFlag);
				if (!bDeepCopyContainers)
				{
					MutableCallbackArgs[ArgumentIndex] = &Arguments[ArgumentIndex];
					if (NativeTypeIsContainer(Native.ParamTypes[ArgumentIndex]))
					{
						MutableRefExpectedTypes[ArgumentIndex] =
						    ResolveNativeParamTypeName(Native, ArgumentIndex);
					}
				}
				std::string CoerceError;
				if (Native.ParamTypes[ArgumentIndex] == ENativeType::Variant && !ValidateVariantValue(Arguments[ArgumentIndex])) return false;
				if (!Numerics::ValidateArgument(Native, ArgumentIndex, Arguments[ArgumentIndex], CoerceError))
				{
					ReportErrorId(CoerceError, Location);
					return false;
				}
				if (Native.bGenericHostObjectCast && Native.ParamTypes[ArgumentIndex] == ENativeType::HostObject)
				{
					const auto* Object = std::get_if<FObjectHandle>(&Arguments[ArgumentIndex]);
					if (Object && *Object && (*Object)->Kind == EScriptObjectKind::Class && !(*Object)->GetHostBase() && ResolveNativeParamTypeName(Native, ArgumentIndex) == "object")
					{
						// Generic casts accept script references as well as native handles.
						// Keep ordinary host coercion for null and host-backed subclasses.
						continue;
					}
				}
				if (!CoerceValueToNativeType(
				        Native.ParamTypes[ArgumentIndex],
				        Arguments[ArgumentIndex],
				        CoerceError,
				        bDeepCopyContainers))
				{
					ReportError(
					    CoerceError.empty()
					        ? ("native argument type mismatch: " + Native.Name)
					        : (CoerceError + ": " + Native.Name),
					    Location);
					return false;
				}
				if (Native.ParamTypes[ArgumentIndex] == ENativeType::HostObject)
				{
					const auto* Host = std::get_if<FHostObjectHandle>(&Arguments[ArgumentIndex]);
					const auto Expected = ResolveNativeParamTypeName(Native, ArgumentIndex);
					if (!Host || (*Host && !HostObjectMatchesType(*Host, Expected)))
					{
						ReportError("host object type mismatch or native registration mismatch: " + Native.Name,
						    Location);
						return false;
					}
				}
				else if (Native.ParamTypes[ArgumentIndex] == ENativeType::Enum)
				{
					const std::string ExpectedEnum =
					    ResolveNativeParamTypeName(Native, ArgumentIndex);
					const auto* const EnumValue =
					    std::get_if<FEnumValue>(&Arguments[ArgumentIndex]);
					if (!EnumValue || EnumValue->TypeName != ExpectedEnum)
					{
						ReportError(
						    "native enum type mismatch: " + Native.Name,
						    Location);
						return false;
					}
				}
				else if (Native.ParamTypes[ArgumentIndex] == ENativeType::Function)
				{
					const std::string ExpectedFunctionType =
					    ResolveNativeParamTypeName(Native, ArgumentIndex);
					const auto* const Function =
					    std::get_if<FFunctionHandle>(&Arguments[ArgumentIndex]);
					if (!Function || !*Function || (*Function)->ProgramGeneration != ProgramGeneration || !VmDetail::RuntimeTypeNamesEqual((*Function)->TypeName, ExpectedFunctionType))
					{
						ReportError(
						    "native function signature mismatch: " + Native.Name,
						    Location);
						return false;
					}
				}
				else if (NativeTypeIsContainer(Native.ParamTypes[ArgumentIndex]))
				{
					const std::string ExpectedType =
					    ResolveNativeParamTypeName(Native, ArgumentIndex);
					const FNativeTypeDesc* const ExpectedDesc =
					    Native.bHasStructuredTypeDescs && ArgumentIndex < Native.StructuredParamTypes.size()
					    ? &Native.StructuredParamTypes[ArgumentIndex]
					    : nullptr;
					if (ExpectedType.empty() || !ValidateAndStampNativeContainerValue(Arguments[ArgumentIndex], ExpectedType, Location, ExpectedDesc))
					{
						ReportError(
						    "native container argument type mismatch: " + Native.Name,
						    Location);
						return false;
					}
				}
			}

			// Snapshot before callback: host must not rely on Session mutation during Run,
			// but reallocation/replacement must not leave dangling references here.
			const std::string NativeName = Native.Name;
			const ENativeType NativeReturnType = Native.ReturnType;
			const std::string ExpectedReturnHost = ResolveNativeReturnTypeName(Native);
			std::vector<ENativeType> NativeParamTypes;
			std::vector<std::string> ExpectedParamTypeNames;
			if (bHasMutableRef)
			{
				NativeParamTypes = Native.ParamTypes;
				ExpectedParamTypeNames.resize(VisibleArgumentCount);
				for (std::size_t ArgumentIndex = 0;
				    ArgumentIndex < VisibleArgumentCount;
				    ++ArgumentIndex)
				{
					if (MutableRefArguments[ArgumentIndex])
					{
						ExpectedParamTypeNames[ArgumentIndex] =
						    ResolveNativeParamTypeName(Native, ArgumentIndex);
					}
				}
			}
			const bool bGenericHostObjectCast = Native.bGenericHostObjectCast;
			std::string GenericReturnType;
			if (bGenericHostObjectCast)
			{
				const auto ReturnParameter = std::find(
				    Native.TypeParams.begin(), Native.TypeParams.end(), Native.ScriptReturnTypeName);
				if (ReturnParameter == Native.TypeParams.end())
				{
					ReportError("invalid generic native return type: " + NativeName, Location);
					return false;
				}
				GenericReturnType = ConcreteTypeArguments[static_cast<std::size_t>(ReturnParameter - Native.TypeParams.begin())];
			}
			const bool bHasStructuredTypeDescs = Native.bHasStructuredTypeDescs;
			FNativeTypeDesc StructuredReturnType;
			if (bHasStructuredTypeDescs && NativeTypeIsContainer(NativeReturnType))
			{
				StructuredReturnType = Native.StructuredReturnType;
			}
			std::vector<FNativeTypeDesc> StructuredParamTypes;
			if (bHasStructuredTypeDescs && bHasMutableRef)
			{
				StructuredParamTypes = Native.StructuredParamTypes;
			}
			const FNativeCallback NativeCallback = Native.Callback;
			void* const NativeUserData = Native.UserData;

			FScriptError NativeDiagnostic;
			FNativeCallContext Context;
			Context.Args = Arguments;
			Context.MutableArgs = bHasMutableRef ? MutableCallbackArgs.data() : nullptr;
			Context.ArgCount = VisibleArgumentCount;
			Context.TypeArguments = ConcreteTypeArguments.data();
			Context.TypeArgumentCount = ConcreteTypeArguments.size();
			Context.UserData = NativeUserData;
			Context.HostThis = nullptr;
			Context.ScriptInvoker = this;
			Context.MemoryServices = this;
			Context.I18nServices = this;
			Context.NumericServices = this;
			Context.DiagnosticOutput = &NativeDiagnostic;
			Context.DeferredValues = &DeferredValues;
			if (!Native.HostTypeName.empty() && VisibleArgumentCount > 0 && !Native.ParamTypes.empty() && Native.ParamTypes[0] == ENativeType::HostObject)
			{
				if (!Native.bHostStaticMethod)
				{
					Context.HostThis = HostObjectPayload(Arguments[0]);
				}
				const std::string ExpectedThisType = ResolveNativeParamTypeName(Native, 0);
				if (!ExpectedThisType.empty() && ExpectedThisType != "object")
				{
					const auto* const Handle = std::get_if<FHostObjectHandle>(&Arguments[0]);
					if (!Handle
					    // A real instance receiver cannot be null, while a static host method
					    // may intentionally take a nullable HostObject as its first explicit
					    // parameter.
					    || (!*Handle && !Native.bHostStaticMethod) || (*Handle && !HostObjectMatchesType(*Handle, ExpectedThisType)))
					{
						ReportError(
						    "host object type mismatch for native: " + NativeName,
						    Location);
						return false;
					}
				}
			}

			FNativeReferenceScope ReferenceScope(*this,
			    OriginalReferences.empty() ? nullptr : OriginalReferences.data(), OriginalReferences.size());
			std::string OutError;
			if (bHasMutableRef)
			{
				OldArguments.reserve(VisibleArgumentCount);
				for (std::size_t I = 0; I < VisibleArgumentCount; ++I)
				{
					if (MutableRefArguments[I])
					{
						OldArguments.push_back(Arguments[I]);
					}
				}
			}
			bool bCallbackOk = false;
			FNativeValueScope ValueScope(*this, Arguments, VisibleArgumentCount, OutReturn);
			try
			{
				NotifyExecutionCommitted();
				bCallbackOk = NativeCallback(Context, OutReturn, OutError);
			}
			catch (const std::exception& Exception)
			{
				NativeDiagnostic = {};
				OutError = std::string("native callback threw: ") + Exception.what();
			}
			catch (...)
			{
				NativeDiagnostic = {};
				OutError = "native callback threw an unknown exception";
			}
			std::string RootFailure;
			FNativeValueScope OriginalValues(*this, OldArguments.data(), OldArguments.size(), OutReturn);
			FNativeValueScope DeferredScope(*this, DeferredValues.data(), DeferredValues.size(), OutReturn);
			// Nested invocation may still borrow native-owned temporary storage. The
			// outer callback's return is the first safe point after those copies die.
			const bool RootsCleaned = ValueScope.Previous || DrainNativeValueRoots(RootFailure);
			if (!bCallbackOk)
			{
				return NativeFailure(std::move(NativeDiagnostic), std::move(OutError), NativeName, Location);
			}
			if (!RootsCleaned)
			{
				if (!bHasError)
				{
					ReportError(std::move(RootFailure), Location);
				}
				return false;
			}

			for (std::size_t ArgumentIndex = 0;
			    ArgumentIndex < MutableRefExpectedTypes.size();
			    ++ArgumentIndex)
			{
				if (MutableRefExpectedTypes[ArgumentIndex].empty())
				{
					continue;
				}
				if (!ValidateAndStampNativeContainerValue(
				        Arguments[ArgumentIndex],
				        MutableRefExpectedTypes[ArgumentIndex],
				        Location,
				        bHasStructuredTypeDescs && ArgumentIndex < StructuredParamTypes.size()
				            ? &StructuredParamTypes[ArgumentIndex]
				            : nullptr))
				{
					ReportError(
					    "native corrupted mutable-ref container: " + NativeName,
					    Location);
					return false;
				}
			}

			// A host may replace any mutable-ref slot, including scalars and handles.
			// Validate every replacement before committing any scalar/whole-value
			// write-back so a malformed callback cannot partially update caller state.
			for (std::size_t ArgumentIndex = 0;
			    ArgumentIndex < VisibleArgumentCount;
			    ++ArgumentIndex)
			{
				if (!bHasMutableRef || !MutableRefArguments[ArgumentIndex])
				{
					continue;
				}
				std::string CoerceError;
				if (!CoerceValueToNativeType(
				        NativeParamTypes[ArgumentIndex],
				        Arguments[ArgumentIndex],
				        CoerceError,
				        false))
				{
					ReportError(
					    CoerceError.empty()
					        ? ("native mutable-ref type mismatch: " + NativeName)
					        : (CoerceError + ": " + NativeName),
					    Location);
					return false;
				}
				if (NativeParamTypes[ArgumentIndex] == ENativeType::Variant && !ValidateVariantValue(Arguments[ArgumentIndex])) return false;
				const std::string& ExpectedType = ExpectedParamTypeNames[ArgumentIndex];
				if (NativeParamTypes[ArgumentIndex] == ENativeType::Enum)
				{
					const auto* const EnumValue =
					    std::get_if<FEnumValue>(&Arguments[ArgumentIndex]);
					if (!EnumValue || EnumValue->TypeName != ExpectedType)
					{
						ReportError(
						    "native enum mutable-ref type mismatch: " + NativeName,
						    Location);
						return false;
					}
				}
				else if (NativeParamTypes[ArgumentIndex] == ENativeType::Function)
				{
					const auto* const Function =
					    std::get_if<FFunctionHandle>(&Arguments[ArgumentIndex]);
					if (!Function || !*Function || (*Function)->ProgramGeneration != ProgramGeneration || !VmDetail::RuntimeTypeNamesEqual((*Function)->TypeName, ExpectedType))
					{
						ReportError(
						    "native function mutable-ref signature mismatch: " + NativeName,
						    Location);
						return false;
					}
				}
				else if (NativeParamTypes[ArgumentIndex] == ENativeType::HostObject && !ExpectedType.empty() && ExpectedType != "object")
				{
					const auto* const Host =
					    std::get_if<FHostObjectHandle>(&Arguments[ArgumentIndex]);
					if (!Host || (*Host && !HostObjectMatchesType(*Host, ExpectedType)))
					{
						ReportError(
						    "native host object mutable-ref type mismatch: " + NativeName,
						    Location);
						return false;
					}
				}
			}
			for (std::size_t ArgumentIndex = 0;
			    ArgumentIndex < VisibleArgumentCount;
			    ++ArgumentIndex)
			{
				if (!bHasMutableRef || !MutableRefHasAddressableTarget[ArgumentIndex])
				{
					continue;
				}
				FValue* const Target =
				    ResolveRefStorage(MutableRefBindings[ArgumentIndex]);
				if (!Target)
				{
					ReportError(
					    "native mutable-ref target became invalid: " + NativeName,
					    Location);
					return false;
				}
				if (!AssignValue(
				        *Target, Arguments[ArgumentIndex], Location))
				{
					return false;
				}
				FValue& Binding = MutableRefBindings[ArgumentIndex];
				NotifyReferenceMutation(Binding);
				if (bHasError)
				{
					return false;
				}
			}

			if (NativeReturnType == ENativeType::Void)
			{
				return true;
			}
			if (bGenericHostObjectCast)
			{
				bool bMatches = false;
				if (const auto* const Host = std::get_if<FHostObjectHandle>(&OutReturn))
				{
					bMatches = !*Host || HostObjectMatchesType(*Host, GenericReturnType);
					if (bMatches && *Host)
					{
						StampHostValueCallbacks(*Host);
					}
				}
				else if (const auto* const Object = std::get_if<FObjectHandle>(&OutReturn))
				{
					bMatches = !*Object || ((*Object)->Kind == EScriptObjectKind::Class && (ValueMatchesElementType(OutReturn, GenericReturnType, Location) || ((*Object)->GetHostBase() && HostObjectMatchesType((*Object)->GetHostBase(), GenericReturnType))));
				}
				if (!bMatches)
				{
					ReportError(
					    "generic host cast returned an incompatible object: " + NativeName,
					    Location);
					return false;
				}
				Push(std::move(OutReturn));
				return true;
			}
			if (NativeReturnType == ENativeType::HostObject && !ExpectedReturnHost.empty() && ExpectedReturnHost != "object")
			{
				if (const auto* const ScriptHandle = std::get_if<FObjectHandle>(&OutReturn);
				    ScriptHandle && !*ScriptHandle)
				{
					OutReturn = FHostObjectHandle{};
				}
				const auto* const Handle = std::get_if<FHostObjectHandle>(&OutReturn);
				if (!Handle)
				{
					ReportError("native must return a host object: " + NativeName, Location);
					return false;
				}
				if (!*Handle)
				{
					// Host reference types are nullable. The declared native return type
					// supplies the static type for this otherwise untyped null value.
				}
				else if ((*Handle)->TypeName.empty())
				{
					(*Handle)->TypeName = ExpectedReturnHost;
				}
				else if (!HostObjectMatchesType(*Handle, ExpectedReturnHost))
				{
					ReportError(
					    "native returned host object of wrong type: " + NativeName,
					    Location);
					return false;
				}
				if (*Handle && (*Handle)->TypeId == 0)
				{
					// Leave TypeId as host-provided; zero is allowed for adapters that only set name.
				}
			}
			if (NativeReturnType == ENativeType::Enum)
			{
				const auto* const EnumValue = std::get_if<FEnumValue>(&OutReturn);
				if (!EnumValue || EnumValue->TypeName != ExpectedReturnHost)
				{
					ReportError(
					    "native returned enum of wrong type: " + NativeName,
					    Location);
					return false;
				}
			}
			if (NativeReturnType == ENativeType::Function)
			{
				const auto* const Function = std::get_if<FFunctionHandle>(&OutReturn);
				if (!Function || !*Function || (*Function)->ProgramGeneration != ProgramGeneration || !VmDetail::RuntimeTypeNamesEqual((*Function)->TypeName, ExpectedReturnHost))
				{
					ReportError(
					    "native returned function of wrong signature: " + NativeName,
					    Location);
					return false;
				}
			}
			if (!CoerceValueToNativeType(NativeReturnType, OutReturn, OutError))
			{
				ReportError(
				    OutError.empty()
				        ? ("native return type mismatch: " + NativeName)
				        : (OutError + ": " + NativeName),
				    Location);
				return false;
			}
			if (NativeReturnType == ENativeType::Variant && !ValidateVariantValue(OutReturn)) return false;
			if (NativeReturnType == ENativeType::HostObject)
			{
				if (const auto* Host = std::get_if<FHostObjectHandle>(&OutReturn);
				    Host && *Host && !HostObjectMatchesType(*Host, "object"))
				{
					ReportError("native returned an object from another registration: " + NativeName, Location);
					return false;
				}
				if (const auto* const Handle = std::get_if<FHostObjectHandle>(&OutReturn); Handle && *Handle)
				{
					(*Handle)->GenericTemplateId = Native.ReturnGenericTemplateId;
					(*Handle)->GenericTypeArguments = Native.ReturnGenericTypeArguments;
					StampHostValueCallbacks(*Handle);
				}
			}
			if (NativeTypeIsContainer(NativeReturnType))
			{
				if (!ValidateAndStampNativeContainerValue(
				        OutReturn,
				        ExpectedReturnHost,
				        Location,
				        bHasStructuredTypeDescs ? &StructuredReturnType : nullptr))
				{
					ReportError(
					    "native returned container with invalid contents: " + NativeName,
					    Location);
					return false;
				}
			}
			Push(std::move(OutReturn));
			return true;
		};
		const auto Cleanup = [&]() -> bool
		{
			bool Ok = ReleaseValue(OutReturn, Location);
			Ok = ReleaseValueList(DeferredValues, Location) && Ok;
			Ok = ReleaseValueList(OldArguments, Location) && Ok;
			for (std::size_t I = ArgumentCount; I > 0; --I)
			{
				Ok = ReleaseValue(Arguments[I - 1], Location) && Ok;
				Arguments[I - 1] = std::int32_t{0};
			}
			return Ok && !bHasError;
		};
		bool Ok = false;
		try
		{
			Ok = Dispatch();
		}
		catch (...)
		{
			(void)Cleanup();
			throw;
		}
		return Cleanup() && Ok;
	}

	bool VeroScriptVm::ResolveScriptObjectMethod(
	    const FObjectHandle& ThisObject,
	    const std::uint16_t MethodNameIndex,
	    const std::uint8_t ArgumentCount,
	    const std::uint32_t CallSiteOffset,
	    FCallFrame& Frame,
	    std::uint16_t& OutFunctionIndex,
	    const FSourceLocation& SourceLocation)
	{
		OutFunctionIndex = static_cast<std::uint16_t>(-1);
		if (!ThisObject || ThisObject->bDestroyed || !ActiveModule || MethodNameIndex >= ActiveModule->StringPool.size())
		{
			ReportError("invalid script object method", SourceLocation);
			return false;
		}

		// The runtime type tag is written when the object is created or first
		// resolved. Keep the generation/module guards here so a hot L1 method-cache
		// hit does not first pay an out-of-line type lookup on every virtual call.
		// Hot reload and cross-module objects still take the authoritative resolver.
		const FScriptObject& Object = *ThisObject;
		std::uint16_t ObjectTypeIndex =
		    Object.RuntimeTypeModule == ActiveModule && Object.RuntimeTypeGeneration == ProgramGeneration && Object.RuntimeTypeIndex < ActiveModule->Types.size()
		    ? Object.RuntimeTypeIndex
		    : ResolveObjectTypeIndex(Object);
		if (Object.bConstructionIncomplete || Object.bDestroying)
		{
			ObjectTypeIndex = ResolveObjectDispatchTypeIndex(Object, ObjectTypeIndex);
		}
		constexpr std::size_t WaysPerSet = 4;
		constexpr std::size_t SetCount = 4;
		static_assert(WaysPerSet * SetCount == decltype(Frame.MethodIc)::Capacity);
		const std::uint32_t SiteHash = CallSiteOffset != 0xFFFFFFFFu
		    ? CallSiteOffset
		    : (static_cast<std::uint32_t>(MethodNameIndex) * 2654435761u ^ static_cast<std::uint32_t>(ArgumentCount) * 2246822519u);
		const std::size_t SetIndex =
		    (static_cast<std::size_t>(SiteHash) * 2654435761u) & (SetCount - 1u);
		const std::size_t SetBase = SetIndex * WaysPerSet;
		if (ObjectTypeIndex != static_cast<std::uint16_t>(-1))
		{
			for (std::size_t Way = 0; Way < WaysPerSet; ++Way)
			{
				const FMethodInlineCacheEntry& Entry = Frame.MethodIc[SetBase + Way];
				if (Entry.CallSiteOffset == CallSiteOffset && Entry.NameIndex == MethodNameIndex && Entry.ArgumentCount == ArgumentCount && Entry.TypeIndex == ObjectTypeIndex && Entry.FunctionIndex < ActiveModule->Functions.size())
				{
					OutFunctionIndex = Entry.FunctionIndex;
					return true;
				}
			}
		}
		const auto StoreFrameCache = [&](const std::uint16_t FunctionIndex)
		{
			const std::size_t Way =
			    Frame.MethodIcNextWay[SetIndex]++ & (WaysPerSet - 1u);
			FMethodInlineCacheEntry& Entry = Frame.MethodIc[SetBase + Way];
			Entry.CallSiteOffset = CallSiteOffset;
			Entry.NameIndex = MethodNameIndex;
			Entry.TypeIndex = ObjectTypeIndex;
			Entry.FunctionIndex = FunctionIndex;
			Entry.ArgumentCount = ArgumentCount;
		};

		FMethodDispatchCacheEntry* DispatchEntry = nullptr;
		if (ObjectTypeIndex != static_cast<std::uint16_t>(-1))
		{
			if (MethodDispatchTypeIcModule == ActiveModule && MethodDispatchTypeIcGeneration == ProgramGeneration && ObjectTypeIndex < MethodDispatchTypeIc.size())
			{
				for (const FMethodTypeDispatchCacheEntry& TypeEntry :
				    MethodDispatchTypeIc[ObjectTypeIndex].Ways)
				{
					if (TypeEntry.NameIndex == MethodNameIndex && TypeEntry.ArgumentCount == ArgumentCount && TypeEntry.FunctionIndex < ActiveModule->Functions.size())
					{
						OutFunctionIndex = TypeEntry.FunctionIndex;
						StoreFrameCache(OutFunctionIndex);
						return true;
					}
				}
			}
			constexpr std::size_t DispatchWaysPerSet = 4;
			constexpr std::size_t DispatchSetCount =
			    std::tuple_size_v<decltype(MethodDispatchIcNextWay)>;
			static_assert(
			    DispatchWaysPerSet * DispatchSetCount == std::tuple_size_v<decltype(MethodDispatchIc)>);
			static_assert((DispatchWaysPerSet & (DispatchWaysPerSet - 1u)) == 0u);
			static_assert((DispatchSetCount & (DispatchSetCount - 1u)) == 0u);
			const std::size_t DispatchHash =
			    (static_cast<std::size_t>(ObjectTypeIndex) * 1315423911u ^ static_cast<std::size_t>(MethodNameIndex) * 2654435761u ^ static_cast<std::size_t>(ArgumentCount) * 2246822519u);
			const std::size_t DispatchSet =
			    DispatchHash & (DispatchSetCount - 1u);
			const std::size_t DispatchBase = DispatchSet * DispatchWaysPerSet;
			for (std::size_t Way = 0; Way < DispatchWaysPerSet; ++Way)
			{
				FMethodDispatchCacheEntry& Candidate =
				    MethodDispatchIc[DispatchBase + Way];
				if (Candidate.Module == ActiveModule && Candidate.ProgramGeneration == ProgramGeneration && Candidate.NameIndex == MethodNameIndex && Candidate.TypeIndex == ObjectTypeIndex && Candidate.ArgumentCount == ArgumentCount && Candidate.FunctionIndex < ActiveModule->Functions.size())
				{
					OutFunctionIndex = Candidate.FunctionIndex;
					StoreFrameCache(OutFunctionIndex);
					return true;
				}
			}
			bModuleLookupArraysDirty = true;
			const std::size_t ReplacementWay =
			    MethodDispatchIcNextWay[DispatchSet]++ & (DispatchWaysPerSet - 1u);
			DispatchEntry = &MethodDispatchIc[DispatchBase + ReplacementWay];
			if (DispatchEntry->Module == ActiveModule && DispatchEntry->ProgramGeneration == ProgramGeneration && DispatchEntry->FunctionIndex < ActiveModule->Functions.size() && (MethodDispatchTypeIcModule != ActiveModule || MethodDispatchTypeIcGeneration != ProgramGeneration || MethodDispatchTypeIc.size() != ActiveModule->Types.size()))
			{
				MethodDispatchTypeIc.assign(
				    ActiveModule->Types.size(), FMethodTypeDispatchCacheSet{});
				MethodDispatchTypeIcModule = ActiveModule;
				MethodDispatchTypeIcGeneration = ProgramGeneration;
				for (const FMethodDispatchCacheEntry& Cached : MethodDispatchIc)
				{
					if (Cached.Module == ActiveModule && Cached.ProgramGeneration == ProgramGeneration && Cached.TypeIndex < MethodDispatchTypeIc.size() && Cached.FunctionIndex < ActiveModule->Functions.size())
					{
						FMethodTypeDispatchCacheSet& TypeSet =
						    MethodDispatchTypeIc[Cached.TypeIndex];
						FMethodTypeDispatchCacheEntry& TypeEntry =
						    TypeSet.Ways[TypeSet.NextWay++ & (TypeSet.Ways.size() - 1u)];
						TypeEntry.NameIndex = Cached.NameIndex;
						TypeEntry.FunctionIndex = Cached.FunctionIndex;
						TypeEntry.ArgumentCount = Cached.ArgumentCount;
					}
				}
			}
		}

		const std::string& MethodName = ActiveModule->StringPool[MethodNameIndex];
		const auto TryResolveOnOwner = [&](const std::string& OwnerName)
		{
			if (OwnerName.empty())
			{
				return false;
			}
			const std::string MethodKey = ResolveCallMethodKey(
			    OwnerName, MethodName, ArgumentCount);
			const auto FunctionIt = ActiveModule->FunctionIndices.find(MethodKey);
			if (FunctionIt == ActiveModule->FunctionIndices.end() || FunctionIt->second >= ActiveModule->Functions.size() || ActiveModule->Functions[FunctionIt->second].ParamCount != ArgumentCount)
			{
				if (ArgumentCount == 0 && MethodName.rfind("operator conv ", 0) == 0)
				{
					// Conversion aliases share a virtual slot by their canonical
					// return type. This cold fallback retains original function names;
					// the existing method caches store the resolved index thereafter.
					OutFunctionIndex = VmDetail::FindVirtualConversionMethod(*ActiveModule, OwnerName, MethodName);
					return OutFunctionIndex != static_cast<std::uint16_t>(-1);
				}
				return false;
			}
			OutFunctionIndex = FunctionIt->second;
			return true;
		};

		// Nearly every virtual override lives directly on the runtime class. Resolve
		// that owner before allocating hierarchy work queues; this is particularly
		// important once a genuinely megamorphic site outgrows the fixed L2 cache.
		const std::string& DispatchTypeName = ObjectTypeIndex < ActiveModule->Types.size() && (Object.bConstructionIncomplete || Object.bDestroying)
		    ? ActiveModule->Types[ObjectTypeIndex].Name
		    : ThisObject->GetTypeName();
		(void)TryResolveOnOwner(DispatchTypeName);
		std::vector<std::uint16_t> TypeQueue;
		if (OutFunctionIndex == static_cast<std::uint16_t>(-1) && ObjectTypeIndex != static_cast<std::uint16_t>(-1) && ObjectTypeIndex < ActiveModule->Types.size())
		{
			TypeQueue = ActiveModule->Types[ObjectTypeIndex].BaseTypeIndices;
		}
		std::unordered_set<std::uint16_t> VisitedTypes;
		for (std::size_t QueueIndex = 0;
		    OutFunctionIndex == static_cast<std::uint16_t>(-1) && QueueIndex < TypeQueue.size();
		    ++QueueIndex)
		{
			const std::uint16_t TypeIndex = TypeQueue[QueueIndex];
			if (TypeIndex >= ActiveModule->Types.size() || !VisitedTypes.insert(TypeIndex).second)
			{
				continue;
			}
			const FBytecodeTypeDesc& Type = ActiveModule->Types[TypeIndex];
			if (TryResolveOnOwner(Type.Name))
			{
				break;
			}
			for (const std::uint16_t BaseIndex : Type.BaseTypeIndices)
			{
				if (BaseIndex < ActiveModule->Types.size())
				{
					TypeQueue.push_back(BaseIndex);
				}
			}
		}

		if (OutFunctionIndex == static_cast<std::uint16_t>(-1) && ObjectTypeIndex != static_cast<std::uint16_t>(-1) && ObjectTypeIndex < ActiveModule->Types.size())
		{
			std::unordered_set<std::uint16_t> VisitedInterfaces;
			std::vector<std::uint16_t> InterfaceQueue =
			    ActiveModule->Types[ObjectTypeIndex].ImplementedInterfaceIndices;
			for (std::size_t QueueIndex = 0; QueueIndex < InterfaceQueue.size(); ++QueueIndex)
			{
				const std::uint16_t InterfaceIndex = InterfaceQueue[QueueIndex];
				if (InterfaceIndex >= ActiveModule->Interfaces.size() || !VisitedInterfaces.insert(InterfaceIndex).second)
				{
					continue;
				}
				if (TryResolveOnOwner(ActiveModule->Interfaces[InterfaceIndex].Name))
				{
					break;
				}
				for (const std::uint16_t BaseInterface :
				    ActiveModule->Interfaces[InterfaceIndex].BaseInterfaceIndices)
				{
					if (BaseInterface < ActiveModule->Interfaces.size())
					{
						InterfaceQueue.push_back(BaseInterface);
					}
				}
			}
		}

		if (OutFunctionIndex == static_cast<std::uint16_t>(-1))
		{
			ReportError("unknown method: " + MethodName, SourceLocation);
			return false;
		}
		if (ActiveModule->Functions[OutFunctionIndex].bIsAbstract || ActiveModule->Functions[OutFunctionIndex].bDeleted)
		{
			ReportError("method has no executable implementation: " + MethodName, SourceLocation);
			return false;
		}
		if (ObjectTypeIndex != static_cast<std::uint16_t>(-1))
		{
			StoreFrameCache(OutFunctionIndex);
			if (MethodDispatchTypeIcModule == ActiveModule && MethodDispatchTypeIcGeneration == ProgramGeneration && ObjectTypeIndex < MethodDispatchTypeIc.size())
			{
				FMethodTypeDispatchCacheSet& TypeSet =
				    MethodDispatchTypeIc[ObjectTypeIndex];
				FMethodTypeDispatchCacheEntry* TypeEntry = nullptr;
				for (FMethodTypeDispatchCacheEntry& Candidate : TypeSet.Ways)
				{
					if (Candidate.NameIndex == MethodNameIndex && Candidate.ArgumentCount == ArgumentCount)
					{
						TypeEntry = &Candidate;
						break;
					}
				}
				if (!TypeEntry)
				{
					TypeEntry = &TypeSet.Ways[TypeSet.NextWay++ & (TypeSet.Ways.size() - 1u)];
				}
				TypeEntry->NameIndex = MethodNameIndex;
				TypeEntry->FunctionIndex = OutFunctionIndex;
				TypeEntry->ArgumentCount = ArgumentCount;
			}
			if (DispatchEntry)
			{
				DispatchEntry->Module = ActiveModule;
				DispatchEntry->ProgramGeneration = ProgramGeneration;
				DispatchEntry->NameIndex = MethodNameIndex;
				DispatchEntry->TypeIndex = ObjectTypeIndex;
				DispatchEntry->FunctionIndex = OutFunctionIndex;
				DispatchEntry->ArgumentCount = ArgumentCount;
			}
		}
		return true;
	}

	std::uint16_t VeroScriptVm::ResolveObjectDispatchTypeIndex(
	    const FScriptObject& Object, const std::uint16_t ActualTypeIndex) const
	{
		if ((!Object.bConstructionIncomplete && !Object.bDestroying) || !ActiveModule) return ActualTypeIndex;
		const auto* Stage = ObjectLifetimeScope;
		while (Stage && Stage->Object != &Object)
		{
			Stage = Stage->Previous;
		}
		const auto Boundary = Stage ? Stage->FrameDepth : 0;
		for (std::size_t Index = Frames.size(); Index > Boundary; --Index)
		{
			const auto& Frame = Frames[Index - 1];
			if (Frame.ThisObject.get() != &Object || !Frame.Function || !Frame.Function->bIsMethod || !Frame.Function->bReturnsVoid || Frame.Function->OwnerTypeIndex >= ActiveModule->Types.size())
			{
				continue;
			}
			const auto Owner = Frame.Function->OwnerTypeIndex;
			const auto& Type = ActiveModule->Types[Owner];
			const bool Constructor = MethodLinkageBareLeaf(Frame.Function->Name) == ModuleMeta::TypeSimpleName(Type.Name);
			const bool Destructor = Type.DestructorFunctionIndex < ActiveModule->Functions.size() && Frame.Function == &ActiveModule->Functions[Type.DestructorFunctionIndex];
			if (Constructor || Destructor) return Owner;
		}
		return Stage && Stage->TypeIndex < ActiveModule->Types.size() ? Stage->TypeIndex : ActualTypeIndex;
	}

	bool VeroScriptVm::ExecuteCallMethod(
	    const std::uint16_t MethodNameIndex,
	    const FValue& ThisValue,
	    const FValue* const Arguments,
	    const std::uint8_t ArgumentCount,
	    const std::uint32_t CallSiteOffset,
	    const FSourceLocation& SourceLocation,
	    const bool bRunNestedToCompletion)
	{
		// Script objects never use the container-method surface below. Route them
		// directly to their dedicated dispatcher so a polymorphic call does not
		// pay for container-name classification or a mutation guard first.
		if (!ActiveModule || MethodNameIndex >= ActiveModule->StringPool.size())
		{
			ReportError("invalid method name", SourceLocation);
			return false;
		}
		if (std::holds_alternative<FObjectHandle>(ThisValue))
		{
			std::optional<FCallFrame> DummyFrame;
			FCallFrame& Frame = Frames.empty() ? DummyFrame.emplace() : Frames.back();
			#include "Calls/VeroScriptVmCallObject.inl"
		}

		#include "Calls/VeroScriptVmCallCommon.inl"
		#include "Calls/VeroScriptVmCallDynamicArray.inl"
		#include "Calls/VeroScriptVmCallMap.inl"
		#include "Calls/VeroScriptVmCallOrderedMap.inl"
		#include "Calls/VeroScriptVmCallSet.inl"
		#include "Calls/VeroScriptVmCallLinearContainers.inl"
		ReportError("unsupported method call", SourceLocation);
		return false;
	}

	bool VeroScriptVm::InvokeResolvedInstanceMethod(
	    const std::uint16_t FunctionIndex,
	    const FObjectHandle& ThisObject,
	    const FValue* const Arguments,
	    const std::uint8_t ArgumentCount,
	    const FSourceLocation& SourceLocation,
	    const bool bRunNestedToCompletion,
	    const bool bAllowJit)
	{
		if (!ActiveModule || FunctionIndex >= ActiveModule->Functions.size())
		{
			ReportError("invalid resolved instance method", SourceLocation);
			return false;
		}
		if (!ThisObject)
		{
			ReportError("instance method call requires object", SourceLocation);
			return false;
		}
		const FBytecodeFunction& Function = ActiveModule->Functions[FunctionIndex];
		if (!Function.bIsMethod || Function.ParamCount != ArgumentCount)
		{
			ReportError("invalid resolved instance method signature", SourceLocation);
			return false;
		}

		// Core negotiates observation and governance capabilities before native
		// entry. Unsupported bodies retain the interpreter path.
		const auto InvokeBody = [&]() -> bool
		{
			const std::size_t DepthBefore = Frames.size();
			const std::size_t StackBefore = OperandStack.size();
			if (!CallFunctionAt(
			        FunctionIndex,
			        Arguments,
			        ArgumentCount,
			        ThisObject,
			        bAllowJit))
			{
				return false;
			}
			return !bRunNestedToCompletion || JitFinishNestedCall(DepthBefore, StackBefore);
		};

		// Fast path: one predictable empty-std::function branch and no allocation.
		if (!InstanceCallInterceptor)
		{
			return InvokeBody();
		}
		// Constructors/destructors are lifecycle operations, not interceptable
		// instance-method calls. Constructor bytecode uses CallBoundMethod, so
		// distinguish it from ordinary statically-bound methods after resolution.
		if (Function.Name.find("::~") != std::string::npos)
		{
			return InvokeBody();
		}
		const std::string_view QualifiedName = Function.Name;
		std::string_view DeclaringTypeName;
		std::size_t OwnerSeparator = std::string_view::npos;
		if (Function.OwnerTypeIndex < ActiveModule->Types.size())
		{
			DeclaringTypeName = ActiveModule->Types[Function.OwnerTypeIndex].Name;
			if (QualifiedName.size() > DeclaringTypeName.size() + 1 && QualifiedName.compare(0, DeclaringTypeName.size(), DeclaringTypeName) == 0 && QualifiedName.substr(DeclaringTypeName.size(), 2) == "::")
			{
				OwnerSeparator = DeclaringTypeName.size();
			}
		}
		if (OwnerSeparator == std::string_view::npos)
		{
			// Interface/default metadata does not always have a Types[] owner index.
			OwnerSeparator = FindMethodLinkageSeparator(QualifiedName);
			DeclaringTypeName = OwnerSeparator == std::string_view::npos
			    ? std::string_view{}
			    : QualifiedName.substr(0, OwnerSeparator);
		}
		const std::size_t MethodStart = OwnerSeparator == std::string_view::npos
		    ? 0
		    : OwnerSeparator + 2;
		const std::size_t SignaturePos = FindFunctionSignatureMarker(QualifiedName);
		const std::size_t NameEnd =
		    SignaturePos == std::string_view::npos ? QualifiedName.size() : SignaturePos;
		const std::string_view MethodName =
		    QualifiedName.substr(MethodStart, NameEnd - MethodStart);
		std::string_view DeclaringTypeLeaf = DeclaringTypeName;
		DeclaringTypeLeaf.remove_prefix(
		    FindLinkageIdentityStart(DeclaringTypeLeaf));
		if (const std::size_t GenericSuffix = DeclaringTypeLeaf.find('#');
		    GenericSuffix != std::string_view::npos)
		{
			DeclaringTypeLeaf = DeclaringTypeLeaf.substr(0, GenericSuffix);
		}
		if (const std::size_t NamespaceSeparator = DeclaringTypeLeaf.rfind("::");
		    NamespaceSeparator != std::string_view::npos)
		{
			DeclaringTypeLeaf.remove_prefix(NamespaceSeparator + 2);
		}
		if (!DeclaringTypeLeaf.empty() && MethodName == DeclaringTypeLeaf)
		{
			return InvokeBody();
		}
		const auto CallInstructionStart = ActiveInstructionStart;
		const auto CallSiteLocation = RefineErrorLocation(SourceLocation);

		const auto HashText = [](const std::string_view Text) -> std::uint64_t
		{
			return Runtime::Fnv1a64(14695981039346656037ull, Text);
		};

		FResolvedInstanceMethod Target;
		Target.ProgramGeneration = ProgramGeneration;
		Target.ModuleName = Function.Location.FileLabel;
		Target.DeclaringTypeName = DeclaringTypeName;
		Target.MethodName = MethodName;
		Target.QualifiedName = QualifiedName;
		Target.ModuleIdentity = HashText(Target.ModuleName);
		Target.DeclaringTypeIdentity = HashText(Target.DeclaringTypeName);
		Target.MethodIdentity = HashText(Target.QualifiedName);
		Target.FunctionIndex = FunctionIndex;
		Target.bVirtual = Function.bIsVirtual;
		Target.bConst = Function.bIsConstMethod;
		Target.bReturnsVoid = Function.bReturnsVoid;

		bool bProceedCalled = false;
		bool bProceedSucceeded = false;
		std::weak_ptr<const void> ProceedReturnOwner;
		const void* ProceedReturnAddress = nullptr;
		std::shared_ptr<const void> ProceedArrayOwner;
		const auto SharedReturnOwner = [](const FValue& Value) -> std::shared_ptr<const void>
		{
			if (const auto* Object = std::get_if<FObjectHandle>(&Value)) return *Object;
			if (const auto* Object = std::get_if<FHostObjectHandle>(&Value)) return *Object;
			return {};
		};
		std::array<FValue, 2> PendingReturns;
		FValue& ProceedReturn = PendingReturns[0];
		FValue& InterceptReturn = PendingReturns[1];
		FScriptError InterceptError;
		FInstanceCallInterceptContext Context;
		Context.Receiver = ThisObject;
		Context.Target = Target;
		Context.Arguments = FValueView(Arguments, ArgumentCount);
		Context.Proceed =
		    [&](FValue& OutReturn, FScriptError& OutError) -> bool
		{
			bProceedSucceeded = false;
			if (bProceedCalled)
			{
				FillFromEnglishMessage(
				    OutError,
				    "instance call interceptor Proceed may be called only once",
				    CallSiteLocation);
				return false;
			}
			bProceedCalled = true;
			const std::size_t DepthBefore = Frames.size();
			const std::size_t StackBefore = OperandStack.size();
			if (!CallFunctionAt(
			        FunctionIndex,
			        Arguments,
			        ArgumentCount,
			        ThisObject,
			        bAllowJit) ||
			    !JitFinishNestedCall(DepthBefore, StackBefore))
			{
				OutError = Error;
				return false;
			}
			if (bExitRequested)
			{
				FillFromEnglishMessage(
				    OutError,
				    "instance method called exit while interceptor Proceed was active",
				    CallSiteLocation);
				return false;
			}
			if (OperandStack.empty())
			{
				FillFromEnglishMessage(
				    OutError,
				    "instance method produced no return value",
				    CallSiteLocation);
				return false;
			}
			ProceedReturn = Pop();
			// The callback may discard its copy with an ordinary C++ assignment.
			// Keep language-owned values alive until VM cleanup can release them.
			if (ValueRequiresOwner(ProceedReturn))
			{
				OutReturn = ProceedReturn;
			}
			else
			{
				OutReturn = std::exchange(ProceedReturn, std::int32_t{0});
			}
			if (auto* Array = std::get_if<FNativeArray>(&OutReturn))
			{
				ProceedArrayOwner = std::make_shared<const char>();
				Array->PreparedReturnOwner = ProceedArrayOwner;
			}
			else
			{
				const auto Owner = SharedReturnOwner(OutReturn);
				ProceedReturnAddress = Owner.get();
				ProceedReturnOwner = Owner;
			}
			bProceedSucceeded = !bHasError;
			return bProceedSucceeded;
		};

		EInstanceCallInterceptResult InterceptResult =
		    EInstanceCallInterceptResult::Failed;
		try
		{
			InterceptResult =
			    InstanceCallInterceptor(Context, InterceptReturn, InterceptError);
		}
		catch (const std::exception& Exception)
		{
			FillFromEnglishMessage(
			    InterceptError,
			    std::string("instance call interceptor threw: ") + Exception.what(),
			    CallSiteLocation);
		}
		catch (...)
		{
			FillFromEnglishMessage(
			    InterceptError,
			    "instance call interceptor threw an unknown exception",
			    CallSiteLocation);
		}

		ActiveInstructionStart = CallInstructionStart;
		const auto ReleasePendingReturns = [&]()
		{
			const bool bReleased = ReleaseValueList(PendingReturns.data(), PendingReturns.size(), CallSiteLocation);
			ActiveInstructionStart = CallInstructionStart;
			return bReleased;
		};

		if (bProceedCalled && !bProceedSucceeded)
		{
			// Proceed is the original call. Its failure must never be converted
			// into a successful handled result by an interceptor callback.
			if (InterceptError.Message.empty() && bHasError)
			{
				InterceptError = Error;
			}
			InterceptResult = EInstanceCallInterceptResult::Failed;
		}
		if (InterceptResult == EInstanceCallInterceptResult::Unhandled)
		{
			if (bProceedCalled)
			{
				FillFromEnglishMessage(
				    InterceptError,
				    "instance call interceptor returned Unhandled after calling Proceed",
				    CallSiteLocation);
				InterceptResult = EInstanceCallInterceptResult::Failed;
			}
			else
			{
				if (!ReleasePendingReturns() || bHasError || bHasPendingScriptException) return false;
				return bExitRequested || InvokeBody();
			}
		}
		if (InterceptResult == EInstanceCallInterceptResult::Failed)
		{
			if (InterceptError.Message.empty())
			{
				FillFromEnglishMessage(
				    InterceptError,
				    "instance call interceptor failed",
				    CallSiteLocation);
			}
			else if (InterceptError.Location.FileLabel.empty())
			{
				InterceptError.Location = CallSiteLocation;
			}
			Error = std::move(InterceptError);
			bHasError = true;
			(void)ReleasePendingReturns();
			return false;
		}

		FValue Result;
		const auto PrepareResult = [&]() -> bool
		{
			if (Function.bReturnsVoid) return true;
			if (Function.ClosureTypeName.empty())
			{
				Result = std::exchange(InterceptReturn, std::int32_t{0});
				return true;
			}
			std::vector<std::string> ParameterTypes;
			std::string ReturnType;
			const bool bHasReturnType = Runtime::ParseFunctionType(
			    Function.ClosureTypeName,
			    ParameterTypes,
			    ReturnType);
			FValue* const ReturnStorage = ResolveRefStorage(InterceptReturn);
			const auto* Array = ReturnStorage ? std::get_if<FNativeArray>(ReturnStorage) : nullptr;
			if (!ReturnStorage || (Array && !Array->Data) ||
				(Function.bReturnsRef && ReturnStorage == &InterceptReturn) ||
				(bHasReturnType && ReturnType != "void" && !ValueMatchesElementType(*ReturnStorage,
					std::string(Runtime::StripReferenceTypeName(ReturnType)), CallSiteLocation)))
			{
				ReportError(
				    "instance call interceptor return type mismatch for " + std::string(QualifiedName),
				    CallSiteLocation);
				return false;
			}
			if (Function.bReturnsRef)
			{
				return PromoteReferenceCapture(std::move(InterceptReturn), Result, CallSiteLocation);
			}
			if (bHasReturnType)
			{
				const auto Owner = SharedReturnOwner(*ReturnStorage);
				// A reference returned by the host borrows storage even when it
				// points at the original Proceed value. Only direct forwarding can
				// reuse the method's completed value copy.
				const bool bPreparedValue = bProceedSucceeded && ReturnStorage == &InterceptReturn &&
					((Owner && Owner.get() == ProceedReturnAddress && !Owner.owner_before(ProceedReturnOwner) && !ProceedReturnOwner.owner_before(Owner)) ||
					 (Array && ProceedArrayOwner && !ProceedArrayOwner.owner_before(Array->PreparedReturnOwner) && !Array->PreparedReturnOwner.owner_before(ProceedArrayOwner)));
				std::uint16_t ReturnTypeIndex = static_cast<std::uint16_t>(-1);
				const bool bValueReturn = Runtime::IsNativeArrayTypeName(ReturnType) || (ModuleMeta::FindTypeIndex(*ActiveModule, ReturnType, ReturnTypeIndex) && ActiveModule->Types[ReturnTypeIndex].Kind == EScriptObjectKind::Struct) || FindActiveNativeIndexByName(MakeHostCloneNativeName(ReturnType)) != static_cast<std::uint16_t>(-1);
				if (bValueReturn && !bPreparedValue)
				{
					if (const auto* Host = std::get_if<FHostObjectHandle>(ReturnStorage))
					{
						Result = *Host;
						return JitCloneStruct(Result);
					}
					return CopyLanguageValue(InterceptReturn, Result, CallSiteLocation, false, ReturnType);
				}
			}
			// Keep the source cell alive until the result is prepared.
			Result = bHasReturnType && ReturnStorage != &InterceptReturn
				? *ReturnStorage : std::exchange(InterceptReturn, std::int32_t{0});
			if (auto* ReturnedArray = std::get_if<FNativeArray>(&Result))
			{
				ReturnedArray->PreparedReturnOwner.reset();
			}
			return true;
		};
		bool bPrepared = false;
		try
		{
			bPrepared = PrepareResult();
		}
		catch (...)
		{
			(void)ReleasePendingReturns();
			(void)ReleaseValue(Result, CallSiteLocation);
			throw;
		}
		if (!ReleasePendingReturns() || !bPrepared || bHasError || bHasPendingScriptException || bExitRequested)
		{
			(void)ReleaseValue(Result, CallSiteLocation);
			return bExitRequested && !bHasError;
		}
		Push(std::move(Result));
		return !bHasError;
	}

	bool VeroScriptVm::JitFinishNestedCall(
	    const std::size_t DepthBefore,
	    const std::size_t StackBefore)
	{
		bool bOk = true;
		if (Frames.size() > DepthBefore)
		{
			const std::size_t SavedStopDepth = StopFrameDepth;
			StopFrameDepth = DepthBefore;
			bOk = Run();
			StopFrameDepth = SavedStopDepth;
			if (!bOk)
			{
				// A nested frame owns diagnostics produced while it executes. Preserve
				// that location when a Linear/native caller subsequently processes the
				// failed helper result at its outer call opcode.
				if (bHasError)
				{
					bJitErrorLocationPinned = true;
				}
				(void)DiscardExecutionAbove(DepthBefore, StackBefore, {});
			}
		}
		if (!bOk || bHasError)
		{
			return false;
		}
		if (bExitRequested)
		{
			return true;
		}
		if (OperandStack.size() != StackBefore + 1)
		{
			ReportError("JIT nested call produced unexpected stack depth");
			return false;
		}
		return true;
	}
}
