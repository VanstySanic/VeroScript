// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "JsonInternal.h"

namespace VeroScript::Json
{
	using namespace Detail;

	static unsigned Hex(char C)
	{
		if (C >= '0' && C <= '9')
		{
			return static_cast<unsigned>(C - '0');
		}
		if (C >= 'a' && C <= 'f')
		{
			return static_cast<unsigned>(C - 'a' + 10);
		}
		if (C >= 'A' && C <= 'F')
		{
			return static_cast<unsigned>(C - 'A' + 10);
		}
		Fail(EError::Syntax, "Invalid Unicode escape");
	}

	static std::string DecodeString(std::string_view Token)
	{
		std::string Out;
		for (std::size_t I = 1; I + 1 < Token.size(); ++I)
		{
			auto C = static_cast<unsigned char>(Token[I]);
			if (C < 0x20)
			{
				Fail(EError::Syntax, "Unescaped control character");
			}
			if (C != '\\')
			{
				Out += static_cast<char>(C);
				continue;
			}
			if (++I + 1 >= Token.size())
			{
				Fail(EError::Syntax, "Incomplete string escape");
			}
			char Escape = Token[I];
			if (Escape == 'u')
			{
				auto ReadScalar = [&]()
				{
					if (Token.size() - I < 6)
					{
						Fail(EError::Syntax, "Incomplete Unicode escape");
					}
					std::uint32_t V = 0;
					for (unsigned J = 0; J < 4; ++J)
					{
						V = (V << 4) | Hex(Token[++I]);
					}
					return V;
				};
				auto Scalar = ReadScalar();
				if (Scalar >= 0xd800 && Scalar <= 0xdbff)
				{
					if (Token.size() - I < 8 || Token[I + 1] != '\\' || Token[I + 2] != 'u')
					{
						Fail(EError::InvalidUtf8, "Unpaired high surrogate");
					}
					I += 2;
					auto Low = ReadScalar();
					if (Low < 0xdc00 || Low > 0xdfff)
					{
						Fail(EError::InvalidUtf8, "Invalid low surrogate");
					}
					Scalar = 0x10000 + ((Scalar - 0xd800) << 10) + Low - 0xdc00;
				}
				else if (Scalar >= 0xdc00 && Scalar <= 0xdfff)
				{
					Fail(EError::InvalidUtf8, "Unpaired low surrogate");
				}
				if (Scalar < 0x80)
				{
					Out += static_cast<char>(Scalar);
				}
				else
				{
					unsigned Count = Scalar < 0x800 ? 1 : Scalar < 0x10000 ? 2 : 3;
					Out += static_cast<char>((Count == 1 ? 0xc0 : Count == 2 ? 0xe0 : 0xf0) | (Scalar >> (6 * Count)));
					while (Count)
					{
						--Count;
						Out += static_cast<char>(0x80 | ((Scalar >> (6 * Count)) & 0x3f));
					}
				}
			}
			else
			{
				switch (Escape)
				{
				case '"':
				case '\\':
				case '/':
					{
						Out += Escape;
						break;
					}
				case 'b':
					{
						Out += '\b';
						break;
					}
				case 'f':
					{
						Out += '\f';
						break;
					}
				case 'n':
					{
						Out += '\n';
						break;
					}
				case 'r':
					{
						Out += '\r';
						break;
					}
				case 't':
					{
						Out += '\t';
						break;
					}
				default:
					{
						Fail(EError::Syntax, "Invalid string escape");
					}
				}
			}
		}
		ValidateUtf8(Out);
		return Out;
	}

	struct FReader::FState
	{
		struct FFrame
		{
			bool bObject = false;
			unsigned Expect = 0;
			std::unordered_set<std::string> Keys;
		};

		explicit FState(FParseOptions InOptions) : Options(std::move(InOptions))
		{
		}

		void Consume(unsigned char C)
		{
			if (Offset >= Options.Limits.InputBytes || WorkUsed >= Options.Limits.WorkUnits)
			{
				Fail(EError::Limit, "Input or work limit exceeded");
			}
			++Offset;
			++WorkUsed;
			if (C == '\r' || (C == '\n' && !bPreviousCr))
			{
				++Line;
				Column = 1;
			}
			else if (C != '\n' && (C & 0xc0) != 0x80)
			{
				++Column;
			}
			bPreviousCr = C == '\r';
		}

