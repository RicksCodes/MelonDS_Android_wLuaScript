package me.magnum.melonds.ui.emulator.lua

import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.provider.OpenableColumns
import android.view.Gravity
import android.view.HapticFeedbackConstants
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.widget.SwitchCompat
import androidx.constraintlayout.widget.ConstraintLayout
import androidx.core.content.edit
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import me.magnum.melonds.MelonEmulator
import me.magnum.melonds.R
import me.magnum.melonds.debug.CrashLogger
import me.magnum.melonds.ui.emulator.component.FloatingPanels
import java.io.File
import kotlin.math.max
import kotlin.math.min

/**
 * UI side of the Lua scripting support: script picker, pause-menu submenu, on-screen script keys (with their appearance
 * settings) and console.
 *
 * Must be created during the activity's initialisation (it registers an activity result launcher).
 */
class LuaScriptController(
    private val activity: AppCompatActivity,
    private val rootProvider: () -> ViewGroup,
    private val onResumeEmulator: () -> Unit,
) {

    private companion object {
        const val STATUS_RUNNING = 1
        const val POLL_INTERVAL_MS = 250L
        const val MESSAGE_TOAST_INTERVAL_MS = 1500L
        const val MAX_CONSOLE_LENGTH = 20_000

        const val PREFERENCES_NAME = "lua_scripts"
        const val KEY_LAST_SCRIPT_URI = "last_script_uri"
        const val KEY_LAST_SCRIPT_NAME = "last_script_name"
        const val KEY_SHOW_KEYS = "show_keys"
        const val KEY_KEYS_AT_BOTTOM = "keys_at_bottom"
        const val KEY_KEYS_SIZE = "keys_size"
        const val KEY_KEYS_OPACITY = "keys_opacity"
        const val KEY_KEYS_THEME = "keys_theme"
        const val KEY_KEYS_LAYOUT = "keys_layout"
        const val KEY_KEYS_X = "keys_x"
        const val KEY_KEYS_Y = "keys_y"
        const val KEY_KEYS_HAPTIC = "keys_haptic"
        const val KEY_KEYS_EXPANDED = "keys_expanded"
        const val KEY_OVERLAY_OPACITY = "overlay_opacity"

        const val DEFAULT_KEYS_SIZE = 100
        const val DEFAULT_KEYS_OPACITY = 90
        const val DEFAULT_OVERLAY_OPACITY = 100

        const val LAYOUT_ROW = 0
        const val LAYOUT_COLUMN = 1
        const val LAYOUT_GRID = 2

        val SCRIPT_KEYS = listOf("1", "2", "3", "4", "5", "6", "7", "8", "9", "0")

        val KEY_THEMES = listOf(
            KeyTheme("Scuro", panel = 0xD9101014.toInt(), key = 0xFF2C2C34.toInt(), keyPressed = 0xFF5A5A6E.toInt(), stroke = 0x40FFFFFF, text = 0xFFFFFFFF.toInt()),
            KeyTheme("Chiaro", panel = 0xD9F2F2F5.toInt(), key = 0xFFFFFFFF.toInt(), keyPressed = 0xFFCFCFD8.toInt(), stroke = 0x33000000, text = 0xFF1E1E24.toInt()),
            KeyTheme("Blu", panel = 0xD90B1A2E.toInt(), key = 0xFF1F5FBF.toInt(), keyPressed = 0xFF4A86E8.toInt(), stroke = 0x60BBDDFF, text = 0xFFFFFFFF.toInt()),
            KeyTheme("Rosso", panel = 0xD92A0C0C.toInt(), key = 0xFFC62828.toInt(), keyPressed = 0xFFEF5350.toInt(), stroke = 0x60FFCDD2, text = 0xFFFFFFFF.toInt()),
            KeyTheme("Verde", panel = 0xD90C2410.toInt(), key = 0xFF2E7D32.toInt(), keyPressed = 0xFF4CAF50.toInt(), stroke = 0x60C8E6C9, text = 0xFFFFFFFF.toInt()),
            KeyTheme("Viola", panel = 0xD91A0F2A.toInt(), key = 0xFF6A3FB5.toInt(), keyPressed = 0xFF9575CD.toInt(), stroke = 0x60E1D5F5, text = 0xFFFFFFFF.toInt()),
        )
    }

    private data class KeyTheme(val name: String, val panel: Int, val key: Int, val keyPressed: Int, val stroke: Int, val text: Int)

    private val preferences by lazy { activity.getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE) }
    private val density get() = activity.resources.displayMetrics.density
    private val console = StringBuilder()
    private var keysBar: View? = null
    private var scriptRunning = false
    // Keys are shown while the appearance dialog is open, so that changes can be previewed even without a script
    private var previewKeys = false
    private var lastMessageToastTime = 0L
    private var rootLayoutListener: View.OnLayoutChangeListener? = null
    private var lastRootSize = 0 to 0

    private val scriptPicker = activity.registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        CrashLogger.note("Selettore script chiuso, file: $uri")
        if (uri != null) {
            try {
                activity.contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
            } catch (_: SecurityException) {
                // Not persistable. The script can still be loaded now, just not reloaded after a restart
            }
            loadScript(uri)
        }
        onResumeEmulator()
    }

    /**
     * Starts polling the script status and output. Call from onCreate().
     */
    fun attach() {
        MelonEmulator.setLuaOverlayOpacity(preferences.getInt(KEY_OVERLAY_OPACITY, DEFAULT_OVERLAY_OPACITY) / 100f)
        activity.lifecycleScope.launch {
            activity.repeatOnLifecycle(Lifecycle.State.STARTED) {
                while (isActive) {
                    poll()
                    delay(POLL_INTERVAL_MS)
                }
            }
        }
    }

    fun showMenu() {
        CrashLogger.note("Menu Script Lua aperto (script in esecuzione: $scriptRunning)")
        val labels = mutableListOf<String>()
        val actions = mutableListOf<() -> Unit>()

        labels += activity.getString(R.string.lua_load_script)
        actions += {
            CrashLogger.note("Apertura selettore file")
            scriptPicker.launch(arrayOf("*/*"))
        }

        val lastUri = preferences.getString(KEY_LAST_SCRIPT_URI, null)?.let { Uri.parse(it) }
        val lastName = preferences.getString(KEY_LAST_SCRIPT_NAME, null)
        if (lastUri != null) {
            labels += activity.getString(R.string.lua_reload_script, lastName ?: lastUri.lastPathSegment ?: "")
            actions += {
                loadScript(lastUri)
                onResumeEmulator()
            }
        }

        if (scriptRunning) {
            labels += activity.getString(R.string.lua_stop_script)
            actions += {
                MelonEmulator.stopLuaScript()
                onResumeEmulator()
            }
        }

        val showKeys = preferences.getBoolean(KEY_SHOW_KEYS, true)
        labels += activity.getString(if (showKeys) R.string.lua_hide_keys else R.string.lua_show_keys)
        actions += {
            preferences.edit { putBoolean(KEY_SHOW_KEYS, !showKeys) }
            updateKeysBar()
            onResumeEmulator()
        }

        labels += activity.getString(R.string.lua_appearance)
        actions += { showAppearanceDialog() }

        labels += activity.getString(R.string.lua_console)
        actions += { showConsole() }

        AlertDialog.Builder(activity)
            .setTitle(R.string.lua_scripts)
            .setItems(labels.toTypedArray()) { _, which -> actions[which]() }
            .setOnCancelListener { onResumeEmulator() }
            .show()
    }

    /**
     * Forwards physical keyboard keys to the script's input.get(). Never consumes the event.
     */
    fun onKeyEvent(event: KeyEvent) {
        if (!scriptRunning) {
            return
        }

        val name = keyName(event.keyCode) ?: return
        when (event.action) {
            KeyEvent.ACTION_DOWN -> if (event.repeatCount == 0) MelonEmulator.setLuaKeyState(name, true)
            KeyEvent.ACTION_UP -> MelonEmulator.setLuaKeyState(name, false)
        }
    }

    private fun loadScript(uri: Uri) {
        val name = queryDisplayName(uri) ?: uri.lastPathSegment?.substringAfterLast('/') ?: "script.lua"
        val source = try {
            activity.contentResolver.openInputStream(uri)?.use { it.readBytes() }
        } catch (e: Exception) {
            null
        }

        if (source == null) {
            Toast.makeText(activity, R.string.lua_read_failed, Toast.LENGTH_LONG).show()
            return
        }

        // Files written by the script (e.g. its own save data) go to Android/data/<app>/files/lua/<script name>
        val baseDir = activity.getExternalFilesDir(null) ?: activity.filesDir
        val folderName = name.substringBeforeLast('.').replace(Regex("[^A-Za-z0-9._-]"), "_").ifEmpty { "script" }
        val workDir = File(baseDir, "lua/$folderName").apply { mkdirs() }

        CrashLogger.note("Caricamento script '$name' (${source.size} byte), cartella ${workDir.absolutePath}")
        MelonEmulator.loadLuaScript(name, source, workDir.absolutePath)
        CrashLogger.note("Script inviato al motore Lua")
        preferences.edit {
            putString(KEY_LAST_SCRIPT_URI, uri.toString())
            putString(KEY_LAST_SCRIPT_NAME, name)
        }
    }

    private fun queryDisplayName(uri: Uri): String? {
        return try {
            activity.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
                if (cursor.moveToFirst()) cursor.getString(0) else null
            }
        } catch (e: Exception) {
            null
        }
    }

    private fun poll() {
        val output = MelonEmulator.pollLuaOutput()
        if (output.isNotEmpty()) {
            val text = String(output, Charsets.UTF_8)
            console.append(text)
            if (console.length > MAX_CONSOLE_LENGTH) {
                console.delete(0, console.length - MAX_CONSOLE_LENGTH)
            }

            val lines = text.lines().filter { it.isNotBlank() }
            lines.take(20).forEach { CrashLogger.note("Output Lua: ${it.take(300)}") }
            val error = lines.lastOrNull { it.startsWith("[Errore]") }
            if (error != null) {
                Toast.makeText(activity, error.take(300), Toast.LENGTH_LONG).show()
            } else {
                val now = System.currentTimeMillis()
                val lastLine = lines.lastOrNull()
                if (lastLine != null && now - lastMessageToastTime > MESSAGE_TOAST_INTERVAL_MS) {
                    lastMessageToastTime = now
                    Toast.makeText(activity, lastLine.take(200), Toast.LENGTH_SHORT).show()
                }
            }
        }

        val running = MelonEmulator.getLuaStatus() == STATUS_RUNNING
        if (running != scriptRunning) {
            CrashLogger.note("Stato script: ${if (running) "in esecuzione" else "fermo"} (codice ${MelonEmulator.getLuaStatus()})")
            scriptRunning = running
            updateKeysBar()
        }
    }

    private fun showConsole() {
        val padding = (16 * density).toInt()
        val textView = TextView(activity).apply {
            text = if (console.isEmpty()) activity.getString(R.string.lua_console_empty) else console.toString()
            typeface = Typeface.MONOSPACE
            textSize = 12f
            setTextIsSelectable(true)
            setPadding(padding, padding, padding, padding)
        }
        val scrollView = ScrollView(activity).apply {
            addView(textView)
            post { fullScroll(View.FOCUS_DOWN) }
        }

        AlertDialog.Builder(activity)
            .setTitle(R.string.lua_console)
            .setView(scrollView)
            .setPositiveButton(R.string.lua_close, null)
            .setNeutralButton(R.string.lua_clear) { _, _ -> console.clear() }
            .setOnDismissListener { onResumeEmulator() }
            .show()
    }

    // --- On-screen script keys ---

    private fun updateKeysBar() {
        val shouldShow = previewKeys || (scriptRunning && preferences.getBoolean(KEY_SHOW_KEYS, true))
        if (shouldShow) {
            if (keysBar == null) {
                CrashLogger.note("Creazione barra tasti 1-0")
                keysBar = createKeysPanel(rootProvider())
                CrashLogger.note("Barra tasti creata")
            }
            keysBar?.visibility = View.VISIBLE
        } else {
            keysBar?.visibility = View.GONE
            // Make sure no key stays pressed while hidden
            SCRIPT_KEYS.forEach { MelonEmulator.setLuaKeyState(it, false) }
        }
    }

    private fun removeKeysBar() {
        keysBar?.let { (it.parent as? ViewGroup)?.removeView(it) }
        keysBar = null
    }

    private fun rebuildKeysBar() {
        removeKeysBar()
        updateKeysBar()
    }

    private fun currentTheme() = KEY_THEMES[preferences.getInt(KEY_KEYS_THEME, 0).coerceIn(0, KEY_THEMES.size - 1)]

    @SuppressLint("ClickableViewAccessibility")
    private fun createKeysPanel(root: ViewGroup): View {
        val theme = currentTheme()
        val scale = preferences.getInt(KEY_KEYS_SIZE, DEFAULT_KEYS_SIZE) / 100f
        val keySize = (40 * density * scale).toInt()
        val gap = max((4 * density * scale).toInt(), 2)
        val haptic = preferences.getBoolean(KEY_KEYS_HAPTIC, true)

        val panel = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            elevation = 6 * density
            alpha = preferences.getInt(KEY_KEYS_OPACITY, DEFAULT_KEYS_OPACITY) / 100f
        }
        val panelBackground = GradientDrawable().apply {
            setColor(theme.panel)
            cornerRadius = 14 * density * scale
        }

        // Small button that opens/closes the keypad. Dragging it moves the whole panel.
        val toggle = TextView(activity).apply {
            setTextColor(theme.text)
            textSize = 13f * scale
            typeface = Typeface.DEFAULT_BOLD
            gravity = Gravity.CENTER
            setPadding((12 * density * scale).toInt(), (6 * density * scale).toInt(), (12 * density * scale).toInt(), (6 * density * scale).toInt())
            background = GradientDrawable().apply {
                setColor(theme.key)
                cornerRadius = 100 * density
                setStroke(max((1 * density).toInt(), 1), theme.stroke)
            }
        }
        panel.addView(toggle, LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply {
            setMargins(gap, gap, gap, gap)
        })

        // How many keys fit in a column/row with the current screen size (it changes with the rotation)
        val cell = keySize + gap
        val screenMargin = (24 * density).toInt()
        val toggleHeight = (40 * density * scale).toInt()
        val rootWidth = root.width.takeIf { it > 0 } ?: activity.resources.displayMetrics.widthPixels
        val rootHeight = root.height.takeIf { it > 0 } ?: activity.resources.displayMetrics.heightPixels
        val maxRows = max(1, (rootHeight - toggleHeight - screenMargin) / cell)
        val maxColumns = max(1, (rootWidth - screenMargin) / cell)
        lastRootSize = rootWidth to rootHeight

        val keyCount = SCRIPT_KEYS.size
        val keys = when (preferences.getInt(KEY_KEYS_LAYOUT, LAYOUT_COLUMN)) {
            // Column: 1, 2, 3... from top to bottom. When the screen is too short (landscape) it splits into more columns
            LAYOUT_COLUMN -> keyLines(SCRIPT_KEYS.chunked(min(keyCount, maxRows)), vertical = true, theme, keySize, gap, scale, haptic)
            LAYOUT_GRID -> keyLines(SCRIPT_KEYS.chunked(min(5, maxColumns)), vertical = false, theme, keySize, gap, scale, haptic)
            // Row: wraps into more rows when the screen is too narrow (portrait)
            else -> keyLines(SCRIPT_KEYS.chunked(min(keyCount, maxColumns)), vertical = false, theme, keySize, gap, scale, haptic)
        }
        // WRAP_CONTENT is essential: with the default MATCH_PARENT the panel would only be as wide as the toggle
        panel.addView(keys, LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT))

        fun showExpanded(expanded: Boolean) {
            keys.visibility = if (expanded) View.VISIBLE else View.GONE
            panel.background = if (expanded) panelBackground else null
            toggle.text = if (expanded) "1–0  ▲" else "1–0  ▼"
            if (!expanded) {
                SCRIPT_KEYS.forEach { MelonEmulator.setLuaKeyState(it, false) }
            }
        }
        // While the appearance dialog is open the keypad is always open, to preview it
        showExpanded(previewKeys || preferences.getBoolean(KEY_KEYS_EXPANDED, false))

        val onMoved = { fractionX: Float, fractionY: Float ->
            preferences.edit {
                putFloat(KEY_KEYS_X, fractionX)
                putFloat(KEY_KEYS_Y, fractionY)
            }
        }
        FloatingPanels.attachDragHandler(toggle, panel, onMoved) {
            val expanded = keys.visibility != View.VISIBLE
            preferences.edit { putBoolean(KEY_KEYS_EXPANDED, expanded) }
            showExpanded(expanded)
            if (haptic) {
                toggle.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
            }
        }

        val layoutParams = if (root is ConstraintLayout) {
            ConstraintLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply {
                startToStart = ConstraintLayout.LayoutParams.PARENT_ID
                topToTop = ConstraintLayout.LayoutParams.PARENT_ID
            }
        } else {
            FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP or Gravity.START)
        }
        root.addView(panel, layoutParams)

        // The panel is placed with x/y, as a fraction of the free space, so it keeps its place across rotations
        panel.addOnLayoutChangeListener { _, _, _, _, _, _, _, _, _ -> applyKeysPosition() }
        if (rootLayoutListener == null) {
            val listener = View.OnLayoutChangeListener { view, _, _, _, _, _, _, _, _ ->
                val size = view.width to view.height
                if (keysBar != null && size != lastRootSize) {
                    // Rotation: rebuild the keypad so the columns fit the new screen
                    lastRootSize = size
                    view.post { rebuildKeysBar() }
                } else {
                    applyKeysPosition()
                }
            }
            root.addOnLayoutChangeListener(listener)
            rootLayoutListener = listener
        }
        return panel
    }

    /**
     * Groups of keys laid out as columns ([vertical]) or rows, side by side.
     */
    private fun keyLines(lines: List<List<String>>, vertical: Boolean, theme: KeyTheme, keySize: Int, gap: Int, scale: Float, haptic: Boolean): LinearLayout {
        val container = LinearLayout(activity).apply {
            orientation = if (vertical) LinearLayout.HORIZONTAL else LinearLayout.VERTICAL
            setPadding(gap / 2, 0, gap / 2, gap / 2)
        }
        lines.forEach { lineKeys ->
            val line = LinearLayout(activity).apply { orientation = if (vertical) LinearLayout.VERTICAL else LinearLayout.HORIZONTAL }
            lineKeys.forEach { key ->
                line.addView(createKey(key, theme, keySize, scale, haptic), LinearLayout.LayoutParams(keySize, keySize).apply {
                    setMargins(gap / 2, gap / 2, gap / 2, gap / 2)
                })
            }
            container.addView(line, LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        }
        return container
    }

    @SuppressLint("ClickableViewAccessibility")
    private fun createKey(key: String, theme: KeyTheme, size: Int, scale: Float, haptic: Boolean): View {
        val normal = keyBackground(theme.key, theme.stroke, size)
        val pressed = keyBackground(theme.keyPressed, theme.stroke, size)
        return TextView(activity).apply {
            text = key
            setTextColor(theme.text)
            textSize = 16f * scale
            typeface = Typeface.DEFAULT_BOLD
            gravity = Gravity.CENTER
            background = normal
            setOnTouchListener { view, event ->
                when (event.actionMasked) {
                    MotionEvent.ACTION_DOWN -> {
                        view.background = pressed
                        view.animate().scaleX(0.88f).scaleY(0.88f).setDuration(50).start()
                        if (haptic) {
                            view.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
                        }
                        MelonEmulator.setLuaKeyState(key, true)
                    }
                    MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                        view.background = normal
                        view.animate().scaleX(1f).scaleY(1f).setDuration(90).start()
                        MelonEmulator.setLuaKeyState(key, false)
                    }
                }
                true
            }
        }
    }

    private fun keyBackground(color: Int, stroke: Int, size: Int) = GradientDrawable().apply {
        setColor(color)
        cornerRadius = size * 0.3f
        setStroke(max((1 * density).toInt(), 1), stroke)
    }

    private fun applyKeysPosition() {
        val panel = keysBar ?: return
        // Older versions only had "top" or "bottom"
        val defaultY = if (preferences.getBoolean(KEY_KEYS_AT_BOTTOM, false)) 1f else 0f
        FloatingPanels.place(panel, preferences.getFloat(KEY_KEYS_X, 0.5f), preferences.getFloat(KEY_KEYS_Y, defaultY))
    }

    private fun setKeysPosition(fractionX: Float, fractionY: Float) {
        preferences.edit {
            putFloat(KEY_KEYS_X, fractionX)
            putFloat(KEY_KEYS_Y, fractionY)
        }
        applyKeysPosition()
    }

    // --- Appearance dialog ---

    private fun showAppearanceDialog() {
        previewKeys = true
        rebuildKeysBar()

        val padding = (20 * density).toInt()
        val content = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(padding, padding / 2, padding, 0)
        }

        content.addView(sectionTitle(R.string.lua_appearance_keys, first = true))
        content.addView(slider(R.string.lua_appearance_size, 70, 160, preferences.getInt(KEY_KEYS_SIZE, DEFAULT_KEYS_SIZE), live = false) {
            preferences.edit { putInt(KEY_KEYS_SIZE, it) }
            rebuildKeysBar()
        })
        content.addView(slider(R.string.lua_appearance_opacity, 20, 100, preferences.getInt(KEY_KEYS_OPACITY, DEFAULT_KEYS_OPACITY), live = true) {
            preferences.edit { putInt(KEY_KEYS_OPACITY, it) }
            keysBar?.alpha = it / 100f
        })

        content.addView(label(R.string.lua_appearance_color))
        content.addView(themeSwatches())

        content.addView(label(R.string.lua_appearance_layout))
        content.addView(layoutSelector())

        content.addView(label(R.string.lua_appearance_position))
        content.addView(positionButtons())
        content.addView(TextView(activity).apply {
            setText(R.string.lua_appearance_position_hint)
            textSize = 12f
            alpha = 0.7f
        })

        content.addView(SwitchCompat(activity).apply {
            setText(R.string.lua_appearance_vibration)
            isChecked = preferences.getBoolean(KEY_KEYS_HAPTIC, true)
            setPadding(0, (8 * density).toInt(), 0, 0)
            setOnCheckedChangeListener { _, checked ->
                preferences.edit { putBoolean(KEY_KEYS_HAPTIC, checked) }
                rebuildKeysBar()
            }
        })

        content.addView(sectionTitle(R.string.lua_appearance_overlay, first = false))
        content.addView(slider(R.string.lua_appearance_overlay_opacity, 20, 100, preferences.getInt(KEY_OVERLAY_OPACITY, DEFAULT_OVERLAY_OPACITY), live = true) {
            preferences.edit { putInt(KEY_OVERLAY_OPACITY, it) }
            MelonEmulator.setLuaOverlayOpacity(it / 100f)
        })

        val dialog = AlertDialog.Builder(activity)
            .setTitle(R.string.lua_appearance)
            .setView(ScrollView(activity).apply { addView(content) })
            .setPositiveButton(R.string.lua_close, null)
            .setNeutralButton(R.string.lua_appearance_reset) { _, _ -> resetAppearance() }
            .setOnDismissListener {
                previewKeys = false
                rebuildKeysBar()
                onResumeEmulator()
            }
            .show()
        // Keep the game and the keys visible behind the dialog to preview the changes
        dialog.window?.setDimAmount(0.15f)
    }

    private fun resetAppearance() {
        preferences.edit {
            remove(KEY_KEYS_SIZE)
            remove(KEY_KEYS_OPACITY)
            remove(KEY_KEYS_THEME)
            remove(KEY_KEYS_LAYOUT)
            remove(KEY_KEYS_X)
            remove(KEY_KEYS_Y)
            remove(KEY_KEYS_AT_BOTTOM)
            remove(KEY_KEYS_HAPTIC)
            remove(KEY_OVERLAY_OPACITY)
        }
        MelonEmulator.setLuaOverlayOpacity(DEFAULT_OVERLAY_OPACITY / 100f)
    }

    private fun sectionTitle(text: Int, first: Boolean) = TextView(activity).apply {
        setText(text)
        textSize = 16f
        typeface = Typeface.DEFAULT_BOLD
        setPadding(0, if (first) 0 else (20 * density).toInt(), 0, (4 * density).toInt())
    }

    private fun label(text: Int) = TextView(activity).apply {
        setText(text)
        textSize = 14f
        setPadding(0, (12 * density).toInt(), 0, (4 * density).toInt())
    }

    /**
     * A labelled slider showing its value in percent. With [live] the callback runs while dragging, otherwise only
     * when the finger is lifted (for changes that rebuild views).
     */
    private fun slider(title: Int, minValue: Int, maxValue: Int, value: Int, live: Boolean, onChange: (Int) -> Unit): View {
        val container = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL }
        val header = LinearLayout(activity).apply { orientation = LinearLayout.HORIZONTAL }
        val titleView = TextView(activity).apply {
            setText(title)
            textSize = 14f
        }
        val valueView = TextView(activity).apply {
            text = "$value%"
            textSize = 14f
            gravity = Gravity.END
        }
        header.addView(titleView, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        header.addView(valueView)
        header.setPadding(0, (12 * density).toInt(), 0, 0)

        val seekBar = SeekBar(activity).apply {
            max = maxValue - minValue
            progress = value.coerceIn(minValue, maxValue) - minValue
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                    valueView.text = "${progress + minValue}%"
                    if (live && fromUser) {
                        onChange(progress + minValue)
                    }
                }

                override fun onStartTrackingTouch(seekBar: SeekBar) {}

                override fun onStopTrackingTouch(seekBar: SeekBar) {
                    onChange(seekBar.progress + minValue)
                }
            })
        }

        container.addView(header)
        container.addView(seekBar)
        return container
    }

    private fun themeSwatches(): View {
        val row = LinearLayout(activity).apply { orientation = LinearLayout.HORIZONTAL }
        val swatchSize = (34 * density).toInt()
        val swatches = mutableListOf<View>()

        fun refresh() {
            val selected = preferences.getInt(KEY_KEYS_THEME, 0)
            swatches.forEachIndexed { index, view ->
                val theme = KEY_THEMES[index]
                view.background = GradientDrawable().apply {
                    shape = GradientDrawable.OVAL
                    setColor(theme.key)
                    if (index == selected) {
                        setStroke((3 * density).toInt(), 0xFF8A8A8A.toInt())
                    } else {
                        setStroke(max((1 * density).toInt(), 1), 0x55000000)
                    }
                }
                view.scaleX = if (index == selected) 1.1f else 1f
                view.scaleY = view.scaleX
            }
        }

        KEY_THEMES.forEachIndexed { index, theme ->
            val swatch = View(activity).apply {
                contentDescription = theme.name
                setOnClickListener {
                    preferences.edit { putInt(KEY_KEYS_THEME, index) }
                    refresh()
                    rebuildKeysBar()
                }
            }
            swatches += swatch
            row.addView(swatch, LinearLayout.LayoutParams(swatchSize, swatchSize).apply {
                setMargins(0, 0, (10 * density).toInt(), 0)
            })
        }
        refresh()
        return row
    }

    private fun layoutSelector(): View {
        val options = listOf(
            LAYOUT_ROW to R.string.lua_appearance_layout_row,
            LAYOUT_COLUMN to R.string.lua_appearance_layout_column,
            LAYOUT_GRID to R.string.lua_appearance_layout_grid,
        )
        val current = preferences.getInt(KEY_KEYS_LAYOUT, LAYOUT_COLUMN)
        return RadioGroup(activity).apply {
            orientation = RadioGroup.HORIZONTAL
            options.forEach { (layout, text) ->
                val button = RadioButton(activity).apply {
                    id = View.generateViewId()
                    setText(text)
                    isChecked = layout == current
                    setPadding(0, 0, (12 * density).toInt(), 0)
                }
                addView(button)
                button.setOnCheckedChangeListener { _, checked ->
                    if (checked) {
                        preferences.edit { putInt(KEY_KEYS_LAYOUT, layout) }
                        rebuildKeysBar()
                    }
                }
            }
        }
    }

    private fun positionButtons(): View {
        val row = LinearLayout(activity).apply { orientation = LinearLayout.HORIZONTAL }
        val positions = listOf(
            R.string.lua_appearance_top to (0.5f to 0f),
            R.string.lua_appearance_bottom to (0.5f to 1f),
            R.string.lua_appearance_left to (0f to 0.5f),
            R.string.lua_appearance_right to (1f to 0.5f),
        )
        positions.forEach { (text, position) ->
            val button = Button(activity, null, android.R.attr.borderlessButtonStyle).apply {
                setText(text)
                isAllCaps = false
                minWidth = 0
                minimumWidth = 0
                setPadding((8 * density).toInt(), 0, (8 * density).toInt(), 0)
                setOnClickListener { setKeysPosition(position.first, position.second) }
            }
            row.addView(button, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        }
        return row
    }

    private fun keyName(keyCode: Int): String? {
        return when (keyCode) {
            in KeyEvent.KEYCODE_0..KeyEvent.KEYCODE_9 -> (keyCode - KeyEvent.KEYCODE_0).toString()
            in KeyEvent.KEYCODE_A..KeyEvent.KEYCODE_Z -> ('A' + (keyCode - KeyEvent.KEYCODE_A)).toString()
            in KeyEvent.KEYCODE_F1..KeyEvent.KEYCODE_F12 -> "F${keyCode - KeyEvent.KEYCODE_F1 + 1}"
            in KeyEvent.KEYCODE_NUMPAD_0..KeyEvent.KEYCODE_NUMPAD_9 -> "numpad${keyCode - KeyEvent.KEYCODE_NUMPAD_0}"
            KeyEvent.KEYCODE_SHIFT_LEFT, KeyEvent.KEYCODE_SHIFT_RIGHT -> "shift"
            KeyEvent.KEYCODE_CTRL_LEFT, KeyEvent.KEYCODE_CTRL_RIGHT -> "control"
            KeyEvent.KEYCODE_ALT_LEFT, KeyEvent.KEYCODE_ALT_RIGHT -> "alt"
            KeyEvent.KEYCODE_SPACE -> "space"
            KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER -> "enter"
            KeyEvent.KEYCODE_ESCAPE -> "escape"
            KeyEvent.KEYCODE_TAB -> "tab"
            KeyEvent.KEYCODE_DEL -> "backspace"
            KeyEvent.KEYCODE_FORWARD_DEL -> "delete"
            KeyEvent.KEYCODE_PAGE_UP -> "pageup"
            KeyEvent.KEYCODE_PAGE_DOWN -> "pagedown"
            KeyEvent.KEYCODE_MOVE_HOME -> "home"
            KeyEvent.KEYCODE_MOVE_END -> "end"
            KeyEvent.KEYCODE_INSERT -> "insert"
            KeyEvent.KEYCODE_DPAD_UP -> "up"
            KeyEvent.KEYCODE_DPAD_DOWN -> "down"
            KeyEvent.KEYCODE_DPAD_LEFT -> "left"
            KeyEvent.KEYCODE_DPAD_RIGHT -> "right"
            else -> null
        }
    }
}
