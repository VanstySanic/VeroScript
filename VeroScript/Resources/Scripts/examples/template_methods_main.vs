// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import "template_methods_decl.vs";
import "template_methods_impl.vs";

void main()
{
	TemplateBox<int> box = TemplateBox<int>();
	print(box.echo<string>("cross-file"));
}
