// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVm.h"
#include "VeroScriptVmJitDiagnostics.h"
#include "VeroScriptVmCleanup.h"
#include "VeroScriptVariantInternal.h"
#include "VeroScriptVmValueRelease.h"

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

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_set>

namespace VeroScript
{
	namespace
	{
		constexpr std::size_t MaxPreparedObjectFields = 16;
		constexpr std::uint8_t MaxPreparedConstructorCalls = 16;

		bool IsPreparedObjectConstant(const FBytecodeConstant& Constant)
		{
			return Constant.Kind != EConstantKind::Str && Constant.Kind != EConstantKind::WStr
			    && Constant.Kind != EConstantKind::Enum;
		}

		bool SamePreparedScalar(const FValue& Value, const FValue& Expected)
		{
			if (Value.index() != Expected.index()) return false;
			return std::visit([&](const auto& Scalar)
			{
				using TScalar = std::decay_t<decltype(Scalar)>;
				if constexpr (std::is_floating_point_v<TScalar>)
				{
					return std::memcmp(&Scalar, &std::get<TScalar>(Expected), sizeof(TScalar)) == 0;
				}
				else if constexpr (std::is_integral_v<TScalar>)
				{
					return Scalar == std::get<TScalar>(Expected);
				}
				else
				{
					return false;
				}
			}, Value);
		}

		struct FDefaultConstructorProof
		{
			bool Verify(const std::uint16_t FunctionIndex, const std::uint8_t Depth)
			{
				if (!RemainingCalls || FunctionIndex >= Module.Functions.size()) return false;
				--RemainingCalls;
				PeakDepth = (std::max)(PeakDepth, Depth);
				const auto& Function = Module.Functions[FunctionIndex];
				if (!Function.bDefaulted || !Function.bIsMethod || !Function.bReturnsVoid
				    || Function.bIsStaticMethod || Function.bIsAbstract || Function.bDeleted
				    || Function.bReturnsRef || Function.ParamCount != 0 || Function.LocalCount != 0
				    || !Function.Captures.empty() || !Function.ExceptionRegions.empty()
				    || !Function.UnavailableReason.empty() || Function.OwnerTypeIndex >= Module.Types.size()
				    || Function.Code.size() > 512)
				{
					return false;
				}
				const auto& Type = Module.Types[Function.OwnerTypeIndex];
				if (!Type.HostBaseTypeName.empty() || MethodLinkageBareLeaf(Function.Name) != ModuleMeta::TypeSimpleName(Type.Name))
				{
					return false;
				}
				const auto& Code = Function.Code;
				for (std::size_t Ip = 0; Ip < Code.size();)
				{
					const auto Op = static_cast<EOpcode>(Code[Ip]);
					if (Op == EOpcode::LoadThis)
					{
						// The bound base call consumes its receiver before pushing the
						// void sentinel, so the whole chain needs one operand slot.
						constexpr auto BaseCallSize = 1u + Bytecode::InstructionSize(EOpcode::CallBoundMethod) + 1u;
						if (Code.size() - Ip < BaseCallSize || static_cast<EOpcode>(Code[Ip + 1]) != EOpcode::CallBoundMethod
						    || Code[Ip + 4] != 0 || static_cast<EOpcode>(Code[Ip + 5]) != EOpcode::Pop)
						{
							return false;
						}
						const auto Target = Bytecode::ReadU16LE(Code.data() + Ip + 2);
						if (Target >= Module.Functions.size()) return false;
						const auto Base = Module.Functions[Target].OwnerTypeIndex;
						if (std::find(Type.BaseTypeIndices.begin(), Type.BaseTypeIndices.end(), Base) == Type.BaseTypeIndices.end()
						    || !Verify(Target, static_cast<std::uint8_t>(Depth + 1u)))
						{
							return false;
						}
						Ip += BaseCallSize;
						continue;
					}
					if (Op != EOpcode::ConstI32 && Op != EOpcode::ConstF64 && Op != EOpcode::ConstBool) return false;
					const auto ConstantSize = Bytecode::InstructionSize(Op);
					if (Code.size() - Ip <= ConstantSize) return false;
					FValue Value;
					if (Op == EOpcode::ConstBool)
					{
						Value = Code[Ip + 1] != 0;
					}
					else
					{
						const auto Constant = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						if (Constant >= Module.Constants.size() || !IsPreparedObjectConstant(Module.Constants[Constant])) return false;
						Value = Runtime::ConstantToValue(Module, Module.Constants[Constant]);
					}
					Ip += ConstantSize;
					const auto Next = static_cast<EOpcode>(Code[Ip]);
					if (Next == EOpcode::Return)
					{
						const auto* Sentinel = std::get_if<std::int32_t>(&Value);
						return Sentinel && *Sentinel == 0 && Ip + 1u == Code.size();
					}
					constexpr auto FieldSize = Bytecode::InstructionSize(EOpcode::InitializeThisField);
					if (Next != EOpcode::InitializeThisField || Code.size() - Ip < FieldSize) return false;
					const auto Name = Bytecode::ReadU16LE(Code.data() + Ip + 1);
					if (Name >= Module.StringPool.size()) return false;
					const auto Field = std::find_if(Layout.begin(), Layout.end(), [&](const FBytecodeFieldDesc& Desc)
					{
						return Desc.Name == Module.StringPool[Name];
					});
					if (Field == Layout.end()) return false;
					const auto Index = static_cast<std::size_t>(Field - Layout.begin());
					if (!SamePreparedScalar(Value, Defaults[Index])) return false;
					Written |= std::uint32_t{1} << Index;
					Ip += FieldSize;
				}
				return false;
			}

			const FBytecodeModule& Module;
			const std::vector<FBytecodeFieldDesc>& Layout;
			const std::vector<FValue>& Defaults;
			std::uint32_t Written = 0;
			std::uint8_t PeakDepth = 0;
			std::uint8_t RemainingCalls = MaxPreparedConstructorCalls;
		};

		bool HasTrivialObjectCleanup(const FBytecodeModule& Module, const std::uint16_t TypeIndex)
		{
			const auto& Layout = Module.Types[TypeIndex].Fields;
			if (Layout.size() > MaxPreparedObjectFields) return false;
			std::array<std::uint16_t, 16> Types{TypeIndex};
			std::size_t Count = 1;
			std::uint32_t Released = 0;
			for (std::size_t Next = 0; Next < Count; ++Next)
			{
				const auto& Type = Module.Types[Types[Next]];
				if (Type.DestructorFunctionIndex != 0xFFFF || !Type.HostBaseTypeName.empty()
				    || Type.OwnFieldStartIndex > Type.Fields.size() || Type.Fields.size() > Layout.size())
				{
					return false;
				}
				for (std::size_t Index = Type.OwnFieldStartIndex; Index < Type.Fields.size(); ++Index)
				{
					if (Next == 0)
					{
						Released |= std::uint32_t{1} << Index;
						continue;
					}
					const auto It = std::find_if(Layout.begin(), Layout.end(), [&](const FBytecodeFieldDesc& Field)
					{
						return Field.Name == Type.Fields[Index].Name;
					});
					if (It == Layout.end()) return false;
					Released |= std::uint32_t{1} << (It - Layout.begin());
				}
				for (const auto Base : Type.BaseTypeIndices)
				{
					// Unusual or oversized hierarchies use the ordinary walk.
					if (Base >= Module.Types.size() || Count == Types.size()
					    || std::find(Types.begin(), Types.begin() + Count, Base) != Types.begin() + Count)
					{
						return false;
					}
					Types[Count++] = Base;
				}
			}
			return Released == (std::uint32_t{1} << Layout.size()) - 1u;
		}

		bool IsTrivialObjectFieldValue(const FValue& Value)
		{
			return VmDetail::IsTrivialLifetimeValue(Value);
		}

		bool HasUninitializedObjectField(const FBytecodeFieldDesc& Field)
		{
			return std::any_of(Field.DeclMeta.begin(), Field.DeclMeta.end(), [](const auto& Entry)
			{
				return Entry.first == UninitializedPayloadMetaKey;
			});
		}

