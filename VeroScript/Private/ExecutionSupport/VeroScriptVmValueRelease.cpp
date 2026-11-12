// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVm.h"
#include "VeroScriptVmCleanup.h"
#include "VeroScriptVariantInternal.h"
#include "VeroScriptVmValueRelease.h"

#include <algorithm>
#include <deque>
#include <memory>

namespace VeroScript
{
	namespace
	{
		enum class EReleaseStage
		{
			Value,
			Type,
			Fields,
			Bases,
			PartialFields,
			PartialBases,
			RemainingFields,
			ObjectComplete,
			Children,
			Finish
		};

		struct FReleaseFrame
		{
			FValue Value;
			FObjectHandle Object;
			std::unique_ptr<FVmObjectLifetimeScope> Lifetime;
			std::vector<FValue> Children;
			std::vector<bool> CompletedFields;
			std::vector<std::uint16_t> CompletedBases;
			FValue Exception;
			FSourceLocation ExceptionLocation;
			FScriptError PendingError;
			FScriptError Failure;
			EReleaseStage Stage = EReleaseStage::Value;
			std::uint16_t TypeIndex = 0xFFFF;
			std::size_t Next = 0;
			std::size_t NextBase = 0;
			bool bOk = true;
			bool bHadError = false;
			bool bHadCleanup = false;
			bool bSavedException = false;
		};

		bool NeedsValueRelease(const FValue& Value)
		{
			if (VmDetail::IsFlatArrayLifetime(Value)) return false;
			return std::holds_alternative<FObjectHandle>(Value) || std::holds_alternative<FNativeArray>(Value)
			    || std::holds_alternative<FDynamicArrayHandle>(Value) || std::holds_alternative<FMapHandle>(Value)
			    || std::holds_alternative<FOrderedMapHandle>(Value) || std::holds_alternative<FQueueHandle>(Value)
			    || std::holds_alternative<FStackHandle>(Value) || std::holds_alternative<FDequeHandle>(Value)
			    || std::holds_alternative<FFunctionHandle>(Value) || std::holds_alternative<FScriptVariantHandle>(Value)
			    || std::holds_alternative<FThisFieldRef>(Value);
		}

		template <typename TMap>
		void DetachMapChildren(TMap& Map, std::vector<FValue>& Children)
		{
			const auto Detach = [&](auto& Entries)
			{
				for (auto& Entry : Entries)
				{
					Children.push_back(std::move(Entry.second));
					Entry.second = std::int32_t{0};
				}
				Entries.clear();
			};
			Detach(Map.IntEntries);
			Detach(Map.UIntEntries);
			Detach(Map.Int64Entries);
			Detach(Map.UInt64Entries);
			Detach(Map.StrEntries);
			Detach(Map.WStrEntries);
			Detach(Map.EnumEntries);
			Detach(Map.HostEntries);
			std::reverse(Children.begin(), Children.end());
		}

		void ClearMapOrder(FOrderedMap& Map)
		{
			Map.IntKeyOrder.clear();
			Map.UIntKeyOrder.clear();
			Map.Int64KeyOrder.clear();
			Map.UInt64KeyOrder.clear();
			Map.StrKeyOrder.clear();
			Map.WStrKeyOrder.clear();
			Map.EnumKeyOrder.clear();
			Map.HostKeyOrder.clear();
		}
	}

