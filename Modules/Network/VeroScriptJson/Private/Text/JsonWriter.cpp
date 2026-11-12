// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "JsonInternal.h"
#include <numeric>

namespace VeroScript::Json::Detail
{
	std::string Quote(std::string_view Text)
	{
		ValidateUtf8(Text);
		std::string Out = "\"";
		static constexpr char Hex[] = "0123456789abcdef";
		for (unsigned char C : Text)
		{
			if (C == '"' || C == '\\')
			{
				Out += '\\';
				Out += static_cast<char>(C);
			}
			else if (C < 0x20)
			{
				Out += "\\u00";
				Out += Hex[C >> 4];
				Out += Hex[C & 15];
			}
			else
			{
				Out += static_cast<char>(C);
			}
		}
		Out += '"';
		return Out;
	}

	void Write(
		const FJsonValue& Value, std::string& Out, const FWriteOptions& Options, FBudget& Budget, std::uint64_t Depth)
	{
		Budget.Check();
		if (Depth >= Options.Limits.Depth || Options.Indent > 16)
		{
			Fail(EError::Limit, "Output depth or indentation limit exceeded");
		}
		auto Append = [&](std::string_view Text)
		{
			if (Text.size() >
				Options.Limits.OutputBytes - std::min<std::uint64_t>(Out.size(), Options.Limits.OutputBytes))
			{
				Fail(EError::Limit, "Output byte limit exceeded");
			}
			Budget.Check(Text.size());
			Out += Text;
		};
		auto Indent = [&](std::uint64_t Level)
		{
			if (Options.bPretty)
			{
				Append("\n");
				Append(std::string(static_cast<std::size_t>(Level * Options.Indent), ' '));
			}
		};
		const auto& N = FAccess::Get(Value);
		if (N.Kind == EKind::Null)
		{
			Append("null");
		}
		else if (N.Kind == EKind::Boolean)
		{
			Append(N.Boolean ? "true" : "false");
		}
		else if (N.Kind == EKind::String)
		{
			Budget.Check(N.Text.size());
			Append(Quote(N.Text));
		}
		else if (N.Kind == EKind::Number)
		{
			Append(N.Text);
		}
		else
		{
			bool bObject = N.Kind == EKind::Object;
			auto Count = bObject ? N.Object.size() : N.Array.size();
			std::vector<std::size_t> Order;
			if (bObject && Options.bSortKeys)
			{
				Order.resize(Count);
				std::iota(Order.begin(), Order.end(), std::size_t{0});
				std::sort(Order.begin(),
					Order.end(),
					[&](auto A, auto B)
					{
						Budget.Check(1 + std::min(N.Object[A].first.size(), N.Object[B].first.size()));
						return N.Object[A].first < N.Object[B].first;
					});
			}
			Append(bObject ? "{" : "[");
			for (std::size_t I = 0; I < Count; ++I)
			{
				if (I)
				{
					Append(",");
				}
				Indent(Depth + 1);
				auto Index = Order.empty() ? I : Order[I];
				if (bObject)
				{
					Append(Quote(N.Object[Index].first));
					Append(Options.bPretty ? ": " : ":");
				}
				Write(bObject ? N.Object[Index].second : N.Array[Index], Out, Options, Budget, Depth + 1);
			}
			if (Count)
			{
				Indent(Depth);
			}
			Append(bObject ? "}" : "]");
		}
	}
} // namespace VeroScript::Json::Detail

namespace VeroScript::Json
{
	using namespace Detail;

	bool TryWrite(const FJsonValue& Value, std::string& Out, FError& Error, const FWriteOptions& Options, FWork Work)
	{
		return Guard(Error,
			[&]
			{
				FBudget Budget(Options.Limits, Work);
				std::string Text;
				Write(Value, Text, Options, Budget);
				Out = std::move(Text);
			});
	}

	struct FWriter::FState
	{
		struct FFrame
		{
			bool bObject = false;
			bool bHasKey = false;
			std::uint64_t Count = 0;
			std::unordered_set<std::string> Keys;
		};

