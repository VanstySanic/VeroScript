// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import "compiler.vs";

using callback = fn(std::compiler::context, std::compiler::output) -> void;

callback create()
{
	return [](std::compiler::context ctx, std::compiler::output out) -> void
	{
		assert(ctx.annotation().matches_name("@ANSWER"));
		auto input = ctx.read_dependency("../templates/value.txt");
		out.add_declaration(ctx.member_scope(), "public:static int value(){return " + input.content() + ";}");
	};
}

export void register_extension(std::compiler::registration reg)
{
	std::compiler::annotation_definition annotation = std::compiler::annotation_definition();
	annotation.name = "@Answer";
	annotation.case_sensitive = false;
	annotation.documentation = "Generate a static value() method from the installed template.";
	annotation.targets = array<std::compiler::target>{std::compiler::target::class_type};
	reg.register_annotation(annotation);

	std::compiler::subscription subscription = std::compiler::subscription();
	subscription.event = std::compiler::event::annotation;
	subscription.annotation_name = "@Answer";
	subscription.targets = array<std::compiler::target>{std::compiler::target::class_type};
	std::compiler::processor_definition processor = std::compiler::processor_definition();
	processor.id = "example.answer";
	processor.version = "0.1.1";
	processor.phases = array<std::compiler::phase>{std::compiler::phase::members};
	processor.operations = array<std::compiler::operation>{std::compiler::operation::add_declaration};
	processor.subscriptions = array<std::compiler::subscription>{subscription};
	reg.register_processor(processor, create);
}
