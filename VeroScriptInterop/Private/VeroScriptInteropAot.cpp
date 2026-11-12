// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInterop.h"
#include "VeroScriptInteropInternal.h"
#include "VeroScriptAotProgram.h"

struct vs_interop_aot_program
{
	std::shared_ptr<const VeroScript::IAotProgram> Program;
};

extern "C"
{
	vs_interop_status VS_INTEROP_CALL vs_interop_aot_program_create(
	    const vs_interop_aot_description* Description, const vs_interop_aot_function* Functions,
	    uint64_t Count, vs_interop_aot_program** Output, vs_interop_buffer** Error)
	{
		using namespace VeroScript;
		if (Output)
		{
			*Output = nullptr;
		}
		if (Error)
		{
			*Error = nullptr;
		}
		if (!Output || !Description || Description->struct_size != sizeof(*Description) ||
		    Count > 65535 || (Count && !Functions))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		for (const auto Text : {Description->program_digest, Description->native_bindings_digest,
		         Description->service_abi, Description->core_implementation, Description->target})
		{
			if (Text.size > 4096 || !Interop::IsValidText(Text)) return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			FAotProgramDescription D;
			D.CoreVersion = Description->core_version;
			D.ProgramDigest = Interop::CopyString(Description->program_digest);
			D.NativeBindingsDigest = Interop::CopyString(Description->native_bindings_digest);
			D.ServiceAbi = Interop::CopyString(Description->service_abi);
			D.CoreImplementation = Interop::CopyString(Description->core_implementation);
			D.Target = Interop::CopyString(Description->target);
			std::vector<FAotFunctionEntry> Entries;
			Entries.reserve(static_cast<std::size_t>(Count));
			for (std::size_t I = 0; I < Count; ++I)
			{
				if (Functions[I].reserved || Functions[I].reserved2) return VS_INTEROP_STATUS_INVALID_ARGUMENT;
				Entries.push_back({Functions[I].function_index, Functions[I].entry, Functions[I].governance_capabilities});
			}
			std::string Failure;
			auto Program = CreateAotProgram(std::move(D), std::move(Entries), Failure);
			if (!Program)
			{
				if (Error)
				{
					const auto Status = Interop::MakeTextBuffer(std::move(Failure), Error);
					if (Status != VS_INTEROP_STATUS_OK) return Status;
				}
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			auto Handle = std::make_unique<vs_interop_aot_program>();
			Handle->Program = std::move(Program);
			*Output = Handle.release();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_aot_program_retain(
	    const vs_interop_aot_program* Program, vs_interop_aot_program** Output)
	{
		if (Output)
		{
			*Output = nullptr;
		}
		if (!Program || !Output) return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		try
		{
			*Output = new vs_interop_aot_program{Program->Program};
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}
	void VS_INTEROP_CALL vs_interop_aot_program_release(vs_interop_aot_program* Program)
	{
		delete Program;
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_session_set_aot_program(vs_interop_session* Session,
	    const vs_interop_aot_program* Program)
	{
		using namespace VeroScript::Interop;
		if (!Session || !Session->State) return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		const auto State = Session->State;
		if (const auto Status = RequireOwnerThread(*State); Status != VS_INTEROP_STATUS_OK) return Status;
		if (const auto Status = RequireIdleSession(*State, "SetAotProgram"); Status != VS_INTEROP_STATUS_OK) return Status;
		try
		{
			const FScopedSessionActivity Activity(*State);
			State->Session.SetAotProgram(Program ? Program->Program : nullptr);
			State->LastError.clear();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return TranslateException(State.get());
		}
	}
}
