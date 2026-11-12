// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCpp.h"
#include "VeroScriptEngine.h"
#include "VeroScriptHost.h"
#include "VeroScriptSession.h"

#include <iostream>
#include <initializer_list>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

static_assert(std::is_abstract_v<VeroScript::IEngineHost>);
static_assert(std::is_destructible_v<VeroScript::FEngineConfig>);
static_assert(!std::is_copy_constructible_v<VeroScript::VeroScriptSession>);

int main()
{
	class FThrowingHost final : public VeroScript::IEngineHost
	{
	public:
		//~ Begin VeroScript::IEngineHost Interface
		void OnPrintLine(std::string_view Line) override
		{
			++PrintCalls;
			LastPrint.assign(Line);
			if (bThrow)
			{
				throw std::runtime_error("host output failure");
			}
		}

		void OnDiagnosticLine(std::string_view Line) override
		{
			++DiagnosticCalls;
			LastDiagnostic.assign(Line);
			if (bThrow)
			{
				throw std::runtime_error("host diagnostic failure");
			}
		}
		//~ End VeroScript::IEngineHost Interface
		bool bThrow = true;
		int PrintCalls = 0;
		int DiagnosticCalls = 0;
		std::string LastPrint;
		std::string LastDiagnostic;
	};

	// An unterminated buffered line is delivered by the stream destructor. A
	// throwing host must not terminate the embedding process during that cleanup.
	FThrowingHost Host;
	{
		VeroScript::FHostStreamBridge Bridge(Host);
		Bridge.OutputStream() << "unterminated";
		Bridge.ErrorStream() << "unterminated";
	}
	if (Host.PrintCalls != 1 || Host.DiagnosticCalls != 1) return 1;
	FThrowingHost FailedHost;
	{
		VeroScript::FHostStreamBridge Bridge(FailedHost);
		Bridge.OutputStream() << "failed print\n" << std::flush;
		Bridge.ErrorStream() << "failed diagnostic\n";
	}
	if (FailedHost.PrintCalls != 1 || FailedHost.DiagnosticCalls != 1)
	{
		std::cerr << "host callback failure replayed during destruction\n";
		return 1;
	}
	// Delivery that throws is still one delivery attempt: reuse and destruction
	// must not replay an event the host may already have processed.
	for (const bool CharacterNewline : {false, true})
	{
		FThrowingHost ReusedHost;
		{
			VeroScript::FHostStreamBridge Bridge(ReusedHost);
			Bridge.OutputStream() << "failed print\n" << std::flush;
			if (CharacterNewline)
			{
				Bridge.ErrorStream() << "failed diagnostic";
				Bridge.ErrorStream().put('\n');
			}
			else
			{
				Bridge.ErrorStream() << "failed diagnostic\n";
			}
			if (!Bridge.OutputStream().bad() || !Bridge.ErrorStream().bad()) return 1;
			Bridge.ResetForReuse();
			if (ReusedHost.PrintCalls != 1 || ReusedHost.DiagnosticCalls != 1)
			{
				std::cerr << "host callback failure replayed a delivered event\n";
				return 1;
			}
			ReusedHost.bThrow = false;
			Bridge.OutputStream() << "next print\n" << std::flush;
			Bridge.ErrorStream() << "next diagnostic\n";
			if (!Bridge.OutputStream().good() || !Bridge.ErrorStream().good() || ReusedHost.LastPrint != "next print" || ReusedHost.LastDiagnostic != "next diagnostic")
			{
				return 1;
			}
		}
		if (ReusedHost.PrintCalls != 2 || ReusedHost.DiagnosticCalls != 2) return 1;
	}
	// The bridge must preserve ordinary ostream exception-mask behavior as well
	// as badbit-only behavior, and recover without relaxing the caller's mask.
	FThrowingHost ExceptionHost;
	{
		VeroScript::FHostStreamBridge Bridge(ExceptionHost);
		Bridge.OutputStream().exceptions(std::ios::badbit);
		Bridge.ErrorStream().exceptions(std::ios::badbit);
		bool PrintThrew = false;
		bool DiagnosticThrew = false;
		try
		{
			Bridge.OutputStream() << "failed print\n"
								  << std::flush;
		}
		catch (const std::runtime_error &Error)
		{
			PrintThrew = std::string_view(Error.what()) == "host output failure";
		}
		try
		{
			Bridge.ErrorStream() << "failed diagnostic\n";
		}
		catch (const std::runtime_error &Error)
		{
			DiagnosticThrew = std::string_view(Error.what()) == "host diagnostic failure";
		}
		if (!PrintThrew || !DiagnosticThrew) return 1;
		Bridge.ResetForReuse();
		if (ExceptionHost.PrintCalls != 1 || ExceptionHost.DiagnosticCalls != 1 || Bridge.OutputStream().exceptions() != std::ios::badbit || Bridge.ErrorStream().exceptions() != std::ios::badbit)
		{
			return 1;
		}
		ExceptionHost.bThrow = false;
		Bridge.OutputStream() << "next print\n" << std::flush;
		Bridge.ErrorStream() << "next diagnostic\n";
		if (!Bridge.OutputStream().good() || !Bridge.ErrorStream().good() || ExceptionHost.LastPrint != "next print" || ExceptionHost.LastDiagnostic != "next diagnostic")
		{
			return 1;
		}
	}
	if (ExceptionHost.PrintCalls != 2 || ExceptionHost.DiagnosticCalls != 2) return 1;
	return 0;
}
