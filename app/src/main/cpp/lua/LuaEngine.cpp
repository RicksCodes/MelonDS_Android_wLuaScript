#include "LuaEngine.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <sys/stat.h>
#include <vector>

#include "lua51/lua.h"
#include "lua51/lauxlib.h"
#include "lua51/lualib.h"
#include "../debug/CrashHandler.h"

namespace MelonDSAndroid::Lua
{

namespace
{

// Address used as a unique registry key for the engine pointer
char kEngineRegistryKey = 0;

constexpr size_t kMaxOutputSize = 64 * 1024;
constexpr const char* kExitSentinel = "__melonds_lua_exit__";

constexpr uint32_t kDefaultTextColor = 0xFFFFFFFF;
constexpr uint32_t kDefaultTextOutline = 0x000000FF;
constexpr uint32_t kDefaultBoxFill = 0xFFFFFF3F;
constexpr uint32_t kDefaultLineColor = 0xFFFFFFFF;

// Relative paths are resolved against the script's working directory, so that scripts written for desktop
// emulators (which use the current directory) keep working.
const char* kPrelude = R"LUA(
local base = ...
local function resolve(p)
  if type(p) == "string" and p:sub(1, 1) ~= "/" then
    return base .. "/" .. p
  end
  return p
end

local io_open, io_lines, io_input, io_output = io.open, io.lines, io.input, io.output
io.open = function(p, m) return io_open(resolve(p), m) end
io.lines = function(p, ...)
  if p == nil then return io_lines() end
  return io_lines(resolve(p), ...)
end
io.input = function(f)
  if type(f) == "string" then f = resolve(f) end
  return io_input(f)
end
io.output = function(f)
  if type(f) == "string" then f = resolve(f) end
  return io_output(f)
end

local os_remove, os_rename = os.remove, os.rename
os.remove = function(p) return os_remove(resolve(p)) end
os.rename = function(a, b) return os_rename(resolve(a), resolve(b)) end

local lf, df = loadfile, dofile
loadfile = function(p)
  if p == nil then return lf() end
  return lf(resolve(p))
end
dofile = function(p)
  if p == nil then return df() end
  return df(resolve(p))
end

package.path = base .. "/?.lua;" .. base .. "/?/init.lua"
package.cpath = ""
)LUA";

LuaEngine* getEngine(lua_State* L)
{
    lua_pushlightuserdata(L, &kEngineRegistryKey);
    lua_rawget(L, LUA_REGISTRYINDEX);
    auto engine = static_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return engine;
}

// --- Number helpers (Lua BitOp semantics: wrap around modulo 2^32) ---

uint32_t numberToU32(lua_Number n)
{
    if (!std::isfinite(n))
        return 0;

    double rounded = std::nearbyint(n);
    double wrapped = std::fmod(rounded, 4294967296.0);
    if (wrapped < 0)
        wrapped += 4294967296.0;

    return (uint32_t) wrapped;
}

uint32_t checkU32(lua_State* L, int index)
{
    return numberToU32(luaL_checknumber(L, index));
}

int checkInt(lua_State* L, int index)
{
    lua_Number n = luaL_checknumber(L, index);
    if (!std::isfinite(n))
        return 0;

    n = std::floor(n);
    if (n > 1e9) return 1000000000;
    if (n < -1e9) return -1000000000;
    return (int) n;
}

void pushBits(lua_State* L, uint32_t value)
{
    lua_pushnumber(L, (lua_Number) (int32_t) value);
}

void pushUnsigned(lua_State* L, uint32_t value)
{
    lua_pushnumber(L, (lua_Number) value);
}

// --- bit library (compatible with Lua BitOp, which DeSmuME embeds) ---

int bitTobit(lua_State* L) { pushBits(L, checkU32(L, 1)); return 1; }
int bitBnot(lua_State* L) { pushBits(L, ~checkU32(L, 1)); return 1; }

int bitBand(lua_State* L)
{
    int n = lua_gettop(L);
    uint32_t result = checkU32(L, 1);
    for (int i = 2; i <= n; ++i)
        result &= checkU32(L, i);
    pushBits(L, result);
    return 1;
}

int bitBor(lua_State* L)
{
    int n = lua_gettop(L);
    uint32_t result = checkU32(L, 1);
    for (int i = 2; i <= n; ++i)
        result |= checkU32(L, i);
    pushBits(L, result);
    return 1;
}

int bitBxor(lua_State* L)
{
    int n = lua_gettop(L);
    uint32_t result = checkU32(L, 1);
    for (int i = 2; i <= n; ++i)
        result ^= checkU32(L, i);
    pushBits(L, result);
    return 1;
}

int bitLshift(lua_State* L) { pushBits(L, checkU32(L, 1) << (checkU32(L, 2) & 31)); return 1; }
int bitRshift(lua_State* L) { pushBits(L, checkU32(L, 1) >> (checkU32(L, 2) & 31)); return 1; }
int bitArshift(lua_State* L) { pushBits(L, (uint32_t) ((int32_t) checkU32(L, 1) >> (checkU32(L, 2) & 31))); return 1; }

int bitRol(lua_State* L)
{
    uint32_t x = checkU32(L, 1);
    uint32_t n = checkU32(L, 2) & 31;
    pushBits(L, n == 0 ? x : (x << n) | (x >> (32 - n)));
    return 1;
}

int bitRor(lua_State* L)
{
    uint32_t x = checkU32(L, 1);
    uint32_t n = checkU32(L, 2) & 31;
    pushBits(L, n == 0 ? x : (x >> n) | (x << (32 - n)));
    return 1;
}

int bitBswap(lua_State* L)
{
    uint32_t x = checkU32(L, 1);
    x = (x >> 24) | ((x >> 8) & 0xFF00) | ((x & 0xFF00) << 8) | (x << 24);
    pushBits(L, x);
    return 1;
}

int bitTohex(lua_State* L)
{
    uint32_t x = checkU32(L, 1);
    int n = lua_isnoneornil(L, 2) ? 8 : checkInt(L, 2);
    const char* digits = "0123456789abcdef";
    if (n < 0)
    {
        n = -n;
        digits = "0123456789ABCDEF";
    }
    if (n > 8) n = 8;

    char buffer[9];
    for (int i = n - 1; i >= 0; --i)
    {
        buffer[i] = digits[x & 15];
        x >>= 4;
    }
    lua_pushlstring(L, buffer, n);
    return 1;
}

// FCEUX-style globals that some DeSmuME scripts still use
int globalShift(lua_State* L)
{
    uint32_t x = checkU32(L, 1);
    int n = checkInt(L, 2);
    if (n >= 32 || n <= -32) pushUnsigned(L, 0);
    else if (n >= 0) pushUnsigned(L, x >> n);
    else pushUnsigned(L, x << (-n));
    return 1;
}

int globalBit(lua_State* L)
{
    int n = checkInt(L, 1);
    pushUnsigned(L, (n >= 0 && n < 32) ? (1u << n) : 0);
    return 1;
}

int globalAnd(lua_State* L) { bitBand(L); lua_pushnumber(L, (lua_Number) numberToU32(lua_tonumber(L, -1))); return 1; }
int globalOr(lua_State* L) { bitBor(L); lua_pushnumber(L, (lua_Number) numberToU32(lua_tonumber(L, -1))); return 1; }
int globalXor(lua_State* L) { bitBxor(L); lua_pushnumber(L, (lua_Number) numberToU32(lua_tonumber(L, -1))); return 1; }

// --- memory library ---

int memReadByte(lua_State* L) { pushUnsigned(L, getEngine(L)->getHost()->read8(checkU32(L, 1))); return 1; }
int memReadByteSigned(lua_State* L) { lua_pushnumber(L, (int8_t) getEngine(L)->getHost()->read8(checkU32(L, 1))); return 1; }
int memReadWord(lua_State* L) { pushUnsigned(L, getEngine(L)->getHost()->read16(checkU32(L, 1))); return 1; }
int memReadWordSigned(lua_State* L) { lua_pushnumber(L, (int16_t) getEngine(L)->getHost()->read16(checkU32(L, 1))); return 1; }
int memReadDword(lua_State* L) { pushUnsigned(L, getEngine(L)->getHost()->read32(checkU32(L, 1))); return 1; }
int memReadDwordSigned(lua_State* L) { lua_pushnumber(L, (int32_t) getEngine(L)->getHost()->read32(checkU32(L, 1))); return 1; }

int memReadByteRange(lua_State* L)
{
    uint32_t address = checkU32(L, 1);
    int length = checkInt(L, 2);
    length = std::clamp(length, 0, 1024 * 1024);

    LuaHost* host = getEngine(L)->getHost();
    lua_createtable(L, length, 0);
    for (int i = 0; i < length; ++i)
    {
        lua_pushnumber(L, host->read8(address + i));
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

int memWriteByte(lua_State* L) { getEngine(L)->getHost()->write8(checkU32(L, 1), (uint8_t) checkU32(L, 2)); return 0; }
int memWriteWord(lua_State* L) { getEngine(L)->getHost()->write16(checkU32(L, 1), (uint16_t) checkU32(L, 2)); return 0; }
int memWriteDword(lua_State* L) { getEngine(L)->getHost()->write32(checkU32(L, 1), checkU32(L, 2)); return 0; }

int memUnsupported(lua_State* L)
{
    getEngine(L)->appendOutput("[Avviso] memory.register* non è supportato su questo emulatore, chiamata ignorata.\n");
    return 0;
}

// --- gui library ---

bool parseHexColor(const char* hex, size_t length, uint32_t& out)
{
    auto hexValue = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    for (size_t i = 0; i < length; ++i)
    {
        if (hexValue(hex[i]) < 0)
            return false;
    }

    if (length == 6 || length == 8)
    {
        uint32_t value = 0;
        for (size_t i = 0; i < length; ++i)
            value = (value << 4) | hexValue(hex[i]);
        out = length == 6 ? (value << 8) | 0xFF : value;
        return true;
    }

    if (length == 3 || length == 4)
    {
        uint32_t value = 0;
        for (size_t i = 0; i < length; ++i)
        {
            uint32_t v = hexValue(hex[i]);
            value = (value << 8) | (v << 4) | v;
        }
        out = length == 3 ? (value << 8) | 0xFF : value;
        return true;
    }

    return false;
}

uint32_t getColor(lua_State* L, int index, uint32_t defaultColor)
{
    switch (lua_type(L, index))
    {
        case LUA_TNONE:
        case LUA_TNIL:
            return defaultColor;

        case LUA_TNUMBER:
            return numberToU32(lua_tonumber(L, index));

        case LUA_TSTRING:
        {
            size_t length;
            const char* text = lua_tolstring(L, index, &length);
            std::string name(text, length);
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return (char) std::tolower(c); });

            struct NamedColor { const char* name; uint32_t value; };
            static const NamedColor namedColors[] = {
                { "white", 0xFFFFFFFF }, { "black", 0x000000FF }, { "clear", 0x00000000 },
                { "gray", 0x7F7F7FFF }, { "grey", 0x7F7F7FFF }, { "red", 0xFF0000FF },
                { "orange", 0xFF7F00FF }, { "yellow", 0xFFFF00FF }, { "chartreuse", 0x7FFF00FF },
                { "green", 0x00FF00FF }, { "teal", 0x00FF7FFF }, { "cyan", 0x00FFFFFF },
                { "blue", 0x0000FFFF }, { "purple", 0x7F00FFFF }, { "magenta", 0xFF00FFFF },
            };
            for (const auto& named : namedColors)
            {
                if (name == named.name)
                    return named.value;
            }

            uint32_t parsed;
            if (!name.empty() && name[0] == '#' && parseHexColor(name.c_str() + 1, name.size() - 1, parsed))
                return parsed;
            if (name.size() > 2 && name[0] == '0' && name[1] == 'x' && parseHexColor(name.c_str() + 2, name.size() - 2, parsed))
                return parsed;

            luaL_error(L, "colore non valido: '%s'", text);
            return 0;
        }

        case LUA_TTABLE:
        {
            int components[4] = { 255, 255, 255, 255 };
            const char* keys[4] = { "r", "g", "b", "a" };
            for (int i = 0; i < 4; ++i)
            {
                lua_getfield(L, index, keys[i]);
                if (lua_isnumber(L, -1))
                    components[i] = (int) lua_tonumber(L, -1);
                lua_pop(L, 1);

                lua_rawgeti(L, index, i + 1);
                if (lua_isnumber(L, -1))
                    components[i] = (int) lua_tonumber(L, -1);
                lua_pop(L, 1);
            }
            uint32_t color = 0;
            for (int c : components)
                color = (color << 8) | (uint32_t) std::clamp(c, 0, 255);
            return color;
        }

        default:
            luaL_error(L, "colore non valido (tipo %s)", luaL_typename(L, index));
            return 0;
    }
}

uint32_t applyOpacity(LuaEngine* engine, uint32_t color)
{
    float opacity = engine->getOpacity();
    if (opacity >= 1.0f)
        return color;

    uint32_t alpha = color & 0xFF;
    alpha = (uint32_t) std::lround(alpha * std::clamp(opacity, 0.0f, 1.0f));
    return (color & 0xFFFFFF00) | alpha;
}

std::string toDisplayString(lua_State* L, int index)
{
    luaL_checkany(L, index);
    int type = lua_type(L, index);
    if (type == LUA_TSTRING || type == LUA_TNUMBER)
    {
        lua_pushvalue(L, index);
        size_t length;
        const char* text = lua_tolstring(L, -1, &length);
        std::string result(text, length);
        lua_pop(L, 1);
        return result;
    }

    lua_getglobal(L, "tostring");
    lua_pushvalue(L, index);
    lua_call(L, 1, 1);
    const char* text = lua_tostring(L, -1);
    std::string result = text ? text : "";
    lua_pop(L, 1);
    return result;
}

int guiText(lua_State* L)
{
    LuaEngine* engine = getEngine(L);
    int x = checkInt(L, 1);
    int y = checkInt(L, 2);
    CRASH_BREADCRUMB("Lua: gui.text");
    std::string text = toDisplayString(L, 3);
    uint32_t color = applyOpacity(engine, getColor(L, 4, kDefaultTextColor));
    uint32_t outline = applyOpacity(engine, getColor(L, 5, kDefaultTextOutline));
    engine->getMutableCanvas().drawText(x, y, text, color, outline);
    return 0;
}

int guiBox(lua_State* L)
{
    LuaEngine* engine = getEngine(L);
    int x1 = checkInt(L, 1);
    int y1 = checkInt(L, 2);
    int x2 = checkInt(L, 3);
    int y2 = checkInt(L, 4);
    CRASH_BREADCRUMB("Lua: gui.box");
    uint32_t fill = getColor(L, 5, kDefaultBoxFill);
    uint32_t outline = getColor(L, 6, fill | 0xFF);
    engine->getMutableCanvas().drawBox(x1, y1, x2, y2, applyOpacity(engine, fill), applyOpacity(engine, outline));
    return 0;
}

int guiLine(lua_State* L)
{
    LuaEngine* engine = getEngine(L);
    int x1 = checkInt(L, 1);
    int y1 = checkInt(L, 2);
    int x2 = checkInt(L, 3);
    int y2 = checkInt(L, 4);
    uint32_t color = applyOpacity(engine, getColor(L, 5, kDefaultLineColor));
    bool skipFirst = lua_toboolean(L, 6) != 0;
    engine->getMutableCanvas().drawLine(x1, y1, x2, y2, color, skipFirst);
    return 0;
}

int guiPixel(lua_State* L)
{
    LuaEngine* engine = getEngine(L);
    int x = checkInt(L, 1);
    int y = checkInt(L, 2);
    uint32_t color = applyOpacity(engine, getColor(L, 3, kDefaultLineColor));
    engine->getMutableCanvas().drawPixel(x, y, color);
    return 0;
}

int guiGetPixel(lua_State* L)
{
    LuaEngine* engine = getEngine(L);
    uint32_t color = engine->getCanvas().getPixel(checkInt(L, 1), checkInt(L, 2));
    lua_pushinteger(L, (color >> 24) & 0xFF);
    lua_pushinteger(L, (color >> 16) & 0xFF);
    lua_pushinteger(L, (color >> 8) & 0xFF);
    lua_pushinteger(L, color & 0xFF);
    return 4;
}

int guiParseColor(lua_State* L)
{
    uint32_t color = getColor(L, 1, 0);
    lua_pushinteger(L, (color >> 24) & 0xFF);
    lua_pushinteger(L, (color >> 16) & 0xFF);
    lua_pushinteger(L, (color >> 8) & 0xFF);
    lua_pushinteger(L, color & 0xFF);
    return 4;
}

int guiOpacity(lua_State* L)
{
    getEngine(L)->setOpacity((float) luaL_checknumber(L, 1));
    return 0;
}

int guiTransparency(lua_State* L)
{
    // DeSmuME/FCEUX: 0 = opaque, 4 = invisible
    float transparency = (float) luaL_checknumber(L, 1);
    getEngine(L)->setOpacity(1.0f - transparency / 4.0f);
    return 0;
}

int guiRegister(lua_State* L)
{
    getEngine(L)->setCallback(L, LuaEngine::CallbackGui, 1);
    return 0;
}

int guiPopup(lua_State* L)
{
    getEngine(L)->appendOutput(toDisplayString(L, 1) + "\n");
    return 0;
}

int noOp(lua_State* L)
{
    return 0;
}

// --- input / joypad ---

int inputGet(lua_State* L)
{
    getEngine(L)->pushInputTable(L);
    return 1;
}

int joypadGet(lua_State* L)
{
    uint32_t pressed = getEngine(L)->getHost()->getPressedButtons();
    // melonDS key order: 0 A, 1 B, 2 Select, 3 Start, 4 Right, 5 Left, 6 Up, 7 Down, 8 R, 9 L, 10 X, 11 Y
    static const char* names[12] = { "A", "B", "select", "start", "right", "left", "up", "down", "R", "L", "X", "Y" };

    lua_createtable(L, 0, 14);
    for (int i = 0; i < 12; ++i)
    {
        lua_pushboolean(L, (pressed >> i) & 1);
        lua_setfield(L, -2, names[i]);
    }
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "debug");
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "lid");
    return 1;
}

