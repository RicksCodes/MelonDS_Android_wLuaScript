package me.magnum.melonds

import android.net.Uri
import me.magnum.melonds.common.camera.DSiCameraSource
import me.magnum.melonds.domain.model.Cheat
import me.magnum.melonds.domain.model.EmulatorConfiguration
import me.magnum.melonds.domain.model.Input
import me.magnum.melonds.domain.model.retroachievements.RASimpleAchievement
import me.magnum.melonds.domain.model.retroachievements.RASimpleLeaderboard
import me.magnum.melonds.domain.model.retroachievements.RASimpleRuntimeAchievement
import me.magnum.melonds.ui.emulator.render.FrameRenderCallback
import me.magnum.melonds.ui.emulator.rewind.model.RewindSaveState
import me.magnum.melonds.ui.emulator.rewind.model.RewindWindow
import java.nio.ByteBuffer

object MelonEmulator {
    enum class LoadResult(val isTerminal: Boolean) {
        SUCCESS(false),
        SUCCESS_GBA_FAILED(false),
        NDS_FAILED(true),
        BIOS_FAILED(true)
    }

    enum class FirmwareLoadResult {
        SUCCESS,
        BIOS9_MISSING,
        BIOS9_BAD,
        BIOS7_MISSING,
        BIOS7_BAD,
        FIRMWARE_MISSING,
        FIRMWARE_BAD,
        FIRMWARE_NOT_BOOTABLE,
        DSI_BIOS9_MISSING,
        DSI_BIOS9_BAD,
        DSI_BIOS7_MISSING,
        DSI_BIOS7_BAD,
        DSI_NAND_MISSING,
        DSI_NAND_BAD
    }

    enum class GbaSlotType {
        NONE,
        GBA_ROM,
        RUMBLE_PAK,
        MEMORY_EXPANSION,
        MOTION_PAK_HOMEBREW,
        MOTION_PAK_RETAIL,
    }

	external fun setupEmulator(
        emulatorConfiguration: EmulatorConfiguration,
        dsiCameraSource: DSiCameraSource?,
        screenshotBuffer: ByteBuffer,
    )

    external fun setupCheats(cheats: Array<Cheat>)

    external fun setupAchievements(achievements: Array<RASimpleAchievement>, leaderboards: Array<RASimpleLeaderboard>, richPresenceScript: String?)

    external fun unloadRetroAchievementsData()

    external fun getRichPresenceStatus(): String?

    external fun getRuntimeAchievements(): Array<RASimpleRuntimeAchievement>

	fun loadRom(romUri: Uri, sramUri: Uri, gbaSlotType: GbaSlotType, gbaRomUri: Uri?, gbaSramUri: Uri?): LoadResult {
        val loadResult = loadRomInternal(romUri.toString(), sramUri.toString(), gbaSlotType.ordinal, gbaRomUri?.toString(), gbaSramUri?.toString())
        return when (loadResult) {
            0 -> LoadResult.SUCCESS
            1 -> LoadResult.SUCCESS_GBA_FAILED
            2 -> LoadResult.NDS_FAILED
            3 -> LoadResult.BIOS_FAILED
            else -> throw RuntimeException("Unknown load result")
        }
    }

    fun bootFirmware(): FirmwareLoadResult {
        val loadResult = bootFirmwareInternal()
        return FirmwareLoadResult.entries[loadResult]
    }

    private external fun loadRomInternal(romPath: String, sramPath: String, gbaSlotType: Int, gbaRomPath: String?, gbaSramPath: String?): Int

    private external fun bootFirmwareInternal(): Int

	external fun startEmulation()

    external fun presentFrame(deadlineNs: Long, frameRenderCallback: FrameRenderCallback)

	external fun getFPS(): Float

	external fun pauseEmulation()

	external fun resumeEmulation()

    external fun resetEmulation()

	external fun stopEmulation()

    fun saveState(path: Uri): Boolean {
        return saveStateInternal(path.toString())
    }

    private external fun saveStateInternal(path: String): Boolean

    fun loadState(path: Uri): Boolean {
        return loadStateInternal(path.toString())
    }

    private external fun loadStateInternal(path: String): Boolean

    external fun loadRewindState(rewindSaveState: RewindSaveState): Boolean

    external fun getRewindWindow(): RewindWindow

	external fun onScreenTouch(x: Int, y: Int)

	external fun onScreenRelease()

	fun onInputDown(input: Input) {
        onKeyPress(input.keyCode)
    }

	fun onInputUp(input: Input) {
        onKeyRelease(input.keyCode)
    }

    private external fun onKeyPress(key: Int)

    private external fun onKeyRelease(key: Int)

    external fun takeScreenshot(): Boolean

    external fun setFastForwardEnabled(enabled: Boolean)

    external fun setMicrophoneEnabled(enabled: Boolean)

    external fun updateEmulatorConfiguration(emulatorConfiguration: EmulatorConfiguration)

    external fun updateMotionData(ax: Float, ay: Float, az: Float, rx: Float, ry: Float, rz: Float)

    // Lua scripting (DeSmuME compatible)

    /**
     * Loads a Lua script, replacing the current one. Takes effect at the next emulated frame.
     *
     * @param scriptName Name shown in errors
     * @param source Raw UTF-8 bytes of the script
     * @param workDir Writable directory used to resolve the script's relative paths
     */
    external fun loadLuaScript(scriptName: String, source: ByteArray, workDir: String)

    external fun stopLuaScript()

    /**
     * Updates the state of a key as seen by the script's input.get() (DeSmuME key names: "1", "A", "F1", "shift"...)
     */
    external fun setLuaKeyState(key: String, pressed: Boolean)

    /**
     * @return The script output (print, errors) produced since the last call, as UTF-8 bytes
     */
    external fun pollLuaOutput(): ByteArray

    /**
     * @return 0 = no script, 1 = running, 2 = stopped because of an error
     */
    external fun getLuaStatus(): Int

    /**
     * Opacity (0..1) of everything Lua scripts draw on screen.
     */
    external fun setLuaOverlayOpacity(opacity: Float)

    // Multi-instance: a second game that talks to the main one through local wireless, like melonDS on PC

    /**
     * Loads [romUri] in a second emulated console and starts it, hidden. Only one second game can run at a time.
     */
    fun startSecondInstance(romUri: Uri, sramUri: Uri): Boolean {
        return startSecondInstanceInternal(romUri.toString(), sramUri.toString())
    }

    private external fun startSecondInstanceInternal(romPath: String, sramPath: String): Boolean

    external fun stopSecondInstance()

    /**
     * Selects the game that is shown and receives the input, the audio and the Lua calls: 0 = main game, 1 = second game.
     */
    external fun setActiveInstance(index: Int)

    /**
     * Installs native signal handlers that write a crash report to [reportPath] (diagnostics for the Lua build).
     */
    external fun installCrashHandler(reportPath: String)
}