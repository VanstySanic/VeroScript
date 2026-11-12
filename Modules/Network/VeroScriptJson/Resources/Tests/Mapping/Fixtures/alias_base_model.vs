// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace AliasModel
{
export template<typename T,int N> struct Base { T inherited; int count=N; };
export using NamedBase=Base<string,7>;
export using AliasChain=NamedBase;
}
