// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBoost.h"
#include "VeroScriptHttpMultipart.h"
#include <iostream>
#include <stdexcept>

namespace N = VeroScript::Network;
namespace H = VeroScript::Http;
#define CHECK(X) \
	do \
	{ \
		if (!(X)) \
		{ \
			throw std::runtime_error(std::string("Line ") + std::to_string(__LINE__) + ": " + #X); \
		} \
	} \
	while (false)
static std::shared_ptr<H::FMultipartReader> Reader(std::string_view Type, H::FMultipartOptions Options = {})
{
	std::shared_ptr<H::FMultipartReader> R;
	N::FError E;
	CHECK(H::FMultipartReader::TryCreate(Type, R, E, Options));
	return R;
}
static std::vector<H::FMultipartEvent> Read(
    std::shared_ptr<H::FMultipartReader> R, std::string_view Bytes, std::size_t Width, bool Success = true)
{
	std::vector<H::FMultipartEvent> Events;
	std::size_t Offset = 0;
	for (std::size_t Iteration = 0; Iteration < Bytes.size() * 4 + 32; ++Iteration)
	{
		const auto Count = std::min(Width, Bytes.size() - Offset);
		H::FMultipartEvent Event;
		const auto F = R->Feed(Bytes.substr(Offset, Count), Offset + Count == Bytes.size(), Event);
		CHECK(F.Consumed <= Count);
		Offset += F.Consumed;
		if (F.State == H::EMultipartReadState::EventReady)
		{
			Events.push_back(std::move(Event));
		}
		else if (F.State == H::EMultipartReadState::Failed)
		{
			CHECK(!Success && R->Error());
			return Events;
		}
		else if (F.State == H::EMultipartReadState::Done)
		{
			CHECK(Success && !R->Error() && Offset == Bytes.size());
			return Events;
		}
		else
		{
			CHECK(F.Consumed == Count);
		}
	}
	throw std::runtime_error("Multipart reader failed to make progress");
}
static void Tests()
{
	const std::string Type = "Multipart/Form-Data; charset=utf-8; boundary=\"one:two\"";
	const std::string Body = std::string("a\0b", 3) + "\r\n--one:twoX\r\nz";
	const std::string Input =
	    "preamble\r\n--one:two \t\r\nContent-Disposition: form-data; name=\"field\"\r\n\r\nhello\r\n"
	    "--one:two\r\nContent-Disposition: form-data; name=\"file\"; filename=\"a\\\"b.bin\"\r\nContent-Type: "
	    "application/octet-stream;\r\n\tcharset=utf-8\r\nX-Test: first\r\nX-Test: second\r\n\r\n" +
	    Body + "\r\n--one:two-- \t\r\nepilogue";
	for (const auto Width :
	    {std::size_t(1), std::size_t(2), std::size_t(3), std::size_t(7), std::size_t(31), Input.size()})
	{
		auto Events = Read(Reader(Type), Input, Width);
		std::vector<std::string> Parts;
		std::size_t Begins = 0, Ends = 0, Closed = 0;
		std::shared_ptr<const H::FMultipartPartHead> File;
		for (const auto& E : Events)
		{
			if (E.Type == H::EMultipartEvent::PartBegin)
			{
				CHECK(E.Head && E.PartIndex == Begins++);
				Parts.emplace_back();
				if (E.PartIndex == 0)
				{
					CHECK(E.Head->Name == "field" && !E.Head->Filename);
				}
				else
				{
					File = E.Head;
				}
			}
			else if (E.Type == H::EMultipartEvent::Data)
			{
				CHECK(E.Data && !E.Data->empty());
				Parts[E.PartIndex].append(E.Data->begin(), E.Data->end());
			}
			else if (E.Type == H::EMultipartEvent::PartEnd)
			{
				++Ends;
			}
			else
			{
				++Closed;
			}
		}
		CHECK(Begins == 2 && Ends == 2 && Closed == 1 && Parts[0] == "hello" && Parts[1] == Body);
		CHECK(File && File->Name == "file" && File->Filename == "a\"b.bin" &&
		    File->ContentType == "application/octet-stream; charset=utf-8");
		CHECK(File->Headers.GetAll("x-test") == std::vector<std::string>({"first", "second"}));
	}
	CHECK(Read(Reader("multipart/form-data; boundary=b"), "--b--", 1).size() == 1);
	// Padding can cross many tiny feeds without rescanning its entire accumulated prefix.
	const std::string Padding(16000, ' ');
	const auto Padded =
	    Read(Reader("multipart/mixed; boundary=b"), "--b" + Padding + "\r\n\r\nx\r\n--b--" + Padding + "\r\n", 1);
	CHECK(Padded.size() == 4 && Padded[1].Data && Padded[1].Data->size() == 1);
	const auto Empty = Read(Reader("multipart/mixed; boundary=b"), "--b\r\n\r\n\r\n--b--\r\n", 1);
	CHECK(Empty.size() == 3 && Empty.front().Head->Name.empty());
	// Every truncation before a complete closing delimiter is a protocol failure.
	const std::string Small = "--b\r\nContent-Disposition: form-data; name=x\r\n\r\ny\r\n--b--";
	for (std::size_t I = 0; I < Small.size(); ++I)
	{
		Read(Reader("multipart/form-data; boundary=b"), std::string_view(Small).substr(0, I), 3, false);
	}
	for (const auto& Bad : {"multipart/form-data",
	         "text/plain; boundary=b",
	         "multipart/form-data; boundary=\"\"",
	         "multipart/form-data; boundary=x; BOUNDARY=y",
	         "multipart/form-data; boundary=\"bad;value\""})
	{
		auto Existing = Reader(Type), Out = Existing;
		N::FError E;
		CHECK(!H::FMultipartReader::TryCreate(Bad, Out, E) && E.Code == N::EError::InvalidArgument && Out == Existing);
	}
	for (const auto& Bad : {"--b\r\nContent-Type: text/plain\r\n\r\nx\r\n--b--",
	         "--b\r\nContent-Disposition: form-data; name=x; name=y\r\n\r\nx\r\n--b--"})
	{
		Read(Reader("multipart/form-data; boundary=b"), Bad, 1, false);
	}
	H::FMultipartOptions O;
	O.MaxPartBodyBytes = 0;
	auto Limited = Reader("multipart/form-data; boundary=b", O);
	Read(Limited, Small, 1, false);
	CHECK(Limited->Error().Code == N::EError::Limit);
	O = {};
	O.MaxHeaderBytes = 8;
	Limited = Reader("multipart/form-data; boundary=b", O);
	Read(Limited, Small, 1, false);
	CHECK(Limited->Error().Code == N::EError::Limit);
	O = {};
	O.MaxParts = 1;
	Limited = Reader(Type, O);
	Read(Limited, Input, 1, false);
	CHECK(Limited->Error().Code == N::EError::Limit);
	O = {};
	O.MaxPreambleBytes = 1;
	Limited = Reader(Type, O);
	Read(Limited, Input, 1, false);
	CHECK(Limited->Error().Code == N::EError::Limit);
	O = {};
	O.MaxEpilogueBytes = 1;
	Limited = Reader(Type, O);
	Read(Limited, Input, 1, false);
	CHECK(Limited->Error().Code == N::EError::Limit);
	auto Cancel = Reader(Type);
	H::FMultipartEvent Unchanged;
	Unchanged.PartIndex = 99;
	const auto C = Cancel->Feed(Input,
	    true,
	    Unchanged,
	    {[](void*, std::uint64_t)
	{
		return false;
	},
	        nullptr});
	CHECK(C.State == H::EMultipartReadState::Failed && Cancel->Error().Code == N::EError::Cancelled &&
	    Unchanged.PartIndex == 99);
	auto Throw = Reader(Type);
	CHECK(Throw->Feed(Input,
	               true,
	               Unchanged,
	               {[](void*, std::uint64_t) -> bool
	{
		throw std::runtime_error("work");
	},
	                   nullptr})
	            .State == H::EMultipartReadState::Failed &&
	    Throw->Error().Code == N::EError::Callback);
	// Context retention follows externally kept immutable payloads; shutdown doesn't invalidate old data.
	N::FContext Context(VeroScript::Boost::CreateNetworkService());
	N::FError E;
	std::shared_ptr<H::FMultipartReader> Managed;
	CHECK(H::FMultipartReader::TryCreate(Type, Managed, E, {}, &Context));
	auto Events = Read(Managed, Input, 5);
	Managed.reset();
	CHECK(Context.MemoryUsage().RetainedBytes > 0);
	Context.Shutdown();
	CHECK(Events.front().Head->Name == "field");
	Events.clear();
	CHECK(Context.MemoryUsage().RetainedBytes == 0);
	{
		N::FContextOptions Limits;
		Limits.MaxBufferBytes = 16384;
		Limits.MaxRetainedBytes = 40000;
		N::FContext LimitedContext(VeroScript::Boost::CreateNetworkService(), Limits);
		H::FMultipartOptions SmallHeader;
		SmallHeader.MaxHeaderBytes = 1024;
		CHECK(H::FMultipartReader::TryCreate("multipart/mixed; boundary=b", Managed, E, SmallHeader, &LimitedContext));
		const std::string Large = "--b\r\n\r\n" + std::string(100000, 'x') + "\r\n--b--\r\n";
		auto Retained = Read(Managed, Large, 16384, false);
		CHECK(Managed->Error().Code == N::EError::Limit);
		CHECK(!Retained.empty() && LimitedContext.MemoryUsage().RetainedBytes <= Limits.MaxRetainedBytes);
		LimitedContext.Shutdown();
		H::FMultipartEvent Ignored;
		CHECK(Managed->Feed("more", true, Ignored).State == H::EMultipartReadState::Failed &&
		    Managed->Error().Code == N::EError::Limit);
		Managed.reset();
		Retained.clear();
		CHECK(LimitedContext.MemoryUsage().RetainedBytes == 0);
	}
}
int main()
{
	try
	{
		Tests();
	}
	catch (const std::exception& E)
	{
		std::cerr << E.what() << '\n';
		return 1;
	}
	return 0;
}
