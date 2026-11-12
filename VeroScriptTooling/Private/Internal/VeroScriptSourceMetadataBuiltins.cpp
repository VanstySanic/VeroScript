// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSourceMetadataBuiltins.h"
#include <map>

namespace VeroScript::Tooling::Detail
{
	const std::vector<FSourceMetadataMethod>& SourceMetadataBuiltinMethods(std::string_view Owner)
	{
		static const auto Methods = []
		{
			std::map<std::string_view, std::vector<FSourceMetadataMethod>> Table;
			const std::vector<FSourceMetadataMethod> Common{
				{"annotation_count", "int () const"}, {"annotation", "annotation_info (int index) const"},
				{"annotations", "array<annotation_info> () const"},
				{"find_annotations", "array<annotation_info> (string name) const"},
				{"comment_count", "int () const"}, {"comment", "comment_info (int index) const"},
				{"comments", "array<comment_info> () const"}, {"documentation", "string () const"},
				{"comments_available", "bool () const"}, {"comments_retention", "int () const"},
				{"source_metadata", "array<source_node_info> () const"}};
			for (const auto Name : {"type", "field_info", "method_info", "function_info", "global_info", "module_info", "enum_member_info"})
			{
				Table[Name] = Common;
				Table[Name].push_back({"declaration", "source_node_info () const"});
			}
			for (const auto Name : {"source_node_info", "annotation_info", "annotation_argument_info", "comment_info", "annotation_definition_info"})
			{
				auto& M = Table[Name];
				M = {{"is_valid", "bool () const"}, {"name", "string () const"},
					{"source_id", "string () const"}, {"line", "int () const"}, {"column", "int () const"}};
				if (std::string_view(Name) == "source_node_info" || std::string_view(Name) == "comment_info")
				{
					M.insert(M.end(),
						{{"origin_source_id", "string () const"}, {"origin_processor_id", "string () const"},
							{"generation_id", "string () const"}, {"origin_line", "int () const"},
							{"origin_column", "int () const"}});
				}

				if (std::string_view(Name) == "source_node_info")
				{
					M.insert(M.end(), Common.begin(), Common.end());
					M.insert(M.end(), {{"kind", "string () const"}, {"id", "string () const"},
						{"owner_id", "string () const"}, {"parent_id", "string () const"}, {"type_name", "string () const"},
						{"parameter_count", "int () const"}, {"parameter", "source_node_info (int index) const"}});
				}
				else if (std::string_view(Name) == "annotation_info" ||
					std::string_view(Name) == "annotation_argument_info")
				{
					M.insert(M.end(), {{"raw_text", "string () const"}, {"argument_count", "int () const"},
						{"argument", "annotation_argument_info (int index) const"},
						{"arguments", "array<annotation_argument_info> () const"},
						{"find_arguments", "array<annotation_argument_info> (string name) const"},
						{"definition", "annotation_definition_info () const"}});
					if (std::string_view(Name) == "annotation_argument_info")
					{
						M.insert(M.end(),
							{{"kind", "int () const"}, {"scalar_kind", "int () const"},
								{"int64_value", "int64 () const"}, {"uint64_value", "uint64 () const"},
								{"float_value", "double () const"}, {"bool_value", "bool () const"},
								{"value", "string () const"}});
					}

					else
					{
						M.push_back({"matches_name", "bool (string name) const"});
					}

				}
				else if (std::string_view(Name) == "comment_info")
				{
					M.insert(M.end(),
						{{"raw_text", "string () const"}, {"text", "string () const"},
							{"kind", "int () const"}, {"activity", "int () const"},
							{"association", "int () const"}});
				}

				else
				{
					M.insert(M.end(),
						{{"documentation", "string () const"}, {"detail", "string () const"},
							{"provider", "string () const"}, {"target_mask", "int () const"},
							{"case_sensitive", "bool () const"}, {"argument_count", "int () const"},
							{"argument", "annotation_definition_info (int index) const"},
							{"arguments", "array<annotation_definition_info> () const"}});
				}
			}
			return Table;
		}();
		static const std::vector<FSourceMetadataMethod> Empty;
		const auto Found = Methods.find(Owner);
		return Found == Methods.end() ? Empty : Found->second;
	}
}
