// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include <VeroScriptJson.h>
#if TEST_HTTP
#include <VeroScriptHttpJson.h>
#endif
#if TEST_WEBSOCKET
#include <VeroScriptWebSocketJson.h>
#endif

int main()
{
	using namespace VeroScript;
	Json::FJsonValue Value, Parsed;
	Json::FError Error;
	if (!Json::TryParse("{\"count\":9223372036854775807}", Value, Error))
	{
		return 1;
	}
	#if TEST_HTTP
	Http::FRequest Request;
	Http::FJsonBodyError BodyError;
	Request.UploadFactory = [](std::uint32_t, Network::FError&)
	{
		return Http::FBodySource{};
	};
	Json::FWriteOptions Small;
	Small.Limits.OutputBytes = 1;
	if (Http::TrySetJsonBody(Request, Value, BodyError, Small) || !BodyError.Json || !Request.UploadFactory)
	{
		return 12;
	}
	if (!Http::TrySetJsonBody(Request, Value, BodyError) ||
		Request.Headers.GetAll("Content-Type") != std::vector<std::string>{"application/json"} || Request.UploadFactory)
	{
		return 2;
	}
	Http::FResponse Response;
	Response.Body = Request.Body;
	Response.Headers = Request.Headers;
	Response.Status = 503;
	Response.bComplete = true;
	if (!Http::TryParseJsonBody(Response, Parsed, BodyError) || Response.Status != 503)
	{
		return 3;
	}
	const auto Saved = Parsed;
	Response.Body = {'{'};
	if (Http::TryParseJsonBody(Response, Parsed, BodyError) || !BodyError.Json || BodyError.Http ||
		Response.Status != 503)
	{
		return 4;
	}
	if (!Response.Headers.TrySet("Content-Type", "text/plain", BodyError.Http) ||
		Http::TryParseJsonBody(Response, Parsed, BodyError) || BodyError.Http.Code != Network::EError::Protocol)
	{
		return 5;
	}
	for (const char* Type : {"application/json", "APPLICATION/PROBLEM+JSON; charset=utf-8", "model/gltf+json"})
	{
		if (!Http::IsJsonMediaType(Type))
		{
			return 6;
		}
	}
	for (const char* Type : {"text/plain", "invalid+json", "x/+json", "application/bad?+json", "a\x01/b+json"})
	{
		if (Http::IsJsonMediaType(Type))
		{
			return 7;
		}
	}
	Http::FSseEvent Event;
	Event.Data = "[1,2]";
	if (!Http::TryParseJsonEvent(Event, Parsed, Error) || Parsed.Kind() != Json::EKind::Array)
	{
		return 8;
	}
	#endif
	#if TEST_WEBSOCKET
	WebSocket::FMessage Message;
	if (!WebSocket::TryMakeJsonMessage(Value, Message, Error) || !Message.bText || !Message.bFinal || !Message.bFirst ||
		!WebSocket::TryParseJsonMessage(Message, Parsed, Error))
	{
		return 9;
	}
	Message.bFirst = false;
	if (WebSocket::TryParseJsonMessage(Message, Parsed, Error) || Error.Code != Json::EError::State)
	{
		return 10;
	}
	Message.bFirst = true;
	Message.bText = false;
	if (WebSocket::TryParseJsonMessage(Message, Parsed, Error) || Error.Code != Json::EError::Type)
	{
		return 11;
	}
	#endif
	return 0;
}