// DeSmuME names, compared case-insensitively. Index = melonDS key bit.
const char* const kJoypadKeyNames[12] = { "a", "b", "select", "start", "right", "left", "up", "down", "r", "l", "x", "y" };

int joypadKeyBit(const char* name)
{
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char) std::tolower(c); });
    for (int i = 0; i < 12; ++i)
    {
        if (lower == kJoypadKeyNames[i])
            return i;
    }
    return -1;
}

// joypad.set([port,] table): true = press, false = force release, nil/absent = leave it to the user.
// Applies to the next frame only (call it every frame to hold a button), like in DeSmuME.
int joypadSet(lua_State* L)
{
    int tableIndex = lua_istable(L, 1) ? 1 : 2;
    luaL_checktype(L, tableIndex, LUA_TTABLE);

    uint32_t press = 0;
    uint32_t release = 0;
    lua_pushnil(L);
    while (lua_next(L, tableIndex) != 0)
    {
        // Key at -2, value at -1. Don't call lua_tostring on the key: it would confuse lua_next.
        if (lua_type(L, -2) == LUA_TSTRING)
        {
            int keyBit = joypadKeyBit(lua_tostring(L, -2));
            if (keyBit >= 0)
            {
                if (lua_toboolean(L, -1))
                    press |= 1u << keyBit;
                else
                    release |= 1u << keyBit;
            }
        }
        lua_pop(L, 1);
    }

    getEngine(L)->setJoypadOverride(press, release);
    return 0;
}

