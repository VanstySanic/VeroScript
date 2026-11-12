// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains

import com.vanstysanic.veroscript.jetbrains.debug.DebugSourceCoordinates
import org.junit.Assert.*
import org.junit.Test

class DebugSourceCoordinatesTest {
	@Test fun dapUtf16ColumnsAreNotConvertedFromUtf8Again() {
		val text = "a中文😀x"
		assertEquals(0, DebugSourceCoordinates.zeroBasedColumn(text, 1))
		assertEquals(1, DebugSourceCoordinates.zeroBasedColumn(text, 2))
		assertEquals(2, DebugSourceCoordinates.zeroBasedColumn(text, 3))
		assertEquals(3, DebugSourceCoordinates.zeroBasedColumn(text, 4))
		assertEquals(5, DebugSourceCoordinates.zeroBasedColumn(text, 6))
		assertEquals(6, DebugSourceCoordinates.zeroBasedColumn(text, 7))
	}

	@Test fun malformedColumnsStayWithinTheLineAndDoNotSplitSurrogates() {
		val text = "a中文😀x"
		assertEquals(0, DebugSourceCoordinates.zeroBasedColumn(text, Int.MIN_VALUE))
		assertEquals(0, DebugSourceCoordinates.zeroBasedColumn(text, 0))
		assertEquals(3, DebugSourceCoordinates.zeroBasedColumn(text, 5))
		assertEquals(6, DebugSourceCoordinates.zeroBasedColumn(text, Int.MAX_VALUE))
		assertEquals(0, DebugSourceCoordinates.zeroBasedColumn("", 1))
	}
}
