// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptConsole.h"

#include <iosfwd>
#include <memory>
#include <string_view>

namespace VeroScript
{
	/**
	 * Host sink for in-process embedding.
	 * Each print() → one OnPrintLine (payload may contain embedded newlines).
	 * Each diagnostic → one OnDiagnosticLine.
	 * Callbacks do not include a trailing newline.
	 * Line views are borrowed for the callback only. If a callback throws, the
	 * stream reports failure and the attempted event is discarded, never replayed.
	 * Destruction suppresses callback exceptions; ResetForReuse restores stream state.
	 */
	class IEngineHost
	{
	public:
		virtual ~IEngineHost() = default;

		virtual void OnPrintLine(std::string_view Line) = 0;
		virtual void OnDiagnosticLine(std::string_view Line) = 0;
		// Opt in only when the host can execute terminal operations. Log/UI text
		// sinks remain plain by default. See Console::IHost for callback semantics.
		virtual Console::FInfo QueryConsole()
		{
			return {};
		}
		virtual bool ExecuteConsole(Console::EOperation, std::int32_t, std::int32_t)
		{
			return false;
		}
		virtual bool HasConsoleTextOutput() const
		{
			return false;
		}
		virtual bool WriteConsoleText(std::string_view)
		{
			return false;
		}
		virtual bool FlushConsoleText()
		{
			return true;
		}
		virtual std::ostream* ConsoleOutputStream()
		{
			return nullptr;
		}
	};

	/**
	 * Adapts std::ostream pair to IEngineHost (CLI / tests).
	 */
	class FStreamEngineHost final : public IEngineHost
	{
	public:
		//~ Begin IEngineHost Interface
		void OnPrintLine(std::string_view Line) override;

		void OnDiagnosticLine(std::string_view Line) override;
		Console::FInfo QueryConsole() override;
		bool ExecuteConsole(Console::EOperation Operation, std::int32_t First, std::int32_t Second) override;
		bool HasConsoleTextOutput() const override
		{
			return true;
		}
		bool WriteConsoleText(std::string_view Text) override;
		bool FlushConsoleText() override;
		std::ostream* ConsoleOutputStream() override
		{
			return &Output;
		}
		//~ End IEngineHost Interface

		FStreamEngineHost(std::ostream& InOutput, std::ostream& InError);

	private:
		std::ostream& Output;
		std::ostream& Error;
	};

	/**
	 * ostream views that forward complete lines to IEngineHost.
	 * Keep alive for the duration of Run/Check.
	 */
	class FHostStreamBridge
	{
	public:
		explicit FHostStreamBridge(IEngineHost& Host);
		~FHostStreamBridge();

		FHostStreamBridge(const FHostStreamBridge&) = delete;
		FHostStreamBridge& operator=(const FHostStreamBridge&) = delete;

		std::ostream& OutputStream();
		std::ostream& ErrorStream();
		// Flush any unterminated call-local text and restore stream state so the
		// bridge can be reused by a sequential Session invocation.
		void ResetForReuse() noexcept;

	private:
		struct FState;
		std::unique_ptr<FState> State;
	};
}
