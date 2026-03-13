// =============================================================================
// Drift 2D Game Engine - RendererResource Implementation
// =============================================================================
// Class-based renderer replacing the old global-state renderer.
// Sprite batching, texture management, camera system, and primitive stubs.
// =============================================================================

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <drift/resources/RendererResource.hpp>
#include <drift/App.hpp>
#include <drift/Math.hpp>

#include <SDL3/SDL.h>

#include "core/HandlePool.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

namespace drift {

// =============================================================================
// Internal types
// =============================================================================

struct TextureEntry {
    SDL_GPUTexture* gpu_texture = nullptr;
    int32_t         width       = 0;
    int32_t         height      = 0;
};

struct CameraData {
    Vec2  position = {};
    float zoom     = 1.0f;
    float rotation = 0.0f;
};

struct SpriteInstance {
    Vec2    position;
    Vec2    scale;
    float   rotation;
    Rect    src_rect;           // in pixel coordinates of the source texture
    Color   tint;
    Vec2    origin;
    Flip    flip;
    float   z_order;
    TextureHandle texture;
    int32_t tex_w;              // cached texture width  (for UV calc)
    int32_t tex_h;              // cached texture height (for UV calc)
};

struct SpriteVertex {
    float x, y;                 // position (world -> projected)
    float u, v;                 // texture coordinates
    uint8_t r, g, b, a;        // vertex colour / tint
};

// =============================================================================
// Embedded SPIR-V shader bytecode
// =============================================================================

static const unsigned char k_sprite_vert_spv[] = {
  0x03, 0x02, 0x23, 0x07, 0x00, 0x00, 0x01, 0x00, 0x0b, 0x00, 0x0d, 0x00,
  0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x02, 0x00,
  0x01, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x06, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x47, 0x4c, 0x53, 0x4c, 0x2e, 0x73, 0x74, 0x64, 0x2e, 0x34, 0x35, 0x30,
  0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x01, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x04, 0x00, 0x00, 0x00, 0x6d, 0x61, 0x69, 0x6e, 0x00, 0x00, 0x00, 0x00,
  0x0d, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x00, 0x00,
  0x1d, 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00,
  0x03, 0x00, 0x03, 0x00, 0x02, 0x00, 0x00, 0x00, 0xc2, 0x01, 0x00, 0x00,
  0x04, 0x00, 0x0a, 0x00, 0x47, 0x4c, 0x5f, 0x47, 0x4f, 0x4f, 0x47, 0x4c,
  0x45, 0x5f, 0x63, 0x70, 0x70, 0x5f, 0x73, 0x74, 0x79, 0x6c, 0x65, 0x5f,
  0x6c, 0x69, 0x6e, 0x65, 0x5f, 0x64, 0x69, 0x72, 0x65, 0x63, 0x74, 0x69,
  0x76, 0x65, 0x00, 0x00, 0x04, 0x00, 0x08, 0x00, 0x47, 0x4c, 0x5f, 0x47,
  0x4f, 0x4f, 0x47, 0x4c, 0x45, 0x5f, 0x69, 0x6e, 0x63, 0x6c, 0x75, 0x64,
  0x65, 0x5f, 0x64, 0x69, 0x72, 0x65, 0x63, 0x74, 0x69, 0x76, 0x65, 0x00,
  0x05, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00, 0x00, 0x6d, 0x61, 0x69, 0x6e,
  0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x06, 0x00, 0x0b, 0x00, 0x00, 0x00,
  0x67, 0x6c, 0x5f, 0x50, 0x65, 0x72, 0x56, 0x65, 0x72, 0x74, 0x65, 0x78,
  0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x06, 0x00, 0x0b, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x67, 0x6c, 0x5f, 0x50, 0x6f, 0x73, 0x69, 0x74,
  0x69, 0x6f, 0x6e, 0x00, 0x06, 0x00, 0x07, 0x00, 0x0b, 0x00, 0x00, 0x00,
  0x01, 0x00, 0x00, 0x00, 0x67, 0x6c, 0x5f, 0x50, 0x6f, 0x69, 0x6e, 0x74,
  0x53, 0x69, 0x7a, 0x65, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x07, 0x00,
  0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x67, 0x6c, 0x5f, 0x43,
  0x6c, 0x69, 0x70, 0x44, 0x69, 0x73, 0x74, 0x61, 0x6e, 0x63, 0x65, 0x00,
  0x06, 0x00, 0x07, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
  0x67, 0x6c, 0x5f, 0x43, 0x75, 0x6c, 0x6c, 0x44, 0x69, 0x73, 0x74, 0x61,
  0x6e, 0x63, 0x65, 0x00, 0x05, 0x00, 0x03, 0x00, 0x0d, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x05, 0x00, 0x12, 0x00, 0x00, 0x00,
  0x69, 0x6e, 0x5f, 0x70, 0x6f, 0x73, 0x69, 0x74, 0x69, 0x6f, 0x6e, 0x00,
  0x05, 0x00, 0x06, 0x00, 0x1c, 0x00, 0x00, 0x00, 0x66, 0x72, 0x61, 0x67,
  0x5f, 0x74, 0x65, 0x78, 0x63, 0x6f, 0x6f, 0x72, 0x64, 0x00, 0x00, 0x00,
  0x05, 0x00, 0x05, 0x00, 0x1d, 0x00, 0x00, 0x00, 0x69, 0x6e, 0x5f, 0x74,
  0x65, 0x78, 0x63, 0x6f, 0x6f, 0x72, 0x64, 0x00, 0x05, 0x00, 0x05, 0x00,
  0x1f, 0x00, 0x00, 0x00, 0x66, 0x72, 0x61, 0x67, 0x5f, 0x63, 0x6f, 0x6c,
  0x6f, 0x72, 0x00, 0x00, 0x05, 0x00, 0x05, 0x00, 0x21, 0x00, 0x00, 0x00,
  0x69, 0x6e, 0x5f, 0x63, 0x6f, 0x6c, 0x6f, 0x72, 0x00, 0x00, 0x00, 0x00,
  0x47, 0x00, 0x03, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
  0x48, 0x00, 0x05, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x00, 0x05, 0x00,
  0x0b, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00,
  0x01, 0x00, 0x00, 0x00, 0x48, 0x00, 0x05, 0x00, 0x0b, 0x00, 0x00, 0x00,
  0x02, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
  0x48, 0x00, 0x05, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
  0x0b, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x47, 0x00, 0x04, 0x00,
  0x12, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x47, 0x00, 0x04, 0x00, 0x1c, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x47, 0x00, 0x04, 0x00, 0x1d, 0x00, 0x00, 0x00,
  0x1e, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x47, 0x00, 0x04, 0x00,
  0x1f, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x47, 0x00, 0x04, 0x00, 0x21, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00,
  0x02, 0x00, 0x00, 0x00, 0x13, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00,
  0x21, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
  0x16, 0x00, 0x03, 0x00, 0x06, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
  0x17, 0x00, 0x04, 0x00, 0x07, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
  0x04, 0x00, 0x00, 0x00, 0x15, 0x00, 0x04, 0x00, 0x08, 0x00, 0x00, 0x00,
  0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x04, 0x00,
  0x08, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x1c, 0x00, 0x04, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
  0x09, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x06, 0x00, 0x0b, 0x00, 0x00, 0x00,
  0x07, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00,
  0x0a, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00, 0x0c, 0x00, 0x00, 0x00,
  0x03, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00,
  0x0c, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
  0x15, 0x00, 0x04, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
  0x01, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x04, 0x00, 0x0e, 0x00, 0x00, 0x00,
  0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x17, 0x00, 0x04, 0x00,
  0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
  0x20, 0x00, 0x04, 0x00, 0x11, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x10, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00, 0x11, 0x00, 0x00, 0x00,
  0x12, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x04, 0x00,
  0x06, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x2b, 0x00, 0x04, 0x00, 0x06, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x80, 0x3f, 0x20, 0x00, 0x04, 0x00, 0x19, 0x00, 0x00, 0x00,
  0x03, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00,
  0x1b, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
  0x3b, 0x00, 0x04, 0x00, 0x1b, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x00, 0x00,
  0x03, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00, 0x11, 0x00, 0x00, 0x00,
  0x1d, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00,
  0x19, 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
  0x20, 0x00, 0x04, 0x00, 0x20, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x07, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00, 0x20, 0x00, 0x00, 0x00,
  0x21, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x36, 0x00, 0x05, 0x00,
  0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x03, 0x00, 0x00, 0x00, 0xf8, 0x00, 0x02, 0x00, 0x05, 0x00, 0x00, 0x00,
  0x3d, 0x00, 0x04, 0x00, 0x10, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00,
  0x12, 0x00, 0x00, 0x00, 0x51, 0x00, 0x05, 0x00, 0x06, 0x00, 0x00, 0x00,
  0x16, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x51, 0x00, 0x05, 0x00, 0x06, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00,
  0x13, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x50, 0x00, 0x07, 0x00,
  0x07, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00,
  0x17, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00,
  0x41, 0x00, 0x05, 0x00, 0x19, 0x00, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x00,
  0x0d, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x03, 0x00,
  0x1a, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x3d, 0x00, 0x04, 0x00,
  0x10, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x1d, 0x00, 0x00, 0x00,
  0x3e, 0x00, 0x03, 0x00, 0x1c, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00,
  0x3d, 0x00, 0x04, 0x00, 0x07, 0x00, 0x00, 0x00, 0x22, 0x00, 0x00, 0x00,
  0x21, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x03, 0x00, 0x1f, 0x00, 0x00, 0x00,
  0x22, 0x00, 0x00, 0x00, 0xfd, 0x00, 0x01, 0x00, 0x38, 0x00, 0x01, 0x00
};

static const unsigned char k_sprite_frag_spv[] = {
  0x03, 0x02, 0x23, 0x07, 0x00, 0x00, 0x01, 0x00, 0x0b, 0x00, 0x0d, 0x00,
  0x1b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x02, 0x00,
  0x01, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x06, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x47, 0x4c, 0x53, 0x4c, 0x2e, 0x73, 0x74, 0x64, 0x2e, 0x34, 0x35, 0x30,
  0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x01, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x08, 0x00, 0x04, 0x00, 0x00, 0x00,
  0x04, 0x00, 0x00, 0x00, 0x6d, 0x61, 0x69, 0x6e, 0x00, 0x00, 0x00, 0x00,
  0x11, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00,
  0x10, 0x00, 0x03, 0x00, 0x04, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
  0x03, 0x00, 0x03, 0x00, 0x02, 0x00, 0x00, 0x00, 0xc2, 0x01, 0x00, 0x00,
  0x04, 0x00, 0x0a, 0x00, 0x47, 0x4c, 0x5f, 0x47, 0x4f, 0x4f, 0x47, 0x4c,
  0x45, 0x5f, 0x63, 0x70, 0x70, 0x5f, 0x73, 0x74, 0x79, 0x6c, 0x65, 0x5f,
  0x6c, 0x69, 0x6e, 0x65, 0x5f, 0x64, 0x69, 0x72, 0x65, 0x63, 0x74, 0x69,
  0x76, 0x65, 0x00, 0x00, 0x04, 0x00, 0x08, 0x00, 0x47, 0x4c, 0x5f, 0x47,
  0x4f, 0x4f, 0x47, 0x4c, 0x45, 0x5f, 0x69, 0x6e, 0x63, 0x6c, 0x75, 0x64,
  0x65, 0x5f, 0x64, 0x69, 0x72, 0x65, 0x63, 0x74, 0x69, 0x76, 0x65, 0x00,
  0x05, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00, 0x00, 0x6d, 0x61, 0x69, 0x6e,
  0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x05, 0x00, 0x09, 0x00, 0x00, 0x00,
  0x74, 0x65, 0x78, 0x5f, 0x63, 0x6f, 0x6c, 0x6f, 0x72, 0x00, 0x00, 0x00,
  0x05, 0x00, 0x05, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x74, 0x65, 0x78, 0x5f,
  0x73, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x72, 0x00, 0x05, 0x00, 0x06, 0x00,
  0x11, 0x00, 0x00, 0x00, 0x66, 0x72, 0x61, 0x67, 0x5f, 0x74, 0x65, 0x78,
  0x63, 0x6f, 0x6f, 0x72, 0x64, 0x00, 0x00, 0x00, 0x05, 0x00, 0x05, 0x00,
  0x15, 0x00, 0x00, 0x00, 0x6f, 0x75, 0x74, 0x5f, 0x63, 0x6f, 0x6c, 0x6f,
  0x72, 0x00, 0x00, 0x00, 0x05, 0x00, 0x05, 0x00, 0x18, 0x00, 0x00, 0x00,
  0x66, 0x72, 0x61, 0x67, 0x5f, 0x63, 0x6f, 0x6c, 0x6f, 0x72, 0x00, 0x00,
  0x47, 0x00, 0x04, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x47, 0x00, 0x04, 0x00, 0x0d, 0x00, 0x00, 0x00,
  0x22, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x47, 0x00, 0x04, 0x00,
  0x11, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x47, 0x00, 0x04, 0x00, 0x15, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x47, 0x00, 0x04, 0x00, 0x18, 0x00, 0x00, 0x00,
  0x1e, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x13, 0x00, 0x02, 0x00,
  0x02, 0x00, 0x00, 0x00, 0x21, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00,
  0x02, 0x00, 0x00, 0x00, 0x16, 0x00, 0x03, 0x00, 0x06, 0x00, 0x00, 0x00,
  0x20, 0x00, 0x00, 0x00, 0x17, 0x00, 0x04, 0x00, 0x07, 0x00, 0x00, 0x00,
  0x06, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00,
  0x08, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
  0x19, 0x00, 0x09, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x1b, 0x00, 0x03, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00,
  0x20, 0x00, 0x04, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x0b, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00, 0x0c, 0x00, 0x00, 0x00,
  0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x17, 0x00, 0x04, 0x00,
  0x0f, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
  0x20, 0x00, 0x04, 0x00, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x0f, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00, 0x10, 0x00, 0x00, 0x00,
  0x11, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00,
  0x14, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
  0x3b, 0x00, 0x04, 0x00, 0x14, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00,
  0x03, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00, 0x17, 0x00, 0x00, 0x00,
  0x01, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00,
  0x17, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x36, 0x00, 0x05, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0xf8, 0x00, 0x02, 0x00,
  0x05, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00, 0x08, 0x00, 0x00, 0x00,
  0x09, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x3d, 0x00, 0x04, 0x00,
  0x0b, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00,
  0x3d, 0x00, 0x04, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00,
  0x11, 0x00, 0x00, 0x00, 0x57, 0x00, 0x05, 0x00, 0x07, 0x00, 0x00, 0x00,
  0x13, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00,
  0x3e, 0x00, 0x03, 0x00, 0x09, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00,
  0x3d, 0x00, 0x04, 0x00, 0x07, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00,
  0x09, 0x00, 0x00, 0x00, 0x3d, 0x00, 0x04, 0x00, 0x07, 0x00, 0x00, 0x00,
  0x19, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x85, 0x00, 0x05, 0x00,
  0x07, 0x00, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00,
  0x19, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x03, 0x00, 0x15, 0x00, 0x00, 0x00,
  0x1a, 0x00, 0x00, 0x00, 0xfd, 0x00, 0x01, 0x00, 0x38, 0x00, 0x01, 0x00
};

// =============================================================================
// Impl struct -- all renderer state lives here
// =============================================================================

struct RendererResource::Impl {
    SDL_Window*    window = nullptr;
    SDL_GPUDevice* device = nullptr;

