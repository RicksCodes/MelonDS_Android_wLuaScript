package me.magnum.melonds.ui.emulator.component

import android.annotation.SuppressLint
import android.graphics.Rect
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import kotlin.math.hypot
import kotlin.math.max

/**
 * Panels that float over the emulator (Lua script keys, game switch button). They are placed with x/y as a fraction of
 * the free space, so that they keep their place across rotations.
 */
object FloatingPanels {

    /**
     * Area where the panel's top-left corner can go: the whole screen minus cutouts/system bars and a small margin.
     */
    fun movementBounds(panel: View): Rect? {
        val root = panel.parent as? View ?: return null
        if (root.width == 0 || panel.width == 0) {
            return null
        }

        val margin = (6 * panel.resources.displayMetrics.density).toInt()
        val insets = ViewCompat.getRootWindowInsets(root)?.getInsets(WindowInsetsCompat.Type.displayCutout() or WindowInsetsCompat.Type.systemBars())
        val left = (insets?.left ?: 0) + margin
        val top = (insets?.top ?: 0) + margin
        val right = max(left, root.width - (insets?.right ?: 0) - margin - panel.width)
        val bottom = max(top, root.height - (insets?.bottom ?: 0) - margin - panel.height)
        return Rect(left, top, right, bottom)
    }

    fun place(panel: View, fractionX: Float, fractionY: Float) {
        val bounds = movementBounds(panel) ?: return
        panel.x = bounds.left + bounds.width() * fractionX
        panel.y = bounds.top + bounds.height() * fractionY
    }

    /**
     * Dragging [handle] moves [panel]; a tap (no movement) calls [onTap]. [onMoved] receives the new position, as
     * fractions of the free space.
     */
    @SuppressLint("ClickableViewAccessibility")
    fun attachDragHandler(handle: View, panel: View, onMoved: (fractionX: Float, fractionY: Float) -> Unit, onTap: () -> Unit) {
        val touchSlop = ViewConfiguration.get(handle.context).scaledTouchSlop
        var startX = 0f
        var startY = 0f
        var offsetX = 0f
        var offsetY = 0f
        var dragging = false

        fun savePosition(bounds: Rect) {
            val fractionX = if (bounds.width() > 0) (panel.x - bounds.left) / bounds.width() else 0.5f
            val fractionY = if (bounds.height() > 0) (panel.y - bounds.top) / bounds.height() else 0f
            onMoved(fractionX.coerceIn(0f, 1f), fractionY.coerceIn(0f, 1f))
        }

        handle.setOnTouchListener { _, event ->
            val bounds = movementBounds(panel) ?: return@setOnTouchListener false
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    startX = event.rawX
                    startY = event.rawY
                    offsetX = panel.x - event.rawX
                    offsetY = panel.y - event.rawY
                    dragging = false
                    handle.alpha = 0.7f
                }
                MotionEvent.ACTION_MOVE -> {
                    if (!dragging && hypot(event.rawX - startX, event.rawY - startY) > touchSlop) {
                        dragging = true
                    }
                    if (dragging) {
                        panel.x = (event.rawX + offsetX).coerceIn(bounds.left.toFloat(), bounds.right.toFloat())
                        panel.y = (event.rawY + offsetY).coerceIn(bounds.top.toFloat(), bounds.bottom.toFloat())
                    }
                }
                MotionEvent.ACTION_UP -> {
                    handle.alpha = 1f
                    if (dragging) savePosition(bounds) else onTap()
                }
                MotionEvent.ACTION_CANCEL -> {
                    handle.alpha = 1f
                    if (dragging) savePosition(bounds)
                }
            }
            true
        }
    }
}
