// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptHttp.h"
#include "VeroScriptHttpSse.h"
#include <VeroScriptJson.h>
#include <algorithm>
#include <new>
#include <stdexcept>

namespace VeroScript::Http
{
	// HTTP status/headers/body remain in the caller's FResponse on JSON failure.
	// A response contract error and a JSON syntax/type error remain distinguishable.
	struct FJsonBodyError
	{
		explicit operator bool() const noexcept
		{
			return bool(Http) || bool(Json);
		}

		Network::FError Http;
		Json::FError Json;
	};
	inline bool IsJsonMediaType(std::string_view Type)
	{
		Type = Type.substr(0, Type.find(';'));
		while (!Type.empty() && (Type.front() == ' ' || Type.front() == '\t'))
		{
			Type.remove_prefix(1);
		}
		while (!Type.empty() && (Type.back() == ' ' || Type.back() == '\t'))
		{
			Type.remove_suffix(1);
		}
		const auto Equal = [](std::string_view A, std::string_view B)
		{
			return A.size() == B.size() && std::equal(A.begin(),
											   A.end(),
											   B.begin(),
											   [](char X, char Y)
											   {
												   return (X >= 'A' && X <= 'Z' ? X + ('a' - 'A') : X) == Y;
											   });
		};
		const auto Token = [](std::string_view Text)
		{
			if (Text.empty())
			{
				return false;
			}
			for (const auto C : Text)
			{
				if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') ||
						std::string_view("!#$%&'*+-.^_`|~").find(C) != std::string_view::npos))
				{
					return false;
				}
			}
			return true;
		};
		if (Equal(Type, "application/json"))
		{
			return true;
		}
		const auto Slash = Type.find('/');
		return Slash != Type.npos && Token(Type.substr(0, Slash)) && Token(Type.substr(Slash + 1)) &&
			   Type.size() - Slash - 1 > 5 && Equal(Type.substr(Type.size() - 5), "+json") &&
			   Type.find_first_of(" \t\r\n") == Type.npos;
	}
	inline bool TryParseJsonBody(const FResponse& Response, Json::FJsonValue& Out, FJsonBodyError& Error,
		const Json::FParseOptions& Options = {}, Json::FWork Work = {}, bool RequireContentType = true)
	{
		Error = {};
		try
		{
			if (!Response.bComplete)
			{
				Error.Http.Code = Network::EError::State;
				return false;
			}
			if (RequireContentType)
			{
				const auto Types = Response.Headers.GetAll("Content-Type");
				if (Types.size() != 1 || !IsJsonMediaType(Types[0]))
				{
					Error.Http.Code = Network::EError::Protocol;
					return false;
				}
			}
			return Json::TryParse(
				std::string_view(Response.Body.empty() ? "" : reinterpret_cast<const char*>(Response.Body.data()),
					Response.Body.size()),
				Out,
				Error.Json,
				Options,
				Work);
		}
		catch (const std::bad_alloc&)
		{
			Error.Http.Code = Network::EError::Allocation;
			return false;
		}
		catch (const std::length_error&)
		{
			Error.Http.Code = Network::EError::Limit;
			return false;
		}
	}
	inline bool TrySetJsonBody(FRequest& Request, const Json::FJsonValue& Value, FJsonBodyError& Error,
		const Json::FWriteOptions& Options = {}, Json::FWork Work = {})
	{
		Error = {};
		try
		{
			std::string Text;
			if (!Json::TryWrite(Value, Text, Error.Json, Options, Work))
			{
				return false;
			}
			Network::FBytes Body(Text.begin(), Text.end());
			auto Headers = Request.Headers;
			if (!Headers.TrySet("Content-Type", "application/json", Error.Http))
			{
				return false;
			}
			Headers.Erase("Content-Length");
			Headers.Erase("Transfer-Encoding");
			// Everything that can allocate has succeeded before the commit.
			Request.Headers = std::move(Headers);
			Request.Body = std::move(Body);
			Request.Upload.reset();
			Request.UploadFactory = {};
			Request.UploadLength.reset();
			Request.Multipart.reset();
			return true;
		}
		catch (const std::bad_alloc&)
		{
			Error.Http.Code = Network::EError::Allocation;
			return false;
		}
		catch (const std::length_error&)
		{
			Error.Http.Code = Network::EError::Limit;
			return false;
		}
	}
	inline bool TryParseJsonEvent(const FSseEvent& Event, Json::FJsonValue& Out, Json::FError& Error,
		const Json::FParseOptions& Options = {}, Json::FWork Work = {})
	{
		return Json::TryParse(Event.Data, Out, Error, Options, Work);
	}
} // namespace VeroScript::Http
