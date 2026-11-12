// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "Compilation/VeroScriptGuiScriptExpression.h"
#include <iostream>
#include <stdexcept>

namespace
{
	using namespace VeroScript::Gui;

	void Check(const std::string& Source, const std::string& Expected, bool bExternal = true, bool bEvent = false)
	{
		FMarkupExpression Expression;
		Expression.Text = Source;
		Expression.Location.Source = "captures.vswml";
		if (bEvent)
		{
			Expression.Event = VeroScript::Platform::EEventType::PointerDown;
		}
		std::string Code, Error;
		std::vector<FMarkupTextSpan> Spans;
		if (!RewriteMarkupExpression(Expression, bExternal, {"Count"}, Code, Error, &Spans))
		{
			throw std::runtime_error(Error);
		}
		if (Code != Expected)
		{
			throw std::runtime_error("incorrect capture scope: " + Code);
		}
		if (Spans.empty())
		{
			throw std::runtime_error("expression source mappings were lost");
		}
		std::size_t PreviousEnd = 0;
		for (const auto& Span : Spans)
		{
			if (Span.Begin < PreviousEnd || Span.Begin >= Span.End || Span.End > Code.size() ||
			    Span.SourceBegin > Source.size() || Span.End - Span.Begin > Source.size() - Span.SourceBegin ||
			    Code.substr(Span.Begin, Span.End - Span.Begin) != Source.substr(Span.SourceBegin, Span.End - Span.Begin))
			{
				throw std::runtime_error("capture rewriting changed a copied diagnostic source mapping");
			}
			PreviousEnd = Span.End;
		}
	}
}

int main()
{
	try
	{
		Check("([Count = Count + 1]()->double { return Count; })()",
		    "([Count = context.Count + 1]()->double { return Count; })()");
		Check("([Count = Count + 1, next = Count + 2]() mutable ->double { Count++; return Count + next; })()",
		    "([Count = context.Count + 1, next = context.Count + 2]() mutable ->double { Count++; return Count + next; })()");
		Check("([Count = ([copy = Count]()->double { return copy; })()]()->double { return Count; })()",
		    "([Count = ([copy = context.Count]()->double { return copy; })()]()->double { return Count; })()");
		Check("auto apply = [&value = Count]()->void { value++; }; apply();",
		    "auto apply = [&value = context.Count]()->void { value++; }; apply();", true, true);
		Check("double Count = 2; auto apply = [&value = Count]()->void { value++; }; apply();",
		    "double Count = 2; auto apply = [&value = Count]()->void { value++; }; apply();", true, true);
		Check("double Count = 2; auto read = [Count]()->double { return Count; };",
		    "double Count = 2; auto read = [Count]()->double { return Count; };", true, true);
		Check("auto read = [&]()->double { static double total = 0; total += Count; return total; };",
		    "auto read = [&]()->double { static double total = 0; total += context.Count; return total; };", true, true);
		Check("([snapshot = context.Count]()->double { return snapshot; })()",
		    "([snapshot = this.Count]()->double { return snapshot; })()", false);
		Check("([Count =\n Count + 1]()->double\n { return Count; })()",
		    "([Count =\n context.Count + 1]()->double\n { return Count; })()");
		Check("([context, read = Helpers::Identity<double>]()->double { return read(Count); })()",
		    "([context, read = Helpers::Identity<double>]()->double { return read(context.Count); })()");
		Check("([context, read = ::Helpers::Identity<double>]()->double { return read(Count); })()",
		    "([context, read = ::Helpers::Identity<double>]()->double { return read(context.Count); })()");
		Check("auto read = Helpers::Identity<double>; static double Count = 2; Count = read(Count);",
		    "auto read = Helpers::Identity<double>; static double Count = 2; Count = read(Count);", true, true);
		Check("([read = Helpers::Identity<double>](double Count)->double { return read(Count); })(Count)",
		    "([read = Helpers::Identity<double>](double Count)->double { return read(Count); })(context.Count)");
		for (const auto* Name : {"self", "context", "elements", "sender", "event", "arg0", "arg1", "arg2"})
		{
			FMarkupExpression Expression;
			Expression.Text = "[" + std::string(Name) + " = Count]()->double { return 0; }";
			std::string Code, Error;
			if (RewriteMarkupExpression(Expression, true, {"Count"}, Code, Error) ||
			    Error != "gui.reserved_binding_name: " + std::string(Name))
			{
				throw std::runtime_error("initialized capture shadowed a reserved GUI binding: " + std::string(Name));
			}
		}
		std::cout << "GUI expression capture scopes and diagnostic mappings passed\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
