// Host test for the Lua engine: fakes a Pokémon Platinum (ITA) memory image and runs scripts for a few frames.
#include "lua/LuaEngine.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

using namespace MelonDSAndroid::Lua;

struct FakeHost : LuaHost
{
    std::vector<uint8_t> mainRam = std::vector<uint8_t>(4 * 1024 * 1024, 0);
    std::vector<uint8_t> dtcm = std::vector<uint8_t>(0x4000, 0);
    uint32_t dtcmBase = 0x027E0000;
    int resets = 0;

    uint8_t* ptr(uint32_t a)
    {
        if ((a & 0xFFFFC000) == dtcmBase) return &dtcm[a & 0x3FFF];
        if ((a & 0xFF000000) == 0x02000000) return &mainRam[a & 0x3FFFFF];
        return nullptr;
    }
    uint8_t read8(uint32_t a) override { auto p = ptr(a); return p ? *p : 0; }
    uint16_t read16(uint32_t a) override { a &= ~1u; auto p = ptr(a); uint16_t v = 0; if (p) memcpy(&v, p, 2); return v; }
    uint32_t read32(uint32_t a) override { a &= ~3u; auto p = ptr(a); uint32_t v = 0; if (p) memcpy(&v, p, 4); return v; }
    void write8(uint32_t a, uint8_t v) override { if (auto p = ptr(a)) *p = v; }
    void write16(uint32_t a, uint16_t v) override { a &= ~1u; if (auto p = ptr(a)) memcpy(p, &v, 2); }
    void write32(uint32_t a, uint32_t v) override { a &= ~3u; if (auto p = ptr(a)) memcpy(p, &v, 4); }
    uint32_t getPressedButtons() override { return 0x1; }
    void requestReset() override { resets++; }
};

static uint32_t lcrng(uint32_t s) { return s * 0x41C64E6Du + 0x6073u; }

// Writes a wild Pokémon with block order 0 (growth block first) at addr
static void writeWild(FakeHost& h, uint32_t addr, uint32_t pid, uint16_t species, uint16_t item, uint16_t checksum)
{
    h.write32(addr, pid);
    h.write16(addr + 6, checksum);
    uint32_t prng = checksum;
    prng = lcrng(prng);
    h.write16(addr + 8, species ^ (prng >> 16));
    prng = lcrng(prng);
    h.write16(addr + 0xA, item ^ (prng >> 16));
}

static void writePpm(const LuaCanvas& c, const std::string& path)
{
    // Composite over a checkerboard "game" background, scaled 3x
    const int S = 3;
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << 256 * S << " " << (384 + 2) * S << "\n255\n";
    for (int row = 0; row < 386 * S; ++row)
    {
        int r = row / S;
        for (int col = 0; col < 256 * S; ++col)
        {
            int x = col / S;
            unsigned char bg[3] = { 40, 90, 60 };
            if (((x / 16) + (r / 16)) % 2) { bg[0] = 70; bg[1] = 120; bg[2] = 90; }
            if (r == 192 || r == 193) { bg[0] = bg[1] = bg[2] = 0; f.write((char*) bg, 3); continue; }
            int y = r < 192 ? r - 192 : r - 194;
            uint32_t p = c.getPixel(x, y);
            float a = (p & 0xFF) / 255.0f;
            unsigned char out[3];
            for (int k = 0; k < 3; ++k)
            {
                float src = ((p >> (24 - 8 * k)) & 0xFF);
                out[k] = (unsigned char) (src * a + bg[k] * (1 - a));
            }
            f.write((char*) out, 3);
        }
    }
}

int main(int argc, char** argv)
{
    if (argc < 4) { fprintf(stderr, "usage: test script.lua out.ppm frames [key...]\n"); return 1; }
    std::ifstream in(argv[1]);
    std::stringstream ss; ss << in.rdbuf();
    int frames = atoi(argv[3]);

    FakeHost host;
    host.write32(0x02FFFE0C, 0x49555043);       // "CPUI": Platinum ITA
    host.write32(0x027FFE0C, 0x49555043);
    uint32_t base = 0x02200000;
    host.write32(0x2101E8C, base);               // pidPointerAddr (ITA)
    host.write32(0x21C08F4, 0x02210000);         // trainerIDsPointerAddr (ITA)
    host.write32(0x02210000 + 0x8C, (54321u << 16) | 12345u);
    // Jolly (13), Garchomp (445), Leftovers (item 234)
    writeWild(host, base + 0x58E3C, 0x0000000D, 445, 234, 0xBEEF);
    // Slot 2: Timid (10), Pikachu (25), Light Ball (236)
    writeWild(host, base + 0x58E3C + 0xEC, 0x0000000A, 25, 236, 0x1234);
    // RNG addresses (ITA)
    host.write32(0x21BFC74, 0x12345678);         // current seed
    host.write32(0x21BFC78, 0x12345678);         // mt seed == current -> initial seed
    host.write32(0x21BF808, 600);                // delay
    host.write32(0x2100990, 624);                // mt index

    LuaEngine engine(&host);
    engine.requestLoad(argv[1], ss.str(), "work");

    for (int i = 0; i < frames; ++i)
    {
        // Press keys given on the command line on alternate frames (as edges)
        for (int k = 4; k < argc; ++k)
        {
            std::string spec = argv[k];         // e.g. "2@3" = press key 2 at frame 3
            auto at = spec.find('@');
            std::string key = spec.substr(0, at);
            int frame = atoi(spec.substr(at + 1).c_str());
            if (i == frame) engine.setKeyState(key, true);
            if (i == frame + 1) engine.setKeyState(key, false);
        }
        engine.onFrameStart();
        engine.onFrameEnd();
        std::string out = engine.pollOutput();
        if (!out.empty()) std::cout << "[frame " << i << "] " << out;
    }
    std::cout << "status=" << (int) engine.getStatus() << " resets=" << host.resets
              << " empty=" << engine.getCanvas().isEmpty() << std::endl;
    writePpm(engine.getCanvas(), argv[2]);
    engine.shutdown();
    std::cout << engine.pollOutput();
    return 0;
}
