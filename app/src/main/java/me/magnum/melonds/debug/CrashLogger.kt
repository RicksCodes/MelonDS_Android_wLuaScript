package me.magnum.melonds.debug

import android.app.Activity
import android.app.ActivityManager
import android.app.Application
import android.app.ApplicationExitInfo
import android.content.Context
import android.os.Build
import android.os.Bundle
import android.util.Log
import androidx.preference.PreferenceManager
import me.magnum.melonds.MelonEmulator
import me.magnum.melonds.ui.emulator.EmulatorActivity
import java.io.File
import java.io.PrintWriter
import java.io.StringWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Diagnostics: records what happens during an emulator session and, when the game closes (normally or because of a
 * crash), writes a report. Only the latest report is kept; it can be viewed and copied from Settings > Log.
 */
object CrashLogger {

    private const val TAG = "melonDS-Lua"
    private const val MAX_SESSION_SIZE = 200_000L
    private const val MAX_LOGCAT_LINES = 400

    private lateinit var application: Application
    private lateinit var directory: File
    private val sessionFile get() = File(directory, "session.txt")
    private val sessionMarker get() = File(directory, "session_active")
    private val nativeCrashFile get() = File(directory, "native_crash.txt")
    private val javaCrashFile get() = File(directory, "java_crash.txt")

    /**
     * The latest report (crash or normal exit), or null if there is none yet.
     */
    fun getLastReport(context: Context): String? {
        val file = lastReportFile(context)
        return if (file.exists()) file.readText() else null
    }

    fun deleteLastReport(context: Context) {
        lastReportFile(context).delete()
    }

    private fun lastReportFile(context: Context) = File(File(context.filesDir, "crashlog"), "last_report.txt")

    fun install(app: Application) {
        application = app
        directory = File(app.filesDir, "crashlog").apply { mkdirs() }

        // Collect what the previous run left behind before the new handlers can overwrite it
        val previousRunCrashed = sessionMarker.exists() || nativeCrashFile.exists() || javaCrashFile.exists()
        if (previousRunCrashed) {
            val nativeCrash = readAndDelete(nativeCrashFile)
            val javaCrash = readAndDelete(javaCrashFile)
            val session = sessionFile.takeIf { it.exists() }?.readText().orEmpty()
            sessionMarker.delete()
            Thread {
                saveReport(buildReport(
                    title = "CHIUSURA IMPROVVISA (crash o processo terminato)",
                    nativeCrash = nativeCrash,
                    javaCrash = javaCrash,
                    session = session,
                    includeExitReasons = true,
                ))
            }.start()
        }

        try {
            MelonEmulator.installCrashHandler(nativeCrashFile.absolutePath)
        } catch (e: Throwable) {
            Log.e(TAG, "Impossibile installare il gestore dei crash nativi", e)
        }
        installJavaCrashHandler()
        app.registerActivityLifecycleCallbacks(lifecycleCallbacks)
    }

    /**
     * Adds a line to the current session log (and to logcat).
     */
    fun note(message: String) {
        Log.i(TAG, message)
        if (!::directory.isInitialized) {
            return
        }

        synchronized(this) {
            try {
                if (sessionFile.length() < MAX_SESSION_SIZE) {
                    sessionFile.appendText("[${timestamp()}] $message\n")
                }
            } catch (_: Exception) {
            }
        }
    }

    private fun startSession(activity: Activity) {
        synchronized(this) {
            sessionFile.writeText("Sessione avviata ${dateTime()}\n")
            sessionMarker.writeText("1")
        }
        note("EmulatorActivity creata (intent: ${activity.intent?.extras?.keySet()?.joinToString()})")
    }

    private fun endSession() {
        note("Gioco chiuso normalmente")
        sessionMarker.delete()
        val session = sessionFile.takeIf { it.exists() }?.readText().orEmpty()
        Thread {
            saveReport(buildReport(
                title = "Chiusura normale del gioco",
                nativeCrash = "",
                javaCrash = "",
                session = session,
                includeExitReasons = false,
            ))
        }.start()
    }