// --- stylus (DeSmuME: stylus.get/peek/read and stylus.set/write with {x, y, touch}) ---

int stylusGet(lua_State* L)
{
    bool isTouching;
    int x, y;
    getEngine(L)->getUserTouch(isTouching, x, y);

    lua_createtable(L, 0, 3);
    lua_pushinteger(L, x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, y);
    lua_setfield(L, -2, "y");
    lua_pushboolean(L, isTouching);
    lua_setfield(L, -2, "touch");
    return 1;
}

int stylusSet(lua_State* L)
{
    int tableIndex = lua_istable(L, 1) ? 1 : 2;
    luaL_checktype(L, tableIndex, LUA_TTABLE);

    lua_getfield(L, tableIndex, "x");
    bool hasPosition = !lua_isnil(L, -1);
    int x = hasPosition ? (int) lua_tonumber(L, -1) : 0;
    lua_pop(L, 1);
    lua_getfield(L, tableIndex, "y");
    int y = lua_isnil(L, -1) ? 0 : (int) lua_tonumber(L, -1);
    lua_pop(L, 1);

    // Without an explicit "touch" field, giving a position means touching
    lua_getfield(L, tableIndex, "touch");
    bool down = lua_isnil(L, -1) ? hasPosition : (bool) lua_toboolean(L, -1);
    lua_pop(L, 1);

    getEngine(L)->setStylusOverride(down, std::clamp(x, 0, 255), std::clamp(y, 0, 191));
    return 0;
}