    // Texture storage
    HandlePool<TextureTag, TextureEntry> textures;
    TextureHandle whiteTexture;

    // Camera storage
    HandlePool<CameraTag, CameraData> cameras;
    CameraHandle activeCamera;
    CameraHandle defaultCamera;

    // Sprite batching
    std::vector<SpriteInstance> spriteQueue;

    // GPU pipeline and resources
    SDL_GPUGraphicsPipeline* spritePipeline = nullptr;
    SDL_GPUBuffer*           vertexBuffer   = nullptr;
    SDL_GPUBuffer*           indexBuffer    = nullptr;
    uint32_t                 vertexBufCap   = 0; // in bytes
    uint32_t                 indexBufCap    = 0;

    SDL_GPUSampler*          nearestSampler = nullptr;

    SDL_GPUCommandBuffer*    cmdBuf         = nullptr;

    // Clear colour
    ColorF clearColor = {0.0f, 0.0f, 0.0f, 1.0f};

    // Stats
    int32_t drawCallCount  = 0;
    int32_t spriteCountVal = 0;
};

// =============================================================================
// Helpers: GPU resource management (static, operating on device pointer)
// =============================================================================

static SDL_GPUTexture* create_gpu_texture(SDL_GPUDevice* dev, int w, int h,
                                          const uint8_t* pixels) {
    SDL_GPUTextureCreateInfo tex_ci{};
    tex_ci.type            = SDL_GPU_TEXTURETYPE_2D;
    tex_ci.format          = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tex_ci.width           = static_cast<Uint32>(w);
    tex_ci.height          = static_cast<Uint32>(h);
    tex_ci.layer_count_or_depth = 1;
    tex_ci.num_levels      = 1;
    tex_ci.usage           = SDL_GPU_TEXTUREUSAGE_SAMPLER;

    SDL_GPUTexture* tex = SDL_CreateGPUTexture(dev, &tex_ci);
    if (!tex) {
        SDL_Log("RendererResource: failed to create GPU texture (%dx%d): %s",
                w, h, SDL_GetError());
        return nullptr;
    }

    // Upload via transfer buffer.
    uint32_t byte_size = static_cast<uint32_t>(w * h * 4);

    SDL_GPUTransferBufferCreateInfo tb_ci{};
    tb_ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tb_ci.size  = byte_size;

    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev, &tb_ci);
    if (!tb) {
        SDL_Log("RendererResource: failed to create transfer buffer: %s", SDL_GetError());
        SDL_ReleaseGPUTexture(dev, tex);
        return nullptr;
    }

