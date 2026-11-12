// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.debug

import com.intellij.openapi.editor.Document
import com.intellij.openapi.project.Project
import com.intellij.ui.SimpleTextAttributes
import com.intellij.xdebugger.XExpression
import com.intellij.xdebugger.XSourcePosition
import com.intellij.xdebugger.evaluation.XDebuggerEvaluator
import com.intellij.xdebugger.frame.*
import com.intellij.xdebugger.XDebuggerUtil
import com.vanstysanic.veroscript.jetbrains.VeroScriptBundle.message
import org.eclipse.lsp4j.debug.EvaluateArguments
import org.eclipse.lsp4j.debug.ScopesArguments
import org.eclipse.lsp4j.debug.SetVariableArguments
import org.eclipse.lsp4j.debug.StackTraceArguments
import org.eclipse.lsp4j.debug.StackTraceResponse
import org.eclipse.lsp4j.debug.Variable
import org.eclipse.lsp4j.debug.VariablesArguments
import org.eclipse.lsp4j.debug.StackFrame as DapFrame
import java.util.concurrent.TimeUnit

class VeroScriptSuspendContext(process: VeroScriptDebugProcess, generation: Long, thread: Int, frames: StackTraceResponse) : XSuspendContext() {
	private val stack = VeroScriptExecutionStack(process, generation, thread, frames)
	override fun getActiveExecutionStack(): XExecutionStack = stack
	override fun getExecutionStacks(): Array<XExecutionStack> = arrayOf(stack)
}

private class VeroScriptExecutionStack(
	private val process: VeroScriptDebugProcess, private val generation: Long, private val thread: Int,
	private val initial: StackTraceResponse
) : XExecutionStack("main") {
	private val firstFrames = initial.stackFrames.map { VeroScriptStackFrame(process, generation, it) }
	private val top: XStackFrame? = firstFrames.firstOrNull()
	override fun getTopFrame() = top
	override fun computeStackFrames(firstFrameIndex: Int, container: XStackFrameContainer) {
		if (!process.isCurrent(generation) || container.isObsolete) return
		if (firstFrameIndex == 0) {
			container.addStackFrames(firstFrames, initial.totalFrames?.let { firstFrames.size >= it } ?: (firstFrames.size < 100))
			return
		}
		process.server.stackTrace(StackTraceArguments().apply {
			threadId = thread
			startFrame = firstFrameIndex
			levels = 100
		}).orTimeout(10, TimeUnit.SECONDS).thenCompose { process.prepareSources(it, generation) }.whenComplete { response, error -> process.ui {
			if (container.isObsolete || !process.isCurrent(generation)) return@ui
			if (error != null) container.errorOccurred(error.message.orEmpty())
			else
			{
				val frames = response.stackFrames.map { VeroScriptStackFrame(process, generation, it) }
				container.addStackFrames(frames, response.totalFrames?.let { firstFrameIndex + frames.size >= it } ?: (frames.size < 100))
			}
		} }
	}
}

private class VeroScriptStackFrame(
	private val process: VeroScriptDebugProcess, private val generation: Long, private val frame: DapFrame
) : XStackFrame() {
	override fun getEqualityObject(): Any = Pair(generation, frame.id)
	override fun getSourcePosition(): XSourcePosition? {
		val file = process.sources.find(frame.source) ?: return null
		return com.intellij.openapi.application.ReadAction.computeBlocking<XSourcePosition?, RuntimeException> {
			val line = (frame.line - 1).coerceAtLeast(0)
			val manager = com.intellij.openapi.fileEditor.FileDocumentManager.getInstance()
			val document = if (file is VeroScriptDebugSourceFile) manager.getDocument(file)
			else manager.getCachedDocument(file)
			val column = if (document != null && line < document.lineCount) {
				val text = document.immutableCharSequence.subSequence(document.getLineStartOffset(line), document.getLineEndOffset(line))
				DebugSourceCoordinates.zeroBasedColumn(text, frame.column)
			}
			else (frame.column.toLong() - 1).coerceAtLeast(0).toInt()
			val debugger = XDebuggerUtil.getInstance()
			if (file is VeroScriptDebugSourceFile && document != null && line < document.lineCount) {
				// SDK line/column positions intentionally report offset -1 for LightVirtualFile.
				debugger.createPositionByOffset(file, document.getLineStartOffset(line) + column)
			}
			else debugger.createPosition(file, line, column)
		}
	}
	override fun customizePresentation(component: com.intellij.ui.ColoredTextContainer) {
		component.append(frame.name, SimpleTextAttributes.REGULAR_ATTRIBUTES)
		component.append(" (${frame.source?.name.orEmpty()}:${frame.line})", SimpleTextAttributes.GRAYED_ATTRIBUTES)
	}
	override fun getEvaluator(): XDebuggerEvaluator? = if (process.canEvaluate) VeroScriptEvaluator(process, generation, frame.id)
	else null
	override fun computeChildren(node: XCompositeNode) {
		if (!process.isCurrent(generation) || node.isObsolete) return
		process.server.scopes(ScopesArguments().apply { frameId = frame.id }).orTimeout(10, TimeUnit.SECONDS)
			.whenComplete { response, error -> process.ui {
				if (node.isObsolete || !process.isCurrent(generation)) return@ui
				if (error != null) node.setErrorMessage(error.message.orEmpty())
				else
				{
					val children = XValueChildrenList()
					response.scopes.forEach { scope ->
						children.add(VeroScriptValue(process, generation, Variable().apply {
							name = scope.name
							value = ""
							variablesReference = scope.variablesReference
							namedVariables = scope.namedVariables
							indexedVariables = scope.indexedVariables
						}, 0))
					}
					node.addChildren(children, true)
				}
			} }
	}
}