		template <typename TRelease>
		bool ReleaseWithPendingException(FValue& Value, bool& bPending, FValue& Pending,
		    FSourceLocation& PendingLocation, TRelease&& Release)
		{
			FValue Exception = std::move(Pending);
			FSourceLocation ExceptionLocation = PendingLocation;
			Pending = std::int32_t{0};
			bPending = false;
			PendingLocation = {};
			bool bOk = Release(Value);
			// A caught throw in a destructor must not consume the transfer being
			// unwound. An uncaught cleanup throw supersedes it, including any
			// subsequent exception raised while releasing the superseded value.
			while (bPending)
			{
				FValue Superseded = std::move(Exception);
				Exception = std::move(Pending);
				ExceptionLocation = PendingLocation;
				Pending = std::int32_t{0};
				bPending = false;
				PendingLocation = {};
				bOk = Release(Superseded) && bOk;
			}
			Pending = std::move(Exception);
			bPending = true;
			PendingLocation = ExceptionLocation;
			return bOk;
		}

		template <typename TSequence, typename TRelease>
		void DrainDetachedSequence(TSequence& Values, TRelease&& Release)
		{
			while (!Values.empty())
			{
				TSequence Pending;
				Pending.swap(Values);
				while (!Pending.empty())
				{
					FValue Value = std::move(Pending.back());
					Pending.pop_back();
					Release(Value);
				}
			}
		}

		template <typename TEntries, typename TRelease>
		void DrainDetachedEntries(TEntries& Entries, TRelease&& Release)
		{
			while (!Entries.empty())
			{
				TEntries Pending;
				Pending.swap(Entries);
				for (auto& Entry : Pending)
				{
					FValue Value = std::move(Entry.second);
					Entry.second = std::int32_t{0};
					Release(Value);
				}
			}
		}
	}

	bool VeroScriptVm::InitializeObjectHostBase(const FObjectHandle& Object, const FSourceLocation& Location)
	{
		if (!Object || !ActiveModule) return false;
		const std::uint16_t TypeIndex = ResolveObjectTypeIndex(*Object);
		if (TypeIndex >= ActiveModule->Types.size()) return false;
		const auto& TypeDesc = ActiveModule->Types[TypeIndex];
		if (TypeDesc.HostBaseTypeName.empty() || Object->GetHostBase()) return true;
		if (!ActiveNatives)
		{
			ReportError("host subclass factory unavailable for type: " + TypeDesc.Name, Location);
			return false;
		}
		const auto FactoryIndex = FindActiveNativeIndexByName(MakeHostSubclassNativeName(TypeDesc.HostBaseTypeName));
		if (FactoryIndex == 0xFFFF)
		{
			ReportError("missing host subclass factory for type: " + TypeDesc.HostBaseTypeName, Location);
			return false;
		}
		const auto DepthBefore = Frames.size();
		const auto StackBefore = OperandStack.size();
		Push(TypeDesc.Name);
		if (!CallNative(FactoryIndex, 1, Location))
		{
			(void)DiscardExecutionAbove(DepthBefore, StackBefore, Location);
			return false;
		}
		if (OperandStack.size() != StackBefore + 1)
		{
			ReportError("host subclass factory produced unexpected stack depth", Location);
			(void)DiscardExecutionAbove(DepthBefore, StackBefore, Location);
			return false;
		}
		FValue HostValue = Pop();
		const auto* Host = std::get_if<FHostObjectHandle>(&HostValue);
		if (!Host || !*Host || (*Host)->TypeName != TypeDesc.HostBaseTypeName)
		{
			ReportError("host subclass factory must return a host object of type " + TypeDesc.HostBaseTypeName, Location);
			(void)ReleaseValue(HostValue, Location);
			return false;
		}
		Object->SetHostBase(*Host);
		StampHostValueCallbacks(Object->GetHostBase());
		if (TypeDesc.Kind == EScriptObjectKind::Struct && !Object->GetHostBase()->CloneCallback)
		{
			ReportError("host struct subclass requires a registered @clone: " + TypeDesc.HostBaseTypeName, Location);
			return false;
		}
		Object->GetHostBase()->ScriptOwner = Object;
		return true;
	}

	void VeroScriptVm::CompleteObjectConstruction(const FObjectHandle& Object, const std::uint16_t ConstructedType)
	{
		if (!Object || !ActiveModule || ConstructedType >= ActiveModule->Types.size() || bHasError || bHasPendingScriptException || bExitRequested)
		{
			return;
		}
		if (ConstructedType == ResolveObjectTypeIndex(*Object))
		{
			Object->bConstructionIncomplete = false;
			Object->ClearCompletedCopyBases();
			return;
		}
		auto& Completed = Object->EditCompletedCopyBases();
		Completed.erase(std::remove_if(Completed.begin(), Completed.end(), [&](const auto Base)
		{
			return ModuleMeta::IsObjectTypeAssignableTo(*ActiveModule,
			    ActiveModule->Types[ConstructedType].Name, ActiveModule->Types[Base].Name);
		}),
		    Completed.end());
		Completed.push_back(ConstructedType);
	}

	bool VeroScriptVm::DestroyAbortedConstruction(const FCallFrame& Frame, const FSourceLocation& Location)
	{
		if (!Frame.ThisObject || !Frame.ThisObject->bConstructionIncomplete || !Frame.Function || !ActiveModule || !Frame.Function->bReturnsVoid || !Frame.Function->bIsMethod || Frame.Function->OwnerTypeIndex >= ActiveModule->Types.size() || MethodLinkageBareLeaf(Frame.Function->Name) != ModuleMeta::TypeSimpleName(ActiveModule->Types[Frame.Function->OwnerTypeIndex].Name))
		{
			return true;
		}
		// A caller may retain the construction temporary after this frame is gone.
		// Failed construction must still release completed subobjects immediately.
		return DestroyObject(Frame.ThisObject, Location);
	}

	FExecutionContext::FObjectTypePlan& VeroScriptVm::GetObjectTypePlan(const std::uint16_t TypeIndex)
	{
		if (ObjectTypePlanModule != ActiveModule || ObjectTypePlanGeneration != ProgramGeneration)
		{
			ObjectTypePlans.clear();
			ObjectTypePlanModule = ActiveModule;
			ObjectTypePlanGeneration = ProgramGeneration;
		}
		if (const auto Found = ObjectTypePlans.find(TypeIndex); Found != ObjectTypePlans.end()) return Found->second;
		if (ObjectTypePlans.size() >= 1024)
		{
			ObjectTypePlans.clear();
		}
		const auto& Type = ActiveModule->Types[TypeIndex];
		auto& Plan = ObjectTypePlans.try_emplace(TypeIndex).first->second;
		Plan.bHasTrivialCleanup = HasTrivialObjectCleanup(*ActiveModule, TypeIndex);
		if (!Type.HostBaseTypeName.empty() || Type.Fields.size() > MaxPreparedObjectFields) return Plan;
		std::array<FValue, MaxPreparedObjectFields> Defaults;
		std::size_t Index = 0;
		for (const auto& Field : Type.Fields)
		{
			FValue& Default = Defaults[Index++];
			if (HasUninitializedObjectField(Field))
			{
				Default = std::int32_t{0};
			}
			else if (Field.bHasDefault)
			{
				// Keep cached data small and independent of all language roots.
				if (!IsPreparedObjectConstant(Field.DefaultValue))
				{
					return Plan;
				}
				Default = Runtime::ConstantToValue(*ActiveModule, Field.DefaultValue);
			}
			else if (Field.bHasInitCode || (!Field.TypeName.empty() && Field.TypeName != "int"))
			{
				return Plan;
			}
		}
		Plan.ScalarDefaults.assign(Defaults.begin(), Defaults.begin() + Index);
		Plan.bHasScalarDefaults = true;
		return Plan;
	}