    void* mapped = SDL_MapGPUTransferBuffer(dev, tb, false);
    if (mapped) {
        memcpy(mapped, pixels, byte_size);
    }
    SDL_UnmapGPUTransferBuffer(dev, tb);

    // Copy pass.
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = tb;
    src.offset          = 0;

    SDL_GPUTextureRegion dst{};
    dst.texture = tex;
    dst.w       = static_cast<Uint32>(w);
    dst.h       = static_cast<Uint32>(h);
    dst.d       = 1;

    SDL_UploadToGPUTexture(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);

    SDL_ReleaseGPUTransferBuffer(dev, tb);

    return tex;
}

static void ensure_gpu_buffer(SDL_GPUDevice* dev,
                              SDL_GPUBuffer** buf, uint32_t* cap,
                              uint32_t needed, SDL_GPUBufferUsageFlags usage) {
    if (*cap >= needed) return;

    if (*buf) {
        SDL_ReleaseGPUBuffer(dev, *buf);
    }

    // Round up to next power of two.
    uint32_t new_cap = 4096;
    while (new_cap < needed) new_cap <<= 1;

    SDL_GPUBufferCreateInfo ci{};
    ci.usage = usage;
    ci.size  = new_cap;

    *buf = SDL_CreateGPUBuffer(dev, &ci);
    *cap = new_cap;
}