		EWriteState Flush(FError& Error, FWork Work)
		{
			if (Status == EWriteState::Failed)
			{
				CopyError(Error, Failure);
				return Status;
			}
			if (Status == EWriteState::Done)
			{
				return Status;
			}
			bool Success = Guard(Error,
				[&]
				{
					if (!Sink)
					{
						Fail(EError::Sink, "Missing output sink");
					}
					while (Offset < Pending.size())
					{
						if (!CheckWork(Work, 1))
						{
							Fail(EError::Cancelled, "Output cancelled");
						}
						auto Remaining = std::string_view(Pending).substr(Offset);
						FSinkResult Result;
						try
						{
							Result = Sink(Remaining);
						}
						catch (const std::bad_alloc&)
						{
							throw;
						}
						catch (...)
						{
							Fail(EError::Sink, "Output sink threw an exception");
						}
						if (Result.Bytes > Remaining.size() ||
							(Result.State != ESinkState::Progress && Result.State != ESinkState::NeedOutput))
						{
							Fail(EError::Sink, "Output sink failed or returned an invalid byte count");
						}
						Offset += Result.Bytes;
						if (Result.State == ESinkState::NeedOutput)
						{
							Status = EWriteState::NeedOutput;
							return;
						}
						if (!Result.Bytes)
						{
							Fail(EError::Sink, "Output sink made no progress");
						}
					}
					Pending.clear();
					Offset = 0;
					Status = bFinishing ? EWriteState::Done : EWriteState::Active;
				});
			if (!Success)
			{
				Status = EWriteState::Failed;
				CopyError(Failure, Error);
			}
			return Status;
		}

		FSink Sink;
		FWriteOptions Options;
		std::vector<FFrame> Frames;
		std::string Pending;
		std::size_t Offset = 0;
		std::uint64_t Total = 0, WorkUsed = 0, Nodes = 0;
		bool bRoot = false;
		bool bFinishing = false;
		EWriteState Status = EWriteState::Active;
		FError Failure;
	};

	FWriter::FWriter(FSink Sink, FWriteOptions Options) : State(std::make_unique<FState>())
	{
		State->Sink = std::move(Sink);
		State->Options = std::move(Options);
	}
	FWriter::~FWriter() = default;
	FWriter::FWriter(FWriter&&) noexcept = default;
	FWriter& FWriter::operator=(FWriter&&) noexcept = default;

