// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains

import com.vanstysanic.veroscript.jetbrains.debug.DebugHoverExpressions
import org.junit.Assert.*
import org.junit.Test

class DebugHoverExpressionsTest {
	@Test fun namesAndMemberPathsUseOnlyTheHoveredPrefix() {
		for ((marked, expected) in listOf(
			"int64 to|tal = 2147483648i64;" to "total",
			"print(ob|ject.field);" to "object",
			"print(object . fi|eld.child);" to "object . field",
			"object /* note */ . fi|eld" to "object /* note */ . field",
			"this.fi|eld" to "this.field",
			"ns::nested::va|lue" to "ns::nested::value",
			"变量.数|量" to "变量.数量",
			"\"😀\"; to|tal" to "total",
			"values[in|dex]" to "index"
		)) {
			val text = marked.replace("|", "")
			val info = DebugHoverExpressions.at(text, marked.indexOf('|'))
			assertNotNull(marked, info)
			assertEquals(marked, expected, info!!.textRange.substring(text))
		}
	}

	@Test fun commentsStringsCallsAndUnsupportedBasesAreNotAutomaticExpressions() {
		for (marked in listOf(
			"// to|tal", "/*\n to|tal\n */", "\"to|tal\"", "r\"line\n to|tal\"", "L\"to|tal\"",
			"fo|r (int i = 0; i < 3; ++i)", "pri|nt(total)", "object.me|thod /* comment */ ()",
			"get().fi|eld", "values[index].fi|eld", ".fi|eld", "object|.field", "total |", "123|4"
		)) assertNull(marked, DebugHoverExpressions.at(marked.replace("|", ""), marked.indexOf('|')))
		assertNull(DebugHoverExpressions.at("value", -1))
		assertNull(DebugHoverExpressions.at("value", 5))
		assertNull(DebugHoverExpressions.at("", 0))
	}
}
