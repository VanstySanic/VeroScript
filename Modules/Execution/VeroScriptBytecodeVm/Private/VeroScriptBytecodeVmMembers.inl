// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace VeroScript
{
	VEROSCRIPT_VM_MEMBER_INLINE std::uint16_t FBytecodeVmExecution::ResolveObjectTypeIndex(const FScriptObject& Object) const
	{
		if (Object.bDestroyed)
		{
			Services.ReportError("receiver has already been destroyed", {});
			return 0xFFFF;
		}
		// Borrow Core's published identity without writing an object-local cache.
		// Cross-program/reload identities still resolve through the owning Core.
		const auto* Module = State.GetActiveModule();
		if (Module && Object.RuntimeTypeModule == Module
			&& Object.RuntimeTypeGeneration == State.GetProgramGeneration()
			&& Object.RuntimeTypeIndex < Module->Types.size())
		{
			return Object.RuntimeTypeIndex;
		}
		return Services.ResolveObjectTypeIndex(Object);
	}
	VEROSCRIPT_VM_MEMBER_INLINE std::uint16_t FBytecodeVmExecution::FindFieldIndex(
		FCallFrame& Frame, std::uint16_t TypeIndex, std::uint16_t NameIndex) const
	{
		if (TypeIndex != 0xFFFF && Frame.IncFieldIcTypeIndex == TypeIndex
			&& Frame.IncFieldIcNameIndex == NameIndex)
		{
			return Frame.IncFieldIcFieldIndex;
		}
		const auto Index = Services.FindJitFieldIndex(TypeIndex, NameIndex);
		if (TypeIndex != 0xFFFF && Index != 0xFFFF)
		{
			Frame.IncFieldIcTypeIndex = TypeIndex;
			Frame.IncFieldIcNameIndex = NameIndex;
			Frame.IncFieldIcFieldIndex = Index;
		}
		return Index;
	}
}
