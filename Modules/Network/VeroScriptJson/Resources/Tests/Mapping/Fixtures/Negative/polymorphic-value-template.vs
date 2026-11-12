// expect: JsonPolymorphic requires a concrete registry class
// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

class Base {};
class First : public Base {};
@JsonPolymorphic(base="Base",variants=(a="First"))
template<int N> class Table {};
int main() { return 0; }
