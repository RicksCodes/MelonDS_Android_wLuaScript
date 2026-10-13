#ifndef MELONDS_LUA_ENGINE_H
#define MELONDS_LUA_ENGINE_H

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include "LuaCanvas.h"
#include "LuaHost.h"

struct lua_State;

namespace MelonDSAndroid::Lua
{

/**
 * Runs a Lua 5.1 script with an API compatible with the most used parts of DeSmuME's Lua engine
 * (memory, bit, gui, input, joypad, stylus, emu). Scripts written for DeSmuME, like the Pokémon RNG overlays, run unchanged.
 *
 * Threading: requestLoad, requestStop, setKeyState, setTouchState, pollOutput, getStatus and getScriptName may be
 * called from any thread. Everything else must be called from the emulator thread, which is the only thread that ever
 * touches the Lua state.
 */
class LuaEngine
{
public:
    enum class Status : int
    {
        Idle = 0,
        Running = 1,
        Error = 2,
    };

    explicit LuaEngine(LuaHost* host);
    ~LuaEngine();

    // --- Any thread ---

    /**
     * Requests a script to be loaded at the next frame boundary, replacing the current one.
     *
     * @param scriptName The script name, used in error messages
     * @param source The script source code
     * @param workDir Writable directory used to resolve relative paths in io.open, os.execute("mkdir ..."), require...
     */
    void requestLoad(std::string scriptName, std::string source, std::string workDir);
    void requestStop();
    void setKeyState(const std::string& key, bool pressed);
    void setTouchState(bool touching, int x, int y);

    /**
     * @return Console output (print, errors) produced since the previous call
     */
    std::string pollOutput();
    Status getStatus() const { return status.load(); }
    std::string getScriptName();

    // --- Emulator thread ---

    /**
     * Applies pending load/stop requests and runs emu.registerbefore callbacks.
     */
    void onFrameStart();

    /**
     * Runs emu.registerafter callbacks, resumes the main script coroutine (emu.frameadvance) and the gui.register
     * callback. The overlay canvas holds what must be drawn on top of this frame afterwards.
     */
    void onFrameEnd();

    /**
     * Called when the emulator is shutting down. Runs emu.registerexit and closes the script.
     */
    void shutdown();

    const LuaCanvas& getCanvas() const { return canvas; }

    // Used by the Lua bindings (they receive the engine through an upvalue)
    LuaHost* getHost() { return host; }
    LuaCanvas& getMutableCanvas() { return canvas; }
    lua_State* getMainCoroutine() { return mainCoroutine; }
    int getFrameCount() const { return frameCount; }
    float getOpacity() const { return opacity; }
    void setOpacity(float newOpacity) { opacity = newOpacity; }
    const std::string& getWorkDir() const { return workDir; }
    void appendOutput(const std::string& text);
    void pushInputTable(lua_State* L);
    void requestStopFromScript() { stopFromScript = true; }

    /**
     * Input requested by the script with joypad.set / stylus.set. Like in DeSmuME, it applies to one frame only: the
     * frame that starts right after the call (or the current one, when called from emu.registerbefore). Scripts that
     * want to hold a button call joypad.set every frame.
     */
    struct InputOverride
    {
        // melonDS key order (0 A, 1 B, 2 Select, 3 Start, 4 Right, 5 Left, 6 Up, 7 Down, 8 R, 9 L, 10 X, 11 Y)
        uint32_t pressMask = 0;     // Keys forced down
        uint32_t releaseMask = 0;   // Keys forced up, even if the user is holding them
        bool touchSet = false;      // stylus.set was called
        bool touchDown = false;
        int touchX = 0;
        int touchY = 0;
    };

    // Used by the joypad / stylus bindings
    void setJoypadOverride(uint32_t pressMask, uint32_t releaseMask);
    void setStylusOverride(bool down, int x, int y);
    void getUserTouch(bool& isTouching, int& x, int& y);

    /**
     * Emulator thread, after onFrameStart(): returns the input to apply to the frame that is about to run and clears it.
     */
    InputOverride takeInputOverride();

    enum CallbackSlot
    {
        CallbackBefore = 0,
        CallbackAfter,
        CallbackGui,
        CallbackExit,
        CallbackCount,
    };
    void setCallback(lua_State* L, CallbackSlot slot, int stackIndex);

private:
    struct LoadRequest
    {
        std::string name;
        std::string source;
        std::string workDir;
    };

    struct KeyState
    {
        bool seen = false;
        bool releasePending = false;
        int framesSinceRelease = 0;
    };

    void load(LoadRequest request);
    void close();
    void fail(const std::string& message);
    bool runCallback(CallbackSlot slot);
    void resumeMainCoroutine();
    bool hasCallbacks() const;
    void releaseSeenKeys();

    LuaHost* host;
    LuaCanvas canvas;

    lua_State* L = nullptr;
    lua_State* mainCoroutine = nullptr;
    int mainCoroutineRef = -2;
    int callbackRefs[CallbackCount];
    bool mainCoroutineFinished = true;
    bool stopFromScript = false;
    int frameCount = 0;
    float opacity = 1.0f;
    std::string workDir;

    std::mutex requestMutex;
    std::optional<LoadRequest> pendingLoad;
    bool pendingStop = false;

    std::mutex outputMutex;
    std::string output;
    std::string scriptName;

    std::mutex inputMutex;
    std::map<std::string, KeyState> pressedKeys;
    bool touching = false;
    int touchX = 0;
    int touchY = 0;

    // Emulator thread only
    InputOverride pendingInput;

    std::atomic<Status> status;
};

}

#endif
