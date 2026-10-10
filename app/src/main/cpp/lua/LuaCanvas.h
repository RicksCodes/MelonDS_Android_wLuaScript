#ifndef MELONDS_LUA_CANVAS_H
#define MELONDS_LUA_CANVAS_H

#include <cstdint>
#include <string>
#include <vector>

namespace MelonDSAndroid::Lua
{

/**
 * CPU-side drawing surface for the Lua gui.* functions. It covers both DS screens using DeSmuME's coordinate system:
 * x goes from 0 to 255, y from -192 to -1 on the top screen and from 0 to 191 on the bottom screen.
 *
 * Colors are passed as 0xRRGGBBAA. Pixels are stored as RGBA bytes, ready to be uploaded to an OpenGL texture.
 */
class LuaCanvas
{
public:
    static constexpr int kWidth = 256;
    static constexpr int kScreenHeight = 192;
    static constexpr int kHeight = kScreenHeight * 2;
    static constexpr int kLineHeight = 9;
    static constexpr int kCharAdvance = 6;

    LuaCanvas();

    void clear();
    bool isEmpty() const { return !dirty; }
    const uint32_t* pixels() const { return buffer.data(); }

    void drawPixel(int x, int y, uint32_t color);
    void drawLine(int x1, int y1, int x2, int y2, uint32_t color, bool skipFirst);
    void drawBox(int x1, int y1, int x2, int y2, uint32_t fill, uint32_t outline);
    void drawText(int x, int y, const std::string& utf8Text, uint32_t color, uint32_t outline);

    /**
     * @return The RGBA (0xRRGGBBAA) value of a pixel, for testing and gui.getpixel.
     */
    uint32_t getPixel(int x, int y) const;

private:
    void blend(int x, int y, uint32_t color);
    static std::vector<uint32_t> decodeUtf8(const std::string& text);

    std::vector<uint32_t> buffer;
    bool dirty;
};

}

#endif