// Upload raw bytes to a GPU buffer via a transient transfer buffer.
static void upload_to_gpu_buffer(SDL_GPUDevice* dev, SDL_GPUBuffer* dst,
                                 const void* data, uint32_t size) {
    if (size == 0) return;

    SDL_GPUTransferBufferCreateInfo tb_ci{};
    tb_ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tb_ci.size  = size;

    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev, &tb_ci);
    if (!tb) return;

    void* mapped = SDL_MapGPUTransferBuffer(dev, tb, false);
    if (mapped) {
        memcpy(mapped, data, size);
    }
    SDL_UnmapGPUTransferBuffer(dev, tb);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTransferBufferLocation src{};
    src.transfer_buffer = tb;
    src.offset          = 0;

    SDL_GPUBufferRegion dst_region{};
    dst_region.buffer = dst;
    dst_region.offset = 0;
    dst_region.size   = size;

    SDL_UploadToGPUBuffer(copy, &src, &dst_region, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);

    SDL_ReleaseGPUTransferBuffer(dev, tb);
}

// =============================================================================
// Sprite sort comparator: sort by z_order then by texture handle id for batching.
// =============================================================================
static bool sprite_sort_cmp(const SpriteInstance& a, const SpriteInstance& b) {
    if (a.z_order != b.z_order) return a.z_order < b.z_order;
    return a.texture.id < b.texture.id;
}

// =============================================================================
// Build combined view-projection from active camera and window size
// =============================================================================
static Mat3 build_view_projection(SDL_Window* window, const HandlePool<CameraTag, CameraData>& cameras, CameraHandle activeCamera) {
    int w_px = 0, h_px = 0;
    SDL_GetWindowSize(window, &w_px, &h_px);
    if (w_px == 0 || h_px == 0) {
        return Mat3::identity();
    }

    float wf = static_cast<float>(w_px);
    float hf = static_cast<float>(h_px);

    Mat3 proj = Mat3::ortho(wf, hf);

    if (!activeCamera.valid()) {
        return proj; // no camera -> identity view
    }

    const CameraData* cam = cameras.get(activeCamera);
    if (!cam) {
        return proj;
    }

    // View = translate(-cam.pos) * rotate(-cam.rot) * scale(cam.zoom)
    // Then shift so that the camera centre is at the screen centre.
    Mat3 view = Mat3::identity();
    view = Mat3::translate(-cam->position.x, -cam->position.y) * view;
    view = Mat3::rotate(-cam->rotation) * view;
    view = Mat3::scale(cam->zoom, cam->zoom) * view;
    view = Mat3::translate(wf * 0.5f, hf * 0.5f) * view;

    return proj * view;
}

// =============================================================================
// Constructor
// =============================================================================

