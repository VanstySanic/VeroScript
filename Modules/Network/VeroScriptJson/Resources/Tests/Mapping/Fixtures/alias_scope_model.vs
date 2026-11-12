// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace PolymorphicAliasScope
{
class ForeignBase { public: int foreign=91; };
class ForeignCase : public ForeignBase {};
using BaseAlias=ForeignBase;
using CaseAlias=ForeignCase;
export struct Anchor { int value=0; };
}
