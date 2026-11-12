// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import "i18n.vs";
import "attribute.vs";

std::i18n::text current_label()
{
	return std::i18n::text("Dynamic label");
}

void main()
{
	std::i18n::arguments params = std::i18n::arguments();
	params.set("count", std::i18n::argument(18446744073709551615u64));
	params.set("name", std::i18n::argument("Vero"));

	std::i18n::configuration config = std::i18n::configuration();
	config.language = "en-US";
	array<string> fallbacks = {"en"};
	config.fallback_languages = fallbacks;
	std::i18n::context app = std::i18n::context(config);
	string rendered = app.format("{name}: {count}", params);
	assert(rendered == "Vero: 18446744073709551615");
	print("{}", rendered);

	std::i18n::snapshot previous = app.snapshot();
	app.set_language("zh-CN");
	assert(previous.version() != app.version());
	assert(previous.configuration().language == "en-US");

	std::attribute<std::i18n::text> label = std::i18n::text("Literal {braces}");
	assert(app.resolve(label.get()) == "Literal {braces}");
	label.bind(current_label);
	print("{}", app.resolve(label.get()));
}