RendererResource::RendererResource(App& app)
    : impl_(new Impl)
{
    impl_->window = static_cast<SDL_Window*>(app.sdlWindow());
    impl_->device = static_cast<SDL_GPUDevice*>(app.gpuDevice());

    SDL_GPUDevice* dev = impl_->device;
    if (!dev) {
        SDL_Log("RendererResource: GPU device not available.");
        return;
    }

    // Create a nearest-neighbour sampler (good default for pixel art).
    SDL_GPUSamplerCreateInfo samp_ci{};
    samp_ci.min_filter       = SDL_GPU_FILTER_NEAREST;
    samp_ci.mag_filter       = SDL_GPU_FILTER_NEAREST;
    samp_ci.mipmap_mode      = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    samp_ci.address_mode_u   = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samp_ci.address_mode_v   = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samp_ci.address_mode_w   = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

    impl_->nearestSampler = SDL_CreateGPUSampler(dev, &samp_ci);

    // Create a 1x1 white texture for untextured draws.
    uint8_t white_pixel[4] = {255, 255, 255, 255};
    SDL_GPUTexture* white_gpu = create_gpu_texture(dev, 1, 1, white_pixel);
    if (white_gpu) {
        TextureEntry entry;
        entry.gpu_texture = white_gpu;
        entry.width       = 1;
        entry.height      = 1;
        impl_->whiteTexture = impl_->textures.create(entry);
    }

    // ------------------------------------------------------------------
    // Create sprite rendering pipeline with embedded SPIR-V shaders.
    // ------------------------------------------------------------------
    SDL_Window* win = impl_->window;
    SDL_GPUShaderFormat supported_formats = SDL_GetGPUShaderFormats(dev);

    if (win && (supported_formats & SDL_GPU_SHADERFORMAT_SPIRV)) {
        // Vertex shader.
        SDL_GPUShaderCreateInfo vert_ci{};
        vert_ci.code        = k_sprite_vert_spv;
        vert_ci.code_size   = sizeof(k_sprite_vert_spv);
        vert_ci.entrypoint  = "main";
        vert_ci.format      = SDL_GPU_SHADERFORMAT_SPIRV;
        vert_ci.stage       = SDL_GPU_SHADERSTAGE_VERTEX;
        vert_ci.num_samplers          = 0;
        vert_ci.num_storage_textures  = 0;
        vert_ci.num_storage_buffers   = 0;
        vert_ci.num_uniform_buffers   = 0;

        SDL_GPUShader* vert_shader = SDL_CreateGPUShader(dev, &vert_ci);

        // Fragment shader (1 combined texture-sampler).
        SDL_GPUShaderCreateInfo frag_ci{};
        frag_ci.code        = k_sprite_frag_spv;
        frag_ci.code_size   = sizeof(k_sprite_frag_spv);
        frag_ci.entrypoint  = "main";
        frag_ci.format      = SDL_GPU_SHADERFORMAT_SPIRV;
        frag_ci.stage       = SDL_GPU_SHADERSTAGE_FRAGMENT;
        frag_ci.num_samplers          = 1;
        frag_ci.num_storage_textures  = 0;
        frag_ci.num_storage_buffers   = 0;
        frag_ci.num_uniform_buffers   = 0;

        SDL_GPUShader* frag_shader = SDL_CreateGPUShader(dev, &frag_ci);

        if (vert_shader && frag_shader) {
            // Vertex buffer layout: SpriteVertex {float x,y; float u,v; u8 r,g,b,a}
            SDL_GPUVertexBufferDescription vb_desc{};
            vb_desc.slot            = 0;
            vb_desc.pitch           = static_cast<Uint32>(sizeof(SpriteVertex));
            vb_desc.input_rate      = SDL_GPU_VERTEXINPUTRATE_VERTEX;
            vb_desc.instance_step_rate = 0;

            SDL_GPUVertexAttribute attrs[3]{};
            attrs[0].location    = 0;
            attrs[0].buffer_slot = 0;
            attrs[0].format      = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
            attrs[0].offset      = offsetof(SpriteVertex, x);
            attrs[1].location    = 1;
            attrs[1].buffer_slot = 0;
            attrs[1].format      = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
            attrs[1].offset      = offsetof(SpriteVertex, u);
            attrs[2].location    = 2;
            attrs[2].buffer_slot = 0;
            attrs[2].format      = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
            attrs[2].offset      = offsetof(SpriteVertex, r);

            // Alpha-blending colour target.
            SDL_GPUColorTargetDescription ct_desc{};
            ct_desc.format = SDL_GetGPUSwapchainTextureFormat(dev, win);
            ct_desc.blend_state.enable_blend           = true;
            ct_desc.blend_state.src_color_blendfactor   = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            ct_desc.blend_state.dst_color_blendfactor   = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            ct_desc.blend_state.color_blend_op          = SDL_GPU_BLENDOP_ADD;
            ct_desc.blend_state.src_alpha_blendfactor    = SDL_GPU_BLENDFACTOR_ONE;
            ct_desc.blend_state.dst_alpha_blendfactor    = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            ct_desc.blend_state.alpha_blend_op           = SDL_GPU_BLENDOP_ADD;

            SDL_GPUGraphicsPipelineCreateInfo pip_ci{};
            pip_ci.vertex_shader   = vert_shader;
            pip_ci.fragment_shader = frag_shader;
            pip_ci.vertex_input_state.vertex_buffer_descriptions = &vb_desc;
            pip_ci.vertex_input_state.num_vertex_buffers         = 1;
            pip_ci.vertex_input_state.vertex_attributes          = attrs;
            pip_ci.vertex_input_state.num_vertex_attributes      = 3;
            pip_ci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            pip_ci.rasterizer_state.fill_mode  = SDL_GPU_FILLMODE_FILL;
            pip_ci.rasterizer_state.cull_mode  = SDL_GPU_CULLMODE_NONE;
            pip_ci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
            pip_ci.target_info.color_target_descriptions = &ct_desc;
            pip_ci.target_info.num_color_targets         = 1;
            pip_ci.target_info.has_depth_stencil_target  = false;

            impl_->spritePipeline = SDL_CreateGPUGraphicsPipeline(dev, &pip_ci);
            if (!impl_->spritePipeline) {
                SDL_Log("RendererResource: failed to create sprite pipeline: %s",
                        SDL_GetError());
            }
        } else {
            SDL_Log("RendererResource: failed to create GPU shaders: vert=%p frag=%p: %s",
                    (void*)vert_shader, (void*)frag_shader, SDL_GetError());
        }

        if (vert_shader) SDL_ReleaseGPUShader(dev, vert_shader);
        if (frag_shader) SDL_ReleaseGPUShader(dev, frag_shader);
    } else {
        SDL_Log("RendererResource: SPIR-V not supported or no window. Sprites won't render.");
        impl_->spritePipeline = nullptr;
    }

    // Create a default camera centred on the window with zoom=1.
    {
        int w_px = 0, h_px = 0;
        if (impl_->window) {
            SDL_GetWindowSize(impl_->window, &w_px, &h_px);
        }
        CameraData cam;
        cam.position = {static_cast<float>(w_px) * 0.5f,
                        static_cast<float>(h_px) * 0.5f};
        cam.zoom     = 1.0f;
        cam.rotation = 0.0f;
        impl_->defaultCamera = impl_->cameras.create(cam);
        impl_->activeCamera  = impl_->defaultCamera;
    }

    SDL_Log("RendererResource: renderer initialised.");
}

// =============================================================================
// Destructor
// =============================================================================

RendererResource::~RendererResource() {
    if (!impl_) return;

    SDL_GPUDevice* dev = impl_->device;
    if (dev) {
        // Wait for the GPU to be idle before releasing resources.
        SDL_WaitForGPUIdle(dev);

        // Release all textures.
        impl_->textures.forEach([dev](TextureHandle, TextureEntry& entry) {
            if (entry.gpu_texture) {
                SDL_ReleaseGPUTexture(dev, entry.gpu_texture);
                entry.gpu_texture = nullptr;
            }
        });

        // Release sampler.
        if (impl_->nearestSampler) {
            SDL_ReleaseGPUSampler(dev, impl_->nearestSampler);
            impl_->nearestSampler = nullptr;
        }

        // Release buffers.
        if (impl_->vertexBuffer) {
            SDL_ReleaseGPUBuffer(dev, impl_->vertexBuffer);
            impl_->vertexBuffer = nullptr;
            impl_->vertexBufCap = 0;
        }
        if (impl_->indexBuffer) {
            SDL_ReleaseGPUBuffer(dev, impl_->indexBuffer);
            impl_->indexBuffer = nullptr;
            impl_->indexBufCap = 0;
        }

        // Release pipeline.
        if (impl_->spritePipeline) {
            SDL_ReleaseGPUGraphicsPipeline(dev, impl_->spritePipeline);
            impl_->spritePipeline = nullptr;
        }
    }

    SDL_Log("RendererResource: renderer shut down.");

    delete impl_;
    impl_ = nullptr;
}

