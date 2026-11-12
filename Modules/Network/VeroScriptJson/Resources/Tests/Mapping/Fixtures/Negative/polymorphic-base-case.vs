// expect: Duplicate polymorphic case
// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

class Base {};
@JsonPolymorphic(base="Base",variants=(a="Base"))
class Table {};
int main(){return 0;}
