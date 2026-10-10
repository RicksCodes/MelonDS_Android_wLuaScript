package me.magnum.melonds.ui.settings.fragments

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Intent
import android.graphics.Typeface
import android.os.Bundle
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.core.content.getSystemService
import androidx.preference.Preference
import me.magnum.melonds.R
import me.magnum.melonds.debug.CrashLogger
import me.magnum.melonds.ui.settings.PreferenceFragmentTitleProvider

/**
 * Settings > Log: shows the latest session report (only one is kept) and lets the user copy or share it.
 */
class LogPreferencesFragment : BasePreferenceFragment(), PreferenceFragmentTitleProvider {

    private companion object {
        // Clipboard and share intents go through a binder transaction, which has a size limit
        const val MAX_SHARE_LENGTH = 100_000
    }

    override fun getTitle() = getString(R.string.log)

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        setPreferencesFromResource(R.xml.pref_log, rootKey)

        findPreference<Preference>("log_view")!!.setOnPreferenceClickListener {
            withReport { showReport(it) }
            true
        }
        findPreference<Preference>("log_copy")!!.setOnPreferenceClickListener {
            withReport { copyReport(it) }
            true
        }
        findPreference<Preference>("log_share")!!.setOnPreferenceClickListener {
            withReport { shareReport(it) }
            true
        }
        findPreference<Preference>("log_delete")!!.setOnPreferenceClickListener {
            CrashLogger.deleteLastReport(requireContext())
            Toast.makeText(requireContext(), R.string.log_deleted, Toast.LENGTH_SHORT).show()
            updateInfo()
            true
        }
    }

    override fun onResume() {
        super.onResume()
        updateInfo()
    }

    private fun updateInfo() {
        val report = CrashLogger.getLastReport(requireContext())
        val info = findPreference<Preference>("log_info")!!
        if (report == null) {
            info.title = getString(R.string.log_none)
            info.summary = null
        } else {
            // Second and third lines of the report: type of closing and date
            val lines = report.lines()
            info.title = lines.getOrNull(1)
            info.summary = lines.getOrNull(2)
        }

        listOf("log_view", "log_copy", "log_share", "log_delete").forEach {
            findPreference<Preference>(it)!!.isEnabled = report != null
        }
    }

    private fun withReport(action: (String) -> Unit) {
        val report = CrashLogger.getLastReport(requireContext())
        if (report == null) {
            Toast.makeText(requireContext(), R.string.log_none, Toast.LENGTH_SHORT).show()
            updateInfo()
        } else {
            action(report)
        }
    }

    private fun showReport(report: String) {
        val padding = (16 * resources.displayMetrics.density).toInt()
        val textView = TextView(requireContext()).apply {
            text = report
            typeface = Typeface.MONOSPACE
            textSize = 10f
            setTextIsSelectable(true)
            setPadding(padding, padding, padding, padding)
        }

        AlertDialog.Builder(requireContext())
            .setTitle(R.string.log)
            .setView(ScrollView(requireContext()).apply { addView(textView) })
            .setPositiveButton(R.string.log_copy) { _, _ -> copyReport(report) }
            .setNeutralButton(R.string.log_share) { _, _ -> shareReport(report) }
            .setNegativeButton(R.string.lua_close, null)
            .show()
    }

    private fun copyReport(report: String) {
        val clipboard = requireContext().getSystemService<ClipboardManager>() ?: return
        clipboard.setPrimaryClip(ClipData.newPlainText("melonDS log", report.take(MAX_SHARE_LENGTH)))
        Toast.makeText(requireContext(), R.string.log_copied, Toast.LENGTH_SHORT).show()
    }

    private fun shareReport(report: String) {
        val intent = Intent(Intent.ACTION_SEND).apply {
            type = "text/plain"
            putExtra(Intent.EXTRA_SUBJECT, "melonDS log")
            putExtra(Intent.EXTRA_TEXT, report.take(MAX_SHARE_LENGTH))
        }
        startActivity(Intent.createChooser(intent, getString(R.string.log_share)))
    }
}
