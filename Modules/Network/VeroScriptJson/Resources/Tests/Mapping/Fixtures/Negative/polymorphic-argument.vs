// expect: Unknown JsonPolymorphic argument
// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

class Base {};
class First : public Base {};
@JsonPolymorphic(base="Base",flatten="yes",variants=(a="First"))
class Table {};
int main(){return 0;}
