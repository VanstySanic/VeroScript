// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Included in FMappingProcessor. Tables compile to ordinary typed methods, without stored VM pointers.

static std::string PolyType(
	const FCompileProcessingContext& C, const FAnnotationDeclarationView& D, std::string Text, unsigned AliasDepth = 0)
{
	if (Text.empty() || Text.size() > 1024 || AliasDepth > 64)
	{
		throw std::runtime_error("A polymorphic case requires a closed type name");
	}
	int Depth = 0;
	for (const unsigned char Ch : Text)
	{
		if (Ch == '<')
		{
			++Depth;
		}
		else if (Ch == '>')
		{
			if (--Depth < 0)
			{
				throw std::runtime_error("Unbalanced polymorphic type arguments");
			}
		}
		else if (!((Ch >= 'a' && Ch <= 'z') || (Ch >= 'A' && Ch <= 'Z') || (Ch >= '0' && Ch <= '9') || Ch == '_' ||
					 Ch == '.' || Ch == ':' || Ch == ',' || Ch == ' ' || Ch >= 0x80))
		{
			throw std::runtime_error("Invalid polymorphic type name");
		}
	}
	if (Depth)
	{
		throw std::runtime_error("Unbalanced polymorphic type arguments");
	}
	Text = QualifyType(C, D, Text);
	if (const auto* Alias = FindTypeAlias(C, D, Text); Alias && !Alias->TypeName.empty())
	{
		// CanonicalTypeName may contain a module key and monomorphization IDs.
		// Generated source must use the alias's written target in its own scope.
		return PolyType(C, *Alias, Alias->TypeName, AliasDepth + 1);
	}
	return Text;
}
void Polymorphic(const FCompileProcessingContext& C, const FAnnotationDeclarationView& D, FCompileProcessingOutput& Out)
{
	try
	{
		if (D.Target != EAnnotationTarget::Class || !TemplateParameters(D).empty())
		{
			throw std::runtime_error("JsonPolymorphic requires a concrete registry class");
		}
		const auto& A = *Annotation(D.Metadata, "JsonPolymorphic");
		const auto Base = PolyType(C, D, Argument(A, "base"));
		std::string Discriminator = "kind", Payload = "value";
		const FAnnotationArgument* Variants = nullptr;
		std::set<std::string> Names;
		for (const auto& Item : A.Arguments)
		{
			if (!Names.insert(Item.Name).second)
			{
				throw std::runtime_error("Duplicate JsonPolymorphic argument");
			}
			if (Item.Name == "variants")
			{
				if (Item.Kind != EAnnotationArgumentKind::Group)
				{
					throw std::runtime_error("Polymorphic variants require a group");
				}
				Variants = &Item;
			}
			else if (Item.Name == "discriminator" || Item.Name == "payload")
			{
				if (Item.Kind != EAnnotationArgumentKind::Scalar || Item.ScalarKind != EAnnotationScalarKind::String)
				{
					throw std::runtime_error("Polymorphic envelope keys require strings");
				}
				(Item.Name == "discriminator" ? Discriminator : Payload) = Item.Value;
			}
			else if (Item.Name != "base")
			{
				throw std::runtime_error("Unknown JsonPolymorphic argument");
			}
		}
		if (!Variants || Variants->Children.empty() || Variants->Children.size() > 256 || Discriminator == Payload)
		{
			throw std::runtime_error("A polymorphic table requires distinct keys and 1..256 cases");
		}
		const auto DKey = Literal(Discriminator), PKey = Literal(Payload);
		std::vector<std::pair<std::string, std::string>> Cases;
		std::set<std::string> Tags, Types;
		for (const auto& Item : Variants->Children)
		{
			if (C.CheckCancelled)
			{
				C.CheckCancelled();
			}
			std::string Tag = Item.Name, Type;
			if (Item.Kind == EAnnotationArgumentKind::Scalar && Item.ScalarKind == EAnnotationScalarKind::String)
			{
				Type = Item.Value;
			}
			else if (Item.Kind == EAnnotationArgumentKind::Group)
			{
				std::set<std::string> Fields;
				for (const auto& Field : Item.Children)
				{
					if (!Fields.insert(Field.Name).second || Field.Kind != EAnnotationArgumentKind::Scalar ||
						Field.ScalarKind != EAnnotationScalarKind::String)
					{
						throw std::runtime_error("Invalid polymorphic case declaration");
					}
					if (Field.Name == "type")
					{
						Type = Field.Value;
					}
					else if (Field.Name == "tag")
					{
						Tag = Field.Value;
					}
					else
					{
						throw std::runtime_error("Unknown polymorphic case argument");
					}
				}
			}
			else
			{
				throw std::runtime_error("A polymorphic case requires a type string");
			}
			Type = PolyType(C, D, std::move(Type));
			if (Type == Base || !Types.insert(Type).second || !Tags.insert(Tag).second)
			{
				throw std::runtime_error("Duplicate polymorphic case, discriminator, or base used as its own case");
			}
			Literal(Tag);
			Cases.emplace_back(std::move(Tag), std::move(Type));
		}
		std::ostringstream S;
		auto Member = [&]
		{
			Emit(C, Out, S.str(), D.Id);
			S.str({});
			S.clear();
		};
		S << "public: static " << Base << " create(json::value input,json::mapping_context context){return nullptr;}";
		Member();
		for (std::size_t I = 0; I < Cases.size(); ++I)
		{
			S << "private: static " << Base << " json_poly_upcast_" << I << '(' << Cases[I].second
			  << " value){return value;}";
			Member();
		}
		S << "public: static bool encode(const " << Base
		  << "& input,json::value& output,json::mapping_context context){"
			 "if(input==nullptr){if(!context.visit())return false;output=json::from_null();return true;}"
			 "if(!context.visit())return false;string actual=context.runtime_type(input);int matches=0;";
		for (const auto& Item : Cases)
		{
			S << "if(actual==type<" << Item.second << ">().qualified_name)matches++;";
		}
		S << "if(matches!=1)return context.fail(\"Unknown or ambiguous polymorphic runtime type\");";
		for (const auto& Item : Cases)
		{
			S << "if(actual==type<" << Item.second << ">().qualified_name){" << Item.second << " typed=cast<"
			  << Item.second
			  << ">(input);"
				 "if(!context.push("
			  << PKey
			  << "))return false;json::value body=json::value();"
				 "bool ok=json::converter<"
			  << Item.second
			  << ">::encode(ref typed,ref body,context);"
				 "if(ok && body.kind()==0)ok=context.fail(\"A tagged polymorphic payload cannot be "
				 "null\");context.pop();if(!ok)return false;"
				 "json::value tag=json::value();if(!context.push("
			  << DKey << "))return false;ok=context.write_string(" << Literal(Item.first)
			  << ",ref tag);"
				 "context.pop();if(!ok)return false;json::builder result=json::object_builder();result.set("
			  << DKey << ",tag);result.set(" << PKey
			  << ",body);"
				 "output=result.finish();return true;}";
		}
		S << "return false;}";
		Member();
		S << "public: static bool decode(json::value input," << Base
		  << "& output,json::mapping_context context){"
			 "if(input.kind()==0){if(!context.visit())return false;output=nullptr;return "
			 "true;}if(!context.visit())return false;"
			 "if(input.kind()!=5)return context.fail(\"Expected a polymorphic JSON envelope\");"
			 "json::value keys=input.keys();for(int64 i=0;i<keys.size();i++){string key=keys.at(i).as_string();"
			 "if(key!="
		  << DKey << " && key!=" << PKey
		  << " && !context.ignore_unknown()){if(!context.push(key))return false;"
			 "context.fail(\"Unknown polymorphic envelope field\");context.pop();return false;}}"
			 "if(!context.push("
		  << DKey << "))return false;if(!input.contains(" << DKey
		  << ")){context.fail(\"Missing polymorphic discriminator\");"
			 "context.pop();return false;}string tag=\"\";bool tag_ok=context.read_string(input.get("
		  << DKey
		  << "),ref tag);"
			 "context.pop();if(!tag_ok)return false;";
		for (std::size_t I = 0; I < Cases.size(); ++I)
		{
			const auto& Item = Cases[I];
			S << "if(tag==" << Literal(Item.first) << "){if(!context.push(" << PKey
			  << "))return false;"
				 "if(!input.contains("
			  << PKey << ") || input.get(" << PKey
			  << ").kind()==0){context.fail(\"Missing or null polymorphic payload\");context.pop();return false;}"
			  << Item.second << " candidate=nullptr;bool ok=json::converter<" << Item.second << ">::decode(input.get("
			  << PKey
			  << "),ref candidate,context);"
				 "if(ok && context.runtime_type(candidate)!=type<"
			  << Item.second
			  << ">().qualified_name)ok=context.fail(\"Polymorphic factory returned a different runtime type\");"
				 "context.pop();if(!ok)return false;output="
			  << "json_poly_upcast_" << I << "(candidate);return true;}";
		}
		S << "if(!context.push(" << DKey
		  << "))return false;context.fail(\"Unknown polymorphic discriminator\");context.pop();return false;}";
		Member();
		const auto SchemaName = Literal("polymorphic:" + Identity(D));
		S << "public: static json::value schema(json::mapping_context context){json::value reference=json::value();"
			 "if(context.schema_reference("
		  << SchemaName
		  << ",ref reference))return reference;json::builder choices=json::array_builder();"
			 "choices.push(json::parse(\"{\\\"type\\\":\\\"null\\\"}\"));";
		for (const auto& Item : Cases)
		{
			S << "{json::builder both=json::array_builder();both.push(json::converter<" << Item.second
			  << ">::schema_nested(context));"
				 "both.push(json::parse(\"{\\\"not\\\":{\\\"type\\\":\\\"null\\\"}}\"));json::value "
				 "body=json::object();body.set(\"allOf\",both.finish());"
				 "json::value tag=json::object();tag.set(\"const\",json::from_string("
			  << Literal(Item.first)
			  << "));json::value fields=json::object();"
				 "fields.set("
			  << DKey << ",tag);fields.set(" << PKey
			  << ",body);json::builder required=json::array_builder();required.push(json::from_string(" << DKey
			  << "));required.push(json::from_string(" << PKey
			  << "));json::value branch=json::object();branch.set(\"type\",json::from_string(\"object\"));"
				 "branch.set(\"properties\",fields);branch.set(\"required\",required.finish());branch.set("
				 "\"additionalProperties\",json::from_bool(context.schema_input() && context.ignore_unknown()));"
				 "choices.push(branch);} ";
		}
		S << "json::value definition=json::object();definition.set(\"oneOf\",choices.finish());context.schema_define("
		  << SchemaName << ",definition);return reference;}";
		Member();
	}
	catch (const std::exception& Exception)
	{
		Error(C, Out, Exception.what());
	}
}
