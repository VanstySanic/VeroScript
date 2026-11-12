// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

[assembly: SuppressMessage("Naming", "CA1711:Identifiers should not have incorrect suffix",
	Justification = "CSharpAttribute is an immutable AST description, not a CLR attribute type.",
	Scope = "type", Target = "~T:VeroScript.CSharpFrontend.CSharpAttribute")]
[assembly: SuppressMessage("Naming", "CA1720:Identifier contains type name",
	Justification = "Pointer is the exact C# type category represented by this stable AST enum.",
	Scope = "member", Target = "~F:VeroScript.CSharpFrontend.CSharpTypeKind.Pointer")]
