// =============================================================================
// Drift 2D Game Engine - Renderer Implementation
// =============================================================================
// Implements the public drift/renderer.h API using SDL3 GPU API.
// Sprite batching, texture management, camera system, and primitive stubs.
// =============================================================================

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <SDL3/SDL.h>
#include <drift/renderer.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

// -----------------------------------------------------------------------------
// External linkage to core module (defined in drift.cpp)
// -----------------------------------------------------------------------------
extern "C" SDL_Window*    drift_internal_get_window(void);
extern "C" SDL_GPUDevice* drift_internal_get_gpu_device(void);

// =============================================================================
// Internal types and state (anonymous namespace)
// =============================================================================
namespace {

// ---- Texture storage --------------------------------------------------------

struct TextureEntry {
    SDL_GPUTexture* gpu_texture;
    int32_t         width;
    int32_t         height;
    bool            alive;           // false = slot freed
};

static std::vector<TextureEntry> g_textures;     // index 0 unused (ids start at 1)
static uint32_t                  g_next_tex_id = 1;

// ---- Sprite batching --------------------------------------------------------

struct SpriteInstance {
    DriftVec2   position;
    DriftVec2   scale;
    float       rotation;
    DriftRect   src_rect;            // in pixel coordinates of the source texture
    DriftColor  tint;
    DriftVec2   origin;
    DriftFlip   flip;
    float       z_order;
    uint32_t    texture_id;
    int32_t     tex_w;               // cached texture width  (for UV calc)
    int32_t     tex_h;               // cached texture height (for UV calc)
};

static std::vector<SpriteInstance> g_sprite_queue;

// ---- Per-vertex layout sent to the GPU --------------------------------------

struct SpriteVertex {
    float x, y;                      // position (world -> projected)
    float u, v;                      // texture coordinates
    uint8_t r, g, b, a;             // vertex colour / tint
};

// ---- Camera storage ---------------------------------------------------------

struct CameraData {
    DriftVec2 position;
    float     zoom;
    float     rotation;
    bool      alive;
};

static std::vector<CameraData> g_cameras;         // index 0 unused
static uint32_t                g_next_cam_id = 1;
static uint32_t                g_active_camera = 0;

// ---- Renderer state ---------------------------------------------------------

static DriftColorF g_clear_color = {0.0f, 0.0f, 0.0f, 1.0f};

static SDL_GPUGraphicsPipeline* g_sprite_pipeline   = nullptr;
static SDL_GPUBuffer*           g_vertex_buffer      = nullptr;
static SDL_GPUBuffer*           g_index_buffer       = nullptr;
static uint32_t                 g_vertex_buf_cap     = 0;  // in bytes
static uint32_t                 g_index_buf_cap      = 0;

static SDL_GPUSampler*          g_nearest_sampler    = nullptr;

static SDL_GPUCommandBuffer*    g_cmd_buf            = nullptr;

// ---- Stats ------------------------------------------------------------------

static int32_t g_draw_calls   = 0;
static int32_t g_sprite_count = 0;

// ---- White 1x1 fallback texture (used for untextured prims in the future) ---

static uint32_t g_white_texture_id = 0;

// =============================================================================
// Helper: build a simple 3x3 view-projection matrix (see sprite_batch.cpp too)
// =============================================================================
// We use column-major float[9] for a 3x3 affine matrix operating in 2D.
// The projection is orthographic mapping screen-pixel space to clip [-1,1].

struct Mat3 {
    float m[9]; // column-major
};

static Mat3 mat3_identity() {
    Mat3 r{};
    r.m[0] = 1.0f; r.m[4] = 1.0f; r.m[8] = 1.0f;
    return r;
}

static Mat3 mat3_multiply(const Mat3& a, const Mat3& b) {
    Mat3 r{};
    for (int c = 0; c < 3; ++c)
        for (int row = 0; row < 3; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 3; ++k)
                sum += a.m[k * 3 + row] * b.m[c * 3 + k];
            r.m[c * 3 + row] = sum;
        }
    return r;
}

