// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiScriptCatalog.h"
#include <algorithm>
#include <limits>
#include <set>

namespace VeroScript::Gui
{
	namespace
	{
		class FCatalog final : public IHostBindingCatalog
		{
		public:
			//~ Begin IHostBindingCatalog Interface
			const std::vector<FNativeFunction>& GetNativeFunctions() const override
			{
				return Natives;
			}
			const std::vector<FHostFunctionDesc>& GetFunctions() const override
			{
				return Functions;
			}
			const std::vector<FHostTypeDesc>& GetTypes() const override
			{
				return Types;
			}
			const std::vector<FHostTypeAliasDesc>& GetTypeAliases() const override
			{
				return Aliases;
			}
			const std::vector<FGenericHostTypeTemplateDesc>& GetGenericTypeTemplates() const override
			{
				return Generics;
			}
			const std::vector<FHostAnnotationDesc>& GetAnnotations() const override
			{
				return Annotations;
			}
			const FHostFunctionDesc* FindFunction(std::string_view Name) const override
			{
				for (const auto& Item : Functions)
				{
					if (Item.Name == Name)
					{
						return &Item;
					}
				}
				return nullptr;
			}
			const FHostTypeDesc* FindType(std::string_view Name) const override
			{
				for (const auto& Item : Types)
				{
					if (Item.Name == Name)
					{
						return &Item;
					}
				}
				return nullptr;
			}
			const FHostTypeDesc* FindType(FHostTypeId Id) const override
			{
				for (const auto& Item : Types)
				{
					if (Item.Id == Id)
					{
						return &Item;
					}
				}
				return nullptr;
			}
			//~ End IHostBindingCatalog Interface

			bool Initialize(std::shared_ptr<const IHostBindingCatalog> InBase,
							std::shared_ptr<const FHostBindingRegistry> InGui, std::uint32_t& Offset,
							std::string& Error)
			{
				Base = std::move(InBase);
				Gui = std::move(InGui);
				Offset = 0;
				if (Base)
				{
					Natives = Base->GetNativeFunctions();
					Functions = Base->GetFunctions();
					Types = Base->GetTypes();
					Aliases = Base->GetTypeAliases();
					Generics = Base->GetGenericTypeTemplates();
					Annotations = Base->GetAnnotations();
					for (const auto& Type : Types)
					{
						Offset = std::max(Offset, Type.Id.Value);
					}
				}
				std::uint32_t Maximum = 0;
				for (const auto& Type : Gui->GetTypes())
				{
					Maximum = std::max(Maximum, Type.Id.Value);
				}
				if (Maximum > std::numeric_limits<std::uint32_t>::max() - Offset)
				{
					Error = "gui.host_type_id_limit";
					return false;
				}
				std::set<std::string> Names;
				for (const auto& Type : Types)
				{
					Names.insert(Type.Name);
				}
				for (const auto& Alias : Aliases)
				{
					Names.insert(Alias.Name);
				}
				for (auto Type : Gui->GetTypes())
				{
					if (!Names.insert(Type.Name).second)
					{
						Error = "gui.host_type_conflict: " + Type.Name;
						return false;
					}
					Type.Id.Value += Offset;
					Types.push_back(std::move(Type));
				}
				Names.clear();
				for (const auto& Native : Natives)
				{
					Names.insert(Native.Name);
				}
				for (const auto& Native : Gui->GetNativeFunctions())
				{
					if (!Names.insert(Native.Name).second)
					{
						Error = "gui.host_native_conflict: " + Native.Name;
						return false;
					}
					Natives.push_back(Native);
				}
				Functions.insert(Functions.end(), Gui->GetFunctions().begin(), Gui->GetFunctions().end());
				return true;
			}

		private:
			std::shared_ptr<const IHostBindingCatalog> Base, Gui;
			std::vector<FNativeFunction> Natives;
			std::vector<FHostFunctionDesc> Functions;
			std::vector<FHostTypeDesc> Types;
			std::vector<FHostTypeAliasDesc> Aliases;
			std::vector<FGenericHostTypeTemplateDesc> Generics;
			std::vector<FHostAnnotationDesc> Annotations;
		};
	} // namespace

	std::shared_ptr<const IHostBindingCatalog> MergeGuiScriptCatalog(std::shared_ptr<const IHostBindingCatalog> Base,
																	 std::shared_ptr<const FHostBindingRegistry> Gui,
																	 std::uint32_t& TypeOffset, std::string& Error)
	{
		auto Result = std::make_shared<FCatalog>();
		if (!Result->Initialize(std::move(Base), std::move(Gui), TypeOffset, Error))
		{
			return {};
		}
		return Result;
	}
} // namespace VeroScript::Gui