	EWriteState FWriter::Write(const FEvent& Event, bool& bAccepted, FError& Error, FWork Work)
	{
		bAccepted = false;
		auto& S = *State;
		Error = {};
		if (S.Status == EWriteState::NeedOutput || S.Status == EWriteState::Failed)
		{
			return S.Flush(Error, Work);
		}
		bool Success = Guard(Error,
			[&]
			{
				if (S.bFinishing || S.Status == EWriteState::Done)
				{
					Fail(EError::State, "Event after finish");
				}
				if (S.Options.bSortKeys)
				{
					Fail(EError::State, "Streaming keys must be supplied in the desired order");
				}
				if (static_cast<unsigned>(Event.Kind) > static_cast<unsigned>(EEvent::String))
				{
					Fail(EError::State, "Unknown JSON event kind");
				}
				if (Event.Text.size() > S.Options.Limits.TokenBytes || S.Options.Indent > 16)
				{
					Fail(EError::Limit, "Event token or indentation exceeds limit");
				}
				FBudget Budget(S.Options.Limits, Work);
				Budget.Used = S.WorkUsed;
				Budget.Check(1 + Event.Text.size());
				auto& Frames = S.Frames;
				std::string Encoded;
				bool bEnd = Event.Kind == EEvent::EndArray || Event.Kind == EEvent::EndObject;
				bool bKey = Event.Kind == EEvent::Key;
				if (!bEnd && !bKey && Frames.size() + 1 > std::min<std::uint64_t>(S.Options.Limits.Depth, 256))
				{
					Fail(EError::Limit, "Output nesting limit exceeded");
				}
				auto Indent = [&](std::size_t Depth)
				{
					if (S.Options.bPretty)
					{
						Encoded += '\n';
						Encoded.append(Depth * S.Options.Indent, ' ');
					}
				};
				if (bEnd)
				{
					if (Frames.empty() || Frames.back().bObject != (Event.Kind == EEvent::EndObject) ||
						Frames.back().bHasKey)
					{
						Fail(EError::State, "Mismatched or incomplete container");
					}
					bool bHasMembers = Frames.back().Count != 0;
					Frames.pop_back();
					if (bHasMembers)
					{
						Indent(Frames.size());
					}
					Encoded += Event.Kind == EEvent::EndObject ? '}' : ']';
				}
				else if (bKey)
				{
					if (Frames.empty() || !Frames.back().bObject || Frames.back().bHasKey)
					{
						Fail(EError::State, "Key outside an object key position");
					}
					auto& Frame = Frames.back();
					if (!Frame.Keys.insert(Event.Text).second)
					{
						Fail(EError::DuplicateKey, "Duplicate output member");
					}
					if (Frame.Count)
					{
						Encoded += ',';
					}
					Indent(Frames.size());
					Encoded += Quote(Event.Text);
					Encoded += S.Options.bPretty ? ": " : ":";
					Frame.bHasKey = true;
				}
				else
				{
					if (Frames.empty())
					{
						if (S.bRoot)
						{
							Fail(EError::State, "Multiple output roots");
						}
					}
					else
					{
						auto& Frame = Frames.back();
						if (Frame.bObject && !Frame.bHasKey)
						{
							Fail(EError::State, "Missing object key");
						}
						if (!Frame.bObject)
						{
							if (Frame.Count)
							{
								Encoded += ',';
							}
							Indent(Frames.size());
						}
						++Frame.Count;
						Frame.bHasKey = false;
					}
					if (Event.Kind == EEvent::BeginArray || Event.Kind == EEvent::BeginObject)
					{
						if (Frames.size() >= std::min<std::uint64_t>(S.Options.Limits.Depth, 256))
						{
							Fail(EError::Limit, "Output nesting limit exceeded");
						}
						Frames.push_back({Event.Kind == EEvent::BeginObject, false, 0, {}});
						Encoded += Event.Kind == EEvent::BeginObject ? '{' : '[';
					}
					else if (Event.Kind == EEvent::Number)
					{
						Decimal(Event.Text, S.Options.Limits);
						Encoded += Event.Text;
					}
					else if (Event.Kind == EEvent::String)
					{
						Encoded += Quote(Event.Text);
					}
					else
					{
						Encoded += Event.Kind == EEvent::Null ? "null" : Event.Boolean ? "true" : "false";
					}
				}
				if (Encoded.size() > S.Options.Limits.OutputBytes - std::min(S.Total, S.Options.Limits.OutputBytes) ||
					(!bEnd && !bKey && S.Nodes >= S.Options.Limits.Nodes))
				{
					Fail(EError::Limit, "Output byte or node limit exceeded");
				}
				S.Total += Encoded.size();
				S.Nodes += !bEnd && !bKey;
				S.WorkUsed = Budget.Used;
				S.Pending = std::move(Encoded);
				S.bRoot = S.bRoot || (!bEnd && !bKey);
				bAccepted = true;
			});
		if (!Success)
		{
			S.Status = EWriteState::Failed;
			CopyError(S.Failure, Error);
			return S.Status;
		}
		return S.Flush(Error, Work);
	}

	EWriteState FWriter::Resume(FError& Error, FWork Work)
	{
		Error = {};
		return State->Flush(Error, Work);
	}

	EWriteState FWriter::Finish(FError& Error, FWork Work)
	{
		auto& S = *State;
		Error = {};
		if (S.Status == EWriteState::Done || S.Status == EWriteState::Failed)
		{
			return S.Flush(Error, Work);
		}
		if (!S.bRoot || !S.Frames.empty())
		{
			SetError(Error, EError::State, "Incomplete output document");
			S.Status = EWriteState::Failed;
			CopyError(S.Failure, Error);
			return S.Status;
		}
		S.bFinishing = true;
		return S.Flush(Error, Work);
	}
} // namespace VeroScript::Json