static Mat3 mat3_translate(float tx, float ty) {
    Mat3 r = mat3_identity();
    r.m[6] = tx;
    r.m[7] = ty;
    return r;
}

static Mat3 mat3_rotate(float radians) {
    Mat3 r = mat3_identity();
    float c = cosf(radians);
    float s = sinf(radians);
    r.m[0] =  c; r.m[3] = -s;
    r.m[1] =  s; r.m[4] =  c;
    return r;
}

static Mat3 mat3_scale(float sx, float sy) {
    Mat3 r = mat3_identity();
    r.m[0] = sx;
    r.m[4] = sy;
    return r;
}

// Orthographic projection: maps (0..w, 0..h) -> (-1..1, -1..1) with Y-down.
static Mat3 mat3_ortho(float w, float h) {
    Mat3 r{};
    r.m[0] =  2.0f / w;
    r.m[4] = -2.0f / h;   // flip Y so that +Y = down (screen coords)
    r.m[6] = -1.0f;
    r.m[7] =  1.0f;
    r.m[8] =  1.0f;
    return r;
}

static void mat3_transform(const Mat3& m, float x, float y, float* ox, float* oy) {
    *ox = m.m[0] * x + m.m[3] * y + m.m[6];
    *oy = m.m[1] * x + m.m[4] * y + m.m[7];
}

// Build the combined view-projection matrix from the active camera and window.
static Mat3 build_view_projection() {
    int w_px = 0, h_px = 0;
    SDL_GetWindowSize(drift_internal_get_window(), &w_px, &h_px);
    if (w_px == 0 || h_px == 0) {
        return mat3_identity();
    }

    float wf = static_cast<float>(w_px);
    float hf = static_cast<float>(h_px);

    Mat3 proj = mat3_ortho(wf, hf);

    if (g_active_camera == 0) {
        return proj; // no camera -> identity view
    }

    const CameraData& cam = g_cameras[g_active_camera];

    // View = translate(-cam.pos) * rotate(-cam.rot) * scale(cam.zoom)
    // Then shift so that the camera centre is at the screen centre.
    Mat3 view = mat3_identity();
    view = mat3_multiply(mat3_translate(-cam.position.x, -cam.position.y), view);
    view = mat3_multiply(mat3_rotate(-cam.rotation), view);
    view = mat3_multiply(mat3_scale(cam.zoom, cam.zoom), view);
    view = mat3_multiply(mat3_translate(wf * 0.5f, hf * 0.5f), view);

    return mat3_multiply(proj, view);
}

// =============================================================================
// Helpers: GPU resource management
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
        SDL_Log("drift_renderer: failed to create GPU texture (%dx%d): %s",
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
        SDL_Log("drift_renderer: failed to create transfer buffer: %s", SDL_GetError());
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
// Sprite sort comparator: sort by z_order then by texture_id for batching.
// =============================================================================
static bool sprite_sort_cmp(const SpriteInstance& a, const SpriteInstance& b) {
    if (a.z_order != b.z_order) return a.z_order < b.z_order;
    return a.texture_id < b.texture_id;
}

// ---- Embedded SPIR-V shader bytecode ----------------------------------------

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

} // anonymous namespace

// =============================================================================
//  PUBLIC API IMPLEMENTATION
// =============================================================================

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------