	bool VeroScriptVm::TryRunTrivialDefaultConstructor(
	    const std::uint16_t FunctionIndex, const FObjectHandle& Object)
	{
		const auto TypeIndex = ActiveModule->Functions[FunctionIndex].OwnerTypeIndex;
		if (bHasError || bHasPendingScriptException || bExitRequested || HasExecutionGovernance()
		    || Debugger || MutationCallback || InstanceCallInterceptor || bHotReloadModuleInitializing
		    || !VmExecutionDomain->bFrozen || AotProgram || Frames.empty()
		    || OperandStack.size() <= Frames.back().StackBase || OperandStack.size() >= MaxOperandStackDepth
		    || !Object->bConstructionIncomplete || Object->bDestroyed || Object->bDestroying
		    || Object->ProgramOwner != ProgramOwner || Object->RuntimeTypeModule != ActiveModule
		    || Object->RuntimeTypeGeneration != ProgramGeneration || Object->RuntimeTypeIndex != TypeIndex
		    || Object->GetHostBase() || !Object->GetCompletedCopyBases().empty())
		{
			return false;
		}
		// The caller must keep the allocation alive through construction. An
		// arbitrary call on an unretained receiver still uses normal frame cleanup.
		const auto* Retained = std::get_if<FObjectHandle>(&OperandStack.back());
		if (!Retained || *Retained != Object) return false;
		auto& Plan = GetObjectTypePlan(TypeIndex);
		if (!Plan.bHasScalarDefaults || Plan.ScalarDefaults.size() != Object->Fields.size()) return false;
		if (Plan.DefaultConstructorIndex != FunctionIndex)
		{
			const auto& Layout = ActiveModule->Types[TypeIndex].Fields;
			FDefaultConstructorProof Proof{*ActiveModule, Layout, Plan.ScalarDefaults};
			Plan.DefaultConstructorIndex = FunctionIndex;
			Plan.DefaultConstructorDepth = Proof.Verify(FunctionIndex, 1)
			        && Proof.Written == (std::uint32_t{1} << Layout.size()) - 1u
			    ? Proof.PeakDepth
			    : 0;
		}
		const auto LogicalDepth = GetLogicalCallDepth();
		if (Plan.DefaultConstructorDepth == 0 || LogicalDepth >= CallDepthLimit
		    || Plan.DefaultConstructorDepth > CallDepthLimit - LogicalDepth
		    || !std::all_of(Object->Fields.begin(), Object->Fields.end(), [](const FValue& Field)
		       {
			       const auto* Scalar = std::get_if<std::int32_t>(&Field);
			       return Scalar && *Scalar == 0;
		       }))
		{
			return false;
		}
		// The verified writes match the cached defaults exactly. There are no
		// calls outside the proof, so publishing the complete value is atomic.
		Object->Fields = Plan.ScalarDefaults;
		CompleteObjectConstruction(Object, TypeIndex);
		Error = {};
		Push(std::int32_t{0});
		return true;
	}

	FObjectHandle VeroScriptVm::CreateObject(const std::uint16_t TypeIndex, const FSourceLocation& Location,
	    const bool bUninitialized)
	{
		if (!ActiveModule || TypeIndex >= ActiveModule->Types.size())
		{
			ReportError("invalid type index", Location);
			return nullptr;
		}

		const FBytecodeTypeDesc& TypeDesc = ActiveModule->Types[TypeIndex];
		if (TypeDesc.bIsStatic)
		{
			ReportError(
			    "cannot instantiate static " + std::string(TypeDesc.Kind == EScriptObjectKind::Struct ? "struct: " : "class: ") + TypeDesc.Name,
			    Location);
			return nullptr;
		}
		if (TypeDesc.bIsAbstract)
		{
			ReportError("cannot instantiate abstract type: " + TypeDesc.Name, Location);
			return nullptr;
		}
		auto Object = std::make_shared<FScriptObject>();
		Object->Kind = TypeDesc.Kind;
		auto& Plan = GetObjectTypePlan(TypeIndex);
		if (Plan.NameIdentity)
		{
			Object->TypeNameIdentity = Plan.NameIdentity;
		}
		else
		{
			Object->SetTypeName(TypeDesc.Name);
			if (TypeDesc.Name.size() <= 4096)
			{
				Plan.NameIdentity = Object->TypeNameIdentity;
			}
		}
		Object->RuntimeTypeGeneration = ProgramGeneration;
		Object->ProgramOwner = ProgramOwner;
		Object->RuntimeTypeModule = ActiveModule;
		Object->RuntimeTypeIndex = TypeIndex;
		if (bUninitialized)
		{
			Object->Fields.resize(TypeDesc.Fields.size());
			Object->bConstructionIncomplete = true;
			return Object;
		}
		if (Plan.bHasScalarDefaults && !bHasError && !bHasPendingScriptException && !bExitRequested)
		{
			Object->Fields = Plan.ScalarDefaults;
			return Object;
		}
		const auto FailInitialization = [&]() -> FObjectHandle
		{
			// The enclosing object has not finished construction. Release only
			// its completed fields, in reverse order, without running its destructor.
			(void)ReleaseValueList(Object->Fields, Location);
			return nullptr;
		};
		if (!TypeDesc.HostBaseTypeName.empty() && !InitializeObjectHostBase(Object, Location)) return FailInitialization();
		Object->Fields.reserve(TypeDesc.Fields.size());
		for (const FBytecodeFieldDesc& Field : TypeDesc.Fields)
		{
			if (HasUninitializedObjectField(Field))
			{
				Object->Fields.push_back(std::int32_t{0});
			}
			else if (Field.bHasDefault)
			{
				Object->Fields.push_back(Runtime::ConstantToValue(*ActiveModule, Field.DefaultValue));
			}
			else if (Field.bHasInitCode)
			{
				FValue InitValue;
				if (!EvalExpressionCode(Field.InitCode, InitValue, Location, Field.InitLocalCount))
				{
					return FailInitialization();
				}
				Object->Fields.push_back(std::move(InitValue));
			}
			else if (!Field.TypeName.empty() && Runtime::IsDynamicArrayTypeName(Field.TypeName))
			{
				Object->Fields.push_back(CreateDynamicArray(Field.TypeName, Location));
			}
			else if (!Field.TypeName.empty() && Runtime::IsMapTypeName(Field.TypeName))
			{
				Object->Fields.push_back(CreateMap(Field.TypeName, Location));
			}
			else if (!Field.TypeName.empty() && Runtime::IsMultiSetTypeName(Field.TypeName))
			{
				Object->Fields.push_back(CreateMap(Field.TypeName, Location));
			}
			else if (!Field.TypeName.empty() && Runtime::IsSetTypeName(Field.TypeName))
			{
				Object->Fields.push_back(CreateSet(Field.TypeName, Location));
			}
			else if (!Field.TypeName.empty() && Runtime::IsQueueTypeName(Field.TypeName))
			{
				Object->Fields.push_back(CreateQueue(Field.TypeName, Location));
			}
			else if (!Field.TypeName.empty() && Runtime::IsStackTypeName(Field.TypeName))
			{
				Object->Fields.push_back(CreateStack(Field.TypeName, Location));
			}
			else if (!Field.TypeName.empty() && Runtime::IsNativeArrayTypeName(Field.TypeName))
			{
				FValue ArrayValue = CreateNativeArray(Field.TypeName, Location);
				if (bHasError || bHasPendingScriptException || bExitRequested)
				{
					return FailInitialization();
				}
				Object->Fields.push_back(std::move(ArrayValue));
			}
			else if (!Field.TypeName.empty())
			{
				FValue DefaultValue = CreateDefaultValue(Field.TypeName, Location);
				if (bHasError || bHasPendingScriptException || bExitRequested)
				{
					return FailInitialization();
				}
				Object->Fields.push_back(std::move(DefaultValue));
			}
			else
			{
				Object->Fields.push_back(std::int32_t{0});
			}
		}
		return Object;
	}

