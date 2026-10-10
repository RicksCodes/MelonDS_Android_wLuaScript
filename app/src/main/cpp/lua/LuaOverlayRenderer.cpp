#include "LuaOverlayRenderer.h"
#include "Platform.h"
#include "../debug/CrashHandler.h"
#include <algorithm>
#include <atomic>
#include <vector>

namespace MelonDSAndroid::Lua
{

namespace
{

std::atomic<float> overlayOpacity { 1.0f };

constexpr int kTextureWidth = LuaCanvas::kWidth;
constexpr int kTextureHeight = LuaCanvas::kScreenHeight * 2 + 2;
constexpr int kBottomScreenTextureRow = LuaCanvas::kScreenHeight + 2;

const char* kVertexShader = R"(#version 300 es
layout(location = 0) in vec2 aPosition;
out vec2 vTexCoord;
void main()
{
    vTexCoord = aPosition * 0.5 + 0.5;
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
)";

const char* kFragmentShader = R"(#version 300 es
precision mediump float;
in vec2 vTexCoord;
uniform sampler2D uOverlay;
uniform float uOpacity;
out vec4 fragColor;
void main()
{
    vec4 color = texture(uOverlay, vTexCoord);
    fragColor = vec4(color.rgb, color.a * uOpacity);
}
)";

GLuint compileShader(GLenum type, const char* source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE)
    {
        char log[512] = { 0 };
        glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
        melonDS::Platform::Log(melonDS::Platform::LogLevel::Error, "Lua overlay shader compile error: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

/**
 * Captures the GL state touched by the overlay pass and restores it afterwards, so the emulator renderers never notice
 * that the overlay was drawn.
 */
struct SavedGlState
{
    GLint drawFramebuffer, readFramebuffer;
    GLint viewport[4];
    GLint program;
    GLint vertexArray;
    GLint arrayBuffer;
    GLint pixelUnpackBuffer;
    GLint activeTexture;
    GLint texture2d;
    GLint sampler;
    GLboolean blend, scissor, depth, stencil, cull;
    GLint blendSrcRgb, blendDstRgb, blendSrcAlpha, blendDstAlpha;
    GLint blendEquationRgb, blendEquationAlpha;
    GLboolean colorMask[4];
    GLint unpackAlignment, unpackRowLength, unpackSkipRows, unpackSkipPixels;

    void save()
    {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertexArray);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &pixelUnpackBuffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture2d);
        glGetIntegerv(GL_SAMPLER_BINDING, &sampler);
        blend = glIsEnabled(GL_BLEND);
        scissor = glIsEnabled(GL_SCISSOR_TEST);
        depth = glIsEnabled(GL_DEPTH_TEST);
        stencil = glIsEnabled(GL_STENCIL_TEST);
        cull = glIsEnabled(GL_CULL_FACE);
        glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &blendEquationRgb);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blendEquationAlpha);
        glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlignment);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &unpackRowLength);
        glGetIntegerv(GL_UNPACK_SKIP_ROWS, &unpackSkipRows);
        glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &unpackSkipPixels);
    }

    void restore()
    {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFramebuffer);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glUseProgram(program);
        glBindVertexArray(vertexArray);
        glBindBuffer(GL_ARRAY_BUFFER, arrayBuffer);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pixelUnpackBuffer);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture2d);
        glBindSampler(0, sampler);
        glActiveTexture(activeTexture);
        if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        if (scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
        if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        if (stencil) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
        if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
        glBlendFuncSeparate(blendSrcRgb, blendDstRgb, blendSrcAlpha, blendDstAlpha);
        glBlendEquationSeparate(blendEquationRgb, blendEquationAlpha);
        glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
        glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlignment);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, unpackRowLength);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, unpackSkipRows);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, unpackSkipPixels);
    }
};

}

void setOverlayOpacity(float opacity)
{
    overlayOpacity.store(std::clamp(opacity, 0.0f, 1.0f), std::memory_order_relaxed);
}

bool LuaOverlayRenderer::init()
{
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, kVertexShader);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    if (!vertexShader || !fragmentShader)
    {
        if (vertexShader) glDeleteShader(vertexShader);
        if (fragmentShader) glDeleteShader(fragmentShader);
        return false;
    }

    program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint linkStatus = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
    if (linkStatus != GL_TRUE)
    {
        melonDS::Platform::Log(melonDS::Platform::LogLevel::Error, "Lua overlay program link failed\n");
        glDeleteProgram(program);
        program = 0;
        return false;
    }
    textureUniform = glGetUniformLocation(program, "uOverlay");
    opacityUniform = glGetUniformLocation(program, "uOpacity");

    const GLfloat vertices[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f,
    };

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), nullptr);

    // The gap rows between both screens are never written again, so they must start transparent
    std::vector<uint32_t> zeroes(kTextureWidth * kTextureHeight, 0);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kTextureWidth, kTextureHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, zeroes.data());

    glGenFramebuffers(1, &framebuffer);
    return true;
}

void LuaOverlayRenderer::uploadCanvas(const LuaCanvas& canvas)
{
    glBindTexture(GL_TEXTURE_2D, texture);
    const uint32_t* pixels = canvas.pixels();
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kTextureWidth, LuaCanvas::kScreenHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, kBottomScreenTextureRow, kTextureWidth, LuaCanvas::kScreenHeight, GL_RGBA, GL_UNSIGNED_BYTE,
                    pixels + LuaCanvas::kWidth * LuaCanvas::kScreenHeight);
}

void LuaOverlayRenderer::render(const LuaCanvas& canvas, GLuint targetTexture, int targetWidth, int targetHeight)
{
    if (failed || targetTexture == 0 || targetWidth <= 0 || targetHeight <= 0)
        return;

    CRASH_BREADCRUMB("Overlay: salvataggio stato OpenGL");
    SavedGlState state;
    state.save();

    // Neutral unpack state for our uploads
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);

    if (!initialized)
    {
        CRASH_BREADCRUMB("Overlay: inizializzazione (shader, texture)");
        if (!init())
        {
            failed = true;
            state.restore();
            return;
        }
        initialized = true;
    }

    CRASH_BREADCRUMB("Overlay: caricamento canvas nella texture");
    uploadCanvas(canvas);

    CRASH_BREADCRUMB("Overlay: disegno sopra il frame");
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, targetTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
    {
        GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
        glDrawBuffers(1, &drawBuffer);
        glViewport(0, 0, targetWidth, targetHeight);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glEnable(GL_BLEND);
        // Blend the colour, keep the destination alpha untouched
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);

        glUseProgram(program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glBindSampler(0, 0);
        glUniform1i(textureUniform, 0);
        glUniform1f(opacityUniform, overlayOpacity.load(std::memory_order_relaxed));
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);

    CRASH_BREADCRUMB("Overlay: ripristino stato OpenGL");
    state.restore();
}

void LuaOverlayRenderer::cleanup()
{
    if (!initialized)
        return;

    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &texture);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);
    framebuffer = texture = vbo = vao = program = 0;
    initialized = false;
    failed = false;
}

}
