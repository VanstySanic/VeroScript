// expect: requires distinct keys
// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

class Base {};
@JsonPolymorphic(base="Base",variants=())
class Table {};
int main(){return 0;}