	bool VeroScriptVm::DestroyObject(const FObjectHandle& Object, const FSourceLocation& Location)
	{
		if (!Object || !ActiveModule)
		{
			return true;
		}
		if (Object->bDestroyed || Object->bDestroying)
		{
			return true;
		}
		if (Object->bConstructionIncomplete)
		{
			Object->bDestroying = true;
			const bool bHadPendingError = bHasError;
			FScriptError PendingError = std::move(Error);
			bHasError = false;
			Error = {};
			std::vector<bool> CompletedFields;
			const auto ObjectTypeIndex = ResolveObjectTypeIndex(*Object);
			if (!Object->GetCompletedCopyBases().empty())
			{
				CompletedFields.resize(Object->Fields.size(), false);
			}
			for (const auto Base : Object->GetCompletedCopyBases())
			{
				if (Base >= ActiveModule->Types.size())
				{
					continue;
				}
				for (const auto& Field : ActiveModule->Types[Base].Fields)
				{
					const auto Index = FindFieldIndexByName(ObjectTypeIndex, Field.Name);
					if (Index < CompletedFields.size())
					{
						CompletedFields[Index] = true;
					}
				}
			}
			bool Ok = true;
			FScriptError FirstCleanupError;
			const auto RecordCleanupFailure = [&]()
			{
				VmDetail::RecordCleanupFailure(Ok, FirstCleanupError, bHasError, Error);
			};
			for (std::size_t I = Object->Fields.size(); I > 0; --I)
			{
				// Completed bases own their fields until their destructor has run.
				// Their slices need not form a prefix in a multiply inherited object.
				if (!CompletedFields.empty() && CompletedFields[I - 1])
				{
					continue;
				}
				FValue Field = std::move(Object->Fields[I - 1]);
				Object->Fields[I - 1] = std::int32_t{0};
				if (!ReleaseValue(Field, Location))
				{
					RecordCleanupFailure();
				}
			}
			for (auto I = Object->GetCompletedCopyBases().rbegin(); I != Object->GetCompletedCopyBases().rend(); ++I)
			{
				if (!DestroyObjectAsType(Object, *I, Location))
				{
					RecordCleanupFailure();
				}
			}
			Object->ClearCompletedCopyBases();
			if (!ReleaseValueList(Object->Fields, Location))
			{
				RecordCleanupFailure();
			}
			Object->SetHostBase({});
			Object->bDestroying = false;
			Object->bDestroyed = true;
			return VmDetail::FinishCleanup(Ok, bHadPendingError, std::move(PendingError),
			    std::move(FirstCleanupError), bExitRequested, bHasError, Error);
		}
		const std::uint16_t TypeIndex = ResolveObjectTypeIndex(*Object);
		if (TypeIndex == static_cast<std::uint16_t>(-1))
		{
			Object->bDestroyed = true;
			return true;
		}
		if (!bHasError && !bHasPendingScriptException && !bExitRequested && !ExecutionCleanup
		    && !Object->GetHostBase() && TypeIndex < ActiveModule->Types.size()
		    && Object->Fields.size() == ActiveModule->Types[TypeIndex].Fields.size()
		    && GetObjectTypePlan(TypeIndex).bHasTrivialCleanup
		    && std::all_of(Object->Fields.begin(), Object->Fields.end(), IsTrivialObjectFieldValue))
		{
			// The verified layout covers every slot and none of these values can
			// execute user code or report a language cleanup error.
			for (FValue& Field : Object->Fields)
			{
				Field = std::int32_t{0};
			}
			Object->bDestroyed = true;
			Error = {};
			return true;
		}
		Object->bDestroying = true;
		const bool bOk = DestroyObjectAsType(Object, TypeIndex, Location);
		Object->bDestroying = false;
		Object->bDestroyed = true;
		return bOk;
	}

	bool VeroScriptVm::DestroyObjectAsType(const FObjectHandle& Object,
	    const std::uint16_t TypeIndex, const FSourceLocation& Location)
	{
		if (!Object || !ActiveModule || TypeIndex >= ActiveModule->Types.size()) return true;
		return ReleaseValueTree(std::int32_t{0}, Location, Object, TypeIndex);
	}

	bool VeroScriptVm::InvokeObjectDestructor(const FObjectHandle& Object,
	    const std::uint16_t TypeIndex, const FSourceLocation& Location)
	{
		const auto& TypeDesc = ActiveModule->Types[TypeIndex];
		bool bOk = true;
		FScriptError Failure;
		const auto RecordDestroyFailure = [&]()
		{
			if (ExecutionCleanup)
			{
				RecordExecutionCleanupFailure(MakeDiagnostic("diag.execution_cleanup_failed", Location));
			}
			VmDetail::RecordCleanupFailure(bOk, Failure, bHasError, Error);
		};
		if (!SkipScriptCleanup() && TypeDesc.DestructorFunctionIndex != static_cast<std::uint16_t>(-1) && TypeDesc.DestructorFunctionIndex < ActiveModule->Functions.size())
		{
			std::vector<FValue> NoArgs;
			const std::size_t DepthBefore = Frames.size();
			const std::size_t StackBefore = OperandStack.size();
			// CallFunctionAt may execute a JIT entry synchronously, so isolate an
			// outer exit request before dispatch rather than only around Run().
			const bool bSavedExitRequested = bExitRequested;
			const int SavedExitCode = RequestedExitCode;
			bExitRequested = false;
			RequestedExitCode = 0;
			const bool bCallOk = CallFunctionAt(TypeDesc.DestructorFunctionIndex, NoArgs, Object);
			bool bRunOk = false;
			if (bCallOk)
			{
				const std::size_t SavedStopDepth = StopFrameDepth;
				StopFrameDepth = DepthBefore;
				bRunOk = Run();
				StopFrameDepth = SavedStopDepth;
			}
			const bool bDestructorExitRequested = bExitRequested;
			const int DestructorExitCode = RequestedExitCode;
			if (bSavedExitRequested)
			{
				bExitRequested = true;
				RequestedExitCode = SavedExitCode;
			}
			else
			{
				bExitRequested = bDestructorExitRequested;
				RequestedExitCode = DestructorExitCode;
			}
			if (!bCallOk || !bRunOk)
			{
				RecordDestroyFailure();
				// An aborted destructor can leave operand temporaries and one or more
				// nested frames alive. Release their language-owned values instead of
				// relying on raw variant/shared_ptr destruction, which would skip script
				// destructors for the last handle.
				while (OperandStack.size() > StackBefore)
				{
					FValue Discarded = std::move(OperandStack.back());
					OperandStack.pop_back();
					if (!ReleaseValue(Discarded, Location))
					{
						RecordDestroyFailure();
					}
				}
				while (Frames.size() > DepthBefore)
				{
					FCallFrame Aborted = std::move(Frames.back());
					PopRuntimeFrame();
					if (!ReleaseFinallyContinuations(Aborted, Location))
					{
						RecordDestroyFailure();
					}
					if (!ReleaseValueList(Aborted.Locals, Location))
					{
						RecordDestroyFailure();
					}
					if (Aborted.ThisObject)
					{
						FValue ReceiverOwner = std::move(Aborted.ThisObject);
						if (!ReleaseValue(ReceiverOwner, Location))
						{
							RecordDestroyFailure();
						}
					}
					if (Aborted.bHasReturn)
					{
						if (!ReleaseValue(Aborted.ReturnValue, Location))
						{
							RecordDestroyFailure();
						}
					}
				}
			}
			else
			{
				// Discard the void sentinel left by the successful destructor call.
				while (OperandStack.size() > StackBefore)
				{
					FValue Discarded = std::move(OperandStack.back());
					OperandStack.pop_back();
					if (!ReleaseValue(Discarded, Location))
					{
						RecordDestroyFailure();
					}
				}
			}
		}
		return VmDetail::FinishCleanup(bOk, false, {}, std::move(Failure), bExitRequested, bHasError, Error);
	}

	bool VeroScriptVm::ReleaseValue(FValue& Value, const FSourceLocation& Location)
	{
		if (bHasPendingScriptException)
		{
			const bool bReleased = ReleaseWithPendingException(Value, bHasPendingScriptException,
			    PendingScriptException, PendingScriptExceptionLocation,
			    [this, &Location](FValue& Released)
			{
				return ReleaseValue(Released, Location);
			});
			if (bExitRequested)
			{
				// Releasing a superseded exception can itself request exit. Drain the
				// saved transfer and report the resulting state, rather than an earlier
				// provisional failure from the release that replaced it.
				(void)ReleasePendingScriptException();
				return !bHasError;
			}
			return bReleased;
		}
		if (auto* Box = std::get_if<FScriptVariantHandle>(&Value))
		{
			if (!*Box || Box->use_count() != 1) return true;
			return ReleaseValueTree(std::move(Value), Location);
		}
		if (auto* ObjectHandle = std::get_if<FObjectHandle>(&Value))
		{
			if (!*ObjectHandle)
			{
				return true;
			}
			FScriptObject& Object = **ObjectHandle;
			if (Object.bDestroyed || Object.bDestroying)
			{
				return true;
			}
			// Structs and classes both use last-handle destruction so shared
			// LoadLocal/Dup views on the operand stack do not destroy early.
			if (ObjectHandle->use_count() != 1)
			{
				return true;
			}
			return DestroyObject(*ObjectHandle, Location);
		}
		// Primitive and string alternatives own no script-visible lifetime hooks.
		// They dominate arithmetic/container hot paths, so return before probing the
		// long list of container/callable handles below. Script objects stay first so
		// their last-reference destruction path does not regress.
		if (std::holds_alternative<std::int8_t>(Value) || std::holds_alternative<std::uint8_t>(Value) || std::holds_alternative<std::int16_t>(Value) || std::holds_alternative<std::uint16_t>(Value) || std::holds_alternative<std::int32_t>(Value) || std::holds_alternative<std::int64_t>(Value) || std::holds_alternative<float>(Value) || std::holds_alternative<double>(Value) || std::holds_alternative<bool>(Value) || std::holds_alternative<std::uint32_t>(Value) || std::holds_alternative<std::uint64_t>(Value) || std::holds_alternative<std::string>(Value) || std::holds_alternative<std::u16string>(Value))
		{
			return true;
		}
		if (VmDetail::IsFlatArrayLifetime(Value)) return true;
		const bool bOwnsChildren = std::visit([](const auto& Handle)
		{
			using T = std::decay_t<decltype(Handle)>;
			if constexpr (std::is_same_v<T, FNativeArray>)
			{
				return true;
			}
			else if constexpr (std::is_same_v<T, FDynamicArrayHandle> || std::is_same_v<T, FMapHandle>
			    || std::is_same_v<T, FOrderedMapHandle> || std::is_same_v<T, FQueueHandle>
			    || std::is_same_v<T, FStackHandle> || std::is_same_v<T, FDequeHandle> || std::is_same_v<T, FFunctionHandle>)
			{
				return Handle && Handle.use_count() == 1;
			}
			else if constexpr (std::is_same_v<T, FThisFieldRef>)
			{
				return Handle.Object && Handle.Object.use_count() == 1;
			}
			else
			{
				return false;
			}
		}, Value);
		return !bOwnsChildren || ReleaseValueTree(std::move(Value), Location);
	}

