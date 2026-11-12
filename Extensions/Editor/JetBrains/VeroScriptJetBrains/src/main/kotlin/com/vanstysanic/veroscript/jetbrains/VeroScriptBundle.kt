// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains

import com.intellij.DynamicBundle
import com.intellij.openapi.util.IconLoader
import org.jetbrains.annotations.PropertyKey

object VeroScriptBundle : DynamicBundle("Localization.messages.VeroScriptBundle") {
	fun message(@PropertyKey(resourceBundle = "Localization.messages.VeroScriptBundle") key: String, vararg params: Any): String =
		getMessage(key, *params)
}

object VeroScriptIcons {
	val file = IconLoader.getIcon("/Icons/veroscript.svg", VeroScriptIcons::class.java)
}