// =============================================================================
// Frame lifecycle
// =============================================================================

void RendererResource::beginFrame() {
    impl_->spriteQueue.clear();
    impl_->drawCallCount  = 0;
    impl_->spriteCountVal = 0;

    SDL_GPUDevice* dev = impl_->device;
    if (dev) {
        impl_->cmdBuf = SDL_AcquireGPUCommandBuffer(dev);
    }
}

void RendererResource::endFrame() {
    if (!impl_->cmdBuf) return;

    SDL_GPUDevice* dev = impl_->device;
    SDL_Window*    win = impl_->window;
    if (!dev || !win) return;

    // --- Sort sprites by z_order then texture for batching ---
    std::sort(impl_->spriteQueue.begin(), impl_->spriteQueue.end(), sprite_sort_cmp);

    // --- Build vertex / index data ---
    const size_t sprite_count = impl_->spriteQueue.size();
    impl_->spriteCountVal = static_cast<int32_t>(sprite_count);

    std::vector<SpriteVertex> vertices;
    std::vector<uint16_t>     indices;
    vertices.reserve(sprite_count * 4);
    indices.reserve(sprite_count * 6);

    Mat3 vp = build_view_projection(impl_->window, impl_->cameras, impl_->activeCamera);

    for (size_t i = 0; i < sprite_count; ++i) {
        const SpriteInstance& s = impl_->spriteQueue[i];

        // Source rect UVs (normalised).
        float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
        if (s.tex_w > 0 && s.tex_h > 0) {
            u0 = s.src_rect.x / static_cast<float>(s.tex_w);
            v0 = s.src_rect.y / static_cast<float>(s.tex_h);
            u1 = (s.src_rect.x + s.src_rect.w) / static_cast<float>(s.tex_w);
            v1 = (s.src_rect.y + s.src_rect.h) / static_cast<float>(s.tex_h);
        }

        // Handle flipping.
        if (static_cast<int>(s.flip) & static_cast<int>(Flip::H)) {
            float tmp = u0; u0 = u1; u1 = tmp;
        }
        if (static_cast<int>(s.flip) & static_cast<int>(Flip::V)) {
            float tmp = v0; v0 = v1; v1 = tmp;
        }

        // Local quad corners relative to origin before transform.
        float qw = s.src_rect.w * s.scale.x;
        float qh = s.src_rect.h * s.scale.y;

        float ox = s.origin.x * s.scale.x;
        float oy = s.origin.y * s.scale.y;

        // Four corner offsets (before rotation).
        float lx[4] = { -ox,      qw - ox, qw - ox, -ox      };
        float ly[4] = { -oy,      -oy,     qh - oy, qh - oy  };

        float cosR = cosf(s.rotation);
        float sinR = sinf(s.rotation);

        float uv_u[4] = { u0, u1, u1, u0 };
        float uv_v[4] = { v0, v0, v1, v1 };

        uint16_t base = static_cast<uint16_t>(vertices.size());

        for (int j = 0; j < 4; ++j) {
            // Rotate around origin, then translate to world position.
            float wx = s.position.x + lx[j] * cosR - ly[j] * sinR;
            float wy = s.position.y + lx[j] * sinR + ly[j] * cosR;

            // Apply view-projection.
            float px, py;
            vp.transformPoint(wx, wy, &px, &py);

            SpriteVertex sv;
            sv.x = px;
            sv.y = py;
            sv.u = uv_u[j];
            sv.v = uv_v[j];
            sv.r = s.tint.r;
            sv.g = s.tint.g;
            sv.b = s.tint.b;
            sv.a = s.tint.a;
            vertices.push_back(sv);
        }

        // Two triangles: 0-1-2, 0-2-3
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }

    // --- Upload vertex / index data to GPU buffers ---
    uint32_t vb_size = static_cast<uint32_t>(vertices.size() * sizeof(SpriteVertex));
    uint32_t ib_size = static_cast<uint32_t>(indices.size() * sizeof(uint16_t));

    if (vb_size > 0) {
        ensure_gpu_buffer(dev, &impl_->vertexBuffer, &impl_->vertexBufCap, vb_size,
                          SDL_GPU_BUFFERUSAGE_VERTEX);
        upload_to_gpu_buffer(dev, impl_->vertexBuffer, vertices.data(), vb_size);
    }
    if (ib_size > 0) {
        ensure_gpu_buffer(dev, &impl_->indexBuffer, &impl_->indexBufCap, ib_size,
                          SDL_GPU_BUFFERUSAGE_INDEX);
        upload_to_gpu_buffer(dev, impl_->indexBuffer, indices.data(), ib_size);
    }

    // --- Acquire swapchain texture and begin render pass ---
    SDL_GPUTexture* swapchain_tex = nullptr;
    Uint32 sc_w = 0, sc_h = 0;
    if (!SDL_AcquireGPUSwapchainTexture(impl_->cmdBuf, win, &swapchain_tex, &sc_w, &sc_h)) {
        SDL_Log("RendererResource: failed to acquire swapchain texture.");
        SDL_SubmitGPUCommandBuffer(impl_->cmdBuf);
        impl_->cmdBuf = nullptr;
        return;
    }
    if (!swapchain_tex) {
        // Window minimised or not ready; just submit and bail.
        SDL_SubmitGPUCommandBuffer(impl_->cmdBuf);
        impl_->cmdBuf = nullptr;
        return;
    }

    SDL_GPUColorTargetInfo color_target{};
    color_target.texture     = swapchain_tex;
    color_target.load_op     = SDL_GPU_LOADOP_CLEAR;
    color_target.store_op    = SDL_GPU_STOREOP_STORE;
    color_target.clear_color.r = impl_->clearColor.r;
    color_target.clear_color.g = impl_->clearColor.g;
    color_target.clear_color.b = impl_->clearColor.b;
    color_target.clear_color.a = impl_->clearColor.a;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(impl_->cmdBuf, &color_target, 1, nullptr);

    // --- Issue batched draw calls (one per texture run) ---
    if (pass && impl_->spritePipeline && sprite_count > 0) {
        SDL_BindGPUGraphicsPipeline(pass, impl_->spritePipeline);

        SDL_GPUBufferBinding vb_bind{};
        vb_bind.buffer = impl_->vertexBuffer;
        vb_bind.offset = 0;
        SDL_BindGPUVertexBuffers(pass, 0, &vb_bind, 1);

        SDL_GPUBufferBinding ib_bind{};
        ib_bind.buffer = impl_->indexBuffer;
        ib_bind.offset = 0;
        SDL_BindGPUIndexBuffer(pass, &ib_bind, SDL_GPU_INDEXELEMENTSIZE_16BIT);

        // Walk sorted sprites and emit one draw per texture batch.
        size_t batch_start = 0;
        while (batch_start < sprite_count) {
            uint32_t cur_tex_id = impl_->spriteQueue[batch_start].texture.id;
            size_t batch_end = batch_start + 1;
            while (batch_end < sprite_count &&
                   impl_->spriteQueue[batch_end].texture.id == cur_tex_id) {
                ++batch_end;
            }

            // Bind the texture + sampler for this batch.
            TextureHandle cur_handle = impl_->spriteQueue[batch_start].texture;
            const TextureEntry* tex_entry = impl_->textures.get(cur_handle);
            if (tex_entry && tex_entry->gpu_texture) {
                SDL_GPUTextureSamplerBinding ts_bind{};
                ts_bind.texture = tex_entry->gpu_texture;
                ts_bind.sampler = impl_->nearestSampler;
                SDL_BindGPUFragmentSamplers(pass, 0, &ts_bind, 1);
            }

            uint32_t index_offset = static_cast<uint32_t>(batch_start * 6);
            uint32_t index_count  = static_cast<uint32_t>((batch_end - batch_start) * 6);

            SDL_DrawGPUIndexedPrimitives(pass, index_count, 1, index_offset, 0, 0);
            ++impl_->drawCallCount;

            batch_start = batch_end;
        }
    }

    if (pass) {
        SDL_EndGPURenderPass(pass);
    }
}