// --- emu ---

int emuFrameAdvance(lua_State* L)
{
    LuaEngine* engine = getEngine(L);
    if (L != engine->getMainCoroutine())
        return luaL_error(L, "emu.frameadvance si può chiamare solo dal corpo principale dello script, non dalle funzioni registrate");

    return lua_yield(L, 0);
}

int emuFrameCount(lua_State* L)
{
    lua_pushinteger(L, getEngine(L)->getFrameCount());
    return 1;
}

int emuLagCount(lua_State* L)
{
    lua_pushinteger(L, 0);
    return 1;
}

int emuEmulating(lua_State* L)
{
    lua_pushboolean(L, 1);
    return 1;
}

int emuReset(lua_State* L)
{
    getEngine(L)->getHost()->requestReset();
    return 0;
}

int emuRegisterBefore(lua_State* L) { getEngine(L)->setCallback(L, LuaEngine::CallbackBefore, 1); return 0; }
int emuRegisterAfter(lua_State* L) { getEngine(L)->setCallback(L, LuaEngine::CallbackAfter, 1); return 0; }
int emuRegisterExit(lua_State* L) { getEngine(L)->setCallback(L, LuaEngine::CallbackExit, 1); return 0; }

int emuMessage(lua_State* L)
{
    getEngine(L)->appendOutput(toDisplayString(L, 1) + "\n");
    return 0;
}

