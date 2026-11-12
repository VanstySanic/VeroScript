// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptPlatformBindingState.h"
#include <exception>

namespace VeroScript::Platform::BindingDetail
{
	bool Invoke(const FNativeCallContext& Context, FValue& Result, std::string& Error)
	{
		try
		{
			FNativeCallContext Explicit = Context;
			// Core includes the receiver in Args while HostThis points to its payload.
			if (Context.HostThis)
			{
				++Explicit.Args;
				if (Explicit.MutableArgs)
				{
					++Explicit.MutableArgs;
				}
				--Explicit.ArgCount;
			}
			return (*static_cast<const FCall*>(Context.UserData))(Explicit, Result, Error);
		}
		catch (const std::exception& Exception)
		{
			Error = std::string("platform.native_exception: ") + Exception.what();
			return false;
		}
		catch (...)
		{
			Error = "platform.native_exception: unknown exception";
			return false;
		}
	}
	FRegistrar::FRegistrar(FHostBindingRegistry& R, std::shared_ptr<FState> S, std::string& E, bool bInConstMethod)
		: Registry(R), State(std::move(S)), Error(E), bConstMethod(bInConstMethod)
	{
	}
	bool FRegistrar::operator()(unsigned Type, const char* Name, EHostMemberKind Kind, ENativeType ReturnType,
								std::vector<ENativeType> Params, FCall Call, const char* HostReturn,
								std::vector<std::string> HostParams) const
	{
		auto Owner = std::make_shared<FCall>(std::move(Call));
		FHostMemberDesc Member;
		Member.Name = Name;
		Member.Kind = Kind;
		Member.bConstMethod = bConstMethod;
		Member.ReturnType = ReturnType;
		Member.ParamTypes = std::move(Params);
		Member.ReturnHostTypeName = HostReturn;
		Member.ParamHostTypeNames = std::move(HostParams);
		Member.Callback = Invoke;
		Member.UserData = Owner.get();
		Member.CallbackOwner = Owner;
		return Registry.RegisterMember(State->Ids[Type], std::move(Member), Error);
	}
}