void RendererResource::present() {
    if (impl_->cmdBuf) {
        SDL_SubmitGPUCommandBuffer(impl_->cmdBuf);
        impl_->cmdBuf = nullptr;
    }
}

// =============================================================================
// Clear colour
// =============================================================================

void RendererResource::setClearColor(ColorF color) {
    impl_->clearColor = color;
}

// =============================================================================
// Texture API
// =============================================================================

TextureHandle RendererResource::loadTexture(const char* path) {
    TextureHandle result;

    if (!path) return result;

    int w = 0, h = 0, channels = 0;
    uint8_t* pixels = stbi_load(path, &w, &h, &channels, 4); // force RGBA
    if (!pixels) {
        SDL_Log("RendererResource::loadTexture: failed to load '%s': %s",
                path, stbi_failure_reason());
        return result;
    }

    SDL_GPUDevice* dev = impl_->device;
    if (!dev) {
        stbi_image_free(pixels);
        return result;
    }

    SDL_GPUTexture* gpu_tex = create_gpu_texture(dev, w, h, pixels);
    stbi_image_free(pixels);

    if (!gpu_tex) return result;

    TextureEntry entry;
    entry.gpu_texture = gpu_tex;
    entry.width       = static_cast<int32_t>(w);
    entry.height      = static_cast<int32_t>(h);

    result = impl_->textures.create(entry);

    SDL_Log("RendererResource::loadTexture: loaded '%s' as handle %u (%dx%d)",
            path, result.id, w, h);
    return result;
}

TextureHandle RendererResource::createTextureFromPixels(const uint8_t* pixels, int w, int h) {
    TextureHandle result;

    SDL_GPUDevice* dev = impl_->device;
    if (!dev || !pixels || w <= 0 || h <= 0) return result;

    SDL_GPUTexture* gpu_tex = create_gpu_texture(dev, w, h, pixels);
    if (!gpu_tex) return result;

    TextureEntry entry;
    entry.gpu_texture = gpu_tex;
    entry.width       = static_cast<int32_t>(w);
    entry.height      = static_cast<int32_t>(h);

    result = impl_->textures.create(entry);
    return result;
}

void RendererResource::destroyTexture(TextureHandle texture) {
    const TextureEntry* entry = impl_->textures.get(texture);
    if (!entry) return;

    SDL_GPUDevice* dev = impl_->device;
    if (dev && entry->gpu_texture) {
        SDL_ReleaseGPUTexture(dev, entry->gpu_texture);
    }

    impl_->textures.destroy(texture);
}

void RendererResource::getTextureSize(TextureHandle texture, int32_t* w, int32_t* h) const {
    const TextureEntry* entry = impl_->textures.get(texture);
    if (!entry) {
        if (w) *w = 0;
        if (h) *h = 0;
        return;
    }
    if (w) *w = entry->width;
    if (h) *h = entry->height;
}

// =============================================================================
// Sprite drawing
// =============================================================================

void RendererResource::drawSprite(TextureHandle texture, Vec2 position, Rect srcRect,
                                  Vec2 scale, float rotation, Vec2 origin,
                                  Color tint, Flip flip, float zOrder) {
    const TextureEntry* entry = impl_->textures.get(texture);
    if (!entry) {
        return; // invalid texture
    }

    SpriteInstance inst;
    inst.position   = position;
    inst.scale      = scale;
    inst.rotation   = rotation;
    inst.src_rect   = srcRect;
    inst.tint       = tint;
    inst.origin     = origin;
    inst.flip       = flip;
    inst.z_order    = zOrder;
    inst.texture    = texture;
    inst.tex_w      = entry->width;
    inst.tex_h      = entry->height;

    // If src_rect is zero-sized, use full texture.
    if (inst.src_rect.w <= 0.0f || inst.src_rect.h <= 0.0f) {
        inst.src_rect.x = 0.0f;
        inst.src_rect.y = 0.0f;
        inst.src_rect.w = static_cast<float>(inst.tex_w);
        inst.src_rect.h = static_cast<float>(inst.tex_h);
    }

    impl_->spriteQueue.push_back(inst);
}