int emuGetDir(lua_State* L)
{
    lua_pushstring(L, getEngine(L)->getWorkDir().c_str());
    return 1;
}

int movieFalse(lua_State* L)
{
    lua_pushboolean(L, 0);
    return 1;
}

int movieZero(lua_State* L)
{
    lua_pushinteger(L, 0);
    return 1;
}

// --- base library overrides ---

int basePrint(lua_State* L)
{
    int n = lua_gettop(L);
    std::string line;

    lua_getglobal(L, "tostring");
    for (int i = 1; i <= n; ++i)
    {
        lua_pushvalue(L, -1);
        lua_pushvalue(L, i);
        lua_call(L, 1, 1);
        const char* text = lua_tostring(L, -1);
        if (text == nullptr)
            return luaL_error(L, "'tostring' must return a string to 'print'");
        if (i > 1)
            line += "\t";
        line += text;
        lua_pop(L, 1);
    }
    line += "\n";

    getEngine(L)->appendOutput(line);
    return 0;
}

int osExit(lua_State* L)
{
    getEngine(L)->requestStopFromScript();
    return luaL_error(L, kExitSentinel);
}

bool makeDirectories(const std::string& path)
{
    if (path.empty())
        return false;

    std::string current;
    size_t position = 0;
    while (position != std::string::npos)
    {
        size_t next = path.find('/', position + 1);
        current = path.substr(0, next);
        position = next;
        if (current.empty() || current == "/")
            continue;

        if (mkdir(current.c_str(), 0770) != 0 && errno != EEXIST)
            return false;
    }
    return true;
}

std::vector<std::string> splitCommand(const std::string& command)
{
    std::vector<std::string> tokens;
    std::string current;
    char quote = 0;
    bool hasToken = false;

    for (char c : command)
    {
        if (quote)
        {
            if (c == quote)
                quote = 0;
            else
                current += c;
        }
        else if (c == '"' || c == '\'')
        {
            quote = c;
            hasToken = true;
        }
        else if (c == ' ' || c == '\t')
        {
            if (hasToken)
            {
                tokens.push_back(current);
                current.clear();
                hasToken = false;
            }
        }
        else
        {
            current += c;
            hasToken = true;
        }
    }
    if (hasToken)
        tokens.push_back(current);

    return tokens;
}

int osExecute(lua_State* L)
{
    LuaEngine* engine = getEngine(L);
    if (lua_isnoneornil(L, 1))
    {
        // No shell available
        lua_pushinteger(L, 0);
        return 1;
    }

    std::string command = luaL_checkstring(L, 1);
    std::vector<std::string> tokens = splitCommand(command);

    if (!tokens.empty() && (tokens[0] == "mkdir" || tokens[0] == "md"))
    {
        bool ok = true;
        for (size_t i = 1; i < tokens.size(); ++i)
        {
            std::string path = tokens[i];
            if (path.empty() || path[0] == '-')
                continue;

            std::replace(path.begin(), path.end(), '\\', '/');
            if (path[0] != '/')
                path = engine->getWorkDir() + "/" + path;

            ok = makeDirectories(path) && ok;
        }
        lua_pushinteger(L, ok ? 0 : 1);
        return 1;
    }

    engine->appendOutput("[Avviso] os.execute supporta solo \"mkdir\". Comando ignorato: " + command + "\n");
    lua_pushinteger(L, 1);
    return 1;
}

void registerFunctions(lua_State* L, const char* table, const luaL_Reg* functions)
{
    luaL_register(L, table, functions);
    lua_pop(L, 1);
}

const luaL_Reg kBitFunctions[] = {
    { "tobit", bitTobit }, { "bnot", bitBnot }, { "band", bitBand }, { "bor", bitBor }, { "bxor", bitBxor },
    { "lshift", bitLshift }, { "rshift", bitRshift }, { "arshift", bitArshift }, { "rol", bitRol },
    { "ror", bitRor }, { "bswap", bitBswap }, { "tohex", bitTohex },
    { nullptr, nullptr }
};

