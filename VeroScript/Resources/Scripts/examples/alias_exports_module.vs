// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Import-only module. Run alias_exports.vs or alias_reexports.vs.
// Both using and typedef use the Alias = Type spelling.
namespace ExportedAliases
{
	export using Count = int;
	export typedef Label = string;

	// This implementation alias remains private to this module.
	using InternalCount = int;

	export Count add(Count left, Count right)
	{
		InternalCount result = left + right;
		return result;
	}
}
