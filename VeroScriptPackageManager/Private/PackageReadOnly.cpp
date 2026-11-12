// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "PackageInstalledInternal.h"
#include "PackageOperation.h"
namespace VeroScript::Packages
{
using namespace Detail;
	bool FPackageManager::Load(const FEnvironmentInput& I, std::shared_ptr<const FInstalledEnvironment>& Out,
							   FDiagnostic& E, const FClientOptions& O) const
	{
		try
		{
			E = {};
			Out.reset();
			FEnvironmentLayout L;
			if (!ResolveEnvironment(I, L, E))
			{
				return false;
			}
			FWork W{O};
			FOperationScope Scope(L.Management);
			auto P = LoadRequirements(L, W, false), S = LoadState(L, W);
			ConfigureStorage(L, I, P, S);
			FFileLock Guard(ManagementRoot(L) / "manager.lock", false, false, true, &W);
			auto Data = ReadInstalled(L, W);
			Out = std::shared_ptr<const FInstalledEnvironment>(new FInstalledEnvironment(std::move(Data)));
			return true;
		}
		catch (const FFailure& X)
		{
			E = X.Diagnostic;
			Out.reset();
			return false;
		}
		catch (const std::exception& X)
		{
			E.Code = "environment-load";
			E.Message = X.what();
			Out.reset();
			return false;
		}
	}
}
