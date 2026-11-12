// expect: Duplicate polymorphic case
// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

class Base {};
class First : public Base {};
class Second : public Base {};
@JsonPolymorphic(base="Base",variants=(a=(type="First",tag="same"),b=(type="Second",tag="same")))
class Table {};
int main(){return 0;}
