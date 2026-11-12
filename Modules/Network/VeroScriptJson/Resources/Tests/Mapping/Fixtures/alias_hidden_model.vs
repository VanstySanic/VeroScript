// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// A private alias in the bridge's dependency must not hide the exported alias
// imported directly by its consumer. Exported names are unique across modules.
namespace AliasModel
{
export struct HiddenBase { bool wrong=true; };
using AliasChain=HiddenBase;
}
