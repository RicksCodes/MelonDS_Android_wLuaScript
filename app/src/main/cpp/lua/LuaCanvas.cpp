#include "LuaCanvas.h"
#include "LuaFont.h"
#include <algorithm>
#include <cstdlib>

namespace MelonDSAndroid::Lua
{

LuaCanvas::LuaCanvas() : buffer(kWidth * kHeight, 0), dirty(false)
{
}

void LuaCanvas::clear()
{
    if (dirty)
    {
        std::fill(buffer.begin(), buffer.end(), 0);
        dirty = false;
    }
}

void LuaCanvas::blend(int x, int y, uint32_t color)
{
    int row = y + kScreenHeight;
    if (x < 0 || x >= kWidth || row < 0 || row >= kHeight)
        return;

    uint32_t srcA = color & 0xFF;
    if (srcA == 0)
        return;

    uint32_t srcR = (color >> 24) & 0xFF;
    uint32_t srcG = (color >> 16) & 0xFF;
    uint32_t srcB = (color >> 8) & 0xFF;

    uint32_t& dst = buffer[row * kWidth + x];
    dirty = true;

    if (srcA == 0xFF || dst == 0)
    {
        dst = srcR | (srcG << 8) | (srcB << 16) | (srcA << 24);
        return;
    }

    // Standard "over" operator with straight (non-premultiplied) alpha
    uint32_t dstR = dst & 0xFF;
    uint32_t dstG = (dst >> 8) & 0xFF;
    uint32_t dstB = (dst >> 16) & 0xFF;
    uint32_t dstA = (dst >> 24) & 0xFF;

    uint32_t dstWeight = dstA * (255 - srcA) / 255;
    uint32_t outA = srcA + dstWeight;
    if (outA == 0)
    {
        dst = 0;
        return;
    }

    uint32_t outR = (srcR * srcA + dstR * dstWeight) / outA;
    uint32_t outG = (srcG * srcA + dstG * dstWeight) / outA;
    uint32_t outB = (srcB * srcA + dstB * dstWeight) / outA;
    dst = outR | (outG << 8) | (outB << 16) | (outA << 24);
}

uint32_t LuaCanvas::getPixel(int x, int y) const
{
    int row = y + kScreenHeight;
    if (x < 0 || x >= kWidth || row < 0 || row >= kHeight)
        return 0;

    uint32_t p = buffer[row * kWidth + x];
    return ((p & 0xFF) << 24) | (((p >> 8) & 0xFF) << 16) | (((p >> 16) & 0xFF) << 8) | ((p >> 24) & 0xFF);
}

void LuaCanvas::drawPixel(int x, int y, uint32_t color)
{
    blend(x, y, color);
}

void LuaCanvas::drawLine(int x1, int y1, int x2, int y2, uint32_t color, bool skipFirst)
{
    // Bresenham
    int dx = std::abs(x2 - x1);
    int dy = -std::abs(y2 - y1);
    int sx = x1 < x2 ? 1 : -1;
    int sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    int x = x1;
    int y = y1;
    bool first = true;

    // Guard against absurdly long lines coming from bad script values
    int maxSteps = 4096;
    while (maxSteps-- > 0)
    {
        if (!(first && skipFirst))
            blend(x, y, color);
        first = false;

        if (x == x2 && y == y2)
            break;

        int e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y += sy;
        }
    }
}

void LuaCanvas::drawBox(int x1, int y1, int x2, int y2, uint32_t fill, uint32_t outline)
{
    if (x1 > x2) std::swap(x1, x2);
    if (y1 > y2) std::swap(y1, y2);

    // Clip to the canvas, keeping track of which edges are real (so the outline is not drawn on clipped edges)
    int minX = std::max(x1, 0);
    int maxX = std::min(x2, kWidth - 1);
    int minY = std::max(y1, -kScreenHeight);
    int maxY = std::min(y2, kScreenHeight - 1);
    if (minX > maxX || minY > maxY)
        return;

    for (int y = minY; y <= maxY; ++y)
    {
        bool horizontalEdge = (y == y1 || y == y2);
        for (int x = minX; x <= maxX; ++x)
        {
            bool edge = horizontalEdge || x == x1 || x == x2;
            blend(x, y, edge ? outline : fill);
        }
    }
}

