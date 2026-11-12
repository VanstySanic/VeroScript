// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptHost.h"

#include <cstdint>
#include <ostream>
#include <streambuf>
#include <string>

namespace VeroScript
{
	namespace Detail
	{
		enum class EHostStreamMode : std::uint8_t
		{
			// One print() call maps to one OnPrintLine call; emit on flush
			// with one trailing newline removed.
			PrintFlush,
			// Diagnostics: emit each \\n-terminated line.
			DiagnosticLines,
		};

		class FHostLineStreambuf final : public std::streambuf, public Console::IHost
		{
		public:
			//~ Begin std::streambuf Interface
			Console::FInfo QueryConsole() override
			{
				try
				{
					return Mode == EHostStreamMode::PrintFlush ? Host.QueryConsole() : Console::FInfo{};
				}
				catch (...)
				{
					return {};
				}
			}
			bool HasConsoleTextOutput() const override
			{
				try
				{
					return Mode == EHostStreamMode::PrintFlush && Host.HasConsoleTextOutput();
				}
				catch (...)
				{
					return false;
				}
			}
			bool WriteConsoleText(std::string_view Text) override
			{
				try
				{
					FlushRemainder();
					return Host.WriteConsoleText(Text);
				}
				catch (...)
				{
					return false;
				}
			}
			bool FlushConsoleText() override
			{
				try
				{
					return Host.FlushConsoleText();
				}
				catch (...)
				{
					return false;
				}
			}
			std::ostream* ConsoleOutputStream() override
			{
				try
				{
					return Host.ConsoleOutputStream();
				}
				catch (...)
				{
					return nullptr;
				}
			}
			bool ExecuteConsole(Console::EOperation Operation, std::int32_t First, std::int32_t Second) override
			{
				try
				{
					return Mode == EHostStreamMode::PrintFlush &&
						Host.ExecuteConsole(Operation, First, Second);
				}
				catch (...)
				{
					return false;
				}
			}
			//~ End std::streambuf Interface
			FHostLineStreambuf(IEngineHost& InHost, const EHostStreamMode InMode)
				: Host(InHost)
				, Mode(InMode)
			{
			}

			void FlushRemainder()
			{
				if (Mode == EHostStreamMode::PrintFlush)
				{
					EmitPrintBuffer();
					return;
				}
				if (Buffer.empty())
				{
					return;
				}
				EmitDiagnosticBuffer();
			}

			void DiscardRemainder() noexcept
			{
				Buffer.clear();
			}

		protected:
			//~ Begin std::streambuf Interface
			int_type overflow(const int_type CharCode) override
			{
				if (traits_type::eq_int_type(CharCode, traits_type::eof()))
				{
					return traits_type::not_eof(CharCode);
				}

				const char Ch = traits_type::to_char_type(CharCode);
				if (Mode == EHostStreamMode::DiagnosticLines && Ch == '\n')
				{
					EmitDiagnosticBuffer();
					return CharCode;
				}

				Buffer.push_back(Ch);
				return CharCode;
			}

			int sync() override
			{
				if (Mode == EHostStreamMode::PrintFlush)
				{
					EmitPrintBuffer();
				}
				return 0;
			}

			std::streamsize xsputn(const char* Data, const std::streamsize Count) override
			{
				if (Data == nullptr || Count <= 0)
				{
					return 0;
				}

				if (Mode == EHostStreamMode::PrintFlush)
				{
					Buffer.append(Data, static_cast<std::size_t>(Count));
					return Count;
				}

				std::streamsize Written = 0;
				while (Written < Count)
				{
					const char Ch = Data[Written];
					++Written;
					if (Ch == '\n')
					{
						EmitDiagnosticBuffer();
						continue;
					}
					Buffer.push_back(Ch);
				}
				return Written;
			}
			//~ End std::streambuf Interface

		private:
			void EmitDiagnosticBuffer()
			{
				try
				{
					Host.OnDiagnosticLine(Buffer);
				}
				catch (...)
				{
					// Delivery may already have caused host side effects. A failure
					// must not leave the event queued for reuse or destruction.
					Buffer.clear();
					throw;
				}
				Buffer.clear();
			}

			void EmitPrintBuffer()
			{
				if (Buffer.empty())
				{
					return;
				}
				std::string_view Line = Buffer;
				if (!Line.empty() && Line.back() == '\n')
				{
					Line.remove_suffix(1);
				}
				try
				{
					Host.OnPrintLine(Line);
				}
				catch (...)
				{
					Buffer.clear();
					throw;
				}
				Buffer.clear();
			}

			IEngineHost& Host;
			EHostStreamMode Mode = EHostStreamMode::PrintFlush;
			std::string Buffer;
		};

		class FHostOStream final : public std::ostream
		{
		public:
			~FHostOStream() noexcept override
			{
				// Host callbacks are user code and are not declared noexcept. Never let
				// a final unterminated line turn ordinary bridge destruction into
				// std::terminate; write/flush calls made while the bridge is alive still
				// report stream failure through the normal ostream state.
				try
				{
					Buffer.FlushRemainder();
				}
				catch (...)
				{
				}
			}

			explicit FHostOStream(IEngineHost& Host, const EHostStreamMode Mode)
				: std::ostream(nullptr)
				, Buffer(Host, Mode)
			{
				rdbuf(&Buffer);
			}

			void ResetForReuse() noexcept
			{
				try
				{
					Buffer.FlushRemainder();
				}
				catch (...)
				{
					// A host callback has already observed the attempted delivery. Drop
					// the remainder just as destruction would, rather than replaying it
					// during the next logically independent Session call.
					Buffer.DiscardRemainder();
				}
				clear();
			}


		private:
			FHostLineStreambuf Buffer;
		};
	}

	struct FHostStreamBridge::FState
	{
		explicit FState(IEngineHost& Host)
			: Output(Host, Detail::EHostStreamMode::PrintFlush)
			, Error(Host, Detail::EHostStreamMode::DiagnosticLines)
		{
		}

		Detail::FHostOStream Output;
		Detail::FHostOStream Error;

	};

	FHostStreamBridge::FHostStreamBridge(IEngineHost& Host)
		: State(std::make_unique<FState>(Host))
	{
	}

	FHostStreamBridge::~FHostStreamBridge() = default;

	std::ostream& FHostStreamBridge::OutputStream()
	{
		return State->Output;
	}

	std::ostream& FHostStreamBridge::ErrorStream()
	{
		return State->Error;
	}

	void FHostStreamBridge::ResetForReuse() noexcept
	{
		State->Output.ResetForReuse();
		State->Error.ResetForReuse();
	}

	void FStreamEngineHost::OnPrintLine(const std::string_view Line)
	{
		Output << Line << '\n';
		Output.flush();
	}

	void FStreamEngineHost::OnDiagnosticLine(const std::string_view Line)
	{
		Error << Line << '\n';
		Error.flush();
	}

	Console::FInfo FStreamEngineHost::QueryConsole()
	{
		return Console::Query(Output);
	}
	bool FStreamEngineHost::ExecuteConsole(Console::EOperation Operation, std::int32_t First, std::int32_t Second)
	{
		return Console::Execute(Output, Operation, First, Second);
	}
	bool FStreamEngineHost::WriteConsoleText(std::string_view Text)
	{
		Output << Text;
		return static_cast<bool>(Output);
	}
	bool FStreamEngineHost::FlushConsoleText()
	{
		Output.flush();
		return static_cast<bool>(Output);
	}

	FStreamEngineHost::FStreamEngineHost(std::ostream& InOutput, std::ostream& InError)
		: Output(InOutput)
		, Error(InError)
	{
	}
}