		void ReportWork(FWork Work)
		{
			auto Units = WorkUsed - ReportedWork;
			ReportedWork = WorkUsed;
			if (Units && !CheckWork(Work, Units))
			{
				Fail(EError::Cancelled, "Parsing cancelled");
			}
		}

		void CompleteValue()
		{
			if (Frames.size() + 1 > std::min<std::uint64_t>(Options.Limits.Depth, 256))
			{
				Fail(EError::Limit, "Nesting depth limit exceeded");
			}
			if (++Nodes > Options.Limits.Nodes)
			{
				Fail(EError::Limit, "Node limit exceeded");
			}
			if (Frames.empty())
			{
				bRoot = true;
			}
			else
			{
				Frames.back().Expect = Frames.back().bObject ? 3 : 1;
			}
		}

		void FinishToken(FEvent& Event)
		{
			Event = {};
			if (TokenKind == 3 || TokenKind == 4)
			{
				Event.Text = DecodeString(Token);
				Event.Kind = TokenKind == 4 ? EEvent::Key : EEvent::String;
				if (TokenKind == 4)
				{
					auto& Frame = Frames.back();
					if (Options.DuplicateKeys == EDuplicateKeys::Reject && !Frame.Keys.insert(Event.Text).second)
					{
						Fail(EError::DuplicateKey, "Duplicate object member");
					}
					Frame.Expect = 1;
				}
			}
			else if (TokenKind == 1)
			{
				Decimal(Token, Options.Limits);
				Event.Kind = EEvent::Number;
				Event.Text = Token;
			}
			else
			{
				if (Token != "null" && Token != "true" && Token != "false")
				{
					Fail(EError::Syntax, "Invalid literal");
				}
				Event.Kind = Token == "null" ? EEvent::Null : EEvent::Boolean;
				Event.Boolean = Token == "true";
			}
			if (TokenKind != 4)
			{
				CompleteValue();
			}
			Token.clear();
			TokenKind = 0;
		}

		FParseOptions Options;
		std::vector<FFrame> Frames;
		std::string Token;
		unsigned TokenKind = 0;
		unsigned Bom = 0;
		bool bEscape = false;
		bool bRoot = false;
		bool bDone = false;
		bool bFailed = false;
		bool bPreviousCr = false;
		std::uint64_t Offset = 0, Line = 1, Column = 1, Nodes = 0, WorkUsed = 0, ReportedWork = 0;
		FError Failure;
	};

	FReader::FReader(FParseOptions Options) : State(std::make_unique<FState>(std::move(Options)))
	{
	}
	FReader::~FReader() = default;
	FReader::FReader(FReader&&) noexcept = default;
	FReader& FReader::operator=(FReader&&) noexcept = default;

