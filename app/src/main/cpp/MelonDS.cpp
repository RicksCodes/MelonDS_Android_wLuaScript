#include <cstring>
#include <utility>
#include <android/asset_manager.h>
#include <oboe/Oboe.h>
#include "EmulatorArgsBuilder.h"
#include "MelonDS.h"
#include "MelonDSAudio.h"
#include "OboeCallback.h"
#include "MicInputOboeCallback.h"
#include "OpenGLContext.h"
#include "mic_blow.h"
#include "NDS.h"
#include "GPU.h"
#include "GPU3D.h"
#include "GBACart.h"
#include "SPU.h"
#include "Platform.h"
#include "Savestate.h"
#include "MelonInstance.h"
#include "RewindManager.h"
#include "ROMManager.h"
#include "MPInterface.h"
#include "AndroidCameraHandler.h"
#include "renderer/ScreenshotRenderer.h"
#include "renderer/FrameQueue.h"
#include "retroachievements/RetroAchievementsManager.h"
#include "net/Net.h"
#include "net/Net_Slirp.h"
#include <fstream>

namespace MelonDSAndroid
{
    OpenGLContext *openGlContext;
    AndroidFileHandler* fileHandler;
    AndroidCameraHandler* cameraHandler;
    std::string internalFilesDir;
    std::shared_ptr<MelonEventMessenger> eventMessenger;
    std::shared_ptr<EmulatorConfiguration> currentConfiguration;
    std::shared_ptr<Net> net;

    std::shared_ptr<MelonInstance> instance;

    // Multi-instance. The second game is created and destroyed while the main one runs, so it is always accessed
    // atomically
    std::shared_ptr<MelonInstance> secondInstance;
    OpenGLContext* secondGlContext = nullptr;
    std::atomic_int activeInstanceIndex = 0;
    u32* screenshotBuffer = nullptr;

    std::shared_ptr<MelonInstance> getSecondInstance()
    {
        return std::atomic_load(&secondInstance);
    }

    // The game on screen, which receives the input
    std::shared_ptr<MelonInstance> getActiveInstance()
    {
        if (activeInstanceIndex == 1)
        {
            if (auto second = getSecondInstance())
                return second;
        }
        return instance;
    }

    bool setupOpenGlContext();
    void cleanupOpenGlContext();

    /**
     * Used to set the emulator's initial configuration, before boot. To update the configuration during runtime, use @updateEmulatorConfiguration.
     *
     * @param emulatorConfiguration The emulator configuration during the next emulator run
     */
    void setConfiguration(EmulatorConfiguration emulatorConfiguration) {
        currentConfiguration = std::make_shared<EmulatorConfiguration>(std::move(emulatorConfiguration));
        internalFilesDir = currentConfiguration->internalFilesDir;

        net = std::make_shared<Net>();
        net->SetDriver(std::make_unique<Net_Slirp>([](const u8* data, int len) {
            net->RXEnqueue(data, len);
        }));
    }

    void setup(AndroidCameraHandler* androidCameraHandler, std::shared_ptr<MelonEventMessenger> androidEventMessenger, u32* screenshotBufferPointer, int instanceId)
    {
        cameraHandler = androidCameraHandler;
        eventMessenger = androidEventMessenger;
        RetroAchievements::RetroAchievementsManager::EventMessenger = androidEventMessenger;
        screenshotBuffer = screenshotBufferPointer;
        activeInstanceIndex = 0;

        // Local wireless between instances. With a single game it behaves like no connection at all (as on PC,
        // where it is the default)
        if (MPInterface::GetType() != MPInterface_Local)
            MPInterface::Set(MPInterface_Local);

        auto instanceArgs = BuildArgsFromConfiguration(*currentConfiguration, instanceId);
        if (!instanceArgs.has_value())
        {
            // TODO: Handle this somehow?
            instance = nullptr;
            return;
        }
        instance = std::make_shared<MelonInstance>(
            instanceId,
            currentConfiguration,
            std::move(instanceArgs.value()),
            net,
            std::make_unique<ScreenshotRenderer>(screenshotBufferPointer),
            currentConfiguration->consoleType
        );

        setupAudio(currentConfiguration->audioSettings);
        setAudioActiveInstance(instance);
    }