	bool VeroScriptVm::ReleasePendingScriptException()
	{
		bool bReleasedAll = true;
		while (bHasPendingScriptException)
		{
			// Destructor callbacks may replace the pending slot. Keep the value
			// being destroyed in stable storage, and drain any new uncaught value.
			FValue Exception = std::move(PendingScriptException);
			PendingScriptException = std::int32_t{0};
			bHasPendingScriptException = false;
			PendingScriptExceptionLocation = {};
			if (!ReleaseValue(Exception, {}))
			{
				bReleasedAll = false;
			}
		}
		return bReleasedAll;
	}

	bool VeroScriptVm::ReleaseValueList(FValue* Values, const std::size_t Count, const FSourceLocation& Location)
	{
		if (Count == 0 && !bHasError)
		{
			Error = {};
			return true;
		}
		const bool bHadPendingError = bHasError;
		FScriptError PendingError = Error;
		bHasError = false;
		Error = {};
		bool bReleasedAll = true;
		FScriptError FirstReleaseError;
		for (std::size_t Index = Count; Index > 0; --Index)
		{
			if (!ReleaseValue(Values[Index - 1], Location))
			{
				VmDetail::RecordCleanupFailure(bReleasedAll, FirstReleaseError, bHasError, Error);
				// Match final-root teardown: one failing destructor must not
				// suppress unrelated remaining language destructors.
			}
			Values[Index - 1] = std::int32_t{0};
		}
		bReleasedAll = VmDetail::FinishCleanup(bReleasedAll, bHadPendingError, std::move(PendingError),
		    std::move(FirstReleaseError), bExitRequested, bHasError, Error);
		return bReleasedAll;
	}

	bool VeroScriptVm::ReleaseValueList(std::vector<FValue>& Values, const FSourceLocation& Location)
	{
		const bool bReleasedAll = ReleaseValueList(Values.data(), Values.size(), Location);
		Values.clear();
		return bReleasedAll;
	}

	bool VeroScriptVm::ReleaseDetachedValueList(
	    std::vector<FValue>& Values,
	    const FSourceLocation& Location)
	{
		const bool bHadPendingError = bHasError;
		FScriptError PendingError = Error;
		bHasError = false;
		Error = {};
		bool bReleasedAll = true;
		FScriptError FirstReleaseError;
		DrainDetachedSequence(
		    Values,
		    [this, &Location, &bReleasedAll, &FirstReleaseError](FValue& Value)
		{
			if (!ReleaseValue(Value, Location))
			{
				VmDetail::RecordCleanupFailure(bReleasedAll, FirstReleaseError, bHasError, Error);
			}
		});
		bReleasedAll = VmDetail::FinishCleanup(bReleasedAll, bHadPendingError, std::move(PendingError),
		    std::move(FirstReleaseError), bExitRequested, bHasError, Error);
		return bReleasedAll;
	}

	bool VeroScriptVm::ReleaseDetachedValueList(
	    std::deque<FValue>& Values,
	    const FSourceLocation& Location)
	{
		const bool bHadPendingError = bHasError;
		FScriptError PendingError = Error;
		bHasError = false;
		Error = {};
		bool bReleasedAll = true;
		FScriptError FirstReleaseError;
		DrainDetachedSequence(
		    Values,
		    [this, &Location, &bReleasedAll, &FirstReleaseError](FValue& Value)
		{
			if (!ReleaseValue(Value, Location))
			{
				VmDetail::RecordCleanupFailure(bReleasedAll, FirstReleaseError, bHasError, Error);
			}
		});
		bReleasedAll = VmDetail::FinishCleanup(bReleasedAll, bHadPendingError, std::move(PendingError),
		    std::move(FirstReleaseError), bExitRequested, bHasError, Error);
		return bReleasedAll;
	}

	bool VeroScriptVm::ReleaseMapValues(
	    FMap& Map,
	    const FSourceLocation& Location)
	{
		const bool bHadPendingError = bHasError;
		FScriptError PendingError = Error;
		bHasError = false;
		Error = {};
		bool bReleasedAll = true;
		FScriptError FirstReleaseError;
		const auto ReleaseEntries =
		    [this, &Location, &bReleasedAll, &FirstReleaseError](auto& Entries)
		{
			DrainDetachedEntries(
			    Entries,
			    [this, &Location, &bReleasedAll, &FirstReleaseError](FValue& Value)
			{
				if (!ReleaseValue(Value, Location))
				{
					VmDetail::RecordCleanupFailure(bReleasedAll, FirstReleaseError, bHasError, Error);
				}
			});
		};
		ReleaseEntries(Map.IntEntries);
		ReleaseEntries(Map.UIntEntries);
		ReleaseEntries(Map.Int64Entries);
		ReleaseEntries(Map.UInt64Entries);
		ReleaseEntries(Map.StrEntries);
		ReleaseEntries(Map.WStrEntries);
		ReleaseEntries(Map.EnumEntries);
		ReleaseEntries(Map.HostEntries);
		bReleasedAll = VmDetail::FinishCleanup(bReleasedAll, bHadPendingError, std::move(PendingError),
		    std::move(FirstReleaseError), bExitRequested, bHasError, Error);
		return bReleasedAll;
	}

	bool VeroScriptVm::ReleaseOrderedMapValues(
	    FOrderedMap& Map,
	    const FSourceLocation& Location)
	{
		// Clear the public order view before a value destructor can reenter the map.
		// Entry storage is detached in batches below, so both views stay consistent.
		Map.IntKeyOrder.clear();
		Map.UIntKeyOrder.clear();
		Map.Int64KeyOrder.clear();
		Map.UInt64KeyOrder.clear();
		Map.StrKeyOrder.clear();
		Map.WStrKeyOrder.clear();
		Map.EnumKeyOrder.clear();
		Map.HostKeyOrder.clear();

		const bool bHadPendingError = bHasError;
		FScriptError PendingError = Error;
		bHasError = false;
		Error = {};
		bool bReleasedAll = true;
		FScriptError FirstReleaseError;
		const auto ReleaseEntries =
		    [this, &Location, &bReleasedAll, &FirstReleaseError](auto& Entries)
		{
			DrainDetachedEntries(
			    Entries,
			    [this, &Location, &bReleasedAll, &FirstReleaseError](FValue& Value)
			{
				if (!ReleaseValue(Value, Location))
				{
					VmDetail::RecordCleanupFailure(bReleasedAll, FirstReleaseError, bHasError, Error);
				}
			});
		};
		ReleaseEntries(Map.IntEntries);
		ReleaseEntries(Map.UIntEntries);
		ReleaseEntries(Map.Int64Entries);
		ReleaseEntries(Map.UInt64Entries);
		ReleaseEntries(Map.StrEntries);
		ReleaseEntries(Map.WStrEntries);
		ReleaseEntries(Map.EnumEntries);
		ReleaseEntries(Map.HostEntries);
		// A destructor may have inserted a replacement entry while a detached
		// batch was draining. The entry loop above releases it; discard the matching
		// order records created by that reentrant insertion as well.
		Map.IntKeyOrder.clear();
		Map.UIntKeyOrder.clear();
		Map.Int64KeyOrder.clear();
		Map.UInt64KeyOrder.clear();
		Map.StrKeyOrder.clear();
		Map.WStrKeyOrder.clear();
		Map.EnumKeyOrder.clear();
		Map.HostKeyOrder.clear();
		bReleasedAll = VmDetail::FinishCleanup(bReleasedAll, bHadPendingError, std::move(PendingError),
		    std::move(FirstReleaseError), bExitRequested, bHasError, Error);
		return bReleasedAll;
	}

