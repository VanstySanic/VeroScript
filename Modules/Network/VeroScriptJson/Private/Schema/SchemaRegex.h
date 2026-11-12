// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <string_view>
#include <stdexcept>

namespace VeroScript::Json::Detail
{
	class FSchemaRegexSyntax : public std::runtime_error
	{
	public:
		using std::runtime_error::runtime_error;
	};

	class FSchemaRegex
	{
	public:
		explicit FSchemaRegex(std::string_view Pattern);
		bool Search(std::string_view Text) const;

	private:
		struct FImpl;
		std::shared_ptr<const FImpl> Impl;
	};
} // namespace VeroScript::Json::Detail
