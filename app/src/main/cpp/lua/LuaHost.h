#ifndef MELONDS_LUA_HOST_H
#define MELONDS_LUA_HOST_H

#include <cstdint>

namespace MelonDSAndroid::Lua
{

/**
 * Bridge between the Lua engine and the emulator. All methods are called from the emulator thread.
 */
class LuaHost
{
public:
    virtual ~LuaHost() = default;

    // ARM9 view of memory (ITCM, DTCM and main RAM). Unmapped addresses read as 0 and ignore writes.
    virtual uint8_t read8(uint32_t address) = 0;
    virtual uint16_t read16(uint32_t address) = 0;
    virtual uint32_t read32(uint32_t address) = 0;
    virtual void write8(uint32_t address, uint8_t value) = 0;
    virtual void write16(uint32_t address, uint16_t value) = 0;
    virtual void write32(uint32_t address, uint32_t value) = 0;

    /**
     * @return Bitmask of the DS buttons currently held, using melonDS key order
     * (0 A, 1 B, 2 Select, 3 Start, 4 Right, 5 Left, 6 Up, 7 Down, 8 R, 9 L, 10 X, 11 Y). A set bit means "pressed".
     */
    virtual uint32_t getPressedButtons() = 0;

    /**
     * Requests a console reset. The host performs it before the next frame.
     */
    virtual void requestReset() = 0;
};

}

#endif