    void setCodeList(std::list<Cheat> cheats)
    {
        instance->loadCheats(std::move(cheats));
    }

    void setupAchievements(
        std::list<RetroAchievements::RAAchievement> achievements,
        std::list<RetroAchievements::RALeaderboard> leaderboards,
        std::optional<std::string> richPresenceScript
    )
    {
        instance->setupAchievements(std::move(achievements), std::move(leaderboards), std::move(richPresenceScript));
    }

    void unloadRetroAchievementsData()
    {
        instance->unloadRetroAchievementsData();
    }

    std::string getRichPresenceStatus()
    {
        return instance->getRichPresenceStatus();
    }

    std::vector<RetroAchievements::RARuntimeAchievement> getRuntimeAchievements()
    {
        return instance->getRuntimeAchievements();
    }

    /**
     * Used to update the emulator's configuration during runtime. Will only update the configurations that can actually change during runtime without causing issues,
     *
     * @param emulatorConfiguration The new emulator configuration
     */
    void updateEmulatorConfiguration(std::unique_ptr<EmulatorConfiguration> emulatorConfiguration) {
        std::shared_ptr<EmulatorConfiguration> sharedConfig = std::move(emulatorConfiguration);
        instance->updateConfiguration(sharedConfig);
        if (auto second = getSecondInstance())
            second->updateConfiguration(sharedConfig);
        updateAudioSettings(sharedConfig->audioSettings);

        currentConfiguration = sharedConfig;
    }

    int loadRom(std::string romPath, std::string sramPath, RomGbaSlotConfig* gbaSlotConfig)
    {
        if (!instance->loadRom(std::move(romPath), std::move(sramPath)))
            return 2;

        if (gbaSlotConfig->type == GBA_ROM)
        {
            RomGbaSlotConfigGbaRom* gbaRomConfig = (RomGbaSlotConfigGbaRom*) gbaSlotConfig;
            if (!instance->loadGbaRom(gbaRomConfig->romPath, gbaRomConfig->savePath))
                return 1;
        }
        else if (gbaSlotConfig->type == RUMBLE_PAK)
        {
            instance->loadRumblePak();
        }
        else if (gbaSlotConfig->type == MEMORY_EXPANSION)
        {
            instance->loadGbaMemoryExpansion();
        }
        else if (gbaSlotConfig->type == MOTION_PAK_HOMEBREW)
        {
            instance->loadMotionPakHomebrew();
        }
        else if (gbaSlotConfig->type == MOTION_PAK_RETAIL)
        {
            instance->loadMotionPakRetail();
        }

        return 0;
    }

    int bootFirmware()
    {
        // TODO: Maybe validate BIOS and firmware?
        if (instance->bootFirmware())
            return ROMManager::SUCCESS;
        else
            return ROMManager::FIRMWARE_NOT_BOOTABLE;
    }

    void touchScreen(u16 x, u16 y)
    {
        if (auto activeInstance = getActiveInstance())
            activeInstance->touchScreen(x, y);
    }

    void releaseScreen()
    {
        if (auto activeInstance = getActiveInstance())
            activeInstance->releaseScreen();
    }

    void pressKey(u32 key)
    {
        if (auto activeInstance = getActiveInstance())
            activeInstance->pressKey(key);
    }

    void releaseKey(u32 key)
    {
        if (auto activeInstance = getActiveInstance())
            activeInstance->releaseKey(key);
    }

    void updateMotionData(float ax, float ay, float az, float rx, float ry, float rz)
    {
        if (auto activeInstance = getActiveInstance())
            activeInstance->updateMotionData(ax, ay, az, rx, ry, rz);
    }

    void start(int index)
    {
        if (index == 1)
        {
            secondGlContext->Use();
            getSecondInstance()->start();
            return;
        }

        startAudio();
        setupOpenGlContext();

        instance->start();
    }

    u32 loop(int index)
    {
        if (index == 1)
            return getSecondInstance()->runFrame();

        MPInterface::Get().Process();
        return instance->runFrame();
    }

    Frame* getPresentationFrame(std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline)
    {
        auto activeInstance = getActiveInstance();
        if (!activeInstance)
            return nullptr;

        return activeInstance->getPresentationFrame(deadline);
    }

