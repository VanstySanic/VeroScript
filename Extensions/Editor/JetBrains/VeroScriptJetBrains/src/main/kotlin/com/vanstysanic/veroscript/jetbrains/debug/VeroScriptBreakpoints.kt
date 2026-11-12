// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.debug

import com.intellij.openapi.editor.EditorFactory
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.xdebugger.XSourcePosition
import com.intellij.xdebugger.breakpoints.XBreakpointProperties
import com.intellij.xdebugger.breakpoints.XBreakpointType
import com.intellij.xdebugger.breakpoints.XLineBreakpointType
import com.intellij.xdebugger.evaluation.EvaluationMode
import com.intellij.xdebugger.evaluation.XDebuggerEditorsProvider
import com.vanstysanic.veroscript.jetbrains.VeroScriptBundle.message
import com.vanstysanic.veroscript.jetbrains.language.VeroScriptFileType
import java.util.EnumSet

class VeroScriptBreakpointProperties : XBreakpointProperties<VeroScriptBreakpointProperties>() {
	override fun getState() = this
	override fun loadState(state: VeroScriptBreakpointProperties) {}
}

class VeroScriptLineBreakpointType : XLineBreakpointType<VeroScriptBreakpointProperties>("veroscript-line", message("debug.breakpoints")) {
	override fun canPutAt(file: VirtualFile, line: Int, project: Project) =
		file.fileType == VeroScriptFileType && file !is VeroScriptDebugSourceFile
	override fun createBreakpointProperties(file: VirtualFile, line: Int) = VeroScriptBreakpointProperties()
	override fun getVisibleStandardPanels(): EnumSet<XBreakpointType.StandardPanels> = EnumSet.noneOf(XBreakpointType.StandardPanels::class.java)
	override fun getEditorsProvider(breakpoint: com.intellij.xdebugger.breakpoints.XLineBreakpoint<VeroScriptBreakpointProperties>, project: Project) =
		VeroScriptDebuggerEditorsProvider()
}

class VeroScriptDebuggerEditorsProvider : XDebuggerEditorsProvider() {
	override fun getFileType() = VeroScriptFileType
	override fun createDocument(project: Project, expression: com.intellij.xdebugger.XExpression,
		sourcePosition: XSourcePosition?, mode: EvaluationMode, debugSessionId: String?) =
		EditorFactory.getInstance().createDocument(expression.expression)
}