	bool VeroScriptVm::DiscardExecutionAbove(
	    const std::size_t FrameDepth,
	    const std::size_t StackDepth,
	    const FSourceLocation& Location)
	{
		const bool bHadExecutionCleanup = ExecutionCleanup != nullptr;
		const bool bHadPendingError = bHasError;
		const bool bHadPendingTransfer = bHasPendingScriptException || bExitRequested;
		FScriptError PendingError = Error;
		bHasError = false;
		Error = {};
		bool bReleasedAll = true;
		FScriptError FirstReleaseError;
		const auto RecordReleaseFailure = [&]()
		{
			VmDetail::RecordCleanupFailure(bReleasedAll, FirstReleaseError, bHasError, Error);
		};

		// Operand values can alias locals/receivers. Drop them first so last-owner
		// checks observe the same order as an ordinary successful frame return.
		// Unwinding may cross several frame bases; these are cleanup operations,
		// not bytecode operand reads restricted to the current frame.
		while (OperandStack.size() > StackDepth)
		{
			FValue Discarded = std::move(OperandStack.back());
			OperandStack.pop_back();
			if (!ReleaseValue(Discarded, Location))
			{
				RecordReleaseFailure();
			}
		}
		while (Frames.size() > FrameDepth)
		{
			FCallFrame Aborted = std::move(Frames.back());
			// Cleanup parks the original error before releasing language roots.
			// Keep that outcome on the aborted entry without changing VM flags.
			if (Aborted.DiagnosticToken)
			{
				MarkFunctionDiagnosticOutcome(JitDiagnostics.get(),
					Aborted.DiagnosticToken, bHadPendingError, bHadPendingTransfer);
			}
			PopRuntimeFrame();
			std::optional<FObjectLifetimeScope> ConstructionStage;
			if (Aborted.ThisObject && Aborted.ThisObject->bConstructionIncomplete && Aborted.Function && Aborted.Function->bReturnsVoid && Aborted.Function->bIsMethod && Aborted.Function->OwnerTypeIndex < ActiveModule->Types.size() && MethodLinkageBareLeaf(Aborted.Function->Name) == ModuleMeta::TypeSimpleName(ActiveModule->Types[Aborted.Function->OwnerTypeIndex].Name))
			{
				ConstructionStage.emplace(ObjectLifetimeScope, Frames.size(), Aborted.ThisObject, Aborted.Function->OwnerTypeIndex);
			}
			if (!ReleaseFinallyContinuations(Aborted, Location))
			{
				RecordReleaseFailure();
			}
			if (!ReleaseValueList(Aborted.Locals, Location))
			{
				RecordReleaseFailure();
			}
			if (Aborted.ThisObject)
			{
				if (!DestroyAbortedConstruction(Aborted, Location))
				{
					RecordReleaseFailure();
				}
				FValue ReceiverOwner = std::move(Aborted.ThisObject);
				if (!ReleaseValue(ReceiverOwner, Location))
				{
					RecordReleaseFailure();
				}
			}
			if (Aborted.bHasReturn && !ReleaseValue(Aborted.ReturnValue, Location))
			{
				RecordReleaseFailure();
			}
		}
		AbortLocalStaticInitializers(FrameDepth);
		// A pending script transfer may have a provisional "uncaught" diagnostic
		// at a nested VM/JIT boundary. An explicit exit during cleanup supersedes
		// that transfer; genuine runtime failures keep their first diagnostic.
		if (!bHadExecutionCleanup && bHadPendingError)
		{
			PreserveExecutionCleanupError(PendingError);
		}
		bReleasedAll = VmDetail::FinishCleanup(bReleasedAll, bHadPendingError, std::move(PendingError),
		    std::move(FirstReleaseError), bExitRequested, bHasError, Error);
		return bReleasedAll;
	}

	bool VeroScriptVm::AssignValue(FValue& Slot, FValue NewValue, const FSourceLocation& Location)
	{
		// Install the new value before releasing the old one. SetIndex binds Slot to a
		// live container element; releasing first can reenter Run() and invalidate Slot
		// (vector reallocation / map erase) before the write.
		FValue Previous = std::move(Slot);
		Slot = std::move(NewValue);
		return ReleaseValue(Previous, Location);
	}

