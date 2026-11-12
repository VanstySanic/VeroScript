// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptHttpMultipart.h"
#include "NetworkProvider.h"
#include <algorithm>
#include <map>

namespace VeroScript::Http
{
	namespace N = Network::Detail;
	namespace
	{
		void ErrorText(Network::FError& E, Network::EError Code, const char* Text) noexcept
		{
			E.Code = Code;
			E.SystemCode = 0;
			try
			{
				E.Message = Text;
			}
			catch (...)
			{
				E.Message.clear();
			}
		}
		std::string_view Trim(std::string_view T)
		{
			while (!T.empty() && (T.front() == ' ' || T.front() == '\t'))
			{
				T.remove_prefix(1);
			}
			while (!T.empty() && (T.back() == ' ' || T.back() == '\t'))
			{
				T.remove_suffix(1);
			}
			return T;
		}
		std::string Lower(std::string_view T)
		{
			std::string S(T);
			for (auto& C : S)
			{
				if (C >= 'A' && C <= 'Z')
				{
					C = static_cast<char>(C - 'A' + 'a');
				}
			}
			return S;
		}
		bool Token(unsigned char C)
		{
			return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') ||
				   std::string_view("!#$%&'*+-.^_`|~").find(static_cast<char>(C)) != std::string_view::npos;
		}
		bool Parameters(std::string_view Input, std::string& Main, std::map<std::string, std::string>& Params)
		{
			const auto Semi = Input.find(';');
			Main = Lower(Trim(Input.substr(0, Semi)));
			if (Main.empty())
			{
				return false;
			}
			std::size_t I = Semi == Input.npos ? Input.size() : Semi;
			while (I < Input.size())
			{
				if (Input[I++] != ';')
				{
					return false;
				}
				while (I < Input.size() && (Input[I] == ' ' || Input[I] == '\t'))
				{
					++I;
				}
				const auto Start = I;
				while (I < Input.size() && Token(static_cast<unsigned char>(Input[I])))
				{
					++I;
				}
				if (I == Start)
				{
					return false;
				}
				auto Name = Lower(Input.substr(Start, I - Start));
				while (I < Input.size() && (Input[I] == ' ' || Input[I] == '\t'))
				{
					++I;
				}
				if (I == Input.size() || Input[I++] != '=')
				{
					return false;
				}
				while (I < Input.size() && (Input[I] == ' ' || Input[I] == '\t'))
				{
					++I;
				}
				std::string Value;
				if (I < Input.size() && Input[I] == '"')
				{
					++I;
					bool Closed = false;
					while (I < Input.size())
					{
						auto C = Input[I++];
						if (C == '"')
						{
							Closed = true;
							break;
						}
						if (C == '\\')
						{
							if (I == Input.size())
							{
								return false;
							}
							C = Input[I++];
						}
						if (static_cast<unsigned char>(C) < 0x20 && C != '\t')
						{
							return false;
						}
						if (C == 0x7f)
						{
							return false;
						}
						Value += C;
					}
					if (!Closed)
					{
						return false;
					}
				}
				else
				{
					const auto Begin = I;
					while (I < Input.size() && Token(static_cast<unsigned char>(Input[I])))
					{
						++I;
					}
					if (I == Begin)
					{
						return false;
					}
					Value = Input.substr(Begin, I - Begin);
				}
				if (!Params.emplace(std::move(Name), std::move(Value)).second)
				{
					return false;
				}
				while (I < Input.size() && (Input[I] == ' ' || Input[I] == '\t'))
				{
					++I;
				}
			}
			return true;
		}
		bool BoundaryValid(std::string_view S)
		{
			if (S.empty() || S.size() > 70 || S.back() == ' ')
			{
				return false;
			}
			for (const unsigned char C : S)
			{
				if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') ||
						std::string_view("'()+_,-./:=? ").find(static_cast<char>(C)) != S.npos))
				{
					return false;
				}
			}
			return true;
		}
	} // namespace
	struct FMultipartReader::FImpl
	{
		enum class EPhase
		{
			Preamble,
			Headers,
			Body,
			PartEnd,
			End,
			Epilogue,
			Done
		};
		enum class EStep
		{
			NeedInput,
			Progress,
			Event
		};
		std::shared_ptr<N::FContext> Context;
		std::shared_ptr<N::FByteLease> Memory;
		FMultipartOptions Options;
		std::string Boundary, FirstMarker, Marker, Buffer;
		std::size_t ChunkBytes = 16384;
		std::size_t Capacity = 0, HeaderScan = 0;
		std::size_t PaddingScan = 0, PaddingPrefix = 0;
		bool PaddingClosing = false;
		std::uint64_t Parts = 0, PartBytes = 0, BodyBytes = 0, HeaderBytes = 0, InputBytes = 0, PreambleBytes = 0,
					  EpilogueBytes = 0;
		Network::FError Failure;
		EPhase Phase = EPhase::Preamble;
		bool bForm = false, bFinal = false, bClosing = false, bFirstPreamble = true;

		void Fail(Network::EError Code, const char* Text)
		{
			if (!Failure)
			{
				ErrorText(Failure, Code, Text);
			}
		}
		void Consume(std::size_t Bytes)
		{
			Buffer.erase(0, Bytes);
			PaddingScan = PaddingPrefix = 0;
		}
		bool Work(FMultipartWork W, std::uint64_t Units)
		{
			try
			{
				if (W.Check && !W.Check(W.UserData, Units))
				{
					Fail(Network::EError::Cancelled, "Multipart read cancelled");
					return false;
				}
			}
			catch (const std::bad_alloc&)
			{
				throw;
			}
			catch (...)
			{
				Fail(Network::EError::Callback, "Multipart work check failed");
				return false;
			}
			return true;
		}
		bool Count(std::uint64_t& Value, std::uint64_t Add, std::uint64_t Limit, const char* Message)
		{
			if (Add > Limit - std::min(Value, Limit))
			{
				Fail(Network::EError::Limit, Message);
				return false;
			}
			Value += Add;
			return true;
		}
		template <class T>
		std::shared_ptr<const T> Own(T Value, std::uint64_t Bytes)
		{
			if (!Context)
			{
				return std::make_shared<const T>(std::move(Value));
			}
			auto Lease = N::ReserveBytes(Context, Bytes, Failure);
			if (!Lease)
			{
				return {};
			}
			auto Any = N::OwnResult(Context, std::move(Value), std::move(Lease));
			const auto* P = static_cast<const T*>(Any->Data);
			return std::shared_ptr<const T>(std::move(Any), P);
		}
		// 1 valid, 0 incomplete, -1 not a delimiter. Closing delimiter may end at EOF.
		int Delimiter(std::size_t Pos, std::size_t Prefix, std::size_t& End, bool& Closing)
		{
			auto I = Pos + Prefix;
			Closing = false;
			if (!Pos && PaddingPrefix == Prefix)
			{
				I = PaddingScan;
				Closing = PaddingClosing;
			}
			else
			{
				if (I == Buffer.size())
				{
					return 0;
				}
				if (Buffer[I] == '-')
				{
					if (I + 1 == Buffer.size())
					{
						return 0;
					}
					if (Buffer[I + 1] != '-')
					{
						return -1;
					}
					Closing = true;
					I += 2;
				}
			}
			while (I < Buffer.size() && (Buffer[I] == ' ' || Buffer[I] == '\t'))
			{
				++I;
			}
			if (!Pos)
			{
				PaddingScan = I;
				PaddingPrefix = Prefix;
				PaddingClosing = Closing;
			}
			if (I == Buffer.size())
			{
				if (Closing && bFinal)
				{
					End = I;
					return 1;
				}
				return 0;
			}
			if (Buffer[I] != '\r')
			{
				return -1;
			}
			if (I + 1 == Buffer.size())
			{
				return 0;
			}
			if (Buffer[I + 1] != '\n')
			{
				return -1;
			}
			End = I + 2;
			return 1;
		}
		bool Head(FMultipartEvent& Event, std::string_view Text, FMultipartWork W)
		{
			FMultipartPartHead Head;
			std::string Name, Value;
			Network::FError E;
			auto Flush = [&]
			{
				if (Name.empty())
				{
					return true;
				}
				const bool Ok = Head.Headers.TryAdd(std::move(Name), std::move(Value), E);
				Name.clear();
				Value.clear();
				return Ok;
			};
			while (!Text.empty())
			{
				const auto End = Text.find("\r\n");
				const auto Line = Text.substr(0, End);
				if (!Work(W, Line.size() + 1))
				{
					return false;
				}
				if (Line.empty())
				{
					Fail(Network::EError::Protocol, "Empty multipart header line");
					return false;
				}
				if (Line.front() == ' ' || Line.front() == '\t')
				{
					if (Name.empty())
					{
						Fail(Network::EError::Protocol, "Multipart header continuation has no field");
						return false;
					}
					Value += ' ';
					Value += Trim(Line);
				}
				else
				{
					if (!Flush())
					{
						Fail(Network::EError::Protocol, "Invalid multipart header");
						return false;
					}
					const auto Colon = Line.find(':');
					if (Colon == Line.npos || !Colon)
					{
						Fail(Network::EError::Protocol, "Invalid multipart header line");
						return false;
					}
					Name = Line.substr(0, Colon);
					Value = Trim(Line.substr(Colon + 1));
				}
				if (End == Text.npos)
				{
					break;
				}
				Text.remove_prefix(End + 2);
			}
			if (!Flush())
			{
				Fail(Network::EError::Protocol, "Invalid multipart header");
				return false;
			}
			const auto Disposition = Head.Headers.GetAll("Content-Disposition");
			if (Disposition.size() > 1 || (bForm && Disposition.size() != 1))
			{
				Fail(Network::EError::Protocol, "Multipart disposition is missing or repeated");
				return false;
			}
			if (!Disposition.empty())
			{
				std::string Main;
				std::map<std::string, std::string> P;
				if (!Parameters(Disposition.front(), Main, P) || (bForm && (Main != "form-data" || !P.count("name"))))
				{
					Fail(Network::EError::Protocol, "Invalid multipart disposition parameters");
					return false;
				}
				if (auto I = P.find("name"); I != P.end())
				{
					Head.Name = I->second;
				}
				if (auto I = P.find("filename"); I != P.end())
				{
					Head.Filename = I->second;
				}
			}
			const auto Types = Head.Headers.GetAll("Content-Type");
			if (Types.size() > 1)
			{
				Fail(Network::EError::Protocol, "Repeated multipart Content-Type");
				return false;
			}
			if (!Types.empty())
			{
				Head.ContentType = Types.front();
			}
			std::uint64_t Retained = Head.Name.capacity() + Head.ContentType.capacity() + sizeof(FMultipartPartHead) +
									 Head.Headers.Fields().capacity() * sizeof(std::pair<std::string, std::string>);
			if (Head.Filename)
			{
				Retained += Head.Filename->capacity();
			}
			for (const auto& H : Head.Headers.Fields())
			{
				Retained += H.first.capacity() + H.second.capacity();
			}
			Event.Head = Own(std::move(Head), Retained);
			return bool(Event.Head);
		}
		EStep Data(FMultipartEvent& Out, std::size_t CountBytes)
		{
			CountBytes = std::min(CountBytes, ChunkBytes);
			if (!Count(PartBytes, CountBytes, Options.MaxPartBodyBytes, "Multipart part byte limit exceeded") ||
				!Count(BodyBytes, CountBytes, Options.MaxTotalBodyBytes, "Multipart total body byte limit exceeded"))
			{
				return EStep::NeedInput;
			}
			FMultipartEvent Event;
			Event.Type = EMultipartEvent::Data;
			Event.PartIndex = Parts - 1;
			Network::FBytes Data(Buffer.begin(), Buffer.begin() + CountBytes);
			const auto DataCapacity = Data.capacity();
			Event.Data = Own(std::move(Data), DataCapacity);
			if (!Event.Data)
			{
				return EStep::NeedInput;
			}
			Consume(CountBytes);
			Out = std::move(Event);
			return EStep::Event;
		}
		EStep Process(FMultipartEvent& Out, FMultipartWork W)
		{
			if (Phase == EPhase::Done)
			{
				return EStep::NeedInput;
			}
			if (Phase == EPhase::End)
			{
				FMultipartEvent E;
				E.Type = EMultipartEvent::End;
				Out = std::move(E);
				Phase = EPhase::Epilogue;
				return EStep::Event;
			}
			if (Phase == EPhase::PartEnd)
			{
				FMultipartEvent E;
				E.Type = EMultipartEvent::PartEnd;
				E.PartIndex = Parts - 1;
				Out = std::move(E);
				Phase = bClosing ? EPhase::End : EPhase::Headers;
				HeaderScan = 0;
				return EStep::Event;
			}
			if (Phase == EPhase::Epilogue)
			{
				if (!Count(EpilogueBytes, Buffer.size(), Options.MaxEpilogueBytes, "Multipart epilogue limit exceeded"))
				{
					return EStep::NeedInput;
				}
				Buffer.clear();
				if (bFinal)
				{
					Phase = EPhase::Done;
				}
				return EStep::NeedInput;
			}
			if (Phase == EPhase::Headers)
			{
				auto End = Buffer.find("\r\n\r\n", HeaderScan);
				std::size_t Separator = 4;
				if (Buffer.substr(0, 2) == "\r\n")
				{
					End = 0;
					Separator = 2;
				}
				if (End == Buffer.npos)
				{
					HeaderScan = Buffer.size() > 3 ? Buffer.size() - 3 : 0;
					if (Buffer.size() > Options.MaxHeaderBytes)
					{
						Fail(Network::EError::Limit, "Multipart part header limit exceeded");
					}
					return EStep::NeedInput;
				}
				if (End + Separator > Options.MaxHeaderBytes || !Count(HeaderBytes,
																	End + Separator,
																	Options.MaxTotalHeaderBytes,
																	"Multipart total header limit exceeded"))
				{
					if (!Failure)
					{
						Fail(Network::EError::Limit, "Multipart part header limit exceeded");
					}
					return EStep::NeedInput;
				}
				if (Parts >= Options.MaxParts)
				{
					Fail(Network::EError::Limit, "Multipart part count exceeded");
					return EStep::NeedInput;
				}
				FMultipartEvent Event;
				Event.Type = EMultipartEvent::PartBegin;
				Event.PartIndex = Parts;
				if (!Head(Event, std::string_view(Buffer).substr(0, End), W))
				{
					return EStep::NeedInput;
				}
				++Parts;
				PartBytes = 0;
				Consume(End + Separator);
				Phase = EPhase::Body;
				Out = std::move(Event);
				return EStep::Event;
			}
			const bool Preamble = Phase == EPhase::Preamble;
			std::size_t Search = 0;
			for (;;)
			{
				std::size_t Pos = Buffer.find(Marker, Search), Prefix = Marker.size();
				if (Preamble && bFirstPreamble &&
					Buffer.compare(0,
						std::min(Buffer.size(), Boundary.size() + 2),
						FirstMarker,
						0,
						std::min(Buffer.size(), Boundary.size() + 2)) == 0)
				{
					if (Buffer.size() < Boundary.size() + 2)
					{
						return EStep::NeedInput;
					}
					Pos = 0;
					Prefix = Boundary.size() + 2;
				}
				if (Pos == Buffer.npos)
				{
					const auto Keep = Marker.size() - 1;
					if (Buffer.size() <= Keep)
					{
						return EStep::NeedInput;
					}
					const auto Safe = Buffer.size() - Keep;
					if (!Preamble)
					{
						return Data(Out, Safe);
					}
					if (!Count(PreambleBytes, Safe, Options.MaxPreambleBytes, "Multipart preamble limit exceeded"))
					{
						return EStep::NeedInput;
					}
					Consume(Safe);
					bFirstPreamble = false;
					return EStep::Progress;
				}
				std::size_t End = 0;
				bool Closing = false;
				const int Match = Delimiter(Pos, Prefix, End, Closing);
				if (Match < 0)
				{
					bFirstPreamble = false;
					Search = Pos + 1;
					continue;
				}
				if (Pos)
				{
					if (!Preamble)
					{
						return Data(Out, Pos);
					}
					if (!Count(PreambleBytes, Pos, Options.MaxPreambleBytes, "Multipart preamble limit exceeded"))
					{
						return EStep::NeedInput;
					}
					Consume(Pos);
					bFirstPreamble = false;
					return EStep::Progress;
				}
				if (!Match)
				{
					return EStep::NeedInput;
				}
				Consume(End);
				bClosing = Closing;
				HeaderScan = 0;
				Phase = Preamble ? (Closing ? EPhase::End : EPhase::Headers) : EPhase::PartEnd;
				return EStep::Progress;
			}
		}
	};
	bool FMultipartReader::TryCreate(std::string_view ContentType, std::shared_ptr<FMultipartReader>& Out,
		Network::FError& Error, const FMultipartOptions& Options, Network::FContext* Context)
	{
		Error = {};
		try
		{
			std::string Main;
			std::map<std::string, std::string> P;
			if (ContentType.size() > 65536 || !Parameters(ContentType, Main, P) || Main.substr(0, 10) != "multipart/" ||
				Main.size() == 10 ||
				!std::all_of(Main.begin() + 10,
					Main.end(),
					[](unsigned char C)
					{
						return Token(C);
					}) ||
				!P.count("boundary") || !BoundaryValid(P["boundary"]) || !Options.MaxParts ||
				Options.MaxHeaderBytes < 2 || Options.MaxHeaderBytes > SIZE_MAX - 4)
			{
				ErrorText(Error, Network::EError::InvalidArgument, "Invalid multipart media type, boundary or limits");
				return false;
			}
			auto Value = std::make_unique<FImpl>();
			Value->Options = Options;
			Value->bForm = Main == "multipart/form-data";
			Value->Boundary = P["boundary"];
			Value->FirstMarker = "--" + Value->Boundary;
			Value->Marker = "\r\n" + Value->FirstMarker;
			if (Context)
			{
				Value->Context = N::FAccess::Context(*Context);
				Value->ChunkBytes =
					static_cast<std::size_t>(std::min<std::uint64_t>(16384, N::Options(Value->Context).MaxBufferBytes));
				if (Options.MaxHeaderBytes > N::Options(Value->Context).MaxBufferBytes)
				{
					ErrorText(Error, Network::EError::Limit, "Multipart header limit exceeds context buffer limit");
					return false;
				}
			}
			Value->Capacity = static_cast<std::size_t>(
				std::max<std::uint64_t>(Options.MaxHeaderBytes + 4, Value->ChunkBytes + Value->Marker.size() + 4));
			if (Value->Context)
			{
				Value->Memory = N::ReserveBytes(Value->Context, Value->Capacity, Error);
				if (!Value->Memory)
				{
					return false;
				}
			}
			Value->Buffer.reserve(Value->Capacity);
			if (Value->Memory && !Value->Memory->TryResize(Value->Buffer.capacity()))
			{
				ErrorText(Error, Network::EError::Limit, "Multipart buffer allocation exceeds context budget");
				return false;
			}
			Out = std::shared_ptr<FMultipartReader>(new FMultipartReader(std::move(Value)));
			return true;
		}
		catch (const std::bad_alloc&)
		{
			ErrorText(Error, Network::EError::Allocation, "Multipart allocation failed");
		}
		catch (const std::length_error&)
		{
			ErrorText(Error, Network::EError::Limit, "Multipart allocation exceeds platform range");
		}
		catch (...)
		{
			ErrorText(Error, Network::EError::Internal, "Multipart initialization failed");
		}
		return false;
	}

	FMultipartReader::~FMultipartReader() = default;

	FMultipartFeedResult FMultipartReader::Feed(
		std::string_view Bytes, bool Final, FMultipartEvent& Out, FMultipartWork Work)
	{
		auto& P = *Impl;
		FMultipartFeedResult R;
		try
		{
			if (P.Context && N::IsClosed(P.Context))
			{
				P.Fail(Network::EError::Closed, "Multipart context is closed");
			}
			if (P.bFinal && !Bytes.empty())
			{
				P.Fail(Network::EError::State, "Multipart input already ended");
			}
			while (!P.Failure)
			{
				if (!P.Work(Work, 1))
				{
					break;
				}
				const auto Step = P.Process(Out, Work);
				if (P.Failure)
				{
					break;
				}
				if (Step == FImpl::EStep::Event)
				{
					R.State = EMultipartReadState::EventReady;
					return R;
				}
				if (Step == FImpl::EStep::Progress)
				{
					continue;
				}
				if (P.Phase == FImpl::EPhase::Done)
				{
					R.State = EMultipartReadState::Done;
					return R;
				}
				if (R.Consumed < Bytes.size())
				{
					const auto Copy =
						std::min({Bytes.size() - R.Consumed, P.Capacity - P.Buffer.size(), std::size_t(16384)});
					if (!Copy)
					{
						P.Fail(Network::EError::Limit, "Multipart pending delimiter/header limit exceeded");
						break;
					}
					if (!P.Work(Work, Copy) ||
						!P.Count(P.InputBytes, Copy, P.Options.MaxInputBytes, "Multipart input byte limit exceeded"))
					{
						break;
					}
					P.Buffer.append(Bytes.substr(R.Consumed, Copy));
					R.Consumed += Copy;
					if (Final && R.Consumed == Bytes.size())
					{
						P.bFinal = true;
					}
					continue;
				}
				if (Final && !P.bFinal)
				{
					P.bFinal = true;
					continue;
				}
				if (P.bFinal && P.Phase != FImpl::EPhase::Epilogue)
				{
					P.Fail(Network::EError::Protocol, "Multipart input is truncated");
				}
				break;
			}
		}
		catch (const std::bad_alloc&)
		{
			P.Fail(Network::EError::Allocation, "Multipart allocation failed");
		}
		catch (const std::length_error&)
		{
			P.Fail(Network::EError::Limit, "Multipart allocation exceeds platform range");
		}
		catch (...)
		{
			P.Fail(Network::EError::Callback, "Multipart work check failed");
		}
		if (P.Failure)
		{
			R.State = EMultipartReadState::Failed;
		}
		else if (P.Phase == FImpl::EPhase::Done)
		{
			R.State = EMultipartReadState::Done;
		}
		return R;
	}

	const Network::FError& FMultipartReader::Error() const noexcept
	{
		return Impl->Failure;
	}

	FMultipartReader::FMultipartReader(std::unique_ptr<FImpl> P) : Impl(std::move(P))
	{
	}
} // namespace VeroScript::Http