DriftResult drift_renderer_init(void) {
    SDL_GPUDevice* dev = drift_internal_get_gpu_device();
    if (!dev) {
        SDL_Log("drift_renderer_init: GPU device not available.");
        return DRIFT_ERROR_INIT_FAILED;
    }

    // Reserve index 0 in containers (ids start at 1).
    g_textures.clear();
    g_textures.push_back(TextureEntry{nullptr, 0, 0, false});
    g_next_tex_id = 1;

    g_cameras.clear();
    g_cameras.push_back(CameraData{{0, 0}, 1.0f, 0.0f, false});
    g_next_cam_id = 1;
    g_active_camera = 0;

    g_sprite_queue.clear();
    g_draw_calls   = 0;
    g_sprite_count = 0;

    // Create a nearest-neighbour sampler (good default for pixel art).
    SDL_GPUSamplerCreateInfo samp_ci{};
    samp_ci.min_filter       = SDL_GPU_FILTER_NEAREST;
    samp_ci.mag_filter       = SDL_GPU_FILTER_NEAREST;
    samp_ci.mipmap_mode      = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    samp_ci.address_mode_u   = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samp_ci.address_mode_v   = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samp_ci.address_mode_w   = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

    g_nearest_sampler = SDL_CreateGPUSampler(dev, &samp_ci);

    // Create a 1x1 white texture for untextured draws.
    uint8_t white_pixel[4] = {255, 255, 255, 255};
    SDL_GPUTexture* white_gpu = create_gpu_texture(dev, 1, 1, white_pixel);
    if (white_gpu) {
        uint32_t id = g_next_tex_id++;
        if (id >= g_textures.size()) {
            g_textures.resize(id + 1, TextureEntry{nullptr, 0, 0, false});
        }
        g_textures[id] = TextureEntry{white_gpu, 1, 1, true};
        g_white_texture_id = id;
    }

    // ------------------------------------------------------------------
    // Create sprite rendering pipeline with embedded SPIR-V shaders.
    // ------------------------------------------------------------------
    SDL_Window* win = drift_internal_get_window();
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

            g_sprite_pipeline = SDL_CreateGPUGraphicsPipeline(dev, &pip_ci);
            if (!g_sprite_pipeline) {
                SDL_Log("drift_renderer_init: failed to create sprite pipeline: %s",
                        SDL_GetError());
            }
        } else {
            SDL_Log("drift_renderer_init: failed to create GPU shaders: vert=%p frag=%p: %s",
                    (void*)vert_shader, (void*)frag_shader, SDL_GetError());
        }

        if (vert_shader) SDL_ReleaseGPUShader(dev, vert_shader);
        if (frag_shader) SDL_ReleaseGPUShader(dev, frag_shader);
    } else {
        SDL_Log("drift_renderer_init: SPIR-V not supported or no window. Sprites won't render.");
        g_sprite_pipeline = nullptr;
    }

    SDL_Log("drift_renderer_init: renderer initialised.");
    return DRIFT_OK;
}

void drift_renderer_shutdown(void) {
    SDL_GPUDevice* dev = drift_internal_get_gpu_device();
    if (!dev) return;

    // Wait for the GPU to be idle before releasing resources.
    SDL_WaitForGPUIdle(dev);

    // Release textures.
    for (auto& t : g_textures) {
        if (t.alive && t.gpu_texture) {
            SDL_ReleaseGPUTexture(dev, t.gpu_texture);
        }
    }
    g_textures.clear();
    g_next_tex_id = 1;

    // Release sampler.
    if (g_nearest_sampler) {
        SDL_ReleaseGPUSampler(dev, g_nearest_sampler);
        g_nearest_sampler = nullptr;
    }

    // Release buffers.
    if (g_vertex_buffer) {
        SDL_ReleaseGPUBuffer(dev, g_vertex_buffer);
        g_vertex_buffer = nullptr;
        g_vertex_buf_cap = 0;
    }
    if (g_index_buffer) {
        SDL_ReleaseGPUBuffer(dev, g_index_buffer);
        g_index_buffer = nullptr;
        g_index_buf_cap = 0;
    }

    // Release pipeline.
    if (g_sprite_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(dev, g_sprite_pipeline);
        g_sprite_pipeline = nullptr;
    }

    g_cameras.clear();
    g_next_cam_id = 1;
    g_active_camera = 0;

    g_sprite_queue.clear();

    SDL_Log("drift_renderer_shutdown: renderer shut down.");
}

// -----------------------------------------------------------------------------
// Frame
// -----------------------------------------------------------------------------

void drift_renderer_begin_frame(void) {
    g_sprite_queue.clear();
    g_draw_calls   = 0;
    g_sprite_count = 0;

    SDL_GPUDevice* dev = drift_internal_get_gpu_device();
    if (dev) {
        g_cmd_buf = SDL_AcquireGPUCommandBuffer(dev);
    }
}

