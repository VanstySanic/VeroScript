// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains

import com.intellij.openapi.Disposable
import com.intellij.openapi.actionSystem.ActionManager
import com.intellij.openapi.actionSystem.DefaultActionGroup
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.editor.Editor
import com.intellij.openapi.editor.EditorFactory
import com.intellij.openapi.editor.LogicalPosition
import com.intellij.openapi.editor.event.EditorMouseEvent
import com.intellij.openapi.editor.event.EditorMouseEventArea
import com.intellij.openapi.project.Project
import com.intellij.openapi.project.ProjectManager
import com.intellij.openapi.ui.popup.JBPopup
import com.intellij.openapi.ui.popup.JBPopupListener
import com.intellij.openapi.ui.popup.LightweightWindowEvent
import com.intellij.openapi.util.Disposer
import com.intellij.openapi.util.TextRange
import com.intellij.testFramework.TestApplicationManager
import com.vanstysanic.veroscript.jetbrains.debug.DebugHoverHints
import org.junit.Assert.*
import org.junit.BeforeClass
import org.junit.Test
import java.awt.Point
import java.awt.event.AWTEventListener
import java.awt.event.MouseEvent
import java.lang.reflect.Proxy
import javax.swing.JComponent
import javax.swing.JPanel

class DebugHoverHintsTest {
	@Test fun movingIntoThePopupKeepsItAndLeavingForBlankSpaceDismissesIt() {
		Fixture().use { f ->
			f.request(); f.show(); f.move(20, 5); settle()
			assertEquals(0, f.hint.cancels)
			f.move(5, 5, f.hint.content, MouseEvent.MOUSE_PRESSED); settle()
			assertEquals(0, f.hint.cancels)
			f.move(320, 170); settle()
			assertEquals(1, f.hint.cancels)
			assertTrue(f.registered.isEmpty())
		}
		Fixture().use { f ->
			f.request(); f.show(); f.move(50, 5); settle()
			assertEquals(1, f.hint.cancels) // exclusive EOF; clamping must not retain it
		}
	}

	@Test fun continuousMovementOutsideCannotKeepPostponingDismissal() {
		Fixture().use { f ->
			f.request(); f.show(); f.move(20, 5); settle()
			repeat(20) { f.move(320 + it, 170); Thread.sleep(25) }
			edt {}
			assertEquals(1, f.hint.cancels)
			assertTrue(f.registered.isEmpty())
		}
	}

	@Test fun explicitEvaluationOtherPopupsAndPromotionKeepIndependentLifetimes() {
		Fixture().use { f ->
			f.request(automatic = false); f.show(); f.move(320, 170); settle()
			assertEquals(0, f.hint.cancels)
			assertTrue(f.registered.isEmpty())
		}
		Fixture().use { f ->
			f.request(); f.show(tooltip = false); f.move(320, 170); settle()
			assertEquals(0, f.hint.cancels)
		}
		Fixture().use { f ->
			f.request(); f.show(); f.move(20, 5); settle()
			f.move(5, 5, f.hint.content, MouseEvent.MOUSE_PRESSED)
			f.request(automatic = false)
			f.move(320, 170); settle()
			assertEquals(0, f.hint.cancels)
			assertTrue(f.registered.isEmpty())
		}
	}

	@Test fun delayedPresentationNativeClosureResumeAndDisposalReleaseTracking() {
		Fixture().use { f ->
			f.request(); f.move(320, 170); settle()
			f.show(); settle()
			assertEquals(1, f.hint.cancels)
			assertTrue(f.registered.isEmpty())
		}
		Fixture().use { f ->
			f.request(); f.show(); f.move(20, 5); settle()
			edt { f.hint.popup.cancel() }
			assertTrue(f.registered.isEmpty())
		}
		Fixture().use { f ->
			f.request(); f.show(); f.move(20, 5); settle()
			edt { f.manager.clear() }
			assertEquals(1, f.hint.cancels)
			assertTrue(f.registered.isEmpty())
		}
		Fixture().use { f ->
			f.request(); f.show(); f.move(20, 5); settle()
			edt { Disposer.dispose(f.lifetime) }
			assertEquals(1, f.hint.cancels)
			assertTrue(f.registered.isEmpty())
		}
	}

