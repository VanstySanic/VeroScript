// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import "template_methods_decl.vs";

template<typename OwnerValue>
template<typename Result>
Result TemplateBox<OwnerValue>::echo(Result input)
{
	return input;
}
