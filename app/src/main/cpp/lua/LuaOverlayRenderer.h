#ifndef MELONDS_LUA_OVERLAY_RENDERER_H
#define MELONDS_LUA_OVERLAY_RENDERER_H

#include <GLES3/gl3.h>
#include "LuaCanvas.h"

namespace MelonDSAndroid::Lua
{

/**
 * Opacity (0..1) applied to the whole Lua overlay, on top of the script's own colours. User setting, any thread.
 */
void setOverlayOpacity(float opacity);

/**
 * Blends the Lua overlay canvas on top of a rendered frame texture. The frame texture layout is the one used by the
 * frontend: top screen in the first 192 rows, 2 empty rows, bottom screen in the last 192 rows, everything multiplied
 * by the renderer scale. Must be used on the emulator thread, with its OpenGL context current.
 */
class LuaOverlayRenderer
{
public:
    void render(const LuaCanvas& canvas, GLuint targetTexture, int targetWidth, int targetHeight);
    void cleanup();

private:
    bool init();
    void uploadCanvas(const LuaCanvas& canvas);

    bool initialized = false;
    bool failed = false;
    GLuint program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint texture = 0;
    GLuint framebuffer = 0;
    GLint textureUniform = -1;
    GLint opacityUniform = -1;
};

}

#endif