	bool VeroScriptVm::DiscardValueRoots(
	    std::vector<FValue>& Roots,
	    const std::unordered_set<const void*>* const KeepHandles)
	{
		bool bReleasedAll = true;
		FScriptError FirstReleaseError;
		const auto RecordReleaseFailure = [this, &bReleasedAll, &FirstReleaseError]()
		{
			VmDetail::RecordCleanupFailure(bReleasedAll, FirstReleaseError, bHasError, Error);
			// A failing destructor must not prevent unrelated remaining roots
			// from receiving their own release/destructor pass.
			OperandStack.clear();
			ClearRuntimeFrames();
		};

		// Phase 1: gather every reachable script object behind a strong reference
		// so the entire graph outlives the destructor pass, independent of any
		// reference cycles. This lets destructors observe intact peer fields.
		std::vector<FObjectHandle> Reachable;
		std::unordered_set<const void*> CollectVisited;
		const auto HasGraphValue = [](const FValue& Value)
		{
			return std::visit([](const auto& Storage)
			{
				using T = std::decay_t<decltype(Storage)>;
				if constexpr (std::is_same_v<T, FNativeArray>)
				{
					return true;
				}
				else if constexpr (std::is_same_v<T, FScriptVariantHandle> || std::is_same_v<T, FObjectHandle> || std::is_same_v<T, FDynamicArrayHandle> || std::is_same_v<T, FMapHandle> || std::is_same_v<T, FOrderedMapHandle> || std::is_same_v<T, FQueueHandle> || std::is_same_v<T, FStackHandle> || std::is_same_v<T, FDequeHandle> || std::is_same_v<T, FFunctionHandle>)
				{
					return static_cast<bool>(Storage);
				}
				else
				{
					return false;
				}
			}, Value);
		};
		std::vector<const FValue*> CollectPending;
		std::unique_ptr<std::deque<FValue>> CapturedValues;
		const auto EnqueueCollect = [&](const FValue& Value)
		{
			if (HasGraphValue(Value))
			{
				CollectPending.push_back(&Value);
			}
		};
		const auto CollectObjects =
		    [&](const FValue& Value) -> void
		{
			if (const auto* Box = std::get_if<FScriptVariantHandle>(&Value))
			{
				if (*Box && CollectVisited.insert(Box->get()).second)
				{
					EnqueueCollect((*Box)->Value);
				}
				return;
			}
			if (const auto* ObjectHandle = std::get_if<FObjectHandle>(&Value))
			{
				if (!*ObjectHandle || !CollectVisited.insert(ObjectHandle->get()).second)
				{
					return;
				}
				Reachable.push_back(*ObjectHandle);
				for (const FValue& Field : (*ObjectHandle)->Fields)
				{
					EnqueueCollect(Field);
				}
				return;
			}
			if (const auto* NativeArray = std::get_if<FNativeArray>(&Value))
			{
				for (const FValue& Element : NativeArray->Storage().Elements)
				{
					EnqueueCollect(Element);
				}
				return;
			}
			if (const auto* DynamicArray = std::get_if<FDynamicArrayHandle>(&Value))
			{
				if (!*DynamicArray || !CollectVisited.insert(DynamicArray->get()).second)
				{
					return;
				}
				for (const FValue& Element : (*DynamicArray)->Elements)
				{
					EnqueueCollect(Element);
				}
				return;
			}
			if (const auto* MapHandle = std::get_if<FMapHandle>(&Value))
			{
				if (!*MapHandle || !CollectVisited.insert(MapHandle->get()).second)
				{
					return;
				}
				for (const auto& Entry : (*MapHandle)->IntEntries)
				{
					EnqueueCollect(Entry.second);
				}
				for (const auto& Entry : (*MapHandle)->UIntEntries)
				{
					EnqueueCollect(Entry.second);
				}
				for (const auto& Entry : (*MapHandle)->Int64Entries)
				{
					EnqueueCollect(Entry.second);
				}
				for (const auto& Entry : (*MapHandle)->UInt64Entries)
				{
					EnqueueCollect(Entry.second);
				}
				for (const auto& Entry : (*MapHandle)->StrEntries)
				{
					EnqueueCollect(Entry.second);
				}
				for (const auto& Entry : (*MapHandle)->WStrEntries)
				{
					EnqueueCollect(Entry.second);
				}
				for (const auto& Entry : (*MapHandle)->EnumEntries)
				{
					EnqueueCollect(Entry.second);
				}
				for (const auto& Entry : (*MapHandle)->HostEntries)
				{
					EnqueueCollect(Entry.second);
				}
				return;
			}
			if (const auto* OrderedMap = std::get_if<FOrderedMapHandle>(&Value))
			{
				if (!*OrderedMap || !CollectVisited.insert(OrderedMap->get()).second)
				{
					return;
				}
				for (const auto& Entry : (*OrderedMap)->IntEntries)
				{
					EnqueueCollect(Entry.second);
				}
				for (const auto& Entry : (*OrderedMap)->UIntEntries)
				{
					EnqueueCollect(Entry.second);
				}
				for (const auto& Entry : (*OrderedMap)->Int64Entries)
				{
					EnqueueCollect(Entry.second);
				}
				for (const auto& Entry : (*OrderedMap)->UInt64Entries)
				{
					EnqueueCollect(Entry.second);
				}
				for (const auto& Entry : (*OrderedMap)->StrEntries)
				{
					EnqueueCollect(Entry.second);
				}
				for (const auto& Entry : (*OrderedMap)->WStrEntries)
				{
					EnqueueCollect(Entry.second);
				}
				for (const auto& Entry : (*OrderedMap)->EnumEntries)
				{
					EnqueueCollect(Entry.second);
				}
				for (const auto& Entry : (*OrderedMap)->HostEntries)
				{
					EnqueueCollect(Entry.second);
				}
				return;
			}
			if (const auto* QueueHandle = std::get_if<FQueueHandle>(&Value))
			{
				if (!*QueueHandle || !CollectVisited.insert(QueueHandle->get()).second)
				{
					return;
				}
				for (const FValue& Element : (*QueueHandle)->Elements)
				{
					EnqueueCollect(Element);
				}
				return;
			}
			if (const auto* StackHandle = std::get_if<FStackHandle>(&Value))
			{
				if (!*StackHandle || !CollectVisited.insert(StackHandle->get()).second)
				{
					return;
				}
				for (const FValue& Element : (*StackHandle)->Elements)
				{
					EnqueueCollect(Element);
				}
				return;
			}
			if (const auto* DequeHandle = std::get_if<FDequeHandle>(&Value))
			{
				if (!*DequeHandle || !CollectVisited.insert(DequeHandle->get()).second)
				{
					return;
				}
				for (const FValue& Element : (*DequeHandle)->Elements)
				{
					EnqueueCollect(Element);
				}
				return;
			}
			if (const auto* FunctionHandle = std::get_if<FFunctionHandle>(&Value))
			{
				if (!*FunctionHandle || !CollectVisited.insert(FunctionHandle->get()).second)
				{
					return;
				}
				for (const FValue& Capture : (*FunctionHandle)->Captures)
				{
					EnqueueCollect(Capture);
				}
				if ((*FunctionHandle)->CapturedThis)
				{
					if (!CapturedValues)
					{
						CapturedValues = std::make_unique<std::deque<FValue>>();
					}
					CapturedValues->emplace_back((*FunctionHandle)->CapturedThis);
					EnqueueCollect(CapturedValues->back());
				}
				return;
			}
		};
		for (const FValue& Root : Roots)
		{
			EnqueueCollect(Root);
		}
		std::reverse(CollectPending.begin(), CollectPending.end());
		while (!CollectPending.empty())
		{
			const FValue* const Value = CollectPending.back();
			CollectPending.pop_back();
			const auto ChildrenBegin = CollectPending.size();
			CollectObjects(*Value);
			std::reverse(CollectPending.begin() + static_cast<std::ptrdiff_t>(ChildrenBegin), CollectPending.end());
		}
		// The original temporary captured receivers were gone before destructors ran.
		CapturedValues.reset();

		// Phase 2: run each exclusive destructor exactly once while the graph is
		// still referenced by Reachable, so destructor bodies see valid peers.
		for (const FObjectHandle& Object : Reachable)
		{
			if (!Object || Object->bDestroyed || Object->bDestroying)
			{
				continue;
			}
			if (KeepHandles != nullptr && KeepHandles->find(Object.get()) != KeepHandles->end())
			{
				continue;
			}
			if (!DestroyObject(Object, {}))
			{
				RecordReleaseFailure();
			}
		}

		// Phase 3: sever references among discarded storage. Retained KeepHandles
		// (objects, containers, closures) must not be mutated.
		std::unordered_set<const void*> Visited;
		const auto IsKept =
		    [KeepHandles](const void* Identity) -> bool
		{
			return KeepHandles != nullptr && KeepHandles->find(Identity) != KeepHandles->end();
		};
		enum class EDetachAction : std::uint8_t
		{
			Visit,
			AssignZero,
			ReleaseTemporary,
			ReleaseSequence,
			MapEntries,
			ClearMap,
			ClearOrderedMap,
			FinishFunction,
		};
		struct FMapDetachCursor
		{
			virtual ~FMapDetachCursor() = default;
			virtual FValue* Next() = 0;
		};
		struct FDetachTask
		{
			EDetachAction Action;
			union
			{
				FValue* Value;
				FMapDetachCursor* Cursor;
			};
			FDetachTask(const EDetachAction InAction, FValue* InValue)
			    : Action(InAction), Value(InValue)
			{
			}
			explicit FDetachTask(FMapDetachCursor* InCursor)
			    : Action(EDetachAction::MapEntries), Cursor(InCursor)
			{
			}
		};
		struct FDetachTemporaries
		{
			std::deque<FValue> Values;
			~FDetachTemporaries()
			{
				// Match local-value unwinding when a nested detach/release fails.
				while (!Values.empty())
				{
					Values.pop_back();
				}
			}
		};
		const auto DetachValue = [&](FValue& Root) -> bool
		{
			std::vector<FDetachTask> Pending;
			std::unique_ptr<FDetachTemporaries> Temporaries;
			std::vector<std::unique_ptr<FMapDetachCursor>> MapCursors;
			const auto EnqueueVisit = [&](FValue& Value)
			{
				if (HasGraphValue(Value))
				{
					Pending.push_back({EDetachAction::Visit, &Value});
				}
			};
			const auto EnqueueAssignment = [&](FValue& Value)
			{
				EnqueueVisit(Value);
				Pending.push_back({EDetachAction::AssignZero, &Value});
			};
			const auto StoreTemporary = [&](FValue&& Value) -> FValue*
			{
				if (!Temporaries)
				{
					Temporaries = std::make_unique<FDetachTemporaries>();
				}
				Temporaries->Values.emplace_back(std::move(Value));
				return &Temporaries->Values.back();
			};
			const auto EnqueueEntries = [&](auto& Map)
			{
				if (Map.IntEntries.empty() && Map.UIntEntries.empty() && Map.Int64Entries.empty() && Map.UInt64Entries.empty() && Map.StrEntries.empty() && Map.WStrEntries.empty() && Map.EnumEntries.empty() && Map.HostEntries.empty())
				{
					return;
				}
				const auto MakeTableCursor = [](auto& Entries)
				{
					return [&Entries, Position = Entries.end(), bStarted = false]() mutable -> FValue*
					{
						// A host payload released by the current entry may erase a later
						// entry. Advance only after that release, just like a range-for.
						if (!bStarted)
						{
							Position = Entries.begin();
							bStarted = true;
						}
						else
						{
							++Position;
						}
						return Position == Entries.end() ? nullptr : &Position->second;
					};
				};
				auto Tables = std::make_tuple(
				    MakeTableCursor(Map.IntEntries), MakeTableCursor(Map.UIntEntries),
				    MakeTableCursor(Map.Int64Entries), MakeTableCursor(Map.UInt64Entries),
				    MakeTableCursor(Map.StrEntries), MakeTableCursor(Map.WStrEntries),
				    MakeTableCursor(Map.EnumEntries), MakeTableCursor(Map.HostEntries));
				using FTables = decltype(Tables);
				struct FCursor final : FMapDetachCursor
				{
					FTables Tables;
					std::size_t TableIndex = 0;
					explicit FCursor(FTables InTables) : Tables(std::move(InTables))
					{
					}
					FValue* Next() override
					{
						FValue* Value = nullptr;
						std::size_t Index = 0;
						std::apply([&](auto&... NextTable)
						{
							([&]
							{
								if (TableIndex == Index)
								{
									Value = NextTable();
									if (!Value)
									{
										++TableIndex;
									}
								}
								++Index;
							}(), ...);
						}, Tables);
						return Value;
					}
				};
				auto Cursor = std::make_unique<FCursor>(std::move(Tables));
				Pending.emplace_back(Cursor.get());
				MapCursors.push_back(std::move(Cursor));
			};
			const auto ClearEntries = [](auto& Map)
			{
				Map.IntEntries.clear();
				Map.UIntEntries.clear();
				Map.Int64Entries.clear();
				Map.UInt64Entries.clear();
				Map.StrEntries.clear();
				Map.WStrEntries.clear();
				Map.EnumEntries.clear();
				Map.HostEntries.clear();
			};
			const auto Process = [&](const FDetachTask& Task) -> bool
			{
				if (Task.Action == EDetachAction::MapEntries)
				{
					if (FValue* Entry = Task.Cursor->Next())
					{
						EnqueueAssignment(*Entry);
						Pending.emplace_back(Task.Cursor);
					}
					else
					{
						MapCursors.pop_back();
					}
					return true;
				}
				FValue& Value = *Task.Value;
				switch (Task.Action)
				{
				case EDetachAction::AssignZero:
					return AssignValue(Value, std::int32_t{0}, {});
				case EDetachAction::ReleaseTemporary:
					{
						const bool Ok = ReleaseValue(Value, {});
						Temporaries->Values.pop_back();
						return Ok;
					}
				case EDetachAction::ReleaseSequence:
					{
						if (auto* Array = std::get_if<FNativeArray>(&Value))
						{
							return ReleaseValueList(Array->Storage().Elements, {});
						}
						if (auto* Array = std::get_if<FDynamicArrayHandle>(&Value))
						{
							return ReleaseValueList((*Array)->Elements, {});
						}
						if (auto* Queue = std::get_if<FQueueHandle>(&Value))
						{
							return ReleaseValueList((*Queue)->Elements, {});
						}
						if (auto* Stack = std::get_if<FStackHandle>(&Value))
						{
							return ReleaseValueList((*Stack)->Elements, {});
						}
						if (auto* Deque = std::get_if<FDequeHandle>(&Value))
						{
							return ReleaseValueList((*Deque)->Elements, {});
						}
						return true;
					}
				case EDetachAction::ClearMap:
					{
						ClearEntries(*std::get<FMapHandle>(Value));
						return true;
					}
				case EDetachAction::ClearOrderedMap:
					{
						auto& Map = *std::get<FOrderedMapHandle>(Value);
						ClearEntries(Map);
						Map.IntKeyOrder.clear();
						Map.UIntKeyOrder.clear();
						Map.Int64KeyOrder.clear();
						Map.UInt64KeyOrder.clear();
						Map.StrKeyOrder.clear();
						Map.WStrKeyOrder.clear();
						Map.EnumKeyOrder.clear();
						Map.HostKeyOrder.clear();
						return true;
					}
				case EDetachAction::FinishFunction:
					{
						auto& Function = *std::get<FFunctionHandle>(Value);
						Function.Captures.clear();
						if (Function.CapturedThis)
						{
							FValue* CapturedThis = StoreTemporary(FValue(std::move(Function.CapturedThis)));
							EnqueueVisit(*CapturedThis);
							Pending.push_back({EDetachAction::ReleaseTemporary, CapturedThis});
						}
						return true;
					}
				case EDetachAction::MapEntries:
				case EDetachAction::Visit:
					{
						break;
					}

				default:
					{
						break;
					}
				}
				if (auto* Box = std::get_if<FScriptVariantHandle>(&Value))
				{
					if (!*Box || IsKept(Box->get()) || !Visited.insert(Box->get()).second) return true;
					// Independent primitive boxes remain immutable. Module-owned
					// payloads move into stable storage until their post-order release.
					if (!(*Box)->Source) return true;
					FValue* Payload = StoreTemporary(std::move((*Box)->Value));
					(*Box)->Value = std::int32_t{0};
					(*Box)->CanonicalTypeName.clear();
					(*Box)->Source.reset();
					EnqueueVisit(*Payload);
					Pending.push_back({EDetachAction::ReleaseTemporary, Payload});
					return true;
				}
				if (auto* Object = std::get_if<FObjectHandle>(&Value))
				{
					if (!*Object || !Visited.insert(Object->get()).second || IsKept(Object->get())) return true;
					for (FValue& Field : (*Object)->Fields)
					{
						EnqueueAssignment(Field);
					}
					return true;
				}
				if (auto* Array = std::get_if<FNativeArray>(&Value))
				{
					for (FValue& Element : Array->Storage().Elements)
					{
						EnqueueVisit(Element);
					}
					Pending.push_back({EDetachAction::ReleaseSequence, &Value});
					return true;
				}
				if (auto* Array = std::get_if<FDynamicArrayHandle>(&Value))
				{
					if (!*Array || !Visited.insert(Array->get()).second || IsKept(Array->get())) return true;
					for (FValue& Element : (*Array)->Elements)
					{
						EnqueueVisit(Element);
					}
					Pending.push_back({EDetachAction::ReleaseSequence, &Value});
					return true;
				}
				if (auto* Map = std::get_if<FMapHandle>(&Value))
				{
					if (!*Map || !Visited.insert(Map->get()).second || IsKept(Map->get())) return true;
					EnqueueEntries(**Map);
					Pending.push_back({EDetachAction::ClearMap, &Value});
					return true;
				}
				if (auto* Map = std::get_if<FOrderedMapHandle>(&Value))
				{
					if (!*Map || !Visited.insert(Map->get()).second || IsKept(Map->get())) return true;
					EnqueueEntries(**Map);
					Pending.push_back({EDetachAction::ClearOrderedMap, &Value});
					return true;
				}
				if (auto* Queue = std::get_if<FQueueHandle>(&Value))
				{
					if (!*Queue || !Visited.insert(Queue->get()).second || IsKept(Queue->get())) return true;
					for (FValue& Element : (*Queue)->Elements)
					{
						EnqueueVisit(Element);
					}
					Pending.push_back({EDetachAction::ReleaseSequence, &Value});
					return true;
				}
				if (auto* Stack = std::get_if<FStackHandle>(&Value))
				{
					if (!*Stack || !Visited.insert(Stack->get()).second || IsKept(Stack->get())) return true;
					for (FValue& Element : (*Stack)->Elements)
					{
						EnqueueVisit(Element);
					}
					Pending.push_back({EDetachAction::ReleaseSequence, &Value});
					return true;
				}
				if (auto* Deque = std::get_if<FDequeHandle>(&Value))
				{
					if (!*Deque || !Visited.insert(Deque->get()).second || IsKept(Deque->get())) return true;
					for (FValue& Element : (*Deque)->Elements)
					{
						EnqueueVisit(Element);
					}
					Pending.push_back({EDetachAction::ReleaseSequence, &Value});
					return true;
				}
				if (auto* Function = std::get_if<FFunctionHandle>(&Value))
				{
					if (!*Function || !Visited.insert(Function->get()).second || IsKept(Function->get())) return true;
					for (FValue& Capture : (*Function)->Captures)
					{
						EnqueueAssignment(Capture);
					}
					Pending.push_back({EDetachAction::FinishFunction, &Value});
				}
				return true;
			};

			EnqueueVisit(Root);
			while (!Pending.empty())
			{
				const FDetachTask Task = Pending.back();
				Pending.pop_back();
				const auto ChildrenBegin = Pending.size();
				if (!Process(Task)) return false;
				// Preserve recursive child/continuation order, including short-circuit
				// failure: no parent assignment/finalizer runs after a failed child.
				std::reverse(Pending.begin() + static_cast<std::ptrdiff_t>(ChildrenBegin), Pending.end());
			}
			return true;
		};
		for (FValue& Root : Roots)
		{
			if (!DetachValue(Root))
			{
				RecordReleaseFailure();
			}
		}
		for (std::size_t Index = Roots.size(); Index > 0; --Index)
		{
			if (!ReleaseValue(Roots[Index - 1], {}))
			{
				Roots[Index - 1] = std::int32_t{0};
				RecordReleaseFailure();
			}
		}
		Reachable.clear();
		Roots.clear();
		return VmDetail::FinishCleanup(bReleasedAll, false, {}, std::move(FirstReleaseError), bExitRequested, bHasError,
		    Error);
	}
}
