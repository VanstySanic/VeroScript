// expect: requires distinct keys
// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

class Base {};
class First : public Base {};
@JsonPolymorphic(base="Base",discriminator="x",payload="x",variants=(a="First"))
class Table {};
int main(){return 0;}
