// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import "compiler.vs";
using callback = fn(std::compiler::context, std::compiler::output)->void;

callback make_getter()
{
	return [](std::compiler::context ctx, std::compiler::output out) -> void
	{
		out.add_declaration(ctx.member_scope(), "public:int Read(){return value;}");
	};
}

export void register_extension(std::compiler::registration reg)
{
	std::compiler::annotation_definition annotation = std::compiler::annotation_definition();
	annotation.name = "@Getter";
	annotation.case_sensitive = false;
	annotation.documentation = "Generates a readable value accessor.";
	annotation.targets = array<std::compiler::target>{std::compiler::target::class_type};
	reg.register_annotation(annotation);

	std::compiler::subscription subscription = std::compiler::subscription();
	subscription.event = std::compiler::event::annotation;
	subscription.annotation_name = "@Getter";
	subscription.targets = array<std::compiler::target>{std::compiler::target::class_type};
	std::compiler::processor_definition definition = std::compiler::processor_definition();
	definition.id = "jetbrains.test.getter";
	definition.version = "0.1.1";
	definition.phases = array<std::compiler::phase>{std::compiler::phase::members};
	definition.operations = array<std::compiler::operation>{std::compiler::operation::add_declaration};
	definition.subscriptions = array<std::compiler::subscription>{subscription};
	reg.register_processor(definition, make_getter);
}