    void pause()
    {
        pauseAudio();
    }

    void resume()
    {
        startAudio();
    }

    void reset()
    {
        instance->reset();
    }

    bool saveState(const char* path)
    {
        Platform::FileHandle* saveStateFile = Platform::OpenFile(path, Platform::FileMode::Write);

        if (!saveStateFile)
            return false;

        Savestate state;
        if (state.Error)
        {
            Platform::CloseFile(saveStateFile);
            return false;
        }

        instance->saveState(&state);

        if (state.Error)
        {
            Platform::CloseFile(saveStateFile);
            return false;
        }

        if (Platform::FileWrite(state.Buffer(), state.Length(), 1, saveStateFile) == 0)
        {
            Platform::Log(Platform::Error, "Failed to write %d-byte savestate to %s\n", state.Length(), path);
            Platform::CloseFile(saveStateFile);
            return false;
        }

        Platform::CloseFile(saveStateFile);
        return true;
    }

    bool loadState(const char* path)
    {
        auto saveStateFile = Platform::OpenFile(path, Platform::FileMode::Read);
        if (!saveStateFile)
        {
            Platform::Log(Platform::LogLevel::Error, "Failed to open state file \"%s\"\n", path);
            return false;
        }

        std::unique_ptr<Savestate> backup = std::make_unique<Savestate>(Savestate::DEFAULT_SIZE);
        if (backup->Error)
        {
            Platform::Log(Platform::LogLevel::Error, "Failed to allocate memory for state backup\n");
            Platform::CloseFile(saveStateFile);
            return false;
        }

        if (!instance->saveState(backup.get()) || backup->Error)
        {
            Platform::Log(Platform::LogLevel::Error, "Failed to back up state, aborting load (from \"%s\")\n", path);
            Platform::CloseFile(saveStateFile);
            return false;
        }

        size_t size = Platform::FileLength(saveStateFile);

        // Allocate exactly as much memory as we need for the savestate
        std::vector<u8> buffer(size);
        if (Platform::FileRead(buffer.data(), size, 1, saveStateFile) == 0)
        {
            Platform::Log(Platform::LogLevel::Error, "Failed to read %u-byte state file \"%s\"\n", size, path);
            Platform::CloseFile(saveStateFile);
            return false;
        }
        Platform::CloseFile(saveStateFile);

        std::unique_ptr<Savestate> state = std::make_unique<Savestate>(buffer.data(), size, false);

        if (!instance->loadState(state.get()) || state->Error)
        {
            Platform::Log(Platform::LogLevel::Error, "Failed to load state file \"%s\" into emulator\n", path);
            // Restore backup
            if (!instance->loadState(backup.get()) || state->Error)
                Platform::Log(Platform::LogLevel::Error, "Failed to load backup state\n", path);
            else
                Platform::Log(Platform::LogLevel::Info, "Backup state loaded\n", path);

            return false;
        }
        return true;
    }

    bool loadRewindState(melonDS::RewindSaveState rewindSaveState)
    {
        std::unique_ptr<Savestate> backup = std::make_unique<Savestate>(Savestate::DEFAULT_SIZE);
        if (backup->Error)
        {
            Platform::Log(Platform::LogLevel::Error, "Failed to allocate memory for state backup");
            return false;
        }

        if (!instance->saveState(backup.get()) || backup->Error)
        {
            Platform::Log(Platform::LogLevel::Error, "Failed to back up state, aborting rewind state load");
            return false;
        }

        bool result = instance->loadRewindState(rewindSaveState);
        if (!result)
        {
            Platform::Log(Platform::LogLevel::Error, "Failed to load rewind state");
            // Restore backup
            if (!instance->loadState(backup.get()) || backup->Error)
                Platform::Log(Platform::LogLevel::Error, "Failed to load backup state");
            else
                Platform::Log(Platform::LogLevel::Info, "Backup state loaded");
        }

        return result;
    }

    RewindWindow getRewindWindow()
    {
        return instance->getRewindWindow();
    }

    bool takeScreenshot()
    {
        if (instance)
            return instance->takeScreenshot();

        return false;
    }

    void setFastForwardFrameSkip(bool enabled)
    {
        auto currentInstance = instance;
        if (currentInstance)
            currentInstance->setFastForwardFrameSkip(enabled);
        if (auto second = getSecondInstance())
            second->setFastForwardFrameSkip(enabled);
    }

