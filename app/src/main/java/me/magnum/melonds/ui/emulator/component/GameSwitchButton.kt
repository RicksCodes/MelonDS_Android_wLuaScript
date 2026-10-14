package me.magnum.melonds.ui.emulator.component

import android.content.Context
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.view.Gravity
import android.view.HapticFeedbackConstants
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.constraintlayout.widget.ConstraintLayout
import androidx.core.content.edit
import kotlin.math.max

/**
 * Floating "1 ⇄ 2" button shown while a second game runs (multi-instance). A tap switches the game on screen, dragging
 * moves the button. The number of the game on screen is highlighted.
 */
class GameSwitchButton(
    private val activity: AppCompatActivity,
    private val rootProvider: () -> ViewGroup,
    private val onSwitch: () -> Unit,
) {

    private companion object {
        const val PREFERENCES_NAME = "multi_instance"
        const val KEY_X = "switch_x"
        const val KEY_Y = "switch_y"

        const val COLOR_BACKGROUND = 0xD92C2C34.toInt()
        const val COLOR_STROKE = 0x40FFFFFF
        const val COLOR_CURRENT = 0xFFFFFFFF.toInt()
        const val COLOR_OTHER = 0x80FFFFFF.toInt()
    }

    private val preferences by lazy { activity.getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE) }
    private var button: TextView? = null

    fun update(isSecondGameRunning: Boolean, isSecondGameVisible: Boolean) {
        if (!isSecondGameRunning) {
            button?.let { (it.parent as? ViewGroup)?.removeView(it) }
            button = null
            return
        }

        val currentButton = button ?: createButton(rootProvider()).also { button = it }
        currentButton.text = label(isSecondGameVisible)
    }

    private fun label(isSecondGameVisible: Boolean): CharSequence {
        val text = SpannableStringBuilder("1 ⇄ 2")
        val (currentStart, otherStart) = if (isSecondGameVisible) 4 to 0 else 0 to 4
        text.setSpan(ForegroundColorSpan(COLOR_CURRENT), currentStart, currentStart + 1, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        text.setSpan(ForegroundColorSpan(COLOR_OTHER), otherStart, otherStart + 1, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        return text
    }

    private fun createButton(root: ViewGroup): TextView {
        val density = activity.resources.displayMetrics.density
        val view = TextView(activity).apply {
            setTextColor(COLOR_CURRENT)
            textSize = 14f
            typeface = Typeface.DEFAULT_BOLD
            gravity = Gravity.CENTER
            elevation = 6 * density
            setPadding((14 * density).toInt(), (7 * density).toInt(), (14 * density).toInt(), (7 * density).toInt())
            background = GradientDrawable().apply {
                setColor(COLOR_BACKGROUND)
                cornerRadius = 100 * density
                setStroke(max((1 * density).toInt(), 1), COLOR_STROKE)
            }
        }

        val onMoved = { fractionX: Float, fractionY: Float ->
            preferences.edit {
                putFloat(KEY_X, fractionX)
                putFloat(KEY_Y, fractionY)
            }
        }
        FloatingPanels.attachDragHandler(view, view, onMoved) {
            view.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
            onSwitch()
        }

        val layoutParams = if (root is ConstraintLayout) {
            ConstraintLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply {
                startToStart = ConstraintLayout.LayoutParams.PARENT_ID
                topToTop = ConstraintLayout.LayoutParams.PARENT_ID
            }
        } else {
            FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP or Gravity.START)
        }
        root.addView(view, layoutParams)

        // Placed again after every layout (rotation, size changes). Top right by default
        view.addOnLayoutChangeListener { _, _, _, _, _, _, _, _, _ -> applyPosition(view) }
        root.addOnLayoutChangeListener(object : View.OnLayoutChangeListener {
            override fun onLayoutChange(v: View, left: Int, top: Int, right: Int, bottom: Int, oldLeft: Int, oldTop: Int, oldRight: Int, oldBottom: Int) {
                if (button != view) {
                    v.removeOnLayoutChangeListener(this)
                } else {
                    applyPosition(view)
                }
            }
        })
        return view
    }

    private fun applyPosition(view: View) {
        FloatingPanels.place(view, preferences.getFloat(KEY_X, 1f), preferences.getFloat(KEY_Y, 0f))
    }
}