private class VeroScriptEvaluator(
	private val process: VeroScriptDebugProcess, private val generation: Long, private val frame: Int
) : XDebuggerEvaluator() {
	override fun getExpressionInfoAtOffset(project: Project, document: Document, offset: Int, allowMethodCalls: Boolean): com.intellij.xdebugger.evaluation.ExpressionInfo? {
		val info = if (process.isCurrent(generation)) DebugHoverExpressions.at(document.immutableCharSequence, offset)
		else null
		process.hoverHints.requested(document, info?.textRange, generation, !allowMethodCalls)
		return info
	}

	override fun evaluate(expression: String, callback: XEvaluationCallback, sourcePosition: XSourcePosition?) {
		if (!process.isCurrent(generation)) {
			callback.errorOccurred(message("debug.notPaused"))
			return
		}
		process.server.evaluate(EvaluateArguments().apply {
			this.expression = expression
			frameId = frame
			context = "watch"
		})
			.orTimeout(10, TimeUnit.SECONDS).whenComplete { response, error -> process.ui {
				if (!process.isCurrent(generation)) {
					callback.errorOccurred(message("debug.notPaused"))
					return@ui
				}
				if (error != null) callback.errorOccurred(error.message.orEmpty())
				else callback.evaluated(VeroScriptValue(process, generation, Variable().apply {
					name = expression
					value = response.result
					type = response.type
					variablesReference = response.variablesReference
					namedVariables = response.namedVariables
					indexedVariables = response.indexedVariables
				}, 0))
			} }
	}
}

/** Values stay as protocol strings, including int64/uint64 and non-finite numeric displays. */
private class VeroScriptValue(
	private val process: VeroScriptDebugProcess, private val generation: Long,
	private val variable: Variable, private val parentReference: Int
) : XNamedValue(variable.name) {
	override fun computePresentation(node: XValueNode, place: XValuePlace) {
		node.setPresentation(null, variable.type, variable.value, variable.variablesReference != 0)
	}
	override fun getEvaluationExpression(): String? = variable.evaluateName
	override fun computeChildren(node: XCompositeNode) {
		val total = if (variable.namedVariables != null || variable.indexedVariables != null)
			(variable.namedVariables ?: 0).toLong() + (variable.indexedVariables ?: 0).toLong()
			else null
		loadChildren(node, 0, total)
	}

	private fun loadChildren(node: XCompositeNode, offset: Int, total: Long?) {
		if (!process.isCurrent(generation) || node.isObsolete || variable.variablesReference == 0) return
		val reference = variable.variablesReference
		if (total == 0L) {
			node.addChildren(XValueChildrenList.EMPTY, true)
			return
		}
		process.server.variables(VariablesArguments().apply {
			variablesReference = reference
			// Native adapters without child counts return a complete snapshot, not pages.
			// Do not infer paging from a response that happens to contain exactly 100 items.
			if (total != null) {
				start = offset
				count = 100
			}
		})
			.orTimeout(10, TimeUnit.SECONDS).whenComplete { response, error -> process.ui {
				if (node.isObsolete || !process.isCurrent(generation) || variable.variablesReference != reference) return@ui
				if (error != null) node.setErrorMessage(error.message.orEmpty())
				else if (total == null) showSnapshot(node, response.variables, reference, 0)
				else
				{
					val children = XValueChildrenList()
					response.variables.forEach { children.add(VeroScriptValue(process, generation, it, reference)) }
					val next = offset.toLong() + response.variables.size
					val more = response.variables.size == 100 && next < total && next <= Int.MAX_VALUE
					node.addChildren(children, !more)
					if (more) node.tooManyChildren((total - next).coerceAtMost(Int.MAX_VALUE.toLong()).toInt(),
						Runnable { loadChildren(node, next.toInt(), total) })
				}
			} }
	}

	/** Present a complete protocol snapshot in bounded UI batches without re-querying it. */
	private fun showSnapshot(node: XCompositeNode, variables: Array<Variable>, reference: Int, offset: Int) {
		if (node.isObsolete || !process.isCurrent(generation) || variable.variablesReference != reference) return
		val end = (offset.toLong() + 100).coerceAtMost(variables.size.toLong()).toInt()
		val children = XValueChildrenList(end - offset)
		for (index in offset until end) children.add(VeroScriptValue(process, generation, variables[index], reference))
		node.addChildren(children, end == variables.size)
		if (end < variables.size) node.tooManyChildren(variables.size - end,
			Runnable { process.ui { showSnapshot(node, variables, reference, end) } })
	}

	override fun getModifier(): XValueModifier? {
		if (parentReference == 0 || !process.canSetVariable ||
			variable.presentationHint?.attributes?.contains("readOnly") == true) return null
		return object : XValueModifier() {
			override fun getInitialValueEditorText() = variable.value
			override fun setValue(expression: XExpression, callback: XModificationCallback) {
				if (!process.isCurrent(generation)) {
					callback.errorOccurred(message("debug.notPaused"))
					return
				}
				process.server.setVariable(SetVariableArguments().apply {
					variablesReference = parentReference
					name = variable.name
					value = expression.expression
				}).orTimeout(10, TimeUnit.SECONDS).whenComplete { response, error -> process.ui {
					if (!process.isCurrent(generation)) {
						callback.errorOccurred(message("debug.notPaused"))
						return@ui
					}
					if (error != null) callback.errorOccurred(error.message.orEmpty())
					else
					{
						variable.value = response.value
						variable.type = response.type
						variable.variablesReference = response.variablesReference ?: 0
						variable.namedVariables = response.namedVariables
						variable.indexedVariables = response.indexedVariables
						callback.valueModified()
					}
				} }
			}
		}
	}
}
