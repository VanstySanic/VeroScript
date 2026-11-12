// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptStandardLibraryServices.h"
#include "VeroScriptBytecode.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptNative.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptMethodKey.h"

namespace VeroScript::StandardLibrary
{
	bool IsDiscreteArgument(std::string_view Name, std::size_t Index) noexcept
	{
		Name.remove_prefix(FindLinkageIdentityStart(Name));
		const auto Signature = Name.find('#');
		if (Signature != std::string_view::npos)
		{
			Name = Name.substr(0, Signature);
		}
		if (Name == "__std_parse" || Name == "__std_repeat" || Name == "__std_read_bytes" || Name == "__std_mutex_lock_for" || Name == "__std_semaphore_acquire_for" || Name == "__std_algorithm_rotate")
		{
			return Index == 1;
		}
		if (Name == "__std_try_parse") return Index == 2;
		if (Name == "__std_numeric_pow") return Index == 1;
		if (Name == "__std_make_semaphore") return Index < 2;
		return false;
	}
	bool IsDiscreteArgument(const FBytecodeModule& Module, const FBytecodeFunction& Function, std::size_t Index)
	{
		if (IsDiscreteArgument(Function.Name, Index)) return true;
		if (Function.bIsMethod && Index == 0 && Function.OwnerTypeIndex < Module.Types.size() && ModuleMeta::TypeSimpleName(Module.Types[Function.OwnerTypeIndex].Name) == "task")
		{
			const auto Separator = Function.Name.rfind("::");
			const auto Method = Separator == std::string::npos ? std::string_view{} : std::string_view(Function.Name).substr(Separator + 2);
			if (Method.substr(0, Method.find('#')) != "await_for") return false;
			// A user namespace may also contain a type named task. Require the
			// implementation operation, rather than assigning roles by leaf names.
			for (std::size_t I = 0; I < Function.Code.size();)
			{
				const auto Op = static_cast<EOpcode>(Function.Code[I]);
				const auto Size = Bytecode::InstructionSize(Op);
				if (!Size || Size > Function.Code.size() - I) return false;
				if (Op == EOpcode::CallBuiltin && Function.Code[I + 1] == static_cast<unsigned>(EBuiltinId::TypedTaskAwaitFor)) return true;
				I += Size;
			}
		}
		return false;
	}
	bool ResolveTaskLayout(const FBytecodeModule& Module, std::string_view TypeName,
	    FTaskLayout& Output, std::string_view ElementType)
	{
		std::uint16_t TypeIndex;
		if (ModuleMeta::TypeSimpleName(std::string(TypeName)) != "task" || !ModuleMeta::FindTypeIndex(Module, TypeName, TypeIndex))
		{
			return false;
		}
		const auto& Type = Module.Types[TypeIndex];
		FTaskLayout Layout;
		if (Type.Fields.size() != 2 || !ModuleMeta::FindFieldIndex(Module, std::string(TypeName), "task_id", Layout.Id) || !ModuleMeta::FindFieldIndex(Module, std::string(TypeName), "result", Layout.Result) || Type.Fields[Layout.Id].TypeName != "int")
		{
			return false;
		}
		std::string ActualElement;
		if (!Runtime::ParseChannelElementType(Type.Fields[Layout.Result].TypeName, ActualElement) || (!ElementType.empty() && !NativeElementTypeNamesEqual(ActualElement, ElementType)))
		{
			return false;
		}
		Output = Layout;
		return true;
	}
}