	FFeedResult FReader::Feed(std::string_view Input, bool bFinal, FEvent& Event, FError& Error, FWork Work)
	{
		auto& S = *State;
		FFeedResult Result;
		S.ReportedWork = S.WorkUsed;
		if (S.bFailed)
		{
			CopyError(Error, S.Failure);
			Result.State = EReadState::Failed;
			return Result;
		}
		bool Success = Guard(Error,
			[&]
			{
				if (static_cast<unsigned>(S.Options.DuplicateKeys) > static_cast<unsigned>(EDuplicateKeys::ReportAll))
				{
					Fail(EError::State, "Unknown duplicate-key policy");
				}
				if (!CheckWork(Work, 0))
				{
					Fail(EError::Cancelled, "Parsing cancelled");
				}
				if (S.bDone)
				{
					if (!Input.empty())
					{
						Fail(EError::State, "Input after finished document");
					}
					Result.State = EReadState::Done;
					return;
				}
				while (Result.Consumed < Input.size())
				{
					auto C = static_cast<unsigned char>(Input[Result.Consumed]);
					auto Consume = [&]
					{
						S.Consume(C);
						++Result.Consumed;
						if (S.WorkUsed - S.ReportedWork >= 256)
						{
							S.ReportWork(Work);
						}
					};
					if (S.Bom || (S.Offset == 0 && C == 0xef && S.Options.bAllowLeadingBom))
					{
						unsigned char Expected[] = {0xef, 0xbb, 0xbf};
						if (C != Expected[S.Bom])
						{
							Fail(EError::Syntax, "Invalid leading BOM");
						}
						Consume();
						S.Bom = (S.Bom + 1) % 3;
						continue;
					}
					if (S.TokenKind)
					{
						if (S.TokenKind <= 2)
						{
							bool Continues = S.TokenKind == 1 ? (C >= '0' && C <= '9') || C == '-' || C == '+' ||
																	C == '.' || C == 'e' || C == 'E'
															  : (C >= 'a' && C <= 'z');
							if (!Continues)
							{
								S.FinishToken(Event);
								Result.State = EReadState::EventReady;
								return;
							}
						}
						if (S.Token.size() >= S.Options.Limits.TokenBytes ||
							(S.TokenKind == 1 && S.Token.size() >= S.Options.Limits.NumberBytes))
						{
							Fail(EError::Limit, "Token limit exceeded");
						}
						S.Token += static_cast<char>(C);
						Consume();
						if (S.TokenKind >= 3)
						{
							if (C == '"' && !S.bEscape)
							{
								S.FinishToken(Event);
								Result.State = EReadState::EventReady;
								return;
							}
							S.bEscape = C == '\\' && !S.bEscape;
						}
						continue;
					}
					if (C == ' ' || C == '\t' || C == '\r' || C == '\n')
					{
						Consume();
						continue;
					}
					if (S.Frames.empty() && S.bRoot)
					{
						Fail(EError::Syntax, "Trailing content after JSON value");
					}
					if (!S.Frames.empty())
					{
						auto& F = S.Frames.back();
						if ((!F.bObject && (F.Expect == 0 || F.Expect == 1) && C == ']') ||
							(F.bObject && (F.Expect == 0 || F.Expect == 3) && C == '}'))
						{
							Event = {};
							Event.Kind = F.bObject ? EEvent::EndObject : EEvent::EndArray;
							S.Frames.pop_back();
							Consume();
							Result.State = EReadState::EventReady;
							return;
						}
						if ((!F.bObject && F.Expect == 1) || (F.bObject && F.Expect == 3))
						{
							if (C != ',')
							{
								Fail(EError::Syntax, "Expected comma or container end");
							}
							F.Expect = F.bObject ? 4 : 2;
							Consume();
							continue;
						}
						if (F.bObject && F.Expect == 1)
						{
							if (C != ':')
							{
								Fail(EError::Syntax, "Expected colon after key");
							}
							F.Expect = 2;
							Consume();
							continue;
						}
						if (F.bObject && (F.Expect == 0 || F.Expect == 4))
						{
							if (C != '"')
							{
								Fail(EError::Syntax, "Expected object key");
							}
							S.TokenKind = 4;
						}
					}
					if (!S.TokenKind && (C == '[' || C == '{'))
					{
						if (S.Frames.size() >= std::min<std::uint64_t>(S.Options.Limits.Depth, 256))
						{
							Fail(EError::Limit, "Nesting depth limit exceeded");
						}
						S.CompleteValue();
						S.Frames.push_back({C == '{', 0, {}});
						Event = {};
						Event.Kind = C == '[' ? EEvent::BeginArray : EEvent::BeginObject;
						Consume();
						Result.State = EReadState::EventReady;
						return;
					}
					if (!S.TokenKind)
					{
						S.TokenKind = C == '"'							   ? 3
									  : C == '-' || (C >= '0' && C <= '9') ? 1
									  : C == 't' || C == 'f' || C == 'n'   ? 2
																		   : 0;
					}
					if (!S.TokenKind)
					{
						Fail(EError::Syntax, "Expected JSON value");
					}
					S.Token.assign(1, static_cast<char>(C));
					S.bEscape = false;
					Consume();
				}
				if (bFinal)
				{
					if (S.TokenKind == 1 || S.TokenKind == 2)
					{
						S.FinishToken(Event);
						Result.State = EReadState::EventReady;
						return;
					}
					if (S.TokenKind || !S.Frames.empty() || !S.bRoot || S.Bom)
					{
						Fail(EError::Syntax, "Incomplete JSON document");
					}
					S.bDone = true;
					Result.State = EReadState::Done;
				}
			});
		FError WorkError;
		if (!Guard(WorkError,
				[&]
				{
					S.ReportWork(Work);
				}))
		{
			Error = std::move(WorkError);
			Success = false;
		}
		if (!Success)
		{
			Error.bHasSource = true;
			Error.ByteOffset = S.Offset;
			Error.Line = S.Line;
			Error.Column = S.Column;
			CopyError(S.Failure, Error);
			S.bFailed = true;
			Result.State = EReadState::Failed;
		}
		return Result;
	}

