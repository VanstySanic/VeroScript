// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptWebSocket.h"
#include <VeroScriptJson.h>
#include <new>

namespace VeroScript::WebSocket
{
	inline bool TryParseJsonMessage(const FMessage& Message, Json::FJsonValue& Out, Json::FError& Error,
		const Json::FParseOptions& Options = {}, Json::FWork Work = {})
	{
		Error = {};
		if (!Message.bText)
		{
			Error.Code = Json::EError::Type;
			return false;
		}
		if (!Message.bFirst || !Message.bFinal)
		{
			Error.Code = Json::EError::State;
			return false;
		}
		return Json::TryParse(
			std::string_view(
				Message.Bytes.empty() ? "" : reinterpret_cast<const char*>(Message.Bytes.data()), Message.Bytes.size()),
			Out,
			Error,
			Options,
			Work);
	}
	inline bool TryMakeJsonMessage(const Json::FJsonValue& Value, FMessage& Out, Json::FError& Error,
		const Json::FWriteOptions& Options = {}, Json::FWork Work = {})
	{
		Error = {};
		std::string Text;
		if (!Json::TryWrite(Value, Text, Error, Options, Work))
		{
			return false;
		}
		try
		{
			FMessage Next;
			Next.bText = true;
			Next.bFinal = true;
			Next.Bytes.assign(Text.begin(), Text.end());
			Out = std::move(Next);
			return true;
		}
		catch (const std::bad_alloc&)
		{
			Error.Code = Json::EError::Allocation;
			return false;
		}
	}
} // namespace VeroScript::WebSocket
