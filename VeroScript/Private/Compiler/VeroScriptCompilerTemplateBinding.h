// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <unordered_map>

namespace VeroScript::CompilerDetail
{
	class FTemplateBindingScope
	{
	public:
		FTemplateBindingScope(std::unordered_map<std::string, std::string>& Target,
		    const std::unordered_map<std::string, std::string>& Bindings, const bool Replace = false)
		    : Target(Target)
		{
			if (&Target == &Bindings || Target == Bindings || (!Replace && Bindings.empty())) return;
			Previous = Replace ? Bindings : Target;
			if (!Replace)
			{
				for (const auto& Binding : Bindings)
				{
					Previous[Binding.first] = Binding.second;
				}
			}
			Target.swap(Previous);
			bChanged = true;
		}

		~FTemplateBindingScope()
		{
			if (bChanged)
			{
				Target.swap(Previous);
			}
		}
		FTemplateBindingScope(const FTemplateBindingScope&) = delete;
		FTemplateBindingScope& operator=(const FTemplateBindingScope&) = delete;

	private:
		std::unordered_map<std::string, std::string>& Target;
		std::unordered_map<std::string, std::string> Previous;
		bool bChanged = false;
	};
}
