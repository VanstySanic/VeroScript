// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptToolingTestVm.h"
#include "VeroScriptLocale.h"
#include "VeroScriptCompile.h"
#include "VeroScriptLexer.h"
#include "VeroScriptTooling.h"
#include "VeroScriptProtocolTestStreams.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
	#if defined(_WIN32)
	const std::string DriveFixturePathPrefix = "C:/veroscript-tests/";
	#else
	const std::string DriveFixturePathPrefix = "/C:/veroscript-tests/";
	#endif

	bool Expect(const bool Condition, const char* Message)
	{
		if (!Condition)
		{
			std::cerr << "VeroScriptLspStdioTests: " << Message << '\n';
		}
		return Condition;
	}

	std::string Frame(const std::string& Body)
	{
		return "Content-Length: " + std::to_string(Body.size()) + "\r\n\r\n" + Body;
	}

	std::string Request(
	    const int Id,
	    const std::string& Method,
	    const std::string& Params = "{}")
	{
		return Frame(
		    "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(Id) + ",\"method\":\"" + Method + "\",\"params\":" + Params + "}");
	}

	constexpr const char* SnippetInitializeParams =
	    "{\"capabilities\":{\"textDocument\":{\"completion\":{"
	    "\"completionItem\":{\"snippetSupport\":true}}}}}";

	std::string Notification(const std::string& Method, const std::string& Params = "{}")
	{
		return Frame(
		    "{\"jsonrpc\":\"2.0\",\"method\":\"" + Method + "\",\"params\":" + Params + "}");
	}

	int RunTranscript(std::istringstream& Input, std::ostream& Output,
	    const VeroScript::Tooling::FLspStdioRunOptions& Options = {})
	{
		auto Text = Input.str();
		const auto Exit = Notification("exit");
		// Finite transcripts expecting responses end at EOF after shutdown. An
		// explicit exit abandons pending analysis; live-client tests cover that path.
		if (Text.size() >= Exit.size() && Text.compare(Text.size() - Exit.size(), Exit.size(), Exit) == 0 && Text.find("\"method\":\"shutdown\"") != std::string::npos)
		{
			Text.resize(Text.size() - Exit.size());
		}
		std::istringstream DrainedInput(std::move(Text));
		return VeroScript::Test::FLspStdio::Run(DrainedInput, Output, Options);
	}

	std::string JsonQuote(const std::string_view Value)
	{
		std::string Result = "\"";
		for (const char Character : Value)
		{
			switch (Character)
			{
			case '"':
				{
					Result += "\\\"";
					break;
				}
			case '\\':
				{
					Result += "\\\\";
					break;
				}
			case '\n':
				{
					Result += "\\n";
					break;
				}
			case '\r':
				{
					Result += "\\r";
					break;
				}
			case '\t':
				{
					Result += "\\t";
					break;
				}
			default:
				{
					Result.push_back(Character);
					break;
				}
			}
		}
		return Result + '"';
	}

	bool Contains(const std::string_view Text, const std::string_view Needle)
	{
		return Text.find(Needle) != std::string::npos;
	}

	std::size_t CountOccurrences(const std::string_view Text, const std::string_view Needle)
	{
		std::size_t Count = 0;
		for (std::size_t Offset = 0;
		    !Needle.empty() && (Offset = Text.find(Needle, Offset)) != std::string_view::npos;
		    Offset += Needle.size())
		{
			++Count;
		}
		return Count;
	}

	std::string_view ResponseForId(const std::string_view Output, const int Id)
	{
		const std::string Needle = "\"id\":" + std::to_string(Id) + ",\"jsonrpc\"";
		const std::size_t IdOffset = Output.find(Needle);
		if (IdOffset == std::string_view::npos)
		{
			return {};
		}
		const std::size_t HeaderEnd = Output.rfind("\r\n\r\n", IdOffset);
		if (HeaderEnd == std::string_view::npos)
		{
			return {};
		}
		const std::size_t BodyStart = HeaderEnd + 4;
		const std::size_t NextFrame = Output.find("Content-Length:", IdOffset);
		return Output.substr(
		    BodyStart,
		    NextFrame == std::string_view::npos ? Output.size() - BodyStart : NextFrame - BodyStart);
	}

	std::string_view LastDiagnosticsForUri(const std::string_view Output, const std::string_view Uri)
	{
		const std::string UriField = "\"uri\":" + JsonQuote(Uri);
		constexpr std::string_view Method = "\"method\":\"textDocument/publishDiagnostics\"";
		std::string_view Result;
		for (auto Offset = Output.find(Method); Offset != std::string_view::npos;
		    Offset = Output.find(Method, Offset + Method.size()))
		{
			const auto Header = Output.rfind("\r\n\r\n", Offset);
			if (Header == std::string_view::npos)
			{
				continue;
			}
			const auto End = Output.find("Content-Length:", Offset);
			const auto Body = Output.substr(Header + 4,
			    End == std::string_view::npos ? Output.size() - Header - 4 : End - Header - 4);
			if (Contains(Body, UriField))
			{
				Result = Body;
			}
		}
		return Result;
	}

	std::string_view CompletionItemForLabel(
	    const std::string_view Response,
	    const std::string_view Label,
	    std::size_t Occurrence = 0)
	{
		const std::string Needle = "\"label\":\"" + std::string(Label) + "\"";
		const std::size_t ItemsOffset = Response.find("\"items\":[");
		if (ItemsOffset == std::string_view::npos)
		{
			return {};
		}
		int Depth = 0;
		bool bInString = false;
		bool bEscaped = false;
		std::size_t Begin = std::string_view::npos;
		for (std::size_t Offset = ItemsOffset + 9; Offset < Response.size(); ++Offset)
		{
			const char Character = Response[Offset];
			if (bInString)
			{
				if (bEscaped)
				{
					bEscaped = false;
				}
				else if (Character == '\\')
				{
					bEscaped = true;
				}
				else if (Character == '"')
				{
					bInString = false;
				}
				continue;
			}
			if (Character == '"')
			{
				bInString = true;
			}
			else if (Character == '{')
			{
				if (Depth == 0)
				{
					Begin = Offset;
				}
				++Depth;
			}
			else if (Character == '}' && Depth > 0 && --Depth == 0)
			{
				const std::string_view Item = Response.substr(Begin, Offset - Begin + 1);
				if (Item.find(Needle) != std::string_view::npos)
				{
					if (Occurrence == 0)
					{
						return Item;
					}
					--Occurrence;
				}
				Begin = std::string_view::npos;
			}
		}
		return {};
	}

	std::string_view JsonStringField(
	    const std::string_view Object,
	    const std::string_view Field)
	{
		const std::string Needle = "\"" + std::string(Field) + "\":\"";
		const std::size_t Begin = Object.find(Needle);
		if (Begin == std::string_view::npos)
		{
			return {};
		}
		const std::size_t ValueStart = Begin + Needle.size();
		const std::size_t End = Object.find('"', ValueStart);
		return End == std::string_view::npos
		    ? std::string_view{}
		    : Object.substr(ValueStart, End - ValueStart);
	}

	// A real request/response conversation: later document edits must not cancel
	// earlier asynchronous semantic requests before the client receives them.
	class FLspConversationBuffer final : public std::streambuf
	{
	public:
		void Append(const std::string& Text)
		{
			const std::lock_guard<std::mutex> Lock(Mutex);
			Input.insert(Input.end(), Text.begin(), Text.end());
			Changed.notify_all();
		}
		void Close()
		{
			const std::lock_guard<std::mutex> Lock(Mutex);
			bClosed = true;
			Changed.notify_all();
		}
		bool WaitForResponse(const int Id)
		{
			std::unique_lock<std::mutex> Lock(Mutex);
			return Changed.wait_for(Lock, std::chrono::seconds(10), [&]()
			{
				const auto Response = ResponseForId(Output, Id);
				if (Response.empty())
				{
					return false;
				}
				const std::size_t Body = static_cast<std::size_t>(Response.data() - Output.data());
				const std::size_t Header = Output.rfind("Content-Length:", Body);
				if (Header == std::string::npos)
				{
					return false;
				}
				const std::size_t Length = std::strtoul(Output.c_str() + Header + 15, nullptr, 10);
				return Output.size() - Body >= Length;
			});
		}
		std::string Str()
		{
			const std::lock_guard<std::mutex> Lock(Mutex);
			return Output;
		}

	protected:
		//~ Begin std::streambuf Interface
		int_type underflow() override
		{
			std::unique_lock<std::mutex> Lock(Mutex);
			Changed.wait(Lock, [&]()
			{
				return bClosed || !Input.empty();
			});
			return Input.empty() ? traits_type::eof() : traits_type::to_int_type(Input.front());
		}
		int_type uflow() override
		{
			std::unique_lock<std::mutex> Lock(Mutex);
			Changed.wait(Lock, [&]()
			{
				return bClosed || !Input.empty();
			});
			if (Input.empty())
			{
				return traits_type::eof();
			}
			const char Value = Input.front();
			Input.pop_front();
			return traits_type::to_int_type(Value);
		}
		std::streamsize xsputn(const char* Text, const std::streamsize Count) override
		{
			const std::lock_guard<std::mutex> Lock(Mutex);
			Output.append(Text, static_cast<std::size_t>(Count));
			Changed.notify_all();
			return Count;
		}
		int_type overflow(const int_type Character) override
		{
			if (traits_type::eq_int_type(Character, traits_type::eof()))
			{
				return traits_type::not_eof(Character);
			}
			const char Value = traits_type::to_char_type(Character);
			xsputn(&Value, 1);
			return Character;
		}
		//~ End std::streambuf Interface

	private:
		std::mutex Mutex;
		std::condition_variable Changed;
		std::deque<char> Input;
		std::string Output;
		bool bClosed = false;
	};

	struct FDecodedSemanticToken
	{
		int Line = 0;
		int Character = 0;
		int Length = 0;
		int Type = 0;
		int Modifiers = 0;
	};

	std::vector<int> SemanticTokenData(const std::string_view Response)
	{
		std::vector<int> Values;
		const std::size_t Data = Response.find("\"data\":[");
		if (Data == std::string_view::npos)
		{
			return {};
		}
		std::size_t Cursor = Data + std::string_view("\"data\":[").size();
		while (Cursor < Response.size() && Response[Cursor] != ']')
		{
			while (Cursor < Response.size() && (Response[Cursor] == ',' || Response[Cursor] == ' '))
			{
				++Cursor;
			}
			if (Cursor >= Response.size() || Response[Cursor] < '0' || Response[Cursor] > '9')
			{
				break;
			}
			int Value = 0;
			while (Cursor < Response.size() && Response[Cursor] >= '0' && Response[Cursor] <= '9')
			{
				Value = Value * 10 + (Response[Cursor++] - '0');
			}
			Values.push_back(Value);
		}

		return Values;
	}

	std::vector<FDecodedSemanticToken> DecodeSemanticTokens(const std::vector<int>& Values)
	{
		std::vector<FDecodedSemanticToken> Tokens;
		int Line = 0;
		int Character = 0;
		for (std::size_t Index = 0; Index + 4 < Values.size(); Index += 5)
		{
			Line += Values[Index];
			Character = Values[Index] == 0 ? Character + Values[Index + 1] : Values[Index + 1];
			Tokens.push_back(FDecodedSemanticToken{
			    Line, Character, Values[Index + 2], Values[Index + 3], Values[Index + 4]});
		}
		return Tokens;
	}

	std::vector<FDecodedSemanticToken> DecodeSemanticTokens(const std::string_view Response)
	{
		return DecodeSemanticTokens(SemanticTokenData(Response));
	}

	// Apply the server's single prefix/suffix delta to the actual encoded stream.
	bool ApplySemanticTokenDelta(const std::string_view Response, std::vector<int>& Data)
	{
		if (Contains(Response, "\"edits\":[]"))
		{
			return true;
		}
		if (!Contains(Response, "\"edits\":[{"))
		{
			return false;
		}
		const auto NumberField = [&](const std::string_view Name)
		{
			const std::size_t Offset = Response.find(std::string("\"") + std::string(Name) + "\":");
			return Offset == std::string_view::npos ? -1
			                                        : std::atoi(std::string(Response.substr(Offset + Name.size() + 3)).c_str());
		};
		const int Start = NumberField("start");
		const int Count = NumberField("deleteCount");
		if (Start < 0 || Count < 0 || static_cast<std::size_t>(Start) > Data.size() || static_cast<std::size_t>(Count) > Data.size() - Start)
		{
			return false;
		}
		const std::vector<int> Inserted = SemanticTokenData(Response);
		Data.erase(Data.begin() + Start, Data.begin() + Start + Count);
		Data.insert(Data.begin() + Start, Inserted.begin(), Inserted.end());
		return Data.size() % 5 == 0;
	}

	bool HasSemanticToken(
	    const std::vector<FDecodedSemanticToken>& Tokens,
	    const int Line,
	    const int Character,
	    const int Length,
	    const int Type,
	    const int Modifiers = -1)
	{
		return std::any_of(
		    Tokens.begin(),
		    Tokens.end(),
		    [=](const FDecodedSemanticToken& Token)
		{
			return Token.Line == Line && Token.Character == Character && Token.Length == Length && Token.Type == Type && (Modifiers < 0 || Token.Modifiers == Modifiers);
		});
	}

	bool SemanticTokensDoNotOverlap(const std::vector<FDecodedSemanticToken>& Tokens)
	{
		for (std::size_t Index = 1; Index < Tokens.size(); ++Index)
		{
			const FDecodedSemanticToken& Previous = Tokens[Index - 1];
			const FDecodedSemanticToken& Current = Tokens[Index];
			if (Current.Line < Previous.Line || (Current.Line == Previous.Line && Current.Character < Previous.Character + Previous.Length))
			{
				return false;
			}
		}
		return true;
	}

	std::string OpenInvalidDocumentScript(const std::string_view Locale = {})
	{
		constexpr const char* Uri = "file:///C:/veroscript-tests/lsp-test.vs";
		std::ostringstream Input;
		Input << Request(
		    1,
		    "initialize",
		    Locale.empty() ? "{}" : "{\"locale\":\"" + std::string(Locale) + "\"}");
		Input << Notification("initialized");
		Input << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + Uri + "\",\"languageId\":\"veroscript\",\"version\":1," + "\"text\":\"void main() { print(\\\"😀中\\\"); missing(); }\\n\"}}");
		Input << Request(2, "shutdown");
		Input << Notification("exit");
		return Input.str();
	}
}

