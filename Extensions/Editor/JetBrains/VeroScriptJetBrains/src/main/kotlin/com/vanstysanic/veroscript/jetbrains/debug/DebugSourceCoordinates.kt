// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.debug

/** The DAP adapter already converts lexer byte columns to one-based UTF-16. */
internal object DebugSourceCoordinates {
	fun zeroBasedColumn(line: CharSequence, oneBasedUtf16Column: Int): Int {
		val offset = (oneBasedUtf16Column.toLong() - 1).coerceIn(0, line.length.toLong()).toInt()
		// A malformed host column must not split an editor surrogate pair.
		return if (offset > 0 && offset < line.length && Character.isLowSurrogate(line[offset]) &&
			Character.isHighSurrogate(line[offset - 1])) offset - 1
		else offset
	}
}