const luaL_Reg kMemoryFunctions[] = {
    { "readbyte", memReadByte }, { "readbyteunsigned", memReadByte }, { "readbytesigned", memReadByteSigned },
    { "readword", memReadWord }, { "readwordunsigned", memReadWord }, { "readwordsigned", memReadWordSigned },
    { "readdword", memReadDword }, { "readdwordunsigned", memReadDword }, { "readdwordsigned", memReadDwordSigned },
    { "readlong", memReadDword }, { "readlongunsigned", memReadDword }, { "readlongsigned", memReadDwordSigned },
    { "readbyterange", memReadByteRange },
    { "writebyte", memWriteByte }, { "writeword", memWriteWord }, { "writedword", memWriteDword },
    { "writelong", memWriteDword },
    { "register", memUnsupported }, { "registerwrite", memUnsupported }, { "registerread", memUnsupported },
    { "registerexec", memUnsupported }, { "registerrun", memUnsupported },
    { nullptr, nullptr }
};

const luaL_Reg kGuiFunctions[] = {
    { "text", guiText }, { "drawtext", guiText },
    { "box", guiBox }, { "drawbox", guiBox }, { "rect", guiBox }, { "drawrect", guiBox },
    { "line", guiLine }, { "drawline", guiLine },
    { "pixel", guiPixel }, { "drawpixel", guiPixel }, { "setpixel", guiPixel }, { "writepixel", guiPixel },
    { "getpixel", guiGetPixel }, { "parsecolor", guiParseColor },
    { "opacity", guiOpacity }, { "transparency", guiTransparency },
    { "register", guiRegister }, { "popup", guiPopup },
    { "clearuncommitted", noOp }, { "gdoverlay", noOp }, { "redraw", noOp },
    { nullptr, nullptr }
};

const luaL_Reg kInputFunctions[] = {
    { "get", inputGet }, { "read", inputGet },
    { nullptr, nullptr }
};

const luaL_Reg kJoypadFunctions[] = {
    { "get", joypadGet }, { "read", joypadGet }, { "set", joypadSet }, { "write", joypadSet },
    { nullptr, nullptr }
};

const luaL_Reg kStylusFunctions[] = {
    { "get", stylusGet }, { "peek", stylusGet }, { "read", stylusGet }, { "set", stylusSet }, { "write", stylusSet },
    { nullptr, nullptr }
};

const luaL_Reg kEmuFunctions[] = {
    { "frameadvance", emuFrameAdvance }, { "framecount", emuFrameCount }, { "lagcount", emuLagCount },
    { "emulating", emuEmulating }, { "reset", emuReset },
    { "registerbefore", emuRegisterBefore }, { "registerafter", emuRegisterAfter },
    { "registerexit", emuRegisterExit }, { "message", emuMessage }, { "getdir", emuGetDir },
    { "pause", noOp }, { "unpause", noOp }, { "speedmode", noOp }, { "wait", noOp }, { "redraw", noOp },
    { "persistglobalvariables", noOp },
    { nullptr, nullptr }
};

const luaL_Reg kSavestateFunctions[] = {
    { "create", noOp }, { "save", noOp }, { "load", noOp }, { "registersave", noOp }, { "registerload", noOp },
    { nullptr, nullptr }
};

const luaL_Reg kMovieFunctions[] = {
    { "active", movieFalse }, { "framecount", emuFrameCount }, { "mode", noOp }, { "rerecordcount", movieZero },
    { "length", movieZero }, { "readonly", movieFalse },
    { nullptr, nullptr }
};

}

LuaEngine::LuaEngine(LuaHost* host) : host(host), status(Status::Idle)
{
    std::fill(std::begin(callbackRefs), std::end(callbackRefs), LUA_NOREF);
}

LuaEngine::~LuaEngine()
{
    close();
}

void LuaEngine::requestLoad(std::string name, std::string source, std::string directory)
{
    std::lock_guard lock(requestMutex);
    pendingLoad = LoadRequest { std::move(name), std::move(source), std::move(directory) };
    pendingStop = false;
}

void LuaEngine::requestStop()
{
    std::lock_guard lock(requestMutex);
    pendingLoad.reset();
    pendingStop = true;
}

void LuaEngine::setKeyState(const std::string& key, bool pressed)
{
    std::lock_guard lock(inputMutex);
    if (pressed)
    {
        pressedKeys[key] = KeyState();
        return;
    }

    auto it = pressedKeys.find(key);
    if (it == pressedKeys.end())
        return;

    // Keep very short taps visible for at least one input.get() call
    if (it->second.seen)
        pressedKeys.erase(it);
    else
        it->second.releasePending = true;
}

void LuaEngine::setTouchState(bool isTouching, int x, int y)
{
    std::lock_guard lock(inputMutex);
    touching = isTouching;
    if (isTouching)
    {
        touchX = x;
        touchY = y;
    }
}

void LuaEngine::setJoypadOverride(uint32_t pressMask, uint32_t releaseMask)
{
    // Each call replaces the previous one, like in DeSmuME
    pendingInput.pressMask = pressMask & 0xFFF;
    pendingInput.releaseMask = releaseMask & 0xFFF & ~pressMask;
}

void LuaEngine::setStylusOverride(bool down, int x, int y)
{
    pendingInput.touchSet = true;
    pendingInput.touchDown = down;
    pendingInput.touchX = x;
    pendingInput.touchY = y;
}