	FFeedResult FReader::Visit(std::string_view Input, bool bFinal, const std::function<bool(const FEvent&)>& Visitor,
		FError& Error, FWork Work)
	{
		FFeedResult Total;
		for (;;)
		{
			FEvent Event;
			auto Result = Feed(Input.substr(Total.Consumed), bFinal, Event, Error, Work);
			Total.Consumed += Result.Consumed;
			Total.State = Result.State;
			if (Result.State != EReadState::EventReady)
			{
				return Total;
			}
			bool Continue = false;
			if (!Guard(Error,
					[&]
					{
						if (!Visitor)
						{
							Fail(EError::Callback, "Missing SAX visitor");
						}
						try
						{
							Continue = Visitor(Event);
						}
						catch (const std::bad_alloc&)
						{
							throw;
						}
						catch (...)
						{
							Fail(EError::Callback, "SAX visitor threw an exception");
						}
					}))
			{
				CopyError(State->Failure, Error);
				State->bFailed = true;
				Total.State = EReadState::Failed;
				return Total;
			}
			if (!Continue)
			{
				return Total;
			}
		}
	}

	void FReader::Reset()
	{
		auto Fresh = std::make_unique<FState>(State->Options);
		State = std::move(Fresh);
	}

	bool TryParse(std::string_view Text, FJsonValue& Out, FError& Error, const FParseOptions& Options, FWork Work)
	{
		FError ParseError;
		bool Success = Guard(Error,
			[&]
			{
				if (Options.DuplicateKeys == EDuplicateKeys::ReportAll)
				{
					Fail(EError::State, "ReportAll is an event-reader policy");
				}
				struct FBuildFrame
				{
					FNode Node;
					std::string Key;
					std::unordered_map<std::string, std::size_t> Keys;
				};
				std::vector<FBuildFrame> Stack;
				FJsonValue Root;
				auto Accept = [&](FJsonValue Value)
				{
					if (Stack.empty())
					{
						Root = std::move(Value);
						return;
					}
					auto& Frame = Stack.back();
					if (Frame.Node.Kind == EKind::Array)
					{
						Frame.Node.Array.push_back(std::move(Value));
					}
					else
					{
						auto [It, Inserted] = Frame.Keys.emplace(Frame.Key, Frame.Node.Object.size());
						if (Inserted)
						{
							Frame.Node.Object.emplace_back(std::move(Frame.Key), std::move(Value));
						}
						else if (Options.DuplicateKeys == EDuplicateKeys::KeepLast)
						{
							Frame.Node.Object[It->second].second = std::move(Value);
						}
					}
				};
				FReader Reader(Options);
				std::size_t Offset = 0;
				for (;;)
				{
					FEvent Event;
					auto Result = Reader.Feed(Text.substr(Offset), true, Event, ParseError, Work);
					Offset += Result.Consumed;
					if (Result.State == EReadState::Failed)
					{
						Fail(ParseError.Code, "JSON parse failed");
					}
					if (Result.State == EReadState::Done)
					{
						Out = std::move(Root);
						return;
					}
					if (Event.Kind == EEvent::BeginArray || Event.Kind == EEvent::BeginObject)
					{
						Stack.emplace_back();
						Stack.back().Node.Kind = Event.Kind == EEvent::BeginArray ? EKind::Array : EKind::Object;
					}
					else if (Event.Kind == EEvent::EndArray || Event.Kind == EEvent::EndObject)
					{
						if (Event.Kind == EEvent::EndObject)
						{
							Stack.back().Node.ObjectIndex =
								std::make_shared<const std::unordered_map<std::string, std::size_t>>(
									std::move(Stack.back().Keys));
						}
						auto Value = FAccess::Make(std::move(Stack.back().Node));
						Stack.pop_back();
						Accept(std::move(Value));
					}
					else if (Event.Kind == EEvent::Key)
					{
						Stack.back().Key = std::move(Event.Text);
					}
					else
					{
						FNode Node;
						Node.Kind = Event.Kind == EEvent::Null		? EKind::Null
									: Event.Kind == EEvent::Boolean ? EKind::Boolean
									: Event.Kind == EEvent::Number	? EKind::Number
																	: EKind::String;
						Node.Boolean = Event.Boolean;
						Node.Text = std::move(Event.Text);
						if (Node.Kind == EKind::Number)
						{
							Node.Decimal = Decimal(Node.Text, Options.Limits);
						}
						Accept(FAccess::Make(std::move(Node)));
					}
				}
			});
		if (!Success && ParseError)
		{
			Error = std::move(ParseError);
		}
		return Success;
	}
} // namespace VeroScript::Json
