// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.gui.jetbrains

import com.intellij.DynamicBundle
import com.intellij.openapi.util.IconLoader
import org.jetbrains.annotations.PropertyKey

object GuiBundle : DynamicBundle("Localization.messages.VeroScriptGuiBundle") {
	fun message(@PropertyKey(resourceBundle = "Localization.messages.VeroScriptGuiBundle") key: String, vararg args: Any) =
		getMessage(key, *args)
}

object GuiIcons {
	val file = IconLoader.getIcon("/Icons/gui.svg", GuiIcons::class.java)
}