    private fun installJavaCrashHandler() {
        val previousHandler = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, throwable ->
            try {
                val writer = StringWriter()
                throwable.printStackTrace(PrintWriter(writer))
                javaCrashFile.writeText("Thread: ${thread.name}\n$writer")
                note("Eccezione non gestita nel thread ${thread.name}: $throwable")
            } catch (_: Throwable) {
            }
            previousHandler?.uncaughtException(thread, throwable)
        }
    }

    private fun saveReport(text: String) {
        try {
            // Write to a temporary file first so a half-written report never replaces the previous one
            val file = lastReportFile(application)
            val temporary = File(file.parentFile, "last_report.tmp")
            temporary.writeText(text)
            if (!temporary.renameTo(file)) {
                file.writeText(text)
                temporary.delete()
            }
        } catch (e: Exception) {
            Log.e(TAG, "Impossibile salvare il log", e)
        }
    }

    private fun buildReport(title: String, nativeCrash: String, javaCrash: String, session: String, includeExitReasons: Boolean): String {
        return buildString {
            appendLine("=== melonDS Lua - log ===")
            appendLine(title)
            appendLine("Data: ${dateTime()}")
            appendLine("Dispositivo: ${Build.MANUFACTURER} ${Build.MODEL} (${Build.DEVICE}), Android ${Build.VERSION.RELEASE} (SDK ${Build.VERSION.SDK_INT}), ABI ${Build.SUPPORTED_ABIS.joinToString()}")
            appendLine("App: ${appVersion()}")
            appendLine("Impostazioni: ${relevantSettings()}")
            appendLine()

            if (nativeCrash.isNotBlank()) {
                appendLine("--- Crash nativo ---")
                appendLine(nativeCrash.trim())
                appendLine()
            }
            if (javaCrash.isNotBlank()) {
                appendLine("--- Crash Java/Kotlin ---")
                appendLine(javaCrash.trim())
                appendLine()
            }
            if (includeExitReasons) {
                appendLine("--- Motivo della chiusura secondo Android ---")
                appendLine(exitReasons())
                appendLine()
            }

            appendLine("--- Sessione ---")
            appendLine(session.trim().ifEmpty { "(vuota)" })
            appendLine()

            appendLine("--- Logcat (ultime righe dell'app) ---")
            appendLine(logcat())
        }
    }

    private fun appVersion(): String {
        return try {
            val info = application.packageManager.getPackageInfo(application.packageName, 0)
            "${application.packageName} ${info.versionName}"
        } catch (e: Exception) {
            application.packageName
        }
    }

    private fun relevantSettings(): String {
        return try {
            PreferenceManager.getDefaultSharedPreferences(application).all
                .filterKeys { key -> listOf("video", "renderer", "fast_forward", "jit", "frame", "rewind").any { key.contains(it) } }
                .toSortedMap()
                .entries
                .joinToString { "${it.key}=${it.value}" }
        } catch (e: Exception) {
            "(non disponibili: $e)"
        }
    }

    private fun exitReasons(): String {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
            return "(non disponibile su questa versione di Android)"
        }

        return try {
            val activityManager = application.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
            val reasons = activityManager.getHistoricalProcessExitReasons(application.packageName, 0, 3)
            if (reasons.isEmpty()) {
                "(nessuna informazione)"
            } else {
                reasons.joinToString("\n") { info ->
                    "${dateTime(info.timestamp)}: ${exitReasonName(info.reason)}, stato ${info.status}, importanza ${info.importance}, " +
                        "descrizione: ${info.description ?: "-"}, PSS ${info.pss / 1024} MB, RSS ${info.rss / 1024} MB"
                }
            }
        } catch (e: Exception) {
            "(errore: $e)"
        }
    }

    private fun exitReasonName(reason: Int): String {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
            return reason.toString()
        }

        return when (reason) {
            ApplicationExitInfo.REASON_CRASH -> "CRASH (Java/Kotlin)"
            ApplicationExitInfo.REASON_CRASH_NATIVE -> "CRASH_NATIVE (codice C++)"
            ApplicationExitInfo.REASON_ANR -> "ANR (app bloccata)"
            ApplicationExitInfo.REASON_LOW_MEMORY -> "LOW_MEMORY (memoria esaurita)"
            ApplicationExitInfo.REASON_SIGNALED -> "SIGNALED"
            ApplicationExitInfo.REASON_EXIT_SELF -> "EXIT_SELF"
            ApplicationExitInfo.REASON_USER_REQUESTED -> "USER_REQUESTED"
            ApplicationExitInfo.REASON_USER_STOPPED -> "USER_STOPPED"
            ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE -> "EXCESSIVE_RESOURCE_USAGE"
            ApplicationExitInfo.REASON_INITIALIZATION_FAILURE -> "INITIALIZATION_FAILURE"
            ApplicationExitInfo.REASON_PERMISSION_CHANGE -> "PERMISSION_CHANGE"
            ApplicationExitInfo.REASON_DEPENDENCY_DIED -> "DEPENDENCY_DIED"
            ApplicationExitInfo.REASON_OTHER -> "OTHER"
            else -> "codice $reason"
        }
    }

    private fun logcat(): String {
        return try {
            val process = Runtime.getRuntime().exec(arrayOf("logcat", "-d", "-v", "threadtime", "-t", "3000"))
            val lines = process.inputStream.bufferedReader().readLines()
            process.waitFor()
            lines.takeLast(MAX_LOGCAT_LINES).joinToString("\n").ifEmpty { "(vuoto)" }
        } catch (e: Exception) {
            "(errore nella lettura del logcat: $e)"
        }
    }

    private val lifecycleCallbacks = object : Application.ActivityLifecycleCallbacks {
        override fun onActivityCreated(activity: Activity, savedInstanceState: Bundle?) {
            if (activity is EmulatorActivity) {
                if (savedInstanceState == null) {
                    startSession(activity)
                } else {
                    note("EmulatorActivity ricreata (stato salvato presente)")
                }
            }
        }

        override fun onActivityResumed(activity: Activity) {
            if (activity is EmulatorActivity) {
                note("EmulatorActivity in primo piano")
            }
        }

        override fun onActivityPaused(activity: Activity) {
            if (activity is EmulatorActivity) {
                note("EmulatorActivity in pausa")
            }
        }

        override fun onActivityStopped(activity: Activity) {
            if (activity is EmulatorActivity) {
                note("EmulatorActivity in background")
            }
        }

        override fun onActivityDestroyed(activity: Activity) {
            if (activity is EmulatorActivity) {
                if (activity.isFinishing) {
                    endSession()
                } else {
                    note("EmulatorActivity distrutta senza chiusura (rotazione o sistema)")
                }
            }
        }

        override fun onActivityStarted(activity: Activity) {}
        override fun onActivitySaveInstanceState(activity: Activity, outState: Bundle) {}
    }

    private fun readAndDelete(file: File): String {
        if (!file.exists()) {
            return ""
        }
        val text = try { file.readText() } catch (_: Exception) { "" }
        file.delete()
        return text
    }

    private fun timestamp() = SimpleDateFormat("HH:mm:ss.SSS", Locale.US).format(Date())

    private fun dateTime(time: Long = System.currentTimeMillis()) = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(Date(time))
}
