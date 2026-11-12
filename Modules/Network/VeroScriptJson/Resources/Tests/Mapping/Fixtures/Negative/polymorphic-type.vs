// expect: Invalid polymorphic type name
// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

class Base {};
@JsonPolymorphic(base="Base",variants=(a="First;"))
class Table {};
int main(){return 0;}
