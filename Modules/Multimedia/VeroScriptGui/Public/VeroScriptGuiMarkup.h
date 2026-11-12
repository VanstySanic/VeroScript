// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGuiWidget.h"
#include <string_view>

namespace VeroScript::Gui
{
	struct FMarkupData;
	struct FStyleSheetData;
	struct FMarkupRuntime;

	struct FMarkupSource
	{
		// Identity is opaque to the compiler; readers resolve references relative to it.
		std::string Identity, Text;
	};

	struct FMarkupTextSpan
	{
		// Decoded byte interval and the corresponding original byte offset.
		std::size_t Begin = 0, End = 0, SourceBegin = 0;
	};

	struct FMarkupLocation
	{
		std::string Source;
		std::size_t Offset = 0, Line = 1, Column = 1;
	};

	struct FMarkupDiagnostic
	{
		std::string Format() const;

		FMarkupLocation Location;
		std::string Message;
	};

	struct FMarkupType
	{
		std::string Name, Mark, DefaultId;
		// Return a fresh, owner-thread, unattached widget with fresh descendants.
		std::function<std::shared_ptr<FWidget>()> Create;
	};

	struct FMarkupExpression
	{
		FMarkupLocation Location;
		std::string Text;
		EProperty Property = EProperty::Count;
		Platform::EEventType Event = Platform::EEventType::None;
		bool bCode = false;
	};

	struct FMarkupScript
	{
		FMarkupSource Source;
		FMarkupLocation Location;
		std::string Type;
		std::vector<FMarkupSource> Imports;
		std::weak_ptr<FWidget> Root;
		std::function<std::shared_ptr<FWidget>(const std::string&)> FindElement;
		bool bComponent = false, bInline = false;
		std::vector<FMarkupTextSpan> TextSpans;
	};

	/** A typed script adapter. Resolve once while building; callbacks execute on the GUI thread.
	 * Contexts retain their compiled program/state without retaining the widget tree. */
	class IMarkupScriptContext
	{
	public:
		virtual ~IMarkupScriptContext() = default;
		virtual bool Synchronize(std::string& Error)
		{
			Error.clear();
			return true;
		}
		virtual bool ResolveGetter(const FMarkupExpression& Expression, std::weak_ptr<FWidget> Self,
								   FPropertyGetter& Getter, std::string& Error) = 0;
		virtual bool ResolveEvent(const FMarkupExpression& Expression, std::weak_ptr<FWidget> Self,
								  std::function<void(FEvent&)>& Callback, std::string& Error) = 0;
	};

	using FMarkupReader = std::function<bool(const std::string& Requester, const std::string& Reference,
											 FMarkupSource& Source, std::string& Error)>;
	using FMarkupScriptFactory = std::function<std::shared_ptr<IMarkupScriptContext>(
		const FMarkupScript& Script, const std::vector<FMarkupExpression>& Expressions, FMarkupDiagnostic& Diagnostic)>;

	struct FMarkupCompileOptions
	{
		FMarkupReader Read;
		FResourceResolver ResolveResource;
		std::vector<FMarkupType> Types;
		std::function<void()> CheckCancelled;
		std::size_t MaxSourceBytes = 16 * 1024 * 1024, MaxNodes = 100000, MaxDepth = 256, MaxDependencies = 4096;
	};

	/** Immutable, backend-independent selector rules. Unsupported CSS is diagnosed. */
	class FStyleSheet
	{
		friend struct FMarkupRuntime;
		friend class FMarkupProgram;
		friend class FWidget;
		friend class FContext;

	public:
		static std::shared_ptr<const FStyleSheet> Compile(const FMarkupSource& Source, FMarkupDiagnostic& Diagnostic,
														  FResourceResolver ResolveResource = {});
		/** Schema-aware clients validate declarations against the target widget profile at evaluation. */
		static std::shared_ptr<const FStyleSheet> CompileTyped(const FMarkupSource& Source,
															   FMarkupDiagnostic& Diagnostic,
															   FResourceResolver ResolveResource = {});

	private:
		explicit FStyleSheet(std::shared_ptr<const FStyleSheetData> Data);

		std::shared_ptr<const FStyleSheetData> Data;
	};

	/** Compiled ML owns its source/dependencies. Each Create produces independent nodes and script state. */
	class FMarkupProgram
	{
	public:
		static std::shared_ptr<const FMarkupProgram>
		Compile(const FMarkupSource& Source, const FMarkupCompileOptions& Options, FMarkupDiagnostic& Diagnostic);
		const std::vector<FMarkupSource>& GetDependencies() const;
		std::shared_ptr<FWidget> Create(const FMarkupScriptFactory& Scripts, FMarkupDiagnostic& Diagnostic,
										std::shared_ptr<IMarkupScriptContext> BindingContext = {}) const;
		static std::shared_ptr<FWidget> Find(const std::shared_ptr<FWidget>& ScopeRoot, const std::string& Id);
		static bool SetTheme(const std::shared_ptr<FWidget>& Root, std::shared_ptr<const FStyleSheet> Theme,
							 std::string& Error);

	private:
		explicit FMarkupProgram(std::shared_ptr<const FMarkupData> Data);

		std::shared_ptr<const FMarkupData> Data;
	};

	/** Filesystem convenience reader. Other hosts may supply module/resource-provider readers instead. */
	bool ReadMarkupFile(const std::string& Requester, const std::string& Reference, FMarkupSource& Source,
						std::string& Error);
} // namespace VeroScript::Gui
