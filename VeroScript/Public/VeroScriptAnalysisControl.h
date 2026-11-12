// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <exception>
#include <functional>
#include <string_view>
#include <utility>

namespace VeroScript
{
	class FAnalysisCancelled final : public std::exception
	{
	public:
		const char* what() const noexcept override
		{
			return "analysis cancelled";
		}
	};

	class FAnalysisControlScope;
	inline thread_local const FAnalysisControlScope* ActiveAnalysisControl = nullptr;

	/**
	 * Owns its callbacks for synchronous analysis on this thread. Nested scopes
	 * restore their predecessor. Captures must outlive the scope; stages must end
	 * before it does. This does not cancel runtime execution.
	 */
	class FAnalysisControlScope
	{
	public:
		FAnalysisControlScope(std::function<bool()> ShouldCancel,
			std::function<void(std::string_view, double)> RecordStage = {})
			: ShouldCancel(std::move(ShouldCancel)), RecordStage(std::move(RecordStage)),
			  Previous(ActiveAnalysisControl)
		{
			ActiveAnalysisControl = this;
		}
		~FAnalysisControlScope()
		{
			ActiveAnalysisControl = Previous;
		}
		FAnalysisControlScope(const FAnalysisControlScope&) = delete;
		FAnalysisControlScope& operator=(const FAnalysisControlScope&) = delete;

		std::function<bool()> ShouldCancel;
		std::function<void(std::string_view, double)> RecordStage;

	private:
		const FAnalysisControlScope* Previous;
	};

	// A true predicate throws FAnalysisCancelled, distinct from syntax diagnostics.
	inline void CheckAnalysisCancellation()
	{
		if (ActiveAnalysisControl && ActiveAnalysisControl->ShouldCancel
			&& ActiveAnalysisControl->ShouldCancel())
		{
			throw FAnalysisCancelled();
		}
	}

	/**
	 * Name must outlive the stage. Observers run on the analysis thread, receive
	 * elapsed milliseconds, and cannot replace an exception during unwinding.
	 */
	class FAnalysisStage
	{
	public:
		explicit FAnalysisStage(std::string_view Name)
			: Name(Name), Control(ActiveAnalysisControl && ActiveAnalysisControl->RecordStage
				? ActiveAnalysisControl
								  : nullptr)
		{
			if (Control)
			{
				Start = std::chrono::steady_clock::now();
			}
		}
		~FAnalysisStage()
		{
			if (Control)
			{
				const double Milliseconds = std::chrono::duration<double, std::milli>(
					std::chrono::steady_clock::now() - Start)
												.count();
				// Profiling must not replace a cancellation/parser exception during
				// stack unwinding, or terminate the analysis if an observer throws.
				try
				{
					Control->RecordStage(Name, Milliseconds);
				}
				catch (...)
				{
				}
			}
		}
		FAnalysisStage(const FAnalysisStage&) = delete;
		FAnalysisStage& operator=(const FAnalysisStage&) = delete;

	private:
		std::string_view Name;
		const FAnalysisControlScope* Control;
		std::chrono::steady_clock::time_point Start;
	};
}