void LuaEngine::getUserTouch(bool& isTouching, int& x, int& y)
{
    std::lock_guard lock(inputMutex);
    isTouching = touching;
    x = touchX;
    y = touchY;
}

LuaEngine::InputOverride LuaEngine::takeInputOverride()
{
    InputOverride result = pendingInput;
    pendingInput = InputOverride();
    return result;
}

void LuaEngine::pushInputTable(lua_State* state)
{
    std::lock_guard lock(inputMutex);
    lua_createtable(state, 0, (int) pressedKeys.size() + 3);
    for (auto& [name, keyState] : pressedKeys)
    {
        keyState.seen = true;
        lua_pushboolean(state, 1);
        lua_setfield(state, -2, name.c_str());
    }

    lua_pushinteger(state, touchX);
    lua_setfield(state, -2, "xmouse");
    lua_pushinteger(state, touchY);
    lua_setfield(state, -2, "ymouse");
    if (touching)
    {
        lua_pushboolean(state, 1);
        lua_setfield(state, -2, "leftclick");
    }
}

void LuaEngine::releaseSeenKeys()
{
    std::lock_guard lock(inputMutex);
    for (auto it = pressedKeys.begin(); it != pressedKeys.end();)
    {
        if (it->second.releasePending)
        {
            it->second.framesSinceRelease++;
            if (it->second.seen || it->second.framesSinceRelease > 2)
            {
                it = pressedKeys.erase(it);
                continue;
            }
        }
        ++it;
    }
}

std::string LuaEngine::pollOutput()
{
    std::lock_guard lock(outputMutex);
    std::string result;
    result.swap(output);
    return result;
}

std::string LuaEngine::getScriptName()
{
    std::lock_guard lock(outputMutex);
    return scriptName;
}

void LuaEngine::appendOutput(const std::string& text)
{
    std::lock_guard lock(outputMutex);
    output += text;
    if (output.size() > kMaxOutputSize)
        output.erase(0, output.size() - kMaxOutputSize);
}

void LuaEngine::setCallback(lua_State* state, CallbackSlot slot, int stackIndex)
{
    if (!lua_isnoneornil(state, stackIndex))
        luaL_checktype(state, stackIndex, LUA_TFUNCTION);

    if (callbackRefs[slot] != LUA_NOREF)
    {
        luaL_unref(state, LUA_REGISTRYINDEX, callbackRefs[slot]);
        callbackRefs[slot] = LUA_NOREF;
    }

    if (!lua_isnoneornil(state, stackIndex))
    {
        lua_pushvalue(state, stackIndex);
        callbackRefs[slot] = luaL_ref(state, LUA_REGISTRYINDEX);
    }
}

bool LuaEngine::hasCallbacks() const
{
    for (int ref : callbackRefs)
    {
        if (ref != LUA_NOREF)
            return true;
    }
    return false;
}

void LuaEngine::load(LoadRequest request)
{
    CRASH_BREADCRUMB("Lua: caricamento script (creazione stato Lua)");
    close();
    canvas.clear();

    {
        std::lock_guard lock(outputMutex);
        scriptName = request.name;
    }

    workDir = request.workDir;
    while (workDir.size() > 1 && workDir.back() == '/')
        workDir.pop_back();
    makeDirectories(workDir);

    L = luaL_newstate();
    if (L == nullptr)
    {
        fail("impossibile creare lo stato Lua (memoria esaurita?)");
        return;
    }

    luaL_openlibs(L);

    lua_pushlightuserdata(L, &kEngineRegistryKey);
    lua_pushlightuserdata(L, this);
    lua_rawset(L, LUA_REGISTRYINDEX);

    registerFunctions(L, "bit", kBitFunctions);
    registerFunctions(L, "memory", kMemoryFunctions);
    registerFunctions(L, "gui", kGuiFunctions);
    registerFunctions(L, "input", kInputFunctions);
    registerFunctions(L, "joypad", kJoypadFunctions);
    registerFunctions(L, "stylus", kStylusFunctions);
    registerFunctions(L, "emu", kEmuFunctions);
    registerFunctions(L, "savestate", kSavestateFunctions);
    registerFunctions(L, "movie", kMovieFunctions);

    // DeSmuME exposes some modules under alternative names
    lua_getglobal(L, "input");
    lua_setglobal(L, "keyboard");
    lua_getglobal(L, "emu");
    lua_setglobal(L, "desmume");

    lua_register(L, "print", basePrint);
    lua_register(L, "AND", globalAnd);
    lua_register(L, "OR", globalOr);
    lua_register(L, "XOR", globalXor);
    lua_register(L, "SHIFT", globalShift);
    lua_register(L, "BIT", globalBit);

    lua_getglobal(L, "os");
    lua_pushcfunction(L, osExecute);
    lua_setfield(L, -2, "execute");
    lua_pushcfunction(L, osExit);
    lua_setfield(L, -2, "exit");
    lua_pop(L, 1);

    if (luaL_loadbuffer(L, kPrelude, strlen(kPrelude), "=prelude") != 0)
    {
        fail(lua_tostring(L, -1));
        return;
    }
    lua_pushstring(L, workDir.c_str());
    if (lua_pcall(L, 1, 0, 0) != 0)
    {
        fail(lua_tostring(L, -1));
        return;
    }

    mainCoroutine = lua_newthread(L);
    mainCoroutineRef = luaL_ref(L, LUA_REGISTRYINDEX);

    std::string chunkName = "@" + request.name;
    if (luaL_loadbuffer(mainCoroutine, request.source.data(), request.source.size(), chunkName.c_str()) != 0)
    {
        const char* message = lua_tostring(mainCoroutine, -1);
        fail(message ? message : "errore di sintassi");
        return;
    }

    frameCount = 0;
    opacity = 1.0f;
    mainCoroutineFinished = false;
    stopFromScript = false;
    status = Status::Running;
    appendOutput("Script avviato: " + request.name + "\n");

    // Run the script's top level right away, until it finishes or calls emu.frameadvance()
    resumeMainCoroutine();
}

