// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace VeroScript
{
	struct FProgram;
	struct FExpr;
	class IHostBindingCatalog;

	/** Signature-only queries over immutable, prepared declarations. The program and
	 * bindings must outlive this query. No bodies, processors, or native calls run.
	 * Results use source lookup spelling (not display text); empty means unresolved.
	 * Callers retain responsibility for visibility/access filtering of query results.
	 * Each instance belongs to one analysis thread; cancellation propagates normally.
	 */
	class FTemplateCallTypeQuery
	{
	  public:
		FTemplateCallTypeQuery(const FProgram &Program, const IHostBindingCatalog *Bindings, const std::string &Module,
							   const std::string &Namespace);
		~FTemplateCallTypeQuery();
		FTemplateCallTypeQuery(const FTemplateCallTypeQuery &) = delete;
		FTemplateCallTypeQuery &operator=(const FTemplateCallTypeQuery &) = delete;

		// An empty owner selects a free function. A non-null argument pointer also
		// represents an explicit empty application, such as method<> or function<>.
		std::string ResolveReturnType(const std::string &Owner, const std::string &Name,
									  const std::vector<std::string> &Arguments,
									  const std::vector<std::string> *ExplicitArguments = nullptr,
									  bool ReceiverIsConst = false, bool StaticOnly = false);
		// For expressions independent of caller locals, including literal arguments.
		std::string ResolveExpressionType(const FExpr &Expression);

	  private:
		struct FState;
		std::unique_ptr<FState> State;
	};
} // namespace VeroScript