std::vector<uint32_t> LuaCanvas::decodeUtf8(const std::string& text)
{
    std::vector<uint32_t> result;
    result.reserve(text.size());

    size_t i = 0;
    while (i < text.size())
    {
        unsigned char c = (unsigned char) text[i];
        uint32_t cp;
        int extra;

        if (c < 0x80) { cp = c; extra = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else
        {
            // Not valid UTF-8: treat the byte as Latin-1, which is what most old scripts use
            result.push_back(c);
            ++i;
            continue;
        }

        if (i + extra >= text.size())
        {
            // Truncated sequence at the end of the string
            result.push_back(c);
            ++i;
            continue;
        }

        bool valid = true;
        for (int k = 1; k <= extra; ++k)
        {
            unsigned char cc = (unsigned char) text[i + k];
            if ((cc & 0xC0) != 0x80)
            {
                valid = false;
                break;
            }
            cp = (cp << 6) | (cc & 0x3F);
        }

        if (!valid)
        {
            result.push_back(c);
            ++i;
            continue;
        }

        result.push_back(cp);
        i += extra + 1;
    }

    return result;
}

void LuaCanvas::drawText(int x, int y, const std::string& utf8Text, uint32_t color, uint32_t outline)
{
    std::vector<uint32_t> codepoints = decodeUtf8(utf8Text);

    // Lay the text out into lines of glyph indices
    std::vector<std::vector<int>> lines(1);
    for (uint32_t cp : codepoints)
    {
        if (cp == '\n')
        {
            lines.emplace_back();
            continue;
        }
        if (cp == '\r')
            continue;
        if (cp == '\t')
        {
            for (int k = 0; k < 4; ++k)
                lines.back().push_back(' ' - kFontFirstChar);
            continue;
        }
        if (cp < 0x20)
            continue;
        if (cp > (uint32_t) kFontLastChar)
            cp = '?';

        lines.back().push_back((int) cp - kFontFirstChar);
    }

    size_t maxColumns = 0;
    for (auto& line : lines)
        maxColumns = std::max(maxColumns, line.size());
    if (maxColumns == 0)
        return;

    // Build a mask for the whole block first so that every pixel is blended exactly once
    // 0 = empty, 1 = outline, 2 = glyph
    int maskWidth = (int) maxColumns * kCharAdvance + 2;
    int maskHeight = (int) lines.size() * kLineHeight + 2;
    std::vector<uint8_t> mask(maskWidth * maskHeight, 0);

    for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
    {
        const auto& line = lines[lineIndex];
        for (size_t col = 0; col < line.size(); ++col)
        {
            const uint8_t* glyph = kFont5x7[line[col]];
            int baseX = 1 + (int) col * kCharAdvance;
            int baseY = 1 + (int) lineIndex * kLineHeight;
            for (int gy = 0; gy < kGlyphHeight; ++gy)
            {
                uint8_t bits = glyph[gy];
                for (int gx = 0; gx < kGlyphWidth; ++gx)
                {
                    if (bits & (1 << (kGlyphWidth - 1 - gx)))
                        mask[(baseY + gy) * maskWidth + baseX + gx] = 2;
                }
            }
        }
    }

    bool hasOutline = (outline & 0xFF) != 0;
    if (hasOutline)
    {
        for (int my = 0; my < maskHeight; ++my)
        {
            for (int mx = 0; mx < maskWidth; ++mx)
            {
                if (mask[my * maskWidth + mx] != 0)
                    continue;

                bool nearGlyph = false;
                for (int oy = -1; oy <= 1 && !nearGlyph; ++oy)
                {
                    for (int ox = -1; ox <= 1; ++ox)
                    {
                        int nx = mx + ox;
                        int ny = my + oy;
                        if (nx < 0 || ny < 0 || nx >= maskWidth || ny >= maskHeight)
                            continue;
                        if (mask[ny * maskWidth + nx] == 2)
                        {
                            nearGlyph = true;
                            break;
                        }
                    }
                }

                if (nearGlyph)
                    mask[my * maskWidth + mx] = 1;
            }
        }
    }

    for (int my = 0; my < maskHeight; ++my)
    {
        for (int mx = 0; mx < maskWidth; ++mx)
        {
            uint8_t value = mask[my * maskWidth + mx];
            if (value == 2)
                blend(x + mx - 1, y + my - 1, color);
            else if (value == 1)
                blend(x + mx - 1, y + my - 1, outline);
        }
    }
}

}