void LuaEngine::resumeMainCoroutine()
{
    if (L == nullptr || mainCoroutineFinished)
        return;

    CRASH_BREADCRUMB("Lua: esecuzione corpo dello script (coroutine)");
    int result = lua_resume(mainCoroutine, 0);
    if (result == LUA_YIELD)
        return;

    if (result == 0)
    {
        mainCoroutineFinished = true;
        if (!hasCallbacks())
        {
            appendOutput("Script terminato.\n");
            close();
            status = Status::Idle;
        }
        return;
    }

    const char* rawMessage = lua_tostring(mainCoroutine, -1);
    std::string message = rawMessage ? rawMessage : "errore sconosciuto";

    // Add a stack traceback when possible
    if (!stopFromScript)
    {
        lua_getglobal(L, "debug");
        if (lua_istable(L, -1))
        {
            lua_getfield(L, -1, "traceback");
            if (lua_isfunction(L, -1))
            {
                lua_rawgeti(L, LUA_REGISTRYINDEX, mainCoroutineRef);
                lua_pushstring(L, message.c_str());
                if (lua_pcall(L, 2, 1, 0) == 0 && lua_isstring(L, -1))
                    message = lua_tostring(L, -1);
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }

    fail(message);
}

bool LuaEngine::runCallback(CallbackSlot slot)
{
    if (L == nullptr || callbackRefs[slot] == LUA_NOREF)
        return true;

    int base = lua_gettop(L);
    int errorHandler = 0;

    lua_getglobal(L, "debug");
    if (lua_istable(L, -1))
    {
        lua_getfield(L, -1, "traceback");
        lua_remove(L, -2);
        if (lua_isfunction(L, -1))
            errorHandler = lua_gettop(L);
        else
            lua_pop(L, 1);
    }
    else
    {
        lua_pop(L, 1);
    }

    static const char* const kCallbackBreadcrumbs[CallbackCount] = {
        "Lua: callback emu.registerbefore", "Lua: callback emu.registerafter", "Lua: callback gui.register", "Lua: callback emu.registerexit",
    };
    CRASH_BREADCRUMB(kCallbackBreadcrumbs[slot]);
    lua_rawgeti(L, LUA_REGISTRYINDEX, callbackRefs[slot]);
    int result = lua_pcall(L, 0, 0, errorHandler);
    if (result != 0)
    {
        const char* rawMessage = lua_tostring(L, -1);
        std::string message = rawMessage ? rawMessage : "errore sconosciuto";
        lua_settop(L, base);
        fail(message);
        return false;
    }

    lua_settop(L, base);
    return true;
}

void LuaEngine::onFrameStart()
{
    std::optional<LoadRequest> load;
    bool stop;
    {
        std::lock_guard lock(requestMutex);
        load.swap(pendingLoad);
        stop = pendingStop;
        pendingStop = false;
    }

    if (stop && L != nullptr)
    {
        runCallback(CallbackExit);
        close();
        status = Status::Idle;
        canvas.clear();
        appendOutput("Script fermato.\n");
    }

    if (load.has_value())
    {
        if (L != nullptr)
            runCallback(CallbackExit);
        this->load(std::move(*load));
    }

    if (L != nullptr)
        runCallback(CallbackBefore);
}

void LuaEngine::onFrameEnd()
{
    canvas.clear();
    if (L == nullptr)
    {
        releaseSeenKeys();
        return;
    }

    frameCount++;

    if (runCallback(CallbackAfter))
    {
        resumeMainCoroutine();
        if (L != nullptr)
            runCallback(CallbackGui);
    }

    releaseSeenKeys();
}

void LuaEngine::shutdown()
{
    if (L != nullptr)
        runCallback(CallbackExit);
    close();
    status = Status::Idle;
}

void LuaEngine::fail(const std::string& message)
{
    bool exitRequested = stopFromScript || message.find(kExitSentinel) != std::string::npos;
    close();
    canvas.clear();

    if (exitRequested)
    {
        appendOutput("Script terminato (os.exit).\n");
        status = Status::Idle;
    }
    else
    {
        appendOutput("[Errore] " + message + "\n");
        status = Status::Error;
    }
}

void LuaEngine::close()
{
    if (L != nullptr)
    {
        lua_close(L);
        L = nullptr;
    }

    mainCoroutine = nullptr;
    mainCoroutineRef = LUA_NOREF;
    std::fill(std::begin(callbackRefs), std::end(callbackRefs), LUA_NOREF);
    mainCoroutineFinished = true;
    stopFromScript = false;
    // A stopped script must not keep holding buttons
    pendingInput = InputOverride();
}

}
