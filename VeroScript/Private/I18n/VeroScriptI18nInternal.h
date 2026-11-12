// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptI18n.h"
#include <functional>
#include <mutex>
#include <new>
#include <exception>

namespace VeroScript::I18n::Detail
{
	bool Fail(FI18nError& Error, EI18nStatus Status, std::string Detail);
	bool ValidName(std::string_view Name) noexcept;
	bool ValidFormatId(std::string_view Name) noexcept;
	bool CheckWork(FI18nWork Work, FI18nError& Error, std::uint64_t Units = 0);
	bool CheckSize(std::size_t Size, FI18nWork Work, FI18nError& Error);
	void Identify(FI18nError& Error, std::string_view Domain, std::string_view Key,
		std::string_view Language, std::string_view Argument = {});

	template<class F> bool Boundary(FI18nError& Error, F&& Action)
	{
		Error = {};
		try
		{
			return Action();
		}
		catch (const std::bad_alloc&)
		{
			return Fail(Error, EI18nStatus::OutOfMemory, "allocation failed");
		}
		catch (const std::exception& E)
		{
			return Fail(Error, EI18nStatus::ProviderFailure, E.what());
		}
		catch (...)
		{
			return Fail(Error, EI18nStatus::ProviderFailure, "unknown native exception");
		}
	}

	struct FContextConfiguration
	{
		FI18nConfiguration Config;
		std::shared_ptr<FI18nServices> Services;
		std::uint64_t Revision = 1;
	};

	class FOperationScope
	{
	public:
		FOperationScope(std::uint64_t ContextId, FI18nError& Error);
		~FOperationScope();
		FOperationScope(const FOperationScope&) = delete;
		FOperationScope& operator=(const FOperationScope&) = delete;
		explicit operator bool() const noexcept
		{
			return Entered;
		}

	private:
		bool Entered = false;
	};
}

namespace VeroScript::I18n
{
	struct FI18nSnapshot::FData
	{
		std::shared_ptr<const Detail::FContextConfiguration> Context;
		std::shared_ptr<const FI18nServicesSnapshot> Services;
		FI18nVersion Stamp;
	};
}