void drift_renderer_end_frame(void) {
    if (!g_cmd_buf) return;

    SDL_GPUDevice* dev = drift_internal_get_gpu_device();
    SDL_Window*    win = drift_internal_get_window();
    if (!dev || !win) return;

    // --- Sort sprites by z_order then texture for batching ---
    std::sort(g_sprite_queue.begin(), g_sprite_queue.end(), sprite_sort_cmp);

    // --- Build vertex / index data ---
    const size_t sprite_count = g_sprite_queue.size();
    g_sprite_count = static_cast<int32_t>(sprite_count);

    std::vector<SpriteVertex> vertices;
    std::vector<uint16_t>     indices;
    vertices.reserve(sprite_count * 4);
    indices.reserve(sprite_count * 6);

    Mat3 vp = build_view_projection();

    for (size_t i = 0; i < sprite_count; ++i) {
        const SpriteInstance& s = g_sprite_queue[i];

        // Source rect UVs (normalised).
        float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
        if (s.tex_w > 0 && s.tex_h > 0) {
            u0 = s.src_rect.x / static_cast<float>(s.tex_w);
            v0 = s.src_rect.y / static_cast<float>(s.tex_h);
            u1 = (s.src_rect.x + s.src_rect.w) / static_cast<float>(s.tex_w);
            v1 = (s.src_rect.y + s.src_rect.h) / static_cast<float>(s.tex_h);
        }

        // Handle flipping.
        if (static_cast<int>(s.flip) & static_cast<int>(DRIFT_FLIP_H)) {
            float tmp = u0; u0 = u1; u1 = tmp;
        }
        if (static_cast<int>(s.flip) & static_cast<int>(DRIFT_FLIP_V)) {
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
            mat3_transform(vp, wx, wy, &px, &py);

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
        ensure_gpu_buffer(dev, &g_vertex_buffer, &g_vertex_buf_cap, vb_size,
                          SDL_GPU_BUFFERUSAGE_VERTEX);
        upload_to_gpu_buffer(dev, g_vertex_buffer, vertices.data(), vb_size);
    }
    if (ib_size > 0) {
        ensure_gpu_buffer(dev, &g_index_buffer, &g_index_buf_cap, ib_size,
                          SDL_GPU_BUFFERUSAGE_INDEX);
        upload_to_gpu_buffer(dev, g_index_buffer, indices.data(), ib_size);
    }

    // --- Acquire swapchain texture and begin render pass ---
    SDL_GPUTexture* swapchain_tex = nullptr;
    Uint32 sc_w = 0, sc_h = 0;
    if (!SDL_AcquireGPUSwapchainTexture(g_cmd_buf, win, &swapchain_tex, &sc_w, &sc_h)) {
        SDL_Log("drift_renderer: failed to acquire swapchain texture.");
        SDL_SubmitGPUCommandBuffer(g_cmd_buf);
        g_cmd_buf = nullptr;
        return;
    }
    if (!swapchain_tex) {
        // Window minimised or not ready; just submit and bail.
        SDL_SubmitGPUCommandBuffer(g_cmd_buf);
        g_cmd_buf = nullptr;
        return;
    }

    SDL_GPUColorTargetInfo color_target{};
    color_target.texture     = swapchain_tex;
    color_target.load_op     = SDL_GPU_LOADOP_CLEAR;
    color_target.store_op    = SDL_GPU_STOREOP_STORE;
    color_target.clear_color.r = g_clear_color.r;
    color_target.clear_color.g = g_clear_color.g;
    color_target.clear_color.b = g_clear_color.b;
    color_target.clear_color.a = g_clear_color.a;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(g_cmd_buf, &color_target, 1, nullptr);

    // --- Issue batched draw calls (one per texture run) ---
    if (pass && g_sprite_pipeline && sprite_count > 0) {
        SDL_BindGPUGraphicsPipeline(pass, g_sprite_pipeline);

        SDL_GPUBufferBinding vb_bind{};
        vb_bind.buffer = g_vertex_buffer;
        vb_bind.offset = 0;
        SDL_BindGPUVertexBuffers(pass, 0, &vb_bind, 1);

        SDL_GPUBufferBinding ib_bind{};
        ib_bind.buffer = g_index_buffer;
        ib_bind.offset = 0;
        SDL_BindGPUIndexBuffer(pass, &ib_bind, SDL_GPU_INDEXELEMENTSIZE_16BIT);

        // Walk sorted sprites and emit one draw per texture batch.
        size_t batch_start = 0;
        while (batch_start < sprite_count) {
            uint32_t cur_tex = g_sprite_queue[batch_start].texture_id;
            size_t batch_end = batch_start + 1;
            while (batch_end < sprite_count &&
                   g_sprite_queue[batch_end].texture_id == cur_tex) {
                ++batch_end;
            }

            // Bind the texture + sampler for this batch.
            if (cur_tex > 0 && cur_tex < g_textures.size() &&
                g_textures[cur_tex].alive) {
                SDL_GPUTextureSamplerBinding ts_bind{};
                ts_bind.texture = g_textures[cur_tex].gpu_texture;
                ts_bind.sampler = g_nearest_sampler;
                SDL_BindGPUFragmentSamplers(pass, 0, &ts_bind, 1);
            }

            uint32_t index_offset = static_cast<uint32_t>(batch_start * 6);
            uint32_t index_count  = static_cast<uint32_t>((batch_end - batch_start) * 6);

            SDL_DrawGPUIndexedPrimitives(pass, index_count, 1, index_offset, 0, 0);
            ++g_draw_calls;

            batch_start = batch_end;
        }
    }

    if (pass) {
        SDL_EndGPURenderPass(pass);
    }
}

void drift_renderer_present(void) {
    if (g_cmd_buf) {
        SDL_SubmitGPUCommandBuffer(g_cmd_buf);
        g_cmd_buf = nullptr;
    }
}

void drift_renderer_set_clear_color(DriftColorF color) {
    g_clear_color = color;
}

// -----------------------------------------------------------------------------
// Texture API
// -----------------------------------------------------------------------------

DriftTexture drift_texture_load(const char* path) {
    DriftTexture result;
    result.id = 0;

    if (!path) return result;

    int w = 0, h = 0, channels = 0;
    uint8_t* pixels = stbi_load(path, &w, &h, &channels, 4); // force RGBA
    if (!pixels) {
        SDL_Log("drift_texture_load: failed to load '%s': %s", path, stbi_failure_reason());
        return result;
    }

    SDL_GPUDevice* dev = drift_internal_get_gpu_device();
    if (!dev) {
        stbi_image_free(pixels);
        return result;
    }

    SDL_GPUTexture* gpu_tex = create_gpu_texture(dev, w, h, pixels);
    stbi_image_free(pixels);

    if (!gpu_tex) return result;

    uint32_t id = g_next_tex_id++;
    if (id >= g_textures.size()) {
        g_textures.resize(id + 1, TextureEntry{nullptr, 0, 0, false});
    }
    g_textures[id] = TextureEntry{gpu_tex, static_cast<int32_t>(w),
                                  static_cast<int32_t>(h), true};

    result.id = id;
    SDL_Log("drift_texture_load: loaded '%s' as id %u (%dx%d)", path, id, w, h);
    return result;
}

void drift_texture_destroy(DriftTexture texture) {
    uint32_t id = texture.id;
    if (id == 0 || id >= g_textures.size()) return;
    if (!g_textures[id].alive) return;

    SDL_GPUDevice* dev = drift_internal_get_gpu_device();
    if (dev && g_textures[id].gpu_texture) {
        SDL_ReleaseGPUTexture(dev, g_textures[id].gpu_texture);
    }
    g_textures[id].gpu_texture = nullptr;
    g_textures[id].alive = false;
}

void drift_texture_get_size(DriftTexture texture, int32_t* w, int32_t* h) {
    uint32_t id = texture.id;
    if (id == 0 || id >= g_textures.size() || !g_textures[id].alive) {
        if (w) *w = 0;
        if (h) *h = 0;
        return;
    }
    if (w) *w = g_textures[id].width;
    if (h) *h = g_textures[id].height;
}

// -----------------------------------------------------------------------------
// Sprite drawing
// -----------------------------------------------------------------------------

void drift_sprite_draw(DriftTexture texture,
                       DriftVec2    position,
                       DriftRect    src_rect,
                       DriftVec2    scale,
                       float        rotation,
                       DriftVec2    origin,
                       DriftColor   tint,
                       DriftFlip    flip,
                       float        z_order) {
    uint32_t id = texture.id;
    if (id == 0 || id >= g_textures.size() || !g_textures[id].alive) {
        return; // invalid texture
    }

    SpriteInstance inst;
    inst.position   = position;
    inst.scale      = scale;
    inst.rotation   = rotation;
    inst.src_rect   = src_rect;
    inst.tint       = tint;
    inst.origin     = origin;
    inst.flip       = flip;
    inst.z_order    = z_order;
    inst.texture_id = id;
    inst.tex_w      = g_textures[id].width;
    inst.tex_h      = g_textures[id].height;

    // If src_rect is zero-sized, use full texture.
    if (inst.src_rect.w <= 0.0f || inst.src_rect.h <= 0.0f) {
        inst.src_rect.x = 0.0f;
        inst.src_rect.y = 0.0f;
        inst.src_rect.w = static_cast<float>(inst.tex_w);
        inst.src_rect.h = static_cast<float>(inst.tex_h);
    }

    g_sprite_queue.push_back(inst);
}

// -----------------------------------------------------------------------------
// Primitive drawing (stubs -- TODO)
// -----------------------------------------------------------------------------

void drift_draw_rect(DriftRect rect, DriftColor color) {
    (void)rect; (void)color;
    // TODO: Implement rectangle outline drawing.
    // Will render four line segments forming the rectangle border.
    SDL_Log("drift_draw_rect: not yet implemented.");
}

void drift_draw_rect_filled(DriftRect rect, DriftColor color) {
    (void)rect; (void)color;
    // TODO: Implement filled rectangle drawing.
    // Will use the white texture with a coloured quad.
    SDL_Log("drift_draw_rect_filled: not yet implemented.");
}

void drift_draw_line(DriftVec2 start, DriftVec2 end, DriftColor color, float thickness) {
    (void)start; (void)end; (void)color; (void)thickness;
    // TODO: Implement line drawing.
    // Will generate a rotated quad from start to end with the given thickness.
    SDL_Log("drift_draw_line: not yet implemented.");
}

void drift_draw_circle(DriftVec2 center, float radius, DriftColor color, int segments) {
    (void)center; (void)radius; (void)color; (void)segments;
    // TODO: Implement circle outline drawing.
    // Will generate a line strip of `segments` edges approximating the circle.
    SDL_Log("drift_draw_circle: not yet implemented.");
}

// -----------------------------------------------------------------------------
// Camera API
// -----------------------------------------------------------------------------

DriftCamera drift_camera_create(void) {
    uint32_t id = g_next_cam_id++;
    if (id >= g_cameras.size()) {
        g_cameras.resize(id + 1, CameraData{{0, 0}, 1.0f, 0.0f, false});
    }
    g_cameras[id] = CameraData{{0.0f, 0.0f}, 1.0f, 0.0f, true};

    DriftCamera cam;
    cam.id = id;
    return cam;
}

void drift_camera_destroy(DriftCamera camera) {
    uint32_t id = camera.id;
    if (id == 0 || id >= g_cameras.size()) return;
    g_cameras[id].alive = false;
    if (g_active_camera == id) {
        g_active_camera = 0;
    }
}

void drift_camera_set_active(DriftCamera camera) {
    uint32_t id = camera.id;
    if (id > 0 && id < g_cameras.size() && g_cameras[id].alive) {
        g_active_camera = id;
    }
}

void drift_camera_set_position(DriftCamera camera, DriftVec2 position) {
    uint32_t id = camera.id;
    if (id > 0 && id < g_cameras.size() && g_cameras[id].alive) {
        g_cameras[id].position = position;
    }
}

void drift_camera_set_zoom(DriftCamera camera, float zoom) {
    uint32_t id = camera.id;
    if (id > 0 && id < g_cameras.size() && g_cameras[id].alive) {
        g_cameras[id].zoom = (zoom > 0.0001f) ? zoom : 0.0001f;
    }
}

void drift_camera_set_rotation(DriftCamera camera, float rotation) {
    uint32_t id = camera.id;
    if (id > 0 && id < g_cameras.size() && g_cameras[id].alive) {
        g_cameras[id].rotation = rotation;
    }
}

DriftVec2 drift_camera_get_position(DriftCamera camera) {
    uint32_t id = camera.id;
    if (id > 0 && id < g_cameras.size() && g_cameras[id].alive) {
        return g_cameras[id].position;
    }
    return DriftVec2{0.0f, 0.0f};
}

float drift_camera_get_zoom(DriftCamera camera) {
    uint32_t id = camera.id;
    if (id > 0 && id < g_cameras.size() && g_cameras[id].alive) {
        return g_cameras[id].zoom;
    }
    return 1.0f;
}

DriftVec2 drift_camera_screen_to_world(DriftCamera camera, DriftVec2 screen_pos) {
    uint32_t id = camera.id;
    if (id == 0 || id >= g_cameras.size() || !g_cameras[id].alive) {
        return screen_pos;
    }

    const CameraData& cam = g_cameras[id];

    int w_px = 0, h_px = 0;
    SDL_GetWindowSize(drift_internal_get_window(), &w_px, &h_px);
    float wf = static_cast<float>(w_px);
    float hf = static_cast<float>(h_px);

    // Reverse of the view transform:
    //   world = cam.pos + rotate(cam.rot, (screen - screen_centre) / cam.zoom)
    float dx = (screen_pos.x - wf * 0.5f) / cam.zoom;
    float dy = (screen_pos.y - hf * 0.5f) / cam.zoom;

    float cosR = cosf(cam.rotation);
    float sinR = sinf(cam.rotation);

    DriftVec2 world;
    world.x = cam.position.x + dx * cosR - dy * sinR;
    world.y = cam.position.y + dx * sinR + dy * cosR;
    return world;
}

DriftVec2 drift_camera_world_to_screen(DriftCamera camera, DriftVec2 world_pos) {
    uint32_t id = camera.id;
    if (id == 0 || id >= g_cameras.size() || !g_cameras[id].alive) {
        return world_pos;
    }

    const CameraData& cam = g_cameras[id];

    int w_px = 0, h_px = 0;
    SDL_GetWindowSize(drift_internal_get_window(), &w_px, &h_px);
    float wf = static_cast<float>(w_px);
    float hf = static_cast<float>(h_px);

    float dx = world_pos.x - cam.position.x;
    float dy = world_pos.y - cam.position.y;

    float cosR = cosf(-cam.rotation);
    float sinR = sinf(-cam.rotation);

    DriftVec2 screen;
    screen.x = (dx * cosR - dy * sinR) * cam.zoom + wf * 0.5f;
    screen.y = (dx * sinR + dy * cosR) * cam.zoom + hf * 0.5f;
    return screen;
}

// -----------------------------------------------------------------------------
// Stats
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Internal: create texture from raw RGBA pixels (used by font system, etc.)
// -----------------------------------------------------------------------------

extern "C" DriftTexture drift_internal_texture_create_from_pixels(const uint8_t* pixels, int w, int h) {
    DriftTexture result;
    result.id = 0;

    SDL_GPUDevice* dev = drift_internal_get_gpu_device();
    if (!dev || !pixels || w <= 0 || h <= 0) return result;

    SDL_GPUTexture* gpu_tex = create_gpu_texture(dev, w, h, pixels);
    if (!gpu_tex) return result;

    uint32_t id = g_next_tex_id++;
    if (id >= g_textures.size()) {
        g_textures.resize(id + 1, TextureEntry{nullptr, 0, 0, false});
    }
    g_textures[id] = TextureEntry{gpu_tex, static_cast<int32_t>(w),
                                  static_cast<int32_t>(h), true};
    result.id = id;
    return result;
}

int32_t drift_renderer_get_draw_calls(void) {
    return g_draw_calls;
}

int32_t drift_renderer_get_sprite_count(void) {
    return g_sprite_count;
}