    void loadLuaScript(std::string scriptName, std::string source, std::string workDir)
    {
        auto currentInstance = getActiveInstance();
        if (currentInstance)
            currentInstance->getLuaEngine()->requestLoad(std::move(scriptName), std::move(source), std::move(workDir));
    }

    void stopLuaScript()
    {
        auto currentInstance = getActiveInstance();
        if (currentInstance)
            currentInstance->getLuaEngine()->requestStop();
    }

    void setLuaKeyState(const std::string& key, bool pressed)
    {
        auto currentInstance = getActiveInstance();
        if (currentInstance)
            currentInstance->getLuaEngine()->setKeyState(key, pressed);
    }

    std::string pollLuaOutput()
    {
        auto currentInstance = getActiveInstance();
        if (currentInstance)
            return currentInstance->getLuaEngine()->pollOutput();
        return "";
    }

    int getLuaStatus()
    {
        auto currentInstance = getActiveInstance();
        if (currentInstance)
            return static_cast<int>(currentInstance->getLuaEngine()->getStatus());
        return 0;
    }

    void stop(int index)
    {
        if (index == 1)
        {
            auto second = getSecondInstance();
            second->stop();
            // Tell the main game that the other console left, so it doesn't keep waiting for its replies
            MPInterface::Get().End(1);
            setActiveInstance(0);
            std::atomic_store(&secondInstance, std::shared_ptr<MelonInstance>());
            // Destroyed here, while its OpenGL context is still current, so that its textures are freed
            second = nullptr;
            secondGlContext->Release();
            return;
        }

        instance->stop();
        cleanupOpenGlContext();
    }

    void cleanup()
    {
        cleanupAudio();

        instance = nullptr;
        std::atomic_store(&secondInstance, std::shared_ptr<MelonInstance>());
        activeInstanceIndex = 0;
        if (secondGlContext)
        {
            secondGlContext->DeInit(false);
            delete secondGlContext;
            secondGlContext = nullptr;
        }
        eventMessenger = nullptr;
    }

    bool loadSecondInstance(std::string romPath, std::string sramPath)
    {
        if (!instance || getSecondInstance() || !openGlContext)
            return false;

        auto instanceArgs = BuildArgsFromConfiguration(*currentConfiguration, 1);
        if (!instanceArgs.has_value())
            return false;

        if (!secondGlContext)
        {
            secondGlContext = new OpenGLContext();
            // Shared with the main context, so that the frames of both games can be shown by the same view
            if (!secondGlContext->InitContext((long) openGlContext->GetContext()))
            {
                delete secondGlContext;
                secondGlContext = nullptr;
                return false;
            }
        }

        auto newInstance = std::make_shared<MelonInstance>(
            1,
            currentConfiguration,
            std::move(instanceArgs.value()),
            net,
            std::make_unique<ScreenshotRenderer>(screenshotBuffer),
            currentConfiguration->consoleType
        );
        if (!newInstance->loadRom(std::move(romPath), std::move(sramPath)))
            return false;

        newInstance->setHidden(true);
        newInstance->setFastForwardFrameSkip(instance->isFastForwardFrameSkipEnabled());
        std::atomic_store(&secondInstance, newInstance);
        return true;
    }

    void setActiveInstance(int index)
    {
        auto second = getSecondInstance();
        if (!second)
            index = 0;
        if (index == activeInstanceIndex)
            return;

        auto previous = getActiveInstance();
        activeInstanceIndex = index;
        auto current = getActiveInstance();

        previous->releaseAllInput();
        previous->setHidden(true);
        current->setHidden(false);
        setAudioActiveInstance(current);
    }

    int getActiveInstanceIndex()
    {
        return activeInstanceIndex;
    }

    bool setupOpenGlContext()
    {
        if (openGlContext == nullptr)
            return false;

        if (!openGlContext->Use())
        {
            Platform::Log(Platform::LogLevel::Error, "Failed to use OpenGL context");
            return false;
        }

        return true;
    }

    void cleanupOpenGlContext()
    {
        if (openGlContext == nullptr)
            return;

        openGlContext->Release();
    }
}

