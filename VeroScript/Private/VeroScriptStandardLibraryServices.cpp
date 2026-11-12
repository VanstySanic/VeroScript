// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptStandardLibraryServices.h"
#include "VeroScriptBytecode.h"

namespace VeroScript::StandardLibrary
{
	bool UsesRuntimeFoundation(const FBytecodeModule& Module)
	{
		const auto UsesCode = [&](const std::vector<std::uint8_t>& Code)
		{
			for (std::size_t I = 0; I < Code.size();)
			{
				const auto Op = static_cast<EOpcode>(Code[I]);
				const auto Size = Bytecode::InstructionSize(Op);
				if (!Size || Size > Code.size() - I) return true;
				if (Op == EOpcode::MakeClosure)
				{
					const auto Index = static_cast<unsigned>(Code[I + 1]) | (static_cast<unsigned>(Code[I + 2]) << 8);
					if (Index >= Module.Functions.size()) return true;
					const auto& Target = Module.Functions[Index];
					// Target-language callable signatures coerce their arguments before
					// entering a helper. They cannot retain the original discrete category.
					for (std::size_t Arg = 0; Arg < Target.ParamCount; ++Arg)
					{
						if (IsDiscreteArgument(Module, Target, Arg)) return true;
					}
				}
				if (Op == EOpcode::CallBuiltin)
				{
					const auto Id = static_cast<EBuiltinId>(Code[I + 1]);
					// Existing pure operations have equivalent standalone target helpers.
					const bool Portable = Id == EBuiltinId::NumericTypeValidate
						|| (Id >= EBuiltinId::StringStartsWith && Id <= EBuiltinId::StringTrim)
						|| (Id >= EBuiltinId::ArrayReverse && Id <= EBuiltinId::RandomIndex);
					if (!Portable && Id >= EBuiltinId::ScalarParse && Id <= EBuiltinId::RandomIndex) return true;
				}
				I += Size;
			}
			return false;
		};
		if (UsesCode(Module.InitCode)) return true;
		for (const auto& Function : Module.Functions)
		{
			if (UsesCode(Function.Code)) return true;
		}
		for (const auto& Type : Module.Types)
		{
			for (const auto& Field : Type.Fields)
			{
				if (UsesCode(Field.InitCode)) return true;
			}
			for (const auto& Field : Type.StaticFields)
			{
				if (UsesCode(Field.InitCode)) return true;
			}
		}
		return false;
	}
}
