// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import "compiler.vs";
using callback = fn(std::compiler::context, std::compiler::output) -> void;

callback make_builder()
{
	return [](std::compiler::context ctx, std::compiler::output out) -> void
	{
		if (ctx.phase() == std::compiler::phase::types)
		{
			out.add_declaration(ctx.top_scope(), "class CardBuilder {public:Card value=Card();" + "CardBuilder Text(string text){value.text=text;return this;}" + "Card Build(){return value;}};");
		}
		else
		{
			out.add_declaration(ctx.member_scope(), "public:static CardBuilder Create(){return CardBuilder();}");
		}
	};
}

export void register_extension(std::compiler::registration reg)
{
	std::compiler::annotation_definition a = std::compiler::annotation_definition();
	a.name = "@Builder";
	a.case_sensitive = false;
	a.documentation = "生成 Card 的构建器。";
	a.targets = array<std::compiler::target>{std::compiler::target::class_type};
	reg.register_annotation(a);

	std::compiler::subscription s = std::compiler::subscription();
	s.event = std::compiler::event::annotation;
	s.annotation_name = "@Builder";
	s.targets = array<std::compiler::target>{std::compiler::target::class_type};
	std::compiler::processor_definition d = std::compiler::processor_definition();
	d.id = "example.card_builder";
	d.version = "0.1.1";
	d.phases = array<std::compiler::phase>{std::compiler::phase::types, std::compiler::phase::members};
	d.operations = array<std::compiler::operation>{std::compiler::operation::add_declaration};
	d.subscriptions = array<std::compiler::subscription>{s};
	reg.register_processor(d, make_builder);
}