int main()
{
	using namespace VeroScript::Tooling;
	{
		Tests::FInputTieCheckingBuffer Buffer(
		    "Content-Length: 1\r\n\r\n{" + Request(1, "initialize") + Request(2, "shutdown") + Notification("exit"));
		Tests::FOutputThreadCheckingBuffer OutputBuffer;
		std::istream In(&Buffer);
		std::ostream Out(&OutputBuffer);
		Buffer.Attach(In);
		In.tie(&Out);
		const auto PreviousErrorTie = std::cerr.tie(&Out);
		const auto Exit = VeroScript::Test::FLspStdio::Run(In, Out);
		const bool bErrorTieRestored = std::cerr.tie() == &Out;
		std::cerr.tie(PreviousErrorTie);
		if (!Expect(Exit == 0 && !Buffer.ObservedTieDuringRead() && !OutputBuffer.FlushedByCaller() &&
		            In.tie() == &Out && bErrorTieRestored && Contains(OutputBuffer.str(), "-32700") &&
		            Contains(ResponseForId(OutputBuffer.str(), 1), "capabilities"),
		        "protocol input and error logging cannot implicitly flush output; stream ties are restored"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		using namespace VeroScript;
		struct FGetter : IAnnotationProcessor
		{
			void Process(const FAnnotationContext& C, FAnnotationOutput& O) override
			{
				O.SubmitMethod(C.Owner->Id, EMemberAccess::Public, "int Read(){return value;}");
			}
		};
		auto Registry = std::make_shared<FAnnotationProcessorRegistry>();
		std::string Error;
		if (!Registry->Register(
		        {"@Getter", "lsp.test", "1", "", AnnotationTargetMask(EAnnotationTarget::Field), 1, 0,
		            []
		{
			return std::make_unique<FGetter>();
		}},
		        Error))
		{
			return EXIT_FAILURE;
		}

		Registry->Freeze();
		FLspStdioRunOptions Options;
		Options.AnnotationProcessing.Registry = Registry;
		const std::string Uri = "file:///C:/veroscript-tests/lsp-annotation.vs";
		const std::string Source = "struct A { @Getter int value=4; };\nvoid main(){ A a=A(); print(a.Read()); }\n";
		std::ostringstream Input;
		Input << Request(1, "initialize") << Notification("initialized")
		      << Notification("textDocument/didOpen", "{\"textDocument\":{\"uri\":" + JsonQuote(Uri) + ",\"languageId\":\"veroscript\",\"version\":1,\"text\":" + JsonQuote(Source) + "}}")
		      << Request(2, "textDocument/definition", "{\"textDocument\":{\"uri\":" + JsonQuote(Uri) + "},\"position\":{\"line\":1,\"character\":31}}")
		      << Request(3, "textDocument/hover", "{\"textDocument\":{\"uri\":" + JsonQuote(Uri) + "},\"position\":{\"line\":1,\"character\":31}}")
		      << Request(4, "shutdown") << Notification("exit");
		std::istringstream In(Input.str());
		std::ostringstream Out;
		const auto Exit = RunTranscript(In, Out, Options);
		const auto Text = Out.str();
		if (!Expect(Exit == 0 && Contains(ResponseForId(Text, 2), JsonQuote(Uri)) &&
		            Contains(ResponseForId(Text, 2), "\"line\":0") && Contains(ResponseForId(Text, 3), "Read"),
		        "LSP native annotation service resolves generated members and navigates to the annotation"))
		{
			std::cerr << Text << '\n';
			return EXIT_FAILURE;
		}
	}

	// Keep English for the primary protocol assertions regardless of host locale.
	(void)VeroScript::Locale::TrySetLanguageFromTag("en-US");

	{
		constexpr const char* Uri = "file:///C:/veroscript-tests/lsp-readonly-fields.vs";
		const std::string Source =
		    "class Base {\npublic:\n"
		    "    const int base_value;\n"
		    "    int third;\n"
		    "    Base(int base_value) : base_value(base_value), third(0) {}\n"
		    "    int read() { return base_value + third; }\n"
		    "    int shadow() { int base_value = 1; return base_value + this.base_value; }\n"
		    "};\n"
		    "class Derived : public Base {\npublic:\n"
		    "    Derived() : Base(1) {}\n"
		    "    int inherited() { return base_value + this.base_value; }\n"
		    "};\n"
		    "class Hidden {\npublic:\n"
		    "    int base_value;\n"
		    "    Hidden() : base_value(2) {}\n"
		    "    int read() { return base_value + this.base_value; }\n"
		    "};\n"
		    "void main() { Derived d = Derived(); print(d.base_value); Hidden h = Hidden(); print(h.base_value); }\n";
		std::string MutableSource = Source;
		MutableSource.replace(MutableSource.find("const int"), 5, "     ");
		const std::string Document = "{\"textDocument\":{\"uri\":" + JsonQuote(Uri) + "}}";
		const auto DeltaParams = [&](const char* Previous)
		{
			return "{\"textDocument\":{\"uri\":" + JsonQuote(Uri) + "},\"previousResultId\":" + JsonQuote(Previous) + "}";
		};
		const auto Change = [&](const int Version, const std::string& Text)
		{
			return Notification("textDocument/didChange",
			    "{\"textDocument\":{\"uri\":" + JsonQuote(Uri) + ",\"version\":" + std::to_string(Version) + "},\"contentChanges\":[{\"text\":" + JsonQuote(Text) + "}]}");
		};
		FLspConversationBuffer Buffer;
		std::istream In(&Buffer);
		std::ostream Out(&Buffer);
		int Exit = -1;
		std::thread Server([&]()
		{
			Exit = VeroScript::Test::FLspStdio::Run(In, Out);
		});
		bool bSession = true;
		const auto Send = [&](const int Id, const char* Method, const std::string& Params = "{}")
		{
			if (!bSession)
			{
				return;
			}
			Buffer.Append(Request(Id, Method, Params));
			bSession = Buffer.WaitForResponse(Id);
		};
		Send(1, "initialize");
		Buffer.Append(Notification("initialized"));
		Buffer.Append(Notification("textDocument/didOpen", "{\"textDocument\":{\"uri\":" + JsonQuote(Uri) + ",\"languageId\":\"veroscript\",\"version\":1,\"text\":" + JsonQuote(Source) + "}}"));
		Send(2, "textDocument/semanticTokens/full", Document);
		Send(3, "textDocument/semanticTokens/full/delta", DeltaParams("1"));
		Send(4, "textDocument/semanticTokens/full", Document);
		Buffer.Append(Change(2, MutableSource));
		Send(5, "textDocument/semanticTokens/full/delta", DeltaParams("3"));
		Send(6, "textDocument/semanticTokens/full", Document);
		Buffer.Append(Change(3, Source));
		Send(7, "textDocument/semanticTokens/full/delta", DeltaParams("5"));
		Send(8, "textDocument/semanticTokens/full", Document);
		Send(9, "shutdown");
		Buffer.Append(Notification("exit"));
		Buffer.Close();
		Server.join();
		const std::string Output = Buffer.Str();
		const auto Tokens = DecodeSemanticTokens(ResponseForId(Output, 2));
		const auto MutableTokens = DecodeSemanticTokens(ResponseForId(Output, 6));
		// Every occurrence is checked in source order, including same-name parameters,
		// locals, an unrelated type's same-name field, and qualified receivers.
		const std::vector<std::pair<int, int>> Expected = {
		    {7, 1}, {6, 0}, {7, 1}, {6, 0}, {7, 1}, {6, 0}, {6, 0}, {7, 1},
		    {7, 1}, {7, 1}, {7, 0}, {7, 0}, {7, 0}, {7, 0}, {7, 1}, {7, 0}};
		bool bCorrect = bSession && Tokens.size() > 0 && SemanticTokensDoNotOverlap(Tokens);
		std::size_t Offset = 0;
		for (const auto [Type, Modifiers] : Expected)
		{
			Offset = Source.find("base_value", Offset);
			if (Offset == std::string::npos)
			{
				bCorrect = false;
				break;
			}
			const int Line = static_cast<int>(std::count(Source.begin(), Source.begin() + Offset, '\n'));
			const std::size_t PreviousLine = Source.rfind('\n', Offset);
			const int Column = static_cast<int>(Offset - (PreviousLine == std::string::npos ? 0 : PreviousLine + 1));
			bCorrect &= HasSemanticToken(Tokens, Line, Column, 10, Type, Modifiers) && HasSemanticToken(MutableTokens, Line, Column, 10, Type, 0);
			Offset += 10;
		}
		bCorrect &= Source.find("base_value", Offset) == std::string::npos;
		Offset = 0;
		while ((Offset = Source.find("third", Offset)) != std::string::npos)
		{
			const int Line = static_cast<int>(std::count(Source.begin(), Source.begin() + Offset, '\n'));
			const int Column = static_cast<int>(Offset - Source.rfind('\n', Offset) - 1);
			bCorrect &= HasSemanticToken(Tokens, Line, Column, 5, 7, 0);
			Offset += 5;
		}
		auto Data = SemanticTokenData(ResponseForId(Output, 2));
		bCorrect &= Contains(ResponseForId(Output, 1), "\"tokenModifiers\":[\"readonly\"]") && Contains(ResponseForId(Output, 3), "\"edits\":[]") && ApplySemanticTokenDelta(ResponseForId(Output, 3), Data) && Data == SemanticTokenData(ResponseForId(Output, 4)) && ApplySemanticTokenDelta(ResponseForId(Output, 5), Data) && Data == SemanticTokenData(ResponseForId(Output, 6)) && ApplySemanticTokenDelta(ResponseForId(Output, 7), Data) && Data == SemanticTokenData(ResponseForId(Output, 8)) && Data == SemanticTokenData(ResponseForId(Output, 2));
		if (!Expect(Exit == 0 && bCorrect,
		        "readonly properties survive declaration/initializers/inheritance/shadowing and full/delta edits"))
		{
			std::cerr << Output << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		struct FReadonlyScopeCase
		{
			const char* Name;
			const char* Source;
			const char* Word;
			std::vector<std::pair<int, int>> Expected;
		};
		const FReadonlyScopeCase Cases[] = {
		    {"same-line callables",
		        "class A { public: const int value = 1; int first(int value) { return value; } int last() { return value; } };\n"
		        "class B { public: int value; B(int value) : value(value) {} int read() { return value; } };\n",
		        "value", {{7, 1}, {6, 0}, {6, 0}, {7, 1}, {7, 0}, {6, 0}, {7, 0}, {6, 0}, {7, 0}}},
		    {"type and field spelling collision",
		        "struct Value { int x; };\n"
		        "class Box { public: const int Value = 1; int read() { Value v = Value(); return Value; } };\n",
		        "Value", {{5, 0}, {7, 1}, {5, 0}, {5, 0}, {7, 1}}},
		    {"reference type and field spelling collision",
		        "struct Value { int x; };\n"
		        "class Box { public: const int Value = 1; Value read(Value v) { Value& r = v; return r; } };\n",
		        "Value", {{5, 0}, {7, 1}, {5, 0}, {5, 0}, {5, 0}}},
		    {"field initializer owner",
		        "class A { public: const int value = 1; int f() { return value; } };\n"
		        "class B { public: int value = 2; int other = value; int read() { return value; } };\n",
		        "value", {{7, 1}, {7, 1}, {7, 0}, {7, 0}, {7, 0}}},
		    {"lambda in field initializer",
		        "class A { public: const int value = 1; int f() { return value; } };\n"
		        "class B { public: int value = 2; fn(int)->int reader = [](int value)->int=>value; int other = this.value; int read() { return value; } };\n",
		        "value", {{7, 1}, {7, 1}, {7, 0}, {6, 0}, {6, 0}, {7, 0}, {7, 0}}},
		    {"generic interface field",
		        "interface I<T> { const T value = 4; };\n"
		        "class C : I<int> { public: int read() { return value + this.value; } };\n",
		        "value", {{7, 1}, {7, 1}, {7, 1}}},
		    {"lambda member receiver in field initializer",
		        "struct X { const int value = 1; };\n"
		        "struct Y { int value = 2; };\n"
		        "class A { public: int read(X obj) { return obj.value; } };\n"
		        "class B { public: fn(Y)->int reader = [](Y obj)->int=>obj.value; };\n",
		        "value", {{7, 1}, {7, 0}, {7, 1}, {7, 0}}},
		    {"static readonly field",
		        "class A { public: int token = 1; };\n"
		        "class B { public: static const int token = 2; int read() { return token; } static int readToken() { return token; } };\n",
		        "token", {{7, 0}, {7, 1}, {7, 1}, {7, 1}}},
		};
		for (const auto& Case : Cases)
		{
			const std::string Source = Case.Source;
			const std::string Uri = "file:///C:/veroscript-tests/lsp-readonly-edge.vs";
			std::istringstream In(Request(1, "initialize") + Notification("initialized") + Notification("textDocument/didOpen", "{\"textDocument\":{\"uri\":" + JsonQuote(Uri) + ",\"languageId\":\"veroscript\",\"version\":1,\"text\":" + JsonQuote(Source) + "}}") + Request(2, "textDocument/semanticTokens/full", "{\"textDocument\":{\"uri\":" + JsonQuote(Uri) + "}}") + Request(3, "shutdown") + Notification("exit"));
			std::ostringstream Out;
			bool bCorrect = RunTranscript(In, Out) == 0;
			const auto Tokens = DecodeSemanticTokens(ResponseForId(Out.str(), 2));
			std::size_t Offset = 0;
			for (const auto& [Type, Modifiers] : Case.Expected)
			{
				Offset = Source.find(Case.Word, Offset);
				if (Offset == std::string::npos)
				{
					bCorrect = false;
					break;
				}
				const int Line = static_cast<int>(std::count(Source.begin(), Source.begin() + Offset, '\n'));
				const std::size_t PreviousLine = Source.rfind('\n', Offset);
				const int Column = static_cast<int>(Offset - (PreviousLine == std::string::npos ? 0 : PreviousLine + 1));
				const int Length = static_cast<int>(std::string_view(Case.Word).size());
				bCorrect &= HasSemanticToken(Tokens, Line, Column, Length, Type, Modifiers);
				Offset += Length;
			}
			bCorrect &= Source.find(Case.Word, Offset) == std::string::npos;
			if (!Expect(bCorrect, Case.Name))
			{
				std::cerr << Out.str() << '\n';
				return EXIT_FAILURE;
			}
		}
	}

	{
		struct FSoftGenericHoverCase
		{
			const char* Source;
			const char* Member;
			const char* Detail;
		};
		const FSoftGenericHoverCase Cases[] = {
		    {"void main() { handle<int> h = handle<int>(1); print(h.value); }\n", "value", "int"},
		    {"struct T { int x; }; void main(){ array<array<T>> h=array<array<T>>(); h.push(array<T>()); }\n",
		        "push", "array<array<T>> (array<T> value)"},
		    {"void main(){ pair<fn(int,int)->string,int> p=pair<fn(int,int)->string,int>(); print(p.first); }\n",
		        "first", "fn(int,int)->string"},
		    {"void main() { shared<int> h = shared<int>(1); print(h.value); }\n", "value", "int"},
		    {"void main() { std::handle<int> h = std::handle<int>(1); print(h.value); }\n", "value", "int"},
		    {"void main() { std::shared<int> h = std::shared<int>(1); print(h.value); }\n", "value", "int"},
		    {"void main() { handle<array<int>> h = handle<array<int>>(); print(h.value); }\n", "value", "array<int>"},
		    {"using Box = handle<int>; void main() { Box h = Box(1); print(h.value); }\n", "value", "int"},
		    {"void main() { pair<int, string> p = pair<int, string>(); print(p.second); }\n", "second", "string"},
		    {"void main() { std::pair<int, string> p = std::pair<int, string>(); print(p.first); }\n", "first", "int"},
		    {"void main() { optional<int> h = optional<int>(1); print(h.get()); }\n", "get", "int () const"},
		    {"void main() { std::readonly<int> h = std::readonly<int>(1); print(h.get()); }\n", "get", "const int& () const"},
		    {"class handle<U> { public: U value; handle(U v) : value(v) {} };\n"
		     "void main() { handle<int> h = handle<int>(1); print(h.value); }\n",
		        "value", "int"},
		};
		for (const auto& Case : Cases)
		{
			const std::string Source = Case.Source;
			const std::size_t Offset = Source.rfind(Case.Member);
			const auto LastNewline = Source.rfind('\n', Offset);
			const auto Line = std::count(Source.begin(), Source.begin() + Offset, '\n');
			const auto Column = Offset - (LastNewline == std::string::npos ? 0 : LastNewline + 1);
			const std::string Uri = "file:///C:/veroscript-tests/lsp-soft-generic-hover.vs";
			std::istringstream In(Request(1, "initialize") + Notification("initialized") + Notification("textDocument/didOpen", "{\"textDocument\":{\"uri\":" + JsonQuote(Uri) + ",\"languageId\":\"veroscript\",\"version\":1,\"text\":" + JsonQuote(Source) + "}}") + Request(2, "textDocument/hover", "{\"textDocument\":{\"uri\":" + JsonQuote(Uri) + "},\"position\":{\"line\":" + std::to_string(Line) + ",\"character\":" + std::to_string(Column) + "}}") + Request(3, "shutdown") + Notification("exit"));
			std::ostringstream Out;
			const int Exit = RunTranscript(In, Out);
			const std::string Output = Out.str();
			const std::string Block = JsonQuote(std::string("```veroscript\n") + Case.Detail + "\n```");
			if (!Expect(Exit == 0 && Contains(ResponseForId(Output, 2), std::string_view(Block).substr(1, Block.size() - 2)),
			        "raw soft generic member hover uses the concrete payload type"))
			{
				std::cerr << Source << Output << '\n';
				return EXIT_FAILURE;
			}
		}
	}

	{
		struct FGenericSignatureCase
		{
			const char* Name;
			const char* Source;
			const char* CaretAfter;
			const char* Label;
			std::vector<std::string> Parameters;
			unsigned ActiveParameter = 0;
		};
		const FGenericSignatureCase Cases[] = {
		    {"readonly_signature",
		        "void main(){ readonly<int> h=readonly<int>(1); print(h.get()); }\n",
		        "h.get(", "readonly.get const int& () const", {}},
		    {"readonly_ctor",
		        "void main(){ readonly<int> h=readonly<int>(1); }\n",
		        "readonly<int>(", "readonly(int value)", {"int value"}},
		    {"optional_signature",
		        "struct T { int x; }; void main(){ optional<array<T>> h=optional<array<T>>(); h.value_or(array<T>()); }\n",
		        "h.value_or(", "optional.value_or array<T> (array<T> fallback) const", {"array<T> fallback"}},
		    {"capture",
		        "struct V { int x; }; void main(){ field_info<V> f=field_info<V>(\"V\",0); f.get<int>(V()); }\n",
		        "f.get<int>(", "int get<int>(V receiver) const", {"V receiver"}},
		    {"capture_return",
		        "struct T { int x; }; struct Point { int x; }; void main(){ field_info<Point> f=field_info<Point>(\"Point\",0); Point p=Point(); f.get<T>(p); }\n",
		        "f.get<T>(", "T get<T>(Point receiver) const", {"Point receiver"}},
		    {"functor",
		        "struct Box<T> { T operator()(T x) { return x; } }; void main(){ Box<int> b=Box<int>(); b(1); }\n",
		        "b(", "Box.operator() int(int x)", {"int x"}},
		    {"callable_payload",
		        "void main(){ optional<fn(int,int)->string> h=optional<fn(int,int)->string>(); h.value_or([](int x,int y)->string=>\"ok\"); }\n",
		        "h.value_or(", "optional.value_or fn(int,int)->string (fn(int,int)->string fallback) const", {"fn(int,int)->string fallback"}},
		    {"nested_callable",
		        "void main(){ fn(int)->fn(string)->int f=[](int x)->fn(string)->int=>[](string s)->int=>1; f(1); }\n",
		        "f(", "fn(int)->fn(string)->int", {"int"}},
		    {"method_callable",
		        "struct Box<T> { T value; fn(T)->T make(int x) { return [](T v)->T=>v; } }; void main(){ Box<int> b=Box<int>(); b.make(1)(2); }\n",
		        "b.make(1)(", "fn(int)->int", {"int"}},
		    {"capture_set",
		        "struct V {int x;}; void main(){ field_info<V> f=field_info<V>(\"V\",0); V v=V(); f.set<int>(v,1); }\n",
		        "f.set<int>(", "void set<int>(V receiver, int value) const", {"V receiver", "int value"}},
		    {"capture_invoke",
		        "struct R {int x;}; void main(){ method_info<R> m=method_info<R>(\"R\",0); R r=R(); m.invoke<int>(r); }\n",
		        "m.invoke<int>(", "int invoke<int>(R receiver, args...) const", {"R receiver", "args..."}},
		    {"capture_callable",
		        "struct Point {int x;}; void main(){ field_info<Point> f=field_info<Point>(\"Point\",0); Point p=Point(); f.get<fn(int,int)->string>(p); }\n",
		        "f.get<fn(int,int)->string>(", "fn(int,int)->string get<fn(int,int)->string>(Point receiver) const", {"Point receiver"}},
		    {"nested_constructor",
		        "void main(){ readonly<array<int>> h=readonly<array<int>>(array<int>()); }\n",
		        "readonly<array<int>>(", "readonly(array<int> value)", {"array<int> value"}, 0},
		    {"nested_direct_constructor",
		        "void main(){ readonly<array<int>> h(array<int>()); }\n",
		        "h(", "readonly(array<int> value)", {"array<int> value"}, 0},
		    {"nested_method",
		        "struct Point{int x;}; void main(){field_info<Point> f=field_info<Point>(\"Point\",0); Point p=Point(); f.get<array<int>>(p); }\n",
		        "f.get<array<int>>(", "array<int> get<array<int>>(Point receiver) const", {"Point receiver"}, 0},
		    {"nested_callable",
		        "struct Point{int x;}; void main(){field_info<Point> f=field_info<Point>(\"Point\",0); Point p=Point(); f.get<map<string,fn(int,int)->string>>(p); }\n",
		        "f.get<map<string,fn(int,int)->string>>(", "map<string,fn(int,int)->string> get<map<string,fn(int,int)->string>>(Point receiver) const", {"Point receiver"}, 0},
		    {"triple_close",
		        "struct Point{int x;}; void main(){field_info<Point> f=field_info<Point>(\"Point\",0); Point p=Point(); f.get<array<array<int>>>(p); }\n",
		        "f.get<array<array<int>>>(", "array<array<int>> get<array<array<int>>>(Point receiver) const", {"Point receiver"}, 0},
		    {"spaced_close",
		        "struct Point{int x;}; void main(){field_info<Point> f=field_info<Point>(\"Point\",0); Point p=Point(); f.get<array<int> >(p); }\n",
		        "f.get<array<int> >(", "array<int> get<array<int>>(Point receiver) const", {"Point receiver"}, 0},
		    {"shift_argument",
		        "void f(int a,int b){} void main(){f(8 >> 1,2);}\n",
		        "f(8 >> 1,", "f void(int a, int b)", {"int a", "int b"}, 1},
		    {"shift_context",
		        "int f(int a,int b){return a+b;} void main(){int a=0; int b=1; bool x=a < b; bool y=a < (8 >> f(1,2));}\n",
		        "f(1,", "f int(int a, int b)", {"int a", "int b"}, 1},
		};
		for (const auto& Case : Cases)
		{
			const std::string Source = Case.Source;
			const std::size_t Offset = Source.rfind(Case.CaretAfter) + std::string_view(Case.CaretAfter).size();
			const auto Line = std::count(Source.begin(), Source.begin() + Offset, '\n');
			const auto LastNewline = Source.rfind('\n', Offset);
			const auto Column = Offset - (LastNewline == std::string::npos ? 0 : LastNewline + 1);
			const std::string Uri = "file:///C:/veroscript-tests/lsp-generic-signature.vs";
			std::istringstream In(Request(1, "initialize") + Notification("initialized") + Notification("textDocument/didOpen", "{\"textDocument\":{\"uri\":" + JsonQuote(Uri) + ",\"languageId\":\"veroscript\",\"version\":1,\"text\":" + JsonQuote(Source) + "}}") + Request(2, "textDocument/signatureHelp", "{\"textDocument\":{\"uri\":" + JsonQuote(Uri) + "},\"position\":{\"line\":" + std::to_string(Line) + ",\"character\":" + std::to_string(Column) + "}}") + Request(3, "shutdown") + Notification("exit"));
			std::ostringstream Out;
			const int Exit = RunTranscript(In, Out);
			const std::string Output = Out.str();
			const auto Raw = ResponseForId(Output, 2);
			std::string Expected = "\"signatures\":[{\"label\":" + JsonQuote(Case.Label) + ",\"parameters\":[";
			for (std::size_t Index = 0; Index < Case.Parameters.size(); ++Index)
			{
				if (Index != 0)
				{
					Expected += ',';
				}
				Expected += "{\"label\":" + JsonQuote(Case.Parameters[Index]) + "}";
			}
			Expected += "]}]";
			if (!Expect(Exit == 0 && Contains(Raw, Expected) && Contains(Raw, "\"activeParameter\":" + std::to_string(Case.ActiveParameter)) && Contains(Raw, "\"activeSignature\":0"), Case.Name))
			{
				std::cerr << Source << "position: " << Line << ':' << Column << '\n'
				          << Output << '\n';
				return EXIT_FAILURE;
			}
		}
	}

	{
		struct FGenericCompletionCase
		{
			const char* Source;
			std::vector<std::pair<std::string, std::string>> Details;
		};
		const FGenericCompletionCase Cases[] = {
		    {"struct T { int x; }; void main(){ optional<array<T>> h=optional<array<T>>(); h. }\n",
		        {{"get", "array<T> () const"}, {"value_or", "array<T> (array<T> fallback) const"}}},
		    {"struct T { int x; }; void main(){ array<array<T>> h=array<array<T>>(); h. }\n",
		        {{"push", "array<array<T>> (array<T> value)"}}},
		    {"void Broken( ;\nstruct T { int x; }; void main(){ array<array<T>> h=array<array<T>>(); h. }\n",
		        {{"push", "array<array<T>> (array<T> value)"}}},
		};
		for (const auto& Case : Cases)
		{
			const std::string Source = Case.Source;
			const auto Offset = Source.rfind("h.") + 2;
			const auto Line = std::count(Source.begin(), Source.begin() + Offset, '\n');
			const auto LastNewline = Source.rfind('\n', Offset);
			const auto Column = Offset - (LastNewline == std::string::npos ? 0 : LastNewline + 1);
			const std::string Uri = "file:///C:/veroscript-tests/lsp-generic-completion.vs";
			std::istringstream In(Request(1, "initialize") + Notification("initialized") + Notification("textDocument/didOpen", "{\"textDocument\":{\"uri\":" + JsonQuote(Uri) + ",\"languageId\":\"veroscript\",\"version\":1,\"text\":" + JsonQuote(Source) + "}}") + Request(2, "textDocument/completion", "{\"textDocument\":{\"uri\":" + JsonQuote(Uri) + "},\"position\":{\"line\":" + std::to_string(Line) + ",\"character\":" + std::to_string(Column) + "}}") + Request(3, "shutdown") + Notification("exit"));
			std::ostringstream Out;
			bool bCorrect = RunTranscript(In, Out) == 0;
			const std::string Output = Out.str();
			const auto Raw = ResponseForId(Output, 2);
			for (const auto& [Label, Detail] : Case.Details)
			{
				bCorrect &= JsonStringField(CompletionItemForLabel(Raw, Label), "detail") == Detail;
				bCorrect &= CompletionItemForLabel(Raw, Label, 1).empty();
			}
			if (!Expect(bCorrect, "generic member details are specialized exactly once, including recovery"))
			{
				std::cerr << Source << Output << '\n';
				return EXIT_FAILURE;
			}
		}
	}

	{
		struct FStandardHoverCase
		{
			const char* Call;
			const char* Prelude;
			std::vector<std::string> Signatures;
			bool bCheckSignatureHelp = false;
			const char* HoverToken = nullptr;
		};
		const FStandardHoverCase Cases[] = {
		    {"print(1);", "", {"void print(any value);", "void print(string fmt, ...);"}},
		    {"std::print(1);", "", {"void std::print(any value);", "void std::print(string fmt, ...);"}, true},
		    {"std::print(1);", "void Broken( ;\n", {"void std::print(any value);", "void std::print(string fmt, ...);"}},
		    {"unknown::print(1);", "void Broken( ;\n", {}},
		    {"object.print(1);", "void Broken( ;\n", {}},
		    {"items[0].print(1);", "void Broken( ;\n", {}},
		    {"make().print(1);", "void Broken( ;\n", {}, false, "print"},
		    {"std::print(1);", "void print(int value) {}\n", {"void std::print(int value);", "void std::print(any value);", "void std::print(string fmt, ...);"}},
		    {"std::format(\"{}\", 1);", "", {"string std::format(string fmt, ...);"}, true},
		    {"std::strings::contains(\"abc\", \"b\");", "", {"bool std::strings::contains(string value, string part);"}, true},
		    {"std::concurrent::spawn(\"worker\", null);", "", {"int std::concurrent::spawn(string name, channel<T> ch);", "int std::concurrent::spawn(fn(args...)->void callable, args...);"}},
		    {"std::concurrent::spawn_task<int>(\"worker\");", "", {"task<T> std::concurrent::spawn_task<T>(string name);", "task<T> std::concurrent::spawn_task<T>(fn(args...)->T callable, args...);"}},
		    // Separate source declarations exercise the general alias path, rather
		    // than only the combined print detail and special concurrency stubs.
		    {"std::abs(1);", "int abs(int value) { return value; }\n"
		                     "double abs(double value) { return value; }\n",
		        {"int std::abs(int value);", "double std::abs(double value);"}, true},
		    {"std::abs();", "int abs(int value = 0) { return value; }\n", {"int std::abs(int value = …);"}},
		    {"std::abs(1);", "fn(int)->int abs(int value) { return [](int x)->int=>x; }\n", {"fn(int)->int std::abs(int value);"}, true},
		    {"std::abs();", "array<array<int>> abs() { return array<array<int>>(); }\n", {"array<array<int>> std::abs();"}},
		    {"std::algorithm::copy<int>(array<int>());",
		        "import \"algorithm.vs\";\n", {"array<T> std::algorithm::copy<T>(array<T> values);"}, true}};
		for (const FStandardHoverCase& Case : Cases)
		{
			const std::string Document = std::string(Case.Prelude) + "void main() { " + Case.Call + " }\n";
			const std::size_t Offset = Document.rfind(Case.Call) + (Case.HoverToken ? std::string_view(Case.Call).rfind(Case.HoverToken) + std::string_view(Case.HoverToken).size() : std::min(std::string_view(Case.Call).find('('), std::string_view(Case.Call).find('<'))) - 1;
			const std::size_t LastNewline = Document.rfind('\n', Offset);
			const std::size_t Line = static_cast<std::size_t>(
			    std::count(Document.begin(), Document.begin() + Offset, '\n'));
			const std::size_t Character = Offset - (LastNewline == std::string::npos ? 0 : LastNewline + 1);
			constexpr const char* HoverUri = "file:///C:/veroscript-tests/lsp-standard-hover.vs";
			std::ostringstream HoverInput;
			HoverInput << Request(1, "initialize", "{\"locale\":\"en-US\"}")
			           << Notification("initialized")
			           << Notification("textDocument/didOpen",
			                  "{\"textDocument\":{\"uri\":" + JsonQuote(HoverUri) + ",\"languageId\":\"veroscript\",\"version\":1,\"text\":" + JsonQuote(Document) + "}}")
			           << Request(2, "textDocument/hover",
			                  "{\"textDocument\":{\"uri\":" + JsonQuote(HoverUri) + "},\"position\":{\"line\":" + std::to_string(Line) + ",\"character\":" + std::to_string(Character) + "}}")
			           << Request(3, "textDocument/signatureHelp",
			                  "{\"textDocument\":{\"uri\":" + JsonQuote(HoverUri) + "},\"position\":{\"line\":" + std::to_string(Line) + ",\"character\":" + std::to_string(Document.rfind(Case.Call) + std::string_view(Case.Call).find('(') + 1 - (LastNewline == std::string::npos ? 0 : LastNewline + 1)) + "}}")
			           << Request(4, "shutdown") << Notification("exit");
			std::istringstream HoverIn(HoverInput.str());
			std::ostringstream HoverOut;
			const int Exit = RunTranscript(HoverIn, HoverOut);
			const std::string Output = HoverOut.str();
			const std::string_view Raw = ResponseForId(Output, 2);
			const std::string_view Markdown = JsonStringField(Raw, "value");
			bool bValid = Exit == 0 && (Case.Signatures.empty() ? Contains(Raw, "\"result\":null") : Contains(Raw, "\"kind\":\"markdown\"")) && !Contains(Markdown, ";;") && !Contains(Markdown, "std.");
			std::size_t ParameterLists = 0;
			for (const std::string& Signature : Case.Signatures)
			{
				const std::string Quoted = JsonQuote(Signature);
				bValid &= CountOccurrences(Markdown,
				              std::string_view(Quoted).substr(1, Quoted.size() - 2)) == 1;
				ParameterLists += CountOccurrences(Signature, "(");
			}
			bValid &= CountOccurrences(Markdown, "(") == ParameterLists;
			if (std::string_view(Case.Call).find("print(") != std::string_view::npos && Case.Signatures.size() == 2)
			{
				const std::string Block = "```veroscript\n" + Case.Signatures[0] + "\n" + Case.Signatures[1] + "\n```";
				const std::string Quoted = JsonQuote(Block);
				bValid &= Contains(Markdown, std::string_view(Quoted).substr(1, Quoted.size() - 2));
			}
			if (Case.bCheckSignatureHelp)
			{
				const std::string_view Help = ResponseForId(Output, 3);
				bValid &= Contains(Help, "\"signatures\":[") && !Contains(Help, "std.") && !Contains(Help, "\\n") && !Contains(Help, ";;");
				for (std::string Signature : Case.Signatures)
				{
					Signature.pop_back(); // SignatureHelp labels omit the declaration semicolon.
					bValid &= CountOccurrences(Help, "\"label\":" + JsonQuote(Signature)) == 1;
				}
				if (std::string_view(Case.Call) == "std::print(1);")
				{
					bValid &= CountOccurrences(Help, "\"label\":") == 5;
				}
				if (std::string_view(Case.Prelude).find("fn(") == 0)
				{
					bValid &= CountOccurrences(Help, "\"label\":") == 2 && Contains(Help, "\"parameters\":[{\"label\":\"int value\"}]");
				}
			}
			if (!Expect(bValid, "raw LSP hover has unique, consistently named standard signatures"))
			{
				std::cerr << "document: " << Document << "\nposition: " << Line
				          << ':' << Character << "\nraw hover: " << Raw << "\nLSP output: " << Output << '\n';
				return EXIT_FAILURE;
			}
		}
	}

	constexpr const char* Uri = "file:///C:/veroscript-tests/lsp-test.vs";
	// line0: void Helper(int a, int b) {}
	// line1: /// Entry point documentation.
	// Line 2 includes a Unicode string literal and a missing call.
	const std::string SourceText =
	    "void Helper(int a, int b) {}\\n"
	    "/// Entry point documentation.\\n"
	    "void main() { Helper(1, 2); print(\\\"😀中\\\"); missing(); }\\n";

	std::ostringstream Input;
	Input << Request(1, "initialize");
	Input << Notification("initialized");
	Input << Notification(
	    "textDocument/didOpen",
	    std::string("{\"textDocument\":{\"uri\":\"") + Uri + "\",\"languageId\":\"veroscript\",\"version\":1," + "\"text\":\"" + SourceText + "\"}}");
	Input << Request(
	    2,
	    "textDocument/documentSymbol",
	    std::string("{\"textDocument\":{\"uri\":\"") + Uri + "\"}}");
	Input << Request(
	    3,
	    "textDocument/completion",
	    std::string("{\"textDocument\":{\"uri\":\"") + Uri + "\"},\"position\":{\"line\":2,\"character\":12}}");
	Input << Request(
	    4,
	    "textDocument/hover",
	    std::string("{\"textDocument\":{\"uri\":\"") + Uri + "\"},\"position\":{\"line\":2,\"character\":5}}");
	// After "Helper(1," -> activeParameter should be 1.
	Input << Request(
	    5,
	    "textDocument/signatureHelp",
	    std::string("{\"textDocument\":{\"uri\":\"") + Uri + "\"},\"position\":{\"line\":2,\"character\":24}}");
	// Helper definition identifier.
	Input << Request(
	    6,
	    "textDocument/references",
	    std::string("{\"textDocument\":{\"uri\":\"") + Uri + "\"},\"position\":{\"line\":0,\"character\":5}," + "\"context\":{\"includeDeclaration\":true}}");
	Input << Request(
	    7,
	    "textDocument/documentHighlight",
	    std::string("{\"textDocument\":{\"uri\":\"") + Uri + "\"},\"position\":{\"line\":0,\"character\":5}}");
	Input << Request(
	    8,
	    "textDocument/rename",
	    std::string("{\"textDocument\":{\"uri\":\"") + Uri + "\"},\"position\":{\"line\":0,\"character\":5}," + "\"newName\":\"RenamedHelper\"}");
	Input << Request(
	    9,
	    "textDocument/formatting",
	    std::string("{\"textDocument\":{\"uri\":\"") + Uri + "\"},\"options\":{\"tabSize\":4,\"insertSpaces\":true}}");
	Input << Request(
	    10,
	    "workspace/symbol",
	    "{\"query\":\"helper\"}");
	Input << Request(
	    11,
	    "textDocument/semanticTokens/full",
	    std::string("{\"textDocument\":{\"uri\":\"") + Uri + "\"}}");
	Input << Request(
	    13,
	    "workspace/symbol",
	    "{\"query\":\"lsp-test Helper\"}");
	Input << Request(
	    14,
	    "textDocument/prepareRename",
	    std::string("{\"textDocument\":{\"uri\":\"") + Uri + "\"},\"position\":{\"line\":0,\"character\":5}}");
	Input << Request(
	    15,
	    "textDocument/declaration",
	    std::string("{\"textDocument\":{\"uri\":\"") + Uri + "\"},\"position\":{\"line\":2,\"character\":15}}");
	Input << Notification(
	    "textDocument/didChange",
	    std::string("{\"textDocument\":{\"uri\":\"") + Uri + "\",\"version\":2},\"contentChanges\":[" + "{\"text\":\"void main() { print(1); }\\n\"}]}");
	Input << Notification(
	    "textDocument/didClose",
	    std::string("{\"textDocument\":{\"uri\":\"") + Uri + "\"}}");
	Input << Request(12, "shutdown");
	Input << Notification("exit");

	std::istringstream In(Input.str());
	std::ostringstream Out;
	const int ExitCode = RunTranscript(In, Out);
	const std::string Output = Out.str();

	if (!Expect(ExitCode == 0, "clean shutdown should return zero") || !Expect(Contains(Output, "\"change\":2"), "incremental text sync capability") || !Expect(Contains(Output, "\"includeText\":true"), "didSave text sync capability") || !Expect(Contains(Output, "\"hoverProvider\":true"), "initialize capabilities") || !Expect(Contains(Output, "\"triggerCharacters\":[\".\",\":\",\"@\"]"), "completion triggerCharacters") || !Expect(Contains(Output, "\"name\":\"veroscript-lsp\""), "lsp serverInfo name") || !Expect(Contains(Output, "\"platform\":\""), "lsp serverInfo platform") || !Expect(Contains(Output, "\"jitCapability\":\"backend-services\""), "lsp serverInfo core JIT service contract") || !Expect(Contains(Output, "\"documentSymbolProvider\":true"), "symbol capability") || !Expect(Contains(Output, "\"decorator\""), "semantic decorator capability") || !Expect(Contains(Output, "\"decoratorArgument\""), "semantic decorator-argument capability") || !Expect(Contains(Output, "\"constant\""), "semantic constant capability") || !Expect(Contains(Output, "\"decoratorArgument\",\"constant\",\"label\",\"string\",\"number\",\"comment\",\"preprocessor\"]"), "semantic lexical categories preserve append-only legend order") || !Expect(Contains(Output, "\"semanticTokensProvider\""), "semantic tokens capability") || !Expect(Contains(Output, "\"signatureHelpProvider\""), "signatureHelp capability") || !Expect(Contains(Output, "\"triggerCharacters\":[\"(\",\",\"]"), "signatureHelp triggerCharacters") || !Expect(Contains(Output, "\"referencesProvider\":true"), "references capability") || !Expect(Contains(Output, "\"documentHighlightProvider\":true"), "documentHighlight capability") || !Expect(Contains(Output, "\"declarationProvider\":true"), "declaration capability") || !Expect(Contains(Output, "\"prepareProvider\":true"), "prepareRename capability") || !Expect(Contains(Output, "\"documentFormattingProvider\":true"), "formatting capability") || !Expect(Contains(Output, "\"workspaceSymbolProvider\":{\"resolveProvider\":true}"), "resolvable workspaceSymbol capability") || !Expect(Contains(Output, "\"workspaceFolders\":{\"changeNotifications\":true,\"supported\":true}"), "workspace folder change capability") || !Expect(Contains(Output, "\"method\":\"textDocument/publishDiagnostics\""), "diagnostics notification") || !Expect(Contains(Output, "\"message\":\"unknown function: missing\""), "invalid source diagnostic") || !Expect(Contains(Output, "\"character\":42"), "LSP ranges use UTF-16 character offsets") || !Expect(Contains(Output, "\"name\":\"main\""), "document symbol result") || !Expect(Contains(Output, "\"isIncomplete\":false"), "completion result") || !Expect(Contains(Output, "\"sortText\":\""), "completion sortText from SortPriority") || !Expect(Contains(Output, "Entry point documentation."), "hover includes leading documentation") || !Expect(Contains(ResponseForId(Output, 4), "```veroscript\\n"), "hover sends a Markdown signature block") || !Expect(Contains(Output, "\"signatures\""), "signatureHelp signatures") || !Expect(Contains(Output, "\"parameters\""), "signatureHelp parameters") || !Expect(Contains(Output, "\"activeSignature\""), "signatureHelp activeSignature") || !Expect(Contains(Output, "\"activeParameter\""), "signatureHelp activeParameter") || !Expect(Contains(Output, "\"id\":6,\"jsonrpc\":\"2.0\",\"result\":[{"), "references Location array") || !Expect(Contains(Output, "\"uri\":\"file:///C:/veroscript-tests/lsp-test.vs\""), "references uri") || !Expect(Contains(Output, "\"id\":7,\"jsonrpc\":\"2.0\",\"result\":[{"), "documentHighlight array") || !Expect(Contains(Output, "\"kind\":"), "documentHighlight kind") || !Expect(Contains(Output, "\"changes\""), "rename WorkspaceEdit changes") || !Expect(Contains(Output, "\"newText\":\"RenamedHelper\""), "rename TextEdit newText") || !Expect(Contains(Output, "\"id\":9,\"jsonrpc\":\"2.0\",\"result\":[{"), "formatting TextEdit array") || !Expect(Contains(Output, "\"newText\":"), "formatting newText") || !Expect(Contains(Output, "\"id\":10,\"jsonrpc\":\"2.0\",\"result\":[{"), "workspace/symbol SymbolInformation array") || !Expect(Contains(Output, "\"containerName\""), "workspace symbol containerName") || !Expect(Contains(Output, "\"detail\""), "workspace symbol detail") || !Expect(Contains(Output, "\"location\""), "workspace symbol location") || !Expect(Contains(Output, "\"id\":13,\"jsonrpc\":\"2.0\",\"result\":[{"), "workspace symbol multi-field token search") || !Expect(Contains(Output, "\"id\":14,\"jsonrpc\":\"2.0\",\"result\":{\"placeholder\":\"Helper\"") || Contains(Output, "\"placeholder\":\"Helper\",\"range\":"), "prepareRename returns semantic rename placeholder") || !Expect(Contains(Output, "\"id\":15,\"jsonrpc\":\"2.0\",\"result\":[{"), "declaration returns declaration locations") || !Expect(Contains(Output, "\"id\":11,\"jsonrpc\":\"2.0\",\"result\":{\"data\":[") || Contains(ResponseForId(Output, 11), "\"code\":-32801"), "semantic tokens or superseded document response") || !Expect(Contains(Output, "\"diagnostics\":[]"), "clean/closed diagnostics cleared") || !Expect(Contains(Output, "\"id\":12,\"jsonrpc\":\"2.0\",\"result\":null"), "shutdown response"))
	{
		std::cerr << Output << '\n';
		return EXIT_FAILURE;
	}

	{
		// UNC file URIs must remain absolute network paths and round-trip without
		// gaining or losing slashes in diagnostics and navigation responses.
		constexpr const char* UncUri = "file://server/share/unc-test.vs";
		std::ostringstream UncInput;
		UncInput << Request(1, "initialize");
		UncInput << Notification("initialized");
		UncInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + UncUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                            "\"text\":\"void main() {}\\n\"}}");
		UncInput << Request(2, "shutdown");
		UncInput << Notification("exit");
		std::istringstream UncIn(UncInput.str());
		std::ostringstream UncOut;
		const int UncExit = RunTranscript(UncIn, UncOut);
		const std::string UncOutput = UncOut.str();
		if (!Expect(
		        UncExit == 0 && Contains(UncOutput, "\"uri\":\"file://server/share/unc-test.vs\""),
		        "UNC file URI remains an absolute network path"))
		{
			std::cerr << UncOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	#if !defined(_WIN32)
	{
		std::ostringstream LocalInput;
		LocalInput << Request(1, "initialize") << Notification("initialized");
		LocalInput << Notification("textDocument/didOpen",
		    "{\"textDocument\":{\"uri\":\"file://LOCALHOST/tmp/vero%5Cpath/main.vs\","
		    "\"languageId\":\"veroscript\",\"version\":1,\"text\":\"void main() {}\\n\"}}");
		LocalInput << Request(2, "shutdown") << Notification("exit");
		std::istringstream LocalIn(LocalInput.str());
		std::ostringstream LocalOut;
		if (!Expect(RunTranscript(LocalIn, LocalOut) == 0 && Contains(LocalOut.str(), "\"uri\":\"file:///tmp/vero%5Cpath/main.vs\""),
		        "POSIX localhost file URI identifies the local absolute path"))
		{
			std::cerr << LocalOut.str() << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		constexpr const char* LiteralUri = "file:///tmp/vero%5Cpath/main.vs";
		std::ostringstream LiteralInput;
		LiteralInput << Request(1, "initialize") << Notification("initialized");
		LiteralInput << Notification("textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + LiteralUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                                "\"text\":\"void main() {}\\n\"}}");
		LiteralInput << Request(2, "shutdown") << Notification("exit");
		std::istringstream LiteralIn(LiteralInput.str());
		std::ostringstream LiteralOut;
		if (!Expect(RunTranscript(LiteralIn, LiteralOut) == 0 && Contains(LiteralOut.str(), "\"uri\":\"file:///tmp/vero%5Cpath/main.vs\""),
		        "POSIX file URI preserves a percent-encoded literal backslash"))
		{
			std::cerr << LiteralOut.str() << '\n';
			return EXIT_FAILURE;
		}
	}
	#endif

	{
		// Closed-file workspace caches are invalidated through the standard dynamic
		// watched-files registration when the client advertises that capability.
		std::ostringstream WatchInput;
		WatchInput << Request(
		    1,
		    "initialize",
		    "{\"capabilities\":{\"workspace\":{\"didChangeWatchedFiles\":{"
		    "\"dynamicRegistration\":true}}}}");
		WatchInput << Notification("initialized");
		// A client response to the server-originated registration request has an id
		// but no method. It must not be dispatched as a client request.
		WatchInput << Frame(
		    "{\"jsonrpc\":\"2.0\",\"id\":\"veroscript-register-workspace-watchers-0\","
		    "\"result\":null}");
		WatchInput << Request(2, "shutdown");
		WatchInput << Notification("exit");
		std::istringstream WatchIn(WatchInput.str());
		std::ostringstream WatchOut;
		const int WatchExit = RunTranscript(WatchIn, WatchOut);
		const std::string WatchOutput = WatchOut.str();
		if (!Expect(
		        WatchExit == 0 && Contains(WatchOutput, "\"method\":\"client/registerCapability\"") && Contains(WatchOutput, "\"method\":\"workspace/didChangeWatchedFiles\"") && Contains(WatchOutput, "\"globPattern\":\"**/*.vs\"") && Contains(WatchOutput, "\"globPattern\":\"**/*.[vV][sS][lL][iI][bB]\"") && !Contains(WatchOutput, "\"code\":-32601"),
		        "dynamic watched-files capability is registered for workspace scripts"))
		{
			std::cerr << WatchOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Semantic classification must come from resolved symbol kinds, not casing:
		// both Payload and Message are PascalCase, but only Payload is a type.
		constexpr const char* SemanticUri =
		    "file:///C:/veroscript-tests/lsp-semantic.vs";
		std::ostringstream SemanticInput;
		SemanticInput << Request(1, "initialize");
		SemanticInput << Notification("initialized");
		SemanticInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + SemanticUri + "\",\"languageId\":\"veroscript\",\"version\":1," + "\"text\":\"struct Payload {}\\n"
		                                                                                                                     "void Send(Payload Message) { Payload Copy = Message; }\\n\"}}");
		SemanticInput << Request(
		    2,
		    "textDocument/semanticTokens/full",
		    std::string("{\"textDocument\":{\"uri\":\"") + SemanticUri + "\"}}");
		SemanticInput << Request(3, "shutdown");
		SemanticInput << Notification("exit");

		std::istringstream SemanticIn(SemanticInput.str());
		std::ostringstream SemanticOut;
		const int SemanticExit = RunTranscript(SemanticIn, SemanticOut);
		const std::string SemanticOutput = SemanticOut.str();
		const std::vector<FDecodedSemanticToken> SemanticTokens =
		    DecodeSemanticTokens(ResponseForId(SemanticOutput, 2));
		if (!Expect(SemanticExit == 0, "semantic-token session should shut down cleanly") || !Expect(HasSemanticToken(SemanticTokens, 0, 0, 6, 12) && HasSemanticToken(SemanticTokens, 0, 7, 7, 5) && HasSemanticToken(SemanticTokens, 1, 0, 4, 12) && HasSemanticToken(SemanticTokens, 1, 5, 4, 9) && HasSemanticToken(SemanticTokens, 1, 10, 7, 5) && HasSemanticToken(SemanticTokens, 1, 18, 7, 6) && HasSemanticToken(SemanticTokens, 1, 29, 7, 5) && HasSemanticToken(SemanticTokens, 1, 37, 4, 6) && HasSemanticToken(SemanticTokens, 1, 44, 7, 6), "PascalCase type and values should receive distinct semantic token kinds"))
		{
			std::cerr << SemanticOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Incremental edits use UTF-16 positions and are applied in order against the
		// current overlay, rather than treating a ranged edit as a full document snapshot.
		std::ostringstream IncrementalInput;
		IncrementalInput << Request(1, "initialize");
		IncrementalInput << Notification("initialized");
		IncrementalInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + Uri + "\",\"languageId\":\"veroscript\",\"version\":1," + "\"text\":\"void main() { print(1); }\\n\"}}");
		// Replace `print(1);` (UTF-16 columns [14, 23)) with an invalid call.
		IncrementalInput << Notification(
		    "textDocument/didChange",
		    std::string("{\"textDocument\":{\"uri\":\"") + Uri + "\",\"version\":2},\"contentChanges\":[{\"range\":{\"start\":{\"line\":0,\"character\":14}," + "\"end\":{\"line\":0,\"character\":23}},\"rangeLength\":9,\"text\":\"missing();\"}]}");
		IncrementalInput << Notification(
		    "textDocument/didSave",
		    std::string("{\"textDocument\":{\"uri\":\"") + Uri + "\"},\"text\":\"void Saved() {}\\n\"}");
		IncrementalInput << Request(
		    2,
		    "textDocument/documentSymbol",
		    std::string("{\"textDocument\":{\"uri\":\"") + Uri + "\"}}");
		IncrementalInput << Request(3, "shutdown");
		IncrementalInput << Notification("exit");

		std::istringstream IncrementalIn(IncrementalInput.str());
		std::ostringstream IncrementalOut;
		const int IncrementalExit = RunTranscript(IncrementalIn, IncrementalOut);
		if (!Expect(IncrementalExit == 0, "incremental session should shut down cleanly") || !Expect(Contains(IncrementalOut.str(), "unknown function: missing"), "incremental edit should update diagnostics") || !Expect(Contains(IncrementalOut.str(), "\"name\":\"Saved\""), "didSave text should refresh the document overlay"))
		{
			std::cerr << IncrementalOut.str() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Rename of a non-renameable token should be InvalidParams (-32602).
		std::ostringstream RenameFailInput;
		RenameFailInput << Request(1, "initialize");
		RenameFailInput << Notification("initialized");
		RenameFailInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + Uri + "\",\"languageId\":\"veroscript\",\"version\":1," + "\"text\":\"void main() { print(1); }\\n\"}}");
		RenameFailInput << Request(
		    2,
		    "textDocument/rename",
		    std::string("{\"textDocument\":{\"uri\":\"") + Uri + "\"},\"position\":{\"line\":0,\"character\":0}," + "\"newName\":\"Nope\"}");
		RenameFailInput << Request(3, "shutdown");
		RenameFailInput << Notification("exit");

		std::istringstream RenameFailIn(RenameFailInput.str());
		std::ostringstream RenameFailOut;
		const int RenameFailExit = RunTranscript(RenameFailIn, RenameFailOut);
		const std::string RenameFailOutput = RenameFailOut.str();
		if (!Expect(RenameFailExit == 0, "rename-fail session should shut down cleanly") || !Expect(Contains(RenameFailOutput, "\"code\":-32602"), "rename failure InvalidParams") || !Expect(Contains(RenameFailOutput, "\"error\":"), "rename failure error object"))
		{
			std::cerr << RenameFailOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		std::istringstream ExitOnlyIn(Notification("exit"));
		std::ostringstream ExitOnlyOut;
		if (!Expect(
		        RunTranscript(ExitOnlyIn, ExitOnlyOut) != 0,
		        "exit without shutdown should return non-zero"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::istringstream MalformedIn(Frame("{\"jsonrpc\":\"2.0\",\"id\":1,") + Request(2, "initialize") + Request(3, "shutdown") + Notification("exit"));
		std::ostringstream MalformedOut;
		const auto Exit = RunTranscript(MalformedIn, MalformedOut);
		const auto MalformedOutput = MalformedOut.str();
		if (!Expect(Exit == 0 && MalformedOutput.find("\"code\":-32700") != std::string::npos &&
		            MalformedOutput.find("\"id\":null") != std::string::npos &&
		            ResponseForId(MalformedOutput, 2).find("capabilities") != std::string::npos &&
		            ResponseForId(MalformedOutput, 3).find("\"result\":null") != std::string::npos,
		        "malformed JSON receives a parse error and subsequent requests remain usable"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// `:` must be registered so clients trigger after `::`, but a request after
		// a lone label/access colon must be suppressed by the server.
		constexpr const char* ScopeUri = "file:///C:/veroscript-tests/lsp-scope.vs";
		std::ostringstream ScopeInput;
		ScopeInput << Request(1, "initialize");
		ScopeInput << Notification("initialized");
		ScopeInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + ScopeUri + "\",\"languageId\":\"veroscript\",\"version\":1," + "\"text\":\"struct P { public:\\n int x;\\n}\\n"
		                                                                                                                  "void main() {\\n P p = P();\\n p::\\n}\\n\"}}");
		ScopeInput << Request(
		    2,
		    "textDocument/completion",
		    std::string("{\"textDocument\":{\"uri\":\"") + ScopeUri + "\"},\"position\":{\"line\":0,\"character\":18},"
		                                                              "\"context\":{\"triggerKind\":2,\"triggerCharacter\":\":\"}}");
		ScopeInput << Request(
		    3,
		    "textDocument/completion",
		    std::string("{\"textDocument\":{\"uri\":\"") + ScopeUri + "\"},\"position\":{\"line\":5,\"character\":4},"
		                                                              "\"context\":{\"triggerKind\":2,\"triggerCharacter\":\":\"}}");
		ScopeInput << Request(4, "shutdown");
		ScopeInput << Notification("exit");

		std::istringstream ScopeIn(ScopeInput.str());
		std::ostringstream ScopeOut;
		const int ScopeExit = RunTranscript(ScopeIn, ScopeOut);
		const std::string ScopeOutput = ScopeOut.str();
		if (!Expect(ScopeExit == 0, "scope completion session should shut down cleanly") || !Expect(Contains(ScopeOutput, "\"items\":[]"), "single-colon trigger returns empty completion") || !Expect(Contains(ScopeOutput, "\"label\":\"x\""), "second-colon trigger returns member completion"))
		{
			std::cerr << ScopeOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Validate the actual wire shape: match rank leads sortText, builtin
		// containers are types, edits replace the typed prefix, and tied/weak
		// matches are not force-preselected by the server.
		constexpr const char* RankingUri =
		    "file:///C:/veroscript-tests/lsp-completion-ranking.vs";
		std::ostringstream RankingInput;
		RankingInput << Request(1, "initialize");
		RankingInput << Notification("initialized");
		RankingInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + RankingUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                                "\"text\":\"void main() { pr }\\n\"}}");
		RankingInput << Request(
		    2,
		    "textDocument/completion",
		    std::string("{\"textDocument\":{\"uri\":\"") + RankingUri + "\"},\"position\":{\"line\":0,\"character\":16}}");
		RankingInput << Notification(
		    "textDocument/didChange",
		    std::string("{\"textDocument\":{\"uri\":\"") + RankingUri + "\",\"version\":2},\"contentChanges\":[{"
		                                                                "\"text\":\"void main() { pq }\\n\"}]}");
		RankingInput << Request(
		    3,
		    "textDocument/completion",
		    std::string("{\"textDocument\":{\"uri\":\"") + RankingUri + "\"},\"position\":{\"line\":0,\"character\":16}}");
		RankingInput << Notification(
		    "textDocument/didChange",
		    std::string("{\"textDocument\":{\"uri\":\"") + RankingUri + "\",\"version\":3},\"contentChanges\":[{"
		                                                                "\"text\":\"void main() { std }\\n\"}]}");
		RankingInput << Request(
		    4,
		    "textDocument/completion",
		    std::string("{\"textDocument\":{\"uri\":\"") + RankingUri + "\"},\"position\":{\"line\":0,\"character\":17}}");
		RankingInput << Notification(
		    "textDocument/didChange",
		    std::string("{\"textDocument\":{\"uri\":\"") + RankingUri + "\",\"version\":4},\"contentChanges\":[{"
		                                                                "\"text\":\"void Select(int value) {}\\n"
		                                                                "void Select(string value) {}\\nvoid main() { Sel }\\n\"}]}");
		RankingInput << Request(
		    5,
		    "textDocument/completion",
		    std::string("{\"textDocument\":{\"uri\":\"") + RankingUri + "\"},\"position\":{\"line\":2,\"character\":17}}");
		RankingInput << Request(6, "shutdown");
		RankingInput << Notification("exit");
		std::istringstream RankingIn(RankingInput.str());
		std::ostringstream RankingOut;
		const int RankingExit = RunTranscript(RankingIn, RankingOut);
		const std::string RankingOutput = RankingOut.str();
		const std::string_view PrResponse = ResponseForId(RankingOutput, 2);
		const std::string_view PrintItem = CompletionItemForLabel(PrResponse, "print");
		const std::string_view PrintfItem = CompletionItemForLabel(PrResponse, "printf");
		const std::string_view PriorityItem =
		    CompletionItemForLabel(PrResponse, "priority_queue");
		const std::string_view PrintSort = JsonStringField(PrintItem, "sortText");
		const std::string_view PrioritySort = JsonStringField(PriorityItem, "sortText");
		const std::string_view PqResponse = ResponseForId(RankingOutput, 3);
		const std::string_view PqItem = CompletionItemForLabel(PqResponse, "priority_queue");
		const std::string_view StdResponse = ResponseForId(RankingOutput, 4);
		const std::string_view StdItem = CompletionItemForLabel(StdResponse, "std");
		const std::string_view OverloadResponse = ResponseForId(RankingOutput, 5);
		const std::string_view FirstOverload =
		    CompletionItemForLabel(OverloadResponse, "Select", 0);
		const std::string_view FirstOverloadSort = JsonStringField(FirstOverload, "sortText");
		if (!Expect(
		        RankingExit == 0 && !PrintItem.empty() && !PrintfItem.empty() && !PriorityItem.empty() && PrResponse.find("\"label\":\"print\"") < PrResponse.find("\"label\":\"priority_queue\"") && PrResponse.find("\"label\":\"printf\"") < PrResponse.find("\"label\":\"priority_queue\"") && Contains(PrintItem, "\"filterText\":\"print\"") && Contains(PrintItem, "\"kind\":3") && Contains(PrintItem, "\"textEdit\":{") && Contains(PrintItem, "\"character\":14") && Contains(PrintItem, "\"character\":16") && Contains(PriorityItem, "\"filterText\":\"priority_queue\"") && Contains(PriorityItem, "\"kind\":22") && Contains(PriorityItem, "\"detail\":\"builtin type\"") && !PrintSort.empty() && !PrioritySort.empty() && PrintSort < PrioritySort && !Contains(PrResponse, "\"preselect\":true") && Contains(PqItem, "\"sortText\":\"03_") && !Contains(PqResponse, "\"preselect\":true") && Contains(StdItem, "\"sortText\":\"00_") && Contains(StdItem, "\"preselect\":true") && !FirstOverload.empty() && CompletionItemForLabel(OverloadResponse, "Select", 1).empty() && Contains(FirstOverload, "int value") && Contains(FirstOverload, "string value") && !FirstOverloadSort.empty(),
		        "completion protocol preserves match-first order, semantic kinds, edits and confidence preselect"))
		{
			std::cerr << RankingOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Completion and semantic coloring must agree even in an unfinished
		// generic type argument. Exercise the JSON-RPC payload, including the
		// committed text, rather than only the internal completion model.
		struct FTypeCompletionCase
		{
			const char* Word;
			int CompletionKind;
			int SemanticKind;
		};
		std::size_t LanguageKeywordCount = 0;
		const char* const* LanguageKeywords =
		    VeroScript::GetLanguageKeywords(LanguageKeywordCount);
		std::vector<FTypeCompletionCase> Cases;
		for (std::size_t Index = 0; Index < LanguageKeywordCount; ++Index)
		{
			const std::string_view Word(LanguageKeywords[Index]);
			if (VeroScript::IsTypeKeyword(Word))
			{
				Cases.push_back({LanguageKeywords[Index], 14, 12});
			}
			else if (VeroScript::IsContainerTypeKeyword(Word))
			{
				Cases.push_back({LanguageKeywords[Index], 22, 1});
			}
		}
		std::ostringstream TypeColorInput;
		TypeColorInput << Request(1, "initialize", SnippetInitializeParams);
		TypeColorInput << Notification("initialized");
		int RequestId = 2;
		for (const FTypeCompletionCase& Case : Cases)
		{
			// These are independent classification cases. Keep every document alive
			// until its asynchronous semantic request has completed.
			const std::string TypeColorUri = "file:///C:/veroscript-tests/lsp-type-color-" + std::string(Case.Word) + ".vs";
			const std::string Source = std::string("void main() { channel<") + Case.Word + " }";
			TypeColorInput << Notification(
			    "textDocument/didOpen",
			    std::string("{\"textDocument\":{\"uri\":\"") + TypeColorUri + "\",\"languageId\":\"veroscript\",\"version\":1,\"text\":\"" + Source + "\"}}");
			const std::size_t WordEnd = Source.find('<') + 1u + std::string_view(Case.Word).size();
			TypeColorInput << Request(
			    RequestId++, "textDocument/completion",
			    std::string("{\"textDocument\":{\"uri\":\"") + TypeColorUri + "\"},\"position\":{\"line\":0,\"character\":" + std::to_string(WordEnd) + "}}");
			TypeColorInput << Request(
			    RequestId++, "textDocument/semanticTokens/full",
			    std::string("{\"textDocument\":{\"uri\":\"") + TypeColorUri + "\"}}");
		}
		TypeColorInput << Request(RequestId, "shutdown");
		TypeColorInput << Notification("exit");
		std::istringstream TypeColorIn(TypeColorInput.str());
		std::ostringstream TypeColorOut;
		const int TypeColorExit = RunTranscript(TypeColorIn, TypeColorOut);
		const std::string TypeColorOutput = TypeColorOut.str();
		RequestId = 2;
		bool bConsistent = TypeColorExit == 0 && Cases.size() >= 7;
		for (const FTypeCompletionCase& Case : Cases)
		{
			const std::string_view Completion = ResponseForId(TypeColorOutput, RequestId++);
			const std::string_view Item = CompletionItemForLabel(Completion, Case.Word);
			const std::vector<FDecodedSemanticToken> Tokens =
			    DecodeSemanticTokens(ResponseForId(TypeColorOutput, RequestId++));
			const std::string Kind = "\"kind\":" + std::to_string(Case.CompletionKind);
			const std::string InsertText = "\"insertText\":\"" + std::string(Case.Word) + "\"";
			const bool bPrimitive = Case.CompletionKind == 14;
			bConsistent = bConsistent && !Item.empty() && CountOccurrences(Completion, "\"label\":\"" + std::string(Case.Word) + "\"") == 1 && Contains(Item, Kind) && Contains(Item, "\"detail\":\"builtin type\"") && Contains(Item, "\"filterText\":\"" + std::string(Case.Word) + "\"") && !JsonStringField(Item, "sortText").empty() && Contains(Item, "\"textEdit\":{") && (bPrimitive ? Contains(Item, InsertText) && Contains(Item, "\"newText\":\"" + std::string(Case.Word) + "\"") && !Contains(Item, "\"insertTextFormat\":2") : Contains(Item, "\"insertTextFormat\":2")) && HasSemanticToken(Tokens, 0, 22, static_cast<int>(std::string_view(Case.Word).size()), Case.SemanticKind);
		}
		if (!Expect(bConsistent,
		        "primitive keyword and container type completion match semantic tokens on the wire"))
		{
			std::cerr << TypeColorOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Container words are named built-in types across completion, hover,
		// navigation and semantic tokens. Primitive spellings remain language
		// keywords in hover and semantic tokens, matching the plugin fallback.
		constexpr const char* BuiltinTypeUri =
		    "file:///C:/veroscript-tests/lsp-builtin-type-consistency.vs";
		std::ostringstream BuiltinTypeInput;
		BuiltinTypeInput << Request(1, "initialize");
		BuiltinTypeInput << Notification("initialized");
		BuiltinTypeInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + BuiltinTypeUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                                    "\"text\":\"void main() { priority_queue<int> values(); }\\n\"}}");
		BuiltinTypeInput << Request(
		    2,
		    "textDocument/hover",
		    std::string("{\"textDocument\":{\"uri\":\"") + BuiltinTypeUri + "\"},\"position\":{\"line\":0,\"character\":16}}");
		BuiltinTypeInput << Request(
		    3,
		    "textDocument/definition",
		    std::string("{\"textDocument\":{\"uri\":\"") + BuiltinTypeUri + "\"},\"position\":{\"line\":0,\"character\":16}}");
		BuiltinTypeInput << Request(
		    4,
		    "textDocument/semanticTokens/full",
		    std::string("{\"textDocument\":{\"uri\":\"") + BuiltinTypeUri + "\"}}");
		BuiltinTypeInput << Request(
		    5,
		    "textDocument/hover",
		    std::string("{\"textDocument\":{\"uri\":\"") + BuiltinTypeUri + "\"},\"position\":{\"line\":0,\"character\":30}}");
		BuiltinTypeInput << Request(6, "shutdown");
		BuiltinTypeInput << Notification("exit");
		std::istringstream BuiltinTypeIn(BuiltinTypeInput.str());
		std::ostringstream BuiltinTypeOut;
		const int BuiltinTypeExit = RunTranscript(BuiltinTypeIn, BuiltinTypeOut);
		const std::string BuiltinTypeOutput = BuiltinTypeOut.str();
		const std::string_view HoverResponse = ResponseForId(BuiltinTypeOutput, 2);
		const std::string_view PrimitiveHoverResponse = ResponseForId(BuiltinTypeOutput, 5);
		const std::string_view DefinitionResponse = ResponseForId(BuiltinTypeOutput, 3);
		const std::vector<FDecodedSemanticToken> BuiltinTypeTokens =
		    DecodeSemanticTokens(ResponseForId(BuiltinTypeOutput, 4));
		if (!Expect(
		        BuiltinTypeExit == 0 && Contains(HoverResponse, "type alias priority_queue") && Contains(HoverResponse, "builtin type") && Contains(HoverResponse, "Built-in VeroScript type.") && !Contains(HoverResponse, "(soft builtin)") && Contains(PrimitiveHoverResponse, "keyword int") && Contains(PrimitiveHoverResponse, "builtin type") && !Contains(PrimitiveHoverResponse, "type alias int") && Contains(DefinitionResponse, "\"result\":[]") && HasSemanticToken(BuiltinTypeTokens, 0, 0, 4, 12) // void keyword
		            && HasSemanticToken(BuiltinTypeTokens, 0, 14, 14, 1) // container type
		            && HasSemanticToken(BuiltinTypeTokens, 0, 29, 3, 12), // int keyword
		        "container type behavior stays consistent while primitive spellings remain keywords"))
		{
			std::cerr << BuiltinTypeOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Exercise the complete language-owned type-word surface over JSON-RPC, not
		// only a representative subset. Scalar spellings are keywords; container
		// spellings are named types.
		struct FTypeWordExpectation
		{
			int Character = 0;
			int Length = 0;
			int Type = 0;
		};
		std::size_t KeywordCount = 0;
		const char* const* Keywords = VeroScript::GetLanguageKeywords(KeywordCount);
		std::string TypeWordSource;
		std::vector<FTypeWordExpectation> TypeWordExpectations;
		for (std::size_t Index = 0; Index < KeywordCount; ++Index)
		{
			const std::string_view Word(Keywords[Index]);
			const bool bPrimitive = VeroScript::IsTypeKeyword(Word);
			const bool bContainer = VeroScript::IsContainerTypeKeyword(Word);
			if (!bPrimitive && !bContainer)
			{
				continue;
			}
			if (!TypeWordSource.empty())
			{
				TypeWordSource.push_back(' ');
			}
			TypeWordExpectations.push_back(FTypeWordExpectation{
			    static_cast<int>(TypeWordSource.size()),
			    static_cast<int>(Word.size()),
			    bContainer ? 1 : 12});
			TypeWordSource.append(Word);
		}
		constexpr const char* TypeWordsUri =
		    "file:///C:/veroscript-tests/lsp-all-type-words.vs";
		std::ostringstream TypeWordsInput;
		TypeWordsInput << Request(1, "initialize");
		TypeWordsInput << Notification("initialized");
		TypeWordsInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + TypeWordsUri + "\",\"languageId\":\"veroscript\",\"version\":1,\"text\":\"" + TypeWordSource + "\"}}");
		TypeWordsInput << Request(
		    2,
		    "textDocument/semanticTokens/full",
		    std::string("{\"textDocument\":{\"uri\":\"") + TypeWordsUri + "\"}}");
		TypeWordsInput << Request(3, "shutdown");
		TypeWordsInput << Notification("exit");
		std::istringstream TypeWordsIn(TypeWordsInput.str());
		std::ostringstream TypeWordsOut;
		const int TypeWordsExit = RunTranscript(TypeWordsIn, TypeWordsOut);
		const std::vector<FDecodedSemanticToken> TypeWordTokens =
		    DecodeSemanticTokens(ResponseForId(TypeWordsOut.str(), 2));
		const bool bAllTypeWordsMatch = std::all_of(
		    TypeWordExpectations.begin(),
		    TypeWordExpectations.end(),
		    [&TypeWordTokens](const FTypeWordExpectation& Expected)
		{
			return HasSemanticToken(
			    TypeWordTokens, 0, Expected.Character, Expected.Length, Expected.Type);
		});
		if (!Expect(
		        TypeWordsExit == 0 && !TypeWordExpectations.empty() && TypeWordTokens.size() == TypeWordExpectations.size() && bAllTypeWordsMatch,
		        "every primitive and container keyword uses the agreed semantic token kind"))
		{
			std::cerr << TypeWordsOut.str() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Lexically unambiguous strings, numbers and comments keep their semantic
		// colors even though they are not declaration symbols.
		constexpr const char* LiteralUri = "file:///C:/veroscript-tests/lsp-literals.vs";
		std::ostringstream LiteralInput;
		LiteralInput << Request(1, "initialize");
		LiteralInput << Notification("initialized");
		LiteralInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + LiteralUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                                "\"text\":\"// semantic comment\\nvoid main() { string text = \\\"ok\\\"; int value = 42; }\\n\"}}");
		LiteralInput << Request(
		    2, "textDocument/semanticTokens/full",
		    std::string("{\"textDocument\":{\"uri\":\"") + LiteralUri + "\"}}");
		LiteralInput << Request(3, "shutdown");
		LiteralInput << Notification("exit");
		std::istringstream LiteralIn(LiteralInput.str());
		std::ostringstream LiteralOut;
		const int LiteralExit = RunTranscript(LiteralIn, LiteralOut);
		const std::string LiteralOutput = LiteralOut.str();
		if (!Expect(
		        LiteralExit == 0 && Contains(LiteralOutput, ",17,0") && Contains(LiteralOutput, ",18,0") && Contains(LiteralOutput, ",19,0"),
		        "semantic tokens include string, number and comment categories"))
		{
			std::cerr << LiteralOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	for (const auto& [MessageJson, MessageLength] : std::vector<std::pair<std::string, std::uint32_t>>{
	         {"this macro // message", 21},
	         {"this macro /* message", 21},
	         {"this macro r\\\"\\u4e2d\\ud83d\\ude00", 16}})
	{
		// Diagnostic directive arguments are textual even when they contain language
		// keywords, quotes or comment markers. Preserve physical CRLF coordinates.
		constexpr const char* DirectiveUri = "file:///C:/veroscript-tests/lsp-directive-text.vs";
		std::ostringstream DirectiveInput;
		DirectiveInput << Request(1, "initialize");
		DirectiveInput << Notification("initialized");
		DirectiveInput << Notification("textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + DirectiveUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                                  "\"text\":\"#define VALUE 3\\r\\n#if 0\\r\\n#error " +
		        MessageJson + "\\r\\n#endif\\r\\n"
		                      "void main() { print(VALUE); }\\r\\n\"}}");
		DirectiveInput << Request(2, "textDocument/semanticTokens/full",
		    std::string("{\"textDocument\":{\"uri\":\"") + DirectiveUri + "\"}}");
		DirectiveInput << Request(3, "shutdown");
		DirectiveInput << Notification("exit");
		std::istringstream DirectiveIn(DirectiveInput.str());
		std::ostringstream DirectiveOut;
		const int DirectiveExit = RunTranscript(DirectiveIn, DirectiveOut);
		const auto DirectiveTokens = DecodeSemanticTokens(ResponseForId(DirectiveOut.str(), 2));
		if (!Expect(DirectiveExit == 0 && HasSemanticToken(DirectiveTokens, 2, 0, 6, 20) && HasSemanticToken(DirectiveTokens, 2, 7, MessageLength, 17) && HasSemanticToken(DirectiveTokens, 3, 0, 6, 20) && HasSemanticToken(DirectiveTokens, 4, 0, 4, 12) && HasSemanticToken(DirectiveTokens, 4, 20, 5, 11) && std::count_if(DirectiveTokens.begin(), DirectiveTokens.end(), [](const FDecodedSemanticToken& Token)
		{
			return Token.Line == 2;
		}) == 2,
		        "error directive messages must not be tokenized as ordinary code or comments"))
		{
			std::cerr << DirectiveOut.str() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Raw strings may cross physical lines and may contain comment markers. LSP
		// semantic tokens must be split per line, and the comment fallback must not
		// reinterpret raw-string contents. Character literals use the string class.
		constexpr const char* RawLiteralUri =
		    "file:///C:/veroscript-tests/lsp-raw-literals.vs";
		std::ostringstream RawLiteralInput;
		RawLiteralInput << Request(1, "initialize");
		RawLiteralInput << Notification("initialized");
		RawLiteralInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + RawLiteralUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                                   "\"text\":\"void main() {\\n    string raw = r\\\"first\\rstill first line\\n"
		                                                                   "// raw, not comment\\n#define FAKE 1\\nlast\\\";\\n    char value = 'A';\\n}\\n\"}}");
		RawLiteralInput << Request(
		    2,
		    "textDocument/semanticTokens/full",
		    std::string("{\"textDocument\":{\"uri\":\"") + RawLiteralUri + "\"}}");
		RawLiteralInput << Request(3, "shutdown");
		RawLiteralInput << Notification("exit");
		std::istringstream RawLiteralIn(RawLiteralInput.str());
		std::ostringstream RawLiteralOut;
		const int RawLiteralExit = RunTranscript(RawLiteralIn, RawLiteralOut);
		const std::string RawLiteralOutput = RawLiteralOut.str();
		if (!Expect(
		        RawLiteralExit == 0 && CountOccurrences(RawLiteralOutput, ",17,0") == 5 && !Contains(RawLiteralOutput, ",19,0") && !Contains(RawLiteralOutput, ",20,0"),
		        "raw and character literals emit one string token per physical line without false comments or directives"))
		{
			std::cerr << RawLiteralOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// The LSP response must contain a complete lexical baseline in addition to
		// semantic identifiers. Primitive type keywords use `keyword`, matching the
		// plugin lexical fallback; container type words use `type`. The decoded
		// positions are UTF-16 positions.
		constexpr const char* BaselineUri =
		    "file:///C:/veroscript-tests/lsp-lexical-baseline.vs";
		std::ostringstream BaselineInput;
		BaselineInput << Request(1, "initialize");
		BaselineInput << Notification("initialized");
		BaselineInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + BaselineUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                                 "\"text\":\"#define LIMIT 3\\n"
		                                                                 "const int seed = 0;\\nint main()\\n{\\nchar marker = 'x';\\nprint(\\\"😀\\\");\\n"
		                                                                 "if (true) return LIMIT; // done\\n}\\n\"}}");
		BaselineInput << Request(
		    2, "textDocument/semanticTokens/full",
		    std::string("{\"textDocument\":{\"uri\":\"") + BaselineUri + "\"}}");
		BaselineInput << Request(
		    3, "textDocument/semanticTokens/full/delta",
		    std::string("{\"textDocument\":{\"uri\":\"") + BaselineUri + "\"},\"previousResultId\":\"1\"}");
		BaselineInput << Request(4, "shutdown");
		BaselineInput << Notification("exit");
		std::istringstream BaselineIn(BaselineInput.str());
		std::ostringstream BaselineOut;
		const int BaselineExit = RunTranscript(BaselineIn, BaselineOut);
		const std::string BaselineOutput = BaselineOut.str();
		const std::string_view FullResponse = ResponseForId(BaselineOutput, 2);
		const std::vector<FDecodedSemanticToken> Tokens = DecodeSemanticTokens(FullResponse);
		if (!Expect(
		        BaselineExit == 0 && SemanticTokensDoNotOverlap(Tokens) && HasSemanticToken(Tokens, 0, 0, 7, 20) // #define
		            && HasSemanticToken(Tokens, 0, 8, 5, 11) // LIMIT
		            && HasSemanticToken(Tokens, 0, 14, 1, 18) // 3
		            && HasSemanticToken(Tokens, 1, 0, 5, 12) // const modifier
		            && HasSemanticToken(Tokens, 1, 6, 3, 12) // int keyword
		            && HasSemanticToken(Tokens, 2, 0, 3, 12) // int keyword
		            && HasSemanticToken(Tokens, 2, 4, 4, 9) // main
		            && HasSemanticToken(Tokens, 4, 0, 4, 12) // char keyword
		            && HasSemanticToken(Tokens, 4, 14, 3, 17) // character literal
		            && HasSemanticToken(Tokens, 5, 0, 5, 9) // print
		            && HasSemanticToken(Tokens, 5, 6, 4, 17) // "😀" in UTF-16
		            && HasSemanticToken(Tokens, 6, 0, 2, 12) // if
		            && HasSemanticToken(Tokens, 6, 4, 4, 12) // true
		            && HasSemanticToken(Tokens, 6, 10, 6, 12) // return
		            && HasSemanticToken(Tokens, 6, 17, 5, 11) // macro use
		            && HasSemanticToken(Tokens, 6, 24, 7, 19) // comment
		            && Contains(
		                   ResponseForId(BaselineOutput, 3),
		                   "\"edits\":[]"),
		        "semantic lexical baseline distinguishes primitive keywords from named types and covers literals, comments, macros and UTF-16 positions"))
		{
			std::cerr << BaselineOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// An unfinished string stops the primary lexer. Lexically certain tokens on
		// later lines must still be published while the user continues editing.
		constexpr const char* IncompleteUri =
		    "file:///C:/veroscript-tests/lsp-incomplete-semantic.vs";
		std::ostringstream IncompleteInput;
		IncompleteInput << Request(1, "initialize");
		IncompleteInput << Notification("initialized");
		IncompleteInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + IncompleteUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                                   "\"text\":\"void main() { int value = 42; string text = "
		                                                                   "\\\"unfinished\\nreturn 7u64;\\nint binary = 0b1f;\\n\"}}");
		IncompleteInput << Request(
		    2, "textDocument/semanticTokens/full",
		    std::string("{\"textDocument\":{\"uri\":\"") + IncompleteUri + "\"}}");
		IncompleteInput << Request(3, "shutdown");
		IncompleteInput << Notification("exit");
		std::istringstream IncompleteIn(IncompleteInput.str());
		std::ostringstream IncompleteOut;
		const int IncompleteExit = RunTranscript(IncompleteIn, IncompleteOut);
		const std::string IncompleteOutput = IncompleteOut.str();
		const std::vector<FDecodedSemanticToken> IncompleteTokens =
		    DecodeSemanticTokens(ResponseForId(IncompleteOutput, 2));
		if (!Expect(
		        IncompleteExit == 0 && HasSemanticToken(IncompleteTokens, 0, 14, 3, 12) // int keyword
		            && HasSemanticToken(IncompleteTokens, 0, 26, 2, 18) // 42
		            && HasSemanticToken(IncompleteTokens, 0, 30, 6, 12) // string keyword
		            && HasSemanticToken(IncompleteTokens, 0, 44, 11, 17) // unfinished string
		            && HasSemanticToken(IncompleteTokens, 1, 0, 6, 12) // return after error
		            && HasSemanticToken(IncompleteTokens, 1, 7, 4, 18) // suffixed number after error
		            && HasSemanticToken(IncompleteTokens, 2, 0, 3, 12) // keyword after error
		            && HasSemanticToken(IncompleteTokens, 2, 13, 3, 18) // recover valid 0b1 prefix
		            && !HasSemanticToken(IncompleteTokens, 2, 13, 4, 18), // do not absorb invalid f
		        "incomplete source recovers primitive keywords and literal tokens after a lexer error"))
		{
			std::cerr << IncompleteOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Constants use the custom semantic token slot rather than sharing the
		// standard macro slot; the IDE assigns them independent Rider colors.
		constexpr const char* ConstantUri =
		    "file:///C:/veroscript-tests/lsp-constant-semantic.vs";
		std::ostringstream ConstantInput;
		ConstantInput << Request(1, "initialize");
		ConstantInput << Notification("initialized");
		ConstantInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + ConstantUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                                 "\"text\":\"const int Max = 10;\\n"
		                                                                 "void main() { print(Max); }\\n\"}}");
		ConstantInput << Request(
		    2,
		    "textDocument/semanticTokens/full",
		    std::string("{\"textDocument\":{\"uri\":\"") + ConstantUri + "\"}}");
		ConstantInput << Request(3, "shutdown");
		ConstantInput << Notification("exit");
		std::istringstream ConstantIn(ConstantInput.str());
		std::ostringstream ConstantOut;
		const int ConstantExit =
		    RunTranscript(ConstantIn, ConstantOut);
		const std::string ConstantOutput = ConstantOut.str();
		const std::vector<FDecodedSemanticToken> ConstantTokens =
		    DecodeSemanticTokens(ResponseForId(ConstantOutput, 2));
		if (!Expect(ConstantExit == 0, "constant semantic LSP session shuts down") || !Expect(HasSemanticToken(ConstantTokens, 0, 10, 3, 15), "constants use the dedicated semantic token type"))
		{
			std::cerr << ConstantOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* MacroUri = "file:///C:/veroscript-tests/lsp-host-macro.vs";
		std::ostringstream MacroInput;
		MacroInput << Request(1, "initialize");
		MacroInput << Notification("initialized");
		MacroInput << Notification(
		    "workspace/didChangeConfiguration",
		    "{\"settings\":{\"veroscript\":{\"predefinedMacros\":[{"
		    "\"name\":\"HOST_LOG\",\"parameters\":[\"format\"],"
		    "\"variadic\":true,\"body\":\"print(format, __VA_ARGS__)\","
		    "\"sourceName\":\"lsp-host\"}]}}}");
		MacroInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + MacroUri + "\",\"languageId\":\"veroscript\",\"version\":1," + "\"text\":\"void main() {\\n    HOST_\\n}\\n\"}}");
		MacroInput << Request(
		    2,
		    "textDocument/completion",
		    std::string("{\"textDocument\":{\"uri\":\"") + MacroUri + "\"},\"position\":{\"line\":1,\"character\":9}}");
		MacroInput << Request(3, "shutdown");
		MacroInput << Notification("exit");
		std::istringstream MacroIn(MacroInput.str());
		std::ostringstream MacroOut;
		const int MacroExit = RunTranscript(MacroIn, MacroOut);
		const std::string MacroOutput = MacroOut.str();
		if (!Expect(MacroExit == 0, "host macro LSP session shuts down") || !Expect(Contains(MacroOutput, "\"label\":\"HOST_LOG\"") && Contains(MacroOutput, "\"insertText\":\"HOST_LOG(\"") && Contains(MacroOutput, "\"veroscriptKind\":\"macro\""), "LSP configuration exposes predefined macros to completion"))
		{
			std::cerr << MacroOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// The IDE sends plural catalog paths through didChangeConfiguration. Verify
		// that host annotations/functions then reach both completion and hover.
		const auto Unique =
		    std::chrono::steady_clock::now().time_since_epoch().count();
		const std::filesystem::path AnnotationCatalogPath =
		    std::filesystem::temp_directory_path() / ("veroscript-lsp-annotations-" + std::to_string(Unique) + ".json");
		const std::filesystem::path FunctionCatalogPath =
		    std::filesystem::temp_directory_path() / ("veroscript-lsp-functions-" + std::to_string(Unique) + ".json");
		{
			std::ofstream Catalog(AnnotationCatalogPath, std::ios::binary | std::ios::trunc);
			Catalog
			    << "{\"version\":1,\"functions\":[],\"types\":[],"
			       "\"annotations\":[{\"name\":\"UFUNCTION\","
			       "\"detail\":\"annotation (BlueprintCallable)\","
			       "\"arguments\":[{\"name\":\"BlueprintCallable\",\"detail\":\"flag\"}]}]}";
		}
		{
			std::ofstream Catalog(FunctionCatalogPath, std::ios::binary | std::ios::trunc);
			Catalog
			    << "{\"version\":1,\"functions\":[{\"name\":\"host_add\","
			       "\"annotations\":[{\"name\":\"HOST_API\",\"arguments\":["
			       "{\"name\":\"Category\",\"value\":\"Math\"}]}],"
			       "\"paramAnnotations\":[[{\"name\":\"HOST_PARAM\","
			       "\"arguments\":[{\"name\":\"DisplayName\",\"value\":\"Left\"}]}],[]],"
			       "\"returnType\":\"int\",\"paramTypes\":[\"int\",\"int\"],"
			       "\"returnHostTypeName\":\"\",\"paramHostTypeNames\":[]}],"
			       "\"genericTypeTemplates\":[{\"name\":\"HostPair\",\"arity\":2,"
			       "\"documentation\":\"A generic host pair.\"}],"
			       "\"types\":[{\"name\":\"HostWidget\",\"kind\":\"class\","
			       "\"annotations\":[{\"name\":\"HOST_TYPE\",\"arguments\":["
			       "{\"name\":\"Reflected\"}]}],"
			       "\"members\":[{\"kind\":\"staticConstant\",\"name\":\"Version\","
			       "\"annotations\":[{\"name\":\"HOST_PROPERTY\",\"arguments\":["
			       "{\"name\":\"ReadOnly\"}]}],"
			       "\"documentation\":\"Host widget version.\",\"returnType\":\"int\"}]}],"
			       "\"annotations\":[]}";
		}

		constexpr const char* BindingsUri =
		    "file:///C:/veroscript-tests/lsp-bindings.vs";
		const std::string AnnotationCatalogJsonPath =
		    AnnotationCatalogPath.generic_string();
		const std::string FunctionCatalogJsonPath =
		    FunctionCatalogPath.generic_string();
		std::ostringstream BindingsInput;
		BindingsInput << Request(1, "initialize", SnippetInitializeParams);
		BindingsInput << Notification("initialized");
		BindingsInput << Notification(
		    "workspace/didChangeConfiguration",
		    std::string("{\"settings\":{\"veroscript\":{\"bindingCatalogPaths\":[\"") + AnnotationCatalogJsonPath + "\",\"" + FunctionCatalogJsonPath + "\"]}}}");
		BindingsInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + BindingsUri + "\",\"languageId\":\"veroscript\",\"version\":1," + "\"text\":\"UFUNCTION(BlueprintCallable)\\n"
		                                                                                                                     "int HostCall() { return host_add(1, 2); }\\n"
		                                                                                                                     "void Use(HostWidget value) {}\\n"
		                                                                                                                     "void More() { HostPa; }\\n"
		                                                                                                                     "void Read() { HostWidget.Version; }\\n\"}}");
		BindingsInput << Request(
		    2,
		    "textDocument/completion",
		    std::string("{\"textDocument\":{\"uri\":\"") + BindingsUri + "\"},\"position\":{\"line\":0,\"character\":3}}");
		BindingsInput << Request(
		    3,
		    "textDocument/completion",
		    std::string("{\"textDocument\":{\"uri\":\"") + BindingsUri + "\"},\"position\":{\"line\":1,\"character\":32}}");
		BindingsInput << Request(
		    4,
		    "textDocument/completion",
		    std::string("{\"textDocument\":{\"uri\":\"") + BindingsUri + "\"},\"position\":{\"line\":2,\"character\":13}}");
		BindingsInput << Request(
		    5,
		    "textDocument/hover",
		    std::string("{\"textDocument\":{\"uri\":\"") + BindingsUri + "\"},\"position\":{\"line\":0,\"character\":3}}");
		BindingsInput << Request(
		    6,
		    "textDocument/hover",
		    std::string("{\"textDocument\":{\"uri\":\"") + BindingsUri + "\"},\"position\":{\"line\":1,\"character\":27}}");
		BindingsInput << Request(
		    7,
		    "textDocument/hover",
		    std::string("{\"textDocument\":{\"uri\":\"") + BindingsUri + "\"},\"position\":{\"line\":2,\"character\":13}}");
		BindingsInput << Request(
		    8,
		    "textDocument/semanticTokens/full",
		    std::string("{\"textDocument\":{\"uri\":\"") + BindingsUri + "\"}}");
		BindingsInput << Request(
		    9,
		    "textDocument/references",
		    std::string("{\"textDocument\":{\"uri\":\"") + BindingsUri + "\"},\"position\":{\"line\":1,\"character\":27}," + "\"context\":{\"includeDeclaration\":true}}");
		BindingsInput << Request(
		    10,
		    "textDocument/references",
		    std::string("{\"textDocument\":{\"uri\":\"") + BindingsUri + "\"},\"position\":{\"line\":2,\"character\":13}," + "\"context\":{\"includeDeclaration\":true}}");
		BindingsInput << Request(
		    11,
		    "textDocument/completion",
		    std::string("{\"textDocument\":{\"uri\":\"") + BindingsUri + "\"},\"position\":{\"line\":3,\"character\":20}}");
		BindingsInput << Request(
		    12,
		    "textDocument/completion",
		    std::string("{\"textDocument\":{\"uri\":\"") + BindingsUri + "\"},\"position\":{\"line\":4,\"character\":25}}");
		BindingsInput << Request(
		    13,
		    "textDocument/hover",
		    std::string("{\"textDocument\":{\"uri\":\"") + BindingsUri + "\"},\"position\":{\"line\":4,\"character\":27}}");
		BindingsInput << Request(14, "shutdown");
		BindingsInput << Notification("exit");

		std::istringstream BindingsIn(BindingsInput.str());
		std::ostringstream BindingsOut;
		const int BindingsExit =
		    RunTranscript(BindingsIn, BindingsOut);
		const std::string BindingsOutput = BindingsOut.str();
		std::error_code RemoveError;
		std::filesystem::remove(AnnotationCatalogPath, RemoveError);
		RemoveError.clear();
		std::filesystem::remove(FunctionCatalogPath, RemoveError);
		if (!Expect(BindingsExit == 0, "bindings LSP session shuts down") || !Expect(Contains(BindingsOutput, "\"label\":\"UFUNCTION\""), "catalog annotation reaches completion") || !Expect(Contains(BindingsOutput, "\"label\":\"host_add\""), "catalog host function reaches completion") || !Expect(Contains(BindingsOutput, "\"label\":\"HostWidget\""), "catalog host type reaches completion") || !Expect(Contains(BindingsOutput, "annotation (BlueprintCallable)"), "catalog annotation reaches hover") || !Expect(Contains(BindingsOutput, "function host_add"), "catalog host function reaches hover") || !Expect(Contains(BindingsOutput, "HOST_API(Category=\\\"Math\\\")") && Contains(BindingsOutput, "HOST_PARAM(DisplayName=\\\"Left\\\")"), "concrete function and parameter annotations reach LSP metadata") || !Expect(Contains(BindingsOutput, "class HostWidget"), "catalog host type reaches hover") || !Expect(Contains(BindingsOutput, "HOST_TYPE(Reflected)"), "concrete host type annotation reaches LSP metadata") || !Expect(Contains(BindingsOutput, "\"label\":\"HostPair\"") && Contains(BindingsOutput, "\"insertText\":\"HostPair<${1}, ${2}>\"") && Contains(BindingsOutput, "\"insertTextFormat\":2"), "generic host template reaches LSP completion as a snippet") || !Expect(Contains(BindingsOutput, "\"label\":\"Version\""), "static host constant reaches qualified LSP completion") || !Expect(Contains(BindingsOutput, "Host widget version."), "static host constant documentation reaches LSP hover") || !Expect(Contains(BindingsOutput, "HOST_PROPERTY(ReadOnly)"), "concrete host member annotation reaches LSP metadata") || !Expect(Contains(BindingsOutput, "[0,0,9,13,0,0,10,17,14,0"), "annotation name and argument use distinct semantic token types") || !Expect(Contains(BindingsOutput, "\"id\":9,\"jsonrpc\":\"2.0\",\"result\":[{"), "host function exposes workspace references") || !Expect(Contains(BindingsOutput, "\"id\":10,\"jsonrpc\":\"2.0\",\"result\":[{"), "host type exposes workspace references"))
		{
			std::cerr << BindingsOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Catalogs that contain only aliases/templates are still meaningful tooling
		// inputs and must not be discarded by the LSP's empty-catalog fast path.
		const auto Unique =
		    std::chrono::steady_clock::now().time_since_epoch().count();
		const std::filesystem::path CatalogPath =
		    std::filesystem::temp_directory_path() / ("veroscript-lsp-type-metadata-" + std::to_string(Unique) + ".json");
		{
			std::ofstream Catalog(CatalogPath, std::ios::binary | std::ios::trunc);
			Catalog
			    << "{\"version\":1,\"functions\":[],\"types\":[],"
			       "\"genericTypeTemplates\":[{\"name\":\"HostOnly\",\"arity\":1}],"
			       "\"typeAliases\":[{\"name\":\"HostAlias\",\"typeParams\":[],"
			       "\"targetTypeName\":\"int\"}]}";
		}
		constexpr const char* MetadataUri =
		    "file:///C:/veroscript-tests/lsp-type-metadata-only.vs";
		std::ostringstream MetadataInput;
		MetadataInput << Request(1, "initialize", SnippetInitializeParams);
		MetadataInput << Notification("initialized");
		MetadataInput << Notification(
		    "workspace/didChangeConfiguration",
		    std::string("{\"settings\":{\"veroscript\":{\"bindingCatalogPaths\":[\"") + CatalogPath.generic_string() + "\"]}}}");
		MetadataInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + MetadataUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                                 "\"text\":\"void main() {\\n  HostOn;\\n  HostAl;\\n}\\n\"}}");
		MetadataInput << Request(
		    2,
		    "textDocument/completion",
		    std::string("{\"textDocument\":{\"uri\":\"") + MetadataUri + "\"},\"position\":{\"line\":1,\"character\":8}}");
		MetadataInput << Request(
		    3,
		    "textDocument/completion",
		    std::string("{\"textDocument\":{\"uri\":\"") + MetadataUri + "\"},\"position\":{\"line\":2,\"character\":8}}");
		MetadataInput << Request(4, "shutdown");
		MetadataInput << Notification("exit");
		std::istringstream MetadataIn(MetadataInput.str());
		std::ostringstream MetadataOut;
		const int MetadataExit = RunTranscript(MetadataIn, MetadataOut);
		const std::string MetadataOutput = MetadataOut.str();
		std::error_code RemoveError;
		std::filesystem::remove(CatalogPath, RemoveError);
		if (!Expect(MetadataExit == 0, "type-metadata-only LSP session shuts down") || !Expect(Contains(MetadataOutput, "\"label\":\"HostOnly\"") && Contains(MetadataOutput, "\"insertText\":\"HostOnly<${1}>\"") && Contains(MetadataOutput, "\"insertTextFormat\":2"), "template-only catalog reaches LSP completion") || !Expect(Contains(MetadataOutput, "\"label\":\"HostAlias\""), "alias-only catalog reaches LSP completion"))
		{
			std::cerr << MetadataOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		const auto Unique =
		    std::chrono::steady_clock::now().time_since_epoch().count();
		const std::filesystem::path Root =
		    std::filesystem::temp_directory_path() / ("veroscript-lsp-implicit-" + std::to_string(Unique));
		std::error_code FileError;
		std::filesystem::create_directories(Root, FileError);
		const std::filesystem::path SharedPath = Root / "shared.vs";
		const std::filesystem::path MainPath = Root / "main.vs";
		{
			std::ofstream Shared(SharedPath);
			Shared << "int project_value() { return 42; }\n";
		}
		const std::string SharedJsonPath = SharedPath.generic_string();
		const std::string MainUri = "file:///" + MainPath.generic_string();
		std::ostringstream ProjectInput;
		ProjectInput << Request(1, "initialize");
		ProjectInput << Notification("initialized");
		ProjectInput << Notification(
		    "workspace/didChangeConfiguration",
		    std::string("{\"settings\":{\"veroscript\":{") + "\"implicitImportPaths\":[\"" + SharedJsonPath + "\"]," + "\"allowCircularImports\":false," + "\"allowProjectWideAccess\":true}}}");
		ProjectInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + MainUri + "\",\"languageId\":\"veroscript\",\"version\":1," + "\"text\":\"void main() { print(project_value()); }\\n\"}}");
		ProjectInput << Request(
		    2,
		    "textDocument/documentSymbol",
		    std::string("{\"textDocument\":{\"uri\":\"") + MainUri + "\"}}");
		ProjectInput << Request(3, "shutdown");
		ProjectInput << Notification("exit");

		std::istringstream ProjectIn(ProjectInput.str());
		std::ostringstream ProjectOut;
		const int ProjectExit = RunTranscript(ProjectIn, ProjectOut);
		const std::string ProjectOutput = ProjectOut.str();
		std::filesystem::remove_all(Root, FileError);
		if (!Expect(ProjectExit == 0, "implicit-import LSP session shuts down") || !Expect(!Contains(ProjectOutput, "unknown function: project_value"), "LSP applies implicit imports and project visibility") || !Expect(Contains(ProjectOutput, "\"id\":2,\"jsonrpc\":\"2.0\",\"result\":[{"), "LSP remains analyzable after module-loading configuration"))
		{
			std::cerr << ProjectOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// A host snapshot can briefly disappear or be incomplete while it is
		// refreshed. Continue using the last successfully loaded bindings.
		const auto Unique = std::chrono::steady_clock::now().time_since_epoch().count();
		const std::filesystem::path GoodPath = std::filesystem::temp_directory_path() / ("veroscript-lsp-last-good-" + std::to_string(Unique) + ".json");
		const std::filesystem::path MissingPath = std::filesystem::temp_directory_path() / ("veroscript-lsp-offline-" + std::to_string(Unique) + ".json");
		{
			std::ofstream Catalog(GoodPath, std::ios::binary | std::ios::trunc);
			Catalog << "{\"version\":1,\"functions\":[],\"types\":[],"
			           "\"genericTypeTemplates\":[{\"name\":\"LastGoodHostType\",\"arity\":1}]}";
		}
		constexpr const char* HostUri = "file:///C:/veroscript-tests/lsp-last-good.vs";
		std::ostringstream LastGoodInput;
		LastGoodInput << Request(1, "initialize", "{\"locale\":\"en-US\"}");
		LastGoodInput << Notification("initialized");
		LastGoodInput << Notification("workspace/didChangeConfiguration",
		    "{\"settings\":{\"veroscript\":{\"bindingCatalogPaths\":[" + JsonQuote(GoodPath.generic_string()) + "]}}}");
		LastGoodInput << Notification("textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + HostUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                             "\"text\":\"void main() { LastGoodHostTy; }\"}}");
		const std::string CompletionParams =
		    std::string("{\"textDocument\":{\"uri\":\"") + HostUri + "\"},\"position\":{\"line\":0,\"character\":28}}";
		LastGoodInput << Request(2, "textDocument/completion", CompletionParams);
		LastGoodInput << Notification("workspace/didChangeConfiguration",
		    "{\"settings\":{\"veroscript\":{\"bindingCatalogPaths\":[" + JsonQuote(MissingPath.generic_string()) + "]}}}");
		LastGoodInput << Request(3, "textDocument/completion", CompletionParams);
		LastGoodInput << Request(4, "shutdown");
		LastGoodInput << Notification("exit");
		std::istringstream LastGoodIn(LastGoodInput.str());
		std::ostringstream LastGoodOut;
		const int Exit = RunTranscript(LastGoodIn, LastGoodOut);
		std::error_code Ec;
		std::filesystem::remove(GoodPath, Ec);
		if (!Expect(Exit == 0 && Contains(ResponseForId(LastGoodOut.str(), 2), "\"label\":\"LastGoodHostType\"") && Contains(ResponseForId(LastGoodOut.str(), 3), "\"label\":\"LastGoodHostType\"") && Contains(LastGoodOut.str(), "\"method\":\"window/logMessage\"") && Contains(LastGoodOut.str(), "using the last valid snapshot"),
		        "offline host metadata preserves last successfully loaded bindings"))
		{
			std::cerr << LastGoodOut.str() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// A missing catalog should report once and then remain stable until its
		// filesystem state changes. Re-querying must not reload/reanalyze forever.
		const auto Unique =
		    std::chrono::steady_clock::now().time_since_epoch().count();
		const std::filesystem::path MissingCatalogPath =
		    std::filesystem::temp_directory_path() / ("veroscript-lsp-missing-" + std::to_string(Unique) + ".json");
		std::error_code RemoveError;
		std::filesystem::remove(MissingCatalogPath, RemoveError);
		const std::string MissingCatalogJsonPath =
		    MissingCatalogPath.generic_string();

		std::ostringstream MissingInput;
		MissingInput << Request(1, "initialize", "{\"locale\":\"en-US\"}");
		MissingInput << Notification("initialized");
		MissingInput << Notification(
		    "workspace/didChangeConfiguration",
		    std::string("{\"settings\":{\"veroscript\":{\"bindingCatalogPaths\":[\"") + MissingCatalogJsonPath + "\"]}}}");
		MissingInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + Uri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                         "\"text\":\"void main() {}\\n\"}}");
		MissingInput << Request(
		    2,
		    "textDocument/completion",
		    std::string("{\"textDocument\":{\"uri\":\"") + Uri + "\"},\"position\":{\"line\":0,\"character\":5}}");
		MissingInput << Request(
		    3,
		    "textDocument/hover",
		    std::string("{\"textDocument\":{\"uri\":\"") + Uri + "\"},\"position\":{\"line\":0,\"character\":5}}");
		MissingInput << Request(4, "shutdown");
		MissingInput << Notification("exit");

		std::istringstream MissingIn(MissingInput.str());
		std::ostringstream MissingOut;
		std::ostringstream MissingErrors;
		std::streambuf* const PreviousErrors = std::cerr.rdbuf(MissingErrors.rdbuf());
		const int MissingExit = RunTranscript(MissingIn, MissingOut);
		std::cerr.rdbuf(PreviousErrors);
		const std::string ErrorText = MissingErrors.str();
		const std::size_t FirstError = ErrorText.find("binding catalog '");
		const bool bOnlyOneError =
		    FirstError != std::string::npos && ErrorText.find("binding catalog '", FirstError + 1) == std::string::npos;
		if (!Expect(MissingExit == 0, "missing-catalog LSP session shuts down") || !Expect(bOnlyOneError, "unchanged missing catalog is reported only once") || !Expect(Contains(MissingOut.str(), "no host bindings are loaded"), "initially missing host bindings report their true offline state"))
		{
			std::cerr << MissingOut.str() << '\n'
			          << ErrorText << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// documentSymbol must distinguish a transient parse failure from a valid
		// source file with no declarations. The IDE retains its last-known-good
		// outline for null, while an empty array deliberately clears it.
		std::ostringstream OutlineInput;
		OutlineInput << Request(1, "initialize");
		OutlineInput << Notification("initialized");
		OutlineInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + Uri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                         "\"text\":\"class Broken {\\n\"}}");
		OutlineInput << Request(
		    2,
		    "textDocument/documentSymbol",
		    std::string("{\"textDocument\":{\"uri\":\"") + Uri + "\"}}");
		OutlineInput << Notification(
		    "textDocument/didChange",
		    std::string("{\"textDocument\":{\"uri\":\"") + Uri + "\",\"version\":2},\"contentChanges\":[{\"text\":\"// empty\\n\"}]}");
		OutlineInput << Request(
		    3,
		    "textDocument/documentSymbol",
		    std::string("{\"textDocument\":{\"uri\":\"") + Uri + "\"}}");
		OutlineInput << Request(4, "shutdown");
		OutlineInput << Notification("exit");

		std::istringstream OutlineIn(OutlineInput.str());
		std::ostringstream OutlineOut;
		const int OutlineExit = RunTranscript(OutlineIn, OutlineOut);
		const std::string OutlineOutput = OutlineOut.str();
		if (!Expect(OutlineExit == 0, "outline contract session shuts down") || !Expect(Contains(OutlineOutput, "\"id\":2,\"jsonrpc\":\"2.0\",\"result\":null"), "failed outline parse returns null") || !Expect(Contains(OutlineOutput, "\"id\":3,\"jsonrpc\":\"2.0\",\"result\":[]"), "valid empty outline returns an empty array"))
		{
			std::cerr << OutlineOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	for (const bool PriorError : {false, true})
	{
		// Neither an authoritative empty result nor a failed live parse may
		// resurrect a previous receiver just because the member token is unchanged.
		constexpr const char* Uri = "file:///C:/veroscript-tests/lsp-live-hover-empty.vs";
		const std::string Before = std::string(PriorError ? "int marker=1;\n" : "") + "class A{public:int value;};\n"
		                                                                              "void main(){A a=A();print(a.value);print(\"ok\");}";
		const std::string After = std::string(PriorError ? "int marker= ;\n" : "") + "class A{public:int value;};\n"
		                                                                             "void main(){A a=A();print(x.value);print(\"unfinished);}";
		const auto Column = After.substr(After.rfind('\n') + 1).find("value") + 2u;
		const std::string Params = std::string("{\"textDocument\":{\"uri\":") + JsonQuote(Uri) + "},\"position\":{\"line\":" + std::to_string(PriorError ? 2 : 1) + ",\"character\":" + std::to_string(Column) + "}}";
		std::ostringstream Transcript;
		Transcript << Request(1, "initialize") << Notification("initialized")
		           << Notification("textDocument/didOpen", std::string("{\"textDocument\":{\"uri\":") + JsonQuote(Uri) + ",\"languageId\":\"veroscript\",\"version\":1,\"text\":" + JsonQuote(Before) + "}}")
		           << Request(2, "textDocument/hover", Params)
		           << Notification("textDocument/didChange", std::string("{\"textDocument\":{\"uri\":") + JsonQuote(Uri) + ",\"version\":2},\"contentChanges\":[{\"text\":" + JsonQuote(After) + "}]}")
		           << Request(3, "textDocument/hover", Params) << Request(4, "shutdown") << Notification("exit");
		std::istringstream Input(Transcript.str());
		std::ostringstream Output;
		const int Exit = RunTranscript(Input, Output);
		const auto Text = Output.str();
		if (!Expect(Exit == 0 && Contains(ResponseForId(Text, 2), "A.value") && Contains(ResponseForId(Text, 3), "\"result\":null"),
		        "current hover never revives a changed receiver from an older snapshot"))
		{
			std::cerr << Text << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Live hover recovery remains useful while an unrelated expression/string
		// is temporarily unfinished in the editor buffer.
		constexpr const char* StableHoverUri =
		    "file:///C:/veroscript-tests/lsp-stable-hover.vs";
		std::ostringstream HoverInput;
		HoverInput << Request(1, "initialize");
		HoverInput << Notification("initialized");
		HoverInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + StableHoverUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                                    "\"text\":\"void StableHoverTarget() {}\\n"
		                                                                    "void main() { StableHoverTarget(); print(\\\"ok\\\"); }\\n\"}}");
		HoverInput << Notification(
		    "textDocument/didChange",
		    std::string("{\"textDocument\":{\"uri\":\"") + StableHoverUri + "\",\"version\":2},\"contentChanges\":[{\"text\":"
		                                                                    "\"void StableHoverTarget() {}\\n"
		                                                                    "void main() {   StableHoverTarget(); print(\\\"unfinished); }\\n\"}]}");
		HoverInput << Request(
		    2,
		    "textDocument/hover",
		    std::string("{\"textDocument\":{\"uri\":\"") + StableHoverUri + "\"},\"position\":{\"line\":1,\"character\":18}}");
		HoverInput << Request(3, "shutdown");
		HoverInput << Notification("exit");

		std::istringstream HoverIn(HoverInput.str());
		std::ostringstream HoverOut;
		const int HoverExit = RunTranscript(HoverIn, HoverOut);
		const std::string HoverOutput = HoverOut.str();
		if (!Expect(HoverExit == 0, "stable-hover LSP session shuts down") || !Expect(Contains(HoverOutput, "\"id\":2") && Contains(HoverOutput, "StableHoverTarget") && Contains(HoverOutput, "\"range\":{\"end\":{\"character\":33,\"line\":1},"
		                                                                                                                                                                                       "\"start\":{\"character\":16,\"line\":1}}"),
		                                                                          "hover survives unfinished input and remaps its live token range"))
		{
			std::cerr << HoverOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// LSP initialize.locale uses the same catalog resolution path as CLI/DAP.
		std::istringstream ZhIn(OpenInvalidDocumentScript("zh_Hans.UTF-8"));
		std::ostringstream ZhOut;
		const int ZhExit = RunTranscript(ZhIn, ZhOut);
		const std::string ZhOutput = ZhOut.str();
		if (!Expect(ZhExit == 0, "zh-CN session should shut down cleanly") || !Expect(Contains(ZhOutput, "\"message\":\"未知函数：missing\""), "diagnostics should be localized for zh-CN"))
		{
			std::cerr << ZhOutput << '\n';
			return EXIT_FAILURE;
		}
		(void)VeroScript::Locale::TrySetLanguageFromTag("en-US");
	}

	{
		// Parser-owned declaration diagnostics must use the same localized catalog
		// path as semantic diagnostics.  This also prevents a newly-added parser
		// feature from leaking its English fallback through LSP.
		constexpr const char* StaticDiagnosticUri =
		    "file:///C:/veroscript-tests/lsp-static-diagnostic.vs";
		std::ostringstream StaticDiagnosticInput;
		StaticDiagnosticInput << Request(1, "initialize", "{\"locale\":\"zh-CN\"}");
		StaticDiagnosticInput << Notification("initialized");
		StaticDiagnosticInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + StaticDiagnosticUri + "\",\"languageId\":\"veroscript\",\"version\":1," + "\"text\":\"static class Broken<T> {}\\n\"}}");
		StaticDiagnosticInput << Request(2, "shutdown");
		StaticDiagnosticInput << Notification("exit");
		std::istringstream StaticDiagnosticIn(StaticDiagnosticInput.str());
		std::ostringstream StaticDiagnosticOut;
		const int StaticDiagnosticExit =
		    RunTranscript(StaticDiagnosticIn, StaticDiagnosticOut);
		const std::string StaticDiagnosticOutput = StaticDiagnosticOut.str();
		if (!Expect(StaticDiagnosticExit == 0, "localized static diagnostic session should shut down cleanly") || !Expect(Contains(StaticDiagnosticOutput, "\"code\":\"diag.static_class_cannot_be_generic\""), "static declaration diagnostic should retain its stable message id") || !Expect(Contains(StaticDiagnosticOutput, "静态 class 不能是泛型类型"), "static declaration diagnostic should use the selected locale"))
		{
			std::cerr << StaticDiagnosticOutput << '\\n';
			return EXIT_FAILURE;
		}
		(void)VeroScript::Locale::TrySetLanguageFromTag("en-US");
	}

	{
		// An unchanged semantic stream must produce an empty delta rather than a
		// replacement edit. This is the baseline for the minimal prefix/suffix delta
		// implementation used after ordinary edits.
		constexpr const char* DeltaUri = "file:///C:/veroscript-tests/lsp-delta.vs";
		std::ostringstream DeltaInput;
		DeltaInput << Request(1, "initialize");
		DeltaInput << Notification("initialized");
		DeltaInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + DeltaUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                              "\"text\":\"class Delta {}\\nint value = 0;\\n\"}}");
		DeltaInput << Request(
		    2, "textDocument/semanticTokens/full",
		    std::string("{\"textDocument\":{\"uri\":\"") + DeltaUri + "\"}}");
		DeltaInput << Request(
		    3, "textDocument/semanticTokens/full/delta",
		    std::string("{\"textDocument\":{\"uri\":\"") + DeltaUri + "\"},\"previousResultId\":\"1\"}");
		DeltaInput << Request(4, "shutdown");
		DeltaInput << Notification("exit");
		std::istringstream DeltaIn(DeltaInput.str());
		std::ostringstream DeltaOut;
		const int DeltaExit = RunTranscript(DeltaIn, DeltaOut);
		const std::string DeltaOutput = DeltaOut.str();
		if (!Expect(
		        DeltaExit == 0 && Contains(DeltaOutput, "\"resultId\":\"1\"") && Contains(DeltaOutput, "\"id\":3,\"jsonrpc\":\"2.0\",\"result\":{\"edits\":[]"),
		        "unchanged semantic tokens produce an empty versioned delta"))
		{
			std::cerr << DeltaOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Namespace segments must remain independently addressable over the JSON-RPC
		// boundary: exact Hover ranges, namespace semantic tokens, and scoped member
		// completion all come from the same SymbolQuery path.
		constexpr const char* NamespaceUri =
		    "file:///C:/veroscript-tests/lsp-standard-namespace.vs";
		const std::string NamespaceSource =
		    "void main() { std::chrono::now(); }\\n";
		std::ostringstream NamespaceInput;
		NamespaceInput << Request(1, "initialize", "{\"locale\":\"en-US\"}");
		NamespaceInput << Notification("initialized");
		NamespaceInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + NamespaceUri + "\",\"languageId\":\"veroscript\",\"version\":1,\"text\":\"" + NamespaceSource + "\"}}");
		// std, chrono, now (all positions are UTF-16 characters on line zero).
		for (const auto& RequestSpec : {
		         std::pair<int, int>{2, 14},
		         std::pair<int, int>{3, 19},
		         std::pair<int, int>{4, 27}})
		{
			NamespaceInput << Request(
			    RequestSpec.first,
			    "textDocument/hover",
			    std::string("{\"textDocument\":{\"uri\":\"") + NamespaceUri + "\"},\"position\":{\"line\":0,\"character\":" + std::to_string(RequestSpec.second) + "}}");
		}
		// At the start of now: completion has an empty prefix but must stay within
		// std::chrono rather than flattening other stdlib namespaces.
		NamespaceInput << Request(
		    5,
		    "textDocument/completion",
		    std::string("{\"textDocument\":{\"uri\":\"") + NamespaceUri + "\"},\"position\":{\"line\":0,\"character\":27}}");
		NamespaceInput << Request(
		    6,
		    "textDocument/semanticTokens/full",
		    std::string("{\"textDocument\":{\"uri\":\"") + NamespaceUri + "\"}}");
		constexpr const char* IoNamespaceUri = "file:///C:/veroscript-tests/lsp-io-namespace.vs";
		NamespaceInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + IoNamespaceUri + "\",\"languageId\":\"veroscript\",\"version\":1,\"text\":\"" + "void main() { std::io::write_file(); }\\n" + "\"}}");
		NamespaceInput << Request(
		    7,
		    "textDocument/completion",
		    std::string("{\"textDocument\":{\"uri\":\"") + IoNamespaceUri + "\"},\"position\":{\"line\":0,\"character\":23}}");
		NamespaceInput << Request(8, "shutdown");
		NamespaceInput << Notification("exit");

		std::istringstream NamespaceIn(NamespaceInput.str());
		std::ostringstream NamespaceOut;
		const int NamespaceExit = RunTranscript(NamespaceIn, NamespaceOut);
		const std::string NamespaceOutput = NamespaceOut.str();
		const std::string_view SteadyItem = CompletionItemForLabel(
		    ResponseForId(NamespaceOutput, 5), "steady_now");
		const std::string_view FileWriteItem = CompletionItemForLabel(
		    ResponseForId(NamespaceOutput, 7), "write_file");
		const std::vector<FDecodedSemanticToken> NamespaceTokens =
		    DecodeSemanticTokens(ResponseForId(NamespaceOutput, 6));
		if (!Expect(
		        NamespaceExit == 0 && Contains(NamespaceOutput, "namespace std") && Contains(NamespaceOutput, "namespace std::chrono") && Contains(NamespaceOutput, "std::chrono::now") && Contains(NamespaceOutput, "\"range\":{\"end\":{\"character\":25,\"line\":0},"
		                                                                                                                                                                                                             "\"start\":{\"character\":19,\"line\":0}}") &&
		            Contains(NamespaceOutput, "\"label\":\"now\"") && !SteadyItem.empty() && Contains(SteadyItem, "\"labelDetails\":{\"description\":\"std::chrono\"}") && Contains(SteadyItem, "\"text\":\"std::chrono::steady_now\"") && Contains(SteadyItem, "\"text\":\"double std::chrono::steady_now();") && Contains(SteadyItem, "{\"classification\":\"namespace\",\"length\":3,\"start\":0}") && Contains(SteadyItem, "{\"classification\":\"namespace\",\"length\":6,\"start\":12}") && Contains(SteadyItem, "{\"classification\":\"function\",\"length\":10,\"start\":20}") && Contains(SteadyItem, "{\"classification\":\"keyword\",\"length\":6,\"start\":0}") && !Contains(SteadyItem, "{\"classification\":\"keyword\",\"length\":6,\"start\":34}") && Contains(SteadyItem, "\"filterText\":\"steady_now\"") && Contains(SteadyItem, "\"kind\":3") && Contains(SteadyItem, "\"newText\":\"steady_now\"") && !Contains(SteadyItem, "\"insertTextFormat\":2") && Contains(SteadyItem, "\"range\":{\"end\":{\"character\":30,\"line\":0},"
		                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           "\"start\":{\"character\":27,\"line\":0}}") &&
		            !FileWriteItem.empty() && Contains(FileWriteItem, "\"labelDetails\":{\"description\":\"std::io\"}") && Contains(FileWriteItem, "{\"classification\":\"keyword\",\"length\":6,\"start\":25}") && Contains(FileWriteItem, "{\"classification\":\"keyword\",\"length\":6,\"start\":38}") && !Contains(FileWriteItem, "\"classification\":\"string\"") && Contains(NamespaceOutput, "double std::chrono::now()") && !Contains(ResponseForId(NamespaceOutput, 5), "\"label\":\"read_int\"") && HasSemanticToken(NamespaceTokens, 0, 0, 4, 12) && HasSemanticToken(NamespaceTokens, 0, 5, 4, 9) && HasSemanticToken(NamespaceTokens, 0, 14, 3, 0) && HasSemanticToken(NamespaceTokens, 0, 19, 6, 0) && HasSemanticToken(NamespaceTokens, 0, 27, 3, 9),
		        "LSP namespace hover, completion, and semantic tokens are segment-accurate"))
		{
			std::cerr << NamespaceOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// User-defined namespace members use abbreviated symbol details. Their
		// popup declarations must receive the same qualified semantic segments
		// as stdlib declarations, without changing standard LSP edits/details.
		constexpr const char* CustomUri =
		    "file:///C:/veroscript-tests/lsp-custom-namespace.vs";
		std::ostringstream CustomInput;
		CustomInput << Request(1, "initialize", "{\"locale\":\"en-US\"}");
		CustomInput << Notification("initialized");
		CustomInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + CustomUri + "\",\"languageId\":\"veroscript\",\"version\":1,\"text\":\"" + "namespace Alpha {\\n namespace Beta {\\n" + "  export class Widget {}\\n" + "  export int calculate() { return 42; }\\n" + "  export int amount = 7;\\n" + "  export const int limit = 9;\\n }\\n}\\n" + "void main() {\\n Alpha::Beta::\\n}\\n\"}}");
		CustomInput << Request(
		    2, "textDocument/completion",
		    std::string("{\"textDocument\":{\"uri\":\"") + CustomUri + "\"},\"position\":{\"line\":9,\"character\":14}}");
		CustomInput << Request(3, "shutdown");
		CustomInput << Notification("exit");
		std::istringstream CustomIn(CustomInput.str());
		std::ostringstream CustomOut;
		const int CustomExit = RunTranscript(CustomIn, CustomOut);
		const std::string CustomOutput = CustomOut.str();
		const std::string_view CustomResponse = ResponseForId(CustomOutput, 2);
		const std::string_view FunctionItem =
		    CompletionItemForLabel(CustomResponse, "calculate");
		const std::string_view ValueItem =
		    CompletionItemForLabel(CustomResponse, "amount");
		const std::string_view ConstItem =
		    CompletionItemForLabel(CustomResponse, "limit");
		const std::string_view ClassItem =
		    CompletionItemForLabel(CustomResponse, "Widget");
		if (!Expect(
		        CustomExit == 0 && Contains(FunctionItem, "\"labelDetails\":{\"description\":\"Alpha::Beta\"}") && Contains(FunctionItem, "\"detail\":\"int()\"") && Contains(FunctionItem, "\"text\":\"Alpha::Beta::calculate\"") && Contains(FunctionItem, "\"text\":\"int Alpha::Beta::calculate();\"") && Contains(FunctionItem, "{\"classification\":\"keyword\",\"length\":3,\"start\":0}") && Contains(FunctionItem, "{\"classification\":\"namespace\",\"length\":5,\"start\":4}") && Contains(FunctionItem, "{\"classification\":\"namespace\",\"length\":4,\"start\":11}") && Contains(FunctionItem, "{\"classification\":\"function\",\"length\":9,\"start\":17}") && Contains(FunctionItem, "\"insertText\":\"calculate\"") && Contains(ValueItem, "\"detail\":\"int\"") && Contains(ValueItem, "\"text\":\"int Alpha::Beta::amount;\"") && Contains(ValueItem, "{\"classification\":\"variable\",\"length\":6,\"start\":17}") && Contains(ConstItem, "\"detail\":\"const int\"") && Contains(ConstItem, "\"text\":\"const int Alpha::Beta::limit;\"") && Contains(ConstItem, "{\"classification\":\"keyword\",\"length\":5,\"start\":0}") && Contains(ConstItem, "{\"classification\":\"namespace\",\"length\":5,\"start\":10}") && Contains(ConstItem, "{\"classification\":\"constant\",\"length\":5,\"start\":23}") && Contains(ClassItem, "\"text\":\"Alpha::Beta::Widget\"") && Contains(ClassItem, "{\"classification\":\"class\",\"length\":6,\"start\":13}") && !Contains(ClassItem, "\"detail\":{\"semanticSpans\":"),
		        "custom namespace completions qualify callable and value detail presentations"))
		{
			std::cerr << CustomOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// A standard cancellation arrives after its request. The input loop must
		// still receive it while the worker is busy with earlier queued work.
		std::ostringstream CancelInput;
		CancelInput << Request(1, "initialize");
		CancelInput << Notification("initialized");
		CancelInput << Request(2, "workspace/symbol", "{\"query\":\"anything\"}");
		CancelInput << Notification("$/cancelRequest", "{\"id\":2}");
		CancelInput << Request(3, "shutdown");
		CancelInput << Notification("exit");
		std::istringstream CancelIn(CancelInput.str());
		std::ostringstream CancelOut;
		const int CancelExit = RunTranscript(CancelIn, CancelOut);
		const std::string CancelOutput = CancelOut.str();
		if (!Expect(
		        CancelExit == 0 && Contains(CancelOutput, "\"source.formatDocument\"") && Contains(CancelOutput, "\"id\":2") && Contains(CancelOutput, "\"code\":-32800") && Contains(CancelOutput, "request cancelled"),
		        "LSP advertises its safe source action and honors queued cancellation"))
		{
			std::cerr << CancelOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Out-of-order didChange notifications must never roll the in-memory overlay
		// back to older text. This is especially important when clients debounce
		// completion and diagnostics independently.
		constexpr const char* VersionUri =
		    "file:///C:/veroscript-tests/lsp-versioned-overlay.vs";
		std::ostringstream VersionInput;
		VersionInput << Request(1, "initialize");
		VersionInput << Notification("initialized");
		VersionInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + VersionUri + "\",\"languageId\":\"veroscript\",\"version\":5,"
		                                                                "\"text\":\"void Initial() {}\\n\"}}");
		VersionInput << Notification(
		    "textDocument/didChange",
		    std::string("{\"textDocument\":{\"uri\":\"") + VersionUri + "\",\"version\":6},\"contentChanges\":[{\"text\":\"void Fresh() {}\\n\"}]}");
		VersionInput << Notification(
		    "textDocument/didChange",
		    std::string("{\"textDocument\":{\"uri\":\"") + VersionUri + "\",\"version\":5},\"contentChanges\":[{\"text\":\"void Stale() {}\\n\"}]}");
		VersionInput << Request(
		    2,
		    "textDocument/documentSymbol",
		    std::string("{\"textDocument\":{\"uri\":\"") + VersionUri + "\"}}");
		VersionInput << Request(3, "shutdown");
		VersionInput << Notification("exit");
		std::istringstream VersionIn(VersionInput.str());
		std::ostringstream VersionOut;
		const int VersionExit = RunTranscript(VersionIn, VersionOut);
		const std::string VersionOutput = VersionOut.str();
		if (!Expect(
		        VersionExit == 0 && Contains(VersionOutput, "\"name\":\"Fresh\"") && !Contains(VersionOutput, "\"name\":\"Stale\""),
		        "stale didChange versions cannot replace the newest in-memory document"))
		{
			std::cerr << VersionOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Each analysis entry owns its diagnostics, including shared imports.
		// Observe actual notifications after each ordered request barrier.
		const auto Directory = std::filesystem::temp_directory_path() /
		    ("veroscript-lsp-diagnostics-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(Directory);
		const auto SharedPath = Directory / "shared.vs";
		{
			std::ofstream Shared(SharedPath, std::ios::binary);
			Shared << "export void Shared(){ missingShared(); }";
		}
		const auto DirectoryUtf8 = Directory.generic_u8string();
		const std::string DirectoryPath(reinterpret_cast<const char*>(DirectoryUtf8.data()), DirectoryUtf8.size());
		std::string EncodedPath;
		constexpr const char* Hex = "0123456789ABCDEF";
		for (const unsigned char C : DirectoryPath)
		{
			if ((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '/' || C == ':' || C == '-' || C == '_' || C == '.' || C == '~')
			{
				EncodedPath += C;
			}
			else
			{
				EncodedPath += '%';
				EncodedPath += Hex[C >> 4];
				EncodedPath += Hex[C & 15];
			}
		}
		const std::string BaseUri = std::string("file://") + (DirectoryPath.front() == '/' ? "" : "/") + EncodedPath;
		const std::string A = BaseUri + "/a.vs", B = BaseUri + "/b.vs", Shared = BaseUri + "/shared.vs";
		FLspConversationBuffer Buffer;
		std::istream SessionIn(&Buffer);
		std::ostream SessionOut(&Buffer);
		int SessionExit = -1;
		std::thread Session([&]()
		{
			SessionExit = VeroScript::Test::FLspStdio::Run(SessionIn, SessionOut);
		});
		Buffer.Append(Request(1, "initialize", "{\"locale\":\"en-US\"}"));
		Buffer.Append(Notification("initialized"));
		int Id = 1;
		bool Correct = true, Responsive = true;
		std::string Observed;
		const auto Checkpoint = [&]()
		{
			if (!Responsive) return;
			Buffer.Append(Request(++Id, "textDocument/documentSymbol", "{\"textDocument\":{\"uri\":" + JsonQuote(A) + "}}"));
			Responsive = Buffer.WaitForResponse(Id);
			Observed = Buffer.Str();
		};
		const auto Open = [&](const std::string& Uri, const std::string_view Source)
		{
			Buffer.Append(Notification("textDocument/didOpen", "{\"textDocument\":{\"uri\":" + JsonQuote(Uri) + ",\"languageId\":\"veroscript\",\"version\":1,\"text\":" + JsonQuote(Source) + "}}"));
		};
		const auto Change = [&](const std::string& Uri, const std::string_view Source, const int Version)
		{
			Buffer.Append(Notification("textDocument/didChange", "{\"textDocument\":{\"uri\":" + JsonQuote(Uri) + ",\"version\":" + std::to_string(Version) + "},\"contentChanges\":[{\"text\":" + JsonQuote(Source) + "}]}"));
		};
		const auto Close = [&](const std::string& Uri)
		{
			Buffer.Append(Notification("textDocument/didClose", "{\"textDocument\":{\"uri\":" + JsonQuote(Uri) + "}}"));
		};
		const auto HasError = [&](const std::string& Uri, const std::string_view Message)
		{
			return CountOccurrences(LastDiagnosticsForUri(Observed, Uri), Message) == 1;
		};
		const auto IsClean = [&](const std::string& Uri)
		{
			return Contains(LastDiagnosticsForUri(Observed, Uri), "\"diagnostics\":[]");
		};
		Open(A, "void A(){ missingA(); }");
		Checkpoint();
		Correct &= HasError(A, "unknown function: missingA");
		Open(B, "void B(){ missingB(); }");
		Checkpoint();
		Correct &= HasError(A, "unknown function: missingA") && HasError(B, "unknown function: missingB");
		Change(B, "void B(){}", 2);
		Checkpoint();
		Correct &= HasError(A, "unknown function: missingA") && IsClean(B);
		Close(B);
		Checkpoint();
		Correct &= HasError(A, "unknown function: missingA") && IsClean(B);
		Close(A);
		Open(A, "import \"shared.vs\";void A(){}");
		Open(B, "import \"shared.vs\";void B(){}");
		Checkpoint();
		Correct &= HasError(Shared, "unknown function: missingShared");
		Change(A, "void A(){}", 2);
		Checkpoint();
		Correct &= HasError(Shared, "unknown function: missingShared");
		Open(A, "import \"shared.vs\";void A(){}");
		Close(B);
		Checkpoint();
		Correct &= HasError(Shared, "unknown function: missingShared");
		Change(A, "void A(){}", 2);
		Checkpoint();
		Correct &= IsClean(Shared);
		Open(B, "import \"shared.vs\";void B(){}");
		Checkpoint();
		Correct &= HasError(Shared, "unknown function: missingShared");
		Open(Shared, "export void Shared(){}");
		Checkpoint();
		Correct &= IsClean(Shared) && Contains(LastDiagnosticsForUri(Observed, Shared), "\"version\":1");
		Change(Shared, "export void Shared(){ missingSecond(); }", 2);
		Checkpoint();
		Correct &= HasError(Shared, "unknown function: missingSecond") && !Contains(LastDiagnosticsForUri(Observed, Shared), "missingShared");
		Change(Shared, "export void Shared(){}", 3);
		Checkpoint();
		Correct &= IsClean(Shared) && Contains(LastDiagnosticsForUri(Observed, Shared), "\"version\":3");
		Close(Shared);
		Change(B, "import \"shared.vs\";void B(){}", 2);
		Checkpoint();
		Correct &= HasError(Shared, "unknown function: missingShared") && !Contains(LastDiagnosticsForUri(Observed, Shared), "\"version\"");
		Buffer.Append(Notification("workspace/didChangeConfiguration", "{\"settings\":{\"veroscript\":{\"defines\":[\"DIAGNOSTIC_REFRESH=1\"]}}}"));
		Checkpoint();
		Correct &= HasError(Shared, "unknown function: missingShared") && IsClean(A);
		Close(B);
		Checkpoint();
		Correct &= IsClean(Shared);
		Buffer.Append(Request(++Id, "shutdown"));
		Buffer.Append(Notification("exit"));
		Buffer.Close();
		Session.join();
		std::error_code Error;
		std::filesystem::remove(SharedPath, Error);
		std::filesystem::remove(Directory, Error);
		if (!Expect(Responsive && SessionExit == 0 && Correct,
		        "diagnostic contributions survive unrelated edits/close, deduplicate shared imports, and invalidate changed sources"))
		{
			std::cerr << Buffer.Str() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Integer protocol fields must not silently truncate JSON fractions. Invalid
		// positions fall back safely instead of invoking an out-of-range cast.
		constexpr const char* NumericUri =
		    "file:///C:/veroscript-tests/lsp-numeric-fields.vs";
		std::ostringstream NumericInput;
		NumericInput << Request(1, "initialize");
		NumericInput << Notification("initialized");
		NumericInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + NumericUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                                "\"text\":\"int Alpha = 0;\\nstring Beta = \\\"\\\";\\n\"}}");
		NumericInput << Frame(
		    std::string("{\"jsonrpc\":\"2.0\",\"id\":2,"
		                "\"method\":\"textDocument/hover\",\"params\":{"
		                "\"textDocument\":{\"uri\":\"") +
		    NumericUri + "\"},\"position\":{\"line\":1.5,\"character\":5}}}");
		NumericInput << Request(3, "shutdown");
		NumericInput << Notification("exit");
		std::istringstream NumericIn(NumericInput.str());
		std::ostringstream NumericOut;
		if (!Expect(
		        RunTranscript(NumericIn, NumericOut) == 0,
		        "invalid numeric LSP fields keep the server alive") ||
		    !Expect(
		        Contains(NumericOut.str(), "variable Alpha"),
		        "fractional LSP position is not truncated to another line"))
		{
			std::cerr << NumericOut.str() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Newly advertised navigation/presentation capabilities must have concrete,
		// non-crashing handlers. Imported paths stay editor-clickable even before the
		// target module itself is opened.
		constexpr const char* ExtendedUri = "file:///C:/veroscript-tests/lsp-extended.vs";
		std::ostringstream ExtendedInput;
		ExtendedInput << Request(1, "initialize");
		ExtendedInput << Notification("initialized");
		ExtendedInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + ExtendedUri + "\",\"languageId\":\"veroscript\",\"version\":7,"
		                                                                 "\"text\":\"import \\\"lib.vs\\\";\\nimport \\\"algorithm.vs\\\";\\nclass Item {}\\nvoid main() { Item value; }\\n\"}}");
		ExtendedInput << Request(
		    2, "textDocument/documentLink",
		    std::string("{\"textDocument\":{\"uri\":\"") + ExtendedUri + "\"}}");
		ExtendedInput << Request(
		    3, "textDocument/selectionRange",
		    std::string("{\"textDocument\":{\"uri\":\"") + ExtendedUri + "\"},\"positions\":[{\"line\":3,\"character\":14}]}");
		ExtendedInput << Request(
		    4, "textDocument/typeDefinition",
		    std::string("{\"textDocument\":{\"uri\":\"") + ExtendedUri + "\"},\"position\":{\"line\":2,\"character\":7}}");
		ExtendedInput << Request(
		    5, "textDocument/implementation",
		    std::string("{\"textDocument\":{\"uri\":\"") + ExtendedUri + "\"},\"position\":{\"line\":2,\"character\":7}}");
		ExtendedInput << Request(
		    6, "completionItem/resolve",
		    "{\"label\":\"Item\",\"detail\":\"class Item\"}");
		ExtendedInput << Request(
		    7, "textDocument/codeAction",
		    std::string("{\"textDocument\":{\"uri\":\"") + ExtendedUri + "\"},\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":0}},\"context\":{\"diagnostics\":[]}}");
		ExtendedInput << Request(8, "shutdown");
		ExtendedInput << Notification("exit");
		std::istringstream ExtendedIn(ExtendedInput.str());
		std::ostringstream ExtendedOut;
		const int ExtendedExit = RunTranscript(ExtendedIn, ExtendedOut);
		const std::string ExtendedOutput = ExtendedOut.str();
		if (!Expect(
		        ExtendedExit == 0 && Contains(ExtendedOutput, "file:///C:/veroscript-tests/lib.vs") && Contains(ResponseForId(ExtendedOutput, 2), "/stdlib/algorithm.vs") && Contains(ExtendedOutput, "\"parent\"") && Contains(ExtendedOutput, "\"id\":4,\"jsonrpc\":\"2.0\",\"result\":[{") && Contains(ExtendedOutput, "\"id\":5,\"jsonrpc\":\"2.0\",\"result\":[]") && Contains(ExtendedOutput, "\"documentation\":{\"kind\":\"markdown\"") && Contains(ExtendedOutput, "\"source.formatDocument\""),
		        "extended LSP capability handlers return safe structured results"))
		{
			std::cerr << ExtendedOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Import links use configured search paths and unsaved document overlays.
		constexpr const char* LinkUri = "file:///C:/veroscript-tests/link-main.vs";
		constexpr const char* OverlayUri = "file:///C:/veroscript-tests/import-root/link-module.vs";
		std::ostringstream LinkInput;
		LinkInput << Request(1, "initialize");
		LinkInput << Notification("initialized");
		LinkInput << Notification("workspace/didChangeConfiguration",
		    "{\"settings\":{\"veroscript\":{\"importPaths\":[\"C:/veroscript-tests/import-root\"]}}}");
		LinkInput << Notification("textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + OverlayUri + "\",\"languageId\":\"veroscript\",\"version\":1,\"text\":\"int LinkedValue = 42;\"}}");
		LinkInput << Notification("textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + LinkUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                             "\"text\":\"import \\\"link-module.vs\\\";\\nvoid main() {}\"}}");
		LinkInput << Request(2, "textDocument/documentLink",
		    std::string("{\"textDocument\":{\"uri\":\"") + LinkUri + "\"}}");
		LinkInput << Request(3, "shutdown");
		LinkInput << Notification("exit");
		std::istringstream LinkIn(LinkInput.str());
		std::ostringstream LinkOut;
		if (!Expect(RunTranscript(LinkIn, LinkOut) == 0 && Contains(ResponseForId(LinkOut.str(), 2), OverlayUri),
		        "import links resolve unsaved modules through configured search paths"))
		{
			std::cerr << LinkOut.str() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Closed-workspace scanning observes the cancellation probe before opening
		// files, so the LSP input thread can stop an expensive symbol query instead
		// of merely discarding its late response.
		VeroScriptWorkspace CancelWorkspace;
		FWorkspaceOptions CancelOptions;
		CancelOptions.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		CancelOptions.WorkspaceRoots.push_back("C:/veroscript-tests/not-needed");
		CancelWorkspace.SetOptions(CancelOptions);
		FWorkspaceSymbolsOptions CancelQuery;
		CancelQuery.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		CancelQuery.ShouldCancel = []()
		{
			return true;
		};
		const FWorkspaceSymbolsResult CancelledSymbols =
		    CancelWorkspace.WorkspaceSymbols(CancelQuery);
		if (!Expect(CancelledSymbols.bCancelled,
		        "workspace symbol scans cooperate with cancellation before filesystem work"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Cancellation is also checked while traversing a real workspace root, not
		// only before the query starts. This models a $/cancelRequest arriving while
		// a large closed-file symbol index is being enumerated.
		const auto Unique = std::chrono::steady_clock::now().time_since_epoch().count();
		const std::filesystem::path Root = std::filesystem::temp_directory_path() / ("veroscript-lsp-cancel-scan-" + std::to_string(Unique));
		std::error_code Error;
		std::filesystem::create_directories(Root, Error);
		std::ofstream(Root / "first.vs") << "void First() {}\n";
		std::ofstream(Root / "second.vs") << "void Second() {}\n";
		VeroScriptWorkspace CancelDuringScanWorkspace;
		FWorkspaceOptions CancelDuringScanOptions;
		CancelDuringScanOptions.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		CancelDuringScanOptions.WorkspaceRoots.push_back(Root.generic_string());
		CancelDuringScanWorkspace.SetOptions(CancelDuringScanOptions);
		std::size_t CancellationPolls = 0;
		FWorkspaceSymbolsOptions CancelDuringScanQuery;
		CancelDuringScanQuery.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		CancelDuringScanQuery.ShouldCancel = [&CancellationPolls]()
		{
			return ++CancellationPolls >= 4u;
		};
		const FWorkspaceSymbolsResult CancelledDuringScan =
		    CancelDuringScanWorkspace.WorkspaceSymbols(CancelDuringScanQuery);
		std::filesystem::remove_all(Root, Error);
		if (!Expect(
		        CancelledDuringScan.bCancelled && CancellationPolls >= 4u,
		        "workspace symbol scans poll cancellation during root enumeration"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// implementation is semantically distinct from definition: querying an
		// interface member finds the overriding class member, not the interface
		// declaration itself.
		constexpr const char* ImplementationUri = "file:///C:/veroscript-tests/lsp-implementation.vs";
		std::ostringstream ImplementationInput;
		ImplementationInput << Request(1, "initialize");
		ImplementationInput << Notification("initialized");
		ImplementationInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + ImplementationUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                                       "\"text\":\"interface IValue {\\nvirtual int Get() const = 0;\\n}\\n"
		                                                                       "class Concrete : public IValue {\\nint Get() const override { return 1; }\\n}\\n\"}}");
		ImplementationInput << Request(
		    2, "textDocument/implementation",
		    std::string("{\"textDocument\":{\"uri\":\"") + ImplementationUri + "\"},\"position\":{\"line\":1,\"character\":13}}");
		ImplementationInput << Request(3, "shutdown");
		ImplementationInput << Notification("exit");
		std::istringstream ImplementationIn(ImplementationInput.str());
		std::ostringstream ImplementationOut;
		const int ImplementationExit = RunTranscript(ImplementationIn, ImplementationOut);
		const std::string ImplementationOutput = ImplementationOut.str();
		if (!Expect(
		        ImplementationExit == 0 && Contains(ImplementationOutput, "\"id\":2,\"jsonrpc\":\"2.0\",\"result\":[{") && Contains(ImplementationOutput, "\"line\":4"),
		        "implementation navigation finds an override rather than reusing definition"))
		{
			std::cerr << ImplementationOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Type hierarchy uses stable type-symbol identities and returns direct
		// parents/children. Its ranges must select the declared type spelling rather
		// than the zero-width declaration-start point retained by the compact outline.
		constexpr const char* TypeHierarchyUri =
		    "file:///C:/veroscript-tests/lsp-type-hierarchy.vs";
		std::ostringstream TypeHierarchyInput;
		TypeHierarchyInput << Request(1, "initialize");
		TypeHierarchyInput << Notification("initialized");
		TypeHierarchyInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + TypeHierarchyUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                                      "\"text\":\"interface IValue {\\nvirtual int Get() const = 0;\\n}\\n"
		                                                                      "class Concrete : public IValue {\\nint Get() const override { return 1; }\\n}\\n\"}}");
		TypeHierarchyInput << Request(
		    2, "textDocument/prepareTypeHierarchy",
		    std::string("{\"textDocument\":{\"uri\":\"") + TypeHierarchyUri + "\"},\"position\":{\"line\":0,\"character\":10}}");
		// The ids are deterministic: kind | path | declaration-start | container | name.
		TypeHierarchyInput << Request(
		    3, "typeHierarchy/subtypes",
		    std::string("{\"item\":{\"data\":{\"veroscriptTypeSymbolId\":\"6|") + DriveFixturePathPrefix + "lsp-type-hierarchy.vs|1:1||IValue\"}}}");
		TypeHierarchyInput << Request(
		    4, "typeHierarchy/supertypes",
		    std::string("{\"item\":{\"data\":{\"veroscriptTypeSymbolId\":\"5|") + DriveFixturePathPrefix + "lsp-type-hierarchy.vs|4:1||Concrete\"}}}");
		TypeHierarchyInput << Request(5, "shutdown");
		TypeHierarchyInput << Notification("exit");
		std::istringstream TypeHierarchyIn(TypeHierarchyInput.str());
		std::ostringstream TypeHierarchyOut;
		const int TypeHierarchyExit = RunTranscript(TypeHierarchyIn, TypeHierarchyOut);
		const std::string TypeHierarchyOutput = TypeHierarchyOut.str();
		if (!Expect(
		        TypeHierarchyExit == 0 && Contains(TypeHierarchyOutput, "\"typeHierarchyProvider\":true") && Contains(TypeHierarchyOutput, "\"id\":2,\"jsonrpc\":\"2.0\",\"result\":[{") && Contains(TypeHierarchyOutput, "\"name\":\"IValue\"") && Contains(TypeHierarchyOutput, std::string("6|") + DriveFixturePathPrefix + "lsp-type-hierarchy.vs|1:1||IValue") && Contains(TypeHierarchyOutput, "\"id\":3,\"jsonrpc\":\"2.0\",\"result\":[{") && Contains(TypeHierarchyOutput, "\"name\":\"Concrete\"") && Contains(TypeHierarchyOutput, "\"id\":4,\"jsonrpc\":\"2.0\",\"result\":[{") && Contains(TypeHierarchyOutput, "\"start\":{\"character\":10,\"line\":0}"),
		        "type hierarchy returns direct inheritance results with identifier selection ranges"))
		{
			std::cerr << TypeHierarchyOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Implementation lookup must use the semantic method signature rather than
		// matching every same-name overload with the same number of parameters.
		constexpr const char* OverloadUri =
		    "file:///C:/veroscript-tests/lsp-implementation-overload.vs";
		std::ostringstream OverloadInput;
		OverloadInput << Request(1, "initialize");
		OverloadInput << Notification("initialized");
		OverloadInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + OverloadUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                                 "\"text\":\"interface IValue {\\nvirtual int Get(int value) const = 0;\\n}\\n"
		                                                                 "class Concrete : public IValue {\\n"
		                                                                 "int Get(string value) const { return 0; }\\n"
		                                                                 "int Get(int renamed) const { return renamed; }\\n}\\n\"}}");
		OverloadInput << Request(
		    2, "textDocument/implementation",
		    std::string("{\"textDocument\":{\"uri\":\"") + OverloadUri + "\"},\"position\":{\"line\":1,\"character\":13}}");
		OverloadInput << Request(3, "shutdown");
		OverloadInput << Notification("exit");
		std::istringstream OverloadIn(OverloadInput.str());
		std::ostringstream OverloadOut;
		const int OverloadExitCode = RunTranscript(OverloadIn, OverloadOut);
		const std::string OverloadOutput = OverloadOut.str();
		if (!Expect(
		        OverloadExitCode == 0 && Contains(OverloadOutput, "\"id\":2,\"jsonrpc\":\"2.0\",\"result\":[{") && Contains(OverloadOutput, "\"line\":5") && !Contains(OverloadOutput, "\"line\":4"),
		        "implementation navigation distinguishes overloads and retains implicit overrides"))
		{
			std::cerr << OverloadOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// A same-signature member that merely hides a non-virtual base method is not
		// an implementation. The implementation provider must return an empty set.
		constexpr const char* NonVirtualUri =
		    "file:///C:/veroscript-tests/lsp-implementation-nonvirtual.vs";
		std::ostringstream NonVirtualInput;
		NonVirtualInput << Request(1, "initialize");
		NonVirtualInput << Notification("initialized");
		NonVirtualInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + NonVirtualUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                                   "\"text\":\"class Base {\\nint Get(int value) { return value; }\\n}\\n"
		                                                                   "class Derived : public Base {\\nint Get(int value) { return value; }\\n}\\n\"}}");
		NonVirtualInput << Request(
		    2, "textDocument/implementation",
		    std::string("{\"textDocument\":{\"uri\":\"") + NonVirtualUri + "\"},\"position\":{\"line\":1,\"character\":5}}");
		NonVirtualInput << Request(3, "shutdown");
		NonVirtualInput << Notification("exit");
		std::istringstream NonVirtualIn(NonVirtualInput.str());
		std::ostringstream NonVirtualOut;
		const int NonVirtualExit = RunTranscript(NonVirtualIn, NonVirtualOut);
		const std::string NonVirtualOutput = NonVirtualOut.str();
		if (!Expect(
		        NonVirtualExit == 0 && Contains(NonVirtualOutput, "\"id\":2,\"jsonrpc\":\"2.0\",\"result\":[]"),
		        "implementation navigation rejects non-virtual method hiding"))
		{
			std::cerr << NonVirtualOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// A comma inside generic base arguments is not a multiple-inheritance
		// separator. Type hierarchy must retain the complete generic base name.
		constexpr const char* GenericHierarchyUri =
		    "file:///C:/veroscript-tests/lsp-generic-type-hierarchy.vs";
		std::ostringstream GenericHierarchyInput;
		GenericHierarchyInput << Request(1, "initialize");
		GenericHierarchyInput << Notification("initialized");
		GenericHierarchyInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + GenericHierarchyUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                                         "\"text\":\"interface IPair<T, U> { virtual T First(U fallback) = 0; }\\n"
		                                                                         "class PairConcrete : public IPair<int, string> { "
		                                                                         "int First(string fallback) { return 1; } }\\n\"}}");
		GenericHierarchyInput << Request(
		    2, "typeHierarchy/supertypes",
		    std::string("{\"item\":{\"data\":{\"veroscriptTypeSymbolId\":\"5|") + DriveFixturePathPrefix + "lsp-generic-type-hierarchy.vs|2:1||PairConcrete\"}}}");
		GenericHierarchyInput << Request(3, "shutdown");
		GenericHierarchyInput << Notification("exit");
		std::istringstream GenericHierarchyIn(GenericHierarchyInput.str());
		std::ostringstream GenericHierarchyOut;
		const int GenericHierarchyExit =
		    RunTranscript(GenericHierarchyIn, GenericHierarchyOut);
		const std::string GenericHierarchyOutput = GenericHierarchyOut.str();
		if (!Expect(
		        GenericHierarchyExit == 0 && Contains(GenericHierarchyOutput, "\"id\":2,\"jsonrpc\":\"2.0\",\"result\":[{") && Contains(GenericHierarchyOutput, "\"name\":\"IPair<T, U>\""),
		        "type hierarchy preserves comma-containing generic base arguments"))
		{
			std::cerr << GenericHierarchyOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Generic implementation lookup must specialize interface parameter types
		// through the concrete base instantiation before comparing signatures.
		constexpr const char* GenericImplementationUri =
		    "file:///C:/veroscript-tests/lsp-generic-implementation.vs";
		std::ostringstream GenericImplementationInput;
		GenericImplementationInput << Request(1, "initialize");
		GenericImplementationInput << Notification("initialized");
		GenericImplementationInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + GenericImplementationUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                                              "\"text\":\"interface IPair<T, U> {\\n"
		                                                                              "virtual T First(U fallback) = 0;\\n}\\n"
		                                                                              "class PairConcrete : public IPair<int, string> {\\n"
		                                                                              "int First(string fallback) { return 1; }\\n}\\n\"}}");
		GenericImplementationInput << Request(
		    2, "textDocument/implementation",
		    std::string("{\"textDocument\":{\"uri\":\"") + GenericImplementationUri + "\"},\"position\":{\"line\":1,\"character\":12}}");
		GenericImplementationInput << Request(3, "shutdown");
		GenericImplementationInput << Notification("exit");
		std::istringstream GenericImplementationIn(GenericImplementationInput.str());
		std::ostringstream GenericImplementationOut;
		const int GenericImplementationExit =
		    RunTranscript(GenericImplementationIn, GenericImplementationOut);
		const std::string GenericImplementationOutput = GenericImplementationOut.str();
		if (!Expect(
		        GenericImplementationExit == 0 && Contains(GenericImplementationOutput, "\"id\":2,\"jsonrpc\":\"2.0\",\"result\":[{") && Contains(GenericImplementationOutput, "\"line\":4"),
		        "implementation navigation specializes generic interface signatures"))
		{
			std::cerr << GenericImplementationOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Closed documents below every initialize.workspaceFolders root participate
		// in workspace symbol search, and a removed root stops contributing without
		// requiring an LSP process restart.
		const auto Unique = std::chrono::steady_clock::now().time_since_epoch().count();
		const std::filesystem::path FirstRoot = std::filesystem::temp_directory_path() / ("veroscript-lsp-workspace-a-" + std::to_string(Unique));
		const std::filesystem::path SecondRoot = std::filesystem::temp_directory_path() / ("veroscript-lsp-workspace-b-" + std::to_string(Unique));
		std::error_code WorkspaceError;
		std::filesystem::create_directories(FirstRoot, WorkspaceError);
		std::filesystem::create_directories(SecondRoot, WorkspaceError);
		{
			std::ofstream(FirstRoot / "first.vs") << "void OnlyFirst() {}\n";
			std::ofstream(SecondRoot / "second.vs") << "void OnlySecond() {}\n";
		}
		const std::string FirstUri = "file:///" + FirstRoot.generic_string();
		const std::string SecondUri = "file:///" + SecondRoot.generic_string();
		std::ostringstream WorkspaceInput;
		WorkspaceInput << Request(
		    1, "initialize",
		    std::string("{\"workspaceFolders\":[{\"uri\":\"") + FirstUri + "\",\"name\":\"first\"},{\"uri\":\"" + SecondUri + "\",\"name\":\"second\"}]}");
		WorkspaceInput << Notification("initialized");
		WorkspaceInput << Request(2, "workspace/symbol", "{\"query\":\"OnlySecond\"}");
		WorkspaceInput << Notification(
		    "workspace/didChangeWorkspaceFolders",
		    std::string("{\"event\":{\"added\":[],\"removed\":[{\"uri\":\"") + SecondUri + "\",\"name\":\"second\"}]}}");
		WorkspaceInput << Request(3, "workspace/symbol", "{\"query\":\"OnlySecond\"}");
		WorkspaceInput << Request(4, "shutdown");
		WorkspaceInput << Notification("exit");
		std::istringstream WorkspaceIn(WorkspaceInput.str());
		std::ostringstream WorkspaceOut;
		const int WorkspaceExit = RunTranscript(WorkspaceIn, WorkspaceOut);
		const std::string WorkspaceOutput = WorkspaceOut.str();
		std::filesystem::remove_all(FirstRoot, WorkspaceError);
		std::filesystem::remove_all(SecondRoot, WorkspaceError);
		if (!Expect(
		        WorkspaceExit == 0 && Contains(WorkspaceOutput, "\"id\":2,\"jsonrpc\":\"2.0\",\"result\":[{") && Contains(WorkspaceOutput, "OnlySecond") && Contains(WorkspaceOutput, "\"id\":3,\"jsonrpc\":\"2.0\",\"result\":[]"),
		        "LSP applies multi-root workspace additions and removals to indexing"))
		{
			std::cerr << WorkspaceOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// An explicit empty workspaceFolders array is authoritative. It must not
		// silently reactivate the deprecated rootUri or the server's default roots.
		const auto Unique = std::chrono::steady_clock::now().time_since_epoch().count();
		const std::filesystem::path LegacyRoot = std::filesystem::temp_directory_path() / ("veroscript-lsp-empty-workspace-" + std::to_string(Unique));
		std::error_code WorkspaceError;
		std::filesystem::create_directories(LegacyRoot, WorkspaceError);
		std::ofstream(LegacyRoot / "legacy.vs") << "void LegacyRootOnly() {}\n";
		const std::string LegacyUri = "file:///" + LegacyRoot.generic_string();
		std::ostringstream EmptyWorkspaceInput;
		EmptyWorkspaceInput << Request(
		    1, "initialize",
		    std::string("{\"rootUri\":\"") + LegacyUri + "\",\"workspaceFolders\":[]}");
		EmptyWorkspaceInput << Notification("initialized");
		EmptyWorkspaceInput << Request(
		    2, "workspace/symbol", "{\"query\":\"LegacyRootOnly\"}");
		EmptyWorkspaceInput << Request(3, "shutdown");
		EmptyWorkspaceInput << Notification("exit");
		std::istringstream EmptyWorkspaceIn(EmptyWorkspaceInput.str());
		std::ostringstream EmptyWorkspaceOut;
		FLspStdioRunOptions EmptyWorkspaceOptions;
		EmptyWorkspaceOptions.DefaultWorkspaceRoots.push_back(LegacyRoot.generic_string());
		const int EmptyWorkspaceExit = RunTranscript(
		    EmptyWorkspaceIn, EmptyWorkspaceOut, EmptyWorkspaceOptions);
		const std::string EmptyWorkspaceOutput = EmptyWorkspaceOut.str();
		std::filesystem::remove_all(LegacyRoot, WorkspaceError);
		if (!Expect(
		        EmptyWorkspaceExit == 0 && Contains(EmptyWorkspaceOutput, "\"id\":2,\"jsonrpc\":\"2.0\",\"result\":[]"),
		        "empty workspaceFolders does not fall back to rootUri/default roots"))
		{
			std::cerr << EmptyWorkspaceOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Semantic-name fuzzy matches rank above candidates that only match their
		// file path. This keeps workspace palettes useful for abbreviation queries.
		constexpr const char* FuzzyUri =
		    "file:///C:/veroscript-tests/semantic-name.vs";
		constexpr const char* FileOnlyUri =
		    "file:///C:/veroscript-tests/HTM.vs";
		std::ostringstream RankingInput;
		RankingInput << Request(1, "initialize");
		RankingInput << Notification("initialized");
		RankingInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + FuzzyUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                              "\"text\":\"void HyperTextManager() {}\\n\"}}");
		RankingInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + FileOnlyUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                                 "\"text\":\"void Other() {}\\n\"}}");
		RankingInput << Request(2, "workspace/symbol", "{\"query\":\"HTM\"}");
		RankingInput << Request(3, "shutdown");
		RankingInput << Notification("exit");
		std::istringstream RankingIn(RankingInput.str());
		std::ostringstream RankingOut;
		const int RankingExit = RunTranscript(RankingIn, RankingOut);
		const std::string RankingOutput = RankingOut.str();
		const std::size_t SemanticPosition = RankingOutput.find("\"name\":\"HyperTextManager\"");
		const std::size_t FilePosition = RankingOutput.find("\"name\":\"Other\"");
		if (!Expect(
		        RankingExit == 0 && SemanticPosition != std::string::npos && FilePosition != std::string::npos && SemanticPosition < FilePosition,
		        "workspace symbol fuzzy-name matches precede file-only matches"))
		{
			std::cerr << RankingOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Organize imports only touches exact, real import declarations; lookalikes
		// inside block comments must not produce a destructive edit.
		constexpr const char* MacroSource = "#define import print(1);\n"
		                                    "import \"shared.vs\";\nimport \"shared.vs\";\n";
		if (!Expect(VeroScript::VeroScriptCompile::CompileSource(MacroSource).bSuccess,
		        "keyword macro fixture is valid executable code"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* CodeActionUri = "file:///C:/veroscript-tests/lsp-code-action.vs";
		std::ostringstream CodeActionInput;
		CodeActionInput << Request(1, "initialize");
		CodeActionInput << Notification("initialized");
		CodeActionInput << Notification(
		    "textDocument/didOpen",
		    "{\"textDocument\":{\"uri\":\"file:///C:/veroscript-tests/shared.vs\","
		    "\"languageId\":\"veroscript\",\"version\":1,\"text\":\"\"}}");
		CodeActionInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + CodeActionUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                                   "\"text\":\"import \\\"shared.vs\\\";\\nimport \\\"shared.vs\\\";\\n\"}}");
		CodeActionInput << Request(
		    2,
		    "textDocument/codeAction",
		    std::string("{\"textDocument\":{\"uri\":\"") + CodeActionUri + "\"},\"range\":{\"start\":{\"line\":0,\"character\":0},"
		                                                                   "\"end\":{\"line\":0,\"character\":0}},\"context\":{\"diagnostics\":[],"
		                                                                   "\"only\":[\"source.organizeImports\"]}}");
		CodeActionInput << Notification(
		    "textDocument/didChange",
		    std::string("{\"textDocument\":{\"uri\":\"") + CodeActionUri + "\",\"version\":2},\"contentChanges\":[{\"text\":\"/* import \\\"shared.vs\\\";\\n"
		                                                                   "import \\\"shared.vs\\\"; */\\n\"}]}");
		CodeActionInput << Request(
		    3,
		    "textDocument/codeAction",
		    std::string("{\"textDocument\":{\"uri\":\"") + CodeActionUri + "\"},\"range\":{\"start\":{\"line\":0,\"character\":0},"
		                                                                   "\"end\":{\"line\":0,\"character\":0}},\"context\":{\"diagnostics\":[],"
		                                                                   "\"only\":[\"source.organizeImports\"]}}");
		CodeActionInput << Notification(
		    "textDocument/didChange",
		    std::string("{\"textDocument\":{\"uri\":\"") + CodeActionUri + "\",\"version\":3},\"contentChanges\":[{\"text\":" + JsonQuote(MacroSource) + "}]}");
		CodeActionInput << Request(4, "textDocument/codeAction",
		    std::string("{\"textDocument\":{\"uri\":\"") + CodeActionUri + "\"},\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":0}},"
		                                                                   "\"context\":{\"diagnostics\":[],\"only\":[\"source.organizeImports\"]}}");
		CodeActionInput << Request(5, "shutdown");
		CodeActionInput << Notification("exit");
		std::istringstream CodeActionIn(CodeActionInput.str());
		std::ostringstream CodeActionOut;
		const int CodeActionExit = RunTranscript(CodeActionIn, CodeActionOut);
		const std::string CodeActionOutput = CodeActionOut.str();
		if (!Expect(
		        CodeActionExit == 0 && Contains(CodeActionOutput, "\"id\":2,\"jsonrpc\":\"2.0\",\"result\":[{") && Contains(CodeActionOutput, "\"kind\":\"source.organizeImports\"") && Contains(CodeActionOutput, "\"veroscriptAction\":\"removeDuplicateImports\"") && Contains(CodeActionOutput, "\"id\":3,\"jsonrpc\":\"2.0\",\"result\":[]") && Contains(CodeActionOutput, "\"id\":4,\"jsonrpc\":\"2.0\",\"result\":[]"),
		        "organize imports is useful for code and conservative for comments"))
		{
			std::cerr << CodeActionOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// workspace/symbol keeps qualified C++-style queries useful while allowing
		// bounded fuzzy matching for palette-style short searches.
		constexpr const char* SymbolSearchUri =
		    "file:///C:/veroscript-tests/lsp-symbol-search.vs";
		std::ostringstream SymbolSearchInput;
		SymbolSearchInput << Request(1, "initialize");
		SymbolSearchInput << Notification("initialized");
		SymbolSearchInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + SymbolSearchUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                                     "\"text\":\"namespace Search {\\nvoid FuzzySymbol() {}\\n}\\n\"}}");
		SymbolSearchInput << Request(2, "workspace/symbol", "{\"query\":\"Search::FuzzySymbol\"}");
		SymbolSearchInput << Request(3, "workspace/symbol", "{\"query\":\"FzS\"}");
		SymbolSearchInput << Request(4, "shutdown");
		SymbolSearchInput << Notification("exit");
		std::istringstream SymbolSearchIn(SymbolSearchInput.str());
		std::ostringstream SymbolSearchOut;
		const int SymbolSearchExit = RunTranscript(SymbolSearchIn, SymbolSearchOut);
		const std::string SymbolSearchOutput = SymbolSearchOut.str();
		if (!Expect(
		        SymbolSearchExit == 0 && Contains(SymbolSearchOutput, "\"id\":2,\"jsonrpc\":\"2.0\",\"result\":[{") && Contains(SymbolSearchOutput, "\"id\":3,\"jsonrpc\":\"2.0\",\"result\":[{") && Contains(SymbolSearchOutput, "FuzzySymbol"),
		        "workspace symbol search supports qualified and fuzzy symbol queries"))
		{
			std::cerr << SymbolSearchOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Every formatting entry point must honor the client's indentation style.
		constexpr const char* TabUri = "file:///C:/veroscript-tests/lsp-tab-format.vs";
		std::ostringstream TabInput;
		TabInput << Request(1, "initialize");
		TabInput << Notification("initialized");
		TabInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + TabUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                            "\"text\":\"void main() {\\nint value=1;\\n}\\n\"}}");
		const std::string Options = "\"options\":{\"tabSize\":8,\"insertSpaces\":false}";
		const std::string Document = std::string("\"textDocument\":{\"uri\":\"") + TabUri + "\"}";
		TabInput << Request(2, "textDocument/formatting",
		    "{" + Document + "," + Options + "}");
		TabInput << Request(3, "textDocument/rangeFormatting",
		    "{" + Document + ",\"range\":{\"start\":{\"line\":1,\"character\":0},"
		                     "\"end\":{\"line\":2,\"character\":0}}," +
		        Options + "}");
		TabInput << Request(4, "textDocument/onTypeFormatting",
		    "{" + Document + ",\"position\":{\"line\":1,\"character\":12},"
		                     "\"ch\":\";\"," +
		        Options + "}");
		TabInput << Request(5, "shutdown");
		TabInput << Notification("exit");
		std::istringstream TabIn(TabInput.str());
		std::ostringstream TabOut;
		const int TabExit = RunTranscript(TabIn, TabOut);
		const std::string TabOutput = TabOut.str();
		const std::string_view ExpectedIndent = "\"newText\":\"\\tint value = 1;";
		if (!Expect(TabExit == 0 && Contains(ResponseForId(TabOutput, 2), "\\n\\tint value = 1;") && Contains(ResponseForId(TabOutput, 3), ExpectedIndent) && Contains(ResponseForId(TabOutput, 4), ExpectedIndent),
		        "document, range, and on-type formatting honor insertSpaces=false"))
		{
			std::cerr << TabOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// A non-document range must produce a real, line-safe edit.  This catches
		// servers that advertise rangeFormattingProvider but silently return [] for
		// every selection other than the complete document.
		constexpr const char* RangeUri = "file:///C:/veroscript-tests/lsp-range-format.vs";
		std::ostringstream RangeInput;
		RangeInput << Request(1, "initialize");
		RangeInput << Notification("initialized");
		RangeInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + RangeUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                              "\"text\":\"void main() {\\nint value=1;\\nprint(value);\\n}\\n\"}}");
		RangeInput << Request(
		    2, "textDocument/rangeFormatting",
		    std::string("{\"textDocument\":{\"uri\":\"") + RangeUri + "\"},\"range\":{\"start\":{\"line\":1,\"character\":0},"
		                                                              "\"end\":{\"line\":2,\"character\":13}},\"options\":{\"tabSize\":2}}");
		RangeInput << Request(3, "shutdown");
		RangeInput << Notification("exit");
		std::istringstream RangeIn(RangeInput.str());
		std::ostringstream RangeOut;
		const int RangeExit = RunTranscript(RangeIn, RangeOut);
		const std::string RangeOutput = RangeOut.str();
		if (!Expect(
		        RangeExit == 0 && Contains(RangeOutput, "\"id\":2,\"jsonrpc\":\"2.0\",\"result\":[{") && Contains(RangeOutput, "\"newText\":\"  int value = 1;") && Contains(RangeOutput, "print(value);"),
		        "range formatting returns a non-empty local edit"))
		{
			std::cerr << RangeOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// The plural protocol setting must preserve standard-library provenance all
		// the way through workspace analysis and imported-symbol hover.
		const auto Unique = std::chrono::steady_clock::now().time_since_epoch().count();
		const std::filesystem::path StdlibRoot = std::filesystem::temp_directory_path() / ("veroscript-lsp-stdlib-" + std::to_string(Unique));
		std::error_code FileError;
		std::filesystem::create_directories(StdlibRoot, FileError);
		{
			std::ofstream Module(
			    StdlibRoot / "lsp_custom.vs",
			    std::ios::binary | std::ios::trunc);
			Module << "/// Custom standard-library function.\n"
			          "export int lsp_stdlib_value() { return 64; }\n";
		}
		{
			std::ofstream Module(StdlibRoot / "algorithm.vs", std::ios::binary);
			Module << "// standard namespace module\n";
		}
		constexpr const char* StdlibUri =
		    "file:///C:/veroscript-tests/lsp-custom-stdlib.vs";
		std::ostringstream StdlibInput;
		StdlibInput << Request(1, "initialize");
		StdlibInput << Notification("initialized");
		StdlibInput << Notification(
		    "workspace/didChangeConfiguration",
		    std::string("{\"settings\":{\"veroscript\":{\"stdlibSearchPaths\":[\"") + StdlibRoot.generic_string() + "\"]}}}");
		StdlibInput << Notification(
		    "textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + StdlibUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                               "\"text\":\"import \\\"lsp_custom.vs\\\";\\n"
		                                                               "int main() { return lsp_stdlib_value(); }\\n\"}}");
		StdlibInput << Request(
		    2,
		    "textDocument/hover",
		    std::string("{\"textDocument\":{\"uri\":\"") + StdlibUri + "\"},\"position\":{\"line\":1,\"character\":24}}");
		constexpr const char* NamespaceSource = "void main() { std::algorithm::sort(); }\n";
		StdlibInput << Notification("textDocument/didChange",
		    std::string("{\"textDocument\":{\"uri\":\"") + StdlibUri + "\",\"version\":2},\"contentChanges\":[{\"text\":" + JsonQuote(NamespaceSource) + "}]}");
		StdlibInput << Request(3, "textDocument/definition",
		    std::string("{\"textDocument\":{\"uri\":\"") + StdlibUri + "\"},\"position\":{\"line\":0,\"character\":19}}");
		StdlibInput << Request(4, "shutdown");
		StdlibInput << Notification("exit");
		std::istringstream StdlibIn(StdlibInput.str());
		std::ostringstream StdlibOut;
		const int StdlibExit = RunTranscript(StdlibIn, StdlibOut);
		const std::string StdlibOutput = StdlibOut.str();
		std::filesystem::remove_all(StdlibRoot, FileError);
		if (!Expect(
		        StdlibExit == 0 && Contains(StdlibOutput, "lsp_stdlib_value") && Contains(StdlibOutput, "Custom standard-library function") && Contains(ResponseForId(StdlibOutput, 3), StdlibRoot.filename().generic_string() + "/algorithm.vs") && Contains(ResponseForId(StdlibOutput, 3), "\"range\":{\"end\":{\"character\":0,\"line\":0},\"start\":{\"character\":0,\"line\":0}}"),
		        "LSP applies plural standard-library roots to semantic queries"))
		{
			std::cerr << StdlibOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// These are wire-level requests, including their exact documents and caret
		// positions. The pipe is a test marker and is removed before didOpen.
		struct FContextCase
		{
			const char* Name;
			const char* MarkedSource;
			const char* RequiredLabel;
			bool bExpectEmpty;
			bool bAutomatic = false;
			const char* ForbiddenLabel = nullptr;
		};
		const FContextCase Cases[] = {
		    {"scope-if", "void main(){ int local=1; if(loc|", "local", false},
		    {"scope-if-body", "void main(){ int local=1; if(loc|) }", "local", false},
		    {"scope-while", "void main(){ int local=1; while(loc|", "local", false},
		    {"scope-return", "int main(){ int local=1; return loc|", "local", false},
		    {"scope-expression", "void main(){ int local=1; loc|", "local", false},
		    {"scope-call", "void main(){ int local=1; print(loc|", "local", false},
		    {"scope-index", "void main(){ int local=1; array<int> values={}; values[loc|", "local", false},
		    {"scope-initializer", "void main(){ int local=1; int next=loc|", "local", false},
		    {"scope-for-initializer", "void main(){ int local=1; for(int i=loc|", "local", false},
		    {"scope-for-body", "void main(){ for(int i=0;i<2;++i){ i|", "i", false},
		    {"scope-for-complete", "void main(){ for(int i=0;i<2;++i){ i|; } }", "i", false},
		    {"scope-comparison", "void main(){ int local=1; if(local<loc|) {} }", "local", false},
		    {"scope-comparison-global", "int local=1; void main(){ if(local<loc|", "local", false},
		    {"scope-comparison-namespace", "namespace N{ int value=1; } void main(){ int local=1; if(N::value<loc|", "local", false},
		    {"scope-comparison-field", "class C{ int value=1; void test(int local){ if(this.value<loc|", "local", false},
		    {"scope-comparison-chain", "void main(){ int local=1; if(local<2>loc|", "local", false},
		    {"scope-for-ternary", "void main(){ int local=1; for(true?0:loc|", "local", false},
		    {"scope-for-expression-lambda", "void main(){ for(int i=0;[](int param)->int=>par|", "param", false},
		    {"scope-for-expression-lambda-close", "void main(){ for(int i=0;[](int param)->int=>par|){} }", "param", false},
		    {"scope-for-expression-lambda-nested", "void main(){ for(int i=0;true?([](int param)->int=>par|):0){} }", "param", false},
		    {"scope-for-expression-lambda-step", "void main(){ for(int i=0;i<1;[](int param)->int=>par|", "param", false},
		    {"scope-for-expression-lambda-outside", "void main(){ for(int i=0;[](int param)->int=>param;par|){} }", nullptr, false, false, "param"},
		    {"member-lambda-for", "void main(){ for(int i=0;[](array<int> items)->int=>items.pu|", "push", false},
		    {"scope-array-initializer", "void main(){ int local=1; array<int> arr={loc|", "local", false},
		    {"scope-array-next-element", "void main(){ int local=1; array<int> arr={0,loc|", "local", false},
		    {"scope-array-nested", "void main(){ int local=1; array<array<int>> arr={{loc|", "local", false},
		    {"scope-array-for", "void main(){ int local=1; for(array<int> arr={loc|", "local", false},
		    {"member-lambda-end", "void main(){ auto f=[](array<int> items)->int{return items.pu|;}; }", "push", false},
		    {"member-lambda-suffix", "void main(){ auto f=[](array<int> items)->int{return items.pu|sh();}; }", "push", false},
		    {"member-lambda-incomplete", "void main(){ auto f=[](array<int> items)->int{return items.pu|", "push", false},
		    {"scope-block-outside", "void main(){ {int local=1;} loc|", nullptr, false, false, "local"},
		    {"scope-later-declaration", "void main(){ loc|; int local=1; }", nullptr, false, false, "local"},
		    {"scope-nested", "void main(){ int local=1; if(true){ loc|", "local", false},
		    {"scope-do", "void main(){ int local=1; do{ loc|", "local", false},
		    {"scope-try", "void main(){ int local=1; try{ loc|", "local", false},
		    {"scope-lambda", "void main(){ auto callback=[](int param)->int{return par|", "param", false},
		    {"scope-lambda-complete", "void main(){ auto callback=[](int param)->int{return par|;}; }", "param", false},
		    {"scope-range", "void main(){ array<int> values={}; for(int value:val|", "values", false, false, "value"},
		    {"scope-lambda-outside", "void main(){ auto callback=[](int param)->int{return param;}; par|", nullptr, false, false, "param"},
		    {"scope-loop-outside", "void main(){ for(int i=0;i<2;++i){} i|", nullptr, false, false, "i"},
		    {"scope-after-error", "void main(){ int local=1; loc|; }\nvoid bad( {", "local", false},
		    {"scope-token-suffix", "void main(){ int local=1; print(lo|cal", "local", false},
		    {"literal-string-end", "void main(){ print(\"hello\"|); }", nullptr, true},
		    {"literal-raw-end", "void main(){ print(r\"hello\"|); }", nullptr, true},
		    {"literal-wide-end", "void main(){ print(L\"hello\"|); }", nullptr, true},
		    {"literal-char-end", "void main(){ char c='a'|; }", nullptr, true},
		    {"literal-true-end", "void main(){ bool c=true|; }", nullptr, true},
		    {"literal-null-end", "void main(){ auto c=nullptr|; }", nullptr, true},
		    {"literal-number-space", "void main(){ int c=0 |; }", nullptr, true},
		    {"literal-comment-gap", "void main(){ int c=0 /* note */ |; }", nullptr, true},
		    {"literal-true-partial", "void main(){ bool c=tr|ue; }", "true", false},
		    {"literal-null-partial", "void main(){ auto c=nu|llptr; }", "nullptr", false},
		    {"literal-operator", "void main(){ int local=1; int c=0+loc|", "local", false},
		    {"literal-statement", "void main(){ int local=1; bool c=true; loc|", "local", false},
		    {"number-manual", "void main(){ int i=0|; }", nullptr, true},
		    {"number-auto", "void main(){ int i=0|; }", nullptr, true, true},
		    {"number-inside", "void main(){ int i=12|34; }", nullptr, true, true},
		    {"number-exponent", "void main(){ double d=1e+|2; }", nullptr, true, true},
		    {"number-incomplete", "void main(){ int i=0x|", nullptr, true, true},
		    {"number-before-error", "void bad( {\nvoid main(){ int i=0|; }", nullptr, true, true},
		    {"number-after-error", "void main(){ int i=0|; }\nvoid bad( {", nullptr, true, true},
		    {"number-operator", "void main(){ int item=1; int i=0+|item; }", "item", false},
		    {"number-semicolon", "void main(){ for(int i=0;|i<2;++i) {} }", "i", false},
		    {"number-new-identifier", "void main(){ int item0=1; int i=0+item0|; }", "item0", false},
		    {"for-condition", "void main(){ for(int i=0;i|) }", "i", false},
		    {"for-condition-auto", "void main(){ for(int i=0;i|) }", "i", false, true},
		    {"for-no-close", "void main(){ for(int i=0;i|", "i", false},
		    {"for-empty-condition", "void main(){ for(int i=0;|", "i", false},
		    {"for-step", "void main(){ for(int i=0;i<2;i|) }", "i", false},
		    {"for-call", "void main(){ for(int i=0;abs(i|)) }", "i", false},
		    {"for-before-error", "void main(){ int before=1\nfor(int i=0;i|) {} }", "i", false},
		    {"for-after-error", "void main(){ for(int i=0;i|) {} }\nvoid bad( {", "i", false},
		    {"for-crlf", "void main(){\r\nfor(int i=0;\r\ni|) {}\r\n}", "i", false},
		    {"for-identifier-suffix", "void main(){ for(int iteration=0;ite|ration) {} }", "iteration", false},
		    {"string", "void main() { print(\"s|\"); }", nullptr, true},
		    {"string-auto", "void main() { print(\"s|\"); }", nullptr, true},
		    {"unclosed-string", "void main() { print(\"s|", nullptr, true},
		    {"raw-string", "void main() { print(r\"/* false| \"); }", nullptr, true},
		    {"multiline-raw", "void main() { print(r\"first\nfalse|\"); }", nullptr, true},
		    {"char", "void main() { char c = 's|'; }", nullptr, true},
		    {"line-comment", "void main() { // fal|\n}", nullptr, true},
		    {"block-comment", "void main() { /* fal| */ }", nullptr, true},
		    {"unclosed-comment", "void main() { /* fal|", nullptr, true},
		    {"fake-import-comment", "/*\nimport \"stdlib/ch|\"\n*/", nullptr, true},
		    {"class-name", "class A|", nullptr, true},
		    {"struct-name", "struct A|", nullptr, true},
		    {"interface-name", "interface A|", nullptr, true},
		    {"enum-name", "enum A|", nullptr, true},
		    {"namespace-name", "namespace A|", nullptr, true},
		    {"class-name-multiline", "class\nA|", nullptr, true},
		    {"namespace-name-multiline", "namespace\nA|", nullptr, true},
		    {"type-name-multiline", "void main() { int\nA|; }", nullptr, true},
		    {"generic-type-name-multiline", "void main() { array<int>\nA|; }", nullptr, true},
		    {"named-type-name-multiline", "class Foo {}\nvoid main() { Foo\nA|; }", nullptr, true},
		    {"named-type-name-multiline-before-error", "void bad( {\nclass Foo {}\nvoid main() { Foo\nA|; }", nullptr, true},
		    {"alias-type-name-multiline", "using Alias = int;\nvoid main() { Alias\nA|; }", nullptr, true},
		    {"macro-after-identifier", "#define ANSWER 42\nvoid main() {\nMA\nANS|\n}", "ANSWER", false},
		    {"variable-name", "void main() { int A|; }", nullptr, true},
		    {"named-type-variable", "class Foo {}\nvoid main() { Foo A|; }", nullptr, true},
		    {"unfinished-variable", "void main() { int A|", nullptr, true},
		    {"string-before-error", "void bad( {\nvoid main() { print(\"s|\"); }", nullptr, true},
		    {"string-after-error", "void main() { print(\"s|\"); }\nvoid bad( {", nullptr, true},
		    {"comment-before-error", "void bad( {\nvoid main() { /* fal| */ }", nullptr, true},
		    {"unclosed-after-error", "void main() { print(\"s|\nvoid bad( {", nullptr, true},
		    {"ambiguous-after-lex-error", "void bad() { print(\"oops\npri|", nullptr, true},
		    {"class-before-error", "void bad( {\nclass A|", nullptr, true},
		    {"class-after-error", "class A|\nvoid bad( {", nullptr, true},
		    {"qualified-typo", "std::chono|", "chrono", false},
		    {"qualified-comment-gap", "std:: /* gap */ chono|", "chrono", false},
		    {"qualified-comment-gap-before-error", "void bad( {\nstd:: /* gap */ chono|", "chrono", false},
		    {"qualified-newline", "std::\nchono|", "chrono", false},
		    {"qualified-newline-before-error", "void bad( {\nstd::\nchono|", "chrono", false},
		    {"qualified-empty-newline", "std::\n|", "chrono", false},
		    {"qualified-valid", "void main() { std::chrono::| }", "now", false},
		    {"qualified-valid-before-error", "void bad( {\nvoid main() { std::chrono::| }", "now", false},
		    {"qualified-valid-after-error", "void main() { std::chrono::| }\nvoid bad( {", "now", false},
		    {"qualified-unclosed-before", "void bad() { print(\"oops\nvoid main() { std::chrono::| }", "now", false},
		    {"qualified-before-error", "void bad( {\nstd::chono|", "chrono", false},
		    {"qualified-after-error", "std::chono|\nvoid bad( {", "chrono", false},
		    {"unknown-member", "void main() { unknown.me| }", nullptr, true},
		    {"unknown-member-before-error", "void bad( {\nvoid main() { unknown.me| }", nullptr, true},
		    {"unknown-qualified", "unknown::me|", nullptr, true},
		    {"unknown-qualified-comment-gap", "unknown:: /* gap */ me|", nullptr, true},
		    {"unknown-qualified-newline", "unknown::\nme|", nullptr, true},
		    {"unknown-qualified-before-error", "void bad( {\nunknown::me|", nullptr, true},
		    {"unknown-arrow", "void main() { unknown->me| }", nullptr, true},
		    {"shadowed-std-dot", "void main() { array<int> std; std.| }", "push", false},
		    {"shadowed-std-dot-before-error", "void bad( {\nvoid main() { array<int> std; std.| }", "push", false},
		    {"code-before-error", "void bad( {\nvoid main() { ret|; }", "return", false},
		    {"code-after-error", "void main() { ret|; }\nvoid bad( {", "return", false},
		    {"after-semicolon", "std::chono;|", "auto", false},
		    {"after-multiline-semicolon", "std::\nchono;|", "auto", false},
		    {"print-call", "void main() { pri|; }", "print", false},
		    {"print-full-prefix", "void main() { print|; }", "print", false},
		    {"print-empty-prefix", "void main() { | }", "print", false},
		    {"print-reference", "void main() { auto callback = pri|; }", "print", false},
		    {"print-existing-paren", "void main() { pri|(); }", "print", false},
		    {"print-existing-paren-suffix", "void main() { pri|nt(); }", "print", false},
		    {"print-existing-paren-comment", "void main() { pri| /* note */ (); }", "print", false},
		};
		std::ostringstream ContextInput;
		ContextInput << Request(1, "initialize", SnippetInitializeParams)
		             << Notification("initialized");
		int RequestId = 2;
		for (const FContextCase& Case : Cases)
		{
			std::string Source = Case.MarkedSource;
			const std::size_t Caret = Source.find('|');
			if (!Expect(Caret != std::string::npos, "completion context marker exists"))
			{
				return EXIT_FAILURE;
			}
			Source.erase(Caret, 1);
			const std::string CaseUri = std::string("file:///C:/veroscript-tests/") + Case.Name + ".vs";
			const std::size_t LastNewline = Caret == 0
			    ? std::string::npos
			    : Source.rfind('\n', Caret - 1);
			const std::size_t Line = std::count(Source.begin(), Source.begin() + Caret, '\n');
			const std::size_t Character = Caret - (LastNewline == std::string::npos ? 0 : LastNewline + 1);
			ContextInput << Notification(
			    "textDocument/didOpen",
			    "{\"textDocument\":{\"uri\":" + JsonQuote(CaseUri) + ",\"languageId\":\"veroscript\",\"version\":1,\"text\":" + JsonQuote(Source) + "}}");
			std::string CompletionParams =
			    "{\"textDocument\":{\"uri\":" + JsonQuote(CaseUri) + "},\"position\":{\"line\":" + std::to_string(Line) + ",\"character\":" + std::to_string(Character) + "}";
			if (std::string_view(Case.Name) == "number-auto")
			{
				CompletionParams += ",\"context\":{\"triggerKind\":2,"
				                    "\"triggerCharacter\":\"0\"}";
			}
			else if (Case.bAutomatic)
			{
				CompletionParams += ",\"context\":{\"triggerKind\":3}";
			}
			else if (std::string_view(Case.Name) == "string-auto")
			{
				CompletionParams += ",\"context\":{\"triggerKind\":2,"
				                    "\"triggerCharacter\":\"s\"}";
			}
			CompletionParams += '}';
			ContextInput << Request(
			    RequestId++,
			    "textDocument/completion",
			    CompletionParams);
		}
		ContextInput << Request(RequestId, "shutdown") << Notification("exit");
		std::istringstream ContextIn(ContextInput.str());
		std::ostringstream ContextOut;
		if (!Expect(RunTranscript(ContextIn, ContextOut) == 0,
		        "completion context session should shut down cleanly"))
		{
			return EXIT_FAILURE;
		}
		const std::string ContextOutput = ContextOut.str();
		RequestId = 2;
		for (const FContextCase& Case : Cases)
		{
			const std::string_view Raw = ResponseForId(ContextOutput, RequestId++);
			const bool bValid = !Raw.empty() && (!Case.bExpectEmpty || Contains(Raw, "\"items\":[]")) && (!Case.RequiredLabel || !CompletionItemForLabel(Raw, Case.RequiredLabel).empty()) && (!Case.ForbiddenLabel || CompletionItemForLabel(Raw, Case.ForbiddenLabel).empty()) && (std::string_view(Case.Name).find("qualified") == std::string_view::npos && std::string_view(Case.Name).find("member") == std::string_view::npos || (CompletionItemForLabel(Raw, "false").empty() && CompletionItemForLabel(Raw, "new").empty() && CompletionItemForLabel(Raw, "__FILE__").empty()));
			if (!Expect(bValid, "completion context must preserve lexical and syntax role"))
			{
				std::cerr << Case.Name << " document: " << Case.MarkedSource
				          << " raw response: " << Raw << '\n';
				return EXIT_FAILURE;
			}
			if (std::string_view(Case.Name).rfind("scope-", 0) == 0 && Case.RequiredLabel)
			{
				std::string Source = Case.MarkedSource;
				const std::size_t Caret = Source.find('|');
				Source.erase(Caret, 1);
				std::size_t Start = Caret;
				std::size_t End = Caret;
				const auto IsNamePart = [](const char C)
				{
					return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_';
				};
				while (Start > 0 && IsNamePart(Source[Start - 1]))
				{
					--Start;
				}
				while (End < Source.size() && IsNamePart(Source[End]))
				{
					++End;
				}
				const std::string Range = "\"range\":{\"end\":{\"character\":" + std::to_string(End) + ",\"line\":0},\"start\":{\"character\":" + std::to_string(Start) + ",\"line\":0}}";
				const std::string_view Item = CompletionItemForLabel(Raw, Case.RequiredLabel);
				if (!Expect(Contains(Item, Range) && Contains(Item, "\"newText\":" + JsonQuote(Case.RequiredLabel)),
				        "scope recovery preserves the exact original token edit"))
				{
					std::cerr << Source << "\nposition=0:" << Caret << "\nresponse=" << Raw << '\n';
					return EXIT_FAILURE;
				}
			}
			if (std::string_view(Case.Name).rfind("for-", 0) == 0 && Case.RequiredLabel && std::string_view(Case.Name) != "for-empty-condition")
			{
				std::string Source = Case.MarkedSource;
				const std::size_t Caret = Source.find('|');
				Source.erase(Caret, 1);
				const std::size_t Start = Caret - (std::string_view(Case.Name) == "for-identifier-suffix" ? 3 : 1);
				const std::size_t End = Start + std::string_view(Case.RequiredLabel).size();
				const std::size_t Line = std::count(Source.begin(), Source.begin() + Caret, '\n');
				const std::size_t Newline = Source.rfind('\n', Caret - 1);
				const std::size_t LineStart = Newline == std::string::npos ? 0 : Newline + 1;
				const std::string Range = "\"range\":{\"end\":{\"character\":" + std::to_string(End - LineStart) + ",\"line\":" + std::to_string(Line) + "},\"start\":{\"character\":" + std::to_string(Start - LineStart) + ",\"line\":" + std::to_string(Line) + "}}";
				if (!Expect(Contains(CompletionItemForLabel(Raw, Case.RequiredLabel), Range),
				        "for recovery must retain the original identifier replacement range"))
				{
					std::cerr << Source << "\nposition=" << Line << ':' << Caret - LineStart
					          << "\nresponse=" << Raw << '\n';
					return EXIT_FAILURE;
				}
			}
			if (std::string_view(Case.Name) == "qualified-typo" && !Expect(Contains(CompletionItemForLabel(Raw, "chrono"), "\"range\":{\"end\":{\"character\":10,\"line\":0},"
			                                                                                                               "\"start\":{\"character\":5,\"line\":0}}"),
			                                                           "qualified correction must replace only the misspelled leaf"))
			{
				return EXIT_FAILURE;
			}
			if (std::string_view(Case.Name) == "qualified-before-error" && !Expect(Contains(CompletionItemForLabel(Raw, "chrono"), "\"range\":{\"end\":{\"character\":10,\"line\":1},"
			                                                                                                                       "\"start\":{\"character\":5,\"line\":1}}"),
			                                                                   "qualified correction after a preceding error must keep its leaf edit"))
			{
				return EXIT_FAILURE;
			}
			if (std::string_view(Case.Name) == "qualified-comment-gap" && !Expect(Contains(CompletionItemForLabel(Raw, "chrono"), "\"range\":{\"end\":{\"character\":21,\"line\":0},"
			                                                                                                                      "\"start\":{\"character\":16,\"line\":0}}"),
			                                                                  "qualified correction must preserve its leaf range after a comment"))
			{
				return EXIT_FAILURE;
			}
			if (std::string_view(Case.Name) == "qualified-newline" && !Expect(Contains(CompletionItemForLabel(Raw, "chrono"), "\"range\":{\"end\":{\"character\":5,\"line\":1},"
			                                                                                                                  "\"start\":{\"character\":0,\"line\":1}}"),
			                                                              "qualified correction must preserve its leaf range across a line"))
			{
				return EXIT_FAILURE;
			}
			if (std::string_view(Case.Name).find("shadowed-std-dot") == 0 && !Expect(CompletionItemForLabel(Raw, "chrono").empty(), "a local std variable must complete its own members"))
			{
				return EXIT_FAILURE;
			}
			const std::string_view Name = Case.Name;
			if (Name.find("print-") == 0)
			{
				const std::string_view Print = CompletionItemForLabel(Raw, "print");
				const bool bCall = Name == "print-call" || Name == "print-full-prefix" || Name == "print-empty-prefix";
				const bool bEmptyPrefix = Name.find("print-empty-") == 0;
				const std::string_view ExpectedText = bCall ? "print(${0})" : "print";
				const int EditStart = Name == "print-reference" ? 30 : 14;
				const int EditEnd = EditStart + (Name == "print-full-prefix" || Name == "print-existing-paren-suffix" ? 5 : 3);
				const std::string ExpectedRange = "\"range\":{\"end\":{\"character\":" + std::to_string(EditEnd) + ",\"line\":0},\"start\":{\"character\":" + std::to_string(EditStart) + ",\"line\":0}}";
				if (!Expect(!Print.empty() && Contains(Print, "\"insertText\":" + JsonQuote(ExpectedText)) && (bEmptyPrefix ? !Contains(Print, "\"textEdit\":") : Contains(Print, "\"newText\":" + JsonQuote(ExpectedText)) && Contains(Print, ExpectedRange)) && Contains(Print, "\"detail\":" + JsonQuote("void print(any value);\nvoid print(string fmt, ...);")) && (Contains(Print, "\"insertTextFormat\":2") == bCall),
				        "print completion edit and snippet format follow call context"))
				{
					std::cerr << Case.Name << " raw response: " << Raw << '\n';
					return EXIT_FAILURE;
				}
			}
		}
	}

	{
		// Clients that did not negotiate snippets must never receive literal
		// tab-stop syntax in either insertText or textEdit.newText.
		constexpr const char* PlainUri = "file:///C:/veroscript-tests/lsp-plain-completion.vs";
		std::ostringstream PlainInput;
		PlainInput << Request(1, "initialize") << Notification("initialized");
		PlainInput << Notification("textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + PlainUri + "\",\"languageId\":\"veroscript\",\"version\":1,"
		                                                              "\"text\":\"void main() { pri; }\"}}");
		PlainInput << Request(2, "textDocument/completion",
		    std::string("{\"textDocument\":{\"uri\":\"") + PlainUri + "\"},\"position\":{\"line\":0,\"character\":17}}");
		PlainInput << Request(3, "shutdown") << Notification("exit");
		std::istringstream PlainIn(PlainInput.str());
		std::ostringstream PlainOut;
		const int PlainExit = RunTranscript(PlainIn, PlainOut);
		const std::string PlainOutput = PlainOut.str();
		const std::string_view Print = CompletionItemForLabel(
		    ResponseForId(PlainOutput, 2), "print");
		if (!Expect(PlainExit == 0 && !Print.empty() && Contains(Print, "\"insertText\":\"print\"") && Contains(Print, "\"newText\":\"print\"") && !Contains(Print, "\"insertTextFormat\":2"),
		        "completion degrades to plain text when snippetSupport is absent"))
		{
			std::cerr << PlainOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		const auto Unique = std::chrono::steady_clock::now().time_since_epoch().count();
		const std::filesystem::path Root = std::filesystem::temp_directory_path() / ("VeroScriptLsp-completion-path-" + std::to_string(Unique));
		std::filesystem::create_directories(Root);
		{
			std::ofstream Module(Root / "helper.vs");
			Module << "void helper() {}\n";
		}
		std::ostringstream PathInput;
		PathInput << Request(1, "initialize") << Notification("initialized");
		int PathRequestId = 2;
		for (const std::string_view Source : {"import \"hel\";", "#include \"hel\""})
		{
			const std::string PathUri = "file:///" +
			    (Root / ("source" + std::to_string(PathRequestId) + ".vs")).generic_string();
			PathInput << Notification(
			    "textDocument/didOpen",
			    "{\"textDocument\":{\"uri\":" + JsonQuote(PathUri) + ",\"languageId\":\"veroscript\",\"version\":1,\"text\":" + JsonQuote(Source) + "}}");
			PathInput << Request(
			    PathRequestId++,
			    "textDocument/completion",
			    "{\"textDocument\":{\"uri\":" + JsonQuote(PathUri) + "},\"position\":{\"line\":0,\"character\":" + std::to_string(Source.find("hel") + 3) + "}}");
		}
		PathInput << Request(PathRequestId, "shutdown") << Notification("exit");
		std::istringstream PathIn(PathInput.str());
		std::ostringstream PathOut;
		const int PathExit = RunTranscript(PathIn, PathOut);
		const std::string PathOutput = PathOut.str();
		const std::string_view ImportItem =
		    CompletionItemForLabel(ResponseForId(PathOutput, 2), "helper.vs");
		const std::string_view IncludeItem =
		    CompletionItemForLabel(ResponseForId(PathOutput, 3), "helper.vs");
		std::error_code RemoveError;
		std::filesystem::remove_all(Root, RemoveError);
		if (!Expect(PathExit == 0 && !ImportItem.empty() && !IncludeItem.empty() && Contains(ImportItem, "\"range\":{\"end\":{\"character\":11,\"line\":0},"
		                                                                                                 "\"start\":{\"character\":8,\"line\":0}}") &&
		            Contains(IncludeItem, "\"detail\":\"re-exported module\""),
		        "import and include path completion must retain exact path edits"))
		{
			std::cerr << PathOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* MacroDocUri = "file:///C:/veroscript-tests/lsp-macro-doc.vs";
		constexpr const char* Source =
		    "/// A documented macro.\n#define DOC 7\nvoid main() {\n\tDO\n}\n";
		std::ostringstream MacroDocInput;
		MacroDocInput << Request(1, "initialize");
		MacroDocInput << Notification("initialized");
		MacroDocInput << Notification("textDocument/didOpen",
		    std::string("{\"textDocument\":{\"uri\":\"") + MacroDocUri + "\",\"languageId\":\"veroscript\",\"version\":1,\"text\":" + JsonQuote(Source) + "}}");
		MacroDocInput << Request(2, "textDocument/completion",
		    std::string("{\"textDocument\":{\"uri\":\"") + MacroDocUri + "\"},\"position\":{\"line\":3,\"character\":3}}");
		MacroDocInput << Request(3, "shutdown");
		MacroDocInput << Notification("exit");
		std::istringstream MacroDocIn(MacroDocInput.str());
		std::ostringstream MacroDocOut;
		const int Exit = RunTranscript(MacroDocIn, MacroDocOut);
		const std::string MacroDocOutput = MacroDocOut.str();
		const std::string_view Item = CompletionItemForLabel(
		    ResponseForId(MacroDocOutput, 2), "DOC");
		if (!Expect(Exit == 0 && Contains(Item, "\"documentation\":\"A documented macro.\""),
		        "LSP macro completion includes leading documentation"))
		{
			std::cerr << MacroDocOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		std::istringstream InvalidLengthIn(
		    "Content-Length: 12junk\r\n\r\n{}");
		std::ostringstream InvalidLengthOut;
		if (!Expect(
		        RunTranscript(InvalidLengthIn, InvalidLengthOut) != 0,
		        "LSP rejects a partially parsed Content-Length"))
		{
			return EXIT_FAILURE;
		}

		std::istringstream DuplicateLengthIn(
		    "Content-Length: 2\r\nContent-Length: 2\r\n\r\n{}");
		std::ostringstream DuplicateLengthOut;
		if (!Expect(
		        RunTranscript(DuplicateLengthIn, DuplicateLengthOut) != 0,
		        "LSP rejects duplicate Content-Length headers"))
		{
			return EXIT_FAILURE;
		}

		std::string DeepJson(300, '[');
		DeepJson += "null";
		DeepJson.append(300, ']');
		std::istringstream DeepJsonIn(Frame(DeepJson));
		std::ostringstream DeepJsonOut;
		if (!Expect(
		        RunTranscript(DeepJsonIn, DeepJsonOut) == 0,
		        "LSP rejects pathological JSON nesting without crashing"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* OverrideUri = "file:///C:/veroscript-tests/lsp-override-action.vs";
		const std::string Source =
		    "class Base { public: virtual int f(int x) { return x; } }\n"
		    "class Derived : public Base {\npublic:\n    \n}\n";
		std::ostringstream OverrideInput;
		OverrideInput << Request(1, "initialize", SnippetInitializeParams)
		              << Notification("initialized");
		OverrideInput << Notification("textDocument/didOpen",
		    "{\"textDocument\":{\"uri\":" + JsonQuote(OverrideUri) + ",\"languageId\":\"veroscript\",\"version\":1,\"text\":" + JsonQuote(Source) + "}}");
		OverrideInput << Request(2, "textDocument/completion",
		    "{\"textDocument\":{\"uri\":" + JsonQuote(OverrideUri) + "},\"position\":{\"line\":3,\"character\":4}}");
		OverrideInput << Request(3, "textDocument/codeAction",
		    "{\"textDocument\":{\"uri\":" + JsonQuote(OverrideUri) + "},\"range\":{\"start\":{\"line\":3,\"character\":4},"
		                                                             "\"end\":{\"line\":3,\"character\":4}},"
		                                                             "\"context\":{\"diagnostics\":[],\"only\":[\"refactor.rewrite\"]}}");
		OverrideInput << Request(4, "shutdown") << Notification("exit");
		std::istringstream OverrideIn(OverrideInput.str());
		std::ostringstream OverrideOut;
		const int Exit = RunTranscript(OverrideIn, OverrideOut);
		const std::string OverrideOutput = OverrideOut.str();
		const std::string_view Completion = ResponseForId(OverrideOutput, 2);
		const std::string_view Action = ResponseForId(OverrideOutput, 3);
		if (!Expect(Exit == 0 && Contains(Completion, "\"label\":\"f\"") && Contains(Completion, "\"insertTextFormat\":2") && Contains(Completion, "\"textEdit\":") && Contains(Completion, "\"character\":4,\"line\":3") && Contains(Completion, "override") && Contains(Action, "\"kind\":\"refactor.rewrite\"") && Contains(Action, "\"newText\":") && Contains(Action, "int f(int x) override") && Contains(Action, "\"character\":4,\"line\":3"),
		        "override completion and code action provide explicit insertion edits"))
		{
			std::cerr << OverrideOutput << '\n';
			return EXIT_FAILURE;
		}
		std::string PlainInput = OverrideInput.str();
		PlainInput.replace(0, Request(1, "initialize", SnippetInitializeParams).size(),
		    Request(1, "initialize", "{}"));
		std::istringstream PlainIn(PlainInput);
		std::ostringstream PlainOut;
		const int PlainExit = RunTranscript(PlainIn, PlainOut);
		const std::string PlainOutput = PlainOut.str();
		const std::string_view PlainCompletion = ResponseForId(PlainOutput, 2);
		const std::string_view PlainOverride =
		    CompletionItemForLabel(PlainCompletion, "f", 0);
		if (!Expect(PlainExit == 0 && Contains(PlainOverride, "int f(int x) override") && Contains(PlainOverride, "\"textEdit\":") && !Contains(PlainOverride, "${0:") && !Contains(PlainOverride, "\"insertTextFormat\":2"),
		        "override completion without snippet support inserts plain method text"))
		{
			std::cerr << PlainOut.str() << '\n';
			return EXIT_FAILURE;
		}

		const std::string LocalizedSource =
		    "interface IValue { virtual int get() = 0; }\n"
		    "class Base { public: virtual int f(int x) { return x; } }\n"
		    "class Derived : public Base, public IValue {\npublic:\n    \n}\n";
		constexpr const char* LocalizedUri =
		    "file:///C:/veroscript-tests/lsp-override-localized.vs";
		const std::string LocalizedInitialize =
		    "{\"locale\":\"zh-CN\",\"capabilities\":{\"textDocument\":{"
		    "\"completion\":{\"completionItem\":{\"snippetSupport\":true}}}}}";
		std::ostringstream LocalizedInput;
		LocalizedInput << Request(1, "initialize", LocalizedInitialize)
		               << Notification("initialized")
		               << Notification("textDocument/didOpen",
		                      "{\"textDocument\":{\"uri\":" + JsonQuote(LocalizedUri) + ",\"languageId\":\"veroscript\",\"version\":1,\"text\":" + JsonQuote(LocalizedSource) + "}}")
		               << Request(2, "textDocument/completion",
		                      "{\"textDocument\":{\"uri\":" + JsonQuote(LocalizedUri) + "},\"position\":{\"line\":4,\"character\":4}}")
		               << Request(3, "textDocument/codeAction",
		                      "{\"textDocument\":{\"uri\":" + JsonQuote(LocalizedUri) + "},\"range\":{\"start\":{\"line\":4,\"character\":4}," + "\"end\":{\"line\":4,\"character\":4}}," + "\"context\":{\"diagnostics\":[],\"only\":[\"refactor.rewrite\"]}}")
		               << Request(4, "shutdown") << Notification("exit");
		std::istringstream LocalizedIn(LocalizedInput.str());
		std::ostringstream LocalizedOut;
		const int LocalizedExit = RunTranscript(LocalizedIn, LocalizedOut);
		const std::string LocalizedOutput = LocalizedOut.str();
		const std::string_view LocalizedCompletion = ResponseForId(LocalizedOutput, 2);
		const std::string_view LocalizedAction = ResponseForId(LocalizedOutput, 3);
		const std::string_view LocalizedOverride = CompletionItemForLabel(
		    LocalizedCompletion, "f", 0);
		const std::string_view LocalizedImplementation = CompletionItemForLabel(
		    LocalizedCompletion, "get", 0);
		(void)VeroScript::Locale::TrySetLanguageFromTag("en-US");
		if (!Expect(LocalizedExit == 0 && Contains(LocalizedOverride, "\"documentation\":\"覆盖继承成员\"") && Contains(LocalizedImplementation, "\"documentation\":\"实现接口成员\"") && Contains(LocalizedAction, JsonQuote("覆盖 int f(int x)")) && Contains(LocalizedAction, JsonQuote("实现 int get()")),
		        "override and interface implementation labels use the selected LSP locale"))
		{
			std::cerr << LocalizedOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* MemberOverloadUri =
		    "file:///C:/veroscript-tests/lsp-member-overloads.vs";
		const std::string MemberOverloadSource =
		    "class Reader { public: int fetch(int value) { return value; } "
		    "int fetch(string value) { return 0; } }\n"
		    "void main() {\n    Reader reader = Reader();\n    reader.fet;\n}\n";
		std::ostringstream MemberInput;
		MemberInput << Request(1, "initialize", SnippetInitializeParams)
		            << Notification("initialized")
		            << Notification("textDocument/didOpen",
		                   "{\"textDocument\":{\"uri\":" + JsonQuote(MemberOverloadUri) + ",\"languageId\":\"veroscript\",\"version\":1,\"text\":" + JsonQuote(MemberOverloadSource) + "}}")
		            << Request(2, "textDocument/completion",
		                   "{\"textDocument\":{\"uri\":" + JsonQuote(MemberOverloadUri) + "},\"position\":{\"line\":3,\"character\":14}}")
		            << Request(3, "shutdown") << Notification("exit");
		std::istringstream MemberIn(MemberInput.str());
		std::ostringstream MemberOut;
		const int MemberExit = RunTranscript(MemberIn, MemberOut);
		const std::string MemberOutput = MemberOut.str();
		const std::string_view Response = ResponseForId(MemberOutput, 2);
		const std::string_view Fetch = CompletionItemForLabel(Response, "fetch", 0);
		if (!Expect(MemberExit == 0 && !Fetch.empty() && CompletionItemForLabel(Response, "fetch", 1).empty() && Contains(Fetch, JsonQuote("int fetch(int value);\nint fetch(string value);")) && Contains(Fetch, "\"newText\":\"fetch(") && Contains(Fetch, "\"insertTextFormat\":2"),
		        "LSP member overload response retains both signatures in one item"))
		{
			std::cerr << MemberOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}
