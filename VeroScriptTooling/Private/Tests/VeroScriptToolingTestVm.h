// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptTooling.h"
#include "VeroScriptTestVm.h"

namespace VeroScript::Test
{
	struct FDapStdio : Tooling::VeroScriptDapStdio
	{
		static int Run(std::istream& Input, std::ostream& Output,
		    const Tooling::FDapStdioRunOptions& Options = {}, Tooling::IVeroScriptLiveDebugDomain* Domain = nullptr)
		{
			auto Selected = Options;
			if (!Selected.VmBackendFactory && Selected.VmBackendPath.empty())
			{
				Selected.VmBackendFactory = VmFactory();
			}
			if (!Selected.ScriptCompileExtensions.VmBackendFactory && Selected.ScriptCompileExtensions.VmBackendPath.empty())
			{
				Selected.ScriptCompileExtensions.VmBackendFactory = Selected.VmBackendFactory;
			}
			return Tooling::VeroScriptDapStdio::Run(Input, Output, Selected, Domain);
		}
	};
	struct FLspStdio
	{
		static int Run(std::istream& Input, std::ostream& Output, const Tooling::FLspStdioRunOptions& Options = {})
		{
			auto Selected = Options;
			if (!Selected.ScriptCompileExtensions.VmBackendFactory && Selected.ScriptCompileExtensions.VmBackendPath.empty())
			{
				Selected.ScriptCompileExtensions.VmBackendFactory = VmFactory();
			}
			return Tooling::VeroScriptLspStdio::Run(Input, Output, Selected);
		}
	};
}