void RendererResource::drawSprite(TextureHandle texture, Vec2 position, float zOrder) {
    drawSprite(texture, position, {}, Vec2::one(), 0.f, {}, Color::white(), Flip::None, zOrder);
}

void RendererResource::drawSprite(TextureHandle texture, Vec2 position, Rect srcRect, float zOrder) {
    drawSprite(texture, position, srcRect, Vec2::one(), 0.f, {}, Color::white(), Flip::None, zOrder);
}

// =============================================================================
// Primitive drawing (stubs -- TODO)
// =============================================================================

void RendererResource::drawRect(Rect rect, Color color) {
    (void)rect; (void)color;
    // TODO: Implement rectangle outline drawing.
    // Will render four line segments forming the rectangle border.
}

void RendererResource::drawRectFilled(Rect rect, Color color) {
    (void)rect; (void)color;
    // TODO: Implement filled rectangle drawing.
    // Will use the white texture with a coloured quad.
}

void RendererResource::drawLine(Vec2 start, Vec2 end, Color color, float thickness) {
    (void)start; (void)end; (void)color; (void)thickness;
    // TODO: Implement line drawing.
    // Will generate a rotated quad from start to end with the given thickness.
}

void RendererResource::drawCircle(Vec2 center, float radius, Color color, int segments) {
    (void)center; (void)radius; (void)color; (void)segments;
    // TODO: Implement circle outline drawing.
    // Will generate a line strip of `segments` edges approximating the circle.
}

// =============================================================================
// Camera API
// =============================================================================

CameraHandle RendererResource::createCamera() {
    CameraData data;
    data.position = {0.0f, 0.0f};
    data.zoom     = 1.0f;
    data.rotation = 0.0f;

    return impl_->cameras.create(data);
}

void RendererResource::destroyCamera(CameraHandle camera) {
    if (!impl_->cameras.valid(camera)) return;
    if (camera == impl_->defaultCamera) return; // cannot destroy default camera

    if (impl_->activeCamera == camera) {
        impl_->activeCamera = impl_->defaultCamera;
    }

    impl_->cameras.destroy(camera);
}

CameraHandle RendererResource::getDefaultCamera() const {
    return impl_->defaultCamera;
}

void RendererResource::setActiveCamera(CameraHandle camera) {
    if (impl_->cameras.valid(camera)) {
        impl_->activeCamera = camera;
    }
}

void RendererResource::setCameraPosition(CameraHandle camera, Vec2 position) {
    CameraData* cam = impl_->cameras.get(camera);
    if (cam) {
        cam->position = position;
    }
}

void RendererResource::setCameraZoom(CameraHandle camera, float zoom) {
    CameraData* cam = impl_->cameras.get(camera);
    if (cam) {
        cam->zoom = (zoom > 0.0001f) ? zoom : 0.0001f;
    }
}

void RendererResource::setCameraRotation(CameraHandle camera, float rotation) {
    CameraData* cam = impl_->cameras.get(camera);
    if (cam) {
        cam->rotation = rotation;
    }
}

Vec2 RendererResource::getCameraPosition(CameraHandle camera) const {
    const CameraData* cam = impl_->cameras.get(camera);
    if (cam) {
        return cam->position;
    }
    return Vec2{0.0f, 0.0f};
}

float RendererResource::getCameraZoom(CameraHandle camera) const {
    const CameraData* cam = impl_->cameras.get(camera);
    if (cam) {
        return cam->zoom;
    }
    return 1.0f;
}

Vec2 RendererResource::screenToWorld(CameraHandle camera, Vec2 screenPos) const {
    const CameraData* cam = impl_->cameras.get(camera);
    if (!cam) {
        return screenPos;
    }

    int w_px = 0, h_px = 0;
    SDL_GetWindowSize(impl_->window, &w_px, &h_px);
    float wf = static_cast<float>(w_px);
    float hf = static_cast<float>(h_px);

    // Reverse of the view transform:
    //   world = cam.pos + rotate(cam.rot, (screen - screen_centre) / cam.zoom)
    float dx = (screenPos.x - wf * 0.5f) / cam->zoom;
    float dy = (screenPos.y - hf * 0.5f) / cam->zoom;

    float cosR = cosf(cam->rotation);
    float sinR = sinf(cam->rotation);

    Vec2 world;
    world.x = cam->position.x + dx * cosR - dy * sinR;
    world.y = cam->position.y + dx * sinR + dy * cosR;
    return world;
}

Vec2 RendererResource::worldToScreen(CameraHandle camera, Vec2 worldPos) const {
    const CameraData* cam = impl_->cameras.get(camera);
    if (!cam) {
        return worldPos;
    }

    int w_px = 0, h_px = 0;
    SDL_GetWindowSize(impl_->window, &w_px, &h_px);
    float wf = static_cast<float>(w_px);
    float hf = static_cast<float>(h_px);

    float dx = worldPos.x - cam->position.x;
    float dy = worldPos.y - cam->position.y;

    float cosR = cosf(-cam->rotation);
    float sinR = sinf(-cam->rotation);

    Vec2 screen;
    screen.x = (dx * cosR - dy * sinR) * cam->zoom + wf * 0.5f;
    screen.y = (dx * sinR + dy * cosR) * cam->zoom + hf * 0.5f;
    return screen;
}

// =============================================================================
// Stats
// =============================================================================

int32_t RendererResource::drawCalls() const {
    return impl_->drawCallCount;
}

int32_t RendererResource::spriteCount() const {
    return impl_->spriteCountVal;
}

// =============================================================================
// Internal access for other subsystems
// =============================================================================

void* RendererResource::getGPUDevice() const {
    return impl_->device;
}

void* RendererResource::getWindow() const {
    return impl_->window;
}

} // namespace drift
