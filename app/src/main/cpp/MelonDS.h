#ifndef MELONDS_MELONDS_H
#define MELONDS_MELONDS_H

#include <list>
#include <vector>
#include "AndroidFileHandler.h"
#include "AndroidCameraHandler.h"
#include "Configuration.h"
#include "MelonEventMessenger.h"
#include "RewindManager.h"
#include "RomGbaSlotConfig.h"
#include "retroachievements/RAAchievement.h"
#include "retroachievements/RALeaderboard.h"
#include "renderer/FrameQueue.h"
#include "types.h"
#include "../GPU.h"
#include <android/asset_manager.h>

using namespace melonDS;

namespace MelonDSAndroid {
    typedef struct {
        std::vector<u32> code;
    } Cheat;

    typedef enum {
        ROM,
        FIRMWARE
    } RunMode;

    extern OpenGLContext *openGlContext;
    extern AndroidFileHandler* fileHandler;
    extern AndroidCameraHandler* cameraHandler;
    extern std::string internalFilesDir;
    extern std::shared_ptr<MelonEventMessenger> eventMessenger;

    extern void setConfiguration(EmulatorConfiguration emulatorConfiguration);
    extern void setup(AndroidCameraHandler* androidCameraHandler, std::shared_ptr<MelonEventMessenger> androidEventMessenger, u32* screenshotBufferPointer, int instanceId);
    extern void setCodeList(std::list<Cheat> cheats);
    extern void setupAchievements(std::list<RetroAchievements::RAAchievement> achievements, std::list<RetroAchievements::RALeaderboard> leaderboards, std::optional<std::string> richPresenceScript);
    extern void unloadRetroAchievementsData();
    extern std::string getRichPresenceStatus();
    extern std::vector<RetroAchievements::RARuntimeAchievement> getRuntimeAchievements();
    extern void updateEmulatorConfiguration(std::unique_ptr<EmulatorConfiguration> emulatorConfiguration);

    /**
     * Loads the NDS ROM and, optionally, the GBA ROM.
     *
     * @param romPath The path to the NDS rom
     * @param sramPath The path to the rom's SRAM file
     * @param gbaSlotConfig The config to be used for the GBA slot
     * @return The load result. 0 if everything was loaded successfully, 1 if the NDS ROM was loaded but the GBA ROM
     * failed to load, 2 if the NDS ROM failed to load
     */
    extern int loadRom(std::string romPath, std::string sramPath, RomGbaSlotConfig* gbaSlotConfig);
    extern int bootFirmware();
    extern void touchScreen(u16 x, u16 y);
    extern void releaseScreen();
    extern void pressKey(u32 key);
    extern void releaseKey(u32 key);
    extern void updateMotionData(float ax, float ay, float az, float rx, float ry, float rz);
    // index: 0 = main game, 1 = second game (multi-instance). Called on that instance's emulator thread.
    extern void start(int index);
    extern u32 loop(int index);
    extern Frame* getPresentationFrame(std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline);
    extern void pause();
    extern void resume();
    extern void reset();
    extern bool saveState(const char* path);
    extern bool loadState(const char* path);
    extern bool loadRewindState(melonDS::RewindSaveState rewindSaveState);
    extern RewindWindow getRewindWindow();
    extern bool takeScreenshot();

    /**
     * Enables or disables frameskip. Must only be enabled while fast-forwarding: frames are skipped so that roughly
     * 60 frames per second are displayed, and the rendering of the skipped ones is not done at all.
     */
    extern void setFastForwardFrameSkip(bool enabled);

    // Lua scripting (DeSmuME compatible). Safe to call from any thread.
    extern void loadLuaScript(std::string scriptName, std::string source, std::string workDir);
    extern void stopLuaScript();
    extern void setLuaKeyState(const std::string& key, bool pressed);
    extern std::string pollLuaOutput();
    extern int getLuaStatus();
    extern void stop(int index);
    extern void cleanup();

    /**
     * Multi-instance, like melonDS on PC: a second game runs next to the main one and the two talk through local
     * wireless (trades, battles, Download Play). Only one game is shown at a time: input, video, audio and Lua go to
     * the visible one. Save states, rewind, cheats and achievements always belong to the main game.
     *
     * @return true if the ROM was loaded. The second game is then started on its own thread with start(1)
     */
    extern bool loadSecondInstance(std::string romPath, std::string sramPath);
    extern void setActiveInstance(int index);
    extern int getActiveInstanceIndex();
}

#endif //MELONDS_MELONDS_H
