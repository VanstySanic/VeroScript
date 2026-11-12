// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.debug

import com.intellij.openapi.Disposable
import com.intellij.openapi.actionSystem.ActionToolbar
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.editor.Document
import com.intellij.openapi.editor.Editor
import com.intellij.openapi.editor.EditorFactory
import com.intellij.openapi.project.Project
import com.intellij.openapi.ui.popup.JBPopup
import com.intellij.openapi.ui.popup.JBPopupFactory
import com.intellij.openapi.ui.popup.JBPopupListener
import com.intellij.openapi.ui.popup.LightweightWindowEvent
import com.intellij.openapi.util.TextRange
import com.intellij.ui.awt.RelativePoint
import java.awt.AWTEvent
import java.awt.Component
import java.awt.Container
import java.awt.MouseInfo
import java.awt.Toolkit
import java.awt.event.AWTEventListener
import java.awt.event.MouseEvent
import java.util.concurrent.atomic.AtomicLong
import javax.swing.SwingUtilities
import javax.swing.Timer

/** Owns dismissal of passive native debugger tooltips, never explicit inspector windows. */
internal class DebugHoverHints(
	private val project: Project,
	private val editors: (Document) -> Array<Editor> = { EditorFactory.getInstance().getEditors(it) },
	private val popups: (Editor) -> List<JBPopup> = ::nativePopups,
	private val watchMouse: (AWTEventListener) -> Disposable = { listener ->
		val toolkit = Toolkit.getDefaultToolkit()
		toolkit.addAWTEventListener(listener, AWTEvent.MOUSE_EVENT_MASK or AWTEvent.MOUSE_MOTION_EVENT_MASK)
		Disposable { toolkit.removeAWTEventListener(listener) }
	},
	private val isCurrent: (Long) -> Boolean
) : Disposable {
	private data class Origin(val document: Document, val range: TextRange, val stamp: Long, val generation: Long, val epoch: Long)
	private val epoch = AtomicLong()
	@Volatile private var origin: Origin? = null
	@Volatile private var disposed = false
	private var active: Tracking? = null

	fun requested(document: Document, range: TextRange?, generation: Long, automatic: Boolean) {
		val source = if (!disposed && automatic && range != null && isCurrent(generation))
			Origin(document, range, document.modificationStamp, generation, epoch.get())
			else null
		origin = source
		onUi {
			if (disposed || origin !== source) return@onUi
			if (source != null && active?.source == source) return@onUi
			// An explicit request takes over the native UI; do not cancel its new popup.
			active?.finish(cancel = automatic)
			if (source != null) {
				val tracking = Tracking(source)
				active = tracking
				tracking.start()
			}
		}
	}

	fun clear() {
		epoch.incrementAndGet()
		origin = null
		onUi { active?.takeIf { it.source.epoch != epoch.get() }?.finish(cancel = true) }
	}

	override fun dispose() { disposed = true; clear() }

	private fun onUi(action: () -> Unit) {
		val application = ApplicationManager.getApplication()
		if (application.isDispatchThread) action()
		else application.invokeLater(action)
	}

	private inner class Tracking(val source: Origin) {
		private val candidates = editors(source.document).filter { !it.isDisposed && it.project === project }
		private val existing = candidates.flatMap(popups).toSet()
		private var popupEditors = emptyList<Editor>()
		private var popup: JBPopup? = null
		private var subscription: Disposable? = null
		private var closed = false
		private var lastEvent: MouseEvent? = null
		private val started = System.nanoTime()
		private val discovery = Timer(75) { discover() }
		private val exit = Timer(150) { if (!inside()) finish(cancel = true) }.apply { isRepeats = false }
		private val hidden = object : JBPopupListener {
			override fun onClosed(event: LightweightWindowEvent) { finish(cancel = false) }
		}
		private val mouse = AWTEventListener { event ->
			if (event is MouseEvent && (event.id == MouseEvent.MOUSE_MOVED || event.id == MouseEvent.MOUSE_DRAGGED ||
					event.id == MouseEvent.MOUSE_ENTERED || event.id == MouseEvent.MOUSE_EXITED || event.id == MouseEvent.MOUSE_PRESSED)) {
				lastEvent = event.takeUnless { it.id == MouseEvent.MOUSE_EXITED }
				if (popup != null) {
					if (event.id != MouseEvent.MOUSE_EXITED && inside()) exit.stop()
					else if (!exit.isRunning) exit.start()
				}
			}
		}

		fun start() {
			if (candidates.isEmpty()) { finish(cancel = false); return }
			subscription = watchMouse(mouse)
			discovery.start()
			discover()
		}

		private fun valid() = !disposed && source.epoch == epoch.get() && isCurrent(source.generation) &&
			source.stamp == source.document.modificationStamp && candidates.any { !it.isDisposed }

		private fun discover() {
			if (!valid()) { finish(cancel = true); return }
			// Native frontend presentation is asynchronous. Observe only newly shown tooltips,
			// for at most the evaluator's ten-second budget plus frontend delivery time.
			for (candidate in candidates.filterNot { it.isDisposed }) {
				val found = popups(candidate).firstOrNull {
					it !in existing && it.isVisible && !it.isDisposed && isValueTooltip(it.content)
				} ?: continue
				// The same document can be shown in several panes of this IDE window.
				val scope = popupScope(candidate)
				popupEditors = candidates.filter { !it.isDisposed && popupScope(it) === scope }
				popup = found
				discovery.stop()
				found.addListener(hidden)
				exit.start()
				return
			}
			if (System.nanoTime() - started > 11_000_000_000L) finish(cancel = false)
		}

		private fun insidePopup(point: RelativePoint): Boolean {
			val current = popup ?: return false
			if (current.isDisposed || !current.isVisible) return false
			val content = current.content
			if (point.component === content || SwingUtilities.isDescendingFrom(point.component, content))
				return content.contains(SwingUtilities.convertPoint(point.component, point.point, content))
			// Popup and editor normally live in separate windows; use their screen coordinates.
			if (!content.isShowing) return false
			return content.contains(point.getPoint(content))
		}

		private fun inside(): Boolean {
			if (closed || !valid()) return false
			return runCatching {
				val point = lastEvent?.let(::RelativePoint) ?: run {
					if (popupEditors.none { !it.isDisposed && it.contentComponent.isShowing }) return@runCatching false
					RelativePoint.fromScreen(MouseInfo.getPointerInfo()?.location ?: return@runCatching false)
				}
				if (insidePopup(point)) return@runCatching true
				popupEditors.any { currentEditor ->
					if (currentEditor.isDisposed) return@any false
					val content = currentEditor.contentComponent
					val inEditor = if (point.component === content) point.point
					else point.getPoint(content)
					if (!content.contains(inEditor)) return@any false
					val position = currentEditor.xyToLogicalPosition(inEditor)
					// xyToOffset clamps blank space to the line/file end; do not treat it as code.
					val offset = currentEditor.logicalPositionToOffset(position)
					currentEditor.offsetToLogicalPosition(offset) == position &&
						offset >= source.range.startOffset && offset < source.range.endOffset
				}
			}.getOrDefault(false)
		}

		fun finish(cancel: Boolean) {
			if (closed) return
			closed = true
			discovery.stop()
			exit.stop()
			subscription?.dispose()
			subscription = null
			val current = popup
			current?.removeListener(hidden)
			if (active === this) active = null
			if (cancel && current != null && !current.isDisposed) current.cancel()
		}
	}

	companion object {
		/** Rider's backend popup factory reparents value tooltips to the IDE root pane.
		 * Query that editor's root, then identify only new native debugger tooltips.
		 */
		internal fun nativePopups(editor: Editor): List<JBPopup> =
			JBPopupFactory.getInstance().getChildPopups(popupScope(editor))

		private fun popupScope(editor: Editor): Component =
			SwingUtilities.getRootPane(editor.contentComponent) ?: editor.contentComponent

		/** SDK 262's passive tooltip has this action toolbar; expanded inspectors do not.
		 * An SDK integration test builds the real native content to guard this distinction.
		 */
		internal fun isValueTooltip(component: Component): Boolean =
			(component is ActionToolbar && component.place == "XDebuggerTooltip") ||
				(component is Container && component.components.any(::isValueTooltip))
	}
}