	bool VeroScriptVm::ReleaseValueTree(FValue Value, const FSourceLocation& Location,
	    const FObjectHandle& Object, const std::uint16_t TypeIndex)
	{
		// Frames own detached values across callbacks. No field/vector iterator is
		// retained while user code executes, and structural cleanup uses heap depth.
		std::deque<FReleaseFrame> Pending;
		const auto Enter = [&]() -> FReleaseFrame&
		{
			auto& Frame = Pending.emplace_back();
			Frame.bHadError = bHasError;
			Frame.bHadCleanup = ExecutionCleanup != nullptr;
			Frame.PendingError = std::move(Error);
			bHasError = false;
			Error = {};
			if (bHasPendingScriptException)
			{
				Frame.bSavedException = true;
				Frame.Exception = std::move(PendingScriptException);
				Frame.ExceptionLocation = PendingScriptExceptionLocation;
				PendingScriptException = std::int32_t{0};
				PendingScriptExceptionLocation = {};
				bHasPendingScriptException = false;
			}
			return Frame;
		};
		const auto PushValue = [&](FValue Child)
		{
			Enter().Value = std::move(Child);
		};
		const auto TryReleaseImmediateChild = [&](FValue& Child, FReleaseFrame& Parent)
		{
			const auto* Handle = std::get_if<FObjectHandle>(&Child);
			if (!Handle) return false;
			// Releasing a non-last handle cannot execute a lifetime hook. A null
			// alias with an owning control block can, so it keeps the ordinary frame.
			if (!*Handle) return Handle->use_count() == 0;
			if (Handle->use_count() != 1) return true;
			if ((*Handle)->bDestroyed || (*Handle)->bDestroying) return false;
			const auto Type = ResolveObjectTypeIndex(**Handle);
			if (!Parent.bOk || Parent.bHadError || Parent.bSavedException
			    || bHasError || bHasPendingScriptException || bExitRequested || ExecutionCleanup
			    || (*Handle)->bConstructionIncomplete || (*Handle)->GetHostBase() || !ActiveModule
			    || Type >= ActiveModule->Types.size() || (*Handle)->Fields.size() != ActiveModule->Types[Type].Fields.size()
			    || !GetObjectTypePlan(Type).bHasTrivialCleanup
			    || !std::all_of((*Handle)->Fields.begin(), (*Handle)->Fields.end(), VmDetail::IsTrivialLifetimeValue))
			{
				return false;
			}
			// The same complete proof is used below after entering a release frame.
			// Ordinary scalar-only children need neither saved error state nor a
			// lifetime continuation. Preserve the original position in release order.
			if (!DestroyObject(*Handle, Location))
			{
				VmDetail::RecordCleanupFailure(Parent.bOk, Parent.Failure, bHasError, Error);
			}
			// A failed destruction was already entered and recorded. Never replay it
			// through the generic frame path, even if a later implementation can fail.
			return true;
		};
		const auto PushType = [&](const FObjectHandle& Receiver, const std::uint16_t Type)
		{
			auto& Frame = Enter();
			Frame.Object = Receiver;
			Frame.TypeIndex = Type;
			Frame.Stage = EReleaseStage::Type;
		};
		if (Object)
		{
			PushType(Object, TypeIndex);
		}
		else
		{
			PushValue(std::move(Value));
		}
		while (!Pending.empty())
		{
			auto& Frame = Pending.back();
			switch (Frame.Stage)
			{
			case EReleaseStage::Value:
				{
					Frame.Stage = EReleaseStage::Finish;
					if (auto* Handle = std::get_if<FObjectHandle>(&Frame.Value))
					{
						if (!*Handle || (*Handle)->bDestroyed || (*Handle)->bDestroying || Handle->use_count() != 1)
						{
							break;
						}
						const auto Type = ResolveObjectTypeIndex(**Handle);
						if (!(*Handle)->bConstructionIncomplete && !Frame.bHadError && !Frame.bSavedException
						    && !bExitRequested && !ExecutionCleanup && !(*Handle)->GetHostBase() && ActiveModule
						    && Type < ActiveModule->Types.size() && (*Handle)->Fields.size() == ActiveModule->Types[Type].Fields.size()
						    && GetObjectTypePlan(Type).bHasTrivialCleanup
						    && std::all_of((*Handle)->Fields.begin(), (*Handle)->Fields.end(), VmDetail::IsTrivialLifetimeValue))
						{
							// Reuse the proven scalar-object fast path inside aggregates too.
							// It cannot recurse or execute script/native lifetime callbacks.
							if (!DestroyObject(*Handle, Location))
							{
								VmDetail::RecordCleanupFailure(Frame.bOk, Frame.Failure, bHasError, Error);
							}
							break;
						}
						Frame.Object = std::move(*Handle);
						Frame.Object->bDestroying = true;
						Frame.TypeIndex = ResolveObjectTypeIndex(*Frame.Object);
						Frame.Stage = EReleaseStage::ObjectComplete;
						if (Frame.Object->bConstructionIncomplete)
						{
							Frame.CompletedBases = Frame.Object->GetCompletedCopyBases();
							Frame.CompletedFields.resize(Frame.Object->Fields.size(), false);
							for (const auto Base : Frame.CompletedBases)
							{
								if (!ActiveModule || Base >= ActiveModule->Types.size())
								{
									continue;
								}
								for (const auto& Field : ActiveModule->Types[Base].Fields)
								{
									const auto Index = FindFieldIndexByName(Frame.TypeIndex, Field.Name);
									if (Index < Frame.CompletedFields.size())
									{
										Frame.CompletedFields[Index] = true;
									}
								}
							}
							Frame.Next = Frame.Object->Fields.size();
							Frame.NextBase = Frame.CompletedBases.size();
							Frame.Stage = EReleaseStage::PartialFields;
						}
						else if (ActiveModule && Frame.TypeIndex < ActiveModule->Types.size())
						{
							PushType(Frame.Object, Frame.TypeIndex);
						}
						else
						{
							Frame.Next = Frame.Object->Fields.size();
							Frame.Stage = EReleaseStage::RemainingFields;
						}
						break;
					}
					if (auto* Box = std::get_if<FScriptVariantHandle>(&Frame.Value))
					{
						if (!*Box || Box->use_count() != 1)
						{
							break;
						}
						FValue Payload = std::move((*Box)->Value);
						(*Box)->Value = std::int32_t{0};
						(*Box)->CanonicalTypeName.clear();
						(*Box)->Source.reset();
						Box->reset();
						PushValue(std::move(Payload));
						break;
					}
					if (auto* Ref = std::get_if<FThisFieldRef>(&Frame.Value))
					{
						PushValue(std::move(Ref->Object));
						break;
					}
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
						else
						{
							return false;
						}
					}, Frame.Value);
					if (bOwnsChildren)
					{
						Frame.Stage = EReleaseStage::Children;
					}
					break;
				}
			case EReleaseStage::Type:
				{
					const auto& Type = ActiveModule->Types[Frame.TypeIndex];
					Frame.Lifetime = std::make_unique<FVmObjectLifetimeScope>(ObjectLifetimeScope, Frames.size(), Frame.Object, Frame.TypeIndex);
					Frame.Next = Type.Fields.size();
					Frame.NextBase = Type.BaseTypeIndices.size();
					Frame.Stage = EReleaseStage::Fields;
					if (!InvokeObjectDestructor(Frame.Object, Frame.TypeIndex, Location))
					{
						VmDetail::RecordCleanupFailure(Frame.bOk, Frame.Failure, bHasError, Error);
					}
					break;
				}
			case EReleaseStage::Fields:
			case EReleaseStage::PartialFields:
			case EReleaseStage::RemainingFields:
				{
					const auto* Type = Frame.Stage == EReleaseStage::Fields ? &ActiveModule->Types[Frame.TypeIndex] : nullptr;
					const auto Begin = Type ? Type->OwnFieldStartIndex : 0;
					bool bChild = false;
					while (Frame.Next > Begin)
					{
						const auto Field = --Frame.Next;
						const auto Index = Type && Frame.TypeIndex != ResolveObjectTypeIndex(*Frame.Object)
						    ? FindFieldIndexByName(ResolveObjectTypeIndex(*Frame.Object), Type->Fields[Field].Name) : Field;
						if (Index >= Frame.Object->Fields.size()
						    || (Frame.Stage == EReleaseStage::PartialFields && Frame.CompletedFields[Index]))
						{
							continue;
						}
						FValue Child = std::move(Frame.Object->Fields[Index]);
						Frame.Object->Fields[Index] = std::int32_t{0};
						if (!NeedsValueRelease(Child) || TryReleaseImmediateChild(Child, Frame))
						{
							continue;
						}
						PushValue(std::move(Child));
						bChild = true;
						break;
					}
					if (bChild)
					{
						break;
					}
					Frame.Stage = Type ? EReleaseStage::Bases
					    : Frame.Stage == EReleaseStage::PartialFields ? EReleaseStage::PartialBases : EReleaseStage::ObjectComplete;
					break;
				}
			case EReleaseStage::Bases:
			case EReleaseStage::PartialBases:
				{
					const bool bPartial = Frame.Stage == EReleaseStage::PartialBases;
					const auto& Bases = bPartial ? Frame.CompletedBases : ActiveModule->Types[Frame.TypeIndex].BaseTypeIndices;
					if (Frame.NextBase)
					{
						const auto Base = Bases[--Frame.NextBase];
						if (Base < ActiveModule->Types.size())
						{
							PushType(Frame.Object, Base);
						}
					}
					else if (bPartial)
					{
						Frame.Object->ClearCompletedCopyBases();
						Frame.Next = Frame.Object->Fields.size();
						Frame.Stage = EReleaseStage::RemainingFields;
						Frame.Object->bConstructionIncomplete = false;
					}
					else
					{
						Frame.Stage = EReleaseStage::Finish;
					}
					break;
				}
			case EReleaseStage::ObjectComplete:
				{
					Frame.Object->SetHostBase({});
					Frame.Object->bDestroying = false;
					Frame.Object->bDestroyed = true;
					Frame.Object.reset();
					Frame.Stage = EReleaseStage::Finish;
					break;
				}
			case EReleaseStage::Children:
				{
					if (Frame.Children.empty())
					{
						std::visit([&](auto& Handle)
						{
							using T = std::decay_t<decltype(Handle)>;
							if constexpr (std::is_same_v<T, FNativeArray>)
							{
								if (Handle.Data)
								{
									Frame.Children.swap(Handle.Storage().Elements);
								}
							}
							else if constexpr (std::is_same_v<T, FMapHandle> || std::is_same_v<T, FOrderedMapHandle>)
							{
								if constexpr (std::is_same_v<T, FOrderedMapHandle>)
								{
									ClearMapOrder(*Handle);
								}
								DetachMapChildren(*Handle, Frame.Children);
							}
							else if constexpr (std::is_same_v<T, FFunctionHandle>)
							{
								Frame.Children.swap(Handle->Captures);
								if (Frame.Children.empty() && Handle->CapturedThis)
								{
									Frame.Children.emplace_back(std::move(Handle->CapturedThis));
								}
							}
							else if constexpr (std::is_same_v<T, FDynamicArrayHandle> || std::is_same_v<T, FStackHandle>)
							{
								Frame.Children.swap(Handle->Elements);
							}
							else if constexpr (std::is_same_v<T, FQueueHandle> || std::is_same_v<T, FDequeHandle>)
							{
								Frame.Children.reserve(Handle->Elements.size());
								while (!Handle->Elements.empty())
								{
									Frame.Children.push_back(std::move(Handle->Elements.front()));
									Handle->Elements.pop_front();
								}
							}
						}, Frame.Value);
						if (Frame.Children.empty())
						{
							Frame.Stage = EReleaseStage::Finish;
							break;
						}
					}
					FValue Child = std::move(Frame.Children.back());
					Frame.Children.pop_back();
					if (NeedsValueRelease(Child) && !TryReleaseImmediateChild(Child, Frame))
					{
						PushValue(std::move(Child));
					}
					break;
				}
			case EReleaseStage::Finish:
				{
					if (Frame.bSavedException && bHasPendingScriptException)
					{
						FValue Superseded = std::move(Frame.Exception);
						Frame.Exception = std::move(PendingScriptException);
						Frame.ExceptionLocation = PendingScriptExceptionLocation;
						PendingScriptException = std::int32_t{0};
						bHasPendingScriptException = false;
						PushValue(std::move(Superseded));
						break;
					}
					if (Frame.bSavedException)
					{
						PendingScriptException = std::move(Frame.Exception);
						PendingScriptExceptionLocation = Frame.ExceptionLocation;
						bHasPendingScriptException = true;
					}
					if (ExecutionCleanup && !Frame.bHadCleanup && Frame.bHadError)
					{
						PreserveExecutionCleanupError(Frame.PendingError);
					}
					const bool bOk = VmDetail::FinishCleanup(Frame.bOk, Frame.bHadError, std::move(Frame.PendingError),
					    std::move(Frame.Failure), bExitRequested, bHasError, Error);
					Pending.pop_back();
					if (Pending.empty())
					{
						return bOk;
					}
					if (!bOk)
					{
						VmDetail::RecordCleanupFailure(Pending.back().bOk, Pending.back().Failure, bHasError, Error);
					}
					break;
				}
			default:
				{
					return false;
				}
			}
		}
		return true;
	}
}
