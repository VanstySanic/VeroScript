// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptAotProgram.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptJitModule.h"
#include "VeroScriptSha256.h"

namespace VeroScript
{
	namespace
	{
		bool IsDigest(const std::string& Digest)
		{
			return Digest.size() == 64 && Digest.find_first_not_of("0123456789abcdef") == std::string::npos;
		}
		class FStaticAotProgram final : public IAotProgram
		{
		public:
			FAotProgramDescription Description;
			std::vector<FAotFunctionEntry> Functions;
			std::shared_ptr<const void> Owner;
			const FAotProgramDescription& GetDescription() const noexcept override
			{
				return Description;
			}
			const std::vector<FAotFunctionEntry>& GetFunctions() const noexcept override
			{
				return Functions;
			}
			const FAotFunctionEntry* Find(const std::uint16_t Index) const noexcept override
			{
				const auto I = std::lower_bound(Functions.begin(), Functions.end(), Index,
				    [](const auto& F, auto Value)
				{
					return F.FunctionIndex < Value;
				});
				return I == Functions.end() || I->FunctionIndex != Index ? nullptr : &*I;
			}
		};
	}

	std::string GetAotServiceAbi()
	{
		return BuildJitModuleAbi();
	}

	bool ComputeAotProgramDigest(const FBytecodeModule& Module, std::string& Digest, std::string& Error, FExecutionCheck Check)
	{
		Digest.clear();
		if (!Check.Poll())
		{
			Error = "AOT validation stopped by host";
			return false;
		}
		std::vector<std::uint8_t> Bytes;
		if (!VeroScriptBytecodeIO::SerializeBytecode(Module, 0, Bytes, Error)) return false;
		if (!ExecutionSupport::Sha256(Bytes.data(), Bytes.size(), Digest, Check))
		{
			Error = "AOT validation stopped by host";
			return false;
		}
		return true;
	}

	std::string ComputeAotNativeBindingsDigest(const std::vector<FNativeFunction>& Natives)
	{
		std::string Digest, Error;
		ComputeAotNativeBindingsDigest(Natives, Digest, Error);
		return Digest;
	}

	bool ComputeAotNativeBindingsDigest(const std::vector<FNativeFunction>& Natives,
	    std::string& Digest, std::string& Error, FExecutionCheck Check)
	{
		Digest.clear();
		Error.clear();
		std::string Signature;
		const auto Add = [&](std::string_view Value)
		{
			Signature += std::to_string(Value.size());
			Signature += ':';
			Signature += Value;
		};
		const auto AddTypeNames = [&](const std::vector<std::string>& Names)
		{
			Add(std::to_string(Names.size()));
			for (std::size_t I = 0; I < Names.size(); ++I)
			{
				if ((I & 255u) == 0 && !Check.Poll())
				{
					Error = "AOT validation stopped by host";
					return false;
				}
				Add(Names[I]);
			}
			return true;
		};
		for (const auto& F : Natives)
		{
			if (!Check.Poll())
			{
				Error = "AOT validation stopped by host";
				return false;
			}
			// Coercion tags and hidden generic arguments are part of the runtime
			// contract even when their resolved script type spellings are identical.
			Add(F.Name);
			Add(std::to_string(static_cast<std::uint32_t>(F.ReturnType)));
			Add(ResolveNativeReturnTypeName(F));
			Add(std::to_string(F.ReturnGenericTemplateId));
			if (!AddTypeNames(F.ReturnGenericTypeArguments)) return false;
			Add(std::to_string(F.ParamTypes.size()));
			for (std::size_t I = 0; I < F.ParamTypes.size(); ++I)
			{
				if ((I & 255u) == 0 && !Check.Poll())
				{
					Error = "AOT validation stopped by host";
					return false;
				}
				Add(std::to_string(static_cast<std::uint32_t>(F.ParamTypes[I])));
				Add(ResolveNativeParamTypeName(F, I));
				Add(std::to_string(I < F.ParamFlags.size() ? F.ParamFlags[I] : 0));
			}
			Add(F.bHostStaticMethod ? "static" : "ordinary");
			Add(F.bGenericHostObjectCast ? "generic-cast" : "ordinary-call");
			if (F.bGenericHostObjectCast)
			{
				if (!AddTypeNames(F.TypeParams)) return false;
				Add(F.ScriptReturnTypeName);
			}
		}
		if (!ExecutionSupport::Sha256(reinterpret_cast<const std::uint8_t*>(Signature.data()), Signature.size(), Digest, Check))
		{
			Error = "AOT validation stopped by host";
			return false;
		}
		return true;
	}

	std::shared_ptr<const IAotProgram> CreateAotProgram(FAotProgramDescription Description,
	    std::vector<FAotFunctionEntry> Functions, std::string& Error, std::shared_ptr<const void> CodeOwner)
	{
		Error.clear();
		if (!IsDigest(Description.ProgramDigest) || !IsDigest(Description.NativeBindingsDigest) ||
		    Description.ServiceAbi.empty() || Description.Target.empty() || Description.CoreImplementation.empty() ||
		    Description.CoreVersion != VEROSCRIPT_VERSION_CODE || Functions.size() > 65535)
		{
			Error = "invalid AOT program description";
			return {};
		}
		std::sort(Functions.begin(), Functions.end(), [](const auto& A, const auto& B)
		{
			return A.FunctionIndex < B.FunctionIndex;
		});
		for (std::size_t I = 0; I < Functions.size(); ++I)
		{
			if (!Functions[I].Entry || (I && Functions[I - 1].FunctionIndex == Functions[I].FunctionIndex))
			{
				Error = "invalid or duplicate AOT function entry";
				return {};
			}
		}
		auto Program = std::make_shared<FStaticAotProgram>();
		Program->Description = std::move(Description);
		Program->Functions = std::move(Functions);
		Program->Owner = std::move(CodeOwner);
		return Program;
	}

	bool ValidateAotProgram(const IAotProgram& Program, const FBytecodeModule& Module,
	    const std::vector<FNativeFunction>& Natives, std::string& Error, FExecutionCheck Check)
	{
		const auto& D = Program.GetDescription();
		if (D.CoreVersion != VEROSCRIPT_VERSION_CODE || D.ServiceAbi != GetAotServiceAbi() ||
		    D.CoreImplementation != VEROSCRIPT_CORE_IMPLEMENTATION_ID || D.Target != GetHostPlatformInfo().Platform)
		{
			Error = "AOT target or Core service contract mismatch";
			return false;
		}
		std::string Digest, BindingsDigest;
		if (!ComputeAotProgramDigest(Module, Digest, Error, Check) ||
		    !ComputeAotNativeBindingsDigest(Natives, BindingsDigest, Error, Check))
		{
			return false;
		}
		if (Digest != D.ProgramDigest || BindingsDigest != D.NativeBindingsDigest)
		{
			Error = "AOT bytecode or native binding identity mismatch";
			return false;
		}
		for (const auto& F : Program.GetFunctions())
		{
			if (!Check.Poll() || F.FunctionIndex >= Module.Functions.size() || !F.Entry)
			{
				Error = "invalid AOT function table or execution stopped";
				return false;
			}
		}
		return true;
	}
}