	@Test fun splitEditorHoverUsesAllPanesInThePopupWindow() {
		Fixture().use { f ->
			val other = f.split()
			f.request(); f.show(); f.move(20, 5, other); settle()
			assertEquals(0, f.hint.cancels)
			f.move(320, 170, other); settle()
			assertEquals(1, f.hint.cancels)
			assertTrue(f.registered.isEmpty())
		}
	}

	@Test fun anotherProjectsTooltipForTheSameDocumentIsNeverAdopted() {
		Fixture().use { f ->
			val other = f.foreignEditor()
			f.request()
			f.show(owner = other)
			val foreignHint = f.hint
			settle()
			assertEquals(0, foreignHint.cancels)
			f.show(); f.move(320, 170); settle()
			assertEquals(1, f.hint.cancels)
			assertEquals(0, foreignHint.cancels)
			assertTrue(f.registered.isEmpty())
		}
	}

	@Test fun queuedFrameResetCannotReuseOldHoverOrCloseTheNewOne() {
		Fixture().use { f ->
			f.request(); f.show(); f.move(20, 5); settle()
			val previous = f.hint
			edt {
				// Queue the background reset while the EDT is occupied, then process a
				// new frame's identical expression before that reset reaches the EDT.
				val reset = Thread { f.manager.clear() }
				reset.start(); reset.join()
				f.request()
				assertEquals(1, previous.cancels)
				f.show(); f.move(20, 5)
			}
			settle()
			assertEquals(0, f.hint.cancels)
			f.move(320, 170); settle()
			assertEquals(1, f.hint.cancels)
			assertTrue(f.registered.isEmpty())
		}
	}

	@Test fun rootOwnedRiderPopupsAreDiscoverableWithoutOpeningAWindow() = edt {
		Fixture().use { f ->
			val root = javax.swing.JRootPane()
			root.contentPane.add(f.content)
			val popup = com.intellij.openapi.ui.popup.JBPopupFactory.getInstance()
				.createComponentPopupBuilder(JPanel(), null).createPopup()
			try {
				// Match Rider's BackendBePopupFactory ownership, which differs from plain
				// PopupFactoryImpl.show(RelativePoint(editor.contentComponent, ...)).
				val field = com.intellij.ui.popup.AbstractPopup::class.java.getDeclaredField("myRequestorComponent")
					.apply { isAccessible = true }
				field.set(popup, root)
				assertFalse(com.intellij.openapi.ui.popup.JBPopupFactory.getInstance().getChildPopups(f.content).contains(popup))
				assertTrue(DebugHoverHints.nativePopups(f.editor).contains(popup))
			}
			finally
			{
				popup.cancel()
			}
		}
	}

	@Test fun sdkNativeTooltipMatchesAndExplicitPopupDoesNot() = edt {
		// Exercise the actual SDK builder, rather than synthetic editor hint events.
		// A disposed editor stops it before popup.show(): no desktop window opens.
		Fixture(disposedEditor = true).use { f ->
			val type = Class.forName("com.intellij.xdebugger.impl.evaluate.quick.common.XDebuggerTooltipPopup")
			val constructor = type.getDeclaredConstructor(Editor::class.java, Point::class.java).apply { isAccessible = true }
			val method = type.getDeclaredMethod("show", JComponent::class.java, EditorMouseEvent::class.java).apply { isAccessible = true }
			val native = constructor.newInstance(f.editor, Point(0, 20))
			val event = EditorMouseEvent(f.editor, MouseEvent(f.content, MouseEvent.MOUSE_MOVED, 0, 0, 20, 5, 0, false),
				EditorMouseEventArea.EDITING_AREA, 2, LogicalPosition(0, 2), com.intellij.openapi.editor.VisualPosition(0, 2),
				true, null, null, null)
			val value = JPanel()
			method.invoke(native, value, event)
			assertTrue(DebugHoverHints.isValueTooltip(value.parent))
			val explicit = constructor.newInstance(f.editor, Point(0, 20))
			val explicitValue = JPanel()
			method.invoke(explicit, explicitValue, null)
			assertFalse(DebugHoverHints.isValueTooltip(explicitValue.parent))
		}
	}

