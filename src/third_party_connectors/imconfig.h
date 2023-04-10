#pragma once

// NOTE! Must do `make clean-lib-imgui` after changing this.

// Customize assertions.

#include "program/errors.h"
// We don't disable those even in release builds for extra safety.
#define IM_ASSERT(expr) (bool(expr) ? void() : ::Program::HardError("ImGui assertion failed: `" #expr "`."))
//#define IM_ASSERT(_EXPR)  MyAssert(_EXPR)
//#define IM_ASSERT(_EXPR)  ((void)(_EXPR))     // Disable asserts

// Remove deprecated functions.
#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS

// Remove dependency on `-limm32`.
#define IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS

// Get rid of ImGui's STB copy. We use our own.
#define IMGUI_STB_RECT_PACK_FILENAME <stb_rect_pack.h>
#define IMGUI_DISABLE_STB_RECT_PACK_IMPLEMENTATION
// Not needed, since `IMGUI_ENABLE_FREETYPE` disables `stb_truetype`.
// If it fails to do so, you'll get a compilation
// #define IMGUI_STB_TRUETYPE_FILENAME <stb_truetype.h>
// #define IMGUI_DISABLE_STB_TRUETYPE_IMPLEMENTATION

// Use freetype by default.
#define IMGUI_ENABLE_FREETYPE

// Add stuff to `ImVec*` structs.
#include "utils/mat.h"
#define IM_VEC2_CLASS_EXTRA                                                     \
        template <typename T> ImVec2(const vec2<T> &f) { x = f.x; y = f.y; }    \
        template <typename T> operator vec2<T>() const { return vec2<T>(x,y); }
#define IM_VEC4_CLASS_EXTRA                                                                    \
        template <typename T> ImVec4(const vec4<T>& f) { x = f.x; y = f.y; z = f.z; w = f.w; } \
        template <typename T> operator vec4<T>() const { return vec4<T>(x,y,z,w); }

// Set our own GL loader.
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM <cglfl/cglfl.hpp>
