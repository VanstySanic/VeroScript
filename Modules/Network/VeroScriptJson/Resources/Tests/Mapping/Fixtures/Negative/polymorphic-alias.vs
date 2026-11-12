// expect: Duplicate polymorphic case
// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

class Base {};
class First : public Base {};
using Alias = First;
@JsonPolymorphic(base="Base",variants=(a="First",b="Alias"))
class Table {};
int main(){return 0;}