	private class Fixture(private val disposedEditor: Boolean = false) : AutoCloseable {
		val registered = mutableListOf<AWTEventListener>()
		val lifetime = Disposer.newDisposable()
		val project = ProjectManager.getInstance().defaultProject
		private val document = EditorFactory.getInstance().createDocument("value")
		val content = TestEditorContent().apply { setSize(400, 200) }
		private val extraEditors = mutableListOf<Editor>()
		val editor = createEditor(content)
		private fun createEditor(component: JPanel, owner: Project = project): Editor = Proxy.newProxyInstance(Editor::class.java.classLoader, arrayOf(Editor::class.java)) { instance, method, arguments ->
			when (method.name) {
				"getDocument" -> document
				"getProject" -> owner
				"getContentComponent" -> component
				"isDisposed" -> disposedEditor
				"xyToLogicalPosition" -> (arguments!![0] as Point).let { LogicalPosition(it.y / 20, it.x / 10) }
				"logicalPositionToOffset" -> (arguments!![0] as LogicalPosition).column.coerceAtMost(document.textLength)
				"offsetToLogicalPosition" -> LogicalPosition(0, arguments!![0] as Int)
				"hashCode" -> System.identityHashCode(instance)
				"equals" -> instance === arguments?.get(0)
				"toString" -> "hover editor"
				else -> null
			}
		} as Editor
		var hint = TestPopup(true)
		private val shown = mutableListOf<Pair<Editor, JBPopup>>()
		val manager = DebugHoverHints(project, { arrayOf(editor, *extraEditors.toTypedArray()) }, { current ->
			shown.filter { it.first === current }.map { it.second }
		}, { listener ->
			registered.add(listener)
			Disposable { registered.remove(listener) }
		}) { it == 1L }.also { Disposer.register(lifetime, it) }

		fun split(): JPanel {
			val other = JPanel().apply { setSize(400, 200) }
			edt {
				val root = javax.swing.JRootPane()
				root.contentPane = JPanel().apply { add(content); add(other) }
				content.acceptsPointer = false
				extraEditors.add(createEditor(other))
			}
			return other
		}

		fun request(automatic: Boolean = true) = edt { manager.requested(document, TextRange(0, 5), 1, automatic) }
		fun foreignEditor(): Editor {
			val other = Proxy.newProxyInstance(Project::class.java.classLoader, arrayOf(Project::class.java)) { _, _, _ -> null } as Project
			return createEditor(JPanel(), other).also { edt { extraEditors.add(it) } }
		}
		fun show(tooltip: Boolean = true, owner: Editor = editor) = edt { hint = TestPopup(tooltip); shown.add(owner to hint.popup) }
		fun move(x: Int, y: Int, target: JComponent = content, id: Int = MouseEvent.MOUSE_MOVED) = edt {
			val event = MouseEvent(target, id, System.currentTimeMillis(), 0, x, y, 0, false)
			registered.toList().forEach { it.eventDispatched(event) }
		}
		override fun close() = edt { Disposer.dispose(lifetime) }
	}

	private class TestEditorContent : JPanel() {
		var acceptsPointer = true
		override fun contains(x: Int, y: Int) = acceptsPointer && super.contains(x, y)
	}

	private class TestPopup(tooltip: Boolean) {
		val content = JPanel().apply {
			setSize(80, 30)
			if (tooltip) add(ActionManager.getInstance().createActionToolbar("XDebuggerTooltip", DefaultActionGroup(), true).component)
		}
		var cancels = 0
		private val listeners = mutableListOf<JBPopupListener>()
		val popup = Proxy.newProxyInstance(JBPopup::class.java.classLoader, arrayOf(JBPopup::class.java)) { instance, method, args ->
			when (method.name) {
				"getContent" -> content
				"isVisible" -> cancels == 0
				"isDisposed" -> cancels != 0
				"cancel" -> { cancels++; listeners.toList().forEach { it.onClosed(LightweightWindowEvent(instance as JBPopup, false)) }; null }
				"addListener" -> { listeners.add(args!![0] as JBPopupListener); null }
				"removeListener" -> { listeners.remove(args!![0] as JBPopupListener); null }
				"hashCode" -> System.identityHashCode(instance)
				"equals" -> instance === args?.get(0)
				"toString" -> "hover popup"
				else -> null
			}
		} as JBPopup
	}

	companion object {
		@JvmStatic @BeforeClass fun initializeApplication() { TestApplicationManager.getInstance() }
		private fun edt(action: () -> Unit) = ApplicationManager.getApplication().invokeAndWait(action)
		private fun settle() { edt {}; Thread.sleep(350); edt {} }
	}
}
