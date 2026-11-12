// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptHttpSse.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace VeroScript;

#define CHECK(X) \
	do \
	{ \
		if (!(X)) \
		{ \
			throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #X); \
		} \
	} \
	while (false)

static void ChunkTests()
{
	const std::string Source =
	    "\xef\xbb\xbf: heartbeat\r\nid: one\rretry: 12\nevent: update\r\n"
	    "data: hello \xf0\x9f\x98\x80\rdata: second\r\n\r\ndata:\n\nid\n\ndata: last\n\ndata: discarded";
	for (std::size_t Width = 1; Width <= Source.size(); ++Width)
	{
		Http::FSseReader Reader;
		std::vector<Http::FSseEvent> Events;
		std::size_t Offset = 0;
		while (Offset < Source.size())
		{
			const auto Count = std::min(Width, Source.size() - Offset);
			Http::FSseEvent Event;
			const auto Read =
			    Reader.Feed(std::string_view(Source).substr(Offset, Count), Offset + Count == Source.size(), Event);
			CHECK(Read.State != Http::ESseReadState::Failed && Read.Consumed <= Count);
			Offset += Read.Consumed;
			if (Read.State == Http::ESseReadState::EventReady)
			{
				Events.push_back(std::move(Event));
			}
			CHECK(Read.Consumed != 0 || Read.State == Http::ESseReadState::Done);
		}
		Http::FSseEvent Event;
		CHECK(Reader.Feed({}, true, Event).State == Http::ESseReadState::Done);
		CHECK(Events.size() == 3);
		CHECK(
		    Events[0].Data == "hello \xf0\x9f\x98\x80\nsecond" && Events[0].Event == "update" && Events[0].Id == "one");
		CHECK(Events[1].Data.empty() && Events[1].Event == "message" && Events[1].Id == "one");
		CHECK(Events[2].Data == "last" && Events[2].Id.empty());
		CHECK(Reader.LastEventId().empty() && Reader.RetryMilliseconds() == 12);
	}
}

static void BoundaryTests()
{
	Http::FSseReader Reader;
	Http::FSseEvent Event;
	CHECK(
	    Reader.Feed("id: kept\n\nretry: 3\nretry: +2\nretry: -1\nretry: 18446744073709551616\n", false, Event).State ==
	    Http::ESseReadState::NeedInput);
	CHECK(Reader.LastEventId() == "kept" && Reader.RetryMilliseconds() == 3);
	const std::string InvalidId = std::string("id: bad\0id\n", 11) + "data: \xed\xa0\x80\n\n";
	CHECK(Reader.Feed(InvalidId, false, Event).State == Http::ESseReadState::EventReady);
	CHECK(Event.Id == "kept" && Event.Data == "\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd");
	Reader.Reset();
	CHECK(Reader.LastEventId().empty() && !Reader.RetryMilliseconds());
	CHECK(Reader.Feed("data: \xe2\x82\n\n", true, Event).State == Http::ESseReadState::EventReady);
	CHECK(Event.Data == "\xef\xbf\xbd");
	CHECK(Reader.Feed({}, true, Event).State == Http::ESseReadState::Done);
	CHECK(Reader.Feed("x", true, Event).State == Http::ESseReadState::Failed);
	CHECK(Reader.Error().Code == Network::EError::State);
	Http::FSseReader Small({8, 3});
	CHECK(Small.Feed("data: abc\n\n", true, Event).State == Http::ESseReadState::Failed);
	CHECK(Small.Error().Code == Network::EError::Limit);
	Http::FSseReader Three({32, 3});
	CHECK(Three.Feed("data: abc\n\n", true, Event).State == Http::ESseReadState::EventReady && Event.Data == "abc");
	Three.Reset();
	CHECK(Three.Feed("data: a\ndata: b\n\n", true, Event).State == Http::ESseReadState::EventReady &&
	    Event.Data == "a\nb");
	Three.Reset();
	CHECK(Three.Feed("data: abc\ndata:\n\n", true, Event).State == Http::ESseReadState::Failed);
	CHECK(Three.Feed({}, true, Event).State == Http::ESseReadState::Failed);
	Reader.Reset();
	CHECK(
	    Reader.Feed("id: uncommitted", true, Event).State == Http::ESseReadState::Done && Reader.LastEventId().empty());
}

static void EncodeTests()
{
	Network::FError Error;
	std::string Text = "unchanged";
	Http::FSseEvent Event{"first\nlast\n", "custom", "42"};
	CHECK(Http::TryEncodeSseEvent(Event, Text, Error));
	Http::FSseReader Reader;
	Http::FSseEvent Parsed;
	CHECK(Reader.Feed(Text, true, Parsed).State == Http::ESseReadState::EventReady);
	CHECK(Parsed.Data == Event.Data && Parsed.Event == Event.Event && Parsed.Id == Event.Id);
	Event.Id = "a\nb";
	const auto Original = Text;
	CHECK(
	    !Http::TryEncodeSseEvent(Event, Text, Error) && Error.Code == Network::EError::InvalidArgument && Text == Original);
	Event.Id.clear();
	CHECK(!Http::TryEncodeSseEvent(Event, Text, Error, 4) && Error.Code == Network::EError::Limit && Text == Original);
	CHECK(Http::TryEncodeSseComment("keep\nalive", Text, Error) && Text == ": keep\n: alive\n");
	CHECK(!Http::TryEncodeSseComment("\xff", Text, Error) && Error.Code == Network::EError::InvalidArgument);
}

int main()
{
	try
	{
		ChunkTests();
		BoundaryTests();
		EncodeTests();
		std::cout << "SSE chunk, framing, UTF-8 and encoding tests passed\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
